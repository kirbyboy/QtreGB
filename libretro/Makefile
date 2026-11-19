# Standalone libretro core build. No SDL/ImGui dependency.
#
# Scope:
# - DMG only, .gb ROMs, single player.
# - XRGB8888 160x144 video and 48 kHz stereo audio.
# - RTC keeps the current core behavior.
# - retro_serialize/unserialize use QtreGB::save_state()/load_state().
# - retro_serialize_size: 327680 bytes (320 KiB), the doubled worst-case
#   core blob (measured worst: 150333 B; conservative bound: ~156 KiB).
# - Link cable, configurable palette and other options come later.

ROOT := $(abspath ..)
CXX ?= g++
WARN := -std=c++17 -Wall -Wextra -Wpedantic
OPT ?= -O2 -g
GIT_VERSION := $(shell git -C $(ROOT) describe --tags --always --dirty 2>/dev/null || echo unknown)

OBJDIR := $(ROOT)/obj
DEPDIR := $(ROOT)/deps
BUILDDIR := $(ROOT)/build

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
SHARED := -dynamiclib
TARGET := $(BUILDDIR)/qtrebg_libretro.dylib
else
SHARED := -shared
TARGET := $(BUILDDIR)/qtrebg_libretro.so
endif

CFLAGS := $(WARN) $(OPT) -fPIC \
	-DQTREGB_BUILD_VERSION=\"$(GIT_VERSION)\" \
	-I$(ROOT)/core/inc -I$(CURDIR)

CORE_SRCS := $(wildcard $(ROOT)/core/src/*.cpp)
CORE_OBJS := $(patsubst $(ROOT)/core/src/%.cpp,$(OBJDIR)/libretro/core/%.o,$(CORE_SRCS))
LIBRETRO_OBJ := $(OBJDIR)/libretro/libretro.o
DEPS := $(patsubst $(OBJDIR)/libretro/core/%.o,$(DEPDIR)/libretro/core/%.d,$(CORE_OBJS)) \
	$(DEPDIR)/libretro/libretro.d

all: $(TARGET)

$(TARGET): $(CORE_OBJS) $(LIBRETRO_OBJ) | $(BUILDDIR)
	$(CXX) $(SHARED) -o $@ $^

$(OBJDIR)/libretro/core/%.o: $(ROOT)/core/src/%.cpp | $(OBJDIR)/libretro/core $(DEPDIR)/libretro/core
	$(CXX) -c $(CFLAGS) -MMD -MP -MF $(DEPDIR)/libretro/core/$*.d -o $@ $<

$(LIBRETRO_OBJ): libretro.cpp | $(OBJDIR)/libretro $(DEPDIR)/libretro
	$(CXX) -c $(CFLAGS) -MMD -MP -MF $(DEPDIR)/libretro/libretro.d -o $@ $<

$(OBJDIR)/libretro $(OBJDIR)/libretro/core $(DEPDIR)/libretro $(DEPDIR)/libretro/core $(BUILDDIR):
	mkdir -p $@

RETROARCH_CORES ?= $(HOME)/.config/retroarch/cores

install: $(TARGET) qtrebg_libretro.info
	mkdir -p $(RETROARCH_CORES)
	cp $(TARGET) $(RETROARCH_CORES)/qtrebg_libretro.so
	cp qtrebg_libretro.info $(RETROARCH_CORES)/qtrebg_libretro.info

clean:
	rm -f $(CORE_OBJS) $(LIBRETRO_OBJ) $(DEPS) $(TARGET)

-include $(DEPS)

.PHONY: all clean install
