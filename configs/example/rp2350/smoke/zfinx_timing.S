/* Zfinx FP op timing on Hazard3 with arcsim-soc's FPU, one binary for the SoC's rtl_sim and gem5's
 * hazard3-proxy (--zfinx --xip-model soc-no-cache): each case is timed by mcycle reads on either side and printed as
 * name=<cycles, hex> through the testbench IO (0xc0000000 print, 0xc0000008 exit), after the empty case's
 * cycles are taken off. Reset at 0x10000040 as the SoC's RESET_VECTOR; traps park at 0x10000000.
 *   riscv32-unknown-elf-gcc -march=rv32imac_zicsr_zfinx -mabi=ilp32 -nostdlib -Wl,-Ttext=0x10000000 \
 *     -Wl,-e,_start zfinx_timing.S -o zfinx_timing.elf && riscv32-unknown-elf-objcopy -O binary ...  */
    .option norvc
    .equ IO, 0xc0000000

    .section .text, "ax", @progbits
    .global _trap
_trap:                          /* 0x10000000, MTVEC_INIT */
    j _trap
    .org 0x40
    .global _start
_start:                         /* 0x10000040, RESET_VECTOR */
    csrw mcountinhibit, zero
    li sp, 0x20040000
    li s0, IO
    li a1, 0x40490fdb           /* pi */
    li a2, 0x402df854           /* e */
    li a3, 0x3f800000           /* 1.0 */
    li a4, 0x40400000           /* 3.0 */

    .macro start
    csrr s1, mcycle
    .endm
    .macro stop name
    csrr s2, mcycle
    sub a0, s2, s1
    sub a0, a0, s3              /* less the empty case's */
    la a5, name_\name
    call print_case
    .endm

    li s3, 0
    csrr s1, mcycle
    csrr s2, mcycle
    sub s3, s2, s1              /* the empty case: the reads' own cycles */
    mv a0, s3
    la a5, name_empty
    call print_case

    start
    .rept 8
    fadd.s a0, a0, a1
    .endr
    stop add_dep
    start
    .rept 8
    fadd.s a5, a1, a2
    .endr
    stop add_ind
    start
    .rept 8
    fmul.s a0, a0, a3
    .endr
    stop mul_dep
    start
    .rept 8
    fmul.s a5, a1, a2
    .endr
    stop mul_ind
    start
    .rept 8
    fdiv.s a5, a1, a4
    .endr
    stop div
    start
    .rept 8
    fsqrt.s a5, a1
    .endr
    stop sqrt
    start
    .rept 8
    fcvt.w.s a5, a1
    .endr
    stop cvt_ws
    start
    .rept 8
    fcvt.s.w a5, a4
    .endr
    stop cvt_sw
    start
    .rept 8
    flt.s a5, a1, a2
    .endr
    stop lt
    start
    .rept 8
    fsgnj.s a5, a1, a2
    .endr
    stop sgnj
    start
    .rept 8
    fadd.s a5, a1, a2
    add a6, a5, a5
    .endr
    stop add_use
    start
    .rept 8
    add a5, a1, a2
    .endr
    stop alu

    /* vvadd's inner loop and its parts (bench examples/vvadd, Zfinx): data at sp - 1024 */
    addi t5, sp, -1024
    start
    .rept 8
    fadd.s a5, a1, a2
    sw a5, 0(t5)
    .endr
    stop add_store
    start
    .rept 8
    lw a4, 0(t5)
    addi t4, t4, 4
    fadd.s a5, a5, a4
    .endr
    stop load_add
    .irp pass, 1, 2
    mv a0, t5
    addi a3, t5, 32
    start
1:  lw a5, 0(a0)
    lw a4, 64(a0)
    addi a0, a0, 4
    fadd.s a5, a5, a4
    sw a5, 124(a0)
    bne a0, a3, 1b
    stop vvadd_loop\pass
    mv a0, t5
    addi a3, t5, 32
    start
1:  lw a5, 0(a0)
    lw a4, 64(a0)
    addi a0, a0, 4
    add a5, a5, a4
    sw a5, 124(a0)
    bne a0, a3, 1b
    stop int_loop\pass
    mv a0, t5
    addi a3, t5, 32
    start
1:  lw a5, 0(a0)
    lw a4, 64(a0)
    addi a0, a0, 4
    fadd.s a5, a5, a4
    bne a0, a3, 1b
    stop vv_nostore\pass
    mv a0, t5
    addi a3, t5, 32
    start
1:  lw a5, 0(a0)
    lw a4, 64(a0)
    addi a0, a0, 4
    fadd.s a6, a1, a2
    sw a5, 124(a0)
    bne a0, a3, 1b
    stop vv_indep\pass
    mv a0, t5
    addi a3, t5, 32
    start
1:  lw a5, 0(a0)
    lw a4, 64(a0)
    addi a0, a0, 4
    fadd.s a5, a5, a4
    sw a6, 124(a0)
    bne a0, a3, 1b
    stop vv_storeother\pass
    mv a0, t5
    start
    .rept 8
    lw a5, 0(a0)
    lw a4, 64(a0)
    addi a0, a0, 4
    fadd.s a5, a5, a4
    sw a5, 124(a0)
    .endr
    stop vv_unrolled\pass
    .endr

    li t0, 0
    sw t0, 8(s0)                /* exit 0 */
1:  j 1b

/* print_case: "<name at a5>=<a0 hex>\n" */
print_case:
    lbu t0, 0(a5)
    beqz t0, 2f
    sw t0, 0(s0)
    addi a5, a5, 1
    j print_case
2:  li t0, '='
    sw t0, 0(s0)
    li t1, 28
3:  srl t2, a0, t1
    andi t2, t2, 15
    addi t2, t2, '0'
    li t3, '9'
    ble t2, t3, 4f
    addi t2, t2, 'a' - '0' - 10
4:  sw t2, 0(s0)
    addi t1, t1, -4
    bgez t1, 3b
    li t0, '\n'
    sw t0, 0(s0)
    ret

    .section .rodata
name_empty: .asciz "empty"
name_add_dep: .asciz "fadd x8 dependent"
name_add_ind: .asciz "fadd x8 independent"
name_mul_dep: .asciz "fmul x8 dependent"
name_mul_ind: .asciz "fmul x8 independent"
name_div: .asciz "fdiv x8"
name_sqrt: .asciz "fsqrt x8"
name_cvt_ws: .asciz "fcvt.w.s x8"
name_cvt_sw: .asciz "fcvt.s.w x8"
name_lt: .asciz "flt x8"
name_sgnj: .asciz "fsgnj x8"
name_add_use: .asciz "fadd+add x8"
name_alu: .asciz "add x8"
name_add_store: .asciz "fadd+sw x8"
name_load_add: .asciz "lw+addi+fadd x8"
name_vvadd_loop1: .asciz "vvadd loop x8, first"
name_int_loop1: .asciz "vvadd loop with add x8, first"
name_vvadd_loop2: .asciz "vvadd loop x8, again"
name_int_loop2: .asciz "vvadd loop with add x8, again"
name_vv_nostore1: .asciz "vvadd loop without sw x8, first"
name_vv_indep1: .asciz "vvadd loop, fadd on other regs x8, first"
name_vv_storeother1: .asciz "vvadd loop, sw of another reg x8, first"
name_vv_unrolled1: .asciz "vvadd body unrolled x8, first"
name_vv_nostore2: .asciz "vvadd loop without sw x8, again"
name_vv_indep2: .asciz "vvadd loop, fadd on other regs x8, again"
name_vv_storeother2: .asciz "vvadd loop, sw of another reg x8, again"
name_vv_unrolled2: .asciz "vvadd body unrolled x8, again"
