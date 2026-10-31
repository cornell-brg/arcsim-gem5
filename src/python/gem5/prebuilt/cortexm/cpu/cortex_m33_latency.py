"""
The Cortex-M33's instruction latency table for the atomic CPU
(BaseAtomicSimpleCPU's inst_latency): an instruction-level simulator whose
time follows each instruction's cycles instead of one cycle a micro-op.

The classes are CortexM33CPU's, read from its functional units: the same
OpClasses and instruction patterns (MinorFUTiming's mask and match), so the
atomic CPU and CortexM33CPU class every instruction alike. Each class costs
the integer core 1 cycle plus its timing's extraCommitLat, has its
resultUsePenalty as a result delay and its srcRegsRelativeLats for which
sources a result delay holds (a multiply's accumulate operands from its
encoding instead), and occupies the floating-point unit, if it is one of
that unit's, for the unit's issueLat plus its extraIssueLat. The
folding patterns are CortexM33CPU's, and the branch rules the RP2350
datasheet's [3.7.4.9.7].

`overrides` sets a class's parameters by its description, e.g.
`{"M33Load": {"cycles": 1, "result_delay": 1}}`; other keyword arguments
set MProfileInstLatency's own (taken_branch, exception_entry, the memory
ranges ...).
"""

from m5.objects import (
    DynamicLatencyIntDivFU,
    MProfileInstLatency,
    MProfileLatencyClass,
)

from .cortex_m33 import (
    _T16,
    _T32,
    _FOLD_KINDS,
    _FOLD_MASKS,
    _FOLD_MATCHES,
    M33FPU,
    CortexM33FUPool,
    M33IntMulFU,
)

# The unit that MProfileInstLatency tracks for the floating-point unit
_FPU_UNIT = 0


def _op_class_names(op_class_set):
    return [str(c.opClass) for c in op_class_set.opClasses]


def cortex_m33_latency_classes(overrides=None):
    """CortexM33CPU's classes as MProfileLatencyClass objects, in the
    order its functional units would take an instruction."""
    overrides = dict(overrides or {})
    classes = []
    # Measured on a Pico 2 where the datasheet is silent, and timed by
    # CortexM33CPU's pipeline rather than its classes: ISB takes 2 cycles
    # as DSB and DMB do (bench's barrier family), and BLX to a register a
    # cycle more than BL (reg_branch's settled_blx and load_blx).
    for description, mask, match, params in (
        ("M33Isb", _T32 | 0xFFFFFFF0, _T32 | 0xF3BF8F60, {"cycles": 2}),
        ("M33BlxReg", _T32 | 0xFF87, _T16 | 0x4780, {"branch_extra": 1}),
    ):
        params.update(overrides.get(description, {}))
        classes.append(
            MProfileLatencyClass(
                description=description, mask=mask, match=match, **params
            )
        )
    for fu in CortexM33FUPool().funcUnits:
        fu_ops = _op_class_names(fu.opClasses)
        fpu = isinstance(fu, M33FPU)
        divide = isinstance(fu, DynamicLatencyIntDivFU)
        unit = _FPU_UNIT if fpu else -1

        def add(description, **params):
            params.update(overrides.get(description, {}))
            classes.append(
                MProfileLatencyClass(description=description, **params)
            )

        for t in fu.timings:
            src_lats = [int(l) for l in t.srcRegsRelativeLats]
            accumulate = isinstance(fu, M33IntMulFU)
            if accumulate:
                # The accumulate operands are found from the encoding: gem5
                # orders a multiply-accumulate's sources differently for
                # each instruction (MLA's accumulator last, MLS's first).
                src_lats = []
            add(
                t.description,
                op_classes=_op_class_names(t.opClasses) or fu_ops,
                mask=t.mask,
                match=t.match,
                cycles=1 + int(t.extraCommitLat),
                result_delay=int(t.resultUsePenalty),
                src_lats=src_lats,
                unit=unit,
                busy=int(fu.issueLat) + int(t.extraIssueLat),
                divide=divide,
                accumulate=accumulate,
            )
        # What the unit takes that none of its timings does
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
        raise ValueError(f"no Cortex-M33 latency class {sorted(unknown)}")
    return classes


def cortex_m33_inst_latency(overrides=None, **params):
    """An MProfileInstLatency for the Cortex-M33."""
    return MProfileInstLatency(
        classes=cortex_m33_latency_classes(overrides),
        fold_masks=_FOLD_MASKS,
        fold_matches=_FOLD_MATCHES,
        fold_kinds=_FOLD_KINDS,
        **params,
    )
