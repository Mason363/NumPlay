/* NumView on its own: the file's table keeps its own section, and np_numview_end marks its end (main.c reads no
   table past it). */
SECTIONS {
  .rodata.np_media : { KEEP(*(.rodata.np_media)) np_numview_end = .; }
}
