#include "imgui_manager.h"

void ImGuiManager::init()
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;

	// Activar docking y otras configuraciones
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // navegación con teclado
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;      // habilitar docking

	ImGui::StyleColorsDark(); // tema por defecto
}

void ImGuiManager::beginFrame()
{
	ImGui::NewFrame();
}

void ImGuiManager::endFrame()
{
	ImGui::Render();
}

ImGuiIO &ImGuiManager::getIO()
{
	return ImGui::GetIO();
}
