/* data.bin (tools/pack.py) in the app's read-only data */
#ifdef __APPLE__
  .const_data
  .p2align 2
  .globl _mb_data
_mb_data:
  .incbin DATA_BIN
  .p2align 2
#else
  .section .rodata.mb_data,"a"
  .balign 4
  .global mb_data
mb_data:
  .incbin DATA_BIN
  .balign 4
#endif
#ifdef __ELF__
  .section .note.GNU-stack,"",%progbits
#endif
