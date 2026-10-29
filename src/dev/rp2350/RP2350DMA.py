from m5.objects.Device import BasicPioDevice
from m5.params import *
from m5.util.pybind import PyBindMethod


class RP2350DMA(BasicPioDevice):
    """
    RP2350 DMA controller, shared by the Arm and Hazard3 cores.

    Models the 16 channels' READ_ADDR, WRITE_ADDR, TRANS_COUNT and CTRL (with
    their three alias rows and trigger registers), INTR, INTE/INTF/INTS for the
    four interrupt outputs, MULTI_CHAN_TRIGGER, FIFO_LEVELS, CHAN_ABORT and
    N_CHANNELS, each also through the atomic XOR, SET and CLR aliases. A
    channel transfers 1, 2 or 4 bytes at a time with incrementing or fixed
    addresses, a ring on either address, the normal, self-triggering and
    endless count modes, chaining, pacing by a peripheral's data request line
    (dreq()) or none, and bus errors.

    As on the chip (datasheet figure 122), an address generator takes the next
    channel round robin and puts the transfer into the read and write address
    FIFOs (RAF, WAF); the read manager reads into the transfer data FIFO
    (TDF); the write manager writes from it. One read and one write leave on
    the two manager ports each cycle, so the DMA competes for the bus like the
    chip's. An answer ends the access's data phase in the next cycle, as on
    AHB-Lite, so a write runs two transfers behind its read; arcsim-soc's RTL
    (rtl/dma/dmaController.sv) times transfers the same way.

    Where a Pico 2 was measured, the model follows it: a count of 0 runs a
    null transfer (busy for one transfer's time, no bus access); READ_ADDR,
    WRITE_ADDR and TRANS_COUNT follow the address generator; after a bus error
    the channel makes no further access and its registers end up showing the
    transfers written (a faulting write counted); an abort lets the transfers
    already generated complete, the addresses reading as those written until
    they have; a channel counts the pulses on its line while it waits, and
    starting it (any trigger, or a write to its DBG_CTDREQ) clears the count,
    so a channel chained after another on the same line starts at the
    peripheral's pace. A peripheral model registered with addDreqSource() is
    told when that happens, to send again what it holds. DBG_CTDREQ and
    DBG_TCR are modelled.

    Not modelled: the CRC sniffer, the pacing timers, byte swapping,
    decrementing addresses, channel priority, and the security and MPU
    registers. Their registers read 0 and ignore writes, with a warning the
    first time one is touched.

    Reference: RP2350 datasheet, section 12.6 (DMA).
    """

    type = "RP2350DMA"
    cxx_class = "gem5::RP2350DMA"
    cxx_header = "dev/rp2350/rp2350_dma.hh"

    pio_addr = 0x50000000
    read_port = RequestPort("The read manager")
    write_port = RequestPort("The write manager")
    irq = VectorIntSourcePin("DMA_IRQ_0 to DMA_IRQ_3, in order")
    # The depths FIFO_LEVELS shows on a Pico 2 (table 1151's bound on
    # READ_ADDR after a read error gives the RAF's).
    raf_depth = Param.Unsigned(3, "Read address FIFO entries")
    waf_depth = Param.Unsigned(4, "Write address FIFO entries")
    tdf_depth = Param.Unsigned(4, "Transfer data FIFO entries")

    cxx_exports = [PyBindMethod("dreq")]
