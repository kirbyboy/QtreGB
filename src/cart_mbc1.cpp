#include "types.h"
#include "cart_mbc1.h"

using namespace qtreGB;

using namespace std;

CartMBC1::CartMBC1()
{
	m_banking_mode = false;
	m_ram_enable = false;
	m_rom_bank = 1;
	m_rom_ram_bank = 0;
}

CartMBC1::~CartMBC1()
{
}

bool CartMBC1::load_rom(std::vector<u8>& rom)
{
	m_rom = rom;
	return true;
}

std::vector<Memory> &CartMBC1::get_ram()
{
	return m_ram;
}

void CartMBC1::set_ram_banks(u8 banks)
{
	if(banks > 0)
	{
		m_ram = vector<Memory>(banks, Memory(0x2000));
	}
}

u8 CartMBC1::read(u16 address)
{
	if(address < 0x4000)
	{
		u8 result;
		if(m_rom[0x0148] > 4 && m_banking_mode)	// 1Mb cartridges or more and mode 1
		{
			u8 rom_bank = m_rom_ram_bank << 4 | m_rom_bank;
			result = m_rom[(rom_bank * 0x4000) + address];
		}
		else
		{
			result = m_rom[address];
		}
		return result;
	}
	else if(address < 0x8000)
	{
		u8 rom_bank = m_rom_bank;
		if(rom_bank == 0)
		{
			rom_bank = 1;
		}
		return m_rom[(rom_bank * 0x4000) + address - 0x4000];
	}
	else if(address >= 0xA000 && address < 0xC000)
	{
		if(m_ram_enable && m_ram.size() > 0)
		{
			u8 ram_bank = m_rom_ram_bank;
			if(ram_bank <= m_ram.size())
			{
				return m_ram[ram_bank].read(address - 0xA000);
			}
		}
		return 0xFF;
	}

	return 0xFF;	// It should never reach here...
}

void CartMBC1::write(u16 address, u8 value)
{
	if(address < 0x2000)
	{
		if((value & 0x0A) != 0 && m_ram.size() > 0)
		{
			m_ram_enable = true;
		}
		else
		{
			m_ram_enable = false;
		}
	}
	else if(address < 0x4000)
	{
		u8 rom_bank_value = (value & 0x1F);
		u8 rom_bank_size = 1 << (1 + m_rom[0x148]);

		if(rom_bank_value > rom_bank_size)
		{
			u8 mask = 0;
			for(int i = 0; i < 5; i++)
			{
				if(i <= rom_bank_size)
				{
					mask |= 1 << i;
				}
				else
				{
					break;
				}
			}
			rom_bank_value &= mask;
		}
		m_rom_bank = rom_bank_value;
	}
	else if(address < 0x6000)
	{
		if(m_rom[0x0149] == 0x03)	// 32Kb of RAM
		{
			m_rom_ram_bank = value & 0x03;
		}
		else if(m_rom[0x0148] > 4)	// 1Mb cartridges or more
		{
			m_rom_ram_bank = value & 0x03;
		}
	}
	else if(address < 0x8000)
	{
		m_banking_mode = ((value & 0x01) != 0);
	}
	else if(address >= 0xA000 && address < 0xC000)
	{
		if(m_ram_enable && m_ram.size() > 0)
		{
			u8 ram_bank = m_rom_ram_bank;
			if(ram_bank <= m_ram.size())
			{
				m_ram[ram_bank].write(address - 0xA000, value);
			}
		}
	}
}
