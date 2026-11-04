/* Moto X3M: the physics. A port to C of the parts of Nape 2 (Luca Deltodesco, BSD 2-clause, LICENSES/Nape.txt)
 * the game uses: bodies of circles and convex polygons, Nape's contact solver (two-contact block solver, position
 * iterations), its sleeping islands, sensors with begin and end events, and six constraints (pivot, weld,
 * distance, line, angle, motor) with Nape's soft-constraint maths. Floats instead of doubles; no continuous
 * collisions or fluids (the levels have no water).
 *
 * Static shapes live in the level data (world coordinates, their bodies at the origin); dynamic and kinematic
 * bodies own RAM shapes in body coordinates whose world copies are refreshed each step. */
#ifndef MX_NAPE_H
#define MX_NAPE_H
#include <stdbool.h>
#include <stdint.h>

typedef struct { float x, y; } V2;

enum { B_STATIC, B_DYNAMIC, B_KINEMATIC };
/* BF_FROZEN: out of the game's window (far from the camera): kept still until it comes back in view;
 * BF_KEEP: its contacts are always made, taking the slot of another's when all are in use (the bike) */
enum { BF_SPACE = 1, BF_NOMOVE = 2, BF_NOROT = 4, BF_SLEEP = 8, BF_FROZEN = 16, BF_KEEP = 32 };
#define BF_STILL (BF_SLEEP | BF_FROZEN)
enum { SH_CIRCLE, SH_POLY };
enum { SF_SENSOR = 1, SF_FLUID = 2 };

typedef struct {
  float elasticity, dyn, stat, density, rolling;
} Material;

typedef struct {
  int32_t cgroup, cmask, sgroup, smask;
} Filter;

struct Body;

/* a shape of a dynamic or kinematic body, in the body's own coordinates */
typedef struct Shape {
  struct Body *body;
  const float *loc;        /* polygon: local vertices (2n floats) */
  float *glob;             /* world vertices (2n), edge normals (2n, outwards), projections (n), refreshed each step */
  float minx, miny, maxx, maxy;
  V2 lcom, wcom;           /* centre of mass (a circle's centre), local and world */
  float radius, area, inertia;   /* inertia per unit mass about the body's origin */
  uint16_t id;             /* 0x8000 | its index in sp.shapes */
  uint8_t type, n, flags, filter, mat, pad;   /* mat: index in sp.mats */
} Shape;

typedef struct Body {
  V2 pos, vel, axis;       /* axis = (sin rot, cos rot) */
  float rot, angvel, torque;
  float mass, imass, iinertia;
  V2 lcom;
  float minx, miny, maxx, maxy;
  Shape *shapes;
  void *user;
  int32_t waket;
  uint16_t cb;             /* the game's callback types */
  int16_t dsf, idx;        /* islands; its index in sp.bodies */
  uint8_t nshapes, type, flags, pad;
} Body;

/* static shapes, in the level data: their box's corner and size, their vertices as int16 pairs from the corner
 * (in quarter pixels, or half, whole or two pixels for big ones: unit in flags' bits 4-5) */
typedef struct {
  float x0, y0;
  int16_t w, h;
  uint16_t voff;
  uint8_t n, filter, mat, flags;
  uint16_t body;
} SShape;

typedef struct {
  uint16_t cb;
  uint8_t flags, pad;      /* BF_SPACE */
  float viscosity, density;   /* a fluid's (water) */
} SBody;

enum { C_PIVOT, C_WELD, C_DIST, C_LINE, C_ANGLE, C_MOTOR };
enum { CF_ACTIVE = 1, CF_STIFF = 2, CF_IGNORE = 4, CF_SLACK = 8, CF_EQUAL = 16 };

typedef struct Con {
  Body *b1, *b2;
  int16_t sb;               /* the static body standing in for the world, or -1 (for CF_IGNORE) */
  uint8_t type, flags;
  float frequency, damping;
  V2 a1, a2, a1rel, a2rel;      /* anchors (local, rotated) */
  V2 nlocal, nrel;              /* line joint's direction */
  float jmin, jmax, ratio, rate, phase;
  float k[6], jacc[3], bias[3]; /* effective mass (2x2 or 3x3 symmetric), accumulated impulse, bias */
  float gamma, pre_dt, scale, cx1, cx2, dot1, dot2;
  V2 n;
  void *user;
} Con;

typedef struct {
  float r1x, r1y, r2x, r2y, lr1x, lr1y, lr2x, lr2y;
  float dist, jn, jt, nmass, tmass, bounce, friction;
  int32_t stamp;
  uint8_t hash, fresh, active, posonly;
} Contact;

enum { A_COL, A_SENSOR, A_FLUID };
enum { PT_CIRCLE, PT_FACE1, PT_FACE2 };

typedef struct Arb {
  Body *b1, *b2;
  uint16_t s1, s2;         /* shape ids: static ones < 0x8000, dynamic ones | 0x8000 */
  uint8_t type, ptype, rev, nc;
  uint8_t fresh, active, present, hc2, hpc2, stat, used, pad;
  int32_t stamp, up_stamp;
  V2 n, lnorm;
  float lproj, radius, biascoef;
  float dyn, stat_f, rest, rfric;
  float jr, rmass;
  float rn1a, rn1b, rn2a, rn2b, rt1a, rt1b, rt2a, rt2b;
  float km[3], kk[3];
  union {
    Contact c[2];
    struct {               /* a fluid arbiter (FluidArbiter) */
      float overlap, cx, cy, r1x, r1y, r2x, r2y, wmass, adamp, agamma, vmass[3], dampx, dampy, lgamma, nx, ny;
      uint8_t nodrag;
    } f;
  };
  int8_t c1, c2;           /* the active contacts */
  int16_t next;            /* free list */
} Arb;

#ifndef NP_MAX_ARBS
#define NP_MAX_ARBS 128
#endif
#define NP_MAX_EVENTS 48

typedef struct {
  uint8_t begin, type;     /* begin or end, A_COL, A_SENSOR or A_FLUID */
  Body *b1, *b2;           /* null: the static body sb1 / sb2 */
  int sb1, sb2;            /* static bodies (-1 if dynamic) */
  float px, py;            /* a collision's first contact */
} Event;

typedef struct {
  V2 gravity;
  int32_t stamp;
  /* the level's static shapes */
  const SShape *sshapes;
  int nsshapes;
  const int16_t *sverts;
  SBody *sbodies;
  int nsbodies;
  const uint16_t *buckets;  /* per 256 px column: first index into bucket_list, count; then the wide shapes' */
  const uint16_t *bucket_list;
  int nbuckets, bucket_x0;
  /* dynamic and kinematic bodies */
  Body **bodies;
  int nbodies;
  Shape **shapes;           /* all RAM shapes, by id */
  int nshapes;
  Con **cons;
  int ncons;
  Material *mats;           /* every shape's material, by index */
  const Filter *filters;
  Arb arbs[NP_MAX_ARBS];
  int16_t free_arb;
  Event events[NP_MAX_EVENTS];
  int nevents;
  bool overflow;
} Space;

extern Space sp;

void np_reset(void);
/* a body's mass and inertia from its shapes (after changing them) */
void np_body_mass(Body *b);
void np_body_set_rot(Body *b, float r);
void np_step(float dt);
/* forget a body's or a shape's contacts (taken out of the space, or its material changed) */
void np_drop_body(Body *b);
void np_drop_shape(Shape *s);
void np_con_clear(Con *c);
void np_body_add(Body *b);
/* a static body taken out of the space: its contacts go */
void np_drop_static(int sb);
Body *np_world(void);
void np_body_remove(Body *b);
void np_wake(Body *b);
/* bodies wholly outside [x0, x1] freeze, the others thaw (BF_FROZEN) */
void np_window(float x0, float x1);
/* a body moved or turned by hand: its shapes follow, and it wakes */
void np_body_moved(Body *b);
/* registers a body (and its shapes) with the space; it enters the space with np_body_add */
void np_register(Body *b);
void np_register_con(Con *c);

static inline V2 v2(float x, float y) { V2 r = {x, y}; return r; }
static inline V2 np_local_to_world(const Body *b, V2 p) {
  return v2(b->pos.x + p.x * b->axis.y - p.y * b->axis.x, b->pos.y + p.x * b->axis.x + p.y * b->axis.y);
}
static inline V2 np_world_to_local(const Body *b, V2 p) {
  float dx = p.x - b->pos.x, dy = p.y - b->pos.y;
  return v2(dx * b->axis.y + dy * b->axis.x, dy * b->axis.y - dx * b->axis.x);
}
void np_apply_impulse(Body *b, V2 j, V2 at);
/* the first shape a ray meets (dir unit length, up to maxd): its body (or null and *sbody a static body) and its
 * distance; false if none */
bool np_raycast(V2 from, V2 dir, float maxd, Body **body, int *sbody, float *dist);
/* the bodies with a shape under a point: dynamic ones in out[], and the first static body in *sbody */
int np_bodies_under(V2 p, Body **out, int max, int *sbody);
#endif
