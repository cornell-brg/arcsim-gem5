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
