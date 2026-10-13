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
		void write_div();	// on HW any write resets the divider
		u8 read_tima();

		// Between begin_cpu_phase() and the next timer_tick(), reads and the
		// reload/load window decisions of writes are judged against end-of-cycle
		// state (dry-run of the coming tick); timer_tick() then lands the cycle.
		void begin_cpu_phase();
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
		u8 m_reload_pending;	// cycle A: T-cycles until the TMA reload
		u8 m_load_active;		// cycle B: TIMA stays latched to TMA
		bool m_tima_write_block;
		bool m_div_increment_absorbed;	// DIV write cycle consumes the divider increment of that T-cycle
		bool m_cpu_phase;				// CPU runs before this cycle's timer_tick; reads see end-of-cycle state
		InterruptCallback m_interrupt_handler;

		u16 selected_bit();	// bit 9 (mode 0) does not fit in u8
		bool signal();
		u16 div_end_of_cycle();
		bool edge_pending();
		u8 peek_tima();
		void apply_timer_tick();
};

} // namespace qtreGB
#endif
