#ifndef QTREGB_INPUT_DIRECTIONS_H
#define QTREGB_INPUT_DIRECTIONS_H

#include <array>
#include <cstddef>
#include <cstdint>

// Resolves the four Game Boy directions from the physical state of a d-pad.
// With opposite directions disabled the latest pressed direction wins; when it
// is released, an older direction that is still held becomes active again.
// The order of the array is Right, Left, Up, Down, matching GbButton.
class DirectionResolver
{
	public:
		void reset();
		void update(const std::array<bool, 4>& physical,
			bool allow_opposite_directions);
		const std::array<bool, 4>& resolved() const { return m_resolved; }

	private:
		std::array<bool, 4> m_physical {};
		std::array<std::uint64_t, 4> m_order {};
		std::array<bool, 4> m_resolved {};
		std::uint64_t m_sequence = 0;
};

#endif
