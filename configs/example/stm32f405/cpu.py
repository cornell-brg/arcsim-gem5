"""The Crazyflie's STM32F405 core: CortexM4CPU (tuned against the NUCLEO-G474RE) with the
timing the Crazyflie's own chip shows on arcsim-bench's micro families. Everything here
is an F405 option: CortexM4CPU and the G474 board are unchanged.

- Each access is made as aligned 32-bit transfers, a cycle each (MinorCPU's
  executeAlignedTransfers): LDRD and STRD take two, an unaligned word two or three.
- The floating-point units are CortexM4CPU's, with the result delays the Crazyflie shows:
  a cycle for VADD, VSUB, VMUL and VCVT, and for VMLA and VFMA (whose accumulator is
  exempt: the next one accumulating onto it is not held). VMOV to a core register has
  none (micro/fp_delay's vmov_use, its result used at once, takes vmov_skip's 59 cycles
  from flash and the same as vmov_skip in every setting). VMRS of the flags takes a cycle
  more, as a branch or IT on the flags right after it waits on the Crazyflie (vcmp_branch
  with prefetch off: 108 cycles).
- VMLA and VFMA keep the floating-point unit 3 cycles, and in those cycles no other
  floating-point instruction issues (FP_INST: any, VLDR and VSTR included, through MinorFU's
  busyHoldMask), while an integer instruction issues and completes beside them
  (resultAfterCommit, their result due as before): micro/fp_delay's vfma_int (40 cycles on
  the Crazyflie, 10 VFMAs each followed by an integer add, the same as 10 VFMAs alone), set
  beside vfma_skip (a float add after each waits: 50) and fp_busy's vfma_vldr and vfma_vstr
  (a float load or store after each waits).
- HALFWORD_WIDE_MEM: a 32-bit instruction after a 32-bit load or store two bytes into a
  word waits a cycle (MinorCPU's executeHalfwordWideMemCycles; every other time when it is
  itself a load or store, the first three after a branch free), as the Crazyflie's
  prefetch drains under such loads (arcsim-bench micro/unaligned's lw and lwh bodies).
- A taken branch whose target line is still on its way keeps that fetch (MinorCPU's
  fetchKeepTargetInFlight) rather than fetching the line again. From flash the line has
  always arrived (an ART hit answers at once); from SRAM, over the system bus, it never
  has, and the refetch cost every taken branch about 4 cycles the Crazyflie does not
  spend (micro/fetch br_back_0 from SRAM: 1010 cycles against the board's 608).
"""

from m5.objects.BaseMinorCPU import (
    MinorFU,
    MinorFUPool,
    MinorFUTiming,
    minorMakeOpClassSet,
)

from gem5.prebuilt.cortexm.cpu.cortex_m4 import (
    CortexM4CPU,
    M4FloatFU,
    M4IntAluFU,
    M4IntDivFU,
    M4IntMulFU,
    M4MemFU,
    M4MiscFU,
)
from gem5.prebuilt.cortexm.cpu.cortex_m33 import _T32, _VMOV_TO_CORE, _VMRS_FLAGS, _t32

HALFWORD_WIDE_MEM = True

# A floating-point instruction: a 32-bit coprocessor instruction (first halfword 1110 11xx)
# for coprocessor 10 or 11 (second halfword's bits 11:9 101), its loads and stores included
FP_INST = _t32(0xEC000E00, 0xEC000A00)
# ... a floating-point load or store (first halfword 1110 110x): VLDR, VSTR, VLDM, VSTM,
# VPUSH, VPOP
FP_MEM = _t32(0xFE000E00, 0xEC000A00)


def _ops(names):
    return minorMakeOpClassSet(names)


class F405FloatFU(M4FloatFU):
    timings = [
        MinorFUTiming(description="F405FToCore", srcRegsRelativeLats=[1], **_VMOV_TO_CORE),
        MinorFUTiming(description="F405FFlags", srcRegsRelativeLats=[1], extraCommitLat=1, **_VMRS_FLAGS),
        MinorFUTiming(
            description="F405FArith",
            srcRegsRelativeLats=[1],
            resultUsePenalty=1,
            opClasses=_ops(["FloatAdd", "FloatMult", "FloatCvt", "SimdFloatAdd", "SimdFloatMult", "SimdFloatCvt"]),
        ),
        MinorFUTiming(description="F405Float", srcRegsRelativeLats=[1]),
    ]


# Where a VMLA or VFMA (and VMLS, VNMLA, VNMLS, VFMS, VFNMA, VFNMS) reads each source, from
# issue, for MinorFUTiming's srcRegsRelativeLats: the accumulator (the first) two cycles,
# which its result's due time (issue + 5) meets when the next VFMA issues 3 cycles after it,
# and the others one, so a multiplied source waits that cycle more
_FMAC_SRC_LATS = [2, 1]


class F405FloatMacFU(MinorFU):
    """CortexM4CPU's M4FloatMacFU, its result delayed a cycle for every source but the
    accumulator, busy 3 cycles in which no other floating-point instruction issues, while
    integer ones issue and complete: it completes after a cycle, its result due 3 cycles
    after issue as before. A VFMA whose multiplied source is the VFMA before's result waits
    that cycle (fp_busy's vfma_cross: 87 cycles on the Crazyflie, 20 VFMAs; vadd_vfma)."""

    opClasses = _ops(["FloatMultAcc", "SimdMultAcc", "SimdFloatMultAcc"])
    opLat = 1
    issueLat = 3
    resultAfterCommit = True
    busyHoldMask = FP_INST["mask"]
    busyHoldMatch = FP_INST["match"]
    timings = [
        MinorFUTiming(
            description="F405FMac", srcRegsRelativeLats=_FMAC_SRC_LATS, resultUsePenalty=1, extraAssumedLat=2
        )
    ]


class F405FloatDivFU(MinorFU):
    """CortexM4CPU's M4FloatDivFU with a cycle of result delay, busy 14 cycles in which no
    other floating-point instruction issues, while integer ones do (fp_delay's vsqrt_skip:
    83 cycles on the Crazyflie against vsqrt_use's 88; fp_busy's vsqrt_int, vdiv_dep). As
    F405FloatMacFU, its result is due a cycle before the unit frees and its sources are read
    a cycle after issue, as the other floating-point units read theirs (vsqrt_use, vdiv_dep)."""

    opClasses = _ops(["FloatDiv", "FloatSqrt", "SimdFloatDiv", "SimdFloatSqrt"])
    opLat = 1
    issueLat = 14
    resultAfterCommit = True
    busyHoldMask = FP_INST["mask"]
    busyHoldMatch = FP_INST["match"]
    timings = [MinorFUTiming(description="F405FDiv", srcRegsRelativeLats=[1], extraAssumedLat=13, resultUsePenalty=1)]


class F405FUPool(MinorFUPool):
    funcUnits = [
        M4IntAluFU(),
        M4IntMulFU(),
        M4IntDivFU(),
        F405FloatFU(),
        F405FloatMacFU(),
        F405FloatDivFU(),
        M4MemFU(),
        M4MiscFU(),
    ]


class CrazyflieF405CPU(CortexM4CPU):
    executeFuncUnits = F405FUPool()
    fetchKeepTargetInFlight = True
    executeAlignedTransfers = True
    if HALFWORD_WIDE_MEM:
        executeHalfwordWideMemCycles = 1
        executeHalfwordWideMemGrace = 3
        executeWideInstMask = _T32
        executeWideInstMatch = _T32
