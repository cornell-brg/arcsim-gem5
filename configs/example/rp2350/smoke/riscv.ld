ENTRY(_start)
SECTIONS {
    . = 0x10000000;
    .text : { *(.text .text.*) *(.rodata .rodata.*) }
    . = 0x20080000;
    .sram_data : { *(.sram_data) }
    . = 0x20080080;
    .sram_text : { *(.sram_text) }
}
