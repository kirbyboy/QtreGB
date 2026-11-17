#ifndef QTREGB_INTERNAL_CART_H
#define QTREGB_INTERNAL_CART_H

#include <cstdint>
#include <functional>
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

		// Appends the battery-backed data (RAM and, for MBC3, RTC state).
		virtual void append_battery_data(std::vector<u8>& data);

		// Loads raw battery RAM. Cartridges with special save formats (MBC2,
		// MBC3+RTC) override this; the default copies the bytes into RAM banks.
		virtual void load_battery_data(const std::vector<u8>& data);

		// RTC clock injection for deterministic tests; real carts use host time.
		virtual void set_rtc_clock(std::function<int64_t()> now) { (void)now; }

		virtual u8 read(u16 address) = 0;
		virtual void write(u16 address, u8 value) = 0;
};


} // namespace qtreGB
#endif
