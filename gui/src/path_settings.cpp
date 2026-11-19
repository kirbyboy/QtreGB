#include <algorithm>
#include "path_settings.h"

#include <filesystem>

#include "cart.h"

namespace
{

std::string rom_stem(const std::string& rom_path)
{
	const std::filesystem::path path =
		std::filesystem::u8path(rom_path).filename();
	const std::string stem = path.stem().u8string();
	return stem.empty() ? "rom" : stem;
}

qtreGB::u8 normalized_player(qtreGB::u8 player_index)
{
	return player_index == 2 ? 2 : 1;
}

} // namespace

std::string PathSettings::battery_path(const PathSettings& settings,
	const std::string& rom_path, qtreGB::u8 player_index)
{
	if(rom_path.empty())
	{
		return {};
	}

	const qtreGB::u8 player = normalized_player(player_index);
	if(settings.battery_same_as_rom || settings.battery.empty())
	{
		std::filesystem::path path =
			std::filesystem::u8path(qtreGB::Cart::battery_save_path(rom_path));
		if(player == 2)
		{
			path.replace_extension(".sa2");
		}
		return path.u8string();
	}

	const std::string filename = rom_stem(rom_path) +
		(player == 2 ? ".sa2" : ".sav");
	return (std::filesystem::u8path(settings.battery) /
		std::filesystem::u8path(filename)).u8string();
}

std::string PathSettings::state_path(const PathSettings& settings,
	const std::string& rom_path, int slot)
{
	if(rom_path.empty())
	{
		return {};
	}

	const int state_slot = std::clamp(slot, 0, 9);
	const std::string extension = ".sr" + std::to_string(state_slot) + ".png";

	if(settings.savestates_same_as_rom || settings.savestates.empty())
	{
		std::filesystem::path path = std::filesystem::u8path(rom_path);
		path.replace_extension(extension);
		return path.u8string();
	}

	const std::string filename = rom_stem(rom_path) + extension;
	return (std::filesystem::u8path(settings.savestates) /
		std::filesystem::u8path(filename)).u8string();
}
