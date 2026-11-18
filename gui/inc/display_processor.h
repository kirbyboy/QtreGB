#ifndef QTREGB_DISPLAY_PROCESSOR_H
#define QTREGB_DISPLAY_PROCESSOR_H

#include <array>
#include <vector>

#include "display_settings.h"
#include "types.h"

// Converts the native 160x144 framebuffer (palette indices 0..3) into RGBA
// pixels, applying the palette and the optional per-pixel ghosting. LCD grid
// and window scaling are presentation concerns and stay outside this class.
class DisplayProcessor
{
	public:
		DisplayProcessor();

		void reset();
		void set_settings(const DisplaySettings& settings);
		const DisplaySettings& settings() const { return m_settings; }

		// New emulated frame. `seconds` is the elapsed emulated time since the
		// previous frame; ghosting is advanced by that amount.
		const std::vector<u32>& process(const std::vector<u8>& native,
			double seconds);

		// Re-render the last native frame with the current settings without
		// advancing ghosting (palette changes while paused).
		const std::vector<u32>& refresh();

		const std::vector<u32>& pixels() const { return m_pixels; }

	private:
		DisplaySettings m_settings;
		std::vector<u8> m_last_native;
		std::vector<u32> m_pixels;
		std::vector<float> m_levels;
		std::vector<float> m_origins;
		std::vector<float> m_elapsed_ms;
		std::vector<float> m_durations_ms;
		std::vector<u8> m_targets;
		std::array<Rgb, 4> m_palette {};
		bool m_initialized = false;

		void update_palette();
		void update_pixels();
		void advance_levels(double seconds);
};

#endif
