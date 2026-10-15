from m5.objects.Device import BasicPioDevice
from m5.params import *


class Hazard3TbIO(BasicPioDevice):
    """
    The Hazard3 testbench IO block (tb_cxxrtl_io.h, rtl-sim's periph_minion),
    so the same firmware ELF runs on rtl-sim and gem5. Not an RP2350 block.

    Models PRINT_CHAR (+0x00), PRINT_U32 (+0x04) and EXIT (+0x08): writes print
    to stdout and the first EXIT write ends the simulation with its value as
    the exit code. The soft IRQ, IRQ and timer registers, and every other
    offset or access size, are fatal.
    """

    type = "Hazard3TbIO"
    cxx_class = "gem5::Hazard3TbIO"
    cxx_header = "dev/rp2350/hazard3_tb_io.hh"

    pio_addr = 0xC0000000
