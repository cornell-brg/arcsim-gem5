"""The Crazyflie's STM32F405 core: CortexM4CPU (tuned against the NUCLEO-G474RE) with the
timing the Crazyflie's own chip shows on arcsim-bench's micro families. Everything here
is an F405 option: CortexM4CPU and the G474 board are unchanged.

- Each access is made as aligned 32-bit transfers, a cycle each (MinorCPU's
  executeAlignedTransfers): LDRD and STRD take two, an unaligned word two or three.
- The floating-point units are CortexM4CPU's, with the result delays the Crazyflie shows:
  a cycle for VADD, VSUB, VMUL and VCVT, for VMOV to a core register, and for VMLA and
  VFMA (whose accumulator is exempt: the next one accumulating onto it is not held), and
  VMRS of the flags a cycle more.
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
from gem5.prebuilt.cortexm.cpu.cortex_m33 import _T32, _VMOV_TO_CORE, _VMRS_FLAGS

HALFWORD_WIDE_MEM = True


def _ops(names):
    return minorMakeOpClassSet(names)


class F405FloatFU(M4FloatFU):
    timings = [
        MinorFUTiming(description="F405FToCore", srcRegsRelativeLats=[1], resultUsePenalty=1, **_VMOV_TO_CORE),
        MinorFUTiming(description="F405FFlags", srcRegsRelativeLats=[1], extraCommitLat=1, **_VMRS_FLAGS),
        MinorFUTiming(
            description="F405FArith",
            srcRegsRelativeLats=[1],
            resultUsePenalty=1,
            opClasses=_ops(["FloatAdd", "FloatMult", "FloatCvt", "SimdFloatAdd", "SimdFloatMult", "SimdFloatCvt"]),
        ),
        MinorFUTiming(description="F405Float", srcRegsRelativeLats=[1]),
    ]


class F405FloatMacFU(MinorFU):
    """CortexM4CPU's M4FloatMacFU, its result delayed a cycle for every source but the
    accumulator (the first)."""

    opClasses = _ops(["FloatMultAcc", "SimdMultAcc", "SimdFloatMultAcc"])
    opLat = 3
    issueLat = 3
    timings = [MinorFUTiming(description="F405FMac", srcRegsRelativeLats=[3, 2], resultUsePenalty=1)]


class F405FloatDivFU(MinorFU):
    """CortexM4CPU's M4FloatDivFU with a cycle of result delay."""

    opClasses = _ops(["FloatDiv", "FloatSqrt", "SimdFloatDiv", "SimdFloatSqrt"])
    opLat = 1
    issueLat = 14
    resultAfterCommit = True
    timings = [MinorFUTiming(description="F405FDiv", srcRegsRelativeLats=[0], extraAssumedLat=14, resultUsePenalty=1)]


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
