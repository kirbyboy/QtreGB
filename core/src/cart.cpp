#include "types.h"
#include "cart.h"
#include "cart_no_mbc.h"
#include "cart_mbc1.h"

#include <algorithm>
#include <fstream>
#include <iterator>

using namespace qtreGB;

Cart::Cart() : m_internal(nullptr)
{
}

Cart::~Cart()
{
	if(m_internal != nullptr)
	{
		delete m_internal;
	}
}

void Cart::connect_error_reporter(ErrorReporter* reporter)
{
	m_errors = reporter;
}

void Cart::report(ErrorCode code)
{
	if(m_errors != nullptr)
	{
		m_errors->report(code);
	}
}

u8 Cart::load_cart(std::vector<u8> rom)
{
	/*
		return codes
		0 - load OK
		1 - small rom
		2 - rom cart size not valid
		3 - rom size doesn't match with internal value
		4 - cartridge type not valid

	*/

	if(rom.size() < 0x150) // Minimum size to get rom data
	{
		report(ErrorCode::CART_ROM_TOO_SMALL);
		return 1;
	}

	if(rom[0x0148] > 0x8) // Rom cart size not valid
	{
		report(ErrorCode::CART_ROM_SIZE_INVALID);
		return 2;
	}

	if(rom.size() != (0x8000ul << rom[0x0148]))
	{
		report(ErrorCode::CART_ROM_SIZE_MISMATCH);
		return 3;
	}

	InternalCart* internal = nullptr;
	bool has_battery = false;

	// Cartridge type
	switch(rom[0x0147])
	{
		case 0:
		case 8:
		case 9:
			internal = new NoMBCCart();
			has_battery = rom[0x0147] == 9;
			break;
		case 1:
		case 2:
		case 3:
			internal = new CartMBC1();
			has_battery = rom[0x0147] == 3;
			break;
		default:
			report(ErrorCode::CART_TYPE_UNSUPPORTED);
			return 4;
	}

	internal->load_rom(rom);

	// RAM
	switch(rom[0x0149])
	{
		case 0: internal->set_ram_banks(0); break;
		case 1: internal->set_ram_banks(1); break;	// 2KB, one bank here
		case 2: internal->set_ram_banks(1); break;
		case 3: internal->set_ram_banks(4); break;
		case 4: internal->set_ram_banks(16); break;
		case 5: internal->set_ram_banks(8); break;
		default: break;
	}

	if(m_internal != nullptr)
	{
		delete m_internal;
	}
	m_internal = internal;
	m_has_battery = has_battery;

	return 0;
}

u8 Cart::read(u16 address)
{
	if(m_internal == nullptr) return 0xFF; // open bus: no cartridge loaded
	return m_internal->read(address);
}

void Cart::write(u16 address, u8 value)
{
	if(m_internal == nullptr) return;
	m_internal->write(address, value);
}

std::string Cart::battery_save_path(const std::string& rom_path)
{
	const size_t slash = rom_path.find_last_of("/\\");
	const size_t dot = rom_path.find_last_of('.');
	if(dot != std::string::npos && (slash == std::string::npos || dot > slash))
	{
		return rom_path.substr(0, dot) + ".sav";
	}
	return rom_path + ".sav";
}

std::vector<u8> Cart::read_battery_ram()
{
	std::vector<u8> data;
	if(m_internal == nullptr)
	{
		return data;
	}

	for(const Memory& bank : m_internal->get_ram())
	{
		data.insert(data.end(), bank.data().begin(), bank.data().end());
	}
	return data;
}

bool Cart::write_battery_ram(const std::vector<u8>& data)
{
	if(m_internal == nullptr)
	{
		return false;
	}

	size_t offset = 0;
	for(Memory& bank : m_internal->get_ram())
	{
		if(offset >= data.size())
		{
			break;
		}
		const size_t count = std::min(bank.size(), data.size() - offset);
		std::copy_n(data.begin() + static_cast<std::ptrdiff_t>(offset),
			static_cast<std::ptrdiff_t>(count), bank.data().begin());
		offset += count;
	}
	return true;
}

bool Cart::save_battery_ram(const std::string& path)
{
	if(!m_has_battery || m_internal == nullptr)
	{
		return false;
	}

	const std::vector<u8> data = read_battery_ram();
	if(data.empty())
	{
		return false;
	}

	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	if(!out)
	{
		return false;
	}
	out.write(reinterpret_cast<const char*>(data.data()),
		static_cast<std::streamsize>(data.size()));
	return out.good();
}

bool Cart::load_battery_ram(const std::string& path)
{
	if(!m_has_battery || m_internal == nullptr)
	{
		return false;
	}

	std::ifstream in(path, std::ios::binary);
	if(!in)
	{
		return false;
	}

	const std::vector<u8> data((std::istreambuf_iterator<char>(in)),
		std::istreambuf_iterator<char>());
	if(data.empty())
	{
		return false;
	}
	return write_battery_ram(data);
}
