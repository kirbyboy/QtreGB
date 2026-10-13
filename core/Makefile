include $(dir $(abspath $(firstword $(MAKEFILE_LIST))))../common.mk

SRCDIR := $(CURDIR)/src
INCDIR := $(CURDIR)/inc

SRCS := $(wildcard $(SRCDIR)/*.cpp)

OBJS_DBG := $(SRCS:$(SRCDIR)/%.cpp=$(OBJDIR)/core/%.o)
DEPS_DBG := $(SRCS:$(SRCDIR)/%.cpp=$(DEPDIR)/core/%.d)
OBJS_OPT := $(SRCS:$(SRCDIR)/%.cpp=$(OBJDIR)/core-opt/%.o)
DEPS_OPT := $(SRCS:$(SRCDIR)/%.cpp=$(DEPDIR)/core-opt/%.d)

CFLAGS_DBG := $(WARN) $(BUILD_DBG) -I$(INCDIR)
CFLAGS_OPT := $(WARN) $(BUILD_OPT) -I$(INCDIR)

all: $(BUILDDIR)/libqtrebgcore_dbg.a $(BUILDDIR)/libqtrebgcore_opt.a

$(OBJDIR)/core/%.o: $(SRCDIR)/%.cpp | $(OBJDIR)/core $(DEPDIR)/core
	$(CXX) -c $(CFLAGS_DBG) -MMD -MP -MF $(DEPDIR)/core/$*.d -o $@ $<

$(OBJDIR)/core-opt/%.o: $(SRCDIR)/%.cpp | $(OBJDIR)/core-opt $(DEPDIR)/core-opt
	$(CXX) -c $(CFLAGS_OPT) -MMD -MP -MF $(DEPDIR)/core-opt/$*.d -o $@ $<

$(BUILDDIR)/libqtrebgcore_dbg.a: $(OBJS_DBG) | $(BUILDDIR)
	ar rcs $@ $(OBJS_DBG)

$(BUILDDIR)/libqtrebgcore_opt.a: $(OBJS_OPT) | $(BUILDDIR)
	ar rcs $@ $(OBJS_OPT)

$(OBJDIR)/core $(OBJDIR)/core-opt $(DEPDIR)/core $(DEPDIR)/core-opt $(BUILDDIR):
	mkdir -p $@

clean:
	$(RM) $(OBJS_DBG) $(DEPS_DBG) $(OBJS_OPT) $(DEPS_OPT) $(BUILDDIR)/libqtrebgcore_dbg.a $(BUILDDIR)/libqtrebgcore_opt.a

-include $(DEPS_DBG)
-include $(DEPS_OPT)

.PHONY: all clean
