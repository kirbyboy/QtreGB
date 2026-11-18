#ifndef QTREGB_CART_MBC2_H
#define QTREGB_CART_MBC2_H

#include <vector>

#include "types.h"
#include "internal_cart.h"
#include "memory.h"

namespace qtreGB
{

class CartMBC2 : public InternalCart
{
	public:
		CartMBC2();
		~CartMBC2();

		bool load_rom(std::vector<u8>& rom) override;
		const std::vector<u8>& rom() const override { return m_rom; }

		std::vector<Memory>& get_ram() override;
		void set_ram_banks(u8 banks) override;
		void load_battery_data(const std::vector<u8>& data) override;
		CartMapper mapper() const override { return CartMapper::Mbc2; }

		void save_state(StateWriter& writer) const override;
		bool load_state(StateReader& reader) override;

		u8 read(u16 address) override;
		void write(u16 address, u8 value) override;

	private:
		std::vector<u8> m_rom;
		// One 512-byte bank. Each byte holds 0xF0 | low-nibble, like VBA-M.
		std::vector<Memory> m_ram;
		bool m_ram_enable;
		u8 m_rom_bank;
};


} // namespace qtreGB
#endif
