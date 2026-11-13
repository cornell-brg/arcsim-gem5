"""Pico 2 / RP2350 timing baseline: one core, or two (the Arm board's two
M33s, the Hazard3 proxy's two cores as arcsim-soc's dual-core soc.v).

Select the Arm M4 timing proxy or the RV32 MinorCPU proxy (Hazard3) at
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


# The core clock (board.clk_domain): the CPU, its cycle counter, the XIP
# cache and the banked SRAM. make_board(clock=) sets another.
CLOCK = "150MHz"
# make_board's xip_model values (run.py's --xip-model, and bench's runner asks for them)
XIP_MODELS = ("legacy",)
XIP_BASE = 0x10000000
XIP_SIZE = "16MiB"  # flash


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

# The HWPEs the Hazard3 board models, by arcsim-soc's CONFIG_HWPE names. Only
# empty: no HWPE and no TCDM, as arcsim-soc's configs without an accelerator
# (XCEL_PRESENT=0). Any other HWPE (soc_configs.py) is not modelled in gem5.
HWPES = ("empty",)


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


def _core_child(board, name, n, obj):
    """Make obj core n's board child called name: core 0's keeps the name, as
    on a one-core board, and core n's ends in n (cpu1, xh3irq1, ...)."""
    setattr(board, name if n == 0 else f"{name}{n}", obj)
    return obj


def _core_routers(board, n):
    """Core n's CPU-facing routers (icode_router, dcode_router): each
    chooses the XIP path by address and sends other requests to the SRAM
    bus, whose default responder rejects bad addresses. Returns them as
    (icode, dcode)."""
    icode = _core_child(board, "icode_router", n, _router(board.fast_clock))
    dcode = _core_child(board, "dcode_router", n, _router(board.fast_clock))
    for router in (icode, dcode):
        router.default = board.sram_bus.cpu_side_ports
        router.mem_side_ports = board.xip_front.cpu_side_ports
    if board._banked_sram:
        # Do not merge ICode/DCode through sram_bus: each must retain its
        # own one-word-per-cycle limit at the SRAM endpoint.
        dcode.mem_side_ports = board.sram.port
    return icode, dcode


def _attach_memory(board, *, xip_miss_ns, sram_latency_ns,
                   scratch_bank_bandwidth, sram_model, dma, xip_model,
                   clock=CLOCK, sram4_7=True):
    """Wire both CPU ports to one shared XIP cache or the SRAM bus.

    The legacy path uses lumped SimpleMemory windows. The banked path uses
    a clocked C++ fabric with independent instruction and data interfaces.
    Without sram4_7, SRAM4-7 are absent: their accesses go to the SRAM bus's
    default (BadAddr), and they are not in board.mem_ranges.
    """
    board.cache_line_size = 8
    board.voltage_domain = VoltageDomain(voltage="1.0V")
    board.clk_domain = SrcClockDomain(
        clock=clock, voltage_domain=board.voltage_domain
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
        board.sram = RP2350SRAM(clk_domain=board.clk_domain, sram4_7=sram4_7)
        if sram4_7:
            board.mem_ranges.append(board.sram.range)
        else:
            # The two ranges its ports claim (RP2350SRAM's sram4_7)
            board.mem_ranges += [
                AddrRange(base, size=size) for name, base, size in
                SRAM_REGIONS if name != "sram4_7"]
        # Loader/stacking/peripheral router has its own manager interface.
        board.sram.port = board.sram_bus.mem_side_ports
    else:
        for name, base, size in SRAM_REGIONS:
            if name == "sram4_7" and not sram4_7:
                continue
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
    # Cache geometry matches RP2350; gem5 hit and QMI miss timing are proxies.
    board.flash = SimpleMemory(range=xip_range, latency="1ns")
    board.xip_cache = NoncoherentCache(
        size="16KiB", assoc=2, tag_latency=1, data_latency=1,
        response_latency=1, mshrs=4, tgts_per_mshr=4,
        addr_ranges=[xip_range],
    )
    board.qmi_delay = Bridge(delay=f"{xip_miss_ns}ns")
    board.xip_cache.mem_side = board.qmi_delay.cpu_side_port
    board.qmi_delay.mem_side_port = board.flash.port

    board.xip_front = _router(board.fast_clock)
    board.xip_front.mem_side_ports = board.xip_cache.cpu_side

    # Core 0's routers; _make_riscv adds the other cores'.
    _core_routers(board, 0)
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
              arm_timing, dma, xip_model, cpu, switch_cpu=None,
              clock=CLOCK, cores=1):
    from m5.objects import ArmMSystem, ArmSemihosting
    from m5.objects.ArmMSystem import ArmMReleaseCortexM33
    from m5.objects.ArmMFsWorkload import ArmMFsWorkload
    from m5.objects.MProfilePlatform import ArmMPlatform
    from m5.objects.MProfileDWT import MProfileDWT
    from m5.objects.MProfileSCS import MProfileSCS
    from m5.objects.BaseMinorCPU import DynamicLatencyIntDivFU
    from m5.objects.BranchPredictor import LocalBP, SimpleBTB

    def m33_scs(core):
        """A core's NVIC, SCB and SysTick (each M33 has its own)."""
        return MProfileSCS(
            num_irqs=52, priority_bits=4, num_systick=1,
            has_basepri=True, systick_calib=0, pio_latency="1ns", core=core,
        )

    class RP2350M4ProxyPlatform(ArmMPlatform):
        code_ranges = [AddrRange(XIP_BASE, size=XIP_SIZE)]
        sram_ranges = [AddrRange(base, size=size)
                       for _, base, size in SRAM_REGIONS]
        scs = m33_scs(0)
        # The cycle counter answers within the core's cycle, like the SCS
        # (BasicPioDevice's default is 100ns, 15 core cycles).
        dwt = MProfileDWT(pio_latency="1ns")

    # The RP2350's Arm cores are Cortex-M33s.
    board = ArmMSystem(release=ArmMReleaseCortexM33())
    _attach_memory(board, xip_miss_ns=xip_miss_ns,
                   sram_latency_ns=sram_latency_ns,
                   scratch_bank_bandwidth=scratch_bank_bandwidth,
                   sram_model=sram_model, dma=dma, xip_model=xip_model,
                   clock=clock)

    # ICode on its own SRAM port, as DCode: each keeps its one word a cycle
    # at the banks.
    if board._banked_sram:
        board.icode_router.mem_side_ports = board.sram.port
    board.platform = RP2350M4ProxyPlatform()


    board.cpuid = 0x410FC241

    def arm_cpu(kind, switched_out=False, cpu_id=0):
        """A Cortex-M33 core of the kind given (atomic or minor)."""
        # The CortexM33CPU timing, tuned against a Pico 2 (the default), or
        # the CortexM4CPU's as it was before its tuning against the
        # STM32G474; or the atomic CPU, with no core timing.
        if kind == "atomic":
            from m5.objects.ArmMCPU import ArmMAtomicSimpleCPU
            core = ArmMAtomicSimpleCPU(cpu_id=cpu_id, switched_out=switched_out)
        elif arm_timing == "tuned":
            from gem5.prebuilt.cortexm.cpu.cortex_m33 import CortexM33CPU
            core = CortexM33CPU(cpu_id=cpu_id, switched_out=switched_out)
        elif arm_timing == "pre-tuning":
            from gem5.prebuilt.cortexm.cpu.cortex_m4 import (
                CortexM4CPU,
                use_pre_tuning_timing,
            )
            core = CortexM4CPU(cpu_id=cpu_id, switched_out=switched_out)
            use_pre_tuning_timing(core)
        else:
            raise ValueError(f"Unknown Arm timing: {arm_timing}")

        if kind == "minor":
            # For playing around with different branch predictors and dividers
            for unit in core.executeFuncUnits.funcUnits:
                if isinstance(unit, DynamicLatencyIntDivFU):
                    unit.rp2350_m33_timing = arm_divider == "rp2350-m33"
            if arm_predictor in ("local", "local-small"):
                core.branchPred.conditionalBranchPred = LocalBP(
                    localPredictorSize=2048, localCtrBits=2
                )
            if arm_predictor in ("local", "large-btb"):
                core.branchPred.btb = SimpleBTB(
                    numEntries=1024, tagBits=16, instShiftAmt=1
                )

            # The Cortex-M33 has no write buffer: a store that the bus makes
            # wait holds the instructions behind it, and so does a load.
            # Only the banked SRAM has banks to lose; the legacy memories
            # keep the store buffer's timing.
            core.executeWaitedStoreHoldsInsts = board._banked_sram
            core.executeWaitedLoadHoldsInsts = board._banked_sram

        core.clk_domain = board.clk_domain
        core.createThreads()
        core.createInterruptController()
        return core

    board.cpu = arm_cpu(cpu)
    board._cores = [board.cpu]
    board.mem_mode = board.cpu.memory_mode()
    board.cpu.icache_port = board.cpu.mmu.stacking_barrier.cpu_side_port
    board.cpu.dcache_port = board.dcode_router.cpu_side_ports
    board.cpu.mmu.stacking_port = board.sram_bus.cpu_side_ports
    if switch_cpu:
        # Switched out until m5.switchCpus hands it the core's place, which
        # moves its fetch and data ports but not its MMU's: the stacking port
        # gets its own, and its stacking barrier, which holds fetch while an
        # exception stacks, goes in the fetch path after the core's.
        board.switch_cpu = arm_cpu(switch_cpu, switched_out=True)
        board.switch_cpu.mmu.stacking_port = board.sram_bus.cpu_side_ports
        board.cpu.mmu.stacking_barrier.mem_side_port = (
            board.switch_cpu.mmu.stacking_barrier.cpu_side_port
        )
        board.switch_cpu.mmu.stacking_barrier.mem_side_port = (
            board.icode_router.cpu_side_ports
        )
    else:
        board.cpu.mmu.stacking_barrier.mem_side_port = (
            board.icode_router.cpu_side_ports
        )
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
    if cores == 2:
        _add_second_m33(board, arm_cpu(cpu, cpu_id=1), m33_scs(1))
        board._cores.append(board.cpu1)
    return board


# The firmware's {stack pointer, entry} pair that the second M33 starts
# from (ArmMFsWorkload secondary_launch), in place of the boot ROM's launch
# of core 1 (multicore_launch_core1_raw).
CORE1_LAUNCH_SYMBOL = "arcsim_core1_launch"


def _add_second_m33(board, core, scs):
    """The RP2350's second M33 (board.cpu1), sharing the first's memory and
    peripherals, with paths of its own as the RP2350's bus fabric gives each
    core: its own ICode and DCode routers to the XIP cache and the SRAM
    banks, and its own private peripheral bus, where its SCS (NVIC, SCB,
    SysTick) and DWT answer at the same addresses as the first core's. It
    starts from the firmware's CORE1_LAUNCH_SYMBOL. Peripheral interrupts
    stay on the first core's NVIC."""
    from m5.objects.MProfileDWT import MProfileDWT
    board.scs1 = scs
    board.dwt1 = MProfileDWT(pio_latency="1ns", scs=scs,
                             clk_domain=board.clk_domain)
    board.icode1_router = _router(board.fast_clock)
    board.dcode1_router = _router(board.fast_clock)
    for router in (board.icode1_router, board.dcode1_router):
        router.default = board.sram_bus.cpu_side_ports
        router.mem_side_ports = board.xip_front.cpu_side_ports
    if board._banked_sram:
        board.icode1_router.mem_side_ports = board.sram.port
        board.dcode1_router.mem_side_ports = board.sram.port
    # Its private peripheral bus: its own SCS and DWT, the rest on to its
    # DCode router.
    board.ppb1_router = _router(board.fast_clock)
    board.ppb1_router.default = board.dcode1_router.cpu_side_ports
    board.scs1.pio = board.ppb1_router.mem_side_ports
    board.dwt1.pio = board.ppb1_router.mem_side_ports
    board.cpu1 = core
    core.icache_port = core.mmu.stacking_barrier.cpu_side_port
    core.mmu.stacking_barrier.mem_side_port = board.icode1_router.cpu_side_ports
    core.dcache_port = board.ppb1_router.cpu_side_ports
    core.mmu.stacking_port = board.sram_bus.cpu_side_ports
    board.workload.secondary_launch = CORE1_LAUNCH_SYMBOL


def _hazard3_cpu(board, n, kind, accessible, switched_out=False):
    """Core n's CPU, a Hazard3 core of the kind given (atomic or minor), on
    core n's interrupt controller (board.xh3irq, xh3irq1, ...) and the
    board's Xh3power. A switched-out core 0 for m5.switchCpus is made the
    same way, with switched_out."""
    from m5.objects.RiscvCPU import RiscvAtomicSimpleCPU, RiscvMinorCPU
    from m5.objects.RiscvISA import RiscvISA
    cpu_class = RiscvAtomicSimpleCPU if kind == "atomic" else RiscvMinorCPU
    core = cpu_class(cpu_id=n, switched_out=switched_out)
    # Peripheral space is device memory: accesses go in order and a load reads the
    # device, never a store still in the store buffer (APB and AHB peripherals, the
    # testbench IO block and SIO).
    core.mmu.pma_checker.uncacheable = [
        AddrRange(0x40000000, 0x60000000),
        AddrRange(0xC0000000, 0xE0000000),
    ]
    core.clk_domain = board.clk_domain
    core.isa = [RiscvISA(
        # M and U modes, as Hazard3 on the RP2350 and in arcsim-soc
        # (U_MODE = 1 in every SoC config's rtl/config_*.vh).
        riscv_type="RV32", enable_rvv=False, privilege_mode_set="MU",
        enable_Zicbom_fs=False, enable_Zicboz_fs=False,
        # Hazard3 has Zcmp (cm.push/cm.pop) and no FPU; gem5 decodes Zcmp only
        # with Zcd, whose encodings it reuses, turned off.
        enable_Zcd=False,
        # Hazard3's bit extract multiple, which the Pico SDK's float
        # routines use.
        enable_Xh3bextm=True,
        xh3irq=board.xh3irq if n == 0 else getattr(board, f"xh3irq{n}"),
        # h3.block and h3.unblock (EXTENSION_XH3POWER = 1)
        xh3power=board.xh3power,
    )]
    # Loads, stores and fetches outside what this board has are access
    # faults, as rtl-sim's bus errors are; the core raises them before the
    # access leaves it, so they are precise.
    core.mmu.pma_checker.accessible = accessible
    core.createThreads()
    core.createInterruptController()
    return core


def _add_hazard3_core(board, n, *, cpu, accessible):
    """Add core n of the Hazard3 proxy (cpu, xh3irq, its testbench soft and
    timer IRQ lines and, for n > 0, its routers; _core_child names them) and
    return its CPU. Core n has mhartid n only if the cores are added in
    order."""
    from m5.objects import RiscvIntLine
    from m5.objects.Xh3irq import Xh3irq

    if n == 0:
        # Made with the memory system.
        icode, dcode = board.icode_router, board.dcode_router
    else:
        icode, dcode = _core_routers(board, n)
        if board._banked_sram:
            icode.mem_side_ports = board.sram.port
    # Hazard3's interrupt controller, one per core, with the RP2350's 52 IRQs
    # and 4 priority bits (every arcsim-soc SoC config's rtl/config_*.vh). Devices
    # reach it through Xh3irqLine.
    _core_child(board, "xh3irq", n, Xh3irq(hart_id=n, num_irqs=52,
                                           priority_bits=4))
    core = _core_child(board, "cpu", n,
                       _hazard3_cpu(board, n, cpu, accessible))
    # The testbench IO block's soft IRQ n and timer n to core n's mip.msip
    # and mip.mtip, as rtl-sim wires them. Pin n of each is connected n-th.
    soft = _core_child(board, "tb_soft_irq_line", n,
                       RiscvIntLine(hart_id=n, int_num=3))
    board.tb_io.soft_irq = soft.pin
    timer = _core_child(board, "tb_timer_irq_line", n,
                        RiscvIntLine(hart_id=n, int_num=7))
    board.tb_io.timer_irq = timer.pin
    core.icache_port = icode.cpu_side_ports
    core.dcache_port = dcode.cpu_side_ports
    return core


def _make_riscv(firmware, *, xip_miss_ns, sram_latency_ns,
                scratch_bank_bandwidth, sram_model, dma, xip_model, cpu,
                hwpe, switch_cpu=None, clock=CLOCK, cores=1,
                sram4_7=True):
    from m5.objects import RiscvSystem
    from m5.objects.RiscvFsWorkload import RiscvBareMetal
    from m5.objects.RiscvSemihosting import RiscvSemihosting

    board = RiscvSystem()
    _attach_memory(board, xip_miss_ns=xip_miss_ns,
                   sram_latency_ns=sram_latency_ns,
                   scratch_bank_bandwidth=scratch_bank_bandwidth,
                   sram_model=sram_model, dma=dma, xip_model=xip_model,
                   clock=clock, sram4_7=sram4_7)
    # Hazard3 testbench IO registers (print, exit, IRQs, timer) at 0xc0000000
    # (tb_cxxrtl_io.h), so firmware built for rtl-sim runs unchanged.
    from m5.objects import Hazard3TbIO
    board.tb_io = Hazard3TbIO(pio_latency="1ns")
    board.tb_io.pio = board.sram_bus.mem_side_ports
    # XIP_CTRL's cache counters at 0x400c8000 (arcsim-soc rtl/xip/xipCtrl.sv),
    # which read 0: nothing counts the XIP cache's hits and accesses
    from m5.objects import RP2350XipCtrl
    board.xip_ctrl = RP2350XipCtrl(pio_latency="1ns")
    board.xip_ctrl.pio = board.sram_bus.mem_side_ports
    # No HWPE and no TCDM (hwpe empty, the only one modelled): their
    # addresses fault, as in soc.v without an accelerator (XCEL_PRESENT=0).
    if board._banked_sram:
        board.icode_router.mem_side_ports = board.sram.port
    # Loads, stores and fetches outside what this board has are access
    # faults, as rtl-sim's bus errors are; the core raises them before the
    # access leaves it, so they are precise.
    # flash and SRAM (without SRAM4-7 if absent)
    accessible = list(board.mem_ranges)
    if dma:
        # Its four register blocks (normal, XOR, SET, CLR), rp2350_dma.cc
        accessible.append(AddrRange(int(board.dma.pio_addr), size=0x4000))
    accessible.append(AddrRange(int(board.sio.pio_addr), size=0x10000))
    accessible.append(AddrRange(int(board.tb_io.pio_addr), size=0x200))
    if hasattr(board, "xip_ctrl"):
        accessible.append(AddrRange(int(board.xip_ctrl.pio_addr),
                                    size=int(board.xip_ctrl.pio_size)))
    # h3.block and h3.unblock: each core's unblock reaches the other's, as
    # soc.v cross-wires unblock_out and unblock_in; a core alone has none.
    from m5.objects.Xh3power import Xh3power
    board.xh3power = Xh3power(hart_ids=list(range(cores)))
    # The cores: core n has mhartid n, its thread in the system in the order
    # the cores are made.
    board._cores = [
        _add_hazard3_core(board, n, cpu=cpu,
                          accessible=accessible)
        for n in range(cores)
    ]
    board.mem_mode = board.cpu.memory_mode()
    if switch_cpu:
        # Switched out until m5.switchCpus hands it core 0's place
        board.switch_cpu = _hazard3_cpu(board, 0, switch_cpu, accessible,
                                        switched_out=True)
    # The testbench IO block's SET_IRQ bits are IRQs 0 to 31, as rtl-sim wires
    # them. Every external IRQ reaches every core's controller, as in soc.v.
    from m5.objects.Xh3irq import Xh3irqLine
    controllers = [core.isa[0].xh3irq for core in board._cores]
    board.tb_irq_lines = [
        Xh3irqLine(controllers=controllers, irq_num=n) for n in range(32)
    ]
    for line in board.tb_irq_lines:
        board.tb_io.irq = line.pin
    if dma:
        # DMA_IRQ_0 to DMA_IRQ_3 are IRQs 10 to 13, as on the RP2350; the controller
        # ORs them with the testbench IO block's lines of the same numbers, as
        # arcsim-soc's soc.v does.
        board.dma_irq_lines = [
            Xh3irqLine(controllers=controllers, irq_num=10 + n)
            for n in range(4)
        ]
        for line in board.dma_irq_lines:
            board.dma.irq = line.pin
    board.workload = RiscvBareMetal(
        bootloader=firmware, semihosting=RiscvSemihosting()
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
        xip_model = "legacy"
    return sram_model, dma, xip_model


def make_board(core, firmware, *, xip_miss_ns=50, sram_latency_ns=1,
               scratch_bank_bandwidth=SCRATCH_BANK_BANDWIDTH,
               arm_predictor="m4", arm_divider="rp2350-m33",
               sram_model=None, arm_timing="tuned", dma=None,
               xip_model=None, model="legacy", cpu="minor", hwpe="empty",
               zfinx=False, switch_cpu=None, clock=CLOCK,
               inst_latency=False, cores=1, sram4_7=True):
    """Return a board of cores cores with the RP2350 memory baseline.
    board.cpu is core 0, board.cpu1 core 1, and board._cores lists them.

    cpu is "minor", the timed core (CortexM33CPU, or the Hazard3 proxy, a
    RISC-V MinorCPU), or "atomic", gem5's atomic CPU as an instruction-level
    simulator: the same ISA and devices, no core timing.

    model names the memory system: "legacy" is the lumped SRAM windows and
    the generic cache in front of flash; "accurate" is the banked SRAM and
    the DMA controller, with the same generic cache in front of flash.
    sram_model, dma and xip_model each choose one part and override model.

    dma adds the DMA controller (RP2350DMA) at 0x50000000-0x50003fff; on the
    Hazard3 proxy its interrupt outputs are left unconnected. xip_model
    is "legacy" (XIP_MODELS), the generic cache and fixed miss delay in
    front of flash. hwpe names the Hazard3 board's HWPE, as
    arcsim-soc's CONFIG_HWPE; only empty, the default, is modelled (HWPES):
    no HWPE and no TCDM, as arcsim-soc's configs without an accelerator
    (XCEL_PRESENT=0). Any other is refused as not modelled. sram4_7
    False (hazard3-proxy only) leaves SRAM4-7 out (0x20040000-0x2007ffff
    fault), as arcsim-soc's SRAM4_7_PRESENT=0. zfinx (arcsim-soc's FPU
    zfinx) is refused as not modelled: gem5's Hazard3 has F and D.
    switch_cpu ("minor" or "atomic", either proxy) adds board.switch_cpu,
    a core of that kind switched out, for m5.switchCpus. clock is the core
    clock (CLOCK, a Pico 2's default), for firmware built for another.
    The XIP cache and the banked SRAM count in its cycles; the flash delay
    (xip_miss_ns) and the legacy SRAM latency (sram_latency_ns) are in ns.
    inst_latency (an atomic CPU timed by a latency table) is refused: this
    board has no table.

    cores is 1 or 2, not yet with switch_cpu. On the arm-m4-proxy, 2 adds
    the RP2350's second M33 (board.cpu1, of the same kind as cpu), which
    starts from the firmware's CORE1_LAUNCH_SYMBOL. On the hazard3-proxy, 2
    is arcsim-soc's dual-core soc.v: the cores share the memory system and
    devices; each has its own routers, interrupt controller and testbench
    soft and timer IRQ. Core 1 boots with core 0 and, under Hazard3's
    init.S, sleeps until core 0 starts it."""
    sram_model, dma, xip_model = memory_models(model, sram_model, dma,
                                                xip_model)
    if sram_model not in ("legacy", "banked"):
        raise ValueError(f"Unknown SRAM model: {sram_model}")
    if xip_model not in XIP_MODELS:
        raise ValueError(f"Unknown XIP model: {xip_model}")
    if cpu not in ("minor", "atomic"):
        raise ValueError(f"Unknown CPU: {cpu}")
    if hwpe not in HWPES:
        raise ValueError(f"HWPE {hwpe} is not modelled in gem5 (modelled: "
                         f"{', '.join(HWPES)})")
    if zfinx:
        raise ValueError("Zfinx is not modelled in gem5 (gem5's Hazard3 "
                         "has F and D)")
    if not sram4_7 and core != "hazard3-proxy":
        raise ValueError("sram4_7 False is only for the hazard3-proxy")
    if switch_cpu and (core not in ("arm-m4-proxy", "hazard3-proxy") or
                       switch_cpu not in ("minor", "atomic")):
        raise ValueError("switch_cpu is minor or atomic, on the arm-m4-proxy "
                         "or the hazard3-proxy")
    if cores not in (1, 2):
        raise ValueError("cores is 1 or 2")
    if cores == 2 and switch_cpu:
        raise ValueError("cores 2 is without switch_cpu")
    if inst_latency:
        raise ValueError("inst_latency is not available on this board")
    if core == "arm-m4-proxy":
        return _make_arm(firmware, xip_miss_ns=xip_miss_ns,
                         sram_latency_ns=sram_latency_ns,
                         scratch_bank_bandwidth=scratch_bank_bandwidth,
                         arm_predictor=arm_predictor,
                         arm_divider=arm_divider, sram_model=sram_model,
                         arm_timing=arm_timing, dma=dma,
                         xip_model=xip_model, cpu=cpu,
                         switch_cpu=switch_cpu, clock=clock,
                         cores=cores)
    if core == "hazard3-proxy":
        return _make_riscv(firmware, xip_miss_ns=xip_miss_ns,
                           sram_latency_ns=sram_latency_ns,
                           scratch_bank_bandwidth=scratch_bank_bandwidth,
                           sram_model=sram_model, dma=dma,
                           xip_model=xip_model, cpu=cpu, hwpe=hwpe,
                           switch_cpu=switch_cpu,
                           clock=clock, cores=cores,
                           sram4_7=sram4_7)
    raise ValueError(f"Unknown RP2350 core selector: {core}")  
