#ifndef QTREGB_EMULATOR_FRONTEND_MANAGER_H
#define QTREGB_EMULATOR_FRONTEND_MANAGER_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "imgui.h"
#include "audio_settings.h"
#include "display_settings.h"
#include "emulation_settings.h"
#include "emulator_instance.h"
#include "fps_counter.h"
#include "input_manager.h"
#include "input_settings.h"
#include "path_settings.h"
#include "recent_files.h"
#include "session_state.h"
#include "link_session.h"
#include "ibackend.h"

class EmulatorFrontend
{
	public:
		EmulatorFrontend(IGuiBackend* backend,
			const DisplaySettings& settings = DisplaySettings {},
			const AudioSettings& audio_settings = AudioSettings {},
			const InputSettings& input_settings = InputSettings::defaults(),
			const PathSettings& path_settings = PathSettings {});
		~EmulatorFrontend();
		void draw_main_window();
		void handle_input();
		void handle_sdl_event(const void* event);
		// Releases SDL controllers before the backend calls SDL_Quit().
		void shutdown_input();
		void set_language(const std::string& language);
		const std::string& language() const { return m_language; }
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
		void set_link_audio_source(LinkAudioSource source);
		void set_input_settings(const InputSettings& settings);
		const InputSettings& input_settings() const { return m_input_settings; }
		void set_path_settings(const PathSettings& settings);
		const PathSettings& path_settings() const { return m_path_settings; }
		void set_recent_roms(const RecentFiles& recent)
		{
			m_recent_roms = recent;
		}
		const RecentFiles& recent_roms() const { return m_recent_roms; }
		bool link_session_active() const;
		int state_slot() const { return m_state_slot; }
		bool fast_forward_active() const;
		// Called by the main loop for every completed emulated frame, so the
		// status bar can report real emulation speed (not display rate).
		void record_emulated_frame();
		// Negative when there is no fresh measurement (startup, LCD off).
		double emulated_fps() const;
		bool should_close() const { return m_should_close; }
		FastForwardSpeed fast_forward_speed() const
		{
			return m_fast_forward_speed;
		}
		void set_fast_forward_speed(FastForwardSpeed speed);

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
		struct PreviewTexture
		{
			void* id = nullptr;
			int width = 0;
			int height = 0;
		};
		struct SlotPreview
		{
			bool exists = false;
			bool readable = false;
			std::uint64_t file_size = 0;
			std::string path;
			SavestateInfo info;
			PreviewTexture texture;
		};
		struct SessionPreview
		{
			bool exists = false;
			bool readable = false;
			std::uint64_t file_size = 0;
			std::string path;
			SessionState state;
			PreviewTexture texture;
		};
		IGuiBackend* m_backend;
		// Always has at least one element; the second one is optional.
		std::vector<std::unique_ptr<EmulatorInstance>> m_instances;
		// One cable per connected pair, not one per instance.
		std::unique_ptr<LinkSession> m_link;
		DisplaySettings m_display_settings;
		AudioSettings m_audio_settings;
		InputSettings m_input_settings;
		PathSettings m_path_settings;
		RecentFiles m_recent_roms;
		PathSettings m_path_edit;
		int m_state_slot = 0;
		std::uint64_t m_emulated_frames = 0;
		FastForwardSpeed m_fast_forward_speed = FastForwardSpeed::X200;
		FpsCounter m_fps;
		std::vector<u8> m_bios;
		std::vector<u8> m_session_bios;
		std::string m_session_bios_path;
		InputManager m_input_manager;
		std::string m_language = "system";
		bool m_show_controllers_window = false;
		bool m_show_key_bindings_window = false;
		bool m_show_paths_window = false;
		bool m_show_state_browser = false;
		bool m_state_browser_dirty = true;
		bool m_state_browser_link_mode = false;
		std::string m_state_browser_rom_path;
		std::array<SlotPreview, 10> m_slot_previews;
		SessionPreview m_session_preview;
		bool m_should_close = false;

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

		void load_rom_player2(const std::string& filename);
		bool start_link_session(const std::string& rom_path);
		void stop_link_session();
		void reset_link_transfer();
		bool build_session_state(SessionState& state,
			std::string& error) const;
		std::string session_state_path() const;
		bool save_session_state(std::string& error);
		bool load_session_state(std::string& error);
		bool locate_session_rom(const SessionRomInfo& expected,
			std::vector<u8>& rom, std::string& found_path,
			std::string& error) const;
		void ensure_combined_texture();
		void upload_combined_frame();
		bool compose_combined_frame(std::vector<u32>& frame) const;
		void draw_custom_palette_window();
		enum class FileDialogAction
		{
			None,
			OpenRom,
			OpenRomPlayer2,
			SaveScreenshot,
			ChooseRomsDirectory,
			ChooseBiosFile,
			ChooseBatteryDirectory,
			ChooseSavestatesDirectory,
			ChooseScreenshotsDirectory,
		};
		FileDialogAction m_file_dialog_action = FileDialogAction::None;
		std::string m_last_file_directory;
		void open_file_dialog(FileDialogAction action);
		void draw_file_dialogs();
		void handle_file_dialog_result(FileDialogAction action,
			const std::string& path);
		void open_rom_path(const std::string& path);
		void open_recent_rom(size_t index);
		void save_screenshot_to_path(const std::string& path);
		bool load_global_bios(std::string& error);
		void apply_bios_to_instance(EmulatorInstance& instance,
			const std::vector<u8>& bios);
		void apply_global_bios_to_primary();
		std::string file_dialog_directory() const;
		std::string preferred_directory(FileDialogAction action) const;
		void remember_file_directory(const std::string& path);
		void open_paths_window();
		void draw_paths_window();
		void open_directory_dialog(FileDialogAction action);
		void apply_directory_choice(FileDialogAction action,
			const std::string& path);
		bool paths_edit_valid() const;
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
		void apply_audio_routing();
		void open_state_browser();
		void draw_state_browser();
		void refresh_state_browser();
		void destroy_state_previews();
		void update_preview_texture(PreviewTexture& texture,
			const std::vector<std::uint32_t>& pixels, int width, int height);
		void set_state_slot(int slot);
		void change_state_slot(int delta);
		float main_status_bar_height() const;
		void draw_main_status_bar();
		void save_screenshot();
		void save_state();
		bool load_state();
		bool m_open_custom_palette = false;
		std::array<Rgb, 4> m_custom_palette_edit {};
		std::string m_state_message;
		double m_state_message_until = 0.0;
};

#endif
