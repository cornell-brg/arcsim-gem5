from m5.objects.AbstractMemory import AbstractMemory
from m5.params import *


class RP2350XIP(AbstractMemory):
    """The RP2350's flash execute-in-place path: its XIP cache in front of
    the QSPI memory interface (QMI), holding the flash contents.

    The cache is 16 KiB, two-way, of 8-byte lines (datasheet 4.4.1). A hit
    answers at the next clock edge. A miss reads its whole line from flash
    as one serial transfer and takes a way at random. The lines of even and
    odd line addresses are two banks: while a bank waits for a line from
    flash, every other access to that bank waits with it, and the other
    bank goes on answering hits. When the line has come the bank gives
    those that waited one 32-bit word a cycle, the bus managers taking
    turns, and a manager with an access waiting makes no other: so a core
    that asks for whole lines, several ahead, gets them at the rate of one
    that fetches a word at a time. Accesses that never wait are answered
    whole, any number a cycle.

    The QMI makes one transfer at a time, in the order they were asked for.
    A transfer whose line follows the last one's in address order, while
    the chip select is still held, only clocks out the data
    (`chained_cycles`); any other first sends the command and address
    (`miss_cycles`), after `reselect_cycles` more if the chip select was
    held or had only just been released. The chip select is held for
    `cooldown_cycles` after a transfer and released at a `page_break`
    boundary. The defaults are a Pico 2's with the Pico SDK's settings
    (M0_TIMING 0x60007203, M0_RFMT 0x000492a8), as measured on one.

    Cache maintenance writes (the window at `maintenance_base`) invalidate
    lines; pinning and cleaning are not modelled.
    """

    type = "RP2350XIP"
    cxx_header = "mem/rp2350_xip.hh"
    cxx_class = "gem5::memory::RP2350XIP"
    port = ResponsePort("The XIP path's bus interface")
    maintenance_base = Param.Addr(
        0x18000000, "Base of the cache maintenance window"
    )
    miss_cycles = Param.Cycles(
        110, "A miss, from a released chip select, over a hit"
    )
    chained_cycles = Param.Cycles(
        50, "A miss that continues the last transfer, over a hit"
    )
    reselect_cycles = Param.Cycles(
        10, "Added to a miss that must first end the last transfer"
    )
    cooldown_cycles = Param.Cycles(
        64, "How long the chip select is held after a transfer"
    )
    page_break = Param.MemorySize(
        "1KiB", "Boundary at which a transfer ends and the chip select is"
        " released; 0 for none"
    )
