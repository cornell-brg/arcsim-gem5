from m5.objects.Device import BasicPioDevice
from m5.params import *


class Hazard3TbIO(BasicPioDevice):
    """
    The Hazard3 testbench IO block (tb_cxxrtl_io.h, rtl-sim's periph_minion),
    so the same firmware ELF runs on rtl-sim and gem5. Not an RP2350 block.

    Models PRINT_CHAR (+0x00), PRINT_U32 (+0x04) and EXIT (+0x08): writes print
    to stdout and the first EXIT write ends the simulation with its value as
    the exit code.

    Also models, as rtl-sim does, the interrupt registers and the timer:
    SET_IRQ (+0x20) and CLR_IRQ (+0x30) set and clear bits of a 32-bit IRQ
    state, bit n driving irq[n]; SET_SOFTIRQ (+0x10) and CLR_SOFTIRQ (+0x14)
    likewise for the soft IRQ state, bit 0 (core 0) driving soft_irq; reads
    of either register return the state. MTIME (+0x100) counts this device's
    clock cycles and MTIMECMP0 (+0x108) drives timer_irq while
    mtime >= mtimecmp0 (so from reset, when both are 0); each is read and
    written as two 32-bit halves. MTIMECMP1 (core 1's) is stored and read back
    and drives nothing. Bits of the IRQ states beyond the connected pins are
    stored and drive nothing.

    Every other offset or access size is fatal.
    """

    type = "Hazard3TbIO"
    cxx_class = "gem5::Hazard3TbIO"
    cxx_header = "dev/rp2350/hazard3_tb_io.hh"

    pio_addr = 0xC0000000
    irq = VectorIntSourcePin("IRQ n: bit n of SET_IRQ/CLR_IRQ, in order")
    soft_irq = IntSourcePin("Core 0's soft IRQ: bit 0 of SET/CLR_SOFTIRQ")
    timer_irq = IntSourcePin("Core 0's timer IRQ: mtime >= mtimecmp0")
