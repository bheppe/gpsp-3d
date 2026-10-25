ENTRY(_start)
SECTIONS {
  . = 0x08000000;
  .text : { *(.text.start) *(.text*) *(.rodata*) }
  . = 0x03000000;
  .data : AT(LOADADDR(.text) + SIZEOF(.text)) { *(.data*) }
  .bss : { *(.bss*) *(COMMON) }
}
