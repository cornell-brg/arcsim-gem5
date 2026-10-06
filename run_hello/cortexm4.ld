/*
 * Linker script for a minimal Cortex-M4 program on gem5.
 *
 * Memory map matches STM32F405Platform (gem5.prebuilt.cortexm.platforms):
 * Flash at 0x08000000, SRAM at 0x20000000.  The vector table (.isr_vector)
 * must be the very first thing in flash — MProfileReset reads the initial
 * SP from VTOR+0 and the Reset_Handler address from VTOR+4, and VTOR
 * defaults to the flash base address.
 *
 * Copied from tests/gem5/m_profile_tests/programs/cortexm4.ld in the gem5
 * tree, with the .data/.bss linker symbols (_sidata/_sdata/_edata/_sbss/
 * _ebss) that startup.S needs added explicitly.
 */

ENTRY(Reset_Handler)

_estack = 0x20020000;  /* Top of SRAM1 (128KB from 0x20000000) */

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 1M
    SRAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
}

SECTIONS
{
    /* Vector table — must be first in flash */
    .isr_vector :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } > FLASH

    /* Code */
    .text :
    {
        . = ALIGN(4);
        *(.text)
        *(.text*)
        . = ALIGN(4);
    } > FLASH

    /* Read-only data */
    .rodata :
    {
        . = ALIGN(4);
        *(.rodata)
        *(.rodata*)
        . = ALIGN(4);
    } > FLASH

    /* Initialized data (copied from flash to SRAM at startup) */
    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data)
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > SRAM AT> FLASH
    _sidata = LOADADDR(.data);

    /* Zero-initialized data */
    .bss :
    {
        . = ALIGN(4);
        _sbss = .;
        *(.bss)
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > SRAM
}
