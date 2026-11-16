ROOT := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))

CXX := g++

WARN := -std=c++17 -fdiagnostics-color=always -Wall -Wextra -Wpedantic

# Build de la app/GUI: sin optimizar y con log -> cómodo para depurar.
BUILD_DBG := -g -O0 -DENABLE_LOG
# Build de tests/runners: optimizado y sin log -> lo bastante rápido.
# LOG()/active_LOG() son inline y su definición cambia con ENABLE_LOG (ODR),
# por eso cada binario enlaza la variante coherente (GUI -> *_dbg, tests -> *_opt).
BUILD_OPT := -g -O2

THIRD_PARTY_INC := -I$(ROOT)/lib/imgui/src -I$(ROOT)/lib/imgui/src/backends -I$(ROOT)/lib/tinyfiledialogs/src -I$(ROOT)/lib/doctest -I/usr/include/GL

SDL_CFLAGS := $(shell sdl2-config --cflags)
SDL_LIBS := $(shell sdl2-config --libs)

GUI_LIBS := $(ROOT)/lib/imgui/libimgui.a $(ROOT)/lib/tinyfiledialogs/libtinyfiledialog.a $(SDL_LIBS) -lGL -ldl -lpthread -lGLEW

OBJDIR := $(ROOT)/obj
BUILDDIR := $(ROOT)/build
DEPDIR := $(ROOT)/deps
