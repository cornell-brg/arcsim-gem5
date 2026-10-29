"""An HWPE model alone on a TCDM, running the jobs of an RTL testbench's
trace (arcsim-xcel's tb-utils/hwpeTraceMonitor.sv) through HWPEReplay. Run
it with --debug-flags=HWPETrace to get the model's trace in the same form:

  gem5.opt --debug-flags=HWPETrace --debug-file=hwpe.txt \\
      configs/example/hwpe/replay.py --hwpe HWPELk --trace lk-trace.txt
"""

import argparse
import ast

import m5
from m5.objects import *

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--hwpe", required=True, help="The HWPE model's class")
parser.add_argument(
    "--param",
    action="append",
    default=[],
    metavar="NAME=VALUE",
    help="A parameter of the HWPE model (a Python literal or a string)",
)
parser.add_argument("--trace", required=True, help="The RTL trace")
parser.add_argument("--clock", default="100MHz")
args = parser.parse_args()


def value(v):
    try:
        return ast.literal_eval(v)
    except (ValueError, SyntaxError):
        return v


system = System()
system.clk_domain = SrcClockDomain(
    clock=args.clock, voltage_domain=VoltageDomain()
)
system.mem_mode = "timing"
# The testbenches' TCDM addresses start at 0
system.tcdm = HCITcdm(range=AddrRange(0, size="256KiB"))
system.mem_ranges = [system.tcdm.range]
system.system_port = system.tcdm.core_port

params = dict(p.split("=", 1) for p in args.param)
system.hwpe = getattr(m5.objects, args.hwpe)(
    pio_latency="1ns", **{k: value(v) for k, v in params.items()}
)
system.hwpe.tcdm_port = system.tcdm.hwpe_port

system.replay = HWPEReplay(trace=args.trace, hwpe_base=system.hwpe.pio_addr)
system.replay.hwpe_port = system.hwpe.pio
system.replay.mem_port = system.tcdm.core_port
system.hwpe.irq = system.replay.irq

root = Root(full_system=False, system=system)
m5.instantiate()
event = m5.simulate()
print(f"{event.getCause()} at cycle {m5.curTick() // system.clk_domain.clock[0].getValue()}")
