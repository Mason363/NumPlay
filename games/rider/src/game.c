/* The ride: the tracks (levels, or the endless run's pieces one after another), their moving and falling parts,
 * and the bike, a rigid body on two wheels sliding on frictionless platforms, as Box2D moves it in the game:
 * the button sets the speed along the bike on the ground and turns it backwards in the air, a crash is the
 * bike's outline hitting the track hard where its front, back, belly or top is.
 *
 * The bike's parts weigh what Box2D gives them in the game (spec: 10.7 kg, 13 kg m^2), so what the game does to
 * its main body (the speed set, the push down in the air, the turning force) moves the whole bike that much. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rider.h"
#include "data.h"

#define DT (1.0f / 60)
#define SUBSTEPS 4
#define GRAV 294.0f          /* 9.8 m/s^2 at 30 px a metre */
#define MASS 10.73f
#define INERTIA 11700.0f     /* kg px^2 */
#define K_SET 0.1407f        /* the main body's share of the bike's weight */
#define DOWN_AIR 4.41f       /* 3.2 x 9.8 px/s on the main body each frame in the air, for the whole bike */
#define DAMP_GROUND 0.01314f /* the wheels' damping 3, for the whole bike, a frame */
#define ACCEL 14.0f
#define ACCEL_BACK 30.0f
#define VMAX 550.0f
#define SPIN_DW (-100.0f / 13.0f * DT) /* the in-air torque, rad/s a frame */
#define WMAX 8.7266f         /* 500 degrees a second */
#define WHEEL_R 8.4f
#define TRIG_R 11.2f
#define WHEEL_Y 13.3f
#define CRASH_FB 1500.0f     /* 50 N s, in kg px/s */
#define CRASH_BOTTOM 4500.0f /* 150 */
#define PI 3.14159265f
#define DEG (PI / 180)

static const float wheel_x[2] = {-34, 38};
/* sensors round the body, x0.7 */
static const float sens_top[] = {-21.35f, -12.95f, 21.35f, -12.95f, 21.35f, 0.35f, -21.35f, 0.35f};
static const float sens_front[] = {26.95f, -3.85f, 40.95f, -3.85f, 40.95f, 10.15f, 35.35f, 10.15f, 26.95f, 1.75f};
static const float sens_back[] = {-40.95f, -3.85f, -26.95f, -3.85f, -26.95f, 1.75f, -35.35f, 10.15f, -40.95f, 10.15f};
static const float sens_bottom[] = {-26.95f, 1.75f, 26.95f, 1.75f, 38.15f, 12.95f, -38.15f, 12.95f};

uint32_t rd_rand(uint32_t *s) {
  uint32_t x = *s ? *s : 0x9E3779B9u;
  x ^= x << 13, x ^= x >> 17, x ^= x << 5;
  return *s = x;
}

/* ---------------------------------------------------------------- easing (Corona's) */
static float bounce(float t) {
  if (t < 1 / 2.75f) return 7.5625f * t * t;
  if (t < 2 / 2.75f) return t -= 1.5f / 2.75f, 7.5625f * t * t + 0.75f;
  if (t < 2.5f / 2.75f) return t -= 2.25f / 2.75f, 7.5625f * t * t + 0.9375f;
  return t -= 2.625f / 2.75f, 7.5625f * t * t + 0.984375f;
}
static float ease(int e, float t) {
  switch (e) {
  case 1: return t < 0.5f ? 2 * t * t : -1 + (4 - 2 * t) * t;
  case 2: return bounce(t);
  case 3: t -= 1; return t * t * (2.70158f * t + 1.70158f) + 1;
  case 4: return t * t;
  case 5: return t * (2 - t);
  case 6: return -(cosf(PI * t) - 1) / 2;
  default: return t;
  }
}
/* how far a list of steps has moved by time t (ms) */
static float steps_at(const rd_step *s, int n, int loops, float t) {
  if (!n || t <= 0) return 0;
  float cycle = 0, sum = 0;
  for (int i = 0; i < n; i++) cycle += s[i].delay + s[i].ms, sum += s[i].delta;
  if (cycle <= 0) return 0;
  float done = 0;
  int k = (int)(t / cycle);
  if (loops > 0 && k >= loops) return sum * loops;
  done = sum * k, t -= cycle * k;
  for (int i = 0; i < n; i++) {
    if (t < s[i].delay) break;
    t -= s[i].delay;
    if (t < s[i].ms) return done + s[i].delta * ease(s[i].ease, t / s[i].ms);
    t -= s[i].ms, done += s[i].delta;
  }
  return done;
}

/* ---------------------------------------------------------------- the world's objects */

static void add_track(rd_run *r, int track) {
  if (r->nseg == MAXSEG) { /* forget the oldest piece */
    int gone = 0;
    for (int i = 0; i < r->nlive; i++)
      if (r->live[i].seg != 0) r->live[gone] = r->live[i], (void)0, gone++;
    /* (parents of the kept ones: the same piece, so their places moved by the same count) */
    int shift = r->nlive - gone;
    for (int i = 0; i < gone; i++)
      if (r->live[i].parent >= 0) r->live[i].parent = (int16_t)(r->live[i].parent - shift);
    r->nlive = gone;
    for (int i = 0; i < r->nlive; i++) r->live[i].seg--;
    memmove(r->seg, r->seg + 1, sizeof r->seg[0] * (MAXSEG - 1));
    r->nseg--;
  }
  const rd_track *t = &rd_tracks[track];
  int s = r->nseg++;
  r->seg[s].track = (int16_t)track, r->seg[s].x = r->end_x;
  int base = r->nlive;
  for (int i = 0; i < t->n && r->nlive < MAXLIVE; i++) {
    rd_live *l = &r->live[r->nlive];
    memset(l, 0, sizeof *l);
    l->obj = (uint16_t)(t->first + i), l->seg = (uint8_t)s, l->parent = -1;
    const rd_obj *o = &rd_objs[l->obj];
    l->cx = (float)(o->cx + r->end_x), l->cy = o->cy, l->rot = o->rot * (PI / 18000);
    l->x0 = (float)(o->x0 + r->end_x), l->x1 = (float)(o->x1 + r->end_x), l->y0 = o->y0, l->y1 = o->y1;
    if (o->anim != 0xFFFF && !rd_anims[o->anim].on_touch) l->st |= ST_RUN;
    if (o->flags & F_DROP) l->st |= ST_TOUCHED;
    r->nlive++;
  }
  for (int i = base; i < r->nlive; i++) { /* grounds that move with their platform */
    const rd_obj *o = &rd_objs[r->live[i].obj];
    if (o->anim == 0xFFFF || rd_anims[o->anim].child == 0xFFFF) continue;
    int c = base + rd_anims[o->anim].child - t->first;
    if (c >= base && c < r->nlive) r->live[c].parent = (int16_t)i;
  }
  r->end_x += t->width;
}

static void endless_more(rd_run *r) {
  int g = 0;
  for (int i = 1; i < 4; i++)
    if (r->score >= endless_from[i]) g = i;
  int at = 0;
  for (int i = 0; i < g; i++) at += endless_n[i];
  uint32_t k = rd_rand(&r->seed) % endless_n[g];
  add_track(r, NLEVELS + endless_pick[at + k]);
}

/* an object's pose now, from its animation and its fall */
static void pose(rd_run *r, int i) {
  rd_live *l = &r->live[i];
  const rd_obj *o = &rd_objs[l->obj];
  int src = r->live[i].parent >= 0 ? r->live[i].parent : i;
  const rd_live *ls = &r->live[src];
  const rd_obj *os = &rd_objs[ls->obj];
  float ox = (float)r->seg[l->seg].x;
  float bx = o->cx + ox, by = o->cy, br = o->rot * (PI / 18000);
  float pcx = l->cx, pcy = l->cy, pr = l->rot;
  if (os->anim != 0xFFFF) {
    const rd_anim *a = &rd_anims[os->anim];
    const rd_step *s = &rd_steps[a->first];
    float t = (ls->st & ST_RUN) ? ls->t - a->start_delay : 0;
    float dr = steps_at(s, a->nrot, a->loops, t) * DEG;
    float dx = steps_at(s + a->nrot, a->nx, a->loops, t), dy = steps_at(s + a->nrot + a->nx, a->ny, a->loops, t);
    if (ls->st & ST_FALL) dy += 0.5f * GRAV * (ls->ft / 1000) * (ls->ft / 1000);
    float px = a->px + ox, py = a->py, c = cosf(dr), sn = sinf(dr);
    l->cx = px + c * (bx - px) - sn * (by - py) + dx;
    l->cy = py + sn * (bx - px) + c * (by - py) + dy;
    l->rot = br + dr;
  } else {
    l->cx = bx, l->cy = by, l->rot = br;
  }
  l->dx = l->cx - pcx, l->dy = l->cy - pcy, l->dr = l->rot - pr;
  /* the box: the data's, turned and moved the same way */
  if (os->anim != 0xFFFF) {
    float c = cosf(l->rot - br), sn = sinf(l->rot - br), mx = 1e9f, my = 1e9f, Mx = -1e9f, My = -1e9f;
    float xs[2] = {o->x0 + ox - bx, o->x1 + ox - bx}, ys[2] = {(float)o->y0 - by, (float)o->y1 - by};
    for (int k = 0; k < 4; k++) {
      float x = xs[k & 1], y = ys[k >> 1], X = l->cx + c * x - sn * y, Y = l->cy + sn * x + c * y;
      mx = fminf(mx, X), my = fminf(my, Y), Mx = fmaxf(Mx, X), My = fmaxf(My, Y);
    }
    l->x0 = mx, l->y0 = my, l->x1 = Mx, l->y1 = My;
  }
}

/* polygon k of an object, in the world: returns its number of points */
int obj_poly(const rd_run *r, int i, int k, float *xy) {
  const rd_live *l = &r->live[i];
  const rd_obj *o = &rd_objs[l->obj];
  float c = cosf(l->rot), s = sinf(l->rot);
  if (o->tpl == 0xFF) { /* a gem or the finish: its box */
    if (k) return 0;
    float hw = o->sx / 2, hh = o->sy / 2, q[8] = {-hw, -hh, hw, -hh, hw, hh, -hw, hh};
    for (int j = 0; j < 4; j++) xy[2 * j] = l->cx + c * q[2 * j] - s * q[2 * j + 1], xy[2 * j + 1] = l->cy + s * q[2 * j] + c * q[2 * j + 1];
    return 4;
  }
  int p = tpl_poly[o->tpl] + k;
  if (p >= tpl_poly[o->tpl + 1]) return 0;
  int v0 = poly_vert[p], n = poly_vert[p + 1] - v0;
  bool flip = (o->sx < 0) != (o->sy < 0);
  for (int j = 0; j < n; j++) {
    int v = v0 + (flip ? n - 1 - j : j);
    float x = tpl_xy[2 * v] * 0.25f * o->sx, y = tpl_xy[2 * v + 1] * 0.25f * o->sy;
    xy[2 * j] = l->cx + c * x - s * y, xy[2 * j + 1] = l->cy + s * x + c * y;
  }
  return n;
}

/* ---------------------------------------------------------------- geometry */
/* the point of a convex polygon (clockwise on screen) nearest to p: distance, signed (negative inside), normal out */
static float poly_dist(const float *xy, int n, float px, float py, float *nx, float *ny) {
  float best = -1e9f, bnx = 0, bny = -1;
  bool inside = true;
  float out_d = 1e9f, onx = 0, ony = -1;
  for (int j = 0; j < n; j++) {
    float ax = xy[2 * j], ay = xy[2 * j + 1], bx = xy[2 * ((j + 1) % n)], by = xy[2 * ((j + 1) % n) + 1];
    float ex = bx - ax, ey = by - ay, len = sqrtf(ex * ex + ey * ey);
    if (len < 1e-4f) continue;
    float mx = ey / len, my = -ex / len; /* outward for clockwise-on-screen */
    float d = (px - ax) * mx + (py - ay) * my;
    if (d > best) best = d, bnx = mx, bny = my;
    if (d > 0) inside = false;
    /* the distance to the edge as a segment */
    float t = ((px - ax) * ex + (py - ay) * ey) / (len * len);
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    float qx = ax + ex * t - px, qy = ay + ey * t - py, dd = sqrtf(qx * qx + qy * qy);
    if (dd < out_d) out_d = dd, onx = dd > 1e-5f ? -qx / dd : mx, ony = dd > 1e-5f ? -qy / dd : my;
  }
  if (inside) return *nx = bnx, *ny = bny, best;
  return *nx = onx, *ny = ony, out_d;
}
static bool polys_overlap(const float *a, int na, const float *b, int nb) {
  for (int pass = 0; pass < 2; pass++) {
    const float *p = pass ? b : a;
    int n = pass ? nb : na;
    for (int j = 0; j < n; j++) {
      float ex = p[2 * ((j + 1) % n)] - p[2 * j], ey = p[2 * ((j + 1) % n) + 1] - p[2 * j + 1];
      float mx = ey, my = -ex, amin = 1e9f, amax = -1e9f, bmin = 1e9f, bmax = -1e9f;
      for (int k = 0; k < na; k++) { float d = a[2 * k] * mx + a[2 * k + 1] * my; amin = fminf(amin, d), amax = fmaxf(amax, d); }
      for (int k = 0; k < nb; k++) { float d = b[2 * k] * mx + b[2 * k + 1] * my; bmin = fminf(bmin, d), bmax = fmaxf(bmax, d); }
      if (amax < bmin || bmax < amin) return false;
    }
  }
  return true;
}

/* ---------------------------------------------------------------- the bike */
static void to_world(const rd_bikestate *b, float lx, float ly, float *x, float *y) {
  float c = cosf(b->a), s = sinf(b->a);
  *x = b->x + c * lx - s * ly, *y = b->y + s * lx + c * ly;
}

typedef struct {
  float px, py, nx, ny, depth, jn, mass;
  float rx, ry;
  float vx, vy; /* the track's own speed there */
  int16_t obj;
  uint8_t hull;
} contact;
#define MAXC 48
static contact cs[MAXC];
static int ncs;
static float hull_impulse;
static bool hit_enemy;
static int touched_boost;

static bool solid(const rd_live *l) {
  const rd_obj *o = &rd_objs[l->obj];
  return (o->kind == K_GROUND || o->kind == K_PLATFORM || o->kind == K_BOOST) && !(l->st & ST_GONE);
}

static void add_contact(rd_run *r, int obj, float px, float py, float nx, float ny, float depth, bool hull) {
  if (ncs == MAXC) return;
  contact *c = &cs[ncs++];
  rd_bikestate *b = &r->b;
  c->px = px, c->py = py, c->nx = nx, c->ny = ny, c->depth = depth, c->jn = 0, c->hull = hull, c->obj = (int16_t)obj;
  c->rx = px - b->x, c->ry = py - b->y;
  float rn = c->rx * ny - c->ry * nx;
  c->mass = 1 / (1 / MASS + (b->fixed ? 0 : rn * rn / INERTIA));
  const rd_live *l = &r->live[obj];
  /* the track's speed at that point (moving and turning parts) */
  float w = l->dr * 60, ax = px - l->cx, ay = py - l->cy;
  c->vx = l->dx * 60 - w * ay, c->vy = l->dy * 60 + w * ax;
}

/* contacts of the wheels and the bike's outline with the track near it */
static void find_contacts(rd_run *r, int bike) {
  rd_bikestate *b = &r->b;
  ncs = 0;
  float wx[2], wy[2];
  for (int k = 0; k < 2; k++) to_world(b, wheel_x[k], WHEEL_Y, &wx[k], &wy[k]);
  /* the outline */
  const rd_bike *bk = &rd_bikes[bike];
  float hull[8][16];
  int hn[8], nh = bk->hn > 8 ? 8 : bk->hn;
  float hx0 = 1e9f, hy0 = 1e9f, hx1 = -1e9f, hy1 = -1e9f;
  for (int h = 0; h < nh; h++) {
    int v0 = hull_poly[bk->h0 + h], n = hull_poly[bk->h0 + h + 1] - v0;
    if (n > 8) n = 8;
    hn[h] = n;
    for (int j = 0; j < n; j++) {
      to_world(b, hull_xy[2 * (v0 + j)] * 0.5f, hull_xy[2 * (v0 + j) + 1] * 0.5f, &hull[h][2 * j], &hull[h][2 * j + 1]);
      hx0 = fminf(hx0, hull[h][2 * j]), hx1 = fmaxf(hx1, hull[h][2 * j]);
      hy0 = fminf(hy0, hull[h][2 * j + 1]), hy1 = fmaxf(hy1, hull[h][2 * j + 1]);
    }
  }
  float bx0 = fminf(fminf(wx[0], wx[1]) - TRIG_R, hx0), bx1 = fmaxf(fmaxf(wx[0], wx[1]) + TRIG_R, hx1);
  float by0 = fminf(fminf(wy[0], wy[1]) - TRIG_R, hy0), by1 = fmaxf(fmaxf(wy[0], wy[1]) + TRIG_R, hy1);
  b->wheel_down[0] = b->wheel_down[1] = 0;
  float xy[64];
  for (int i = 0; i < r->nlive; i++) {
    rd_live *l = &r->live[i];
    if (l->x1 < bx0 || l->x0 > bx1 || l->y1 < by0 || l->y0 > by1 || (l->st & ST_GONE)) continue;
    const rd_obj *o = &rd_objs[l->obj];
    if (o->kind == K_GEM || o->kind == K_FINISH) continue;
    for (int k = 0;; k++) {
      int n = obj_poly(r, i, k, xy);
      if (!n) break;
      if (n > 32) n = 32;
      for (int w = 0; w < 2; w++) {
        float nx, ny, d = poly_dist(xy, n, wx[w], wy[w], &nx, &ny);
        if (d >= TRIG_R) continue;
        if (o->kind == K_ENEMY) { if (d < WHEEL_R - 2) hit_enemy = true; continue; }
        if (o->kind == K_BOOST) touched_boost = i;
        if (!solid(l)) continue;
        b->wheel_down[w] = 1;
        l->st |= ST_TOUCHED;
        if (d < WHEEL_R) add_contact(r, i, wx[w] - nx * WHEEL_R, wy[w] - ny * WHEEL_R, nx, ny, WHEEL_R - d, false);
      }
      for (int h = 0; h < nh; h++) {
        if (!polys_overlap(hull[h], hn[h], xy, n)) continue;
        if (o->kind == K_ENEMY) { /* (more than a touch: the game's start positions sit that close) */
          for (int j = 0; j < hn[h]; j++) {
            float nx, ny;
            if (poly_dist(xy, n, hull[h][2 * j], hull[h][2 * j + 1], &nx, &ny) < -1.5f) hit_enemy = true;
          }
          for (int j = 0; j < n; j++) {
            float nx, ny;
            if (poly_dist(hull[h], hn[h], xy[2 * j], xy[2 * j + 1], &nx, &ny) < -1.5f) hit_enemy = true;
          }
          continue;
        }
        if (o->kind == K_BOOST) touched_boost = i;
        if (!solid(l)) continue;
        l->st |= ST_TOUCHED;
        /* the outline's corners in the track, and the track's corners in the outline */
        for (int j = 0; j < hn[h]; j++) {
          float nx, ny, d = poly_dist(xy, n, hull[h][2 * j], hull[h][2 * j + 1], &nx, &ny);
          if (d < 0) add_contact(r, i, hull[h][2 * j], hull[h][2 * j + 1], nx, ny, -d, true);
        }
        for (int j = 0; j < n; j++) {
          float nx, ny, d = poly_dist(hull[h], hn[h], xy[2 * j], xy[2 * j + 1], &nx, &ny);
          if (d < 0) add_contact(r, i, xy[2 * j], xy[2 * j + 1], -nx, -ny, -d, true);
        }
      }
    }
  }
}

static bool sensor_on_track(rd_run *r, const float *sens, int n) {
  float s[10], xy[64];
  for (int j = 0; j < n; j++) to_world(&r->b, sens[2 * j], sens[2 * j + 1], &s[2 * j], &s[2 * j + 1]);
  float x0 = s[0], x1 = s[0], y0 = s[1], y1 = s[1];
  for (int j = 1; j < n; j++) x0 = fminf(x0, s[2 * j]), x1 = fmaxf(x1, s[2 * j]), y0 = fminf(y0, s[2 * j + 1]), y1 = fmaxf(y1, s[2 * j + 1]);
  for (int i = 0; i < r->nlive; i++) {
    rd_live *l = &r->live[i];
    if (l->x1 < x0 || l->x0 > x1 || l->y1 < y0 || l->y0 > y1 || !solid(l)) continue;
    if (rd_objs[l->obj].flags & F_SAFE) continue;
    for (int k = 0;; k++) {
      int m = obj_poly(r, i, k, xy);
      if (!m) break;
      if (polys_overlap(s, n, xy, m > 32 ? 32 : m)) return true;
    }
  }
  return false;
}

static void solve(rd_run *r) {
  rd_bikestate *b = &r->b;
  for (int it = 0; it < 6; it++) {
    for (int k = 0; k < ncs; k++) {
      contact *c = &cs[k];
      float vx = b->vx - b->w * c->ry - c->vx, vy = b->vy + b->w * c->rx - c->vy;
      float vn = vx * c->nx + vy * c->ny;
      float j = c->mass * -vn, old = c->jn; /* (no push from the overlap: that's put right below, as Box2D does) */
      c->jn = fmaxf(old + j, 0);
      j = c->jn - old;
      b->vx += j * c->nx / MASS, b->vy += j * c->ny / MASS;
      if (!b->fixed) b->w += (c->rx * c->ny - c->ry * c->nx) * j / INERTIA;
    }
  }
  for (int k = 0; k < ncs; k++)
    if (cs[k].hull) hull_impulse += cs[k].jn;
}

/* ---------------------------------------------------------------- a run */
void run_start(rd_run *r, int mode, int level, int bike, int theme, uint32_t seed) {
  memset(r, 0, sizeof *r);
  r->mode = mode, r->level = level, r->bike = bike, r->theme = theme, r->seed = seed ? seed : 1;
  int first = mode == RUN_LEVEL ? level : NLEVELS; /* (the endless run starts with its start piece) */
  add_track(r, first);
  const rd_track *t = &rd_tracks[first];
  rd_bikestate *b = &r->b;
  b->x = t->sx, b->y = t->sy;
  b->flip_start = 0;
  r->finish_x = mode == RUN_LEVEL ? (float)t->finish : 1e30f;
  float bottom = 0;
  for (int i = 0; i < t->n; i++) bottom = fmaxf(bottom, rd_objs[t->first + i].y0);
  r->bottom = mode == RUN_LEVEL ? bottom + 300 : 2200;
  if (mode == RUN_ENDLESS)
    while (r->end_x < b->x + 2400) endless_more(r);
  /* settle the bike on the track */
  for (int i = 0; i < r->nlive; i++) pose(r, i);
  for (int i = 0; i < 60; i++) {
    float y = b->y;
    run_step(r, false);
    b->x = t->sx, b->vx = 0, b->w *= 0.5f;
    if (fabsf(b->y - y) < 0.01f && b->ground) break;
  }
  r->ms = 0, r->ended = END_NONE, r->nbits = 0, b->trail_n = 0, b->air_ms = 0, b->speed = 0;
  b->rot_abs = b->a / DEG, b->flip_start = b->rot_abs;
  r->camx = b->x - 200, r->camy = b->y - 260;
}

const char *crash_why;
static void crash(rd_run *r) {
  if (r->ended) return;
  r->ended = END_CRASH, r->end_ms = 0, r->pending = 0, r->landed_flips = 0;
  rd_bikestate *b = &r->b;
  /* the bike in pieces, as the game breaks it in six, and sparks */
  r->nbits = 0;
  for (int i = 0; i < NBITS; i++) {
    rd_bit *p = &r->bits[r->nbits++];
    uint32_t q = rd_rand(&r->seed);
    float ang = (q & 1023) / 1023.0f * 2 * PI, sp = 60 + (q >> 10 & 255) * 1.2f;
    float lx = i < 6 ? (i % 3 - 1) * 26.0f : 0, ly = i < 6 ? (i / 3) * 12.0f - 6 : 0;
    to_world(b, lx, ly, &p->x, &p->y);
    p->vx = b->vx * 0.4f + cosf(ang) * sp, p->vy = b->vy * 0.4f + sinf(ang) * sp - 120;
    p->a = b->a, p->w = ((q >> 18 & 63) - 32) * 0.4f;
    p->c = i < 6 ? C_BODY : i < 10 ? C_WHITE : C_SPARK, p->size = i < 6 ? 12 : 2;
  }
}

static void say(rd_run *r, const char *t, const char *t2) {
  r->text = t, r->text2 = t2, r->text_ms = 1500;
}

static void landed(rd_run *r) {
  rd_bikestate *b = &r->b;
  int n = b->air_flips;
  bool perfect = n > 0 && fabsf(b->land_t[0] - b->land_t[1]) < 30;
  if (perfect) r->perfect++;
  if (n >= 2) {
    int k = n > 6 ? 6 : n, kind = perfect ? 0 : 1, m = flip_ntexts[(k - 2) * 2 + kind];
    say(r, flip_texts[k - 2][kind][rd_rand(&r->seed) % m], 0);
    r->doubles += n >= 2;
  } else if (n == 1 && perfect) {
    say(r, "Perfect!", 0);
  } else if (n == 1 && b->front_flip) {
    say(r, front_texts[rd_rand(&r->seed) % 2], 0);
  }
  if (b->air_ms >= 2000 && !(n >= 2)) {
    int k = b->air_ms >= 5000 ? 3 : b->air_ms >= 4000 ? 2 : b->air_ms >= 3000 ? 1 : 0;
    say(r, air_texts[k], r->text);
  }
  if (n > r->best_flips_jump) r->best_flips_jump = n;
  b->air_flips = 0, b->front_flip = false;
}

void run_step(rd_run *r, bool held) {
  rd_bikestate *b = &r->b;
  if (!r->ended) r->ms += 1000 * DT;
  if (r->text_ms > 0) r->text_ms -= 1000 * DT;
  /* the world moves */
  for (int i = 0; i < r->nlive; i++) {
    rd_live *l = &r->live[i];
    const rd_obj *o = &rd_objs[l->obj];
    if (o->anim != 0xFFFF) {
      const rd_anim *a = &rd_anims[o->anim];
      if ((l->st & ST_TOUCHED) && a->on_touch) l->st |= ST_RUN;
      if (l->st & ST_RUN) l->t += 1000 * DT;
      if ((o->flags & F_FALLS) && (l->st & ST_TOUCHED)) {
        if (!(l->st & ST_FALL)) {
          l->ft -= 1000 * DT; /* counts down the delay first (negative), then the fall */
          if (-l->ft >= a->fall_delay) l->st |= ST_FALL, l->ft = 0;
        } else {
          l->ft += 1000 * DT;
          if (l->ft > 4000) l->st |= ST_GONE;
        }
      }
    }
  }
  for (int i = 0; i < r->nlive; i++) {
    const rd_obj *o = &rd_objs[r->live[i].obj];
    if (o->anim != 0xFFFF || r->live[i].parent >= 0 || r->ms < 20) pose(r, i);
    else r->live[i].dx = r->live[i].dy = r->live[i].dr = 0;
  }
  if (r->ended) {
    r->end_ms += 1000 * DT;
    for (int i = 0; i < r->nbits; i++) {
      rd_bit *p = &r->bits[i];
      p->vy += GRAV * 1.5f * DT, p->x += p->vx * DT, p->y += p->vy * DT, p->a += p->w * DT;
    }
    if (r->shake > 0) r->shake -= DT;
    return;
  }
  bool was_ground = b->ground, was_held = b->held;
  b->held = held;
  /* the button: speed on the ground, a backflip in the air */
  float c = cosf(b->a), s = sinf(b->a);
  if (held) {
    b->fixed = false;
    if (b->ground) {
      float fwd = b->vx * c + b->vy * s;
      if (!was_held || b->speed == 0) b->speed = fmaxf(fwd, 0);
      float deg = fmodf(fmodf(b->a / DEG, 360) + 360, 360);
      bool back = b->vx < 0 && (deg < 90 || deg > 270);
      b->speed = fminf(b->speed + (back ? ACCEL_BACK : ACCEL), VMAX);
      b->vx += (c * b->speed - b->vx) * K_SET, b->vy += (s * b->speed - b->vy) * K_SET;
    } else if (b->air_ms > 70 && b->w > -WMAX) {
      b->w += SPIN_DW;
    }
  } else if (was_held && !b->ground) { /* let go in the air */
    b->fixed = true, b->ease_t = 0, b->ease_w0 = b->w;
  }
  if (b->fixed) { /* outCirc to a standstill in 1000 ms, contacts can't turn it meanwhile */
    b->ease_t += 1000 * DT;
    float t = b->ease_t / 1000 - 1;
    b->w = b->ease_t >= 1000 ? 0 : b->ease_w0 - b->ease_w0 * sqrtf(fmaxf(0, 1 - t * t));
    if (b->ease_t >= 1000 || b->ground) b->fixed = false;
  }
  if (!b->ground) b->vy += DOWN_AIR;
  else b->vx *= 1 - DAMP_GROUND, b->vy *= 1 - DAMP_GROUND;
  /* speed pads */
  if (b->boost_ms > 0) {
    b->boost_ms -= 1000 * DT;
    float bc = cosf(b->boost_dir), bs = sinf(b->boost_dir), v = b->vx * bc + b->vy * bs;
    v = fminf(fmaxf(v, 550) + 2000 * DT, 1050);
    b->vx = bc * v + (b->vx - bc * (b->vx * bc + b->vy * bs)), b->vy = bs * v + (b->vy - bs * (b->vx * bc + b->vy * bs));
  }
  /* move, a few small steps so that nothing is passed through */
  float sp = sqrtf(b->vx * b->vx + b->vy * b->vy) + fabsf(b->w) * 45;
  int n = SUBSTEPS;
  while (n < 16 && sp * DT / n > 3) n++;
  float h = DT / n;
  hull_impulse = 0, hit_enemy = false, touched_boost = -1;
  bool down[2] = {false, false};
  for (int k = 0; k < n; k++) {
    b->vy += GRAV * h;
    b->x += b->vx * h, b->y += b->vy * h, b->a += b->w * h;
    find_contacts(r, r->bike);
    down[0] |= b->wheel_down[0], down[1] |= b->wheel_down[1];
    solve(r);
    /* push out what still overlaps, turning the bike too (Box2D's position step: a fifth a step, at most 0.2 m) */
    for (int j = 0; j < ncs; j++) {
      contact *ct = &cs[j];
      if (ct->depth <= 0.15f) continue;
      float corr = fminf((ct->depth - 0.15f) * 0.5f, 6), rn = ct->rx * ct->ny - ct->ry * ct->nx;
      float k = 1 / MASS + (b->fixed ? 0 : rn * rn / INERTIA), p = corr / k / (ncs > 2 ? 2 : 1);
      b->x += ct->nx * p / MASS, b->y += ct->ny * p / MASS;
      if (!b->fixed) b->a += rn * p / INERTIA;
    }
  }
  b->wheel_down[0] = down[0], b->wheel_down[1] = down[1];
  b->ground = down[0] || down[1];
  /* flips: degrees turned since the last one, the first counts at 270 */
  b->rot_abs = b->a / DEG;
  if (!b->ground) {
    b->air_ms += 1000 * DT;
    float d = b->rot_abs - b->flip_start;
    if (fabsf(d) >= (b->air_flips ? 360 : 270)) {
      b->air_flips++, r->flips++;
      if (d > 0) b->front_flip = true;
      /* the next one from the last whole turn passed, as the game's floored modulo does for a backflip */
      b->flip_start = d < 0 ? floorf(b->rot_abs / 360) * 360 : ceilf(b->rot_abs / 360) * 360;
      if (r->mode == RUN_ENDLESS) r->pending++;
    }
  }
  if (b->ground) {
    for (int w = 0; w < 2; w++)
      if (down[w] && b->land_t[w] < 0) b->land_t[w] = r->ms;
    bool both = b->land_t[0] >= 0 && b->land_t[1] >= 0;
    float first = b->land_t[0] < 0 ? b->land_t[1] : b->land_t[1] < 0 ? b->land_t[0] : fminf(b->land_t[0], b->land_t[1]);
    if (b->air_ms > 0 && (both || r->ms - first > 150)) { /* down: on both wheels, or on one for a while */
      if (b->air_flips && r->mode == RUN_ENDLESS) r->landed_flips += b->air_flips, r->pending_ms = 500;
      landed(r);
      b->air_ms = 0, b->flip_start = b->rot_abs;
    }
  } else {
    if (was_ground) b->land_t[0] = b->land_t[1] = -1, b->flip_start = b->rot_abs, b->air_flips = 0, b->front_flip = false;
  }
  /* landed flips count 500 ms later (the game's bonusScoreWaitTimerDelay), a crash before loses them */
  if (r->landed_flips && (r->pending_ms -= 1000 * DT) <= 0)
    r->score += r->landed_flips, r->pending -= r->landed_flips, r->landed_flips = 0;
  /* boost pads, gems, the finish */
  if (touched_boost >= 0) b->boost_ms = 500, b->boost_dir = r->live[touched_boost].rot;
  for (int i = 0; i < r->nlive; i++) {
    rd_live *l = &r->live[i];
    const rd_obj *o = &rd_objs[l->obj];
    if (o->kind != K_GEM || (l->st & ST_GONE)) continue;
    float dx = l->cx - b->x, dy = l->cy - b->y, rr = 30 + o->sx * 0.25f;
    if (dx * dx + dy * dy < rr * rr) l->st |= ST_GONE, r->gems++;
  }
  /* crashes */
  if (hit_enemy) crash_why = "enemy", crash(r);
  if (!r->ended && hull_impulse > 0) {
    if (sensor_on_track(r, sens_top, 4)) crash_why = "top", crash(r);
    else if (hull_impulse > CRASH_FB && (sensor_on_track(r, sens_front, 5) || sensor_on_track(r, sens_back, 5))) crash_why = "front/back", crash(r);
    else if (hull_impulse > CRASH_BOTTOM && sensor_on_track(r, sens_bottom, 4)) crash_why = "bottom", crash(r);
  }
  if (!r->ended && b->y > r->bottom) crash_why = "fell", crash(r);
#ifdef HOST
  if (r->ended == END_CRASH && r->end_ms == 0 && crash_why && getenv("CRASH")) printf("crash: %s impulse %.0f a %.1f\n", crash_why, hull_impulse, b->a / DEG);
#endif
  if (!r->ended && b->x >= r->finish_x) r->ended = END_FINISH, r->end_ms = 0;
  if (r->ended == END_CRASH) r->shake = 0.3f;
  /* the trail */
  if (b->trail_n < TRAIL) b->trail_n++;
  memmove(b->trail_x + 1, b->trail_x, sizeof(float) * (TRAIL - 1));
  memmove(b->trail_y + 1, b->trail_y, sizeof(float) * (TRAIL - 1));
  to_world(b, -34, WHEEL_Y, &b->trail_x[0], &b->trail_y[0]);
  /* the endless run goes on */
  if (r->mode == RUN_ENDLESS)
    while (r->end_x < b->x + 2400) endless_more(r);
  /* the camera: the bike a third in, a little below the middle */
  float tx = b->x + b->vx * 0.15f - 210, ty = b->y - 250;
  r->camx += (tx - r->camx) * 0.15f;
  r->camy += (ty - r->camy) * 0.12f;
}

#ifdef HOST
void run_debug(rd_run *r) {
  find_contacts(r, r->bike);
  for (int k = 0; k < ncs; k++)
    printf("  contact obj %d hull %d p %.1f %.1f n %.2f %.2f depth %.2f\n", rd_objs[r->live[cs[k].obj].obj].tpl, cs[k].hull, cs[k].px, cs[k].py, cs[k].nx, cs[k].ny, cs[k].depth);
}
#endif
float run_level_time(const rd_run *r) {
  float t = r->ms - r->flips * 500.0f;
  return t < 0 ? 0 : t;
}
