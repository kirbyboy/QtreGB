#ifndef QTREGB_QTREGB_H
#define QTREGB_QTREGB_H

#include <vector>
#include <memory>

#include "bus.h"
#include "Z80GB.h"
#include "memory.h"
#include "cart.h"
#include "io.h"
#include "timer.h"
#include "ppu.h"
#include "dma.h"
#include "lcd.h"
#include "gamepad.h"
#include "interrupt_controller.h"
#include "bus_cpu_view.h"
#include "error_reporter.h"

namespace qtreGB
{

class QtreGB
{
	public:
		QtreGB();
		~QtreGB();

		void tick();

		bool load_cart(std::vector<u8> cart);

		InterruptController m_interrupt_controller;
		ErrorReporter m_errors;
		Z80GB m_cpu;
		Cart m_cart;
		Memory m_wram;
		Memory m_hram;
		IO m_io;
		Timer m_timer;
		LCD m_lcd;
		PPU m_ppu;
		DMA m_dma;
		Gamepad m_gamepad;
		Bus m_bus;


		bool m_frame_ready;

		bool get_frame_ready();

	private:
		void set_frame_ready(bool value);

		// Reproduces the VRAM the DMG boot ROM leaves behind, for the case
		// where the boot ROM is not emulated. Called on every successful
		// cartridge load; must be skipped once a boot ROM can be run.
		void apply_post_boot_vram();

		BusCpuView m_bus_cpu;

};

} // namespace qtreGB

#endif
