from m5.objects.MProfileCoprocessor import MProfileCoprocessor
from m5.params import *
from m5.proxy import *


class RP2350GpioCoprocessor(MProfileCoprocessor):
    """
    The RP2350's GPIO coprocessor (p0): the SIO GPIO registers from the
    Cortex-M33 pipeline. Implements the instruction set of RP2350 datasheet
    section 3.6.1 on the given SIO's GPIO state.
    """

    type = "RP2350GpioCoprocessor"
    cxx_class = "gem5::RP2350GpioCoprocessor"
    cxx_header = "dev/arm/rp2350_coprocessors.hh"

    numbers = [0]
    sio = Param.RP2350SIO("SIO whose GPIO registers it accesses")


class RP2350DcpCoprocessor(MProfileCoprocessor):
    """
    The RP2350's double-precision coprocessor (DCP, p4 and p5), only as far as
    the SDK's start-up uses it: the RCMP status read, which returns 0. Any
    other DCP instruction stops the simulation.
    """

    type = "RP2350DcpCoprocessor"
    cxx_class = "gem5::RP2350DcpCoprocessor"
    cxx_header = "dev/arm/rp2350_coprocessors.hh"

    numbers = [4, 5]
