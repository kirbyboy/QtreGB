#include "lcd.h"
#include "QtreGB.h"
#include "state_stream.h"
#include <type_traits>
#include <cstdlib>
#include <cstdio>
#include <cstdlib>

using namespace qtreGB;

namespace
{

constexpr u16 LCD_LCDC = 0x0001;
constexpr u16 LCD_STAT = 0x0002;
constexpr u16 LCD_SCY = 0x0003;
constexpr u16 LCD_SCX = 0x0004;
constexpr u16 LCD_LY = 0x0005;
constexpr u16 LCD_LYC = 0x0006;
constexpr u16 LCD_BGP = 0x0007;
constexpr u16 LCD_OBP0 = 0x0008;
constexpr u16 LCD_OBP1 = 0x0009;
constexpr u16 LCD_WY = 0x000A;
constexpr u16 LCD_WX = 0x000B;
constexpr u16 LCD_PENDING_STAT = 0x0010;
constexpr u16 LCD_PENDING_STAT_VALUE = 0x0011;
constexpr u16 LCD_DELAYED_COUNT = 0x0012;
constexpr u16 LCD_DELAYED_WRITES = 0x0013;

bool valid_delayed_write_address(u8 address)
{
	switch(address)
	{
		case 0: case 2: case 3: case 5: case 7:
		case 8: case 9: case 10: case 11:
			return true;
		default:
			return false;
	}
}

} // namespace

void LCD::lcd_init(bool boot_rom)
{
	// On DMG the LCD is off at power-on; the boot ROM enables it. Without a
	// boot ROM the emulator starts from the usual post-boot LCDC value.
	m_lcdc = boot_rom ? 0x00 : 0x91;
	m_lcds = 0;
	m_scroll_x = 0;
	m_scroll_y = 0;
	m_ly = 0;
	m_lyc = 0;
	m_bg_palette = 0xFC;
	m_obj_palette_0= 0xFF;
	m_obj_palette_1 = 0xFF;
	m_win_y = 0;
	m_win_x = 0;
	m_pending_stat = 0;
	m_pending_stat_value = 0;
	m_delayed_write_count = 0;
	m_ppu_write_delay = 3;
	if(const char* e = std::getenv("QTR_PPUWRITEDELAY"))
	{
		m_ppu_write_delay = static_cast<u8>(std::atoi(e));
	}
	for(int i = 0; i < 12; ++i) m_reg_write_delay[i] = m_ppu_write_delay;
	m_reg_write_delay[7] = 4;	// BGP
	m_reg_write_delay[8] = 4;	// OBP0
	m_reg_write_delay[3] = 4;	// SCX
	m_reg_write_delay[2] = 4;	// SCY
	if(const char* e = std::getenv("QTR_BGPWRITEDELAY")) m_reg_write_delay[7] = static_cast<u8>(std::atoi(e));
	m_bgp_write_delay_ly0 = 8;
	if(const char* e = std::getenv("QTR_BGPWRITEDELAY_LY0")) m_bgp_write_delay_ly0 = static_cast<u8>(std::atoi(e));
	if(const char* e = std::getenv("QTR_OBP0WRITEDELAY")) m_reg_write_delay[8] = static_cast<u8>(std::atoi(e));
	if(const char* e = std::getenv("QTR_OBP1WRITEDELAY")) m_reg_write_delay[9] = static_cast<u8>(std::atoi(e));
	if(const char* e = std::getenv("QTR_SCXWRITEDELAY")) m_reg_write_delay[3] = static_cast<u8>(std::atoi(e));
	if(const char* e = std::getenv("QTR_SCYWRITEDELAY")) m_reg_write_delay[2] = static_cast<u8>(std::atoi(e));
	if(const char* e = std::getenv("QTR_WXWRITEDELAY")) m_reg_write_delay[11] = static_cast<u8>(std::atoi(e));
	if(const char* e = std::getenv("QTR_WYWRITEDELAY")) m_reg_write_delay[10] = static_cast<u8>(std::atoi(e));
}

void LCD::tick()
{
	// On DMG a STAT write takes 1 T-cycle to become visible. The CPU writes it
	// during its phase; the PPU applies it at the end of the current dot, so
	// the next dot reads the new enable bits.
	if(m_pending_stat)
	{
		if(--m_pending_stat == 0)
		{
			m_lcds = static_cast<u8>(m_pending_stat_value | (m_lcds & 0x07));
		}
	}

	apply_delayed_writes();
}

void LCD::apply_delayed_writes()
{
	for(u8 i = 0; i < m_delayed_write_count; )
	{
		if(m_delayed_writes[i].countdown > 0)
		{
			--m_delayed_writes[i].countdown;
		}
		if(m_delayed_writes[i].countdown == 0)
		{
			const DelayedWrite w = m_delayed_writes[i];
			switch(w.address)
			{
				case 0: m_lcdc = w.value; break;
				case 2: m_scroll_y = w.value; break;
				case 3: m_scroll_x = w.value; break;
				case 5: m_lyc = w.value; break;
				case 7: m_bg_palette = w.value; break;
				case 8: m_obj_palette_0 = w.value; break;
				case 9: m_obj_palette_1 = w.value; break;
				case 10: m_win_y = w.value; break;
				case 11: m_win_x = w.value; break;
				default: break;
			}
			for(u8 j = i; j + 1 < m_delayed_write_count; j++)
			{
				m_delayed_writes[j] = m_delayed_writes[j + 1];
			}
			--m_delayed_write_count;
		}
		else
		{
			++i;
		}
	}
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
		case 1:
			// On DMG STAT takes 1 T-cycle to become visible; LCD::tick() applies it.
			m_pending_stat_value = static_cast<u8>(value & 0x78);
			m_pending_stat = 1;
			if(const char* e = std::getenv("QTR_STATWRITEDELAY"))
			{
				m_pending_stat = static_cast<u8>(m_pending_stat + std::atoi(e));
			}
			break;
		case 2: case 3: case 5: case 7: case 8: case 9: case 10: case 11:
		{
			const u8 delay = (address == 7 && m_ly == 0) ? m_bgp_write_delay_ly0 : m_reg_write_delay[address];
			if(delay == 0 || m_delayed_write_count >= 8)
			{
				switch(address)
				{
					case 2: m_scroll_y = value; break;
					case 3: m_scroll_x = value; break;
					case 5: m_lyc = value; break;
					case 7: m_bg_palette = value; break;
					case 8: m_obj_palette_0 = value; break;
					case 9: m_obj_palette_1 = value; break;
					case 10: m_win_y = value; break;
					case 11: m_win_x = value; break;
				}
			}
			else
			{
				m_delayed_writes[m_delayed_write_count++] = DelayedWrite{address, value, delay};
			}
			break;
		}
		case 4: /*m_ly = value*/; break;	// read_only
	}
}

void LCD::save_state(StateWriter& writer) const
{
	writer.write_u8(LCD_LCDC, m_lcdc);
	writer.write_u8(LCD_STAT, m_lcds);
	writer.write_u8(LCD_SCY, m_scroll_y);
	writer.write_u8(LCD_SCX, m_scroll_x);
	writer.write_u8(LCD_LY, m_ly);
	writer.write_u8(LCD_LYC, m_lyc);
	writer.write_u8(LCD_BGP, m_bg_palette);
	writer.write_u8(LCD_OBP0, m_obj_palette_0);
	writer.write_u8(LCD_OBP1, m_obj_palette_1);
	writer.write_u8(LCD_WY, m_win_y);
	writer.write_u8(LCD_WX, m_win_x);
	writer.write_u8(LCD_PENDING_STAT, m_pending_stat);
	writer.write_u8(LCD_PENDING_STAT_VALUE, m_pending_stat_value);
	writer.write_u8(LCD_DELAYED_COUNT, m_delayed_write_count);

	std::vector<u8> delayed;
	delayed.reserve(static_cast<size_t>(m_delayed_write_count) * 3);
	for(u8 i = 0; i < m_delayed_write_count; ++i)
	{
		delayed.push_back(m_delayed_writes[i].address);
		delayed.push_back(m_delayed_writes[i].value);
		delayed.push_back(m_delayed_writes[i].countdown);
	}
	writer.write_bytes(LCD_DELAYED_WRITES, delayed);
}

bool LCD::load_state(StateReader& reader)
{
	const LCD snapshot = *this;
	const auto fail = [&]()
	{
		*this = snapshot;
		return false;
	};

	u8 lcdc = 0;
	u8 lcds = 0;
	u8 scy = 0;
	u8 scx = 0;
	u8 ly = 0;
	u8 lyc = 0;
	u8 bgp = 0;
	u8 obp0 = 0;
	u8 obp1 = 0;
	u8 wy = 0;
	u8 wx = 0;
	u8 pending_stat = 0;
	u8 pending_value = 0;
	u8 delayed_count = 0;
	std::vector<u8> delayed;

	if(!reader.read_u8(LCD_LCDC, lcdc) ||
	   !reader.read_u8(LCD_STAT, lcds) ||
	   !reader.read_u8(LCD_SCY, scy) ||
	   !reader.read_u8(LCD_SCX, scx) ||
	   !reader.read_u8(LCD_LY, ly) ||
	   !reader.read_u8(LCD_LYC, lyc) ||
	   !reader.read_u8(LCD_BGP, bgp) ||
	   !reader.read_u8(LCD_OBP0, obp0) ||
	   !reader.read_u8(LCD_OBP1, obp1) ||
	   !reader.read_u8(LCD_WY, wy) ||
	   !reader.read_u8(LCD_WX, wx) ||
	   !reader.read_u8(LCD_PENDING_STAT, pending_stat) ||
	   !reader.read_u8(LCD_PENDING_STAT_VALUE, pending_value) ||
	   !reader.read_u8(LCD_DELAYED_COUNT, delayed_count) ||
	   !reader.read_bytes(LCD_DELAYED_WRITES, delayed))
	{
		return fail();
	}

	if(delayed_count > 8 ||
	   delayed.size() != static_cast<size_t>(delayed_count) * 3)
	{
		reader.mark_error(StateError::BadValue);
		return fail();
	}
	for(u8 i = 0; i < delayed_count; ++i)
	{
		if(!valid_delayed_write_address(delayed[i * 3]))
		{
			reader.mark_error(StateError::BadValue);
			return fail();
		}
	}

	m_lcdc = lcdc;
	m_lcds = lcds;
	m_scroll_y = scy;
	m_scroll_x = scx;
	m_ly = ly;
	m_lyc = lyc;
	m_bg_palette = bgp;
	m_obj_palette_0 = obp0;
	m_obj_palette_1 = obp1;
	m_win_y = wy;
	m_win_x = wx;
	m_pending_stat = pending_stat;
	m_pending_stat_value = pending_value;
	m_delayed_write_count = delayed_count;
	for(u8 i = 0; i < delayed_count; ++i)
	{
		m_delayed_writes[i].address = delayed[i * 3 + 0];
		m_delayed_writes[i].value = delayed[i * 3 + 1];
		m_delayed_writes[i].countdown = delayed[i * 3 + 2];
	}
	return true;
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
