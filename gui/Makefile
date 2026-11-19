include $(dir $(abspath $(firstword $(MAKEFILE_LIST))))../common.mk

SRCDIR := $(CURDIR)/src
INCDIR := $(CURDIR)/inc

SRCS := $(wildcard $(SRCDIR)/*.cpp)

OBJS := $(SRCS:$(SRCDIR)/%.cpp=$(OBJDIR)/gui/%.o)
DEPS := $(SRCS:$(SRCDIR)/%.cpp=$(DEPDIR)/gui/%.d)

CFLAGS := $(WARN) $(BUILD_DBG) -I$(INCDIR) -I$(ROOT)/core/inc $(THIRD_PARTY_INC) $(SDL_CFLAGS)
IMGUIFILE_DIALOG_LIB := $(ROOT)/third_party/ImGuiFileDialog/libImGuiFileDialog.a

all: $(BUILDDIR)/QtreGB-GUI

$(BUILDDIR)/libqtrebgcore_dbg.a: ; $(MAKE) --no-print-directory -C $(ROOT)/core

$(IMGUIFILE_DIALOG_LIB): ; $(MAKE) --no-print-directory -C $(ROOT)/third_party/ImGuiFileDialog

$(OBJDIR)/gui/%.o: $(SRCDIR)/%.cpp | $(OBJDIR)/gui $(DEPDIR)/gui
	$(CXX) -c $(CFLAGS) -MMD -MP -MF $(DEPDIR)/gui/$*.d -o $@ $<

$(BUILDDIR)/QtreGB-GUI: $(OBJS) $(BUILDDIR)/libqtrebgcore_dbg.a $(IMGUIFILE_DIALOG_LIB) | $(BUILDDIR)
	$(CXX) $(LTO_FLAGS) -o $@ $(OBJS) $(BUILDDIR)/libqtrebgcore_dbg.a $(GUI_LIBS)

$(OBJDIR)/gui $(DEPDIR)/gui $(BUILDDIR):
	mkdir -p $@

clean:
	$(RM) $(OBJS) $(DEPS) $(BUILDDIR)/QtreGB-GUI

-include $(DEPS)

.PHONY: all clean
