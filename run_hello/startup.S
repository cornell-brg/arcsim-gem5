/*
 * startup.S — Minimal Cortex-M4 startup for bare-metal firmware on gem5.
 *
 * Provides:
 *   - Vector table with all 16 core exceptions (no external IRQs used)
 *   - Reset_Handler: copies .data from flash to SRAM, zeros .bss, calls main()
 *   - Default_Handler: infinite loop for any unhandled/unexpected exception
 *
 * Modelled on tests/gem5/m_profile_tests/programs/freertos/startup_freertos.S
 * in the gem5 tree, trimmed down since this program doesn't use interrupts.
 */

    .syntax unified
    .cpu cortex-m4
    .thumb

/* =========================================================================
 * Vector table
 * ========================================================================= */
    .section .isr_vector, "a", %progbits
    .align 2
    .globl __isr_vector
__isr_vector:
    .word   _estack              /* 0:  Initial MSP */
    .word   Reset_Handler        /* 1:  Reset */
    .word   Default_Handler      /* 2:  NMI */
    .word   Default_Handler      /* 3:  HardFault */
    .word   Default_Handler      /* 4:  MemManage */
    .word   Default_Handler      /* 5:  BusFault */
    .word   Default_Handler      /* 6:  UsageFault */
    .word   0                    /* 7:  Reserved */
    .word   0                    /* 8:  Reserved */
    .word   0                    /* 9:  Reserved */
    .word   0                    /* 10: Reserved */
    .word   Default_Handler      /* 11: SVCall */
    .word   Default_Handler      /* 12: DebugMonitor */
    .word   0                    /* 13: Reserved */
    .word   Default_Handler      /* 14: PendSV */
    .word   Default_Handler      /* 15: SysTick */
    /* No external IRQ vectors — this program never enables one. */

/* =========================================================================
 * Reset_Handler — copy .data, zero .bss, call main()
 * ========================================================================= */
    .section .text.Reset_Handler
    .thumb_func
    .globl Reset_Handler
Reset_Handler:
    /* Copy .data section from flash (LMA) to SRAM (VMA). */
    ldr     r0, =_sidata
    ldr     r1, =_sdata
    ldr     r2, =_edata
copy_data_loop:
    cmp     r1, r2
    bge     copy_data_done
    ldr     r3, [r0], #4
    str     r3, [r1], #4
    b       copy_data_loop
copy_data_done:

    /* Zero .bss section. */
    ldr     r1, =_sbss
    ldr     r2, =_ebss
    movs    r3, #0
zero_bss_loop:
    cmp     r1, r2
    bge     zero_bss_done
    str     r3, [r1], #4
    b       zero_bss_loop
zero_bss_done:

    bl      main
    /* main() should not return (it exits via semihosting), but if it
     * does, fall into an infinite loop rather than executing garbage. */
    b       .

    .thumb_func
    .globl Default_Handler
Default_Handler:
    b       .

    .end
