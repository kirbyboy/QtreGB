#include "types.h"
#include "cart_mbc2.h"
#include "state_stream.h"

#include <algorithm>

using namespace qtreGB;

using namespace std;

namespace
{
constexpr u16 CART_RAM_ENABLE = 0x0001;
constexpr u16 CART_ROM_BANK = 0x0002;
constexpr u16 CART_RAM = 0x0010;
}

CartMBC2::CartMBC2()
{
	m_ram_enable = false;
	m_rom_bank = 1;
}

CartMBC2::~CartMBC2()
{
}

bool CartMBC2::load_rom(std::vector<u8>& rom)
{
	m_rom = rom;
	return true;
}

std::vector<Memory> &CartMBC2::get_ram()
{
	return m_ram;
}

void CartMBC2::set_ram_banks(u8 banks)
{
	if(banks > 0)
	{
		m_ram = vector<Memory>(1, Memory(0x200));
	}
	else
	{
		m_ram.clear();
	}
}

void CartMBC2::load_battery_data(const std::vector<u8>& data)
{
	if(m_ram.empty())
	{
		return;
	}

	vector<u8>& ram = m_ram[0].data();
	if(data.size() == 0x100)
	{
		// mGBA-style packed format: two nibbles per byte, even address in
		// the low nibble and odd address in the high nibble.
		const size_t count = min(ram.size() / 2, data.size());
		for(size_t i = 0; i < count; i++)
		{
			ram[i * 2] = static_cast<u8>(0xF0 | (data[i] & 0x0F));
			ram[i * 2 + 1] = static_cast<u8>(0xF0 | (data[i] >> 4));
		}
		return;
	}

	// 512-byte (VBA/SameBoy) files; larger files only contribute their first
	// 512 bytes (e.g. Gambatte's 8KB MBC2 save).
	const size_t count = min(ram.size(), data.size());
	for(size_t i = 0; i < count; i++)
	{
		ram[i] = static_cast<u8>(0xF0 | (data[i] & 0x0F));
	}
}

u8 CartMBC2::read(u16 address)
{
	if(address < 0x4000)
	{
		return m_rom[address];
	}
	if(address < 0x8000)
	{
		const size_t banks = m_rom.size() / 0x4000;
		if(banks == 0)
		{
			return 0xFF;
		}
		size_t bank = m_rom_bank ? m_rom_bank : 1;
		bank %= banks;
		return m_rom[bank * 0x4000 + address - 0x4000];
	}
	if(address >= 0xA000 && address < 0xC000)
	{
		if(!m_ram_enable || m_ram.empty())
		{
			return 0xFF;
		}
		return m_ram[0].read(address & 0x01FF);
	}

	return 0xFF;
}

void CartMBC2::write(u16 address, u8 value)
{
	if(address < 0x4000)
	{
		if((address & 0x0100) != 0)
		{
			m_rom_bank = static_cast<u8>(value & 0x0F);
			if(m_rom_bank == 0)
			{
				m_rom_bank = 1;
			}
		}
		else
		{
			m_ram_enable = (value & 0x0F) == 0x0A;
		}
	}
	else if(address >= 0xA000 && address < 0xC000)
	{
		if(m_ram_enable && !m_ram.empty())
		{
			m_ram[0].write(address & 0x01FF,
				static_cast<u8>(0xF0 | (value & 0x0F)));
		}
	}
}

void CartMBC2::save_state(StateWriter& writer) const
{
	writer.write_bool(CART_RAM_ENABLE, m_ram_enable);
	writer.write_u8(CART_ROM_BANK, m_rom_bank);
	writer.write_bytes(CART_RAM, cart_ram_bytes(m_ram));
}

bool CartMBC2::load_state(StateReader& reader)
{
	bool ram_enable = false;
	u8 rom_bank = 0;
	std::vector<u8> ram;

	if(!reader.read_bool(CART_RAM_ENABLE, ram_enable) ||
	   !reader.read_u8(CART_ROM_BANK, rom_bank) ||
	   !reader.read_bytes(CART_RAM, ram))
	{
		return false;
	}

	if(rom_bank > 0x0F)
	{
		reader.mark_error(StateError::BadValue);
		return false;
	}
	if(!cart_ram_load(ram, m_ram))
	{
		reader.mark_error(StateError::BadLength);
		return false;
	}

	m_ram_enable = ram_enable;
	m_rom_bank = rom_bank;
	return true;
}
