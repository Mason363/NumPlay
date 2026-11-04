/* A fx-CG add-in: its code and read-only data where the calculator maps the .g3a (after the header), its data in
   the add-in RAM (512 KB at 0x08100000 shared with the stack, which grows down from the end: 128 KB left for it).
   From libfxcg's toolchain/prizm.x, with room for bigger games. */
OUTPUT_ARCH(sh3)
ENTRY(initialize)
MEMORY {
  rom (rx) : o = 0x00300000, l = 0x1F8FFC /* 2 MiB for the whole .g3a: its 0x7000-byte header and checksum too */
  ram (rwx) : o = 0x08100004, l = 384k
  ilram (rwx) : o = 0xE5200000, l = 4k
}
SECTIONS {
  .text : { *(.pretext) *(.text) *(.text.*) } > rom
  .ilram : { _ilramld = LOADADDR(.ilram); _silram = .; *(.ilram) *(.ilram.*) _eilram = .; } > ilram AT> rom
  .rodata : { *(.rodata) *(.rodata.*) } > rom
  .data : ALIGN(4) { _datald = LOADADDR(.data); _sdata = .; *(.data) *(.data.*); _edata = .; } > ram AT> rom
  .bss : ALIGN(4) { _bbss = .; *(.bss) *(.bss*) *(COMMON) _ebss = .; } > ram
  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
