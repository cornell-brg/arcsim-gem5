/*
 * Test: VLDR and VSTR in IT blocks
 *
 * A conditional VLDR or VSTR must act on the flags of the instruction just
 * before its IT, not on older ones. Each subtest sets the flags one way,
 * then the other, right before the IT block, so a CPU model that starts the
 * access early (the Minor CPU issues memory accesses ahead of commit) sees
 * the wrong flags unless the instruction waits for them.
 *
 * Runs on the atomic, timing and Minor CPUs.
 *
 * Result at 0x20000100:
 *   0xCAFECAFE = all subtests passed
 *   0xDEADDEAD = a subtest failed; its number is at 0x20000104
 */

    .syntax unified
    .cpu cortex-m4
    .fpu fpv4-sp-d16
    .thumb

    .equ TEST_RESULT,  0x20000100
    .equ TEST_SUBTEST, 0x20000104
    .equ PASS_VAL,     0xCAFECAFE
    .equ FAIL_VAL,     0xDEADDEAD
    .equ BUF,          0x20000200

    .section .isr_vector, "a", %progbits
    .align 2
__isr_vector:
    .word   0x20020000          /* Initial SP */
    .word   Reset_Handler       /* Reset */
    .word   default_handler     /* NMI */
    .word   default_handler     /* HardFault */
    .word   default_handler     /* MemManage */
    .word   default_handler     /* BusFault */
    .word   default_handler     /* UsageFault */

    .section .text

    /* Fail the current subtest unless \reg holds \value. Uses r6. */
    .macro expect reg, value
    ldr     r6, =\value
    cmp     \reg, r6
    bne     fail
    .endm

    .thumb_func
    .globl Reset_Handler
Reset_Handler:
    /* Full access to CP10/CP11 */
    ldr     r0, =0xE000ED88
    ldr     r1, [r0]
    orr     r1, r1, #(0xF << 20)
    str     r1, [r0]
    dsb
    isb

    /* BUF: words 0-1 hold one pattern, words 2-3 another */
    ldr     r4, =BUF
    ldr     r0, =0x11111111
    str     r0, [r4]
    ldr     r0, =0x33333333
    str     r0, [r4, #4]
    ldr     r0, =0x22222222
    str     r0, [r4, #8]
    ldr     r0, =0x44444444
    str     r0, [r4, #12]

    /* 1: flags NE, then EQ: VLDRNE (single) loads nothing */
    movs    r7, #1
    vldr    s0, [r4, #8]
    cmp     r4, r4
    it      ne
    vldrne  s0, [r4]
    vmov    r0, s0
    expect  r0, 0x22222222

    /* 2: flags EQ, then NE: VLDRNE (single) loads */
    movs    r7, #2
    vldr    s0, [r4, #8]
    cmp     r4, r4
    movs    r1, #1
    it      ne
    vldrne  s0, [r4]
    vmov    r0, s0
    expect  r0, 0x11111111

    /* 3: flags NE, then EQ: VLDRNE (double) loads nothing */
    movs    r7, #3
    vldr    d1, [r4, #8]
    cmp     r4, r4
    it      ne
    vldrne  d1, [r4]
    vmov    r0, r1, d1
    expect  r0, 0x22222222
    expect  r1, 0x44444444

    /* 4: flags EQ, then NE: VLDRNE (double) loads */
    movs    r7, #4
    vldr    d1, [r4, #8]
    cmp     r4, r4
    movs    r1, #1
    it      ne
    vldrne  d1, [r4]
    vmov    r0, r1, d1
    expect  r0, 0x11111111
    expect  r1, 0x33333333

    /* 5: flags NE, then EQ: VSTRNE (single) stores nothing */
    movs    r7, #5
    vldr    s4, [r4, #8]
    cmp     r4, r4
    it      ne
    vstrne  s4, [r4]
    ldr     r0, [r4]
    expect  r0, 0x11111111

    /* 6: flags EQ, then NE: VSTRNE (single) stores */
    movs    r7, #6
    cmp     r4, r4
    movs    r1, #1
    it      ne
    vstrne  s4, [r4]
    ldr     r0, [r4]
    expect  r0, 0x22222222

    /* 7: flags NE, then EQ: VSTRNE (double) stores nothing */
    movs    r7, #7
    ldr     r0, =0x11111111
    str     r0, [r4]
    vldr    d3, [r4, #8]
    cmp     r4, r4
    it      ne
    vstrne  d3, [r4]
    ldr     r0, [r4]
    expect  r0, 0x11111111
    ldr     r0, [r4, #4]
    expect  r0, 0x33333333

    /* 8: flags EQ, then NE: VSTRNE (double) stores */
    movs    r7, #8
    cmp     r4, r4
    movs    r1, #1
    it      ne
    vstrne  d3, [r4]
    ldr     r0, [r4]
    expect  r0, 0x22222222
    ldr     r0, [r4, #4]
    expect  r0, 0x44444444

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
default_handler:
    b       .
