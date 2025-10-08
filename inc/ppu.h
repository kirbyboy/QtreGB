#ifndef QTREGB_PPU_H
#define QTREGB_PPU_H

#include <vector>
#include <list>
#include <queue>
#include <array>
#include <functional>

#include "common.h"
#include "memory.h"
#include "fifo.h"
#include "ibus.h"
#include "oam_entry.h"
#include "fetcher.h"

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
		void ppu_init();
		void ppu_tick();

		u8 ppu_oam_read(u16 address);
		void ppu_oam_write(u16 address, u8 value);

		u8 ppu_vram_read(u16 address);
		void ppu_vram_write(u16 address, u8 value);

		const std::vector<u8>& get_video_buffer() const;
		const std::vector<OAMEntry>& get_oam_line_entry() const;

		void connect_bus(IBus* bus);
		bool is_vram_locked();

	private:
		IBus *m_bus;
		LCD *m_lcd;
		Memory m_vram;
		std::vector<OAMEntry> m_oam_ram;	// OAM RAM
		std::vector<OAMEntry> m_oam_line_entry;	// "List" of available sprites in a line, maximum 10, it just simulates a FIFO, not a real list
		std::vector<u8> m_video_buffer;	// A buffer that has all the pixels of a frame
		//FetchEntry m_current_fetch_state;	// Indicates actual state of PPU state machine
		u16 m_line_ticks; // Indicates in which dot of the PPU we are while we are scanning horizontal lines
		u8 m_line_x;	// Indicates the current pixel in FIFO we are trying to push, may be discarted by SCX
		u8 m_fetched;	// Indicates how many pixels we have fetched, incresing always in 8(when we get a line from a sprite or background, we always take 8)


		//u8 m_window_tile_map;	// Indicates the window map tile fetched for current tile
		u8 m_window_tile_data_lo;	// Indicates the window tile data fetched for current tile, lo bits data
		u8 m_window_tile_data_hi;	// Indicates the window tile data fetched for current tile, hi bits data


		u8 m_bg_tile_map;	// Indicates the background map tile fetched for current tile
		std::array<u8, 2> m_bg_tile_data;	// Indicates the background tile data fetched for current tile
		u8 m_bg_tile_map_x;	// Indicates the X in the 32x32 background tile map
		u8 m_bg_tile_map_y;	// Indicates the Y in the 32x32 background tile map
		u8 m_bg_tile_data_y;	// Indicates the row of data treated in a tile
		void pipeline_load_background_tile();	// Loads background tiles for a line, only the references to tiles is needed for now
		void pipeline_load_background_data(u8 offset);	// Loads background data tile for a line

		u8 m_window_line;	// Indicates how many window lines are draw in a screen
		u8 m_window_tile_map;	// Indicates the window tiles data fetched for current position of lcd(in groups of 8 bytes)
		std::array<u8, 2> m_window_tile_data;	// Indicates the window tile data fetched for current tile
		void pipeline_load_window_tile();	// Loads window tile needed for a fetch operation.
		void pipeline_load_window_data(u8 offset);	// Loads window data needed for a fetch operation.


		//PPU State Machine
		void ppu_mode_oam();
		void ppu_mode_xfer();
		void ppu_mode_vblank();
		void ppu_mode_hblank();

		void increment_ly();	// Increases lcd counter ly and launches interrupts if needed


		u8 m_pushed_x;	// Indicates how many pixels have been pushed to screen
		u8 m_discarded_pixels;	// How many pixels must be discarded before starting to process





		void load_line_sprites(); // Loads in memory the sprites available in each line, done in OAM mode

		// Pipeline methods
		void pipeline_fifo_reset();	// Emptys the pixel FIFO
		void pipeline_process();	// Does all the pipeline process
		void pipeline_push_pixel();	// Push pixel into screen



		std::vector<u8> m_sp_tile_data_lo;	// Vector with all the possible sprites in a group of fetched pixels, lo bits data
		std::vector<u8> m_sp_tile_data_hi;	// Vector with all the possible sprites in a group of fetched pixels, hi bits data
		void pipeline_load_sprite_tile();	// Loads sprite tiles for a line, only the references to tiles is needed for now
		void pipeline_load_sprite_data(u8 offset);	// Loads sprite data from tiles in a line

		std::vector<u8> m_pixel_fifo;	// The pixel FIFO
		// Pixel fifo push and pop
		void pixel_fifo_push(u8 value);
		u8 pixel_fifo_pop();


		FIFO m_bgw_fifo;	// The backgroung/window FIFO
		FIFO m_spr_fifo;	// The sprite FIFO

		InterruptCallback m_request_interrupt;
		FrameCallback m_frame_done;

		Fetcher m_fetcher;

		void request_interrupt(InterruptType IType);
		bool m_is_vram_locked;

};

#endif
