#include <SDL2/SDL.h>
#include "sdl_renderer_backend.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_sdlrenderer2.h"

SDLRendererBackend::SDLRendererBackend()
{}

void SDLRendererBackend::init()
{
	SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER);
	m_window = SDL_CreateWindow("QtreGB", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 800, 600, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
	m_renderer = SDL_CreateRenderer(m_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::StyleColorsDark();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

	ImGui_ImplSDL2_InitForSDLRenderer(m_window, m_renderer);
	ImGui_ImplSDLRenderer2_Init(m_renderer);
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
	ImGui_ImplSDL2_ProcessEvent(static_cast<const SDL_Event*>(event));
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
	void* id = reinterpret_cast<void*>(texture);
	m_textures[id] = texture;
	return id;
}

void SDLRendererBackend::updateTexture(void *textureId, const uint32_t *pixels, int width, int height)
{
	auto it = m_textures.find(textureId);
	if(it == m_textures.end())
	{
		return;
	}

	SDL_Texture* texture = it->second;
	void* dst;
	int pitch;
	if(SDL_LockTexture(texture, NULL, &dst, &pitch) == 0)
	{
		memcpy(dst, pixels, width * height * sizeof(uint32_t));
		SDL_UnlockTexture(texture);
	}
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
