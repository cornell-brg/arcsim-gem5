# FAST cell HWPE in gem5: CL and IL models, progress log

`fast-with-scoring-wide-multicycle-cell-best-k` (top `fastTopWithScoringWideMulticycleCellBestK`),
modelled from its RTL (arcsim-xcel `ebec185`, `fast-rtl-log.md`): a CL HWPE (`--cpu minor`, per
job within 1% of rtl-sim) and an IL model (`--cpu atomic`, `atomic_latency`, within 5%).

Cycle programs: `arcsim-tests/hwpe-cycles/` (`build.sh` builds them with soc's test build flags
into `arcsim-tests/build/hwpe-cycles`): `fast_cycles.c` (test_fast_cell's 10 jobs of cases 4-8,
mcycle from before the TRIGGER write to the STATUS read of 0, results checked), `fast_trace.c`
(two of them, cell 0 and the flat cell, for traces), `vvadd_cycles.c` (a 100-element vvadd job
three times).

## 2026-11-15: starting point

- gem5 `master`, `271c071 init squashed`; `scons build/RISCV/gem5.opt` builds.
- `run.py --soc-config single-core-zfinx-fpu-fast-cell-xcel`: `ValueError: HWPE
  fast-with-scoring-wide-multicycle-cell-best-k is not modelled in gem5`; every soc test on
  `gem5-single-core-zfinx-fpu-fast-cell-xcel` fails.
- soc tests on gem5: `test_vvadd` 19/19 on `gem5-single-core-vvadd-xcel` and
  `gem5-dual-core-vvadd-xcel`; also pass there: test_fp_f, test_interconnect, test_dma,
  test_memory (and test_multicore on dual-core); fail as known: test_sio, test_xip_ctrl,
  test_xip_cache, test_xip_timing.
- vvadd reference (`vvadd_cycles.elf`, single-core-vvadd-xcel): `--cpu minor` 376, 323, 323
  cycles; `--cpu atomic` 19, 19, 19.
- rtl-sim (`fast_cycles.bin`, RTL `ebec185`): 8180 8129 8129 8129 8129 8129 8129 8129 8124
  8129; min 8124, mean 8133, max 8180 (zero and random init alike). `test_fast_cell` and
  `_randinit` 8/8.
