#ifndef QTREGB_EMULATOR_INSTANCE_H
#define QTREGB_EMULATOR_INSTANCE_H

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <SDL2/SDL.h>

#include "QtreGB.h"
#include "audio_settings.h"
#include "display_processor.h"
#include "ibackend.h"
#include "input_state.h"
#include "path_settings.h"
#include "state_store.h"

using namespace qtreGB;

// Game Boy resolution
constexpr int GB_XRES = 160;
constexpr int GB_YRES = 144;

// One EmulatorInstance owns exactly one QtreGB machine. It keeps no mutable
// state shared with other instances, so the frontend can create a second one
// for a future link-cable session. A future LinkCable should connect the two
// QtreGB machines, not the GUI wrappers.
class EmulatorInstance
{
	public:
		EmulatorInstance(IGuiBackend* backend, u8 player_index = 1);
		~EmulatorInstance();
		void update_frame(const DisplaySettings& settings,
			double seconds = 1.0 / 59.7275);
		void refresh_display(const DisplaySettings& settings);
		const std::vector<u32>& display_pixels() const;
		void* get_texture_ID() const;
		std::pair<int,int> get_texture_size() const;

		void press_button(u8 button);
		void release_button(u8 button);
		bool is_button_pressed(u8 button) const;

		void load_cart(std::vector<u8> cart, std::string rom_path = {});
		// Stores a boot ROM. It is applied on the next load_cart()/reset().
		bool load_boot_rom(std::vector<u8> boot_rom);
		void clear_boot_rom();
		void reset();
		void set_audio_settings(const AudioSettings& settings);
		const AudioSettings& audio_settings() const { return m_audio_settings; }
		void set_audio_output_enabled(bool enabled);
		bool audio_output_enabled() const { return m_audio_output_enabled; }
		void set_path_settings(const PathSettings& settings);
		const PathSettings& path_settings() const { return m_paths; }
		void set_state_slot(int slot);
		void save_battery_now() { save_battery(); }

		// Savestate container next to the ROM: <rom>.sr<slot>. The core blob
		// is wrapped with ROM/BIOS identity, timestamp, emulated frames and an
		// optional RGBA preview.
		bool save_state(std::uint64_t emulated_frames = 0);
		bool load_state(SavestateInfo* info = nullptr);
		std::string state_path() const;
		std::string battery_path() const;
		u8 player_index() const { return m_player_index; }

		// The machine owned by this instance. Used by future link/debug code;
		// normal UI code should keep using the high-level methods above.
		QtreGB& machine() { return *m_machine; }
		const QtreGB& machine() const { return *m_machine; }
		bool has_boot_rom() const { return !m_boot_rom.empty(); }
		bool boot_rom_active() const { return m_machine->has_boot_rom(); }
		const std::vector<u8>& boot_rom() const { return m_boot_rom; }
		const std::vector<u8>& rom_data() const { return m_cart; }
		const std::string& rom_path() const { return m_rom_path; }
		void tick();
		bool get_frame_ready();
		bool is_ready() const { return m_is_emulator_ready; }
		void drain_audio();
		void discard_audio();

	private:
		IGuiBackend* m_backend = nullptr;
		void* m_texture_id = nullptr;
		SDL_AudioDeviceID m_audio_device = 0;
		AudioSettings m_audio_settings;
		bool m_audio_output_enabled = true;
		PathSettings m_paths;
		DisplayProcessor m_display;
		std::unique_ptr<QtreGB> m_machine;
		std::vector<u8> m_cart;
		std::vector<u8> m_boot_rom;
		std::string m_rom_path;
		u8 m_player_index = 1;
		InputState m_input;
		StateStore m_state;
		bool m_is_frame_ready;
		bool m_is_emulator_ready;

		bool init_audio();
		void update_audio_device_state();
		void rebuild_machine();
		void save_battery();
		void load_battery();
		SavestateInfo current_state_info(std::uint64_t emulated_frames) const;
		void set_button(u8 button, bool pressed);
		void apply_input();

};

#endif
