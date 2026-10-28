from m5.objects.AbstractMemory import AbstractMemory
from m5.params import *


class HCITcdm(AbstractMemory):
    """
    The HWPE's TCDM as arcsim-soc's soc.v builds it: word-interleaved SRAM
    banks shared by the core side and the HWPE.

    Banks: num_banks banks of 32-bit words; bank = word address modulo
    num_banks. A bank serves one access per cycle and always accepts it
    (hci_sram); read data comes one cycle later (a synchronous SRAM).

    Core side (core_port, the ahbl_to_hci bridges): each AHB transfer is
    one word on one bank. There is one bridge per word address modulo
    num_bridges, each serving one transfer at a time; between core ports,
    the lower index goes first, as on the AHB crossbar. A write is answered
    in the cycle it is granted. A read is answered one cycle later: a core
    read of the TCDM has one wait state more than one of the SRAM. A packet
    wider than a word (an 8-byte instruction fetch) is made one word at a
    time, as consecutive transfers: the next word competes two cycles after
    a read's grant, one after a write's, and the packet is answered after
    its last word.

    HWPE side (hwpe_port, hci_hwpe_interconnect): every access requests
    hwpe_words consecutive banks, from the bank of its address on and
    whatever its size (the RTL requests all of them regardless of the byte
    enables). It is granted only if all of them grant it in the same cycle.
    Reads and writes are answered one cycle after the grant (r_valid).

    Arbitration (hci_shallow_interconnect, hwpe_prio = 0): the core side
    has priority on every bank it requests, so the HWPE loses a cycle when
    the core requests any one of its banks. Starvation guard: after
    low_prio_max_stall consecutive cycles in which both sides request
    something (on any banks), the core side is masked for one cycle and
    gets no grant. 0 disables the guard.

    Each port holds one request waiting for its grant, as the RTL holds
    req until gnt; a further request is refused until then and retried
    after the grant. Requests that arrive within window after a clock edge
    compete in that cycle; later ones in the next.

    Timing in gem5 terms: a core request competes in the cycle it arrives
    in. In the RTL the bridge requests the bank one cycle after the AHB
    address phase, but the core model's response time stands for the data
    phase, so the cycle of arrival plays the part of the bridge's request
    cycle.
    """

    type = "HCITcdm"
    cxx_class = "gem5::HCITcdm"
    cxx_header = "dev/hwpe/hci_tcdm.hh"

    range = AddrRange(0x60000000, size="128KiB")
    core_port = VectorResponsePort("Core side (ahbl_to_hci bridges)")
    hwpe_port = ResponsePort("The HWPE's TCDM port")

    num_banks = Param.Unsigned(16, "Word-interleaved banks")
    num_bridges = Param.Unsigned(
        4, "Core-side bridges, one per word address modulo num_bridges"
    )
    hwpe_words = Param.Unsigned(
        9, "Banks each HWPE access requests (288-bit port: 9 words)"
    )
    low_prio_max_stall = Param.Unsigned(
        8,
        "Cycles of requests on both sides after which the core side is "
        "masked for one cycle (0: never)",
    )
    window = Param.Latency(
        "500ps",
        "After a clock edge, how long requests still compete in its cycle",
    )
    latency = Param.Latency(
        "1ns", "From the clock edge of the answer's cycle to the answer"
    )
