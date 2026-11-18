#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>

#include "emulator_frontend.h"
#include "tinyfiledialogs.h"

using namespace qtreGB;
using namespace std;

namespace
{

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

bool write_bmp(const std::string& path, int width, int height,
	const std::vector<u32>& pixels)
{
	if(width <= 0 || height <= 0 ||
	   pixels.size() != static_cast<size_t>(width) * static_cast<size_t>(height))
	{
		return false;
	}

	std::ofstream output(path, std::ios::binary | std::ios::trunc);
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
	const InputSettings& input_settings) :
m_backend(backend),
m_display_settings(settings),
m_audio_settings(audio_settings),
m_input_settings(input_settings),
m_show_layers_window(false)
{
	m_input_manager.set_settings(m_input_settings);
	m_instances.push_back(std::make_unique<EmulatorInstance>(m_backend, 1));
	m_instances.front()->set_audio_settings(m_audio_settings);
}

void EmulatorFrontend::set_audio_settings(const AudioSettings& settings)
{
	m_audio_settings = settings;
	m_audio_settings.volume = std::clamp(m_audio_settings.volume, 0, 100);
	for(const auto& instance : m_instances)
	{
		instance->set_audio_settings(m_audio_settings);
	}
}

void EmulatorFrontend::set_input_settings(const InputSettings& settings)
{
	m_input_settings = settings;
	m_input_settings.sanitize();
	m_input_manager.set_settings(m_input_settings);
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

void EmulatorFrontend::notify(const std::string& message)
{
	m_state_message = message;
	m_state_message_until = ImGui::GetTime() + 3.5;
}

EmulatorFrontend::~EmulatorFrontend()
{
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
		if (ImGui::BeginMenu("File"))
		{
			/*
			if (ImGui::MenuItem("Open ROM..."))
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
			if (ImGui::MenuItem("Open ROM..."))
			{
				std::string filename = open_rom_dialog();
				if(!filename.empty())
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

						primary()->load_cart(rom, filename);
					}
				}
			}
			if (ImGui::MenuItem("Load ROM player 2..."))
			{
				const std::string filename = open_rom_dialog();
				if(!filename.empty())
				{
					load_rom_player2(filename);
				}
			}
			if (ImGui::MenuItem("Load BIOS..."))
			{
				std::string filename = open_bios_dialog();
				if(!filename.empty())
				{
					ifstream myfile (filename, ios::binary);
					if (myfile.is_open())
					{
						myfile.seekg(0, ios::end);
						int file_size = myfile.tellg();

						vector<u8> bios;
						myfile.seekg(0, ios::beg);
						bios.resize(static_cast<std::size_t>(file_size));
						myfile.read(reinterpret_cast<char*>(bios.data()), static_cast<std::size_t>(file_size));
						myfile.close();

						if(primary()->load_boot_rom(bios))
						{
							m_bios_message.clear();
						}
						else
						{
							m_bios_message = "BIOS: invalid size (expected 256 bytes)";
						}
					}
				}
			}
			if (ImGui::MenuItem("Reset"))
			{
				primary()->reset();
				m_bios_message.clear();
			}

			if (ImGui::MenuItem("Save state"))
			{
				save_state();
			}
			if (ImGui::MenuItem("Load state"))
			{
				load_state();
			}

			const std::string bios_status = !primary()->has_boot_rom()
				? "BIOS: none"
				: (primary()->boot_rom_active() ? "BIOS: active" : "BIOS: loaded (reset to apply)");
			ImGui::MenuItem(bios_status.c_str(), nullptr, false, false);
			if(!m_bios_message.empty())
			{
				ImGui::MenuItem(m_bios_message.c_str(), nullptr, false, false);
			}
			ImGui::EndMenu();
		}

		if(ImGui::BeginMenu("Graphics"))
		{
			const ImGuiIO& io = ImGui::GetIO();
			ImGui::Text("FPS: %.2f - %.2f ms", io.Framerate,
				io.Framerate > 0.0f ? 1000.0f / io.Framerate : 0.0f);
			ImGui::Separator();

			if(ImGui::BeginMenu("Palette"))
			{
				if(ImGui::MenuItem("DMG", nullptr,
					m_display_settings.palette == PaletteMode::Dmg))
				{
					m_display_settings.palette = PaletteMode::Dmg;
					apply_display_settings();
				}
				if(ImGui::MenuItem("Pocket", nullptr,
					m_display_settings.palette == PaletteMode::Pocket))
				{
					m_display_settings.palette = PaletteMode::Pocket;
					apply_display_settings();
				}
				if(ImGui::MenuItem("Custom...", nullptr,
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
			if(ImGui::ColorEdit3("Background", background,
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

			if(ImGui::Checkbox("LCD grid", &m_display_settings.lcd_grid))
			{
				apply_display_settings();
			}
			if(m_display_settings.lcd_grid)
			{
				ImGui::SliderInt("Grid opacity",
					&m_display_settings.grid_opacity, 0, 100, "%d%%");
			}
			if(ImGui::Checkbox("Integer scaling",
				&m_display_settings.integer_scaling))
			{
				apply_display_settings();
			}

			const char* present_modes[] = {"Auto", "Monitor", "Game Boy"};
			int present_mode = static_cast<int>(m_display_settings.present_mode);
			if(ImGui::Combo("Present mode", &present_mode, present_modes,
				IM_ARRAYSIZE(present_modes)))
			{
				m_display_settings.present_mode =
					static_cast<PresentMode>(present_mode);
			}

			if(ImGui::MenuItem("Fullscreen"))
			{
				m_backend->toggle_fullscreen();
			}

			if(ImGui::MenuItem("Save screenshot..."))
			{
				save_screenshot();
			}

			if(ImGui::MenuItem("Reset graphics settings"))
			{
				m_display_settings = DisplaySettings {};
				apply_display_settings();
			}

			if(ImGui::BeginMenu("Ghosting"))
			{
				const char* preset = "Custom";
				int preset_index = 4;
				if(!m_display_settings.ghosting)
				{
					preset = "Off";
					preset_index = 0;
				}
				else if(m_display_settings.ghosting_light_ms == 27 &&
					m_display_settings.ghosting_dark_ms == 27)
				{
					preset = "Fast";
					preset_index = 1;
				}
				else if(m_display_settings.ghosting_light_ms == 32 &&
					m_display_settings.ghosting_dark_ms == 16)
				{
					preset = "Normal";
					preset_index = 2;
				}
				else if(m_display_settings.ghosting_light_ms == 47 &&
					m_display_settings.ghosting_dark_ms == 103)
				{
					preset = "Slow";
					preset_index = 3;
				}

				bool changed = false;
				if(ImGui::BeginCombo("Preset", preset))
				{
					const char* names[] = {"Off", "Fast", "Normal", "Slow"};
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

				changed |= ImGui::Checkbox("Enabled",
					&m_display_settings.ghosting);
				changed |= ImGui::SliderInt("Lighten ms/tone",
					&m_display_settings.ghosting_light_ms, 0,
					DisplaySettings::kMaxGhostingMs, "%d ms/tone");
				changed |= ImGui::SliderInt("Darken ms/tone",
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

		if(ImGui::BeginMenu("Audio"))
		{
			bool changed = ImGui::Checkbox("Enable audio",
				&m_audio_settings.enabled);
			changed |= ImGui::SliderInt("Volume", &m_audio_settings.volume,
				0, 100, "%d%%");
			if(changed)
			{
				set_audio_settings(m_audio_settings);
			}
			ImGui::EndMenu();
		}

		if(ImGui::BeginMenu("Settings"))
		{
			if(ImGui::MenuItem("Controllers..."))
			{
				m_show_controllers_window = true;
			}
			if(ImGui::MenuItem("Key bindings..."))
			{
				m_show_key_bindings_window = true;
			}
			ImGui::EndMenu();
		}

		if(ImGui::BeginMenu("Debug"))
		{
			if(ImGui::MenuItem("PPU"))
			{
				m_show_layers_window = true;
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

        // Full area available inside the main window
        ImVec2 avail = ImGui::GetContentRegionAvail();
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

	if(m_show_layers_window)
	{
		draw_layers_window();
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

	if(!m_state_message.empty() && ImGui::GetTime() < m_state_message_until)
	{
		ImDrawList* draw_list = ImGui::GetForegroundDrawList();
		const ImVec2 text_pos(12.0f, 34.0f);
		const ImVec2 text_size = ImGui::CalcTextSize(m_state_message.c_str());
		draw_list->AddRectFilled(
			ImVec2(text_pos.x - 6.0f, text_pos.y - 4.0f),
			ImVec2(text_pos.x + text_size.x + 6.0f, text_pos.y + text_size.y + 4.0f),
			IM_COL32(0, 0, 0, 190), 4.0f);
		draw_list->AddText(text_pos, IM_COL32(255, 255, 255, 255),
			m_state_message.c_str());
	}

	if(m_open_custom_palette)
	{
		ImGui::OpenPopup("Custom palette");
		m_open_custom_palette = false;
	}
	draw_custom_palette_window();

	ImGui::End(); // End of MainWindow
}

void EmulatorFrontend::tick()
{
	if(m_link && m_link->connected())
	{
		m_link->tick();
	}
	else if(primary())
	{
		primary()->tick();
	}
}

void EmulatorFrontend::drain_audio()
{
	for(const auto& instance : m_instances)
	{
		instance->drain_audio();
	}
}

void EmulatorFrontend::handle_input()
{
	m_input_manager.update();

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
		notify("Select a device for this player before binding controls");
		return;
	}

	m_capture_target = InputCaptureTarget::PlayerBinding;
	m_capture_player = player;
	m_capture_button = button;

	const InputManager::CaptureKind kind =
		(device.type == InputDeviceType::Keyboard)
		? InputManager::CaptureKind::Key
		: InputManager::CaptureKind::Controller;
	const std::string description =
		"Player " + std::to_string(player + 1) + ": " +
		InputSettings::gb_button_name(button);
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
	const std::string description =
		std::string(InputSettings::shortcut_name(action)) +
		(keyboard_slot ? " (keyboard)" : " (controller)");
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
				return InputSettings::shortcut_name(
					static_cast<ShortcutAction>(i));
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
			notify(std::string(InputSettings::gb_button_name(m_capture_button)) +
				" binding cleared");
		}
		else if(const char* conflict = shortcut_using(control))
		{
			notify(std::string("Control already used by the ") + conflict +
				" key binding");
		}
		else
		{
			const GbButton replaced = m_capture_button;
			std::string duplicate_name;
			for(size_t i = 0; i < player.bindings.size(); ++i)
			{
				if(i != index && player.bindings[i] == control)
				{
					duplicate_name = InputSettings::gb_button_name(
						static_cast<GbButton>(i));
					player.bindings[i] = {};
				}
			}
			player.bindings[index] = control;

			if(!duplicate_name.empty())
			{
				notify(duplicate_name + " was using that control; its binding "
					"was removed");
			}
			else
			{
				notify(std::string(InputSettings::gb_button_name(replaced)) +
					" = " + InputSettings::control_name(control));
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
			notify(std::string(InputSettings::shortcut_name(
				m_capture_action)) + " binding cleared");
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
						removed_detail += "Player " +
							std::to_string(player_index + 1) + " " +
							InputSettings::gb_button_name(
								static_cast<GbButton>(binding_index));
					}
				}
			}

			slot = control;
			if(!removed_detail.empty())
			{
				notify(std::string(InputSettings::shortcut_name(
					m_capture_action)) + " wins: removed " + removed_detail);
			}
			else if(duplicate_shortcut)
			{
				notify(std::string(InputSettings::shortcut_name(
					m_capture_action)) + " = " +
					InputSettings::control_name(control) +
					" (moved from another shortcut)");
			}
			else
			{
				notify(std::string(InputSettings::shortcut_name(
					m_capture_action)) + " = " +
					InputSettings::control_name(control));
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
		notify("Player " + std::to_string(player + 1) +
			" device changed; its bindings were cleared");
	}
	else
	{
		notify("Player " + std::to_string(player + 1) + " device changed");
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
			"Capturing %s", m_input_manager.capture_description().c_str());
		ImGui::TextDisabled(
			"Press a key or button. Esc or Backspace clears.");
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
	if(!ImGui::Begin("Controllers", &m_show_controllers_window))
	{
		if(m_capture_target == InputCaptureTarget::PlayerBinding)
		{
			m_input_manager.cancel_capture();
			m_capture_target = InputCaptureTarget::None;
		}
		ImGui::End();
		return;
	}

	if(ImGui::Button("Refresh devices"))
	{
		m_input_manager.refresh_devices();
		notify("Controller list refreshed");
	}
	ImGui::SameLine();
	if(ImGui::Button("Reset controllers"))
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
		notify("Controller settings reset to defaults");
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
			ImGui::Text("Player %d", static_cast<int>(player + 1));
			PlayerInputSettings& settings =
				m_input_settings.players[player];

			ImGui::SetNextItemWidth(240.0f);
			const std::string preview =
				m_input_manager.device_label(settings.device);
			if(ImGui::BeginCombo("Device", preview.c_str()))
			{
				if(ImGui::Selectable("None",
					settings.device.type == InputDeviceType::None))
				{
					set_player_device(player, InputDeviceId {});
				}
				InputDeviceId keyboard;
				keyboard.type = InputDeviceType::Keyboard;
				if(ImGui::Selectable("Keyboard",
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
				ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
					"Controller not connected. Bindings stay assigned and "
					"become active when it reconnects.");
			}

			const ImGuiTableFlags table_flags =
				ImGuiTableFlags_BordersInnerV |
				ImGuiTableFlags_RowBg |
				ImGuiTableFlags_SizingStretchProp;
			if(ImGui::BeginTable("##bindings", 4, table_flags))
			{
				ImGui::TableSetupColumn("Game Boy",
					ImGuiTableColumnFlags_WidthFixed, 90.0f);
				ImGui::TableSetupColumn("Control",
					ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Bind",
					ImGuiTableColumnFlags_WidthFixed, 54.0f);
				ImGui::TableSetupColumn("Clear",
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
					ImGui::TextUnformatted(InputSettings::gb_button_name(
						static_cast<GbButton>(i)));

					const HostControl& binding = settings.bindings[i];
					ImGui::TableSetColumnIndex(1);
					const std::string label =
						InputSettings::control_name(binding);
					if(ImGui::Selectable((label + "##control").c_str()))
					{
						start_player_capture(player,
							static_cast<GbButton>(i));
					}
					if(ImGui::IsItemHovered())
					{
						ImGui::SetTooltip("Click to bind a new control");
					}

					ImGui::TableSetColumnIndex(2);
					if(ImGui::Button("Bind", ImVec2(48.0f, 0.0f)))
					{
						start_player_capture(player,
							static_cast<GbButton>(i));
					}
					ImGui::TableSetColumnIndex(3);
					ImGui::BeginDisabled(binding.is_none());
					if(ImGui::Button("Clear", ImVec2(48.0f, 0.0f)))
					{
						settings.bindings[i] = {};
						set_input_settings(m_input_settings);
						notify(std::string(InputSettings::gb_button_name(
							static_cast<GbButton>(i))) +
							" binding cleared");
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

	if(ImGui::Checkbox("Allow opposite directions",
		&m_input_settings.allow_opposite_directions))
	{
		set_input_settings(m_input_settings);
	}
	if(ImGui::IsItemHovered())
	{
		ImGui::SetTooltip("When disabled, the latest pressed direction wins "
			"and the other one returns when it is released.");
	}

	draw_input_status_bar(
		"Click a control or Bind and press the new control.",
		"Analog sticks/triggers: 60% press, 40% release. "
		"Esc or Backspace clears.");

	ImGui::End();
}

void EmulatorFrontend::draw_key_bindings_window()
{
	ImGui::SetNextWindowSize(ImVec2(820.0f, 340.0f), ImGuiCond_FirstUseEver);
	if(!ImGui::Begin("Key bindings", &m_show_key_bindings_window))
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

	if(ImGui::Button("Reset key bindings"))
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
		notify("Key bindings reset to defaults");
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
			ImGui::TableSetupColumn("Action",
				ImGuiTableColumnFlags_WidthFixed, 110.0f);
			ImGui::TableSetupColumn("Keyboard",
				ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Bind",
				ImGuiTableColumnFlags_WidthFixed, 54.0f);
			ImGui::TableSetupColumn("Clear",
				ImGuiTableColumnFlags_WidthFixed, 54.0f);
			ImGui::TableSetupColumn("Controller",
				ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Bind",
				ImGuiTableColumnFlags_WidthFixed, 54.0f);
			ImGui::TableSetupColumn("Clear",
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
				ImGui::TextUnformatted(InputSettings::shortcut_name(action));

				ImGui::TableSetColumnIndex(1);
				const std::string keyboard_label =
					InputSettings::control_name(shortcut.keyboard);
				if(ImGui::Selectable((keyboard_label + "##keyboard").c_str()))
				{
					start_shortcut_capture(action, true);
				}
				ImGui::TableSetColumnIndex(2);
				if(ImGui::Button("Bind##keyboard", ImVec2(48.0f, 0.0f)))
				{
					start_shortcut_capture(action, true);
				}
				ImGui::TableSetColumnIndex(3);
				ImGui::BeginDisabled(shortcut.keyboard.is_none());
				if(ImGui::Button("Clear##keyboard", ImVec2(48.0f, 0.0f)))
				{
					shortcut.keyboard = {};
					set_input_settings(m_input_settings);
					notify(std::string(InputSettings::shortcut_name(action)) +
						" keyboard binding cleared");
				}
				ImGui::EndDisabled();

				ImGui::TableSetColumnIndex(4);
				const std::string controller_label =
					InputSettings::control_name(shortcut.controller);
				if(ImGui::Selectable(
					(controller_label + "##controller").c_str()))
				{
					start_shortcut_capture(action, false);
				}
				ImGui::TableSetColumnIndex(5);
				if(ImGui::Button("Bind##controller", ImVec2(48.0f, 0.0f)))
				{
					start_shortcut_capture(action, false);
				}
				ImGui::TableSetColumnIndex(6);
				ImGui::BeginDisabled(shortcut.controller.is_none());
				if(ImGui::Button("Clear##controller", ImVec2(48.0f, 0.0f)))
				{
					shortcut.controller = {};
					set_input_settings(m_input_settings);
					notify(std::string(InputSettings::shortcut_name(action)) +
						" controller binding cleared");
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
		"Click a slot or Bind. Keyboard slots accept keys; controller slots "
		"accept any controller.",
		"Esc or Backspace clears a capture.");

	ImGui::End();
}

void EmulatorFrontend::save_state()
{
	if(!primary())
	{
		return;
	}

	const std::string path = primary()->state_path();
	if(primary()->save_state())
	{
		m_state_message = "State saved: " + path;
	}
	else
	{
		m_state_message = path.empty()
			? "Save state failed: no ROM loaded"
			: "Save state failed";
	}
	m_state_message_until = ImGui::GetTime() + 2.5;
}

void EmulatorFrontend::load_state()
{
	if(!primary())
	{
		return;
	}

	const std::string path = primary()->state_path();
	if(primary()->load_state())
	{
		m_state_message = "State loaded: " + path;
	}
	else
	{
		m_state_message = path.empty()
			? "Load state failed: no ROM loaded"
			: "Load state failed (missing/corrupt state or different ROM)";
	}
	m_state_message_until = ImGui::GetTime() + 2.5;
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
	const u8 player_index = static_cast<u8>(m_instances.size() + 1);
	auto instance = std::make_unique<EmulatorInstance>(m_backend, player_index);
	EmulatorInstance* result = instance.get();
	instance->set_audio_settings(m_audio_settings);
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
	std::ifstream myfile(filename, std::ios::binary);
	if(!myfile.is_open())
	{
		m_state_message = "Player 2: could not open ROM";
		m_state_message_until = ImGui::GetTime() + 2.5;
		return;
	}

	myfile.seekg(0, std::ios::end);
	const std::streamsize file_size = myfile.tellg();
	myfile.seekg(0, std::ios::beg);

	std::vector<u8> rom(static_cast<size_t>(file_size));
	if(file_size > 0)
	{
		myfile.read(reinterpret_cast<char*>(rom.data()), file_size);
	}
	myfile.close();

	EmulatorInstance* second = instance(1);
	if(second == nullptr)
	{
		second = add_instance();
	}
	if(second == nullptr)
	{
		m_state_message = "Player 2: could not create instance";
		m_state_message_until = ImGui::GetTime() + 2.5;
		return;
	}

	second->load_cart(std::move(rom), filename);
	if(!second->is_ready())
	{
		m_state_message = "Player 2: invalid ROM";
		m_state_message_until = ImGui::GetTime() + 2.5;
		return;
	}

	// Fill the combined texture immediately so the first draw after loading
	// already has a valid 320x144 image.
	update_frame();
	m_state_message = "Player 2 ROM loaded: " + filename;
	m_state_message_until = ImGui::GetTime() + 2.5;
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

	for(const auto& instance : m_instances)
	{
		instance->update_frame(m_display_settings);
	}

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

void EmulatorFrontend::upload_combined_frame()
{
	if(m_instances.size() < 2)
	{
		return;
	}

	ensure_combined_texture();
	if(m_backend == nullptr || m_combined_texture == nullptr)
	{
		return;
	}

	const std::vector<u32>& left = m_instances[0]->display_pixels();
	const std::vector<u32>& right = m_instances[1]->display_pixels();
	if(left.size() != static_cast<size_t>(GB_XRES * GB_YRES) ||
	   right.size() != static_cast<size_t>(GB_XRES * GB_YRES))
	{
		return;
	}

	m_combined_frame.resize(static_cast<size_t>(GB_XRES * 2) * GB_YRES);
	for(int y = 0; y < GB_YRES; ++y)
	{
		const size_t left_row = static_cast<size_t>(y) * GB_XRES;
		const size_t right_row = static_cast<size_t>(y) * GB_XRES * 2;
		for(int x = 0; x < GB_XRES; ++x)
		{
			m_combined_frame[right_row + x] = left[left_row + x];
			m_combined_frame[right_row + GB_XRES + x] = right[left_row + x];
		}
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

void EmulatorFrontend::save_screenshot()
{
	const char* filters[] = {"*.bmp"};
	const char* filename = tinyfd_saveFileDialog("Save screenshot",
		"qtrebg.bmp", 1, filters, "BMP image");
	if(filename == nullptr || filename[0] == '\0')
	{
		return;
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
		saved = write_bmp(filename, width, height, *pixels);
	}

	m_state_message = saved
		? std::string("Screenshot saved: ") + filename
		: std::string("Screenshot failed");
	m_state_message_until = ImGui::GetTime() + 2.5;
}

std::string EmulatorFrontend::open_rom_dialog()
{
    const char *filters[] = {"*.gb"};
    const char *file = tinyfd_openFileDialog("Open ROM", "", 1, filters, "Game Boy ROMs", 0);
    return file ? file : "";
}

std::string EmulatorFrontend::open_bios_dialog()
{
    const char *filters[] = {"*.gb", "*.bin"};
    const char *file = tinyfd_openFileDialog("Open boot ROM", "", 2, filters, "Game Boy boot ROM", 0);
    return file ? file : "";
}

void EmulatorFrontend::draw_custom_palette_window()
{
	if(!ImGui::BeginPopupModal("Custom palette", nullptr,
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
		const std::string label = "Color " + std::to_string(i);
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

	if(ImGui::Button("Apply"))
	{
		m_display_settings.custom_palette = m_custom_palette_edit;
		m_display_settings.palette = PaletteMode::Custom;
		apply_display_settings();
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	if(ImGui::Button("Cancel"))
	{
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	if(ImGui::Button("Reset DMG"))
	{
		const DisplaySettings defaults;
		m_custom_palette_edit = defaults.custom_palette;
	}

	ImGui::EndPopup();
}

void EmulatorFrontend::draw_layers_window()
{
	// Open the window with a title
	if (!ImGui::Begin("Layers Window", &m_show_layers_window))
	{
		ImGui::End();
		return;
	}

	// Create tabs
	if (ImGui::BeginTabBar("LayersTabBar"))
	{
		// BG tab
		if (ImGui::BeginTabItem("BG"))
		{
			// Call a method that returns the BG layer texture
		//	SDL_Texture* bg_tex = get_emulator()->get_bg_texture();
		//	if (bg_tex)
		//	{
		//		ImGui::Image(bg_tex, ImVec2(160, 144)); // example size
		//	}
			ImGui::Text("hello1");
			ImGui::EndTabItem();
		}

		// Window tab
		if (ImGui::BeginTabItem("Window"))
		{
		//	SDL_Texture* window_tex = get_emulator()->get_window_texture();
		//	if (window_tex)
		//	{
		//		ImGui::Image(window_tex, ImVec2(160, 144));
		//	}
			ImGui::Text("hello2");
			ImGui::EndTabItem();
		}

		// Sprites tab
		if (ImGui::BeginTabItem("Sprites"))
		{
		//	SDL_Texture* sprites_tex = get_emulator()->get_sprites_texture();
		//	if (sprites_tex)
		//	{
		//		ImGui::Image(sprites_tex, ImVec2(160, 144));
		//	}
			ImGui::Text("hello3");
			ImGui::EndTabItem();
		}

		ImGui::EndTabBar();
	}

	ImGui::End();
}
