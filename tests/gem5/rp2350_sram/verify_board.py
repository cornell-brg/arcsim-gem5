"""Compile/run existing board smoke firmware in scratch and striped SRAM."""
import argparse
from pathlib import Path
import subprocess
import tempfile

repo = Path(__file__).resolve().parents[3]
parser = argparse.ArgumentParser()
parser.add_argument("--isa", choices=("ARM", "RISCV"), required=True)
parser.add_argument("--compiler", type=Path)
parser.add_argument("--outdir", type=Path)
args = parser.parse_args()
output = args.outdir or Path(tempfile.mkdtemp(prefix="rp2350-sram-board-"))
output.mkdir(parents=True, exist_ok=True)
smoke = repo / "configs/example/rp2350/smoke"
if args.isa == "ARM":
    arch = "arm"
    core = "arm-m4-proxy"
    compiler = args.compiler or repo.parent / "toolchains/gcc-arm-embedded/Payload/bin/arm-none-eabi-gcc"
    flags = ["-mcpu=cortex-m4", "-mthumb"]
else:
    arch = "riscv"
    core = "hazard3-proxy"
    compiler = args.compiler or repo.parent / "toolchains/riscv-toolchain-16/bin/riscv32-pico-elf-gcc"
    flags = ["-march=rv32imac_zicsr", "-mabi=ilp32"]
for placement in ("scratch", "striped"):
    linker = output / f"{placement}.ld"
    script = (smoke / f"{arch}.ld").read_text()
    if placement == "striped":
        script = script.replace("0x20080000", "0x20000000").replace("0x20080080", "0x20000080")
    linker.write_text(script)
    elf = output / f"{placement}.elf"
    subprocess.run([str(compiler.resolve()), *flags, "-nostdlib", f"-Wl,-T,{linker}",
                    str(smoke / f"{arch}.S"), "-o", str(elf)], check=True,
                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30)
    for model in ("legacy", "banked"):
        directory = output / f"{placement}-{model}"
        result = subprocess.run([
            str(repo / f"build/{args.isa}/gem5.fast"), "-d", str(directory),
            str(repo / "configs/example/rp2350/run.py"), "--core", core,
            "--firmware", str(elf), "--sram-model", model,
            "--tick-limit", "100000000",
        ], text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60)
        directory.mkdir(parents=True, exist_ok=True)
        (directory / "run.log").write_text(result.stdout)
        assert result.returncode == 0, result.stdout
        assert f"RP2350_{args.isa}_SMOKE_OK" in result.stdout, result.stdout
        assert "SMOKE_FAIL" not in result.stdout, result.stdout
        assert "semi:ADP_Stopped_ApplicationExit" in result.stdout, result.stdout
        if model == "banked":
            stats = {parts[0]: float(parts[1]) for line in
                     (directory / "stats.txt").read_text().splitlines()
                     if len(parts := line.split()) >= 2 and
                     parts[0].startswith("system.sram.banks.")}
            banks = range(4) if placement == "striped" else (8,)
            assert sum(stats[f"system.sram.banks.instructionGrants::sram{bank}"]
                       for bank in banks) > 0
        print(f"PASS {args.isa} {placement} {model}")
print(f"Results: {output}")
