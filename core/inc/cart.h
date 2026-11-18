#ifndef QTREGB_CART_H
#define QTREGB_CART_H

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "types.h"
#include "memory.h"
#include "internal_cart.h"
#include "error_reporter.h"

namespace qtreGB
{

class StateReader;
class StateWriter;

class Cart
{
	public:
		Cart();
		~Cart();

		u8 load_cart(std::vector<u8> rom);
		void connect_error_reporter(ErrorReporter* reporter);

		u8 read(u16 address);
		void write(u16 address, u8 value);

		bool has_cartridge() const { return m_internal != nullptr; }
		// Loaded ROM image. Empty when no cartridge is loaded.
		const std::vector<u8>& rom() const;

		bool has_battery() const { return m_has_battery; }

		// Path next to the ROM with the .sav extension.
		static std::string battery_save_path(const std::string& rom_path);

		// Raw contents of all cartridge RAM banks, concatenated in bank order.
		std::vector<u8> read_battery_ram();
		bool write_battery_ram(const std::vector<u8>& data);

		// Full battery payload: RAM plus RTC state for MBC3.
		std::vector<u8> read_battery_data();

		// Battery-backed RAM persistence (.sav next to the ROM).
		bool save_battery_ram(const std::string& path);
		bool load_battery_ram(const std::string& path);

		// Savestate: mapper registers, cartridge RAM and MBC3 RTC.
		void save_state(StateWriter& writer) const;
		bool load_state(StateReader& reader);

		// Overrides the host clock used by MBC3 RTC (tests). Returns
		// microseconds since the Unix epoch.
		void set_rtc_clock(std::function<int64_t()> now);

	private:
		void report(ErrorCode code);

		InternalCart* m_internal;
		ErrorReporter* m_errors = nullptr;
		bool m_has_battery = false;
		std::function<int64_t()> m_rtc_now;

};


} // namespace qtreGB
#endif
