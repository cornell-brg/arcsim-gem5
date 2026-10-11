/*
 * Test: the core fetches only a few words past a branch
 *
 * A Cortex-M33 keeps a few bytes of prefetched instructions: past a branch
 * it has fetched one word, or two, when the branch takes effect. The lines
 * of flash it fetched are in the XIP cache afterwards, so reads of them are
 * quick. Each stub in flash is some nops and a branch, in turn bx lr, a
 * slow udiv and bx lr, b, a taken bne, pop {pc}, bx lr after 32-bit nops and
 * a 32-bit b. With the stub's lines and the two after them in the cache, the
 * call must fetch no line further on and so wait for none. The last stub is
 * 64 nops, which the core must not run ahead of either.
 *
 * Runs from SRAM on the RP2350 board with the XIP model (run_rp2350_sdk.py
 * --dma --xip-model qmi), its stubs in flash at 0x103f0000. It also runs on
 * a Pico 2 with the stubs written there and the flash interface as the Pico
 * SDK sets it up (see the README).
 *
 * Result at 0x20000100:
 *   0xCAFECAFE = all subtests passed (and RP2350_FETCH_AHEAD_OK through
 *                semihosting)
 *   0xDEADDEAD = a subtest failed; its number is at 0x20000104, plus 0x100
 *                if a fault was taken, and the value it rejected at
 *                0x20000108: for subtest 1 the stub's number from bit 24,
 *                the call's cycles from bit 8 and, in bits 6:0, which of
 *                the seven lines from the stub's start were in the cache
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

    .equ MAINT,    0x18000000       /* cache maintenance writes */

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

    .macro delay rounds
    mov     r0, #\rounds
1:  subs    r0, #1
    bne     1b
    .endm

    /* A stub of \n nops and a branch, 64 bytes from the last; kind 0: bx
     * lr; 1: udiv, bx lr; 2: b; 3: bne, taken; 4: pop {pc}; 5: 32-bit nops,
     * bx lr; 6: 32-bit b. The branches go to a bx lr at the stub's start +
     * 56. Its start and the bytes it runs go in the table. */
    .macro stub kind, n
    .section .flash, "ax"
    .balign 64
    .thumb_func
s_\kind\()_\n:
    .rept \n
    .if \kind == 5
    nop.w
    .else
    nop
    .endif
    .endr
    .if \kind == 0
    bx      lr
    .endif
    .if \kind == 1
    udiv    r0, r1, r2
    bx      lr
    .endif
    .if \kind == 2
    b.n     9f
    .endif
    .if \kind == 3
    bne.n   9f
    .endif
    .if \kind == 4
    pop     {pc}
    .endif
    .if \kind == 5
    bx      lr
    .endif
    .if \kind == 6
    b.w     9f
    .endif
e_\kind\()_\n:
    .org    s_\kind\()_\n + 56
9:  bx      lr
    .section .table, "a"
    .word   s_\kind\()_\n, e_\kind\()_\n - s_\kind\()_\n
    .endm

    .irp kind, 0, 1, 2, 3, 4, 5, 6
    .irp n, 0, 1, 2, 3, 4, 5, 6, 7
    stub \kind, \n
    .endr
    .endr
    .section .table_end, "a"
    .word   0, 0

    .section .flash, "ax"
    .balign 64
    .thumb_func
long_stub:
    .rept 64
    nop
    .endr
    bx      lr
    .equ LONG_LINES, 17             /* the lines it runs */

    .section .text
    .thumb_func
    .globl Reset_Handler
Reset_Handler:
    ldr     r0, =STACK_TOP
    mov     sp, r0
    ldr     r0, =vectors
    ldr     r1, =0xE000ED08
    str     r0, [r1]
    cpsid   i
    ldr     r0, =0xE000EDFC         /* DWT cycle counter on */
    ldr     r1, [r0]
    orr     r1, r1, #(1 << 24)
    str     r1, [r0]
    ldr     r6, =0xE0001000
    ldr     r1, [r6]
    orr     r1, r1, #1
    str     r1, [r6]

    /* 1: with a stub's lines and the two after them in the cache, its call
     * leaves no further line there, and waits for none (a Pico 2: 5 to 20
     * cycles) */
    movs    r7, #1
    ldr     r4, =stubs
    movs    r9, #0
next_stub:
    ldr     r5, [r4]
    cmp     r5, #0
    beq     long_body
    bic     r5, r5, #1              /* the stub's first line */
    ldr     r8, [r4, #4]            /* the bytes it runs */
    sub     r10, r8, #1
    lsr     r10, r10, #3
    add     r10, r10, #2            /* the last line to have in the cache */
    bl      run_stub
    movs    r1, #0
    mov     r12, #0
1:  add     lr, r5, r12, lsl #3
    tread   lr
    cmp     r0, #12
    bhi     2f
    movs    r0, #1
    lsl     r0, r0, r12
    orrs    r1, r0
2:  add     r12, r12, #1
    cmp     r12, #7
    bne     1b
    orr     r0, r1, r11, lsl #8
    orr     r0, r0, r9, lsl #24
    movs    r2, #2
    lsl     r2, r2, r10
    subs    r2, #1                  /* lines 0 to r10 */
    cmp     r1, r2
    bne     fail
    cmp     r11, #60
    bhs     fail
    adds    r4, #8
    add     r9, r9, #1
    b       next_stub

    /* 2: nor does the core run ahead of 64 nops: with their lines and the
     * two after in the cache, the call waits for none (a Pico 2: 37
     * cycles) */
long_body:
    movs    r7, #2
    ldr     r5, =long_stub
    bic     r5, r5, #1
    mov     r10, #(LONG_LINES + 1)
    bl      run_stub
    mov     r0, r11
    cmp     r11, #110
    bhs     fail

    /* 3: and the four lines after those are not in the cache */
    movs    r7, #3
    add     r4, r5, #((LONG_LINES + 2) * 8)
    movs    r1, #4
1:  tread   r4
    cmp     r0, #12
    bls     fail
    adds    r4, #8
    subs    r1, #1
    bne     1b

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

    /* Empties the cache, reads in lines 0 to r10 from r5 and the lines a
     * stub's branch goes to, and calls r5 with what the stubs need: r1 / r2
     * a slow division, ne, and the return address in lr and on the stack.
     * Returns r11 = the call's cycles; uses r0-r3, r12. */
    .thumb_func
run_stub:
    push    {r4, lr}
    bl      flush
    mov     r0, r10
    mov     r1, r5
1:  ldr     r2, [r1], #8
    subs    r0, #1
    bpl     1b
    ldr     r2, [r5, #56]
    ldr     r2, [r5, #64]
    delay   400                     /* until the chip select is released */
    mov     r4, sp
    ldr     r3, =after
    push    {r3}
    mov     lr, r3
    orr     r12, r5, #1
    ldr     r1, =0x7fffffff
    movs    r2, #1
    movs    r0, #0
    cmp     r0, #1
    ldr     r11, [r6, #4]
    bx      r12
    .balign 4
    .thumb_func
after:
    ldr     r0, [r6, #4]
    sub     r11, r0, r11
    mov     sp, r4
    delay   400                     /* until a line on its way has come */
    pop     {r4, pc}

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
fault:
    orr     r7, r7, #0x100
    b       fail
    .ltorg

ok_message:   .asciz "RP2350_FETCH_AHEAD_OK\n"
fail_message: .asciz "RP2350_FETCH_AHEAD_FAIL\n"
