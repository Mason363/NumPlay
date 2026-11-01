/* The 3D renderer (r3d.h): polygons in a list, drawn band by band with a depth buffer. */
#include "r3d.h"
#include <math.h>
#if defined(__ARM_FEATURE_SIMD32) && !defined(HOST)
#include <arm_acle.h>
#endif

#define NEAR 0.1f
#define ZK R_ZK          /* depths are ZK / z: 65000 at the near plane, larger is nearer */
#define MAXP 2000        /* polygons a frame */
#define MAXV 6600        /* their corners */
#define MAXN 14          /* corners of a polygon once cut */
#define SUB 8.0f         /* corners are kept in 1/8 pixels */
#define GUARD 1900.0f    /* (so they may lie at most this far from the screen's middle) */

typedef struct {
  float a, b, c;         /* its depth at (sx, sy): a sx + b sy + c */
  uint16_t v0;           /* its first corner */
  uint16_t color;        /* RGB565 */
  uint8_t y0, y1;        /* the rows it covers */
  uint8_t n;             /* its corners */
  uint8_t alpha;         /* see-through (255: not) */
} Poly;
typedef struct {
  int16_t x, y;
} Pos;

uint16_t *r_col;
int r_y0, r_y1;
Camera cam;

/* the frame's lists: in the RAM after the game's own (r_init). The see-through polygons are kept from the end of
 * polys down, so that they are drawn last, in the order they came. */
typedef struct {
  Poly polys[MAXP];
  Pos pos[MAXV];
  uint16_t zbuf[SCREEN_W * BAND_H] __attribute__((aligned(8)));
} Work;
static Work *work;
#define polys (work->polys)
#define pos (work->pos)
#define zbuf (work->zbuf)
static int npolys, nalpha, nverts;

void r_set_band_memory(uint16_t *band) { r_col = band; }
uint32_t r_work_size(void) { return sizeof(Work); }
void r_init(void *mem) { work = (Work *)mem; }

void r_camera(float x, float y, float z, const float r[9], float f, float cx, float cy) {
  cam.x = x, cam.y = y, cam.z = z, cam.f = f, cam.cx = cx, cam.cy = cy;
  for (int i = 0; i < 9; i++) cam.r[i] = r[i];
}

#ifdef HOST
int r_dropped;   /* (polygons left out this frame: the list was full) */
#endif
void r_begin(void) {
  npolys = nalpha = nverts = 0;
#ifdef HOST
  r_dropped = 0;
#endif
}
int r_count(void) { return npolys + nalpha; }
bool r_full(void) { return npolys + nalpha >= MAXP * 19 / 20; }

/* ---------------------------------------------------------------- cutting */
/* keeps the part of the polygon where a x + b y + c z + d >= 0 */
static int cut(const V3 *in, int n, V3 *out, float a, float b, float c, float d) {
  int m = 0;
  for (int i = 0, j = n - 1; i < n; j = i++) {
    float di = a * in[i].x + b * in[i].y + c * in[i].z + d, dj = a * in[j].x + b * in[j].y + c * in[j].z + d;
    if ((di >= 0) != (dj >= 0) && m < MAXN) {
      float t = dj / (dj - di);
      const V3 *p = &in[j], *q = &in[i];
      out[m++] = (V3){p->x + t * (q->x - p->x), p->y + t * (q->y - p->y), p->z + t * (q->z - p->z)};
    }
    if (di >= 0 && m < MAXN) out[m++] = in[i];
  }
  return m;
}

/* projects, checks and keeps a polygon in camera space */
static void emit(const V3 *p, int n, uint16_t color, int flags, int alpha) {
#ifdef HOST
  r_dropped += npolys + nalpha >= MAXP;
#endif
  if (npolys + nalpha >= MAXP || nverts + MAXN > MAXV) return;
  V3 b1[MAXN], b2[MAXN];
  int behind = 0;
  for (int i = 0; i < n; i++) behind += p[i].z < NEAR;
  if (behind == n) return;
  if (behind) {
    n = cut(p, n, b1, 0, 0, 1, -NEAR);
    if (n < 3) return;
    p = b1;
  }
  float sx[MAXN], sy[MAXN];
  float f = cam.f;
  bool wide = false;
  for (int i = 0; i < n; i++) {
    float k = f / p[i].z;
    sx[i] = p[i].x * k, sy[i] = -p[i].y * k;
    if (sx[i] > GUARD || sx[i] < -GUARD || sy[i] > GUARD || sy[i] < -GUARD) wide = true;
  }
  if (wide) {
    /* far off the screen: cut at the guard band's sides */
    float g = GUARD / f;
    n = cut(p, n, b2, -1, 0, g, 0);
    n = n >= 3 ? cut(b2, n, b1, 1, 0, g, 0) : 0;
    n = n >= 3 ? cut(b1, n, b2, 0, -1, g, 0) : 0;
    n = n >= 3 ? cut(b2, n, b1, 0, 1, g, 0) : 0;
    if (n < 3) return;
    p = b1;
    for (int i = 0; i < n; i++) {
      float k = f / p[i].z;
      sx[i] = p[i].x * k, sy[i] = -p[i].y * k;
    }
  }
  float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
  for (int i = 0; i < n; i++) {
    sx[i] += cam.cx, sy[i] += cam.cy;
    if (sx[i] < x0) x0 = sx[i];
    if (sx[i] > x1) x1 = sx[i];
    if (sy[i] < y0) y0 = sy[i];
    if (sy[i] > y1) y1 = sy[i];
  }
  if (x1 < 0 || x0 >= SCREEN_W || y1 < 0 || y0 >= SCREEN_H) return;
  float area = 0;
  for (int i = 0, j = n - 1; i < n; j = i++) area += sx[j] * sy[i] - sx[i] * sy[j];
  if (area < 0.01f && !(flags & P_TWOSIDED && area < -0.01f)) return;
  int iy0 = y0 < 0 ? 0 : (int)(y0 + 0.5f), iy1 = y1 >= SCREEN_H ? SCREEN_H : (int)(y1 + 0.5f);
  if (iy0 >= iy1) return;
  /* the depth plane, by Newell's method (good for any convex polygon): depth = ZK (n . (xs, ys, 1)) / d */
  V3 nm = {0, 0, 0};
  for (int i = 0, j = n - 1; i < n; j = i++) {
    nm.x += (p[j].y - p[i].y) * (p[j].z + p[i].z);
    nm.y += (p[j].z - p[i].z) * (p[j].x + p[i].x);
    nm.z += (p[j].x - p[i].x) * (p[j].y + p[i].y);
  }
  float d = nm.x * p[0].x + nm.y * p[0].y + nm.z * p[0].z;
  if (d > -1e-6f && d < 1e-6f) return;   /* seen edge-on */
  int a8 = flags & P_ALPHA ? (alpha > 255 ? 255 : alpha < 0 ? 0 : alpha) : 255;
  Poly *q = a8 == 255 ? &polys[npolys++] : &polys[MAXP - 1 - nalpha++];
  float s = ZK / d;
  q->a = s * nm.x / f;
  q->b = -s * nm.y / f;
  q->c = s * (nm.z - nm.x * cam.cx / f + nm.y * cam.cy / f);
  q->color = color;
  q->alpha = (uint8_t)a8;
  q->v0 = (uint16_t)nverts, q->n = (uint8_t)n;
  for (int i = 0; i < n; i++) pos[nverts + i] = (Pos){(int16_t)(sx[i] * SUB + 0.5f), (int16_t)(sy[i] * SUB + 0.5f)};
  q->y0 = (uint8_t)iy0, q->y1 = (uint8_t)iy1;
  nverts += n;
}

void r_poly_cam(const V3 *v, int n, uint16_t c565, int flags, int alpha) {
  if (n < 3 || n > MAXN - 4) return;
  emit(v, n, c565, flags, alpha);
}

bool r_tri_screen(const V3 *A, const V3 *B, const V3 *C, uint16_t color) {
  float ax = A->x - cam.cx, ay = A->y - cam.cy, bx = B->x - cam.cx, by = B->y - cam.cy, cx = C->x - cam.cx, cy = C->y - cam.cy;
  if (fabsf(ax) > GUARD || fabsf(ay) > GUARD || fabsf(bx) > GUARD || fabsf(by) > GUARD || fabsf(cx) > GUARD || fabsf(cy) > GUARD)
    return false;
  /* (twice its area, as emit's: turned away or too thin to be seen) */
  float x1 = B->x - A->x, y1 = B->y - A->y, x2 = C->x - A->x, y2 = C->y - A->y;
  float det = x1 * y2 - x2 * y1;
  if (det < 0.01f) return true;
  float x0 = fminf(A->x, fminf(B->x, C->x)), xm = fmaxf(A->x, fmaxf(B->x, C->x));
  float y0 = fminf(A->y, fminf(B->y, C->y)), ym = fmaxf(A->y, fmaxf(B->y, C->y));
  if (xm < 0 || x0 >= SCREEN_W || ym < 0 || y0 >= SCREEN_H) return true;
  int iy0 = y0 < 0 ? 0 : (int)(y0 + 0.5f), iy1 = ym >= SCREEN_H ? SCREEN_H : (int)(ym + 0.5f);
  if (iy0 >= iy1) return true;
#ifdef HOST
  r_dropped += npolys + nalpha >= MAXP;
#endif
  if (npolys + nalpha >= MAXP || nverts + 3 > MAXV) return true;
  /* the depth plane through the three corners (a triangle's depth R_ZK / z is exactly a plane on the screen) */
  Poly *q = &polys[npolys];
  float inv = 1 / det, w1 = B->z - A->z, w2 = C->z - A->z;
  q->a = (w1 * y2 - w2 * y1) * inv;
  q->b = (x1 * w2 - x2 * w1) * inv;
  q->c = A->z - q->a * A->x - q->b * A->y;
  q->alpha = 255, q->color = color;
  q->v0 = (uint16_t)nverts, q->n = 3;
  pos[nverts] = (Pos){(int16_t)(A->x * SUB + 0.5f), (int16_t)(A->y * SUB + 0.5f)};
  pos[nverts + 1] = (Pos){(int16_t)(B->x * SUB + 0.5f), (int16_t)(B->y * SUB + 0.5f)};
  pos[nverts + 2] = (Pos){(int16_t)(C->x * SUB + 0.5f), (int16_t)(C->y * SUB + 0.5f)};
  q->y0 = (uint8_t)iy0, q->y1 = (uint8_t)iy1;
  nverts += 3;
  npolys++;
  return true;
}

/* ---------------------------------------------------------------- drawing a band */
/* an opaque run of n pixels: each one nearer than what is there takes the colour (two at a time on the calculator:
 * its DSP compares and picks both halves of a word at once) */
static inline __attribute__((always_inline)) void span_flat(uint16_t *zb, uint16_t *cb, int n, uint32_t z, uint32_t da, uint32_t col) {
#if defined(__ARM_FEATURE_SIMD32) && !defined(HOST)
  if (((uintptr_t)zb & 2) && n) {
    uint32_t zz = z >> 16;
    if (zz > *zb) *zb = (uint16_t)zz, *cb = (uint16_t)col;
    zb++, cb++, z += da, n--;
  }
  uint32_t cc = col | col << 16, d2 = da * 2;
  uint32_t *zw = (uint32_t *)(void *)zb, *cw = (uint32_t *)(void *)cb;
  for (; n >= 2; n -= 2, zw++, cw++, z += d2) {
    uint32_t nz = z >> 16 | ((z + da) & 0xFFFF0000u), oz = *zw;
    __usub16(oz, nz);   /* (each half: what is there is as near or nearer) */
    *zw = __sel(oz, nz), *cw = __sel(*cw, cc);
  }
  zb = (uint16_t *)zw, cb = (uint16_t *)cw;
  if (n) {
    uint32_t zz = z >> 16;
    if (zz > *zb) *zb = (uint16_t)zz, *cb = (uint16_t)col;
  }
#else
  for (uint16_t *ze = zb + n; zb < ze; zb++, cb++, z += da) {
    uint32_t zz = z >> 16;
    if (zz > *zb) *zb = (uint16_t)zz, *cb = (uint16_t)col;
  }
#endif
}

/* the depth at the middle of pixel x of the row whose middle is yc, in 16.16 */
static inline uint32_t depth_at(const Poly *q, int x, float yc) {
  float zf = q->a * ((float)x + 0.5f) + q->b * yc + q->c;
  if (zf < 0) zf = 0;
  if (zf > 65535) zf = 65535;
  return (uint32_t)(zf * 65536.0f);
}

/* the depth's step from one pixel to the next */
static inline uint32_t depth_step(const Poly *q) {
  float qa = q->a > 30000 ? 30000 : q->a < -30000 ? -30000 : q->a;   /* (seen nearly edge-on) */
  return (uint32_t)(int32_t)(qa * 65536.0f);
}

/* an edge crossing the band: where it starts and ends, and x = x + y dx along it */
typedef struct {
  float y0, y1, x, dx;
} Edge;

/* any polygon (cut ones, see-through ones): its left side and its right side, edge by edge from the top; each pixel
 * whose middle lies inside is drawn */
static void raster(const Poly *q) {
  int ya = q->y0 > r_y0 ? q->y0 : r_y0, yb = q->y1 < r_y1 ? q->y1 : r_y1;
  if (ya >= yb) return;
  const Pos *pp = pos + q->v0;
  int n = q->n;
  /* (a convex polygon has a left side and a right side from its top to its bottom; which way it turns tells
   * which edges go down which side) */
  float area = 0;
  for (int i = 0, j = n - 1; i < n; j = i++) area += (float)pp[j].x * (float)pp[i].y - (float)pp[i].x * (float)pp[j].y;
  Edge e[2][MAXN];
  int ne[2] = {0, 0};
  for (int i = 0, j = n - 1; i < n; j = i++) {
    float ay = pp[j].y * (1 / SUB), by = pp[i].y * (1 / SUB);
    if (ay == by) continue;
    int ka = j, kb = i;
    bool down = by > ay;
    if (!down) {
      float t = ay;
      ay = by, by = t;
      ka = i, kb = j;
    }
    if (by <= (float)ya || ay >= (float)yb) continue;
    int side = (down == (area > 0)) ? 1 : 0;   /* 1: right */
    /* (kept in order from the top) */
    int k = ne[side]++;
    while (k > 0 && e[side][k - 1].y0 > ay) e[side][k] = e[side][k - 1], k--;
    Edge *E = &e[side][k];
    float h = 1 / (by - ay), xa = pp[ka].x * (1 / SUB), xb = pp[kb].x * (1 / SUB);
    E->y0 = ay, E->y1 = by;
    E->dx = (xb - xa) * h, E->x = xa - ay * E->dx;
  }
  if (!ne[0] || !ne[1]) return;
  uint16_t col = q->color;
  int alpha = q->alpha >> 3;
  uint32_t da = depth_step(q);
  int li = 0, ri = 0;
  for (int y = ya; y < yb; y++) {
    float yc = (float)y + 0.5f;
    while (li < ne[0] - 1 && yc >= e[0][li].y1) li++;
    while (ri < ne[1] - 1 && yc >= e[1][ri].y1) ri++;
    const Edge *L = &e[0][li], *R = &e[1][ri];
    if (yc < L->y0 || yc >= L->y1 || yc < R->y0 || yc >= R->y1) continue;
    float xl = L->x + yc * L->dx, xr = R->x + yc * R->dx;
    if (xl > xr) {
      float t = xl;
      xl = xr, xr = t;
    }
    int x0 = xl <= -1 ? 0 : (int)(xl + 0.5f), x1 = xr >= SCREEN_W ? SCREEN_W : (int)(xr + 0.5f);
    if (x0 < 0) x0 = 0;
    if (x0 >= x1) continue;
    uint32_t z = depth_at(q, x0, yc);
    uint16_t *zb = zbuf + (y - r_y0) * SCREEN_W, *cb = r_col + (y - r_y0) * SCREEN_W;
    if (alpha >= 31) span_flat(zb + x0, cb + x0, x1 - x0, z, da, col);
    else
      for (int x = x0; x < x1; x++, z += da)
        if ((uint16_t)(z >> 16) > zb[x]) cb[x] = r_mix565(cb[x], col, alpha);
  }
}

/* one half of a triangle: rows [y, ye) between the edges x = lx + yc ldx and x = rx + yc rdx (as raster's) */
static inline __attribute__((always_inline)) void tri_rows(const Poly *q, int y, int ye, float lx, float ldx, float rx, float rdx,
                                                          uint32_t da) {
  for (; y < ye; y++) {
    float yc = (float)y + 0.5f;
    float xl = lx + yc * ldx, xr = rx + yc * rdx;
    int x0 = xl <= -1 ? 0 : (int)(xl + 0.5f), x1 = xr >= SCREEN_W ? SCREEN_W : (int)(xr + 0.5f);
    if (x0 < 0) x0 = 0;
    if (x0 >= x1) continue;
    int k = (y - r_y0) * SCREEN_W + x0;
    span_flat(zbuf + k, r_col + k, x1 - x0, depth_at(q, x0, yc), da, q->color);
  }
}

/* an opaque triangle (most of the world), drawn as raster draws any polygon but with less to find out: its corners
 * from the top, the long edge on one side and the two others in turn on the other */
__attribute__((noinline)) static void raster_tri(const Poly *q) {
  int ya = q->y0 > r_y0 ? q->y0 : r_y0, yb = q->y1 < r_y1 ? q->y1 : r_y1;
  if (ya >= yb) return;
  const Pos *p0 = pos + q->v0, *p1 = p0 + 1, *p2 = p0 + 2, *t;
  if (p1->y < p0->y) t = p0, p0 = p1, p1 = t;
  if (p2->y < p1->y) t = p1, p1 = p2, p2 = t;
  if (p1->y < p0->y) t = p0, p0 = p1, p1 = t;
  if (p0->y == p2->y) return;
  float x0 = p0->x * (1 / SUB), y0 = p0->y * (1 / SUB), x1 = p1->x * (1 / SUB), y1 = p1->y * (1 / SUB);
  float x2 = p2->x * (1 / SUB), y2 = p2->y * (1 / SUB);
  /* the edges as raster keeps them: x = e + yc d */
  float d02 = (x2 - x0) / (y2 - y0), e02 = x0 - y0 * d02;
  uint32_t da = depth_step(q);
  /* the long edge on the left when the middle corner is to its right */
  bool long_left = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0) > 0;
  /* rows whose middles lie in [ys, ye): from ceil(ys - 0.5) */
  int r0 = (int)__builtin_ceilf(y0 - 0.5f), r1 = (int)__builtin_ceilf(y1 - 0.5f), r2 = (int)__builtin_ceilf(y2 - 0.5f);
  if (r0 < ya) r0 = ya;
  if (r2 > yb) r2 = yb;
  int m = r1 < r0 ? r0 : r1 > r2 ? r2 : r1;
  if (p0->y != p1->y && r0 < m) {
    float d = (x1 - x0) / (y1 - y0), e = x0 - y0 * d;
    if (long_left) tri_rows(q, r0, m, e02, d02, e, d, da);
    else tri_rows(q, r0, m, e, d, e02, d02, da);
  }
  if (p1->y != p2->y && m < r2) {
    float d = (x2 - x1) / (y2 - y1), e = x1 - y1 * d;
    if (long_left) tri_rows(q, m, r2, e02, d02, e, d, da);
    else tri_rows(q, m, r2, e, d, e02, d02, da);
  }
}

void r_render(r_band_fn bg, r_band_fn overlay) {
  for (int band = 0; band < NBANDS; band++) {
    r_y0 = band * BAND_H, r_y1 = r_y0 + BAND_H;
    /* (cleared a word at a time: the C library's is a byte at a time) */
    for (uint32_t *p = (uint32_t *)(void *)zbuf, *e = p + sizeof zbuf / 4; p < e; p += 4) p[0] = p[1] = p[2] = p[3] = 0;
    if (bg) bg();
    else
      for (int i = 0; i < SCREEN_W * BAND_H; i++) r_col[i] = 0;
    for (const Poly *q = polys, *e = polys + npolys; q < e; q++)
      if (q->y1 > r_y0 && q->y0 < r_y1) {
        if (q->n == 3) raster_tri(q);
        else raster(q);
      }
    /* the see-through ones after the others, in the order they came */
    for (int i = 0; i < nalpha; i++) {
      const Poly *q = &polys[MAXP - 1 - i];
      if (q->y1 > r_y0 && q->y0 < r_y1) raster(q);
    }
    if (overlay) overlay();
    plat_push(0, r_y0, SCREEN_W, BAND_H, r_col);
  }
}
