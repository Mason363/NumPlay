/* Balatro on the TI-84 Plus CE: where its code goes. The game's code is bigger than a program can have in RAM, so
 * part of it goes where a program has no code otherwise (tools/place.py picks which, biggest first):
 *   vram     the second half of the screen's memory (the game shows 8-bit colour: the first half is the screen),
 *   pshadow  the pixel shadow, under the static data,
 *   bssx     the static data's room left after the game's own,
 * and the rest stays in the program (convbin splits it into appvars past 64 KB). src/ce_main.c's loader copies them
 * into place from appvars (BALCV0, BALCV1, BALCP, BALCB) when the game starts: the loader and what it calls stay in
 * the program (.text.boot, the C library's .text and .text.__*). */
INCLUDE gen/place.ld

MEMORY
{
    prgm    : ORIGIN = LOAD_ADDR, LENGTH = 0x25000
    pshadow : ORIGIN = 0xD031F6, LENGTH = BSSHEAP_LOW - 0xD031F6
    bss     : ORIGIN = BSSHEAP_LOW, LENGTH = BSS_SIZE
    bssx    : ORIGIN = BSSHEAP_LOW + BSS_SIZE, LENGTH = BSSHEAP_HIGH - BSSHEAP_LOW - BSS_SIZE
    vram    : ORIGIN = 0xD52C00, LENGTH = 0x12C00
}

SECTIONS
{
    . = LOAD_ADDR;

    .header :
    {
        KEEP (*(.header))
        KEEP (*(.header.icon))
    } >prgm

    .init :
    {
        *(.init)
    } >prgm

    /* the places outside the program, filled by tools/place.py (named before .text, so they take their sections) */
    .vram : { _ce_vram_lo = .; INCLUDE gen/place_vram.ld _ce_vram_hi = .; } >vram
    .pshadow : { _ce_pshadow_lo = .; INCLUDE gen/place_pshadow.ld _ce_pshadow_hi = .; } >pshadow
    .bssx : { _ce_bssx_lo = .; INCLUDE gen/place_bssx.ld _ce_bssx_hi = .; } >bssx

    .text :
    {
        *(.text.boot .text.boot.*)
        *(.rodata.boot .rodata.boot.*)
        *(.text .stub .text.__*)
        *(.text.*)
    } >prgm

    .rodata :
    {
        *(.rodata .rodata.* .gnu.linkonce.r.*)
    } >prgm

    .init_array :
    {
        PROVIDE_HIDDEN (__init_array_start = .);
        KEEP (*(.preinit_array))
        KEEP (*(SORT_BY_INIT_PRIORITY(.init_array.*)))
        KEEP (*(.init_array))
        PROVIDE_HIDDEN (__init_array_end = .);
    } >prgm
    .fini_array :
    {
        PROVIDE_HIDDEN (__fini_array_start = .);
        KEEP (*(SORT_BY_INIT_PRIORITY(.fini_array.*)))
        KEEP (*(.fini_array))
        PROVIDE_HIDDEN (__fini_array_end = .);
    } >prgm
    ___data_low = .;
    .data :
    {
        *(.data .data.* .gnu.linkonce.d.*)
        SORT(CONSTRUCTORS)
        *(.data1)
        ___runprgm_reloc_start = .;
        *(.runprgm.reloc)
        ___runprgm_reloc_end = .;
    } >prgm
    ___data_high = .;
    ___data_len = ___data_high - ___data_low;

    .bss (NOLOAD) :
    {
        ___bss_low = .;
        *(.dynbss)
        *(.bss .bss.* .gnu.linkonce.b.*)
        *(COMMON)
        ___bss_high = .;
    } >bss
    ___bss_len = ___bss_high - ___bss_low;

    /* no heap: the room after the static data holds code */
    ___heap_low = BSSHEAP_HIGH;
    ___heap_high = BSSHEAP_HIGH;

    .runprgm 0xE30800 : AT(___runprgm_reloc_start) {
        KEEP(*(.runprgm.reloc))
    }

    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
        *(.debug*)
        *(.eh_frame)
    }
}

PROVIDE(___runprgm_reloc_len = ___runprgm_reloc_end - ___runprgm_reloc_start);
