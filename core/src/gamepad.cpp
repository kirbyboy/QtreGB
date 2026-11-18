#include "gamepad.h"
#include "state_stream.h"

using namespace qtreGB;

namespace
{

constexpr u16 PAD_START = 0x0001;
constexpr u16 PAD_SELECT = 0x0002;
constexpr u16 PAD_B = 0x0003;
constexpr u16 PAD_A = 0x0004;
constexpr u16 PAD_DOWN = 0x0005;
constexpr u16 PAD_UP = 0x0006;
constexpr u16 PAD_LEFT = 0x0007;
constexpr u16 PAD_RIGHT = 0x0008;
constexpr u16 PAD_SELECT_DIRECTIONS = 0x0009;
constexpr u16 PAD_SELECT_BUTTONS = 0x000A;

} // namespace

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

void Gamepad::save_state(StateWriter& writer) const
{
	writer.write_bool(PAD_START, m_button_start);
	writer.write_bool(PAD_SELECT, m_button_select);
	writer.write_bool(PAD_B, m_button_b);
	writer.write_bool(PAD_A, m_button_a);
	writer.write_bool(PAD_DOWN, m_button_down);
	writer.write_bool(PAD_UP, m_button_up);
	writer.write_bool(PAD_LEFT, m_button_left);
	writer.write_bool(PAD_RIGHT, m_button_right);
	writer.write_bool(PAD_SELECT_DIRECTIONS, m_select_directions);
	writer.write_bool(PAD_SELECT_BUTTONS, m_select_buttons);
}

bool Gamepad::load_state(StateReader& reader)
{
	bool start = false;
	bool select = false;
	bool b = false;
	bool a = false;
	bool down = false;
	bool up = false;
	bool left = false;
	bool right = false;
	bool directions = false;
	bool buttons = false;

	if(!reader.read_bool(PAD_START, start) ||
	   !reader.read_bool(PAD_SELECT, select) ||
	   !reader.read_bool(PAD_B, b) ||
	   !reader.read_bool(PAD_A, a) ||
	   !reader.read_bool(PAD_DOWN, down) ||
	   !reader.read_bool(PAD_UP, up) ||
	   !reader.read_bool(PAD_LEFT, left) ||
	   !reader.read_bool(PAD_RIGHT, right) ||
	   !reader.read_bool(PAD_SELECT_DIRECTIONS, directions) ||
	   !reader.read_bool(PAD_SELECT_BUTTONS, buttons))
	{
		return false;
	}

	m_button_start = start;
	m_button_select = select;
	m_button_b = b;
	m_button_a = a;
	m_button_down = down;
	m_button_up = up;
	m_button_left = left;
	m_button_right = right;
	m_select_directions = directions;
	m_select_buttons = buttons;
	return true;
}
