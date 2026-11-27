/* NumVisuals' backgrounds. Each works its colours out in 8 bits a channel and dithers them to the screen (vis.h);
 * what moves is worked out once a frame (frame), and each strip of the screen is then drawn from it (draw).
 *
 * The soft ones (Plasma, Pastel, Lava Lamp, Fire, Ocean, and what's behind Warp and Bokeh) are painted at half the
 * screen's resolution on a canvas, then drawn twice as big, filtered and dithered at full resolution: their light is
 * smooth, so nothing shows of it but the speed. The sharp ones (Aurora, Sunset Drive, Tunnel, Code Rain) are drawn at
 * full resolution, from tables made once a frame. */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("O2") /* (drawing: speed over size) */
#endif
#include "vis.h"
#include "../../common/np_text.h"

HOT int lerp8(int a, int b, int t) { return a + ((b - a) * t >> 8); }

/* ------------------------------------------------------------------ the canvas: half the screen, a pixel of margin */
#define CW 162
#define CH 122
typedef struct {
  uint8_t r[CH][CW], g[CH][CW], b[CH][CW];
} canvas_t;
/* canvas pixel (i, j) is screen pixel (2i - 2, 2j - 2): the screen's between them are their averages */
static void upsample(const canvas_t *c) {
  for (int j = 0; j < SH; j++) {
    int y = top + j, ly = (y >> 1) + 1, odd = y & 1;
    const uint8_t *r0 = c->r[ly], *g0 = c->g[ly], *b0 = c->b[ly];
    const uint8_t *r1 = c->r[ly + odd], *g1 = c->g[ly + odd], *b1 = c->b[ly + odd];
    color *p = buf + j * W;
    const uint8_t *bt = bayer[y & 3];
    int r = r0[1] + r1[1], g = g0[1] + g1[1], b = b0[1] + b1[1];
    for (int x = 0, lx = 1; x < W; x += 2, lx++) {
      int rn = r0[lx + 1] + r1[lx + 1], gn = g0[lx + 1] + g1[lx + 1], bn = b0[lx + 1] + b1[lx + 1];
      p[x] = dith(r >> 1, g >> 1, b >> 1, bt[x & 3]);
      p[x + 1] = dith((r + rn) >> 2, (g + gn) >> 2, (b + bn) >> 2, bt[(x + 1) & 3]);
      r = rn, g = gn, b = bn;
    }
  }
}
/* a quarter-resolution picture (82 x 62, a pixel of margin) onto the canvas, twice as big */
static void expand(uint8_t (*q)[82], uint8_t (*o)[CW]) {
  for (int j = 0; j < CH; j++) {
    int qj = ((j - 1) >> 1) + 1, oj = (j - 1) & 1;
    if (qj < 0) qj = 0;
    if (qj > 60) qj = 60, oj = 0;
    const uint8_t *a = q[qj], *b = q[qj + oj];
    for (int i = 0; i < CW; i++) {
      int qi = ((i - 1) >> 1) + 1, oi = (i - 1) & 1;
      if (qi < 0) qi = 0;
      if (qi > 80) qi = 80, oi = 0;
      o[j][i] = (uint8_t)((a[qi] + a[qi + oi] + b[qi] + b[qi + oi] + 2) >> 2);
    }
  }
}

/* what the backgrounds keep: one at a time */
#define NSTAR 150
typedef struct {
  int16_t x, y;
  uint8_t b, ph;
} star_t;
static union {
  struct { /* Aurora */
    int16_t yb[2][W], in[2][W], hk[2][W], rf[W], rm[W];
    star_t st[NSTAR];
  } au;
  struct { /* Sunset Drive */
    int16_t m1[W], m2[W]; /* the ridges, 1/16 pixels */
    int32_t dx2[W];
    uint8_t sunx[W], neon[1024];
    star_t st[NSTAR];
  } sd;
  struct { /* Plasma */
    canvas_t cv;
  } pl;
  struct { /* Pastel */
    canvas_t cv;
    uint8_t r[62][82], g[62][82], b[62][82];
  } pa;
  struct { /* Lava Lamp */
    canvas_t cv;
    uint8_t q[62][82], ql[62][82], f[CH][CW];
  } lv;
  struct { /* Warp */
    canvas_t cv;
    int32_t x[220], y[220], z[220];
    int16_t px[220], py[220];
    uint8_t c[220];
    uint8_t neb[62][82];
    int32_t speed;
  } wp;
  struct { /* Tunnel */
    uint8_t a[120][160], d[120][160], tex[128][128];
    uint8_t fog[256], side[128], vrow[256];
    uint8_t ar[256], ag[256], ab[256]; /* the light added at each depth: the rings and the far end */
  } tu;
  struct { /* Fire */
    canvas_t cv;
    uint8_t f[62][82], v[CH][CW];
    int16_t ex[48], ey[48], evx[48], evy[48];
    uint8_t el[48];
    int acc;
  } fi;
  struct { /* Ocean */
    canvas_t cv;
    uint8_t cl[62][CW];
  } oc;
  struct { /* Bokeh */
    canvas_t cv;
    int32_t x[52], y[52];
    uint8_t r[52], c[52], l[52], ph[52];
  } bk;
  struct { /* Code Rain */
    uint8_t mask[40][8 * 12], blur[40][12 * 16];
    uint8_t g[2][24][48];
    int32_t head[2][48];
    uint8_t spd[2][48], len[2][48];
    int acc;
  } cr;
} M;
static ramp_t P0, P1, P2;

static void stars_init(star_t *st, int n, int ymax) {
  for (int i = 0; i < n; i++) {
    st[i].x = (int16_t)(rnd() % W), st[i].y = (int16_t)(rnd() % (uint32_t)ymax);
    int b = (int)(rnd() % 256);
    st[i].b = (uint8_t)(30 + (b * b >> 8) * 225 / 255), st[i].ph = (uint8_t)rnd();
  }
}
/* the stars in this strip, twinkling, where the sky (above lim, 1/16 pixels per column, or 0) shows */
static void stars_draw(const star_t *st, int n, const int16_t *lim, int k) {
  int t = (int)now;
  for (int i = 0; i < n; i++) {
    int y = st[i].y, x = st[i].x;
    if (y < top - 2 || y >= top + SH + 2 || (lim && y * 16 >= lim[x] - 16)) continue;
    int tw = 200 + (isin(t / 3 + st[i].ph * 9 + (st[i].ph & 7) * t / 40) >> 8); /* 136 to 264 */
    int b = st[i].b * tw >> 8;
    b = b * k >> 8;
    if (b > 180) glow_dot(x * 16 + 8, y * 16 + 8, 40, 0xBFD4FF, (b - 150) * 2);
    if (y >= top && y < top + SH) add(buf + (y - top) * W + x, b, b, b + (b >> 3) > 255 ? 255 : b + (b >> 3));
  }
}

static uint8_t halo_t[1024];
static void halo_init(int s) { /* 1 / (1 + d/s)^2, d = (squared distance past the edge) >> 6 */
  for (int i = 0; i < 1024; i++) {
    int q = 256 * s / (s + i);
    halo_t[i] = (uint8_t)(q * q >> 8 > 255 ? 255 : q * q >> 8);
  }
}
HOT int halo(int d2) { return d2 <= 0 ? 255 : halo_t[(d2 >> 6) > 1023 ? 1023 : d2 >> 6]; }

/* ================================================================== Aurora */
#define A_HZ 192
static void aurora_init(void) {
  static const uint32_t sky[] = {0x01020A, 0x030A1C, 0x061830, 0x0B2840, 0x15384A};
  static const uint32_t cur[] = {0x7CFFC0, 0x2DF0A0, 0x14C89C, 0x1A8AA8, 0x4B4BB0, 0x8A2E98, 0x5A1A60, 0x000000};
  ramp(&P0, sky, 5), ramp(&P1, cur, 8);
  for (int x = 0; x < W; x++) {
    int n = fbm1(x * 6 + 3000, 6), r = 255 - iabs(2 * n - 255); /* ridged: peaks, rough on every scale */
    M.au.rf[x] = (int16_t)(160 * 16 - (r * r >> 8) * 40 * 16 / 255);
    int m = fbm1(x * 4 + 9000, 5), q = 255 - iabs(2 * m - 255), side = (x - 160) * (x - 160) / 600;
    M.au.rm[x] = (int16_t)((188 - side) * 16 - (q * q >> 8) * 22 * 16 / 255);
    if (M.au.rm[x] > (A_HZ - 1) * 16) M.au.rm[x] = (A_HZ - 1) * 16;
  }
  stars_init(M.au.st, NSTAR, 170);
}
static void aurora_frame(int dt) {
  int t = (int)now;
  for (int l = 0; l < 2; l++)
    for (int x = 0; x < W; x++) {
      int y = 92 + l * 16 + (isin(x * 3 + t / 25 + l * 300) * 14 >> 14) + ((noise1(x * 6 + t / 9 + l * 7000) - 128) * 26 >> 8);
      int e = noise1(x * 4 - t / 13 + l * 3000), f = noise1(x * 2 + t / 31 + l * 1700);
      e = (e * e >> 8) * (96 + f) >> 8;                                 /* where it's bright */
      int r = noise1(x * 120 + t / 4 + l * 1000), r2 = noise1(x * 47 - t / 6 + l * 500); /* the rays */
      int in = e * (90 + (r * 120 >> 8) + (r2 * 60 >> 8)) >> 7;
      M.au.yb[l][x] = (int16_t)y;
      M.au.in[l][x] = (int16_t)(in * (l ? 150 : 256) >> 8);
      M.au.hk[l][x] = (int16_t)(32 * 256 / (34 + (e * 60 >> 8) + (r2 * 26 >> 8))); /* 1/height, to fall() */
    }
}
/* the aurora's light at (x, y) */
HOT void au_light(int x, int y, int *R, int *G, int *B) {
  int ar = 0, ag = 0, ab = 0;
  for (int l = 0; l < 2; l++) {
    int h = M.au.yb[l][x] - y;
    if (h < -6 || h > 230) continue;
    int k = h >= 0 ? fall(h * M.au.hk[l][x] >> 8) : fall(-h * 11);
    int a = M.au.in[l][x] * k >> 8, c = (h * 327 >> 8) + l * 30;
    c = c < 0 ? 0 : c > 255 ? 255 : c;
    ar += P1.r[c] * a >> 8, ag += P1.g[c] * a >> 8, ab += P1.b[c] * a >> 8;
  }
  *R = ar, *G = ag, *B = ab;
}
/* the sky (r, g, b), the aurora's light and the mountains at (x, y) */
HOT color au_px(int x, int y, int r, int g, int b, int ar, int ag, int ab, int t) {
  int y16 = y * 16 + 8;
  if (y16 >= M.au.rm[x]) { /* near mountains: almost black, lit at their edge */
    int e = (y16 - M.au.rm[x]) >> 4, rim = e < 3 ? 3 - e : 0, a = (ar + ag + ab) >> 4;
    return dith(3 + rim * 6 + (a >> 2), 7 + rim * 14 + (a >> 1), 12 + rim * 12 + (a >> 1), t);
  }
  if (y16 >= M.au.rf[x]) { /* far mountains: the sky's haze, and the aurora's light on their snow */
    int d = (y16 - M.au.rf[x]) >> 4, snow = d < 10 ? (10 - d) * 3 : 0;
    return dith((r * 90 >> 8) + 8 + snow + (ar >> 3), (g * 90 >> 8) + 18 + snow + (ag >> 2), (b * 90 >> 8) + 30 + snow + (ab >> 2), t);
  }
  return dith(r + ar, g + ag, b + ab, t);
}
static void aurora(void) {
  /* (the aurora's light changes slowly down the screen: worked out on even rows, kept for the odd ones, a column
     pair at a time) */
  static uint8_t lr[W / 2], lg[W / 2], lb[W / 2];
  int t = (int)now, ar, ag, ab;
  for (int j = 0; j < SH; j++) {
    int y = top + j;
    color *p = buf + j * W;
    const uint8_t *bt = bayer[y & 3];
    if (y < A_HZ) {
      int si = y * 255 / A_HZ, r = P0.r[si], g = P0.g[si], b = P0.b[si];
      for (int x = 0; x < W; x += 2) {
        if (!(y & 1) || j == 0) {
          au_light(x, y, &ar, &ag, &ab);
          ar = ar > 255 ? 255 : ar, ag = ag > 255 ? 255 : ag, ab = ab > 255 ? 255 : ab;
          lr[x >> 1] = (uint8_t)ar, lg[x >> 1] = (uint8_t)ag, lb[x >> 1] = (uint8_t)ab;
        } else
          ar = lr[x >> 1], ag = lg[x >> 1], ab = lb[x >> 1];
        p[x] = au_px(x, y, r, g, b, ar, ag, ab, bt[x & 3]);
        p[x + 1] = au_px(x + 1, y, r, g, b, ar, ag, ab, bt[(x + 1) & 3]);
      }
    } else { /* the lake: the scene upside down, rippled and darker */
      int d = y - A_HZ, sy = A_HZ - 1 - d * 9 / 10, amp = 1 + d / 6, si = sy * 255 / A_HZ;
      int sh = 150 + (isin(y * 61 + t / 5) >> 9) + (isin(y * 23 - t / 7) >> 10) - (d == 0 ? 70 : 0);
      int r = P0.r[si] * sh >> 8, g = P0.g[si] * sh >> 8, b = P0.b[si] * sh >> 8;
      for (int x = 0; x < W; x += 2) {
        int sx = clampi(x + (isin(y * 40 + x * 3 - t / 3) * amp >> 14), 0, W - 2) & ~1;
        if (!(y & 1) || j == 0) {
          au_light(sx, sy, &ar, &ag, &ab);
          lr[x >> 1] = (uint8_t)(ar > 255 ? 255 : ar), lg[x >> 1] = (uint8_t)(ag > 255 ? 255 : ag), lb[x >> 1] = (uint8_t)(ab > 255 ? 255 : ab);
        }
        ar = lr[x >> 1] * sh >> 8, ag = lg[x >> 1] * sh >> 8, ab = lb[x >> 1] * sh >> 8;
        for (int k = 0; k < 2; k++) {
          int y16 = sy * 16 + 8, xx = sx + k;
          if (y16 >= M.au.rm[xx]) p[x + k] = dith(1 + (ar >> 3), 4 + (ag >> 3), 9 + (ab >> 3), bt[(x + k) & 3]);
          else if (y16 >= M.au.rf[xx]) p[x + k] = dith((r * 90 >> 8) + 5 + (ar >> 3), (g * 90 >> 8) + 12 + (ag >> 2), (b * 90 >> 8) + 22 + (ab >> 2), bt[(x + k) & 3]);
          else p[x + k] = dith(r + ar + 1, g + ag + 4, b + ab + 9, bt[(x + k) & 3]);
        }
      }
    }
  }
  stars_draw(M.au.st, NSTAR, M.au.rf, 256);
}

/* ================================================================== Sunset Drive */
#define S_HZ 150
#define S_SX 160
#define S_SY 100
#define S_R 58
static void synth_init(void) {
  static const uint32_t sky[] = {0x05011A, 0x150530, 0x3A0B57, 0x7A1468, 0xC8296C, 0xFF6A45, 0xFFA45A};
  static const uint32_t sun[] = {0xFFF6A0, 0xFFD45A, 0xFF9A4A, 0xFF5577, 0xF0287E};
  static const uint32_t gnd[] = {0x2A0848, 0x16052C, 0x0A021A, 0x05010D};
  ramp(&P0, sky, 7), ramp(&P1, sun, 5), ramp(&P2, gnd, 4);
  halo_init(90);
  for (int x = 0; x < W; x++) {
    int s = iabs(x - 160), lift = s * s / 520;
    int n = fbm1(x * 5 + 600, 2), r = 255 - iabs(2 * n - 255);
    M.sd.m1[x] = (int16_t)((S_HZ - 6 - lift * 2 / 3) * 16 - (r * r >> 8) * (8 + s / 5) * 16 / 255);
    n = fbm1(x * 4 + 2600, 2), r = 255 - iabs(2 * n - 255);
    M.sd.m2[x] = (int16_t)((S_HZ - 2 - lift) * 16 - (r * r >> 8) * (4 + s / 4) * 16 / 255);
    M.sd.dx2[x] = (x - S_SX) * (x - S_SX);
    M.sd.sunx[x] = (uint8_t)halo(M.sd.dx2[x] * 9);
  }
  for (int d = 0; d < 1024; d++) /* a neon line's light at d sixteenths of a pixel */
    M.sd.neon[d] = (uint8_t)(d < 10 ? 255 : 220 * fall((d - 10) * 32 / 40) >> 8);
  stars_init(M.sd.st, 90, 90);
}
/* a ridge's light d sixteenths of a pixel from its edge. Past 1023 (the near ridge high at the screen's sides) that's
 * the stars' bytes after neon[]: on a big-endian CPU, their int16s' bytes as a little-endian one has them, as the
 * NumWorks draws */
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define RIDGE_NEON(d) neon[(d) < 1024 || ((d) - 1024) % (int)sizeof(star_t) >= 4 ? (d) : (d) ^ 1]
#else
#define RIDGE_NEON(d) neon[d]
#endif
static void synth(void) {
  int t = (int)now, phase = t / 60 % 160; /* the sun's stripes, 1/16 pixels */
  const uint8_t *neon = M.sd.neon;
  for (int j = 0; j < SH; j++) {
    int y = top + j;
    color *p = buf + j * W;
    const uint8_t *bt = bayer[y & 3];
    if (y < S_HZ) {
      int i = y * 255 / S_HZ, hz = fall(iabs(y - S_HZ) * 32 / 7) * 200 >> 8; /* the sky, the horizon's glow */
      int sr = P0.r[i] + (250 * hz >> 8), sg = P0.g[i] + (50 * hz >> 8), sb = P0.b[i] + (170 * hz >> 8);
      int dy = y - S_SY, dy2 = dy * dy, si = clampi((y - (S_SY - S_R)) * 255 / (2 * S_R), 0, 255);
      bool sunrow = dy > -S_R - 1 && dy < S_R + 1;
      int gap = 0;
      if (dy > -8) { /* the row spans [pos, pos + 16) of the stripes' period, the gap its last w16 */
        int per = 160, w16 = 10 + (dy + 8) * 70 / 66, pos = ((y * 16 - phase) % per + per) % per;
        int a0 = pos > per - w16 ? pos : per - w16, a1 = pos + 16 < per ? pos + 16 : per;
        gap = a1 > a0 ? (a1 - a0) * 16 : 0;
      }
      int ur = P1.r[si] + 30, ug = P1.g[si], ub = P1.b[si], y16 = y * 16 + 8;
      for (int x = 0; x < W; x++) {
        int d2 = M.sd.dx2[x] + dy2, k = halo(d2 - S_R * S_R);
        int r = sr + (255 * k >> 8), g = sg + (70 * k >> 8), b = sb + (110 * k >> 8);
        if (sunrow && d2 < S_R * S_R + 2 * S_R) {
          int cov = clampi(((S_R * S_R - d2) * 256 / (2 * S_R)) + 128, 0, 256) * (256 - gap) >> 8;
          r = lerp8(r, ur, cov), g = lerp8(g, ug, cov), b = lerp8(b, ub, cov);
        }
        if (y16 >= M.sd.m1[x] - 32) { /* the far ridge: a dark face, a neon edge */
          int e = y16 - M.sd.m1[x], f = clampi(e + 8, 0, 16) * 16, n = RIDGE_NEON(iabs(e)) * 150 >> 8;
          r = lerp8(r, 26, f) + (255 * n >> 8), g = lerp8(g, 6, f) + (60 * n >> 8), b = lerp8(b, 48, f) + (220 * n >> 8);
        }
        if (y16 >= M.sd.m2[x] - 32) { /* the near ridge */
          int e = y16 - M.sd.m2[x], f = clampi(e + 8, 0, 16) * 16, n = RIDGE_NEON(iabs(e));
          r = lerp8(r, 16, f) + (255 * n >> 8), g = lerp8(g, 3, f) + (40 * n >> 8), b = lerp8(b, 32, f) + (200 * n >> 8);
        }
        p[x] = dith(r, g, b, bt[x & 3]);
      }
    } else { /* the grid, running towards you */
      int dy = y - S_HZ;
      int32_t z = (480 << 8) / dy;                       /* the row's distance, 1/256 */
      int32_t dz = (480 << 8) / (dy * dy) + 1;            /* its change a row */
      int32_t wz = z + (int32_t)(t * 3 / 4), fz = wz & 1023, dw = fz < 512 ? fz : 1024 - fz; /* lines every 4 */
      int hd = dw * 16 / dz, fog = clampi(dy * 256 / 70, 0, 256), gi = clampi(dy * 255 / 90, 0, 255);
      int hk = (hd > 1023 ? 0 : neon[hd]) * clampi((dy - 2) * 16, 0, 256) >> 8;
      int hz = fall(dy * 32 / 6) * 200 >> 8, sfog = (256 - fog / 2) >> 1;
      int br = P2.r[gi] + (250 * hz >> 8), bg = P2.g[gi] + (50 * hz >> 8), bb = P2.b[gi] + (170 * hz >> 8);
      int32_t step = z * 16 / 160;                         /* world a pixel across, 1/4096 */
      int32_t inv = (int32_t)((16 * 4096LL) / (step > 0 ? step : 1)); /* pixels a world unit, 1/16 */
      int32_t wx = -160 * step + 2048 * 4;               /* (lines every 4 units across) */
      for (int x = 0; x < W; x++, wx += step) {
        int fx = (int)(wx & 16383), dx = fx < 8192 ? fx : 16384 - fx, vd = (int)((int64_t)dx * inv >> 12);
        int vk = (vd > 1023 ? 0 : neon[vd]) * fog >> 8, k = (vk > hk ? vk : hk) * fog >> 8, sun = M.sd.sunx[x] * sfog >> 8;
        p[x] = dith(br + ((255 * k + 255 * sun) >> 8), bg + ((40 * k + 70 * sun) >> 8), bb + ((210 * k + 120 * sun) >> 8), bt[x & 3]);
      }
    }
  }
  stars_draw(M.sd.st, 90, 0, 200);
}

/* ================================================================== Plasma: a liquid, lit */
static void plasma_init(void) {
  static const uint32_t c[] = {0x1A0B4A, 0x4A1AA0, 0xB020C0, 0xFF3C82, 0xFF8C3A, 0xFFD45C, 0x3ED6B8, 0x1F6FC0, 0x1A0B4A};
  ramp(&P0, c, 9);
}
static void plasma_frame(int dt) {
  int t = (int)now, cx = 80 + (isin(t / 37) * 50 >> 14), cy = 60 + (icos(t / 53) * 40 >> 14), drift = t >> 6;
  /* waves, warped, and rings from a wandering point; lit from the top left by its exact slope (each wave's cosine) */
  for (int y = 0; y < CH; y++) {
    int wxo = isin(y * 9 + t / 13) * 7 >> 14;
    for (int x = 0; x < CW; x++) {
      int wx = x + wxo, wy = y + (isin(x * 7 - t / 17) * 7 >> 14);
      int dx = wx - cx, dy = wy - cy, d = isqrt(dx * dx + dy * dy);
      int p1 = wx * 15 + t / 9, p2 = wy * 19 - t / 11, p3 = (wx + wy) * 11 + t / 15, p4 = d * 18 - t / 6;
      int h = isin(p1) + isin(p2) + isin(p3) + isin(p4);
      int c4 = d ? icos(p4) * 18 / d : 0, c3 = icos(p3) * 11;
      int lt = clampi(128 + ((icos(p1) * 15 + icos(p2) * 19 + c3 * 2 + c4 * (dx + dy)) >> 14), 0, 255);
      int ci = ((h >> 9) + drift) & 255, dif = 96 + lt * 3 / 4, spec = lt > 168 ? (lt - 168) * (lt - 168) >> 4 : 0;
      M.pl.cv.r[y][x] = (uint8_t)clampi((P0.r[ci] * dif >> 8) + spec, 0, 255);
      M.pl.cv.g[y][x] = (uint8_t)clampi((P0.g[ci] * dif >> 8) + spec, 0, 255);
      M.pl.cv.b[y][x] = (uint8_t)clampi((P0.b[ci] * dif >> 8) + spec, 0, 255);
    }
  }
}
static void plasma(void) { upsample(&M.pl.cv); }

/* ================================================================== Pastel: soft colours flowing into each other */
static void pastel_init(void) {}
static void pastel_frame(int dt) {
  static const uint32_t col[7] = {0xFF8FBF, 0xA98BFF, 0x6EC6FF, 0x7FEBC0, 0xFFD27A, 0xFF9E86, 0xE0A6FF};
  static const int8_t ax[7] = {6, 74, 40, 4, 76, 28, 58}, ay[7] = {8, 6, 30, 52, 54, 60, 24};
  int t = (int)now, px[7], py[7];
  for (int i = 0; i < 7; i++) { /* each colour circling its own place */
    px[i] = ax[i] + (isin(t / (53 + i * 11) + i * 140) * 16 >> 14);
    py[i] = ay[i] + (icos(t / (67 + i * 9) + i * 211) * 12 >> 14);
  }
  for (int y = 0; y < 62; y++)
    for (int x = 0; x < 82; x++) {
      int32_t sw = 0, sr = 0, sg = 0, sb = 0;
      for (int i = 0; i < 7; i++) {
        int dx = x - 1 - px[i], dy = y - 1 - py[i], d2 = dx * dx + dy * dy;
        int32_t w = (1 << 24) / (d2 * d2 / 4 + 64); /* (inverse distance, squared twice: soft, each its own) */
        sw += w, sr += w * (int32_t)(col[i] >> 16 & 255), sg += w * (int32_t)(col[i] >> 8 & 255), sb += w * (int32_t)(col[i] & 255);
      }
      M.pa.r[y][x] = (uint8_t)(sr / sw), M.pa.g[y][x] = (uint8_t)(sg / sw), M.pa.b[y][x] = (uint8_t)(sb / sw);
    }
  expand(M.pa.r, M.pa.cv.r), expand(M.pa.g, M.pa.cv.g), expand(M.pa.b, M.pa.cv.b);
}
static void pastel(void) { upsample(&M.pa.cv); }

/* ================================================================== Lava Lamp: glowing wax rising in a lit liquid */
#define NBLOB 6
static uint8_t dome_t[256]; /* the wax's height above its edge, from the field (128 on the edge) */
static void lava_init(void) {
  static const uint32_t wax[] = {0x9A1018, 0xE0381C, 0xFF6E22, 0xFFA040, 0xFFD27A};
  static const uint32_t liq[] = {0x0E0418, 0x22072E, 0x3C0B3C, 0x5A1238, 0x7A2030};
  ramp(&P0, wax, 5), ramp(&P1, liq, 5);
  for (int f = 0; f < 256; f++) dome_t[f] = (uint8_t)(f <= 128 ? 0 : clampi(255 * (f - 128) * 2 / f, 0, 255));
  halo_init(200);
}
static void lava_frame(int dt) {
  int t = (int)now, bx[NBLOB], by[NBLOB], br2[NBLOB];
  for (int i = 0; i < NBLOB; i++) { /* slow rises and falls, swaying, swelling */
    bx[i] = 80 + (isin(t / (97 + i * 23) + i * 170) * (40 + i * 3) >> 14);
    by[i] = 60 + (icos(t / (150 + i * 37) + i * 300) * 56 >> 14);
    int r = 14 + (i * 7) % 11 + (isin(t / 29 + i * 91) * 2 >> 14);
    br2[i] = r * r * 128;
  }
  for (int y = 0; y < 62; y++) /* the wax's field, smooth: at a quarter of the screen's resolution, filtered up */
    for (int x = 0; x < 82; x++) {
      int32_t f = 0;
      for (int i = 0; i < NBLOB; i++) {
        int dx = 2 * x - 3 - bx[i], dy = 2 * y - 3 - by[i];
        f += br2[i] / (dx * dx + dy * dy + 1);
      }
      M.lv.q[y][x] = (uint8_t)(f > 255 ? 255 : f);
    }
  for (int y = 0; y < 62; y++) /* the light on the wax's dome, from above left: from the smooth field */
    for (int x = 0; x < 82; x++)
      M.lv.ql[y][x] = (uint8_t)(y && x && y < 61 && x < 81 ? clampi(128 + dome_t[M.lv.q[y + 1][x + 1]] - dome_t[M.lv.q[y - 1][x - 1]], 0, 255) : 128);
  expand(M.lv.q, M.lv.f);
  for (int y = 0; y < CH; y++) /* near the wax's edge, the field worked out exactly, for a smooth outline */
    for (int x = 0; x < CW; x++) {
      int v = M.lv.f[y][x];
      if (v < 108 || v > 146) continue;
      int32_t f = 0;
      for (int i = 0; i < NBLOB; i++) {
        int dx = x - 1 - bx[i], dy = y - 1 - by[i];
        f += br2[i] / (dx * dx + dy * dy + 1);
      }
      M.lv.f[y][x] = (uint8_t)(f > 255 ? 255 : f);
    }
  for (int y = 0; y < CH; y++) {
    int Y = 2 * y - 2, li = clampi(Y * 255 / H, 0, 255), by2 = (Y - 300) * (Y - 300) - 3000;
    for (int x = 0; x < CW; x++) {
      int X = 2 * x - 2, f = M.lv.f[y][x];
      /* the liquid, the lamp's light below it, and the wax's glow through it */
      int bulb = halo((X - 160) * (X - 160) * 3 + by2), gl = f * f >> 9;
      int r = P1.r[li] + (bulb * 200 >> 8) + (gl * 220 >> 8), g = P1.g[li] + (bulb * 60 >> 8) + (gl * 50 >> 8);
      int b = P1.b[li] + (bulb * 40 >> 8) + (gl * 20 >> 8);
      int cov = clampi((f - 124) * 32, 0, 256);
      if (cov) { /* the wax, lit from above left on its dome, and from behind at its edge */
        int qy = ((y - 1) >> 1) + 1, oy = (y - 1) & 1, qx = ((x - 1) >> 1) + 1, ox = (x - 1) & 1;
        qy = clampi(qy, 0, 60), qx = clampi(qx, 0, 80);
        int lt = (M.lv.ql[qy][qx] + M.lv.ql[qy][qx + ox] + M.lv.ql[qy + oy][qx] + M.lv.ql[qy + oy][qx + ox] + 2) >> 2;
        int dm = dome_t[f], c = clampi(dm * 3 / 4 + (lt - 128) / 2 + 30, 0, 255);
        int sp = lt > 165 ? (lt - 165) * (lt - 165) >> 4 : 0, rim = dm < 24 ? 24 - dm : 0;
        int wr = P0.r[c] + sp + rim, wg = P0.g[c] + (sp * 7 >> 3) + (rim >> 2), wb = P0.b[c] + (sp >> 1);
        r = r + ((wr - r) * cov >> 8), g = g + ((wg - g) * cov >> 8), b = b + ((wb - b) * cov >> 8);
      }
      M.lv.cv.r[y][x] = (uint8_t)clampi(r, 0, 255), M.lv.cv.g[y][x] = (uint8_t)clampi(g, 0, 255), M.lv.cv.b[y][x] = (uint8_t)clampi(b, 0, 255);
    }
  }
}
static void lava(void) { upsample(&M.lv.cv); }

/* ================================================================== Warp */
#define NW 220
static const uint32_t warp_col[4] = {0xCFE0FF, 0xFFFFFF, 0x9CC2FF, 0xFFE0C0};
static void warp_star(int i, int far) {
  M.wp.x[i] = (int32_t)(rnd() % 4000) - 2000, M.wp.y[i] = (int32_t)(rnd() % 3000) - 1500;
  M.wp.z[i] = far ? 4096 : 256 + (int32_t)(rnd() % 3840);
  if (M.wp.x[i] * M.wp.x[i] + M.wp.y[i] * M.wp.y[i] < 40000) M.wp.x[i] += M.wp.x[i] < 0 ? -200 : 200;
  M.wp.c[i] = (uint8_t)(rnd() % 4);
  M.wp.px[i] = (int16_t)(160 * 16 + M.wp.x[i] * 16 * 96 / M.wp.z[i]), M.wp.py[i] = (int16_t)(120 * 16 + M.wp.y[i] * 16 * 96 / M.wp.z[i]);
}
static void warp_init(void) {
  static const uint32_t sky[] = {0x0A0F2E, 0x05081C, 0x020310, 0x000006};
  static const uint32_t neb[] = {0x000000, 0x120A30, 0x2A1458, 0x1E3A7A, 0x3A6AB0};
  ramp(&P0, sky, 4), ramp(&P1, neb, 5);
  halo_init(90);
  for (int i = 0; i < NW; i++) warp_star(i, 0);
  for (int y = 0; y < 62; y++)
    for (int x = 0; x < 82; x++) {
      int n = fbm2(x * 60, y * 60, 5), m = fbm2(x * 130 + 5000, y * 130, 3);
      M.wp.neb[y][x] = (uint8_t)clampi((n - 105) * 4 * m >> 8, 0, 255);
    }
  M.wp.speed = 0;
}
static void warp_frame(int dt) {
  int c = (int)(now % 14000); /* cruising, then the jump */
  int target = c < 8000 ? 6 : c < 11000 ? 6 + (c - 8000) * 60 / 3000 : 66 - (c - 11000) * 60 / 3000;
  M.wp.speed += (target - M.wp.speed + (target > M.wp.speed ? 1 : 0)) / 2;
  if (M.wp.speed < 4) M.wp.speed = 4;
  for (int i = 0; i < NW; i++) {
    M.wp.z[i] -= M.wp.speed * dt / 4 + 1;
    if (M.wp.z[i] < 64) {
      warp_star(i, 1);
      continue;
    }
    M.wp.px[i] = (int16_t)clampi(160 * 16 + M.wp.x[i] * 16 * 96 / M.wp.z[i], -4000, 9000);
    M.wp.py[i] = (int16_t)clampi(120 * 16 + M.wp.y[i] * 16 * 96 / M.wp.z[i], -4000, 8000);
    if (M.wp.px[i] < -400 || M.wp.px[i] > W * 16 + 400 || M.wp.py[i] < -400 || M.wp.py[i] > H * 16 + 400) warp_star(i, 1);
  }
  /* what's behind, on the canvas: deep space, the nebula turning slowly about the middle, the light ahead */
  int t = (int)now, ca = icos(t / 40), sa = isin(t / 40), glow = 40 + M.wp.speed * 2;
  for (int y = 0; y < CH; y++) {
    int dy = 2 * y - 2 - 120;
    int32_t rx = (-162 * ca - dy * sa), ry = (-162 * sa + dy * ca); /* (the turned point, 1/16384 pixels) */
    for (int x = 0; x < CW; x++, rx += 2 * ca, ry += 2 * sa) {
      int dx = 2 * x - 2 - 160, d2 = dx * dx + dy * dy, gi = d2 >> 9 > 255 ? 255 : d2 >> 9;
      int r = P0.r[gi], g = P0.g[gi], b = P0.b[gi];
      int px = rx >> 14, py = ry >> 14, u = (px >> 3) + 40, v = (py >> 3) + 30;
      if (u >= 0 && u < 80 && v >= 0 && v < 60) { /* (its picture an eighth of the screen's resolution) */
        int uu = (px & 7) << 5, vv = (py & 7) << 5;
        int n = lerp8(lerp8(M.wp.neb[v + 1][u + 1], M.wp.neb[v + 1][u + 2], uu), lerp8(M.wp.neb[v + 2][u + 1], M.wp.neb[v + 2][u + 2], uu), vv);
        r += P1.r[n] * 3 >> 2, g += P1.g[n] * 3 >> 2, b += P1.b[n] * 3 >> 2;
      }
      int k = halo(d2 * 3) * glow >> 8;
      r += k * 120 >> 8, g += k * 150 >> 8, b += k * 255 >> 8;
      M.wp.cv.r[y][x] = (uint8_t)(r > 255 ? 255 : r), M.wp.cv.g[y][x] = (uint8_t)(g > 255 ? 255 : g), M.wp.cv.b[y][x] = (uint8_t)(b > 255 ? 255 : b);
    }
  }
}
static void warp(void) {
  int sp = M.wp.speed;
  upsample(&M.wp.cv);
  for (int i = 0; i < NW; i++) {
    int z = M.wp.z[i], x1 = M.wp.px[i], y1 = M.wp.py[i];
    int zt = z + sp * 6 + 20; /* where it was a moment ago: the streak */
    int x0 = 160 * 16 + M.wp.x[i] * 16 * 96 / zt, y0 = 120 * 16 + M.wp.y[i] * 16 * 96 / zt;
    if ((y0 >> 4) > top + SH + 2 && (y1 >> 4) > top + SH + 2) continue;
    if ((y0 >> 4) < top - 2 && (y1 >> 4) < top - 2) continue;
    int k = clampi(5200 * 64 / z, 30, 256);
    glow_line(x0, y0, x1, y1, warp_col[M.wp.c[i]], k / 6, k);
    if (z < 900) glow_dot(x1, y1, 24 + (900 - z) / 30, warp_col[M.wp.c[i]], k / 2);
  }
}

/* ================================================================== Tunnel: a lit corridor, flying through it */
static void tunnel_init(void) {
  static const uint32_t c[] = {0x04050A, 0x141C30, 0x2C3A58, 0x4E6488, 0x8098BA, 0xC8D8EC};
  ramp(&P0, c, 6);
  for (int y = 0; y < 120; y++)
    for (int x = 0, r = 0; x < 160; x++) {
      int fx = x * 2 + 1, fy = y * 2 + 1; /* (half pixels from the middle) */
      int a = fx >= fy ? (fy * 128 / fx) : 256 - (fx * 128 / fy); /* the angle in a quarter turn, near enough */
      int d2 = fx * fx + fy * fy;
      if (x == 0) r = 0;
      while ((r + 1) * (r + 1) <= d2) r++; /* (the distance grows along the row) */
      M.tu.a[y][x] = (uint8_t)clampi(a, 0, 255), M.tu.d[y][x] = (uint8_t)clampi(7000 / (r + 22), 0, 255);
    }
  /* the walls: bevelled panels, lit seams between them, rivets */
  for (int v = 0; v < 128; v++)
    for (int u = 0; u < 128; u++) {
      int pu = u & 15, pv = v & 15, l = 140 + (int)(hash2(u >> 4, v >> 4) & 31) + (int)(hash2(u, v) & 7);
      if (pu == 0 || pv == 0) l = 255;                       /* a seam: light */
      else if (pu == 1 || pv == 1) l = 215;                  /* the panel's lit edges */
      else if (pu == 15 || pv == 15) l = 60;                 /* its shaded ones */
      else if ((pu == 3 || pu == 12) && (pv == 3 || pv == 12)) l = 90; /* rivets */
      M.tu.tex[v][u] = (uint8_t)l;
    }
  for (int u = 0; u < 128; u++) M.tu.side[u] = (uint8_t)(200 + (icos((u << 3) + 512) >> 8)); /* lit from above */
}
static void tunnel(void) {
  int t = (int)now, spin = t / 24, move = t / 3, ring = t / 2;
  if (top == 0) /* (once a frame) the fog, the rings and the far end's light at each depth, and each depth's row */
    for (int d = 0; d < 256; d++) {
      int fog = fall(d * 32 / 56), rv = (d * 5 + ring) & 511, rk = rv < 24 ? (24 - iabs(rv - 12) * 2) * (fog + 40) >> 4 : 0;
      int c = d > 120 ? (d - 120) * (d - 120) >> 6 : 0;
      M.tu.fog[d] = (uint8_t)(fog > 255 ? 255 : fog), M.tu.vrow[d] = (uint8_t)((d * 5 + move) & 127);
      M.tu.ar[d] = (uint8_t)clampi((rk >> 2) + (c * 140 >> 8), 0, 255), M.tu.ag[d] = (uint8_t)clampi(rk + (c * 220 >> 8), 0, 255);
      M.tu.ab[d] = (uint8_t)clampi(rk + (rk >> 2) + c, 0, 255);
    }
  for (int j = 0; j < SH; j++) {
    int y = top + j, hy = y - 120, qy = hy < 0 ? -hy - 1 : hy;
    color *p = buf + j * W;
    const uint8_t *bt = bayer[y & 3], *ar = M.tu.a[qy], *dr = M.tu.d[qy];
    /* each half of the row: the angle's quarter turn read one way or the other (1/1024 turn) */
    for (int half = 0; half < 2; half++) {
      int base = half ? (hy >= 0 ? 0 : 1024) : (hy >= 0 ? 512 : 512), sgn = half ? (hy >= 0 ? 1 : -1) : (hy >= 0 ? -1 : 1);
      for (int x = half ? 160 : 0; x < (half ? W : 160); x++) {
        int qx = half ? x - 160 : 159 - x, a = ar[qx], d = dr[qx];
        int u = (((base + sgn * a) >> 2) + spin) & 127, l = M.tu.tex[M.tu.vrow[d]][u], fog = M.tu.fog[d], r, g, b;
        if (l == 255) { /* the seams glow */
          r = 20 + (50 * fog >> 8), g = 40 + (190 * fog >> 8), b = 60 + (255 * fog >> 8);
        } else {
          int c = l * M.tu.side[u] >> 8;
          r = P0.r[c] * fog >> 8, g = P0.g[c] * fog >> 8, b = P0.b[c] * fog >> 8;
        }
        p[x] = dith(r + M.tu.ar[d], g + M.tu.ag[d], b + M.tu.ab[d], bt[x & 3]);
      }
    }
  }
}

/* ================================================================== Fire: flames rising, embers drifting up */
static void fire_init(void) {
  static const uint32_t c[] = {0x050207, 0x240406, 0x6A0C06, 0xB82206, 0xEE5410, 0xFF9A24, 0xFFCC5C, 0xFFF0B8, 0xFFFFF0};
  ramp(&P0, c, 9);
  for (int i = 0; i < 48; i++) M.fi.el[i] = 0;
  M.fi.acc = 0;
}
static void fire_frame(int dt) {
  int t = (int)now;
  /* the flames at a quarter of the screen's resolution: noise rising, warped by more noise, stretched upwards into
     tongues, strong at the bottom and thinning out going up */
  for (int y = 0; y < 62; y++) {
    int up = 61 - y; /* (cells from the bottom) */
    for (int x = 0; x < 82; x++) {
      int wx = (noise2(x * 30, y * 30 + t / 3) - 128) / 2;
      int n = fbm2(x * 64 + wx * 10, y * 24 + t * 5 / 8, 4);
      int tongue = noise1(x * 20 - t / 9); /* (where the flames reach higher) */
      int v = n * (255 - up * 4 + (tongue - 128) * up / 40) / 128 - 90;
      M.fi.f[y][x] = (uint8_t)clampi(v, 0, 255);
    }
  }
  expand(M.fi.f, M.fi.v);
  for (int y = 0; y < CH; y++) { /* coloured after filtering, so the flames' edges stay sharp; the heat's light on the air */
    int Y = 2 * y - 2, glow = Y > 90 ? (Y - 90) * (Y - 90) / 200 : 0;
    for (int x = 0; x < CW; x++) {
      int v = M.fi.v[y][x];
      M.fi.cv.r[y][x] = (uint8_t)clampi(P0.r[v] + glow, 0, 255);
      M.fi.cv.g[y][x] = (uint8_t)clampi(P0.g[v] + (glow >> 2), 0, 255);
      M.fi.cv.b[y][x] = (uint8_t)clampi(P0.b[v] + (glow >> 4), 0, 255);
    }
  }
  for (M.fi.acc = clampi(M.fi.acc + dt, 0, 100); M.fi.acc >= 25; M.fi.acc -= 25)
    for (int i = 0; i < 48; i++) { /* embers */
      if (!M.fi.el[i]) {
        if (rnd() % 6) continue;
        M.fi.ex[i] = (int16_t)((rnd() % W) * 16), M.fi.ey[i] = (int16_t)((150 + rnd() % 80) * 16);
        M.fi.evx[i] = (int16_t)((int)(rnd() % 17) - 8), M.fi.evy[i] = (int16_t)(-24 - (int)(rnd() % 36));
        M.fi.el[i] = (uint8_t)(120 + rnd() % 130);
        continue;
      }
      M.fi.ex[i] = (int16_t)(M.fi.ex[i] + M.fi.evx[i] + (isin(M.fi.ey[i] * 2 + i * 99) >> 12));
      M.fi.ey[i] = (int16_t)(M.fi.ey[i] + M.fi.evy[i]);
      M.fi.el[i] = (uint8_t)(M.fi.el[i] > 3 ? M.fi.el[i] - 3 : 0);
      if (M.fi.ey[i] < 0) M.fi.el[i] = 0;
    }
}
static void flames(void) {
  upsample(&M.fi.cv);
  for (int i = 0; i < 48; i++)
    if (M.fi.el[i]) {
      int k = M.fi.el[i] > 200 ? 256 : M.fi.el[i] * 256 / 200;
      glow_dot(M.fi.ex[i], M.fi.ey[i], 20 + (M.fi.el[i] & 15), M.fi.el[i] > 160 ? 0xFFE6A0 : 0xFF8A2A, k);
    }
}

/* ================================================================== Ocean at sunset */
#define O_HZ 132
#define O_SX 232
#define O_SY 104
static void ocean_init(void) {
  static const uint32_t sky[] = {0x141844, 0x2C2A66, 0x5A3878, 0x9C4E7C, 0xE07A6E, 0xFFB07A, 0xFFD49A};
  static const uint32_t sea[] = {0x0E2E4E, 0x0A2440, 0x061A30, 0x041224};
  ramp(&P0, sky, 7), ramp(&P2, sea, 4);
  halo_init(60);
}
/* the sky at (x, y) in screen pixels; with the clouds (cy: their canvas row) or without */
HOT void ocean_sky(int x, int y, int cy, int *R, int *G, int *B) {
  int i = clampi(y * 255 / O_HZ, 0, 255), r = P0.r[i], g = P0.g[i], b = P0.b[i];
  int dx = x - O_SX, dy = y - O_SY, d2 = dx * dx + dy * dy, k = halo(d2 * 2 - 13 * 13 * 2);
  r += 255 * k >> 8, g += 190 * k >> 8, b += 120 * k >> 8;
  if (d2 < 15 * 15) {
    int c = d2 < 13 * 13 ? 256 : (15 * 15 - d2) * 256 / (15 * 15 - 13 * 13);
    r += 255 * c >> 8, g += 240 * c >> 8, b += 200 * c >> 8;
  }
  if (cy >= 0) { /* clouds: dark mauve, gold where the sun reaches them and at their thin edges */
    int d = M.oc.cl[cy][clampi((x >> 1) + 1, 0, CW - 1)];
    if (d) {
      int lit = clampi(255 - (d2 >> 6) + ((255 - d) >> 1), 0, 255);
      int cr = 70 + (185 * lit >> 8), cg = 40 + (130 * lit >> 8), cb = 80 + (30 * lit >> 8), a = d > 200 ? 230 : d + (d >> 3);
      r = lerp8(r, cr, a), g = lerp8(g, cg, a), b = lerp8(b, cb, a);
    }
  }
  *R = r, *G = g, *B = b;
}
static void ocean_frame(int dt) {
  int t = (int)now;
  static int part;
  part = dt ? (part + 1) % 3 : 3; /* (the clouds drift slowly: a third of them worked out each frame; all at first) */
  for (int y = part == 3 ? 0 : part * 21; y < (part == 3 ? 62 : part * 21 + 21) && y < 62; y++) /* (canvas rows) */
    for (int x = 0; x < CW; x++) {
      int n = fbm2(x * 34 + t / 6, y * 70 + 300, 4), Y = 2 * y - 2;
      int d = clampi((n - 128 + (Y < 80 ? 20 - Y / 4 : (80 - Y) * 3 / 2)) * 4, 0, 255), lift = 70 + Y;
      M.oc.cl[y][x] = (uint8_t)(d * lift >> 8 > 255 ? 255 : d * lift >> 8);
    }
  for (int cy = 0; cy < CH; cy++) {
    int y = 2 * cy - 2, r, g, b;
    if (y < O_HZ) {
      for (int cx = 0; cx < CW; cx++) {
        ocean_sky(2 * cx - 2, y, y < 114 && cy < 62 ? cy : -1, &r, &g, &b);
        M.oc.cv.r[cy][cx] = (uint8_t)clampi(r, 0, 255), M.oc.cv.g[cy][cx] = (uint8_t)clampi(g, 0, 255), M.oc.cv.b[cy][cx] = (uint8_t)clampi(b, 0, 255);
      }
      continue;
    }
    int dy = y - O_HZ + 1;
    int32_t z = (900 << 6) / dy; /* distance, 1/64 */
    int amp = dy * 2 / 3, fres = clampi(230 - dy * 2, 50, 230), si = clampi(dy * 255 / 108, 0, 255);
    int wr = P2.r[si], wg = P2.g[si], wb = P2.b[si];
    for (int cx = 0; cx < CW; cx++) {
      int x = 2 * cx - 2, wx = (x - 160) * z >> 9, wz = z;
      /* the sea's slopes: waves coming in, and smaller ones across; the sky they show, darker */
      int sz = isin((wz >> 2) + t / 7) + (isin((wz >> 1) + (wx >> 3) - t / 4) >> 1) + (isin(wz + (wx >> 1) + t / 3) >> 2);
      int sx = (isin((wx >> 2) - (wz >> 3) + t / 5) >> 1) + (isin(wx + (wz >> 2) - t / 3) >> 2);
      int ry = O_HZ - 1 - dy * 5 / 8 - (sz * amp >> 15), rx = x + (sx * amp >> 14);
      ocean_sky(clampi(rx, 0, W - 1), clampi(ry, 0, O_HZ - 1), -1, &r, &g, &b);
      r = lerp8(wr, r * 7 >> 3, fres), g = lerp8(wg, g * 7 >> 3, fres), b = lerp8(wb, b * 7 >> 3, fres);
      int gx = x - O_SX, sp = sz + (sx >> 1); /* glints of the sun on the wave faces that catch it */
      if (iabs(gx) < 14 + dy / 2 && sp > 9000) {
        int k = clampi(((sp - 9000) >> 6) * (256 - iabs(gx) * 256 / (14 + dy / 2)) >> 8, 0, 300);
        r += k, g += k * 7 >> 3, b += k * 5 >> 3;
      }
      M.oc.cv.r[cy][cx] = (uint8_t)clampi(r, 0, 255), M.oc.cv.g[cy][cx] = (uint8_t)clampi(g, 0, 255), M.oc.cv.b[cy][cx] = (uint8_t)clampi(b, 0, 255);
    }
  }
}
static void ocean(void) { upsample(&M.oc.cv); }

/* ================================================================== Bokeh: city lights out of focus */
#define NBK 52
static const uint32_t bk_col[6] = {0xFFB25A, 0xFF6F96, 0xC56BFF, 0x55E0D6, 0xFFE08A, 0xCFE3FF};
static void bk_one(int i, int start) {
  int layer = i < 28 ? 0 : i < 44 ? 1 : 2; /* far, middle, near */
  M.bk.r[i] = (uint8_t)(layer == 0 ? 4 + rnd() % 5 : layer == 1 ? 10 + rnd() % 8 : 22 + rnd() % 16);
  M.bk.x[i] = (int32_t)(rnd() % (W + 80) - 40) * 256;
  M.bk.y[i] = (int32_t)(start ? rnd() % (H + 80) - 40 : H + 45) * 256;
  M.bk.c[i] = (uint8_t)(rnd() % 6), M.bk.l[i] = (uint8_t)layer, M.bk.ph[i] = (uint8_t)rnd();
}
static void bokeh_init(void) {
  static const uint32_t sky[] = {0x05071A, 0x0E0A28, 0x1C0C30, 0x120820};
  ramp(&P0, sky, 4);
  halo_init(140);
  for (int i = 0; i < NBK; i++) bk_one(i, 1);
  for (int cy = 0; cy < CH; cy++) /* the night, and the glow of the city low on it (still: worked out once) */
    for (int cx = 0; cx < CW; cx++) {
      int x = 2 * cx - 2, y = 2 * cy - 2, i = clampi(y * 255 / H, 0, 255);
      int k1 = halo((x - 90) * (x - 90) + (y - 260) * (y - 260) - 3000), k2 = halo((x - 250) * (x - 250) + (y - 250) * (y - 250) - 2000);
      M.bk.cv.r[cy][cx] = (uint8_t)clampi(P0.r[i] + ((90 * k1 + 40 * k2) >> 9), 0, 255);
      M.bk.cv.g[cy][cx] = (uint8_t)clampi(P0.g[i] + ((40 * k1 + 20 * k2) >> 9), 0, 255);
      M.bk.cv.b[cy][cx] = (uint8_t)clampi(P0.b[i] + ((60 * k1 + 80 * k2) >> 9), 0, 255);
    }
}
static void bokeh_frame(int dt) {
  for (int i = 0; i < NBK; i++) {
    int v = M.bk.l[i] == 0 ? 3 : M.bk.l[i] == 1 ? 7 : 14;
    M.bk.y[i] -= v * dt * 4, M.bk.x[i] -= v * dt;
    if (M.bk.y[i] < -45 * 256 || M.bk.x[i] < -50 * 256) bk_one(i, 0);
  }
}
static void bokeh(void) {
  int t = (int)now;
  upsample(&M.bk.cv);
  for (int i = 0; i < NBK; i++) {
    int R = M.bk.r[i], cx = (M.bk.x[i] >> 8) + (isin(t / 9 + M.bk.ph[i] * 4) * 3 >> 14), cy = M.bk.y[i] >> 8;
    int blur = M.bk.l[i] == 0 ? 1 : M.bk.l[i] == 1 ? 2 : 5;
    int y0 = clampi(cy - R - blur, top, top + SH), y1 = clampi(cy + R + blur + 1, top, top + SH);
    if (y0 >= y1) continue;
    int br = (M.bk.l[i] == 2 ? 70 : M.bk.l[i] == 1 ? 110 : 150) * (200 + (isin(t / 5 + M.bk.ph[i] * 7) >> 9)) >> 8;
    uint32_t c = bk_col[M.bk.c[i]];
    int cr = (int)(c >> 16 & 255) * br >> 8, cg = (int)(c >> 8 & 255) * br >> 8, cb = (int)(c & 255) * br >> 8;
    int R2 = R * R, inv = 256 * 128 / (R * blur), rim0 = 2 * 256 / blur + 256;
    for (int y = y0; y < y1; y++) {
      color *p = buf + (y - top) * W;
      int dy2 = (y - cy) * (y - cy);
      for (int x = clampi(cx - R - blur, 0, W), x1 = clampi(cx + R + blur + 1, 0, W); x < x1; x++) {
        int e = (R2 - (x - cx) * (x - cx) - dy2) * inv >> 8; /* about (R - d) / blur, x 256 */
        int a = clampi(e + 128, 0, 256);
        if (!a) continue;
        if (e > 0 && e < rim0) a += (rim0 - e) / 6; /* the rim, a little brighter, as a lens's */
        add(p + x, cr * a >> 8, cg * a >> 8, cb * a >> 8);
      }
    }
  }
}

/* ================================================================== Code Rain */
#define PX(n) ((n) * 16)
#define RC 36 /* front columns, 9 pixels apart */
#define RB 48 /* back columns, 7 apart */
static void rain_init(void) {
  /* the signs: the font's digits and capitals, mirrored, as the film's are */
  static const char signs[41] = "0123456789ZTHEKLPRSXYNMAUVJFGDCWQ=+*<>:|";
  for (int gi = 0; gi < 40; gi++) {
    uint8_t *m = M.cr.mask[gi];
    for (int i = 0; i < 96; i++) m[i] = 0;
    glyph_mask((unsigned char)signs[gi], PX(8) + 8, m, 8, 12, 8, 10, true);
  }
  for (int gi = 0; gi < 40; gi++) /* each sign blurred, for the glow round the leading ones and the far columns */
    for (int y = 0; y < 16; y++)
      for (int x = 0; x < 12; x++) {
        int s = 0;
        for (int dy = -2; dy <= 2; dy++)
          for (int dx = -2; dx <= 2; dx++) {
            int mx = x - 2 + dx, my = y - 2 + dy;
            if (mx >= 0 && mx < 8 && my >= 0 && my < 12) s += M.cr.mask[gi][my * 8 + mx] * (3 - (iabs(dx) > iabs(dy) ? iabs(dx) : iabs(dy)));
          }
        M.cr.blur[gi][y * 12 + x] = (uint8_t)(s / 24 > 255 ? 255 : s / 24);
      }
  for (int l = 0; l < 2; l++)
    for (int c = 0; c < 48; c++) {
      for (int r = 0; r < 24; r++) M.cr.g[l][r][c] = (uint8_t)(rnd() % 40);
      M.cr.head[l][c] = (int32_t)(rnd() % (30 * 256)) - 8 * 256;
      M.cr.spd[l][c] = (uint8_t)(l ? 3 + rnd() % 4 : 5 + rnd() % 6), M.cr.len[l][c] = (uint8_t)(8 + rnd() % 16);
    }
  M.cr.acc = 0;
}
static void rain_frame(int dt) {
  for (int l = 0; l < 2; l++)
    for (int c = 0; c < (l ? RB : RC); c++) {
      M.cr.head[l][c] += M.cr.spd[l][c] * dt;
      if ((M.cr.head[l][c] >> 8) - M.cr.len[l][c] > (l ? 27 : 20)) {
        M.cr.head[l][c] = -(int32_t)(rnd() % (12 * 256));
        M.cr.spd[l][c] = (uint8_t)(l ? 3 + rnd() % 4 : 5 + rnd() % 6), M.cr.len[l][c] = (uint8_t)(8 + rnd() % 16);
      }
    }
  for (M.cr.acc += dt; M.cr.acc > 40; M.cr.acc -= 40)
    for (int i = 0; i < 10; i++) M.cr.g[rnd() & 1][rnd() % 24][rnd() % 48] = (uint8_t)(rnd() % 40);
}
/* a sign's mask (8x12, or 12x16 blurred) at (x, y), in colour, times k */
static void rain_sign(const uint8_t *m, int mw, int mh, int x, int y, int cr, int cg, int cb, int k) {
  for (int j = 0; j < mh; j++) {
    int py = y + j;
    if (py < top || py >= top + SH) continue;
    color *p = buf + (py - top) * W;
    for (int i = 0; i < mw; i++) {
      int px = x + i, a = m[j * mw + i] * k >> 8;
      if (a && px >= 0 && px < W) add(p + px, cr * a >> 8, cg * a >> 8, cb * a >> 8);
    }
  }
}
static void rain(void) {
  for (int j = 0; j < SH; j++) { /* black, a little green at the bottom (a row's four dithered colours, repeated) */
    int y = top + j, f = y > 160 ? (y - 160) / 8 : 0;
    color *p = buf + j * W, c[4];
    const uint8_t *bt = bayer[y & 3];
    for (int k = 0; k < 4; k++) c[k] = dith(0, f + 2, f / 2 + 1, bt[k]);
    for (int x = 0; x < W; x += 4) p[x] = c[0], p[x + 1] = c[1], p[x + 2] = c[2], p[x + 3] = c[3];
  }
  for (int l = 1; l >= 0; l--) { /* the far columns first, small and soft; then the near ones */
    int ncol = l ? RB : RC, cw = l ? 7 : 9, ch = l ? 10 : 13;
    for (int c = 0; c < ncol; c++) {
      int hd = M.cr.head[l][c] >> 8, len = M.cr.len[l][c];
      int r0 = (top - 16) / ch, r1 = (top + SH) / ch + 1;
      for (int r = r0 < 0 ? 0 : r0; r <= r1 && r < 24; r++) {
        int d = hd - r;
        if (d < 0 || d > len) continue;
        int x = c * cw + (l ? 2 : 0), y = r * ch;
        int gi = M.cr.g[l][r][(c + l * 7) % 48];
        int k = d == 0 ? 256 : 230 - d * 200 / len;
        if (l) { /* (out of focus) */
          rain_sign(M.cr.blur[gi], 12, 16, x - 2, y - 2, 30, 190, 80, k * 70 >> 8);
          continue;
        }
        if (d == 0) {
          rain_sign(M.cr.blur[gi], 12, 16, x - 2, y - 2, 120, 255, 160, 200);
          rain_sign(M.cr.mask[gi], 8, 12, x, y, 220, 255, 230, 256);
        } else {
          rain_sign(M.cr.mask[gi], 8, 12, x, y, 40, 255, 110, k);
        }
      }
    }
  }
}

const bg_t bgs[] = {
  {T("Aurora"), aurora_init, aurora_frame, aurora},
  {T("Sunset Drive"), synth_init, 0, synth},
  {T("Plasma"), plasma_init, plasma_frame, plasma},
  {T("Pastel"), pastel_init, pastel_frame, pastel},
  {T("Lava Lamp"), lava_init, lava_frame, lava},
  {T("Warp"), warp_init, warp_frame, warp},
  {T("Tunnel"), tunnel_init, 0, tunnel},
  {T("Fire"), fire_init, fire_frame, flames},
  {T("Ocean"), ocean_init, ocean_frame, ocean},
  {T("Bokeh"), bokeh_init, bokeh_frame, bokeh},
  {T("Code Rain"), rain_init, rain_frame, rain},
};
const int nbg = (int)(sizeof bgs / sizeof bgs[0]);
