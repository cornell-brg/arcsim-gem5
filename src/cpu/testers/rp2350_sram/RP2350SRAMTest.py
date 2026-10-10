from m5.objects.ClockedObject import ClockedObject
from m5.params import *
from m5.proxy import Parent


class RP2350SRAMTest(ClockedObject):
    """Deterministic protocol/data regression driver for RP2350SRAM."""
    type = "RP2350SRAMTest"
    cxx_header = "cpu/testers/rp2350_sram/rp2350_sram_test.hh"
    cxx_class = "gem5::RP2350SRAMTest"
    port = VectorRequestPort("Two independent test managers")
    scenario = Param.String("same", "Deterministic SRAM regression scenario")
    sram_window = Param.Latency("500ps", "The SRAM's window parameter")
    sram_latency = Param.Latency("1ns", "The SRAM's latency parameter")
    system = Param.System(Parent.any, "Test system")
