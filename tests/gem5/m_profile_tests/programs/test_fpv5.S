/*
 * Test: FPv5 single-precision instructions (Armv8-M FP extension, Cortex-M33)
 *
 * VSEL, VMAXNM/VMINNM, VRINT{A,N,P,M,Z,R,X} and VCVT{A,N,P,M}, plus the FPv4 VCVT and
 * VCVTR to integer, on ordinary values, ties, signed zeros, infinities, quiet and
 * signalling NaNs, and denormals with and without FPSCR.FZ. Each case sets FPSCR (and the
 * APSR flags for VSEL), runs one instruction, and compares the result bits and the whole
 * FPSCR with the values the RP2350's Cortex-M33 produced for the same case.
 *
 * Runs on the M33 release (run_m4_test.py --release m33). Assembled with DUMP defined,
 * every case's result and FPSCR are stored from OUT_BASE on instead of compared; with
 * PICO also defined, the build is the one loaded into the RP2350's SRAM to measure them.
 *
 * Result at 0x20000100: 0xCAFECAFE = all cases passed; on failure 0xDEADDEAD, and
 * 0x20000104 holds the number (from 1) of the first failing case.
 */

    .syntax unified
    .cpu cortex-m33
    .fpu fpv5-sp-d16
    .thumb

    .equ TEST_RESULT,  0x20000100
    .equ TEST_SUBTEST, 0x20000104
    .equ OUT_BASE,     0x20004000
    .equ PASS_VAL,     0xCAFECAFE
    .equ FAIL_VAL,     0xDEADDEAD

/* r8: case number; r9: first failing case (0 = none); r10: next OUT_BASE word */

    .macro record er, ef
    vmov    r1, s2
    vmrs    r2, fpscr
    str     r1, [r10], #4
    str     r2, [r10], #4
    add     r8, r8, #1
.ifndef DUMP
    ldr     r3, =\er
    cmp     r1, r3
    bne     1f
    ldr     r3, =\ef
    cmp     r2, r3
    beq     2f
1:  cmp     r9, #0
    it      eq
    moveq   r9, r8
2:
.endif
    b       3f
    .ltorg
3:
    .endm

    /* VSEL: APSR flags from nzcv */
    .macro case_sel insn, fpscr, a, b, nzcv, er=0, ef=0
    ldr     r0, =\fpscr
    vmsr    fpscr, r0
    ldr     r0, =\a
    vmov    s0, r0
    ldr     r0, =\b
    vmov    s1, r0
    ldr     r0, =\nzcv
    msr     APSR_nzcvq, r0
    \insn  s2, s0, s1
    record  \er, \ef
    .endm

    .macro case_bin insn, fpscr, a, b, er=0, ef=0
    ldr     r0, =\fpscr
    vmsr    fpscr, r0
    ldr     r0, =\a
    vmov    s0, r0
    ldr     r0, =\b
    vmov    s1, r0
    \insn  s2, s0, s1
    record  \er, \ef
    .endm

    .macro case_un insn, fpscr, a, er=0, ef=0
    ldr     r0, =\fpscr
    vmsr    fpscr, r0
    ldr     r0, =\a
    vmov    s0, r0
    \insn  s2, s0
    record  \er, \ef
    .endm

.ifndef PICO
    .section .isr_vector, "a", %progbits
    .align 2
__isr_vector:
    .word   0x20020000          /* Initial SP */
    .word   Reset_Handler       /* Reset */
    .word   Fault_Handler       /* NMI */
    .word   Fault_Handler       /* HardFault */
    .word   Fault_Handler       /* MemManage */
    .word   Fault_Handler       /* BusFault */
    .word   Fault_Handler       /* UsageFault */
.endif

    .section .text
    .thumb_func
    .globl Reset_Handler
Reset_Handler:
.ifdef PICO
    ldr     r0, =0x20040000
    mov     sp, r0
.endif
    /* Full access to CP10/CP11 (CPACR) */
    ldr     r0, =0xE000ED88
    ldr     r1, [r0]
    orr     r1, r1, #(0xF << 20)
    str     r1, [r0]
    dsb
    isb

    mov     r8, #0
    mov     r9, #0
    ldr     r10, =OUT_BASE

    /* 1: vseleq Z=1 */
    case_sel vseleq.f32, 0x00000000, 0x3f800000, 0x40000000, 0x40000000, 0x3f800000, 0x00000000
    /* 2: vseleq Z=0 */
    case_sel vseleq.f32, 0x00000000, 0x3f800000, 0x40000000, 0x00000000, 0x40000000, 0x00000000
    /* 3: vselvs V=1 */
    case_sel vselvs.f32, 0x00000000, 0x3f800000, 0x40000000, 0x10000000, 0x3f800000, 0x00000000
    /* 4: vselvs V=0 */
    case_sel vselvs.f32, 0x00000000, 0x3f800000, 0x40000000, 0x00000000, 0x40000000, 0x00000000
    /* 5: vselge N=V=0 */
    case_sel vselge.f32, 0x00000000, 0x3f800000, 0x40000000, 0x00000000, 0x3f800000, 0x00000000
    /* 6: vselge N=1 V=0 */
    case_sel vselge.f32, 0x00000000, 0x3f800000, 0x40000000, 0x80000000, 0x40000000, 0x00000000
    /* 7: vselge N=V=1 */
    case_sel vselge.f32, 0x00000000, 0x3f800000, 0x40000000, 0x90000000, 0x3f800000, 0x00000000
    /* 8: vselgt Z=0 N=V */
    case_sel vselgt.f32, 0x00000000, 0x3f800000, 0x40000000, 0x00000000, 0x3f800000, 0x00000000
    /* 9: vselgt Z=1 */
    case_sel vselgt.f32, 0x00000000, 0x3f800000, 0x40000000, 0x40000000, 0x40000000, 0x00000000
    /* 10: vselgt N!=V */
    case_sel vselgt.f32, 0x00000000, 0x3f800000, 0x40000000, 0x80000000, 0x40000000, 0x00000000
    /* 11: vselgt copies a signalling NaN unchanged */
    case_sel vselgt.f32, 0x00000000, 0x7f800001, 0x40000000, 0x00000000, 0x7f800001, 0x00000000
    /* 12: vseleq copies a denormal under FZ */
    case_sel vseleq.f32, 0x01000000, 0x00000001, 0x40000000, 0x40000000, 0x00000001, 0x01000000
    /* 13: vmaxnm 1.0 2.0 */
    case_bin vmaxnm.f32, 0x00000000, 0x3f800000, 0x40000000, 0x40000000, 0x00000000
    /* 14: vmaxnm 2.0 1.0 */
    case_bin vmaxnm.f32, 0x00000000, 0x40000000, 0x3f800000, 0x40000000, 0x00000000
    /* 15: vmaxnm -0 +0 */
    case_bin vmaxnm.f32, 0x00000000, 0x80000000, 0x00000000, 0x00000000, 0x00000000
    /* 16: vmaxnm +0 -0 */
    case_bin vmaxnm.f32, 0x00000000, 0x00000000, 0x80000000, 0x00000000, 0x00000000
    /* 17: vmaxnm qnan 1.0 */
    case_bin vmaxnm.f32, 0x00000000, 0x7fc00001, 0x3f800000, 0x3f800000, 0x00000000
    /* 18: vmaxnm 1.0 qnan */
    case_bin vmaxnm.f32, 0x00000000, 0x3f800000, 0x7fc00001, 0x3f800000, 0x00000000
    /* 19: vmaxnm qnan snan */
    case_bin vmaxnm.f32, 0x00000000, 0x7fc00001, 0x7f800001, 0x7fc00001, 0x00000001
    /* 20: vmaxnm snan 1.0 */
    case_bin vmaxnm.f32, 0x00000000, 0x7f800001, 0x3f800000, 0x7fc00001, 0x00000001
    /* 21: vmaxnm snan 1.0 DN */
    case_bin vmaxnm.f32, 0x02000000, 0x7f800001, 0x3f800000, 0x7fc00000, 0x02000001
    /* 22: vmaxnm +inf 1.0 */
    case_bin vmaxnm.f32, 0x00000000, 0x7f800000, 0x3f800000, 0x7f800000, 0x00000000
    /* 23: vmaxnm -inf 1.0 */
    case_bin vmaxnm.f32, 0x00000000, 0xff800000, 0x3f800000, 0x3f800000, 0x00000000
    /* 24: vmaxnm denorm -0 FZ */
    case_bin vmaxnm.f32, 0x01000000, 0x00000001, 0x80000000, 0x00000000, 0x01000080
    /* 25: vminnm 1.0 2.0 */
    case_bin vminnm.f32, 0x00000000, 0x3f800000, 0x40000000, 0x3f800000, 0x00000000
    /* 26: vminnm 2.0 1.0 */
    case_bin vminnm.f32, 0x00000000, 0x40000000, 0x3f800000, 0x3f800000, 0x00000000
    /* 27: vminnm -0 +0 */
    case_bin vminnm.f32, 0x00000000, 0x80000000, 0x00000000, 0x80000000, 0x00000000
    /* 28: vminnm +0 -0 */
    case_bin vminnm.f32, 0x00000000, 0x00000000, 0x80000000, 0x80000000, 0x00000000
    /* 29: vminnm qnan 1.0 */
    case_bin vminnm.f32, 0x00000000, 0x7fc00001, 0x3f800000, 0x3f800000, 0x00000000
    /* 30: vminnm 1.0 qnan */
    case_bin vminnm.f32, 0x00000000, 0x3f800000, 0x7fc00001, 0x3f800000, 0x00000000
    /* 31: vminnm qnan snan */
    case_bin vminnm.f32, 0x00000000, 0x7fc00001, 0x7f800001, 0x7fc00001, 0x00000001
    /* 32: vminnm snan 1.0 */
    case_bin vminnm.f32, 0x00000000, 0x7f800001, 0x3f800000, 0x7fc00001, 0x00000001
    /* 33: vminnm snan 1.0 DN */
    case_bin vminnm.f32, 0x02000000, 0x7f800001, 0x3f800000, 0x7fc00000, 0x02000001
    /* 34: vminnm +inf 1.0 */
    case_bin vminnm.f32, 0x00000000, 0x7f800000, 0x3f800000, 0x3f800000, 0x00000000
    /* 35: vminnm -inf 1.0 */
    case_bin vminnm.f32, 0x00000000, 0xff800000, 0x3f800000, 0xff800000, 0x00000000
    /* 36: vminnm denorm -0 FZ */
    case_bin vminnm.f32, 0x01000000, 0x00000001, 0x80000000, 0x80000000, 0x01000080
    /* 37: vrinta 2.5 */
    case_un vrinta.f32, 0x00000000, 0x40200000, 0x40400000, 0x00000000
    /* 38: vrinta -2.5 */
    case_un vrinta.f32, 0x00000000, 0xc0200000, 0xc0400000, 0x00000000
    /* 39: vrinta -0.5 */
    case_un vrinta.f32, 0x00000000, 0xbf000000, 0xbf800000, 0x00000000
    /* 40: vrinta 1.5 */
    case_un vrinta.f32, 0x00000000, 0x3fc00000, 0x40000000, 0x00000000
    /* 41: vrinta 3.7 */
    case_un vrinta.f32, 0x00000000, 0x406ccccd, 0x40800000, 0x00000000
    /* 42: vrinta 1e10 */
    case_un vrinta.f32, 0x00000000, 0x501502f9, 0x501502f9, 0x00000000
    /* 43: vrinta +inf */
    case_un vrinta.f32, 0x00000000, 0x7f800000, 0x7f800000, 0x00000000
    /* 44: vrinta qnan */
    case_un vrinta.f32, 0x00000000, 0x7fc00001, 0x7fc00001, 0x00000000
    /* 45: vrinta snan */
    case_un vrinta.f32, 0x00000000, 0x7f800001, 0x7fc00001, 0x00000001
    /* 46: vrinta denorm */
    case_un vrinta.f32, 0x00000000, 0x00000001, 0x00000000, 0x00000000
    /* 47: vrinta denorm FZ */
    case_un vrinta.f32, 0x01000000, 0x00000001, 0x00000000, 0x01000080
    /* 48: vrintn 2.5 */
    case_un vrintn.f32, 0x00000000, 0x40200000, 0x40000000, 0x00000000
    /* 49: vrintn -2.5 */
    case_un vrintn.f32, 0x00000000, 0xc0200000, 0xc0000000, 0x00000000
    /* 50: vrintn -0.5 */
    case_un vrintn.f32, 0x00000000, 0xbf000000, 0x80000000, 0x00000000
    /* 51: vrintn 1.5 */
    case_un vrintn.f32, 0x00000000, 0x3fc00000, 0x40000000, 0x00000000
    /* 52: vrintn 3.7 */
    case_un vrintn.f32, 0x00000000, 0x406ccccd, 0x40800000, 0x00000000
    /* 53: vrintn 1e10 */
    case_un vrintn.f32, 0x00000000, 0x501502f9, 0x501502f9, 0x00000000
    /* 54: vrintn +inf */
    case_un vrintn.f32, 0x00000000, 0x7f800000, 0x7f800000, 0x00000000
    /* 55: vrintn qnan */
    case_un vrintn.f32, 0x00000000, 0x7fc00001, 0x7fc00001, 0x00000000
    /* 56: vrintn snan */
    case_un vrintn.f32, 0x00000000, 0x7f800001, 0x7fc00001, 0x00000001
    /* 57: vrintn denorm */
    case_un vrintn.f32, 0x00000000, 0x00000001, 0x00000000, 0x00000000
    /* 58: vrintn denorm FZ */
    case_un vrintn.f32, 0x01000000, 0x00000001, 0x00000000, 0x01000080
    /* 59: vrintp 2.5 */
    case_un vrintp.f32, 0x00000000, 0x40200000, 0x40400000, 0x00000000
    /* 60: vrintp -2.5 */
    case_un vrintp.f32, 0x00000000, 0xc0200000, 0xc0000000, 0x00000000
    /* 61: vrintp -0.5 */
    case_un vrintp.f32, 0x00000000, 0xbf000000, 0x80000000, 0x00000000
    /* 62: vrintp 1.5 */
    case_un vrintp.f32, 0x00000000, 0x3fc00000, 0x40000000, 0x00000000
    /* 63: vrintp 3.7 */
    case_un vrintp.f32, 0x00000000, 0x406ccccd, 0x40800000, 0x00000000
    /* 64: vrintp 1e10 */
    case_un vrintp.f32, 0x00000000, 0x501502f9, 0x501502f9, 0x00000000
    /* 65: vrintp +inf */
    case_un vrintp.f32, 0x00000000, 0x7f800000, 0x7f800000, 0x00000000
    /* 66: vrintp qnan */
    case_un vrintp.f32, 0x00000000, 0x7fc00001, 0x7fc00001, 0x00000000
    /* 67: vrintp snan */
    case_un vrintp.f32, 0x00000000, 0x7f800001, 0x7fc00001, 0x00000001
    /* 68: vrintp denorm */
    case_un vrintp.f32, 0x00000000, 0x00000001, 0x3f800000, 0x00000000
    /* 69: vrintp denorm FZ */
    case_un vrintp.f32, 0x01000000, 0x00000001, 0x00000000, 0x01000080
    /* 70: vrintm 2.5 */
    case_un vrintm.f32, 0x00000000, 0x40200000, 0x40000000, 0x00000000
    /* 71: vrintm -2.5 */
    case_un vrintm.f32, 0x00000000, 0xc0200000, 0xc0400000, 0x00000000
    /* 72: vrintm -0.5 */
    case_un vrintm.f32, 0x00000000, 0xbf000000, 0xbf800000, 0x00000000
    /* 73: vrintm 1.5 */
    case_un vrintm.f32, 0x00000000, 0x3fc00000, 0x3f800000, 0x00000000
    /* 74: vrintm 3.7 */
    case_un vrintm.f32, 0x00000000, 0x406ccccd, 0x40400000, 0x00000000
    /* 75: vrintm 1e10 */
    case_un vrintm.f32, 0x00000000, 0x501502f9, 0x501502f9, 0x00000000
    /* 76: vrintm +inf */
    case_un vrintm.f32, 0x00000000, 0x7f800000, 0x7f800000, 0x00000000
    /* 77: vrintm qnan */
    case_un vrintm.f32, 0x00000000, 0x7fc00001, 0x7fc00001, 0x00000000
    /* 78: vrintm snan */
    case_un vrintm.f32, 0x00000000, 0x7f800001, 0x7fc00001, 0x00000001
    /* 79: vrintm denorm */
    case_un vrintm.f32, 0x00000000, 0x00000001, 0x00000000, 0x00000000
    /* 80: vrintm denorm FZ */
    case_un vrintm.f32, 0x01000000, 0x00000001, 0x00000000, 0x01000080
    /* 81: vrintz 2.5 */
    case_un vrintz.f32, 0x00000000, 0x40200000, 0x40000000, 0x00000000
    /* 82: vrintz -2.5 */
    case_un vrintz.f32, 0x00000000, 0xc0200000, 0xc0000000, 0x00000000
    /* 83: vrintz -0.5 */
    case_un vrintz.f32, 0x00000000, 0xbf000000, 0x80000000, 0x00000000
    /* 84: vrintz 1.5 */
    case_un vrintz.f32, 0x00000000, 0x3fc00000, 0x3f800000, 0x00000000
    /* 85: vrintz 3.7 */
    case_un vrintz.f32, 0x00000000, 0x406ccccd, 0x40400000, 0x00000000
    /* 86: vrintz 1e10 */
    case_un vrintz.f32, 0x00000000, 0x501502f9, 0x501502f9, 0x00000000
    /* 87: vrintz +inf */
    case_un vrintz.f32, 0x00000000, 0x7f800000, 0x7f800000, 0x00000000
    /* 88: vrintz qnan */
    case_un vrintz.f32, 0x00000000, 0x7fc00001, 0x7fc00001, 0x00000000
    /* 89: vrintz snan */
    case_un vrintz.f32, 0x00000000, 0x7f800001, 0x7fc00001, 0x00000001
    /* 90: vrintz denorm */
    case_un vrintz.f32, 0x00000000, 0x00000001, 0x00000000, 0x00000000
    /* 91: vrintz denorm FZ */
    case_un vrintz.f32, 0x01000000, 0x00000001, 0x00000000, 0x01000080
    /* 92: vrintr 2.5 rp */
    case_un vrintr.f32, 0x00400000, 0x40200000, 0x40400000, 0x00400000
    /* 93: vrintr -2.5 rp */
    case_un vrintr.f32, 0x00400000, 0xc0200000, 0xc0000000, 0x00400000
    /* 94: vrintr -0.5 rp */
    case_un vrintr.f32, 0x00400000, 0xbf000000, 0x80000000, 0x00400000
    /* 95: vrintr 1.5 rp */
    case_un vrintr.f32, 0x00400000, 0x3fc00000, 0x40000000, 0x00400000
    /* 96: vrintr 3.7 rp */
    case_un vrintr.f32, 0x00400000, 0x406ccccd, 0x40800000, 0x00400000
    /* 97: vrintr 1e10 rp */
    case_un vrintr.f32, 0x00400000, 0x501502f9, 0x501502f9, 0x00400000
    /* 98: vrintr +inf rp */
    case_un vrintr.f32, 0x00400000, 0x7f800000, 0x7f800000, 0x00400000
    /* 99: vrintr qnan rp */
    case_un vrintr.f32, 0x00400000, 0x7fc00001, 0x7fc00001, 0x00400000
    /* 100: vrintr snan rp */
    case_un vrintr.f32, 0x00400000, 0x7f800001, 0x7fc00001, 0x00400001
    /* 101: vrintr denorm rp */
    case_un vrintr.f32, 0x00400000, 0x00000001, 0x3f800000, 0x00400000
    /* 102: vrintr denorm rp FZ */
    case_un vrintr.f32, 0x01400000, 0x00000001, 0x00000000, 0x01400080
    /* 103: vrintr 2.5 rm */
    case_un vrintr.f32, 0x00800000, 0x40200000, 0x40000000, 0x00800000
    /* 104: vrintr -2.5 rm */
    case_un vrintr.f32, 0x00800000, 0xc0200000, 0xc0400000, 0x00800000
    /* 105: vrintr -0.5 rm */
    case_un vrintr.f32, 0x00800000, 0xbf000000, 0xbf800000, 0x00800000
    /* 106: vrintr 1.5 rm */
    case_un vrintr.f32, 0x00800000, 0x3fc00000, 0x3f800000, 0x00800000
    /* 107: vrintr 3.7 rm */
    case_un vrintr.f32, 0x00800000, 0x406ccccd, 0x40400000, 0x00800000
    /* 108: vrintr 1e10 rm */
    case_un vrintr.f32, 0x00800000, 0x501502f9, 0x501502f9, 0x00800000
    /* 109: vrintr +inf rm */
    case_un vrintr.f32, 0x00800000, 0x7f800000, 0x7f800000, 0x00800000
    /* 110: vrintr qnan rm */
    case_un vrintr.f32, 0x00800000, 0x7fc00001, 0x7fc00001, 0x00800000
    /* 111: vrintr snan rm */
    case_un vrintr.f32, 0x00800000, 0x7f800001, 0x7fc00001, 0x00800001
    /* 112: vrintr denorm rm */
    case_un vrintr.f32, 0x00800000, 0x00000001, 0x00000000, 0x00800000
    /* 113: vrintr denorm rm FZ */
    case_un vrintr.f32, 0x01800000, 0x00000001, 0x00000000, 0x01800080
    /* 114: vrintx 2.5 */
    case_un vrintx.f32, 0x00000000, 0x40200000, 0x40000000, 0x00000010
    /* 115: vrintx -2.5 */
    case_un vrintx.f32, 0x00000000, 0xc0200000, 0xc0000000, 0x00000010
    /* 116: vrintx -0.5 */
    case_un vrintx.f32, 0x00000000, 0xbf000000, 0x80000000, 0x00000010
    /* 117: vrintx 1.5 */
    case_un vrintx.f32, 0x00000000, 0x3fc00000, 0x40000000, 0x00000010
    /* 118: vrintx 3.7 */
    case_un vrintx.f32, 0x00000000, 0x406ccccd, 0x40800000, 0x00000010
    /* 119: vrintx 1e10 */
    case_un vrintx.f32, 0x00000000, 0x501502f9, 0x501502f9, 0x00000000
    /* 120: vrintx +inf */
    case_un vrintx.f32, 0x00000000, 0x7f800000, 0x7f800000, 0x00000000
    /* 121: vrintx qnan */
    case_un vrintx.f32, 0x00000000, 0x7fc00001, 0x7fc00001, 0x00000000
    /* 122: vrintx snan */
    case_un vrintx.f32, 0x00000000, 0x7f800001, 0x7fc00001, 0x00000001
    /* 123: vrintx denorm */
    case_un vrintx.f32, 0x00000000, 0x00000001, 0x00000000, 0x00000010
    /* 124: vrintx denorm FZ */
    case_un vrintx.f32, 0x01000000, 0x00000001, 0x00000000, 0x01000080
    /* 125: vcvta.s32 2.5 */
    case_un vcvta.s32.f32, 0x00000000, 0x40200000, 0x00000003, 0x00000010
    /* 126: vcvta.s32 -2.5 */
    case_un vcvta.s32.f32, 0x00000000, 0xc0200000, 0xfffffffd, 0x00000010
    /* 127: vcvta.s32 -0.5 */
    case_un vcvta.s32.f32, 0x00000000, 0xbf000000, 0xffffffff, 0x00000010
    /* 128: vcvta.s32 1.5 */
    case_un vcvta.s32.f32, 0x00000000, 0x3fc00000, 0x00000002, 0x00000010
    /* 129: vcvta.s32 3.7 */
    case_un vcvta.s32.f32, 0x00000000, 0x406ccccd, 0x00000004, 0x00000010
    /* 130: vcvta.s32 3e9 */
    case_un vcvta.s32.f32, 0x00000000, 0x4f32d05e, 0x7fffffff, 0x00000001
    /* 131: vcvta.s32 -1.0 */
    case_un vcvta.s32.f32, 0x00000000, 0xbf800000, 0xffffffff, 0x00000000
    /* 132: vcvta.s32 qnan */
    case_un vcvta.s32.f32, 0x00000000, 0x7fc00001, 0x00000000, 0x00000001
    /* 133: vcvta.s32 +inf */
    case_un vcvta.s32.f32, 0x00000000, 0x7f800000, 0x7fffffff, 0x00000001
    /* 134: vcvta.s32 -inf */
    case_un vcvta.s32.f32, 0x00000000, 0xff800000, 0x80000000, 0x00000001
    /* 135: vcvta.u32 2.5 */
    case_un vcvta.u32.f32, 0x00000000, 0x40200000, 0x00000003, 0x00000010
    /* 136: vcvta.u32 -2.5 */
    case_un vcvta.u32.f32, 0x00000000, 0xc0200000, 0x00000000, 0x00000001
    /* 137: vcvta.u32 -0.5 */
    case_un vcvta.u32.f32, 0x00000000, 0xbf000000, 0x00000000, 0x00000001
    /* 138: vcvta.u32 1.5 */
    case_un vcvta.u32.f32, 0x00000000, 0x3fc00000, 0x00000002, 0x00000010
    /* 139: vcvta.u32 3.7 */
    case_un vcvta.u32.f32, 0x00000000, 0x406ccccd, 0x00000004, 0x00000010
    /* 140: vcvta.u32 3e9 */
    case_un vcvta.u32.f32, 0x00000000, 0x4f32d05e, 0xb2d05e00, 0x00000000
    /* 141: vcvta.u32 -1.0 */
    case_un vcvta.u32.f32, 0x00000000, 0xbf800000, 0x00000000, 0x00000001
    /* 142: vcvta.u32 qnan */
    case_un vcvta.u32.f32, 0x00000000, 0x7fc00001, 0x00000000, 0x00000001
    /* 143: vcvta.u32 +inf */
    case_un vcvta.u32.f32, 0x00000000, 0x7f800000, 0xffffffff, 0x00000001
    /* 144: vcvta.u32 -inf */
    case_un vcvta.u32.f32, 0x00000000, 0xff800000, 0x00000000, 0x00000001
    /* 145: vcvtn.s32 2.5 */
    case_un vcvtn.s32.f32, 0x00000000, 0x40200000, 0x00000002, 0x00000010
    /* 146: vcvtn.s32 -2.5 */
    case_un vcvtn.s32.f32, 0x00000000, 0xc0200000, 0xfffffffe, 0x00000010
    /* 147: vcvtn.s32 -0.5 */
    case_un vcvtn.s32.f32, 0x00000000, 0xbf000000, 0x00000000, 0x00000010
    /* 148: vcvtn.s32 1.5 */
    case_un vcvtn.s32.f32, 0x00000000, 0x3fc00000, 0x00000002, 0x00000010
    /* 149: vcvtn.s32 3.7 */
    case_un vcvtn.s32.f32, 0x00000000, 0x406ccccd, 0x00000004, 0x00000010
    /* 150: vcvtn.s32 3e9 */
    case_un vcvtn.s32.f32, 0x00000000, 0x4f32d05e, 0x7fffffff, 0x00000001
    /* 151: vcvtn.s32 -1.0 */
    case_un vcvtn.s32.f32, 0x00000000, 0xbf800000, 0xffffffff, 0x00000000
    /* 152: vcvtn.s32 qnan */
    case_un vcvtn.s32.f32, 0x00000000, 0x7fc00001, 0x00000000, 0x00000001
    /* 153: vcvtn.s32 +inf */
    case_un vcvtn.s32.f32, 0x00000000, 0x7f800000, 0x7fffffff, 0x00000001
    /* 154: vcvtn.s32 -inf */
    case_un vcvtn.s32.f32, 0x00000000, 0xff800000, 0x80000000, 0x00000001
    /* 155: vcvtn.u32 2.5 */
    case_un vcvtn.u32.f32, 0x00000000, 0x40200000, 0x00000002, 0x00000010
    /* 156: vcvtn.u32 -2.5 */
    case_un vcvtn.u32.f32, 0x00000000, 0xc0200000, 0x00000000, 0x00000001
    /* 157: vcvtn.u32 -0.5 */
    case_un vcvtn.u32.f32, 0x00000000, 0xbf000000, 0x00000000, 0x00000010
    /* 158: vcvtn.u32 1.5 */
    case_un vcvtn.u32.f32, 0x00000000, 0x3fc00000, 0x00000002, 0x00000010
    /* 159: vcvtn.u32 3.7 */
    case_un vcvtn.u32.f32, 0x00000000, 0x406ccccd, 0x00000004, 0x00000010
    /* 160: vcvtn.u32 3e9 */
    case_un vcvtn.u32.f32, 0x00000000, 0x4f32d05e, 0xb2d05e00, 0x00000000
    /* 161: vcvtn.u32 -1.0 */
    case_un vcvtn.u32.f32, 0x00000000, 0xbf800000, 0x00000000, 0x00000001
    /* 162: vcvtn.u32 qnan */
    case_un vcvtn.u32.f32, 0x00000000, 0x7fc00001, 0x00000000, 0x00000001
    /* 163: vcvtn.u32 +inf */
    case_un vcvtn.u32.f32, 0x00000000, 0x7f800000, 0xffffffff, 0x00000001
    /* 164: vcvtn.u32 -inf */
    case_un vcvtn.u32.f32, 0x00000000, 0xff800000, 0x00000000, 0x00000001
    /* 165: vcvtp.s32 2.5 */
    case_un vcvtp.s32.f32, 0x00000000, 0x40200000, 0x00000003, 0x00000010
    /* 166: vcvtp.s32 -2.5 */
    case_un vcvtp.s32.f32, 0x00000000, 0xc0200000, 0xfffffffe, 0x00000010
    /* 167: vcvtp.s32 -0.5 */
    case_un vcvtp.s32.f32, 0x00000000, 0xbf000000, 0x00000000, 0x00000010
    /* 168: vcvtp.s32 1.5 */
    case_un vcvtp.s32.f32, 0x00000000, 0x3fc00000, 0x00000002, 0x00000010
    /* 169: vcvtp.s32 3.7 */
    case_un vcvtp.s32.f32, 0x00000000, 0x406ccccd, 0x00000004, 0x00000010
    /* 170: vcvtp.s32 3e9 */
    case_un vcvtp.s32.f32, 0x00000000, 0x4f32d05e, 0x7fffffff, 0x00000001
    /* 171: vcvtp.s32 -1.0 */
    case_un vcvtp.s32.f32, 0x00000000, 0xbf800000, 0xffffffff, 0x00000000
    /* 172: vcvtp.s32 qnan */
    case_un vcvtp.s32.f32, 0x00000000, 0x7fc00001, 0x00000000, 0x00000001
    /* 173: vcvtp.s32 +inf */
    case_un vcvtp.s32.f32, 0x00000000, 0x7f800000, 0x7fffffff, 0x00000001
    /* 174: vcvtp.s32 -inf */
    case_un vcvtp.s32.f32, 0x00000000, 0xff800000, 0x80000000, 0x00000001
    /* 175: vcvtp.u32 2.5 */
    case_un vcvtp.u32.f32, 0x00000000, 0x40200000, 0x00000003, 0x00000010
    /* 176: vcvtp.u32 -2.5 */
    case_un vcvtp.u32.f32, 0x00000000, 0xc0200000, 0x00000000, 0x00000001
    /* 177: vcvtp.u32 -0.5 */
    case_un vcvtp.u32.f32, 0x00000000, 0xbf000000, 0x00000000, 0x00000010
    /* 178: vcvtp.u32 1.5 */
    case_un vcvtp.u32.f32, 0x00000000, 0x3fc00000, 0x00000002, 0x00000010
    /* 179: vcvtp.u32 3.7 */
    case_un vcvtp.u32.f32, 0x00000000, 0x406ccccd, 0x00000004, 0x00000010
    /* 180: vcvtp.u32 3e9 */
    case_un vcvtp.u32.f32, 0x00000000, 0x4f32d05e, 0xb2d05e00, 0x00000000
    /* 181: vcvtp.u32 -1.0 */
    case_un vcvtp.u32.f32, 0x00000000, 0xbf800000, 0x00000000, 0x00000001
    /* 182: vcvtp.u32 qnan */
    case_un vcvtp.u32.f32, 0x00000000, 0x7fc00001, 0x00000000, 0x00000001
    /* 183: vcvtp.u32 +inf */
    case_un vcvtp.u32.f32, 0x00000000, 0x7f800000, 0xffffffff, 0x00000001
    /* 184: vcvtp.u32 -inf */
    case_un vcvtp.u32.f32, 0x00000000, 0xff800000, 0x00000000, 0x00000001
    /* 185: vcvtm.s32 2.5 */
    case_un vcvtm.s32.f32, 0x00000000, 0x40200000, 0x00000002, 0x00000010
    /* 186: vcvtm.s32 -2.5 */
    case_un vcvtm.s32.f32, 0x00000000, 0xc0200000, 0xfffffffd, 0x00000010
    /* 187: vcvtm.s32 -0.5 */
    case_un vcvtm.s32.f32, 0x00000000, 0xbf000000, 0xffffffff, 0x00000010
    /* 188: vcvtm.s32 1.5 */
    case_un vcvtm.s32.f32, 0x00000000, 0x3fc00000, 0x00000001, 0x00000010
    /* 189: vcvtm.s32 3.7 */
    case_un vcvtm.s32.f32, 0x00000000, 0x406ccccd, 0x00000003, 0x00000010
    /* 190: vcvtm.s32 3e9 */
    case_un vcvtm.s32.f32, 0x00000000, 0x4f32d05e, 0x7fffffff, 0x00000001
    /* 191: vcvtm.s32 -1.0 */
    case_un vcvtm.s32.f32, 0x00000000, 0xbf800000, 0xffffffff, 0x00000000
    /* 192: vcvtm.s32 qnan */
    case_un vcvtm.s32.f32, 0x00000000, 0x7fc00001, 0x00000000, 0x00000001
    /* 193: vcvtm.s32 +inf */
    case_un vcvtm.s32.f32, 0x00000000, 0x7f800000, 0x7fffffff, 0x00000001
    /* 194: vcvtm.s32 -inf */
    case_un vcvtm.s32.f32, 0x00000000, 0xff800000, 0x80000000, 0x00000001
    /* 195: vcvtm.u32 2.5 */
    case_un vcvtm.u32.f32, 0x00000000, 0x40200000, 0x00000002, 0x00000010
    /* 196: vcvtm.u32 -2.5 */
    case_un vcvtm.u32.f32, 0x00000000, 0xc0200000, 0x00000000, 0x00000001
    /* 197: vcvtm.u32 -0.5 */
    case_un vcvtm.u32.f32, 0x00000000, 0xbf000000, 0x00000000, 0x00000001
    /* 198: vcvtm.u32 1.5 */
    case_un vcvtm.u32.f32, 0x00000000, 0x3fc00000, 0x00000001, 0x00000010
    /* 199: vcvtm.u32 3.7 */
    case_un vcvtm.u32.f32, 0x00000000, 0x406ccccd, 0x00000003, 0x00000010
    /* 200: vcvtm.u32 3e9 */
    case_un vcvtm.u32.f32, 0x00000000, 0x4f32d05e, 0xb2d05e00, 0x00000000
    /* 201: vcvtm.u32 -1.0 */
    case_un vcvtm.u32.f32, 0x00000000, 0xbf800000, 0x00000000, 0x00000001
    /* 202: vcvtm.u32 qnan */
    case_un vcvtm.u32.f32, 0x00000000, 0x7fc00001, 0x00000000, 0x00000001
    /* 203: vcvtm.u32 +inf */
    case_un vcvtm.u32.f32, 0x00000000, 0x7f800000, 0xffffffff, 0x00000001
    /* 204: vcvtm.u32 -inf */
    case_un vcvtm.u32.f32, 0x00000000, 0xff800000, 0x00000000, 0x00000001
    /* 205: vcvt.s32 2.5 (rp ignored) */
    case_un vcvt.s32.f32, 0x00400000, 0x40200000, 0x00000002, 0x00400010
    /* 206: vcvtr.s32 2.5 rp */
    case_un vcvtr.s32.f32, 0x00400000, 0x40200000, 0x00000003, 0x00400010
    /* 207: vcvt.s32 -2.5 (rp ignored) */
    case_un vcvt.s32.f32, 0x00400000, 0xc0200000, 0xfffffffe, 0x00400010
    /* 208: vcvtr.s32 -2.5 rp */
    case_un vcvtr.s32.f32, 0x00400000, 0xc0200000, 0xfffffffe, 0x00400010
    /* 209: vcvt.s32 -0.5 (rp ignored) */
    case_un vcvt.s32.f32, 0x00400000, 0xbf000000, 0x00000000, 0x00400010
    /* 210: vcvtr.s32 -0.5 rp */
    case_un vcvtr.s32.f32, 0x00400000, 0xbf000000, 0x00000000, 0x00400010
    /* 211: vcvt.s32 1.5 (rp ignored) */
    case_un vcvt.s32.f32, 0x00400000, 0x3fc00000, 0x00000001, 0x00400010
    /* 212: vcvtr.s32 1.5 rp */
    case_un vcvtr.s32.f32, 0x00400000, 0x3fc00000, 0x00000002, 0x00400010
    /* 213: vcvt.s32 3.7 (rp ignored) */
    case_un vcvt.s32.f32, 0x00400000, 0x406ccccd, 0x00000003, 0x00400010
    /* 214: vcvtr.s32 3.7 rp */
    case_un vcvtr.s32.f32, 0x00400000, 0x406ccccd, 0x00000004, 0x00400010
    /* 215: vcvt.s32 3e9 (rp ignored) */
    case_un vcvt.s32.f32, 0x00400000, 0x4f32d05e, 0x7fffffff, 0x00400001
    /* 216: vcvtr.s32 3e9 rp */
    case_un vcvtr.s32.f32, 0x00400000, 0x4f32d05e, 0x7fffffff, 0x00400001
    /* 217: vcvt.s32 -1.0 (rp ignored) */
    case_un vcvt.s32.f32, 0x00400000, 0xbf800000, 0xffffffff, 0x00400000
    /* 218: vcvtr.s32 -1.0 rp */
    case_un vcvtr.s32.f32, 0x00400000, 0xbf800000, 0xffffffff, 0x00400000
    /* 219: vcvt.s32 qnan (rp ignored) */
    case_un vcvt.s32.f32, 0x00400000, 0x7fc00001, 0x00000000, 0x00400001
    /* 220: vcvtr.s32 qnan rp */
    case_un vcvtr.s32.f32, 0x00400000, 0x7fc00001, 0x00000000, 0x00400001
    /* 221: vcvt.s32 +inf (rp ignored) */
    case_un vcvt.s32.f32, 0x00400000, 0x7f800000, 0x7fffffff, 0x00400001
    /* 222: vcvtr.s32 +inf rp */
    case_un vcvtr.s32.f32, 0x00400000, 0x7f800000, 0x7fffffff, 0x00400001
    /* 223: vcvt.s32 -inf (rp ignored) */
    case_un vcvt.s32.f32, 0x00400000, 0xff800000, 0x80000000, 0x00400001
    /* 224: vcvtr.s32 -inf rp */
    case_un vcvtr.s32.f32, 0x00400000, 0xff800000, 0x80000000, 0x00400001
    /* 225: vcvt.u32 2.5 (rp ignored) */
    case_un vcvt.u32.f32, 0x00400000, 0x40200000, 0x00000002, 0x00400010
    /* 226: vcvtr.u32 2.5 rp */
    case_un vcvtr.u32.f32, 0x00400000, 0x40200000, 0x00000003, 0x00400010
    /* 227: vcvt.u32 -2.5 (rp ignored) */
    case_un vcvt.u32.f32, 0x00400000, 0xc0200000, 0x00000000, 0x00400001
    /* 228: vcvtr.u32 -2.5 rp */
    case_un vcvtr.u32.f32, 0x00400000, 0xc0200000, 0x00000000, 0x00400001
    /* 229: vcvt.u32 -0.5 (rp ignored) */
    case_un vcvt.u32.f32, 0x00400000, 0xbf000000, 0x00000000, 0x00400010
    /* 230: vcvtr.u32 -0.5 rp */
    case_un vcvtr.u32.f32, 0x00400000, 0xbf000000, 0x00000000, 0x00400010
    /* 231: vcvt.u32 1.5 (rp ignored) */
    case_un vcvt.u32.f32, 0x00400000, 0x3fc00000, 0x00000001, 0x00400010
    /* 232: vcvtr.u32 1.5 rp */
    case_un vcvtr.u32.f32, 0x00400000, 0x3fc00000, 0x00000002, 0x00400010
    /* 233: vcvt.u32 3.7 (rp ignored) */
    case_un vcvt.u32.f32, 0x00400000, 0x406ccccd, 0x00000003, 0x00400010
    /* 234: vcvtr.u32 3.7 rp */
    case_un vcvtr.u32.f32, 0x00400000, 0x406ccccd, 0x00000004, 0x00400010
    /* 235: vcvt.u32 3e9 (rp ignored) */
    case_un vcvt.u32.f32, 0x00400000, 0x4f32d05e, 0xb2d05e00, 0x00400000
    /* 236: vcvtr.u32 3e9 rp */
    case_un vcvtr.u32.f32, 0x00400000, 0x4f32d05e, 0xb2d05e00, 0x00400000
    /* 237: vcvt.u32 -1.0 (rp ignored) */
    case_un vcvt.u32.f32, 0x00400000, 0xbf800000, 0x00000000, 0x00400001
    /* 238: vcvtr.u32 -1.0 rp */
    case_un vcvtr.u32.f32, 0x00400000, 0xbf800000, 0x00000000, 0x00400001
    /* 239: vcvt.u32 qnan (rp ignored) */
    case_un vcvt.u32.f32, 0x00400000, 0x7fc00001, 0x00000000, 0x00400001
    /* 240: vcvtr.u32 qnan rp */
    case_un vcvtr.u32.f32, 0x00400000, 0x7fc00001, 0x00000000, 0x00400001
    /* 241: vcvt.u32 +inf (rp ignored) */
    case_un vcvt.u32.f32, 0x00400000, 0x7f800000, 0xffffffff, 0x00400001
    /* 242: vcvtr.u32 +inf rp */
    case_un vcvtr.u32.f32, 0x00400000, 0x7f800000, 0xffffffff, 0x00400001
    /* 243: vcvt.u32 -inf (rp ignored) */
    case_un vcvt.u32.f32, 0x00400000, 0xff800000, 0x00000000, 0x00400001
    /* 244: vcvtr.u32 -inf rp */
    case_un vcvtr.u32.f32, 0x00400000, 0xff800000, 0x00000000, 0x00400001

.ifdef PICO
    bkpt    #0
.else
.ifndef DUMP
    cmp     r9, #0
    bne     fail
.endif
    ldr     r0, =PASS_VAL
    ldr     r1, =TEST_RESULT
    str     r0, [r1]
    b       done
fail:
    ldr     r0, =FAIL_VAL
    ldr     r1, =TEST_RESULT
    str     r0, [r1]
    ldr     r1, =TEST_SUBTEST
    str     r9, [r1]
done:
    mov     r0, #0x18
    ldr     r1, =0x20026
    bkpt    #0xab
    b       .

    /* Any fault fails the test, with subtest 0xFFFF */
    .thumb_func
Fault_Handler:
    ldr     r0, =FAIL_VAL
    ldr     r1, =TEST_RESULT
    str     r0, [r1]
    ldr     r0, =0xFFFF
    ldr     r1, =TEST_SUBTEST
    str     r0, [r1]
    mov     r0, #0x18
    ldr     r1, =0x20026
    bkpt    #0xab
    b       .
.endif
