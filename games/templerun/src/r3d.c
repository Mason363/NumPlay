/* The 3D renderer (r3d.h): polygons in a list, drawn band by band with a depth buffer. */
#include "r3d.h"

#define NEAR R_NEAR
#define FAR 190.0f
#define ZK 62000.0f      /* depths are ZK / z: 65263 at the near plane, larger is nearer */
#define MAXP 1800        /* polygons a frame */
#define MAXV 6000        /* their corners */
#define MAXT 760         /* textured faces in perspective */
#define MAXN 14          /* corners of a polygon once cut */
#define SUB 8.0f         /* corners are kept in 1/8 pixels */
#define GUARD 1900.0f    /* (so they may lie at most this far from the screen's middle) */

enum { K_FLAT, K_PERSP };

typedef struct {
  float a, b, c;         /* its depth at (sx, sy): a sx + b sy + c */
  uint16_t v0;           /* its first corner */
  uint16_t color;        /* RGB565, or the texture's number */
  uint16_t extra;        /* a textured face in perspective: its planes (uvs) */
  uint8_t n, kind;
  uint8_t y0, y1;        /* the rows it covers */
  uint8_t alpha;         /* see-through (255: not); textured faces: their light 0..32 */
  uint8_t near;          /* its nearest depth >> 8: the polygons are drawn nearest first */
} Poly;
typedef struct {
  int16_t x, y;
} Pos;
typedef struct {
  float ua, ub, uc, va, vb, vc;   /* u and v (in texels) times ZK / z, as planes like the depth */
} UV;

/* a corner while being cut: camera space, light, texture place */
typedef struct {
  float x, y, z, s, u, v;
} CV;

uint16_t *r_col;
int r_y0, r_y1;
Camera cam;
float r_fog0 = 19, r_fog1 = 76, r_far = 40;
int r_fade;
uint32_t r_fog_rgb = 0x325256;
/* the fog by depth: its amount (0..32) for depths d >> 3, below fog_d0 (nearer than r_fog0: none; farther than r_fog1,
 * below fog_d1: all) */
#define FOG_N 512
static uint8_t fogtab[FOG_N];
static uint16_t fog_d0, fog_d1, fog565;
static float fog_made0, fog_made1;

/* the frame's lists: in the RAM after the game's own (r_init) */
typedef struct {
  Poly polys[MAXP];
  uint16_t order[MAXP];   /* the see-through ones go last */
  Pos pos[MAXV];
  UV uvs[MAXT];
  uint16_t zbuf[SCREEN_W * BAND_H] __attribute__((aligned(8)));
} Work;
_Static_assert(SCREEN_W * BAND_H >= MAXP + 256, "the depth buffer holds the sort's lists");
static Work *work;
#define polys (work->polys)
#define order (work->order)
#define pos (work->pos)
#define uvs (work->uvs)
#define zbuf (work->zbuf)
static int npolys, nverts, nuv, nalpha;

void r_set_band_memory(uint16_t *band) { r_col = band; }
uint32_t r_work_size(void) { return sizeof(Work); }
void r_init(void *mem) { work = (Work *)mem; }

void r_sincos(float a, float *s, float *c) {
  a -= (float)(int)(a / 6.2831853f) * 6.2831853f;
  if (a > 3.14159265f) a -= 6.2831853f;
  if (a < -3.14159265f) a += 6.2831853f;
  float a2 = a * a;
  *s = a * (1 - a2 / 6 * (1 - a2 / 20 * (1 - a2 / 42 * (1 - a2 / 72 * (1 - a2 / 110)))));
  *c = 1 - a2 / 2 * (1 - a2 / 12 * (1 - a2 / 30 * (1 - a2 / 56 * (1 - a2 / 90))));
}

void r_camera(float x, float y, float z, float pitch, float f, float cx, float cy) {
  cam.x = x, cam.y = y, cam.z = z, cam.pitch = pitch, cam.f = f, cam.cx = cx, cam.cy = cy;
  r_sincos(pitch, &cam.sp, &cam.cp);
}

bool r_project(V3 p, float *sx, float *sy) {
  V3 v = r_view(p);
  if (v.z < NEAR) return false;
  float k = cam.f / v.z;
  *sx = cam.cx + v.x * k, *sy = cam.cy - v.y * k;
  return true;
}

#ifdef HOST
uint16_t r_heat[SCREEN_W * SCREEN_H];   /* (pixels tried at each place, this frame) */
#endif
void r_begin(void) {
  npolys = nverts = nuv = nalpha = 0;
#ifdef HOST
  for (int i = 0; i < SCREEN_W * SCREEN_H; i++) r_heat[i] = 0;
#endif
}
int r_count(void) { return npolys; }

uint32_t r_mix888(uint32_t a, uint32_t b, int t) {
  uint32_t rb = (a & 0xFF00FF) * (uint32_t)(256 - t) + (b & 0xFF00FF) * (uint32_t)t;
  uint32_t g = (a & 0x00FF00) * (uint32_t)(256 - t) + (b & 0x00FF00) * (uint32_t)t;
  return ((rb >> 8) & 0xFF00FF) | ((g >> 8) & 0x00FF00);
}

/* a bright cartoon light: tops full, the left a little darker, the right more, fronts in between */
uint32_t r_shade(uint32_t rgb, V3 n) {
  float s = 0.74f + 0.26f * n.y + 0.08f * -n.x - 0.13f * n.z;
  if (s > 1) s = 1;
  if (s < 0.3f) s = 0.3f;
  int k = (int)(s * 256);
  uint32_t r = ((rgb >> 16) & 255) * (uint32_t)k >> 8, g = ((rgb >> 8) & 255) * (uint32_t)k >> 8, b = (rgb & 255) * (uint32_t)k >> 8;
  return r << 16 | g << 8 | b;
}

/* ---------------------------------------------------------------- cutting */
/* keeps the part of the polygon where a x + b y + c z + d >= 0 */
static int cut(const CV *in, int n, CV *out, float a, float b, float c, float d) {
  int m = 0;
  for (int i = 0, j = n - 1; i < n; j = i++) {
    float di = a * in[i].x + b * in[i].y + c * in[i].z + d, dj = a * in[j].x + b * in[j].y + c * in[j].z + d;
    if ((di >= 0) != (dj >= 0) && m < MAXN) {
      float t = dj / (dj - di);
      const CV *p = &in[j], *q = &in[i];
      out[m++] = (CV){p->x + t * (q->x - p->x), p->y + t * (q->y - p->y), p->z + t * (q->z - p->z),
                      p->s + t * (q->s - p->s), p->u + t * (q->u - p->u), p->v + t * (q->v - p->v)};
    }
    if (di >= 0 && m < MAXN) out[m++] = in[i];
  }
  return m;
}

/* the plane through three screen points with values w: w(sx, sy) = a sx + b sy + c */
static bool fit(const float *x, const float *y, const float *w, float *a, float *b, float *c) {
  float x1 = x[1] - x[0], y1 = y[1] - y[0], x2 = x[2] - x[0], y2 = y[2] - y[0];
  float det = x1 * y2 - x2 * y1;
  if (det > -1e-3f && det < 1e-3f) return false;
  float w1 = w[1] - w[0], w2 = w[2] - w[0];
  *a = (w1 * y2 - w2 * y1) / det;
  *b = (x1 * w2 - x2 * w1) / det;
  *c = w[0] - *a * x[0] - *b * y[0];
  return true;
}

static void emit_screen(const CV *p, int n, float *sx, float *sy, int kind, uint16_t color, int flags, int alpha, uint16_t avg);

/* projects, checks and keeps a polygon in camera space */
static void emit(CV *p, int n, int kind, uint16_t color, int flags, int alpha, uint16_t avg) {
  if (npolys >= MAXP || nverts + MAXN > MAXV) return;
  CV b1[MAXN], b2[MAXN];
  int behind = 0, past = 0;
  for (int i = 0; i < n; i++) behind += p[i].z < NEAR, past += p[i].z > r_fog1;
  if (behind == n || past == n) return;   /* (wholly in the fog: its colour is the fog's, whatever is behind) */
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
  emit_screen(p, n, sx, sy, kind, color, flags, alpha, avg);
}

/* a plain face's colour gone r_fade into the fog's */
static uint16_t fade(uint16_t c) {
  uint32_t c8 = (uint32_t)(c >> 11) * 255 / 31 << 16 | (uint32_t)((c >> 5) & 63) * 255 / 63 << 8 | (uint32_t)(c & 31) * 255 / 31;
  return r_565(r_mix888(c8, r_fog_rgb, r_fade > 256 ? 256 : r_fade));
}

/* the rest of emit, from the corners on the screen (sx, sy from its middle) */
static void emit_screen(const CV *p, int n, float *sx, float *sy, int kind, uint16_t color, int flags, int alpha, uint16_t avg) {
  float f = cam.f;
  float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f, zsum = 0, zmin = 1e9f;
  for (int i = 0; i < n; i++) {
    sx[i] += cam.cx, sy[i] += cam.cy;
    if (sx[i] < x0) x0 = sx[i];
    if (sx[i] > x1) x1 = sx[i];
    if (sy[i] < y0) y0 = sy[i];
    if (sy[i] > y1) y1 = sy[i];
    zsum += p[i].z;
    if (p[i].z < zmin) zmin = p[i].z;
  }
  if (x1 < 0 || x0 >= SCREEN_W || y1 < 0 || y0 >= SCREEN_H) return;
  float area = 0;
  for (int i = 0, j = n - 1; i < n; j = i++) area += sx[j] * sy[i] - sx[i] * sy[j];
  if (area < 1.0f && !(flags & P_TWOSIDED && area < -1.0f)) return;   /* (turned away, or under half a pixel) */
  /* the depth plane, by Newell's method (good for any convex polygon): depth = ZK (n . (xs, ys, 1)) / d */
  V3 nm = {0, 0, 0};
  for (int i = 0, j = n - 1; i < n; j = i++) {
    nm.x += (p[j].y - p[i].y) * (p[j].z + p[i].z);
    nm.y += (p[j].z - p[i].z) * (p[j].x + p[i].x);
    nm.z += (p[j].x - p[i].x) * (p[j].y + p[i].y);
  }
  float d = nm.x * p[0].x + nm.y * p[0].y + nm.z * p[0].z;
  if (d > -1e-6f && d < 1e-6f) return;   /* seen edge-on */
  Poly *q = &polys[npolys];
  float s = ZK / d;
  q->a = s * nm.x / f;
  q->b = -s * nm.y / f;
  q->c = s * (nm.z - nm.x * cam.cx / f + nm.y * cam.cy / f);
  if (flags & P_DECAL) q->c += 3;
  if (flags & P_DECAL2) q->c += 6;
  if (flags & P_DECAL3) q->c += 9;
  float zm = zsum / (float)n, dn = ZK / zmin;
  q->near = (uint8_t)(dn >= 65535 ? 255 : (int)dn >> 8);
  q->kind = (uint8_t)kind;
  q->extra = 0;
  if (kind == K_PERSP) {
    /* far off, a picture is only its average colour (color already holds it when kind comes back flat) */
    const Texture *t = r_tex[color];
    if (t->clear && !avg && (nuv >= MAXT || zm > r_far * 1.6f)) return;   /* (a see-through one is only drawn as itself: near) */
    if ((!t->clear && (zm > r_fog1 * 0.6f || nuv >= MAXT)) || (avg && (zm > r_far || nuv >= MAXT || r_fade))) {
      /* (the palette's first useful colour stands for it: the tool puts the average there; or the face's own) */
      uint16_t c = avg ? avg : t->pal[t->clear ? 1 : 0];
      uint32_t a8 = (uint32_t)(c >> 11) * 255 / 31 << 16 | (uint32_t)((c >> 5) & 63) * 255 / 63 << 8 | (uint32_t)(c & 31) * 255 / 31;
      color = r_565(r_mix888(a8, 0, 256 - alpha * 8 > 256 ? 0 : 256 - alpha * 8));
      kind = K_FLAT;
      q->kind = K_FLAT;
      alpha = 255;
    } else {
      /* u and v over the depth, as planes: the three corners spanning the most */
      int best = 1;
      float ba = 0;
      for (int i = 1; i + 1 < n; i++) {
        float ar = (sx[i] - sx[0]) * (sy[i + 1] - sy[0]) - (sx[i + 1] - sx[0]) * (sy[i] - sy[0]);
        if (ar < 0) ar = -ar;
        if (ar > ba) ba = ar, best = i;
      }
      float px[3] = {sx[0], sx[best], sx[best + 1]}, py[3] = {sy[0], sy[best], sy[best + 1]};
      const CV *c3[3] = {&p[0], &p[best], &p[best + 1]};
      float wu[3], wv[3];
      for (int i = 0; i < 3; i++) wu[i] = c3[i]->u * ZK / c3[i]->z, wv[i] = c3[i]->v * ZK / c3[i]->z;
      UV *u = &uvs[nuv];
      if (!fit(px, py, wu, &u->ua, &u->ub, &u->uc) || !fit(px, py, wv, &u->va, &u->vb, &u->vc)) return;
      q->extra = (uint16_t)nuv++;
    }
  }
  if (kind == K_FLAT && r_fade) color = fade(color);
  q->color = color;
  q->alpha = (uint8_t)(kind == K_PERSP ? alpha : flags & P_ALPHA ? (alpha > 255 ? 255 : alpha < 0 ? 0 : alpha) : 255);
  q->v0 = (uint16_t)nverts, q->n = (uint8_t)n;
  for (int i = 0; i < n; i++) pos[nverts + i] = (Pos){(int16_t)(sx[i] * SUB + 0.5f), (int16_t)(sy[i] * SUB + 0.5f)};
  int iy0 = y0 < 0 ? 0 : (int)(y0 + 0.5f), iy1 = y1 >= SCREEN_H ? SCREEN_H : (int)(y1 + 0.5f);
  if (iy0 >= iy1) return;
  q->y0 = (uint8_t)iy0, q->y1 = (uint8_t)iy1;
  nverts += n;
  if (q->alpha != 255 && kind != K_PERSP) nalpha++;
  order[npolys] = (uint16_t)npolys;
  npolys++;
}

void r_poly(const V3 *w, int n, uint32_t rgb, int flags, int alpha) {
  if (n < 3 || n > MAXN - 4) return;
  V3 v[MAXN];
  for (int i = 0; i < n; i++) v[i] = r_view(w[i]);
  r_poly_view(v, n, rgb, flags, alpha);
}

void r_poly_view(const V3 *v, int n, uint32_t rgb, int flags, int alpha) {
  if (n < 3 || n > MAXN - 4) return;
  CV c[MAXN];
  int far = 0;
  for (int i = 0; i < n; i++) {
    c[i] = (CV){v[i].x, v[i].y, v[i].z, 0, 0, 0};
    far += v[i].z > FAR;
  }
  if (far == n) return;
  emit(c, n, K_FLAT, r_565(rgb), flags, alpha, 0);
}

void r_poly_tex(const V3 *w, const float *uv, int n, int tex, int light, int flags) { r_poly_tex_avg(w, uv, n, tex, light, flags, 0); }

void r_poly_tex_avg(const V3 *w, const float *uv, int n, int tex, int light, int flags, uint16_t avg) {
  if (n < 3 || n > MAXN - 4) return;
  V3 v[MAXN];
  for (int i = 0; i < n; i++) v[i] = r_view(w[i]);
  r_poly_tex_view(v, uv, n, tex, light, flags, avg);
}

void r_poly_tex_view(const V3 *v, const float *uv, int n, int tex, int light, int flags, uint16_t avg) {
  if (n < 3 || n > MAXN - 4) return;
  CV c[MAXN];
  int far = 0;
  for (int i = 0; i < n; i++) {
    c[i] = (CV){v[i].x, v[i].y, v[i].z, 0, uv[2 * i], uv[2 * i + 1]};
    far += v[i].z > FAR;
  }
  if (far == n) return;
  int l = light >> 3;
  emit(c, n, K_PERSP, (uint16_t)tex, flags, l > 32 ? 32 : l < 0 ? 0 : l, avg);
}

bool r_tri_tex_screen(const SV *A, const SV *B, const SV *C, const float *uv, int tex, int light, int flags, uint16_t avg) {
  if (A->z > FAR && B->z > FAR && C->z > FAR) return true;
  if (A->z > r_fog1 && B->z > r_fog1 && C->z > r_fog1) return true;   /* (as emit: wholly in the fog) */
  float sx[3] = {A->sx, B->sx, C->sx}, sy[3] = {A->sy, B->sy, C->sy};
  for (int i = 0; i < 3; i++)
    if (sx[i] > GUARD || sx[i] < -GUARD || sy[i] > GUARD || sy[i] < -GUARD) return false;
  /* (turned away or under half a pixel, as emit finds it: left out before anything else) */
  if ((sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0]) < 1.0f) return true;
  if (npolys >= MAXP || nverts + MAXN > MAXV) return true;
  CV p[3] = {{A->x, A->y, A->z, 0, uv[0], uv[1]}, {B->x, B->y, B->z, 0, uv[2], uv[3]}, {C->x, C->y, C->z, 0, uv[4], uv[5]}};
  int l = light >> 3;
  emit_screen(p, 3, sx, sy, K_PERSP, (uint16_t)tex, flags, l > 32 ? 32 : l < 0 ? 0 : l, avg);
  return true;
}

#if NP_SOFT_FLOAT
/* ---------------------------------------------------------------- a mesh's triangle in integers
 * (calculators without a floating-point unit: r_tri_tex_screen and emit_screen for a textured triangle, its corners in
 * 16.16 and the planes fitted through them with reciprocals; the planes are kept as floats, as emit_screen keeps them) */
#include "np_fixed.h"

void r_screen_fx(SVq *s) {
  int sh = __builtin_clz((uint32_t)s->z);
  int64_t r = fx_rcp((uint32_t)s->z << sh), f = fx_from_float(cam.f, 16);   /* r: 2^61 / (z 2^16 2^sh) */
  s->sx = fx_sat32(((int64_t)s->x * r >> (45 - sh)) * f >> 16), s->sy = fx_sat32(-(((int64_t)s->y * r >> (45 - sh)) * f >> 16));
  s->w = (int64_t)ZK * r >> (29 - sh);
}

/* emit_screen for a triangle in perspective, false when a number would not fit (emit_screen then) */
static bool emit_tri_fx(const SVq *const c[3], const int16_t *uv, int tex, int flags, int alpha, uint16_t avg) {
  int32_t sx[3], sy[3], cx = (int32_t)fx_from_float(cam.cx, 16), cy = (int32_t)fx_from_float(cam.cy, 16);
  int64_t zsum = 0, w[3], wmax = 0;
  int32_t x0 = INT32_MAX, x1 = INT32_MIN, y0 = INT32_MAX, y1 = INT32_MIN;
  for (int i = 0; i < 3; i++) {
    sx[i] = c[i]->sx + cx, sy[i] = c[i]->sy + cy, w[i] = c[i]->w;
    if (sx[i] < x0) x0 = sx[i];
    if (sx[i] > x1) x1 = sx[i];
    if (sy[i] < y0) y0 = sy[i];
    if (sy[i] > y1) y1 = sy[i];
    zsum += c[i]->z;
    if (w[i] > wmax) wmax = w[i];
  }
  if (x1 < 0 || x0 >= SCREEN_W << 16 || y1 < 0 || y0 >= SCREEN_H << 16) return true;
  int64_t ax = sx[1] - sx[0], ay = sy[1] - sy[0], bx = sx[2] - sx[0], by = sy[2] - sy[0], det = ax * by - bx * ay;
  /* the depth plane through the corners: depth = a x + b y + c */
  int64_t a = fx_div((w[1] - w[0]) * by - (w[2] - w[0]) * ay, det, 16), b = fx_div(ax * (w[2] - w[0]) - bx * (w[1] - w[0]), det, 16);
  const int64_t big = (int64_t)1 << 34;
  if (a > big || a < -big || b > big || b < -big) return false;
  Poly *q = &polys[npolys];
  int64_t pc = w[0] - (a * sx[0] >> 16) - (b * sy[0] >> 16);
  if (flags & P_DECAL) pc += 3 << 16;
  if (flags & P_DECAL2) pc += 6 << 16;
  if (flags & P_DECAL3) pc += 9 << 16;
  int kind = K_PERSP;
  uint16_t color = (uint16_t)tex;
  const Texture *t = r_tex[tex];
  /* (zm: the corners' mean depth, as thrice it) */
  int64_t far16 = fx_from_float(r_far, 16), fog16 = fx_from_float(r_fog1, 16);
  if (t->clear && !avg && (nuv >= MAXT || zsum * 5 > far16 * 24)) return true;
  UV *u = &uvs[nuv];
  if ((!t->clear && (zsum * 5 > fog16 * 9 || nuv >= MAXT)) || (avg && (zsum > far16 * 3 || nuv >= MAXT || r_fade))) {
    uint16_t c0 = avg ? avg : t->pal[t->clear ? 1 : 0];
    uint32_t a8 = (uint32_t)(c0 >> 11) * 255 / 31 << 16 | (uint32_t)((c0 >> 5) & 63) * 255 / 63 << 8 | (uint32_t)(c0 & 31) * 255 / 31;
    color = r_565(r_mix888(a8, 0, 256 - alpha * 8 > 256 ? 0 : 256 - alpha * 8));
    if (r_fade) color = fade(color);
    kind = K_FLAT;
    alpha = 255;
  } else {
    /* u and v over the depth, as planes (4 u w and 4 v w, the places being in quarter texels) */
    int64_t pa[2], pb[2], pk[2];
    for (int k = 0; k < 2; k++) {
      int64_t t0 = (int64_t)uv[k] * w[0] >> 16, t1 = (int64_t)uv[2 + k] * w[1] >> 16, t2 = (int64_t)uv[4 + k] * w[2] >> 16;
      pa[k] = fx_div((t1 - t0) * by - (t2 - t0) * ay, det, 30), pb[k] = fx_div(ax * (t2 - t0) - bx * (t1 - t0), det, 30);
      if (pa[k] > big || pa[k] < -big || pb[k] > big || pb[k] < -big) return false;
      pk[k] = (t0 << 14) - (pa[k] * sx[0] >> 16) - (pb[k] * sy[0] >> 16);
    }
    u->ua = fx_to_float(pa[0], 16), u->ub = fx_to_float(pb[0], 16), u->uc = fx_to_float(pk[0], 16);
    u->va = fx_to_float(pa[1], 16), u->vb = fx_to_float(pb[1], 16), u->vc = fx_to_float(pk[1], 16);
  }
  int iy0 = y0 < 0 ? 0 : (y0 + 32768) >> 16, iy1 = y1 >= SCREEN_H << 16 ? SCREEN_H : (y1 + 32768) >> 16;
  if (iy0 >= iy1) return true;
  q->a = fx_to_float(a, 16), q->b = fx_to_float(b, 16), q->c = fx_to_float(pc, 16);
  q->near = (uint8_t)(wmax >= (int64_t)65535 << 16 ? 255 : (int)(wmax >> 24));
  q->kind = (uint8_t)kind;
  q->extra = kind == K_PERSP ? (uint16_t)nuv++ : 0;
  q->color = color;
  q->alpha = (uint8_t)alpha;
  q->v0 = (uint16_t)nverts, q->n = 3;
  for (int i = 0; i < 3; i++) {
    int32_t px = sx[i] * 8 + 32768, py = sy[i] * 8 + 32768;   /* (as (int16_t)(x * SUB + 0.5f)) */
    pos[nverts + i] = (Pos){(int16_t)(px < 0 ? -(-px >> 16) : px >> 16), (int16_t)(py < 0 ? -(-py >> 16) : py >> 16)};
  }
  q->y0 = (uint8_t)iy0, q->y1 = (uint8_t)iy1;
  nverts += 3;
  order[npolys] = (uint16_t)npolys;
  npolys++;
  return true;
}

bool r_tri_tex_screen_fx(const SVq *A, const SVq *B, const SVq *C, const int16_t *uv, int tex, int light, int flags,
                         uint16_t avg) {
  const int32_t far = (int32_t)(FAR * 65536), fog = (int32_t)fx_from_float(r_fog1, 16), g = (int32_t)(GUARD * 65536);
  if (A->z > far && B->z > far && C->z > far) return true;
  if (A->z > fog && B->z > fog && C->z > fog) return true;
  const SVq *c[3] = {A, B, C};
  for (int i = 0; i < 3; i++)
    if (c[i]->sx > g || c[i]->sx < -g || c[i]->sy > g || c[i]->sy < -g) return false;
  if ((int64_t)(B->sx - A->sx) * (C->sy - A->sy) - (int64_t)(C->sx - A->sx) * (B->sy - A->sy) < (int64_t)1 << 32) return true;
  if (npolys >= MAXP || nverts + MAXN > MAXV) return true;
  int l = light >> 3;
  if (emit_tri_fx(c, uv, tex, flags, l > 32 ? 32 : l < 0 ? 0 : l, avg)) return true;
  /* (numbers too big: as r_tri_tex_screen) */
  float u[6];
  for (int i = 0; i < 6; i++) u[i] = uv[i] * 0.25f;
  SV s[3];
  for (int i = 0; i < 3; i++)
    s[i] = (SV){fx_to_float(c[i]->x, 16), fx_to_float(c[i]->y, 16), fx_to_float(c[i]->z, 16), fx_to_float(c[i]->sx, 16),
                fx_to_float(c[i]->sy, 16)};
  return r_tri_tex_screen(&s[0], &s[1], &s[2], u, tex, light, flags, avg);
}
#endif

static V3 add3(V3 a, V3 b, V3 c, V3 d) { return (V3){a.x + b.x + c.x + d.x, a.y + b.y + c.y + d.y, a.z + b.z + c.z + d.z}; }
static V3 neg(V3 a) { return (V3){-a.x, -a.y, -a.z}; }
static V3 unit(V3 a) {
  float l = a.x * a.x + a.y * a.y + a.z * a.z;
  if (l <= 0) return a;
  float r = 1.0f / __builtin_sqrtf(l);
  return (V3){a.x * r, a.y * r, a.z * r};
}

void r_box(V3 c, V3 ax, V3 ay, V3 az, const uint32_t *rgb6, int skip) {
  V3 nx = neg(ax), ny = neg(ay), nz = neg(az);
  /* each face seen from outside, clockwise: its four corners as signs along x, y, z */
  static const int8_t faces[6][4][3] = {
    {{-1, 1, 1}, {1, 1, 1}, {1, 1, -1}, {-1, 1, -1}},        /* top */
    {{1, -1, 1}, {-1, -1, 1}, {-1, -1, -1}, {1, -1, -1}},    /* bottom */
    {{-1, 1, 1}, {-1, 1, -1}, {-1, -1, -1}, {-1, -1, 1}},    /* -x */
    {{1, 1, -1}, {1, 1, 1}, {1, -1, 1}, {1, -1, -1}},        /* +x */
    {{-1, 1, -1}, {1, 1, -1}, {1, -1, -1}, {-1, -1, -1}},    /* front, -z */
    {{1, 1, 1}, {-1, 1, 1}, {-1, -1, 1}, {1, -1, 1}},        /* back, +z */
  };
  const V3 normals[6] = {ay, ny, nx, ax, nz, az};
  for (int f = 0; f < 6; f++) {
    if (skip & 1 << f) continue;
    /* only the faces turned towards the camera */
    V3 nf = normals[f], fc = (V3){c.x + nf.x, c.y + nf.y, c.z + nf.z};
    if ((fc.x - cam.x) * nf.x + (fc.y - cam.y) * nf.y + (fc.z - cam.z) * nf.z >= 0) continue;
    V3 v[4];
    for (int k = 0; k < 4; k++) {
      const int8_t *s = faces[f][k];
      V3 x = s[0] > 0 ? ax : nx, y = s[1] > 0 ? ay : ny, z = s[2] > 0 ? az : nz;
      v[k] = add3(c, x, y, z);
    }
    r_poly(v, 4, r_shade(rgb6[f], unit(nf)), 0, 0);
  }
}

void r_cube(float x0, float y0, float z0, float x1, float y1, float z1, uint32_t rgb, int skip) {
  float hx = (x1 - x0) / 2, hy = (y1 - y0) / 2, hz = (z1 - z0) / 2;
  uint32_t c[6] = {rgb, rgb, rgb, rgb, rgb, rgb};
  r_box((V3){x0 + hx, y0 + hy, z0 + hz}, (V3){hx, 0, 0}, (V3){0, hy, 0}, (V3){0, 0, hz}, c, skip);
}

/* ---------------------------------------------------------------- drawing a band */
/* an edge crossing the band: where it starts, its slope, and its light and texture place likewise */
typedef struct {
  float y0, y1, x, dx;
} Edge;

/* texels in 16.16 (kept within reach: they repeat anyway) */
static inline int32_t fix16(float t) {
  if (t > 16000) t = 16000;
  if (t < -16000) t = -16000;
  return (int32_t)(t * 65536.0f);
}

#ifdef HOST
uint32_t r_stat_texhole[64];
int r_show_holes;
uint32_t r_stat[4], r_stat_tex[64], r_stat_rows, r_stat_polys, r_stat_skips, r_stat_chunks, r_stat_written, r_stat_holes;   /* (pixels tried, by kind and texture: for measuring) */
#endif

/* a picture's palette with each colour spread as r_mix565 spreads it (two words' worth of fields), made the first
 * time it is lit: a lit texel is then a multiply and a fold */
static const Texture *spread_of[4];
static uint32_t spread_pal[4][256];
static const uint32_t *spread(const Texture *t) {
  for (int i = 0; i < 4; i++) {
    if (spread_of[i] == t) return spread_pal[i];
    if (!spread_of[i]) {
      for (int k = 0; k < 256; k++) spread_pal[i][k] = (t->pal[k] | (uint32_t)t->pal[k] << 16) & 0x07E0F81F;
      spread_of[i] = t;
      return spread_pal[i];
    }
  }
  return NULL;
}

/* pixels [zp, ze) of a textured run (their colours at cp): where it lies in front, its texel, see-through where the
 * picture is, lit by light (32: as it is; sp: the picture's spread palette, when lit). WL: the picture's side, log2, when
 * it is known here (0: the picture's own). Returns the depth after the run */
__attribute__((always_inline)) static inline uint32_t tex_run(uint16_t *zp, const uint16_t *ze, uint16_t *cp, uint32_t z,
                                                              uint32_t da, uint32_t uu, uint32_t du, uint32_t vv, uint32_t dv,
                                                              const Texture *tx, int light, const uint32_t *sp, int WL) {
  const uint8_t *px = tx->px;
  const uint16_t *pal = tx->pal;
  bool clear = tx->clear;
  /* the texel: v's row times the width, or u's column */
  uint32_t wl = WL ? (uint32_t)WL : tx->wl, wm = (1u << wl) - 1, hm = WL ? wm : (1u << tx->hl) - 1;
#define TEXEL ((((vv >> 16) & hm) << wl) | ((uu >> 16) & wm))
  if (light >= 32 && !clear) {
    for (; zp < ze; zp++, cp++, z += da, uu += du, vv += dv) {
      uint16_t d = (uint16_t)(z >> 16);
      if (d > *zp) *zp = d, *cp = pal[px[TEXEL]];
    }
  } else if (light >= 32) {
    for (; zp < ze; zp++, cp++, z += da, uu += du, vv += dv) {
      uint16_t d = (uint16_t)(z >> 16);
      if (d <= *zp) continue;
      uint8_t t = px[TEXEL];
      if (t) *zp = d, *cp = pal[t];
    }
  } else if (sp && !clear) {
    for (; zp < ze; zp++, cp++, z += da, uu += du, vv += dv) {
      uint16_t d = (uint16_t)(z >> 16);
      if (d <= *zp) continue;
      uint32_t x = (sp[px[TEXEL]] * (uint32_t)light >> 5) & 0x07E0F81F;   /* (r_mix565(0, pal[t], light)) */
      *zp = d, *cp = (uint16_t)(x | x >> 16);
    }
  } else if (sp) {
    for (; zp < ze; zp++, cp++, z += da, uu += du, vv += dv) {
      uint16_t d = (uint16_t)(z >> 16);
      if (d <= *zp) continue;
      uint8_t t = px[TEXEL];
      if (!t) continue;
      uint32_t x = (sp[t] * (uint32_t)light >> 5) & 0x07E0F81F;
      *zp = d, *cp = (uint16_t)(x | x >> 16);
    }
  } else {
    for (; zp < ze; zp++, cp++, z += da, uu += du, vv += dv) {
      uint16_t d = (uint16_t)(z >> 16);
      if (d <= *zp) continue;
      uint8_t t = px[TEXEL];
      if (clear && !t) continue;
      *zp = d, *cp = r_mix565(0, pal[t], light);
    }
  }
#undef TEXEL
  return z;
}

/* (each its own function: the run's loop has the registers to itself) */
#define TEX_RUN(name, WL)                                                                                              \
  __attribute__((noinline)) static uint32_t name(uint16_t *zp, const uint16_t *ze, uint16_t *cp, uint32_t z, uint32_t da, \
                                                 uint32_t uu, uint32_t du, uint32_t vv, uint32_t dv, const Texture *tx,  \
                                                 int light, const uint32_t *sp) {                                         \
    return tex_run(zp, ze, cp, z, da, uu, du, vv, dv, tx, light, sp, WL);                                               \
  }
TEX_RUN(tex_run9, 9)
TEX_RUN(tex_run8, 8)
TEX_RUN(tex_run0, 0)

/* a row of a polygon, pixels [x0, x1) of row y (its middle yc), between the edges L (at xl) and R (at xr) */
__attribute__((noinline)) static void raster_row(const Poly *q, int kind, int y, float yc, int x0, int x1,
                                                            const Texture *tx, int32_t da, int alpha, uint16_t col) {
  float zf = q->a * ((float)x0 + 0.5f) + q->b * yc + q->c;
  if (zf < 0) zf = 0;
  if (zf > 65535) zf = 65535;
  uint32_t z = (uint32_t)(zf * 65536.0f);
  uint16_t *zb = zbuf + (y - r_y0) * SCREEN_W, *cb = r_col + (y - r_y0) * SCREEN_W;
#ifdef HOST
  r_stat_rows++;
  r_stat[kind] += (uint32_t)(x1 - x0);
  if (kind == K_PERSP) r_stat_tex[q->color] += (uint32_t)(x1 - x0);
  for (int x = x0; x < x1; x++) r_heat[y * SCREEN_W + x]++;
#endif
  if (kind == K_FLAT) {
    if (alpha >= 31) {
      for (int x = x0; x < x1; x++, z += (uint32_t)da) {
        uint16_t zz = (uint16_t)(z >> 16);
        if (zz > zb[x]) zb[x] = zz, cb[x] = col;
      }
    } else {
      for (int x = x0; x < x1; x++, z += (uint32_t)da)
        if ((uint16_t)(z >> 16) > zb[x]) cb[x] = r_mix565(cb[x], col, alpha);
    }
    return;
  }
  if (kind == K_PERSP) {
    const UV *w = &uvs[q->extra];
    int light = q->alpha;
    const uint32_t *sp = light < 32 ? spread(tx) : NULL;
    /* u and v exactly every 16 pixels, straight lines between; a run of 16 all behind what is drawn is passed
     * over without its texture places */
    float fx = (float)x0 + 0.5f;
    float qz = q->a * fx + q->b * yc + q->c, qu = w->ua * fx + w->ub * yc + w->uc, qv = w->va * fx + w->vb * yc + w->vc;
    int32_t u = 0, v = 0;
    bool known = false;   /* u and v are those at x */
    for (int x = x0; x < x1;) {
      int n = x1 - x < 16 ? x1 - x : 16;
      uint16_t *zp = zb + x, *cp = cb + x, *ze = zp + n;
      float fn = (float)n;
      float qz2 = qz + q->a * fn, qu2 = qu + w->ua * fn, qv2 = qv + w->va * fn;
      /* (any pixel in front?) */
      uint32_t zt = z;
      uint16_t *zq = zp;
      while (zq < ze && (uint16_t)(zt >> 16) <= *zq) zq++, zt += (uint32_t)da;
#ifdef HOST
      r_stat_chunks++;
#endif
      if (zq == ze) {
#ifdef HOST
        r_stat_skips++;
#endif
        z = zt, x += n;
        qz = qz2, qu = qu2, qv = qv2;
        known = false;
        continue;
      }
      if (!known) {
        float iz = qz > 1 ? 1 / qz : 1;
        u = fix16(qu * iz), v = fix16(qv * iz);
      }
      float iz2 = qz2 > 1 ? 1 / qz2 : 1;
      int32_t u2 = fix16(qu2 * iz2), v2 = fix16(qv2 * iz2);
      uint32_t du, dv;
      if (n == 16) du = (uint32_t)((u2 - u) >> 4), dv = (uint32_t)((v2 - v) >> 4);
      else du = (uint32_t)((u2 - u) / n), dv = (uint32_t)((v2 - v) / n);
      uint32_t uu = (uint32_t)u, vv = (uint32_t)v;
      /* (the game's pictures are square, 512 or 256 across: their loops with the sizes known, short of registers) */
      if (tx->wl == 9 && tx->hl == 9) z = tex_run9(zp, ze, cp, z, (uint32_t)da, uu, du, vv, dv, tx, light, sp);
      else if (tx->wl == 8 && tx->hl == 8) z = tex_run8(zp, ze, cp, z, (uint32_t)da, uu, du, vv, dv, tx, light, sp);
      else z = tex_run0(zp, ze, cp, z, (uint32_t)da, uu, du, vv, dv, tx, light, sp);
      x += n;
      qz = qz2, qu = qu2, qv = qv2, u = u2, v = v2;
      known = true;
    }
    return;
  }
}

static void raster(const Poly *q) {
  int ya = q->y0 > r_y0 ? q->y0 : r_y0, yb = q->y1 < r_y1 ? q->y1 : r_y1;
  if (ya >= yb) return;
  const Pos *pp = pos + q->v0;
  int n = q->n;
  /* the edges of each side (a convex polygon has a left side and a right side from its top to its bottom; which
   * way it turns tells which edges go down which side), each side's from the top */
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
    Edge *E = &e[side][ne[side]];
    /* (kept in order from the top) */
    int k = ne[side]++;
    while (k > 0 && e[side][k - 1].y0 > ay) e[side][k] = e[side][k - 1], k--;
    E = &e[side][k];
    float h = 1 / (by - ay), xa = pp[ka].x * (1 / SUB), xb = pp[kb].x * (1 / SUB);
    E->y0 = ay, E->y1 = by;
    E->dx = (xb - xa) * h, E->x = xa - ay * E->dx;
  }
  if (!ne[0] || !ne[1]) return;
#ifdef HOST
  r_stat_polys++;
#endif
  uint16_t col = q->color;
  int kind = q->kind;
  const Texture *tx = kind == K_PERSP ? r_tex[q->color] : NULL;
  int alpha = q->alpha >> 3;
  float qa = q->a > 30000 ? 30000 : q->a < -30000 ? -30000 : q->a;   /* (seen nearly edge-on) */
  int32_t da = (int32_t)(qa * 65536.0f);
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
      const Edge *T = L;
      L = R, R = T;
    }
    int x0 = xl <= -1 ? 0 : (int)(xl + 0.5f), x1 = xr >= SCREEN_W ? SCREEN_W : (int)(xr + 0.5f);
    if (x0 < 0) x0 = 0;
    if (x0 >= x1) continue;
    raster_row(q, kind, y, yc, x0, x1, tx, da, alpha, col);
  }
}

/* one half of a triangle: rows [y, ye) between the edges x = lx + yc ldx and x = rx + yc rdx, as raster draws them */
static inline __attribute__((always_inline)) void tri_rows(const Poly *q, int kind, int y, int ye, float lx, float ldx, float rx,
                                                          float rdx, const Texture *tx, int32_t da, int alpha, uint16_t col) {
  for (; y < ye; y++) {
    float yc = (float)y + 0.5f;
    float xl = lx + yc * ldx, xr = rx + yc * rdx;
    if (xl > xr) {
      float t = xl;
      xl = xr, xr = t;
    }
    int x0 = xl <= -1 ? 0 : (int)(xl + 0.5f), x1 = xr >= SCREEN_W ? SCREEN_W : (int)(xr + 0.5f);
    if (x0 < 0) x0 = 0;
    if (x0 >= x1) continue;
    raster_row(q, kind, y, yc, x0, x1, tx, da, alpha, col);
  }
}

/* a flat or a textured triangle (nearly all of them): drawn as raster draws it, with less to find out first: its
 * corners from the top, the long edge on one side and the two others in turn on the other */
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
  float h02 = 1 / (y2 - y0), d02 = (x2 - x0) * h02, e02 = x0 - y0 * d02;
  int kind = q->kind;
  const Texture *tx = kind == K_PERSP ? r_tex[q->color] : NULL;
  float qa = q->a > 30000 ? 30000 : q->a < -30000 ? -30000 : q->a;   /* (seen nearly edge-on) */
  int32_t da = (int32_t)(qa * 65536.0f);
  int alpha = q->alpha >> 3;
  uint16_t col = q->color;
  /* rows whose middles lie in [ys, ye): from ceil(ys - 0.5) */
  int r0 = (int)__builtin_ceilf(y0 - 0.5f), r1 = (int)__builtin_ceilf(y1 - 0.5f), r2 = (int)__builtin_ceilf(y2 - 0.5f);
  if (r0 < ya) r0 = ya;
  if (r2 > yb) r2 = yb;
  int m = r1 < r0 ? r0 : r1 > r2 ? r2 : r1;
  if (p0->y != p1->y && r0 < m) {
    float h = 1 / (y1 - y0), d = (x1 - x0) * h, e = x0 - y0 * d;
    tri_rows(q, kind, r0, m, e02, d02, e, d, tx, da, alpha, col);
  }
  if (p1->y != p2->y && m < r2) {
    float h = 1 / (y2 - y1), d = (x2 - x1) * h, e = x1 - y1 * d;
    tri_rows(q, kind, m, r2, e02, d02, e, d, tx, da, alpha, col);
  }
}

#if NP_SOFT_FLOAT
/* ---------------------------------------------------------------- the same in integers
 * (calculators without a floating-point unit: per polygon a few conversions, then no float at all in its rows) */

/* the depth and texture planes at pixel (x, y) (its middle), 16.16 */
typedef struct {
  int64_t za, zb, zc, ua, ub, uc, va, vb, vc;
} Planes;
static void planes(const Poly *q, Planes *P) {
  P->za = fx_from_float(q->a, 16), P->zb = fx_from_float(q->b, 16);
  P->zc = fx_from_float(q->c, 16) + ((P->za + P->zb) >> 1);
  if (q->kind == K_PERSP) {
    const UV *w = &uvs[q->extra];
    P->ua = fx_from_float(w->ua, 16), P->ub = fx_from_float(w->ub, 16);
    P->uc = fx_from_float(w->uc, 16) + ((P->ua + P->ub) >> 1);
    P->va = fx_from_float(w->va, 16), P->vb = fx_from_float(w->vb, 16);
    P->vc = fx_from_float(w->vc, 16) + ((P->va + P->vb) >> 1);
  }
}

/* a texel place over the depth (16.16) as fix16 makes it: within 16000 texels */
static inline int32_t tex16(int64_t t) {
  return t > (int64_t)16000 << 16 ? 16000 << 16 : t < -((int64_t)16000 << 16) ? -(16000 << 16) : (int32_t)t;
}
/* u = qu / qz and v = qv / qz, as raster_row finds them (qz under 1 counts as 1) */
static inline void persp(int64_t qz, int64_t qu, int64_t qv, int32_t *u, int32_t *v) {
  if (qz <= 65536) {
    *u = tex16(qu), *v = tex16(qv);
    return;
  }
  uint32_t d = qz >= (int64_t)1 << 32 ? 0xFFFFFFFFu : (uint32_t)qz;
  int s = __builtin_clz(d);
  int32_t r = fx_rcp(d << s);   /* 2^61 / (d << s) */
  *u = tex16((int64_t)fx_sat32(qu >> 16) * r >> (29 - s));
  *v = tex16((int64_t)fx_sat32(qv >> 16) * r >> (29 - s));
}

/* raster_row with the planes */
__attribute__((noinline)) static void raster_row_fx(const Poly *q, const Planes *P, int kind, int y, int x0, int x1,
                                                    const Texture *tx, int32_t da, int alpha, uint16_t col) {
  int64_t qz = P->zc + P->zb * y + P->za * x0;
  uint32_t z = qz < 0 ? 0 : qz > (int64_t)65535 << 16 ? (uint32_t)65535 << 16 : (uint32_t)qz;
  uint16_t *zb = zbuf + (y - r_y0) * SCREEN_W, *cb = r_col + (y - r_y0) * SCREEN_W;
  if (kind == K_FLAT) {
    if (alpha >= 31) {
      for (int x = x0; x < x1; x++, z += (uint32_t)da) {
        uint16_t zz = (uint16_t)(z >> 16);
        if (zz > zb[x]) zb[x] = zz, cb[x] = col;
      }
    } else {
      for (int x = x0; x < x1; x++, z += (uint32_t)da)
        if ((uint16_t)(z >> 16) > zb[x]) cb[x] = r_mix565(cb[x], col, alpha);
    }
    return;
  }
  if (kind == K_PERSP) {
    int light = q->alpha;
    const uint32_t *sp = light < 32 ? spread(tx) : NULL;
    int64_t qu = P->uc + P->ub * y + P->ua * x0, qv = P->vc + P->vb * y + P->va * x0;
    int32_t u = 0, v = 0;
    bool known = false;
    for (int x = x0; x < x1;) {
      int n = x1 - x < 16 ? x1 - x : 16;
      uint16_t *zp = zb + x, *cp = cb + x, *ze = zp + n;
      int64_t qz2, qu2, qv2;
      if (n == 16) qz2 = qz + (P->za << 4), qu2 = qu + (P->ua << 4), qv2 = qv + (P->va << 4);
      else qz2 = qz + P->za * n, qu2 = qu + P->ua * n, qv2 = qv + P->va * n;
      uint32_t zt = z;
      uint16_t *zq = zp;
      while (zq < ze && (uint16_t)(zt >> 16) <= *zq) zq++, zt += (uint32_t)da;
      if (zq == ze) {
        z = zt, x += n;
        qz = qz2, qu = qu2, qv = qv2;
        known = false;
        continue;
      }
      if (!known) persp(qz, qu, qv, &u, &v);
      int32_t u2, v2;
      persp(qz2, qu2, qv2, &u2, &v2);
      uint32_t du, dv;
      if (n == 16) du = (uint32_t)((u2 - u) >> 4), dv = (uint32_t)((v2 - v) >> 4);
      else {
        /* (over n without a division: 2^32 / n, rounded up) */
        static const int64_t over[16] = {0, 4294967296, 2147483648, 1431655766, 1073741824, 858993460, 715827883, 613566757,
                                        536870912, 477218589, 429496730, 390451573, 357913942, 330382100, 306783379, 286331154};
        int64_t k = over[n];
        du = (uint32_t)(int32_t)((u2 - u) * k >> 32), dv = (uint32_t)(int32_t)((v2 - v) * k >> 32);
      }
      uint32_t uu = (uint32_t)u, vv = (uint32_t)v;
      if (tx->wl == 9 && tx->hl == 9) z = tex_run9(zp, ze, cp, z, (uint32_t)da, uu, du, vv, dv, tx, light, sp);
      else if (tx->wl == 8 && tx->hl == 8) z = tex_run8(zp, ze, cp, z, (uint32_t)da, uu, du, vv, dv, tx, light, sp);
      else z = tex_run0(zp, ze, cp, z, (uint32_t)da, uu, du, vv, dv, tx, light, sp);
      x += n;
      qz = qz2, qu = qu2, qv = qv2, u = u2, v = v2;
      known = true;
    }
  }
}

/* an edge from (xa, ya) down to (xb, yb), in 1/8 pixels: its x at a row's middle Y, 1/8 pixels with 12 more bits */
typedef struct {
  int32_t ya, yb, xa, dx, r, sh;
} EdgeI;
static void edge_i(EdgeI *E, int xa, int ya, int xb, int yb) {
  uint32_t d = (uint32_t)(yb - ya);
  int s = __builtin_clz(d);
  E->ya = ya, E->yb = yb, E->xa = xa, E->dx = xb - xa, E->r = fx_rcp(d << s), E->sh = 49 - s;
}
static inline int32_t edge_x(const EdgeI *E, int32_t Y) {
  return (E->xa << 12) + (int32_t)((int64_t)((Y - E->ya) * E->dx) * E->r >> E->sh);
}
/* the pixels [x0, x1) between two edges' places, as raster rounds them; false when none */
static inline bool span_x(int32_t xl, int32_t xr, int *x0, int *x1) {
  if (xl > xr) {
    int32_t t = xl;
    xl = xr, xr = t;
  }
  int a = xl <= -(8 << 12) ? 0 : (xl + (4 << 12)) >> 15, b = xr >= SCREEN_W * 8 << 12 ? SCREEN_W : (xr + (4 << 12)) >> 15;
  if (a < 0) a = 0;
  *x0 = a, *x1 = b;
  return a < b;
}
static inline int32_t clamp_da(int64_t za) {
  return za > (int64_t)30000 << 16 ? 30000 << 16 : za < -((int64_t)30000 << 16) ? -(30000 << 16) : (int32_t)za;
}

static void raster_fx(const Poly *q) {
  int ya = q->y0 > r_y0 ? q->y0 : r_y0, yb = q->y1 < r_y1 ? q->y1 : r_y1;
  if (ya >= yb) return;
  const Pos *pp = pos + q->v0;
  int n = q->n;
  int64_t area = 0;
  for (int i = 0, j = n - 1; i < n; j = i++) area += (int64_t)pp[j].x * pp[i].y - (int64_t)pp[i].x * pp[j].y;
  EdgeI e[2][MAXN];
  int ne[2] = {0, 0};
  for (int i = 0, j = n - 1; i < n; j = i++) {
    int ay = pp[j].y, by = pp[i].y;
    if (ay == by) continue;
    int ka = j, kb = i;
    bool down = by > ay;
    if (!down) {
      int t = ay;
      ay = by, by = t;
      ka = i, kb = j;
    }
    if (by <= ya * 8 || ay >= yb * 8) continue;
    int side = (down == (area > 0)) ? 1 : 0;
    int k = ne[side]++;
    while (k > 0 && e[side][k - 1].ya > ay) e[side][k] = e[side][k - 1], k--;
    edge_i(&e[side][k], pp[ka].x, ay, pp[kb].x, by);
  }
  if (!ne[0] || !ne[1]) return;
  Planes P;
  planes(q, &P);
  uint16_t col = q->color;
  int kind = q->kind;
  const Texture *tx = kind == K_PERSP ? r_tex[q->color] : NULL;
  int alpha = q->alpha >> 3;
  int32_t da = clamp_da(P.za);
  int li = 0, ri = 0;
  for (int y = ya; y < yb; y++) {
    int32_t Y = y * 8 + 4;
    while (li < ne[0] - 1 && Y >= e[0][li].yb) li++;
    while (ri < ne[1] - 1 && Y >= e[1][ri].yb) ri++;
    const EdgeI *L = &e[0][li], *R = &e[1][ri];
    if (Y < L->ya || Y >= L->yb || Y < R->ya || Y >= R->yb) continue;
    int x0, x1;
    if (span_x(edge_x(L, Y), edge_x(R, Y), &x0, &x1)) raster_row_fx(q, &P, kind, y, x0, x1, tx, da, alpha, col);
  }
}

__attribute__((noinline)) static void raster_tri_fx(const Poly *q) {
  int ya = q->y0 > r_y0 ? q->y0 : r_y0, yb = q->y1 < r_y1 ? q->y1 : r_y1;
  if (ya >= yb) return;
  const Pos *p0 = pos + q->v0, *p1 = p0 + 1, *p2 = p0 + 2, *t;
  if (p1->y < p0->y) t = p0, p0 = p1, p1 = t;
  if (p2->y < p1->y) t = p1, p1 = p2, p2 = t;
  if (p1->y < p0->y) t = p0, p0 = p1, p1 = t;
  if (p0->y == p2->y) return;
  EdgeI e02, e;
  edge_i(&e02, p0->x, p0->y, p2->x, p2->y);
  Planes P;
  planes(q, &P);
  int kind = q->kind;
  const Texture *tx = kind == K_PERSP ? r_tex[q->color] : NULL;
  int32_t da = clamp_da(P.za);
  int alpha = q->alpha >> 3;
  uint16_t col = q->color;
  /* rows whose middles lie in [ys, ye): from ceil(ys - 0.5) */
  int r0 = (p0->y + 3) >> 3, r1 = (p1->y + 3) >> 3, r2 = (p2->y + 3) >> 3;
  if (r0 < ya) r0 = ya;
  if (r2 > yb) r2 = yb;
  int m = r1 < r0 ? r0 : r1 > r2 ? r2 : r1;
  int x0, x1;
  if (p0->y != p1->y && r0 < m) {
    edge_i(&e, p0->x, p0->y, p1->x, p1->y);
    for (int y = r0; y < m; y++)
      if (span_x(edge_x(&e02, y * 8 + 4), edge_x(&e, y * 8 + 4), &x0, &x1))
        raster_row_fx(q, &P, kind, y, x0, x1, tx, da, alpha, col);
  }
  if (p1->y != p2->y && m < r2) {
    edge_i(&e, p1->x, p1->y, p2->x, p2->y);
    for (int y = m; y < r2; y++)
      if (span_x(edge_x(&e02, y * 8 + 4), edge_x(&e, y * 8 + 4), &x0, &x1))
        raster_row_fx(q, &P, kind, y, x0, x1, tx, da, alpha, col);
  }
}
#endif

/* the fog's table for the current r_fog0, r_fog1 and r_fog_rgb */
static void fog_make(void) {
  if (fog_made0 == r_fog0 && fog_made1 == r_fog1) return;
  fog_made0 = r_fog0, fog_made1 = r_fog1;
  float d0 = ZK / r_fog0;
  fog_d0 = d0 >= 8 * FOG_N ? 8 * FOG_N - 1 : (uint16_t)d0;
  fog_d1 = 0;
  for (int i = 0; i < FOG_N; i++) {
    float z = ZK / ((float)i * 8 + 4), t = (z - r_fog0) / (r_fog1 - r_fog0);
    fogtab[i] = (uint8_t)(t <= 0 ? 0 : t >= 1 ? 32 : (int)(t * 32 + 0.5f));
    if (fogtab[i] == 32) fog_d1 = (uint16_t)(i * 8 + 8);
  }
}

static uint16_t r_floor_depth(float z) {
  float d = ZK / z;
  d -= d * (1.0f / 256);   /* (a hair farther: a shadow lying on it is drawn, what is under it is not) */
  return d >= 65535 ? 65535 : d <= 0 ? 0 : (uint16_t)d;
}

/* after a band's polygons: the ground (its picture, fogged as its row's depth has it) and the sky where none lies
 * nearer, and every other pixel toward the fog's colour by its depth; one pass over the band */
static void back_band(const Texture *tex, r_ground_fn ground) {
  uint16_t fc = fog565, d0 = fog_d0, d1 = fog_d1;
  uint32_t fs = (fc | (uint32_t)fc << 16) & 0x07E0F81F;   /* (r_mix565 with the fog's colour spread once) */
  const uint32_t *sp = tex ? spread(tex) : NULL;
  for (int y = r_y0; y < r_y1; y++) {
    uint16_t *zb = zbuf + (y - r_y0) * SCREEN_W, *cb = r_col + (y - r_y0) * SCREEN_W;
    RGround g = {0, 0, 0, 0, 0};
    if (ground && sp) ground(y, &g);
    uint16_t dg = g.z > 0 && g.z < r_fog1 ? r_floor_depth(g.z) : 0;
    int tg = dg >= d0 ? 0 : fogtab[dg >> 3];
    if (!dg || tg >= 32) {
      /* the sky (or the ground all in the fog): the fog's colour where nothing is drawn */
      for (int x = 0; x < SCREEN_W; x++) {
        uint16_t d = zb[x];
        if (d >= d0) continue;
        if (d < d1) {
          cb[x] = fc;
          continue;
        }
        uint32_t c = cb[x], k = (c | c << 16) & 0x07E0F81F;
        k = (k + ((fs - k) * (uint32_t)fogtab[d >> 3] >> 5)) & 0x07E0F81F;
        cb[x] = (uint16_t)(k | k >> 16);
      }
      continue;
    }
    /* the ground's texel at x: (v >> vs & hm) | (u >> 16 & wm), as raster_row finds them; fogged by its row */
    const uint8_t *px = tex->px;
    uint32_t wm = (1u << tex->wl) - 1, hm = ((1u << tex->hl) - 1) << tex->wl, keep = (uint32_t)(32 - tg), add = fs * (uint32_t)tg;
    int vs = 16 - tex->wl;
    for (int x = 0; x < SCREEN_W; x++) {
      uint16_t d = zb[x];
      if (d < dg) {
        uint32_t u = (uint32_t)(g.u + g.du * x), v = (uint32_t)(g.v + g.dv * x);
        uint32_t k = ((sp[px[(v >> vs & hm) | (u >> 16 & wm)]] * keep + add) >> 5) & 0x07E0F81F;
        cb[x] = (uint16_t)(k | k >> 16), zb[x] = dg;
      } else if (d < d0) {
        if (d < d1) {
          cb[x] = fc;
          continue;
        }
        uint32_t c = cb[x], k = (c | c << 16) & 0x07E0F81F;
        k = (k + ((fs - k) * (uint32_t)fogtab[d >> 3] >> 5)) & 0x07E0F81F;
        cb[x] = (uint16_t)(k | k >> 16);
      }
    }
  }
}

void r_render(const Texture *tex, r_ground_fn ground, r_band_fn overlay) {
  fog_make();
  fog565 = r_565(r_fog_rgb);
  /* the see-through polygons after the others (and after the ground: they are seen over it), in the order they came */
  if (nalpha) {
    int k = 0;
    for (int i = 0; i < npolys; i++)
      if (polys[i].alpha == 255 || polys[i].kind == K_PERSP) order[k++] = (uint16_t)i;
    for (int i = 0; i < npolys; i++)
      if (polys[i].alpha != 255 && polys[i].kind != K_PERSP) order[k++] = (uint16_t)i;
  }
  /* the opaque ones nearest first: what lies behind them is then passed over 16 pixels at a time (raster_row), not drawn
   * and drawn over (a counting sort by their nearest depths, its lists in the depth buffer's room, unused till the
   * bands) */
  int opaque = npolys - nalpha;
  uint16_t *sorted = (uint16_t *)(void *)zbuf, *count = sorted + MAXP;
  for (int k = 0; k < 256; k++) count[k] = 0;
  for (int i = 0; i < opaque; i++) count[polys[order[i]].near]++;
  for (int k = 255, at = 0; k >= 0; k--) {
    int c = count[k];
    count[k] = (uint16_t)at, at += c;
  }
  for (int i = 0; i < opaque; i++) sorted[count[polys[order[i]].near]++] = order[i];
  for (int i = 0; i < opaque; i++) order[i] = sorted[i];
  for (int band = 0; band < NBANDS; band++) {
    r_y0 = band * BAND_H, r_y1 = r_y0 + BAND_H;
    /* (cleared a word at a time: the C library's is a byte at a time) */
    for (uint32_t *p = (uint32_t *)(void *)zbuf, *e = p + sizeof zbuf / 4; p < e; p += 4) p[0] = p[1] = p[2] = p[3] = 0;
    for (int i = 0; i < npolys; i++) {
      if (i == opaque) back_band(tex, ground);
      const Poly *q = &polys[order[i]];
      if (q->y1 > r_y0 && q->y0 < r_y1) {
#if NP_SOFT_FLOAT
        if (q->n == 3 && (q->kind == K_FLAT || q->kind == K_PERSP)) raster_tri_fx(q);
        else raster_fx(q);
#else
        if (q->n == 3 && (q->kind == K_FLAT || q->kind == K_PERSP)) raster_tri(q);
        else raster(q);
#endif
      }
    }
    if (opaque == npolys) back_band(tex, ground);
    if (overlay) overlay();
    plat_push(0, r_y0, SCREEN_W, BAND_H, r_col);
  }
}
