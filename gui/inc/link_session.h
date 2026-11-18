#ifndef QTREGB_LINK_SESSION_H
#define QTREGB_LINK_SESSION_H

#include "link_cable.h"

class EmulatorInstance;

// GUI/session-side link object. There is exactly one LinkSession per
// connected pair of EmulatorInstance objects; it does not own either
// instance. It owns the core LinkCable and advances both machines in
// lockstep at T-cycle granularity.
//
// The core LinkCable does the bit-level serial exchange; this class only
// owns the pairing/lifecycle and the common tick.
class LinkSession
{
	public:
		LinkSession() = default;
		~LinkSession();

		LinkSession(const LinkSession&) = delete;
		LinkSession& operator=(const LinkSession&) = delete;

		bool connect(EmulatorInstance& left, EmulatorInstance& right);
		void disconnect();

		bool connected() const;
		void tick();

		EmulatorInstance* left() const { return m_left; }
		EmulatorInstance* right() const { return m_right; }

	private:
		EmulatorInstance* m_left = nullptr;
		EmulatorInstance* m_right = nullptr;
		qtreGB::LinkCable m_cable;
};

#endif
