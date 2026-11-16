/*
 * Test: the FP context goes across m5.switchCpus between the RP2350 board's
 * atomic and Minor M33s, wherever the switch lands
 * (configs/run_rp2350_switch.py switches every few microseconds).
 *
 * The thread runs ROUNDS rounds of a chain held in FP registers only, with
 * FPSCR's rounding mode towards zero: s[i] = frac(3 s[i] + s[i+1 mod 30]),
 * a map that stretches every difference, so the result depends on every bit
 * of s0-s29, on the rounding mode and on the order of the steps (s30 is the
 * steps' scratch, s31 the 3). SysTick fires every SYST_RELOAD cycles
 * and overwrites s0-s15 and FPSCR (a lazy save on its first FP
 * instruction), then pends PendSV, which saves s16-s31 with VPUSH,
 * overwrites them and FPSCR, and restores them with VPOP. Neither changes
 * the thread's results, so the result is the same however many interrupts
 * come, at any timing.
 *
 * At the end the thread folds s0-s31 and FPSCR into one word, at RESULT, and
 * exits by semihosting with that word's difference from EXPECTED (the
 * chain's value without a switch, at IL and at CL alike) as the exit code:
 * 0 passes. A fault exits as ADP_Stopped_InternalError. The SysTick and
 * PendSV counts are at TICKS and PENDS.
 */
    .syntax unified
    .cpu cortex-m33
    .fpu fpv5-sp-d16
    .thumb

    .equ ICSR,     0xE000ED04
    .equ SHPR3,    0xE000ED20
    .equ CPACR,    0xE000ED88
    .equ SYST_CSR, 0xE000E010
    .equ SYST_RVR, 0xE000E014
    .equ SYST_CVR, 0xE000E018

    .equ RESULT,   0x20000100
    .equ TICKS,    0x20000104
    .equ PENDS,    0x20000108

    .equ ROUNDS,      400
    .equ SYST_RELOAD, 613
    .equ THREAD_FPSCR, 0x00C00000   /* RMode 0b11: towards zero */
    .equ CLOB_FPSCR,   0x00400000   /* RMode 0b01: towards +inf */
    .equ EXPECTED,     0xECC80717

    .section .text
    .align 2
    .word   0x20040000
    .word   Reset_Handler
    .word   fault_handler       /* NMI */
    .word   fault_handler       /* HardFault */
    .word   fault_handler       /* MemManage */
    .word   fault_handler       /* BusFault */
    .word   fault_handler       /* UsageFault */
    .word   fault_handler
    .word   fault_handler
    .word   fault_handler
    .word   fault_handler
    .word   fault_handler       /* SVC */
    .word   fault_handler       /* DebugMon */
    .word   fault_handler
    .word   PendSV_Handler
    .word   SysTick_Handler

    /* s\i = frac(3 s\i + s\j) (s30 a scratch, s31 = 3) */
    .macro step i, j
    vmul.f32  s\i, s\i, s31
    vadd.f32  s\i, s\i, s\j
    vrintm.f32 s30, s\i
    vsub.f32  s\i, s\i, s30
    .endm

    .thumb_func
    .global Reset_Handler
Reset_Handler:
    /* CP10 and CP11 full access; FPCCR keeps its reset value (ASPEN and
     * LSPEN set: lazy stacking) */
    ldr     r0, =CPACR
    ldr     r1, [r0]
    orr     r1, r1, #(0xF << 20)
    str     r1, [r0]
    dsb
    isb
    /* PendSV lowest, SysTick above it */
    ldr     r0, =SHPR3
    ldr     r1, =0x40FF0000
    str     r1, [r0]
    ldr     r0, =TICKS
    movs    r1, #0
    str     r1, [r0]
    str     r1, [r0, #4]        /* PENDS */

    /* s0-s31 = 1 + i / 64 (then their fractions), FPSCR towards zero */
    .irp i, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    ldr     r0, =(0x3F800000 + \i * 0x20000)
    vmov    s\i, r0
    .endr
    ldr     r0, =THREAD_FPSCR
    vmsr    fpscr, r0

    /* SysTick every SYST_RELOAD + 1 cycles, from now */
    ldr     r0, =SYST_RVR
    ldr     r1, =SYST_RELOAD
    str     r1, [r0]
    ldr     r0, =SYST_CVR
    movs    r1, #0
    str     r1, [r0]
    ldr     r0, =SYST_CSR
    movs    r1, #7              /* ENABLE, TICKINT, CLKSOURCE */
    str     r1, [r0]

    ldr     r7, =ROUNDS
round:
    vmov.f32 s31, #3.0
    step 0, 1
    step 1, 2
    step 2, 3
    step 3, 4
    step 4, 5
    step 5, 6
    step 6, 7
    step 7, 8
    step 8, 9
    step 9, 10
    step 10, 11
    step 11, 12
    step 12, 13
    step 13, 14
    step 14, 15
    step 15, 16
    step 16, 17
    step 17, 18
    step 18, 19
    step 19, 20
    step 20, 21
    step 21, 22
    step 22, 23
    step 23, 24
    step 24, 25
    step 25, 26
    step 26, 27
    step 27, 28
    step 28, 29
    step 29, 0
    subs    r7, r7, #1
    bne     round

    /* SysTick off, then fold s0-s31 and FPSCR into r4: r4 = ror(r4, 5) ^ x */
    ldr     r0, =SYST_CSR
    movs    r1, #0
    str     r1, [r0]
    dsb
    isb
    movs    r4, #0
    .irp i, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    vmov    r0, s\i
    ror     r4, r4, #5
    eor     r4, r4, r0
    .endr
    vmrs    r0, fpscr
    bic     r0, r0, #0x9F       /* the cumulative exception flags: not compared */
    ror     r4, r4, #5
    eor     r4, r4, r0
    ldr     r0, =RESULT
    str     r4, [r0]
    /* one semihosting call, SYS_EXIT_EXTENDED: ApplicationExit with the
     * result's difference from EXPECTED as its code (gem5's semihosting
     * keeps the port proxy of the core that first calls it, so a call
     * after a switch would go through the core switched out) */
    ldr     r0, =EXPECTED
    eor     r4, r4, r0
    ldr     r1, =EXIT_BLOCK
    ldr     r0, =0x20026        /* ADP_Stopped_ApplicationExit */
    str     r0, [r1]
    str     r4, [r1, #4]
    movs    r0, #0x20           /* SYS_EXIT_EXTENDED */
    bkpt    #0xab
    b       .
    .ltorg

    /* SysTick: overwrite s0-s15 and FPSCR (the lazy save comes at the first
     * FP instruction), count, pend PendSV */
    .thumb_func
SysTick_Handler:
    ldr     r0, =CLOB_FPSCR
    vmsr    fpscr, r0
    .irp i, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
    vmov.f32 s\i, #-3.0
    .endr
    vadd.f32 s0, s1, s2
    ldr     r0, =TICKS
    ldr     r1, [r0]
    adds    r1, r1, #1
    str     r1, [r0]
    ldr     r0, =ICSR
    mov     r1, #(1 << 28)
    str     r1, [r0]
    bx      lr
    .ltorg

    /* PendSV: s16-s31 saved, overwritten and restored, as AAPCS asks; s0-s15
     * and FPSCR overwritten */
    .thumb_func
PendSV_Handler:
    vpush   {s16-s31}
    ldr     r0, =CLOB_FPSCR
    vmsr    fpscr, r0
    .irp i, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    vmov.f32 s\i, #5.0
    .endr
    vadd.f32 s16, s17, s0
    ldr     r0, =PENDS
    ldr     r1, [r0]
    adds    r1, r1, #1
    str     r1, [r0]
    vpop    {s16-s31}
    bx      lr
    .ltorg

    .thumb_func
fault_handler:
    ldr     r1, =EXIT_BLOCK
    ldr     r0, =0x20024        /* ADP_Stopped_InternalError */
    str     r0, [r1]
    movs    r0, #0
    str     r0, [r1, #4]
    movs    r0, #0x20           /* SYS_EXIT_EXTENDED */
    bkpt    #0xab
    b       .
    .ltorg

    .equ EXIT_BLOCK, 0x20000300
