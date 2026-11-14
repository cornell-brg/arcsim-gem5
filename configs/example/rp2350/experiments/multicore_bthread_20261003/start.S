.section .text.start, "ax", @progbits
.global _start
_start:
    csrr a0, mhartid
    li t0, BTHREAD_NUM_CORES
    bgeu a0, t0, park
    .option push
    .option norelax
    la gp, __global_pointer$
    .option pop
    la sp, __stacks_end
    slli t0, a0, 12
    sub sp, sp, t0           /* private, 16-byte-aligned 4 KiB stacks */
    bnez a0, enter
    la t0, __bss_start
    la t1, __bss_end
clear_bss:
    bgeu t0, t1, enter
    sw zero, 0(t0)
    addi t0, t0, 4
    j clear_bss
enter:
    call boot_main
park:
    j park

.global semihost
semihost:
    .option push
    .option norvc
    slli x0, x0, 0x1f
    ebreak
    srai x0, x0, 7
    .option pop
    ret
