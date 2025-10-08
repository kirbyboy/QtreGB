#ifndef QTREGB_LCD_H
#define QTREGB_LCD_H

#include "common.h"

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

		//registers
		u8 m_lcdc;	// FF40 — LCDC: LCD control
		u8 m_lcds;	// FF41 — STAT: LCD status
		u8 m_scroll_y;	// FF42 – SCY: Background viewport Y position
		u8 m_scroll_x;	// FF43 — SCX: Background viewport X position
		u8 m_ly;	// FF44 — LY: LCD Y coordinate [read-only]
		u8 m_lyc;	// FF45 — LYC: LY compare
		u8 m_dma;	// FF46 — DMA: OAM DMA source address & start
		u8 m_bg_palette;	// FF47 — BGP (Non-CGB Mode only): BG palette data
		u8 m_obj_palette_0;	// FF48 — OBP0: OBJ palette 0 data
		u8 m_obj_palette_1;	// FF49 — OBP1: OBJ palette 1 data
		u8 m_win_y;	// FF4A — WY: Window Y position
		u8 m_win_x;	// FF4B — WX: Window X position plus 7
};

#endif
