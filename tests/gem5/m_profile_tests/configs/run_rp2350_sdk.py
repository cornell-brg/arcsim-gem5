"""Run Pico SDK firmware on the RP2350 board's Arm proxy.

    gem5.opt run_rp2350_sdk.py --firmware <elf> [--sram-model legacy|banked]

The board has no APB or AHB peripherals. The SDK's start-up still writes a
few of their registers (the boot locks, pads), so both regions get
stand-ins that read 0 and ignore writes, as arcsim-bench's runner gives
them. The firmware's semihosting output is this run's output.
"""

import argparse
import os
import sys

import m5
from m5.objects import (
    IsaFake,
    Root,
)

parser = argparse.ArgumentParser()
parser.add_argument("--firmware", required=True)
parser.add_argument(
    "--sram-model", choices=("legacy", "banked"), default="legacy"
)
parser.add_argument("--tick-limit", type=int, default=10**12)
args = parser.parse_args()

sys.path.insert(
    0,
    os.path.join(
        os.path.dirname(os.path.abspath(__file__)),
        "..", "..", "..", "..", "configs", "example", "rp2350",
    ),
)
from board import make_board  # noqa: E402

board = make_board(
    "arm-m4-proxy", args.firmware, sram_model=args.sram_model
)
board.stand_in_apb = IsaFake(
    pio_addr=0x40000000, pio_size=0x10000000, ret_data32=0, pio_latency="1ns"
)
board.stand_in_ahb = IsaFake(
    pio_addr=0x50000000, pio_size=0x10000000, ret_data32=0, pio_latency="1ns"
)
for stand_in in (board.stand_in_apb, board.stand_in_ahb):
    stand_in.pio = board.sram_bus.mem_side_ports
root = Root(full_system=True, system=board)
m5.instantiate()
event = m5.simulate(args.tick_limit)
print(f"Exiting @ tick {m5.curTick()} because {event.getCause()}")
