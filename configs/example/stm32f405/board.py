"""The Crazyflie 2.1's STM32F405 as a gem5 board: CortexM4CPU (the G474's core,
Cortex-M4 r0p1 on both), the F405's flash accelerator (ART) in each of its four
settings, SRAM on the system bus and CCM on the data bus only.

    make_board(firmware, caches="full", clock_mhz=168, flash_ws=5) -> ArmMSystem

caches names FLASH_ACR's bits as arcsim-bench does: full (ICEN DCEN PRFTEN),
noprefetch (ICEN DCEN), nocache (PRFTEN), none. Peripherals beyond the SCS and
the DWT are the caller's: attach them to board.system_bus.mem_side_ports.

The accelerator (RM0090 section 3.4): flash reads 128 bits; the instruction cache
holds 64 lines and the data cache 8, each of 128 bits, replaced least recently
used once all are full; one line is prefetched ahead of the line in use; data
reads have priority over instruction fetches. With every bit off, a flash read
still serves the rest of its 128-bit line to the fetches that follow it (RM0090
figure 5). CCM is outside the bus matrix, on the core's data bus only: an
instruction fetch from it finds nothing.

The buses, the routers and the SRAM are the STM32G474RETimingBoard's
(src/python/gem5/prebuilt/cortexm/boards/stm32g474re_board.py), whose
latencies were tuned against a NUCLEO-G474RE; the flash latencies start from
that board's, scaled by wait states, and are tuned against the Crazyflie.
"""

from m5.objects import (
    ArmMSystem,
    BadAddr,
    NoncoherentXBar,
    PipelinedSimpleMemory,
    SimpleMemory,
    SrcClockDomain,
    VoltageDomain,
)
from m5.objects.ArmMFsWorkload import ArmMFsWorkload
from m5.objects.Cache import ARTCache, NoncoherentCache
from m5.objects.ReplacementPolicies import LRURP
from m5.params import AddrRange

from gem5.prebuilt.cortexm.cpu.cortex_m4 import CortexM4CPU
from gem5.prebuilt.cortexm.platforms import STM32F405Platform

CACHES = {
    # setting: (instruction and data caches, prefetch)
    "full": (True, True),
    "noprefetch": (True, False),
    "nocache": (False, True),
    "none": (False, False),
}

FLASH = AddrRange(0x08000000, size="1MiB")
SRAM = AddrRange(0x20000000, size="128KiB")  # SRAM1 and SRAM2, one span
CCM = AddrRange(0x10000000, size="64KiB")

LINE = 16  # bytes in a flash read and in a cache line


def _xbar(clk_domain, width):
    bus = NoncoherentXBar(
        frontend_latency=0,
        forward_latency=0,
        response_latency=0,
        header_latency=0,
        width=width,
        clk_domain=clk_domain,
    )
    return bus


def _with_badaddr(bus):
    bus.badaddr_responder = BadAddr()
    bus.default = bus.badaddr_responder.pio
    return bus


def flash_latency_ps(caches, clock_mhz, flash_ws):
    """The flash's read latency. Behind the accelerator, a little under the wait
    states (the G474's 23 ns at 4 WS, 170 MHz); without it, a read lands half way
    through the cycle after the wait states (the G474's 26 ns plus a 0.5 ns
    address phase)."""
    period_ps = 1e6 / clock_mhz
    ws_ps = flash_ws * period_ps
    if caches == "none":
        return int(round(ws_ps + 0.5 * period_ps)) - 500
    return int(round(ws_ps * 23000 / (4 * 1e6 / 170)))


def make_board(firmware=None, *, caches="full", clock_mhz=168, flash_ws=5):
    if caches not in CACHES:
        raise ValueError(f"caches is one of {', '.join(CACHES)}, not {caches}")
    cache_on, prefetch_on = CACHES[caches]
    platform = STM32F405Platform()

    board = ArmMSystem()
    board.platform = platform
    board.cpuid = platform.cpuid()
    # The accelerator's line: a 128-bit flash read. The core still fetches 32 bits
    # at a time (CortexM4CPU's fetch1LineWidth).
    board.cache_line_size = LINE
    board.voltage_domain = VoltageDomain(voltage="1.0V")
    board.clk_domain = SrcClockDomain(
        clock=f"{clock_mhz}MHz", voltage_domain=board.voltage_domain
    )
    # Every bus is a zero-latency router on a one-tick clock (the G474 board's
    # _make_bus_clock_domain): the cycles live in the memories and the caches.
    board.bus_clk_domain = SrcClockDomain(
        clock="1ps", voltage_domain=VoltageDomain(voltage="1.0V")
    )

    cpu = CortexM4CPU()
    board.cpu = cpu
    board.mem_mode = cpu.memory_mode()
    cpu.clk_domain = board.clk_domain
    cpu.cpu_id = 0
    cpu.socket_id = 0
    cpu.createThreads()
    cpu.createInterruptController()

    clk = board.bus_clk_domain
    board.system_bus = _with_badaddr(_xbar(clk, 4))
    board.flash_bus = _with_badaddr(_xbar(clk, 4))
    board.dcode_flash_bus = _with_badaddr(_xbar(clk, 4))
    # ICode and DCode decode: flash to the accelerator, CCM (DCode only) to its
    # own memory, everything else to the system bus.
    board.icode_bus = _xbar(clk, 8)
    board.icode_bus.default = board.system_bus.cpu_side_ports
    board.dcode_bus = _xbar(clk, 8)
    board.dcode_bus.default = board.system_bus.cpu_side_ports

    latency = f"{flash_latency_ps(caches, clock_mhz, flash_ws)}ps"
    if caches == "none":
        # One memory with an ICode and a DCode port, DCode first, each keeping
        # the last 128-bit line it read.
        board.flash = PipelinedSimpleMemory(
            range=FLASH,
            latency=latency,
            address_phase_latency="500ps",
            memory_read_request_size=LINE,
            port_priority=[0, 1],
            port_read_buffer_size=[LINE, LINE],
        )
        board.flash.port = board.flash_bus.mem_side_ports
        board.flash.port = board.dcode_flash_bus.mem_side_ports
        board.icode_bus.mem_side_ports = board.flash_bus.cpu_side_ports
        board.dcode_bus.mem_side_ports = board.dcode_flash_bus.cpu_side_ports
    else:
        board.flash = SimpleMemory(range=FLASH, latency=latency)
        board.flash.port = board.flash_bus.mem_side_ports
        board.dcode_flash_bus.mem_side_ports = board.flash_bus.cpu_side_ports
        board.art_icache = ARTCache(
            size="1KiB",
            assoc=1024 // LINE,  # one set: LRU over all 64 lines
            tag_latency=0,
            data_latency=0,
            response_latency=0,
            mshrs=2,
            tgts_per_mshr=2,
            write_buffers=0,
            is_read_only=True,
            sequential_access=False,
            replacement_policy=LRURP(),
            addr_ranges=[FLASH],
            cache_blk_size=LINE,
            pf_blk_size=LINE,
            enable_prefetch=prefetch_on,
            direct_memory_mode=not cache_on,
            prefetch_on_cache_hit=True,
            buffer_hit_latency="0ns",
            address_phase_latency="0ns",
            flash_start_addr=FLASH.start,
            flash_end_addr=FLASH.end,
            arrive_buffer_size=0,
        )
        board.art_icache.cpu_side = board.icode_bus.mem_side_ports
        board.art_icache.mem_side = board.flash_bus.cpu_side_ports
        if cache_on:
            board.art_dcache = NoncoherentCache(
                size=f"{8 * LINE}B",
                assoc=8,  # one set: LRU over all 8 lines
                tag_latency=0,
                data_latency=0,
                response_latency=0,
                mshrs=2,
                tgts_per_mshr=2,
                write_buffers=0,
                is_read_only=True,
                sequential_access=False,
                replacement_policy=LRURP(),
                addr_ranges=[FLASH],
                blk_size=LINE,
            )
            board.art_dcache.cpu_side = board.dcode_bus.mem_side_ports
            board.art_dcache.mem_side = board.dcode_flash_bus.cpu_side_ports
        else:
            board.dcode_bus.mem_side_ports = board.dcode_flash_bus.cpu_side_ports

    board.sram = SimpleMemory(range=SRAM, latency="0ns")
    board.sram.port = board.system_bus.mem_side_ports
    # CCM: the core's loads and stores come from DCode; exception stacking (a
    # stack in CCM, as the firmware's Kalman task has) and gem5's own accesses
    # come from the system bus.
    board.ccm_bus = _xbar(clk, 4)
    board.ccm = SimpleMemory(range=CCM, latency="0ns")
    board.ccm.port = board.ccm_bus.mem_side_ports
    board.dcode_bus.mem_side_ports = board.ccm_bus.cpu_side_ports
    board.system_bus.mem_side_ports = board.ccm_bus.cpu_side_ports
    board.mem_ranges = [FLASH, SRAM, CCM]

    # gem5's own accesses (loading the image) reach every memory from the
    # system bus.
    board.system_bus.mem_side_ports = board.flash_bus.cpu_side_ports
    board.system_port = board.system_bus.cpu_side_ports

    platform.scs.pio = board.system_bus.mem_side_ports
    platform.dwt.pio = board.system_bus.mem_side_ports
    platform.dwt.clk_domain = board.clk_domain  # counts core cycles

    cpu.mmu.stacking_port = board.system_bus.cpu_side_ports
    cpu.icache_port = cpu.mmu.stacking_barrier.cpu_side_port
    cpu.mmu.stacking_barrier.mem_side_port = board.icode_bus.cpu_side_ports
    cpu.dcache_port = board.dcode_bus.cpu_side_ports

    if firmware is not None:
        board.workload = ArmMFsWorkload(object_file=firmware)
    return board
