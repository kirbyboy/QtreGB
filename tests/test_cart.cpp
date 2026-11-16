#include "doctest.h"
#include "cart.h"
#include "error_reporter.h"

#include <filesystem>
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

	CHECK(cart.load_cart(make_rom(0x8000, 0x03, 0x00, 0x02)) == 0);
	CHECK(cart.has_battery());

	CHECK(cart.load_cart(make_rom(0x8000, 0x09, 0x00, 0x02)) == 0);
	CHECK(cart.has_battery());
}
