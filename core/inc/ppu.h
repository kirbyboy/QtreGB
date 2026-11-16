#ifndef QTREGB_PPU_H
#define QTREGB_PPU_H

#include <array>
#include <deque>
#include <functional>
#include <vector>

#include "types.h"
#include "memory.h"
#include "oam_entry.h"
#include "videobus.h"
#include "lcd.h"

namespace qtreGB
{

class IBus;

// Native DMG PPU. Dot-driven pipeline with a per-dot state selector:
//
//   - one T-cycle per tick, dispatched through a per-dot state selector,
//   - OAM scan reading and filtering one entry every two dots,
//   - STAT mode / LYC / VBlank edge timing including the DMG glitches
//     (first line after turn on, last VBlank line, LYC IRQ disable window),
//   - BG/window fetcher and OBJ fetcher with FIFO merging,
//   - bus ownership of VRAM/OAM for CPU contention.
class PPU
{
	public:
		using FrameCallback = std::function<void(bool)>;

		static const int LINES_PER_FRAME;
		static const int TICKS_PER_LINE;
		static const int YRES;
		static const int XRES;

		PPU(LCD *lcd);

		void set_interrupt_callback(InterruptCallback cb) { m_request_interrupt = std::move(cb); }
		void set_frame_callback(FrameCallback cb) { m_frame_done = std::move(cb); }

		void ppu_init(bool boot_rom = false);
		void ppu_tick();
		void ppu_sync_lcd_power() {}
		void set_dma_active(bool active) { m_dma_active = active; }

		u8 ppu_oam_read(u16 address);
		void ppu_oam_write(u16 address, u8 value);
		void ppu_oam_write_dma(u16 address, u8 value);

		u8 ppu_vram_read(u16 address);
		void ppu_vram_write(u16 address, u8 value);

		const std::vector<u8>& get_video_buffer() const { return m_video_buffer; }

		// Bus ownership exposed to the CPU view. The PPU keeps these flags;
		// BusCpuView is the one that drops/returns 0xFF for CPU accesses.
		bool is_oam_read_locked() const { return m_oam_bus.is_acquired(VideoAccess::Read); }
		bool is_oam_write_locked() const { return m_oam_bus.is_acquired(VideoAccess::Write); }
		bool is_vram_read_locked() const { return m_vram_bus.is_acquired(VideoAccess::Read); }
		bool is_vram_write_locked() const { return m_vram_bus.is_acquired(VideoAccess::Write); }

		void connect_bus(IBus *bus) { m_bus = bus; }

		u8 read_lcd_register(u16 offset);
		void write_lcd_register(u16 offset, u8 value);

		// Debug-only accessors for the trace tools.
		u16 debug_dots() const { return m_dots; }
		u8 debug_lx() const { return m_lx; }
		u8 debug_fetch_x() const { return static_cast<u8>(m_bwf.lx); }
		u8 debug_pushed_x() const { return m_lx >= 8 ? static_cast<u8>(m_lx - 8) : 0; }
		int debug_fetch_phase() const { return 0; }
		bool debug_fetch_window() const { return m_w_active; }
		bool debug_fetching_sprite() const { return m_fetching_sprite; }
		size_t debug_bg_fifo() const { return m_bg_fifo.size(); }
		size_t debug_obj_fifo() const { return m_obj_fifo.size(); }
		bool debug_oam_pending() const { return !m_oam_entries[m_lx].empty(); }
		u8 debug_oam_served() const { return 0; }
		u8 debug_oam_entries() const { return m_oam_entries[m_lx].count; }
		int debug_obj_phase() const { return 0; }
		u8 debug_obj_wait() const { return 0; }
		u8 debug_post_wait() const { return 0; }

	private:
		enum Mode : u8
		{
			HBLANK = 0,
			VBLANK = 1,
			OAM_SCAN = 2,
			PIXEL_TRANSFER = 3,
		};

		struct Lcdc
		{
			bool enable = false;
			bool win_tile_map = false;
			bool win_enable = false;
			bool bg_win_tile_data = false;
			bool bg_tile_map = false;
			bool obj_size = false;
			bool obj_enable = false;
			bool bg_win_enable = false;
		};

		struct Stat
		{
			bool lyc_eq_ly_int = false;
			bool oam_int = false;
			bool vblank_int = false;
			bool hblank_int = false;
			bool lyc_eq_ly = false;
			u8 mode = 0;
		};

		struct PendingWrite
		{
			u8 countdown = 0;
			u8 value = 0;
		};

		struct BgPixel
		{
			u8 color_index = 0;
		};

		struct ObjPixel
		{
			u8 color_index = 0;
			u8 attributes = 0;
			u8 number = 0;
			u8 x = 0;
		};

		struct OamScanEntry
		{
			u8 number = 0;
			u8 y = 0;
			u8 x = 0;
		};

		struct SpriteList
		{
			std::array<OamScanEntry, 10> entries {};
			u8 count = 0;

			bool empty() const { return count == 0; }
			void clear() { count = 0; }

			void push(const OamScanEntry& entry)
			{
				if(count < entries.size())
				{
					entries[count++] = entry;
				}
			}

			const OamScanEntry& back() const { return entries[count - 1]; }
			void pop_back() { --count; }
		};

		using TickSelector = void (PPU::*)();
		using FetcherTickSelector = void (PPU::*)();

		IBus *m_bus = nullptr;
		LCD *m_lcd = nullptr;
		Memory m_vram;
		std::vector<OAMEntry> m_oam_ram;
		std::vector<u8> m_video_buffer;
		VideoBus m_oam_bus;
		VideoBus m_vram_bus;

		InterruptCallback m_request_interrupt;
		FrameCallback m_frame_done;

		bool m_boot_rom = false;
		bool m_dma_active = false;

		// PPU registers (the PPU owns them; the LCD is a passive mirror for
		// debug/GUI, as in the full pipeline).
		Lcdc m_lcdc {};
		Stat m_stat {};
		u8 m_scy = 0;
		u8 m_scx = 0;
		u8 m_ly = 0;
		u8 m_lyc = 0;
		u8 m_dma = 0;
		u8 m_bgp = 0;
		u8 m_obp0 = 0;
		u8 m_obp1 = 0;
		u8 m_wy = 0;
		u8 m_wx = 0;

		// Dot-driven state machine.
		TickSelector m_tick_selector = &PPU::oam_scan_after_turn_on;
		FetcherTickSelector m_fetcher_tick_selector = &PPU::bgwin_prefetcher_get_tile_0;
		Mode m_mode = OAM_SCAN;
		u16 m_dots = 0;	// [0, 456)
		u8 m_lx = 0;	// LX = X + 8, therefore [0, 168]

		// DMG: the PPU sees LCDC/BGP/WX/LY/LYC delayed by 1 T-cycle.
		bool m_last_stat_irq = false;
		bool m_enable_lyc_eq_ly_irq = true;
		bool m_pending_stat_irq = false;
		u8 m_last_bgp = 0;
		u8 m_last_wx = 0;
		u8 m_last_ly = 0;
		u8 m_last_lyc = 0;
		Lcdc m_last_lcdc {};

		PendingWrite m_pending_stat {};

		// First line after the LCD is turned on: STAT stays in HBlank during
		// OAM scan and the HBlank mode update is delayed by two dots.
		bool m_is_glitched_line_0 = false;
		u8 m_glitched_line_0_hblank_delay = 0;

		// Last VBlank line: STAT reads HBlank during the OAM scan transition.
		bool m_vblank_last_line_stat_mode_hblank_glitch = false;

		// OAM scan.
		struct
		{
			u8 count = 0;
			u8 index = 0;
			u8 request = 0;
		} m_oam_scan;
		OAMEntry m_oam_scan_registers {};	// word read for the entry being scanned
		std::array<SpriteList, 168> m_oam_entries {};

		// Window.
		bool m_w_active_for_frame = false;
		u8 m_w_wly = 0xFF;
		bool m_w_activating = false;
		bool m_w_active = false;
		bool m_w_just_activated = false;
		u8 m_wf_tilemap_x = 0;

		// Pixel transfer.
		struct
		{
			u8 to_discard = 0;
			u8 discarded = 0;
		} m_initial_scx;

		// BG/window fetcher state (the pixel-slice fetcher is shared).
		struct
		{
			u16 lx = 0;	// [0, 256), advances 8 by 8
			u16 tilemap_tile_vram_addr = 0;
			u8 tile_number = 0;
			bool interrupted_fetch_has_data = false;
			u8 interrupted_tile_data_low = 0;
			u8 interrupted_tile_data_high = 0;
		} m_bwf;
		// OBJ prefetcher state: the OAM entry being served and the tile number
		// and attributes read from it.
		struct
		{
			OamScanEntry entry {};
			u8 tile_number = 0;
			u8 attributes = 0;
		} m_of;
		struct
		{
			u16 tile_data_vram_address = 0;
			u8 tile_data_low = 0;
			u8 tile_data_high = 0;
		} m_psf;

		// BG/window and OBJ FIFOs.
		std::deque<BgPixel> m_bg_fifo {};
		std::deque<ObjPixel> m_obj_fifo {};
		bool m_pixel_slice_fetcher_push_enabled = false;
		bool m_pending_bg_fifo_fill = false;
		bool m_fetching_sprite = false;

		// Helpers.
		void turn_on();
		void turn_off();
		bool is_lyc_eq_ly() const;
		void raise_stat_irq();
		void tick_pending_write();
		void tick_stat();
		void update_stat_irq(bool irq);
		void update_stat_irq_for_oam_mode();
		void update_stat_irq_for_oam_mode_do_not_clear_last_stat_irq();
		void begin_increase_ly();
		void end_increase_ly();
		void write_stat_real(u8 value);
		void write_lcdc_real(u8 value);
		u8 read_lcdc() const;
		void write_lcdc(u8 value);
		u8 read_stat() const;
		void write_stat(u8 value);
		void tick_window();

		// States.
		void oam_scan_even();
		void oam_scan_odd();
		void oam_scan_77();
		void oam_scan_done();
		void oam_scan_after_turn_on();
		void oam_scan_after_turn_on_78();
		void oam_scan_after_turn_on_79();

		void pixel_transfer_dummy_lx0();
		void pixel_transfer_discard_lx0();
		void pixel_transfer_discard_lx0_wx0_scx();
		void pixel_transfer_lx0();
		void pixel_transfer_lx8();

		void hblank();
		void hblank_452();
		void hblank_453();
		void hblank_454();
		void hblank_455();
		void hblank_last_line();
		void hblank_last_line_453();
		void hblank_last_line_454();
		void hblank_last_line_455();
		void hblank_first_line_after_turn_on();

		void vblank();
		void vblank_453();
		void vblank_454();
		void vblank_455();
		void vblank_last_line();
		void vblank_last_line_2();
		void vblank_last_line_3();
		void vblank_last_line_4();
		void vblank_last_line_7();
		void vblank_last_line_453();
		void vblank_last_line_454();
		void vblank_last_line_455();

		// State helpers.
		void enter_oam_scan();
		void enter_pixel_transfer();
		void enter_hblank();
		void enter_vblank();
		void enter_new_frame();
		void update_mode(u8 mode);
		u8 resolve_color(u8 color_index, u8 palette) const;
		void increase_lx();

		// Fetcher. The BG/window prefetcher and pixel-slice fetcher follow the
		// validated state sequence: GetTile, GetTileDataLow, GetTileDataHigh,
		// push. The OBJ fetcher interrupts the BG/window one when a sprite hit
		// is pending at the current LX.
		void tick_fetcher();
		void reset_fetcher();
		void pixel_slice_fetcher_idle();

		void bgwin_prefetcher_get_tile_0();
		void bg_prefetcher_get_tile_0();
		void bg_prefetcher_get_tile_1();
		void bg_pixel_slice_fetcher_get_tile_data_low_0();
		void bg_pixel_slice_fetcher_get_tile_data_low_1();
		void bg_pixel_slice_fetcher_get_tile_data_high_0();
		void bg_pixel_slice_fetcher_get_tile_data_high_1();

		void win_prefetcher_get_tile_0();
		void win_prefetcher_get_tile_1();
		void win_pixel_slice_fetcher_get_tile_data_low_0();
		void win_pixel_slice_fetcher_get_tile_data_low_1();
		void win_pixel_slice_fetcher_get_tile_data_high_0();
		void win_pixel_slice_fetcher_get_tile_data_high_1();

		void bgwin_pixel_slice_fetcher_get_tile_data_high_1();

		// OBJ fetcher: tile number/attributes from OAM, tile data from VRAM and
		// merge into the OBJ FIFO honoring sprite priority.
		void obj_prefetcher_get_tile_0();
		void obj_prefetcher_get_tile_1();
		void obj_pixel_slice_fetcher_get_tile_data_low_0();
		void obj_pixel_slice_fetcher_get_tile_data_low_1();
		void obj_pixel_slice_fetcher_get_tile_data_high_0();
		void obj_pixel_slice_fetcher_get_tile_data_high_1_and_merge_with_obj_fifo();

		void setup_bg_pixel_slice_fetcher_tilemap_tile_address();
		void setup_bg_pixel_slice_fetcher_tile_data_address();
		void setup_win_pixel_slice_fetcher_tilemap_tile_address();
		void setup_win_pixel_slice_fetcher_tile_data_address();
		void setup_obj_pixel_slice_fetcher_tile_data_address();
		void read_bgwin_tile_number();
		void read_bgwin_tile_data_low();
		void read_bgwin_tile_data_high();
		void read_obj_tile_data_low();
		void read_obj_tile_data_high();
		void enable_pixel_slice_fetcher_push();
		void disable_pixel_slice_fetcher_push();
		void cache_bg_win_fetch();
		void restore_bg_win_fetch();
		void fill_bg_fifo();

		bool is_bg_fifo_ready_to_be_popped() const;
		bool is_obj_ready_to_be_fetched() const;
		static bool is_obj_opaque(u8 color_index) { return color_index != 0; }
		void handle_pixel_slice_fetcher_push();
		void handle_pending_bg_fifo_fill();
		void handle_pending_window_activation();
		void activate_window();
		void check_window_activation();
		void setup_fetcher_for_window();

		// OAM scan helpers.
		void reset_oam_scan_entries();
		void oam_scan_read_request();
		void oam_scan_flush_read_request();
		void handle_oam_scan_entry();

		void push_pixel(u8 color);
		void sync_lcd_from_ppu();
		void reset();
};

} // namespace qtreGB

#endif
