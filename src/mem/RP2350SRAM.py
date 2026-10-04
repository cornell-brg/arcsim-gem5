from m5.objects.AbstractMemory import AbstractMemory
from m5.params import *


class RP2350SRAM(AbstractMemory):
    """Clocked ten-bank SRAM with one 32-bit interface per connected port.

    Connect instruction and data managers separately: a crossbar that merges
    them into one port also merges their transfer limits and priorities.
    Larger packets consume consecutive word-boundary beats on their manager.
    This models SRAM service, not an AHB signal-level implementation.
    """

    type = "RP2350SRAM"
    cxx_header = "mem/rp2350_sram.hh"
    cxx_class = "gem5::memory::RP2350SRAM"
    range = AddrRange(0x20000000, size="520KiB")
    port = VectorResponsePort("Independent SRAM manager interfaces")
    response_cycles = Param.Cycles(1, "Cycles from bank grant to data completion")
    queue_depth = Param.Unsigned(8, "Outstanding packets per manager, including responses")
    port_priority = VectorParam.Unsigned([0], "Per-manager priority, 0 or 1; last entry repeats")
