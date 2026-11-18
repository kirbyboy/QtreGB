#ifndef QTREGB_DISPLAY_SETTINGS_H
#define QTREGB_DISPLAY_SETTINGS_H

#include <array>
#include <cstdint>
#include <string>

#include "types.h"

using qtreGB::u8;
using qtreGB::u32;

enum class PaletteMode
{
	Dmg,
	Pocket,
	Custom,
};

enum class PresentMode
{
	Auto,
	Monitor,
	GameBoy,
};

struct Rgb
{
	u8 r = 0;
	u8 g = 0;
	u8 b = 0;

	bool operator==(const Rgb& other) const
	{
		return r == other.r && g == other.g && b == other.b;
	}
	bool operator!=(const Rgb& other) const { return !(*this == other); }
};

struct DisplaySettings
{
	PaletteMode palette = PaletteMode::Dmg;
	std::array<Rgb, 4> custom_palette = {{
		{0xE0, 0xF8, 0xD0},
		{0x88, 0xC0, 0x70},
		{0x34, 0x68, 0x56},
		{0x08, 0x18, 0x20},
	}};
	Rgb background {0, 0, 0};
	bool integer_scaling = true;
	bool lcd_grid = false;
	int grid_opacity = 100;
	PresentMode present_mode = PresentMode::Auto;
	static constexpr int kMaxGhostingMs = 150;

	bool ghosting = false;
	int ghosting_light_ms = 32;
	int ghosting_dark_ms = 16;

	// The four colours actually used, always in light-to-dark order.
	std::array<Rgb, 4> colors() const;
	std::array<u32, 4> rgba_colors() const;

	static const char* palette_name(PaletteMode mode);
	static bool parse_palette(const std::string& text, PaletteMode& mode);
	static const char* present_mode_name(PresentMode mode);
	static bool parse_present_mode(const std::string& text, PresentMode& mode);
	static u32 to_rgba(Rgb color);
	static std::string to_hex(Rgb color);
	static bool parse_hex(const std::string& text, Rgb& color);
};

#endif
