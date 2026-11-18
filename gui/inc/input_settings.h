#ifndef QTREGB_INPUT_SETTINGS_H
#define QTREGB_INPUT_SETTINGS_H

#include <array>
#include <string>

#include "input_state.h"
#include "types.h"

enum class InputDeviceType : qtreGB::u8
{
	None,
	Keyboard,
	Controller,
};

struct InputDeviceId
{
	InputDeviceType type = InputDeviceType::None;
	std::string guid;

	bool operator==(const InputDeviceId& other) const
	{
		return type == other.type && guid == other.guid;
	}
	bool operator!=(const InputDeviceId& other) const
	{
		return !(*this == other);
	}
};

enum class HostControlType : qtreGB::u8
{
	None,
	Key,
	ControllerButton,
	ControllerAxis,
};

struct HostControl
{
	HostControlType type = HostControlType::None;
	int code = 0;
	// -1 or +1 for ControllerAxis; 0 otherwise.
	int direction = 0;

	bool is_none() const { return type == HostControlType::None; }
	bool operator==(const HostControl& other) const
	{
		return type == other.type && code == other.code &&
			(type != HostControlType::ControllerAxis ||
			 direction == other.direction);
	}
	bool operator!=(const HostControl& other) const
	{
		return !(*this == other);
	}
};

struct PlayerInputSettings
{
	InputDeviceId device;
	std::array<HostControl, 8> bindings {};
};

enum class ShortcutAction : qtreGB::u8
{
	Fullscreen = 0,
	SaveState,
	LoadState,
	Count,
};

struct Shortcut
{
	HostControl keyboard;
	HostControl controller;
};

struct InputSettings
{
	std::array<PlayerInputSettings, 2> players;
	std::array<Shortcut, 3> shortcuts;
	bool allow_opposite_directions = false;

	static InputSettings defaults();

	// Removes invalid, duplicate and cross-conflicting controls. Key bindings
	// always win over player bindings.
	void sanitize();

	static const char* gb_button_name(GbButton button);
	static const char* shortcut_name(ShortcutAction action);
	static bool is_forbidden_key(int scancode);

	static std::string device_token(const InputDeviceId& device);
	static bool parse_device(const std::string& text, InputDeviceId& device);
	static std::string control_token(const HostControl& control);
	static bool parse_control(const std::string& text, HostControl& control);
	static std::string control_name(const HostControl& control);
};

#endif
