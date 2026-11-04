/* Drawing on the CE's 8-bit screen (ce.h's scr): the data's assets, picture groups (their palettes and maps),
 * pictures, blits, text and the palette's effects. */
#ifndef CE_GFX_H
#define CE_GFX_H
#include <stdbool.h>
#include <stdint.h>
#include "ce.h"
#include "pic.h"

#define PICS_COLORS 224   /* a palette: the group's colours, then 32 fixed ones (the games' UI_*) */
#define MAP_BYTES (512 + (YQ_HI - YQ_LO) + 2 * (CQ_HI - CQ_LO) + 2048 + 512 + 1)   /* (the last: 4 with phases) */

/* the data: the appvars, where each asset is (appvar << 16 | offset), each picture's group, where the kinds
 * of assets start */
typedef struct {
  const uint8_t *const *vars;
  const uint32_t *at;
  const uint8_t *pic_group;
  int pic_first, map_first, font_first;
} Assets;
void gfx_init(const Assets *a, int tables);
const uint8_t *asset(int id);

/* a group's map (decoded once into one of two places): its palette (256 words), and the rest */
const uint8_t *map_of(int group);
const uint16_t *map_palette(int group);
const uint8_t *map_remap(int group, int which);   /* 0: each colour darker, 1: lighter */

/* pictures: decoded over what dst holds (pic_into), or with their bases first (pic_full) */
int pic_group_of(int pic);
void pic_into(int pic, uint8_t *dst, int stride);
void pic_full(int pic, uint8_t *dst, int stride);
int pic_w(int pic);
int pic_h(int pic);

/* drawing on scr, clipped to the screen */
void d_fill(int x, int y, int w, int h, uint8_t c);
void d_frame(int x, int y, int w, int h, uint8_t c);
void d_remap(int x, int y, int w, int h, const uint8_t *lut);
/* (sx, sy, w, h) of an image (stride bytes a row) at (x, y): rows doubled (dbl: the image's rows are half) */
void d_blit(const uint8_t *src, int stride, int sx, int sy, int w, int h, int x, int y, bool dbl);
/* the same, the colour key skipped */
void d_blit_key(const uint8_t *src, int stride, int w, int h, int x, int y, uint8_t key);
/* an image doubled both ways over the whole screen (160 x 120), dx pixels to the right (fill on the side) */
void d_blit2(const uint8_t *src, int stride, int dx, uint8_t fill);
/* static: random greys of the fixed colours (UI) over the rectangle */
void d_noise(int x, int y, int w, int h, uint32_t seed, const uint8_t *greys);

/* text: the fonts' glyphs (2-bit alpha) in an ink: the palette indices of alpha 1, 2, 3 (ink[1..3]) */
typedef const uint8_t *Ink;
int d_text(int font, int x, int y, const char *s, Ink ink);
int d_text_n(int font, int x, int y, const char *s, int n, Ink ink);
int d_text_w(int font, const char *s);
int d_text_wn(int font, const char *s, int n);
int d_line_h(int font);
void d_text_c(int font, int cx, int y, const char *s, Ink ink);
void d_text_r(int font, int rx, int y, const char *s, Ink ink);
/* a small picture of 2-bit alpha (w, h, rows) */
void d_img(const uint8_t *img, int x, int y, Ink ink);

/* the palette shown (ce_pal) from a group's (-1: none, its colours black): dim (0 black .. 32 as it is .. 64
 * twice as bright), the 4 phases' static (phase[0..3]: 0 .. 32 of the way to the grey phase[4..7], 0 .. 31; NULL:
 * none), white (0 .. 32 of the way
 * to white); the fixed colours as they are */
void pal_set(int group, int dim, const uint8_t *phase, int white);
/* the fixed colours dimmed too (0 .. 32), until the next pal_set */
void pal_ui(int dim);
#endif
