"""Run firmware on the RP2350 board's Arm proxy with its DMA, playing a
peripheral on one of the DMA's data request lines.

    gem5.opt run_rp2350_dma_chain.py --firmware <elf>

Sends test_rp2350_dma_chain's pulses on line 5, 2 ms apart: 10, then 8, then
16 for each of 6 stripes.
"""

import argparse
import os
import sys

import m5
from m5.objects import Root

parser = argparse.ArgumentParser()
parser.add_argument("--firmware", required=True)
parser.add_argument(
    "--sram-model", choices=("legacy", "banked"), default="legacy"
)
parser.add_argument("--cpu", default=None, help="make_board's cpu")
args = parser.parse_args()

sys.path.insert(
    0,
    os.path.join(
        os.path.dirname(os.path.abspath(__file__)),
        "..", "..", "..", "..", "configs", "example", "rp2350",
    ),
)
from board import make_board  # noqa: E402

LINE = 5
GAP = 2 * 10**9  # 2 ms in ticks

extra = {"cpu": args.cpu} if args.cpu else {}
board = make_board(
    "arm-m4-proxy", args.firmware, sram_model=args.sram_model, dma=True,
    **extra,
)
root = Root(full_system=True, system=board)
m5.instantiate()
event = m5.simulate(GAP)
for pulses in [10, 8] + [16] * 6:
    if event.getCause() != "simulate() limit reached":
        break
    board.dma.dreq(LINE, pulses)
    event = m5.simulate(GAP)
if event.getCause() == "simulate() limit reached":
    event = m5.simulate(10 * GAP)
print(f"Exiting @ tick {m5.curTick()} because {event.getCause()}")
