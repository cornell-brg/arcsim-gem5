/* Hazard3's latency table's rules (hazard3_latency.py) at IL: each case is timed by mcycle
 * reads on either side, less the reads' own cycle, and compared with Hazard3's
 * instruction_timings.adoc:
 *   alu:      four addi                                   4
 *   loaduse:  lw, then an add that reads its result       3 (a cycle's bubble)
 *   loadstore: lw, then a sw of its result                2 (store data read in M)
 *   div:      div                                         19
 *   loop:     li, then four turns of addi and a bnez back 11 (taken 2, predicted 1, 1,
 *                                                            predicted and not taken 2)
 *   jump:     j to the next instruction                   2
 *   unaligned: jal to a 32-bit addi at a halfword address  4 (jal 2, a cycle more, addi 1)
 * Prints each case's cycles as name=hex, then RP2350_RISCV_LATENCY_OK or ..._FAIL. At CL
 * (--cpu minor) the same program prints the model's cycles for comparison; the check is
 * the table's.
 *   riscv32-unknown-elf-gcc -march=rv32imac_zicsr -mabi=ilp32 -nostdlib -T riscv.ld \
 *     riscv_latency.S -o riscv_latency.elf
 *   gem5.opt configs/example/rp2350/run.py --core hazard3-proxy --firmware riscv_latency.elf \
 *     --cpu atomic --inst-latency --xip-model soc-no-cache
 * (soc: code memory without misses, so a case's cycles are its instructions' alone) */
    .option rvc
    .macro start
    csrr s1, mcycle
    .endm
    .macro stop name, want
    csrr s2, mcycle
    sub a0, s2, s1
    addi a0, a0, -1        /* the first read's own cycle */
    li a1, \want
    lla a2, name_\name
    call report
    .endm

.section .text, "ax", @progbits
.global _start
_start:
    li sp, 0x20082000
    lla s0, data
    li s3, 0                /* failures */
    li a1, 7
    li a2, 3

    start
    addi t0, t0, 1
    addi t0, t0, 1
    addi t0, t0, 1
    addi t0, t0, 1
    stop alu, 4

    start
    lw a0, 0(s0)
    add a3, a0, a2
    stop loaduse, 3

    start
    lw a0, 0(s0)
    sw a0, 4(s0)
    stop loadstore, 2

    li a4, 7
    li a5, 3
    start
    div a3, a4, a5
    stop div, 19

    start
    li t2, 4
1:  addi t2, t2, -1
    bnez t2, 1b
    stop loop, 11

    start
    j 2f
    .balign 4               /* a word-aligned target: no cycle more */
2:  stop jump, 2

    .balign 4
    start
    .option push
    .option norvc
    jal x0, 3f
    .option pop
    c.nop
3:
    .option push
    .option norvc
    addi t0, t0, 1
    .option pop
    stop unaligned, 4

    bnez s3, fail
    li a0, 4                /* SYS_WRITE0 */
    lla a1, ok_message
    call semihost
    j exit
fail:
    li a0, 4
    lla a1, fail_message
    call semihost
exit:
    li a0, 0x18             /* SYS_EXIT */
    li a1, 0x20026
    call semihost
spin:
    j spin

/* report: print "name=<a0 in hex>" and, when a0 is not a1, " want <a1>"; count a failure. */
report:
    addi sp, sp, -16
    sw ra, 12(sp)
    sw a0, 8(sp)
    sw a1, 4(sp)
    mv a0, a2
    call puts
    lw a0, 8(sp)
    call puthex
    lw a0, 8(sp)
    lw a1, 4(sp)
    beq a0, a1, 4f
    addi s3, s3, 1
    lla a0, want_text
    call puts
    lw a0, 4(sp)
    call puthex
4:  lla a0, newline
    call puts
    lw ra, 12(sp)
    addi sp, sp, 16
    ret

/* puthex: print a0 as eight hex digits */
puthex:
    addi sp, sp, -16
    sw ra, 12(sp)
    lla t3, hexbuf
    li t4, 8
5:  srli t5, a0, 28
    slli a0, a0, 4
    addi t5, t5, '0'
    li t6, '9'
    ble t5, t6, 6f
    addi t5, t5, 'a' - '0' - 10
6:  sb t5, 0(t3)
    addi t3, t3, 1
    addi t4, t4, -1
    bnez t4, 5b
    sb zero, 0(t3)
    lla a0, hexbuf
    call puts
    lw ra, 12(sp)
    addi sp, sp, 16
    ret

/* puts: SYS_WRITE0 of the string at a0 */
puts:
    addi sp, sp, -16
    sw ra, 12(sp)
    mv a1, a0
    li a0, 4
    call semihost
    lw ra, 12(sp)
    addi sp, sp, 16
    ret

.align 2
semihost:
    .option push
    .option norvc
    slli x0, x0, 0x1f
    ebreak
    srai x0, x0, 7
    .option pop
    ret

.section .rodata, "a", @progbits
ok_message: .asciz "RP2350_RISCV_LATENCY_OK\n"
fail_message: .asciz "RP2350_RISCV_LATENCY_FAIL\n"
want_text: .asciz " want "
newline: .asciz "\n"
name_alu: .asciz "alu="
name_loaduse: .asciz "loaduse="
name_loadstore: .asciz "loadstore="
name_div: .asciz "div="
name_loop: .asciz "loop="
name_jump: .asciz "jump="
name_unaligned: .asciz "unaligned="

.section .sram_data, "aw", @progbits
.align 2
data: .word 5, 0
hexbuf: .space 12
