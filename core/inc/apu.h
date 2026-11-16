#ifndef QTREGB_APU_H
#define QTREGB_APU_H

#include <cstddef>

#include "types.h"

namespace qtreGB
{

// DMG APU. Milestone 3: register file, power state and the CH1 pulse
// generator (timer, duty and fixed volume). The mixer, the ring buffer and the
// remaining channels are added in later milestones.
class APU
{
	public:
		static constexpr u32 CLOCK = 4194304;		// master clock in Hz
		static constexpr u32 SAMPLE_RATE = 48000;	// output rate in Hz
		static constexpr size_t RING_FRAMES = 4096;	// ~85 ms at 48 kHz

		enum Channel : u8
		{
			CH1 = 0,
			CH2,
			CH3,
			CH4
		};

		void reset();

		// Advances the APU by one T-cycle.
		void tick();

		u8 read(u16 address);
		void write(u16 address, u8 value);

		bool is_powered() const { return m_powered; }

		// Current 4-bit digital output of a channel (0-15). Channels that are
		// not implemented yet return 0.
		u8 channel_output(Channel channel) const;
		bool is_channel_enabled(Channel channel) const;

		// Stereo PCM produced by the mixer at SAMPLE_RATE.
		size_t available_frames() const { return m_ring_count; }
		size_t read_samples(int16_t* dst, size_t frames);

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

		// Pulse channel state (CH1 and CH2).
		struct Pulse
		{
			bool enabled = false;
			u8 duty = 0;			// 0..3
			u8 duty_step = 0;		// 0..7
			u16 frequency = 0;		// 0..2047
			int timer = 0;			// T-cycles until the next duty step
			u8 volume = 0;			// 0..15
			u8 initial_volume = 0;
			u8 env_period = 0;
			bool env_increase = false;
		};

		Pulse m_ch1;
		Pulse m_ch2;

		// Frame sequencer clock: one step every 8192 T-cycles (512 Hz).
		u32 m_frame_counter = 0;
		u8 m_frame_step = 0;

		// Resampling from CLOCK to SAMPLE_RATE and stereo ring buffer.
		u32 m_sample_acc = 0;
		int16_t m_ring[RING_FRAMES * 2] = {};
		size_t m_ring_read = 0;
		size_t m_ring_write = 0;
		size_t m_ring_count = 0;

		void push_sample(int16_t left, int16_t right);
		void mix_and_emit();
		void mix_channel(Channel channel, int& left, int& right) const;

		void power_on();
		void power_off();
		void clear_registers();

		void tick_pulse(Pulse& channel);
		void trigger_pulse(Pulse& channel, u8 duty_reg, u8 env_reg,
			u16 frequency, bool dac_enabled);
		u8 pulse_output(const Pulse& channel) const;

		bool ch1_dac_enabled() const { return (m_nr12 & 0xF8) != 0; }
		bool ch2_dac_enabled() const { return (m_nr22 & 0xF8) != 0; }
};

} // namespace qtreGB

#endif
