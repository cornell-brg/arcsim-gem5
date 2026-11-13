from m5.objects.Device import BasicPioDevice
from m5.params import *


class RP2350XipCtrl(BasicPioDevice):
    """
    The RP2350's XIP_CTRL cache counters (datasheet 4.4.5), as arcsim-soc's
    rtl/xip/xipCtrl.sv has them: CTR_HIT at 0x0c and CTR_ACC at 0x10, 32-bit
    counters that saturate and that a write to them clears. Nothing counts
    the XIP cache's hits and accesses here: every offset, the counters
    included, reads 0 and ignores writes.
    """

    type = "RP2350XipCtrl"
    cxx_class = "gem5::RP2350XipCtrl"
    cxx_header = "dev/rp2350/rp2350_xip_ctrl.hh"

    pio_addr = 0x400C8000
    pio_size = Param.Addr(0x8000, "Size of the register window (the slot)")
