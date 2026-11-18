#include "timer.h"
#include "QtreGB.h"
#include "state_stream.h"

using namespace qtreGB;

namespace
{

// Component-local field tags. See documentation/Game Boy/savestate-core-format.md.
constexpr u16 TIMER_DIV = 0x0001;
constexpr u16 TIMER_TIMA = 0x0002;
constexpr u16 TIMER_TMA = 0x0003;
constexpr u16 TIMER_TAC = 0x0004;
constexpr u16 TIMER_STATE = 0x0005;
constexpr u16 TIMER_LAST_DIV_SIGNAL = 0x0006;

} // namespace

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

void Timer::save_state(StateWriter& writer) const
{
	writer.write_u16(TIMER_DIV, m_div);
	writer.write_u8(TIMER_TIMA, m_tima);
	writer.write_u8(TIMER_TMA, m_tma);
	writer.write_u8(TIMER_TAC, m_tac);
	writer.write_u8(TIMER_STATE, m_timer_state);
	writer.write_bool(TIMER_LAST_DIV_SIGNAL, m_last_div_signal);
}

bool Timer::load_state(StateReader& reader)
{
	u16 div = 0;
	u8 tima = 0;
	u8 tma = 0;
	u8 tac = 0;
	u8 timer_state = 0;
	bool last_div_signal = false;

	if(!reader.read_u16(TIMER_DIV, div) ||
	   !reader.read_u8(TIMER_TIMA, tima) ||
	   !reader.read_u8(TIMER_TMA, tma) ||
	   !reader.read_u8(TIMER_TAC, tac) ||
	   !reader.read_u8(TIMER_STATE, timer_state) ||
	   !reader.read_bool(TIMER_LAST_DIV_SIGNAL, last_div_signal))
	{
		return false;
	}

	// 0=None, 1=Reload, 2=Pending; TAC only stores its low three bits.
	if(timer_state > 2 || tac > 0x07)
	{
		reader.mark_error(StateError::BadValue);
		return false;
	}

	m_div = div;
	m_tima = tima;
	m_tma = tma;
	m_tac = tac;
	m_timer_state = timer_state;
	m_last_div_signal = last_div_signal;
	return true;
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
