from m5.objects.ClockedObject import ClockedObject
from m5.params import *
from m5.proxy import *
from m5.SimObject import SimObject


class Xh3irq(SimObject):
    """
    Hazard3's external interrupt controller (Xh3irq), one per core. A RISC-V
    core has it when its ISA's xh3irq parameter names it; the CSRs h3.meiea,
    h3.meipa, h3.meifa, h3.meipra, h3.meinext and h3.meicontext then read and
    write it, and it drives the core's mip.meip. Interrupt lines reach it
    through Xh3irqLine.

    Each array CSR is a 16-bit window (bits 31:16) into an array, chosen by
    the low bits of the source operand of the same CSR instruction (5 bits,
    7 for h3.meipra). An interrupt is pending when its line is high or it is
    forced (h3.meifa), and mip.meip is set while a pending, enabled (h3.meiea)
    interrupt has a priority (h3.meipra) at or above meicontext.preempt.

    h3.meinext shows the highest-priority pending, enabled interrupt at or
    above meicontext.ppreempt (lowest number first among equals); reading it
    clears that interrupt's force bit, and writing its update bit takes it
    into h3.meicontext. Entry to the external interrupt pushes the preemption
    priority stack (preempt, ppreempt, pppreempt) and sets mreteirq; mret
    pops it when mreteirq is set. meicontext.clearts clears and saves
    mie.mtie and mie.msie.

    Not modelled: how many cycles any of this takes.

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


class Xh3irqLine(ClockedObject):
    """
    One external interrupt line of an Xh3irq as an interrupt sink pin, for
    devices that signal through gem5's interrupt pins. The line is
    level-sensitive. Each edge reaches the controller at the delay-th rising
    edge of the line's clock after it arrives, as through that many
    registers: Hazard3 registers each IRQ input once in the core clock
    (hazard3_irq_ctrl.v, unless IRQ_INPUT_BYPASS), so the default is 1.
    """

    type = "Xh3irqLine"
    cxx_class = "gem5::RiscvISA::Xh3irqLine"
    cxx_header = "arch/riscv/xh3irq.hh"

    controller = Param.Xh3irq("The controller the line belongs to")
    irq_num = Param.UInt32("External interrupt number (0..num_irqs - 1)")
    delay = Param.Cycles(1, "Register stages between the pin and the controller")
    pin = IntSinkPin("The line")
