#include "emulator_instance.h"

#include <algorithm>
#include <chrono>
#include <utility>

#include "build_info.h"

using namespace qtreGB;

namespace
{

std::string rom_title(const std::vector<u8>& rom)
{
	if(rom.size() < 0x144)
	{
		return {};
	}
	std::string title(reinterpret_cast<const char*>(rom.data() + 0x134), 16);
	const size_t terminator = title.find('\0');
	if(terminator != std::string::npos)
	{
		title.resize(terminator);
	}
	while(!title.empty() && title.back() == ' ')
	{
		title.pop_back();
	}
	return title;
}

std::string file_name(const std::string& path)
{
	const size_t separator = path.find_last_of("/\\");
	if(separator == std::string::npos)
	{
		return path;
	}
	return path.substr(separator + 1);
}

} // namespace

EmulatorInstance::EmulatorInstance(IGuiBackend *backend, u8 player_index) :
m_backend(backend),
m_player_index(player_index == 0 ? 1 : player_index),
m_is_frame_ready(false),
m_is_emulator_ready(false)
{
	m_audio_output_enabled = (m_player_index == 1);

	// Create texture using the backend
	if(m_backend)
	{
		m_texture_id = m_backend->createTexture(GB_XRES, GB_YRES);
	}

	m_machine = std::unique_ptr<QtreGB>(new QtreGB());
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

void EmulatorInstance::set_audio_settings(const AudioSettings& settings)
{
	m_audio_settings = settings;
	m_audio_settings.volume = std::clamp(m_audio_settings.volume, 0, 100);
	update_audio_device_state();
}

void EmulatorInstance::set_audio_output_enabled(bool enabled)
{
	if(m_audio_output_enabled == enabled)
	{
		return;
	}
	m_audio_output_enabled = enabled;
	update_audio_device_state();
}

void EmulatorInstance::update_audio_device_state()
{
	if(m_audio_device == 0)
	{
		return;
	}
	const bool audible = m_audio_settings.enabled &&
		m_audio_settings.volume > 0 && m_audio_output_enabled;
	SDL_ClearQueuedAudio(m_audio_device);
	SDL_PauseAudioDevice(m_audio_device, audible ? 0 : 1);
}

void EmulatorInstance::set_state_slot(int slot)
{
	m_state.set_slot(slot);
}

void EmulatorInstance::set_path_settings(const PathSettings& settings)
{
	const bool battery_changed =
		m_paths.battery != settings.battery ||
		m_paths.battery_same_as_rom != settings.battery_same_as_rom;
	if(battery_changed)
	{
		// Keep the current battery in the old location before switching.
		save_battery();
	}
	m_paths = settings;
	m_state.set_path_settings(settings);
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

	update_audio_device_state();
	return true;
}

void EmulatorInstance::discard_audio()
{
	if(!m_machine)
	{
		return;
	}
	if(m_audio_device != 0)
	{
		SDL_ClearQueuedAudio(m_audio_device);
	}
	int16_t buffer[2048];
	while(m_machine->m_apu.available_frames() > 0)
	{
		m_machine->m_apu.read_samples(buffer, 1024);
	}
}

void EmulatorInstance::drain_audio()
{
	if(m_audio_device == 0 || !m_is_emulator_ready)
	{
		return;
	}

	if(!m_audio_settings.enabled || m_audio_settings.volume <= 0 ||
	   !m_audio_output_enabled)
	{
		// Keep the APU ring from holding stale samples while muted.
		int16_t discard[1024 * 2];
		while(m_machine->m_apu.available_frames() > 0)
		{
			m_machine->m_apu.read_samples(discard, 1024);
		}
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
	const size_t frames = m_machine->m_apu.read_samples(buffer, 1024);
	if(frames == 0)
	{
		return;
	}

	if(m_audio_settings.volume < 100)
	{
		for(size_t i = 0; i < frames * 2; ++i)
		{
			buffer[i] = static_cast<int16_t>(
				(static_cast<int>(buffer[i]) * m_audio_settings.volume) / 100);
		}
	}

	SDL_QueueAudio(m_audio_device, buffer,
		static_cast<Uint32>(frames * 2 * sizeof(int16_t)));
}

void EmulatorInstance::update_frame(const DisplaySettings& settings,
	double seconds)
{
	if(!m_backend || !m_texture_id || !m_is_emulator_ready)
	{
		return;
	}

	m_display.set_settings(settings);
	const std::vector<u32>& pixels = m_display.process(
		m_machine->m_ppu.get_video_buffer(), seconds);
	if(pixels.size() == static_cast<size_t>(GB_XRES * GB_YRES))
	{
		m_backend->updateTexture(m_texture_id, pixels.data(), GB_XRES, GB_YRES);
	}
}

void EmulatorInstance::refresh_display(const DisplaySettings& settings)
{
	m_display.set_settings(settings);
	const std::vector<u32>& pixels = m_display.refresh();
	if(m_backend && m_texture_id &&
	   pixels.size() == static_cast<size_t>(GB_XRES * GB_YRES))
	{
		m_backend->updateTexture(m_texture_id, pixels.data(), GB_XRES, GB_YRES);
	}
}

const std::vector<u32>& EmulatorInstance::display_pixels() const
{
	return m_display.pixels();
}

void *EmulatorInstance::get_texture_ID() const
{
	return m_texture_id;
}

std::pair<int, int> EmulatorInstance::get_texture_size() const
{
	return {GB_XRES, GB_YRES};
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
	if(button >= static_cast<u8>(GbButton::Count))
	{
		return false;
	}
	return m_input.is_pressed(static_cast<GbButton>(button));
}

void EmulatorInstance::load_cart(std::vector<u8> cart, std::string rom_path)
{
	save_battery();
	m_cart = std::move(cart);
	m_rom_path = std::move(rom_path);
	m_state.set_rom_path(m_rom_path);
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

void EmulatorInstance::clear_boot_rom()
{
	m_boot_rom.clear();
}

void EmulatorInstance::reset()
{
	save_battery();
	rebuild_machine();
}

std::string EmulatorInstance::state_path() const
{
	// Player 2 has no independent savestates; link sessions use one .qss.
	if(m_player_index != 1)
	{
		return {};
	}
	return m_state.path();
}

std::string EmulatorInstance::battery_path() const
{
	return PathSettings::battery_path(m_paths, m_rom_path, m_player_index);
}

bool EmulatorInstance::save_state(std::uint64_t emulated_frames)
{
	if(m_player_index != 1 || !m_machine || !m_is_emulator_ready ||
	   m_rom_path.empty())
	{
		return false;
	}

	std::vector<u8> blob;
	if(!m_machine->save_state(blob) || blob.empty())
	{
		return false;
	}
	return m_state.save(current_state_info(emulated_frames), blob);
}

bool EmulatorInstance::load_state(SavestateInfo* info)
{
	if(m_player_index != 1 || !m_machine || !m_is_emulator_ready ||
	   m_rom_path.empty())
	{
		return false;
	}

	std::vector<u8> blob;
	SavestateInfo loaded;
	if(!m_state.load(blob, &loaded))
	{
		return false;
	}
	if(m_machine->load_state(blob) != StateError::Ok)
	{
		return false;
	}
	m_display.reset();

	// Output staging is not part of the state: drop queued audio. Host input
	// wins over the saved Gamepad state, so a button held at save time does
	// not stick, while keys currently held by the user stay pressed.
	if(m_audio_device != 0)
	{
		SDL_ClearQueuedAudio(m_audio_device);
	}
	apply_input();
	if(info != nullptr)
	{
		*info = std::move(loaded);
	}
	return true;
}

SavestateInfo EmulatorInstance::current_state_info(
	std::uint64_t emulated_frames) const
{
	SavestateInfo info;
	info.rom_size = static_cast<std::uint32_t>(m_cart.size());
	if(!m_cart.empty())
	{
		info.rom_hash = state_hash_rom(m_cart);
	}
	info.rom_title = rom_title(m_cart);
	info.rom_filename = file_name(m_rom_path);
	if(m_cart.size() >= 0x150)
	{
		info.cgb_flag = m_cart[0x143];
		info.cart_type = m_cart[0x147];
		info.header_checksum = m_cart[0x14D];
		info.global_checksum = static_cast<std::uint16_t>(m_cart[0x14E]) |
			(static_cast<std::uint16_t>(m_cart[0x14F]) << 8);
	}
	if(m_machine)
	{
		info.mapper = static_cast<std::uint8_t>(m_machine->m_cart.mapper());
		info.bios_active = m_machine->m_bus.is_boot_rom_enabled();
	}
	info.bios_size = static_cast<std::uint32_t>(m_boot_rom.size());
	if(!m_boot_rom.empty())
	{
		info.bios_hash = state_hash_bytes(m_boot_rom.data(),
			m_boot_rom.size());
	}
	info.created_unix_seconds = static_cast<std::uint64_t>(
		std::chrono::duration_cast<std::chrono::seconds>(
			std::chrono::system_clock::now().time_since_epoch()).count());
	info.emulated_frames = emulated_frames;
	info.slot = m_state.slot();
	info.kind = SavestateInfo::KindManual;
	info.app_version = QTREGB_BUILD_VERSION;

	const std::vector<u32>& pixels = m_display.pixels();
	if(pixels.size() == static_cast<size_t>(GB_XRES) * GB_YRES)
	{
		info.preview_width = GB_XRES;
		info.preview_height = GB_YRES;
		info.preview_pixels = pixels;
	}
	return info;
}

void EmulatorInstance::rebuild_machine()
{
	if(m_audio_device != 0)
	{
		SDL_ClearQueuedAudio(m_audio_device);
	}

	// Reuse the same QtreGB object so link endpoints (Serial/LinkCable) stay
	// valid across ROM loads and resets.
	if(!m_boot_rom.empty())
	{
		m_machine->load_boot_rom(m_boot_rom);
	}
	else
	{
		m_machine->clear_boot_rom();
	}
	if(!m_cart.empty())
	{
		m_machine->load_cart(m_cart);
	}
	else
	{
		m_machine->reset();
	}
	load_battery();
	apply_input();
	m_display.reset();
	m_is_emulator_ready = !m_cart.empty();
}

void EmulatorInstance::save_battery()
{
	if(!m_machine || m_rom_path.empty())
	{
		return;
	}
	m_machine->m_cart.save_battery_ram(battery_path());
}

void EmulatorInstance::load_battery()
{
	if(!m_machine || m_rom_path.empty())
	{
		return;
	}
	m_machine->m_cart.load_battery_ram(battery_path());
}

void EmulatorInstance::tick()
{
	if(m_is_emulator_ready)
	{
		m_machine->tick();
	}
}

bool EmulatorInstance::get_frame_ready()
{
	if(m_is_emulator_ready)
	{
		return m_machine->get_frame_ready();
	}
	return false;
}

void EmulatorInstance::set_button(u8 button, bool pressed)
{
	if(button >= static_cast<u8>(GbButton::Count))
	{
		return;
	}

	const GbButton logical = static_cast<GbButton>(button);
	if(pressed)
	{
		m_input.press(logical);
	}
	else
	{
		m_input.release(logical);
	}
	apply_input();
}

void EmulatorInstance::apply_input()
{
	if(m_machine)
	{
		m_input.apply(m_machine->m_gamepad);
	}
}
