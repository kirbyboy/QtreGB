#include <iostream>
#include <cstdio>
#include <string>
#include <iostream>
#include <fstream>
#include <vector>
#include <unistd.h>

#include "QtreGB.h"
#include "cart.h"
#include "ui.h"
#include "logger.h"

#include <SDL2/SDL.h>
#include "sdl_renderer_backend.h"
#include "emulator_frontend.h"
#include "imgui_manager.h"

using namespace qtreGB;

using namespace std;

constexpr u32 GB_PALETTE[4] = {
    0xFFFFFFFF, // White -> color 0
    0xAAAAAAFF, // Light gray -> color 1
    0x555555FF, // Dark gray -> color 2
    0x000000FF  // Black -> color 3
};

/*
int main(int argc, char* argv[])
{
	QtreGB emu;
	const char * filename;
	if (argc < 2) {
		filename = "../roms/01-special.gb";
	}
	else
	{
		filename = argv[1];
	}

	string filenameStr = filename;

	ifstream myfile (filename, ios::binary);
	if (myfile.is_open())
	{
		myfile.seekg(0, ios::end);
		int file_size = myfile.tellg();

		vector<u8> rom;
		myfile.seekg(0, ios::beg);
		rom.resize(static_cast<std::size_t>(file_size));
		myfile.read(reinterpret_cast<char*>(&rom.front()), static_cast<std::size_t>(file_size));
		myfile.close();

		emu.load_cart(rom);
		//ui_init();
		bool exit = false;
		int counter = 0;

		ui_init_imgui();

		while(!exit)
		{
			emu.tick();
			//ui_update(&emu);

			ui_render(&emu);


			counter++;
		}
	}

	return 0;
}
*/

int main()
{
	SDLRendererBackend backend;
	backend.init();

	ImGuiManager imguiManager;
	imguiManager.init();

	EmulatorFrontend frontend(&backend);

	bool running = true;
	SDL_Event event;

	u32 FPS = 1000 / 60;

	while (running)
	{
		// Process SDL events
		while (SDL_PollEvent(&event))
		{

			if (event.type == SDL_QUIT)
			{
				running = false;
			}
			else
			{
				backend.handle_event(&event);
			}
		}

		bool frame_ready = false;
		auto start = SDL_GetTicks();   // or std::chrono
		while (!frame_ready)
		{
			frontend.get_emulator()->tick();
			frame_ready = frontend.get_emulator()->get_frame_ready();

			// exit condition: do not block the GUI
			auto now = SDL_GetTicks();
			if (now - start > FPS)  // ~16ms: time to refresh the GUI
				break;
		}

		// --- 2. Update framebuffer if there is one ---
		if (frame_ready)
		{
			frontend.get_emulator()->update_frame(GB_PALETTE);
		}

		backend.new_frame();
		frontend.handle_input();
		frontend.draw_main_window();
		backend.render_draw_data();
	}

	backend.shutdown();


	return 0;
}
