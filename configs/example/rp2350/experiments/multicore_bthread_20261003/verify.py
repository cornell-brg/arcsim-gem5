"""Build one ELF per core-count configuration, shared by every core in it."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile

here = Path(__file__).resolve().parent
repo = here.parents[4]
parser = argparse.ArgumentParser()
parser.add_argument("--compiler", type=Path, default=repo.parent /
                    "toolchains/riscv-toolchain-16/bin/riscv32-pico-elf-gcc")
parser.add_argument("--simulator", type=Path, default=repo / "build/RISCV/gem5.fast")
parser.add_argument("--outdir", type=Path)
args = parser.parse_args()
output = args.outdir or Path(tempfile.mkdtemp(prefix="gem5-bthread-"))
output.mkdir(parents=True, exist_ok=True)
results = []
for count in (1, 2, 4):
    elf = output / f"bthread-{count}.elf"
    compile_command = [str(args.compiler.resolve()), "-march=rv32imac_zicsr",
                       "-mabi=ilp32", "-O2", "-ffreestanding", "-fno-builtin",
                       "-nostdlib", "-nostartfiles", "-Wall", "-Wextra", "-Werror",
                       f"-DBTHREAD_NUM_CORES={count}", f"-Wl,-T,{here / 'link.ld'}",
                       str(here / "start.S"), str(here / "main.c"), "-o", str(elf)]
    subprocess.run(compile_command, check=True, stdout=subprocess.PIPE,
                   stderr=subprocess.PIPE, timeout=30)
    directory = output / f"cores-{count}"
    command = [str(args.simulator.resolve()), "-d", str(directory),
               str(here / "config.py"), "--binary", str(elf), "--cores", str(count)]
    run = subprocess.run(command, text=True, stdout=subprocess.PIPE,
                         stderr=subprocess.STDOUT, timeout=90)
    directory.mkdir(parents=True, exist_ok=True)
    (directory / "run.log").write_text(run.stdout)
    assert run.returncode == 0, run.stdout
    assert f"BTHREAD_PASS cores={count} waves=8" in run.stdout, run.stdout
    assert "BTHREAD_FAIL" not in run.stdout, run.stdout
    config = json.loads((directory / "config.json").read_text())
    assert len(config["system"]["cpu"]) == count
    stats = {}
    for line in (directory / "stats.txt").read_text().splitlines():
        columns = line.split()
        if len(columns) >= 2 and columns[0].startswith("system."):
            try:
                stats[columns[0]] = float(columns[1])
            except ValueError:
                pass
    assert stats["system.sram.banks.grants::total"] > 0
    if count > 1:
        assert stats["system.sram.banks.contested::total"] > 0
    result = {"cores": count, "status": "pass", "work_waves": 8,
              "bank_grants": stats["system.sram.banks.grants::total"],
              "contested_bank_cycles": stats["system.sram.banks.contested::total"],
              "exit": next(line for line in run.stdout.splitlines()
                           if line.startswith("BTHREAD_SIM"))}
    results.append(result)
    print(f"PASS {count} cores, same ELF/entry, pinned work, private stacks, 8 waves")
(output / "results.json").write_text(json.dumps(results, indent=2) + "\n")
print(f"Results: {output}")
