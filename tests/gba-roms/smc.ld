ENTRY(_start)
OVL = 0x03006000;
SECTIONS
{
  . = 0x08000000;
  .text : { *(.text.start) *(.text*) *(.rodata*) }

  .iwram 0x03000400 : AT(LOADADDR(.text) + SIZEOF(.text))
  { __iwram_start = .; *(.iwram) . = ALIGN(4); __iwram_end = .; }
  __iwram_lma = LOADADDR(.iwram);

  .ewram 0x02010000 : AT(__iwram_lma + SIZEOF(.iwram))
  { __ewram_start = .; *(.ewram) . = ALIGN(4); __ewram_end = .; }
  __ewram_lma = LOADADDR(.ewram);

  OVERLAY 0x03006000 : AT(__ewram_lma + SIZEOF(.ewram))
  {
    .ovA { *(.ovA) }
    .ovB { *(.ovB) }
  }
  /DISCARD/ : { *(.ARM.attributes) }
}
