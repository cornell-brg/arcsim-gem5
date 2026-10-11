/* RP2350 tests that run from SRAM, as loaded on the Pico through OpenOCD, and
   call code in flash: .flash is to be written there first. Its first two
   words are a vector table for gem5, which resets from the image's lowest
   address. */
ENTRY(Reset_Handler)
SECTIONS {
  . = 0x20010000; .text : { *(.text*) *(.rodata*) . = ALIGN(4); stubs = .; *(.table) *(.table_end) }
  . = 0x103f0000; .flash : { LONG(0x20020000) LONG(Reset_Handler) *(.flash) }
}
