/* NumVisuals: what the app (main.c), the drawing kit (core.c) and the backgrounds (scenes.c) share.
 *
 * The screen is drawn in strips of SH rows (buf, from row top) that go straight to the screen. Colours are worked out
 * in 8 bits a channel (and up to 511, for light added on light), then dithered to the screen's 16 bits with a 4x4
 * Bayer matrix: gradients come out smooth instead of in bands. Everything per pixel is in integers, so the TI and Casio
 * calculators, which have no floating-point unit, draw it as fast. */
#ifndef VIS_H
#define VIS_H
#define HOT static inline __attribute__((always_inline)) /* (the per-pixel helpers, inlined even at -Os) */
#include <stdbool.h>
#include <stdint.h>

#define W 320
#define H 240
#define SH 24 /* rows per strip */
typedef uint16_t color;
#define RGB(c) (color)((((c) >> 8) & 0xF800) | (((c) >> 5) & 0x07E0) | (((c) >> 3) & 0x1F))
#define WHITE 0xFFFF

extern color buf[W * SH];
extern int top;       /* the screen row buf starts at */
extern uint32_t now;  /* milliseconds */

/* ---- numbers */
uint32_t rnd(void);
HOT int clampi(int v, int a, int b) { return v < a ? a : v > b ? b : v; }
HOT int iabs(int v) { return v < 0 ? -v : v; }
/* sine and cosine of a in 1/1024 turns, times 16384 */
extern int16_t sin_t[1024];
HOT int isin(int a) { return sin_t[a & 1023]; }
HOT int icos(int a) { return sin_t[(a + 256) & 1023]; }
/* e^(-i/32) times 256, i from 0 (256) to 255 (about 0) */
extern uint8_t exp_t[256];
HOT int fall(int i) { return i <= 0 ? 256 : i > 255 ? 0 : exp_t[i]; }
/* a hash of two numbers, 0 to 255 */
HOT unsigned hash2(int x, int y) {
  uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return (h ^ (h >> 16)) & 255;
}
/* the square root of v (below 65536), rounded down */
extern uint8_t sqrt_t[4096];
HOT int isqrt(int v) { return v <= 0 ? 0 : v >= 65536 ? 255 : sqrt_t[v >> 4]; }
/* value noise: x and y in 1/256 of its grid, 0 to 255; noise1 along one line */
int noise2(int x, int y);
int noise1(int x);
/* fractal noise: n octaves, each half the last (0 to about 255) */
int fbm2(int x, int y, int n);
int fbm1(int x, int n);

/* ---- colour out: r, g, b from 0 to 1015 (over 255 is light added on light, kept white), t the pixel's Bayer value */
extern uint16_t R5[1024], G6[1024], B5[1024];
extern const uint8_t bayer[4][4];
HOT color dith(int r, int g, int b, int t) { return R5[r + (t >> 1)] | G6[g + (t >> 2)] | B5[b + (t >> 1)]; }
/* a gradient of 256 colours through n given ones (0xRRGGBB), as three channels */
typedef struct {
  uint8_t r[256], g[256], b[256];
} ramp_t;
void ramp(ramp_t *p, const uint32_t *c, int n);
/* f over b, a from 0 (all b) to 32 (all f) */
HOT color mix(color f, color b, int a) {
  uint32_t x = (f | (uint32_t)f << 16) & 0x07E0F81F, y = (b | (uint32_t)b << 16) & 0x07E0F81F;
  y = (y + ((x - y) * (uint32_t)a >> 5)) & 0x07E0F81F;
  return (color)(y | y >> 16);
}
/* light added to a pixel already drawn (r, g, b from 0 to 255) */
HOT void add(color *p, int r, int g, int b) {
  int c = *p, R = (c >> 11) + (r >> 3), G = (c >> 5 & 63) + (g >> 2), B = (c & 31) + (b >> 3);
  *p = (color)((R > 31 ? 31 : R) << 11 | (G > 63 ? 63 : G) << 5 | (B > 31 ? 31 : B));
}
/* a soft dot of light at (x, y) in 1/16 pixels, radius r in 1/16 pixels, colour times k (256: as given) */
void glow_dot(int x, int y, int r, uint32_t rgb, int k);
/* a line of light, antialiased, from (x0, y0) to (x1, y1) in 1/16 pixels, brightening from k0 to k1 (256: full) */
void glow_line(int x0, int y0, int x1, int y1, uint32_t rgb, int k0, int k1);
/* a rectangle darkened (or lightened) towards c, a from 0 to 32 */
void shade(int x, int y, int w, int h, color c, int a);
/* rows y0..y1 darkened from a0 (at y0) to a1 (at y1), out of 256 */
void veil(int y0, int y1, int a0, int a1);

/* ---- text: Nunito as a distance field, at any size. x and the baseline in pixels, cap: the capitals' height in
 * 1/16 pixels; bold: 0 normal, more for bolder, less for lighter (1/16 pixels of outline); a: 0 to 32 */
int text_w(const char *s, int cap);           /* in 1/16 pixels */
void text(const char *s, int x16, int base, int cap, int bold, color c, int a);
void text_soft(const char *s, int x16, int base, int cap, int bold, int soft, color c, int a); /* blurred: shadows */
/* white, with a soft dark halo round it (one pass) */
void say(const char *s, int x16, int base, int cap, int bold, int a);
void say_c(const char *s, int cx, int base, int cap, int bold, int a); /* centred on cx */
/* a letter's coverage (0 to 255) into a w x h mask, its pen at x16 on the baseline base, mirrored or not */
void glyph_mask(unsigned ch, int cap, uint8_t *m, int w, int h, int x16, int base, bool mirror);
/* digits at a fixed width (a clock's don't move as they change) */
int digits_w(const char *s, int cap);
void digits(const char *s, int x16, int base, int cap, int bold, int a);

/* ---- the backgrounds (scenes.c) */
typedef struct {
  const char *name;
  void (*init)(void);
  void (*frame)(int dt);
  void (*draw)(void);
} bg_t;
extern const bg_t bgs[];
extern const int nbg;
#endif
