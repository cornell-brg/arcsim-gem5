.syntax unified
.thumb
.section .vectors, "a", %progbits
.word 0x20082000
.word reset + 1

.section .text, "ax", %progbits
.global reset
.thumb_func
reset:
    ldr r0, =sram_word
    movs r1, #42
    ldr r2, [r0]
    cmp r2, r1
    bne fail
    adds r0, #4
    str r1, [r0]
    ldr r2, [r0]
    cmp r2, r1
    bne fail
    ldr r3, =xip_word
    ldr r4, [r3]
    cmp r4, r1
    bne fail
    ldr r3, =sram_func
    blx r3
    cmp r5, r1
    bne fail
    movs r0, #4              /* SYS_WRITE0 */
    ldr r1, =ok_message
    bkpt #0xab
    b exit
fail:
    movs r0, #4
    ldr r1, =fail_message
    bkpt #0xab
exit:
    movs r0, #0x18           /* SYS_EXIT */
    ldr r1, =0x20026
    bkpt #0xab
spin:
    b spin

.section .rodata, "a", %progbits
.align 2
xip_word: .word 42
ok_message: .asciz "RP2350_ARM_SMOKE_OK\n"
fail_message: .asciz "RP2350_ARM_SMOKE_FAIL\n"

.section .sram_text, "ax", %progbits
.thumb_func
sram_func:
    movs r5, #42
    bx lr

.section .sram_data, "aw", %progbits
sram_word: .word 42
