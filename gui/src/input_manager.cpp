#include "input_manager.h"

#include <algorithm>
#include <utility>

namespace
{

constexpr Sint16 kAxisPressThreshold = 19660;	// 60% of 32767
constexpr Sint16 kAxisReleaseThreshold = 13107;	// 40% of 32767

bool is_controller_control(const HostControl& control)
{
	return control.type == HostControlType::ControllerButton ||
		control.type == HostControlType::ControllerAxis;
}

} // namespace

InputManager::InputManager()
{
	refresh_devices();
}

InputManager::~InputManager()
{
	shutdown();
}

void InputManager::shutdown()
{
	if(SDL_WasInit(SDL_INIT_GAMECONTROLLER) != 0)
	{
		for(OpenController& open : m_open_controllers)
		{
			if(open.controller != nullptr)
			{
				SDL_GameControllerClose(open.controller);
				open.controller = nullptr;
			}
		}
	}
	m_open_controllers.clear();
	m_controllers.clear();
}

void InputManager::set_settings(const InputSettings& settings)
{
	m_settings = settings;
	m_settings.sanitize();

	// Re-arm the shortcuts against the current physical state. Changing a
	// shortcut while its new key is still held must not fire it immediately.
	for(size_t i = 0; i < m_settings.shortcuts.size(); ++i)
	{
		m_shortcut_previous[i] =
			shortcut_pressed(m_settings.shortcuts[i], i);
		m_shortcut_trigger[i] = false;
	}
}

void InputManager::refresh_devices()
{
	if(SDL_WasInit(SDL_INIT_GAMECONTROLLER) == 0)
	{
		m_open_controllers.clear();
		m_controllers.clear();
		return;
	}

	for(size_t i = 0; i < m_open_controllers.size();)
	{
		OpenController& open = m_open_controllers[i];
		if(open.controller == nullptr || !SDL_GameControllerGetAttached(open.controller))
		{
			if(open.controller != nullptr)
			{
				SDL_GameControllerClose(open.controller);
			}
			m_open_controllers.erase(m_open_controllers.begin() +
				static_cast<std::ptrdiff_t>(i));
			continue;
		}
		++i;
	}

	const int joystick_count = SDL_NumJoysticks();
	for(int index = 0; index < joystick_count; ++index)
	{
		if(!SDL_IsGameController(index))
		{
			continue;
		}

		const SDL_JoystickID device_id = SDL_JoystickGetDeviceInstanceID(index);
		const bool already_open = std::any_of(
			m_open_controllers.begin(), m_open_controllers.end(),
			[device_id](const OpenController& open)
			{
				return open.instance_id == device_id;
			});
		if(already_open)
		{
			continue;
		}

		SDL_GameController* controller = SDL_GameControllerOpen(index);
		if(controller == nullptr)
		{
			continue;
		}
		SDL_Joystick* joystick = SDL_GameControllerGetJoystick(controller);
		if(joystick == nullptr)
		{
			SDL_GameControllerClose(controller);
			continue;
		}

		OpenController open;
		open.controller = controller;
		open.instance_id = SDL_JoystickInstanceID(joystick);

		char guid_text[64] {};
		const SDL_JoystickGUID guid = SDL_JoystickGetGUID(joystick);
		SDL_JoystickGetGUIDString(guid, guid_text, sizeof(guid_text));
		open.guid = guid_text;
		open.guid_text = guid_text;

		const char* name = SDL_GameControllerName(controller);
		open.name = (name != nullptr && name[0] != '\0') ? name : "Controller";
		m_open_controllers.push_back(std::move(open));
	}

	rebuild_device_list();
}

void InputManager::rebuild_device_list()
{
	m_controllers.clear();
	for(const OpenController& open : m_open_controllers)
	{
		ControllerInfo info;
		info.device.type = InputDeviceType::Controller;
		info.device.guid = open.guid;
		info.name = open.name;
		info.guid_text = open.guid_text;
		info.instance_id = open.instance_id;
		m_controllers.push_back(std::move(info));
	}
}

std::string InputManager::device_label(const InputDeviceId& device) const
{
	switch(device.type)
	{
		case InputDeviceType::None:
			return "None";
		case InputDeviceType::Keyboard:
			return "Keyboard";
		case InputDeviceType::Controller:
			for(const ControllerInfo& info : m_controllers)
			{
				if(info.device.guid == device.guid)
				{
					return info.name;
				}
			}
			return "Controller (not connected)";
	}
	return "None";
}

bool InputManager::is_device_connected(const InputDeviceId& device) const
{
	switch(device.type)
	{
		case InputDeviceType::None:
			return false;
		case InputDeviceType::Keyboard:
			return true;
		case InputDeviceType::Controller:
			return std::any_of(m_controllers.begin(), m_controllers.end(),
				[&device](const ControllerInfo& info)
				{
					return info.device.guid == device.guid;
				});
	}
	return false;
}

void InputManager::handle_event(const SDL_Event& event)
{
	switch(event.type)
	{
		case SDL_KEYDOWN:
		{
			const int scancode = event.key.keysym.scancode;
			if(scancode >= 0 && scancode < SDL_NUM_SCANCODES)
			{
				m_keys[static_cast<size_t>(scancode)] = true;
			}
			if(event.key.repeat || m_capture.done ||
			   (m_capture.kind == CaptureKind::None))
			{
				break;
			}

			if(scancode == SDL_SCANCODE_ESCAPE ||
			   scancode == SDL_SCANCODE_BACKSPACE)
			{
				m_capture.done = true;
				m_capture.cleared = true;
				break;
			}
			if(m_capture.kind == CaptureKind::Key && scancode > 0 &&
			   scancode < SDL_NUM_SCANCODES &&
			   !InputSettings::is_forbidden_key(scancode))
			{
				m_capture.control = {
					HostControlType::Key, scancode, 0
				};
				m_capture.done = true;
			}
			break;
		}
		case SDL_KEYUP:
		{
			const int scancode = event.key.keysym.scancode;
			if(scancode >= 0 && scancode < SDL_NUM_SCANCODES)
			{
				m_keys[static_cast<size_t>(scancode)] = false;
			}
			break;
		}
		case SDL_WINDOWEVENT:
			if(event.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
			{
				// A key released while another window has the focus may not
				// produce a KEYUP event. Release everything to avoid a
				// stuck direction or button.
				m_keys.fill(false);
			}
			break;
		case SDL_CONTROLLERDEVICEADDED:
		case SDL_CONTROLLERDEVICEREMOVED:
			refresh_devices();
			break;
		case SDL_CONTROLLERBUTTONDOWN:
			if(!m_capture.done && m_capture.kind == CaptureKind::Controller &&
			   capture_matches_controller(event.cbutton.which) &&
			   event.cbutton.button < SDL_CONTROLLER_BUTTON_MAX)
			{
				m_capture.control = {
					HostControlType::ControllerButton,
					event.cbutton.button, 0
				};
				m_capture.done = true;
			}
			break;
		case SDL_CONTROLLERAXISMOTION:
			if(!m_capture.done && m_capture.kind == CaptureKind::Controller &&
			   capture_matches_controller(event.caxis.which))
			{
				const Sint16 value = event.caxis.value;
				if(value >= kAxisPressThreshold)
				{
					m_capture.control = {
						HostControlType::ControllerAxis,
						event.caxis.axis, 1
					};
					m_capture.done = true;
				}
				else if(value <= -kAxisPressThreshold)
				{
					m_capture.control = {
						HostControlType::ControllerAxis,
						event.caxis.axis, -1
					};
					m_capture.done = true;
				}
			}
			break;
		default:
			break;
	}
}

bool InputManager::capture_matches_controller(SDL_JoystickID which) const
{
	if(m_capture.controller_guid.empty())
	{
		return true;
	}
	for(const OpenController& open : m_open_controllers)
	{
		if(open.instance_id == which &&
		   open.guid == m_capture.controller_guid)
		{
			return true;
		}
	}
	return false;
}

bool InputManager::controller_control_pressed(const HostControl& control,
	const std::string& guid, bool was_pressed) const
{
	if(!is_controller_control(control))
	{
		return false;
	}

	for(const OpenController& open : m_open_controllers)
	{
		if(open.controller == nullptr || open.guid != guid)
		{
			continue;
		}

		if(control.type == HostControlType::ControllerButton)
		{
			if(SDL_GameControllerGetButton(open.controller,
				static_cast<SDL_GameControllerButton>(control.code)) != 0)
			{
				return true;
			}
			continue;
		}

		const Sint16 value = SDL_GameControllerGetAxis(open.controller,
			static_cast<SDL_GameControllerAxis>(control.code));
		const Sint16 threshold = was_pressed
			? kAxisReleaseThreshold
			: kAxisPressThreshold;
		if(control.direction > 0 && value >= threshold)
		{
			return true;
		}
		if(control.direction < 0 && value <= -threshold)
		{
			return true;
		}
	}
	return false;
}

bool InputManager::any_controller_control_pressed(const HostControl& control,
	bool was_pressed) const
{
	if(!is_controller_control(control))
	{
		return false;
	}

	for(const OpenController& open : m_open_controllers)
	{
		if(open.controller == nullptr)
		{
			continue;
		}

		if(control.type == HostControlType::ControllerButton)
		{
			if(SDL_GameControllerGetButton(open.controller,
				static_cast<SDL_GameControllerButton>(control.code)) != 0)
			{
				return true;
			}
			continue;
		}

		const Sint16 value = SDL_GameControllerGetAxis(open.controller,
			static_cast<SDL_GameControllerAxis>(control.code));
		const Sint16 threshold = was_pressed
			? kAxisReleaseThreshold
			: kAxisPressThreshold;
		if(control.direction > 0 && value >= threshold)
		{
			return true;
		}
		if(control.direction < 0 && value <= -threshold)
		{
			return true;
		}
	}
	return false;
}

bool InputManager::player_binding_pressed(const PlayerInputSettings& player,
	const HostControl& binding, bool was_pressed) const
{
	if(binding.type == HostControlType::None)
	{
		return false;
	}

	if(player.device.type == InputDeviceType::Keyboard)
	{
		return binding.type == HostControlType::Key &&
			binding.code >= 0 && binding.code < SDL_NUM_SCANCODES &&
			m_keys[static_cast<size_t>(binding.code)];
	}

	if(player.device.type == InputDeviceType::Controller)
	{
		return controller_control_pressed(binding, player.device.guid,
			was_pressed);
	}

	return false;
}

bool InputManager::shortcut_pressed(const Shortcut& shortcut, size_t index)
{
	bool keyboard = false;
	if(shortcut.keyboard.type == HostControlType::Key &&
	   shortcut.keyboard.code >= 0 &&
	   shortcut.keyboard.code < SDL_NUM_SCANCODES)
	{
		keyboard = m_keys[static_cast<size_t>(shortcut.keyboard.code)];
	}

	const bool controller = any_controller_control_pressed(
		shortcut.controller, m_shortcut_controller_raw[index]);
	m_shortcut_controller_raw[index] = controller;
	return keyboard || controller;
}

void InputManager::set_ui_blocked(bool blocked)
{
	m_ui_blocked = blocked;
}

void InputManager::update()
{
	// While a binding is being captured, key/button presses belong to the
	// capture, not to the game. The same applies while a file dialog or
	// another modal UI owns the input. The raw state is still tracked so axis
	// hysteresis and release edges stay coherent.
	const bool suspended = capturing() || m_ui_blocked;

	for(size_t player = 0; player < m_settings.players.size(); ++player)
	{
		const PlayerInputSettings& settings = m_settings.players[player];
		std::array<bool, 8> raw {};

		for(size_t i = 0; i < raw.size(); ++i)
		{
			raw[i] = player_binding_pressed(settings, settings.bindings[i],
				m_raw_pressed[player][i]);
		}
		m_raw_pressed[player] = raw;

		const std::array<bool, 4> physical = {
			raw[static_cast<size_t>(GbButton::Right)],
			raw[static_cast<size_t>(GbButton::Left)],
			raw[static_cast<size_t>(GbButton::Up)],
			raw[static_cast<size_t>(GbButton::Down)],
		};
		m_directions[player].update(physical,
			m_settings.allow_opposite_directions);
		const std::array<bool, 4>& resolved =
			m_directions[player].resolved();

		std::array<bool, 8> logical = raw;
		logical[static_cast<size_t>(GbButton::Right)] = resolved[0];
		logical[static_cast<size_t>(GbButton::Left)] = resolved[1];
		logical[static_cast<size_t>(GbButton::Up)] = resolved[2];
		logical[static_cast<size_t>(GbButton::Down)] = resolved[3];
		if(suspended)
		{
			logical.fill(false);
		}
		m_logical_pressed[player] = logical;
	}

	for(size_t i = 0; i < m_settings.shortcuts.size(); ++i)
	{
		const bool pressed = shortcut_pressed(m_settings.shortcuts[i], i);
		if(!suspended && pressed && !m_shortcut_previous[i])
		{
			m_shortcut_trigger[i] = true;
		}
		m_shortcut_previous[i] = pressed;
	}
}

bool InputManager::player_pressed(size_t player, GbButton button) const
{
	const size_t index = static_cast<size_t>(button);
	if(player >= m_logical_pressed.size() || index >= 8)
	{
		return false;
	}
	return m_logical_pressed[player][index];
}

bool InputManager::consume_shortcut(ShortcutAction action)
{
	const size_t index = static_cast<size_t>(action);
	if(index >= m_shortcut_trigger.size())
	{
		return false;
	}
	const bool triggered = m_shortcut_trigger[index];
	m_shortcut_trigger[index] = false;
	return triggered;
}

bool InputManager::shortcut_held(ShortcutAction action) const
{
	const size_t index = static_cast<size_t>(action);
	if(index >= m_shortcut_previous.size() || capturing() || m_ui_blocked)
	{
		return false;
	}
	return m_shortcut_previous[index];
}

void InputManager::begin_capture(CaptureKind kind, std::string description,
	std::string controller_guid)
{
	m_capture.active = true;
	m_capture.done = false;
	m_capture.cleared = false;
	m_capture.kind = kind;
	m_capture.control = {};
	m_capture.description = std::move(description);
	m_capture.controller_guid = std::move(controller_guid);
}

void InputManager::cancel_capture()
{
	m_capture = {};
}

bool InputManager::capturing() const
{
	return m_capture.active || m_capture.done;
}

bool InputManager::take_capture(HostControl& control, bool& cleared)
{
	if(!m_capture.done)
	{
		return false;
	}

	control = m_capture.control;
	cleared = m_capture.cleared;
	m_capture = {};

	// The key/button used for the capture may still be held. Re-arm the
	// shortcuts against the current state so it does not fire immediately.
	for(size_t i = 0; i < m_settings.shortcuts.size(); ++i)
	{
		m_shortcut_previous[i] =
			shortcut_pressed(m_settings.shortcuts[i], i);
		m_shortcut_trigger[i] = false;
	}
	return true;
}
