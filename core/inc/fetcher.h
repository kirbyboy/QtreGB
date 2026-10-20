#ifndef QTREGB_FETCHER_H
#define QTREGB_FETCHER_H

#include <vector>
#include <array>
#include <string>

#include "types.h"
#include "fifo.h"
#include "ifetcher_regs.h"
#include "ivram.h"
#include "oam_entry.h"

namespace qtreGB
{

// Background/window fetch. Each VRAM access is split in two: the _T1 state
// computes the address (and samples the registers that select it) and the _T2
// state performs the read. A register write landing between them changes the
// address the read uses (bitplane mixing, tile-select glitches).
enum class FetchEntry:u8
{
	FS_TILE_T1,
	FS_TILE_T2,
	FS_DATA0_T1,
	FS_DATA0_T2,
	FS_DATA1_T1,
	FS_DATA1_T2,
	FS_PUSH,
	FS_GAP
};

// Objects keep the legacy four-step machine for now; they are rewritten in a
// later cut. They still advance on every other tick to preserve their timing.
enum class FetchEntryObj:u8
{
	FS_OBJ_TILE,
	FS_OBJ_DATA0,
	FS_OBJ_DATA1,
	FS_OBJ_PUSH
};

enum class FetchMode:u8
{
	FS_BGD,	// BG fetcher
	FS_OBJ	// OBJ fetcher
};

enum class FetchModeBG:u8
{
	FS_BG_BGD,	// Background mode
	FS_BG_WIN	// Window mode
};

class OAM_line_Entry
{
	public:
		OAM_line_Entry();
		OAM_line_Entry(OAMEntry entry);

		OAMEntry m_oam_entry;
		bool m_marked;
		bool m_done;
};

class OAM_line_Entries
{
	public:
		OAM_line_Entries();

		void clear();
		void add_entry(OAMEntry entry);
		u8 size();
		OAM_line_Entry& get_entry(u8 index);

	private:
		u8 m_size;
		std::array<OAM_line_Entry, 10> m_oam_entries;
};

class Fetcher
{
	public:

		Fetcher();

		void reset();
		void set_state(FetchEntryObj state);
		void set_mode(FetchMode mode);
		void tick();
		void check_obj(u8 x); // Every time we change X in lcd we must check available OBJ's
		void set_window_line(u8 window_line);

		void set_vram(IVram* vram);
		void set_regs(IFetcherRegs* regs);
		void set_current_x(u8 x);

		// The fetcher owns the two pixel FIFOs; the PPU consumes them through these
		// views while pushing pixels to the screen.
		FIFO& bgw_fifo();
		FIFO& spr_fifo();

		// Produces one screen pixel from the two FIFOs. Owns the BG/OBJ merge, the
		// palette lookup and the fine-scroll discard. Returns false when no pixel is
		// ready (empty FIFO or a discarded pixel), leaving the caller to try again.
		bool pop_pixel(u8& out_pixel);

		void set_warming(bool warming);
		void load_oam_entries(std::vector<OAMEntry> entries);
		void check_mode();

		// Debug trace: records what the fetcher does each fetch while enabled.
		// Used only by the headless dumper; disabled (no cost) in normal runs.
void debug_begin(u8 line);
	void debug_end();
	void debug_set_line(u8 line);
	const std::vector<std::string>& debug_log() const;

		void fill_object_array();
		std::array<FIFOElement, 160>& get_object_array();

	private:
		// Screen height in lines. The fetcher keeps its own copy so it does not depend
		// on PPU::YRES (used by the window activation check in check_mode).
		static constexpr int SCREEN_HEIGHT = 144;

		IFetcherRegs* m_regs = nullptr;
		IVram* m_vram = nullptr;

		FetchEntryObj m_current_state;
		FetchEntry m_state_bg;
		FetchMode m_mode;
		FetchModeBG m_bg_mode;
		u8 m_pushed_bg_x;	// how many pushed background tiles I have done
		u8 m_tile_map_bg;	// the tile for the background, the info comes from the OAM.
		u8 m_tile_map_spr;	// the tile for the sprites, the info comes from the OAM.
		u8 m_current_x;	// current x in lcd
		u8 m_discarded_pixels;	// fine-scroll pixels dropped before the visible area
		FIFO m_bgw_fifo;
		FIFO m_spr_fifo;
		u8 m_data_lo_bg;
		u8 m_data_hi_bg;
		u8 m_data_lo_spr;
		u8 m_data_hi_spr;
		u16 m_bg_map_addr;	// tile-map address latched by FS_TILE_T1, read by FS_TILE_T2
		u16 m_bg_data_addr;	// tile-data address latched by the *_T1 states, read by the *_T2 states
		bool m_bg_signed_tiles;	// LCDC bit 4 sampled at the tile step (0x8800 tileset)
		OAM_line_Entries m_oam_line_entries;
		u8 m_current_oam_entry;
		u8 m_bg_tile_to_push;
		bool m_bg_need_recalculate;

		// Window fine offset: when WX < 7 the window's left edge lands at a negative
		// screen X, so the first (7 - WX) columns of its first tile are off-screen.
		// Sampled on activation and then consumed one pixel per dot by pop_pixel,
		// because those off-screen columns still occupy output positions: dropping
		// them instantly would start the visible stream too early. If the first
		// window tile does not have enough pixels they keep being consumed from the
		// following tiles.
		u8 m_win_fine_offset;
		bool is_window;
		u8 m_window_line;

		// Window reactivation state (see documentation/Game Boy/ppu-fetcher.md §6-§7).
		// m_wx_triggered is set on the first window activation of a line and stays set;
		// the window does not fall back to background when WX changes mid-line.
		// m_window_is_being_fetched is set on activation and cleared once the first
		// window tile has been pushed, gating the reactivation quirk.
		bool m_wx_triggered;
		bool m_window_is_being_fetched;
		// Set by check_mode when the reactivation condition is met; consumed by
		// pop_pixel, which then emits a raw color-0 pixel instead of popping the FIFO.
		bool m_insert_bg_pixel;

		bool m_warming;
		u8 m_phase;	// tick divisor: the sprite machine and the object check advance on every other tick

		std::array<FIFOElement, 160> m_object_array;

		bool m_debug_enabled = false;
		u8 m_debug_line = 0;
		std::vector<std::string> m_debug_log;

		void debug_trace(const std::string& entry);

		void fill_map_background_t1();
		void fill_map_background_t2();
		void fill_map_window();
		void fill_map_sprite();

		void load_data_background_t1(u8 offset);
		void load_data_background_t2(u8 offset);
		void load_data_window(u8 offset);
		void load_data_sprite(u8 offset);

		bool push_data_background();
		bool push_data_window();
		bool push_data_sprite();



		void sm_bg(bool every_other);	// background/window state machine
		void sm_obj();	// objects state machine
};


} // namespace qtreGB
#endif
