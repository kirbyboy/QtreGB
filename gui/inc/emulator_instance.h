#ifndef QTREGB_EMULATOR_INSTANCE_H
#define QTREGB_EMULATOR_INSTANCE_H

#include <vector>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <array>
#include <SDL2/SDL.h>

#include "QtreGB.h"
#include "ibackend.h"

using namespace qtreGB;

// Game Boy resolution
constexpr int GB_XRES = 160;
constexpr int GB_YRES = 144;

constexpr int SCALE = 4;

enum class GbButton : u8
{
	Right = 0,
	Left,
	Up,
	Down,
	A,
	B,
	Start,
	Select,
	Count
};

class EmulatorInstance
{
	private:
		static constexpr size_t ButtonCount = static_cast<size_t>(GbButton::Count);

	public:
		EmulatorInstance(IGuiBackend* backend);
		~EmulatorInstance();
		void update_frame(const u32* palette);
		void* get_texture_ID() const;
		std::pair<int,int> get_texture_size() const;

		void press_button(u8 button);
		void release_button(u8 button);
		bool is_button_pressed(u8 button) const;

		void load_cart(std::vector<u8> cart, std::string rom_path = {});
		// Stores a boot ROM. It is applied on the next load_cart()/reset().
		bool load_boot_rom(std::vector<u8> boot_rom);
		void reset();
		bool has_boot_rom() const { return !m_boot_rom.empty(); }
		bool boot_rom_active() const { return m_emulator->has_boot_rom(); }
		void tick();
		bool get_frame_ready();
		void drain_audio();

	private:
		IGuiBackend* m_backend = nullptr;
		void* m_texture_id = nullptr;
		SDL_AudioDeviceID m_audio_device = 0;
		std::vector<u32> m_frame_buffer;
		std::unique_ptr<QtreGB> m_emulator;
		std::vector<u8> m_cart;
		std::vector<u8> m_boot_rom;
		std::string m_rom_path;
		u8 m_buttonState[8] = {1,1,1,1,1,1,1,1};
		bool m_is_frame_ready;
		bool m_is_emulator_ready;

		bool init_audio();
		void rebuild_machine();
		void save_battery();
		void load_battery();
		void set_button(u8 button, bool released);
		void fill_rect(int x, int y, int w, int h, u32 color);

};

#endif
