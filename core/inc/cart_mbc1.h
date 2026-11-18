#ifndef QTREGB_CART_MBC1_H
#define QTREGB_CART_MBC1_H
#include <cstddef>
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
		const std::vector<u8>& rom() const override { return m_rom; }

		std::vector<Memory>& get_ram() override;
		void set_ram_banks(u8 banks) override;
		CartMapper mapper() const override { return CartMapper::Mbc1; }

		void save_state(StateWriter& writer) const override;
		bool load_state(StateReader& reader) override;

		u8 read(u16 address) override;
		void write(u16 address, u8 value) override;

	private:
		std::vector<u8> m_rom;
		std::vector<Memory> m_ram;
		bool m_banking_mode;	// 0 simple, 1 advanced
		bool m_ram_enable;
		bool m_mbc1m;
		u8 m_rom_ram_bank;
		u8 m_rom_bank;

		size_t rom_bank_count() const;
		size_t selected_rom_bank() const;
		size_t bank0_rom_bank() const;
		size_t selected_ram_bank() const;
		bool detect_mbc1m() const;
};


} // namespace qtreGB
#endif
