#ifndef QTREGB_UI_H
#define QTREGB_UI_H

#include "QtreGB.h"

static const int SCREEN_WIDTH = 1024;
static const int SCREEN_HEIGHT = 768;

void ui_init();
void ui_update(QtreGB *emulator);
void ui_handle_events(QtreGB *emulator);
void ui_init_imgui();
void ui_render(QtreGB* emu);


#endif
