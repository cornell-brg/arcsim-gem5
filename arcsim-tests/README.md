# arcsim-tests

soc's software tests (`$ARCSIM_SOC_HOME/tests`), run on gem5's RP2350 board
(`configs/example/rp2350/run.py`) with the
[arcsim test framework](https://github.com/cornell-brg/arcsim/tree/arcsim-stable/test),
the same way soc runs them on rtl-sim. Each of soc's SoC configs
(`soc/rtl-sim/soc-configs`) is a gem5 simulator, `gem5-<config>`:
`run.py --soc-config <config>` sets the board's cores, HWPE, FPU and SRAM4-7
from `configs/example/rp2350/soc_configs.py`. This directory only writes each
one's manifest; soc's tests build the test programs and run each on every
simulator that has what it needs, as they do on rtl-sim.

## Run

```sh
source setup-arcsim.sh                     # in an arcsim clone: sets ARCSIM_TEST_HOME
module load arcsim-soc                     # sets ARCSIM_SOC_HOME
scons build/RISCV/gem5.opt -j32            # in .., gem5
make check                                 # every test on every SoC config, one line per test
make check SOC_CONFIGS="single-core dual-core-vvadd-xcel"  # on these configs only
```

```
soc-tests
  gem5-single-core/test_memory             passed (20/20 test cases passed)
  gem5-dual-core-vvadd-xcel/test_multicore passed (13/13 test cases passed)
  ...
```

- `make sims`: only writes the manifests, `build/sims/gem5-<config>/soc-config.cmake`.
  soc's tests then run on them with
  `make -C $ARCSIM_SOC_HOME/tests check SOC_CONFIGS="gem5-<config> ..."`, also
  beside rtl-sim's (`SOC_CONFIGS="single-core gem5-single-core"`).
  soc's plain `make check` runs on rtl-sim only.
- `make check-verbose`: also prints each run's output.
- `make check JOBS=32`: runs 32 tests at once (default 1).
- `make check GEM5=../build/RISCV/gem5.debug`: runs on another gem5 binary.
- `make check TICK_LIMIT=<n>`: every run's `--tick-limit` (default
  100000000000); a run that reaches it fails.

`make sims` first checks `soc_configs.py` against soc's `rtl-sim/soc-configs/*.mk`
and fails if they differ (a config in one only, or another accelerator, number of
cores, FPU or SRAM4-7).

Each run's log is in `$ARCSIM_SOC_HOME/tests/build/test-logs/gem5-<config>/<test>.log`
and its gem5 output (`stats.txt`, `config.ini`, `soc-config.json`) in
`$ARCSIM_SOC_HOME/tests/build/m5out/gem5-<config>/<test>/`.

## Which tests run where

A manifest lists what its simulator has, as rtl-sim's do (`SOC_N_HARTS`,
`SOC_CONFIG_HWPE`, `SOC_FPU`, `SOC_XCEL_PRESENT`, `SOC_SRAM4_7_PRESENT`, ...), plus
how to run a test on it (`SOC_RUNNER gem5`, `SOC_SIM` and `SOC_GEM5_ARGS`). soc's
tests run a test program on every simulator whose manifest has what it needs
(`add_soc_test` in `soc/tests/CMakeLists.txt`), so on gem5 a test runs on the same
configs as on rtl-sim, with two differences:

- gem5's Hazard3 has F and D unless Zfinx, so the configs without an FPU are
  `FPU=f` here and also run `test_fp_f`.
- `*_randinit` (Verilator's random initial state) does not run on gem5.

Every test runs with `--model accurate --sram-model banked` (`SOC_GEM5_ARGS`).
To add a test, add it in soc (`add_soc_test`); to add a SoC config, add it to soc's
`rtl-sim/soc-configs` and to `soc_configs.py`.

## Known failures

These fail on gem5 because its board models the SoC differently from the RTL:

- `test_sio`: gem5's SIO model reads `GPIO_IN` as all ones instead of 0, and it
  answers its whole 64 KiB window, where the RTL faults above `+0x4000`.
- `test_xip_ctrl`: `--model accurate` puts the Pico 2's flash and XIP cache in
  front of flash, whose counters count; rtl-sim has no XIP cache, so they stay 0
  (`--xip-model soc-no-cache`, the RTL's code memory, passes).
- `test_interconnect` on the optical-flow configs (case 8): the RTL repeats the
  accelerator's 16 KiB register window across the 1 MiB HWPE slot, but gem5 maps
  only the four 4 KiB windows, so the slot's last word (`0x508ffffc`) faults.
