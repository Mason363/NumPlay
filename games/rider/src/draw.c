/* The picture, drawn the way the game looks: dark ground under a dark sky, the track as glowing lines (a light core,
 * its colour, a soft edge), red saws and spikes, white gems, and the bike as a coloured shape with a white edge
 * on two white wheels, a trail behind it. Everything is first written down as a list of shapes in colour numbers,
 * then filled strip by strip into the screen with the texts over it (launcher/src/gfx.c): no picture of the whole
 * screen is kept, and the list itself sits in the physics' memory left free between two steps. */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "gfx.h"
#include "rider.h"
#include "data.h"

uint16_t pal[256];
#define ZOOM 0.5f

/* ---------------------------------------------------------------- the list of shapes */
enum { D_CONVEX, D_POLY, D_DISC, D_LINE, D_RECT };
typedef struct {
  uint8_t type, col;
  uint16_t n, at; /* points (or the line's thickness), where they start in the pool */
  int16_t y0, y1; /* the rows it covers */
} dcmd;
static dcmd *cmds;   /* (in the physics' free memory: a third shapes, the rest points) */
static int16_t *pts; /* eighths of a pixel */
static int ncmd, npts, NCMD, NPTS;
static uint8_t bg_col;

void draw_clear(uint8_t c) {
  size_t n;
  uint8_t *m = ph_scratch(&n);
  NCMD = (int)(n / 3 / sizeof(dcmd)), cmds = (dcmd *)m;
  NPTS = (int)((n - NCMD * sizeof(dcmd)) / 4), pts = (int16_t *)(m + NCMD * sizeof(dcmd));
  if (NCMD > 900) NCMD = 900;
  if (NPTS > 4000) NPTS = 4000;
  ncmd = npts = 0, bg_col = c;
}

#if NP_SOFT_FLOAT
/* (calculators without a floating-point unit: the shapes go in as eighths of a pixel, read from the floats' bits) */
#include "np_fixed.h"
static inline int32_t q8i(float v) { return (int32_t)((fx_from_float(v, 4) + 1) >> 1); } /* (to the nearest) */
static int32_t slope[4000]; /* each edge's x a row (16.16), worked out once a frame; INT32_MIN: too flat for that */
static void push_q(int type, const int32_t *q, int n, uint8_t col) {
  if (ncmd == NCMD || npts + n > NPTS || n < 1) return;
  int32_t y0 = INT32_MAX, y1 = INT32_MIN, x0 = INT32_MAX, x1 = INT32_MIN;
  for (int i = 0; i < n; i++) {
    int32_t x = q[2 * i], y = q[2 * i + 1];
    y0 = y < y0 ? y : y0, y1 = y > y1 ? y : y1, x0 = x < x0 ? x : x0, x1 = x > x1 ? x : x1;
  }
  if (y1 < 0 || y0 > 240 * 8 || x1 < 0 || x0 > 320 * 8) return;
  dcmd *c = &cmds[ncmd++];
  c->type = (uint8_t)type, c->col = col, c->n = (uint16_t)n, c->at = (uint16_t)npts;
  c->y0 = (int16_t)((y0 > -8 ? y0 : -8) >> 3), c->y1 = (int16_t)(((y1 < 241 * 8 ? y1 : 241 * 8) + 7) >> 3);
  for (int i = 0; i < 2 * n; i++) {
    int32_t v = q[i];
    pts[2 * npts + i] = (int16_t)(v > 32000 ? 32000 : v < -32000 ? -32000 : v);
  }
  if (type == D_CONVEX || type == D_POLY)
    for (int i = 0; i < n; i++) {
      const int16_t *a = pts + 2 * (npts + i), *b = pts + 2 * (npts + (i + 1 == n ? 0 : i + 1));
      int32_t d = b[1] - a[1], sign = d < 0 ? -1 : 1;
      if (!d) { slope[npts + i] = 0; continue; } /* (never crossed) */
      uint32_t ad = (uint32_t)(d * sign);
      int sh = __builtin_clz(ad);
      int64_t k = (int64_t)(b[0] - a[0]) * sign * fx_rcp(ad << sh) >> (45 - sh);
      slope[npts + i] = k > INT32_MAX || k <= INT32_MIN ? INT32_MIN : (int32_t)k;
    }
  npts += n;
}
static void push_shape(int type, const float *xy, int n, uint8_t col) {
  int32_t q[2 * 64];
  if (n > 64) n = 64;
  for (int i = 0; i < 2 * n; i++) q[i] = q8i(xy[i]);
  push_q(type, q, n, col);
}
#else
static inline int16_t q8(float v) {
  v *= 8;
  if (v > 32000) v = 32000;
  if (v < -32000) v = -32000;
  return (int16_t)lrintf(v);
}
static void push_shape(int type, const float *xy, int n, uint8_t col) {
  if (ncmd == NCMD || npts + n > NPTS || n < 1) return;
  float y0 = 1e9f, y1 = -1e9f, x0 = 1e9f, x1 = -1e9f;
  for (int i = 0; i < n; i++) {
    y0 = fminf(y0, xy[2 * i + 1]), y1 = fmaxf(y1, xy[2 * i + 1]);
    x0 = fminf(x0, xy[2 * i]), x1 = fmaxf(x1, xy[2 * i]);
  }
  if (y1 < 0 || y0 > 240 || x1 < 0 || x0 > 320) return;
  dcmd *c = &cmds[ncmd++];
  c->type = (uint8_t)type, c->col = col, c->n = (uint16_t)n, c->at = (uint16_t)npts;
  c->y0 = (int16_t)floorf(fmaxf(y0, -1)), c->y1 = (int16_t)ceilf(fminf(y1, 241));
  for (int i = 0; i < n; i++) pts[2 * npts] = q8(xy[2 * i]), pts[2 * npts + 1] = q8(xy[2 * i + 1]), npts++;
}
#endif
static void fill_convex(const float *xy, int n, uint8_t col) { push_shape(D_CONVEX, xy, n, col); }
static void fill_poly(const float *xy, int n, uint8_t col) { push_shape(D_POLY, xy, n, col); }
static void disc(float cx, float cy, float rad, uint8_t c) {
  float q[4] = {cx - rad, cy - rad, cx + rad, cy + rad};
  int k = ncmd;
  push_shape(D_DISC, q, 2, c);
  if (ncmd > k) cmds[k].y0 = (int16_t)floorf(cy - rad), cmds[k].y1 = (int16_t)ceilf(cy + rad);
}
static void line(float x0, float y0, float x1, float y1, uint8_t c, int thick) {
  float q[4] = {x0, y0, x1, y1};
  int k = ncmd;
  push_shape(D_LINE, q, 2, c);
  if (ncmd > k) cmds[k].type = D_LINE, cmds[k].y1 = (int16_t)(cmds[k].y1 + 1), cmds[k].n = 2 | (uint16_t)(thick << 8);
}
void fb_fill(int x, int y, int w, int h, uint8_t c) {
  float q[4] = {(float)x, (float)y, (float)(x + w), (float)(y + h)};
  push_shape(D_RECT, q, 2, c);
}

/* ---------------------------------------------------------------- filling a strip */
static color_t *strip;
static int sy0, sy1;
static inline void span(int y, int a, int b, color_t c) {
  if (a < 0) a = 0;
  if (b > 319) b = 319;
  color_t *p = strip + (y - sy0) * SCREEN_W;
#if NP_SOFT_FLOAT
  if (b - a >= 6) { /* (two pixels a store) */
    typedef uint32_t __attribute__((may_alias)) word;
    color_t *e = p + b + 1;
    p += a;
    if ((uintptr_t)p & 2) *p++ = c;
    word w = c | (uint32_t)c << 16;
    for (; p + 2 <= e; p += 2) *(word *)p = w;
    if (p < e) *p = c;
    return;
  }
#endif
  for (int x = a; x <= b; x++) p[x] = c;
}
static void raster(const dcmd *c) {
  const int16_t *p = pts + 2 * c->at;
  color_t col = pal[c->col];
  int r0 = c->y0 > sy0 ? c->y0 : sy0, r1 = c->y1 < sy1 - 1 ? c->y1 : sy1 - 1;
  switch (c->type) {
  case D_CONVEX:
  case D_POLY: {
    int n = c->n;
#if NP_SOFT_FLOAT
    /* (calculators without a floating-point unit: each crossing in 16.16 from the corners' 1/8 pixels and the edge's
     * slope, or for an edge too flat for one, its division a reciprocal) */
    const int32_t *sl = slope + c->at;
    for (int y = r0; y <= r1; y++) {
      int32_t yc = 8 * y + 4, xs[48];
      int m = 0;
      for (int i = 0; i < n && m < 48; i++) {
        int j = i + 1 == n ? 0 : i + 1;
        int32_t ax = p[2 * i], ay = p[2 * i + 1], bx = p[2 * j], by = p[2 * j + 1];
        if ((ay <= yc) == (by <= yc)) continue;
        if (sl[i] != INT32_MIN) {
          xs[m++] = (int32_t)(((int64_t)ax * 65536 + (int64_t)(yc - ay) * sl[i]) >> 3);
          continue;
        }
        int32_t d = by - ay, sign = d < 0 ? -1 : 1;
        uint32_t ad = (uint32_t)(d * sign);
        int sh = __builtin_clz(ad);
        int64_t q = (int64_t)(yc - ay) * (bx - ax) * sign * fx_rcp(ad << sh) >> (45 - sh);   /* 1/8 pixels, 16.16 */
        xs[m++] = (int32_t)(((int64_t)ax * 65536 + q) >> 3);
      }
      if (c->type == D_CONVEX) {
        if (m < 2) continue;
        int32_t xl = xs[0], xr = xs[0];
        for (int i = 1; i < m; i++) xl = xs[i] < xl ? xs[i] : xl, xr = xs[i] > xr ? xs[i] : xr;
        span(y, (xl - 32768 + 65535) >> 16, (xr - 32768) >> 16, col);
        continue;
      }
      for (int i = 1; i < m; i++)
        for (int j = i; j > 0 && xs[j - 1] > xs[j]; j--) { int32_t t = xs[j]; xs[j] = xs[j - 1], xs[j - 1] = t; }
      for (int i = 0; i + 1 < m; i += 2) span(y, (xs[i] - 32768 + 65535) >> 16, (xs[i + 1] - 32768) >> 16, col);
    }
    break;
#endif
    for (int y = r0; y <= r1; y++) {
      float yc = y + 0.5f, xs[48];
      int m = 0;
      for (int i = 0; i < n && m < 48; i++) {
        int j = i + 1 == n ? 0 : i + 1;
        float ax = p[2 * i] * 0.125f, ay = p[2 * i + 1] * 0.125f, bx = p[2 * j] * 0.125f, by = p[2 * j + 1] * 0.125f;
        if ((ay <= yc) == (by <= yc)) continue;
        xs[m++] = ax + (yc - ay) * (bx - ax) / (by - ay);
      }
      if (c->type == D_CONVEX) {
        if (m < 2) continue;
        float xl = xs[0], xr = xs[0];
        for (int i = 1; i < m; i++) xl = fminf(xl, xs[i]), xr = fmaxf(xr, xs[i]);
        span(y, (int)ceilf(xl - 0.5f), (int)floorf(xr - 0.5f), col);
        continue;
      }
      for (int i = 1; i < m; i++)
        for (int j = i; j > 0 && xs[j - 1] > xs[j]; j--) { float t = xs[j]; xs[j] = xs[j - 1], xs[j - 1] = t; }
      for (int i = 0; i + 1 < m; i += 2) span(y, (int)ceilf(xs[i] - 0.5f), (int)floorf(xs[i + 1] - 0.5f), col);
    }
    break;
  }
  case D_DISC: {
#if NP_SOFT_FLOAT
    if (p[2] - p[0] < 4000) {
      /* (in 1/16 pixels, the half-width a square root of 32 bits) */
      int32_t cx = p[0] + p[2], cy = p[1] + p[3], rad = p[2] - p[0];
      for (int y = r0; y <= r1; y++) {
        int32_t dy = 16 * y + 8 - cy, w2 = rad * rad - dy * dy;
        if (w2 < 0) continue;
        int32_t w = (int32_t)fx_isqrt((uint32_t)w2 << 8);   /* 1/256 pixels */
        int32_t a = (cx << 4) - w - 128, b = (cx << 4) + w - 128;
        span(y, (a + 255) >> 8, b >> 8, col);
      }
      break;
    }
#endif
    float cx = (p[0] + p[2]) * 0.0625f, cy = (p[1] + p[3]) * 0.0625f, rad = (p[2] - p[0]) * 0.0625f;
    for (int y = r0; y <= r1; y++) {
      float dy = y + 0.5f - cy, w2 = rad * rad - dy * dy;
      if (w2 < 0) continue;
      float w = sqrtf(w2);
      span(y, (int)ceilf(cx - w - 0.5f), (int)floorf(cx + w - 0.5f), col);
    }
    break;
  }
  case D_LINE: {
#if NP_SOFT_FLOAT
    {
      /* (in 16.16, stepped: n steps of the longer side's pixels) */
      int32_t dx = p[2] - p[0], dy = p[3] - p[1], ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
      int thick = c->n >> 8, n = ((ax > ay ? ax : ay) >> 3) + 1;
      int32_t x = p[0] * 8192, y = p[1] * 8192, sx = dx * 8192 / n, sy = dy * 8192 / n;
      for (int i = 0; i <= n; i++, x += sx, y += sy) {
        int px = x < 0 ? -(-x >> 16) : x >> 16, py = y < 0 ? -(-y >> 16) : y >> 16; /* (toward zero, as (int)) */
        for (int k = 0; k < (thick > 1 ? 4 : 1); k++) {
          int xx = px + (k & 1), yy = py + (k >> 1);
          if (xx >= 0 && xx < 320 && yy >= sy0 && yy < sy1) strip[(yy - sy0) * SCREEN_W + xx] = col;
        }
      }
      break;
    }
#endif
    float x0 = p[0] * 0.125f, y0 = p[1] * 0.125f, dx = p[2] * 0.125f - x0, dy = p[3] * 0.125f - y0;
    int thick = c->n >> 8, n = (int)(fmaxf(fabsf(dx), fabsf(dy)) + 1);
    for (int i = 0; i <= n; i++) {
      int x = (int)(x0 + dx * i / n), y = (int)(y0 + dy * i / n);
      for (int k = 0; k < (thick > 1 ? 4 : 1); k++) {
        int xx = x + (k & 1), yy = y + (k >> 1);
        if (xx >= 0 && xx < 320 && yy >= sy0 && yy < sy1) strip[(yy - sy0) * SCREEN_W + xx] = col;
      }
    }
    break;
  }
  case D_RECT: {
    int x0 = p[0] >> 3, x1 = (p[2] >> 3) - 1, ya = (p[1] >> 3) > r0 ? (p[1] >> 3) : r0, yb = (p[3] >> 3) - 1 < r1 ? (p[3] >> 3) - 1 : r1;
    for (int y = ya; y <= yb; y++) span(y, x0, x1, col);
    break;
  }
  }
}

static void (*hud_fn)(void);
static void scene(void *ctx) {
  (void)ctx;
  strip = gfx_buf, sy0 = gfx_y0, sy1 = gfx_y1;
  color_t bg = pal[bg_col];
#if NP_SOFT_FLOAT
  for (int y = sy0; y < sy1; y++) span(y, 0, 319, bg);
#else
  for (int i = 0; i < SCREEN_W * (sy1 - sy0); i++) strip[i] = bg;
#endif
  for (int i = 0; i < ncmd; i++)
    if (cmds[i].y1 >= sy0 && cmds[i].y0 < sy1) raster(&cmds[i]);
  if (hud_fn) hud_fn();
}
#ifdef HOST
int draw_most_cmds, draw_most_pts;
#endif
void draw_flush(void (*hud)(void)) {
  hud_fn = hud;
#ifdef HOST
  if (ncmd > draw_most_cmds) draw_most_cmds = ncmd;
  if (npts > draw_most_pts) draw_most_pts = npts;
#endif
  gfx_render(scene, 0, 0, SCREEN_H);
}

/* ---------------------------------------------------------------- colours */
static uint32_t mix888(uint32_t a, uint32_t b, float t) {
  uint32_t o = 0;
  for (int s = 0; s < 24; s += 8) {
    float ca = (a >> s) & 255, cb = (b >> s) & 255;
    o |= (uint32_t)(ca + (cb - ca) * t + 0.5f) << s;
  }
  return o;
}
static uint32_t scale888(uint32_t a, float t) { return mix888(0, a, t); }
static color_t c565(uint32_t c) { return RGB((c >> 16) & 255, (c >> 8) & 255, c & 255); }

static uint32_t theme_body; /* (the body's colour, for the flash) */
void draw_theme(int theme) {
  const uint32_t *t = themes[theme];
  uint32_t bg = scale888(t[3], 0.31f), ground = scale888(t[3], 0.49f), red = 0xFF5959;
  uint32_t c[C_TRAIL + 8] = {0};
  c[C_BG] = bg, c[C_GROUND] = ground;
  c[C_HALO] = mix888(bg, t[2], 0.4f), c[C_GLOW] = t[2], c[C_CORE] = t[1];
  c[C_EN_HALO] = mix888(bg, 0xFF0000, 0.4f), c[C_EN] = red;
  c[C_BODY] = t[0], c[C_BODY_DARK] = scale888(t[0], 0.55f), c[C_WHITE] = 0xFFFFFF, c[C_WHEEL_HUB] = mix888(bg, 0, 0.3f);
  c[C_GEM_HALO] = mix888(bg, t[6], 0.45f), c[C_GEM] = t[5];
  c[C_BOOST] = 0x2E7BFF, c[C_BOOST_DIM] = mix888(bg, 0x2E7BFF, 0.45f), c[C_FINISH] = mix888(t[2], 0xFFFFFF, 0.5f);
  c[C_SPARK] = 0xFFD46A, c[C_SHADE] = 0x000000;
  for (int i = 0; i < 8; i++) c[C_TRAIL + i] = mix888(t[4], bg, i / 8.0f);
  for (int i = 0; i < C_TRAIL + 8; i++) pal[i] = c565(c[i]);
  for (int i = 0; i < 4; i++) pal[C_RING + i] = c565(mix888(0xFFFFFF, bg, i / 4.0f));
  theme_body = t[0], pal[C_FLASH] = pal[C_BODY];
}

/* the polygon grown (d > 0) or shrunk (d < 0) by d pixels; with long_only, only its long sides move (a strip's
 * pieces keep meeting end to end) */
static void offset_poly(const float *xy, int n, float d, bool long_only, float *out) {
  float area = 0;
  for (int i = 0; i < n; i++) area += xy[2 * i] * xy[2 * ((i + 1) % n) + 1] - xy[2 * ((i + 1) % n)] * xy[2 * i + 1];
  float sgn = area > 0 ? 1 : -1;
  float nx[24], ny[24], dd[24], ln[24];
  for (int i = 0; i < n; i++) {
    float ex = xy[2 * ((i + 1) % n)] - xy[2 * i], ey = xy[2 * ((i + 1) % n) + 1] - xy[2 * i + 1];
    float len = sqrtf(ex * ex + ey * ey);
    if (len < 1e-4f) len = 1e-4f;
    nx[i] = sgn * ey / len, ny[i] = -sgn * ex / len, ln[i] = len;
    dd[i] = long_only && len < 9 ? 0 : d;
  }
  if (long_only && n == 4) { /* a strip's piece: its two short sides are where it meets the next */
    int a = ln[0] + ln[2] < ln[1] + ln[3] ? 0 : 1;
    dd[a] = dd[a + 2] = 0, dd[a + 1] = dd[(a + 3) % 4] = d;
  }
  for (int i = 0; i < n; i++) {
    int p = (i + n - 1) % n;
    float det = nx[p] * ny[i] - ny[p] * nx[i];
    float ox, oy;
    if (fabsf(det) < 0.05f) ox = nx[i] * dd[i], oy = ny[i] * dd[i];
    else ox = (dd[p] * ny[i] - dd[i] * ny[p]) / det, oy = (nx[p] * dd[i] - nx[i] * dd[p]) / det;
    float m = ox * ox + oy * oy, lim = 4 * d * d;
    if (m > lim) { float k = sqrtf(lim / m); ox *= k, oy *= k; }
    out[2 * i] = xy[2 * i] + ox, out[2 * i + 1] = xy[2 * i + 1] + oy;
  }
}

/* the light core of a line's piece: the piece squeezed across its length (so pieces still meet end to end), or
 * for a wide piece its inside */
static int core_poly(const float *xy, int n, float *out) {
  float cx = 0, cy = 0;
  for (int i = 0; i < n; i++) cx += xy[2 * i], cy += xy[2 * i + 1];
  cx /= n, cy /= n;
  float sxx = 0, syy = 0, sxy = 0;
  for (int i = 0; i < n; i++) {
    float dx = xy[2 * i] - cx, dy = xy[2 * i + 1] - cy;
    sxx += dx * dx, syy += dy * dy, sxy += dx * dy;
  }
  float th = 0.5f * atan2f(2 * sxy, sxx - syy), ux = cosf(th), uy = sinf(th);
  float lo = 1e9f, hi = -1e9f;
  for (int i = 0; i < n; i++) {
    float p = -(xy[2 * i] - cx) * uy + (xy[2 * i + 1] - cy) * ux;
    lo = fminf(lo, p), hi = fmaxf(hi, p);
  }
  float thick = hi - lo;
  if (thick > 9) { offset_poly(xy, n, -2.5f, false, out); return -n; } /* (drawn dark inside: an outline) */
  if (thick < 2.5f) return 0;
  float k = 1 - 2.4f / thick, mid = (hi + lo) / 2;
  for (int i = 0; i < n; i++) {
    float dx = xy[2 * i] - cx, dy = xy[2 * i + 1] - cy, a = dx * ux + dy * uy, p = -dx * uy + dy * ux;
    p = mid + (p - mid) * k;
    out[2 * i] = cx + a * ux - p * uy, out[2 * i + 1] = cy + a * uy + p * ux;
  }
  return n;
}

/* ---------------------------------------------------------------- the bike */
static int bike_shape(int bike, float x, float y, float a, float z, float *xy) {
  const rd_bike *bk = &rd_bikes[bike];
  float c = cosf(a), s = sinf(a);
  int n = bk->on > 48 ? 48 : bk->on;
  for (int i = 0; i < n; i++) {
    float lx = bike_xy[2 * (bk->o0 + i)] * 0.5f * z, ly = bike_xy[2 * (bk->o0 + i) + 1] * 0.5f * z;
    xy[2 * i] = x + c * lx - s * ly, xy[2 * i + 1] = y + s * lx + c * ly;
  }
  return n;
}
#define WHEEL_R 8.4f
void draw_wheels_at(float x0, float y0, float x1, float y1, float z, int front) {
  (void)front;
  disc(x0, y0, WHEEL_R * z, C_WHITE), disc(x0, y0, WHEEL_R * z * 0.35f, C_WHEEL_HUB);
  disc(x1, y1, WHEEL_R * z, C_WHITE), disc(x1, y1, WHEEL_R * z * 0.35f, C_WHEEL_HUB);
}
static void bike_body_at(int bike, float x, float y, float a, float z, int glow, uint8_t col) {
  float xy[96], g[96];
  int n = bike_shape(bike, x, y, a, z, xy);
  if (glow) { /* the white edge: the shape drawn one pixel bigger in white first */
    for (int d = 0; d < 4; d++) {
      float ox = d == 0 ? 1 : d == 1 ? -1 : 0, oy = d == 2 ? 1 : d == 3 ? -1 : 0;
      for (int i = 0; i < n; i++) g[2 * i] = xy[2 * i] + ox, g[2 * i + 1] = xy[2 * i + 1] + oy;
      fill_poly(g, n, C_WHITE);
    }
  }
  fill_poly(xy, n, col);
}
/* GameCharacter.perfectFlip: the body turns white and gets its colour back in 30 steps of 16 ms; at each wheel a
   ring (motor_wheel_outline, 68 pixels to the wheel's 36) grows from nothing to 1.5 times its size and fades out in
   gameCharacterPerfectLandingAnimationTime (250 ms, evenly: the game misspells its easing), and a second one 100 ms
   later (perfectLandingAnimationDelayBtwinRepeat) */
static uint8_t perfect_flash(const rd_run *r) {
  if (r->perfect_fx <= 0) return C_BODY;
  int k = (int)((30 * 16 - r->perfect_fx) / 16);
  pal[C_FLASH] = c565(mix888(0xFFFFFF, theme_body, k >= 30 ? 1 : k / 30.0f));
  return C_FLASH;
}
static void perfect_rings(const rd_run *r, float x0, float y0, float x1, float y1) {
  if (r->perfect_fx <= 0) return;
  float e = 30 * 16 - r->perfect_fx;
  for (int k = 0; k < 2; k++) {
    float t = (e - k * 100) / 250;
    if (t < 0 || t >= 1) continue;
    float rad = WHEEL_R * 68 / 36 * ZOOM * (0.0025f + 1.4975f * t);
    uint8_t c = (uint8_t)(C_RING + (int)(t * 4));
    for (int w = 0; w < 2; w++) {
      float cx = w ? x1 : x0, cy = w ? y1 : y0, px = cx + rad, py = cy;
      for (int i = 1; i <= 16; i++) {
        float a = i * 0.39269908f, qx = cx + cosf(a) * rad, qy = cy + sinf(a) * rad;
        line(px, py, qx, qy, c, 1);
        px = qx, py = qy;
      }
    }
  }
}
/* the bike as it stands (menus): wheels where GameCharacter puts them, x0.7 (x0.49 down) from the body's centre */
static void stand_wheels(float x, float y, float a, float z, float *w) {
  float c = cosf(a), s = sinf(a);
  for (int k = 0; k < 2; k++) {
    float lx = (k ? 38 : -34) * 0.7f * z, ly = (37 - 18) * 0.49f * z;
    w[2 * k] = x + c * lx - s * ly, w[2 * k + 1] = y + s * lx + c * ly;
  }
}
void draw_bike_at(int bike, float x, float y, float a, float z, int glow) {
  float w[4];
  stand_wheels(x, y, a, z, w);
  bool front = rd_bikes[bike].front;
  if (!front) draw_wheels_at(w[0], w[1], w[2], w[3], z, 0);
  bike_body_at(bike, x, y, a, z, glow, C_BODY);
  if (front) draw_wheels_at(w[0], w[1], w[2], w[3], z, 1);
}
void draw_bike_shape(int bike, float x, float y, float a, float z, uint8_t c) {
  float w[4], xy[96];
  stand_wheels(x, y, a, z, w);
  disc(w[0], w[1], WHEEL_R * z, c), disc(w[2], w[3], WHEEL_R * z, c);
  fill_poly(xy, bike_shape(bike, x, y, a, z, xy), c);
}

#if NP_SOFT_FLOAT
/* ---------------------------------------------------------------- (no floating-point unit) the track in integers
 * Each object is worked out once, the first time it's drawn: its own turn's cosine and sine, and its polygons' halo
 * and light core as sixteenths of a pixel off their corners, in its own frame, zoomed (growing, shrinking and
 * squeezing a polygon turn with it). Each frame then only turns and moves the corners, in 64ths of a pixel. */
#define OC_POOL 49152
static uint8_t oc_pool[OC_POOL];
static uint16_t oc_at[NOBJ]; /* where an object is in the pool, plus 1 (0: not worked out) */
static int oc_used;
static inline int32_t q14(float v) { return (int32_t)fx_from_float(v, 14); }
static inline int32_t q16(float v) { return (int32_t)((fx_from_float(v, 5) + 1) >> 1); } /* sixteenths, nearest */
static inline int8_t d16(float v) {
  int32_t q = q16(v);
  return (int8_t)(q > 127 ? 127 : q < -127 ? -127 : q);
}
/* polygon k of an object in its own frame, zoomed: as floats (pixels) and as 64ths of a pixel, as tpl_local has it */
static int oc_local_f(const rd_obj *o, int k, float *xy) {
  if (o->tpl == 0xFF) {
    if (k) return 0;
    float hw = fabsf(o->sx) / 2 * ZOOM, hh = fabsf(o->sy) / 2 * ZOOM, q[8] = {-hw, -hh, hw, -hh, hw, hh, -hw, hh};
    memcpy(xy, q, sizeof q);
    return 4;
  }
  int p = tpl_poly[o->tpl] + k;
  if (p >= tpl_poly[o->tpl + 1]) return 0;
  int v0 = poly_vert[p], n = poly_vert[p + 1] - v0;
  bool flip = signbit(o->sx) != signbit(o->sy);
  for (int j = 0; j < n; j++) {
    int v = v0 + (flip ? n - 1 - j : j);
    xy[2 * j] = tpl_xy[2 * v] * 0.25f * o->sx * ZOOM, xy[2 * j + 1] = tpl_xy[2 * v + 1] * 0.25f * o->sy * ZOOM;
  }
  return n;
}
static int oc_local(const rd_obj *o, int k, int32_t *q) {
  if (o->tpl == 0xFF) {
    if (k) return 0;
    int32_t hw = (int32_t)((fx_from_float(fabsf(o->sx), 5) + 1) >> 1), hh = (int32_t)((fx_from_float(fabsf(o->sy), 5) + 1) >> 1);
    q[0] = -hw, q[1] = -hh, q[2] = hw, q[3] = -hh, q[4] = hw, q[5] = hh, q[6] = -hw, q[7] = hh;
    return 4;
  }
  int p = tpl_poly[o->tpl] + k;
  if (p >= tpl_poly[o->tpl + 1]) return 0;
  int v0 = poly_vert[p], n = poly_vert[p + 1] - v0;
  bool flip = signbit(o->sx) != signbit(o->sy);
  /* (64ths of a pixel: a template point (quarter pixels) x 0.25 x the object's scale x ZOOM x 64, 8 times its scale;
     at most 81000 here, so x 2^13 fits an int32_t) */
  int32_t kx = (int32_t)fx_from_float(o->sx, 16), ky = (int32_t)fx_from_float(o->sy, 16);
  for (int j = 0; j < n; j++) {
    int v = v0 + (flip ? n - 1 - j : j);
    q[2 * j] = (tpl_xy[2 * v] * kx + 4096) >> 13, q[2 * j + 1] = (tpl_xy[2 * v + 1] * ky + 4096) >> 13;
  }
  return n;
}
static int oc_polys(const rd_obj *o) { return o->tpl == 0xFF ? 1 : tpl_poly[o->tpl + 1] - tpl_poly[o->tpl]; }
/* an object's cosine and sine (Q14), then (but the ground's) a byte a polygon: 0 no core, 1 a light core, 2 a dark
   inside; then four bytes a corner: the halo's and the core's offsets */
static const uint8_t *oc_get(int oi) {
  if (oc_at[oi]) return oc_pool + oc_at[oi] - 1;
  const rd_obj *o = &rd_objs[oi];
  int np = oc_polys(o), nc = 0;
  bool deco = o->kind != K_GROUND;
  float xy[16], h[16], c[16];
  if (deco)
    for (int k = 0; k < np; k++) nc += oc_local_f(o, k, xy);
  int need = (4 + (deco ? np + 4 * nc : 0) + 1) & ~1;
  if (oc_used + need > OC_POOL) memset(oc_at, 0, sizeof oc_at), oc_used = 0; /* (full: all again, as they're drawn) */
  uint8_t *d = oc_pool + oc_used;
  oc_at[oi] = (uint16_t)(oc_used + 1), oc_used += need;
  float a = o->rot * 0.017453292f;
  int16_t cs[2] = {(int16_t)q14(cosf(a)), (int16_t)q14(sinf(a))};
  memcpy(d, cs, sizeof cs);
  if (!deco) return d;
  uint8_t *mode = d + 4;
  int8_t *off = (int8_t *)(mode + np);
  for (int k = 0; k < np; k++) {
    int n = oc_local_f(o, k, xy);
    offset_poly(xy, n, o->kind == K_BOOST ? 1.5f : 2, false, h);
    int m = o->kind == K_ENEMY || o->kind == K_BOOST ? 0 : core_poly(xy, n, c);
    mode[k] = (uint8_t)(m > 0 ? 1 : m < 0 ? 2 : 0);
    for (int j = 0; j < n; j++, off += 4) {
      off[0] = d16(h[2 * j] - xy[2 * j]), off[1] = d16(h[2 * j + 1] - xy[2 * j + 1]);
      off[2] = m ? d16(c[2 * j] - xy[2 * j]) : 0, off[3] = m ? d16(c[2 * j + 1] - xy[2 * j + 1]) : 0;
    }
  }
  return d;
}
/* the corners turned and moved onto the screen (64ths of a pixel in, eighths out), each with its offset (off: 0 none,
   else the halo's or the core's, sixteenths) */
static void oc_turn(const int32_t *loc, const int8_t *off, int n, int32_t X, int32_t Y, int32_t C, int32_t S, int32_t *q) {
  for (int j = 0; j < n; j++) {
    int64_t lx = loc[2 * j] + (off ? off[4 * j] * 4 : 0), ly = loc[2 * j + 1] + (off ? off[4 * j + 1] * 4 : 0);
    q[2 * j] = (X + (int32_t)((C * lx - S * ly + 8192) >> 14) + 4) >> 3;
    q[2 * j + 1] = (Y + (int32_t)((S * lx + C * ly + 8192) >> 14) + 4) >> 3;
  }
}
/* an object's polygons for one pass (0 the ground, 1 the halos, 2 the lines and cores); cam: the camera's top left */
static void oc_draw(const rd_run *r, int i, int pass, float camx, float camy) {
  const rd_live *l = &r->live[i];
  const rd_obj *o = &rd_objs[l->obj];
  const uint8_t *d = oc_get(l->obj);
  int32_t X, Y, C, S;
  if (l->dyn == NO_DYN) {
    int16_t cs[2];
    memcpy(cs, d, sizeof cs);
    /* (its centre on the screen, in 64ths of a pixel: x32 in the world) */
    X = (int32_t)fx_from_float(o->cx + (float)r->seg[l->seg].x - camx, 5), Y = (int32_t)fx_from_float(o->cy - camy, 5);
    C = cs[0], S = cs[1];
  } else {
    float x, y, a;
    obj_pose(r, i, &x, &y, &a);
    X = (int32_t)fx_from_float(x - camx, 5), Y = (int32_t)fx_from_float(y - camy, 5);
    C = q14(cosf(a)), S = q14(sinf(a));
  }
  int kind = o->kind, np = oc_polys(o);
  bool en = kind == K_ENEMY;
  const uint8_t *mode = d + 4;
  const int8_t *off = (const int8_t *)(mode + np);
  int32_t loc[16], q[16];
  for (int k = 0; k < np; k++) {
    int n = oc_local(o, k, loc);
    if (pass == 0) {
      oc_turn(loc, 0, n, X, Y, C, S, q), push_q(D_CONVEX, q, n, C_GROUND);
      continue;
    }
    if (pass == 1) oc_turn(loc, off, n, X, Y, C, S, q), push_q(D_CONVEX, q, n, kind == K_BOOST ? C_BOOST_DIM : en ? C_EN_HALO : C_HALO);
    else {
      oc_turn(loc, 0, n, X, Y, C, S, q), push_q(D_CONVEX, q, n, kind == K_BOOST ? C_BOOST : en ? C_EN : C_GLOW);
      if (mode[k]) oc_turn(loc, off + 2, n, X, Y, C, S, q), push_q(D_CONVEX, q, n, mode[k] == 1 ? C_CORE : C_HALO);
    }
    off += 4 * n;
  }
}
/* the object is in sight (its box against the window, in integers when it's still) */
static bool oc_seen(const rd_run *r, int i, float wx0, float wy0, float wx1, float wy1) {
  const rd_live *l = &r->live[i];
  if (l->dyn == NO_DYN) {
    const rd_obj *o = &rd_objs[l->obj];
    int32_t ox = r->seg[l->seg].x;
    return !(o->x1 + ox < (int32_t)fx_from_float(wx0, 0) || o->x0 + ox > (int32_t)fx_from_float(wx1, 0) + 1 ||
             o->y1 < (int32_t)fx_from_float(wy0, 0) || o->y0 > (int32_t)fx_from_float(wy1, 0) + 1);
  }
  float bx0, by0, bx1, by1;
  obj_box(r, i, &bx0, &by0, &bx1, &by1);
  return !(bx1 < wx0 || bx0 > wx1 || by1 < wy0 || by0 > wy1);
}
#endif

/* ---------------------------------------------------------------- the run */
void draw_run(const rd_run *r) {
  float sh = r->ended == END_CRASH && r->shake > 0 ? r->shake * 20 : 0;
  float cx = r->camx + (sh ? (float)((int)(r->end_ms * 7) % 5 - 2) * sh * 0.3f : 0), cy = r->camy;
  draw_clear(C_BG);
  float wx0 = cx - 8, wx1 = cx + 320 / ZOOM + 8, wy0 = cy - 8, wy1 = cy + 240 / ZOOM + 8;
  float xy[64], o[64];
#if NP_SOFT_FLOAT
  (void)xy;
#endif
  /* three passes: the ground, the glow round the lines, the lines and the rest */
  for (int pass = 0; pass < 3; pass++) {
    for (int i = 0; i < r->nlive; i++) {
      const rd_live *l = &r->live[i];
      if (!(l->st & ST_USED) || (l->st & ST_GONE)) continue;
#if NP_SOFT_FLOAT
      const rd_obj *ob = &rd_objs[l->obj];
      int kind = ob->kind;
      if ((pass == 0) != (kind == K_GROUND)) continue;
      if (!oc_seen(r, i, wx0, wy0, wx1, wy1)) continue;
      if (kind != K_GEM && kind != K_FINISH) {
        oc_draw(r, i, pass, cx, cy);
        if (kind != K_BOOST || pass != 2) continue;
      }
#else
      float bx0, by0, bx1, by1;
      obj_box(r, i, &bx0, &by0, &bx1, &by1);
      if (bx1 < wx0 || bx0 > wx1 || by1 < wy0 || by0 > wy1) continue;
      const rd_obj *ob = &rd_objs[l->obj];
      int kind = ob->kind;
      if ((pass == 0) != (kind == K_GROUND)) continue;
#endif
      float px, py, pa;
      obj_pose(r, i, &px, &py, &pa);
      if (kind == K_GEM) {
        if (pass != 2) continue;
        float sx = (px - cx) * ZOOM, sy = (py - cy) * ZOOM, w = fabsf(ob->sx) * 0.14f * ZOOM, h = fabsf(ob->sy) * 0.18f * ZOOM;
        float d[8] = {sx, sy - h, sx + w, sy, sx, sy + h, sx - w, sy};
        offset_poly(d, 4, 2, false, o);
        fill_convex(o, 4, C_GEM_HALO);
        fill_convex(d, 4, C_GEM);
        continue;
      }
      if (kind == K_FINISH) {
        if (pass != 2) continue;
        int sx = (int)((px - cx) * ZOOM), top = (int)((py - fabsf(ob->sy) / 2 - cy) * ZOOM), bot = (int)((py + fabsf(ob->sy) / 2 - cy) * ZOOM);
        fb_fill(sx - 3, top, 1, bot - top, C_HALO), fb_fill(sx + 3, top, 1, bot - top, C_HALO);
        for (int y = top; y < bot; y += 6) fb_fill(sx - 2, y, 5, bot - y < 6 ? bot - y : 6, ((y - top) / 6) & 1 ? C_WHITE : C_FINISH);
        continue;
      }
#if !NP_SOFT_FLOAT
      for (int k = 0;; k++) {
        int n = obj_poly(r, i, k, xy);
        if (!n) break;
        if (n > 24) n = 24;
        for (int j = 0; j < n; j++) xy[2 * j] = (xy[2 * j] - cx) * ZOOM, xy[2 * j + 1] = (xy[2 * j + 1] - cy) * ZOOM;
        if (pass == 0) { fill_convex(xy, n, C_GROUND); continue; }
        if (kind == K_BOOST) {
          if (pass == 1) offset_poly(xy, n, 1.5f, false, o), fill_convex(o, n, C_BOOST_DIM);
          else fill_convex(xy, n, C_BOOST);
          continue;
        }
        bool en = kind == K_ENEMY;
        if (pass == 1) {
          offset_poly(xy, n, 2, false, o);
          fill_convex(o, n, en ? C_EN_HALO : C_HALO);
        } else {
          fill_convex(xy, n, en ? C_EN : C_GLOW);
          int m = en ? 0 : core_poly(xy, n, o);
          if (m) fill_convex(o, m > 0 ? m : -m, m > 0 ? C_CORE : C_HALO);
        }
      }
#endif
      /* speed pads: arrows running along */
      if (kind == K_BOOST && pass == 2) {
        float c = cosf(pa), s = sinf(pa), ph = fmodf(r->ms / 8, 24);
        for (int k = -2; k <= 1; k++) {
          float ax = (k * 24 + ph) * ZOOM, bx = (px - cx) * ZOOM, by = (py - cy) * ZOOM - 4;
          float qx = bx + c * ax, qy = by + s * ax;
          line(qx - c * 3 - s * 3, qy - s * 3 + c * 3, qx, qy, C_WHITE, 1);
          line(qx - c * 3 + s * 3, qy - s * 3 - c * 3, qx, qy, C_WHITE, 1);
        }
      }
    }
  }
  /* the trail */
  const rd_bikestate *b = &r->b;
  for (int i = 0; i + 1 < b->trail_n && !r->ended; i++) {
    float x0 = (b->trail_x[i] - cx) * ZOOM, y0 = (b->trail_y[i] - cy) * ZOOM;
    float x1 = (b->trail_x[i + 1] - cx) * ZOOM, y1 = (b->trail_y[i + 1] - cy) * ZOOM;
    line(x0, y0, x1, y1, (uint8_t)(C_TRAIL + i * 8 / TRAIL), i < TRAIL / 2 ? 2 : 1);
  }
  if (r->ended != END_CRASH) {
    float w0x = (b->wx[0] - cx) * ZOOM, w0y = (b->wy[0] - cy) * ZOOM, w1x = (b->wx[1] - cx) * ZOOM, w1y = (b->wy[1] - cy) * ZOOM;
    bool front = rd_bikes[r->bike].front;
    if (!front) draw_wheels_at(w0x, w0y, w1x, w1y, ZOOM, 0);
    bike_body_at(r->bike, (b->x - cx) * ZOOM, (b->y - cy) * ZOOM, b->a, ZOOM, 1, perfect_flash(r));
    if (front) draw_wheels_at(w0x, w0y, w1x, w1y, ZOOM, 1);
    perfect_rings(r, w0x, w0y, w1x, w1y);
  } else {
    for (int i = 0; i < r->nbits; i++) {
      const rd_bit *p = &r->bits[i];
      float x = (p->x - cx) * ZOOM, y = (p->y - cy) * ZOOM;
      if (p->size > 4) {
        float c = cosf(p->a) * p->size * ZOOM, s = sinf(p->a) * p->size * ZOOM;
        float q[8] = {x - c - s * 0.35f, y - s + c * 0.35f, x + c - s * 0.35f, y + s + c * 0.35f,
                      x + c + s * 0.35f, y + s - c * 0.35f, x - c + s * 0.35f, y - s - c * 0.35f};
        offset_poly(q, 4, 1, false, o), fill_convex(o, 4, C_WHITE), fill_convex(q, 4, C_BODY);
      } else if (r->end_ms < 900) {
        fb_fill((int)x, (int)y, 2, 1, p->c);
      }
    }
  }
}

/* little pictures for the texts over it */
void draw_gem_icon(float x, float y, float h) {
  float d[8] = {x, y - h, x + h * 0.75f, y, x, y + h, x - h * 0.75f, y}, o[8];
  offset_poly(d, 4, 1.5f, false, o);
  fill_convex(o, 4, C_GEM_HALO), fill_convex(d, 4, C_GEM);
}
void draw_star(float x, float y, float rad, uint8_t c) {
  float p[20];
  for (int i = 0; i < 10; i++) {
    float a = -1.5708f + i * 0.6283f, rr = i & 1 ? rad * 0.45f : rad;
    p[2 * i] = x + cosf(a) * rr, p[2 * i + 1] = y + sinf(a) * rr;
  }
  fill_poly(p, 10, c);
}
void draw_line(float x0, float y0, float x1, float y1, uint8_t c) { line(x0, y0, x1, y1, c, 1); }
void draw_disc(float x, float y, float rad, uint8_t c) { disc(x, y, rad, c); }
