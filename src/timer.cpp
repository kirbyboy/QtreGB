#include "timer.h"
#include "QtreGB.h"

void Timer::set_interrupt_callback(InterruptCallback cb)
{
	m_interrupt_handler = cb;
}

void Timer::timer_init()
{
	m_div = 0xAC00;
}

void Timer::timer_tick()
{
	u16 prev_div = m_div;
	bool timer_update = false;
	m_div++;

	switch(m_tac & 0x03)
	{
		case 0x00:
			timer_update = (m_div & (1 << 9)) && (!(prev_div & (1 << 9)));
			break;
		case 0x01:
			timer_update = (m_div & (1 << 3)) && (!(prev_div & (1 << 3)));
			break;
		case 0x02:
			timer_update = (m_div & (1 << 5)) && (!(prev_div & (1 << 5)));
			break;
		case 0x03:
			timer_update = (m_div & (1 << 7)) && (!(prev_div & (1 << 7)));
			break;
	}

	if(timer_update && (m_tac & 0x04))
	{
		if(m_tima == 0xFF)
		{
			m_tima = m_tma;

			if(m_interrupt_handler)
			{
				m_interrupt_handler(InterruptType::IT_TIMER);
			}
		}
		else
		{
			m_tima++;
		}
	}


}
