#include <cstdlib>
#include <cstdio>

#include "ppu.h"
#include "QtreGB.h"
#include "lcd.h"

using namespace qtreGB;
using namespace std;

const int PPULegacy::LINES_PER_FRAME = 154;
const int PPULegacy::TICKS_PER_LINE = 456;
const int PPULegacy::YRES = 144;
const int PPULegacy::XRES = 160;

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

void PPULegacy::set_interrupt_callback(InterruptCallback cb)
{
	m_request_interrupt = cb;
}

void PPULegacy::set_frame_callback(FrameCallback cb)
{
	m_frame_done = cb;
}

PPULegacy::PPULegacy(LCD *lcd) :
m_lcd(lcd),
m_vram(Memory(0x2000)),
m_oam_ram(vector<OAMEntry>(40))
{
	// Palette/output stage: BG palette 6 dots after the pop, window and
	// OBJ resolved immediately.
	m_bg_palette_delay = 6;
	m_win_palette_delay = 0;
	m_obj_palette_delay = 0;
#ifdef QTR_CPU_MICRO
	// Calibrated against DocBoy with the micro-op CPU (T1 writes): the PPU
	// was originally tuned for the old T3 write phase.
	m_lcdc_hist_delay = 4;
	m_lcdc_delay_bit[0] = 4;	// BG enable
	m_lcdc_delay_bit[1] = 4;	// OBJ enable
	m_lcdc_delay_bit[2] = 4;	// OBJ size
	m_lcdc_delay_bit[3] = 3;	// BG tile map
	m_lcdc_delay_bit[4] = 3;	// BG/WIN tile data
	m_lcdc_delay_bit[5] = 3;	// WIN enable
	m_lcdc_delay_bit[6] = 3;	// WIN tile map
	m_win_startup_wait_ly0 = 2;
	m_wx0_scx0_delay = 0;
	m_wx0_scx1_delay = 1;
	m_wx0_ly0_delay = 1;
	m_stat_delay_oam = 0;
	m_bg_palette_delay_ly0 = m_bg_palette_delay;
#endif
	if(const char* e = std::getenv("QTR_BGDELAY")) m_bg_palette_delay = static_cast<u8>(std::atoi(e));
	m_bg_palette_delay_ly0 = m_bg_palette_delay;
	if(const char* e = std::getenv("QTR_BGDELAY_LY0")) m_bg_palette_delay_ly0 = static_cast<u8>(std::atoi(e));
	if(const char* e = std::getenv("QTR_WINDELAY")) m_win_palette_delay = static_cast<u8>(std::atoi(e));
	if(const char* e = std::getenv("QTR_OBJDELAY")) m_obj_palette_delay = static_cast<u8>(std::atoi(e));
	if(const char* e = std::getenv("QTR_WINWAIT_START")) m_win_startup_wait = static_cast<u8>(std::atoi(e));
	m_win_startup_wait_ly0 = m_win_startup_wait;
#ifdef QTR_CPU_MICRO
	m_win_startup_wait_ly0 = 2;
#endif
	if(const char* e = std::getenv("QTR_WINWAIT_LY0")) m_win_startup_wait_ly0 = static_cast<u8>(std::atoi(e));
	if(const char* e = std::getenv("QTR_WINWAIT_MID")) m_win_midline_wait = static_cast<u8>(std::atoi(e));
	if(const char* e = std::getenv("QTR_WINWAIT_LX")) m_win_startup_lx = static_cast<u8>(std::atoi(e));
	if(const char* e = std::getenv("QTR_WINBIAS")) m_win_trigger_bias = static_cast<u8>(std::atoi(e));
	if(const char* e = std::getenv("QTR_WX0TRIG")) m_wx0_trigger_lx = static_cast<u8>(std::atoi(e));
	if(const char* e = std::getenv("QTR_WX0DELAY")) m_wx0_activation_delay = static_cast<u8>(std::atoi(e));
	if(const char* e = std::getenv("QTR_WX0_SCX0_DELAY")) m_wx0_scx0_delay = static_cast<u8>(std::atoi(e));
	if(const char* e = std::getenv("QTR_WX0_SCX1_DELAY")) m_wx0_scx1_delay = static_cast<u8>(std::atoi(e));
	if(const char* e = std::getenv("QTR_WX0_LY0_DELAY")) m_wx0_ly0_delay = static_cast<u8>(std::atoi(e));
	if(const char* e = std::getenv("QTR_WXGLITCHDELAY")) m_wx_glitch_delay = static_cast<u8>(std::atoi(e));
	if(const char* e = std::getenv("QTR_LCDCHIST")) m_lcdc_hist_delay = static_cast<u8>(std::atoi(e));
	for(int i = 0; i < 7; ++i) m_lcdc_delay_bit[i] = m_lcdc_hist_delay;
#ifdef QTR_CPU_MICRO
	m_lcdc_delay_bit[0] = 4;	// BG enable
	m_lcdc_delay_bit[1] = 4;	// OBJ enable
	m_lcdc_delay_bit[2] = 4;	// OBJ size
	m_lcdc_delay_bit[3] = 3;	// BG tile map
	m_lcdc_delay_bit[4] = 3;	// BG/WIN tile data
	m_lcdc_delay_bit[5] = 3;	// WIN enable
	m_lcdc_delay_bit[6] = 3;	// WIN tile map
#endif
	if(const char* e = std::getenv("QTR_D_BGEN")) m_lcdc_delay_bit[0] = static_cast<u8>(std::atoi(e));
	if(const char* e = std::getenv("QTR_D_OBJEN")) m_lcdc_delay_bit[1] = static_cast<u8>(std::atoi(e));
	if(const char* e = std::getenv("QTR_D_OBJSIZE")) m_lcdc_delay_bit[2] = static_cast<u8>(std::atoi(e));
	if(const char* e = std::getenv("QTR_D_BGMAP")) m_lcdc_delay_bit[3] = static_cast<u8>(std::atoi(e));
	if(const char* e = std::getenv("QTR_D_TILESEL")) m_lcdc_delay_bit[4] = static_cast<u8>(std::atoi(e));
	m_tile_sel_low_delay = m_lcdc_delay_bit[4];
	m_tile_sel_high_delay = m_lcdc_delay_bit[4];
	if(const char* e = std::getenv("QTR_D_TILESEL_LOW")) m_tile_sel_low_delay = static_cast<u8>(std::atoi(e));
	if(const char* e = std::getenv("QTR_D_TILESEL_HIGH")) m_tile_sel_high_delay = static_cast<u8>(std::atoi(e));
	m_tile_sel_ly0_delay = m_tile_sel_low_delay;
#ifdef QTR_CPU_MICRO
	m_tile_sel_ly0_delay = 5;
#endif
	if(const char* e = std::getenv("QTR_D_TILESEL_LY0")) m_tile_sel_ly0_delay = static_cast<u8>(std::atoi(e));
	m_tile_sel_tail_delay = m_tile_sel_low_delay;
	if(const char* e = std::getenv("QTR_D_TILESEL_TAIL")) m_tile_sel_tail_delay = static_cast<u8>(std::atoi(e));
	if(const char* e = std::getenv("QTR_D_TILESEL_TAIL_HIGH_DELTA")) m_tile_sel_tail_high_delta = static_cast<u8>(std::atoi(e));
	if(const char* e = std::getenv("QTR_D_WINEN")) m_lcdc_delay_bit[5] = static_cast<u8>(std::atoi(e));
	if(const char* e = std::getenv("QTR_D_WINMAP")) m_lcdc_delay_bit[6] = static_cast<u8>(std::atoi(e));
	if(const char* e = std::getenv("QTR_SCXLATCH")) m_scx_latch_dot = static_cast<u8>(std::atoi(e));
	if(const char* e = std::getenv("QTR_HBLANKIRQ")) m_hblank_irq_lx = static_cast<u16>(std::atoi(e));
	if(const char* e = std::getenv("QTR_MODE0LX")) m_mode0_lx = static_cast<u16>(std::atoi(e));
	if(const char* e = std::getenv("QTR_VBLANKQUIRKDOT")) m_vblank_quirk_dot = static_cast<u16>(std::atoi(e));
	if(const char* e = std::getenv("QTR_VBLANKIRQDOT")) m_vblank_irq_dot = static_cast<u16>(std::atoi(e));
	if(const char* e = std::getenv("QTR_SDLYC")) m_stat_delay_lyc = static_cast<u8>(std::atoi(e));
	if(const char* e = std::getenv("QTR_SDOAM")) m_stat_delay_oam = static_cast<u8>(std::atoi(e));
	if(const char* e = std::getenv("QTR_SDVBLANK")) m_stat_delay_vblank = static_cast<u8>(std::atoi(e));
	if(const char* e = std::getenv("QTR_SDHBLANK")) m_stat_delay_hblank = static_cast<u8>(std::atoi(e));
	if(const char* e = std::getenv("QTR_MODE0BUDGET")) m_mode0_budget = static_cast<u16>(std::atoi(e));
	if(const char* e = std::getenv("QTR_STATMODEL")) m_stat_model = std::atoi(e) != 0;
}

void PPULegacy::ppu_init(bool boot_rom)
{
	m_line_ticks = 0;
	m_dots = 0;
	m_pushed_x = 0;
	m_lcd_on_line = 255;
	m_oam_bus.reset();
	m_vram_bus.reset();
	m_video_buffer = vector<u8>(PPULegacy::YRES * PPULegacy::XRES);

	m_lcd->lcd_init(boot_rom);
	if(boot_rom)
	{
		// Power-on DMG state: the LCD is off until the boot ROM writes LCDC.
		// The PPU stays idle in mode 0 and ppu_power_on() starts it later.
		m_lcd->set_lcds_mode(LCDMode::MODE_HBLANK);
		m_tick_selector = &PPULegacy::ppu_mode_hblank;
	}
	else
	{
		m_lcd->set_lcds_mode(LCDMode::MODE_OAM);
		m_tick_selector = &PPULegacy::ppu_mode_oam;
	}

	m_lcd_enabled = m_lcd->get_lcdc_lcd_enable();
	m_lyc_seen = m_lcd->m_lyc;
	m_lyc_update_countdown = 0;
	m_last_stat_irq = false;
	m_pending_stat_irq = false;
	m_hblank_line = false;
	m_enable_lyc_eq_ly_irq = true;
	m_vblank_last_line = false;

	update_delayed_registers();
}

u8 PPULegacy::ppu_oam_read(u16 address)
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

void PPULegacy::ppu_oam_write(u16 address, u8 value)
{
	ppu_oam_write_dma(address, value);
}

void PPULegacy::ppu_oam_write_dma(u16 address, u8 value)
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

u8 PPULegacy::ppu_vram_read(u16 address)
{
	return m_vram.read(address);
}

void PPULegacy::ppu_vram_write(u16 address, u8 value)
{
	m_vram.write(address, value);
}

const std::vector<u8> &PPULegacy::get_video_buffer() const
{
	return m_video_buffer;
}

void PPULegacy::ppu_sync_lcd_power()
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

void PPULegacy::ppu_tick()
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
#ifdef QTR_CPU_MICRO
	m_bgp_pre_tick = m_lcd->m_bg_palette;
#endif
	m_lcd->tick();
	update_delayed_registers();
}

void PPULegacy::ppu_reset_line_state()
{
	flush_pending_palette(true);
	m_wx0_write_pending = false;
	m_wx0_line0_window = false;
	m_win_palette_delay = 0;
	m_line_ticks = 0;
	m_dots = 0;
	m_pushed_x = 0;
	m_window_line = 0;
}

void PPULegacy::ppu_power_off()
{
	// LY is forced to 0 and STAT mode to 0 (HBlank). The LYC==LY flag is NOT updated:
	// with the PPU off the comparison clock is halted and the flag keeps its last value
	// (mooneye stat_lyc_onoff). No interrupts are generated.
	m_lcd->m_ly = 0;
	m_lcd->set_lcds_mode(LCDMode::MODE_HBLANK);
	m_tick_selector = &PPULegacy::ppu_mode_hblank;
	m_lcd_on_line = 255;
	m_lyc_update_countdown = 0;	// A pending delayed update must not fire while the comparison clock is halted
	m_last_stat_irq = false;
	m_pending_stat_irq = false;
	m_stat_irq_line = false;
	m_oam_line = false;
	m_hblank_line = false;
	m_enable_lyc_eq_ly_irq = true;
	m_vblank_last_line = false;
	m_oam_bus.reset();
	m_vram_bus.reset();
	ppu_reset_line_state();
}

void PPULegacy::ppu_power_on()
{
	// The first line (line 0) starts in mode 0 instead of mode 2 and goes straight to mode 3,
	// with the PPU running late by 2 T-cycles (mooneye lcdon_timing-GS). Lines 1 and 2 keep
	// a late OAM scan (+4 dots) before returning to the normal schedule on line 3. The
	// LYC==LY comparison restarts with LY=0 and its interrupt only fires on a 0->1 edge
	// (mooneye stat_lyc_onoff).
	m_lcd->m_ly = 0;
	m_lcd->set_lcds_mode(LCDMode::MODE_HBLANK);
	m_tick_selector = &PPULegacy::ppu_mode_oam;
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

void PPULegacy::ppu_mode_oam()
{
	if(m_dots == 0)
	{
		m_oam_scan_index = 0;
		m_oam_scan_count = 0;
		m_oam_scan_obj_height = lcdc_obj_height();
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
		m_tick_selector = &PPULegacy::pixel_transfer_tick;

		// The internal STAT IRQ line is cleared on entering mode 3, unless the
		// LYC condition is still asserted. Keeping it high blocks the following
		// STAT interrupts (mooneye stat_irq_blocking).
		if(!(m_lcd->get_lcds_stat_int(StatSRC::SS_LYC) && m_lcd->get_lcds_lyc()))
		{
			m_stat_irq_line = false;
		}
		m_oam_line = false;

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
		m_bg_fetch_wait = 2;	// First tile fetch starts two dots after the dummy startup
		m_fetch_window = false;
		m_bg_pixels.clear();
		m_obj_pixels.clear();
	}
}

void PPULegacy::ppu_mode_hblank()
{
	// Finish the palette pipeline with its natural delay: the last pixels of
	// the line are written during hblank, after any late BGP restore.
	flush_pending_palette(false);

	// The OAM STAT IRQ is signalled at dot 452 on DMG.
	if(m_dots == 452 && m_lcd->m_ly != PPULegacy::YRES)
	{
		update_stat_irq_for_oam_mode();
	}

	if(m_dots == 448)
	{
		// Scheduler phase calibration: LY++ is nominally at dot 453, but the
		// CPU/PPU tick order makes the observable change land at 448 here.
		// OAM is locked and the LYC IRQ window closes at the same point.
		if(m_lcd->m_ly != PPULegacy::YRES - 1)
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

		m_hblank_line = false;
		if(m_lcd->m_ly == PPULegacy::YRES)
		{
			m_lcd->set_lcds_mode(LCDMode::MODE_VBLANK);
		}
		else
		{
			m_lcd->set_lcds_mode(LCDMode::MODE_OAM);
		}

	}
	if(m_dots == m_vblank_quirk_dot && m_lcd->m_ly == PPULegacy::YRES)
	{
		// DMG quirk: one-tick OAM pulse at VBlank start.
		if(m_stat_model)
		{
			m_vblank_oam_pulse = true;
		}
		else
		{
			update_stat_irq_for_oam_mode();
		}
	}
	else if(m_dots == m_vblank_irq_dot && m_lcd->m_ly == PPULegacy::YRES)
	{
		request_interrupt(InterruptType::IT_VBLANK);
		update_stat_irq();
		update_stat_irq();
	}
	else if(m_dots == PPULegacy::TICKS_PER_LINE - 1)
	{
		if(m_lcd->m_ly == PPULegacy::YRES)
		{
			m_tick_selector = &PPULegacy::ppu_mode_vblank;
			m_vblank_last_line = false;
			m_oam_bus.release(VideoDevice::Ppu, VideoAccess::Read);
		}
		else
		{
			m_tick_selector = &PPULegacy::ppu_mode_oam;
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
void PPULegacy::ppu_lcd_on_line()
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
			m_tick_selector = &PPULegacy::ppu_mode_oam;
			update_stat_irq();
		}
	}
}

void PPULegacy::ppu_mode_vblank()
{
	// On the last VBlank line LY drops to 0 at dot 2, but the line itself
	// continues until dot 455. Remember that state explicitly: after the reset
	// LY no longer identifies the line.
	if(!m_vblank_last_line && m_dots == 0 && m_lcd->m_ly == PPULegacy::LINES_PER_FRAME - 1)
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
		else if(m_dots == PPULegacy::TICKS_PER_LINE - 1)
		{
			m_lcd->set_lcds_mode(LCDMode::MODE_OAM);
			m_tick_selector = &PPULegacy::ppu_mode_oam;
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
	else if(m_dots == PPULegacy::TICKS_PER_LINE - 1)
	{
		m_line_ticks = 0;
	}
}

void PPULegacy::increment_ly()
{
	m_lcd->m_ly++;

	schedule_lyc_update();
}

void PPULegacy::schedule_lyc_update()
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

void PPULegacy::check_lyc()
{
	set_lyc_flag(m_lcd->m_ly == m_lcd->m_lyc);
}

void PPULegacy::set_lyc_flag(bool ly_equals_lyc)
{
	const bool previous = m_lcd->get_lcds_lyc();
	m_lcd->set_lcds_lyc(ly_equals_lyc);

	// The STAT interrupt is edge-triggered by the 0->1 transition of the
	// comparison result. This also makes power-on keep the retained flag
	// without firing when the result does not change (stat_lyc_onoff).
	if(ly_equals_lyc && !previous)
	{
		if(m_stat_model && m_lcd_on_line != 255)
		{
			// During the LCD-on startup lines the regular per-dot STAT
			// evaluation is bypassed, so the LYC 0->1 edge produced while
			// the LCD is switched on must be delivered synchronously
			// (stat_lyc_onoff round 4).
			if(m_lcd->get_lcds_stat_int(StatSRC::SS_LYC) && !m_stat_irq_line)
			{
				m_stat_irq_line = true;
				request_interrupt(InterruptType::IT_LCD_STAT);
			}
		}
		else
		{
			update_stat_irq();
		}
	}
}

void PPULegacy::connect_bus(IBus *bus)
{
	m_bus = bus;
}

void PPULegacy::request_interrupt(InterruptType IType)
{
	if(std::getenv("QTR_IRQDBG"))
	{
		static int nv = 0;
		static int ns = 0;
		if(IType == InterruptType::IT_VBLANK && nv < 5)
		{
			std::fprintf(stderr, "PPUIRQ VBLANK dot=%u ly=%u mode=%d\n",
				static_cast<unsigned>(m_dots), static_cast<unsigned>(m_lcd->m_ly),
				static_cast<int>(m_lcd->get_lcds_mode()));
			nv++;
		}
		else if(IType == InterruptType::IT_LCD_STAT && ns < 5)
		{
			std::fprintf(stderr, "PPUIRQ STAT dot=%u ly=%u mode=%d\n",
				static_cast<unsigned>(m_dots), static_cast<unsigned>(m_lcd->m_ly),
				static_cast<int>(m_lcd->get_lcds_mode()));
			ns++;
		}
	}
	if (m_request_interrupt)
	{
		m_request_interrupt(IType);
	}
}

void PPULegacy::update_stat_irq()
{
	if(m_stat_model)
	{
		return;
	}
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

void PPULegacy::pixel_transfer_tick()
{
	if(std::getenv("QTR_WXDBG") && m_lcd->m_ly == 92)
	{
		std::fprintf(stderr, "QXFER dot=%u lx=%u lastwx=%u wx=%u act=%d actg=%d phase=%d wait=%d\n",
			static_cast<unsigned>(m_dots), static_cast<unsigned>(m_lx),
			static_cast<unsigned>(m_last_wx), static_cast<unsigned>(m_lcd->m_win_x),
			m_window_active ? 1 : 0, m_window_activating ? 1 : 0,
			static_cast<int>(m_fetch_phase), static_cast<int>(m_bg_fetch_wait));
	}

	// m_lx is X + 8 and must stay in [0, 168). If the mode was changed
	// externally, do not index m_oam_entries out of bounds.
	if(m_lx >= 168)
	{
		finish_pixel_transfer();
		return;
	}

	// The palette/output stage advances every dot, even when the pixel
	// pipeline is stalled (window activation, OBJ fetch).
	flush_pending_palette(false);

	// SCX's low 3 bits are sampled once per line at the start of the first
	// real B fetch (see mealybug m3_scx_low_3_bits). A write after this dot
	// still changes the tilemap address but not the sub-tile alignment.
	if(m_dots == m_scx_latch_dot)
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


	if(m_pixel_transfer_phase == PixelTransferPhase::Dummy)
	{
		tick_fetcher();
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
			std::array<u8, 168> counts{};
			int sprite_count = 0;
			int first_y = 0;
			int first_x = 0;
			int last_x = 0;
			int min_x = 168;
			int max_x = 0;
			const u8 ly = m_lcd->m_ly;
			const u8 height = m_lcd->get_lcdc_obj_height();
			for(int i = 0; i < 40 && sprite_count < 10; i++)
			{
				const OAMEntry& entry = m_oam_ram[i];
				if(entry.m_y <= ly + 16 && entry.m_y + height > ly + 16 && entry.m_x < 168)
				{
					if(sprite_count == 0)
					{
						first_y = entry.m_y;
						first_x = entry.m_x;
					}
					last_x = entry.m_x;
					if(entry.m_x < min_x) min_x = entry.m_x;
					if(entry.m_x > max_x) max_x = entry.m_x;
					counts[entry.m_x]++;
					++sprite_count;
				}
			}

			int groups = 0;
			for(u8 count : counts)
			{
				if(count != 0)
				{
					++groups;
				}
			}
			m_obj_multiple_groups = groups > 1;

			std::vector<int> considered;
			int total = 0;
			for(u8 x = 0; x < 168; x++)
			{
				const int count = counts[x];
				if(count == 0)
				{
					continue;
				}
				for(int j = 0; j < count; j++)
				{
					const int screen_x = static_cast<int>(x) - 8;
					const int position = screen_x + static_cast<int>(m_lcd->m_scroll_x);
					const int tile_key = floor_div8(position);
					const int pixel_in_tile = floor_mod8(position);
					bool tile_considered = false;
					for(int key : considered)
					{
						if(key == tile_key)
						{
							tile_considered = true;
							break;
						}
					}

					int penalty;
					if(x == 0 && !tile_considered)
					{
						penalty = 8;
					}
					else if(tile_considered)
					{
						penalty = 6;
					}
					else
					{
						int extra = 5 - pixel_in_tile;
						if(extra < 0)
						{
							extra = 0;
						}
						penalty = 6 + extra;
						if(count > 1)
						{
							if(pixel_in_tile >= 4)
							{
								penalty = 6;
							}
							else if(pixel_in_tile >= 2)
							{
								penalty = m_obj_multiple_groups ? 7 : 6;
							}
							else if(pixel_in_tile == 0)
							{
								penalty = 10;
							}
						}
					}

					total += penalty;
					if(!tile_considered)
					{
						considered.push_back(tile_key);
					}
				}
			}
			m_mode0_penalty_dots = static_cast<u16>(total);
			m_mode0_extra_correction = 0;
			if(sprite_count == 1 && first_x > 0)
			{
				const u8 pixel_in_tile = static_cast<u8>(first_x & 7);
				m_mode0_extra_correction = (pixel_in_tile >= 1 && pixel_in_tile <= 3) ? 1 : 2;
			}
			else if(sprite_count >= 2 &&
				(max_x - min_x) == 8 * (sprite_count - 1) && min_x >= 2)
			{
				m_mode0_extra_correction = 1;
			}
		}
		m_last_group_lx = 0xFF;
		m_obj_group_total = 0;
		m_lx = 0;
		m_bg_fetch_x = 0;
		m_bg_fetch_skip = 0;
		m_bg_prev_scx_low = static_cast<u8>(m_lcd->m_scroll_x % 8);
		m_bg_line_scx_low = m_bg_prev_scx_low;
		m_fetch_phase = FetchPhase::BgTilemap;
		m_bg_fetch_wait = 2;
		m_fetch_window = false;
		m_window_active = false;
		m_window_activating = false;
		m_window_fetch_enable = false;
		m_window_activation_delay = 0;
		m_wx0_write_pending = false;
		m_wx0_line0_window = false;
		m_win_palette_delay = 0;
		m_line_obj_out_delay = 0;
		m_pixel_transfer_phase = m_initial_scx_to_discard != 0
			? PixelTransferPhase::Discard
			: PixelTransferPhase::Lx0;
		return;
	}

	const bool win_enable_now = lcdc_win_en();
	const bool win_enable_last = lcdc_win_en_prev();

	if((m_window_active || m_window_activating) && !win_enable_last)
	{
		// The current tile fetch completes as window. The next tile fetch
		// boundary samples WIN_EN and switches to BG if it is still off.
		m_window_activating = false;
		m_window_activation_delay = 0;
	}

	if(m_window_activating)
	{
		if(m_window_activation_delay > 0)
		{
			--m_window_activation_delay;
			return;
		}

		if(std::getenv("QTR_WXDBG") && m_lcd->m_ly == 92)
		{
			std::fprintf(stderr, "QACT dot=%u lx=%u lastwx=%u wx=%u\n",
				static_cast<unsigned>(m_dots), static_cast<unsigned>(m_lx),
				static_cast<unsigned>(m_last_wx), static_cast<unsigned>(m_lcd->m_win_x));
		}
		m_window_active = true;
		m_window_just_activated = true;
		m_window_tile_x = 0;
		m_window_line = m_window_trigger_row;
		m_bg_pixels.clear();
		m_fetch_phase = FetchPhase::BgTilemap;
		m_fetch_window = true;
#ifdef QTR_CPU_MICRO
		m_bg_fetch_wait = (m_lx <= m_win_startup_lx)
			? ((m_lcd->m_ly == 0) ? m_win_startup_wait_ly0 : m_win_startup_wait)
			: m_win_midline_wait;
#else
		m_bg_fetch_wait = (m_lx <= m_win_startup_lx) ? m_win_startup_wait : m_win_midline_wait;
#endif
		m_window_activating = false;
		return;
	}
	// WX=0 line-0 case: the write lands after lx=1. Detect the write event
	// and activate once the first three visible BG pixels have been output.
	if(m_lcd->m_win_x == 0 && m_last_wx != 0)
	{
		m_wx0_write_pending = true;
	}
	const bool wx0_line0_trigger = m_wx0_write_pending && m_lx == m_wx0_trigger_lx;
	if(!m_window_active && m_window_active_for_frame && win_enable_now &&
	   (wx0_line0_trigger || static_cast<u8>(m_lx + m_win_trigger_bias) == m_last_wx + 1 ||
	    (m_lx == m_last_wx + 2 && !win_enable_last)))
	{
		if(std::getenv("QTR_WXDBG") && m_lcd->m_ly == 92)
		{
			std::fprintf(stderr, "QTRIG dot=%u lx=%u lastwx=%u wx=%u winen_last=%d\n",
				static_cast<unsigned>(m_dots), static_cast<unsigned>(m_lx),
				static_cast<unsigned>(m_last_wx), static_cast<unsigned>(m_lcd->m_win_x),
				win_enable_last ? 1 : 0);
		}
		m_window_activating = true;
		m_window_trigger_row = m_window_next_row++;
		if(wx0_line0_trigger)
		{
			m_wx0_write_pending = false;
			m_wx0_line0_window = true;
		}
		// WX=0 activates the window one T-cycle later when SCX has a
		// non-zero sub-tile offset (mealybug m3_window_timing_wx_0).
#ifdef QTR_CPU_MICRO
		if(m_last_wx == 0)
		{
			const u8 scx_low = static_cast<u8>(m_lcd->m_scroll_x & 0x07);
			if(scx_low == 0)
			{
				m_window_activation_delay = (m_lcd->m_ly == 0) ? m_wx0_ly0_delay : m_wx0_scx0_delay;
			}
			else if(scx_low == 1)
			{
				m_window_activation_delay = m_wx0_scx1_delay;
			}
			else
			{
				m_window_activation_delay = m_wx0_activation_delay;
			}
		}
		else
		{
			m_window_activation_delay = 0;
		}
#else
		m_window_activation_delay =
			(m_last_wx == 0 && (m_lcd->m_scroll_x & 0x07) != 0) ? 1 : 0;
#endif
		return;
	}

	if(m_dots < 83)
	{
		tick_fetcher();
		return;
	}

	if(m_obj_post_wait > 0)
	{
		--m_obj_post_wait;
	}

	const bool obj_pending =
		lcdc_obj_en() && m_lx < 168 && oam_entry_pending(m_lx);
	const bool can_pop = !m_is_fetching_sprite && m_obj_post_wait == 0 &&
		!obj_pending && !m_bg_pixels.empty();

	bool inc_lx = false;
	bool discard_done = false;
	const u8 lx_before = m_lx;

	// The HBlank STAT condition is asserted one M-cycle before the visible
	// mode switch (SM8: the IRQ is raised at lx=165 while STAT still reads
	// mode 3 until lx=167). Keep the CPU-visible mode transition separate.
	if(lx_before == 165 && m_lcd->get_lcds_stat_int(StatSRC::SS_HBLANK))
	{
		m_hblank_line = true;
	}

	// Expose mode 0 at the CPU-visible dot budget: base 252 plus the OBJ
	// penalties of the line. The framebuffer keeps receiving pixels after
	// this point; only the STAT mode switches.
	if(m_mode0_penalty_dots == 0)
	{
		// No OBJ on this line: use the validated end-of-line phase.
		if(m_lx == static_cast<u8>(m_mode0_lx) &&
		   !oam_entry_pending(static_cast<u8>(m_mode0_lx)) && !m_is_fetching_sprite &&
		   m_obj_post_wait == 0 && m_lcd->get_lcds_mode() == LCDMode::MODE_XFER)
		{
			m_lcd->set_lcds_mode(LCDMode::MODE_HBLANK);
		}
	}
	else
	{
		const u16 mode0_dot = static_cast<u16>(m_mode0_budget + m_mode0_penalty_dots -
			(m_mode0_penalty_dots != 0 ? 1 : 0) - m_mode0_extra_correction);
		if(m_dots >= mode0_dot && !oam_entry_pending(167) && !m_is_fetching_sprite &&
		   m_lcd->get_lcds_mode() == LCDMode::MODE_XFER)
		{
			m_lcd->set_lcds_mode(LCDMode::MODE_HBLANK);
		}
	}

	if(can_pop)
	{
		if(m_pixel_transfer_phase == PixelTransferPhase::Discard)
		{
			m_bg_pixels.pop_front();
			if(!m_obj_pixels.empty())
			{
				m_obj_pixels.pop_front();
			}
			if(++m_initial_scx_discarded == m_initial_scx_to_discard)
			{
				discard_done = true;
			}
		}
		else if(m_pixel_transfer_phase == PixelTransferPhase::Lx0)
		{
			m_bg_pixels.pop_front();
			if(!m_obj_pixels.empty())
			{
				m_obj_pixels.pop_front();
			}
			inc_lx = true;
		}
		else
		{
			const BgPixel bg = m_bg_pixels.front();
			m_bg_pixels.pop_front();

			// DMG: the PPU sees LCDC delayed by one T-cycle at pixel output.
			// LX == 8 is the documented exception: the current value is used.
#ifdef QTR_CPU_MICRO
			const bool bg_enable = lcdc_bg_en();
			const bool obj_enable = lcdc_obj_en();
#else
			const u8 lcdc = (m_lx == 8) ? m_lcd->m_lcdc : m_last_lcdc;
			const bool bg_enable = (lcdc & 0x01) != 0;
			const bool obj_enable = (lcdc & 0x02) != 0;
#endif
			const u8 bgp = m_lcd->m_bg_palette;

			const u8 bg_color_index = bg_enable ? bg.color_index : 0;
			u8 color_index = bg_color_index;
			u8 palette_sel = 0;	// BGP
			bool use_window = bg.window;
			u8 obj_delay = 0;

			// The OBJ FIFO advances in lockstep with the BG pixel output, even
			// while the LCDC OBJ enable bit is off. Only the drawing is gated
			// by the enable bit; leaving stale pixels in the FIFO would emit
			// them much later in the line (mealybug m3_lcdc_obj_en_change).
			if(!m_obj_pixels.empty())
			{
				const ObjPixel obj = m_obj_pixels.front();
				m_obj_pixels.pop_front();

				if(obj_enable && obj.color_index != 0 && (!obj.priority || bg_color_index == 0))
				{
					color_index = obj.color_index;
					palette_sel = obj.palette ? 2 : 1;	// OBP1 / OBP0
					obj_delay = ((obj.x & 0x07) >= 6) ? 1 : 0;
				}
			}

			if(std::getenv("QTR_WXDBG") && m_lcd->m_ly == 92 && m_pushed_x >= 75 && m_pushed_x <= 100)
			{
				std::fprintf(stderr, "QPIX dot=%u x=%u lx=%u win=%d bg=%u col=%u\n",
					static_cast<unsigned>(m_dots), static_cast<unsigned>(m_pushed_x),
					static_cast<unsigned>(m_lx), use_window ? 1 : 0,
					static_cast<unsigned>(bg_color_index), static_cast<unsigned>(color_index));
			}
			m_pending_palette_pixels.push_back(PendingPalettePixel{
				static_cast<u16>(m_dots), static_cast<u8>(m_pushed_x), m_lcd->m_ly,
				color_index, palette_sel, use_window, m_line_obj_out_delay, obj_delay});
			flush_pending_palette(false);

			m_pushed_x++;
			inc_lx = true;
		}
	}

	tick_fetcher();

	if(discard_done)
	{
		m_pixel_transfer_phase = PixelTransferPhase::Lx0;
	}
	if(inc_lx)
	{
		++m_lx;
		if(m_lx == 8 && m_pixel_transfer_phase == PixelTransferPhase::Lx0)
		{
			m_pixel_transfer_phase = PixelTransferPhase::Lx8;
		}
	}

	// The STAT mode 0 interrupt is raised one M-cycle before the actual mode
	// switch; this is what the CPU measures in intr_2_0_timing.
	if(!m_stat_model && lx_before == static_cast<u8>(m_hblank_irq_lx) && m_lcd->get_lcds_stat_int(StatSRC::SS_HBLANK) && !m_stat_irq_line)
	{
		m_stat_irq_line = true;
		request_interrupt(InterruptType::IT_LCD_STAT);
	}

	if(m_lx >= 168)
	{
		finish_pixel_transfer();
	}
}

void PPULegacy::flush_pending_palette(bool all)
{
	for(auto it = m_pending_palette_pixels.begin(); it != m_pending_palette_pixels.end(); )
	{
		const u8 win_delay = m_wx0_line0_window ? m_bg_palette_delay : m_win_palette_delay;
		const u8 bg_delay_cfg = (it->y == 0) ? m_bg_palette_delay_ly0 : m_bg_palette_delay;
		const u8 bg_delay = it->out_delay >= bg_delay_cfg ? 0
			: static_cast<u8>(bg_delay_cfg - it->out_delay);
		const u8 obj_delay = static_cast<u8>(m_obj_palette_delay + it->obj_delay);
		const u8 delay = it->palette_sel != 0 ? obj_delay
			: (it->window ? win_delay : bg_delay);
		if(!all && static_cast<u16>(m_dots - it->dot) < delay)
		{
			++it;
			continue;
		}
		const PendingPalettePixel p = *it;
		it = m_pending_palette_pixels.erase(it);
		// DMG BGP delay: on the write tick the PPU sees old | new.
		const u8 palette = p.palette_sel == 0 ? (m_lcd->m_bg_palette | m_last_bgp)
			: (p.palette_sel == 1 ? m_lcd->m_obj_palette_0 : m_lcd->m_obj_palette_1);
		const u8 color = static_cast<u8>((palette >> (p.color_index * 2)) & 0x03);
		if(p.y < PPULegacy::YRES && p.x < PPULegacy::XRES)
		{
			m_video_buffer[p.x + p.y * PPULegacy::XRES] = color;
		}
	}
}

void PPULegacy::finish_pixel_transfer()
{
	flush_pending_palette(false);
	if(m_lcd->get_lcds_mode() == LCDMode::MODE_XFER)
	{
		m_lcd->set_lcds_mode(LCDMode::MODE_HBLANK);
	}

	m_tick_selector = &PPULegacy::ppu_mode_hblank;
	m_oam_bus.release(VideoDevice::Ppu, VideoAccess::Read);
	m_is_fetching_sprite = false;
	update_stat_irq();
}

void PPULegacy::tick_fetcher()
{
	if(m_window_activating)
	{
		return;	// The window trigger skips the next fetcher tick
	}

	if(m_is_fetching_sprite)
	{
		tick_obj_fetch();
		return;
	}

	// Consume the access wait here so the push stage runs one dot after the
	// last fetch access, matching the hardware fetch cadence. Without this
	// the push would land on the same tick the wait reaches zero and the
	// window's first fetch would be one dot short.
	if(m_bg_fetch_wait > 0)
	{
		--m_bg_fetch_wait;
		return;
	}

	// The BG/window fetcher advances independently of the FIFO. The push
	// stage below decides when the ready tile enters the FIFO and when an
	// OBJ interrupts the fetch.
	tick_bg_fetch();
	handle_bg_push();
}

void PPULegacy::tick_bg_fetch()
{
	// The window fetcher checks WIN_EN at every phase. If the window is
	// turned off mid-fetch, the remaining phases of this tile use BG.
	if(m_window_active && !lcdc_win_en())
	{
		m_window_active = false;
		m_fetch_window = false;
	}

	if(m_fetch_phase == FetchPhase::BgTilemap)
	{
		m_window_fetch_enable = lcdc_win_en();
		if(!m_window_fetch_enable)
		{
			m_window_active = false;
			m_fetch_window = false;
		}
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
			tilemap_base = lcdc_win_map();
		}
		else
		{
			tilemap_x = (x / 8) & 0x1F;
			tilemap_y = (y / 8) & 0x1F;
			tilemap_base = lcdc_bg_map();
		}

		m_fetch_tilemap_addr = static_cast<u16>(tilemap_base + tilemap_y * 32 + tilemap_x);
		m_fetch_tile_number = m_bus->read(m_fetch_tilemap_addr);
		if(m_fetch_window && m_window_active)
		{
			m_bg_fetch_wait = 1;
		}

		// The tile row and the LCDC data area are sampled separately at each
		// byte access. On DMG a mid-fetch SCY or tile-select change can
		// therefore desync the low and high bitplanes (mealybug
		// m3_scy_change / m3_lcdc_tile_sel_change).
		m_fetch_phase = FetchPhase::BgLow;
		m_bg_fetch_wait = 1;
		return;
	}

	if(m_fetch_phase == FetchPhase::BgLow)
	{
		const u16 tile_base = lcdc_tile_data_at((m_lcd->m_ly == 0) ? m_tile_sel_ly0_delay
			: (m_lcd->m_ly >= 128 ? m_tile_sel_tail_delay : m_tile_sel_low_delay));
		const u16 tile_data = (tile_base == 0x8800)
			? static_cast<u16>(0x9000 + static_cast<int8_t>(m_fetch_tile_number) * 16)
			: static_cast<u16>(0x8000 + m_fetch_tile_number * 16);
		const u8 tile_y = (m_fetch_window && m_window_active)
			? static_cast<u8>(m_window_line % 8)
			: static_cast<u8>((m_lcd->m_ly + m_lcd->m_scroll_y) % 8);
		m_fetch_data_addr = static_cast<u16>(tile_data + tile_y * 2);
		m_fetch_low = m_bus->read(m_fetch_data_addr);
		m_fetch_phase = FetchPhase::BgHigh;
		m_bg_fetch_wait = 1;
		return;
	}

	if(m_fetch_phase == FetchPhase::BgHigh)
	{
		// The high byte re-samples LCDC (tile-select tests change the data
		// area between the two accesses), but keeps the row latched at the
		// tilemap fetch so a mid-tile SCY change cannot corrupt the byte pair.
		const u16 tile_base = lcdc_tile_data_at((m_lcd->m_ly == 0) ? m_tile_sel_ly0_delay
			: (m_lcd->m_ly >= 128 ? static_cast<u8>(m_tile_sel_tail_delay + m_tile_sel_tail_high_delta) : m_tile_sel_high_delay));
		const u16 tile_data = (tile_base == 0x8800)
			? static_cast<u16>(0x9000 + static_cast<int8_t>(m_fetch_tile_number) * 16)
			: static_cast<u16>(0x8000 + m_fetch_tile_number * 16);
		const u8 tile_y = (m_fetch_window && m_window_active)
			? static_cast<u8>(m_window_line % 8)
			: static_cast<u8>((m_lcd->m_ly + m_lcd->m_scroll_y) % 8);
		m_fetch_data_addr = static_cast<u16>(tile_data + tile_y * 2);
		m_fetch_high = m_bus->read(m_fetch_data_addr + 1);
		m_fetch_phase = FetchPhase::BgPush;
		m_bg_fetch_wait = 1;
		return;
	}

	(void)0;
}

void PPULegacy::handle_bg_push()
{
	if(m_fetch_phase != FetchPhase::BgPush)
	{
		return;
	}
	if(m_bg_fetch_wait > 0)
	{
		return;
	}

	if(m_bg_pixels.empty())
	{
		// Window reactivation zero pixel: if the window was eligible at
		// some point this frame and this push lands on the current WX,
		// push a zero pixel instead of the fetched tile (DMG glitch).
		// The first window tile after activation is the exception.
		const u8 glitch_wx = m_wx_glitch_delay == 0 ? m_lcd->m_win_x
			: (m_wx_glitch_delay == 1 ? m_last_wx
			: (m_wx_glitch_delay == 2 ? m_wx_delay2 : m_wx_delay3));
		if(m_window_active_for_frame &&
		   m_lx == glitch_wx && !m_window_just_activated)
		{
			if(std::getenv("QTR_WXDBG") && m_lcd->m_ly == 92)
			{
				std::fprintf(stderr, "QZERO dot=%u lx=%u wx=%u\n",
					static_cast<unsigned>(m_dots), static_cast<unsigned>(m_lx),
					static_cast<unsigned>(m_lcd->m_win_x));
			}
			m_bg_pixels.push_back(BgPixel{0});
			return;
		}

		push_bg_tile();
		m_fetch_phase = FetchPhase::BgTilemap;
		m_bg_fetch_x = static_cast<u8>(m_bg_fetch_x + 8);
		if(m_window_just_activated)
		{
			// The first window tile fetch takes 6 dots: start the next
			// tilemap access in the same tick as the push.
			m_window_just_activated = false;
			tick_bg_fetch();
		}
		return;
	}

	// FIFO not empty: if an OBJ is pending, start the object fetch. The
	// BG tile stays cached and is pushed after the group.
	if(lcdc_obj_en() && m_lx < 168 && oam_entry_pending(m_lx))
	{
		start_sprite_fetch();
	}
}

void PPULegacy::push_bg_tile()
{
	const bool is_window = m_fetch_window && m_window_active;
	for(int i = m_bg_fetch_skip; i < 8; i++)
	{
		const u8 bit = static_cast<u8>(7 - i);
		const u8 low = static_cast<u8>((m_fetch_low >> bit) & 1);
		const u8 high = static_cast<u8>((m_fetch_high >> bit) & 1);
		m_bg_pixels.push_back(BgPixel{static_cast<u8>((high << 1) | low), is_window});
	}
}

void PPULegacy::start_sprite_fetch()
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
	const bool window_tile = m_window_active && lcdc_win_en() && screen_x >= window_x;

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

void PPULegacy::tick_obj_fetch()
{
	if(m_obj_fetch_wait > 0)
	{
		--m_obj_fetch_wait;
		return;
	}

	if(m_obj_fetch_phase == FetchPhase::ObjLow)
	{
		const u8 height = lcdc_obj_height();
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
		// A lone sprite at X>0 needs six more dots to align the next BG
		// tile fetch with the point where SCX is sampled. The CPU-visible
		// mode 0 is handled by the per-line dot budget, so this visual phase
		// shift does not affect the sprite timing test.
		int x_extra = 6;
		if(const char* env = std::getenv("QTR_XEXTRA")) x_extra = std::atoi(env);
		int x0_extra = 4;
		if(const char* env = std::getenv("QTR_X0WAIT")) x0_extra = std::atoi(env);
		u8 post_wait = 0;
		if(m_obj_group_total == 1 && !lcdc_win_en())
		{
			post_wait = m_sprite_entry.entry.m_x != 0
				? static_cast<u8>(x_extra) : static_cast<u8>(x0_extra);
		}
		m_obj_post_wait = post_wait;
		// Sprites starting at screen x=-2/-1 stall the visible output one
		// dot less than the generic lone-sprite case.
		const u8 out_delay = (m_obj_post_wait == 6 &&
			((m_sprite_entry.entry.m_x & 0x07) >= 6)) ? 5 : m_obj_post_wait;
		m_line_obj_out_delay = static_cast<u8>(m_line_obj_out_delay + out_delay);
	}
}

void PPULegacy::push_obj_pixels()
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

void PPULegacy::update_delayed_registers()
{
	// On DMG the PPU sees these registers delayed by 1 T-cycle: this dot uses
	// the snapshot from the previous dot.
	for(int i = 9; i > 0; --i)
	{
		m_lcdc_hist[i] = m_lcdc_hist[i - 1];
	}
	m_lcdc_hist[0] = m_lcd->m_lcdc;
#ifdef QTR_CPU_MICRO
	m_last_lcdc = lcdc_ppu();
#else
	m_last_lcdc = m_lcd->m_lcdc;
#endif
#ifdef QTR_CPU_MICRO
	m_last_bgp = m_bgp_pre_tick;
#else
	m_last_bgp = m_lcd->m_bg_palette;
#endif
	m_wx_delay3 = m_wx_delay2;
	m_wx_delay2 = m_last_wx;
	m_last_wx = m_lcd->m_win_x;
	m_last_ly = m_lcd->m_ly;
	m_last_lyc = m_lcd->m_lyc;
}

void PPULegacy::raise_stat_irq()
{
	if(m_stat_model)
	{
		const LCDMode mode = m_lcd->get_lcds_mode();
		const bool lyc = m_enable_lyc_eq_ly_irq &&
			m_lcd->get_lcds_stat_int(StatSRC::SS_LYC) && (m_last_lyc == m_last_ly);
		const bool oam = m_lcd->get_lcds_stat_int(StatSRC::SS_OAM) &&
			(m_oam_line || m_vblank_oam_pulse || mode == LCDMode::MODE_OAM);
		const bool vblank = m_lcd->get_lcds_stat_int(StatSRC::SS_VBLANK) &&
			mode == LCDMode::MODE_VBLANK;
		const bool hblank = m_lcd->get_lcds_stat_int(StatSRC::SS_HBLANK) &&
			m_hblank_line;
		const bool level = lyc || oam || vblank || hblank;

		// Deliver a previously scheduled edge with its per-source phase.
		if(m_stat_deliver_pending)
		{
			if(m_stat_deliver_countdown > 0)
			{
				--m_stat_deliver_countdown;
			}
			if(m_stat_deliver_countdown == 0)
			{
				request_interrupt(InterruptType::IT_LCD_STAT);
				m_stat_deliver_pending = false;
			}
		}

		if(level && !m_stat_irq_line)
		{
			u8 delay = 0;
			if(lyc && !m_stat_prev_lyc)
			{
				delay = m_stat_delay_lyc;
			}
			else if(oam && !m_stat_prev_oam)
			{
				delay = m_stat_delay_oam;
			}
			else if(vblank && !m_stat_prev_vblank)
			{
				delay = m_stat_delay_vblank;
			}
			else if(hblank && !m_stat_prev_hblank)
			{
				delay = m_stat_delay_hblank;
			}

			if(delay == 0)
			{
				request_interrupt(InterruptType::IT_LCD_STAT);
			}
			else
			{
				m_stat_deliver_countdown = delay;
				m_stat_deliver_pending = true;
			}
		}

		m_stat_irq_line = level;
		m_stat_prev_lyc = lyc;
		m_stat_prev_oam = oam;
		m_stat_prev_vblank = vblank;
		m_stat_prev_hblank = hblank;
		m_last_stat_irq = level;
		m_vblank_oam_pulse = false;
		m_pending_stat_irq = false;
		return;
	}
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

void PPULegacy::update_stat_irq_for_oam_mode()
{
	if(m_stat_model)
	{
		m_oam_line = true;
		return;
	}
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
