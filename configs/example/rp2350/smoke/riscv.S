.section .text, "ax", @progbits
.global _start
_start:
    li sp, 0x20082000
    lla t0, sram_word
    li t1, 42
    lw t2, 0(t0)
    bne t1, t2, fail
    addi t0, t0, 4
    sw t1, 0(t0)
    lw t2, 0(t0)
    bne t1, t2, fail
    lla t3, xip_word
    lw t4, 0(t3)
    bne t1, t4, fail
    call sram_func
    bne t1, t5, fail
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
.align 2
xip_word: .word 42
ok_message: .asciz "RP2350_RISCV_SMOKE_OK\n"
fail_message: .asciz "RP2350_RISCV_SMOKE_FAIL\n"

.section .sram_text, "ax", @progbits
sram_func:
    li t5, 42
    ret

.section .sram_data, "aw", @progbits
sram_word: .word 42
