#ifndef QTREGB_IMGUI_MANAGER_H
#define QTREGB_IMGUI_MANAGER_H

#include "imgui.h"

class ImGuiManager
{
	public:
		ImGuiManager() = default;
		~ImGuiManager() = default;

		void init();
		void beginFrame();
		void endFrame();

		ImGuiIO& getIO();
};

#endif
