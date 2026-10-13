#include "types.h"
#include "cart_no_mbc.h"

using namespace qtreGB;

using namespace std;

NoMBCCart::NoMBCCart()
{
}

NoMBCCart::~NoMBCCart()
{
}

bool NoMBCCart::load_rom(std::vector<u8>& rom)
{
	m_rom = rom;
	return true;
}

std::vector<Memory> &NoMBCCart::get_ram()
{
	return m_ram;
}

void NoMBCCart::set_ram_banks(u8 banks)
{
	if(banks > 1)	// Only 1 bank is allowed for no mbc carts
	{
		m_ram = vector<Memory>(banks, Memory(0x2000));
	}
}

u8 NoMBCCart::read(u16 address)
{
	if(address < 0x8000)
	{
		return m_rom[address];
	}
	else if(address >= 0xA000 && address < 0xE000 && m_ram.size() > 0)
	{
		return m_ram[0].read(address - 0xA000);
	}

	return 0xFF;	// It never should reach here...
}

void NoMBCCart::write(u16 address, u8 value)
{
	if(address >= 0xA000 && address < 0xC000 && m_ram.size() > 0)
	{
		m_ram[0].write(address - 0xA000, value);
	}
}
