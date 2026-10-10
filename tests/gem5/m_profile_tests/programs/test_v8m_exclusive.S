/*
 * Test: ARMv8-M exclusive, load-acquire and store-release instructions
 *
 * LDREX/LDREXB/LDREXH, STREX/STREXB/STREXH, CLREX, and the ARMv8-M additions
 * LDA/LDAB/LDAH, STL/STLB/STLH, LDAEX/LDAEXB/LDAEXH, STLEX/STLEXB/STLEXH
 * (DDI0553 C2.4). The Pico SDK builds its spin locks on the RP2350 from
 * LDAEXB, STREXB and STLB, so its mutexes, malloc and printf all run them.
 *
 * Each subtest checks the loaded value (zero-extended), the bytes a store
 * changes, the status a store-exclusive returns, and that a failed
 * store-exclusive leaves memory alone.
 *
 * Runs on the Cortex-M33 release (run_m4_test.py --release m33) on the
 * atomic, timing and Minor CPUs. It also links with rp2350_sram.ld, to run
 * from SRAM on a Pico 2 (see the README).
 *
 * Result at 0x20000100:
 *   0xCAFECAFE = all subtests passed
 *   0xDEADDEAD = a subtest failed; its number is at 0x20000104, plus 0x100
 *                if a fault was taken while it ran
 */

    .syntax unified
    .cpu cortex-m33
    .thumb

    .equ TEST_RESULT,  0x20000100
    .equ TEST_SUBTEST, 0x20000104
    .equ PASS_VAL,     0xCAFECAFE
    .equ FAIL_VAL,     0xDEADDEAD
    .equ STACK_TOP,    0x20020000
    .equ BUF,          0x20000200
    .equ SCB_VTOR,     0xE000ED08

    .section .isr_vector, "a", %progbits
    .align 7
__isr_vector:
    .word   STACK_TOP           /* Initial SP */
    .word   Reset_Handler       /* Reset */
    .word   Fault_Handler       /* NMI */
    .word   Fault_Handler       /* HardFault */
    .word   Fault_Handler       /* MemManage */
    .word   Fault_Handler       /* BusFault */
    .word   Fault_Handler       /* UsageFault */
    .word   Fault_Handler       /* SecureFault */
    .word   0                   /* Reserved */
    .word   0                   /* Reserved */
    .word   0                   /* Reserved */
    .word   SVC_Handler         /* SVCall */
    .word   0                   /* Debug Monitor */
    .word   0                   /* Reserved */
    .word   Fault_Handler       /* PendSV */
    .word   Fault_Handler       /* SysTick */

    .section .text

    /* Fail the current subtest unless \reg holds \value. Uses r6. */
    .macro expect reg, value
    ldr     r6, =\value
    cmp     \reg, r6
    bne     fail
    .endm

    /* Fail the current subtest unless the word at [r4] is \value. Uses r0, r6. */
    .macro expect_word value
    ldr     r0, [r4]
    expect  r0, \value
    .endm

    /* Store \value to the word at [r4]. Uses r0. */
    .macro set_word value
    ldr     r0, =\value
    str     r0, [r4]
    .endm

    .thumb_func
    .globl Reset_Handler
Reset_Handler:
    ldr     r0, =STACK_TOP
    mov     sp, r0
    ldr     r0, =__isr_vector
    ldr     r1, =SCB_VTOR
    str     r0, [r1]
    cpsie   i
    ldr     r4, =BUF
    clrex

    /* 1: LDAEXB then STREXB, the Pico SDK's spin lock acquire, with the
     * registers its mutex_enter_blocking uses */
    movs    r7, #1
    set_word 0x818283F4
    mov     r2, r4
    mvn     ip, #0
    ldaexb  ip, [r2]
    expect  ip, 0xF4
    ldr     r1, =0x12345601
    strexb  ip, r1, [r2]
    expect  ip, 0
    expect_word 0x81828301

    /* 2: STLB, the Pico SDK's spin lock release */
    movs    r7, #2
    ldr     r1, =0xAABBCC00
    stlb    r1, [r2]
    expect_word 0x81828300

    /* 3: LDAEXB then STLEXB */
    movs    r7, #3
    set_word 0x818283F4
    mvn     r0, #0
    ldaexb  r0, [r4]
    expect  r0, 0xF4
    ldr     r2, =0x123456A5
    mvn     r1, #0
    stlexb  r1, r2, [r4]
    expect  r1, 0
    expect_word 0x818283A5

    /* 4: LDAEXH then STLEXH */
    movs    r7, #4
    set_word 0x8182F3F4
    mvn     r0, #0
    ldaexh  r0, [r4]
    expect  r0, 0xF3F4
    ldr     r2, =0x1234BEEF
    mvn     r1, #0
    stlexh  r1, r2, [r4]
    expect  r1, 0
    expect_word 0x8182BEEF

    /* 5: LDAEX then STLEX */
    movs    r7, #5
    movs    r0, #0
    ldaex   r0, [r4]
    expect  r0, 0x8182BEEF
    ldr     r2, =0x600DF00D
    mvn     r1, #0
    stlex   r1, r2, [r4]
    expect  r1, 0
    expect_word 0x600DF00D

    /* 6: LDREXB then STREXB, with high registers */
    movs    r7, #6
    set_word 0x818283F4
    mvn     lr, #0
    ldrexb  lr, [r4]
    expect  lr, 0xF4
    ldr     r8, =0x123456A5
    mvn     r9, #0
    strexb  r9, r8, [r4]
    expect  r9, 0
    expect_word 0x818283A5

    /* 7: LDREXH then STREXH */
    movs    r7, #7
    set_word 0x8182F3F4
    mvn     r0, #0
    ldrexh  r0, [r4]
    expect  r0, 0xF3F4
    ldr     r2, =0x1234BEEF
    mvn     r1, #0
    strexh  r1, r2, [r4]
    expect  r1, 0
    expect_word 0x8182BEEF

    /* 8: LDREX then STREX, with an offset */
    movs    r7, #8
    ldr     r0, =0x0BADF00D
    str     r0, [r4, #8]
    sub     r5, r4, #8
    movs    r0, #0
    ldrex   r0, [r5, #16]
    expect  r0, 0x0BADF00D
    ldr     r2, =0x600DF00D
    mvn     r1, #0
    strex   r1, r2, [r5, #16]
    expect  r1, 0
    ldr     r0, [r4, #8]
    expect  r0, 0x600DF00D
    expect_word 0x8182BEEF

    /* 9: LDAB, LDAH and LDA load zero-extended */
    movs    r7, #9
    set_word 0x8182F3F4
    mvn     r0, #0
    ldab    r0, [r4]
    expect  r0, 0xF4
    mvn     r0, #0
    ldah    r0, [r4]
    expect  r0, 0xF3F4
    movs    r0, #0
    lda     r0, [r4]
    expect  r0, 0x8182F3F4

    /* 10: STLB, STLH and STL store their own width */
    movs    r7, #10
    ldr     r1, =0x11223344
    set_word 0xFFFFFFFF
    stlb    r1, [r4]
    expect_word 0xFFFFFF44
    set_word 0xFFFFFFFF
    stlh    r1, [r4]
    expect_word 0xFFFF3344
    set_word 0xFFFFFFFF
    stl     r1, [r4]
    expect_word 0x11223344

    /* 11: with the monitor open, as a successful store-exclusive leaves
     * it, every store-exclusive returns 1 and stores nothing */
    movs    r7, #11
    set_word 0x55555555
    ldaex   r0, [r4]
    stlex   r1, r0, [r4]
    expect  r1, 0
    ldr     r2, =0xAAAAAAAA
    movs    r1, #0
    stlexb  r1, r2, [r4]
    expect  r1, 1
    movs    r1, #0
    stlexh  r1, r2, [r4]
    expect  r1, 1
    movs    r1, #0
    stlex   r1, r2, [r4]
    expect  r1, 1
    movs    r1, #0
    strexb  r1, r2, [r4]
    expect  r1, 1
    movs    r1, #0
    strexh  r1, r2, [r4]
    expect  r1, 1
    movs    r1, #0
    strex   r1, r2, [r4]
    expect  r1, 1
    expect_word 0x55555555

    /* 12: CLREX opens the monitor after each exclusive load */
    movs    r7, #12
    ldaex   r0, [r4]
    clrex
    movs    r1, #0
    stlex   r1, r2, [r4]
    expect  r1, 1
    ldaexb  r0, [r4]
    clrex
    movs    r1, #0
    strexb  r1, r2, [r4]
    expect  r1, 1
    ldaexh  r0, [r4]
    clrex
    movs    r1, #0
    stlexh  r1, r2, [r4]
    expect  r1, 1
    ldrex   r0, [r4]
    clrex
    movs    r1, #0
    strex   r1, r2, [r4]
    expect  r1, 1
    expect_word 0x55555555

    /* 13: a load-acquire of the marked address and a store-release to
     * another address leave the monitor set */
    movs    r7, #13
    ldaex   r0, [r4]
    lda     r1, [r4]
    ldab    r1, [r4]
    add     r5, r4, #0x100
    stl     r2, [r5]
    mvn     r1, #0
    stlex   r1, r2, [r4]
    expect  r1, 0
    expect_word 0xAAAAAAAA

    /* 14: an exclusive load whose IT condition fails loads nothing and
     * does not set the monitor */
    movs    r7, #14
    set_word 0x55555555
    ldr     r0, =0x0000CAFE
    cmp     r4, r4
    it      ne
    ldaexbne r0, [r4]
    expect  r0, 0x0000CAFE
    movs    r1, #0
    stlexb  r1, r2, [r4]
    expect  r1, 1
    expect_word 0x55555555

    /* 15: a store-exclusive whose IT condition fails stores nothing,
     * leaves its status register and keeps the monitor */
    movs    r7, #15
    ldaex   r0, [r4]
    ldr     r1, =0x0000CAFE
    cmp     r4, r4
    it      ne
    stlexne r1, r2, [r4]
    expect  r1, 0x0000CAFE
    expect_word 0x55555555
    cmp     r4, r4
    it      eq
    stlexeq r1, r2, [r4]
    expect  r1, 0
    expect_word 0xAAAAAAAA

    /* 16: a store-release whose IT condition fails stores nothing */
    movs    r7, #16
    set_word 0x55555555
    cmp     r4, r4
    itt     ne
    stlbne  r2, [r4]
    stlne   r2, [r4]
    expect_word 0x55555555

    /* 17: an exception between the load and the store opens the monitor
     * (DDI0553 B8.2: exception entry and return clear the local monitor) */
    movs    r7, #17
    ldaex   r0, [r4]
    svc     #0
    movs    r1, #0
    stlex   r1, r2, [r4]
    expect  r1, 1
    expect_word 0x55555555

    /* 18: a spin lock taken, found held, released and taken again, as
     * the Pico SDK's spin_lock_blocking and spin_unlock do it */
    movs    r7, #18
    set_word 0
    bl      try_lock
    expect  r0, 1
    bl      try_lock
    expect  r0, 0
    movs    r1, #0
    stlb    r1, [r4]
    bl      try_lock
    expect  r0, 1
    expect_word 1

    ldr     r0, =PASS_VAL
    ldr     r1, =TEST_RESULT
    str     r0, [r1]
    b       done

fail:
    ldr     r0, =FAIL_VAL
    ldr     r1, =TEST_RESULT
    str     r0, [r1]
    ldr     r1, =TEST_SUBTEST
    str     r7, [r1]

done:
    /* Exit via ARM semihosting SYS_EXIT */
    mov     r0, #0x18
    ldr     r1, =0x20026
    bkpt    #0xab
    b       .

    /* r0 = 1 if the byte lock at [r4] was free and is now taken, else 0 */
    .thumb_func
try_lock:
    ldaexb  r3, [r4]
    cbnz    r3, 1f
    movs    r1, #1
    strexb  r3, r1, [r4]
    cmp     r3, #0
    bne     try_lock
    dmb
    movs    r0, #1
    bx      lr
1:  clrex
    movs    r0, #0
    bx      lr

    .thumb_func
SVC_Handler:
    bx      lr

    .thumb_func
Fault_Handler:
    orr     r7, r7, #0x100
    b       fail

    .ltorg
