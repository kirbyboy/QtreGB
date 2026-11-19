#include "types.h"
#include "cart.h"
#include "cart_no_mbc.h"
#include "cart_mbc1.h"
#include "cart_mbc2.h"
#include "cart_mbc3.h"
#include "cart_mbc5.h"
#include "state_stream.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iterator>
#include <utility>

using namespace qtreGB;

namespace
{

constexpr u16 CART_MAPPER = 0x0001;
constexpr u16 CART_DATA = 0x0002;

} // namespace

Cart::Cart() : m_internal(nullptr)
{
	m_rtc_now = []
	{
		return std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::system_clock::now().time_since_epoch()).count();
	};
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
		case 5:
		case 6:
			internal = new CartMBC2();
			has_battery = rom[0x0147] == 6;
			break;
		case 0x0F:
		case 0x10:
		case 0x11:
		case 0x12:
		case 0x13:
			internal = new CartMBC3(m_rtc_now);
			has_battery = rom[0x0147] == 0x0F || rom[0x0147] == 0x10 ||
				rom[0x0147] == 0x13;
			break;
		case 0x19:
		case 0x1A:
		case 0x1B:
		case 0x1C:
		case 0x1D:
		case 0x1E:
			// 0x1C-0x1E are the MBC5 rumble variants.
			internal = new CartMBC5(rom[0x0147] >= 0x1C);
			has_battery = rom[0x0147] == 0x1B || rom[0x0147] == 0x1E;
			break;
		default:
			report(ErrorCode::CART_TYPE_UNSUPPORTED);
			return 4;
	}

	internal->load_rom(rom);

	// RAM
	if(rom[0x0147] == 5 || rom[0x0147] == 6)
	{
		// MBC2 has 512 x 4 bits of built-in RAM; the header RAM size is 0.
		internal->set_ram_banks(1);
	}
	else
	{
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

const std::vector<u8>& Cart::rom() const
{
	static const std::vector<u8> empty;
	return m_internal != nullptr ? m_internal->rom() : empty;
}

CartMapper Cart::mapper() const
{
	return m_internal != nullptr ? m_internal->mapper() : CartMapper::None;
}

void Cart::save_state(StateWriter& writer) const
{
	const u8 mapper = m_internal != nullptr ?
		static_cast<u8>(m_internal->mapper()) :
		static_cast<u8>(CartMapper::None);
	writer.write_u8(CART_MAPPER, mapper);

	if(m_internal != nullptr)
	{
		writer.begin_chunk(CART_DATA);
		m_internal->save_state(writer);
		writer.end_chunk();
	}
}

bool Cart::load_state(StateReader& reader)
{
	u8 mapper = 0;
	if(!reader.read_u8(CART_MAPPER, mapper))
	{
		return false;
	}

	if(mapper == static_cast<u8>(CartMapper::None))
	{
		if(m_internal != nullptr)
		{
			reader.mark_error(StateError::BadValue);
			return false;
		}
		return true;
	}

	if(m_internal == nullptr ||
	   mapper != static_cast<u8>(m_internal->mapper()))
	{
		reader.mark_error(StateError::BadValue);
		return false;
	}

	if(!reader.begin_chunk(CART_DATA))
	{
		return false;
	}
	if(!m_internal->load_state(reader))
	{
		return false;
	}
	return reader.end_chunk();
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
	m_internal->load_battery_data(data);
	return true;
}

std::vector<u8> Cart::read_battery_data()
{
	std::vector<u8> data;
	if(m_internal != nullptr)
	{
		m_internal->append_battery_data(data);
	}
	return data;
}

void Cart::set_rtc_clock(std::function<int64_t()> now)
{
	m_rtc_now = std::move(now);
	if(m_internal != nullptr)
	{
		m_internal->set_rtc_clock(m_rtc_now);
	}
}

bool Cart::save_battery_ram(const std::string& path)
{
	if(!m_has_battery || m_internal == nullptr)
	{
		return false;
	}

	const std::vector<u8> data = read_battery_data();
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
