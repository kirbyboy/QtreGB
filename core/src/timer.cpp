#include "timer.h"
#include "QtreGB.h"

using namespace qtreGB;

void Timer::set_interrupt_callback(InterruptCallback cb)
{
	m_interrupt_handler = cb;
}

void Timer::timer_init()
{
	m_div = 0xABCC;
	m_tima = 0x00;
	m_tma = 0x00;
	m_tac = 0x00;
	m_timer_state = 0;
	m_last_div_signal = false;
}

void Timer::timer_tick()
{
	handle_pending_tima_reload();
	set_div(static_cast<u16>(m_div + 4));
}

u8 Timer::read_div()
{
	return static_cast<u8>(m_div >> 8);
}

void Timer::write_div()
{
	set_div(0);
}

u8 Timer::read_tima()
{
	return m_tima;
}

void Timer::write_tima(u8 value)
{
	if(m_timer_state != 1)
	{
		m_tima = value;
		m_timer_state = 0;
	}
}

u8 Timer::read_tma()
{
	return m_tma;
}

void Timer::write_tma(u8 value)
{
	m_tma = value;

	if(m_timer_state == 1)
	{
		m_tima = value;
	}
}

u8 Timer::read_tac()
{
	// Pan Docs / schematics: only TAC bits 0-2 are R/W. Bits 3-7 are not wired
	// to the write bus, so they never latch anything and always read back as 1
	// (the data bus idles high). This is why read-back is NOT a round-trip.
	return m_tac | 0xF8;
}

void Timer::write_tac(u8 value)
{
	m_tac = value & 0x07;
	on_falling_edge_inc_tima();
}

void Timer::serialize(TaggedSerializer& s)
{
	s.process("div", m_div);
	s.process("tima", m_tima);
	s.process("tma", m_tma);
	s.process("tac", m_tac);
	s.process("timer_state", m_timer_state);
	s.process("last_div_signal", m_last_div_signal);
}

void Timer::set_div(u16 value)
{
	m_div = value;
	on_falling_edge_inc_tima();
}

void Timer::inc_tima()
{
	if(++m_tima == 0)
	{
		m_timer_state = 2;
	}
}

void Timer::on_falling_edge_inc_tima()
{
	const bool tac_div_bit = (m_tac & 0x04) && (m_div & selected_bit());
	if(m_last_div_signal && !tac_div_bit)
	{
		inc_tima();
	}
	m_last_div_signal = tac_div_bit;
}

void Timer::handle_pending_tima_reload()
{
	if(m_timer_state != 0)
	{
		if(--m_timer_state == 1)
		{
			m_tima = m_tma;
			if(m_interrupt_handler)
			{
				m_interrupt_handler(InterruptType::IT_TIMER);
			}
		}
	}
}

u16 Timer::selected_bit()
{
	switch(m_tac & 0x03)
	{
		case 0x00: return static_cast<u16>(1u << 9);
		case 0x01: return 1 << 3;
		case 0x02: return 1 << 5;
		case 0x03: return 1 << 7;
	}
	return 0;
}
