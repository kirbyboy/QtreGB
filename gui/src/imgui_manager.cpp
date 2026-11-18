#include "imgui_manager.h"

void ImGuiManager::init()
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;

	// QtreGB keeps its settings in its own .ini, not in imgui.ini.
	io.IniFilename = nullptr;

	// Menus are mouse-only: the arrow keys belong to player 1. Do not enable
	// ImGui keyboard/gamepad navigation.
	io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
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
