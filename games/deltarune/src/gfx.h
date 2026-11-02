/* Deltarune: drawing.
 *
 * A frame is a draw list, rasterised band by band (the calculator has no
 * frame buffer) and pushed to the screen. Commands are in screen pixels and
 * painted in the order given. The Light World is the original's 320 x 240, the
 * calculator's own size; the Dark World's 640 x 480 is halved (its pictures,
 * drawn twice their size there, are drawn as they are). */
#ifndef DR_GFX_H
#define DR_GFX_H
#include <stdbool.h>
#include <stdint.h>
#include "plat.h"

static inline uint16_t rgb565(uint32_t rgb) {   /* 0xRRGGBB */
  return (uint16_t)(((rgb >> 8) & 0xF800) | ((rgb >> 5) & 0x07E0) | ((rgb >> 3) & 0x001F));
}
#define C_BLACK 0x0000
#define C_WHITE 0xFFFF
#define C_RED 0xF800
#define C_YELLOW 0xFFE0

enum { GFX_FLIPX = 1, GFX_FLIPY = 2 };

#define GFX_BAND 16           /* screen rows rasterised at a time */
#define GFX_MAX_ITEMS 400     /* commands in a frame (more are dropped) */
#define GFX_TEXT_POOL 1536    /* bytes of text in a frame */

/* the band: SCREEN_W * GFX_BAND pixels, the caller's (main's stack on the calculator) */
void gfx_set_band_memory(uint16_t *px);
void gfx_begin(void);
void gfx_end(void);
/* later commands draw only inside this screen rectangle (gfx_clip_all(): all of it) */
void gfx_clip(int x, int y, int w, int h);
void gfx_clip_all(void);

void gfx_rect(int x, int y, int w, int h, uint16_t color, uint8_t alpha);   /* alpha 0 (none) to 255 */
void gfx_frame_rect(int x, int y, int w, int h, int t, uint16_t color);     /* an outline t pixels thick */
/* frame i of sprite s (SPR_* in data.h), its origin at (x, y); alpha 255 opaque */
void gfx_sprite(uint16_t s, int i, int x, int y, uint8_t flags, uint8_t alpha);
/* the same, every opaque pixel in one colour (a flash, a silhouette) */
void gfx_sprite_color(uint16_t s, int i, int x, int y, uint8_t flags, uint16_t color, uint8_t alpha);
/* the same, scaled (fixed point 8.8, 256 = 1) around its origin, and turned by angle (0-255 for a turn) */
void gfx_sprite_ex(uint16_t s, int i, int x, int y, int scale_x, int scale_y, int angle, uint16_t color, uint8_t alpha);
/* the room's tiles (world.c gives them), its point (wx, wy) in screen pixels at the screen's top-left; front:
 * those drawn over the characters */
void gfx_tiles(int wx, int wy, bool front);
/* text in a font (FONT_* in data.h), its top-left at (x, y); returns its width. '\n' starts a line */
int gfx_text(int font, int x, int y, const char *s, uint16_t color, uint8_t alpha);
/* the same, each letter shaken by up to `shake` pixels (the game's shaking text), seeded */
int gfx_text_ex(int font, int x, int y, const char *s, int n, uint16_t color, int shake, uint32_t seed, int spacing);
#define GFX_FIXED 100   /* spacing GFX_FIXED + n: every letter n pixels on (obj_writer's hspace) */
int gfx_text_width(int font, const char *s, int n);
int font_height(int font);
/* a line one pixel wide */
void gfx_line(int x0, int y0, int x1, int y1, uint16_t color);
/* sine and cosine of a in 1/256 turns, in 1/1024 */
int gfx_sin(int a);
int gfx_cos(int a);
/* a solid circle */
void gfx_circle(int cx, int cy, int r, uint16_t color, uint8_t alpha);

/* how many commands the last frame dropped */
extern int gfx_dropped;
#endif
