/* ce_emu's flash image: the boot code at 0, the OS's jump table at 0x20000, the code linked in after it */
SECTIONS
{
    .boot 0x000000 : { KEEP(*(.boot)) }
    .os 0x020000 : { KEEP(*(.os)) }
    .text 0x024000 : { *(.text .text.*) *(.rodata .rodata.*) }
    .data : { *(.data .data.*) *(.bss .bss.*) *(COMMON) }
    ASSERT(SIZEOF(.data) == 0, "rom: code with data of its own can't run from flash")
    ASSERT(. < 0x0C0000, "rom: past the OS's space")
    /DISCARD/ : { *(.comment) *(.note*) }
}
