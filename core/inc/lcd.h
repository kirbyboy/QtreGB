#ifndef QTREGB_LCD_H
#define QTREGB_LCD_H

#include "types.h"
#include "tagged_serializer.h"

namespace qtreGB
{

enum class LCDMode
{
	MODE_HBLANK,
	MODE_VBLANK,
	MODE_OAM,
	MODE_XFER
};

enum class StatSRC:u8
{
	SS_HBLANK = (1 << 3),
	SS_VBLANK = (1 << 4),
	SS_OAM = (1 << 5),
	SS_LYC = (1 << 6)
};

class LCD
{
	public:
		void lcd_init();

		u8 read(u16 address);
		void write(u16 address, u8 value);

		/*
		LCDC:
		0 - BG & Window enable / priority: 0 = Off; 1 = On
		1 - OBJ enable: 0 = Off; 1 = On
		2 - OBJ size: 0 = 8×8; 1 = 8×16
		3 - BG tile map area: 0 = 9800–9BFF; 1 = 9C00–9FFF
		4 - BG & Window tile data area: 0 = 8800–97FF; 1 = 8000–8FFF
		5 - Window enable: 0 = Off; 1 = On
		6 - Window tile map area: 0 = 9800–9BFF; 1 = 9C00–9FFF
		7 - LCD & PPU enable: 0 = Off; 1 = On
		*/
		bool get_lcdc_bgw_enable();
		bool get_lcdc_obj_enable();
		u8 get_lcdc_obj_height();
		u16 get_lcdc_bg_map_area();
		u16 get_lcdc_bgw_data_area();
		bool get_lcdc_win_enable();
		u16 get_lcdc_win_map_area();
		bool get_lcdc_lcd_enable();

		/*
		LCDS:
		0-1 - PPU mode (Read-only): Indicates the PPU’s current status.
		2 - LYC == LY (Read-only): Set when LY contains the same value as LYC; it is constantly updated.
		3 - Mode 0 int select (Read/Write): If set, selects the Mode 0 condition for the STAT interrupt.
		4 - Mode 1 int select (Read/Write): If set, selects the Mode 1 condition for the STAT interrupt.
		5 - Mode 2 int select (Read/Write): If set, selects the Mode 2 condition for the STAT interrupt.
		6 - LYC int select (Read/Write): If set, selects the LYC == LY condition for the STAT interrupt.
		*/
		LCDMode get_lcds_mode();
		void set_lcds_mode(LCDMode mode);
		bool get_lcds_lyc();
		void set_lcds_lyc(bool value);
		bool get_lcds_stat_int(StatSRC stat);	// gets the source interruption for STAT type interruption

		// Raw register values: the register file owns the storage, callers read it through
		// these accessors instead of reaching into the fields. LY is an output produced by
		// the PPU (the register file only mirrors it), hence set_ly().
		u8 get_lcdc();
		u8 get_ly();
		u8 get_lyc();
		u8 get_scroll_x();
		u8 get_scroll_y();
		u8 get_win_x();
		u8 get_win_y();
		u8 get_bg_palette();
		u8 get_obj_palette_0();
		u8 get_obj_palette_1();
		void set_ly(u8 value);

		// --- Savestates / access backdoor ---
		// Mirror of the register file. The tags are consumed in the same order as
		// LCD::serialize, so a State snapshot works as a prefix: writing to State and
		// serializing it injects the raw values (bypassing the write() masking rules).
		struct State
		{
			u8 lcdc, lcds, scroll_y, scroll_x, ly, lyc;
			u8 bg_palette, obj_palette_0, obj_palette_1;
			u8 win_y, win_x;

			void serialize(TaggedSerializer& s);
		};

		void serialize(TaggedSerializer& s);

	private:
		//registers
		u8 m_lcdc;	// FF40 — LCDC: LCD control
		u8 m_lcds = 0;	// FF41 — STAT: LCD status
		u8 m_scroll_y;	// FF42 – SCY: Background viewport Y position
		u8 m_scroll_x;	// FF43 — SCX: Background viewport X position
		u8 m_ly;	// FF44 — LY: LCD Y coordinate [read-only]
		u8 m_lyc;	// FF45 — LYC: LY compare
		u8 m_bg_palette;	// FF47 — BGP (Non-CGB Mode only): BG palette data
		u8 m_obj_palette_0;	// FF48 — OBP0: OBJ palette 0 data
		u8 m_obj_palette_1;	// FF49 — OBP1: OBJ palette 1 data
		u8 m_win_y;	// FF4A — WY: Window Y position
		u8 m_win_x;	// FF4B — WX: Window X position plus 7
};


} // namespace qtreGB
#endif
