/* the game data pack produced by tools/pack.py */
#ifdef __APPLE__
  /* (the host harness built on macOS: Mach-O sections, C names with an underscore) */
  .section __TEXT,__const
  .p2align 2
  .globl _pack_data
_pack_data:
  .incbin "pack.bin"
pack_end:
  .p2align 2
  .globl _pack_size
_pack_size:
  .long pack_end - _pack_data
#else
  .section .rodata.pack_data, "a"
  .balign 4
  .global pack_data
pack_data:
  .incbin "pack.bin"
pack_end:
  .balign 4
  .global pack_size
pack_size:
  .word pack_end - pack_data
#endif
#if defined(__linux__) && defined(__ELF__)
  .section .note.GNU-stack, "", %progbits
#endif
