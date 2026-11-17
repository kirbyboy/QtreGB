#include "types.h"
#include "cart_mbc1.h"

#include <algorithm>

using namespace qtreGB;

using namespace std;

namespace
{
constexpr size_t NINTENDO_LOGO_OFFSET = 0x104;
constexpr size_t NINTENDO_LOGO_SIZE = 0x30;
constexpr size_t ROM_BANK_SIZE = 0x4000;
constexpr size_t MBC1M_DETECTION_BANK = 0x10;
}

CartMBC1::CartMBC1()
{
	m_banking_mode = false;
	m_ram_enable = false;
	m_mbc1m = false;
	m_rom_bank = 1;
	m_rom_ram_bank = 0;
}

CartMBC1::~CartMBC1()
{
}

bool CartMBC1::load_rom(std::vector<u8>& rom)
{
	m_rom = rom;
	m_banking_mode = false;
	m_ram_enable = false;
	m_rom_bank = 1;
	m_rom_ram_bank = 0;
	m_mbc1m = detect_mbc1m();
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
	else
	{
		m_ram.clear();
	}
}

size_t CartMBC1::rom_bank_count() const
{
	return m_rom.size() / ROM_BANK_SIZE;
}

bool CartMBC1::detect_mbc1m() const
{
	// MBC1M multi-game carts duplicate a Nintendo header in ROM bank $10.
	// Only 1 MiB carts are known to use this wiring.
	if(m_rom.size() != 0x100000)
	{
		return false;
	}

	const auto first = m_rom.begin() + NINTENDO_LOGO_OFFSET;
	const auto second = m_rom.begin() + MBC1M_DETECTION_BANK * ROM_BANK_SIZE
		+ NINTENDO_LOGO_OFFSET;
	return equal(first, first + NINTENDO_LOGO_SIZE, second) &&
		any_of(first, first + NINTENDO_LOGO_SIZE,
			[](u8 byte) { return byte != 0; });
}

size_t CartMBC1::selected_rom_bank() const
{
	const size_t banks = rom_bank_count();
	if(banks == 0)
	{
		return 0;
	}

	// The 00 -> 01 translation uses the full 5-bit register, even on carts
	// that only wire part of it.
	u8 low = m_rom_bank & 0x1F;
	if(low == 0)
	{
		low = 1;
	}

	size_t bank;
	if(m_mbc1m)
	{
		// MBC1M: secondary register drives bits 4-5; main bit 4 is ignored.
		bank = (static_cast<size_t>(m_rom_ram_bank) << 4) | (low & 0x0F);
	}
	else if(m_rom.size() > 0x80000)
	{
		// Standard MBC1 with 1 MiB or more: secondary drives bits 5-6.
		bank = (static_cast<size_t>(m_rom_ram_bank) << 5) | low;
	}
	else
	{
		bank = low;
	}
	return bank & (banks - 1);
}

size_t CartMBC1::bank0_rom_bank() const
{
	const size_t banks = rom_bank_count();
	if(banks == 0 || !m_banking_mode)
	{
		return 0;
	}

	size_t bank = 0;
	if(m_mbc1m)
	{
		bank = static_cast<size_t>(m_rom_ram_bank) << 4;
	}
	else if(m_rom.size() > 0x80000)
	{
		bank = static_cast<size_t>(m_rom_ram_bank) << 5;
	}
	return bank & (banks - 1);
}

size_t CartMBC1::selected_ram_bank() const
{
	if(m_ram.empty() || m_mbc1m || m_rom.size() > 0x80000)
	{
		// MBC1M and 1 MiB+ standard MBC1 use the 2-bit register for ROM
		// banking, so cartridge RAM is fixed to bank 0 (8 KiB).
		return 0;
	}
	return m_rom_ram_bank % m_ram.size();
}

u8 CartMBC1::read(u16 address)
{
	if(address < 0x4000)
	{
		return m_rom[bank0_rom_bank() * ROM_BANK_SIZE + address];
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

	return 0xFF;	// It should never reach here...
}

void CartMBC1::write(u16 address, u8 value)
{
	if(address < 0x2000)
	{
		if((value & 0x0F) == 0x0A && m_ram.size() > 0)
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
		// Keep the full 5-bit register for the 00 -> 01 translation; the
		// selected bank is masked to the actual ROM size on reads.
		m_rom_bank = static_cast<u8>(value & 0x1F);
	}
	else if(address < 0x6000)
	{
		// RAM bank (32 KiB carts) or upper ROM bits (1 MiB+ / MBC1M).
		m_rom_ram_bank = static_cast<u8>(value & 0x03);
	}
	else if(address < 0x8000)
	{
		m_banking_mode = ((value & 0x01) != 0);
	}
	else if(address >= 0xA000 && address < 0xC000)
	{
		if(m_ram_enable && !m_ram.empty())
		{
			m_ram[selected_ram_bank()].write(address - 0xA000, value);
		}
	}
}
