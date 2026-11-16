#include "ppu.h"

using namespace qtreGB;
using namespace std;

namespace
{
constexpr u16 TILE_WIDTH = 8;
constexpr u16 TILE_HEIGHT = 8;
constexpr u16 TILE_BYTES = 16;
constexpr u16 TILE_ROW_BYTES = 2;
constexpr u16 TILEMAP_WIDTH = 32;
constexpr u16 TILEMAP_HEIGHT = 32;
constexpr u16 TILEMAP_CELL_BYTES = 1;
constexpr u16 TILE_DOUBLE_HEIGHT = 16;
} // namespace

const int PPU::LINES_PER_FRAME = 154;
const int PPU::TICKS_PER_LINE = 456;
const int PPU::YRES = 144;
const int PPU::XRES = 160;

PPU::PPU(LCD *lcd) :
m_lcd(lcd),
m_vram(0x2000),
m_oam_ram(40),
m_video_buffer(static_cast<size_t>(PPU::YRES) * PPU::XRES)
{
}

void PPU::ppu_init(bool boot_rom)
{
	m_boot_rom = boot_rom;
	m_vram_bus.reset();
	m_oam_bus.reset();
	m_video_buffer.assign(static_cast<size_t>(PPU::YRES) * PPU::XRES, 0);
	m_lcd->lcd_init(boot_rom);
	reset();
	sync_lcd_from_ppu();
}

void PPU::ppu_tick()
{
	// The PPU is frozen while LCDC.7 is clear. The STAT mode update and the
	// pending STAT write are still ticked, as in the oracle.
	if(m_lcdc.enable)
	{
		(this->*(m_tick_selector))();

		// The window activation condition is checked every dot for the entire
		// frame, not only during pixel transfer.
		tick_window();

		// LYC == LY and the STAT sources are evaluated every dot.
		raise_stat_irq();

		// DMG: the PPU sees BGP/WX/LCDC/LY/LYC delayed by one T-cycle.
		m_last_bgp = m_bgp;
		m_last_wx = m_wx;
		m_last_lcdc = m_lcdc;
		m_last_ly = m_ly;
		m_last_lyc = m_lyc;
	}

	tick_pending_write();
	tick_stat();
	sync_lcd_from_ppu();
}

// ------- PPU helpers -------

void PPU::turn_on()
{
	m_last_lyc = m_lyc;

	// STAT's LYC_EQ_LY is updated but no interrupt is raised.
	m_stat.lyc_eq_ly = is_lyc_eq_ly();
}

void PPU::turn_off()
{
	m_dots = 0;
	m_ly = 0;
	m_last_ly = 0;

	for(auto& list : m_oam_entries)
	{
		list.clear();
	}

	reset_fetcher();

	// The first line after the PPU is turned on behaves differently: STAT
	// remains in HBlank during OAM scan and the OAM bus is kept released.
	update_mode(OAM_SCAN);
	m_tick_selector = &PPU::oam_scan_after_turn_on;

	m_is_glitched_line_0 = true;
	m_glitched_line_0_hblank_delay = 0;

	m_enable_lyc_eq_ly_irq = true;

	m_vram_bus.release(VideoDevice::Ppu, VideoAccess::Read);
	m_vram_bus.release(VideoDevice::Ppu, VideoAccess::Write);
	m_oam_bus.release(VideoDevice::Ppu, VideoAccess::Read);
	m_oam_bus.release(VideoDevice::Ppu, VideoAccess::Write);
}

bool PPU::is_lyc_eq_ly() const
{
	// On DMG, LYC_EQ_LY is forced low at dot 454 and on the last VBlank line
	// dots 2..6; LYC_EQ_LY IRQ is disabled through m_enable_lyc_eq_ly_irq.
	// [mooneye/lcdon_timing, daid/ppu_scanline_bgp]
	return (m_last_lyc == m_last_ly) && m_enable_lyc_eq_ly_irq;
}

void PPU::raise_stat_irq()
{
	// STAT interrupt is edge triggered and evaluated every dot. The OAM source
	// is the exception: it is only checked on mode transitions.
	// [mooneye/stat_irq_blocking, mooneye/stat_lyc_onoff]
	const bool lyc_eq_ly = is_lyc_eq_ly();

	if(m_pending_stat_irq)
	{
		// Actually raise the STAT interrupt (one T-cycle after the edge).
		if(m_request_interrupt)
		{
			m_request_interrupt(InterruptType::IT_LCD_STAT);
		}
		m_pending_stat_irq = false;
	}
	else
	{
		const bool lyc_eq_ly_irq = m_stat.lyc_eq_ly_int && lyc_eq_ly;
		const bool hblank_irq = m_stat.hblank_int && m_mode == HBLANK;

		// In DMG, VBlank interrupt is raised with either the VBlank or the OAM
		// STAT source flag set.
		const bool vblank_irq = (m_stat.vblank_int || m_stat.oam_int) && m_mode == VBLANK;

		update_stat_irq(lyc_eq_ly_irq || hblank_irq || vblank_irq);
	}

	m_stat.lyc_eq_ly = lyc_eq_ly;
}

void PPU::update_stat_irq(bool irq)
{
	// Rising edge only; the actual request is raised on the next dot.
	m_pending_stat_irq = m_last_stat_irq < irq;
	m_last_stat_irq = irq;
}

void PPU::update_stat_irq_for_oam_mode()
{
	const bool lyc_eq_ly_irq = m_stat.lyc_eq_ly_int && (m_last_lyc == m_last_ly);
	const bool lyc_eq_next_ly_irq = m_stat.lyc_eq_ly_int && (m_last_lyc == static_cast<u8>(m_last_ly + 1));
	const bool irq = m_stat.oam_int || lyc_eq_ly_irq;

	m_pending_stat_irq = m_last_stat_irq < irq;

	// The STAT IRQ line is only reset when LYC == LY is true for the next LY.
	if(lyc_eq_next_ly_irq)
	{
		m_last_stat_irq = irq;
	}
}

void PPU::update_stat_irq_for_oam_mode_do_not_clear_last_stat_irq()
{
	const bool lyc_eq_ly_irq = m_stat.lyc_eq_ly_int && (m_last_lyc == m_last_ly);
	const bool irq = m_stat.oam_int || lyc_eq_ly_irq;

	m_pending_stat_irq = m_last_stat_irq < irq;
}

void PPU::begin_increase_ly()
{
	// DMG has no LY update glitch; nothing to prepare here.
}

void PPU::end_increase_ly()
{
	++m_ly;
}

void PPU::write_stat_real(u8 value)
{
	m_stat.lyc_eq_ly_int = (value & 0x40) != 0;
	m_stat.oam_int = (value & 0x20) != 0;
	m_stat.vblank_int = (value & 0x10) != 0;
	m_stat.hblank_int = (value & 0x08) != 0;
}

void PPU::write_lcdc_real(u8 value)
{
	// LCDC.7 is handled by write_lcdc(): it powers the PPU on/off instantly.
	m_lcdc.win_tile_map = (value & 0x40) != 0;
	m_lcdc.win_enable = (value & 0x20) != 0;
	m_lcdc.bg_win_tile_data = (value & 0x10) != 0;
	m_lcdc.bg_tile_map = (value & 0x08) != 0;
	m_lcdc.obj_size = (value & 0x04) != 0;
	m_lcdc.obj_enable = (value & 0x02) != 0;
	m_lcdc.bg_win_enable = (value & 0x01) != 0;
}

void PPU::tick_window()
{
	// The window is considered active for the rest of the frame if at some
	// point it happens that the window is enabled while LY matches WY.
	m_w_active_for_frame = m_w_active_for_frame || (m_lcdc.win_enable && m_ly == m_wy);
}

void PPU::tick_pending_write()
{
	// On DMG a STAT write takes effect one T-cycle later; during that cycle all
	// the STAT interrupt sources read as enabled.
	if(m_pending_stat.countdown)
	{
		m_pending_stat.countdown = 0;
		write_stat_real(m_pending_stat.value);
	}
}

void PPU::tick_stat()
{
	// STAT reads as HBlank during OAM scan in three cases: first line after
	// turn on, OAM DMA in progress, and the last VBlank line transition.
	const bool force_hblank =
		m_mode == OAM_SCAN && (m_is_glitched_line_0 || m_dma_active || m_vblank_last_line_stat_mode_hblank_glitch);

	m_stat.mode = force_hblank ? HBLANK : m_mode;
}

// ------- PPU states -------

void PPU::oam_scan_even()
{
	// Flush the read request for the current OAM entry (Y and X).
	oam_scan_flush_read_request();

	if(++m_dots == 77)
	{
		m_tick_selector = &PPU::oam_scan_77;
	}
	else
	{
		m_tick_selector = &PPU::oam_scan_odd;
	}
}

void PPU::oam_scan_odd()
{
	// Submit the read request for the next OAM entry.
	oam_scan_read_request();

	if(m_dots == 75)
	{
		// On DMG the OAM bus is released for this cycle only.
		// [mooneye/lcdon_write_timing]
		m_oam_bus.release(VideoDevice::Ppu, VideoAccess::Read);
		m_oam_bus.release(VideoDevice::Ppu, VideoAccess::Write);
	}

	++m_dots;

	m_tick_selector = &PPU::oam_scan_even;
}

void PPU::oam_scan_77()
{
	// On DMG the OAM bus is re-acquired this cycle.
	m_oam_bus.acquire(VideoDevice::Ppu, VideoAccess::Read);
	m_oam_bus.acquire(VideoDevice::Ppu, VideoAccess::Write);

	// VRAM is acquired one cycle before STAT is updated.
	// [mooneye/lcdon_timing]
	m_vram_bus.acquire(VideoDevice::Ppu, VideoAccess::Read);
	m_vram_bus.acquire(VideoDevice::Ppu, VideoAccess::Write);

	m_dots = 78;

	m_tick_selector = &PPU::oam_scan_done;
}

void PPU::oam_scan_done()
{
	if(++m_dots == 79)
	{
		// STAT mode is updated to pixel transfer one dot before it starts.
		update_mode(PIXEL_TRANSFER);
	}
	else
	{
		enter_pixel_transfer();
	}
}

void PPU::oam_scan_after_turn_on()
{
	// The first OAM scan after turn on does nothing and leaves the OAM bus
	// released.
	if(++m_dots == 78)
	{
		m_tick_selector = &PPU::oam_scan_after_turn_on_78;
	}
}

void PPU::oam_scan_after_turn_on_78()
{
	update_mode(PIXEL_TRANSFER);

	m_tick_selector = &PPU::oam_scan_after_turn_on_79;

	m_dots = 79;
}

void PPU::oam_scan_after_turn_on_79()
{
	m_dots = 80;

	enter_pixel_transfer();
}

void PPU::pixel_transfer_dummy_lx0()
{
	if(++m_dots == 83)
	{
		// The first tile fetch is only used to align the initial SCX % 8 and is
		// discarded anyway.
		fill_bg_fifo();

		// Initial SCX is read shortly after pixel transfer begins.
		// [mealybug/m3_scx_low_3_bits]
		m_initial_scx.to_discard = m_scx & 0x07;

		if(m_initial_scx.to_discard)
		{
			m_initial_scx.discarded = 0;

			// With SCX % 8 > 0 the window can be activated before BG.
			check_window_activation();

			m_tick_selector =
				m_w_activating ? &PPU::pixel_transfer_discard_lx0_wx0_scx : &PPU::pixel_transfer_discard_lx0;
		}
		else
		{
			m_tick_selector = &PPU::pixel_transfer_lx0;
		}
	}
}

void PPU::pixel_transfer_discard_lx0()
{
	handle_pending_bg_fifo_fill();

	// The first SCX % 8 pixels of a BG/window tile are simply discarded. LX is
	// not incremented, which lets OBJ align with the BG.
	if(is_bg_fifo_ready_to_be_popped())
	{
		m_bg_fifo.pop_front();

		if(++m_initial_scx.discarded == m_initial_scx.to_discard)
		{
			m_tick_selector = &PPU::pixel_transfer_lx0;
		}
	}

	tick_fetcher();

	handle_pixel_slice_fetcher_push();

	++m_dots;
}

void PPU::pixel_transfer_discard_lx0_wx0_scx()
{
	handle_pending_window_activation();

	// Window activation is one T-cycle late when WX == 0: skip the first
	// fetcher tick.
	m_tick_selector = &PPU::pixel_transfer_discard_lx0;

	++m_dots;
}

void PPU::pixel_transfer_lx0()
{
	handle_pending_window_activation();

	handle_pending_bg_fifo_fill();

	bool inc_lx = false;

	// For LX in [0, 8) pixels are popped but not pushed to the LCD.
	if(is_bg_fifo_ready_to_be_popped())
	{
		m_bg_fifo.pop_front();

		if(!m_obj_fifo.empty())
		{
			m_obj_fifo.pop_front();
		}

		inc_lx = true;

		if(m_lx + 1 == 8)
		{
			m_tick_selector = &PPU::pixel_transfer_lx8;
		}

		check_window_activation();
	}

	tick_fetcher();

	handle_pixel_slice_fetcher_push();

	if(inc_lx)
	{
		increase_lx();
	}

	++m_dots;
}

void PPU::pixel_transfer_lx8()
{
	handle_pending_window_activation();

	handle_pending_bg_fifo_fill();

	bool inc_lx = false;

	if(is_bg_fifo_ready_to_be_popped())
	{
		const BgPixel bg_pixel = m_bg_fifo.front();
		m_bg_fifo.pop_front();

		// On DMG the PPU sees LCDC delayed by one T-cycle, except at LX == 8.
		// [mealybug/m3_lcdc_bg_en_change, m3_lcdc_obj_en_change]
		const Lcdc& lcdc = m_lx == 8 ? m_lcdc : m_last_lcdc;

		// NO_COLOR means "no OBJ pixel was selected".
		static constexpr u8 NO_COLOR = 4;
		u8 color = NO_COLOR;

		if(!m_obj_fifo.empty())
		{
			const ObjPixel obj_pixel = m_obj_fifo.front();
			m_obj_fifo.pop_front();

			// Take the OBJ pixel instead of the BG pixel only if all these are
			// satisfied:
			// - LCDC.OBJ_ENABLE is 1,
			// - the OBJ pixel is opaque,
			// - the BG color index is 0, BG is disabled or BG_OVER_OBJ is 0.
			const bool show_obj =
				lcdc.obj_enable && is_obj_opaque(obj_pixel.color_index) &&
				(!lcdc.bg_win_enable || bg_pixel.color_index == 0 ||
			     (obj_pixel.attributes & 0x80) == 0);

			if(show_obj)
			{
				color = resolve_color(obj_pixel.color_index, (obj_pixel.attributes & 0x10) ? m_obp1 : m_obp0);
			}
		}

		if(color == NO_COLOR)
		{
			if(lcdc.bg_win_enable)
			{
				// On DMG there is a one T-cycle BGP delay during which the PPU
				// sees the last BGP ORed with the new one.
				// [mealybug/m3_bgp_change]
				color = resolve_color(bg_pixel.color_index, m_bgp | m_last_bgp);
			}
			else
			{
				color = 0;
			}
		}

		push_pixel(color);

		inc_lx = true;

		if(m_lx + 1 == 168)
		{
			increase_lx();
			++m_dots;
			enter_hblank();
			return;
		}

		check_window_activation();
	}

	tick_fetcher();

	handle_pixel_slice_fetcher_push();

	if(inc_lx)
	{
		increase_lx();
	}

	++m_dots;
}

void PPU::hblank()
{
	if(++m_dots == 452)
	{
		m_tick_selector = &PPU::hblank_452;
	}
}

void PPU::hblank_452()
{
	// Eventually raise the OAM interrupt.
	update_stat_irq_for_oam_mode();

	begin_increase_ly();

	m_dots = 453;

	m_tick_selector = &PPU::hblank_453;
}

void PPU::hblank_453()
{
	// On DMG the OAM bus is acquired here.
	m_oam_bus.acquire(VideoDevice::Ppu, VideoAccess::Read);
	m_oam_bus.acquire(VideoDevice::Ppu, VideoAccess::Write);

	reset_oam_scan_entries();

	// The first OAM entry is read at dot 453, before OAM scan begins.
	oam_scan_read_request();

	// LYC_EQ_LY IRQ is disabled for dot 453.
	m_enable_lyc_eq_ly_irq = false;

	end_increase_ly();

	m_dots = 454;

	m_tick_selector = &PPU::hblank_454;
}

void PPU::hblank_454()
{
	// Flush the read request for the first OAM entry.
	oam_scan_flush_read_request();

	// Enable LYC_EQ_LY IRQ again.
	m_enable_lyc_eq_ly_irq = true;

	m_dots = 455;

	m_tick_selector = &PPU::hblank_455;

	// STAT mode is updated to OAM one dot before it actually comes in.
	update_mode(OAM_SCAN);
}

void PPU::hblank_455()
{
	oam_scan_read_request();

	m_dots = 0;

	enter_oam_scan();
}

void PPU::hblank_last_line()
{
	if(++m_dots == 453)
	{
		// last_stat_irq is intentionally not reset here.
		update_stat_irq_for_oam_mode_do_not_clear_last_stat_irq();

		begin_increase_ly();

		m_tick_selector = &PPU::hblank_last_line_453;
	}
}

void PPU::hblank_last_line_453()
{
	// LYC_EQ_LY IRQ is disabled for dot 453.
	m_enable_lyc_eq_ly_irq = false;

	end_increase_ly();

	m_dots = 454;

	m_tick_selector = &PPU::hblank_last_line_454;
}

void PPU::hblank_last_line_454()
{
	// Enable LYC_EQ_LY IRQ again.
	m_enable_lyc_eq_ly_irq = true;

	m_dots = 455;

	update_mode(VBLANK);

	m_tick_selector = &PPU::hblank_last_line_455;
}

void PPU::hblank_last_line_455()
{
	m_dots = 0;

	enter_vblank();
}

void PPU::hblank_first_line_after_turn_on()
{
	++m_dots;
	++m_glitched_line_0_hblank_delay;

	if(m_glitched_line_0_hblank_delay == 1)
	{
		// VRAM and OAM buses are released one dot before the mode update.
		m_vram_bus.release(VideoDevice::Ppu, VideoAccess::Read);
		m_vram_bus.release(VideoDevice::Ppu, VideoAccess::Write);
		m_oam_bus.release(VideoDevice::Ppu, VideoAccess::Read);
		m_oam_bus.release(VideoDevice::Ppu, VideoAccess::Write);
	}
	else if(m_glitched_line_0_hblank_delay == 2)
	{
		// STAT is updated to HBlank two dots later on the first line after
		// turn on.
		update_mode(HBLANK);

		m_tick_selector = &PPU::hblank;

		m_is_glitched_line_0 = false;
	}
}

void PPU::vblank()
{
	if(++m_dots == 453)
	{
		begin_increase_ly();

		m_tick_selector = &PPU::vblank_453;
	}
}

void PPU::vblank_453()
{
	m_dots = 454;

	end_increase_ly();

	// LYC_EQ_LY IRQ is disabled for dot 454.
	m_enable_lyc_eq_ly_irq = false;

	m_tick_selector = &PPU::vblank_454;
}

void PPU::vblank_454()
{
	m_dots = 455;

	// Enable LYC_EQ_LY IRQ again.
	m_enable_lyc_eq_ly_irq = true;

	m_tick_selector = &PPU::vblank_455;
}

void PPU::vblank_455()
{
	m_dots = 0;

	m_tick_selector = m_ly == 153 ? &PPU::vblank_last_line : &PPU::vblank;
}

void PPU::vblank_last_line()
{
	if(++m_dots == 2)
	{
		// On DMG, LY is reset to 0 here.
		m_ly = 0;

		m_tick_selector = &PPU::vblank_last_line_2;
	}
}

void PPU::vblank_last_line_2()
{
	m_dots = 3;

	// LYC_EQ_LY IRQ is disabled from dot 2 through dot 6.
	m_enable_lyc_eq_ly_irq = false;

	m_tick_selector = &PPU::vblank_last_line_3;
}

void PPU::vblank_last_line_3()
{
	m_dots = 4;

	m_tick_selector = &PPU::vblank_last_line_4;
}

void PPU::vblank_last_line_4()
{
	if(++m_dots == 7)
	{
		// Enable LYC_EQ_LY IRQ again.
		m_enable_lyc_eq_ly_irq = true;

		m_tick_selector = &PPU::vblank_last_line_7;
	}
}

void PPU::vblank_last_line_7()
{
	if(++m_dots == 453)
	{
		m_tick_selector = &PPU::vblank_last_line_453;
	}
}

void PPU::vblank_last_line_453()
{
	// OAM bus is acquired here.
	m_oam_bus.acquire(VideoDevice::Ppu, VideoAccess::Read);
	m_oam_bus.acquire(VideoDevice::Ppu, VideoAccess::Write);

	reset_oam_scan_entries();

	// The first OAM entry is read before OAM scan begins.
	oam_scan_read_request();

	// On DMG, reading STAT during the last few cycles of VBlank yields HBlank
	// without raising a HBlank STAT interrupt.
	m_vblank_last_line_stat_mode_hblank_glitch = true;

	update_mode(OAM_SCAN);

	m_dots = 454;

	m_tick_selector = &PPU::vblank_last_line_454;
}

void PPU::vblank_last_line_454()
{
	// Flush the read request for the first OAM entry.
	oam_scan_flush_read_request();

	m_dots = 455;

	m_tick_selector = &PPU::vblank_last_line_455;
}

void PPU::vblank_last_line_455()
{
	oam_scan_read_request();

	m_dots = 0;

	enter_new_frame();
}

// ------- State helpers -------

void PPU::enter_oam_scan()
{
	m_tick_selector = &PPU::oam_scan_even;
}

void PPU::enter_pixel_transfer()
{
	reset_fetcher();

	m_tick_selector = &PPU::pixel_transfer_dummy_lx0;

	m_vram_bus.acquire(VideoDevice::Ppu, VideoAccess::Read);
	m_vram_bus.acquire(VideoDevice::Ppu, VideoAccess::Write);
	m_oam_bus.acquire(VideoDevice::Ppu, VideoAccess::Read);
	m_oam_bus.acquire(VideoDevice::Ppu, VideoAccess::Write);
}

void PPU::enter_hblank()
{
	if(m_is_glitched_line_0)
	{
		// HBlank is delayed by two dots on the first line after turn on and the
		// buses stay acquired until the delay elapses.
		m_tick_selector = &PPU::hblank_first_line_after_turn_on;

		m_glitched_line_0_hblank_delay = 0;
	}
	else
	{
		update_mode(HBLANK);

		m_tick_selector = m_ly == 143 ? &PPU::hblank_last_line : &PPU::hblank;

		m_vram_bus.release(VideoDevice::Ppu, VideoAccess::Read);
		m_vram_bus.release(VideoDevice::Ppu, VideoAccess::Write);
		m_oam_bus.release(VideoDevice::Ppu, VideoAccess::Read);
		m_oam_bus.release(VideoDevice::Ppu, VideoAccess::Write);
	}
}

void PPU::enter_vblank()
{
	m_tick_selector = &PPU::vblank;

	if(m_request_interrupt)
	{
		m_request_interrupt(InterruptType::IT_VBLANK);
	}
}

void PPU::enter_new_frame()
{
	// The window line counter is reset to 255 so that it goes to 0 on the next
	// window trigger.
	m_w_wly = 0xFF;

	m_w_active_for_frame = false;

	// End of the HBlank STAT glitch.
	m_vblank_last_line_stat_mode_hblank_glitch = false;

	enter_oam_scan();

	// Eventually raise the OAM interrupt.
	update_stat_irq_for_oam_mode();

	if(m_frame_done)
	{
		m_frame_done(true);
	}
}

void PPU::update_mode(u8 mode)
{
	m_mode = static_cast<Mode>(mode);
}

u8 PPU::resolve_color(u8 color_index, u8 palette) const
{
	return (palette >> (2 * color_index)) & 0x03;
}

void PPU::increase_lx()
{
	// Clear the OAM entries of the current LX (they may contain OBJs not
	// served).
	m_oam_entries[m_lx].clear();

	++m_lx;
}

// ------- Fetcher -------

void PPU::tick_fetcher()
{
	(this->*(m_fetcher_tick_selector))();
}

void PPU::reset_fetcher()
{
	m_lx = 0;
	m_bg_fifo.clear();
	m_obj_fifo.clear();
	m_pixel_slice_fetcher_push_enabled = false;
	m_pending_bg_fifo_fill = false;
	m_fetching_sprite = false;
	m_w_activating = false;
	m_w_active = false;
	m_w_just_activated = false;
	m_bwf.lx = 0;
	m_bwf.tile_number = 0;
	m_bwf.interrupted_fetch_has_data = false;
	m_wf_tilemap_x = 0;
	m_fetcher_tick_selector = &PPU::bgwin_prefetcher_get_tile_0;
}

void PPU::pixel_slice_fetcher_idle()
{
}

void PPU::bgwin_prefetcher_get_tile_0()
{
	// Fetch the window tile when the window is active, otherwise BG.
	if(m_w_active)
	{
		if(m_lcdc.win_enable)
		{
			win_prefetcher_get_tile_0();
			return;
		}

		// Abort the window fetch and proceed with BG.
		m_w_active = false;
	}

	bg_prefetcher_get_tile_0();
}

void PPU::bg_prefetcher_get_tile_0()
{
	// This phase only computes the tile base address from the tilemap
	// coordinates. [mealybug/m3_lcdc_bg_map_change]
	setup_bg_pixel_slice_fetcher_tilemap_tile_address();

	read_bgwin_tile_number();

	m_fetcher_tick_selector = &PPU::bg_prefetcher_get_tile_1;
}

void PPU::bg_prefetcher_get_tile_1()
{
	m_fetcher_tick_selector = &PPU::bg_pixel_slice_fetcher_get_tile_data_low_0;
}

void PPU::bg_pixel_slice_fetcher_get_tile_data_low_0()
{
	// The tile data address is computed just before each VRAM access, so SCY
	// or LCDC.BG_WIN_TILE_DATA changes between the low/high phases can desync
	// the bitplanes. [mealybug/m3_scy_change, m3_lcdc_tile_sel_change]
	setup_bg_pixel_slice_fetcher_tile_data_address();

	read_bgwin_tile_data_low();

	m_fetcher_tick_selector = &PPU::bg_pixel_slice_fetcher_get_tile_data_low_1;
}

void PPU::bg_pixel_slice_fetcher_get_tile_data_low_1()
{
	m_fetcher_tick_selector = &PPU::bg_pixel_slice_fetcher_get_tile_data_high_0;
}

void PPU::bg_pixel_slice_fetcher_get_tile_data_high_0()
{
	setup_bg_pixel_slice_fetcher_tile_data_address();

	read_bgwin_tile_data_high();

	m_fetcher_tick_selector = &PPU::bg_pixel_slice_fetcher_get_tile_data_high_1;
}

void PPU::bg_pixel_slice_fetcher_get_tile_data_high_1()
{
	bgwin_pixel_slice_fetcher_get_tile_data_high_1();
}

void PPU::win_prefetcher_get_tile_0()
{
	// If the window is turned off, switch back to the BG fetcher.
	if(!m_lcdc.win_enable)
	{
		bg_prefetcher_get_tile_0();
		return;
	}

	// The window prefetcher has its own internal tile counter.
	setup_win_pixel_slice_fetcher_tilemap_tile_address();

	read_bgwin_tile_number();

	m_fetcher_tick_selector = &PPU::win_prefetcher_get_tile_1;
}

void PPU::win_prefetcher_get_tile_1()
{
	if(!m_lcdc.win_enable)
	{
		bg_prefetcher_get_tile_1();
		return;
	}

	m_fetcher_tick_selector = &PPU::win_pixel_slice_fetcher_get_tile_data_low_0;
}

void PPU::win_pixel_slice_fetcher_get_tile_data_low_0()
{
	if(!m_lcdc.win_enable)
	{
		bg_pixel_slice_fetcher_get_tile_data_low_0();
		return;
	}

	setup_win_pixel_slice_fetcher_tile_data_address();

	read_bgwin_tile_data_low();

	m_fetcher_tick_selector = &PPU::win_pixel_slice_fetcher_get_tile_data_low_1;
}

void PPU::win_pixel_slice_fetcher_get_tile_data_low_1()
{
	if(!m_lcdc.win_enable)
	{
		bg_pixel_slice_fetcher_get_tile_data_low_1();
		return;
	}

	m_fetcher_tick_selector = &PPU::win_pixel_slice_fetcher_get_tile_data_high_0;
}

void PPU::win_pixel_slice_fetcher_get_tile_data_high_0()
{
	if(!m_lcdc.win_enable)
	{
		bg_pixel_slice_fetcher_get_tile_data_high_0();
		return;
	}

	setup_win_pixel_slice_fetcher_tile_data_address();

	read_bgwin_tile_data_high();

	m_fetcher_tick_selector = &PPU::win_pixel_slice_fetcher_get_tile_data_high_1;
}

void PPU::win_pixel_slice_fetcher_get_tile_data_high_1()
{
	if(!m_lcdc.win_enable)
	{
		bg_pixel_slice_fetcher_get_tile_data_high_1();
		return;
	}

	bgwin_pixel_slice_fetcher_get_tile_data_high_1();
}

void PPU::bgwin_pixel_slice_fetcher_get_tile_data_high_1()
{
	// The BG tile is increased only when the fetch reaches this step. The first
	// window fetch does not increase the BG tile.
	if(!m_w_just_activated)
	{
		m_bwf.lx += TILE_WIDTH;
	}

	// Allow the pixel-slice fetcher to push to the BG FIFO.
	m_fetcher_tick_selector = &PPU::pixel_slice_fetcher_idle;
	enable_pixel_slice_fetcher_push();
}

// ------- OBJ fetcher -------

void PPU::obj_prefetcher_get_tile_0()
{
	// The tile number and attributes are requested here and read one T-cycle
	// later, as in the oracle.
	m_fetcher_tick_selector = &PPU::obj_prefetcher_get_tile_1;
}

void PPU::obj_prefetcher_get_tile_1()
{
	const u16 address = static_cast<u16>(4 * m_of.entry.number);

	m_of.tile_number = ppu_oam_read(static_cast<u16>(address + 2));
	m_of.attributes = static_cast<u8>(ppu_oam_read(static_cast<u16>(address + 3)) & 0xF0);

	m_fetcher_tick_selector = &PPU::obj_pixel_slice_fetcher_get_tile_data_low_0;
}

void PPU::obj_pixel_slice_fetcher_get_tile_data_low_0()
{
	m_fetcher_tick_selector = &PPU::obj_pixel_slice_fetcher_get_tile_data_low_1;
}

void PPU::obj_pixel_slice_fetcher_get_tile_data_low_1()
{
	// The tile data address is computed just before the access, so a change to
	// LCDC.OBJ_SIZE between the low/high phases can desync the bitplanes.
	// [mealybug/m3_lcdc_obj_size_change, m3_lcdc_obj_size_change_scx]
	setup_obj_pixel_slice_fetcher_tile_data_address();

	read_obj_tile_data_low();

	m_fetcher_tick_selector = &PPU::obj_pixel_slice_fetcher_get_tile_data_high_0;
}

void PPU::obj_pixel_slice_fetcher_get_tile_data_high_0()
{
	m_fetcher_tick_selector = &PPU::obj_pixel_slice_fetcher_get_tile_data_high_1_and_merge_with_obj_fifo;
}

void PPU::obj_pixel_slice_fetcher_get_tile_data_high_1_and_merge_with_obj_fifo()
{
	setup_obj_pixel_slice_fetcher_tile_data_address();

	read_obj_tile_data_high();

	// Expand the two bitplanes into eight color indices (bit 7 first, or bit 0
	// first when X_FLIP is set).
	const bool x_flip = (m_of.attributes & 0x20) != 0;

	ObjPixel obj_pixels[8];
	for(int i = 0; i < 8; ++i)
	{
		const int shift = x_flip ? i : 7 - i;
		const u8 color = static_cast<u8>(((m_psf.tile_data_low >> shift) & 1) |
		                                 (((m_psf.tile_data_high >> shift) & 1) << 1));
		obj_pixels[i].color_index = color;
		obj_pixels[i].attributes = m_of.attributes;
		obj_pixels[i].number = m_of.entry.number;
		obj_pixels[i].x = m_lx;
	}

	// Merge with the pixels already in the OBJ FIFO. The smaller X coordinate
	// has priority; with equal X, the object first in OAM wins.
	const size_t obj_fifo_size = m_obj_fifo.size();
	size_t i = 0;

	while(i < obj_fifo_size)
	{
		const ObjPixel& fifo_obj_pixel = m_obj_fifo[i];
		const ObjPixel& obj_pixel = obj_pixels[i];

		const bool overwrite_obj_pixel =
			(is_obj_opaque(obj_pixel.color_index) && !is_obj_opaque(fifo_obj_pixel.color_index)) ||
			(is_obj_opaque(obj_pixel.color_index) && is_obj_opaque(fifo_obj_pixel.color_index) &&
			 obj_pixel.x == fifo_obj_pixel.x && obj_pixel.number < fifo_obj_pixel.number);

		if(overwrite_obj_pixel)
		{
			m_obj_fifo[i] = obj_pixel;
		}

		++i;
	}

	while(i < 8)
	{
		m_obj_fifo.push_back(obj_pixels[i++]);
	}

	if(is_obj_ready_to_be_fetched())
	{
		// Still OAM entries to serve for this X: start another OBJ fetch.
		m_of.entry = m_oam_entries[m_lx].back();
		m_oam_entries[m_lx].pop_back();

		m_fetcher_tick_selector = &PPU::obj_prefetcher_get_tile_0;
	}
	else
	{
		// Restore the BG/window fetch interrupted by this OBJ fetch, if any.
		if(m_bwf.interrupted_fetch_has_data)
		{
			restore_bg_win_fetch();
		}

		m_fetching_sprite = false;

		m_fetcher_tick_selector = &PPU::pixel_slice_fetcher_idle;
	}
}

void PPU::setup_obj_pixel_slice_fetcher_tile_data_address()
{
	const u8 height_mask = static_cast<u8>(m_lcdc.obj_size ? 0x0F : 0x07);

	const u8 obj_y = static_cast<u8>(m_of.entry.y - TILE_DOUBLE_HEIGHT);
	u8 tile_y = static_cast<u8>((m_ly - obj_y) & height_mask);

	if((m_of.attributes & 0x40) != 0)
	{
		// Take the opposite row within the object height.
		tile_y = static_cast<u8>(tile_y ^ height_mask);
	}

	// The last bit is ignored for 8x16 OBJs.
	const u8 tile_number = m_lcdc.obj_size ? static_cast<u8>(m_of.tile_number & 0xFE) : m_of.tile_number;

	m_psf.tile_data_vram_address = static_cast<u16>(TILE_BYTES * tile_number + TILE_ROW_BYTES * tile_y);
}

void PPU::read_obj_tile_data_low()
{
	m_psf.tile_data_low = m_vram.read(m_psf.tile_data_vram_address);
}

void PPU::read_obj_tile_data_high()
{
	m_psf.tile_data_high = m_vram.read(static_cast<u16>(m_psf.tile_data_vram_address + 1));
}

void PPU::setup_bg_pixel_slice_fetcher_tilemap_tile_address()
{
	const u16 tilemap_x = static_cast<u16>(((m_bwf.lx + m_scx) / TILE_WIDTH) % TILEMAP_WIDTH);
	const u16 tilemap_y = static_cast<u16>(((m_ly + m_scy) / TILE_HEIGHT) % TILEMAP_HEIGHT);

	const u16 tilemap_vram_addr_base = m_lcdc.bg_tile_map ? 0x1C00 : 0x1800;
	const u16 tilemap_vram_addr_slack =
		static_cast<u16>((TILEMAP_WIDTH * TILEMAP_CELL_BYTES * tilemap_y) + tilemap_x);
	m_bwf.tilemap_tile_vram_addr = static_cast<u16>(tilemap_vram_addr_base + tilemap_vram_addr_slack);
}

void PPU::setup_bg_pixel_slice_fetcher_tile_data_address()
{
	const u16 vram_tile_addr =
		m_lcdc.bg_win_tile_data
		? static_cast<u16>(0x0000 + TILE_BYTES * m_bwf.tile_number)
		: static_cast<u16>(0x1000 + TILE_BYTES * static_cast<int8_t>(m_bwf.tile_number));

	// On DMG, SCY is sampled again for both the low and the high byte, which
	// can cause bitplane desync effects. [mealybug/m3_scy_change]
	const u8 tile_y = static_cast<u8>((m_ly + m_scy) % TILE_HEIGHT);

	m_psf.tile_data_vram_address = static_cast<u16>(vram_tile_addr + TILE_ROW_BYTES * tile_y);
}

void PPU::setup_win_pixel_slice_fetcher_tilemap_tile_address()
{
	const u8 tilemap_x = m_wf_tilemap_x++;
	const u8 tilemap_y = static_cast<u8>(m_w_wly / TILE_HEIGHT);

	const u16 tilemap_vram_addr_base = m_lcdc.win_tile_map ? 0x1C00 : 0x1800;
	const u16 tilemap_vram_addr_slack =
		static_cast<u16>((TILEMAP_WIDTH * TILEMAP_CELL_BYTES * tilemap_y) + tilemap_x);
	m_bwf.tilemap_tile_vram_addr = static_cast<u16>(tilemap_vram_addr_base + tilemap_vram_addr_slack);
}

void PPU::setup_win_pixel_slice_fetcher_tile_data_address()
{
	const u16 vram_tile_addr =
		m_lcdc.bg_win_tile_data
		? static_cast<u16>(0x0000 + TILE_BYTES * m_bwf.tile_number)
		: static_cast<u16>(0x1000 + TILE_BYTES * static_cast<int8_t>(m_bwf.tile_number));

	const u8 tile_y = static_cast<u8>(m_w_wly % TILE_HEIGHT);

	m_psf.tile_data_vram_address = static_cast<u16>(vram_tile_addr + TILE_ROW_BYTES * tile_y);
}

void PPU::read_bgwin_tile_number()
{
	m_bwf.tile_number = m_vram.read(m_bwf.tilemap_tile_vram_addr);
}

void PPU::read_bgwin_tile_data_low()
{
	m_psf.tile_data_low = m_vram.read(m_psf.tile_data_vram_address);
}

void PPU::read_bgwin_tile_data_high()
{
	m_psf.tile_data_high = m_vram.read(static_cast<u16>(m_psf.tile_data_vram_address + 1));
}

void PPU::enable_pixel_slice_fetcher_push()
{
	m_pixel_slice_fetcher_push_enabled = true;
}

void PPU::disable_pixel_slice_fetcher_push()
{
	m_pixel_slice_fetcher_push_enabled = false;
}

void PPU::cache_bg_win_fetch()
{
	m_bwf.interrupted_tile_data_low = m_psf.tile_data_low;
	m_bwf.interrupted_tile_data_high = m_psf.tile_data_high;
	m_bwf.interrupted_fetch_has_data = true;
}

void PPU::restore_bg_win_fetch()
{
	m_psf.tile_data_low = m_bwf.interrupted_tile_data_low;
	m_psf.tile_data_high = m_bwf.interrupted_tile_data_high;
	m_bwf.interrupted_fetch_has_data = false;
}

void PPU::fill_bg_fifo()
{
	m_bg_fifo.clear();

	// The two tile bitplanes are expanded into eight color indices, leftmost
	// pixel first (bit 7 of each plane).
	for(int i = 0; i < 8; ++i)
	{
		const u8 shift = static_cast<u8>(7 - i);
		const u8 color = static_cast<u8>(((m_psf.tile_data_low >> shift) & 1) |
		                                 (((m_psf.tile_data_high >> shift) & 1) << 1));
		m_bg_fifo.push_back(BgPixel {color});
	}
}

bool PPU::is_bg_fifo_ready_to_be_popped() const
{
	// The BG FIFO can be popped when it is not empty, no sprite fetch is in
	// progress and there are no pending OAM entries for the current LX (or
	// sprites are disabled in LCDC).
	const bool obj_blocks = !m_oam_entries[m_lx].empty() && m_lcdc.obj_enable;

	return !m_bg_fifo.empty() && !m_fetching_sprite && !obj_blocks;
}

bool PPU::is_obj_ready_to_be_fetched() const
{
	// A sprite fetch can be served when there is a pending OAM entry for the
	// current LX and sprites are enabled in LCDC.
	return !m_oam_entries[m_lx].empty() && m_lcdc.obj_enable;
}

void PPU::handle_pixel_slice_fetcher_push()
{
	if(!m_pixel_slice_fetcher_push_enabled || m_fetching_sprite)
	{
		return;
	}

	const bool can_push_to_bg_fifo = m_bg_fifo.empty();

	if(can_push_to_bg_fifo)
	{
		// If the window activation condition was met during the frame, a
		// glitch pushes a 00 pixel when the push happens at LX == WX and
		// postpones the real push by one dot.
		if(m_w_active_for_frame && m_lx == m_wx && !m_w_just_activated)
		{
			m_bg_fifo.push_back(BgPixel {0});
			return;
		}

		m_pending_bg_fifo_fill = true;

		m_pixel_slice_fetcher_push_enabled = false;

		m_fetcher_tick_selector = &PPU::bgwin_prefetcher_get_tile_0;

		// Sprite fetches are ignored just after a window tile push.
		if(m_w_just_activated)
		{
			m_w_just_activated = false;
			return;
		}
	}

	// If there is a pending OBJ hit, discard the fetched BG pixels (or cache
	// them if they were not pushed yet) and start the OBJ prefetcher.
	if(is_obj_ready_to_be_fetched())
	{
		if(!can_push_to_bg_fifo)
		{
			// The bg/win tile fetched is not thrown away: instead, it is
			// cached so that the fetcher can start with it after the sprite
			// has been merged into the OBJ FIFO.
			cache_bg_win_fetch();
		}

		m_fetching_sprite = true;

		m_of.entry = m_oam_entries[m_lx].back();
		m_oam_entries[m_lx].pop_back();

		obj_prefetcher_get_tile_0();
	}
}

void PPU::handle_pending_bg_fifo_fill()
{
	if(m_pending_bg_fifo_fill)
	{
		m_pending_bg_fifo_fill = false;
		fill_bg_fifo();
	}
}

void PPU::handle_pending_window_activation()
{
	if(m_w_activating)
	{
		activate_window();

		m_w_activating = false;

		m_fetcher_tick_selector = &PPU::bgwin_prefetcher_get_tile_0;
	}
}

void PPU::activate_window()
{
	// Throw away the pixels in the BG FIFO.
	m_bg_fifo.clear();

	m_w_active = true;
	m_w_just_activated = true;

	// Reset the window tile counter.
	m_wf_tilemap_x = 0;
}

void PPU::check_window_activation()
{
	// The window triggers when the frame condition was met, the window is
	// enabled and LX matches WX. On DMG it can also trigger one dot late when
	// LCDC.WIN_ENABLE was off and is turned on at LX == WX + 1.
	// [mealybug/m3_lcdc_win_en_change_multiple_wx]
	if(m_w_active_for_frame && !m_w_active && m_lcdc.win_enable &&
	   (m_lx == m_last_wx || (m_lx == static_cast<u8>(m_last_wx + 1) && !m_last_lcdc.win_enable)))
	{
		setup_fetcher_for_window();
	}
}

void PPU::setup_fetcher_for_window()
{
	m_w_activating = true;

	// Increase the window line counter.
	++m_w_wly;

	// Do not allow the pixel slice fetcher to push to the BG FIFO.
	m_pixel_slice_fetcher_push_enabled = false;

	// The next fetcher tick is skipped.
	m_fetcher_tick_selector = &PPU::pixel_slice_fetcher_idle;
}

// ------- OAM scan helpers -------

void PPU::reset_oam_scan_entries()
{
	for(auto& list : m_oam_entries)
	{
		list.clear();
	}

	m_oam_scan.count = 0;
	m_oam_scan.index = 0;
}

void PPU::oam_scan_read_request()
{
	m_oam_scan.request = m_oam_scan.index;
}

void PPU::oam_scan_flush_read_request()
{
	// The PPU cannot access OAM while DMA is in progress: in that case it does
	// not read at all and the registers hold their old values.
	if(!m_dma_active)
	{
		const u16 address = static_cast<u16>(4u * m_oam_scan.request);
		m_oam_scan_registers.m_y = ppu_oam_read(address);
		m_oam_scan_registers.m_x = ppu_oam_read(static_cast<u16>(address + 1));
	}

	handle_oam_scan_entry();
}

void PPU::handle_oam_scan_entry()
{
	// Do nothing if the 10 sprite limit has already been reached.
	if(m_oam_scan.count < 10)
	{
		const u8 obj_height = static_cast<u8>(8 << (m_lcdc.obj_size ? 1 : 0));

		const u8 entry_y = m_oam_scan_registers.m_y;
		const int obj_y = static_cast<int>(entry_y) - 16;

		if(obj_y <= m_ly && m_ly < obj_y + obj_height)
		{
			const u8 entry_x = m_oam_scan_registers.m_x;

			if(entry_x < 168)
			{
				OamScanEntry entry;
				entry.number = m_oam_scan.index;
				entry.y = entry_y;
				entry.x = entry_x;
				m_oam_entries[entry_x].push(entry);
			}

			++m_oam_scan.count;
		}
	}

	++m_oam_scan.index;
}

// ------- Registers -------

u8 PPU::read_lcd_register(u16 offset)
{
	switch(offset)
	{
		case 0: return read_lcdc();
		case 1: return read_stat();
		case 2: return m_scy;
		case 3: return m_scx;
		case 4: return m_ly;
		case 5: return m_lyc;
		case 6: return m_dma;
		case 7: return m_bgp;
		case 8: return m_obp0;
		case 9: return m_obp1;
		case 10: return m_wy;
		case 11: return m_wx;
		default: return 0;
	}
}

void PPU::write_lcd_register(u16 offset, u8 value)
{
	switch(offset)
	{
		case 0:
			write_lcdc(value);
			m_lcd->m_lcdc = value;
			break;
		case 1:
			write_stat(value);
			m_lcd->m_lcds = static_cast<u8>((m_lcd->m_lcds & 0x07) | (value & 0x78));
			break;
		case 2:
			m_scy = value;
			m_lcd->m_scroll_y = value;
			break;
		case 3:
			m_scx = value;
			m_lcd->m_scroll_x = value;
			break;
		case 4: break;	// LY is read-only
		case 5:
			m_lyc = value;
			m_lcd->m_lyc = value;
			break;
		case 7:
			m_bgp = value;
			m_lcd->m_bg_palette = value;
			break;
		case 8:
			m_obp0 = value;
			m_lcd->m_obj_palette_0 = value;
			break;
		case 9:
			m_obp1 = value;
			m_lcd->m_obj_palette_1 = value;
			break;
		case 10:
			m_wy = value;
			m_lcd->m_win_y = value;
			break;
		case 11:
			m_wx = value;
			m_lcd->m_win_x = value;
			break;
		default: break;
	}
}

u8 PPU::read_lcdc() const
{
	return static_cast<u8>((m_lcdc.enable ? 0x80 : 0x00) | (m_lcdc.win_tile_map ? 0x40 : 0x00) |
	                       (m_lcdc.win_enable ? 0x20 : 0x00) | (m_lcdc.bg_win_tile_data ? 0x10 : 0x00) |
	                       (m_lcdc.bg_tile_map ? 0x08 : 0x00) | (m_lcdc.obj_size ? 0x04 : 0x00) |
	                       (m_lcdc.obj_enable ? 0x02 : 0x00) | (m_lcdc.bg_win_enable ? 0x01 : 0x00));
}

void PPU::write_lcdc(u8 value)
{
	const bool enable = (value & 0x80) != 0;
	if(enable != m_lcdc.enable)
	{
		// Enabling or disabling the PPU happens instantly.
		if(enable)
		{
			turn_on();
		}
		else
		{
			turn_off();
		}

		m_lcdc.enable = enable;
	}

	write_lcdc_real(value);
}

u8 PPU::read_stat() const
{
	return static_cast<u8>(0x80 | (m_stat.lyc_eq_ly_int ? 0x40 : 0x00) | (m_stat.oam_int ? 0x20 : 0x00) |
	                       (m_stat.vblank_int ? 0x10 : 0x00) | (m_stat.hblank_int ? 0x08 : 0x00) |
	                       (m_stat.lyc_eq_ly ? 0x04 : 0x00) | m_stat.mode);
}

void PPU::write_stat(u8 value)
{
	// A STAT write takes effect one T-cycle later on DMG; during this cycle it
	// behaves as if FF had been written, i.e. all sources enabled.
	m_pending_stat.countdown = 1;
	m_pending_stat.value = value;

	m_stat.lyc_eq_ly_int = true;
	m_stat.oam_int = true;
	m_stat.vblank_int = true;
	m_stat.hblank_int = true;
}

// ------- Memory -------

u8 PPU::ppu_oam_read(u16 address)
{
	switch(address % 4)
	{
		case 0: return m_oam_ram[address / 4].m_y;
		case 1: return m_oam_ram[address / 4].m_x;
		case 2: return m_oam_ram[address / 4].m_tile;
		case 3: return m_oam_ram[address / 4].m_flags;
		default: return 0;
	}
}

void PPU::ppu_oam_write(u16 address, u8 value)
{
	ppu_oam_write_dma(address, value);
}

void PPU::ppu_oam_write_dma(u16 address, u8 value)
{
	switch(address % 4)
	{
		case 0: m_oam_ram[address / 4].m_y = value; break;
		case 1: m_oam_ram[address / 4].m_x = value; break;
		case 2: m_oam_ram[address / 4].m_tile = value; break;
		case 3: m_oam_ram[address / 4].m_flags = value; break;
		default: break;
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

// ------- Output -------

void PPU::push_pixel(u8 color)
{
	const int x = static_cast<int>(m_lx) - 8;
	const int y = static_cast<int>(m_ly);

	if(x >= 0 && x < PPU::XRES && y >= 0 && y < PPU::YRES)
	{
		m_video_buffer[static_cast<size_t>(y) * PPU::XRES + x] = color;
	}
}

void PPU::sync_lcd_from_ppu()
{
	m_lcd->m_ly = m_ly;
	m_lcd->m_lyc = m_lyc;
	m_lcd->m_lcdc = read_lcdc();
	m_lcd->m_scroll_y = m_scy;
	m_lcd->m_scroll_x = m_scx;
	m_lcd->m_bg_palette = m_bgp;
	m_lcd->m_obj_palette_0 = m_obp0;
	m_lcd->m_obj_palette_1 = m_obp1;
	m_lcd->m_win_y = m_wy;
	m_lcd->m_win_x = m_wx;
	m_lcd->m_lcds = static_cast<u8>(read_stat() & 0x7F);
}

void PPU::reset()
{
	m_lcdc.enable = !m_boot_rom;
	m_lcdc.win_tile_map = false;
	m_lcdc.win_enable = false;
	m_lcdc.bg_win_tile_data = !m_boot_rom;
	m_lcdc.bg_tile_map = false;
	m_lcdc.obj_size = false;
	m_lcdc.obj_enable = false;
	m_lcdc.bg_win_enable = !m_boot_rom;

	m_stat.lyc_eq_ly_int = false;
	m_stat.oam_int = false;
	m_stat.vblank_int = false;
	m_stat.hblank_int = false;
	m_stat.lyc_eq_ly = !m_boot_rom;
	m_stat.mode = m_boot_rom ? OAM_SCAN : VBLANK;

	m_scy = 0;
	m_scx = 0;
	m_ly = 0;
	m_lyc = 0;
	m_dma = 0xFF;
	m_bgp = m_boot_rom ? 0x00 : 0xFC;
	m_obp0 = 0;
	m_obp1 = 0;
	m_wy = 0;
	m_wx = 0;

	m_tick_selector = m_boot_rom ? &PPU::oam_scan_after_turn_on : &PPU::vblank_last_line_7;

	m_last_stat_irq = false;
	m_enable_lyc_eq_ly_irq = true;
	m_pending_stat_irq = false;

	m_dots = m_boot_rom ? 0 : 395;
	m_lx = m_boot_rom ? 0 : 168;

	m_mode = static_cast<Mode>(m_stat.mode);

	m_last_ly = m_ly;
	m_last_lyc = m_lyc;
	m_last_bgp = m_bgp;
	m_last_wx = 0;
	m_last_lcdc = m_lcdc;

	m_pending_stat.countdown = 0;
	m_pending_stat.value = 0;

	m_bg_fifo.clear();
	m_obj_fifo.clear();

	for(auto& list : m_oam_entries)
	{
		list.clear();
	}

	m_pixel_slice_fetcher_push_enabled = !m_boot_rom;
	m_pending_bg_fifo_fill = false;
	m_fetching_sprite = false;

	m_is_glitched_line_0 = false;
	m_glitched_line_0_hblank_delay = 0;
	m_vblank_last_line_stat_mode_hblank_glitch = false;

	m_oam_scan_registers.m_y = m_boot_rom ? 0x00 : 0x63;
	m_oam_scan_registers.m_x = m_boot_rom ? 0x00 : 0x45;

	m_oam_scan.count = m_boot_rom ? 0 : 1;
	m_oam_scan.index = m_boot_rom ? 0 : 40;
	m_oam_scan.request = 0;

	m_initial_scx.to_discard = 0;
	m_initial_scx.discarded = 0;

	m_w_active_for_frame = false;
	m_w_wly = 0xFF;
	m_w_activating = false;
	m_w_active = false;
	m_w_just_activated = false;
	m_wf_tilemap_x = 0;

	m_bwf.lx = m_boot_rom ? 0 : 168;
	m_bwf.tile_number = 0;
	m_bwf.interrupted_fetch_has_data = false;

	m_psf.tile_data_vram_address = 0;
	m_psf.tile_data_low = 0;
	m_psf.tile_data_high = 0;

	m_fetcher_tick_selector = m_boot_rom ? &PPU::bg_prefetcher_get_tile_0 : &PPU::pixel_slice_fetcher_idle;

	// The compile-time oracle always acquires OAM at reset (the runtime boot ROM
	// flag only changes the register defaults, not the bus state).
	m_oam_bus.acquire(VideoDevice::Ppu, VideoAccess::Read);
	m_oam_bus.acquire(VideoDevice::Ppu, VideoAccess::Write);
}

