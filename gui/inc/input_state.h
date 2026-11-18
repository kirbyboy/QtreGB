#ifndef QTREGB_INPUT_STATE_H
#define QTREGB_INPUT_STATE_H

#include <array>
#include <cstddef>

#include "types.h"

namespace qtreGB
{
class Gamepad;
}

// One logical Game Boy button. The values are also used by the GUI as array
// indices, so keep them stable.
enum class GbButton : qtreGB::u8
{
	Right = 0,
	Left,
	Up,
	Down,
	A,
	B,
	Start,
	Select,
	Count
};

// Host input state for one emulator instance. true means pressed; applying it
// writes the eight Gamepad booleans, which is the only input state the core
// serializes.
class InputState
{
	public:
		void press(GbButton button);
		void release(GbButton button);
		void release_all();
		bool is_pressed(GbButton button) const;
		void apply(qtreGB::Gamepad& gamepad) const;

	private:
		static constexpr size_t Count = static_cast<size_t>(GbButton::Count);
		std::array<bool, Count> m_pressed {};
};

#endif
