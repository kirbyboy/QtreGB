#include <SDL2/SDL.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "sdl_renderer_backend.h"

namespace
{
int find_render_driver(const char* name)
{
	if(name == nullptr || name[0] == '\0' || std::strcmp(name, "auto") == 0)
	{
		return -1;
	}

	const int count = SDL_GetNumRenderDrivers();
	for(int i = 0; i < count; i++)
	{
		SDL_RendererInfo info{};
		if(SDL_GetRenderDriverInfo(i, &info) == 0 && std::strcmp(info.name, name) == 0)
		{
			return i;
		}
	}
	return -1;
}
}
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_sdlrenderer2.h"

SDLRendererBackend::SDLRendererBackend()
{}

void SDLRendererBackend::init(int width, int height, bool maximized)
{
	SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER |
		SDL_INIT_GAMECONTROLLER);
	SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1");

	width = std::max(160, width);
	height = std::max(144, height);
	m_window = SDL_CreateWindow("QtreGB", SDL_WINDOWPOS_CENTERED,
		SDL_WINDOWPOS_CENTERED, width, height,
		SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
	if(m_window != nullptr && maximized)
	{
		SDL_MaximizeWindow(m_window);
	}

	const char* requested = std::getenv("QTR_RENDERER");
	const int driver_index = find_render_driver(requested);
	Uint32 renderer_flags = SDL_RENDERER_PRESENTVSYNC;
	if(requested == nullptr || std::strcmp(requested, "software") != 0)
	{
		renderer_flags |= SDL_RENDERER_ACCELERATED;
	}

	m_renderer = SDL_CreateRenderer(m_window, driver_index, renderer_flags);
	if(m_renderer == nullptr && driver_index >= 0)
	{
		std::fprintf(stderr, "Could not create renderer '%s'; falling back to auto.\n", requested);
		m_renderer = SDL_CreateRenderer(m_window, -1,
			SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	}

	if(m_renderer != nullptr)
	{
		// The creation flag is only a request; enforce it explicitly and then
		// query the renderer, since some drivers silently ignore it.
		const int vsync_result = SDL_RenderSetVSync(m_renderer, 1);
		SDL_RendererInfo renderer_info{};
		if(SDL_GetRendererInfo(m_renderer, &renderer_info) == 0)
		{
			m_vsync = (renderer_info.flags & SDL_RENDERER_PRESENTVSYNC) != 0;
			if(std::getenv("QTR_RENDER_INFO") != nullptr)
			{
				const char* video_driver = SDL_GetCurrentVideoDriver();
				std::fprintf(stderr,
					"Video driver: %s\nRenderer: %s\nVSync: %s (request %s)\n",
					video_driver ? video_driver : "unknown",
					renderer_info.name,
					m_vsync ? "enabled" : "UNAVAILABLE",
					vsync_result == 0 ? "ok" : "failed");
			}
		}
	}

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	// QtreGB stores its settings in its own .ini; ImGui must not create a
	// separate imgui.ini in the working directory.
	ImGui::GetIO().IniFilename = nullptr;
	ImGui::StyleColorsDark();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

	ImGui_ImplSDL2_InitForSDLRenderer(m_window, m_renderer);
	ImGui_ImplSDLRenderer2_Init(m_renderer);
}

int SDLRendererBackend::window_width() const
{
	int width = 0;
	if(m_window != nullptr)
	{
		SDL_GetWindowSize(m_window, &width, nullptr);
	}
	return width;
}

int SDLRendererBackend::window_height() const
{
	int height = 0;
	if(m_window != nullptr)
	{
		SDL_GetWindowSize(m_window, nullptr, &height);
	}
	return height;
}

bool SDLRendererBackend::window_maximized() const
{
	return m_window != nullptr &&
		(SDL_GetWindowFlags(m_window) & SDL_WINDOW_MAXIMIZED) != 0;
}

bool SDLRendererBackend::window_fullscreen() const
{
	if(m_window == nullptr)
	{
		return false;
	}
	const Uint32 flags = SDL_GetWindowFlags(m_window);
	return (flags & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP)) != 0;
}

int SDLRendererBackend::display_refresh_hz() const
{
	if(m_window == nullptr)
	{
		return 0;
	}

	const int display = SDL_GetWindowDisplayIndex(m_window);
	SDL_DisplayMode mode{};
	if(display < 0 || SDL_GetCurrentDisplayMode(display, &mode) != 0)
	{
		return 0;
	}
	return mode.refresh_rate;
}

void SDLRendererBackend::new_frame()
{
	ImGui_ImplSDLRenderer2_NewFrame();
	ImGui_ImplSDL2_NewFrame();
	ImGui::NewFrame();
}

void SDLRendererBackend::render_draw_data()
{
	ImGui::Render();
	SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 255);
	SDL_RenderClear(m_renderer);
	ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), m_renderer);
	SDL_RenderPresent(m_renderer);
}

void SDLRendererBackend::handle_event(const void *event)
{
	const SDL_Event* sdl_event = static_cast<const SDL_Event*>(event);
	ImGui_ImplSDL2_ProcessEvent(sdl_event);
}

void SDLRendererBackend::toggle_fullscreen()
{
	if(m_window == nullptr)
	{
		return;
	}

	const Uint32 flags = SDL_GetWindowFlags(m_window);
	if((flags & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP)) != 0)
	{
		SDL_SetWindowFullscreen(m_window, 0);
		return;
	}

	const Uint32 mode = std::getenv("QTR_FULLSCREEN_EXCLUSIVE") != nullptr
		? SDL_WINDOW_FULLSCREEN
		: SDL_WINDOW_FULLSCREEN_DESKTOP;
	SDL_SetWindowFullscreen(m_window, mode);
}

void SDLRendererBackend::shutdown()
{
	ImGui_ImplSDLRenderer2_Shutdown();
	ImGui_ImplSDL2_Shutdown();
	ImGui::DestroyContext();

	SDL_DestroyRenderer(m_renderer);
	SDL_DestroyWindow(m_window);
	SDL_Quit();
}

void *SDLRendererBackend::createTexture(int width, int height)
{
	SDL_Texture* texture = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, width, height);
	if (!texture)
	{
		return nullptr;
	}
	SDL_SetTextureScaleMode(texture, SDL_ScaleModeNearest);
	void* id = reinterpret_cast<void*>(texture);
	m_textures[id] = texture;
	return id;
}

void SDLRendererBackend::updateTexture(void *textureId, const uint32_t *pixels, int width, int height)
{
	(void)height;
	auto it = m_textures.find(textureId);
	if(it == m_textures.end())
	{
		return;
	}

	// SDL_UpdateTexture handles the destination pitch and driver-specific
	// synchronization internally. The manual LockTexture/memcpy path can
	// leave a visible seam with some SDL2 renderer drivers.
	SDL_UpdateTexture(it->second, nullptr, pixels,
		width * static_cast<int>(sizeof(uint32_t)));
}

void SDLRendererBackend::destroyTexture(void *textureId)
{
	auto it = m_textures.find(textureId);
	if(it != m_textures.end())
	{
		SDL_DestroyTexture(it->second);
		m_textures.erase(it);
	}
}
