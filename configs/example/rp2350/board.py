"""Single-core Pico 2 / RP2350 timing baseline.

Select the Arm M4 timing proxy or the calibrated RV32 MinorCPU proxy at
configuration time. Both use the same physically addressed XIP and SRAM
paths. This is a benchmark board, not an RP2350 boot ROM/peripheral model.
"""

from m5.objects import (
    AddrRange,
    BadAddr,
    Bridge,
    NoncoherentCache,
    NoncoherentXBar,
    SimpleMemory,
    SrcClockDomain,
    VoltageDomain,
)


CLOCK = "150MHz"
XIP_BASE = 0x10000000
XIP_SIZE = "4MiB"  # Pico 2's fitted external flash

# Physical windows; the legacy model lumps each striped group together.
SRAM_REGIONS = (
    ("sram0_3", 0x20000000, "256KiB"),
    ("sram4_7", 0x20040000, "256KiB"),
    ("sram8", 0x20080000, "4KiB"),
    ("sram9", 0x20081000, "4KiB"),
)
# A SimpleMemory has two knobs, latency and bandwidth
# Each scratch SRAM bank has a single 32-bit (4-byte) access port.
# At 150 MHz, the theoretical peak throughput is:
#   4 bytes/access * 150e6 accesses/s = 600e6 bytes/s = 600 MB/s
# which is about 572 MiB/s.
SCRATCH_BANK_BANDWIDTH = "572MiB/s"


# 64 bit 0 extra latency crossbar
def _router(clock_domain):
    return NoncoherentXBar(
        clk_domain=clock_domain,
        frontend_latency=0,
        forward_latency=0,
        response_latency=0,
        header_latency=0,
        width=8,
    )


def _attach_memory(board, *, xip_miss_ns, sram_latency_ns,
                   scratch_bank_bandwidth, sram_model, dma, xip_model):
    """Wire both CPU ports to one shared XIP cache or the SRAM bus.

    The legacy path uses lumped SimpleMemory windows. The banked path uses
    a clocked C++ fabric with independent instruction and data interfaces.
    """
    board.cache_line_size = 8
    board.voltage_domain = VoltageDomain(voltage="1.0V")
    board.clk_domain = SrcClockDomain(
        clock=CLOCK, voltage_domain=board.voltage_domain
    )
    board.fast_clock = SrcClockDomain(
        clock="10GHz", voltage_domain=board.voltage_domain
    )

    xip_range = AddrRange(XIP_BASE, size=XIP_SIZE)
    board.mem_ranges = [xip_range]

    # The auxiliary SRAM/peripheral router also supplies the legacy path.
    board.sram_bus = _router(board.fast_clock)
    # throw errors for bad addresses
    board.badaddr = BadAddr()
    board.sram_bus.default = board.badaddr.pio
    # SIO (CPUID and the GPIO registers), shared by both core types.
    from m5.objects import RP2350SIO
    board.sio = RP2350SIO(pio_latency="1ns")
    board.sio.pio = board.sram_bus.mem_side_ports
    board._banked_sram = sram_model == "banked"
    if board._banked_sram:
        from m5.objects import RP2350SRAM
        board.sram = RP2350SRAM(clk_domain=board.clk_domain)
        board.mem_ranges.append(board.sram.range)
        # Loader/stacking/peripheral router has its own manager interface.
        board.sram.port = board.sram_bus.mem_side_ports
    else:
        for name, base, size in SRAM_REGIONS:
            memory_args = dict(range=AddrRange(base, size=size),
                               latency=f"{sram_latency_ns}ns")
            if name in ("sram8", "sram9"):
                memory_args["bandwidth"] = scratch_bank_bandwidth
            memory = SimpleMemory(**memory_args)
            setattr(board, name, memory)
            board.mem_ranges.append(memory.range)
            memory.port = board.sram_bus.mem_side_ports

    # XIP path: the front router merges instruction and data requests before
    # the shared cache. The QMI bridge connects only to flash memory.
    # The physical XIP cache is shared by instruction fetch and data reads.
    board.xip_front = _router(board.fast_clock)
    if xip_model == "qmi":
        # The cache's two banks and the flash interface's serial transfers,
        # with a Pico 2's timing (RP2350XIP); xip_miss_ns is not used.
        from m5.objects import RP2350XIP
        board.flash = RP2350XIP(range=xip_range, clk_domain=board.clk_domain)
        board.xip_front.mem_side_ports = board.flash.port
    else:
        # Cache geometry matches RP2350; gem5 hit and QMI miss timing are
        # proxies.
        board.flash = SimpleMemory(range=xip_range, latency="1ns")
        # A hit answers in the core cycle it is asked, as an SRAM read does
        # (RP2350 datasheet 4.4.1, "1 cycle hit"): a request waits for the
        # cache's next clock edge, so the cache runs on the routers' clock,
        # with zero tag, data and response latency.
        board.xip_cache = NoncoherentCache(
            size="16KiB", assoc=2, tag_latency=0, data_latency=0,
            response_latency=0, mshrs=4, tgts_per_mshr=4,
            addr_ranges=[xip_range], clk_domain=board.fast_clock,
        )
        board.qmi_delay = Bridge(delay=f"{xip_miss_ns}ns")
        board.xip_cache.mem_side = board.qmi_delay.cpu_side_port
        board.qmi_delay.mem_side_port = board.flash.port
        board.xip_front.mem_side_ports = board.xip_cache.cpu_side

    # CPU-facing routers choose the XIP path by address and send other
    # requests to the SRAM bus, whose default responder rejects bad addresses.
    board.icode_router = _router(board.fast_clock)
    board.dcode_router = _router(board.fast_clock)
    for router in (board.icode_router, board.dcode_router):
        router.default = board.sram_bus.cpu_side_ports
        router.mem_side_ports = board.xip_front.cpu_side_ports
    if board._banked_sram:
        # Do not merge ICode/DCode through sram_bus: each must retain its
        # own one-word-per-cycle limit at the SRAM endpoint.
        board.dcode_router.mem_side_ports = board.sram.port
    # The firmware loader needs a path to both XIP and SRAM addresses.
    board.system_port = board.dcode_router.cpu_side_ports

    if dma:
        # The DMA's read and write managers, each routed like a core's data
        # port: flash through the XIP cache, the SRAM (on the banked model
        # as a manager of its own, competing bank by bank), the rest through
        # the peripheral router.
        from m5.objects import RP2350DMA
        board.dma = RP2350DMA(pio_latency="1ns")
        board.dma.pio = board.sram_bus.mem_side_ports
        board.dma_read_router = _router(board.fast_clock)
        board.dma_write_router = _router(board.fast_clock)
        for router in (board.dma_read_router, board.dma_write_router):
            router.default = board.sram_bus.cpu_side_ports
            router.mem_side_ports = board.xip_front.cpu_side_ports
            if board._banked_sram:
                router.mem_side_ports = board.sram.port
        board.dma.read_port = board.dma_read_router.cpu_side_ports
        board.dma.write_port = board.dma_write_router.cpu_side_ports


def _make_arm(firmware, *, xip_miss_ns, sram_latency_ns,
              scratch_bank_bandwidth, arm_predictor, arm_divider, sram_model,
              arm_timing, dma, xip_model):
    from m5.objects import ArmMSystem, ArmSemihosting
    from m5.objects.ArmMSystem import ArmMReleaseCortexM33
    from m5.objects.ArmMFsWorkload import ArmMFsWorkload
    from m5.objects.MProfilePlatform import ArmMPlatform
    from m5.objects.MProfileDWT import MProfileDWT
    from m5.objects.MProfileSCS import MProfileSCS
    from m5.objects.BaseMinorCPU import DynamicLatencyIntDivFU
    from m5.objects.BranchPredictor import LocalBP, SimpleBTB

    class RP2350M4ProxyPlatform(ArmMPlatform):
        code_ranges = [AddrRange(XIP_BASE, size=XIP_SIZE)]
        sram_ranges = [AddrRange(base, size=size)
                       for _, base, size in SRAM_REGIONS]
        scs = MProfileSCS(
            num_irqs=52, priority_bits=4, num_systick=1,
            has_basepri=True, systick_calib=0, pio_latency="1ns",
        )
        # The cycle counter answers within the core's cycle, like the SCS
        # (BasicPioDevice's default is 100ns, 15 core cycles).
        dwt = MProfileDWT(pio_latency="1ns")

    # The RP2350's Arm cores are Cortex-M33s.
    board = ArmMSystem(release=ArmMReleaseCortexM33())
    _attach_memory(board, xip_miss_ns=xip_miss_ns,
                   sram_latency_ns=sram_latency_ns,
                   scratch_bank_bandwidth=scratch_bank_bandwidth,
                   sram_model=sram_model, dma=dma, xip_model=xip_model)

    # Decouple in-flight ICode responses on SRAM branch redirects. Direct
    # xbar-to-memory fetch crashes this gem5 branch; this adds no SRAM cache.
    # The fork's single-stage M4 Fetch2 handles an Execute redirect immediately,
    # then its latched-branch fallback flushes the input again one cycle later.
    # Direct SRAM can return the target line between those flushes, so Fetch2
    # discards it and pairs the target PC with the next line; its unsigned line
    # offset then underflows and gem5 exits with SIGBUS. This zero-delay bridge
    # buffers the response until after the duplicate flush. It is a simulator
    # workaround, not an RP2350 hardware component.
    board.sram_fetch_bridge = Bridge(
        delay="0ns",
        ranges=[AddrRange(base, size=size) for _, base, size in SRAM_REGIONS],
    )
    board.sram_fetch_bridge.mem_side_port = (
        board.sram.port if board._banked_sram else board.sram_bus.cpu_side_ports
    )
    board.icode_router.mem_side_ports = board.sram_fetch_bridge.cpu_side_port
    board.platform = RP2350M4ProxyPlatform()


    board.cpuid = 0x410FC241

    # The CortexM33CPU timing, tuned against a Pico 2 (the default), or the
    # CortexM4CPU's as it was before its tuning against the STM32G474
    if arm_timing == "tuned":
        from gem5.prebuilt.cortexm.cpu.cortex_m33 import CortexM33CPU
        board.cpu = CortexM33CPU(cpu_id=0)
    elif arm_timing == "pre-tuning":
        from gem5.prebuilt.cortexm.cpu.cortex_m4 import (
            CortexM4CPU,
            use_pre_tuning_timing,
        )
        board.cpu = CortexM4CPU(cpu_id=0)
        use_pre_tuning_timing(board.cpu)
    else:
        raise ValueError(f"Unknown Arm timing: {arm_timing}")

    # For playing around with different branch predictors and dividers
    for unit in board.cpu.executeFuncUnits.funcUnits:
        if isinstance(unit, DynamicLatencyIntDivFU):
            unit.rp2350_m33_timing = arm_divider == "rp2350-m33"
    if arm_predictor in ("local", "local-small"):
        board.cpu.branchPred.conditionalBranchPred = LocalBP(
            localPredictorSize=2048, localCtrBits=2
        )
    if arm_predictor in ("local", "large-btb"):
        board.cpu.branchPred.btb = SimpleBTB(
            numEntries=1024, tagBits=16, instShiftAmt=1
        )

    # The Cortex-M33 has no write buffer: a store that the bus makes wait
    # holds the instructions behind it. Only the banked SRAM has banks to
    # lose; the legacy memories keep the store buffer's timing.
    board.cpu.executeWaitedStoreHoldsInsts = board._banked_sram

    board.cpu.clk_domain = board.clk_domain
    board.mem_mode = board.cpu.memory_mode()
    board.cpu.createThreads()
    board.cpu.createInterruptController()
    board.cpu.icache_port = board.cpu.mmu.stacking_barrier.cpu_side_port
    board.cpu.mmu.stacking_barrier.mem_side_port = (
        board.icode_router.cpu_side_ports
    )
    board.cpu.dcache_port = board.dcode_router.cpu_side_ports
    board.cpu.mmu.stacking_port = board.sram_bus.cpu_side_ports
    board.platform.scs.pio = board.sram_bus.mem_side_ports
    # The DWT (cycle counter) on the same bus, counting core clock cycles.
    board.platform.dwt.pio = board.sram_bus.mem_side_ports
    board.platform.dwt.clk_domain = board.clk_domain
    # The M33s' coprocessors: GPIO (p0) on the SIO, the DCP (p4, p5) as far
    # as the SDK's start-up uses it, and the RCP (p7), present but not
    # modelled.
    from m5.objects import (
        MProfileCoprocessor,
        RP2350DcpCoprocessor,
        RP2350GpioCoprocessor,
    )
    if dma:
        # DMA_IRQ_0 to DMA_IRQ_3 are the NVIC's IRQs 10 to 13.
        from m5.objects import MProfileIrqLine
        board.dma_irq_lines = [
            MProfileIrqLine(scs=board.platform.scs, irq_num=10 + n)
            for n in range(4)
        ]
        for line in board.dma_irq_lines:
            board.dma.irq = line.pin
    board.gpio_coprocessor = RP2350GpioCoprocessor(sio=board.sio)
    board.dcp = RP2350DcpCoprocessor()
    board.rcp = MProfileCoprocessor(numbers=[7])
    board.semihosting = ArmSemihosting(mem_reserve="0B", stack_size="0B")
    board.workload = ArmMFsWorkload(object_file=firmware)
    return board


def _make_riscv(firmware, *, xip_miss_ns, sram_latency_ns,
                scratch_bank_bandwidth, sram_model, dma, xip_model):
    from m5.objects import RiscvSystem
    from m5.objects.RiscvCPU import RiscvMinorCPU
    from m5.objects.RiscvFsWorkload import RiscvBareMetal
    from m5.objects.RiscvISA import RiscvISA
    from m5.objects.RiscvSemihosting import RiscvSemihosting
    from m5.objects.BaseMinorCPU import (
        MinorDefaultFUPool,
        MinorDefaultIntDivFU,
        MinorDefaultIntFU,
        MinorDefaultIntMulFU,
    )

    board = RiscvSystem()
    _attach_memory(board, xip_miss_ns=xip_miss_ns,
                   sram_latency_ns=sram_latency_ns,
                   scratch_bank_bandwidth=scratch_bank_bandwidth,
                   sram_model=sram_model, dma=dma, xip_model=xip_model)
    # Hazard3 testbench IO registers (print, exit, IRQs, timer) at 0xc0000000
    # (tb_cxxrtl_io.h), so firmware built for rtl-sim runs unchanged.
    from m5.objects import Hazard3TbIO
    board.tb_io = Hazard3TbIO(pio_latency="1ns")
    board.tb_io.pio = board.sram_bus.mem_side_ports
    if board._banked_sram:
        board.icode_router.mem_side_ports = board.sram.port
    board.mem_mode = "timing"
    board.cpu = RiscvMinorCPU(cpu_id=0)
    board.cpu.clk_domain = board.clk_domain
    board.cpu.isa = [RiscvISA(
        # M and U modes, as Hazard3 on the RP2350 and in arcsim-soc
        # (U_MODE = 1 in config_rp2350_default.vh).
        riscv_type="RV32", enable_rvv=False, privilege_mode_set="MU",
        enable_Zicbom_fs=False, enable_Zicboz_fs=False,
        # Hazard3 has Zcmp (cm.push/cm.pop) and no FPU; gem5 decodes Zcmp only
        # with Zcd, whose encodings it reuses, turned off.
        enable_Zcd=False,
        # Hazard3's bit extract multiple, which the Pico SDK's float
        # routines use.
        enable_Xh3bextm=True,
    )]
    # Hazard3's interrupt controller, with the RP2350's 52 IRQs and 4 priority
    # bits (arcsim-soc's config_rp2350_default.vh). Devices reach it through
    # Xh3irqLine.
    from m5.objects.Xh3irq import Xh3irq
    board.xh3irq = Xh3irq(num_irqs=52, priority_bits=4)
    board.cpu.isa[0].xh3irq = board.xh3irq
    # The testbench IO block's interrupts, wired as rtl-sim wires them:
    # SET_IRQ's 32 bits to IRQs 0 to 31, its soft IRQ to mip.msip and its
    # timer to mip.mtip.
    from m5.objects import RiscvIntLine
    from m5.objects.Xh3irq import Xh3irqLine
    board.tb_irq_lines = [
        Xh3irqLine(controller=board.xh3irq, irq_num=n) for n in range(32)
    ]
    for line in board.tb_irq_lines:
        board.tb_io.irq = line.pin
    board.tb_soft_irq_line = RiscvIntLine(int_num=3)
    board.tb_io.soft_irq = board.tb_soft_irq_line.pin
    board.tb_timer_irq_line = RiscvIntLine(int_num=7)
    board.tb_io.timer_irq = board.tb_timer_irq_line.pin
    board.cpu.decodeInputWidth = 1
    board.cpu.executeInputWidth = 1
    board.cpu.executeIssueLimit = 1
    board.cpu.executeCommitLimit = 1
    board.cpu.fetch1LineWidth = 8
    board.cpu.fetch1LineSnapWidth = 8
    board.cpu.fetch1FetchLimit = 2
    board.cpu.fetch1ToFetch2ForwardDelay = 1
    board.cpu.fetch1ToFetch2BackwardDelay = 0
    board.cpu.executeBranchDelay = 1
    units = MinorDefaultFUPool()
    for unit in units.funcUnits:
        if isinstance(unit, (MinorDefaultIntFU, MinorDefaultIntMulFU)):
            unit.opLat = 1
        elif isinstance(unit, MinorDefaultIntDivFU):
            unit.opLat = 19
            unit.issueLat = 19
    board.cpu.executeFuncUnits = units
    board.cpu.createThreads()
    board.cpu.createInterruptController()
    board.cpu.icache_port = board.icode_router.cpu_side_ports
    board.cpu.dcache_port = board.dcode_router.cpu_side_ports
    board.workload = RiscvBareMetal(
        bootloader=firmware, semihosting=RiscvSemihosting(),
        auto_reset_vect=False, reset_vect=0x10000040,
    )
    return board


def memory_models(model, sram_model=None, dma=None, xip_model=None):
    """(sram_model, dma, xip_model) for a board model, each as given or,
    if None, as the model names it."""
    if model not in ("legacy", "accurate"):
        raise ValueError(f"Unknown board model: {model}")
    accurate = model == "accurate"
    if sram_model is None:
        sram_model = "banked" if accurate else "legacy"
    if dma is None:
        dma = accurate
    if xip_model is None:
        xip_model = "qmi" if accurate else "legacy"
    return sram_model, dma, xip_model


def make_board(core, firmware, *, xip_miss_ns=50, sram_latency_ns=1,
               scratch_bank_bandwidth=SCRATCH_BANK_BANDWIDTH,
               arm_predictor="m4", arm_divider="rp2350-m33",
               sram_model=None, arm_timing="tuned", dma=None,
               xip_model=None, model="legacy"):
    """Return exactly one active core with the RP2350 memory baseline.

    model names the memory system: "legacy" is the lumped SRAM windows and
    the generic cache in front of flash; "accurate" is the banked SRAM, the
    DMA controller and RP2350XIP, the models timed against a Pico 2.
    sram_model, dma and xip_model each choose one part and override model.

    dma adds the DMA controller (RP2350DMA) at 0x50000000-0x50003fff; on the
    Hazard3 proxy its interrupt outputs are left unconnected. xip_model
    "qmi" replaces the generic cache and fixed miss delay in front of flash
    with RP2350XIP."""
    sram_model, dma, xip_model = memory_models(model, sram_model, dma,
                                                xip_model)
    if sram_model not in ("legacy", "banked"):
        raise ValueError(f"Unknown SRAM model: {sram_model}")
    if xip_model not in ("legacy", "qmi"):
        raise ValueError(f"Unknown XIP model: {xip_model}")
    if core == "arm-m4-proxy":
        return _make_arm(firmware, xip_miss_ns=xip_miss_ns,
                         sram_latency_ns=sram_latency_ns,
                         scratch_bank_bandwidth=scratch_bank_bandwidth,
                         arm_predictor=arm_predictor,
                         arm_divider=arm_divider, sram_model=sram_model,
                         arm_timing=arm_timing, dma=dma,
                         xip_model=xip_model)
    if core == "hazard3-proxy":
        return _make_riscv(firmware, xip_miss_ns=xip_miss_ns,
                           sram_latency_ns=sram_latency_ns,
                           scratch_bank_bandwidth=scratch_bank_bandwidth,
                           sram_model=sram_model, dma=dma,
                           xip_model=xip_model)
    raise ValueError(f"Unknown RP2350 core selector: {core}")  
