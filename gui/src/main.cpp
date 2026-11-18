#include <iostream>
#include <cstdio>
#include <string>
#include <iostream>
#include <fstream>
#include <vector>
#include <chrono>
#include <cmath>
#include <thread>
#include <unistd.h>

#include "QtreGB.h"
#include "cart.h"
#include "ui.h"
#include "logger.h"

#include <SDL2/SDL.h>
#include "sdl_renderer_backend.h"
#include "emulator_frontend.h"
#include "imgui_manager.h"
#include "app_config.h"

using namespace qtreGB;

using namespace std;

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
static bool run_emulated_frame(EmulatorFrontend& frontend)
{
    EmulatorInstance* primary = frontend.get_emulator();
    if(primary == nullptr)
    {
        return false;
    }

    for(int i = 0; i < GB_FRAME_TICKS; i++)
    {
        frontend.tick();
        if(primary->get_frame_ready())
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
	const AppConfig config = AppConfig::load_default();

	SDLRendererBackend backend;
	backend.init(config.window_width, config.window_height,
		config.window_maximized);

	ImGuiManager imguiManager;
	imguiManager.init();

	EmulatorFrontend frontend(&backend, config.display, config.audio,
		config.input);
	EmulatorInstance* emulator = frontend.get_emulator();

	bool running = true;
	SDL_Event event;

	using Clock = std::chrono::steady_clock;
	const Clock::duration frame_duration = std::chrono::duration_cast<Clock::duration>(
		std::chrono::duration<double>(GB_FRAME_SECONDS));
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
				frontend.handle_sdl_event(&event);
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
				if(run_emulated_frame(frontend))
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

			frontend.drain_audio();
		}
		else
		{
			accumulator = 0.0;
		}

		if(new_frame)
		{
			frontend.update_frame();
		}

		backend.new_frame();
		frontend.handle_input();
		frontend.draw_main_window();

		const Clock::time_point present_start = Clock::now();
		backend.render_draw_data();
		const Clock::time_point presented = Clock::now();
		const Clock::duration present_duration = presented - present_start;

		// If vsync really blocked, let the display drive presentation:
		// adding our own 59.7275 Hz sleep on top of a 60 Hz vblank creates a
		// cumulative drift and a missed vblank every few seconds. If the
		// driver reports vsync but returns immediately, keep the explicit
		// frame cap to avoid a busy loop.
		const int refresh_hz = backend.display_refresh_hz();
		const Clock::duration vsync_interval = refresh_hz > 0
			? std::chrono::duration_cast<Clock::duration>(
				std::chrono::duration<double>(1.0 / refresh_hz))
			: frame_duration;
		const bool vsync_blocked = backend.vsync_enabled() &&
			present_duration >= vsync_interval * 3 / 4;

		bool sleep_for_frame = false;
		switch(frontend.display_settings().present_mode)
		{
			case PresentMode::Auto:
				sleep_for_frame = !vsync_blocked;
				break;
			case PresentMode::Monitor:
				// Trust the driver: keep presenting at monitor rate. If the
				// driver lies about vsync, the user can switch back to Auto.
				sleep_for_frame = !backend.vsync_enabled();
				break;
			case PresentMode::GameBoy:
				sleep_for_frame = true;
				break;
		}
		if(sleep_for_frame)
		{
			const Clock::time_point next_present = present_start + frame_duration;
			if(presented < next_present)
			{
				std::this_thread::sleep_for(next_present - presented);
			}
		}
	}

	AppConfig saved = config;
	saved.display = frontend.display_settings();
	saved.audio = frontend.audio_settings();
	saved.input = frontend.input_settings();
	saved.window_maximized = backend.window_maximized();
	if(!saved.window_maximized && !backend.window_fullscreen())
	{
		// Keep the last restored size when the window was maximized or
		// fullscreen, instead of saving the monitor dimensions.
		saved.window_width = backend.window_width();
		saved.window_height = backend.window_height();
	}
	std::string config_error;
	if(!saved.save_default(config_error))
	{
		std::fprintf(stderr, "Could not save configuration: %s\n",
			config_error.c_str());
	}

	frontend.shutdown_input();
	backend.shutdown();


	return 0;
}
