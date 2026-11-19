#ifndef QTREGB_STATE_STORE_H
#define QTREGB_STATE_STORE_H

#include <cstdint>
#include <string>
#include <vector>

#include "path_settings.h"
#include "types.h"

// Metadata stored next to the raw core blob in <rom>.sr<slot>. Everything
// here is GUI/container information; the machine state itself is the core
// blob produced by QtreGB::save_state().
struct SavestateInfo
{
	static constexpr std::uint8_t KindManual = 0;
	static constexpr std::uint8_t KindQuick = 1;
	static constexpr std::uint8_t KindAuto = 2;

	// ROM identity (informational; the core blob already binds the state to
	// the ROM size and FNV-1a hash).
	std::string rom_title;
	std::string rom_filename;
	std::uint32_t rom_size = 0;
	std::uint64_t rom_hash = 0;
	std::uint8_t mapper = 0;
	std::uint8_t cgb_flag = 0;
	std::uint8_t cart_type = 0;
	std::uint8_t header_checksum = 0;
	std::uint16_t global_checksum = 0;

	// BIOS identity. The core bus chunk already validates the hash when the
	// boot ROM is active; this copy is for display and preflight checks.
	std::uint32_t bios_size = 0;
	std::uint64_t bios_hash = 0;
	bool bios_active = false;

	std::uint64_t created_unix_seconds = 0;
	std::uint64_t emulated_frames = 0;
	int slot = 0;
	std::uint8_t kind = KindManual;
	std::string note;
	std::string app_version;

	// Preview in 0xRRGGBBAA pixels, top-down. Empty means no screenshot.
	std::uint16_t preview_width = 0;
	std::uint16_t preview_height = 0;
	std::vector<std::uint32_t> preview_pixels;
};

// Savestate container for one ROM: <rom>.sr<slot>.png, slot 0..9. The file is
// a valid PNG image (the screenshot) followed by the container bytes, so file
// managers can show it as a thumbnail. The container itself keeps the same
// header/metadata/raw-preview layout described below.
//
// Layout (little-endian, no padding):
//   PNG:     signature, IHDR, IDAT (screenshot), IEND;
//   header:  magic "QTGBSTF1", container version, core format/schema, created
//            time, emulated frames, slot/kind/flags, ROM/BIOS identity,
//            payload sizes and CRCs;
//   payload: metadata strings, optional RGBA preview, raw core state blob.
//
// Legacy files containing only the raw container or the raw core blob
// ("QTGBST01") are still accepted. Writes are atomic through a temporary file.
class StateStore
{
	public:
		void set_rom_path(const std::string& rom_path);
		void set_path_settings(const PathSettings& settings);
		void set_slot(int slot);
		int slot() const { return m_slot; }
		const std::string& rom_path() const { return m_rom_path; }
		std::string path() const;
		// Old raw-container path without the .png wrapper: <rom>.sr<slot>.
		std::string legacy_path() const;

		bool save(const SavestateInfo& info,
			const std::vector<std::uint8_t>& data) const;
		// info may be null when the caller only needs the core blob.
		bool load(std::vector<std::uint8_t>& data,
			SavestateInfo* info = nullptr) const;

	private:
		std::string m_rom_path;
		PathSettings m_path_settings;
		int m_slot = 0;
};

#endif
