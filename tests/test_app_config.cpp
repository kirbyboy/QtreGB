#include "doctest.h"
#include "app_config.h"

#include <SDL2/SDL.h>

#include <filesystem>
#include <fstream>
#include <string>

using namespace std;

namespace
{

const filesystem::path CONFIG_TEST_DIR =
	filesystem::temp_directory_path() / "qtrebg_app_config_test";

void write_text(const filesystem::path& path, const string& text)
{
	filesystem::create_directories(path.parent_path());
	ofstream output(path, ios::binary | ios::trunc);
	REQUIRE(output.good());
	output << text;
}

} // namespace

TEST_CASE("AppConfig: missing file keeps defaults")
{
	const AppConfig config =
		AppConfig::load((CONFIG_TEST_DIR / "missing.ini").string());
	CHECK(config.window_width == AppConfig::kDefaultWindowWidth);
	CHECK(config.window_height == AppConfig::kDefaultWindowHeight);
	CHECK_FALSE(config.window_maximized);
	CHECK(config.display.ghosting_light_ms == 32);
	CHECK(config.display.ghosting_dark_ms == 16);
	CHECK_FALSE(config.display.lcd_grid);
	CHECK(config.display.integer_scaling);
	CHECK(config.display.grid_opacity == 100);
	CHECK(config.display.present_mode == PresentMode::Auto);
	CHECK((config.display.background == Rgb {0, 0, 0}));
	CHECK(config.audio.enabled);
	CHECK(config.audio.volume == 50);
	CHECK(config.audio.link_source == LinkAudioSource::Player1);
	CHECK(config.fast_forward_speed == FastForwardSpeed::X200);
	CHECK(config.recent_roms.empty());
	CHECK(config.language == "system");
	CHECK(config.input.shortcuts[3].keyboard ==
		(HostControl {HostControlType::Key, SDL_SCANCODE_PAGEUP, 0}));
	CHECK(config.input.shortcuts[4].keyboard ==
		(HostControl {HostControlType::Key, SDL_SCANCODE_PAGEDOWN, 0}));
	CHECK(config.input.shortcuts[5].keyboard ==
		(HostControl {HostControlType::Key, SDL_SCANCODE_SPACE, 0}));
	CHECK(config.paths.roms.empty());
	CHECK(config.paths.battery.empty());
	CHECK(config.paths.battery_same_as_rom);
	CHECK(config.paths.savestates_same_as_rom);
}

TEST_CASE("AppConfig: round-trip and malformed values")
{
	filesystem::remove_all(CONFIG_TEST_DIR);

	const filesystem::path path =
		CONFIG_TEST_DIR / "with space" / "QtreGB.ini";
	AppConfig saved;
	saved.window_width = 1280;
	saved.window_height = 720;
	saved.window_maximized = true;

	string error;
	REQUIRE(saved.save(path.string(), error));
	CHECK(error.empty());

	AppConfig loaded = AppConfig::load(path.string());
	CHECK(loaded.window_width == 1280);
	CHECK(loaded.window_height == 720);
	CHECK(loaded.window_maximized);

	// Saving again must replace the previous file atomically.
	loaded.window_width = 640;
	loaded.window_maximized = false;
	REQUIRE(loaded.save(path.string(), error));
	AppConfig replaced = AppConfig::load(path.string());
	CHECK(replaced.window_width == 640);
	CHECK_FALSE(replaced.window_maximized);

	const filesystem::path malformed = CONFIG_TEST_DIR / "malformed.ini";
	write_text(malformed,
		"version=1\n[window]\nwidth=nope\nheight=-1\nmaximized=maybe\n"
		"[recent]\n0=/zero.gb\n11=/eleven.gb\nx=/bad.gb\n"
		"1=/one.gb\n2=/two.gb\n");
	const AppConfig fallback = AppConfig::load(malformed.string());
	CHECK(fallback.window_width == AppConfig::kDefaultWindowWidth);
	CHECK(fallback.window_height == AppConfig::kDefaultWindowHeight);
	CHECK_FALSE(fallback.window_maximized);
	REQUIRE(fallback.recent_roms.size() == 2);
	CHECK(fallback.recent_roms.paths()[0] == "/one.gb");
	CHECK(fallback.recent_roms.paths()[1] == "/two.gb");

	filesystem::remove_all(CONFIG_TEST_DIR);
}

TEST_CASE("AppConfig: graphics settings round-trip")
{
	filesystem::remove_all(CONFIG_TEST_DIR);
	const filesystem::path path = CONFIG_TEST_DIR / "QtreGB.ini";

	AppConfig saved;
	saved.display.palette = PaletteMode::Custom;
	saved.display.custom_palette = {{
		{0x11, 0x22, 0x33},
		{0x44, 0x55, 0x66},
		{0x77, 0x88, 0x99},
		{0xAA, 0xBB, 0xCC},
	}};
	saved.display.background = {0x12, 0x34, 0x56};
	saved.display.integer_scaling = false;
	saved.display.lcd_grid = true;
	saved.display.grid_opacity = 45;
	saved.display.present_mode = PresentMode::Monitor;
	saved.display.ghosting = true;
	saved.display.ghosting_light_ms = 120;
	saved.display.ghosting_dark_ms = 90;
	saved.audio.enabled = false;
	saved.audio.volume = 73;
	saved.audio.link_source = LinkAudioSource::Both;
	saved.fast_forward_speed = FastForwardSpeed::Max;
	saved.language = "es";
	saved.paths.roms = "/games/roms";
	saved.paths.bios = "/games/bios";
	saved.paths.battery = "/games/saves";
	saved.paths.savestates = "/games/states";
	saved.paths.screenshots = "/games/shots";
	saved.paths.battery_same_as_rom = false;
	saved.paths.savestates_same_as_rom = false;

	string error;
	REQUIRE(saved.save(path.string(), error));
	const AppConfig loaded = AppConfig::load(path.string());
	CHECK(loaded.display.palette == PaletteMode::Custom);
	CHECK(loaded.display.custom_palette == saved.display.custom_palette);
	CHECK((loaded.display.background == Rgb {0x12, 0x34, 0x56}));
	CHECK_FALSE(loaded.display.integer_scaling);
	CHECK(loaded.display.lcd_grid);
	CHECK(loaded.display.grid_opacity == 45);
	CHECK(loaded.display.present_mode == PresentMode::Monitor);
	CHECK(loaded.display.ghosting);
	CHECK(loaded.display.ghosting_light_ms == 120);
	CHECK(loaded.display.ghosting_dark_ms == 90);
	CHECK_FALSE(loaded.audio.enabled);
	CHECK(loaded.audio.volume == 73);
	CHECK(loaded.audio.link_source == LinkAudioSource::Both);
	CHECK(loaded.fast_forward_speed == FastForwardSpeed::Max);
	CHECK(loaded.language == "es");
	CHECK(loaded.paths.roms == "/games/roms");
	CHECK(loaded.paths.bios == "/games/bios");
	CHECK(loaded.paths.battery == "/games/saves");
	CHECK(loaded.paths.savestates == "/games/states");
	CHECK(loaded.paths.screenshots == "/games/shots");
	CHECK_FALSE(loaded.paths.battery_same_as_rom);
	CHECK_FALSE(loaded.paths.savestates_same_as_rom);
	CHECK(DisplaySettings::to_rgba({0x11, 0x22, 0x33}) == 0x112233FFu);
	CHECK_FALSE(loaded.input.allow_opposite_directions);
	CHECK(loaded.input.players[0].device.type == InputDeviceType::Keyboard);

	filesystem::remove_all(CONFIG_TEST_DIR);
}

TEST_CASE("AppConfig: input and shortcut settings round-trip")
{
	filesystem::remove_all(CONFIG_TEST_DIR);
	const filesystem::path path = CONFIG_TEST_DIR / "QtreGB.ini";

	AppConfig saved;
	saved.input.allow_opposite_directions = true;
	saved.input.players[0].device = {};
	saved.input.players[0].device.type = InputDeviceType::Controller;
	saved.input.players[0].device.guid = "030000005e0400008e02000000000000";
	saved.input.players[0].bindings[static_cast<size_t>(GbButton::A)] =
		{HostControlType::ControllerButton, SDL_CONTROLLER_BUTTON_A, 0};
	saved.input.players[0].bindings[static_cast<size_t>(GbButton::Up)] =
		{HostControlType::ControllerAxis, SDL_CONTROLLER_AXIS_LEFTY, -1};
	saved.input.players[1].device.type = InputDeviceType::None;
	saved.input.shortcuts[0].controller =
		{HostControlType::ControllerButton, SDL_CONTROLLER_BUTTON_BACK, 0};
	// Clearing a default shortcut must survive the round-trip as "none".
	saved.input.shortcuts[1].keyboard = {};
	saved.input.shortcuts[2].keyboard =
		{HostControlType::Key, SDL_SCANCODE_F12, 0};

	string error;
	REQUIRE(saved.save(path.string(), error));
	const AppConfig loaded = AppConfig::load(path.string());
	CHECK(loaded.input.allow_opposite_directions);
	CHECK(loaded.input.players[0].device.type == InputDeviceType::Controller);
	CHECK(loaded.input.players[0].device.guid ==
		"030000005e0400008e02000000000000");
	CHECK(loaded.input.players[0].bindings[static_cast<size_t>(GbButton::A)] ==
		(HostControl {HostControlType::ControllerButton,
			SDL_CONTROLLER_BUTTON_A, 0}));
	CHECK(loaded.input.players[0].bindings[static_cast<size_t>(GbButton::Up)] ==
		(HostControl {HostControlType::ControllerAxis,
			SDL_CONTROLLER_AXIS_LEFTY, -1}));
	CHECK(loaded.input.shortcuts[0].controller ==
		(HostControl {HostControlType::ControllerButton,
			SDL_CONTROLLER_BUTTON_BACK, 0}));
	CHECK(loaded.input.shortcuts[1].keyboard.is_none());
	CHECK(loaded.input.shortcuts[2].keyboard ==
		(HostControl {HostControlType::Key, SDL_SCANCODE_F12, 0}));

	filesystem::remove_all(CONFIG_TEST_DIR);
}

TEST_CASE("AppConfig: input conflicts are cleaned when loading")
{
	filesystem::remove_all(CONFIG_TEST_DIR);
	const filesystem::path path = CONFIG_TEST_DIR / "conflicts.ini";

	// p1 has Right and Left on the same key; B collides with Fullscreen;
	// both Save state and Load state use F8. Player 2 may reuse p1's Z key.
	write_text(path,
		"version=1\n"
		"[input]\n"
		"p1_device=keyboard\n"
		"p1_right=key:79\n"
		"p1_left=key:79\n"
		"p1_up=key:82\n"
		"p1_down=key:81\n"
		"p1_a=key:29\n"
		"p1_b=key:9\n"
		"p1_start=key:40\n"
		"p1_select=key:229\n"
		"p2_device=keyboard\n"
		"p2_right=key:79\n"
		"p2_left=key:80\n"
		"p2_up=key:82\n"
		"p2_down=key:81\n"
		"p2_a=key:29\n"
		"p2_b=key:27\n"
		"p2_start=key:40\n"
		"p2_select=key:229\n"
		"[shortcuts]\n"
		"fullscreen_key=key:9\n"
		"save_state_key=key:66\n"
		"load_state_key=key:66\n");

	const AppConfig loaded = AppConfig::load(path.string());
	const auto& p1 = loaded.input.players[0].bindings;
	const auto& p2 = loaded.input.players[1].bindings;

	// Right comes before Left, so Left is the duplicate that is removed.
	CHECK(p1[static_cast<size_t>(GbButton::Right)] ==
		(HostControl {HostControlType::Key, 79, 0}));
	CHECK(p1[static_cast<size_t>(GbButton::Left)].is_none());
	// The key binding wins and removes the player's B.
	CHECK(p1[static_cast<size_t>(GbButton::B)].is_none());
	// Duplicate shortcuts keep the first and remove the last.
	CHECK(loaded.input.shortcuts[1].keyboard ==
		(HostControl {HostControlType::Key, 66, 0}));
	CHECK(loaded.input.shortcuts[2].keyboard.is_none());
	// The same controls are allowed across players.
	CHECK(p2[static_cast<size_t>(GbButton::Right)] ==
		(HostControl {HostControlType::Key, 79, 0}));
	CHECK(p2[static_cast<size_t>(GbButton::A)] ==
		(HostControl {HostControlType::Key, 29, 0}));

	filesystem::remove_all(CONFIG_TEST_DIR);
}

TEST_CASE("AppConfig: recent ROMs round-trip in order")
{
	filesystem::remove_all(CONFIG_TEST_DIR);
	const filesystem::path path = CONFIG_TEST_DIR / "QtreGB.ini";

	AppConfig saved;
	saved.recent_roms.remember("/games/tetris.gb");
	saved.recent_roms.remember("/games/pokemon.gb");
	saved.recent_roms.remember("/games/mario.gb");

	string error;
	REQUIRE(saved.save(path.string(), error));
	const AppConfig loaded = AppConfig::load(path.string());
	REQUIRE(loaded.recent_roms.size() == 3);
	CHECK(loaded.recent_roms.paths()[0] == "/games/mario.gb");
	CHECK(loaded.recent_roms.paths()[1] == "/games/pokemon.gb");
	CHECK(loaded.recent_roms.paths()[2] == "/games/tetris.gb");

	// Indexed keys with holes are compacted, and out-of-range keys ignored.
	write_text(path,
		"version=1\n[recent]\n1=/a.gb\n3=/c.gb\n11=/bad.gb\n");
	const AppConfig sparse = AppConfig::load(path.string());
	REQUIRE(sparse.recent_roms.size() == 2);
	CHECK(sparse.recent_roms.paths()[0] == "/a.gb");
	CHECK(sparse.recent_roms.paths()[1] == "/c.gb");

	filesystem::remove_all(CONFIG_TEST_DIR);
}
