from m5.objects.AbstractMemory import AbstractMemory
from m5.params import *


class RP2350SRAM(AbstractMemory):
    """Clocked ten-bank SRAM with one 32-bit interface per connected port.

    Connect instruction and data managers separately: a crossbar that merges
    them into one port also merges their transfer limits and priorities.
    Larger packets consume consecutive word-boundary beats on their manager.
    This models SRAM service, not an AHB signal-level implementation.

    The banks have zero wait states, as the RP2350's (datasheet 2.1.1): a
    bank serves one manager per clock cycle. A beat is granted in the cycle
    it is requested and answered within that cycle, unless the bank has
    served another manager, when it waits a cycle. Without contention an
    access costs what it does on a plain memory of the same latency.

    Managers that ask in the same cycle are chosen between as if at once:
    the banks choose `window` after the clock edge, by when a request
    issued at the edge has crossed the routers in front of this memory.
    """

    type = "RP2350SRAM"
    cxx_header = "mem/rp2350_sram.hh"
    cxx_class = "gem5::memory::RP2350SRAM"
    range = AddrRange(0x20000000, size="520KiB")
    port = VectorResponsePort("Independent SRAM manager interfaces")
    window = Param.Latency("500ps", "After a clock edge, how long requests still count as made at it")
    latency = Param.Latency("1ns", "From a bank's grant to the data, within the clock cycle")
    queue_depth = Param.Unsigned(8, "Outstanding packets per manager, including responses")
    port_priority = VectorParam.Unsigned([0], "Per-manager priority, 0 or 1; last entry repeats")
