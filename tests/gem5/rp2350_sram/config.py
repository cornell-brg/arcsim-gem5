"""Deterministic and randomized SRAM regressions; run via verify.py."""
import argparse

import m5
from m5.objects import (
    MemTest, PyTrafficGen, Root, RP2350SRAM, RP2350SRAMTest,
    SrcClockDomain, System, VoltageDomain,
)

parser = argparse.ArgumentParser()
parser.add_argument("--scenario", required=True, choices=(
    "same", "different", "priority", "split", "data", "backpressure",
    "map", "random", "byte", "half", "atomic",
))
args = parser.parse_args()
system = System()
system.clk_domain = SrcClockDomain(clock="150MHz", voltage_domain=VoltageDomain())
system.mem_mode = "timing"
system.cache_line_size = 8
system.sram = RP2350SRAM(
    queue_depth=1 if args.scenario in ("backpressure", "random") else 8,
    port_priority=[0, 1, 0] if args.scenario == "priority" else [0],
)
system.mem_ranges = [system.sram.range]
if args.scenario == "random":
    system.testers = [MemTest(
        base_addr_1=0x20000000, base_addr_2=0x20040000,
        uncacheable_base_addr=0x20010000, size=4096,
        percent_functional=30,
        max_loads=2000, progress_interval=1000,
    ) for _ in range(2)]
    for tester in system.testers:
        tester.port = system.sram.port
elif args.scenario == "map":
    system.generators = [PyTrafficGen() for _ in range(10)]
    for generator in system.generators:
        generator.port = system.sram.port
else:
    system.tester = RP2350SRAMTest(scenario=args.scenario)
    system.tester.port = system.sram.port
    system.tester.port = system.sram.port
system.system_port = system.sram.port
root = Root(full_system=False, system=system)
m5.instantiate()


def traffic(generator, base):
    yield generator.createLinear(1_000_000, base, base + 4, 4,
                                 10_000, 10_000, 100, 64)
    yield generator.createIdle(100_000)
    yield generator.createExit(0)


if args.scenario == "map":
    addresses = ([0x20000000 + i * 4 for i in range(4)] +
                 [0x20040000 + i * 4 for i in range(4)] +
                 [0x20080000, 0x20081000])
    for generator, base in zip(system.generators, addresses):
        generator.start(traffic(generator, base))
event = m5.simulate(100_000_000)
cause = event.getCause()
if args.scenario == "random":
    assert cause == "maximum number of loads reached", cause
elif args.scenario == "map":
    assert "exit state" in cause, cause
else:
    assert cause == "RP2350_SRAM_TEST_PASS " + args.scenario, cause
print(f"RP2350_SRAM_PASS {args.scenario} tick={m5.curTick()} cause={cause}")
