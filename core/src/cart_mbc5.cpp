#include "types.h"
#include "cart_mbc5.h"
#include "state_stream.h"

using namespace qtreGB;

using namespace std;

namespace
{
constexpr size_t ROM_BANK_SIZE = 0x4000;

constexpr u16 CART_RAM_ENABLE = 0x0001;
constexpr u16 CART_ROM_BANK = 0x0002;
constexpr u16 CART_RAM_BANK = 0x0003;
constexpr u16 CART_RAM = 0x0010;
}

CartMBC5::CartMBC5(bool has_rumble) :
m_has_rumble(has_rumble),
m_ram_enable(false),
m_rom_bank(1),
m_ram_bank(0)
{
}

CartMBC5::~CartMBC5()
{
}

bool CartMBC5::load_rom(std::vector<u8>& rom)
{
	m_rom = rom;
	m_ram_enable = false;
	m_rom_bank = 1;
	m_ram_bank = 0;
	return true;
}

std::vector<Memory> &CartMBC5::get_ram()
{
	return m_ram;
}

void CartMBC5::set_ram_banks(u8 banks)
{
	if(banks > 0)
	{
		m_ram = vector<Memory>(banks, Memory(0x2000));
	}
	else
	{
		m_ram.clear();
	}
}

size_t CartMBC5::rom_bank_count() const
{
	return m_rom.size() / ROM_BANK_SIZE;
}

size_t CartMBC5::selected_rom_bank() const
{
	const size_t banks = rom_bank_count();
	if(banks == 0)
	{
		return 0;
	}

	// MBC5 exposes the full 9-bit register and allows bank 0 at $4000.
	return static_cast<size_t>(m_rom_bank) % banks;
}

size_t CartMBC5::selected_ram_bank() const
{
	if(m_ram.empty())
	{
		return 0;
	}

	// On rumble carts, bit 3 of $4000-$5FFF drives the motor and is not part
	// of the RAM bank number. Non-rumble carts use all four bits.
	const u8 bank_mask = m_has_rumble ? 0x07 : 0x0F;
	return static_cast<size_t>(m_ram_bank & bank_mask) % m_ram.size();
}

u8 CartMBC5::read(u16 address)
{
	if(address < 0x4000)
	{
		return m_rom[address];
	}
	if(address < 0x8000)
	{
		return m_rom[selected_rom_bank() * ROM_BANK_SIZE + address - 0x4000];
	}
	if(address >= 0xA000 && address < 0xC000)
	{
		if(m_ram_enable && !m_ram.empty())
		{
			return m_ram[selected_ram_bank()].read(address - 0xA000);
		}
		return 0xFF;
	}

	return 0xFF;
}

void CartMBC5::write(u16 address, u8 value)
{
	if(address < 0x2000)
	{
		m_ram_enable = (value & 0x0F) == 0x0A;
	}
	else if(address < 0x3000)
	{
		m_rom_bank = static_cast<u16>((m_rom_bank & 0x100) | value);
	}
	else if(address < 0x4000)
	{
		// Only bit 8 of the ROM bank number is wired here.
		m_rom_bank = static_cast<u16>((m_rom_bank & 0x0FF) |
			(static_cast<u16>(value & 0x01) << 8));
	}
	else if(address < 0x6000)
	{
		// Bit 3 is the rumble motor on MBC5 rumble carts.
		m_ram_bank = static_cast<u8>(value & 0x0F);
	}
	else if(address < 0x8000)
	{
		// MBC5 has no banking-mode or RTC latch register.
	}
	else if(address >= 0xA000 && address < 0xC000)
	{
		if(m_ram_enable && !m_ram.empty())
		{
			m_ram[selected_ram_bank()].write(address - 0xA000, value);
		}
	}
}

void CartMBC5::save_state(StateWriter& writer) const
{
	writer.write_bool(CART_RAM_ENABLE, m_ram_enable);
	writer.write_u16(CART_ROM_BANK, m_rom_bank);
	writer.write_u8(CART_RAM_BANK, m_ram_bank);
	writer.write_bytes(CART_RAM, cart_ram_bytes(m_ram));
}

bool CartMBC5::load_state(StateReader& reader)
{
	bool ram_enable = false;
	u16 rom_bank = 0;
	u8 ram_bank = 0;
	std::vector<u8> ram;

	if(!reader.read_bool(CART_RAM_ENABLE, ram_enable) ||
	   !reader.read_u16(CART_ROM_BANK, rom_bank) ||
	   !reader.read_u8(CART_RAM_BANK, ram_bank) ||
	   !reader.read_bytes(CART_RAM, ram))
	{
		return false;
	}

	if(rom_bank > 0x1FF || ram_bank > 0x0F)
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
	m_ram_bank = ram_bank;
	return true;
}
