/* Five Nights at Freddy's 2: drawing, a band of 16 rows at a time.
 *
 * The screen is drawn in 15 bands of 320 x 16 pixels (one row of the pictures' 16 x 16 squares): for each
 * band the scene's whole drawing function runs, everything clipped to the band, then the band is pushed.
 * So the game needs no frame buffer (it would not fit in the RAM apps get), and a picture's squares are
 * decoded once each, as the band they fall in is drawn. */
#ifndef FNAF2_GFX_H
#define FNAF2_GFX_H
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
/* draws the frame: draw() once for each band */
void gfx_frame(void (*draw)(void));

uint16_t gfx_mix(uint16_t f, uint16_t b, int a);   /* f over b, a from 0 (b) to 32 (f) */
void gfx_fill(int x, int y, int w, int h, uint16_t c);
void gfx_blend(int x, int y, int w, int h, uint16_t c, int a);
void gfx_frame_rect(int x, int y, int w, int h, uint16_t c);
/* video noise over a rectangle: a of 32 of the way to grey static; seed changes it each frame */
void gfx_static(int x, int y, int w, int h, int a, uint32_t seed);
/* the rectangle a of 32 of the way to black, or brighter (a > 32 up to 64) */
void gfx_dim(int x, int y, int w, int h, int a);

/* text in data.bin's fonts (FONT_*); returns the width */
int gfx_text(int font, int x, int y, const char *s, uint16_t c);
int gfx_text_w(int font, const char *s);
int gfx_text_n(int font, int x, int y, const char *s, int n, uint16_t c);   /* the first n letters */
int gfx_text_wn(int font, const char *s, int n);
int gfx_line_h(int font);
void gfx_text_c(int font, int cx, int y, const char *s, uint16_t c);         /* centred on cx */
void gfx_text_r(int font, int rx, int y, const char *s, uint16_t c);         /* ending at rx */
/* with a 1-pixel dark shadow, for words over pictures */
void gfx_text_s(int font, int x, int y, const char *s, uint16_t c);
/* data.bin's small pictures (IMG_*): white (or grey) with alpha, in colour c */
void gfx_img(int id, int x, int y, uint16_t c);
int gfx_img_w(int id);
int gfx_img_h(int id);

/* pictures (pic.c): the whole of one at (x, y), or the part (sx, sy, w, h) of it */
void pic_draw(int id, int x, int y);
void pic_part(int id, int x, int y, int sx, int sy, int w, int h);
int pic_w(int id);   /* (in squares of 16 pixels, times 16) */
int pic_h(int id);
#endif
