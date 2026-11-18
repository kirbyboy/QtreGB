#include "apu.h"
#include "state_stream.h"

using namespace qtreGB;

namespace
{

// Component-local field tags. See documentation/Game Boy/savestate-core-format.md.
constexpr u16 APU_NR10 = 0x0001;
constexpr u16 APU_NR11 = 0x0002;
constexpr u16 APU_NR12 = 0x0003;
constexpr u16 APU_NR13 = 0x0004;
constexpr u16 APU_NR14 = 0x0005;
constexpr u16 APU_NR21 = 0x0006;
constexpr u16 APU_NR22 = 0x0007;
constexpr u16 APU_NR23 = 0x0008;
constexpr u16 APU_NR24 = 0x0009;
constexpr u16 APU_NR30 = 0x000A;
constexpr u16 APU_NR31 = 0x000B;
constexpr u16 APU_NR32 = 0x000C;
constexpr u16 APU_NR33 = 0x000D;
constexpr u16 APU_NR34 = 0x000E;
constexpr u16 APU_NR41 = 0x000F;
constexpr u16 APU_NR42 = 0x0010;
constexpr u16 APU_NR43 = 0x0011;
constexpr u16 APU_NR44 = 0x0012;
constexpr u16 APU_NR50 = 0x0013;
constexpr u16 APU_NR51 = 0x0014;
constexpr u16 APU_WAVE_RAM = 0x0020;
constexpr u16 APU_POWERED = 0x0021;
constexpr u16 APU_LAST_DIV = 0x0022;
constexpr u16 APU_FRAME_STEP = 0x0023;
constexpr u16 APU_PHASE = 0x0024;
constexpr u16 APU_CYCLE = 0x0025;
constexpr u16 APU_SAMPLE_ACC = 0x0026;
constexpr u16 APU_HPF_LEFT = 0x0027;
constexpr u16 APU_HPF_RIGHT = 0x0028;

constexpr u16 APU_CHANNEL_CH1 = 0x0100;
constexpr u16 APU_CHANNEL_CH2 = 0x0101;
constexpr u16 APU_CHANNEL_CH3 = 0x0102;
constexpr u16 APU_CHANNEL_CH4 = 0x0103;

constexpr u16 PULSE_ENABLED = 0x0001;
constexpr u16 PULSE_DUTY = 0x0002;
constexpr u16 PULSE_DUTY_STEP = 0x0003;
constexpr u16 PULSE_FREQUENCY = 0x0004;
constexpr u16 PULSE_TIMER = 0x0005;
constexpr u16 PULSE_TRIGGER_DELAY = 0x0006;
constexpr u16 PULSE_VOLUME = 0x0007;
constexpr u16 PULSE_INITIAL_VOLUME = 0x0008;
constexpr u16 PULSE_ENV_PERIOD = 0x0009;
constexpr u16 PULSE_ENV_TIMER = 0x000A;
constexpr u16 PULSE_ENV_RELOADED = 0x000B;
constexpr u16 PULSE_ENV_PENDING = 0x000C;
constexpr u16 PULSE_ENV_INCREASE = 0x000D;
constexpr u16 PULSE_LENGTH = 0x000E;
constexpr u16 PULSE_LENGTH_ENABLE = 0x000F;
constexpr u16 PULSE_SHADOW_FREQUENCY = 0x0010;
constexpr u16 PULSE_SWEEP_PERIOD = 0x0011;
constexpr u16 PULSE_SWEEP_SHIFT = 0x0012;
constexpr u16 PULSE_SWEEP_TIMER = 0x0013;
constexpr u16 PULSE_SWEEP_DECREASE = 0x0014;
constexpr u16 PULSE_SWEEP_ENABLED = 0x0015;
constexpr u16 PULSE_SWEEP_NEGATE_USED = 0x0016;

constexpr u16 WAVE_ENABLED = 0x0001;
constexpr u16 WAVE_FREQUENCY = 0x0002;
constexpr u16 WAVE_TIMER = 0x0003;
constexpr u16 WAVE_TRIGGER_DELAY = 0x0004;
constexpr u16 WAVE_SAMPLE_INDEX = 0x0005;
constexpr u16 WAVE_SAMPLE = 0x0006;
constexpr u16 WAVE_LEVEL = 0x0007;
constexpr u16 WAVE_LENGTH = 0x0008;
constexpr u16 WAVE_LENGTH_ENABLE = 0x0009;
constexpr u16 WAVE_SAMPLED_BYTE = 0x000A;
constexpr u16 WAVE_WINDOW_TTL = 0x000B;
constexpr u16 WAVE_LAST_SAMPLE_CYCLE = 0x000C;
constexpr u16 WAVE_SAMPLE_VALID = 0x000D;

constexpr u16 NOISE_ENABLED = 0x0001;
constexpr u16 NOISE_TIMER = 0x0002;
constexpr u16 NOISE_LFSR = 0x0003;
constexpr u16 NOISE_SHORT_MODE = 0x0004;
constexpr u16 NOISE_VOLUME = 0x0005;
constexpr u16 NOISE_INITIAL_VOLUME = 0x0006;
constexpr u16 NOISE_ENV_PERIOD = 0x0007;
constexpr u16 NOISE_ENV_TIMER = 0x0008;
constexpr u16 NOISE_ENV_RELOADED = 0x0009;
constexpr u16 NOISE_ENV_PENDING = 0x000A;
constexpr u16 NOISE_ENV_INCREASE = 0x000B;
constexpr u16 NOISE_LENGTH = 0x000C;
constexpr u16 NOISE_LENGTH_ENABLE = 0x000D;
constexpr u16 NOISE_CLOCK_SHIFT = 0x000E;
constexpr u16 NOISE_DIVISOR_CODE = 0x000F;

// One pulse period, one entry per duty step. 1 = high, 0 = low.
constexpr u8 DUTY[4][8] = {
	{0, 0, 0, 0, 0, 0, 0, 1},	// 12.5 %
	{1, 0, 0, 0, 0, 0, 0, 1},	// 25 %
	{1, 0, 0, 0, 0, 1, 1, 1},	// 50 %
	{0, 1, 1, 1, 1, 1, 1, 0},	// 75 %
};

// NR32 output level: 0 mutes, 1 is 100 %, 2 is 50 %, 3 is 25 %.
constexpr u8 WAVE_SHIFT[4] = {4, 0, 1, 2};

u16 frequency_from_registers(u8 low, u8 high)
{
	return static_cast<u16>(((high & 0x07) << 8) | low);
}

bool is_length_step(u8 step)
{
	// DocBoy's div_apu counter is incremented before processing, so length
	// clocks when the counter is even.
	return (step & 0x01) == 0;
}

// CGB-E NRX2 glitch ("zombie mode") tables. The volume update is a
// combination of an optional 4-bit negation and a small increment/decrement.
// Index order: [prev_initial_volume != 0][prev_sweep != 0][new_sweep != 0]
//             [prev_direction][new_direction]
constexpr bool GLITCH_NRX2_NEGATE[2][2][2][2][2] = {
	{
		{
			{{true, false}, {true, false}},
			{{true, false}, {true, false}},
		},
		{
			{{true, false}, {true, false}},
			{{true, false}, {true, false}},
		},
	},
	{
		{
			{{false, true}, {true, false}},
			{{false, true}, {true, false}},
		},
		{
			{{false, true}, {true, false}},
			{{false, true}, {true, false}},
		},
	},
};

constexpr u8 GLITCH_NRX2_INCREMENT[2][2][2][2][2] = {
	{
		{
			{{1, 1}, {1, 1}},
			{{0, 1}, {0, 1}},
		},
		{
			{{1, 0}, {1, 0}},
			{{1, 0}, {1, 0}},
		},
	},
	{
		{
			{{0, 0}, {1, 1}},
			{{15, 0}, {0, 1}},
		},
		{
			{{0, 15}, {1, 0}},
			{{0, 15}, {1, 0}},
		},
	},
};

u8 nrx2_glitch_volume(u8 prev_value, u8 new_value, u8 volume)
{
	const bool prev_volume = (prev_value & 0xF0) != 0;
	const bool prev_sweep = (prev_value & 0x07) != 0;
	const bool new_sweep = (new_value & 0x07) != 0;
	const bool prev_direction = (prev_value & 0x08) != 0;
	const bool new_direction = (new_value & 0x08) != 0;

	const bool negate = GLITCH_NRX2_NEGATE[prev_volume][prev_sweep][new_sweep]
		[prev_direction][new_direction];
	const u8 increment = GLITCH_NRX2_INCREMENT[prev_volume][prev_sweep][new_sweep]
		[prev_direction][new_direction];
	const u8 base = negate ? static_cast<u8>(15 - volume) : volume;
	return static_cast<u8>((base + increment) & 0x0F);
}

int16_t clamp_sample(int value)
{
	if(value > 32767)
	{
		return 32767;
	}
	if(value < -32768)
	{
		return -32768;
	}
	return static_cast<int16_t>(value);
}

// Pan Docs gives 0.999958 at 4194304 Hz; at 48 kHz this is
// 0.999958^(4194304/48000) ~= 0.99634.
constexpr float HPF_COEFFICIENT = 0.99634f;

float high_pass(float in, float& capacitor)
{
	const float out = in - capacitor;
	capacitor = in - out * HPF_COEFFICIENT;
	return out;
}

} // namespace

void APU::reset()
{
	clear_registers();
	for(u8& byte : m_wave_ram)
	{
		byte = 0;
	}

	m_powered = false;
	m_last_div = 0;
	m_frame_step = 0;
	m_apu_phase = 0;
	m_cycle = 0;

	m_sample_acc = 0;
	m_ring_read = 0;
	m_ring_write = 0;
	m_ring_count = 0;

	m_hpf_left = 0.0f;
	m_hpf_right = 0.0f;
}

void APU::tick(u16 div)
{
	if(!m_powered)
	{
		// Keep tracking DIV so powering on does not create a spurious edge.
		m_last_div = div;
		m_ch3.access_window = false;
		m_ch3.window_ttl = 0;
		return;
	}

	if(m_ch3.window_ttl > 0)
	{
		--m_ch3.window_ttl;
	}

	const bool odd_phase = (m_apu_phase & 0x01) != 0;
	m_apu_phase = static_cast<u8>((m_apu_phase + 1) & 0x03);

	if((m_last_div & 0x1000) != 0 && (div & 0x1000) == 0)
	{
		clock_frame_sequencer();
	}
	else if((m_last_div & 0x1000) == 0 && (div & 0x1000) != 0)
	{
		// The volume-sweep pace is reloaded from NRX2 on the rising edge of
		// DIV bit 12, half a DIV-APU event after the frame sequencer step.
		reload_envelope(m_ch1, m_ch1.env_period);
		reload_envelope(m_ch2, m_ch2.env_period);
		reload_envelope(m_ch4, m_ch4.env_period);
	}
	m_last_div = div;

	update_envelope(m_ch1);
	update_envelope(m_ch2);
	update_envelope(m_ch4);

	tick_pulse(m_ch1);
	tick_pulse(m_ch2);
	if(odd_phase)
	{
		tick_wave();
	}
	tick_noise();

	// Nearest-neighbour resampling: emit one frame every CLOCK/SAMPLE_RATE
	// T-cycles (about 87.38 at 48 kHz).
	m_sample_acc += SAMPLE_RATE;
	if(m_sample_acc >= CLOCK)
	{
		m_sample_acc -= CLOCK;
		mix_and_emit();
	}

	m_ch3.access_window = false;
}

void APU::clock_frame_sequencer()
{
	// The DIV-APU counter is incremented before the step is processed.
	m_frame_step = static_cast<u8>((m_frame_step + 1) & 0x07);

	// Length is clocked for all channels every two steps (256 Hz).
	if(is_length_step(m_frame_step))
	{
		if(!clock_length(m_ch1.length, m_ch1.length_enable)) m_ch1.enabled = false;
		if(!clock_length(m_ch2.length, m_ch2.length_enable)) m_ch2.enabled = false;
		if(!clock_length(m_ch3.length, m_ch3.length_enable)) m_ch3.enabled = false;
		if(!clock_length(m_ch4.length, m_ch4.length_enable)) m_ch4.enabled = false;
	}

	// CH1 frequency sweep is clocked at 128 Hz on multiples of 4.
	if((m_frame_step & 0x03) == 0)
	{
		clock_sweep();
	}

	// Volume envelope is clocked at 64 Hz on multiples of 8.
	if(m_frame_step == 0)
	{
		clock_envelope(m_ch1);
		clock_envelope(m_ch2);
		clock_envelope(m_ch4);
	}

	// A reloaded pace becomes a pending update on the next event.
	latch_envelope(m_ch1);
	latch_envelope(m_ch2);
	latch_envelope(m_ch4);
}

bool APU::clock_length(u16& length, bool enable)
{
	if(enable && length > 0)
	{
		--length;
		return length != 0;
	}
	return true;
}

void APU::clock_extra_length(u16& length, bool& length_enable,
	bool new_enable, bool& channel_enabled, bool trigger)
{
	const bool was_enabled = length_enable;
	length_enable = new_enable;

	// Enabling the length in a step that does not clock it performs an
	// extra clock, unless the counter is already empty.
	if(!was_enabled && new_enable && length > 0 && is_length_step(m_frame_step))
	{
		--length;
		if(length == 0 && !trigger)
		{
			channel_enabled = false;
		}
	}
}

void APU::clock_envelope(Pulse& channel)
{
	// The countdown decreases even with a zero period; the reload with a
	// non-zero period is what arms a volume update.
	if(channel.enabled && !channel.env_reloaded)
	{
		channel.env_timer = static_cast<u8>((channel.env_timer - 1) & 0x07);
	}
}

void APU::clock_envelope(Noise& channel)
{
	if(channel.enabled && !channel.env_reloaded)
	{
		channel.env_timer = static_cast<u8>((channel.env_timer - 1) & 0x07);
	}
}

void APU::reload_envelope(Pulse& channel, u8 period)
{
	if(channel.env_timer == 0)
	{
		channel.env_timer = period;
		if(channel.env_timer != 0)
		{
			channel.env_reloaded = true;
		}
	}
}

void APU::reload_envelope(Noise& channel, u8 period)
{
	if(channel.env_timer == 0)
	{
		channel.env_timer = period;
		if(channel.env_timer != 0)
		{
			channel.env_reloaded = true;
		}
	}
}

void APU::latch_envelope(Pulse& channel)
{
	if(channel.env_reloaded)
	{
		channel.env_reloaded = false;
		channel.env_pending = true;
	}
}

void APU::latch_envelope(Noise& channel)
{
	if(channel.env_reloaded)
	{
		channel.env_reloaded = false;
		channel.env_pending = true;
	}
}

void APU::update_envelope(Pulse& channel)
{
	if(!channel.env_pending)
	{
		return;
	}

	channel.env_pending = false;
	if(channel.env_increase)
	{
		if(channel.volume < 15)
		{
			++channel.volume;
		}
	}
	else if(channel.volume > 0)
	{
		--channel.volume;
	}
}

void APU::update_envelope(Noise& channel)
{
	if(!channel.env_pending)
	{
		return;
	}

	channel.env_pending = false;
	if(channel.env_increase)
	{
		if(channel.volume < 15)
		{
			++channel.volume;
		}
	}
	else if(channel.volume > 0)
	{
		--channel.volume;
	}
}

void APU::Pulse::save_state(StateWriter& writer) const
{
	writer.write_bool(PULSE_ENABLED, enabled);
	writer.write_u8(PULSE_DUTY, duty);
	writer.write_u8(PULSE_DUTY_STEP, duty_step);
	writer.write_u16(PULSE_FREQUENCY, frequency);
	writer.write_i32(PULSE_TIMER, static_cast<int32_t>(timer));
	writer.write_u8(PULSE_TRIGGER_DELAY, trigger_delay);
	writer.write_u8(PULSE_VOLUME, volume);
	writer.write_u8(PULSE_INITIAL_VOLUME, initial_volume);
	writer.write_u8(PULSE_ENV_PERIOD, env_period);
	writer.write_u8(PULSE_ENV_TIMER, env_timer);
	writer.write_bool(PULSE_ENV_RELOADED, env_reloaded);
	writer.write_bool(PULSE_ENV_PENDING, env_pending);
	writer.write_bool(PULSE_ENV_INCREASE, env_increase);
	writer.write_u16(PULSE_LENGTH, length);
	writer.write_bool(PULSE_LENGTH_ENABLE, length_enable);
	writer.write_u16(PULSE_SHADOW_FREQUENCY, shadow_frequency);
	writer.write_u8(PULSE_SWEEP_PERIOD, sweep_period);
	writer.write_u8(PULSE_SWEEP_SHIFT, sweep_shift);
	writer.write_u8(PULSE_SWEEP_TIMER, sweep_timer);
	writer.write_bool(PULSE_SWEEP_DECREASE, sweep_decrease);
	writer.write_bool(PULSE_SWEEP_ENABLED, sweep_enabled);
	writer.write_bool(PULSE_SWEEP_NEGATE_USED, sweep_negate_used);
}

bool APU::Pulse::load_state(StateReader& reader)
{
	const Pulse snapshot = *this;
	int32_t timer_value = 0;

	if(!reader.read_bool(PULSE_ENABLED, enabled) ||
	   !reader.read_u8(PULSE_DUTY, duty) ||
	   !reader.read_u8(PULSE_DUTY_STEP, duty_step) ||
	   !reader.read_u16(PULSE_FREQUENCY, frequency) ||
	   !reader.read_i32(PULSE_TIMER, timer_value) ||
	   !reader.read_u8(PULSE_TRIGGER_DELAY, trigger_delay) ||
	   !reader.read_u8(PULSE_VOLUME, volume) ||
	   !reader.read_u8(PULSE_INITIAL_VOLUME, initial_volume) ||
	   !reader.read_u8(PULSE_ENV_PERIOD, env_period) ||
	   !reader.read_u8(PULSE_ENV_TIMER, env_timer) ||
	   !reader.read_bool(PULSE_ENV_RELOADED, env_reloaded) ||
	   !reader.read_bool(PULSE_ENV_PENDING, env_pending) ||
	   !reader.read_bool(PULSE_ENV_INCREASE, env_increase) ||
	   !reader.read_u16(PULSE_LENGTH, length) ||
	   !reader.read_bool(PULSE_LENGTH_ENABLE, length_enable) ||
	   !reader.read_u16(PULSE_SHADOW_FREQUENCY, shadow_frequency) ||
	   !reader.read_u8(PULSE_SWEEP_PERIOD, sweep_period) ||
	   !reader.read_u8(PULSE_SWEEP_SHIFT, sweep_shift) ||
	   !reader.read_u8(PULSE_SWEEP_TIMER, sweep_timer) ||
	   !reader.read_bool(PULSE_SWEEP_DECREASE, sweep_decrease) ||
	   !reader.read_bool(PULSE_SWEEP_ENABLED, sweep_enabled) ||
	   !reader.read_bool(PULSE_SWEEP_NEGATE_USED, sweep_negate_used))
	{
		*this = snapshot;
		return false;
	}

	if(duty > 3 || duty_step > 7 || frequency > 0x7FF ||
	   volume > 15 || initial_volume > 15 || env_period > 7 ||
	   env_timer > 7 || length > 64 || sweep_period > 7 ||
	   sweep_shift > 7 || sweep_timer > 8)
	{
		*this = snapshot;
		reader.mark_error(StateError::BadValue);
		return false;
	}

	timer = static_cast<int>(timer_value);
	return true;
}

void APU::Wave::save_state(StateWriter& writer) const
{
	writer.write_bool(WAVE_ENABLED, enabled);
	writer.write_u16(WAVE_FREQUENCY, frequency);
	writer.write_i32(WAVE_TIMER, static_cast<int32_t>(timer));
	writer.write_u8(WAVE_TRIGGER_DELAY, trigger_delay);
	writer.write_u8(WAVE_SAMPLE_INDEX, sample_index);
	writer.write_u8(WAVE_SAMPLE, sample);
	writer.write_u8(WAVE_LEVEL, level);
	writer.write_u16(WAVE_LENGTH, length);
	writer.write_bool(WAVE_LENGTH_ENABLE, length_enable);
	writer.write_u8(WAVE_SAMPLED_BYTE, sampled_byte);
	writer.write_u8(WAVE_WINDOW_TTL, window_ttl);
	writer.write_u32(WAVE_LAST_SAMPLE_CYCLE, last_sample_cycle);
	writer.write_bool(WAVE_SAMPLE_VALID, sample_valid);
}

bool APU::Wave::load_state(StateReader& reader)
{
	const Wave snapshot = *this;
	int32_t timer_value = 0;

	if(!reader.read_bool(WAVE_ENABLED, enabled) ||
	   !reader.read_u16(WAVE_FREQUENCY, frequency) ||
	   !reader.read_i32(WAVE_TIMER, timer_value) ||
	   !reader.read_u8(WAVE_TRIGGER_DELAY, trigger_delay) ||
	   !reader.read_u8(WAVE_SAMPLE_INDEX, sample_index) ||
	   !reader.read_u8(WAVE_SAMPLE, sample) ||
	   !reader.read_u8(WAVE_LEVEL, level) ||
	   !reader.read_u16(WAVE_LENGTH, length) ||
	   !reader.read_bool(WAVE_LENGTH_ENABLE, length_enable) ||
	   !reader.read_u8(WAVE_SAMPLED_BYTE, sampled_byte) ||
	   !reader.read_u8(WAVE_WINDOW_TTL, window_ttl) ||
	   !reader.read_u32(WAVE_LAST_SAMPLE_CYCLE, last_sample_cycle) ||
	   !reader.read_bool(WAVE_SAMPLE_VALID, sample_valid))
	{
		*this = snapshot;
		return false;
	}

	if(frequency > 0x7FF || sample_index > 31 || sample > 15 ||
	   level > 3 || length > 256)
	{
		*this = snapshot;
		reader.mark_error(StateError::BadValue);
		return false;
	}

	timer = static_cast<int>(timer_value);
	return true;
}

void APU::Noise::save_state(StateWriter& writer) const
{
	writer.write_bool(NOISE_ENABLED, enabled);
	writer.write_i32(NOISE_TIMER, static_cast<int32_t>(timer));
	writer.write_u16(NOISE_LFSR, lfsr);
	writer.write_bool(NOISE_SHORT_MODE, short_mode);
	writer.write_u8(NOISE_VOLUME, volume);
	writer.write_u8(NOISE_INITIAL_VOLUME, initial_volume);
	writer.write_u8(NOISE_ENV_PERIOD, env_period);
	writer.write_u8(NOISE_ENV_TIMER, env_timer);
	writer.write_bool(NOISE_ENV_RELOADED, env_reloaded);
	writer.write_bool(NOISE_ENV_PENDING, env_pending);
	writer.write_bool(NOISE_ENV_INCREASE, env_increase);
	writer.write_u16(NOISE_LENGTH, length);
	writer.write_bool(NOISE_LENGTH_ENABLE, length_enable);
	writer.write_u8(NOISE_CLOCK_SHIFT, clock_shift);
	writer.write_u8(NOISE_DIVISOR_CODE, divisor_code);
}

bool APU::Noise::load_state(StateReader& reader)
{
	const Noise snapshot = *this;
	int32_t timer_value = 0;

	if(!reader.read_bool(NOISE_ENABLED, enabled) ||
	   !reader.read_i32(NOISE_TIMER, timer_value) ||
	   !reader.read_u16(NOISE_LFSR, lfsr) ||
	   !reader.read_bool(NOISE_SHORT_MODE, short_mode) ||
	   !reader.read_u8(NOISE_VOLUME, volume) ||
	   !reader.read_u8(NOISE_INITIAL_VOLUME, initial_volume) ||
	   !reader.read_u8(NOISE_ENV_PERIOD, env_period) ||
	   !reader.read_u8(NOISE_ENV_TIMER, env_timer) ||
	   !reader.read_bool(NOISE_ENV_RELOADED, env_reloaded) ||
	   !reader.read_bool(NOISE_ENV_PENDING, env_pending) ||
	   !reader.read_bool(NOISE_ENV_INCREASE, env_increase) ||
	   !reader.read_u16(NOISE_LENGTH, length) ||
	   !reader.read_bool(NOISE_LENGTH_ENABLE, length_enable) ||
	   !reader.read_u8(NOISE_CLOCK_SHIFT, clock_shift) ||
	   !reader.read_u8(NOISE_DIVISOR_CODE, divisor_code))
	{
		*this = snapshot;
		return false;
	}

	if(volume > 15 || initial_volume > 15 || env_period > 7 ||
	   env_timer > 7 || length > 64 || clock_shift > 15 ||
	   divisor_code > 7)
	{
		*this = snapshot;
		reader.mark_error(StateError::BadValue);
		return false;
	}

	timer = static_cast<int>(timer_value);
	return true;
}

void APU::save_state(StateWriter& writer) const
{
	writer.write_u8(APU_NR10, m_nr10);
	writer.write_u8(APU_NR11, m_nr11);
	writer.write_u8(APU_NR12, m_nr12);
	writer.write_u8(APU_NR13, m_nr13);
	writer.write_u8(APU_NR14, m_nr14);
	writer.write_u8(APU_NR21, m_nr21);
	writer.write_u8(APU_NR22, m_nr22);
	writer.write_u8(APU_NR23, m_nr23);
	writer.write_u8(APU_NR24, m_nr24);
	writer.write_u8(APU_NR30, m_nr30);
	writer.write_u8(APU_NR31, m_nr31);
	writer.write_u8(APU_NR32, m_nr32);
	writer.write_u8(APU_NR33, m_nr33);
	writer.write_u8(APU_NR34, m_nr34);
	writer.write_u8(APU_NR41, m_nr41);
	writer.write_u8(APU_NR42, m_nr42);
	writer.write_u8(APU_NR43, m_nr43);
	writer.write_u8(APU_NR44, m_nr44);
	writer.write_u8(APU_NR50, m_nr50);
	writer.write_u8(APU_NR51, m_nr51);

	writer.write_bytes(APU_WAVE_RAM, m_wave_ram, sizeof(m_wave_ram));
	writer.write_bool(APU_POWERED, m_powered);

	writer.begin_chunk(APU_CHANNEL_CH1);
	m_ch1.save_state(writer);
	writer.end_chunk();
	writer.begin_chunk(APU_CHANNEL_CH2);
	m_ch2.save_state(writer);
	writer.end_chunk();
	writer.begin_chunk(APU_CHANNEL_CH3);
	m_ch3.save_state(writer);
	writer.end_chunk();
	writer.begin_chunk(APU_CHANNEL_CH4);
	m_ch4.save_state(writer);
	writer.end_chunk();

	writer.write_u16(APU_LAST_DIV, m_last_div);
	writer.write_u8(APU_FRAME_STEP, m_frame_step);
	writer.write_u8(APU_PHASE, m_apu_phase);
	writer.write_u32(APU_CYCLE, m_cycle);
	writer.write_u32(APU_SAMPLE_ACC, m_sample_acc);
	writer.write_f32(APU_HPF_LEFT, m_hpf_left);
	writer.write_f32(APU_HPF_RIGHT, m_hpf_right);
}

bool APU::load_state(StateReader& reader)
{
	const APU snapshot = *this;

	if(!reader.read_u8(APU_NR10, m_nr10) ||
	   !reader.read_u8(APU_NR11, m_nr11) ||
	   !reader.read_u8(APU_NR12, m_nr12) ||
	   !reader.read_u8(APU_NR13, m_nr13) ||
	   !reader.read_u8(APU_NR14, m_nr14) ||
	   !reader.read_u8(APU_NR21, m_nr21) ||
	   !reader.read_u8(APU_NR22, m_nr22) ||
	   !reader.read_u8(APU_NR23, m_nr23) ||
	   !reader.read_u8(APU_NR24, m_nr24) ||
	   !reader.read_u8(APU_NR30, m_nr30) ||
	   !reader.read_u8(APU_NR31, m_nr31) ||
	   !reader.read_u8(APU_NR32, m_nr32) ||
	   !reader.read_u8(APU_NR33, m_nr33) ||
	   !reader.read_u8(APU_NR34, m_nr34) ||
	   !reader.read_u8(APU_NR41, m_nr41) ||
	   !reader.read_u8(APU_NR42, m_nr42) ||
	   !reader.read_u8(APU_NR43, m_nr43) ||
	   !reader.read_u8(APU_NR44, m_nr44) ||
	   !reader.read_u8(APU_NR50, m_nr50) ||
	   !reader.read_u8(APU_NR51, m_nr51) ||
	   !reader.read_bytes(APU_WAVE_RAM, m_wave_ram, sizeof(m_wave_ram)) ||
	   !reader.read_bool(APU_POWERED, m_powered))
	{
		*this = snapshot;
		return false;
	}

	if(!reader.begin_chunk(APU_CHANNEL_CH1) ||
	   !m_ch1.load_state(reader) ||
	   !reader.end_chunk() ||
	   !reader.begin_chunk(APU_CHANNEL_CH2) ||
	   !m_ch2.load_state(reader) ||
	   !reader.end_chunk() ||
	   !reader.begin_chunk(APU_CHANNEL_CH3) ||
	   !m_ch3.load_state(reader) ||
	   !reader.end_chunk() ||
	   !reader.begin_chunk(APU_CHANNEL_CH4) ||
	   !m_ch4.load_state(reader) ||
	   !reader.end_chunk())
	{
		*this = snapshot;
		return false;
	}

	if(!reader.read_u16(APU_LAST_DIV, m_last_div) ||
	   !reader.read_u8(APU_FRAME_STEP, m_frame_step) ||
	   !reader.read_u8(APU_PHASE, m_apu_phase) ||
	   !reader.read_u32(APU_CYCLE, m_cycle) ||
	   !reader.read_u32(APU_SAMPLE_ACC, m_sample_acc) ||
	   !reader.read_f32(APU_HPF_LEFT, m_hpf_left) ||
	   !reader.read_f32(APU_HPF_RIGHT, m_hpf_right))
	{
		*this = snapshot;
		return false;
	}

	if(m_frame_step > 7 || m_apu_phase > 3)
	{
		*this = snapshot;
		reader.mark_error(StateError::BadValue);
		return false;
	}

	// The PCM ring is output staging, not machine state. The wave-RAM access
	// window is re-derived from the current T-cycle by prepare_tick().
	m_ring_read = 0;
	m_ring_write = 0;
	m_ring_count = 0;
	m_ch3.access_window = false;
	m_ch3.access_byte = 0;
	return true;
}

void APU::prepare_tick()
{
	++m_cycle;

	m_ch3.access_window = m_powered && m_ch3.enabled && m_ch3.window_ttl > 0;
	if(m_ch3.access_window)
	{
		m_ch3.access_byte = m_ch3.sampled_byte;
	}
}

u8 APU::read(u16 address)
{
	if(address >= 0xFF30 && address <= 0xFF3F)
	{
		if(m_ch3.enabled)
		{
			if(!m_ch3.sample_valid)
			{
				return 0xFF;
			}
			const u32 age = m_cycle - m_ch3.last_sample_cycle;
			if(age < 2 || age > 3)
			{
				return 0xFF;
			}
			return m_wave_ram[m_ch3.sampled_byte];
		}
		return m_wave_ram[address - 0xFF30];
	}

	if(address == 0xFF26)
	{
		// Bits 6-4 are unused on the DMG and read as 1. Bits 3-0 report the
		// channel status.
		const u8 status = static_cast<u8>(
			(m_ch1.enabled ? 0x01 : 0x00) |
			(m_ch2.enabled ? 0x02 : 0x00) |
			(m_ch3.enabled ? 0x04 : 0x00) |
			(m_ch4.enabled ? 0x08 : 0x00));
		return static_cast<u8>(0x70 | (m_powered ? 0x80 : 0x00) | status);
	}

	switch(address)
	{
		case 0xFF10: return static_cast<u8>(m_nr10 | 0x80);	// bit 7 unused
		case 0xFF11: return static_cast<u8>((m_nr11 & 0xC0) | 0x3F);	// length is write-only
		case 0xFF12: return m_nr12;
		case 0xFF13: return 0xFF;	// write-only
		case 0xFF14: return static_cast<u8>((m_nr14 & 0x40) | 0xBF);	// only length enable is readable
		case 0xFF16: return static_cast<u8>((m_nr21 & 0xC0) | 0x3F);	// length is write-only
		case 0xFF17: return m_nr22;
		case 0xFF18: return 0xFF;	// write-only
		case 0xFF19: return static_cast<u8>((m_nr24 & 0x40) | 0xBF);
		case 0xFF1A: return static_cast<u8>(m_nr30 | 0x7F);	// bits 6-0 unused
		case 0xFF1B: return 0xFF;	// write-only
		case 0xFF1C: return static_cast<u8>(m_nr32 | 0x9F);	// bits 7 and 4-0 unused
		case 0xFF1D: return 0xFF;	// write-only
		case 0xFF1E: return static_cast<u8>((m_nr34 & 0x40) | 0xBF);
		case 0xFF20: return 0xFF;	// write-only
		case 0xFF21: return m_nr42;
		case 0xFF22: return m_nr43;
		case 0xFF23: return static_cast<u8>((m_nr44 & 0x40) | 0xBF);
		case 0xFF24: return m_nr50;
		case 0xFF25: return m_nr51;
		default: return 0xFF;
	}
}

void APU::write(u16 address, u8 value)
{
	if(address >= 0xFF30 && address <= 0xFF3F)
	{
		if(m_ch3.enabled)
		{
			if(m_ch3.access_window)
			{
				m_wave_ram[m_ch3.access_byte] = value;
			}
		}
		else
		{
			m_wave_ram[address - 0xFF30] = value;
		}
		return;
	}

	if(address == 0xFF26)
	{
		const bool enable = (value & 0x80) != 0;
		if(enable && !m_powered)
		{
			power_on();
		}
		else if(!enable && m_powered)
		{
			power_off();
		}
		return;
	}

	if(!m_powered)
	{
		// On DMG the length registers still write their counters while the
		// APU is off, even though the register file itself is cleared.
		switch(address)
		{
			case 0xFF11: m_ch1.length = static_cast<u16>(64 - (value & 0x3F)); break;
			case 0xFF16: m_ch2.length = static_cast<u16>(64 - (value & 0x3F)); break;
			case 0xFF1B: m_ch3.length = static_cast<u16>(256 - value); break;
			case 0xFF20: m_ch4.length = static_cast<u16>(64 - (value & 0x3F)); break;
			default: break;
		}
		return;
	}

	switch(address)
	{
		case 0xFF10:
			m_nr10 = static_cast<u8>(value & 0x7F);
			m_ch1.sweep_period = static_cast<u8>((m_nr10 >> 4) & 0x07);
			m_ch1.sweep_shift = static_cast<u8>(m_nr10 & 0x07);
			if(m_ch1.sweep_negate_used && (m_nr10 & 0x08) == 0)
			{
				m_ch1.enabled = false;
			}
			m_ch1.sweep_decrease = (m_nr10 & 0x08) != 0;
			break;
		case 0xFF11:
			m_nr11 = value;
			m_ch1.duty = static_cast<u8>((value >> 6) & 0x03);
			m_ch1.length = static_cast<u16>(64 - (value & 0x3F));
			break;
		case 0xFF12:
		{
			const u8 previous = m_nr12;
			m_nr12 = value;
			m_ch1.initial_volume = static_cast<u8>(value >> 4);
			m_ch1.env_period = static_cast<u8>(value & 0x07);
			m_ch1.env_increase = (value & 0x08) != 0;
			if(m_ch1.enabled)
			{
				m_ch1.volume = nrx2_glitch_volume(previous, value, m_ch1.volume);
				if(!ch1_dac_enabled())
				{
					m_ch1.enabled = false;
				}
			}
			break;
		}
		case 0xFF13:
			m_nr13 = value;
			m_ch1.frequency = frequency_from_registers(m_nr13, m_nr14);
			break;
		case 0xFF14:
			m_nr14 = static_cast<u8>(value & 0x47);
			m_ch1.frequency = frequency_from_registers(m_nr13, m_nr14);
			clock_extra_length(m_ch1.length, m_ch1.length_enable,
				(value & 0x40) != 0, m_ch1.enabled, (value & 0x80) != 0);
			if((value & 0x80) != 0)
			{
				trigger_pulse(m_ch1, m_nr11, m_nr12, m_ch1.frequency,
					ch1_dac_enabled());
				trigger_sweep();
			}
			break;
		case 0xFF16:
			m_nr21 = value;
			m_ch2.duty = static_cast<u8>((value >> 6) & 0x03);
			m_ch2.length = static_cast<u16>(64 - (value & 0x3F));
			break;
		case 0xFF17:
		{
			const u8 previous = m_nr22;
			m_nr22 = value;
			m_ch2.initial_volume = static_cast<u8>(value >> 4);
			m_ch2.env_period = static_cast<u8>(value & 0x07);
			m_ch2.env_increase = (value & 0x08) != 0;
			if(m_ch2.enabled)
			{
				m_ch2.volume = nrx2_glitch_volume(previous, value, m_ch2.volume);
				if(!ch2_dac_enabled())
				{
					m_ch2.enabled = false;
				}
			}
			break;
		}
		case 0xFF18:
			m_nr23 = value;
			m_ch2.frequency = frequency_from_registers(m_nr23, m_nr24);
			break;
		case 0xFF19:
			m_nr24 = static_cast<u8>(value & 0x47);
			m_ch2.frequency = frequency_from_registers(m_nr23, m_nr24);
			clock_extra_length(m_ch2.length, m_ch2.length_enable,
				(value & 0x40) != 0, m_ch2.enabled, (value & 0x80) != 0);
			if((value & 0x80) != 0)
			{
				trigger_pulse(m_ch2, m_nr21, m_nr22, m_ch2.frequency,
					ch2_dac_enabled());
			}
			break;
		case 0xFF1A:
			m_nr30 = static_cast<u8>(value & 0x80);
			if(!ch3_dac_enabled())
			{
				m_ch3.enabled = false;
			}
			break;
		case 0xFF1B:
			m_nr31 = value;
			m_ch3.length = static_cast<u16>(256 - value);
			break;
		case 0xFF1C:
			m_nr32 = static_cast<u8>(value & 0x60);
			m_ch3.level = static_cast<u8>((m_nr32 >> 5) & 0x03);
			break;
		case 0xFF1D:
			m_nr33 = value;
			m_ch3.frequency = frequency_from_registers(m_nr33, m_nr34);
			break;
		case 0xFF1E:
			m_nr34 = static_cast<u8>(value & 0x47);
			m_ch3.frequency = frequency_from_registers(m_nr33, m_nr34);
			clock_extra_length(m_ch3.length, m_ch3.length_enable,
				(value & 0x40) != 0, m_ch3.enabled, (value & 0x80) != 0);
			if((value & 0x80) != 0)
			{
				trigger_wave();
			}
			break;
		case 0xFF20:
			m_nr41 = static_cast<u8>(value & 0x3F);
			m_ch4.length = static_cast<u16>(64 - (value & 0x3F));
			break;
		case 0xFF21:
		{
			const u8 previous = m_nr42;
			m_nr42 = value;
			m_ch4.initial_volume = static_cast<u8>(value >> 4);
			m_ch4.env_period = static_cast<u8>(value & 0x07);
			m_ch4.env_increase = (value & 0x08) != 0;
			if(m_ch4.enabled)
			{
				m_ch4.volume = nrx2_glitch_volume(previous, value, m_ch4.volume);
				if(!ch4_dac_enabled())
				{
					m_ch4.enabled = false;
				}
			}
			break;
		}
		case 0xFF22:
			m_nr43 = value;
			m_ch4.clock_shift = static_cast<u8>((m_nr43 >> 4) & 0x0F);
			m_ch4.divisor_code = static_cast<u8>(m_nr43 & 0x07);
			m_ch4.short_mode = (m_nr43 & 0x08) != 0;
			break;
		case 0xFF23:
			m_nr44 = static_cast<u8>(value & 0x40);
			clock_extra_length(m_ch4.length, m_ch4.length_enable,
				(value & 0x40) != 0, m_ch4.enabled, (value & 0x80) != 0);
			if((value & 0x80) != 0)
			{
				trigger_noise();
			}
			break;
		case 0xFF24: m_nr50 = value; break;
		case 0xFF25: m_nr51 = value; break;
		default: break;
	}
}

void APU::power_on()
{
	m_powered = true;
	m_frame_step = 0;
	// Powering on forces the previous DIV-APU edge bit high; if DIV bit 12 is
	// low, the next tick produces an immediate DIV-APU event.
	m_last_div = 0x1000;
}

void APU::power_off()
{
	// On DMG the length counters survive a power cycle and are not clocked
	// while the APU is off.
	const u16 length1 = m_ch1.length;
	const u16 length2 = m_ch2.length;
	const u16 length3 = m_ch3.length;
	const u16 length4 = m_ch4.length;

	m_powered = false;
	clear_registers();

	m_ch1.length = length1;
	m_ch2.length = length2;
	m_ch3.length = length3;
	m_ch4.length = length4;

	m_frame_step = 0;
	m_hpf_left = 0.0f;
	m_hpf_right = 0.0f;
}

void APU::clear_registers()
{
	m_nr10 = 0;
	m_nr11 = 0;
	m_nr12 = 0;
	m_nr13 = 0;
	m_nr14 = 0;
	m_nr21 = 0;
	m_nr22 = 0;
	m_nr23 = 0;
	m_nr24 = 0;
	m_nr30 = 0;
	m_nr31 = 0;
	m_nr32 = 0;
	m_nr33 = 0;
	m_nr34 = 0;
	m_nr41 = 0;
	m_nr42 = 0;
	m_nr43 = 0;
	m_nr44 = 0;
	m_nr50 = 0;
	m_nr51 = 0;

	m_ch1 = Pulse{};
	m_ch2 = Pulse{};
	m_ch3 = Wave{};
	m_ch4 = Noise{};
}

u8 APU::channel_output(Channel channel) const
{
	switch(channel)
	{
		case CH1: return pulse_output(m_ch1);
		case CH2: return pulse_output(m_ch2);
		case CH3: return wave_output();
		case CH4: return noise_output();
		default: return 0;
	}
}

bool APU::is_channel_enabled(Channel channel) const
{
	switch(channel)
	{
		case CH1: return m_ch1.enabled;
		case CH2: return m_ch2.enabled;
		case CH3: return m_ch3.enabled;
		case CH4: return m_ch4.enabled;
		default: return false;
	}
}

u8 APU::channel_volume(Channel channel) const
{
	switch(channel)
	{
		case CH1: return m_ch1.volume;
		case CH2: return m_ch2.volume;
		case CH4: return m_ch4.volume;
		default: return 0;
	}
}

u16 APU::channel_frequency(Channel channel) const
{
	switch(channel)
	{
		case CH1: return m_ch1.frequency;
		case CH2: return m_ch2.frequency;
		case CH3: return m_ch3.frequency;
		default: return 0;
	}
}

u8 APU::pcm12() const
{
	return static_cast<u8>((channel_output(CH2) << 4) | channel_output(CH1));
}

u8 APU::pcm34() const
{
	return static_cast<u8>((channel_output(CH4) << 4) | channel_output(CH3));
}

u8 APU::pulse_output(const Pulse& channel) const
{
	if(!channel.enabled)
	{
		return 0;
	}

	return DUTY[channel.duty & 0x03][channel.duty_step & 0x07] ? channel.volume : 0;
}

void APU::tick_pulse(Pulse& channel)
{
	if(!channel.enabled)
	{
		return;
	}

	if(channel.trigger_delay > 0)
	{
		--channel.trigger_delay;
		return;
	}

	if(channel.timer > 0)
	{
		--channel.timer;
	}
	if(channel.timer == 0)
	{
		channel.timer = static_cast<int>((2048 - channel.frequency) * 4);
		channel.duty_step = static_cast<u8>((channel.duty_step + 1) & 0x07);
	}
}

void APU::trigger_pulse(Pulse& channel, u8 duty_reg, u8 env_reg,
	u16 frequency, bool dac_enabled)
{
	channel.duty = static_cast<u8>((duty_reg >> 6) & 0x03);
	channel.frequency = frequency;

	channel.volume = static_cast<u8>(env_reg >> 4);
	channel.initial_volume = channel.volume;
	channel.env_period = static_cast<u8>(env_reg & 0x07);
	channel.env_increase = (env_reg & 0x08) != 0;
	channel.env_timer = channel.env_period;
	channel.env_reloaded = false;
	channel.env_pending = false;

	if(channel.length == 0)
	{
		channel.length = 64;
		// Triggering an expired length that is already enabled clocks it
		// once when the next frame-sequencer step does not.
		if(channel.length_enable && is_length_step(m_frame_step))
		{
			--channel.length;
		}
	}

	const bool was_enabled = channel.enabled;
	const bool about_to_sample = was_enabled && channel.timer == 4;

	channel.timer = static_cast<int>((2048 - frequency) * 4);
	// The reference implementation measures the trigger delay in 4-T
	// opportunities; this APU is ticked per T-cycle, so the values are
	// calibrated in T-cycles against the SameSuite restart tests.
	channel.trigger_delay = static_cast<u8>(was_enabled
		? 6 + (channel.trigger_delay ? 4 : 0)
		: 12);
	if(about_to_sample)
	{
		// If the channel is one 4-T opportunity away from sampling, the
		// square-wave position is advanced immediately on a re-trigger.
		channel.duty_step = static_cast<u8>((channel.duty_step + 1) & 0x07);
	}
	channel.enabled = dac_enabled;
}

void APU::trigger_sweep()
{
	m_ch1.shadow_frequency = m_ch1.frequency;
	m_ch1.sweep_period = static_cast<u8>((m_nr10 >> 4) & 0x07);
	m_ch1.sweep_shift = static_cast<u8>(m_nr10 & 0x07);
	m_ch1.sweep_decrease = (m_nr10 & 0x08) != 0;
	m_ch1.sweep_timer = m_ch1.sweep_period ? m_ch1.sweep_period : 8;
	m_ch1.sweep_enabled = m_ch1.sweep_period != 0 || m_ch1.sweep_shift != 0;
	m_ch1.sweep_negate_used = false;

	if(m_ch1.sweep_shift != 0)
	{
		const int next = sweep_next_frequency();
		if(m_ch1.sweep_decrease)
		{
			m_ch1.sweep_negate_used = true;
		}
		if(next > 2047)
		{
			m_ch1.enabled = false;
		}
	}
}

int APU::sweep_next_frequency() const
{
	const int shadow = static_cast<int>(m_ch1.shadow_frequency);
	const int delta = shadow >> (m_ch1.sweep_shift & 0x07);
	return m_ch1.sweep_decrease ? shadow - delta : shadow + delta;
}

void APU::clock_sweep()
{
	if(!m_ch1.enabled || !m_ch1.sweep_enabled)
	{
		return;
	}

	// A pace of 0 is treated as 8 by the timer, but no calculation occurs
	// while the pace is zero.
	const u8 period = m_ch1.sweep_period ? m_ch1.sweep_period : 8;
	if(m_ch1.sweep_timer > 0)
	{
		--m_ch1.sweep_timer;
	}
	if(m_ch1.sweep_timer != 0)
	{
		return;
	}
	m_ch1.sweep_timer = period;

	if(m_ch1.sweep_period == 0)
	{
		return;
	}

	const int next = sweep_next_frequency();
	if(m_ch1.sweep_decrease)
	{
		m_ch1.sweep_negate_used = true;
	}
	if(next > 2047)
	{
		m_ch1.enabled = false;
		return;
	}
	if(m_ch1.sweep_shift == 0)
	{
		return;
	}

	m_ch1.shadow_frequency = static_cast<u16>(next);
	m_ch1.frequency = static_cast<u16>(next);
	m_ch1.timer = static_cast<int>((2048 - m_ch1.frequency) * 4);

	// The new frequency is written back to NR13/NR14.
	m_nr13 = static_cast<u8>(next & 0xFF);
	m_nr14 = static_cast<u8>((m_nr14 & ~0x07) | ((next >> 8) & 0x07));

	// Hardware performs a second overflow check without writing it back.
	if(sweep_next_frequency() > 2047)
	{
		m_ch1.enabled = false;
	}
}

u8 APU::wave_output() const
{
	if(!m_ch3.enabled)
	{
		return 0;
	}

	return m_ch3.sample >> WAVE_SHIFT[m_ch3.level & 0x03];
}

void APU::tick_wave()
{
	if(!m_ch3.enabled)
	{
		return;
	}

	if(m_ch3.trigger_delay > 0)
	{
		--m_ch3.trigger_delay;
		return;
	}

	if(m_ch3.timer > 0)
	{
		--m_ch3.timer;
	}
	if(m_ch3.timer == 0)
	{
		m_ch3.timer = static_cast<int>(2048 - m_ch3.frequency);
		m_ch3.sample_index = static_cast<u8>((m_ch3.sample_index + 1) & 0x1F);

		m_ch3.sampled_byte = static_cast<u8>(m_ch3.sample_index >> 1);
		m_ch3.last_sample_cycle = m_cycle;
		m_ch3.sample_valid = true;
		const u8 byte = m_wave_ram[m_ch3.sampled_byte];
		m_ch3.sample = (m_ch3.sample_index & 0x01) ? (byte & 0x0F) : (byte >> 4);
		m_ch3.window_ttl = 2;
	}
}

void APU::trigger_wave()
{
	// On DMG, retriggering while CH3 is about to read a byte corrupts the
	// first four bytes of wave RAM.
	if(m_ch3.enabled && m_ch3.timer <= 1)
	{
		const u8 next_byte = static_cast<u8>(((m_ch3.sample_index + 1) & 0x1F) >> 1);
		if(next_byte < 4)
		{
			m_wave_ram[0] = m_wave_ram[next_byte];
		}
		else
		{
			const u8 base = static_cast<u8>(next_byte & 0x0C);
			for(int i = 0; i < 4; ++i)
			{
				m_wave_ram[i] = m_wave_ram[base + i];
			}
		}
	}

	m_ch3.frequency = frequency_from_registers(m_nr33, m_nr34);
	m_ch3.level = static_cast<u8>((m_nr32 >> 5) & 0x03);

	m_ch3.sample_index = 0;
	m_ch3.sample = 0;
	m_ch3.sample_valid = false;
	m_ch3.trigger_delay = 3;
	m_ch3.timer = static_cast<int>(2048 - m_ch3.frequency);

	if(m_ch3.length == 0)
	{
		m_ch3.length = 256;
		if(m_ch3.length_enable && is_length_step(m_frame_step))
		{
			--m_ch3.length;
		}
	}

	m_ch3.enabled = ch3_dac_enabled();
}

u8 APU::noise_output() const
{
	if(!m_ch4.enabled)
	{
		return 0;
	}

	return (m_ch4.lfsr & 0x01) ? m_ch4.volume : 0;
}

void APU::tick_noise()
{
	if(!m_ch4.enabled)
	{
		return;
	}

	// Clock shifts of 14 and 15 stop the LFSR entirely.
	if(m_ch4.clock_shift >= 14)
	{
		return;
	}

	if(m_ch4.timer > 0)
	{
		--m_ch4.timer;
	}
	if(m_ch4.timer == 0)
	{
		static constexpr u8 DIVISOR[8] = {8, 16, 32, 48, 64, 80, 96, 112};
		m_ch4.timer = DIVISOR[m_ch4.divisor_code & 0x07] << (m_ch4.clock_shift & 0x0F);

		const u16 feedback = static_cast<u16>((m_ch4.lfsr ^ (m_ch4.lfsr >> 1)) & 0x01);
		m_ch4.lfsr = static_cast<u16>((m_ch4.lfsr >> 1) | (feedback << 14));
		if(m_ch4.short_mode)
		{
			m_ch4.lfsr = static_cast<u16>((m_ch4.lfsr & ~0x0040) | (feedback << 6));
		}
	}
}

void APU::trigger_noise()
{
	m_ch4.volume = static_cast<u8>(m_nr42 >> 4);
	m_ch4.initial_volume = m_ch4.volume;
	m_ch4.env_period = static_cast<u8>(m_nr42 & 0x07);
	m_ch4.env_increase = (m_nr42 & 0x08) != 0;
	m_ch4.env_timer = m_ch4.env_period;
	m_ch4.env_reloaded = false;
	m_ch4.env_pending = false;

	m_ch4.clock_shift = static_cast<u8>((m_nr43 >> 4) & 0x0F);
	m_ch4.divisor_code = static_cast<u8>(m_nr43 & 0x07);
	m_ch4.short_mode = (m_nr43 & 0x08) != 0;

	if(m_ch4.length == 0)
	{
		m_ch4.length = 64;
		if(m_ch4.length_enable && is_length_step(m_frame_step))
		{
			--m_ch4.length;
		}
	}

	m_ch4.lfsr = 0x7FFF;

	static constexpr u8 DIVISOR[8] = {8, 16, 32, 48, 64, 80, 96, 112};
	m_ch4.timer = DIVISOR[m_ch4.divisor_code] << m_ch4.clock_shift;
	m_ch4.enabled = ch4_dac_enabled();
}

void APU::mix_and_emit()
{
	int left = 0;
	int right = 0;

	mix_channel(CH1, left, right);
	mix_channel(CH2, left, right);
	mix_channel(CH3, left, right);
	mix_channel(CH4, left, right);

	// NR50 master volume: 0 means 1/8 and 7 means 8/8.
	left = left * (((m_nr50 >> 4) & 0x07) + 1) / 8;
	right = right * ((m_nr50 & 0x07) + 1) / 8;

	// Scale 1000 leaves headroom for the four channels (max |sum| is 32).
	const float left_filtered = high_pass(static_cast<float>(left * 1000), m_hpf_left);
	const float right_filtered = high_pass(static_cast<float>(right * 1000), m_hpf_right);

	push_sample(clamp_sample(static_cast<int>(left_filtered)),
		clamp_sample(static_cast<int>(right_filtered)));
}

void APU::mix_channel(Channel channel, int& left, int& right) const
{
	if(!is_channel_enabled(channel))
	{
		return;
	}

	// Center the 0..15 DAC value around 0 so an idle channel is silent and a
	// playing one has no DC offset.
	const int value = 7 - static_cast<int>(channel_output(channel));
	if(m_nr51 & (1 << (4 + channel)))
	{
		left += value;
	}
	if(m_nr51 & (1 << channel))
	{
		right += value;
	}
}

void APU::push_sample(int16_t left, int16_t right)
{
	if(m_ring_count == RING_FRAMES)
	{
		// Buffer full: drop the oldest frame.
		m_ring_read = (m_ring_read + 1) % RING_FRAMES;
		--m_ring_count;
	}

	m_ring[m_ring_write * 2 + 0] = left;
	m_ring[m_ring_write * 2 + 1] = right;
	m_ring_write = (m_ring_write + 1) % RING_FRAMES;
	++m_ring_count;
}

size_t APU::read_samples(int16_t* dst, size_t frames)
{
	const size_t count = frames < m_ring_count ? frames : m_ring_count;
	for(size_t i = 0; i < count; ++i)
	{
		dst[i * 2 + 0] = m_ring[m_ring_read * 2 + 0];
		dst[i * 2 + 1] = m_ring[m_ring_read * 2 + 1];
		m_ring_read = (m_ring_read + 1) % RING_FRAMES;
	}
	m_ring_count -= count;
	return count;
}
