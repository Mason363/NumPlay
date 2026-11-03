/* Moto X3M: the physics on its own (a floor, falling boxes and wheels, a pendulum). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "nape.h"

static Material mats[1] = {{0, 1, 2, 1, 0.001f}};
static const Filter filters[1] = {{1, -1, 1, -1}};
static SShape sshapes[1];
static SBody sbodies[1];
static uint16_t buckets[2 * 64], blist[64];

static Body bodies[32];
static Shape shapes[32];
static float loc[32][20], glob[32][20];
static Body *bptr[32];
static Shape *sptr[32];
static Con *cptr[8];

static void box_shape(Shape *s, int i, float w, float h) {
  V2 v[4] = {{-w / 2, -h / 2}, {w / 2, -h / 2}, {w / 2, h / 2}, {-w / 2, h / 2}};
  for (int k = 0; k < 4; k++) {
    V2 u = v[k], n = v[(k + 1) & 3];
    float dx = u.x - n.x, dy = u.y - n.y, l = sqrtf(dx * dx + dy * dy);
    dx /= l, dy /= l;
    loc[i][2 * k] = u.x, loc[i][2 * k + 1] = u.y;
    loc[i][8 + 2 * k] = -dy, loc[i][8 + 2 * k + 1] = dx;
    loc[i][16 + k] = -dy * u.x + dx * u.y;
  }
  s->type = SH_POLY, s->n = 4, s->loc = loc[i], s->glob = glob[i];
  s->area = w * h;
  s->inertia = (w * w + h * h) / 12;
  s->mat = 0;
}

int main(void) {
  np_reset();
  sp.gravity = v2(0, 500);
  /* floor from x -1000 to 1000 at y 0..100: 4 verts, 4 normals, 4 projections */
  V2 fv[4] = {{-1000, 0}, {1000, 0}, {1000, 100}, {-1000, 100}};
  static float sd2[20];
  for (int k = 0; k < 4; k++) {
    V2 u = fv[k], n = fv[(k + 1) & 3];
    float dx = u.x - n.x, dy = u.y - n.y, l = sqrtf(dx * dx + dy * dy);
    dx /= l, dy /= l;
    sd2[2 * k] = u.x, sd2[2 * k + 1] = u.y;
    sd2[8 + 2 * k] = -dy, sd2[8 + 2 * k + 1] = dx;
    sd2[16 + k] = -dy * u.x + dx * u.y;
  }
  sshapes[0] = (SShape){-1000, 0, 1000, 100, 0, 4, 0, 0, 0, 0, 0};
  sbodies[0].flags = BF_SPACE;
  sp.sshapes = sshapes, sp.nsshapes = 1, sp.sdata = sd2, sp.sbodies = sbodies, sp.nsbodies = 1;
  for (int c = 0; c < 8; c++) buckets[2 * c] = 0, buckets[2 * c + 1] = 1;
  blist[0] = 0;
  sp.buckets = buckets, sp.bucket_list = blist, sp.nbuckets = 8, sp.bucket_x0 = -1024;
  sp.bodies = bptr, sp.shapes = sptr, sp.cons = cptr, sp.mats = mats, sp.filters = filters;
  /* a stack of 5 boxes */
  for (int i = 0; i < 5; i++) {
    Body *b = &bodies[i];
    b->type = B_DYNAMIC;
    b->pos = v2(0, -20 - 41 * i);
    np_body_set_rot(b, 0);
    b->shapes = &shapes[i], b->nshapes = 1;
    box_shape(&shapes[i], i, 40, 40);
    np_register(b);
    np_body_add(b);
  }
  /* a wheel */
  Body *w = &bodies[5];
  w->type = B_DYNAMIC, w->pos = v2(200, -300);
  np_body_set_rot(w, 0);
  w->shapes = &shapes[5], w->nshapes = 1;
  Shape *ws = &shapes[5];
  ws->type = SH_CIRCLE, ws->radius = 19, ws->area = 3.14159f * 19 * 19, ws->inertia = 19 * 19 / 2.0f, ws->mat = 0;
  np_register(w);
  np_body_add(w);
  w->angvel = 10;
  for (int t = 0; t < 600; t++) {
    np_step(1 / 60.0f);
    if (t % 60 == 59)
      printf("t=%3d top box y=%.2f rot=%.4f sleep=%d  wheel x=%.1f y=%.2f w=%.2f  arbs ovf=%d\n", t + 1, bodies[4].pos.y,
             bodies[4].rot, (bodies[4].flags & BF_SLEEP) != 0, w->pos.x, w->pos.y, w->angvel, sp.overflow);
  }
  return 0;
}
