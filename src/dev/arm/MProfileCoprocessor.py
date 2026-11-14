from m5.params import *
from m5.proxy import *
from m5.SimObject import SimObject


class MProfileCoprocessor(SimObject):
    """
    A coprocessor on a Cortex-M core's coprocessor port, answering for one or
    more of the numbers p0-p7. MCR, MRC, MCRR, MRRC and CDP instructions for
    those numbers reach it once CPACR grants access; instructions for a number
    with no coprocessor attached raise a UsageFault (NOCP), as on silicon.

    Used directly, it stands for a coprocessor that is present but not
    modelled: any access stops the simulation.
    """

    type = "MProfileCoprocessor"
    cxx_class = "gem5::MProfileCoprocessor"
    cxx_header = "dev/arm/m_profile_coprocessor.hh"

    system = Param.ArmMSystem(Parent.any, "System whose cores it serves")
    numbers = VectorParam.Unsigned("Coprocessor numbers (0-7) it answers")
