#include "link_session.h"

#include "link_cable.h"

#include "emulator_instance.h"

LinkSession::~LinkSession()
{
	disconnect();
}

bool LinkSession::connect(EmulatorInstance& left, EmulatorInstance& right)
{
	if(&left == &right)
	{
		return false;
	}
	if(!m_cable.connect(left.machine().m_serial, right.machine().m_serial))
	{
		return false;
	}

	m_left = &left;
	m_right = &right;
	return true;
}

void LinkSession::disconnect()
{
	m_cable.disconnect();
	m_left = nullptr;
	m_right = nullptr;
}

bool LinkSession::connected() const
{
	return m_left != nullptr && m_right != nullptr;
}

void LinkSession::tick()
{
	if(!connected())
	{
		return;
	}

	// Lockstep: both machines advance one T-cycle per cable step. The core
	// LinkCable already connects both Serial ports, so the master's serial
	// clock shifts the slave during this same step.
	m_left->tick();
	m_right->tick();
}
