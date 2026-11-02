/* First step of a NumPlay module: `ld -r -d -T tools/module.ld game.o`.
   Merges a game into one code section, one .data and one .bss, and drops what
   the launcher does not need (the game's own name, icon and unwind tables, and
   its constructor lists: the installer's start code never runs them, so a game
   on its own doesn't either). .eh_frame stays, in the code: an app linked with
   GCC's crtbegin.o points to it (frame_dummy's __EH_FRAME_BEGIN__). */
SECTIONS {
  /DISCARD/ : {
    *(.rodata.eadk_app_name) *(.rodata.eadk_app_icon) *(.rodata.eadk_api_level)
    *(.ARM.exidx*) *(.ARM.extab*) *(.comment) *(.note*) *(.debug*)
    *(.init_array*) *(.fini_array*) *(.preinit_array*)
  }
  .text : {
    *(.text .text.* .rodata .rodata.* .glue_7 .glue_7t .vfp11_veneer .v4_bx)
    *(.eh_frame .eh_frame.* .gcc_except_table .gcc_except_table.*)
  }
  .data : { *(.data .data.*) }
  .bss : { *(.bss .bss.* COMMON) }
}
