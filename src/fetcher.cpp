#include <sstream>

#include "fetcher.h"
#include "QtreGB.h"
#include "lcd.h"
#include "logger.h"

Fetcher::Fetcher(FIFO & bgw_fifo, FIFO & spr_fifo) :
m_bgw_fifo(bgw_fifo),
m_spr_fifo(spr_fifo)
{}

void Fetcher::reset()
{
	m_warming = true;
	m_current_x = 0;
	m_mode = FetchMode::FS_BGD;
	m_state_bg = FetchEntry::FS_TILE;
	m_object_array.fill(FIFOElement(0, false, true));
	m_bg_need_recalculate = true;
	m_bg_tile_to_push = 0;
	m_bg_mode = FetchModeBG::FS_BG_BGD;
	m_bgw_fifo.clear();

	is_window = (m_lcd->m_ly >= m_lcd->m_win_y);
}

void Fetcher::set_state(FetchEntry state)
{
	m_current_state = state;
}

void Fetcher::set_mode(FetchMode mode)
{
	m_mode = mode;
}

void Fetcher::tick()
{

	if(m_mode == FetchMode::FS_BGD)
	{
		//m_state_bg = m_current_state;
	}

	if(m_warming)
	{
		// In the 12 dots, always is background mode
		m_mode = FetchMode::FS_BGD;
	}
	else
	{
		//check_obj(m_current_x);
		//m_mode = FetchMode::FS_BGD;
		//check_mode();
	}

	if(m_mode == FetchMode::FS_BGD)
	{
		sm_bg();
	}

/*
	switch(m_current_state)
	{
		case FetchEntry::FS_TILE:
		{
			switch(m_mode)
			{
				case FetchMode::FS_BGD: fill_map_background(); break;
				//case FetchMode::FS_WIN: fill_map_window(); break;
				case FetchMode::FS_OBJ: fill_map_sprite(); break;
			}
			m_current_state = FetchEntry::FS_DATA0;
		}
		break;

		case FetchEntry::FS_DATA0:
		{
			switch(m_mode)
			{
				case FetchMode::FS_BGD: load_data_background(0); break;
				//case FetchMode::FS_WIN: load_data_window(0); break;
				case FetchMode::FS_OBJ: load_data_sprite(0); break;
			}
			m_current_state = FetchEntry::FS_DATA1;
		}
		break;

		case FetchEntry::FS_DATA1:
		{
			switch(m_mode)
			{
				case FetchMode::FS_BGD: load_data_background(1); break;
				//case FetchMode::FS_WIN: load_data_window(1); break;
				case FetchMode::FS_OBJ: load_data_sprite(1); break;
			}
			m_current_state = FetchEntry::FS_PUSH;
		}
		break;

		case FetchEntry::FS_PUSH:
		{
			bool result;
			switch(m_mode)
			{
				case FetchMode::FS_BGD: result = push_data_background(); break;
				//case FetchMode::FS_WIN: result = push_data_window(); break;
				case FetchMode::FS_OBJ: result = push_data_sprite(); break;
			}
			if(result)
			{
				m_current_state = FetchEntry::FS_TILE;
			}
		}
		break;
	}

*/
}

void Fetcher::check_obj(u8 x)
{
	for(u8 i= 0; i < m_oam_line_entries.size(); i++)
	{
		OAM_line_Entry& entry = m_oam_line_entries.get_entry(i);
		if(entry.m_oam_entry.m_x <= x + 16 && !entry.m_done)
		{
			entry.m_marked = true;
			if(m_mode == FetchMode::FS_BGD)
			{
				m_mode = FetchMode::FS_OBJ;
				m_current_state = FetchEntry::FS_TILE;
				return;
			}
		}
	}
	m_mode = FetchMode::FS_BGD;
}

void Fetcher::set_window_line(u8 window_line)
{
	m_window_line = window_line;
}

void Fetcher::set_bus(IBus* bus)
{
	m_bus = bus;
}

void Fetcher::set_lcd(LCD* lcd)
{
	m_lcd = lcd;
}

void Fetcher::set_current_x(u8 x)
{
	m_current_x = x;
}

void Fetcher::set_warming(bool warming)
{
	m_warming = warming;
}

void Fetcher::load_oam_entries(std::vector<OAMEntry> entries)
{
	m_oam_line_entries.clear();
	for(size_t i = 0; i < entries.size(); i++)
	{
		m_oam_line_entries.add_entry(entries[i]);
	}
}

void Fetcher::fill_object_array()
{
	for(u8 i = 0; i < m_oam_line_entries.size(); i++)
	{
		OAMEntry entry = m_oam_line_entries.get_entry(i).m_oam_entry;
		u8 tile = entry.m_tile;
		u8 sprite_height = m_lcd->get_lcdc_obj_height();
		u8 current_y = m_lcd->m_ly;
		u8 sprite_data_y = 0;
		u8 data_lo = 0;
		u8 data_hi = 0;

		sprite_data_y = ((current_y + 16) - entry.m_y) * 2;

		if(sprite_height == 16)
		{
			tile &= ~(1); //remove last bit...
		}

		if((entry.m_flags & (1 << 6)) != 0)
		{
			sprite_data_y = ((sprite_height * 2) - 2) - sprite_data_y;
		}

		data_lo = m_bus->read(0x8000 + (tile * 16) + sprite_data_y + 0);
		data_hi = m_bus->read(0x8000 + (tile * 16) + sprite_data_y + 1);

		bool palette = (entry.m_flags & (1 << 4)) != 0;
		bool priority = (entry.m_flags & (1 << 7)) != 0;

		for(int j = 0; j < 8; j++)
		{

			u8 xpos = entry.m_x + j;

			u8 bit = 7 - j;
			if((entry.m_flags & (1 << 5)) != 0)
			{
				bit = 7 - bit;
			}

			u8 index_lo = !!((data_lo) & (1 << bit));
			u8 index_hi = !!(((data_hi)) & (1 << bit)) << 1;
			u8 color_index = index_hi | index_lo;

			if(xpos >= 8 && xpos < 168)
			{
				FIFOElement original_element = m_object_array[xpos - 8];
				FIFOElement element = FIFOElement(color_index, palette, priority);

				if(original_element.get_color_index() == 0)
				{
					m_object_array[xpos - 8] = element;
				}
			}

		}

	}
}

std::array<FIFOElement, 160> &Fetcher::get_object_array()
{
	return m_object_array;
}

/*********************/
/* Fill Map data     */
/*********************/
void Fetcher::fill_map_background()
{
	u8 tile_map_y;

	if(m_bg_mode == FetchModeBG::FS_BG_BGD)
	{
		tile_map_y = (m_lcd->m_ly + m_lcd->m_scroll_y) / 8;
		if(m_bg_need_recalculate)
		{
			m_bg_tile_to_push = ((m_lcd->m_scroll_x + m_current_x) / 8) & 0x1F;
			m_bg_need_recalculate = false;
		}
		m_bg_tile_to_push = m_bg_tile_to_push & 0x1F;
		LOG("fill_map_background tile", to_hex(m_bg_tile_to_push));

		m_tile_map_bg = m_bus->read(m_lcd->get_lcdc_bg_map_area() + m_bg_tile_to_push + (tile_map_y * 32));

		if(m_lcd->get_lcdc_bgw_data_area() == 0x8800)
		{
			m_tile_map_bg += 128;
		}

	}
	else
	{
		tile_map_y = m_lcd->m_ly - m_lcd->m_win_y;
		m_tile_map_bg = m_bus->read(m_lcd->get_lcdc_win_map_area() + ((m_current_x + 7 - m_lcd->m_win_x) / 8) + ((m_window_line / 8) * 32));

		if(m_lcd->get_lcdc_bgw_data_area() == 0x8800)
		{
			m_tile_map_bg += 128;
		}
	}



	LOG("fill_map_background tilemap", to_hex(m_tile_map_bg));
}

void Fetcher::fill_map_sprite()
{
	u8 x = 255;
	// We get the first one that is marked with the smallest X, it is ordered by OAM priority
	for(u8 i= 0; i < m_oam_line_entries.size(); i++)
	{
		OAM_line_Entry& entry = m_oam_line_entries.get_entry(i);
		if(entry.m_oam_entry.m_x < x && entry.m_marked && !entry.m_done)
		{
			m_current_oam_entry = i;
			x = entry.m_oam_entry.m_x;
			m_tile_map_spr = entry.m_oam_entry.m_tile;
		}
	}



}

/*********************/
/* Fill data         */
/*********************/
void Fetcher::load_data_background(u8 offset)
{
	if(offset > 1)
	{
		return;
	}

	u8 tile_data_y;
	u8 data;

	if(m_bg_mode == FetchModeBG::FS_BG_BGD)
	{
		tile_data_y = (m_lcd->m_ly + m_lcd->m_scroll_y) % 8 * 2;
		data = m_bus->read(m_lcd->get_lcdc_bgw_data_area() + (m_tile_map_bg * 16) + tile_data_y + offset);
	}
	else
	{
		tile_data_y = m_lcd->m_ly - m_lcd->m_win_y;
		data = m_bus->read(m_lcd->get_lcdc_bgw_data_area() + (m_tile_map_bg * 16) + (tile_data_y * 2) + offset);
	}

	if(offset == 0)
	{
		m_data_lo_bg = data;
	}
	else
	{
		m_data_hi_bg = data;
	}
}


void Fetcher::load_data_sprite(u8 offset)
{
	if(offset > 1)
	{
		return;
	}
	u8 sprite_height = m_lcd->get_lcdc_obj_height();
	u8 current_y = m_lcd->m_ly;
	u8 sprite_data_y = 0;

	sprite_data_y = ((current_y + 16) - m_oam_line_entries.get_entry(m_current_oam_entry).m_oam_entry.m_y) * 2;

	if(sprite_height == 16)
	{
		m_tile_map_spr &= ~(1); //remove last bit...
	}

	if((m_oam_line_entries.get_entry(m_current_oam_entry).m_oam_entry.m_flags & (1 << 6)) != 0)
	{
		sprite_data_y = ((sprite_height * 2) - 2) - sprite_data_y;
	}

	u8 data = m_bus->read(0x8000 + (m_tile_map_spr * 16) + sprite_data_y + offset);

	if(offset == 0)
	{
		m_data_lo_spr = data;
	}
	else
	{
		m_data_hi_spr = data;
	}
}

/*********************/
/* Push data         */
/*********************/
bool Fetcher::push_data_background()
{
	if(!m_bgw_fifo.empty())
	{
		return false;
	}

	for(u8 i = 0; i < 8; i++)
	{
		u8 bit = 7 - i;
		u8 index_lo = !!((m_data_lo_bg) & (1 << bit));
		u8 index_hi = !!(((m_data_hi_bg)) & (1 << bit)) << 1;
		u8 color_index = index_hi | index_lo;

		m_bgw_fifo.push(FIFOElement(color_index, false, false));
	}

	m_bg_tile_to_push++;

	return true;
}

bool Fetcher::push_data_window()
{
	if(!m_bgw_fifo.empty())
	{
		return false;
	}

	for(u8 i = 0; i < 8; i++)
	{
		u8 bit = 7 - i;
		u8 index_lo = !!((m_data_lo_bg) & (1 << bit));
		u8 index_hi = !!(((m_data_hi_bg)) & (1 << bit)) << 1;
		u8 color_index = index_hi | index_lo;

		m_bgw_fifo.push(FIFOElement(color_index, false, false));
	}

	return true;
}

bool Fetcher::push_data_sprite()
{
	/*
	u8 offset = 0;
	if(m_oam_line_entries.get_entry(m_current_oam_entry).m_oam_entry.m_x < 8)
	{
		offset = 8 - m_oam_line_entries.get_entry(m_current_oam_entry).m_oam_entry.m_x;
	}
	offset = 0;
	*/

	while(m_spr_fifo.size() < 8)
	{
		// We fill the sprite fifo until it has 8(or needed) elements with transparent pixels
		m_spr_fifo.push(FIFOElement(0, false, true));
	}


	bool palette = (m_oam_line_entries.get_entry(m_current_oam_entry).m_oam_entry.m_flags & (1 << 4)) != 0;
	bool priority = (m_oam_line_entries.get_entry(m_current_oam_entry).m_oam_entry.m_flags & (1 << 7)) != 0;

	for(u8 i = 0; i < 8; i++)
	{
		u8 bit = 7 - i;
		if((m_oam_line_entries.get_entry(m_current_oam_entry).m_oam_entry.m_flags & (1 << 5)) != 0)
		{
			bit = 7 - bit;
		}

		u8 index_lo = !!((m_data_lo_spr) & (1 << bit));
		u8 index_hi = !!(((m_data_hi_spr)) & (1 << bit)) << 1;
		u8 color_index = index_hi | index_lo;

		FIFOElement element = FIFOElement(color_index, palette, priority);
		FIFOElement original_element = m_spr_fifo.get(i);
		if(original_element.get_color_index() == 0)
		{
			m_spr_fifo.set(i, element);
		}
	}

	m_oam_line_entries.get_entry(m_current_oam_entry).m_done = true;

	return true;
}

void Fetcher::check_mode()
{
	if(m_mode == FetchMode::FS_BGD)
	{
		// If it's BGD mode, check for window
		if(m_bg_mode == FetchModeBG::FS_BG_BGD)
		{
			if(m_current_x + 7 >= m_lcd->m_win_x &&
				m_lcd->get_lcdc_win_enable() &&
				m_lcd->m_win_x <= 166 &&
				m_lcd->m_win_y < PPU::YRES &&
				is_window
			)
			{
				m_bgw_fifo.clear();
				m_bg_mode = FetchModeBG::FS_BG_WIN;
				m_state_bg = FetchEntry::FS_TILE;
				m_bg_need_recalculate = true;
			}
		}
		// If it's WIN mode, check to return to background mode
		else if(m_bg_mode == FetchModeBG::FS_BG_WIN)
		{
			if(!(m_current_x + 7 >= m_lcd->m_win_x &&
				m_lcd->get_lcdc_win_enable() &&
				m_lcd->m_win_x <= 166 &&
				m_lcd->m_win_y < PPU::YRES &&
				is_window )
			)
			{
				m_bgw_fifo.clear();
				m_bg_mode = FetchModeBG::FS_BG_BGD;
				m_state_bg = FetchEntry::FS_TILE;
				m_bg_need_recalculate = true;
			}
		}
	}
}

void Fetcher::sm_bg()
{
	switch(m_state_bg)
	{
		case FetchEntry::FS_TILE:
		{
			fill_map_background();
			m_state_bg = FetchEntry::FS_DATA0;
		}
		break;

		case FetchEntry::FS_DATA0:
		{
			load_data_background(0);
			m_state_bg = FetchEntry::FS_DATA1;
		}
		break;

		case FetchEntry::FS_DATA1:
		{
			load_data_background(1);
			m_state_bg = FetchEntry::FS_PUSH;
		}
		break;

		case FetchEntry::FS_PUSH:
		{
			bool result;
			result = push_data_background();

			if(result)
			{
				m_state_bg = FetchEntry::FS_TILE;
			}
		}
		break;
	}
}

OAM_line_Entry::OAM_line_Entry() :
m_oam_entry(OAMEntry())
{}

OAM_line_Entry::OAM_line_Entry(OAMEntry entry) :
m_oam_entry(entry),
m_marked(false),
m_done(false)
{}

OAM_line_Entries::OAM_line_Entries() :
m_size(0),
m_oam_entries{}
{
}

void OAM_line_Entries::clear()
{
	m_size = 0;
}

void OAM_line_Entries::add_entry(OAMEntry entry)
{
	if(m_size < m_oam_entries.size())
	{
		m_oam_entries[m_size] = OAM_line_Entry(entry);
		m_oam_entries[m_size].m_done = false;
		m_oam_entries[m_size].m_marked = false;
		m_size++;
	}
}

u8 OAM_line_Entries::size()
{
	return m_size;
}

OAM_line_Entry& OAM_line_Entries::get_entry(u8 index)
{
	return m_oam_entries[index];
}
