/*
 * Test: a load that loses its SRAM bank holds the core
 *
 * On the Cortex-M33 a load the bus makes wait holds the instructions behind
 * it, as a store does. A bank serves one manager a cycle: here the core's
 * own instruction fetches, running from SRAM, and a DMA channel. Bodies of
 * loads are timed with their data on a bank the code is not on (bank 4),
 * and on the banks the code is fetched from (0 to 3, word-striped), and
 * beside a DMA channel writing, or reading and writing, bank 4 every cycle.
 *
 * Runs from SRAM on the RP2350 board with its DMA and the banked SRAM
 * (run_rp2350_sdk.py --dma --sram-model banked), and as loaded on a Pico 2
 * through OpenOCD (see the README). The cycle counts are what a Pico 2
 * gives; each subtest's cycles beyond the first's are checked to within 2,
 * as the cycle counter's own loads cost the Pico 2 two more.
 *
 * Result at 0x20000100:
 *   0xCAFECAFE = all subtests passed (and RP2350_LOAD_WAIT_OK through
 *                semihosting)
 *   0xDEADDEAD = a subtest failed; its number is at 0x20000104, plus 0x100
 *                if a fault was taken
 * Each subtest's cycles are at 0x20000110 onwards, one word each.
 */
    .syntax unified
    .cpu cortex-m33
    .thumb

    .equ TEST_RESULT,  0x20000100
    .equ TEST_SUBTEST, 0x20000104
    .equ TEST_CYCLES,  0x20000110
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

    .equ WORD_B4,  0x20040100       /* the core's words on bank 4 */
    .equ WORD_B0,  0x20018000       /* and on bank 0, where code is */
    .equ WORD_B1,  0x20018004       /* 1 */
    .equ WORD_B2,  0x20018008       /* 2 */
    .equ WORD_B3,  0x2001800c       /* 3 */
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

    /* Subtest \n: times \body on the words from \word beside a DMA channel
     * reading \read and writing \write (\read = 0: alone), and records its
     * cycles */
    .macro timed n, body, word, read, write
    ldr     r9, =\word
    ldr     r4, =\read
    ldr     r5, =\write
    ldr     r0, =\body + 1
    bl      measure
    ldr     r1, =TEST_CYCLES + 4 * (\n - 1)
    str     r0, [r1]
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
    ldr     r0, =0xE000EDFC         /* DWT cycle counter on */
    ldr     r1, [r0]
    orr     r1, r1, #(1 << 24)
    str     r1, [r0]
    ldr     r6, =0xE0001000
    ldr     r1, [r6]
    orr     r1, r1, #1
    str     r1, [r6]

    /* 1, 2: code and data on different banks: 128 wide loads, and 64 loads
     * each with three adds */
    timed   1, body_wide_same, WORD_B4, 0, 0
    timed   2, body_spaced, WORD_B4, 0, 0
    /* 3-6: data on the banks code is fetched from. A fetch takes a load's
     * bank from it once every four fetched words: each wide load of one
     * word waits a cycle in four, whichever bank it is on */
    timed   3, body_wide_same, WORD_B0, 0, 0
    timed   4, body_wide_same, WORD_B1, 0, 0
    timed   5, body_wide_same, WORD_B2, 0, 0
    timed   6, body_wide_same, WORD_B3, 0, 0
    /* 7: wide loads of four words in turn (banks 0 to 3) meet no fetch */
    timed   7, body_wide_four, WORD_B0, 0, 0
    /* 8-10: 16-bit loads, two to a fetched word, of one word on bank 0, of
     * two in turn (banks 0 and 1), and of one on bank 4 */
    timed   8, body_narrow_same, WORD_B0, 0, 0
    timed   9, body_narrow_two, WORD_B0, 0, 0
    timed   10, body_narrow_same, WORD_B4, 0, 0
    /* 11, 12: beside a DMA write to bank 4 every cycle, back-to-back loads
     * get every second cycle, and spaced ones find their turn come back */
    timed   11, body_wide_same, WORD_B4, DMA_B8, DMA_B4
    timed   12, body_spaced, WORD_B4, DMA_B8, DMA_B4
    /* 13, 14: beside a read and a write of bank 4, every third cycle, and
     * each spaced load waits one */
    timed   13, body_wide_same, WORD_B4, DMA_B4, DMA_B4B
    timed   14, body_spaced, WORD_B4, DMA_B4, DMA_B4B

    /* Print each subtest's cycles, then check each one's cycles beyond the
     * first's against a Pico 2's, to within 2 */
    movs    r7, #1
    ldr     r2, =TEST_CYCLES
1:  ldr     r0, [r2], #4
    bl      print_cycles
    adds    r7, #1
    cmp     r7, #(N_SUBTESTS + 1)
    bne     1b
    movs    r7, #1
    ldr     r2, =TEST_CYCLES
    ldr     r3, =expected
    ldr     r10, [r2]               /* the first's own difference */
    ldr     r1, [r3]
    subs    r10, r10, r1
1:  ldr     r0, [r2], #4
    ldr     r1, [r3], #4
    subs    r1, r0, r1
    subs    r1, r1, r10
    adds    r1, #2
    cmp     r1, #4
    bhi     fail
    adds    r7, #1
    cmp     r7, #(N_SUBTESTS + 1)
    bne     1b

    ldr     r0, =PASS_VAL
    ldr     r1, =TEST_RESULT
    str     r0, [r1]
    ldr     r1, =ok_message
    b       done

fail:
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
     * DMA); r9 = the core's words. Returns r0 = the body's cycles. */
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

    /* Prints "subtest <r7> <r0>\n" in decimal through semihosting; keeps
     * r2, r3, r7 */
    .thumb_func
print_cycles:
    push    {r2, r3, r4, r5, lr}
    ldr     r4, =PRINT_BUF + 31
    movs    r1, #0
    strb    r1, [r4]
    movs    r1, #10
    strb    r1, [r4, #-1]!
    movs    r3, #10
2:  udiv    r1, r0, r3              /* r0 in decimal, last digit first */
    mls     r5, r1, r3, r0
    adds    r5, #'0'
    strb    r5, [r4, #-1]!
    movs    r0, r1
    cbz     r0, 3f
    b       2b
3:  movs    r1, #' '
    strb    r1, [r4, #-1]!
    udiv    r1, r7, r3              /* then the subtest's number */
    mls     r5, r1, r3, r7
    adds    r5, #'0'
    strb    r5, [r4, #-1]!
    cbz     r1, 4f
    adds    r1, #'0'
    strb    r1, [r4, #-1]!
4:  ldr     r0, =subtest_label
    subs    r4, #8
    ldr     r5, [r0]
    str     r5, [r4]
    ldr     r5, [r0, #4]
    str     r5, [r4, #4]
    movs    r0, #4
    mov     r1, r4
    bkpt    #0xab
    pop     {r2, r3, r4, r5, pc}

    .ltorg

    .equ PRINT_BUF, 0x20000200
    .balign 4
subtest_label: .ascii "subtest "

    /* A Pico 2's cycles, with the cycle counter's loads */
    .equ N_SUBTESTS, 14
    .balign 4
expected:
    .word   135, 262, 167, 167, 167, 167, 135, 151, 151, 135
    .word   262, 262, 389, 325

    /* Each body: r1 = its words; returns r0 = its cycles. The cycle
     * counter's loads bracket the body alone, which has no branches and
     * starts on a word of bank 0. */
    .macro timed_body name
    .balign 16
    .thumb_func
\name:
    ldr     r0, [r6, #4]
    push    {r0}
    nop.w
    nop.w
    .balign 16
    .endm
    .macro end_body
    ldr     r0, [r6, #4]
    pop     {r2}
    subs    r0, r0, r2
    bx      lr
    .endm

    timed_body body_wide_same
    .rept 128
    ldr.w   r0, [r1]
    .endr
    end_body

    timed_body body_wide_four
    .rept 32
    ldr.w   r0, [r1]
    ldr.w   r0, [r1, #4]
    ldr.w   r0, [r1, #8]
    ldr.w   r0, [r1, #12]
    .endr
    end_body

    timed_body body_narrow_same
    .rept 128
    ldr     r0, [r1]
    .endr
    end_body

    timed_body body_narrow_two
    .rept 64
    ldr     r0, [r1]
    ldr     r0, [r1, #4]
    .endr
    end_body

    timed_body body_spaced
    .rept 64
    ldr.w   r0, [r1]
    adds    r2, #1
    adds    r2, #1
    adds    r2, #1
    .endr
    end_body

    .thumb_func
fault:
    orr     r7, r7, #0x100
    b       fail
    .ltorg

ok_message:   .asciz "RP2350_LOAD_WAIT_OK\n"
fail_message: .asciz "RP2350_LOAD_WAIT_FAIL\n"
