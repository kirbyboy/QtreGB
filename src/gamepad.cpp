#include "gamepad.h"

using namespace qtreGB;

void Gamepad::set_interrupt_callback(InterruptCallback cb)
{
	m_interrupt_handler = cb;
}

u8 Gamepad::get_state()
{
	u8 state = 0xFF;

	if(!m_select_buttons)
	{
		if(m_button_start)
		{
			state &= ~(1 << 3);
		}
		if(m_button_select)
		{
			state &= ~(1 << 2);
		}
		if(m_button_b)
		{
			state &= ~(1 << 1);
		}
		if(m_button_a)
		{
			state &= ~(1 << 0);
		}
		state &= ~(1 << 5);
	}

	if(!m_select_directions)
	{
		if(m_button_down)
		{
			state &= ~(1 << 3);
		}
		if(m_button_up)
		{
			state &= ~(1 << 2);
		}
		if(m_button_left)
		{
			state &= ~(1 << 1);
		}
		if(m_button_right)
		{
			state &= ~(1 << 0);
		}
		state &= ~(1 << 4);
	}


	return state;
}

void Gamepad::set_selection(u8 selection)
{
	m_select_directions = selection & 0x10;
	m_select_buttons = selection & 0x20;
}
