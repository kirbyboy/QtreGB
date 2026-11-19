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

run_regress:
	$(MAKE) --no-print-directory -C regress run

update_regress:
	$(MAKE) --no-print-directory -C regress update

clean:
	$(MAKE) --no-print-directory -C core clean
	$(MAKE) --no-print-directory -C gui clean
	$(MAKE) --no-print-directory -C tests clean
	$(MAKE) --no-print-directory -C mts clean
	$(MAKE) --no-print-directory -C blargg clean
	$(MAKE) --no-print-directory -C regress clean
	$(MAKE) --no-print-directory -C third_party/ImGuiFileDialog clean
	$(RM) -r $(OBJDIR) $(DEPDIR) $(BUILDDIR)

.PHONY: all clean run_tests run_mts run_blargg run_regress update_regress
