"""The Crazyflie 2.1's STM32F405 as a gem5 board: CrazyflieF405CPU (cpu.py: the G474's
CortexM4CPU, Cortex-M4 r0p1 on both, with the Crazyflie's own timing), the F405's flash accelerator (ART) in each of its four
settings, SRAM on the system bus and CCM on the data bus only.

    make_board(firmware, caches="full", clock_mhz=168, flash_ws=5, cpu="minor",
               inst_latency=False) -> ArmMSystem

caches names FLASH_ACR's bits as arcsim-bench does: full (ICEN DCEN PRFTEN),
noprefetch (ICEN DCEN), nocache (PRFTEN), none. Peripherals beyond the SCS and
the DWT are the caller's: attach them to board.system_bus.mem_side_ports.

cpu is minor (CrazyflieF405CPU, the cycle-level model) or atomic (gem5's atomic CPU,
instruction level: no core timing). With inst_latency the atomic CPU is timed by the
F405's latency table (latency.py: CrazyflieF405CPU's classes), the accelerator's atomic
timing (its hits, its current and prefetched lines, a flash line read of the wait states
plus one) and the system bus that SRAM code's fetches share with data.

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

import os
import sys

from gem5.prebuilt.cortexm.platforms import STM32F405Platform

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cpu import CrazyflieF405CPU  # noqa: E402  (this directory's cpu.py)

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


def make_board(
    firmware=None, *, caches="full", clock_mhz=168, flash_ws=5, cpu="minor", inst_latency=False,
    timing="tuned"
):
    """timing "prior-m4" gives the timed core (cpu "minor") the Cortex-M4 model as published
    before ARCsim's tuning (CortexM4CPU with use_pre_tuning_timing) in place of the
    Crazyflie's own, on the same board and flash, for comparing the two."""
    if caches not in CACHES:
        raise ValueError(f"caches is one of {', '.join(CACHES)}, not {caches}")
    if cpu not in ("minor", "atomic"):
        raise ValueError(f"cpu is minor or atomic, not {cpu}")
    if timing not in ("tuned", "prior-m4"):
        raise ValueError(f"timing is tuned or prior-m4, not {timing}")
    if timing != "tuned" and cpu != "minor":
        raise ValueError("timing prior-m4 is for the minor CPU")
    if inst_latency and cpu != "atomic":
        raise ValueError("inst_latency is for the atomic CPU")
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

    kind = cpu
    if kind == "atomic":
        from m5.objects.ArmMCPU import ArmMAtomicSimpleCPU

        cpu = ArmMAtomicSimpleCPU()
        if inst_latency:
            from latency import f405_inst_latency  # this directory's latency.py

            # Flash: the latency the accelerator (or the flash) returns. SRAM: the
            # system bus, which its code's fetches share with data, data first
            # [DDI0439D 2.3.1]. A fetch is registered, two cycles, never back to
            # back [the Cortex-M3's DDI0337B 14.4.6; the M4's manual gives no fetch
            # timing, and the Crazyflie's micro families behave so]. Fetches run
            # ahead through the core's three-word fetch queue, and an unaligned load
            # splits into a byte, a halfword and a byte [DDI0439C glossary, Prefetch
            # Unit; DDI0439C 3.3.2].
            cpu.inst_latency = f405_inst_latency(
                timed_ranges=[FLASH], bus_ranges=[SRAM], bus_word_fifo=3, unaligned_pieces=True
            )
    elif timing == "prior-m4":
        from gem5.prebuilt.cortexm.cpu.cortex_m4 import CortexM4CPU, use_pre_tuning_timing

        cpu = CortexM4CPU()
        use_pre_tuning_timing(cpu)
    else:
        cpu = CrazyflieF405CPU()  # cpu.py: CortexM4CPU with the Crazyflie's own timing
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
    # An instruction read from flash takes the wait states plus one cycle (RM0090
    # table 11: 6 cycles at 5 WS) and a data read the wait states, the core's load
    # spending the other cycle: this router holds instruction reads for that cycle.
    board.icode_flash_link = _xbar(clk, LINE)
    board.icode_flash_link.frontend_latency = flash_latency_ps(
        caches, clock_mhz, flash_ws + 1
    ) - flash_latency_ps(caches, clock_mhz, flash_ws)
    board.icode_flash_link.mem_side_ports = board.flash_bus.cpu_side_ports
    # ICode and DCode decode: flash to the accelerator, CCM (DCode only) to its
    # own memory, everything else to the system bus.
    board.icode_bus = _xbar(clk, 8)
    # An instruction fetch from SRAM goes over the system bus, one word every two core
    # cycles (the Crazyflie: 32-bit instructions in SRAM about 2 cycles each, 16-bit
    # ones 1): a 32-bit router clocked at half the core's, which a word occupies for
    # one of its cycles.
    board.sram_fetch_clk_domain = SrcClockDomain(
        clock=f"{clock_mhz / 2}MHz", voltage_domain=board.voltage_domain
    )
    board.sram_fetch_bus = _xbar(board.sram_fetch_clk_domain, 4)
    board.icode_bus.default = board.sram_fetch_bus.cpu_side_ports
    board.sram_fetch_bus.default = board.system_bus.cpu_side_ports
    board.dcode_bus = _xbar(clk, 8)
    board.dcode_bus.default = board.system_bus.cpu_side_ports

    latency = f"{flash_latency_ps(caches, clock_mhz, flash_ws)}ps"
    # At IL every setting has the accelerator, whose atomic timing keeps the line last
    # read. With every bit off it serves only that line (direct mode), and the next line is
    # read ahead as the core's own fetch-ahead reads it. This setting is the IL tier's
    # weakest: no read time fits both the Crazyflie's straight-line bodies (9 to 10 cycles
    # a line) and its kernels and suites, so it keeps the documented one.
    if caches == "none" and not inst_latency:
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
        board.icode_bus.mem_side_ports = board.icode_flash_link.cpu_side_ports
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
            enable_prefetch=prefetch_on or (inst_latency and caches == "none"),
            direct_memory_mode=not cache_on,
            prefetch_on_cache_hit=True,
            buffer_hit_latency="0ns",
            address_phase_latency="0ns",
            flash_start_addr=FLASH.start,
            flash_end_addr=FLASH.end,
            arrive_buffer_size=0,
            # An instruction read from flash: the wait states plus one cycle
            # (RM0090 table 11).
            atomic_latency=inst_latency,
            atomic_line_cycles=flash_ws + 1,
        )
        board.art_icache.cpu_side = board.icode_bus.mem_side_ports
        board.art_icache.mem_side = board.icode_flash_link.cpu_side_ports
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
