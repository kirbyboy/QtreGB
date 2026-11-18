#include "doctest.h"
#include "display_processor.h"

#include <vector>

TEST_CASE("DisplayProcessor: palette colours are opaque")
{
	std::vector<u8> native(160 * 144, 0);
	DisplaySettings settings;
	settings.palette = PaletteMode::Dmg;

	DisplayProcessor processor;
	processor.set_settings(settings);
	const std::vector<u32>& pixels = processor.process(native, 0.0);
	REQUIRE(pixels.size() == native.size());
	CHECK(pixels[0] == 0xE0F8D0FFu);

	settings.palette = PaletteMode::Pocket;
	processor.set_settings(settings);
	processor.refresh();
	CHECK(processor.pixels()[0] == 0xFFFFFFFFu);

	settings.palette = PaletteMode::Custom;
	settings.custom_palette[0] = {0x12, 0x34, 0x56};
	processor.set_settings(settings);
	processor.refresh();
	CHECK(processor.pixels()[0] == 0x123456FFu);
}

TEST_CASE("DisplayProcessor: ghosting moves toward the target level")
{
	std::vector<u8> native(160 * 144, 0);
	DisplaySettings settings;
	settings.palette = PaletteMode::Dmg;
	settings.ghosting = true;
	settings.ghosting_light_ms = 80;
	settings.ghosting_dark_ms = 80;

	DisplayProcessor processor;
	processor.set_settings(settings);
	processor.process(native, 0.0);
	CHECK(processor.pixels()[0] == 0xE0F8D0FFu);

	native.assign(native.size(), 1);
	processor.process(native, 0.04);
	// Half of 80 ms, ease-out cubic (t=0.5, eased=0.875).
	CHECK(processor.pixels()[0] == 0x93C77CFFu);

	native.assign(native.size(), 3);
	processor.process(native, 0.1);
	// High-contrast darkening is driven immediately (drive coherence).
	CHECK(processor.pixels()[0] == 0x081820FFu);

	native.assign(native.size(), 0);
	processor.process(native, 1.0);
	CHECK(processor.pixels()[0] == 0xE0F8D0FFu);
}
