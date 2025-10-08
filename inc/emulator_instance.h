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

// Resolución de Game Boy
constexpr int GB_XRES = 160;
constexpr int GB_YRES = 144;

constexpr int SCALE = 4;

// Tipos básicos
using u8 = uint8_t;
using u32 = uint32_t;

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

		void load_cart(vector<u8> cart);
		void tick();
		bool get_frame_ready();

	private:
		IGuiBackend* m_backend = nullptr;
		void* m_texture_id = nullptr;
		std::vector<u32> m_frame_buffer;
		std::unique_ptr<QtreGB> m_emulator;
		u8 m_buttonState[8] = {1,1,1,1,1,1,1,1};
		bool m_is_frame_ready;
		bool m_is_emulator_ready;

		void set_button(u8 button, bool released);
		void fill_rect(int x, int y, int w, int h, u32 color);

};

#endif
