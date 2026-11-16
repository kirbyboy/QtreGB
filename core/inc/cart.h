#ifndef QTREGB_CART_H
#define QTREGB_CART_H

#include <string>
#include <vector>

#include "types.h"
#include "memory.h"
#include "internal_cart.h"
#include "error_reporter.h"

namespace qtreGB
{

class Cart
{
	public:
		Cart();
		~Cart();

		u8 load_cart(std::vector<u8> rom);
		void connect_error_reporter(ErrorReporter* reporter);

		u8 read(u16 address);
		void write(u16 address, u8 value);

		bool has_battery() const { return m_has_battery; }

		// Path next to the ROM with the .sav extension.
		static std::string battery_save_path(const std::string& rom_path);

		// Raw contents of all cartridge RAM banks, concatenated in bank order.
		std::vector<u8> read_battery_ram();
		bool write_battery_ram(const std::vector<u8>& data);

		// Battery-backed RAM persistence (.sav next to the ROM).
		bool save_battery_ram(const std::string& path);
		bool load_battery_ram(const std::string& path);

	private:
		void report(ErrorCode code);

		InternalCart* m_internal;
		ErrorReporter* m_errors = nullptr;
		bool m_has_battery = false;

};


} // namespace qtreGB
#endif
