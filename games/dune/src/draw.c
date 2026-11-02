/* The screens, drawn 8 rows at a time (a 5 KB strip); a strip is pushed only when it changed.
 *
 * The world as the web game shows a 900 x 665 screen, at 320/900 of a point per pixel: the landscape's sky and
 * layers (screen-fixed, moving with the ball's distance and the zoom), the dunes with their ground and surface,
 * the trail, the ball, the line and the coins; then the score, the guide and the menus, in the game's Bango Pro.
 * A key line in plain words stays at the bottom. */
#include <math.h>
#ifdef HOST
#include <stdio.h>
#endif
#include <string.h>
#include "art.h"
#include "draw.h"

#define W 320
#define H 240
#define BAND 8
#define S (320.0f / 900)
#define C(rgb) ((uint16_t)((((rgb) >> 8) & 0xF800) | (((rgb) >> 5) & 0x07E0) | (((rgb) >> 3) & 0x1F)))
#define WHITE 0xFFFF
#define BLACK 0x0000
#define YELLOW C(0xFFCC00)      /* font_yellow */
#define RED C(0xFF3800)         /* font_red */
#define FOCUS C(0xFFD21F)       /* the keypad's focus: a thick gold frame */
#define INK C(0x0A1230)
#define MUTED C(0xC8D0E0)
#define KEYS_Y 226              /* the key line's band */

static uint16_t band[W * BAND] __attribute__((aligned(4)));
static int top;
static uint32_t sums[H / BAND];
static bool fresh;
static const dn_view *V;
static const dn_run *R;
#ifdef HOST
int dn_clipped;
#endif

void dn_redraw(void) { fresh = false; }

/* ---------------------------------------------------------------- pixels */

static inline uint16_t mix(uint16_t a, uint16_t b, unsigned k) {   /* b over a, k of 256 */
  unsigned r = ((a >> 11) * (256 - k) + (b >> 11) * k) >> 8;
  unsigned g = (((a >> 5) & 63) * (256 - k) + ((b >> 5) & 63) * k) >> 8;
  unsigned bl = ((a & 31) * (256 - k) + (b & 31) * k) >> 8;
  return (uint16_t)(r << 11 | g << 5 | bl);
}
static uint16_t rgb888(uint32_t c) { return C(c); }
static uint32_t lerp888(uint32_t a, uint32_t b, float t) {
  if (t < 0) t = 0;
  if (t > 1) t = 1;
  int k = (int)(t * 256);
  uint32_t r = (((a >> 16) & 255) * (256 - k) + ((b >> 16) & 255) * k) >> 8;
  uint32_t g = (((a >> 8) & 255) * (256 - k) + ((b >> 8) & 255) * k) >> 8;
  uint32_t bl = ((a & 255) * (256 - k) + (b & 255) * k) >> 8;
  return r << 16 | g << 8 | bl;
}

static inline void plot(int x, int y, uint16_t c) {
  if ((unsigned)x < W && y >= top && y < top + BAND) band[(y - top) * W + x] = c;
}
static inline void blend(int x, int y, uint16_t c, unsigned k) {
  if ((unsigned)x < W && y >= top && y < top + BAND) {
    uint16_t *p = &band[(y - top) * W + x];
    *p = k >= 256 ? c : mix(*p, c, k);
  }
}

static void rect(int x, int y, int w, int h, uint16_t c, unsigned k) {
  int x0 = x < 0 ? 0 : x, x1 = x + w > W ? W : x + w, y0 = y < top ? top : y, y1 = y + h > top + BAND ? top + BAND : y + h;
  for (int j = y0; j < y1; j++) {
    uint16_t *row = band + (j - top) * W;
    if (k >= 256) for (int i = x0; i < x1; i++) row[i] = c;
    else for (int i = x0; i < x1; i++) row[i] = mix(row[i], c, k);
  }
}

/* a rounded rectangle, filled (t = 0) or as a frame t wide */
static void round_rect(int x, int y, int w, int h, int r, int t, uint16_t c, unsigned k) {
  int y0 = y < top ? top : y, y1 = y + h > top + BAND ? top + BAND : y + h;
  for (int j = y0; j < y1; j++) {
    float cy = j + 0.5f;
    float d = cy < y + r ? y + r - cy : cy > y + h - r ? cy - (y + h - r) : 0;
    int in = d > 0 ? (int)(r - sqrtf(fmaxf(0, (float)(r * r) - d * d)) + 0.5f) : 0;
    if (!t || j < y + t || j >= y + h - t) {
      for (int i = x + in; i < x + w - in; i++) blend(i, j, c, k);
    } else {
      int r2 = r - t > 0 ? r - t : 0;
      float d2 = cy < y + t + r2 ? y + t + r2 - cy : cy > y + h - t - r2 ? cy - (y + h - t - r2) : 0;
      int in2 = d2 > 0 ? (int)(r2 - sqrtf(fmaxf(0, (float)(r2 * r2) - d2 * d2)) + 0.5f) : 0;
      for (int i = x + in; i < x + t + in2; i++) blend(i, j, c, k);
      for (int i = x + w - t - in2; i < x + w - in; i++) blend(i, j, c, k);
    }
  }
}

/* the keypad's focus: gold, with a dark edge outside so it shows on any colour */
static void focus(int x, int y, int w, int h, int r) {
  round_rect(x - 5, y - 5, w + 10, h + 10, r + 5, 1, INK, 220);
  round_rect(x - 4, y - 4, w + 8, h + 8, r + 4, 3, FOCUS, 256);
}

static void ring(float cx, float cy, float r0, float r1, uint16_t c, unsigned k) {
  int y0 = (int)floorf(cy - r1), y1 = (int)ceilf(cy + r1);
  if (y0 < top) y0 = top;
  if (y1 > top + BAND) y1 = top + BAND;
  for (int j = y0; j < y1; j++)
    for (int i = (int)floorf(cx - r1); i <= (int)ceilf(cx + r1); i++) {
      float dx = i + 0.5f - cx, dy = j + 0.5f - cy, d = sqrtf(dx * dx + dy * dy);
      float a = fminf(fminf(d - r0 + 0.5f, r1 + 0.5f - d), 1);
      if (a > 0) blend(i, j, c, (unsigned)(a * k));
    }
}

static void dim(unsigned k) { rect(0, top, W, BAND, BLACK, k); }

/* ---------------------------------------------------------------- pictures */

static inline unsigned nib(const uint8_t *p, unsigned i) { return i & 1 ? p[i >> 1] & 15 : p[i >> 1] >> 4; }

/* a white picture's coverage, in a colour */
static void mask(const dn_mask *m, int x, int y, uint16_t c, unsigned k) {
  if (y >= top + BAND || y + m->h <= top) return;
  int j0 = top > y ? top - y : 0, j1 = top + BAND - y < m->h ? top + BAND - y : m->h;
  for (int j = j0; j < j1; j++)
    for (int i = 0; i < m->w; i++) {
      unsigned a = nib(m->px, (unsigned)(j * m->w + i));
      if (a) blend(x + i, y + j, c, a * k / 15);
    }
}

/* a mask scaled by s (nearest), its top left at (x, y) */
static void mask_scaled(const dn_mask *m, float x, float y, float s, uint16_t c, unsigned k) {
  int w = (int)(m->w * s + 0.5f), h = (int)(m->h * s + 0.5f);
  int iy0 = (int)floorf(y), y0 = iy0 < top ? top : iy0, y1 = iy0 + h > top + BAND ? top + BAND : iy0 + h;
  for (int j = y0; j < y1; j++) {
    int sy = (int)((j - iy0 + 0.5f) / s);
    if (sy >= m->h) continue;
    for (int i = 0; i < w; i++) {
      int sx = (int)((i + 0.5f) / s);
      if (sx >= m->w) continue;
      unsigned a = nib(m->px, (unsigned)(sy * m->w + sx));
      if (a) blend((int)floorf(x) + i, j, c, a * k / 15);
    }
  }
}

/* a 4-bit picture, its centre at (cx, cy), `size` pixels across, turned by `angle` (nearest) */
static void raw_turned(const dn_raw *p, float cx, float cy, float size, float angle, unsigned k) {
  float r = size * 0.72f;
  int y0 = (int)floorf(cy - r), y1 = (int)ceilf(cy + r);
  if (y0 < top) y0 = top;
  if (y1 > top + BAND) y1 = top + BAND;
  if (y0 >= y1) return;
  float co = cosf(angle), si = sinf(angle), k_ = p->w / size;
  for (int j = y0; j < y1; j++)
    for (int i = (int)floorf(cx - r); i <= (int)ceilf(cx + r); i++) {
      if ((unsigned)i >= W) continue;
      float dx = i + 0.5f - cx, dy = j + 0.5f - cy;
      float u = (dx * co + dy * si) * k_ + p->w / 2.0f, v = (-dx * si + dy * co) * k_ + p->h / 2.0f;
      if (u < 0 || v < 0 || u >= p->w || v >= p->h) continue;
      unsigned c = nib(p->px, (unsigned)((int)v * p->w + (int)u));
      if (c) blend(i, j, p->pal[c], k);
    }
}

/* a 4-bit picture scaled, its top left at (x, y) */
static void raw_scaled(const dn_raw *p, float x, float y, float w, float h, unsigned k) {
  int ix = (int)floorf(x), iy = (int)floorf(y), iw = (int)(w + 0.5f), ih = (int)(h + 0.5f);
  int y0 = iy < top ? top : iy, y1 = iy + ih > top + BAND ? top + BAND : iy + ih;
  for (int j = y0; j < y1; j++) {
    int sy = (int)((j - iy + 0.5f) * p->h / h);
    if (sy >= p->h) continue;
    for (int i = 0; i < iw; i++) {
      int sx = (int)((i + 0.5f) * p->w / w);
      if (sx >= p->w || (unsigned)(ix + i) >= W) continue;
      unsigned c = nib(p->px, (unsigned)(sy * p->w + sx));
      if (c) blend(ix + i, j, p->pal[c], k);
    }
  }
}

/* a row of runs (art.h), source row sy, its left at screen x dx, on screen row y; k: opacity of 256 */
static void rle_row(const dn_rle *p, int sy, int y, int dx, unsigned k) {
  if (sy < 0 || sy >= p->h || dx >= W || dx + p->w <= 0) return;
  uint16_t *row = band + (y - top) * W;
  const uint8_t *q = p->data + p->rows[sy], *e = p->data + p->rows[sy + 1];
  int x = dx;
  while (q < e && x < W) {
    unsigned t = *q++;
    if (t >= 128) {
      int n = (int)t - 127;
      unsigned c = *q++;
      if (c && x + n > 0) {
        int a = x < 0 ? 0 : x, b = x + n > W ? W : x + n;
        uint16_t col = p->pal[c];
        if (k >= 256) for (int i = a; i < b; i++) row[i] = col;
        else for (int i = a; i < b; i++) row[i] = mix(row[i], col, k);
      }
      x += n;
    } else {
      int n = (int)t + 1;
      if (x + n > 0)
        for (int i = 0; i < n; i++) {
          int xx = x + i;
          if ((unsigned)xx >= W) continue;
          unsigned c = i & 1 ? q[i >> 1] & 15 : q[i >> 1] >> 4;
          if (c) row[xx] = k >= 256 ? p->pal[c] : mix(row[xx], p->pal[c], k);
        }
      q += (n + 1) / 2;
      x += n;
    }
  }
}

/* a row of runs decoded to colour indices (for scaled pictures and the ground) */
static uint8_t idx[1280];
static void rle_decode(const dn_rle *p, int sy) {
  const uint8_t *q = p->data + p->rows[sy], *e = p->data + p->rows[sy + 1];
  int x = 0;
  while (q < e && x < p->w) {
    unsigned t = *q++;
    if (t >= 128) {
      int n = (int)t - 127;
      unsigned c = *q++;
      for (int i = 0; i < n && x < p->w; i++) idx[x++] = (uint8_t)c;
    } else {
      int n = (int)t + 1;
      for (int i = 0; i < n && x < p->w; i++) idx[x++] = (uint8_t)(i & 1 ? q[i >> 1] & 15 : q[i >> 1] >> 4);
      q += (n + 1) / 2;
    }
  }
}

/* a picture of runs, its top left at (x, y), drawn at s times its size (nearest) */
static void rle_scaled(const dn_rle *p, float x, float y, float s, unsigned k) {
  if (s <= 0) return;
  float w = p->w * s, h = p->h * s;
  if (x >= W || x + w <= 0 || y >= top + BAND || y + h <= top) return;
  int y0 = (int)floorf(y), y1 = (int)ceilf(y + h);
  if (y0 < top) y0 = top;
  if (y1 > top + BAND) y1 = top + BAND;
  for (int j = y0; j < y1; j++) {
    int sy = (int)((j + 0.5f - y) / s);
    if (sy < 0 || sy >= p->h) continue;
    if (s > 0.999f && s < 1.001f) {
      rle_row(p, sy, j, (int)floorf(x), k);
      continue;
    }
    rle_decode(p, sy);
    uint16_t *row = band + (j - top) * W;
    int i0 = (int)floorf(x), i1 = (int)ceilf(x + w);
    if (i0 < 0) i0 = 0;
    if (i1 > W) i1 = W;
    for (int i = i0; i < i1; i++) {
      int sx = (int)((i + 0.5f - x) / s);
      if (sx < 0 || sx >= p->w) continue;
      unsigned c = idx[sx];
      if (c) row[i] = k >= 256 ? p->pal[c] : mix(row[i], p->pal[c], k);
    }
  }
}

/* a layer repeated across the screen: its top at y, shifted by off pixels */
static void tiled(const dn_rle *p, float y, float off, unsigned k) {
  int iy = (int)floorf(y + 0.5f);
  int x = (int)floorf(fmodf(off, (float)p->w));
  if (x > 0) x -= p->w;
  for (int j = top; j < top + BAND; j++) {
    int sy = j - iy;
    if (sy < 0 || sy >= p->h) continue;
    for (int xx = x; xx < W; xx += p->w) rle_row(p, sy, j, xx, k);
  }
}

/* the same, repeated down too */
static void tiled2(const dn_rle *p, float y, float offx, float offy, float s, int y_end, unsigned k) {
  float pw = p->w * s, ph = p->h * s;
  float ox = fmodf(offx, pw), oy = fmodf(offy, ph);
  if (ox > 0) ox -= pw;
  if (oy > 0) oy -= ph;
  for (int j = top; j < top + BAND && j < y_end; j++) {
    if (j < y) continue;
    float v = fmodf(j + 0.5f - y - oy, ph);
    int sy = (int)(v / s);
    if (sy >= p->h) sy = p->h - 1;
    if (s == 1) {
      for (float xx = ox; xx < W; xx += pw) rle_row(p, sy, j, (int)floorf(xx), k);
      continue;
    }
    rle_decode(p, sy);
    uint16_t *row = band + (j - top) * W;
    for (int i = 0; i < W; i++) {
      float u = fmodf(i + 0.5f - ox, pw);
      unsigned c = idx[(int)(u / s) % p->w];
      if (c) row[i] = k >= 256 ? p->pal[c] : mix(row[i], p->pal[c], k);
    }
  }
}

/* ---------------------------------------------------------------- text: Bango Pro */

static int glyph_of(const dn_font *f, char c) {
  /* (a table for each font, made on first use) */
  static const dn_font *fonts[8];
  static int8_t map[8][128];
  int k = 0;
  while (k < 8 && fonts[k] && fonts[k] != f) k++;
  if (k == 8) k = 7;
  if (fonts[k] != f) {
    fonts[k] = f;
    memset(map[k], -1, sizeof map[k]);
    for (int i = 0; i < f->count; i++) map[k][(unsigned char)f->chars[i] & 127] = (int8_t)i;
  }
  return (unsigned char)c < 128 ? map[k][(unsigned char)c] : -1;
}

static int text_width(const dn_font *f, const char *s) {
  int w = 0;
  for (; *s; s++) {
    int g = glyph_of(f, *s);
    if (g >= 0) w += f->g[g].adv;
  }
  return w;
}

/* text with its baseline at y */
static void text(const dn_font *f, int x, int y, const char *s, uint16_t c, unsigned k) {
#ifdef HOST
  if (top == 0 && (x < 0 || x + text_width(f, s) > W || y - f->base < -2 || y + f->base / 3 > H)) {
    if (!dn_clipped) fprintf(stderr, "clipped: \"%s\" at %d,%d (screen %d)\n", s, x, y, V->screen);
    dn_clipped++;
  }
#endif
  if (y - f->base - 6 >= top + BAND || y + f->base / 2 + 4 < top) return;
  for (; *s; s++) {
    int gi = glyph_of(f, *s);
    if (gi < 0) continue;
    const dn_glyph *g = &f->g[gi];
    int gx = x + g->x, gy = y + g->y;
    if (gy < top + BAND && gy + g->h > top)
      for (int j = 0; j < g->h; j++) {
        int yy = gy + j;
        if (yy < top || yy >= top + BAND) continue;
        for (int i = 0; i < g->w; i++) {
          unsigned a = nib(f->px + g->off, (unsigned)(j * g->w + i));
          if (a) blend(gx + i, yy, c, a * k / 15);
        }
      }
    x += g->adv;
  }
}
static bool off_band(const dn_font *f, int y) {
#ifdef HOST
  if (top == 0) return false;   /* (the tests look at every text once) */
#endif
  return y - f->base - 6 >= top + BAND || y + f->base / 2 + 4 < top;
}
static void center(const dn_font *f, int cx, int y, const char *s, uint16_t c, unsigned k) {
  if (!off_band(f, y)) text(f, cx - text_width(f, s) / 2, y, s, c, k);
}
static void right(const dn_font *f, int rx, int y, const char *s, uint16_t c, unsigned k) {
  if (!off_band(f, y)) text(f, rx - text_width(f, s), y, s, c, k);
}
/* with a dark edge, for text over the landscape */
static void outlined(const dn_font *f, int cx, int y, const char *s, uint16_t c, unsigned k) {
  if (off_band(f, y)) return;
  int x = cx - text_width(f, s) / 2;
  for (int d = 0; d < 4; d++) text(f, x + (d == 0) - (d == 1), y + (d == 2) - (d == 3), s, INK, k * 3 / 4);
  text(f, x, y, s, c, k);
}

static char *num(char *p, uint32_t v) {
  char t[12];
  int n = 0;
  do t[n++] = (char)('0' + v % 10), v /= 10;
  while (v);
  while (n) *p++ = t[--n];
  *p = 0;
  return p;
}

/* a paragraph, centred, broken at spaces to fit `width`; returns the next baseline */
static int paragraph(const dn_font *f, int cx, int y, int width, int line, const char *s, uint16_t c) {
  char buf[96];
  while (*s) {
    int n = 0, last = -1;
    while (s[n]) {
      if (s[n] == ' ') last = n;
      memcpy(buf, s, (size_t)n + 1), buf[n + 1] = 0;
      if (text_width(f, buf) > width && last > 0) break;
      n++;
      if (n >= 94) break;
    }
    int len = s[n] ? last : n;
    memcpy(buf, s, (size_t)len), buf[len] = 0;
    center(f, cx, y, buf, c, 256);
    y += line;
    s += len;
    while (*s == ' ') s++;
  }
  return y;
}

/* ---------------------------------------------------------------- easing (Phaser's) */

static float cubic_out(float t) { t -= 1; return t * t * t + 1; }
static float cubic_in(float t) { return t * t * t; }
static float cubic_inout(float t) { return t < 0.5f ? 4 * t * t * t : (t - 1) * (2 * t - 2) * (2 * t - 2) + 1; }
static float back_in(float t) { const float s = 1.70158f; return t * t * ((s + 1) * t - s); }
static float back_out(float t) { const float s = 1.70158f; t -= 1; return t * t * ((s + 1) * t + s) + 1; }
static float yoyo(uint32_t ms, uint32_t period) {   /* 0 to 1 and back, eased, each way `period` ms */
  uint32_t p = ms % (2 * period);
  float t = (float)(p < period ? p : 2 * period - p) / period;
  return cubic_inout(t);
}

/* ---------------------------------------------------------------- the world */

static float zoom, camx, camy, shake_x, shake_y;   /* world point -> pixel: (x - camx) * zoom * S */
static float sx_of(float wx) { return (wx - camx) * zoom * S + shake_x; }
static float sy_of(float wy) { return (wy * zoom - camy) * S + shake_y; }

/* the sky */
static void sky(int land) {
  const dn_sky *s = art_skies[land];
  for (int j = top; j < top + BAND; j++) {
    uint16_t *row = band + (j - top) * W;
    if (!s->pic) {
      uint16_t c = s->rows[j < s->n ? j : s->n - 1];
      for (int i = 0; i < W; i++) row[i] = c;
    } else if (s->scale == 1) {
      rle_row(s->pic, j < s->pic->h ? j : s->pic->h - 1, j, 0, 256);
    } else {
      static uint32_t half[W / 2];   /* (two pixels a word: the row doubled) */
      if (j % 2 == 0 || j == top) {
        rle_decode(s->pic, (j / 2) < s->pic->h ? j / 2 : s->pic->h - 1);
        for (int i = 0; i < W / 2; i++) half[i] = s->pic->pal[idx[i]] * 0x10001u;
      }
      uint32_t *w32 = (uint32_t *)row;
      for (int i = 0; i < W / 2; i++) w32[i] = half[i];
    }
  }
}

/* the landscape's layers, as SceneBackground places them each frame (screen points; the ball's distance bx) */
static void layers(int land) {
  const dn_bg *b = &R->bg;
  float bx = dn_world_x(R), iz = 1 / zoom;
  switch (land) {
    case 0:
      tiled(&art_dunes, (332.5f + 50 * iz - 1) * S, -bx / 10 * S, 256);
      rle_scaled(&art_moon, 0.7f * DN_W * S, 50 * S, 1, (unsigned)(b->alpha * 256));
      break;
    case 1:
      tiled(&art_land1, (DN_H / 2.7f - 1) * S, -bx / 20 * S, 256);
      tiled(&art_land2, (DN_H / 2.1f - 1.8f) * S, -bx / 13 * 1.8f * S, 256);
      rle_scaled(&art_sun, 0.7f * DN_W * S, 50 * S, 1, (unsigned)(b->alpha * 256));
      break;
    case 2: {
      tiled(&art_hills_far, (DN_H / 3.5f - 1) * S, -bx / 10 * S, 256);
      float s = b->scale[0] / 0.9f, x = (b->start[0] - bx / 7) * S;
      rle_scaled(&art_vulcan, x - art_vulcan.w * s / 2, 332 * S, s, 256);
      float s2 = b->scale[1] / 1.1f;
      rle_scaled(&art_vulcan_fire, (b->start[1] - bx / 8.5f) * S, 0, s2, 256);
      tiled(&art_hills_close, (DN_H / 1.6f + 30 * iz - 1) * S, -bx / 6 * S, 256);
      break;
    }
    case 3: {
      rle_scaled(&art_mush1, (b->start[0] - bx / 18) * S, 0, b->scale[0] / 1.1f, 256);
      tiled(&art_mush_far, (DN_H / 4 - 1) * S, -bx / 10 * S, 256);
      float s = b->scale[1] / 1.1f;
      rle_scaled(&art_mush_purple, (b->start[1] - bx / 8) * S - art_mush_purple.w * s / 2, b->y[1] * S - art_mush_purple.h * s, s, 256);
      tiled(&art_mush_close, (DN_H / 1.7f - 1) * S, -bx / 7 * S, 256);
      break;
    }
    case 4: {
      static const float scroll[5] = {30, 18, 14, 12, 9};
      static const dn_rle *const planet[5] = {&art_planet0, &art_planet1, &art_planet2, &art_planet3, &art_planet4};
      tiled2(&art_stars, 0, -bx / 20 * S, (b->ty - 1) * S, 1, H, 256);
      for (int i = 0; i < 5; i++) {
        float s = b->scale[i] / 1.1f;
        rle_scaled(planet[i], (b->start[i] - bx / scroll[i]) * S, b->y[i] * S - planet[i]->h * s * 0.4f, s, 256);
      }
      break;
    }
    case 5:
      tiled(&art_snow_far, (182.5f + 40 * iz - 1) * S, -bx / 10 * S, 256);
      tiled2(&art_flakes2, 0, b->tx2 * S, b->ty2 * S, 1, (int)(DN_H / 2 * S), 128);
      tiled(&art_snow_close, (272.5f + 50 * iz - 1) * S, -bx / 6 * S, 256);
      tiled2(&art_flakes1, 0, b->tx * 2 * S, b->ty * 2 * S, 1, (int)(DN_H / 2 * S), 256);
      break;
    case 6: {
      float far = 182.5f + 40 * iz, close = 182.5f + 50 * iz;
      static const dn_rle *const candy[3] = {&art_candy1, &art_candy2, &art_candy3};
      static const float stored[3] = {1.1f, 1.1f, 1.2f};
      for (int i = 0; i < 2; i++) {
        float s = b->scale[i] / stored[i], cy = 232.5f + (i ? 45 : 40) * iz;
        rle_scaled(candy[i], (b->start[i] - bx / 15) * S - candy[i]->w * s / 2, cy * S - candy[i]->h * s / 2, s, 256);
      }
      tiled(&art_candy_far, (far - 1) * S, -bx / 17 * S, 256);
      {
        float s = b->scale[2] / stored[2];
        rle_scaled(candy[2], (b->start[2] - bx / 8) * S - candy[2]->w * s / 2, (close + 40) * S - candy[2]->h * s / 2, s, 256);
      }
      tiled(&art_candy_close, (close - 1) * S, -bx / 8 * S, 256);
      for (int i = 3; i < 5; i++) {
        float s = b->scale[i] / 1.4f, cy = close - (i == 3 ? 130 : 100);
        rle_scaled(&art_cloud, (b->start[i] - bx / 8) * S - art_cloud.w * s * b->ax[i], cy * S - art_cloud.h * s / 2, s, i == 4 ? 205 : 256);
      }
      break;
    }
  }
}

/* the dunes: the surface band, then the ground's texture (or Ice Mountains' gradient) */
static float surf[W], surf_min;
static int16_t tex_u[W];
static void dunes_prepare(void) {
  surf_min = 1e9f;
  int k = 0;
  for (int x = 0; x < W; x++) {
    float wx = camx + (x + 0.5f - shake_x) / (zoom * S);
    int s = -1;
    for (; k < 8; k++) {
      const dn_seg *g = &R->seg[R->order[k]];
      if (wx < g->x) break;
      if (wx < g->x + g->w + 2) {
        s = R->order[k];
        break;
      }
    }
    if (k >= 8) k = 7;
    if (s < 0) {   /* (left of the flat start, or between: the one under it) */
      s = dn_seg_at(R, wx);
    }
    if (s < 0) {
      surf[x] = 1e9f;
      continue;
    }
    const dn_seg *g = &R->seg[s];
    surf[x] = sy_of(DN_TY + dn_curve_at(R, s, wx));
    if (surf[x] < surf_min) surf_min = surf[x];
    float u = (wx - g->x) * (g->w > 830 ? 1280 / g->w : 1);
    int tu = (int)(u / 4);
    tex_u[x] = (int16_t)(tu < 0 ? 0 : tu > 319 ? 319 : tu);
  }
}

static void dunes(int land) {
  const dn_land_info *L = &art_land_info_[land];
  uint16_t surface = rgb888(L->surface);
  float bandh = 30 * zoom * S;
  const dn_rle *earth = art_earths[land];
  static int decoded = -1;
  decoded = -1;
  for (int j = top; j < top + BAND; j++) {
    if (j + 1 <= surf_min) continue;
    float wy = (j + 0.5f - shake_y) / S;
    wy = (wy + camy) / zoom;
    float v = wy - DN_TY;
    bool ground = v >= 0 && v < 405 && j >= surf_min + bandh;
    uint16_t grad = 0;
    if (ground) {
      if (earth) {
        int tv = ((int)v % 270) / 4;
        if (tv > earth->h - 1) tv = earth->h - 1;
        if (tv != decoded) rle_decode(earth, tv), decoded = tv;
      } else {
        grad = rgb888(lerp888(0x3E22FF, 0x694EFF, v / 405));
      }
    }
    uint16_t *row = band + (j - top) * W;
    for (int x = 0; x < W; x++) {
      float s = surf[x];
      if (j + 1 <= s) continue;
      if (j < s + bandh) {
        float cov = j + 1 - s;
        row[x] = cov >= 1 ? surface : mix(row[x], surface, (unsigned)(cov * 256));
      } else if (ground) {
        if (earth) {
          unsigned c = idx[tex_u[x]];
          if (c) row[x] = earth->pal[c];
        } else {
          row[x] = mix(row[x], grad, 243);   /* alpha 0.95 */
        }
      }
    }
  }
}

/* the trail: a triangle behind the ball, the ball's own colours (CreateTriangleTrail) */
static uint32_t trail_color(const dn_ball_info *b, float u, float v) {   /* u along (0 tip, 1 base), v across, 0..1 */
  const uint32_t *c = b->colors;
  int n = b->n;
  switch (b->type) {
    case 2:
      return c[((int)(u * 10) + (int)(v * 10)) & 1];
    case 4: {   /* colour 0, with 20 dots of colour 1 (random in the game) */
      static const uint8_t dots[20][3] = {{5, 9, 2}, {12, 30, 1}, {20, 14, 2}, {28, 44, 1}, {33, 22, 2}, {40, 8, 1}, {46, 36, 2}, {50, 50, 1},
                                          {9, 48, 2}, {16, 4, 1}, {24, 26, 2}, {30, 12, 1}, {37, 40, 2}, {44, 18, 1}, {52, 28, 2}, {3, 34, 1},
                                          {18, 52, 2}, {26, 2, 1}, {42, 54, 2}, {48, 6, 1}};
      float x = u * b->d, y = v * b->d;
      for (int i = 0; i < 20; i++) {
        float dx = x - dots[i][0] * b->d / 56.0f, dy = y - dots[i][1] * b->d / 56.0f;
        if (dx * dx + dy * dy < dots[i][2] * dots[i][2]) return c[1];
      }
      return c[0];
    }
    case 3: {
      float dx = u * b->d - b->cx, dy = (v - 0.5f) * b->d, t = sqrtf(dx * dx + dy * dy) / (0.8f * b->d);
      if (t >= 1) return c[n - 1];
      float p = t * (n - 1);
      int i = (int)p;
      return lerp888(c[i], c[i + 1 < n ? i + 1 : i], p - i);
    }
    default: {
      float t = b->type == 1 ? u : v;
      if (n == 1) {
        if (t < 0.45f) return lerp888(0xFFFFFF, c[0], t / 0.45f);
        if (t > 0.55f) return lerp888(c[0], 0xFFFFFF, (t - 0.55f) / 0.45f);
        return c[0];
      }
      float p = t * (n - 1);
      int i = (int)p;
      if (i >= n - 1) return c[n - 1];
      return lerp888(c[i], c[i + 1], p - i);
    }
  }
}

static void trail(void) {
  const dn_ball_info *b = &art_ball_info_[R->ball];
  float d = b->d, scale = zoom * S;
  float len = d * R->trail_sx * scale, wid = d * scale;
  if (len < 0.5f || wid < 0.5f) return;
  float cx = sx_of(R->x + R->vx / 75), cy = sy_of(R->y + R->vy / 75);
  float co = cosf(R->trail_rot), si = sinf(R->trail_rot);
  float oy = b->oy / 100.0f;   /* the anchor at (1, 0.5 + offsetY) */
  float ext = len + wid;
  int y0 = (int)floorf(cy - ext), y1 = (int)ceilf(cy + ext);
  if (y0 < top) y0 = top;
  if (y1 > top + BAND) y1 = top + BAND;
  bool donut = R->ball == 13;   /* the Donut's hollow V */
  for (int j = y0; j < y1; j++)
    for (int i = (int)floorf(cx - ext); i <= (int)ceilf(cx + ext); i++) {
      if ((unsigned)i >= W) continue;
      float dx = i + 0.5f - cx, dy = j + 0.5f - cy;
      float along = dx * co + dy * si, across = -dx * si + dy * co;
      float u = 1 + along / len, v = across / wid + 0.5f + oy;
      if (u < 0 || u > 1 || v < 0 || v > 1 || fabsf(v - 0.5f) > u * 0.5f) continue;
      if (donut && u > 0.5f && fabsf(v - 0.5f) < (u - 0.5f) * (1 - 16 / d)) continue;
      plot(i, j, rgb888(trail_color(b, u, v)));
    }
}

static void coins(void) {
  float base = 43 * zoom * S;
  for (int i = 0; i < DN_COINS; i++) {
    const dn_coin *c = &R->coin[i];
    if (!c->on) continue;
    float s = base;
    if (c->shrink) s *= 1 - back_in(1 - c->shrink / 6.0f);
    float x = sx_of(c->x), y = sy_of(c->y);
    if (x + s < 0 || x - s > W || y + s < top || y - s > top + BAND) continue;
    raw_scaled(&art_coin, x - s / 2, y - s / 2, s, s, 256);
  }
}

/* the guide (Guide at world (800, 80)) and its words, until the first press */
static void guide(void) {
  float x = sx_of(800), y = sy_of(80), s = zoom;
  mask_scaled(&art_guide, x, y, s, WHITE, 256);
  static const char *const words[3][3] = {{"Hold", "=", "Speed"}, {"Release", "=", "Fly"}, {"Hold", "=", "Dive"}};
  static const int at[3] = {85, 244, 420};
  for (int k = 0; k < 3; k++)
    for (int l = 0; l < 3; l++) {
      int cx = (int)sx_of(800 + at[k]), cy = (int)(sy_of(80 + 245) + (l - 1) * 12 * s + 5);
      center(&art_f13, cx, cy, words[k][l], WHITE, 256);
    }
}

/* "Perfect Slide! +3", swinging and growing, flying in from the right and out again */
static uint8_t slide_buf[200 * 30];
static int slide_w, slide_h, slide_kind = -1;
static void slide_popup(void) {
  if (!R->slide_t) return;
  const dn_font *f = &art_f21;
  if (slide_kind != R->slide_kind) {
    slide_kind = R->slide_kind;
    const char *s = R->slide_kind == DN_POP_PERFECT ? "Perfect Slide! +3" : "Nice Slide! +1";
    slide_w = text_width(f, s) + 2, slide_h = f->base + 8;
    if (slide_w > 200) slide_w = 200;
    memset(slide_buf, 0, sizeof slide_buf);
    int x = 1;
    for (const char *p = s; *p; p++) {
      int gi = glyph_of(f, *p);
      if (gi < 0) continue;
      const dn_glyph *g = &f->g[gi];
      for (int j = 0; j < g->h; j++)
        for (int i = 0; i < g->w; i++) {
          int xx = x + g->x + i, yy = f->base + 1 + g->y + j;
          if (xx >= 0 && xx < slide_w && yy >= 0 && yy < slide_h) slide_buf[yy * slide_w + xx] = (uint8_t)nib(f->px + g->off, (unsigned)(j * g->w + i));
        }
      x += g->adv;
    }
  }
  int t = R->slide_t, hold = R->slide_hold;
  float px, py, alpha;
  if (t <= 12) {
    float e = cubic_out(t / 12.0f);
    px = DN_W + (DN_W / 2 - DN_W) * e, py = R->slide_y0 + (DN_TY + 100 - R->slide_y0) * e;
    alpha = fminf(1, back_out(t / 12.0f));
  } else if (t <= 12 + hold) {
    px = DN_W / 2, py = DN_TY + 100, alpha = 1;
  } else {
    float e = cubic_out((t - 12 - hold) / 12.0f);
    px = DN_W / 2 + (DN_W - DN_W / 2) * e, py = DN_TY + 100 + (R->slide_y1 - DN_TY - 100) * e;
    alpha = 1 - e;
  }
  if (alpha <= 0) return;
  uint32_t ms = R->frame * 50 / 3;
  float angle = (-30 + 60 * yoyo(ms, 2000)) * 3.14159265f / 180, scale = (1 + 0.2f * yoyo(ms, 2000)) * 60 / 60.0f;
  float cx = px * S, cy = py * S, co = cosf(angle), si = sinf(angle);
  float r = (slide_w * scale) * 0.55f + 4;
  int y0 = (int)(cy - r), y1 = (int)(cy + r) + 1;
  if (y0 < top) y0 = top;
  if (y1 > top + BAND) y1 = top + BAND;
  for (int j = y0; j < y1; j++)
    for (int i = (int)(cx - r); i <= (int)(cx + r); i++) {
      if ((unsigned)i >= W) continue;
      float dx = (i + 0.5f - cx) / scale, dy = (j + 0.5f - cy) / scale;
      float u = dx * co + dy * si + slide_w / 2.0f, v = -dx * si + dy * co + slide_h / 2.0f;
      if (u < 0 || v < 0 || u >= slide_w || v >= slide_h) continue;
      unsigned a = slide_buf[(int)v * slide_w + (int)u];
      if (a) blend(i, j, YELLOW, (unsigned)(a * 256 * alpha / 15));
    }
}

static void world(void) {
  int land = R->land;
  sky(land);
  layers(land);
  dunes(land);
  trail();
  float bs = 56 * zoom * S;
  raw_turned(art_balls[R->ball], sx_of(R->x), sy_of(R->y), bs, R->rotates ? R->angle : 0, 256);
  if (!R->guide) {   /* the line: a 20-point bar at y 200, at alpha 0.7 */
    float y0 = sy_of(190), y1 = sy_of(210);
    uint16_t c = rgb888(art_land_info_[land].line);
    for (int j = top; j < top + BAND; j++) {
      float cov = fminf(j + 1, y1) - fmaxf(j, y0);
      if (cov > 0) rect(0, j, W, 1, c, (unsigned)(179 * fminf(cov, 1)));
    }
  }
  coins();
}

/* ---------------------------------------------------------------- the score, the coins, the best */

static void digits(const dn_font *f, int cx, int y, uint32_t v, bool centred) {
  char s[12];
  num(s, v);
  if (centred) center(f, cx, y, s, WHITE, 256);
  else right(f, cx, y, s, WHITE, 256);
}

static void balance(bool best) {
  /* best and coins at the top right: crown at (W - 70, 70), coin at (W - 70, 120), numbers right of W - 75 */
  if (best) {
    mask(&art_crown, 295, 18, WHITE, 256);
    digits(&art_d18, 292, 31, V->best, false);
  }
  raw_scaled(&art_coin, 295, 35, 15, 15, 256);
  digits(&art_d18, 292, 49, V->coins, false);
}

static void hud(void) {
  if (R->guide) return;
  digits(&art_d50, 160, 37, R->score, true);
  balance(true);
  if (R->line_t) {   /* "+N" by the line: in 200 ms, 300 ms, out 200 ms */
    int t = 42 - R->line_t;
    float a = t < 12 ? cubic_out(t / 12.0f) : t < 30 ? 1 : 1 - cubic_out((t - 30) / 12.0f);
    char s[4] = {'+', (char)('0' + R->line_n), 0};
    float y = (200 * zoom - camy) * S - 50 * S;
    center(&art_f39, (int)(400 * S), (int)(y + art_f39.base / 2), s, WHITE, (unsigned)(a * 256));
  }
}

/* ---------------------------------------------------------------- the menus */

static void keyline(const char *s) {
  if (top + BAND <= KEYS_Y) return;
  for (int j = top < KEYS_Y ? KEYS_Y : top; j < top + BAND; j++) {   /* the band under it: a quarter as bright */
    uint16_t *row = band + (j - top) * W;
    for (int i = 0; i < W; i++) row[i] = (uint16_t)((row[i] >> 2) & 0x39E7);
  }
  center(&art_f13, W / 2, H - 4, s, MUTED, 256);
}

static void button(int cx, int cy, int w, int h, const char *s, bool on) {
  round_rect(cx - w / 2, cy - h / 2, w, h, 4, 0, C(0x071B3C), 256);
  center(&art_f21, cx, cy + art_f21.base / 2 - 1, s, WHITE, 256);
  if (on) focus(cx - w / 2, cy - h / 2, w, h, 4);
}

static void title(void) {
  dim(128);   /* SceneOverlay: black at 0.5 */
  float s = (1.2f + 0.05f * yoyo(V->clock, 4000)) / 1.2f, a = (-3 + 6 * yoyo(V->clock, 2000)) * 3.14159265f / 180;
  /* the logo, turning a little and breathing */
  {
    const dn_mask *m = &art_logo;
    float cx = 450 * S + art_logo_dx, cy = (332 - 150) * S + art_logo_dy, co = cosf(a), si = sinf(a);
    float r = m->w * s * 0.55f + 2;
    for (int j = (int)(cy - r) < top ? top : (int)(cy - r); j < top + BAND && j <= cy + r; j++)
      for (int i = (int)(cx - r); i <= cx + r; i++) {
        float dx = (i + 0.5f - cx) / s, dy = (j + 0.5f - cy) / s;
        float u = dx * co + dy * si + m->w / 2.0f, v = -dx * si + dy * co + m->h / 2.0f;
        if (u < 0 || v < 0 || u >= m->w || v >= m->h) continue;
        unsigned k = nib(m->px, (unsigned)((int)v * m->w + (int)u));
        if (k) blend(i, j, WHITE, k * 256 / 15);
      }
  }
  int py = (int)((332 + 50) * S);
  raw_scaled(&art_shop, (450 - 140) * S - 30, py - 30, 60, 60, 256);
  raw_scaled(&art_play, (450 + 140) * S - 30, py - 30, 60, 60, 256);
  ring(25, 25, 10, 12, WHITE, 256);   /* how to play (the game's info button has no picture here) */
  center(&art_f21, 25, 25 + art_f21.base / 2 - 1, "?", WHITE, 256);
  if (V->sel == TITLE_SHOP) ring((450 - 140) * S, py, 31.5f, 34.5f, FOCUS, 256);
  if (V->sel == TITLE_PLAY) ring((450 + 140) * S, py, 31.5f, 34.5f, FOCUS, 256);
  if (V->sel == TITLE_HELP) ring(25, 25, 13.5f, 16.5f, FOCUS, 256);
  balance(true);
  keyline("OK select   Arrows move   Back quit");
}

static void help(void) {
  dim(160);
  center(&art_f32, W / 2, 44, "Instructions", WHITE, 256);
  int y = paragraph(&art_f13, W / 2, 70, 270, 15,
                    "Hold OK, EXE or Shift to speed down the dunes. Let go to fly, and hold again in the air to dive. "
                    "Land smoothly on the way down: landing hard on a slope going up ends the run.",
                    WHITE);
  y = paragraph(&art_f13, W / 2, y + 4, 270, 15,
                "Fly over the line for 1 to 3 points and coins. Slide fast down a whole dune for a Perfect Slide. "
                "Spend coins on balls and landscapes in the shop.",
                WHITE);
  y += 6;
  center(&art_f13, W / 2, y, "Inspired by Dune! by Team Lava, not affiliated.", MUTED, 256);
  center(&art_f13, W / 2, y + 14, "Brought to NumPlay by Bobydeluxe.", MUTED, 256);
  center(&art_f13, W / 2, y + 28, "Made by Mason Chen as part of NumPlay.", MUTED, 256);
  keyline("OK or Back return");
}

static void paused(void) {
  dim(110);
  static const char *const items[PAUSE_ITEMS] = {"Resume", "Restart", "Shop", "How to play", "Quit game"};
  for (int i = 0; i < PAUSE_ITEMS; i++) {
    int y = 52 + i * 32;
    bool on = V->sel == i;
    int w = text_width(&art_f21, items[i]) + 28;
    if (on) round_rect(W / 2 - w / 2, y - 12, w, 26, 6, 0, INK, 200);
    outlined(&art_f21, W / 2, y + art_f21.base / 2 - 1, items[i], WHITE, 256);
    if (on) focus(W / 2 - w / 2, y - 12, w, 26, 6);
  }
  keyline("Up/Down choose   OK select   Back resume");
}

static void prompt_panel(int h) {
  dim(204);   /* the overlay: black at 0.8 */
  round_rect(160 - 85, 118 - h / 2, 171, h, 0, 0, C(0x0C2B61), 256);
}

static void quit(void) {
  prompt_panel(100);
  center(&art_f21, W / 2, 101, "Quit the game?", WHITE, 256);
  button(160 - 36, 136, 53, 25, "No", V->sel == 0);
  button(160 + 36, 136, 53, 25, "Yes", V->sel == 1);
  keyline("Arrows choose   OK select   Back no");
}

static void result(void) {
  dim(204);
  center(&art_f32, W / 2, (int)(312 * S) + 11, "You Crashed!", RED, 256);
  center(&art_f21, W / 2, (int)(482 * S) + 8, "Press OK to retry", WHITE, 256);
  /* the score, the best and the coins stay over it */
  digits(&art_d50, 160, 37, R->score, true);
  balance(true);
  keyline("OK retry   Back menu");
}

/* ---------------------------------------------------------------- the shop */

#define CARD_W 53
#define CARD_H 71
#define LCARD_W 37
#define LCARD_H 137
static int shop_scroll;   /* the balls' first column shown */

static bool owned(int cat, int i) { return (cat == SHOP_BALLS ? V->owned_balls : V->owned_lands) >> i & 1; }

static void price_tag(int cx, int y, uint32_t price) {
  char s[8];
  num(s, price);
  int w = text_width(&art_f13, s) + 13;
  text(&art_f13, cx - w / 2, y, s, WHITE, 256);
  raw_scaled(&art_coin, cx + w / 2 - 12, y - 10, 12, 12, 256);
}

static void side_bar(void) {
  /* the side bar (#607589 at 0.9) with the two categories */
  rect(0, top, 50, BAND, C(0x607589), 230);
  float bs = V->shop_cat == SHOP_BALLS ? 1.2f : 1, ls = V->shop_cat == SHOP_LANDS ? 1.2f : 1;
  mask_scaled(&art_cat_balls, 25 - 20 * bs, (332 - 80) * S - 20 * bs, bs, WHITE, 256);
  mask_scaled(&art_cat_lands, 25 - 20 * ls, (332 + 120) * S - 20 * ls, ls, WHITE, 256);
  if (V->shop_side) {
    float cy = V->shop_cat == SHOP_BALLS ? (332 - 80) * S : (332 + 120) * S;
    ring(25, cy, 25, 28, FOCUS, 256);
  }
}

static void shop(void) {
  dim(128);
  /* the title: a soft white ribbon (Shop_Title_2) with "Shop" */
  for (int j = 5; j < 31; j++)
    if (j >= top && j < top + BAND)
      for (int i = 52; i < W; i++) {
        float e = fminf(1, fminf((i - 52) / 24.0f, (W - i) / 24.0f));
        blend(i, j, WHITE, (unsigned)(94 * e));
      }
  center(&art_f21, 188, 26, "Shop", WHITE, 256);
  raw_scaled(&art_coin, 295, 35, 15, 15, 256);
  digits(&art_d18, 292, 49, V->coins, false);
  if (V->shop_cat == SHOP_BALLS) {
    int col = V->shop_sel % 13;
    if (col < shop_scroll) shop_scroll = col;
    if (col > shop_scroll + 3) shop_scroll = col - 3;
    for (int i = 0; i < DN_BALLS; i++) {
      int c = i % 13, r = i / 13;
      int cx = 60 + 30 + (c - shop_scroll) * 60, cy = (int)((342 - 80 + 220 * r) * S);
      int x = cx - CARD_W / 2, y = cy - CARD_H / 2;
      if (x + CARD_W <= 52 || x >= W) continue;
      bool own = owned(SHOP_BALLS, i);
      const dn_raw *b = art_shopballs[i];
      if (!own) raw_scaled(b, cx - 14, cy - 5 - 14, 28, 28, 256);   /* behind the card when locked */
      rect(x, y, CARD_W, CARD_H, C(0x425367), 204);
      round_rect(x, y, CARD_W, CARD_H, 0, 1, C(0x2D3738), 256);
      if (own) raw_scaled(b, cx - 14, cy - 5 - 14, 28, 28, 256);
      else if (x >= 52 && x + CARD_W <= W) mask(&art_lock, cx - 8, cy - 11, WHITE, 256), price_tag(cx, cy + 30, art_ball_info_[i].price);
      if (i == V->ball) round_rect(x - 1, y - 1, CARD_W + 2, CARD_H + 2, 0, 2, WHITE, 256);
      if (!V->shop_side && i == V->shop_sel) focus(x - 1, y - 1, CARD_W + 2, CARD_H + 2, 2);
    }
  } else {
    for (int i = 0; i < DN_LANDS; i++) {
      int cx = (int)((200 + 107.5f * i) * S + 0.5f), cy = (int)(372 * S);
      int x = cx - LCARD_W / 2, y = cy - LCARD_H / 2;
      rle_scaled(art_cards[i], (float)x, (float)y, 1, 256);
      if (!owned(SHOP_LANDS, i)) {
        rect(x + 2, y + 2, LCARD_W - 4, LCARD_H - 10, BLACK, 128);
        mask(&art_lock, cx - 8, cy - 11, WHITE, 256);
        price_tag(cx, cy + 40, art_land_info_[i].price);
      }
      if (i == V->land) round_rect(x + 1, y + 1, LCARD_W - 2, LCARD_H - 4, 0, 2, WHITE, 256);
      if (!V->shop_side && i == V->shop_sel) focus(x, y, LCARD_W, LCARD_H, 2);
    }
  }
  side_bar();   /* (the cards slide under it) */
}

static const char *shop_keys(char *buf) {
  if (V->shop_side) return "Up/Down category   Right choose   Back close";
  int cat = V->shop_cat, i = V->shop_sel;
  uint32_t price = cat == SHOP_BALLS ? art_ball_info_[i].price : art_land_info_[i].price;
  bool eq = cat == SHOP_BALLS ? i == V->ball : i == V->land;
  if (owned(cat, i)) return eq ? "In use   Arrows move   Back close" : "OK use   Arrows move   Back close";
  if (V->coins >= price) return "OK buy   Arrows move   Back close";
  char *p = buf;
  memcpy(p, "Need ", 5), p = num(p + 5, price);
  memcpy(p, " coins   Arrows move   Back close", 34);
  return buf;
}

static void buy(void) {
  prompt_panel(174);
  int cat = V->shop_cat, i = V->shop_sel;
  char s[64], *p = s;
  const char *name = cat == SHOP_BALLS ? art_ball_info_[i].name : art_land_info_[i].name;
  uint32_t price = cat == SHOP_BALLS ? art_ball_info_[i].price : art_land_info_[i].price;
  memcpy(p, "Buy ", 4), p += 4;
  size_t n = strlen(name);
  memcpy(p, name, n), p += n;
  memcpy(p, " for ", 5), p = num(p + 5, price);
  memcpy(p, " coins?", 8);
  paragraph(&art_f13, W / 2, 50, 156, 14, s, WHITE);
  rect(160 - 74, 118 - 43, 149, 87, C(0x071A3A), 256);
  if (cat == SHOP_BALLS) raw_scaled(art_shopballs[i], 160 - 26, 118 - 26, 52, 52, 256);
  else rle_scaled(art_previews[i], 160 - 70, 118 - 39, 1, 256);
  button(160 - 36, 184, 53, 25, "Yes", V->buy_yes);
  button(160 + 36, 184, 53, 25, "No", !V->buy_yes);
  keyline("Arrows choose   OK select   Back no");
}

/* ---------------------------------------------------------------- a frame */

static char keybuf[64];
static void scene(void) {
  switch (V->screen) {
    case V_SHOP:
    case V_BUY:
      world();
      shop();
      if (V->screen == V_BUY) buy();
      else keyline(shop_keys(keybuf));
      break;
    case V_TITLE:
      world();
      title();
      break;
    case V_HELP:
      world();
      help();
      break;
    case V_PLAY:
      world();
      if (R->guide) guide();
      hud();
      slide_popup();
      if (R->flash_t) {   /* the white flash: to 0.4 in 150 ms and back */
        int t = 18 - R->flash_t;
        float a = 0.4f * (t < 9 ? cubic_in(t / 9.0f) : cubic_in((18 - t) / 9.0f));
        rect(0, top, W, BAND, WHITE, (unsigned)(a * 256));
      }
      keyline(R->alive ? "Hold OK to speed up and dive   Back pause" : "");
      break;
    case V_PAUSE:
      world();
      paused();
      break;
    case V_QUIT:
      world();
      quit();
      break;
    case V_RESULT:
      world();
      result();
      break;
  }
}

void dn_draw(const dn_view *v, dn_push_fn push) {
  V = v, R = v->run;
  zoom = R->zoom, camx = R->x - 150 / zoom, camy = R->cam_y;
  shake_x = shake_y = 0;
  if (R->shake_t) {   /* camera.shake(0.01, 100): up to 9 points each way */
    uint32_t h = R->frame * 2654435761u;
    shake_x = ((float)(h >> 8 & 255) / 255 * 2 - 1) * 9 * S, shake_y = ((float)(h >> 16 & 255) / 255 * 2 - 1) * 6.65f * S;
  }
  dunes_prepare();
  for (top = 0; top < H; top += BAND) {
    scene();
    uint32_t sum = 0;
    if (v->screen != V_PLAY)   /* (while playing every strip changes) */
      for (int i = 0; i < W * BAND; i++) sum = (sum ^ band[i]) * 16777619u;
    if (!fresh || v->screen == V_PLAY || sum != sums[top / BAND]) {
      sums[top / BAND] = sum;
      push(0, top, W, BAND, band);
    }
  }
  fresh = true;
}
