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
    likewise for the soft IRQ state, bit n (core n's) driving soft_irq[n];
    reads of either register return the state. MTIME (+0x100) counts this
    device's clock cycles, and MTIMECMP0 (+0x108) and MTIMECMP1 (+0x110)
    drive timer_irq[0] and timer_irq[1] while mtime >= mtimecmp0 or
    mtimecmp1 (so from reset, when all are 0); each is read and written as
    two 32-bit halves. Bits of the IRQ states beyond the connected pins, and
    a comparator whose pin is not connected, are stored and drive nothing.

    Every other offset or access size is fatal.
    """

    type = "Hazard3TbIO"
    cxx_class = "gem5::Hazard3TbIO"
    cxx_header = "dev/rp2350/hazard3_tb_io.hh"

    pio_addr = 0xC0000000
    irq = VectorIntSourcePin("IRQ n: bit n of SET_IRQ/CLR_IRQ, in order")
    soft_irq = VectorIntSourcePin(
        "Core n's soft IRQ: bit n of SET/CLR_SOFTIRQ, in order (at most 2)")
    timer_irq = VectorIntSourcePin(
        "Core n's timer IRQ: mtime >= mtimecmpn, in order (at most 2)")
