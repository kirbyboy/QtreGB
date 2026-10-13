#ifndef QTREGB_SDL_BACKEND_H
#define QTREGB_SDL_BACKEND_H

#include<unordered_map>
#include <SDL2/SDL.h>
#include "ibackend.h"


class SDLRendererBackend : public IGuiBackend
{
	public:
		SDLRendererBackend();
		void init();
		void new_frame() override;
		void render_draw_data() override;
		void handle_event(const void* event) override;
		void shutdown() override;
		void* createTexture(int width, int height) override;
		void updateTexture(void* textureId, const uint32_t* pixels, int width, int height) override;
		void destroyTexture(void* textureId) override;

	private:
		SDL_Window* m_window;
		SDL_Renderer* m_renderer;
		std::unordered_map<void*, SDL_Texture*> m_textures;
};

#endif
