/* The 3D renderer (r3d.h): polygons in a list, drawn band by band with a depth buffer. */
#include "r3d.h"

#define NEAR 0.35f
#define FAR 190.0f
#define ZK 22000.0f      /* depths are ZK / z: 62857 at the near plane, larger is nearer */
#define MAXP 1600        /* polygons a frame */
#define MAXV 5400        /* their corners */
#define MAXA 3600        /* corners with a light level and a place in a picture */
#define MAXT 480         /* textured faces in perspective */
#define MAXN 14          /* corners of a polygon once cut */
#define SUB 8.0f         /* corners are kept in 1/8 pixels */
#define GUARD 1900.0f    /* (so they may lie at most this far from the screen's middle) */

enum { K_FLAT, K_SHADE, K_TEX, K_PERSP };

typedef struct {
  float a, b, c;         /* its depth at (sx, sy): a sx + b sy + c */
  uint16_t v0, at;       /* its first corner, and its first corner's light and texture place */
  uint16_t color;        /* RGB565, or the texture's number */
  uint8_t n, kind;
  uint8_t y0, y1;        /* the rows it covers */
  uint8_t alpha;         /* see-through (255: not); textured faces: their light 0..32 */
  uint8_t extra;         /* a textured face in perspective: its planes */
} Poly;
typedef struct {
  int16_t x, y;
} Pos;
typedef uint32_t Attr;   /* light (8 bits), u and v (12 bits each, 1/4 texels) */
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
float r_haze0 = 60, r_haze1 = 150, r_far = 40;
uint32_t r_haze_rgb = 0xD8F0FF;

/* the frame's lists: in the RAM after the game's own (r_init) */
typedef struct {
  Poly polys[MAXP];
  uint16_t order[MAXP];   /* the see-through ones go last */
  Pos pos[MAXV];
  Attr attr[MAXA];
  UV uvs[MAXT];
  uint16_t zbuf[SCREEN_W * BAND_H] __attribute__((aligned(8)));
} Work;
static Work *work;
#define polys (work->polys)
#define order (work->order)
#define pos (work->pos)
#define attr (work->attr)
#define uvs (work->uvs)
#define zbuf (work->zbuf)
static int npolys, nverts, nattr, nuv, nalpha;

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
  npolys = nverts = nattr = nuv = nalpha = 0;
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

/* projects, checks and keeps a polygon in camera space */
static void emit(CV *p, int n, int kind, uint16_t color, int flags, int alpha, uint16_t avg) {
  if (npolys >= MAXP || nverts + MAXN > MAXV || ((kind == K_SHADE || kind == K_TEX) && nattr + MAXN > MAXA)) return;
  CV b1[MAXN], b2[MAXN];
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
  float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f, zsum = 0;
  for (int i = 0; i < n; i++) {
    sx[i] += cam.cx, sy[i] += cam.cy;
    if (sx[i] < x0) x0 = sx[i];
    if (sx[i] > x1) x1 = sx[i];
    if (sy[i] < y0) y0 = sy[i];
    if (sy[i] > y1) y1 = sy[i];
    zsum += p[i].z;
  }
  if (x1 < 0 || x0 >= SCREEN_W || y1 < 0 || y0 >= SCREEN_H) return;
  float area = 0;
  for (int i = 0, j = n - 1; i < n; j = i++) area += sx[j] * sy[i] - sx[i] * sy[j];
  if (area < 0.01f && !(flags & P_TWOSIDED && area < -0.01f)) return;
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
  float zm = zsum / (float)n;
  q->kind = (uint8_t)kind;
  q->extra = 0;
  if (kind == K_PERSP) {
    /* far off, a picture is only its average colour (color already holds it when kind comes back flat) */
    const Texture *t = r_tex[color];
    if (t->clear && !avg && (nuv >= MAXT || zm > r_far * 1.6f)) return;   /* (a see-through one is only drawn as itself: near) */
    if ((!t->clear && (zm > r_haze1 * 0.6f || nuv >= MAXT)) || (avg && (zm > r_far || nuv >= MAXT))) {
      /* (the palette's first useful colour stands for it: the tool puts the average there; or the face's own) */
      uint16_t c = avg ? avg : t->pal[t->clear ? 1 : 0];
      uint32_t a8 = (uint32_t)(c >> 11) * 255 / 31 << 16 | (uint32_t)((c >> 5) & 63) * 255 / 63 << 8 | (uint32_t)(c & 31) * 255 / 31;
      color = r_565(r_mix888(a8, 0, 256 - alpha * 8 > 256 ? 0 : 256 - alpha * 8));
      kind = K_FLAT;
      q->kind = K_FLAT;
      alpha = 255;
      flags &= ~P_NOHAZE;
      if (zm > r_haze0) {
        float h = (zm - r_haze0) / (r_haze1 - r_haze0);
        uint32_t c8 = (uint32_t)(color >> 11) * 255 / 31 << 16 | (uint32_t)((color >> 5) & 63) * 255 / 63 << 8 | (uint32_t)(color & 31) * 255 / 31;
        color = r_565(r_mix888(c8, r_haze_rgb, h >= 1 ? 240 : (int)(h * 240)));
      }
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
      q->extra = (uint8_t)nuv++;
    }
  }
  if (kind == K_FLAT && !(flags & P_NOHAZE) && zm > r_haze0) {
    float h = (zm - r_haze0) / (r_haze1 - r_haze0);
    uint32_t c8 = (uint32_t)(color >> 11) * 255 / 31 << 16 | (uint32_t)((color >> 5) & 63) * 255 / 63 << 8 | (uint32_t)(color & 31) * 255 / 31;
    color = r_565(r_mix888(c8, r_haze_rgb, h >= 1 ? 240 : (int)(h * 240)));
  }
  q->color = color;
  q->alpha = (uint8_t)(kind == K_PERSP ? alpha : flags & P_ALPHA ? (alpha > 255 ? 255 : alpha < 0 ? 0 : alpha) : 255);
  q->v0 = (uint16_t)nverts, q->n = (uint8_t)n;
  for (int i = 0; i < n; i++) pos[nverts + i] = (Pos){(int16_t)(sx[i] * SUB + 0.5f), (int16_t)(sy[i] * SUB + 0.5f)};
  if (kind == K_SHADE || kind == K_TEX) {
    q->at = (uint16_t)nattr;
    for (int i = 0; i < n; i++) {
      uint32_t l = (uint32_t)(p[i].s + 0.5f), u = (uint32_t)(p[i].u + 0.5f), v = (uint32_t)(p[i].v + 0.5f);
      attr[nattr + i] = (l > 255 ? 255 : l) | (u > 4095 ? 4095 : u) << 8 | (v > 4095 ? 4095 : v) << 20;
    }
    nattr += n;
  }
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
  CV c[MAXN];
  int far = 0;
  for (int i = 0; i < n; i++) {
    V3 v = r_view(w[i]);
    c[i] = (CV){v.x, v.y, v.z, 0, 0, 0};
    far += v.z > FAR;
  }
  if (far == n) return;
  emit(c, n, K_FLAT, r_565(rgb), flags, alpha, 0);
}

void r_poly_tex(const V3 *w, const float *uv, int n, int tex, int light, int flags) { r_poly_tex_avg(w, uv, n, tex, light, flags, 0); }

void r_poly_tex_avg(const V3 *w, const float *uv, int n, int tex, int light, int flags, uint16_t avg) {
  if (n < 3 || n > MAXN - 4) return;
  CV c[MAXN];
  int far = 0;
  for (int i = 0; i < n; i++) {
    V3 v = r_view(w[i]);
    c[i] = (CV){v.x, v.y, v.z, 0, uv[2 * i], uv[2 * i + 1]};
    far += v.z > FAR;
  }
  if (far == n) return;
  int l = light >> 3;
  emit(c, n, K_PERSP, (uint16_t)tex, flags, l > 32 ? 32 : l < 0 ? 0 : l, avg);
}

void r_mesh(const RV *v, int nv, const Tri *t, int nt, const uint16_t *pal, int tex) {
  for (int i = 0; i < nt; i++) {
    const RV *a = &v[t[i].a], *b = &v[t[i].b], *c = &v[t[i].c];
    CV p[3] = {{a->x, a->y, a->z, a->s, a->u, a->v}, {b->x, b->y, b->z, b->s, b->u, b->v}, {c->x, c->y, c->z, c->s, c->u, c->v}};
    /* (quick: a triangle turned away is left out before anything else) */
    if (a->z > NEAR && b->z > NEAR && c->z > NEAR) {
      float ax = a->x / a->z, ay = a->y / a->z, bx = b->x / b->z, by = b->y / b->z, cx = c->x / c->z, cy = c->y / c->z;
      if ((bx - ax) * (cy - ay) - (cx - ax) * (by - ay) >= 0) continue;
    }
    if (t[i].col == TEX_COL) emit(p, 3, K_TEX, (uint16_t)tex, 0, 255, 0);
    else emit(p, 3, K_SHADE, pal[t[i].col], 0, 255, 0);
  }
  (void)nv;
}

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
  float y0, y1, x, dx, s, ds, u, du, v, dv;
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

static void raster(const Poly *q) {
  int ya = q->y0 > r_y0 ? q->y0 : r_y0, yb = q->y1 < r_y1 ? q->y1 : r_y1;
  if (ya >= yb) return;
  const Pos *pp = pos + q->v0;
  const Attr *at = q->kind == K_SHADE || q->kind == K_TEX ? attr + q->at : NULL;
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
    if (at) {
      float sa = (float)(at[ka] & 255), sb = (float)(at[kb] & 255);
      float ua = (float)(at[ka] >> 8 & 4095), ub = (float)(at[kb] >> 8 & 4095);
      float va = (float)(at[ka] >> 20), vb = (float)(at[kb] >> 20);
      E->ds = (sb - sa) * h, E->s = sa - ay * E->ds;
      E->du = (ub - ua) * h, E->u = ua - ay * E->du;
      E->dv = (vb - va) * h, E->v = va - ay * E->dv;
    }
  }
  if (!ne[0] || !ne[1]) return;
#ifdef HOST
  r_stat_polys++;
#endif
  uint16_t col = q->color;
  int kind = q->kind;
  const Texture *tx = kind == K_TEX || kind == K_PERSP ? r_tex[q->color] : NULL;
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
      continue;
    }
    if (kind == K_PERSP) {
      const UV *w = &uvs[q->extra];
      const uint8_t *px = tx->px;
      const uint16_t *pal = tx->pal;
      /* the texel: (v >> vs & hmask) | (u >> 16 & wm), v's row already times the width */
      uint32_t wm = (1u << tx->wl) - 1, hmask = ((1u << tx->hl) - 1) << tx->wl;
      int vs = 16 - tx->wl, light = q->alpha;
      bool clear = tx->clear;
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
        /* (three loops: the plain one kept as short as can be) */
        if (light >= 32 && !clear) {
          for (; zp < ze; zp++, cp++, z += (uint32_t)da, uu += du, vv += dv) {
            uint16_t d = (uint16_t)(z >> 16);
            if (d > *zp) *zp = d, *cp = pal[px[(vv >> vs & hmask) | (uu >> 16 & wm)]];
          }
        } else if (light >= 32) {
          for (; zp < ze; zp++, cp++, z += (uint32_t)da, uu += du, vv += dv) {
            uint16_t d = (uint16_t)(z >> 16);
            if (d <= *zp) continue;
            uint8_t t = px[(vv >> vs & hmask) | (uu >> 16 & wm)];
            if (t) *zp = d, *cp = pal[t];
#ifdef HOST
            if (t) r_stat_written++;
            else {
              r_stat_holes++, r_stat_texhole[q->color]++;
              extern int r_show_holes;
              if (r_show_holes) *cp = 0xF81F;
            }
#endif
          }
        } else {
          for (; zp < ze; zp++, cp++, z += (uint32_t)da, uu += du, vv += dv) {
            uint16_t d = (uint16_t)(z >> 16);
            if (d <= *zp) continue;
            uint8_t t = px[(vv >> vs & hmask) | (uu >> 16 & wm)];
            if (clear && !t) continue;
            *zp = d, *cp = r_mix565(0, pal[t], light);
          }
        }
        x += n;
        qz = qz2, qu = qu2, qv = qv2, u = u2, v = v2;
        known = true;
      }
      continue;
    }
    /* shaded, and maybe textured: the light and the texture place from the edges, across */
    float sl = L->s + yc * L->ds, sr = R->s + yc * R->ds, span = xr - xl > 0.01f ? 1 / (xr - xl) : 0;
    float off = (float)x0 + 0.5f - xl;
    float dsx = (sr - sl) * span;
    int32_t s = (int32_t)((sl + dsx * off) * 65536.0f), ds = (int32_t)(dsx * 65536.0f);
    if (kind == K_SHADE) {
      for (int x = x0; x < x1; x++, z += (uint32_t)da, s += ds) {
        uint16_t zz = (uint16_t)(z >> 16);
        if (zz <= zb[x]) continue;
        int k = s >> 19;   /* 0..255 to 0..32 */
        zb[x] = zz, cb[x] = k >= 32 ? col : r_mix565(0, col, k < 0 ? 0 : k);
      }
    } else {
      float ul = L->u + yc * L->du, ur = R->u + yc * R->du, vl = L->v + yc * L->dv, vr = R->v + yc * R->dv;
      float dux = (ur - ul) * span, dvx = (vr - vl) * span;
      /* (texture places are in 1/4 texels) */
      int32_t u = (int32_t)((ul + dux * off) * 16384.0f), du = (int32_t)(dux * 16384.0f);
      int32_t v = (int32_t)((vl + dvx * off) * 16384.0f), dv = (int32_t)(dvx * 16384.0f);
      uint32_t wm = (1u << tx->wl) - 1, hm = (1u << tx->hl) - 1;
      int wl = tx->wl, clear = tx->clear;
      for (int x = x0; x < x1; x++, z += (uint32_t)da, s += ds, u += du, v += dv) {
        uint16_t zz = (uint16_t)(z >> 16);
        if (zz <= zb[x]) continue;
        uint8_t t = tx->px[(((uint32_t)v >> 16) & hm) << wl | (((uint32_t)u >> 16) & wm)];
        if (clear && !t) continue;
        int k = s >> 19;
        uint16_t c = tx->pal[t];
        zb[x] = zz, cb[x] = k >= 32 ? c : r_mix565(0, c, k < 0 ? 0 : k);
      }
    }
  }
}

uint16_t r_floor_depth(float z) {
  float d = ZK / z;
  d -= d * (1.0f / 256);   /* (a hair farther: a shadow lying on it is drawn, what is under it is not) */
  return d >= 65535 ? 65535 : d <= 0 ? 0 : (uint16_t)d;
}

void r_band_depth(int y, int x0, int x1, uint16_t d) {
  uint16_t *zb = zbuf + (y - r_y0) * SCREEN_W;
  for (int x = x0; x < x1; x++) zb[x] = d;
}

void r_render(r_band_fn bg, r_band_fn overlay) {
  /* the see-through polygons after the others, in the order they came */
  if (nalpha) {
    int k = 0;
    for (int i = 0; i < npolys; i++)
      if (polys[i].alpha == 255 || polys[i].kind == K_PERSP) order[k++] = (uint16_t)i;
    for (int i = 0; i < npolys; i++)
      if (polys[i].alpha != 255 && polys[i].kind != K_PERSP) order[k++] = (uint16_t)i;
  }
  for (int band = 0; band < NBANDS; band++) {
    r_y0 = band * BAND_H, r_y1 = r_y0 + BAND_H;
    /* (cleared a word at a time: the C library's is a byte at a time) */
    for (uint32_t *p = (uint32_t *)(void *)zbuf, *e = p + sizeof zbuf / 4; p < e; p += 4) p[0] = p[1] = p[2] = p[3] = 0;
    if (bg) bg();
    else
      for (int i = 0; i < SCREEN_W * BAND_H; i++) r_col[i] = 0;
    for (int i = 0; i < npolys; i++) {
      const Poly *q = &polys[order[i]];
      if (q->y1 > r_y0 && q->y0 < r_y1) raster(q);
    }
    if (overlay) overlay();
    plat_push(0, r_y0, SCREEN_W, BAND_H, r_col);
  }
}
