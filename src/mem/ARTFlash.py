from m5.objects.SimpleMemory import SimpleMemory
from m5.params import *


class ARTFlash(SimpleMemory):
    """An STM32 flash behind its accelerator (ART), RM0090 3.5.2: one read at a time, each
    holding the flash for read_time. A data read waits for the read in progress and goes
    before a prefetch that has not started (D-Code before I-Code). An instruction miss
    drops the prefetch, read or not, the stream having moved (a miss costs the wait
    states)."""

    type = "ARTFlash"
    cxx_header = "mem/art_flash.hh"
    cxx_class = "gem5::memory::ARTFlash"

    read_time = Param.Latency("0ns", "How long one read holds the flash")
