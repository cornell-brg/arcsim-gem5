from m5.objects.Device import BasicPioDevice
from m5.params import *


class RP2350SIO(BasicPioDevice):
    """
    RP2350 single-cycle I/O block (SIO), shared by the Arm and Hazard3 cores.

    Models CPUID and the GPIO registers: GPIO_OUT, GPIO_OE (with their SET, CLR
    and XOR aliases) and GPIO_IN, for GPIO 0-31 and the high bank (GPIO 32-47 and
    the QSPI/USB pins). The Cortex-M33s' GPIO coprocessor (RP2350GpioCoprocessor)
    acts on the same state. GPIO_IN has no pads behind it: a pin reads the value it
    drives while its output is enabled, otherwise 0. Every other SIO register
    reads 0 and ignores writes, with a warning the first time it is touched.

    Reference: RP2350 datasheet, section 3.1 (SIO).
    """

    type = "RP2350SIO"
    cxx_class = "gem5::RP2350SIO"
    cxx_header = "dev/rp2350/rp2350_sio.hh"

    pio_addr = 0xD0000000
