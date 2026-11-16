#include "apu.h"

using namespace qtreGB;

namespace
{

// One pulse period, one entry per duty step. 1 = high, 0 = low.
constexpr u8 DUTY[4][8] = {
	{0, 0, 0, 0, 0, 0, 0, 1},	// 12.5 %
	{1, 0, 0, 0, 0, 0, 0, 1},	// 25 %
	{1, 0, 0, 0, 0, 1, 1, 1},	// 50 %
	{0, 1, 1, 1, 1, 1, 1, 0},	// 75 %
};

// NR32 output level: 0 mutes, 1 is 100 %, 2 is 50 %, 3 is 25 %.
constexpr u8 WAVE_SHIFT[4] = {4, 0, 1, 2};

u16 channel_frequency(u8 low, u8 high)
{
	return static_cast<u16>(((high & 0x07) << 8) | low);
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

} // namespace

void APU::reset()
{
	clear_registers();
	for(u8& byte : m_wave_ram)
	{
		byte = 0;
	}

	m_powered = false;
	m_frame_counter = 0;
	m_frame_step = 0;

	m_sample_acc = 0;
	m_ring_read = 0;
	m_ring_write = 0;
	m_ring_count = 0;
}

void APU::tick()
{
	if(!m_powered)
	{
		return;
	}

	if(++m_frame_counter >= 8192)
	{
		m_frame_counter = 0;
		m_frame_step = static_cast<u8>((m_frame_step + 1) & 0x07);
	}

	tick_pulse(m_ch1);
	tick_pulse(m_ch2);
	tick_wave();
	tick_noise();

	// Nearest-neighbour resampling: emit one frame every CLOCK/SAMPLE_RATE
	// T-cycles (about 87.38 at 48 kHz).
	m_sample_acc += SAMPLE_RATE;
	if(m_sample_acc >= CLOCK)
	{
		m_sample_acc -= CLOCK;
		mix_and_emit();
	}
}

u8 APU::read(u16 address)
{
	if(address >= 0xFF30 && address <= 0xFF3F)
	{
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

	if(!m_powered && address >= 0xFF10 && address <= 0xFF25)
	{
		return 0;
	}

	switch(address)
	{
		case 0xFF10: return static_cast<u8>(m_nr10 | 0x80);	// bit 7 unused
		case 0xFF11: return m_nr11;
		case 0xFF12: return m_nr12;
		case 0xFF13: return m_nr13;
		case 0xFF14: return static_cast<u8>((m_nr14 & 0x47) | 0x38);	// trigger reads 0, bits 5-3 unused
		case 0xFF16: return m_nr21;
		case 0xFF17: return m_nr22;
		case 0xFF18: return m_nr23;
		case 0xFF19: return static_cast<u8>((m_nr24 & 0x47) | 0x38);
		case 0xFF1A: return static_cast<u8>(m_nr30 | 0x7F);	// bits 6-0 unused
		case 0xFF1B: return m_nr31;
		case 0xFF1C: return static_cast<u8>(m_nr32 | 0x9F);	// bits 7 and 4-0 unused
		case 0xFF1D: return m_nr33;
		case 0xFF1E: return static_cast<u8>((m_nr34 & 0x47) | 0x38);
		case 0xFF20: return static_cast<u8>(m_nr41 | 0xC0);	// bits 7-6 unused
		case 0xFF21: return m_nr42;
		case 0xFF22: return m_nr43;
		case 0xFF23: return static_cast<u8>((m_nr44 & 0x40) | 0x3F);	// trigger reads 0, bits 5-0 unused
		case 0xFF24: return m_nr50;
		case 0xFF25: return m_nr51;
		default: return 0xFF;
	}
}

void APU::write(u16 address, u8 value)
{
	if(address >= 0xFF30 && address <= 0xFF3F)
	{
		m_wave_ram[address - 0xFF30] = value;
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
		return;
	}

	switch(address)
	{
		case 0xFF10: m_nr10 = static_cast<u8>(value & 0x7F); break;
		case 0xFF11:
			m_nr11 = value;
			m_ch1.duty = static_cast<u8>((value >> 6) & 0x03);
			break;
		case 0xFF12:
			m_nr12 = value;
			if(!ch1_dac_enabled())
			{
				m_ch1.enabled = false;
			}
			break;
		case 0xFF13:
			m_nr13 = value;
			m_ch1.frequency = channel_frequency(m_nr13, m_nr14);
			break;
		case 0xFF14:
			m_nr14 = static_cast<u8>(value & 0x47);
			m_ch1.frequency = channel_frequency(m_nr13, m_nr14);
			if((value & 0x80) != 0)
			{
				trigger_pulse(m_ch1, m_nr11, m_nr12, m_ch1.frequency,
					ch1_dac_enabled());
			}
			break;
		case 0xFF16:
			m_nr21 = value;
			m_ch2.duty = static_cast<u8>((value >> 6) & 0x03);
			break;
		case 0xFF17:
			m_nr22 = value;
			if(!ch2_dac_enabled())
			{
				m_ch2.enabled = false;
			}
			break;
		case 0xFF18:
			m_nr23 = value;
			m_ch2.frequency = channel_frequency(m_nr23, m_nr24);
			break;
		case 0xFF19:
			m_nr24 = static_cast<u8>(value & 0x47);
			m_ch2.frequency = channel_frequency(m_nr23, m_nr24);
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
			break;
		case 0xFF1C:
			m_nr32 = static_cast<u8>(value & 0x60);
			m_ch3.level = static_cast<u8>((m_nr32 >> 5) & 0x03);
			break;
		case 0xFF1D:
			m_nr33 = value;
			m_ch3.frequency = channel_frequency(m_nr33, m_nr34);
			break;
		case 0xFF1E:
			m_nr34 = static_cast<u8>(value & 0x47);
			m_ch3.frequency = channel_frequency(m_nr33, m_nr34);
			if((value & 0x80) != 0)
			{
				trigger_wave();
			}
			break;
		case 0xFF20:
			m_nr41 = static_cast<u8>(value & 0x3F);
			break;
		case 0xFF21:
			m_nr42 = value;
			if(!ch4_dac_enabled())
			{
				m_ch4.enabled = false;
			}
			break;
		case 0xFF22:
			m_nr43 = value;
			m_ch4.clock_shift = static_cast<u8>((m_nr43 >> 4) & 0x0F);
			m_ch4.divisor_code = static_cast<u8>(m_nr43 & 0x07);
			m_ch4.short_mode = (m_nr43 & 0x08) != 0;
			break;
		case 0xFF23:
			m_nr44 = static_cast<u8>(value & 0x40);
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
	m_frame_counter = 0;
	m_frame_step = 0;
}

void APU::power_off()
{
	m_powered = false;
	clear_registers();
	m_frame_counter = 0;
	m_frame_step = 0;
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

	channel.timer = static_cast<int>((2048 - frequency) * 4);
	channel.enabled = dac_enabled;
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

	if(m_ch3.timer > 0)
	{
		--m_ch3.timer;
	}
	if(m_ch3.timer == 0)
	{
		m_ch3.timer = static_cast<int>((2048 - m_ch3.frequency) * 2);
		m_ch3.sample_index = static_cast<u8>((m_ch3.sample_index + 1) & 0x1F);

		const u8 byte = m_wave_ram[m_ch3.sample_index >> 1];
		m_ch3.sample = (m_ch3.sample_index & 0x01) ? (byte & 0x0F) : (byte >> 4);
	}
}

void APU::trigger_wave()
{
	m_ch3.frequency = channel_frequency(m_nr33, m_nr34);
	m_ch3.level = static_cast<u8>((m_nr32 >> 5) & 0x03);

	m_ch3.sample_index = 0;
	m_ch3.sample = 0;
	m_ch3.timer = static_cast<int>((2048 - m_ch3.frequency) * 2);
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

	m_ch4.clock_shift = static_cast<u8>((m_nr43 >> 4) & 0x0F);
	m_ch4.divisor_code = static_cast<u8>(m_nr43 & 0x07);
	m_ch4.short_mode = (m_nr43 & 0x08) != 0;

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

	push_sample(clamp_sample(left * 1500), clamp_sample(right * 1500));
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
