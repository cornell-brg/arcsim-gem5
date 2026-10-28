# Runs soc's tests (built in $ARCSIM_SOC_HOME/tests) on gem5's RP2350 board,
# with arcsim's test framework, as soc/tests does on rtl-sim. The build tree is
# ./build. Needs ARCSIM_TEST_HOME (source setup-arcsim.sh in an arcsim clone),
# ARCSIM_SOC_HOME (module load arcsim-soc) and a gem5 RISC-V build
# (scons build/RISCV/gem5.opt in ..).
#
#   make check            run the tests on gem5, one line per test
#   make check-verbose    the same, with each run's full output first
#   make check-rebuild    build soc's tests first (make soc-tests)
#   make soc-tests        build soc's tests (make -C $ARCSIM_SOC_HOME/tests
#                         build), once for each HWPE that has a test program
#   make clean
#
# GEM5 (default ../build/RISCV/gem5.opt) is the gem5 binary, e.g.
#   make check GEM5=../build/RISCV/gem5.debug
# SOC_TESTS (default $ARCSIM_SOC_HOME/tests/build) holds soc's test ELFs.
# JOBS (default 1) is how many tests run at once.

BUILD ?= build
GEM5 ?= $(CURDIR)/../build/RISCV/gem5.opt
SOC_TESTS ?= $(ARCSIM_SOC_HOME)/tests/build
TICK_LIMIT ?= 100000000000
JOBS ?= 1

.PHONY: configure check check-verbose check-rebuild soc-tests clean
configure:
	@test -x $(GEM5) || { echo "$(GEM5) not built: scons build/RISCV/gem5.opt in .."; exit 1; }
	@cmake -S . -B $(BUILD) -DGEM5_RISCV=$(abspath $(GEM5)) \
	       -DSOC_TESTS=$(abspath $(SOC_TESTS)) -DTICK_LIMIT=$(TICK_LIMIT) -Wno-dev >/dev/null

check: configure
	@CTEST_PARALLEL_LEVEL=$(JOBS) sh $(ARCSIM_TEST_HOME)/bin/arcsim-test-check $(BUILD)

check-verbose: configure
	@CTEST_PARALLEL_LEVEL=$(JOBS) sh $(ARCSIM_TEST_HOME)/bin/arcsim-test-check -v $(BUILD)

# soc builds an HWPE's test program only for the HWPE it is configured with, and
# keeps the ELFs of earlier ones; vvadd, its default, last.
soc-tests:
	@$(MAKE) --no-print-directory -C $(ARCSIM_SOC_HOME)/tests build \
	         HWPE=fast-with-scoring-wide-multicycle-cell-best-k
	@$(MAKE) --no-print-directory -C $(ARCSIM_SOC_HOME)/tests build HWPE=vvadd

check-rebuild: soc-tests
	@$(MAKE) --no-print-directory check

clean:
	rm -rf $(BUILD)
