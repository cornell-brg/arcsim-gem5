from m5.params import *
from m5.proxy import *
from m5.SimObject import SimObject


class Xh3irq(SimObject):
    """
    Hazard3's external interrupt controller (Xh3irq), one per core. A RISC-V
    core has it when its ISA's xh3irq parameter names it; the CSRs h3.meiea,
    h3.meipa, h3.meifa and h3.meipra then read and write it, and it drives the
    core's mip.meip. Interrupt lines reach it through Xh3irqLine.

    Each array CSR is a 16-bit window (bits 31:16) into an array, chosen by
    the low bits of the source operand of the same CSR instruction (5 bits,
    7 for h3.meipra). An interrupt is pending when its line is high or it is
    forced (h3.meifa), and mip.meip is set while any pending interrupt is
    enabled (h3.meiea).

    Not modelled yet: priority-based preemption, h3.meinext and
    h3.meicontext; priorities are stored and read back, nothing more.

    Reference: Hazard3 documentation, "Xh3irq: Hazard3 interrupt controller",
    and hdl/hazard3_irq_ctrl.v.
    """

    type = "Xh3irq"
    cxx_class = "gem5::RiscvISA::Xh3irq"
    cxx_header = "arch/riscv/xh3irq.hh"

    system = Param.System(Parent.any, "The system whose core this is")
    hart_id = Param.UInt32(0, "The core's thread in the system")
    num_irqs = Param.UInt32(52, "Number of external interrupts (at most 512)")
    priority_bits = Param.UInt8(4, "Implemented priority bits (1 to 4)")


class Xh3irqLine(SimObject):
    """
    One external interrupt line of an Xh3irq as an interrupt sink pin, for
    devices that signal through gem5's interrupt pins. The line is
    level-sensitive.
    """

    type = "Xh3irqLine"
    cxx_class = "gem5::RiscvISA::Xh3irqLine"
    cxx_header = "arch/riscv/xh3irq.hh"

    controller = Param.Xh3irq("The controller the line belongs to")
    irq_num = Param.UInt32("External interrupt number (0..num_irqs - 1)")
    pin = IntSinkPin("The line")
