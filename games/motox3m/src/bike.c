/* Moto X3M: the bike and its rider (see bike.h). Numbers from the game's levels/1.json and levels/bike.json. */
#include <math.h>
#include <string.h>
#include "bike.h"
#include "bike_data.h"
#include "fx.h"
#include "level.h"
#include "plat.h"

Bike bike;

#define TO_RAD 0.017453292519943295f
#define PI 3.14159265358979f
#define LONG_FLY_TIME 0.1f
#define DAMPING_DELTA 0.99f
#define MIN_AC_BR_TIME 1.0f
#define MAX_VEL_TIME 2.0f
#define FRAME_END_DELAY 0.1f
#define WHEEL_END_DELAY 0.25f
#define START_SPIN_FRICTION 0.01f
#define START_SPIN_DELAY 1.0f
#define SMOKE_TIME 0.2f

extern Body *np_world(void);
static int nmats_base;
static float max_ang_vel, accel_torque, delta_torque, tilt_torque, max_tilt_speed, wheel_radius;

/* the shapes' materials go after the level's in sp.mats */
static uint8_t add_mat(float el, float dyn, float stat, float density, float rolling) {
  Material *m = &sp.mats[nmats_base++];
  m->elasticity = el, m->dyn = dyn, m->stat = stat, m->density = density, m->rolling = rolling;
  return (uint8_t)(nmats_base - 1);
}

static void box(float *loc, float w, float h, float ox, float oy) {
  float v[8] = {-w / 2 + ox, -h / 2 + oy, w / 2 + ox, -h / 2 + oy, w / 2 + ox, h / 2 + oy, -w / 2 + ox, h / 2 + oy};
  memcpy(loc, v, sizeof v);
}

/* a polygon's area, centre and inertia per unit mass about the origin (Polygon.__validate_area_inertia) */
static void poly_props(Shape *s) {
  const V2 *v = (const V2 *)(const void *)s->loc;
  int n = s->n;
  float area = 0, s1 = 0, s2 = 0, cx = 0, cy = 0;
  for (int i = 0; i < n; i++) {
    V2 u = v[(i + n - 1) % n], p = v[i], w = v[(i + 1) % n];
    float a = u.x * p.y - u.y * p.x, b = p.x * p.x + p.y * p.y + p.x * u.x + p.y * u.y + u.x * u.x + u.y * u.y;
    s1 += a * b, s2 += a;
    area += p.x * (w.y - u.y);
    float cf = p.x * w.y - p.y * w.x;
    cx += (p.x + w.x) * cf, cy += (p.y + w.y) * cf;
  }
  s->inertia = s1 / (6 * s2);
  s->lcom = v2(cx / (3 * area), cy / (3 * area));
  s->area = area * 0.5f;
}

static void poly_shape(Shape *s, float *loc, float *glob, int n, uint8_t mat, uint8_t filter) {
  s->type = SH_POLY, s->n = (uint8_t)n, s->loc = loc, s->glob = glob, s->mat = mat, s->filter = filter;
  /* Nape keeps polygons with a positive area */
  float a = 0;
  for (int i = 0; i < n; i++) {
    int j = (i + 1) % n, k = (i + n - 1) % n;
    a += loc[2 * i] * (loc[2 * j + 1] - loc[2 * k + 1]);
  }
  if (a < 0)
    for (int i = 0; i < n / 2; i++) {
      float tx = loc[2 * i], ty = loc[2 * i + 1];
      loc[2 * i] = loc[2 * (n - 1 - i)], loc[2 * i + 1] = loc[2 * (n - 1 - i) + 1];
      loc[2 * (n - 1 - i)] = tx, loc[2 * (n - 1 - i) + 1] = ty;
    }
  poly_props(s);
}

/* Body.align: the origin moves to the centre of mass */
static void align(Body *b) {
  np_body_mass(b);
  V2 c = b->lcom;
  for (int i = 0; i < b->nshapes; i++) {
    Shape *s = &b->shapes[i];
    if (s->type == SH_POLY) {
      float *l = (float *)(uintptr_t)s->loc;
      for (int k = 0; k < s->n; k++) l[2 * k] -= c.x, l[2 * k + 1] -= c.y;
      poly_props(s);
    } else s->lcom = v2(s->lcom.x - c.x, s->lcom.y - c.y);
  }
  b->pos = np_local_to_world(b, c);
  np_body_mass(b);
}

static Con *con(Con *c, int type, Body *b1, Body *b2) {
  memset(c, 0, sizeof *c);
  c->type = (uint8_t)type, c->b1 = b1, c->b2 = b2;
  c->flags = CF_ACTIVE | CF_STIFF;
  c->frequency = 10, c->damping = 1, c->ratio = 1;
  c->sb = -1;
  np_register_con(c);
  return c;
}

static void con_on(Con *c, bool on) {
  if (on) c->flags |= CF_ACTIVE;
  else c->flags &= (uint8_t)~CF_ACTIVE;
}

/* DriverPartObject.createJoints / DriverObject.connectArm, connectLeg */
static void joints(PartJoint *j, Body *b, Body *parent, V2 at, float r, float freq, float damp, bool to_frame) {
  j->has = true;
  /* pivot(body, parent) for parts; pivot(frame, part) for the arm and leg */
  Body *b1 = to_frame ? parent : b, *b2 = to_frame ? b : parent;
  Con *p = con(&j->pivot, C_PIVOT, b1, b2);
  p->a1 = np_world_to_local(b1, at), p->a2 = np_world_to_local(b2, at);
  float u = b2->rot - b1->rot;
  j->base = u, j->delta = r;
  if (r > 0) {
    Con *s = con(&j->soft, C_ANGLE, b1, b2);
    s->jmin = s->jmax = u;
    s->flags &= (uint8_t)~CF_STIFF;
    s->frequency = freq, s->damping = damp;
    Con *t = con(&j->stiff, C_ANGLE, b1, b2);
    t->jmin = u - r, t->jmax = u + r;
  }
}

static void joint_connect(PartJoint *j, bool on) {
  if (!j->has) return;
  con_on(&j->pivot, on);
  con_on(&j->soft, on);
  if (on) j->stiff.jmin = j->base - j->delta, j->stiff.jmax = j->base + j->delta;
  con_on(&j->stiff, on);
  np_con_clear(&j->pivot), np_con_clear(&j->soft), np_con_clear(&j->stiff);
}

static void joint_move(PartJoint *j, float t, float e) {
  j->stiff.jmin = j->base - j->delta + t * e, j->stiff.jmax = j->base + j->delta + t * e;
  j->soft.jmin = j->soft.jmax = j->base + t * e;
}

static void joint_relax(PartJoint *j) {
  if (!j->has) return;
  con_on(&j->soft, false);
  j->stiff.jmin = j->base - 5 * j->delta, j->stiff.jmax = j->base + 5 * j->delta;
}

static const PartGeo *part_geo(int p) {
  switch (p >= P_HIP2 ? p - 4 : p) {
  case P_HEAD: return &GEO_DRIVER_HEAD;
  case P_CORPSE: return &GEO_DRIVER_CORPSE;
  case P_HIP: return &GEO_DRIVER_HIP;
  case P_SHIN: return &GEO_DRIVER_SHIN;
  case P_ARM: return &GEO_DRIVER_ARM;
  default: return &GEO_DRIVER_FOREARM;
  }
}

static float start_x, start_y;

static Body *all_bodies[4 + NPARTS];

void bike_build(float x, float y, int skin) {
  Bike *k = &bike;
  memset(k, 0, sizeof *k);
  k->skin = skin;
  nmats_base = lv.h->nmats;
  Body *world = np_world();
  (void)world;
  /* the frame: a box (mass) and the bike's outline (what touches things) */
  Body *f = &k->frame;
  f->type = B_DYNAMIC, f->pos = v2(x, y), f->cb = CB_BODY;
  np_body_set_rot(f, 0);
  f->shapes = k->frame_shapes, f->nshapes = 2;
  box(k->frame_loc[0], CAR_bodyW, CAR_bodyH, 0, 0);
  poly_shape(&k->frame_shapes[0], k->frame_loc[0], k->frame_glob[0], 4, add_mat(0, 0, 0, CAR_bodyDensity, 0.001f),
             FL_NOTHING);
  for (int i = 0; i < 4; i++) k->frame_loc[1][2 * i] = GEO_POINTS[i].x, k->frame_loc[1][2 * i + 1] = GEO_POINTS[i].y;
  poly_shape(&k->frame_shapes[1], k->frame_loc[1], k->frame_glob[1], 4, add_mat(0, 1, 2, 0.001f, 0.001f), FL_WHEEL);
  align(f);
  np_register(f);
  /* the wheels: on a line joint (the fork) and a spring */
  for (int w = 0; w < 2; w++) {
    bool back = w == 0;
    float fr = back ? CAR_wheelBackFriction : CAR_wheelFrontFriction, de = back ? CAR_wheelBackDensity : CAR_wheelFrontDensity;
    float r = back ? CAR_wheelBackR : CAR_wheelFrontR;
    float dx = back ? -CAR_wheelBackDx : CAR_wheelFrontDx;
    float ay = back ? CAR_axisBackY : CAR_axisFrontY, alen = back ? CAR_axisBackLen : CAR_axisFrontLen;
    float dy = back ? CAR_damperBackY : CAR_damperFrontY, dlen = back ? CAR_damperBackLen : CAR_damperFrontLen;
    float freq = back ? CAR_damperBackFreq : CAR_damperFrontFreq, damp = back ? CAR_damperBackDamp : CAR_damperFrontDamp;
    float ang = back ? CAR_axisBackAngle : CAR_axisFrontAngle;
    Body *b = &k->wheel[w];
    b->type = B_DYNAMIC, b->cb = CB_WHEEL;
    b->pos = v2(f->pos.x + dx, f->pos.y + dlen);
    np_body_set_rot(b, 0);
    b->shapes = &k->wheel_shape[w], b->nshapes = 1;
    Shape *s = &k->wheel_shape[w];
    s->type = SH_CIRCLE, s->radius = r, s->area = PI * r * r, s->inertia = r * r * 0.5f, s->filter = FL_WHEEL;
    k->mat_wheel[w] = add_mat(0, fr, fr, de, 0.001f);
    s->mat = k->mat_wheel[w];
    np_register(b);
    Con *l = con(&k->line[w], C_LINE, f, b);
    l->a1 = v2(dx, ay), l->a2 = v2(0, 0), l->nlocal = v2(cosf(ang), sinf(ang)), l->jmin = 0, l->jmax = alen;
    Con *d = con(&k->dist[w], C_DIST, f, b);
    d->a1 = v2(dx, dy), d->a2 = v2(0, 0), d->jmin = dlen, d->jmax = 2 * dlen;
    d->frequency = freq, d->damping = damp;
    d->flags = CF_ACTIVE | CF_IGNORE;
    if (!back) {
      Con *bj = con(&k->brake_joint, C_ANGLE, f, b);
      bj->jmin = bj->jmax = 0;
      bj->flags = 0;          /* inactive, soft */
      bj->damping = 100;
    }
  }
  /* back wheel's braking material: a tenth of its friction */
  {
    Material *m = &sp.mats[k->mat_wheel[0]];
    k->mat_brake = add_mat(m->elasticity, 0.1f * m->stat, 0.1f * m->stat, m->density, m->rolling);
  }
  /* the rider's body: a box welded to the frame, its weight shifting as he leans */
  Body *dv = &k->driver;
  dv->type = B_DYNAMIC;
  np_body_set_rot(dv, 0);
  dv->shapes = &k->driver_shape, dv->nshapes = 1;
  box(k->driver_loc, CAR_driverW, CAR_driverH, 0, 0);
  poly_shape(&k->driver_shape, k->driver_loc, k->driver_glob, 4, add_mat(0, 0, 0, CAR_driverDensity, 0.001f),
             FL_NOTHING);
  np_register(dv);
  Con *wd = con(&k->weld, C_WELD, f, dv);
  wd->a1 = v2(0, CAR_driverDy), wd->a2 = v2(0, 0);
  wd->flags |= CF_IGNORE;
  /* the rider's parts, at the frame's start position */
  uint8_t pmat = add_mat(0, 1, 2, 1, 0.001f);
  float fx = f->pos.x, fy = f->pos.y;
  for (int p = 0; p < NPARTS; p++) {
    const PartGeo *g = part_geo(p);
    Body *b = &k->part[p];
    b->type = B_DYNAMIC, b->cb = p == P_HEAD ? CB_DRIVER : CB_PART;
    b->pos = v2(g->x + fx, g->y + fy);
    np_body_set_rot(b, g->r);
    b->shapes = &k->part_shape[p], b->nshapes = 1;
    box(k->part_loc[p], g->w, g->h, 0, 0);
    poly_shape(&k->part_shape[p], k->part_loc[p], k->part_glob[p], 4, pmat, FL_PART);
    np_register(b);
  }
  float r10 = 10 * TO_RAD;
  V2 o = v2(fx, fy);
#define AT(J) v2(o.x + (J).x, o.y + (J).y)
  joints(&k->pj[P_SHIN], &k->part[P_SHIN], &k->part[P_HIP], AT(GEO_HIP_JOINT), r10, 10, 1, false);
  joints(&k->pj[P_SHIN2], &k->part[P_SHIN2], &k->part[P_HIP2], AT(GEO_HIP_JOINT), r10, 10, 1, false);
  joints(&k->pj[P_HIP], &k->part[P_HIP], &k->part[P_CORPSE], AT(GEO_CORPSE_JOINT), r10, 10, 1, false);
  joints(&k->pj[P_HIP2], &k->part[P_HIP2], &k->part[P_CORPSE], AT(GEO_CORPSE_JOINT), r10, 10, 1, false);
  joints(&k->pj[P_FOREARM], &k->part[P_FOREARM], &k->part[P_ARM], AT(GEO_FOREARM_JOINT), r10, 10, 1, false);
  joints(&k->pj[P_FOREARM2], &k->part[P_FOREARM2], &k->part[P_ARM2], AT(GEO_FOREARM_JOINT), r10, 10, 1, false);
  joints(&k->pj[P_ARM], &k->part[P_ARM], &k->part[P_CORPSE], AT(GEO_ARM_JOINT), r10, 10, 1, false);
  joints(&k->pj[P_ARM2], &k->part[P_ARM2], &k->part[P_CORPSE], AT(GEO_ARM_JOINT), r10, 10, 1, false);
  joints(&k->pj[P_HEAD], &k->part[P_HEAD], &k->part[P_CORPSE], AT(GEO_HEAD_JOINT), r10, 10, 1, false);
  joints(&k->arm, &k->part[P_FOREARM], f, AT(GEO_STEER_JOINT), r10, 50, 50, true);
  joints(&k->leg, &k->part[P_SHIN], f, AT(GEO_SHIN_JOINT), r10, 50, 100, true);
#undef AT
  /* CarObject.setParams */
  wheel_radius = CAR_wheelBackR;
  max_ang_vel = 2 * (CAR_maxSpeed / (2 * PI * wheel_radius)) * PI;
  accel_torque = CAR_torque;
  delta_torque = CAR_maxTorque > CAR_torque ? CAR_maxTorque - CAR_torque : 0;
  tilt_torque = CAR_flipTorque;
  max_tilt_speed = CAR_maxFlipSpeed;
  k->review_x = start_x = f->pos.x, k->review_y = start_y = f->pos.y;
  all_bodies[0] = f, all_bodies[1] = &k->wheel[0], all_bodies[2] = &k->wheel[1], all_bodies[3] = dv;
  for (int p = 0; p < NPARTS; p++) all_bodies[4 + p] = &k->part[p];
  for (int i = 0; i < 4 + NPARTS; i++) all_bodies[i]->user = 0, all_bodies[i]->flags |= BF_KEEP;
  k->safe_id = -1;
}

bool bike_owns(const Body *b) {
  for (int i = 0; i < 4 + NPARTS; i++)
    if (all_bodies[i] == b) return true;
  return false;
}

int bike_body_slot(const Body *b) {
  for (int i = 0; i < 4 + NPARTS; i++)
    if (all_bodies[i] == b) return i == 3 ? -1 : i;   /* (the welded body has no effector) */
  return -1;
}

static void stop(Body *b) {
  b->torque = 0, b->vel = v2(0, 0), b->angvel = 0;
  b->flags &= (uint8_t)~(BF_NOMOVE | BF_NOROT);
  np_body_mass(b);
}

static void mirror_parts(bool on) {
  for (int p = P_HIP2; p < NPARTS; p++) {
    Body *m = &bike.part[p], *s = &bike.part[p - 4];
    if (on) {
      stop(m);
      m->pos = s->pos;
      np_body_set_rot(m, s->rot);
      np_body_add(m);
      np_body_moved(m);
    } else np_body_remove(m);
    joint_connect(&bike.pj[p], on);
  }
}

static void connect_driver(bool on) {
  joint_connect(&bike.arm, on);
  joint_connect(&bike.leg, on);
}

static void driver_move(float t, float e) {
  /* the rider's weight: his box's centre goes driverDx forwards or back */
  float *l = bike.driver_loc;
  box(l, CAR_driverW, CAR_driverH, CAR_driverDx * t, 0);
  poly_props(&bike.driver_shape);
  np_body_mass(&bike.driver);
  joint_move(&bike.pj[P_SHIN], e, 30 * TO_RAD);
  joint_move(&bike.pj[P_HIP], e, -45 * TO_RAD);
  joint_move(&bike.pj[P_ARM], e, -45 * TO_RAD);
  float s = 15 * e * TO_RAD;
  joint_move(&bike.leg, 1, s);
}

static void wheel_brake(int w, bool on) {
  Shape *s = &bike.wheel_shape[w];
  if (w != 0) return;
  if (on && !bike.braked) {
    s->mat = bike.mat_brake;
    np_drop_shape(s);
  } else if (!on && bike.braked) {
    s->mat = bike.mat_wheel[0];
    np_drop_shape(s);
  }
  bike.braked = on;
}

/* CarObject.review: back to the review point, standing */
void bike_review(void) {
  Bike *k = &bike;
  Body *f = &k->frame;
  for (int w = 0; w < 2; w++) {
    con_on(&k->line[w], false), con_on(&k->dist[w], false);
  }
  connect_driver(false);
  /* driver.connect(false): the weld and every part's joints */
  con_on(&k->weld, false);
  for (int p = 0; p < NPARTS; p++) joint_connect(&k->pj[p], false);
  stop(f), stop(&k->wheel[0]), stop(&k->wheel[1]);
  stop(&k->driver);
  for (int p = 0; p < NPARTS; p++) stop(&k->part[p]);
  /* frame.review */
  f->pos = v2(k->review_x, k->review_y);
  np_body_set_rot(f, 0);
  k->start_fly = k->max_fly = k->min_fly = k->rotation = 0;
  float wdy = fmaxf(CAR_axisBackY + CAR_axisBackLen + CAR_wheelBackR, CAR_axisFrontY + CAR_axisFrontLen + CAR_wheelFrontR);
  k->wheel[0].pos = np_local_to_world(f, v2(-CAR_wheelBackDx, wdy));
  k->wheel[1].pos = np_local_to_world(f, v2(CAR_wheelFrontDx, wdy));
  /* driver.review: lean 0, his parts at their places around the frame */
  driver_move(0, 0);
  k->driver.pos = v2(f->pos.x, f->pos.y + CAR_driverDy);
  np_body_set_rot(&k->driver, 0);
  for (int p = 0; p < NPARTS; p++) {
    const PartGeo *g = part_geo(p);
    k->part[p].pos = v2(f->pos.x + g->x, f->pos.y + g->y);
    np_body_set_rot(&k->part[p], g->r);
  }
  k->spiked = false;
  for (int w = 0; w < 2; w++) {
    con_on(&k->line[w], true), con_on(&k->dist[w], true);
    np_con_clear(&k->line[w]), np_con_clear(&k->dist[w]);
  }
  connect_driver(true);
  con_on(&k->weld, true);
  np_con_clear(&k->weld);
  for (int p = 0; p < P_HIP2; p++) joint_connect(&k->pj[p], true);
  mirror_parts(false);
  for (int i = 0; i < 4 + NPARTS; i++) {
    if (i >= 4 + P_HIP2) continue;
    np_body_add(all_bodies[i]);
    np_body_moved(all_bodies[i]);
  }
  k->dead = false, k->drive = true, k->exploded = false;
  con_on(&k->brake_joint, false);
  k->boost_left = 0, k->boosting = false;
  k->accel_brake_timer = k->max_vel_timer = 0;
}

/* CarObject.restart + frame and wheels back at the start (hard restart) */
void bike_restart(void) {
  Bike *k = &bike;
  k->safe_id = -1;
  k->finished = false;
  /* the frame's start: the level's start plus its centre of mass (it was aligned) */
  k->review_x = start_x, k->review_y = start_y;
  bike_review();
  for (int w = 0; w < 2; w++) {
    sp.mats[k->mat_wheel[w]].dyn = sp.mats[k->mat_wheel[w]].stat = w ? CAR_wheelFrontFriction : CAR_wheelBackFriction;
    k->spin_delay[w] = 0;
    k->wheel_ground[w] = false, k->wheel_end_delay[w] = 0, k->wheel_fly[w] = 0;
  }
  k->frame_ground = false, k->frame_end_delay = 0;
  k->fly_time = k->fall_time = 0;
  k->braked = false;
  k->wheel_shape[0].mat = k->mat_wheel[0];
  for (int i = 0; i < NPARTS + 4; i++) k->eff_vx[i] = k->eff_vy[i] = 0;
}

/* ---- each tick ---- */

/* userData.isGround: shapers, pathers, the ground and planks (the packer flags them) */
static bool ground_body(const Arb *a, const Body *me, V2 *p, float *nang) {
  const Body *o = a->b1 == me ? a->b2 : a->b1;
  uint16_t s = a->b1 == me ? a->s2 : a->s1;
  bool g = s & 0x8000 ? o->pad != 0 : sp.sbodies[sp.sshapes[s].body].pad != 0;
  if (!g) return false;
  if (p) {
    const Contact *c = &a->c[a->c1 >= 0 ? a->c1 : 0];
    *p = v2(c->r1x + a->b1->pos.x, c->r1y + a->b1->pos.y);
    float ang = atan2f(a->n.y, a->n.x);
    if (a->b1 == me) ang += PI;
    *nang = ang;
  }
  return true;
}

static bool touching_ground(const Body *b, V2 *p, float *nang) {
  for (int i = 0; i < NP_MAX_ARBS; i++) {
    const Arb *a = &sp.arbs[i];
    if (!a->used || a->type != A_COL || !a->active || a->up_stamp != sp.stamp) continue;
    if (a->b1 != b && a->b2 != b) continue;
    if (ground_body(a, b, p, nang)) return true;
  }
  return false;
}

static void effector(Body *b, int slot, float dt) {
  float ex = bike.eff_vx[slot], ey = bike.eff_vy[slot];
  if (ex != 0 || ey != 0) {
    b->vel.x += 2 * dt * (ex - b->vel.x);
    b->vel.y += 2 * dt * (ey - b->vel.y);
    bike.eff_vx[slot] = bike.eff_vy[slot] = 0;
    np_wake(b);
  }
}

static void wheels_update(float dt) {
  Bike *k = &bike;
  for (int w = 0; w < 2; w++) {
    Body *b = &k->wheel[w];
    if (k->spin_delay[w] > 0) {
      k->spin_delay[w] -= dt;
      float fr = w ? CAR_wheelFrontFriction : CAR_wheelBackFriction;
      Material *m = &sp.mats[k->mat_wheel[w]];
      if (k->spin_delay[w] <= 0) m->stat = m->dyn = fr;
      else if (k->spin_delay[w] < 0.5f * START_SPIN_DELAY)
        m->stat = m->dyn = START_SPIN_FRICTION + (fr - START_SPIN_FRICTION) *
                                                     (0.5f * START_SPIN_DELAY - k->spin_delay[w]) / (0.5f * START_SPIN_DELAY);
    }
    V2 p;
    float ang;
    bool e = touching_ground(b, &p, &ang);
    if (e && k->skin != 4) {
      float h = hypotf(b->vel.x, b->vel.y), l = b->angvel * k->wheel_shape[w].radius;
      if (fabsf(l) - fabsf(h) > 250) for (int q = 0; q < 3; q++) fx_ground(p.x, p.y, ang, l > 0);
    }
    if (e) k->wheel_end_delay[w] = 0, k->wheel_ground[w] = true, k->wheel_fly[w] = 0;
    else if (k->wheel_ground[w] && k->wheel_end_delay[w] == 0) k->wheel_end_delay[w] = WHEEL_END_DELAY;
    if (k->wheel_ground[w]) {
      if (k->wheel_end_delay[w] > 0) {
        k->wheel_end_delay[w] -= dt;
        if (k->wheel_end_delay[w] <= 0) k->wheel_ground[w] = false;
      }
    } else k->wheel_fly[w] += dt;
    effector(b, 1 + w, dt);
  }
}

static void frame_update(float dt) {
  Bike *k = &bike;
  bool e = touching_ground(&k->frame, 0, 0);
  if (e) k->frame_end_delay = 0, k->frame_ground = true;
  else if (k->frame_ground && k->frame_end_delay == 0) k->frame_end_delay = FRAME_END_DELAY;
  if (k->frame_ground && k->frame_end_delay > 0) {
    k->frame_end_delay -= dt;
    if (k->frame_end_delay <= 0) k->frame_ground = false;
  }
  effector(&k->frame, 0, dt);
}

static void process_land(void) {
  Bike *k = &bike;
  if (k->finished) return;
  if (!k->dead && (k->wheel_ground[0] || k->wheel_ground[1])) {
    float t = k->start_fly - k->min_fly;
    int i = (int)(t / (2 * PI));
    t -= 2 * i * PI;
    if (t >= PI) i++;
    int back = i;
    t = k->max_fly - k->start_fly;
    i = (int)(t / (2 * PI));
    t -= 2 * i * PI;
    if (t >= PI) i++;
    int front = i;
    if (back > 0) game_flip(back);
    if (front > 0) game_flip(front);
  }
  k->start_fly = k->max_fly = k->min_fly = k->rotation;
}

static void apply_drive(float t) {
  Bike *k = &bike;
  bool s = k->accell, n = k->brake;
  float o = accel_torque, a = max_ang_vel;
  if (delta_torque > 0 && s) {
    float r = k->speed / wheel_radius;
    if (r < a) {
      float p = 0.5f + 2.5f * r / a, h = (1 / (p * p) - 1.0f / 9) / (4 - 1.0f / 9);
      o = accel_torque + h * delta_torque;
    }
  }
  if (s && n) k->accel_brake_timer += t;
  else {
    if (k->accel_brake_timer > MIN_AC_BR_TIME) k->max_vel_timer = MAX_VEL_TIME;
    k->accel_brake_timer = 0;
  }
  Body *fw = &k->wheel[1], *bw = &k->wheel[0];
  if (n) {
    if (fabsf(fw->angvel) < 1) {
      k->brake_joint.jmin = k->brake_joint.jmax = fw->rot - k->frame.rot;
      if (!(k->brake_joint.flags & CF_ACTIVE)) np_con_clear(&k->brake_joint);
      k->brake_joint.flags |= CF_ACTIVE;
    }
    fw->angvel *= 0.25f;
    wheel_brake(0, true);
    if (!s) bw->angvel = 0;
  } else {
    k->brake_joint.flags &= (uint8_t)~CF_ACTIVE;
    wheel_brake(0, false);
  }
  if (s) {
    if (k->max_vel_timer > 0) {
      if (bw->angvel < 1.2f * a) bw->angvel += 1.5f * t * o * bw->iinertia;
      k->max_vel_timer -= t;
    } else {
      if (bw->angvel < 0) bw->angvel *= 0.5f;
      if (bw->angvel < a) bw->angvel += t * o * bw->iinertia;
    }
    np_wake(bw);
  } else k->max_vel_timer = 0;
}

static void apply_tilt(float e) {
  Bike *k = &bike;
  k->tilt = (int)e;
  Body *f = &k->frame;
  f->torque = e * tilt_torque;
  np_wake(f);
  float s = f->angvel;
  if (s < 0 && s < -max_tilt_speed) f->angvel = -max_tilt_speed;
  else if (s > 0 && s > max_tilt_speed) f->angvel = max_tilt_speed;
  float n = e;
  if (n == 0) {
    if (k->frame_ground || k->wheel_ground[0] || k->wheel_ground[1]) {
      if (k->accell) n = -1;
    } else {
      float vy = f->vel.y;
      if (vy < -20) n = 1;
      else if (vy > 20) n = -1;
    }
  }
  driver_move(e, n);
}

/* the bike's centre of mass (Compound.COM) */
static V2 bike_com(void) {
  float m = 0, x = 0, y = 0;
  for (int i = 0; i < 4 + NPARTS; i++) {
    Body *b = all_bodies[i];
    if (!(b->flags & BF_SPACE)) continue;
    V2 c = np_local_to_world(b, b->lcom);
    x += c.x * b->mass, y += c.y * b->mass, m += b->mass;
  }
  return m ? v2(x / m, y / m) : bike.frame.pos;
}

void bike_update(float dt, uint32_t keys) {
  Bike *k = &bike;
  Body *f = &k->frame;
  /* wheels, frame and parts update before the car (their order in MaxGameCore) */
  wheels_update(dt);
  frame_update(dt);
  for (int p = 0; p < NPARTS; p++) effector(&k->part[p], 4 + p, dt);
  if (k->dead) k->x = k->part[P_HEAD].pos.x, k->y = k->part[P_HEAD].pos.y;
  else k->x = f->pos.x, k->y = f->pos.y;
  k->rotation = f->rot;
  float prev = k->speed;
  k->speed = hypotf(f->vel.x, f->vel.y);
  bool ground = k->frame_ground || k->wheel_ground[0] || k->wheel_ground[1];
  if (ground) {
    if (k->fly_time > LONG_FLY_TIME) {
      process_land();
      if (prev > 500) fx_landing();
    }
    k->fly_time = 0, k->fall_time = 0;
  } else {
    if (k->fly_time == 0) k->max_fly = k->min_fly = k->start_fly = k->rotation;
    if (k->rotation > k->max_fly) k->max_fly = k->rotation;
    else if (k->rotation < k->min_fly) k->min_fly = k->rotation;
    k->fly_time += dt;
    if (f->vel.y > 0) k->fall_time += dt;
    else k->fall_time = 0;
  }
  if (k->drive) {
    k->accell = !k->finished && (keys & K_UP);
    k->brake = !k->finished && (keys & K_DOWN);
    apply_drive(dt);
    if (k->finished) apply_tilt(0);
    else apply_tilt((keys & K_RIGHT ? 1 : 0) - (keys & K_LEFT ? 1 : 0));
    if (k->skin != 4) {
      k->smoke_timer -= k->accell ? 2 * dt : dt;
      if (k->smoke_timer <= 0) {
        k->smoke_timer += SMOKE_TIME;
        V2 a = np_local_to_world(f, v2(-43, -16));
        float h = 0.1f * PI;
        if (k->accell) {
          fx_smoke(a.x, a.y, k->rotation - 0.5f * h + fx_rand() * h);
          fx_smoke(a.x, a.y, k->rotation - 0.1f * PI + 0.5f * h + fx_rand() * h);
          fx_smoke(a.x, a.y, k->rotation + 0.1f * PI + 0.5f * h + fx_rand() * h);
        } else fx_smoke(a.x, a.y, k->rotation - 0.5f * h + fx_rand() * h);
      }
    }
    if (k->skin == 5) fx_fire(k->part[P_HEAD].pos.x, k->part[P_HEAD].pos.y);
  } else if (k->finished) {
    k->wheel[0].angvel *= 0.8f, k->wheel[1].angvel *= 0.8f;
  }
  /* the boost */
  if (k->boost_left > 0) {
    V2 c = bike_com();
    float r = f->rot;
    np_apply_impulse(f, v2(dt * k->boost_force * cosf(r), dt * k->boost_force * sinf(r)), c);
    if (!ground) f->angvel *= 0.8f;
    k->boost_left -= dt;
    k->boosting = k->boost_left > 0;
  }
}

/* BikeObject.processDriverGround */
void bike_driver_ground(void) {
  Bike *k = &bike;
  if (k->dead) return;
  k->drive = false, k->dead = true;
  connect_driver(false);
  for (int p = 0; p < NPARTS; p++) joint_relax(&k->pj[p]);
  mirror_parts(true);
  apply_tilt(0);
  game_died();
}

void bike_explode(void) {
  Bike *k = &bike;
  if (k->exploded) return;
  k->exploded = true;
  Body *f = &k->frame;
  for (int w = 0; w < 2; w++) {
    Body *b = &k->wheel[w];
    float dx = b->pos.x - f->pos.x, dy = b->pos.y - f->pos.y, l = hypotf(dx, dy);
    if (l > 0) dx /= l, dy /= l;
    con_on(&k->line[w], false), con_on(&k->dist[w], false);
    b->vel.x += 500 * dx, b->vel.y += 500 * dy;
    np_wake(b);
  }
  f->vel.y -= 100;
  np_wake(f);
  fx_explosion(f->pos.x, f->pos.y, 0);
  fx_shake();
}

void bike_spikes(Body *part) {
  bike.spiked = true;
  if (part) {
    stop(part);
    part->flags |= BF_NOMOVE | BF_NOROT;
    np_body_mass(part);
  }
}

void bike_finish(void) {
  Bike *k = &bike;
  if (k->finished) return;
  k->drive = false, k->finished = true;
  apply_tilt(0);
  game_finished();
}

void bike_add_boost(float time, float force) {
  Bike *k = &bike;
  if (k->dead) return;
  if (k->boost_left <= 0) k->boost_force = force, k->boost_left = time;
  else {
    if (force > k->boost_force) k->boost_force = force;
    k->boost_left += time;
  }
  k->boosting = true;
}
