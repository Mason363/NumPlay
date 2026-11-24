/* NumVisuals' drawing kit: the tables, noise, colour out, light added, and text drawn from a distance field. */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("O2") /* (drawing: speed over size) */
#endif
#include "vis.h"
#include "../../common/np_text.h"
#include "font_sdf.h"

color buf[W * SH];
int top;
uint32_t now;
int16_t sin_t[1024];
uint8_t exp_t[256];
uint16_t R5[1024], G6[1024], B5[1024];
uint8_t sqrt_t[4096];
const uint8_t bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
static uint8_t perm[256];
static uint16_t smooth_t[256]; /* smoothstep, 0 to 256 */
static uint32_t seed = 0x2545F491;

uint32_t rnd(void) {
  seed ^= seed << 13;
  seed ^= seed >> 17;
  seed ^= seed << 5;
  return seed;
}

void core_init(uint32_t salt) {
  seed ^= salt;
  /* (once, in floats: the calculators without an FPU only pay for it here) */
  for (int i = 0; i < 256; i++) {
    float x = i * (3.14159265f / 512), x2 = x * x;
    /* sin on a quarter turn, from its series (exact to a few parts in 10^5) */
    float s = x * (1 - x2 / 6 * (1 - x2 / 20 * (1 - x2 / 42 * (1 - x2 / 72))));
    int v = (int)(s * 16384 + 0.5f);
    sin_t[i] = (int16_t)v, sin_t[512 - i] = (int16_t)v;
    sin_t[512 + i] = (int16_t)-v, sin_t[(1024 - i) & 1023] = (int16_t)-v;
  }
  sin_t[256] = 16384, sin_t[768] = -16384;
  float e = 256;
  for (int i = 0; i < 256; i++, e *= 0.96923323f) exp_t[i] = (uint8_t)(e > 255 ? 255 : e); /* e^(-1/32) */
  for (int i = 0; i < 1024; i++) {
    R5[i] = (uint16_t)((i >> 3 > 31 ? 31 : i >> 3) << 11);
    G6[i] = (uint16_t)((i >> 2 > 63 ? 63 : i >> 2) << 5);
    B5[i] = (uint16_t)(i >> 3 > 31 ? 31 : i >> 3);
  }
  for (int i = 0, r = 0; i < 4096; i++) { /* sqrt(16 i) */
    while ((r + 1) * (r + 1) <= i * 16) r++;
    sqrt_t[i] = (uint8_t)(r > 255 ? 255 : r);
  }
  for (int i = 0; i < 256; i++) {
    int t = i; /* 3t^2 - 2t^3, t in 1/256 */
    smooth_t[i] = (uint16_t)((3 * t * t * 256 - 2 * t * t * t) >> 16);
  }
  /* the noise's lattice: a fixed shuffle, so every background looks the same each time */
  uint32_t s0 = seed, k = 0x9E3779B9;
  for (int i = 0; i < 256; i++) perm[i] = (uint8_t)i;
  for (int i = 255; i > 0; i--) {
    k ^= k << 13, k ^= k >> 17, k ^= k << 5;
    int j = (int)(k % (uint32_t)(i + 1));
    uint8_t t = perm[i];
    perm[i] = perm[j], perm[j] = t;
  }
  seed = s0;
}

/* ------------------------------------------------------------------ noise */
static inline int lat(int x, int y) { return perm[(perm[x & 255] + y) & 255]; }
int noise2(int x, int y) {
  int xi = x >> 8, yi = y >> 8, sx = smooth_t[x & 255], sy = smooth_t[y & 255];
  int a = lat(xi, yi), b = lat(xi + 1, yi), c = lat(xi, yi + 1), d = lat(xi + 1, yi + 1);
  int ab = a + ((b - a) * sx >> 8), cd = c + ((d - c) * sx >> 8);
  return ab + ((cd - ab) * sy >> 8);
}
int noise1(int x) {
  int xi = x >> 8, a = perm[xi & 255], b = perm[(xi + 1) & 255];
  return a + ((b - a) * smooth_t[x & 255] >> 8);
}
int fbm2(int x, int y, int n) {
  int s = 0, w = 0;
  for (int i = 0, k = 256; i < n; i++, k >>= 1, x = x * 2 + 9301, y = y * 2 + 4967) s += noise2(x, y) * k, w += k;
  return s / w;
}
int fbm1(int x, int n) {
  int s = 0, w = 0;
  for (int i = 0, k = 256; i < n; i++, k >>= 1, x = x * 2 + 7703) s += noise1(x) * k, w += k;
  return s / w;
}

void ramp(ramp_t *p, const uint32_t *c, int n) {
  for (int i = 0; i < 256; i++) {
    int q = i * (n - 1), k = q >> 8, f = q & 255;
    uint32_t a = c[k], b = c[k + 1 < n ? k + 1 : k];
    p->r[i] = (uint8_t)((((a >> 16) & 255) * (256 - f) + ((b >> 16) & 255) * f) >> 8);
    p->g[i] = (uint8_t)((((a >> 8) & 255) * (256 - f) + ((b >> 8) & 255) * f) >> 8);
    p->b[i] = (uint8_t)(((a & 255) * (256 - f) + (b & 255) * f) >> 8);
  }
}

/* ------------------------------------------------------------------ light */
void glow_dot(int x, int y, int r, uint32_t rgb, int k) {
  int y0 = clampi((y - r) >> 4, top, top + SH), y1 = clampi(((y + r) >> 4) + 1, top, top + SH);
  int x0 = clampi((x - r) >> 4, 0, W), x1 = clampi(((x + r) >> 4) + 1, 0, W);
  if (y0 >= y1 || x0 >= x1 || r <= 0) return;
  int R = (int)(rgb >> 16 & 255) * k >> 8, G = (int)(rgb >> 8 & 255) * k >> 8, B = (int)(rgb & 255) * k >> 8;
  int32_t r2 = r * r, inv = (int32_t)((1 << 30) / r2); /* (1/r^2 in 2^-30) */
  for (int py = y0; py < y1; py++) {
    int dy = py * 16 + 8 - y;
    color *p = buf + (py - top) * W;
    for (int px = x0; px < x1; px++) {
      int dx = px * 16 + 8 - x;
      int32_t e = r2 - dx * dx - dy * dy;
      if (e <= 0) continue;
      int w = (int)(((int64_t)e * inv) >> 22); /* 0 to 256 */
      w = w * w >> 8;
      add(p + px, R * w >> 8, G * w >> 8, B * w >> 8);
    }
  }
}

void glow_line(int x0, int y0, int x1, int y1, uint32_t rgb, int k0, int k1) {
  int dx = x1 - x0, dy = y1 - y0, R = rgb >> 16 & 255, G = rgb >> 8 & 255, B = rgb & 255;
  if (iabs(dx) >= iabs(dy)) { /* along x: two pixels a column, as Wu's lines */
    if (dx < 0) {
      int t;
      t = x0, x0 = x1, x1 = t, t = y0, y0 = y1, y1 = t, t = k0, k0 = k1, k1 = t;
      dx = -dx, dy = -dy;
    }
    int a = clampi(x0 >> 4, 0, W - 1), b = clampi(x1 >> 4, 0, W - 1);
    if (!dx) dx = 1;
    int32_t slope = dy * 65536 / dx, kslope = (k1 - k0) * 65536 / dx;
    for (int px = a; px <= b; px++) {
      int s = px * 16 + 8 - x0, y = y0 + (int)((int64_t)s * slope >> 16), k = k0 + (int)((int64_t)s * kslope >> 16);
      int yi = y >> 4, f = y & 15;
      if (k <= 0) continue;
      for (int h = 0; h < 2; h++) {
        int py = yi + h, w = (h ? f : 16 - f) * k >> 4;
        if (py >= top && py < top + SH && w > 0) add(buf + (py - top) * W + px, R * w >> 8, G * w >> 8, B * w >> 8);
      }
    }
  } else { /* along y */
    if (dy < 0) {
      int t;
      t = x0, x0 = x1, x1 = t, t = y0, y0 = y1, y1 = t, t = k0, k0 = k1, k1 = t;
      dx = -dx, dy = -dy;
    }
    int a = clampi(y0 >> 4, top, top + SH - 1), b = clampi(y1 >> 4, top, top + SH - 1);
    if ((y1 >> 4) < top || (y0 >> 4) >= top + SH) return;
    int32_t slope = dx * 65536 / dy, kslope = (k1 - k0) * 65536 / dy;
    for (int py = a; py <= b; py++) {
      int s = py * 16 + 8 - y0, x = x0 + (int)((int64_t)s * slope >> 16), k = k0 + (int)((int64_t)s * kslope >> 16);
      int xi = x >> 4, f = x & 15;
      if (k <= 0) continue;
      for (int h = 0; h < 2; h++) {
        int px = xi + h, w = (h ? f : 16 - f) * k >> 4;
        if (px >= 0 && px < W && w > 0) add(buf + (py - top) * W + px, R * w >> 8, G * w >> 8, B * w >> 8);
      }
    }
  }
}

void shade(int x, int y, int w, int h, color c, int a) {
  int y0 = y < top ? top : y, y1 = y + h > top + SH ? top + SH : y + h;
  int x0 = x < 0 ? 0 : x, x1 = x + w > W ? W : x + w;
  for (int j = y0; j < y1; j++)
    for (color *p = buf + (j - top) * W + x0, *e = p + x1 - x0; p < e; p++) *p = a >= 32 ? c : mix(c, *p, a);
}

void veil(int y0, int y1, int a0, int a1) {
  for (int y = y0 > top ? y0 : top; y < y1 && y < top + SH; y++) {
    int k = 32 - ((a0 + (a1 - a0) * (y - y0) / (y1 - y0 > 0 ? y1 - y0 : 1)) >> 3); /* (what's kept, out of 32) */
    color *p = buf + (y - top) * W;
    for (int x = 0; x < W; x++) {
      uint32_t c = (p[x] | (uint32_t)p[x] << 16) & 0x07E0F81F;
      c = (c * (uint32_t)k >> 5) & 0x07E0F81F;
      p[x] = (color)(c | c >> 16);
    }
  }
}

/* ------------------------------------------------------------------ text */
static unsigned next_ch(const char **s) {
#if NP_TEXT_EXTRA
  uint32_t cp = np_utf8(s);
  if (cp < 128) return cp;
  char b = 0, b2 = 0;
  np_latin(cp, &b, &b2); /* (an accented letter as its own letter: NumVisuals' texts are English) */
  return b ? (uint8_t)b : '?';
#else
  return (uint8_t)*(*s)++;
#endif
}
static const sdf_glyph_t *glyph_of(unsigned c) {
  return &sdf_glyphs[(c < SDF_FIRST || c > SDF_LAST ? '?' : c) - SDF_FIRST];
}
/* advance in 1/16 pixels at cap (1/16 pixels) */
static int adv16(const sdf_glyph_t *g, int cap) { return g->adv * cap / (8 * SDF_BASE); }
int text_w(const char *s, int cap) {
  int w = 0;
  while (*s) w += adv16(glyph_of(next_ch(&s)), cap);
  return w;
}

/* one glyph, its pen at x16 (1/16 pixels) on the baseline base; soft: 16 sharp, more for a blur; halo: how dark
   (0 to 32) a soft halo round it is, drawn in the same pass */
static void draw_glyph(const sdf_glyph_t *g, int x16, int base, int cap, int bold, int soft, color c, int a, int halo) {
  if (!g->w) return;
  /* s: screen pixels a font pixel, 16.16; inv: the other way */
  int32_t s = cap * 4096 / SDF_BASE, inv = (int32_t)(((int64_t)16 * SDF_BASE << 16) / cap);
  int32_t left = x16 * 4096 + g->x * s, topy = base * 65536 + g->y * s; /* 16.16 */
  int px0 = clampi(left >> 16, 0, W), px1 = clampi((int)((left + g->w * s) >> 16) + 1, 0, W);
  int py0 = clampi(topy >> 16, top, top + SH), py1 = clampi((int)((topy + g->h * s) >> 16) + 1, top, top + SH);
  if (px0 >= px1 || py0 >= py1) return;
  /* distance (font units, 128 on the outline) to screen 1/256 pixels: x SPREAD/127 x s; the halo's, blurred */
  /* (d times these fits 32 bits: d is under 2^15, these under 2^14 for the sizes drawn) */
  int32_t kk = (int32_t)((int64_t)SDF_SPREAD * s / 127) * 16 / soft, kh = kk * 16 / (16 + cap / 32);
  int grow = (14 + cap / 112) * 16;
  /* a font pixel whose four samples are all at most lo is outside the letter and its halo: skipped */
  int32_t far = halo ? (int32_t)((int64_t)(bold * 16 + grow + 113) * 65536 / kh) : (int32_t)((int64_t)(bold * 16 + 128) * 65536 / kk);
  int lo = (32768 - far) / 256 - 1;
  const uint8_t *px = sdf_px + g->at;
  int gw = g->w, gh = g->h;
  for (int py = py0; py < py1; py++) {
    int32_t v = (int32_t)(((int64_t)(py * 65536 + 32768 - topy) * inv) >> 16) - 32768;
    int j = v >> 16, fy = (v >> 8) & 255;
    if (j < -1 || j >= gh) continue;
    const uint8_t *r0 = j >= 0 ? px + j * gw : 0, *r1 = j + 1 < gh ? px + (j + 1) * gw : 0;
    int32_t u = (int32_t)(((int64_t)(px0 * 65536 + 32768 - left) * inv) >> 16) - 32768;
    color *p = buf + (py - top) * W;
    int li = -1000, a00 = 0, a10 = 0, a01 = 0, a11 = 0, any = 0, full = 0;
    for (int x = px0; x < px1; x++, u += inv) {
      int i = u >> 16, fx = (u >> 8) & 255;
      if (i != li) { /* (a font pixel spans several screen ones: its four samples read once) */
        li = i;
        bool in0 = i >= 0 && i < gw, in1 = i + 1 >= 0 && i + 1 < gw;
        a00 = r0 && in0 ? r0[i] : 0, a10 = r0 && in1 ? r0[i + 1] : 0;
        a01 = r1 && in0 ? r1[i] : 0, a11 = r1 && in1 ? r1[i + 1] : 0;
        int hi = a00 > a10 ? a00 : a10, h2 = a01 > a11 ? a01 : a11;
        any = (hi > h2 ? hi : h2) > lo, full = (a00 & a10 & a01 & a11) == 255 && a >= 32 && bold >= 0;
      }
      if (!any) continue;
      if (full) { /* (deep inside the letter) */
        p[x] = c;
        continue;
      }
      int t0 = a00 * 256 + (a10 - a00) * fx, t1 = a01 * 256 + (a11 - a01) * fx;
      int d = t0 + ((t1 - t0) * fy >> 8) - 32768; /* 8.8, 0 on the outline */
      if (halo) {
        int hc = ((d * kh >> 16) + bold * 16 + grow + 128) >> 3;
        if (hc > 1) p[x] = mix(0, p[x], (hc > 32 ? 32 : hc) * halo >> 5);
      }
      int cov = ((d * kk >> 16) + bold * 16 + 128) >> 3; /* 0 to 32 */
      if (cov <= 0) continue;
      if (cov > 32) cov = 32;
      cov = cov * a >> 5;
      p[x] = cov >= 32 ? c : mix(c, p[x], cov);
    }
  }
}

static void draw_text(const char *s, int x16, int base, int cap, int bold, int soft, color c, int a, int fixed, int halo) {
  if (base + cap / 16 < top - 8 || base - cap / 8 - 8 > top + SH) return; /* (not in this strip) */
  int fw = fixed ? adv16(glyph_of('0'), cap) : 0;
  while (*s) {
    unsigned ch = next_ch(&s);
    const sdf_glyph_t *g = glyph_of(ch);
    if (fixed && ch >= '0' && ch <= '9') {
      draw_glyph(g, x16 + (fw - adv16(g, cap)) / 2, base, cap, bold, soft, c, a, halo);
      x16 += fw;
    } else {
      draw_glyph(g, x16, base, cap, bold, soft, c, a, halo);
      x16 += adv16(g, cap);
    }
  }
}
void text(const char *s, int x16, int base, int cap, int bold, color c, int a) {
  draw_text(s, x16, base, cap, bold, 16, c, a, 0, 0);
}
void text_soft(const char *s, int x16, int base, int cap, int bold, int soft, color c, int a) {
  draw_text(s, x16, base, cap, bold, soft, c, a, 0, 0);
}
/* white letters with a soft dark halo round them, in one pass */
void say(const char *s, int x16, int base, int cap, int bold, int a) { draw_text(s, x16, base, cap, bold, 16, WHITE, a, 0, a * 5 / 8); }
void say_c(const char *s, int cx, int base, int cap, int bold, int a) {
  say(s, cx * 16 - text_w(s, cap) / 2, base, cap, bold, a);
}
int digits_w(const char *s, int cap) {
  int fw = adv16(glyph_of('0'), cap), w = 0;
  while (*s) {
    unsigned ch = next_ch(&s);
    w += ch >= '0' && ch <= '9' ? fw : adv16(glyph_of(ch), cap);
  }
  return w;
}
void digits(const char *s, int x16, int base, int cap, int bold, int a) { draw_text(s, x16, base, cap, bold, 16, WHITE, a, 1, a * 5 / 8); }

void glyph_mask(unsigned ch, int cap, uint8_t *m, int mw, int mh, int x16, int base, bool mirror) {
  const sdf_glyph_t *g = glyph_of(ch);
  if (!g->w) return;
  int32_t s = cap * 4096 / SDF_BASE, inv = (int32_t)(((int64_t)16 * SDF_BASE << 16) / cap);
  int32_t left = x16 * 4096 + g->x * s, topy = base * 65536 + g->y * s;
  int32_t kk = (int32_t)((int64_t)SDF_SPREAD * s / 127);
  const uint8_t *px = sdf_px + g->at;
  for (int y = 0; y < mh; y++)
    for (int x = 0; x < mw; x++) {
      int32_t u = (int32_t)(((int64_t)(x * 65536 + 32768 - left) * inv) >> 16) - 32768;
      int32_t v = (int32_t)(((int64_t)(y * 65536 + 32768 - topy) * inv) >> 16) - 32768;
      int i = u >> 16, j = v >> 16, fx = (u >> 8) & 255, fy = (v >> 8) & 255;
#define AT(a, b) ((a) >= 0 && (a) < g->w && (b) >= 0 && (b) < g->h ? px[(b) * g->w + (a)] : 0)
      int t0 = AT(i, j) * 256 + (AT(i + 1, j) - AT(i, j)) * fx, t1 = AT(i, j + 1) * 256 + (AT(i + 1, j + 1) - AT(i, j + 1)) * fx;
#undef AT
      int d = t0 + (int)(((int64_t)(t1 - t0) * fy) >> 8);
      int cov = clampi(((int)(((int64_t)(d - 32768) * kk) >> 16) + 128), 0, 255);
      m[y * mw + (mirror ? mw - 1 - x : x)] = (uint8_t)cov;
    }
}
