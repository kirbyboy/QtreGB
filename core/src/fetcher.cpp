#include <sstream>
#include <cstdio>
#include <cstdlib>

#include "fetcher.h"
#include "QtreGB.h"
#include "logger.h"
#include "utils.h"

using namespace qtreGB;

// TEMP DEBUG (remove before finishing): per-line fetcher trace gated by env var.
static bool fetch_trace_line(u8 ly)
{
	const char* env = std::getenv("FETCH_TRACE");
	return env != nullptr && ly == static_cast<u8>(std::atoi(env));
}

Fetcher::Fetcher()
{}

void Fetcher::reset()
{
	m_warming = true;
	m_current_x = 0;
	m_discarded_pixels = 0;
	m_phase = 1;
	m_mode = FetchMode::FS_BGD;
	m_state_bg = FetchEntry::FS_TILE_T1;
	m_current_state = FetchEntryObj::FS_OBJ_TILE;
	m_object_array.fill(FIFOElement(0, false, true));
	m_bg_need_recalculate = true;
	m_bg_tile_to_push = 0;
	m_bg_mode = FetchModeBG::FS_BG_BGD;
	m_bgw_fifo.clear();

	m_wx_triggered = false;
	m_window_is_being_fetched = false;
	m_insert_bg_pixel = false;
	m_win_fine_offset = 0;

	is_window = (m_regs->get_ly() >= m_regs->get_win_y());

	if(m_debug_enabled)
	{
		debug_trace("reset ly=" + std::to_string(static_cast<int>(m_regs->get_ly())) +
			" win_y=" + std::to_string(static_cast<int>(m_regs->get_win_y())) +
			" is_window=" + std::to_string(is_window) +
			" lcdc=" + to_hex(m_regs->get_lcdc()));
	}
}

void Fetcher::set_state(FetchEntryObj state)
{
	m_current_state = state;
}

void Fetcher::set_mode(FetchMode mode)
{
	m_mode = mode;
}

void Fetcher::tick()
{
	// The background/window fetch advances one state per tick (six T-states per
	// tile: T1/T2 for the tile index and for each of the two bitplanes). The
	// sprite fetch still advances on every other tick, and so does the object
	// check; the window check runs every tick so a switch lands on the exact X.
	const bool every_other = (m_phase == 0);
	m_phase ^= 1;

	if(m_warming)
	{
		// In the 12 dots, always is background mode
		m_mode = FetchMode::FS_BGD;
	}
	else
	{
		if(every_other)
		{
			check_obj(m_current_x);
		}
		check_mode();
	}

	sm_bg(every_other);
}

void Fetcher::check_obj(u8 x)
{
	if(m_mode == FetchMode::FS_OBJ)
		return;
	for(u8 i= 0; i < m_oam_line_entries.size(); i++)
	{
		OAM_line_Entry& entry = m_oam_line_entries.get_entry(i);
		if(entry.m_oam_entry.m_x <= x + 16 && !entry.m_done)
		{
			entry.m_marked = true;
			if(m_mode == FetchMode::FS_BGD)
			{
				m_mode = FetchMode::FS_OBJ;
				m_current_state = FetchEntryObj::FS_OBJ_TILE;
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

void Fetcher::set_vram(IVram* vram)
{
	m_vram = vram;
}

void Fetcher::set_regs(IFetcherRegs* regs)
{
	m_regs = regs;
}

FIFO& Fetcher::bgw_fifo()
{
	return m_bgw_fifo;
}

FIFO& Fetcher::spr_fifo()
{
	return m_spr_fifo;
}

bool Fetcher::pop_pixel(u8& out_pixel)
{
	if(m_bgw_fifo.empty())
	{
		return false;
	}

	if(m_discarded_pixels < (m_regs->get_scroll_x() % 8))
	{
		m_bgw_fifo.pop();
		if(!m_spr_fifo.empty())
		{
			m_spr_fifo.pop();
		}
		m_discarded_pixels++;
		return false;
	}

	FIFOElement bgw_element;
	if(m_insert_bg_pixel)
	{
		// Window reactivation zero pixel: emit a raw color-0 item and leave the FIFO
		// untouched, so the rest of the line is shifted right by one pixel.
		m_insert_bg_pixel = false;
		bgw_element = FIFOElement(0, false, false);
		if(fetch_trace_line(m_regs->get_ly()))
		{
			std::fprintf(stderr, "[fet] INSERT zero pixel cx=%d fifo=%zu wx=%d\n",
				(int)m_current_x, m_bgw_fifo.size(), (int)m_regs->get_win_x());
		}
	}
	else
	{
		bgw_element = m_bgw_fifo.pop();
	}
	// LCDC bit 0 = 0 blanks the whole BG/window layer: it outputs BGP color 0 while
	// objects are still drawn on top (dmg-acid2 "hair" rows 8-15).
	const u8 bgw_color_index = m_regs->get_lcdc_bgw_enable() ? bgw_element.get_color_index() : 0;
	u8 pixel_data = (m_regs->get_bg_palette() >> (bgw_color_index * 2)) & 0x03;

	// The object array holds one entry per screen pixel. X reaches XRES on the last
	// push of a line (that pixel is off-screen and not stored), so keep the lookup in
	// range instead of reading the array out of bounds.
	if(m_regs->get_lcdc_obj_enable() && static_cast<std::size_t>(m_current_x) < m_object_array.size())
	{
		// Sprites are resolved from the per-line object array, which is indexed by
		// screen X and already applies OAM priority and the sprite palette.
		FIFOElement spr_element = m_object_array[m_current_x];
		if(m_regs->get_ly() == 24 && m_current_x < 4)
		{
			std::fprintf(stderr, "QTRDBG pop ly=24 x=%d obj_en=1 spr_ci=%u bgw_ci=%u\n",
				(int)m_current_x, (unsigned)spr_element.get_color_index(),
				(unsigned)bgw_color_index);
		}
		if(spr_element.get_color_index() != 0)
		{
			if(!spr_element.get_priority() || bgw_color_index == 0)
			{
				u8 palette = spr_element.get_palette() ? m_regs->get_obj_palette_1() : m_regs->get_obj_palette_0();
				pixel_data = (palette >> (spr_element.get_color_index() * 2)) & 0x03;
			}
		}
	}

	out_pixel = pixel_data;
	return true;
}

void Fetcher::set_current_x(u8 x)
{
	m_current_x = x;
}

void Fetcher::set_warming(bool warming)
{
	m_warming = warming;
}

void Fetcher::debug_begin(u8 line)
{
	m_debug_enabled = true;
	m_debug_line = line;
	m_debug_log.clear();
}

void Fetcher::debug_end()
{
	m_debug_enabled = false;
}

void Fetcher::debug_set_line(u8 line)
{
	m_debug_line = line;
}

const std::vector<std::string>& Fetcher::debug_log() const
{
	return m_debug_log;
}

void Fetcher::debug_trace(const std::string& entry)
{
	if(m_debug_enabled && m_regs->get_ly() == m_debug_line)
	{
		if(m_debug_log.size() < 4096)
		{
			m_debug_log.push_back(entry);
		}
	}
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
	// The object array is rebuilt for every line: stale sprite pixels from the
	// previous line must not leak into a line where the sprite is transparent.
	m_object_array.fill(FIFOElement(0, false, true));

	if(m_regs->get_ly() == 24)
	{
		std::fprintf(stderr, "QTRDBG fill ly=24 n=%u obj_en=%d h=%u\n",
			(unsigned)m_oam_line_entries.size(), (int)m_regs->get_lcdc_obj_enable(),
			(unsigned)m_regs->get_lcdc_obj_height());
	}

	for(u8 i = 0; i < m_oam_line_entries.size(); i++)
	{
		OAMEntry entry = m_oam_line_entries.get_entry(i).m_oam_entry;
		u8 tile = entry.m_tile;
		u8 sprite_height = m_regs->get_lcdc_obj_height();
		u8 current_y = m_regs->get_ly();
		u8 sprite_data_y = 0;
		if(current_y == 24)
		{
			std::fprintf(stderr, "QTRDBG   spr y=%u x=%u tile=%02x flags=%02x\n",
				(unsigned)entry.m_y, (unsigned)entry.m_x, (unsigned)tile,
				(unsigned)entry.m_flags);
		}
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

		data_lo = m_vram->read(0x8000 + (tile * 16) + sprite_data_y + 0);
		data_hi = m_vram->read(0x8000 + (tile * 16) + sprite_data_y + 1);
		if(current_y == 24)
		{
			std::fprintf(stderr, "QTRDBG   tile=%02x sdy=%u lo=%02x hi=%02x\n",
				(unsigned)tile, (unsigned)sprite_data_y, (unsigned)data_lo, (unsigned)data_hi);
		}

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
// FS_TILE_T1: compute the tile-map address and sample the map-select bits.
void Fetcher::fill_map_background_t1()
{
	u8 tile_map_y;

	if(m_bg_mode == FetchModeBG::FS_BG_BGD)
	{
		tile_map_y = (m_regs->get_ly() + m_regs->get_scroll_y()) / 8;
		if(m_bg_need_recalculate)
		{
			m_bg_tile_to_push = ((m_regs->get_scroll_x() + m_current_x) / 8) & 0x1F;
			m_bg_need_recalculate = false;
		}
		m_bg_tile_to_push = m_bg_tile_to_push & 0x1F;
		LOG("fill_map_background tile", to_hex(m_bg_tile_to_push));

		m_bg_map_addr = m_regs->get_lcdc_bg_map_area() + m_bg_tile_to_push + (tile_map_y * 32);
	}
	else
	{
		tile_map_y = m_regs->get_ly() - m_regs->get_win_y();
		m_bg_map_addr = m_regs->get_lcdc_win_map_area() + (m_bg_tile_to_push & 0x1F) + ((m_window_line / 8) * 32);
	}

	// LCDC bit 4 is sampled at the tile step to fix how the tile index read in T2
	// is interpreted (signed 0x8800 tileset gets the +128 offset baked in). The
	// data steps re-read it live for the base address, so a mid-fetch write can
	// still mix bitplanes from two different tilesets.
	m_bg_signed_tiles = (m_regs->get_lcdc_bgw_data_area() == 0x8800);

	if(m_debug_enabled)
	{
		debug_trace("fill_map_t1 mode=" + std::to_string(static_cast<int>(m_bg_mode)) +
			" ly=" + std::to_string(static_cast<int>(m_regs->get_ly())) +
			" sys=" + std::to_string(static_cast<int>(m_regs->get_scroll_y())) +
			" tile_map_y=" + std::to_string(static_cast<int>(tile_map_y)) +
			" wx=" + std::to_string(static_cast<int>(m_regs->get_win_x())) +
			" wy=" + std::to_string(static_cast<int>(m_regs->get_win_y())) +
			" wl=" + std::to_string(static_cast<int>(m_window_line)) +
			" tile_to_push=" + std::to_string(static_cast<int>(m_bg_tile_to_push)) +
			" addr=" + to_hex(m_bg_map_addr));
	}
}

// FS_TILE_T2: perform the tile-index read from the address computed in T1.
void Fetcher::fill_map_background_t2()
{
	m_tile_map_bg = m_vram->read(m_bg_map_addr);

	if(m_bg_signed_tiles)
	{
		m_tile_map_bg += 128;
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
// FS_DATA*_T1: compute the tile-data address and sample the tileset bits. Called
// independently for the low and high bitplanes so a mid-fetch write to SCY or
// LCDC bit 4 can mix bitplanes from different rows or tilesets.
void Fetcher::load_data_background_t1(u8 offset)
{
	if(offset > 1)
	{
		return;
	}

	u8 tile_data_y;

	if(m_bg_mode == FetchModeBG::FS_BG_BGD)
	{
		tile_data_y = (m_regs->get_ly() + m_regs->get_scroll_y()) % 8 * 2;
	}
	else
	{
		tile_data_y = m_window_line % 8;
		tile_data_y = tile_data_y * 2;
	}

	const u16 data_area = m_regs->get_lcdc_bgw_data_area();

	m_bg_data_addr = data_area + (m_tile_map_bg * 16) + tile_data_y;

	if(m_debug_enabled)
	{
		debug_trace("load_data_t1 mode=" + std::to_string(static_cast<int>(m_bg_mode)) +
			" offset=" + std::to_string(static_cast<int>(offset)) +
			" addr=" + to_hex(m_bg_data_addr) +
			" tile_data_y=" + std::to_string(static_cast<int>(tile_data_y)));
	}
}

// FS_DATA*_T2: perform the read from the address computed in T1.
void Fetcher::load_data_background_t2(u8 offset)
{
	if(offset > 1)
	{
		return;
	}

	const u8 data = m_vram->read(m_bg_data_addr + offset);

	if(offset == 0)
	{
		m_data_lo_bg = data;
	}
	else
	{
		m_data_hi_bg = data;
	}

	if(m_debug_enabled)
	{
		debug_trace("load_data_t2 offset=" + std::to_string(static_cast<int>(offset)) +
			" addr=" + to_hex(m_bg_data_addr + offset) +
			" data=" + to_hex(data));
	}
}


void Fetcher::load_data_sprite(u8 offset)
{
	if(offset > 1)
	{
		return;
	}
	u8 sprite_height = m_regs->get_lcdc_obj_height();
	u8 current_y = m_regs->get_ly();
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

	u8 data = m_vram->read(0x8000 + (m_tile_map_spr * 16) + sprite_data_y + offset);

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

	if(m_debug_enabled)
	{
		debug_trace("push_bg fifo_empty=1 lo=" + to_hex(m_data_lo_bg) +
			" hi=" + to_hex(m_data_hi_bg) +
			" tile_to_push=" + std::to_string(static_cast<int>(m_bg_tile_to_push)));
	}

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
		// Everything that can invalidate the window on this line, sampled live.
		const bool win_visible =
			m_regs->get_lcdc_win_enable() &&
			m_regs->get_win_x() <= 166 &&
			m_regs->get_win_y() < SCREEN_HEIGHT &&
			is_window;

		// The window's leftmost pixel lands on screen at x = WX - 7; for WX < 7 it is
		// clamped to the left edge (the window is not shifted, so its first columns are
		// simply shown at x = 0).
		const u8 trigger_x = (m_regs->get_win_x() >= 7) ? (m_regs->get_win_x() - 7) : 0;

		if(fetch_trace_line(m_regs->get_ly()))
		{
			std::fprintf(stderr, "[fet] cm cx=%d st=%d bg=%d fifo=%zu win_vis=%d trig=%d wx=%d wy=%d winspr=%d done=%d ins=%d\n",
				(int)m_current_x, (int)m_state_bg, (int)m_bg_mode, m_bgw_fifo.size(),
				(int)win_visible, (int)trigger_x, (int)m_regs->get_win_x(), (int)m_regs->get_win_y(),
				(int)m_window_is_being_fetched, (int)m_wx_triggered, (int)m_insert_bg_pixel);
		}

		if(m_bg_mode == FetchModeBG::FS_BG_BGD)
		{
			// Activation: exact equality at the crossing pixel (not a >=, which cannot
			// tell a first activation apart from a reactivation). Evaluated only once
			// the warm-up is over: the window's left edge is at x = WX - 7, so with
			// WX < 7 it sits off-screen and the FIFO clear must stall the first
			// visible pixel instead of being absorbed by the warm-up fetch.
			if(!m_warming && win_visible && m_current_x == trigger_x)
			{
				m_bgw_fifo.clear();
				m_bg_mode = FetchModeBG::FS_BG_WIN;
				m_state_bg = FetchEntry::FS_TILE_T1;
				m_bg_need_recalculate = true;
				m_bg_tile_to_push = 0;
				m_wx_triggered = true;
				m_window_is_being_fetched = true;
				// Anchoring the window at x = WX - 7 leaves its first columns off-screen
				// when WX < 7; they are dropped from the first tile push.
				m_win_fine_offset = (m_regs->get_win_x() < 7) ? (7 - m_regs->get_win_x()) : 0;

				if(m_debug_enabled)
				{
					debug_trace("check_mode -> WIN cx=" + std::to_string(static_cast<int>(m_current_x)) +
						" win_x=" + std::to_string(static_cast<int>(m_regs->get_win_x())));
				}
			}
		}
		else if(m_bg_mode == FetchModeBG::FS_BG_WIN)
		{
			if(!win_visible)
			{
				// The window can only be disabled mid-line by clearing LCDC.5 (or by
				// WY ceasing to trigger); WX changing does not hide it.
				m_bgw_fifo.clear();
				m_bg_mode = FetchModeBG::FS_BG_BGD;
				m_state_bg = FetchEntry::FS_TILE_T1;
				m_bg_need_recalculate = true;

				if(m_debug_enabled)
				{
					debug_trace("check_mode -> BGD cx=" + std::to_string(static_cast<int>(m_current_x)) +
						" win_x=" + std::to_string(static_cast<int>(m_regs->get_win_x())));
				}
			}
			else if(std::getenv("NO_REACT_INSERT") == nullptr &&
				m_wx_triggered &&
				!m_window_is_being_fetched &&
				!m_insert_bg_pixel &&
				m_current_x + 7 == m_regs->get_win_x() &&
				m_state_bg == FetchEntry::FS_PUSH &&
				m_bgw_fifo.empty())
			{
				// Window reactivation zero pixel (ppu-fetcher.md §7). The trigger is
				// exact equality at x = WX - 7 while the window is already active, and
				// it only lands on lines where that x coincides with the window tile
				// nametable read. In this fetcher the visible phase is FS_PUSH (the
				// fetch has completed and is waiting to push the row into the FIFO),
				// which reproduces the hardware's LY ≡ 4 (mod 8) pattern. A raw color-0
				// pixel is prepended to the stream without clearing the FIFO, shifting
				// the rest of the line by one pixel.
				m_insert_bg_pixel = true;

				if(std::getenv("REACT_TRACE") != nullptr)
				{
					std::fprintf(stderr, "[react] ly=%d cx=%d wx=%d st=%d fifo=%zu\n",
						(int)m_regs->get_ly(), (int)m_current_x, (int)m_regs->get_win_x(),
						(int)m_state_bg, m_bgw_fifo.size());
				}

				if(m_debug_enabled)
				{
					debug_trace("check_mode -> REACT cx=" + std::to_string(static_cast<int>(m_current_x)) +
						" win_x=" + std::to_string(static_cast<int>(m_regs->get_win_x())));
				}
			}
		}
	}
}

void Fetcher::sm_bg(bool every_other)
{
	if(m_debug_enabled)
	{
		debug_trace("sm_bg state=" + std::to_string(static_cast<int>(m_state_bg)) +
			" mode=" + std::to_string(static_cast<int>(m_bg_mode)) +
			" warming=" + std::to_string(m_warming) +
			" cx=" + std::to_string(static_cast<int>(m_current_x)) +
			" fifo=" + std::to_string(m_bgw_fifo.size()));
	}

	if(m_mode == FetchMode::FS_OBJ)
	{
		// Objects still advance on every other tick (their four-step machine is
		// rewritten into the pause model in a later cut).
		if(!every_other)
		{
			return;
		}

		switch(m_current_state)
		{
			case FetchEntryObj::FS_OBJ_TILE:
			{
				fill_map_sprite();
				m_current_state = FetchEntryObj::FS_OBJ_DATA0;
			}
			break;

			case FetchEntryObj::FS_OBJ_DATA0:
			{
				load_data_sprite(0);
				m_current_state = FetchEntryObj::FS_OBJ_DATA1;
			}
			break;

			case FetchEntryObj::FS_OBJ_DATA1:
			{
				load_data_sprite(1);
				m_current_state = FetchEntryObj::FS_OBJ_PUSH;
			}
			break;

			case FetchEntryObj::FS_OBJ_PUSH:
			{
				bool result = push_data_sprite();
				if(result)
				{
					m_current_state = FetchEntryObj::FS_OBJ_TILE;
					m_state_bg = FetchEntry::FS_TILE_T1;
					m_mode = FetchMode::FS_BGD;
				}
			}
			break;
		}
		return;
	}

	switch(m_state_bg)
	{
		case FetchEntry::FS_TILE_T1:
		{
			fill_map_background_t1();
			m_state_bg = FetchEntry::FS_TILE_T2;
		}
		break;

		case FetchEntry::FS_TILE_T2:
		{
			fill_map_background_t2();
			m_state_bg = FetchEntry::FS_DATA0_T1;
		}
		break;

		case FetchEntry::FS_DATA0_T1:
		{
			load_data_background_t1(0);
			m_state_bg = FetchEntry::FS_DATA0_T2;
		}
		break;

		case FetchEntry::FS_DATA0_T2:
		{
			load_data_background_t2(0);
			m_state_bg = FetchEntry::FS_DATA1_T1;
		}
		break;

		case FetchEntry::FS_DATA1_T1:
		{
			load_data_background_t1(1);
			m_state_bg = FetchEntry::FS_DATA1_T2;
		}
		break;

		case FetchEntry::FS_DATA1_T2:
		{
			load_data_background_t2(1);
			m_state_bg = FetchEntry::FS_PUSH;
		}
		break;

		case FetchEntry::FS_PUSH:
		{
			bool result = push_data_background();

			if(result)
			{
				// The window's first tile has been pushed: subsequent nametable reads
				// can trigger the reactivation quirk.
				m_window_is_being_fetched = false;

				if(m_bg_mode == FetchModeBG::FS_BG_WIN && m_win_fine_offset > 0)
				{
					// Drop the window columns that fall off the left edge (WX < 7).
					for(u8 i = 0; i < m_win_fine_offset; i++)
					{
						m_bgw_fifo.pop();
					}
					m_win_fine_offset = 0;
				}

				m_state_bg = FetchEntry::FS_GAP;
			}
		}
		break;

		case FetchEntry::FS_GAP:
		{
			m_state_bg = FetchEntry::FS_TILE_T1;
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
