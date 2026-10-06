# Makefile for the minimal Cortex-M4 "hello world" gem5 example.
#
# Requires: arm-none-eabi-gcc (ARM embedded toolchain)
#   Debian/Ubuntu: apt install gcc-arm-none-eabi
#
# Usage:
#   make            # build hello.elf (+ hello.dump disassembly)
#   make clean

CC      = arm-none-eabi-gcc
OBJDUMP = arm-none-eabi-objdump

CFLAGS  = -mcpu=cortex-m4 -mthumb -mfloat-abi=soft \
          -O1 -g -Wall -Wextra \
          -ffreestanding -fno-builtin

ASFLAGS = -mcpu=cortex-m4 -mthumb -g

# -nostartfiles (we supply our own vector table + Reset_Handler, not
# the toolchain's crt0) but NOT -nostdlib, so libgcc stays linked in
# case the compiler emits any runtime helper calls. -specs=nosys.specs
# stubs out the libc syscalls (_exit, _sbrk, ...) that would otherwise
# be undefined since nothing else provides them. Same combination used
# by gem5/tests/gem5/m_profile_tests/programs/freertos/Makefile.
LDFLAGS = -mcpu=cortex-m4 -mthumb -mfloat-abi=soft \
          -T cortexm4.ld -nostartfiles -specs=nosys.specs \
          -Wl,--gc-sections -Wl,--no-warn-rwx-segments

SRCS_C  = main.c
SRCS_S  = startup.S
OBJS    = $(SRCS_C:.c=.o) $(SRCS_S:.S=.o)

all: hello.elf

hello.elf: $(OBJS) cortexm4.ld
	$(CC) $(LDFLAGS) -o $@ $(OBJS)
	$(OBJDUMP) -d -t $@ > hello.dump

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.S
	$(CC) $(ASFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) hello.elf hello.dump

.PHONY: all clean
