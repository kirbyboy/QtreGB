#ifndef QTREGB_SESSION_STATE_H
#define QTREGB_SESSION_STATE_H

#include <cstdint>
#include <string>
#include <vector>

#include "path_settings.h"
#include "types.h"

// One machine inside a link-session state file.
struct SessionRomInfo
{
	std::string name;
	std::string path;
	std::uint32_t size = 0;
	std::uint64_t hash = 0;
};

// User-level link session state. The two state blobs are the raw core
// states; the GUI adds ROM identity, BIOS metadata, a combined preview and
// versioning around them. Version 1 files (metadata without preview) still
// load; saves always use the current version.
struct SessionState
{
	static constexpr std::uint16_t kVersion = 2;

	SessionRomInfo roms[2];
	std::string bios_path;
	std::uint64_t bios_hash = 0;
	std::uint32_t bios_size = 0;
	std::uint64_t created_unix_seconds = 0;
	std::uint64_t emulated_frames = 0;
	std::string app_version;
	// Combined 320x144 frame exactly as displayed (0xRRGGBBAA pixels).
	std::uint16_t preview_width = 0;
	std::uint16_t preview_height = 0;
	std::vector<std::uint32_t> preview_pixels;
	std::vector<qtreGB::u8> states[2];

	// "Game1 - Game2 (short-hash).qss", safe for a single filename. The
	// on-disk path appends ".png" because the state is a PNG image with the
	// container bytes appended.
	static std::string file_name(const SessionRomInfo& rom1,
		const SessionRomInfo& rom2);
	// Session file path derived from player 1's state directory.
	static std::string path_for(const PathSettings& settings,
		const SessionState& state);

	// Binary container with magic, version, CRCs, ROM metadata and the two
	// core blobs. Saves atomically through a temporary file.
	static bool save(const std::string& path, const SessionState& state,
		std::string& error);
	static bool load(const std::string& path, SessionState& state,
		std::string& error);
};

#endif
