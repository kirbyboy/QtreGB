#ifndef QTREGB_INPUT_MANAGER_H
#define QTREGB_INPUT_MANAGER_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <SDL2/SDL.h>

#include "input_directions.h"
#include "input_settings.h"

struct ControllerInfo
{
	InputDeviceId device;
	std::string name;
	std::string guid_text;
	SDL_JoystickID instance_id = -1;
};

// Owns the SDL keyboard/controller state for the GUI. Player bindings and
// global shortcuts are resolved here so the rest of the GUI only sees logical
// Game Boy buttons and one-shot shortcut events.
class InputManager
{
	public:
		enum class CaptureKind
		{
			None,
			Key,
			Controller,
		};

		InputManager();
		~InputManager();
		InputManager(const InputManager&) = delete;
		InputManager& operator=(const InputManager&) = delete;

		void set_settings(const InputSettings& settings);
		// Closes open controllers; call before SDL_Quit.
		void shutdown();

		void refresh_devices();
		const std::vector<ControllerInfo>& controllers() const
		{
			return m_controllers;
		}
		std::string device_label(const InputDeviceId& device) const;
		bool is_device_connected(const InputDeviceId& device) const;

		void handle_event(const SDL_Event& event);
		// Recomputes player and shortcut state from the latest SDL events.
		void update();

		bool player_pressed(size_t player, GbButton button) const;
		// Returns true once per press. The trigger is cleared when consumed.
		bool consume_shortcut(ShortcutAction action);

		// An empty controller_guid means "any controller" (used by the global
		// shortcut slots). Player slots pass the selected device GUID.
		void begin_capture(CaptureKind kind, std::string description,
			std::string controller_guid = {});
		void cancel_capture();
		bool capturing() const;
		bool take_capture(HostControl& control, bool& cleared);
		const std::string& capture_description() const
		{
			return m_capture.description;
		}

	private:
		struct OpenController
		{
			SDL_GameController* controller = nullptr;
			SDL_JoystickID instance_id = -1;
			std::string guid;
			std::string name;
			std::string guid_text;
		};

		struct CaptureState
		{
			bool active = false;
			bool done = false;
			bool cleared = false;
			CaptureKind kind = CaptureKind::None;
			HostControl control;
			std::string description;
			std::string controller_guid;
		};

		InputSettings m_settings;
		std::vector<OpenController> m_open_controllers;
		std::vector<ControllerInfo> m_controllers;
		std::array<bool, SDL_NUM_SCANCODES> m_keys {};

		// Physical state before opposite-direction resolution, used both as
		// the player input and as axis hysteresis memory.
		std::array<std::array<bool, 8>, 2> m_raw_pressed {};
		std::array<std::array<bool, 8>, 2> m_logical_pressed {};
		std::array<DirectionResolver, 2> m_directions;

		std::array<bool, 3> m_shortcut_controller_raw {};
		std::array<bool, 3> m_shortcut_previous {};
		std::array<bool, 3> m_shortcut_trigger {};

		CaptureState m_capture;

		void rebuild_device_list();
		bool capture_matches_controller(SDL_JoystickID which) const;
		bool controller_control_pressed(const HostControl& control,
			const std::string& guid, bool was_pressed) const;
		bool any_controller_control_pressed(const HostControl& control,
			bool was_pressed) const;
		bool player_binding_pressed(const PlayerInputSettings& player,
			const HostControl& binding, bool was_pressed) const;
		bool shortcut_pressed(const Shortcut& shortcut, size_t index);
};

#endif
