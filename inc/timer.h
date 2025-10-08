#ifndef QTREGB_TIMER_H
#define QTREGB_TIMER_H

#include <functional>
#include "interrupt_controller.h"
#include "common.h"

class QtreGB;

class Timer
{
	public:
		u16 m_div;
		u8 m_tima;
		u8 m_tma;
		u8 m_tac;

		void set_interrupt_callback(InterruptCallback cb);
		void timer_init();
		void timer_tick();

	private:
		InterruptCallback m_interrupt_handler;
};

#endif
