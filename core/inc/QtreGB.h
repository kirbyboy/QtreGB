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
#include "phase_clock.h"

namespace qtreGB
{

class QtreGB
{
	public:
		QtreGB();
		~QtreGB();

		void tick();
		void cycle();

		Phase get_phase() const { return m_clock.get(); }


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

		void tick_t0();
		void tick_t1();
		void tick_t2();
		void tick_t3();
		void tick_once();

		PhaseClock m_clock;
		BusCpuView m_bus_cpu;

};

} // namespace qtreGB

#endif
