/*
 * test_dwt_cyccnt.S: DWT cycle counter (CYCCNT) and DEMCR.TRCENA.
 *
 * Subtests:
 *   1  DWT_CTRL reads NUMCOMP = 4
 *   2  CYCCNT stays 0 while DEMCR.TRCENA is clear
 *   3  DEMCR.TRCENA reads back set
 *   4  CYCCNT counts after 100 nops (at least 25: a wide core folds nops)
 *   5  CYCCNT counts a plausible number (at most 5000: a slow memory
 *      model stretches them)
 *   6  CYCCNT freezes when CYCCNTENA is cleared
 *   7  DWT_CTRL reads CYCCNTENA clear, NUMCOMP still 4
 *   8  CYCCNT resumes from the frozen value when re-enabled
 *   9  CYCCNT freezes when TRCENA is cleared with CYCCNTENA set
 *  10  CYCCNT resumes when TRCENA is set again
 *  11  CYCCNT write while stopped reads back exactly
 *  12  CYCCNT write while running counts up from the written value
 *  13  CPICNT..FOLDCNT accept writes and read 0
 *  14  an unmodeled register (COMP0) reads 0
 *  15  halfword write to DWT_CTRL sets CYCCNTENA
 *  16  byte write to DWT_CTRL clears CYCCNTENA
 *
 * Result protocol: 0xCAFECAFE at 0x20000100 on pass, 0xDEADDEAD
 * with the subtest number at 0x20000104 and the observed value at
 * 0x20000108 on failure.
 */

    .syntax unified
    .cpu cortex-m4
    .thumb

    .equ TEST_RESULT,   0x20000100
    .equ TEST_SUBTEST,  0x20000104
    .equ TEST_DATA,     0x20000108
    .equ PASS_VAL,      0xCAFECAFE
    .equ FAIL_VAL,      0xDEADDEAD

    .equ DWT_BASE,      0xE0001000
    .equ DWT_CTRL,      0x00
    .equ DWT_CYCCNT,    0x04
    .equ DWT_CPICNT,    0x08
    .equ DWT_FOLDCNT,   0x18
    .equ DWT_COMP0,     0x20
    .equ DEMCR_ADDR,    0xE000EDFC
    .equ TRCENA,        (1 << 24)
    .equ CYCCNTENA,     1

    .section .isr_vector, "a", %progbits
    .align 2
    .globl _vector_table
_vector_table:
    .word _estack
    .word Reset_Handler

.macro fail_test subtest_num, obs_reg
    ldr r6, =TEST_SUBTEST
    movs r7, #\subtest_num
    str r7, [r6]
    ldr r6, =TEST_DATA
    str \obs_reg, [r6]
    ldr r6, =TEST_RESULT
    ldr r7, =FAIL_VAL
    str r7, [r6]
    b test_exit
.endm

.macro nops count
    .rept \count
    nop
    .endr
.endm

    .text
    .thumb_func
    .globl Reset_Handler
Reset_Handler:
    ldr r0, =DWT_BASE
    ldr r1, =DEMCR_ADDR

    /* 1: NUMCOMP = 4 at reset */
    ldr r2, [r0, #DWT_CTRL]
    lsrs r3, r2, #28
    cmp r3, #4
    beq 1f
    fail_test 1, r2
1:
    /* 2: no counting while TRCENA is clear */
    movs r2, #0
    str r2, [r1]
    str r2, [r0, #DWT_CYCCNT]
    movs r2, #CYCCNTENA
    str r2, [r0, #DWT_CTRL]
    nops 20
    ldr r2, [r0, #DWT_CYCCNT]
    cmp r2, #0
    beq 1f
    fail_test 2, r2
1:
    /* 3: TRCENA reads back */
    ldr r2, =TRCENA
    str r2, [r1]
    ldr r2, [r1]
    ldr r3, =TRCENA
    tst r2, r3
    bne 1f
    fail_test 3, r2
1:
    /* 4, 5: 100 nops count a plausible number of cycles */
    movs r2, #0
    str r2, [r0, #DWT_CYCCNT]
    nops 100
    ldr r4, [r0, #DWT_CYCCNT]
    cmp r4, #25
    bhs 1f
    fail_test 4, r4
1:
    ldr r3, =5000
    cmp r4, r3
    bls 1f
    fail_test 5, r4
1:
    /* 6: freeze on CYCCNTENA clear */
    movs r2, #0
    str r2, [r0, #DWT_CTRL]
    ldr r5, [r0, #DWT_CYCCNT]
    nops 20
    ldr r6, [r0, #DWT_CYCCNT]
    cmp r5, r6
    beq 1f
    fail_test 6, r6
1:
    /* 7: CTRL reads CYCCNTENA clear and NUMCOMP intact */
    ldr r2, [r0, #DWT_CTRL]
    ldr r3, =0x40000000
    cmp r2, r3
    beq 1f
    fail_test 7, r2
1:
    /* 8: resume from the frozen value */
    movs r2, #CYCCNTENA
    str r2, [r0, #DWT_CTRL]
    nops 20
    ldr r7, [r0, #DWT_CYCCNT]
    cmp r7, r6
    bhi 1f
    fail_test 8, r7
1:
    /* 9: freeze on TRCENA clear while CYCCNTENA stays set */
    movs r2, #0
    str r2, [r1]
    ldr r5, [r0, #DWT_CYCCNT]
    nops 20
    ldr r6, [r0, #DWT_CYCCNT]
    cmp r5, r6
    beq 1f
    fail_test 9, r6
1:
    /* 10: resume on TRCENA set */
    ldr r2, =TRCENA
    str r2, [r1]
    nops 20
    ldr r7, [r0, #DWT_CYCCNT]
    cmp r7, r6
    bhi 1f
    fail_test 10, r7
1:
    /* 11: write while stopped reads back exactly */
    movs r2, #0
    str r2, [r0, #DWT_CTRL]
    ldr r3, =0x12345678
    str r3, [r0, #DWT_CYCCNT]
    ldr r2, [r0, #DWT_CYCCNT]
    cmp r2, r3
    beq 1f
    fail_test 11, r2
1:
    /* 12: write while running counts up from the written value */
    movs r2, #CYCCNTENA
    str r2, [r0, #DWT_CTRL]
    ldr r3, =0x80000000
    str r3, [r0, #DWT_CYCCNT]
    nops 20
    ldr r2, [r0, #DWT_CYCCNT]
    cmp r2, r3
    bhi 1f
    fail_test 12, r2
1:
    /* 13: CPICNT..FOLDCNT accept writes, read 0 */
    movs r2, #0
    str r2, [r0, #DWT_CTRL]
    mov r3, #0xFFFFFFFF
    movs r5, #DWT_CPICNT
2:
    str r3, [r0, r5]
    ldr r2, [r0, r5]
    cmp r2, #0
    beq 1f
    fail_test 13, r5
1:
    adds r5, r5, #4
    cmp r5, #DWT_FOLDCNT
    bls 2b

    /* 14: COMP0 reads 0 */
    ldr r2, [r0, #DWT_COMP0]
    cmp r2, #0
    beq 1f
    fail_test 14, r2
1:
    /* 15: halfword write sets CYCCNTENA */
    movs r2, #CYCCNTENA
    strh r2, [r0, #DWT_CTRL]
    ldr r2, [r0, #DWT_CTRL]
    ldr r3, =0x40000001
    cmp r2, r3
    beq 1f
    fail_test 15, r2
1:
    /* 16: byte write clears CYCCNTENA */
    movs r2, #0
    strb r2, [r0, #DWT_CTRL]
    ldr r2, [r0, #DWT_CTRL]
    ldr r3, =0x40000000
    cmp r2, r3
    beq 1f
    fail_test 16, r2
1:

test_pass:
    ldr r0, =TEST_RESULT
    ldr r1, =PASS_VAL
    str r1, [r0]

test_exit:
    mov r0, #0x18
    ldr r1, =0x20026
    bkpt #0xAB
    b .
