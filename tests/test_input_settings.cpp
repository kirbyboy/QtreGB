#include "doctest.h"
#include "input_settings.h"

#include <SDL2/SDL.h>

TEST_CASE("Input settings: defaults use the keyboard for player 1")
{
	const InputSettings settings = InputSettings::defaults();
	CHECK(settings.players[0].device.type == InputDeviceType::Keyboard);
	CHECK(settings.players[0].bindings[static_cast<size_t>(GbButton::A)] ==
		HostControl {HostControlType::Key, SDL_SCANCODE_Z, 0});
	CHECK(settings.players[0].bindings[static_cast<size_t>(GbButton::Select)] ==
		HostControl {HostControlType::Key, SDL_SCANCODE_RSHIFT, 0});
	CHECK(settings.players[1].device.type == InputDeviceType::None);
	CHECK(settings.shortcuts[0].keyboard ==
		HostControl {HostControlType::Key, SDL_SCANCODE_F, 0});
	CHECK(settings.shortcuts[1].keyboard ==
		HostControl {HostControlType::Key, SDL_SCANCODE_F5, 0});
	CHECK(settings.shortcuts[2].keyboard ==
		HostControl {HostControlType::Key, SDL_SCANCODE_F8, 0});
	CHECK_FALSE(settings.allow_opposite_directions);
}

TEST_CASE("Input settings: duplicate player bindings are removed")
{
	InputSettings settings = InputSettings::defaults();
	settings.players[0].bindings[static_cast<size_t>(GbButton::A)] =
		{HostControlType::Key, SDL_SCANCODE_Q, 0};
	settings.players[0].bindings[static_cast<size_t>(GbButton::B)] =
		{HostControlType::Key, SDL_SCANCODE_Q, 0};

	settings.sanitize();

	CHECK(settings.players[0].bindings[static_cast<size_t>(GbButton::A)] ==
		HostControl {HostControlType::Key, SDL_SCANCODE_Q, 0});
	CHECK(settings.players[0].bindings[static_cast<size_t>(GbButton::B)].is_none());
}

TEST_CASE("Input settings: key bindings win over player bindings")
{
	InputSettings settings = InputSettings::defaults();
	settings.players[0].bindings[static_cast<size_t>(GbButton::A)] =
		{HostControlType::Key, SDL_SCANCODE_F, 0};
	settings.players[0].bindings[static_cast<size_t>(GbButton::B)] =
		{HostControlType::ControllerButton, SDL_CONTROLLER_BUTTON_A, 0};
	settings.players[0].device.type = InputDeviceType::Controller;
	settings.players[0].device.guid = "controller-guid";
	settings.shortcuts[0].controller =
		{HostControlType::ControllerButton, SDL_CONTROLLER_BUTTON_A, 0};

	settings.sanitize();

	CHECK(settings.players[0].bindings[static_cast<size_t>(GbButton::A)].is_none());
	CHECK(settings.players[0].bindings[static_cast<size_t>(GbButton::B)].is_none());
}

TEST_CASE("Input settings: duplicate shortcuts remove the last one")
{
	InputSettings settings = InputSettings::defaults();
	settings.shortcuts[1].keyboard =
		{HostControlType::Key, SDL_SCANCODE_F, 0};

	settings.sanitize();

	CHECK(settings.shortcuts[0].keyboard ==
		HostControl {HostControlType::Key, SDL_SCANCODE_F, 0});
	CHECK(settings.shortcuts[1].keyboard.is_none());
}

TEST_CASE("Input settings: forbidden keys are rejected and shift is allowed")
{
	InputSettings settings = InputSettings::defaults();
	settings.players[0].bindings[static_cast<size_t>(GbButton::A)] =
		{HostControlType::Key, SDL_SCANCODE_ESCAPE, 0};
	settings.players[0].bindings[static_cast<size_t>(GbButton::B)] =
		{HostControlType::Key, SDL_SCANCODE_BACKSPACE, 0};
	settings.players[0].bindings[static_cast<size_t>(GbButton::Start)] =
		{HostControlType::Key, SDL_SCANCODE_LCTRL, 0};
	settings.players[0].bindings[static_cast<size_t>(GbButton::Select)] =
		{HostControlType::Key, SDL_SCANCODE_LSHIFT, 0};

	settings.sanitize();

	CHECK(settings.players[0].bindings[static_cast<size_t>(GbButton::A)].is_none());
	CHECK(settings.players[0].bindings[static_cast<size_t>(GbButton::B)].is_none());
	CHECK(settings.players[0].bindings[static_cast<size_t>(GbButton::Start)].is_none());
	CHECK(settings.players[0].bindings[static_cast<size_t>(GbButton::Select)] ==
		HostControl {HostControlType::Key, SDL_SCANCODE_LSHIFT, 0});
}

TEST_CASE("Input settings: the same control is allowed for both players")
{
	InputSettings settings = InputSettings::defaults();
	settings.players[1].device.type = InputDeviceType::Keyboard;
	settings.players[1].bindings = settings.players[0].bindings;

	settings.sanitize();

	CHECK(settings.players[0].bindings[static_cast<size_t>(GbButton::A)] ==
		settings.players[1].bindings[static_cast<size_t>(GbButton::A)]);
	CHECK_FALSE(settings.players[0].bindings[static_cast<size_t>(GbButton::A)].is_none());
}

TEST_CASE("Input settings: controller shortcuts win over controller bindings")
{
	InputSettings settings = InputSettings::defaults();
	settings.players[0].device = {};
	settings.players[0].device.type = InputDeviceType::Controller;
	settings.players[0].device.guid = "guid";
	settings.players[0].bindings[static_cast<size_t>(GbButton::A)] =
		{HostControlType::ControllerButton, SDL_CONTROLLER_BUTTON_A, 0};
	settings.players[1].device = {};
	settings.players[1].device.type = InputDeviceType::Controller;
	settings.players[1].device.guid = "other-guid";
	settings.players[1].bindings[static_cast<size_t>(GbButton::B)] =
		{HostControlType::ControllerButton, SDL_CONTROLLER_BUTTON_A, 0};
	settings.shortcuts[static_cast<size_t>(ShortcutAction::Fullscreen)].controller =
		{HostControlType::ControllerButton, SDL_CONTROLLER_BUTTON_A, 0};

	settings.sanitize();

	CHECK(settings.players[0].bindings[static_cast<size_t>(GbButton::A)].is_none());
	CHECK(settings.players[1].bindings[static_cast<size_t>(GbButton::B)].is_none());
	CHECK(settings.shortcuts[static_cast<size_t>(ShortcutAction::Fullscreen)].controller ==
		(HostControl {HostControlType::ControllerButton,
			SDL_CONTROLLER_BUTTON_A, 0}));
}

TEST_CASE("Input settings: control and device tokens round-trip")
{
	InputDeviceId device;
	REQUIRE(InputSettings::parse_device("controller:ABC123", device));
	CHECK(device.type == InputDeviceType::Controller);
	CHECK(device.guid == "ABC123");
	CHECK(InputSettings::device_token(device) == "controller:ABC123");

	const HostControl controls[] = {
		{HostControlType::Key, SDL_SCANCODE_Z, 0},
		{HostControlType::ControllerButton, SDL_CONTROLLER_BUTTON_X, 0},
		{HostControlType::ControllerAxis, SDL_CONTROLLER_AXIS_LEFTY, -1},
	};
	for(const HostControl& control : controls)
	{
		HostControl parsed;
		REQUIRE(InputSettings::parse_control(
			InputSettings::control_token(control), parsed));
		CHECK(parsed == control);
	}
}
