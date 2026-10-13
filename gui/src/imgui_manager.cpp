#include "imgui_manager.h"

void ImGuiManager::init()
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;

	// Enable docking and other settings
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // keyboard navigation
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;      // enable docking

	ImGui::StyleColorsDark(); // default theme
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
