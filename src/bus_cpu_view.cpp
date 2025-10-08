#include "bus_cpu_view.h"

BusCpuView::BusCpuView(Bus *real_bus, PPU *ppu) :
m_real_bus(real_bus),
m_ppu(ppu)
{}

u8 BusCpuView::read(u16 address)
{
	if(!(address >= 0x8000 && address < 0xA000 && m_ppu->is_vram_locked()))
	{
		m_last_value = m_real_bus->read(address);
	}

	return m_last_value;
}

void BusCpuView::write(u16 address, u8 value)
{
	if(address >= 0x8000 && address < 0xA000 && m_ppu->is_vram_locked())
	{
		return;
	}
	m_real_bus->write(address, value);
}
