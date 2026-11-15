# gem5 config script for running the minimal Cortex-M4 "hello world"
# example (or any other bare-metal M-profile ELF) on an STM32F405
# platform model.
#
# Usage (from the gem5 repo root, after `scons build/ARM/gem5.opt`):
#
#   ./build/ARM/gem5.opt -re --outdir=m5out_hello \
#       ./run_hello/run_hello.py \
#       --firmware ./run_hello/hello.elf \
#       [--cpu-type atomic|timing|minor] \
#       [--debug-flags Exec]
#
# -re redirects gem5's stdout (including semihosting SYS_WRITE0 output
# and any --debug-flags trace) into <outdir>/simout.txt as well as the
# terminal.
#
# This is a trimmed-down version of
# gem5/tests/gem5/m_profile_tests/configs/run_m4_test.py — no
# pass/fail result parsing, since this program just prints and exits.

import argparse

import m5
from m5 import debug as m5_debug
from m5.objects import Root
from m5.objects.ArmSemihosting import ArmSemihosting

from gem5.components.boards.arm_m_board import ArmMBoard
from gem5.prebuilt.cortexm.platforms import STM32F405Platform

parser = argparse.ArgumentParser(description="Run a Cortex-M4 ELF on gem5")
parser.add_argument(
    "--firmware", required=True, help="Path to a Cortex-M4 ELF binary"
)
parser.add_argument(
    "--cpu-type",
    choices=["atomic", "timing", "minor"],
    default="minor",
    help="CPU model: atomic, timing, or minor (default: minor)",
)
parser.add_argument(
    "--tick-limit",
    type=int,
    default=20000000,
    help="Maximum simulation ticks (0 = no limit, default is 20000000)",
)
parser.add_argument(
    "--debug-flags",
    type=str,
    default="",
    help="Comma-separated gem5 debug flags to enable for the whole run "
    "(e.g. Exec, MinorExecute). Off by default.",
)
args = parser.parse_args()

# ---------------------------------------------------------------------------
# Platform, CPU, memories, board
# ---------------------------------------------------------------------------
platform = STM32F405Platform()

if args.cpu_type == "timing":
    from m5.objects import ArmMTimingSimpleCPU

    cpu = ArmMTimingSimpleCPU()
elif args.cpu_type == "minor":
    from m5.objects import ArmMMinorCPU

    cpu = ArmMMinorCPU()
else:
    from m5.objects import ArmMAtomicSimpleCPU

    cpu = ArmMAtomicSimpleCPU()

memories = platform.default_memories()

board = ArmMBoard(
    platform=platform,
    cpu=cpu,
    memories=memories,
    clk_freq="168MHz",
)

# Semihosting lets the firmware print (SYS_WRITE0) and exit (SYS_EXIT).
board.semihosting = ArmSemihosting()

board.set_workload(args.firmware)

root = Root(full_system=True, system=board)

for flag_name in (f.strip() for f in args.debug_flags.split(",") if f.strip()):
    if flag_name in m5_debug.flags:
        m5_debug.flags[flag_name].enable()
    else:
        print(f"WARNING: unknown debug flag '{flag_name}'")

m5.instantiate()

print(f"Starting simulation: firmware={args.firmware}")

if args.tick_limit > 0:
    exit_event = m5.simulate(args.tick_limit)
else:
    exit_event = m5.simulate()

print(f"Exiting @ tick {m5.curTick()} because {exit_event.getCause()}")
