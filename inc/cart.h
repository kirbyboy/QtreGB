#ifndef QTREGB_CART_H
#define QTREGB_CART_H

#include <vector>

#include "common.h"
#include "memory.h"
#include "internal_cart.h"

class Cart
{
	public:
		Cart();
		~Cart();

		u8 load_cart(std::vector<u8> rom);

		u8 read(u16 address);
		void write(u16 address, u8 value);
	private:
		InternalCart* m_internal;
		std::vector<Memory> m_ram;

};

#endif
