/* Plants vs. Zombies: drawing.
 *
 * A frame is a draw list, rasterised band by band (the calculator has no frame
 * buffer for its screen) and pushed. Under everything is the background, one
 * byte a pixel in RAM: the lawn or a menu's picture. Commands are in screen
 * pixels and painted in the order given. */
#ifndef PVZ_GFX_H
#define PVZ_GFX_H
#include <stdbool.h>
#include <stdint.h>
#include "plat.h"

static inline uint16_t rgb565(uint32_t rgb) {   /* 0xRRGGBB */
  return (uint16_t)(((rgb >> 8) & 0xF800) | ((rgb >> 5) & 0x07E0) | ((rgb >> 3) & 0x001F));
}
#define C_BLACK 0x0000
#define C_WHITE 0xFFFF

/* a colour f over b, a from 0 (all b) to 32 (all f) */
static inline uint16_t mix565(uint16_t f, uint16_t b, int a) {
  uint32_t x = (f | (uint32_t)f << 16) & 0x07E0F81F, y = (b | (uint32_t)b << 16) & 0x07E0F81F;
  y = (y + ((x - y) * (uint32_t)a >> 5)) & 0x07E0F81F;
  return (uint16_t)(y | y >> 16);
}

enum { GFX_FLIP = 1, GFX_OUTLINE = 2, GFX_CENTER = 4, GFX_RIGHT = 8, GFX_SHADOW = 16, GFX_MEASURE = 32, GFX_TIGHT = 64 };
/* colourings of a sprite (its palette, changed) */
enum { TINT_NONE, TINT_HIT, TINT_CHILL, TINT_FROZEN, TINT_HYPNO, TINT_SLEEP, TINT_DARK, TINT_WHITE, TINT_BLACK,
       TINT_RED, TINT_GRAY, TINT_N };

#define GFX_BAND 20            /* screen rows rasterised at a time */
#define GFX_MAX_ITEMS 360      /* commands in a frame (more are dropped) */
#define GFX_TEXT_POOL 2048     /* bytes of text in a frame */

/* the background: one palette index a pixel, and its colours */
extern uint8_t gfx_bg[SCREEN_W * SCREEN_H];
extern uint16_t gfx_bg_pal[256];
extern bool gfx_bg_on;         /* false: the background is black */
/* unpacks background b (BG_*) into gfx_bg, its columns from src_x on; rows y0 to y1 only, if y1 > y0 */
bool gfx_load_back(int b, int src_x);
bool gfx_load_back_rows(int b, int src_x, int y0, int y1, int x0, int x1);
/* where the backgrounds are unpacked; between two of those, free for a frame's own use */
extern uint32_t gfx_scratch[640 * 16 / 4];

/* the band: SCREEN_W * GFX_BAND pixels, the caller's (main's stack on the calculator) */
void gfx_set_band_memory(uint16_t *px);
void gfx_begin(void);
void gfx_end(void);
/* later commands draw only inside this screen rectangle (gfx_clip_all(): all of it) */
void gfx_clip(int x, int y, int w, int h);
void gfx_clip_all(void);

void gfx_rect(int x, int y, int w, int h, uint16_t color, int alpha);   /* alpha 0 (none) to 32 */
void gfx_frame_rect(int x, int y, int w, int h, int t, uint16_t color, int alpha);
void gfx_round_rect(int x, int y, int w, int h, int r, uint16_t color, int alpha);
void gfx_ellipse(int cx, int cy, int rx, int ry, uint16_t color, int alpha);
/* frame f of animation a (A_* or I_*), its anchor at (x, y); alpha 32 opaque */
void gfx_anim(int a, int f, int x, int y, int flags, int tint, int alpha);
/* the same, scaled (256 = 1) from its anchor */
void gfx_anim_scaled(int a, int f, int x, int y, int sx, int sy, int flags, int tint, int alpha);
/* a picture (I_*) */
static inline void gfx_image(int a, int x, int y) { gfx_anim(a, 0, x, y, 0, TINT_NONE, 32); }
/* a sprite's size (its first frame's) and its top-left from its anchor */
void anim_box(int a, int f, int *x, int *y, int *w, int *h);
/* text in a font (F_*), its baseline at y and its left (or centre, GFX_CENTER, or right) at x; returns its
 * width. GFX_OUTLINE draws it with a dark rim; '\n' starts a line */
int gfx_text(int font, int x, int y, const char *s, uint16_t color, int flags);
int gfx_text_width(int font, const char *s);
int font_line(int font);       /* the height of a line */
/* text wrapped to width, centred on x (flags as gfx_text; GFX_MEASURE: nothing drawn; GFX_TIGHT: its lines a pixel
 * closer); returns the baseline after its last line */
int gfx_paragraph(int font, int x, int y, int width, const char *s, uint16_t color, int flags);
/* copies text into the frame's pool (text kept until gfx_end), printf-like */
const char *gfx_fmt(const char *fmt, ...);
/* a call made for each band, in order with the commands (for what the commands can't draw) */
typedef void (*gfx_fn)(uint16_t *band, int y0, int y1, void *ctx);
void gfx_custom(gfx_fn fn, void *ctx);

extern int gfx_dropped;
#endif
