CXX ?= g++
CXXFLAGS := -std=c++17 -Isrc -Isrc/backends -Wall -O2 $(shell sdl2-config --cflags)

IMGUI_DIR := src
IMGUI_BACKENDS := $(IMGUI_DIR)/backends
IMGUI_SRCS := $(IMGUI_DIR)/imgui.cpp \
              $(IMGUI_DIR)/imgui_draw.cpp \
              $(IMGUI_DIR)/imgui_widgets.cpp \
              $(IMGUI_DIR)/imgui_tables.cpp \
              $(IMGUI_BACKENDS)/imgui_impl_sdl2.cpp \
              $(IMGUI_BACKENDS)/imgui_impl_sdlrenderer2.cpp \
              $(IMGUI_BACKENDS)/imgui_impl_opengl3.cpp
IMGUI_OBJS := $(patsubst src/%.cpp,build/src/%.o,$(IMGUI_SRCS))

TARGET := libimgui.a

all: $(TARGET)

$(TARGET): $(IMGUI_OBJS)
	ar rcs $@ $^

build/src/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	$(RM) -r build
	$(RM) $(TARGET)

.PHONY: all clean
