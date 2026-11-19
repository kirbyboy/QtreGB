#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>

#include "emulator_frontend.h"
#include "ImGuiFileDialog.h"
#include "build_info.h"
#include "cart.h"
#include "file_dialog_i18n.h"
#include "i18n.h"
#include "state_stream.h"

using namespace qtreGB;
using namespace std;

namespace
{

double monotonic_seconds()
{
	return std::chrono::duration<double>(
		std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::string format_state_timestamp(std::uint64_t unix_seconds)
{
	if(unix_seconds == 0)
	{
		return {};
	}
	const std::time_t time = static_cast<std::time_t>(unix_seconds);
	std::tm local {};
#if defined(_WIN32)
	if(localtime_s(&local, &time) != 0)
	{
		return {};
	}
#else
	if(localtime_r(&time, &local) == nullptr)
	{
		return {};
	}
#endif
	char buffer[32] {};
	if(std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M",
		   &local) == 0)
	{
		return {};
	}
	return buffer;
}

std::string format_emulated_time(std::uint64_t frames)
{
	const double seconds = static_cast<double>(frames) * 70224.0 /
		4194304.0;
	const std::uint64_t total = static_cast<std::uint64_t>(seconds + 0.5);
	const std::uint64_t hours = total / 3600;
	const std::uint64_t minutes = (total / 60) % 60;
	const std::uint64_t remaining = total % 60;
	char buffer[32] {};
	if(hours > 0)
	{
		std::snprintf(buffer, sizeof(buffer), "%llu:%02llu:%02llu",
			static_cast<unsigned long long>(hours),
			static_cast<unsigned long long>(minutes),
			static_cast<unsigned long long>(remaining));
	}
	else
	{
		std::snprintf(buffer, sizeof(buffer), "%llu:%02llu",
			static_cast<unsigned long long>(minutes),
			static_cast<unsigned long long>(remaining));
	}
	return buffer;
}

std::string format_file_size(std::uint64_t bytes)
{
	char buffer[32] {};
	std::snprintf(buffer, sizeof(buffer), "%.1f KiB", bytes / 1024.0);
	return buffer;
}

std::string localized_control(const HostControl& control)
{
	const std::string name = InputSettings::control_name(control);
	return i18n::tr(name.c_str());
}

std::string localized_device(const InputManager& manager,
	const InputDeviceId& device)
{
	const std::string name = manager.device_label(device);
	return i18n::tr(name.c_str());
}

const char* gb_button_label(GbButton button)
{
	return i18n::tr(InputSettings::gb_button_name(button));
}

const char* shortcut_label(ShortcutAction action)
{
	return i18n::tr(InputSettings::shortcut_name(action));
}

int path_input_resize_callback(ImGuiInputTextCallbackData* data)
{
	if(data->EventFlag == ImGuiInputTextFlags_CallbackResize)
	{
		auto* value = static_cast<std::string*>(data->UserData);
		value->resize(static_cast<size_t>(data->BufTextLen));
		data->Buf = value->data();
	}
	return 0;
}

bool path_input_text(const char* label, std::string& value)
{
	return ImGui::InputText(label, value.data(), value.capacity() + 1,
		ImGuiInputTextFlags_CallbackResize |
		ImGuiInputTextFlags_ElideLeft,
		path_input_resize_callback, &value);
}

bool valid_rom(const std::vector<u8>& rom)
{
	if(rom.empty())
	{
		return false;
	}
	qtreGB::Cart candidate;
	return candidate.load_cart(rom) == 0;
}

std::string hex_hash(std::uint64_t value)
{
	char buffer[24];
	std::snprintf(buffer, sizeof(buffer), "%016llx",
		static_cast<unsigned long long>(value));
	return buffer;
}

void write_u16(std::ofstream& output, u16 value)
{
	const char bytes[2] = {
		static_cast<char>(value & 0xFF),
		static_cast<char>((value >> 8) & 0xFF),
	};
	output.write(bytes, 2);
}

void write_u32(std::ofstream& output, u32 value)
{
	const char bytes[4] = {
		static_cast<char>(value & 0xFF),
		static_cast<char>((value >> 8) & 0xFF),
		static_cast<char>((value >> 16) & 0xFF),
		static_cast<char>((value >> 24) & 0xFF),
	};
	output.write(bytes, 4);
}

bool read_binary_file(const std::string& path, std::vector<u8>& data)
{
	constexpr std::streamsize kMaxReadBytes = 64 << 20;
	const std::filesystem::path file_path =
		std::filesystem::u8path(path);
	std::error_code error;
	if(!std::filesystem::is_regular_file(file_path, error))
	{
		return false;
	}

	std::ifstream input(file_path, std::ios::binary);
	if(!input)
	{
		return false;
	}
	input.seekg(0, std::ios::end);
	if(!input)
	{
		return false;
	}
	const std::streamsize size = input.tellg();
	if(size < 0 || size > kMaxReadBytes)
	{
		return false;
	}
	input.seekg(0, std::ios::beg);
	if(!input)
	{
		return false;
	}
	data.resize(static_cast<size_t>(size));
	if(size > 0)
	{
		input.read(reinterpret_cast<char*>(data.data()), size);
	}
	return input.good() || input.eof();
}

bool write_bmp(const std::string& path, int width, int height,
	const std::vector<u32>& pixels)
{
	if(width <= 0 || height <= 0 ||
	   pixels.size() != static_cast<size_t>(width) * static_cast<size_t>(height))
	{
		return false;
	}

	std::ofstream output(std::filesystem::u8path(path),
		std::ios::binary | std::ios::trunc);
	if(!output)
	{
		return false;
	}

	const int row_size = (width * 3 + 3) & ~3;
	const u32 image_size = static_cast<u32>(row_size) * static_cast<u32>(height);
	output.write("BM", 2);
	write_u32(output, 54 + image_size);
	write_u16(output, 0);
	write_u16(output, 0);
	write_u32(output, 54);
	write_u32(output, 40);
	write_u32(output, static_cast<u32>(width));
	write_u32(output, static_cast<u32>(height));
	write_u16(output, 1);
	write_u16(output, 24);
	write_u32(output, 0);
	write_u32(output, image_size);
	write_u32(output, 2835);
	write_u32(output, 2835);
	write_u32(output, 0);
	write_u32(output, 0);

	std::vector<char> row(static_cast<size_t>(row_size), 0);
	for(int y = height - 1; y >= 0; --y)
	{
		for(int x = 0; x < width; ++x)
		{
			const u32 pixel = pixels[static_cast<size_t>(y) * width + x];
			row[static_cast<size_t>(x) * 3 + 0] =
				static_cast<char>((pixel >> 8) & 0xFF);
			row[static_cast<size_t>(x) * 3 + 1] =
				static_cast<char>((pixel >> 16) & 0xFF);
			row[static_cast<size_t>(x) * 3 + 2] =
				static_cast<char>((pixel >> 24) & 0xFF);
		}
		output.write(row.data(), row.size());
	}
	return output.good();
}

} // namespace

EmulatorFrontend::EmulatorFrontend(IGuiBackend *backend,
	const DisplaySettings& settings, const AudioSettings& audio_settings,
	const InputSettings& input_settings, const PathSettings& path_settings) :
m_backend(backend),
m_display_settings(settings),
m_audio_settings(audio_settings),
m_input_settings(input_settings),
m_path_settings(path_settings),
m_path_edit(path_settings)
{
	m_input_manager.set_settings(m_input_settings);
	m_instances.push_back(std::make_unique<EmulatorInstance>(m_backend, 1));
	m_instances.front()->set_audio_settings(m_audio_settings);
	m_instances.front()->set_path_settings(m_path_settings);
	std::string bios_error;
	load_global_bios(bios_error);
	apply_global_bios_to_primary();
	apply_audio_routing();
}

void EmulatorFrontend::set_audio_settings(const AudioSettings& settings)
{
	m_audio_settings = settings;
	m_audio_settings.volume = std::clamp(m_audio_settings.volume, 0, 100);
	for(const auto& instance : m_instances)
	{
		instance->set_audio_settings(m_audio_settings);
	}
	apply_audio_routing();
}

bool EmulatorFrontend::fast_forward_active() const
{
	return m_input_manager.shortcut_held(ShortcutAction::FastForward);
}

void EmulatorFrontend::record_emulated_frame()
{
	m_fps.add_frame(monotonic_seconds());
	++m_emulated_frames;
}

double EmulatorFrontend::emulated_fps() const
{
	const double now = monotonic_seconds();
	if(!m_fps.has_value(now))
	{
		return -1.0;
	}
	return m_fps.fps(now);
}

void EmulatorFrontend::set_fast_forward_speed(FastForwardSpeed speed)
{
	m_fast_forward_speed = speed;
}

void EmulatorFrontend::set_link_audio_source(LinkAudioSource source)
{
	m_audio_settings.link_source = source;
	set_audio_settings(m_audio_settings);
	const char* name = source == LinkAudioSource::Player2 ? "Player 2" :
		(source == LinkAudioSource::Both ? "Both" : "Player 1");
	notify(i18n::format("Link audio: %s", i18n::tr(name)));
}

void EmulatorFrontend::apply_audio_routing()
{
	const bool linked = link_session_active();
	for(size_t i = 0; i < m_instances.size(); ++i)
	{
		const bool audible = i == 0
			? (!linked || m_audio_settings.link_source !=
				LinkAudioSource::Player2)
			: (linked && m_audio_settings.link_source !=
				LinkAudioSource::Player1);
		m_instances[i]->set_audio_output_enabled(audible);
	}
}

void EmulatorFrontend::set_input_settings(const InputSettings& settings)
{
	m_input_settings = settings;
	m_input_settings.sanitize();
	m_input_manager.set_settings(m_input_settings);
}

void EmulatorFrontend::set_path_settings(const PathSettings& settings)
{
	const bool bios_changed = m_path_settings.bios != settings.bios;
	m_path_settings = settings;
	for(const auto& instance : m_instances)
	{
		instance->set_path_settings(m_path_settings);
	}

	if(bios_changed)
	{
		std::string error;
		if(!load_global_bios(error) && !error.empty())
		{
			notify(error);
		}
		if(!link_session_active())
		{
			apply_global_bios_to_primary();
		}
	}
}

bool EmulatorFrontend::load_global_bios(std::string& error)
{
	error.clear();
	m_bios.clear();
	if(m_path_settings.bios.empty())
	{
		return true;
	}

	std::vector<u8> bios;
	if(!read_binary_file(m_path_settings.bios, bios))
	{
		error = i18n::format("Could not read the BIOS file: %s",
			m_path_settings.bios.c_str());
		return false;
	}
	if(bios.size() != 0x100)
	{
		error = i18n::tr("BIOS: invalid size (expected 256 bytes)");
		return false;
	}
	m_bios = std::move(bios);
	return true;
}

void EmulatorFrontend::apply_bios_to_instance(EmulatorInstance& instance,
	const std::vector<u8>& bios)
{
	if(bios.empty())
	{
		instance.clear_boot_rom();
	}
	else
	{
		instance.load_boot_rom(bios);
	}
}

void EmulatorFrontend::apply_global_bios_to_primary()
{
	if(primary() != nullptr)
	{
		apply_bios_to_instance(*primary(), m_bios);
	}
}

void EmulatorFrontend::handle_sdl_event(const void* event)
{
	if(event != nullptr)
	{
		m_input_manager.handle_event(*static_cast<const SDL_Event*>(event));
	}
}

void EmulatorFrontend::shutdown_input()
{
	m_input_manager.shutdown();
}

void EmulatorFrontend::set_language(const std::string& language)
{
	m_language = (language == "system" || language == "en" || language == "es")
		? language
		: "system";
	i18n::set_locale(m_language);
	apply_file_dialog_language();
}

void EmulatorFrontend::notify(const std::string& message)
{
	m_state_message = message;
	m_state_message_until = ImGui::GetTime() + 3.5;
}

void EmulatorFrontend::set_state_slot(int slot)
{
	m_state_slot = std::clamp(slot, 0, 9);
	for(const auto& instance : m_instances)
	{
		instance->set_state_slot(m_state_slot);
	}
}

void EmulatorFrontend::change_state_slot(int delta)
{
	if(link_session_active())
	{
		notify(i18n::tr("Link sessions use a single state file"));
		return;
	}

	const int slot = (m_state_slot + delta + 10) % 10;
	set_state_slot(slot);
	notify(i18n::format("State slot: %d", m_state_slot));
}

float EmulatorFrontend::main_status_bar_height() const
{
	const ImGuiStyle& style = ImGui::GetStyle();
	return ImGui::GetTextLineHeightWithSpacing() +
		style.WindowPadding.y * 2.0f + 4.0f;
}

void EmulatorFrontend::draw_main_status_bar()
{
	const ImGuiStyle& style = ImGui::GetStyle();
	const float height = main_status_bar_height();
	const ImVec2 window_pos = ImGui::GetWindowPos();
	const float top = window_pos.y + ImGui::GetWindowHeight() - height;

	ImGui::SetCursorScreenPos(ImVec2(window_pos.x, top));
	ImGui::PushStyleColor(ImGuiCol_ChildBg,
		ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));
	ImGui::BeginChild("##main_status", ImVec2(0.0f, height), false,
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	if(link_session_active())
	{
		ImGui::TextDisabled("%s", i18n::tr("Session state (single slot)"));
	}
	else
	{
		ImGui::Text("%s", i18n::format("State slot: %d", m_state_slot).c_str());
	}
	ImGui::SameLine();
	const double fps_value = emulated_fps();
	if(fps_value >= 0.0)
	{
		ImGui::TextDisabled("FPS: %.1f", fps_value);
	}
	else
	{
		ImGui::TextDisabled("FPS: --");
	}
	if(!m_state_message.empty() && ImGui::GetTime() < m_state_message_until)
	{
		ImGui::SameLine();
		ImGui::TextUnformatted(m_state_message.c_str());
	}
	ImGui::EndChild();
	ImGui::PopStyleColor();
	(void)style;
}

EmulatorFrontend::~EmulatorFrontend()
{
	destroy_state_previews();
	if(m_backend && m_combined_texture)
	{
		m_backend->destroyTexture(m_combined_texture);
		m_combined_texture = nullptr;
	}
}

void EmulatorFrontend::draw_main_window()
{

    // Flags so the main window has no borders or title bar
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar
                                  | ImGuiWindowFlags_NoDocking
                                  | ImGuiWindowFlags_NoTitleBar
                                  | ImGuiWindowFlags_NoCollapse
                                  | ImGuiWindowFlags_NoResize
                                  | ImGuiWindowFlags_NoMove
                                  | ImGuiWindowFlags_NoBringToFrontOnFocus
                                  | ImGuiWindowFlags_NoNavFocus;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    // Let the screen use the whole window below the menu bar so integer
    // scaling can use the largest possible factor.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    ImGui::Begin("MainWindow", nullptr, window_flags);
    ImGui::PopStyleVar(3);

    // Top menu
    if (ImGui::BeginMenuBar())
    {
		if (ImGui::BeginMenu(i18n::tr("File")))
		{
			/*
			if (ImGui::MenuItem(i18n::tr("Open ROM...")))
			{
				std::string filename = "../roms/01-special.gb"; // your file dialog function
				if (!filename.empty())
				{
					ifstream myfile (filename, ios::binary);
					if (myfile.is_open())
					{
						myfile.seekg(0, ios::end);
						int file_size = myfile.tellg();

						vector<u8> rom;
						myfile.seekg(0, ios::beg);
						rom.resize(static_cast<std::size_t>(file_size));
						myfile.read(reinterpret_cast<char*>(&rom.front()), static_cast<std::size_t>(file_size));
						myfile.close();

						primary()->load_cart(rom);
					}
				}
			}*/
			if (ImGui::MenuItem(i18n::tr("Open ROM...")))
			{
				open_file_dialog(FileDialogAction::OpenRom);
			}
			if (ImGui::MenuItem(i18n::tr(link_session_active()
				? "Reset player 1" : "Reset")))
			{
				const bool was_linked = link_session_active();
				primary()->reset();
				m_emulated_frames = 0;
				m_state_browser_dirty = true;
				if(was_linked)
				{
					reset_link_transfer();
				}
			}

			if (ImGui::MenuItem(i18n::tr("Save state")))
			{
				save_state();
			}
			if (ImGui::MenuItem(i18n::tr("Load state")))
			{
				load_state();
			}
			if (ImGui::MenuItem(i18n::tr("State browser...")))
			{
				open_state_browser();
			}

			if(!m_recent_roms.empty())
			{
				ImGui::Separator();
				for(size_t i = 0; i < m_recent_roms.size(); ++i)
				{
					const std::string path = m_recent_roms.paths()[i];
					std::string name = path;
					const size_t separator = path.find_last_of("/\\");
					if(separator != std::string::npos &&
					   separator + 1 < path.size())
					{
						name = path.substr(separator + 1);
					}
					const std::string label = std::to_string(i + 1) +
						". " + name;
					ImGui::PushID(static_cast<int>(i));
					if(ImGui::MenuItem(label.c_str()))
					{
						open_recent_rom(i);
					}
					if(ImGui::IsItemHovered())
					{
						ImGui::SetTooltip("%s", path.c_str());
					}
					ImGui::PopID();
				}
			}

			ImGui::Separator();
			if (ImGui::MenuItem(i18n::tr("Exit")))
			{
				m_should_close = true;
			}
			ImGui::EndMenu();
		}

		if(ImGui::BeginMenu(i18n::tr("Link")))
		{
			const bool active = link_session_active();
			if(ImGui::MenuItem(i18n::tr("Start session..."), nullptr,
				false, !active && primary() != nullptr &&
				primary()->is_ready()))
			{
				open_file_dialog(FileDialogAction::OpenRomPlayer2);
			}
			if(ImGui::MenuItem(i18n::tr("Load ROM player 2..."), nullptr,
				false, active))
			{
				open_file_dialog(FileDialogAction::OpenRomPlayer2);
			}
			if(ImGui::MenuItem(i18n::tr("Reset player 2"), nullptr,
				false, active))
			{
				EmulatorInstance* second = instance(1);
				if(second != nullptr)
				{
					second->reset();
					reset_link_transfer();
				}
			}
			if(ImGui::MenuItem(i18n::tr("Stop session"), nullptr,
				false, active))
			{
				stop_link_session();
			}
			ImGui::Separator();
			if(ImGui::BeginMenu(i18n::tr("Audio")))
			{
				if(ImGui::MenuItem(i18n::tr("Player 1"), nullptr,
					m_audio_settings.link_source ==
						LinkAudioSource::Player1))
				{
					set_link_audio_source(LinkAudioSource::Player1);
				}
				if(ImGui::MenuItem(i18n::tr("Player 2"), nullptr,
					m_audio_settings.link_source ==
						LinkAudioSource::Player2))
				{
					set_link_audio_source(LinkAudioSource::Player2);
				}
				if(ImGui::MenuItem(i18n::tr("Both"), nullptr,
					m_audio_settings.link_source ==
						LinkAudioSource::Both))
				{
					set_link_audio_source(LinkAudioSource::Both);
				}
				ImGui::EndMenu();
			}
			ImGui::EndMenu();
		}

		if(ImGui::BeginMenu(i18n::tr("Graphics")))
		{
			const ImGuiIO& io = ImGui::GetIO();
			ImGui::Text(i18n::tr("Display FPS: %.1f - %.2f ms"), io.Framerate,
				io.Framerate > 0.0f ? 1000.0f / io.Framerate : 0.0f);
			ImGui::Separator();

			if(ImGui::BeginMenu(i18n::tr("Palette")))
			{
				if(ImGui::MenuItem(i18n::tr("DMG"), nullptr,
					m_display_settings.palette == PaletteMode::Dmg))
				{
					m_display_settings.palette = PaletteMode::Dmg;
					apply_display_settings();
				}
				if(ImGui::MenuItem(i18n::tr("Pocket"), nullptr,
					m_display_settings.palette == PaletteMode::Pocket))
				{
					m_display_settings.palette = PaletteMode::Pocket;
					apply_display_settings();
				}
				if(ImGui::MenuItem(i18n::tr("Custom..."), nullptr,
					m_display_settings.palette == PaletteMode::Custom))
				{
					m_custom_palette_edit = m_display_settings.custom_palette;
					m_open_custom_palette = true;
				}
				ImGui::EndMenu();
			}

			const std::array<Rgb, 4> palette_colors =
				m_display_settings.colors();
			for(size_t i = 0; i < palette_colors.size(); ++i)
			{
				const Rgb& color = palette_colors[i];
				const ImVec4 swatch(
					static_cast<float>(color.r) / 255.0f,
					static_cast<float>(color.g) / 255.0f,
					static_cast<float>(color.b) / 255.0f, 1.0f);
				char id[32];
				std::snprintf(id, sizeof(id), "##palette%zu", i);
				ImGui::ColorButton(id, swatch,
					ImGuiColorEditFlags_NoAlpha |
					ImGuiColorEditFlags_NoPicker |
					ImGuiColorEditFlags_NoTooltip,
					ImVec2(18.0f, 18.0f));
				if(i + 1 < palette_colors.size())
				{
					ImGui::SameLine();
				}
			}

			float background[3] = {
				static_cast<float>(m_display_settings.background.r) / 255.0f,
				static_cast<float>(m_display_settings.background.g) / 255.0f,
				static_cast<float>(m_display_settings.background.b) / 255.0f,
			};
			if(ImGui::ColorEdit3(i18n::tr("Background"), background,
				ImGuiColorEditFlags_NoAlpha))
			{
				const auto to_byte = [](float value)
				{
					return static_cast<u8>(std::lround(
						std::clamp(value, 0.0f, 1.0f) * 255.0f));
				};
				m_display_settings.background = {
					to_byte(background[0]), to_byte(background[1]),
					to_byte(background[2])
				};
			}

			if(ImGui::Checkbox(i18n::tr("LCD grid"), &m_display_settings.lcd_grid))
			{
				apply_display_settings();
			}
			if(m_display_settings.lcd_grid)
			{
				ImGui::SliderInt(i18n::tr("Grid opacity"),
					&m_display_settings.grid_opacity, 0, 100, "%d%%");
			}
			if(ImGui::Checkbox(i18n::tr("Integer scaling"),
				&m_display_settings.integer_scaling))
			{
				apply_display_settings();
			}

			const char* present_modes[] = {
				i18n::tr("Auto"), i18n::tr("Monitor"), i18n::tr("Game Boy")
			};
			int present_mode = static_cast<int>(m_display_settings.present_mode);
			if(ImGui::Combo(i18n::tr("Present mode"), &present_mode, present_modes,
				IM_ARRAYSIZE(present_modes)))
			{
				m_display_settings.present_mode =
					static_cast<PresentMode>(present_mode);
			}

			if(ImGui::MenuItem(i18n::tr("Fullscreen")))
			{
				m_backend->toggle_fullscreen();
			}

			if(ImGui::MenuItem(i18n::tr("Save screenshot...")))
			{
				save_screenshot();
			}

			if(ImGui::MenuItem(i18n::tr("Reset graphics settings")))
			{
				m_display_settings = DisplaySettings {};
				apply_display_settings();
			}

			if(ImGui::BeginMenu(i18n::tr("Ghosting")))
			{
				const char* preset = i18n::tr("Custom");
				int preset_index = 4;
				if(!m_display_settings.ghosting)
				{
					preset = i18n::tr("Off");
					preset_index = 0;
				}
				else if(m_display_settings.ghosting_light_ms == 27 &&
					m_display_settings.ghosting_dark_ms == 27)
				{
					preset = i18n::tr("Fast");
					preset_index = 1;
				}
				else if(m_display_settings.ghosting_light_ms == 32 &&
					m_display_settings.ghosting_dark_ms == 16)
				{
					preset = i18n::tr("Normal");
					preset_index = 2;
				}
				else if(m_display_settings.ghosting_light_ms == 47 &&
					m_display_settings.ghosting_dark_ms == 103)
				{
					preset = i18n::tr("Slow");
					preset_index = 3;
				}

				bool changed = false;
				if(ImGui::BeginCombo(i18n::tr("Preset"), preset))
				{
					const char* names[] = {
						i18n::tr("Off"), i18n::tr("Fast"),
						i18n::tr("Normal"), i18n::tr("Slow")
					};
					for(int i = 0; i < 4; ++i)
					{
						if(ImGui::Selectable(names[i], preset_index == i))
						{
							changed = true;
							switch(i)
							{
								case 0:
									m_display_settings.ghosting = false;
									break;
								case 1:
									m_display_settings.ghosting = true;
									m_display_settings.ghosting_light_ms = 27;
									m_display_settings.ghosting_dark_ms = 27;
									break;
								case 2:
									m_display_settings.ghosting = true;
									m_display_settings.ghosting_light_ms = 32;
									m_display_settings.ghosting_dark_ms = 16;
									break;
								case 3:
									m_display_settings.ghosting = true;
									m_display_settings.ghosting_light_ms = 47;
									m_display_settings.ghosting_dark_ms = 103;
									break;
							}
						}
					}
					ImGui::EndCombo();
				}

				changed |= ImGui::Checkbox(i18n::tr("Enabled"),
					&m_display_settings.ghosting);
				changed |= ImGui::SliderInt(i18n::tr("Lighten ms/tone"),
					&m_display_settings.ghosting_light_ms, 0,
					DisplaySettings::kMaxGhostingMs, "%d ms/tone");
				changed |= ImGui::SliderInt(i18n::tr("Darken ms/tone"),
					&m_display_settings.ghosting_dark_ms, 0,
					DisplaySettings::kMaxGhostingMs, "%d ms/tone");
				if(changed)
				{
					apply_display_settings();
				}
				ImGui::EndMenu();
			}
			ImGui::EndMenu();
		}

		if(ImGui::BeginMenu(i18n::tr("Audio")))
		{
			bool changed = ImGui::Checkbox(i18n::tr("Enable audio"),
				&m_audio_settings.enabled);
			changed |= ImGui::SliderInt(i18n::tr("Volume"), &m_audio_settings.volume,
				0, 100, "%d%%");
			if(changed)
			{
				set_audio_settings(m_audio_settings);
			}
			ImGui::EndMenu();
		}

		if(ImGui::BeginMenu(i18n::tr("Settings")))
		{
			if(ImGui::MenuItem(i18n::tr("Controllers...")))
			{
				m_show_controllers_window = true;
			}
			if(ImGui::MenuItem(i18n::tr("Key bindings...")))
			{
				m_show_key_bindings_window = true;
			}
			if(ImGui::MenuItem(i18n::tr("Paths...")))
			{
				open_paths_window();
			}
			if(ImGui::BeginMenu(i18n::tr("Fast Forward speed")))
			{
				if(ImGui::MenuItem("200%", nullptr,
					m_fast_forward_speed == FastForwardSpeed::X200))
				{
					set_fast_forward_speed(FastForwardSpeed::X200);
				}
				if(ImGui::MenuItem("300%", nullptr,
					m_fast_forward_speed == FastForwardSpeed::X300))
				{
					set_fast_forward_speed(FastForwardSpeed::X300);
				}
				if(ImGui::MenuItem("MAX", nullptr,
					m_fast_forward_speed == FastForwardSpeed::Max))
				{
					set_fast_forward_speed(FastForwardSpeed::Max);
				}
				ImGui::EndMenu();
			}
			if(ImGui::BeginMenu(i18n::tr("Language")))
			{
				if(ImGui::MenuItem(i18n::tr("System default"), nullptr,
					m_language == "system"))
				{
					set_language("system");
				}
				if(ImGui::MenuItem("English", nullptr,
					m_language == "en"))
				{
					set_language("en");
				}
				if(ImGui::MenuItem("Español", nullptr,
					m_language == "es"))
				{
					set_language("es");
				}
				ImGui::EndMenu();
			}
			ImGui::EndMenu();
		}


		ImGui::EndMenuBar();
	}

    // --- Draw the emulator directly ---
    if (primary())
    {
        void* textureId = get_texture_ID();
        std::pair<int,int> texSize = get_texture_size();
        int nativeW = texSize.first;
        int nativeH = texSize.second;

        // Full area available inside the main window, minus the fixed
        // status bar at the bottom.
        ImVec2 avail = ImGui::GetContentRegionAvail();
        avail.y = std::max(1.0f, avail.y - main_status_bar_height());
        if(avail.x < 1.0f || avail.y < 1.0f)
        {
            ImGui::End();
            return;
        }

        // Prefer integer scaling: each Game Boy pixel maps to the same number
        // of screen pixels. Fractional scaling is what makes some lines look
        // thicker/thinner or "cut". On HiDPI displays the integer factor must
        // be computed in physical pixels, not logical ImGui units.
        const ImVec2 fb_scale = ImGui::GetIO().DisplayFramebufferScale;
        const float scale_x = fb_scale.x > 0.0f ? fb_scale.x : 1.0f;
        const float scale_y = fb_scale.y > 0.0f ? fb_scale.y : 1.0f;

        ImVec2 finalSize;
        if(m_display_settings.integer_scaling)
        {
            const int scaleX = static_cast<int>(avail.x * scale_x) / nativeW;
            const int scaleY = static_cast<int>(avail.y * scale_y) / nativeH;
            const int integerScale = std::max(1, std::min(scaleX, scaleY));
            finalSize = ImVec2(nativeW * integerScale / scale_x,
                nativeH * integerScale / scale_y);

            if(finalSize.x > avail.x || finalSize.y > avail.y)
            {
                // Window smaller than the native image: keep the aspect
                // ratio, even if the result has to be fractional.
                const float fit = std::min(avail.x / nativeW,
                    avail.y / nativeH);
                finalSize = ImVec2(nativeW * fit, nativeH * fit);
            }
        }
        else
        {
            const float fit = std::min(avail.x / nativeW,
                avail.y / nativeH);
            finalSize = ImVec2(nativeW * fit, nativeH * fit);
        }

        // Fill the letterbox area with the configured screen background.
        // The framebuffer itself is drawn on top.
        const ImVec2 content_origin = ImGui::GetCursorScreenPos();
        const ImU32 background_color = IM_COL32(
            m_display_settings.background.r,
            m_display_settings.background.g,
            m_display_settings.background.b, 255);
        ImGui::GetWindowDrawList()->AddRectFilled(content_origin,
            ImVec2(content_origin.x + avail.x, content_origin.y + avail.y),
            background_color);

        // Center the image, but snap it to whole physical pixels. With
        // nearest filtering, a .5 px offset makes some columns/rows one pixel
        // wider, which is especially visible while scrolling vertically.
        ImVec2 screen_pos = ImGui::GetCursorScreenPos();
        const ImVec2 offset((avail.x - finalSize.x) * 0.5f,
            (avail.y - finalSize.y) * 0.5f);
        screen_pos.x = std::floor((screen_pos.x + std::max(0.0f, offset.x)) * scale_x) / scale_x;
        screen_pos.y = std::floor((screen_pos.y + std::max(0.0f, offset.y)) * scale_y) / scale_y;
        ImGui::SetCursorScreenPos(screen_pos);

        // Draw the emulator framebuffer
        ImGui::Image(textureId, finalSize);

        const float effective_scale = (finalSize.x * scale_x) / nativeW;
        if(m_display_settings.lcd_grid && effective_scale >= 2.0f)
        {
            const ImVec2 image_min = ImGui::GetItemRectMin();
            const ImVec2 image_max = ImGui::GetItemRectMax();
            const float step_x = finalSize.x / nativeW;
            const float step_y = finalSize.y / nativeH;
            const Rgb grid_rgb = m_display_settings.colors()[0];
            const u8 grid_alpha = static_cast<u8>(
                std::clamp(m_display_settings.grid_opacity, 0, 100) * 255 / 100);
            const ImU32 grid_color = IM_COL32(grid_rgb.r, grid_rgb.g,
                grid_rgb.b, grid_alpha);
            const float thickness_x = 1.0f / scale_x;
            const float thickness_y = 1.0f / scale_y;

            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            draw_list->PushClipRect(image_min, image_max, true);

            for(int i = 1; i < nativeW; ++i)
            {
                const float x = image_min.x + static_cast<float>(i) * step_x;
                draw_list->AddLine(ImVec2(x, image_min.y),
                    ImVec2(x, image_max.y), grid_color, thickness_x);
            }
            for(int j = 1; j < nativeH; ++j)
            {
                const float y = image_min.y + static_cast<float>(j) * step_y;
                draw_list->AddLine(ImVec2(image_min.x, y),
                    ImVec2(image_max.x, y), grid_color, thickness_y);
            }

            // Close the outer border at the last pixel edge. The line is
            // placed on the boundary (half a physical pixel inside) so the
            // last pixels keep the same size as the rest.
            const float left = image_min.x + thickness_x * 0.5f;
            const float right = image_max.x - thickness_x * 0.5f;
            const float top = image_min.y + thickness_y * 0.5f;
            const float bottom = image_max.y - thickness_y * 0.5f;
            draw_list->AddLine(ImVec2(left, image_min.y),
                ImVec2(left, image_max.y), grid_color, thickness_x);
            draw_list->AddLine(ImVec2(right, image_min.y),
                ImVec2(right, image_max.y), grid_color, thickness_x);
            draw_list->AddLine(ImVec2(image_min.x, top),
                ImVec2(image_max.x, top), grid_color, thickness_y);
            draw_list->AddLine(ImVec2(image_min.x, bottom),
                ImVec2(image_max.x, bottom), grid_color, thickness_y);
            draw_list->PopClipRect();
        }
    }

	if(!m_show_controllers_window &&
	   m_capture_target == InputCaptureTarget::PlayerBinding)
	{
		m_input_manager.cancel_capture();
		m_capture_target = InputCaptureTarget::None;
	}
	if(!m_show_key_bindings_window &&
	   (m_capture_target == InputCaptureTarget::ShortcutKeyboard ||
	    m_capture_target == InputCaptureTarget::ShortcutController))
	{
		m_input_manager.cancel_capture();
		m_capture_target = InputCaptureTarget::None;
	}
	if(m_capture_target != InputCaptureTarget::None)
	{
		apply_input_capture();
	}
	if(m_show_controllers_window)
	{
		draw_controllers_window();
	}
	if(m_show_key_bindings_window)
	{
		draw_key_bindings_window();
	}
	if(m_show_paths_window)
	{
		draw_paths_window();
	}
	if(m_show_state_browser)
	{
		draw_state_browser();
	}

	draw_main_status_bar();

	if(m_open_custom_palette)
	{
		const std::string title =
			std::string(i18n::tr("Custom palette")) + "###CustomPalette";
		ImGui::OpenPopup(title.c_str());
		m_open_custom_palette = false;
	}
	draw_custom_palette_window();

	ImGui::End(); // End of MainWindow
	draw_file_dialogs();
}

void EmulatorFrontend::tick()
{
	if(m_link && m_link->connected())
	{
		m_link->tick();

		// The two PPUs do not necessarily reach line 0 on the same tick.
		// Capture player 2's completed frame at its own boundary; otherwise
		// the combined image mixes rows from two different frames and shows a
		// fixed horizontal seam.
		EmulatorInstance* second = instance(1);
		if(second != nullptr && second->is_ready() &&
		   second->get_frame_ready())
		{
			second->update_frame(m_display_settings);
		}
	}
	else if(primary())
	{
		primary()->tick();
	}
}

void EmulatorFrontend::drain_audio()
{
	if(fast_forward_active())
	{
		for(const auto& instance : m_instances)
		{
			instance->discard_audio();
		}
		return;
	}
	for(const auto& instance : m_instances)
	{
		instance->drain_audio();
	}
}

void EmulatorFrontend::handle_input()
{
	m_input_manager.set_ui_blocked(
		m_file_dialog_action != FileDialogAction::None ||
		m_show_controllers_window || m_show_key_bindings_window ||
		m_show_paths_window || m_show_state_browser);
	m_input_manager.update();
	// Fast forward is hold-based; discard its one-shot trigger.
	m_input_manager.consume_shortcut(ShortcutAction::FastForward);

	for(size_t player = 0; player < m_instances.size() && player < 2; ++player)
	{
		EmulatorInstance* target = instance(player);
		if(target == nullptr)
		{
			continue;
		}

		for(size_t i = 0; i < 8; ++i)
		{
			const GbButton button = static_cast<GbButton>(i);
			const bool pressed = m_input_manager.player_pressed(player, button);
			if(pressed == target->is_button_pressed(static_cast<u8>(i)))
			{
				continue;
			}
			if(pressed)
			{
				target->press_button(static_cast<u8>(i));
			}
			else
			{
				target->release_button(static_cast<u8>(i));
			}
		}
	}

	if(m_input_manager.consume_shortcut(ShortcutAction::Fullscreen) &&
	   m_backend != nullptr)
	{
		m_backend->toggle_fullscreen();
	}
	if(m_input_manager.consume_shortcut(ShortcutAction::SaveState))
	{
		save_state();
	}
	if(m_input_manager.consume_shortcut(ShortcutAction::LoadState))
	{
		load_state();
	}
	if(m_input_manager.consume_shortcut(ShortcutAction::NextStateSlot))
	{
		change_state_slot(1);
	}
	if(m_input_manager.consume_shortcut(ShortcutAction::PreviousStateSlot))
	{
		change_state_slot(-1);
	}
}

void EmulatorFrontend::start_player_capture(size_t player, GbButton button)
{
	if(player >= m_input_settings.players.size())
	{
		return;
	}

	const InputDeviceId& device = m_input_settings.players[player].device;
	if(device.type == InputDeviceType::None)
	{
		notify(i18n::tr("Select a device for this player before binding controls"));
		return;
	}

	m_capture_target = InputCaptureTarget::PlayerBinding;
	m_capture_player = player;
	m_capture_button = button;

	const InputManager::CaptureKind kind =
		(device.type == InputDeviceType::Keyboard)
		? InputManager::CaptureKind::Key
		: InputManager::CaptureKind::Controller;
	const std::string description = i18n::format("Player %zu: %s",
		player + 1, gb_button_label(button));
	const std::string controller_guid =
		(device.type == InputDeviceType::Controller) ? device.guid : "";
	m_input_manager.begin_capture(kind, description, controller_guid);
}

void EmulatorFrontend::start_shortcut_capture(ShortcutAction action,
	bool keyboard_slot)
{
	m_capture_target = keyboard_slot
		? InputCaptureTarget::ShortcutKeyboard
		: InputCaptureTarget::ShortcutController;
	m_capture_action = action;

	const InputManager::CaptureKind kind = keyboard_slot
		? InputManager::CaptureKind::Key
		: InputManager::CaptureKind::Controller;
	const std::string description = i18n::format("%s (%s)",
		shortcut_label(action),
		i18n::tr(keyboard_slot ? "keyboard" : "controller"));
	m_input_manager.begin_capture(kind, description);
}

void EmulatorFrontend::apply_input_capture()
{
	HostControl control;
	bool cleared = false;
	if(!m_input_manager.take_capture(control, cleared))
	{
		return;
	}

	const InputCaptureTarget target = m_capture_target;
	m_capture_target = InputCaptureTarget::None;

	const auto shortcut_using = [this](const HostControl& value)
		-> const char*
	{
		for(size_t i = 0; i < m_input_settings.shortcuts.size(); ++i)
		{
			const Shortcut& shortcut = m_input_settings.shortcuts[i];
			if(!value.is_none() &&
			   (shortcut.keyboard == value || shortcut.controller == value))
			{
				return shortcut_label(static_cast<ShortcutAction>(i));
			}
		}
		return nullptr;
	};

	if(target == InputCaptureTarget::PlayerBinding)
	{
		if(m_capture_player >= m_input_settings.players.size())
		{
			return;
		}
		PlayerInputSettings& player =
			m_input_settings.players[m_capture_player];
		const size_t index = static_cast<size_t>(m_capture_button);
		if(index >= player.bindings.size())
		{
			return;
		}

		if(cleared)
		{
			player.bindings[index] = {};
			notify(i18n::format("Binding cleared: %s",
				gb_button_label(m_capture_button)));
		}
		else if(const char* conflict = shortcut_using(control))
		{
			notify(i18n::format(
				"Control already used by the %s key binding", conflict));
		}
		else
		{
			const GbButton replaced = m_capture_button;
			std::string duplicate_name;
			for(size_t i = 0; i < player.bindings.size(); ++i)
			{
				if(i != index && player.bindings[i] == control)
				{
					duplicate_name = gb_button_label(static_cast<GbButton>(i));
					player.bindings[i] = {};
				}
			}
			player.bindings[index] = control;

			if(!duplicate_name.empty())
			{
				notify(i18n::format(
					"That control was already assigned to %s; its binding "
					"was removed", duplicate_name.c_str()));
			}
			else
			{
				notify(i18n::format("%s = %s", gb_button_label(replaced),
					localized_control(control).c_str()));
			}
		}
	}
	else if(target == InputCaptureTarget::ShortcutKeyboard ||
		target == InputCaptureTarget::ShortcutController)
	{
		const bool keyboard_slot =
			target == InputCaptureTarget::ShortcutKeyboard;
		Shortcut& shortcut =
			m_input_settings.shortcuts[static_cast<size_t>(m_capture_action)];
		HostControl& slot = keyboard_slot
			? shortcut.keyboard
			: shortcut.controller;

		if(cleared)
		{
			slot = {};
			notify(i18n::format("%s binding cleared",
				shortcut_label(m_capture_action)));
		}
		else
		{
			bool duplicate_shortcut = false;
			for(size_t i = 0; i < m_input_settings.shortcuts.size(); ++i)
			{
				if(i == static_cast<size_t>(m_capture_action))
				{
					continue;
				}
				HostControl& other = keyboard_slot
					? m_input_settings.shortcuts[i].keyboard
					: m_input_settings.shortcuts[i].controller;
				if(other == control)
				{
					other = {};
					duplicate_shortcut = true;
				}
			}

			std::string removed_detail;
			for(size_t player_index = 0;
				player_index < m_input_settings.players.size(); ++player_index)
			{
				PlayerInputSettings& player =
					m_input_settings.players[player_index];
				for(size_t binding_index = 0;
					binding_index < player.bindings.size(); ++binding_index)
				{
					if(player.bindings[binding_index] == control)
					{
						player.bindings[binding_index] = {};
						if(!removed_detail.empty())
						{
							removed_detail += ", ";
						}
						removed_detail += i18n::format("Player %zu %s",
							player_index + 1,
							gb_button_label(
								static_cast<GbButton>(binding_index)));
					}
				}
			}

			slot = control;
			if(!removed_detail.empty())
			{
				notify(i18n::format("%s wins: removed %s",
					shortcut_label(m_capture_action), removed_detail.c_str()));
			}
			else if(duplicate_shortcut)
			{
				notify(i18n::format("%s = %s (moved from another shortcut)",
					shortcut_label(m_capture_action),
					localized_control(control).c_str()));
			}
			else
			{
				notify(i18n::format("%s = %s",
					shortcut_label(m_capture_action),
					localized_control(control).c_str()));
			}
		}
	}

	set_input_settings(m_input_settings);
}

void EmulatorFrontend::set_player_device(size_t player,
	const InputDeviceId& device)
{
	if(player >= m_input_settings.players.size())
	{
		return;
	}

	PlayerInputSettings& settings = m_input_settings.players[player];
	if(settings.device == device)
	{
		return;
	}

	if(m_capture_target == InputCaptureTarget::PlayerBinding &&
	   m_capture_player == player)
	{
		m_input_manager.cancel_capture();
		m_capture_target = InputCaptureTarget::None;
	}

	bool had_bindings = false;
	for(const HostControl& binding : settings.bindings)
	{
		had_bindings |= !binding.is_none();
	}

	settings.device = device;
	for(HostControl& binding : settings.bindings)
	{
		binding = {};
	}
	set_input_settings(m_input_settings);

	if(had_bindings)
	{
		notify(i18n::format(
			"Player %zu device changed; its bindings were cleared",
			player + 1));
	}
	else
	{
		notify(i18n::format("Player %zu device changed", player + 1));
	}
}

float EmulatorFrontend::input_status_bar_height() const
{
	const ImGuiStyle& style = ImGui::GetStyle();
	return ImGui::GetTextLineHeightWithSpacing() * 2.0f +
		style.WindowPadding.y * 2.0f + 4.0f;
}

void EmulatorFrontend::draw_input_status_bar(const char* primary_hint,
	const char* secondary_hint)
{
	ImGui::BeginChild("##input_status",
		ImVec2(0.0f, input_status_bar_height()), true,
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

	if(m_input_manager.capturing())
	{
		ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.35f, 1.0f),
			i18n::tr("Capturing %s"),
			m_input_manager.capture_description().c_str());
		ImGui::TextDisabled("%s", i18n::tr(
			"Press a key or button. Esc or Backspace clears."));
	}
	else if(!m_state_message.empty() &&
		ImGui::GetTime() < m_state_message_until)
	{
		ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "%s",
			m_state_message.c_str());
		ImGui::TextDisabled("%s", secondary_hint);
	}
	else
	{
		ImGui::TextDisabled("%s", primary_hint);
		ImGui::TextDisabled("%s", secondary_hint);
	}

	ImGui::EndChild();
}

void EmulatorFrontend::draw_controllers_window()
{
	ImGui::SetNextWindowSize(ImVec2(660.0f, 540.0f), ImGuiCond_FirstUseEver);
	const std::string window_title =
		std::string(i18n::tr("Controllers")) + "###Controllers";
	if(!ImGui::Begin(window_title.c_str(), &m_show_controllers_window))
	{
		if(m_capture_target == InputCaptureTarget::PlayerBinding)
		{
			m_input_manager.cancel_capture();
			m_capture_target = InputCaptureTarget::None;
		}
		ImGui::End();
		return;
	}

	if(ImGui::Button(i18n::tr("Refresh devices")))
	{
		m_input_manager.refresh_devices();
		notify(i18n::tr("Controller list refreshed"));
	}
	ImGui::SameLine();
	if(ImGui::Button(i18n::tr("Reset controllers")))
	{
		if(m_capture_target == InputCaptureTarget::PlayerBinding)
		{
			m_input_manager.cancel_capture();
			m_capture_target = InputCaptureTarget::None;
		}
		const InputSettings defaults = InputSettings::defaults();
		m_input_settings.players = defaults.players;
		m_input_settings.allow_opposite_directions =
			defaults.allow_opposite_directions;
		set_input_settings(m_input_settings);
		notify(i18n::tr("Controller settings reset to defaults"));
	}

	const float status_height = input_status_bar_height();
	const float footer_height = ImGui::GetFrameHeightWithSpacing() +
		status_height + ImGui::GetStyle().ItemSpacing.y;
	if(ImGui::BeginChild("##controller_players",
		ImVec2(0.0f, -footer_height), true))
	{
		for(size_t player = 0; player < m_input_settings.players.size();
			++player)
		{
			ImGui::PushID(static_cast<int>(player));
			ImGui::Text(i18n::tr("Player %d"), static_cast<int>(player + 1));
			PlayerInputSettings& settings =
				m_input_settings.players[player];

			ImGui::SetNextItemWidth(240.0f);
			const std::string preview =
				localized_device(m_input_manager, settings.device);
			if(ImGui::BeginCombo(i18n::tr("Device"), preview.c_str()))
			{
				if(ImGui::Selectable(i18n::tr("None"),
					settings.device.type == InputDeviceType::None))
				{
					set_player_device(player, InputDeviceId {});
				}
				InputDeviceId keyboard;
				keyboard.type = InputDeviceType::Keyboard;
				if(ImGui::Selectable(i18n::tr("Keyboard"),
					settings.device.type == InputDeviceType::Keyboard))
				{
					set_player_device(player, keyboard);
				}
				for(const ControllerInfo& info :
					m_input_manager.controllers())
				{
					ImGui::PushID(static_cast<int>(info.instance_id));
					const bool selected =
						settings.device.type == InputDeviceType::Controller &&
						settings.device.guid == info.device.guid;
					if(ImGui::Selectable(info.name.c_str(), selected))
					{
						set_player_device(player, info.device);
					}
					ImGui::PopID();
				}
				ImGui::EndCombo();
			}

			if(settings.device.type == InputDeviceType::Controller &&
			   !m_input_manager.is_device_connected(settings.device))
			{
				ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "%s",
					i18n::tr("Controller not connected. Bindings stay "
						"assigned and become active when it reconnects."));
			}

			const ImGuiTableFlags table_flags =
				ImGuiTableFlags_BordersInnerV |
				ImGuiTableFlags_RowBg |
				ImGuiTableFlags_SizingStretchProp;
			if(ImGui::BeginTable("##bindings", 4, table_flags))
			{
				ImGui::TableSetupColumn(i18n::tr("Game Boy"),
					ImGuiTableColumnFlags_WidthFixed, 90.0f);
				ImGui::TableSetupColumn(i18n::tr("Control"),
					ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn(i18n::tr("Bind"),
					ImGuiTableColumnFlags_WidthFixed, 54.0f);
				ImGui::TableSetupColumn(i18n::tr("Clear"),
					ImGuiTableColumnFlags_WidthFixed, 54.0f);
				ImGui::TableHeadersRow();

				for(size_t i = 0; i < settings.bindings.size(); ++i)
				{
					ImGui::PushID(static_cast<int>(i));
					// Keep the row namespace separate from the table's
					// numeric column IDs used by TableHeadersRow().
					ImGui::PushID("binding");
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextUnformatted(gb_button_label(
						static_cast<GbButton>(i)));

					const HostControl& binding = settings.bindings[i];
					ImGui::TableSetColumnIndex(1);
					const std::string label = localized_control(binding);
					if(ImGui::Selectable((label + "##control").c_str()))
					{
						start_player_capture(player,
							static_cast<GbButton>(i));
					}
					if(ImGui::IsItemHovered())
					{
						ImGui::SetTooltip("%s", i18n::tr("Click to bind a new control"));
					}

					ImGui::TableSetColumnIndex(2);
					if(ImGui::Button(i18n::tr("Bind"), ImVec2(48.0f, 0.0f)))
					{
						start_player_capture(player,
							static_cast<GbButton>(i));
					}
					ImGui::TableSetColumnIndex(3);
					ImGui::BeginDisabled(binding.is_none());
					if(ImGui::Button(i18n::tr("Clear"), ImVec2(48.0f, 0.0f)))
					{
						settings.bindings[i] = {};
						set_input_settings(m_input_settings);
						notify(i18n::format("Binding cleared: %s",
							gb_button_label(static_cast<GbButton>(i))));
					}
					ImGui::EndDisabled();
					ImGui::PopID(); // "binding"
					ImGui::PopID(); // button index
				}
				ImGui::EndTable();
			}

			if(player + 1 < m_input_settings.players.size())
			{
				ImGui::Spacing();
				ImGui::Separator();
				ImGui::Spacing();
			}
			ImGui::PopID();
		}
	}
	ImGui::EndChild();

	if(ImGui::Checkbox(i18n::tr("Allow opposite directions"),
		&m_input_settings.allow_opposite_directions))
	{
		set_input_settings(m_input_settings);
	}
	if(ImGui::IsItemHovered())
	{
		ImGui::SetTooltip("%s", i18n::tr(
			"When disabled, the latest pressed direction wins "
			"and the other one returns when it is released."));
	}

	draw_input_status_bar(
		i18n::tr("Click a control or Bind and press the new control."),
		i18n::tr("Analog sticks/triggers: 60% press, 40% release. "
			"Esc or Backspace clears."));

	ImGui::End();
}

void EmulatorFrontend::draw_key_bindings_window()
{
	ImGui::SetNextWindowSize(ImVec2(820.0f, 340.0f), ImGuiCond_FirstUseEver);
	const std::string window_title =
		std::string(i18n::tr("Key bindings")) + "###KeyBindings";
	if(!ImGui::Begin(window_title.c_str(), &m_show_key_bindings_window))
	{
		if(m_capture_target == InputCaptureTarget::ShortcutKeyboard ||
		   m_capture_target == InputCaptureTarget::ShortcutController)
		{
			m_input_manager.cancel_capture();
			m_capture_target = InputCaptureTarget::None;
		}
		ImGui::End();
		return;
	}

	if(ImGui::Button(i18n::tr("Reset key bindings")))
	{
		if(m_capture_target == InputCaptureTarget::ShortcutKeyboard ||
		   m_capture_target == InputCaptureTarget::ShortcutController)
		{
			m_input_manager.cancel_capture();
			m_capture_target = InputCaptureTarget::None;
		}
		const InputSettings defaults = InputSettings::defaults();
		m_input_settings.shortcuts = defaults.shortcuts;
		set_input_settings(m_input_settings);
		notify(i18n::tr("Key bindings reset to defaults"));
	}

	ImGui::Separator();

	const float status_height = input_status_bar_height();
	if(ImGui::BeginChild("##shortcut_list",
		ImVec2(0.0f, -(status_height + ImGui::GetStyle().ItemSpacing.y)),
		true))
	{
		const ImGuiTableFlags table_flags =
			ImGuiTableFlags_BordersInnerV |
			ImGuiTableFlags_RowBg |
			ImGuiTableFlags_SizingStretchProp;
		if(ImGui::BeginTable("##shortcuts", 7, table_flags))
		{
			ImGui::TableSetupColumn(i18n::tr("Action"),
				ImGuiTableColumnFlags_WidthFixed, 110.0f);
			ImGui::TableSetupColumn(i18n::tr("Keyboard"),
				ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn(i18n::tr("Bind"),
				ImGuiTableColumnFlags_WidthFixed, 54.0f);
			ImGui::TableSetupColumn(i18n::tr("Clear"),
				ImGuiTableColumnFlags_WidthFixed, 54.0f);
			ImGui::TableSetupColumn(i18n::tr("Controller"),
				ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn(i18n::tr("Bind"),
				ImGuiTableColumnFlags_WidthFixed, 54.0f);
			ImGui::TableSetupColumn(i18n::tr("Clear"),
				ImGuiTableColumnFlags_WidthFixed, 54.0f);
			ImGui::TableHeadersRow();

			for(size_t i = 0; i < m_input_settings.shortcuts.size(); ++i)
			{
				const ShortcutAction action = static_cast<ShortcutAction>(i);
				Shortcut& shortcut = m_input_settings.shortcuts[i];
				ImGui::PushID(static_cast<int>(i));
				ImGui::PushID("shortcut");
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextUnformatted(shortcut_label(action));

				ImGui::TableSetColumnIndex(1);
				const std::string keyboard_label =
					localized_control(shortcut.keyboard);
				if(ImGui::Selectable((keyboard_label + "##keyboard").c_str()))
				{
					start_shortcut_capture(action, true);
				}
				ImGui::TableSetColumnIndex(2);
				if(ImGui::Button((std::string(i18n::tr("Bind")) + "##keyboard").c_str(), ImVec2(48.0f, 0.0f)))
				{
					start_shortcut_capture(action, true);
				}
				ImGui::TableSetColumnIndex(3);
				ImGui::BeginDisabled(shortcut.keyboard.is_none());
				if(ImGui::Button((std::string(i18n::tr("Clear")) + "##keyboard").c_str(), ImVec2(48.0f, 0.0f)))
				{
					shortcut.keyboard = {};
					set_input_settings(m_input_settings);
					notify(i18n::format(
						"%s keyboard binding cleared",
						shortcut_label(action)));
				}
				ImGui::EndDisabled();

				ImGui::TableSetColumnIndex(4);
				const std::string controller_label =
					localized_control(shortcut.controller);
				if(ImGui::Selectable(
					(controller_label + "##controller").c_str()))
				{
					start_shortcut_capture(action, false);
				}
				ImGui::TableSetColumnIndex(5);
				if(ImGui::Button((std::string(i18n::tr("Bind")) + "##controller").c_str(), ImVec2(48.0f, 0.0f)))
				{
					start_shortcut_capture(action, false);
				}
				ImGui::TableSetColumnIndex(6);
				ImGui::BeginDisabled(shortcut.controller.is_none());
				if(ImGui::Button((std::string(i18n::tr("Clear")) + "##controller").c_str(), ImVec2(48.0f, 0.0f)))
				{
					shortcut.controller = {};
					set_input_settings(m_input_settings);
					notify(i18n::format(
						"%s controller binding cleared",
						shortcut_label(action)));
				}
				ImGui::EndDisabled();
				ImGui::PopID(); // "shortcut"
				ImGui::PopID(); // action index
			}
			ImGui::EndTable();
		}
	}
	ImGui::EndChild();

	draw_input_status_bar(
		i18n::tr("Click a slot or Bind. Keyboard slots accept keys; controller "
			"slots accept any controller."),
		i18n::tr("Esc or Backspace clears a capture."));

	ImGui::End();
}

void EmulatorFrontend::open_state_browser()
{
	m_show_state_browser = true;
	m_state_browser_dirty = true;
}

void EmulatorFrontend::destroy_state_previews()
{
	if(m_backend != nullptr)
	{
		for(SlotPreview& card : m_slot_previews)
		{
			if(card.texture.id != nullptr)
			{
				m_backend->destroyTexture(card.texture.id);
			}
		}
		if(m_session_preview.texture.id != nullptr)
		{
			m_backend->destroyTexture(m_session_preview.texture.id);
		}
	}
	m_slot_previews = {};
	m_session_preview = {};
}

void EmulatorFrontend::update_preview_texture(PreviewTexture& texture,
	const std::vector<std::uint32_t>& pixels, int width, int height)
{
	if(m_backend == nullptr || width <= 0 || height <= 0 ||
	   pixels.size() != static_cast<size_t>(width) *
		   static_cast<size_t>(height))
	{
		return;
	}
	if(texture.id != nullptr &&
	   (texture.width != width || texture.height != height))
	{
		m_backend->destroyTexture(texture.id);
		texture.id = nullptr;
	}
	if(texture.id == nullptr)
	{
		texture.id = m_backend->createTexture(width, height);
		texture.width = width;
		texture.height = height;
	}
	if(texture.id != nullptr)
	{
		m_backend->updateTexture(texture.id, pixels.data(), width, height);
	}
}

void EmulatorFrontend::refresh_state_browser()
{
	m_state_browser_dirty = false;
	m_state_browser_link_mode = link_session_active();
	m_state_browser_rom_path = primary() != nullptr ?
		primary()->rom_path() : std::string();
	destroy_state_previews();

	if(m_state_browser_link_mode)
	{
		m_session_preview.path = session_state_path();
		if(m_session_preview.path.empty())
		{
			return;
		}
		std::error_code error;
		m_session_preview.file_size = static_cast<std::uint64_t>(
			std::filesystem::file_size(
				std::filesystem::u8path(m_session_preview.path), error));
		m_session_preview.exists = !error &&
			m_session_preview.file_size > 0;
		if(!m_session_preview.exists)
		{
			return;
		}
		std::string load_error;
		if(SessionState::load(m_session_preview.path,
			   m_session_preview.state, load_error))
		{
			m_session_preview.readable = true;
			if(!m_session_preview.state.preview_pixels.empty())
			{
				update_preview_texture(m_session_preview.texture,
					m_session_preview.state.preview_pixels,
					static_cast<int>(m_session_preview.state.preview_width),
					static_cast<int>(m_session_preview.state.preview_height));
			}
		}
		return;
	}

	EmulatorInstance* first = primary();
	if(first == nullptr || !first->is_ready())
	{
		return;
	}
	for(int slot = 0; slot < 10; ++slot)
	{
		SlotPreview& card = m_slot_previews[static_cast<size_t>(slot)];
		StateStore store;
		store.set_rom_path(first->rom_path());
		store.set_path_settings(m_path_settings);
		store.set_slot(slot);
		card.path = store.path();
		std::error_code error;
		card.file_size = static_cast<std::uint64_t>(
			std::filesystem::file_size(
				std::filesystem::u8path(card.path), error));
		card.exists = !error && card.file_size > 0;
		if(!card.exists)
		{
			continue;
		}
		std::vector<std::uint8_t> blob;
		SavestateInfo info;
		if(!store.load(blob, &info))
		{
			continue;
		}
		card.readable = true;
		card.info = std::move(info);
		if(!card.info.preview_pixels.empty())
		{
			update_preview_texture(card.texture, card.info.preview_pixels,
				static_cast<int>(card.info.preview_width),
				static_cast<int>(card.info.preview_height));
		}
	}
}

void EmulatorFrontend::draw_state_browser()
{
	const std::string current_rom = primary() != nullptr ?
		primary()->rom_path() : std::string();
	if(m_state_browser_dirty ||
	   m_state_browser_link_mode != link_session_active() ||
	   m_state_browser_rom_path != current_rom)
	{
		refresh_state_browser();
	}

	ImGui::SetNextWindowSize(ImVec2(720.0f, 500.0f),
		ImGuiCond_FirstUseEver);
	const std::string title = std::string(i18n::tr("State browser")) +
		"###StateBrowser";
	if(!ImGui::Begin(title.c_str(), &m_show_state_browser))
	{
		ImGui::End();
		return;
	}

	if(ImGui::Button(i18n::tr("Refresh")))
	{
		m_state_browser_dirty = true;
	}
	ImGui::SameLine();
	if(link_session_active())
	{
		ImGui::TextDisabled("%s", i18n::tr("Link session"));
	}
	else if(primary() != nullptr && primary()->is_ready())
	{
		const std::string path = primary()->rom_path();
		const size_t separator = path.find_last_of("/\\");
		const std::string name = separator == std::string::npos ?
			path : path.substr(separator + 1);
		ImGui::TextDisabled("%s", name.c_str());
	}
	else
	{
		ImGui::TextDisabled("%s", i18n::tr("No ROM loaded"));
	}
	ImGui::Separator();

	const bool can_save = link_session_active() ||
		(primary() != nullptr && primary()->is_ready());
	const auto draw_slot = [&](int slot)
	{
		SlotPreview& card = m_slot_previews[static_cast<size_t>(slot)];
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::PushID(slot);
		if(card.texture.id != nullptr)
		{
			ImGui::Image(card.texture.id, ImVec2(128.0f, 115.0f));
			if(ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("%s", card.path.c_str());
			}
		}
		else if(card.exists)
		{
			ImGui::Dummy(ImVec2(128.0f, 24.0f));
			ImGui::TextDisabled("%s", i18n::tr("No preview"));
		}
		else
		{
			ImGui::Dummy(ImVec2(128.0f, 20.0f));
		}

		ImGui::TableSetColumnIndex(1);
		ImGui::Text("%s", i18n::format("Slot %d", slot).c_str());
		if(slot == m_state_slot)
		{
			ImGui::SameLine();
			ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f),
				"(%s)", i18n::tr("Current"));
		}
		if(card.readable)
		{
			const std::string name = !card.info.rom_title.empty() ?
				card.info.rom_title : card.info.rom_filename;
			if(!name.empty())
			{
				ImGui::TextUnformatted(name.c_str());
			}
			const std::string date = format_state_timestamp(
				card.info.created_unix_seconds);
			if(!date.empty())
			{
				ImGui::TextDisabled("%s",
					i18n::format("Saved: %s", date.c_str()).c_str());
			}
			ImGui::TextDisabled("%s",
				i18n::format("Frames: %llu (%s)",
					static_cast<unsigned long long>(
						card.info.emulated_frames),
					format_emulated_time(
						card.info.emulated_frames).c_str()).c_str());
			ImGui::TextDisabled("%s",
				i18n::format("Size: %s",
					format_file_size(card.file_size).c_str()).c_str());
		}
		else if(card.exists)
		{
			ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.25f, 1.0f), "%s",
				i18n::tr("Unreadable state"));
			ImGui::TextDisabled("%s",
				i18n::format("Size: %s",
					format_file_size(card.file_size).c_str()).c_str());
		}
		else
		{
			ImGui::TextDisabled("%s", i18n::tr("Empty slot"));
		}

		if(card.readable)
		{
			if(ImGui::Button(i18n::tr("Load")))
			{
				set_state_slot(slot);
				if(load_state())
				{
					m_show_state_browser = false;
				}
				else
				{
					m_state_browser_dirty = true;
				}
			}
			ImGui::SameLine();
		}
		ImGui::BeginDisabled(!can_save);
		if(ImGui::Button(i18n::tr("Save here")))
		{
			set_state_slot(slot);
			save_state();
			m_state_browser_dirty = true;
		}
		ImGui::EndDisabled();
		ImGui::PopID();
	};

	const auto draw_session = [&]()
	{
		SessionPreview& card = m_session_preview;
		ImGui::PushID("session");
		ImGui::Text("%s", i18n::tr("Link session"));
		if(card.readable)
		{
			const std::string names =
				card.state.roms[0].name + " + " +
				card.state.roms[1].name;
			ImGui::TextUnformatted(names.c_str());
			const std::string date = format_state_timestamp(
				card.state.created_unix_seconds);
			if(!date.empty())
			{
				ImGui::TextDisabled("%s",
					i18n::format("Saved: %s", date.c_str()).c_str());
			}
			ImGui::TextDisabled("%s",
				i18n::format("Frames: %llu (%s)",
					static_cast<unsigned long long>(
						card.state.emulated_frames),
					format_emulated_time(
						card.state.emulated_frames).c_str()).c_str());
			ImGui::TextDisabled("%s",
				i18n::format("Size: %s",
					format_file_size(card.file_size).c_str()).c_str());
			if(card.texture.id != nullptr)
			{
				ImGui::Image(card.texture.id, ImVec2(480.0f, 216.0f));
			}
			else
			{
				ImGui::Dummy(ImVec2(480.0f, 216.0f));
				ImGui::TextDisabled("%s", i18n::tr("No preview"));
			}
			if(ImGui::Button(i18n::tr("Load")))
			{
				if(load_state())
				{
					m_show_state_browser = false;
				}
				else
				{
					m_state_browser_dirty = true;
				}
			}
			ImGui::SameLine();
		}
		else if(card.exists)
		{
			ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.25f, 1.0f), "%s",
				i18n::tr("Unreadable state"));
			ImGui::TextDisabled("%s",
				i18n::format("Size: %s",
					format_file_size(card.file_size).c_str()).c_str());
		}
		else
		{
			ImGui::TextDisabled("%s", i18n::tr("No session state saved"));
		}
		ImGui::BeginDisabled(!link_session_active());
		if(ImGui::Button(i18n::tr("Save state")))
		{
			save_state();
			m_state_browser_dirty = true;
		}
		ImGui::EndDisabled();
		ImGui::PopID();
	};

	if(ImGui::BeginChild("##state_cards", ImVec2(0.0f, 0.0f), false))
	{
		if(link_session_active())
		{
			draw_session();
		}
		else if(primary() == nullptr || !primary()->is_ready())
		{
			ImGui::TextUnformatted(i18n::tr("No ROM loaded"));
		}
		else
		{
			const ImGuiTableFlags flags = ImGuiTableFlags_RowBg |
				ImGuiTableFlags_BordersInnerH |
				ImGuiTableFlags_SizingFixedFit;
			if(ImGui::BeginTable("##slots", 2, flags))
			{
				ImGui::TableSetupColumn("##preview",
					ImGuiTableColumnFlags_WidthFixed, 136.0f);
				ImGui::TableSetupColumn("##details",
					ImGuiTableColumnFlags_WidthStretch);
				for(int slot = 0; slot < 10; ++slot)
				{
					draw_slot(slot);
				}
				ImGui::EndTable();
			}
		}
	}
	ImGui::EndChild();
	ImGui::End();
}

void EmulatorFrontend::save_state()
{
	m_state_browser_dirty = true;
	if(link_session_active())
	{
		std::string error;
		const std::string path = session_state_path();
		if(save_session_state(error))
		{
			notify(i18n::format("Session state saved: %s", path.c_str()));
		}
		else
		{
			notify(i18n::format("Session state failed: %s", error.c_str()));
		}
		return;
	}

	if(!primary())
	{
		return;
	}

	const std::string path = primary()->state_path();
	if(primary()->save_state(m_emulated_frames))
	{
		notify(i18n::format("State saved: %s", path.c_str()));
	}
	else
	{
		notify(path.empty()
			? i18n::tr("Save state failed: no ROM loaded")
			: i18n::tr("Save state failed"));
	}
}

bool EmulatorFrontend::load_state()
{
	m_state_browser_dirty = true;
	if(link_session_active())
	{
		std::string error;
		const std::string path = session_state_path();
		if(load_session_state(error))
		{
			notify(i18n::format("Session state loaded: %s", path.c_str()));
			return true;
		}
		notify(i18n::format("Session state failed: %s", error.c_str()));
		return false;
	}

	if(!primary())
	{
		return false;
	}

	const std::string path = primary()->state_path();
	SavestateInfo info;
	if(primary()->load_state(&info))
	{
		m_emulated_frames = info.emulated_frames;
		notify(i18n::format("State loaded: %s", path.c_str()));
		return true;
	}
	notify(path.empty()
		? i18n::tr("Load state failed: no ROM loaded")
		: i18n::tr(
			"Load state failed (missing/corrupt state or different ROM)"));
	return false;
}

EmulatorInstance* EmulatorFrontend::get_emulator()
{
	return primary();
}

EmulatorInstance* EmulatorFrontend::primary()
{
	return m_instances.empty() ? nullptr : m_instances.front().get();
}

EmulatorInstance* EmulatorFrontend::instance(size_t index)
{
	return index < m_instances.size() ? m_instances[index].get() : nullptr;
}

EmulatorInstance* EmulatorFrontend::add_instance()
{
	if(m_instances.size() >= 2)
	{
		return nullptr;
	}
	const u8 player_index = static_cast<u8>(m_instances.size() + 1);
	auto instance = std::make_unique<EmulatorInstance>(m_backend, player_index);
	EmulatorInstance* result = instance.get();
	instance->set_audio_settings(m_audio_settings);
	instance->set_path_settings(m_path_settings);
	instance->set_state_slot(m_state_slot);
	m_instances.push_back(std::move(instance));

	if(m_instances.size() == 2)
	{
		m_link = std::make_unique<LinkSession>();
		m_link->connect(*m_instances[0], *m_instances[1]);
	}
	return result;
}

void EmulatorFrontend::load_rom_player2(const std::string& filename)
{
	std::vector<u8> rom;
	if(!read_binary_file(filename, rom) || !valid_rom(rom))
	{
		notify(i18n::tr("Player 2: could not open ROM"));
		return;
	}

	EmulatorInstance* second = instance(1);
	if(second == nullptr)
	{
		start_link_session(filename);
		return;
	}

	apply_bios_to_instance(*second, m_session_bios);
	second->load_cart(std::move(rom), filename);
	if(!second->is_ready())
	{
		notify(i18n::tr("Player 2: invalid ROM"));
		return;
	}
	reset_link_transfer();
	m_state_browser_dirty = true;
	update_frame();
	notify(i18n::format("Player 2 ROM loaded: %s", filename.c_str()));
}

bool EmulatorFrontend::start_link_session(const std::string& rom_path)
{
	if(link_session_active() || primary() == nullptr ||
	   !primary()->is_ready())
	{
		return false;
	}
	if(m_instances.size() >= 2)
	{
		stop_link_session();
	}

	std::vector<u8> rom;
	if(!read_binary_file(rom_path, rom) || !valid_rom(rom))
	{
		notify(i18n::tr("Player 2: could not open ROM"));
		return false;
	}

	m_session_bios = primary()->boot_rom();
	m_session_bios_path = m_path_settings.bios;

	EmulatorInstance* second = add_instance();
	if(second == nullptr)
	{
		notify(i18n::tr("Player 2: could not create instance"));
		return false;
	}
	apply_bios_to_instance(*second, m_session_bios);
	second->load_cart(std::move(rom), rom_path);
	if(!second->is_ready())
	{
		notify(i18n::tr("Player 2: invalid ROM"));
		stop_link_session();
		return false;
	}

	reset_link_transfer();
	apply_audio_routing();
	m_state_browser_dirty = true;
	update_frame();
	notify(i18n::format("Player 2 ROM loaded: %s", rom_path.c_str()));
	return true;
}

void EmulatorFrontend::stop_link_session()
{
	EmulatorInstance* first = primary();
	EmulatorInstance* second = instance(1);
	if(m_link)
	{
		m_link->disconnect();
	}
	if(first != nullptr)
	{
		first->machine().m_serial.reset();
		apply_bios_to_instance(*first, m_bios);
	}
	if(second != nullptr && m_instances.size() > 1)
	{
		m_instances.pop_back();
	}
	m_link.reset();
	m_session_bios.clear();
	m_session_bios_path.clear();
	apply_audio_routing();
	m_state_browser_dirty = true;
	notify(i18n::tr("Session stopped"));
}

void EmulatorFrontend::reset_link_transfer()
{
	if(m_instances.size() < 2)
	{
		return;
	}
	if(m_link)
	{
		m_link->disconnect();
	}
	for(size_t i = 0; i < 2; ++i)
	{
		m_instances[i]->machine().m_serial.reset();
	}
	if(!m_link)
	{
		m_link = std::make_unique<LinkSession>();
	}
	m_link->connect(*m_instances[0], *m_instances[1]);
}

bool EmulatorFrontend::link_session_active() const
{
	return m_link != nullptr && m_link->connected() &&
		m_instances.size() >= 2 && m_instances[1]->is_ready();
}

bool EmulatorFrontend::build_session_state(SessionState& state,
	std::string& error) const
{
	error.clear();
	if(!link_session_active())
	{
		error = "No active link session";
		return false;
	}

	state = {};
	for(size_t i = 0; i < 2; ++i)
	{
		const EmulatorInstance* inst = m_instances[i].get();
		const std::string& path = inst->rom_path();
		state.roms[i].path = path;
		state.roms[i].name = std::filesystem::u8path(path)
			.filename().u8string();
		state.roms[i].size = static_cast<std::uint32_t>(
			inst->rom_data().size());
		state.roms[i].hash = qtreGB::state_hash_rom(inst->rom_data());
		if(!inst->machine().save_state(state.states[i]) ||
		   state.states[i].empty())
		{
			error = "Could not serialize machine " + std::to_string(i + 1);
			return false;
		}
	}

	state.bios_path = m_session_bios_path;
	state.bios_hash = qtreGB::state_hash_bytes(m_session_bios.data(),
		m_session_bios.size());
	state.bios_size = static_cast<std::uint32_t>(m_session_bios.size());
	state.created_unix_seconds = static_cast<std::uint64_t>(
		std::chrono::duration_cast<std::chrono::seconds>(
			std::chrono::system_clock::now().time_since_epoch()).count());
	state.emulated_frames = m_emulated_frames;
	state.app_version = QTREGB_BUILD_VERSION;
	if(compose_combined_frame(state.preview_pixels))
	{
		state.preview_width = GB_XRES * 2;
		state.preview_height = GB_YRES;
	}
	else
	{
		state.preview_pixels.clear();
	}
	return true;
}

std::string EmulatorFrontend::session_state_path() const
{
	if(!link_session_active())
	{
		return {};
	}

	SessionState info;
	for(size_t i = 0; i < 2; ++i)
	{
		const EmulatorInstance* inst = m_instances[i].get();
		info.roms[i].path = inst->rom_path();
		info.roms[i].name = std::filesystem::u8path(inst->rom_path())
			.filename().u8string();
		info.roms[i].size = static_cast<std::uint32_t>(
			inst->rom_data().size());
		info.roms[i].hash = qtreGB::state_hash_rom(inst->rom_data());
	}
	return SessionState::path_for(m_path_settings, info);
}

bool EmulatorFrontend::save_session_state(std::string& error)
{
	error.clear();
	if(!link_session_active())
	{
		error = "No active link session";
		return false;
	}

	for(const auto& instance : m_instances)
	{
		instance->save_battery_now();
	}

	SessionState state;
	if(!build_session_state(state, error))
	{
		return false;
	}
	const std::string path = session_state_path();
	return SessionState::save(path, state, error);
}

bool EmulatorFrontend::locate_session_rom(const SessionRomInfo& expected,
	std::vector<u8>& rom, std::string& found_path, std::string& error) const
{
	found_path.clear();
	const auto matches = [&expected](const std::vector<u8>& data)
	{
		return !data.empty() &&
			data.size() == expected.size &&
			qtreGB::state_hash_rom(data) == expected.hash;
	};

	std::vector<std::string> candidates;
	if(!expected.path.empty())
	{
		candidates.push_back(expected.path);
	}
	if(!m_path_settings.roms.empty())
	{
		const std::string filename = std::filesystem::u8path(
			!expected.name.empty() ? expected.name : expected.path)
			.filename().u8string();
		if(!filename.empty())
		{
			candidates.push_back((std::filesystem::u8path(
				m_path_settings.roms) / std::filesystem::u8path(filename))
				.u8string());
		}
	}

	std::string mismatch;
	for(const std::string& candidate : candidates)
	{
		if(read_binary_file(candidate, rom))
		{
			if(matches(rom))
			{
				found_path = candidate;
				return true;
			}
			mismatch = " (found " + candidate + " with hash " +
				hex_hash(qtreGB::state_hash_rom(rom)) + ")";
		}
	}
	error = "Expected ROM " + expected.name + " with hash " +
		hex_hash(expected.hash) + " was not found" + mismatch;
	return false;
}

bool EmulatorFrontend::load_session_state(std::string& error)
{
	error.clear();
	if(!link_session_active())
	{
		error = "No active link session";
		return false;
	}

	SessionState state;
	const std::string path = session_state_path();
	if(!SessionState::load(path, state, error))
	{
		return false;
	}

	bool reload[2] = {false, false};
	std::vector<u8> roms[2];
	std::string rom_paths[2];
	for(size_t i = 0; i < 2; ++i)
	{
		EmulatorInstance* inst = m_instances[i].get();
		const std::vector<u8>& current = inst->rom_data();
		if(current.size() == state.roms[i].size &&
		   qtreGB::state_hash_rom(current) == state.roms[i].hash)
		{
			rom_paths[i] = inst->rom_path();
			continue;
		}
		if(!locate_session_rom(state.roms[i], roms[i], rom_paths[i], error))
		{
			error = "Machine " + std::to_string(i + 1) + ": " + error +
				" (current hash " + hex_hash(qtreGB::state_hash_rom(current)) +
				")";
			return false;
		}
		reload[i] = true;
	}

	std::vector<u8> old_roms[2];
	std::string old_paths[2];
	std::vector<u8> old_states[2];
	for(size_t i = 0; i < 2; ++i)
	{
		EmulatorInstance* inst = m_instances[i].get();
		old_roms[i] = inst->rom_data();
		old_paths[i] = inst->rom_path();
		if(!inst->machine().save_state(old_states[i]) ||
		   old_states[i].empty())
		{
			error = "Could not capture the current session state";
			return false;
		}
	}

	const auto rollback = [&]()
	{
		for(size_t i = 0; i < 2; ++i)
		{
			EmulatorInstance* inst = m_instances[i].get();
			if(reload[i])
			{
				inst->load_cart(old_roms[i], old_paths[i]);
			}
			if(!old_states[i].empty())
			{
				inst->machine().load_state(old_states[i]);
			}
		}
		reset_link_transfer();
		update_frame();
	};

	for(size_t i = 0; i < 2; ++i)
	{
		if(!reload[i])
		{
			continue;
		}
		EmulatorInstance* inst = m_instances[i].get();
		inst->load_cart(std::move(roms[i]), rom_paths[i]);
		if(!inst->is_ready())
		{
			rollback();
			error = "Machine " + std::to_string(i + 1) +
				": could not load the expected ROM";
			return false;
		}
	}

	for(size_t i = 0; i < 2; ++i)
	{
		EmulatorInstance* inst = m_instances[i].get();
		const qtreGB::StateError result =
			inst->machine().load_state(state.states[i]);
		if(result != qtreGB::StateError::Ok)
		{
			rollback();
			error = "Machine " + std::to_string(i + 1) +
				": " + qtreGB::state_error_name(result);
			if(result == qtreGB::StateError::BadValue)
			{
				error += " (possibly a different BIOS or state)";
			}
			return false;
		}
	}

	m_emulated_frames = state.emulated_frames;
	m_state_browser_dirty = true;
	update_frame();
	return true;
}

bool EmulatorFrontend::link_mode() const
{
	return m_instances.size() >= 2 && m_instances[1]->is_ready();
}

void EmulatorFrontend::ensure_combined_texture()
{
	if(m_backend && m_combined_texture == nullptr)
	{
		m_combined_texture = m_backend->createTexture(GB_XRES * 2, GB_YRES);
	}
}

void EmulatorFrontend::update_frame()
{
	EmulatorInstance* first = primary();
	if(first == nullptr)
	{
		return;
	}

	// Player 2 is updated by tick() when its own PPU completes a frame.
	first->update_frame(m_display_settings);

	if(link_mode())
	{
		upload_combined_frame();
	}
}

void EmulatorFrontend::apply_display_settings()
{
	for(const auto& instance : m_instances)
	{
		instance->refresh_display(m_display_settings);
	}
	if(link_mode())
	{
		upload_combined_frame();
	}
}

bool EmulatorFrontend::compose_combined_frame(std::vector<u32>& frame) const
{
	if(m_instances.size() < 2)
	{
		return false;
	}

	const std::vector<u32>& left = m_instances[0]->display_pixels();
	const std::vector<u32>& right = m_instances[1]->display_pixels();
	if(left.size() != static_cast<size_t>(GB_XRES * GB_YRES) ||
	   right.size() != static_cast<size_t>(GB_XRES * GB_YRES))
	{
		return false;
	}

	frame.resize(static_cast<size_t>(GB_XRES * 2) * GB_YRES);
	for(int y = 0; y < GB_YRES; ++y)
	{
		const size_t left_row = static_cast<size_t>(y) * GB_XRES;
		const size_t right_row = static_cast<size_t>(y) * GB_XRES * 2;
		for(int x = 0; x < GB_XRES; ++x)
		{
			frame[right_row + x] = left[left_row + x];
			frame[right_row + GB_XRES + x] = right[left_row + x];
		}
	}
	return true;
}

void EmulatorFrontend::upload_combined_frame()
{
	if(!compose_combined_frame(m_combined_frame))
	{
		return;
	}

	ensure_combined_texture();
	if(m_backend == nullptr || m_combined_texture == nullptr)
	{
		return;
	}

	m_backend->updateTexture(m_combined_texture, m_combined_frame.data(),
		GB_XRES * 2, GB_YRES);
}

void* EmulatorFrontend::get_texture_ID()
{
	if(link_mode())
	{
		ensure_combined_texture();
		return m_combined_texture;
	}
	EmulatorInstance* first = primary();
	return first ? first->get_texture_ID() : nullptr;
}

std::pair<int,int> EmulatorFrontend::get_texture_size() const
{
	if(link_mode())
	{
		return {GB_XRES * 2, GB_YRES};
	}
	return {GB_XRES, GB_YRES};
}

std::string EmulatorFrontend::file_dialog_directory() const
{
	std::error_code error;
	if(!m_last_file_directory.empty() &&
	   std::filesystem::is_directory(m_last_file_directory, error))
	{
		return m_last_file_directory;
	}
	if(const char* home = std::getenv("HOME"))
	{
		if(home[0] != '\0')
		{
			const std::filesystem::path home_path =
				std::filesystem::u8path(home);
			if(std::filesystem::is_directory(home_path, error))
			{
				return home_path.u8string();
			}
		}
	}
	return ".";
}

std::string EmulatorFrontend::preferred_directory(
	FileDialogAction action) const
{
	std::string configured;
	switch(action)
	{
		case FileDialogAction::OpenRom:
		case FileDialogAction::OpenRomPlayer2:
			configured = m_path_settings.roms;
			break;
		case FileDialogAction::SaveScreenshot:
			configured = m_path_settings.screenshots;
			break;
		case FileDialogAction::ChooseRomsDirectory:
			configured = m_path_edit.roms;
			break;
		case FileDialogAction::ChooseBiosFile:
			if(!m_path_edit.bios.empty())
			{
				configured = std::filesystem::u8path(m_path_edit.bios)
					.parent_path().u8string();
			}
			break;
		case FileDialogAction::ChooseBatteryDirectory:
			configured = m_path_edit.battery;
			break;
		case FileDialogAction::ChooseSavestatesDirectory:
			configured = m_path_edit.savestates;
			break;
		case FileDialogAction::ChooseScreenshotsDirectory:
			configured = m_path_edit.screenshots;
			break;
		case FileDialogAction::None:
			break;
	}

	std::error_code error;
	if(!configured.empty() &&
	   std::filesystem::is_directory(std::filesystem::u8path(configured),
			error))
	{
		return configured;
	}
	return file_dialog_directory();
}

void EmulatorFrontend::remember_file_directory(const std::string& path)
{
	const std::filesystem::path parent =
		std::filesystem::path(path).parent_path();
	if(!parent.empty())
	{
		m_last_file_directory = parent.u8string();
	}
}

void EmulatorFrontend::open_file_dialog(FileDialogAction action)
{
	if(action == FileDialogAction::None ||
	   m_file_dialog_action != FileDialogAction::None)
	{
		return;
	}

	if(m_capture_target != InputCaptureTarget::None)
	{
		m_input_manager.cancel_capture();
		m_capture_target = InputCaptureTarget::None;
	}

	static constexpr const char* dialog_key = "QtreGBFileDialog";
	std::string title;
	std::string filters;
	std::string default_name;

	switch(action)
	{
		case FileDialogAction::OpenRom:
		case FileDialogAction::OpenRomPlayer2:
			title = i18n::tr("Open ROM");
			filters = std::string(i18n::tr("Game Boy ROMs")) +
				"{.gb,.gbc,.zip,.sav,.mbs,.bin}";
			break;
		case FileDialogAction::SaveScreenshot:
			title = i18n::tr("Save screenshot");
			filters = std::string(i18n::tr("BMP image")) + "{.bmp}";
			default_name = "qtrebg.bmp";
			break;
		case FileDialogAction::ChooseBiosFile:
			title = i18n::tr("Choose BIOS file");
			filters = std::string(i18n::tr("BIOS")) + "{.gb,.bin}";
			break;
		case FileDialogAction::ChooseRomsDirectory:
		case FileDialogAction::ChooseBatteryDirectory:
		case FileDialogAction::ChooseSavestatesDirectory:
		case FileDialogAction::ChooseScreenshotsDirectory:
		case FileDialogAction::None:
			return;
	}

	IGFD::FileDialogConfig config;
	config.countSelectionMax = 1;
	if(action == FileDialogAction::SaveScreenshot)
	{
		config.flags = ImGuiFileDialogFlags_Default;
	}
	else
	{
		// File-open dialogs must not ask to overwrite an existing file.
		config.flags = ImGuiFileDialogFlags_Modal |
			ImGuiFileDialogFlags_HideColumnType |
			ImGuiFileDialogFlags_CaseInsensitiveExtentionFiltering;
	}
	config.path = preferred_directory(action);
	if(!default_name.empty())
	{
		config.fileName = default_name;
	}

	m_file_dialog_action = action;
	m_input_manager.set_ui_blocked(true);
	ImGuiFileDialog::Instance()->OpenDialog(
		dialog_key, title.c_str(), filters.c_str(), config);
}

void EmulatorFrontend::draw_file_dialogs()
{
	if(m_file_dialog_action == FileDialogAction::None)
	{
		return;
	}

	static constexpr const char* dialog_key = "QtreGBFileDialog";
	if(!ImGuiFileDialog::Instance()->IsOpened(dialog_key))
	{
		m_file_dialog_action = FileDialogAction::None;
		return;
	}

	if(!ImGuiFileDialog::Instance()->Display(dialog_key,
		ImGuiWindowFlags_NoCollapse, ImVec2(720.0f, 480.0f),
		ImVec2(FLT_MAX, FLT_MAX)))
	{
		return;
	}

	const FileDialogAction action = m_file_dialog_action;
	const bool accepted = ImGuiFileDialog::Instance()->IsOk();
	const bool directory_dialog =
		action == FileDialogAction::ChooseRomsDirectory ||
		action == FileDialogAction::ChooseBatteryDirectory ||
		action == FileDialogAction::ChooseSavestatesDirectory ||
		action == FileDialogAction::ChooseScreenshotsDirectory;
	const std::string path = accepted
		? (directory_dialog
			? ImGuiFileDialog::Instance()->GetCurrentPath()
			: ImGuiFileDialog::Instance()->GetFilePathName())
		: std::string();
	m_file_dialog_action = FileDialogAction::None;
	ImGuiFileDialog::Instance()->Close();

	if(accepted && !path.empty())
	{
		remember_file_directory(path);
		handle_file_dialog_result(action, path);
	}
}

void EmulatorFrontend::handle_file_dialog_result(FileDialogAction action,
	const std::string& path)
{
	switch(action)
	{
		case FileDialogAction::OpenRom:
			open_rom_path(path);
			break;
		case FileDialogAction::OpenRomPlayer2:
			load_rom_player2(path);
			break;
		case FileDialogAction::SaveScreenshot:
			save_screenshot_to_path(path);
			break;
		case FileDialogAction::ChooseBiosFile:
			m_path_edit.bios = path;
			break;
		case FileDialogAction::ChooseRomsDirectory:
		case FileDialogAction::ChooseBatteryDirectory:
		case FileDialogAction::ChooseSavestatesDirectory:
		case FileDialogAction::ChooseScreenshotsDirectory:
			apply_directory_choice(action, path);
			break;
		case FileDialogAction::None:
			break;
	}
}

void EmulatorFrontend::open_rom_path(const std::string& path)
{
	std::vector<u8> rom;
	if(!read_binary_file(path, rom) || !valid_rom(rom))
	{
		notify(i18n::tr("Could not open the ROM file"));
		return;
	}
	if(primary() == nullptr)
	{
		return;
	}

	const bool was_linked = link_session_active();
	if(was_linked)
	{
		apply_bios_to_instance(*primary(), m_session_bios);
	}
	primary()->load_cart(std::move(rom), path);
	m_recent_roms.remember(path);
	m_emulated_frames = 0;
	m_state_browser_dirty = true;
	set_state_slot(0);
	if(was_linked)
	{
		reset_link_transfer();
		update_frame();
		notify(i18n::format("Player 1 ROM loaded: %s", path.c_str()));
	}
}

void EmulatorFrontend::open_recent_rom(size_t index)
{
	const std::vector<std::string>& paths = m_recent_roms.paths();
	if(index >= paths.size())
	{
		return;
	}
	const std::string path = paths[index];
	std::error_code error;
	if(!std::filesystem::is_regular_file(std::filesystem::u8path(path), error))
	{
		m_recent_roms.remove(path);
		notify(i18n::tr("Could not open the ROM file"));
		return;
	}
	open_rom_path(path);
}

void EmulatorFrontend::save_screenshot()
{
	open_file_dialog(FileDialogAction::SaveScreenshot);
}

void EmulatorFrontend::open_directory_dialog(FileDialogAction action)
{
	if(m_file_dialog_action != FileDialogAction::None)
	{
		return;
	}

	std::string title;
	switch(action)
	{
		case FileDialogAction::ChooseRomsDirectory:
			title = i18n::tr("Choose ROMs directory");
			break;
		case FileDialogAction::ChooseBatteryDirectory:
			title = i18n::tr("Choose battery saves directory");
			break;
		case FileDialogAction::ChooseSavestatesDirectory:
			title = i18n::tr("Choose save states directory");
			break;
		case FileDialogAction::ChooseScreenshotsDirectory:
			title = i18n::tr("Choose screenshots directory");
			break;
		default:
			return;
	}

	if(m_capture_target != InputCaptureTarget::None)
	{
		m_input_manager.cancel_capture();
		m_capture_target = InputCaptureTarget::None;
	}

	IGFD::FileDialogConfig config;
	config.countSelectionMax = 1;
	config.flags = ImGuiFileDialogFlags_Modal |
		ImGuiFileDialogFlags_HideColumnType;
	config.path = preferred_directory(action);

	m_file_dialog_action = action;
	m_input_manager.set_ui_blocked(true);
	ImGuiFileDialog::Instance()->OpenDialog(
		"QtreGBFileDialog", title.c_str(), nullptr, config);
}

void EmulatorFrontend::apply_directory_choice(FileDialogAction action,
	const std::string& path)
{
	switch(action)
	{
		case FileDialogAction::ChooseRomsDirectory:
			m_path_edit.roms = path;
			break;
		case FileDialogAction::ChooseBatteryDirectory:
			m_path_edit.battery = path;
			break;
		case FileDialogAction::ChooseSavestatesDirectory:
			m_path_edit.savestates = path;
			break;
		case FileDialogAction::ChooseScreenshotsDirectory:
			m_path_edit.screenshots = path;
			break;
		default:
			break;
	}
}

void EmulatorFrontend::open_paths_window()
{
	m_path_edit = m_path_settings;
	m_show_paths_window = true;
}

bool EmulatorFrontend::paths_edit_valid() const
{
	const auto valid_directory = [](const std::string& path)
	{
		if(path.empty())
		{
			return true;
		}
		std::error_code error;
		return std::filesystem::is_directory(
			std::filesystem::u8path(path), error);
	};

	if(!valid_directory(m_path_edit.roms) ||
	   !valid_directory(m_path_edit.screenshots))
	{
		return false;
	}

	if(!m_path_edit.bios.empty())
	{
		std::error_code error;
		const std::filesystem::path bios_path =
			std::filesystem::u8path(m_path_edit.bios);
		if(!std::filesystem::is_regular_file(bios_path, error) ||
		   std::filesystem::file_size(bios_path, error) != 0x100)
		{
			return false;
		}
	}
	if(!m_path_edit.battery_same_as_rom &&
	   !valid_directory(m_path_edit.battery))
	{
		return false;
	}
	if(!m_path_edit.savestates_same_as_rom &&
	   !valid_directory(m_path_edit.savestates))
	{
		return false;
	}
	return true;
}

void EmulatorFrontend::draw_paths_window()
{
	ImGui::SetNextWindowSize(ImVec2(780.0f, 430.0f), ImGuiCond_FirstUseEver);
	const std::string window_title =
		std::string(i18n::tr("Paths")) + "###Paths";
	if(!ImGui::Begin(window_title.c_str(), &m_show_paths_window))
	{
		ImGui::End();
		return;
	}

	ImGui::TextWrapped("%s", i18n::tr(
		"Working directories used by the file dialogs and save files. "
		"Empty paths use the last used directory; battery and save state "
		"paths fall back to the ROM folder."));

	const float footer_height = ImGui::GetFrameHeightWithSpacing() + 8.0f;
	if(ImGui::BeginChild("##path_fields", ImVec2(0.0f, -footer_height),
		true))
	{
		const auto valid_directory = [](const std::string& path)
		{
			if(path.empty())
			{
				return true;
			}
			std::error_code error;
			return std::filesystem::is_directory(
				std::filesystem::u8path(path), error);
		};

		const auto draw_row = [&](const char* label, std::string& value,
			FileDialogAction action, bool* same_as_rom)
		{
			ImGui::PushID(label);
			ImGui::TextUnformatted(i18n::tr(label));
			const bool enabled = same_as_rom == nullptr || !*same_as_rom;
			ImGui::BeginDisabled(!enabled);
			ImGui::SetNextItemWidth(
				ImGui::GetContentRegionAvail().x - 155.0f);
			path_input_text("##path", value);
			ImGui::SameLine();
			if(ImGui::Button(i18n::tr("Browse...")))
			{
				open_directory_dialog(action);
			}
			ImGui::SameLine();
			if(ImGui::Button(i18n::tr("Clear")))
			{
				value.clear();
			}
			ImGui::EndDisabled();

			if(same_as_rom != nullptr)
			{
				ImGui::Checkbox(i18n::tr("Same folder as ROM"),
					same_as_rom);
				if(*same_as_rom)
				{
					ImGui::TextDisabled("%s", i18n::tr(
						"Files stay next to the loaded ROM."));
				}
			}

			if(enabled && !valid_directory(value))
			{
				ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
					"%s", i18n::tr("Directory does not exist."));
			}
			ImGui::Separator();
			ImGui::PopID();
		};

		draw_row("ROMs", m_path_edit.roms,
			FileDialogAction::ChooseRomsDirectory, nullptr);

		ImGui::PushID("BIOS file");
		ImGui::TextUnformatted(i18n::tr("BIOS file"));
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 155.0f);
		path_input_text("##bios", m_path_edit.bios);
		ImGui::SameLine();
		if(ImGui::Button(i18n::tr("Browse...")))
		{
			open_file_dialog(FileDialogAction::ChooseBiosFile);
		}
		ImGui::SameLine();
		if(ImGui::Button(i18n::tr("Clear")))
		{
			m_path_edit.bios.clear();
		}
		{
			std::error_code error;
			const std::filesystem::path bios_path =
				std::filesystem::u8path(m_path_edit.bios);
			const bool valid = m_path_edit.bios.empty() ||
				(std::filesystem::is_regular_file(bios_path, error) &&
				 std::filesystem::file_size(bios_path, error) == 0x100);
			if(!valid)
			{
				ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
					"%s", i18n::tr(
						"BIOS file must be exactly 256 bytes."));
			}
		}
		ImGui::Separator();
		ImGui::PopID();

		draw_row("Battery saves", m_path_edit.battery,
			FileDialogAction::ChooseBatteryDirectory,
			&m_path_edit.battery_same_as_rom);
		draw_row("Save states", m_path_edit.savestates,
			FileDialogAction::ChooseSavestatesDirectory,
			&m_path_edit.savestates_same_as_rom);
		draw_row("Screenshots", m_path_edit.screenshots,
			FileDialogAction::ChooseScreenshotsDirectory, nullptr);
	}
	ImGui::EndChild();

	ImGui::BeginDisabled(!paths_edit_valid());
	if(ImGui::Button(i18n::tr("Apply")))
	{
		set_path_settings(m_path_edit);
		m_show_paths_window = false;
		notify(i18n::tr("Paths applied"));
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	if(ImGui::Button(i18n::tr("Cancel")))
	{
		m_path_edit = m_path_settings;
		m_show_paths_window = false;
	}
	ImGui::SameLine();
	if(ImGui::Button(i18n::tr("Reset defaults")))
	{
		m_path_edit = PathSettings {};
	}

	ImGui::End();
}

void EmulatorFrontend::save_screenshot_to_path(const std::string& filename)
{
	std::string target = filename;
	if(std::filesystem::path(target).extension().empty())
	{
		target += ".bmp";
	}

	const std::vector<u32>* pixels = nullptr;
	int width = GB_XRES;
	int height = GB_YRES;
	if(link_mode() && m_combined_frame.size() ==
		static_cast<size_t>(GB_XRES * 2) * GB_YRES)
	{
		pixels = &m_combined_frame;
		width = GB_XRES * 2;
	}
	else if(primary() != nullptr)
	{
		pixels = &primary()->display_pixels();
	}

	bool saved = false;
	if(pixels != nullptr)
	{
		saved = write_bmp(target, width, height, *pixels);
	}

	if(saved)
	{
		notify(i18n::format("Screenshot saved: %s", target.c_str()));
	}
	else
	{
		notify(i18n::tr("Screenshot failed"));
	}
}

void EmulatorFrontend::draw_custom_palette_window()
{
	const std::string title =
		std::string(i18n::tr("Custom palette")) + "###CustomPalette";
	if(!ImGui::BeginPopupModal(title.c_str(), nullptr,
		ImGuiWindowFlags_AlwaysAutoResize))
	{
		return;
	}

	for(int i = 0; i < 4; ++i)
	{
		const Rgb& color = m_custom_palette_edit[static_cast<size_t>(i)];
		float rgb[3] = {
			static_cast<float>(color.r) / 255.0f,
			static_cast<float>(color.g) / 255.0f,
			static_cast<float>(color.b) / 255.0f,
		};
		const std::string label = i18n::format("Color %zu", i);
		if(ImGui::ColorEdit3(label.c_str(), rgb,
			ImGuiColorEditFlags_NoAlpha))
		{
			const auto to_byte = [](float value)
			{
				return static_cast<u8>(std::lround(
					std::clamp(value, 0.0f, 1.0f) * 255.0f));
			};
			m_custom_palette_edit[static_cast<size_t>(i)] = {
				to_byte(rgb[0]), to_byte(rgb[1]), to_byte(rgb[2])
			};
		}
	}

	if(ImGui::Button(i18n::tr("Apply")))
	{
		m_display_settings.custom_palette = m_custom_palette_edit;
		m_display_settings.palette = PaletteMode::Custom;
		apply_display_settings();
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	if(ImGui::Button(i18n::tr("Cancel")))
	{
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	if(ImGui::Button(i18n::tr("Reset DMG")))
	{
		const DisplaySettings defaults;
		m_custom_palette_edit = defaults.custom_palette;
	}

	ImGui::EndPopup();
}
