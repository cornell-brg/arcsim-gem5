/*
 * Test: the RP2350's DMA controller (RP2350DMA)
 *
 * Channels copying words, halfwords and bytes, rings on either address, the
 * alias rows and their triggers, MULTI_CHAN_TRIGGER, chaining, the zero,
 * self-triggering and endless counts, CHAN_ABORT, null triggers, the atomic
 * register aliases, and DMA_IRQ_0 through the NVIC. A read that follows a
 * write to the same register must see the register, not the value written.
 *
 * Runs from SRAM on the RP2350 board with its DMA (run_rp2350_sdk.py --dma),
 * and as loaded on a Pico 2 through OpenOCD (see the README): every value
 * checked here is what the Pico 2 gives.
 *
 * Result at 0x20000100:
 *   0xCAFECAFE = all subtests passed (and RP2350_DMA_OK through semihosting)
 *   0xDEADDEAD = a subtest failed; its number is at 0x20000104
 */
    .syntax unified
    .cpu cortex-m33
    .thumb

    .equ TEST_RESULT,  0x20000100
    .equ TEST_SUBTEST, 0x20000104
    .equ PASS_VAL,     0xCAFECAFE
    .equ FAIL_VAL,     0xDEADDEAD
    .equ STACK_TOP,    0x20020000

    .equ DMA,      0x50000000
    .equ ISR,      0x20000800       /* handler's count, then the INTS0 it saw */
    .equ SRC,      0x20041000       /* 16 words, 0xA5000001 onwards */
    .equ DST,      0x20042000       /* 16 words, cleared before a subtest */
    .equ RING,     0x20044000       /* 16 KiB */
    .equ SINK,     0x20048000

    /* A channel's registers: alias 0, then the rows that end in each other
     * register as trigger */
    .equ CH_READ, 0x00
    .equ CH_WRITE, 0x04
    .equ CH_COUNT, 0x08
    .equ CH_CTRL_TRIG, 0x0c
    .equ AL1_CTRL, 0x10
    .equ AL1_READ, 0x14
    .equ AL1_WRITE, 0x18
    .equ AL1_COUNT_TRIG, 0x1c
    .equ AL2_WRITE_TRIG, 0x2c
    .equ AL3_READ_TRIG, 0x3c
    .equ INTR, 0x400
    .equ INTE0, 0x404
    .equ INTF0, 0x408
    .equ INTS0, 0x40c
    .equ MULTI, 0x450
    .equ ABORT, 0x464
    .equ NCH, 0x468

    .equ BUSY, 1 << 26
    .equ QUIET, 1 << 23
    /* enabled, both addresses incrementing, unpaced; 32, 16 and 8 bits */
    .equ C32, 1 | (2 << 2) | (1 << 4) | (1 << 6) | (0x3f << 17)
    .equ C16, 1 | (1 << 2) | (1 << 4) | (1 << 6) | (0x3f << 17)
    .equ C8, 1 | (0 << 2) | (1 << 4) | (1 << 6) | (0x3f << 17)
    /* enabled, 32-bit, read incrementing in a 16 KiB ring, write fixed, unpaced */
    .equ CRING, 1 | (2 << 2) | (1 << 4) | (14 << 8) | (0x3f << 17)

    .section .text
    /* The vector table first: the board's reset takes SP and PC from it */
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

    /* Fail the current subtest unless \reg holds \value. Uses r6. */
    .macro expect reg, value
    ldr     r6, =\value
    cmp     \reg, r6
    bne     fail
    .endm
    /* Fail unless the register at \offset of \base reads \value. Uses r0, r6. */
    .macro expreg offset, value, base=r8
    ldr     r0, [\base, #\offset]
    expect  r0, \value
    .endm
    /* Program the channel at r8 through alias 0, CTRL last: the trigger */
    .macro setch read, write, count, ctrl
    ldr     r0, =\read
    str     r0, [r8, #CH_READ]
    ldr     r0, =\write
    str     r0, [r8, #CH_WRITE]
    ldr     r0, =\count
    str     r0, [r8, #CH_COUNT]
    ldr     r0, =\ctrl
    str     r0, [r8, #CH_CTRL_TRIG]
    .endm
    /* Clear the channels' interrupts in \bits */
    .macro ack bits
    ldr     r0, =\bits
    str     r0, [r11, #INTR]
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
    movs    r0, #0
    ldr     r1, =ISR
    str     r0, [r1]
    str     r0, [r1, #4]
    ldr     r1, =SRC
    ldr     r0, =0xA5000001
    movs    r2, #16
1:  str     r0, [r1], #4
    adds    r0, #1
    subs    r2, #1
    bne     1b

    /* 1: 16 channels */
    movs    r7, #1
    expreg  NCH, 16, r11

    /* 2: channel 0 copies 8 words; its registers afterwards; its interrupt
     * bit clears when written, as the read right after the write shows */
    movs    r7, #2
    bl      clear_dst
    mov     r8, r11
    setch   SRC, DST, 8, C32
    bl      wait_idle
    expreg  CH_CTRL_TRIG, C32
    expreg  CH_READ, SRC + 32
    expreg  CH_WRITE, DST + 32
    expreg  CH_COUNT, 0
    expreg  INTR, 1, r11
    bl      sum_dst
    expect  r0, 0x28000024          /* 8 * 0xA5000000 + (1 + ... + 8) */
    movs    r0, #1
    str     r0, [r11, #INTR]
    ldr     r0, [r11, #INTR]
    expect  r0, 0

    /* 3: channel 1 through alias 1; TRANS_COUNT reads 0 until a sequence
     * runs; MULTI_CHAN_TRIGGER starts it; triggered again with nothing
     * rewritten, it copies the same number from where the addresses are */
    movs    r7, #3
    bl      clear_dst
    add     r8, r11, #0x40
    ldr     r0, =SRC
    str     r0, [r8, #AL1_READ]
    ldr     r0, =DST
    str     r0, [r8, #AL1_WRITE]
    ldr     r0, =C32 | (1 << 13)
    str     r0, [r8, #AL1_CTRL]
    movs    r0, #5
    str     r0, [r8, #CH_COUNT]
    ldr     r0, [r8, #CH_COUNT]
    expect  r0, 0
    movs    r0, #2
    str     r0, [r11, #MULTI]
    bl      wait_idle
    bl      sum_dst
    expect  r0, 0x3900000F          /* words 1-5 */
    movs    r0, #2
    str     r0, [r11, #MULTI]
    bl      wait_idle
    bl      sum_dst
    expect  r0, 0x72000037          /* words 1-10 */
    expreg  CH_READ, SRC + 40
    ack     2

    /* 4: a channel triggered with a count of 0 completes at once, in the
     * normal and the endless mode: not busy, its interrupt raised */
    movs    r7, #4
    add     r8, r11, #0x80
    setch   SRC, DST, 0, C32 | (2 << 13)
    bl      short_delay
    expreg  CH_CTRL_TRIG, C32 | (2 << 13)
    expreg  INTR, 4, r11
    ack     4
    setch   SRC, DST, 0xF0000000, C32 | (2 << 13)
    bl      short_delay
    expreg  CH_CTRL_TRIG, C32 | (2 << 13)
    expreg  INTR, 4, r11
    ack     4

    /* 5: a read address in an 8-byte ring goes round it; the write address
     * runs on */
    movs    r7, #5
    bl      clear_dst
    add     r8, r11, #0xc0
    setch   SRC, DST, 6, C32 | (3 << 13) | (3 << 8)
    bl      wait_idle
    expreg  CH_READ, SRC
    expreg  CH_WRITE, DST + 24
    bl      sum_dst
    expect  r0, 0xDE000009          /* words 1, 2, 1, 2, 1, 2 */
    /* and a write address in the ring: the last two words land in it */
    bl      clear_dst
    setch   SRC, DST, 6, C32 | (3 << 13) | (3 << 8) | (1 << 12)
    bl      wait_idle
    expreg  CH_READ, SRC + 24
    expreg  CH_WRITE, DST
    bl      sum_dst
    expect  r0, 0x4A00000B          /* words 5, 6 */
    ack     8

    /* 6: a quiet channel does not interrupt when it completes, and does
     * when its trigger register is written 0 */
    movs    r7, #6
    add     r8, r11, #0x100
    setch   SRC, DST, 4, C32 | (4 << 13) | QUIET
    bl      wait_idle
    expreg  INTR, 0, r11
    movs    r0, #0
    str     r0, [r8, #AL1_COUNT_TRIG]
    bl      short_delay
    expreg  INTR, 16, r11
    expreg  CH_CTRL_TRIG, C32 | (4 << 13) | QUIET
    ack     16

    /* 7: an endless channel stays busy with its count unchanged; aborted,
     * it stops with a count of 0 and no interrupt */
    movs    r7, #7
    add     r8, r11, #0x140
    setch   RING, SINK, 0xFFFFFFFF, CRING | (5 << 13)
    bl      short_delay
    expreg  CH_CTRL_TRIG, CRING | (5 << 13) | BUSY
    expreg  CH_COUNT, 0xFFFFFFFF
    movs    r0, #32
    str     r0, [r11, #ABORT]
    bl      wait_idle
    expreg  CH_CTRL_TRIG, CRING | (5 << 13)
    expreg  CH_COUNT, 0xF0000000
    expreg  INTR, 0, r11
    expreg  ABORT, 0, r11

    /* 8: a self-triggering channel interrupts each time round and stays
     * busy until aborted */
    movs    r7, #8
    setch   RING, SINK, 0x10000004, CRING | (5 << 13)
    bl      short_delay
    expreg  CH_CTRL_TRIG, CRING | (5 << 13) | BUSY
    expreg  INTR, 32, r11
    movs    r0, #32
    str     r0, [r11, #ABORT]
    bl      wait_idle
    expreg  CH_CTRL_TRIG, CRING | (5 << 13)
    expreg  CH_COUNT, 0x10000000
    ack     32
    expreg  INTR, 0, r11

    /* 9: a disabled channel ignores its trigger */
    movs    r7, #9
    bl      clear_dst
    add     r8, r11, #0x180
    setch   SRC, DST, 4, (C32 | (6 << 13)) & ~1
    bl      short_delay
    expreg  CH_CTRL_TRIG, (C32 | (6 << 13)) & ~1
    expreg  INTR, 0, r11
    bl      sum_dst
    expect  r0, 0

    /* 10: bytes, then halfwords: only those bytes change, and the
     * addresses move by them */
    movs    r7, #10
    bl      clear_dst
    setch   SRC, DST + 1, 5, C8 | (6 << 13)
    bl      wait_idle
    expreg  CH_READ, SRC + 5
    expreg  CH_WRITE, DST + 6
    ldr     r1, =DST
    ldr     r0, [r1]
    expect  r0, 0x00000100          /* word 1's bytes 01 00 00, from byte 1 */
    ldr     r0, [r1, #4]
    expect  r0, 0x000002A5          /* its byte A5, then word 2's 02 */
    bl      clear_dst
    setch   SRC + 2, DST + 2, 3, C16 | (6 << 13)
    bl      wait_idle
    expreg  CH_READ, SRC + 8
    expreg  CH_WRITE, DST + 8
    ldr     r1, =DST
    ldr     r0, [r1]
    expect  r0, 0xA5000000          /* the high half of word 1 */
    ldr     r0, [r1, #4]
    expect  r0, 0xA5000002
    ldr     r0, [r1, #8]
    expect  r0, 0
    ack     64

    /* 11: channel 8 chains to channel 9: triggering 8 runs both */
    movs    r7, #11
    bl      clear_dst
    add     r8, r11, #0x240
    ldr     r0, =SRC + 16
    str     r0, [r8, #AL1_READ]
    ldr     r0, =DST + 16
    str     r0, [r8, #AL1_WRITE]
    movs    r0, #4
    str     r0, [r8, #CH_COUNT]
    ldr     r0, =C32 | (9 << 13)
    str     r0, [r8, #AL1_CTRL]
    add     r8, r11, #0x200
    setch   SRC, DST, 4, C32 | (9 << 13)
    add     r8, r11, #0x240
    bl      wait_idle
    bl      short_delay
    expreg  CH_CTRL_TRIG, C32 | (9 << 13)
    expreg  INTR, 0x300, r11
    bl      sum_dst
    expect  r0, 0x28000024          /* words 1-8 */
    ack     0x300

    /* 12: writing WRITE_ADDR_TRIG or READ_ADDR_TRIG starts a channel */
    movs    r7, #12
    bl      clear_dst
    add     r8, r11, #0x280
    ldr     r0, =SRC
    str     r0, [r8, #AL1_READ]
    movs    r0, #2
    str     r0, [r8, #CH_COUNT]
    ldr     r0, =C32 | (10 << 13)
    str     r0, [r8, #AL1_CTRL]
    ldr     r0, =DST
    str     r0, [r8, #AL2_WRITE_TRIG]
    bl      wait_idle
    bl      sum_dst
    expect  r0, 0x4A000003          /* words 1, 2 */
    ldr     r0, =SRC + 8
    str     r0, [r8, #AL3_READ_TRIG]
    bl      wait_idle
    bl      sum_dst
    expect  r0, 0x9400000A          /* words 1-4 */
    ack     0x400

    /* 13: INTE0 through the SET, XOR and CLR aliases */
    movs    r7, #13
    movs    r0, #0xF0
    add     r1, r11, #0x2000
    str     r0, [r1, #INTE0]
    expreg  INTE0, 0xF0, r11
    movs    r0, #0x30
    add     r1, r11, #0x1000
    str     r0, [r1, #INTE0]
    expreg  INTE0, 0xC0, r11
    movs    r0, #0x40
    add     r1, r11, #0x3000
    str     r0, [r1, #INTE0]
    expreg  INTE0, 0x80, r11
    movs    r0, #0
    str     r0, [r11, #INTE0]

    /* 14: INTF0 forces INTS0 whatever INTE0 says, and not INTR. The NVIC
     * latches the line: released again before the interrupt is enabled,
     * the handler still runs once, finding nothing in INTS0. */
    movs    r7, #14
    movs    r0, #0x80
    str     r0, [r11, #INTF0]
    expreg  INTS0, 0x80, r11
    expreg  INTR, 0, r11
    movs    r0, #0
    str     r0, [r11, #INTF0]
    expreg  INTS0, 0, r11
    ldr     r0, =0xE000E100
    mov     r1, #(1 << 10)
    str     r1, [r0]
    cpsie   i
    bl      short_delay
    ldr     r1, =ISR
    ldr     r0, [r1]
    expect  r0, 1
    ldr     r0, [r1, #4]
    expect  r0, 0

    /* 15: channel 6 interrupts through DMA_IRQ_0 when it completes; the
     * handler sees its bit in INTS0 and clears it there */
    movs    r7, #15
    movs    r0, #0x40
    str     r0, [r11, #INTE0]
    add     r8, r11, #0x180
    setch   SRC, DST, 4, C32 | (6 << 13)
    bl      wait_idle
    bl      short_delay
    ldr     r1, =ISR
    ldr     r0, [r1]
    expect  r0, 2
    ldr     r0, [r1, #4]
    expect  r0, 0x40
    expreg  INTS0, 0, r11
    expreg  INTR, 0, r11
    /* and a forced one */
    movs    r0, #0x80
    str     r0, [r11, #INTF0]
    bl      short_delay
    ldr     r1, =ISR
    ldr     r0, [r1]
    expect  r0, 3
    ldr     r0, [r1, #4]
    expect  r0, 0x80
    cpsid   i

    /* 16: a channel is busy from the write that triggers it: the read
     * right after sees BUSY, and 1024 words later it is idle */
    movs    r7, #16
    add     r8, r11, #0x1c0
    ldr     r0, =RING
    str     r0, [r8, #CH_READ]
    ldr     r0, =RING + 0x2000
    str     r0, [r8, #CH_WRITE]
    mov     r0, #1024
    str     r0, [r8, #CH_COUNT]
    ldr     r0, =C32 | (7 << 13)
    str     r0, [r8, #CH_CTRL_TRIG]
    ldr     r0, [r8, #CH_CTRL_TRIG]
    expect  r0, C32 | (7 << 13) | BUSY
    bl      wait_idle
    expreg  CH_CTRL_TRIG, C32 | (7 << 13)
    expreg  CH_WRITE, RING + 0x3000

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

    /* r8 = channel: wait until it is not busy, or give up */
    .thumb_func
wait_idle:
    ldr     r1, =200000
1:  ldr     r0, [r8, #CH_CTRL_TRIG]
    tst     r0, #BUSY
    beq     2f
    subs    r1, #1
    bne     1b
2:  bx      lr

    .thumb_func
short_delay:
    ldr     r1, =200
1:  subs    r1, #1
    bne     1b
    bx      lr

    .thumb_func
clear_dst:
    ldr     r1, =DST
    movs    r0, #0
    movs    r2, #16
1:  str     r0, [r1], #4
    subs    r2, #1
    bne     1b
    bx      lr

    /* r0 = sum of DST's 16 words */
    .thumb_func
sum_dst:
    ldr     r1, =DST
    movs    r0, #0
    movs    r2, #16
1:  ldr     r3, [r1], #4
    add     r0, r0, r3
    subs    r2, #1
    bne     1b
    bx      lr

    /* Counts, records INTS0, clears what it shows and any forced bits */
    .thumb_func
dma_isr:
    ldr     r0, =DMA
    ldr     r1, [r0, #INTS0]
    ldr     r2, =ISR
    ldr     r3, [r2]
    adds    r3, #1
    str     r3, [r2]
    str     r1, [r2, #4]
    str     r1, [r0, #INTS0]
    movs    r1, #0
    str     r1, [r0, #INTF0]
    bx      lr

    .thumb_func
fault:
    orr     r7, r7, #0x100
    b       fail
    .ltorg

ok_message:   .asciz "RP2350_DMA_OK\n"
fail_message: .asciz "RP2350_DMA_FAIL\n"
