#include "doctest.h"
#include "png_writer.h"
#include "state_store.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace std;

namespace
{

const filesystem::path STATE_STORE_TEST_DIR =
	filesystem::temp_directory_path() / "qtrebg_state_store_test";

vector<uint8_t> read_file(const filesystem::path& path)
{
	ifstream input(path, ios::binary);
	return vector<uint8_t>(istreambuf_iterator<char>(input),
		istreambuf_iterator<char>());
}

void write_file(const filesystem::path& path, const vector<uint8_t>& data)
{
	ofstream output(path, ios::binary | ios::trunc);
	REQUIRE(output.good());
	output.write(reinterpret_cast<const char*>(data.data()),
		static_cast<streamsize>(data.size()));
}

SavestateInfo make_info()
{
	SavestateInfo info;
	info.rom_title = "TETRIS";
	info.rom_filename = "tetris.gb";
	info.rom_size = 32768;
	info.rom_hash = 0x0123456789ABCDEFull;
	info.mapper = 2;
	info.cgb_flag = 0x80;
	info.cart_type = 0x1B;
	info.header_checksum = 0x5A;
	info.global_checksum = 0x1234;
	info.bios_size = 256;
	info.bios_hash = 0xFEDCBA9876543210ull;
	info.bios_active = true;
	info.created_unix_seconds = 1700000000;
	info.emulated_frames = 123456;
	info.slot = 3;
	info.kind = SavestateInfo::KindQuick;
	info.note = "before the final boss";
	info.app_version = "test-build";
	info.preview_width = 2;
	info.preview_height = 2;
	info.preview_pixels = {
		0x11223344u, 0x55667788u, 0x99AABBCCu, 0xDDEEFF00u
	};
	return info;
}

} // namespace

TEST_CASE("StateStore: container round-trip keeps metadata and preview")
{
	filesystem::remove_all(STATE_STORE_TEST_DIR);
	filesystem::create_directories(STATE_STORE_TEST_DIR);

	StateStore store;
	store.set_rom_path((STATE_STORE_TEST_DIR / "tetris.gb").string());
	PathSettings settings;
	settings.savestates_same_as_rom = true;
	store.set_path_settings(settings);
	store.set_slot(3);
	REQUIRE(store.path() ==
		(STATE_STORE_TEST_DIR / "tetris.sr3.png").string());

	const SavestateInfo info = make_info();
	const vector<uint8_t> core = {
		0x51, 0x54, 0x47, 0x42, 0x53, 0x54, 0x30, 0x31,
		0xDE, 0xAD, 0xBE, 0xEF
	};
	REQUIRE(store.save(info, core));

	const vector<uint8_t> file = read_file(store.path());
	// The file starts with a valid PNG; the container follows IEND.
	std::size_t container_offset = 0;
	REQUIRE(png_payload_offset(file, container_offset));
	CHECK(container_offset > 8);
	CHECK(container_offset < file.size());
	CHECK(file.size() > container_offset + 84 + core.size());

	vector<uint8_t> loaded_core;
	SavestateInfo loaded;
	REQUIRE(store.load(loaded_core, &loaded));
	CHECK(loaded_core == core);
	CHECK(loaded.rom_title == info.rom_title);
	CHECK(loaded.rom_filename == info.rom_filename);
	CHECK(loaded.rom_size == info.rom_size);
	CHECK(loaded.rom_hash == info.rom_hash);
	CHECK(loaded.mapper == info.mapper);
	CHECK(loaded.cgb_flag == info.cgb_flag);
	CHECK(loaded.cart_type == info.cart_type);
	CHECK(loaded.header_checksum == info.header_checksum);
	CHECK(loaded.global_checksum == info.global_checksum);
	CHECK(loaded.bios_size == info.bios_size);
	CHECK(loaded.bios_hash == info.bios_hash);
	CHECK(loaded.bios_active);
	CHECK(loaded.created_unix_seconds == info.created_unix_seconds);
	CHECK(loaded.emulated_frames == info.emulated_frames);
	CHECK(loaded.slot == 3);
	CHECK(loaded.kind == SavestateInfo::KindQuick);
	CHECK(loaded.note == info.note);
	CHECK(loaded.app_version == info.app_version);
	CHECK(loaded.preview_width == 2);
	CHECK(loaded.preview_height == 2);
	CHECK(loaded.preview_pixels == info.preview_pixels);

	filesystem::remove_all(STATE_STORE_TEST_DIR);
}

TEST_CASE("StateStore: corrupt headers and payloads are rejected")
{
	filesystem::remove_all(STATE_STORE_TEST_DIR);
	filesystem::create_directories(STATE_STORE_TEST_DIR);

	StateStore store;
	store.set_rom_path((STATE_STORE_TEST_DIR / "tetris.gb").string());
	PathSettings settings;
	settings.savestates_same_as_rom = true;
	store.set_path_settings(settings);

	const vector<uint8_t> core = {0x51, 0x54, 0x47, 0x42, 0x53, 0x54, 0x30,
		0x31, 0x01, 0x02, 0x03, 0x04};
	REQUIRE(store.save(make_info(), core));
	const vector<uint8_t> original = read_file(store.path());
	REQUIRE(original.size() > 90);

	std::size_t container_offset = 0;
	REQUIRE(png_payload_offset(original, container_offset));

	vector<uint8_t> corrupt = original;
	corrupt.back() ^= 0xFF;
	write_file(store.path(), corrupt);
	vector<uint8_t> loaded;
	CHECK_FALSE(store.load(loaded));

	corrupt = original;
	corrupt[container_offset + 20] ^= 0x01;
	write_file(store.path(), corrupt);
	CHECK_FALSE(store.load(loaded));

	corrupt = original;
	corrupt.resize(corrupt.size() - 1);
	write_file(store.path(), corrupt);
	CHECK_FALSE(store.load(loaded));

	filesystem::remove_all(STATE_STORE_TEST_DIR);
}

TEST_CASE("StateStore: legacy raw core blobs still load")
{
	filesystem::remove_all(STATE_STORE_TEST_DIR);
	filesystem::create_directories(STATE_STORE_TEST_DIR);

	StateStore store;
	store.set_rom_path((STATE_STORE_TEST_DIR / "tetris.gb").string());
	PathSettings settings;
	settings.savestates_same_as_rom = true;
	store.set_path_settings(settings);

	const vector<uint8_t> legacy = {
		0x51, 0x54, 0x47, 0x42, 0x53, 0x54, 0x30, 0x31,
		0x10, 0x20, 0x30, 0x40
	};
	write_file(store.legacy_path(), legacy);

	vector<uint8_t> loaded;
	SavestateInfo info;
	REQUIRE(store.load(loaded, &info));
	CHECK(loaded == legacy);
	CHECK(info.rom_title.empty());
	CHECK(info.preview_pixels.empty());

	filesystem::remove_all(STATE_STORE_TEST_DIR);
}

TEST_CASE("StateStore: empty or mismatched previews are not saved")
{
	filesystem::remove_all(STATE_STORE_TEST_DIR);
	filesystem::create_directories(STATE_STORE_TEST_DIR);

	StateStore store;
	store.set_rom_path((STATE_STORE_TEST_DIR / "tetris.gb").string());

	CHECK_FALSE(store.save(SavestateInfo {}, {}));

	SavestateInfo info;
	info.preview_width = 2;
	info.preview_height = 2;
	info.preview_pixels = {0x11223344u};
	CHECK_FALSE(store.save(info, {0x51, 0x54, 0x47, 0x42, 0x53, 0x54, 0x30,
		0x31}));

	filesystem::remove_all(STATE_STORE_TEST_DIR);
}
