#include "input_settings.h"

#include <SDL2/SDL.h>

#include <charconv>
#include <string_view>

namespace
{

bool parse_int(std::string_view text, int& value)
{
	const char* begin = text.data();
	const char* end = begin + text.size();
	const auto result = std::from_chars(begin, end, value);
	return result.ec == std::errc() && result.ptr == end;
}

bool valid_key(const HostControl& control)
{
	return control.type == HostControlType::Key &&
		control.code > 0 && control.code < SDL_NUM_SCANCODES &&
		!InputSettings::is_forbidden_key(control.code);
}

bool valid_controller_control(const HostControl& control)
{
	if(control.type == HostControlType::ControllerButton)
	{
		return control.code >= 0 && control.code < SDL_CONTROLLER_BUTTON_MAX;
	}
	if(control.type == HostControlType::ControllerAxis)
	{
		return control.code >= 0 && control.code < SDL_CONTROLLER_AXIS_MAX &&
			(control.direction == -1 || control.direction == 1);
	}
	return false;
}

bool matches_shortcut(const HostControl& control,
	const std::array<Shortcut, InputSettings::ShortcutCount>& shortcuts)
{
	for(const Shortcut& shortcut : shortcuts)
	{
		if((control.type == HostControlType::Key &&
		    control == shortcut.keyboard) ||
		   (control.type != HostControlType::Key &&
		    control == shortcut.controller))
		{
			return true;
		}
	}
	return false;
}

} // namespace

InputSettings InputSettings::defaults()
{
	InputSettings settings;

	PlayerInputSettings& p1 = settings.players[0];
	p1.device.type = InputDeviceType::Keyboard;
	p1.bindings[static_cast<size_t>(GbButton::Right)] =
		{HostControlType::Key, SDL_SCANCODE_RIGHT, 0};
	p1.bindings[static_cast<size_t>(GbButton::Left)] =
		{HostControlType::Key, SDL_SCANCODE_LEFT, 0};
	p1.bindings[static_cast<size_t>(GbButton::Up)] =
		{HostControlType::Key, SDL_SCANCODE_UP, 0};
	p1.bindings[static_cast<size_t>(GbButton::Down)] =
		{HostControlType::Key, SDL_SCANCODE_DOWN, 0};
	p1.bindings[static_cast<size_t>(GbButton::A)] =
		{HostControlType::Key, SDL_SCANCODE_Z, 0};
	p1.bindings[static_cast<size_t>(GbButton::B)] =
		{HostControlType::Key, SDL_SCANCODE_X, 0};
	p1.bindings[static_cast<size_t>(GbButton::Start)] =
		{HostControlType::Key, SDL_SCANCODE_RETURN, 0};
	p1.bindings[static_cast<size_t>(GbButton::Select)] =
		{HostControlType::Key, SDL_SCANCODE_RSHIFT, 0};

	settings.players[1].device.type = InputDeviceType::None;

	settings.shortcuts[static_cast<size_t>(ShortcutAction::Fullscreen)].keyboard =
		{HostControlType::Key, SDL_SCANCODE_F, 0};
	settings.shortcuts[static_cast<size_t>(ShortcutAction::SaveState)].keyboard =
		{HostControlType::Key, SDL_SCANCODE_F5, 0};
	settings.shortcuts[static_cast<size_t>(ShortcutAction::LoadState)].keyboard =
		{HostControlType::Key, SDL_SCANCODE_F8, 0};
	settings.shortcuts[static_cast<size_t>(ShortcutAction::NextStateSlot)].keyboard =
		{HostControlType::Key, SDL_SCANCODE_PAGEUP, 0};
	settings.shortcuts[static_cast<size_t>(ShortcutAction::PreviousStateSlot)].keyboard =
		{HostControlType::Key, SDL_SCANCODE_PAGEDOWN, 0};
	settings.shortcuts[static_cast<size_t>(ShortcutAction::FastForward)].keyboard =
		{HostControlType::Key, SDL_SCANCODE_SPACE, 0};

	return settings;
}

void InputSettings::sanitize()
{
	std::array<HostControl, ShortcutCount> keyboard_shortcuts {};
	std::array<HostControl, ShortcutCount> controller_shortcuts {};

	for(size_t i = 0; i < shortcuts.size(); ++i)
	{
		HostControl& keyboard = shortcuts[i].keyboard;
		if(!valid_key(keyboard))
		{
			keyboard = {};
		}
		else
		{
			for(size_t j = 0; j < i; ++j)
			{
				if(keyboard == keyboard_shortcuts[j])
				{
					keyboard = {};
					break;
				}
			}
		}
		keyboard_shortcuts[i] = keyboard;

		HostControl& controller = shortcuts[i].controller;
		if(!valid_controller_control(controller))
		{
			controller = {};
		}
		else
		{
			for(size_t j = 0; j < i; ++j)
			{
				if(controller == controller_shortcuts[j])
				{
					controller = {};
					break;
				}
			}
		}
		controller_shortcuts[i] = controller;
	}

	for(PlayerInputSettings& player : players)
	{
		if(player.device.type == InputDeviceType::Controller &&
		   player.device.guid.empty())
		{
			player.device = {};
		}

		if(player.device.type == InputDeviceType::None)
		{
			for(HostControl& binding : player.bindings)
			{
				binding = {};
			}
			continue;
		}

		for(size_t i = 0; i < player.bindings.size(); ++i)
		{
			HostControl& binding = player.bindings[i];
			if(binding.is_none())
			{
				continue;
			}

			if(player.device.type == InputDeviceType::Keyboard)
			{
				if(!valid_key(binding))
				{
					binding = {};
					continue;
				}
			}
			else if(!valid_controller_control(binding))
			{
				binding = {};
				continue;
			}

			bool duplicate = false;
			for(size_t j = 0; j < i; ++j)
			{
				if(binding == player.bindings[j])
				{
					duplicate = true;
					break;
				}
			}
			if(duplicate || matches_shortcut(binding, shortcuts))
			{
				binding = {};
			}
		}
	}
}

const char* InputSettings::gb_button_name(GbButton button)
{
	switch(button)
	{
		case GbButton::Right: return "Right";
		case GbButton::Left: return "Left";
		case GbButton::Up: return "Up";
		case GbButton::Down: return "Down";
		case GbButton::A: return "A";
		case GbButton::B: return "B";
		case GbButton::Start: return "Start";
		case GbButton::Select: return "Select";
		case GbButton::Count: break;
	}
	return "?";
}

const char* InputSettings::shortcut_name(ShortcutAction action)
{
	switch(action)
	{
		case ShortcutAction::Fullscreen: return "Fullscreen";
		case ShortcutAction::SaveState: return "Save state";
		case ShortcutAction::LoadState: return "Load state";
		case ShortcutAction::NextStateSlot: return "Next state slot";
		case ShortcutAction::PreviousStateSlot: return "Previous state slot";
		case ShortcutAction::FastForward: return "Fast forward";
		case ShortcutAction::Count: break;
	}
	return "?";
}

bool InputSettings::is_forbidden_key(int scancode)
{
	switch(scancode)
	{
		case SDL_SCANCODE_LCTRL:
		case SDL_SCANCODE_RCTRL:
		case SDL_SCANCODE_LALT:
		case SDL_SCANCODE_RALT:
		case SDL_SCANCODE_ESCAPE:
		case SDL_SCANCODE_BACKSPACE:
			return true;
		default:
			return false;
	}
}

std::string InputSettings::device_token(const InputDeviceId& device)
{
	switch(device.type)
	{
		case InputDeviceType::None:
			return "none";
		case InputDeviceType::Keyboard:
			return "keyboard";
		case InputDeviceType::Controller:
			return "controller:" + device.guid;
	}
	return "none";
}

bool InputSettings::parse_device(const std::string& text, InputDeviceId& device)
{
	if(text == "none")
	{
		device = {};
		return true;
	}
	if(text == "keyboard")
	{
		device = {};
		device.type = InputDeviceType::Keyboard;
		return true;
	}
	constexpr std::string_view prefix = "controller:";
	if(text.rfind(prefix, 0) == 0 && text.size() > prefix.size())
	{
		device = {};
		device.type = InputDeviceType::Controller;
		device.guid = text.substr(prefix.size());
		return true;
	}
	return false;
}

std::string InputSettings::control_token(const HostControl& control)
{
	switch(control.type)
	{
		case HostControlType::None:
			return "none";
		case HostControlType::Key:
			return "key:" + std::to_string(control.code);
		case HostControlType::ControllerButton:
			return "button:" + std::to_string(control.code);
		case HostControlType::ControllerAxis:
			return "axis:" + std::to_string(control.code) + ":" +
				std::to_string(control.direction);
	}
	return "none";
}

bool InputSettings::parse_control(const std::string& text, HostControl& control)
{
	if(text == "none" || text.empty())
	{
		control = {};
		return true;
	}

	int value = 0;
	if(text.rfind("key:", 0) == 0 &&
	   parse_int(std::string_view(text).substr(4), value) &&
	   value >= 0 && value < SDL_NUM_SCANCODES)
	{
		control = {HostControlType::Key, value, 0};
		return true;
	}
	if(text.rfind("button:", 0) == 0 &&
	   parse_int(std::string_view(text).substr(7), value) &&
	   value >= 0 && value < SDL_CONTROLLER_BUTTON_MAX)
	{
		control = {HostControlType::ControllerButton, value, 0};
		return true;
	}
	if(text.rfind("axis:", 0) == 0)
	{
		const std::string_view rest = std::string_view(text).substr(5);
		const size_t colon = rest.find(':');
		if(colon != std::string_view::npos &&
		   parse_int(rest.substr(0, colon), value))
		{
			int direction = 0;
			if(parse_int(rest.substr(colon + 1), direction) &&
			   value >= 0 && value < SDL_CONTROLLER_AXIS_MAX &&
			   (direction == -1 || direction == 1))
			{
				control = {HostControlType::ControllerAxis, value, direction};
				return true;
			}
		}
	}
	return false;
}

std::string InputSettings::control_name(const HostControl& control)
{
	switch(control.type)
	{
		case HostControlType::None:
			return "(none)";
		case HostControlType::Key:
		{
			const char* name = SDL_GetScancodeName(
				static_cast<SDL_Scancode>(control.code));
			return (name != nullptr && name[0] != '\0')
				? name
				: "Key " + std::to_string(control.code);
		}
		case HostControlType::ControllerButton:
		{
			switch(control.code)
			{
				case SDL_CONTROLLER_BUTTON_A: return "A";
				case SDL_CONTROLLER_BUTTON_B: return "B";
				case SDL_CONTROLLER_BUTTON_X: return "X";
				case SDL_CONTROLLER_BUTTON_Y: return "Y";
				case SDL_CONTROLLER_BUTTON_BACK: return "Back";
				case SDL_CONTROLLER_BUTTON_GUIDE: return "Guide";
				case SDL_CONTROLLER_BUTTON_START: return "Start";
				case SDL_CONTROLLER_BUTTON_LEFTSTICK: return "Left Stick";
				case SDL_CONTROLLER_BUTTON_RIGHTSTICK: return "Right Stick";
				case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return "LB";
				case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return "RB";
				case SDL_CONTROLLER_BUTTON_DPAD_UP: return "D-Pad Up";
				case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return "D-Pad Down";
				case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return "D-Pad Left";
				case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return "D-Pad Right";
				default:
					return "Button " + std::to_string(control.code);
			}
		}
		case HostControlType::ControllerAxis:
		{
			switch(control.code)
			{
				case SDL_CONTROLLER_AXIS_LEFTX:
					return control.direction < 0
						? "Left Stick Left" : "Left Stick Right";
				case SDL_CONTROLLER_AXIS_LEFTY:
					return control.direction < 0
						? "Left Stick Up" : "Left Stick Down";
				case SDL_CONTROLLER_AXIS_RIGHTX:
					return control.direction < 0
						? "Right Stick Left" : "Right Stick Right";
				case SDL_CONTROLLER_AXIS_RIGHTY:
					return control.direction < 0
						? "Right Stick Up" : "Right Stick Down";
				case SDL_CONTROLLER_AXIS_TRIGGERLEFT:
					return "LT";
				case SDL_CONTROLLER_AXIS_TRIGGERRIGHT:
					return "RT";
				default:
					return "Axis " + std::to_string(control.code) +
						(control.direction < 0 ? " -" : " +");
			}
		}
	}
	return "(none)";
}
