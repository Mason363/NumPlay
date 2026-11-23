/* Balatro's animated paint background: its two shaders ('background' in a
 * run, 'splash' on the title screen) evaluated on a coarse grid, then
 * interpolated per pixel and turned into colours through a table. The paint
 * value is smooth, so interpolating it before the colour mapping keeps the
 * sharp colour bands of the original. */
#ifndef PROF_NOINLINE
#define PROF_NOINLINE
#endif
#include "bg.h"
extern uint32_t g_bg_version;
#include <string.h>

#define GW (320 / BG_CELL + 1)
#define GH (240 / BG_CELL + 1)

/* One grid of paint values, refreshed a few rows per frame. Each pass
 * samples the shader a pixel off in a different direction and blends into
 * the grid: over a few passes this averages the paint over each cell, which
 * is what a smaller picture of the original looks like (no aliasing). */
/* In a run the paint value is smooth, so the grid keeps values and colours
 * them per pixel (sharp bands). The title's swirl is much finer: there the
 * grid keeps colours, averaged over each cell, so it stays smooth. */
static union {
  uint8_t v[GH][GW]; /* run: paint value, 0..255 */
  C c[GH][GW];       /* title: colour */
} G_;
#define grid G_.v
#define cgrid G_.c
static int next_row, pass;
static uint8_t drow[2][GW];
static int drow_y = -1;
static float t_real, t_spin;
static C lut[256];
static float colours[3][3], contrast = 1, spin = 0, spin_target = 0;
static int mode; /* 0 run, 1 title */
static int lut_dirty = 1;
static float target[3][3], contrast_target = 1;

/* ------------------------------------------------------------ fast maths */
#define PI 3.14159265f
static float fsin(float x) {
  /* reduce to [-pi, pi] */
  float k = x * (1.0f / (2 * PI));
  k = (float)(int)(k + (k >= 0 ? 0.5f : -0.5f));
  x -= k * 2 * PI;
  /* parabola approximation with a correction (max error ~0.001) */
  float y = 1.27323954f * x - 0.405284735f * x * (x < 0 ? -x : x);
  return 0.225f * (y * (y < 0 ? -y : y) - y) + y;
}
static float fcos(float x) { return fsin(x + PI / 2); }
static float fatan2(float y, float x) {
  float ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
  float mx = ax > ay ? ax : ay, mn = ax > ay ? ay : ax;
  if (mx == 0) return 0;
  float a = mn / mx, s = a * a;
  float r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;
  if (ay > ax) r = PI / 2 - r;
  if (x < 0) r = PI - r;
  return y < 0 ? -r : r;
}
static float fsqrt(float x) { return __builtin_sqrtf(x); }
static float fabsf_(float x) { return x < 0 ? -x : x; }
static float fmaxf_(float a, float b) { return a > b ? a : b; }
static float fminf_(float a, float b) { return a < b ? a : b; }

/* the paint value at a screen point, 0..255 */
static int paint(float sx, float sy) {
  const float diag = 400.f;
  float ux = (sx - 160.f) / diag, uy = (sy - 120.f) / diag;
  float vx, vy, speed;
  if (mode == 0) ux -= 0.12f;
  float len = fsqrt(ux * ux + uy * uy);
  if (mode == 0) {
    float sp = t_spin * 0.5f * 0.2f + 302.2f;
    float ang = fatan2(uy, ux) + sp - 0.5f * 20.f * (spin * len + (1.f - spin));
    vx = len * fcos(ang) * 30.f;
    vy = len * fsin(ang) * 30.f;
    speed = t_real * 2.f;
  } else {
    float s = t_real * 0.4f, ms = fminf_(6.f, s);
    float ang = fatan2(uy, ux) + (2.2f + 0.4f * ms) * len - 1.f - s * 0.05f - ms * s * 0.02f;
    vx = len * fcos(ang) * 30.f;
    vy = len * fsin(ang) * 30.f;
    speed = t_real * 6.f * 0.4f + 1033.f;
  }
  float u2x = vx + vy, u2y = vx + vy;
  for (int i = 0; i < 5; i++) {
    float m = fsin(fmaxf_(vx, vy));
    u2x += m + vx;
    u2y += m + vy;
    vx += 0.5f * fcos(5.1123314f + 0.353f * u2y + speed * 0.131121f);
    vy += 0.5f * fsin(u2x - 0.113f * speed);
    float d = fcos(vx + vy) - fsin(vx * 0.711f - vy);
    vx -= d;
    vy -= d;
  }
  float l = fsqrt(vx * vx + vy * vy), r;
  if (mode == 0) {
    float cm = 0.25f * contrast + 0.5f * spin + 1.2f;
    r = fminf_(2.f, fmaxf_(0.f, l * 0.035f * cm)); /* 0..2 */
  } else {
    r = fminf_(2.f, fmaxf_(-2.f, 1.5f + l * 0.12f - 0.17f * 10.f));
    if (r < 0.2f) r = (r - 0.2f) * 0.6f + 0.2f;
    r = (r + 2.f) * 0.5f; /* -2..2 -> 0..2 */
  }
  int v = (int)(r * 127.5f + 0.5f);
  return v < 0 ? 0 : v > 255 ? 255 : v;
}

#if NP_SOFT_FLOAT
/* No FPU (the TI and Casio calculators): the same in fixed point, in 65536ths, the angles in turns of 2^32 and fsin
 * from a table of it (floats took 85% of the title's time). What changes only once a frame is worked out there. */
static int32_t sin_tab[1025]; /* fsin over a turn, 65536ths */
static struct {
  int32_t ux0, d1, k0;   /* ux's shift, the turn's change with len (65536ths of a radian), r's scale */
  uint32_t d0, p1, p2;   /* turns: the turn at the middle, and the two the time moves */
} fx;
static uint32_t turn_of(float rad) { /* (a float angle in turns of 2^32) */
  float t = rad * (1.f / (2 * PI));
  t -= (float)(int)t;
  if (t < 0) t += 1;
  return (uint32_t)(int64_t)(t * 4294967296.f);
}
static uint32_t turn_q(int32_t rad) { return (uint32_t)(((int64_t)rad * 683565276) >> 16); } /* (2^32 / 2 pi) */
static int32_t isin(uint32_t t) {
  int32_t a = sin_tab[t >> 22], b = sin_tab[(t >> 22) + 1];
  return a + (int32_t)(((int64_t)(b - a) * ((t >> 6) & 0xFFFF)) >> 16);
}
static int32_t icos(uint32_t t) { return isin(t + 0x40000000u); }
static uint32_t isqrt(uint32_t v) {
  uint32_t r = 0, b = 1u << 30;
  while (b > v) b >>= 2;
  for (; b; b >>= 2)
    if (v >= r + b) v -= r + b, r = (r >> 1) + b;
    else r >>= 1;
  return r;
}
static int32_t q16(float v) { return (int32_t)(v * 65536.f); }
static void fx_frame(void) {
  if (!sin_tab[256])
    for (int i = 0; i <= 1024; i++) sin_tab[i] = q16(fsin((float)i * (2 * PI / 1024)));
  float speed;
  if (mode == 0) {
    fx.ux0 = q16(-0.12f), fx.d1 = q16(-10.f * spin);
    fx.d0 = turn_of(t_spin * 0.5f * 0.2f + 302.2f - 10.f * (1.f - spin));
    fx.k0 = q16(0.035f * (0.25f * contrast + 0.5f * spin + 1.2f));
    speed = t_real * 2.f;
  } else {
    float s = t_real * 0.4f, ms = fminf_(6.f, s);
    fx.ux0 = 0, fx.d1 = q16(2.2f + 0.4f * ms), fx.d0 = turn_of(-1.f - s * 0.05f - ms * s * 0.02f), fx.k0 = 0;
    speed = t_real * 6.f * 0.4f + 1033.f;
  }
  fx.p1 = turn_of(5.1123314f + speed * 0.131121f), fx.p2 = turn_of(-0.113f * speed);
}
static int paint_q(int sx, int sy) {
  int32_t ux = (int32_t)(((int64_t)(sx - 160) * 10737418) >> 16) + fx.ux0; /* (/ 400) */
  int32_t uy = (int32_t)(((int64_t)(sy - 120) * 10737418) >> 16);
  int32_t len = (int32_t)isqrt((uint32_t)(ux * ux) + (uint32_t)(uy * uy));
  uint32_t d = fx.d0 + turn_q((int32_t)(((int64_t)fx.d1 * len) >> 16));
  int32_t c = icos(d), s = isin(d);
  /* len cos(atan2(uy, ux) + d) = ux cos d - uy sin d */
  int32_t vx = (int32_t)((((int64_t)ux * c - (int64_t)uy * s) >> 16) * 30);
  int32_t vy = (int32_t)((((int64_t)uy * c + (int64_t)ux * s) >> 16) * 30);
  int32_t u2x = vx + vy, u2y = vx + vy;
  for (int i = 0; i < 5; i++) {
    int32_t m = isin(turn_q(vx > vy ? vx : vy));
    u2x += m + vx, u2y += m + vy;
    vx += icos(fx.p1 + turn_q((int32_t)(((int64_t)u2y * 23134) >> 16))) >> 1; /* (0.353) */
    vy += isin(fx.p2 + turn_q(u2x)) >> 1;
    int32_t e = icos(turn_q(vx + vy)) - isin(turn_q((int32_t)(((int64_t)vx * 46596) >> 16) - vy)); /* (0.711) */
    vx -= e, vy -= e;
  }
  int32_t ax = vx >> 8, ay = vy >> 8; /* (256ths: the squares fit) */
  int32_t l = (int32_t)isqrt((uint32_t)(ax * ax) + (uint32_t)(ay * ay)) << 8, r;
  if (mode == 0) {
    r = (int32_t)(((int64_t)l * fx.k0) >> 16);
    r = r < 0 ? 0 : r > 2 * 65536 ? 2 * 65536 : r;
  } else {
    r = q16(1.5f - 1.7f) + (int32_t)(((int64_t)l * 7864) >> 16); /* (0.12) */
    r = r < -2 * 65536 ? -2 * 65536 : r > 2 * 65536 ? 2 * 65536 : r;
    if (r < 13107) r = (int32_t)(((int64_t)(r - 13107) * 39322) >> 16) + 13107; /* (0.2, 0.6) */
    r = (r + 2 * 65536) >> 1;
  }
  int v = (int)(((int64_t)r * 255 + 65536) >> 17); /* (r 127.5 + 0.5) */
  return v < 0 ? 0 : v > 255 ? 255 : v;
}
#define PAINT(x, y) paint_q((int)(x), (int)(y))
#define FRAME() fx_frame()
#else
#define PAINT(x, y) paint((float)(x), (float)(y))
#define FRAME() ((void)0)
#endif

static void make_lut(void) {
  for (int i = 0; i < 256; i++) {
    float r = i / 127.5f, out[3];
    if (mode == 0) {
      float cm = 0.25f * contrast + 0.5f * spin + 1.2f;
      float c1p = fmaxf_(0.f, 1.f - cm * fabsf_(1.f - r));
      float c2p = fmaxf_(0.f, 1.f - cm * fabsf_(r));
      float c3p = 1.f - fminf_(1.f, c1p + c2p);
      float k = 0.3f / contrast;
      for (int j = 0; j < 3; j++)
        out[j] = k * colours[0][j] + (1 - k) * (colours[0][j] * c1p + colours[1][j] * c2p + colours[2][j] * c3p);
    } else {
      float s = r * 2.f - 2.f; /* back to -2..2 */
      float c1p = fmaxf_(0.f, 1.f - 2.f * fabsf_(1.f - s));
      float c2p = fmaxf_(0.f, 1.f - 2.f * s);
      float cb = 1.f - fminf_(1.f, c1p + c2p);
      static const float red[3] = {0xFE / 255.f, 0x5F / 255.f, 0x55 / 255.f};
      static const float blue[3] = {0x00, 0x9D / 255.f, 1.f};
      static const float black[3] = {0.6f * 79 / 255.f, 0.6f * 99 / 255.f, 0.6f * 103 / 255.f};
      float fl = fmaxf_(c1p, c2p) * 5.f - 4.4f;
      if (fl < 0) fl = 0;
      for (int j = 0; j < 3; j++) {
        float v = red[j] * c1p + blue[j] * c2p + black[j] * cb;
        out[j] = v * (1 - fl) + fl;
      }
    }
    int R = (int)(fminf_(1.f, fmaxf_(0.f, out[0])) * 255.f + .5f);
    int G = (int)(fminf_(1.f, fmaxf_(0.f, out[1])) * 255.f + .5f);
    int B = (int)(fminf_(1.f, fmaxf_(0.f, out[2])) * 255.f + .5f);
    lut[i] = RGB(R, G, B);
  }
  lut_dirty = 0;
}

void bg_mode(int m) {
  if (m != mode) {
    mode = m;
    lut_dirty = 1;
    bg_reset();
  }
}

static void set3(float d[3], uint32_t hex, float k) {
  d[0] = ((hex >> 16) & 255) / 255.f * k;
  d[1] = ((hex >> 8) & 255) / 255.f * k;
  d[2] = (hex & 255) / 255.f * k;
}

/* Balatro's ease_background_colour: special/tertiary colours are optional (0). */
void bg_colours(uint32_t col, uint32_t special, uint32_t tertiary, float contr, float spin_amount, int instant) {
  if (special && tertiary) {
    set3(target[1], col, 1.f);
    set3(target[0], special, 1.f);
    set3(target[2], tertiary, 1.f);
  } else {
    set3(target[1], col, 1.3f);
    set3(target[2], col, special ? 0.4f : 0.7f);
    if (special) set3(target[0], special, 1.f);
    else set3(target[0], col, 0.9f);
  }
  contrast_target = contr;
  spin_target = spin_amount;
  if (instant) {
    memcpy(colours, target, sizeof colours);
    contrast = contr;
    spin = spin_amount;
  }
  lut_dirty = 1;
}

/* the title's colour for one cell: the average over it (4 samples) */
static C cell_colour(int gx, int gy) {
  static const int8_t o[4][2] = {{-1, -1}, {1, 1}, {1, -1}, {-1, 1}};
  unsigned r = 2, g = 2, b = 2;
  for (int k = 0; k < 4; k++) {
    C c = lut[PAINT(gx * BG_CELL + o[k][0], gy * BG_CELL + o[k][1])];
    r += c >> 11, g += (c >> 5) & 63, b += c & 31;
  }
  return (C)((r >> 2) << 11 | (g >> 2) << 5 | (b >> 2));
}

void bg_reset(void) {
  next_row = 0;
  if (mode == 1 && t_real < 20) t_real = 20; /* the title's steady state */
  if (lut_dirty) make_lut();
  FRAME();
  for (int y = 0; y < GH; y++)
    for (int x = 0; x < GW; x++) {
      if (mode == 1) cgrid[y][x] = cell_colour(x, y);
      else grid[y][x] = (uint8_t)PAINT(x * BG_CELL, y * BG_CELL);
    }
  drow_y = -1;
}

/* Advance the animation by dt seconds, computing up to `rows` grid rows. */
void bg_update(float dt, int rows) {
  t_real += dt;
  t_spin += dt * spin;
  /* ease colours like the game (0.6 s) */
  float k = dt / 0.6f * 3.f;
  if (k > 1) k = 1;
  int moving = 0;
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++) {
      float d = target[i][j] - colours[i][j];
      if (d > 0.002f || d < -0.002f) moving = 1, colours[i][j] += d * k;
      else colours[i][j] = target[i][j];
    }
  float dc = contrast_target - contrast, ds = spin_target - spin;
  if (dc > 0.01f || dc < -0.01f) moving = 1, contrast += dc * k;
  else contrast = contrast_target;
  if (ds > 0.001f || ds < -0.001f) moving = 1, spin += ds * (dt * 2.f > 1 ? 1 : dt * 2.f);
  else spin = spin_target;
  if (moving) lut_dirty = 1;
  static const int8_t jit[4][2] = {{-1, -1}, {1, 1}, {1, -1}, {-1, 1}};
  int did = rows > 0;
  if (did) FRAME();
  while (rows-- > 0) {
    float jx = jit[pass & 3][0], jy = jit[pass & 3][1];
    if (mode == 1) {
      /* the title's swirl moves fast: no averaging over time, over space */
      if (lut_dirty) make_lut();
      C *row = cgrid[next_row];
      for (int x = 0; x < GW; x++) row[x] = cell_colour(x, next_row);
    } else {
      uint8_t *row = grid[next_row];
      for (int x = 0; x < GW; x++) {
        int v = PAINT(x * BG_CELL + jx, next_row * BG_CELL + jy);
        row[x] = (uint8_t)((row[x] * 3 + v + 2) >> 2);
      }
    }
    if (++next_row == GH) next_row = 0, pass++;
  }
  drow_y = -1;
  if (did || lut_dirty) g_bg_version++;
}

/* title: colours interpolated in both directions, the three channels at
 * once (RGB565 spread over 32 bits leaves room for the weights) */
/* (BG_CELL is 4: four pixels per cell below) */
#define SPREAD(c) (((uint32_t)(c) | (uint32_t)(c) << 16) & 0x07E0F81Fu)
static void bg_row_colour(C *dst, int y) {
  int gy = y / BG_CELL, fy = y % BG_CELL;
  const C *r0 = cgrid[gy], *r1 = cgrid[gy + 1 < GH ? gy + 1 : gy];
  uint32_t wa = BG_CELL - fy, wb = fy;
  uint32_t a = SPREAD(r0[0]) * wa + SPREAD(r1[0]) * wb; /* x4 */
  for (int gx = 0; gx < GW - 1; gx++) {
    uint32_t b = SPREAD(r0[gx + 1]) * wa + SPREAD(r1[gx + 1]) * wb;
    uint32_t p0 = a << 2, p1 = a * 3 + b, p2 = (a + b) << 1, p3 = a + b * 3; /* x16 */
    p0 = (p0 >> 4) & 0x07E0F81Fu, p1 = (p1 >> 4) & 0x07E0F81Fu;
    p2 = (p2 >> 4) & 0x07E0F81Fu, p3 = (p3 >> 4) & 0x07E0F81Fu;
    dst[0] = (C)(p0 | p0 >> 16), dst[1] = (C)(p1 | p1 >> 16);
    dst[2] = (C)(p2 | p2 >> 16), dst[3] = (C)(p3 | p3 >> 16);
    dst += 4;
    a = b;
  }
}

PROF_NOINLINE void bg_row(C *dst, int y, int x0, int x1) {
  (void)x1;
  if (lut_dirty) make_lut();
  if (mode == 1) {
    bg_row_colour(dst, y);
    return;
  }
  int gy = y / BG_CELL, fy = y % BG_CELL;
  if (gy != drow_y) {
    int gy1 = gy + 1 < GH ? gy + 1 : gy;
    memcpy(drow[0], grid[gy], GW);
    memcpy(drow[1], grid[gy1], GW);
    drow_y = gy;
  }
  const uint8_t *r0 = drow[0], *r1 = drow[1];
  unsigned wa = BG_CELL - fy, wb = fy;
  int g0 = x0 / BG_CELL; /* cells left of x0 are hidden */
  dst += g0 * BG_CELL;
  unsigned a = r0[g0] * wa + r1[g0] * wb;
  for (int gx = g0; gx < GW - 1; gx++) {
    unsigned b = r0[gx + 1] * wa + r1[gx + 1] * wb;
    int acc = (int)a * BG_CELL, d = (int)b - (int)a;
    for (int k = 0; k < BG_CELL; k++) {
      *dst++ = lut[acc >> 4];
      acc += d;
    }
    a = b;
  }
}
