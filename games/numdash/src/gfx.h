#ifndef NUMDASH_GFX_H
#define NUMDASH_GFX_H
#include <stdbool.h>
#include <stdint.h>
#include "assets.h"
#include "assets_lv.h"

/* The screen is rendered in horizontal strips: every primitive clips to the
 * current strip, so a full frame needs only one strip of RGB565 memory. */
enum { GFX_W = 320, GFX_H = 240, STRIP_H = 24, STRIPS = GFX_H / STRIP_H };
extern uint16_t *gfx_strip; /* (on main's stack: the RAM apps get is smaller on some calculator software) */
extern int gfx_y0, gfx_y1;
void gfx_begin_strip(int index);
void gfx_clip(int x0, int y0, int x1, int y1);
void gfx_unclip(void);

typedef uint16_t color_t;
static inline color_t rgb(unsigned r, unsigned g, unsigned b) {
  return (color_t)(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
}
static inline unsigned c_r(color_t c) { unsigned v = (c >> 11) & 31; return (v << 3) | (v >> 2); }
static inline unsigned c_g(color_t c) { unsigned v = (c >> 5) & 63; return (v << 2) | (v >> 4); }
static inline unsigned c_b(color_t c) { unsigned v = c & 31; return (v << 3) | (v >> 2); }
color_t c_mix(color_t a, color_t b, unsigned t256);
color_t c_scale(color_t a, unsigned t256);
color_t c_hsv_lighten(color_t c, int sat_delta, int val_delta);
static inline uint16_t px_mix(uint16_t d, uint16_t s, unsigned a32) {
  uint32_t dd = (d | ((uint32_t)d << 16)) & 0x07E0F81Fu;
  uint32_t ss = (s | ((uint32_t)s << 16)) & 0x07E0F81Fu;
  uint32_t r = ((ss * a32 + dd * (32 - a32)) >> 5) & 0x07E0F81Fu;
  return (uint16_t)(r | (r >> 16));
}
static inline uint16_t px_add(uint16_t d, uint16_t s, unsigned a32) {
  uint32_t dd = (d | ((uint32_t)d << 16)) & 0x07E0F81Fu;
  uint32_t ss = (((s | ((uint32_t)s << 16)) & 0x07E0F81Fu) * a32 >> 5) & 0x07E0F81Fu;
  uint32_t sum = dd + ss;
  uint32_t o1 = sum & 0x00010020u, o2 = sum & 0x08000000u;
  sum |= (o1 - (o1 >> 5)) | (o2 - (o2 >> 6));
  sum &= 0x07E0F81Fu;
  return (uint16_t)(sum | (sum >> 16));
}

enum { BLEND_NORMAL = 0, BLEND_ADD = 1 };
/* Transform bits: rotation in 90 degree clockwise steps, then flips (applied
 * to the source first, as in cocos2d). */
enum { XF_ROT = 3, XF_FLIPX = 4, XF_FLIPY = 8, XF_ANCHOR = 16 /* gfx_sprite_ex: mirror about the anchor */ };

void gfx_fill(int x, int y, int w, int h, color_t c);
void gfx_blend(int x, int y, int w, int h, color_t c, unsigned alpha256);
void gfx_add(int x, int y, int w, int h, color_t c, unsigned alpha256);
void gfx_vgrad(int x, int y, int w, int h, color_t top, color_t bottom);
void gfx_vgrad_alpha(int x, int y, int w, int h, color_t c, unsigned a_top, unsigned a_bottom, int mode);
void gfx_hgrad_alpha(int x, int y, int w, int h, color_t c, unsigned a_left, unsigned a_right, int mode);
void gfx_scale_rect(int x, int y, int w, int h, unsigned keep256);
/* Anti-aliased discs and rings; coordinates in 1/16 pixel. */
void gfx_disc(int cx16, int cy16, int r16, color_t c, unsigned alpha256, int mode);
void gfx_ring(int cx16, int cy16, int r16, int thick16, color_t c, unsigned alpha256, int mode);
void gfx_round_rect(int x, int y, int w, int h, int r, color_t c, unsigned alpha256);
void gfx_triangle(int x0, int y0, int x1, int y1, int x2, int y2, color_t c, unsigned alpha256);

void gfx_sprite(int spr, int x, int y, int xform, color_t tint, unsigned alpha256, int mode);
/* Rotated/scaled sprite, bilinear. Angle in degrees clockwise, positions in
 * 1/16 px, scale in 1/256. */
void gfx_sprite_ex(int spr, int x16, int y16, int angle_deg16, int scale256, int flips, color_t tint, unsigned alpha256, int mode);
int gfx_sprite_w(int spr, int xform);
/* Rows [y0, y1) that gfx_sprite and gfx_sprite_ex can draw on (before
   clipping), so callers can skip strips they would not touch. */
void gfx_sprite_rows(int spr, int y, int xform, int *y0, int *y1);
void gfx_sprite_ex_rows(int spr, int x16, int y16, int angle_deg16, int scale256, int *y0, int *y1);
/* Sprites of both sets (assets.h, then assets_lv.h). */
static inline const Sprite *sprite_def(int spr) { return spr < SPR_COUNT ? &sprites[spr] : &sprites_lv[spr - SPR_COUNT]; }
/* Filled rectangle with anti-aliased edges (pixel coordinates). */
void gfx_rectf(float x0, float y0, float x1, float y1, color_t c, unsigned alpha256, int mode);
int gfx_sprite_h(int spr, int xform);

int gfx_text_width(int font, const char *s);
/* Draws outlined text with its pen at (x, baseline). top/bottom tint a
 * vertical gradient over the cap height. */
void gfx_text(int font, int x, int baseline, const char *s, color_t top, color_t bottom, unsigned alpha256);
void gfx_text_center(int font, int cx, int baseline, const char *s, color_t top, color_t bottom, unsigned alpha256);
void gfx_text_right(int font, int rx, int baseline, const char *s, color_t top, color_t bottom, unsigned alpha);
void gfx_number(int font, int x, int baseline, long v, color_t c);
/* Writes v in decimal, returns the length. */
int gfx_format_uint(char *buf, unsigned long v);
/* One cached text layer (lines separated by '\n') drawn scaled around its
 * centre; used for the pop-in titles. */
void gfx_layer_build(int font, const char *s);
void gfx_layer_draw(int cx, int cy, int scale256, color_t top, color_t bottom, unsigned alpha);
void gfx_layer_line_color(int line, color_t top, color_t bottom);
/* Anti-aliased convex polygon, up to 8 vertices (x, y pairs in pixels). */
void gfx_poly(const float *xy, int n, color_t c, unsigned alpha, int mode);
#endif
