include common.mk

MAKEFLAGS += -j$(shell nproc)

all:
	$(MAKE) --no-print-directory -C core
	$(MAKE) --no-print-directory -C gui

run_tests:
	$(MAKE) --no-print-directory -C tests run

run_mts:
	$(MAKE) --no-print-directory -C mts run

run_blargg:
	$(MAKE) --no-print-directory -C blargg run

clean:
	$(MAKE) --no-print-directory -C core clean
	$(MAKE) --no-print-directory -C gui clean
	$(MAKE) --no-print-directory -C tests clean
	$(MAKE) --no-print-directory -C mts clean
	$(MAKE) --no-print-directory -C blargg clean
	$(RM) -r $(OBJDIR) $(DEPDIR) $(BUILDDIR)

.PHONY: all clean run_tests run_mts run_blargg
