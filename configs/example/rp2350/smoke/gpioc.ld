/* gpioc.S: everything in SRAM, as loaded on the Pico through OpenOCD. */
ENTRY(Reset_Handler)
SECTIONS { . = 0x20010000; .text : { *(.text*) *(.rodata*) } }
