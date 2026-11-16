#include "apu.h"

using namespace qtreGB;

void APU::reset()
{
	clear_registers();
	for(u8& byte : m_wave_ram)
	{
		byte = 0;
	}

	m_powered = false;
	m_channel_status = 0;
	m_frame_counter = 0;
	m_frame_step = 0;
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
		// channel status (all off until the channels are implemented).
		return static_cast<u8>(0x70 | (m_powered ? 0x80 : 0x00) | m_channel_status);
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
		case 0xFF11: m_nr11 = value; break;
		case 0xFF12: m_nr12 = value; break;
		case 0xFF13: m_nr13 = value; break;
		case 0xFF14: m_nr14 = static_cast<u8>(value & 0x47); break;
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
	m_channel_status = 0;
	m_frame_counter = 0;
	m_frame_step = 0;
}

void APU::power_off()
{
	m_powered = false;
	clear_registers();
	m_channel_status = 0;
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
}
