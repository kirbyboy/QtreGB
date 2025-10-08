CPP := g++ -g -O0
ROOTDIR := $(shell pwd)
SRCDIR := $(ROOTDIR)/src
INCDIR := $(ROOTDIR)/inc
OBJDIR := $(ROOTDIR)/obj
BUILDDIR := $(ROOTDIR)/build
DEPDIR := $(ROOTDIR)/deps
SRCS := $(wildcard $(SRCDIR)/*.cpp)
OBJS := $(SRCS:$(SRCDIR)/%.cpp=$(OBJDIR)/%.o)
DEPS := $(SRCS:$(SRCDIR)/%.cpp=$(DEPDIR)/%.d)
TARGET := QtreGB

CFLAGS := -std=c++11 -fdiagnostics-color=always -Wall -Wextra -Wpedantic -DENABLE_LOG
CFLAGS += -Ilib/tinyfiledialogs/src -Ilib/imgui/src -Ilib/imgui/src/backends -I/usr/include/GL
#LFLAGS := -shared -Wl,-soname,$(TARGET)
LFLAGS := lib/imgui/libimgui.a lib/tinyfiledialogs/libtinyfiledialog.a `sdl2-config --cflags --libs` -lGL -ldl -lpthread -lGLEW

all: $(BUILDDIR)/$(TARGET)
	
$(OBJDIR)/%.o: $(SRCDIR)/%.cpp $(DEPDIR)/%.d | $(OBJDIR)	
	$(CPP) -c $(CFLAGS) -I$(INCDIR) -o $@ $(<:.d=.cpp)

$(DEPDIR)/%.d: $(SRCDIR)/%.cpp | $(DEPDIR)
	@set -e; rm -f $@; \
	$(CPP) $(CFLAGS) -I $(INCDIR) -MP -MM  $< > $@.$$$$; \
	sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' < $@.$$$$ > $@; \
	rm -f $@.$$$$

$(BUILDDIR)/$(TARGET): $(OBJS) | $(BUILDDIR) 
	$(CPP) -I $(INCDIR) -o $(BUILDDIR)/$(TARGET) $(OBJS) $(LFLAGS)

$(OBJDIR):
	mkdir $(OBJDIR)
	
$(BUILDDIR):
	mkdir $(BUILDDIR)
	
$(DEPDIR):
	mkdir $(DEPDIR)
	
clean:
	$(RM) $(OBJS) $(DEPS) $(BUILDDIR)/$(TARGET)

ifneq ($(MAKECMDGOALS),clean)	
-include $(DEPS)
endif

.PHONY: clean all
