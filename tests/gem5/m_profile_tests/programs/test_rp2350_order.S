/*
 * Test: GPIO coprocessor instructions in program order with SIO accesses
 *
 * On the RP2350 board's Arm proxy (a Minor CPU), with the SIO made slow
 * (configs/run_rp2350_slow_sio.py) so that a reordering would show:
 *   1. SIO store to GPIO_OUT, then MRC of OUT at once: reads the store
 *   2. MCR to OUT, then SIO load of GPIO_OUT at once: reads the MCR
 *   3. SIO XOR store, then MCR put at once: the put is the final value
 *   4. MCR put, then SIO XOR store at once: the XOR applies to the put
 *   5. and 6. cases 1 and 2 with eight SRAM stores queued ahead
 *
 * Prints RP2350_ORDER_OK, or RP2350_ORDER_FAIL with the failing cases as a
 * bitmask stored at 0x20004000.
 */
    .syntax unified
    .cpu cortex-m33
    .thumb

    .equ SIO, 0xd0000000
    .equ GPIO_OUT, 0x10
    .equ GPIO_OUT_XOR, 0x28

    .section .text
    .align  2
    /* initial SP and reset vector */
    .word   0x20040000
    .word   Reset_Handler
    .thumb_func
    .globl  Reset_Handler
Reset_Handler:
    /* CPACR: CP0 (GPIO), CP4, CP10/CP11 full access */
    ldr     r0, =0xE000ED88
    ldr     r1, =0x00F00303
    str     r1, [r0]
    dsb
    isb
    ldr     r11, =SIO
    ldr     r10, =0x20030000
    mov     r9, #0

    /* 1: SIO store, then MRC */
    ldr     r1, =0x11111111
    str     r1, [r11, #GPIO_OUT]
    mrc     p0, #0, r2, c0, c0
    cmp     r2, r1
    it      ne
    orrne   r9, r9, #(1 << 1)

    /* 2: MCR, then SIO load */
    ldr     r1, =0x22222222
    mcr     p0, #0, r1, c0, c0
    ldr     r2, [r11, #GPIO_OUT]
    cmp     r2, r1
    it      ne
    orrne   r9, r9, #(1 << 2)

    /* 3: SIO XOR store, then MCR put: the put wins */
    ldr     r1, =0x0000ffff
    ldr     r3, =0x33333333
    str     r1, [r11, #GPIO_OUT_XOR]
    mcr     p0, #0, r3, c0, c0
    ldr     r2, [r11, #GPIO_OUT]
    cmp     r2, r3
    it      ne
    orrne   r9, r9, #(1 << 3)

    /* 4: MCR put, then SIO XOR store: the XOR applies to the put */
    ldr     r3, =0x44444444
    mcr     p0, #0, r3, c0, c0
    str     r1, [r11, #GPIO_OUT_XOR]
    mrc     p0, #0, r2, c0, c0
    eor     r3, r3, r1
    cmp     r2, r3
    it      ne
    orrne   r9, r9, #(1 << 4)

    /* 5: SRAM stores queued, then SIO store, then MRC */
    ldr     r1, =0x55555555
    stmia   r10, {r0-r7}
    str     r1, [r11, #GPIO_OUT]
    mrc     p0, #0, r2, c0, c0
    cmp     r2, r1
    it      ne
    orrne   r9, r9, #(1 << 5)

    /* 6: SRAM stores queued, then MCR, then SIO load */
    ldr     r1, =0x66666666
    stmia   r10, {r0-r7}
    mcr     p0, #0, r1, c0, c0
    ldr     r2, [r11, #GPIO_OUT]
    cmp     r2, r1
    it      ne
    orrne   r9, r9, #(1 << 6)

    ldr     r0, =0x20004000
    str     r9, [r0]
    ldr     r1, =ok_message
    cmp     r9, #0
    it      ne
    ldrne   r1, =fail_message
    movs    r0, #4
    bkpt    #0xab
    mov     r0, #0x18
    ldr     r1, =0x20026
    bkpt    #0xab
    b       .
    .ltorg

ok_message:   .asciz "RP2350_ORDER_OK\n"
fail_message: .asciz "RP2350_ORDER_FAIL\n"
