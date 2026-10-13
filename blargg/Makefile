include $(dir $(abspath $(firstword $(MAKEFILE_LIST))))../common.mk

SRCDIR := $(CURDIR)
INCDIR_CORE := $(ROOT)/core/inc

SRCS := $(wildcard $(SRCDIR)/*.cpp)

OBJS := $(SRCS:$(SRCDIR)/%.cpp=$(OBJDIR)/blargg/%.o)
DEPS := $(SRCS:$(SRCDIR)/%.cpp=$(DEPDIR)/blargg/%.d)

CFLAGS := $(WARN) $(BUILD_OPT) -I$(INCDIR_CORE) $(THIRD_PARTY_INC)

BLARGGDIRS := $(ROOT)/roms/blargg/instr_timing \
	$(ROOT)/roms/blargg/cpu_instrs/individual \
	$(ROOT)/roms/blargg/mem_timing-2/rom_singles

all: $(BUILDDIR)/blargg_runner

$(BUILDDIR)/libqtrebgcore_opt.a: ; $(MAKE) --no-print-directory -C $(ROOT)/core

$(OBJDIR)/blargg/%.o: $(SRCDIR)/%.cpp | $(OBJDIR)/blargg $(DEPDIR)/blargg
	$(CXX) -c $(CFLAGS) -MMD -MP -MF $(DEPDIR)/blargg/$*.d -o $@ $<

$(BUILDDIR)/blargg_runner: $(OBJS) $(BUILDDIR)/libqtrebgcore_opt.a | $(BUILDDIR)
	$(CXX) -o $@ $(OBJS) $(BUILDDIR)/libqtrebgcore_opt.a

$(OBJDIR)/blargg $(DEPDIR)/blargg $(BUILDDIR):
	mkdir -p $@

run: $(BUILDDIR)/blargg_runner
	@find $(BLARGGDIRS) -name '*.gb' -print0 | xargs -0 -r $(BUILDDIR)/blargg_runner

clean:
	$(RM) $(OBJS) $(DEPS) $(BUILDDIR)/blargg_runner

-include $(DEPS)

.PHONY: all clean run
