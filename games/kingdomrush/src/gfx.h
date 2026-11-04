/* Kingdom Rush: drawing, a band of 16 rows at a time.
 *
 * The screen is drawn in 15 bands of 320 x 16 pixels: for each band the scene's whole drawing function runs,
 * everything clipped to the band, then the band is pushed. So the game needs no frame buffer (it would not fit
 * in the RAM apps get): the battlefield's terrain waits in RAM at a byte a pixel (bg_*), the sprites are read
 * from flash as they are drawn. */
#ifndef KR_GFX_H
#define KR_GFX_H
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
/* is any of rows y to y + h - 1 in this band? */
static inline bool gfx_rows(int y, int h) { return y < gfx_y0 + BAND_H && y + h > gfx_y0; }

uint16_t gfx_mix(uint16_t f, uint16_t b, int a);   /* f over b, a from 0 (b) to 32 (f) */
void gfx_fill(int x, int y, int w, int h, uint16_t c);
void gfx_blend(int x, int y, int w, int h, uint16_t c, int a);
void gfx_frame_rect(int x, int y, int w, int h, uint16_t c);
/* an ellipse's outline (centre, half axes), a of 32 opaque */
void gfx_ellipse(int cx, int cy, int rx, int ry, uint16_t c, int a);
/* a filled ellipse, a of 32 */
void gfx_ellipse_fill(int cx, int cy, int rx, int ry, uint16_t c, int a);
void gfx_line(int x0, int y0, int x1, int y1, uint16_t c);
/* a rounded panel: a fill and a 1-pixel border */
void gfx_panel(int x, int y, int w, int h, uint16_t fill, uint16_t border);

/* text in data.bin's fonts (FONT_*); returns the width */
int gfx_text(int font, int x, int y, const char *s, uint16_t c);
int gfx_text_w(int font, const char *s);
int gfx_line_h(int font);
void gfx_text_c(int font, int cx, int y, const char *s, uint16_t c);   /* centred on cx */
void gfx_text_r(int font, int rx, int y, const char *s, uint16_t c);   /* ending at rx */
/* with a 1-pixel dark outline, for words over pictures */
void gfx_text_o(int font, int x, int y, const char *s, uint16_t c);
void gfx_text_oc(int font, int cx, int y, const char *s, uint16_t c);
/* text wrapped to width w from (x, y); returns the height */
int gfx_text_box(int font, int x, int y, int w, const char *s, uint16_t c);

/* sprites (data.bin's FRAMES and ANIMS): an animation's frame with its origin at (x, y), mirrored or not */
void spr_frame(int slot, int x, int y, bool flip);
/* the frame shown t ticks into an animation (held at its end, or looping) */
int anim_slot(int an, int t, bool loop);
int anim_ticks(int an);   /* how long it plays */
int anim_frames(int an);
static inline void spr_anim(int an, int t, bool loop, int x, int y, bool flip) { spr_frame(anim_slot(an, t, loop), x, y, flip); }
/* a frame's box around its origin (w, h; ox, oy: the origin from the top left) */
void spr_box(int slot, int *w, int *h, int *ox, int *oy);
/* tinted: the frame's shape in colour c, a of 32 */
void spr_frame_tint(int slot, int x, int y, bool flip, uint16_t c, int a);

/* pictures (pic.c) */
void pic_draw(int id, int x, int y);   /* straight from flash, decoded as the band needs */
#if NP_SOFT_FLOAT
void pic_end(void);   /* (the decoded squares kept: freed) */
#endif
int pic_w(int id);
int pic_h(int id);
/* the terrain: the picture decoded once into RAM (w x h bytes at mem), drawn at (x, y) */
void bg_load(int id, uint8_t *mem);
void bg_draw(int x, int y);
void bg_dim(bool dim);   /* the terrain darkened (behind a menu) */
#endif
