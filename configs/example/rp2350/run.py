"""Run a bare-metal RP2350 benchmark with an Arm or RISC-V core proxy."""

import argparse
import json
import sys
from pathlib import Path

import m5
from m5.objects import Root
from m5.stats import dump as stats_dump
from m5.stats import reset as stats_reset

from board import HWPES, SCRATCH_BANK_BANDWIDTH, make_board, memory_models
from soc_configs import SOC_CONFIGS, list_configs, manifest

# Firmware output (Hazard3TbIO) goes straight to the file descriptor; line
# buffering keeps this script's prints in order with it under -r.
sys.stdout.reconfigure(line_buffering=True)

parser = argparse.ArgumentParser()
parser.add_argument("--core", choices=("arm-m4-proxy", "hazard3-proxy"),
                    help="Required, except with --soc-config "
                    "(hazard3-proxy) or --list-configs")
parser.add_argument("--firmware", type=Path,
                    help="Required, except with --list-configs")
parser.add_argument("--soc-config", choices=tuple(SOC_CONFIGS),
                    help="arcsim-soc's SoC config (soc/rtl-sim/soc-configs): "
                    "the hazard3-proxy with its cores, HWPE, FPU and SRAM4-7 "
                    "(soc_configs.py), in place of --cores, --hwpe, --zfinx "
                    "and --no-sram4-7")
parser.add_argument("--list-configs", action="store_true",
                    help="List the SoC configs (--soc-config) and exit")
parser.add_argument("--cpu", choices=("minor", "atomic"), default="minor",
                    help="minor is the timed core (CortexM33CPU, or the "
                    "Hazard3 proxy's MinorCPU); atomic is gem5's atomic CPU, "
                    "an instruction-level simulator with no core timing, "
                    "for which the --arm-* options do nothing")
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
parser.add_argument("--xip-model", choices=("legacy", "qmi", "soc"),
                    help="qmi selects the cache banks and flash interface "
                    "timing of a Pico 2; soc (hazard3-proxy) arcsim-soc's RTL "
                    "code memory, zero wait states; the XIP knobs apply only "
                    "to legacy")
parser.add_argument("--hwpe", choices=tuple(HWPES),
                    help="The HWPE on the Hazard3 board, as arcsim-soc's "
                    "CONFIG_HWPE; empty is no HWPE and no TCDM, as "
                    "arcsim-soc's configs without an accelerator "
                    "(default)")
parser.add_argument("--no-sram4-7", dest="sram4_7", action="store_false",
                    default=None,
                    help="Without SRAM4-7 on the hazard3-proxy "
                    "(0x20040000-0x2007ffff fault), as arcsim-soc's "
                    "SRAM4_7_PRESENT=0")
parser.add_argument("--hwpe-timed-in-atomic", action="store_true",
                    help="With --cpu atomic, the HWPE's jobs still take the cycles "
                    "they take with a timing CPU")
parser.add_argument("--zfinx", action="store_true", default=None,
                    help="Zfinx on the hazard3-proxy: floats in the x "
                    "registers instead of F and D")
parser.add_argument("--cores", type=int, choices=(1, 2),
                    help="2: on the arm-m4-proxy, the second M33, which starts "
                    "from the firmware's arcsim_core1_launch; on the "
                    "hazard3-proxy, arcsim-soc's dual-core soc.v, core 1 "
                    "waiting in init.S until core 0 starts it "
                    "(default 1)")
parser.add_argument("--inst-latency", action="store_true",
                    help="with --cpu atomic: each instruction's cycles from "
                    "the core's latency table (the M33's or Hazard3's), with "
                    "RP2350XIP's flash misses")
parser.add_argument("--tick-limit", type=int, default=100_000_000_000)
args = parser.parse_args()
if args.list_configs:
    print(list_configs())
    sys.exit(0)
# The system: from the SoC config, or from --cores, --hwpe, --zfinx and
# --no-sram4-7 (None: not given)
system_args = ("cores", "hwpe", "zfinx", "sram4_7")
if args.soc_config:
    given = [name for name in system_args if getattr(args, name) is not None]
    if given:
        parser.error("--soc-config sets the system: not with "
                     + ", ".join("--no-sram4-7" if name == "sram4_7" else
                                 f"--{name}" for name in given))
    if args.core not in (None, "hazard3-proxy"):
        parser.error("--soc-config is for the hazard3-proxy")
    args.core = "hazard3-proxy"
    for name in system_args:
        setattr(args, name, SOC_CONFIGS[args.soc_config][name])
else:
    defaults = dict(cores=1, hwpe="empty", zfinx=False, sram4_7=True)
    for name in system_args:
        if getattr(args, name) is None:
            setattr(args, name, defaults[name])
if args.core is None:
    parser.error("--core is required (or --soc-config)")
if args.firmware is None:
    parser.error("--firmware is required")
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
    hwpe=args.hwpe,
    hwpe_timed_in_atomic=args.hwpe_timed_in_atomic,
    zfinx=args.zfinx,
    sram4_7=args.sram4_7,
    cpu=args.cpu,
    cores=args.cores,
    inst_latency=args.inst_latency,
)
if args.xip_model == "legacy":
    board.xip_cache.tag_latency = args.xip_hit_latency_cycles
    board.xip_cache.data_latency = args.xip_hit_latency_cycles
    if args.xip_replacement == "random":
        from m5.objects import RandomRP
        board.xip_cache.replacement_policy = RandomRP()
board.exit_on_work_items = True
# What was simulated, beside stats.txt and config.ini: the SoC config (null
# without --soc-config), the system it set, its manifest values and how it
# differs from the config on rtl-sim
soc_config = {
    "soc_config": args.soc_config,
    "core": args.core,
    "cores": args.cores,
    "hwpe": args.hwpe,
    "zfinx": args.zfinx,
    "sram4_7": args.sram4_7,
    "manifest": manifest(args.soc_config) if args.soc_config else None,
    "differs": (SOC_CONFIGS[args.soc_config]["differs"]
                if args.soc_config else None),
}
with open(Path(m5.options.outdir) / "soc-config.json", "w") as f:
    json.dump(soc_config, f, indent=2)
    f.write("\n")
root = Root(full_system=True, system=board)
m5.instantiate()
if args.soc_config:
    print(f"SoC config={args.soc_config}"
          + (f" (gem5 differs: {soc_config['differs']})"
             if soc_config["differs"] else ""))
print(f"RP2350 core={args.core} cpu={args.cpu} cores={args.cores} "
      "clock=150MHz")
print("XIP=0x10000000+16MiB cache=16KiB/2-way/8B")
if args.xip_model == "qmi":
    print("XIP model=qmi: two cache banks, flash interface timing of a Pico 2")
else:
    print(f"XIP cache tag/data latency={args.xip_hit_latency_cycles} cycles; "
          f"replacement={args.xip_replacement}; "
          f"QMI delay={args.xip_miss_ns} ns")
if args.sram4_7:
    print("SRAM=0x20000000+512KiB group windows, 0x20080000/0x20081000+4KiB")
else:
    print("SRAM=0x20000000+256KiB group window (SRAM4-7 absent), "
          "0x20080000/0x20081000+4KiB")
if args.sram_model == "banked":
    print("SRAM model=banked: ten banks, 32-bit grants at 150MHz, zero wait states")
else:
    print(f"SRAM model=legacy; scratch bank effective bandwidth={args.scratch_bank_bandwidth}")
if args.core == "hazard3-proxy":
    if args.hwpe == "empty":
        print("HWPE=empty: no HWPE, no TCDM")
    else:
        print(f"HWPE={args.hwpe} at 0x50800000, TCDM=0x60000000+256KiB")
    print(f"FPU={'zfinx' if args.zfinx else 'f+d'}")
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
