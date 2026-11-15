/*
 * Test: two DMA channels chained to each other on one data request line, as
 * a camera driver uses them, and the DREQ counter a trigger clears
 *
 * Steps 1 to 3: a channel armed on line 5 but not triggered counts the
 * line's pulses (DBG_CTDREQ), and triggering it clears the count: it then
 * waits for new pulses, as on a Pico 2.
 *
 * Steps 4 on: channels 0 and 1 chain to each other, both paced by line 5,
 * and carry 6 stripes of 16 words into two slots, channel 0 always into
 * SLOT0 and channel 1 into SLOT1. Each completion interrupts through
 * DMA_IRQ_0; the handler checks the stripe in the slot and points the
 * channel at the stripe after next. After each stripe the other channel,
 * just started by the chain, must wait with all 16 transfers to go and a
 * counter of 0, though it counted the finished channel's pulses.
 *
 * The run script (run_rp2350_dma_chain.py) plays the peripheral on line 5,
 * 2 ms apart: 10 pulses, 8, then 16 a stripe. gem5 only: on a Pico 2
 * nothing drives the line.
 *
 * Prints RP2350_DMA_CHAIN_OK or RP2350_DMA_CHAIN_FAIL through semihosting;
 * the number of the step that failed is at 0x20000104, and the first bad
 * stripe (plus 1) at 0x2000080c.
 */
    .syntax unified
    .cpu cortex-m33
    .thumb

    .equ TEST_SUBTEST, 0x20000104
    .equ STACK_TOP,    0x20020000
    .equ ISR,      0x20000800       /* stripes done, last INTS0, -, bad stripe + 1 */
    .equ DMA,      0x50000000
    .equ SRC,      0x20041000       /* 96 words: 1 to 96 */
    .equ SLOT0,    0x20042000
    .equ SLOT1,    0x20042100
    .equ P1DST,    0x20043000
    .equ CH_READ, 0x00
    .equ CH_WRITE, 0x04
    .equ CH_COUNT, 0x08
    .equ CH_CTRL_TRIG, 0x0c
    .equ AL1_CTRL, 0x10
    .equ INTE0, 0x404
    .equ INTF0, 0x408
    .equ INTS0, 0x40c
    .equ ABORT, 0x464
    .equ DBG, 0x800                 /* + 0x40 a channel: CTDREQ */
    .equ CLR, 0x3000                /* the atomic clear alias */
    .equ BUSY, 1 << 26
    .equ LINE, 5
    .equ WORDS, 16                  /* a stripe */
    .equ STRIPES, 6
    /* enabled, 32-bit, both addresses incrementing, paced by LINE */
    .equ PACED, 1 | (2 << 2) | (1 << 4) | (1 << 6) | (LINE << 17)

    .section .text
vectors:
    .word   STACK_TOP
    .word   Reset_Handler
    .rept   24
    .word   fault
    .endr
    .word   dma_isr             /* IRQ 10: DMA_IRQ_0 */
    .rept   41
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
    ldr     r11, =DMA
    /* SRC: words 1 to 96; the slots, P1DST and the handler's words cleared */
    ldr     r1, =SRC
    movs    r0, #1
1:  str     r0, [r1], #4
    adds    r0, #1
    cmp     r0, #(STRIPES * WORDS + 1)
    bne     1b
    movs    r0, #0
    ldr     r1, =SLOT0
    movs    r2, #(2 * 64)
2:  str     r0, [r1], #4
    subs    r2, #1
    bne     2b
    ldr     r1, =P1DST
    movs    r2, #8
3:  str     r0, [r1], #4
    subs    r2, #1
    bne     3b
    ldr     r1, =ISR
    str     r0, [r1]
    str     r0, [r1, #4]
    str     r0, [r1, #12]

    /* 1: channel 2 armed on line 5, not triggered, counts the 10 pulses */
    movs    r7, #1
    add     r8, r11, #0x80
    ldr     r0, =SRC
    str     r0, [r8, #CH_READ]
    ldr     r0, =P1DST
    str     r0, [r8, #CH_WRITE]
    movs    r0, #8
    str     r0, [r8, #CH_COUNT]
    ldr     r0, =PACED | (2 << 13)
    str     r0, [r8, #AL1_CTRL]
    add     r9, r11, #(DBG + 0x80)
    str     r0, [r9]                    /* any write clears the counter */
    ldr     r0, [r9]
    expect  r0, 0
    movs    r5, #10
    bl      wait_credit
    ldr     r0, [r9]
    expect  r0, 10

    /* 2: triggering it clears the count; it waits, all 8 to go */
    movs    r7, #2
    ldr     r0, =PACED | (2 << 13)
    str     r0, [r8, #CH_CTRL_TRIG]
    bl      settle
    ldr     r0, [r9]
    expect  r0, 0
    ldr     r0, [r8, #CH_COUNT]
    expect  r0, 8
    ldr     r0, [r8, #AL1_CTRL]
    expect  r0, PACED | (2 << 13) | BUSY

    /* 3: the next 8 pulses move the 8 words */
    movs    r7, #3
    movs    r5, #0
    bl      wait_count
    bl      settle
    ldr     r0, [r8, #AL1_CTRL]
    expect  r0, PACED | (2 << 13)
    ldr     r1, =P1DST
    ldr     r0, [r1, #28]
    expect  r0, 8

    /* 4: channels 0 and 1 chained to each other, 0 started on stripe 0,
     * 1 armed on stripe 1; their completions interrupt */
    movs    r7, #4
    movs    r0, #3
    str     r0, [r11, #INTE0]
    ldr     r0, =0xE000E100
    mov     r1, #(1 << 10)
    str     r1, [r0]
    cpsie   i
    add     r8, r11, #0x40
    ldr     r0, =SRC + 4 * WORDS
    str     r0, [r8, #CH_READ]
    ldr     r0, =SLOT1
    str     r0, [r8, #CH_WRITE]
    movs    r0, #WORDS
    str     r0, [r8, #CH_COUNT]
    ldr     r0, =PACED | (0 << 13)
    str     r0, [r8, #AL1_CTRL]
    mov     r8, r11
    ldr     r0, =SRC
    str     r0, [r8, #CH_READ]
    ldr     r0, =SLOT0
    str     r0, [r8, #CH_WRITE]
    movs    r0, #WORDS
    str     r0, [r8, #CH_COUNT]
    ldr     r0, =PACED | (1 << 13)
    str     r0, [r8, #CH_CTRL_TRIG]

    /* 5 to 10: after stripe s, the channel the chain started (the other
     * one) waits with all its transfers to go and a counter of 0 */
    movs    r4, #0                      /* s */
stripe:
    adds    r7, r4, #5
    adds    r5, r4, #1
    bl      wait_done
    bl      settle
    ldr     r1, =ISR
    ldr     r0, [r1]
    cmp     r0, r5
    bne     fail
    ldr     r0, [r1, #12]
    expect  r0, 0
    /* the other channel: 1 after an even stripe, 0 after an odd one */
    ands    r0, r4, #1
    eor     r0, r0, #1
    lsls    r0, r0, #6
    add     r8, r11, r0
    ldr     r0, [r8, #CH_COUNT]
    expect  r0, WORDS
    add     r9, r8, #DBG
    ldr     r0, [r9]
    expect  r0, 0
    adds    r4, #1
    cmp     r4, #STRIPES
    bne     stripe

    cpsid   i
    movs    r0, #3
    str     r0, [r11, #ABORT]
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

    /* Wait until channel r8's TRANS_COUNT is at most r5, or give up. */
    .thumb_func
wait_count:
    ldr     r1, =4000000
1:  ldr     r0, [r8, #CH_COUNT]
    cmp     r0, r5
    bls     2f
    subs    r1, #1
    bne     1b
2:  bx      lr

    /* Wait until the counter at r9 reaches r5, or give up. */
    .thumb_func
wait_credit:
    ldr     r1, =4000000
1:  ldr     r0, [r9]
    cmp     r0, r5
    bhs     2f
    subs    r1, #1
    bne     1b
2:  bx      lr

    /* Wait until the handler has seen r5 stripes, or give up. */
    .thumb_func
wait_done:
    ldr     r2, =ISR
    ldr     r1, =4000000
1:  ldr     r0, [r2]
    cmp     r0, r5
    bhs     2f
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

    /* DMA_IRQ_0: for the channel that finished stripe s (the handler's
     * count): its slot must hold words 16 s + 1 to 16 s + 16, and the
     * channel must be s's (0 for even stripes); then the channel is pointed
     * at stripe s + 2, if there is one, and the interrupt cleared in INTS0
     * and INTF0. */
    .thumb_func
dma_isr:
    push    {r4-r7, lr}
    ldr     r4, =DMA
    ldr     r5, =ISR
    ldr     r1, [r4, #INTS0]
    str     r1, [r5, #4]
    movs    r6, #0                      /* channel */
isr_channel:
    lsrs    r0, r1, r6
    tst     r0, #1
    beq     isr_next
    ldr     r7, [r5]                    /* s */
    /* the channel must be s's */
    and     r0, r7, #1
    cmp     r0, r6
    bne     isr_bad
    /* the slot's sum must be 256 s + 136 */
    ldr     r2, =SLOT0
    add     r2, r2, r6, lsl #8
    movs    r3, #0
    movs    r12, #WORDS
1:  ldr     r0, [r2], #4
    add     r3, r3, r0
    subs    r12, #1
    bne     1b
    lsls    r0, r7, #8
    adds    r0, #136
    cmp     r0, r3
    bne     isr_bad
    b       isr_point
isr_bad:
    ldr     r0, [r5, #12]
    cbnz    r0, isr_point
    adds    r0, r7, #1
    str     r0, [r5, #12]
isr_point:
    /* stripe s + 2 into the same slot, if there is one */
    adds    r0, r7, #2
    cmp     r0, #STRIPES
    bhs     isr_count
    add     r2, r4, r6, lsl #6
    ldr     r3, =SRC
    add     r3, r3, r0, lsl #6
    str     r3, [r2, #CH_READ]
    ldr     r3, =SLOT0
    add     r3, r3, r6, lsl #8
    str     r3, [r2, #CH_WRITE]
isr_count:
    adds    r7, #1
    str     r7, [r5]
    movs    r0, #1
    lsls    r0, r0, r6
    str     r0, [r4, #INTS0]
    ldr     r2, =DMA + CLR + INTF0
    str     r0, [r2]
isr_next:
    adds    r6, #1
    cmp     r6, #2
    bne     isr_channel
    pop     {r4-r7, pc}

    .thumb_func
fault:
    orr     r7, r7, #0x100
    b       fail
    .ltorg

ok_message:   .asciz "RP2350_DMA_CHAIN_OK\n"
fail_message: .asciz "RP2350_DMA_CHAIN_FAIL\n"
