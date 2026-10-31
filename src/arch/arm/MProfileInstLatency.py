from m5.objects.InstLatency import BaseInstLatency
from m5.params import *
from m5.SimObject import SimObject


class MProfileLatencyClass(SimObject):
    """One class of instruction in an MProfileInstLatency table: the
    instructions it takes (by OpClass and by mask and match on the
    ExtMachInst, as a MinorFUTiming takes them) and what they cost."""

    type = "MProfileLatencyClass"
    cxx_header = "arch/arm/m_inst_latency.hh"
    cxx_class = "gem5::ArmISA::MProfileLatencyClass"

    description = Param.String("the class's name in the statistics")
    op_classes = VectorParam.String(
        [], "the OpClasses (by name) it takes; empty: any"
    )
    mask = Param.UInt64(0, "ExtMachInst bits compared with match")
    match = Param.UInt64(0, "what those bits must be")
    cycles = Param.Cycles(
        1, "the integer core's cycles; a load or store's per register"
    )
    result_delay = Param.Cycles(
        0,
        "cycles after its own (a unit's: after busy; a load's: after the "
        "transfer that brings the register) before an instruction can read "
        "one of its results",
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
    divide = Param.Bool(
        False, "cycles from its operands, as the RP2350's Cortex-M33 divides"
    )
    branch_extra = Param.Cycles(0, "more cycles when it is a taken branch")
    accumulate = Param.Bool(
        False,
        "a multiply whose accumulate operands (Ra; a long multiply's RdLo "
        "and RdHi), named by the encoding, are read a cycle late, so a "
        "result delay does not hold them",
    )


class MProfileInstLatency(BaseInstLatency):
    """Each instruction's cycles on an Arm M-profile core, for the atomic
    CPU (its inst_latency): a class's cycles by the first class that takes
    the instruction, plus

    - a result delay when the next instruction reads its result;
    - waiting for its unit (the FPU) to be done with the one before;
    - a branch's cycles (taken: taken_branch, plus branch_from_load when a
      load writes the PC, plus unaligned_target when the target is a 32-bit
      instruction at a halfword address; less one when folded with the
      instruction before, unless conditional on flags that instruction
      set; not taken: not_taken_branch, less one when folded);
    - folding: an instruction of fold kind 1 (NOP, B, B<cond>, BX LR) takes
      a cycle less after a 16-bit instruction that was not itself folded
      and was reached in sequence or is word-aligned; one of kind 2 (IT)
      takes none before a 16-bit instruction;
    - the memory: the latency the memory returns for an access to
      timed_ranges, and wait_cycles for one to the matching wait_ranges;
    - exception entry, return and tail-chaining.

    The RP2350 datasheet's section 3.7.4.9 gives these rules for its
    Cortex-M33."""

    type = "MProfileInstLatency"
    cxx_header = "arch/arm/m_inst_latency.hh"
    cxx_class = "gem5::ArmISA::MProfileInstLatency"

    classes = VectorParam.MProfileLatencyClass(
        "the classes, the first that takes an instruction its own"
    )
    default_cycles = Param.Cycles(
        1, "the cycles of an instruction no class takes"
    )
    fold_masks = VectorParam.UInt64([], "as MinorCPU's fetchFoldMasks")
    fold_matches = VectorParam.UInt64([], "as MinorCPU's fetchFoldMatches")
    fold_kinds = VectorParam.Unsigned(
        [],
        "as MinorCPU's fetchFoldKinds: 1 folds into the instruction before, "
        "2 into the one after, 0 neither",
    )
    taken_branch = Param.Cycles(2, "a taken branch's cycles")
    not_taken_branch = Param.Cycles(1, "a branch not taken's cycles")
    branch_from_load = Param.Cycles(
        1, "more for a taken branch a load makes, after the load's own"
    )
    unaligned_target = Param.Cycles(
        1,
        "more when a taken branch's target is a 32-bit instruction at a "
        "halfword address, unless a PUSH or POP came before the branch",
    )
    fetch_ahead = Param.Bool(
        True,
        "after a taken branch, read the 32-bit word after the one that ends "
        "the branch, as the core has fetched it by the time the branch "
        "takes effect (a flash miss when it starts a new line)",
    )
    exception_entry = Param.Cycles(12, "cycles to enter an exception")
    exception_return = Param.Cycles(10, "cycles to return from one")
    tail_chain = Param.Cycles(
        6, "cycles from an exception's return straight into another's entry"
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
