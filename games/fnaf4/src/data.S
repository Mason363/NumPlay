/* data.bin (tools/pack.py) in the app's read-only data */
#ifdef __APPLE__
  .const_data
  .p2align 2
  .globl _fnaf4_data
_fnaf4_data:
  .incbin DATA_BIN
  .p2align 2
#else
  .section .rodata.fnaf4_data,"a"
  .balign 4
  .global fnaf4_data
fnaf4_data:
  .incbin DATA_BIN
  .balign 4
#endif
#ifdef __ELF__
  .section .note.GNU-stack,"",%progbits
#endif
