#ifndef QTREGB_INTERNAL_CART_H
#define QTREGB_INTERNAL_CART_H

#include <vector>

#include "types.h"

namespace qtreGB
{

class Memory;

class InternalCart
{
	public:
		InternalCart();
		virtual ~InternalCart();

		virtual bool load_rom(std::vector<u8>& rom) = 0;
		virtual std::vector<Memory>& get_ram() = 0;
		virtual void set_ram_banks(u8 banks) = 0;

		virtual u8 read(u16 address) = 0;
		virtual void write(u16 address, u8 value) = 0;
};


} // namespace qtreGB
#endif
