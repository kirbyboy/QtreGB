#ifndef QTREGB_GAMEPAD_H
#define QTREGB_GAMEPAD_H

#include <functional>
#include "types.h"

namespace qtreGB
{

class StateReader;
class StateWriter;

class Gamepad
{
	public:
		bool m_button_start = false;
		bool m_button_select = false;
		bool m_button_b = false;
		bool m_button_a = false;
		bool m_button_down = false;
		bool m_button_up = false;
		bool m_button_left = false;
		bool m_button_right = false;

		u8 get_state();
		void set_selection(u8 selection);

		void save_state(StateWriter& writer) const;
		bool load_state(StateReader& reader);

		void set_interrupt_callback(InterruptCallback cb);

	private:
		bool m_select_directions = false;
		bool m_select_buttons = false;

		InterruptCallback m_interrupt_handler;
};


} // namespace qtreGB
#endif
