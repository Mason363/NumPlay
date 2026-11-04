/* The 3D view's inner loops in C: what rs.s does on the CE, the same arithmetic step for step (24-bit numbers
 * wrap as on the eZ80), for a computer. (On the CE, built with -DNB_BENCH, these are rsc_* to check rs.s with.) */
#include <string.h>
#include "rs.h"
#include "draw.h"

#ifdef __ez80__
#ifndef NB_BENCH
#define RS_NONE
#endif
#define RS(n) rsc_##n
#else
#define RS(n) n
#endif

#ifndef RS_NONE
void RS(rs_setup)(const uint8_t *tex, const uint8_t *cm, uint8_t alpha);
void RS(rs_poly)(const SV *p, uint8_t n);
void RS(rs_proj)(int x, int y, int z, int *out);
uint8_t RS(rs_hidden)(int x, int y, int z, int r);
int RS(rs_code)(int x, int y, int z);
void RS(rs_compose)(uint8_t *dst, const uint8_t *a, const uint8_t *f);
int RS(rs_lerp)(int a, int b, int f);
uint8_t RS(rs_clip2)(const SV *in, uint8_t n, SV *out, uint8_t k);
uint8_t RS(rs_face)(uint8_t f);
void RS(rs_shade)(uint8_t *p, const uint8_t *row, uint16_t n);
void RS(rs_rle)(uint8_t *q, const uint8_t *p, int sw);
uint8_t *RS(rs_runs_of)(const uint8_t *h, uint8_t *o, uint8_t rows);
void RS(rs_expand)(const uint8_t *src, uint8_t *dst, const uint8_t *sky, const uint8_t *hud);
void RS(rs_shade)(uint8_t *p, const uint8_t *row, uint16_t n) {
  for (unsigned i = 0; i < n; i++) p[i] = row[p[i]];
}

uint8_t *RS(rs_runs_of)(const uint8_t *h, uint8_t *o, uint8_t rows) {
  for (; rows; rows--, h += HUD_W)
    for (int x = 0; x < HUD_W;) {
      uint8_t t = 0, n = 0;
      while (x < HUD_W && !h[x]) x++, t++;
      while (x < HUD_W && h[x]) x++, n++;
      *o++ = t, *o++ = n;
    }
  return o;
}

void RS(rs_rle)(uint8_t *q, const uint8_t *p, int sw) {
  while (sw > 0) {
    uint8_t n = *p++;
    if (n < 128) {
      for (int k = 0; k <= n; k++, q++, p++)
        if (*p) *q = *p;
      sw -= n + 1;
    } else {
      uint8_t c = *p++;
      if (c) memset(q, c, (size_t)(n - 126));
      q += n - 126, sw -= n - 126;
    }
  }
}

int RS(rs_lerp)(int a, int b, int f) { return a + (((b - a) * f) >> 8); }

void RS(rs_compose)(uint8_t *dst, const uint8_t *a, const uint8_t *f) {
  for (int i = 0; i < 256; i++) dst[i] = f[a[i]];
}

int RS(rs_code)(int x, int y, int z) {
  int c = 0, v = 0;
  if (z < 819) c |= 1, v |= 1;
  if (x > 4 * z) c |= 2;
  if (-x > 4 * z) c |= 4;
  if (y > 4 * z) c |= 8;
  if (-y > 4 * z) c |= 16;
  if (16 * x > 15 * z) v |= 2;
  if (-16 * x > 15 * z) v |= 4;
  if (4 * y > 3 * z) v |= 8;
  if (-4 * y > 3 * z) v |= 16;
  return c | v << 8;
}

/* (a range of pixels around c, r each way: its first, how many; 0: none on the screen) */
static int h_range(int c, int r, int last, int *first) {
  int lo = c - r, hi = c + r;
  int a = lo < 0 ? 0 : lo >> 4, b;
  if (a > last) return 0;
  if (hi < 0) return 0;
  b = hi >> 4;
  if (b > last) b = last;
  *first = a;
  return b - a + 1;
}

uint8_t RS(rs_hidden)(int x, int y, int z, int r) {
  int o[2], q[2], x0, y0;
  RS(rs_proj)(r, 0, z, q);
  r = q[0] - 1280 + 16;
  RS(rs_proj)(x, y, z, o);
  int rows = h_range(o[1], r, RH - 1, &y0);
  if (!rows) return 1;
  int w = h_range(o[0], r, RW - 1, &x0);
  if (!w) return 1;
  const uint8_t *row = rbuf + y0 * RW + x0;
  for (; rows; rows--, row += RW)
    if (memchr(row, 0, (size_t)w)) return 0;
  return 1;
}

uint8_t RS(rs_cells)(const uint8_t *mask, uint8_t slot, uint8_t up, uint8_t down, int8_t *out);
void RS(rs_column)(const uint8_t *mask, int h2, int Y0, int Z0, int V0, int cx);
void RS(rs_quad)(void);

#ifndef __ez80__
unsigned long rs_visited, rs_drawn, rs_rows, rs_edges, rs_polys;
#define COUNT(x) (x)
#else
#define COUNT(x)
#endif
static const uint8_t *r_tex, *r_cm;
static uint8_t r_alpha;
#define W24(x) ((int32_t)((uint32_t)(x) << 8) >> 8)   /* (as a 24-bit register holds it) */

void RS(rs_setup)(const uint8_t *tex, const uint8_t *cm, uint8_t alpha) { r_tex = tex, r_cm = cm, r_alpha = alpha; }

/* a x r >> 24, a signed, r 0..2^24 (the low partial products left out, as rs.s does) */
static int32_t mulhi(int32_t a, uint32_t r) {
  int neg = a < 0;
  uint32_t m = (uint32_t)(neg ? -a : a) & 0xFFFFFF;
  uint32_t a0 = m & 255, a1 = m >> 8 & 255, a2 = m >> 16, r0 = r & 255, r1 = r >> 8 & 255, r2 = r >> 16 & 255;
  uint32_t t = a2 * r0 + a1 * r1 + a0 * r2;
  int32_t res = (int32_t)((a2 * r2 << 8) + a2 * r1 + a1 * r2 + (t >> 8));
  return W24(neg ? -res : res);
}
/* s x f >> 8, s signed, f 0..255 */
static int32_t mul8(int32_t s, unsigned f) {
  int neg = s < 0;
  uint32_t m = (uint32_t)(neg ? -s : s) & 0xFFFFFF;
  int32_t res = (int32_t)((m * f) >> 8);
  return W24(neg ? -res : res);
}
static uint32_t recip(int k) { const uint8_t *p = rs_recip + (k - 16) * 3; return p[0] | p[1] << 8 | (uint32_t)p[2] << 16; }

typedef struct { int32_t x, u, v, sx, su, sv; int rows, idx; } Walker;

/* the walker's next edge with rows (from vertex idx on, dir 1 or n - 1); false: none left */
static int edge(Walker *w, const SV *p, int n, int dir) {
  COUNT(rs_edges++);
  for (int tries = 0; tries < n; tries++) {
    const SV *a = &p[w->idx];
    int nx = w->idx + dir;
    if (nx >= n) nx -= n;
    const SV *b = &p[nx];
    w->idx = nx;
    int r0 = (a->y + 7) >> 4, r1 = (b->y + 7) >> 4;
    if (r1 <= r0) continue;
    int d = b->y - a->y, f = r0 * 16 + 8 - a->y;
    int32_t dx = (int32_t)(b->x - a->x) * 16, du = (int32_t)(b->u - a->u) * 16, dv = (int32_t)(b->v - a->v) * 16;
    if (d >= 16) {
      uint32_t r = recip(d);
      w->sx = mulhi(dx, r), w->su = mulhi(du, r), w->sv = mulhi(dv, r);
      w->x = (int32_t)a->x * 16 + mul8(w->sx, (unsigned)f * 16);
      w->u = (int32_t)a->u * 16 + mul8(w->su, (unsigned)f * 16);
      w->v = (int32_t)a->v * 16 + mul8(w->sv, (unsigned)f * 16);
    } else {
      unsigned q = rs_ratio[d * 16 + f];
      w->sx = w->su = w->sv = 0;
      w->x = (int32_t)a->x * 16 + mul8(dx, q);
      w->u = (int32_t)a->u * 16 + mul8(du, q);
      w->v = (int32_t)a->v * 16 + mul8(dv, q);
    }
    w->x = W24(w->x), w->u = W24(w->u), w->v = W24(w->v);
    w->rows = r1 - r0;
    return 1;
  }
  return 0;
}

static void span(uint8_t *dst, int n, int u, int v, int du, int dv) {
  do {
    COUNT(rs_visited++);
    if (!*dst) {
      COUNT(rs_drawn++);
      uint8_t t = r_tex[((v >> 8) & 0xF0) | ((u >> 8) & 0xFF)];
      if (!(r_alpha && !t)) *dst = r_cm[t];
    }
    dst++;
    u += du, v += dv;
  } while (--n);
}

void RS(rs_poly)(const SV *p, uint8_t n) {
  int top = 0, ymin = p[0].y, ymax = p[0].y;
  for (int i = 1; i < n; i++) {
    if (p[i].y < ymin) ymin = p[i].y, top = i;
    if (p[i].y > ymax) ymax = p[i].y;
  }
  int y = (ymin + 7) >> 4, yend = (ymax + 7) >> 4;
  if (y >= yend) return;
#ifndef __ez80__
  { extern unsigned long st_h[8]; int hh = yend - y; st_h[hh > 7 ? 7 : hh]++; }
#endif
  Walker a = {0}, b = {0}, *l, *r;
  a.idx = b.idx = top;
  if (!edge(&a, p, n, 1) || !edge(&b, p, n, n - 1)) return;
  l = &a, r = &b;
  if (a.x > b.x || (a.x == b.x && a.sx > b.sx)) l = &b, r = &a;
  uint8_t *row = rbuf + y * RW;
  int lock = 0;
  int32_t du = 0, dv = 0, ku[16], kv[16];
  COUNT(rs_polys++);
  for (;;) {
    COUNT(rs_rows++);
    int px0 = (int)((l->x + 127) >> 8), px1 = (int)((r->x + 127) >> 8);
    if (px1 > RW) px1 = RW;
    if (px0 < px1) {
      int32_t u0, v0;
      if (!lock) {
        du = dv = 0;
        int w = (int)((r->x - l->x) >> 4);
        if (w >= 16) {
          uint32_t rc = recip(w);
          du = mulhi(W24(r->u - l->u), rc), dv = mulhi(W24(r->v - l->v), rc);
          if (w >= 64) {
            /* (four pixels wide or more: this gradient for the rows below; the steps from an edge to a pixel's
             * middle in sixteenths) */
            lock = 1;
            int32_t bu = mul8(du, 8), su = mul8(du, 16), bv = mul8(dv, 8), sv = mul8(dv, 16);
            for (int k = 0; k < 16; k++) ku[k] = W24(bu + su * k), kv[k] = W24(bv + sv * k);
          }
        }
      }
#ifndef __ez80__
      { extern unsigned long st_rl, st_ru; if (lock) st_rl++; else st_ru++; }
#endif
      if (lock) {
        int k = (int)((unsigned)(128 - l->x) & 255) >> 4;
        u0 = W24(l->u + ku[k]), v0 = W24(l->v + kv[k]);
      } else {
        unsigned frac = (unsigned)(128 - l->x) & 255;
        u0 = W24(l->u + mul8(du, frac)), v0 = W24(l->v + mul8(dv, frac));
      }
      if (u0 < 0) u0 = 0;
      if (u0 > 0xFFFFF) u0 = 0xFFFFF;
      if (v0 < 0) v0 = 0;
      if (v0 > 0xFFFFF) v0 = 0xFFFFF;
      span(row + px0, px1 - px0, (int)(u0 >> 8), (int)(v0 >> 4), (int)(du >> 8), (int)(dv >> 4));
    }
    if (++y >= yend) return;
    row += RW;
    l->x = W24(l->x + l->sx), l->u = W24(l->u + l->su), l->v = W24(l->v + l->sv);
    r->x = W24(r->x + r->sx), r->u = W24(r->u + r->su), r->v = W24(r->v + r->sv);
    if (!--l->rows && !edge(l, p, n, l == &a ? 1 : n - 1)) return;
    if (!--r->rows && !edge(r, p, n, r == &a ? 1 : n - 1)) return;
  }
}

static int32_t c2_d(const SV *v, uint8_t k) {
  return k == 0 ? v->x : k == 1 ? RW * 16 - v->x : k == 2 ? v->y : RH * 16 - v->y;
}
uint8_t RS(rs_clip2)(const SV *in, uint8_t n, SV *out, uint8_t k) {
  uint8_t m = 0;
  for (int i = 0; i < n; i++) {
    const SV *a = &in[i], *b = &in[i + 1 == n ? 0 : i + 1];
    int32_t da = c2_d(a, k), db = c2_d(b, k);
    if (da >= 0) out[m++] = *a;
    if ((da < 0) != (db < 0)) {
      /* t: the share before the side, 4096ths, bit by bit; the values as mulhi rounds, the side's exact */
      int32_t kk = da - db, r = da < 0 ? -da : da;
      if (kk < 0) kk = -kk;
      uint32_t t = 0;
      for (int j = 0; j < 12; j++) {
        r <<= 1, t <<= 1;
        if (r >= kk) r -= kk, t |= 1;
      }
      uint32_t tt = t << 12;
      SV o;
      o.x = W24(a->x + mulhi(W24(b->x - a->x), tt)), o.y = W24(a->y + mulhi(W24(b->y - a->y), tt));
      o.u = W24(a->u + mulhi(W24(b->u - a->u), tt)), o.v = W24(a->v + mulhi(W24(b->v - a->v), tt));
      if (k == 0) o.x = 0;
      else if (k == 1) o.x = RW * 16;
      else if (k == 2) o.y = 0;
      else o.y = RH * 16;
      out[m++] = o;
    }
  }
  return m;
}

/* 1371 << 12 / (256 + i) */
static uint16_t rtab[256];
void RS(rs_proj)(int x, int y, int z, int *out) {
  if (!rtab[0])
    for (int i = 0; i < 256; i++) rtab[i] = (uint16_t)((1371L << 12) / (256 + i));
  int c = 0;
  int32_t zz = z;
  while (!(zz & 0x800000)) zz <<= 1, c++;
  int32_t t = rtab[(zz >> 15) & 255];
  /* (x << (c - 3), its top 16 bits as the size, the sign apart: as rs.s does) */
  int32_t xs = W24((int32_t)x << (c - 3)), ys = W24((int32_t)y << (c - 3));
  int32_t mx = ((xs < 0 ? -xs : xs) & 0xFFFFFF) >> 8, my = ((ys < 0 ? -ys : ys) & 0xFFFFFF) >> 8;
  int32_t ox = (mx * t) >> 16, oy = (my * t) >> 16;
  if (xs < 0) ox = -ox;
  if (ys < 0) oy = -oy;
  out[0] = 1280 + (int)ox;
  out[1] = 960 - (int)oy;
}

uint8_t RS(rs_cells)(const uint8_t *mask, uint8_t slot, uint8_t up, uint8_t down, int8_t *out) {
  uint8_t n = 0;
  for (int t = 0; t < up; t++) {
    int b = (slot + t) & 31;
    if (mask[b >> 3] >> (b & 7) & 1) out[n++] = (int8_t)t;
  }
  for (int t = 1; t <= down; t++) {
    int b = (slot - t) & 31;
    if (mask[b >> 3] >> (b & 7) & 1) out[n++] = (int8_t)-t;
  }
  return n;
}

#ifndef __ez80__
int rs_ck[24];
SV rs_fs[4] = {{0, 0, 0, 0}, {0, 0, 65535, 0}, {0, 0, 65535, 65535}, {0, 0, 0, 65535}};
uint8_t rs_fclip;
#endif
static const uint8_t FACEC[8][4] = {{4, 5, 1, 0}, {2, 3, 7, 6}, {3, 2, 0, 1}, {6, 7, 5, 4},
                                    {2, 6, 4, 0}, {7, 3, 1, 5}, {2, 7, 5, 0}, {6, 3, 1, 4}};
uint8_t RS(rs_face)(uint8_t f) {
  uint8_t clip = 0, side = 255;
  for (int i = 0; i < 4; i++) {
    const int *c = rs_ck + 3 * FACEC[f][i];
    int code = RS(rs_code)(c[0], c[1], c[2]);
    clip |= (uint8_t)code, side &= (uint8_t)(code >> 8);
  }
  if (side) return 0;
  rs_fclip = clip;
  if (clip) return 1;
  for (int i = 0; i < 4; i++) {
    const int *c = rs_ck + 3 * FACEC[f][i];
    RS(rs_proj)(c[0], c[1], c[2], &rs_fs[i].x);
  }
  for (int i = 0; i < 4; i++)
    if ((unsigned)rs_fs[i].x > RW * 16 || (unsigned)rs_fs[i].y > RH * 16) return 2;
  RS(rs_poly)(rs_fs, 4);
  return 0;
}

#ifndef __ez80__
const uint8_t *rs_flash;
int rs_jY[65], rs_jZ[65], rs_jV[65];
uint16_t rs_jS[128];
uint8_t rs_fog[257];
int rs_vlim, rs_cy0, rs_cz0, rs_vzk, rs_R, rs_KX;
uint8_t rs_eys, rs_up, rs_down, rs_jbase;
unsigned long st_list, st_frus, st_cov, st_culled, st_cols, st_rl, st_ru, st_h[8], st_c2, st_c3;
int rs_qXi, rs_qYi, rs_qZi, rs_qVi, rs_qsXx, rs_qsYx, rs_qsZx, rs_qsVx, rs_qsXz, rs_qsYz, rs_qsZz, rs_qsVz;
int rs_qx0, rs_qz0, rs_qsxi, rs_qszi, rs_czm, rs_cx0, rs_wx0, rs_wx1, rs_wz0, rs_wz1;
const uint16_t *rs_qsqi, *rs_qsqk;
uint8_t rs_qsx, rs_qsz, rs_qxs0, rs_qzs0, rs_vd;
int rs_x, rs_z, rs_ci, rs_X0;
uint8_t rs_xs, rs_zs;
#define ST(x) (x++)
#else
#define ST(x)
#endif

void RS(rs_column)(const uint8_t *mask, int h2, int Y0, int Z0, int V0, int cx) {
  int8_t list[64];
  uint8_t n = RS(rs_cells)(mask, rs_eys, rs_up, rs_down, list);
#ifndef __ez80__
  st_list += n;
#endif
  for (uint8_t t = 0; t < n; t++) {
    int8_t lj = list[t];
    uint8_t o = (uint8_t)(lj + rs_jbase + 32);
    int d2 = h2 + rs_jS[o];
    if (d2 > rs_vlim) continue;
    int Y = Y0 + rs_jY[o], Z = Z0 + rs_jZ[o], cy = Y + rs_cy0, cz = Z + rs_cz0, vz = V0 + rs_vzk + rs_jV[o];
    if (cz + rs_R < 0) continue;
    if (cz - (cx - rs_KX) < 0 || cz + (cx + rs_KX) < 0) continue;
    if (vz - cy < 0 || vz + cy < 0) continue;
    uint8_t fog = rs_fog[d2];
    if (fog > 8) continue;
    ST(st_frus);
    if (rs_R + rs_R - cz < 0) {
      ST(st_cov);
      if (RS(rs_hidden)(cx, cy, cz, rs_R)) {
        ST(st_culled);
        continue;
      }
    }
    rs_cell(Y, Z, fog, o, (uint8_t)((uint8_t)(lj + rs_eys) & 31));
  }
}

void RS(rs_quad)(void) {
  uint8_t xs = rs_qxs0;
  int x = rs_qx0;
  for (int a = 0; a <= rs_vd; a++) {
    if (x < rs_wx0 || x >= rs_wx1) return;
    int X = rs_qXi, Y = rs_qYi, Z = rs_qZi, V = rs_qVi, z = rs_qz0, hi = rs_qsqi[a];
    uint8_t zs = rs_qzs0;
    for (int c = 0; c <= rs_vd; c++) {
      int h2 = hi + rs_qsqk[c];
      if (h2 > rs_vlim) break;
      if (z < rs_wz0 || z >= rs_wz1) break;
      int cx = X + rs_cx0, lim = Z + rs_czm;
      if (lim - cx >= 0 && lim + cx >= 0) {
        int ci = zs * 32 + xs;
        const uint8_t *mask = (const uint8_t *)&wexp[ci];
#ifndef __ez80__
        st_cols++;
#endif
        if (mask[0] | mask[1] | mask[2] | mask[3]) {
          rs_x = x, rs_z = z, rs_ci = ci, rs_X0 = X, rs_xs = xs, rs_zs = zs;
          RS(rs_column)(mask, h2, Y, Z, V, cx);
        }
      }
      zs = (uint8_t)((zs + rs_qsz) & 31), z += rs_qszi;
      X += rs_qsXz, Y += rs_qsYz, Z += rs_qsZz, V += rs_qsVz;
    }
    xs = (uint8_t)((xs + rs_qsx) & 31), x += rs_qsxi;
    rs_qXi += rs_qsXx, rs_qYi += rs_qsYx, rs_qZi += rs_qsZx, rs_qVi += rs_qsVx;
  }
}

void RS(rs_expand)(const uint8_t *src, uint8_t *dst, const uint8_t *sky, const uint8_t *hud) {
  for (int y = 0; y < RH; y++, src += RW) {
    uint8_t *d = dst + y * 2 * SCREEN_W, s = sky[y];
    for (int x = 0; x < RW; x++) {
      uint8_t c = src[x] ? src[x] : s;
      d[2 * x] = d[2 * x + 1] = c;
    }
    memcpy(d + SCREEN_W, d, SCREEN_W);
    for (int k = 0; k < 2; k++) {
      int r = y * 2 + k;
      if (hud && r >= HUD_Y) {
        const uint8_t *h = hud + (r - HUD_Y) * HUD_W;
        uint8_t *o = d + k * SCREEN_W + HUD_X;
        for (int x = 0; x < HUD_W; x++)
          if (h[x]) o[x] = h[x];
      }
    }
  }
}
#endif

#ifdef __ez80__
/* (rs.s: the trampolines, jp and an address each; the program's copy's addresses; this build's mark) */
extern uint8_t rs_tramps[], rs_tramps_ram[], rs_ntramps, rs_build[3];
void rs_flash_init(const uint8_t *blob, unsigned size) {
  uint8_t n = rs_ntramps;
  bool ok = blob && size >= 8 + 3 * (unsigned)n && !memcmp(blob, "NBRS", 4) && !memcmp(blob + 4, rs_build, 3) &&
            blob[7] == n;
  uint8_t *t = rs_tramps + 1;
  const uint8_t *r = rs_tramps_ram, *o = blob + 8;
  for (uint8_t i = 0; i < n; i++, t += 4, r += 3, o += 3) {
    if (ok) {
      const uint8_t *at = blob + (o[0] | o[1] << 8 | (unsigned)o[2] << 16);
      memcpy(t, &at, 3);
    } else
      memcpy(t, r, 3);
  }
  rs_flash = ok ? blob : 0;
}
#else
void rs_flash_init(const uint8_t *blob, unsigned size) { (void)blob, (void)size; }
#endif

