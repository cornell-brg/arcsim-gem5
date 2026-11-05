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
result; the cycles measured on the SoC's rtl_sim by smoke/zfinx_timing.S):
fp_cycles for an add, multiply, compare, convert or sign-inject,
fp_div_cycles for a divide, fp_sqrt_cycles for a square root."""

from m5.objects import RiscvInstLatency, RiscvLatencyClass

from board import (
    HAZARD3_DIV_CYCLES,
    HAZARD3_FP_CYCLES,
    HAZARD3_FP_DIV_CYCLES,
    HAZARD3_FP_OPS,
    HAZARD3_FP_SQRT_CYCLES,
    HAZARD3_LOAD_RESULT_DELAY,
)


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
    return RiscvInstLatency(classes=classes, **kwargs)
