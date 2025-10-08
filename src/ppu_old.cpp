#if 0
#include <algorithm>

#include "ppu.h"
#include "QtreGB.h"
#include "lcd.h"

using namespace std;

const int PPU::LINES_PER_FRAME = 154;
const int PPU::TICKS_PER_LINE = 456;
const int PPU::YRES = 144;
const int PPU::XRES = 160;

static bool compare_function(OAMEntry e1, OAMEntry e2)
{
	return (e1.m_x < e2.m_x);
}

OAMEntry::OAMEntry()
{
	m_flags = 0;
	m_tile = 0;
	m_x = 0;
	m_y = 0;
}

PPU::PPU(QtreGB *emu) :
m_emu(emu),
m_vram(Memory(0x2000)),
m_oam_ram(vector<OAMEntry>(40)),
m_oam_line_entry(list<OAMEntry>())
{
	m_lcd = &(m_emu->m_lcd);
}

void PPU::ppu_init()
{
	m_line_ticks = 0;

	m_line_x = 0;
	m_pushed_x = 0;
	m_fetch_x = 0;
	m_current_fetch_state = FetchEntry::FS_TILE;
	m_pixel_fifo = queue<u8>();
	m_video_buffer = vector<u8>(PPU::YRES * PPU::XRES);

	m_lcd->lcd_init();
	m_lcd->set_lcds_mode(LCDMode::MODE_OAM);

	m_oam_line_entry = list<OAMEntry>();
	m_fetched_entry_count = 0;
	m_window_line = 0;
}

void PPU::ppu_tick()
{
	m_line_ticks++;
	switch(m_lcd->get_lcds_mode())
	{
		case LCDMode::MODE_OAM:
			ppu_mode_oam();
			break;
		case LCDMode::MODE_XFER:
			ppu_mode_xfer();
			break;
		case LCDMode::MODE_VBLANK:
			ppu_mode_vblank();
			break;
		case LCDMode::MODE_HBLANK:
			ppu_mode_hblank();
			break;
		default: break;
	}
}

u8 PPU::ppu_oam_read(u16 address)
{
	u8 value = 0;
	switch(address % 4)
	{
		case 0:
			value =m_oam_ram[address / 4].m_y;
		break;

		case 1:
			value = m_oam_ram[address / 4].m_x;
		break;

		case 2:
			value = m_oam_ram[address / 4].m_tile;
		break;

		case 3:
			value = m_oam_ram[address / 4].m_flags;
		break;
	}

	return value;
}

void PPU::ppu_oam_write(u16 address, u8 value)
{
	switch(address % 4)
	{
		case 0:
			m_oam_ram[address / 4].m_y = value;
		break;

		case 1:
			m_oam_ram[address / 4].m_x = value;
		break;

		case 2:
			m_oam_ram[address / 4].m_tile = value;
		break;

		case 3:
			m_oam_ram[address / 4].m_flags = value;
		break;
	}
}

u8 PPU::ppu_vram_read(u16 address)
{
	return m_vram.read(address);
}

void PPU::ppu_vram_write(u16 address, u8 value)
{
	m_vram.write(address, value);
}

const std::vector<u8> &PPU::get_video_buffer() const
{
	return m_video_buffer;
}

void PPU::ppu_mode_oam()
{
	if(m_line_ticks >= 80)
	{
		m_lcd->set_lcds_mode(LCDMode::MODE_XFER);

		m_current_fetch_state = FetchEntry::FS_TILE;
		m_line_x = 0;
		m_fetch_x = 0;
		m_pushed_x = 0;
		m_fifo_x = 0;
	}

	if(m_line_ticks == 1)
	{
		//read oam on the first tick only...
		m_oam_line_entry.clear();

		load_line_sprites();
	}
}

void PPU::ppu_mode_xfer()
{
	pipeline_process();

	if(m_pushed_x >= PPU::XRES)
	{
		pipeline_fifo_reset();
		m_lcd->set_lcds_mode(LCDMode::MODE_HBLANK);

		if(m_lcd->get_lcds_stat_int(StatSRC::SS_HBLANK))
		{
			m_emu->m_cpu.request_interrupt(InterruptType::IT_LCD_STAT);
		}
	}
}

void PPU::ppu_mode_hblank()
{
	if(m_line_ticks == PPU::TICKS_PER_LINE)
	{
		increment_ly();

		if(m_lcd->m_ly == PPU::YRES)
		{
			m_lcd->set_lcds_mode(LCDMode::MODE_VBLANK);
			m_emu->m_cpu.request_interrupt(InterruptType::IT_VBLANK);

			if(m_lcd->get_lcds_stat_int(StatSRC::SS_VBLANK))
			{
				m_emu->m_cpu.request_interrupt(InterruptType::IT_LCD_STAT);
			}
			m_emu->m_frame_ready = true;
		}
		else
		{
			m_lcd->set_lcds_mode(LCDMode::MODE_OAM);
		}
		m_line_ticks = 0;
	}
}

void PPU::ppu_mode_vblank()
{
	if(m_line_ticks == PPU::TICKS_PER_LINE)
	{
		increment_ly();

		if(m_lcd->m_ly == PPU::LINES_PER_FRAME)
		{
			m_lcd->set_lcds_mode(LCDMode::MODE_OAM);
			m_lcd->m_ly = 0;
			m_window_line = 0;
		}

		m_line_ticks = 0;
	}
}

void PPU::increment_ly()
{
	if(window_visible() &&
	   m_lcd->m_ly >= m_lcd->m_win_y &&
	   m_lcd->m_ly < m_lcd->m_win_y + PPU::YRES
	)
	{
		m_window_line++;
	}

	m_lcd->m_ly++;

	if(m_lcd->m_ly == m_lcd->m_lyc)
	{
		m_lcd->set_lcds_lyc(true);
		if(m_lcd->get_lcds_stat_int(StatSRC::SS_LYC))
		{
			m_emu->m_cpu.request_interrupt(InterruptType::IT_LCD_STAT);
		}
	}
	else
	{
		m_lcd->set_lcds_lyc(false);
	}
}

void PPU::pipeline_fifo_reset()
{
	while(!m_pixel_fifo.empty())
	{
		m_pixel_fifo.pop();
	}
}

void PPU::pipeline_process()
{
	m_map_x = m_fetch_x + m_lcd->m_scroll_x;
	m_map_y = m_lcd->m_ly + m_lcd->m_scroll_y;
	m_tile_y = (m_map_y % 8) * 2;

	if((m_line_ticks & 1) == 0)
	{
		pipeline_fetch();
	}

	pipeline_push_pixel();
}

void PPU::pipeline_fetch()
{
	switch(m_current_fetch_state)
	{
		case FetchEntry::FS_TILE:
		{
			m_fetched_entry_count = 0;

			if(m_lcd->get_lcdc_bgw_enable())
			{
				m_bgw_fetch_data[0] = m_emu->m_bus.read(m_lcd->get_lcdc_bg_map_area() +
					(m_map_x / 8) +
					((m_map_y / 8) * 32));

				if(m_lcd->get_lcdc_bgw_data_area() == 0x8800)
				{
					m_bgw_fetch_data[0] += 128;
				}

				pipeline_load_window_tile();
			}

			if(m_lcd->get_lcdc_obj_enable() && m_oam_line_entry.size() != 0)
			{
				pipeline_load_sprite_tile();
			}

			m_current_fetch_state = FetchEntry::FS_DATA0;
			m_fetch_x += 8;
		}
		break;

		case FetchEntry::FS_DATA0:
		{
			m_bgw_fetch_data[1] = m_emu->m_bus.read(m_lcd->get_lcdc_bgw_data_area() +
				(m_bgw_fetch_data[0] * 16) + m_tile_y);

			pipeline_load_sprite_data(0);

			m_current_fetch_state = FetchEntry::FS_DATA1;
		}
		break;

		case FetchEntry::FS_DATA1:
		{
			m_bgw_fetch_data[2] = m_emu->m_bus.read(m_lcd->get_lcdc_bgw_data_area() +
				(m_bgw_fetch_data[0] * 16) + m_tile_y + 1);

			pipeline_load_sprite_data(1);

			m_current_fetch_state = FetchEntry::FS_IDLE;
		}
		break;

		case FetchEntry::FS_IDLE:
		{
			m_current_fetch_state = FetchEntry::FS_PUSH;
		}
		break;

		case FetchEntry::FS_PUSH:
		{
			if(pipeline_fifo_add())
			{
				m_current_fetch_state = FetchEntry::FS_TILE;
			}
		}
		break;
	}
}

void PPU::pipeline_push_pixel()
{
	if(m_pixel_fifo.size() > 8)
	{
		u8 pixel_data = pixel_fifo_pop();

		if(m_line_x >= (m_lcd->m_scroll_x % 8))
		{
			m_video_buffer[m_pushed_x +	(m_lcd->m_ly * PPU::XRES)] = pixel_data;
			m_pushed_x++;
		}
		m_line_x++;
	}
}

bool PPU::pipeline_fifo_add()
{
	if(m_pixel_fifo.size() > 8)
	{
		// fifo is full
		return false;
	}
	u8 color;
	u8 index_hi;
	u8 index_lo;
	u8 index;
	int x = m_fetch_x - (8 - (m_lcd->m_scroll_x % 8));

	for(u8 i = 0; i < 8; i++)
	{
		int bit = 7 - i;
		index_lo = !!(m_bgw_fetch_data[1] & (1 << bit));
		index_hi = !!(m_bgw_fetch_data[2] & (1 << bit)) << 1;
		index = index_hi | index_lo;

		if(!m_lcd->get_lcdc_bgw_enable())
		{
			// If background isn't enabled, we get the color 0 from background palette
			color = m_lcd->m_bg_palette & 0x03;
		}
		else
		{
			color = (m_lcd->m_bg_palette >> (index * 2)) & 0x03;
		}

		if(m_lcd->get_lcdc_obj_enable())
		{
			color = fetch_sprite_pixels(bit, color, index);
		}

		if(x >= 0)
		{
			pixel_fifo_push(color);
			m_fifo_x++;
		}
	}
	return true;
}

void PPU::load_line_sprites()
{
	u8 current_y = m_lcd->m_ly;
	u8 sprite_height = m_lcd->get_lcdc_obj_height();

	for(int i = 0; i < 40; i++)
	{
		OAMEntry entry = m_oam_ram[i];

		if(entry.m_x == 0)
		{
			//x = 0 means not visible... but it counts to the limit of objects in the scanline
			//continue;
		}

		if(m_oam_line_entry.size() >= 10)
		{
			//max 10 sprites per line...
			break;
		}

		if(entry.m_y <= current_y + 16 && entry.m_y + sprite_height > current_y + 16)
		{
			m_oam_line_entry.push_back(entry);
			m_oam_line_entry.sort(compare_function);
		}
	}
}

u8 PPU::fetch_sprite_pixels(u8 bit, u8 color, u8 index)
{
	for(int i = 0; i < m_fetched_entry_count; i++)
	{
		int sp_x = (m_fetched_entries[i].m_x - 8) +
			(m_lcd->m_scroll_x % 8);

		if(sp_x + 8 < m_fifo_x)
		{
			//past pixel point already...
			continue;
		}

		int offset = m_fifo_x - sp_x;

		if(offset < 0 || offset > 7)
		{
			//out of bounds...
			continue;
		}

		bit = (7 - offset);
		if((m_fetched_entries[i].m_flags & (1 << 5)) != 0)
		{
			bit = offset;
		}


		u8 lo = !!(m_fetch_entry_data[i * 2] & (1 << bit));
		u8 hi = !!(m_fetch_entry_data[(i * 2) + 1] & (1 << bit)) << 1;

		bool bg_priority = (m_fetched_entries[i].m_flags & (1 << 7)) != 0;

		if((hi|lo) == 0)
		{
			//transparent
			continue;
		}

		// if background doesn't have priority and it's not transparent(index = 0 in the palette indicates transparency for sprites)
		// we get the color of the sprite instead of background.
		if(!bg_priority || index == 0)
		{
			if((m_fetched_entries[i].m_flags & (1 << 4)) != 0)
			{
				color = (m_lcd->m_obj_palette_1 >> ((lo|hi) * 2)) & 0x03;
			}
			else
			{
				color = (m_lcd->m_obj_palette_0 >> ((lo|hi) * 2)) & 0x03;
			}
			if(hi|lo)
			{
				// With this, the left most sprite have priority, the first sprite in drawing a color is the winner.
				break;
			}
		}
	}
	return color;
}

void PPU::pipeline_load_sprite_tile()
{
	for(auto it = m_oam_line_entry.begin(); it != m_oam_line_entry.end(); ++it)
	{
		int sp_x = (it->m_x - 8) + (m_lcd->m_scroll_y % 8);

		if((sp_x >= m_fetch_x && sp_x < m_fetch_x + 8) ||
			((sp_x + 8) >= m_fetch_x && (sp_x + 8) < m_fetch_x + 8))
		{
			//need to add entry
			m_fetched_entries[m_fetched_entry_count] = *it;
			m_fetched_entry_count++;
		}

		if(m_fetched_entry_count >= 3)
		{
			//max checking 3 sprites on pixels
			break;
		}
	}
}

void PPU::pipeline_load_sprite_data(u8 offset)
{
	int current_y = m_lcd->m_ly;
	u8 sprite_height = m_lcd->get_lcdc_obj_height();

	for(int i = 0; i < m_fetched_entry_count; i++)
	{
		u8 ty = ((current_y + 16) - m_fetched_entries[i].m_y) * 2;

		if((m_fetched_entries[i].m_flags & (1 << 6)) != 0)
		{
			ty = ((sprite_height * 2) - 2) - ty;
		}

		u8 tile_index = m_fetched_entries[i].m_tile;

		if(sprite_height == 16)
		{
			tile_index &= ~(1); //remove last bit...
		}

		m_fetch_entry_data[(i * 2) + offset] = m_emu->m_bus.read(0x8000 + (tile_index * 16) + ty + offset);
	}
}

void PPU::pipeline_load_window_tile()
{
	if(!window_visible())
	{
		return;
	}

	if(m_fetch_x + 7 >= m_lcd->m_win_x &&
	   m_fetch_x + 7 < m_lcd->m_win_x + PPU::XRES + 14
	)
	{
		if(m_lcd->m_ly >= m_lcd->m_win_y &&
			m_lcd->m_ly < m_lcd->m_win_y + PPU::YRES
		)
		{
			m_bgw_fetch_data[0] = m_emu->m_bus.read(m_lcd->get_lcdc_win_map_area() +
				((m_fetch_x + 7 - m_lcd->m_win_x) / 8) +
				((m_window_line / 8) * 32));

			if(m_lcd->get_lcdc_bgw_data_area() == 0x8800)
			{
				m_bgw_fetch_data[0] += 128;
			}
		}
	}
}

bool PPU::window_visible()
{
	if(m_lcd->get_lcdc_win_enable()// &&
		//m_lcd->m_win_x < 166 &&
		//m_lcd->m_win_y < PPU::YRES
	)
	{
		return true;
	}

	return false;
}

void PPU::pixel_fifo_push(u8 value)
{
	m_pixel_fifo.push(value);
}

u8 PPU::pixel_fifo_pop()
{
	if(m_pixel_fifo.empty())
	{
		return 0xFF;	// It never should go here...
	}
	u8 value = m_pixel_fifo.front();
	m_pixel_fifo.pop();
	return value;
}
#endif
