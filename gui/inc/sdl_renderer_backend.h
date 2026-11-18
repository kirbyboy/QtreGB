#ifndef QTREGB_SDL_BACKEND_H
#define QTREGB_SDL_BACKEND_H

#include<unordered_map>
#include <SDL2/SDL.h>
#include "ibackend.h"


class SDLRendererBackend : public IGuiBackend
{
	public:
		SDLRendererBackend();
		void init(int width, int height, bool maximized);
		int window_width() const;
		int window_height() const;
		bool window_maximized() const;
		bool window_fullscreen() const;
		int display_refresh_hz() const;
		void new_frame() override;
		void render_draw_data() override;
		void handle_event(const void* event) override;
		void shutdown() override;
		void* createTexture(int width, int height) override;
		void updateTexture(void* textureId, const uint32_t* pixels, int width, int height) override;
		void destroyTexture(void* textureId) override;
		bool vsync_enabled() const override { return m_vsync; }
		void toggle_fullscreen() override;

	private:
		SDL_Window* m_window;
		SDL_Renderer* m_renderer;
		bool m_vsync = false;
		std::unordered_map<void*, SDL_Texture*> m_textures;
};

#endif
