#include "doctest.h"
#include "cart.h"

#include <filesystem>
#include <string>
#include <vector>

using namespace qtreGB;

namespace
{

const std::filesystem::path BATTERY_TEST_DIR =
	std::filesystem::temp_directory_path() / "qtrebg_cart_battery_test";

std::vector<u8> make_battery_rom()
{
	std::vector<u8> rom(0x8000, 0);
	rom[0x147] = 0x1B;	// MBC5 + RAM + battery
	rom[0x148] = 0x00;	// 32 KiB ROM
	rom[0x149] = 0x03;	// 4 RAM banks (32 KiB)
	return rom;
}

} // namespace

TEST_CASE("Cart battery: dirty state and atomic .sav replacement")
{
	std::filesystem::remove_all(BATTERY_TEST_DIR);
	std::filesystem::create_directories(BATTERY_TEST_DIR);
	const std::filesystem::path save_path = BATTERY_TEST_DIR / "game.sav";

	Cart cart;
	REQUIRE(cart.load_cart(make_battery_rom()) == 0);
	CHECK(cart.has_battery());
	CHECK_FALSE(cart.battery_dirty());

	cart.write(0x0000, 0x0A);	// RAM enable
	cart.write(0x4000, 0x00);	// RAM bank 0
	cart.write(0xA000, 0x42);
	CHECK(cart.battery_dirty());

	REQUIRE(cart.save_battery_ram(save_path.u8string()));
	CHECK_FALSE(cart.battery_dirty());
	CHECK(std::filesystem::is_regular_file(save_path));
	CHECK_FALSE(std::filesystem::exists(save_path.string() + ".tmp"));

	// A second save must replace the existing file atomically.
	cart.write(0xA000, 0x99);
	CHECK(cart.battery_dirty());
	REQUIRE(cart.save_battery_ram(save_path.u8string()));
	CHECK_FALSE(cart.battery_dirty());
	CHECK_FALSE(std::filesystem::exists(save_path.string() + ".tmp"));

	Cart loaded;
	REQUIRE(loaded.load_cart(make_battery_rom()) == 0);
	REQUIRE(loaded.load_battery_ram(save_path.u8string()));
	CHECK_FALSE(loaded.battery_dirty());
	const std::vector<u8> data = loaded.read_battery_ram();
	REQUIRE_FALSE(data.empty());
	CHECK(data[0] == 0x99);

	// Loading a savestate that restores cartridge RAM must mark the battery
	// for the next autosave.
	loaded.mark_battery_dirty();
	CHECK(loaded.battery_dirty());

	std::filesystem::remove_all(BATTERY_TEST_DIR);
}
