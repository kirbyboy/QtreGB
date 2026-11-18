#include "serial.h"

#include "io.h"
#include "link_cable.h"
#include "state_stream.h"

using namespace qtreGB;

namespace
{

constexpr u16 SB_ADDR = 0xFF01;
constexpr u16 SC_ADDR = 0xFF02;
constexpr u16 SERIAL_BIT_TICKS = 512;	// 8192 Hz at 4.194304 MHz
constexpr u8 SERIAL_BITS = 8;

constexpr u16 SERIAL_ACTIVE = 0x0001;
constexpr u16 SERIAL_BITS_REMAINING = 0x0002;
constexpr u16 SERIAL_CLOCK_COUNTER = 0x0003;

} // namespace

Serial::Serial(IO* io) : m_io(io)
{
}

void Serial::reset()
{
	m_active = false;
	m_bits_remaining = 0;
	m_clock_counter = 0;
	if(m_io)
	{
		m_io->io_write(SB_ADDR, 0);
		m_io->io_write(SC_ADDR, 0);
	}
}

bool Serial::is_internal_clock() const
{
	return m_active && m_io && (m_io->io_read(SC_ADDR) & 0x01) != 0;
}

bool Serial::is_external_clock() const
{
	return m_active && m_io && (m_io->io_read(SC_ADDR) & 0x01) == 0;
}

void Serial::tick()
{
	if(!m_io)
	{
		return;
	}

	const u8 sc = m_io->io_read(SC_ADDR);
	if((sc & 0x80) == 0)
	{
		// A write of 0 to SC cancels a transfer waiting to finish.
		m_active = false;
		m_bits_remaining = 0;
		m_clock_counter = 0;
		return;
	}

	if(!m_active)
	{
		m_active = true;
		m_bits_remaining = SERIAL_BITS;
		m_clock_counter = SERIAL_BIT_TICKS;
	}

	if((sc & 0x01) == 0)
	{
		// External clock: wait for LinkCable to shift bits.
		return;
	}

	if(m_clock_counter > 0)
	{
		--m_clock_counter;
	}
	if(m_clock_counter == 0)
	{
		m_clock_counter = SERIAL_BIT_TICKS;
		if(m_link != nullptr)
		{
			m_link->clock_from(this);
		}
		else
		{
			// No peer: the serial input line floats high.
			shift_from_link(1);
		}
	}
}

u8 Serial::output_bit() const
{
	if(!m_active || !m_io)
	{
		return 1;
	}
	return static_cast<u8>((m_io->io_read(SB_ADDR) >> 7) & 0x01);
}

void Serial::shift_from_link(u8 input_bit)
{
	if(!m_active || !m_io)
	{
		return;
	}

	const u8 sb = m_io->io_read(SB_ADDR);
	m_io->io_write(SB_ADDR,
		static_cast<u8>((sb << 1) | (input_bit & 0x01)));

	if(--m_bits_remaining == 0)
	{
		complete();
	}
}

void Serial::complete()
{
	m_active = false;
	m_bits_remaining = 0;
	m_clock_counter = 0;

	if(m_io)
	{
		const u8 sc = m_io->io_read(SC_ADDR);
		m_io->io_write(SC_ADDR, static_cast<u8>(sc & 0x7F));
	}
	if(m_interrupt)
	{
		m_interrupt(InterruptType::IT_SERIAL);
	}
}

void Serial::save_state(StateWriter& writer) const
{
	writer.write_bool(SERIAL_ACTIVE, m_active);
	writer.write_u8(SERIAL_BITS_REMAINING, m_bits_remaining);
	writer.write_u16(SERIAL_CLOCK_COUNTER, m_clock_counter);
}

bool Serial::load_state(StateReader& reader)
{
	bool active = false;
	u8 bits_remaining = 0;
	u16 clock_counter = 0;

	if(!reader.read_bool(SERIAL_ACTIVE, active) ||
	   !reader.read_u8(SERIAL_BITS_REMAINING, bits_remaining) ||
	   !reader.read_u16(SERIAL_CLOCK_COUNTER, clock_counter))
	{
		return false;
	}

	if(bits_remaining > SERIAL_BITS || clock_counter > SERIAL_BIT_TICKS ||
	   (active && bits_remaining == 0))
	{
		reader.mark_error(StateError::BadValue);
		return false;
	}

	m_active = active;
	m_bits_remaining = bits_remaining;
	m_clock_counter = clock_counter;
	return true;
}
