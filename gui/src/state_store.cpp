#include "state_store.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string_view>

#include "build_info.h"
#include "png_writer.h"
#include "state_stream.h"

namespace
{

constexpr std::string_view MAGIC = "QTGBSTF1";
constexpr std::string_view CORE_MAGIC = "QTGBST01";
constexpr std::uint16_t CONTAINER_VERSION = 1;
constexpr std::size_t HEADER_SIZE = 84;
constexpr std::size_t MAX_STRING_BYTES = 1u << 20;
constexpr std::size_t MAX_TITLE_BYTES = 64;
constexpr std::size_t MAX_FILENAME_BYTES = 1024;
constexpr std::size_t MAX_VERSION_BYTES = 256;
constexpr std::size_t MAX_NOTE_BYTES = 4096;
constexpr std::uint32_t MAX_PREVIEW_DIMENSION = 4096;
constexpr std::size_t MAX_PREVIEW_BYTES = 64u << 20;
constexpr std::size_t MAX_CORE_BYTES = 64u << 20;
constexpr std::size_t MAX_PAYLOAD_BYTES = 128u << 20;
constexpr std::size_t MAX_FILE_BYTES = 192u << 20;

void put_u8(std::vector<std::uint8_t>& out, std::uint8_t value)
{
	out.push_back(value);
}

void put_u16(std::vector<std::uint8_t>& out, std::uint16_t value)
{
	out.push_back(static_cast<std::uint8_t>(value & 0xFF));
	out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
}

void put_string(std::vector<std::uint8_t>& out, const std::string& value,
	std::size_t limit)
{
	const std::size_t size = std::min({value.size(), limit, MAX_STRING_BYTES});
	put_u16(out, static_cast<std::uint16_t>(size));
	out.insert(out.end(), value.begin(),
		value.begin() + static_cast<std::ptrdiff_t>(size));
}

void set_u16(std::vector<std::uint8_t>& out, std::size_t offset,
	std::uint16_t value)
{
	out[offset + 0] = static_cast<std::uint8_t>(value & 0xFF);
	out[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
}

void set_u32(std::vector<std::uint8_t>& out, std::size_t offset,
	std::uint32_t value)
{
	for(int i = 0; i < 4; ++i)
	{
		out[offset + static_cast<std::size_t>(i)] =
			static_cast<std::uint8_t>((value >> (i * 8)) & 0xFF);
	}
}

void set_u64(std::vector<std::uint8_t>& out, std::size_t offset,
	std::uint64_t value)
{
	for(int i = 0; i < 8; ++i)
	{
		out[offset + static_cast<std::size_t>(i)] =
			static_cast<std::uint8_t>((value >> (i * 8)) & 0xFF);
	}
}

std::uint16_t get_u16(const std::vector<std::uint8_t>& data,
	std::size_t offset)
{
	return static_cast<std::uint16_t>(data[offset]) |
		(static_cast<std::uint16_t>(data[offset + 1]) << 8);
}

std::uint32_t get_u32(const std::vector<std::uint8_t>& data,
	std::size_t offset)
{
	std::uint32_t value = 0;
	for(int i = 0; i < 4; ++i)
	{
		value |= static_cast<std::uint32_t>(
			data[offset + static_cast<std::size_t>(i)]) << (i * 8);
	}
	return value;
}

std::uint64_t get_u64(const std::vector<std::uint8_t>& data,
	std::size_t offset)
{
	std::uint64_t value = 0;
	for(int i = 0; i < 8; ++i)
	{
		value |= static_cast<std::uint64_t>(
			data[offset + static_cast<std::size_t>(i)]) << (i * 8);
	}
	return value;
}

bool read_string(const std::vector<std::uint8_t>& data, std::size_t& offset,
	std::size_t end, std::size_t limit, std::string& value)
{
	if(offset + 2 > end)
	{
		return false;
	}
	const std::size_t size = get_u16(data, offset);
	offset += 2;
	if(size > limit || size > MAX_STRING_BYTES || offset + size > end)
	{
		return false;
	}
	value.assign(reinterpret_cast<const char*>(data.data() + offset), size);
	offset += size;
	return true;
}

bool write_atomic(const std::filesystem::path& file_path,
	const std::vector<std::uint8_t>& data)
{
	if(file_path.has_parent_path())
	{
		std::error_code directory_error;
		std::filesystem::create_directories(file_path.parent_path(),
			directory_error);
		if(directory_error)
		{
			return false;
		}
	}

	std::filesystem::path temporary = file_path;
	temporary += ".tmp";
	std::ofstream out(temporary, std::ios::out | std::ios::binary |
		std::ios::trunc);
	if(!out)
	{
		return false;
	}
	out.write(reinterpret_cast<const char*>(data.data()),
		static_cast<std::streamsize>(data.size()));
	out.flush();
	if(!out)
	{
		out.close();
		std::error_code remove_error;
		std::filesystem::remove(temporary, remove_error);
		return false;
	}
	out.close();

	std::error_code rename_error;
	std::filesystem::rename(temporary, file_path, rename_error);
	if(rename_error)
	{
		std::filesystem::remove(temporary, rename_error);
		return false;
	}
	return true;
}

} // namespace

void StateStore::set_rom_path(const std::string& rom_path)
{
	m_rom_path = rom_path;
}

void StateStore::set_path_settings(const PathSettings& settings)
{
	m_path_settings = settings;
}

void StateStore::set_slot(int slot)
{
	m_slot = std::clamp(slot, 0, 9);
}

std::string StateStore::path() const
{
	return PathSettings::state_path(m_path_settings, m_rom_path, m_slot);
}

std::string StateStore::legacy_path() const
{
	std::string result = path();
	if(result.size() >= 4 &&
	   result.compare(result.size() - 4, 4, ".png") == 0)
	{
		result.resize(result.size() - 4);
	}
	return result;
}

bool StateStore::save(const SavestateInfo& info,
	const std::vector<std::uint8_t>& data) const
{
	const std::string state_path = path();
	if(state_path.empty() || data.empty() || data.size() > MAX_CORE_BYTES)
	{
		return false;
	}

	std::vector<std::uint8_t> metadata;
	put_string(metadata, info.rom_title, MAX_TITLE_BYTES);
	put_string(metadata, info.rom_filename, MAX_FILENAME_BYTES);
	put_string(metadata, info.app_version, MAX_VERSION_BYTES);
	put_string(metadata, info.note, MAX_NOTE_BYTES);
	put_u8(metadata, info.mapper);
	put_u8(metadata, info.cgb_flag);
	put_u8(metadata, info.cart_type);
	put_u8(metadata, info.header_checksum);
	put_u16(metadata, info.global_checksum);

	std::vector<std::uint8_t> preview;
	const std::size_t preview_pixels = static_cast<std::size_t>(
		info.preview_width) * static_cast<std::size_t>(info.preview_height);
	if(!info.preview_pixels.empty())
	{
		if(info.preview_width == 0 || info.preview_height == 0 ||
		   info.preview_width > MAX_PREVIEW_DIMENSION ||
		   info.preview_height > MAX_PREVIEW_DIMENSION ||
		   info.preview_pixels.size() != preview_pixels ||
		   preview_pixels > MAX_PREVIEW_BYTES / 4)
		{
			return false;
		}
		put_u16(preview, info.preview_width);
		put_u16(preview, info.preview_height);
		put_u8(preview, 0);	// RGBA8888
		put_u8(preview, 0);
		put_u8(preview, 0);
		put_u8(preview, 0);
		for(const std::uint32_t pixel : info.preview_pixels)
		{
			put_u8(preview, static_cast<std::uint8_t>((pixel >> 24) & 0xFF));
			put_u8(preview, static_cast<std::uint8_t>((pixel >> 16) & 0xFF));
			put_u8(preview, static_cast<std::uint8_t>((pixel >> 8) & 0xFF));
			put_u8(preview, static_cast<std::uint8_t>(pixel & 0xFF));
		}
	}

	const std::size_t payload_size = metadata.size() + preview.size() +
		data.size();
	if(payload_size > MAX_PAYLOAD_BYTES ||
	   metadata.size() > UINT32_MAX || preview.size() > UINT32_MAX ||
	   data.size() > UINT32_MAX)
	{
		return false;
	}

	std::vector<std::uint8_t> payload;
	payload.reserve(payload_size);
	payload.insert(payload.end(), metadata.begin(), metadata.end());
	payload.insert(payload.end(), preview.begin(), preview.end());
	payload.insert(payload.end(), data.begin(), data.end());

	std::uint8_t flags = 0;
	if(!preview.empty())
	{
		flags |= 0x01;	// has preview
	}
	if(info.bios_size != 0)
	{
		flags |= 0x02;	// BIOS configured
	}
	if(info.bios_active)
	{
		flags |= 0x04;	// boot ROM mapped
	}

	std::vector<std::uint8_t> header(HEADER_SIZE, 0);
	std::memcpy(header.data(), MAGIC.data(), MAGIC.size());
	set_u16(header, 8, CONTAINER_VERSION);
	set_u16(header, 10, static_cast<std::uint16_t>(HEADER_SIZE));
	set_u16(header, 12, qtreGB::STATE_FORMAT_VERSION);
	set_u16(header, 14, qtreGB::STATE_SCHEMA_VERSION);
	set_u64(header, 16, info.created_unix_seconds);
	set_u64(header, 24, info.emulated_frames);
	set_u32(header, 32, static_cast<std::uint32_t>(std::clamp(info.slot, 0, 9)));
	header[36] = info.kind <= SavestateInfo::KindAuto ? info.kind : 0;
	header[37] = flags;
	set_u32(header, 40, info.rom_size);
	set_u64(header, 44, info.rom_hash);
	set_u32(header, 52, info.bios_size);
	set_u64(header, 56, info.bios_hash);
	set_u32(header, 64, static_cast<std::uint32_t>(metadata.size()));
	set_u32(header, 68, static_cast<std::uint32_t>(preview.size()));
	set_u32(header, 72, static_cast<std::uint32_t>(data.size()));
	set_u32(header, 76, qtreGB::state_crc32(payload.data(), payload.size()));
	set_u32(header, 80, qtreGB::state_crc32(header.data(), 80));

	std::vector<std::uint8_t> container;
	container.reserve(header.size() + payload.size());
	container.insert(container.end(), header.begin(), header.end());
	container.insert(container.end(), payload.begin(), payload.end());

	std::vector<std::uint8_t> preview_png;
	if(!info.preview_pixels.empty())
	{
		if(!encode_png_rgba(info.preview_width, info.preview_height,
			info.preview_pixels, preview_png))
		{
			return false;
		}
	}
	else
	{
		const std::vector<std::uint32_t> placeholder(
			static_cast<std::size_t>(160) * 144, 0x202020FFu);
		if(!encode_png_rgba(160, 144, placeholder, preview_png))
		{
			return false;
		}
	}
	preview_png.insert(preview_png.end(), container.begin(),
		container.end());
	return write_atomic(std::filesystem::u8path(state_path), preview_png);
}

bool StateStore::load(std::vector<std::uint8_t>& data,
	SavestateInfo* info) const
{
	if(info != nullptr)
	{
		*info = SavestateInfo {};
	}

	std::string state_path = path();
	if(state_path.empty())
	{
		return false;
	}
	std::error_code file_error;
	if(!std::filesystem::is_regular_file(
		   std::filesystem::u8path(state_path), file_error))
	{
		state_path = legacy_path();
	}

	std::ifstream in(std::filesystem::u8path(state_path),
		std::ios::binary);
	if(!in)
	{
		return false;
	}
	in.seekg(0, std::ios::end);
	const std::streamoff file_size = in.tellg();
	if(file_size <= 0 ||
	   static_cast<std::uint64_t>(file_size) > MAX_FILE_BYTES)
	{
		return false;
	}
	in.seekg(0, std::ios::beg);
	std::vector<std::uint8_t> file(
		static_cast<std::size_t>(file_size));
	in.read(reinterpret_cast<char*>(file.data()), file_size);
	if(!in.good() && !in.eof())
	{
		return false;
	}

	// New files are a PNG screenshot followed by the container bytes.
	std::size_t container_offset = 0;
	if(png_payload_offset(file, container_offset))
	{
		file.erase(file.begin(), file.begin() +
			static_cast<std::ptrdiff_t>(container_offset));
	}

	// Legacy .srN files are a bare core blob.
	if(file.size() >= CORE_MAGIC.size() &&
	   std::memcmp(file.data(), CORE_MAGIC.data(), CORE_MAGIC.size()) == 0)
	{
		data = std::move(file);
		return true;
	}

	if(file.size() < HEADER_SIZE ||
	   std::memcmp(file.data(), MAGIC.data(), MAGIC.size()) != 0)
	{
		return false;
	}
	if(get_u16(file, 8) != CONTAINER_VERSION ||
	   get_u16(file, 10) != HEADER_SIZE ||
	   get_u16(file, 12) != qtreGB::STATE_FORMAT_VERSION ||
	   get_u16(file, 14) != qtreGB::STATE_SCHEMA_VERSION)
	{
		return false;
	}
	if(get_u32(file, 80) != qtreGB::state_crc32(file.data(), 80))
	{
		return false;
	}

	const std::uint32_t metadata_size = get_u32(file, 64);
	const std::uint32_t preview_size = get_u32(file, 68);
	const std::uint32_t core_size = get_u32(file, 72);
	if(core_size > MAX_CORE_BYTES)
	{
		return false;
	}
	const std::uint64_t payload_size =
		static_cast<std::uint64_t>(metadata_size) + preview_size + core_size;
	if(payload_size > MAX_PAYLOAD_BYTES ||
	   HEADER_SIZE + payload_size != file.size())
	{
		return false;
	}
	const std::uint8_t* payload = file.data() + HEADER_SIZE;
	if(get_u32(file, 76) != qtreGB::state_crc32(payload,
		static_cast<std::size_t>(payload_size)))
	{
		return false;
	}

	SavestateInfo parsed;
	parsed.created_unix_seconds = get_u64(file, 16);
	parsed.emulated_frames = get_u64(file, 24);
	parsed.slot = static_cast<int>(get_u32(file, 32));
	if(parsed.slot < 0 || parsed.slot > 9)
	{
		return false;
	}
	parsed.kind = file[36];
	if(parsed.kind > SavestateInfo::KindAuto)
	{
		return false;
	}
	const std::uint8_t flags = file[37];
	parsed.rom_size = get_u32(file, 40);
	parsed.rom_hash = get_u64(file, 44);
	parsed.bios_size = get_u32(file, 52);
	parsed.bios_hash = get_u64(file, 56);
	parsed.bios_active = (flags & 0x04) != 0;

	std::size_t offset = HEADER_SIZE;
	const std::size_t metadata_end = offset + metadata_size;
	if(metadata_end > file.size() ||
	   !read_string(file, offset, metadata_end, MAX_TITLE_BYTES,
		   parsed.rom_title) ||
	   !read_string(file, offset, metadata_end, MAX_FILENAME_BYTES,
		   parsed.rom_filename) ||
	   !read_string(file, offset, metadata_end, MAX_VERSION_BYTES,
		   parsed.app_version) ||
	   !read_string(file, offset, metadata_end, MAX_NOTE_BYTES,
		   parsed.note))
	{
		return false;
	}
	if(offset + 6 > metadata_end)
	{
		return false;
	}
	parsed.mapper = file[offset++];
	parsed.cgb_flag = file[offset++];
	parsed.cart_type = file[offset++];
	parsed.header_checksum = file[offset++];
	parsed.global_checksum = get_u16(file, offset);
	offset += 2;
	if(offset != metadata_end)
	{
		return false;
	}

	const std::size_t preview_end = offset + preview_size;
	if(preview_end > file.size())
	{
		return false;
	}
	if(preview_size != 0)
	{
		if(preview_size < 8 || offset + 8 > preview_end)
		{
			return false;
		}
		parsed.preview_width = get_u16(file, offset);
		parsed.preview_height = get_u16(file, offset + 2);
		const std::uint8_t format = file[offset + 4];
		offset += 8;
		if(format != 0 || parsed.preview_width == 0 ||
		   parsed.preview_height == 0 ||
		   parsed.preview_width > MAX_PREVIEW_DIMENSION ||
		   parsed.preview_height > MAX_PREVIEW_DIMENSION)
		{
			return false;
		}
		const std::size_t count = static_cast<std::size_t>(
			parsed.preview_width) * static_cast<std::size_t>(
			parsed.preview_height);
		if(count > MAX_PREVIEW_BYTES / 4 ||
		   offset + count * 4 != preview_end)
		{
			return false;
		}
		parsed.preview_pixels.resize(count);
		for(std::size_t i = 0; i < count; ++i)
		{
			const std::uint8_t r = file[offset++];
			const std::uint8_t g = file[offset++];
			const std::uint8_t b = file[offset++];
			const std::uint8_t a = file[offset++];
			parsed.preview_pixels[i] =
				(static_cast<std::uint32_t>(r) << 24) |
				(static_cast<std::uint32_t>(g) << 16) |
				(static_cast<std::uint32_t>(b) << 8) |
				static_cast<std::uint32_t>(a);
		}
	}
	else if(flags & 0x01)
	{
		return false;
	}
	if(offset != preview_end || offset + core_size != file.size())
	{
		return false;
	}

	data.assign(file.begin() + static_cast<std::ptrdiff_t>(offset),
		file.end());
	if(info != nullptr)
	{
		*info = std::move(parsed);
	}
	return true;
}
