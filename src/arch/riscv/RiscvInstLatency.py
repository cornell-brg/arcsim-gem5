from m5.objects.InstLatency import BaseInstLatency
from m5.params import *
from m5.SimObject import SimObject


class RiscvLatencyClass(SimObject):
    """One class of instruction in a RiscvInstLatency table: the
    instructions it takes (by OpClass and by mask and match on the machine
    code, as a MinorFUTiming takes them) and what they cost."""

    type = "RiscvLatencyClass"
    cxx_header = "arch/riscv/inst_latency.hh"
    cxx_class = "gem5::RiscvISA::RiscvLatencyClass"

    description = Param.String("the class's name in the statistics")
    op_classes = VectorParam.String(
        [], "the OpClasses (by name) it takes; empty: any"
    )
    mask = Param.UInt64(0, "machine code bits compared with match")
    match = Param.UInt64(0, "what those bits must be")
    cycles = Param.Cycles(1, "its cycles")
    result_delay = Param.Cycles(
        0,
        "cycles after its own before an instruction can read one of its "
        "results",
    )
    src_lats = VectorParam.Cycles(
        [],
        "per source operand, as MinorFUTiming's srcRegsRelativeLats (the "
        "last entry for the rest): one larger than the smallest is read a "
        "cycle late, so a result delay holds it a cycle less",
    )
    unit = Param.Int(
        -1,
        "the unit it issues to (-1: none); an instruction for a unit waits "
        "until the unit is done with the one before",
    )
    busy = Param.Cycles(1, "cycles it keeps its unit")


class RiscvInstLatency(BaseInstLatency):
    """Each instruction's cycles on an in-order RISC-V core, for the atomic
    CPU (its inst_latency): a class's cycles by the first class that takes
    the instruction, plus

    - a result delay when the next instruction reads its result;
    - waiting for its unit to be done with the one before;
    - a micro-op's cycle for each of a split instruction's after its first;
    - a branch's or jump's cycles: taken_branch when taken, not_taken_branch
      when not; with predictor, the last taken backward branch is predicted
      taken when it comes again (predicted_branch) and forgotten, at
      taken_branch, when it is not taken;
    - unaligned_target when a taken branch's target is a 32-bit instruction
      at a halfword address;
    - the memory: the latency it returns for an access to timed_ranges, and
      wait_cycles for one to the matching wait_ranges;
    - trap_entry for an interrupt or a trap (ecall, ebreak, a fault).

    Hazard3's instruction_timings.adoc gives these rules for Hazard3."""

    type = "RiscvInstLatency"
    cxx_header = "arch/riscv/inst_latency.hh"
    cxx_class = "gem5::RiscvISA::RiscvInstLatency"

    classes = VectorParam.RiscvLatencyClass(
        "the classes, the first that takes an instruction its own"
    )
    default_cycles = Param.Cycles(
        1, "the cycles of an instruction no class takes"
    )
    taken_branch = Param.Cycles(2, "a taken branch's or jump's cycles")
    not_taken_branch = Param.Cycles(1, "a branch not taken's cycles")
    predicted_branch = Param.Cycles(
        1, "a taken branch the predictor predicted"
    )
    predictor = Param.Bool(
        True, "Hazard3's predictor of the last taken backward branch"
    )
    unaligned_target = Param.Cycles(
        1,
        "more when a taken branch's target is a 32-bit instruction at a "
        "halfword address",
    )
    trap_entry = Param.Cycles(3, "an interrupt's or trap's entry to mtvec")
    e906_div_sqrt = Param.Bool(
        False,
        "a float divide's or square root's cycles from its operands, as "
        "T-Head's E906 unit takes them (cpu/e906_divsqrt.hh), in place of "
        "its class's",
    )
    timed_ranges = VectorParam.AddrRange(
        [], "memory whose accesses cost the latency it returns"
    )
    wait_ranges = VectorParam.AddrRange(
        [], "memory whose accesses cost wait_cycles"
    )
    wait_cycles = VectorParam.Cycles(
        [], "per wait range, a read's cycles beyond the instruction's"
    )
    wait_write_cycles = VectorParam.Cycles(
        [],
        "per wait range, a write's cycles beyond the instruction's (empty: "
        "as a read's)",
    )
