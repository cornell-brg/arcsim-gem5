"""Hazard3's instruction latency table for the atomic CPU (its inst_latency,
RiscvInstLatency): an instruction-level simulator whose time follows each
instruction's cycles instead of one cycle an instruction. The classes and
cycles are the Hazard3 CL model's (board.py's _configure_hazard3_minor, from
Hazard3's instruction_timings.adoc): an instruction 1 cycle; a load's result
a cycle late, except to a store's data, which is read a cycle later; a
divide HAZARD3_DIV_CYCLES; a taken branch or jump 2, a predicted one 1, and
a 32-bit target at a halfword address a cycle more. Zfinx's FP operations
are arcsim-soc's FPU's (rtl/fpu/hazard3_fpu.sv: one at a time, the core
held until its result): fp_cycles for add, multiply, compare and convert
(one plus FPU_PIPE_ADDMUL's, NONCOMP's and CONV's one register), and
fp_div_cycles for divide and square root (FPnew's iterative unit)."""

from m5.objects import RiscvInstLatency, RiscvLatencyClass

from board import HAZARD3_DIV_CYCLES, HAZARD3_LOAD_RESULT_DELAY

FP_OPS = ["FloatAdd", "FloatCmp", "FloatCvt", "FloatMult", "FloatMultAcc",
          "FloatMisc"]
FP_DIV_OPS = ["FloatDiv", "FloatSqrt"]


def hazard3_inst_latency(*, fp_cycles=2, fp_div_cycles=12, **kwargs):
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
        RiscvLatencyClass(description="Hazard3Fpu", op_classes=FP_OPS,
                          cycles=fp_cycles),
        RiscvLatencyClass(description="Hazard3FpuDiv", op_classes=FP_DIV_OPS,
                          cycles=fp_div_cycles),
    ]
    return RiscvInstLatency(classes=classes, **kwargs)
