#ifndef QTREGB_CART_NO_MBC_H
#define QTREGB_CART_NO_MBC_H

#include <vector>

#include "types.h"
#include "internal_cart.h"
#include "memory.h"

namespace qtreGB
{

class NoMBCCart : public InternalCart
{
	public:
		NoMBCCart();
		~NoMBCCart();

		bool load_rom(std::vector<u8>& rom) override;
		const std::vector<u8>& rom() const override { return m_rom; }

		std::vector<Memory>& get_ram() override;
		void set_ram_banks(u8 banks) override;
		CartMapper mapper() const override { return CartMapper::NoMbc; }

		void save_state(StateWriter& writer) const override;
		bool load_state(StateReader& reader) override;

		u8 read(u16 address) override;
		void write(u16 address, u8 value) override;

	private:
		std::vector<u8> m_rom;
		std::vector<Memory> m_ram;
};


} // namespace qtreGB
#endif
