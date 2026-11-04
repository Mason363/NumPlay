/* circloO's physics: the parts of Box2D (2.3) the game uses, set up as GameMaker does.
 *
 * Meters inside (50 pixels a meter), y down. Body 0 is the ground: every static shape (circles, edges,
 * boxes, triangles) belongs to it. Moving bodies are circles, or boxes turning on a pin at their middle
 * (the game's rotating bars). Joints: rope, distance and friction. One step is 1/60 s with 10 velocity and
 * 10 position iterations, as GameMaker runs Box2D. */
#ifndef CO_PHYS_H
#define CO_PHYS_H
#include <stdbool.h>
#include <stdint.h>

#define PX_M 0.02f            /* meters a pixel */
#define MAX_BODIES 128
#define MAX_SCIRCLES 256
#define MAX_SEDGES 800
#define MAX_SPOLYS 24
#define MAX_CONTACTS 360
#define MAX_JOINTS 40
#define NSTATIC (MAX_SCIRCLES + MAX_SEDGES + MAX_SPOLYS)

enum { SH_CIRCLE, SH_BOX };

typedef struct {
  float x, y, a;          /* place (m), angle (rad) */
  float vx, vy, w;
  float im, ii;           /* 1/mass, 1/inertia (0: does not move / turn) */
  float ld, ad;           /* linear and angular damping */
  float fr;               /* friction */
  float r, hx, hy;        /* circle radius, or the box's half sizes */
  float c, s;             /* cos and sin of a */
  float sx, sy;           /* place at the start of the step (sweeps) */
  uint8_t shape, alive, pinned, user;
} Body;

typedef struct { float x, y, r; } SCircle;
typedef struct {
  float x1, y1, x2, y2;
  int16_t prev, next;     /* the neighbours in a chain (Box2D's ghost vertices), -1 if none */
} SEdge;
typedef struct {
  uint8_t n;
  float x[4], y[4], nx[4], ny[4];
} SPoly;

enum { J_ROPE, J_DIST, J_FRIC };
typedef struct {
  uint8_t type, alive, a, b;
  float lax, lay, lbx, lby;  /* anchors, in each body's frame */
  float len;                 /* rope: max length; distance: length */
  float maxf, maxt;          /* friction joint */
  float ux, uy, rax, ray, rbx, rby, mass, imp;   /* solver */
  float fix, fiy, fia, m11, m12, m22, am;
} Joint;

typedef struct {
  float rax, ray, rbx, rby, ni, ti, nm, tm;
} CPoint;
typedef struct {
  uint32_t key;
  uint8_t a, b;            /* b is 0 for the ground */
  uint16_t sid;            /* the static shape, for the ground */
  uint8_t n;
  float nx, ny, fr;
  CPoint p[2];
} Contact;
typedef struct { uint32_t key; float ni[2], ti[2]; } Warm;

typedef struct {
  Body b[MAX_BODIES];
  int nb;
  SCircle sc[MAX_SCIRCLES];
  SEdge se[MAX_SEDGES];
  SPoly sp[MAX_SPOLYS];
  int nsc, nse, nsp;
  int keep_se;             /* edges below this stay: the level's boundary comes after them */
  Joint j[MAX_JOINTS];
  int nj;
  Contact c[MAX_CONTACTS];
  int nc;
  Warm warm[MAX_CONTACTS];
  int nwarm;
  float gx, gy;
  /* the static shapes by grid cell (2 m, the room is 60 m) */
  uint16_t cell_start[30 * 30 + 1];
  uint16_t cell_items[3072];
  uint16_t stamp[NSTATIC];
  uint16_t stamp_now;
  uint8_t order[MAX_BODIES];
  float minx[MAX_BODIES];
} World;

extern World W;

void ph_reset(void);
int ph_add_circle(float x, float y, float r, float density, float friction);     /* a moving circle */
int ph_add_rotor(float x, float y, float hx, float hy, float angle, float density, float adamp);
void ph_static_circle(float x, float y, float r);
void ph_static_edge(float x1, float y1, float x2, float y2);
void ph_static_chain(const float *xy, int n, bool loop);
void ph_static_poly(const float *xy, int n);
void ph_build_grid(void);
void ph_keep_statics(void);   /* the static shapes so far stay; ph_drop_statics drops the later ones */
void ph_drop_statics(void);
int ph_rope(int a, int b, float ax, float ay, float bx, float by, float len);
int ph_dist(int a, int b, float ax, float ay, float bx, float by);
int ph_fric(int a, int b, float ax, float ay, float maxf, float maxt);
void ph_kill(int body);
void ph_step(void);
void ph_joint_anchors(const Joint *j, float *ax, float *ay, float *bx, float *by);
bool ph_overlaps(int i, float x, float y, float r);   /* body i over the circle (x, y, r) */
#endif
