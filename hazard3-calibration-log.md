# Hazard3 RTL -> gem5 CL calibration log

Progress log of the `hazard3-rtl-cl-calibrate` skill: gem5's cycle-level Hazard3 model
(RiscvMinorCPU on the RP2350 board, `--model accurate --xip-model soc --cpu minor`) calibrated
against arcsim-soc's Hazard3 RTL (rtl-sim), item by item with the designer.

Error per body = (gem5 - rtl) / rtl of the mean cycles per measured sample, in %, as the skill's
`3_analyze.sh` prints it. Results: `scripts/work/<SOC_CONFIG>/results/<GEM5_TAG>.csv` next to the
skill.

## Starting point (it00), 2026-11-14

Revisions: gem5 `da1cb2c`, soc `c173f42`, bench `327008c`.

### Tests

rtl-sim (`make -C $ARCSIM_SOC_HOME/tests check`): every test passes in both configs, each also as
`_randinit`: test_sio 7/7, test_xip_ctrl 7/7, test_xip_cache 13/13, test_xip_backdoor 3/3,
test_xip_timing 8/8, test_interconnect 15/15, test_memory 20/20, test_dma 28/28; on
single-core-zfinx-fpu also test_fp_zfinx 7/7 and test_fpu 8/8.

gem5 (`make -C arcsim-tests check-verbose`), the baseline:

`single-core` (gem5's board has F and D here: "gem5 differs: F and D (rtl-sim: no FPU)"):

| test | result | failing cases / checks |
|---|---|---|
| test_sio | FAILED, 5/7 cases | case_4_reads_of_other_offsets 7/8 checks (`test_sio.c:68 GPIO_IN == 0u`, 0xffffffff); case_6_slot 3/5 checks (`test_sio.c:95` load at SIO_BASE+0x4000 does not fault) |
| test_xip_ctrl | FAILED | case_1_ctrl_and_stat_reset 0/2 (`CTRL == CTRL_RESET`, 0 != 3203); case_2_cache_off_counts_no_hits 1/2 (`hit == 0u`, 228); case_3_cold_then_warm passed 4/4; case_4_uncached_counts_access_only: unexpected trap mcause=5 (uncached alias 0x14000000), run stops |
| test_xip_cache | FAILED | case_1_write_then_read passed 260/260; case_2_clean_then_read_uncached: unexpected trap mcause=5, run stops |
| test_xip_timing | FAILED | case_1_hit: 7 cycles (expected 2); case_2_miss: 117 cycles (expected 112); case_3_uncached: unexpected trap mcause=5, run stops |
| test_interconnect | passed 15/15 | |
| test_memory | passed 20/20 | |
| test_dma | passed 28/28 | |
| test_fp_f | passed 8/8 | (gem5 only: built for gem5's F) |

`single-core-zfinx-fpu`: all 9 tests refused (`ValueError: Zfinx is not modelled in gem5`):
test_sio, test_xip_ctrl, test_xip_cache, test_xip_timing, test_interconnect, test_memory,
test_dma, test_fp_zfinx, test_fpu.

### Microbenchmarks (it00)

`single-core`: **mean |err| 25.11 %** over 491 bodies (max 181.03 %, 199 within 5 %).

| family | bodies | mean \|err\| % | max % | within 5 % |
|---|---:|---:|---:|---:|
| empty | 1 | 134.00 | 134.00 | 0 |
| nop16 | 6 | 41.15 | 133.33 | 1 |
| nop32 | 6 | 45.69 | 133.33 | 0 |
| alu | 33 | 17.28 | 53.22 | 13 |
| branch | 7 | 46.77 | 99.76 | 0 |
| fetch | 34 | 41.04 | 148.18 | 3 |
| operand | 6 | 5.98 | 7.02 | 2 |
| mem | 35 | 5.93 | 21.48 | 19 |
| mix | 60 | 1.99 | 7.86 | 57 |
| result_delay | 19 | 37.41 | 181.03 | 0 |
| irwin_alu | 56 | 35.11 | 52.30 | 2 |
| irwin_control | 56 | 69.48 | 154.39 | 3 |
| cache | 19 | 29.89 | 49.50 | 2 |
| xip_miss | 10 | 17.03 | 66.68 | 7 |
| kern | 6 | 11.61 | 46.43 | 3 |
| dma | 7 | 16.74 | 39.14 | 2 |
| rand_asm | 30 | 26.97 | 58.38 | 0 |
| rand_fact | 100 | 7.16 | 54.95 | 85 |

Worst bodies: result_delay/mul_chain +181 % (58 -> 163), irwin_control/cf_call_indirect_9 +154 %,
cf_jump_indirect_8 +151 %, fetch/br_fwd_1 +148 % (411 -> 1020), irwin_control
cf_jump_indirect_100/16/9 and cf_call_indirect_100 +138..141 %, empty/empty and nop*_0 +133 %
(6 -> 14 cycles of measurement overhead).

instret differs at the starting point (cycles not comparable): alu/dmb, dma/words_1,
dma/words_16, dma/words_256, dma/halves_256, dma/bytes_256, dma/workspace_256.

`single-core-zfinx-fpu`: every gem5 run (22 families) refused (Zfinx not modelled); rtl-sim gave
558 bodies. No error yet.

### Open issues

- test_xip_timing case_1_hit measures a cached load at 7 cycles on gem5 against 2 in the RTL:
  partly core-side load timing (to look at in calibration), partly the memory system.

## Item 1 (it01): Zfinx support, 2026-11-14

Ranked list proposed (after it00): 1. Zfinx support (functional); 2. execute latencies (ALU,
mul, div, FPU) as the RTL's; 3. branch prediction (single-entry BTB) and taken-branch penalty;
4. measurement / CSR-read overhead (+6..8 cycles a body); 5. load-use and load/store hazards.
Selected: **1**.

What changed, and the RTL it models (`hazard3_decode.v:534-551`, `hazard3_csr.v`,
`rtl/fpu/hazard3_fpu.sv`, EXTENSION_ZFINX=1 in `config_single_core_zfinx_fpu.vh`):

- `RiscvISA.enable_Zfinx` (default False; `src/arch/riscv/RiscvISA.py`, `isa.hh/.cc`), passed to
  the decoder as ExtMachInst bit 59 (`enable_zfinx`, bitfield `ENABLE_ZFINX`; `types.hh`,
  `decoder.hh/.cc`, `isa/bitfields.isa`).
- Decoder (`isa/decoder.isa`): OP-FP decodes on `ENABLE_ZFINX`; with it, only Hazard3's FP32
  subset, on the integer registers (format `ZfinxOp`, `isa/formats/fp.isa`, the softfloat calls
  of gem5's F, flags accrued to fflags, no mstatus.FS): fadd/fsub/fmul/fdiv/fsqrt.s (rs2=0),
  fsgnj/fsgnjn/fsgnjx.s, fmin/fmax.s, feq/flt/fle.s, fclass.s (rs2=0), fcvt.w/wu.s and
  fcvt.s.w/wu. Reserved rm and DYN with frm>4 are illegal (RM_REQUIRED), as
  `d_fp_rm_rsvd`/`x_fpu_rm_illegal`. Every other OP-FP encoding is illegal. Instruction names
  carry `_zfinx` (`fadd_s_zfinx`, ...).
- misa.F and misa.D read 0 and mstatus.FS is hardwired Off (writes to mstatus/misa cannot set
  them; `isa.cc`), as `MISA_VAL` and mstatus in `hazard3_csr.v`. So FMA, FP loads/stores/moves
  (incl. c.flw/c.fsw) and D/H trap illegal, through `updateFPUStatus`.
- fflags, frm and fcsr stay accessible with misa.F clear (`isa/formats/standard.isa`), as
  `hazard3_csr.v:1146-1157` under EXTENSION_ZFINX.
- Board (`configs/example/rp2350/board.py`): the Zfinx refusal is gone; `zfinx` (hazard3-proxy
  only) sets `enable_Zfinx` on every Hazard3 core (`_hazard3_cpu`, via `board._zfinx`).
  `run.py` prints `FPU=zfinx`; docstrings of `run.py`, `soc_configs.py` updated. Without zfinx
  the core keeps gem5's F and D (single-core unchanged; test_fp_f still runs there).
- Timing is not part of this item: the Zfinx instructions keep gem5's F op classes (FloatAdd,
  FloatMult, FloatDiv, FloatSqrt, FloatCmp, FloatCvt, FloatMisc) on Minor's default FU pool.

Tests (gem5):

- `single-core`: identical to the baseline, test by test and check by check.
- `single-core-zfinx-fpu`: all 9 tests run; the same four as single-core fail with the same
  failing cases/checks as single-core's baseline (test_sio 5/7, test_xip_ctrl, test_xip_cache,
  test_xip_timing); test_interconnect 15/15, test_memory 20/20, test_dma 28/28,
  test_fp_zfinx 7/7, test_fpu 8/8 pass.

Microbenchmarks:

| config | it00 | it01 |
|---|---|---|
| single-core | 25.11 % (491) | 25.11 % (491), every body unchanged |
| single-core-zfinx-fpu | refused | **28.64 %** (558 bodies, 194 within 5 %) |

single-core-zfinx-fpu per family (it01), mean |err| %: empty 134.00, nop16 41.15, nop32 45.69,
alu 17.13, branch 46.77, fetch 41.01, operand 5.98, mem 6.41, mix 2.11, result_delay 36.92,
irwin_alu 35.19, irwin_control 69.30, cache 29.89, xip_miss 16.44, kern 12.37, dma 18.55,
rand_asm 27.05, rand_fact 7.17, **fp_ops 74.23, fp_delay 36.57, fp_busy 27.36, fp_cvt 29.90**.
instret: no new DIFFERS (the same alu/dmb and six dma bodies as it00; every fp_* body matches).

Worst FP bodies: fp_ops vdiv_1_3 / vsqrt_hard -90.5 % (388 -> 37), vdiv_hard -90.0 %, and the
vdiv/vsqrt_bitsN series (248..369 cycles in the RTL, data-dependent: the TH32 divide/sqrt;
gem5 ~37).

Open issues: none new. Not covered by a test: that FMA / flw / fmv trap under Zfinx (follows
from FS Off through updateFPUStatus, as for gem5's F with FS Off).

## Item 2 (it02): single issue and load timing, 2026-11-14

Ranked list proposed (after it01): 1. branch prediction (single-entry BTB) and taken-branch
penalty; 2. execute latencies (ALU, mul, div, FPU); 3. measurement / CSR-read overhead; 4.
load-use and load/store hazards. Selected: **1**. While analysing it, Minor's default dual issue
turned out to hide the branch penalty (nop16_64: RTL 70, gem5 46 cycles), and single issue alone
made every load take 2 cycles (mem 6 % -> 55 % in an unrecorded wip run). The designer split the
item: **single issue and load timing first (it02)**, branches and the BTB next.

What changed (`configs/example/rp2350/board.py`, new `_hazard3_pipeline`, applied to every
Hazard3 Minor core in `_hazard3_cpu`; board-only, no Minor or ARM change):

- Single issue: `decodeInputWidth`, `executeInputWidth`, `executeIssueLimit` = 1 (were 2), as
  Hazard3's decode hands X one instruction a cycle. `executeCommitLimit` stays 2: a load/store
  finishing in M and the next instruction in X complete in the same cycle (1 made every store or
  load burst cost an extra cycle: mx_alu_store_b4_q 420 -> 520, RTL 413).
- Loads go to the bus in their issue cycle (`executeLoadAccessAtIssue`), as Hazard3's address
  phase in X and data phase in M.
- Load-use stall: the memory FU's timings (otherwise Minor's default FU pool): a load has
  `resultUsePenalty=1`, a store reads rs1 at issue and rs2 a cycle later
  (`srcRegsRelativeLats=[1, 2]`), as `hazard3_core.v` `x_stall_on_raw` (stall on a load's rd, not
  for a store's data).
- Tried and dropped: fetch1LineWidth 4 (Hazard3 fetches one 32-bit word a cycle). Minor's
  two-stage fetch then issues a 4-byte fetch only every other cycle (fetch1 waits for the line to
  be processed; fetch1FetchLimit 2 or 4 does not help), doubling every body. Fetch stays at the
  board's 8-byte line; the frontend's shape belongs to the branch/frontend item.

Tests (gem5): identical to it01 in both configs, test by test and check by check, except
test_xip_timing case_2_miss now measures 116 cycles (was 117; still FAILED, expected 112).

Microbenchmarks:

| config | it00 | it01 | it02 |
|---|---|---|---|
| single-core | 25.11 % | 25.11 % | **24.34 %** (491, 204 within 5 %) |
| single-core-zfinx-fpu | refused | 28.64 % | **28.03 %** (558, 203 within 5 %) |

single-core per family, it01 -> it02 (mean |err| %): empty 134.00 -> 134.00, nop16 41.15 ->
42.76, nop32 45.69 -> 45.69, alu 17.28 -> 15.56, branch 46.77 -> 53.50, fetch 41.04 -> 41.36,
operand 5.98 -> 6.99, mem 5.93 -> 6.44, mix 1.99 -> 1.91, result_delay 37.41 -> 35.35, irwin_alu
35.11 -> 35.67, irwin_control 69.48 -> 73.53, **cache 29.89 -> 2.57**, xip_miss 17.03 -> 17.35,
kern 11.61 -> 16.53, dma 16.74 -> 15.32, **rand_asm 26.97 -> 20.36**, rand_fact 7.16 -> 7.85.
single-core-zfinx-fpu (it02): fp_ops 74.12, fp_delay 37.81, fp_busy 28.22, fp_cvt 33.78; the
other families as single-core within ~0.5 points.

- Better: bodies gem5 ran ~2x too fast with dual issue now fit: cache cap_64..cap_1024,
  icache_miss, prefetch -45..-50 % -> 0.3..4.7 %; alu16_rand -45.9 -> +1.7 %, fetch_nop16_128/256
  -42..-46 -> +3..5 %; br_back_64 -45 -> +4.7 %; result_delay load_store +52.9 -> +11.9 %.
  Straight-line slopes now match the RTL: nop16/nop32 1 cycle each, independent loads 1,
  dependent loads 2 (mem load_dep 108 vs 115); what is left there is the constant +7..8 cycles of
  measurement overhead.
- Worse: what dual issue used to hide. Short nop bodies whose overhead was compensated
  (nop16_16 0 -> +31.8 %); branches (br_nottaken +51 -> +100 %, br_back_4 +1.8 -> +50.9 %,
  bp_alternating +46 -> +73 %, cf_loop_body4_* +5..10 -> +46..55 %) - the next item; multiply
  latency (rand_fact rf_mem_v16_* 1.7 -> 13.4 %: a mulh every 16 instructions, Minor's IntMul
  opLat 3 against Hazard3's fast multiplier) - the execute-latency item; kern_tree_sram +46 -> +54 %.
- instret: unchanged (alu/dmb and the six dma bodies, as it00).

Open issues: none new.

## Item 3 (it03): frontend and branch prediction, 2026-11-14

Ranked list proposed (after it02): 1. frontend and branches (single-entry BTB, taken-branch
penalty, 32-bit fetch); 2. execute latencies (mul, div, FPU); 3. measurement overhead; 4. Zcmp
push/pop and multi-word load/store. Selected: **1**.

The RTL's behaviour, from the bodies and rtl-sim's waveform (fetch br_back_0, branch bp_align):

- A taken branch or jump not predicted is resolved in X and costs 1 cycle: its target's address
  phase is in the same cycle (`jump_target_vld`), the data is decoded in the cycle it arrives
  (`fetch_data = mem_data`), X the cycle after (cbz_taken, direct_forward16/32, jump_indirect,
  call_*: one cycle more than the instructions). A not-taken branch costs nothing.
- The BTB (`have_btb`, one entry) is set by a taken backward conditional branch that was not
  predicted (`x_btb_set`), cleared by a predicted branch that falls through, fence.i or a trap
  (`x_btb_clear`), and compared with `fetch_addr` (`btb_match_word`): a predicted branch costs
  nothing. After a jump from X, `fetch_addr` already points one word beyond the target
  (`jump_target_post_increment`), so a branch in the jump target's word is never predicted:
  br_back_0's 2-instruction loop in one word takes 3 cycles an iteration (xjmp every iteration in
  the waveform), br_fwd_N's backward branch in the forward branch's target word costs +1.
- JAL/JALR are never predicted (no RAS).

What changed:

- New predictor (`src/cpu/pred/hazard3_bp.{hh,cc}`, `BranchPredictor.py`, `SConscript`):
  `Hazard3BTB`, the single entry (set only by taken backward conditional branches), and
  `Hazard3BP`, the conditional predictor (taken, so that BPredUnit takes a branch exactly on a BTB
  hit; clears the entry when a predicted branch falls through). Board: `BranchPredictor(btb,
  Hazard3BP(btb), ras=NULL, indirectBranchPred=NULL)`. Not modelled: the clear on fence.i and
  traps.
- Minor, three new parameters, each defaulting to the old behaviour (ARM models unchanged):
  - `fetchUncondDirectTaken` (True; `fetch2.cc`): single-stage fetch took every unconditional
    direct jump in fetch; False leaves it to Execute.
  - `fetchToDecodeSameCycle` (False; `pipeline.cc`): Decode reads the fetch stage's output in
    the same cycle (the latch's input wire, Decode evaluated after fetch), as Hazard3 decodes
    fetch data in the cycle it arrives. Without it Minor has one stage too many: a taken branch
    cost 2 cycles instead of 1.
  - `fetchPredictInRedirectWord` (True; `fetch2.cc`): False takes back a prediction for a
    branch in the 4-byte word an Execute stream change went to.
- Board (`_hazard3_pipeline`): `singleFetchStage`, 4-byte fetch (`fetch1LineWidth` /
  `fetch1LineSnapWidth` 4), `fetch1FetchLimit` 2 (pending_fetches), `fetchReadAheadLimit` 8
  (the 2-word FIFO), `executeBranchAtIssue`, the three parameters above, and a 1-cycle IntFU
  (opLat 1, srcRegsRelativeLats [1]): Minor's default 3-cycle IntFU resolved branches 3 cycles
  after issue. The 1-cycle IntFU also gives an ALU instruction reading a load result the
  RTL's 1-cycle stall, which it02 missed.

Tests (gem5): pass/fail identical to it02 in both configs, check by check; only the measured
values in already-failing checks changed: test_xip_timing case_1_hit 7 -> 4 cycles (expected 2),
case_2_miss 116 -> 113 (expected 112); test_xip_ctrl case_2 hit 228 -> 296 (expected 0; 4-byte
fetches count more cache hits).

Microbenchmarks:

| config | it00 | it01 | it02 | it03 |
|---|---|---|---|---|
| single-core | 25.11 % | 25.11 % | 24.34 % | **9.62 %** (491, 295 within 5 %) |
| single-core-zfinx-fpu | refused | 28.64 % | 28.03 % | **14.62 %** (558, 302 within 5 %) |

single-core per family, it02 -> it03 (mean |err| %): empty 134.00 -> 33.33, nop16 42.76 ->
11.42, nop32 45.69 -> 11.42, alu 15.56 -> 12.88, **branch 53.50 -> 2.29**, **fetch 41.36 ->
7.04**, operand 6.99 -> 2.62, mem 6.44 -> 2.08, mix 1.91 -> 1.87, result_delay 35.35 -> 28.99,
irwin_alu 35.67 -> 24.11, **irwin_control 73.53 -> 4.70**, cache 2.57 -> 0.07, xip_miss 17.35 ->
3.19, kern 16.53 -> 2.58, dma 15.32 -> 6.71, rand_asm 20.36 -> 12.81, rand_fact 7.85 -> 10.26.
single-core-zfinx-fpu (it03): fp_ops 71.87, fp_delay 38.89, fp_busy 20.94, fp_cvt 29.73; the
other families as single-core within ~0.6 points.

- Measurement overhead: +7..8 -> +2 cycles a body (empty 6 -> 8, RTL 6).
- Better: 359 bodies by more than a point; unpredicted taken branches and jumps, calls, loops
  now within the +2 cycles of overhead (cbz_taken_100 +0.65 %, jump_indirect_100 +0.40 %,
  br_back_N, bp_long_body +0.2 %).
- Worse (62 bodies by more than a point): the multiplier. With a 1-cycle ALU, Minor's 3-cycle
  IntMul shows (a mulh followed by an ALU op writing its register): rand_fact rf_alu_v4_* and
  rf_uniform_v4_* +2 -> +28..30 %, result_delay mul_use +14 -> +65 %, mul_chain, mx_alu_mul_b4_r
  +18 %, fetch_interleave_mul +35 %; result_delay bytes_load_word +42 -> +75 %. For the
  execute-latency item.
- instret: unchanged (alu/dmb and the six dma bodies).

Open issues:

- fetch/br_fwd_0 -38 %: a taken branch to the next instruction. Hazard3 redirects (+1, and the
  backward branch then lands in the redirect word); gem5's PC after the branch is the same
  either way, so Minor sees no branch.
- branch/bp_align +14 % (loop C, a 32-bit instruction across a word boundary then the branch: RTL
  2 cycles an iteration, gem5 3). The RTL's BTB redirects fetch when it fetches the branch's word;
  Minor predicts when it decodes the branch, a cycle later here because decode trails fetch.
- BTB clear on fence.i and trap entry not modelled.

## Item 4 (it04): integer multiply and divide, 2026-11-14

Ranked list proposed (after it03): 1. integer multiply and divide; 2. FPU timing (zfinx);
3. remaining measurement overhead (+2 cycles); 4. fetch-time BTB redirect and branch to the next
instruction. Selected: **1**.

The RTL (`hazard3_core.v`, `arith/hazard3_mul_fast.v`, `arith/hazard3_muldiv_seq.v`; MUL_FAST =
MUL_FASTER = MULH_FAST = 1, MULDIV_UNROLL = 2):

- MUL, MULH, MULHU, MULHSU: the fast multiplier with MUL_FASTER gives its result combinationally
  in X (`op_passthrough`), bypassed like an ALU result, no use stall (`m_fast_mul_result_vld`
  stays 0): one cycle (result_delay mul_chain RTL 58 = 50 dependent muls + overhead).
- DIV, DIVU, REM, REMU: the sequential divider, X stalled until it is done (`x_stall_muldiv`):
  the issue cycle, the sign pre-adjust (always for an unsigned op; for a signed one unless the
  dividend is non-negative and the divisor negative, `op_b_neg ^ is_div`, `accum_neg_l`, when the
  first iteration is done in it instead), 32/2 iterations, and the post-adjust, a cycle more when
  it negates the result (`op_signs_differ`, divisor non-zero). 19 cycles; a signed op of a
  negative dividend by a positive divisor 20 (udiv_*_100 1908, sdiv_fast 1 / -2 also 1908).

What changed:

- Minor (`func_unit.{hh,cc}`, `BaseMinorCPU.py`): `DynamicLatencyIntDivFU.hazard3_timing`
  (default False): one cycle beyond opLat for a signed DIV/REM of a negative dividend by a
  positive divisor.
- Board (`_hazard3_pipeline`): IntMul FU opLat 1, srcRegsRelativeLats [1] (was 3);
  `DynamicLatencyIntDivFU(opLat=19, issueLat=19, hazard3_timing=True)` for IntDiv (was opLat 9);
  `executeInOrderCompletion` and `executeDynamicLatencyHoldsInst`, so nothing issues behind an
  instruction still in an FU (X stalls), as x_stall_muldiv.

Tests (gem5): pass/fail identical to it03 in both configs, check by check; only
test_xip_timing case_2_miss 113 -> 114 cycles (still FAILED, expected 112).

Microbenchmarks:

| config | it00 | it01 | it02 | it03 | it04 |
|---|---|---|---|---|---|
| single-core | 25.11 % | 25.11 % | 24.34 % | 9.62 % | **3.62 %** (491, 402 within 5 %) |
| single-core-zfinx-fpu | refused | 28.64 % | 28.03 % | 14.62 % | **9.95 %** (558, 410 within 5 %) |

single-core per family, it03 -> it04 (mean |err| %): empty 33.33 -> 33.33, nop16 11.42 -> 11.42,
nop32 11.42 -> 11.42, **alu 12.88 -> 2.08**, branch 2.29 -> 2.29, fetch 7.04 -> 4.83, operand
2.62 -> 1.75, mem 2.08 -> 1.96, mix 1.87 -> 0.87, **result_delay 28.99 -> 10.29**, **irwin_alu
24.11 -> 5.33**, irwin_control 4.70 -> 6.17, cache 0.07 -> 0.07, xip_miss 3.19 -> 3.19, kern 2.58
-> 3.14, dma 6.71 -> 19.93, **rand_asm 12.81 -> 2.58**, **rand_fact 10.26 -> 1.19**.
single-core-zfinx-fpu (it03 -> it04): fp_ops 71.87 -> 44.02, fp_delay 38.89 -> 81.48, fp_busy
20.94 -> 41.31, fp_cvt 29.73 -> 96.69; the other families as single-core within ~0.5 points,
except irwin_control 4.05 and dma 24.18.

- Better: divides (div_flip*, div_rand, sdiv/udiv_*: -45..-53 % -> within +0.3..+0.7 % at 100;
  the 8/9/16 bodies +2..+5 %), multiplies (mul_chain +204 % -> within a few %, mul_use, rand_fact
  rf_*_v4 +28 % -> ~1 %, fetch_interleave_mul/div).
- Worse:
  - FP (zfinx): X now waits for each FP op, as the RTL's FPU stalls X (one op in flight), but
    the FP ops still have Minor's default FloatSimd latencies, which the overlap had hidden:
    fp_delay, fp_busy, fp_cvt worse, fp_ops better. For the FPU item.
  - irwin_control cf_conditional_not_taken_8 +6.5 -> +58.7 %, cf_cmpbeq_taken_9 +19 -> +49 %,
    dma setup +7 -> +62 %, words_1: not the core. Their first measured samples miss in the XIP
    cache (cf_conditional_not_taken_8: samples 141, 141, 141, then 33 throughout; the 8-byte line
    at body+64 misses in samples 1-3; at it03 every sample 33): the changed execute timing changes
    the harness's fetch and access pattern, and with it which lines the 2-way XIP cache keeps
    between samples.
- instret: alu/dmb and the dma bodies as before, but which dma bodies differ changed (words_1
  now equal, workspace_256 now DIFFERS): their polling loops make instret timing-dependent.

Open issues:

- XIP cache residency between samples depends on execute timing (above); memory system, for the
  designer.
- result_delay bytes_load_word +65 %: a word load after four byte stores to that word costs 3
  cycles in gem5 (the store buffer cannot forward partial stores, the load waits for them), 1 in
  the RTL. A load/store hazard item.
- result_delay jump_next -41 %: a taken branch to the next instruction (as fetch br_fwd_0, it03).

## Item 5 (it05): FPU timing, 2026-11-15

Ranked list proposed (after it04): 1. FPU timing (zfinx); 2. remaining measurement overhead
(+2 cycles); 3. load/store hazards; 4. frontend leftovers. Selected: **1**.

The RTL (arcsim-soc `rtl/fpu/hazard3_fpu.sv`, `hazard3_fpnew_cfg.sv`; FPU_PIPE_ADDMUL,
_NONCOMP, _CONV = 1, _DIVSQRT = 0; the E906 divide/sqrt unit `pa_fdsu_ctrl.v`):

- One operation at a time; X stalls until the adapter holds the result (`x_stall_fpu`,
  `busy`, `result_r`): FPnew's one pipeline register plus two cycles, 3 cycles for every add,
  mul, sign-inject, min/max, compare, classify and convert (vadd 50 ops = 158; no extra cycle
  for a dependent FP op or integer use).
- FDIV and FSQRT: a zero, infinite or NaN operand or a negative square root skips the SRT
  (`ex1_srt_skip`): 3 cycles. Otherwise 4 cycles plus the SRT rounds (radix 4, `srt_cnt` from
  14, at most 15), which stop once the partial remainder is zero (`srt_remainder_zero`): an
  exact result of m significant bits takes m/2 + 2 rounds (fp_ops vdiv_bits1..24, vdiv_3_2,
  vdiv_100_2, vdiv_lt2/lt8, vsqrt_bits2..12, vsqrt_4: 6..18 cycles a divide), an inexact one 15
  (vdiv_hard, vdiv_1_3, vsqrt_hard: 19).

What changed:

- Minor (`func_unit.{hh,cc}`, `BaseMinorCPU.py`, `SConscript`): new FU `Hazard3FpuFU`,
  opLat for every operation, plus for FloatDiv/FloatSqrt the rounds above from the operands
  (an integer register under Zfinx, else a float register): the quotient is exact when the
  divisor's mantissa over the gcd of both is a power of two, m the significant bits of the
  dividend's over it; a root is exact when the mantissa (doubled for an odd exponent) is a
  perfect square, m the significant bits of its root.
- Board: `Hazard3FpuFU` (opLat 3, issueLat 3, srcRegsRelativeLats [1]) for FloatAdd, Cmp, Cvt,
  Mult, MultAcc, Div, Misc, Sqrt. Minor's default float/SIMD FU is no longer in the pool: an FP
  op the FPU could not take yet (a loaded operand) issued there instead, with its 6-cycle
  latency (fp_delay vldr_use: 3 cycles a load-use pair instead of the RTL's 1). Hazard3 has no
  SIMD. In-order completion (it04) gives the X stall.

Tests (gem5): identical to it04 in both configs, check by check (test_fp_f, test_fp_zfinx,
test_fpu pass).

Microbenchmarks:

| config | it00 | it01 | it02 | it03 | it04 | it05 |
|---|---|---|---|---|---|---|
| single-core | 25.11 % | 25.11 % | 24.34 % | 9.62 % | 3.62 % | **3.62 %** (every body as it04) |
| single-core-zfinx-fpu | refused | 28.64 % | 28.03 % | 14.62 % | 9.95 % | **4.36 %** (558, 413 within 5 %) |

single-core-zfinx-fpu, it04 -> it05: fp_ops 44.02 -> 9.91 %, fp_delay 81.48 -> 12.41 %,
fp_busy 41.31 -> 10.93 %, fp_cvt 96.69 -> 11.06 %; the other families unchanged.

- Every warm sample of the FP bodies is now within +2..3 cycles of the RTL (vadd 161 vs 158,
  vdiv_bits8 211 vs 208, vdiv_zero 72 vs 69, vldr_use 188 -> 138 vs 135). What is left of the
  ~10 % is their first measured samples, ~100 cycles slower in gem5 (vadd 258, 264, 269, then
  161): a miss in the XIP cache on the harness's arcsim_roi_call_close after the body returns,
  in the first 3-4 measured samples, never in the RTL (as irwin_control and dma at it04).
- The frames' XIP counters show gem5's frontend makes more XIP accesses than the RTL: vadd
  103-105 a sample against the RTL's 79 (xip_hit 103 vs 77). The extra fetch traffic (refetches
  after stream changes, fetches past a jump) changes which lines the 2-way XIP cache keeps. A
  frontend item.

Open issues:

- The early-sample XIP misses above (frontend fetch traffic vs the memory system).
- Not modelled: the E906 unit's extra cycle for a denormal operand (WFI2, `fdsu_dn_stall`)
  and its skip on exponent overflow/underflow (`ex2_of`, `ex2_uf_srt_skip`).
- Scripts' work directory: a helper of mine overwrote `work/single-core/runs/rtl/irwin_control/
  frames` (it imported the skill's analyze.py, whose main ran); regenerated with `SIMS=rtl
  TARGETS=irwin_control FORCE=1`, the it04 analysis reproduced exactly (irwin_control 6.17 %,
  all 3.62 %).

## Item 6 (it06): frontend fetch traffic, 2026-11-15

Ranked list proposed (after it05): 1. frontend fetch traffic (gem5 ~30 % more XIP accesses than
the RTL, early-sample XIP misses); 2. measurement overhead (+2 cycles); 3. load/store hazards;
4. frontend leftovers. Selected: **1**.

The frames' XIP counters (xip_acc a sample, RTL vs gem5 it05) showed two causes:

- +15 accesses in every body, the empty one too (27 vs 42): the ROI open/close sequence's six CSR
  instructions. gem5 marks RISC-V CSR instructions SerializeAfter, and Minor restarted the
  stream after each, fetching the words behind it again. Hazard3 redirects its frontend only for
  a jump from X or a trap (`f_jump_req = x_jump_req || m_trap_enter_vld`), and refetches for
  FENCE.I (`EXCEPT_REFETCH`); a CSR access does not flush it. The same restart was the +2
  cycles of measurement overhead in every body.
- One extra access per divide (udiv_fast_8: 45 vs 37): during a long stall in X, fetch ran up
  to Minor's decode (3) and execute (7) input buffers ahead, words that the body's return then
  threw away. Hazard3 holds one instruction in D (the CIR) and the 2-word FIFO, and stops fetching
  when they are full (`fetch_stall`).

What changed:

- Minor (`execute.{hh,cc}`, `BaseMinorCPU.py`): `executeSerializeAfterRefetches` (default True,
  the old behaviour): False lets only SquashAfter instructions (FENCE.I, WFI) restart the stream.
- Board: `executeSerializeAfterRefetches = False`, `decodeInputBufferSize = 1`,
  `executeInputBufferSize = 1`.

Tests (gem5): as it05 in both configs, except **test_xip_timing case_1_hit and case_2_miss now
pass** (2 and 112 cycles, as the RTL; they failed since the starting point with 7 and 117), so
test_xip_timing still fails only in case_3_uncached (the trap at the uncached alias);
test_xip_ctrl case_2 hit 296 -> 262 (expected 0, as before).

Microbenchmarks:

| config | it00 | it01 | it02 | it03 | it04 | it05 | it06 |
|---|---|---|---|---|---|---|---|
| single-core | 25.11 % | 25.11 % | 24.34 % | 9.62 % | 3.62 % | 3.62 % | **1.01 %** (491, 469 within 5 %) |
| single-core-zfinx-fpu | refused | 28.64 % | 28.03 % | 14.62 % | 9.95 % | 4.36 % | **1.09 %** (558, 542 within 5 %) |

single-core per family, it05 -> it06 (mean |err| %): empty 33.33 -> 0.00, nop16 11.42 -> 0.00,
nop32 11.42 -> 0.00, alu 2.08 -> 1.46, branch 2.29 -> 1.96, fetch 4.83 -> 1.14, operand 1.75 ->
0.00, mem 1.96 -> 0.72, mix 0.87 -> 0.65, result_delay 10.29 -> 4.22, irwin_alu 5.33 -> 0.18,
irwin_control 6.17 -> 0.86, cache 0.07 -> 0.00, xip_miss 3.19 -> 3.01, kern 3.14 -> 2.27, dma
19.93 -> 7.32, rand_asm 2.58 -> 0.51, rand_fact 1.19 -> 0.82.
single-core-zfinx-fpu, it05 -> it06: fp_ops 9.91 -> 0.61, fp_delay 12.41 -> 0.46, fp_busy 10.93
-> 0.63, fp_cvt 11.06 -> 0.42; irwin_control 2.37 (max cf_direct_forward32_9 +116 %), irwin_alu
0.71 (max umull_16 +30 %).

- The early-sample XIP misses of it04/it05 are gone in single-core and in every fp body; xip_acc
  a sample now equals the RTL's (nop16_*, udiv_fast_8 37, cf_direct_forward32_* 54, vadd).
- instret: only alu/dmb differs now (the dma bodies' polling loops match the RTL's).
- Left: zfinx cf_direct_forward32_9 and umull_16: their last samples equal the RTL (33 cycles,
  54 accesses; 40), but a run of early samples misses in the XIP cache (161 cycles) before it
  settles; their access counts match the RTL's, so this is how gem5's XIP cache state evolves
  over the samples in that config's layout (memory system).

Open issues:

- XIP cache state over the early samples (zfinx cf_direct_forward32_9, umull_16; dma setup /
  words_1 / words_16 +17..36 %): memory system, for the designer.
- result_delay jump_next -45 % and fetch br_fwd_0 -39 %: taken branch to the next instruction.
- branch bp_align +14 %: fetch-time BTB redirect.
- result_delay bytes_load_word: word load after byte stores (store buffer).
- alu/dmb instret differs (since it00).
