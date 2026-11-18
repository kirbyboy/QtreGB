include $(dir $(abspath $(firstword $(MAKEFILE_LIST))))../common.mk

SRCDIR := $(CURDIR)
INCDIR_CORE := $(ROOT)/core/inc
INCDIR_GUI := $(ROOT)/gui/inc
APP_CONFIG_SRC := $(ROOT)/gui/src/app_config.cpp
APP_CONFIG_OBJ := $(OBJDIR)/tests/app_config.o
APP_CONFIG_DEP := $(DEPDIR)/tests/app_config.d
DISPLAY_SETTINGS_SRC := $(ROOT)/gui/src/display_settings.cpp
DISPLAY_SETTINGS_OBJ := $(OBJDIR)/tests/display_settings.o
DISPLAY_SETTINGS_DEP := $(DEPDIR)/tests/display_settings.d
DISPLAY_PROCESSOR_SRC := $(ROOT)/gui/src/display_processor.cpp
DISPLAY_PROCESSOR_OBJ := $(OBJDIR)/tests/display_processor.o
DISPLAY_PROCESSOR_DEP := $(DEPDIR)/tests/display_processor.d
INPUT_SETTINGS_SRC := $(ROOT)/gui/src/input_settings.cpp
INPUT_SETTINGS_OBJ := $(OBJDIR)/tests/input_settings.o
INPUT_SETTINGS_DEP := $(DEPDIR)/tests/input_settings.d
INPUT_DIRECTIONS_SRC := $(ROOT)/gui/src/input_directions.cpp
INPUT_DIRECTIONS_OBJ := $(OBJDIR)/tests/input_directions.o
INPUT_DIRECTIONS_DEP := $(DEPDIR)/tests/input_directions.d
INPUT_MANAGER_SRC := $(ROOT)/gui/src/input_manager.cpp
INPUT_MANAGER_OBJ := $(OBJDIR)/tests/input_manager.o
INPUT_MANAGER_DEP := $(DEPDIR)/tests/input_manager.d

SRCS := $(wildcard $(SRCDIR)/*.cpp)

OBJS := $(SRCS:$(SRCDIR)/%.cpp=$(OBJDIR)/tests/%.o) $(APP_CONFIG_OBJ) $(DISPLAY_SETTINGS_OBJ) $(DISPLAY_PROCESSOR_OBJ) $(INPUT_SETTINGS_OBJ) $(INPUT_DIRECTIONS_OBJ) $(INPUT_MANAGER_OBJ)
DEPS := $(SRCS:$(SRCDIR)/%.cpp=$(DEPDIR)/tests/%.d) $(APP_CONFIG_DEP) $(DISPLAY_SETTINGS_DEP) $(DISPLAY_PROCESSOR_DEP) $(INPUT_SETTINGS_DEP) $(INPUT_DIRECTIONS_DEP) $(INPUT_MANAGER_DEP)

CFLAGS := $(WARN) $(BUILD_OPT) -I$(INCDIR_CORE) -I$(INCDIR_GUI) $(THIRD_PARTY_INC) $(SDL_CFLAGS)

all: $(BUILDDIR)/tests_runner

$(BUILDDIR)/libqtrebgcore_opt.a: ; $(MAKE) --no-print-directory -C $(ROOT)/core

$(OBJDIR)/tests/%.o: $(SRCDIR)/%.cpp | $(OBJDIR)/tests $(DEPDIR)/tests
	$(CXX) -c $(CFLAGS) -MMD -MP -MF $(DEPDIR)/tests/$*.d -o $@ $<

$(APP_CONFIG_OBJ): $(APP_CONFIG_SRC) | $(OBJDIR)/tests $(DEPDIR)/tests
	$(CXX) -c $(CFLAGS) -MMD -MP -MF $(APP_CONFIG_DEP) -o $@ $<

$(DISPLAY_SETTINGS_OBJ): $(DISPLAY_SETTINGS_SRC) | $(OBJDIR)/tests $(DEPDIR)/tests
	$(CXX) -c $(CFLAGS) -MMD -MP -MF $(DISPLAY_SETTINGS_DEP) -o $@ $<

$(DISPLAY_PROCESSOR_OBJ): $(DISPLAY_PROCESSOR_SRC) | $(OBJDIR)/tests $(DEPDIR)/tests
	$(CXX) -c $(CFLAGS) -MMD -MP -MF $(DISPLAY_PROCESSOR_DEP) -o $@ $<

$(INPUT_SETTINGS_OBJ): $(INPUT_SETTINGS_SRC) | $(OBJDIR)/tests $(DEPDIR)/tests
	$(CXX) -c $(CFLAGS) -MMD -MP -MF $(INPUT_SETTINGS_DEP) -o $@ $<

$(INPUT_DIRECTIONS_OBJ): $(INPUT_DIRECTIONS_SRC) | $(OBJDIR)/tests $(DEPDIR)/tests
	$(CXX) -c $(CFLAGS) -MMD -MP -MF $(INPUT_DIRECTIONS_DEP) -o $@ $<

$(INPUT_MANAGER_OBJ): $(INPUT_MANAGER_SRC) | $(OBJDIR)/tests $(DEPDIR)/tests
	$(CXX) -c $(CFLAGS) -MMD -MP -MF $(INPUT_MANAGER_DEP) -o $@ $<

$(BUILDDIR)/tests_runner: $(OBJS) $(BUILDDIR)/libqtrebgcore_opt.a | $(BUILDDIR)
	$(CXX) $(LTO_FLAGS) -o $@ $(OBJS) $(BUILDDIR)/libqtrebgcore_opt.a $(SDL_LIBS)

$(OBJDIR)/tests $(DEPDIR)/tests $(BUILDDIR):
	mkdir -p $@

run: $(BUILDDIR)/tests_runner
	@echo "Running unit tests..."
	@$(BUILDDIR)/tests_runner

clean:
	$(RM) $(OBJS) $(DEPS) $(BUILDDIR)/tests_runner

-include $(DEPS)

.PHONY: all clean run
