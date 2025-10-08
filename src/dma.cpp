#include "dma.h"

void DMA::dma_start(u8 start)
{
	m_active = true;
	m_byte = 0;
	m_value = start;
	m_start_delay = 1;
}

void DMA::dma_tick()
{
	if(!m_active)
	{
		return;
	}

	if(m_start_delay != 0)
	{
		m_start_delay--;
		return;
	}

	m_ppu->ppu_oam_write(m_byte, m_bus->read((m_value * 0x100) + m_byte));

	m_byte++;

	m_active = m_byte < 0xA0;
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
;
}
