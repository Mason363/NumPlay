#include "gfx.h"
#include <math.h>
#include <string.h>
#include "../../common/np_text.h"

#if PLATFORM_DEVICE
uint16_t *gfx_strip; /* main() points it at a strip on its stack */
#else
static uint16_t host_strip[GFX_W * STRIP_H] __attribute__((aligned(8)));
uint16_t *gfx_strip = host_strip;
#endif
int gfx_y0 = 0, gfx_y1 = STRIP_H;
static int clip_x0 = 0, clip_y0 = 0, clip_x1 = GFX_W, clip_y1 = GFX_H;

void gfx_begin_strip(int index) { gfx_y0 = index * STRIP_H; gfx_y1 = gfx_y0 + STRIP_H; }
void gfx_clip(int x0, int y0, int x1, int y1) {
  clip_x0 = x0 < 0 ? 0 : x0; clip_y0 = y0 < 0 ? 0 : y0;
  clip_x1 = x1 > GFX_W ? GFX_W : x1; clip_y1 = y1 > GFX_H ? GFX_H : y1;
}
void gfx_unclip(void) { clip_x0 = 0; clip_y0 = 0; clip_x1 = GFX_W; clip_y1 = GFX_H; }

static inline bool clip_rect(int *x, int *y, int *w, int *h) {
  int x0 = *x, y0 = *y, x1 = *x + *w, y1 = *y + *h;
  int cy0 = clip_y0 > gfx_y0 ? clip_y0 : gfx_y0, cy1 = clip_y1 < gfx_y1 ? clip_y1 : gfx_y1;
  if (x0 < clip_x0) x0 = clip_x0;
  if (x1 > clip_x1) x1 = clip_x1;
  if (y0 < cy0) y0 = cy0;
  if (y1 > cy1) y1 = cy1;
  if (x0 >= x1 || y0 >= y1) return false;
  *x = x0; *y = y0; *w = x1 - x0; *h = y1 - y0;
  return true;
}
static inline uint16_t *pix(int x, int y) { return gfx_strip + (y - gfx_y0) * GFX_W + x; }
static inline unsigned a32_of(unsigned a256) { return a256 >= 256 ? 32 : (a256 + 4) >> 3; }
/* Sets n pixels from p to c, two per store once aligned. */
typedef uint32_t __attribute__((may_alias)) u32_alias;
static inline void fill_span(uint16_t *p, int n, color_t c) {
  if (n <= 0) return;
  if ((uintptr_t)p & 2) { *p++ = c; n--; }
  uint32_t cc = c | (uint32_t)c << 16;
  u32_alias *q = (u32_alias *)p;
  int m = n >> 1;
  for (; m >= 4; m -= 4, q += 4) { q[0] = cc; q[1] = cc; q[2] = cc; q[3] = cc; }
  for (; m > 0; m--) *q++ = cc;
  if (n & 1) *(uint16_t *)q = c;
}
/* n pixels blended normally with c at a32 (0..32), as put() does. */
static inline void mix_span(uint16_t *p, int n, color_t c, unsigned a32) {
  if (!a32) return;
  if (a32 >= 32) { fill_span(p, n, c); return; }
  for (int i = 0; i < n; i++) p[i] = px_mix(p[i], c, a32);
}

color_t c_mix(color_t a, color_t b, unsigned t) {
  if (t >= 256) return b;
  return rgb((c_r(a) * (256 - t) + c_r(b) * t) >> 8, (c_g(a) * (256 - t) + c_g(b) * t) >> 8,
             (c_b(a) * (256 - t) + c_b(b) * t) >> 8);
}
color_t c_scale(color_t a, unsigned t) {
  if (t >= 256) return a;
  return rgb(c_r(a) * t >> 8, c_g(a) * t >> 8, c_b(a) * t >> 8);
}
color_t c_hsv_lighten(color_t c, int sat_delta, int val_delta) {
  float r = c_r(c) / 255.f, g = c_g(c) / 255.f, b = c_b(c) / 255.f;
  float mx = r > g ? (r > b ? r : b) : (g > b ? g : b), mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
  float d = mx - mn, h = 0, s = mx > 0 ? d / mx : 0, v = mx;
  if (d > 0) {
    if (mx == r) h = (g - b) / d; else if (mx == g) h = 2 + (b - r) / d; else h = 4 + (r - g) / d;
    h *= 60; if (h < 0) h += 360;
  }
  s += sat_delta / 100.f; v += val_delta / 100.f;
  if (s < 0) s = 0; if (s > 1) s = 1; if (v < 0) v = 0; if (v > 1) v = 1;
  float C = v * s, X = C * (1 - fabsf(fmodf(h / 60, 2) - 1)), m = v - C, rr, gg, bb;
  int sector = (int)(h / 60) % 6;
  switch (sector) {
    case 0: rr = C; gg = X; bb = 0; break;
    case 1: rr = X; gg = C; bb = 0; break;
    case 2: rr = 0; gg = C; bb = X; break;
    case 3: rr = 0; gg = X; bb = C; break;
    case 4: rr = X; gg = 0; bb = C; break;
    default: rr = C; gg = 0; bb = X; break;
  }
  return rgb((unsigned)((rr + m) * 255 + .5f), (unsigned)((gg + m) * 255 + .5f), (unsigned)((bb + m) * 255 + .5f));
}

void gfx_fill(int x, int y, int w, int h, color_t c) {
  if (!clip_rect(&x, &y, &w, &h)) return;
  for (int j = y; j < y + h; j++) fill_span(pix(x, j), w, c);
}
void gfx_blend(int x, int y, int w, int h, color_t c, unsigned alpha) {
  unsigned a = a32_of(alpha);
  if (!a) return;
  if (a >= 32) { gfx_fill(x, y, w, h, c); return; }
  if (!clip_rect(&x, &y, &w, &h)) return;
  for (int j = y; j < y + h; j++) {
    uint16_t *p = pix(x, j);
    for (int i = 0; i < w; i++) p[i] = px_mix(p[i], c, a);
  }
}
void gfx_add(int x, int y, int w, int h, color_t c, unsigned alpha) {
  unsigned a = a32_of(alpha);
  if (!a || !clip_rect(&x, &y, &w, &h)) return;
  for (int j = y; j < y + h; j++) {
    uint16_t *p = pix(x, j);
    for (int i = 0; i < w; i++) p[i] = px_add(p[i], c, a);
  }
}
void gfx_vgrad(int x, int y, int w, int h, color_t top, color_t bottom) {
  int x0 = x, y0 = y, w0 = w, h0 = h;
  if (h0 <= 0 || !clip_rect(&x, &y, &w, &h)) return;
  for (int j = y; j < y + h; j++) {
    color_t c = c_mix(top, bottom, h0 > 1 ? (unsigned)((j - y0) * 256 / (h0 - 1)) : 0);
    fill_span(pix(x, j), w, c);
  }
  (void)x0; (void)w0;
}
void gfx_vgrad_alpha(int x, int y, int w, int h, color_t c, unsigned at, unsigned ab, int mode) {
  int y0 = y, h0 = h;
  if (h0 <= 0 || !clip_rect(&x, &y, &w, &h)) return;
  for (int j = y; j < y + h; j++) {
    unsigned a = a32_of(h0 > 1 ? (at * (unsigned)(h0 - 1 - (j - y0)) + ab * (unsigned)(j - y0)) / (unsigned)(h0 - 1) : at);
    if (!a) continue;
    uint16_t *p = pix(x, j);
    if (mode == BLEND_ADD) for (int i = 0; i < w; i++) p[i] = px_add(p[i], c, a);
    else for (int i = 0; i < w; i++) p[i] = px_mix(p[i], c, a);
  }
}
void gfx_hgrad_alpha(int x, int y, int w, int h, color_t c, unsigned al, unsigned ar, int mode) {
  int x0 = x, w0 = w;
  if (w0 <= 0 || !clip_rect(&x, &y, &w, &h)) return;
  /* the alpha of a column is the same on every row */
  uint8_t acol[GFX_W];
  for (int i = 0; i < w; i++) {
    int k = x + i - x0;
    acol[i] = (uint8_t)a32_of(w0 > 1 ? (al * (unsigned)(w0 - 1 - k) + ar * (unsigned)k) / (unsigned)(w0 - 1) : al);
  }
  for (int j = y; j < y + h; j++) {
    uint16_t *p = pix(x, j);
    if (mode == BLEND_ADD) {
      for (int i = 0; i < w; i++) if (acol[i]) p[i] = px_add(p[i], c, acol[i]);
    } else {
      for (int i = 0; i < w; i++) if (acol[i]) p[i] = px_mix(p[i], c, acol[i]);
    }
  }
}
void gfx_scale_rect(int x, int y, int w, int h, unsigned keep) {
  unsigned k = a32_of(keep);
  if (k >= 32 || !clip_rect(&x, &y, &w, &h)) return;
  for (int j = y; j < y + h; j++) {
    uint16_t *p = pix(x, j);
    for (int i = 0; i < w; i++) p[i] = px_mix(0, p[i], k);
  }
}

static inline void put(uint16_t *p, color_t c, unsigned a32, int mode) {
  if (!a32) return;
  if (mode == BLEND_ADD) *p = px_add(*p, c, a32);
  else *p = a32 >= 32 ? c : px_mix(*p, c, a32);
}

/* Discs and rings are processed as per-row spans; only the one-pixel
 * anti-aliased edge needs a square root. */
void gfx_disc(int cx16, int cy16, int r16, color_t c, unsigned alpha, int mode) {
  if (r16 <= 0 || !alpha) return;
  float cx = cx16 / 16.f, cy = cy16 / 16.f, r = r16 / 16.f;
  int x = (int)floorf(cx - r - 1), y = (int)floorf(cy - r - 1), w = (int)(2 * r + 3), h = w;
  if (!clip_rect(&x, &y, &w, &h)) return;
  const unsigned full = a32_of(alpha);
  const float ro = r + .5f, ri = r - .5f;
  for (int j = y; j < y + h; j++) {
    float dy = j + .5f - cy, dy2 = dy * dy;
    if (dy2 >= ro * ro) continue;
    float half = sqrtf(ro * ro - dy2);
    int i0 = (int)floorf(cx - half - .5f), i1 = (int)ceilf(cx + half + .5f);
    if (i0 < x) i0 = x;
    if (i1 > x + w) i1 = x + w;
    int k0 = i1, k1 = i1;   /* fully covered interior */
    if (ri > 0 && dy2 < ri * ri) {
      float in = sqrtf(ri * ri - dy2);
      k0 = (int)ceilf(cx - in + .5f);
      k1 = (int)floorf(cx + in - .5f) + 1;
      if (k0 < i0) k0 = i0;
      if (k1 > i1) k1 = i1;
      if (k1 < k0) k1 = k0;
    }
    uint16_t *p = pix(0, j);
    for (int i = i0; i < i1; i++) {
      if (i == k0 && k1 > k0) {
        for (; i < k1; i++) put(p + i, c, full, mode);
        if (i >= i1) break;
      }
      float dx = i + .5f - cx, d = sqrtf(dx * dx + dy2), cov = r - d + .5f;
      if (cov <= 0) continue;
      if (cov > 1) cov = 1;
      put(p + i, c, a32_of((unsigned)(cov * alpha)), mode);
    }
  }
}

void gfx_ring(int cx16, int cy16, int r16, int t16, color_t c, unsigned alpha, int mode) {
  if (r16 <= 0 || t16 <= 0 || !alpha) return;
  float cx = cx16 / 16.f, cy = cy16 / 16.f, r = r16 / 16.f, t = t16 / 32.f;
  int x = (int)floorf(cx - r - t - 1), y = (int)floorf(cy - r - t - 1), w = (int)(2 * (r + t) + 3), h = w;
  if (!clip_rect(&x, &y, &w, &h)) return;
  const float ro = r + t + .5f, ri = r - t - .5f;
  for (int j = y; j < y + h; j++) {
    float dy = j + .5f - cy, dy2 = dy * dy;
    if (dy2 >= ro * ro) continue;
    float out = sqrtf(ro * ro - dy2), in = ri > 0 && dy2 < ri * ri ? sqrtf(ri * ri - dy2) : -1;
    /* the band is [cx-out, cx-in] and [cx+in, cx+out] (one span if in < 0) */
    float spans[2][2] = {{cx - out, in >= 0 ? cx - in : cx + out}, {cx + in, cx + out}};
    int nspans = in >= 0 ? 2 : 1;
    uint16_t *p = pix(0, j);
    for (int sidx = 0; sidx < nspans; sidx++) {
      int i0 = (int)floorf(spans[sidx][0] - .5f), i1 = (int)ceilf(spans[sidx][1] + .5f);
      if (i0 < x) i0 = x;
      if (i1 > x + w) i1 = x + w;
      for (int i = i0; i < i1; i++) {
        float dx = i + .5f - cx, d = sqrtf(dx * dx + dy2), cov = t - fabsf(d - r) + .5f;
        if (cov <= 0) continue;
        if (cov > 1) cov = 1;
        put(p + i, c, a32_of((unsigned)(cov * alpha)), mode);
      }
    }
  }
}
void gfx_round_rect(int x, int y, int w, int h, int r, color_t c, unsigned alpha) {
  int x0 = x, y0 = y, w0 = w, h0 = h;
  if (!clip_rect(&x, &y, &w, &h)) return;
  const unsigned full = a32_of(alpha);
  for (int j = y; j < y + h; j++) {
    uint16_t *p = pix(x, j);
    int ry = j - y0 < r ? r - (j - y0) : (j - y0 >= h0 - r ? (j - y0) - (h0 - r - 1) : 0);
    if (!ry) { mix_span(p, w, c, full); continue; }   /* no corner on this row */
    for (int i = 0; i < w; i++) {
      int k = x + i - x0;
      int rx = k < r ? r - k : (k >= w0 - r ? k - (w0 - r - 1) : 0);
      unsigned a = alpha;
      if (rx && ry) {
        float d = sqrtf((float)((rx - .5f) * (rx - .5f) + (ry - .5f) * (ry - .5f)));
        float cov = r - d + .5f;
        if (cov <= 0) continue;
        if (cov < 1) a = (unsigned)(a * cov);
      }
      put(p + i, c, a32_of(a), BLEND_NORMAL);
    }
  }
}
void gfx_rectf(float x0, float y0, float x1, float y1, color_t c, unsigned alpha, int mode) {
  if (x1 <= x0 || y1 <= y0 || !alpha) return;
  int ix0 = (int)floorf(x0), iy0 = (int)floorf(y0), ix1 = (int)ceilf(x1), iy1 = (int)ceilf(y1);
  int x = ix0, y = iy0, w = ix1 - ix0, h = iy1 - iy0;
  if (!clip_rect(&x, &y, &w, &h)) return;
  /* columns: a partial pixel on each side, full coverage in between */
  int fx0 = (int)ceilf(x0), fx1 = (int)floorf(x1);
  float lcov = fx0 > x1 ? x1 - x0 : fx0 - x0, rcov = x1 - fx1;
  if (fx0 < x) fx0 = x;
  if (fx1 > x + w) fx1 = x + w;
  for (int j = y; j < y + h; j++) {
    float cy = fminf(y1, j + 1.f) - fmaxf(y0, (float)j);
    if (cy <= 0) continue;
    unsigned ra = cy >= 1 ? alpha : (unsigned)(cy * alpha), a = a32_of(ra);
    uint16_t *p = pix(0, j);
    if (ix0 >= x && ix0 < x + w && lcov > 0 && ix0 < (int)ceilf(x0)) put(p + ix0, c, a32_of((unsigned)(lcov * ra)), mode);
    if (fx1 >= x && fx1 < x + w && rcov > 0 && fx1 >= fx0) put(p + fx1, c, a32_of((unsigned)(rcov * ra)), mode);
    if (!a) continue;
    if (mode == BLEND_NORMAL && a >= 32) fill_span(p + fx0, fx1 - fx0, c);
    else if (mode == BLEND_ADD) for (int i = fx0; i < fx1; i++) p[i] = px_add(p[i], c, a);
    else for (int i = fx0; i < fx1; i++) p[i] = px_mix(p[i], c, a);
  }
}

void gfx_triangle(int x0, int y0, int x1, int y1, int x2, int y2, color_t c, unsigned alpha) {
  float X[3] = {(float)x0, (float)x1, (float)x2}, Y[3] = {(float)y0, (float)y1, (float)y2};
  float area = (X[1] - X[0]) * (Y[2] - Y[0]) - (X[2] - X[0]) * (Y[1] - Y[0]);
  if (area == 0) return;
  float nx[3], ny[3], nc[3];
  for (int k = 0; k < 3; k++) {
    int a = k, b = (k + 1) % 3;
    float ex = X[b] - X[a], ey = Y[b] - Y[a], L = sqrtf(ex * ex + ey * ey);
    float s = area > 0 ? 1.f : -1.f;
    nx[k] = -ey / L * s; ny[k] = ex / L * s; nc[k] = -(nx[k] * X[a] + ny[k] * Y[a]);
  }
  int bx0 = (int)floorf(fminf(X[0], fminf(X[1], X[2]))), bx1 = (int)ceilf(fmaxf(X[0], fmaxf(X[1], X[2])));
  int by0 = (int)floorf(fminf(Y[0], fminf(Y[1], Y[2]))), by1 = (int)ceilf(fmaxf(Y[0], fmaxf(Y[1], Y[2])));
  int x = bx0, y = by0, w = bx1 - bx0 + 1, h = by1 - by0 + 1;
  if (!clip_rect(&x, &y, &w, &h)) return;
  for (int j = y; j < y + h; j++) {
    uint16_t *p = pix(x, j);
    for (int i = 0; i < w; i++) {
      float px_ = x + i + .5f, py_ = j + .5f, m = 1e9f;
      for (int k = 0; k < 3; k++) { float d = nx[k] * px_ + ny[k] * py_ + nc[k]; if (d < m) m = d; }
      float cov = m + .5f;
      if (cov <= 0) continue;
      if (cov > 1) cov = 1;
      put(p + i, c, a32_of((unsigned)(cov * alpha)), BLEND_NORMAL);
    }
  }
}

/* ---------------------------------------------------------------- sprites */

typedef struct {
  const Sprite *s;
  const uint8_t *data;
  int stride;
  uint16_t col[16];
  uint8_t a32[16];
} Sampler;

static void sampler_init(Sampler *sm, int spr, color_t tint, unsigned alpha) {
  const Sprite *s = sprite_def(spr);
  bool lv = spr >= SPR_COUNT;
  sm->s = s;
  sm->data = (lv ? sprite_blob_lv : sprite_blob) + s->off;
  sm->stride = s->fmt == 1 ? s->w : (s->w + 1) / 2;
  unsigned al = alpha > 256 ? 256 : alpha;
  for (int k = 0; k < 16; k++) {
    if (s->fmt == 2) {
      sm->col[k] = (lv ? sprite_pal_rgb_lv : sprite_pal_rgb)[s->pal][k];
      if (tint != 0xffff) sm->col[k] = rgb(c_r(sm->col[k]) * c_r(tint) / 255, c_g(sm->col[k]) * c_g(tint) / 255, c_b(sm->col[k]) * c_b(tint) / 255);
      sm->a32[k] = (uint8_t)(((lv ? sprite_pal_a_lv : sprite_pal_a)[s->pal][k] * al * 32 + 15 * 128) / (15 * 256));
    } else {
      sm->col[k] = s->fmt == 1 ? c_scale(tint, (unsigned)(k * 256 / 15)) : tint;
      sm->a32[k] = (uint8_t)((k * al * 32 + 15 * 128) / (15 * 256));
    }
  }
}
/* Returns colour and alpha (0..32) of source texel (u, v). */
static inline unsigned sample(const Sampler *sm, int u, int v, uint16_t *c) {
  const uint8_t *row = sm->data + v * sm->stride;
  unsigned idx;
  if (sm->s->fmt == 1) {
    uint8_t b = row[u];
    *c = sm->col[b >> 4];
    return sm->a32[b & 15];
  }
  uint8_t b = row[u >> 1];
  idx = (u & 1) ? b >> 4 : b & 15;
  *c = sm->col[idx];
  return sm->a32[idx];
}

int gfx_sprite_w(int spr, int xf) { return (xf & 1) ? sprite_def(spr)->h : sprite_def(spr)->w; }
int gfx_sprite_h(int spr, int xf) { return (xf & 1) ? sprite_def(spr)->w : sprite_def(spr)->h; }

/* Where gfx_sprite puts a sprite: the anchor's offset in the turned image,
   and the turned image's size. */
static void sprite_place(const Sprite *s, int xf, int *tax, int *tay, int *dw, int *dh) {
  int w = s->w, h = s->h, rot = xf & XF_ROT;
  int ax = (xf & XF_FLIPX) ? w - s->ax : s->ax, ay = (xf & XF_FLIPY) ? h - s->ay : s->ay;
  *dw = w;
  *dh = h;
  switch (rot) {
    case 1: *tax = h - ay; *tay = ax; *dw = h; *dh = w; break;
    case 2: *tax = w - ax; *tay = h - ay; break;
    case 3: *tax = ay; *tay = w - ax; *dw = h; *dh = w; break;
    default: *tax = ax; *tay = ay; break;
  }
}
void gfx_sprite_rows(int spr, int y, int xf, int *y0, int *y1) {
  int tax, tay, dw, dh;
  sprite_place(sprite_def(spr), xf, &tax, &tay, &dw, &dh);
  *y0 = y - tay;
  *y1 = y - tay + dh;
}

void gfx_sprite(int spr, int x, int y, int xf, color_t tint, unsigned alpha, int mode) {
  if (spr < 0 || !alpha) return;
  const Sprite *s = sprite_def(spr);
  int w = s->w, h = s->h, rot = xf & XF_ROT;
  int tax, tay, dw, dh;
  sprite_place(s, xf, &tax, &tay, &dw, &dh);
  int dx0 = x - tax, dy0 = y - tay, cx = dx0, cy = dy0, cw = dw, ch = dh;
  if (!clip_rect(&cx, &cy, &cw, &ch)) return;
  Sampler sm;
  sampler_init(&sm, spr, tint, alpha);
  for (int j = cy; j < cy + ch; j++) {
    uint16_t *p = pix(cx, j);
    int dy = j - dy0;
    for (int i = 0; i < cw; i++) {
      int dx = cx + i - dx0, u, v;
      switch (rot) {
        case 1: u = dy; v = h - 1 - dx; break;
        case 2: u = w - 1 - dx; v = h - 1 - dy; break;
        case 3: u = w - 1 - dy; v = dx; break;
        default: u = dx; v = dy; break;
      }
      if (xf & XF_FLIPX) u = w - 1 - u;
      if (xf & XF_FLIPY) v = h - 1 - v;
      uint16_t c;
      unsigned a = sample(&sm, u, v, &c);
      put(p + i, c, a, mode);
    }
  }
}

/* One row of gfx_sprite_ex: n pixels from p, source position (u, v) in 16.16
   stepped by (sdu, sdv). Inlined for each format and blending mode. */
static inline __attribute__((always_inline)) void ex_span(uint16_t *p, int n, int32_t u, int32_t v, const int32_t sdu, const int32_t sdv,
                                                          const Sampler *sm, const int W, const int H, const unsigned al,
                                                          const int fmt, const int mode) {
  const uint8_t *data = sm->data;
  const int stride = sm->stride;
  for (; n > 0; n--, p++, u += sdu, v += sdv) {
    int32_t uu = u + 65536, vv = v + 65536;          /* offset so that -1 < u maps to >= 0 */
    if (uu < 0 || vv < 0) continue;
    int ui = (uu >> 16) - 1, vi = (vv >> 16) - 1;
    unsigned fu = (unsigned)(uu >> 8) & 255, fv = (unsigned)(vv >> 8) & 255;
    unsigned i0, i1, i2, i3;   /* texels (u, v), (u+1, v), (u, v+1), (u+1, v+1) */
    if (ui >= 0 && vi >= 0 && ui + 1 < W && vi + 1 < H) {
      if (fmt == 1) {
        const uint8_t *r0 = data + vi * stride + ui, *r1 = r0 + stride;
        i0 = r0[0]; i1 = r0[1]; i2 = r1[0]; i3 = r1[1];
      } else {
        const uint8_t *r0 = data + vi * stride + (ui >> 1), *r1 = r0 + stride;
        if (ui & 1) {
          i0 = r0[0] >> 4; i1 = r0[1] & 15u; i2 = r1[0] >> 4; i3 = r1[1] & 15u;
        } else {
          unsigned b0 = r0[0], b1 = r1[0];
          i0 = b0 & 15; i1 = b0 >> 4; i2 = b1 & 15; i3 = b1 >> 4;
        }
      }
    } else {
      unsigned idx[4];
      for (int k = 0; k < 4; k++) {
        int x = ui + (k & 1), y = vi + (k >> 1);
        if (x < 0 || y < 0 || x >= W || y >= H) { idx[k] = 0; continue; }
        const uint8_t *row = data + y * stride;
        idx[k] = fmt == 1 ? row[x] : (unsigned)((row[x >> 1] >> ((x & 1) * 4)) & 15);
      }
      i0 = idx[0]; i1 = idx[1]; i2 = idx[2]; i3 = idx[3];
    }
    if (i0 == i1 && i0 == i2 && i0 == i3) {
      /* uniform neighbourhood: no filtering needed */
      unsigned a = fmt == 1 ? sm->a32[i0 & 15] : sm->a32[i0];
      if (!a) continue;
      color_t c = fmt == 1 ? sm->col[i0 >> 4] : sm->col[i0];
      put(p, c, (a * al + 128) >> 8, mode);
      continue;
    }
    unsigned w00 = (256 - fu) * (256 - fv) >> 8, w10 = fu * (256 - fv) >> 8, w01 = (256 - fu) * fv >> 8, w11 = fu * fv >> 8;
    if (fmt == 0) {
      /* A4: one colour, only coverage is filtered */
      unsigned ta = sm->a32[i0] * w00 + sm->a32[i1] * w10 + sm->a32[i2] * w01 + sm->a32[i3] * w11;
      if (ta < 128) continue;
      unsigned a32 = (ta * al + 32768) >> 16;
      put(p, sm->col[0], a32 > 32 ? 32 : a32, mode);
      continue;
    }
    const unsigned ix[4] = {i0, i1, i2, i3}, wt[4] = {w00, w10, w01, w11};
    unsigned ta = 0, tr = 0, tg = 0, tb = 0;
    for (int k = 0; k < 4; k++) {
      unsigned a, cc;
      if (fmt == 1) { a = sm->a32[ix[k] & 15]; cc = sm->col[ix[k] >> 4]; } else { a = sm->a32[ix[k]]; cc = sm->col[ix[k]]; }
      a *= wt[k];
      if (!a) continue;
      ta += a;
      tr += ((cc >> 11) & 31) * a;
      tg += ((cc >> 5) & 63) * a;
      tb += (cc & 31) * a;
    }
    if (ta < 128) continue;
    color_t c = (color_t)(((tr / ta) << 11) | ((tg / ta) << 5) | (tb / ta));
    unsigned a32 = (ta * al + 32768) >> 16;
    put(p, c, a32 > 32 ? 32 : a32, mode);
  }
}

/* Rotated / scaled sprite with bilinear filtering. The inverse transform is
 * stepped in 16.16 fixed point; each row is clipped to the span where the
 * sample falls inside the texture. */
typedef struct { float sc, cs, sn, cx, cy; int bx, by, bw, bh; } ExGeom;
/* gfx_sprite_ex's transform and the box of the turned sprite (its flips do
   not change the box). */
static void ex_geom(const Sprite *s, int x16, int y16, int ang16, int scale256, ExGeom *g) {
  float sc = scale256 / 256.f, ang = ang16 / 16.f * 3.14159265f / 180.f;
  float cs = cosf(ang), sn = sinf(ang), cx = x16 / 16.f, cy = y16 / 16.f;
  float w = s->w, h = s->h, ax = s->ax, ay = s->ay;
  float corners[4][2] = {{-ax, -ay}, {w - ax, -ay}, {-ax, h - ay}, {w - ax, h - ay}};
  float mnx = 1e9f, mny = 1e9f, mxx = -1e9f, mxy = -1e9f;
  for (int k = 0; k < 4; k++) {
    float px_ = corners[k][0] * sc, py_ = corners[k][1] * sc;
    float rx = px_ * cs - py_ * sn + cx, ry = px_ * sn + py_ * cs + cy;
    if (rx < mnx) mnx = rx; if (rx > mxx) mxx = rx; if (ry < mny) mny = ry; if (ry > mxy) mxy = ry;
  }
  g->sc = sc; g->cs = cs; g->sn = sn; g->cx = cx; g->cy = cy;
  g->bx = (int)floorf(mnx) - 1; g->by = (int)floorf(mny) - 1;
  g->bw = (int)ceilf(mxx) - g->bx + 2; g->bh = (int)ceilf(mxy) - g->by + 2;
}
void gfx_sprite_ex_rows(int spr, int x16, int y16, int ang16, int scale256, int *y0, int *y1) {
  ExGeom g;
  ex_geom(sprite_def(spr), x16, y16, ang16, scale256, &g);
  *y0 = g.by;
  *y1 = g.by + g.bh;
}

void gfx_sprite_ex(int spr, int x16, int y16, int ang16, int scale256, int flips, color_t tint, unsigned alpha, int mode) {
  if (spr < 0 || !alpha || scale256 <= 0) return;
  const Sprite *s = sprite_def(spr);
  ExGeom g;
  ex_geom(s, x16, y16, ang16, scale256, &g);
  const float sc = g.sc, cs = g.cs, sn = g.sn, cx = g.cx, cy = g.cy;
  const float w = s->w, h = s->h, ax = s->ax, ay = s->ay;
  int bx = g.bx, by = g.by, bw = g.bw, bh = g.bh;
  if (!clip_rect(&bx, &by, &bw, &bh)) return;
  Sampler sm;
  sampler_init(&sm, spr, tint, 256);
  float inv = 1.f / sc;
  /* u = (dx*cs + dy*sn)*inv + ax - .5, v = (-dx*sn + dy*cs)*inv + ay - .5 */
  float dudx = cs * inv, dvdx = -sn * inv, dudy = sn * inv, dvdy = cs * inv;
  float u0 = ax - .5f, v0 = ay - .5f;
  if (flips & XF_FLIPX) { dudx = -dudx; dudy = -dudy; if (!(flips & XF_ANCHOR)) u0 = w - 1 - u0; }
  if (flips & XF_FLIPY) { dvdx = -dvdx; dvdy = -dvdy; if (!(flips & XF_ANCHOR)) v0 = h - 1 - v0; }
  const int W = s->w, H = s->h;
  const int32_t sdu = (int32_t)(dudx * 65536), sdv = (int32_t)(dvdx * 65536);
  const unsigned al = alpha > 256 ? 256 : alpha;
  for (int j = by; j < by + bh; j++) {
    float dx0 = bx + .5f - cx, dy = j + .5f - cy;
    float uf = u0 + dx0 * dudx + dy * dudy, vf = v0 + dx0 * dvdx + dy * dvdy;
    /* span where -1 < u < W and -1 < v < H */
    float lo = 0, hi = (float)bw;
    if (dudx > 1e-6f) { float a = (-1 - uf) / dudx, b = (W - uf) / dudx; if (a > lo) lo = a; if (b < hi) hi = b; }
    else if (dudx < -1e-6f) { float a = (W - uf) / dudx, b = (-1 - uf) / dudx; if (a > lo) lo = a; if (b < hi) hi = b; }
    else if (uf <= -1 || uf >= W) continue;
    if (dvdx > 1e-6f) { float a = (-1 - vf) / dvdx, b = (H - vf) / dvdx; if (a > lo) lo = a; if (b < hi) hi = b; }
    else if (dvdx < -1e-6f) { float a = (H - vf) / dvdx, b = (-1 - vf) / dvdx; if (a > lo) lo = a; if (b < hi) hi = b; }
    else if (vf <= -1 || vf >= H) continue;
    int i0 = (int)ceilf(lo), i1 = (int)ceilf(hi);
    if (i0 < 0) i0 = 0;
    if (i1 > bw) i1 = bw;
    if (i0 >= i1) continue;
    int32_t u = (int32_t)((uf + i0 * dudx) * 65536), v = (int32_t)((vf + i0 * dvdx) * 65536);
    uint16_t *p = pix(bx + i0, j);
    int n = i1 - i0;
    bool add = mode == BLEND_ADD;
    if (s->fmt == 0) {
      if (add) ex_span(p, n, u, v, sdu, sdv, &sm, W, H, al, 0, BLEND_ADD);
      else ex_span(p, n, u, v, sdu, sdv, &sm, W, H, al, 0, BLEND_NORMAL);
    } else if (s->fmt == 1) {
      if (add) ex_span(p, n, u, v, sdu, sdv, &sm, W, H, al, 1, BLEND_ADD);
      else ex_span(p, n, u, v, sdu, sdv, &sm, W, H, al, 1, BLEND_NORMAL);
    } else {
      if (add) ex_span(p, n, u, v, sdu, sdv, &sm, W, H, al, 2, BLEND_ADD);
      else ex_span(p, n, u, v, sdu, sdv, &sm, W, H, al, 2, BLEND_NORMAL);
    }
  }
}

/* ---------------------------------------------------------------- text */

static int glyph_of(int font, int ch) {
  (void)font;
  if (ch < 32 || ch > 127) return -1;
  unsigned g = font_charmap[ch - 32];
  return g == 255 ? -1 : (int)g;
}
#if NP_TEXT_EXTRA
/* Language builds (np_text.h): letters the font lacks are glyphs made here, a bit a pixel in
 * x_rows, then outlined, shadowed and filled as the font's own: an accent over the plain capital
 * (a cedilla under it), 5 x 2 pixels each k x k, and the letters of the 12-pixel font (Chinese),
 * each pixel s x s, centred on the capitals. */
static Sprite x_spr;
static uint64_t x_rows[56];
static void x_plot(int x, int y, void *ctx) {
  (void)ctx;
  if (x >= 0 && x < 48 && y >= 0 && y < 56) x_rows[y] |= (uint64_t)1 << x;
}
static int x_scale(const Font *f) { return (f->cap + 6) / 12; } /* 1, 1, 2 */
static int x_k(const Font *f) { return (f->cap + 3) / 7; }      /* 1, 2, 3 */
/* The next letter of a line: the font's glyph *g (-1: none) with the accent *acc, or else *cp,
   a letter of the 12-pixel font (0: none). A ligature's second letter waits in second. */
typedef struct {
  const char *s;
  char second;
} x_iter;
static bool x_next(x_iter *it, int font, int *g, int *acc, uint32_t *cp) {
  uint32_t c;
  *acc = 0, *cp = 0;
  if (it->second) {
    c = (uint8_t)it->second, it->second = 0;
  } else {
    if (!*it->s || *it->s == '\n') return false;
    c = np_utf8(&it->s);
  }
  if (c >= 0x80) {
    char b, b2;
    *acc = np_latin(c, &b, &b2);
    if (!b) {
      *g = -1, *cp = np_xglyph(c) ? c : 0;
      return true;
    }
    it->second = b2, c = (uint8_t)b;
  }
  *g = glyph_of(font, (int)c);
  return true;
}
static int x_adv(const Font *f, int g, uint32_t cp) {
  return g >= 0 ? f->advance[g] : cp ? np_xadvance(cp, x_scale(f)) + f->outline : f->cap / 2;
}
/* x_spr: the accent acc of the glyph b, or the letter cp */
static const Sprite *x_glyph(const Font *f, int acc, const Sprite *b, uint32_t cp) {
  memset(x_rows, 0, sizeof x_rows);
  if (cp) {
    int s = x_scale(f);
    np_xdraw(cp, 0, 0, s, x_plot, 0);
    x_spr.w = x_spr.h = (uint16_t)(12 * s);
    x_spr.ax = 0, x_spr.ay = (int16_t)((f->cap + 12 * s + 1) / 2);
  } else {
    int k = x_k(f);
    np_accent(acc, 0, 0, k, x_plot, 0);
    x_spr.w = (uint16_t)(5 * k), x_spr.h = (uint16_t)(2 * k);
    x_spr.ax = (int16_t)(b->ax - (b->w - 5 * k) / 2);
    /* k pixels over the capital; a cedilla under its outline */
    x_spr.ay = (int16_t)(acc == NP_ACC_CEDIL ? -f->outline : f->cap + 3 * k);
  }
  return &x_spr;
}
#endif

int gfx_text_width(int font, const char *s) {
  const Font *f = &fonts[font];
  int w = 0;
#if NP_TEXT_EXTRA
  x_iter it = {s, 0};
  int g, acc;
  uint32_t cp;
  while (x_next(&it, font, &g, &acc, &cp)) w += x_adv(f, g, cp);
#else
  for (; *s && *s != '\n'; s++) {
    int g = glyph_of(font, (unsigned char)*s);
    w += g < 0 ? f->cap / 2 : f->advance[g];
  }
#endif
  return w + f->outline * 2;
}

/* Glyph coverage and its dilation (the black outline). Outline radii are 1
 * (3x3 square) or 2 (5x5 disc without corners), computed separably. */
enum { GB_W = 48, GB_H = 56 };
/* They live in the level decoder's scratch memory: text is never drawn
   while a chunk is being decoded. */
extern uint8_t level_scratch[];
#define g_cov ((uint8_t (*)[GB_W])(level_scratch))
#define g_h1 ((uint8_t (*)[GB_W])(level_scratch + GB_W * GB_H))
#define g_dil ((uint8_t (*)[GB_W])(level_scratch + 2 * GB_W * GB_H))
_Static_assert(3 * GB_W * GB_H <= 8064, "glyph buffers exceed the shared scratch");

/* Fills rows [k0, k1) of the extended glyph box (w x h plus a margin of r). */
static void glyph_rows(const Sprite *s, int r, int ew, int eh, int k0, int k1) {
  if (k0 < 0) k0 = 0;
  if (k1 > eh) k1 = eh;
  const uint8_t *data = sprite_blob + s->off;
  int stride = (s->w + 1) / 2;
  int c0 = k0 - r < 0 ? 0 : k0 - r, c1 = k1 + r > eh ? eh : k1 + r;
  for (int k = c0; k < c1; k++) {
    uint8_t *row = g_cov[k];
    memset(row, 0, (size_t)ew);
    int v = k - r;
    if (v >= 0 && v < s->h) {
#if NP_TEXT_EXTRA
      if (s == &x_spr) {
        for (int u = 0; u < s->w; u++) row[u + r] = x_rows[v] >> u & 1 ? 15 : 0;
      } else
#endif
      {
        const uint8_t *src = data + v * stride;
        for (int u = 0; u < s->w; u++) row[u + r] = (u & 1) ? src[u >> 1] >> 4 : src[u >> 1] & 15;
      }
    }
    uint8_t *h1 = g_h1[k];
    for (int i = 0; i < ew; i++) {
      uint8_t m = row[i];
      if (i > 0 && row[i - 1] > m) m = row[i - 1];
      if (i + 1 < ew && row[i + 1] > m) m = row[i + 1];
      h1[i] = m;
    }
  }
  /* Row k's own h1 stands in for rows outside the box: it is part of the
     maximum anyway. */
  for (int k = k0; k < k1; k++) {
    uint8_t *d = g_dil[k];
    const uint8_t *hm = g_h1[k], *hu = k >= 1 ? g_h1[k - 1] : hm, *hd = k + 1 < eh ? g_h1[k + 1] : hm;
    if (r == 1) {
      for (int i = 0; i < ew; i++) {
        uint8_t m = hm[i];
        if (hu[i] > m) m = hu[i];
        if (hd[i] > m) m = hd[i];
        d[i] = m;
      }
      continue;
    }
    /* 5x5 without corners: |dy| <= 1 with |dx| <= 2, or |dy| = 2 with |dx| <= 1 */
    const uint8_t *hu2 = k >= 2 ? g_h1[k - 2] : hm, *hd2 = k + 2 < eh ? g_h1[k + 2] : hm;
    uint8_t vb[GB_W + 2], *v = vb + 1;   /* h1 over rows k-1..k+1, with zeros around */
    vb[0] = 0;
    for (int i = 0; i < ew; i++) {
      uint8_t m = hm[i];
      if (hu[i] > m) m = hu[i];
      if (hd[i] > m) m = hd[i];
      v[i] = m;
    }
    v[ew] = 0;
    for (int i = 0; i < ew; i++) {
      uint8_t m = v[i];
      if (v[i - 1] > m) m = v[i - 1];
      if (v[i + 1] > m) m = v[i + 1];
      if (hu2[i] > m) m = hu2[i];
      if (hd2[i] > m) m = hd2[i];
      d[i] = m;
    }
  }
}

/* Draw one glyph: shadow, outline then gradient fill. Only rows inside the
 * current strip are computed. */
static void draw_glyph(const Font *f, const Sprite *s, int x, int baseline, color_t top, color_t bottom, unsigned alpha) {
  int r = f->outline, gx = x - s->ax, gy = baseline - s->ay;
  int sdx = f->sdx > 0 ? f->sdx : 0, sdy = f->sdy > 0 ? f->sdy : 0;
  int ex0 = gx - r, ey0 = gy - r, ew = s->w + 2 * r + sdx, eh = s->h + 2 * r + sdy;
  if (ew > GB_W || eh > GB_H) return;
  int cx = ex0, cy = ey0, cw = ew, ch = eh;
  if (!clip_rect(&cx, &cy, &cw, &ch)) return;
  int bw = s->w + 2 * r, bh = s->h + 2 * r;   /* dilated box without shadow */
  int k0 = cy - ey0 - sdy, k1 = cy + ch - ey0;
  glyph_rows(s, r, bw, bh, k0, k1);
  unsigned al = alpha > 256 ? 256 : alpha;
  /* blend amounts of each coverage level: shadow, and outline or fill */
  uint8_t ash[16], acv[16];
  for (unsigned v = 0; v < 16; v++) {
    ash[v] = (uint8_t)a32_of(v * f->shadow / 15 * al / 15);
    acv[v] = (uint8_t)a32_of(v * al / 15);
  }
  int cap_top = baseline - f->cap;
  for (int j = cy; j < cy + ch; j++) {
    uint16_t *p = pix(cx, j);
    int t = (j - cap_top) * 256 / (f->cap > 1 ? f->cap : 1);
    color_t fill = c_mix(top, bottom, (unsigned)(t < 0 ? 0 : t > 256 ? 256 : t));
    int lj = j - ey0, sj = lj - sdy;
    for (int i = 0; i < cw; i++) {
      int li = cx + i - ex0, si = li - sdx;
      unsigned sh = (si >= 0 && sj >= 0 && si < bw && sj < bh) ? g_dil[sj][si] : 0;
      unsigned o = 0, fv = 0;
      if (li < bw && lj < bh) { o = g_dil[lj][li]; fv = g_cov[lj][li]; }
      if (o < 15) put(p + i, 0, ash[sh], BLEND_NORMAL);
      put(p + i, 0, acv[o], BLEND_NORMAL);
      put(p + i, fill, acv[fv], BLEND_NORMAL);
    }
  }
}

void gfx_text(int font, int x, int baseline, const char *s, color_t top, color_t bottom, unsigned alpha) {
  const Font *f = &fonts[font];
  if (baseline + f->cap < gfx_y0 - 8 || baseline - 2 * f->cap > gfx_y1 + 8 || !alpha) return;
  x += f->outline;
#if NP_TEXT_EXTRA
  x_iter it = {s, 0};
  int g, acc;
  uint32_t cp;
  while (x_next(&it, font, &g, &acc, &cp)) {
    if (g > 0) { /* (0 is the space) an accent over the letter first: the letter's outline goes over its shadow */
      const Sprite *b = &sprites[f->base + g];
      if (acc && acc != NP_ACC_CEDIL) draw_glyph(f, x_glyph(f, acc, b, 0), x, baseline, top, bottom, alpha);
      draw_glyph(f, b, x, baseline, top, bottom, alpha);
      if (acc == NP_ACC_CEDIL) draw_glyph(f, x_glyph(f, acc, b, 0), x, baseline, top, bottom, alpha);
    } else if (cp) {
      draw_glyph(f, x_glyph(f, 0, 0, cp), x, baseline, top, bottom, alpha);
    }
    x += x_adv(f, g, cp);
  }
#else
  for (; *s && *s != '\n'; s++) {
    int g = glyph_of(font, (unsigned char)*s);
    if (g < 0) { x += f->cap / 2; continue; }
    if (*s != ' ') draw_glyph(f, &sprites[f->base + g], x, baseline, top, bottom, alpha);
    x += f->advance[g];
  }
#endif
}
void gfx_text_center(int font, int cx, int baseline, const char *s, color_t top, color_t bottom, unsigned alpha) {
  gfx_text(font, cx - gfx_text_width(font, s) / 2, baseline, s, top, bottom, alpha);
}
void gfx_text_right(int font, int rx, int baseline, const char *s, color_t top, color_t bottom, unsigned alpha) {
  gfx_text(font, rx - gfx_text_width(font, s), baseline, s, top, bottom, alpha);
}
int gfx_format_uint(char *buf, unsigned long v) {
  char tmp[12];
  int n = 0, len = 0;
  do { tmp[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 11);
  while (n) buf[len++] = tmp[--n];
  buf[len] = 0;
  return len;
}
void gfx_number(int font, int x, int baseline, long v, color_t c) {
  char buf[16];
  if (v < 0) { buf[0] = '-'; gfx_format_uint(buf + 1, (unsigned long)(-v)); }
  else gfx_format_uint(buf, (unsigned long)v);
  gfx_text(font, x, baseline, buf, c, c, 256);
}

/* ------------------------------------------------------------ text layer */

/* Text rendered once into coverage nibbles (fill high, outline low) so it
 * can be drawn scaled each frame: used by the pop-in titles. */
enum { TL_W = 320, TL_H = 64 };
static uint8_t tl_buf[TL_W * TL_H];
static int tl_w, tl_h, tl_font, tl_ox, tl_oy;   /* origin: layer pixel of the anchor */
static int tl_line_top[4], tl_line_cap[4], tl_lines;
static color_t tl_top[4], tl_bot[4];
static bool tl_custom[4];
void gfx_layer_line_color(int line, color_t top, color_t bottom) {
  if (line < 0 || line >= 4) return;
  tl_top[line] = top;
  tl_bot[line] = bottom;
  tl_custom[line] = true;
}

/* A glyph into the layer, its pen at (pen, base) */
static void layer_glyph(const Sprite *sp, int r, int pen, int base) {
  int bw = sp->w + 2 * r, bh = sp->h + 2 * r;
  if (bw > GB_W || bh > GB_H) return;
  glyph_rows(sp, r, bw, bh, 0, bh);
  int ox = pen - sp->ax - r, oy = base - sp->ay - r;
  for (int k = 0; k < bh; k++) {
    int y = oy + k;
    if (y < 0 || y >= tl_h) continue;
    for (int i = 0; i < bw; i++) {
      int x = ox + i;
      if (x < 0 || x >= tl_w) continue;
      uint8_t *d = &tl_buf[y * TL_W + x];
      unsigned fo = *d >> 4, oo = *d & 15, fn = g_cov[k][i], on = g_dil[k][i];
      if (fn > fo) fo = fn;
      if (on > oo) oo = on;
      *d = (uint8_t)(fo << 4 | oo);
    }
  }
}

void gfx_layer_build(int font, const char *s) {
  const Font *f = &fonts[font];
  int r = f->outline, line_h = f->cap + f->cap / 2 + 2 * r + 2;
  int lines = 1, maxw = 0;
  for (const char *q = s; *q; q++) if (*q == '\n') lines++;
  if (lines > 4) lines = 4;
  const char *q = s;
  for (int l = 0; l < lines; l++) {
    int w = gfx_text_width(font, q);
    if (w > maxw) maxw = w;
    while (*q && *q != '\n') q++;
    if (*q) q++;
  }
  int xm = 0; /* room over the first line for its accents and taller letters */
#if NP_TEXT_EXTRA
  {
    x_iter it = {s, 0};
    int g, acc;
    uint32_t cp;
    while (x_next(&it, font, &g, &acc, &cp)) {
      int up = acc && acc != NP_ACC_CEDIL ? 3 * x_k(f) - 1 : cp ? (f->cap + 12 * x_scale(f) + 1) / 2 - f->cap - 1 : 0;
      if (up > xm) xm = up;
    }
  }
#endif
  tl_w = maxw + f->sdx + 2 < TL_W ? maxw + f->sdx + 2 : TL_W;
  tl_h = lines * line_h + f->sdy + xm < TL_H ? lines * line_h + f->sdy + xm : TL_H;
  tl_font = font;
  tl_lines = lines;
  memset(tl_custom, 0, sizeof(tl_custom));
  memset(tl_buf, 0, sizeof(tl_buf));
  q = s;
  for (int l = 0; l < lines; l++) {
    int w = gfx_text_width(font, q), pen = (tl_w - w) / 2 + r, base = l * line_h + r + f->cap + 1 + xm;
    tl_line_top[l] = base - f->cap;
    tl_line_cap[l] = f->cap;
#if NP_TEXT_EXTRA
    x_iter it = {q, 0};
    int g, acc;
    uint32_t cp;
    while (x_next(&it, font, &g, &acc, &cp)) {
      if (g > 0) {
        const Sprite *b = &sprites[f->base + g];
        layer_glyph(b, r, pen, base);
        if (acc) layer_glyph(x_glyph(f, acc, b, 0), r, pen, base);
      } else if (cp) {
        layer_glyph(x_glyph(f, 0, 0, cp), r, pen, base);
      }
      pen += x_adv(f, g, cp);
    }
    q = it.s;
#else
    for (; *q && *q != '\n'; q++) {
      int g = glyph_of(font, (unsigned char)*q);
      if (g < 0) { pen += f->cap / 2; continue; }
      if (*q != ' ') layer_glyph(&sprites[f->base + g], r, pen, base);
      pen += f->advance[g];
    }
#endif
    if (*q) q++;
  }
  tl_ox = tl_w / 2;
  tl_oy = tl_h / 2;
}

/* Draws the layer centred on (cx, cy) with scale in 1/256. Gradient colours
 * run over each line's cap height. Nearest sampling below 1.5x is enough at
 * this size; the scale animations are short. */
void gfx_layer_draw(int cx, int cy, int scale256, color_t top, color_t bottom, unsigned alpha) {
  if (scale256 < 8 || !alpha) return;
  const Font *f = &fonts[tl_font];
  int dw = tl_w * scale256 / 256 + 2, dh = tl_h * scale256 / 256 + 2;
  int x = cx - dw / 2, y = cy - dh / 2, w = dw, h = dh;
  if (!clip_rect(&x, &y, &w, &h)) return;
  int inv = 65536 / scale256;   /* 1/256 layer px per screen px, *256 */
  unsigned al = alpha > 256 ? 256 : alpha;
  int sdx = f->sdx * 256, sdy = f->sdy * 256;
  for (int j = y; j < y + h; j++) {
    int v = ((j - cy) * 2 + 1) * inv / 2 + tl_oy * 256;   /* 1/256 px */
    int vi = v >> 8;
    color_t fill = top;
    for (int l = 0; l < tl_lines; l++) {
      if (vi >= tl_line_top[l] - 4 && vi <= tl_line_top[l] + tl_line_cap[l] + 6) {
        int t = (vi - tl_line_top[l]) * 256 / tl_line_cap[l];
        fill = c_mix(tl_custom[l] ? tl_top[l] : top, tl_custom[l] ? tl_bot[l] : bottom, (unsigned)(t < 0 ? 0 : t > 256 ? 256 : t));
        break;
      }
    }
    int vs = (v - sdy) >> 8;
    bool row_ok = vi >= 0 && vi < tl_h, srow_ok = vs >= 0 && vs < tl_h;
    if (!row_ok && !srow_ok) continue;
    uint16_t *p = pix(x, j);
    for (int i = 0; i < w; i++) {
      int u = ((x + i - cx) * 2 + 1) * inv / 2 + tl_ox * 256;
      int ui = u >> 8, us = (u - sdx) >> 8;
      unsigned sh = 0, o = 0, fv = 0;
      if (srow_ok && us >= 0 && us < tl_w) sh = (tl_buf[vs * TL_W + us] & 15) * f->shadow / 15;
      if (row_ok && ui >= 0 && ui < tl_w) { uint8_t b = tl_buf[vi * TL_W + ui]; o = b & 15; fv = b >> 4; }
      if (sh && o < 15) put(p + i, 0, a32_of(sh * al / 15), BLEND_NORMAL);
      if (o) put(p + i, 0, a32_of(o * al / 15), BLEND_NORMAL);
      if (fv) put(p + i, fill, a32_of(fv * al / 15), BLEND_NORMAL);
    }
  }
}

/* ------------------------------------------------------------ polygons */

/* Anti-aliased convex polygon (vertices in pixels, either winding). */
void gfx_poly(const float *xy, int n, color_t c, unsigned alpha, int mode) {
  if (n < 3 || n > 8 || !alpha) return;
  {
    /* Quick reject: the rows below cover every vertex, so they hold the
       ones drawn (floor(min) - 1 to ceil(max) + 1). */
    float lo = 1e9f, hi = -1e9f;
    for (int k = 0; k < n; k++) {
      if (xy[2 * k + 1] < lo) lo = xy[2 * k + 1];
      if (xy[2 * k + 1] > hi) hi = xy[2 * k + 1];
    }
    int cy0 = clip_y0 > gfx_y0 ? clip_y0 : gfx_y0, cy1 = clip_y1 < gfx_y1 ? clip_y1 : gfx_y1;
    if (hi <= (float)(cy0 - 2) || lo >= (float)(cy1 + 1)) return;
  }
  float area = 0, nx[8], ny[8], nc[8];
  for (int k = 0; k < n; k++) {
    int b = (k + 1) % n;
    area += xy[2 * k] * xy[2 * b + 1] - xy[2 * b] * xy[2 * k + 1];
  }
  if (area == 0) return;
  float sgn = area > 0 ? 1.f : -1.f, mnx = 1e9f, mny = 1e9f, mxx = -1e9f, mxy = -1e9f;
  for (int k = 0; k < n; k++) {
    int b = (k + 1) % n;
    float ex = xy[2 * b] - xy[2 * k], ey = xy[2 * b + 1] - xy[2 * k + 1], L = sqrtf(ex * ex + ey * ey);
    if (L < 1e-6f) { nx[k] = ny[k] = 0; nc[k] = 1e9f; continue; }
    nx[k] = -ey / L * sgn; ny[k] = ex / L * sgn; nc[k] = -(nx[k] * xy[2 * k] + ny[k] * xy[2 * k + 1]);
    if (xy[2 * k] < mnx) mnx = xy[2 * k];
    if (xy[2 * k] > mxx) mxx = xy[2 * k];
    if (xy[2 * k + 1] < mny) mny = xy[2 * k + 1];
    if (xy[2 * k + 1] > mxy) mxy = xy[2 * k + 1];
  }
  int x = (int)floorf(mnx) - 1, y = (int)floorf(mny) - 1, w = (int)ceilf(mxx) - x + 2, h = (int)ceilf(mxy) - y + 2;
  if (!clip_rect(&x, &y, &w, &h)) return;
  const unsigned full = a32_of(alpha);
  for (int j = y; j < y + h; j++) {
    uint16_t *p = pix(x, j);
    float py_ = j + .5f;
    /* [lo, hi): every edge distance > -0.5 (touched); [flo, fhi): > 0.5 (fully covered) */
    float lo = (float)x, hi = (float)(x + w), flo = lo, fhi = hi;
    bool empty = false;
    for (int k = 0; k < n && !empty; k++) {
      float a = nx[k], d = ny[k] * py_ + nc[k];   /* distance at px is a*px + d */
      if (a > 1e-6f) {
        float t = (-.5f - d) / a, u = (.5f - d) / a;
        if (t > lo) lo = t;
        if (u > flo) flo = u;
      } else if (a < -1e-6f) {
        float t = (-.5f - d) / a, u = (.5f - d) / a;
        if (t < hi) hi = t;
        if (u < fhi) fhi = u;
      } else {
        if (d <= -.5f) empty = true;
        if (d < .5f) fhi = flo - 1;
      }
    }
    if (empty || hi <= lo) continue;
    int i0 = (int)floorf(lo - .5f) - x, i1 = (int)ceilf(hi + .5f) - x;
    int f0 = (int)ceilf(flo - .5f) - x, f1 = (int)floorf(fhi - .5f) - x + 1;
    if (i0 < 0) i0 = 0;
    if (i1 > w) i1 = w;
    if (f0 < i0) f0 = i0;
    if (f1 > i1) f1 = i1;
    if (f1 < f0) f1 = f0;
    for (int i = i0; i < i1; i++) {
      if (i == f0 && f1 > f0) {
        for (; i < f1; i++) put(p + i, c, full, mode);
        if (i >= i1) break;
      }
      float px_ = x + i + .5f, m = 1e9f;
      for (int k = 0; k < n; k++) { float d = nx[k] * px_ + ny[k] * py_ + nc[k]; if (d < m) m = d; }
      float cov = m + .5f;
      if (cov <= 0) continue;
      if (cov > 1) cov = 1;
      put(p + i, c, a32_of((unsigned)(cov * alpha)), mode);
    }
  }
}
