"""arcsim-soc's SoC configs (soc/rtl-sim/soc-configs/<name>.mk) on gem5's
Hazard3 board: run.py --soc-config <name> sets the board's cores, HWPE, FPU
and SRAM4-7 from here.

Each entry: desc (as the .mk's SOC_CONFIG_DESC), cores (N_HARTS), hwpe
(CONFIG_HWPE; empty is no HWPE and no TCDM, as XCEL_PRESENT=0), zfinx (FPU
zfinx; otherwise gem5's Hazard3 has F and D), sram4_7 (SRAM4_7_PRESENT), and
differs: how the gem5 board differs from the config on rtl-sim.

Imports nothing from gem5, so it also runs on its own:
  python3 soc_configs.py                  lists the configs
  python3 soc_configs.py --names          only their names
  python3 soc_configs.py --check <dir>    compares them with soc's
                                          rtl-sim/soc-configs/*.mk in <dir>
  python3 soc_configs.py --manifests <dir> --gem5 <gem5.opt> [--tick-limit n]
                                          writes <dir>/gem5-<name>/soc-config.cmake
                                          for each, which soc's tests read
"""

import argparse
import re
import sys
from pathlib import Path

NO_FPU = "F and D (rtl-sim: no FPU)"

SOC_CONFIGS = {
    "single-core": dict(
        desc="One Hazard3, no accelerator",
        cores=1, hwpe="empty", zfinx=False, sram4_7=True, differs=NO_FPU),
    "single-core-vvadd-xcel": dict(
        desc="One Hazard3, vvadd accelerator",
        cores=1, hwpe="vvadd", zfinx=False, sram4_7=False, differs=NO_FPU),
    "single-core-zfinx-fpu": dict(
        desc="One Hazard3 with Zfinx FPU, no accelerator",
        cores=1, hwpe="empty", zfinx=True, sram4_7=True, differs=""),
    "single-core-zfinx-fpu-fast-cell-xcel": dict(
        desc="One Hazard3 with Zfinx FPU, FAST cell accelerator",
        cores=1, hwpe="fast-with-scoring-wide-multicycle-cell-best-k",
        zfinx=True, sram4_7=False, differs=""),
    "single-core-zfinx-fpu-optical-flow-xcel": dict(
        desc="One Hazard3 with Zfinx FPU, optical-flow accelerator",
        cores=1, hwpe="optical-flow", zfinx=True, sram4_7=False, differs=""),
    "single-core-zfinx-fpu-optical-flow-rotated-xcel": dict(
        desc="One Hazard3 with Zfinx FPU, optical-flow accelerator with the "
             "rotated LK engine",
        cores=1, hwpe="optical-flow-rotated", zfinx=True, sram4_7=False,
        differs=""),
    "dual-core": dict(
        desc="Two Hazard3, no accelerator",
        cores=2, hwpe="empty", zfinx=False, sram4_7=True, differs=NO_FPU),
    "dual-core-vvadd-xcel": dict(
        desc="Two Hazard3, vvadd accelerator",
        cores=2, hwpe="vvadd", zfinx=False, sram4_7=False, differs=NO_FPU),
    "dual-core-zfinx-fpu": dict(
        desc="Two Hazard3 with Zfinx FPU, no accelerator",
        cores=2, hwpe="empty", zfinx=True, sram4_7=True, differs=""),
    "dual-core-zfinx-fpu-fast-cell-xcel": dict(
        desc="Two Hazard3 with Zfinx FPU, FAST cell accelerator",
        cores=2, hwpe="fast-with-scoring-wide-multicycle-cell-best-k",
        zfinx=True, sram4_7=False, differs=""),
    "dual-core-zfinx-fpu-optical-flow-xcel": dict(
        desc="Two Hazard3 with Zfinx FPU, optical-flow accelerator",
        cores=2, hwpe="optical-flow", zfinx=True, sram4_7=False, differs=""),
}


def manifest(name):
    """The gem5 board's manifest values for SoC config name, with the keys
    of soc's manifests (soc-config.cmake's SOC_<key>)."""
    c = SOC_CONFIGS[name]
    return {
        "CONFIG": name,
        "SIM": "gem5",
        "CONFIG_HWPE": c["hwpe"],
        "FPU": "zfinx" if c["zfinx"] else "f",
        "N_HARTS": str(c["cores"]),
        "XCEL_PRESENT": "0" if c["hwpe"] == "empty" else "1",
        "SRAM4_7_PRESENT": "1" if c["sram4_7"] else "0",
    }


def list_configs():
    """The table as run.py --list-configs prints it."""
    lines = []
    for name, c in SOC_CONFIGS.items():
        lines.append(f"{name:42} {c['desc']}")
        lines.append(f"{'':42} --cores {c['cores']} --hwpe {c['hwpe']}"
                     + (" --zfinx" if c["zfinx"] else "")
                     + ("" if c["sram4_7"] else " --no-sram4-7"))
        if c["differs"]:
            lines.append(f"{'':42} gem5 differs: {c['differs']}")
    return "\n".join(lines)


def check(soc_configs_dir):
    """The differences between this table and soc's SoC configs
    (<soc_configs_dir>/<name>.mk), as messages; none if they agree."""
    mks = {}
    for mk in sorted(Path(soc_configs_dir).glob("*.mk")):
        mks[mk.stem] = dict(re.findall(r"^(\w+)\s*:=\s*(.*?)\s*$",
                                       mk.read_text(), re.M))
    errors = [f"{name}: in soc ({soc_configs_dir}), not in soc_configs.py"
              for name in mks if name not in SOC_CONFIGS]
    errors += [f"{name}: in soc_configs.py, not in soc ({soc_configs_dir})"
               for name in SOC_CONFIGS if name not in mks]
    for name in SOC_CONFIGS.keys() & mks.keys():
        mk, ours = mks[name], manifest(name)
        expected = {
            "SOC_CONFIG_DESC": SOC_CONFIGS[name]["desc"],
            "CONFIG_HWPE": ours["CONFIG_HWPE"],
            "N_HARTS": ours["N_HARTS"],
            "XCEL_PRESENT": ours["XCEL_PRESENT"],
            "SRAM4_7_PRESENT": ours["SRAM4_7_PRESENT"],
        }
        for key, value in expected.items():
            if mk.get(key) != value:
                errors.append(f"{name}: {key} is {mk.get(key)} in soc, "
                              f"{value} here")
        if (mk.get("FPU") == "zfinx") != SOC_CONFIGS[name]["zfinx"]:
            errors.append(f"{name}: FPU is {mk.get('FPU')} in soc, zfinx "
                          f"{SOC_CONFIGS[name]['zfinx']} here")
    return errors


def write_manifests(directory, gem5, tick_limit, names=None):
    """Write <directory>/gem5-<name>/soc-config.cmake for each SoC config
    in names (default: all): what soc's tests need to know to run a test on
    gem5 with that config (as rtl-sim's Makefile writes for an rtl_sim)."""
    run_py = Path(__file__).resolve().parent / "run.py"
    for name in names or SOC_CONFIGS:
        values = manifest(name)
        keys = {
            "SOC_CONFIG": name,
            "SOC_RUNNER": "gem5",
            "SOC_SIM": str(Path(gem5).resolve()),
            # The arguments after gem5's own (--outdir), before --firmware
            "SOC_GEM5_ARGS": ";".join([
                str(run_py), "--soc-config", name, "--model", "accurate",
                "--sram-model", "banked", "--tick-limit", str(tick_limit)]),
            "SOC_MODEL": "gem5",
            "SOC_TRACE": "none",
            "SOC_CONFIG_VH": "none",
        }
        keys.update({f"SOC_{key}": value for key, value in values.items()
                     if key not in ("CONFIG", "SIM")})
        out = Path(directory) / f"gem5-{name}" / "soc-config.cmake"
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text("".join(f'set({key} "{value}")\n'
                               for key, value in keys.items()))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--names", dest="list_names", action="store_true",
                        help="print only the SoC configs' names")
    parser.add_argument("--check", metavar="DIR",
                        help="compare with soc's rtl-sim/soc-configs")
    parser.add_argument("--manifests", metavar="DIR",
                        help="write the gem5-<name> manifests into DIR")
    parser.add_argument("--gem5", help="gem5 binary (--manifests)")
    parser.add_argument("--tick-limit", type=int, default=100_000_000_000,
                        help="run.py's --tick-limit (--manifests)")
    parser.add_argument("names", nargs="*",
                        help="SoC configs (--manifests; default: all)")
    args = parser.parse_args()
    if args.check:
        errors = check(args.check)
        for error in errors:
            print(f"soc_configs.py: {error}", file=sys.stderr)
        if errors:
            sys.exit(1)
    if args.manifests:
        if not args.gem5:
            parser.error("--manifests needs --gem5")
        unknown = [name for name in args.names if name not in SOC_CONFIGS]
        if unknown:
            parser.error(f"unknown SoC config {' '.join(unknown)} "
                         f"(one of: {' '.join(SOC_CONFIGS)})")
        write_manifests(args.manifests, args.gem5, args.tick_limit,
                        args.names)
    if args.list_names:
        print(" ".join(SOC_CONFIGS))
    elif not (args.check or args.manifests):
        print(list_configs())
