#ifndef QTREGB_CART_MBC5_H
#define QTREGB_CART_MBC5_H

#include <vector>

#include "types.h"
#include "internal_cart.h"
#include "memory.h"

namespace qtreGB
{

class CartMBC5 : public InternalCart
{
	public:
		explicit CartMBC5(bool has_rumble);
		~CartMBC5();

		bool load_rom(std::vector<u8>& rom) override;
		const std::vector<u8>& rom() const override { return m_rom; }

		std::vector<Memory>& get_ram() override;
		void set_ram_banks(u8 banks) override;
		CartMapper mapper() const override { return CartMapper::Mbc5; }

		void save_state(StateWriter& writer) const override;
		bool load_state(StateReader& reader) override;

		u8 read(u16 address) override;
		void write(u16 address, u8 value) override;

	private:
		std::vector<u8> m_rom;
		std::vector<Memory> m_ram;
		bool m_has_rumble;
		bool m_ram_enable;
		u16 m_rom_bank;
		u8 m_ram_bank;

		size_t rom_bank_count() const;
		size_t selected_rom_bank() const;
		size_t selected_ram_bank() const;
};


} // namespace qtreGB
#endif
