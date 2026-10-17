#include "bus_cpu_view.h"

using namespace qtreGB;

BusCpuView::BusCpuView(Bus *real_bus, PPU *ppu, DMA *dma) :
m_real_bus(real_bus),
m_ppu(ppu),
m_dma(dma)
{}

u8 BusCpuView::read(u16 address)
{
	// During an OAM DMA transfer the DMA controller owns OAM, so CPU reads of
	// $FE00-$FE9F return $FF. Every other area stays accessible: the CPU keeps
	// executing (the DMA only contends for OAM), which is why the OAM DMA tests
	// can run code out of echo WRAM / OAM while a transfer is in progress.
	if(m_dma->dma_transferring() && address >= 0xFE00 && address < 0xFEA0)
	{
		return 0xFF;
	}

	m_last_value = m_real_bus->read(address);

	return m_last_value;
}

void BusCpuView::write(u16 address, u8 value)
{
	// CPU writes to OAM are ignored while the DMA controller owns it.
	if(m_dma->dma_transferring() && address >= 0xFE00 && address < 0xFEA0)
	{
		return;
	}

	m_real_bus->write(address, value);
}
