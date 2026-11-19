#include "libretro.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "QtreGB.h"
#include "state_stream.h"

#ifndef QTREGB_BUILD_VERSION
#define QTREGB_BUILD_VERSION "unknown"
#endif

namespace
{

constexpr unsigned GB_WIDTH = 160;
constexpr unsigned GB_HEIGHT = 144;
constexpr int GB_FRAME_TICKS = 70224;
constexpr size_t LIBRETRO_STATE_SIZE = 320u * 1024u;

// DMG shades in XRGB8888 order (0x00RRGGBB). The luminance of these four
// values maps cleanly to the 0..3 indices expected by display shaders.
constexpr std::uint32_t DMG_PALETTE[4] = {
	0x00E0F8D0,
	0x0088C070,
	0x00346856,
	0x00081820,
};

std::unique_ptr<qtreGB::QtreGB> g_machine;
std::vector<std::uint32_t> g_video(static_cast<size_t>(GB_WIDTH) * GB_HEIGHT);
std::vector<std::int16_t> g_audio(qtreGB::APU::RING_FRAMES * 2);
std::vector<std::uint8_t> g_save_ram;
bool g_apply_save_ram = false;

retro_environment_t g_environment = nullptr;
retro_video_refresh_t g_video_callback = nullptr;
retro_audio_sample_t g_audio_sample_callback = nullptr;
retro_audio_sample_batch_t g_audio_batch_callback = nullptr;
retro_input_poll_t g_input_poll_callback = nullptr;
retro_input_state_t g_input_state_callback = nullptr;
unsigned g_controller_device = RETRO_DEVICE_JOYPAD;

void publish_input_info()
{
	if(g_environment == nullptr)
	{
		return;
	}

	static retro_input_descriptor descriptors[] = {
		{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,
			"D-Pad Up"},
		{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,
			"D-Pad Down"},
		{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,
			"D-Pad Left"},
		{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT,
			"D-Pad Right"},
		{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A, "A"},
		{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B, "B"},
		{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Start"},
		{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "Select"},
		{0, 0, 0, 0, nullptr},
	};
	g_environment(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, descriptors);

	static retro_controller_description controller_types[] = {
		{"Game Boy", RETRO_DEVICE_JOYPAD},
	};
	static retro_controller_info controller_ports[] = {
		{controller_types, 1},
		{nullptr, 0},
	};
	g_environment(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, controller_ports);
}

std::uint32_t read_le32(const std::uint8_t* data)
{
	return static_cast<std::uint32_t>(data[0]) |
		(static_cast<std::uint32_t>(data[1]) << 8) |
		(static_cast<std::uint32_t>(data[2]) << 16) |
		(static_cast<std::uint32_t>(data[3]) << 24);
}

bool load_system_bios(qtreGB::QtreGB& machine)
{
	if(g_environment == nullptr)
	{
		return false;
	}
	const char* directory = nullptr;
	if(!g_environment(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &directory) ||
	   directory == nullptr || directory[0] == '\0')
	{
		return false;
	}

	for(const char* name : {"dmg_boot.bin", "dmg_bios.bin"})
	{
		const std::filesystem::path path =
			std::filesystem::u8path(directory) / name;
		std::error_code error;
		if(!std::filesystem::is_regular_file(path, error) ||
		   std::filesystem::file_size(path, error) != 0x100)
		{
			continue;
		}
		std::ifstream input(path, std::ios::binary);
		if(!input)
		{
			continue;
		}
		std::vector<qtreGB::u8> bios(0x100);
		input.read(reinterpret_cast<char*>(bios.data()), 0x100);
		if(input.good() || input.eof())
		{
			return machine.load_boot_rom(std::move(bios));
		}
	}
	return false;
}

void poll_input()
{
	if(g_machine == nullptr)
	{
		return;
	}
	if(g_input_poll_callback != nullptr)
	{
		g_input_poll_callback();
	}

	const auto pressed = [](unsigned id)
	{
		return g_input_state_callback != nullptr &&
			g_input_state_callback(0, RETRO_DEVICE_JOYPAD, 0, id) != 0;
	};

	(void)g_controller_device;
	g_machine->m_gamepad.m_button_a = pressed(RETRO_DEVICE_ID_JOYPAD_A);
	g_machine->m_gamepad.m_button_b = pressed(RETRO_DEVICE_ID_JOYPAD_B);
	g_machine->m_gamepad.m_button_start =
		pressed(RETRO_DEVICE_ID_JOYPAD_START);
	g_machine->m_gamepad.m_button_select =
		pressed(RETRO_DEVICE_ID_JOYPAD_SELECT);
	g_machine->m_gamepad.m_button_up = pressed(RETRO_DEVICE_ID_JOYPAD_UP);
	g_machine->m_gamepad.m_button_down = pressed(RETRO_DEVICE_ID_JOYPAD_DOWN);
	g_machine->m_gamepad.m_button_left = pressed(RETRO_DEVICE_ID_JOYPAD_LEFT);
	g_machine->m_gamepad.m_button_right =
		pressed(RETRO_DEVICE_ID_JOYPAD_RIGHT);
}

void update_video()
{
	if(g_machine == nullptr)
	{
		return;
	}
	const std::vector<qtreGB::u8>& native =
		g_machine->m_ppu.get_video_buffer();
	if(native.size() != static_cast<size_t>(GB_WIDTH) * GB_HEIGHT)
	{
		return;
	}
	for(size_t i = 0; i < native.size(); ++i)
	{
		g_video[i] = DMG_PALETTE[native[i] & 0x03];
	}
	if(g_video_callback != nullptr)
	{
		g_video_callback(g_video.data(), GB_WIDTH, GB_HEIGHT,
			GB_WIDTH * sizeof(std::uint32_t));
	}
}

void update_audio()
{
	if(g_machine == nullptr)
	{
		return;
	}
	size_t frames = g_machine->m_apu.available_frames();
	frames = std::min(frames, g_audio.size() / 2);
	if(frames == 0)
	{
		return;
	}
	g_machine->m_apu.read_samples(g_audio.data(), frames);

	if(g_audio_batch_callback != nullptr)
	{
		g_audio_batch_callback(g_audio.data(), frames);
	}
	else if(g_audio_sample_callback != nullptr)
	{
		for(size_t i = 0; i < frames; ++i)
		{
			g_audio_sample_callback(g_audio[i * 2 + 0], g_audio[i * 2 + 1]);
		}
	}
}

void update_save_ram()
{
	if(g_machine == nullptr)
	{
		g_save_ram.clear();
		return;
	}
	const std::vector<qtreGB::u8> data =
		g_machine->m_cart.read_battery_ram();
	// Keep the buffer address stable for retro_get_memory_data() unless the
	// save size actually changes (for example when another ROM is loaded).
	if(g_save_ram.size() != data.size())
	{
		g_save_ram.resize(data.size());
	}
	std::copy(data.begin(), data.end(), g_save_ram.begin());
}

} // namespace

RETRO_API void retro_set_environment(retro_environment_t cb)
{
	g_environment = cb;
}

RETRO_API void retro_set_video_refresh(retro_video_refresh_t cb)
{
	g_video_callback = cb;
}

RETRO_API void retro_set_audio_sample(retro_audio_sample_t cb)
{
	g_audio_sample_callback = cb;
}

RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
	g_audio_batch_callback = cb;
}

RETRO_API void retro_set_input_poll(retro_input_poll_t cb)
{
	g_input_poll_callback = cb;
}

RETRO_API void retro_set_input_state(retro_input_state_t cb)
{
	g_input_state_callback = cb;
}

RETRO_API void retro_init(void)
{
	publish_input_info();
}

RETRO_API void retro_deinit(void)
{
	g_machine.reset();
	g_save_ram.clear();
	g_apply_save_ram = false;
}

RETRO_API unsigned retro_api_version(void)
{
	return RETRO_API_VERSION;
}

RETRO_API void retro_get_system_info(struct retro_system_info* info)
{
	if(info == nullptr)
	{
		return;
	}
	info->library_name = "QtreGB";
	info->library_version = QTREGB_BUILD_VERSION;
	info->valid_extensions = "gb";
	info->need_fullpath = false;
	info->block_extract = false;
}

RETRO_API void retro_get_system_av_info(struct retro_system_av_info* info)
{
	if(info == nullptr)
	{
		return;
	}
	info->geometry.base_width = GB_WIDTH;
	info->geometry.base_height = GB_HEIGHT;
	info->geometry.max_width = GB_WIDTH;
	info->geometry.max_height = GB_HEIGHT;
	info->geometry.aspect_ratio =
		static_cast<float>(GB_WIDTH) / static_cast<float>(GB_HEIGHT);
	info->timing.fps = 4194304.0 / static_cast<double>(GB_FRAME_TICKS);
	info->timing.sample_rate = 48000.0;
}

RETRO_API void retro_set_controller_port_device(unsigned port, unsigned device)
{
	if(port == 0)
	{
		g_controller_device = device;
	}
}

RETRO_API void retro_reset(void)
{
	if(g_machine != nullptr)
	{
		g_machine->reset();
		update_save_ram();
		g_apply_save_ram = false;
	}
}

RETRO_API void retro_run(void)
{
	if(g_machine == nullptr)
	{
		return;
	}

	if(g_apply_save_ram)
	{
		if(!g_save_ram.empty())
		{
			g_machine->m_cart.write_battery_ram(g_save_ram);
		}
		g_apply_save_ram = false;
	}

	poll_input();

	int ticks = 0;
	while(ticks < GB_FRAME_TICKS)
	{
		g_machine->tick();
		++ticks;
		if(g_machine->get_frame_ready())
		{
			break;
		}
	}

	update_video();
	update_audio();
	update_save_ram();
}

RETRO_API size_t retro_serialize_size(void)
{
	return LIBRETRO_STATE_SIZE;
}

RETRO_API bool retro_serialize(void* data, size_t len)
{
	if(g_machine == nullptr || data == nullptr || len < 36)
	{
		return false;
	}
	std::vector<qtreGB::u8> blob;
	if(!g_machine->save_state(blob) || blob.size() > len)
	{
		return false;
	}
	std::memset(data, 0, len);
	std::memcpy(data, blob.data(), blob.size());
	return true;
}

RETRO_API bool retro_unserialize(const void* data, size_t len)
{
	if(g_machine == nullptr || data == nullptr || len < 36)
	{
		return false;
	}
	const std::uint8_t* bytes = static_cast<const std::uint8_t*>(data);
	const std::uint32_t payload_size = read_le32(bytes + 24);
	if(payload_size > len - 36)
	{
		return false;
	}
	const size_t total = 36 + payload_size;
	std::vector<qtreGB::u8> blob(bytes, bytes + total);
	if(g_machine->load_state(blob) != qtreGB::StateError::Ok)
	{
		return false;
	}
	update_save_ram();
	g_apply_save_ram = false;
	return true;
}

RETRO_API void retro_cheat_reset(void)
{
}

RETRO_API void retro_cheat_set(unsigned index, bool enabled, const char* code)
{
	(void)index;
	(void)enabled;
	(void)code;
}

RETRO_API bool retro_load_game(const struct retro_game_info* game)
{
	if(game == nullptr || game->data == nullptr || game->size == 0)
	{
		return false;
	}
	publish_input_info();

	g_machine = std::make_unique<qtreGB::QtreGB>();
	load_system_bios(*g_machine);

	std::vector<qtreGB::u8> rom(
		static_cast<const qtreGB::u8*>(game->data),
		static_cast<const qtreGB::u8*>(game->data) + game->size);
	if(!g_machine->load_cart(std::move(rom)))
	{
		g_machine.reset();
		return false;
	}

	update_save_ram();
	g_apply_save_ram = true;

	if(g_environment != nullptr)
	{
		retro_pixel_format format = RETRO_PIXEL_FORMAT_XRGB8888;
		g_environment(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &format);
	}
	return true;
}

RETRO_API bool retro_load_game_special(unsigned game_type,
	const struct retro_game_info* info, size_t num_info)
{
	(void)game_type;
	(void)info;
	(void)num_info;
	return false;
}

RETRO_API void retro_unload_game(void)
{
	g_machine.reset();
	g_save_ram.clear();
	g_apply_save_ram = false;
}

RETRO_API unsigned retro_get_region(void)
{
	return RETRO_REGION_NTSC;
}

RETRO_API void* retro_get_memory_data(unsigned id)
{
	if(id == RETRO_MEMORY_SAVE_RAM)
	{
		return g_save_ram.empty() ? nullptr : g_save_ram.data();
	}
	if(id == RETRO_MEMORY_SYSTEM_RAM && g_machine != nullptr)
	{
		return g_machine->m_wram.data().data();
	}
	return nullptr;
}

RETRO_API size_t retro_get_memory_size(unsigned id)
{
	if(id == RETRO_MEMORY_SAVE_RAM)
	{
		return g_save_ram.size();
	}
	if(id == RETRO_MEMORY_SYSTEM_RAM && g_machine != nullptr)
	{
		return g_machine->m_wram.size();
	}
	return 0;
}
