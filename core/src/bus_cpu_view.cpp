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

	// The PPU owns VRAM/OAM on its own T-cycles. The lock lives in the PPU;
	// the CPU view is the one that turns a CPU access into 0xFF.
	if(address >= 0x8000 && address < 0xA000 && m_ppu->is_vram_read_locked())
	{
		m_last_value = 0xFF;
		return 0xFF;
	}

	if(address >= 0xFE00 && address < 0xFEA0 && m_ppu->is_oam_read_locked())
	{
		m_last_value = 0xFF;
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

	if(address >= 0x8000 && address < 0xA000 && m_ppu->is_vram_write_locked())
	{
		return;
	}

	if(address >= 0xFE00 && address < 0xFEA0 && m_ppu->is_oam_write_locked())
	{
		return;
	}

	m_real_bus->write(address, value);
}
