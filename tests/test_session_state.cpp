#include "doctest.h"
#include "png_writer.h"
#include "session_state.h"
#include "state_stream.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{

const std::filesystem::path SESSION_TEST_DIR =
	std::filesystem::temp_directory_path() / "qtrebg_session_state_test";

SessionState make_state()
{
	SessionState state;
	state.roms[0].name = "Pokemon Red.gb";
	state.roms[0].path = "/roms/Pokemon Red.gb";
	state.roms[0].size = 1024;
	state.roms[0].hash = 0x123456789abcdef0ull;
	state.roms[1].name = "Pokemon Blue.gb";
	state.roms[1].path = "/roms/Pokemon Blue.gb";
	state.roms[1].size = 2048;
	state.roms[1].hash = 0x0fedcba987654321ull;
	state.bios_path = "/roms/bios/dmg.bin";
	state.bios_hash = 0x1111222233334444ull;
	state.bios_size = 256;
	state.created_unix_seconds = 1700000000;
	state.emulated_frames = 98765;
	state.app_version = "test-build";
	state.preview_width = 2;
	state.preview_height = 2;
	state.preview_pixels = {
		0x11223344u, 0x55667788u, 0x99AABBCCu, 0xDDEEFF00u
	};
	state.states[0] = {1, 2, 3, 4};
	state.states[1] = {5, 6, 7};
	return state;
}

void put_u16(std::vector<std::uint8_t>& out, std::uint16_t value)
{
	out.push_back(static_cast<std::uint8_t>(value & 0xFF));
	out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
}

void put_u32(std::vector<std::uint8_t>& out, std::uint32_t value)
{
	for(int i = 0; i < 4; ++i)
	{
		out.push_back(static_cast<std::uint8_t>((value >> (i * 8)) & 0xFF));
	}
}

void put_u64(std::vector<std::uint8_t>& out, std::uint64_t value)
{
	for(int i = 0; i < 8; ++i)
	{
		out.push_back(static_cast<std::uint8_t>((value >> (i * 8)) & 0xFF));
	}
}

void put_string(std::vector<std::uint8_t>& out, const std::string& value)
{
	put_u32(out, static_cast<std::uint32_t>(value.size()));
	out.insert(out.end(), value.begin(), value.end());
}

void put_rom_v1(std::vector<std::uint8_t>& out, const SessionRomInfo& rom)
{
	put_string(out, rom.name);
	put_string(out, rom.path);
	put_u32(out, rom.size);
	put_u64(out, rom.hash);
}

void write_v1_session(const std::filesystem::path& path,
	const SessionState& state)
{
	std::vector<std::uint8_t> header;
	put_rom_v1(header, state.roms[0]);
	put_rom_v1(header, state.roms[1]);
	put_string(header, state.bios_path);
	put_u64(header, state.bios_hash);

	std::vector<std::uint8_t> payload;
	for(const std::vector<qtreGB::u8>& blob : state.states)
	{
		put_u32(payload, static_cast<std::uint32_t>(blob.size()));
		payload.insert(payload.end(), blob.begin(), blob.end());
	}

	std::vector<std::uint8_t> output = {
		'Q', 'T', 'R', 'S', 'E', 'S', '0', '1'
	};
	put_u16(output, 1);
	put_u16(output, 0x0001);
	put_u32(output, static_cast<std::uint32_t>(header.size()));
	put_u32(output, qtreGB::state_crc32(header.data(), header.size()));
	put_u32(output, static_cast<std::uint32_t>(payload.size()));
	put_u32(output, qtreGB::state_crc32(payload.data(), payload.size()));
	output.insert(output.end(), header.begin(), header.end());
	output.insert(output.end(), payload.begin(), payload.end());

	std::filesystem::create_directories(path.parent_path());
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	REQUIRE(out.good());
	out.write(reinterpret_cast<const char*>(output.data()),
		static_cast<std::streamsize>(output.size()));
}

} // namespace

TEST_CASE("Session state: file name includes both games and hash")
{
	SessionRomInfo first;
	first.name = "Pokemon Red.gb";
	first.hash = 0x1234;
	SessionRomInfo second;
	second.name = "Pokemon Blue.gbc";
	second.hash = 0x5678;

	const std::string name = SessionState::file_name(first, second);
	CHECK(name.rfind("Pokemon Red - Pokemon Blue (", 0) == 0);
	CHECK(name.size() > 0);
	CHECK(name.back() != ' ');
	CHECK(name.substr(name.size() - 4) == ".qss");
}

TEST_CASE("Session state: file name sanitizes invalid characters")
{
	SessionRomInfo first;
	first.name = "A/B:C*D?.gb";
	first.hash = 1;
	SessionRomInfo second;
	second.name = "Game\\Two.gb";
	second.hash = 2;

	const std::string name = SessionState::file_name(first, second);
	CHECK(name.find('/') == std::string::npos);
	CHECK(name.find('\\') == std::string::npos);
	CHECK(name.find(':') == std::string::npos);
	CHECK(name.find('*') == std::string::npos);
	CHECK(name.find('?') == std::string::npos);
	CHECK(name.substr(name.size() - 4) == ".qss");
}

TEST_CASE("Session state: long names are bounded")
{
	SessionRomInfo first;
	first.name = std::string(200, 'a') + ".gb";
	first.hash = 1;
	SessionRomInfo second;
	second.name = std::string(200, 'b') + ".gb";
	second.hash = 2;

	const std::string name = SessionState::file_name(first, second);
	CHECK(name.size() <= 255);
	CHECK(name.substr(name.size() - 4) == ".qss");
}

TEST_CASE("Session state: round-trip through the binary container")
{
	std::filesystem::remove_all(SESSION_TEST_DIR);
	const std::filesystem::path path = SESSION_TEST_DIR / "session.qss.png";
	const SessionState saved = make_state();

	std::string error;
	REQUIRE(SessionState::save(path.u8string(), saved, error));
	CHECK(error.empty());
	{
		std::vector<std::uint8_t> bytes;
		std::ifstream input(path, std::ios::binary);
		bytes.assign(std::istreambuf_iterator<char>(input),
			std::istreambuf_iterator<char>());
		std::size_t offset = 0;
		REQUIRE(png_payload_offset(bytes, offset));
		CHECK(offset > 8);
		CHECK(offset < bytes.size());
	}

	SessionState loaded;
	REQUIRE(SessionState::load(path.u8string(), loaded, error));
	CHECK(error.empty());
	CHECK(loaded.roms[0].name == saved.roms[0].name);
	CHECK(loaded.roms[0].path == saved.roms[0].path);
	CHECK(loaded.roms[0].hash == saved.roms[0].hash);
	CHECK(loaded.roms[1].hash == saved.roms[1].hash);
	CHECK(loaded.bios_path == saved.bios_path);
	CHECK(loaded.bios_hash == saved.bios_hash);
	CHECK(loaded.bios_size == saved.bios_size);
	CHECK(loaded.created_unix_seconds == saved.created_unix_seconds);
	CHECK(loaded.emulated_frames == saved.emulated_frames);
	CHECK(loaded.app_version == saved.app_version);
	CHECK(loaded.preview_width == saved.preview_width);
	CHECK(loaded.preview_height == saved.preview_height);
	CHECK(loaded.preview_pixels == saved.preview_pixels);
	CHECK(loaded.states[0] == saved.states[0]);
	CHECK(loaded.states[1] == saved.states[1]);

	// Corrupting one payload byte must be rejected.
	std::vector<char> bytes;
	{
		std::ifstream input(path, std::ios::binary);
		bytes.assign(std::istreambuf_iterator<char>(input),
			std::istreambuf_iterator<char>());
	}
	REQUIRE(bytes.size() > 32);
	bytes.back() ^= 0x01;
	{
		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
	}
	CHECK_FALSE(SessionState::load(path.u8string(), loaded, error));
	CHECK_FALSE(error.empty());

	std::filesystem::remove_all(SESSION_TEST_DIR);
}

TEST_CASE("Session state: path uses player 1 savestate directory")
{
	PathSettings settings;
	settings.savestates = "/saves/states";
	settings.savestates_same_as_rom = false;
	SessionState state;
	state.roms[0].name = "Game One.gb";
	state.roms[0].path = "/roms/Game One.gb";
	state.roms[1].name = "Game Two.gb";
	state.roms[1].path = "/roms/Game Two.gb";

	const std::string path = SessionState::path_for(settings, state);
	CHECK(path.rfind("/saves/states/", 0) == 0);
	CHECK(path.find("Game One - Game Two (") != std::string::npos);
	CHECK(path.substr(path.size() - 8) == ".qss.png");
}

TEST_CASE("Session state: a directory cannot be loaded as a session")
{
	std::filesystem::remove_all(SESSION_TEST_DIR);
	std::filesystem::create_directories(SESSION_TEST_DIR);
	SessionState state;
	std::string error;
	CHECK_FALSE(SessionState::load(SESSION_TEST_DIR.u8string(), state, error));
	CHECK_FALSE(error.empty());
	std::filesystem::remove_all(SESSION_TEST_DIR);
}

TEST_CASE("Session state: version 1 files still load")
{
	std::filesystem::remove_all(SESSION_TEST_DIR);
	const std::filesystem::path path = SESSION_TEST_DIR / "legacy.qss";
	const SessionState original = make_state();
	write_v1_session(path, original);

	SessionState loaded;
	std::string error;
	REQUIRE(SessionState::load(path.u8string(), loaded, error));
	CHECK(loaded.roms[0].name == original.roms[0].name);
	CHECK(loaded.roms[1].hash == original.roms[1].hash);
	CHECK(loaded.bios_path == original.bios_path);
	CHECK(loaded.bios_hash == original.bios_hash);
	CHECK(loaded.bios_size == 0);
	CHECK(loaded.created_unix_seconds == 0);
	CHECK(loaded.emulated_frames == 0);
	CHECK(loaded.app_version.empty());
	CHECK(loaded.preview_width == 0);
	CHECK(loaded.preview_pixels.empty());
	CHECK(loaded.states[0] == original.states[0]);
	CHECK(loaded.states[1] == original.states[1]);

	std::filesystem::remove_all(SESSION_TEST_DIR);
}

TEST_CASE("Session state: invalid preview metadata is rejected")
{
	std::filesystem::remove_all(SESSION_TEST_DIR);
	const std::filesystem::path path = SESSION_TEST_DIR / "invalid.qss.png";
	SessionState state = make_state();
	state.preview_pixels.pop_back();
	std::string error;
	CHECK_FALSE(SessionState::save(path.u8string(), state, error));
	CHECK_FALSE(error.empty());
	std::filesystem::remove_all(SESSION_TEST_DIR);
}

TEST_CASE("Session state: legacy .qss is found when .qss.png is missing")
{
	std::filesystem::remove_all(SESSION_TEST_DIR);
	const std::filesystem::path legacy =
		SESSION_TEST_DIR / "legacy.qss";
	const SessionState original = make_state();
	write_v1_session(legacy, original);

	SessionState loaded;
	std::string error;
	const std::filesystem::path requested =
		SESSION_TEST_DIR / "legacy.qss.png";
	REQUIRE(SessionState::load(requested.u8string(), loaded, error));
	CHECK(loaded.roms[0].hash == original.roms[0].hash);
	CHECK(loaded.states[0] == original.states[0]);
	CHECK(loaded.states[1] == original.states[1]);

	std::filesystem::remove_all(SESSION_TEST_DIR);
}
