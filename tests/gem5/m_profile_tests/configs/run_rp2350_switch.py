# Runs firmware on the RP2350 board's Arm proxy, switching its M33 between
# gem5's atomic CPU (IL) and the Minor CortexM33CPU (CL) by m5.switchCpus
# every --every-ns of simulated time (none with 0), as core's board_run.py
# switches it, until the firmware exits by semihosting; prints SWITCH_FP_OK
# when it exits as an application with code 0 (test_rp2350_switch_fp.S).
#
#   gem5.opt run_rp2350_switch.py --firmware ELF [--cpu atomic|minor]
#       [--every-ns N] [--inst-latency]

import argparse
import sys
from pathlib import Path

import m5
from m5.objects import Root

sys.path.insert(
    0,
    str(Path(__file__).resolve().parents[4] / "configs" / "example" / "rp2350"),
)
from board import make_board  # noqa: E402

parser = argparse.ArgumentParser()
parser.add_argument("--firmware", type=Path, required=True)
parser.add_argument("--cpu", choices=("atomic", "minor"), default="atomic",
                    help="the core it starts on")
parser.add_argument("--every-ns", type=int, default=0,
                    help="switch every this many ns (0: never)")
parser.add_argument("--inst-latency", action="store_true",
                    help="the atomic core timed by the M33's latency table")
parser.add_argument("--tick-limit", type=int, default=1_000_000_000_000)
args = parser.parse_args()

other = "minor" if args.cpu == "atomic" else "atomic"
board = make_board(
    "arm-m4-proxy", str(args.firmware.resolve()), model="accurate",
    cpu=args.cpu, switch_cpu=other if args.every_ns else None,
    inst_latency=args.inst_latency,
)
board.exit_on_work_items = True
root = Root(full_system=True, system=board)
m5.instantiate()

period = args.every_ns * 1000
current, waiting = board.cpu, getattr(board, "switch_cpu", None)
switches = 0
code = 0
while True:
    left = args.tick_limit - m5.curTick()
    if left <= 0:
        print(f"Tick limit reached @ {m5.curTick()} without guest exit")
        code = -1
        break
    event = m5.simulate(min(period, left) if period else left)
    cause = event.getCause()
    if cause == "simulate() limit reached":
        if period:
            m5.switchCpus(board, [(current, waiting)])
            current, waiting = waiting, current
            switches += 1
        continue
    code = event.getCode()
    print(f"Exiting @ tick {m5.curTick()} because {cause} after {switches} "
          f"switches, code {code:#x}")
    # test_rp2350_switch_fp: its result's difference from the expected
    ok = cause == "semi:ADP_Stopped_ApplicationExit" and code == 0
    print("SWITCH_FP_OK" if ok else "SWITCH_FP_FAIL")
    code = 0 if ok else 1
    break
sys.exit(code)
