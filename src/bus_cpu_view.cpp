#include "bus_cpu_view.h"

using namespace qtreGB;

BusCpuView::BusCpuView(Bus *real_bus, PPU *ppu, DMA *dma) :
m_real_bus(real_bus),
m_ppu(ppu),
m_dma(dma)
{}

u8 BusCpuView::read(u16 address)
{
	// While an OAM DMA transfer owns the buses the CPU can only reach HRAM
	// (plus the DMA register itself). Everything else reads as 0xFF.
	if(m_dma->dma_transferring() && address < 0xFF80 && address != 0xFF46)
	{
		return 0xFF;
	}

	m_last_value = m_real_bus->read(address);

	return m_last_value;
}

void BusCpuView::write(u16 address, u8 value)
{
	if(m_dma->dma_transferring() && address < 0xFF80 && address != 0xFF46)
	{
		return;
	}

	m_real_bus->write(address, value);
}
