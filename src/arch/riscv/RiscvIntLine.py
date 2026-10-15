from m5.objects.ClockedObject import ClockedObject
from m5.params import *
from m5.proxy import *


class RiscvIntLine(ClockedObject):
    """
    A level-sensitive interrupt line into one bit of a RISC-V core's mip (for
    example 3 for msip, 7 for mtip), as an interrupt sink pin: raising the pin
    sets the bit, lowering it clears it. For devices that signal a core's
    standard interrupts through gem5's interrupt pins. Each edge reaches the
    core at the delay-th rising edge of the line's clock after it arrives, as
    through that many registers: Hazard3 registers its soft and timer IRQ
    inputs once in the core clock (hazard3_csr.v), so the default is 1.
    """

    type = "RiscvIntLine"
    cxx_class = "gem5::RiscvISA::RiscvIntLine"
    cxx_header = "arch/riscv/int_line.hh"

    system = Param.System(Parent.any, "The system whose core this is")
    hart_id = Param.UInt32(0, "The core's thread in the system")
    int_num = Param.UInt32("The mip bit the line drives")
    delay = Param.Cycles(1, "Register stages between the pin and the core")
    pin = IntSinkPin("The line")
