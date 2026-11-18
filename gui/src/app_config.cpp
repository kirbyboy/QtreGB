#include "app_config.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace
{

std::string trim(const std::string& text)
{
	const size_t first = text.find_first_not_of(" \t\r\n");
	if(first == std::string::npos)
	{
		return {};
	}
	const size_t last = text.find_last_not_of(" \t\r\n");
	return text.substr(first, last - first + 1);
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

bool parse_int(const std::string& text, int& value)
{
	const char* begin = text.data();
	const char* end = begin + text.size();
	const auto result = std::from_chars(begin, end, value);
	return result.ec == std::errc() && result.ptr == end;
}

bool parse_bool(const std::string& text, bool& value)
{
	const std::string normalized = lowercase(trim(text));
	if(normalized == "1" || normalized == "true" ||
	   normalized == "yes" || normalized == "on")
	{
		value = true;
		return true;
	}
	if(normalized == "0" || normalized == "false" ||
	   normalized == "no" || normalized == "off")
	{
		value = false;
		return true;
	}
	return false;
}

constexpr const char* USER_CONFIG_FILE = "config.ini";
constexpr const char* PORTABLE_CONFIG_FILE = "QtreGB.ini";

std::string executable_config_path()
{
	char* base = SDL_GetBasePath();
	if(base == nullptr)
	{
		return {};
	}

	const std::filesystem::path candidate =
		std::filesystem::u8path(base) / PORTABLE_CONFIG_FILE;
	SDL_free(base);

	std::error_code error;
	if(std::filesystem::is_regular_file(candidate, error))
	{
		return candidate.u8string();
	}
	return {};
}

std::string user_config_path()
{
#if defined(_WIN32) || defined(__APPLE__)
	char* pref = SDL_GetPrefPath("Qtre", "QtreGB");
	if(pref != nullptr)
	{
		const std::filesystem::path path =
			std::filesystem::u8path(pref) / USER_CONFIG_FILE;
		SDL_free(pref);
		return path.u8string();
	}
#else
	if(const char* config_home = std::getenv("XDG_CONFIG_HOME"))
	{
		if(config_home[0] != '\0')
		{
			const std::filesystem::path base =
				std::filesystem::u8path(config_home);
			if(base.is_absolute())
			{
				return (base / "Qtre" / "QtreGB" /
					USER_CONFIG_FILE).u8string();
			}
		}
	}

	if(const char* home = std::getenv("HOME"))
	{
		if(home[0] != '\0')
		{
			const std::filesystem::path base =
				std::filesystem::u8path(home);
			if(base.is_absolute())
			{
				return (base / ".config" / "Qtre" / "QtreGB" /
					USER_CONFIG_FILE).u8string();
			}
		}
	}

	// Last-resort platform path, still under the Qtre/QtreGB namespace.
	char* pref = SDL_GetPrefPath("Qtre", "QtreGB");
	if(pref != nullptr)
	{
		const std::filesystem::path path =
			std::filesystem::u8path(pref) / USER_CONFIG_FILE;
		SDL_free(pref);
		return path.u8string();
	}
#endif
	return {};
}

} // namespace

AppConfig AppConfig::load(const std::string& path)
{
	AppConfig config;
	if(path.empty())
	{
		return config;
	}

	std::ifstream input(std::filesystem::u8path(path));
	if(!input)
	{
		return config;
	}

	std::string section;
	std::string line;
	while(std::getline(input, line))
	{
		if(!line.empty() && line.back() == '\r')
		{
			line.pop_back();
		}

		const std::string normalized = trim(line);
		if(normalized.empty() || normalized.front() == '#' ||
		   normalized.front() == ';')
		{
			continue;
		}

		if(normalized.front() == '[' && normalized.back() == ']')
		{
			section = lowercase(trim(normalized.substr(1,
				normalized.size() - 2)));
			continue;
		}

		const size_t equals = normalized.find('=');
		if(equals == std::string::npos)
		{
			continue;
		}

		const std::string key = lowercase(trim(normalized.substr(0, equals)));
		const std::string value = trim(normalized.substr(equals + 1));
		const std::string full_key = section.empty() ? key : section + "." + key;

		int number = 0;
		if(full_key == "window.width")
		{
			if(parse_int(value, number) &&
			   number >= kMinWindowWidth && number <= kMaxWindowSize)
			{
				config.window_width = number;
			}
		}
		else if(full_key == "window.height")
		{
			if(parse_int(value, number) &&
			   number >= kMinWindowHeight && number <= kMaxWindowSize)
			{
				config.window_height = number;
			}
		}
		else if(full_key == "window.maximized")
		{
			bool maximized = false;
			if(parse_bool(value, maximized))
			{
				config.window_maximized = maximized;
			}
		}
		else if(full_key == "graphics.palette")
		{
			PaletteMode mode = PaletteMode::Dmg;
			if(DisplaySettings::parse_palette(value, mode))
			{
				config.display.palette = mode;
			}
		}
		else if(full_key.rfind("graphics.custom_", 0) == 0 &&
			full_key.size() == 17)
		{
			const char index_character = full_key[16];
			if(index_character >= '0' && index_character <= '3')
			{
				Rgb color;
				if(DisplaySettings::parse_hex(value, color))
				{
					config.display.custom_palette[
						static_cast<size_t>(index_character - '0')] = color;
				}
			}
		}
		else if(full_key == "graphics.integer_scaling")
		{
			bool enabled = config.display.integer_scaling;
			if(parse_bool(value, enabled))
			{
				config.display.integer_scaling = enabled;
			}
		}
		else if(full_key == "graphics.background")
		{
			Rgb color;
			if(DisplaySettings::parse_hex(value, color))
			{
				config.display.background = color;
			}
		}
		else if(full_key == "graphics.lcd_grid")
		{
			bool enabled = config.display.lcd_grid;
			if(parse_bool(value, enabled))
			{
				config.display.lcd_grid = enabled;
			}
		}
		else if(full_key == "graphics.grid_opacity")
		{
			if(parse_int(value, number) && number >= 0 && number <= 100)
			{
				config.display.grid_opacity = number;
			}
		}
		else if(full_key == "graphics.present_mode")
		{
			PresentMode mode = PresentMode::Auto;
			if(DisplaySettings::parse_present_mode(value, mode))
			{
				config.display.present_mode = mode;
			}
		}
		else if(full_key == "graphics.ghosting")
		{
			bool enabled = config.display.ghosting;
			if(parse_bool(value, enabled))
			{
				config.display.ghosting = enabled;
			}
		}
		else if(full_key == "graphics.ghosting_light_ms")
		{
			if(parse_int(value, number) && number >= 0 &&
			   number <= DisplaySettings::kMaxGhostingMs)
			{
				config.display.ghosting_light_ms = number;
			}
		}
		else if(full_key == "graphics.ghosting_dark_ms")
		{
			if(parse_int(value, number) && number >= 0 &&
			   number <= DisplaySettings::kMaxGhostingMs)
			{
				config.display.ghosting_dark_ms = number;
			}
		}
		else if(full_key == "audio.enabled")
		{
			bool enabled = config.audio.enabled;
			if(parse_bool(value, enabled))
			{
				config.audio.enabled = enabled;
			}
		}
		else if(full_key == "audio.volume")
		{
			if(parse_int(value, number) && number >= 0 && number <= 100)
			{
				config.audio.volume = number;
			}
		}
		else if(full_key == "input.allow_opposite_directions")
		{
			bool enabled = config.input.allow_opposite_directions;
			if(parse_bool(value, enabled))
			{
				config.input.allow_opposite_directions = enabled;
			}
		}
		else if(full_key == "input.p1_device")
		{
			InputDeviceId device;
			if(InputSettings::parse_device(value, device))
			{
				config.input.players[0].device = device;
			}
		}
		else if(full_key == "input.p2_device")
		{
			InputDeviceId device;
			if(InputSettings::parse_device(value, device))
			{
				config.input.players[1].device = device;
			}
		}
		else if(full_key == "shortcuts.fullscreen_key")
		{
			HostControl control;
			if(InputSettings::parse_control(value, control) &&
			   (control.type == HostControlType::Key || control.is_none()))
			{
				config.input.shortcuts[0].keyboard = control;
			}
		}
		else if(full_key == "shortcuts.fullscreen_controller")
		{
			HostControl control;
			if(InputSettings::parse_control(value, control) &&
			   (control.type != HostControlType::Key || control.is_none()))
			{
				config.input.shortcuts[0].controller = control;
			}
		}
		else if(full_key == "shortcuts.save_state_key")
		{
			HostControl control;
			if(InputSettings::parse_control(value, control) &&
			   (control.type == HostControlType::Key || control.is_none()))
			{
				config.input.shortcuts[1].keyboard = control;
			}
		}
		else if(full_key == "shortcuts.save_state_controller")
		{
			HostControl control;
			if(InputSettings::parse_control(value, control) &&
			   (control.type != HostControlType::Key || control.is_none()))
			{
				config.input.shortcuts[1].controller = control;
			}
		}
		else if(full_key == "shortcuts.load_state_key")
		{
			HostControl control;
			if(InputSettings::parse_control(value, control) &&
			   (control.type == HostControlType::Key || control.is_none()))
			{
				config.input.shortcuts[2].keyboard = control;
			}
		}
		else if(full_key == "shortcuts.load_state_controller")
		{
			HostControl control;
			if(InputSettings::parse_control(value, control) &&
			   (control.type != HostControlType::Key || control.is_none()))
			{
				config.input.shortcuts[2].controller = control;
			}
		}
		else if(full_key.rfind("input.p", 0) == 0)
		{
			const size_t underscore = full_key.find('_');
			if(underscore == std::string::npos)
			{
				continue;
			}
			const std::string binding_name = full_key.substr(underscore + 1);
			static constexpr std::array<const char*, 8> names = {
				"right", "left", "up", "down", "a", "b", "start", "select"
			};
			for(size_t i = 0; i < names.size(); ++i)
			{
				if(binding_name == names[i])
				{
					HostControl control;
					if(InputSettings::parse_control(value, control))
					{
						const size_t player =
							(full_key.size() > 7 && full_key[7] == '2') ? 1 : 0;
						config.input.players[player].bindings[i] = control;
					}
					break;
				}
			}
		}
	}

	config.input.sanitize();

	return config;
}

AppConfig AppConfig::load_default()
{
	return load(default_path());
}

std::string AppConfig::default_path()
{
	if(const char* override_path = std::getenv("QTR_CONFIG"))
	{
		if(override_path[0] != '\0')
		{
			return override_path;
		}
	}

	const std::string portable = executable_config_path();
	if(!portable.empty())
	{
		return portable;
	}

	const std::string user_path = user_config_path();
	if(!user_path.empty())
	{
		return user_path;
	}

	std::error_code error;
	const std::filesystem::path current =
		std::filesystem::current_path(error);
	if(!error)
	{
		return (current / PORTABLE_CONFIG_FILE).u8string();
	}
	return PORTABLE_CONFIG_FILE;
}

bool AppConfig::save(const std::string& path, std::string& error) const
{
	error.clear();
	if(path.empty())
	{
		error = "No configuration path";
		return false;
	}

	const std::filesystem::path file_path = std::filesystem::u8path(path);
	std::error_code io_error;
	if(file_path.has_parent_path())
	{
		std::filesystem::create_directories(file_path.parent_path(), io_error);
		if(io_error)
		{
			error = "Could not create configuration directory: " +
				io_error.message();
			return false;
		}
	}

	std::filesystem::path temporary_path = file_path;
	temporary_path += ".tmp";
	std::ofstream output(temporary_path, std::ios::out | std::ios::trunc);
	if(!output)
	{
		error = "Could not write " + temporary_path.u8string();
		return false;
	}

	output << "# QtreGB configuration\n";
	output << "version=1\n\n";
	output << "[window]\n";
	output << "width=" << window_width << '\n';
	output << "height=" << window_height << '\n';
	output << "maximized=" << (window_maximized ? 1 : 0) << '\n';
	output << '\n';
	output << "[graphics]\n";
	output << "palette=" << DisplaySettings::palette_name(display.palette) << '\n';
	for(size_t i = 0; i < display.custom_palette.size(); ++i)
	{
		output << "custom_" << i << '='
			<< DisplaySettings::to_hex(display.custom_palette[i]) << '\n';
	}
	output << "background=" << DisplaySettings::to_hex(display.background) << '\n';
	output << "integer_scaling=" << (display.integer_scaling ? 1 : 0) << '\n';
	output << "lcd_grid=" << (display.lcd_grid ? 1 : 0) << '\n';
	output << "grid_opacity=" << std::clamp(display.grid_opacity, 0, 100) << '\n';
	output << "present_mode=" << DisplaySettings::present_mode_name(
		display.present_mode) << '\n';
	output << "ghosting=" << (display.ghosting ? 1 : 0) << '\n';
	output << "ghosting_light_ms="
		<< std::clamp(display.ghosting_light_ms, 0,
			DisplaySettings::kMaxGhostingMs) << '\n';
	output << "ghosting_dark_ms="
		<< std::clamp(display.ghosting_dark_ms, 0,
			DisplaySettings::kMaxGhostingMs) << '\n';
	output << '\n';
	output << "[audio]\n";
	output << "enabled=" << (audio.enabled ? 1 : 0) << '\n';
	output << "volume=" << std::clamp(audio.volume, 0, 100) << '\n';
	output << '\n';
	InputSettings clean_input = input;
	clean_input.sanitize();

	output << "[input]\n";
	output << "allow_opposite_directions="
		<< (clean_input.allow_opposite_directions ? 1 : 0) << '\n';
	for(size_t player = 0; player < clean_input.players.size(); ++player)
	{
		output << "p" << (player + 1) << "_device="
			<< InputSettings::device_token(
				clean_input.players[player].device) << '\n';
		static constexpr std::array<const char*, 8> names = {
			"right", "left", "up", "down", "a", "b", "start", "select"
		};
		for(size_t i = 0; i < names.size(); ++i)
		{
			output << "p" << (player + 1) << '_' << names[i] << '='
				<< InputSettings::control_token(
					clean_input.players[player].bindings[i]) << '\n';
		}
	}
	output << '\n';
	output << "[shortcuts]\n";
	for(size_t i = 0; i < clean_input.shortcuts.size(); ++i)
	{
		const char* name = InputSettings::shortcut_name(
			static_cast<ShortcutAction>(i));
		std::string key_name = name;
		std::transform(key_name.begin(), key_name.end(), key_name.begin(),
			[](unsigned char character)
			{
				return character == ' ' ? '_' : static_cast<char>(
					std::tolower(character));
			});
		output << key_name << "_key="
			<< InputSettings::control_token(
				clean_input.shortcuts[i].keyboard) << '\n';
		output << key_name << "_controller="
			<< InputSettings::control_token(
				clean_input.shortcuts[i].controller) << '\n';
	}
	output.flush();
	if(!output)
	{
		output.close();
		std::filesystem::remove(temporary_path, io_error);
		error = "Could not write " + temporary_path.u8string();
		return false;
	}
	output.close();

	std::filesystem::rename(temporary_path, file_path, io_error);
	if(io_error)
	{
		std::filesystem::remove(temporary_path, io_error);
		error = "Could not replace " + file_path.u8string();
		return false;
	}

	return true;
}

bool AppConfig::save_default(std::string& error) const
{
	const std::string path = default_path();
	if(path.empty())
	{
		error = "No configuration path";
		return false;
	}
	return save(path, error);
}
