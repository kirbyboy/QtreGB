#include "dma.h"

using namespace qtreGB;

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
		}
		return;
	}

	m_ppu->ppu_oam_write_dma(m_byte, m_bus->read((m_value * 0x100) + m_byte));

	m_byte++;

	if(m_byte >= 0xA0)
	{
		m_active = false;
		m_pending = false;
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
