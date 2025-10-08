#ifndef QTREGB_IBACKEND_H
#define QTREGB_IBACKEND_H

class IGuiBackend
{
	public:
		virtual ~IGuiBackend() = default;

		virtual void new_frame() = 0;
		virtual void render_draw_data() = 0;
		virtual void handle_event(const void* event) = 0;
		virtual void shutdown() = 0;
		virtual void* createTexture(int width, int height) = 0;
		virtual void updateTexture(void* texture_id, const uint32_t* pixels, int width, int height) = 0;
		virtual void destroyTexture(void* textureId) = 0;
};

#endif
