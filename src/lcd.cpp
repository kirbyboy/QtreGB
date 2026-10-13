#include "lcd.h"
#include "QtreGB.h"
#include <type_traits>

using namespace qtreGB;

void LCD::lcd_init()
{
	m_lcdc = 0x91;
	m_scroll_x = 0;
	m_scroll_y = 0;
	m_ly = 0;
	m_lyc = 0;
	m_bg_palette = 0xFC;
	m_obj_palette_0= 0xFF;
	m_obj_palette_1 = 0xFF;
	m_win_y = 0;
	m_win_x = 0;
}

u8 LCD::read(u16 address)
{
	switch(address)
	{
		case 0: return m_lcdc;
		case 1: return m_lcds | 0x80;	// Bit 7 of STAT always reads 1 on DMG, required by mooneye lcdon/stat tests
		case 2: return m_scroll_y;
		case 3: return m_scroll_x;
		case 4: return m_ly;
		case 5: return m_lyc;
		case 7: return m_bg_palette;
		case 8: return m_obj_palette_0;
		case 9: return m_obj_palette_1;
		case 10: return m_win_y;
		case 11: return m_win_x;
		default: return 0;
	}
}

void LCD::write(u16 address, u8 value)
{
	switch(address)
	{
		case 0: m_lcdc = value; break;
		case 1: m_lcds = (value & 0x78) | (m_lcds & 0x07); break; // PPU mode  and LYC == LY are read only, bit 7 unused
		case 2: m_scroll_y = value; break;
		case 3: m_scroll_x = value; break;
		case 4: /*m_ly = value*/; break;	// read_only
		case 5: m_lyc = value; break;
		case 7: m_bg_palette = value; break;
		case 8: m_obj_palette_0 = value; break;
		case 9: m_obj_palette_1 = value; break;
		case 10: m_win_y = value; break;
		case 11: m_win_x = value; break;
	}
}

bool LCD::get_lcdc_bgw_enable()
{
	return (m_lcdc & 0x01) != 0;
}

bool LCD::get_lcdc_obj_enable()
{
	return (m_lcdc & 0x02) != 0;
}

u8 LCD::get_lcdc_obj_height()
{
	if((m_lcdc & 0x04) == 0)
	{
		return 8;
	}
	else
	{
		return 16;
	}
}

u16 LCD::get_lcdc_bg_map_area()
{
	if((m_lcdc & 0x08) == 0)
	{
		return 0x9800;
	}
	else
	{
		return 0x9C00;
	}
}

u16 LCD::get_lcdc_bgw_data_area()
{
	if((m_lcdc & 0x10) == 0)
	{
		return 0x8800;
	}
	else
	{
		return 0x8000;
	}
}

bool LCD::get_lcdc_win_enable()
{
	return (m_lcdc & 0x20) != 0;
}

u16 LCD::get_lcdc_win_map_area()
{
	if((m_lcdc & 0x40) == 0)
	{
		return 0x9800;
	}
	else
	{
		return 0x9C00;
	}
}

bool LCD::get_lcdc_lcd_enable()
{
	return (m_lcdc & 0x80) != 0;
}

LCDMode LCD::get_lcds_mode()
{
	switch(m_lcds & 0x03)
	{
		case 0: return LCDMode::MODE_HBLANK;
		case 1: return LCDMode::MODE_VBLANK;
		case 2: return LCDMode::MODE_OAM;
		case 3: return LCDMode::MODE_XFER;
	}
	return LCDMode::MODE_XFER;	// Will never reach here, but I don't get a warning while I compile it...
}

void LCD::set_lcds_mode(LCDMode mode)
{
	m_lcds &= 0xFC;
	switch(mode)
	{
		case LCDMode::MODE_HBLANK: break;
		case LCDMode::MODE_VBLANK: m_lcds |= 0x01; break;
		case LCDMode::MODE_OAM: m_lcds |= 0x02; break;
		case LCDMode::MODE_XFER: m_lcds |= 0x03; break;
	}
}

bool LCD::get_lcds_lyc()
{
	return ((m_lcds & 0x04) != 0);
}

void LCD::set_lcds_lyc(bool value)
{
	m_lcds &= ~0x04;
	if(value)
	{
		m_lcds |= 0x04;
	}
}

bool LCD::get_lcds_stat_int(StatSRC stat)
{
	return (m_lcds & static_cast<std::underlying_type<StatSRC>::type>(stat)) != 0;
}
