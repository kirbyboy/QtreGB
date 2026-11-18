#include "input_directions.h"

namespace
{

constexpr size_t RIGHT = 0;
constexpr size_t LEFT = 1;
constexpr size_t UP = 2;
constexpr size_t DOWN = 3;

} // namespace

void DirectionResolver::reset()
{
	m_physical = {};
	m_order = {};
	m_resolved = {};
	m_sequence = 0;
}

void DirectionResolver::update(const std::array<bool, 4>& physical,
	bool allow_opposite_directions)
{
	// Assign the press order in a fixed sequence so simultaneous presses have
	// a deterministic winner: Right over Left and Down over Up.
	const size_t press_order[] = {LEFT, RIGHT, UP, DOWN};
	for(size_t index : press_order)
	{
		if(physical[index] && !m_physical[index])
		{
			m_order[index] = ++m_sequence;
		}
	}

	m_resolved = physical;
	if(!allow_opposite_directions)
	{
		if(physical[RIGHT] && physical[LEFT])
		{
			const bool right_wins = m_order[RIGHT] > m_order[LEFT];
			m_resolved[RIGHT] = right_wins;
			m_resolved[LEFT] = !right_wins;
		}
		if(physical[UP] && physical[DOWN])
		{
			const bool down_wins = m_order[DOWN] > m_order[UP];
			m_resolved[UP] = !down_wins;
			m_resolved[DOWN] = down_wins;
		}
	}

	m_physical = physical;
}
