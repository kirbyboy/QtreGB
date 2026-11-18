#include "input_state.h"

#include "gamepad.h"

using namespace qtreGB;

void InputState::press(GbButton button)
{
	const size_t index = static_cast<size_t>(button);
	if(index < Count)
	{
		m_pressed[index] = true;
	}
}

void InputState::release(GbButton button)
{
	const size_t index = static_cast<size_t>(button);
	if(index < Count)
	{
		m_pressed[index] = false;
	}
}

void InputState::release_all()
{
	m_pressed.fill(false);
}

bool InputState::is_pressed(GbButton button) const
{
	const size_t index = static_cast<size_t>(button);
	return index < Count && m_pressed[index];
}

void InputState::apply(Gamepad& gamepad) const
{
	gamepad.m_button_right = is_pressed(GbButton::Right);
	gamepad.m_button_left = is_pressed(GbButton::Left);
	gamepad.m_button_up = is_pressed(GbButton::Up);
	gamepad.m_button_down = is_pressed(GbButton::Down);
	gamepad.m_button_a = is_pressed(GbButton::A);
	gamepad.m_button_b = is_pressed(GbButton::B);
	gamepad.m_button_start = is_pressed(GbButton::Start);
	gamepad.m_button_select = is_pressed(GbButton::Select);
}
