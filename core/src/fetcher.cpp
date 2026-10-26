#include <sstream>
#include <cstdlib>

#include "fetcher.h"
#include "QtreGB.h"
#include "logger.h"
#include "utils.h"

using namespace qtreGB;

Fetcher::Fetcher()
{}

void Fetcher::reset()
{
	m_warming = true;
	m_warmup_pos = -16;	// SameBoy: 17 warm-up dots (positions -16..0)
	m_window_check_armed = false;
	m_current_x = 0;
	m_discarded_pixels = 0;
	m_mode = FetchMode::FS_BGD;
	m_state_bg = FetchEntry::FS_TILE_T1;
	m_current_state = FetchEntryObj::FS_OBJ_TILE;
	m_obj_in_fixed = false;
	m_obj_fixed_left = 0;
	m_object_array.fill(FIFOElement(0, false, true));
	m_bg_need_recalculate = true;
	m_bg_tile_to_push = 0;
	m_bg_mode = FetchModeBG::FS_BG_BGD;
	m_bgw_fifo.clear();

	m_wx_triggered = false;
	m_window_is_being_fetched = false;
	m_insert_bg_pixel = false;
	m_win_fine_offset = 0;
	m_win_drop_first = false;

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
	if(m_warming)
	{
		// During the warm-up (discard lead, positions -15..-1) object fetches
		// must follow SameBoy's x_for_object_match, which returns 0 for
		// positions -16..-9 (only OAM.X == 0 due) and position+8 for -8..-1.
		// We track the warm-up position so each sprite becomes due on its own
		// dot instead of all at once on the first dot (the old check_obj(0xFF)
		// made due = 7 for every warm-up tick).
		if(m_current_x == 0)
		{
			// positions -15..-9 -> due 0 (x = 248); -8..-1 -> due 0..7.
			u8 x = (m_warmup_pos < -8) ? 248 : static_cast<u8>(m_warmup_pos);
			check_obj(x);
			m_warmup_pos++;
			// The window check also runs during the warm-up (SameBoy's mode-3
			// loop evaluates it from the first dot with negative positions):
			// a WX == 0 edge activates here instead of at the output anchor
			// (handoff §23/A).
			check_mode();
		}
		else
		{
			// After the discard lead, pixels are popping: fetch sprites as
			// they become due (position == OAM.X - 8).
			check_obj(m_current_x);
		}
	}
	else
	{
		// The object check runs on every tick (SameBoy's mode-3 loop evaluates
		// x_for_object_match once per dot): checking only every other tick lets
		// the pop position advance past a sprite's left edge before its fetch
		// is triggered, and its leftmost column is then popped before the row
		// is stamped (the §14 invariant violation seen in dmg-acid2).
		check_obj(m_current_x);
		// Keep advancing the discard-lead position after the warm-up so it
		// stays == dot - 97 through the pre-pop dots and the pop phase: the
		// window edge check for WX <= 7 uses it to fire on the exact dot
		// (handoff §26).
		m_warmup_pos++;
		// The per-tick window check (mid-line WX switches) only starts after
		// the push-time anchor has run, so the first window evaluation is the
		// anchor's own and the first window fetch keeps its 1-dot-late phase
		// (m3_window_timing_wx_0).
		if(m_window_check_armed)
		{
			check_mode();
		}
	}

	sm_bg();
}

void Fetcher::check_obj(u8 x)
{
	if(m_mode == FetchMode::FS_OBJ)
		return;
	// The due threshold is x_for_object_match = position + 8 in u8 arithmetic
	// (SameBoy), so the warm-up call with x = 0xFF (position -1, the last
	// discard pixel) yields 7: only sprites with OAM.X <= 7 are fetched during
	// the warm-up lead, and the OAM.X == 8 sprite becomes due exactly at the
	// first pop, where its fetch stalls the output.
	// SameBoy's x_for_object_match: returns 0 for positions -16..-9 (only objx=0
	// due), and position+8 for -8..-1. During normal operation (x = m_current_x,
	// 0..159), due = x + 8. During warm-up (x cast from signed position),
	// positions -16..-9 yield due=0 and -8..-1 yield due=0..7.
	u8 due;
	if(x >= 248)
	{
		// Warm-up positions -8..-1: due = 0..7
		due = static_cast<u8>(x + 8);
	}
	else if(x >= 240)
	{
		// Warm-up positions -16..-9: due = 0 (only objx=0)
		due = 0;
	}
	else
	{
		// Normal operation: due = x + 8
		due = static_cast<u8>(x + 8);
	}
	for(u8 i= 0; i < m_oam_line_entries.size(); i++)
	{
		OAM_line_Entry& entry = m_oam_line_entries.get_entry(i);
		if(entry.m_done)
		{
			continue;
		}
		// SameBoy's x_for_object_match model: an object is fetched when the pop
		// position reaches its left edge (position == OAM.X - 8, i.e. the entry
		// becomes due at m_current_x + 8), and objects whose left edge is
		// already behind the pop position are discarded (missed). The discard
		// also holds while OBJ display is disabled: the PPU still drops them as
		// the line advances, they are never fetched later.
		if(entry.m_oam_entry.m_x < due)
		{
			// Already behind the pop position: missed (SameBoy discards these).
			entry.m_done = true;
		}
		else if(entry.m_oam_entry.m_x == due)
		{
			if(getenv("QTR_OBJDBG"))
			{
				fprintf(stderr, "[OBJDBG] gtick=%llu check x=%u entry=%u oamx=%u objen=%d mode=%d marked=%d done=%d\n",
				        qtreGB::qtregb_global_tick, x, i, entry.m_oam_entry.m_x,
				        m_regs->get_lcdc_obj_enable() ? 1 : 0, static_cast<int>(m_mode),
				        entry.m_marked ? 1 : 0, entry.m_done ? 1 : 0);
			}
			if(m_regs->get_lcdc_obj_enable())
			{
				entry.m_marked = true;
				if(m_mode == FetchMode::FS_BGD)
				{
					m_mode = FetchMode::FS_OBJ;
					m_current_state = FetchEntryObj::FS_OBJ_TILE;
					m_obj_in_fixed = false;
					m_obj_fixed_left = 0;
					m_obj_start_tick = qtreGB::qtregb_global_tick;
					// The sprite that enters the fetch is selected here: the fetch
					// reads its data on its own dots and the row is stamped before
					// its columns are popped (§14 invariant).
					fill_map_sprite();

					if(getenv("QTR_OBJ_TRACE"))
					{
						fprintf(stderr, "[OBJTRACE] start ly=%u oamx=%u bgstate=%u fifo=%u tick=%llu\n",
							m_regs->get_ly(),
						        m_oam_line_entries.get_entry(m_current_oam_entry).m_oam_entry.m_x,
						        static_cast<unsigned>(m_state_bg),
						        m_bgw_fifo.size(), qtreGB::qtregb_global_tick);
					}

					return;
				}
			}
			else
			{
				// OBJ display is off: the entry is dropped, not fetched.
				entry.m_done = true;
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
	// While an object fetch is in progress the pixel output is stalled for its
	// whole cost (SameBoy's mode-3 loop runs the object fetch inside one
	// iteration with no pop): this is also what keeps the §14 invariant — the
	// sprite's columns cannot pop before its row is stamped into the object
	// array at the fetch's push step.
	if(m_mode == FetchMode::FS_OBJ)
	{
		return false;
	}

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

	if(m_win_fine_offset > 0)
	{
		// TEMP debug (QTR_FINDISC=<ly>): the per-dot window discard trace.
		{
			static const char* dbg = getenv("QTR_FINDISC");
			if(dbg && m_regs->get_ly() == static_cast<u8>(atoi(dbg)))
			{
				fprintf(stderr, "[FINDISC] gtick=%llu curx=%u fine=%u fifo=%u\n",
				        qtreGB::qtregb_global_tick, m_current_x, m_win_fine_offset,
				        static_cast<unsigned>(m_bgw_fifo.size()));
			}
		}
		// The window's leftmost columns can fall off the left edge (WX < 7). They are
		// shifted out of the output one per dot before x = 0, so they cost real dots
		// (dropping them all at once at push time would let the visible window start
		// too early).
		m_bgw_fifo.pop();
		if(!m_spr_fifo.empty())
		{
			m_spr_fifo.pop();
		}
		m_win_fine_offset--;
		return false;
	}

	FIFOElement bgw_element;
	if(m_insert_bg_pixel)
	{
		// Window reactivation zero pixel: emit a raw color-0 item and leave the FIFO
		// untouched, so the rest of the line is shifted right by one pixel.
		m_insert_bg_pixel = false;
		bgw_element = FIFOElement(0, false, false);
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
		if(spr_element.get_color_index() != 0)
		{
			if(!spr_element.get_priority() || bgw_color_index == 0)
			{
				u8 palette = spr_element.get_palette() ? m_regs->get_obj_palette_1() : m_regs->get_obj_palette_0();
				pixel_data = (palette >> (spr_element.get_color_index() * 2)) & 0x03;
				// Cached env probe: pop_pixel is a per-dot hot path.
				static const bool obp_trace = (getenv("QTR_OBP_TRACE") != nullptr);
				if(obp_trace && m_regs->get_ly() == static_cast<u8>(atoi(getenv("QTR_OBP_TRACE"))))
				{
					fprintf(stderr, "[OBPTR] gtick=%llu ly=%u x=%u cidx=%u pal=%u obp=%02X out=%u\n",
					        qtreGB::qtregb_global_tick, m_regs->get_ly(), m_current_x, spr_element.get_color_index(),
					        spr_element.get_palette(), palette, pixel_data);
				}
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

	for(u8 i = 0; i < m_oam_line_entries.size(); i++)
	{
		OAMEntry entry = m_oam_line_entries.get_entry(i).m_oam_entry;
		u8 tile = entry.m_tile;
		u8 sprite_height = m_regs->get_lcdc_obj_height();
		u8 current_y = m_regs->get_ly();
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

		data_lo = m_vram->read(0x8000 + (tile * 16) + sprite_data_y + 0);
		data_hi = m_vram->read(0x8000 + (tile * 16) + sprite_data_y + 1);

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

OAM_line_Entries &Fetcher::get_oam_line_entries()
{
	return m_oam_line_entries;
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
	// Window-disabled insertion glitch (ppu-fetcher.md §8.2): when the window
	// is disabled but WY has triggered, and WX is hit at the right cycle, the
	// PUSH state inserts a single zero pixel instead of pushing a tile row.
	if(is_window && !m_regs->get_lcdc_win_enable() && !m_disable_window_pixel_insertion_glitch)
	{
		u8 logical_position = m_current_x + 7;
		if(logical_position > 167) logical_position = 0;
		if(m_regs->get_win_x() == logical_position)
		{
			m_bgw_fifo.insert_glitch_zero();
			return true;
		}
	}

	if(!m_bgw_fifo.empty())
	{
		return false;
	}

	// WX == 0, SCX&7 == 0 activation (m_win_drop_first, set at the warm-up
	// trigger in check_mode): the first window column never occupies a pop
	// slot, so push columns 1..7 and leave one fewer per-dot discard (the 7
	// off-screen columns then fit the 6 pop slots before the visible window
	// starts, handoff §23/A).
	const u8 first_col = m_win_drop_first ? 1 : 0;
	if(m_win_drop_first)
	{
		m_win_drop_first = false;
		m_win_fine_offset--;
	}

	for(u8 i = first_col; i < 8; i++)
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

	// The fetched row is stamped into the per-line object array at the fetch's
	// push step (this replaces the former dot-81 full-line precompute: each
	// sprite's row, palette and priority are resolved at its own fetch dot, so
	// mid-line LCDC.2 / OAM / OBJ-enable writes affect the sprites fetched
	// after them). Same overlay rule as the precompute: a sprite only fills
	// transparent targets (the DMG X priority — the leftmost sprite wins).
	// The pops are stalled while the fetch is in progress (see pop_pixel), so
	// the sprite's columns are stamped before they are popped (§14 invariant).
	if(getenv("QTR_OBJDBG"))
	{
		fprintf(stderr, "[OBJDBG] gtick=%llu stamp entry=%u oamx=%u curx=%u colors=[",
		        qtreGB::qtregb_global_tick, m_current_oam_entry,
		        m_oam_line_entries.get_entry(m_current_oam_entry).m_oam_entry.m_x, m_current_x);
		for(u8 i = 0; i < 8; i++)
		{
			u8 bit = 7 - i;
			if((m_oam_line_entries.get_entry(m_current_oam_entry).m_oam_entry.m_flags & (1 << 5)) != 0)
			{
				bit = 7 - bit;
			}
			const u8 ci = (!!((m_data_lo_spr) & (1 << bit))) | ((!!(((m_data_hi_spr)) & (1 << bit))) << 1);
			fprintf(stderr, "%u", ci);
		}
		fprintf(stderr, "] data=%02X,%02X\n", m_data_lo_spr, m_data_hi_spr);
	}
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

		const u8 xpos = m_oam_line_entries.get_entry(m_current_oam_entry).m_oam_entry.m_x + i;
		if(xpos >= 8 && xpos < 168)
		{
			FIFOElement& original_element = m_object_array[xpos - 8];
			if(original_element.get_color_index() == 0)
			{
				original_element = element;
			}
		}

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
	// The first evaluation is the push-time window anchor (see PPU's
	// k_anchor_dot); it arms the per-tick checks below.
	m_window_check_armed = true;
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

		if(m_bg_mode == FetchModeBG::FS_BG_BGD)
		{
			// WX == 0 warm-up activation (SameBoy display.c): hardware activates
			// the window during the discard lead (position -7), not at the output
			// anchor. The fetch phase depends on the fine scroll: with SCX&7 == 0
			// the fetch is one dot shorter (T1 pre-done) and the first window
			// column is dropped at the push, so the 7 off-screen columns fit 6
			// pop slots; with SCX&7 != 0 the restart is plain and the SCX discard
			// runs first. Both phases produce the measured law
			// p0 = 103 + (k == 0 ? 0 : k + 1) with k = SCX&7 (handoff §23/A).
			// Window-edge activation with an off-screen edge (WX <= 7): the
			// edge position WX - 7 is reached at dot 90 + WX, and m_warmup_pos
			// tracks dot - 97 from the mode-3 start through the pop phase, so
			// the equality below fires on the exact dot for every WX 0..7
			// (handoff §26). Line 0 runs its whole pipeline 4 dots early
			// (k_pixel0_dot_line0, handoff §5), so its edges are 4 earlier
			// too (WX == 0: position -11 instead of -7).
			// WX == 0 keeps the SCX&7-conditioned fetch phase (§24.1: k == 0 ->
			// one-dot-earlier fetch + first column dropped at the push so the
			// 7 off-screen columns fit 6 pop slots; k != 0 -> plain restart,
			// the SCX discard runs first). WX 1..7 always use the plain 6-dot
			// restart: the first push lands at dot 96+WX and the per-dot
			// discard of 7-WX columns puts the first visible pixel at dot 103
			// for every WX.
			const int win_edge = static_cast<int>(m_regs->get_win_x()) - 7
				- ((m_regs->get_ly() == 0) ? 4 : 0);
			const bool edge_offscreen = (m_regs->get_win_x() <= 7);
			// m_warmup_pos counts from the first fetcher tick (mode-3 dot 81),
			// so position == dot - 96: the edge dot 90 + WX (96 + WX - 6) is
			// reached when the tracked position equals win_edge + 1.
			const bool pos_trigger =
				edge_offscreen && win_visible &&
				m_warmup_pos == win_edge + 1;
			// Activation: exact equality at the crossing pixel (not a >=, which cannot
			// tell a first activation apart from a reactivation). The late
			// equality below is now the FALLBACK for mid-line WX writes that
			// land after their edge position has already passed (wx_4/wx_5
			// write WX mid-line; for WX >= 6 the edge dot lies after the anchor,
			// so the fallback must stay silent until the edge is truly past).
			const bool late_trigger =
				!m_warming && win_visible && m_current_x == trigger_x &&
				(!edge_offscreen || m_warmup_pos > win_edge);
			if(pos_trigger || late_trigger)
			{
				if(getenv("QTR_WX_TRIG"))
				{
					fprintf(stderr, "[WXTRIG] gtick=%llu curx=%u wx=%u%s\n",
					        qtreGB::qtregb_global_tick, m_current_x, m_regs->get_win_x(),
					        pos_trigger ? " pos" : " late");
				}
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

				if(pos_trigger)
				{
					if(m_regs->get_win_x() == 0 && (m_regs->get_scroll_x() % 8) == 0)
					{
						// One-dot-earlier fetch + first column never occupies a pop
						// slot: the 7 off-screen columns fit the 6 pop slots before
						// the visible window starts at dot 103.
						m_win_drop_first = true;
						fill_map_background_t1();
						m_state_bg = FetchEntry::FS_TILE_T2;
					}
					// WX 1..7 and WX == 0 with SCX&7 != 0: plain restart (6 dots);
					// for WX == 0 with SCX&7 the SCX discard runs first and
					// delays the stream by k + 1, matching the hardware law.
				}
				else if(m_win_fine_offset > 0 &&
					!(m_regs->get_win_x() == 0 && (m_regs->get_scroll_x() % 8) != 0))
				{
					fill_map_background_t1();
					m_state_bg = FetchEntry::FS_TILE_T2;
				}

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
				m_window_is_being_fetched = false;

				if(m_debug_enabled)
				{
					debug_trace("check_mode -> BGD cx=" + std::to_string(static_cast<int>(m_current_x)) +
						" win_x=" + std::to_string(static_cast<int>(m_regs->get_win_x())));
				}
			}
			else if(m_wx_triggered &&
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

				if(m_debug_enabled)
				{
					debug_trace("check_mode -> REACT cx=" + std::to_string(static_cast<int>(m_current_x)) +
						" win_x=" + std::to_string(static_cast<int>(m_regs->get_win_x())));
				}
			}
		}
	}
}

void Fetcher::sm_bg()
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
		// Abort on LCDC.1 clear mid-fetch (SameBoy §8.8): if OBJ display is
		// disabled while a sprite fetch is in progress, abort the fetch and
		// drop the entry (no stamp). This matters for m3_lcdc_obj_en_change*.
		if(!m_regs->get_lcdc_obj_enable())
		{
			if(m_current_oam_entry < m_oam_line_entries.size())
			{
				m_oam_line_entries.get_entry(m_current_oam_entry).m_done = true;
			}
			m_mode = FetchMode::FS_BGD;
			m_obj_in_fixed = false;
			m_obj_fixed_left = 0;
			return;
		}

		{
			static const char* qs = getenv("QTR_OBJ_STEP");
			if(qs && m_regs->get_ly() == static_cast<u8>(atoi(qs)))
			{
				fprintf(stderr, "[OBJSTEP] gt=%llu state=%u fifo=%u infixed=%d left=%u catchup=%d\n",
					qtreGB::qtregb_global_tick, static_cast<unsigned>(m_state_bg), m_bgw_fifo.size(),
					m_obj_in_fixed ? 1 : 0, m_obj_fixed_left, m_obj_fixed_left == 0 ? 0 : 1);
			}
		}
		// SameBoy's object fetch (Core/display.c, the mode-3 loop): the fetch
		// first lets the background fetcher catch up — it is advanced one state
		// per dot until it has read the high bitplane and the FIFO is not empty
		// — and then costs a fixed number of dots while the sprite's own data is
		// read. The whole fetch stalls the pixel output (see pop_pixel), and its
		// cost depends on the phase of the background fetcher at the trigger dot.
		//
		// The fixed part is one dot advancing the background fetcher, two dots
		// reading the sprite's OAM entry, two dots reading the low bitplane and
		// one dot reading the high bitplane.
		static const u8 fixed_dots = []()
		{
			const char* e = getenv("QTR_OBJ_FIXED");
			return e ? (u8)atoi(e) : (u8)6;
		}();
		static const bool catchup_enabled = []()
		{
			const char* e = getenv("QTR_OBJ_CATCHUP");
			return e ? (atoi(e) != 0) : true;
		}();

		if(!m_obj_in_fixed)
		{
			if(catchup_enabled &&
				(m_state_bg < FetchEntry::FS_DATA1_T2 || m_bgw_fifo.empty()))
			{
				advance_bg_state();
				return;
			}

			m_obj_in_fixed = true;
			m_obj_fixed_left = fixed_dots;
		}

		// The first dot of the fixed part advances the background fetcher; the
		// remaining dots read the sprite's data.
		if(m_obj_fixed_left == fixed_dots)
		{
			advance_bg_state();
		}

		m_obj_fixed_left--;

		if(m_obj_fixed_left == 0)
		{
			load_data_sprite(0);
			load_data_sprite(1);
			push_data_sprite();

			if(getenv("QTR_OBJ_TRACE"))
			{
				fprintf(stderr, "[OBJTRACE] end ly=%u oamx=%u cost=%llu bgstate=%u end_tick=%llu\n",
					m_regs->get_ly(),
				        m_oam_line_entries.get_entry(m_current_oam_entry).m_oam_entry.m_x,
				        qtreGB::qtregb_global_tick - m_obj_start_tick,
				        static_cast<unsigned>(m_state_bg), qtreGB::qtregb_global_tick);
			}

			m_mode = FetchMode::FS_BGD;
		}

		return;
	}

	advance_bg_state();
}

void Fetcher::advance_bg_state()
{
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
				// can trigger the reactivation quirk. The off-screen window columns
				// are not dropped here; pop_pixel consumes them one pixel per dot.
				m_window_is_being_fetched = false;

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
