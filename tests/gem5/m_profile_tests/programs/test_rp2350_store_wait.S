/*
 * Test: a store that loses its SRAM bank to the DMA holds the core
 *
 * The Cortex-M33 has no write buffer: while the bus makes a store wait, the
 * instructions behind it wait too. Two straight-line bodies of stores to one
 * word of SRAM bank 4 are timed alone, beside a DMA channel writing bank 4
 * every cycle, and beside one both reading and writing it. A bank serves one
 * manager a cycle and takes turns between those asking.
 *
 * Runs from SRAM on the RP2350 board with its DMA and the banked SRAM
 * (run_rp2350_sdk.py --dma --sram-model banked), and as loaded on a Pico 2
 * through OpenOCD (see the README): the cycle counts checked here are what
 * a Pico 2 gives, to within 2.
 *
 * Result at 0x20000100:
 *   0xCAFECAFE = all subtests passed (and RP2350_STORE_WAIT_OK through
 *                semihosting)
 *   0xDEADDEAD = a subtest failed; its number is at 0x20000104, plus 0x100
 *                if a fault was taken, and the cycles it rejected at
 *                0x20000108
 */
    .syntax unified
    .cpu cortex-m33
    .thumb

    .equ TEST_RESULT,  0x20000100
    .equ TEST_SUBTEST, 0x20000104
    .equ TEST_VALUE,   0x20000108
    .equ PASS_VAL,     0xCAFECAFE
    .equ FAIL_VAL,     0xDEADDEAD
    .equ STACK_TOP,    0x20020000

    .equ DMA,      0x50000000
    .equ CH7,      0x1c0
    .equ CH_READ, 0x00
    .equ CH_WRITE, 0x04
    .equ CH_COUNT, 0x08
    .equ CH_CTRL_TRIG, 0x0c
    .equ INTR, 0x400
    .equ ABORT, 0x464

    .equ WORD,     0x20040100       /* the core's word, bank 4 */
    .equ DMA_B4,   0x20048000       /* the DMA's words of bank 4 */
    .equ DMA_B4B,  0x20048010
    .equ DMA_B8,   0x20080000       /* and of bank 8 */

    /* enabled, 32-bit, unpaced, both addresses fixed, for ever */
    .equ CFIX, 1 | (2 << 2) | (0x3f << 17) | (7 << 13)

    .section .text
    /* The vector table first: the board's reset takes SP and PC from it */
vectors:
    .word   STACK_TOP
    .word   Reset_Handler
    .rept   14
    .word   fault
    .endr

    /* Times \body beside a DMA channel reading \read and writing \write
     * (\read = 0: alone); r0 = its cycles */
    .macro timed body, read, write
    ldr     r4, =\read
    ldr     r5, =\write
    ldr     r0, =\body + 1
    bl      measure
    .endm

    /* Fail unless r0 is within 2 of \base + \extra; uses r1 */
    .macro expect_cycles base, extra
    sub     r1, r0, \base
    sub     r1, r1, #(\extra - 2)
    cmp     r1, #4
    bhi     fail
    .endm

    .thumb_func
    .globl Reset_Handler
Reset_Handler:
    ldr     r0, =STACK_TOP
    mov     sp, r0
    ldr     r0, =vectors
    ldr     r1, =0xE000ED08
    str     r0, [r1]
    cpsid   i
    /* On a Pico 2 the DMA starts in reset: RESETS.RESET bit 2 through the
     * clear alias, then RESET_DONE, giving up where nothing models it */
    ldr     r0, =0x40023000
    movs    r1, #4
    str     r1, [r0]
    ldr     r0, =0x40020008
    ldr     r2, =1000
1:  ldr     r1, [r0]
    tst     r1, #4
    bne     2f
    subs    r2, #1
    bne     1b
2:
    ldr     r11, =DMA
    add     r8, r11, #CH7
    ldr     r9, =WORD
    ldr     r0, =0xE000EDFC         /* DWT cycle counter on */
    ldr     r1, [r0]
    orr     r1, r1, #(1 << 24)
    str     r1, [r0]
    ldr     r6, =0xE0001000
    ldr     r1, [r6]
    orr     r1, r1, #1
    str     r1, [r6]

    /* 1: alone, 128 stores take a cycle each, and a store with three adds
     * four (a Pico 2: 132 and 259 cycles with the counter's loads) */
    movs    r7, #1
    timed   body_stores, 0, 0
    push    {r0}                    /* 128 stores alone */
    expect_cycles #0, 132
    timed   body_spaced, 0, 0
    push    {r0}                    /* 64 spaced stores alone */
    expect_cycles #0, 259

    /* 2: beside a DMA write to the bank every cycle, back-to-back stores
     * get every second cycle: each waits one */
    movs    r7, #2
    timed   body_stores, DMA_B8, DMA_B4
    ldr     r10, [sp, #4]
    expect_cycles r10, 127

    /* 3: a store every fourth cycle finds the bank's turn has come back to
     * the core, and does not wait */
    movs    r7, #3
    timed   body_spaced, DMA_B8, DMA_B4
    ldr     r10, [sp]
    expect_cycles r10, 0

    /* 4: beside a read and a write of the bank, back-to-back stores get
     * every third cycle */
    movs    r7, #4
    timed   body_stores, DMA_B4, DMA_B4B
    ldr     r10, [sp, #4]
    expect_cycles r10, 254

    /* 5: and each store of the spaced body waits one cycle, which the adds
     * behind it wait too */
    movs    r7, #5
    timed   body_spaced, DMA_B4, DMA_B4B
    ldr     r10, [sp]
    expect_cycles r10, 63

    ldr     r0, =PASS_VAL
    ldr     r1, =TEST_RESULT
    str     r0, [r1]
    ldr     r1, =ok_message
    b       done

fail:
    ldr     r1, =TEST_VALUE
    str     r0, [r1]
    ldr     r0, =FAIL_VAL
    ldr     r1, =TEST_RESULT
    str     r0, [r1]
    ldr     r1, =TEST_SUBTEST
    str     r7, [r1]
    ldr     r1, =fail_message
done:
    movs    r0, #4
    bkpt    #0xab
    mov     r0, #0x18
    ldr     r1, =0x20026
    bkpt    #0xab
    b       .

    /* r0 = body; r4, r5 = the DMA's read and write addresses (r4 = 0: no
     * DMA); r9 = the core's word. Returns r0 = the body's cycles. */
    .thumb_func
measure:
    push    {lr}
    mov     r12, r0
    cbz     r4, 1f
    str     r4, [r8, #CH_READ]
    str     r5, [r8, #CH_WRITE]
    ldr     r0, =0x0FFFFFFF
    str     r0, [r8, #CH_COUNT]
    ldr     r0, =CFIX
    str     r0, [r8, #CH_CTRL_TRIG]
    movs    r0, #64                 /* let it reach its steady state */
2:  subs    r0, #1
    bne     2b
1:  mov     r1, r9
    blx     r12
    cbz     r4, 1f
    mov     r2, #(1 << 7)
    str     r2, [r11, #ABORT]
2:  ldr     r2, [r11, #ABORT]
    cmp     r2, #0
    bne     2b
    mov     r2, #(1 << 7)
    str     r2, [r11, #INTR]
1:  pop     {pc}

    .ltorg

    /* Each body: r1 = the word; returns r0 = its cycles. The cycle counter's
     * loads bracket the body alone, which has no branches. */
    .macro timed_body name
    .balign 4
    .thumb_func
\name:
    ldr     r0, [r6, #4]
    push    {r0}
    .balign 4
    .endm
    .macro end_body
    ldr     r0, [r6, #4]
    pop     {r2}
    subs    r0, r0, r2
    bx      lr
    .endm

    timed_body body_stores
    .rept 128
    str     r0, [r1]
    .endr
    end_body

    timed_body body_spaced
    .rept 64
    str     r0, [r1]
    adds    r0, #1
    adds    r0, #1
    adds    r0, #1
    .endr
    end_body

    .thumb_func
fault:
    orr     r7, r7, #0x100
    b       fail
    .ltorg

ok_message:   .asciz "RP2350_STORE_WAIT_OK\n"
fail_message: .asciz "RP2350_STORE_WAIT_FAIL\n"
