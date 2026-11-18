#ifndef QTREGB_LINK_CABLE_H
#define QTREGB_LINK_CABLE_H

namespace qtreGB
{

class Serial;

// A bidirectional serial cable between exactly two Serial ports. It owns no
// endpoint and does not choose a permanent master: at each transfer/clock
// edge, the port configured for internal clock drives the cable, while the
// other port must be configured for external clock.
class LinkCable
{
	public:
		LinkCable() = default;
		~LinkCable();

		LinkCable(const LinkCable&) = delete;
		LinkCable& operator=(const LinkCable&) = delete;

		bool connect(Serial& left, Serial& right);
		void disconnect();

		bool connected() const { return m_left != nullptr && m_right != nullptr; }
		Serial* left() const { return m_left; }
		Serial* right() const { return m_right; }

		// Called by the internal-clock port when it produces one bit clock.
		void clock_from(Serial* source);

	private:
		Serial* m_left = nullptr;
		Serial* m_right = nullptr;
};

} // namespace qtreGB

#endif
