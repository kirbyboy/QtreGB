#include <fstream>

#include "emulator_frontend.h"
#include "tinyfiledialogs.h"

constexpr u32 GB_PALETTE[4] = {
    0xFFFFFFFF, // Blanco → color 0
    0xFFAAAAAA, // Gris claro → color 1
    0xFF555555, // Gris oscuro → color 2
    0xFF000000  // Negro → color 3
};

EmulatorFrontend::EmulatorFrontend(IGuiBackend *backend) :
m_backend(backend), m_show_layers_window(false)
{
	m_emulator = std::unique_ptr<EmulatorInstance>(new EmulatorInstance(m_backend));
}

void EmulatorFrontend::draw_main_window()
{

    // Flags para que la ventana principal no tenga bordes ni título
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

    ImGui::Begin("MainWindow", nullptr, window_flags);
    ImGui::PopStyleVar(2);

    // Menú superior
    if (ImGui::BeginMenuBar())
    {
		if (ImGui::BeginMenu("Archivo"))
		{
			if (ImGui::MenuItem("Abrir ROM..."))
			{
				std::string filename = "../roms/01-special.gb"; // tu función de diálogo de archivos
				if (!filename.empty())
				{
					m_emulator.reset(new EmulatorInstance(m_backend));

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

						m_emulator->load_cart(rom);
					}
				}
			}
			if (ImGui::MenuItem("Open ROM..."))
			{
				std::string filename = open_rom_dialog();
				if(!filename.empty())
				{
					m_emulator.reset(new EmulatorInstance(m_backend));

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

						m_emulator->load_cart(rom);
					}
				}
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

    // --- Dibujar el emulador directamente ---
    if (m_emulator)
    {
        void* textureId = m_emulator->get_texture_ID();
        std::pair<int,int> texSize = m_emulator->get_texture_size();
        int nativeW = texSize.first;
        int nativeH = texSize.second;

        // Área completa disponible dentro de la ventana principal
        ImVec2 avail = ImGui::GetContentRegionAvail();

        // Escalar para mantener aspecto
        float scale = std::min(avail.x / nativeW, avail.y / nativeH);
        ImVec2 finalSize(nativeW * scale, nativeH * scale);

        // Centrar la imagen
        ImVec2 cursor = ImGui::GetCursorPos();
        ImVec2 offset((avail.x - finalSize.x) * 0.5f, (avail.y - finalSize.y) * 0.5f);
        ImGui::SetCursorPos(ImVec2(cursor.x + offset.x, cursor.y + offset.y));

        // Dibujar el framebuffer del emulador
        ImGui::Image(textureId, finalSize);
    }

	if(m_show_layers_window)
	{
		draw_layers_window();
	}

	ImGui::End(); // Fin de MainWindow
}

void EmulatorFrontend::handle_input()
{
	handle_key(ImGuiKey_RightArrow, Right);
	handle_key(ImGuiKey_LeftArrow,  Left);
	handle_key(ImGuiKey_UpArrow,    Up);
	handle_key(ImGuiKey_DownArrow,  Down);

	handle_key(ImGuiKey_Z,          A);
	handle_key(ImGuiKey_X,          B);
	handle_key(ImGuiKey_Enter,      Start);
	handle_key(ImGuiKey_RightShift, Select);
}

EmulatorInstance* EmulatorFrontend::get_emulator()
{
	return m_emulator.get();
}

std::string EmulatorFrontend::open_rom_dialog()
{
    const char *filters[] = {"*.gb"};
    const char *file = tinyfd_openFileDialog("Open ROM", "", 1, filters, "Game Boy ROMs", 0);
    return file ? file : "";
}

void EmulatorFrontend::draw_layers_window()
{
	// Abre la ventana con título
	if (!ImGui::Begin("Layers Window", &m_show_layers_window))
	{
		ImGui::End();
		return;
	}

	// Crear pestañas
	if (ImGui::BeginTabBar("LayersTabBar"))
	{
		// Pestaña BG
		if (ImGui::BeginTabItem("BG"))
		{
			// Llamamos a un método que devuelve la textura de la capa BG
		//	SDL_Texture* bg_tex = get_emulator()->get_bg_texture();
		//	if (bg_tex)
		//	{
		//		ImGui::Image(bg_tex, ImVec2(160, 144)); // tamaño ejemplo
		//	}
			ImGui::Text("hola1");
			ImGui::EndTabItem();
		}

		// Pestaña Window
		if (ImGui::BeginTabItem("Window"))
		{
		//	SDL_Texture* window_tex = get_emulator()->get_window_texture();
		//	if (window_tex)
		//	{
		//		ImGui::Image(window_tex, ImVec2(160, 144));
		//	}
			ImGui::Text("hola2");
			ImGui::EndTabItem();
		}

		// Pestaña Sprites
		if (ImGui::BeginTabItem("Sprites"))
		{
		//	SDL_Texture* sprites_tex = get_emulator()->get_sprites_texture();
		//	if (sprites_tex)
		//	{
		//		ImGui::Image(sprites_tex, ImVec2(160, 144));
		//	}
			ImGui::Text("hola3");
			ImGui::EndTabItem();
		}

		ImGui::EndTabBar();
	}

	ImGui::End();
}

void EmulatorFrontend::handle_key(ImGuiKey imguiKey, GbButton gbButton)
{
	if (ImGui::IsKeyPressed(imguiKey))
	{
		m_emulator->press_button(static_cast<uint8_t>(gbButton));
	}
	else if(ImGui::IsKeyReleased(imguiKey))
	{
		m_emulator->release_button(static_cast<uint8_t>(gbButton));
	}
}
