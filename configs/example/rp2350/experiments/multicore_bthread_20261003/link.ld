ENTRY(_start)
MEMORY { SRAM (rwx) : ORIGIN = 0x20000000, LENGTH = 512K }
SECTIONS {
    .text : { KEEP(*(.text.start)) *(.text .text.*) } > SRAM
    .rodata : { *(.rodata .rodata.*) *(.srodata .srodata.*) } > SRAM
    .data : {
        *(.data .data.*) *(.sdata .sdata.*)
        __global_pointer$ = . + 0x800;
        KEEP(*(.boot_sync))
    } > SRAM
    .bss (NOLOAD) : {
        . = ALIGN(4);
        __bss_start = .;
        *(.bss .bss.*) *(.sbss .sbss.*) *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > SRAM
    .stacks (NOLOAD) : {
        . = ALIGN(16);
        __stacks_start = .;
        . += 4 * 4096;
        __stacks_end = .;
    } > SRAM
}
