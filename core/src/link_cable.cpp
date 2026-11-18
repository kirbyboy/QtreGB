#include "link_cable.h"

#include "serial.h"

using namespace qtreGB;

LinkCable::~LinkCable()
{
	disconnect();
}

bool LinkCable::connect(Serial& left, Serial& right)
{
	if(&left == &right)
	{
		return false;
	}

	disconnect();
	m_left = &left;
	m_right = &right;
	m_left->set_link(this);
	m_right->set_link(this);
	return true;
}

void LinkCable::disconnect()
{
	if(m_left != nullptr && m_left->link() == this)
	{
		m_left->set_link(nullptr);
	}
	if(m_right != nullptr && m_right->link() == this)
	{
		m_right->set_link(nullptr);
	}
	m_left = nullptr;
	m_right = nullptr;
}

void LinkCable::clock_from(Serial* source)
{
	if(!connected() || source == nullptr ||
	   (source != m_left && source != m_right))
	{
		return;
	}
	if(!source->is_internal_clock())
	{
		return;
	}

	Serial* other = source == m_left ? m_right : m_left;
	if(other->is_internal_clock())
	{
		// Both sides drive the clock: like real hardware, this is a bus
		// conflict. Do not invent a master; leave the transfer stalled.
		return;
	}

	const u8 left_out = m_left->output_bit();
	const u8 right_out = m_right->output_bit();
	m_left->shift_from_link(right_out);
	m_right->shift_from_link(left_out);
}
