#include "doctest.h"
#include "path_settings.h"

#include <filesystem>

TEST_CASE("Path settings: ROM folder defaults")
{
	const PathSettings settings;
	CHECK(settings.battery_same_as_rom);
	CHECK(settings.savestates_same_as_rom);
	CHECK(PathSettings::battery_path(settings, "/roms/Pokemon Red.gb") ==
		"/roms/Pokemon Red.sav");
	CHECK(PathSettings::state_path(settings, "/roms/Pokemon Red.gb") ==
		"/roms/Pokemon Red.sr0.png");
}

TEST_CASE("Path settings: central flat directories")
{
	PathSettings settings;
	settings.battery = "/saves/battery";
	settings.savestates = "/saves/states";
	settings.battery_same_as_rom = false;
	settings.savestates_same_as_rom = false;

	CHECK(PathSettings::battery_path(settings, "/roms/Pokemon Red.gb") ==
		"/saves/battery/Pokemon Red.sav");
	CHECK(PathSettings::state_path(settings, "/roms/Pokemon Red.gb") ==
		"/saves/states/Pokemon Red.sr0.png");
}

TEST_CASE("Path settings: battery player 2 and state slots")
{
	PathSettings settings;
	settings.battery = "/saves/battery";
	settings.savestates = "/saves/states";
	settings.battery_same_as_rom = false;
	settings.savestates_same_as_rom = false;

	CHECK(PathSettings::battery_path(settings, "/roms/game.gb", 2) ==
		"/saves/battery/game.sa2");
	CHECK(PathSettings::state_path(settings, "/roms/game.gb", 7) ==
		"/saves/states/game.sr7.png");
	CHECK(PathSettings::state_path(settings, "/roms/game.gb", 9) ==
		"/saves/states/game.sr9.png");
}

TEST_CASE("Path settings: empty central directories fall back to the ROM")
{
	PathSettings settings;
	settings.battery_same_as_rom = false;
	settings.savestates_same_as_rom = false;

	CHECK(PathSettings::battery_path(settings, "/roms/game.gbc") ==
		"/roms/game.sav");
	CHECK(PathSettings::state_path(settings, "/roms/game.gbc") ==
		"/roms/game.sr0.png");
}

TEST_CASE("Path settings: equality covers paths and checkboxes")
{
	PathSettings first;
	PathSettings second;
	CHECK(first == second);

	second.battery = "/saves";
	CHECK(first != second);

	first = second;
	CHECK(first == second);

	second.battery_same_as_rom = false;
	CHECK(first != second);
}
