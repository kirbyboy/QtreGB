#include "types.h"
#include "cart.h"
#include "cart_no_mbc.h"
#include "cart_mbc1.h"

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

	// Cartridge type
	switch(rom[0x0147])
	{
		case 0:
			internal = new NoMBCCart();
			break;
		case 1:
		case 2:
		case 3:
			internal = new CartMBC1();
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
		case 1: break;
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
