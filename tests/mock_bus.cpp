#include "mock_bus.h"

using namespace qtreGB;

MockBus::MockBus() :
m_dma_transferring(false)
{
}

u8 MockBus::read(u16 address)
{
	if(address >= 0x8000 && address < 0xA000)
	{
		return m_ppu->ppu_vram_read(address - 0x8000);
	}
	else if(address >= 0xFE00 && address < 0xFEA0)
	{
		if(m_dma_transferring)
		{
			return 0xFF;
		}
		return m_ppu->ppu_oam_read(address - 0xFE00);
	}
	else
	{
		return 0xFE;	// just another random value that is not 0xFF
	}
}

void MockBus::write(u16 address, u8 value)
{
	if(address >= 0x8000 && address < 0xA000)
	{
		m_ppu->ppu_vram_write(address - 0x8000, value);
	}
	else if(address >= 0xFE00 && address < 0xFEA0)
	{
		if(!m_dma_transferring)
		{
			m_ppu->ppu_oam_write(address - 0xFE00, value);
		}
	}
}

void MockBus::connect(PPU *ppu)
{
	m_ppu = ppu;
}

void MockBus::set_dma_transferring(bool transferring)
{
	m_dma_transferring = transferring;
}
