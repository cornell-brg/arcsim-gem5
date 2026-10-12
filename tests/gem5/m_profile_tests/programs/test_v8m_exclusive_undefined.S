/*
 * Test: the ARMv8-M load-acquire and store-release instructions are
 * UNDEFINED on an ARMv7-M release (Cortex-M4)
 *
 * LDA/LDAB/LDAH, STL/STLB/STLH, LDAEX/LDAEXB/LDAEXH and
 * STLEX/STLEXB/STLEXH are new in ARMv8-M (DDI0553 C2.4). On the Cortex-M4
 * each must raise a UsageFault and change neither its register nor memory.
 * The UsageFault handler counts the faults and skips the instruction.
 * LDREX, STREX and CLREX are ARMv7-M instructions and must not fault.
 *
 * Runs on the default (M4) release, on the timing CPU, which takes
 * consecutive UsageFaults.
 *
 * Result at 0x20000100:
 *   0xCAFECAFE = all twelve instructions faulted, and the ARMv7-M ones did not
 */

    .syntax unified
    .cpu cortex-m33
    .thumb

    .equ TEST_RESULT,  0x20000100
    .equ TEST_SUBTEST, 0x20000104
    .equ TEST_SCRATCH, 0x20000108
    .equ PASS_VAL,     0xCAFECAFE
    .equ FAIL_VAL,     0xDEADDEAD
    .equ BUF,          0x20000200

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

    /* Initialize fault counter to 0 */
    ldr     r4, =TEST_SCRATCH
    mov     r0, #0
    str     r0, [r4]

    ldr     r5, =BUF
    ldr     r0, =0x55555555
    str     r0, [r5]
    ldr     r2, =0xAAAAAAAA
    ldr     r3, =0x0000CAFE     /* every faulting load and status leaves this */

    /* The ARMv7-M exclusives must not fault */
    movs    r7, #1
    ldrex   r0, [r5]
    strex   r1, r0, [r5]
    clrex
    ldr     r0, [r4]
    cmp     r0, #0
    bne     fail

    movs    r7, #2
    lda     r3, [r5]
    ldab    r3, [r5]
    ldah    r3, [r5]
    ldaex   r3, [r5]
    ldaexb  r3, [r5]
    ldaexh  r3, [r5]
    ldr     r0, [r4]
    cmp     r0, #6
    bne     fail

    movs    r7, #3
    stl     r2, [r5]
    stlb    r2, [r5]
    stlh    r2, [r5]
    ldrex   r0, [r5]            /* the monitor is set: a decoded STLEX would store */
    stlex   r3, r2, [r5]
    stlexb  r3, r2, [r5]
    stlexh  r3, r2, [r5]
    clrex
    ldr     r0, [r4]
    cmp     r0, #12
    bne     fail

    movs    r7, #4
    ldr     r0, =0x0000CAFE
    cmp     r3, r0
    bne     fail
    ldr     r1, [r5]
    ldr     r0, =0x55555555
    cmp     r1, r0
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
