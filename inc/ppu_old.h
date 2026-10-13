#if 0
#ifndef QTREGB_PPU_H
#define QTREGB_PPU_H

#include <vector>
#include <list>
#include <queue>
#include <array>

#include "types.h"
#include "memory.h"

class QtreGB;
class LCD;

class OAMEntry
{
	public:
		OAMEntry();

		u8 m_y;
		u8 m_x;
		u8 m_tile;
		u8 m_flags;
};

enum class FetchEntry:u8
{
	FS_TILE,
	FS_DATA0,
	FS_DATA1,
	FS_IDLE,
	FS_PUSH
};

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
		static const int LINES_PER_FRAME;
		static const int TICKS_PER_LINE;
		static const int YRES;
		static const int XRES;

		u16 m_line_ticks;

		PPU(QtreGB *emu);
		void ppu_init();
		void ppu_tick();

		u8 ppu_oam_read(u16 address);
		void ppu_oam_write(u16 address, u8 value);

		u8 ppu_vram_read(u16 address);
		void ppu_vram_write(u16 address, u8 value);

		const std::vector<u8>& get_video_buffer() const;


	private:
		QtreGB *m_emu;
		LCD *m_lcd;
		Memory m_vram;
		std::vector<OAMEntry> m_oam_ram;
		std::list<OAMEntry> m_oam_line_entry;
		std::vector<u8> m_video_buffer;
		u8 m_fetched_entry_count;
		std::array<OAMEntry, 3> m_fetched_entries; //entries fetched during pipeline.
		u8 m_window_line;

		//Pipeline
		FetchEntry m_current_fetch_state;
		std::queue<u8> m_pixel_fifo;
		u8 m_line_x;
		u8 m_pushed_x; // Indicates the X pixel that is being treated/pushed/etc... in the FIFO
		u8 m_fetch_x; //Indicates which pixel we are fetching right now
		std::array<u8, 3> m_bgw_fetch_data;
		std::array<u8, 6> m_fetch_entry_data; //oam data...
		u8 m_map_y;	// Indicates which tile in the map is being read/written, 32x32 map, y value
		u8 m_map_x; // Indicates which tile in the map is being read/written, 32x32 map, x value
		u8 m_tile_y;
		u8 m_fifo_x;

		//PPU State Machine
		void ppu_mode_oam();
		void ppu_mode_xfer();
		void ppu_mode_vblank();
		void ppu_mode_hblank();

		void increment_ly();

		void pipeline_fifo_reset();
		void pipeline_process();
		void pipeline_fetch();
		void pipeline_push_pixel();
		bool pipeline_fifo_add();

		void load_line_sprites();
		u8 fetch_sprite_pixels(u8 bit, u8 color, u8 index);
		void pipeline_load_sprite_tile();
		void pipeline_load_sprite_data(u8 offset);

		void pipeline_load_window_tile();
		bool window_visible();

		void pixel_fifo_push(u8 value);
		u8 pixel_fifo_pop();


};

#endif
#endif
