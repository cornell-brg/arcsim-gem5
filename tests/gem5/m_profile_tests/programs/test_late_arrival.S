/*
 * Test: a higher-priority exception that arrives while an exception frame
 * or a lazy FP save is still being written must not cut the writes short.
 *
 * Each round pends PendSV (lowest priority) from a thread holding known
 * values in r0-r3, r12, s0-s15 and FPSCR, with SysTick (highest priority)
 * set to fire k cycles after it is started just before; k runs from 1 to
 * KMAX, so that some round has SysTick arrive inside each write window.
 * The thread checks its registers after each round, and that PendSV ran
 * once.
 *
 *   pass 1: eager (FPCCR.LSPEN=0): PendSV entry stacks 25 words
 *   pass 2: lazy (LSPEN=1): PendSV's first FP instruction saves 17 words
 *
 * Result: 0xCAFECAFE at 0x20000100, or 0xDEADDEAD there with the pass at
 * 0x20000104 and k at 0x20000108; prints LATE_ARRIVAL_OK or
 * LATE_ARRIVAL_FAIL by semihosting, then exits.  SysTick rounds that
 * preempted PendSV are counted at 0x2000010C.
 *
 * Runs on the STM32F405 platform's atomic, timing and Minor CPUs, and, built
 * as test_late_arrival_rp2350.elf, on the RP2350 board's Arm proxy.
 */
    .syntax unified
    .cpu cortex-m4
    .fpu fpv4-sp-d16
    .thumb

    .equ ICSR,    0xE000ED04
    .equ SHPR3,   0xE000ED20
    .equ CPACR,   0xE000ED88
    .equ FPCCR,   0xE000EF34
    .equ SYST_CSR, 0xE000E010
    .equ SYST_RVR, 0xE000E014
    .equ SYST_CVR, 0xE000E018

    .equ RESULT,  0x20000100
    .equ SUBTEST, 0x20000104
    .equ CHECK,   0x20000108
    .equ COUNT,   0x2000010C
    .equ TICKED,  0x20000200
    .equ PENDED,  0x20000204
    .equ KMAX,    160

    /* RP2350: everything in SRAM (rp2350_sram.ld), the vectors first */
    .ifdef RP2350
    .section .text
    .align 2
    .word   0x20040000
    .else
    .section .isr_vector, "a", %progbits
    .align 2
    .word   _estack
    .endif
    .word   Reset_Handler
    .rept   9
    .word   fault_handler
    .endr
    .word   fault_handler       /* SVC */
    .word   fault_handler
    .word   fault_handler
    .word   PendSV_Handler
    .word   SysTick_Handler

    .section .text

    .macro pool
    b       1f
    .ltorg
1:
    .endm

    /* s0-s15 = (\tag + i * 0x10001) ^ \key */
    .macro fill_low tag, key
    .irp i, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
    ldr     r0, =(\tag + \i * 0x10001)
    eor     r0, r0, \key
    vmov    s\i, r0
    .endr
    .endm

    .macro check_low tag, key
    .irp i, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
    vmov    r0, s\i
    ldr     r1, =(\tag + \i * 0x10001)
    eor     r1, r1, \key
    cmp     r0, r1
    bne     fail
    .endr
    .endm

    /* \reg = \value ^ r11 */
    .macro keyed reg, value
    ldr     \reg, =\value
    eor     \reg, \reg, r11
    .endm

    /* one pass of KMAX rounds; r7 = pass, r8 = k */
    .macro rounds
    mov     r8, #1
2:
    ldr     r0, =TICKED
    movs    r1, #0
    str     r1, [r0]
    str     r1, [r0, #4]        /* PENDED */
    ldr     r0, =SYST_RVR
    str     r8, [r0]
    ldr     r0, =SYST_CVR
    str     r1, [r0]
    /* r11: this round's key, k in every byte */
    orr     r11, r8, r8, lsl #8
    orr     r11, r11, r11, lsl #16
    fill_low 0x41000000, r11
    ldr     r0, =0x6340009F
    eor     r0, r0, r8, lsl #28
    vmsr    fpscr, r0
    ldr     r4, =SYST_CSR
    movs    r5, #7              /* ENABLE, TICKINT, CLKSOURCE */
    ldr     r6, =ICSR
    mov     r9, #(1 << 28)
    keyed   r0, 0x10101010
    keyed   r1, 0x11111111
    keyed   r2, 0x12121212
    keyed   r3, 0x13131313
    keyed   r12, 0x1C1C1C1C
    str     r5, [r4]
    str     r9, [r6]
    dsb
    isb
    keyed   r10, 0x10101010
    cmp     r0, r10
    bne     fail
    keyed   r10, 0x11111111
    cmp     r1, r10
    bne     fail
    keyed   r10, 0x12121212
    cmp     r2, r10
    bne     fail
    keyed   r10, 0x13131313
    cmp     r3, r10
    bne     fail
    keyed   r10, 0x1C1C1C1C
    cmp     r12, r10
    bne     fail
    check_low 0x41000000, r11
    vmrs    r0, fpscr
    ldr     r1, =0x6340009F
    eor     r1, r1, r8, lsl #28
    cmp     r0, r1
    bne     fail
    /* PendSV ran once */
    ldr     r0, =PENDED
    ldr     r0, [r0]
    cmp     r0, #1
    bne     fail
    /* wait for the SysTick round to be over */
    ldr     r0, =TICKED
3:  ldr     r1, [r0]
    cmp     r1, #0
    beq     3b
    add     r8, r8, #1
    cmp     r8, #KMAX
    bls     2b
    b       4f
    .ltorg
4:
    .endm

    .thumb_func
    .globl Reset_Handler
Reset_Handler:
    ldr     r0, =CPACR
    ldr     r1, [r0]
    orr     r1, r1, #(0xF << 20)
    str     r1, [r0]
    dsb
    isb
    /* PendSV lowest, SysTick highest */
    ldr     r0, =SHPR3
    ldr     r1, =0x00FF0000
    str     r1, [r0]
    ldr     r0, =COUNT
    movs    r1, #0
    str     r1, [r0]
    pool

    movs    r7, #1
    ldr     r0, =FPCCR
    ldr     r1, =0x80000000
    str     r1, [r0]
    rounds

    movs    r7, #2
    ldr     r0, =FPCCR
    ldr     r1, =0xC0000000
    str     r1, [r0]
    rounds

    ldr     r0, =0xCAFECAFE
    ldr     r1, =RESULT
    str     r0, [r1]
    ldr     r1, =ok_message
    b       done

fail:
    ldr     r0, =0xDEADDEAD
    ldr     r1, =RESULT
    str     r0, [r1]
    ldr     r1, =SUBTEST
    str     r7, [r1]
    ldr     r1, =CHECK
    str     r8, [r1]
    mov     r0, r7
    bl      print_hex
    mov     r0, r8
    bl      print_hex
    ldr     r1, =fail_message

done:
    movs    r0, #4
    bkpt    #0xab
    mov     r0, #0x18
    ldr     r1, =0x20026
    bkpt    #0xab
    b       .
    pool

    /* PendSV: overwrite s0-s15 and FPSCR (the first FP instruction makes
     * a pending lazy save) */
    .thumb_func
PendSV_Handler:
    ldr     r1, =PENDED
    ldr     r2, [r1]
    adds    r2, #1
    str     r2, [r1]
    vmov    s0, r0
    movs    r3, #0
    fill_low 0x70000000, r3
    ldr     r0, =0x9080001B
    vmsr    fpscr, r0
    bx      lr
    pool

    /* SysTick: stop it, count the round as over; no FP */
    .thumb_func
SysTick_Handler:
    ldr     r0, =SYST_CSR
    movs    r1, #0
    str     r1, [r0]
    ldr     r0, =TICKED
    movs    r1, #1
    str     r1, [r0]
    /* fired while PendSV was active or pending: count it */
    ldr     r0, =ICSR
    ldr     r0, [r0]
    tst     r0, #(1 << 11)      /* RETTOBASE clear: preempted a handler */
    bne     1f
    ldr     r0, =COUNT
    ldr     r1, [r0]
    adds    r1, #1
    str     r1, [r0]
1:
    bx      lr
    pool

    .thumb_func
fault_handler:
    mov     r8, #999
    b       fail

ok_message:   .asciz "LATE_ARRIVAL_OK\n"
fail_message: .asciz "LATE_ARRIVAL_FAIL\n"

    /* print r0 as 8 hex digits and a space by semihosting (r0-r3 used) */
    .thumb_func
print_hex:
    ldr     r1, =hexbuf
    movs    r2, #28
1:  lsr     r3, r0, r2
    and     r3, r3, #0xF
    cmp     r3, #10
    ite     lo
    addlo   r3, r3, #'0'
    addhs   r3, r3, #('a' - 10)
    strb    r3, [r1], #1
    subs    r2, r2, #4
    bpl     1b
    movs    r3, #' '
    strb    r3, [r1], #1
    movs    r3, #0
    strb    r3, [r1]
    movs    r0, #4
    ldr     r1, =hexbuf
    bkpt    #0xab
    bx      lr
    .ltorg

    .equ hexbuf, 0x20000300
