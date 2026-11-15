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

## Phase 1: plan (proposed, awaiting the designer's approval)

RTL cycle budget, from an rtl-sim VCD of `fast_trace` (cycle T = the TRIGGER write's periph
handshake):
- T+4 top STARTING, T+5 WORKING (engine LOAD, source req_start), T+7 first TCDM grant
  (hci_core_source: 2 cycles from req_start to its first request).
- Rows y = 3..60, R band rows (9; 8 at y = 3 and 60): first chunk R+1 load cycles (no beat held)
  + 8 steps, chunks 1..7 R loads (the first beat was read on the last load cycle of the chunk
  before and held) + 8 steps, then 3 flush steps: 8R+68 = 140 (132 at the edges). 58 rows:
  8104 cycles from the first grant to the last flush step. The next row's source restarts
  2 cycles after its last beat (done pulse, src_y register) and is ready 2 later, hidden by
  the 11 steps.
- End: no corners: engine DONE +1, top FINISHED +2. Corners: OUT +1 (sink req_start), write
  grant +3, engine DONE +4, top sees sink done +6, FINISHED +7. Then the slave shows STATUS 0;
  the core polls STATUS every 5 cycles.

Plan: `HWPEFastCell` (src/dev/hwpe/hwpe_fast_cell.{hh,cc}, HWPE.py, SConscript, board HWPES):
- One component per RTL block: HWPEBase = hwpe_ctrl_slave (COUNT = extFlags(), cleared on
  soft clear); the top's job FSM and per-row source sequencer; the streamer's Source `pix`
  (channel 0, depth 2: the source's held beat + one read) and Sink `corner` (channel 1,
  depth 1), mux_channels 2; `FastCellEngine` (band 9 x (7+chunk), 3x3 score window, best 3,
  E_IDLE/LOAD/STEP/OUT/DONE) evaluated once per clock edge as the RTL's always_ff; `fastScore`
  as the RTL's arithmetic (arc minima, max, M > 10, M - 1).
- Same TCDM accesses: per row, base IMAGE_PTR + max(y-4,0)*320, d0 R rows x 320, d1 4 x 0,
  d2 32; one masked write of n words at CORNERS_PTR when n > 0.
- Timing parameters (RTL behaviour): chunk 8; pix_depth 2; source_start_latency 2
  (req_start to first request); row_restart_latency 2 (last beat to the next row's
  req_start); sink_start_latency 2 (OUT to the write request); sink_done_latency 3 (write
  grant to the top seeing it done); start_latency (TRIGGER to WORKING) and done_latency
  (FINISHED to STATUS 0) calibrated end to end.
- Shared change: `Source::setReady()`, hci_core_source's req = address valid & stream.ready:
  a gated source reads only in cycles where its consumer is ready. Ungated by default, so vvadd
  is unchanged (tests and cycles checked before and after).
- IL: `atomic_latency` = the RTL's job time less the atomic polling overhead (~8,115); the job
  time is data-independent but for +5 cycles with corners, so one number (within 0.1%).

### Designer: approved. Phases 2 and 3 start.

## Phase 2: CL model (done)

Built (`src/dev/hwpe/hwpe_fast_cell.{hh,cc}`, `HWPEFastCell` in `HWPE.py`, `SConscript`, board
`HWPES`):
- `fastScore()`: fastScore.sv's arithmetic (arc minima as a 2/4/8/9 tree, max, M > threshold,
  M - 1). `FastCellEngine`: fastCellEngine.sv's registers (band 9 x (7+chunk), 3x3 scores,
  best 3, nbest, count, y, c, lr) and FSM; `clock()` is its always_ff at one edge.
  `HWPEFastCell`: the top's FSM (WORKING -> FINISHED on engine done and, if started, sink
  done), the per-row source sequencing (src_y, src_busy, req_start), the sink start;
  evaluated once per clock edge by `cycleEvent` (priority before the streamer's tick, so the
  source's read in a cycle sees the engine's ready of that cycle). COUNT = extFlags(), latched
  at FINISHED, 0 after a soft clear.
- Streams: Source `pix` (channel 0, depth 2, gated by the engine's ready), Sink `corner`
  (channel 1, depth 1, one masked 32-byte write); mux_channels 2.
- Parameters: chunk 8, threshold 10, pix_depth 2, source_start_latency 2, row_restart_latency 2,
  sink_start_latency 2, sink_done_latency 3 (all read off the RTL VCD); start_latency 5 (TRIGGER
  handshake to WORKING); done_latency 1 (hwpe_ctrl_regfile clears STATUS at the edge after
  true_done, so a read in the next cycle sees 0).
- Shared code: `Source::setReady()` (hci_core_source's req = address valid & stream.ready);
  ungated by default. vvadd before/after: test_vvadd 19/19 on both vvadd configs; vvadd job
  minor 376/323/323 -> 376/323/323, atomic 19/19/19 -> 19/19/19.

Calibration: none needed past the RTL's numbers. Trace of `fast_trace` (cell 0, flat cell) on
rtl-sim (VCD) and gem5 (`--debug-flags=HWPE,HWPETrace,HCITcdm`): both jobs make the same TCDM
accesses (4161 and 4160: same addresses, same order) granted in the same cycle relative to the
TRIGGER (first read T+7, corner write T+8113), and reach FINISHED at the same cycle (T+8117,
T+8112). The rest of the per-job difference is on the core's side (mcycle read to TRIGGER,
STATUS polling): 12 cycles on rtl-sim, 14-15 on gem5.

## Phase 3: IL model and results (done)

- IL: `atomic_latency` 8117, the RTL's TRIGGER -> FINISHED with corners. The atomic CPU's polling
  loop rounds to its next STATUS read: 8115 reads 8123, 8117 and 8119 read 8127, 8121 reads 8131;
  8129 is not reachable. One number, no formula.
- Tests: soc tests on gem5 (`gem5-single-core-zfinx-fpu-fast-cell-xcel`): test_fast_cell 8/8,
  test_dma, test_fpu, test_fp_zfinx, test_interconnect, test_memory pass; test_sio,
  test_xip_ctrl, test_xip_cache, test_xip_timing fail as on every gem5 config. IL:
  test_fast_cell 8/8 with `--cpu atomic` (run.py). vvadd configs as at the starting point.
- Cycles per job (`fast_cycles.elf`, test_fast_cell's 10 jobs; job 1 = cell 0 cold, 2-8 cells
  with corners, 9 the flat cell, 10 the core reading the TCDM):

  | job | rtl-sim | CL (minor) | CL err | IL (atomic) | IL err |
  |---|---|---|---|---|---|
  | 1 | 8180 | 8178 | -0.02% | 8127 | -0.65% |
  | 2-8, 10 | 8129 | 8131 | +0.02% | 8127 | -0.02% |
  | 9 (flat) | 8124 | 8127 | +0.04% | 8127 | +0.04% |
  | min / mean / max | 8124 / 8133 / 8180 | 8127 / 8135 / 8178 | mean abs 0.026% | 8127 / 8127 / 8127 | mean abs 0.09% |

  Requirements: CL within 1% per job (max 0.04%), IL within 5% (max 0.65%, the cold first job:
  core-side).
- arcsim-tests/README.md: the fast-cell configs are no longer a known failure.
- Open: the remaining 2-3 cycles per job are in gem5's core/peripheral-bus timing (TRIGGER write
  and STATUS polling), not the HWPE; not fixed (out of scope).
