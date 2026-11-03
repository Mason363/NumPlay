/* Moto X3M: the camera and the effects (see fx.h). */
#include <math.h>
#include <string.h>
#include "bike.h"
#include "fx.h"
#include "gfx.h"
#include "level.h"

Cam cam;

#define VIEW_H 480.0f      /* the game's view: 720 x 480; the calculator shows its middle 640 x 480 */
#define VIEW_W 640.0f
#define CAR_DX 150.0f

static uint32_t seed = 12345;
float fx_rand(void) {
  seed = seed * 1664525u + 1013904223u;
  return (seed >> 8) * (1.0f / 16777216.0f);
}

static float prev_x, prev_y;

void cam_reset(float x, float y) {
  cam.frame_h = VIEW_H;
  cam.scale = 0.5f;
  cam.path = -1;
  cam.tweening = false;
  cam.shake = 0;
  prev_x = x + CAR_DX, prev_y = y - 100;
  cam.px = prev_x, cam.py = prev_y;
}

static float frame_h_by_speed(float t) {
  float e = VIEW_H;
  if (t > 500) {
    if (t > 3000) t = 3000;
    e += (t - 500) / 2500 * VIEW_H;
  }
  float d = e - cam.frame_h;
  if (d < -2) e = cam.frame_h - 2;
  else if (d > 8) e = cam.frame_h + 8;
  cam.frame_h = e;
  return e;
}

/* RaceCamera.focusOnCar */
void cam_update(float x, float y, float speed, bool frozen) {
  int path;
  float o = level_cam_y(x, y, &path);
  float h = frame_h_by_speed(speed), r, l, c;
  bool tw = false;
  if (cam.path >= 0 && path >= 0 && path != cam.path) tw = true;
  if (path >= 0 || cam.path < 0) cam.path = path >= 0 ? path : cam.path;
  if (isnan(o)) {
    r = VIEW_H / h;
    l = x + CAR_DX / r, c = y - 100;
  } else {
    float d = o - y + 200;
    if (d > h) h = d;
    r = VIEW_H / h;
    c = o - 0.5f * h, l = x + CAR_DX / r;
  }
  if (tw) cam.tweening = true;
  if (cam.tweening) prev_x += 0.4f * (l - prev_x), prev_y += 0.4f * (c - prev_y);
  else prev_x = l, prev_y = c;
  cam.x = prev_x - 0.5f * VIEW_W / r, cam.y = prev_y - 0.5f * VIEW_H / r;
  cam.w = VIEW_W / r, cam.h = VIEW_H / r;
  if (!frozen) {
    cam.px = prev_x, cam.py = prev_y;
    cam.scale = r * (SCREEN_H / VIEW_H);
  }
}

bool fx_in_camera(float x, float y, float m) {
  return x > cam.x - m && x < cam.x + cam.w + m && y > cam.y - m && y < cam.y + cam.h + m;
}

void fx_shake(void) { cam.shake = 0.6f; }
float fx_shake_x(void) {
  if (cam.shake <= 0) return 0;
  float t = 0.6f - cam.shake, k = fmodf(t, 0.2f) / 0.1f;
  return 7.5f * (k < 1 ? k : 2 - k);
}
float fx_shake_y(void) { return fx_shake_x(); }

/* ---- particles ---- */

enum { PT_SMOKE, PT_DUST, PT_FIRE, PT_GLASS };
typedef struct {
  float x, y, vx, vy, t, rot, va, scale;
  uint8_t kind, frame, alive, pic;
} Part;

#define NPART 96
static Part parts[NPART];
static int next_part;

typedef struct {
  float x, y, t;
  struct Obj *hide;
  bool alive;
} Boom;
static Boom booms[4];

static Part *new_part(int kind) {
  for (int k = 0; k < NPART; k++) {
    Part *p = &parts[(next_part + k) % NPART];
    if (!p->alive) {
      next_part = (next_part + k + 1) % NPART;
      memset(p, 0, sizeof *p);
      p->kind = (uint8_t)kind, p->alive = 1, p->scale = 1;
      return p;
    }
  }
  Part *p = &parts[next_part];
  next_part = (next_part + 1) % NPART;
  memset(p, 0, sizeof *p);
  p->kind = (uint8_t)kind, p->alive = 1, p->scale = 1;
  return p;
}

void fx_smoke(float x, float y, float a) {
  Part *p = new_part(PT_SMOKE);
  p->x = x, p->y = y, p->vx = -50 * cosf(a), p->vy = -50 * sinf(a);
}

void fx_ground(float x, float y, float s, bool fwd) {
  Part *p = new_part(PT_DUST);
  float a = 128 + 64 * fx_rand(), r = fx_rand() * 0.1f * 3.14159265f, h = 2 * 3.14159265f * fx_rand();
  r = fwd ? s - r : s + r;
  p->x = x + 8 * cosf(h), p->y = y + 8 * cosf(h);
  p->vx = a * cosf(r), p->vy = a * sinf(r);
}

void fx_fire(float x, float y) {
  Part *p = new_part(PT_FIRE);
  p->x = x - 5 + 10 * fx_rand(), p->y = y - 5 + 10 * fx_rand();
  p->rot = 3.14159265f * fx_rand(), p->scale = 1.3f;
}

void fx_glass(struct Obj *o) {
  static const float pts[2][12] = {{-47, -6, -35, 2, 13, 2, 48, 2, 47, -4, 4, -4},
                                   {-23, -6, -17, 2, 7, 2, 25, 2, 24, -4, 3, -4}};
  const ObjRec *r = o->r;
  bool big = level_param(o, 3) == 1;
  float scale = level_param(o, 3), hh = 0.5f * level_param(o, 2);
  if (!fx_in_camera(r->x, r->y, 256)) return;
  float cx = o->x->gx, cy = o->x->gy + 2 * hh;
  float c = cosf(r->rot), s = sinf(r->rot);
  for (int k = 0; k < 6; k++) {
    Part *p = new_part(PT_GLASS);
    float lx = pts[big ? 0 : 1][2 * k], ly = pts[big ? 0 : 1][2 * k + 1];
    p->x = r->x + lx * c - ly * s, p->y = r->y + lx * s + ly * c;
    p->rot = r->rot, p->scale = scale, p->pic = (uint8_t)k;
    /* a small push away from where it was hit (an impulse of 5 on a 4 px circle) */
    float dx = p->x + 0.5f * fx_rand() - cx, dy = p->y - cy, l = hypotf(dx, dy);
    if (l > 0) p->vx = dx / l * 5 / 0.0503f, p->vy = dy / l * 5 / 0.0503f;
    p->va = 0;
  }
}

void fx_landing(void) {}

void fx_explosion(float x, float y, struct Obj *hide) {
  for (int i = 0; i < 4; i++)
    if (!booms[i].alive) {
      booms[i] = (Boom){x, y, 0, hide, true};
      return;
    }
}

void fx_clear(void) {
  memset(parts, 0, sizeof parts);
  memset(booms, 0, sizeof booms);
  cam.shake = 0;
}

void fx_update(float dt) {
  if (cam.shake > 0) cam.shake -= dt;
  for (int i = 0; i < NPART; i++) {
    Part *p = &parts[i];
    if (!p->alive) continue;
    p->t += dt;
    switch (p->kind) {
    case PT_SMOKE:
      p->x += p->vx * dt, p->y += p->vy * dt, p->vy -= 500 * dt;
      if (p->t >= 15 / 30.0f) p->alive = 0;
      break;
    case PT_DUST:
      p->x += p->vx * dt, p->y += p->vy * dt, p->vy += 500 * dt;
      if (p->t >= 26 / 30.0f) p->alive = 0;
      break;
    case PT_FIRE:
      p->vy -= 350 * dt, p->y += p->vy * dt;
      p->scale -= 10 * dt;
      p->rot += 3.14159265f / 180;
      if (p->t >= 11 / 20.0f || p->scale <= 0) p->alive = 0;
      break;
    case PT_GLASS:
      p->vx *= 1 - dt * 0.015f, p->vy = p->vy * (1 - dt * 0.015f) + 500 * dt;
      p->x += p->vx * dt, p->y += p->vy * dt;
      if (p->t >= 5) p->alive = 0;
      break;
    }
  }
  for (int i = 0; i < 4; i++) {
    Boom *b = &booms[i];
    if (!b->alive) continue;
    b->t += dt;
    if (b->t >= 27 / 30.0f) {
      b->alive = false;
      if (b->hide) b->hide->hidden = 1;
    }
  }
}

/* ---- drawing ---- */

static inline float scr_x(float x) { return (x - cam.px) * cam.scale + SCREEN_W / 2 + cam.shx; }
static inline float scr_y(float y) { return (y - cam.py) * cam.scale + SCREEN_H / 2 + cam.shy; }

void fx_draw_back(void) {
  float k = cam.scale / 0.5f;
  for (int i = 0; i < NPART; i++) {
    Part *p = &parts[i];
    if (!p->alive || p->kind == PT_GLASS || p->kind == PT_FIRE) continue;
    float y = scr_y(p->y);
    if (!gfx_rows(y - 10, y + 10)) continue;
    int frame = (int)(p->t * 30);
    int base = p->kind == PT_SMOKE ? PIC_SMOKE_PART_ANIM : PIC_GROUND_PART_ANIM;
    int n = p->kind == PT_SMOKE ? 15 : 26;
    if (frame >= n) frame = n - 1;
    gfx_pic(base + frame, scr_x(p->x), y, k, k, 0, 256);
  }
}

/* the flaming rider's fire: behind everything in the objects' layer */
void fx_draw_fire(void) {
  float k = cam.scale / 0.5f;
  for (int i = 0; i < NPART; i++) {
    Part *p = &parts[i];
    if (!p->alive || p->kind != PT_FIRE) continue;
    int frame = (int)(p->t * 20);
    if (frame > 10) frame = 10;
    gfx_pic(PIC_FIRE + frame, scr_x(p->x), scr_y(p->y), k * p->scale, k * p->scale, p->rot, 256);
  }
}

void fx_draw_front(void) {
  float k = cam.scale / 0.5f;
  for (int i = 0; i < NPART; i++) {
    Part *p = &parts[i];
    if (!p->alive) continue;
    if (p->kind == PT_GLASS) {
      gfx_pic(PIC_GLASSCRASHED0_0 + p->pic, scr_x(p->x), scr_y(p->y), k * p->scale, k * p->scale, p->rot, 256);
    }
  }
  for (int i = 0; i < 4; i++) {
    Boom *b = &booms[i];
    if (!b->alive) continue;
    int frame = (int)(b->t * 30);
    if (frame > 26) frame = 26;
    gfx_pic(PIC_CAREXPLODE + frame, scr_x(b->x), scr_y(b->y), 2 * k, 2 * k, 0, 256);
  }
}
