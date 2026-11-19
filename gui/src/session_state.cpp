#include "session_state.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string_view>

#include "png_writer.h"
#include "state_stream.h"

namespace
{

constexpr std::string_view MAGIC = "QTRSES01";
constexpr std::uint16_t LEGACY_VERSION = 1;
constexpr std::uint16_t FLAG_LINK_CONNECTED = 0x0001;
constexpr std::uint16_t FLAG_HAS_PREVIEW = 0x0002;
constexpr std::size_t FIXED_HEADER_SIZE = 28;
constexpr std::size_t MAX_STRING_BYTES = 1u << 20;
constexpr std::size_t MAX_STATE_BYTES = 64u << 20;
constexpr std::size_t MAX_PREVIEW_BYTES = 64u << 20;
constexpr std::uint32_t MAX_PREVIEW_DIMENSION = 4096;
constexpr std::size_t MAX_FILE_BYTES = 256u << 20;
constexpr std::size_t MAX_SESSION_BASE_BYTES = 240;

void put_u16(std::vector<qtreGB::u8>& out, std::uint16_t value)
{
	out.push_back(static_cast<qtreGB::u8>(value & 0xFF));
	out.push_back(static_cast<qtreGB::u8>((value >> 8) & 0xFF));
}

void put_u32(std::vector<qtreGB::u8>& out, std::uint32_t value)
{
	for(int i = 0; i < 4; ++i)
	{
		out.push_back(static_cast<qtreGB::u8>((value >> (i * 8)) & 0xFF));
	}
}

void put_u64(std::vector<qtreGB::u8>& out, std::uint64_t value)
{
	for(int i = 0; i < 8; ++i)
	{
		out.push_back(static_cast<qtreGB::u8>((value >> (i * 8)) & 0xFF));
	}
}

void put_string(std::vector<qtreGB::u8>& out, const std::string& value)
{
	const std::size_t limit = std::min(value.size(), MAX_STRING_BYTES);
	put_u32(out, static_cast<std::uint32_t>(limit));
	out.insert(out.end(), value.begin(), value.begin() +
		static_cast<std::ptrdiff_t>(limit));
}

bool read_u16(const std::vector<qtreGB::u8>& data, std::size_t& offset,
	std::uint16_t& value)
{
	if(offset + 2 > data.size())
	{
		return false;
	}
	value = static_cast<std::uint16_t>(data[offset]) |
		(static_cast<std::uint16_t>(data[offset + 1]) << 8);
	offset += 2;
	return true;
}

bool read_u32(const std::vector<qtreGB::u8>& data, std::size_t& offset,
	std::uint32_t& value)
{
	if(offset + 4 > data.size())
	{
		return false;
	}
	value = 0;
	for(int i = 0; i < 4; ++i)
	{
		value |= static_cast<std::uint32_t>(data[offset + i]) << (i * 8);
	}
	offset += 4;
	return true;
}

bool read_u64(const std::vector<qtreGB::u8>& data, std::size_t& offset,
	std::uint64_t& value)
{
	if(offset + 8 > data.size())
	{
		return false;
	}
	value = 0;
	for(int i = 0; i < 8; ++i)
	{
		value |= static_cast<std::uint64_t>(data[offset + i]) << (i * 8);
	}
	offset += 8;
	return true;
}

bool read_string(const std::vector<qtreGB::u8>& data, std::size_t& offset,
	std::string& value)
{
	std::uint32_t size = 0;
	if(!read_u32(data, offset, size) || size > MAX_STRING_BYTES ||
	   offset + size > data.size())
	{
		return false;
	}
	value.assign(reinterpret_cast<const char*>(data.data() + offset), size);
	offset += size;
	return true;
}

void put_rom(std::vector<qtreGB::u8>& out, const SessionRomInfo& rom)
{
	put_string(out, rom.name);
	put_string(out, rom.path);
	put_u32(out, rom.size);
	put_u64(out, rom.hash);
}

bool read_rom(const std::vector<qtreGB::u8>& data, std::size_t& offset,
	SessionRomInfo& rom)
{
	return read_string(data, offset, rom.name) &&
		read_string(data, offset, rom.path) &&
		read_u32(data, offset, rom.size) &&
		read_u64(data, offset, rom.hash);
}

std::string rom_stem(const SessionRomInfo& rom)
{
	std::string source = !rom.name.empty() ? rom.name : rom.path;
	std::filesystem::path path = std::filesystem::u8path(source);
	return path.stem().u8string();
}

std::string sanitize(std::string value)
{
	for(char& character : value)
	{
		const unsigned char byte = static_cast<unsigned char>(character);
		if(byte < 0x20 || byte == 0x7F ||
		   character == '/' || character == '\\' || character == ':' ||
		   character == '*' || character == '?' || character == '"' ||
		   character == '<' || character == '>' || character == '|')
		{
			character = '_';
		}
	}
	while(!value.empty() && (value.back() == ' ' || value.back() == '.'))
	{
		value.pop_back();
	}
	while(!value.empty() && (value.front() == ' ' || value.front() == '.'))
	{
		value.erase(value.begin());
	}
	if(value.empty())
	{
		value = "Unknown";
	}
	return value;
}

std::string truncate_utf8(const std::string& value, std::size_t max_bytes)
{
	if(value.size() <= max_bytes)
	{
		return value;
	}
	std::size_t valid = 0;
	std::size_t index = 0;
	while(index < value.size() && index < max_bytes)
	{
		const unsigned char byte =
			static_cast<unsigned char>(value[index]);
		std::size_t length = 1;
		if((byte & 0xE0) == 0xC0)
		{
			length = 2;
		}
		else if((byte & 0xF0) == 0xE0)
		{
			length = 3;
		}
		else if((byte & 0xF8) == 0xF0)
		{
			length = 4;
		}
		if(index + length > max_bytes)
		{
			break;
		}
		index += length;
		valid = index;
	}
	return value.substr(0, valid);
}

std::string short_hash(std::uint64_t first, std::uint64_t second)
{
	const std::uint64_t mixed = first ^
		(second + 0x9e3779b97f4a7c15ull + (first << 6) + (first >> 2));
	char buffer[16];
	std::snprintf(buffer, sizeof(buffer), "%08x",
		static_cast<unsigned int>(mixed ^ (mixed >> 32)));
	return buffer;
}

bool read_file(const std::string& path, std::vector<qtreGB::u8>& data)
{
	const std::filesystem::path file_path =
		std::filesystem::u8path(path);
	std::error_code error;
	if(!std::filesystem::is_regular_file(file_path, error))
	{
		return false;
	}

	std::ifstream input(file_path, std::ios::binary);
	if(!input)
	{
		return false;
	}
	input.seekg(0, std::ios::end);
	if(!input)
	{
		return false;
	}
	const std::streamsize size = input.tellg();
	if(size < 0 || static_cast<std::size_t>(size) > MAX_FILE_BYTES)
	{
		return false;
	}
	input.seekg(0, std::ios::beg);
	if(!input)
	{
		return false;
	}
	data.resize(static_cast<std::size_t>(size));
	if(size > 0)
	{
		input.read(reinterpret_cast<char*>(data.data()), size);
	}
	return input.good() || input.eof();
}

} // namespace

std::string SessionState::file_name(const SessionRomInfo& rom1,
	const SessionRomInfo& rom2)
{
	std::string first = sanitize(rom_stem(rom1));
	std::string second = sanitize(rom_stem(rom2));

	constexpr std::size_t separator_bytes = 3;	// " - "
	const std::size_t half =
		(MAX_SESSION_BASE_BYTES - separator_bytes) / 2;
	first = truncate_utf8(first, half);
	second = truncate_utf8(second, half);

	return first + " - " + second + " (" +
		short_hash(rom1.hash, rom2.hash) + ").qss";
}

std::string SessionState::path_for(const PathSettings& settings,
	const SessionState& state)
{
	if(state.roms[0].path.empty())
	{
		return {};
	}
	std::filesystem::path path = std::filesystem::u8path(
		PathSettings::state_path(settings, state.roms[0].path, 0));
	path.replace_filename(file_name(state.roms[0], state.roms[1]) + ".png");
	return path.u8string();
}

bool SessionState::save(const std::string& path, const SessionState& state,
	std::string& error)
{
	error.clear();
	if(path.empty())
	{
		error = "No session path";
		return false;
	}
	if(state.states[0].empty() || state.states[1].empty())
	{
		error = "Cannot save an empty session state";
		return false;
	}

	std::vector<qtreGB::u8> header;
	put_rom(header, state.roms[0]);
	put_rom(header, state.roms[1]);
	put_string(header, state.bios_path);
	put_u64(header, state.bios_hash);
	put_u32(header, state.bios_size);
	put_u64(header, state.created_unix_seconds);
	put_u64(header, state.emulated_frames);
	put_string(header, state.app_version);

	std::vector<qtreGB::u8> preview;
	if(!state.preview_pixels.empty())
	{
		if(state.preview_width == 0 || state.preview_height == 0 ||
		   state.preview_width > MAX_PREVIEW_DIMENSION ||
		   state.preview_height > MAX_PREVIEW_DIMENSION)
		{
			error = "Session preview is invalid";
			return false;
		}
		const std::size_t count = static_cast<std::size_t>(
			state.preview_width) * static_cast<std::size_t>(
			state.preview_height);
		if(count > MAX_PREVIEW_BYTES / 4 ||
		   state.preview_pixels.size() != count)
		{
			error = "Session preview is invalid";
			return false;
		}
		put_u16(preview, state.preview_width);
		put_u16(preview, state.preview_height);
		preview.push_back(0);	// RGBA8888
		preview.push_back(0);
		preview.push_back(0);
		preview.push_back(0);
		for(const std::uint32_t pixel : state.preview_pixels)
		{
			preview.push_back(
				static_cast<qtreGB::u8>((pixel >> 24) & 0xFF));
			preview.push_back(
				static_cast<qtreGB::u8>((pixel >> 16) & 0xFF));
			preview.push_back(
				static_cast<qtreGB::u8>((pixel >> 8) & 0xFF));
			preview.push_back(static_cast<qtreGB::u8>(pixel & 0xFF));
		}
	}

	std::vector<qtreGB::u8> payload;
	put_u32(payload, static_cast<std::uint32_t>(preview.size()));
	payload.insert(payload.end(), preview.begin(), preview.end());
	for(const std::vector<qtreGB::u8>& blob : state.states)
	{
		if(blob.size() > MAX_STATE_BYTES)
		{
			error = "Session state blob is too large";
			return false;
		}
		put_u32(payload, static_cast<std::uint32_t>(blob.size()));
		payload.insert(payload.end(), blob.begin(), blob.end());
	}

	std::vector<qtreGB::u8> output;
	output.reserve(FIXED_HEADER_SIZE + header.size() + payload.size());
	output.insert(output.end(), MAGIC.begin(), MAGIC.end());
	put_u16(output, kVersion);
	put_u16(output, FLAG_LINK_CONNECTED |
		(preview.empty() ? 0 : FLAG_HAS_PREVIEW));
	put_u32(output, static_cast<std::uint32_t>(header.size()));
	put_u32(output, qtreGB::state_crc32(header.data(), header.size()));
	put_u32(output, static_cast<std::uint32_t>(payload.size()));
	put_u32(output, qtreGB::state_crc32(payload.data(), payload.size()));
	output.insert(output.end(), header.begin(), header.end());
	output.insert(output.end(), payload.begin(), payload.end());

	std::vector<std::uint8_t> preview_png;
	if(!state.preview_pixels.empty())
	{
		if(!encode_png_rgba(state.preview_width, state.preview_height,
			state.preview_pixels, preview_png))
		{
			error = "Could not encode the session preview";
			return false;
		}
	}
	else
	{
		const std::vector<std::uint32_t> placeholder(
			static_cast<std::size_t>(320) * 144, 0x202020FFu);
		if(!encode_png_rgba(320, 144, placeholder, preview_png))
		{
			error = "Could not encode the session preview";
			return false;
		}
	}
	preview_png.insert(preview_png.end(), output.begin(), output.end());

	const std::filesystem::path file_path = std::filesystem::u8path(path);
	if(file_path.has_parent_path())
	{
		std::error_code directory_error;
		std::filesystem::create_directories(file_path.parent_path(),
			directory_error);
		if(directory_error)
		{
			error = "Could not create session directory: " +
				directory_error.message();
			return false;
		}
	}

	std::filesystem::path temporary = file_path;
	temporary += ".tmp";
	std::ofstream out(temporary, std::ios::out | std::ios::binary |
		std::ios::trunc);
	if(!out)
	{
		error = "Could not write " + temporary.u8string();
		return false;
	}
	out.write(reinterpret_cast<const char*>(preview_png.data()),
		static_cast<std::streamsize>(preview_png.size()));
	out.flush();
	if(!out)
	{
		out.close();
		std::error_code remove_error;
		std::filesystem::remove(temporary, remove_error);
		error = "Could not write " + temporary.u8string();
		return false;
	}
	out.close();

	std::error_code rename_error;
	std::filesystem::rename(temporary, file_path, rename_error);
	if(rename_error)
	{
		std::filesystem::remove(temporary, rename_error);
		error = "Could not replace " + file_path.u8string();
		return false;
	}
	return true;
}

bool SessionState::load(const std::string& path, SessionState& state,
	std::string& error)
{
	error.clear();
	std::vector<qtreGB::u8> data;
	std::string actual_path = path;
	std::filesystem::path file_path =
		std::filesystem::u8path(actual_path);
	std::error_code file_error;
	if(!std::filesystem::is_regular_file(file_path, file_error) &&
	   file_path.extension() == ".png")
	{
		file_path.replace_extension();
		actual_path = file_path.u8string();
	}
	if(!read_file(actual_path, data))
	{
		error = "Could not read " + path;
		return false;
	}
	std::size_t container_offset = 0;
	if(png_payload_offset(data, container_offset))
	{
		data.erase(data.begin(), data.begin() +
			static_cast<std::ptrdiff_t>(container_offset));
	}
	if(data.size() < FIXED_HEADER_SIZE)
	{
		error = "Session file is truncated";
		return false;
	}
	if(!std::equal(MAGIC.begin(), MAGIC.end(), data.begin()))
	{
		error = "Not a QtreGB session file";
		return false;
	}

	std::uint16_t version = 0;
	std::uint16_t flags = 0;
	std::uint32_t header_size = 0;
	std::uint32_t header_crc = 0;
	std::uint32_t payload_size = 0;
	std::uint32_t payload_crc = 0;
	std::size_t offset = MAGIC.size();
	if(!read_u16(data, offset, version) ||
	   !read_u16(data, offset, flags) ||
	   !read_u32(data, offset, header_size) ||
	   !read_u32(data, offset, header_crc) ||
	   !read_u32(data, offset, payload_size) ||
	   !read_u32(data, offset, payload_crc))
	{
		error = "Session file has an invalid header";
		return false;
	}
	if(version != kVersion && version != LEGACY_VERSION)
	{
		error = "Unsupported session file version";
		return false;
	}
	const std::uint16_t known_flags = version == LEGACY_VERSION
		? FLAG_LINK_CONNECTED
		: static_cast<std::uint16_t>(FLAG_LINK_CONNECTED | FLAG_HAS_PREVIEW);
	if((flags & ~known_flags) != 0)
	{
		error = "Session file has unsupported flags";
		return false;
	}
	if(header_size > MAX_FILE_BYTES || payload_size > MAX_FILE_BYTES ||
	   FIXED_HEADER_SIZE + header_size + payload_size != data.size())
	{
		error = "Session file has an invalid size";
		return false;
	}
	if(qtreGB::state_crc32(data.data() + FIXED_HEADER_SIZE, header_size) !=
		header_crc)
	{
		error = "Session file header checksum mismatch";
		return false;
	}
	if(qtreGB::state_crc32(data.data() + FIXED_HEADER_SIZE + header_size,
		payload_size) != payload_crc)
	{
		error = "Session file payload checksum mismatch";
		return false;
	}

	state = {};
	std::size_t header_offset = FIXED_HEADER_SIZE;
	const std::size_t header_end = FIXED_HEADER_SIZE + header_size;
	std::vector<qtreGB::u8> header(data.begin() +
		static_cast<std::ptrdiff_t>(header_offset), data.begin() +
		static_cast<std::ptrdiff_t>(header_end));
	std::size_t header_position = 0;
	bool metadata_ok =
		read_rom(header, header_position, state.roms[0]) &&
		read_rom(header, header_position, state.roms[1]) &&
		read_string(header, header_position, state.bios_path) &&
		read_u64(header, header_position, state.bios_hash);
	if(metadata_ok && version != LEGACY_VERSION)
	{
		metadata_ok =
			read_u32(header, header_position, state.bios_size) &&
			read_u64(header, header_position, state.created_unix_seconds) &&
			read_u64(header, header_position, state.emulated_frames) &&
			read_string(header, header_position, state.app_version);
	}
	if(!metadata_ok || header_position != header.size())
	{
		error = "Session file metadata is invalid";
		return false;
	}

	std::size_t payload_offset = header_end;
	if(version != LEGACY_VERSION)
	{
		std::uint32_t preview_size = 0;
		if(!read_u32(data, payload_offset, preview_size) ||
		   preview_size > MAX_PREVIEW_BYTES ||
		   payload_offset + preview_size > data.size())
		{
			error = "Session file preview is invalid";
			return false;
		}
		if(((flags & FLAG_HAS_PREVIEW) != 0) != (preview_size != 0))
		{
			error = "Session file preview flag is inconsistent";
			return false;
		}
		const std::size_t preview_end = payload_offset + preview_size;
		if(preview_size != 0)
		{
			std::uint16_t width = 0;
			std::uint16_t height = 0;
			if(preview_size < 8 ||
			   !read_u16(data, payload_offset, width) ||
			   !read_u16(data, payload_offset, height) ||
			   payload_offset + 4 > preview_end)
			{
				error = "Session file preview is invalid";
				return false;
			}
			const std::uint8_t format = data[payload_offset];
			payload_offset += 4;	// format + 3 reserved bytes
			if(format != 0 || width == 0 || height == 0 ||
			   width > MAX_PREVIEW_DIMENSION ||
			   height > MAX_PREVIEW_DIMENSION)
			{
				error = "Session file preview is invalid";
				return false;
			}
			const std::size_t count = static_cast<std::size_t>(width) *
				static_cast<std::size_t>(height);
			if(count > MAX_PREVIEW_BYTES / 4 ||
			   payload_offset + count * 4 != preview_end)
			{
				error = "Session file preview is invalid";
				return false;
			}
			state.preview_width = width;
			state.preview_height = height;
			state.preview_pixels.resize(count);
			for(std::size_t i = 0; i < count; ++i)
			{
				const std::uint8_t r = data[payload_offset++];
				const std::uint8_t g = data[payload_offset++];
				const std::uint8_t b = data[payload_offset++];
				const std::uint8_t a = data[payload_offset++];
				state.preview_pixels[i] =
					(static_cast<std::uint32_t>(r) << 24) |
					(static_cast<std::uint32_t>(g) << 16) |
					(static_cast<std::uint32_t>(b) << 8) |
					static_cast<std::uint32_t>(a);
			}
		}
		if(payload_offset != preview_end)
		{
			error = "Session file preview is invalid";
			return false;
		}
	}
	for(std::vector<qtreGB::u8>& blob : state.states)
	{
		std::uint32_t size = 0;
		if(!read_u32(data, payload_offset, size) || size > MAX_STATE_BYTES ||
		   payload_offset + size > data.size())
		{
			error = "Session file state data is invalid";
			return false;
		}
		blob.assign(data.begin() + static_cast<std::ptrdiff_t>(payload_offset),
			data.begin() + static_cast<std::ptrdiff_t>(payload_offset + size));
		payload_offset += size;
	}
	if(payload_offset != data.size())
	{
		error = "Session file has trailing data";
		return false;
	}
	if((flags & FLAG_LINK_CONNECTED) == 0 || state.states[0].empty() ||
	   state.states[1].empty())
	{
		error = "Session file does not contain a connected session";
		return false;
	}
	return true;
}
