#ifndef QTREGB_EMULATOR_FRONTEND_MANAGER_H
#define QTREGB_EMULATOR_FRONTEND_MANAGER_H

#include "imgui.h"
#include "emulator_instance.h"
#include "ibackend.h"

class EmulatorFrontend
{
	public:
		EmulatorFrontend(IGuiBackend* backend);
		void draw_main_window();
		void handle_input();

		EmulatorInstance* get_emulator();
	private:
		IGuiBackend* m_backend;
		std::unique_ptr<EmulatorInstance> m_emulator;

		std::string open_rom_dialog();
		void draw_layers_window();
		bool m_show_layers_window;

	enum GbButton : u8
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

	void handle_key(ImGuiKey imguiKey, GbButton gbButton);
};

#endif
