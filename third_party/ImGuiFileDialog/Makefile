CXX ?= g++
THIS_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
ROOTDIR := $(abspath $(THIS_DIR)/../..)

CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic
CXXFLAGS += -pthread -Wno-unknown-pragmas \
	-I. -I$(ROOTDIR)/lib/imgui/src -I$(ROOTDIR)/lib/imgui/src/backends

TARGET := libImGuiFileDialog.a
OBJDIR := build
SRCS := ImGuiFileDialog.cpp
OBJS := $(patsubst %.cpp,$(OBJDIR)/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)

all: $(TARGET)

$(TARGET): $(OBJS)
	ar rcs $@ $^

$(OBJDIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

clean:
	$(RM) -r $(OBJDIR) $(TARGET)

-include $(DEPS)

.PHONY: all clean
