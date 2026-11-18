#include "types.h"
#include "cart_no_mbc.h"
#include "state_stream.h"

using namespace qtreGB;

using namespace std;

namespace
{
constexpr u16 CART_RAM = 0x0010;
}

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
	// No-MBC cartridges expose at most one 8KB RAM bank.
	if(banks > 0)
	{
		m_ram = vector<Memory>(1, Memory(0x2000));
	}
	else
	{
		m_ram.clear();
	}
}

u8 NoMBCCart::read(u16 address)
{
	if(address < 0x8000)
	{
		return m_rom[address];
	}
	else if(address >= 0xA000 && address < 0xC000 && m_ram.size() > 0)
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

void NoMBCCart::save_state(StateWriter& writer) const
{
	writer.write_bytes(CART_RAM, cart_ram_bytes(m_ram));
}

bool NoMBCCart::load_state(StateReader& reader)
{
	std::vector<u8> ram;
	if(!reader.read_bytes(CART_RAM, ram))
	{
		return false;
	}
	if(!cart_ram_load(ram, m_ram))
	{
		reader.mark_error(StateError::BadLength);
		return false;
	}
	return true;
}
