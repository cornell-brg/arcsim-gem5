/*
 * Test: coprocessor instructions with no coprocessor attached
 *
 * MCR, MRC, MCRR, MRRC and CDP (and an MCR2) to each of p0-p7, and MCR to
 * p8, p9, p12 and p13, with CPACR granting full access to p0-p7: with
 * nothing attached to the coprocessor port, each
 * raises a UsageFault (NOCP), as the RP2350's Cortex-M33 does for the numbers
 * it has no coprocessor on. The UsageFault handler counts the faults and skips
 * the instruction.
 *
 * Runs on the timing CPU, which takes consecutive UsageFaults.
 *
 * Result at 0x20000100:
 *   0xCAFECAFE = all thirteen instructions faulted
 */

    .syntax unified
    .cpu cortex-m33
    .thumb

    .equ TEST_RESULT,  0x20000100
    .equ TEST_SUBTEST, 0x20000104
    .equ TEST_SCRATCH, 0x20000108
    .equ PASS_VAL,     0xCAFECAFE
    .equ FAIL_VAL,     0xDEADDEAD
    .equ CPACR,        0xE000ED88

    .section .isr_vector, "a", %progbits
    .align 2
__isr_vector:
    .word   0x20020000          /* Initial SP */
    .word   Reset_Handler       /* Reset */
    .word   default_handler     /* NMI */
    .word   HardFault_Handler   /* HardFault */
    .word   default_handler     /* MemManage */
    .word   default_handler     /* BusFault */
    .word   UsageFault_Handler  /* UsageFault */

    .section .text

    .thumb_func
    .globl Reset_Handler
Reset_Handler:
    /* Enable UsageFault in SHCSR */
    ldr     r0, =0xE000ED24
    ldr     r1, [r0]
    orr     r1, r1, #(1 << 18)
    str     r1, [r0]

    /* Full access to CP0-CP7 and CP10/CP11 */
    ldr     r0, =CPACR
    ldr     r1, =0x00F0FFFF
    str     r1, [r0]
    dsb
    isb

    /* Initialize fault counter to 0 */
    ldr     r4, =TEST_SCRATCH
    mov     r0, #0
    str     r0, [r4]

    mcr     p0, #0, r0, c0, c0
    mrc     p1, #0, r0, c0, c0
    mcrr    p2, #0, r0, r1, c0
    mrrc    p3, #0, r0, r1, c0
    mrc     p4, #0, r0, c0, c0, #1
    cdp     p5, #0, c0, c0, c0, #0
    mcr     p6, #0, r0, c0, c0
    mcr     p7, #0, r0, c0, c0
    mcr2    p0, #0, r0, c0, c0
    mcr     p8, #0, r0, c0, c0
    mcr     p9, #0, r0, c0, c0
    mcr     p12, #0, r0, c0, c0
    mcr     p13, #0, r0, c0, c0

    ldr     r0, [r4]
    mov     r2, #1
    cmp     r0, #13
    bne     fail
    b       pass

pass:
    ldr     r0, =PASS_VAL
    ldr     r1, =TEST_RESULT
    str     r0, [r1]
    b       done

fail:
    ldr     r0, =FAIL_VAL
    ldr     r1, =TEST_RESULT
    str     r0, [r1]
    ldr     r1, =TEST_SUBTEST
    str     r2, [r1]

done:
    mov     r0, #0x18
    ldr     r1, =0x20026
    bkpt    #0xab
    b       .

    /* --- UsageFault Handler ---
     * Increment fault counter and skip the 4-byte instruction.
     */
    .thumb_func
UsageFault_Handler:
    ldr     r0, =TEST_SCRATCH
    ldr     r1, [r0]
    add     r1, r1, #1
    str     r1, [r0]
    ldr     r0, [sp, #0x18]
    add     r0, r0, #4
    str     r0, [sp, #0x18]
    bx      lr

    .thumb_func
HardFault_Handler:
    ldr     r0, =FAIL_VAL
    ldr     r1, =TEST_RESULT
    str     r0, [r1]
    mov     r2, #99
    ldr     r1, =TEST_SUBTEST
    str     r2, [r1]
    mov     r0, #0x18
    ldr     r1, =0x20026
    bkpt    #0xab
    b       .

    .thumb_func
default_handler:
    b       .
