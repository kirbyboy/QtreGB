#include "display_settings.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace
{

constexpr std::array<Rgb, 4> DMG_COLORS = {{
	{0xE0, 0xF8, 0xD0},
	{0x88, 0xC0, 0x70},
	{0x34, 0x68, 0x56},
	{0x08, 0x18, 0x20},
}};

constexpr std::array<Rgb, 4> POCKET_COLORS = {{
	{0xFF, 0xFF, 0xFF},
	{0xAA, 0xAA, 0xAA},
	{0x55, 0x55, 0x55},
	{0x00, 0x00, 0x00},
}};

bool hex_nibble(char character, u8& value)
{
	if(character >= '0' && character <= '9')
	{
		value = static_cast<u8>(character - '0');
		return true;
	}
	if(character >= 'a' && character <= 'f')
	{
		value = static_cast<u8>(character - 'a' + 10);
		return true;
	}
	if(character >= 'A' && character <= 'F')
	{
		value = static_cast<u8>(character - 'A' + 10);
		return true;
	}
	return false;
}

bool parse_byte(const std::string& text, size_t offset, u8& value)
{
	u8 high = 0;
	u8 low = 0;
	if(offset + 1 >= text.size() ||
	   !hex_nibble(text[offset], high) ||
	   !hex_nibble(text[offset + 1], low))
	{
		return false;
	}
	value = static_cast<u8>((high << 4) | low);
	return true;
}

std::string lowercase(std::string text)
{
	std::transform(text.begin(), text.end(), text.begin(),
		[](unsigned char character)
		{
			return static_cast<char>(std::tolower(character));
		});
	return text;
}

} // namespace

std::array<Rgb, 4> DisplaySettings::colors() const
{
	switch(palette)
	{
		case PaletteMode::Dmg:
			return DMG_COLORS;
		case PaletteMode::Pocket:
			return POCKET_COLORS;
		case PaletteMode::Custom:
			return custom_palette;
	}
	return DMG_COLORS;
}

std::array<u32, 4> DisplaySettings::rgba_colors() const
{
	const std::array<Rgb, 4> palette_colors = colors();
	std::array<u32, 4> result {};
	for(size_t i = 0; i < result.size(); ++i)
	{
		result[i] = to_rgba(palette_colors[i]);
	}
	return result;
}

const char* DisplaySettings::palette_name(PaletteMode mode)
{
	switch(mode)
	{
		case PaletteMode::Dmg:
			return "dmg";
		case PaletteMode::Pocket:
			return "pocket";
		case PaletteMode::Custom:
			return "custom";
	}
	return "dmg";
}

bool DisplaySettings::parse_palette(const std::string& text, PaletteMode& mode)
{
	const std::string value = lowercase(text);
	if(value == "dmg")
	{
		mode = PaletteMode::Dmg;
		return true;
	}
	if(value == "pocket" || value == "grayscale" || value == "grey")
	{
		mode = PaletteMode::Pocket;
		return true;
	}
	if(value == "custom")
	{
		mode = PaletteMode::Custom;
		return true;
	}
	return false;
}

const char* DisplaySettings::present_mode_name(PresentMode mode)
{
	switch(mode)
	{
		case PresentMode::Auto:
			return "auto";
		case PresentMode::Monitor:
			return "monitor";
		case PresentMode::GameBoy:
			return "gameboy";
	}
	return "auto";
}

bool DisplaySettings::parse_present_mode(const std::string& text, PresentMode& mode)
{
	const std::string value = lowercase(text);
	if(value == "auto")
	{
		mode = PresentMode::Auto;
		return true;
	}
	if(value == "monitor")
	{
		mode = PresentMode::Monitor;
		return true;
	}
	if(value == "gameboy" || value == "game-boy" || value == "gb")
	{
		mode = PresentMode::GameBoy;
		return true;
	}
	return false;
}

u32 DisplaySettings::to_rgba(Rgb color)
{
	// SDL_PIXELFORMAT_RGBA8888 uses an R,G,B,A byte layout in memory. On a
	// little-endian host that means the alpha byte is the low byte, matching
	// the existing QtreGB palette constants (0xRRGGBBAA).
	return (static_cast<u32>(color.r) << 24) |
		(static_cast<u32>(color.g) << 16) |
		(static_cast<u32>(color.b) << 8) |
		0xFFu;
}

std::string DisplaySettings::to_hex(Rgb color)
{
	char buffer[8] {};
	std::snprintf(buffer, sizeof(buffer), "#%02X%02X%02X",
		static_cast<unsigned>(color.r),
		static_cast<unsigned>(color.g),
		static_cast<unsigned>(color.b));
	return buffer;
}

bool DisplaySettings::parse_hex(const std::string& text, Rgb& color)
{
	std::string value = text;
	if(!value.empty() && value.front() == '#')
	{
		value.erase(value.begin());
	}
	if(value.size() != 6 && value.size() != 8)
	{
		return false;
	}

	Rgb parsed;
	if(!parse_byte(value, 0, parsed.r) ||
	   !parse_byte(value, 2, parsed.g) ||
	   !parse_byte(value, 4, parsed.b))
	{
		return false;
	}

	// A trailing alpha byte is accepted but deliberately ignored: QtreGB
	// palettes are always opaque.
	color = parsed;
	return true;
}
