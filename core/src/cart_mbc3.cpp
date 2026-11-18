#include "types.h"
#include "cart_mbc3.h"
#include "state_stream.h"

#include <algorithm>
#include <utility>

using namespace qtreGB;

using namespace std;

namespace
{
constexpr size_t ROM_BANK_SIZE = 0x4000;
constexpr size_t RTC_SAVE_SIZE32 = 44;
constexpr size_t RTC_SAVE_SIZE64 = 48;

constexpr u16 CART_RAM_ENABLE = 0x0001;
constexpr u16 CART_ROM_BANK = 0x0002;
constexpr u16 CART_SELECTED = 0x0003;
constexpr u16 CART_LATCH_ARMED = 0x0004;
constexpr u16 CART_MBC30 = 0x0005;
constexpr u16 CART_HAS_RTC = 0x0006;

constexpr u16 CART_RTC_SECONDS = 0x0010;
constexpr u16 CART_RTC_MINUTES = 0x0011;
constexpr u16 CART_RTC_HOURS = 0x0012;
constexpr u16 CART_RTC_DAYS = 0x0013;
constexpr u16 CART_RTC_HALT = 0x0014;
constexpr u16 CART_RTC_CARRY = 0x0015;

constexpr u16 CART_LATCHED_SECONDS = 0x0020;
constexpr u16 CART_LATCHED_MINUTES = 0x0021;
constexpr u16 CART_LATCHED_HOURS = 0x0022;
constexpr u16 CART_LATCHED_DAYS = 0x0023;
constexpr u16 CART_LATCHED_HALT = 0x0024;
constexpr u16 CART_LATCHED_CARRY = 0x0025;

constexpr u16 CART_SUBSECOND = 0x0030;
constexpr u16 CART_RAM = 0x0040;

u32 read_u32(const std::vector<u8>& data, size_t offset)
{
	return static_cast<u32>(data[offset]) |
		(static_cast<u32>(data[offset + 1]) << 8) |
		(static_cast<u32>(data[offset + 2]) << 16) |
		(static_cast<u32>(data[offset + 3]) << 24);
}

u64 read_u64(const std::vector<u8>& data, size_t offset)
{
	u64 value = 0;
	for(int i = 7; i >= 0; i--)
	{
		value = (value << 8) | data[offset + static_cast<size_t>(i)];
	}
	return value;
}
}

CartMBC3::CartMBC3(std::function<int64_t()> now) : m_now(std::move(now))
{
}

CartMBC3::~CartMBC3()
{
}

bool CartMBC3::load_rom(std::vector<u8>& rom)
{
	m_rom = rom;
	m_ram_enable = false;
	m_has_rtc = m_rom.size() > 0x147 &&
		(m_rom[0x0147] == 0x0F || m_rom[0x0147] == 0x10);
	m_mbc30 = m_rom.size() > 0x149 &&
		(m_rom[0x0147] == 0x10 || m_rom[0x0147] == 0x12 || m_rom[0x0147] == 0x13) &&
		m_rom[0x0149] == 0x05;
	m_rom_bank = 1;
	m_selected = 0;
	m_latch_armed = false;
	m_rtc = RtcValue{};
	m_rtc_latched = RtcValue{};
	m_subsecond = 0;
	m_last_time = m_now ? m_now() : 0;
	return true;
}

std::vector<Memory> &CartMBC3::get_ram()
{
	return m_ram;
}

void CartMBC3::set_ram_banks(u8 banks)
{
	if(banks > 0)
	{
		m_ram = vector<Memory>(banks, Memory(0x2000));
	}
	else
	{
		m_ram.clear();
	}
}

void CartMBC3::set_rtc_clock(std::function<int64_t()> now)
{
	m_now = std::move(now);
	m_last_time = m_now ? m_now() : 0;
}
size_t CartMBC3::rom_bank_count() const
{
	return m_rom.size() / ROM_BANK_SIZE;
}

u8 CartMBC3::control_value(const RtcValue& value) const
{
	return static_cast<u8>(((value.days >> 8) & 0x01) |
		(value.halt ? 0x40 : 0x00) |
		(value.carry ? 0x80 : 0x00));
}

bool CartMBC3::valid_time(const RtcValue& value) const
{
	return value.seconds < 60 && value.minutes < 60 && value.hours < 24;
}

void CartMBC3::increment_second()
{
	m_rtc.seconds = static_cast<u8>((m_rtc.seconds + 1) & 0x3F);
	if(m_rtc.seconds != 60)
	{
		return;
	}

	m_rtc.seconds = 0;
	m_rtc.minutes = static_cast<u8>((m_rtc.minutes + 1) & 0x3F);
	if(m_rtc.minutes != 60)
	{
		return;
	}

	m_rtc.minutes = 0;
	m_rtc.hours = static_cast<u8>((m_rtc.hours + 1) & 0x1F);
	if(m_rtc.hours != 24)
	{
		return;
	}

	m_rtc.hours = 0;
	m_rtc.days = static_cast<u16>((m_rtc.days + 1) & 0x1FF);
	if(m_rtc.days == 0)
	{
		m_rtc.carry = true;
	}
}

void CartMBC3::add_seconds(u64 elapsed)
{
	if(m_rtc.halt || elapsed == 0)
	{
		return;
	}

	if(valid_time(m_rtc))
	{
		u64 total = static_cast<u64>(m_rtc.days) * 86400 +
			static_cast<u64>(m_rtc.hours) * 3600 +
			static_cast<u64>(m_rtc.minutes) * 60 +
			m_rtc.seconds + elapsed;
		const u64 new_days = total / 86400;
		if(new_days > 511)
		{
			m_rtc.carry = true;
		}
		m_rtc.days = static_cast<u16>(new_days % 512);

		u32 rest = static_cast<u32>(total % 86400);
		m_rtc.hours = static_cast<u8>(rest / 3600);
		rest %= 3600;
		m_rtc.minutes = static_cast<u8>(rest / 60);
		m_rtc.seconds = static_cast<u8>(rest % 60);
		return;
	}

	for(u64 i = 0; i < elapsed; i++)
	{
		increment_second();
	}
}

void CartMBC3::update_rtc()
{
	const int64_t now = m_now ? m_now() : m_last_time;
	if(now > m_last_time && !m_rtc.halt)
	{
		const int64_t elapsed = now - m_last_time;
		const int64_t total = m_subsecond + elapsed;
		m_subsecond = total % 1000000;
		add_seconds(static_cast<u64>(total / 1000000));
	}
	m_last_time = now;
}

void CartMBC3::latch_rtc()
{
	update_rtc();
	m_rtc_latched = m_rtc;
}

u8 CartMBC3::read_rtc_register() const
{
	switch(m_selected)
	{
		case 0x08: return static_cast<u8>(m_rtc_latched.seconds & 0x3F);
		case 0x09: return static_cast<u8>(m_rtc_latched.minutes & 0x3F);
		case 0x0A: return static_cast<u8>(m_rtc_latched.hours & 0x1F);
		case 0x0B: return static_cast<u8>(m_rtc_latched.days & 0xFF);
		case 0x0C: return control_value(m_rtc_latched);
		default: return 0xFF;
	}
}

void CartMBC3::write_rtc_register(u8 value)
{
	switch(m_selected)
	{
		case 0x08:
			m_rtc.seconds = static_cast<u8>(value & 0x3F);
			m_subsecond = 0;
			break;
		case 0x09:
			m_rtc.minutes = static_cast<u8>(value & 0x3F);
			break;
		case 0x0A:
			m_rtc.hours = static_cast<u8>(value & 0x1F);
			break;
		case 0x0B:
			m_rtc.days = static_cast<u16>((m_rtc.days & 0x100) | value);
			break;
		case 0x0C:
		{
			const bool new_halt = (value & 0x40) != 0;
			if(m_rtc.halt && !new_halt)
			{
				m_last_time = m_now ? m_now() : m_last_time;
			}
			m_rtc.halt = new_halt;
			m_rtc.days = static_cast<u16>((m_rtc.days & 0xFF) |
				((value & 0x01) << 8));
			m_rtc.carry = (value & 0x80) != 0;
			break;
		}
		default:
			break;
	}
}

u8 CartMBC3::read(u16 address)
{
	if(address < 0x4000)
	{
		return m_rom[address];
	}
	if(address < 0x8000)
	{
		const size_t banks = rom_bank_count();
		if(banks == 0)
		{
			return 0xFF;
		}
		size_t bank = m_rom_bank;
		if(!m_mbc30)
		{
			bank &= 0x7F;
		}
		if(bank == 0)
		{
			bank = 1;
		}
		bank &= banks - 1;
		return m_rom[bank * ROM_BANK_SIZE + address - 0x4000];
	}
	if(address >= 0xA000 && address < 0xC000)
	{
		if(!m_ram_enable)
		{
			return 0xFF;
		}
		if(m_selected <= (m_mbc30 ? 0x07 : 0x03))
		{
			if(m_ram.empty())
			{
				return 0xFF;
			}
			const size_t bank = m_selected % m_ram.size();
			return m_ram[bank].read(address - 0xA000);
		}
		if(m_selected >= 0x08 && m_selected <= 0x0C && m_has_rtc)
		{
			return read_rtc_register();
		}
		return 0xFF;
	}

	return 0xFF;
}

void CartMBC3::write(u16 address, u8 value)
{
	if(address < 0x2000)
	{
		m_ram_enable = (value & 0x0F) == 0x0A;
	}
	else if(address < 0x4000)
	{
		m_rom_bank = static_cast<u8>(value & (m_mbc30 ? 0xFF : 0x7F));
	}
	else if(address < 0x6000)
	{
		if(value <= (m_mbc30 ? 0x07 : 0x03))
		{
			m_selected = value;
		}
		else if(value >= 0x08 && value <= 0x0C)
		{
			m_selected = value;
		}
		else
		{
			m_selected = 0xFF;
		}
	}
	else if(address < 0x8000)
	{
		if(value == 0x00)
		{
			m_latch_armed = true;
		}
		else if(value == 0x01 && m_latch_armed)
		{
			latch_rtc();
			m_latch_armed = false;
		}
		else
		{
			m_latch_armed = false;
		}
	}
	else if(address >= 0xA000 && address < 0xC000)
	{
		if(!m_ram_enable)
		{
			return;
		}
		if(m_selected <= (m_mbc30 ? 0x07 : 0x03))
		{
			if(!m_ram.empty())
			{
				const size_t bank = m_selected % m_ram.size();
				m_ram[bank].write(address - 0xA000, value);
			}
		}
		else if(m_selected >= 0x08 && m_selected <= 0x0C && m_has_rtc)
		{
			update_rtc();
			write_rtc_register(value);
		}
	}
}

void CartMBC3::save_state(StateWriter& writer) const
{
	writer.write_bool(CART_RAM_ENABLE, m_ram_enable);
	writer.write_u8(CART_ROM_BANK, m_rom_bank);
	writer.write_u8(CART_SELECTED, m_selected);
	writer.write_bool(CART_LATCH_ARMED, m_latch_armed);
	writer.write_bool(CART_MBC30, m_mbc30);
	writer.write_bool(CART_HAS_RTC, m_has_rtc);

	writer.write_u8(CART_RTC_SECONDS, m_rtc.seconds);
	writer.write_u8(CART_RTC_MINUTES, m_rtc.minutes);
	writer.write_u8(CART_RTC_HOURS, m_rtc.hours);
	writer.write_u16(CART_RTC_DAYS, m_rtc.days);
	writer.write_bool(CART_RTC_HALT, m_rtc.halt);
	writer.write_bool(CART_RTC_CARRY, m_rtc.carry);

	writer.write_u8(CART_LATCHED_SECONDS, m_rtc_latched.seconds);
	writer.write_u8(CART_LATCHED_MINUTES, m_rtc_latched.minutes);
	writer.write_u8(CART_LATCHED_HOURS, m_rtc_latched.hours);
	writer.write_u16(CART_LATCHED_DAYS, m_rtc_latched.days);
	writer.write_bool(CART_LATCHED_HALT, m_rtc_latched.halt);
	writer.write_bool(CART_LATCHED_CARRY, m_rtc_latched.carry);

	writer.write_u32(CART_SUBSECOND, static_cast<u32>(m_subsecond));
	writer.write_bytes(CART_RAM, cart_ram_bytes(m_ram));
}

bool CartMBC3::load_state(StateReader& reader)
{
	bool ram_enable = false;
	bool latch_armed = false;
	bool mbc30 = false;
	bool has_rtc = false;
	u8 rom_bank = 0;
	u8 selected = 0;
	RtcValue rtc {};
	RtcValue latched {};
	u32 subsecond = 0;
	std::vector<u8> ram;

	if(!reader.read_bool(CART_RAM_ENABLE, ram_enable) ||
	   !reader.read_u8(CART_ROM_BANK, rom_bank) ||
	   !reader.read_u8(CART_SELECTED, selected) ||
	   !reader.read_bool(CART_LATCH_ARMED, latch_armed) ||
	   !reader.read_bool(CART_MBC30, mbc30) ||
	   !reader.read_bool(CART_HAS_RTC, has_rtc) ||
	   !reader.read_u8(CART_RTC_SECONDS, rtc.seconds) ||
	   !reader.read_u8(CART_RTC_MINUTES, rtc.minutes) ||
	   !reader.read_u8(CART_RTC_HOURS, rtc.hours) ||
	   !reader.read_u16(CART_RTC_DAYS, rtc.days) ||
	   !reader.read_bool(CART_RTC_HALT, rtc.halt) ||
	   !reader.read_bool(CART_RTC_CARRY, rtc.carry) ||
	   !reader.read_u8(CART_LATCHED_SECONDS, latched.seconds) ||
	   !reader.read_u8(CART_LATCHED_MINUTES, latched.minutes) ||
	   !reader.read_u8(CART_LATCHED_HOURS, latched.hours) ||
	   !reader.read_u16(CART_LATCHED_DAYS, latched.days) ||
	   !reader.read_bool(CART_LATCHED_HALT, latched.halt) ||
	   !reader.read_bool(CART_LATCHED_CARRY, latched.carry) ||
	   !reader.read_u32(CART_SUBSECOND, subsecond) ||
	   !reader.read_bytes(CART_RAM, ram))
	{
		return false;
	}

	const auto valid_rtc = [](const RtcValue& value)
	{
		return value.seconds < 64 && value.minutes < 64 &&
		       value.hours < 32 && value.days < 512;
	};

	const u8 max_ram_bank = mbc30 ? 0x07 : 0x03;
	const bool selected_ok = selected == 0xFF ||
		selected <= max_ram_bank ||
		(selected >= 0x08 && selected <= 0x0C);

	if(mbc30 != m_mbc30 || has_rtc != m_has_rtc ||
	   (!mbc30 && rom_bank > 0x7F) || !selected_ok ||
	   !valid_rtc(rtc) || !valid_rtc(latched) || subsecond > 999999)
	{
		reader.mark_error(StateError::BadValue);
		return false;
	}
	if(!cart_ram_load(ram, m_ram))
	{
		reader.mark_error(StateError::BadLength);
		return false;
	}

	m_ram_enable = ram_enable;
	m_rom_bank = rom_bank;
	m_selected = selected;
	m_latch_armed = latch_armed;
	m_mbc30 = mbc30;
	m_has_rtc = has_rtc;
	m_rtc = rtc;
	m_rtc_latched = latched;
	m_subsecond = static_cast<int64_t>(subsecond);

	// RTC savestate policy: the host epoch is not serialized. Counters are
	// frozen at save time and resume from the load instant, so the time the
	// user spent between saving and loading is not applied.
	m_last_time = m_now ? m_now() : 0;
	return true;
}

void CartMBC3::append_battery_data(std::vector<u8>& data)
{
	InternalCart::append_battery_data(data);

	if(!m_has_rtc)
	{
		return;
	}

	update_rtc();

	auto put_u32 = [&data](u32 value)
	{
		data.push_back(static_cast<u8>(value & 0xFF));
		data.push_back(static_cast<u8>((value >> 8) & 0xFF));
		data.push_back(static_cast<u8>((value >> 16) & 0xFF));
		data.push_back(static_cast<u8>((value >> 24) & 0xFF));
	};
	auto put_u64 = [&data](u64 value)
	{
		for(int i = 0; i < 8; i++)
		{
			data.push_back(static_cast<u8>((value >> (i * 8)) & 0xFF));
		}
	};
	auto put_rtc = [&put_u32](const RtcValue& value, std::function<u8(const RtcValue&)> control)
	{
		put_u32(value.seconds);
		put_u32(value.minutes);
		put_u32(value.hours);
		put_u32(value.days & 0xFF);
		put_u32(control(value));
	};

	put_rtc(m_rtc, [this](const RtcValue& value) { return control_value(value); });
	put_rtc(m_rtc_latched, [this](const RtcValue& value) { return control_value(value); });
	put_u64(static_cast<u64>(m_last_time / 1000000));
}

void CartMBC3::load_battery_data(const std::vector<u8>& data)
{
	size_t ram_size = 0;
	for(const Memory& bank : m_ram)
	{
		ram_size += bank.size();
	}

	size_t offset = 0;
	for(Memory& bank : m_ram)
	{
		if(offset >= data.size())
		{
			break;
		}
		const size_t count = min(bank.size(), data.size() - offset);
		copy_n(data.begin() + static_cast<std::ptrdiff_t>(offset),
			static_cast<std::ptrdiff_t>(count), bank.data().begin());
		offset += count;
	}

	if(!m_has_rtc)
	{
		return;
	}

	const size_t remaining = data.size() > ram_size ? data.size() - ram_size : 0;
	if(remaining < RTC_SAVE_SIZE32)
	{
		// RAM-only save: the RTC starts from zero.
		m_rtc = RtcValue{};
		m_rtc_latched = RtcValue{};
		m_subsecond = 0;
		m_last_time = m_now ? m_now() : 0;
		return;
	}

	auto load_rtc = [&data](size_t base) -> RtcValue
	{
		RtcValue value;
		value.seconds = static_cast<u8>(read_u32(data, base) & 0x3F);
		value.minutes = static_cast<u8>(read_u32(data, base + 4) & 0x3F);
		value.hours = static_cast<u8>(read_u32(data, base + 8) & 0x1F);
		const u32 days_low = read_u32(data, base + 12);
		const u32 control = read_u32(data, base + 16);
		value.days = static_cast<u16>((days_low & 0xFF) | ((control & 0x01) << 8));
		value.halt = (control & 0x40) != 0;
		value.carry = (control & 0x80) != 0;
		return value;
	};

	const size_t rtc_offset = ram_size;
	m_rtc = load_rtc(rtc_offset);
	m_rtc_latched = load_rtc(rtc_offset + 20);
	m_subsecond = 0;
	u64 timestamp;
	if(remaining >= RTC_SAVE_SIZE64)
	{
		timestamp = read_u64(data, rtc_offset + 40);
	}
	else
	{
		timestamp = read_u32(data, rtc_offset + 40);
	}
	m_last_time = static_cast<int64_t>(timestamp) * 1000000;

	const int64_t now = m_now ? m_now() : 0;
	if(m_last_time > now)
	{
		m_last_time = now;
	}
}
