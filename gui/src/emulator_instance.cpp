#include "emulator_instance.h"

#include <utility>

using namespace qtreGB;

EmulatorInstance::EmulatorInstance(IGuiBackend *backend) :
m_backend(backend),
m_is_frame_ready(false),
m_is_emulator_ready(false)
{
	m_frame_buffer.resize(GB_XRES * SCALE * GB_YRES * SCALE, 0);

	// Create texture using the backend
	if(m_backend)
	{
		m_texture_id = m_backend->createTexture(GB_XRES * SCALE, GB_YRES * SCALE);
	}

	m_emulator = std::unique_ptr<QtreGB>(new QtreGB());
	init_audio();
}

EmulatorInstance::~EmulatorInstance()
{
	save_battery();

	if(m_audio_device != 0)
	{
		SDL_CloseAudioDevice(m_audio_device);
		m_audio_device = 0;
	}

	// The backend may handle destroying the texture
	if(m_backend && m_texture_id)
	{
		m_backend->destroyTexture(m_texture_id);
		m_texture_id = nullptr;
	}
}

bool EmulatorInstance::init_audio()
{
	SDL_AudioSpec want{};
	want.freq = static_cast<int>(APU::SAMPLE_RATE);
	want.format = AUDIO_S16SYS;
	want.channels = 2;
	want.samples = 1024;
	want.callback = nullptr;	// SDL_QueueAudio

	m_audio_device = SDL_OpenAudioDevice(nullptr, 0, &want, nullptr, 0);
	if(m_audio_device == 0)
	{
		return false;
	}

	SDL_PauseAudioDevice(m_audio_device, 0);
	return true;
}

void EmulatorInstance::drain_audio()
{
	if(m_audio_device == 0 || !m_is_emulator_ready)
	{
		return;
	}

	// Do not let the queue grow without bound if the emulator runs faster
	// than the audio device consumes samples (about 100 ms of audio).
	const Uint32 max_queued = (APU::SAMPLE_RATE / 10) * 2 * sizeof(int16_t);
	if(SDL_GetQueuedAudioSize(m_audio_device) > max_queued)
	{
		return;
	}

	int16_t buffer[2048];	// 1024 stereo frames
	const size_t frames = m_emulator->m_apu.read_samples(buffer, 1024);
	if(frames == 0)
	{
		return;
	}

	SDL_QueueAudio(m_audio_device, buffer,
		static_cast<Uint32>(frames * 2 * sizeof(int16_t)));
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

void EmulatorInstance::load_cart(std::vector<u8> cart, std::string rom_path)
{
	save_battery();
	m_cart = std::move(cart);
	m_rom_path = std::move(rom_path);
	rebuild_machine();
}

bool EmulatorInstance::load_boot_rom(std::vector<u8> boot_rom)
{
	if(boot_rom.size() != 0x100)
	{
		return false;
	}
	m_boot_rom = std::move(boot_rom);
	return true;
}

void EmulatorInstance::reset()
{
	save_battery();
	rebuild_machine();
}

void EmulatorInstance::rebuild_machine()
{
	if(m_audio_device != 0)
	{
		SDL_ClearQueuedAudio(m_audio_device);
	}

	m_emulator = std::unique_ptr<QtreGB>(new QtreGB());
	if(!m_boot_rom.empty())
	{
		m_emulator->load_boot_rom(m_boot_rom);
	}
	if(!m_cart.empty())
	{
		m_emulator->load_cart(m_cart);
	}
	load_battery();
	m_is_emulator_ready = !m_cart.empty();
}

void EmulatorInstance::save_battery()
{
	if(!m_emulator || m_rom_path.empty())
	{
		return;
	}
	m_emulator->m_cart.save_battery_ram(Cart::battery_save_path(m_rom_path));
}

void EmulatorInstance::load_battery()
{
	if(!m_emulator || m_rom_path.empty())
	{
		return;
	}
	m_emulator->m_cart.load_battery_ram(Cart::battery_save_path(m_rom_path));
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

    // Clamp the rectangle borders
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
