/* circloO's physics (see phys.h): Box2D 2.3's contact and joint solvers, written down for the few shapes
 * the game has. Box2D (c) Erin Catto, zlib licence. */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "phys.h"

World W;

/* Box2D's settings */
#define DT (1.0f / 60)
#define ITER_V 10
#define ITER_P 10
#define SLOP 0.005f
#define SKIN (2 * SLOP)          /* b2_polygonRadius: the skin of edges and polygons */
#define MAX_CORR 0.2f
#define BAUM 0.2f
#define MAX_TRANS 2.0f
#define MAX_ROT (0.5f * 3.14159265f)
#define CELL 2.0f
#define GRID 30

typedef struct {
  int n;
  float nx, ny;              /* from A to B */
  float px[2], py[2], sep[2];
  uint8_t id[2];
} Man;

static inline float cross(float ax, float ay, float bx, float by) { return ax * by - ay * bx; }

void ph_reset(void) {
  memset(&W, 0, sizeof W);
  W.nb = 1;                   /* the ground */
  W.b[0].alive = 1;
  W.b[0].c = 1;
  W.gx = 0, W.gy = 35;        /* the room's gravity, m/s^2 */
}

static int new_body(void) {
  for (int i = 1; i < W.nb; i++)
    if (!W.b[i].alive) {
      memset(&W.b[i], 0, sizeof W.b[i]);
      return i;
    }
  if (W.nb >= MAX_BODIES) return -1;
  memset(&W.b[W.nb], 0, sizeof W.b[0]);
  return W.nb++;
}

int ph_add_circle(float x, float y, float r, float density, float friction) {
  int i = new_body();
  if (i < 0) return -1;
  Body *b = &W.b[i];
  b->x = x, b->y = y, b->c = 1, b->r = r;
  b->shape = SH_CIRCLE, b->alive = 1, b->fr = friction;
  if (density > 0) {
    float m = density * 3.14159265f * r * r;
    b->im = 1 / m;
    b->ii = 1 / (m * 0.5f * r * r);
  }
  return i;
}

int ph_add_rotor(float x, float y, float hx, float hy, float angle, float density, float adamp) {
  int i = new_body();
  if (i < 0) return -1;
  Body *b = &W.b[i];
  b->x = x, b->y = y, b->a = angle, b->c = cosf(angle), b->s = sinf(angle);
  b->hx = hx, b->hy = hy, b->shape = SH_BOX, b->alive = 1, b->pinned = 1, b->fr = 0.2f;
  b->ad = adamp;
  if (density > 0) {
    float m = density * 4 * hx * hy;
    b->ii = 1 / (m * (hx * hx + hy * hy) / 3);
  }
  return i;
}

void ph_kill(int i) {
  W.b[i].alive = 0;
  for (int k = 0; k < W.nj; k++)
    if (W.j[k].a == i || W.j[k].b == i) W.j[k].alive = 0;
}

void ph_static_circle(float x, float y, float r) {
  if (W.nsc >= MAX_SCIRCLES) return;
  W.sc[W.nsc++] = (SCircle){x, y, r};
}

void ph_static_edge(float x1, float y1, float x2, float y2) {
  if (W.nse >= MAX_SEDGES) return;
  W.se[W.nse++] = (SEdge){x1, y1, x2, y2, -1, -1};
}

void ph_static_chain(const float *xy, int n, bool loop) {
  int first = W.nse, segs = loop ? n : n - 1;
  if (n < 2 || W.nse + segs > MAX_SEDGES) return;
  for (int k = 0; k < segs; k++) {
    int k2 = (k + 1) % n;
    SEdge *e = &W.se[W.nse++];
    *e = (SEdge){xy[2 * k], xy[2 * k + 1], xy[2 * k2], xy[2 * k2 + 1], -1, -1};
    if (k > 0 || loop) e->prev = (int16_t)(first + (k + segs - 1) % segs);
    if (k < segs - 1 || loop) e->next = (int16_t)(first + (k + 1) % segs);
  }
}

static void poly_normals(SPoly *p) {
  for (int k = 0; k < p->n; k++) {
    int k2 = (k + 1) % p->n;
    float ex = p->x[k2] - p->x[k], ey = p->y[k2] - p->y[k], l = sqrtf(ex * ex + ey * ey);
    if (l < 1e-9f) l = 1;
    p->nx[k] = ey / l, p->ny[k] = -ex / l;
  }
}

void ph_static_poly(const float *xy, int n) {
  if (W.nsp >= MAX_SPOLYS || n < 2 || n > 4) return;
  SPoly *p = &W.sp[W.nsp++];
  p->n = (uint8_t)n;
  float area = 0;
  for (int k = 0; k < n; k++) area += cross(xy[2 * k], xy[2 * k + 1], xy[2 * ((k + 1) % n)], xy[2 * ((k + 1) % n) + 1]);
  for (int k = 0; k < n; k++) {
    int s = area >= 0 ? k : n - 1 - k;   /* counter-clockwise in Box2D's sense */
    p->x[k] = xy[2 * s], p->y[k] = xy[2 * s + 1];
  }
  poly_normals(p);
}

void ph_keep_statics(void) { W.keep_se = W.nse; }
void ph_drop_statics(void) { W.nse = W.keep_se; }

/* ---- the grid of static shapes ---- */

static void static_box(int s, float *x0, float *y0, float *x1, float *y1) {
  if (s < MAX_SCIRCLES) {
    const SCircle *c = &W.sc[s];
    *x0 = c->x - c->r, *y0 = c->y - c->r, *x1 = c->x + c->r, *y1 = c->y + c->r;
  } else if (s < MAX_SCIRCLES + MAX_SEDGES) {
    const SEdge *e = &W.se[s - MAX_SCIRCLES];
    *x0 = fminf(e->x1, e->x2), *x1 = fmaxf(e->x1, e->x2), *y0 = fminf(e->y1, e->y2), *y1 = fmaxf(e->y1, e->y2);
  } else {
    const SPoly *p = &W.sp[s - MAX_SCIRCLES - MAX_SEDGES];
    *x0 = *x1 = p->x[0], *y0 = *y1 = p->y[0];
    for (int k = 1; k < p->n; k++) *x0 = fminf(*x0, p->x[k]), *x1 = fmaxf(*x1, p->x[k]), *y0 = fminf(*y0, p->y[k]), *y1 = fmaxf(*y1, p->y[k]);
  }
}

static int cell_of(float v) {
  int c = (int)floorf(v / CELL);
  return c < 0 ? 0 : c >= GRID ? GRID - 1 : c;
}

#define FOR_STATICS(s)                                                                   \
  for (int _g = 0; _g < 3; _g++)                                                         \
    for (int _i = 0, _n = _g == 0 ? W.nsc : _g == 1 ? W.nse : W.nsp; _i < _n; _i++)      \
      for (int s = _i + (_g == 0 ? 0 : _g == 1 ? MAX_SCIRCLES : MAX_SCIRCLES + MAX_SEDGES), _once = 1; _once; _once = 0)

void ph_build_grid(void) {
  static uint16_t count[GRID * GRID];
  memset(count, 0, sizeof count);
  for (int pass = 0; pass < 2; pass++) {
    FOR_STATICS(s) {
      float x0, y0, x1, y1;
      static_box(s, &x0, &y0, &x1, &y1);
      for (int cy = cell_of(y0 - SKIN); cy <= cell_of(y1 + SKIN); cy++)
        for (int cx = cell_of(x0 - SKIN); cx <= cell_of(x1 + SKIN); cx++) {
          int c = cy * GRID + cx;
          if (pass == 0) count[c]++;
          else if (W.cell_start[c] + count[c] < (int)(sizeof W.cell_items / 2)) W.cell_items[W.cell_start[c] + count[c]++] = (uint16_t)s;
        }
    }
    if (pass == 0) {
      int t = 0;
      for (int c = 0; c < GRID * GRID; c++) W.cell_start[c] = (uint16_t)t, t += count[c], count[c] = 0;
      W.cell_start[GRID * GRID] = (uint16_t)t;
    }
  }
}

/* ---- shapes in the world ---- */

static void body_poly(const Body *b, SPoly *p) {
  static const float sx[4] = {-1, 1, 1, -1}, sy[4] = {-1, -1, 1, 1};
  p->n = 4;
  for (int k = 0; k < 4; k++) {
    float lx = sx[k] * b->hx, ly = sy[k] * b->hy;
    p->x[k] = b->x + b->c * lx - b->s * ly;
    p->y[k] = b->y + b->s * lx + b->c * ly;
  }
  p->nx[0] = b->s, p->ny[0] = -b->c;     /* (0,-1) turned */
  p->nx[1] = b->c, p->ny[1] = b->s;      /* (1,0) */
  p->nx[2] = -b->s, p->ny[2] = b->c;     /* (0,1) */
  p->nx[3] = -b->c, p->ny[3] = -b->s;    /* (-1,0) */
}

static void edge_poly(const SEdge *e, SPoly *p) {
  p->n = 2;
  p->x[0] = e->x1, p->y[0] = e->y1, p->x[1] = e->x2, p->y[1] = e->y2;
  poly_normals(p);
  p->nx[1] = -p->nx[0], p->ny[1] = -p->ny[0];
}

/* ---- contacts (Box2D's b2Collide* functions) ---- */

static void coll_cc(float ax, float ay, float ar, float bx, float by, float br, Man *m) {
  float dx = bx - ax, dy = by - ay, d2 = dx * dx + dy * dy, R = ar + br;
  m->n = 0;
  if (d2 > R * R) return;
  float d = sqrtf(d2), nx = 1, ny = 0;
  if (d > 1e-9f) nx = dx / d, ny = dy / d;
  m->n = 1, m->nx = nx, m->ny = ny, m->id[0] = 0;
  m->sep[0] = d - R;
  m->px[0] = 0.5f * (ax + nx * ar + bx - nx * br);
  m->py[0] = 0.5f * (ay + ny * ar + by - ny * br);
}

/* a circle (A) on an edge (B), with Box2D's handling of a chain's corners */
static void coll_ce(float cx, float cy, float r, const SEdge *e, Man *m) {
  m->n = 0;
  float R = r + SKIN;
  float ex = e->x2 - e->x1, ey = e->y2 - e->y1;
  float u = ex * (e->x2 - cx) + ey * (e->y2 - cy);
  float v = ex * (cx - e->x1) + ey * (cy - e->y1);
  float px, py;
  if (v <= 0) {
    px = e->x1, py = e->y1;
    float dx = cx - px, dy = cy - py;
    if (dx * dx + dy * dy > R * R) return;
    if (e->prev >= 0) {
      const SEdge *p = &W.se[e->prev];
      if ((e->x1 - p->x1) * (e->x1 - cx) + (e->y1 - p->y1) * (e->y1 - cy) > 0) return;
    }
  } else if (u <= 0) {
    px = e->x2, py = e->y2;
    float dx = cx - px, dy = cy - py;
    if (dx * dx + dy * dy > R * R) return;
    if (e->next >= 0) {
      const SEdge *n = &W.se[e->next];
      if ((n->x2 - e->x2) * (cx - e->x2) + (n->y2 - e->y2) * (cy - e->y2) > 0) return;
    }
  } else {
    float den = ex * ex + ey * ey;
    px = (u * e->x1 + v * e->x2) / den, py = (u * e->y1 + v * e->y2) / den;
    float dx = cx - px, dy = cy - py;
    if (dx * dx + dy * dy > R * R) return;
  }
  float dx = px - cx, dy = py - cy, d = sqrtf(dx * dx + dy * dy);
  float nx, ny;
  if (d > 1e-9f) nx = dx / d, ny = dy / d;
  else {
    float l = sqrtf(ex * ex + ey * ey);
    nx = ey / l, ny = -ex / l;
  }
  m->n = 1, m->nx = nx, m->ny = ny, m->id[0] = 0;
  m->sep[0] = d - R;
  m->px[0] = 0.5f * (cx + nx * r + px - nx * SKIN);
  m->py[0] = 0.5f * (cy + ny * r + py - ny * SKIN);
}

/* a polygon (A) on a circle (B) */
static void coll_pc(const SPoly *p, float cx, float cy, float r, Man *m) {
  m->n = 0;
  float R = r + SKIN, sep = -1e30f;
  int ni = 0;
  for (int k = 0; k < p->n; k++) {
    float s = p->nx[k] * (cx - p->x[k]) + p->ny[k] * (cy - p->y[k]);
    if (s > R) return;
    if (s > sep) sep = s, ni = k;
  }
  int n2 = (ni + 1) % p->n;
  float x1 = p->x[ni], y1 = p->y[ni], x2 = p->x[n2], y2 = p->y[n2];
  float nx = p->nx[ni], ny = p->ny[ni], qx, qy;
  if (sep < 1e-6f) {
    qx = cx - nx * sep, qy = cy - ny * sep;
  } else {
    float u1 = (cx - x1) * (x2 - x1) + (cy - y1) * (y2 - y1);
    float u2 = (cx - x2) * (x1 - x2) + (cy - y2) * (y1 - y2);
    if (u1 <= 0 || u2 <= 0) {
      qx = u1 <= 0 ? x1 : x2, qy = u1 <= 0 ? y1 : y2;
      float dx = cx - qx, dy = cy - qy, d2 = dx * dx + dy * dy;
      if (d2 > R * R) return;
      float d = sqrtf(d2);
      if (d < 1e-9f) return;
      nx = dx / d, ny = dy / d;
      sep = d;
    } else {
      qx = cx - nx * sep, qy = cy - ny * sep;
    }
  }
  m->n = 1, m->nx = nx, m->ny = ny, m->id[0] = 0;
  m->sep[0] = sep - R;
  m->px[0] = 0.5f * (qx + nx * SKIN + cx - nx * r);
  m->py[0] = 0.5f * (qy + ny * SKIN + cy - ny * r);
}

static float max_sep(int *edge, const SPoly *a, const SPoly *b) {
  float best = -1e30f;
  int bi = 0;
  for (int i = 0; i < a->n; i++) {
    float si = 1e30f;
    for (int j = 0; j < b->n; j++) {
      float s = a->nx[i] * (b->x[j] - a->x[i]) + a->ny[i] * (b->y[j] - a->y[i]);
      if (s < si) si = s;
    }
    if (si > best) best = si, bi = i;
  }
  *edge = bi;
  return best;
}

static int clip(float *ox, float *oy, uint8_t *oid, const float *ix, const float *iy, const uint8_t *iid, float nx,
                float ny, float off) {
  int n = 0;
  float d0 = nx * ix[0] + ny * iy[0] - off, d1 = nx * ix[1] + ny * iy[1] - off;
  if (d0 <= 0) ox[n] = ix[0], oy[n] = iy[0], oid[n++] = iid[0];
  if (d1 <= 0) ox[n] = ix[1], oy[n] = iy[1], oid[n++] = iid[1];
  if (d0 * d1 < 0) {
    float t = d0 / (d0 - d1);
    ox[n] = ix[0] + t * (ix[1] - ix[0]), oy[n] = iy[0] + t * (iy[1] - iy[0]), oid[n++] = iid[0];
  }
  return n;
}

/* two polygons (an edge is a polygon of two sides) */
static void coll_pp(const SPoly *a, const SPoly *b, Man *m) {
  m->n = 0;
  float R = 2 * SKIN;
  int ea, eb;
  float sa = max_sep(&ea, a, b);
  if (sa > R) return;
  float sb = max_sep(&eb, b, a);
  if (sb > R) return;
  const SPoly *p1 = a, *p2 = b;
  int e1 = ea, flip = 0;
  if (sb > sa + 0.1f * SLOP) p1 = b, p2 = a, e1 = eb, flip = 1;
  float n1x = p1->nx[e1], n1y = p1->ny[e1];
  int idx = 0;
  float mind = 1e30f;
  for (int i = 0; i < p2->n; i++) {
    float d = n1x * p2->nx[i] + n1y * p2->ny[i];
    if (d < mind) mind = d, idx = i;
  }
  int i2 = (idx + 1) % p2->n;
  float ix[2] = {p2->x[idx], p2->x[i2]}, iy[2] = {p2->y[idx], p2->y[i2]};
  uint8_t iid[2] = {(uint8_t)idx, (uint8_t)i2};
  int e2 = (e1 + 1) % p1->n;
  float v1x = p1->x[e1], v1y = p1->y[e1], v2x = p1->x[e2], v2y = p1->y[e2];
  float tx = v2x - v1x, ty = v2y - v1y, tl = sqrtf(tx * tx + ty * ty);
  if (tl < 1e-9f) return;
  tx /= tl, ty /= tl;
  float nx = ty, ny = -tx;
  float front = nx * v1x + ny * v1y;
  float side1 = -(tx * v1x + ty * v1y) + R, side2 = tx * v2x + ty * v2y + R;
  float c1x[3], c1y[3], c2x[3], c2y[3];
  uint8_t c1i[3], c2i[3];
  if (clip(c1x, c1y, c1i, ix, iy, iid, -tx, -ty, side1) < 2) return;
  if (clip(c2x, c2y, c2i, c1x, c1y, c1i, tx, ty, side2) < 2) return;
  m->nx = flip ? -nx : nx, m->ny = flip ? -ny : ny;
  for (int k = 0; k < 2; k++) {
    float s = nx * c2x[k] + ny * c2y[k] - front;
    if (s <= R) {
      m->px[m->n] = c2x[k] - 0.5f * s * nx;
      m->py[m->n] = c2y[k] - 0.5f * s * ny;
      m->sep[m->n] = s - R;
      m->id[m->n] = (uint8_t)(c2i[k] | flip << 4);
      m->n++;
    }
  }
}

static void collide_bodies(const Body *A, const Body *B, Man *m) {
  if (A->shape == SH_CIRCLE && B->shape == SH_CIRCLE) coll_cc(A->x, A->y, A->r, B->x, B->y, B->r, m);
  else if (A->shape == SH_BOX && B->shape == SH_CIRCLE) {
    SPoly p;
    body_poly(A, &p);
    coll_pc(&p, B->x, B->y, B->r, m);
  } else if (A->shape == SH_CIRCLE) {
    SPoly p;
    body_poly(B, &p);
    coll_pc(&p, A->x, A->y, A->r, m);
    m->nx = -m->nx, m->ny = -m->ny;
  } else {
    SPoly p, q;
    body_poly(A, &p);
    body_poly(B, &q);
    coll_pp(&p, &q, m);
  }
}

static void collide_static(const Body *A, int s, Man *m) {
  m->n = 0;
  if (s < MAX_SCIRCLES) {
    const SCircle *c = &W.sc[s];
    if (A->shape == SH_CIRCLE) coll_cc(A->x, A->y, A->r, c->x, c->y, c->r, m);
    else {
      SPoly p;
      body_poly(A, &p);
      coll_pc(&p, c->x, c->y, c->r, m);
    }
  } else if (s < MAX_SCIRCLES + MAX_SEDGES) {
    const SEdge *e = &W.se[s - MAX_SCIRCLES];
    if (A->shape == SH_CIRCLE) coll_ce(A->x, A->y, A->r, e, m);
    else {
      SPoly p, q;
      body_poly(A, &p);
      edge_poly(e, &q);
      coll_pp(&p, &q, m);
    }
  } else {
    const SPoly *q = &W.sp[s - MAX_SCIRCLES - MAX_SEDGES];
    if (A->shape == SH_CIRCLE) {
      coll_pc(q, A->x, A->y, A->r, m);
      m->nx = -m->nx, m->ny = -m->ny;
    } else {
      SPoly p;
      body_poly(A, &p);
      coll_pp(&p, q, m);
    }
  }
}

static void body_box(const Body *b, float *x0, float *y0, float *x1, float *y1) {
  float e = b->shape == SH_CIRCLE ? b->r : sqrtf(b->hx * b->hx + b->hy * b->hy);
  *x0 = b->x - e, *x1 = b->x + e, *y0 = b->y - e, *y1 = b->y + e;
}

static const Warm *warm_find(uint32_t key) {
  int lo = 0, hi = W.nwarm - 1;
  while (lo <= hi) {
    int mid = (lo + hi) >> 1;
    if (W.warm[mid].key == key) return &W.warm[mid];
    if (W.warm[mid].key < key) lo = mid + 1;
    else hi = mid - 1;
  }
  return NULL;
}

static void add_contact(int a, int b, int sid, const Man *m) {
  if (W.nc >= MAX_CONTACTS || !m->n) return;
  Contact *c = &W.c[W.nc++];
  const Body *A = &W.b[a];
  float frb = b ? W.b[b].fr : 0.2f;   /* (a static fixture: Box2D's default friction) */
  c->key = (uint32_t)a << 24 | (uint32_t)(b ? b : 256 + sid);
  c->a = (uint8_t)a, c->b = (uint8_t)b, c->sid = (uint16_t)sid, c->n = (uint8_t)m->n;
  c->nx = m->nx, c->ny = m->ny, c->fr = sqrtf(A->fr * frb);
  const Warm *w = warm_find(c->key);
  for (int k = 0; k < m->n; k++) {
    CPoint *p = &c->p[k];
    p->ni = w && k < 2 ? w->ni[k] : 0;
    p->ti = w && k < 2 ? w->ti[k] : 0;
    p->rax = m->px[k] - A->x, p->ray = m->py[k] - A->y;
    p->rbx = m->px[k] - W.b[b].x, p->rby = m->py[k] - W.b[b].y;
  }
}

static int cmp_warm(const void *x, const void *y) {
  uint32_t a = ((const Warm *)x)->key, b = ((const Warm *)y)->key;
  return a < b ? -1 : a > b;
}

static void find_contacts(void) {
  /* last step's impulses, to start from (warm starting) */
  W.nwarm = W.nc;
  for (int i = 0; i < W.nc; i++) {
    W.warm[i].key = W.c[i].key;
    for (int k = 0; k < 2; k++) W.warm[i].ni[k] = W.c[i].p[k].ni, W.warm[i].ti[k] = W.c[i].p[k].ti;
  }
  qsort(W.warm, (size_t)W.nwarm, sizeof W.warm[0], cmp_warm);
  W.nc = 0;
  /* moving bodies against each other: sorted by their left edge */
  int n = 0;
  for (int i = 1; i < W.nb; i++)
    if (W.b[i].alive) {
      float x0, y0, x1, y1;
      body_box(&W.b[i], &x0, &y0, &x1, &y1);
      W.minx[i] = x0;
      int k = n++;
      while (k > 0 && W.minx[W.order[k - 1]] > x0) W.order[k] = W.order[k - 1], k--;
      W.order[k] = (uint8_t)i;
    }
  for (int p = 0; p < n; p++) {
    int i = W.order[p];
    const Body *A = &W.b[i];
    float ax0, ay0, ax1, ay1;
    body_box(A, &ax0, &ay0, &ax1, &ay1);
    for (int q = p + 1; q < n; q++) {
      int j = W.order[q];
      const Body *B = &W.b[j];
      if (W.minx[j] > ax1) break;
      if (A->pinned && B->pinned && A->ii == 0 && B->ii == 0) continue;
      float bx0, by0, bx1, by1;
      body_box(B, &bx0, &by0, &bx1, &by1);
      if (by0 > ay1 || by1 < ay0) continue;
      int a = i < j ? i : j, b = i < j ? j : i;
      Man m;
      collide_bodies(&W.b[a], &W.b[b], &m);
      add_contact(a, b, 0, &m);
    }
  }
  /* against the static shapes, through the grid */
  for (int p = 0; p < n; p++) {
    int i = W.order[p];
    const Body *A = &W.b[i];
    if (A->pinned && A->ii == 0) continue;
    float x0, y0, x1, y1;
    body_box(A, &x0, &y0, &x1, &y1);
    if (++W.stamp_now == 0) memset(W.stamp, 0, sizeof W.stamp), W.stamp_now = 1;
    for (int cy = cell_of(y0); cy <= cell_of(y1); cy++)
      for (int cx = cell_of(x0); cx <= cell_of(x1); cx++) {
        int c = cy * GRID + cx;
        for (int t = W.cell_start[c]; t < W.cell_start[c + 1]; t++) {
          int s = W.cell_items[t];
          if (W.stamp[s] == W.stamp_now) continue;
          W.stamp[s] = W.stamp_now;
          Man m;
          collide_static(A, s, &m);
          add_contact(i, 0, s, &m);
        }
      }
  }
}

/* ---- the solver ---- */

static void contact_masses(Contact *c) {
  const Body *A = &W.b[c->a], *B = &W.b[c->b];
  float tx = c->ny, ty = -c->nx;
  float ima = A->pinned ? 0 : A->im, imb = B->pinned ? 0 : B->im;
  for (int k = 0; k < c->n; k++) {
    CPoint *p = &c->p[k];
    float rna = cross(p->rax, p->ray, c->nx, c->ny), rnb = cross(p->rbx, p->rby, c->nx, c->ny);
    float kn = ima + imb + A->ii * rna * rna + B->ii * rnb * rnb;
    p->nm = kn > 0 ? 1 / kn : 0;
    float rta = cross(p->rax, p->ray, tx, ty), rtb = cross(p->rbx, p->rby, tx, ty);
    float kt = ima + imb + A->ii * rta * rta + B->ii * rtb * rtb;
    p->tm = kt > 0 ? 1 / kt : 0;
  }
}

static inline void apply(Body *A, Body *B, float rax, float ray, float rbx, float rby, float px, float py) {
  float ima = A->pinned ? 0 : A->im, imb = B->pinned ? 0 : B->im;
  A->vx -= ima * px, A->vy -= ima * py, A->w -= A->ii * cross(rax, ray, px, py);
  B->vx += imb * px, B->vy += imb * py, B->w += B->ii * cross(rbx, rby, px, py);
}

static void warm_start(void) {
  for (int i = 0; i < W.nc; i++) {
    Contact *c = &W.c[i];
    float tx = c->ny, ty = -c->nx;
    for (int k = 0; k < c->n; k++) {
      CPoint *p = &c->p[k];
      apply(&W.b[c->a], &W.b[c->b], p->rax, p->ray, p->rbx, p->rby, p->ni * c->nx + p->ti * tx, p->ni * c->ny + p->ti * ty);
    }
  }
}

static void solve_contacts(void) {
  for (int i = 0; i < W.nc; i++) {
    Contact *c = &W.c[i];
    Body *A = &W.b[c->a], *B = &W.b[c->b];
    float tx = c->ny, ty = -c->nx;
    for (int k = 0; k < c->n; k++) {
      CPoint *p = &c->p[k];
      float dvx = B->vx - B->w * p->rby - A->vx + A->w * p->ray;
      float dvy = B->vy + B->w * p->rbx - A->vy - A->w * p->rax;
      float vt = dvx * tx + dvy * ty, lam = -p->tm * vt, maxf = c->fr * p->ni;
      float nt = p->ti + lam;
      nt = nt < -maxf ? -maxf : nt > maxf ? maxf : nt;
      lam = nt - p->ti, p->ti = nt;
      apply(A, B, p->rax, p->ray, p->rbx, p->rby, lam * tx, lam * ty);
    }
    for (int k = 0; k < c->n; k++) {
      CPoint *p = &c->p[k];
      float dvx = B->vx - B->w * p->rby - A->vx + A->w * p->ray;
      float dvy = B->vy + B->w * p->rbx - A->vy - A->w * p->rax;
      float vn = dvx * c->nx + dvy * c->ny, lam = -p->nm * vn;
      float nn = p->ni + lam;
      if (nn < 0) nn = 0;
      lam = nn - p->ni, p->ni = nn;
      apply(A, B, p->rax, p->ray, p->rbx, p->rby, lam * c->nx, lam * c->ny);
    }
  }
}

static bool solve_positions(void) {
  float minsep = 0;
  for (int i = 0; i < W.nc; i++) {
    Contact *c = &W.c[i];
    Body *A = &W.b[c->a], *B = &W.b[c->b];
    Man m;
    if (c->b) collide_bodies(A, B, &m);
    else collide_static(A, c->sid, &m);
    float ima = A->pinned ? 0 : A->im, imb = B->pinned ? 0 : B->im;
    for (int k = 0; k < m.n; k++) {
      float rax = m.px[k] - A->x, ray = m.py[k] - A->y, rbx = m.px[k] - B->x, rby = m.py[k] - B->y;
      float s = m.sep[k];
      if (s < minsep) minsep = s;
      float C = BAUM * (s + SLOP);
      C = C < -MAX_CORR ? -MAX_CORR : C > 0 ? 0 : C;
      float rna = cross(rax, ray, m.nx, m.ny), rnb = cross(rbx, rby, m.nx, m.ny);
      float K = ima + imb + A->ii * rna * rna + B->ii * rnb * rnb;
      float imp = K > 0 ? -C / K : 0, px = imp * m.nx, py = imp * m.ny;
      A->x -= ima * px, A->y -= ima * py, A->a -= A->ii * cross(rax, ray, px, py);
      B->x += imb * px, B->y += imb * py, B->a += B->ii * cross(rbx, rby, px, py);
      if (A->shape == SH_BOX) A->c = cosf(A->a), A->s = sinf(A->a);
      if (B->shape == SH_BOX) B->c = cosf(B->a), B->s = sinf(B->a);
      if (c->b == 0) {   /* (the ground does not move) */
        B->x = B->y = B->a = 0;
      }
    }
  }
  return minsep >= -3 * SLOP;
}

/* ---- joints (Box2D's b2RopeJoint, b2DistanceJoint, b2FrictionJoint) ---- */

static void local_anchor(const Body *b, float x, float y, float *lx, float *ly) {
  float dx = x - b->x, dy = y - b->y;
  *lx = b->c * dx + b->s * dy, *ly = -b->s * dx + b->c * dy;
}

static int new_joint(int type, int a, int b, float ax, float ay, float bx, float by) {
  if (W.nj >= MAX_JOINTS) return -1;
  Joint *j = &W.j[W.nj];
  memset(j, 0, sizeof *j);
  j->type = (uint8_t)type, j->alive = 1, j->a = (uint8_t)a, j->b = (uint8_t)b;
  local_anchor(&W.b[a], ax, ay, &j->lax, &j->lay);
  local_anchor(&W.b[b], bx, by, &j->lbx, &j->lby);
  return W.nj++;
}

int ph_rope(int a, int b, float ax, float ay, float bx, float by, float len) {
  int k = new_joint(J_ROPE, a, b, ax, ay, bx, by);
  if (k >= 0) W.j[k].len = len;
  return k;
}
int ph_dist(int a, int b, float ax, float ay, float bx, float by) {
  int k = new_joint(J_DIST, a, b, ax, ay, bx, by);
  if (k >= 0) W.j[k].len = sqrtf((bx - ax) * (bx - ax) + (by - ay) * (by - ay));
  return k;
}
int ph_fric(int a, int b, float ax, float ay, float maxf, float maxt) {
  int k = new_joint(J_FRIC, a, b, ax, ay, ax, ay);
  if (k >= 0) W.j[k].maxf = maxf, W.j[k].maxt = maxt;
  return k;
}

static void joint_rs(Joint *j, const Body *A, const Body *B) {
  j->rax = A->c * j->lax - A->s * j->lay, j->ray = A->s * j->lax + A->c * j->lay;
  j->rbx = B->c * j->lbx - B->s * j->lby, j->rby = B->s * j->lbx + B->c * j->lby;
}

void ph_joint_anchors(const Joint *j, float *ax, float *ay, float *bx, float *by) {
  const Body *A = &W.b[j->a], *B = &W.b[j->b];
  *ax = A->x + A->c * j->lax - A->s * j->lay, *ay = A->y + A->s * j->lax + A->c * j->lay;
  *bx = B->x + B->c * j->lbx - B->s * j->lby, *by = B->y + B->s * j->lbx + B->c * j->lby;
}

static void joints_init(void) {
  for (int k = 0; k < W.nj; k++) {
    Joint *j = &W.j[k];
    if (!j->alive) continue;
    Body *A = &W.b[j->a], *B = &W.b[j->b];
    float ima = A->pinned ? 0 : A->im, imb = B->pinned ? 0 : B->im;
    joint_rs(j, A, B);
    if (j->type == J_FRIC) {
      float a = ima + imb, ia = A->ii, ib = B->ii;
      float k11 = a + ia * j->ray * j->ray + ib * j->rby * j->rby, k12 = -ia * j->rax * j->ray - ib * j->rbx * j->rby;
      float k22 = a + ia * j->rax * j->rax + ib * j->rbx * j->rbx, det = k11 * k22 - k12 * k12;
      if (det != 0) det = 1 / det;
      j->m11 = det * k22, j->m12 = -det * k12, j->m22 = det * k11;
      j->am = ia + ib > 0 ? 1 / (ia + ib) : 0;
      apply(A, B, j->rax, j->ray, j->rbx, j->rby, j->fix, j->fiy);
      A->w -= ia * j->fia, B->w += ib * j->fia;
      continue;
    }
    float ux = B->x + j->rbx - A->x - j->rax, uy = B->y + j->rby - A->y - j->ray, l = sqrtf(ux * ux + uy * uy);
    j->mass = 0;
    if (l > SLOP) {
      ux /= l, uy /= l;
      float cra = cross(j->rax, j->ray, ux, uy), crb = cross(j->rbx, j->rby, ux, uy);
      float m = ima + A->ii * cra * cra + imb + B->ii * crb * crb;
      j->mass = m != 0 ? 1 / m : 0;
    } else {
      ux = uy = 0;
      if (j->type == J_ROPE) j->imp = 0;
    }
    j->ux = ux, j->uy = uy;
    j->ux = ux, j->uy = uy;
    /* (the rope keeps its length to tell how slack it is) */
    j->fix = l;
    apply(A, B, j->rax, j->ray, j->rbx, j->rby, j->imp * ux, j->imp * uy);
  }
}

static void joints_solve(void) {
  for (int k = 0; k < W.nj; k++) {
    Joint *j = &W.j[k];
    if (!j->alive) continue;
    Body *A = &W.b[j->a], *B = &W.b[j->b];
    float dvx = B->vx - B->w * j->rby - A->vx + A->w * j->ray;
    float dvy = B->vy + B->w * j->rbx - A->vy - A->w * j->rax;
    if (j->type == J_FRIC) {
      float ia = A->ii, ib = B->ii;
      float wd = B->w - A->w, imp = -j->am * wd, old = j->fia, maxi = DT * j->maxt;
      j->fia = old + imp;
      j->fia = j->fia < -maxi ? -maxi : j->fia > maxi ? maxi : j->fia;
      imp = j->fia - old;
      A->w -= ia * imp, B->w += ib * imp;
      dvx = B->vx - B->w * j->rby - A->vx + A->w * j->ray;
      dvy = B->vy + B->w * j->rbx - A->vy - A->w * j->rax;
      float ix = -(j->m11 * dvx + j->m12 * dvy), iy = -(j->m12 * dvx + j->m22 * dvy);
      float ox = j->fix, oy = j->fiy, maxf = DT * j->maxf;
      j->fix += ix, j->fiy += iy;
      float l2 = j->fix * j->fix + j->fiy * j->fiy;
      if (l2 > maxf * maxf) {
        float s = maxf / sqrtf(l2);
        j->fix *= s, j->fiy *= s;
      }
      apply(A, B, j->rax, j->ray, j->rbx, j->rby, j->fix - ox, j->fiy - oy);
      continue;
    }
    float cdot = j->ux * dvx + j->uy * dvy;
    float imp;
    if (j->type == J_ROPE) {
      float C = j->fix - j->len;
      if (C < 0) cdot += C / DT;
      imp = -j->mass * cdot;
      float old = j->imp;
      j->imp = old + imp < 0 ? old + imp : 0;
      imp = j->imp - old;
    } else {
      imp = -j->mass * cdot;
      j->imp += imp;
    }
    apply(A, B, j->rax, j->ray, j->rbx, j->rby, imp * j->ux, imp * j->uy);
  }
}

static bool joints_positions(void) {
  bool ok = true;
  for (int k = 0; k < W.nj; k++) {
    Joint *j = &W.j[k];
    if (!j->alive || j->type == J_FRIC) continue;
    Body *A = &W.b[j->a], *B = &W.b[j->b];
    float ima = A->pinned ? 0 : A->im, imb = B->pinned ? 0 : B->im;
    joint_rs(j, A, B);
    float ux = B->x + j->rbx - A->x - j->rax, uy = B->y + j->rby - A->y - j->ray, l = sqrtf(ux * ux + uy * uy);
    if (l < 1e-9f) continue;
    ux /= l, uy /= l;
    float C = l - j->len;
    if (j->type == J_ROPE) C = C < 0 ? 0 : C > MAX_CORR ? MAX_CORR : C;
    else C = C < -MAX_CORR ? -MAX_CORR : C > MAX_CORR ? MAX_CORR : C;
    float cra = cross(j->rax, j->ray, ux, uy), crb = cross(j->rbx, j->rby, ux, uy);
    float m = ima + A->ii * cra * cra + imb + B->ii * crb * crb;
    float imp = m > 0 ? -C / m : 0, px = imp * ux, py = imp * uy;
    A->x -= ima * px, A->y -= ima * py, A->a -= A->ii * cross(j->rax, j->ray, px, py);
    B->x += imb * px, B->y += imb * py, B->a += B->ii * cross(j->rbx, j->rby, px, py);
    A->c = cosf(A->a), A->s = sinf(A->a), B->c = cosf(B->a), B->s = sinf(B->a);
    if (j->a == 0) A->x = A->y = A->a = 0, A->c = 1, A->s = 0;
    if (j->b == 0) B->x = B->y = B->a = 0, B->c = 1, B->s = 0;
    if ((j->type == J_ROPE ? l - j->len : fabsf(l - j->len)) > SLOP) ok = false;
  }
  return ok;
}

/* a fast circle may not jump through a wall: it stops where its sweep first meets one (Box2D sweeps every
 * moving body against the static ones) */
static float sweep_circle(float x0, float y0, float dx, float dy, float cx, float cy, float R) {
  float fx = x0 - cx, fy = y0 - cy;
  float a = dx * dx + dy * dy, b = fx * dx + fy * dy, c = fx * fx + fy * fy - R * R;
  if (c < 0 || a < 1e-12f) return 1;   /* already touching: the contact handles it */
  float disc = b * b - a * c;
  if (disc < 0) return 1;
  float t = (-b - sqrtf(disc)) / a;
  return t >= 0 && t < 1 ? t : 1;
}

static void sweep(Body *b) {
  float dx = b->x - b->sx, dy = b->y - b->sy;
  if (dx * dx + dy * dy < 0.25f * b->r * b->r) return;
  float R = b->r - SLOP, best = 1;
  float x0 = fminf(b->sx, b->x) - b->r, x1 = fmaxf(b->sx, b->x) + b->r;
  float y0 = fminf(b->sy, b->y) - b->r, y1 = fmaxf(b->sy, b->y) + b->r;
  if (++W.stamp_now == 0) memset(W.stamp, 0, sizeof W.stamp), W.stamp_now = 1;
  for (int cy = cell_of(y0); cy <= cell_of(y1); cy++)
    for (int cx = cell_of(x0); cx <= cell_of(x1); cx++) {
      int c = cy * GRID + cx;
      for (int t = W.cell_start[c]; t < W.cell_start[c + 1]; t++) {
        int s = W.cell_items[t];
        if (W.stamp[s] == W.stamp_now) continue;
        W.stamp[s] = W.stamp_now;
        float hit = 1;
        if (s < MAX_SCIRCLES) hit = sweep_circle(b->sx, b->sy, dx, dy, W.sc[s].x, W.sc[s].y, W.sc[s].r + R);
        else if (s < MAX_SCIRCLES + MAX_SEDGES) {
          const SEdge *e = &W.se[s - MAX_SCIRCLES];
          float ex = e->x2 - e->x1, ey = e->y2 - e->y1, el = sqrtf(ex * ex + ey * ey);
          if (el < 1e-9f) continue;
          float nx = ey / el, ny = -ex / el;
          float d0 = (b->sx - e->x1) * nx + (b->sy - e->y1) * ny, dd = dx * nx + dy * ny;
          if (d0 < 0) d0 = -d0, dd = -dd;
          if (d0 > R && dd < 0) {
            float t = (d0 - R) / -dd;
            if (t < 1) {
              float px = b->sx + dx * t, py = b->sy + dy * t;
              float u = ((px - e->x1) * ex + (py - e->y1) * ey) / (el * el);
              if (u >= 0 && u <= 1) hit = t;
            }
          }
          float h1 = sweep_circle(b->sx, b->sy, dx, dy, e->x1, e->y1, R), h2 = sweep_circle(b->sx, b->sy, dx, dy, e->x2, e->y2, R);
          if (h1 < hit) hit = h1;
          if (h2 < hit) hit = h2;
        }
        if (hit < best) best = hit;
      }
    }
  if (best < 1) b->x = b->sx + dx * best, b->y = b->sy + dy * best;
}

void ph_step(void) {
  find_contacts();
  for (int i = 1; i < W.nb; i++) {
    Body *b = &W.b[i];
    if (!b->alive) continue;
    b->sx = b->x, b->sy = b->y;
    if (!b->pinned && b->im > 0) {
      b->vx += DT * W.gx, b->vy += DT * W.gy;
      b->vx *= 1 / (1 + DT * b->ld), b->vy *= 1 / (1 + DT * b->ld);
    }
    if (b->pinned) b->vx = b->vy = 0;
    b->w *= 1 / (1 + DT * b->ad);
  }
  for (int i = 0; i < W.nc; i++) contact_masses(&W.c[i]);
  warm_start();
  joints_init();
  for (int it = 0; it < ITER_V; it++) {
    joints_solve();
    solve_contacts();
  }
  for (int i = 1; i < W.nb; i++) {
    Body *b = &W.b[i];
    if (!b->alive) continue;
    float tx = DT * b->vx, ty = DT * b->vy;
    if (tx * tx + ty * ty > MAX_TRANS * MAX_TRANS) {
      float s = MAX_TRANS / sqrtf(tx * tx + ty * ty);
      b->vx *= s, b->vy *= s;
    }
    float r = DT * b->w;
    if (r * r > MAX_ROT * MAX_ROT) b->w *= MAX_ROT / fabsf(r);
    if (!b->pinned) b->x += DT * b->vx, b->y += DT * b->vy;
    b->a += DT * b->w;
    b->c = cosf(b->a), b->s = sinf(b->a);
  }
  for (int it = 0; it < ITER_P; it++) {
    bool a = solve_positions();
    bool b = joints_positions();
    if (a && b) break;
  }
  for (int i = 1; i < W.nb; i++)
    if (W.b[i].alive && W.b[i].shape == SH_CIRCLE && W.b[i].im > 0) sweep(&W.b[i]);
}

bool ph_overlaps(int i, float x, float y, float r) {
  const Body *b = &W.b[i];
  if (b->shape == SH_CIRCLE) return (b->x - x) * (b->x - x) + (b->y - y) * (b->y - y) < (b->r + r) * (b->r + r);
  SPoly p;
  body_poly(b, &p);
  Man m;
  coll_pc(&p, x, y, r, &m);
  return m.n > 0;
}
