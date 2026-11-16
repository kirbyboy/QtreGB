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

	tick_ch1();

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
		// channel status; only CH1 exists for now.
		const u8 status = m_ch1.enabled ? 0x01 : 0x00;
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
		case 0xFF22: return static_cast<u8>(m_nr43 | 0x08);	// bit 3 unused
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
			update_ch1_frequency();
			break;
		case 0xFF14:
			m_nr14 = static_cast<u8>(value & 0x47);
			update_ch1_frequency();
			if((value & 0x80) != 0)
			{
				trigger_ch1();
			}
			break;
		case 0xFF16: m_nr21 = value; break;
		case 0xFF17: m_nr22 = value; break;
		case 0xFF18: m_nr23 = value; break;
		case 0xFF19: m_nr24 = static_cast<u8>(value & 0x47); break;
		case 0xFF1A: m_nr30 = static_cast<u8>(value & 0x80); break;
		case 0xFF1B: m_nr31 = value; break;
		case 0xFF1C: m_nr32 = static_cast<u8>(value & 0x60); break;
		case 0xFF1D: m_nr33 = value; break;
		case 0xFF1E: m_nr34 = static_cast<u8>(value & 0x47); break;
		case 0xFF20: m_nr41 = static_cast<u8>(value & 0x3F); break;
		case 0xFF21: m_nr42 = value; break;
		case 0xFF22: m_nr43 = static_cast<u8>(value & 0xF7); break;
		case 0xFF23: m_nr44 = static_cast<u8>(value & 0x40); break;
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
}

u8 APU::channel_output(Channel channel) const
{
	if(channel != CH1 || !m_ch1.enabled)
	{
		return 0;
	}

	return DUTY[m_ch1.duty & 0x03][m_ch1.duty_step & 0x07] ? m_ch1.volume : 0;
}

bool APU::is_channel_enabled(Channel channel) const
{
	return channel == CH1 && m_ch1.enabled;
}

void APU::tick_ch1()
{
	if(!m_ch1.enabled)
	{
		return;
	}

	if(m_ch1.timer > 0)
	{
		--m_ch1.timer;
	}
	if(m_ch1.timer == 0)
	{
		m_ch1.timer = static_cast<int>((2048 - m_ch1.frequency) * 4);
		m_ch1.duty_step = static_cast<u8>((m_ch1.duty_step + 1) & 0x07);
	}
}

void APU::trigger_ch1()
{
	m_ch1.duty = static_cast<u8>((m_nr11 >> 6) & 0x03);
	update_ch1_frequency();

	m_ch1.volume = static_cast<u8>(m_nr12 >> 4);
	m_ch1.initial_volume = m_ch1.volume;
	m_ch1.env_period = static_cast<u8>(m_nr12 & 0x07);
	m_ch1.env_increase = (m_nr12 & 0x08) != 0;

	m_ch1.timer = static_cast<int>((2048 - m_ch1.frequency) * 4);
	m_ch1.enabled = ch1_dac_enabled();
}

void APU::update_ch1_frequency()
{
	m_ch1.frequency = static_cast<u16>(((m_nr14 & 0x07) << 8) | m_nr13);
}

void APU::mix_and_emit()
{
	// Milestone 4 (reduced): CH1 only. Center the 0..15 DAC value around 0 so
	// an idle channel is silent and a playing one has no DC offset.
	int sample = 0;
	if(is_channel_enabled(CH1))
	{
		sample = (7 - static_cast<int>(channel_output(CH1))) * 2000;
	}

	push_sample(static_cast<int16_t>(sample), static_cast<int16_t>(sample));
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
