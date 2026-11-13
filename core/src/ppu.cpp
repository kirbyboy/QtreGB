#include "ppu.h"
#include "QtreGB.h"
#include "lcd.h"

using namespace qtreGB;
using namespace std;

const int PPU::LINES_PER_FRAME = 154;
const int PPU::TICKS_PER_LINE = 456;
const int PPU::YRES = 144;
const int PPU::XRES = 160;

namespace
{
int floor_div8(int value)
{
	return value >= 0 ? value / 8 : -(((-value) + 7) / 8);
}

int floor_mod8(int value)
{
	const int remainder = value % 8;
	return remainder < 0 ? remainder + 8 : remainder;
}

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
m_oam_ram(vector<OAMEntry>(40))
{}

void PPU::ppu_init(bool boot_rom)
{
	m_line_ticks = 0;
	m_dots = 0;
	m_pushed_x = 0;
	m_lcd_on_line = 255;
	m_oam_bus.reset();
	m_vram_bus.reset();
	m_video_buffer = vector<u8>(PPU::YRES * PPU::XRES);

	m_lcd->lcd_init(boot_rom);
	if(boot_rom)
	{
		// Power-on DMG state: the LCD is off until the boot ROM writes LCDC.
		// The PPU stays idle in mode 0 and ppu_power_on() starts it later.
		m_lcd->set_lcds_mode(LCDMode::MODE_HBLANK);
		m_tick_selector = &PPU::ppu_mode_hblank;
	}
	else
	{
		m_lcd->set_lcds_mode(LCDMode::MODE_OAM);
		m_tick_selector = &PPU::ppu_mode_oam;
	}

	m_lcd_enabled = m_lcd->get_lcdc_lcd_enable();
	m_lyc_seen = m_lcd->m_lyc;
	m_lyc_update_countdown = 0;
	m_last_stat_irq = false;
	m_pending_stat_irq = false;
	m_enable_lyc_eq_ly_irq = true;
	m_vblank_last_line = false;

	update_delayed_registers();
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

		// Power-on can complete the LYC comparison and generate a STAT IRQ.
		// The LCDC write path calls this synchronously, so the IF bit must be
		// raised here: waiting for the next ppu_tick would lose the interrupt
		// before the CPU executes the next instruction (stat_lyc_onoff round 4).
		if(m_pending_stat_irq)
		{
			request_interrupt(InterruptType::IT_LCD_STAT);
			m_pending_stat_irq = false;
		}
	}
}

void PPU::ppu_tick()
{
	ppu_sync_lcd_power();

	if(!m_lcd_enabled)
	{
		m_lcd->tick();
		return;
	}

	// STAT IRQ requests are delayed by 1 T-cycle: the edge was detected on the
	// previous dot, so raise it now.
	if(m_pending_stat_irq)
	{
		request_interrupt(InterruptType::IT_LCD_STAT);
		m_pending_stat_irq = false;
	}

	if(m_lcd->m_lyc != m_lyc_seen)
	{
		m_lyc_seen = m_lcd->m_lyc;
		m_lyc_update_countdown = 0;
		check_lyc();
	}

	m_line_ticks++;
	m_dots = m_line_ticks - 1;

	if(m_lyc_update_countdown > 0 && --m_lyc_update_countdown == 0)
	{
		set_lyc_flag(m_lyc_update_value);
	}

	if(m_lcd_on_line != 255)
	{
		ppu_lcd_on_line();
		m_lcd->tick();
		update_delayed_registers();
		return;
	}

	(this->*m_tick_selector)();
	raise_stat_irq();
	m_lcd->tick();
	update_delayed_registers();
}

void PPU::ppu_reset_line_state()
{
	m_line_ticks = 0;
	m_dots = 0;
	m_pushed_x = 0;
	m_window_line = 0;
}

void PPU::ppu_power_off()
{
	// LY is forced to 0 and STAT mode to 0 (HBlank). The LYC==LY flag is NOT updated:
	// with the PPU off the comparison clock is halted and the flag keeps its last value
	// (mooneye stat_lyc_onoff). No interrupts are generated.
	m_lcd->m_ly = 0;
	m_lcd->set_lcds_mode(LCDMode::MODE_HBLANK);
	m_tick_selector = &PPU::ppu_mode_hblank;
	m_lcd_on_line = 255;
	m_lyc_update_countdown = 0;	// A pending delayed update must not fire while the comparison clock is halted
	m_last_stat_irq = false;
	m_pending_stat_irq = false;
	m_stat_irq_line = false;
	m_enable_lyc_eq_ly_irq = true;
	m_vblank_last_line = false;
	m_oam_bus.reset();
	m_vram_bus.reset();
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
	m_tick_selector = &PPU::ppu_mode_oam;
	m_lcd_on_line = 0;
	m_lyc_update_countdown = 0;	// The comparison restarts: apply the instant re-evaluation below instead of any stale pending update
	m_last_stat_irq = false;
	m_pending_stat_irq = false;
	m_enable_lyc_eq_ly_irq = true;
	m_vblank_last_line = false;
	m_oam_bus.reset();
	m_vram_bus.reset();
	ppu_reset_line_state();
	check_lyc();
	update_delayed_registers();
}

void PPU::ppu_mode_oam()
{
	if(m_dots == 0)
	{
		m_oam_scan_index = 0;
		m_oam_scan_count = 0;
		m_oam_scan_obj_height = m_lcd->get_lcdc_obj_height();
		for(auto& entries : m_oam_entries)
		{
			entries.clear();
		}
		m_oam_served_count.fill(0);
	}

	// Per-dot OAM scan: Y on even dots, X/tile/flags on odd dots.
	if(m_dots < 80 && m_oam_scan_index < 40)
	{
		if((m_dots & 1) == 0)
		{
			m_oam_scan_y = m_oam_ram[m_oam_scan_index].m_y;
		}
		else
		{
			OAMEntry entry = m_oam_ram[m_oam_scan_index];
			entry.m_y = m_oam_scan_y;

			const u8 current_y = m_lcd->m_ly;
			if(entry.m_y <= current_y + 16 &&
			   entry.m_y + m_oam_scan_obj_height > current_y + 16)
			{
				if(m_oam_scan_count < 10)
				{
					if(entry.m_x < 168)
					{
						OamLineEntry line_entry;
						line_entry.entry = entry;
						line_entry.number = m_oam_scan_index;
						m_oam_entries[entry.m_x].push_back(line_entry);
					}
					m_oam_scan_count++;
				}
			}

			m_oam_scan_index++;
		}
	}

	if(m_dots == 79)
	{
		m_lcd->set_lcds_mode(LCDMode::MODE_XFER);
		m_tick_selector = &PPU::pixel_transfer_tick;

		// The internal STAT IRQ line is cleared on entering mode 3, unless the
		// LYC condition is still asserted. Keeping it high blocks the following
		// STAT interrupts (mooneye stat_irq_blocking).
		if(!(m_lcd->get_lcds_stat_int(StatSRC::SS_LYC) && m_lcd->get_lcds_lyc()))
		{
			m_stat_irq_line = false;
		}

		m_pushed_x = 0;
		m_lx = 0;
		m_initial_scx_to_discard = 0;
		m_initial_scx_discarded = 0;
		m_bg_fetch_x = 0;
		m_window_tile_x = 0;
		m_window_active = false;
		m_window_just_activated = false;
		m_is_fetching_sprite = false;
		m_pixel_transfer_phase = PixelTransferPhase::Dummy;
		m_fetch_phase = FetchPhase::BgTilemap;
		m_fetch_window = false;
		m_bg_pixels.clear();
		m_obj_pixels.clear();
	}
}

void PPU::ppu_mode_hblank()
{
	// The OAM STAT IRQ is signalled at dot 452 on DMG.
	if(m_dots == 452 && m_lcd->m_ly != PPU::YRES)
	{
		update_stat_irq_for_oam_mode();
	}

	if(m_dots == 448)
	{
		// Scheduler phase calibration: LY++ is nominally at dot 453, but the
		// CPU/PPU tick order makes the observable change land at 448 here.
		// OAM is locked and the LYC IRQ window closes at the same point.
		if(m_lcd->m_ly != PPU::YRES - 1)
		{
			m_oam_bus.acquire(VideoDevice::Ppu, VideoAccess::Read);
		}

		m_enable_lyc_eq_ly_irq = false;
		increment_ly();
	}
	else if(m_dots == 454)
	{
		// The STAT mode changes one dot before the line ends (LY advances at 453).
		m_enable_lyc_eq_ly_irq = true;

		if(m_lcd->m_ly == PPU::YRES)
		{
			m_lcd->set_lcds_mode(LCDMode::MODE_VBLANK);
		}
		else
		{
			m_lcd->set_lcds_mode(LCDMode::MODE_OAM);
		}

		// DMG quirk: the OAM source is also signalled when VBlank starts,
		// at the same time as the VBlank interrupt.
		if(m_lcd->m_ly == PPU::YRES)
		{
			update_stat_irq_for_oam_mode();
		}
	}
	else if(m_dots == PPU::TICKS_PER_LINE - 1)
	{
		if(m_lcd->m_ly == PPU::YRES)
		{
			m_tick_selector = &PPU::ppu_mode_vblank;
			m_vblank_last_line = false;
			m_oam_bus.release(VideoDevice::Ppu, VideoAccess::Read);
			request_interrupt(InterruptType::IT_VBLANK);
			update_stat_irq();
			update_stat_irq();
		}
		else
		{
			m_tick_selector = &PPU::ppu_mode_oam;
			// OAM IRQ was already handled by
			// update_stat_irq_for_oam_mode() at dot 454.
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
			m_oam_bus.acquire(VideoDevice::Ppu, VideoAccess::Read);
			m_vram_bus.acquire(VideoDevice::Ppu, VideoAccess::Read);
			m_oam_bus.acquire(VideoDevice::Ppu, VideoAccess::Write);
			m_vram_bus.acquire(VideoDevice::Ppu, VideoAccess::Write);
		}
		else if(t == 250)
		{
			m_lcd->set_lcds_mode(LCDMode::MODE_HBLANK);
			m_oam_bus.release(VideoDevice::Ppu, VideoAccess::Read);
			m_vram_bus.release(VideoDevice::Ppu, VideoAccess::Read);
			m_oam_bus.release(VideoDevice::Ppu, VideoAccess::Write);
			m_vram_bus.release(VideoDevice::Ppu, VideoAccess::Write);
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
		m_oam_bus.acquire(VideoDevice::Ppu, VideoAccess::Read);
	}
	else if(t == 4)
	{
		m_lcd->set_lcds_mode(LCDMode::MODE_OAM);
		m_oam_bus.acquire(VideoDevice::Ppu, VideoAccess::Write);
		update_stat_irq();
	}
	else if(t == 80)
	{
		m_oam_bus.release(VideoDevice::Ppu, VideoAccess::Write);
		m_vram_bus.acquire(VideoDevice::Ppu, VideoAccess::Read);
	}
	else if(t == 84)
	{
		m_lcd->set_lcds_mode(LCDMode::MODE_XFER);
		m_stat_irq_line = false;
		m_oam_bus.acquire(VideoDevice::Ppu, VideoAccess::Write);
		m_vram_bus.acquire(VideoDevice::Ppu, VideoAccess::Write);
	}
	else if(t == 256)
	{
		m_lcd->set_lcds_mode(LCDMode::MODE_HBLANK);
		m_oam_bus.release(VideoDevice::Ppu, VideoAccess::Read);
		m_vram_bus.release(VideoDevice::Ppu, VideoAccess::Read);
		m_oam_bus.release(VideoDevice::Ppu, VideoAccess::Write);
		m_vram_bus.release(VideoDevice::Ppu, VideoAccess::Write);
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
			m_tick_selector = &PPU::ppu_mode_oam;
			update_stat_irq();
		}
	}
}

void PPU::ppu_mode_vblank()
{
	// On the last VBlank line LY drops to 0 at dot 2, but the line itself
	// continues until dot 455. Remember that state explicitly: after the reset
	// LY no longer identifies the line.
	if(!m_vblank_last_line && m_dots == 0 && m_lcd->m_ly == PPU::LINES_PER_FRAME - 1)
	{
		m_vblank_last_line = true;
	}

	if(m_vblank_last_line)
	{
		if(m_dots == 2)
		{
			m_lcd->m_ly = 0;
			schedule_lyc_update();
			m_enable_lyc_eq_ly_irq = false;
		}
		else if(m_dots == 7)
		{
			m_enable_lyc_eq_ly_irq = true;
		}
		else if(m_dots == 453)
		{
			m_oam_bus.acquire(VideoDevice::Ppu, VideoAccess::Read);

			// Start-of-frame OAM STAT IRQ: raised here so that IF is already
			// set at line 0 dot 0 (mooneye intr_1_2_timing-GS).
			update_stat_irq_for_oam_mode();
		}
		else if(m_dots == PPU::TICKS_PER_LINE - 1)
		{
			m_lcd->set_lcds_mode(LCDMode::MODE_OAM);
			m_tick_selector = &PPU::ppu_mode_oam;
			m_window_active_for_frame = false;
			m_window_active = false;
			m_line_ticks = 0;
			m_window_line = 0;
			m_window_next_row = 0;
			m_vblank_last_line = false;
			check_lyc();
			update_stat_irq();

			if(m_frame_done)
			{
				m_frame_done(true); // We have all the info to draw the screen
			}
		}
		return;
	}

	// Normal VBlank line.
	if(m_dots == 453)
	{
		m_enable_lyc_eq_ly_irq = false;
		increment_ly();
	}
	else if(m_dots == 454)
	{
		m_enable_lyc_eq_ly_irq = true;
	}
	else if(m_dots == PPU::TICKS_PER_LINE - 1)
	{
		m_line_ticks = 0;
	}
}

void PPU::increment_ly()
{
	m_lcd->m_ly++;

	schedule_lyc_update();
}

void PPU::schedule_lyc_update()
{
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
	const bool previous = m_lcd->get_lcds_lyc();
	m_lcd->set_lcds_lyc(ly_equals_lyc);

	// The STAT interrupt is edge-triggered by the 0->1 transition of the
	// comparison result. This also makes power-on keep the retained flag
	// without firing when the result does not change (stat_lyc_onoff).
	if(ly_equals_lyc && !previous)
	{
		update_stat_irq();
	}
}

void PPU::connect_bus(IBus *bus)
{
	m_bus = bus;
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
		m_pending_stat_irq = true;	// raised on the next dot (1-T delay)
	}
}

//*********************//
// F5 pixel pipeline   //
//*********************//

void PPU::pixel_transfer_tick()
{
	// m_lx is X + 8 and must stay in [0, 168). If the mode was changed
	// externally, do not index m_oam_entries out of bounds.
	if(m_lx >= 168)
	{
		finish_pixel_transfer();
		return;
	}

	// SCX's low 3 bits are sampled once per line at the start of the first
	// real B fetch (see mealybug m3_scx_low_3_bits). A write after this dot
	// still changes the tilemap address but not the sub-tile alignment.
	if(m_dots == 89)
	{
		m_bg_line_scx_low = static_cast<u8>(m_lcd->m_scroll_x % 8);
	}

	// The window becomes eligible for the rest of the frame once LY == WY.
	if(m_lcd->get_lcdc_win_enable() && m_lcd->m_ly == m_lcd->m_win_y)
	{
		m_window_active_for_frame = true;
	}

	if(m_dots >= 451)
	{
		finish_pixel_transfer();
		return;
	}

	if(m_obj_post_wait > 0)
	{
		--m_obj_post_wait;
	}

	tick_fetcher();

	// The OBJ push takes one more dot before the FIFO can be popped: the
	// object pixels must be available for the next pixel, not the current one.
	if(m_obj_post_wait > 0)
	{
		return;
	}

	if(m_pixel_transfer_phase == PixelTransferPhase::Dummy)
	{
		m_bg_pixels.clear();
		for(int i = 0; i < 8; i++)
		{
			m_bg_pixels.push_back(BgPixel{0});
		}

		m_initial_scx_to_discard = static_cast<u8>(m_lcd->m_scroll_x % 8);
		m_initial_scx_discarded = 0;
		m_obj_considered_tiles.clear();
		m_obj_multiple_groups = false;
		{
			int groups = 0;
			for(const auto& entries : m_oam_entries)
			{
				if(!entries.empty())
				{
					++groups;
					if(groups > 1) break;
				}
			}
			m_obj_multiple_groups = groups > 1;
		}
		m_last_group_lx = 0xFF;
		m_obj_group_total = 0;
		m_lx = 0;
		m_bg_fetch_x = 0;
		m_bg_fetch_skip = 0;
		m_bg_prev_scx_low = static_cast<u8>(m_lcd->m_scroll_x % 8);
		m_bg_line_scx_low = m_bg_prev_scx_low;
		m_fetch_phase = FetchPhase::BgTilemap;
		m_fetch_window = false;
		m_window_active = false;
		m_window_activating = false;
		m_window_activation_delay = 0;
		m_pixel_transfer_phase = m_initial_scx_to_discard != 0
			? PixelTransferPhase::Discard
			: PixelTransferPhase::Lx0;
		return;
	}

	// Window activation: throw away the BG FIFO and restart the fetcher with
	// the window tilemap at the current LX. On DMG WX/LCDC.WIN are seen with
	// a 1 T-cycle delay; the +1 case is the documented late-activation glitch.
	const bool win_enable_now = m_lcd->get_lcdc_win_enable();
	const bool win_enable_last = (m_last_lcdc & 0x20) != 0;

	// Disabling WIN during mode 3 aborts the window fetch and returns to the
	// background at the next tile fetch. The already-fetched pixels drain.
	if((m_window_active || m_window_activating) && !win_enable_last)
	{
		m_window_active = false;
		m_window_activating = false;
		m_window_activation_delay = 0;
		m_fetch_window = false;
		m_fetch_phase = FetchPhase::BgTilemap;
	}

	if(m_window_activating)
	{
		if(m_window_activation_delay > 0)
		{
			--m_window_activation_delay;
			return;
		}

		// Activate one dot after the trigger: throw away the BG FIFO and
		// restart the fetcher with the window tilemap.
		m_window_active = true;
		m_window_just_activated = true;
		m_window_tile_x = 0;
		// The window row advances on every activation, including multiple
		// activations on the same line.
		m_window_line = m_window_next_row++;
		m_bg_pixels.clear();
		m_fetch_phase = FetchPhase::BgTilemap;
		m_fetch_window = true;
		m_window_activating = false;
		return;
	}
	if(!m_window_active && m_window_active_for_frame && win_enable_now &&
	   (m_lx == m_last_wx + 1 ||
	    (m_lx == m_last_wx + 2 && !win_enable_last)))
	{
		m_window_activating = true;
		// WX=0 with SCX % 8 > 0 activates one dot later, during the discard.
		m_window_activation_delay =
			(m_pixel_transfer_phase == PixelTransferPhase::Discard && m_last_wx == 0) ? 1 : 0;
		return;
	}

	// Mode 3 has a four-dot startup before the FIFO starts draining: the
	// fetcher needs its first tile access before pixel 0 can be produced.
	// Without it mode 0 starts four dots too early (169 instead of 172 dots).
	if(m_dots < 85)
	{
		return;
	}

	if(m_pixel_transfer_phase == PixelTransferPhase::Discard)
	{
		if(m_is_fetching_sprite || m_obj_post_wait > 0)
		{
			return;
		}

		if(m_lcd->get_lcdc_obj_enable() && oam_entry_pending(m_lx))
		{
			start_sprite_fetch();
			return;
		}

		if(m_bg_pixels.empty())
		{
			return;
		}

		m_bg_pixels.pop_front();
		if(!m_obj_pixels.empty())
		{
			m_obj_pixels.pop_front();
		}
		if(++m_initial_scx_discarded == m_initial_scx_to_discard)
		{
			m_pixel_transfer_phase = PixelTransferPhase::Lx0;
		}
		return;
	}

	if(m_pixel_transfer_phase == PixelTransferPhase::Lx0)
	{
		if(m_is_fetching_sprite || m_obj_post_wait > 0)
		{
			return;
		}

		if(m_lcd->get_lcdc_obj_enable() && oam_entry_pending(m_lx))
		{
			start_sprite_fetch();
			return;
		}

		if(m_bg_pixels.empty())
		{
			return;
		}

		m_bg_pixels.pop_front();
		if(!m_obj_pixels.empty())
		{
			m_obj_pixels.pop_front();
		}
		if(++m_lx == 8)
		{
			m_pixel_transfer_phase = PixelTransferPhase::Lx8;
		}
		return;
	}

	// Expose mode 0 one dot before the last pixel finishes. The framebuffer
	// still receives the final pixels, but the CPU observes the mode switch
	// at the end-of-line timing used by the sprite timing test. OBJs at the
	// last tile must finish their fetch and penalty first.
	if(m_lx == 167 && !oam_entry_pending(167) && !m_is_fetching_sprite &&
	   m_obj_post_wait == 0 && m_lcd->get_lcds_mode() == LCDMode::MODE_XFER)
	{
		m_lcd->set_lcds_mode(LCDMode::MODE_HBLANK);
	}

	// The STAT mode 0 interrupt is raised one M-cycle before the actual mode
	// switch; this is what the CPU measures in intr_2_0_timing.
	if(m_lx == 163 && m_lcd->get_lcds_stat_int(StatSRC::SS_HBLANK) && !m_stat_irq_line)
	{
		m_stat_irq_line = true;
		request_interrupt(InterruptType::IT_LCD_STAT);
	}

	// Visible pixels.
	if(m_is_fetching_sprite || m_obj_post_wait > 0)
	{
		return;
	}

	if(m_lx < 168 && m_lcd->get_lcdc_obj_enable() && oam_entry_pending(m_lx))
	{
		start_sprite_fetch();
		return;
	}

	if(m_bg_pixels.empty())
	{
		return;
	}

	const BgPixel bg = m_bg_pixels.front();
	m_bg_pixels.pop_front();

	// DMG: the PPU sees LCDC/BGP delayed by 1 T-cycle. LX == 8 is the
	// documented exception for LCDC.
	const u8 lcdc = m_lcd->m_lcdc;
	const bool bg_enable = (lcdc & 0x01) != 0;
	const bool obj_enable = (lcdc & 0x02) != 0;
	const u8 bgp = m_lcd->m_bg_palette;

	const u8 bg_color_index = bg_enable ? bg.color_index : 0;
	u8 color = static_cast<u8>((bgp >> (bg_color_index * 2)) & 0x03);

	if(obj_enable && !m_obj_pixels.empty())
	{
		const ObjPixel obj = m_obj_pixels.front();
		m_obj_pixels.pop_front();

		if(obj.color_index != 0 && (!obj.priority || bg_color_index == 0))
		{
			const u8 palette = obj.palette ? m_lcd->m_obj_palette_1 : m_lcd->m_obj_palette_0;
			color = static_cast<u8>((palette >> (obj.color_index * 2)) & 0x03);
		}
	}

	if(m_lcd->m_ly < PPU::YRES && m_pushed_x < PPU::XRES)
	{
		m_video_buffer[m_pushed_x + m_lcd->m_ly * PPU::XRES] = color;
	}

	m_pushed_x++;
	m_lx++;

	if(m_lx >= 168)
	{
		finish_pixel_transfer();
	}
}

void PPU::finish_pixel_transfer()
{
	if(m_lcd->get_lcds_mode() == LCDMode::MODE_XFER)
	{
		m_lcd->set_lcds_mode(LCDMode::MODE_HBLANK);
	}

	m_tick_selector = &PPU::ppu_mode_hblank;
	m_oam_bus.release(VideoDevice::Ppu, VideoAccess::Read);
	m_is_fetching_sprite = false;
	update_stat_irq();
}

void PPU::tick_fetcher()
{
	if(m_window_activating)
	{
		return;	// The window trigger skips the next fetcher tick
	}

	if(m_is_fetching_sprite)
	{
		tick_obj_fetch();
	}
	else
	{
		tick_bg_fetch();
	}
}

void PPU::tick_bg_fetch()
{
	if(m_bg_fetch_wait > 0)
	{
		--m_bg_fetch_wait;
		return;
	}

	if(m_fetch_phase == FetchPhase::BgTilemap)
	{
		if(m_fetch_window && m_window_active)
		{
			// The window has its own tile grid: no SCX sub-tile offset.
			m_bg_fetch_skip = 0;
			m_bg_prev_scx_low = static_cast<u8>(m_lcd->m_scroll_x % 8);
		}
		else
		{
			const u8 scx_low = m_bg_line_scx_low;
			// The first tile of the line is aligned by the initial SCX
			// discard. Later tiles only need to discard the difference when
			// SCX's low bits change in the middle of the line.
			m_bg_fetch_skip = (m_bg_fetch_x == 0)
				? 0
				: static_cast<u8>((scx_low - m_bg_prev_scx_low) & 0x07);
			m_bg_prev_scx_low = scx_low;
		}

		const u16 x = static_cast<u16>(m_bg_fetch_x) + m_lcd->m_scroll_x;
		const u16 y = static_cast<u16>(m_lcd->m_ly) + m_lcd->m_scroll_y;
		u16 tilemap_x;
		u16 tilemap_y;
		u16 tilemap_base;

		if(m_fetch_window && m_window_active)
		{
			tilemap_x = m_window_tile_x++;
			tilemap_y = static_cast<u16>(m_window_line / 8);
			tilemap_base = m_lcd->get_lcdc_win_map_area();
		}
		else
		{
			tilemap_x = (x / 8) & 0x1F;
			tilemap_y = (y / 8) & 0x1F;
			tilemap_base = m_lcd->get_lcdc_bg_map_area();
		}

		m_fetch_tilemap_addr = static_cast<u16>(tilemap_base + tilemap_y * 32 + tilemap_x);
		m_fetch_tile_number = m_bus->read(m_fetch_tilemap_addr);
		if(m_fetch_window && m_window_active)
		{
			m_bg_fetch_wait = 1;
		}

		// Latch the tile row with the tile number. The LCDC data area is
		// still sampled at the low-byte access, matching the tile-select
		// change tests.
		m_fetch_tile_y = (m_fetch_window && m_window_active)
			? static_cast<u8>(m_window_line % 8)
			: static_cast<u8>((m_lcd->m_ly + m_lcd->m_scroll_y) % 8);
		m_fetch_phase = FetchPhase::BgLow;
		return;
	}

	if(m_fetch_phase == FetchPhase::BgLow)
	{
		const u16 tile_base = m_lcd->get_lcdc_bgw_data_area();
		const u16 tile_data = (tile_base == 0x8800)
			? static_cast<u16>(0x9000 + static_cast<int8_t>(m_fetch_tile_number) * 16)
			: static_cast<u16>(0x8000 + m_fetch_tile_number * 16);
		m_fetch_data_addr = static_cast<u16>(tile_data + m_fetch_tile_y * 2);
		m_fetch_low = m_bus->read(m_fetch_data_addr);
		m_fetch_phase = FetchPhase::BgHigh;
		if(m_fetch_window && m_window_active)
		{
			m_bg_fetch_wait = 1;
		}
		return;
	}

	if(m_fetch_phase == FetchPhase::BgHigh)
	{
		// The high byte re-samples LCDC (tile-select tests change the data
		// area between the two accesses), but keeps the row latched at the
		// tilemap fetch so a mid-tile SCY change cannot corrupt the byte pair.
		const u16 tile_base = m_lcd->get_lcdc_bgw_data_area();
		const u16 tile_data = (tile_base == 0x8800)
			? static_cast<u16>(0x9000 + static_cast<int8_t>(m_fetch_tile_number) * 16)
			: static_cast<u16>(0x8000 + m_fetch_tile_number * 16);
		const u8 tile_y = (m_fetch_window && m_window_active)
			? static_cast<u8>(m_window_line % 8)
			: m_fetch_tile_y;
		m_fetch_data_addr = static_cast<u16>(tile_data + tile_y * 2);
		m_fetch_high = m_bus->read(m_fetch_data_addr + 1);
		m_fetch_phase = FetchPhase::BgPush;
		if(m_fetch_window && m_window_active)
		{
			m_bg_fetch_wait = 1;
		}
		return;
	}

	if(m_fetch_phase == FetchPhase::BgPush)
	{
		// The FIFO is filled with a whole tile at once, so the fetcher must
		// wait until it is empty; meanwhile it has already fetched the next
		// tile and is ready to push.
		if(!m_bg_pixels.empty())
		{
			return;
		}

		push_bg_tile();
		m_fetch_phase = FetchPhase::BgTilemap;
		m_bg_fetch_x = static_cast<u8>(m_bg_fetch_x + 8);
	}
}

void PPU::push_bg_tile()
{
	for(int i = m_bg_fetch_skip; i < 8; i++)
	{
		const u8 bit = static_cast<u8>(7 - i);
		const u8 low = static_cast<u8>((m_fetch_low >> bit) & 1);
		const u8 high = static_cast<u8>((m_fetch_high >> bit) & 1);
		m_bg_pixels.push_back(BgPixel{static_cast<u8>((high << 1) | low)});
	}
}

void PPU::start_sprite_fetch()
{
	if(m_lx >= 168)
	{
		return;
	}

	if(m_last_group_lx != m_lx)
	{
		m_last_group_lx = m_lx;
		m_obj_group_total = static_cast<u8>(m_oam_entries[m_lx].size());
	}

	m_sprite_entry = m_oam_entries[m_lx][m_oam_served_count[m_lx]];
	++m_oam_served_count[m_lx];

	// DMG OBJ penalty (Pan Docs): each OBJ costs a flat 6 dots. The first OBJ
	// found in a given BG/window tile also waits for the rest of that tile's
	// background fetch: 5 - (pixel X inside the tile) extra dots, clamped to
	// zero. The documented X=0 exception is 11 dots; three of them overlap
	// with the mode-3 startup in this scheduler, so the net cost is 8.
	const int screen_x = static_cast<int>(m_sprite_entry.entry.m_x) - 8;
	const int window_x = static_cast<int>(m_lcd->m_win_x) - 7;
	const bool window_tile = m_window_active && m_lcd->get_lcdc_win_enable() && screen_x >= window_x;

	int tile_key;
	int pixel_in_tile;
	if(window_tile)
	{
		const int x = screen_x - window_x;
		tile_key = 1000 + floor_div8(x);
		pixel_in_tile = floor_mod8(x);
	}
	else
	{
		const int x = screen_x + static_cast<int>(m_lcd->m_scroll_x);
		tile_key = floor_div8(x);
		pixel_in_tile = floor_mod8(x);
	}

	bool tile_considered = false;
	for(int key : m_obj_considered_tiles)
	{
		if(key == tile_key)
		{
			tile_considered = true;
			break;
		}
	}
	if(!tile_considered)
	{
		m_obj_considered_tiles.push_back(tile_key);
	}

	if(m_sprite_entry.entry.m_x == 0 && !tile_considered)
	{
		m_obj_penalty = 8;
	}
	else if(tile_considered)
	{
		m_obj_penalty = 6;
	}
	else
	{
		int extra = 5 - pixel_in_tile;
		if(extra < 0)
		{
			extra = 0;
		}
		m_obj_penalty = static_cast<u8>(6 + extra);

		// When several OBJs share the same X, the first one's sub-tile wait
		// is absorbed by the rest of the group. The group timing matches
		// 10 dots for p=0/1 and the flat 6-dot fetch for p=2..7.
		if(m_obj_group_total > 1)
		{
			if(pixel_in_tile >= 4)
			{
				m_obj_penalty = 6;
			}
			else if(pixel_in_tile >= 2)
			{
				m_obj_penalty = m_obj_multiple_groups ? 7 : 6;
			}
			else if(pixel_in_tile == 0)
			{
				m_obj_penalty = 10;
			}
		}
	}

	m_is_fetching_sprite = true;
	m_obj_fetch_phase = FetchPhase::ObjLow;
	m_sprite_low = 0;
	m_sprite_high = 0;
	m_sprite_data_addr = 0;
}

void PPU::tick_obj_fetch()
{
	if(m_obj_fetch_wait > 0)
	{
		--m_obj_fetch_wait;
		return;
	}

	if(m_obj_fetch_phase == FetchPhase::ObjLow)
	{
		const u8 height = m_lcd->get_lcdc_obj_height();
		u8 tile = m_sprite_entry.entry.m_tile;
		if(height == 16)
		{
			tile &= static_cast<u8>(~1);
		}

		u8 row = static_cast<u8>(m_lcd->m_ly + 16 - m_sprite_entry.entry.m_y);
		if((m_sprite_entry.entry.m_flags & 0x40) != 0)
		{
			row = static_cast<u8>((height - 1) - row);
		}

		m_sprite_data_addr = static_cast<u16>(0x8000 + tile * 16 + row * 2);
		m_sprite_low = m_bus->read(m_sprite_data_addr);
		m_obj_fetch_phase = FetchPhase::ObjHigh;
		m_obj_fetch_wait = 1;
		return;
	}

	if(m_obj_fetch_phase == FetchPhase::ObjHigh)
	{
		m_sprite_high = m_bus->read(m_sprite_data_addr + 1);
		m_obj_fetch_phase = FetchPhase::ObjPush;
		m_obj_fetch_wait = 1;
		return;
	}

	if(m_obj_fetch_phase == FetchPhase::ObjPush)
	{
		push_obj_pixels();
		m_is_fetching_sprite = false;
		m_obj_fetch_phase = FetchPhase::ObjLow;
		// The three fetch accesses take 6 dots; the remaining penalty is the
		// wait for the background fetch (6..11 dots total per OBJ).
		// A lone sprite at X>0 starts after the first BG pixel; the background
		// pipe needs three more dots to align the next tile fetch with the
		// point where SCX is sampled.
		const u8 x_extra = (m_obj_group_total == 1 && m_sprite_entry.entry.m_x != 0) ? 3 : 0;
		m_obj_post_wait = static_cast<u8>(m_obj_penalty - 5 + x_extra);
	}
}

void PPU::push_obj_pixels()
{
	const bool x_flip = (m_sprite_entry.entry.m_flags & 0x20) != 0;
	const u8 palette = static_cast<u8>((m_sprite_entry.entry.m_flags >> 4) & 1);
	const u8 priority = static_cast<u8>((m_sprite_entry.entry.m_flags >> 7) & 1);

	ObjPixel pixels[8];
	for(int i = 0; i < 8; i++)
	{
		const u8 bit = static_cast<u8>(x_flip ? i : 7 - i);
		const u8 low = static_cast<u8>((m_sprite_low >> bit) & 1);
		const u8 high = static_cast<u8>((m_sprite_high >> bit) & 1);
		pixels[i] = ObjPixel{
			static_cast<u8>((high << 1) | low),
			palette,
			priority,
			m_sprite_entry.number,
			m_sprite_entry.entry.m_x};
	}

	const size_t old_size = m_obj_pixels.size();
	for(size_t i = 0; i < 8; i++)
	{
		if(i < old_size)
		{
			ObjPixel& old = m_obj_pixels[i];
			const ObjPixel& pixel = pixels[i];
			const bool overwrite =
				pixel.color_index != 0 &&
				(old.color_index == 0 ||
				 pixel.x < old.x ||
				 (pixel.x == old.x && pixel.number < old.number));

			if(overwrite)
			{
				old = pixel;
			}
		}
		else
		{
			m_obj_pixels.push_back(pixels[i]);
		}
	}
}

void PPU::update_delayed_registers()
{
	// On DMG the PPU sees these registers delayed by 1 T-cycle: this dot uses
	// the snapshot from the previous dot.
	m_last_lcdc = m_lcd->m_lcdc;
	m_last_bgp = m_lcd->m_bg_palette;
	m_last_wx = m_lcd->m_win_x;
	m_last_ly = m_lcd->m_ly;
	m_last_lyc = m_lcd->m_lyc;
}

void PPU::raise_stat_irq()
{
	const bool lyc_eq_ly = m_enable_lyc_eq_ly_irq && (m_last_lyc == m_last_ly);

	const bool lyc_irq = m_lcd->get_lcds_stat_int(StatSRC::SS_LYC) && lyc_eq_ly;
	const bool hblank_irq = m_lcd->get_lcds_stat_int(StatSRC::SS_HBLANK) &&
		m_lcd->get_lcds_mode() == LCDMode::MODE_HBLANK;
	// The DMG OAM-in-VBlank quirk is a one-shot event raised on entering
	// VBlank (see update_stat_irq_for_oam_mode), not a condition that stays
	// true for the whole VBlank period.
	const bool vblank_irq = m_lcd->get_lcds_stat_int(StatSRC::SS_VBLANK) &&
		m_lcd->get_lcds_mode() == LCDMode::MODE_VBLANK;

	const bool irq = lyc_irq || hblank_irq || vblank_irq;

	// Latch the internal STAT IRQ line: while it stays high no new interrupt
	// is requested. It is only cleared on entering mode 3 (see ppu_mode_oam).
	if(irq && !m_stat_irq_line)
	{
		m_stat_irq_line = true;
		m_pending_stat_irq = true;
	}

	if(m_lcd->get_lcds_mode() == LCDMode::MODE_XFER && !irq)
	{
		m_stat_irq_line = false;
	}

	m_last_stat_irq = irq;
}

void PPU::update_stat_irq_for_oam_mode()
{
	const bool lyc_eq_ly_irq = m_lcd->get_lcds_stat_int(StatSRC::SS_LYC) && (m_last_lyc == m_last_ly);
	const bool lyc_eq_next_ly_irq = m_lcd->get_lcds_stat_int(StatSRC::SS_LYC) &&
		(m_last_lyc == static_cast<u8>(m_last_ly + 1));
	const bool irq = m_lcd->get_lcds_stat_int(StatSRC::SS_OAM) || lyc_eq_ly_irq;

	m_pending_stat_irq = m_pending_stat_irq || (m_last_stat_irq < irq);

	if(lyc_eq_next_ly_irq)
	{
		m_last_stat_irq = irq;
	}
}
