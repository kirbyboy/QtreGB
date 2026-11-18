#include "dma.h"
#include "state_stream.h"

using namespace qtreGB;

namespace
{

constexpr u16 DMA_ACTIVE = 0x0001;
constexpr u16 DMA_PENDING = 0x0002;
constexpr u16 DMA_BYTE = 0x0003;
constexpr u16 DMA_VALUE = 0x0004;
constexpr u16 DMA_START_DELAY = 0x0005;

} // namespace

void DMA::dma_start(u8 start)
{
	// A write to FF46 requests a transfer. It does not begin immediately:
	// the write cycle (M0) and the following one (M1) are idle and OAM stays
	// reachable; at M2 the DMA takes over and one byte is copied per machine
	// cycle (160 total).
	m_value = start;
	m_byte = 0;
	m_pending = true;
	m_start_delay = 2;
	if(m_ppu)
	{
		m_ppu->set_dma_active(false);
	}
}

void DMA::dma_tick()
{
	if(!m_pending)
	{
		return;
	}

	if(m_start_delay != 0)
	{
		m_start_delay--;
		if(m_start_delay == 1)
		{
			// M2: the DMA owns OAM from here on (CPU reads return $FF).
			m_active = true;
			if(m_ppu)
			{
				m_ppu->set_dma_active(true);
			}
		}
		return;
	}

	m_ppu->ppu_oam_write_dma(m_byte, m_bus->read((m_value * 0x100) + m_byte));

	m_byte++;

	if(m_byte >= 0xA0)
	{
		m_active = false;
		m_pending = false;
		if(m_ppu)
		{
			m_ppu->set_dma_active(false);
		}
	}
}

bool DMA::dma_transferring()
{
	return m_active;
}

void DMA::connect(IBus* bus, PPU* ppu)
{
	m_bus = bus;
	m_ppu = ppu;
}

u8 DMA::get_value()
{
	return m_value;
}

void DMA::reset()
{
	m_active = false;
	m_pending = false;
	m_byte = 0;
	m_value = 0;
	m_start_delay = 0;
	if(m_ppu)
	{
		m_ppu->set_dma_active(false);
	}
}

void DMA::save_state(StateWriter& writer) const
{
	writer.write_bool(DMA_ACTIVE, m_active);
	writer.write_bool(DMA_PENDING, m_pending);
	writer.write_u8(DMA_BYTE, m_byte);
	writer.write_u8(DMA_VALUE, m_value);
	writer.write_u8(DMA_START_DELAY, m_start_delay);
}

bool DMA::load_state(StateReader& reader)
{
	bool active = false;
	bool pending = false;
	u8 byte = 0;
	u8 value = 0;
	u8 start_delay = 0;

	if(!reader.read_bool(DMA_ACTIVE, active) ||
	   !reader.read_bool(DMA_PENDING, pending) ||
	   !reader.read_u8(DMA_BYTE, byte) ||
	   !reader.read_u8(DMA_VALUE, value) ||
	   !reader.read_u8(DMA_START_DELAY, start_delay))
	{
		return false;
	}

	if(start_delay > 2 || byte > 0xA0 || (active && !pending))
	{
		reader.mark_error(StateError::BadValue);
		return false;
	}

	m_active = active;
	m_pending = pending;
	m_byte = byte;
	m_value = value;
	m_start_delay = start_delay;

	// The PPU mirrors the DMA ownership flag; keep it in sync on load.
	if(m_ppu)
	{
		m_ppu->set_dma_active(m_active);
	}
	return true;
}
