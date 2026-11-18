#ifndef QTREGB_APP_CONFIG_H
#define QTREGB_APP_CONFIG_H

#include <string>

#include "audio_settings.h"
#include "display_settings.h"
#include "input_settings.h"

// Small, human-readable application configuration stored outside the
// installation directory. The format is INI-like:
//
//   [window]
//   width=800
//   height=600
//   maximized=0
//
// Per-user path is Qtre/QtreGB/config.ini: XDG_CONFIG_HOME on Linux and
// SDL_GetPrefPath("Qtre", "QtreGB") on Windows/macOS. QTR_CONFIG can override
// the whole file path, and a QtreGB.ini next to the executable enables
// portable mode.
class AppConfig
{
	public:
		static constexpr int kDefaultWindowWidth = 800;
		static constexpr int kDefaultWindowHeight = 600;
		static constexpr int kMinWindowWidth = 160;
		static constexpr int kMinWindowHeight = 144;
		static constexpr int kMaxWindowSize = 16384;

		// Missing or unreadable files produce the defaults. Unknown keys,
		// malformed values and comments are ignored.
		static AppConfig load(const std::string& path);
		static AppConfig load_default();
		static std::string default_path();

		// Writes atomically through a temporary file in the same directory.
		bool save(const std::string& path, std::string& error) const;
		bool save_default(std::string& error) const;

		int window_width = kDefaultWindowWidth;
		int window_height = kDefaultWindowHeight;
		bool window_maximized = false;
		DisplaySettings display;
		AudioSettings audio;
		InputSettings input = InputSettings::defaults();
};

#endif
