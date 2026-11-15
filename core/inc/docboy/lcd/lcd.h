#ifndef DOCBOYLCD_H
#define DOCBOYLCD_H

#include <cstdint>
#include <functional>

// LCD facade for the DocBoy PPU. The PPU owns timing and pushes one color
// index per visible pixel; the wrapper writes it into QtreGB's framebuffer.
class Lcd
{
	public:
		using Pixel = uint8_t;
		using PushCallback = std::function<void(Pixel)>;
		using FrameCallback = std::function<void()>;

		void set_push_callback(PushCallback cb) { m_push = std::move(cb); }
		void set_new_frame_callback(FrameCallback cb) { m_new_frame = std::move(cb); }

		void push_pixel(Pixel pixel)
		{
			if(m_push)
			{
				m_push(pixel);
			}
		}

		void turn_on_ppu() {}
		void turn_off_ppu() {}
		void new_frame()
		{
			if(m_new_frame)
			{
				m_new_frame();
			}
		}
		void reset_row_cursor() {}
		void clear() {}

	private:
		PushCallback m_push;
		FrameCallback m_new_frame;
};

#endif // DOCBOYLCD_H
