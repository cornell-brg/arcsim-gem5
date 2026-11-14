# Runs soc's tests ($ARCSIM_SOC_HOME/tests) on gem5's RP2350 board: each of
# soc's SoC configs is a simulator gem5-<config> (run.py --soc-config <config>,
# ../configs/example/rp2350/soc_configs.py), and soc's tests run each test on
# every one whose manifest has what the test needs, as on rtl-sim's. Needs
# ARCSIM_TEST_HOME (source setup-arcsim.sh in an arcsim clone), ARCSIM_SOC_HOME
# (module load arcsim-soc) and a gem5 RISC-V build (scons build/RISCV/gem5.opt
# in ..).
#
#   make sims             write build/sims/gem5-<config>/soc-config.cmake for
#                         each SoC config, after checking soc_configs.py
#                         against soc's rtl-sim/soc-configs
#   make check            make sims, then soc's make check on them, one line
#                         per test
#   make check-verbose    the same, with each run's full output first
#   make clean
#
# SOC_CONFIGS (default: all) runs on these SoC configs only, e.g.
#   make check SOC_CONFIGS="single-core dual-core"
# GEM5 (default ../build/RISCV/gem5.opt) is the gem5 binary, e.g.
#   make check GEM5=../build/RISCV/gem5.debug
# TICK_LIMIT is every run's --tick-limit. JOBS (default 1) is how many tests
# (and soc's builds) run at a time.

BUILD ?= build
GEM5 ?= $(CURDIR)/../build/RISCV/gem5.opt
SOC_CONFIGS ?=
TICK_LIMIT ?= 100000000000
JOBS ?= 1

SOC_CONFIGS_PY := $(CURDIR)/../configs/example/rp2350/soc_configs.py
SIMS := $(abspath $(BUILD))/sims
# soc's ids of the simulators: gem5-<config>
IDS := $(addprefix gem5-,$(or $(SOC_CONFIGS),$(shell python3 $(SOC_CONFIGS_PY) --names)))
SOC_CHECK = $(MAKE) --no-print-directory -C $(ARCSIM_SOC_HOME)/tests JOBS=$(JOBS) SOC_CONFIGS="$(IDS)"

.PHONY: sims check check-verbose clean
sims:
	@test -x $(GEM5) || { echo "$(GEM5) not built: scons build/RISCV/gem5.opt in .."; exit 1; }
	@python3 $(SOC_CONFIGS_PY) --check $(ARCSIM_SOC_HOME)/rtl-sim/soc-configs
	@rm -rf $(SIMS)
	@python3 $(SOC_CONFIGS_PY) --manifests $(SIMS) --gem5 $(abspath $(GEM5)) \
	         --tick-limit $(TICK_LIMIT) $(SOC_CONFIGS)

check: sims
	@$(SOC_CHECK) check

check-verbose: sims
	@$(SOC_CHECK) check-verbose

clean:
	rm -rf $(BUILD)
