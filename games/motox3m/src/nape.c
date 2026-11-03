/* Moto X3M: the physics, ported from Nape 2's cx sources (zpp_nape/space/Space.cx, dynamics/Arbiter.cx,
 * geom/Collide.cx, the constraints, phys/Body.cx, shape/Polygon.cx) as the game sets it up: Space.step with 10
 * velocity and 10 position iterations, world drag 0.015, Nape's Config values. See nape.h. */
#include <math.h>
#include <string.h>
#include "nape.h"

Space sp;

#define EPS 1e-8f
#define FMAX 3.4e38f
#define SLOP 0.2f
#define EXPIRE 6
#define STATIC_FRICTION_THRESHOLD 2.0f
#define ELASTIC_THRESHOLD 20.0f
#define SLEEP_DELAY 60
#define LIN_SLEEP 0.2f
#define ANG_SLEEP 0.4f
#define BIAS_DYN 0.3f
#define BIAS_STATIC 0.6f
#define LIN_SLOP 0.1f
#define ANG_SLOP 1e-3f
#define ILL_COND 2e8f
#define DRAG 0.015f
#define TAU 6.28318530718f
#define VEL_ITER 10
#define POS_ITER 10

static Body world;   /* every static body: at the origin, never turned */
static int16_t htab[256];
static int16_t hnext[NP_MAX_ARBS];

static inline float cross(float ax, float ay, float bx, float by) { return ax * by - ay * bx; }

static void delta_rot(Body *b, float dr) {
  b->rot += dr;
  if (dr * dr > 0.0001f) {
    b->axis.x = sinf(b->rot), b->axis.y = cosf(b->rot);
  } else {
    float d2 = dr * dr, p = 1 - 0.5f * d2, m = 1 - d2 * d2 / 8;
    float nx = (p * b->axis.x + dr * b->axis.y) * m;
    b->axis.y = (p * b->axis.y - dr * b->axis.x) * m;
    b->axis.x = nx;
  }
}

void np_body_set_rot(Body *b, float r) {
  b->rot = r;
  b->axis.x = sinf(r), b->axis.y = cosf(r);
}

void np_body_mass(Body *b) {
  float m = 0, in = 0, cx = 0, cy = 0;
  for (int i = 0; i < b->nshapes; i++) {
    Shape *s = &b->shapes[i];
    float sm = s->area * sp.mats[s->mat].density / 1000;
    m += sm;
    in += s->inertia * sm;
    cx += s->lcom.x * sm, cy += s->lcom.y * sm;
  }
  b->lcom = m ? v2(cx / m, cy / m) : v2(0, 0);
  bool dyn = b->type == B_DYNAMIC;
  b->mass = m;
  b->imass = dyn && !(b->flags & BF_NOMOVE) && m > 0 ? 1 / m : 0;
  b->iinertia = dyn && !(b->flags & BF_NOROT) && in > 0 ? 1 / in : 0;
}

void np_reset(void) {
  memset(&sp, 0, sizeof sp);
  memset(&world, 0, sizeof world);
  world.axis = v2(0, 1);
  world.type = B_STATIC;
  world.flags = BF_SPACE;
  for (int i = 0; i < NP_MAX_ARBS; i++) sp.arbs[i].next = (int16_t)(i + 1 < NP_MAX_ARBS ? i + 1 : -1);
  sp.free_arb = 0;
  for (int i = 0; i < 256; i++) htab[i] = -1;
}

/* ---- shapes as the narrow phase sees them ---- */

#define SV_MAX 48

typedef struct {
  uint16_t id;
  uint8_t type, n, flags, filter;
  const Material *mat;
  Body *body;
  int sbody;              /* static body, or -1 */
  float radius;
  V2 wcom, lcom;
  const V2 *gv, *gn;
  const float *gp;
  float minx, miny, maxx, maxy;
  V2 vb[SV_MAX], nb[SV_MAX];   /* a static shape's, decoded */
  float pb[SV_MAX];
} SV;

static const float UNIT[4] = {0.25f, 0.5f, 1, 2};

static void edges(const V2 *v, int n, V2 *nrm, float *proj) {
  for (int i = 0; i < n; i++) {
    V2 u = v[i], w = v[i + 1 < n ? i + 1 : 0];
    float dx = u.x - w.x, dy = u.y - w.y, l = sqrtf(dx * dx + dy * dy);
    if (l > 0) dx /= l, dy /= l;
    nrm[i] = v2(-dy, dx);
    proj[i] = -dy * u.x + dx * u.y;
  }
}

static void sview(uint16_t id, SV *v) {
  v->id = id;
  if (id & 0x8000) {
    Shape *s = sp.shapes[id & 0x7FFF];
    v->type = s->type, v->n = s->n, v->flags = s->flags, v->filter = s->filter;
    v->mat = &sp.mats[s->mat], v->body = s->body, v->sbody = -1;
    v->radius = s->radius, v->wcom = s->wcom, v->lcom = s->lcom;
    v->gv = (const V2 *)(const void *)s->glob, v->gn = (const V2 *)(const void *)(s->glob + 2 * s->n);
    v->gp = s->glob + 4 * s->n;
    v->minx = s->minx, v->miny = s->miny, v->maxx = s->maxx, v->maxy = s->maxy;
  } else {
    const SShape *s = &sp.sshapes[id];
    const int16_t *q = sp.sverts + 2 * s->voff;
    float k = UNIT[(s->flags >> 4) & 3];
    v->type = SH_POLY, v->n = s->n, v->flags = s->flags & 15, v->filter = s->filter;
    v->mat = &sp.mats[s->mat], v->body = &world, v->sbody = s->body;
    v->radius = 0;
    for (int i = 0; i < s->n; i++) v->vb[i] = v2(s->x0 + q[2 * i] * k, s->y0 + q[2 * i + 1] * k);
    edges(v->vb, s->n, v->nb, v->pb);
    v->gv = v->vb, v->gn = v->nb, v->gp = v->pb;
    v->minx = s->x0, v->miny = s->y0, v->maxx = s->x0 + s->w, v->maxy = s->y0 + s->h;
  }
}

/* a face's normal and projection in its body's own coordinates (for the position iterations) */
static void local_face(const SV *p, int e, V2 *ln, float *lp) {
  const Body *b = p->body;
  V2 g = p->gn[e];
  *ln = v2(g.x * b->axis.y + g.y * b->axis.x, g.y * b->axis.y - g.x * b->axis.x);
  *lp = p->gp[e] - (g.x * b->pos.x + g.y * b->pos.y);
}

static void shape_world(Shape *s) {
  Body *b = s->body;
  s->wcom = np_local_to_world(b, s->lcom);
  if (s->type == SH_CIRCLE) {
    s->minx = s->wcom.x - s->radius, s->maxx = s->wcom.x + s->radius;
    s->miny = s->wcom.y - s->radius, s->maxy = s->wcom.y + s->radius;
    return;
  }
  float mnx = FMAX, mny = FMAX, mxx = -FMAX, mxy = -FMAX;
  const V2 *lv = (const V2 *)(const void *)s->loc;
  V2 *gv = (V2 *)(void *)s->glob;
  for (int i = 0; i < s->n; i++) {
    V2 g = np_local_to_world(b, lv[i]);
    gv[i] = g;
    if (g.x < mnx) mnx = g.x;
    if (g.x > mxx) mxx = g.x;
    if (g.y < mny) mny = g.y;
    if (g.y > mxy) mxy = g.y;
  }
  edges(gv, s->n, (V2 *)(void *)(s->glob + 2 * s->n), s->glob + 4 * s->n);
  s->minx = mnx, s->miny = mny, s->maxx = mxx, s->maxy = mxy;
}

static void body_world(Body *b) {
  b->minx = b->miny = FMAX, b->maxx = b->maxy = -FMAX;
  for (int i = 0; i < b->nshapes; i++) {
    Shape *s = &b->shapes[i];
    shape_world(s);
    if (s->minx < b->minx) b->minx = s->minx;
    if (s->miny < b->miny) b->miny = s->miny;
    if (s->maxx > b->maxx) b->maxx = s->maxx;
    if (s->maxy > b->maxy) b->maxy = s->maxy;
  }
}

/* ---- arbiters ---- */

static inline unsigned hkey(uint16_t a, uint16_t b) {
  if (a > b) { uint16_t t = a; a = b; b = t; }
  return ((unsigned)a * 31u + b * 7u) & 255u;
}

static Arb *arb_find(uint16_t a, uint16_t b) {
  for (int i = htab[hkey(a, b)]; i >= 0; i = hnext[i]) {
    Arb *r = &sp.arbs[i];
    if ((r->s1 == a && r->s2 == b) || (r->s1 == b && r->s2 == a)) return r;
  }
  return 0;
}

static void arb_free(Arb *r);
static Body *shape_body(uint16_t id);

/* all slots in use: a contact of the bike's takes a level object's (the least useful one) */
static bool arb_evict(void) {
  int best = -1, score = -1;
  for (int i = 0; i < NP_MAX_ARBS; i++) {
    Arb *r = &sp.arbs[i];
    if (!r->used || r->present || (r->b1->flags & BF_KEEP) || (r->b2->flags & BF_KEEP)) continue;
    int sc = r->up_stamp != sp.stamp ? 3 : r->type == A_SENSOR ? 2 : r->b1->type != B_DYNAMIC || r->b2->type != B_DYNAMIC ? 0 : 1;
    if (sc > score) score = sc, best = i;
    if (sc == 3) break;
  }
  if (best < 0) return false;
  arb_free(&sp.arbs[best]);
  return true;
}

static Arb *arb_new(uint16_t a, uint16_t b) {
  if (sp.free_arb < 0) {
    sp.overflow = true;
    Body *ba = shape_body(a), *bb = shape_body(b);
    if (!((ba->flags | bb->flags) & BF_KEEP) || !arb_evict()) return 0;
  }
  int i = sp.free_arb;
  Arb *r = &sp.arbs[i];
  sp.free_arb = r->next;
  memset(r, 0, sizeof *r);
  r->used = 1;
  r->s1 = a, r->s2 = b;
  r->c1 = r->c2 = -1;
  unsigned h = hkey(a, b);
  hnext[i] = htab[h];
  htab[h] = (int16_t)i;
  return r;
}

static void arb_free(Arb *r) {
  int i = (int)(r - sp.arbs);
  unsigned h = hkey(r->s1, r->s2);
  int16_t *p = &htab[h];
  while (*p >= 0 && *p != i) p = &hnext[*p];
  if (*p == i) *p = hnext[i];
  r->used = 0;
  r->next = sp.free_arb;
  sp.free_arb = (int16_t)i;
}

static int sbody_of(uint16_t id) { return id & 0x8000 ? -1 : sp.sshapes[id].body; }
static Body *shape_body(uint16_t id) { return id & 0x8000 ? sp.shapes[id & 0x7FFF]->body : &world; }
static const Material *shape_mat(uint16_t id) {
  return &sp.mats[id & 0x8000 ? sp.shapes[id & 0x7FFF]->mat : sp.sshapes[id].mat];
}

static void push_event(bool begin, const Arb *a) {
  if (sp.nevents >= NP_MAX_EVENTS) return;
  Event *e = &sp.events[sp.nevents++];
  e->begin = begin, e->type = a->type;
  e->sb1 = sbody_of(a->s1), e->sb2 = sbody_of(a->s2);
  e->b1 = e->sb1 < 0 ? a->b1 : 0;
  e->b2 = e->sb2 < 0 ? a->b2 : 0;
  e->px = e->py = 0;
  if (a->type == A_COL && a->nc) {
    const Contact *c = &a->c[a->c1 >= 0 ? a->c1 : 0];
    e->px = c->r1x + a->b1->pos.x, e->py = c->r1y + a->b1->pos.y;
  }
}

/* the same two bodies (static ones by their own index) */
static bool same_pair(const Arb *a, const Arb *b) {
  int a1 = sbody_of(a->s1), a2 = sbody_of(a->s2), b1 = sbody_of(b->s1), b2 = sbody_of(b->s2);
  const void *pa1 = a1 < 0 ? (const void *)a->b1 : 0, *pa2 = a2 < 0 ? (const void *)a->b2 : 0;
  const void *pb1 = b1 < 0 ? (const void *)b->b1 : 0, *pb2 = b2 < 0 ? (const void *)b->b2 : 0;
  return (pa1 == pb1 && a1 == b1 && pa2 == pb2 && a2 == b2) || (pa1 == pb2 && a1 == b2 && pa2 == pb1 && a2 == b1);
}

static bool pair_present(const Arb *a) {
  for (int i = 0; i < NP_MAX_ARBS; i++) {
    const Arb *o = &sp.arbs[i];
    if (o != a && o->used && o->present && o->type == a->type && same_pair(o, a)) return true;
  }
  return false;
}

static void arb_retire(Arb *a, bool events) {
  if (a->present) {
    a->present = 0;
    if (events && !pair_present(a)) push_event(false, a);
  }
  arb_free(a);
}

void np_drop_body(Body *b) {
  for (int i = 0; i < NP_MAX_ARBS; i++) {
    Arb *a = &sp.arbs[i];
    if (a->used && (a->b1 == b || a->b2 == b)) arb_retire(a, true);
  }
}

void np_drop_static(int sb) {
  for (int i = 0; i < NP_MAX_ARBS; i++) {
    Arb *a = &sp.arbs[i];
    if (a->used && (sbody_of(a->s1) == sb || sbody_of(a->s2) == sb)) arb_retire(a, true);
  }
}

Body *np_world(void) { return &world; }

void np_drop_shape(Shape *s) {
  for (int i = 0; i < NP_MAX_ARBS; i++) {
    Arb *a = &sp.arbs[i];
    if (!a->used) continue;
    uint16_t id1 = a->s1, id2 = a->s2;
    if (((id1 & 0x8000) && sp.shapes[id1 & 0x7FFF] == s) || ((id2 & 0x8000) && sp.shapes[id2 & 0x7FFF] == s))
      arb_retire(a, true);
  }
}

static Contact *inject(Arb *a, float px, float py, float nx, float ny, float dist, int hash, bool posonly) {
  Contact *c = 0;
  for (int i = 0; i < a->nc; i++)
    if (a->c[i].hash == hash) { c = &a->c[i]; break; }
  if (!c) {
    if (a->nc >= 2) return &a->c[0];
    c = &a->c[a->nc++];
    memset(c, 0, sizeof *c);
    c->hash = (uint8_t)hash;
    c->fresh = 1;
    a->jr = 0;
  } else c->fresh = 0;
  c->r1x = px - a->b1->pos.x, c->r1y = py - a->b1->pos.y;
  c->r2x = px - a->b2->pos.x, c->r2y = py - a->b2->pos.y;
  a->n = v2(nx, ny);
  c->dist = dist;
  c->stamp = sp.stamp;
  c->posonly = posonly;
  return c;
}

static Contact *circle_query(Arb *a, V2 p1, V2 p2, float r1, float r2, bool rev, int hash) {
  float min = r1 + r2, px = p2.x - p1.x, py = p2.y - p1.y, d2 = px * px + py * py;
  if (d2 > min * min) return 0;
  if (d2 < EPS * EPS) return inject(a, p1.x, p1.y, 1, 0, -min, hash, false);
  float inv = 1 / sqrtf(d2), dist = inv < EPS ? FMAX : 1 / inv, df = 0.5f + (r1 - 0.5f * min) * inv;
  if (rev) return inject(a, p1.x + px * df, p1.y + py * df, -px * inv, -py * inv, dist - min, hash, false);
  return inject(a, p1.x + px * df, p1.y + py * df, px * inv, py * inv, dist - min, hash, false);
}

static inline V2 revrot(V2 a, V2 axis) { return v2(a.x * axis.y + a.y * axis.x, a.y * axis.y - a.x * axis.x); }

static bool circle2circle(const SV *c1, const SV *c2, Arb *a, bool rev) {
  Contact *c = circle_query(a, c1->wcom, c2->wcom, c1->radius, c2->radius, rev, 0);
  if (!c) return false;
  if (rev) c->lr1x = c2->lcom.x, c->lr1y = c2->lcom.y, c->lr2x = c1->lcom.x, c->lr2y = c1->lcom.y;
  else c->lr1x = c1->lcom.x, c->lr1y = c1->lcom.y, c->lr2x = c2->lcom.x, c->lr2y = c2->lcom.y;
  a->radius = c1->radius + c2->radius;
  a->ptype = PT_CIRCLE;
  return true;
}

static bool circle2poly(const SV *c, const SV *p, Arb *a, bool rev) {
  float max = -FMAX;
  int a0 = -1;
  for (int i = 0; i < p->n; i++) {
    float d = p->gn[i].x * c->wcom.x + p->gn[i].y * c->wcom.y - p->gp[i] - c->radius;
    if (d > 0) return false;
    if (d > max) max = d, a0 = i;
  }
  V2 v0 = p->gv[a0], v1 = p->gv[a0 + 1 < p->n ? a0 + 1 : 0], g = p->gn[a0];
  float dt = cross(g.x, g.y, c->wcom.x, c->wcom.y);
  for (int k = 0; k < 2; k++) {
    V2 vv = k ? v1 : v0;
    bool hit = k ? dt >= cross(g.x, g.y, v1.x, v1.y) : dt <= cross(g.x, g.y, v0.x, v0.y);
    if (!hit) continue;
    Contact *co = circle_query(a, c->wcom, vv, c->radius, 0, rev, 0);
    if (co) {
      a->ptype = PT_CIRCLE;
      V2 l = revrot(v2(vv.x - p->body->pos.x, vv.y - p->body->pos.y), p->body->axis);
      if (rev) co->lr1x = l.x, co->lr1y = l.y, co->lr2x = c->lcom.x, co->lr2y = c->lcom.y;
      else co->lr2x = l.x, co->lr2y = l.y, co->lr1x = c->lcom.x, co->lr1y = c->lcom.y;
      a->radius = c->radius;
    }
    return co != 0;
  }
  float k = c->radius + max * 0.5f;
  float px = c->wcom.x - g.x * k, py = c->wcom.y - g.y * k;
  Contact *co = rev ? inject(a, px, py, g.x, g.y, max, 0, false) : inject(a, px, py, -g.x, -g.y, max, 0, false);
  a->ptype = rev ? PT_FACE1 : PT_FACE2;
  local_face(p, a0, &a->lnorm, &a->lproj);
  a->rev = !rev;
  a->radius = c->radius;
  co->lr1x = c->lcom.x, co->lr1y = c->lcom.y;
  return true;
}

static bool poly2poly(const SV *p1, const SV *p2, Arb *a, bool rev) {
  float max = -FMAX;
  int maxi = -1, ax1 = -1, ax2 = -1;
  for (int pass = 0; pass < 2; pass++) {
    const SV *pa = pass ? p2 : p1, *pb = pass ? p1 : p2;
    for (int e = 0; e < pa->n; e++) {
      float min = FMAX, gx = pa->gn[e].x, gy = pa->gn[e].y, gp = pa->gp[e];
      for (int i = 0; i < pb->n; i++) {
        float k = gx * pb->gv[i].x + gy * pb->gv[i].y;
        if (k < min) min = k;
        if (min - gp <= max) break;
      }
      min -= gp;
      if (min >= 0) return false;
      if (min > max) {
        max = min;
        if (pass) ax2 = e; else ax1 = e;
        maxi = pass + 1;
      }
    }
  }
  const SV *q1, *q2;
  int ax;
  float scale;
  if (maxi == 1) q1 = p1, q2 = p2, ax = ax1, scale = 1;
  else q1 = p2, q2 = p1, ax = ax2, scale = -1;
  V2 g = q1->gn[ax];
  float gproj = q1->gp[ax];
  int n1 = q1->n;
  V2 g0 = q1->gv[ax], g1 = q1->gv[ax + 1 < n1 ? ax + 1 : 0];
  float tp0 = cross(g.x, g.y, g0.x, g0.y), tp1 = cross(g.x, g.y, g1.x, g1.y);
  int ay = 0;
  float min = FMAX;
  for (int e = 0; e < q2->n; e++) {
    float k = g.x * q2->gn[e].x + g.y * q2->gn[e].y;
    if (k < min) min = k, ay = e;
  }
  V2 c0 = q2->gv[ay], c1 = q2->gv[ay + 1 < q2->n ? ay + 1 : 0];
  float dvx = c1.x - c0.x, dvy = c1.y - c0.y;
  float d0 = cross(c0.x, c0.y, g.x, g.y), d1 = cross(c1.x, c1.y, g.x, g.y), den = 1 / (d1 - d0);
  float t = (-tp1 - d0) * den;
  if (t > EPS) c0.x += dvx * t, c0.y += dvy * t;
  t = (-tp0 - d1) * den;
  if (t < -EPS) c1.x += dvx * t, c1.y += dvy * t;
  float nx = g.x * scale, ny = g.y * scale;
  local_face(q1, ax, &a->lnorm, &a->lproj);
  a->radius = 0;
  a->rev = rev != (scale == -1);
  a->ptype = a->rev ? PT_FACE2 : PT_FACE1;
  float c0d = c0.x * g.x + c0.y * g.y - gproj, c1d = c1.x * g.x + c1.y * g.y - gproj;
  if (c0d > 0 && c1d > 0) return false;
  if (rev) nx = -nx, ny = -ny;
  Body *qb = q2->body;
  Contact *con = inject(a, c0.x - g.x * c0d * 0.5f, c0.y - g.y * c0d * 0.5f, nx, ny, c0d, a->rev ? 1 : 0, c0d > 0);
  V2 l = revrot(v2(c0.x - qb->pos.x, c0.y - qb->pos.y), qb->axis);
  con->lr1x = l.x, con->lr1y = l.y;
  con = inject(a, c1.x - g.x * c1d * 0.5f, c1.y - g.y * c1d * 0.5f, nx, ny, c1d, a->rev ? 0 : 1, c1d > 0);
  l = revrot(v2(c1.x - qb->pos.x, c1.y - qb->pos.y), qb->axis);
  con->lr1x = l.x, con->lr1y = l.y;
  return true;
}

static bool test_circle_poly(const SV *c, const SV *p) {
  float max = -FMAX;
  int a0 = -1;
  for (int i = 0; i < p->n; i++) {
    float d = p->gn[i].x * c->wcom.x + p->gn[i].y * c->wcom.y - p->gp[i] - c->radius;
    if (d > 0) return false;
    if (d > max) max = d, a0 = i;
  }
  V2 v0 = p->gv[a0], v1 = p->gv[a0 + 1 < p->n ? a0 + 1 : 0], g = p->gn[a0];
  float dt = cross(g.x, g.y, c->wcom.x, c->wcom.y);
  V2 v;
  if (dt <= cross(g.x, g.y, v0.x, v0.y)) v = v0;
  else if (dt >= cross(g.x, g.y, v1.x, v1.y)) v = v1;
  else return true;
  float dx = v.x - c->wcom.x, dy = v.y - c->wcom.y;
  return dx * dx + dy * dy <= c->radius * c->radius;
}

static bool test_collide(const SV *s1, const SV *s2) {
  if (s1->type == SH_CIRCLE && s2->type == SH_CIRCLE) {
    float dx = s2->wcom.x - s1->wcom.x, dy = s2->wcom.y - s1->wcom.y, r = s1->radius + s2->radius;
    return dx * dx + dy * dy <= r * r;
  }
  if (s1->type == SH_CIRCLE) return test_circle_poly(s1, s2);
  if (s2->type == SH_CIRCLE) return test_circle_poly(s2, s1);
  for (int pass = 0; pass < 2; pass++) {
    const SV *pa = pass ? s2 : s1, *pb = pass ? s1 : s2;
    for (int e = 0; e < pa->n; e++) {
      float min = FMAX;
      for (int i = 0; i < pb->n; i++) {
        float k = pa->gn[e].x * pb->gv[i].x + pa->gn[e].y * pb->gv[i].y;
        if (k < min) min = k;
      }
      if (min - pa->gp[e] > 0) return false;
    }
  }
  return true;
}

/* a constraint with ignore set between the two bodies (a static one by its index: sb) */
static bool ignored(const Body *b1, const Body *b2, int sb) {
  for (int i = 0; i < sp.ncons; i++) {
    const Con *c = sp.cons[i];
    if (!(c->flags & CF_IGNORE)) continue;
    if ((c->b1 == b1 && c->b2 == b2) || (c->b1 == b2 && c->b2 == b1)) {
      if (b1 != &world && b2 != &world) return true;
      if (c->sb == sb) return true;
    }
  }
  return false;
}

/* Space.interactionType: 2 sensor, 1 collision, -1 none */
static int itype(const SV *s1, const SV *s2) {
  const Body *b1 = s1->body, *b2 = s2->body;
  if (ignored(b1, b2, s1->sbody >= 0 ? s1->sbody : s2->sbody)) return -1;
  const Filter *f1 = &sp.filters[s1->filter], *f2 = &sp.filters[s2->filter];
  if ((s1->flags & SF_SENSOR) || (s2->flags & SF_SENSOR)) {
    if ((f1->sgroup & f2->smask) && (f2->sgroup & f1->smask)) return 2;
  }
  /* fluids: every filter flows (fluidGroup 1, fluidMask -1) */
  if (((s1->flags & SF_FLUID) || (s2->flags & SF_FLUID)) &&
      !(b1->imass == 0 && b2->imass == 0 && b1->iinertia == 0 && b2->iinertia == 0))
    return 0;
  if ((f1->cgroup & f2->cmask) && (f2->cgroup & f1->cmask) &&
      !(b1->imass == 0 && b2->imass == 0 && b1->iinertia == 0 && b2->iinertia == 0))
    return 1;
  return -1;
}

/* ---- fluids (water): the overlap's area and centre, by clipping (the game's water is static and convex) ---- */

static float soft_gamma(float freq, float damping, float dt, float *gamma, float *biascoef);

static int clip_poly(const V2 *in, int n, V2 nrm, float proj, V2 *out) {
  int m = 0;
  for (int i = 0; i < n; i++) {
    V2 a = in[i], b = in[(i + 1) % n];
    float da = nrm.x * a.x + nrm.y * a.y - proj, db = nrm.x * b.x + nrm.y * b.y - proj;
    if (da <= 0) out[m++] = a;
    if ((da < 0) != (db < 0) && da != db) {
      float t = da / (da - db);
      out[m++] = v2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
    }
  }
  return m;
}

static bool flow_collide(const SV *s1, const SV *s2, Arb *a) {
  const SV *fl = s1->flags & SF_FLUID ? s1 : s2, *o = fl == s1 ? s2 : s1;
  if (fl->type != SH_POLY) return false;
  V2 buf[2][SV_MAX + 24];
  int n;
  if (o->type == SH_CIRCLE) {
    n = 16;
    for (int i = 0; i < n; i++) {
      float t = i * (TAU / 16);
      buf[0][i] = v2(o->wcom.x + o->radius * cosf(t), o->wcom.y + o->radius * sinf(t));
    }
  } else {
    n = o->n;
    for (int i = 0; i < n; i++) buf[0][i] = o->gv[i];
  }
  int cur = 0;
  for (int e = 0; e < fl->n && n > 0; e++) {
    n = clip_poly(buf[cur], n, fl->gn[e], fl->gp[e], buf[cur ^ 1]);
    cur ^= 1;
    if (n > SV_MAX + 16) n = SV_MAX + 16;
  }
  if (n < 3) return false;
  float area = 0, cx = 0, cy = 0;
  const V2 *p = buf[cur];
  for (int i = 0; i < n; i++) {
    V2 u = p[i], w = p[(i + 1) % n];
    float c = u.x * w.y - w.x * u.y;
    area += c;
    cx += (u.x + w.x) * c, cy += (u.y + w.y) * c;
  }
  if (area == 0) return false;
  cx /= 3 * area, cy /= 3 * area;
  area = fabsf(area) * 0.5f;
  if (area < 1e-4f) return false;
  a->f.overlap = area, a->f.cx = cx, a->f.cy = cy;
  return true;
}

/* Polygon.__validate_angDrag and Circle.__validate_angDrag */
static float ang_drag(const SV *v) {
  float dyn = v->mat->dyn;
  if (v->type == SH_CIRCLE) {
    float lc = v->lcom.x * v->lcom.x + v->lcom.y * v->lcom.y, r2 = v->radius * v->radius, skin = dyn * 2.5f;
    return ((lc + 2 * r2) * skin + 0.5f * 100 * 1.5f * lc) / (2 * (lc + 0.5f * r2));
  }
  const Shape *sh = (v->id & 0x8000) ? sp.shapes[v->id & 0x7FFF] : 0;
  float inertia = sh ? sh->inertia : 1;
  float acc = 0, perim = 0;
  const Body *b = v->body;
  for (int i = 0; i < v->n; i++) {
    /* in the body's own coordinates */
    V2 gu = v->gv[i], gw = v->gv[(i + 1) % v->n];
    V2 u = np_world_to_local(b, gu), w = np_world_to_local(b, gw);
    V2 ln;
    float lp;
    local_face(v, i, &ln, &lp);
    float dx = w.x - u.x, dy = w.y - u.y, len = sqrtf(dx * dx + dy * dy);
    perim += len;
    acc += len * 2.5f * dyn * lp * lp;
    float den = cross(ln.x, ln.y, dx, dy);
    float t = den != 0 ? -cross(ln.x, ln.y, u.x, u.y) / den : 2;
    if (t > 0) {
      float ta = t > 1 ? 1 : t, ccx = u.x + dx * ta, ccy = u.y + dy * ta;
      float da = cross(u.x, u.y, ln.x, ln.y), db = cross(ccx, ccy, ln.x, ln.y);
      if (db != da) acc += (db * db * db - da * da * da) / (3 * (db - da)) * ta * len * 100;
    }
    if (t < 1) {
      float tb = t < 0 ? 0 : t, ccx = u.x + dx * tb, ccy = u.y + dy * tb;
      float da = cross(ccx, ccy, ln.x, ln.y), db = cross(w.x, w.y, ln.x, ln.y);
      if (db != da) acc += (db * db * db - da * da * da) / (3 * (db - da)) * 0.5f * (1 - tb) * len * 100;
    }
  }
  return perim > 0 ? acc / (inertia * perim) : 0;
}

static float shape_area(const SV *v) {
  if (v->type == SH_CIRCLE) return 3.14159265f * v->radius * v->radius;
  const Shape *sh = (v->id & 0x8000) ? sp.shapes[v->id & 0x7FFF] : 0;
  if (sh) return sh->area;
  float a = 0;
  for (int i = 0; i < v->n; i++) {
    V2 u = v->gv[i], w = v->gv[(i + 1) % v->n];
    a += u.x * w.y - w.x * u.y;
  }
  return fabsf(a) * 0.5f;
}

static float lin_drag(const SV *other, float f, V2 n) {
  if (other->type == SH_CIRCLE) return -f * other->radius * 0.5f / (2 * other->radius * 3.14159265f);
  float bord = 0, acc = 0;
  for (int i = 0; i < other->n; i++) {
    V2 u = other->gv[i], w = other->gv[(i + 1) % other->n];
    float len = hypotf(w.x - u.x, w.y - u.y);
    bord += len;
    float fact = f * len * (other->gn[i].x * n.x + other->gn[i].y * n.y);
    if (fact > 0) fact *= -0.5f;
    acc -= fact * 0.5f * 0.5f;
  }
  return bord > 0 ? acc / bord : 0;
}

static void fluid_prestep(Arb *a, float dt) {
  Body *b1 = a->b1, *b2 = a->b2;
  SV s1, s2;
  sview(a->s1, &s1), sview(a->s2, &s2);
  a->f.r1x = a->f.cx - b1->pos.x, a->f.r1y = a->f.cy - b1->pos.y;
  a->f.r2x = a->f.cx - b2->pos.x, a->f.r2y = a->f.cy - b2->pos.y;
  bool f1 = s1.flags & SF_FLUID, f2 = s2.flags & SF_FLUID;
  const SBody *w1 = f1 ? &sp.sbodies[s1.sbody] : 0, *w2 = f2 ? &sp.sbodies[s2.sbody] : 0;
  float bx = 0, by = 0, g = 1;
  (void)g;
  if (f1) {
    float m = a->f.overlap * w1->density / 1000;
    bx -= sp.gravity.x * m, by -= sp.gravity.y * m;
  } else if (f2) {
    float m = a->f.overlap * w2->density / 1000;
    bx += sp.gravity.x * m, by += sp.gravity.y * m;
  }
  bx *= dt, by *= dt;
  if (b1->type == B_DYNAMIC) {
    b1->vel.x -= bx * b1->imass, b1->vel.y -= by * b1->imass;
    b1->angvel -= cross(a->f.r1x, a->f.r1y, bx, by) * b1->iinertia;
  }
  if (b2->type == B_DYNAMIC) {
    b2->vel.x += bx * b2->imass, b2->vel.y += by * b2->imass;
    b2->angvel += cross(a->f.r2x, a->f.r2y, bx, by) * b2->iinertia;
  }
  float v1 = f1 ? w1->viscosity : 0, v2v = f2 ? w2->viscosity : 0;
  if (v1 == 0 && v2v == 0) {
    a->f.nodrag = 1;
    a->f.dampx = a->f.dampy = a->f.adamp = 0;
    return;
  }
  a->f.nodrag = 0;
  float tv = 0, bc;
  if (f1) tv += v1 * ang_drag(&s2) * a->f.overlap / shape_area(&s2);
  if (f2) tv += v2v * ang_drag(&s1) * a->f.overlap / shape_area(&s1);
  if (tv != 0) {
    float is = b1->iinertia + b2->iinertia;
    a->f.wmass = is != 0 ? 1 / is : 0;
    tv *= 0.0004f;
    a->f.wmass *= soft_gamma(tv, 1, dt, &a->f.agamma, &bc);
  } else a->f.wmass = 0, a->f.agamma = 0;
  float vx = (b2->vel.x - a->f.r2y * b2->angvel) - (b1->vel.x - a->f.r1y * b2->angvel);
  float vy = (b2->vel.y + a->f.r2x * b2->angvel) - (b1->vel.y + a->f.r1x * b1->angvel);
  float l = vx * vx + vy * vy;
  if (l >= EPS * EPS) {
    l = 1 / sqrtf(l);
    a->f.nx = vx * l, a->f.ny = vy * l;
  }
  V2 n = v2(a->f.nx, a->f.ny);
  tv = 0;
  if (f1) tv += lin_drag(&s2, -v1 * a->f.overlap / shape_area(&s2), n);
  if (f2) tv += lin_drag(&s1, -v2v * a->f.overlap / shape_area(&s1), n);
  if (tv != 0) {
    float K[3], m = b1->imass + b2->imass;
    K[0] = m, K[1] = 0, K[2] = m;
    if (b1->iinertia != 0) {
      float X = a->f.r1x * b1->iinertia, Y = a->f.r1y * b1->iinertia;
      K[0] += Y * a->f.r1y, K[1] += -Y * a->f.r1x, K[2] += X * a->f.r1x;
    }
    if (b2->iinertia != 0) {
      float X = a->f.r2x * b2->iinertia, Y = a->f.r2y * b2->iinertia;
      K[0] += Y * a->f.r2y, K[1] += -Y * a->f.r2x, K[2] += X * a->f.r2x;
    }
    float det = K[0] * K[2] - K[1] * K[1];
    if (det != 0) {
      det = 1 / det;
      float t = K[2] * det;
      K[2] = K[0] * det, K[0] = t, K[1] *= -det;
    }
    float ig = soft_gamma(tv, 1, dt, &a->f.lgamma, &bc);
    a->f.vmass[0] = K[0] * ig, a->f.vmass[1] = K[1] * ig, a->f.vmass[2] = K[2] * ig;
  } else a->f.vmass[0] = a->f.vmass[1] = a->f.vmass[2] = 0, a->f.lgamma = 0;
}

static void fluid_warm(Arb *a) {
  Body *b1 = a->b1, *b2 = a->b2;
  float dx = a->f.dampx, dy = a->f.dampy;
  b1->vel.x -= dx * b1->imass, b1->vel.y -= dy * b1->imass;
  b2->vel.x += dx * b2->imass, b2->vel.y += dy * b2->imass;
  b1->angvel -= b1->iinertia * cross(a->f.r1x, a->f.r1y, dx, dy);
  b2->angvel += b2->iinertia * cross(a->f.r2x, a->f.r2y, dx, dy);
  b1->angvel -= a->f.adamp * b1->iinertia;
  b2->angvel += a->f.adamp * b2->iinertia;
}

static void fluid_vel(Arb *a) {
  if (a->f.nodrag) return;
  Body *b1 = a->b1, *b2 = a->b2;
  float w1 = b1->angvel, w2 = b2->angvel;
  float jx = (b1->vel.x - a->f.r1y * w1) - (b2->vel.x - a->f.r2y * w2);
  float jy = (b1->vel.y + a->f.r1x * w1) - (b2->vel.y + a->f.r2x * w2);
  float t = a->f.vmass[0] * jx + a->f.vmass[1] * jy;
  jy = a->f.vmass[1] * jx + a->f.vmass[2] * jy, jx = t;
  jx -= a->f.dampx * a->f.lgamma, jy -= a->f.dampy * a->f.lgamma;
  a->f.dampx += jx, a->f.dampy += jy;
  b1->vel.x -= jx * b1->imass, b1->vel.y -= jy * b1->imass;
  b2->vel.x += jx * b2->imass, b2->vel.y += jy * b2->imass;
  b1->angvel -= b1->iinertia * cross(a->f.r1x, a->f.r1y, jx, jy);
  b2->angvel += b2->iinertia * cross(a->f.r2x, a->f.r2y, jx, jy);
  float jd = (w1 - w2) * a->f.wmass - a->f.adamp * a->f.agamma;
  a->f.adamp += jd;
  b1->angvel -= jd * b1->iinertia;
  b2->angvel += jd * b2->iinertia;
}

static void wake_body(Body *b);

static void narrow(uint16_t id1, uint16_t id2, bool stat) {
  SV v1, v2_;
  sview(id1, &v1), sview(id2, &v2_);
  if (v1.maxx < v2_.minx || v2_.maxx < v1.minx || v1.maxy < v2_.miny || v2_.maxy < v1.miny) return;
  int it = itype(&v1, &v2_);
  if (it < 0) return;
  const SV *sa, *sb;
  if (v1.type > v2_.type) sa = &v2_, sb = &v1;
  else if (v1.type == v2_.type) {
    if (v1.id < v2_.id) sa = &v1, sb = &v2_;
    else sa = &v2_, sb = &v1;
  } else sa = &v1, sb = &v2_;
  bool rev = sa == &v2_;
  Arb *a = arb_find(id1, id2);
  int type = it == 2 ? A_SENSOR : it == 0 ? A_FLUID : A_COL;
  if (a && a->type != type) {
    arb_retire(a, true);
    a = 0;
  }
  bool first = !a;
  if (!first && a->stamp == sp.stamp) return;
  if (first) {
    a = arb_new(id1, id2);
    if (!a) return;
    a->type = (uint8_t)type;
    a->b1 = v1.body, a->b2 = v2_.body;
  } else if (type == A_COL) rev = sa->id != a->s1;
  a->stamp = sp.stamp;
  a->stat = stat;
  bool hit;
  if (type == A_SENSOR) hit = test_collide(sa, sb);
  else if (type == A_FLUID) hit = flow_collide(&v1, &v2_, a);
  else if (sb->type == SH_POLY) hit = sa->type == SH_POLY ? poly2poly(sa, sb, a, rev) : circle2poly(sa, sb, a, rev);
  else hit = circle2circle(sa, sb, a, rev);
  if (!hit) {
    if (first) arb_free(a);
    return;
  }
  a->fresh = first ? 1 : a->up_stamp < sp.stamp - 1;
  a->up_stamp = a->stamp;
  if (type != A_SENSOR) {
    if (a->b1->flags & BF_FROZEN) a->b1->flags &= (uint8_t)~BF_FROZEN;
    if (a->b2->flags & BF_FROZEN) a->b2->flags &= (uint8_t)~BF_FROZEN;
    if (a->b1->type == B_DYNAMIC && (a->b1->flags & BF_SLEEP)) wake_body(a->b1);
    if (a->b2->type == B_DYNAMIC && (a->b2->flags & BF_SLEEP)) wake_body(a->b2);
  }
}

/* ---- sleeping ---- */

static int16_t dsf_find(int i) {
  while (sp.bodies[i]->dsf != i) {
    int p = sp.bodies[i]->dsf;
    sp.bodies[i]->dsf = sp.bodies[p]->dsf;
    i = p;
  }
  return (int16_t)i;
}
static void dsf_union(int a, int b) {
  a = dsf_find(a), b = dsf_find(b);
  if (a != b) sp.bodies[b]->dsf = (int16_t)a;
}

static int body_index(const Body *b) { return b->idx; }

static void wake_body(Body *b) {
  /* the whole island wakes: everything joined to it by a constraint or touching it */
  static Body *queue[64];
  if (!(b->flags & BF_SLEEP)) return;
  int head = 0, tail = 0;
  b->flags &= (uint8_t)~BF_SLEEP;
  b->waket = sp.stamp;
  queue[tail++] = b;
  while (head < tail) {
    Body *w = queue[head++];
    for (int pass = 0; pass < 2; pass++) {
      int n = pass ? sp.ncons : NP_MAX_ARBS;
      for (int i = 0; i < n; i++) {
        Body *o;
        if (pass) {
          Con *c = sp.cons[i];
          if (!(c->flags & CF_ACTIVE)) continue;
          o = c->b1 == w ? c->b2 : c->b2 == w ? c->b1 : 0;
        } else {
          Arb *a = &sp.arbs[i];
          if (!a->used || a->type != A_COL) continue;
          o = a->b1 == w ? a->b2 : a->b2 == w ? a->b1 : 0;
        }
        if (!o || o->type != B_DYNAMIC || !(o->flags & BF_SLEEP)) continue;
        o->flags &= (uint8_t)~BF_SLEEP;
        o->waket = sp.stamp;
        if (tail < 64) queue[tail++] = o;
      }
    }
    if (head == tail) break;
  }
}

void np_wake(Body *b) {
  if (b->type == B_DYNAMIC) {
    if (b->flags & BF_SLEEP) wake_body(b);
    b->waket = sp.stamp;
  }
}

static bool at_rest(Body *b, float dt) {
  float lin = LIN_SLEEP * LIN_SLEEP;
  bool can = true;
  /* (Nape also checks how far it moved in the step; the velocities are enough here) */
  (void)dt;
  if (b->vel.x * b->vel.x + b->vel.y * b->vel.y > lin) can = false;
  else {
    float wx = b->maxx - b->minx, wy = b->maxy - b->miny, idl = wx * wx + wy * wy, ang = ANG_SLEEP * ANG_SLEEP;
    if (4 * b->angvel * b->angvel * idl > ang) can = false;
  }
  if (!can) b->waket = sp.stamp;
  return b->waket + SLEEP_DELAY < sp.stamp;
}

static void forests(float dt) {
  int n = sp.nbodies;
  for (int i = 0; i < n; i++) sp.bodies[i]->dsf = (int16_t)i;
  for (int i = 0; i < NP_MAX_ARBS; i++) {
    Arb *a = &sp.arbs[i];
    if (!a->used || a->type != A_COL || a->up_stamp != sp.stamp) continue;
    if (a->b1->type == B_DYNAMIC && a->b2->type == B_DYNAMIC) dsf_union(body_index(a->b1), body_index(a->b2));
  }
  for (int i = 0; i < sp.ncons; i++) {
    Con *c = sp.cons[i];
    if (!(c->flags & CF_ACTIVE)) continue;
    if (!(c->b1->flags & BF_SPACE) || !(c->b2->flags & BF_SPACE)) continue;
    if (c->b1->type == B_DYNAMIC && c->b2->type == B_DYNAMIC) dsf_union(body_index(c->b1), body_index(c->b2));
  }
  /* an island sleeps when every body in it has rested long enough */
  static uint8_t awake[256];
  int m = n < 256 ? n : 256;
  memset(awake, 0, (size_t)m);
  for (int i = 0; i < m; i++) {
    Body *b = sp.bodies[i];
    if (b->type != B_DYNAMIC || !(b->flags & BF_SPACE) || (b->flags & BF_STILL)) continue;
    if (!at_rest(b, dt)) awake[dsf_find(i)] = 1;
  }
  for (int i = 0; i < m; i++) {
    Body *b = sp.bodies[i];
    if (b->type != B_DYNAMIC || !(b->flags & BF_SPACE) || (b->flags & BF_STILL)) continue;
    if (!awake[dsf_find(i)]) {
      b->flags |= BF_SLEEP;
      b->vel = v2(0, 0), b->angvel = 0;
    }
  }
}

/* ---- the step ---- */

static float pre_dt_space;

static bool arb_prestep(Arb *a, float dt) {
  /* two bodies at rest keep no contacts (Nape keeps them sleeping; here they are made again on waking) */
  bool r1 = a->b1->type != B_DYNAMIC || (a->b1->flags & BF_STILL), r2 = a->b2->type != B_DYNAMIC || (a->b2->flags & BF_STILL);
  if (r1 && r2 && a->type != A_SENSOR && !(a->b1->type == B_KINEMATIC || a->b2->type == B_KINEMATIC)) {
    arb_free(a);
    return false;
  }
  bool begcb = a->fresh, endcb = a->up_stamp == sp.stamp - 1;
  if (begcb) {
    if (!a->present) {
      if (!pair_present(a)) push_event(true, a);
      a->present = 1;
    }
  } else if (endcb && a->present) {
    a->present = 0;
    if (!pair_present(a)) push_event(false, a);
  }
  a->fresh = 0;
  if (a->up_stamp + (a->type == A_COL ? EXPIRE : 0) < sp.stamp) {
    arb_retire(a, true);
    return false;
  }
  a->active = a->up_stamp == sp.stamp;
  if (a->type == A_FLUID) {
    if (a->active) fluid_prestep(a, dt);
    return a->active;
  }
  if (a->type != A_COL) return a->active;
  Body *b1 = a->b1, *b2 = a->b2;
  /* materials, from the shapes */
  {
    const Material *m1 = shape_mat(a->s1), *m2 = shape_mat(a->s2);
    float r = (m1->elasticity + m2->elasticity) / 2;
    a->rest = r < 0 ? 0 : r > 1 ? 1 : r;
    a->dyn = sqrtf(m1->dyn * m2->dyn);
    a->stat_f = sqrtf(m1->stat * m2->stat);
    a->rfric = sqrtf(m1->rolling * m2->rolling);
  }
  if (!a->active) {
    /* cleanupContacts */
    int k = 0;
    for (int i = 0; i < a->nc; i++)
      if (a->c[i].stamp + EXPIRE >= sp.stamp) a->c[k++] = a->c[i];
    a->nc = (uint8_t)k;
    for (int i = 0; i < a->nc; i++) a->c[i].active = 0;
    return false;
  }
  float dtratio = 1;   /* the game steps at a fixed 1/60 s */
  (void)dt;
  float ms = b1->imass + b2->imass;
  bool stat = b1->type != B_DYNAMIC || b2->type != B_DYNAMIC;
  a->biascoef = stat ? BIAS_STATIC : BIAS_DYN;
  int k = 0;
  for (int i = 0; i < a->nc; i++)
    if (a->c[i].stamp + EXPIRE >= sp.stamp) a->c[k++] = a->c[i];
  a->nc = (uint8_t)k;
  int c1 = -1, c2 = -1;
  bool fst = true;
  a->hc2 = 0;
  float nx = a->n.x, ny = a->n.y;
  for (int i = 0; i < a->nc; i++) {
    Contact *c = &a->c[i];
    c->active = c->stamp == sp.stamp;
    if (!c->active) continue;
    if (fst) fst = false, c1 = i;
    else a->hc2 = 1, c2 = i;
    float d2 = c->r2x * nx + c->r2y * ny, d1 = c->r1x * nx + c->r1y * ny;
    float kt = ms + b2->iinertia * d2 * d2 + b1->iinertia * d1 * d1;
    c->tmass = kt < EPS * EPS ? 0 : 1 / kt;
    float x2 = cross(c->r2x, c->r2y, nx, ny), x1 = cross(c->r1x, c->r1y, nx, ny);
    float nt = ms + b2->iinertia * x2 * x2 + b1->iinertia * x1 * x1;
    c->nmass = nt < EPS * EPS ? 0 : 1 / nt;
    float vrx = (b2->vel.x - c->r2y * b2->angvel) - (b1->vel.x - c->r1y * b1->angvel);
    float vry = (b2->vel.y + c->r2x * b2->angvel) - (b1->vel.y + c->r1x * b1->angvel);
    float vdot = nx * vrx + ny * vry;
    c->bounce = vdot * a->rest;
    if (c->bounce > -ELASTIC_THRESHOLD) c->bounce = 0;
    vdot = cross(nx, ny, vrx, vry);
    c->friction = vdot * vdot > STATIC_FRICTION_THRESHOLD * STATIC_FRICTION_THRESHOLD ? a->dyn : a->stat_f;
    c->jn *= dtratio, c->jt *= dtratio;
  }
  if (a->hc2) {
    a->hpc2 = 1;
    if (a->c[c1].posonly) {
      int t = c1; c1 = c2; c2 = t;
      a->hc2 = 0;
    } else if (a->c[c2].posonly) a->hc2 = 0;
    if (a->c[c1].posonly) fst = true;
  } else a->hpc2 = 0;
  a->jr *= dtratio;
  a->c1 = (int8_t)c1, a->c2 = (int8_t)c2;
  if (!fst) {
    Contact *c = &a->c[c1];
    a->rn1a = cross(c->r1x, c->r1y, nx, ny), a->rt1a = c->r1x * nx + c->r1y * ny;
    a->rn1b = cross(c->r2x, c->r2y, nx, ny), a->rt1b = c->r2x * nx + c->r2y * ny;
  }
  if (a->hc2) {
    Contact *c = &a->c[c2];
    a->rn2a = cross(c->r1x, c->r1y, nx, ny), a->rt2a = c->r1x * nx + c->r1y * ny;
    a->rn2b = cross(c->r2x, c->r2y, nx, ny), a->rt2b = c->r2x * nx + c->r2y * ny;
    float i1 = b1->iinertia, i2 = b2->iinertia;
    float ka = ms + i1 * a->rn1a * a->rn1a + i2 * a->rn1b * a->rn1b;
    float kb = ms + i1 * a->rn1a * a->rn2a + i2 * a->rn1b * a->rn2b;
    float kc = ms + i1 * a->rn2a * a->rn2a + i2 * a->rn2b * a->rn2b;
    float norm = ka * ka + 2 * kb * kb + kc * kc, det = ka * kc - kb * kb;
    if (norm < ILL_COND * det) {
      a->kk[0] = ka, a->kk[1] = kb, a->kk[2] = kc;
      det = 1 / det;
      a->km[0] = kc * det, a->km[1] = -kb * det, a->km[2] = ka * det;
    } else {
      a->hc2 = 0;
      if (a->c[c2].dist < a->c[c1].dist) { a->c1 = (int8_t)c2; a->c2 = (int8_t)c1; c1 = a->c1; }
      a->c[a->c2].active = 0;
      if (!fst) {
        Contact *c = &a->c[c1];
        a->rn1a = cross(c->r1x, c->r1y, nx, ny), a->rt1a = c->r1x * nx + c->r1y * ny;
        a->rn1b = cross(c->r2x, c->r2y, nx, ny), a->rt1b = c->r2x * nx + c->r2y * ny;
      }
    }
  }
  float rm = b1->iinertia + b2->iinertia;
  a->rmass = rm != 0 ? 1 / rm : 0;
  a->active = !fst;
  return a->active;
}

static inline void app_imp(Arb *a, const Contact *c, float jn, float jt) {
  Body *b1 = a->b1, *b2 = a->b2;
  float nx = a->n.x, ny = a->n.y;
  float jx = nx * jn - ny * jt, jy = ny * jn + nx * jt;
  b1->vel.x -= jx * b1->imass, b1->vel.y -= jy * b1->imass;
  b1->angvel -= b1->iinertia * cross(c->r1x, c->r1y, jx, jy);
  b2->vel.x += jx * b2->imass, b2->vel.y += jy * b2->imass;
  b2->angvel += b2->iinertia * cross(c->r2x, c->r2y, jx, jy);
}

static void arb_warm(Arb *a) {
  app_imp(a, &a->c[a->c1], a->c[a->c1].jn, a->c[a->c1].jt);
  if (a->hc2) app_imp(a, &a->c[a->c2], a->c[a->c2].jn, a->c[a->c2].jt);
  a->b2->angvel += a->jr * a->b2->iinertia;
  a->b1->angvel -= a->jr * a->b1->iinertia;
}

static void arb_vel(Arb *a) {
  Body *b1 = a->b1, *b2 = a->b2;
  float nx = a->n.x, ny = a->n.y, j, jmax, jold, cj;
  Contact *c1 = &a->c[a->c1], *c2 = a->hc2 ? &a->c[a->c2] : 0;
#define REL(c, vx, vy) \
  float vx = b2->vel.x - (c)->r2y * b2->angvel - (b1->vel.x - (c)->r1y * b1->angvel); \
  float vy = b2->vel.y + (c)->r2x * b2->angvel - (b1->vel.y + (c)->r1x * b1->angvel)
#define FRICTION(c, v1x, v1y, rta, rtb) \
  j = cross(nx, ny, v1x, v1y) * (c)->tmass; \
  jmax = (c)->friction * (c)->jn; jold = (c)->jt; \
  cj = jold - j; if (cj > jmax) cj = jmax; else if (cj < -jmax) cj = -jmax; \
  j = cj - jold; (c)->jt = cj; \
  { float jx = -ny * j, jy = nx * j; \
    b2->vel.x += jx * b2->imass; b2->vel.y += jy * b2->imass; \
    b1->vel.x -= jx * b1->imass; b1->vel.y -= jy * b1->imass; \
    b2->angvel += (rtb) * j * b2->iinertia; b1->angvel -= (rta) * j * b1->iinertia; }
  {
    REL(c1, v1x, v1y);
    FRICTION(c1, v1x, v1y, a->rt1a, a->rt1b);
  }
  if (c2) {
    {
      REL(c2, v2x, v2y);
      FRICTION(c2, v2x, v2y, a->rt2a, a->rt2b);
    }
    REL(c1, v1x, v1y);
    REL(c2, v2x, v2y);
    float ax = c1->jn, ay = c2->jn;
    float jnx = v1x * nx + v1y * ny + c1->bounce - (a->kk[0] * ax + a->kk[1] * ay);
    float jny = v2x * nx + v2y * ny + c2->bounce - (a->kk[1] * ax + a->kk[2] * ay);
    float xx = -(a->km[0] * jnx + a->km[1] * jny), xy = -(a->km[1] * jnx + a->km[2] * jny);
    if (xx >= 0 && xy >= 0) {
      jnx = xx - ax, jny = xy - ay;
      c1->jn = xx, c2->jn = xy;
    } else {
      xx = -c1->nmass * jnx;
      if (xx >= 0 && (a->kk[1] * xx + jny) >= 0) {
        jnx = xx - ax, jny = -ay;
        c1->jn = xx, c2->jn = 0;
      } else {
        xy = -c2->nmass * jny;
        if (xy >= 0 && (a->kk[1] * xy + jnx) >= 0) {
          jnx = -ax, jny = xy - ay;
          c1->jn = 0, c2->jn = xy;
        } else if (jnx >= 0 && jny >= 0) {
          jnx = -ax, jny = -ay;
          c1->jn = c2->jn = 0;
        } else jnx = jny = 0;
      }
    }
    j = jnx + jny;
    float jx = nx * j, jy = ny * j;
    b2->vel.x += jx * b2->imass, b2->vel.y += jy * b2->imass;
    b1->vel.x -= jx * b1->imass, b1->vel.y -= jy * b1->imass;
    b2->angvel += (a->rn1b * jnx + a->rn2b * jny) * b2->iinertia;
    b1->angvel -= (a->rn1a * jnx + a->rn2a * jny) * b1->iinertia;
  } else {
    if (a->radius != 0) {
      float dw = b2->angvel - b1->angvel;
      j = dw * a->rmass;
      jmax = a->rfric * c1->jn;
      jold = a->jr;
      a->jr -= j;
      if (a->jr > jmax) a->jr = jmax; else if (a->jr < -jmax) a->jr = -jmax;
      j = a->jr - jold;
      b2->angvel += j * b2->iinertia;
      b1->angvel -= j * b1->iinertia;
    }
    REL(c1, v1x, v1y);
    j = (c1->bounce + nx * v1x + ny * v1y) * c1->nmass;
    jold = c1->jn;
    cj = jold - j;
    if (cj < 0) cj = 0;
    j = cj - jold;
    c1->jn = cj;
    float jx = nx * j, jy = ny * j;
    b2->vel.x += jx * b2->imass, b2->vel.y += jy * b2->imass;
    b1->vel.x -= jx * b1->imass, b1->vel.y -= jy * b1->imass;
    b2->angvel += a->rn1b * j * b2->iinertia;
    b1->angvel -= a->rn1a * j * b1->iinertia;
  }
#undef REL
#undef FRICTION
}

static inline V2 rot_axis(float lx, float ly, V2 axis) { return v2(lx * axis.y - ly * axis.x, lx * axis.x + ly * axis.y); }

static void arb_pos(Arb *a) {
  Body *b1 = a->b1, *b2 = a->b2;
  float bc = a->biascoef;
  if (a->ptype == PT_CIRCLE) {
    Contact *c = &a->c[a->c1];
    V2 r2 = rot_axis(c->lr2x, c->lr2y, b2->axis);
    r2.x += b2->pos.x, r2.y += b2->pos.y;
    V2 r1 = rot_axis(c->lr1x, c->lr1y, b1->axis);
    r1.x += b1->pos.x, r1.y += b1->pos.y;
    float dx = r2.x - r1.x, dy = r2.y - r1.y, dl = sqrtf(dx * dx + dy * dy);
    float r = a->radius - SLOP, err = dl - r;
    if (dx * a->n.x + dy * a->n.y < 0) {
      dx = -dx, dy = -dy;
      err -= a->radius;
    }
    if (err < 0) {
      if (dl < EPS) {
        if (b1->imass != 0) b1->pos.x += EPS * 10;
        else b2->pos.x += EPS * 10;
      } else {
        dx /= dl, dy /= dl;
        float px = 0.5f * (r1.x + r2.x), py = 0.5f * (r1.y + r2.y), pen = dl - r;
        float q1x = px - b1->pos.x, q1y = py - b1->pos.y, q2x = px - b2->pos.x, q2y = py - b2->pos.y;
        float rn1 = cross(q1x, q1y, dx, dy), rn2 = cross(q2x, q2y, dx, dy);
        float K = b2->imass + rn2 * rn2 * b2->iinertia + b1->imass + rn1 * rn1 * b1->iinertia;
        if (K != 0) {
          float jn = -bc * pen / K, jx = dx * jn, jy = dy * jn;
          b1->pos.x -= jx * b1->imass, b1->pos.y -= jy * b1->imass;
          delta_rot(b1, -rn1 * b1->iinertia * jn);
          b2->pos.x += jx * b2->imass, b2->pos.y += jy * b2->imass;
          delta_rot(b2, rn2 * b2->iinertia * jn);
        }
      }
    }
    return;
  }
  V2 g;
  float gproj;
  V2 clip1, clip2 = {0, 0};
  Contact *c1 = &a->c[a->c1], *c2 = a->hpc2 && a->c2 >= 0 ? &a->c[a->c2] : 0;
  if (a->ptype == PT_FACE1) {
    g = rot_axis(a->lnorm.x, a->lnorm.y, b1->axis);
    gproj = a->lproj + g.x * b1->pos.x + g.y * b1->pos.y;
    clip1 = rot_axis(c1->lr1x, c1->lr1y, b2->axis);
    clip1.x += b2->pos.x, clip1.y += b2->pos.y;
    if (c2) {
      clip2 = rot_axis(c2->lr1x, c2->lr1y, b2->axis);
      clip2.x += b2->pos.x, clip2.y += b2->pos.y;
    }
  } else {
    g = rot_axis(a->lnorm.x, a->lnorm.y, b2->axis);
    gproj = a->lproj + g.x * b2->pos.x + g.y * b2->pos.y;
    clip1 = rot_axis(c1->lr1x, c1->lr1y, b1->axis);
    clip1.x += b1->pos.x, clip1.y += b1->pos.y;
    if (c2) {
      clip2 = rot_axis(c2->lr1x, c2->lr1y, b1->axis);
      clip2.x += b1->pos.x, clip2.y += b1->pos.y;
    }
  }
  float err1 = clip1.x * g.x + clip1.y * g.y - gproj - a->radius + SLOP, err2 = 0;
  if (c2) err2 = clip2.x * g.x + clip2.y * g.y - gproj - a->radius + SLOP;
  if (!(err1 < 0 || err2 < 0)) return;
  if (a->rev) g.x = -g.x, g.y = -g.y;
  float c1r1x = clip1.x - b1->pos.x, c1r1y = clip1.y - b1->pos.y;
  float c1r2x = clip1.x - b2->pos.x, c1r2y = clip1.y - b2->pos.y;
  if (c2) {
    float c2r1x = clip2.x - b1->pos.x, c2r1y = clip2.y - b1->pos.y;
    float c2r2x = clip2.x - b2->pos.x, c2r2y = clip2.y - b2->pos.y;
    float rn1a = cross(c1r1x, c1r1y, g.x, g.y), rn1b = cross(c1r2x, c1r2y, g.x, g.y);
    float rn2a = cross(c2r1x, c2r1y, g.x, g.y), rn2b = cross(c2r2x, c2r2y, g.x, g.y);
    float ms = b1->imass + b2->imass, i1 = b1->iinertia, i2 = b2->iinertia;
    float ka = ms + i1 * rn1a * rn1a + i2 * rn1b * rn1b;
    float kb = ms + i1 * rn1a * rn2a + i2 * rn1b * rn2b;
    float kc = ms + i1 * rn2a * rn2a + i2 * rn2b * rn2b;
    float bx = err1 * bc, by = err2 * bc, xx, xy;
    /* mat2_solve(kMass, -b) */
    {
      float det = ka * kc - kb * kb, vx = -bx, vy = -by;
      if (det != det) xx = xy = 0;
      else if (det == 0) {
        xx = ka != 0 ? vx / ka : 0;
        xy = kc != 0 ? vy / kc : 0;
      } else {
        det = 1 / det;
        xx = det * (kc * vx - kb * vy);
        xy = det * (ka * vy - kb * vx);
      }
    }
    bool ok = xx >= 0 && xy >= 0;
    if (!ok) {
      xx = -bx / ka, xy = 0;
      ok = xx >= 0 && kb * xx + by >= 0;
    }
    if (!ok) {
      xx = 0, xy = -by / kc;
      ok = xy >= 0 && kb * xy + bx >= 0;
    }
    if (ok) {
      float s = xx + xy;
      b1->pos.x -= g.x * s * b1->imass, b1->pos.y -= g.y * s * b1->imass;
      delta_rot(b1, -b1->iinertia * (rn1a * xx + rn2a * xy));
      b2->pos.x += g.x * s * b2->imass, b2->pos.y += g.y * s * b2->imass;
      delta_rot(b2, b2->iinertia * (rn1b * xx + rn2b * xy));
    }
  } else {
    float rn1 = cross(c1r1x, c1r1y, g.x, g.y), rn2 = cross(c1r2x, c1r2y, g.x, g.y);
    float K = b2->imass + rn2 * rn2 * b2->iinertia + b1->imass + rn1 * rn1 * b1->iinertia;
    if (K != 0) {
      float jn = -bc * err1 / K, jx = g.x * jn, jy = g.y * jn;
      b1->pos.x -= jx * b1->imass, b1->pos.y -= jy * b1->imass;
      delta_rot(b1, -rn1 * b1->iinertia * jn);
      b2->pos.x += jx * b2->imass, b2->pos.y += jy * b2->imass;
      delta_rot(b2, rn2 * b2->iinertia * jn);
    }
  }
}

/* ---- constraints ---- */

static float soft_gamma(float freq, float damping, float dt, float *gamma, float *biascoef) {
  float omega = TAU * freq;
  float g = 1 / (dt * omega * (2 * damping + omega * dt)), ig = 1 / (1 + g);
  *biascoef = dt * omega * omega * g;
  *gamma = g * ig;
  return ig;
}

static int mat2_invert(float *m) {
  float det = m[0] * m[2] - m[1] * m[1];
  if (det != det) { m[0] = m[1] = m[2] = 0; return 3; }
  if (det == 0) {
    int flag = 0;
    if (m[0] != 0) m[0] = 1 / m[0]; else { m[0] = 0; flag |= 1; }
    if (m[2] != 0) m[2] = 1 / m[2]; else { m[2] = 0; flag |= 2; }
    m[1] = 0;
    return flag;
  }
  det = 1 / det;
  float t = m[2] * det;
  m[2] = m[0] * det;
  m[0] = t;
  m[1] *= -det;
  return 0;
}

static void mat2_solve(const float *m, float *vx, float *vy) {
  float det = m[0] * m[2] - m[1] * m[1];
  if (det != det) { *vx = *vy = 0; return; }
  if (det == 0) {
    *vx = m[0] != 0 ? *vx / m[0] : 0;
    *vy = m[2] != 0 ? *vy / m[2] : 0;
    return;
  }
  det = 1 / det;
  float t = det * (m[2] * *vx - m[1] * *vy);
  *vy = det * (m[0] * *vy - m[1] * *vx);
  *vx = t;
}

/* 3x3 symmetric: a b c / b d e / c e f stored a,b,d,c,e,f as Nape's mat3_set(m,A,B,D,C,E,F) */
static int mat3_invert(float *m) {
  float a = m[0], b = m[1], d = m[2], c = m[3], e = m[4], f = m[5];
  float det = a * (d * f - e * e) + b * (c * e - b * f) + c * (b * e - c * d);
  if (det != det) { for (int i = 0; i < 6; i++) m[i] = 0; return 7; }
  if (det == 0) {
    int flag = 0;
    if (a != 0) m[0] = 1 / a; else { m[0] = 0; flag |= 1; }
    if (d != 0) m[2] = 1 / d; else { m[2] = 0; flag |= 2; }
    if (f != 0) m[5] = 1 / f; else { m[5] = 0; flag |= 4; }
    m[1] = m[3] = m[4] = 0;
    return flag;
  }
  det = 1 / det;
  float A = det * (d * f - e * e);
  float B = det * (e * c - b * f), D = det * (a * f - c * c);
  float C = det * (b * e - c * d), E = det * (b * c - a * e), F = det * (a * d - b * b);
  m[0] = A, m[1] = B, m[2] = D, m[3] = C, m[4] = E, m[5] = F;
  return 0;
}

static void mat3_xform(const float *m, float *v) {
  float X = m[0] * v[0] + m[1] * v[1] + m[3] * v[2];
  float Y = m[1] * v[0] + m[2] * v[1] + m[4] * v[2];
  v[2] = m[3] * v[0] + m[4] * v[1] + m[5] * v[2];
  v[0] = X, v[1] = Y;
}

static void mat3_solve(const float *m, float *v) {
  float a = m[0], b = m[1], d = m[2], c = m[3], e = m[4], f = m[5];
  float det = a * (d * f - e * e) + b * (c * e - b * f) + c * (b * e - c * d);
  if (det != det) { v[0] = v[1] = v[2] = 0; return; }
  if (det == 0) {
    v[0] = a != 0 ? v[0] / a : 0;
    v[1] = d != 0 ? v[1] / d : 0;
    v[2] = f != 0 ? v[2] / f : 0;
    return;
  }
  det = 1 / det;
  float B = e * c - b * f, C = b * e - c * d, E = b * c - a * e;
  float X = det * (v[0] * (d * f - e * e) + v[1] * B + v[2] * C);
  float Y = det * (v[0] * B + v[1] * (a * f - c * c) + v[2] * E);
  v[2] = det * (v[0] * C + v[1] * E + v[2] * (a * d - b * b));
  v[0] = X, v[1] = Y;
}

static void clamp2(float *x, float *y, float m) {
  float ls = *x * *x + *y * *y;
  if (ls > m * m) {
    float k = m / sqrtf(ls);
    *x *= k, *y *= k;
  }
}

static inline V2 anchor(const Body *b, V2 l) { return rot_axis(l.x, l.y, b->axis); }

/* PivotJoint, WeldJoint (2D part) helpers */
static void pivot_keff(const Body *b1, const Body *b2, V2 r1, V2 r2, float *k) {
  float m = b1->imass + b2->imass;
  k[0] = m, k[1] = 0, k[2] = m;
  if (b1->iinertia != 0) {
    float X = r1.x * b1->iinertia, Y = r1.y * b1->iinertia;
    k[0] += Y * r1.y, k[1] += -Y * r1.x, k[2] += X * r1.x;
  }
  if (b2->iinertia != 0) {
    float X = r2.x * b2->iinertia, Y = r2.y * b2->iinertia;
    k[0] += Y * r2.y, k[1] += -Y * r2.x, k[2] += X * r2.x;
  }
}

static void pivot_imp(Body *b1, Body *b2, V2 r1, V2 r2, float jx, float jy, bool bias) {
  if (bias) {
    b1->pos.x -= jx * b1->imass, b1->pos.y -= jy * b1->imass;
    b2->pos.x += jx * b2->imass, b2->pos.y += jy * b2->imass;
    delta_rot(b1, -cross(r1.x, r1.y, jx, jy) * b1->iinertia);
    delta_rot(b2, cross(r2.x, r2.y, jx, jy) * b2->iinertia);
  } else {
    b1->vel.x -= jx * b1->imass, b1->vel.y -= jy * b1->imass;
    b2->vel.x += jx * b2->imass, b2->vel.y += jy * b2->imass;
    b1->angvel -= cross(r1.x, r1.y, jx, jy) * b1->iinertia;
    b2->angvel += cross(r2.x, r2.y, jx, jy) * b2->iinertia;
  }
}

static void weld_keff(const Body *b1, const Body *b2, V2 r1, V2 r2, float *k) {
  float m = b1->imass + b2->imass;
  k[0] = m, k[1] = 0, k[2] = m, k[3] = 0, k[4] = 0, k[5] = 0;
  const Body *bs[2] = {b1, b2};
  V2 rs[2] = {r1, r2};
  for (int i = 0; i < 2; i++) {
    const Body *b = bs[i];
    if (b->iinertia == 0) continue;
    float X = rs[i].x * b->iinertia, Y = rs[i].y * b->iinertia;
    k[0] += Y * rs[i].y, k[1] += -Y * rs[i].x, k[2] += X * rs[i].x;
    k[3] += -Y, k[4] += X, k[5] += b->iinertia;
  }
}

static void weld_imp(Body *b1, Body *b2, V2 r1, V2 r2, const float *j, bool bias) {
  float c1 = cross(r1.x, r1.y, j[0], j[1]) + j[2], c2 = cross(r2.x, r2.y, j[0], j[1]) + j[2];
  if (bias) {
    b1->pos.x -= j[0] * b1->imass, b1->pos.y -= j[1] * b1->imass;
    b2->pos.x += j[0] * b2->imass, b2->pos.y += j[1] * b2->imass;
    delta_rot(b1, -c1 * b1->iinertia);
    delta_rot(b2, c2 * b2->iinertia);
  } else {
    b1->vel.x -= j[0] * b1->imass, b1->vel.y -= j[1] * b1->imass;
    b2->vel.x += j[0] * b2->imass, b2->vel.y += j[1] * b2->imass;
    b1->angvel -= c1 * b1->iinertia;
    b2->angvel += c2 * b2->iinertia;
  }
}

/* DistanceJoint Cerr: C, and n */
static float dist_cerr(Con *c, V2 r1, V2 r2, V2 *n, bool *slack) {
  Body *b1 = c->b1, *b2 = c->b2;
  float nx = b2->pos.x + r2.x - (b1->pos.x + r1.x), ny = b2->pos.y + r2.y - (b1->pos.y + r1.y);
  float C = nx * nx + ny * ny;
  if (C < EPS) { *n = v2(0, 0); *slack = true; return 0; }
  C = sqrtf(C);
  nx /= C, ny /= C;
  if (c->flags & CF_EQUAL) { C -= c->jmax; *slack = false; }
  else if (C < c->jmin) { C = c->jmin - C; nx = -nx, ny = -ny; *slack = false; }
  else if (C > c->jmax) { C -= c->jmax; *slack = false; }
  else { nx = ny = 0; C = 0; *slack = true; }
  *n = v2(nx, ny);
  return C;
}

static void dist_imp(Body *b1, Body *b2, V2 n, float cx1, float cx2, float j, bool bias) {
  if (bias) {
    b1->pos.x -= n.x * b1->imass * j, b1->pos.y -= n.y * b1->imass * j;
    b2->pos.x += n.x * b2->imass * j, b2->pos.y += n.y * b2->imass * j;
    delta_rot(b1, -cx1 * b1->iinertia * j);
    delta_rot(b2, cx2 * b2->iinertia * j);
  } else {
    b1->vel.x -= n.x * b1->imass * j, b1->vel.y -= n.y * b1->imass * j;
    b2->vel.x += n.x * b2->imass * j, b2->vel.y += n.y * b2->imass * j;
    b1->angvel -= cx1 * b1->iinertia * j;
    b2->angvel += cx2 * b2->iinertia * j;
  }
}

/* AngleJoint Cerr */
static float angle_cerr(Con *c, float *scale, bool *slack) {
  float C = c->ratio * c->b2->rot - c->b1->rot;
  if (c->flags & CF_EQUAL) { C -= c->jmax; *slack = false; *scale = 1; }
  else if (C < c->jmin) { C = c->jmin - C; *scale = -1; *slack = false; }
  else if (C > c->jmax) { C -= c->jmax; *scale = 1; *slack = false; }
  else { *scale = 0; C = 0; *slack = true; }
  return C;
}

static void angle_imp(Con *c, float scale, float j, bool bias) {
  Body *b1 = c->b1, *b2 = c->b2;
  if (bias) {
    delta_rot(b1, -scale * j * b1->iinertia);
    delta_rot(b2, c->ratio * scale * j * b2->iinertia);
  } else {
    b1->angvel -= scale * b1->iinertia * j;
    b2->angvel += c->ratio * scale * b2->iinertia * j;
  }
}

/* LineJoint */
static void line_cerr(Con *c, V2 r1, V2 r2, V2 n, V2 *d, float *Cx, float *Cy, float *scale) {
  Body *b1 = c->b1, *b2 = c->b2;
  d->x = b2->pos.x + r2.x - b1->pos.x - r1.x;
  d->y = b2->pos.y + r2.y - b1->pos.y - r1.y;
  *Cx = cross(n.x, n.y, d->x, d->y);
  *Cy = n.x * d->x + n.y * d->y;
  if (c->flags & CF_EQUAL) { *Cy -= c->jmin; *scale = 1; }
  else if (*Cy > c->jmax) { *Cy -= c->jmax; *scale = 1; }
  else if (*Cy < c->jmin) { *Cy = c->jmin - *Cy; *scale = -1; }
  else { *Cy = 0; *scale = 0; }
}

static void line_keff(Con *c, V2 r1, V2 r2, V2 n, V2 d, float scale, float *k, float *dot1, float *cx1, float *dot2,
                      float *cx2) {
  Body *b1 = c->b1, *b2 = c->b2;
  float drx = d.x + r1.x, dry = d.y + r1.y;
  *dot1 = n.x * drx + n.y * dry, *cx1 = cross(n.x, n.y, drx, dry);
  *dot2 = n.x * r2.x + n.y * r2.y, *cx2 = cross(n.x, n.y, r2.x, r2.y);
  k[0] = b1->imass + b2->imass + *dot1 * *dot1 * b1->iinertia + *dot2 * *dot2 * b2->iinertia;
  k[1] = -scale * (*dot1 * *cx1 * b1->iinertia + *dot2 * *cx2 * b2->iinertia);
  k[2] = scale * scale * (b1->imass + b2->imass + *cx1 * *cx1 * b1->iinertia + *cx2 * *cx2 * b2->iinertia);
}

static void line_imp(Body *b1, Body *b2, V2 n, float scale, float dot1, float cx1, float dot2, float cx2, float jx,
                     float jy, bool bias) {
  float Jx = scale * n.x * jy - n.y * jx, Jy = n.x * jx + scale * n.y * jy;
  float w1 = (scale * cx1 * jy - dot1 * jx) * b1->iinertia, w2 = (dot2 * jx - scale * cx2 * jy) * b2->iinertia;
  if (bias) {
    b1->pos.x -= Jx * b1->imass, b1->pos.y -= Jy * b1->imass;
    b2->pos.x += Jx * b2->imass, b2->pos.y += Jy * b2->imass;
    delta_rot(b1, w1);
    delta_rot(b2, w2);
  } else {
    b1->vel.x -= Jx * b1->imass, b1->vel.y -= Jy * b1->imass;
    b2->vel.x += Jx * b2->imass, b2->vel.y += Jy * b2->imass;
    b1->angvel += w1;
    b2->angvel += w2;
  }
}

static bool con_live(const Con *c) {
  return (c->flags & CF_ACTIVE) && (c->b1->flags & BF_SPACE) && (c->b2->flags & BF_SPACE) &&
         !((c->b1->flags & BF_STILL || c->b1->type != B_DYNAMIC) && (c->b2->flags & BF_STILL || c->b2->type != B_DYNAMIC)) &&
         !(c->b1->flags & BF_FROZEN) && !(c->b2->flags & BF_FROZEN);
}

void np_con_clear(Con *c) {
  c->jacc[0] = c->jacc[1] = c->jacc[2] = 0;
  c->pre_dt = -1;
  c->flags &= (uint8_t)~CF_SLACK;
}

static void con_prestep(Con *c, float dt) {
  Body *b1 = c->b1, *b2 = c->b2;
  if (c->pre_dt == -1) c->pre_dt = dt;
  float dtratio = dt / c->pre_dt;
  c->pre_dt = dt;
  bool stiff = c->flags & CF_STIFF;
  float biascoef;
  switch (c->type) {
  case C_PIVOT: {
    c->a1rel = anchor(b1, c->a1), c->a2rel = anchor(b2, c->a2);
    pivot_keff(b1, b2, c->a1rel, c->a2rel, c->k);
    int flag = mat2_invert(c->k);
    if (flag & 1) c->jacc[0] = 0;
    if (flag & 2) c->jacc[1] = 0;
    if (!stiff) {
      float ig = soft_gamma(c->frequency, c->damping, dt, &c->gamma, &biascoef);
      c->k[0] *= ig, c->k[1] *= ig, c->k[2] *= ig;
      c->bias[0] = -biascoef * (b2->pos.x + c->a2rel.x - (b1->pos.x + c->a1rel.x));
      c->bias[1] = -biascoef * (b2->pos.y + c->a2rel.y - (b1->pos.y + c->a1rel.y));
    } else c->bias[0] = c->bias[1] = 0, c->gamma = 0;
    c->jacc[0] *= dtratio, c->jacc[1] *= dtratio;
    break;
  }
  case C_WELD: {
    c->a1rel = anchor(b1, c->a1), c->a2rel = anchor(b2, c->a2);
    weld_keff(b1, b2, c->a1rel, c->a2rel, c->k);
    int flag = mat3_invert(c->k);
    if (flag & 1) c->jacc[0] = 0;
    if (flag & 2) c->jacc[1] = 0;
    if (flag & 4) c->jacc[2] = 0;
    if (!stiff) {
      float ig = soft_gamma(c->frequency, c->damping, dt, &c->gamma, &biascoef);
      for (int i = 0; i < 6; i++) c->k[i] *= ig;
      c->bias[0] = -biascoef * (b2->pos.x + c->a2rel.x - (b1->pos.x + c->a1rel.x));
      c->bias[1] = -biascoef * (b2->pos.y + c->a2rel.y - (b1->pos.y + c->a1rel.y));
      c->bias[2] = -biascoef * (b2->rot - b1->rot - c->phase);
    } else c->bias[0] = c->bias[1] = c->bias[2] = 0, c->gamma = 0;
    for (int i = 0; i < 3; i++) c->jacc[i] *= dtratio;
    break;
  }
  case C_DIST: {
    if (c->jmin == c->jmax) c->flags |= CF_EQUAL; else c->flags &= (uint8_t)~CF_EQUAL;
    c->a1rel = anchor(b1, c->a1), c->a2rel = anchor(b2, c->a2);
    bool slack;
    float C = dist_cerr(c, c->a1rel, c->a2rel, &c->n, &slack);
    if (slack) c->flags |= CF_SLACK; else c->flags &= (uint8_t)~CF_SLACK;
    if (!slack) {
      c->cx1 = cross(c->a1rel.x, c->a1rel.y, c->n.x, c->n.y);
      c->cx2 = cross(c->a2rel.x, c->a2rel.y, c->n.x, c->n.y);
      float k = b1->imass + b2->imass + c->cx1 * c->cx1 * b1->iinertia + c->cx2 * c->cx2 * b2->iinertia;
      if (k != 0) k = 1 / k; else c->jacc[0] = 0;
      if (!stiff) {
        k *= soft_gamma(c->frequency, c->damping, dt, &c->gamma, &biascoef);
        c->bias[0] = -C * biascoef;
      } else c->bias[0] = 0, c->gamma = 0;
      c->k[0] = k;
      c->jacc[0] *= dtratio;
    }
    break;
  }
  case C_LINE: {
    if (c->jmin == c->jmax) c->flags |= CF_EQUAL; else c->flags &= (uint8_t)~CF_EQUAL;
    c->a1rel = anchor(b1, c->a1), c->nrel = anchor(b1, c->nlocal), c->a2rel = anchor(b2, c->a2);
    V2 d;
    float Cx, Cy;
    line_cerr(c, c->a1rel, c->a2rel, c->nrel, &d, &Cx, &Cy, &c->scale);
    line_keff(c, c->a1rel, c->a2rel, c->nrel, d, c->scale, c->k, &c->dot1, &c->cx1, &c->dot2, &c->cx2);
    int flag = mat2_invert(c->k);
    if (flag & 1) c->jacc[0] = 0;
    if (flag & 2) c->jacc[1] = 0;
    if (!stiff) {
      float ig = soft_gamma(c->frequency, c->damping, dt, &c->gamma, &biascoef);
      c->k[0] *= ig, c->k[1] *= ig, c->k[2] *= ig;
      c->bias[0] = -Cx * biascoef, c->bias[1] = -Cy * biascoef;
    } else c->gamma = 0, c->bias[0] = c->bias[1] = 0;
    c->jacc[0] *= dtratio, c->jacc[1] *= dtratio;
    break;
  }
  case C_ANGLE: {
    if (c->jmin == c->jmax) c->flags |= CF_EQUAL; else c->flags &= (uint8_t)~CF_EQUAL;
    bool slack;
    float C = angle_cerr(c, &c->scale, &slack);
    if (slack) c->flags |= CF_SLACK; else c->flags &= (uint8_t)~CF_SLACK;
    if (!slack) {
      float k = b1->iinertia + c->ratio * c->ratio * b2->iinertia;
      if (k != 0) k = 1 / k; else c->jacc[0] = 0;
      if (!stiff) {
        k *= soft_gamma(c->frequency, c->damping, dt, &c->gamma, &biascoef);
        c->bias[0] = -C * biascoef;
      } else c->bias[0] = 0, c->gamma = 0;
      c->k[0] = k;
      c->jacc[0] *= dtratio;
    }
    break;
  }
  case C_MOTOR:
    c->k[0] = 1 / (b1->iinertia + c->ratio * c->ratio * b2->iinertia);
    c->jacc[0] *= dtratio;
    break;
  }
}

static void con_warm(Con *c) {
  Body *b1 = c->b1, *b2 = c->b2;
  switch (c->type) {
  case C_PIVOT: pivot_imp(b1, b2, c->a1rel, c->a2rel, c->jacc[0], c->jacc[1], false); break;
  case C_WELD: weld_imp(b1, b2, c->a1rel, c->a2rel, c->jacc, false); break;
  case C_DIST:
    if (!(c->flags & CF_SLACK)) dist_imp(b1, b2, c->n, c->cx1, c->cx2, c->jacc[0], false);
    break;
  case C_LINE:
    line_imp(b1, b2, c->nrel, c->scale, c->dot1, c->cx1, c->dot2, c->cx2, c->jacc[0], c->jacc[1], false);
    break;
  case C_ANGLE:
    if (!(c->flags & CF_SLACK)) angle_imp(c, c->scale, c->jacc[0], false);
    break;
  case C_MOTOR:
    b1->angvel -= b1->iinertia * c->jacc[0];
    b2->angvel += c->ratio * b2->iinertia * c->jacc[0];
    break;
  }
}

static void con_vel(Con *c) {
  Body *b1 = c->b1, *b2 = c->b2;
  switch (c->type) {
  case C_PIVOT: {
    V2 r1 = c->a1rel, r2 = c->a2rel;
    float Ex = (b2->vel.x - r2.y * b2->angvel) - (b1->vel.x - r1.y * b1->angvel);
    float Ey = (b2->vel.y + r2.x * b2->angvel) - (b1->vel.y + r1.x * b1->angvel);
    float jx = c->bias[0] - Ex, jy = c->bias[1] - Ey;
    float t = c->k[0] * jx + c->k[1] * jy;
    jy = c->k[1] * jx + c->k[2] * jy, jx = t;
    jx -= c->jacc[0] * c->gamma, jy -= c->jacc[1] * c->gamma;
    float ox = c->jacc[0], oy = c->jacc[1];
    c->jacc[0] += jx, c->jacc[1] += jy;
    jx = c->jacc[0] - ox, jy = c->jacc[1] - oy;
    pivot_imp(b1, b2, r1, r2, jx, jy, false);
    break;
  }
  case C_WELD: {
    V2 r1 = c->a1rel, r2 = c->a2rel;
    float E[3];
    E[0] = (b2->vel.x - r2.y * b2->angvel) - (b1->vel.x - r1.y * b1->angvel);
    E[1] = (b2->vel.y + r2.x * b2->angvel) - (b1->vel.y + r1.x * b1->angvel);
    E[2] = b2->angvel - b1->angvel;
    float J[3] = {c->bias[0] - E[0], c->bias[1] - E[1], c->bias[2] - E[2]};
    mat3_xform(c->k, J);
    for (int i = 0; i < 3; i++) J[i] -= c->jacc[i] * c->gamma;
    for (int i = 0; i < 3; i++) {
      float o = c->jacc[i];
      c->jacc[i] += J[i];
      J[i] = c->jacc[i] - o;
    }
    weld_imp(b1, b2, r1, r2, J, false);
    break;
  }
  case C_DIST: {
    if (c->flags & CF_SLACK) break;
    float E = c->n.x * (b2->vel.x - b1->vel.x) + c->n.y * (b2->vel.y - b1->vel.y) + b2->angvel * c->cx2 - b1->angvel * c->cx1;
    float j = c->k[0] * (c->bias[0] - E) - c->jacc[0] * c->gamma;
    float o = c->jacc[0];
    c->jacc[0] += j;
    if (!(c->flags & CF_EQUAL) && c->jacc[0] > 0) c->jacc[0] = 0;
    j = c->jacc[0] - o;
    dist_imp(b1, b2, c->n, c->cx1, c->cx2, j, false);
    break;
  }
  case C_LINE: {
    V2 n = c->nrel;
    float dvx = b2->vel.x - b1->vel.x, dvy = b2->vel.y - b1->vel.y;
    float Ex = cross(n.x, n.y, dvx, dvy) + b2->angvel * c->dot2 - b1->angvel * c->dot1;
    float Ey = c->scale * (n.x * dvx + n.y * dvy - b2->angvel * c->cx2 + b1->angvel * c->cx1);
    float jx = c->bias[0] - Ex, jy = c->bias[1] - Ey;
    float t = c->k[0] * jx + c->k[1] * jy;
    jy = c->k[1] * jx + c->k[2] * jy, jx = t;
    jx -= c->jacc[0] * c->gamma, jy -= c->jacc[1] * c->gamma;
    float ox = c->jacc[0], oy = c->jacc[1];
    c->jacc[0] += jx, c->jacc[1] += jy;
    if (c->jacc[1] > 0) c->jacc[1] = 0;
    jx = c->jacc[0] - ox, jy = c->jacc[1] - oy;
    line_imp(b1, b2, n, c->scale, c->dot1, c->cx1, c->dot2, c->cx2, jx, jy, false);
    break;
  }
  case C_ANGLE: {
    if (c->flags & CF_SLACK) break;
    float E = c->scale * (c->ratio * b2->angvel - b1->angvel);
    float j = c->k[0] * (c->bias[0] - E) - c->jacc[0] * c->gamma;
    float o = c->jacc[0];
    c->jacc[0] += j;
    if (!(c->flags & CF_EQUAL) && c->jacc[0] > 0) c->jacc[0] = 0;
    j = c->jacc[0] - o;
    angle_imp(c, c->scale, j, false);
    break;
  }
  case C_MOTOR: {
    float E = c->ratio * b2->angvel - b1->angvel - c->rate;
    float j = -c->k[0] * E;
    c->jacc[0] += j;
    b1->angvel -= b1->iinertia * j;
    b2->angvel += c->ratio * b2->iinertia * j;
    break;
  }
  }
}

static void con_pos(Con *c) {
  Body *b1 = c->b1, *b2 = c->b2;
  switch (c->type) {
  case C_PIVOT: {
    V2 r1 = anchor(b1, c->a1), r2 = anchor(b2, c->a2);
    float Ex = b2->pos.x + r2.x - (b1->pos.x + r1.x), Ey = b2->pos.y + r2.y - (b1->pos.y + r1.y);
    if (Ex * Ex + Ey * Ey < LIN_SLOP * LIN_SLOP) return;
    Ex *= 0.5f, Ey *= 0.5f;
    if (Ex * Ex + Ey * Ey > 6) {
      float k = b1->imass + b2->imass;
      if (k > EPS) {
        k = 0.75f / k;
        float jx = -Ex * k, jy = -Ey * k;
        clamp2(&jx, &jy, 20);
        b1->pos.x -= jx * b1->imass, b1->pos.y -= jy * b1->imass;
        b2->pos.x += jx * b2->imass, b2->pos.y += jy * b2->imass;
        Ex = 0.5f * (b2->pos.x + r2.x - (b1->pos.x + r1.x)), Ey = 0.5f * (b2->pos.y + r2.y - (b1->pos.y + r1.y));
      }
    }
    float K[3];
    pivot_keff(b1, b2, r1, r2, K);
    float jx = -Ex, jy = -Ey;
    clamp2(&jx, &jy, 6);
    mat2_solve(K, &jx, &jy);
    pivot_imp(b1, b2, r1, r2, jx, jy, true);
    break;
  }
  case C_WELD: {
    V2 r1 = anchor(b1, c->a1), r2 = anchor(b2, c->a2);
    float E[3] = {b2->pos.x + r2.x - (b1->pos.x + r1.x), b2->pos.y + r2.y - (b1->pos.y + r1.y), b2->rot - b1->rot - c->phase};
    bool cont = true;
    if (E[0] * E[0] + E[1] * E[1] < LIN_SLOP * LIN_SLOP) { cont = false; E[0] = E[1] = 0; }
    if (E[2] * E[2] < ANG_SLOP * ANG_SLOP) {
      if (!cont) return;
      E[2] = 0;
    }
    E[0] *= 0.5f, E[1] *= 0.5f, E[2] *= 0.5f;
    if (E[0] * E[0] + E[1] * E[1] > 6) {
      float k = b1->imass + b2->imass;
      if (k > EPS) {
        k = 0.75f / k;
        float jx = -E[0] * k, jy = -E[1] * k;
        clamp2(&jx, &jy, 20);
        b1->pos.x -= jx * b1->imass, b1->pos.y -= jy * b1->imass;
        b2->pos.x += jx * b2->imass, b2->pos.y += jy * b2->imass;
        E[0] = 0.5f * (b2->pos.x + r2.x - (b1->pos.x + r1.x));
        E[1] = 0.5f * (b2->pos.y + r2.y - (b1->pos.y + r1.y));
        E[2] = 0.5f * (b2->rot - b1->rot - c->phase);
      }
    }
    float K[6];
    weld_keff(b1, b2, r1, r2, K);
    float J[3] = {-E[0], -E[1], -E[2]};
    clamp2(&J[0], &J[1], 6);
    J[2] = J[2] > 0.25f ? 0.25f : J[2] < -0.25f ? -0.25f : J[2];
    mat3_solve(K, J);
    weld_imp(b1, b2, r1, r2, J, true);
    break;
  }
  case C_DIST: {
    V2 r1 = anchor(b1, c->a1), r2 = anchor(b2, c->a2), n;
    bool slack;
    float E = dist_cerr(c, r1, r2, &n, &slack);
    if (slack) return;
    if (E * E < LIN_SLOP * LIN_SLOP) return;
    E *= 0.5f;
    bool equal = c->flags & CF_EQUAL;
    if (E * E > 6) {
      float k = b1->imass + b2->imass;
      if (k > EPS) {
        k = 0.75f / k;
        float j = -E * k;
        if (equal || j < 0) {
          b1->pos.x -= n.x * j * b1->imass, b1->pos.y -= n.y * j * b1->imass;
          b2->pos.x += n.x * j * b2->imass, b2->pos.y += n.y * j * b2->imass;
          E = 0.5f * dist_cerr(c, r1, r2, &n, &slack);
        }
      }
    }
    float cx1 = cross(r1.x, r1.y, n.x, n.y), cx2 = cross(r2.x, r2.y, n.x, n.y);
    float k = b1->imass + b2->imass + cx1 * cx1 * b1->iinertia + cx2 * cx2 * b2->iinertia;
    if (k != 0) k = 1 / k;
    float j = -E * k;
    if (equal || j < 0) dist_imp(b1, b2, n, cx1, cx2, j, true);
    break;
  }
  case C_LINE: {
    V2 n = anchor(b1, c->nlocal), r1 = anchor(b1, c->a1), r2 = anchor(b2, c->a2), d;
    float Ex, Ey, scale;
    line_cerr(c, r1, r2, n, &d, &Ex, &Ey, &scale);
    if (Ex * Ex + Ey * Ey < LIN_SLOP * LIN_SLOP) return;
    Ex *= 0.5f, Ey *= 0.5f;
    if (Ex * Ex + Ey * Ey > 6) {
      float k = b1->imass + b2->imass;
      if (k > EPS) {
        k = 0.8f / k;
        /* (as Nape writes it) */
        float jx = k * (n.y * Ex - scale * n.x * Ey), jy = k * (n.x * Ex * scale - n.y * Ex);
        b1->pos.x -= jx * b1->imass, b1->pos.y -= jy * b1->imass;
        b2->pos.x += jx * b2->imass, b2->pos.y += jy * b2->imass;
        line_cerr(c, r1, r2, n, &d, &Ex, &Ey, &scale);
        Ex *= 0.5f, Ey *= 0.5f;
      }
    }
    float K[3], dot1, cx1, dot2, cx2;
    line_keff(c, r1, r2, n, d, scale, K, &dot1, &cx1, &dot2, &cx2);
    float jx = -Ex, jy = -Ey;
    mat2_solve(K, &jx, &jy);
    if (jy > 0) jy = 0;
    line_imp(b1, b2, n, scale, dot1, cx1, dot2, cx2, jx, jy, true);
    break;
  }
  case C_ANGLE: {
    float scale;
    bool slack;
    float E = angle_cerr(c, &scale, &slack);
    if (slack) return;
    E *= 0.5f;
    float j = -E * c->k[0];
    if ((c->flags & CF_EQUAL) || j < 0) angle_imp(c, scale, j, true);
    break;
  }
  default: break;
  }
}

static Arb *col_list[NP_MAX_ARBS], *fl_list[16];

void np_step(float dt) {
  sp.stamp++;
  pre_dt_space = dt;
  /* validation: the world shapes of moving bodies */
  for (int i = 0; i < sp.nbodies; i++) {
    Body *b = sp.bodies[i];
    if ((b->flags & BF_SPACE) && !(b->flags & BF_STILL)) body_world(b);
  }
  /* broad and narrow phases */
  for (int i = 0; i < sp.nbodies; i++) {
    Body *a = sp.bodies[i];
    if (!(a->flags & BF_SPACE) || (a->flags & BF_STILL)) continue;
    bool kin = a->type == B_KINEMATIC;
    if (kin && a->vel.x == 0 && a->vel.y == 0 && a->angvel == 0) continue;
    for (int si = 0; si < a->nshapes; si++) {
      Shape *s = &a->shapes[si];
      uint16_t id = s->id;
      /* against the level: static shapes sit in the 256 px columns they span */
      if (!kin) {
        int c0 = (int)((s->minx - sp.bucket_x0) / 256), c1 = (int)((s->maxx - sp.bucket_x0) / 256);
        if (c0 < 0) c0 = 0;
        if (c1 >= sp.nbuckets) c1 = sp.nbuckets - 1;
        for (int c = c0; c <= c1 + 1; c++) {
          int col = c > c1 ? sp.nbuckets : c;   /* then the wide shapes */
          int first = sp.buckets[2 * col], count = sp.buckets[2 * col + 1];
          for (int k = 0; k < count; k++) {
            uint16_t sid = sp.bucket_list[first + k];
            const SShape *ss = &sp.sshapes[sid];
            if (ss->x0 + ss->w < s->minx || ss->x0 > s->maxx || ss->y0 + ss->h < s->miny || ss->y0 > s->maxy) continue;
            if (!(sp.sbodies[ss->body].flags & BF_SPACE)) continue;
            /* a shape spanning columns is met once, in the first column both share */
            if (col < sp.nbuckets && c > c0 && (int)((ss->x0 - sp.bucket_x0) / 256) < c) continue;
            narrow(id, sid, true);
          }
        }
      }
      /* against the other bodies (two moving ones once) */
      for (int j = 0; j < sp.nbodies; j++) {
        Body *b = sp.bodies[j];
        if (b == a || !(b->flags & BF_SPACE)) continue;
        if (kin && b->type != B_DYNAMIC) continue;
        bool bmoving = (b->flags & BF_FROZEN) ? false
                       : b->type == B_DYNAMIC ? !(b->flags & BF_SLEEP) : (b->vel.x != 0 || b->vel.y != 0 || b->angvel != 0);
        if (bmoving && j < i) continue;
        if (b->maxx < s->minx || b->minx > s->maxx || b->maxy < s->miny || b->miny > s->maxy) continue;
        for (int sj = 0; sj < b->nshapes; sj++) {
          Shape *t = &b->shapes[sj];
          if (t->maxx < s->minx || t->minx > s->maxx || t->maxy < s->miny || t->miny > s->maxy) continue;
          narrow(id, t->id, b->type != B_DYNAMIC || a->type != B_DYNAMIC);
        }
      }
    }
  }
  /* prestep */
  for (int i = 0; i < sp.ncons; i++) {
    Con *c = sp.cons[i];
    if (con_live(c)) con_prestep(c, dt);
  }
  int ncol = 0, nfl = 0;
  for (int pass = 0; pass < 2; pass++)
    for (int i = 0; i < NP_MAX_ARBS; i++) {
      Arb *a = &sp.arbs[i];
      if (!a->used || a->stat != pass) continue;
      if (arb_prestep(a, dt)) {
        if (a->type == A_COL) col_list[ncol++] = a;
        else if (a->type == A_FLUID && nfl < 16) fl_list[nfl++] = a;
      }
    }
  /* updateVel */
  float lin = 1 - dt * DRAG, ang = 1 - dt * DRAG;
  for (int i = 0; i < sp.nbodies; i++) {
    Body *b = sp.bodies[i];
    if (!(b->flags & BF_SPACE) || (b->flags & BF_STILL) || b->type != B_DYNAMIC) continue;
    if (b->imass != 0) {
      float t = dt * b->imass;
      b->vel.x = lin * b->vel.x + (sp.gravity.x * b->mass) * t;
      b->vel.y = lin * b->vel.y + (sp.gravity.y * b->mass) * t;
    }
    if (b->iinertia != 0) {
      V2 wc = rot_axis(b->lcom.x, b->lcom.y, b->axis);
      float torque = b->torque + cross(wc.x, wc.y, sp.gravity.x, sp.gravity.y) * b->mass;
      b->angvel = ang * b->angvel + torque * dt * b->iinertia;
    }
  }
  /* warm start and velocity iterations */
  for (int i = 0; i < nfl; i++) fluid_warm(fl_list[i]);
  for (int i = 0; i < ncol; i++) arb_warm(col_list[i]);
  for (int i = 0; i < sp.ncons; i++) {
    Con *c = sp.cons[i];
    if (con_live(c)) con_warm(c);
  }
  for (int it = 0; it < VEL_ITER; it++) {
    for (int i = 0; i < nfl; i++) fluid_vel(fl_list[i]);
    for (int i = 0; i < sp.ncons; i++) {
      Con *c = sp.cons[i];
      if (con_live(c)) con_vel(c);
    }
    for (int i = 0; i < ncol; i++) arb_vel(col_list[i]);
  }
  /* positions */
  float maxvel = TAU / dt;
  for (int i = 0; i < sp.nbodies; i++) {
    Body *b = sp.bodies[i];
    if (!(b->flags & BF_SPACE) || (b->flags & BF_STILL) || b->type == B_STATIC) continue;
    b->pos.x += b->vel.x * dt, b->pos.y += b->vel.y * dt;
    if (b->angvel != 0) delta_rot(b, fmodf(b->angvel, maxvel) * dt);
  }
  for (int it = 0; it < POS_ITER; it++) {
    for (int i = 0; i < sp.ncons; i++) {
      Con *c = sp.cons[i];
      if (c->type != C_MOTOR && (c->flags & CF_STIFF) && con_live(c)) con_pos(c);
    }
    for (int i = 0; i < ncol; i++) arb_pos(col_list[i]);
  }
  forests(dt);
}

void np_body_add(Body *b) {
  if (b->flags & BF_SPACE) return;
  b->flags |= BF_SPACE;
  b->flags &= (uint8_t)~BF_SLEEP;
  b->waket = sp.stamp;
  body_world(b);
}

void np_body_remove(Body *b) {
  if (!(b->flags & BF_SPACE)) return;
  np_drop_body(b);
  b->flags &= (uint8_t)~(BF_SPACE | BF_SLEEP);
}

void np_apply_impulse(Body *b, V2 j, V2 at) {
  b->vel.x += j.x * b->imass, b->vel.y += j.y * b->imass;
  b->angvel += cross(at.x - b->pos.x, at.y - b->pos.y, j.x, j.y) * b->iinertia;
  np_wake(b);
}

/* ---- queries ---- */

static bool ray_poly(V2 o, V2 d, float maxd, const V2 *gv, const V2 *gn, const float *gp, int n, float *t) {
  float tmin = 0, tmax = maxd;
  for (int i = 0; i < n; i++) {
    float den = gn[i].x * d.x + gn[i].y * d.y, num = gp[i] - (gn[i].x * o.x + gn[i].y * o.y);
    if (den == 0) {
      if (num < 0) return false;
    } else {
      float tt = num / den;
      if (den < 0) { if (tt > tmin) tmin = tt; }
      else if (tt < tmax) tmax = tt;
      if (tmin > tmax) return false;
    }
  }
  (void)gv;
  *t = tmin;
  return true;
}

bool np_raycast(V2 o, V2 d, float maxd, Body **body, int *sbody, float *dist) {
  float best = maxd;
  bool hit = false;
  *body = 0, *sbody = -1;
  for (int i = 0; i < sp.nsshapes; i++) {
    const SShape *s = &sp.sshapes[i];
    if (!(sp.sbodies[s->body].flags & BF_SPACE) || (s->flags & SF_SENSOR)) continue;
    float x0 = o.x < o.x + d.x * best ? o.x : o.x + d.x * best, x1 = o.x < o.x + d.x * best ? o.x + d.x * best : o.x;
    if (s->x0 + s->w < x0 || s->x0 > x1) continue;
    SV v;
    sview((uint16_t)i, &v);
    float t;
    if (ray_poly(o, d, best, v.gv, v.gn, v.gp, v.n, &t) && t <= best) best = t, hit = true, *body = 0, *sbody = s->body;
  }
  for (int i = 0; i < sp.nbodies; i++) {
    Body *b = sp.bodies[i];
    if (!(b->flags & BF_SPACE)) continue;
    for (int k = 0; k < b->nshapes; k++) {
      Shape *s = &b->shapes[k];
      if (s->flags & SF_SENSOR) continue;
      float t;
      if (s->type == SH_CIRCLE) {
        float ox = o.x - s->wcom.x, oy = o.y - s->wcom.y, bq = ox * d.x + oy * d.y, c = ox * ox + oy * oy - s->radius * s->radius;
        float disc = bq * bq - c;
        if (disc < 0) continue;
        t = -bq - sqrtf(disc);
        if (t < 0) t = 0;
        if (t > best) continue;
      } else if (!ray_poly(o, d, best, (const V2 *)(const void *)s->glob, (const V2 *)(const void *)(s->glob + 2 * s->n),
                           s->glob + 4 * s->n, s->n, &t) || t > best) continue;
      best = t, hit = true, *body = b, *sbody = -1;
    }
  }
  *dist = best;
  return hit;
}

int np_bodies_under(V2 p, Body **out, int max, int *sbody) {
  int n = 0;
  *sbody = -1;
  for (int i = 0; i < sp.nbodies && n < max; i++) {
    Body *b = sp.bodies[i];
    if (!(b->flags & BF_SPACE)) continue;
    body_world(b);
    for (int k = 0; k < b->nshapes; k++) {
      Shape *s = &b->shapes[k];
      bool in;
      if (s->type == SH_CIRCLE) {
        float dx = p.x - s->wcom.x, dy = p.y - s->wcom.y;
        in = dx * dx + dy * dy < s->radius * s->radius;
      } else {
        in = true;
        const float *gn = s->glob + 2 * s->n, *gp = s->glob + 4 * s->n;
        for (int e = 0; e < s->n && in; e++) in = gn[2 * e] * p.x + gn[2 * e + 1] * p.y <= gp[e];
      }
      if (in) { out[n++] = b; break; }
    }
  }
  for (int i = 0; i < sp.nsshapes; i++) {
    const SShape *s = &sp.sshapes[i];
    if (!(sp.sbodies[s->body].flags & BF_SPACE)) continue;
    if (p.x < s->x0 || p.x > s->x0 + s->w || p.y < s->y0 || p.y > s->y0 + s->h) continue;
    SV v;
    sview((uint16_t)i, &v);
    bool in = true;
    for (int e = 0; e < v.n && in; e++) in = v.gn[e].x * p.x + v.gn[e].y * p.y <= v.gp[e];
    if (in) { *sbody = s->body; break; }
  }
  return n;
}


void np_body_moved(Body *b) {
  body_world(b);
  np_wake(b);
}

void np_register(Body *b) {
  b->idx = (int16_t)sp.nbodies;
  sp.bodies[sp.nbodies++] = b;
  for (int i = 0; i < b->nshapes; i++) {
    Shape *s = &b->shapes[i];
    s->body = b;
    s->id = (uint16_t)(0x8000 | sp.nshapes);
    sp.shapes[sp.nshapes++] = s;
  }
  np_body_mass(b);
}

void np_register_con(Con *c) {
  sp.cons[sp.ncons++] = c;
  c->pre_dt = -1;
}

void np_window(float x0, float x1) {
  for (int i = 0; i < sp.nbodies; i++) {
    Body *b = sp.bodies[i];
    if (!(b->flags & BF_SPACE) || (b->flags & BF_KEEP)) continue;
    bool out = b->maxx < x0 || b->minx > x1;
    if (out) b->flags |= BF_FROZEN;
    else if (b->flags & BF_FROZEN) {
      b->flags &= (uint8_t)~BF_FROZEN;
      b->waket = sp.stamp;
    }
  }
}
