#ifndef QTREGB_PNG_WRITER_H
#define QTREGB_PNG_WRITER_H

#include <cstddef>
#include <cstdint>
#include <vector>

// Encodes a top-down 0xRRGGBBAA pixel buffer as a PNG image.
bool encode_png_rgba(int width, int height,
	const std::vector<std::uint32_t>& pixels,
	std::vector<std::uint8_t>& out);

// If data starts with a valid PNG stream, returns the offset just after its
// IEND chunk. The caller can use that offset to find appended payload data.
bool png_payload_offset(const std::vector<std::uint8_t>& data,
	std::size_t& offset);

#endif
