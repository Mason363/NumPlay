/* Moto X3M: a level in play (see level.h). */
#include <math.h>
#include <string.h>
#include "bike.h"
#include "fx.h"
#include "level.h"

Level lv;

static uint8_t arena[ARENA_SIZE] __attribute__((aligned(8)));
int arena_used, arena_peak;

void *arena_alloc(int size) {
  size = (size + (int)sizeof(void *) - 1) & ~((int)sizeof(void *) - 1);
  if (arena_used + size > ARENA_SIZE) return 0;
  void *p = arena + arena_used;
  arena_used += size;
  if (arena_used > arena_peak) arena_peak = arena_used;
  memset(p, 0, (size_t)size);
  return p;
}

#define MAX_MATS 40
static Material mats[MAX_MATS];
/* Filters (InteractionFilter(collisionGroup, collisionMask, sensorGroup, sensorMask)), as tools/levels.py */
static const Filter filters[] = {
  {1, -1, 1, -1}, {0, 0, 0, 0}, {0, 0, 1, 4369}, {1, 4369, 1, -1},
  {4096, 272, 17, 4369}, {4096, 272, 1, -1}, {17, 4369, 1, 4369}, {256, 4096, 1, 4369},
};
static SBody *sbodies;

float level_param(const Obj *o, int k) { return lv.params[o->r->params + k]; }

static Con *new_con(int type, Body *b1, Body *b2) {
  Con *c = arena_alloc(sizeof(Con));
  if (!c) return 0;
  c->type = (uint8_t)type;
  c->b1 = b1, c->b2 = b2;
  c->flags = CF_ACTIVE | CF_STIFF;
  c->frequency = 10, c->damping = 1, c->ratio = 1;
  c->sb = -1;
  np_register_con(c);
  return c;
}

extern Body *np_world(void);

static void body_reset(Body *b, const DBodyRec *r) {
  b->pos = v2(r->x, r->y);
  np_body_set_rot(b, r->rot);
  b->vel = v2(0, 0);
  b->angvel = 0, b->torque = 0;
}

/* the level's bodies, objects and constraints into the space */
bool level_load(int n) {
  const LevelHdr *h = level(n);
  memset(&lv, 0, sizeof lv);
  lv.h = h, lv.n = n;
  arena_used = 0;
  np_reset();
  sp.gravity = v2(0, h->gravity);
  /* materials: the level's, then room for the bike's */
  memcpy(mats, lvl_at(h, L_MATS), sizeof(Material) * h->nmats);
  sp.mats = mats;
  sp.filters = filters;
  sp.sshapes = lvl_at(h, L_SSHAPES);
  sp.nsshapes = h->nsshapes;
  sp.sverts = lvl_at(h, L_SVERTS);
  sp.buckets = lvl_at(h, L_BUCKETS);
  sp.bucket_list = lvl_at(h, L_BLIST);
  sp.nbuckets = h->nbuckets;
  sp.bucket_x0 = (int)h->bucket_x0;
  sbodies = arena_alloc(sizeof(SBody) * (h->nsbodies + 1));
  const SBodyRec *sr = lvl_at(h, L_SBODIES);
  for (int i = 0; i < h->nsbodies; i++) {
    sbodies[i].cb = sr[i].cb, sbodies[i].flags = BF_SPACE, sbodies[i].pad = sr[i].pad;
    sbodies[i].viscosity = sr[i].viscosity, sbodies[i].density = sr[i].density;
  }
  sp.sbodies = sbodies, sp.nsbodies = h->nsbodies;
  /* pointer tables: the level's bodies and shapes, then the bike's */
  int njoints = 0;
  const ObjRec *orr0 = lvl_at(h, L_OBJS);
  for (int i = 0; i < h->nobj; i++) njoints += orr0[i].type == O_PIVOT ? 1 : orr0[i].type == O_MOTOR ? 2 : 0;
  int maxb = h->ndbodies + 16, maxs = h->ndshapes + 16, maxc = njoints + 40;   /* (the bike: 15 bodies, 39 joints) */
  sp.bodies = arena_alloc(sizeof(Body *) * maxb);
  sp.shapes = arena_alloc(sizeof(Shape *) * maxs);
  sp.cons = arena_alloc(sizeof(Con *) * maxc);
  if (!sp.bodies || !sp.shapes || !sp.cons || !sbodies) return false;
  /* dynamic and kinematic bodies */
  lv.ndbodies = h->ndbodies;
  lv.dbodies = arena_alloc(sizeof(Body) * (h->ndbodies ? h->ndbodies : 1));
  const DBodyRec *dr = lvl_at(h, L_DBODIES);
  const DShapeRec *ds = lvl_at(h, L_DSHAPES);
  const float *dd = lvl_at(h, L_DDATA);
  if (!lv.dbodies) return false;
  for (int i = 0; i < h->ndbodies; i++) {
    Body *b = &lv.dbodies[i];
    const DBodyRec *r = &dr[i];
    b->type = r->type == 2 ? B_KINEMATIC : B_DYNAMIC;
    b->cb = r->cb;
    b->flags = r->flags & BF_NOROT;
    b->pad = (r->flags & 0x10) != 0;   /* ground */
    body_reset(b, r);
    b->nshapes = r->nshapes;
    b->shapes = arena_alloc(sizeof(Shape) * r->nshapes);
    if (!b->shapes) return false;
    for (int k = 0; k < r->nshapes; k++) {
      const DShapeRec *q = &ds[r->first_shape + k];
      Shape *s = &b->shapes[k];
      s->type = q->type ? SH_POLY : SH_CIRCLE;
      s->n = q->n, s->filter = q->filter, s->flags = q->flags, s->mat = q->mat;
      s->radius = q->radius, s->lcom = v2(q->cx, q->cy), s->area = q->area, s->inertia = q->inertia;
      if (q->type) {
        s->loc = dd + q->off;
        s->glob = arena_alloc((int)sizeof(float) * 5 * q->n);
        if (!s->glob) return false;
      }
    }
    np_register(b);
    b->user = 0;
    np_body_add(b);
  }
  /* objects */
  lv.nobj = h->nobj;
  lv.objs = arena_alloc(sizeof(Obj) * (h->nobj ? h->nobj : 1));
  if (!lv.objs) return false;
  lv.params = lvl_at(h, L_PARAMS);
  lv.aux = lvl_at(h, L_AUX);
  lv.draws = lvl_at(h, L_DRAWS);
  lv.dpts = lvl_at(h, L_DPTS);
  const ObjRec *orr = lvl_at(h, L_OBJS);
  Body *world = np_world();
  for (int i = 0; i < h->nobj; i++) {
    Obj *o = &lv.objs[i];
    const ObjRec *r = &orr[i];
    o->r = r;
    o->body = r->body >= 0 ? &lv.dbodies[r->body] : 0;
    o->body2 = r->body2 >= 0 ? &lv.dbodies[r->body2] : 0;
    o->sbody = r->type == O_PIVOT || r->type == O_MOTOR ? -1 : r->sbody;
    if (o->body && !o->body->user) o->body->user = o;
    if (r->type == O_PIVOT || r->type == O_MOTOR || r->type == O_MOVER || r->type == O_GLASS) {
      o->x = arena_alloc(sizeof(ObjExt));
      if (!o->x) return false;
    }
    if (r->type == O_MOVER) {
      o->x->mv = arena_alloc(sizeof(Mover));
      if (!o->x->mv) return false;
    }
    switch (r->type) {
    case O_PIVOT: {
      /* the pivot holds body (c) to body2 (u) or the world, at the object's point */
      Body *c = o->body, *u = o->body2 ? o->body2 : world;
      V2 p = v2(r->x, r->y);
      Con *k = new_con(C_PIVOT, c, u);
      if (!k) return false;
      k->a1 = np_world_to_local(c, p), k->a2 = np_world_to_local(u, p);
      k->flags |= CF_IGNORE;
      k->sb = (int16_t)(o->body2 ? -1 : r->sbody);
      o->x->con[0] = k;
      break;
    }
    case O_MOTOR: {
      /* pivot(f, s) and motor(f, s): s = body turns, f = body2 or the world */
      Body *s = o->body, *f = o->body2 ? o->body2 : world;
      V2 p = v2(r->x, r->y);
      Con *k = new_con(C_PIVOT, f, s);
      Con *m = new_con(C_MOTOR, f, s);
      if (!k || !m) return false;
      k->a1 = np_world_to_local(f, p), k->a2 = np_world_to_local(s, p);
      k->flags |= CF_IGNORE;
      k->sb = m->sb = (int16_t)(o->body2 ? -1 : r->sbody);
      m->rate = level_param(o, 0);
      m->flags &= (uint8_t)~CF_ACTIVE;
      o->x->con[0] = k, o->x->con[1] = m;
      o->x->gx = r->x, o->x->gy = r->y;
      break;
    }
    default: break;
    }
  }
  return true;
}

/* MaxGameCore.add: an object answers activate() when it has an id */
static bool activatable(const Obj *o) {
  switch (o->r->type) {
  case O_SHAPER: case O_DYN: case O_REMOVED: case O_TNT: case O_MOTOR: case O_MOVER: case O_PIVOT: case O_DECOR:
  case O_GEYSER: case O_WATER:
    return true;
  default:
    return false;
  }
}

static void body_space(Body *b, bool in) {
  if (!b) return;
  if (in) np_body_add(b);
  else np_body_remove(b);
}

static void body_cons(Body *b, bool on, bool pivots_only) {
  for (int i = 0; i < sp.ncons; i++) {
    Con *c = sp.cons[i];
    if ((c->b1 == b || c->b2 == b) && (!pivots_only || c->type == C_PIVOT)) {
      if (on) c->flags |= CF_ACTIVE;
      else c->flags &= (uint8_t)~CF_ACTIVE;
      np_con_clear(c);
    }
  }
}

static bool body_has_cons(const Body *b) {
  for (int i = 0; i < sp.ncons; i++)
    if (sp.cons[i]->b1 == b || sp.cons[i]->b2 == b) return true;
  return false;
}

static void stop_reset(Obj *o) {
  if (!o->body) return;
  const DBodyRec *r = (const DBodyRec *)lvl_at(lv.h, L_DBODIES) + o->r->body;
  body_reset(o->body, r);
  if (o->body->flags & BF_SPACE) np_body_moved(o->body);
}

/* ---- movers ---- */

static V2 mv_point(const Obj *o, int i) { return v2(lv.aux[2 * (o->r->aux + i)], lv.aux[2 * (o->r->aux + i) + 1]); }

static V2 mv_next(const Obj *o) {
  int n = o->r->naux;
  if (o->x->mv->pdir > 0) return o->x->mv->pid >= n - 1 ? mv_point(o, 0) : mv_point(o, o->x->mv->pid + 1);
  return o->x->mv->pid <= 0 ? mv_point(o, n - 1) : mv_point(o, o->x->mv->pid - 1);
}

static void mv_vel(Obj *o) {
  Body *b = o->body;
  V2 e = mv_next(o);
  float dx = e.x - b->pos.x, dy = e.y - b->pos.y, a = atan2f(dy, dx);
  o->x->mv->vx = cosf(a) * o->x->mv->speed, o->x->mv->vy = sinf(a) * o->x->mv->speed;
  o->x->mv->ttp_total = o->x->mv->ttp = sqrtf(dx * dx + dy * dy) / o->x->mv->speed;
}

static void mv_accel_vel(Obj *o) {
  Body *b = o->body;
  int n = o->r->naux;
  float e = 0;
  if (o->x->mv->pdir > 0) {
    for (int i = 1; i <= o->x->mv->pid; i++) {
      V2 p = mv_point(o, i - 1), q = mv_point(o, i);
      e += hypotf(q.x - p.x, q.y - p.y);
    }
    V2 p = mv_point(o, o->x->mv->pid);
    e += hypotf(b->pos.x - p.x, b->pos.y - p.y);
  } else {
    for (int i = n - 2; i >= o->x->mv->pid; i--) {
      V2 p = mv_point(o, i + 1), q = mv_point(o, i);
      e += hypotf(q.x - p.x, q.y - p.y);
    }
    V2 p = mv_point(o, o->x->mv->pid);
    e += hypotf(b->pos.x - p.x, b->pos.y - p.y);
    e = o->x->mv->total - e;
  }
  float base = level_param(o, 0), ad = level_param(o, 1);
  if (e < ad) o->x->mv->speed = o->x->mv->min_speed + e / ad * (base - o->x->mv->min_speed);
  else if (e > o->x->mv->total - ad) o->x->mv->speed = o->x->mv->min_speed + (o->x->mv->total - e) / ad * (base - o->x->mv->min_speed);
  else o->x->mv->speed = base;
  V2 nx = mv_next(o);
  float dx = nx.x - b->pos.x, dy = nx.y - b->pos.y, a = atan2f(dy, dx);
  o->x->mv->vx = cosf(a) * o->x->mv->speed, o->x->mv->vy = sinf(a) * o->x->mv->speed;
  o->x->mv->ttp_total = o->x->mv->ttp = sqrtf(dx * dx + dy * dy) / o->x->mv->speed;
}

static void mv_check_dir(Obj *o) {
  if (o->x->mv->dir * o->x->mv->prev_dir < 0) {
    o->x->mv->pdir = (int8_t)-o->x->mv->pdir;
    o->x->mv->vx = -o->x->mv->vx, o->x->mv->vy = -o->x->mv->vy;
    o->x->mv->ttp = o->x->mv->ttp_total - o->x->mv->ttp;
  }
  o->x->mv->prev_dir = o->x->mv->dir;
}

static void mv_restart(Obj *o) {
  Body *b = o->body;
  const ObjRec *r = o->r;
  int fl = r->flags;
  o->x->mv->start_time = level_param(o, 2);
  o->x->mv->active = (fl & 2) != 0;
  o->x->mv->ang_speed = 0;
  o->x->mv->dir = o->x->mv->active ? 1 : 0;
  o->x->mv->prev_dir = 1;
  o->x->mv->pid = 0, o->x->mv->pdir = 1;
  o->x->mv->speed = level_param(o, 0);
  b->type = B_KINEMATIC;
  b->flags |= BF_NOROT;
  np_body_mass(b);
  np_body_set_rot(b, o->x->mv->base_rot);
  if (!(b->flags & BF_SPACE)) np_body_add(b);
  float t = 0, pos = level_param(o, 5);
  bool found = false;
  for (int i = 1; i < r->naux; i++) {
    V2 p = mv_point(o, i - 1), q = mv_point(o, i);
    float len = hypotf(q.x - p.x, q.y - p.y);
    t += len;
    if (!found && t >= pos) {
      o->x->mv->pid = (int16_t)(i - 1);
      float k = 1 - (t - pos) / len;
      b->pos = v2(p.x + k * (q.x - p.x), p.y + k * (q.y - p.y));
      mv_vel(o);
      found = true;
    }
  }
  b->vel = v2(0, 0), b->angvel = 0;
  np_body_moved(b);
}

static void mv_init(Obj *o) {
  const ObjRec *r = o->r;
  o->x->mv->base_rot = o->body->rot;
  float base = level_param(o, 0), ad = level_param(o, 1);
  if (ad > 0) {
    o->x->mv->min_speed = 0.05f * base;
    o->x->mv->accel = (base * base - o->x->mv->min_speed * o->x->mv->min_speed) / (2 * ad);
  }
  float t = 0;
  for (int i = 1; i < r->naux; i++) {
    V2 p = mv_point(o, i - 1), q = mv_point(o, i);
    t += hypotf(q.x - p.x, q.y - p.y);
  }
  o->x->mv->total = t;
  mv_restart(o);
}

static void mv_switch_on(Obj *o, int t) {
  int fl = o->r->flags;
  if (o->x->mv->active && (fl & 12)) return;
  o->x->mv->dir = (int8_t)(o->x->mv->dir + t);
  o->x->mv->active = o->x->mv->dir != 0;
  if (o->x->mv->active) mv_check_dir(o);
  else o->body->vel = v2(0, 0);
}

static void mv_switch_off(Obj *o, int t) {
  int fl = o->r->flags;
  if (fl & 12) {
    if (o->x->mv->dir != 0) return;
    o->x->mv->dir = (int8_t)(o->x->mv->dir + t);
  } else o->x->mv->dir = (int8_t)(o->x->mv->dir - t);
  o->x->mv->active = o->x->mv->dir != 0;
  if (o->x->mv->active) mv_check_dir(o);
  else o->body->vel = v2(0, 0);
}

static void mv_update(Obj *o, float dt) {
  Body *b = o->body;
  const ObjRec *r = o->r;
  int n = r->naux, fl = r->flags;
  float side_wait = level_param(o, 3), side_angle = level_param(o, 4), ad = level_param(o, 1);
  if (!o->x->mv->active) {
    b->vel = v2(0, 0);
    return;
  }
  if (o->x->mv->start_time > 0) {
    b->vel = v2(0, 0);
    o->x->mv->start_time -= dt;
    if (o->x->mv->start_time <= 0) {
      if (side_angle != 0 && o->x->mv->ang_speed != 0) {
        o->x->mv->ang_speed = 0;
        np_body_set_rot(b, o->x->mv->side_target);
      }
    } else if (side_angle != 0 && o->x->mv->ang_speed != 0) np_body_set_rot(b, b->rot + o->x->mv->ang_speed * dt);
    return;
  }
  if (o->x->mv->ttp > 0) {
    if (ad > 0) mv_accel_vel(o);
  } else {
    o->x->mv->pid = (int16_t)(o->x->mv->pid + o->x->mv->pdir);
    if (o->x->mv->pid == 0 || o->x->mv->pid == n - 1) {
      if (side_wait > 0) {
        o->x->mv->start_time = side_wait;
        if (side_angle != 0) {
          if (o->x->mv->pid == 0) o->x->mv->side_target = o->x->mv->base_rot, o->x->mv->ang_speed = -side_angle / side_wait;
          else o->x->mv->side_target = o->x->mv->base_rot + side_angle, o->x->mv->ang_speed = side_angle / side_wait;
        }
      }
      if (!(fl & 1)) o->x->mv->pdir = (int8_t)-o->x->mv->pdir;
      if (fl & 4) {
        o->x->mv->active = 0, o->x->mv->dir = 0;
        b->pos = mv_point(o, o->x->mv->pid);
      }
    } else if (o->x->mv->pid < 0 || o->x->mv->pid > n - 1) o->x->mv->pid = (int16_t)(o->x->mv->pdir > 0 ? 0 : n - 1);
    if (ad > 0) mv_accel_vel(o);
    else mv_vel(o);
    if (fl & 8) o->x->mv->active = 0;
  }
  b->vel = o->x->mv->active ? v2(o->x->mv->vx, o->x->mv->vy) : v2(0, 0);
  o->x->mv->ttp -= dt;
}

/* ---- activation ---- */

static void tnt_explode(Obj *o) {
  const ObjRec *r = o->r;
  if (o->body) np_body_remove(o->body);
  float imp = level_param(o, 0), count = level_param(o, 1), radius = level_param(o, 2);
  if (imp > 0 && count > 0) {
    float ang = 6.2831853f / count;
    for (int k = 0; k < (int)count; k++) {
      float a = k * ang;
      V2 d = v2(cosf(a), sinf(a));
      Body *b;
      int sb;
      float dist;
      if (np_raycast(v2(r->x, r->y), d, radius, &b, &sb, &dist) && b && b->type == B_DYNAMIC) {
        float m = imp * (radius - dist) / radius;
        np_apply_impulse(b, v2(m * d.x, m * d.y), v2(r->x + d.x * dist, r->y + d.y * dist));
      }
    }
  }
  o->active = 1;
  if (fx_in_camera(r->x, r->y, 256)) {
    fx_explosion(r->x, r->y, o);
    fx_shake();
  } else o->hidden = 1;
}

static void obj_activate(Obj *o, bool t) {
  const ObjRec *r = o->r;
  switch (r->type) {
  case O_SHAPER: {
    bool remove = !(r->flags & 1);
    if (remove) {
      o->hidden = 1;
      if (o->body) {
        body_cons(o->body, false, true);
        np_body_remove(o->body);
      }
      if (o->sbody >= 0) {
        sbodies[o->sbody].flags &= (uint8_t)~BF_SPACE;
        np_drop_static(o->sbody);
      }
    } else {
      if (o->body && !body_has_cons(o->body)) body_space(o->body, t);
      if (o->sbody >= 0) {
        if (t) sbodies[o->sbody].flags |= BF_SPACE;
        else {
          sbodies[o->sbody].flags &= (uint8_t)~BF_SPACE;
          np_drop_static(o->sbody);
        }
      }
      o->hidden = !t;
    }
    break;
  }
  case O_REMOVED:
    if (o->active) break;
    /* fall through */
  case O_DYN:
    if (t) {
      np_body_add(o->body);
      body_cons(o->body, true, false);
    } else {
      for (int i = 0; i < sp.ncons; i++) {
        Con *c = sp.cons[i];
        if (c->b1 == o->body || c->b2 == o->body) c->flags = (uint8_t)((c->flags & ~CF_ACTIVE) | 0x80);
      }
      np_body_remove(o->body);
    }
    break;
  case O_TNT:
    if (!o->active && t) tnt_explode(o);
    break;
  case O_MOTOR:
    if (!o->active && o->x->con[1]) {
      o->body->flags &= (uint8_t)~BF_NOROT;
      np_body_mass(o->body);
      np_wake(o->body);
      if (t) o->x->con[1]->flags |= CF_ACTIVE;
      else o->x->con[1]->flags &= (uint8_t)~CF_ACTIVE;
      o->t1 = level_param(o, 1);
      o->active = 1;
    }
    break;
  case O_MOVER:
    if (t) mv_switch_on(o, 1);
    else mv_switch_off(o, 1);
    break;
  case O_PIVOT:
    if (!o->active && o->x->con[0]) {
      o->x->con[0]->flags &= (uint8_t)~CF_ACTIVE;
      if (o->body) np_wake(o->body);
      if (o->body2) np_wake(o->body2);
      o->active = 1;
    }
    break;
  case O_DECOR:
    o->hidden = t;
    break;
  case O_GEYSER:
  case O_WATER:
    o->active = t;
    break;
  default: break;
  }
}

void level_activate(int id, bool on) {
  for (int i = 0; i < lv.nobj; i++) {
    Obj *o = &lv.objs[i];
    if (o->r->id == id && activatable(o)) obj_activate(o, on);
  }
}

/* ---- restart ---- */

static void obj_restart(Obj *o) {
  const ObjRec *r = o->r;
  switch (r->type) {
  case O_SHAPER:
    if (!(r->flags & 1)) {
      o->hidden = 0;
      if (o->body) {
        np_body_add(o->body);
        body_cons(o->body, true, true);
      }
      if (o->sbody >= 0) sbodies[o->sbody].flags |= BF_SPACE;
    } else if (r->id >= 0) obj_activate(o, false);
    if (o->body && o->body->type != B_STATIC) stop_reset(o);
    break;
  case O_PLANK:
    if (o->body) stop_reset(o);
    break;
  case O_REMOVED:
    o->t2 = 1, o->active = 0, o->alpha = 1, o->hidden = 0;
    np_body_add(o->body);
    /* fall through */
  case O_DYN:
    stop_reset(o);
    if (r->flags & 1) obj_activate(o, false);
    break;
  case O_TNT:
    if (o->body) {
      stop_reset(o);
      np_body_add(o->body);
    }
    o->active = 0, o->hidden = 0;
    break;
  case O_TOGGLE:
    o->active = 0, o->sens = 0;
    sbodies[o->sbody].flags |= BF_SPACE;
    break;
  case O_SAFE:
    o->active = 0, o->t1 = 0;
    break;
  case O_MOTOR:
    if (o->x->con[0]) {
      np_con_clear(o->x->con[0]), np_con_clear(o->x->con[1]);
      o->x->con[1]->flags &= (uint8_t)~CF_ACTIVE;
      o->body->flags |= BF_NOROT;
      o->body->angvel = 0;
      np_body_mass(o->body);
    }
    o->active = 0, o->t2 = 0;
    break;
  case O_MOVER:
    mv_restart(o);
    break;
  case O_PIVOT:
    if (o->x->con[0]) {
      np_con_clear(o->x->con[0]);
      if (!(o->x->con[0]->flags & 0x80)) o->x->con[0]->flags |= CF_ACTIVE;
    }
    o->active = 0;
    break;
  case O_GEYSER:
    if (r->id >= 0) o->active = 0;
    else o->active = 1;
    break;
  case O_BOOST:
    o->active = 0, o->hidden = 0;
    sbodies[o->sbody].flags |= BF_SPACE;
    break;
  case O_GLASS:
    o->t1 = 0, o->active = 0, o->hidden = 0, o->alpha = 1;
    sbodies[o->sbody].flags |= BF_SPACE;
    break;
  default: break;
  }
}

static bool obj_reviewed(const Obj *o) {
  switch (o->r->type) {
  case O_SHAPER: case O_PLANK: case O_DYN: case O_REMOVED: case O_TNT: case O_TOGGLE: case O_MOTOR: case O_MOVER:
  case O_PIVOT: case O_GEYSER: case O_BOOST: case O_GLASS:
    return true;
  default:
    return false;
  }
}

void level_restart(void) {
  for (int i = 0; i < lv.nobj; i++) {
    Obj *o = &lv.objs[i];
    if (o->r->type == O_MOVER && !o->x->mv->total) mv_init(o);
    obj_restart(o);
  }
  fx_clear();
}

void level_review(void) {
  for (int i = 0; i < lv.nobj; i++) {
    Obj *o = &lv.objs[i];
    if (!obj_reviewed(o)) continue;
    if (o->r->type == O_SHAPER && (o->r->flags & 2) && o->body && o->body->type == B_KINEMATIC) continue;
    obj_restart(o);
  }
}

/* the first time: MaxGameCore.initialDeactivate (objects not active at the start) */
void level_start(void);
void level_start(void) {
  for (int i = 0; i < lv.nobj; i++) {
    Obj *o = &lv.objs[i];
    if (o->r->type == O_MOVER) mv_init(o);
    if (o->r->type == O_REMOVED) o->t2 = 1, o->alpha = 1;
    if ((o->r->type == O_DYN || o->r->type == O_REMOVED) && (o->r->flags & 1)) obj_activate(o, false);
    if (o->r->type == O_GEYSER) o->active = o->r->id < 0;
  }
}

/* ---- each game tick ---- */

void level_update(float dt) {
  for (int i = 0; i < lv.nobj; i++) {
    Obj *o = &lv.objs[i];
    switch (o->r->type) {
    case O_REMOVED:
      if (o->active) {
        if (o->t1 > 0) {
          o->t1 -= dt;
          if (o->t1 <= 0) {
            np_body_remove(o->body);
            o->hidden = 1;
          } else if (o->t1 < 0.5f) o->alpha = o->t1 / 0.5f;
        }
      } else if (o->t2 > 0) o->t2 -= dt;
      else if (o->body->flags & BF_SPACE) {
        float v = hypotf(o->body->vel.x, o->body->vel.y);
        if (v > 100) o->active = 1, o->t1 = 1;
      }
      break;
    case O_MOTOR:
      if (o->x->con[1] && o->active && o->t1 > 0) {
        o->x->grot += dt * o->x->con[1]->rate;
        o->t1 -= dt;
        if (o->t1 <= 0) {
          o->x->con[1]->flags &= (uint8_t)~CF_ACTIVE;
          o->body->angvel = 0;
          o->body->flags |= BF_NOROT;
          np_body_mass(o->body);
        }
      }
      if (o->x->con[0] && o->body2) {
        V2 p = np_local_to_world(o->body, o->x->con[0]->a2);
        o->x->gx = p.x, o->x->gy = p.y;
      }
      break;
    case O_MOVER:
      mv_update(o, dt);
      break;
    case O_PIVOT:
      if (o->x->con[0] && o->body2) {
        V2 p = np_local_to_world(o->body2, o->x->con[0]->a2);
        o->x->gx = p.x, o->x->gy = p.y;
      } else o->x->gx = o->r->x, o->x->gy = o->r->y;
      break;
    case O_SAFE:
      if (o->active && o->t1 < 1) o->t1 += dt;
      break;
    case O_GLASS:
      if (o->t1 > 0) {
        o->t1 -= dt;
        if (o->t1 <= 0) {
          sbodies[o->sbody].flags &= (uint8_t)~BF_SPACE;
          np_drop_static(o->sbody);
          o->hidden = 1;
          fx_glass(o);
        }
      }
      break;
    default: break;
    }
  }
}

/* ---- the space's events (MaxPhysics listeners) ---- */

static Obj *obj_of_static(int sb, int type) {
  for (int i = 0; i < lv.nobj; i++)
    if (lv.objs[i].sbody == sb && (type < 0 || lv.objs[i].r->type == type)) return &lv.objs[i];
  return 0;
}

static void toggle_sens(Obj *o, bool begin, bool dead) {
  const ObjRec *r = o->r;
  bool off = r->flags & 1;
  if (!begin) {
    if (--o->sens <= 0) level_activate(r->id, false);
    return;
  }
  if (!dead) {
    if (r->id >= 0) {
      level_activate(r->id, true);
      if (!off) {
        sbodies[o->sbody].flags &= (uint8_t)~BF_SPACE;
        np_drop_static(o->sbody);
      }
      o->sens++;
    }
    if (r->id2 >= 0) {
      level_activate(r->id2, false);
      if (!off) {
        sbodies[o->sbody].flags &= (uint8_t)~BF_SPACE;
        np_drop_static(o->sbody);
      }
    }
  }
  o->active = 1;
}

static void handle(const Event *e, Body *b, int sb, Body *ob, int osb) {
  /* b: the bike's (or a level object's) body; ob/osb: the other side */
  uint16_t cb = b ? b->cb : 0;
  uint16_t ocb = ob ? ob->cb : osb >= 0 ? sbodies[osb].cb : 0;
  (void)sb;
  bool car = cb & (CB_WHEEL | CB_BODY), driver = cb & CB_DRIVER, part = cb & CB_PART;
  if (e->type == A_SENSOR) {
    if (!e->begin) {
      if (car && (ocb & CB_TOGGLE_OFF) && osb >= 0) {
        Obj *o = obj_of_static(osb, O_TOGGLE);
        if (o) toggle_sens(o, false, false);
      }
      return;
    }
    if (car && (ocb & CB_TOGGLE) && osb >= 0) {
      Obj *o = obj_of_static(osb, O_TOGGLE);
      if (o && (sbodies[osb].flags & BF_SPACE)) toggle_sens(o, true, bike.dead);
    }
    if ((car || part) && (ocb & CB_BOOST) && osb >= 0) {
      Obj *o = obj_of_static(osb, O_BOOST);
      if (o && !o->active) {
        o->active = 1, o->hidden = 1;
        sbodies[osb].flags &= (uint8_t)~BF_SPACE;
        np_drop_static(osb);
        bike_add_boost(level_param(o, 0), level_param(o, 1));
      }
    }
    if ((driver || part) && (ocb & CB_FINISH)) bike_finish();
    if ((driver || part) && (ocb & CB_SAFE) && osb >= 0 && !bike.dead) {
      Obj *o = obj_of_static(osb, O_SAFE);
      if (o) {
        bike.review_x = o->r->x, bike.review_y = o->r->y, bike.safe_id = o->r->id;
        if (!o->active) o->active = 1, o->t1 = 0;
      }
    }
    return;
  }
  if (e->type == A_FLUID) {
    if (e->begin && (driver || part) && (ocb & CB_WATER)) {
      bike_driver_ground();
      bike_explode();
    }
    return;
  }
  if (!e->begin) return;
  if ((car || driver || part) && (ocb & CB_GLASS) && osb >= 0) {
    Obj *o = obj_of_static(osb, O_GLASS);
    if (o && !o->active) {
      o->active = 1;
      o->x->gx = e->px, o->x->gy = e->py;
      o->t1 = level_param(o, 0);
      if (o->t1 <= 0) o->t1 = 0.0001f;
    }
  }
  if (driver && (ocb & CB_GROUND)) {
    bike_driver_ground();
    bike_explode();
  }
  if (car && (ocb & (CB_SPIKES | CB_SAW))) {
    bike_driver_ground();
    bike_explode();
  }
  if ((driver || part) && (ocb & CB_SPIKES)) {
    bike_driver_ground();
    bike_spikes(b);
  }
  if ((driver || part) && (ocb & CB_SAW)) {
    bike_spikes(0);
    bike_driver_ground();
    bike_explode();
  }
}

void level_events(void) {
  for (int i = 0; i < sp.nevents; i++) {
    const Event *e = &sp.events[i];
    if (e->b1 && bike_owns(e->b1)) handle(e, e->b1, e->sb1, e->b2, e->sb2);
    if (e->b2 && bike_owns(e->b2)) handle(e, e->b2, e->sb2, e->b1, e->sb1);
  }
  sp.nevents = 0;
  /* geysers push while touching (ONGOING) */
  for (int i = 0; i < NP_MAX_ARBS; i++) {
    const Arb *a = &sp.arbs[i];
    if (!a->used || a->type != A_SENSOR || a->up_stamp != sp.stamp) continue;
    int sb1 = a->s1 & 0x8000 ? -1 : sp.sshapes[a->s1].body, sb2 = a->s2 & 0x8000 ? -1 : sp.sshapes[a->s2].body;
    int gs = -1;
    Body *b = 0;
    if (sb2 >= 0 && (sbodies[sb2].cb & CB_GEYSER) && sb1 < 0) gs = sb2, b = a->b1;
    else if (sb1 >= 0 && (sbodies[sb1].cb & CB_GEYSER) && sb2 < 0) gs = sb1, b = a->b2;
    if (gs < 0 || !b) continue;
    int slot = bike_body_slot(b);
    if (slot < 0) continue;
    Obj *o = obj_of_static(gs, O_GEYSER);
    if (!o) continue;
    bike.eff_vx[slot] = level_param(o, 0), bike.eff_vy[slot] = level_param(o, 1);
  }
}

/* ---- camera paths (CameraPathes.getPathPointXY) ---- */

float level_cam_y(float x, float y, int *path) {
  const uint8_t *p = lvl_at(lv.h, L_CAMS);
  float best = NAN;
  int bi = -1;
  for (int k = 0; k < lv.h->ncams; k++) {
    const CamRec *c = (const CamRec *)(const void *)(p + 8 * k);
    const int16_t *q = lv.dpts + 2 * c->pts;
    float minx = q[0], maxx = q[2 * (c->n - 1)];
    if (x < minx || x > maxx) continue;
    for (int i = 1; i < c->n; i++) {
      float ax = q[2 * i], ay = q[2 * i + 1] + c->dy;
      if (ax >= x) {
        float rx = q[2 * i - 2], ry = q[2 * i - 1] + c->dy;
        float h = x - rx, ang = atan2f(ay - ry, ax - rx);
        float yy = ry + h * tanf(ang);
        if (!(yy < y) && (isnan(best) || yy < best)) best = yy, bi = k;
        break;
      }
    }
  }
  *path = bi;
  return best;
}
