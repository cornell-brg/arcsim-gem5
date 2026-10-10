/*
 * Test: a DMA channel paced by a peripheral's data request line
 *
 * A channel whose TREQ_SEL names a peripheral's line transfers once per
 * pulse on it and no more. The run script (run_rp2350_dma_dreq.py) plays the
 * peripheral on line 5: 3 pulses, then 200 at once, of which the channel's
 * six-bit counter keeps 63, then 34. The channel has 100 words to copy, so
 * it stands at 97, then 34, then 0 left.
 *
 * Runs from SRAM on the RP2350 board with its DMA. gem5 only: on a Pico 2
 * nothing drives the line.
 *
 * Prints RP2350_DMA_DREQ_OK or RP2350_DMA_DREQ_FAIL through semihosting; the
 * number of the step that failed is at 0x20000104.
 */
    .syntax unified
    .cpu cortex-m33
    .thumb

    .equ TEST_SUBTEST, 0x20000104
    .equ STACK_TOP,    0x20020000
    .equ DMA,      0x50000000
    .equ SRC,      0x20041000
    .equ DST,      0x20042000
    .equ CH_READ, 0x00
    .equ CH_WRITE, 0x04
    .equ CH_COUNT, 0x08
    .equ CH_CTRL_TRIG, 0x0c
    .equ INTR, 0x400
    .equ BUSY, 1 << 26
    .equ LINE, 5
    /* enabled, 32-bit, both addresses incrementing, paced by LINE, no chain */
    .equ CTRL, 1 | (2 << 2) | (1 << 4) | (1 << 6) | (LINE << 17)

    .section .text
    /* The vector table first: the board's reset takes SP and PC from it */
vectors:
    .word   STACK_TOP
    .word   Reset_Handler
    .rept   14
    .word   fault
    .endr

    /* Fail the current step unless \reg holds \value. Uses r6. */
    .macro expect reg, value
    ldr     r6, =\value
    cmp     \reg, r6
    bne     fail
    .endm

    .thumb_func
    .globl Reset_Handler
Reset_Handler:
    ldr     r0, =STACK_TOP
    mov     sp, r0
    ldr     r0, =vectors
    ldr     r1, =0xE000ED08
    str     r0, [r1]
    ldr     r8, =DMA
    /* SRC: words 1 to 100; DST: zeros */
    ldr     r1, =SRC
    ldr     r2, =DST
    movs    r0, #1
    movs    r3, #0
1:  str     r0, [r1], #4
    str     r3, [r2], #4
    adds    r0, #1
    cmp     r0, #101
    bne     1b

    /* 1: triggered, the channel is busy and waits: no pulse yet */
    movs    r7, #1
    ldr     r0, =SRC
    str     r0, [r8, #CH_READ]
    ldr     r0, =DST
    str     r0, [r8, #CH_WRITE]
    movs    r0, #100
    str     r0, [r8, #CH_COUNT]
    ldr     r0, =CTRL
    str     r0, [r8, #CH_CTRL_TRIG]
    bl      settle
    ldr     r0, [r8, #CH_COUNT]
    expect  r0, 100
    ldr     r0, [r8, #CH_CTRL_TRIG]
    expect  r0, CTRL | BUSY

    /* 2: 3 pulses, 3 transfers */
    movs    r7, #2
    movs    r5, #97
    bl      wait_count
    bl      settle
    ldr     r0, [r8, #CH_COUNT]
    expect  r0, 97
    ldr     r1, =DST
    ldr     r0, [r1, #8]
    expect  r0, 3
    ldr     r0, [r1, #12]
    expect  r0, 0

    /* 3: 200 pulses at once, 63 transfers */
    movs    r7, #3
    movs    r5, #34
    bl      wait_count
    bl      settle
    ldr     r0, [r8, #CH_COUNT]
    expect  r0, 34
    ldr     r0, [r8, #CH_CTRL_TRIG]
    expect  r0, CTRL | BUSY

    /* 4: the last 34: the sequence completes */
    movs    r7, #4
    movs    r5, #0
    bl      wait_count
    bl      settle
    ldr     r0, [r8, #CH_CTRL_TRIG]
    expect  r0, CTRL
    ldr     r0, [r8, #INTR]
    expect  r0, 1
    ldr     r1, =DST
    ldr     r0, [r1, #396]
    expect  r0, 100

    ldr     r1, =ok_message
    b       done
fail:
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

    /* Wait until TRANS_COUNT is at most r5, or give up. The script sends
     * its pulses 2 ms apart; this gives up after far longer. */
    .thumb_func
wait_count:
    ldr     r1, =4000000
1:  ldr     r0, [r8, #CH_COUNT]
    cmp     r0, r5
    bls     2f
    subs    r1, #1
    bne     1b
2:  bx      lr

    /* Long enough for transfers in flight to finish, far shorter than the
     * script's 2 ms */
    .thumb_func
settle:
    ldr     r1, =2000
1:  subs    r1, #1
    bne     1b
    bx      lr

    .thumb_func
fault:
    orr     r7, r7, #0x100
    b       fail
    .ltorg

ok_message:   .asciz "RP2350_DMA_DREQ_OK\n"
fail_message: .asciz "RP2350_DMA_DREQ_FAIL\n"
