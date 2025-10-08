#include <algorithm>

#include "ppu.h"
#include "QtreGB.h"
#include "lcd.h"
#include "fetcher.h"
#include "logger.h"

using namespace std;

const int PPU::LINES_PER_FRAME = 154;
const int PPU::TICKS_PER_LINE = 456;
const int PPU::YRES = 144;
const int PPU::XRES = 160;

static bool compare_function(OAMEntry e1, OAMEntry e2)
{
	return (e1.m_x < e2.m_x);
}

void PPU::set_interrupt_callback(InterruptCallback cb)
{
	m_request_interrupt = cb;
}

void PPU::set_frame_callback(FrameCallback cb)
{
	m_frame_done = cb;
}

PPU::PPU(LCD *lcd) :
m_lcd(lcd),
m_vram(Memory(0x2000)),
m_oam_ram(vector<OAMEntry>(40)),
m_oam_line_entry(vector<OAMEntry>()),
m_fetcher(Fetcher(m_bgw_fifo, m_spr_fifo))
{}

void PPU::ppu_init()
{
	m_line_ticks = 0;
	m_pushed_x = 0;
	m_line_x = 0;
	m_fetched = 0;
	m_is_vram_locked = false;
	m_fetcher.set_state(FetchEntry::FS_TILE);
	m_pixel_fifo = vector<u8>();
	m_video_buffer = vector<u8>(PPU::YRES * PPU::XRES);

	m_lcd->lcd_init();
	m_lcd->set_lcds_mode(LCDMode::MODE_OAM);

	m_oam_line_entry = vector<OAMEntry>();

	m_bgw_fifo.clear();
	m_spr_fifo.clear();
	m_fetcher.set_lcd(m_lcd);
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

void PPU::ppu_mode_oam()
{
	if(m_line_ticks == 1)
	{
		//read oam on the first tick only...
		m_oam_line_entry.clear();

		load_line_sprites();
		m_fetcher.load_oam_entries(m_oam_line_entry);
		//m_fetcher.check_obj(0);	// Check available sprites in lcd X = 0, after that, everytime that m_pushed changes
	}

	if(m_line_ticks == 80)
	{
		m_lcd->set_lcds_mode(LCDMode::MODE_XFER);

		m_fetcher.set_state(FetchEntry::FS_TILE);
		m_pushed_x = 0;
		m_line_x = 0;
		m_fetched = 0;
		pipeline_fifo_reset();
		//std::fill(m_video_buffer.begin(), m_video_buffer.end(), 0);
	}

}

void PPU::ppu_mode_xfer()
{
	if(m_line_ticks == 81)
	{
		// TODO fix this
		//m_is_vram_locked = true;
	}

	pipeline_process();

	if(m_pushed_x >= PPU::XRES || m_line_ticks >= 289 /* It should never exit from this condition...*/)
	{
		m_lcd->set_lcds_mode(LCDMode::MODE_HBLANK);

		if(m_lcd->get_lcds_stat_int(StatSRC::SS_HBLANK))
		{
			request_interrupt(InterruptType::IT_LCD_STAT);
		}
	}
}

void PPU::ppu_mode_hblank()
{
	m_is_vram_locked = false;

	if(m_line_ticks == PPU::TICKS_PER_LINE) // If reached to the end of hblank we increment the internal counter to actual line
	{
		increment_ly();

		if(m_lcd->m_ly == PPU::YRES) // If reached to the number of lines the lcd has, we go to vblank
		{
			m_lcd->set_lcds_mode(LCDMode::MODE_VBLANK);
			request_interrupt(InterruptType::IT_VBLANK);

			if(m_lcd->get_lcds_stat_int(StatSRC::SS_VBLANK))
			{
				request_interrupt(InterruptType::IT_LCD_STAT);
			}
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
			if(m_frame_done)
			{
				m_frame_done(true); // We have all the info to draw the screen
			}
		}

		m_line_ticks = 0;
		m_window_line = 0;
		m_fetcher.set_window_line(m_window_line);
	}
}

void PPU::increment_ly()
{
	m_lcd->m_ly++;

	if(m_lcd->m_ly >= m_lcd->m_win_y)
	{
		m_window_line++;
		m_fetcher.set_window_line(m_window_line);
	}

	if(m_lcd->m_ly == m_lcd->m_lyc)
	{
		m_lcd->set_lcds_lyc(true);
		if(m_lcd->get_lcds_stat_int(StatSRC::SS_LYC))
		{
			request_interrupt(InterruptType::IT_LCD_STAT);
		}
	}
	else
	{
		m_lcd->set_lcds_lyc(false);
	}
}

void PPU::load_line_sprites()
{
	u8 current_y = m_lcd->m_ly;
	u8 sprite_height = m_lcd->get_lcdc_obj_height();

	// We save a list of the maximun 10 sprites that exists in a line
	// With this loop we get the first 10 elements that exists in OAM memory
	for(auto oam_ram_element : m_oam_ram)
	{
		if(m_oam_line_entry.size() >= 10)
		{
			//max 10 sprites per line...
			break;
		}

		if(oam_ram_element.m_y <= current_y + 16 && oam_ram_element.m_y + sprite_height > current_y + 16)
		{
			m_oam_line_entry.push_back(oam_ram_element);
		}
	}

	// With this we have ordered the sprites in the line in the same order of preference in screen, lesser X sprite has preference.
	// If two sprites have the same X, the earlier object in OAM wins, I hope this works...
	if(!m_oam_line_entry.empty())
	{
		sort(m_oam_line_entry.begin(), m_oam_line_entry.end(), compare_function);
	}
}


//******************//
// Pipeline methods //
//******************//
void PPU::pipeline_fifo_reset()
{
	m_bgw_fifo.clear();
	m_spr_fifo.clear();

	m_fetcher.reset();
	m_fetcher.set_current_x(0);
	m_fetcher.check_mode();
	//m_fetcher.check_obj(0);
}

void PPU::pipeline_process()
{
	//LOG("line ticks", "======================== " + to_string(static_cast<int>(m_line_ticks)));
	if((m_line_ticks & 1) == 0)
	{
		m_fetcher.tick();
	}

	pipeline_push_pixel();
}

void PPU::pipeline_push_pixel()
{
	u8 pixel_data = 0;

	// We check in which state we are
	if(m_line_ticks == 81)
	{
		// In the first tick of mode 3 we set the fetcher to fetch background pixels
		m_fetcher.set_mode(FetchMode::FS_BGD);
		m_fetcher.set_state(FetchEntry::FS_TILE);
		m_discarded_pixels = 0;
		m_fetcher.fill_object_array();
	}

	if(m_line_ticks < 92 || m_bgw_fifo.empty())
	{
		return;
	}

	if(m_line_ticks == 92)
	{
		m_fetcher.set_warming(false);
	}

	if (m_discarded_pixels < (m_lcd->m_scroll_x % 8))
	{
		m_bgw_fifo.pop();
		if(!m_spr_fifo.empty())
		{
			//m_spr_fifo.pop();
		}
		m_discarded_pixels++;
		return;
	}

	FIFOElement bgw_element = m_bgw_fifo.pop();
	pixel_data = (m_lcd->m_bg_palette >> (bgw_element.get_color_index() * 2)) & 0x03;

	/*
	if(!m_spr_fifo.empty())
	{
		FIFOElement spr_element = m_spr_fifo.pop();
		LOG("pipeline_push_pixel color", to_hex(spr_element.get_color_index()));
		if(spr_element.get_color_index() != 0)
		{
			if(!spr_element.get_priority())
			{
				u8 palette = spr_element.get_palette() ? m_lcd->m_obj_palette_0 : m_lcd->m_obj_palette_1;
				pixel_data = (palette >> (spr_element.get_color_index() * 2)) & 0x03;
			}
		}
	}
	*/


	FIFOElement spr_element = m_fetcher.get_object_array()[m_pushed_x];
	if((!spr_element.get_priority() && spr_element.get_color_index() != 0) || (spr_element.get_priority() && bgw_element.get_color_index() == 0))
	{
		u8 palette = spr_element.get_palette() ? m_lcd->m_obj_palette_1 : m_lcd->m_obj_palette_0;
		pixel_data = (palette >> (spr_element.get_color_index() * 2)) & 0x03;
	}


	m_video_buffer[m_pushed_x +	(m_lcd->m_ly * PPU::XRES)] = pixel_data;
	m_pushed_x++;
	m_fetcher.set_current_x(m_pushed_x);
	m_fetcher.check_mode();
}


const std::vector<OAMEntry> &PPU::get_oam_line_entry() const
{
	return m_oam_line_entry;
}

void PPU::connect_bus(IBus *bus)
{
	m_bus = bus;
	m_fetcher.set_bus(m_bus);
}

bool PPU::is_vram_locked()
{
	return m_is_vram_locked;
}

void PPU::request_interrupt(InterruptType IType)
{
	if (m_request_interrupt)
	{
		m_request_interrupt(IType);
	}
}
