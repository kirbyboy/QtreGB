#include "common.h"
#include "cart.h"
#include "cart_no_mbc.h"
#include "cart_mbc1.h"

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

u8 Cart::load_cart(std::vector<u8> rom)
{
	u8 return_code = 0;
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
		return_code = 1;
	}

	if(rom[0x0148] > 0x8) // Rom cart size not valid
	{
		return_code = 2;
	}

	if(rom.size() != (0x2000ul * (1 << rom[0x0148])))
	{
		//return_code = 3;
	}

	// Cartridge type
	switch(rom[0x0147])
	{
		case 0:
			m_internal = new NoMBCCart();
			m_internal->load_rom(rom);
			break;
		case 1:
		case 2:
		case 3:
			m_internal = new CartMBC1();
			m_internal->load_rom(rom);
			break;
		default:
			return_code = 4;
	}

	// RAM
	switch(rom[0x0149])
	{
		case 0: m_internal->set_ram_banks(0); break;
		case 1: break;
		case 2: m_internal->set_ram_banks(1); break;
		case 3: m_internal->set_ram_banks(4); break;
		case 4: m_internal->set_ram_banks(16); break;
		case 5: m_internal->set_ram_banks(8); break;
		default: break;
	}

	return return_code;
}

u8 Cart::read(u16 address)
{
	return m_internal->read(address);
}

void Cart::write(u16 address, u8 value)
{
	m_internal->write(address, value);
}
