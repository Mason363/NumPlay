/* Compact a standalone NWA after the first link has removed unused code.
 * Keep EADK metadata separate, and preserve mergeable strings for nwlink.
 * NumPlay modules retain their original sections for launcher garbage collection.
 */
SECTIONS {
  .rodata.eadk_app_name : { *(.rodata.eadk_app_name) }
  .rodata.eadk_app_icon : { *(.rodata.eadk_app_icon) }
  .rodata.eadk_api_level : { *(.rodata.eadk_api_level) }
  .text : { *(.text.startup.main) *(.text .text.*) }
  .rodata.str1.1 : { *(.rodata*.str1.1) }
  .rodata : { *(.rodata .rodata.*) }
  .data : { *(.data .data.*) }
  .bss : { *(.bss .bss.*) *(COMMON) }
  /DISCARD/ : { *(.comment) *(.note.GNU-stack) }
}
