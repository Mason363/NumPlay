/* The 3D view: the faces of the blocks drawn front to back, at 160 x 120, shown at 2x.
 *
 * The columns around the eye are visited from near to far (a quadrant at a time, each axis outward from the
 * eye: in that order nothing can hide what came before it), and in each column the cells with a face to draw
 * (wexp) outward from the eye's level. A face is projected, clipped to the screen and filled a row of pixels
 * at a time with its texture, mapped linearly across it (faces near the eye are cut into smaller ones, so the
 * mapping stays true). A bit per pixel says which pixels are drawn: each is drawn once, and a cell whose
 * square on screen is all drawn is passed by. What is left at the end is sky.
 *
 * Like Minecraft 1.8 with Fast graphics and smooth lighting off: a face is lit by the light of the cell in
 * front of it, times its side's shade (top 1.0, north and south 0.8, east and west 0.6, bottom 0.5); grass and
 * leaves take their biome's colour; leaves are solid, their holes black; plants, glass and torches let what is
 * behind show through their see-through texels; fog fades the far blocks to the sky's horizon. The palette
 * does the shading: a texel's colour goes through a row of shade_cm (32 levels of light), then a fog row. */
#include "nb.h"

#ifndef NB_NO_RBUF
static uint8_t rbuf_mem[RW * RH];
uint8_t *rbuf = rbuf_mem;
#endif
static uint8_t cov[RH][RW / 8];   /* 1: drawn */
static int px_left;               /* pixels not drawn yet */
uint32_t stat_faces, stat_spans, stat_px, stat_cells;
int view_dist = 13;
uint32_t world_time;
int crack_stage = -1;

/* ---------------------------------------------------------------- the camera */
#define Q 14
#define Q1 (1L << Q)
static int eye_bx, eye_by, eye_bz;      /* the eye's block */
static int rxx, rxz;                    /* camera axes in world units, 2.14: right (no y part) */
static int uxx, uxy, uxz, fxx, fxy, fxz; /* up, forward */
static int bx0, by0, bz0;               /* the camera-space position of the eye block's low corner, Q14 */
#define F16 1371L    /* 60 / tan(35 degrees), in 1/16 pixels: Minecraft's 70 degree view */
#define NEAR (Q1 / 20)

static uint8_t lvl_sky[16], lvl_blk[16];   /* shade level of each light level */
static const uint8_t face_off[6] = {6, 0, 2, 2, 4, 4};   /* -Y +Y -Z +Z -X +X: 0.5 1.0 0.8 0.8 0.6 0.6 */
static uint8_t fog_cm[8][256];          /* fog rows: 1/9 to 8/9 of the way to the fog's colour */
static uint8_t fog_rgb[3], sky_rgb[3];
static int fog_q;                       /* the fog's colour as last made into fog_cm */
static int fog0_2, fog_step;            /* fog: from fog0 (squared, 1/32 blocks) in 8 steps to view_dist */
static uint8_t crack_tex[256];          /* the mined block's texture with its cracks */
static int crack_x, crack_y, crack_z;

int16_t fsin(uint16_t a) {
  /* a quarter wave, 64 steps, linear between */
  static const int16_t q[65] = {
    0, 402, 804, 1205, 1606, 2006, 2404, 2801, 3196, 3590, 3981, 4370, 4756, 5139, 5520, 5897, 6270, 6639, 7005,
    7366, 7723, 8076, 8423, 8765, 9102, 9434, 9760, 10080, 10394, 10702, 11003, 11297, 11585, 11866, 12140, 12406,
    12665, 12916, 13160, 13395, 13623, 13842, 14053, 14256, 14449, 14635, 14811, 14978, 15137, 15286, 15426, 15557,
    15679, 15791, 15893, 15986, 16069, 16143, 16207, 16261, 16305, 16340, 16364, 16379, 16384};
  int quad = a >> 14, k = a & 16383;
  if (quad & 1) k = 16384 - k;
  int i = k >> 8, f = k & 255;
  int v = i >= 64 ? 16384 : q[i] + (((q[i + 1] - q[i]) * f) >> 8);
  return (int16_t)(quad & 2 ? -v : v);
}

/* the camera's axes from the player's yaw and pitch (Minecraft: yaw 0 faces +Z, a quarter turn faces -X) */
static void camera(fx ex, fx ey, fx ez, uint16_t yaw, int16_t pitch) {
  int sy = fsin(yaw), cy = fcos(yaw), sp = fsin((uint16_t)pitch), cp = fcos((uint16_t)pitch);
  fxx = (int)(-(int32_t)sy * cp >> Q), fxy = -sp, fxz = (int)((int32_t)cy * cp >> Q);
  rxx = -cy, rxz = -sy;
  /* up = right x forward */
  uxx = (int)(((int32_t)0 * fxz - (int32_t)rxz * fxy) >> Q);
  uxy = (int)(((int32_t)rxz * fxx - (int32_t)rxx * fxz) >> Q);
  uxz = (int)(((int32_t)rxx * fxy - (int32_t)0 * fxx) >> Q);
  eye_bx = FXI(ex), eye_by = FXI(ey), eye_bz = FXI(ez);
  int32_t dx = -(int32_t)((ex & (FX1 - 1)) << (Q - FXS)), dy = -(int32_t)((ey & (FX1 - 1)) << (Q - FXS)),
          dz = -(int32_t)((ez & (FX1 - 1)) << (Q - FXS));
  bx0 = (int)((dx * rxx + dz * rxz) >> Q);
  by0 = (int)((dx * uxx + dy * uxy + dz * uxz) >> Q);
  bz0 = (int)((dx * fxx + dy * fxy + dz * fxz) >> Q);
}

/* ---------------------------------------------------------------- the light and the sky's colours */
static int sun_k;   /* 0..256: how much the sun lights (Minecraft's sun brightness) */
static int day_k;   /* 0..256: the sky's brightness */

/* the shade level (0..31, 0.89 apart) of a brightness 0..1 (x 4096) */
static int level_of(int32_t b) {
  int k = 31;
  int32_t v = 4096;
  while (k > 0 && v * 89 / 100 >= b) v = v * 89 / 100, k--;
  /* nearest of v and v * 0.89 */
  if (k > 0 && b < (v + v * 89 / 100) / 2) k--;
  return k;
}

static uint16_t lerp_pal(int c, const uint8_t *to, int t9) {
  /* (a colour t9 ninths of the way to `to`, as the nearest of the cube) */
  int r = pal_rgb[c * 3], g = pal_rgb[c * 3 + 1], b = pal_rgb[c * 3 + 2];
  r += (to[0] - r) * t9 / 9, g += (to[1] - g) * t9 / 9, b += (to[2] - b) * t9 / 9;
  /* the cube's levels: (i / 15) ^ 1.8 */
  int ri = 0, gi = 0, bi = 0;
  while (ri < 15 && (cube_levels[ri] + cube_levels[ri + 1]) / 2 < r) ri++;
  while (gi < 15 && (cube_levels[gi] + cube_levels[gi + 1]) / 2 < g) gi++;
  while (bi < 15 && (cube_levels[bi] + cube_levels[bi + 1]) / 2 < b) bi++;
  return pal_cube[ri << 8 | gi << 4 | bi];
}

/* celestial angle (World.calculateCelestialAngle), 0..65535: 0 noon */
static uint16_t celestial(void) {
  int32_t t = (int32_t)((world_time + 18000) % 24000);   /* (Minecraft's time 0 is sunrise: +6000 is noon) */
  /* f = t / 24000 - 0.25, eased: f + ((1 - (cos(f * pi) + 1) / 2) - f) / 3 */
  int32_t f = t * 65536 / 24000;   /* 0..65535 from noon */
  int c = fcos((uint16_t)(f >> 1));   /* cos(f * pi) */
  int32_t e = (16384 - ((int32_t)c + 16384) / 2) * 4;   /* (1 - (cos+1)/2) x 65536 */
  return (uint16_t)(f + (e - f) / 3);
}

void render_sky_palette(void) {
  uint16_t ang = celestial();
  /* World.getSunBrightness and getCelestialLight-ish: the sky's brightness 0.2 at night to 1 by day */
  int c = fcos(ang);   /* 1 at noon, -1 at midnight */
  int32_t f = 16384 - (2 * (int32_t)c + 8192 * 2 / 5);   /* 1 - (cos * 2 + 0.2) */
  f = f < 0 ? 0 : f > 16384 ? 16384 : f;
  sun_k = (int)((16384 - f) * 256 / 16384 * 8 / 10 + 51);   /* x 0.8 + 0.2 */
  int32_t f2 = 16384 - (2 * (int32_t)c + 8192);   /* World.getSkyColor: cos * 2 + 0.5 */
  f2 = f2 < 0 ? 0 : f2 > 16384 ? 16384 : f2;
  day_k = (int)((16384 - f2) * 256 / 16384);
  /* the light table (gamma "Moody"): (1 - f) / (3f + 1), f = 1 - level / 15; the sky's scaled by the sun */
  for (int l = 0; l < 16; l++) {
    int32_t t = (int32_t)l * 4096 / (60 - 3 * l);   /* (l / 15) / ((1 - l / 15) * 3 + 1) */
    int32_t blk = t * 96 / 100 + 123;               /* x 0.96 + 0.03 */
    int32_t sky = (t * (sun_k * 95 / 100 + 13) / 256) * 96 / 100 + 123;
    lvl_blk[l] = (uint8_t)level_of(blk);
    lvl_sky[l] = (uint8_t)level_of(sky);
  }
  /* the sky (Minecraft's plains sky: HSB 0.6, 0.54, 1) and the fog (0.75, 0.85, 1), darker at night */
  int dk = day_k;
  sky_rgb[0] = (uint8_t)(120 * dk / 256), sky_rgb[1] = (uint8_t)(167 * dk / 256), sky_rgb[2] = (uint8_t)(255 * dk / 256);
  int fk = dk * 94 / 100 + 15;
  fog_rgb[0] = (uint8_t)(192 * fk / 256), fog_rgb[1] = (uint8_t)(216 * fk / 256);
  fog_rgb[2] = (uint8_t)(255 * (dk * 91 / 100 + 23) / 256);
  /* (the fog leans a quarter towards the sky) */
  for (int k = 0; k < 3; k++) fog_rgb[k] = (uint8_t)(fog_rgb[k] + (sky_rgb[k] - fog_rgb[k]) * 26 / 100);
  /* the sky's 15 colours: 1 the fog (the horizon) to 15 the sky above */
  uint8_t pal[15 * 3];
  for (int i = 0; i < 15; i++)
    for (int k = 0; k < 3; k++) pal[i * 3 + k] = (uint8_t)(fog_rgb[k] + (sky_rgb[k] - fog_rgb[k]) * i / 14);
  plat_palette(1, 15, pal);
  /* the fog rows, when its colour has changed enough */
  int qf = (fog_rgb[0] >> 3) << 10 | (fog_rgb[1] >> 3) << 5 | (fog_rgb[2] >> 3);
  if (qf != fog_q) {
    fog_q = qf;
    for (int f = 0; f < 8; f++) {
      fog_cm[f][0] = 0;
      for (int i = 1; i < 256; i++) fog_cm[f][i] = i < FIRST_STATIC ? (uint8_t)i : (uint8_t)lerp_pal(i, fog_rgb, f + 1);
    }
  }
}

/* ---------------------------------------------------------------- filling a polygon */
/* (the eZ80's int has 24 bits: what is drawn stays in them; 32-bit only where products need it) */
typedef struct { int x, y, u, v; } SV;      /* screen x, y in 1/16 pixels; u, v in 1/256 texels */
typedef struct { int x, y, z, u, v; } CV;   /* camera space, Q14; u, v */

static const uint8_t *s_tex, *s_cm, *s_fog;
static bool s_alpha;

/* a row of pixels [x0, x1) at row y, those not drawn yet, from texel u, v (1/65536 texels) stepping du, dv */
static void span(int y, int x0, int x1, int u, int v, int du, int dv) {
  uint8_t *c = cov[y], *d = rbuf + y * RW;
  stat_spans++;
  int x = x0;
  while (x < x1) {
    int byte = x >> 3, bit = x & 7;
    int end = (byte + 1) << 3;
    if (end > x1) end = x1;
    unsigned m = c[byte];
    unsigned want = (0xFFu << bit) & (0xFFu >> (8 - (end - (byte << 3))));
    int n = end - x;
    if ((m & want) == want) {   /* all drawn */
      while (n--) u += du, v += dv;
      x = end;
      continue;
    }
    if (!(m & want) && !s_alpha) {   /* none drawn: the whole run */
      c[byte] = (uint8_t)(m | want);
      px_left -= n;
      stat_px += n;
      uint8_t *p = d + x;
      if (s_fog)
        while (n--) {
          *p++ = s_fog[s_cm[s_tex[((v >> 12) & 0xF0) | ((u >> 16) & 15)]]];
          u += du, v += dv;
        }
      else
        while (n--) {
          *p++ = s_cm[s_tex[((v >> 12) & 0xF0) | ((u >> 16) & 15)]];
          u += du, v += dv;
        }
      x = end;
      continue;
    }
    for (; x < end; x++, u += du, v += dv) {
      unsigned b = 1u << (x & 7);
      if (m & b) continue;
      int t = s_tex[((v >> 12) & 0xF0) | ((u >> 16) & 15)];
      if (s_alpha && !t) continue;
      m |= b;
      px_left--;
      stat_px++;
      d[x] = s_fog ? s_fog[s_cm[t]] : s_cm[t];
    }
    c[byte] = (uint8_t)m;
  }
}

/* n x 4096 / d (32-bit arithmetic, the most the eZ80 does) */
static __attribute__((noinline)) int muldiv(int32_t n, int32_t d) {
  int s = 12;
  int32_t an = n < 0 ? -n : n;
  while (s && an < (1L << 29)) an <<= 1, n <<= 1, s--;
  while (s) d >>= 1, s--;
  return d ? (int)(n / d) : 0;
}
/* a + (b - a) x t / 4096 */
static __attribute__((noinline)) int lerp12(int a, int b, int t) { return a + (int)((int32_t)(b - a) * t >> 12); }

/* clipping in 2D against x >= 0 (k 0), x <= W (1), y >= 0 (2), y <= H (3) */
static int clip2(SV *in, int n, SV *out, int k) {
  int m = 0;
  for (int i = 0; i < n; i++) {
    SV *a = &in[i], *b = &in[i + 1 == n ? 0 : i + 1];
    int da, db;
    switch (k) {
      case 0: da = a->x, db = b->x; break;
      case 1: da = RW * 16 - a->x, db = RW * 16 - b->x; break;
      case 2: da = a->y, db = b->y; break;
      default: da = RH * 16 - a->y, db = RH * 16 - b->y;
    }
    if (da >= 0) out[m++] = *a;
    if ((da >= 0) != (db >= 0)) {
      int t = (int)(((int32_t)da << 12) / (da - db));   /* 0..4096 */
      SV *o = &out[m++];
      o->x = lerp12(a->x, b->x, t), o->y = lerp12(a->y, b->y, t);
      o->u = lerp12(a->u, b->u, t), o->v = lerp12(a->v, b->v, t);
      if (k == 0) o->x = 0;
      else if (k == 1) o->x = RW * 16;
      else if (k == 2) o->y = 0;
      else o->y = RH * 16;
    }
  }
  return m;
}

/* fills a convex polygon (any winding) with its texture, mapped linearly */
static int16_t row_xl[RH], row_xr[RH];
static int row_u[RH], row_v[RH];
static void raster(SV *p, int n) {
  static SV buf_a[16], buf_b[16];
  int xmin = p[0].x, xmax = p[0].x, ymin = p[0].y, ymax = p[0].y;
  for (int i = 1; i < n; i++) {
    if (p[i].x < xmin) xmin = p[i].x;
    if (p[i].x > xmax) xmax = p[i].x;
    if (p[i].y < ymin) ymin = p[i].y;
    if (p[i].y > ymax) ymax = p[i].y;
  }
  if (xmax <= 0 || ymax <= 0 || xmin >= RW * 16 || ymin >= RH * 16) return;
  if (xmin < 0 || ymin < 0 || xmax > RW * 16 || ymax > RH * 16) {
    SV *a = p, *b = buf_a;
    if (xmin < 0) n = clip2(a, n, b, 0), a = b, b = a == buf_a ? buf_b : buf_a;
    if (xmax > RW * 16 && n) n = clip2(a, n, b, 1), a = b, b = a == buf_a ? buf_b : buf_a;
    if (ymin < 0 && n) n = clip2(a, n, b, 2), a = b, b = a == buf_a ? buf_b : buf_a;
    if (ymax > RH * 16 && n) n = clip2(a, n, b, 3), a = b, b = a == buf_a ? buf_b : buf_a;
    if (n < 3) return;
    p = a;
    ymin = ymax = p[0].y;
    for (int i = 1; i < n; i++) {
      if (p[i].y < ymin) ymin = p[i].y;
      if (p[i].y > ymax) ymax = p[i].y;
    }
  }
  int y0 = (ymin + 7) >> 4, y1 = (ymax + 7) >> 4;   /* rows whose middle is inside */
  if (y0 < 0) y0 = 0;
  if (y1 > RH) y1 = RH;
  if (y0 >= y1) return;
  /* the texture's gradient: from the largest triangle of the polygon's first vertex */
  int32_t det = 0;
  int ia = 1, ib = 2;
  for (int i = 1; i + 1 < n; i++) {
    int32_t d = (int32_t)(p[i].x - p[0].x) * (p[i + 1].y - p[0].y) - (int32_t)(p[i + 1].x - p[0].x) * (p[i].y - p[0].y);
    if ((d < 0 ? -d : d) > (det < 0 ? -det : det)) det = d, ia = i, ib = i + 1;
  }
  if (det > -64 && det < 64) return;   /* (a sliver of less than a pixel) */
  int ax = p[ia].x - p[0].x, ay = p[ia].y - p[0].y, bx = p[ib].x - p[0].x, by = p[ib].y - p[0].y;
  int au = p[ia].u - p[0].u, bu = p[ib].u - p[0].u, av = p[ia].v - p[0].v, bv = p[ib].v - p[0].v;
  /* d/dx and d/dy per pixel, in 1/65536 texels */
  int dudx = muldiv((int32_t)au * by - (int32_t)bu * ay, det), dudy = muldiv((int32_t)bu * ax - (int32_t)au * bx, det);
  int dvdx = muldiv((int32_t)av * by - (int32_t)bv * ay, det), dvdy = muldiv((int32_t)bv * ax - (int32_t)av * bx, det);
  /* k/16 of a pixel's step, to step from an edge to the next pixel's middle */
  static int ku[16], kv[16];
  for (int k = 0, a = 0, b = 0; k < 16; k++, a += dudx >> 4, b += dvdx >> 4) ku[k] = a, kv[k] = b;
  /* the edges: walk the outline once, noting each row's left and right x, and the texel at the left one */
  for (int y = y0; y < y1; y++) row_xl[y] = 32767, row_xr[y] = -32768;
  for (int i = 0; i < n; i++) {
    const SV *a = &p[i], *b = &p[i + 1 == n ? 0 : i + 1];
    if (a->y == b->y) continue;
    if (a->y > b->y) {
      const SV *t = a;
      a = b, b = t;
    }
    int ya = (a->y + 7) >> 4, yb = (b->y + 7) >> 4;
    if (ya < y0) ya = y0;
    if (yb > y1) yb = y1;
    if (ya >= yb) continue;
    /* x in 1/1024 pixels, a row's step; u, v at the edge, their step */
    int step = (int)(((int32_t)(b->x - a->x) << 10) / (b->y - a->y));
    int dy0 = (ya << 4) + 8 - a->y;
    int x = (a->x << 6) + (int)((int32_t)step * dy0 >> 4);
    int ex = (x >> 6) - a->x;   /* the edge's x at the first row, from the vertex (1/16 px) */
    int u = (a->u << 8) + (int)(((int32_t)dudx * ex + (int32_t)dudy * dy0) >> 4);
    int v = (a->v << 8) + (int)(((int32_t)dvdx * ex + (int32_t)dvdy * dy0) >> 4);
    int su = dudy + (int)((int32_t)dudx * step >> 10), sv = dvdy + (int)((int32_t)dvdx * step >> 10);
    for (int y = ya; y < yb; y++, x += step, u += su, v += sv) {
      int px = (x + (7 << 6)) >> 10;   /* the first pixel whose middle is right of the edge */
      if (px < row_xl[y]) {
        row_xl[y] = (int16_t)px;
        int f = (px << 4) + 8 - (x >> 6);   /* from the edge to its middle: 0..15 sixteenths */
        row_u[y] = u + ku[f & 15], row_v[y] = v + kv[f & 15];
      }
      if (px > row_xr[y]) row_xr[y] = (int16_t)px;
    }
  }
  for (int y = y0; y < y1; y++) {
    int a = row_xl[y], b = row_xr[y];
    int u = row_u[y], v = row_v[y];
    if (a < 0) u -= dudx * a, v -= dvdx * a, a = 0;
    if (b > RW) b = RW;
    if (a >= b) continue;
    span(y, a, b, u, v, dudx, dvdx);
  }
}

/* ---------------------------------------------------------------- faces */
/* projects a camera-space vertex: x, y in 1/16 pixels */
static void project(const CV *c, SV *s) {
  int32_t inv = (F16 << 18) / c->z;
  s->x = RW * 8 + (int)((c->x * inv) >> 18);
  s->y = RH * 8 - (int)((c->y * inv) >> 18);
  s->u = c->u, s->v = c->v;
}

/* clips a camera-space polygon to z >= NEAR and the guard planes |x|, |y| <= 4z */
static int clip3(CV *in, int n, CV *out, int k) {
  int m = 0;
  for (int i = 0; i < n; i++) {
    CV *a = &in[i], *b = &in[i + 1 == n ? 0 : i + 1];
    int da, db;
    switch (k) {
      case 0: da = a->z - NEAR, db = b->z - NEAR; break;
      case 1: da = 4 * a->z - a->x, db = 4 * b->z - b->x; break;
      case 2: da = 4 * a->z + a->x, db = 4 * b->z + b->x; break;
      case 3: da = 4 * a->z - a->y, db = 4 * b->z - b->y; break;
      default: da = 4 * a->z + a->y, db = 4 * b->z + b->y;
    }
    if (da >= 0) out[m++] = *a;
    if ((da >= 0) != (db >= 0)) {
      /* (where it crosses, 0..4096: both brought under 2^18 first) */
      int32_t dd = da - db, aa = da;
      while (dd > 262143 || dd < -262143) dd >>= 1, aa >>= 1;
      int t = (int)((aa << 12) / dd);
      CV *o = &out[m++];
      o->x = lerp12(a->x, b->x, t), o->y = lerp12(a->y, b->y, t), o->z = lerp12(a->z, b->z, t);
      o->u = lerp12(a->u, b->u, t), o->v = lerp12(a->v, b->v, t);
      if (k == 0 && o->z < NEAR) o->z = NEAR;
    }
  }
  return m;
}

static void draw_quad(CV *q, int n) {
  bool far = true;
  for (int i = 0; i < n; i++) {
    int z4 = 4 * q[i].z;
    if (q[i].z < NEAR || q[i].x > z4 || -q[i].x > z4 || q[i].y > z4 || -q[i].y > z4) far = false;
  }
  static CV ca[16], cb[16];
  if (!far) {
    CV *a = q, *b = ca;
    for (int k = 0; k < 5 && n >= 3; k++) {
      n = clip3(a, n, b, k);
      a = b, b = a == ca ? cb : ca;
    }
    if (n < 3) return;
    q = a;
  }
  static SV s[16];
  for (int i = 0; i < n; i++) project(&q[i], &s[i]);
  raster(s, n);
}

/* a face given by its four corners (camera space) and texture corners; near faces are cut in 2 x 2 or 4 x 4 */
static void face4(const int (*c)[3], int split) {
  stat_faces++;
  static const int uv[4][2] = {{0, 0}, {4096, 0}, {4096, 4096}, {0, 4096}};
  if (split <= 1) {
    CV q[4];
    for (int i = 0; i < 4; i++) q[i] = (CV){c[i][0], c[i][1], c[i][2], uv[i][0], uv[i][1]};
    draw_quad(q, 4);
    return;
  }
  /* corners: c0 + s * (c1 - c0) + t * (c3 - c0) */
  int e1[3], e3[3];
  for (int a = 0; a < 3; a++) e1[a] = (c[1][a] - c[0][a]) / split, e3[a] = (c[3][a] - c[0][a]) / split;
  for (int j = 0; j < split; j++)
    for (int i = 0; i < split; i++) {
      CV q[4];
      static const int8_t o[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
      for (int k = 0; k < 4; k++) {
        int s = i + o[k][0], t = j + o[k][1];
        /* (the far edges exactly on the face's: no cracks between the parts and the faces around) */
        for (int a = 0; a < 3; a++) {
          int v = s == split ? c[1][a] + t * e3[a] : c[0][a] + s * e1[a] + t * e3[a];
          if (t == split) v = s == split ? c[2][a] : c[3][a] + s * e1[a];
          if (a == 0) q[k].x = v;
          else if (a == 1) q[k].y = v;
          else q[k].z = v;
        }
        q[k].u = 4096 * s / split, q[k].v = 4096 * t / split;
      }
      draw_quad(q, 4);
    }
}

/* each face's corners as offsets of the cell's low corner: x, y, z bits (top-left, top-right, bottom-right,
 * bottom-left, as seen from outside) */
static const uint8_t FACE_C[6][4] = {
  {0x4, 0x5, 0x1, 0x0},   /* -Y: (0,0,1) (1,0,1) (1,0,0) (0,0,0) */
  {0x2, 0x3, 0x7, 0x6},   /* +Y: (0,1,0) (1,1,0) (1,1,1) (0,1,1) */
  {0x3, 0x2, 0x0, 0x1},   /* -Z: (1,1,0) (0,1,0) (0,0,0) (1,0,0) */
  {0x6, 0x7, 0x5, 0x4},   /* +Z: (0,1,1) (1,1,1) (1,0,1) (0,0,1) */
  {0x2, 0x6, 0x4, 0x0},   /* -X: (0,1,0) (0,1,1) (0,0,1) (0,0,0) */
  {0x7, 0x3, 0x1, 0x5},   /* +X: (1,1,1) (1,1,0) (1,0,0) (1,0,1) */
};
static const int8_t FACE_D[6][3] = {{0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}};
/* the cross of plants: two diagonal quads */
static const uint8_t CROSS_C[2][4] = {{0x2, 0x7, 0x5, 0x0}, {0x6, 0x3, 0x1, 0x4}};

static int corner[8][3];   /* the cell's corners, camera space */
static void corners(int x, int y, int z) {
  for (int k = 0; k < 8; k++) {
    int cx = x, cy = y, cz = z;
    if (k & 1) cx += rxx, cy += uxx, cz += fxx;
    if (k & 2) cy += uxy, cz += fxy;
    if (k & 4) cx += rxz, cy += uxz, cz += fxz;
    corner[k][0] = cx, corner[k][1] = cy, corner[k][2] = cz;
  }
}

static const uint8_t *face_cm(int lb, int side) {
  int l = lvl_sky[lb & 15], b = lvl_blk[lb >> 4];
  if (b > l) l = b;
  l -= face_off[side];
  return shade_cm[l < 0 ? 0 : l];
}

static void crack_make(int tex) {
  const uint8_t *t = tex_px[tex], *c = tex_px[TX_DESTROY_STAGE_0 + crack_stage];
  for (int i = 0; i < 256; i++) crack_tex[i] = c[i] ? shade_cm[18][t[i]] : t[i];
}

static int tile_front(int x, int y, int z) {
  Tile *t = tile_at(x, y, z);
  return t ? t->facing : 3;
}

static void cell(int x, int y, int z, int b, int X, int Y, int Z, int dist2) {
  stat_cells++;
  int fog = 0;
  if (dist2 > fog0_2) {
    fog = (int)((dist2 - fog0_2) / fog_step) + 1;
    if (fog > 8) return;
  }
  s_fog = fog ? fog_cm[fog - 1] : 0;
  corners(X, Y, Z);
  int zmin = corner[0][2];
  for (int k = 1; k < 8; k++)
    if (corner[k][2] < zmin) zmin = corner[k][2];
  int split = zmin < Q1 / 2 ? 4 : zmin < 2 * Q1 ? 2 : 1;
  int m = blk_model[b];
  int ci = CI(x, z);
  if (m == M_CROSS || m == M_TORCH) {
    int tex = blk_tex[b][2];
    if (blk_tint[b]) tex += tint_of_biome[wbiome[ci]];
    s_tex = tex_px[tex];
    s_cm = face_cm(wlight[WI(x, y, z)], 1);
    s_alpha = true;
    for (int k = 0; k < 2; k++) {
      int q[4][3];
      for (int i = 0; i < 4; i++) memcpy(q[i], corner[CROSS_C[k][i]], sizeof q[i]);
      face4(q, split);
    }
    s_alpha = false;
    return;
  }
  s_alpha = (blk_flags[b] & BF_ALPHA) != 0;
  bool mined = crack_stage >= 0 && x == crack_x && y == crack_y && z == crack_z;
  for (int f = 0; f < 6; f++) {
    /* only the faces turned to the eye */
    int d = f >> 1, pos = f & 1;
    int e = d == 0 ? eye_by - y : d == 1 ? eye_bz - z : eye_bx - x;
    if (pos ? e <= 0 : e >= 0) continue;
    int nx = x + FACE_D[f][0], ny = y + FACE_D[f][1], nz = z + FACE_D[f][2];
    int nb = world_get(nx, ny, nz);
    if (blk_flags[nb] & BF_OPAQUE) continue;
    if (nb == b) continue;
    if ((blk_flags[b] & BF_LIQUID) && (blk_flags[nb] & BF_LIQUID)) continue;
    int tex = blk_tex[b][f];
    if (blk_tint[b] >> f & 1) tex += tint_of_biome[wbiome[ci]];
    if ((b == B_FURNACE || b == B_FURNACE_LIT || b == B_CHEST) && f >= 2) {
      int fr = tile_front(x, y, z);
      tex = f == fr ? blk_tex[b][3] : blk_tex[b][2];
    } else if (b == B_CRAFTING_TABLE && f >= 2)
      tex = f == 3 || f == 4 ? blk_tex[b][3] : blk_tex[b][2];
    if (mined) {
      crack_make(tex);
      s_tex = crack_tex;
    } else s_tex = tex_px[tex];
    s_cm = face_cm(world_light(nx, ny, nz), f);
    int q[4][3];
    for (int i = 0; i < 4; i++) memcpy(q[i], corner[FACE_C[f][i]], sizeof q[i]);
    face4(q, split);
  }
  s_alpha = false;
}

/* is the square of radius r (1/16 px) around (sx, sy) all drawn? */
static bool covered(int sx, int sy, int r) {
  int x0 = (int)((sx - r) >> 4), x1 = (int)((sx + r) >> 4), y0 = (int)((sy - r) >> 4), y1 = (int)((sy + r) >> 4);
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 >= RW) x1 = RW - 1;
  if (y1 >= RH) y1 = RH - 1;
  if (x0 > x1 || y0 > y1) return true;   /* off screen */
  int b0 = x0 >> 3, b1 = x1 >> 3;
  uint8_t m0 = (uint8_t)(0xFF << (x0 & 7)), m1 = (uint8_t)(0xFF >> (7 - (x1 & 7)));
  for (int y = y0; y <= y1; y++) {
    const uint8_t *c = cov[y];
    if (b0 == b1) {
      if ((c[b0] & (m0 & m1)) != (m0 & m1)) return false;
      continue;
    }
    if ((c[b0] & m0) != m0 || (c[b1] & m1) != m1) return false;
    for (int b = b0 + 1; b < b1; b++)
      if (c[b] != 0xFF) return false;
  }
  return true;
}

/* a column's cells, outward from the eye's level */
static void column(int x, int z) {
  if ((unsigned)(x - win_x) >= WS || (unsigned)(z - win_z) >= WS) return;
  uint32_t mask = wexp[CI(x, z)];
  if (!mask) return;
  int i = x - eye_bx, k = z - eye_bz;
  /* the column's low corner at the eye's level */
  int X0 = bx0 + i * rxx + k * rxz;
  int Y0 = by0 + i * uxx + k * uxz;
  int Z0 = bz0 + i * fxx + k * fxz;
  int ylo = win_y, yhi = win_y + WH - 1;
  const int R = (int)(Q1 * 87 / 100);   /* a cell's half diagonal */
  int vd2 = (view_dist * 32 + 28) * (view_dist * 32 + 28);
  for (int pass = 0; pass < 2; pass++) {
    int y = pass ? eye_by - 1 : eye_by, dy = pass ? -1 : 1;
    if (y < ylo) y = ylo;
    if (y > yhi) y = yhi;
    for (; pass ? y >= ylo : y <= yhi; y += dy) {
      if (!(mask >> (y & 31) & 1)) continue;
      int j = y - eye_by;
      int X = X0, Y = Y0 + j * uxy, Z = Z0 + j * fxy;
      /* its middle, its distance, the view's sides */
      int cx = X + (rxx + rxz) / 2, cy = Y + (uxx + uxy + uxz) / 2, cz = Z + (fxx + fxy + fxz) / 2;
      if (cz < -R) continue;
      int ax = cx < 0 ? -cx : cx, ay = cy < 0 ? -cy : cy;
      /* (|x| <= z * tan(43 degrees) + r / cos: 0.933 and 1.37; |y| <= z * 0.7 + r * 1.22) */
      if (ax > cz - cz / 16 + R * 137 / 100 || ay > cz / 10 * 7 + R * 122 / 100) continue;
      int qx = cx >> 9, qy = cy >> 9, qz = cz >> 9;   /* (1/32 blocks) */
      int d2 = qx * qx + qy * qy + qz * qz;
      if (d2 > vd2) continue;
      /* passed by when what is in front of it covers its square already */
      if (cz > R) {
        int32_t inv = (F16 << 18) / cz;
        int sx = RW * 8 + (int)(((int32_t)cx * inv) >> 18), sy = RH * 8 - (int)(((int32_t)cy * inv) >> 18);
        int r = (int)(((int32_t)R * inv) >> 18) + 16;
        if (covered(sx, sy, r)) continue;
      }
      cell(x, y, z, wblk[WI(x, y, z)], X, Y, Z, d2);
      if (px_left <= 0) return;
    }
  }
}

/* ---------------------------------------------------------------- the sky */
static void sky(void) {
  /* each row: its height in the sky (the ray through its middle, at the screen's middle column) */
  for (int y = 0; y < RH; y++) {
    uint8_t *c = cov[y];
    bool any = false;
    for (int b = 0; b < RW / 8; b++)
      if (c[b] != 0xFF) any = true;
    if (!any) continue;
    /* the ray: forward + (60 - y - 0.5) / F * up; its world height part */
    int32_t t = (int32_t)(RH / 2 * 16 - y * 16 - 8) * Q1 / F16;   /* Q14 */
    int32_t ry = fxy + (t * uxy >> Q);
    int32_t len = Q1;   /* (approximately: elevation from ry) */
    int e = (int)(ry * 15 * 5 / 2 / len);   /* 0 at the horizon, 15 at 0.4 up */
    uint8_t col = (uint8_t)(e <= 0 ? 1 : e >= 14 ? 15 : e + 1);
    uint8_t *d = rbuf + y * RW;
    for (int b = 0; b < RW / 8; b++) {
      uint8_t m = c[b];
      if (m == 0xFF) continue;
      for (int k = 0; k < 8; k++)
        if (!(m >> k & 1)) d[b * 8 + k] = col;
    }
  }
}

/* ---------------------------------------------------------------- a frame */
static void frame_from(fx ex, fx ey, fx ez, uint16_t yaw, int16_t pitch) {
  camera(ex, ey, ez, yaw, pitch);
  memset(cov, 0, sizeof cov);
  px_left = RW * RH;
  stat_faces = stat_spans = stat_px = stat_cells = 0;
  int fd = view_dist * 32;
  fog0_2 = fd * fd / 16 * 9;   /* the fog starts at 3/4 of the view (squared, 1/32 blocks) */
  fog_step = (fd * fd - fog0_2) / 9 + 1;
  int R = view_dist;
  /* the quadrants: the one looked into first */
  int qx = fxx >= 0 ? 0 : 1, qz = fxz >= 0 ? 0 : 2;
  static const uint8_t order[4] = {0, 1, 2, 3};
  for (int n = 0; n < 4 && px_left > 0; n++) {
    int q = order[n] ^ qx ^ qz;
    int sx = q & 1 ? -1 : 1, sz = q & 2 ? -1 : 1;
    for (int i = 0; i <= R && px_left > 0; i++) {
      int x = sx > 0 ? eye_bx + i : eye_bx - 1 - i;
      for (int k = 0; k <= R && px_left > 0; k++) {
        int z = sz > 0 ? eye_bz + k : eye_bz - 1 - k;
        int dx = sx > 0 ? i : i + 1, dz = sz > 0 ? k : k + 1;
        if ((dx - 1) * (dx - 1) + (dz - 1) * (dz - 1) > R * R) break;
        column(x, z);
      }
    }
  }
  sky();
}

void render_frame(void) {
  fx ey = pl.y + (pl.sneaking ? FX1 * 154 / 100 : FX1 * 162 / 100);
  if (pl.hit_face >= 0) crack_x = pl.hit_x, crack_y = pl.hit_y, crack_z = pl.hit_z;
  crack_stage = pl.break_need > 0 && pl.break_ticks > 0 ? pl.break_ticks * 10 / pl.break_need : -1;
  if (crack_stage > 9) crack_stage = 9;
  frame_from(pl.x, ey, pl.z, pl.yaw, pl.pitch);
}

void render_title(uint16_t yaw) {
  crack_stage = -1;
  frame_from(pl.x, pl.y + FX1 * 162 / 100, pl.z, yaw, 1200);
}

void render_init(void) { fog_q = -1; }
