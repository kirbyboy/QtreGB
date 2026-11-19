ROOT := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))

CXX := g++

WARN := -std=c++17 -fdiagnostics-color=always -Wall -Wextra -Wpedantic

# LTO permite inlinear entre unidades de compilación (tick_once -> CPU/PPU/
# APU/Serial). Hay que pasarlo también al enlace. Para desactivarlo:
# make clean && make LTO_FLAGS=
LTO_FLAGS ?= -flto

# Build de la app/GUI: optimizado y con log. Sigue teniendo -g para depurar.
# Para un build realmente sin optimizar: make clean && make GUI_DEBUG=1.
# Hay que hacer make clean al cambiar este flag: los objetos no lo codifican.
BUILD_DBG := -g -O2 -DENABLE_LOG $(LTO_FLAGS)
ifeq ($(strip $(GUI_DEBUG)),1)
BUILD_DBG := -g -O0 -DENABLE_LOG
endif
# Build de tests/runners: optimizado y sin log -> lo bastante rápido.
# LOG()/active_LOG() son inline y su definición cambia con ENABLE_LOG (ODR),
# por eso cada binario enlaza la variante coherente (GUI -> *_dbg, tests -> *_opt).
BUILD_OPT := -g -O2 $(LTO_FLAGS)

# Build identity stored in savestate containers for diagnostics. Outside a
# Git checkout this falls back to "unknown".
QTR_BUILD_VERSION := $(shell git -C $(ROOT) describe --tags --always --dirty 2>/dev/null || echo unknown)
BUILD_DBG += -DQTREGB_BUILD_VERSION=\"$(QTR_BUILD_VERSION)\"
BUILD_OPT += -DQTREGB_BUILD_VERSION=\"$(QTR_BUILD_VERSION)\"

# Traza CPUEVT de desarrollo: make clean && make QTR_IRQDBG=1.
# Los objetos no codifican el flag: hay que hacer make clean al cambiarlo.
ifeq ($(strip $(QTR_IRQDBG)),1)
BUILD_DBG += -DQTR_ENABLE_IRQDBG
BUILD_OPT += -DQTR_ENABLE_IRQDBG
endif

THIRD_PARTY_INC := -I$(ROOT)/third_party -I$(ROOT)/lib/imgui/src -I$(ROOT)/lib/imgui/src/backends -I$(ROOT)/third_party/ImGuiFileDialog -I$(ROOT)/lib/doctest -I/usr/include/GL

SDL_CFLAGS := $(shell sdl2-config --cflags)
SDL_LIBS := $(shell sdl2-config --libs)

GUI_LIBS := $(ROOT)/third_party/ImGuiFileDialog/libImGuiFileDialog.a $(ROOT)/lib/imgui/libimgui.a $(SDL_LIBS) -lGL -ldl -lpthread -lGLEW

OBJDIR := $(ROOT)/obj
BUILDDIR := $(ROOT)/build
DEPDIR := $(ROOT)/deps
