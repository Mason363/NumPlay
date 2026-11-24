/* The backgrounds are those of NumVisuals (games/numvisuals), with the same
 * formulas, in its order. Time is the millisecond counter; the ones that move a
 * step at a time take NumVisuals' steps, 30 a second. */
#include <string.h>
#include "live.h"

#define W SCREEN_W
#define H SCREEN_H
#define RGB1(c) (color_t)((((c) >> 8) & 0xF800) | (((c) >> 5) & 0x07E0) | (((c) >> 3) & 0x1F))

enum { AURORA, SUNSET, PLASMA, PASTEL, LAVA, WARP, TUNNEL, FIRE, OCEAN, BOKEH, RAIN };

static int16_t *sn;     /* sine, -256..256 */
static color_t *pal;    /* 256 colours */
static int16_t *scr;    /* the background's tables */
static uint32_t scr_size;
static bool ready;
static int mode = LIVE_COUNT; /* none until Backspace picks one: the home screen as it always was */
static int now, last, acc;
static int plasma_k, plasma_speed;
static uint32_t seed;

#define S(a) sn[(a) & 255]
#define SCRATCH_MIN (W * 6 * (int)sizeof(int16_t)) /* Ocean's six waves; Tunnel and Fire take more */

/* Warp's stars, Tunnel's angle and depth for a quarter of the screen at half resolution, Fire's cells,
 * Bokeh's bubbles and Code Rain's columns, in scr */
#define NS 240
typedef struct { int16_t x[NS], y[NS], z[NS], px[NS], py[NS], qx[NS], qy[NS]; } warp_t;
typedef struct { uint8_t a[60][80], d[60][80]; } tunnel_t;
#define FW 80
#define FH 60
typedef struct { uint8_t v[FH + 1][FW]; int32_t acc; } fire_t;
#define NB 22
typedef struct { int16_t x[NB], y[NB], r[NB]; uint8_t v[NB], a[NB], c[NB]; } bokeh_t; /* y in 1/16 pixels */
#define MC 40
#define MR 24
typedef struct { int32_t h[MC]; uint8_t g[MR][MC], spd[MC], len[MC]; } rain_t; /* h: the head's row, in 1/256 */

static int clamp(int v, int a, int b) { return v < a ? a : v > b ? b : v; }
static int iabs(int v) { return v < 0 ? -v : v; }
static uint32_t rnd(void) {
  seed ^= seed << 13;
  seed ^= seed >> 17;
  seed ^= seed << 5;
  return seed;
}

/* f over b; a from 0 (all b) to 32 (all f) */
static color_t mixf(color_t f, color_t b, int a) {
  uint32_t x = (f | (uint32_t)f << 16) & 0x07E0F81F, y = (b | (uint32_t)b << 16) & 0x07E0F81F;
  y = (y + ((x - y) * (uint32_t)a >> 5)) & 0x07E0F81F;
  return (color_t)(y | y >> 16);
}

/* each background's colours, one after the other (Code Rain has its own) */
static const uint8_t ncol[LIVE_COUNT] = {5, 4, 6, 6, 5, 3, 5, 5, 4, 3, 0};
static const uint32_t cols[] = {
    0x02030C, 0x071A33, 0x0E6070, 0x33E8A0, 0xD2FFEA,           /* Aurora */
    0x14002E, 0x55106E, 0xC72C79, 0xFF8A5B,                     /* Sunset Drive */
    0x1B0B3A, 0x6B1FA8, 0xFF3E8A, 0xFFC857, 0x2EC4B6, 0x1B0B3A, /* Plasma */
    0xFFB3C7, 0xB9A2FF, 0x8FD8FF, 0x9EEDB6, 0xFFE08A, 0xFFB3C7, /* Pastel */
    0x12061F, 0x4A0F3F, 0xB8233B, 0xFF7A2E, 0xFFE7A6,           /* Lava Lamp */
    0x000004, 0x06061C, 0x140A2E,                               /* Warp */
    0x00E5FF, 0x7A2BFF, 0xFF2BB1, 0x7A2BFF, 0x00E5FF,           /* Tunnel */
    0x07050B, 0x4A0A0A, 0xC4260C, 0xFF8A1E, 0xFFF0B8,           /* Fire */
    0x0E1B45, 0x5A3C8C, 0xE9867A, 0xFFD6A0,                     /* Ocean */
    0x0B0520, 0x250A33, 0x3A0F2E,                               /* Bokeh */
};

static uint32_t need(int m) { return m == TUNNEL ? sizeof(tunnel_t) : m == FIRE ? sizeof(fire_t) : SCRATCH_MIN; }

static void star(warp_t *w, int i, int z) {
  w->x[i] = (int16_t)(rnd() % 2400) - 1200;
  w->y[i] = (int16_t)(rnd() % 1800) - 900;
  w->z[i] = (int16_t)z;
  w->px[i] = w->qx[i] = (int16_t)(160 + w->x[i] * 64 / z);
  w->py[i] = w->qy[i] = (int16_t)(120 + w->y[i] * 64 / z);
}
static void bubble(bokeh_t *b, int i, int y) {
  b->x[i] = (int16_t)(rnd() % W);
  b->r[i] = (int16_t)(8 + rnd() % 28);
  b->y[i] = (int16_t)((y + b->r[i]) * 16);
  b->v[i] = (uint8_t)(4 + rnd() % 12);
  b->a[i] = (uint8_t)(5 + rnd() % 8);
  b->c[i] = (uint8_t)(rnd() % 5);
}
static void drop(rain_t *r, int c) {
  r->h[c] = -(int32_t)(rnd() % (MR * 256));
  r->spd[c] = (uint8_t)(2 + rnd() % 4);
  r->len[c] = (uint8_t)(5 + rnd() % 14);
}
static float atn(float v) { /* atan, 0 <= v <= 1 */
  float s = v * v;
  return v * (0.9998660f + s * (-0.3302995f + s * (0.1801410f + s * (-0.0851330f + s * 0.0208351f))));
}

/* the chosen background from its beginning: its colours and tables */
static void start(void) {
  if (mode >= LIVE_COUNT) return;
  const uint32_t *c = cols;
  for (int i = 0; i < mode; i++) c += ncol[i];
  for (int i = 0, n = ncol[mode]; n && i < 256; i++) { /* a gradient through its colours */
    int p = i * (n - 1), k = p >> 8, f = p & 255;
    uint32_t o = 0;
    for (int s = 0; s < 24; s += 8)
      o |= (uint32_t)((((c[k] >> s) & 255) * (256 - f) + ((c[k + 1] >> s) & 255) * f) >> 8) << s;
    pal[i] = RGB1(o);
  }
  plasma_k = mode == PLASMA ? 10 : 6, plasma_speed = mode == PLASMA ? 16 : 8;
  seed = (0x2545F491u ^ np_millis()) | 1;
  acc = 0;
  if (mode == WARP) {
    for (int i = 0; i < NS; i++) star((warp_t *)scr, i, 64 + rnd() % 960);
  } else if (mode == TUNNEL) {
    tunnel_t *t = (tunnel_t *)scr;
    for (int y = 0; y < 60; y++)
      for (int x = 0; x < 80; x++) {
        float fx = x + 0.5f, fy = y + 0.5f, a = fx >= fy ? atn(fy / fx) : 1.5707964f - atn(fx / fy);
        t->a[y][x] = (uint8_t)(a * 40.743665f); /* 64 a quarter turn */
        int d = (int)(1400 / __builtin_sqrtf(fx * fx + fy * fy));
        t->d[y][x] = (uint8_t)(d > 255 ? 255 : d);
      }
  } else if (mode == FIRE) {
    memset(scr, 0, sizeof(fire_t));
  } else if (mode == BOKEH) {
    for (int i = 0; i < NB; i++) bubble((bokeh_t *)scr, i, (int)(rnd() % H) - 30);
  } else if (mode == RAIN) {
    rain_t *r = (rain_t *)scr;
    for (int k = 0; k < MC; k++) {
      for (int j = 0; j < MR; j++) r->g[j][k] = (uint8_t)(33 + rnd() % 94);
      drop(r, k);
      r->h[k] += MR * 128;
    }
  }
}

void live_init(void) {
  ready = false;
  scr_size = need(mode);
  sn = np_alloc(256 * sizeof(int16_t));
  pal = np_alloc(256 * sizeof(color_t));
  scr = np_alloc(scr_size);
  if (!sn || !pal || !scr) return;
  for (int i = 0; i < 128; i++) { /* Bhaskara's sine */
    float x = i * (3.14159265f / 128), q = x * (3.14159265f - x);
    sn[i] = (int16_t)(16 * q / (49.348022f - 4 * q) * 256 + 0.5f);
    sn[i + 128] = (int16_t)-sn[i];
  }
  ready = true;
  start();
}

bool live_ready(void) { return ready; }
bool live_active(void) { return ready && mode < LIVE_COUNT; }
int live_mode(void) { return mode; }
void live_set(int m) {
  mode = (m >= 0 && m <= LIVE_COUNT) ? m : LIVE_COUNT;
  if (need(mode) > scr_size) ready = false; /* (Tunnel's and Fire's tables: live_init makes room) */
  else if (ready) start();
}
void live_next(void) { live_set((mode + 1) % (LIVE_COUNT + 1)); }

/* ---------------------------------------------------------------- per frame */
static void frame_aurora(void) {
  int16_t *ca = scr, *cb = scr + W, *cc = scr + 2 * W;
  int t = now;
  for (int x = 0; x < W; x++) {
    ca[x] = (int16_t)(104 + (S(x + t / 29) * 22 + S(x * 2 - t / 41 + S(x / 2 + t / 67) / 6) * 12) / 256);
    cb[x] = (int16_t)(175 + S(x * 3 + t / 13) / 9 + S(x * 9 - t / 9) / 12 + S(x / 2 - t / 23) / 4);
    cc[x] = (int16_t)(220 + S(x * 2 + 50) / 26 + S(x * 7) / 50);
  }
}

static void frame_plasma(void) {
  int16_t *ca = scr, *cb = scr + W;
  int t = now * plasma_speed / 16, k = plasma_k;
  for (int x = 0; x < W; x++) ca[x] = (int16_t)(S(x * k / 8 + t / 7) + S(x * k / 13 - t / 11));
  for (int y = 0; y < H; y++) cb[y] = (int16_t)(S(y * k / 8 - t / 9) + S(y * k / 17 + t / 13));
}

static void frame_lava(void) {
  int16_t *bx = scr, *by = scr + 6;
  int t = now;
  for (int i = 0; i < 6; i++) {
    bx[i] = (int16_t)(160 + S(t / (41 + i * 7) + i * 43) * (110 - i * 9) / 256);
    by[i] = (int16_t)(120 + S(t / (53 + i * 11) + i * 97 + 64) * (100 - i * 6) / 256);
  }
}

static void frame_ocean(void) {
  static const uint8_t base[6] = {121, 130, 142, 158, 180, 209}, amp[6] = {2, 3, 4, 6, 9, 13};
  int t = now;
  for (int k = 0; k < 6; k++)
    for (int x = 0; x < W; x++)
      scr[k * W + x] = (int16_t)(base[k] + (S(x * (8 - k) / 4 + t / (70 + k * 25) + k * 50) * 2 +
                                            S(x * (11 - k) / 3 - t / (110 + k * 30) + k * 90)) * amp[k] / 768);
}

/* one of NumVisuals' 33 ms steps */
static void step_warp(int dt) {
  warp_t *w = (warp_t *)scr;
  for (int i = 0; i < NS; i++) {
    w->qx[i] = w->px[i], w->qy[i] = w->py[i];
    w->z[i] = (int16_t)(w->z[i] - dt / 2 - 1);
    if (w->z[i] < 20) {
      star(w, i, 1024);
      continue;
    }
    w->px[i] = (int16_t)(160 + w->x[i] * 64 / w->z[i]);
    w->py[i] = (int16_t)(120 + w->y[i] * 64 / w->z[i]);
    if (w->px[i] < -40 || w->px[i] > W + 40 || w->py[i] < -40 || w->py[i] > H + 40) star(w, i, 1024);
  }
}

static void step_fire(int dt) {
  fire_t *f = (fire_t *)scr;
  for (f->acc = clamp(f->acc + dt, 0, 120); f->acc >= 30; f->acc -= 30) {
    for (int x = 0; x < FW; x++) f->v[FH][x] = (uint8_t)(150 + rnd() % 106);
    for (int y = 0; y < FH; y++)
      for (int x = 0; x < FW; x++) {
        uint32_t r = rnd();
        int v = f->v[y + 1][clamp(x + (int)(r & 3) - 1, 0, FW - 1)] - (int)((r >> 2) % 11);
        f->v[y][x] = (uint8_t)(v < 0 ? 0 : v);
      }
  }
}

static void step_bokeh(int dt) {
  bokeh_t *b = (bokeh_t *)scr;
  for (int i = 0; i < NB; i++) {
    b->y[i] = (int16_t)(b->y[i] - b->v[i] * dt / 64);
    if (b->y[i] / 16 + b->r[i] < 0) bubble(b, i, H);
  }
}

static void step_rain(int dt) {
  rain_t *r = (rain_t *)scr;
  for (int c = 0; c < MC; c++) {
    r->h[c] += r->spd[c] * dt;
    if ((r->h[c] >> 8) - r->len[c] > MR) drop(r, c);
  }
  for (int i = 0; i < 6; i++) r->g[rnd() % MR][rnd() % MC] = (uint8_t)(33 + rnd() % 94);
}

void live_frame(uint32_t now_ms) {
  if (!live_active()) return;
  now = (int)now_ms;
  int dt = clamp(now - last, 0, 100);
  last = now;
  switch (mode) {
    case AURORA: frame_aurora(); break;
    case PLASMA:
    case PASTEL: frame_plasma(); break;
    case LAVA: frame_lava(); break;
    case OCEAN: frame_ocean(); break;
    default: break;
  }
  for (acc += dt; acc >= 33; acc -= 33) {
    if (mode == WARP) step_warp(33);
    if (mode == FIRE) step_fire(33);
    if (mode == BOKEH) step_bokeh(33);
    if (mode == RAIN) step_rain(33);
  }
}

/* ---------------------------------------------------------------- per strip */
/* c over the pixel (x, y) when it is in the strip */
static void plot(int x, int y, color_t c, int a) {
  if ((unsigned)x < W && y >= gfx_y0 && y < gfx_y1) {
    color_t *p = gfx_buf + (y - gfx_y0) * W + x;
    *p = mixf(c, *p, a);
  }
}

/* pal from top to bottom of the screen */
static void gradient(int top, int sh) {
  for (int j = 0; j < sh; j++) {
    color_t c = pal[(top + j) * 255 / H], *p = gfx_buf + j * W;
    for (int x = 0; x < W; x++) p[x] = c;
  }
}

static void strip_aurora(int top, int sh) {
  int16_t *ca = scr, *cb = scr + W, *cc = scr + 2 * W;
  for (int j = 0; j < sh; j++) {
    int y = top + j, sky = 8 + y * 52 / H;
    color_t *p = gfx_buf + j * W;
    for (int x = 0; x < W; x++) {
      if (y >= cc[x]) {
        p[x] = RGB1(0x010308);
        continue;
      }
      int d = ca[x] - y, i = d >= 0 ? cb[x] - d * 3 / 2 : cb[x] * 2 / 3 + d * 3;
      if (i < sky) {
        uint32_t h = (uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u;
        i = (h * 0x5BD1E995u) >> 22 ? sky : 190;
      }
      p[x] = pal[i > 255 ? 255 : i];
    }
  }
}

static void strip_sunset(int top, int sh) {
  int t = now;
  for (int j = 0; j < sh; j++) {
    int y = top + j;
    color_t *p = gfx_buf + j * W;
    if (y < 150) {
      color_t sky = pal[y * 255 / 150];
      int dy = y - 102, r2 = 3600 - dy * dy;
      bool cut = dy > 4 && dy % 11 < (dy - 4) / 9 + 1;
      color_t sun = mixf(RGB1(0xFF3F81), RGB1(0xFFE45E), clamp((y - 42) * 32 / 108, 0, 32));
      for (int x = 0; x < W; x++) {
        int dx = x - 160, d2 = dx * dx + dy * dy;
        p[x] = dx * dx < r2 && !cut ? sun : d2 < 8100 ? mixf(RGB1(0xFF4F9A), sky, (8100 - d2) / 700) : sky;
      }
    } else {
      int d = y - 146, z0 = 98304 / d, z1 = 98304 / (d + 1), off = t / 2, a = clamp(4 + d / 3, 0, 28);
      bool row = ((z0 + off) >> 8) != ((z1 + off) >> 8) || d == 4;
      color_t base = mixf(RGB1(0x0A0018), RGB1(0x2A0A4A), d * 32 / 94), line = mixf(RGB1(0xFF3DDB), base, d == 4 ? 32 : a);
      int q = (-160 * z0) >> 15;
      for (int x = 0; x < W; x++) {
        int nq = ((x - 159) * z0) >> 15;
        p[x] = row || nq != q ? line : base;
        q = nq;
      }
    }
  }
}

static void strip_plasma(int top, int sh) {
  int16_t *ca = scr, *cb = scr + W;
  int t = now * plasma_speed / 16, k = plasma_k;
  for (int j = 0; j < sh; j++) {
    int y = top + j, r = cb[y];
    color_t *p = gfx_buf + j * W;
    for (int x = 0; x < W; x++) p[x] = pal[(uint8_t)(((ca[x] + r + S((x + y) * k / 16 + t / 5)) >> 3) + (t >> 4))];
  }
}

static void strip_lava(int top, int sh) {
  int16_t *bx = scr, *by = scr + 6;
  for (int j = 0; j < sh; j++) {
    int y = top + j, dy2[6];
    color_t *p = gfx_buf + j * W;
    for (int i = 0; i < 6; i++) dy2[i] = (y - by[i]) * (y - by[i]) + 16;
    for (int x = 0; x < W; x += 2) {
      int f = 0;
      for (int i = 0; i < 6; i++) {
        int dx = x - bx[i], r = 26 + i * 3;
        f += (r * r << 7) / (dx * dx + dy2[i]);
      }
      p[x] = p[x + 1] = pal[f < 128 ? f * 5 / 8 : 150 + (f - 128 > 315 ? 105 : (f - 128) / 3)];
    }
  }
}

static void strip_warp(int top, int sh) {
  const warp_t *w = (const warp_t *)scr;
  gradient(top, sh);
  for (int i = 0; i < NS; i++) {
    int x0 = w->qx[i], y0 = w->qy[i], x1 = w->px[i], y1 = w->py[i];
    if ((y0 < top && y1 < top) || (y0 >= top + sh && y1 >= top + sh)) continue;
    int a = clamp(40 - w->z[i] / 26, 10, 32), n = iabs(x1 - x0) > iabs(y1 - y0) ? iabs(x1 - x0) : iabs(y1 - y0);
    n = clamp(n, 1, 40);
    for (int s = 0; s <= n; s++) plot(x0 + (x1 - x0) * s / n, y0 + (y1 - y0) * s / n, RGB1(0xDDE8FF), a * (s + 1) / (n + 1));
    if (w->z[i] < 420) plot(x1 + 1, y1, 0xFFFF, a), plot(x1, y1 + 1, 0xFFFF, a), plot(x1 + 1, y1 + 1, 0xFFFF, a);
  }
}

static void strip_tunnel(int top, int sh) {
  const tunnel_t *tb = (const tunnel_t *)scr;
  int t = now, spin = t / 24, move = t / 6;
  for (int j = 0; j < sh; j++) {
    int hy = (top + j) / 2 - 60, qy = hy < 0 ? -hy - 1 : hy;
    color_t *p = gfx_buf + j * W;
    for (int x = 0; x < W; x += 2) {
      int hx = x / 2 - 80, qx = hx < 0 ? -hx - 1 : hx, a = tb->a[qy][qx], d = tb->d[qy][qx];
      int u = hx >= 0 ? (hy >= 0 ? a : 256 - a) : (hy >= 0 ? 128 - a : 128 + a);
      color_t c = pal[(uint8_t)(d * 2 + move)];
      if ((((u * 2 + spin) >> 4) ^ ((d + move) >> 4)) & 1) c = mixf(c, 0, 20);
      p[x] = p[x + 1] = mixf(c, 0, 32 - (d >> 3));
    }
  }
}

static void strip_fire(int top, int sh) {
  const fire_t *f = (const fire_t *)scr;
  for (int j = 0; j < sh; j++) {
    int y = top + j, wy = y & 3;
    const uint8_t *r0 = f->v[y >> 2], *r1 = f->v[(y >> 2) + 1];
    color_t *p = gfx_buf + j * W;
    for (int x = 0; x < W; x++) {
      int fx = x >> 2, fx1 = fx + 1 < FW ? fx + 1 : fx, wx = x & 3;
      p[x] = pal[((r0[fx] * (4 - wx) + r0[fx1] * wx) * (4 - wy) + (r1[fx] * (4 - wx) + r1[fx1] * wx) * wy) >> 4];
    }
  }
}

static void strip_ocean(int top, int sh) {
  static const uint32_t sea_rgb[6] = {0x7FA9CF, 0x5A8CBE, 0x3F72A8, 0x2B5B91, 0x1C4677, 0x11335E};
  color_t sea[6];
  for (int i = 0; i < 6; i++) sea[i] = RGB1(sea_rgb[i]);
  int t = now;
  for (int j = 0; j < sh; j++) {
    int y = top + j;
    color_t *p = gfx_buf + j * W;
    for (int x = 0; x < W; x++) {
      int k = 5;
      while (k >= 0 && y < scr[k * W + x]) k--;
      color_t c;
      if (k < 0) {
        int dx = x - 236, dy = y - 92, d2 = dx * dx + dy * dy;
        c = d2 < 225 ? RGB1(0xFFF1C9) : pal[clamp(y * 2, 0, 255)];
        if (d2 >= 225 && d2 < 2500) c = mixf(RGB1(0xFFD9A0), c, (2500 - d2) / 160);
      } else {
        int e = y - scr[k * W + x];
        c = e < 2 ? mixf(0xFFFF, sea[k], 12 - e * 5) : sea[k];
        if (iabs(x - 236) < 4 + (y - 120) / 4 && S(y * 29 + x * 3 + t / 6) > 170) c = mixf(RGB1(0xFFE2B0), c, 14);
      }
      p[x] = c;
    }
  }
}

static void strip_bokeh(int top, int sh) {
  static const color_t rgb[5] = {RGB1(0xFFB84D), RGB1(0xFF6B9A), RGB1(0x9B7BFF), RGB1(0x5CE1E6), RGB1(0xFFE08A)};
  const bokeh_t *b = (const bokeh_t *)scr;
  gradient(top, sh);
  for (int i = 0; i < NB; i++) {
    int r = b->r[i], cx = b->x[i] + S(now / 30 + i * 37) * 10 / 256, cy = b->y[i] / 16;
    int y0 = clamp(cy - r, top, top + sh), y1 = clamp(cy + r + 1, top, top + sh);
    for (int y = y0; y < y1; y++)
      for (int x = clamp(cx - r, 0, W), x1 = clamp(cx + r + 1, 0, W); x < x1; x++) {
        int e = r * r - (x - cx) * (x - cx) - (y - cy) * (y - cy), a = b->a[i];
        if (e <= 0) continue;
        if (e < 2 * r) a = a * e / (2 * r);
        else if (e < 5 * r) a += 3; /* a brighter rim, like a lens */
        color_t *q = gfx_buf + (y - top) * W + x;
        *q = mixf(rgb[b->c[i]], *q, a);
      }
  }
}

/* NumVisuals' letters from '!' to '~', columns of 7 bits, the top row in bit 0 */
static const uint8_t font[94][5] = {
    {0x00,0x00,0x5F,0x00,0x00},{0x00,0x07,0x00,0x07,0x00},{0x14,0x7F,0x14,0x7F,0x14},{0x24,0x2A,0x7F,0x2A,0x12},
    {0x23,0x13,0x08,0x64,0x62},{0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},{0x00,0x1C,0x22,0x41,0x00},
    {0x00,0x41,0x22,0x1C,0x00},{0x14,0x08,0x3E,0x08,0x14},{0x08,0x08,0x3E,0x08,0x08},{0x00,0x50,0x30,0x00,0x00},
    {0x08,0x08,0x08,0x08,0x08},{0x00,0x60,0x60,0x00,0x00},{0x20,0x10,0x08,0x04,0x02},{0x3E,0x41,0x41,0x41,0x3E},
    {0x00,0x42,0x7F,0x40,0x00},{0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},{0x18,0x14,0x12,0x7F,0x10},
    {0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},{0x36,0x49,0x49,0x49,0x36},
    {0x06,0x49,0x49,0x29,0x1E},{0x00,0x36,0x36,0x00,0x00},{0x00,0x56,0x36,0x00,0x00},{0x08,0x14,0x22,0x41,0x00},
    {0x14,0x14,0x14,0x14,0x14},{0x00,0x41,0x22,0x14,0x08},{0x02,0x01,0x51,0x09,0x06},{0x32,0x49,0x79,0x41,0x3E},
    {0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},{0x7F,0x41,0x41,0x22,0x1C},
    {0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},{0x3E,0x41,0x49,0x49,0x7A},{0x7F,0x08,0x08,0x08,0x7F},
    {0x00,0x41,0x7F,0x41,0x00},{0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},{0x7F,0x40,0x40,0x40,0x40},
    {0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},{0x7F,0x09,0x09,0x09,0x06},
    {0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7F,0x01,0x01},
    {0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},{0x63,0x14,0x08,0x14,0x63},
    {0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43},{0x00,0x7F,0x41,0x41,0x00},{0x02,0x04,0x08,0x10,0x20},
    {0x00,0x41,0x41,0x7F,0x00},{0x04,0x02,0x01,0x02,0x04},{0x40,0x40,0x40,0x40,0x40},{0x00,0x01,0x02,0x04,0x00},
    {0x20,0x54,0x54,0x54,0x78},{0x7F,0x48,0x44,0x44,0x38},{0x38,0x44,0x44,0x44,0x20},{0x38,0x44,0x44,0x48,0x7F},
    {0x38,0x54,0x54,0x54,0x18},{0x08,0x7E,0x09,0x01,0x02},{0x0C,0x52,0x52,0x52,0x3E},{0x7F,0x08,0x04,0x04,0x78},
    {0x00,0x44,0x7D,0x40,0x00},{0x20,0x40,0x44,0x3D,0x00},{0x7F,0x10,0x28,0x44,0x00},{0x00,0x41,0x7F,0x40,0x00},
    {0x7C,0x04,0x18,0x04,0x78},{0x7C,0x08,0x04,0x04,0x78},{0x38,0x44,0x44,0x44,0x38},{0x7C,0x14,0x14,0x14,0x08},
    {0x08,0x14,0x14,0x18,0x7C},{0x7C,0x08,0x04,0x04,0x08},{0x48,0x54,0x54,0x54,0x20},{0x04,0x3F,0x44,0x40,0x20},
    {0x3C,0x40,0x40,0x20,0x7C},{0x1C,0x20,0x40,0x20,0x1C},{0x3C,0x40,0x30,0x40,0x3C},{0x44,0x28,0x10,0x28,0x44},
    {0x0C,0x50,0x50,0x50,0x3C},{0x44,0x64,0x54,0x4C,0x44},{0x00,0x08,0x36,0x41,0x00},{0x00,0x00,0x7F,0x00,0x00},
    {0x00,0x41,0x36,0x08,0x00},{0x08,0x04,0x08,0x10,0x08},
};

static void strip_rain(int top, int sh) {
  const rain_t *r = (const rain_t *)scr;
  memset(gfx_buf, 0, (size_t)sh * W * sizeof(color_t));
  for (int row = top / 10; row <= (top + sh - 1) / 10 && row < MR; row++)
    for (int c = 0; c < MC; c++) {
      int d = (r->h[c] >> 8) - row;
      if (d < 0 || d >= r->len[c]) continue;
      color_t col = d ? mixf(RGB1(0x2BFF6A), 0, 32 - d * 28 / r->len[c]) : RGB1(0xD8FFE0);
      const uint8_t *g = font[r->g[row][c] - 33];
      for (int i = 0; i < 5; i++)
        for (int j = 0, b = g[i]; b; j++, b >>= 1)
          if (b & 1) plot(c * 8 + 1 + i, row * 10 + 1 + j, col, 32);
    }
}

void live_strip(void) {
  if (!live_active()) return;
  int top = gfx_y0, sh = gfx_y1 - gfx_y0;
  switch (mode) {
    case AURORA: strip_aurora(top, sh); break;
    case SUNSET: strip_sunset(top, sh); break;
    case PLASMA:
    case PASTEL: strip_plasma(top, sh); break;
    case LAVA: strip_lava(top, sh); break;
    case WARP: strip_warp(top, sh); break;
    case TUNNEL: strip_tunnel(top, sh); break;
    case FIRE: strip_fire(top, sh); break;
    case OCEAN: strip_ocean(top, sh); break;
    case BOKEH: strip_bokeh(top, sh); break;
    case RAIN: strip_rain(top, sh); break;
    default: break;
  }
  /* three quarters of the brightness, so that the white text and the cards stay readable */
  color_t *p = gfx_buf, *e = gfx_buf + sh * W;
  for (; p < e; p++) *p = (color_t)(((*p & 0xF7DE) >> 1) + ((*p & 0xE79C) >> 2));
}
