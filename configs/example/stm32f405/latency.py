"""The STM32F405's instruction latency table for gem5's atomic CPU (its inst_latency,
MProfileInstLatency): an instruction-level simulator timed by CrazyflieF405CPU's classes.

    f405_inst_latency(overrides=None, **params) -> MProfileInstLatency

The classes are CrazyflieF405CPU's, read from its functional units as cortex_m33_latency
reads CortexM33CPU's: each MinorFUTiming (and, after a unit's timings, what the unit takes
that none of them does) costs 1 cycle plus its extraCommitLat, has its resultUsePenalty as
a result delay and its srcRegsRelativeLats for which sources a result delay holds, and the
floating-point units (one for VADD and the like, one for VMLA and VFMA, one for VDIV and
VSQRT, as in the CL model) are busy for their issueLat plus extraIssueLat. A divide's
cycles come from its larger operand, as CortexM4CPU's divider. An IT takes no cycle before a 16-bit instruction, as on the
Crazyflie; nothing else folds (CortexM4CPU has no fold patterns).

`overrides` sets a class's parameters by its description, e.g. `{"M4Mem": {"result_delay":
1}}`; other keyword arguments set MProfileInstLatency's own (the memory ranges ...).
"""

from m5.objects import (
    DynamicLatencyIntDivFU,
    MProfileInstLatency,
    MProfileLatencyClass,
)

from gem5.prebuilt.cortexm.cpu.cortex_m33 import _T16, _T32, _fold

from cpu import F405FloatDivFU, F405FloatFU, F405FloatMacFU, F405FUPool  # noqa: E402

# The units MProfileInstLatency tracks: the floating-point units, as the CL model's
_UNITS = {F405FloatFU: 0, F405FloatMacFU: 1, F405FloatDivFU: 2}

# What the CL model times by its pipeline rather than its classes, from the Cortex-M4's
# timing [DDI0439D table 3-1, 3.3.2] and the Crazyflie's micro families:
# - ISB: 1 + B, the pipeline refilled: 3 cycles (barrier's isb_only);
# - BX and BLX to a register: a cycle more than a direct branch, the target known only
#   in execute (reg_branch's bx_fresh, bx_settled, load_blx), except BX LR, a return,
#   which costs a direct branch's (call_ret, empty);
# - a load or store (M4Mem): an address phase before its transfers (ldm4, stm4, ldrd,
#   strd: 1 + N), which a single load after a load or store overlaps and a single store
#   never pays (load, store: 1 each), and its result a cycle after its transfer
#   (load_dep: 2 each);
# - VMLA and VFMA (F405FMac): 3 cycles that hold the next instruction, as the CL model's
#   in-order completion and the TRM's 3 cycles [table 7-1] (fp_busy's vfma_vldr and
#   vfma_vstr; kalman -24 % to -10 %, the held-out set unchanged).
_PIPELINE_CLASSES = (
    ("F405Isb", _T32 | 0xFFFFFFF0, _T32 | 0xF3BF8F60, {"cycles": 3}),
    ("F405BxLr", _T32 | 0xFFFF, _T16 | 0x4770, {}),
    ("F405BxReg", _T32 | 0xFF07, _T16 | 0x4700, {"branch_extra": 1}),
)
_DEFAULTS = {"M4Mem": {"address_phase": 1, "result_delay": 1}, "F405FMac": {"cycles": 3}}
# An IT takes no cycle before a 16-bit instruction (fold's it_same and it_cross); nothing
# else folds.
_IT_FOLDS = _fold([(0xFF08, 0xBF08, 2), (0xFF04, 0xBF04, 2), (0xFF02, 0xBF02, 2), (0xFF01, 0xBF01, 2)])


def _op_class_names(op_class_set):
    return [str(c.opClass) for c in op_class_set.opClasses]


def f405_latency_classes(overrides=None):
    """CrazyflieF405CPU's classes as MProfileLatencyClass objects, in the order its
    functional units would take an instruction."""
    overrides = dict(overrides or {})
    classes = []

    def add(description, **params):
        params.update(_DEFAULTS.get(description, {}))
        params.update(overrides.get(description, {}))
        classes.append(MProfileLatencyClass(description=description, **params))

    for description, mask, match, params in _PIPELINE_CLASSES:
        add(description, mask=mask, match=match, **params)

    for fu in F405FUPool().funcUnits:
        fu_ops = _op_class_names(fu.opClasses)
        unit = _UNITS.get(type(fu), -1)
        divide = isinstance(fu, DynamicLatencyIntDivFU)
        for t in fu.timings:
            add(
                t.description,
                op_classes=_op_class_names(t.opClasses) or fu_ops,
                mask=t.mask,
                match=t.match,
                cycles=1 + int(t.extraCommitLat),
                result_delay=int(t.resultUsePenalty),
                src_lats=[int(l) for l in t.srcRegsRelativeLats],
                unit=unit,
                busy=int(fu.issueLat) + int(t.extraIssueLat),
                divide=divide,
            )
        add(
            type(fu).__name__,
            op_classes=fu_ops,
            cycles=1,
            unit=unit,
            busy=int(fu.issueLat),
            divide=divide,
        )
    unknown = set(overrides) - {c.description for c in classes}
    if unknown:
        raise ValueError(f"no F405 latency class {sorted(unknown)}")
    return classes


def f405_inst_latency(overrides=None, **params):
    """An MProfileInstLatency for the Crazyflie's STM32F405."""
    params.setdefault("divide_by_size", True)
    masks, matches, kinds = _IT_FOLDS
    return MProfileInstLatency(
        classes=f405_latency_classes(overrides),
        fold_masks=masks,
        fold_matches=matches,
        fold_kinds=kinds,
        **params,
    )
