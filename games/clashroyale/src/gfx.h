/* Clash Royale: drawing, a band of 16 rows at a time (no frame buffer fits in the RAM apps get).
 *
 * For each band the screen's whole drawing function runs, everything clipped to the band, then the band is
 * pushed to the screen. */
#ifndef CR_GFX_H
#define CR_GFX_H
#include <stdbool.h>
#include <stdint.h>
#include "plat.h"

#define BAND_H 16
#define RGB(c) (uint16_t)((((c) >> 8) & 0xF800) | (((c) >> 5) & 0x07E0) | (((c) >> 3) & 0x1F))
#define WHITE 0xFFFF
#define BLACK 0x0000

extern uint16_t *gfx_band;   /* SCREEN_W x BAND_H */
extern int gfx_y0;           /* the band's top row */
void gfx_set_band_memory(uint16_t *m);
void gfx_frame(void (*draw)(void));
static inline bool gfx_rows(int y, int h) { return y < gfx_y0 + BAND_H && y + h > gfx_y0; }

uint16_t gfx_mix(uint16_t f, uint16_t b, int a);   /* f over b, a from 0 (b) to 32 (f) */
void gfx_fill(int x, int y, int w, int h, uint16_t c);
void gfx_blend(int x, int y, int w, int h, uint16_t c, int a);
void gfx_frame_rect(int x, int y, int w, int h, uint16_t c);
void gfx_round_rect(int x, int y, int w, int h, int r, uint16_t c);
/* an ellipse (rx, ry) blended a / 32; filled, or its ring of width t */
void gfx_ellipse(int cx, int cy, int rx, int ry, uint16_t c, int a);
void gfx_ring(int cx, int cy, int rx, int ry, int t, uint16_t c, int a);
void gfx_line(int x0, int y0, int x1, int y1, uint16_t c);

/* text in data.bin's fonts (FONT_*), with a dark outline when shadow */
int gfx_text(int font, int x, int y, const char *s, uint16_t c);
int gfx_text_w(int font, const char *s);
int gfx_line_h(int font);
void gfx_text_c(int font, int cx, int y, const char *s, uint16_t c);
void gfx_text_r(int font, int rx, int y, const char *s, uint16_t c);
void gfx_text_o(int font, int x, int y, const char *s, uint16_t c);    /* outlined */
void gfx_text_oc(int font, int cx, int y, const char *s, uint16_t c);
/* plain words wrapped in w pixels; returns the height used */
int gfx_wrap(int font, int x, int y, int w, const char *s, uint16_t c);

/* data.bin's pictures (IMG_*): drawn with their anchor at (x, y), red palette for team 1, mirrored */
void gfx_img(int id, int x, int y, int team, bool mirror);
void gfx_img_dim(int id, int x, int y, int team, bool mirror, int dim);   /* dim: 0 none to 32 black */
void gfx_img_tint(int id, int x, int y, int team, bool mirror, uint16_t c, int a);
int gfx_img_w(int id);
int gfx_img_h(int id);
void gfx_img_box(int id, bool mirror, int *x0, int *y0, int *w, int *h);   /* relative to the anchor */

/* the arena: 320 x 180 colour indices (unpacked in RAM) and its 256 colours */
void gfx_arena(const uint8_t *idx, const uint8_t *pal, int y0, int rows);
#endif
