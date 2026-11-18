#ifndef QTREGB_INTERNAL_CART_H
#define QTREGB_INTERNAL_CART_H

#include <cstdint>
#include <functional>
#include <vector>

#include "types.h"

namespace qtreGB
{

class Memory;
class StateReader;
class StateWriter;

enum class CartMapper : u8
{
	None = 0,
	NoMbc = 1,
	Mbc1 = 2,
	Mbc2 = 3,
	Mbc3 = 4,
	Mbc5 = 5,
};

class InternalCart
{
	public:
		InternalCart();
		virtual ~InternalCart();

		virtual bool load_rom(std::vector<u8>& rom) = 0;
		virtual const std::vector<u8>& rom() const = 0;
		virtual std::vector<Memory>& get_ram() = 0;
		virtual void set_ram_banks(u8 banks) = 0;
		virtual CartMapper mapper() const = 0;

		virtual void save_state(StateWriter& writer) const = 0;
		virtual bool load_state(StateReader& reader) = 0;

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

// Savestate helpers shared by the mapper implementations. The RAM payload is
// the concatenation of all banks in bank order.
std::vector<u8> cart_ram_bytes(const std::vector<Memory>& ram);
bool cart_ram_load(const std::vector<u8>& data, std::vector<Memory>& ram);


} // namespace qtreGB
#endif
