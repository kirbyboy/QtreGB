#ifndef QTREGB_CART_MBC1_H
#define QTREGB_CART_MBC1_H
#include <vector>

#include "types.h"
#include "internal_cart.h"
#include "memory.h"

namespace qtreGB
{

class CartMBC1 : public InternalCart
{
	public:
		CartMBC1();
		~CartMBC1();

		bool load_rom(std::vector<u8>& rom) override;

		std::vector<Memory>& get_ram() override;
		void set_ram_banks(u8 banks) override;

		u8 read(u16 address) override;
		void write(u16 address, u8 value) override;

	private:
		std::vector<u8> m_rom;
		std::vector<Memory> m_ram;
		bool m_banking_mode;	// 0 simple, 1 advanced
		bool m_ram_enable;
		u8 m_rom_ram_bank;
		u8 m_rom_bank;
};


} // namespace qtreGB
#endif
