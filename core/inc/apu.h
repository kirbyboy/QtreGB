#ifndef QTREGB_APU_H
#define QTREGB_APU_H

#include <cstddef>

#include "types.h"

namespace qtreGB
{

class StateReader;
class StateWriter;

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

		// Prepares the wave-RAM access window for the current T-cycle. Must be
		// called before the CPU runs so same-cycle wave-RAM accesses work.
		void prepare_tick();

		// Advances the APU by one T-cycle. The 16-bit divider drives the
		// frame sequencer through the falling edge of its bit 12.
		void tick(u16 div);

		u8 read(u16 address);
		void write(u16 address, u8 value);

		bool is_powered() const { return m_powered; }

		// Current 4-bit digital output of a channel (0-15). Channels that are
		// not implemented yet return 0.
		u8 channel_output(Channel channel) const;
		bool is_channel_enabled(Channel channel) const;

		// Current envelope volume (0-15). CH3 has no envelope and returns 0.
		u8 channel_volume(Channel channel) const;

		// Current 11-bit period value. CH4 has no frequency and returns 0.
		u16 channel_frequency(Channel channel) const;

		// Stereo PCM produced by the mixer at SAMPLE_RATE.
		size_t available_frames() const { return m_ring_count; }
		size_t read_samples(int16_t* dst, size_t frames);

		// Digital PCM values (CGB PCM12/PCM34 layout), useful for tests.
		u8 pcm12() const;
		u8 pcm34() const;

		void save_state(StateWriter& writer) const;
		bool load_state(StateReader& reader);

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
			u8 trigger_delay = 0;	// T-cycles to skip right after a trigger
			u8 volume = 0;			// 0..15
			u8 initial_volume = 0;
			u8 env_period = 0;
			u8 env_timer = 0;		// 3-bit volume countdown (0..7)
			bool env_reloaded = false;
			bool env_pending = false;
			bool env_increase = false;
			u16 length = 0;			// restante (máx. 64)
			bool length_enable = false;

			// CH1 sweep state (unused by CH2).
			u16 shadow_frequency = 0;
			u8 sweep_period = 0;
			u8 sweep_shift = 0;
			u8 sweep_timer = 0;
			bool sweep_decrease = false;
			bool sweep_enabled = false;
			bool sweep_negate_used = false;

			void save_state(StateWriter& writer) const;
			bool load_state(StateReader& reader);
		};

		Pulse m_ch1;
		Pulse m_ch2;

		// Wave channel state (CH3).
		struct Wave
		{
			bool enabled = false;
			u16 frequency = 0;		// 0..2047
			int timer = 0;			// odd ticks until the next sample
			u8 trigger_delay = 0;	// odd ticks to skip after a trigger
			u8 sample_index = 0;	// 0..31
			u8 sample = 0;			// 0..15
			u8 level = 0;			// NR32 bits 6-5
			u16 length = 0;			// restante (máx. 256)
			bool length_enable = false;

			// DMG wave-RAM access window: the CPU may only touch wave RAM on
			// the T-cycles after CH3 reads a byte, and then only that byte.
			bool access_window = false;
			u8 access_byte = 0;
			u8 sampled_byte = 0;
			u8 window_ttl = 0;
			u32 last_sample_cycle = 0;
			bool sample_valid = false;

			void save_state(StateWriter& writer) const;
			bool load_state(StateReader& reader);
		};

		Wave m_ch3;

		// Noise channel state (CH4).
		struct Noise
		{
			bool enabled = false;
			int timer = 0;			// T-cycles until the next LFSR clock
			u16 lfsr = 0x7FFF;
			bool short_mode = false;	// 7-bit instead of 15-bit
			u8 volume = 0;			// 0..15
			u8 initial_volume = 0;
			u8 env_period = 0;
			u8 env_timer = 0;		// 3-bit volume countdown (0..7)
			bool env_reloaded = false;
			bool env_pending = false;
			bool env_increase = false;
			u16 length = 0;			// restante (máx. 64)
			bool length_enable = false;
			u8 clock_shift = 0;
			u8 divisor_code = 0;

			void save_state(StateWriter& writer) const;
			bool load_state(StateReader& reader);
		};

		Noise m_ch4;

		// Frame sequencer clock: steps on the falling edge of DIV bit 12.
		u16 m_last_div = 0;
		u8 m_frame_step = 0;

		// T-cycle phase (0..3); CH3 samples on odd phases, like DocBoy/hardware.
		u8 m_apu_phase = 0;
		u32 m_cycle = 0;

		// Resampling from CLOCK to SAMPLE_RATE and stereo ring buffer.
		u32 m_sample_acc = 0;
		int16_t m_ring[RING_FRAMES * 2] = {};
		size_t m_ring_read = 0;
		size_t m_ring_write = 0;
		size_t m_ring_count = 0;

		// High-pass filter state, one capacitor per output side.
		float m_hpf_left = 0.0f;
		float m_hpf_right = 0.0f;

		void push_sample(int16_t left, int16_t right);
		void mix_and_emit();
		void mix_channel(Channel channel, int& left, int& right) const;

		void clock_frame_sequencer();
		bool clock_length(u16& length, bool enable);
		void clock_extra_length(u16& length, bool& length_enable,
			bool new_enable, bool& channel_enabled, bool trigger);

		// Volume envelope: a 3-bit countdown decremented at 64 Hz (frame step
		// 7). The pace is reloaded from NRX2 on the rising edge of DIV bit 12,
		// and the pending volume update is applied on the next event.
		void clock_envelope(Pulse& channel);
		void clock_envelope(Noise& channel);
		void reload_envelope(Pulse& channel, u8 period);
		void reload_envelope(Noise& channel, u8 period);
		void latch_envelope(Pulse& channel);
		void latch_envelope(Noise& channel);
		void update_envelope(Pulse& channel);
		void update_envelope(Noise& channel);

		void power_on();
		void power_off();
		void clear_registers();

		void tick_pulse(Pulse& channel);
		void trigger_pulse(Pulse& channel, u8 duty_reg, u8 env_reg,
			u16 frequency, bool dac_enabled);
		u8 pulse_output(const Pulse& channel) const;

		void trigger_sweep();
		void clock_sweep();
		int sweep_next_frequency() const;

		void tick_wave();
		void trigger_wave();
		u8 wave_output() const;

		void tick_noise();
		void trigger_noise();
		u8 noise_output() const;

		bool ch1_dac_enabled() const { return (m_nr12 & 0xF8) != 0; }
		bool ch2_dac_enabled() const { return (m_nr22 & 0xF8) != 0; }
		bool ch3_dac_enabled() const { return (m_nr30 & 0x80) != 0; }
		bool ch4_dac_enabled() const { return (m_nr42 & 0xF8) != 0; }
};

} // namespace qtreGB

#endif
