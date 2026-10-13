#ifndef QTREGB_GAMEPAD_H
#define QTREGB_GAMEPAD_H

#include <functional>
#include "types.h"

namespace qtreGB
{

class Gamepad
{
	public:
		bool m_button_start;
		bool m_button_select;
		bool m_button_b;
		bool m_button_a;
		bool m_button_down;
		bool m_button_up;
		bool m_button_left;
		bool m_button_right;

		u8 get_state();
		void set_selection(u8 selection);

		void set_interrupt_callback(InterruptCallback cb);

	private:
		bool m_select_directions;
		bool m_select_buttons;

		InterruptCallback m_interrupt_handler;
};


} // namespace qtreGB
#endif
