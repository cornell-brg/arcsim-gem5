/*
 * Test: FPv5 instructions are UNDEFINED on a release without FPv5 (Cortex-M4)
 *
 * The Cortex-M4's FPU is FPv4-SP. Each FPv5 instruction below, from both the
 * hw1[12]=1 space (VSEL, VMAXNM, VRINTA, VCVTA) and the ordinary coprocessor
 * space (VRINTZ, VRINTX), must raise a UsageFault. The UsageFault handler
 * counts the faults and skips the instruction.
 *
 * Runs on the default (M4) release, on the timing CPU, which takes
 * consecutive UsageFaults.
 *
 * Result at 0x20000100:
 *   0xCAFECAFE = all six instructions faulted, and VADD did not
 */

    .syntax unified
    .cpu cortex-m33
    .fpu fpv5-sp-d16
    .thumb

    .equ TEST_RESULT,  0x20000100
    .equ TEST_SUBTEST, 0x20000104
    .equ TEST_SCRATCH, 0x20000108
    .equ PASS_VAL,     0xCAFECAFE
    .equ FAIL_VAL,     0xDEADDEAD

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

    /* Full access to CP10/CP11, so the FPU itself is usable */
    ldr     r0, =0xE000ED88
    ldr     r1, [r0]
    orr     r1, r1, #(0xF << 20)
    str     r1, [r0]
    dsb
    isb

    /* Initialize fault counter to 0 */
    ldr     r4, =TEST_SCRATCH
    mov     r0, #0
    str     r0, [r4]

    /* An ordinary FPv4 instruction must not fault */
    vadd.f32    s2, s0, s1

    vselgt.f32  s2, s0, s1
    vmaxnm.f32  s2, s0, s1
    vrinta.f32  s2, s0
    vcvta.s32.f32 s2, s0
    vrintz.f32  s2, s0
    vrintx.f32  s2, s0

    ldr     r0, [r4]
    cmp     r0, #6
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
    str     r0, [r1]

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
