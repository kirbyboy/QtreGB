#include "doctest.h"
#include "input_manager.h"

#include <SDL2/SDL.h>

namespace
{

void send_key(InputManager& manager, SDL_Scancode scancode, bool down)
{
	SDL_Event event {};
	event.type = down ? SDL_KEYDOWN : SDL_KEYUP;
	event.key.keysym.scancode = scancode;
	event.key.repeat = 0;
	manager.handle_event(event);
}

} // namespace

TEST_CASE("Input manager: keyboard player bindings and shortcuts")
{
	InputManager manager;
	manager.set_settings(InputSettings::defaults());

	send_key(manager, SDL_SCANCODE_Z, true);
	manager.update();
	CHECK(manager.player_pressed(0, GbButton::A));
	CHECK_FALSE(manager.player_pressed(0, GbButton::B));

	send_key(manager, SDL_SCANCODE_Z, false);
	manager.update();
	CHECK_FALSE(manager.player_pressed(0, GbButton::A));

	send_key(manager, SDL_SCANCODE_F5, true);
	manager.update();
	CHECK(manager.consume_shortcut(ShortcutAction::SaveState));
	CHECK_FALSE(manager.consume_shortcut(ShortcutAction::SaveState));

	// Holding the key must not repeat the shortcut.
	manager.update();
	CHECK_FALSE(manager.consume_shortcut(ShortcutAction::SaveState));

	// Release and press again rearms it.
	send_key(manager, SDL_SCANCODE_F5, false);
	manager.update();
	send_key(manager, SDL_SCANCODE_F5, true);
	manager.update();
	CHECK(manager.consume_shortcut(ShortcutAction::SaveState));
}

TEST_CASE("Input manager: player 2 has no bindings by default")
{
	InputManager manager;
	manager.set_settings(InputSettings::defaults());

	send_key(manager, SDL_SCANCODE_Z, true);
	send_key(manager, SDL_SCANCODE_I, true);
	manager.update();

	CHECK(manager.player_pressed(0, GbButton::A));
	CHECK_FALSE(manager.player_pressed(1, GbButton::A));
	CHECK_FALSE(manager.player_pressed(1, GbButton::Up));
}

TEST_CASE("Input manager: key capture rejects forbidden keys and clears")
{
	InputManager manager;
	manager.set_settings(InputSettings::defaults());

	manager.begin_capture(InputManager::CaptureKind::Key, "test");
	CHECK(manager.capturing());

	send_key(manager, SDL_SCANCODE_LCTRL, true);
	HostControl control;
	bool cleared = false;
	CHECK_FALSE(manager.take_capture(control, cleared));
	CHECK(manager.capturing());

	send_key(manager, SDL_SCANCODE_LCTRL, false);
	send_key(manager, SDL_SCANCODE_ESCAPE, true);
	CHECK(manager.take_capture(control, cleared));
	CHECK(cleared);
	CHECK_FALSE(manager.capturing());
}

TEST_CASE("Input manager: key capture returns the pressed key")
{
	InputManager manager;
	manager.set_settings(InputSettings::defaults());

	manager.begin_capture(InputManager::CaptureKind::Key, "test");
	send_key(manager, SDL_SCANCODE_K, true);

	HostControl control;
	bool cleared = true;
	CHECK(manager.take_capture(control, cleared));
	CHECK_FALSE(cleared);
	CHECK(control == (HostControl {HostControlType::Key, SDL_SCANCODE_K, 0}));
}

TEST_CASE("Input manager: a captured key does not immediately trigger a shortcut")
{
	InputManager manager;
	manager.set_settings(InputSettings::defaults());

	manager.begin_capture(InputManager::CaptureKind::Key, "test");
	send_key(manager, SDL_SCANCODE_F5, true);

	HostControl control;
	bool cleared = false;
	CHECK(manager.take_capture(control, cleared));
	CHECK_FALSE(cleared);
	CHECK(control ==
		(HostControl {HostControlType::Key, SDL_SCANCODE_F5, 0}));

	// F5 is still physically held after the capture.
	manager.update();
	CHECK_FALSE(manager.consume_shortcut(ShortcutAction::SaveState));

	send_key(manager, SDL_SCANCODE_F5, false);
	manager.update();
	send_key(manager, SDL_SCANCODE_F5, true);
	manager.update();
	CHECK(manager.consume_shortcut(ShortcutAction::SaveState));
}

TEST_CASE("Input manager: Backspace clears a controller capture")
{
	InputManager manager;
	manager.set_settings(InputSettings::defaults());

	manager.begin_capture(InputManager::CaptureKind::Controller, "test");
	send_key(manager, SDL_SCANCODE_BACKSPACE, true);

	HostControl control;
	bool cleared = false;
	CHECK(manager.take_capture(control, cleared));
	CHECK(cleared);
}

TEST_CASE("Input manager: opposite directions honour the setting")
{
	InputSettings settings = InputSettings::defaults();
	InputManager manager;
	manager.set_settings(settings);

	// Left first, then Right: Right wins while both keys are held.
	send_key(manager, SDL_SCANCODE_LEFT, true);
	manager.update();
	CHECK(manager.player_pressed(0, GbButton::Left));

	send_key(manager, SDL_SCANCODE_RIGHT, true);
	manager.update();
	CHECK(manager.player_pressed(0, GbButton::Right));
	CHECK_FALSE(manager.player_pressed(0, GbButton::Left));

	// Releasing the winner restores the key that is still held.
	send_key(manager, SDL_SCANCODE_RIGHT, false);
	manager.update();
	CHECK(manager.player_pressed(0, GbButton::Left));
	CHECK_FALSE(manager.player_pressed(0, GbButton::Right));

	settings.allow_opposite_directions = true;
	manager.set_settings(settings);
	send_key(manager, SDL_SCANCODE_RIGHT, true);
	manager.update();
	CHECK(manager.player_pressed(0, GbButton::Left));
	CHECK(manager.player_pressed(0, GbButton::Right));
}

TEST_CASE("Input manager: rebinding a shortcut to a held key does not fire it")
{
	InputManager manager;
	const InputSettings defaults = InputSettings::defaults();
	manager.set_settings(defaults);

	manager.begin_capture(InputManager::CaptureKind::Key, "test");
	send_key(manager, SDL_SCANCODE_F6, true);

	HostControl control;
	bool cleared = false;
	REQUIRE(manager.take_capture(control, cleared));
	REQUIRE_FALSE(cleared);

	InputSettings changed = defaults;
	changed.shortcuts[static_cast<size_t>(ShortcutAction::SaveState)].keyboard =
		control;
	manager.set_settings(changed);

	manager.update();
	CHECK_FALSE(manager.consume_shortcut(ShortcutAction::SaveState));

	send_key(manager, SDL_SCANCODE_F6, false);
	manager.update();
	send_key(manager, SDL_SCANCODE_F6, true);
	manager.update();
	CHECK(manager.consume_shortcut(ShortcutAction::SaveState));
}
