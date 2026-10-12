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
#
# Some results have one cycle of result delay: an instruction that uses one
# as a source operand waits a cycle [RP2350 datasheet 3.7.4.9.1]. That is a
# timing's resultUsePenalty.
# ---------------------------------------------------------------------------

# A 32-bit Thumb instruction in an ExtMachInst: the thumb and bigThumb bits,
# with the instruction's first halfword in bits 31:16 and its second below.
_T32 = (1 << 36) | (1 << 35)


def _t32(mask, match):
    return dict(mask=_T32 | mask, match=_T32 | match)


# Data processing with a shifted register operand (first halfword 1110 101x
# xxxS nnnn; second 0 imm3 dddd imm2 type mmmm)
_SHIFTED_REG = _t32(0xFE000000, 0xEA000000)
# ... whose shift is LSL #0 to #3
_SHIFTED_REG_SMALL = _t32(0xFE007030, 0xEA000000)
# ... that set the flags
_SHIFTED_REG_FLAGS = _t32(0xFE100000, 0xEA100000)
# ... that are the shift instructions LSL, LSR, ASR, ROR and RRX themselves
# (MOV with a shifted register)
_SHIFT_INSTS = _t32(0xFFEF0000, 0xEA4F0000)
# VMOV from a single-precision register to a core register
_VMOV_TO_CORE = _t32(0xFFF00F7F, 0xEE100A10)

# A 16-bit Thumb instruction in an ExtMachInst: thumb without bigThumb, the
# instruction in bits 15:0.
_T16 = 1 << 36


def _fold(patterns):
    """(mask, match, kind) lists for fetchFoldMasks and its companions from
    (halfwords, mask, match, kind) entries."""
    masks, matches, kinds = [], [], []
    for halfwords, mask, match, kind in patterns:
        masks.append(_T32 | mask)
        matches.append((_T32 if halfwords == 2 else _T16) | match)
        kinds.append(kind)
    return masks, matches, kinds


# Folding [RP2350 datasheet 3.7.4.9.7, 3.7.4.9.9]: a NOP, or a B, B<cond> or
# BX LR, takes no cycle of its own after a 16-bit instruction, and an IT
# none before one.
_FOLD_INTO_PREVIOUS = 1
_FOLD_INTO_NEXT = 2
_FOLD_MASKS, _FOLD_MATCHES, _FOLD_KINDS = _fold(
    [
        (1, 0xFFFF, 0xBF00, _FOLD_INTO_PREVIOUS),  # NOP
        (1, 0xFF08, 0xBF08, _FOLD_INTO_NEXT),  # IT: 1011 1111 with a mask
        (1, 0xFF04, 0xBF04, _FOLD_INTO_NEXT),
        (1, 0xFF02, 0xBF02, _FOLD_INTO_NEXT),
        (1, 0xFF01, 0xBF01, _FOLD_INTO_NEXT),
        (1, 0xFE00, 0xDE00, 0),  # UDF and SVC, not B<cond>
        (1, 0xF000, 0xD000, _FOLD_INTO_PREVIOUS),  # B<cond>
        (1, 0xF800, 0xE000, _FOLD_INTO_PREVIOUS),  # B
        (1, 0xFFFF, 0x4770, _FOLD_INTO_PREVIOUS),  # BX LR
        (2, 0xFB80D000, 0xF3808000, 0),  # MSR and others, not B<cond>.W
        (2, 0xF800D000, 0xF0008000, _FOLD_INTO_PREVIOUS),  # B<cond>.W
        (2, 0xF800D000, 0xF0009000, _FOLD_INTO_PREVIOUS),  # B.W
    ]
)


class M33IntAluFU(MinorFU):
    """ADD, SUB, MOV, AND, ORR, EOR, CMP, CMN, TST, TEQ, CLZ, REV, etc."""

    opClasses = minorMakeOpClassSet(["IntAlu"])
    opLat = 1
    issueLat = 1
    # An operand shifted by other than LSL #0 to #3 gives its result a
    # cycle later, and with the flags set the instruction takes that cycle
    # whatever follows it; the shift instructions themselves do not
    # [RP2350 datasheet 3.7.4.9.2].
    timings = [
        MinorFUTiming(
            description="M33Shift", srcRegsRelativeLats=[1], **_SHIFT_INSTS
        ),
        MinorFUTiming(
            description="M33ShiftedSmall",
            srcRegsRelativeLats=[1],
            **_SHIFTED_REG_SMALL,
        ),
        MinorFUTiming(
            description="M33ShiftedFlags",
            srcRegsRelativeLats=[1],
            extraCommitLat=1,
            **_SHIFTED_REG_FLAGS,
        ),
        MinorFUTiming(
            description="M33Shifted",
            srcRegsRelativeLats=[1],
            resultUsePenalty=1,
            **_SHIFTED_REG,
        ),
        MinorFUTiming(description="M33Int", srcRegsRelativeLats=[1]),
    ]


class M33IntMulFU(MinorFU):
    """MUL, MLA, MLS, SMULL, UMULL, SMLAL, UMLAL: one cycle, with a cycle
    of result delay that a following multiply-accumulate's accumulator
    does not wait for [RP2350 datasheet 3.7.4.9.3]."""

    opClasses = minorMakeOpClassSet(["IntMult"])
    opLat = 1
    issueLat = 1
    timings = [
        MinorFUTiming(
            description="M33Mul",
            srcRegsRelativeLats=[1, 1, 2],
            resultUsePenalty=1,
        )
    ]


class M33IntDivFU(DynamicLatencyIntDivFU):
    """SDIV, UDIV: cycles that depend on the operands, as the RP2350's
    Cortex-M33 takes them, with a cycle of result delay [RP2350 datasheet
    3.7.4.9.4]. opLat is the minimum; the rest is computed at commit, and
    the unit stays busy until then."""

    opClasses = minorMakeOpClassSet(["IntDiv"])
    opLat = 2
    issueLat = 2
    rp2350_m33_timing = True
    timings = [
        MinorFUTiming(
            description="M33Div", srcRegsRelativeLats=[0], resultUsePenalty=1
        )
    ]


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
    # VADD, VSUB and VMUL have a cycle of result delay, as has a move to a
    # core register [RP2350 datasheet 3.7.4.9.10].
    timings = [
        MinorFUTiming(
            description="M33FToCore",
            srcRegsRelativeLats=[1],
            resultUsePenalty=1,
            **_VMOV_TO_CORE,
        ),
        MinorFUTiming(
            description="M33FArith",
            srcRegsRelativeLats=[1],
            resultUsePenalty=1,
            opClasses=minorMakeOpClassSet(
                ["FloatAdd", "FloatMult", "SimdFloatAdd", "SimdFloatMult"]
            ),
        ),
        MinorFUTiming(description="M33Float", srcRegsRelativeLats=[1]),
    ]


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
    its result go ahead, and its result comes 14 cycles after issue, with
    a cycle of result delay [RP2350 datasheet 3.7.4.9.10]. A second divide
    waits for the first (issueLat)."""

    opClasses = minorMakeOpClassSet(
        ["FloatDiv", "FloatSqrt", "SimdFloatDiv", "SimdFloatSqrt"]
    )
    opLat = 1
    issueLat = 14
    resultAfterCommit = True
    timings = [
        MinorFUTiming(
            description="M33FDiv", srcRegsRelativeLats=[0], extraAssumedLat=15
        )
    ]


class M33MemFU(MinorFU):
    """Loads and stores of the integer registers. One cycle for the
    address; the access's own time comes from the memory system."""

    opClasses = minorMakeOpClassSet(["MemRead", "MemWrite"])
    opLat = 1
    issueLat = 1
    # A loaded register has one cycle of result delay. The registers a
    # load's or store's address is made from are source operands; the
    # register a store writes to memory is not, and can come a cycle later
    # [RP2350 datasheet 3.7.4.9.5, 3.7.4.9.6].
    timings = [
        MinorFUTiming(
            description="M33Store",
            srcRegsRelativeLats=[1, 2],
            opClasses=minorMakeOpClassSet(["MemWrite"]),
        ),
        MinorFUTiming(
            description="M33Load", srcRegsRelativeLats=[1], resultUsePenalty=1
        ),
    ]


class M33FloatMemFU(MinorFU):
    """Loads and stores of the floating-point registers: as M33MemFU, but
    a loaded register can be used in the next cycle."""

    opClasses = minorMakeOpClassSet(["FloatMemRead", "FloatMemWrite"])
    opLat = 1
    issueLat = 1
    timings = [
        MinorFUTiming(
            description="M33FMem", srcRegsRelativeLats=[1], extraAssumedLat=1
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
        M33FloatMemFU(),
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
    stage, giving Fetch, Decode and Execute. One instruction is decoded,
    issued and committed a cycle, beside those that take no cycle of their
    own.
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
    # So is the target of BX and BLX, read from its register: any taken
    # branch that is not a load costs 2 cycles [RP2350 datasheet
    # 3.7.4.9.7].
    fetchRegisterTargetAtDecode = True

    fetch2InputBufferSize = 2
    fetch2ToDecodeForwardDelay = 1
    fetch2CycleInput = True
    fetchFoldMasks = _FOLD_MASKS
    fetchFoldMatches = _FOLD_MATCHES
    fetchFoldKinds = _FOLD_KINDS

    # -- Decode stage --
    # One instruction a cycle, and with it one that folds into it.
    decodeInputBufferSize = 1
    decodeToExecuteForwardDelay = 1
    decodeInputWidth = 2
    decodeCycleInput = True

    # -- Execute stage --
    # PUSH, POP and the load and store multiples take one cycle for each
    # register [RP2350 datasheet 3.7.4.9.5]: the base register's update
    # has no cycle of its own. It reaches Execute beside the transfer
    # before it, the one case in which two micro-ops arrive together.
    executeInputWidth = 2
    executeFreeUpdateMicroops = True
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
    # One execute stage: a multi-cycle instruction (SDIV, VMLA, LDM, a load
    # the bus makes wait) holds up the next one, except that loads and
    # stores pipeline behind each other and VDIV/VSQRT complete beside the
    # pipeline (M33FloatDivFU).
    executeInOrderCompletion = True
    # A load takes one cycle: its address phase is its execute cycle, and
    # the instruction behind it executes in its data phase [RP2350
    # datasheet 3.7.4.9.5]. One that uses the loaded value waits a cycle
    # more (M33MemFU).
    executeLoadAccessAtIssue = True
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
