/*
 * Test: the RP2350 coprocessors (run_m4_test.py --rp2350-coprocessors)
 *
 * Subtests:
 *   1. GPIO coprocessor (p0): an MCR to GPIO_OUT and an MCRR single-pin write
 *      read back through MRC and through the SIO's GPIO_OUT register
 *   2. DCP (p4): the RCMP read returns 0 without a fault
 *   3. p1, with nothing attached: UsageFault (NOCP)
 *   4. p0 with CPACR.CP0 = 0: UsageFault (NOCP)
 *   5. p0 with CPACR.CP0 = privileged only, from privileged code: no fault
 *   6. exactly the two faults of 3 and 4
 *   7. GPIO_IN reads OUT & OE (no pads), through MRC and through SIO
 *   8. a byte write to SIO GPIO_OUT replicates across the register
 *   9. p0 with CPACR.CP0 = privileged only, from unprivileged code: NOCP
 *
 * Runs on the M33 release and the timing CPU.
 *
 * Result at 0x20000100:
 *   0xCAFECAFE = all subtests passed
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

    .equ SIO_GPIO_OUT, 0xD0000010

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

    /* 1: GPIO coprocessor and SIO see the same GPIO_OUT */
    mov     r2, #1
    mov     r0, #0x5
    mcr     p0, #0, r0, c0, c0
    mov     r0, #6
    mov     r1, #1
    mcrr    p0, #4, r0, r1, c0
    mrc     p0, #0, r0, c0, c0
    cmp     r0, #0x45
    bne     fail
    ldr     r1, =SIO_GPIO_OUT
    ldr     r0, [r1]
    cmp     r0, #0x45
    bne     fail

    /* 2: DCP RCMP read */
    mov     r2, #2
    mov     r0, #0x7
    mrc     p4, #0, r0, c0, c0, #1
    cmp     r0, #0
    bne     fail
    ldr     r0, [r4]
    cmp     r0, #0
    bne     fail

    /* 3: nothing on p1 */
    mov     r2, #3
    mcr     p1, #0, r0, c0, c0
    ldr     r0, [r4]
    cmp     r0, #1
    bne     fail

    /* 4: CPACR.CP0 = 0 */
    mov     r2, #4
    ldr     r5, =CPACR
    ldr     r1, =0x00F0FFFC
    str     r1, [r5]
    dsb
    isb
    mcr     p0, #0, r0, c0, c0
    ldr     r0, [r4]
    cmp     r0, #2
    bne     fail

    /* 5: CPACR.CP0 = privileged only */
    mov     r2, #5
    ldr     r1, =0x00F0FFFD
    str     r1, [r5]
    dsb
    isb
    mcr     p0, #0, r0, c0, c0
    ldr     r0, [r4]
    cmp     r0, #2
    bne     fail

    /* 6: no other faults */
    mov     r2, #6
    ldr     r0, [r4]
    cmp     r0, #2
    bne     fail

    /* 7: GPIO_IN = OUT & OE */
    mov     r2, #7
    ldr     r1, =0x00F0FFFF
    str     r1, [r5]
    dsb
    isb
    ldr     r0, =0x000000F0
    mcr     p0, #0, r0, c0, c0
    ldr     r0, =0x00000033
    mcr     p0, #0, r0, c0, c4
    mrc     p0, #0, r0, c0, c8
    cmp     r0, #0x30
    bne     fail
    ldr     r1, =0xD0000004
    ldr     r0, [r1]
    cmp     r0, #0x30
    bne     fail

    /* 8: strb replicates: GPIO_OUT = 0x5A5A5A5A */
    mov     r2, #8
    ldr     r1, =SIO_GPIO_OUT
    mov     r0, #0x5A
    strb    r0, [r1, #1]
    mrc     p0, #0, r0, c0, c0
    ldr     r1, =0x5A5A5A5A
    cmp     r0, r1
    bne     fail

    /* 9: privileged-only grant, unprivileged caller (CONTROL.nPRIV = 1) */
    mov     r2, #9
    ldr     r1, =0x00F0FFFD
    str     r1, [r5]
    dsb
    isb
    mov     r0, #1
    msr     control, r0
    isb
    mcr     p0, #0, r0, c0, c0
    ldr     r0, [r4]
    cmp     r0, #3
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
