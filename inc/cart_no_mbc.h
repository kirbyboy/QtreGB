#ifndef QTREGB_CART_NO_MBC_H
#define QTREGB_CART_NO_MBC_H

#include <vector>

#include "common.h"
#include "internal_cart.h"
#include "memory.h"

class NoMBCCart : public InternalCart
{
	public:
		NoMBCCart();
		~NoMBCCart();

		bool load_rom(std::vector<u8>& rom) override;

		std::vector<Memory>& get_ram() override;
		void set_ram_banks(u8 banks) override;

		u8 read(u16 address) override;
		void write(u16 address, u8 value) override;

	private:
		std::vector<u8> m_rom;
		std::vector<Memory> m_ram;
};

#endif
