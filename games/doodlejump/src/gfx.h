/* Doodle Jump: drawing. One buffer of 160 x 240 pixels holds the page, or half the screen (320 x 120) for the
 * menus; a view says which part of the screen it is, and everything drawn clips itself to it. */
#ifndef DJ_GFX_H
#define DJ_GFX_H
#include <stdbool.h>
#include <stdint.h>
#include "data.h"

#define RGB(r, g, b) ((uint16_t)(((r) >> 3) << 11 | ((g) >> 2) << 5 | (b) >> 3))
#define INK RGB(20, 18, 16)
#define RED_INK RGB(160, 20, 20) /* the original's red handwriting */

typedef struct {
  uint16_t *buf;
  int x0, y0, w, h; /* the part of the screen it holds */
} view_t;
extern view_t V;
extern uint16_t gfx_buf[160 * 240];

void gfx_begin(int x, int y, int w, int h);
void gfx_end(void);                                   /* to the screen */
void gfx_screen(void (*draw)(void));                  /* the whole screen, a half at a time */
uint16_t gfx_mix(uint16_t bg, uint16_t fg, int a32);  /* a32: 0 bg ... 32 fg */
void gfx_fill(int x, int y, int w, int h, uint16_t c);
void gfx_shade(int x, int y, int w, int h, uint16_t c, int a32);
/* a picture with its anchor at x, y (screen pixels); alpha 0..32 */
void gfx_spr(int id, int x, int y, int alpha);
void gfx_spr_tint(int id, int x, int y, uint16_t c, int alpha);
/* the graph paper: lines `cell` sixteenths of a pixel apart, the page scrolled down by `scroll` sixteenths */
void gfx_paper(int x, int y, int w, int h, int cell, int left, int scroll, int dim);
/* the game's font: big (its letters 28 pixels apart) or small (21); align 0 left, 1 middle, 2 right */
enum { F_BIG, F_SMALL };
int gfx_text(int font, int x, int base, const char *s, uint16_t c, int align);
int gfx_text_w(int font, const char *s);
#endif
