from m5.params import *
from m5.proxy import *
from m5.SimObject import SimObject


class Xh3power(SimObject):
    """
    Hazard3's h3.block and h3.unblock (Xh3power) for a system's cores: each
    core's sticky unblock request and the wiring of each core's unblock to
    the others. A RISC-V core has them when its ISA's xh3power parameter
    names this object; otherwise their encodings (slt x0, x0, x0 and
    slt x0, x0, x1) are the plain hints they are on other cores.

    h3.block sleeps as wfi does, until another core's h3.unblock or an
    interrupt that would wake a wfi (mip & mie). An h3.unblock to a core
    that is not sleeping on h3.block is remembered: its next h3.block falls
    through. Every release from h3.block, falling through or woken, clears
    the request. h3.unblock reaches every other core in hart_ids, never its
    own (arcsim-soc's soc.v cross-wires its two cores' unblock_out and
    unblock_in); a core alone has nothing to unblock. Both are illegal in
    U-mode with mstatus.TW set, as wfi.

    Not modelled: how many cycles the wake takes (the core wakes the tick
    the h3.unblock executes), msleep's power states.

    Reference: Hazard3 documentation, "Xh3power: Hazard3 power management",
    and hdl/hazard3_power_ctrl.v.
    """

    type = "Xh3power"
    cxx_class = "gem5::RiscvISA::Xh3power"
    cxx_header = "arch/riscv/xh3power.hh"

    system = Param.System(Parent.any, "The system whose cores these are")
    hart_ids = VectorParam.UInt32(
        "The cores' threads in the system, each core's h3.unblock reaching "
        "the others'"
    )
