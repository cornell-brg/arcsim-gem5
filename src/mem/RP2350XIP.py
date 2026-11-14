from m5.objects.AbstractMemory import AbstractMemory
from m5.params import *


class RP2350XIP(AbstractMemory):
    """Shared two-bank XIP cache and serialized, burst-aware QMI flash reads.

    Timing parameters describe the fixed flash operating point. Functional
    loader accesses neither warm the cache nor advance the serial interface.
    """
    type = "RP2350XIP"
    cxx_class = "gem5::memory::RP2350XIP"
    cxx_header = "mem/rp2350_xip.hh"
    range = AddrRange(0x10000000, size="16MiB")
    port = ResponsePort("Shared XIP instruction/data interface")
    hit_cycles = Param.Unsigned(1, "Cache hit service in system cycles")
    qmi_clkdiv = Param.Unsigned(3, "System cycles per SCK")
    qmi_prefix_cycles = Param.Unsigned(8, "Read command prefix SCK cycles")
    qmi_address_cycles = Param.Unsigned(6, "24-bit quad address SCK cycles")
    qmi_suffix_cycles = Param.Unsigned(2, "8-bit quad suffix SCK cycles")
    qmi_dummy_cycles = Param.Unsigned(4, "Quad read dummy SCK cycles")
    qmi_data_width = Param.Unsigned(4, "Read data bits per SCK")
    qmi_min_deselect = Param.Unsigned(7, "Additional system cycles CS inactive")
    qmi_sample_cycles = Param.Unsigned(2, "QMI sample/return pipeline cycles")
    qmi_cooldown_cycles = Param.Unsigned(64, "CS assertion idle timeout")
    qmi_pagebreak_bytes = Param.Unsigned(1024, "Burst boundary, zero disables")
    replacement_seed = Param.Unsigned(2350, "Deterministic random replacement seed")
