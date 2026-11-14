#ifndef QTREGB_PPU_H
#define QTREGB_PPU_H

#include <vector>
#include <array>
#include <deque>
#include <functional>

#include "types.h"
#include "memory.h"
#include "ibus.h"
#include "oam_entry.h"
#include "videobus.h"

namespace qtreGB
{

class LCD;

/*
 Bit7   BG and Window over OBJ (0=No, 1=BG and Window colors 1-3 over the OBJ)
 Bit6   Y flip          (0=Normal, 1=Vertically mirrored)
 Bit5   X flip          (0=Normal, 1=Horizontally mirrored)
 Bit4   Palette number  **Non CGB Mode Only** (0=OBP0, 1=OBP1)
 Bit3   Tile VRAM-Bank  **CGB Mode Only**     (0=Bank 0, 1=Bank 1)
 Bit2-0 Palette number  **CGB Mode Only**     (OBP0-7)
 */

class PPU
{
	public:
		using FrameCallback = std::function<void(bool)>;

		void set_interrupt_callback(InterruptCallback cb);
    	void set_frame_callback(FrameCallback cb);

		static const int LINES_PER_FRAME;
		static const int TICKS_PER_LINE;
		static const int YRES;
		static const int XRES;

		PPU(LCD *lcd);
		void ppu_init(bool boot_rom = false);
		void ppu_tick();
		void ppu_sync_lcd_power();	// Evaluates the LCDC bit 7 change synchronously at the FF40 write, before the CPU's end-of-opcode interrupt check (mooneye stat_lyc_onoff)

		u8 ppu_oam_read(u16 address);
		void ppu_oam_write(u16 address, u8 value);
		void ppu_oam_write_dma(u16 address, u8 value);	// OAM DMA write: never blocked by the PPU bus locks (real DMA wins the bus)

		u8 ppu_vram_read(u16 address);
		void ppu_vram_write(u16 address, u8 value);

		const std::vector<u8>& get_video_buffer() const;

		// Bus ownership exposed to the CPU view. The PPU keeps these flags;
		// BusCpuView is the one that drops/returns 0xFF for CPU accesses.
		bool is_oam_read_locked() const { return m_oam_bus.is_acquired(VideoAccess::Read); }
		bool is_oam_write_locked() const { return m_oam_bus.is_acquired(VideoAccess::Write); }
		bool is_vram_read_locked() const { return m_vram_bus.is_acquired(VideoAccess::Read); }
		bool is_vram_write_locked() const { return m_vram_bus.is_acquired(VideoAccess::Write); }

		void connect_bus(IBus* bus);

		// Debug-only accessors for the temp trace tools.
		u16 debug_dots() const { return m_dots; }
		u8 debug_lx() const { return m_lx; }
		u8 debug_fetch_x() const { return m_bg_fetch_x; }
		u8 debug_pushed_x() const { return m_pushed_x; }
		int debug_fetch_phase() const { return static_cast<int>(m_fetch_phase); }
		bool debug_fetch_window() const { return m_fetch_window; }
		bool debug_fetching_sprite() const { return m_is_fetching_sprite; }
		size_t debug_bg_fifo() const { return m_bg_pixels.size(); }
		size_t debug_obj_fifo() const { return m_obj_pixels.size(); }
		bool debug_oam_pending() const { return oam_entry_pending(m_lx); }
		u8 debug_oam_served() const { return m_oam_served_count[m_lx]; }
		u8 debug_oam_entries() const { return static_cast<u8>(m_oam_entries[m_lx].size()); }
		int debug_obj_phase() const { return static_cast<int>(m_obj_fetch_phase); }
		u8 debug_obj_wait() const { return m_obj_fetch_wait; }
		u8 debug_post_wait() const { return m_obj_post_wait; }

	private:
		IBus *m_bus;
		LCD *m_lcd;
		Memory m_vram;
		std::vector<OAMEntry> m_oam_ram;	// OAM RAM
		u8 m_oam_scan_index = 0;	// OAM entry currently being scanned (0..39)
		u8 m_oam_scan_y = 0;	// Y byte read on the even dot of the current OAM entry
		u8 m_oam_scan_obj_height = 8;	// Sprite height captured at dot 0 of the scan

		// F5: per-LX sprite entries and pixel pipeline
		struct BgPixel
		{
			u8 color_index = 0;
			bool window = false;
		};

		struct ObjPixel
		{
			u8 color_index = 0;
			u8 palette = 0;
			u8 priority = 0;
			u8 number = 0;
			u8 x = 0;
		};

		struct OamLineEntry
		{
			OAMEntry entry;
			u8 number = 0;
		};

		enum class PixelTransferPhase
		{
			Dummy,
			Discard,
			Lx0,
			Lx8,
		};

		enum class FetchPhase
		{
			BgTilemap,
			BgLow,
			BgHigh,
			BgPush,
			ObjLow,
			ObjHigh,
			ObjPush,
		};

		std::array<std::vector<OamLineEntry>, 168> m_oam_entries;
		std::array<u8, 168> m_oam_served_count{};	// OBJ entries already served at each LX
		struct PendingPalettePixel
		{
			u16 dot;
			u8 x;
			u8 y;
			u8 color_index;
			u8 palette_sel;	// 0 = BGP, 1 = OBP0, 2 = OBP1
			bool window;
			u8 out_delay;	// Cumulative x_extra output delay before this pixel
			u8 obj_delay;	// Per-sprite OBJ palette delay
		};

		std::deque<BgPixel> m_bg_pixels;
		std::deque<PendingPalettePixel> m_pending_palette_pixels;
		u8 m_bg_palette_delay = 0;
		u8 m_line_obj_out_delay = 0;
		u8 m_win_palette_delay = 0;
		bool m_wx0_write_pending = false;	// WX changed to 0 this line
		bool m_wx0_line0_window = false;	// WX=0 window activated at lx=11
		u8 m_obj_palette_delay = 0;
		void flush_pending_palette(bool all);
		std::deque<ObjPixel> m_obj_pixels;

		u8 m_lx = 0;	// X + 8, 0..167
		u8 m_initial_scx_to_discard = 0;
		u8 m_initial_scx_discarded = 0;
		u8 m_bg_fetch_x = 0;	// pixel X inside the current BG fetch, advances 8 per tile
		u8 m_bg_fetch_skip = 0;	// Pixels discarded from the next BG tile after a mid-line SCX low-bit change
		u8 m_bg_prev_scx_low = 0;	// SCX low bits used by the previous BG fetch
		u8 m_bg_line_scx_low = 0;	// SCX low bits latched for the current line (sampled at the first real B fetch)
		u8 m_window_tile_x = 0;
		u8 m_oam_scan_count = 0;
		bool m_window_active = false;
		bool m_window_activating = false;	// Window trigger seen; activation happens on the next dot
		u8 m_window_activation_delay = 0;	// Extra dots before activation (WX=0 with SCX>0)
		bool m_window_active_for_frame = false;
		bool m_window_just_activated = false;
		bool m_is_fetching_sprite = false;
		OamLineEntry m_sprite_entry;
		u8 m_sprite_low = 0;
		u8 m_sprite_high = 0;
		u8 m_obj_fetch_wait = 0;	// Second dot of an OBJ access (2 dots per access)
		u8 m_obj_post_wait = 0;	// Dot after the OBJ push where the FIFO still waits
		u8 m_obj_penalty = 6;	// Total dots consumed by the current OBJ fetch
		u16 m_mode0_penalty_dots = 0;	// CPU-visible mode 3 extension from OBJ penalties
		u8 m_mode0_extra_correction = 0;	// Extra pullback for a lone X>0 OBJ
		u8 m_obj_group_total = 0;	// Number of sprites in the current same-X OBJ group
		u8 m_last_group_lx = 0xFF;	// LX of the current OBJ group
		bool m_obj_multiple_groups = false;	// The line has sprites in more than one X group
		std::vector<int> m_obj_considered_tiles;	// BG/window tiles already considered by an OBJ on this line
		u16 m_sprite_data_addr = 0;
		u8 m_sprite_pixels[8] = {};

		PixelTransferPhase m_pixel_transfer_phase = PixelTransferPhase::Dummy;
		FetchPhase m_fetch_phase = FetchPhase::BgTilemap;
		FetchPhase m_obj_fetch_phase = FetchPhase::ObjLow;
		u16 m_fetch_tilemap_addr = 0;
		u16 m_fetch_data_addr = 0;
		u8 m_fetch_tile_number = 0;
		u8 m_fetch_tile_y = 0;	// Tile row latched at the tilemap fetch
		u8 m_fetch_low = 0;
		u8 m_fetch_high = 0;
		u8 m_bg_fetch_wait = 0;	// Second dot of a window access (6-dot window startup)
		u8 m_scx_latch_dot = 89;	// Dot where SCX low bits are latched
		u8 m_win_startup_wait = 3;
		u8 m_win_midline_wait = 2;
		u8 m_win_startup_lx = 11;
		bool m_fetch_window = false;

		bool oam_entry_pending(u8 lx) const { return m_oam_served_count[lx] < m_oam_entries[lx].size(); }
		void pixel_transfer_tick();
		void tick_fetcher();
		void handle_bg_push();
		void tick_bg_fetch();
		void tick_obj_fetch();
		void push_bg_tile();
		void push_obj_pixels();
		void start_sprite_fetch();
		void finish_pixel_transfer();
		std::vector<u8> m_video_buffer;	// A buffer that has all the pixels of a frame
		u16 m_line_ticks; // Indicates in which dot of the PPU we are while we are scanning horizontal lines
		u16 m_dots = 0;	// 0-based dot within the line (m_line_ticks - 1); target timeline unit
		u8 m_window_line;	// Window tile row used by the current activation
		u8 m_window_next_row = 0;	// Row for the next window activation
		//PPU State Machine
		void ppu_mode_oam();
		void ppu_mode_vblank();
		void ppu_mode_hblank();
		void ppu_lcd_on_line();	// Special line schedule used only for the first 3 lines after the LCD is turned on

		using TickSelector = void (PPU::*)();
		TickSelector m_tick_selector = &PPU::ppu_mode_oam;	// Current per-dot state (F4)

		void ppu_power_off();	// LCD turned off: force LY=0 and mode 0, reset pipeline state, no interrupts
		void ppu_power_on();	// LCD turned on: line 0 starts in mode 0 without OAM scan, restart the LYC==LY comparison
		void ppu_reset_line_state();	// Resets the per-line/pipeline state shared by the LCD power on/off transitions

		void increment_ly();	// Increases lcd counter ly and launches interrupts if needed
		void schedule_lyc_update();	// Schedules the delayed LYC==LY flag update after an LY change
		void check_lyc();	// Compares LY with LYC, updates the flag and launches the interrupt if needed, must be called every time LY changes
		void set_lyc_flag(bool ly_equals_lyc);	// Writes the LYC==LY flag and fires the STAT interrupt on its 0->1 edge
		bool m_lcd_enabled;	// Last known LCDC.7 state, used to detect LCD power on/off transitions on each tick
		bool m_stat_irq_line = false;	// Internal STAT IRQ line (latched), cleared on entering mode 3
		bool m_pending_stat_irq = false;	// STAT IRQ edge seen last dot; raised on the next dot (1-T delay)
		bool m_last_stat_irq = false;	// Previous dot's STAT IRQ condition (edge detection)
		bool m_enable_lyc_eq_ly_irq = true;	// LYC IRQ disabled at dot 453 and last VBlank line 2..6
		bool m_vblank_last_line = false;	// True while the PPU is on VBlank line 153 (LY is 0 from dot 2 on)
		void update_stat_irq();	// Update STAT IRQ with edge detection (LCD-on special schedule)
		void update_stat_irq_for_oam_mode();	// OAM IRQ is checked only on mode transitions
		void raise_stat_irq();	// Per-dot STAT IRQ edge evaluation
		u8 m_lyc_seen;	// Last LYC value seen by the PPU, used to detect CPU writes to LYC
		u8 m_lyc_update_countdown = 0;	// Dots left until the delayed LYC==LY flag update is applied (0 = no update pending)
		bool m_lyc_update_value = false;	// Value written when the delayed LYC==LY flag update fires

		// DMG: the PPU sees LCDC/BGP/WX/LY/LYC delayed by 1 T-cycle.
		u8 m_last_lcdc = 0;
		u8 m_last_bgp = 0;
		u8 m_last_wx = 0;
		u8 m_last_ly = 0;
		u8 m_last_lyc = 0;
		void update_delayed_registers();


		u8 m_pushed_x;	// Indicates how many pixels have been pushed to screen

		InterruptCallback m_request_interrupt;
		FrameCallback m_frame_done;

		void request_interrupt(InterruptType IType);
		// While a lock is active the CPU reads $FF and writes are dropped (mooneye lcdon_timing-GS).
		VideoBus m_oam_bus;
		VideoBus m_vram_bus;
		u8 m_lcd_on_line = 255;	// 0/1/2 = special lines after LCD power on, 255 = normal schedule

};


} // namespace qtreGB
#endif
