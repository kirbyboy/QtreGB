#ifndef QTREGB_SERIAL_H
#define QTREGB_SERIAL_H

#include "types.h"

namespace qtreGB
{

class IO;
class LinkCable;
class StateReader;
class StateWriter;

// DMG serial port. SB/SC live in IO (FF01/FF02); this class owns the transfer
// state: start detection, 8192 Hz internal clock, shift and serial interrupt.
class Serial
{
	public:
		explicit Serial(IO* io = nullptr);

		void connect_io(IO* io) { m_io = io; }
		void set_interrupt_callback(InterruptCallback cb) { m_interrupt = std::move(cb); }
		void set_link(LinkCable* link) { m_link = link; }
		LinkCable* link() const { return m_link; }

		void reset();
		void tick();	// one T-cycle

		bool is_active() const { return m_active; }
		bool is_internal_clock() const;
		bool is_external_clock() const;

		// LinkCable interface.
		u8 output_bit() const;
		void shift_from_link(u8 input_bit);

		void save_state(StateWriter& writer) const;
		bool load_state(StateReader& reader);

	private:
		void complete();

		IO* m_io = nullptr;
		LinkCable* m_link = nullptr;
		InterruptCallback m_interrupt;
		bool m_active = false;
		u8 m_bits_remaining = 0;
		u16 m_clock_counter = 0;
};

} // namespace qtreGB

#endif
