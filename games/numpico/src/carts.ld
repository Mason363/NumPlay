/* NumPico.nwa on its own: the table of carts keeps its own section, and np_numpico_end marks its end,
   as NumPlay's numplay.ld marks the end of NumPico's block (main.c reads no cart past it). */
SECTIONS {
  .rodata.np_carts : { KEEP(*(.rodata.np_carts)) np_numpico_end = .; }
}
