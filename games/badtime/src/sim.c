/* Bad Time Simulator: the fight's rules (bt.h), tick by tick: the attack scripts, the zone, the heart, the bones,
 * blasters and platforms, the damage and KR. The numbers are jcw87's (Bad Time Simulator's event sheets), in
 * the original's 640 x 480 pixels and seconds, at 30 ticks a second. */
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "bt.h"

void (*sim_log)(const char *line);

/* ---------------------------------------------------------------- sums that come out the same everywhere */
#define PI 3.14159265358979323846
#define D2R (PI / 180.0)

double bt_floor(double x) {
  if (!(x > -4.0e15 && x < 4.0e15)) return x != x ? 0.0 : x;   /* (NaN: 0; huge: already whole) */
  long long i = (long long)x;
  double f = (double)i;
  return f > x ? f - 1.0 : f;
}

/* sin of d degrees: the angle brought to [0, 90], then a polynomial (the same steps as tools/attacks.py) */
double bt_sin_deg(double d) {
  d = d - 360.0 * bt_floor(d / 360.0);
  if (d == 0.0 || d == 180.0) return 0.0;
  if (d == 90.0) return 1.0;
  if (d == 270.0) return -1.0;
  double s = 1.0;
  if (d >= 180.0) d = d - 180.0, s = -1.0;
  if (d > 90.0) d = 180.0 - d;
  double x = d * D2R, x2 = x * x;
  double p = -2.5052108385441720e-08;
  p = p * x2 + 2.7557319223985893e-06;
  p = p * x2 - 1.9841269841269841e-04;
  p = p * x2 + 8.3333333333333333e-03;
  p = p * x2 - 1.6666666666666666e-01;
  p = p * x2 + 1.0;
  return s * (x * p);
}
double bt_cos_deg(double d) { return bt_sin_deg(d + 90.0); }

static double atan_unit(double t) {
  int big = t > 0.4142135623730950;
  if (big) t = (t - 1.0) / (t + 1.0);
  double t2 = t * t;
  double p = 0.0769230769230769;
  p = p * t2 - 0.0833333333333333;
  p = p * t2 + 0.0909090909090909;
  p = p * t2 - 0.1;
  p = p * t2 + 0.1111111111111111;
  p = p * t2 - 0.1428571428571428;
  p = p * t2 + 0.2;
  p = p * t2 - 0.3333333333333333;
  double r = t + t * t2 * p;
  if (big) r = r + 0.7853981633974483;
  return r;
}
double bt_atan2_deg(double y, double x) {
  if (x == 0.0 && y == 0.0) return 0.0;
  double ax = x < 0 ? -x : x, ay = y < 0 ? -y : y, a;
  if (ay <= ax) a = atan_unit(ay / ax);
  else a = 1.5707963267948966 - atan_unit(ax / ay);
  if (x < 0.0) a = 3.141592653589793 - a;
  if (y < 0.0) a = -a;
  return a / D2R;
}

/* the same in floats, for the look and the moves (a turn of 360) */
float bt_sinf_deg(float d) {
  d = d - 360.0f * (float)bt_ifloor(d / 360.0f);
  float s = 1.0f;
  if (d >= 180.0f) d -= 180.0f, s = -1.0f;
  if (d > 90.0f) d = 180.0f - d;
  float x = d * (float)D2R, x2 = x * x;
  float p = 2.7557319e-06f;
  p = p * x2 - 1.9841270e-04f;
  p = p * x2 + 8.3333333e-03f;
  p = p * x2 - 1.6666667e-01f;
  p = p * x2 + 1.0f;
  return s * x * p;
}
static float cosf_deg(float d) { return bt_sinf_deg(d + 90.0f); }
static float absf(float x) { return x < 0 ? -x : x; }
static float minf(float a, float b) { return a < b ? a : b; }

uint32_t bt_rand(uint32_t *s) {
  uint32_t x = *s ? *s : 1;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  return *s = x;
}
uint32_t bt_below(uint32_t *s, uint32_t n) { return n ? (uint32_t)(((uint64_t)bt_rand(s) * n) >> 32) : 0; }

/* ---------------------------------------------------------------- the log, while checking */
static const signed char DX[4] = {1, 0, -1, 0}, DY[4] = {0, 1, 0, -1};

static void logf_(const world_t *w, const char *op, int n, const int *v) {
  if (!w->checking || !sim_log) return;
  char line[160];
  int p = snprintf(line, sizeof line, "%u %s", (unsigned)w->tick, op);
  for (int i = 0; i < n && p < (int)sizeof line - 12; i++) p += snprintf(line + p, sizeof line - (size_t)p, " %d", v[i]);
  sim_log(line);
}
static int ims(double v) { return (int)bt_floor(v * 1000.0 + 0.5); }
static int ifl(double v) {
  v = bt_floor(v);
  return v > 2.0e9 ? 2000000000 : v < -2.0e9 ? -2000000000 : (int)v;
}

/* ---------------------------------------------------------------- the zone */
#define MENU_L 33.0f
#define MENU_T 251.0f
#define MENU_R 608.0f
#define MENU_B 391.0f

void sim_zone_to(world_t *w, float l, float t, float r, float b, bool finish) {
  w->tl = l, w->tt = t, w->tr = r, w->tb = b;
  w->sc.finish = finish;
  w->zone_hidden = 0;
}
bool sim_zone_there(const world_t *w) { return w->zl == w->tl && w->zt == w->tt && w->zr == w->tr && w->zb == w->tb; }

static float step_to(float v, float to, float step) {
  float d = to - v;
  if (d > 0) return v + minf(step, d);
  if (d < 0) return v - minf(step, -d);
  return v;
}

/* the heart pushed back into the zone */
static void heart_clamp(world_t *w) {
  if (!w->heart_on) return;
  if (w->hx + 8 <= w->zl || w->hx - 8 >= w->zr || w->hy + 8 <= w->zt || w->hy - 8 >= w->zb) return;
  if (w->zl + 5 > w->hx - 8) w->hx = w->zl + 13;
  if (w->zt + 5 > w->hy - 8) w->hy = w->zt + 13;
  if (w->zr - 5 < w->hx + 8) w->hx = w->zr - 13;
  if (w->zb - 5 < w->hy + 8) w->hy = w->zb - 13;
}

static void zone_tick(world_t *w) {
  float s = w->zone_speed / 30.0f;
  w->zl = step_to(w->zl, w->tl, s);
  w->zt = step_to(w->zt, w->tt, s);
  w->zr = step_to(w->zr, w->tr, s);
  w->zb = step_to(w->zb, w->tb, s);
  if (w->sc.finish && sim_zone_there(w)) {
    w->sc.finish = 0;
    w->sc.running = 1;
  }
  if (w->sc.talk && w->checking) {   /* (checking: said and read at once) */
    w->sc.talk = 0;
    w->sc.running = 1;
  }
  heart_clamp(w);
}

/* ---------------------------------------------------------------- things */
void sim_clear_attacks(world_t *w) { w->nbones = w->nstabs = w->nblasters = w->nplats = 0; }

void sim_shake(world_t *w, int n) {
  w->shake = (int16_t)n;
  w->shake_t = 0;
}

static void add_bone(world_t *w, bool vertical, int x, int y, int len, int dir, int speed, int color) {
  int v[6] = {x, y, len, dir, speed, color};
  logf_(w, vertical ? "BoneV" : "BoneH", 6, v);
  if (w->nbones >= MAX_BONES) return;
  bone_t *b = &w->bones[w->nbones++];
  memset(b, 0, sizeof *b);
  b->x = (float)x, b->y = (float)y;
  b->w = vertical ? 10.0f : (float)len;
  b->h = vertical ? (float)len : 10.0f;
  b->dir = (uint8_t)(dir & 3);
  b->vx = (float)(DX[dir & 3] * speed) / 30.0f;
  b->vy = (float)(DY[dir & 3] * speed) / 30.0f;
  b->color = (uint8_t)(color >= 0 && color <= 2 ? color : 0);
  b->karma = 6;
  b->vertical = vertical;
}

static void add_plat(world_t *w, int x, int y, int width, int dir, int speed, int reverse) {
  int v[6] = {x, y, width, dir, speed, reverse};
  logf_(w, "Platform", 6, v);
  if (w->nplats >= MAX_PLATS) return;
  plat_t *p = &w->plats[w->nplats++];
  memset(p, 0, sizeof *p);
  p->x = (float)x, p->y = (float)y, p->w = (float)width;
  p->dir = (uint8_t)(dir & 3);
  p->vx = (float)(DX[dir & 3] * speed) / 30.0f;
  p->reverse = reverse > 0;
}

static void add_stab(world_t *w, int dir, int distance, double warn, double stay) {
  int v[4] = {dir, distance, ims(warn), ims(stay)};
  logf_(w, "BoneStab", 4, v);
  if (dir < 0 || dir > 3 || w->nstabs >= MAX_STABS) return;
  stab_t *s = &w->stabs[w->nstabs++];
  memset(s, 0, sizeof *s);
  float d = (float)distance, zw = w->zr - w->zl, zh = w->zb - w->zt;
  s->dir = (uint8_t)dir, s->distance = d, s->karma = 6, s->state = STAB_WARN;
  s->warn = (int16_t)ifl(warn * 30.0 + 0.5);
  s->stay = (int16_t)ifl(stay * 30.0 + 0.5);
  if (dir == DIR_RIGHT) s->w = d - 3, s->h = zh - 16, s->x = w->zr - s->w - 8, s->y = w->zt + 8;
  else if (dir == DIR_DOWN) s->w = zw - 16, s->h = d - 3, s->x = w->zl + 8, s->y = w->zb - s->h - 8;
  else if (dir == DIR_LEFT) s->w = d - 3, s->h = zh - 16, s->x = w->zl + 8, s->y = w->zt + 8;
  else s->w = zw - 16, s->h = d - 3, s->x = w->zl + 8, s->y = w->zt + 8;
}

/* the warning's over: the wall of bones, just out of sight, slides in */
static void stab_go(world_t *w, stab_t *s) {
  float d = s->distance, zw = w->zr - w->zl, zh = w->zb - w->zt;
  if (s->dir == DIR_DOWN || s->dir == DIR_UP) {
    s->x = w->zl, s->w = zw, s->h = d + 8, s->dest_x = s->x;
    if (s->dir == DIR_DOWN) s->y = w->zb - 5, s->dest_y = w->zb - 5 - d;
    else s->y = w->zt + 5 - s->h, s->dest_y = s->y + d;
  } else {
    s->y = w->zt, s->h = zh, s->w = d + 8, s->dest_y = s->y;
    if (s->dir == DIR_RIGHT) s->x = w->zr - 5, s->dest_x = w->zr - 5 - d;
    else s->x = w->zl + 5 - s->w, s->dest_x = s->x + d;
  }
  s->speed = d * 10.0f / 30.0f;
  s->state = STAB_IN;
}

static void add_blaster(world_t *w, int size, int x, int y, int ex, int ey, int ea, double spin, double blast) {
  int v[8] = {size, x, y, ex, ey, ea, ims(spin), ims(blast)};
  logf_(w, "GasterBlaster", 8, v);
  if (w->nblasters >= MAX_BLASTERS) return;
  blaster_t *b = &w->blasters[w->nblasters++];
  memset(b, 0, sizeof *b);
  b->x = (float)x, b->y = (float)y, b->end_x = (float)ex, b->end_y = (float)ey;
  b->ang = 90.0f, b->end_ang = (float)ea;
  b->size = (uint8_t)(size < 0 ? 0 : size > 2 ? 2 : size);
  b->sx = b->size == 0 ? 1.0f : (float)(b->size + 1);   /* across */
  b->sy = b->size == 0 ? 2.0f : (float)(b->size + 1);   /* along */
  b->scale = b->sx;
  b->timer = (int16_t)ifl(spin * 30.0 + 0.5);
  b->blast = (int16_t)ifl(blast * 30.0 + 0.5);
  b->karma = 10, b->opacity = 100;
  if (b->x == b->end_x && b->y == b->end_y) b->ang = b->end_ang;
}

void blaster_beam(const blaster_t *b, float from, float to, float half, float qx[4], float qy[4]) {
  float c = cosf_deg(b->ang), s = bt_sinf_deg(b->ang);
  float ax = b->x + c * from, ay = b->y + s * from, bx = b->x + c * to, by = b->y + s * to;
  qx[0] = ax - s * half, qy[0] = ay + c * half;
  qx[1] = bx - s * half, qy[1] = by + c * half;
  qx[2] = bx + s * half, qy[2] = by - c * half;
  qx[3] = ax + s * half, qy[3] = ay - c * half;
}

void sim_menu_bones(world_t *w, bool left, bool bottom) {
  /* the left one slides back out first; the bottom ones stop coming */
  for (int i = 0; i < w->nmbones; i++)
    if (w->mbones[i].kind == MB_LEFT && !left) w->mbones[i].state = 1;
  if (left) {
    bool have = false;
    for (int i = 0; i < w->nmbones; i++) have |= w->mbones[i].kind == MB_LEFT && !w->mbones[i].state;
    if (!have && w->nmbones < MAX_MBONES) {
      mbone_t *m = &w->mbones[w->nmbones++];
      memset(m, 0, sizeof *m);
      m->kind = MB_LEFT, m->x = -10, m->y = 270;
    }
  }
  w->menu_bottom = bottom;
  if (bottom) w->bottom_t = 0, w->bottom_alt = 0;
}

/* the buttons' edges (FIGHT, ACT, ITEM, MERCY) */
static const float BUTTON_X[4] = {32, 184, 344, 496};
#define BUTTON_W 110.0f

static void mbones_tick(world_t *w) {
  if (w->menu_bottom) {
    w->bottom_t += 1.0f / 30.0f;
    if (w->bottom_t >= 0.6f) {
      w->bottom_t -= 0.6f;
      for (int k = 0; k < 2 && w->nmbones < MAX_MBONES; k++) {
        mbone_t *m = &w->mbones[w->nmbones++];
        memset(m, 0, sizeof *m);
        m->kind = MB_BOTTOM, m->button = (uint8_t)(k * 2 + w->bottom_alt);
        m->x = BUTTON_X[m->button] + BUTTON_W, m->y = LAYOUT_H;
      }
      w->bottom_alt ^= 1;
    }
  }
  int n = 0;
  for (int i = 0; i < w->nmbones; i++) {
    mbone_t m = w->mbones[i];
    bool keep = true;
    if (m.kind == MB_LEFT) {
      m.t += 1.0f / 30.0f;
      m.x = -30.0f + absf(bt_sinf_deg(600.0f * m.t / (float)PI)) * 105.0f;
      if (m.x > 64.0f) m.t -= 0.72f / 30.0f;
      if (m.state && m.x <= -8.0f) keep = false;
    } else {
      if (m.state == 0) {
        m.y -= 10.0f;
        if (m.y <= 440.0f) m.y = 440.0f, m.state = 1;
      } else if (m.state == 1) {
        m.x -= 5.0f;
        if (m.x <= BUTTON_X[m.button] - 14.0f) m.x = BUTTON_X[m.button] - 14.0f, m.state = 2;
      } else {
        m.y += 10.0f;
        if (m.y > LAYOUT_H) keep = false;
      }
    }
    if (keep) w->mbones[n++] = m;
  }
  w->nmbones = (uint16_t)n;
}

static void bones_tick(world_t *w) {
  int n = 0;
  for (int i = 0; i < w->nbones; i++) {
    bone_t b = w->bones[i];
    b.x += b.vx, b.y += b.vy;
    bool gone = (b.dir == DIR_RIGHT && b.x > LAYOUT_W) || (b.dir == DIR_DOWN && b.y > LAYOUT_H) ||
                (b.dir == DIR_LEFT && b.x < -b.w) || (b.dir == DIR_UP && b.y < -b.h);
    if (!gone) w->bones[n++] = b;
  }
  w->nbones = (uint16_t)n;
}

static bool outside_layout(float x, float y, float rw, float rh) {
  return x + rw < 0 || y + rh < 0 || x > LAYOUT_W || y > LAYOUT_H;
}

static void stabs_tick(world_t *w) {
  int n = 0;
  for (int i = 0; i < w->nstabs; i++) {
    stab_t s = w->stabs[i];
    bool keep = true;
    if (s.state == STAB_WARN) {
      if (s.warn <= 0) stab_go(w, &s);
      else s.warn--;
    } else if (s.state == STAB_IN || s.state == STAB_STAY) {
      if (s.state == STAB_IN) {
        float px = s.x - DX[s.dir] * s.speed, py = s.y - DY[s.dir] * s.speed;
        /* past where it stops: there */
        bool past = (DX[s.dir] < 0 && px >= s.dest_x) || (DX[s.dir] > 0 && px <= s.dest_x) ||
                    (DY[s.dir] < 0 && py >= s.dest_y) || (DY[s.dir] > 0 && py <= s.dest_y);
        if (past) px = s.dest_x, py = s.dest_y, s.state = STAB_STAY;
        s.x = px, s.y = py;
      }
      if (s.state == STAB_STAY) {
        if (s.stay > 0) s.stay--;
        if (s.stay <= 0) s.state = STAB_OUT;
      }
    } else {
      s.x += DX[s.dir] * s.speed, s.y += DY[s.dir] * s.speed;
      if (outside_layout(s.x, s.y, s.w, s.h)) keep = false;
    }
    if (keep) w->stabs[n++] = s;
  }
  w->nstabs = (uint16_t)n;
}

static void blasters_tick(world_t *w) {
  int n = 0;
  for (int i = 0; i < w->nblasters; i++) {
    blaster_t b = w->blasters[i];
    bool keep = true;
    if (b.timer > 0 && (b.state == BL_WAIT || b.state == BL_FIRE)) b.timer--;
    if (b.state == BL_ENTER) {
      if (absf(b.x - b.end_x) >= 3) b.x += (b.end_x - b.x) / 3.0f;
      if (absf(b.x - b.end_x) < 3) b.x = b.end_x;
      if (absf(b.y - b.end_y) >= 3) b.y += (b.end_y - b.y) / 3.0f;
      if (absf(b.y - b.end_y) < 3) b.y = b.end_y;
      if (absf(b.ang - b.end_ang) >= 3) b.ang += (b.end_ang - b.ang) / 3.0f;
      if (absf(b.ang - b.end_ang) < 3) b.ang = b.end_ang;
      if (b.x == b.end_x && b.y == b.end_y && b.ang == b.end_ang) b.state = BL_WAIT;
    }
    if (b.state == BL_WAIT && b.timer == 0) b.state = BL_FIRE, b.timer = 3;
    else if (b.state == BL_FIRE && b.timer == 0) {
      b.state = BL_LEAVE;
      b.hurts = 1;
      if (b.size >= 1) sim_shake(w, 5);
    }
    if (b.state == BL_LEAVE) {
      b.leave += 30.0f;
      float r = 28.5f * (b.sy > b.sx ? b.sy : b.sx);
      if (outside_layout(b.x - r, b.y - r, r * 2, r * 2)) b.leave = 0;
      b.x -= cosf_deg(b.ang) * b.leave / 30.0f;
      b.y -= bt_sinf_deg(b.ang) * b.leave / 30.0f;
      /* the beam */
      b.beam_t++;
      float full = 35.0f * b.scale;
      if (b.beam_t < 4) b.base += (float)(int)(full / 4.0f);
      else if (b.beam_t == 4) b.base = full;
      if (b.beam_t > 5 + b.blast) {
        b.base *= 0.8f;
        int op = 100 - ((b.beam_t - b.blast) - 5) * 10;
        b.opacity = (uint8_t)(op < 0 ? 0 : op);
        if (b.base <= 2.0f) keep = false;
      }
      if (b.opacity <= 80) b.hurts = 0;
      b.sine = bt_sinf_deg((float)b.beam_t * (2.0f / 3.0f) * (float)(180.0 / PI)) * b.base / 4.0f;
    }
    if (keep) w->blasters[n++] = b;
  }
  w->nblasters = (uint16_t)n;
}

static void plats_tick(world_t *w) {
  int n = 0;
  for (int i = 0; i < w->nplats; i++) {
    plat_t p = w->plats[i];
    p.x += p.vx;
    if (p.reverse) {
      if (p.dir == DIR_RIGHT && p.x + p.w >= w->zr) p.dir = DIR_LEFT, p.vx = -p.vx;
      else if (p.dir == DIR_LEFT && p.x <= w->zl) p.dir = DIR_RIGHT, p.vx = -p.vx;
    }
    bool gone = (p.vx > 0 && p.x > LAYOUT_W + 200) || (p.vx < 0 && p.x + p.w < -200);
    if (!gone) w->plats[n++] = p;
  }
  w->nplats = (uint16_t)n;
}

/* ---------------------------------------------------------------- the heart */
void sim_heart_mode(world_t *w, int mode) {
  w->heart_mode = (uint8_t)(mode == HEART_BLUE);
  w->heart_dir = DIR_DOWN;
}

/* a 16 x 16 heart at (x, y) against the zone's border */
static bool in_wall(const world_t *w, float x, float y) {
  return x - 8 < w->zl + 5 || x + 8 > w->zr - 5 || y - 8 < w->zt + 5 || y + 8 > w->zb - 5;
}
/* ... and against a platform it's falling onto (gravity up or down only: they're jump-through) */
static const plat_t *on_plat(const world_t *w, float x, float y, float vy) {
  if (w->heart_mode != HEART_BLUE || (w->heart_dir != DIR_DOWN && w->heart_dir != DIR_UP)) return NULL;
  for (int i = 0; i < w->nplats; i++) {
    const plat_t *p = &w->plats[i];
    if (x + 8 <= p->x || x - 8 >= p->x + p->w || y + 8 <= p->y || y - 8 >= p->y + 7) continue;
    if (w->heart_dir == DIR_DOWN && p->y > y && vy >= 0 && y + 8 <= p->y + 2) return p;
    if (w->heart_dir == DIR_UP && p->y < y && vy <= 0 && y - 8 >= p->y + 7 - 2) return p;
  }
  return NULL;
}
static bool solid(const world_t *w, float x, float y, float vy) { return in_wall(w, x, y) || on_plat(w, x, y, vy); }

/* the heart lands: a slam's end */
static void landed(world_t *w, float speed) {
  if (!w->slammed) return;
  w->slammed = 0;
  if (absf(speed) >= 330.0f) {
    sim_shake(w, (int)(absf(speed) / 90.0f));
    if (w->slam_damage && w->hp > 1) w->hp--;
  }
}

/* moves along one axis, stopping flush against the zone's border or on a platform it lands on */
static void move_axis(world_t *w, bool vertical) {
  float v = vertical ? w->hvy : w->hvx, d = v / 30.0f;
  if (d == 0) return;
  float p0 = vertical ? w->hy : w->hx, p1 = p0 + d;
  float lo = vertical ? w->zt + 13 : w->zl + 13, hi = vertical ? w->zb - 13 : w->zr - 13;
  bool hit = false;
  if (d > 0 && p0 <= hi && p1 > hi) p1 = hi, hit = true;
  if (d < 0 && p0 >= lo && p1 < lo) p1 = lo, hit = true;
  /* platforms: landed on from the side gravity pulls to (they're jump-through) */
  if (vertical && w->heart_mode == HEART_BLUE && (w->heart_dir == DIR_DOWN || w->heart_dir == DIR_UP)) {
    bool down = w->heart_dir == DIR_DOWN;
    for (int i = 0; i < w->nplats; i++) {
      const plat_t *p = &w->plats[i];
      if (w->hx + 8 <= p->x || w->hx - 8 >= p->x + p->w) continue;
      if (down && d > 0 && p0 + 8 <= p->y + 2 && p1 + 8 > p->y && p->y - 8.05f < p1) p1 = p->y - 8.05f, hit = true;
      if (!down && d < 0 && p0 - 8 >= p->y + 5 && p1 - 8 < p->y + 7 && p->y + 15.05f > p1) p1 = p->y + 15.05f, hit = true;
    }
  }
  if (vertical) w->hy = p1;
  else w->hx = p1;
  if (hit) {
    landed(w, v);
    if (vertical) w->hvy = 0;
    else w->hvx = 0;
  }
}

static void heart_tick(world_t *w, uint32_t keys, uint32_t pressed) {
  if (!w->heart_on || !w->heart_free) return;
  float speed = keys & BK_SLOW ? 75.0f : 150.0f;
  bool l = keys & BK_LEFT, r = keys & BK_RIGHT, u = keys & BK_UP, dn = keys & BK_DOWN;
  if (w->heart_mode == HEART_RED) {
    w->hvx = l == r ? 0 : l ? -speed : speed;
    w->hvy = u == dn ? 0 : u ? -speed : speed;
  } else {
    int g = w->heart_dir;
    float gx = (float)DX[g], gy = (float)DY[g];
    /* the jump key is the one against gravity */
    static const uint32_t JUMP[4] = {BK_LEFT, BK_UP, BK_RIGHT, BK_DOWN};
    uint32_t jk = JUMP[g];
    if ((pressed & jk) && solid(w, w->hx + gx, w->hy + gy, w->hvy)) w->hvx -= gx * 180.0f, w->hvy -= gy * 180.0f;
    if (!(keys & jk) && w->jump_held) {
      float along = w->hvx * gx + w->hvy * gy;
      if (along < -30.0f) {
        if (gx != 0) w->hvx = -30.0f * gx;
        else w->hvy = -30.0f * gy;
      }
    }
    w->jump_held = (keys & jk) != 0;
    float down = w->hvx * gx + w->hvy * gy, grav = 0;
    if (down < 240 && down > 15) grav = 540;
    else if (down <= 15 && down > -30) grav = 180;
    else if (down <= -30 && down > -120) grav = 450;
    else if (down <= -120) grav = 180;
    if (!solid(w, w->hx + gx * 0.2f, w->hy + gy * 0.2f, w->hvy)) {
      w->hvx += gx * grav / 30.0f, w->hvy += gy * grav / 30.0f;
      float m = w->max_fall;
      if (g == DIR_RIGHT && w->hvx > m) w->hvx = m;
      if (g == DIR_DOWN && w->hvy > m) w->hvy = m;
      if (g == DIR_LEFT && w->hvx < -m) w->hvx = -m;
      if (g == DIR_UP && w->hvy < -m) w->hvy = -m;
    }
    if (g == DIR_RIGHT || g == DIR_LEFT) {
      w->hvy = 0;
      if (u != dn) w->hvy += u ? -speed : speed;
    } else {
      w->hvx = 0;
      const plat_t *p = on_plat(w, w->hx + gx * 0.5f, w->hy + gy * 0.5f, w->hvy);
      if (p) {
        w->hvx = p->vx * 30.0f, w->hvy = 0;
        w->hy = g == DIR_DOWN ? p->y - 8.05f : p->y + 7 + 8.05f;
      }
      if (l != r) w->hvx += l ? -speed : speed;
    }
  }
  move_axis(w, false);
  move_axis(w, true);
  w->moving = w->hvx != 0 || w->hvy != 0;
}

bool sim_heart_in(const world_t *w, float x, float y, float rw, float rh) {
  return w->hx >= x && w->hx <= x + rw && w->hy >= y && w->hy <= y + rh;
}
/* the heart's centre inside the convex quad (corners in order) */
static bool heart_in_quad(const world_t *w, const float qx[4], const float qy[4]) {
  int pos = 0, neg = 0;
  for (int i = 0; i < 4; i++) {
    int j = (i + 1) & 3;
    float c = (qx[j] - qx[i]) * (w->hy - qy[i]) - (qy[j] - qy[i]) * (w->hx - qx[i]);
    if (c > 0) pos++;
    else if (c < 0) neg++;
  }
  return !(pos && neg);
}

/* ---------------------------------------------------------------- damage and KR */
static bool hurts(const world_t *w, int color) {
  return color == COL_WHITE || (color == COL_BLUE && w->moving) || (color == COL_ORANGE && !w->moving);
}

static void damage_tick(world_t *w) {
  if (w->hp <= 0) return;
  int karma = -1;
  bool floor1 = false;
  for (int i = 0; i < w->nbones && w->heart_on; i++) {
    bone_t *b = &w->bones[i];
    if (sim_heart_in(w, b->x, b->y, b->w, b->h) && hurts(w, b->color)) {
      if (karma < 0) karma = b->karma;
      if (b->karma >= 3) b->karma = 2;
    }
  }
  for (int i = 0; i < w->nstabs && w->heart_on; i++) {
    stab_t *s = &w->stabs[i];
    if (s->state != STAB_WARN && sim_heart_in(w, s->x, s->y, s->w, s->h)) {
      if (karma < 0) karma = s->karma;
      if (s->karma >= 3) s->karma = 2;
    }
  }
  for (int i = 0; i < w->nblasters && w->heart_on; i++) {
    blaster_t *b = &w->blasters[i];
    if (!b->hurts) continue;
    float qx[4], qy[4], k = b->scale;
    blaster_beam(b, 35.0f * k, 35.0f * k + 1000.0f, b->base * 3.0f / 8.0f, qx, qy);
    if (heart_in_quad(w, qx, qy)) {
      if (karma < 0) karma = b->karma;
      if (b->karma >= 3) b->karma = 2;
    }
  }
  for (int i = 0; i < w->nmbones && w->heart_on; i++) {
    const mbone_t *m = &w->mbones[i];
    /* 14 x 44, its sides and ends rounded off */
    if (sim_heart_in(w, m->x + 2, m->y + 2, 10, 40)) {
      if (karma < 0) karma = 0;
      floor1 = true;
    }
  }
  if (karma >= 0) {
    w->hp--;
    w->kr = (int16_t)(w->kr + karma);
    w->hits++;
    if (floor1 && w->hp <= 0) w->hp = 1;
  }
  if (w->kr > 40) w->kr = 40;
  if (w->kr >= w->hp) w->kr = (int16_t)(w->hp - 1);
  if (w->kr < 0) w->kr = 0;
  if (w->kr > 0 && w->hp > 1) {
    w->kr_t++;
    int k = w->kr, t = w->kr_t;
    if ((k >= 40 && t >= 1) || (k >= 30 && t >= 2) || (k >= 20 && t >= 5) || (k >= 10 && t >= 15) || t >= 30)
      w->kr--, w->hp--, w->kr_t = 0;
  }
  if (w->hp > MAX_HP) w->hp = MAX_HP;
}

/* ---------------------------------------------------------------- the script */
static double arg(const world_t *w, uint16_t a) { return a & 0x8000 ? w->sc.vars[a & 0x7FFF] : atk_pool[a]; }

static int delay_ticks(double s) {
  double v = bt_floor(s * 30.0 + 0.5);
  return v > 0 ? (v > 100000 ? 100000 : (int)v) : 0;
}

void sim_resume(world_t *w) {
  w->sc.talk = 0;
  w->sc.running = 1;
}

static void set_var(world_t *w, double idx, double v) {
  int i = (int)idx;
  if (i >= 0 && i < MAX_VARS) w->sc.vars[i] = v;
}

/* one line of the script; returns the line to go on from (before the step to the next) */
static int exec(world_t *w, int line, int op, int n, const uint16_t *a) {
  double v[10] = {0};
  for (int i = 0; i < n && i < 10; i++) v[i] = arg(w, a[i]);
  int iv[10];
  for (int i = 0; i < 10; i++) iv[i] = ifl(v[i]);
  switch (op) {
    case OP_SET: set_var(w, v[0], v[1]); break;
    case OP_ADD: set_var(w, v[0], v[1] + v[2]); break;
    case OP_SUB: set_var(w, v[0], v[1] - v[2]); break;
    case OP_MUL: set_var(w, v[0], v[1] * v[2]); break;
    case OP_DIV: set_var(w, v[0], v[2] != 0.0 ? v[1] / v[2] : 0.0); break;
    case OP_MOD: {
      double q = v[2] != 0.0 ? v[1] / v[2] : 0.0;
      double t = q < 0 ? -bt_floor(-q) : bt_floor(q);   /* (fmod: towards zero) */
      set_var(w, v[0], v[2] != 0.0 ? v[1] - t * v[2] : 0.0);
      break;
    }
    case OP_FLOOR: set_var(w, v[0], bt_floor(v[1])); break;
    case OP_SIN: set_var(w, v[0], bt_sin_deg(v[1])); break;
    case OP_COS: set_var(w, v[0], bt_cos_deg(v[1])); break;
    case OP_DEG: set_var(w, v[0], v[1] / D2R); break;
    case OP_RAD: set_var(w, v[0], v[1] * D2R); break;
    case OP_ANGLE: set_var(w, v[0], bt_atan2_deg(v[4] - v[2], v[3] - v[1])); break;
    case OP_RND: set_var(w, v[0], (double)bt_below(&w->rng, iv[1] > 0 ? (uint32_t)iv[1] : 0)); break;
    case OP_JMPABS: return iv[0] - 1;
    case OP_JMPREL: return line + iv[0] - 1;
    case OP_JMPZ: return v[1] == 0.0 ? iv[0] - 1 : line;
    case OP_JMPNZ: return v[1] != 0.0 ? iv[0] - 1 : line;
    case OP_JMPE: return v[1] == v[2] ? iv[0] - 1 : line;
    case OP_JMPNE: return v[1] != v[2] ? iv[0] - 1 : line;
    case OP_JMPL: return v[1] < v[2] ? iv[0] - 1 : line;
    case OP_JMPNL: return v[1] >= v[2] ? iv[0] - 1 : line;
    case OP_JMPG: return v[1] > v[2] ? iv[0] - 1 : line;
    case OP_JMPNG: return v[1] <= v[2] ? iv[0] - 1 : line;
    case OP_TLPAUSE: w->sc.running = 0; break;
    case OP_ENDATTACK:
      logf_(w, "EndAttack", 0, iv);
      w->sc.ended = 1, w->sc.running = 0;
      break;
    case OP_BLACKSCREEN:
      logf_(w, "BlackScreen", 1, iv);
      w->black = iv[0] == 1;
      if (w->black) sim_clear_attacks(w);
      break;
    case OP_BONEH:
    case OP_BONEV: add_bone(w, op == OP_BONEV, iv[0], iv[1], iv[2], iv[3], iv[4], n > 5 ? iv[5] : 0); break;
    case OP_BONEHREPEAT:
    case OP_BONEVREPEAT:
    case OP_PLATFORMREPEAT: {
      int d = iv[3] & 3, cnt = iv[5], sp = iv[6];
      for (int i = 0; i < cnt && i < 200; i++) {
        double x = v[0] - (double)DX[d] * sp * i, y = v[1] - (double)DY[d] * sp * i;
        if (op == OP_PLATFORMREPEAT) add_plat(w, ifl(x), ifl(y), iv[2], iv[3], iv[4], 0);
        else add_bone(w, op == OP_BONEVREPEAT, ifl(x), ifl(y), iv[2], iv[3], iv[4], 0);
      }
      break;
    }
    case OP_SINEBONES: {
      int cnt = iv[0], sp = iv[1], spd = iv[2], h = iv[3];
      for (int i = 0; i < cnt && i < 100; i++) {
        double x;
        int d;
        if (sp > 0) x = (double)w->zr + sp * i, d = DIR_LEFT;
        else x = (double)w->zl + sp * i, d = DIR_RIGHT;
        int sine = ifl(bt_sin_deg((double)i / 3.0 / D2R) * 28.0);
        double y = (double)w->zt + 6;
        add_bone(w, true, ifl(x), ifl(y), h + sine, d, spd, 0);
        y = (double)w->zt + 6 + h + sine + 39;
        add_bone(w, true, ifl(x), ifl(y), ifl((double)w->zb - 5 - y), d, spd, 0);
      }
      break;
    }
    case OP_BONESTAB: add_stab(w, iv[0], iv[1], v[2], v[3]); break;
    case OP_GASTERBLASTER: add_blaster(w, iv[0], iv[1], iv[2], iv[3], iv[4], iv[5], v[6], v[7]); break;
    case OP_PLATFORM: add_plat(w, iv[0], iv[1], iv[2], iv[3], iv[4], n > 5 ? iv[5] : 0); break;
    case OP_HEARTMODE:
      logf_(w, "HeartMode", 1, iv);
      sim_heart_mode(w, iv[0]);
      break;
    case OP_HEARTTELEPORT:
      logf_(w, "HeartTeleport", 2, iv);
      w->hx = (float)iv[0], w->hy = (float)iv[1];
      w->heart_on = 1;
      break;
    case OP_HEARTMAXFALLSPEED:
      logf_(w, "HeartMaxFallSpeed", 1, iv);
      w->max_fall = (float)iv[0];
      break;
    case OP_SANSSLAM:
      logf_(w, "SansSlam", 1, iv);
      if (v[0] >= 0 && v[0] <= 3) {
        int d = iv[0] & 3;
        w->heart_mode = HEART_BLUE, w->heart_dir = (uint8_t)d, w->slammed = 1;
        w->hvx = DX[d] * w->max_fall, w->hvy = DY[d] * w->max_fall;
      }
      break;
    case OP_SANSSLAMDAMAGE:
      logf_(w, "SansSlamDamage", 1, iv);
      w->slam_damage = iv[0] != 0;
      break;
    case OP_COMBATZONESPEED:
      logf_(w, "CombatZoneSpeed", 1, iv);
      w->zone_speed = (float)iv[0];
      break;
    case OP_COMBATZONERESIZE: {
      int z[5] = {ims(v[0]), ims(v[1]), ims(v[2]), ims(v[3]), v[4] != 0.0};
      logf_(w, "CombatZoneResize", 5, z);
      sim_zone_to(w, (float)v[0], (float)v[1], (float)v[2], (float)v[3], v[4] != 0.0);
      break;
    }
    case OP_COMBATZONERESIZEINSTANT: {
      int z[4] = {ims(v[0]), ims(v[1]), ims(v[2]), ims(v[3])};
      logf_(w, "CombatZoneResizeInstant", 4, z);
      w->zl = w->tl = (float)v[0], w->zt = w->tt = (float)v[1], w->zr = w->tr = (float)v[2], w->zb = w->tb = (float)v[3];
      w->zone_hidden = 0;
      heart_clamp(w);
      break;
    }
    case OP_GETHEARTPOS:
      logf_(w, "GetHeartPos", 0, iv);
      set_var(w, v[0], w->checking ? 320.0 : (double)w->hx);
      set_var(w, v[1], w->checking ? 304.0 : (double)w->hy);
      break;
    case OP_SANSANIMATION:
      logf_(w, "SansAnimation", 1, iv);
      w->anim = (uint8_t)iv[0], w->body = BODY_NONE, w->anim_t = 0;
      break;
    case OP_SANSBODY:
      logf_(w, "SansBody", 1, iv);
      w->body = (uint8_t)(iv[0] & 3), w->anim = ANIM_NONE, w->anim_t = 0;
      break;
    case OP_SANSTORSO:
      logf_(w, "SansTorso", 1, iv);
      w->torso = (uint8_t)(iv[0] & 1), w->body = BODY_NONE;
      break;
    case OP_SANSHEAD:
      logf_(w, "SansHead", 1, iv);
      w->head = (uint8_t)(iv[0] & 7);
      break;
    case OP_SANSSWEAT:
      logf_(w, "SansSweat", 1, iv);
      w->sweat = (uint8_t)(iv[0] < 0 ? 0 : iv[0] > 3 ? 3 : iv[0]);
      break;
    case OP_SANSX:
      logf_(w, "SansX", 1, iv);
      w->ox = (float)iv[0];
      break;
    case OP_SANSREPEAT:
      logf_(w, "SansRepeat", 0, iv);
      w->ox_speed = -900.0f;
      break;
    case OP_SANSENDREPEAT:
      logf_(w, "SansEndRepeat", 0, iv);
      w->ox_speed = 0;
      break;
    case OP_SANSTEXT:
      logf_(w, "SansText", 1, iv);
      w->sc.running = 0, w->sc.talk = 1, w->sc.talk_line = (int16_t)iv[0];
      break;
    default: break;
  }
  return line;
}

static void script_tick(world_t *w) {
  script_t *s = &w->sc;
  if (s->attack < 0 || !s->running) return;
  const atk_info_t *in = &atk_info[s->attack];
  int guard = 0;
  while (s->running && !s->ended && s->line >= 1 && s->line <= in->lines) {
    const uint16_t *c = &atk_code[atk_line[in->first + s->line - 1]];
    int op = c[0] & 0xFF, n = c[0] >> 8;
    int d = delay_ticks(arg(w, c[1]));
    if (s->t < d) break;
    int to = exec(w, s->line, op, n, c + 2);
    s->t -= d;
    s->line = (uint16_t)(to + 1 < 0 ? 0 : to + 1);
    if (++guard >= 4000) {   /* a loop that never waits: the attack ends */
      s->ended = 1, s->running = 0;
      break;
    }
  }
  if (s->running) s->t++;
}

void sim_start_attack(world_t *w, int attack) {
  script_t *s = &w->sc;
  memset(s, 0, sizeof *s);
  s->attack = (int16_t)attack;
  s->talk_line = -1;
  s->vars[0] = PI;
  s->line = 1, s->running = 1;
  /* (ResetVars) */
  w->zone_speed = 480, w->max_fall = 750, w->slam_damage = 0;
  w->ox_speed = 0, w->ox = 320;
  w->hvx = w->hvy = 0, w->slammed = 0;
  w->heart_dir = DIR_DOWN;
  w->heart_free = 1;
  w->menu_left = 0;
  sim_menu_bones(w, false, false);
}

void sim_stop_attack(world_t *w) {
  w->heart_free = 0, w->hvx = w->hvy = 0, w->moving = 0;
  w->sc.attack = -1, w->sc.running = 0, w->sc.finish = 0, w->sc.talk = 0;
  sim_clear_attacks(w);
  w->black = 0;
  w->zone_speed = 480, w->max_fall = 750, w->slam_damage = 0;
  w->ox_speed = 0, w->ox = 320;
}

void sim_reset(world_t *w, uint32_t seed) {
  memset(w, 0, sizeof *w);
  w->zl = w->tl = 32, w->zt = w->tt = 240, w->zr = w->tr = 608, w->zb = w->tb = 384;
  w->zone_speed = 480;
  w->hx = 320, w->hy = 320;
  w->max_fall = 750;
  w->hp = MAX_HP;
  w->ox = 320;
  w->anim = ANIM_NONE, w->body = BODY_NONE, w->head = HEAD_CLOSED;
  w->sc.attack = -1, w->sc.talk_line = -1;
  w->rng = seed ? seed : 1;
  w->fx_rng = seed ^ 0xA5A5A5A5u;
  if (!w->fx_rng) w->fx_rng = 7;
}

void sim_tick(world_t *w, uint32_t keys, uint32_t pressed) {
  heart_tick(w, keys, pressed);
  script_tick(w);
  zone_tick(w);
  bones_tick(w);
  stabs_tick(w);
  blasters_tick(w);
  plats_tick(w);
  mbones_tick(w);
  damage_tick(w);
  /* the opponent */
  w->anim_t++;
  if (w->ox_speed != 0) {
    w->ox += w->ox_speed / 30.0f;
    w->ox_speed -= 1.5f;
    if (w->ox < -100) w->ox = 740;
  }
  if (w->shake > 0) {
    if (++w->shake_t >= 1) w->shake_t = 0, w->shake--;
  }
  w->tick++;
}
