"""Run firmware on the RP2350 board's Arm proxy with a slow SIO.

    gem5.opt run_rp2350_slow_sio.py --firmware <elf> [--sio-latency 200ns]

A long SIO latency makes any reordering between SIO accesses and GPIO
coprocessor instructions visible to a test (test_rp2350_order).
"""

import argparse
import os
import sys

import m5
from m5.objects import Root

parser = argparse.ArgumentParser()
parser.add_argument("--firmware", required=True)
parser.add_argument("--sio-latency", default="200ns")
args = parser.parse_args()

sys.path.insert(
    0,
    os.path.join(
        os.path.dirname(os.path.abspath(__file__)),
        "..", "..", "..", "..", "configs", "example", "rp2350",
    ),
)
from board import make_board  # noqa: E402

board = make_board("arm-m4-proxy", args.firmware)
board.sio.pio_latency = args.sio_latency
root = Root(full_system=True, system=board)
m5.instantiate()
event = m5.simulate()
print(f"Exiting @ tick {m5.curTick()} because {event.getCause()}")
