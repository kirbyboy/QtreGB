#ifdef QTR_PPU_FULL

#include "ppu_full.h"

#include "docboy/ppu/ppu.h"

#include <algorithm>

using namespace qtreGB;
using namespace std;

bool g_docboy_bootrom = false;

const int PPUFull::LINES_PER_FRAME = 154;
const int PPUFull::TICKS_PER_LINE = 456;
const int PPUFull::YRES = 144;
const int PPUFull::XRES = 160;

PPUFull::PPUFull(LCD *lcd) :
m_lcd(lcd),
m_vram(0x2000),
m_oam_ram(40),
m_video_buffer(static_cast<size_t>(PPUFull::YRES) * PPUFull::XRES)
{
	m_docboy_ppu = make_unique<Ppu>(
		m_docboy_lcd,
		m_docboy_interrupts,
		VramBus::View<Device::Ppu>(m_docboy_vram_bus),
		OamBus::View<Device::Ppu>(m_docboy_oam_bus),
		m_docboy_dma);
	bind_adapters();
}

PPUFull::~PPUFull() = default;

void PPUFull::bind_adapters()
{
	m_docboy_vram_bus.bind(
		[this](u16 address) { return m_vram.read(address); },
		[this](bool acquire)
		{
			if(acquire)
			{
				m_vram_bus.acquire(VideoDevice::Ppu, VideoAccess::Read);
				m_vram_bus.acquire(VideoDevice::Ppu, VideoAccess::Write);
			}
			else
			{
				m_vram_bus.release(VideoDevice::Ppu, VideoAccess::Read);
				m_vram_bus.release(VideoDevice::Ppu, VideoAccess::Write);
			}
		});

	m_docboy_oam_bus.bind(
		[this](u16 address)
		{
			return ppu_oam_read(address >= 0xFE00 ? static_cast<u16>(address - 0xFE00) : address);
		},
		[this](u16 address, u8 value)
		{
			ppu_oam_write(address >= 0xFE00 ? static_cast<u16>(address - 0xFE00) : address, value);
		},
		[this](bool acquire)
		{
			if(acquire)
			{
				m_oam_bus.acquire(VideoDevice::Ppu, VideoAccess::Read);
				m_oam_bus.acquire(VideoDevice::Ppu, VideoAccess::Write);
			}
			else
			{
				m_oam_bus.release(VideoDevice::Ppu, VideoAccess::Read);
				m_oam_bus.release(VideoDevice::Ppu, VideoAccess::Write);
			}
		},
		[this]() { return m_docboy_dma.is_active(); });

	m_docboy_lcd.set_push_callback([this](Lcd::Pixel color) { push_pixel(color); });
	m_docboy_lcd.set_new_frame_callback([this]()
	{
		if(m_frame_done)
		{
			m_frame_done(true);
		}
	});

	m_docboy_interrupts.set_callback([this](Interrupts::InterruptType type)
	{
		request_interrupt(type == Interrupts::InterruptType::VBlank
			? InterruptType::IT_VBLANK
			: InterruptType::IT_LCD_STAT);
	});
}

void PPUFull::request_interrupt(InterruptType type)
{
	if(m_request_interrupt)
	{
		m_request_interrupt(type);
	}
}

void PPUFull::set_interrupt_callback(InterruptCallback cb)
{
	m_request_interrupt = cb;
}

void PPUFull::set_frame_callback(FrameCallback cb)
{
	m_frame_done = cb;
}

void PPUFull::ppu_init(bool boot_rom)
{
	m_boot_rom = boot_rom;
	m_vram_bus.reset();
	m_oam_bus.reset();
	m_video_buffer.assign(static_cast<size_t>(PPUFull::YRES) * PPUFull::XRES, 0);

	// The legacy LCD object remains as a passive mirror for debug output; the
	// DocBoy PPU owns the LCD registers and timing.
	m_lcd->lcd_init(boot_rom);

	g_docboy_bootrom = boot_rom;
	m_docboy_ppu->reset();
	sync_lcd_from_ppu();
}

void PPUFull::ppu_tick()
{
	m_docboy_ppu->tick();
	sync_lcd_from_ppu();
}

void PPUFull::set_dma_active(bool active)
{
	m_docboy_dma.set_active(active);
}

void PPUFull::ppu_sync_lcd_power()
{
	// LCDC writes go through write_lcd_register(), which updates the DocBoy
	// PPU immediately; no deferred power transition is needed here.
}

void PPUFull::push_pixel(u8 color)
{
	const int x = static_cast<int>(m_docboy_ppu->debug_lx()) - 8;
	const int y = static_cast<int>(m_docboy_ppu->ly);
	if(x >= 0 && x < PPUFull::XRES && y >= 0 && y < PPUFull::YRES)
	{
		m_video_buffer[static_cast<size_t>(y) * PPUFull::XRES + x] = color;
	}
}

void PPUFull::sync_lcd_from_ppu()
{
	const Ppu& p = *m_docboy_ppu;
	m_lcd->m_ly = p.ly;
	m_lcd->m_lyc = p.lyc;
	m_lcd->m_lcdc = p.read_lcdc();
	m_lcd->m_scroll_y = p.scy;
	m_lcd->m_scroll_x = p.scx;
	m_lcd->m_bg_palette = p.bgp;
	m_lcd->m_obj_palette_0 = p.obp0;
	m_lcd->m_obj_palette_1 = p.obp1;
	m_lcd->m_win_y = p.wy;
	m_lcd->m_win_x = p.wx;
	m_lcd->m_lcds = static_cast<u8>(p.read_stat() & 0x7F);
}

u8 PPUFull::read_lcd_register(u16 offset)
{
	const Ppu& p = *m_docboy_ppu;
	switch(offset)
	{
		case 0: return p.read_lcdc();
		case 1: return p.read_stat();
		case 2: return p.scy;
		case 3: return p.scx;
		case 4: return p.ly;
		case 5: return p.lyc;
		case 7: return p.bgp;
		case 8: return p.obp0;
		case 9: return p.obp1;
		case 10: return p.wy;
		case 11: return p.wx;
		default: return 0;
	}
}

void PPUFull::write_lcd_register(u16 offset, u8 value)
{
	Ppu& p = *m_docboy_ppu;
	switch(offset)
	{
		case 0:
			p.write_lcdc(value);
			m_lcd->m_lcdc = value;
			break;
		case 1:
			p.write_stat(value);
			m_lcd->m_lcds = static_cast<u8>((m_lcd->m_lcds & 0x07) | (value & 0x78));
			break;
		case 2:
			p.scy = value;
			m_lcd->m_scroll_y = value;
			break;
		case 3:
			p.scx = value;
			m_lcd->m_scroll_x = value;
			break;
		case 5:
			p.lyc = value;
			m_lcd->m_lyc = value;
			break;
		case 7:
			p.bgp = value;
			m_lcd->m_bg_palette = value;
			break;
		case 8:
			p.obp0 = value;
			m_lcd->m_obj_palette_0 = value;
			break;
		case 9:
			p.obp1 = value;
			m_lcd->m_obj_palette_1 = value;
			break;
		case 10:
			p.wy = value;
			m_lcd->m_win_y = value;
			break;
		case 11:
			p.wx = value;
			m_lcd->m_win_x = value;
			break;
		default:
			break;
	}
}

u8 PPUFull::ppu_oam_read(u16 address)
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

void PPUFull::ppu_oam_write(u16 address, u8 value)
{
	ppu_oam_write_dma(address, value);
}

void PPUFull::ppu_oam_write_dma(u16 address, u8 value)
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

u8 PPUFull::ppu_vram_read(u16 address)
{
	return m_vram.read(address);
}

void PPUFull::ppu_vram_write(u16 address, u8 value)
{
	m_vram.write(address, value);
}

u16 PPUFull::debug_dots() const
{
	return m_docboy_ppu ? m_docboy_ppu->debug_dots() : 0;
}

u8 PPUFull::debug_lx() const
{
	return m_docboy_ppu ? m_docboy_ppu->debug_lx() : 0;
}

bool PPUFull::debug_fetching_sprite() const
{
	return m_docboy_ppu ? m_docboy_ppu->debug_fetching() : false;
}

size_t PPUFull::debug_bg_fifo() const
{
	return m_docboy_ppu ? m_docboy_ppu->debug_fifo() : 0;
}

size_t PPUFull::debug_obj_fifo() const
{
	return m_docboy_ppu ? m_docboy_ppu->debug_obj_fifo() : 0;
}

#endif // QTR_PPU_FULL
