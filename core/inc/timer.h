#ifndef QTREGB_TIMER_H
#define QTREGB_TIMER_H

#include <functional>
#include "interrupt_controller.h"
#include "tagged_serializer.h"
#include "types.h"

namespace qtreGB
{

class Timer
{
	public:
		void set_interrupt_callback(InterruptCallback cb);
		void timer_init();
		void timer_tick();

		// --- MMIO register port (HW semantics) ---
		u8 read_div();
		// Full 16-bit internal divider. The APU frame sequencer is clocked by
		// the falling edge of its bit 12 (bit 4 of FF04).
		u16 read_div16() const { return m_div; }
		void write_div();	// on HW any write resets the divider
		u8 read_tima();
		void write_tima(u8 value);
		u8 read_tma();
		void write_tma(u8 value);
		u8 read_tac();
		void write_tac(u8 value);

		// --- Savestates ---
		void serialize(TaggedSerializer& s);

	private:
		u16 m_div;
		u8 m_tima;
		u8 m_tma;
		u8 m_tac;
		InterruptCallback m_interrupt_handler;

		// The divider is advanced by 4 once per M-cycle at T3.
		u8 m_timer_state = 0;			// 0=None, 1=Reload, 2=Pending
		bool m_last_div_signal = false;
		void set_div(u16 value);
		void inc_tima();
		void on_falling_edge_inc_tima();
		void handle_pending_tima_reload();

		u16 selected_bit();	// bit 9 (mode 0) does not fit in u8
};

} // namespace qtreGB
#endif
