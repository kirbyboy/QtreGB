include $(dir $(abspath $(firstword $(MAKEFILE_LIST))))../common.mk

SRCDIR := $(CURDIR)
INCDIR_CORE := $(ROOT)/core/inc

SRCS := $(wildcard $(SRCDIR)/*.cpp)
OBJS := $(SRCS:$(SRCDIR)/%.cpp=$(OBJDIR)/regress/%.o)
DEPS := $(SRCS:$(SRCDIR)/%.cpp=$(DEPDIR)/regress/%.d)

CFLAGS := $(WARN) $(BUILD_OPT) -I$(INCDIR_CORE) $(THIRD_PARTY_INC)

MANIFEST := $(SRCDIR)/manifest.txt
GOLDEN := $(SRCDIR)/golden.txt
REGRESS_ARGS :=

ifneq ($(strip $(QTR_BIOS)),)
MANIFEST := $(SRCDIR)/manifest_bios.txt
GOLDEN := $(SRCDIR)/golden_bios.txt
REGRESS_ARGS += --bios "$(QTR_BIOS)"
endif

all: $(BUILDDIR)/regress_runner

$(BUILDDIR)/libqtrebgcore_opt.a: ; $(MAKE) --no-print-directory -C $(ROOT)/core

$(OBJDIR)/regress/%.o: $(SRCDIR)/%.cpp | $(OBJDIR)/regress $(DEPDIR)/regress
	$(CXX) -c $(CFLAGS) -MMD -MP -MF $(DEPDIR)/regress/$*.d -o $@ $<

$(BUILDDIR)/regress_runner: $(OBJS) $(BUILDDIR)/libqtrebgcore_opt.a | $(BUILDDIR)
	$(CXX) $(LTO_FLAGS) -o $@ $(OBJS) $(BUILDDIR)/libqtrebgcore_opt.a

$(OBJDIR)/regress $(DEPDIR)/regress $(BUILDDIR):
	mkdir -p $@

run: all
	@$(BUILDDIR)/regress_runner \
		--manifest "$(MANIFEST)" \
		--golden "$(GOLDEN)" \
		$(REGRESS_ARGS)

update: all
	@$(BUILDDIR)/regress_runner \
		--manifest "$(MANIFEST)" \
		--golden "$(GOLDEN)" \
		--update \
		$(REGRESS_ARGS)

clean:
	$(RM) $(OBJS) $(DEPS) $(BUILDDIR)/regress_runner

-include $(DEPS)

.PHONY: all run update clean
