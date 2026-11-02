/* A level being played (game.h): built from the level's bytes (tools/pack.py), stepped 60 times a second.
 *
 * As in the game (gamesrc spec): level parts of density 2.1 (heavy x3), restitution 0.35, friction 0.2; characters'
 * pieces 2.0, 0.35, 0.2; bullets circles of radius 2, density 0.1, restitution 1, no friction and no gravity, sent
 * by an impulse of 0.008 and kept 300 steps; grenades of density 0.8, restitution 0.4, friction 0.5, that go off
 * after 150 steps. Breakables (bo, br) break at the first bullet or a blow of 7.5 m/s; explosives (be) go off. */
#include <math.h>
#include <string.h>
#include "game.h"
#include "res.h"
#include "world.h"

Game g;
float mb_scratch[SCRATCH_FLOATS];
static uint8_t pending_blast[MAX_OBJ];   /* explosives set off, going off in a few steps */

#define PI 3.14159265f
#define T_LEVEL 1
#define T_CHAR 2
#define T_SHOT 3
#define T_SWITCH 4
#define CODE(t, i) ((t) << 8 | (i))
#define KIND(c) ((c) >> 8)
#define INDEX(c) ((c) & 255)

#define BULLET_R 2.0f
#define BULLET_SPEED (0.008f / (0.1f * PI * (BULLET_R / 60) * (BULLET_R / 60)) * 60)   /* units/s */
#define GRENADE_R 5.0f
#define FUSE 150
#define BLAST_R 160.0f
#define KILL_SPEED 180.0f      /* a blow that kills a standing character (units/s) */
#define BREAK_SPEED 450.0f     /* a blow that breaks a breakable (7.5 m/s) */
#define RAGDOLL_BYTES 9000     /* about what a ragdoll takes of Box2D's memory */

static const WFix FIX_LEVEL = {2.1f, 0.2f, 0.35f, WC_LEVEL, 0};
static const WFix FIX_STATIC = {2.1f, 0.2f, 0.35f, WC_STATIC, 0};
static const WFix FIX_CHAR = {2.0f, 0.2f, 0.35f, WC_CHARACTER, 0};
static const WFix FIX_BULLET = {0.1f, 0.0f, 1.0f, WC_BULLET, 0};
static const WFix FIX_GRENADE = {0.8f, 0.5f, 0.4f, WC_GRENADE, 0};
static const WFix FIX_SENSOR = {0.0f, 0.2f, 0.0f, WC_SENSOR, 1};

/* ------------------------------------------------------------------ reading the level */
static const uint8_t *rp;
static int rd8(void) { return *rp++; }
static int rds8(void) { return (int8_t)*rp++; }
static int rd16(void) { int v = rp[0] | rp[1] << 8; rp += 2; return v; }
static float rdc(void) { int16_t v = (int16_t)(rp[0] | rp[1] << 8); rp += 2; return v * 0.125f; }
static uint32_t rd32(void) { uint32_t v = rp[0] | rp[1] << 8 | rp[2] << 16 | (uint32_t)rp[3] << 24; rp += 4; return v; }
static float rdf(void) { union { uint32_t u; float f; } x; x.u = rd32(); return x.f; }

static Obj *obj_by_idx(int idx) {
  for (int i = 0; i < g.nobj; i++)
    if (g.obj[i].idx == idx && g.obj[i].kind != 255) return &g.obj[i];
  return 0;
}

int game_mode_of(int level) {
  for (int m = MODE_COUNT - 1; m >= 0; m--)
    if (level >= res_mode_first(m)) return m;
  return 0;
}

/* ------------------------------------------------------------------ characters */
/* a piece: its centre (x right as the character faces, y up from the feet, in 80ths of its height) and size */
static const float PART[P_COUNT][4] = {
  {0, 43.7f, 28.8f, 31.4f},   /* torso */
  {0, 69.7f, 18.7f, 20.6f},   /* head */
  {17, 47.4f, 7.2f, 16},      /* front arm */
  {17, 31.4f, 7.2f, 16},
  {-17, 47.4f, 7.2f, 16},     /* back arm */
  {-17, 31.4f, 7.2f, 16},
  {9, 21, 7.2f, 14},          /* front leg */
  {9, 7, 7.2f, 14},
  {-9, 21, 7.2f, 14},
  {-9, 7, 7.2f, 14},
};
#define SHOULDER_Y 55.4f

void game_part_size(const Char *c, int part, float *w, float *h) {
  float k = c->h / 80;
  *w = PART[part][2] * k, *h = PART[part][3] * k;
}

/* the character's frame: feet, its right (as it faces) and up, from its standing body */
static void frame_of(const Char *c, float *fx, float *fy, float *rx, float *ry, float *ux, float *uy) {
  float s, cs;
  if (c->body >= 0 && world_alive(c->body)) world_xf(c->body, fx, fy, &s, &cs);
  else *fx = c->fx, *fy = c->fy, s = sinf(c->tilt), cs = cosf(c->tilt);
  float side = c->left ? -1.0f : 1.0f;
  *rx = cs * side, *ry = s * side;
  *ux = s, *uy = -cs;
}

/* a standing character's piece (and the shooter's arm along his aim) */
static void pose(const Char *c, int part, float *x, float *y, float *ang) {
  float fx, fy, rx, ry, ux, uy, k = c->h / 80;
  frame_of(c, &fx, &fy, &rx, &ry, &ux, &uy);
  float tilt = atan2f(ux, -uy);   /* (the frame's turn) */
  if (c->role == R_HERO && (part == P_ARM_F || part == P_FORE_F) && &g.ch[g.hero] == c) {
    float shx = fx + rx * 17 * k + ux * SHOULDER_Y * k, shy = fy + ry * 17 * k + uy * SHOULDER_Y * k;
    float d = (part == P_ARM_F ? 8 : 24) * k;
    *x = shx + cosf(g.aim) * d, *y = shy + sinf(g.aim) * d;
    *ang = g.aim - PI / 2;
    return;
  }
  float lx = PART[part][0] * k, ly = PART[part][1] * k;
  *x = fx + rx * lx + ux * ly, *y = fy + ry * lx + uy * ly;
  *ang = tilt;
}

bool game_part(const Char *c, int part, float *x, float *y, float *s, float *cs) {
  if (!c->alive && c->part[part] >= 0 && world_alive(c->part[part])) {
    world_xf(c->part[part], x, y, s, cs);
    return true;
  }
  if (!c->alive && (c->body < 0 || !world_alive(c->body))) return false;
  float a;
  pose(c, part, x, y, &a);
  *s = sinf(a), *cs = cosf(a);
  return true;
}

static bool make_standing(Char *c, int ci) {
  float k = c->h / 80;
  c->body = (int16_t)world_body(WB_DYNAMIC, c->fx, c->fy, c->tilt, CODE(T_CHAR, ci), false, true, 1.0f);
  if (c->body < 0) return false;
  return world_box(c->body, 0, -c->h / 2, 15 * k, c->h / 2, &FIX_CHAR);
}

/* the ragdoll, from where the standing character is */
static void fall(Char *c, int ci) {
  float vx = 0, vy = 0, x[P_COUNT], y[P_COUNT], a[P_COUNT];
  if (c->body >= 0 && world_alive(c->body)) world_velocity(c->body, &vx, &vy);
  for (int p = 0; p < P_COUNT; p++) pose(c, p, &x[p], &y[p], &a[p]);
  float fx, fy, rx, ry, ux, uy, k = c->h / 80;
  frame_of(c, &fx, &fy, &rx, &ry, &ux, &uy);
  c->alive = 0;
  if (b2_heap_limit && b2_heap_now + RAGDOLL_BYTES > b2_heap_limit && c->body >= 0) {
    /* no room for its ragdoll: it falls stiff */
    world_set_fixed_rotation(c->body, false);
    world_wake(c->body);
    return;
  }
  if (c->body >= 0) world_destroy(c->body);
  c->body = -1;
  for (int p = 0; p < P_COUNT; p++) {
    int b = world_body(WB_DYNAMIC, x[p], y[p], a[p], CODE(T_CHAR, ci), false, false, 1.0f);
    c->part[p] = (int16_t)b;
    if (b < 0) continue;
    world_box(b, 0, 0, PART[p][2] * k / 2, PART[p][3] * k / 2, &FIX_CHAR);
    world_set_velocity(b, vx, vy);
  }
  /* the joints: neck and hips +-0.9425, knees 0 to 2.5133 and elbows 0 to 1.885 (by the way it faces), shoulders
     free */
  float side = c->left ? -1.0f : 1.0f;
#define AT(lx, ly) fx + rx * (lx) * k + ux * (ly) * k, fy + ry * (lx) * k + uy * (ly) * k
  world_revolute(c->part[P_TORSO], c->part[P_HEAD], AT(0, 59.4f), true, -0.9425f, 0.9425f, false, 0, 0);
  for (int s = 0; s < 2; s++) {
    int arm = s ? P_ARM_B : P_ARM_F, fore = arm + 1, thigh = s ? P_THIGH_B : P_THIGH_F, shin = thigh + 1;
    float ax = s ? -17.0f : 17.0f, lx = s ? -9.0f : 9.0f;
    if (s == 0 && &g.ch[g.hero] == c) {
      /* the aiming arm: its shoulder and elbow along the aim */
      float shx, shy, mx, my;
      game_gun(&shx, &shy, &mx, &my);
      world_revolute(c->part[P_TORSO], c->part[arm], shx, shy, false, 0, 0, false, 0, 0);
      world_revolute(c->part[arm], c->part[fore], shx + cosf(g.aim) * 16 * k, shy + sinf(g.aim) * 16 * k, true,
                     side > 0 ? 0 : -1.885f, side > 0 ? 1.885f : 0, false, 0, 0);
    } else {
      world_revolute(c->part[P_TORSO], c->part[arm], AT(ax, SHOULDER_Y), false, 0, 0, false, 0, 0);
      world_revolute(c->part[arm], c->part[fore], AT(ax, 39.4f), true, side > 0 ? 0 : -1.885f,
                     side > 0 ? 1.885f : 0, false, 0, 0);
    }
    world_revolute(c->part[P_TORSO], c->part[thigh], AT(lx, 28), true, -0.9425f, 0.9425f, false, 0, 0);
    world_revolute(c->part[thigh], c->part[shin], AT(lx, 14), true, side > 0 ? -2.5133f : 0,
                   side > 0 ? 0 : 2.5133f, false, 0, 0);
  }
#undef AT
}

static void kill(int ci, float ix, float iy, float px, float py) {
  Char *c = &g.ch[ci];
  if (!c->alive) return;
  if (c->role == R_HERO && !c->hittable) return;
  fall(c, ci);
  /* the blow goes to the piece nearest it */
  int best = -1;
  float bd = 1e9f;
  for (int p = 0; p < P_COUNT; p++) {
    if (c->part[p] < 0) continue;
    float x, y, s, cs;
    world_xf(c->part[p], &x, &y, &s, &cs);
    float d = (x - px) * (x - px) + (y - py) * (y - py);
    if (d < bd) bd = d, best = p;
  }
  if (best >= 0 && (ix != 0 || iy != 0)) world_impulse(c->part[best], ix, iy, px, py);
  if (c->role == R_ENEMY) g.enemies_left--;
  else if (c->role == R_HOSTAGE) { if (!g.lose) g.lose = LOSE_HOSTAGE; }
  else if (!g.lose) g.lose = LOSE_SELF;
}

/* ------------------------------------------------------------------ building */
static bool build_poly(Obj *o, int oi, const uint8_t *parts, int nparts) {
  bool fixed = (o->flags & F_F) != 0;
  o->body = (int16_t)world_body(fixed ? WB_STATIC : WB_DYNAMIC, o->cx, o->cy, 0, CODE(T_LEVEL, oi), false, false,
                                1.0f);
  if (o->body < 0) return false;
  WFix f = fixed ? FIX_STATIC : FIX_LEVEL;
  if (o->flags & F_HV) f.density *= 3;
  const uint8_t *save = rp;
  if (fixed) {
    /* a static one: its outline and holes as loops of edges (its convex parts' boxes overlap too much: thousands
       of contacts with what lies on it) */
    float *xy = mb_scratch;
    rp = o->outline;
    int n = o->nout;
    for (int h = 0; h <= o->nholes; h++) {
      if (h) n = rd8();
      int m = n < SCRATCH_FLOATS / 2 ? n : SCRATCH_FLOATS / 2;
      for (int i = 0; i < n; i++) {
        float x = rdc(), y = rdc();
        if (i < m) xy[2 * i] = x, xy[2 * i + 1] = y;
      }
      world_chain(o->body, xy, m, &f);
    }
  } else {
    rp = parts;
    float xy[32];
    for (int k = 0; k < nparts; k++) {
      int n = rd8();
      for (int i = 0; i < n; i++) xy[2 * i] = rdc(), xy[2 * i + 1] = rdc();
      world_poly(o->body, xy, n, &f);
    }
  }
  rp = save;
  return true;
}

static bool build_ball(Obj *o, int oi) {
  bool fixed = (o->flags & F_F) != 0;
  o->body = (int16_t)world_body(fixed ? WB_STATIC : WB_DYNAMIC, o->cx + o->bx, o->cy + o->by, 0, CODE(T_LEVEL, oi),
                                false, false, 1.0f);
  if (o->body < 0) return false;
  WFix f = fixed ? FIX_STATIC : FIX_LEVEL;
  if (o->flags & F_HV) f.density *= 3;
  return world_circle(o->body, 0, 0, o->r, &f);
}

static const uint8_t *obj_parts[MAX_OBJ];
static uint8_t obj_nparts[MAX_OBJ];

static bool build_obj(int oi) {
  Obj *o = &g.obj[oi];
  if (o->kind == K_POLY) return build_poly(o, oi, obj_parts[oi], obj_nparts[oi]);
  if (o->kind == K_SEG && (o->flags & F_B)) return build_ball(o, oi);
  return true;
}

bool game_load(int level, uint8_t *buf) {
  game_end();
  memset(&g, 0, sizeof g);
  memset(pending_blast, 0, sizeof pending_blast);
  g.level = level;
  g.mode = game_mode_of(level);
  g.hero = -1;
  int size = res_level(level, buf);
  if (!size) return false;
  if (!world_begin()) return false;
  rp = buf;
  g.tr = rd8();
  int nobj = rd8(), npin = rd8(), nsw = rd8();
  g.ammo = g.tr + 3;
  for (int i = 0; i < MAX_SHOT; i++) g.shot[i].body = -1;
  for (int i = 0; i < nobj; i++) {
    int kind = rd8(), idx = rd8();
    uint32_t flags = rd32();
    float cx = rdc(), cy = rdc();
    if (kind == K_SEG && (flags & (F_SHOOTER | F_VI | F_HS)) && !(flags & F_B)) {
      float x0 = rdc() + cx, y0 = rdc() + cy, x1 = rdc() + cx, y1 = rdc() + cy;
      if (g.nchar >= MAX_CHAR) continue;
      Char *c = &g.ch[g.nchar];
      c->role = (flags & F_SHOOTER) ? R_HERO : (flags & F_HS) ? R_HOSTAGE : R_ENEMY;
      c->alive = 1;
      c->left = (flags & F_RH) != 0;
      c->hittable = (flags & (F_HKI | F_HGKI)) != 0;
      c->fx = x0, c->fy = y0;
      c->h = sqrtf((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0));
      if (c->h < 20) c->h = 80;
      c->tilt = atan2f(x1 - x0, -(y1 - y0));
      c->body = -1;
      for (int p = 0; p < P_COUNT; p++) c->part[p] = -1;
      if (c->role == R_HERO && g.hero < 0) {
        g.hero = g.nchar;
        g.grenades = (flags & (F_GKI | F_HGKI)) != 0;
      } else if (c->role == R_HERO) {
        c->role = R_ENEMY;   /* (never: one shooter a level) */
      }
      if (c->role == R_ENEMY) g.enemies_left++;
      g.nchar++;
      continue;
    }
    if (g.nobj >= MAX_OBJ) return false;
    Obj *o = &g.obj[g.nobj];
    o->kind = (uint8_t)kind, o->idx = (uint8_t)idx, o->flags = flags, o->cx = cx, o->cy = cy, o->body = -1;
    if (kind == K_SEG) {
      float x0 = rdc(), y0 = rdc(), x1 = rdc(), y1 = rdc();
      o->bx = x0, o->by = y0;
      o->r = sqrtf((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0));
      if (!(flags & F_B)) o->kind = K_POINT;   /* (a stray segment: nothing) */
    } else if (kind == K_POINT) {
      rp += 4;
    } else {
      int np = rd8();
      obj_parts[g.nobj] = rp;
      obj_nparts[g.nobj] = (uint8_t)np;
      for (int k = 0; k < np; k++) rp += 4 * rd8();
      o->nout = (uint16_t)rd16();
      o->outline = rp;
      rp += 4 * o->nout;
      o->nholes = (uint8_t)rd8();
      for (int k = 0; k < o->nholes; k++) rp += 4 * rd8();
    }
    g.nobj++;
  }
  /* pins */
  const uint8_t *pins = rp;
  for (int i = 0; i < npin; i++) {
    rp += 8;
    int nv = rd8();
    rp += 4 * nv + 8 + 4 + 8;
  }
  /* switches: the objects they hold */
  for (int i = 0; i < nsw && g.nsw < MAX_SW; i++) {
    Switch *s = &g.sw[g.nsw];
    s->id = (uint8_t)rd8(), s->colour = (uint8_t)rd8();
    int n = rd8();
    for (int k = 0; k < n; k++) {
      int l = rd8();
      if (s->nlinks < sizeof s->link) s->link[s->nlinks++] = (uint8_t)l;
    }
    float cx = rdc(), cy = rdc();
    s->x0 = rdc() + cx, s->y0 = rdc() + cy, s->x1 = rdc() + cx, s->y1 = rdc() + cy;
    for (int k = 0; k < s->nlinks; k++) {
      Obj *o = obj_by_idx(s->link[k]);
      if (o && !o->sw) o->sw = (uint8_t)(g.nsw + 1);
    }
    g.nsw++;
  }
  if ((uintptr_t)(rp - buf) != (uintptr_t)size) return false;
  /* bodies */
  for (int i = 0; i < g.nobj; i++) {
    Obj *o = &g.obj[i];
    if (o->sw && (o->flags & F_SI)) {
      o->gone = 1;   /* it comes when its switch is hit */
      continue;
    }
    if (!build_obj(i)) return false;
  }
  for (int i = 0; i < g.nchar; i++)
    if (!make_standing(&g.ch[i], i)) return false;
  for (int i = 0; i < g.nsw; i++) {
    Switch *s = &g.sw[i];
    float mx = (s->x0 + s->x1) / 2, my = (s->y0 + s->y1) / 2, dx = s->x1 - s->x0, dy = s->y1 - s->y0;
    float len = sqrtf(dx * dx + dy * dy);
    s->body = (int16_t)world_body(WB_STATIC, mx, my, atan2f(dy, dx), CODE(T_SWITCH, i), false, false, 1);
    if (s->body < 0) return false;
    world_box(s->body, 0, 0, len / 2 + 2, 6, &FIX_SENSOR);
  }
  rp = pins;
  for (int i = 0; i < npin; i++) {
    int t = rd8(), a = rds8(), b = rds8(), motor = rd8();
    float cx = rdc(), cy = rdc();
    int nv = rd8();
    float vx[4] = {0}, vy[4] = {0};
    for (int k = 0; k < nv; k++) {
      float x = rdc() + cx, y = rdc() + cy;
      if (k < 4) vx[k] = x, vy[k] = y;
    }
    float lax = rdc() + cx, lay = rdc() + cy, lbx = rdc() + cx, lby = rdc() + cy;
    float rl = rdc(), as = rdc(), ms = rdf(), mt = rdf();
    if (g.npin >= MAX_PIN) continue;
    Obj *oa = a >= 0 ? obj_by_idx(a) : 0, *ob = b >= 0 ? obj_by_idx(b) : 0;
    int ba = oa && !oa->gone ? oa->body : -1, bb = ob && !ob->gone ? ob->body : -1;
    Pin *p = &g.pin[g.npin];
    p->t = (uint8_t)t, p->motor = (uint8_t)motor, p->a = (int8_t)a, p->b = (int8_t)b, p->joint = -1;
    if (t == 2) {
      if (ba < 0 && bb < 0) continue;
      p->joint = (int16_t)world_revolute(ba, bb, vx[0], vy[0], false, 0, 0, motor != 0, ms, mt);
      p->x0 = vx[0], p->y0 = vy[0];
    } else if (t == 3) {
      if (ba < 0 && bb < 0) continue;
      float len = rl > 0 ? rl : sqrtf((lbx - lax) * (lbx - lax) + (lby - lay) * (lby - lay));
      p->joint = (int16_t)world_rope(ba, bb, lax, lay, lbx, lby, len);
      p->x0 = lax, p->y0 = lay, p->x1 = lbx, p->y1 = lby;
    } else if (t == 5) {
      Obj *o = oa ? oa : ob;
      if (!o || o->body < 0 || nv < 2) continue;
      float dx = vx[1] - vx[0], dy = vy[1] - vy[0], len = sqrtf(dx * dx + dy * dy);
      if (len < 1) continue;
      o->kin = 1;
      o->kdx = dx / len, o->kdy = dy / len, o->klen = len, o->kspeed = as > 0 ? as : 60, o->kpos = 0;
      world_set_type(o->body, WB_KINEMATIC);
      p->x0 = vx[0], p->y0 = vy[0], p->x1 = vx[1], p->y1 = vy[1];
    } else {
      continue;
    }
    g.npin++;
  }
  /* the shooter aims straight ahead */
  if (g.hero >= 0) g.aim = g.ch[g.hero].left ? PI : 0;
  g.power = 0.55f;
  g.state = ST_PLAY;
  return g.hero >= 0;
}

void game_end(void) { world_end(); }

/* ------------------------------------------------------------------ the shooter */
void game_gun(float *sx, float *sy, float *mx, float *my) {
  const Char *c = &g.ch[g.hero];
  float fx, fy, rx, ry, ux, uy, k = c->h / 80;
  frame_of(c, &fx, &fy, &rx, &ry, &ux, &uy);
  *sx = fx + rx * 17 * k + ux * SHOULDER_Y * k;
  *sy = fy + ry * 17 * k + uy * SHOULDER_Y * k;
  float reach = (32 + 14) * k;   /* the arm, then the gun */
  *mx = *sx + cosf(g.aim) * reach, *my = *sy + sinf(g.aim) * reach;
}

static float wrap(float a) {
  while (a > PI) a -= 2 * PI;
  while (a < -PI) a += 2 * PI;
  return a;
}

void game_aim_by(float d) {
  if (g.hero < 0) return;
  Char *c = &g.ch[g.hero];
  /* up turns the gun up on the side he faces; past straight up he turns round */
  g.aim = wrap(g.aim + (c->left ? d : -d));
  bool left = cosf(g.aim) < 0;
  if (fabsf(cosf(g.aim)) > 0.02f && left != (c->left != 0)) c->left = left;
}

void game_turn(bool left) {
  if (g.hero < 0) return;
  Char *c = &g.ch[g.hero];
  if ((c->left != 0) == left) return;
  c->left = left;
  g.aim = wrap(PI - g.aim);
}

bool game_can_fire(void) {
  return g.state == ST_PLAY && !g.end_wait && g.hero >= 0 && g.ch[g.hero].alive && g.shots < g.ammo;
}

static int free_shot(void) {
  for (int i = 0; i < MAX_SHOT; i++)
    if (g.shot[i].body < 0) return i;
  return -1;
}

void game_fire(void) {
  if (!game_can_fire()) return;
  int si = free_shot();
  if (si < 0) return;
  float sx, sy, mx, my;
  game_gun(&sx, &sy, &mx, &my);
  Shot *s = &g.shot[si];
  memset(s, 0, sizeof *s);
  float dx = cosf(g.aim), dy = sinf(g.aim);
  if (g.grenades) {
    float k = g.ch[g.hero].h / 80, hx = sx + dx * 32 * k, hy = sy + dy * 32 * k;
    s->body = (int16_t)world_body(WB_DYNAMIC, hx, hy, 0, CODE(T_SHOT, si), false, false, 1.0f);
    if (s->body < 0) return;
    world_circle(s->body, 0, 0, GRENADE_R, &FIX_GRENADE);
    float v = (3.0f + 13.0f * g.power) * 60;
    world_set_velocity(s->body, dx * v, dy * v);
    world_set_angular(s->body, g.ch[g.hero].left ? -6 : 6);
    s->kind = 1, s->life = FUSE;
    s->px = hx, s->py = hy;
  } else {
    s->body = (int16_t)world_body(WB_DYNAMIC, mx, my, 0, CODE(T_SHOT, si), true, false, 0.0f);
    if (s->body < 0) return;
    world_circle(s->body, 0, 0, BULLET_R, &FIX_BULLET);
    world_set_velocity(s->body, dx * BULLET_SPEED, dy * BULLET_SPEED);
    s->kind = 0, s->life = 300;
    s->px = mx, s->py = my;
  }
  s->tx[0] = s->px, s->ty[0] = s->py, s->tn = 1;
  g.shots++;
}

int game_arc(float *xs, float *ys, int n) {
  if (g.hero < 0 || !g.grenades) return 0;
  float sx, sy, mx, my, k = g.ch[g.hero].h / 80;
  game_gun(&sx, &sy, &mx, &my);
  float x = sx + cosf(g.aim) * 32 * k, y = sy + sinf(g.aim) * 32 * k;
  float v = (3.0f + 13.0f * g.power) * 60, vx = cosf(g.aim) * v, vy = sinf(g.aim) * v;
  int m = 0;
  for (int step = 1; step <= 120 && m < n; step++) {
    vy += 9.81f * 60 * W_STEP;
    vx /= 1 + W_STEP * 0.5f, vy /= 1 + W_STEP * 0.5f;
    float nx = x + vx * W_STEP, ny = y + vy * W_STEP, hx, hy, ax, ay;
    if (world_ray(x, y, nx, ny, 1u << WC_STATIC | 1u << WC_LEVEL, &hx, &hy, &ax, &ay) >= 0) {
      xs[m] = hx, ys[m] = hy, m++;
      break;
    }
    x = nx, y = ny;
    if (step % 4 == 0) xs[m] = x, ys[m] = y, m++;
  }
  return m;
}

/* ------------------------------------------------------------------ what happens */
typedef struct {
  int16_t a, b, ba, bb;
  float speed, x, y;
} Event;
#define MAX_EVENTS 48
static Event events[MAX_EVENTS];
static int nevents;

void game_contact(int a, int b, int ba, int bb, float speed, float x, float y) {
  if (nevents < MAX_EVENTS) events[nevents++] = (Event){(int16_t)a, (int16_t)b, (int16_t)ba, (int16_t)bb, speed, x, y};
}

static bool is_hero_code(int code) { return KIND(code) == T_CHAR && INDEX(code) == g.hero; }

bool game_contact_enabled(int a, int b) {
  /* his grenades don't touch the shooter */
  if (KIND(a) == T_SHOT && is_hero_code(b)) return false;
  if (KIND(b) == T_SHOT && is_hero_code(a)) return false;
  return true;
}

static void remove_obj(int oi) {
  Obj *o = &g.obj[oi];
  if (o->gone) return;
  o->gone = 1;
  if (o->body >= 0) world_destroy(o->body);
  o->body = -1;
  /* the pins on it go with it */
  for (int i = 0; i < g.npin; i++)
    if (g.pin[i].joint >= 0 && !world_joint_alive(g.pin[i].joint)) g.pin[i].joint = -1;
}

static void boom_at(float x, float y) {
  int k = 0;
  for (int i = 1; i < MAX_BOOM; i++)
    if (g.boom[i].t > g.boom[k].t) k = i;
  for (int i = 0; i < MAX_BOOM; i++)
    if (g.boom[i].t == 0) { k = i; break; }
  g.boom[k].x = x, g.boom[k].y = y, g.boom[k].t = 1;
}


static void blast(float x, float y) {
  boom_at(x, y);
  for (int i = 0; i < g.nchar; i++) {
    Char *c = &g.ch[i];
    float cx, cy, s, cs;
    if (c->alive) {
      if (c->role == R_HERO && !c->hittable) continue;
      game_part(c, P_TORSO, &cx, &cy, &s, &cs);
      float dx = cx - x, dy = cy - y, d = sqrtf(dx * dx + dy * dy);
      if (d < BLAST_R) {
        float f = 1.2f * (1 - d / BLAST_R) / (d > 1 ? d : 1);
        kill(i, dx * f, dy * f, cx, cy);
      }
      continue;
    }
    for (int p = 0; p < P_COUNT; p++) {
      if (c->part[p] < 0 || !world_alive(c->part[p])) continue;
      world_xf(c->part[p], &cx, &cy, &s, &cs);
      float dx = cx - x, dy = cy - y, d = sqrtf(dx * dx + dy * dy);
      if (d < BLAST_R) {
        float f = 0.4f * (1 - d / BLAST_R) / (d > 1 ? d : 1);
        world_impulse(c->part[p], dx * f, dy * f, cx, cy);
      }
    }
  }
  for (int i = 0; i < g.nobj; i++) {
    Obj *o = &g.obj[i];
    if (o->gone || o->body < 0) continue;
    float cx, cy, s, cs;
    world_xf(o->body, &cx, &cy, &s, &cs);
    if (o->kind == K_SEG) cx += 0, cy += 0;
    /* the nearest point of its box: its centre is enough for these sizes */
    float dx = cx - x, dy = cy - y;
    if (o->kind == K_POLY && o->nout) {
      /* (polygons' origins can be far from them: use their outline's middle) */
      const uint8_t *q = o->outline;
      float mx = 0, my = 0;
      for (int k = 0; k < o->nout; k++) {
        mx += (int16_t)(q[4 * k] | q[4 * k + 1] << 8) * 0.125f;
        my += (int16_t)(q[4 * k + 2] | q[4 * k + 3] << 8) * 0.125f;
      }
      mx /= o->nout, my /= o->nout;
      dx = cx + cs * mx - s * my - x, dy = cy + s * mx + cs * my - y;
    }
    float d = sqrtf(dx * dx + dy * dy);
    if (d >= BLAST_R) continue;
    if (o->flags & F_BE) { if (!pending_blast[i]) pending_blast[i] = 6; continue; }
    if (o->flags & (F_BO | F_BR)) { remove_obj(i); continue; }
    if (!world_dynamic(o->body)) continue;
    float m = world_mass(o->body), f = 3.0f * (1 - d / BLAST_R) / (d > 1 ? d : 1);
    float cap = m * 12.0f / (f * (d > 1 ? d : 1) + 1e-6f);
    if (cap < 1) f *= cap;
    world_impulse(o->body, dx * f, dy * f, cx + dx * 0.5f, cy + dy * 0.5f);
  }
}

static void explode_obj(int oi) {
  Obj *o = &g.obj[oi];
  if (o->gone) return;
  float x = o->cx, y = o->cy, s, cs;
  if (o->body >= 0) world_xf(o->body, &x, &y, &s, &cs);
  if (o->kind == K_POLY && o->nout && o->body >= 0) {
    const uint8_t *q = o->outline;
    float mx = 0, my = 0;
    for (int k = 0; k < o->nout; k++) {
      mx += (int16_t)(q[4 * k] | q[4 * k + 1] << 8) * 0.125f;
      my += (int16_t)(q[4 * k + 2] | q[4 * k + 3] << 8) * 0.125f;
    }
    mx /= o->nout, my /= o->nout;
    float bx = x, by = y;
    x = bx + cs * mx - s * my, y = by + s * mx + cs * my;
  }
  remove_obj(oi);
  blast(x, y);
}

static void switch_on(int si) {
  Switch *s = &g.sw[si];
  if (s->on) return;
  s->on = 1;
  for (int k = 0; k < s->nlinks; k++)
    for (int i = 0; i < g.nobj; i++) {
      Obj *o = &g.obj[i];
      if (o->idx != s->link[k] || o->sw != si + 1) continue;
      if (o->flags & F_SI) {
        if (o->gone && o->body < 0) {
          o->gone = 0;
          build_obj(i);
        }
      } else {
        remove_obj(i);
      }
    }
}

static void hit_level(int oi, bool by_bullet, float speed) {
  Obj *o = &g.obj[oi];
  if (o->gone) return;
  if (o->flags & F_BE) {
    if ((by_bullet || speed > BREAK_SPEED) && !pending_blast[oi]) pending_blast[oi] = 1;
  } else if (o->flags & (F_BO | F_BR)) {
    if (by_bullet || speed > BREAK_SPEED) remove_obj(oi);
  }
}

static void handle(const Event *e) {
  int a = e->a, b = e->b;
  for (int pass = 0; pass < 2; pass++, a = e->b, b = e->a) {
    int ka = KIND(a), kb = KIND(b), ia = INDEX(a), ib = INDEX(b);
    if (ka == T_SWITCH && (kb == T_SHOT || kb == T_CHAR || (kb == T_LEVEL && g.obj[ib].body >= 0 &&
                                                            world_dynamic(g.obj[ib].body)))) {
      switch_on(ia);
      continue;
    }
    if (ka == T_SHOT && g.shot[ia].body >= 0) {
      Shot *s = &g.shot[ia];
      if (s->kind == 0 && kb == T_LEVEL) hit_level(ib, true, e->speed);
      continue;
    }
    if (ka == T_CHAR && g.ch[ia].alive && kb != T_SHOT && kb != T_SWITCH && e->speed > KILL_SPEED) {
      bool other_still = kb == T_LEVEL && (g.obj[ib].body < 0 || !world_dynamic(g.obj[ib].body));
      (void)other_still;
      kill(ia, 0, 0, e->x, e->y);
      continue;
    }
    if (ka == T_LEVEL && kb != T_SHOT && kb != T_SWITCH) hit_level(ia, false, e->speed);
  }
}

static bool seg_cross(float ax, float ay, float bx, float by, float cx, float cy, float dx, float dy) {
  float r1 = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax), r2 = (bx - ax) * (dy - ay) - (by - ay) * (dx - ax);
  float r3 = (dx - cx) * (ay - cy) - (dy - cy) * (ax - cx), r4 = (dx - cx) * (by - cy) - (dy - cy) * (bx - cx);
  return r1 * r2 <= 0 && r3 * r4 <= 0 && (r1 != 0 || r2 != 0);
}

void game_step(void) {
  if (g.state != ST_PLAY) return;
  g.frame++;
  /* tracks */
  for (int i = 0; i < g.nobj; i++) {
    Obj *o = &g.obj[i];
    if (!o->kin || o->gone || o->body < 0) continue;
    o->kpos += o->kspeed * W_STEP;
    if (o->kpos > o->klen) o->kpos = o->klen, o->kspeed = -o->kspeed;
    if (o->kpos < 0) o->kpos = 0, o->kspeed = -o->kspeed;
    world_set_velocity(o->body, o->kdx * o->kspeed, o->kdy * o->kspeed);
  }
  nevents = 0;
  world_step();
  for (int i = 0; i < nevents; i++) handle(&events[i]);
  for (int i = 0; i < g.nobj; i++)
    if (pending_blast[i] && --pending_blast[i] == 0) explode_obj(i);
  /* shots */
  bool flying = false;
  for (int i = 0; i < MAX_SHOT; i++) {
    Shot *s = &g.shot[i];
    if (s->body < 0) continue;
    float x, y, sn, cs;
    world_xf(s->body, &x, &y, &sn, &cs);
    if (s->kind == 0) {
      /* a rope it crosses is cut */
      for (int p = 0; p < g.npin; p++) {
        Pin *pn = &g.pin[p];
        if (pn->t != 3 || pn->joint < 0 || !world_joint_alive(pn->joint)) continue;
        float ax, ay, bx, by;
        world_joint_anchors(pn->joint, &ax, &ay, &bx, &by);
        if (seg_cross(s->px, s->py, x, y, ax, ay, bx, by)) {
          world_destroy_joint(pn->joint);
          pn->joint = -1;
        }
      }
      /* characters along its path: shot through */
      int hit[8];
      float hx[8], hy[8];
      int nh = world_ray_all(s->px, s->py, x, y, 1u << WC_CHARACTER, hit, hx, hy, 8);
      float vx, vy;
      world_velocity(s->body, &vx, &vy);
      float v = sqrtf(vx * vx + vy * vy) + 1e-3f;
      for (int h = 0; h < nh; h++) {
        int code = world_obj(hit[h]);
        if (KIND(code) != T_CHAR) continue;
        Char *c = &g.ch[INDEX(code)];
        if (INDEX(code) == g.hero && !c->hittable) continue;
        if (c->alive) kill(INDEX(code), vx / v * 0.5f, vy / v * 0.5f, hx[h], hy[h]);
        else world_impulse(hit[h], vx / v * 0.15f, vy / v * 0.15f, hx[h], hy[h]);
      }
      for (int k = TRAIL - 1; k > 0; k--) s->tx[k] = s->tx[k - 1], s->ty[k] = s->ty[k - 1];
      s->tx[0] = x, s->ty[0] = y;
      if (s->tn < TRAIL) s->tn++;
    }
    s->px = x, s->py = y;
    if (--s->life <= 0 || fabsf(x) > 2500 || fabsf(y) > 2500) {
      world_destroy(s->body);
      s->body = -1;
      if (s->kind == 1) blast(x, y);
      continue;
    }
    flying = true;
  }
  /* characters that fell out of the world */
  for (int i = 0; i < g.nchar; i++) {
    Char *c = &g.ch[i];
    if (c->alive && c->body >= 0) {
      float x, y, s, cs;
      world_xf(c->body, &x, &y, &s, &cs);
      if (y > 1500) kill(i, 0, 0, x, y);
    }
  }
  for (int i = 0; i < MAX_BOOM; i++)
    if (g.boom[i].t && ++g.boom[i].t > 24) g.boom[i].t = 0;
  /* the end */
  if (g.end_wait) {
    if (--g.end_wait == 0) g.state = (g.lose || g.enemies_left > 0) ? ST_LOST : ST_WON;
    return;
  }
  if (g.lose) {
    g.end_wait = 50;
  } else if (g.enemies_left <= 0) {
    g.end_wait = 60;
  } else if (g.shots >= g.ammo && !flying) {
    if (++g.quiet > 150) {
      g.lose = LOSE_AMMO;
      g.end_wait = 1;
    }
  } else {
    g.quiet = 0;
  }
}

int game_stars(void) {
  int over = g.shots - g.tr;
  if (over <= 0) return 3;
  return over >= 3 ? 0 : 3 - over;
}
