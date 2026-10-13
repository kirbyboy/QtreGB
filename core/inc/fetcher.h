#ifndef QTREGB_FETCHER_H
#define QTREGB_FETCHER_H

#include <vector>
#include <array>
#include <string>

#include "types.h"
#include "fifo.h"
#include "ibus.h"
#include "lcd.h"
#include "oam_entry.h"

namespace qtreGB
{

enum class FetchEntry:u8
{
	FS_TILE,
	FS_DATA0,
	FS_DATA1,
	FS_PUSH
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

		Fetcher(FIFO & bgw_fifo, FIFO & spr_fifo);

		void reset();
		void set_state(FetchEntry state);
		void set_mode(FetchMode mode);
		void tick();
		void check_obj(u8 x); // Every time we change X in lcd we must check available OBJ's
		void set_window_line(u8 window_line);

		void set_bus(IBus* bus);
		void set_lcd(LCD* lcd);
		void set_current_x(u8 x);

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
		LCD *m_lcd;

		FetchEntry m_current_state;
		FetchEntry m_state_bg;
		FetchMode m_mode;
		FetchModeBG m_bg_mode;
		u8 m_pushed_bg_x;	// how many pushed background tiles I have done
		u8 m_tile_map_bg;	// the tile for the background, the info comes from the OAM.
		u8 m_tile_map_spr;	// the tile for the sprites, the info comes from the OAM.
		u8 m_current_x;	// current x in lcd
		FIFO & m_bgw_fifo;
		FIFO & m_spr_fifo;
		u8 m_data_lo_bg;
		u8 m_data_hi_bg;
		u8 m_data_lo_spr;
		u8 m_data_hi_spr;
		OAM_line_Entries m_oam_line_entries;
		u8 m_current_oam_entry;
		u8 m_bg_tile_to_push;
		bool m_bg_need_recalculate;
		bool is_window;
		u8 m_window_line;

		IBus* m_bus;
		bool m_warming;

		std::array<FIFOElement, 160> m_object_array;

		bool m_debug_enabled = false;
		u8 m_debug_line = 0;
		std::vector<std::string> m_debug_log;

		void debug_trace(const std::string& entry);

		void fill_map_background();
		void fill_map_window();
		void fill_map_sprite();

		void load_data_background(u8 offset);
		void load_data_window(u8 offset);
		void load_data_sprite(u8 offset);

		bool push_data_background();
		bool push_data_window();
		bool push_data_sprite();



		void sm_bg();	// background/window state machine
		void sm_obj();	// objects state machine
};


} // namespace qtreGB
#endif
