#ifndef QTREGB_EMULATOR_FRONTEND_MANAGER_H
#define QTREGB_EMULATOR_FRONTEND_MANAGER_H

#include <cstddef>
#include <memory>
#include <vector>

#include "imgui.h"
#include "audio_settings.h"
#include "display_settings.h"
#include "emulator_instance.h"
#include "input_manager.h"
#include "input_settings.h"
#include "link_session.h"
#include "ibackend.h"

class EmulatorFrontend
{
	public:
		EmulatorFrontend(IGuiBackend* backend,
			const DisplaySettings& settings = DisplaySettings {},
			const AudioSettings& audio_settings = AudioSettings {},
			const InputSettings& input_settings = InputSettings::defaults());
		~EmulatorFrontend();
		void draw_main_window();
		void handle_input();
		void handle_sdl_event(const void* event);
		// Releases SDL controllers before the backend calls SDL_Quit().
		void shutdown_input();
		// One T-cycle for the primary instance, or for both when linked.
		void tick();
		void drain_audio();
		// Single-screen texture or the combined 320x144 link texture.
		void update_frame();
		void* get_texture_ID();
		std::pair<int,int> get_texture_size() const;
		bool link_mode() const;
		DisplaySettings& display_settings() { return m_display_settings; }
		const DisplaySettings& display_settings() const { return m_display_settings; }
		void apply_display_settings();
		void set_audio_settings(const AudioSettings& settings);
		const AudioSettings& audio_settings() const { return m_audio_settings; }
		void set_input_settings(const InputSettings& settings);
		const InputSettings& input_settings() const { return m_input_settings; }

		// Primary instance, kept for the current single-screen UI.
		EmulatorInstance* get_emulator();
		EmulatorInstance* primary();
		EmulatorInstance* instance(size_t index);
		size_t instance_count() const { return m_instances.size(); }
		LinkSession* link() const { return m_link.get(); }

		// Extension point for a second player/link session. The new instance
		// gets its own QtreGB, ROM, input, audio device and texture. When it
		// becomes the second instance, a single shared LinkCable is created.
		EmulatorInstance* add_instance();

	private:
		IGuiBackend* m_backend;
		// Always has at least one element; the second one is optional.
		std::vector<std::unique_ptr<EmulatorInstance>> m_instances;
		// One cable per connected pair, not one per instance.
		std::unique_ptr<LinkSession> m_link;
		DisplaySettings m_display_settings;
		AudioSettings m_audio_settings;
		InputSettings m_input_settings;
		InputManager m_input_manager;
		bool m_show_controllers_window = false;
		bool m_show_key_bindings_window = false;

		enum class InputCaptureTarget
		{
			None,
			PlayerBinding,
			ShortcutKeyboard,
			ShortcutController,
		};
		InputCaptureTarget m_capture_target = InputCaptureTarget::None;
		size_t m_capture_player = 0;
		GbButton m_capture_button = GbButton::A;
		ShortcutAction m_capture_action = ShortcutAction::Fullscreen;

		// Two 160x144 machines composited side by side into one 320x144
		// texture, so the existing integer-scaling rules apply unchanged.
		void* m_combined_texture = nullptr;
		std::vector<u32> m_combined_frame;

		std::string open_rom_dialog();
		std::string open_bios_dialog();
		void load_rom_player2(const std::string& filename);
		void ensure_combined_texture();
		void upload_combined_frame();
		void draw_layers_window();
		void draw_custom_palette_window();
		void draw_controllers_window();
		void draw_key_bindings_window();
		float input_status_bar_height() const;
		void draw_input_status_bar(const char* primary_hint,
			const char* secondary_hint);
		void apply_input_capture();
		void start_player_capture(size_t player, GbButton button);
		void start_shortcut_capture(ShortcutAction action, bool keyboard_slot);
		void set_player_device(size_t player, const InputDeviceId& device);
		void notify(const std::string& message);
		void save_screenshot();
		void save_state();
		void load_state();
		bool m_show_layers_window;
		bool m_open_custom_palette = false;
		std::array<Rgb, 4> m_custom_palette_edit {};
		std::string m_bios_message;
		std::string m_state_message;
		double m_state_message_until = 0.0;
};

#endif
