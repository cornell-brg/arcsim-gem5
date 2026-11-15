#!/bin/sh
# Builds fast_cycles.c and vvadd_cycles.c into <out> (default build/hwpe-cycles here) with the
# flags and objects of soc's test build (soc's tests must have been built once).
set -e
here=$(cd "$(dirname "$0")" && pwd)
out=${1:-$here/../build/hwpe-cycles}
mkdir -p "$out"
tb=$ARCSIM_SOC_HOME/tests/build
obj=$tb/CMakeFiles/xcel.test_fast_cell.dir
gcc=$(sed -n 's/^\([^ ]*riscv32-unknown-elf-gcc\) .*/\1/p' "$obj/link.txt")
defs=$(sed -n 's/^C_DEFINES = //p' "$obj/flags.make")
incs=$(sed -n 's/^C_INCLUDES = //p' "$obj/flags.make")
flags=$(sed -n 's/^C_FLAGS = //p' "$obj/flags.make")
for p in fast_cycles fast_trace vvadd_cycles; do
  $gcc $defs $incs -I"$ARCSIM_SOC_HOME/tests" $flags -c "$here/$p.c" -o "$out/$p.o"
  link=$(sed -e "s#CMakeFiles/xcel.test_fast_cell.dir/test_fast_cell.c.obj#$out/$p.o#" \
             -e "s#-o xcel/test_fast_cell.elf#-o $out/$p.elf#" "$obj/link.txt")
  (cd "$tb" && eval "$link")
  "${gcc%gcc}objcopy" -O binary "$out/$p.elf" "$out/$p.bin"
done
echo "$out"
