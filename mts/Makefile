include $(dir $(abspath $(firstword $(MAKEFILE_LIST))))../common.mk

SRCDIR := $(CURDIR)
INCDIR_CORE := $(ROOT)/core/inc

SRCS := $(wildcard $(SRCDIR)/*.cpp)

OBJS := $(SRCS:$(SRCDIR)/%.cpp=$(OBJDIR)/mts/%.o)
DEPS := $(SRCS:$(SRCDIR)/%.cpp=$(DEPDIR)/mts/%.d)

CFLAGS := $(WARN) $(BUILD_OPT) -I$(INCDIR_CORE) $(THIRD_PARTY_INC)

MTSROOT := $(firstword $(wildcard $(ROOT)/vendor/mts/*))
MTSACC := $(MTSROOT)/acceptance
MTSROMS := $(wildcard \
	$(MTSACC)/timer/*.gb \
	$(MTSACC)/interrupts/*.gb \
	$(MTSACC)/intr_timing.gb \
	$(MTSACC)/ei_timing.gb \
	$(MTSACC)/ei_sequence.gb \
	$(MTSACC)/rapid_di_ei.gb \
	$(MTSACC)/reti_intr_timing.gb \
	$(MTSACC)/bits/mem_oam.gb \
	$(MTSACC)/bits/reg_f.gb \
	$(MTSACC)/instr/daa.gb \
	$(MTSACC)/div_timing.gb \
	$(MTSACC)/halt_ime0_ei.gb \
	$(MTSACC)/halt_ime1_timing.gb \
	$(MTSACC)/if_ie_registers.gb \
	$(MTSACC)/pop_timing.gb \
	$(MTSACC)/oam_dma/basic.gb \
	$(MTSACC)/oam_dma/reg_read.gb \
	$(MTSACC)/boot_regs-dmgABC.gb \
	$(MTSROOT)/emulator-only/mbc1/bits_bank1.gb \
	$(MTSROOT)/emulator-only/mbc1/bits_bank2.gb \
	$(MTSROOT)/emulator-only/mbc1/ram_64kb.gb \
	$(MTSROOT)/emulator-only/mbc1/rom_4Mb.gb)

all: $(BUILDDIR)/mts_runner

$(BUILDDIR)/libqtrebgcore_opt.a: ; $(MAKE) --no-print-directory -C $(ROOT)/core

$(OBJDIR)/mts/%.o: $(SRCDIR)/%.cpp | $(OBJDIR)/mts $(DEPDIR)/mts
	$(CXX) -c $(CFLAGS) -MMD -MP -MF $(DEPDIR)/mts/$*.d -o $@ $<

$(BUILDDIR)/mts_runner: $(OBJS) $(BUILDDIR)/libqtrebgcore_opt.a | $(BUILDDIR)
	$(CXX) -o $@ $(OBJS) $(BUILDDIR)/libqtrebgcore_opt.a

$(OBJDIR)/mts $(DEPDIR)/mts $(BUILDDIR):
	mkdir -p $@

run: $(BUILDDIR)/mts_runner
	@if [ -z "$(MTSROOT)" ]; then echo "No hay ROMs en vendor/mts/. Ver mts/README.md"; exit 1; fi
	@$(BUILDDIR)/mts_runner $(MTSROMS)

clean:
	$(RM) $(OBJS) $(DEPS) $(BUILDDIR)/mts_runner

-include $(DEPS)

.PHONY: all clean run
