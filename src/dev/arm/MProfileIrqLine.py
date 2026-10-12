from m5.objects.MProfileSCS import MProfileSCS
from m5.params import *
from m5.SimObject import SimObject


class MProfileIrqLine(SimObject):
    """
    One external interrupt line of the M-profile NVIC (MProfileSCS) as an
    interrupt sink pin, for devices that signal through gem5's interrupt
    pins rather than calling the SCS. The line is level-sensitive
    (MProfileSCS::setIrqLevel).
    """

    type = "MProfileIrqLine"
    cxx_class = "gem5::MProfileIrqLine"
    cxx_header = "dev/arm/m_profile_irq_line.hh"

    scs = Param.MProfileSCS("M-profile SCS the line belongs to")
    # 0-based external IRQ index, as MProfileBridgeIO's irq_num.
    irq_num = Param.UInt32("External IRQ index (0..scs.num_irqs - 1)")
    pin = IntSinkPin("The line")
