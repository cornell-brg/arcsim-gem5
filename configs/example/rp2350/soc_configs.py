"""arcsim-soc's SoC configs (soc/rtl-sim/soc-configs/<name>.mk) on gem5's
Hazard3 board: run.py --soc-config <name> sets the board's cores, HWPE, FPU
and SRAM4-7 from here.

Each entry: desc (as the .mk's SOC_CONFIG_DESC), cores (N_HARTS), hwpe
(CONFIG_HWPE; empty is no HWPE and no TCDM, as XCEL_PRESENT=0), zfinx (FPU
zfinx; otherwise gem5's Hazard3 has F and D), sram4_7 (SRAM4_7_PRESENT), and
differs: how the gem5 board differs from the config on rtl-sim.

Imports nothing from gem5, so it also runs on its own: python3 soc_configs.py
lists the configs.
"""

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


if __name__ == "__main__":
    print(list_configs())
