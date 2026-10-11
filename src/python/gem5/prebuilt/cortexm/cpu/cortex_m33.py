"""
Cortex-M33 MinorCPU for the RP2350, tuned against a Pico 2.

A configuration of its own beside the Cortex-M4's (cortex_m4.py), so that
tuning one against its board leaves the other's timing alone. The pipeline
is the same three stages (Fetch, Decode, Execute) with a 32-bit fetch bus.
"""

from m5.objects.ArmMCPU import ArmMMinorCPU
from m5.objects.BaseMinorCPU import (
    DynamicLatencyIntDivFU,
    MinorFU,
    MinorFUPool,
    MinorFUTiming,
    minorMakeOpClassSet,
)
from m5.objects.BranchPredictor import (
    BranchPredictor,
    ReturnAddrStack,
    SimpleBTB,
    StaticBTFNT,
)
from m5.params import NULL

# ---------------------------------------------------------------------------
# Functional units
# ---------------------------------------------------------------------------


class M33IntAluFU(MinorFU):
    """ADD, SUB, MOV, AND, ORR, EOR, CMP, CMN, TST, TEQ, CLZ, REV, etc."""

    opClasses = minorMakeOpClassSet(["IntAlu"])
    opLat = 1
    issueLat = 1
    timings = [MinorFUTiming(description="M33Int", srcRegsRelativeLats=[1])]


class M33IntMulFU(MinorFU):
    """MUL, MLA, MLS, SMULL, UMULL, SMLAL, UMLAL: one cycle."""

    opClasses = minorMakeOpClassSet(["IntMult"])
    opLat = 1
    issueLat = 1
    timings = [MinorFUTiming(description="M33Mul", srcRegsRelativeLats=[0])]


class M33IntDivFU(DynamicLatencyIntDivFU):
    """SDIV, UDIV: cycles that depend on the operands, as the RP2350's
    Cortex-M33 takes them. opLat is the minimum; the rest is computed at
    commit, and the unit stays busy until then."""

    opClasses = minorMakeOpClassSet(["IntDiv"])
    opLat = 2
    issueLat = 2
    rp2350_m33_timing = True


class M33FloatFU(MinorFU):
    """FPv5 single-precision and DSP SIMD operations of one cycle.

    The ARM ISA maps VFP single-precision Thumb instructions to SimdFloat*
    OpClasses (not Float*), so both families are listed."""

    opClasses = minorMakeOpClassSet(
        [
            "FloatAdd",
            "FloatCmp",
            "FloatCvt",
            "FloatMisc",
            "FloatMult",
            "SimdFloatAdd",
            "SimdFloatAlu",
            "SimdFloatCmp",
            "SimdFloatCvt",
            "SimdFloatMisc",
            "SimdFloatMult",
            "SimdAdd",
            "SimdAlu",
            "SimdCmp",
            "SimdMisc",
            "SimdMult",
            "SimdShift",
            "SimdShiftAcc",
        ]
    )
    opLat = 1
    issueLat = 1
    timings = [MinorFUTiming(description="M33Float", srcRegsRelativeLats=[1])]


class M33FloatMacFU(MinorFU):
    """VMLA, VFMA, VFMS, VNMLA, VNMLS: three cycles, not pipelined."""

    opClasses = minorMakeOpClassSet(
        ["FloatMultAcc", "SimdMultAcc", "SimdFloatMultAcc"]
    )
    opLat = 3
    issueLat = 3
    timings = [MinorFUTiming(description="M33FMac", srcRegsRelativeLats=[2])]


class M33FloatDivFU(MinorFU):
    """VDIV.F32 and VSQRT.F32: 14 cycles, not pipelined.

    The divider works beside the pipeline: the instruction leaves the
    execute stage after one cycle, later instructions that do not read
    its result go ahead, and its result is ready 14 cycles after issue.
    A second divide waits for the first (issueLat)."""

    opClasses = minorMakeOpClassSet(
        ["FloatDiv", "FloatSqrt", "SimdFloatDiv", "SimdFloatSqrt"]
    )
    opLat = 1
    issueLat = 14
    resultAfterCommit = True
    timings = [
        MinorFUTiming(
            description="M33FDiv", srcRegsRelativeLats=[0], extraAssumedLat=14
        )
    ]


class M33MemFU(MinorFU):
    """Loads and stores. One cycle for the address; the access's own time
    comes from the memory system."""

    opClasses = minorMakeOpClassSet(
        ["MemRead", "MemWrite", "FloatMemRead", "FloatMemWrite"]
    )
    opLat = 1
    issueLat = 1
    # A load's result is ready after its data phase, so a load whose
    # address depends on it waits for that.
    timings = [
        MinorFUTiming(
            description="M33Mem", srcRegsRelativeLats=[1], extraAssumedLat=2
        )
    ]


class M33MiscFU(MinorFU):
    """DMB, DSB, ISB, NOP, BKPT, system instructions."""

    opClasses = minorMakeOpClassSet(["InstPrefetch", "System"])
    opLat = 1
    issueLat = 1


class CortexM33FUPool(MinorFUPool):
    funcUnits = [
        M33IntAluFU(),
        M33IntMulFU(),
        M33IntDivFU(),
        M33FloatFU(),
        M33FloatMacFU(),
        M33FloatDivFU(),
        M33MemFU(),
        M33MiscFU(),
    ]


# ---------------------------------------------------------------------------
# Branch predictor
# ---------------------------------------------------------------------------


class CortexM33BP(BranchPredictor):
    """The Cortex-M33 has no learned branch predictor. Backward conditional
    branches are taken as fetched and forward ones fall through; a small
    BTB and return stack stand for the pipeline computing targets early."""

    instShiftAmt = 1  # Thumb: instructions are half-word aligned
    conditionalBranchPred = StaticBTFNT()
    btb = SimpleBTB(numEntries=16, tagBits=16, instShiftAmt=1)
    ras = ReturnAddrStack(numEntries=4)
    indirectBranchPred = NULL


# ---------------------------------------------------------------------------
# CortexM33CPU
# ---------------------------------------------------------------------------


class CortexM33CPU(ArmMMinorCPU):
    """
    MinorCPU modelling the Cortex-M33's three-stage pipeline.

    singleFetchStage collapses Minor's Fetch1 and Fetch2 into one fetch
    stage, giving Fetch, Decode and Execute. All widths are 1: one
    instruction decoded, issued and committed a cycle.
    """

    threadPolicy = "SingleThreaded"

    # -- Fetch stage --
    # One word a fetch, as the 32-bit fetch bus gives, with two fetches on
    # their way at once.
    singleFetchStage = True
    fetch1FetchLimit = 2
    fetch1LineSnapWidth = 4
    fetch1LineWidth = 4
    fetch1ToFetch2ForwardDelay = 0  # bypassed with singleFetchStage
    fetch1ToFetch2BackwardDelay = 0  # bypassed with singleFetchStage
    # The prefetch holds a few bytes: the core has fetched a word or two
    # past a branch when the branch takes effect. 8 bytes are what decode
    # can take in the two cycles a fetch needs here, so it waits for no
    # line it would not wait for without the limit.
    fetchReadAheadLimit = 8
    # Direct branch targets are computed in Decode and fetched from there;
    # a conditional branch's target is fetched while its condition
    # resolves.
    fetchBranchTargetAtDecode = True

    fetch2InputBufferSize = 2
    fetch2ToDecodeForwardDelay = 1
    fetch2CycleInput = True

    # -- Decode stage --
    decodeInputBufferSize = 1
    decodeToExecuteForwardDelay = 1
    decodeInputWidth = 1
    decodeCycleInput = True

    # -- Execute stage --
    executeInputWidth = 1
    executeCycleInput = True
    executeIssueLimit = 1
    executeCommitLimit = 2
    executeMemoryIssueLimit = 1
    executeMemoryCommitLimit = 1
    executeInputBufferSize = 3

    # -- Memory --
    # Two accesses in memory at once, so that a store's data phase and the
    # micro-ops of PUSH and POP overlap the next access's address phase.
    executeMaxAccessesInMemory = 2
    executeLSQMaxStoreBufferStoresPerCycle = 2
    executeLSQRequestsQueueSize = 1
    executeLSQTransfersQueueSize = 2
    executeLSQStoreBufferSize = 3
    executeBranchDelay = 1
    # A branch drives its target address in its execute cycle, so the
    # refill starts there rather than a cycle later at commit.
    executeBranchAtIssue = True
    # One execute stage: a multi-cycle instruction (a load's data phase,
    # SDIV, VMLA, LDM) holds up the next one, except that loads and stores
    # pipeline behind each other and VDIV/VSQRT complete beside the
    # pipeline (M33FloatDivFU).
    executeInOrderCompletion = True
    # SDIV/UDIV's data-dependent cycles (M33IntDivFU) stall the pipeline
    # like any other multi-cycle instruction.
    executeDynamicLatencyHoldsInst = True
    executeMemoryWidth = 8  # LDRD/STRD move 8 bytes
    executeSetTraceTimeOnCommit = True
    executeSetTraceTimeOnIssue = False
    executeAllowEarlyMemoryIssue = True
    enableIdling = True

    executeFuncUnits = CortexM33FUPool()
    branchPred = CortexM33BP()
