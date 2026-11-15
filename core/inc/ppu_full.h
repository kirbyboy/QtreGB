#ifndef QTREGB_PPU_FULL_H
#define QTREGB_PPU_FULL_H

#include <functional>
#include <memory>
#include <vector>

#include "types.h"
#include "memory.h"
#include "ibus.h"
#include "oam_entry.h"
#include "videobus.h"
#include "lcd.h"

#include "docboy/bus/oambus.h"
#include "docboy/bus/vrambus.h"
#include "docboy/dma/dma.h"
#include "docboy/interrupts/interrupts.h"
#include "docboy/lcd/lcd.h"

class Ppu;

namespace qtreGB
{

// Full DocBoy PPU port, selected with QTR_PPU_FULL. It keeps the DocBoy
// pixel pipeline, mode state machine and STAT/VBlank timing, and exposes the
// same public interface as the legacy PPU.
class PPUFull
{
	public:
		using FrameCallback = std::function<void(bool)>;

		static const int LINES_PER_FRAME;
		static const int TICKS_PER_LINE;
		static const int YRES;
		static const int XRES;

		PPUFull(LCD *lcd);
		~PPUFull();

		void set_interrupt_callback(InterruptCallback cb);
		void set_frame_callback(FrameCallback cb);

		void ppu_init(bool boot_rom = false);
		void ppu_tick();
		void ppu_sync_lcd_power();
		void set_dma_active(bool active);

		u8 ppu_oam_read(u16 address);
		void ppu_oam_write(u16 address, u8 value);
		void ppu_oam_write_dma(u16 address, u8 value);

		u8 ppu_vram_read(u16 address);
		void ppu_vram_write(u16 address, u8 value);

		const std::vector<u8>& get_video_buffer() const { return m_video_buffer; }

		bool is_oam_read_locked() const { return m_oam_bus.is_acquired(VideoAccess::Read); }
		bool is_oam_write_locked() const { return m_oam_bus.is_acquired(VideoAccess::Write); }
		bool is_vram_read_locked() const { return m_vram_bus.is_acquired(VideoAccess::Read); }
		bool is_vram_write_locked() const { return m_vram_bus.is_acquired(VideoAccess::Write); }

		void connect_bus(IBus *bus) { m_bus = bus; }

		// LCD register access routed from the bus.
		u8 read_lcd_register(u16 offset);
		void write_lcd_register(u16 offset, u8 value);

		// Debug-only accessors used by the harness and temp tools.
		u16 debug_dots() const;
		u8 debug_lx() const;
		u8 debug_fetch_x() const { return 0; }
		u8 debug_pushed_x() const { return 0; }
		int debug_fetch_phase() const { return 0; }
		bool debug_fetch_window() const { return false; }
		bool debug_fetching_sprite() const;
		size_t debug_bg_fifo() const;
		size_t debug_obj_fifo() const;
		bool debug_oam_pending() const { return false; }
		u8 debug_oam_served() const { return 0; }
		u8 debug_oam_entries() const { return 0; }
		int debug_obj_phase() const { return 0; }
		u8 debug_obj_wait() const { return 0; }
		u8 debug_post_wait() const { return 0; }

	private:
		void bind_adapters();
		void request_interrupt(InterruptType type);
		void sync_lcd_from_ppu();
		void push_pixel(u8 color);

		LCD *m_lcd = nullptr;
		IBus *m_bus = nullptr;
		Memory m_vram;
		std::vector<OAMEntry> m_oam_ram;
		std::vector<u8> m_video_buffer;
		VideoBus m_oam_bus;
		VideoBus m_vram_bus;

		Lcd m_docboy_lcd;
		Interrupts m_docboy_interrupts;
		Dma m_docboy_dma;
		VramBus m_docboy_vram_bus;
		OamBus m_docboy_oam_bus;
		std::unique_ptr<Ppu> m_docboy_ppu;

		InterruptCallback m_request_interrupt;
		FrameCallback m_frame_done;
		bool m_boot_rom = false;
};

} // namespace qtreGB

#endif // QTREGB_PPU_FULL_H
