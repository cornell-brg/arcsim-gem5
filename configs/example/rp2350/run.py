"""Run a bare-metal RP2350 benchmark with an Arm or RISC-V core proxy."""

import argparse
import sys
from pathlib import Path

import m5
from m5.objects import Root
from m5.stats import dump as stats_dump
from m5.stats import reset as stats_reset

from board import SCRATCH_BANK_BANDWIDTH, make_board, memory_models

# Firmware output (Hazard3TbIO) goes straight to the file descriptor; line
# buffering keeps this script's prints in order with it under -r.
sys.stdout.reconfigure(line_buffering=True)

parser = argparse.ArgumentParser()
parser.add_argument("--core", choices=("arm-m4-proxy", "hazard3-proxy"),
                    required=True)
parser.add_argument("--firmware", type=Path, required=True)
parser.add_argument("--xip-miss-ns", type=int, default=50)
parser.add_argument("--xip-hit-latency-cycles", type=int, default=1,
                    help="Set both parallel XIP cache tag and data latency, "
                    "in cycles of the cache's 10 GHz clock")
parser.add_argument("--xip-replacement", choices=("lru", "random"),
                    default="lru")
parser.add_argument("--sram-latency-ns", type=int, default=1)
parser.add_argument("--model", choices=("legacy", "accurate"),
                    default="legacy",
                    help="The memory system: accurate is the banked SRAM, the "
                    "DMA controller and the qmi XIP model, timed against a "
                    "Pico 2; --sram-model, --dma and --xip-model each choose "
                    "one part and override it")
parser.add_argument("--sram-model", choices=("legacy", "banked"),
                    help="Banked selects clocked 32-bit SRAM service; legacy timing knobs apply only to legacy")
parser.add_argument("--scratch-bank-bandwidth",
                    default=SCRATCH_BANK_BANDWIDTH)
parser.add_argument("--arm-predictor",
                    choices=("m4", "large-btb", "local-small", "local"),
                    default="m4")
parser.add_argument("--arm-divider", choices=("m4", "rp2350-m33"),
                    default="rp2350-m33")
parser.add_argument("--arm-timing", choices=("tuned", "pre-tuning"),
                    default="tuned",
                    help="CortexM33CPU timing, tuned against a Pico 2, or the "
                    "CortexM4CPU's as it was before its tuning against the "
                    "STM32G474")
parser.add_argument("--dma", action=argparse.BooleanOptionalAction,
                    help="Add the DMA controller")
parser.add_argument("--xip-model", choices=("legacy", "qmi"),
                    help="qmi selects the cache banks and flash interface "
                    "timing of a Pico 2; the XIP knobs apply only to legacy")
parser.add_argument("--tick-limit", type=int, default=100_000_000_000)
args = parser.parse_args()
if not args.firmware.is_file():
    parser.error(f"Firmware ELF does not exist: {args.firmware}")
if (args.xip_miss_ns < 0 or args.sram_latency_ns < 0
        or args.xip_hit_latency_cycles < 0):
    parser.error("Memory delays must be nonnegative")

args.sram_model, args.dma, args.xip_model = memory_models(
    args.model, args.sram_model, args.dma, args.xip_model)
board = make_board(
    args.core, str(args.firmware.resolve()),
    xip_miss_ns=args.xip_miss_ns,
    sram_latency_ns=args.sram_latency_ns,
    scratch_bank_bandwidth=args.scratch_bank_bandwidth,
    arm_predictor=args.arm_predictor,
    arm_divider=args.arm_divider,
    sram_model=args.sram_model,
    arm_timing=args.arm_timing,
    dma=args.dma,
    xip_model=args.xip_model,
)
if args.xip_model == "legacy":
    board.xip_cache.tag_latency = args.xip_hit_latency_cycles
    board.xip_cache.data_latency = args.xip_hit_latency_cycles
    if args.xip_replacement == "random":
        from m5.objects import RandomRP
        board.xip_cache.replacement_policy = RandomRP()
board.exit_on_work_items = True
root = Root(full_system=True, system=board)
m5.instantiate()
print(f"RP2350 core={args.core} cpu_id=0 clock=150MHz")
print("XIP=0x10000000+16MiB cache=16KiB/2-way/8B")
if args.xip_model == "qmi":
    print("XIP model=qmi: two cache banks, flash interface timing of a Pico 2")
else:
    print(f"XIP cache tag/data latency={args.xip_hit_latency_cycles} cycles; "
          f"replacement={args.xip_replacement}; "
          f"QMI delay={args.xip_miss_ns} ns")
print("SRAM=0x20000000+512KiB group windows, 0x20080000/0x20081000+4KiB")
if args.sram_model == "banked":
    print("SRAM model=banked: ten banks, 32-bit grants at 150MHz, zero wait states")
else:
    print(f"SRAM model=legacy; scratch bank effective bandwidth={args.scratch_bank_bandwidth}")
roi_start = None
roi_number = 0
exit_code = 0
while m5.curTick() < args.tick_limit:
    event = m5.simulate(args.tick_limit - m5.curTick())
    cause = event.getCause()
    if cause == "workbegin":
        if roi_start is not None:
            raise RuntimeError("Nested workbegin events are unsupported")
        roi_start = m5.curTick()
        stats_reset()
        print(f"ROI {roi_number} begin={roi_start}")
    elif cause == "workend":
        if roi_start is None:
            raise RuntimeError("workend without workbegin")
        stats_dump()
        print(f"ROI {roi_number} end={m5.curTick()} ticks={m5.curTick()-roi_start}")
        roi_start = None
        roi_number += 1
    elif cause == "simulate() limit reached":
        # Stats are dumped at exit, as for a guest exit.
        print(f"Tick limit reached @ {m5.curTick()} without guest exit")
        # As rtl-sim on a timeout.
        exit_code = -1
        break
    else:
        print(f"Exiting @ tick {m5.curTick()} because {cause}")
        # The guest's code (e.g. Hazard3TbIO's EXIT), as rtl-sim returns it.
        exit_code = event.getCode()
        break
sys.exit(exit_code)
