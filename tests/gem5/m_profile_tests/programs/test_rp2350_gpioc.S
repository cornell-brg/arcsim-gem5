/*
 * RP2350 GPIO coprocessor (p0) and SIO GPIO registers, against silicon.
 *
 * 108 GPIO operations (every coprocessor write form on OUT and OE, both banks;
 * single-pin and index forms, including out-of-range pins and values; SIO's
 * SET/CLR/XOR aliases), each followed by a snapshot of OUT, OE and IN through
 * both the coprocessor and SIO; then CPUID, the DCP RCMP read, and DWT cycles
 * for 32 of each access. The expected values were recorded on a Pimoroni Pico
 * Plus 2 (RP2350, Cortex-M33 core 0, Secure, this code in SRAM) through
 * OpenOCD; with CHECK the image compares its records with them (all but the
 * pad inputs, the state before the baseline writes finish, and the cycle counts) and
 * prints RP2350_GPIOC_OK or RP2350_GPIOC_FAIL.
 *
 * Built by this directory's Makefile (with CHECK) and registered in
 * test_m_profile.py to run on configs/example/rp2350/run.py --core
 * arm-m4-proxy.
 *
 * Without CHECK the image only records, from 0x20004000 on: the build that
 * ran on the Pico (OpenOCD: load_image, then start at Reset_Handler). With
 * FAULTS it also records the NOCP cases (CPACR off, coprocessors the RP2350
 * does not have); gem5 tests those in tests/gem5/m_profile_tests
 * (test_coproc_nocp, test_coproc_rp2350).
 */
    .syntax unified
    .cpu cortex-m33
    .thumb

    .equ OUT_BASE, 0x20004000
    .equ SIO, 0xd0000000

    .macro snap
    mrrc    p0, #0, r0, r1, c0
    mrrc    p0, #0, r2, r3, c4
    stmia   r10!, {r0-r3}
    mrrc    p0, #0, r0, r1, c8
    stmia   r10!, {r0, r1}
    ldr     r0, [r11, #0x10]
    ldr     r1, [r11, #0x14]
    ldr     r2, [r11, #0x30]
    ldr     r3, [r11, #0x34]
    stmia   r10!, {r0-r3}
    ldr     r0, [r11, #0x04]
    ldr     r1, [r11, #0x08]
    stmia   r10!, {r0, r1}
    .endm

    .macro timed n, body:vararg
    ldr     r5, [r4]
    .rept   \n
    \body
    .endr
    ldr     r6, [r4]
    sub     r6, r6, r5
    str     r6, [r10], #4
    .endm

    .section .text
    .align  2
    /* initial SP and reset vector, for gem5, which boots from them; OpenOCD
     * starts the Pico at Reset_Handler directly */
    .word   0x20040000
    .word   Reset_Handler
    .thumb_func
    .globl  Reset_Handler
Reset_Handler:
    ldr     r0, =0x20040000
    mov     sp, r0
    cpsid   i
    /* CPACR: CP0 (GPIO), CP4 (DCP), CP10/CP11 full access */
    ldr     r0, =0xE000ED88
    ldr     r1, =0x00F00303
    str     r1, [r0]
    dsb
    isb
    ldr     r10, =OUT_BASE
    ldr     r11, =SIO

    snap
    /* sio OUT=0 */
    ldr r0, =0x0
    str r0, [r11, #0x10]
    snap
    /* sio HI_OUT=0 */
    ldr r0, =0x0
    str r0, [r11, #0x14]
    snap
    /* sio OE=0 */
    ldr r0, =0x0
    str r0, [r11, #0x30]
    snap
    /* sio HI_OE=0 */
    ldr r0, =0x0
    str r0, [r11, #0x34]
    snap
    b 9f
    .ltorg
9:
    /* mcr #0 c0 OUT lo put a5a5a5a5 */
    ldr r0, =0xa5a5a5a5
    mcr p0, #0, r0, c0, c0
    snap
    /* mcr #0 c1 OUT hi put ffffffff (writable bits) */
    ldr r0, =0xffffffff
    mcr p0, #0, r0, c0, c1
    snap
    /* mcr #1 c0 OUT lo xor ffff0000 */
    ldr r0, =0xffff0000
    mcr p0, #1, r0, c0, c0
    snap
    /* mcr #2 c0 OUT lo set f */
    ldr r0, =0xf
    mcr p0, #2, r0, c0, c0
    snap
    b 9f
    .ltorg
9:
    /* mcr #3 c0 OUT lo clr f0 */
    ldr r0, =0xf0
    mcr p0, #3, r0, c0, c0
    snap
    /* mcr #1 c1 OUT hi xor ffff */
    ldr r0, =0xffff
    mcr p0, #1, r0, c0, c1
    snap
    /* mcr #2 c1 OUT hi set 1 */
    ldr r0, =0x1
    mcr p0, #2, r0, c0, c1
    snap
    /* mcr #3 c1 OUT hi clr 2 */
    ldr r0, =0x2
    mcr p0, #3, r0, c0, c1
    snap
    b 9f
    .ltorg
9:
    /* mcrr #0 c0 OUT put 12345678 / 5555 */
    ldr r0, =0x12345678
    ldr r1, =0x5555
    mcrr p0, #0, r0, r1, c0
    snap
    /* mcrr #1 c0 OUT xor ff / 3 */
    ldr r0, =0xff
    ldr r1, =0x3
    mcrr p0, #1, r0, r1, c0
    snap
    /* mcrr #2 c0 OUT set 0f000000 / 100 */
    ldr r0, =0xf000000
    ldr r1, =0x100
    mcrr p0, #2, r0, r1, c0
    snap
    /* mcrr #3 c0 OUT clr f00 / 4 */
    ldr r0, =0xf00
    ldr r1, =0x4
    mcrr p0, #3, r0, r1, c0
    snap
    b 9f
    .ltorg
9:
    /* mcrr #4 c0 OUT bit put pin 6 val 0x1 */
    ldr r0, =0x6
    ldr r1, =0x1
    mcrr p0, #4, r0, r1, c0
    snap
    /* mcrr #4 c0 OUT bit put pin 6 val 0x0 */
    ldr r0, =0x6
    ldr r1, =0x0
    mcrr p0, #4, r0, r1, c0
    snap
    /* mcrr #4 c0 OUT bit put pin 40 val 0x1 */
    ldr r0, =0x28
    ldr r1, =0x1
    mcrr p0, #4, r0, r1, c0
    snap
    /* mcrr #4 c0 OUT bit put pin 6 val 0x2 */
    ldr r0, =0x6
    ldr r1, =0x2
    mcrr p0, #4, r0, r1, c0
    snap
    b 9f
    .ltorg
9:
    /* mcrr #4 c0 OUT bit put pin 7 val 0xffffffff */
    ldr r0, =0x7
    ldr r1, =0xffffffff
    mcrr p0, #4, r0, r1, c0
    snap
    /* mcrr #4 c0 OUT bit put pin 63 val 0x1 */
    ldr r0, =0x3f
    ldr r1, =0x1
    mcrr p0, #4, r0, r1, c0
    snap
    /* mcrr #4 c0 OUT bit put pin 64 val 0x1 */
    ldr r0, =0x40
    ldr r1, =0x1
    mcrr p0, #4, r0, r1, c0
    snap
    /* mcrr #4 c0 OUT bit put pin 70 val 0x1 */
    ldr r0, =0x46
    ldr r1, =0x1
    mcrr p0, #4, r0, r1, c0
    snap
    b 9f
    .ltorg
9:
    /* mcr #5 c0 OUT bit xor pin 9 */
    ldr r0, =0x9
    mcr p0, #5, r0, c0, c0
    snap
    /* mcr #5 c0 OUT bit xor pin 41 */
    ldr r0, =0x29
    mcr p0, #5, r0, c0, c0
    snap
    /* mcrr #5 c0 OUT bit xor2 pin 10 val 0 */
    ldr r0, =0xa
    ldr r1, =0x0
    mcrr p0, #5, r0, r1, c0
    snap
    /* mcrr #5 c0 OUT bit xor2 pin 10 val 1 */
    ldr r0, =0xa
    ldr r1, =0x1
    mcrr p0, #5, r0, r1, c0
    snap
    b 9f
    .ltorg
9:
    /* mcr #6 c0 OUT bit set pin 9 */
    ldr r0, =0x9
    mcr p0, #6, r0, c0, c0
    snap
    /* mcr #6 c0 OUT bit set pin 41 */
    ldr r0, =0x29
    mcr p0, #6, r0, c0, c0
    snap
    /* mcrr #6 c0 OUT bit set2 pin 10 val 0 */
    ldr r0, =0xa
    ldr r1, =0x0
    mcrr p0, #6, r0, r1, c0
    snap
    /* mcrr #6 c0 OUT bit set2 pin 10 val 1 */
    ldr r0, =0xa
    ldr r1, =0x1
    mcrr p0, #6, r0, r1, c0
    snap
    b 9f
    .ltorg
9:
    /* mcr #7 c0 OUT bit clr pin 9 */
    ldr r0, =0x9
    mcr p0, #7, r0, c0, c0
    snap
    /* mcr #7 c0 OUT bit clr pin 41 */
    ldr r0, =0x29
    mcr p0, #7, r0, c0, c0
    snap
    /* mcrr #7 c0 OUT bit clr2 pin 10 val 0 */
    ldr r0, =0xa
    ldr r1, =0x0
    mcrr p0, #7, r0, r1, c0
    snap
    /* mcrr #7 c0 OUT bit clr2 pin 10 val 1 */
    ldr r0, =0xa
    ldr r1, =0x1
    mcrr p0, #7, r0, r1, c0
    snap
    b 9f
    .ltorg
9:
    /* mcrr #8 c0 OUT index put idx 0 */
    ldr r0, =0xdeadbeef
    ldr r1, =0x0
    mcrr p0, #8, r0, r1, c0
    snap
    /* mcrr #8 c0 OUT index put idx 1 */
    ldr r0, =0xdeadbeef
    ldr r1, =0x1
    mcrr p0, #8, r0, r1, c0
    snap
    /* mcrr #8 c0 OUT index put idx 2 */
    ldr r0, =0xdeadbeef
    ldr r1, =0x2
    mcrr p0, #8, r0, r1, c0
    snap
    /* mcrr #9 c0 OUT index xor idx 0 */
    ldr r0, =0xf000f0
    ldr r1, =0x0
    mcrr p0, #9, r0, r1, c0
    snap
    b 9f
    .ltorg
9:
    /* mcrr #9 c0 OUT index xor idx 1 */
    ldr r0, =0xf000f0
    ldr r1, =0x1
    mcrr p0, #9, r0, r1, c0
    snap
    /* mcrr #9 c0 OUT index xor idx 2 */
    ldr r0, =0xf000f0
    ldr r1, =0x2
    mcrr p0, #9, r0, r1, c0
    snap
    /* mcrr #10 c0 OUT index set idx 0 */
    ldr r0, =0xf000f0
    ldr r1, =0x0
    mcrr p0, #10, r0, r1, c0
    snap
    /* mcrr #10 c0 OUT index set idx 1 */
    ldr r0, =0xf000f0
    ldr r1, =0x1
    mcrr p0, #10, r0, r1, c0
    snap
    b 9f
    .ltorg
9:
    /* mcrr #10 c0 OUT index set idx 2 */
    ldr r0, =0xf000f0
    ldr r1, =0x2
    mcrr p0, #10, r0, r1, c0
    snap
    /* mcrr #11 c0 OUT index clr idx 0 */
    ldr r0, =0xf000f0
    ldr r1, =0x0
    mcrr p0, #11, r0, r1, c0
    snap
    /* mcrr #11 c0 OUT index clr idx 1 */
    ldr r0, =0xf000f0
    ldr r1, =0x1
    mcrr p0, #11, r0, r1, c0
    snap
    /* mcrr #11 c0 OUT index clr idx 2 */
    ldr r0, =0xf000f0
    ldr r1, =0x2
    mcrr p0, #11, r0, r1, c0
    snap
    b 9f
    .ltorg
9:
    /* mcr #0 c4 OE lo put a5a5a5a5 */
    ldr r0, =0xa5a5a5a5
    mcr p0, #0, r0, c0, c4
    snap
    /* mcr #0 c5 OE hi put ffffffff (writable bits) */
    ldr r0, =0xffffffff
    mcr p0, #0, r0, c0, c5
    snap
    /* mcr #1 c4 OE lo xor ffff0000 */
    ldr r0, =0xffff0000
    mcr p0, #1, r0, c0, c4
    snap
    /* mcr #2 c4 OE lo set f */
    ldr r0, =0xf
    mcr p0, #2, r0, c0, c4
    snap
    b 9f
    .ltorg
9:
    /* mcr #3 c4 OE lo clr f0 */
    ldr r0, =0xf0
    mcr p0, #3, r0, c0, c4
    snap
    /* mcr #1 c5 OE hi xor ffff */
    ldr r0, =0xffff
    mcr p0, #1, r0, c0, c5
    snap
    /* mcr #2 c5 OE hi set 1 */
    ldr r0, =0x1
    mcr p0, #2, r0, c0, c5
    snap
    /* mcr #3 c5 OE hi clr 2 */
    ldr r0, =0x2
    mcr p0, #3, r0, c0, c5
    snap
    b 9f
    .ltorg
9:
    /* mcrr #0 c4 OE put 12345678 / 5555 */
    ldr r0, =0x12345678
    ldr r1, =0x5555
    mcrr p0, #0, r0, r1, c4
    snap
    /* mcrr #1 c4 OE xor ff / 3 */
    ldr r0, =0xff
    ldr r1, =0x3
    mcrr p0, #1, r0, r1, c4
    snap
    /* mcrr #2 c4 OE set 0f000000 / 100 */
    ldr r0, =0xf000000
    ldr r1, =0x100
    mcrr p0, #2, r0, r1, c4
    snap
    /* mcrr #3 c4 OE clr f00 / 4 */
    ldr r0, =0xf00
    ldr r1, =0x4
    mcrr p0, #3, r0, r1, c4
    snap
    b 9f
    .ltorg
9:
    /* mcrr #4 c4 OE bit put pin 6 val 0x1 */
    ldr r0, =0x6
    ldr r1, =0x1
    mcrr p0, #4, r0, r1, c4
    snap
    /* mcrr #4 c4 OE bit put pin 6 val 0x0 */
    ldr r0, =0x6
    ldr r1, =0x0
    mcrr p0, #4, r0, r1, c4
    snap
    /* mcrr #4 c4 OE bit put pin 40 val 0x1 */
    ldr r0, =0x28
    ldr r1, =0x1
    mcrr p0, #4, r0, r1, c4
    snap
    /* mcrr #4 c4 OE bit put pin 6 val 0x2 */
    ldr r0, =0x6
    ldr r1, =0x2
    mcrr p0, #4, r0, r1, c4
    snap
    b 9f
    .ltorg
9:
    /* mcrr #4 c4 OE bit put pin 7 val 0xffffffff */
    ldr r0, =0x7
    ldr r1, =0xffffffff
    mcrr p0, #4, r0, r1, c4
    snap
    /* mcrr #4 c4 OE bit put pin 63 val 0x1 */
    ldr r0, =0x3f
    ldr r1, =0x1
    mcrr p0, #4, r0, r1, c4
    snap
    /* mcrr #4 c4 OE bit put pin 64 val 0x1 */
    ldr r0, =0x40
    ldr r1, =0x1
    mcrr p0, #4, r0, r1, c4
    snap
    /* mcrr #4 c4 OE bit put pin 70 val 0x1 */
    ldr r0, =0x46
    ldr r1, =0x1
    mcrr p0, #4, r0, r1, c4
    snap
    b 9f
    .ltorg
9:
    /* mcr #5 c4 OE bit xor pin 9 */
    ldr r0, =0x9
    mcr p0, #5, r0, c0, c4
    snap
    /* mcr #5 c4 OE bit xor pin 41 */
    ldr r0, =0x29
    mcr p0, #5, r0, c0, c4
    snap
    /* mcrr #5 c4 OE bit xor2 pin 10 val 0 */
    ldr r0, =0xa
    ldr r1, =0x0
    mcrr p0, #5, r0, r1, c4
    snap
    /* mcrr #5 c4 OE bit xor2 pin 10 val 1 */
    ldr r0, =0xa
    ldr r1, =0x1
    mcrr p0, #5, r0, r1, c4
    snap
    b 9f
    .ltorg
9:
    /* mcr #6 c4 OE bit set pin 9 */
    ldr r0, =0x9
    mcr p0, #6, r0, c0, c4
    snap
    /* mcr #6 c4 OE bit set pin 41 */
    ldr r0, =0x29
    mcr p0, #6, r0, c0, c4
    snap
    /* mcrr #6 c4 OE bit set2 pin 10 val 0 */
    ldr r0, =0xa
    ldr r1, =0x0
    mcrr p0, #6, r0, r1, c4
    snap
    /* mcrr #6 c4 OE bit set2 pin 10 val 1 */
    ldr r0, =0xa
    ldr r1, =0x1
    mcrr p0, #6, r0, r1, c4
    snap
    b 9f
    .ltorg
9:
    /* mcr #7 c4 OE bit clr pin 9 */
    ldr r0, =0x9
    mcr p0, #7, r0, c0, c4
    snap
    /* mcr #7 c4 OE bit clr pin 41 */
    ldr r0, =0x29
    mcr p0, #7, r0, c0, c4
    snap
    /* mcrr #7 c4 OE bit clr2 pin 10 val 0 */
    ldr r0, =0xa
    ldr r1, =0x0
    mcrr p0, #7, r0, r1, c4
    snap
    /* mcrr #7 c4 OE bit clr2 pin 10 val 1 */
    ldr r0, =0xa
    ldr r1, =0x1
    mcrr p0, #7, r0, r1, c4
    snap
    b 9f
    .ltorg
9:
    /* mcrr #8 c4 OE index put idx 0 */
    ldr r0, =0xdeadbeef
    ldr r1, =0x0
    mcrr p0, #8, r0, r1, c4
    snap
    /* mcrr #8 c4 OE index put idx 1 */
    ldr r0, =0xdeadbeef
    ldr r1, =0x1
    mcrr p0, #8, r0, r1, c4
    snap
    /* mcrr #8 c4 OE index put idx 2 */
    ldr r0, =0xdeadbeef
    ldr r1, =0x2
    mcrr p0, #8, r0, r1, c4
    snap
    /* mcrr #9 c4 OE index xor idx 0 */
    ldr r0, =0xf000f0
    ldr r1, =0x0
    mcrr p0, #9, r0, r1, c4
    snap
    b 9f
    .ltorg
9:
    /* mcrr #9 c4 OE index xor idx 1 */
    ldr r0, =0xf000f0
    ldr r1, =0x1
    mcrr p0, #9, r0, r1, c4
    snap
    /* mcrr #9 c4 OE index xor idx 2 */
    ldr r0, =0xf000f0
    ldr r1, =0x2
    mcrr p0, #9, r0, r1, c4
    snap
    /* mcrr #10 c4 OE index set idx 0 */
    ldr r0, =0xf000f0
    ldr r1, =0x0
    mcrr p0, #10, r0, r1, c4
    snap
    /* mcrr #10 c4 OE index set idx 1 */
    ldr r0, =0xf000f0
    ldr r1, =0x1
    mcrr p0, #10, r0, r1, c4
    snap
    b 9f
    .ltorg
9:
    /* mcrr #10 c4 OE index set idx 2 */
    ldr r0, =0xf000f0
    ldr r1, =0x2
    mcrr p0, #10, r0, r1, c4
    snap
    /* mcrr #11 c4 OE index clr idx 0 */
    ldr r0, =0xf000f0
    ldr r1, =0x0
    mcrr p0, #11, r0, r1, c4
    snap
    /* mcrr #11 c4 OE index clr idx 1 */
    ldr r0, =0xf000f0
    ldr r1, =0x1
    mcrr p0, #11, r0, r1, c4
    snap
    /* mcrr #11 c4 OE index clr idx 2 */
    ldr r0, =0xf000f0
    ldr r1, =0x2
    mcrr p0, #11, r0, r1, c4
    snap
    b 9f
    .ltorg
9:
    /* sio OUT_SET 100 */
    ldr r0, =0x100
    str r0, [r11, #0x18]
    snap
    /* sio OUT_CLR 100 */
    ldr r0, =0x100
    str r0, [r11, #0x20]
    snap
    /* sio OUT_XOR 3 */
    ldr r0, =0x3
    str r0, [r11, #0x28]
    snap
    /* sio HI_OUT_SET 8 */
    ldr r0, =0x8
    str r0, [r11, #0x1c]
    snap
    b 9f
    .ltorg
9:
    /* sio HI_OUT_CLR 8 */
    ldr r0, =0x8
    str r0, [r11, #0x24]
    snap
    /* sio HI_OUT_XOR 3 */
    ldr r0, =0x3
    str r0, [r11, #0x2c]
    snap
    /* sio OE_SET 100 */
    ldr r0, =0x100
    str r0, [r11, #0x38]
    snap
    /* sio OE_CLR 100 */
    ldr r0, =0x100
    str r0, [r11, #0x40]
    snap
    b 9f
    .ltorg
9:
    /* sio OE_XOR 3 */
    ldr r0, =0x3
    str r0, [r11, #0x48]
    snap
    /* sio HI_OE_SET 8 */
    ldr r0, =0x8
    str r0, [r11, #0x3c]
    snap
    /* sio HI_OE_CLR 8 */
    ldr r0, =0x8
    str r0, [r11, #0x44]
    snap
    /* sio HI_OE_XOR 3 */
    ldr r0, =0x3
    str r0, [r11, #0x4c]
    snap
    b 9f
    .ltorg
9:
    /* sio OUT=0 */
    ldr r0, =0x0
    str r0, [r11, #0x10]
    snap
    /* sio HI_OUT=0 */
    ldr r0, =0x0
    str r0, [r11, #0x14]
    snap
    /* sio OE=0 */
    ldr r0, =0x0
    str r0, [r11, #0x30]
    snap
    /* sio HI_OE=0 */
    ldr r0, =0x0
    str r0, [r11, #0x34]
    snap
    b 9f
    .ltorg
9:

    /* misc: CPUID, then the DCP RCMP read twice */
    ldr     r0, [r11, #0]
    str     r0, [r10], #4
    mrc     p4, #0, r0, c0, c0, #1
    str     r0, [r10], #4
    mrc     p4, #0, r0, c0, c0, #1
    str     r0, [r10], #4
    b       9f
    .ltorg
9:
    /* timing: DWT cycles for 32 of each access (TRCENA, CYCCNTENA) */
    ldr     r0, =0xE000EDFC
    ldr     r1, [r0]
    orr     r1, r1, #(1 << 24)
    str     r1, [r0]
    ldr     r0, =0xE0001000
    ldr     r1, [r0]
    orr     r1, r1, #1
    str     r1, [r0]
    ldr     r4, =0xE0001004
    mov     r7, #20
    mov     r8, #1
    timed   1, nop
    timed   32, mcrr p0, #4, r7, r8, c0
    timed   32, str r8, [r11, #0x18]
    timed   32, mrc p0, #0, r9, c0, c8
    timed   32, ldr r9, [r11, #4]
    timed   32, nop
    b       9f
    .ltorg
9:


.ifdef FAULTS
    /* faults: our vector table, UsageFault enabled; each case records
     * (faults taken, CFSR seen by the handler) */
    ldr     r0, =vectors
    ldr     r1, =0xE000ED08
    str     r0, [r1]
    ldr     r0, =0xE000ED24
    ldr     r1, [r0]
    orr     r1, r1, #(1 << 18)
    str     r1, [r0]
    dsb
    isb
    ldr     r6, =0xE000ED88

    /* CPACR CP0 off: mcr p0 */
    ldr     r1, =0xf00300
    str     r1, [r6]
    dsb
    isb
    mov     r8, #0
    mov     r9, #0
    mcr p0, #0, r0, c0, c0
    str     r8, [r10], #4
    str     r9, [r10], #4
    b       9f
    .ltorg
9:
    /* CPACR CP0 off: mrc p0 */
    ldr     r1, =0xf00300
    str     r1, [r6]
    dsb
    isb
    mov     r8, #0
    mov     r9, #0
    mrc p0, #0, r0, c0, c0
    str     r8, [r10], #4
    str     r9, [r10], #4
    b       9f
    .ltorg
9:
    /* CPACR CP4 off: mrc p4 RCMP */
    ldr     r1, =0xf00003
    str     r1, [r6]
    dsb
    isb
    mov     r8, #0
    mov     r9, #0
    mrc p4, #0, r0, c0, c0, #1
    str     r8, [r10], #4
    str     r9, [r10], #4
    b       9f
    .ltorg
9:
    /* p1 enabled in CPACR: mcr p1 */
    ldr     r1, =0xf0030f
    str     r1, [r6]
    dsb
    isb
    mov     r8, #0
    mov     r9, #0
    mcr p1, #0, r0, c0, c0
    str     r8, [r10], #4
    str     r9, [r10], #4
    b       9f
    .ltorg
9:
    /* p1 off in CPACR: mcr p1 */
    ldr     r1, =0xf00303
    str     r1, [r6]
    dsb
    isb
    mov     r8, #0
    mov     r9, #0
    mcr p1, #0, r0, c0, c0
    str     r8, [r10], #4
    str     r9, [r10], #4
    b       9f
    .ltorg
9:
    /* p2 enabled in CPACR: mcr p2 */
    ldr     r1, =0xf00333
    str     r1, [r6]
    dsb
    isb
    mov     r8, #0
    mov     r9, #0
    mcr p2, #0, r0, c0, c0
    str     r8, [r10], #4
    str     r9, [r10], #4
    b       9f
    .ltorg
9:
    /* p2 off in CPACR: mcr p2 */
    ldr     r1, =0xf00303
    str     r1, [r6]
    dsb
    isb
    mov     r8, #0
    mov     r9, #0
    mcr p2, #0, r0, c0, c0
    str     r8, [r10], #4
    str     r9, [r10], #4
    b       9f
    .ltorg
9:
    /* p3 enabled in CPACR: mcr p3 */
    ldr     r1, =0xf003c3
    str     r1, [r6]
    dsb
    isb
    mov     r8, #0
    mov     r9, #0
    mcr p3, #0, r0, c0, c0
    str     r8, [r10], #4
    str     r9, [r10], #4
    b       9f
    .ltorg
9:
    /* p3 off in CPACR: mcr p3 */
    ldr     r1, =0xf00303
    str     r1, [r6]
    dsb
    isb
    mov     r8, #0
    mov     r9, #0
    mcr p3, #0, r0, c0, c0
    str     r8, [r10], #4
    str     r9, [r10], #4
    b       9f
    .ltorg
9:
    /* p6 enabled in CPACR: mcr p6 */
    ldr     r1, =0xf03303
    str     r1, [r6]
    dsb
    isb
    mov     r8, #0
    mov     r9, #0
    mcr p6, #0, r0, c0, c0
    str     r8, [r10], #4
    str     r9, [r10], #4
    b       9f
    .ltorg
9:
    /* p6 off in CPACR: mcr p6 */
    ldr     r1, =0xf00303
    str     r1, [r6]
    dsb
    isb
    mov     r8, #0
    mov     r9, #0
    mcr p6, #0, r0, c0, c0
    str     r8, [r10], #4
    str     r9, [r10], #4
    b       9f
    .ltorg
9:
    ldr     r1, =0x00F00303
    str     r1, [r6]
    dsb
    isb
.endif

.ifdef CHECK
    /* compare every record flagged in `compare` with the silicon value in
     * `expected`; print the outcome through semihosting */
    ldr     r0, =OUT_BASE
    ldr     r1, =expected
    ldr     r2, =compare
    ldr     r3, =N_RECORDS
    mov     r9, #0
1:  ldrb    r4, [r2], #1
    ldr     r5, [r0], #4
    ldr     r6, [r1], #4
    cbz     r4, 2f
    cmp     r5, r6
    it      ne
    addne   r9, r9, #1
2:  subs    r3, r3, #1
    bne     1b
    ldr     r1, =ok_message
    cmp     r9, #0
    it      ne
    ldrne   r1, =fail_message
    movs    r0, #4
    bkpt    #0xab
.endif

    /* done: semihosting exit on gem5; on the Pico, OpenOCD sees the core halt */
    mov     r0, #0x18
    ldr     r1, =0x20026
    bkpt    #0xab
    b       .
    .ltorg

.ifdef FAULTS
    /* faults count in r8, CFSR in r9 (then cleared), and skip the 4-byte instruction */
    .thumb_func
fault_handler:
    add     r8, r8, #1
    ldr     r0, =0xE000ED28
    ldr     r1, [r0]
    orr     r9, r9, r1
    str     r1, [r0]
    ldr     r0, [sp, #24]
    add     r0, r0, #4
    str     r0, [sp, #24]
    bx      lr
    .ltorg

    .align  9
vectors:
    .word   0x20040000
    .word   Reset_Handler
    .word   fault_handler
    .word   fault_handler
    .word   fault_handler
    .word   fault_handler
    .word   fault_handler
.endif


.ifdef CHECK
    .equ N_RECORDS, 1317
    .align  2
expected:
    .word 0x00000000, 0x00000000, 0x02000040, 0x00000000, 0x0000301c, 0xc8000000, 0x00000000, 0x00000000
    .word 0x02000040, 0x00000000, 0x0000301c, 0xc8000000, 0x00000000, 0x00000000, 0x02000040, 0x00000000
    .word 0x0000303c, 0xc8000000, 0x00000000, 0x00000000, 0x02000040, 0x00000000, 0x0000303c, 0xc8000000
    .word 0x00000000, 0x00000000, 0x02000040, 0x00000000, 0x0000301c, 0xc8000000, 0x00000000, 0x00000000
    .word 0x02000040, 0x00000000, 0x0000301c, 0xc8000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000
    .word 0x0000303c, 0xc8000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x0000303c, 0xc8000000
    .word 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x0000303e, 0xc8000000, 0x00000000, 0x00000000
    .word 0x00000000, 0x00000000, 0x0000303e, 0xc8000000, 0xa5a5a5a5, 0x00000000, 0x00000000, 0x00000000
    .word 0x0000301c, 0xc8000000, 0xa5a5a5a5, 0x00000000, 0x00000000, 0x00000000, 0x0000301c, 0xc8000000
    .word 0xa5a5a5a5, 0xff00ffff, 0x00000000, 0x00000000, 0x0000303e, 0xc8000000, 0xa5a5a5a5, 0xff00ffff
    .word 0x00000000, 0x00000000, 0x0000303e, 0xc8000000, 0x5a5aa5a5, 0xff00ffff, 0x00000000, 0x00000000
    .word 0x0000301c, 0xc8000000, 0x5a5aa5a5, 0xff00ffff, 0x00000000, 0x00000000, 0x0000301c, 0xc8000000
    .word 0x5a5aa5af, 0xff00ffff, 0x00000000, 0x00000000, 0x0000303e, 0xc8000000, 0x5a5aa5af, 0xff00ffff
    .word 0x00000000, 0x00000000, 0x0000303e, 0xc8000000, 0x5a5aa50f, 0xff00ffff, 0x00000000, 0x00000000
    .word 0x0000303e, 0xc8000000, 0x5a5aa50f, 0xff00ffff, 0x00000000, 0x00000000, 0x0000303e, 0xc8000000
    .word 0x5a5aa50f, 0xff000000, 0x00000000, 0x00000000, 0x0000303e, 0xc8000000, 0x5a5aa50f, 0xff000000
    .word 0x00000000, 0x00000000, 0x0000303e, 0xc8000000, 0x5a5aa50f, 0xff000001, 0x00000000, 0x00000000
    .word 0x0000301c, 0xc8000000, 0x5a5aa50f, 0xff000001, 0x00000000, 0x00000000, 0x0000301c, 0xc8000000
    .word 0x5a5aa50f, 0xff000001, 0x00000000, 0x00000000, 0x0000303e, 0xc8000000, 0x5a5aa50f, 0xff000001
    .word 0x00000000, 0x00000000, 0x0000303e, 0xc8000000, 0x12345678, 0x00005555, 0x00000000, 0x00000000
    .word 0x0000303e, 0xc8000000, 0x12345678, 0x00005555, 0x00000000, 0x00000000, 0x0000303e, 0xc8000000
    .word 0x12345687, 0x00005556, 0x00000000, 0x00000000, 0x0000303e, 0xc8000000, 0x12345687, 0x00005556
    .word 0x00000000, 0x00000000, 0x0000303e, 0xc8000000, 0x1f345687, 0x00005556, 0x00000000, 0x00000000
    .word 0x0000303e, 0xc8000000, 0x1f345687, 0x00005556, 0x00000000, 0x00000000, 0x0000303e, 0xc8000000
    .word 0x1f345087, 0x00005552, 0x00000000, 0x00000000, 0x0000303e, 0xc8000000, 0x1f345087, 0x00005552
    .word 0x00000000, 0x00000000, 0x0000303e, 0xc8000000, 0x1f3450c7, 0x00005552, 0x00000000, 0x00000000
    .word 0x0000303e, 0xc8000000, 0x1f3450c7, 0x00005552, 0x00000000, 0x00000000, 0x0000303e, 0xc8000000
    .word 0x1f345087, 0x00005552, 0x00000000, 0x00000000, 0x0000303e, 0xc8000000, 0x1f345087, 0x00005552
    .word 0x00000000, 0x00000000, 0x0000303e, 0xc8000000, 0x1f345087, 0x00005552, 0x00000000, 0x00000000
    .word 0x0000303e, 0xc8000000, 0x1f345087, 0x00005552, 0x00000000, 0x00000000, 0x0000303e, 0xc8000000
    .word 0x1f345087, 0x00005552, 0x00000000, 0x00000000, 0x0000303e, 0xc8000000, 0x1f345087, 0x00005552
    .word 0x00000000, 0x00000000, 0x0000303e, 0xc8000000, 0x1f345087, 0x00005552, 0x00000000, 0x00000000
    .word 0x0000303c, 0xc8000000, 0x1f345087, 0x00005552, 0x00000000, 0x00000000, 0x0000303c, 0xc8000000
    .word 0x1f345087, 0x80005552, 0x00000000, 0x00000000, 0x0000303e, 0xc8000000, 0x1f345087, 0x80005552
    .word 0x00000000, 0x00000000, 0x0000303e, 0xc8000000, 0x1f345087, 0x80005552, 0x00000000, 0x00000000
    .word 0x0000301c, 0xc8000000, 0x1f345087, 0x80005552, 0x00000000, 0x00000000, 0x0000301c, 0xc8000000
    .word 0x1f3450c7, 0x80005552, 0x00000000, 0x00000000, 0x0000303e, 0xc8000000, 0x1f3450c7, 0x80005552
    .word 0x00000000, 0x00000000, 0x0000303e, 0xc8000000, 0x1f3452c7, 0x80005552, 0x00000000, 0x00000000
    .word 0x0000303e, 0xc8000000, 0x1f3452c7, 0x80005552, 0x00000000, 0x00000000, 0x0000303e, 0xc8000000
    .word 0x1f3452c7, 0x80005752, 0x00000000, 0x00000000, 0x0000301c, 0xc8000000, 0x1f3452c7, 0x80005752
    .word 0x00000000, 0x00000000, 0x0000301c, 0xc8000000, 0x1f3452c7, 0x80005752, 0x00000000, 0x00000000
    .word 0x0000303c, 0xc8000000, 0x1f3452c7, 0x80005752, 0x00000000, 0x00000000, 0x0000303c, 0xc8000000
    .word 0x1f3456c7, 0x80005752, 0x00000000, 0x00000000, 0x0000301c, 0xc8000000, 0x1f3456c7, 0x80005752
    .word 0x00000000, 0x00000000, 0x0000301c, 0xc8000000, 0x1f3456c7, 0x80005752, 0x00000000, 0x00000000
    .word 0x0000301c, 0xc8000000, 0x1f3456c7, 0x80005752, 0x00000000, 0x00000000, 0x0000301c, 0xc8000000
    .word 0x1f3456c7, 0x80005752, 0x00000000, 0x00000000, 0x0000303e, 0xc8000000, 0x1f3456c7, 0x80005752
    .word 0x00000000, 0x00000000, 0x0000303e, 0xc8000000, 0x1f3456c7, 0x80005752, 0x00000000, 0x00000000
    .word 0x0000303e, 0xc8000000, 0x1f3456c7, 0x80005752, 0x00000000, 0x00000000, 0x0000303e, 0xc8000000
    .word 0x1f3456c7, 0x80005752, 0x00000000, 0x00000000, 0x0000303e, 0xc8000000, 0x1f3456c7, 0x80005752
    .word 0x00000000, 0x00000000, 0x0000303e, 0xc8000000, 0x1f3454c7, 0x80005752, 0x00000000, 0x00000000
    .word 0x0000301c, 0xc8000000, 0x1f3454c7, 0x80005752, 0x00000000, 0x00000000, 0x0000301c, 0xc8000000
    .word 0x1f3454c7, 0x80005552, 0x00000000, 0x00000000, 0x0000303c, 0xc8000000, 0x1f3454c7, 0x80005552
    .word 0x00000000, 0x00000000, 0x0000303c, 0xc8000000, 0x1f3454c7, 0x80005552, 0x00000000, 0x00000000
    .word 0x0000301c, 0xc8000000, 0x1f3454c7, 0x80005552, 0x00000000, 0x00000000, 0x0000301c, 0xc8000000
    .word 0x1f3450c7, 0x80005552, 0x00000000, 0x00000000, 0x0000303c, 0xc8000000, 0x1f3450c7, 0x80005552
    .word 0x00000000, 0x00000000, 0x0000303c, 0xc8000000, 0x1f3450c7, 0xde00beef, 0x00000000, 0x00000000
    .word 0x0000303c, 0xc8000000, 0x1f3450c7, 0xde00beef, 0x00000000, 0x00000000, 0x0000303c, 0xc8000000
    .word 0x1f3450c7, 0xde00beef, 0x00000000, 0x00000000, 0x0000301c, 0xc8000000, 0x1f3450c7, 0xde00beef
    .word 0x00000000, 0x00000000, 0x0000301c, 0xc8000000, 0x1f3450c7, 0xde00beef, 0x00000000, 0x00000000
    .word 0x0000303c, 0xc8000000, 0x1f3450c7, 0xde00beef, 0x00000000, 0x00000000, 0x0000303c, 0xc8000000
    .word 0x1fc45037, 0xde00beef, 0x00000000, 0x00000000, 0x0000301c, 0xc8000000, 0x1fc45037, 0xde00beef
    .word 0x00000000, 0x00000000, 0x0000301c, 0xc8000000, 0x1f3450c7, 0xde00beef, 0x00000000, 0x00000000
    .word 0x0000301c, 0xc8000000, 0x1f3450c7, 0xde00beef, 0x00000000, 0x00000000, 0x0000301c, 0xc8000000
    .word 0x1fc45037, 0xde00beef, 0x00000000, 0x00000000, 0x0000303e, 0xc8000000, 0x1fc45037, 0xde00beef
    .word 0x00000000, 0x00000000, 0x0000303e, 0xc8000000, 0x1ff450f7, 0xde00beef, 0x00000000, 0x00000000
    .word 0x0000303c, 0xc8000000, 0x1ff450f7, 0xde00beef, 0x00000000, 0x00000000, 0x0000303c, 0xc8000000
    .word 0x1ff450f7, 0xde00beef, 0x00000000, 0x00000000, 0x0000303e, 0xc8000000, 0x1ff450f7, 0xde00beef
    .word 0x00000000, 0x00000000, 0x0000303e, 0xc8000000, 0x1ff450f7, 0xde00beef, 0x00000000, 0x00000000
    .word 0x0000303e, 0xc8000000, 0x1ff450f7, 0xde00beef, 0x00000000, 0x00000000, 0x0000303e, 0xc8000000
    .word 0x1f045007, 0xde00beef, 0x00000000, 0x00000000, 0x0000303e, 0xc8000000, 0x1f045007, 0xde00beef
    .word 0x00000000, 0x00000000, 0x0000303e, 0xc8000000, 0x1f045007, 0xde00beef, 0x00000000, 0x00000000
    .word 0x0000303e, 0xc8000000, 0x1f045007, 0xde00beef, 0x00000000, 0x00000000, 0x0000303e, 0xc8000000
    .word 0x1f045007, 0xde00beef, 0x00000000, 0x00000000, 0x0000303e, 0xc8000000, 0x1f045007, 0xde00beef
    .word 0x00000000, 0x00000000, 0x0000303e, 0xc8000000, 0x1f045007, 0xde00beef, 0xa5a5a5a5, 0x00000000
    .word 0x0000301c, 0xc8000000, 0x1f045007, 0xde00beef, 0xa5a5a5a5, 0x00000000, 0x0000301c, 0xc8000000
    .word 0x1f045007, 0xde00beef, 0xa5a5a5a5, 0xff00ffff, 0x0000303c, 0xc8000000, 0x1f045007, 0xde00beef
    .word 0xa5a5a5a5, 0xff00ffff, 0x0000303c, 0xc8000000, 0x1f045007, 0xde00beef, 0x5a5aa5a5, 0xff00ffff
    .word 0x0200303e, 0xc8000000, 0x1f045007, 0xde00beef, 0x5a5aa5a5, 0xff00ffff, 0x0200303e, 0xc8000000
    .word 0x1f045007, 0xde00beef, 0x5a5aa5af, 0xff00ffff, 0x0200303c, 0xc8000000, 0x1f045007, 0xde00beef
    .word 0x5a5aa5af, 0xff00ffff, 0x0200303c, 0xc8000000, 0x1f045007, 0xde00beef, 0x5a5aa50f, 0xff00ffff
    .word 0x0200303e, 0xc8000000, 0x1f045007, 0xde00beef, 0x5a5aa50f, 0xff00ffff, 0x0200303e, 0xc8000000
    .word 0x1f045007, 0xde00beef, 0x5a5aa50f, 0xff000000, 0x0200303c, 0xc8000000, 0x1f045007, 0xde00beef
    .word 0x5a5aa50f, 0xff000000, 0x0200303c, 0xc8000000, 0x1f045007, 0xde00beef, 0x5a5aa50f, 0xff000001
    .word 0x0200301c, 0xc8000000, 0x1f045007, 0xde00beef, 0x5a5aa50f, 0xff000001, 0x0200301c, 0xc8000000
    .word 0x1f045007, 0xde00beef, 0x5a5aa50f, 0xff000001, 0x0200303e, 0xc8000000, 0x1f045007, 0xde00beef
    .word 0x5a5aa50f, 0xff000001, 0x0200303e, 0xc8000000, 0x1f045007, 0xde00beef, 0x12345678, 0x00005555
    .word 0x0200303e, 0xc8000000, 0x1f045007, 0xde00beef, 0x12345678, 0x00005555, 0x0200303e, 0xc8000000
    .word 0x1f045007, 0xde00beef, 0x12345687, 0x00005556, 0x0200303e, 0xc8000000, 0x1f045007, 0xde00beef
    .word 0x12345687, 0x00005556, 0x0200303e, 0xc8000000, 0x1f045007, 0xde00beef, 0x1f345687, 0x00005556
    .word 0x0200303e, 0xc8000000, 0x1f045007, 0xde00beef, 0x1f345687, 0x00005556, 0x0200303e, 0xc8000000
    .word 0x1f045007, 0xde00beef, 0x1f345087, 0x00005552, 0x0200303e, 0xc8000000, 0x1f045007, 0xde00beef
    .word 0x1f345087, 0x00005552, 0x0200303e, 0xc8000000, 0x1f045007, 0xde00beef, 0x1f3450c7, 0x00005552
    .word 0x0200303e, 0xc8000000, 0x1f045007, 0xde00beef, 0x1f3450c7, 0x00005552, 0x0200303e, 0xc8000000
    .word 0x1f045007, 0xde00beef, 0x1f345087, 0x00005552, 0x0200303c, 0xc8000000, 0x1f045007, 0xde00beef
    .word 0x1f345087, 0x00005552, 0x0200303c, 0xc8000000, 0x1f045007, 0xde00beef, 0x1f345087, 0x00005552
    .word 0x0200303e, 0xc8000000, 0x1f045007, 0xde00beef, 0x1f345087, 0x00005552, 0x0200303e, 0xc8000000
    .word 0x1f045007, 0xde00beef, 0x1f345087, 0x00005552, 0x0200301c, 0xc8000000, 0x1f045007, 0xde00beef
    .word 0x1f345087, 0x00005552, 0x0200301c, 0xc8000000, 0x1f045007, 0xde00beef, 0x1f345087, 0x00005552
    .word 0x0200303c, 0xc8000000, 0x1f045007, 0xde00beef, 0x1f345087, 0x00005552, 0x0200303c, 0xc8000000
    .word 0x1f045007, 0xde00beef, 0x1f345087, 0x80005552, 0x0200301c, 0xc8000000, 0x1f045007, 0xde00beef
    .word 0x1f345087, 0x80005552, 0x0200301c, 0xc8000000, 0x1f045007, 0xde00beef, 0x1f345087, 0x80005552
    .word 0x0200303c, 0xc8000000, 0x1f045007, 0xde00beef, 0x1f345087, 0x80005552, 0x0200303c, 0xc8000000
    .word 0x1f045007, 0xde00beef, 0x1f3450c7, 0x80005552, 0x0200301c, 0xc8000000, 0x1f045007, 0xde00beef
    .word 0x1f3450c7, 0x80005552, 0x0200301c, 0xc8000000, 0x1f045007, 0xde00beef, 0x1f3452c7, 0x80005552
    .word 0x0200303e, 0xc8000000, 0x1f045007, 0xde00beef, 0x1f3452c7, 0x80005552, 0x0200303e, 0xc8000000
    .word 0x1f045007, 0xde00beef, 0x1f3452c7, 0x80005752, 0x0200303e, 0xc8000000, 0x1f045007, 0xde00beef
    .word 0x1f3452c7, 0x80005752, 0x0200303e, 0xc8000000, 0x1f045007, 0xde00beef, 0x1f3452c7, 0x80005752
    .word 0x0200303e, 0xc8000000, 0x1f045007, 0xde00beef, 0x1f3452c7, 0x80005752, 0x0200303e, 0xc8000000
    .word 0x1f045007, 0xde00beef, 0x1f3456c7, 0x80005752, 0x0200303e, 0xc8000000, 0x1f045007, 0xde00beef
    .word 0x1f3456c7, 0x80005752, 0x0200303e, 0xc8000000, 0x1f045007, 0xde00beef, 0x1f3456c7, 0x80005752
    .word 0x0200303e, 0xc8000000, 0x1f045007, 0xde00beef, 0x1f3456c7, 0x80005752, 0x0200303e, 0xc8000000
    .word 0x1f045007, 0xde00beef, 0x1f3456c7, 0x80005752, 0x0200303c, 0xc8000000, 0x1f045007, 0xde00beef
    .word 0x1f3456c7, 0x80005752, 0x0200303c, 0xc8000000, 0x1f045007, 0xde00beef, 0x1f3456c7, 0x80005752
    .word 0x0200301c, 0xc8000000, 0x1f045007, 0xde00beef, 0x1f3456c7, 0x80005752, 0x0200301c, 0xc8000000
    .word 0x1f045007, 0xde00beef, 0x1f3456c7, 0x80005752, 0x0200303c, 0xc8000000, 0x1f045007, 0xde00beef
    .word 0x1f3456c7, 0x80005752, 0x0200303c, 0xc8000000, 0x1f045007, 0xde00beef, 0x1f3454c7, 0x80005752
    .word 0x0200303e, 0xc8000000, 0x1f045007, 0xde00beef, 0x1f3454c7, 0x80005752, 0x0200303e, 0xc8000000
    .word 0x1f045007, 0xde00beef, 0x1f3454c7, 0x80005552, 0x0200303c, 0xc8000000, 0x1f045007, 0xde00beef
    .word 0x1f3454c7, 0x80005552, 0x0200303c, 0xc8000000, 0x1f045007, 0xde00beef, 0x1f3454c7, 0x80005552
    .word 0x0200303e, 0xc8000000, 0x1f045007, 0xde00beef, 0x1f3454c7, 0x80005552, 0x0200303e, 0xc8000000
    .word 0x1f045007, 0xde00beef, 0x1f3450c7, 0x80005552, 0x0200301c, 0xc8000000, 0x1f045007, 0xde00beef
    .word 0x1f3450c7, 0x80005552, 0x0200301c, 0xc8000000, 0x1f045007, 0xde00beef, 0x1f3450c7, 0xde00beef
    .word 0x0200303e, 0xc8000000, 0x1f045007, 0xde00beef, 0x1f3450c7, 0xde00beef, 0x0200303e, 0xc8000000
    .word 0x1f045007, 0xde00beef, 0x1f3450c7, 0xde00beef, 0x0200301c, 0xc8000000, 0x1f045007, 0xde00beef
    .word 0x1f3450c7, 0xde00beef, 0x0200301c, 0xc8000000, 0x1f045007, 0xde00beef, 0x1f3450c7, 0xde00beef
    .word 0x0200303e, 0xc8000000, 0x1f045007, 0xde00beef, 0x1f3450c7, 0xde00beef, 0x0200303e, 0xc8000000
    .word 0x1f045007, 0xde00beef, 0x1fc45037, 0xde00beef, 0x0200303e, 0xc8000000, 0x1f045007, 0xde00beef
    .word 0x1fc45037, 0xde00beef, 0x0200303e, 0xc8000000, 0x1f045007, 0xde00beef, 0x1f3450c7, 0xde00beef
    .word 0x0200303e, 0xc8000000, 0x1f045007, 0xde00beef, 0x1f3450c7, 0xde00beef, 0x0200303e, 0xc8000000
    .word 0x1f045007, 0xde00beef, 0x1fc45037, 0xde00beef, 0x0200303e, 0xc8000000, 0x1f045007, 0xde00beef
    .word 0x1fc45037, 0xde00beef, 0x0200303e, 0xc8000000, 0x1f045007, 0xde00beef, 0x1ff450f7, 0xde00beef
    .word 0x0200303e, 0xc8000000, 0x1f045007, 0xde00beef, 0x1ff450f7, 0xde00beef, 0x0200303e, 0xc8000000
    .word 0x1f045007, 0xde00beef, 0x1ff450f7, 0xde00beef, 0x0200303e, 0xc8000000, 0x1f045007, 0xde00beef
    .word 0x1ff450f7, 0xde00beef, 0x0200303e, 0xc8000000, 0x1f045007, 0xde00beef, 0x1ff450f7, 0xde00beef
    .word 0x0200301c, 0xc8000000, 0x1f045007, 0xde00beef, 0x1ff450f7, 0xde00beef, 0x0200301c, 0xc8000000
    .word 0x1f045007, 0xde00beef, 0x1f045007, 0xde00beef, 0x0200303c, 0xc8000000, 0x1f045007, 0xde00beef
    .word 0x1f045007, 0xde00beef, 0x0200303c, 0xc8000000, 0x1f045007, 0xde00beef, 0x1f045007, 0xde00beef
    .word 0x0200301c, 0xc8000000, 0x1f045007, 0xde00beef, 0x1f045007, 0xde00beef, 0x0200301c, 0xc8000000
    .word 0x1f045007, 0xde00beef, 0x1f045007, 0xde00beef, 0x0200303c, 0xc8000000, 0x1f045007, 0xde00beef
    .word 0x1f045007, 0xde00beef, 0x0200303c, 0xc8000000, 0x1f045107, 0xde00beef, 0x1f045007, 0xde00beef
    .word 0x0200303c, 0xc8000000, 0x1f045107, 0xde00beef, 0x1f045007, 0xde00beef, 0x0200303c, 0xc8000000
    .word 0x1f045007, 0xde00beef, 0x1f045007, 0xde00beef, 0x0200301c, 0xc8000000, 0x1f045007, 0xde00beef
    .word 0x1f045007, 0xde00beef, 0x0200301c, 0xc8000000, 0x1f045004, 0xde00beef, 0x1f045007, 0xde00beef
    .word 0x0200303e, 0xc8000000, 0x1f045004, 0xde00beef, 0x1f045007, 0xde00beef, 0x0200303e, 0xc8000000
    .word 0x1f045004, 0xde00beef, 0x1f045007, 0xde00beef, 0x0200303e, 0xc8000000, 0x1f045004, 0xde00beef
    .word 0x1f045007, 0xde00beef, 0x0200303e, 0xc8000000, 0x1f045004, 0xde00bee7, 0x1f045007, 0xde00beef
    .word 0x0200303c, 0xc8000000, 0x1f045004, 0xde00bee7, 0x1f045007, 0xde00beef, 0x0200303c, 0xc8000000
    .word 0x1f045004, 0xde00bee4, 0x1f045007, 0xde00beef, 0x0200301c, 0xc8000000, 0x1f045004, 0xde00bee4
    .word 0x1f045007, 0xde00beef, 0x0200301c, 0xc8000000, 0x1f045004, 0xde00bee4, 0x1f045107, 0xde00beef
    .word 0x0200303c, 0xc8000000, 0x1f045004, 0xde00bee4, 0x1f045107, 0xde00beef, 0x0200303c, 0xc8000000
    .word 0x1f045004, 0xde00bee4, 0x1f045007, 0xde00beef, 0x0200303e, 0xc8000000, 0x1f045004, 0xde00bee4
    .word 0x1f045007, 0xde00beef, 0x0200303e, 0xc8000000, 0x1f045004, 0xde00bee4, 0x1f045004, 0xde00beef
    .word 0x0200303e, 0xc8000000, 0x1f045004, 0xde00bee4, 0x1f045004, 0xde00beef, 0x0200303e, 0xc8000000
    .word 0x1f045004, 0xde00bee4, 0x1f045004, 0xde00beef, 0x0200303e, 0xc8000000, 0x1f045004, 0xde00bee4
    .word 0x1f045004, 0xde00beef, 0x0200303e, 0xc8000000, 0x1f045004, 0xde00bee4, 0x1f045004, 0xde00bee7
    .word 0x0200303e, 0xc8000000, 0x1f045004, 0xde00bee4, 0x1f045004, 0xde00bee7, 0x0200303e, 0xc8000000
    .word 0x1f045004, 0xde00bee4, 0x1f045004, 0xde00bee4, 0x0200301c, 0xc8000000, 0x1f045004, 0xde00bee4
    .word 0x1f045004, 0xde00bee4, 0x0200301c, 0xc8000000, 0x00000000, 0xde00bee4, 0x1f045004, 0xde00bee4
    .word 0x0000303e, 0xc8000000, 0x00000000, 0xde00bee4, 0x1f045004, 0xde00bee4, 0x0000303e, 0xc8000000
    .word 0x00000000, 0x00000000, 0x1f045004, 0xde00bee4, 0x0000303e, 0xc8000000, 0x00000000, 0x00000000
    .word 0x1f045004, 0xde00bee4, 0x0000303e, 0xc8000000, 0x00000000, 0x00000000, 0x00000000, 0xde00bee4
    .word 0x0000303e, 0xc8000000, 0x00000000, 0x00000000, 0x00000000, 0xde00bee4, 0x0000303e, 0xc8000000
    .word 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x0000303e, 0xc8000000, 0x00000000, 0x00000000
    .word 0x00000000, 0x00000000, 0x0000303e, 0xc8000000, 0x00000000, 0x00000000, 0x00000000, 0x00000001
    .word 0x00000022, 0x00000023, 0x00000022, 0x00000023, 0x00000011
compare:
    .byte 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    .byte 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1
    .byte 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0
    .byte 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1
    .byte 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1
    .byte 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0
    .byte 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1
    .byte 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1
    .byte 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0
    .byte 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1
    .byte 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1
    .byte 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0
    .byte 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1
    .byte 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1
    .byte 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0
    .byte 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1
    .byte 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1
    .byte 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0
    .byte 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1
    .byte 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1
    .byte 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0
    .byte 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1
    .byte 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1
    .byte 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0
    .byte 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1
    .byte 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1
    .byte 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0
    .byte 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1
    .byte 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1
    .byte 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0
    .byte 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1
    .byte 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1
    .byte 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0
    .byte 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1
    .byte 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1
    .byte 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0
    .byte 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1
    .byte 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1
    .byte 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0
    .byte 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1
    .byte 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 0
    .byte 0, 0, 0, 0, 0
ok_message: .asciz "RP2350_GPIOC_OK\n"
fail_message: .asciz "RP2350_GPIOC_FAIL\n"
.endif
