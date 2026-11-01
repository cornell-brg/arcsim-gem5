ENTRY(reset0)
SECTIONS {
    . = 0x10000000;
    .vectors : { KEEP(*(.vectors)) }
    .text : { *(.text .text.*) *(.rodata .rodata.*) }
    .shared 0x20000000 (NOLOAD) : { *(.shared) }
}
