#include <algorithm>

#include "ppu.h"
#include "QtreGB.h"
#include "lcd.h"
#include "fetcher.h"
#include "logger.h"
#include "utils.h"

using namespace qtreGB;
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
	m_lcd_on_line = 255;
	m_oam_read_locked = false;
	m_vram_read_locked = false;
	m_oam_write_locked = false;
	m_vram_write_locked = false;
	m_fetcher.set_state(FetchEntry::FS_TILE);
	m_pixel_fifo = vector<u8>();
	m_video_buffer = vector<u8>(PPU::YRES * PPU::XRES);

	m_lcd->lcd_init();
	m_lcd->set_lcds_mode(LCDMode::MODE_OAM);

	m_lcd_enabled = m_lcd->get_lcdc_lcd_enable();
	m_last_lyc = m_lcd->m_lyc;
	m_lyc_update_countdown = 0;

	m_oam_line_entry = vector<OAMEntry>();

	m_bgw_fifo.clear();
	m_spr_fifo.clear();
	m_fetcher.set_lcd(m_lcd);
}

u8 PPU::ppu_oam_read(u16 address)
{
	if(m_oam_read_locked)
	{
		return 0xFF;
	}

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
	if(m_oam_write_locked)
	{
		return;
	}

	ppu_oam_write_dma(address, value);
}

void PPU::ppu_oam_write_dma(u16 address, u8 value)
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
	if(m_vram_read_locked)
	{
		return 0xFF;
	}

	return m_vram.read(address);
}

void PPU::ppu_vram_write(u16 address, u8 value)
{
	if(m_vram_write_locked)
	{
		return;
	}

	m_vram.write(address, value);
}

const std::vector<u8> &PPU::get_video_buffer() const
{
	return m_video_buffer;
}

void PPU::ppu_sync_lcd_power()
{
	const bool lcd_enabled = m_lcd->get_lcdc_lcd_enable();

	if(lcd_enabled != m_lcd_enabled)
	{
		if(lcd_enabled)
		{
			ppu_power_on();
		}
		else
		{
			ppu_power_off();
		}
		m_lcd_enabled = lcd_enabled;
	}
}

void PPU::ppu_tick()
{
	ppu_sync_lcd_power();

	if(!m_lcd_enabled)
	{
		return;
	}

	if(m_lcd->m_lyc != m_last_lyc)
	{
		m_last_lyc = m_lcd->m_lyc;
		m_lyc_update_countdown = 0;	// The comparison is re-evaluated instantly against the new LYC: cancel any pending delayed update
		check_lyc();	// The game wrote LYC: LY==LYC must be re-evaluated (it is compared constantly while the PPU is on)
	}

	m_line_ticks++;

	if(m_lyc_update_countdown > 0 && --m_lyc_update_countdown == 0)
	{
		set_lyc_flag(m_lyc_update_value);
	}

	if(m_lcd_on_line != 255)
	{
		ppu_lcd_on_line();
		return;
	}

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

void PPU::ppu_reset_line_state()
{
	m_line_ticks = 0;
	m_pushed_x = 0;
	m_line_x = 0;
	m_fetched = 0;
	m_discarded_pixels = 0;
	m_window_line = 0;
	m_oam_line_entry = vector<OAMEntry>();
	m_bgw_fifo.clear();
	m_spr_fifo.clear();
	m_fetcher.reset();
	m_fetcher.set_window_line(0);
}

void PPU::ppu_power_off()
{
	// LY is forced to 0 and STAT mode to 0 (HBlank). The LYC==LY flag is NOT updated:
	// with the PPU off the comparison clock is halted and the flag keeps its last value
	// (mooneye stat_lyc_onoff). No interrupts are generated.
	m_lcd->m_ly = 0;
	m_lcd->set_lcds_mode(LCDMode::MODE_HBLANK);
	m_lcd_on_line = 255;
	m_lyc_update_countdown = 0;	// A pending delayed update must not fire while the comparison clock is halted
	m_oam_read_locked = false;
	m_vram_read_locked = false;
	m_oam_write_locked = false;
	m_vram_write_locked = false;
	ppu_reset_line_state();
}

void PPU::ppu_power_on()
{
	// The first line (line 0) starts in mode 0 instead of mode 2 and goes straight to mode 3,
	// with the PPU running late by 2 T-cycles (mooneye lcdon_timing-GS). Lines 1 and 2 keep
	// a late OAM scan (+4 dots) before returning to the normal schedule on line 3. The
	// LYC==LY comparison restarts with LY=0 and its interrupt only fires on a 0->1 edge
	// (mooneye stat_lyc_onoff).
	m_lcd->m_ly = 0;
	m_lcd->set_lcds_mode(LCDMode::MODE_HBLANK);
	m_lcd_on_line = 0;
	m_lyc_update_countdown = 0;	// The comparison restarts: apply the instant re-evaluation below instead of any stale pending update
	m_oam_read_locked = false;
	m_vram_read_locked = false;
	m_oam_write_locked = false;
	m_vram_write_locked = false;
	ppu_reset_line_state();
	check_lyc();
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
		m_stat_irq_line = false;	// Clear STAT IRQ line on entering mode 3

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
	pipeline_process();

	// STAT mode=0 interrupt happens one M-cycle (4 T-cycles) before the actual
	// mode switch (mooneye ppu.rs: cycles == 1 in AccessVram).
	if(m_pushed_x == PPU::XRES - 3)
	{
		if(m_lcd->get_lcds_stat_int(StatSRC::SS_HBLANK))
		{
			request_interrupt(InterruptType::IT_LCD_STAT);
		}
	}

	if(m_pushed_x > PPU::XRES || m_line_ticks >= 289 /* It should never exit from this condition...*/)
	{
		m_lcd->set_lcds_mode(LCDMode::MODE_HBLANK);
		m_oam_read_locked = false;	// Mode 0: OAM is accessible to the CPU again
		update_stat_irq();
	}
}

void PPU::ppu_mode_hblank()
{
	if(m_line_ticks == PPU::TICKS_PER_LINE) // If reached to the end of hblank we increment the internal counter to actual line
	{
		increment_ly();

		if(m_lcd->m_ly == PPU::YRES) // If reached to the number of lines the lcd has, we go to vblank
		{
			m_lcd->set_lcds_mode(LCDMode::MODE_VBLANK);
			m_oam_read_locked = false;
			request_interrupt(InterruptType::IT_VBLANK);
			update_stat_irq();
			// The mode 2 (OAM) interrupt is also triggered when vblank starts (mooneye vblank_stat_intr-GS)
			update_stat_irq();
		}
		else
		{
			m_lcd->set_lcds_mode(LCDMode::MODE_OAM);
			m_oam_read_locked = true;	// Mode 2: the PPU is scanning OAM, so the CPU cannot read it
			update_stat_irq();
		}
		m_line_ticks = 0;
	}
}

// Special schedule for the first 3 lines right after the LCD is switched on
// (mooneye lcdon_timing-GS / lcdon_write_timing-GS). The measured PPU clock is late by
// 8 ticks (2 T-cycles) on power-on, so line 0 uses +8 constants: it skips the OAM scan,
// runs mode 0 [0,78), mode 3 [78,250), mode 0 [250,450) and advances LY at 450 with
// OAM/VRAM fully locked during mode 3. Lines 1 and 2 reset that offset: normal-length
// (456) lines with a late OAM scan (mode 0 [0,4), mode 2 [4,84), mode 3 [84,256),
// mode 0 [256,456)) and CPU-access windows OAM read [2,256), OAM write
// [4,80) U [84,256), VRAM read [80,256) and VRAM write [84,256). The embedder pushes
// no pixels for these lines: the tests only probe OAM/VRAM access and STAT.
void PPU::ppu_lcd_on_line()
{
	const u16 t = m_line_ticks;

	if(m_lcd_on_line == 0)
	{
		if(t == 78)
		{
			m_lcd->set_lcds_mode(LCDMode::MODE_XFER);
			m_stat_irq_line = false;
			m_oam_read_locked = true;
			m_vram_read_locked = true;
			m_oam_write_locked = true;
			m_vram_write_locked = true;
		}
		else if(t == 250)
		{
			m_lcd->set_lcds_mode(LCDMode::MODE_HBLANK);
			m_oam_read_locked = false;
			m_vram_read_locked = false;
			m_oam_write_locked = false;
			m_vram_write_locked = false;
			if(m_lcd->get_lcds_stat_int(StatSRC::SS_HBLANK))
			{
				request_interrupt(InterruptType::IT_LCD_STAT);
			}
		}
		else if(t == 450)
		{
			increment_ly();
			m_lcd_on_line = 1;
			m_line_ticks = 0;
		}
		return;
	}

	if(t == 2)
	{
		m_oam_read_locked = true;
	}
	else if(t == 4)
	{
		m_lcd->set_lcds_mode(LCDMode::MODE_OAM);
		m_oam_write_locked = true;
		update_stat_irq();
	}
	else if(t == 80)
	{
		m_oam_write_locked = false;
		m_vram_read_locked = true;
	}
	else if(t == 84)
	{
		m_lcd->set_lcds_mode(LCDMode::MODE_XFER);
		m_stat_irq_line = false;
		m_oam_write_locked = true;
		m_vram_write_locked = true;
	}
	else if(t == 256)
	{
		m_lcd->set_lcds_mode(LCDMode::MODE_HBLANK);
		m_oam_read_locked = false;
		m_vram_read_locked = false;
		m_oam_write_locked = false;
		m_vram_write_locked = false;
		update_stat_irq();
	}
	else if(t == 456)
	{
		increment_ly();
		m_line_ticks = 0;
		if(m_lcd_on_line == 1)
		{
			m_lcd_on_line = 2;
		}
		else
		{
			m_lcd_on_line = 255;
			m_lcd->set_lcds_mode(LCDMode::MODE_OAM);
			update_stat_irq();
		}
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
			m_oam_read_locked = true;	// Mode 2 of the new frame: OAM is locked again
			m_lcd->m_ly = 0;
			check_lyc();
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
	const u8 finished_line = m_lcd->m_ly;	// the line that has just completed (before LY is advanced)

	m_lcd->m_ly++;

	// The window keeps an internal line counter that advances only on lines where the
	// window is actually displayed. It is used as the window's tile row, and it must
	// survive the window being hidden mid-frame (dmg-acid2 draws the right eye, hides the
	// window with WX=240, then draws the chin continuing from the same counter).
	const bool window_shown_on_finished_line =
		m_lcd->get_lcdc_win_enable() &&
		finished_line < PPU::YRES &&
		finished_line >= m_lcd->m_win_y &&
		m_lcd->m_win_x <= 166;

	if(window_shown_on_finished_line)
	{
		m_window_line++;
		m_fetcher.set_window_line(m_window_line);
	}

	// The LYC==LY flag lags LY changes on the DMG: after LY changes, the new comparison
	// result is written 4 dots later when it sets and 2 dots later when it clears.
	// (mooneye lcdon_timing-GS: with LYC=1, STAT shows $80 at dot 452 and $86 at dot 456.)
	// The dots are counted by m_lyc_update_countdown, decremented in ppu_tick.
	const bool new_value = (m_lcd->m_ly == m_lcd->m_lyc);
	if(new_value != m_lcd->get_lcds_lyc())
	{
		m_lyc_update_value = new_value;
		m_lyc_update_countdown = new_value ? 4 : 2;
	}
}

void PPU::check_lyc()
{
	set_lyc_flag(m_lcd->m_ly == m_lcd->m_lyc);
}

void PPU::set_lyc_flag(bool ly_equals_lyc)
{
	m_lcd->set_lcds_lyc(ly_equals_lyc);
	update_stat_irq();
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
			m_spr_fifo.pop();
		}
		m_discarded_pixels++;
		return;
	}

	FIFOElement bgw_element = m_bgw_fifo.pop();
	// LCDC bit 0 = 0 blanks the whole BG/window layer: it outputs BGP color 0 while
	// objects are still drawn on top (dmg-acid2 "hair" rows 8-15).
	const u8 bgw_color_index = m_lcd->get_lcdc_bgw_enable() ? bgw_element.get_color_index() : 0;
	pixel_data = (m_lcd->m_bg_palette >> (bgw_color_index * 2)) & 0x03;

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


	if(m_pushed_x < PPU::XRES)
	{
		if(m_lcd->get_lcdc_obj_enable())
		{
			if(!m_spr_fifo.empty())
			{
				FIFOElement spr_element = m_spr_fifo.pop();
				if(spr_element.get_color_index() != 0)
				{
					if(!spr_element.get_priority() || bgw_color_index == 0)
					{
						u8 palette = spr_element.get_palette() ? m_lcd->m_obj_palette_1 : m_lcd->m_obj_palette_0;
						pixel_data = (palette >> (spr_element.get_color_index() * 2)) & 0x03;
					}
				}
			}
			else
			{
				FIFOElement spr_element = m_fetcher.get_object_array()[m_pushed_x];
				if(spr_element.get_color_index() != 0)
				{
					if(!spr_element.get_priority() || bgw_color_index == 0)
					{
						u8 palette = spr_element.get_palette() ? m_lcd->m_obj_palette_1 : m_lcd->m_obj_palette_0;
						pixel_data = (palette >> (spr_element.get_color_index() * 2)) & 0x03;
					}
				}
			}
		}

		m_video_buffer[m_pushed_x +	(m_lcd->m_ly * PPU::XRES)] = pixel_data;
	}
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

void PPU::request_interrupt(InterruptType IType)
{
	if (m_request_interrupt)
	{
		m_request_interrupt(IType);
	}
}

void PPU::update_stat_irq()
{
	bool match = false;
	LCDMode mode = m_lcd->get_lcds_mode();
	// Check conditions in priority order? The hardware checks all enabled conditions
	if(m_lcd->get_lcds_stat_int(StatSRC::SS_LYC) && m_lcd->get_lcds_lyc())
	{
		match = true;
	}
	if(!match && m_lcd->get_lcds_stat_int(StatSRC::SS_OAM) && mode == LCDMode::MODE_OAM)
	{
		match = true;
	}
	if(!match && m_lcd->get_lcds_stat_int(StatSRC::SS_VBLANK) && mode == LCDMode::MODE_VBLANK)
	{
		match = true;
	}
	if(!match && m_lcd->get_lcds_stat_int(StatSRC::SS_HBLANK) && mode == LCDMode::MODE_HBLANK)
	{
		match = true;
	}
	if(match && !m_stat_irq_line)
	{
		m_stat_irq_line = true;
		request_interrupt(InterruptType::IT_LCD_STAT);
	}
}
