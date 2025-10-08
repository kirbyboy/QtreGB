#include "emulator_instance.h"

EmulatorInstance::EmulatorInstance(IGuiBackend *backend) :
m_backend(backend),
m_is_frame_ready(false),
m_is_emulator_ready(false)
{
	m_frame_buffer.resize(GB_XRES * SCALE * GB_YRES * SCALE, 0);

	// Crear textura usando el backend
	if(m_backend)
	{
		m_texture_id = m_backend->createTexture(GB_XRES * SCALE, GB_YRES * SCALE);
	}

	m_emulator = std::unique_ptr<QtreGB>(new QtreGB());
}

EmulatorInstance::~EmulatorInstance()
{
	// El backend puede encargarse de destruir la textura
	if(m_backend && m_texture_id)
	{
		m_backend->destroyTexture(m_texture_id);
		m_texture_id = nullptr;
	}
}

void EmulatorInstance::update_frame(const u32 *palette)
{
	if (!m_backend || !m_texture_id)
	{
		return;
	}

	std::vector<u8> video_buffer = m_emulator->m_ppu.get_video_buffer();

	SDL_Rect rc;
    rc.x = rc.y = 0;
    rc.w = rc.h = 0;

	for (int line_num = 0; line_num < PPU::YRES; line_num++)
	{
		for (int x = 0; x < PPU::XRES; x++)
		{
			rc.x = x * SCALE;
			rc.y = line_num * SCALE;
			rc.w = SCALE;
			rc.h = SCALE;

			fill_rect(rc.x, rc.y, rc.w, rc.h, palette[video_buffer[x + line_num * PPU::XRES]]);
		}
	}

	m_backend->updateTexture(m_texture_id, m_frame_buffer.data(), GB_XRES * 4, GB_YRES * 4);
}

void *EmulatorInstance::get_texture_ID() const
{
	return m_texture_id;
}

std::pair<int, int> EmulatorInstance::get_texture_size() const
{
	return {GB_XRES * SCALE, GB_YRES * SCALE};
}

void EmulatorInstance::press_button(u8 button)
{
	set_button(button, true);
}

void EmulatorInstance::release_button(u8 button)
{
	set_button(button, false);
}

bool EmulatorInstance::is_button_pressed(u8 button) const
{
	if(button < static_cast<u8>(GbButton::Count))
	{
		return m_buttonState[button] == 0;
	}
	return false;
}

void EmulatorInstance::load_cart(vector<u8> cart)
{
	m_emulator = std::unique_ptr<QtreGB>(new QtreGB());
	m_emulator->load_cart(cart);
	m_is_emulator_ready = true;
}

void EmulatorInstance::tick()
{
	if(m_is_emulator_ready)
	{
		m_emulator->tick();
	}
}

bool EmulatorInstance::get_frame_ready()
{
	if(m_is_emulator_ready)
	{
		return m_emulator->get_frame_ready();
	}
	return false;
}

void EmulatorInstance::set_button(u8 button, bool released)
{
	if(button >= 8)
	{
		return;
	}

	m_buttonState[button] = released ? 1 : 0;
	switch(button)
	{
		case 0: m_emulator->m_gamepad.m_button_right = released; break;
		case 1: m_emulator->m_gamepad.m_button_left = released; break;
		case 2: m_emulator->m_gamepad.m_button_up = released; break;
		case 3: m_emulator->m_gamepad.m_button_down = released; break;
		case 4: m_emulator->m_gamepad.m_button_a = released; break;
		case 5: m_emulator->m_gamepad.m_button_b = released; break;
		case 6: m_emulator->m_gamepad.m_button_start = released; break;
		case 7: m_emulator->m_gamepad.m_button_select = released; break;
	}
}

void EmulatorInstance::fill_rect(int x, int y, int w, int h, u32 color)
{
    int maxW = GB_XRES * SCALE;
    int maxH = GB_YRES * SCALE;

    // Limitar los bordes del rectángulo
    int x0 = std::max(0, x);
    int y0 = std::max(0, y);
    int x1 = std::min(x + w, maxW);
    int y1 = std::min(y + h, maxH);

    for (int yy = y0; yy < y1; yy++)
    {
        for (int xx = x0; xx < x1; xx++)
        {
            m_frame_buffer[yy * maxW + xx] = color;
        }
    }
}
