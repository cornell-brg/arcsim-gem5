/*
 * Test: the RP2350's flash execute-in-place path (RP2350XIP)
 *
 * Times single loads from flash with the cycle counter, against a load that
 * hits the XIP cache: a miss from a released chip select, a miss on a line
 * elsewhere while the chip select is still held, a miss on the next line in
 * address order, and the same after the chip select's cooldown. Then which
 * line a miss replaces, and what a DMA channel reading flash for ever does
 * to the core's hits.
 *
 * Runs from SRAM, so its own instruction fetches stay off the XIP path, on
 * the RP2350 board with its DMA and the XIP model (run_rp2350_sdk.py --dma
 * --xip-model qmi). It also runs on a Pico 2 whose flash interface the Pico
 * SDK has set up (see the README): the cycle counts checked here are what a
 * Pico 2 gives, and the ranges hold what it gives.
 *
 * Result at 0x20000100:
 *   0xCAFECAFE = all subtests passed (and RP2350_XIP_OK through semihosting)
 *   0xDEADDEAD = a subtest failed; its number is at 0x20000104, plus 0x100
 *                if a fault was taken, and the value it rejected at
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

    .equ MAINT,    0x18000000       /* cache maintenance writes */
    .equ FLASH,    0x10100000       /* flash this test reads; any contents */
    .equ TABLE,    0x10200000       /* the DMA's 32 KiB of flash */
    .equ SINK,     0x20081000       /* the DMA's write word, SRAM bank 9 */

    /* enabled, 32-bit, unpaced, write address fixed, reads round 32 KiB */
    .equ CTABLE, 1 | (2 << 2) | (0x3f << 17) | (1 << 4) | (15 << 8)

    .section .text
    /* The vector table first: the board's reset takes SP and PC from it */
vectors:
    .word   STACK_TOP
    .word   Reset_Handler
    .rept   14
    .word   fault
    .endr

    /* r0 = the cycles of one load from [\areg], with the cycle counter's
     * own two loads. The first follows a register move and the second
     * takes its address through the loaded value, so that neither can
     * start before the load ahead of it has finished. Uses r2, r3. */
    .macro tread areg
    mov     r2, r6
    ldr     r2, [r2, #4]
    ldr     r3, [\areg]
    bics    r3, r3
    adds    r3, r3, r6
    ldr     r0, [r3, #4]
    subs    r0, r0, r2
    .endm

    /* Fail unless the load from [\areg] takes \extra cycles more than a
     * hit (r5); uses r0, r2, r3 */
    .macro expect_extra areg, extra
    tread   \areg
    subs    r0, r0, r5
    cmp     r0, #\extra
    bne     fail
    .endm

    /* Fail unless \lo <= r0 <= \hi */
    .macro expect_range lo, hi
    cmp     r0, #\lo
    blo     fail
    cmp     r0, #\hi
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
    bl      dma_stop
    ldr     r0, =0xE000EDFC         /* DWT cycle counter on */
    ldr     r1, [r0]
    orr     r1, r1, #(1 << 24)
    str     r1, [r0]
    ldr     r6, =0xE0001000
    ldr     r1, [r6]
    orr     r1, r1, #1
    str     r1, [r6]
    ldr     r4, =FLASH

    /* 1: with every line invalid and the chip select long released, a
     * miss takes 110 cycles more than the hit that follows it */
    movs    r7, #1
    bl      flush
    tread   r4
    mov     r9, r0
    tread   r4
    mov     r5, r0                  /* a hit, with the counter's loads */
    sub     r0, r9, r5
    cmp     r0, #110
    bne     fail

    /* 2: the second word of the line hits too */
    movs    r7, #2
    add     r1, r4, #4
    expect_extra r1, 0

    /* 3: a miss elsewhere, with the chip select still held, first ends
     * the last transfer: 120 cycles */
    movs    r7, #3
    add     r1, r4, #0x108
    expect_extra r1, 120
    add     r1, r4, #0x210
    expect_extra r1, 120

    /* 4: the next lines in address order continue the transfer: 50 */
    movs    r7, #4
    add     r1, r4, #0x218
    expect_extra r1, 50
    add     r1, r4, #0x220
    expect_extra r1, 50

    /* 5: so does the next line a few dozen cycles later */
    movs    r7, #5
    movs    r0, #6
1:  subs    r0, #1
    bne     1b
    add     r1, r4, #0x228
    expect_extra r1, 50

    /* 6: once the chip select has been released, 110 again */
    movs    r7, #6
    mov     r0, #200
1:  subs    r0, #1
    bne     1b
    add     r1, r4, #0x230
    expect_extra r1, 110

    /* 7: every second line is not a continuation */
    movs    r7, #7
    add     r1, r4, #0x240
    expect_extra r1, 120

    /* 8: a line read twice hits the second time, in all 256 sets tried */
    movs    r7, #8
    movs    r1, #0
    bl      replaced
    cmp     r0, #0
    bne     fail

    /* 9: a miss takes either way of its set, even with the other empty:
     * after a second line of its set, the first is gone about half the
     * time (a Pico 2: 129 of 256) */
    movs    r7, #9
    movs    r1, #1
    bl      replaced
    expect_range 96, 160

    /* 10: and after a third, about three times in four (a Pico 2: 200) */
    movs    r7, #10
    movs    r1, #2
    bl      replaced
    expect_range 160, 224

    /* 11: beside a DMA channel that reads flash for ever, 64 lines already
     * in the cache, read in address order: each waits for the bank the
     * DMA's line is coming into (a Pico 2: all 64 take over 12 cycles) */
    movs    r7, #11
    bl      flush
    ldr     r4, =FLASH + 0x11000
    movs    r1, #64
1:  ldr     r0, [r4], #8
    subs    r1, #1
    bne     1b
    bl      dma_start
    ldr     r4, =FLASH + 0x11000
    movs    r1, #64
    bl      slow_reads
    expect_range 56, 64

    /* 12: one line read 256 times waits only while the DMA's line is
     * coming into its own bank: once for every two lines, four words, the
     * DMA reads (a Pico 2: 86 waits beside 344 words, 85 beside 336) */
    movs    r7, #12
    ldr     r4, =FLASH + 0x20000
    ldr     r0, [r4]
    bl      same_line
    add     r0, r1, #16
    sub     r0, r0, r12, lsl #2
    expect_range 0, 32
    ldr     r4, =FLASH + 0x20008
    ldr     r0, [r4]
    bl      same_line
    add     r0, r1, #16
    sub     r0, r0, r12, lsl #2
    expect_range 0, 32
    bl      dma_stop

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

    /* Invalidates every cache line (by set and way) */
    .thumb_func
flush:
    ldr     r0, =MAINT
    ldr     r1, =0x4000
    movs    r2, #0
1:  strb    r2, [r0], #8
    subs    r1, #8
    bne     1b
    bx      lr

    .thumb_func
dma_start:
    ldr     r0, =TABLE
    str     r0, [r8, #CH_READ]
    ldr     r0, =SINK
    str     r0, [r8, #CH_WRITE]
    ldr     r0, =0x0FFFFFFF
    str     r0, [r8, #CH_COUNT]
    ldr     r0, =CTABLE
    str     r0, [r8, #CH_CTRL_TRIG]
    ldr     r0, =2000               /* let it reach its steady state */
1:  subs    r0, #1
    bne     1b
    bx      lr

    .thumb_func
dma_stop:
    mov     r0, #(1 << 7)
    str     r0, [r11, #ABORT]
1:  ldr     r0, [r11, #ABORT]
    cmp     r0, #0
    bne     1b
    mov     r0, #(1 << 7)
    str     r0, [r11, #INTR]
    bx      lr

    /* r0 = how many of r1 loads, 8 bytes apart from r4, take over 12
     * cycles; uses r1-r4, r12 */
    .thumb_func
slow_reads:
    movs    r12, #0
1:  tread   r4
    cmp     r0, #12
    it      hi
    addhi   r12, r12, #1
    adds    r4, #8
    subs    r1, #1
    bne     1b
    mov     r0, r12
    bx      lr

    /* r12 = how many of 256 loads from r4 take over 12 cycles, r1 = the
     * words the DMA reads meanwhile; uses r0-r3, r9 */
    .thumb_func
same_line:
    ldr     r9, [r8, #CH_COUNT]
    mov     r1, #256
    movs    r12, #0
1:  tread   r4
    cmp     r0, #12
    it      hi
    addhi   r12, r12, #1
    subs    r1, #1
    bne     1b
    ldr     r1, [r8, #CH_COUNT]
    sub     r1, r9, r1
    bx      lr

    /* Over 256 sets, from every line invalid: reads a first line, then
     * r1 (0, 1 or 2) other lines of its set, then the first again. r0 =
     * how many of those last reads take over 12 cycles. */
    .thumb_func
replaced:
    push    {r4, r5, r9, lr}
    mov     r5, r1
    bl      flush
    ldr     r4, =FLASH + 0x30000
    mov     r9, #256
    movs    r12, #0
1:  ldr     r0, [r4]
    cbz     r5, 2f
    add     r1, r4, #0x2000
    ldr     r0, [r1]
    cmp     r5, #1
    beq     2f
    add     r1, r4, #0x4000
    ldr     r0, [r1]
2:  tread   r4
    cmp     r0, #12
    it      hi
    addhi   r12, r12, #1
    adds    r4, #8
    subs    r9, #1
    bne     1b
    mov     r0, r12
    pop     {r4, r5, r9, pc}

    .thumb_func
fault:
    orr     r7, r7, #0x100
    b       fail
    .ltorg

ok_message:   .asciz "RP2350_XIP_OK\n"
fail_message: .asciz "RP2350_XIP_FAIL\n"
