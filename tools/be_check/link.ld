/* tools/be_check.py: a game in one block of RAM from 64 KB up (its stack set by the emulator) */
ENTRY(np_be_start)
SECTIONS {
  . = 0x10000;
  .text : { *(.text*) }
  .rodata : { *(.rodata*) }
  .ARM.exidx : { *(.ARM.exidx*) }
  . = ALIGN(16);
  .data : { *(.data*) }
  .bss : { *(.bss*) *(COMMON) }
  /DISCARD/ : { *(.comment) *(.note*) *(.ARM.attributes) *(.init_array*) *(.fini_array*) }
}
