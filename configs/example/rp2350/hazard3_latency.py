"""Hazard3's instruction latency table for the atomic CPU (its inst_latency,
RiscvInstLatency): an instruction-level simulator whose time follows each
instruction's cycles instead of one cycle an instruction. The classes and
cycles are the Hazard3 CL model's (board.py's _configure_hazard3_minor, from
Hazard3's instruction_timings.adoc): an instruction 1 cycle; a load's result
a cycle late, except to a store's data, which is read a cycle later; a
divide HAZARD3_DIV_CYCLES; a taken branch or jump 2, a predicted one 1, and
a 32-bit target at a halfword address a cycle more. Zfinx's FP operations
are arcsim-soc's FPU's, as board.py's HAZARD3_FP_* give them for both tiers
(rtl/fpu/hazard3_fpu.sv takes one op at a time and holds the core until its
result; the cycles measured on the SoC's rtl_sim, as arcsim-bench's micro/fp_*
Zfinx bodies measure them):
fp_cycles for an add, multiply, compare, convert or sign-inject,
fp_div_cycles for a divide, fp_sqrt_cycles for a square root when the result
needs every bit; e906_div_sqrt gives a divide or root its cycles from its
operands, as T-Head's E906 unit (cpu/e906_divsqrt.hh) and Minor's
Hazard3FpDivSqrtFU take them."""

from m5.objects import RiscvInstLatency, RiscvLatencyClass

# Hazard3's timing (instruction_timings.adoc): a divide's cycles, and the
# cycle a loaded register comes after the load.
HAZARD3_DIV_CYCLES = 19
HAZARD3_LOAD_RESULT_DELAY = 1
# Zfinx on arcsim-soc's FPU (rtl/fpu/hazard3_fpu.sv): one op at a time, the core held until its result.
# Measured on the SoC's rtl_sim (soc 9d3bed4; arcsim-bench's micro/fp_ops, fp_delay, fp_busy and fp_cvt Zfinx
# bodies): an add, multiply, compare, convert or sign-inject 3 cycles, dependent or not; an integer use of the
# result no more. Divide and square root are T-Head's E906 unit, whose cycles follow the operands
# (cpu/e906_divsqrt.hh: 19 when the result needs every bit, fewer when it is exact in a few bits, 3 for a zero
# operand); these two are what a result needing every bit takes.
HAZARD3_FP_CYCLES = 3
HAZARD3_FP_DIV_CYCLES = 19
HAZARD3_FP_SQRT_CYCLES = 19
HAZARD3_FP_OPS = ["FloatAdd", "FloatCmp", "FloatCvt", "FloatMisc", "FloatMult", "FloatMultAcc"]


def hazard3_inst_latency(*, fp_cycles=HAZARD3_FP_CYCLES, fp_div_cycles=HAZARD3_FP_DIV_CYCLES,
                         fp_sqrt_cycles=HAZARD3_FP_SQRT_CYCLES, **kwargs):
    """The table; kwargs set RiscvInstLatency's own parameters
    (timed_ranges, wait_ranges, ...)."""
    classes = [
        # A store reads its address in X and its data in M: a loaded
        # register holds its data a cycle less (srcRegsRelativeLats [1, 2])
        RiscvLatencyClass(description="Hazard3Store",
                          op_classes=["MemWrite", "FloatMemWrite"],
                          src_lats=[1, 2]),
        RiscvLatencyClass(description="Hazard3Load",
                          op_classes=["MemRead", "FloatMemRead"],
                          result_delay=HAZARD3_LOAD_RESULT_DELAY),
        RiscvLatencyClass(description="Hazard3Div", op_classes=["IntDiv"],
                          cycles=HAZARD3_DIV_CYCLES),
        RiscvLatencyClass(description="Hazard3Mul", op_classes=["IntMult"]),
        RiscvLatencyClass(description="Hazard3Fpu", op_classes=HAZARD3_FP_OPS,
                          cycles=fp_cycles),
        RiscvLatencyClass(description="Hazard3FpuDiv", op_classes=["FloatDiv"],
                          cycles=fp_div_cycles),
        RiscvLatencyClass(description="Hazard3FpuSqrt", op_classes=["FloatSqrt"],
                          cycles=fp_sqrt_cycles),
    ]
    return RiscvInstLatency(classes=classes, e906_div_sqrt=True, **kwargs)
