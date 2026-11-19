#include "png_writer.h"

#include <cstdlib>
#include <cstring>

#define STBIW_MALLOC(size) std::malloc(size)
#define STBIW_REALLOC(pointer, size) std::realloc(pointer, size)
#define STBIW_FREE(pointer) std::free(pointer)

// stb_image_write is third-party code; keep the project's warning flags for
// our code while not flooding the build with its initializer warnings.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace
{

constexpr std::uint8_t PNG_SIGNATURE[8] = {
	0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A
};

std::uint32_t read_be32(const std::uint8_t* data)
{
	return (static_cast<std::uint32_t>(data[0]) << 24) |
		(static_cast<std::uint32_t>(data[1]) << 16) |
		(static_cast<std::uint32_t>(data[2]) << 8) |
		static_cast<std::uint32_t>(data[3]);
}

} // namespace

bool encode_png_rgba(int width, int height,
	const std::vector<std::uint32_t>& pixels,
	std::vector<std::uint8_t>& out)
{
	out.clear();
	if(width <= 0 || height <= 0 ||
	   pixels.size() != static_cast<std::size_t>(width) *
		   static_cast<std::size_t>(height))
	{
		return false;
	}

	std::vector<std::uint8_t> rgba(pixels.size() * 4);
	for(std::size_t i = 0; i < pixels.size(); ++i)
	{
		const std::uint32_t pixel = pixels[i];
		rgba[i * 4 + 0] = static_cast<std::uint8_t>((pixel >> 24) & 0xFF);
		rgba[i * 4 + 1] = static_cast<std::uint8_t>((pixel >> 16) & 0xFF);
		rgba[i * 4 + 2] = static_cast<std::uint8_t>((pixel >> 8) & 0xFF);
		rgba[i * 4 + 3] = static_cast<std::uint8_t>(pixel & 0xFF);
	}

	int encoded_size = 0;
	unsigned char* encoded = stbi_write_png_to_mem(rgba.data(), width * 4,
		width, height, 4, &encoded_size);
	if(encoded == nullptr || encoded_size <= 0)
	{
		std::free(encoded);
		return false;
	}
	out.assign(encoded, encoded + encoded_size);
	std::free(encoded);
	return true;
}

bool png_payload_offset(const std::vector<std::uint8_t>& data,
	std::size_t& offset)
{
	offset = 0;
	if(data.size() < sizeof(PNG_SIGNATURE) ||
	   std::memcmp(data.data(), PNG_SIGNATURE, sizeof(PNG_SIGNATURE)) != 0)
	{
		return false;
	}

	std::size_t position = sizeof(PNG_SIGNATURE);
	while(position + 12 <= data.size())
	{
		const std::uint32_t length = read_be32(data.data() + position);
		if(length > data.size() - position - 12)
		{
			return false;
		}
		const bool is_iend =
			std::memcmp(data.data() + position + 4, "IEND", 4) == 0;
		position += 12 + length;
		if(is_iend)
		{
			offset = position;
			return true;
		}
	}
	return false;
}
