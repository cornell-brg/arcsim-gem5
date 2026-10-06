"""Run SRAM protocol/data regressions and assert bank-level statistics."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
repo = Path(__file__).resolve().parents[3]
parser.add_argument("--binary", type=Path, default=repo / "build/RISCV/gem5.fast")
parser.add_argument("--outdir", type=Path)
args = parser.parse_args()
output = args.outdir or Path(tempfile.mkdtemp(prefix="rp2350-sram-tests-"))
output.mkdir(parents=True, exist_ok=True)
results = []
for scenario in ("same", "different", "priority", "split", "data", "backpressure", "map", "random", "byte", "half", "atomic"):
    directory = output / scenario
    command = [str(args.binary.resolve()), "-d", str(directory),
               str(Path(__file__).with_name("config.py")), "--scenario", scenario]
    result = subprocess.run(command, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, timeout=60)
    directory.mkdir(parents=True, exist_ok=True)
    (directory / "run.log").write_text(result.stdout)
    assert result.returncode == 0, f"{scenario}: {result.stdout}"
    assert f"RP2350_SRAM_PASS {scenario}" in result.stdout, result.stdout
    stats = {}
    for line in (directory / "stats.txt").read_text().splitlines():
        columns = line.split()
        if len(columns) >= 2 and columns[0].startswith("system."):
            try:
                stats[columns[0]] = float(columns[1])
            except ValueError:
                pass
    prefix = "system.sram.banks."
    if scenario in ("same", "priority", "byte", "half"):
        assert stats[prefix + "grants::sram0"] == 8
        assert stats[prefix + "contested::sram0"] > 0
    if scenario == "different":
        assert stats[prefix + "grants::sram0"] == 4
        assert stats[prefix + "grants::sram1"] == 4
        assert stats[prefix + "contested::total"] == 0
    if scenario in ("split", "backpressure"):
        assert stats[prefix + "splitPackets"] == 4
        assert stats[prefix + "grants::total"] == 8
    if scenario == "backpressure":
        assert stats[prefix + "requestRetries"] > 0
        assert stats[prefix + "responseRetries"] == 1
    if scenario == "map":
        for bank in range(10):
            assert stats[prefix + f"grants::sram{bank}"] == 16
    results.append({"scenario": scenario, "status": "pass",
                    "bank_grants": stats[prefix + "grants::total"]})
    print(f"PASS {scenario}")
(output / "results.json").write_text(json.dumps(results, indent=2) + "\n")
print(f"Results: {output}")
