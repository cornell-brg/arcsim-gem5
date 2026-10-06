# run_hello — minimal Cortex-M4 "hello world" for gem5

A from-scratch bare-metal program (no OS, no libc) that prints a
greeting and a computed value, then exits — meant as the smallest
possible example of building your own firmware for gem5's Cortex-M4
model, separate from the existing test/benchmark suites.

## Files

| File           | What it is                                                          |
|----------------|----------------------------------------------------------------------|
| `startup.S`    | Vector table + `Reset_Handler` (copies `.data`, zeros `.bss`, calls `main`) |
| `cortexm4.ld`  | Linker script — flash @ `0x08000000`, SRAM @ `0x20000000` (STM32F405 map) |
| `main.c`       | The program: prints via semihosting, computes a sum, exits         |
| `Makefile`     | Builds `hello.elf` with `arm-none-eabi-gcc`                        |
| `run_hello.py` | gem5 config script — boots `STM32F405Platform` and runs the ELF    |

## Prerequisites (on your server)

1. **ARM embedded toolchain** (not installed on this machine, hence why
   this wasn't built/run here):
   ```bash
   sudo apt install gcc-arm-none-eabi
   ```
2. **gem5 built for ARM**, from the `gem5/` repo (this hasn't been
   built here yet either):
   ```bash
   cd gem5
   scons build/ARM/gem5.opt -j$(nproc)
   ```
   (First build takes a while — ARM ISA + Ruby protocol code is a lot
   to compile. `gem5.opt` will appear at `gem5/build/ARM/gem5.opt`.)

## Build the firmware

```bash
cd run_hello
make
```

This produces `hello.elf` (and `hello.dump`, a disassembly you can
read with e.g. `less hello.dump` to see exactly what code was
generated — useful for sanity-checking before you trust cycle counts
later).

## Run it

From wherever your `gem5/` checkout is, invoke `gem5.opt` with
`run_hello.py` as the config script and point `--firmware` at the
ELF you just built:

```bash
cd gem5
./build/ARM/gem5.opt -re ./run_hello/run_hello.py --firmware ./run_hello/hello.elf --cpu-type minor
```

(Adjust the relative paths if your directory layout differs — the
important thing is `run_hello.py`'s `--firmware` argument pointing at
`hello.elf`.)

### Expected output

```
Starting simulation: firmware=../run_hello/hello.elf
Hello from Cortex-M4 on gem5!
Sum of 1..10 = 55
Exiting.
Exiting @ tick <N> because semi:ADP_Stopped_ApplicationExit
```

The three "Hello.../Sum.../Exiting." lines come from the firmware's
`SYS_WRITE0` semihosting calls; gem5 relays them straight to stdout
(and, because of `-re`, also into `m5out_hello/simout.txt`).

### Options

- `--cpu-type atomic|timing|minor` — CPU model (default `minor`, the
  timing-accurate pipeline; `atomic` is fastest for a pure
  correctness check with no cycle counts).
- `--debug-flags Exec` — trace every instruction executed (verbose;
  useful if something doesn't work and you want to see exactly what
  the CPU is doing). Any gem5 debug flag works, e.g.
  `--debug-flags MinorExecute,MinorMem` for more Minor-pipeline detail.
- `--tick-limit <N>` — safety cutoff in case the program doesn't exit
  on its own (not needed here since `main()` calls `semi_exit()`).

## Extending this

- To print more values, reuse `print_u32()` in `main.c`, or add a hex
  printer the same way.
- To do real work worth timing, wrap the region you care about in
  `m5_work_begin()`/`m5_work_end()` (see
  `stm32-board-microbenchmarks/microbenchmark/include/m5ops_semi.h`
  for the pseudo-op calling convention) and drive it with
  `gem5/run_m5op_bench.py` instead of `run_hello.py` — that script
  already knows how to bracket a ROI, dump stats, and report cycles.
- If you want real `printf()` instead of hand-rolled `semi_write0()`
  calls, you'd add an `int _write(int fd, char *buf, int len)` that
  loops the same `SYS_WRITE0`/`SYS_WRITEC` semihosting calls per byte,
  and drop `-nostdlib` (link libc, but still supply your own syscall
  stubs) — nothing in the existing gem5/microbenchmark codebase does
  this today, everyone calls semihosting directly.
