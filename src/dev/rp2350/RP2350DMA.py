from m5.objects.Device import BasicPioDevice
from m5.params import *
from m5.util.pybind import PyBindMethod


class RP2350DMA(BasicPioDevice):
    """
    RP2350 DMA controller, shared by the Arm and Hazard3 cores.

    Models the 16 channels' READ_ADDR, WRITE_ADDR, TRANS_COUNT and CTRL (with
    their three alias rows and trigger registers), INTR, INTE/INTF/INTS for the
    four interrupt outputs, MULTI_CHAN_TRIGGER, CHAN_ABORT and N_CHANNELS, each
    also through the atomic XOR, SET and CLR aliases. A channel transfers 1, 2
    or 4 bytes at a time with incrementing or fixed addresses, a ring on either
    address, the normal, self-triggering and endless count modes, chaining, and
    pacing by a peripheral's data request line (dreq()) or none.

    One read and one write leave on the two manager ports each clock cycle,
    round-robin over the channels that have a transfer to make, so the DMA
    competes for the bus like the chip's.

    Not modelled: the CRC sniffer, the pacing timers, byte swapping, decrementing
    addresses, channel priority, bus errors, the security and MPU registers and
    the debug registers. Their registers read 0 and ignore writes, with a
    warning the first time one is touched.

    Reference: RP2350 datasheet, section 12.6 (DMA).
    """

    type = "RP2350DMA"
    cxx_class = "gem5::RP2350DMA"
    cxx_header = "dev/rp2350/rp2350_dma.hh"

    pio_addr = 0x50000000
    read_port = RequestPort("The read manager")
    write_port = RequestPort("The write manager")
    irq = VectorIntSourcePin("DMA_IRQ_0 to DMA_IRQ_3, in order")
    window = Param.Latency(
        "500ps",
        "After a clock edge, how long a read's answer still counts as having"
        " come at it, so that the next read leaves in the same cycle",
    )

    cxx_exports = [PyBindMethod("dreq")]
