# arcsim-tests

soc's software tests (`$ARCSIM_SOC_HOME/tests`), run on gem5's RP2350 board
(`configs/example/rp2350/run.py`) with the
[arcsim test framework](https://github.com/cornell-brg/arcsim/tree/arcsim-stable/test),
the same way soc runs them on rtl-sim. The tests are built in soc; this
directory only lists them (`CMakeLists.txt`) and runs them on gem5.

## Run

```sh
source setup-arcsim.sh                     # in an arcsim clone: sets ARCSIM_TEST_HOME
module load arcsim-soc                     # sets ARCSIM_SOC_HOME
make -C $ARCSIM_SOC_HOME/tests build       # the test ELFs
scons build/RISCV/gem5.opt -j32            # in .., gem5
make check                                 # run them on gem5, one line per test
```

```
arcsim-gem5-tests
  hazard3_test_interconnect                passed (15/15 test cases passed)
  hazard3_test_memory                      passed (20/20 test cases passed)
  ...
```

- `make check-verbose`: also prints each run's output.
- `make check-rebuild`: builds soc's tests first.
- `make check JOBS=4`: runs four tests at once.
- `make check GEM5=../build/RISCV/gem5.debug`: runs on another gem5 binary.
- `make check SOC_TESTS=<dir>`: takes the ELFs from another directory.

Each run's log is in `build/test-logs/<test>.log`, and its gem5 output
(`stats.txt`, `config.ini`) is in `build/m5out/<test>/`.

## Tests

Each test is named after the core it runs on, followed by soc's test name: for
example, `hazard3_test_memory` is `test_memory.elf` on the Hazard3 proxy
(`--core hazard3-proxy --model accurate --sram-model banked`). The tests are
also labelled (`ctest --test-dir build -L hazard3`). They are listed by hand,
because the gem5 board doesn't model everything rtl-sim does yet:

- `test_memory`
- `test_interconnect`
- `test_dma`
- `test_vvadd`: on the vvadd HWPE (`--hwpe vvadd`).
- `test_fast_cell`: on the FAST cell HWPE
  (`--hwpe fast-with-scoring-wide-multicycle-cell-best-k`).
- `test_fp_f` and `test_fp_zfinx`: soc's `test_fp.c` built for F and for
  Zfinx; the Zfinx one runs with `--zfinx`.

The other tests run on the board's default HWPE (vvadd). soc builds an HWPE's
test program only for the HWPE its tests are configured with, so
`make soc-tests` (and `make check-rebuild`) builds them once for each.

soc's `*_randinit` variants only apply to Verilator, so they don't run here. The
`TEST_CASE` that soc's tests were configured with is built into the ELFs, so it
applies here too.

To add a test, add `hazard3_test(<soc test> [<run.py arg>...])` to
`CMakeLists.txt`, with `--hwpe <CONFIG_HWPE>` for an HWPE's test program (and
its HWPE in `make soc-tests`). If soc moves its tests, change `SOC_TESTS` there.
