/* data.bin (tools/pack.py) in the app's read-only data */
#ifdef __APPLE__
  .const_data
  .p2align 2
  .globl _fnafw_data
_fnafw_data:
  .incbin DATA_BIN
  .p2align 2
#else
  .section .rodata.fnafw_data,"a"
  .balign 4
  .global fnafw_data
fnafw_data:
  .incbin DATA_BIN
  .balign 4
#endif
#ifdef __ELF__
  .section .note.GNU-stack,"",%progbits
#endif
