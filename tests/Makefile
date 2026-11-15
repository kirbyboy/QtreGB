include $(dir $(abspath $(firstword $(MAKEFILE_LIST))))../common.mk

SRCDIR := $(CURDIR)
INCDIR_CORE := $(ROOT)/core/inc

SRCS := $(wildcard $(SRCDIR)/*.cpp)

OBJS := $(SRCS:$(SRCDIR)/%.cpp=$(OBJDIR)/tests/%.o)
DEPS := $(SRCS:$(SRCDIR)/%.cpp=$(DEPDIR)/tests/%.d)

CFLAGS := $(WARN) $(BUILD_OPT) -I$(INCDIR_CORE) $(THIRD_PARTY_INC)

all: $(BUILDDIR)/tests_runner

$(BUILDDIR)/libqtrebgcore_opt.a: ; $(MAKE) --no-print-directory -C $(ROOT)/core

$(OBJDIR)/tests/%.o: $(SRCDIR)/%.cpp | $(OBJDIR)/tests $(DEPDIR)/tests
	$(CXX) -c $(CFLAGS) -MMD -MP -MF $(DEPDIR)/tests/$*.d -o $@ $<

$(BUILDDIR)/tests_runner: $(OBJS) $(BUILDDIR)/libqtrebgcore_opt.a | $(BUILDDIR)
	$(CXX) -o $@ $(OBJS) $(BUILDDIR)/libqtrebgcore_opt.a

$(OBJDIR)/tests $(DEPDIR)/tests $(BUILDDIR):
	mkdir -p $@

run: $(BUILDDIR)/tests_runner
	@echo "Running unit tests..."
	@$(BUILDDIR)/tests_runner

clean:
	$(RM) $(OBJS) $(DEPS) $(BUILDDIR)/tests_runner

-include $(DEPS)

.PHONY: all clean run
