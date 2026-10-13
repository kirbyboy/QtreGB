#include "timer.h"
#include "QtreGB.h"

using namespace qtreGB;

void Timer::set_interrupt_callback(InterruptCallback cb)
{
	m_interrupt_handler = cb;
}

void Timer::timer_init()
{
	m_div = 0xAC00;
	m_tima = 0x00;
	m_tma = 0x00;
	m_tac = 0x00; // TAC is stored as 0x00 here. Software still reads 0xF8 back, because the unmapped upper bits are applied on read, not stored.
	m_reload_pending = 0;
	m_load_active = 0;
	m_tima_write_block = false;
	m_div_increment_absorbed = false;
	m_cpu_phase = false;
}

void Timer::begin_cpu_phase()
{
	m_cpu_phase = true;
}

void Timer::timer_tick()
{
	bool old_signal = signal();
	if(m_div_increment_absorbed)
	{
		m_div_increment_absorbed = false;
	}
	else
	{
		m_div++;
	}
	bool new_signal = signal();

	if(m_reload_pending > 0)
	{
		m_reload_pending --;

		if(m_reload_pending == 0)
		{
			m_tima = m_tma;
			m_load_active = 4;

			if(m_interrupt_handler)
			{
				m_interrupt_handler(InterruptType::IT_TIMER);
			}
		}
	}
	else if(m_load_active > 0)
	{
		m_load_active --;
	}
	else if(old_signal && !new_signal)
	{
		if(!m_tima_write_block)
		{
			apply_timer_tick();
		}
	}


	m_tima_write_block = false;
	m_cpu_phase = false;
}

u8 Timer::read_div()
{
	if(m_cpu_phase)
	{
		return static_cast<u8>(div_end_of_cycle() >> 8);
	}
	return m_div >> 8;
}

void Timer::write_div()
{
	// The write cycle can raise a falling edge twice: the absorbed increment
	// (k -> k+1) and the reset itself (last value -> 0). Tick if either side
	// of the increment held the selected bit.
	const u16 sel = selected_bit();
	const bool old_signal = (m_tac & 0x04) &&
		((m_div & sel) || (div_end_of_cycle() & sel));
	m_div = 0;
	m_div_increment_absorbed = true;
	if(old_signal)
	{
		apply_timer_tick();
	}
}

u8 Timer::read_tima()
{
	if(m_cpu_phase)
	{
		return peek_tima();
	}
	return m_tima;
}

void Timer::write_tima(u8 value)
{
	// End-of-cycle judgment (same convention as reads): pending>1 is cycle A,
	// the write cancels the reload. pending==1 or load_active>1 means the
	// reload/load still lands this T-cycle, so hardware drops the write (MTS C).
	if(m_reload_pending > 1)
	{
		m_reload_pending = 0;
		m_tima = value;
		m_tima_write_block = true;
	}
	else if(m_reload_pending == 0 && m_load_active <= 1)
	{
		m_tima = value;
		m_tima_write_block = true;
	}
}

u8 Timer::read_tma()
{
	return m_tma;
}

void Timer::write_tma(u8 value)
{
	m_tma = value;

	// Copy into TIMA only while the load is still active past this cycle;
	// at load_active==1 the load window closes with the write (MTS tma C).
	if(m_load_active > 1)
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
	// Like write_div: both edges of the gate are evaluated on the
	// end-of-cycle divider value (see div_end_of_cycle).
	const u16 div = div_end_of_cycle();
	const bool old_signal = (m_tac & 0x04) && (div & selected_bit());
	// Only the lower 3 bits are connected; the upper 5 are discarded.
	m_tac = value & 0x07;
	const bool new_signal = (m_tac & 0x04) && (div & selected_bit());
	if(old_signal && !new_signal)
	{
		apply_timer_tick();
	}
}

void Timer::serialize(TaggedSerializer& s)
{
	s.process("div", m_div);
	s.process("tima", m_tima);
	s.process("tma", m_tma);
	s.process("tac", m_tac);
	s.process("timer_reload_pending", m_reload_pending);
	s.process("tima_load_active", m_load_active);
	s.process("tima_write_block", m_tima_write_block);
	s.process("div_increment_absorbed", m_div_increment_absorbed);
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

bool Timer::signal()
{
	return (m_tac & 0x04) && (m_div & selected_bit());
}

// Reads are sampled at the end of the T-cycle: dry-run the pending
// timer_tick() (no mutation) instead of returning the pre-increment state.

u16 Timer::div_end_of_cycle()
{
	if(m_div_increment_absorbed)
	{
		return m_div;
	}
	return static_cast<u16>(m_div + 1);
}

bool Timer::edge_pending()
{
	bool old_signal = signal();
	const u16 div = div_end_of_cycle();
	bool new_signal = (m_tac & 0x04) && (div & selected_bit());
	return old_signal && !new_signal;
}

u8 Timer::peek_tima()
{
	if(m_reload_pending > 0)
	{
		// timer_tick() decrements now; reaching 0 loads TMA into TIMA.
		return (m_reload_pending == 1) ? m_tma : m_tima;
	}
	if(m_load_active > 0)
	{
		return m_tima;
	}
	if(edge_pending() && !m_tima_write_block)
	{
		return static_cast<u8>(m_tima + 1);
	}
	return m_tima;
}

void qtreGB::Timer::apply_timer_tick()
{
	if(m_tima == 0xFF)
	{
		m_tima = 0x00;
		m_reload_pending = 4;
	}
	else
	{
		m_tima++;
	}

}
