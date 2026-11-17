#ifndef QTREGB_CART_MBC3_H
#define QTREGB_CART_MBC3_H

#include <cstdint>
#include <functional>
#include <vector>

#include "types.h"
#include "internal_cart.h"
#include "memory.h"

namespace qtreGB
{

class CartMBC3 : public InternalCart
{
	public:
		explicit CartMBC3(std::function<int64_t()> now);
		~CartMBC3();

		bool load_rom(std::vector<u8>& rom) override;

		std::vector<Memory>& get_ram() override;
		void set_ram_banks(u8 banks) override;
		void append_battery_data(std::vector<u8>& data) override;
		void load_battery_data(const std::vector<u8>& data) override;
		void set_rtc_clock(std::function<int64_t()> now) override;

		u8 read(u16 address) override;
		void write(u16 address, u8 value) override;

	private:
		struct RtcValue
		{
			u8 seconds = 0;		// 0..63 (normally 0..59)
			u8 minutes = 0;		// 0..63 (normally 0..59)
			u8 hours = 0;		// 0..31 (normally 0..23)
			u16 days = 0;		// 0..511
			bool halt = false;
			bool carry = false;
		};

		std::vector<u8> m_rom;
		std::vector<Memory> m_ram;
		bool m_ram_enable = false;
		bool m_has_rtc = false;
		bool m_mbc30 = false;
		u8 m_rom_bank = 1;
		u8 m_selected = 0;	// 0x00-0x03 RAM bank, 0x08-0x0C RTC
		bool m_latch_armed = false;
		RtcValue m_rtc;
		RtcValue m_rtc_latched;
		int64_t m_last_time = 0;	// microseconds
		int64_t m_subsecond = 0;	// 0..999999
		std::function<int64_t()> m_now;

		size_t rom_bank_count() const;
		u8 control_value(const RtcValue& value) const;
		bool valid_time(const RtcValue& value) const;
		void update_rtc();
		void add_seconds(u64 seconds);
		void increment_second();
		void latch_rtc();
		u8 read_rtc_register() const;
		void write_rtc_register(u8 value);
};


} // namespace qtreGB
#endif
