#include "bus.h"
#include "QtreGB.h"

using namespace qtreGB;

Bus::Bus(Cart* cart, Timer* timer, DMA* dma, Gamepad* gamepad, Memory* wram, Memory* hram, IO* io, LCD* lcd, PPU* ppu, ICPURegisters* cpu_registers)
{
	m_cart = cart;
	m_timer = timer;
	m_dma = dma;
	m_gamepad = gamepad;
	m_wram = wram;
	m_hram = hram;
	m_io = io;
	m_lcd = lcd;
	m_ppu = ppu;

	m_cpu_registers = cpu_registers;

}

u8 Bus::read(u16 address)
{
	if(address < 0x8000)
	{
		return m_cart->read(address);
	}
	else if(address < 0xA000)
	{
		return m_ppu->ppu_vram_read(address - 0x8000);
	}
	else if(address < 0xC000)
	{
		return m_cart->read(address);
	}
	else if(address < 0xE000)
	{
		return m_wram->read(address - 0xC000);
	}
	else if(address < 0xFE00)
	{
		// Echo RAM: mirrors 0xC000-0xDDFF (confirmed on all official hardware)
		return m_wram->read(address - 0xE000);
	}
	else if(address < 0xFEA0)
	{
		if(m_dma->dma_transferring())
		{
			return 0xFF;
		}
		return m_ppu->ppu_oam_read(address - 0xFE00);
	}
	else if(address < 0xFF00)
	{
		return 0xFF;
	}
	else if(address < 0xFF80)
	{
		// IO registers

		// gamepad
		if(address == 0xFF00)
		{
			return m_gamepad->get_state();
		}
		// timer
		else if(address == 0xFF04)
		{
			return m_timer->read_div();
		}
		else if(address == 0xFF05)
		{
			return m_timer->read_tima();
		}
		else if(address == 0xFF06)
		{
			return m_timer->read_tma();
		}
		else if(address == 0xFF07)
		{
			return m_timer->read_tac();
		}
		else if(address >= 0xFF40 && address <= 0xFF4B)
		{
			if(address == 0xFF46)
			{
				return m_dma->get_value();
			}
			return m_lcd->read(address - 0xFF40);
		}
		// Interrupts
		else if(address == 0xFF0F)
		{
			return m_cpu_registers->read_IF();
		}

		return m_io->io_read(address);
	}
	else if(address < 0xFFFF)
	{
		return m_hram->read(address - 0xFF80);
	}
	else if(address == 0xFFFF)
	{
		return m_cpu_registers->read_IE();
	}

	return 0xFF;
}

void Bus::write(u16 address, u8 value)
{
	if(address < 0x8000)
	{
		m_cart->write(address, value);
	}
	else if(address < 0xA000)
	{
		m_ppu->ppu_vram_write(address - 0x8000, value);
	}
	else if(address < 0xC000)
	{
		m_cart->write(address, value);
	}
	else if(address < 0xE000)
	{
		m_wram->write(address - 0xC000, value);
	}
	else if(address < 0xFE00)
	{
		m_wram->write(address - 0xE000, value);
	}
	else if(address < 0xFEA0)
	{
		if(m_dma->dma_transferring())
		{
			return;
		}
		m_ppu->ppu_oam_write(address - 0xFE00, value);
	}
	else if(address < 0xFF00)
	{

	}
	else if(address < 0xFF80)
	{
		// IO
		if(address == 0xFF00)
		{
			m_gamepad->set_selection(value);
		}
		else if(address == 0xFF04)
		{
			m_timer->write_div();
		}
		else if(address == 0xFF05)
		{
			m_timer->write_tima(value);
		}
		else if(address == 0xFF06)
		{
			m_timer->write_tma(value);
		}
		else if(address == 0xFF07)
		{
			m_timer->write_tac(value);
		}
		else if(address == 0xFF0F)
		{
			m_cpu_registers->write_IF(value);
		}
		else if(address >= 0xFF40 && address <= 0xFF4B)
		{
			if(address == 0xFF46)
			{
				m_dma->dma_start(value);
			}
			else
			{
				m_lcd->write(address - 0xFF40, value);

				// The LCD power transition is applied synchronously at the LCDC write so that a
				// STAT interrupt from the restarted LYC comparison is already pending when the CPU
				// runs its end-of-opcode interrupt check (mooneye stat_lyc_onoff round 4).
				if(address == 0xFF40)
				{
					m_ppu->ppu_sync_lcd_power();
				}
				else if(address == 0xFF41)
				{
					m_ppu->ppu_stat_write();
				}
			}
		}
		else
		{
			m_io->io_write(address, value);
		}
	}
	else if(address < 0xFFFF)
	{
		m_hram->write(address - 0xFF80, value);
	}
	else if(address == 0xFFFF)
	{
		m_cpu_registers->write_IE(value);
	}
}
