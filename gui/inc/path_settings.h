#ifndef QTREGB_PATH_SETTINGS_H
#define QTREGB_PATH_SETTINGS_H

#include <string>

#include "types.h"

// Working directories used by the GUI, plus the global BIOS file path.
// Empty strings mean "not configured": file dialogs fall back to the last
// used directory, while battery and state paths fall back to the ROM folder.
//
// Files inside the configured battery/state directories use flat names based
// on the ROM stem, so two ROMs with the same file name share their save files.
struct PathSettings
{
	std::string roms;
	// Path to the 256-byte DMG boot ROM file, not a directory.
	std::string bios;
	std::string battery;
	std::string savestates;
	std::string screenshots;
	bool battery_same_as_rom = true;
	bool savestates_same_as_rom = true;

	bool operator==(const PathSettings& other) const
	{
		return roms == other.roms && bios == other.bios &&
			battery == other.battery && savestates == other.savestates &&
			screenshots == other.screenshots &&
			battery_same_as_rom == other.battery_same_as_rom &&
			savestates_same_as_rom == other.savestates_same_as_rom;
	}
	bool operator!=(const PathSettings& other) const
	{
		return !(*this == other);
	}

	// <rom stem>.sav or <rom stem>.sa2 (player 2) in the battery directory.
	static std::string battery_path(const PathSettings& settings,
		const std::string& rom_path, qtreGB::u8 player_index = 1);
	// <rom stem>.sr<slot>.png in the savestate directory. Slot range: 0..9.
	static std::string state_path(const PathSettings& settings,
		const std::string& rom_path, int slot = 0);
};

#endif
