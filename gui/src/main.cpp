#include <iostream>
#include <cstdio>
#include <string>
#include <iostream>
#include <fstream>
#include <vector>
#include <chrono>
#include <cmath>
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

constexpr int GB_FRAME_TICKS = 70224;
constexpr double GB_CLOCK_HZ = 4194304.0;
constexpr double GB_FRAME_SECONDS = GB_FRAME_TICKS / GB_CLOCK_HZ;
constexpr double MAX_CATCHUP_SECONDS = 0.25;
constexpr int MAX_FRAMES_PER_LOOP = 4;

// Runs the emulator until the PPU completes a frame. The PPU signals the
// frame callback at the start of line 0, when the video buffer contains a
// complete frame. Stopping there is essential: reading the buffer after a
// fixed number of T-cycles can catch the PPU mid-frame, mixing rows from the
// new and previous frames (a fixed horizontal seam that only shows while
// scrolling).
static bool run_emulated_frame(EmulatorInstance& emulator)
{
    for(int i = 0; i < GB_FRAME_TICKS; i++)
    {
        emulator.tick();
        if(emulator.get_frame_ready())
        {
            return true;
        }
    }

    // The LCD may be off, in which case the PPU does not produce frames.
    // Still consume one frame worth of CPU cycles so emulation keeps pace.
    return false;
}

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
	EmulatorInstance* emulator = frontend.get_emulator();

	bool running = true;
	SDL_Event event;

	using Clock = std::chrono::steady_clock;
	Clock::time_point previous = Clock::now();
	double accumulator = 0.0;

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

		// Emulation runs on its own clock (59.7275 Hz), not on the monitor's
		// refresh rate. Rendering still happens once per loop and vsync makes
		// it wait for the display, so the emulation speed stays correct on
		// 50 Hz, 60 Hz, 144 Hz, etc. monitors.
		const Clock::time_point now = Clock::now();
		double delta = std::chrono::duration<double>(now - previous).count();
		previous = now;
		if(delta < 0.0)
		{
			delta = 0.0;
		}
		if(delta > MAX_CATCHUP_SECONDS)
		{
			delta = MAX_CATCHUP_SECONDS;
		}
		accumulator += delta;

		bool new_frame = false;
		if(emulator->is_ready())
		{
			int frames = 0;
			while(accumulator >= GB_FRAME_SECONDS && frames < MAX_FRAMES_PER_LOOP)
			{
				if(run_emulated_frame(*emulator))
				{
					new_frame = true;
				}
				accumulator -= GB_FRAME_SECONDS;
				frames++;
			}

			// Do not let a long stall build an ever-growing backlog.
			if(frames == MAX_FRAMES_PER_LOOP && accumulator >= GB_FRAME_SECONDS)
			{
				accumulator = std::fmod(accumulator, GB_FRAME_SECONDS);
			}

			emulator->drain_audio();
		}
		else
		{
			accumulator = 0.0;
		}

		if(new_frame)
		{
			emulator->update_frame(GB_PALETTE);
		}

		backend.new_frame();
		frontend.handle_input();
		frontend.draw_main_window();
		backend.render_draw_data();

		// Vsync normally blocks in render_draw_data(). If the backend could
		// not enable it, avoid a busy loop with a ~60 Hz fallback. The second
		// guard covers minimized windows, where some drivers stop blocking on
		// present.
		if(!backend.vsync_enabled())
		{
			SDL_Delay(16);
		}
		else if(delta < 0.001)
		{
			SDL_Delay(1);
		}
	}

	backend.shutdown();


	return 0;
}
