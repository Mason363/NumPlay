/* NumView's reading fonts (read_font.c, made by tools/readfont.py): a book's text at three sizes, its headings at
 * three. Glyphs: the 95 ASCII ones, then those of nv_extra's code points, in its (sorted) order. */
#ifndef NV_READ_FONT_H
#define NV_READ_FONT_H
#include "font.h"

extern const np_font_t nv_read_s, nv_read_m, nv_read_l, nv_head_s, nv_head_m, nv_head_l;
extern const uint16_t nv_extra[];
extern const int nv_extra_count;
#endif
