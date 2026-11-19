#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <vector>
#include <algorithm>
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
#include "i18n.h"

using namespace qtreGB;

using namespace std;

static std::string resolve_language_directory()
{
	if(const char* override_path = std::getenv("QTR_LANG_DIR"))
	{
		if(override_path[0] != '\0')
		{
			return std::filesystem::u8path(override_path).u8string();
		}
	}

	std::vector<std::filesystem::path> candidates;
	if(char* base = SDL_GetBasePath())
	{
		const std::filesystem::path base_path = std::filesystem::u8path(base);
		candidates.push_back(base_path / "lang");
		candidates.push_back(base_path / ".." / "lang");
		candidates.push_back(base_path / ".." / "share" / "QtreGB" / "lang");
		candidates.push_back(base_path / ".." / "Resources" / "lang");
		SDL_free(base);
	}
	std::error_code error;
	candidates.push_back(std::filesystem::current_path(error) / "lang");

	for(const std::filesystem::path& candidate : candidates)
	{
		if(std::filesystem::is_directory(candidate, error))
		{
			return candidate.u8string();
		}
	}
	return "lang";
}

constexpr int GB_FRAME_TICKS = 70224;
constexpr double GB_CLOCK_HZ = 4194304.0;
constexpr double GB_FRAME_SECONDS = GB_FRAME_TICKS / GB_CLOCK_HZ;
constexpr double MAX_CATCHUP_SECONDS = 0.25;
constexpr int MAX_FRAMES_PER_LOOP = 4;
constexpr int MAX_FAST_FRAMES_PER_LOOP = 240;
constexpr double MAX_FAST_FORWARD_BUDGET_SECONDS = 0.012;
constexpr double MAX_FAST_FORWARD_DISPLAY_FRACTION = 0.75;

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
            frontend.record_emulated_frame();
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

	i18n::set_language_directory(resolve_language_directory());
	i18n::set_locale(config.language);

	EmulatorFrontend frontend(&backend, config.display, config.audio,
		config.input, config.paths);
	frontend.set_language(config.language);
	frontend.set_fast_forward_speed(config.fast_forward_speed);
	frontend.set_recent_roms(config.recent_roms);
	EmulatorInstance* emulator = frontend.get_emulator();

	bool running = true;
	SDL_Event event;

	using Clock = std::chrono::steady_clock;
	const Clock::duration frame_duration = std::chrono::duration_cast<Clock::duration>(
		std::chrono::duration<double>(GB_FRAME_SECONDS));
	Clock::time_point previous = Clock::now();
	double accumulator = 0.0;

	while (running && !frontend.should_close())
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
		bool new_frame = false;
		const int refresh_hz = backend.display_refresh_hz();
		const bool fast_forward = frontend.fast_forward_active();
		const FastForwardSpeed fast_speed = frontend.fast_forward_speed();
		if(emulator->is_ready())
		{
			if(fast_forward && fast_speed == FastForwardSpeed::Max)
			{
				// Run as much as fits in roughly three quarters of the display
				// interval, then let presentation/sleep give the CPU some
				// breathing room.
				const double display_seconds = refresh_hz > 0
					? 1.0 / refresh_hz : GB_FRAME_SECONDS;
				const double budget = std::min(MAX_FAST_FORWARD_BUDGET_SECONDS,
					display_seconds * MAX_FAST_FORWARD_DISPLAY_FRACTION);
				const Clock::time_point fast_start = Clock::now();
				int frames = 0;
				do
				{
					if(run_emulated_frame(frontend))
					{
						new_frame = true;
					}
					++frames;
					const double elapsed = std::chrono::duration<double>(
						Clock::now() - fast_start).count();
					if(elapsed >= budget || frames >= MAX_FAST_FRAMES_PER_LOOP)
					{
						break;
					}
				} while(true);
				accumulator = 0.0;
			}
			else
			{
				const double multiplier = !fast_forward ? 1.0 :
					(fast_speed == FastForwardSpeed::X300 ? 3.0 : 2.0);
				accumulator += delta * multiplier;
				const int max_frames = fast_forward
					? MAX_FRAMES_PER_LOOP * 3 : MAX_FRAMES_PER_LOOP;
				int frames = 0;
				while(accumulator >= GB_FRAME_SECONDS && frames < max_frames)
				{
					if(run_emulated_frame(frontend))
					{
						new_frame = true;
					}
					accumulator -= GB_FRAME_SECONDS;
					frames++;
				}

				// Do not let a long stall build an ever-growing backlog.
				if(frames == max_frames && accumulator >= GB_FRAME_SECONDS)
				{
					accumulator = std::fmod(accumulator, GB_FRAME_SECONDS);
				}
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
	saved.paths = frontend.path_settings();
	saved.fast_forward_speed = frontend.fast_forward_speed();
	saved.recent_roms = frontend.recent_roms();
	saved.language = frontend.language();
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
