from m5.objects.HWPE import HWPEBase
from m5.params import *


class HWPELk(HWPEBase):
    """
    arcsim-xcel's lkTop, the LK engine of the optical-flow HWPE: one job
    tracks a list of point records through pyramidal LK (win 15, 3 levels,
    Q14), as arcsim-kernels klt/lk.h track_point_q, the arithmetic from
    that code (of_reference.cc, built from $ARCSIM_KERNELS_HOME).

    Job registers: 0x40 LIST_PTR, 0x44 N_POINTS, 0x48 PATCH_BASE (slot s,
    level l at PATCH_BASE + 972 s + 324 l), 0x4C MIN_EIG, 0x50 EPS2,
    0x54 MAX_ITER, 0x58 + 12 l level l's BASE, SIZE (height in 31:16,
    width in 15:0, the width the row stride) and HI (rows [0, hi) held),
    0x7C SCRATCH_PTR (not used). 0x18 reads the points processed in bits
    7:0 and those with done = 0 in 15:8. A record is 32 B: word 0 the slot
    in bits 7:0, words 1..4 px0, py0, gx, gy; the job writes words 5..7,
    x, y and the flags, in one access.

    Timing follows lkTop and lkPoint state by state, one 32-bit TCDM read
    at a time, a cycle a state: the record's 5 words, then for each level
    its 81 patch words, the template (template_cycles from the last patch
    word to the first iteration, reject_cycles to the level's end if the
    solve rejects it), and for each iteration the window's 256 pixels, one
    a cycle from the last word read, a read for each other word, and
    mismatch_cycles from the last pixel to the next iteration. Then the
    result write and the next record; the job ends end_cycles after the
    last result write.
    """

    type = "HWPELk"
    cxx_class = "gem5::HWPELk"
    cxx_header = "dev/hwpe/hwpe_lk.hh"

    num_io_regs = 16
    mux_channels = 1
    start_latency = 4
    done_latency = 1
    b_shift = Param.Unsigned(2, "The mismatch sums' shift (lkPackage B_SHIFT)")
    point_cycles = Param.Cycles(2, "The last record word to LEVEL_START")
    template_cycles = Param.Cycles(
        260, "The last patch word to the first ITER_START"
    )
    reject_cycles = Param.Cycles(
        229, "The last patch word to LEVEL_END when the solve rejects"
    )
    mismatch_cycles = Param.Cycles(
        228, "The window's last pixel to the next ITER_START or LEVEL_END"
    )
    end_cycles = Param.Cycles(1, "The last result write to the job's end")
