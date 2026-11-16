#ifndef QTREGB_APU_H
#define QTREGB_APU_H

#include "types.h"

namespace qtreGB
{

// DMG APU skeleton (milestones 1-2): register file and power state.
//
// The waveform generators are added in later milestones. For now tick() only
// advances the frame sequencer clock so the wiring and timing are in place.
class APU
{
	public:
		static constexpr u32 CLOCK = 4194304;	// master clock in Hz

		void reset();

		// Advances the APU by one T-cycle. No audio is produced yet.
		void tick();

		u8 read(u16 address);
		void write(u16 address, u8 value);

		bool is_powered() const { return m_powered; }

	private:
		// Registers FF10-FF25. NR52 is not stored: m_powered is its bit 7.
		u8 m_nr10 = 0;
		u8 m_nr11 = 0;
		u8 m_nr12 = 0;
		u8 m_nr13 = 0;
		u8 m_nr14 = 0;
		u8 m_nr21 = 0;
		u8 m_nr22 = 0;
		u8 m_nr23 = 0;
		u8 m_nr24 = 0;
		u8 m_nr30 = 0;
		u8 m_nr31 = 0;
		u8 m_nr32 = 0;
		u8 m_nr33 = 0;
		u8 m_nr34 = 0;
		u8 m_nr41 = 0;
		u8 m_nr42 = 0;
		u8 m_nr43 = 0;
		u8 m_nr44 = 0;
		u8 m_nr50 = 0;
		u8 m_nr51 = 0;

		u8 m_wave_ram[16] = {};

		bool m_powered = false;

		// Bits 0-3 of NR52. The channels fill this in when they exist.
		u8 m_channel_status = 0;

		// Frame sequencer clock: one step every 8192 T-cycles (512 Hz).
		u32 m_frame_counter = 0;
		u8 m_frame_step = 0;

		void power_on();
		void power_off();
		void clear_registers();
};

} // namespace qtreGB

#endif
