/*
 * Test: floating-point context on M-profile exception entry and return
 * (DDI0403E PushStack, PopStack and PreserveFPState).
 *
 * The thread fills s0-s31 and FPSCR with known values, pends PendSV, and
 * checks after the return that they all survived a handler that overwrites
 * them (s0-s15 and FPSCR freely, s16-s31 under VPUSH/VPOP as AAPCS asks).
 * The handlers record what they saw at entry (EXC_RETURN, SP, CONTROL,
 * FPCCR, FPCAR, the stacked xPSR) and the thread checks those too.
 *
 *   1  lazy (ASPEN=1, LSPEN=1), handler uses FP
 *   2  eager (LSPEN=0), handler uses FP
 *   3  lazy, handler does not use FP: nothing stacked, LSPACT cleared on return
 *   4  no FP context (CONTROL.FPCA cleared): basic frame, EXC_RETURN 0xFFFFFFF9
 *   5  lazy, thread on the process stack: EXC_RETURN 0xFFFFFFED
 *   6  lazy, nested: PendSV uses FP, then SVC (higher priority) overwrites it
 *   7  as 6, eager
 *   8  lazy, SP 4 off 8-byte alignment with CCR.STKALIGN clear: aligned anyway
 *   9  ASPEN=0: FP use does not set FPCA, basic frame
 *  10  lazy, the handler's first instruction is FP and it returns at once:
 *      the return reads the frame the lazy save has only just written
 *
 * Result: 0xCAFECAFE at 0x20000100, or 0xDEADDEAD there with the subtest at
 * 0x20000104 and the check at 0x20000108; prints FP_STACKING_OK or
 * FP_STACKING_FAIL by semihosting, then exits.
 *
 * Runs on the STM32F405 platform's atomic, timing and Minor CPUs, and, built
 * as test_fp_stacking_rp2350.elf, on the RP2350 board's Arm proxy.
 */
    .syntax unified
    .cpu cortex-m4
    .fpu fpv4-sp-d16
    .thumb

    .equ ICSR,    0xE000ED04
    .equ CCR,     0xE000ED14
    .equ SHPR2,   0xE000ED1C
    .equ SHPR3,   0xE000ED20
    .equ CPACR,   0xE000ED88
    .equ FPCCR,   0xE000EF34
    .equ FPCAR,   0xE000EF38
    .equ FPDSCR,  0xE000EF3C

    .equ RESULT,  0x20000100
    .equ SUBTEST, 0x20000104
    .equ CHECK,   0x20000108
    .equ REC,     0x20000200
    .equ MODE,    0x20000280
    .equ PSP_TOP, 0x20008000

    .equ SENT,        0x5E5E5E5E
    .equ THREAD_FPSCR, 0x6340009F
    .equ CLOB_FPSCR,   0x9080001B
    .equ SVC_FPSCR,    0x50000004
    .equ DEF_FPSCR,    0x02C00000
    .equ CLOB_TAG,     0x70000000
    .equ SVC_TAG,      0x78000000

    /* the record the handlers write */
    .equ R_LR,       0
    .equ R_SP,       4
    .equ R_CTRL,     8
    .equ R_FPCCR,    12
    .equ R_FPCAR,    16
    .equ R_FPSCR1,   20
    .equ R_FPCCR1,   24
    .equ R_CTRL1,    28
    .equ R_XPSR,     32
    .equ R_SVC_LR,   36
    .equ R_SVC_SP,   40
    .equ R_NEST_OK,  44
    .equ R_PSP,      48

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
    .word   fault_handler       /* NMI */
    .word   fault_handler       /* HardFault */
    .word   fault_handler       /* MemManage */
    .word   fault_handler       /* BusFault */
    .word   fault_handler       /* UsageFault */
    .word   fault_handler
    .word   fault_handler
    .word   fault_handler
    .word   fault_handler
    .word   SVC_Handler
    .word   fault_handler       /* DebugMon */
    .word   fault_handler
    .word   PendSV_Handler
    .word   fault_handler       /* SysTick */

    .section .text

    .macro pool
    b       1f
    .ltorg
1:
    .endm

    /* s0-s15 (or s16-s31) = \tag + i * 0x10001 */
    .macro fill_low tag
    .irp i, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
    ldr     r0, =(\tag + \i * 0x10001)
    vmov    s\i, r0
    .endr
    .endm
    .macro fill_high tag
    .irp i, 16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    ldr     r0, =(\tag + \i * 0x10001)
    vmov    s\i, r0
    .endr
    .endm

    /* branch to \bad unless s0-s15 (or s16-s31) hold \tag + i * 0x10001 */
    .macro check_low tag, bad
    .irp i, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
    vmov    r0, s\i
    ldr     r1, =(\tag + \i * 0x10001)
    cmp     r0, r1
    bne     \bad
    .endr
    .endm
    .macro check_high tag, bad
    .irp i, 16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    vmov    r0, s\i
    ldr     r1, =(\tag + \i * 0x10001)
    cmp     r0, r1
    bne     \bad
    .endr
    .endm

    /* fail as check \id unless \reg == \value */
    .macro expect reg, value, id
    mov     r6, #\id
    ldr     r1, =\value
    cmp     \reg, r1
    bne     fail
    .endm

    /* fail as check \id unless \reg == \other */
    .macro expect_reg reg, other, id
    mov     r6, #\id
    cmp     \reg, \other
    bne     fail
    .endm

    /* r0 = record word \off */
    .macro rec off
    ldr     r0, =REC
    ldr     r0, [r0, #\off]
    .endm

    /* the 26 words below \reg (r0-r2 used) = SENT */
    .macro sentinel reg
    sub     r0, \reg, #0x68
    ldr     r1, =SENT
    movs    r2, #26
1:  str     r1, [r0], #4
    subs    r2, #1
    bne     1b
    .endm

    /* FPCCR = \val */
    .macro set_fpccr val
    ldr     r0, =FPCCR
    ldr     r1, =\val
    str     r1, [r0]
    dsb
    isb
    .endm

    .macro set_mode val
    ldr     r0, =MODE
    movs    r1, #\val
    str     r1, [r0]
    .endm

    .macro clear_fpca
    mrs     r0, control
    bic     r0, r0, #4
    msr     control, r0
    isb
    .endm

    /* pend PendSV; it is taken at the ISB */
    .macro pendsv
    ldr     r0, =ICSR
    mov     r1, #(1 << 28)
    str     r1, [r0]
    dsb
    isb
    .endm

    /* thread values in, PendSV, all 32 registers and FPSCR checked after */
    .macro fp_round tag
    fill_low \tag
    fill_high \tag
    ldr     r0, =THREAD_FPSCR
    vmsr    fpscr, r0
    pendsv
    mov     r6, #1
    cmp     sp, r8
    bne     fail
    mov     r6, #2
    check_low \tag, fail
    check_high \tag, fail
    vmrs    r0, fpscr
    expect  r0, THREAD_FPSCR, 3
    .endm

    /* the stacked s0-s15 and FPSCR at \base + 0x20 hold the thread's */
    .macro check_frame base, tag
    .irp i, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
    ldr     r0, [\base, #(0x20 + 4 * \i)]
    expect  r0, (\tag + \i * 0x10001), 40
    .endr
    ldr     r0, [\base, #0x60]
    expect  r0, THREAD_FPSCR, 41
    .endm

    .thumb_func
    .globl Reset_Handler
Reset_Handler:
    /* FPU on: CP10, CP11 full access */
    ldr     r0, =CPACR
    ldr     r1, [r0]
    orr     r1, r1, #(0xF << 20)
    str     r1, [r0]
    dsb
    isb
    /* PendSV lowest priority, SVC highest, so SVC preempts PendSV */
    ldr     r0, =SHPR3
    ldr     r1, =0x00FF0000
    str     r1, [r0]
    ldr     r0, =SHPR2
    movs    r1, #0
    str     r1, [r0]
    ldr     r0, =FPDSCR
    ldr     r1, =DEF_FPSCR
    str     r1, [r0]
    pool

    /* ---- 1: lazy, handler uses FP ---- */
    movs    r7, #1
    set_fpccr 0xC0000000
    set_mode 1
    mov     r8, sp
    sentinel r8
    fp_round 0x41000000
    rec     R_LR
    expect  r0, 0xFFFFFFE9, 10
    rec     R_SP
    sub     r9, r8, #0x68
    expect_reg r0, r9, 11
    rec     R_CTRL
    and     r0, r0, #6
    expect  r0, 0, 12
    rec     R_FPCCR
    and     r0, r0, #0x9
    expect  r0, 0x9, 13             /* LSPACT, THREAD */
    rec     R_FPCAR
    add     r1, r9, #0x20
    expect_reg r0, r1, 14
    rec     R_FPSCR1
    and     r0, r0, #0x07C00000
    expect  r0, DEF_FPSCR, 15
    rec     R_FPCCR1
    and     r0, r0, #1
    expect  r0, 0, 16
    rec     R_CTRL1
    and     r0, r0, #4
    expect  r0, 4, 17
    mrs     r0, control
    and     r0, r0, #4
    expect  r0, 4, 18
    ldr     r0, =FPCCR
    ldr     r0, [r0]
    and     r0, r0, #1
    expect  r0, 0, 19
    check_frame r9, 0x41000000
    pool

    /* ---- 2: eager, handler uses FP ---- */
    movs    r7, #2
    set_fpccr 0x80000000
    set_mode 1
    mov     r8, sp
    sentinel r8
    fp_round 0x42000000
    rec     R_LR
    expect  r0, 0xFFFFFFE9, 10
    rec     R_SP
    sub     r9, r8, #0x68
    expect_reg r0, r9, 11
    rec     R_FPCCR
    and     r0, r0, #1
    expect  r0, 0, 13
    rec     R_FPSCR1
    and     r0, r0, #0x07C00000
    expect  r0, DEF_FPSCR, 15
    mrs     r0, control
    and     r0, r0, #4
    expect  r0, 4, 18
    check_frame r9, 0x42000000
    pool

    /* ---- 3: lazy, handler does not use FP ---- */
    movs    r7, #3
    set_fpccr 0xC0000000
    set_mode 0
    mov     r8, sp
    sentinel r8
    fp_round 0x43000000
    rec     R_LR
    expect  r0, 0xFFFFFFE9, 10
    rec     R_FPCCR
    and     r0, r0, #1
    expect  r0, 1, 13
    ldr     r0, =FPCCR
    ldr     r0, [r0]
    and     r0, r0, #1
    expect  r0, 0, 19
    sub     r9, r8, #0x68
    ldr     r0, [r9, #0x20]
    expect  r0, SENT, 20            /* lazy: nothing stacked */
    ldr     r0, [r9, #0x60]
    expect  r0, SENT, 21
    pool

    /* ---- 4: no FP context ---- */
    movs    r7, #4
    set_mode 1
    fill_high 0x44000000
    clear_fpca
    mov     r8, sp
    pendsv
    mrs     r0, control
    and     r0, r0, #4
    expect  r0, 0, 18               /* before any FP instruction makes one */
    mov     r6, #1
    cmp     sp, r8
    bne     fail
    mov     r6, #2
    check_high 0x44000000, fail
    rec     R_LR
    expect  r0, 0xFFFFFFF9, 10
    rec     R_SP
    sub     r9, r8, #0x20
    expect_reg r0, r9, 11
    rec     R_FPCCR
    and     r0, r0, #1
    expect  r0, 0, 13
    pool

    /* ---- 5: lazy, process stack ---- */
    movs    r7, #5
    set_mode 1
    ldr     r0, =PSP_TOP
    msr     psp, r0
    mrs     r0, control
    orr     r0, r0, #2
    msr     control, r0
    isb
    mov     r8, sp
    sentinel r8
    fp_round 0x45000000
    rec     R_LR
    expect  r0, 0xFFFFFFED, 10
    rec     R_PSP
    sub     r9, r8, #0x68
    expect_reg r0, r9, 11
    rec     R_FPCAR
    add     r1, r9, #0x20
    expect_reg r0, r1, 14
    rec     R_FPCCR
    and     r0, r0, #0x9
    expect  r0, 0x9, 13
    check_frame r9, 0x45000000
    mrs     r0, control
    and     r0, r0, #6
    expect  r0, 6, 18
    mrs     r0, control
    bic     r0, r0, #2
    msr     control, r0
    isb
    pool

    /* ---- 6: lazy, nested ---- */
    movs    r7, #6
    set_fpccr 0xC0000000
    set_mode 3
    mov     r8, sp
    fp_round 0x46000000
    rec     R_LR
    expect  r0, 0xFFFFFFE9, 10
    rec     R_SVC_LR
    expect  r0, 0xFFFFFFE1, 30
    rec     R_SP
    sub     r9, r0, #(64 + 0x68)
    rec     R_SVC_SP
    expect_reg r0, r9, 31
    rec     R_NEST_OK
    expect  r0, 1, 32
    pool

    /* ---- 7: eager, nested ---- */
    movs    r7, #7
    set_fpccr 0x80000000
    set_mode 3
    mov     r8, sp
    fp_round 0x47000000
    rec     R_SVC_LR
    expect  r0, 0xFFFFFFE1, 30
    rec     R_NEST_OK
    expect  r0, 1, 32
    pool

    /* ---- 8: lazy, SP 4 off alignment, STKALIGN clear ---- */
    movs    r7, #8
    set_fpccr 0xC0000000
    set_mode 1
    ldr     r0, =CCR
    ldr     r1, [r0]
    bic     r1, r1, #(1 << 9)
    str     r1, [r0]
    sub     sp, sp, #4
    mov     r8, sp
    fp_round 0x48000000
    rec     R_SP
    sub     r9, r8, #(4 + 0x68)
    expect_reg r0, r9, 11
    rec     R_XPSR
    and     r0, r0, #(1 << 9)
    expect  r0, (1 << 9), 22
    add     sp, sp, #4
    ldr     r0, =CCR
    ldr     r1, [r0]
    orr     r1, r1, #(1 << 9)
    str     r1, [r0]
    pool

    /* ---- 9: ASPEN=0 ---- */
    movs    r7, #9
    set_fpccr 0x00000000
    set_mode 1
    clear_fpca
    fill_high 0x49000000
    mrs     r0, control
    and     r0, r0, #4
    expect  r0, 0, 23
    mov     r8, sp
    pendsv
    rec     R_LR
    expect  r0, 0xFFFFFFF9, 10
    rec     R_SP
    sub     r9, r8, #0x20
    expect_reg r0, r9, 11
    mov     r6, #2
    check_high 0x49000000, fail
    set_fpccr 0xC0000000
    pool

    /* ---- 10: lazy, FP then return at once ---- */
    movs    r7, #10
    set_fpccr 0xC0000000
    set_mode 4
    mov     r8, sp
    fp_round 0x4A000000
    pool

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
    str     r6, [r1]
    mov     r0, r7
    bl      print_hex
    mov     r0, r6
    bl      print_hex
    ldr     r1, =fail_message

done:
    movs    r0, #4              /* SYS_WRITE0 */
    bkpt    #0xab
    mov     r0, #0x18           /* SYS_EXIT */
    ldr     r1, =0x20026
    bkpt    #0xab
    b       .
    pool

    /* PendSV: record the entry state; with MODE bit 0 use FP, with bit 1
     * also take an SVC that overwrites s0-s15 and FPSCR, and check them */
    .thumb_func
PendSV_Handler:
    ldr     r3, =MODE
    ldr     r3, [r3]
    tst     r3, #4
    beq     1f
    vmov    s0, r3
    bx      lr
1:
    ldr     r2, =REC
    str     lr, [r2, #R_LR]
    mov     r0, sp
    str     r0, [r2, #R_SP]
    mrs     r0, control
    str     r0, [r2, #R_CTRL]
    ldr     r1, =FPCCR
    ldr     r0, [r1]
    str     r0, [r2, #R_FPCCR]
    ldr     r0, [r1, #4]
    str     r0, [r2, #R_FPCAR]
    tst     lr, #4
    ite     eq
    mrseq   r3, msp
    mrsne   r3, psp
    ldr     r0, [r3, #0x1C]
    str     r0, [r2, #R_XPSR]
    mrs     r0, psp
    str     r0, [r2, #R_PSP]
    movs    r0, #0
    str     r0, [r2, #R_NEST_OK]
    ldr     r3, =MODE
    ldr     r3, [r3]
    tst     r3, #1
    beq     pendsv_done
    /* the handler's first FP instruction */
    vmrs    r0, fpscr
    str     r0, [r2, #R_FPSCR1]
    ldr     r1, =FPCCR
    ldr     r0, [r1]
    str     r0, [r2, #R_FPCCR1]
    mrs     r0, control
    str     r0, [r2, #R_CTRL1]
    vpush   {s16-s31}
    fill_low CLOB_TAG
    fill_high CLOB_TAG
    ldr     r0, =CLOB_FPSCR
    vmsr    fpscr, r0
    tst     r3, #2
    beq     pendsv_pop
    svc     #0
    ldr     r2, =REC
    check_low CLOB_TAG, pendsv_pop
    vmrs    r0, fpscr
    ldr     r1, =CLOB_FPSCR
    cmp     r0, r1
    bne     pendsv_pop
    movs    r0, #1
    str     r0, [r2, #R_NEST_OK]
pendsv_pop:
    vpop    {s16-s31}
pendsv_done:
    bx      lr
    pool

    /* SVC: record the entry state and overwrite s0-s15 and FPSCR */
    .thumb_func
SVC_Handler:
    ldr     r2, =REC
    str     lr, [r2, #R_SVC_LR]
    mov     r0, sp
    str     r0, [r2, #R_SVC_SP]
    fill_low SVC_TAG
    ldr     r0, =SVC_FPSCR
    vmsr    fpscr, r0
    bx      lr
    pool

    .thumb_func
fault_handler:
    mov     r6, #99
    b       fail

ok_message:   .asciz "FP_STACKING_OK\n"
fail_message: .asciz "FP_STACKING_FAIL\n"

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
