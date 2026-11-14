from m5.objects.Device import BasicPioDevice
from m5.params import *


class RP2350XipCtrl(BasicPioDevice):
    """
    RP2350 XIP cache controls and saturating access/hit counters. Atomic
    XOR/SET/CLR aliases operate on the attached shared cache endpoint.
    With no endpoint attached, retains the legacy register stub.
    """

    type = "RP2350XipCtrl"
    cxx_class = "gem5::RP2350XipCtrl"
    cxx_header = "dev/rp2350/rp2350_xip_ctrl.hh"

    pio_addr = 0x400C8000
    pio_size = Param.Addr(0x8000, "Size of the register window (the slot)")
    xip = Param.RP2350XIP(NULL, "Shared XIP cache endpoint, optional in legacy mode")
