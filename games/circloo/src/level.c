/* circloO: a level being played. Its objects come from its script (levels.h, read the way the game's level
 * creator reads it); this runs what the game's objects do each frame: the ball, the circles to collect, the
 * level growing a ring at a time, the balls that appear and go, the joints. */
#include <math.h>
#include <string.h>
#include "game.h"
#include "gfx.h"
#include "phys.h"
#include "plat.h"

#include "levels.h"

Level L;

enum { T_END, T_Y, T_C, T_I, T_IO, T_MC, T_TMC, T_B, T_T, T_RR, T_LINE, T_CHAIN, T_ROPE, T_DIST, T_FRIC, T_INFO };

#define CX 1500.0f
#define CY 1500.0f
#define RMAX 1400.0f
#define BALL_R 32.0f
#define PUSH 0.53f                /* px a frame, each frame an arrow is held (0.52 + HTML5's 0.01) */
#define M(v) ((v) * PX_M)
#define P8(v) ((v) / 8.0f)

/* ---- the colours (the game's _y1/_z1: a hue a level) ---- */

static void hsv(float h, float s, float v, float *r, float *g, float *b) {
  h = h / 255 * 6, s /= 255, v /= 255;
  int i = (int)floorf(h) % 6;
  float f = h - floorf(h), p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
  float rr[6] = {v, q, p, p, t, v}, gg[6] = {t, v, v, q, p, p}, bb[6] = {p, p, t, v, v, q};
  *r = rr[i] * 255, *g = gg[i] * 255, *b = bb[i] * 255;
}

static void hue_of(int n, float *r, float *g, float *b) {
  if (n <= 11) hsv(255.0f * n / 11, 210, 255, r, g, b);
  else hsv(fmodf(255.0f * (n - 11.5f) / 11 + 255, 255), 180, 255, r, g, b);
}

void colours_for(float level) {
  float r, gr, b;
  int lo = (int)floorf(level);
  hue_of(lo, &r, &gr, &b);
  if (level != lo) {
    float r2, g2, b2, t = level - lo;
    hue_of(lo + 1, &r2, &g2, &b2);
    r += (r2 - r) * t, gr += (g2 - gr) * t, b += (b2 - b) * t;
  }
  /* B: merged 30% with grey; A: B at 40%; C: B at 80% */
  r = r * 0.7f + 128 * 0.3f, gr = gr * 0.7f + 128 * 0.3f, b = b * 0.7f + 128 * 0.3f;
  g.hue_b = rgb((int)r, (int)gr, (int)b);
  g.hue_a = rgb((int)(r * 0.4f), (int)(gr * 0.4f), (int)(b * 0.4f));
  g.hue_c = rgb((int)(r * 0.8f), (int)(gr * 0.8f), (int)(b * 0.8f));
}

/* ---- building ---- */

static void boundary(void) {
  ph_drop_statics();
  int n = L.R > 200 ? 200 : 100;
  static float xy[400];
  for (int k = 0; k < n; k++) {
    float a = 6.28318530717959f * k / n + 3.14159265358979f;
    xy[2 * k] = M(sinf(a) * L.R + CX), xy[2 * k + 1] = M(cosf(a) * L.R + CY);
  }
  ph_static_chain(xy, n, true);
  ph_build_grid();
}

static void make_mc(Mc *m) {
  m->body = (int16_t)ph_add_circle(M(m->x), M(m->y), M(m->r), m->dens, 0.2f);
  if (m->body >= 0) W.b[m->body].user = 1;
  L.objbody[m->obj] = m->body;
}

static void spawn_timed(const Tmc *t, int life) {
  for (int k = 0; k < MAX_TIMED; k++)
    if (L.timed[k].body <= 0) {
      int b = ph_add_circle(M(t->x), M(t->y), M(t->r), t->dens, 0.2f);
      if (b < 0) return;
      W.b[b].user = 2;
      L.timed[k] = (Timed){(int16_t)b, (int16_t)life, 0};
      return;
    }
}

/* the two edges either side of a line, 2 pixels off it (obj_linemanager) */
static void line_edges(float x1, float y1, float x2, float y2) {
  float dx = x2 - x1, dy = y2 - y1, l = sqrtf(dx * dx + dy * dy);
  if (l < 1e-6f) return;
  float nx = -dy / l * 2, ny = dx / l * 2;
  ph_static_edge(M(x1 + nx), M(y1 + ny), M(x2 + nx), M(y2 + ny));
  ph_static_edge(M(x1 - nx), M(y1 - ny), M(x2 - nx), M(y2 - ny));
}

/* the two chains either side of a path (obj_chainmanager) */
static void chain_edges(const int16_t *p, int n) {
  static float xy[2][2 * 300];
  if (n > 300) n = 300;
  for (int k = 0; k < n; k++) {
    int a = k == n - 1 ? k - 1 : k, b = a + 1;
    float dx = P8(p[2 * b] - p[2 * a]), dy = P8(p[2 * b + 1] - p[2 * a + 1]), l = sqrtf(dx * dx + dy * dy);
    float nx = l > 0 ? -dy / l * 2 : 0, ny = l > 0 ? dx / l * 2 : 0;
    float x = P8(p[2 * k]), y = P8(p[2 * k + 1]);
    xy[0][2 * k] = M(x + nx), xy[0][2 * k + 1] = M(y + ny);
    xy[1][2 * k] = M(x - nx), xy[1][2 * k + 1] = M(y - ny);
  }
  ph_static_chain(xy[0], n, false);
  ph_static_chain(xy[1], n, false);
}

void level_load(int level, bool hard) {
  memset(&L, 0, sizeof L);
  ph_reset();
  L.level = level, L.hard = hard;
  L.data = level_data + level_start[(hard ? NBASE : 0) + level - 1];
  L.R = 200;
  L.ball = -1;
  memset(L.info, -1, sizeof L.info);
  for (const int16_t *r = L.data; *r != T_END; r += 2 + r[1]) {
    const int16_t *v = r + 2;
    switch (r[0]) {
    case T_Y:
      if (L.ball < 0) {
        L.ball = ph_add_circle(M(P8(v[0])), M(P8(v[1])), M(BALL_R), 1, 0.02f);
        W.b[L.ball].ld = 0.5f, W.b[L.ball].ad = 0.1f;
      }
      break;
    case T_C:
      ph_static_circle(M(P8(v[0])), M(P8(v[1])), M(P8(v[2])));
      if (L.nobj < MAX_OBJ) L.objbody[L.nobj++] = 0;
      break;
    case T_B: {
      float x = P8(v[0]), y = P8(v[1]), hx = P8(v[2]), hy = P8(v[3]);
      float xy[8] = {M(x - hx), M(y - hy), M(x + hx), M(y - hy), M(x + hx), M(y + hy), M(x - hx), M(y + hy)};
      ph_static_poly(xy, 4);
      if (L.nobj < MAX_OBJ) L.objbody[L.nobj++] = 0;
      break;
    }
    case T_T: {
      float xy[6];
      for (int k = 0; k < 6; k++) xy[k] = M(P8(v[2 + k]));
      ph_static_poly(xy, 3);
      if (L.nobj < MAX_OBJ) L.objbody[L.nobj++] = 0;
      break;
    }
    case T_MC:
      if (L.nmc < MAX_MC && L.nobj < MAX_OBJ) {
        Mc *m = &L.mc[L.nmc++];
        m->x = P8(v[0]), m->y = P8(v[1]), m->r = P8(v[2]), m->dens = v[3] / 100.0f;
        m->dist = sqrtf((m->x - CX) * (m->x - CX) + (m->y - CY) * (m->y - CY));
        m->obj = (int16_t)L.nobj, m->body = -1;
        L.objbody[L.nobj++] = -1;
      }
      break;
    case T_RR: {
      float dens = v[5] / 100.0f, damp = v[6] < 0 ? 0 : v[6] / 100.0f;
      int b = ph_add_rotor(M(P8(v[0])), M(P8(v[1])), M(P8(v[2]) / 2), M(P8(v[3]) / 2), v[4] / 100.0f * 3.14159265f / 180, dens, damp);
      if (b >= 0) W.b[b].user = 3;
      if (L.nobj < MAX_OBJ) L.objbody[L.nobj++] = (int16_t)b;
      break;
    }
    case T_TMC:
      if (L.ntmc < MAX_TMC) {
        Tmc *t = &L.tmc[L.ntmc++];
        t->x = P8(v[0]), t->y = P8(v[1]), t->r = P8(v[2]), t->dens = v[3] / 100.0f;
        t->life = v[4], t->period = v[5], t->alarm = (int16_t)(v[5] + v[6]);
        t->dist = sqrtf((t->x - CX) * (t->x - CX) + (t->y - CY) * (t->y - CY));
      }
      break;
    case T_I:
    case T_IO:
      if (L.ncoll < 8) L.coll[L.ncoll++] = (Coll){P8(v[0]), P8(v[1]), (uint8_t)(r[0] == T_IO), 0};
      break;
    case T_LINE: line_edges(P8(v[0]), P8(v[1]), P8(v[2]), P8(v[3])); break;
    case T_CHAIN: chain_edges(v + 1, v[0]); break;
    case T_ROPE:
    case T_DIST:
    case T_FRIC:
      if (L.npj < MAX_PJ) {
        PJoint *j = &L.pj[L.npj++];
        j->type = (uint8_t)r[0], j->oa = v[0], j->ob = v[1], j->joint = -1;
        if (r[0] == T_FRIC) j->f = v[2] / 100.0f, j->t = v[3] / 100.0f;
        else j->ax = P8(v[2]), j->ay = P8(v[3]), j->bx = P8(v[4]), j->by = P8(v[5]);
        if (r[0] == T_ROPE) j->len = P8(v[6]);
      }
      break;
    case T_INFO:
      if (v[0] >= 1 && v[0] <= 7) L.info[v[0]] = (int8_t)v[1];
      break;
    }
  }
  ph_keep_statics();
  for (int k = 0; k < L.nmc; k++)
    if (L.R >= L.mc[k].dist) make_mc(&L.mc[k]);
  boundary();
  colours_for((float)level);
}

/* ---- each frame ---- */

static int body_of(int obj) { return obj >= 0 && obj < L.nobj ? L.objbody[obj] : -1; }

static void joints_tick(void) {
  for (int k = 0; k < L.npj; k++) {
    PJoint *j = &L.pj[k];
    if (j->joint < 0) {
      int a = body_of(j->oa), b = body_of(j->ob);
      if (a < 0 || b < 0 || (a == 0 && b == 0)) continue;
      if (j->type == T_ROPE) j->joint = (int16_t)ph_rope(a, b, M(j->ax), M(j->ay), M(j->bx), M(j->by), M(j->len));
      else if (j->type == T_DIST) j->joint = (int16_t)ph_dist(a, b, M(j->ax), M(j->ay), M(j->bx), M(j->by));
      else j->joint = (int16_t)ph_fric(a, b, W.b[a].x, W.b[a].y, j->f, j->t);
      if (j->joint < 0) j->joint = 999;
    } else if (j->fade < 5) j->fade++;
  }
}

static void grow(void) {
  float old = L.R;
  L.R += 200;
  for (int k = 0; k < L.nmc; k++)
    if (L.mc[k].body < 0 && L.R >= L.mc[k].dist) make_mc(&L.mc[k]);
  for (int k = 0; k < L.ntmc; k++) {
    Tmc *t = &L.tmc[k];
    float d = t->dist + t->r;
    if (L.R >= d && old < d && t->alarm > t->period + 1 && t->alarm - t->period > 2) spawn_timed(t, t->alarm - t->period);
  }
  boundary();
}

static void take(Coll *c) {
  c->taken = 1;
  L.taken++;
  if (L.R < RMAX) L.grow = 10;
  else L.complete = true;
}

void level_tick(uint32_t keys) {
  Body *ball = L.ball >= 0 ? &W.b[L.ball] : NULL;
  if (ball && !L.complete) {
    float dv = PUSH * 60 * PX_M;   /* (px a frame, in m/s) */
    if (keys & K_LEFT) ball->vx -= dv;
    if (keys & K_RIGHT) ball->vx += dv;
  }
  /* the timed balls' makers and lives (alarms) */
  for (int k = 0; k < L.ntmc; k++) {
    Tmc *t = &L.tmc[k];
    if (--t->alarm <= 0) {
      if (L.R >= t->dist + t->r) spawn_timed(t, t->life);
      t->alarm += t->life + t->period;
    }
  }
  for (int k = 0; k < MAX_TIMED; k++) {
    Timed *t = &L.timed[k];
    if (t->body <= 0) continue;
    if (t->age < 10) t->age++;
    if (--t->life <= 0) ph_kill(t->body), t->body = 0;
  }
  if (L.grow > 0 && --L.grow == 0) grow();
  joints_tick();
  ph_step();
  if (!L.complete) L.frames++;
  /* the circles: the ball takes the plain ones, the other balls and the bars the others */
  for (int k = 0; k < L.ncoll; k++) {
    Coll *c = &L.coll[k];
    if (c->taken || L.complete) continue;
    if (!c->io) {
      if (ball) {
        float dx = M(c->x) - ball->x, dy = M(c->y) - ball->y, r = M(24 + 32.5f);
        if (dx * dx + dy * dy < r * r) take(c);
      }
    } else {
      for (int b = 1; b < W.nb; b++)
        if (W.b[b].alive && W.b[b].user && ph_overlaps(b, M(c->x), M(c->y), M(24))) {
          take(c);
          break;
        }
    }
  }
}

/* ---- drawing ---- */

#define SCALE 0.32f
static float sx(float x) { return (x - g.camx) * SCALE + SCREEN_W / 2; }
static float sy(float y) { return (y - g.camy) * SCALE + SCREEN_H / 2; }

static void ring(float x, float y, int alpha, bool io) {
  gfx_disc(sx(x), sy(y), 20 * SCALE, g.hue_a, 256);
  gfx_disc(sx(x), sy(y), 16 * SCALE, g.hue_b, alpha);
  if (io) gfx_disc(sx(x), sy(y), 10 * SCALE, g.hue_a, 256);
}

void level_draw(void) {
  uint16_t A = g.hue_a, B = g.hue_b;
  gfx_fill(A);
  float cx = sx(CX), cy = sy(CY);
  if (L.grow > 0) gfx_disc(cx, cy, (L.R + 200) * SCALE, B, (10 - L.grow) * 256 / 10);
  gfx_disc(cx, cy, L.R * SCALE, B, 256);
  /* the circles to collect (only drawn inside the level, and in the ring that is appearing) */
  for (int k = 0; k < L.ncoll; k++) {
    const Coll *c = &L.coll[k];
    if (c->taken) continue;
    float d = sqrtf((c->x - CX) * (c->x - CX) + (c->y - CY) * (c->y - CY));
    if (d < L.R) ring(c->x, c->y, 256, c->io);
    else if (L.grow > 0 && d < L.R + 200) ring(c->x, c->y, (10 - L.grow) * 256 / 10, c->io);
  }
  /* the joints' lines */
  for (int k = 0; k < L.npj; k++) {
    const PJoint *j = &L.pj[k];
    if (j->joint < 0 || j->joint >= W.nj || j->type == T_FRIC) continue;
    const Joint *pj = &W.j[j->joint];
    if (!pj->alive) continue;
    float ax, ay, bx, by;
    ph_joint_anchors(pj, &ax, &ay, &bx, &by);
    gfx_line(sx(ax / PX_M), sy(ay / PX_M), sx(bx / PX_M), sy(by / PX_M), 5 * SCALE, g.hue_c, j->fade * 256 / 5);
  }
  /* the level's script: lines, chains, boxes, triangles */
  for (const int16_t *r = L.data; *r != T_END; r += 2 + r[1]) {
    const int16_t *v = r + 2;
    if (r[0] == T_LINE) gfx_line(sx(P8(v[0])), sy(P8(v[1])), sx(P8(v[2])), sy(P8(v[3])), 6 * SCALE, A, 256);
    else if (r[0] == T_CHAIN)
      for (int k = 0; k + 1 < v[0]; k++)
        gfx_line(sx(P8(v[1 + 2 * k])), sy(P8(v[2 + 2 * k])), sx(P8(v[3 + 2 * k])), sy(P8(v[4 + 2 * k])), 6 * SCALE, A, 256);
    else if (r[0] == T_B) gfx_rect(sx(P8(v[0] - v[2])), sy(P8(v[1] - v[3])), sx(P8(v[0] + v[2])), sy(P8(v[1] + v[3])), A, 256);
    else if (r[0] == T_T) {
      float x[3], y[3];
      for (int k = 0; k < 3; k++) x[k] = sx(P8(v[2 + 2 * k])), y[k] = sy(P8(v[3 + 2 * k]));
      gfx_poly(x, y, 3, A, 256);
    }
  }
  /* the moving things */
  for (int i = 1; i < W.nb; i++) {
    const Body *b = &W.b[i];
    if (!b->alive || i == L.ball) continue;
    float x = sx(b->x / PX_M), y = sy(b->y / PX_M);
    if (b->shape == SH_BOX) {
      float hx = b->hx / PX_M * SCALE, hy = b->hy / PX_M * SCALE, px[4], py[4];
      static const float ux[4] = {-1, 1, 1, -1}, uy[4] = {-1, -1, 1, 1};
      for (int k = 0; k < 4; k++) px[k] = x + b->c * ux[k] * hx - b->s * uy[k] * hy, py[k] = y + b->s * ux[k] * hx + b->c * uy[k] * hy;
      gfx_poly(px, py, 4, A, 256);
      continue;
    }
    int alpha = 256;
    if (b->user == 2)
      for (int k = 0; k < MAX_TIMED; k++)
        if (L.timed[k].body == i) {
          const Timed *t = &L.timed[k];
          alpha = t->life < 10 ? t->life * 256 / 10 : t->age < 10 ? t->age * 256 / 10 : 256;
        }
    gfx_disc(x, y, (b->r / PX_M + 0.5f) * SCALE, A, alpha);
  }
  if (L.ball >= 0) {
    const Body *b = &W.b[L.ball];
    gfx_disc(sx(b->x / PX_M), sy(b->y / PX_M), 32.5f * SCALE, mix(A, 0, 51), 256);
  }
  for (int k = 0; k < W.nsc; k++) gfx_disc(sx(W.sc[k].x / PX_M), sy(W.sc[k].y / PX_M), (W.sc[k].r / PX_M + 0.5f) * SCALE, A, 256);
  /* the text of this ring, at the bottom (obj_levelguihandler) */
  int ringno = (int)lroundf(L.R / 200), alpha = 256;
  int text = ringno >= 1 && ringno <= 7 ? L.info[ringno] : -1;
  if (L.grow > 0) {
    if (L.grow >= 5) alpha = (L.grow - 5) * 256 / 5;
    else {
      alpha = (5 - L.grow) * 256 / 5;
      text = ringno + 1 <= 7 ? L.info[ringno + 1] : -1;
    }
  }
  if (text >= 0 && !L.complete) {
    const char *s = level_texts[text];
    int n = gfx_wrap(F_SMALL, 0, 0, s, SCREEN_W - 16, 0, 0, AL_CENTER, 0), h = gfx_font_h(F_SMALL);
    float top = SCREEN_H - n * h - 8;
    gfx_rect(0, top, SCREEN_W, SCREEN_H, A, alpha * 205 / 256);
    gfx_wrap(F_SMALL, SCREEN_W / 2, top + 4, s, SCREEN_W - 16, B, alpha, AL_CENTER, 1);
  }
}
