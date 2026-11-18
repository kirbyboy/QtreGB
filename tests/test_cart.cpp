#include "doctest.h"
#include "cart.h"
#include "error_reporter.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <vector>

using namespace qtreGB;

namespace
{
std::vector<u8> make_rom(size_t size, u8 type, u8 rom_size, u8 ram_size = 0)
{
	std::vector<u8> rom(size, 0);
	if(size > 0x149)
	{
		rom[0x0147] = type;
		rom[0x0148] = rom_size;
		rom[0x0149] = ram_size;
	}
	return rom;
}

const std::string SAVE_PATH =
	(std::filesystem::temp_directory_path() / "qtrebg_cart_battery_test.sav").string();
constexpr int64_t RTC_SECOND_US = 1000000;

void set_bank_marker(std::vector<u8>& rom, size_t bank, u8 value)
{
	rom[bank * 0x4000] = value;
}

void make_mbc1m_signature(std::vector<u8>& rom)
{
	// MBC1M carts duplicate the Nintendo header/logo in ROM bank $10.
	for(size_t i = 0; i < 0x30; i++)
	{
		rom[0x104 + i] = static_cast<u8>(0x80 + i);
	}
	std::copy(rom.begin() + 0x104, rom.begin() + 0x134,
		rom.begin() + 0x40104);
}
}

TEST_CASE("Cart: valid 32KB ROM-only loads and returns 0")
{
	Cart cart;
	ErrorReporter errors;
	cart.connect_error_reporter(&errors);

	CHECK(cart.load_cart(make_rom(0x8000, 0x00, 0x00)) == 0);
	CHECK_FALSE(errors.has_error());
}

TEST_CASE("Cart: ROM smaller than the header is rejected as too small")
{
	Cart cart;
	ErrorReporter errors;
	cart.connect_error_reporter(&errors);

	CHECK(cart.load_cart(make_rom(0x100, 0x00, 0x00)) == 1);
	CHECK(errors.code() == ErrorCode::CART_ROM_TOO_SMALL);
}

TEST_CASE("Cart: invalid ROM-size header value is rejected")
{
	Cart cart;
	ErrorReporter errors;
	cart.connect_error_reporter(&errors);

	CHECK(cart.load_cart(make_rom(0x8000, 0x00, 0x09)) == 2);
	CHECK(errors.code() == ErrorCode::CART_ROM_SIZE_INVALID);
}

TEST_CASE("Cart: file size not matching the header is rejected")
{
	Cart cart;
	ErrorReporter errors;
	cart.connect_error_reporter(&errors);

	CHECK(cart.load_cart(make_rom(0x8000, 0x00, 0x01)) == 3);
	CHECK(errors.code() == ErrorCode::CART_ROM_SIZE_MISMATCH);
}

TEST_CASE("Cart: unsupported cartridge type is rejected without crashing")
{
	Cart cart;
	ErrorReporter errors;
	cart.connect_error_reporter(&errors);

	CHECK(cart.load_cart(make_rom(0x8000, 0x99, 0x00)) == 4);
	CHECK(errors.code() == ErrorCode::CART_TYPE_UNSUPPORTED);
}

TEST_CASE("Cart: read with no cartridge loaded returns 0xFF without crashing")
{
	Cart cart;

	CHECK(cart.read(0x0000) == 0xFF);
	CHECK(cart.read(0x4000) == 0xFF);
}

TEST_CASE("Cart: write with no cartridge loaded is a no-op without crashing")
{
	Cart cart;

	CHECK_NOTHROW(cart.write(0x0000, 0x00));
}

TEST_CASE("Cart: MBC1 battery RAM saves and loads all banks")
{
	const std::vector<u8> rom = make_rom(0x8000, 0x03, 0x00, 0x03);
	Cart cart;
	REQUIRE(cart.load_cart(rom) == 0);
	REQUIRE(cart.has_battery());

	cart.write(0x0000, 0x0A);	// enable RAM
	for(u8 bank = 0; bank < 4; bank++)
	{
		cart.write(0x4000, bank);
		cart.write(0xA000, static_cast<u8>(0x10 + bank));
		cart.write(0xBFFF, static_cast<u8>(0x20 + bank));
	}

	CHECK(cart.read_battery_ram().size() == 0x8000);
	CHECK(cart.save_battery_ram(SAVE_PATH));

	Cart loaded;
	REQUIRE(loaded.load_cart(rom) == 0);
	REQUIRE(loaded.has_battery());
	CHECK(loaded.load_battery_ram(SAVE_PATH));

	loaded.write(0x0000, 0x0A);	// enable RAM
	for(u8 bank = 0; bank < 4; bank++)
	{
		loaded.write(0x4000, bank);
		CHECK(loaded.read(0xA000) == static_cast<u8>(0x10 + bank));
		CHECK(loaded.read(0xBFFF) == static_cast<u8>(0x20 + bank));
	}

	std::filesystem::remove(SAVE_PATH);
}

TEST_CASE("Cart: no-MBC RAM+battery saves and loads its RAM")
{
	const std::vector<u8> rom = make_rom(0x8000, 0x09, 0x00, 0x02);
	Cart cart;
	REQUIRE(cart.load_cart(rom) == 0);
	REQUIRE(cart.has_battery());

	cart.write(0xA000, 0x12);
	cart.write(0xBFFF, 0x34);
	CHECK(cart.read(0xA000) == 0x12);
	CHECK(cart.read(0xBFFF) == 0x34);
	CHECK(cart.read_battery_ram().size() == 0x2000);
	CHECK(cart.save_battery_ram(SAVE_PATH));

	Cart loaded;
	REQUIRE(loaded.load_cart(rom) == 0);
	CHECK(loaded.load_battery_ram(SAVE_PATH));
	CHECK(loaded.read(0xA000) == 0x12);
	CHECK(loaded.read(0xBFFF) == 0x34);

	std::filesystem::remove(SAVE_PATH);
}

TEST_CASE("Cart: battery save path replaces the ROM extension")
{
	CHECK(Cart::battery_save_path("game.gb") == "game.sav");
	CHECK(Cart::battery_save_path("/roms/game.gb") == "/roms/game.sav");
	CHECK(Cart::battery_save_path("/roms/game") == "/roms/game.sav");
	CHECK(Cart::battery_save_path("/roms/a.b/game") == "/roms/a.b/game.sav");
}

TEST_CASE("Cart: battery flag follows the cartridge type")
{
	Cart cart;

	CHECK(cart.load_cart(make_rom(0x8000, 0x00, 0x00, 0x00)) == 0);
	CHECK_FALSE(cart.has_battery());

	CHECK(cart.load_cart(make_rom(0x8000, 0x02, 0x00, 0x02)) == 0);
	CHECK_FALSE(cart.has_battery());

	CHECK(cart.load_cart(make_rom(0x8000, 0x08, 0x00, 0x02)) == 0);
	CHECK_FALSE(cart.has_battery());

	CHECK(cart.load_cart(make_rom(0x8000, 0x05, 0x00, 0x00)) == 0);
	CHECK_FALSE(cart.has_battery());

	CHECK(cart.load_cart(make_rom(0x8000, 0x03, 0x00, 0x02)) == 0);
	CHECK(cart.has_battery());

	CHECK(cart.load_cart(make_rom(0x8000, 0x09, 0x00, 0x02)) == 0);
	CHECK(cart.has_battery());

	CHECK(cart.load_cart(make_rom(0x8000, 0x06, 0x00, 0x00)) == 0);
	CHECK(cart.has_battery());

	CHECK(cart.load_cart(make_rom(0x8000, 0x0F, 0x00, 0x00)) == 0);
	CHECK(cart.has_battery());
	CHECK(cart.load_cart(make_rom(0x8000, 0x10, 0x00, 0x02)) == 0);
	CHECK(cart.has_battery());
	CHECK(cart.load_cart(make_rom(0x8000, 0x11, 0x00, 0x00)) == 0);
	CHECK_FALSE(cart.has_battery());
	CHECK(cart.load_cart(make_rom(0x8000, 0x12, 0x00, 0x02)) == 0);
	CHECK_FALSE(cart.has_battery());
	CHECK(cart.load_cart(make_rom(0x8000, 0x13, 0x00, 0x02)) == 0);
	CHECK(cart.has_battery());

	CHECK(cart.load_cart(make_rom(0x8000, 0x19, 0x00, 0x00)) == 0);
	CHECK_FALSE(cart.has_battery());
	CHECK(cart.load_cart(make_rom(0x8000, 0x1A, 0x00, 0x02)) == 0);
	CHECK_FALSE(cart.has_battery());
	CHECK(cart.load_cart(make_rom(0x8000, 0x1B, 0x00, 0x02)) == 0);
	CHECK(cart.has_battery());
	CHECK(cart.load_cart(make_rom(0x8000, 0x1C, 0x00, 0x00)) == 0);
	CHECK_FALSE(cart.has_battery());
	CHECK(cart.load_cart(make_rom(0x8000, 0x1D, 0x00, 0x02)) == 0);
	CHECK_FALSE(cart.has_battery());
	CHECK(cart.load_cart(make_rom(0x8000, 0x1E, 0x00, 0x02)) == 0);
	CHECK(cart.has_battery());
}

TEST_CASE("Cart: MBC1 RAM enable requires lower nibble 0x0A")
{
	const std::vector<u8> rom = make_rom(0x8000, 0x02, 0x00, 0x02);
	Cart cart;
	REQUIRE(cart.load_cart(rom) == 0);

	cart.write(0x0000, 0x02);	// not 0x0A: RAM stays disabled
	cart.write(0xA000, 0x55);
	CHECK(cart.read(0xA000) == 0xFF);

	cart.write(0x0000, 0x0A);	// enable
	cart.write(0xA000, 0x55);
	CHECK(cart.read(0xA000) == 0x55);

	cart.write(0x0000, 0x08);	// disable
	CHECK(cart.read(0xA000) == 0xFF);
}

TEST_CASE("Cart: MBC2 decodes registers with A8 and mirrors 512 nibbles")
{
	std::vector<u8> rom = make_rom(0x10000, 0x05, 0x01, 0x00);
	rom[0x4000] = 0x11;	// ROM bank 1
	rom[0x8000] = 0x22;	// ROM bank 2
	rom[0xC000] = 0x33;	// ROM bank 3

	Cart cart;
	REQUIRE(cart.load_cart(rom) == 0);
	CHECK_FALSE(cart.has_battery());

	CHECK(cart.read(0xA000) == 0xFF);	// RAM disabled by default

	cart.write(0x0000, 0x0A);	// A8=0: enable RAM
	cart.write(0xA000, 0xAB);
	CHECK(cart.read(0xA000) == 0xFB);	// only the low nibble is data

	cart.write(0xA200, 0xCD);	// echo of 0xA000
	CHECK(cart.read(0xA000) == 0xFD);
	CHECK(cart.read(0xA200) == 0xFD);

	cart.write(0x0200, 0x00);	// A8=0: disable RAM
	CHECK(cart.read(0xA000) == 0xFF);

	cart.write(0x0100, 0x02);	// A8=1: ROM bank 2
	CHECK(cart.read(0x4000) == 0x22);

	cart.write(0x0300, 0x01);	// A8=1: ROM bank 1
	CHECK(cart.read(0x4000) == 0x11);

	cart.write(0x0200, 0x0A);	// A8=0: enable RAM, bank stays 1
	CHECK(cart.read(0xA000) == 0xFD);
	CHECK(cart.read(0x4000) == 0x11);
}

TEST_CASE("Cart: MBC2 battery saves 512 bytes and loads them back")
{
	const std::vector<u8> rom = make_rom(0x8000, 0x06, 0x00, 0x00);
	Cart cart;
	REQUIRE(cart.load_cart(rom) == 0);
	REQUIRE(cart.has_battery());

	cart.write(0x0000, 0x0A);	// enable RAM
	cart.write(0xA000, 0x0B);
	cart.write(0xA1FF, 0x0C);

	CHECK(cart.read_battery_ram().size() == 0x200);
	CHECK(cart.save_battery_ram(SAVE_PATH));

	Cart loaded;
	REQUIRE(loaded.load_cart(rom) == 0);
	CHECK(loaded.load_battery_ram(SAVE_PATH));
	loaded.write(0x0000, 0x0A);	// enable RAM
	CHECK(loaded.read(0xA000) == 0xFB);
	CHECK(loaded.read(0xA1FF) == 0xFC);

	std::filesystem::remove(SAVE_PATH);
}

TEST_CASE("Cart: MBC2 loads packed 256-byte mGBA saves")
{
	const std::vector<u8> rom = make_rom(0x8000, 0x06, 0x00, 0x00);
	Cart cart;
	REQUIRE(cart.load_cart(rom) == 0);

	std::vector<u8> packed(0x100, 0);
	packed[0x00] = 0x21;	// A000=1, A001=2
	packed[0x7F] = 0x43;	// A0FE=3, A0FF=4
	{
		std::ofstream out(SAVE_PATH, std::ios::binary | std::ios::trunc);
		REQUIRE(out.good());
		out.write(reinterpret_cast<const char*>(packed.data()),
			static_cast<std::streamsize>(packed.size()));
	}

	CHECK(cart.load_battery_ram(SAVE_PATH));
	cart.write(0x0000, 0x0A);	// enable RAM
	CHECK(cart.read(0xA000) == 0xF1);
	CHECK(cart.read(0xA001) == 0xF2);
	CHECK(cart.read(0xA0FE) == 0xF3);
	CHECK(cart.read(0xA0FF) == 0xF4);
	CHECK(cart.read_battery_ram().size() == 0x200);	// canonical 512

	std::filesystem::remove(SAVE_PATH);
}

TEST_CASE("Cart: MBC1 1MB uses secondary bits 5-6 for ROM banking")
{
	std::vector<u8> rom = make_rom(0x100000, 0x01, 0x05, 0x00);
	set_bank_marker(rom, 0x01, 0x01);
	set_bank_marker(rom, 0x20, 0x20);	// only reachable via mode 1
	set_bank_marker(rom, 0x21, 0x21);

	Cart cart;
	REQUIRE(cart.load_cart(rom) == 0);
	CHECK(cart.read(0x4000) == 0x01);	// default bank 1

	cart.write(0x2000, 0x01);	// main bank = 1
	cart.write(0x4000, 0x01);	// secondary = 1 -> bank $21
	CHECK(cart.read(0x4000) == 0x21);

	cart.write(0x2000, 0x00);	// full register 0 -> 00->01 translation
	CHECK(cart.read(0x4000) == 0x21);

	cart.write(0x6000, 0x01);	// mode 1: bank 0 region uses secondary
	CHECK(cart.read(0x0000) == 0x20);
}

TEST_CASE("Cart: MBC1M uses secondary bits 4-5 and mode 1 bank 0")
{
	std::vector<u8> rom = make_rom(0x100000, 0x01, 0x05, 0x00);
	make_mbc1m_signature(rom);
	set_bank_marker(rom, 0x01, 0x01);
	set_bank_marker(rom, 0x10, 0x10);
	set_bank_marker(rom, 0x11, 0x11);
	set_bank_marker(rom, 0x20, 0x20);
	set_bank_marker(rom, 0x21, 0x21);

	Cart cart;
	REQUIRE(cart.load_cart(rom) == 0);
	CHECK(cart.read(0x4000) == 0x01);	// default bank 1

	cart.write(0x2000, 0x01);	// main bank = 1
	cart.write(0x4000, 0x01);	// secondary = 1 -> bank $11 (not $21)
	CHECK(cart.read(0x4000) == 0x11);

	cart.write(0x6000, 0x01);	// mode 1: bank 0 region -> bank $10
	CHECK(cart.read(0x0000) == 0x10);
}

TEST_CASE("Cart: MBC3 ROM and RAM banking")
{
	std::vector<u8> rom = make_rom(0x10000, 0x11, 0x01, 0x00);
	rom[0x4000] = 0x11;
	rom[0x8000] = 0x22;
	rom[0xC000] = 0x33;

	Cart cart;
	REQUIRE(cart.load_cart(rom) == 0);
	CHECK_FALSE(cart.has_battery());
	CHECK(cart.read(0x4000) == 0x11);

	cart.write(0x2000, 0x02);
	CHECK(cart.read(0x4000) == 0x22);

	cart.write(0x2000, 0x00);	// 0 -> 1 translation
	CHECK(cart.read(0x4000) == 0x11);

	const std::vector<u8> ram_rom = make_rom(0x8000, 0x13, 0x00, 0x03);
	Cart ram_cart;
	REQUIRE(ram_cart.load_cart(ram_rom) == 0);
	CHECK(ram_cart.has_battery());

	ram_cart.write(0x0000, 0x0A);
	ram_cart.write(0x4000, 0x00);
	ram_cart.write(0xA000, 0x10);
	ram_cart.write(0x4000, 0x03);
	ram_cart.write(0xA000, 0x13);

	ram_cart.write(0x4000, 0x00);
	CHECK(ram_cart.read(0xA000) == 0x10);
	ram_cart.write(0x4000, 0x03);
	CHECK(ram_cart.read(0xA000) == 0x13);
}

TEST_CASE("Cart: MBC5 ROM banking uses all 9 bits and allows bank 0")
{
	std::vector<u8> rom = make_rom(0x800000, 0x19, 0x08, 0x00);
	rom[0x0000] = 0x42;	// bank 0, offset 0
	set_bank_marker(rom, 0x001, 0x11);
	set_bank_marker(rom, 0x100, 0x22);
	set_bank_marker(rom, 0x1FF, 0x33);

	Cart cart;
	REQUIRE(cart.load_cart(rom) == 0);
	CHECK(cart.read(0x0000) == 0x42);
	CHECK(cart.read(0x4000) == 0x11);	// reset bank is 1

	cart.write(0x2000, 0x00);
	cart.write(0x3000, 0x00);
	CHECK(cart.read(0x4000) == 0x42);	// MBC5 maps physical bank 0

	cart.write(0x2000, 0x00);
	cart.write(0x3000, 0x01);
	CHECK(cart.read(0x4000) == 0x22);	// bank $100

	cart.write(0x2000, 0xFF);
	cart.write(0x3000, 0x01);
	CHECK(cart.read(0x4000) == 0x33);	// bank $1FF

	cart.write(0x2000, 0x01);
	cart.write(0x3000, 0xFE);	// only bit 0 of the high register is wired
	CHECK(cart.read(0x4000) == 0x11);	// bank 1
}

TEST_CASE("Cart: MBC5 RAM banking and battery save")
{
	const std::vector<u8> rom = make_rom(0x8000, 0x1B, 0x00, 0x04);
	Cart cart;
	REQUIRE(cart.load_cart(rom) == 0);
	REQUIRE(cart.has_battery());

	CHECK(cart.read(0xA000) == 0xFF);	// RAM disabled by default
	cart.write(0x0000, 0x0A);
	for(u8 bank = 0; bank < 16; bank++)
	{
		cart.write(0x4000, bank);
		cart.write(0xA000, static_cast<u8>(0x10 + bank));
		cart.write(0xBFFF, static_cast<u8>(0x20 + bank));
	}

	for(u8 bank = 0; bank < 16; bank++)
	{
		cart.write(0x4000, bank);
		CHECK(cart.read(0xA000) == static_cast<u8>(0x10 + bank));
		CHECK(cart.read(0xBFFF) == static_cast<u8>(0x20 + bank));
	}

	CHECK(cart.read_battery_ram().size() == 0x20000);
	CHECK(cart.save_battery_ram(SAVE_PATH));

	Cart loaded;
	REQUIRE(loaded.load_cart(rom) == 0);
	REQUIRE(loaded.has_battery());
	CHECK(loaded.load_battery_ram(SAVE_PATH));
	loaded.write(0x0000, 0x0A);
	for(u8 bank = 0; bank < 16; bank++)
	{
		loaded.write(0x4000, bank);
		CHECK(loaded.read(0xA000) == static_cast<u8>(0x10 + bank));
		CHECK(loaded.read(0xBFFF) == static_cast<u8>(0x20 + bank));
	}

	std::filesystem::remove(SAVE_PATH);
}

TEST_CASE("Cart: MBC5 rumble bit does not select a RAM bank")
{
	const std::vector<u8> rumble_rom = make_rom(0x8000, 0x1E, 0x00, 0x03);
	Cart rumble;
	REQUIRE(rumble.load_cart(rumble_rom) == 0);
	REQUIRE(rumble.has_battery());

	rumble.write(0x0000, 0x0A);
	rumble.write(0x4000, 0x00);
	rumble.write(0xA000, 0x10);
	rumble.write(0x4000, 0x01);
	rumble.write(0xA000, 0x11);

	rumble.write(0x4000, 0x08);	// rumble only: RAM bank 0
	CHECK(rumble.read(0xA000) == 0x10);
	rumble.write(0x4000, 0x09);	// rumble + RAM bank 1
	CHECK(rumble.read(0xA000) == 0x11);

	const std::vector<u8> plain_rom = make_rom(0x8000, 0x1B, 0x00, 0x04);
	Cart plain;
	REQUIRE(plain.load_cart(plain_rom) == 0);
	plain.write(0x0000, 0x0A);
	plain.write(0x4000, 0x08);
	plain.write(0xA000, 0x88);

	plain.write(0x4000, 0x00);
	CHECK(plain.read(0xA000) != 0x88);
	plain.write(0x4000, 0x08);
	CHECK(plain.read(0xA000) == 0x88);	// non-rumble: bank 8
}

TEST_CASE("Cart: MBC3 RTC latch, seconds and halt")
{
	const std::vector<u8> rom = make_rom(0x8000, 0x10, 0x00, 0x02);
	int64_t now = 1000 * RTC_SECOND_US;
	Cart cart;
	cart.set_rtc_clock([&now] { return now; });
	REQUIRE(cart.load_cart(rom) == 0);
	REQUIRE(cart.has_battery());

	cart.write(0x0000, 0x0A);	// enable RAM/RTC
	cart.write(0x4000, 0x08);	// seconds
	cart.write(0x6000, 0x00);	// latch
	cart.write(0x6000, 0x01);
	CHECK(cart.read(0xA000) == 0);

	now += RTC_SECOND_US;
	cart.write(0x6000, 0x00);
	cart.write(0x6000, 0x01);
	CHECK(cart.read(0xA000) == 1);

	now += 59 * RTC_SECOND_US;	// 60 seconds total
	cart.write(0x4000, 0x09);	// minutes
	cart.write(0x6000, 0x00);
	cart.write(0x6000, 0x01);
	CHECK(cart.read(0xA000) == 1);
	cart.write(0x4000, 0x08);
	cart.write(0x6000, 0x00);
	cart.write(0x6000, 0x01);
	CHECK(cart.read(0xA000) == 0);

	cart.write(0x4000, 0x0C);	// control: halt
	cart.write(0xA000, 0x40);
	now += 10 * RTC_SECOND_US;
	cart.write(0x4000, 0x08);
	cart.write(0x6000, 0x00);
	cart.write(0x6000, 0x01);
	CHECK(cart.read(0xA000) == 0);	// time did not advance while halted

	cart.write(0x4000, 0x0C);
	cart.write(0xA000, 0x00);	// unhalt
	now += RTC_SECOND_US;
	cart.write(0x4000, 0x08);
	cart.write(0x6000, 0x00);
	cart.write(0x6000, 0x01);
	CHECK(cart.read(0xA000) == 1);
}

TEST_CASE("Cart: MBC3 RTC day overflow and carry")
{
	const std::vector<u8> rom = make_rom(0x8000, 0x10, 0x00, 0x02);
	int64_t now = 2000 * RTC_SECOND_US;
	Cart cart;
	cart.set_rtc_clock([&now] { return now; });
	REQUIRE(cart.load_cart(rom) == 0);
	cart.write(0x0000, 0x0A);

	cart.write(0x4000, 0x08);
	cart.write(0xA000, 59);
	cart.write(0x4000, 0x09);
	cart.write(0xA000, 59);
	cart.write(0x4000, 0x0A);
	cart.write(0xA000, 23);
	cart.write(0x4000, 0x0B);
	cart.write(0xA000, 0xFF);
	cart.write(0x4000, 0x0C);
	cart.write(0xA000, 0x01);	// day bit 8 = 1 -> 511 days

	now += RTC_SECOND_US;
	cart.write(0x6000, 0x00);
	cart.write(0x6000, 0x01);

	cart.write(0x4000, 0x0B);
	CHECK(cart.read(0xA000) == 0x00);	// days low byte
	cart.write(0x4000, 0x0C);
	CHECK(cart.read(0xA000) == 0x80);	// carry set, day bit 8 clear
	cart.write(0x4000, 0x0A);
	CHECK(cart.read(0xA000) == 0x00);
	cart.write(0x4000, 0x09);
	CHECK(cart.read(0xA000) == 0x00);
	cart.write(0x4000, 0x08);
	CHECK(cart.read(0xA000) == 0x00);
}

TEST_CASE("Cart: MBC3 RTC invalid values roll at field width")
{
	const std::vector<u8> rom = make_rom(0x8000, 0x10, 0x00, 0x02);
	int64_t now = 3000 * RTC_SECOND_US;
	Cart cart;
	cart.set_rtc_clock([&now] { return now; });
	REQUIRE(cart.load_cart(rom) == 0);
	cart.write(0x0000, 0x0A);

	cart.write(0x4000, 0x08);
	cart.write(0xA000, 63);
	cart.write(0x4000, 0x09);
	cart.write(0xA000, 5);

	now += RTC_SECOND_US;
	cart.write(0x6000, 0x00);
	cart.write(0x6000, 0x01);

	cart.write(0x4000, 0x08);
	CHECK(cart.read(0xA000) == 0x00);	// 63 -> 0, no carry
	cart.write(0x4000, 0x09);
	CHECK(cart.read(0xA000) == 5);	// minutes unchanged
}

TEST_CASE("Cart: MBC3 RTC saves and loads the 48-byte VBA block")
{
	const std::vector<u8> rom = make_rom(0x8000, 0x10, 0x00, 0x02);
	int64_t now = 4000 * RTC_SECOND_US;
	Cart cart;
	cart.set_rtc_clock([&now] { return now; });
	REQUIRE(cart.load_cart(rom) == 0);
	cart.write(0x0000, 0x0A);

	cart.write(0x4000, 0x08);
	cart.write(0xA000, 0);
	cart.write(0x4000, 0x09);
	cart.write(0xA000, 0);
	cart.write(0x4000, 0x0A);
	cart.write(0xA000, 0);
	cart.write(0x4000, 0x0B);
	cart.write(0xA000, 0);
	cart.write(0x4000, 0x0C);
	cart.write(0xA000, 0x00);

	CHECK(cart.read_battery_data().size() == 0x2000 + 48);
	CHECK(cart.save_battery_ram(SAVE_PATH));

	now += 100 * RTC_SECOND_US;	// time passes while the emulator is closed
	Cart loaded;
	loaded.set_rtc_clock([&now] { return now; });
	REQUIRE(loaded.load_cart(rom) == 0);
	CHECK(loaded.load_battery_ram(SAVE_PATH));
	loaded.write(0x0000, 0x0A);
	loaded.write(0x6000, 0x00);
	loaded.write(0x6000, 0x01);

	loaded.write(0x4000, 0x09);
	CHECK(loaded.read(0xA000) == 1);	// 100 seconds = 1:40
	loaded.write(0x4000, 0x08);
	CHECK(loaded.read(0xA000) == 40);

	std::filesystem::remove(SAVE_PATH);
}

TEST_CASE("Cart: MBC3 RTC-only cart saves 48 bytes")
{
	const std::vector<u8> rom = make_rom(0x8000, 0x0F, 0x00, 0x00);
	int64_t now = 5000 * RTC_SECOND_US;
	Cart cart;
	cart.set_rtc_clock([&now] { return now; });
	REQUIRE(cart.load_cart(rom) == 0);
	REQUIRE(cart.has_battery());
	CHECK(cart.read_battery_data().size() == 48);

	cart.write(0x0000, 0x0A);
	cart.write(0x4000, 0x08);
	cart.write(0x6000, 0x00);
	cart.write(0x6000, 0x01);
	CHECK(cart.read(0xA000) == 0);

	CHECK(cart.save_battery_ram(SAVE_PATH));
	Cart loaded;
	loaded.set_rtc_clock([&now] { return now; });
	REQUIRE(loaded.load_cart(rom) == 0);
	CHECK(loaded.load_battery_ram(SAVE_PATH));
	loaded.write(0x0000, 0x0A);
	loaded.write(0x4000, 0x08);
	loaded.write(0x6000, 0x00);
	loaded.write(0x6000, 0x01);
	CHECK(loaded.read(0xA000) == 0);

	std::filesystem::remove(SAVE_PATH);
}

TEST_CASE("Cart: MBC30 uses 8-bit ROM banks and 8 RAM banks")
{
	std::vector<u8> rom = make_rom(0x400000, 0x10, 0x07, 0x05);
	set_bank_marker(rom, 0x01, 0x01);
	set_bank_marker(rom, 0x80, 0x80);
	set_bank_marker(rom, 0xFF, 0xFF);

	Cart cart;
	REQUIRE(cart.load_cart(rom) == 0);
	REQUIRE(cart.has_battery());
	CHECK(cart.read(0x4000) == 0x01);

	cart.write(0x2000, 0x80);
	CHECK(cart.read(0x4000) == 0x80);
	cart.write(0x2000, 0xFF);
	CHECK(cart.read(0x4000) == 0xFF);
	cart.write(0x2000, 0x00);
	CHECK(cart.read(0x4000) == 0x01);

	cart.write(0x0000, 0x0A);
	cart.write(0x4000, 0x00);
	cart.write(0xA000, 0x10);
	cart.write(0x4000, 0x07);
	cart.write(0xA000, 0x77);
	cart.write(0x4000, 0x00);
	CHECK(cart.read(0xA000) == 0x10);
	cart.write(0x4000, 0x07);
	CHECK(cart.read(0xA000) == 0x77);

	CHECK(cart.read_battery_data().size() == 0x10000 + 48);
}
