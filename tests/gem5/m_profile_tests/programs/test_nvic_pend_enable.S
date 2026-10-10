/*
 * Test: interrupts pended by a store, and pended while disabled
 *
 * A store to ISPR pends an interrupt while the store itself is under way,
 * when a pipelined CPU model cannot yet enter a handler. The interrupt
 * must still be taken afterwards, every time: an interrupt controller that
 * marks it taken when the CPU only asks about it loses it.
 *
 * An interrupt set pending while disabled is taken once it is enabled.
 *
 * Runs on the timing and Minor CPUs.
 *
 * Result at 0x20000100:
 *   0xCAFECAFE = all subtests passed
 *   0xDEADDEAD = a subtest failed; its number is at 0x20000104
 */

    .syntax unified
    .cpu cortex-m4
    .thumb

    .equ TEST_RESULT,  0x20000100
    .equ TEST_SUBTEST, 0x20000104
    .equ PASS_VAL,     0xCAFECAFE
    .equ FAIL_VAL,     0xDEADDEAD
    .equ BUF,          0x20000200
    .equ IRQS,         0x20000300   /* IRQ 3 handler's count */
    .equ NVIC_ISER,    0xE000E100
    .equ NVIC_ICER,    0xE000E180
    .equ NVIC_ISPR,    0xE000E200

    .section .isr_vector, "a", %progbits
    .align 2
__isr_vector:
    .word   0x20020000          /* Initial SP */
    .word   Reset_Handler       /* Reset */
    .rept   14
    .word   default_handler
    .endr
    .word   default_handler     /* IRQ 0 */
    .word   default_handler     /* IRQ 1 */
    .word   default_handler     /* IRQ 2 */
    .word   Irq3_Handler        /* IRQ 3 */

    .section .text

    .thumb_func
    .globl Reset_Handler
Reset_Handler:
    ldr     r4, =BUF
    ldr     r5, =IRQS
    movs    r0, #0
    str     r0, [r5]
    ldr     r8, =NVIC_ISPR
    ldr     r9, =NVIC_ISER
    movs    r6, #8              /* IRQ 3 */

    /* 1: IRQ 3, enabled, pended 16 times among loads and stores: taken
     * 16 times */
    movs    r7, #1
    str     r6, [r9]
    movs    r3, #16
1:  ldr     r0, [r4]
    str     r0, [r4, #4]
    str     r6, [r8]
    ldr     r1, [r4, #8]
    str     r1, [r4, #12]
    nop
    nop
    subs    r3, #1
    bne     1b
    ldr     r0, [r5]
    cmp     r0, #16
    bne     fail

    /* 2: pended while disabled, it waits; enabled, it is taken once */
    movs    r7, #2
    ldr     r0, =NVIC_ICER
    str     r6, [r0]
    str     r6, [r8]
    nop
    nop
    nop
    nop
    ldr     r0, [r5]
    cmp     r0, #16
    bne     fail
    str     r6, [r9]
    nop
    nop
    nop
    nop
    ldr     r0, [r5]
    cmp     r0, #17
    bne     fail

    ldr     r0, =PASS_VAL
    ldr     r1, =TEST_RESULT
    str     r0, [r1]
    b       done

fail:
    ldr     r0, =FAIL_VAL
    ldr     r1, =TEST_RESULT
    str     r0, [r1]
    ldr     r1, =TEST_SUBTEST
    str     r7, [r1]

done:
    /* Exit via ARM semihosting SYS_EXIT */
    mov     r0, #0x18
    ldr     r1, =0x20026
    bkpt    #0xab
    b       .

    .thumb_func
Irq3_Handler:
    ldr     r0, =IRQS
    ldr     r1, [r0]
    adds    r1, #1
    str     r1, [r0]
    bx      lr

    .thumb_func
default_handler:
    b       .
