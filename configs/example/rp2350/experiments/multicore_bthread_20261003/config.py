"""Same-ELF multicore RISC-V bare-metal investigation, with shared SRAM.

This is a standalone test platform, not RP2350 boot-ROM emulation. Four
cores are a synthetic configuration; physical RP2350 has two active cores.
"""
import argparse

import m5
from m5.objects import (
    Root, RP2350SRAM, RiscvSystem, RiscvISA, RiscvBareMetal,
    RiscvSemihosting, SrcClockDomain, VoltageDomain,
)
from m5.objects.RiscvCPU import RiscvMinorCPU

parser = argparse.ArgumentParser()
parser.add_argument("--binary", required=True)
parser.add_argument("--cores", type=int, choices=(1, 2, 4), required=True)
args = parser.parse_args()
system = RiscvSystem()
system.clk_domain = SrcClockDomain(clock="150MHz", voltage_domain=VoltageDomain())
system.mem_mode = "timing"
system.cache_line_size = 8
system.sram = RP2350SRAM(clk_domain=system.clk_domain)
system.mem_ranges = [system.sram.range]
system.cpu = [RiscvMinorCPU(cpu_id=i, numThreads=1) for i in range(args.cores)]
for cpu in system.cpu:
    cpu.isa = [RiscvISA(
        riscv_type="RV32", enable_rvv=False, privilege_mode_set="M",
        enable_Zicbom_fs=False, enable_Zicboz_fs=False,
    )]
    cpu.decodeInputWidth = 1
    cpu.executeInputWidth = 1
    cpu.executeIssueLimit = 1
    cpu.executeCommitLimit = 1
    cpu.fetch1LineWidth = 8
    cpu.fetch1LineSnapWidth = 8
    cpu.fetch1FetchLimit = 2
    cpu.createThreads()
    cpu.createInterruptController()
    # Two manager ports per CPU preserve instruction/data parallelism.
    cpu.icache_port = system.sram.port
    cpu.dcache_port = system.sram.port
system.system_port = system.sram.port
system.workload = RiscvBareMetal(
    bootloader=args.binary, semihosting=RiscvSemihosting(),
)
root = Root(full_system=True, system=system)
m5.instantiate()
event = m5.simulate(10_000_000_000)
print(f"BTHREAD_SIM cores={args.cores} tick={m5.curTick()} cause={event.getCause()}")
assert event.getCause() == "semi:ADP_Stopped_ApplicationExit", event.getCause()
