/* Enemies (tools/ents.py: OK_ENEMY): HealthManager, Recoil, SpriteFlash, DamageHero and EnemyDeathEffects, each kind's
 * FSM, the corpses they leave and the geo they drop. */
#pragma GCC optimize("Os")   /* (its code small: not where a frame's time goes) */
#include <math.h>
#include "game.h"
#ifdef HOST
#include <stdio.h>
#include <stdlib.h>
#endif

#define DT 0.02f
#define MAX_ENEMIES 24
#define MAX_GEO 64
#define TERRAIN_FRICTION 0.2f   /* (the Terrain material; with another, their geometric mean) */

/* ---------------------------------------------------------------- what each kind is */
/* (data.h: EK_* each kind, KIND_TABLE: the FSM each runs and the clips its roles play) */
enum {
  EF_CRAWLER = 1, EF_BUZZER, EF_SHADE, EF_HUSK, EF_CLIMBER, EF_BOUNCER, EF_SPITTER, EF_ROLLER, EF_BLOCKER, EF_LEAPER, EF_GUARD,
  EF_FK, EF_FKHEAD, EF_GFLY, EF_HATCHER, EF_HATCHLING, EF_SLUG, EF_MOSSWALKER, EF_PIGEON, EF_PLANTTRAP, EF_SHAKER,
  EF_MOSQUITO, EF_FATFLY, EF_MOSSCHARGER, EF_MOSSKNIGHT, EF_HORNET, EF_MENDER
};
enum { R_IDLE, R_TURN, R_WALK, R_A1, R_A2, R_A3, R_A4, R_A5, R_A6, R_A7, R_A8, R_DEATH_AIR, R_DEATH_LAND, NUM_ROLES };
typedef struct {
  uint8_t fsm;
  int16_t clip[NUM_ROLES];
} Kind;
static const Kind kinds[NUM_KINDS] = KIND_TABLE;
#define CLIP(e, role) (kinds[(e)->kind].clip[role])
#define FSM(e) (kinds[(e)->kind].fsm)
#define NO_ENT 0xFFFF   /* (an enemy spawned, not one of the room's) */
/* the records after an enemy's (tools/ents.py: ET_*) */
enum { ET_COLLIDER, ET_ALERT, ET_RANGE, ET_WALKER, ET_RECOIL, ET_CORPSE, ET_VARS, ET_TERRAIN, ET_HITBOX, ET_ZONE, ET_COND,
       ET_CONTACT };
enum { CF_BREAKER = 1, CF_FACES_RIGHT = 2, CF_LOW_ARC = 4, CF_NO_COLLIDER = 32 };   /* a corpse's (ET_CORPSE) */
#define EF_INVINCIBLE 0x8000   /* (its HealthManager's invincible at first) */
enum { WF_PAUSES = 1, WF_IGNORE_HOLES = 2, WF_NO_TURN_TO_HERO = 4, WF_START_INACTIVE = 8, WF_AMBUSH = 16, WF_WAIT_HERO_X = 32,
       WF_PREVENT_TURN = 64, WF_NO_SCALE = 128, WF_RIGHT_NEG = 256 };   /* a Walker's (ET_WALKER) */

/* ---------------------------------------------------------------- an enemy */
enum { EM_OFF, EM_ALIVE, EM_CORPSE };
enum { RC_READY, RC_RECOILING, RC_FROZEN };
enum { CS_AIR, CS_DEATH_ANIM, CS_LANDED };
/* (flags: 1 collider off, 2 RECOIL HORIZONTAL, 4 invincible, 8 TOOK DAMAGE: for its FSM this frame, 16 collider off
 * by its frame, 32 not drawn, 64 its FSMs off (FSMActivator), 128 its object off till the Knight touches a trigger
 * (ActivateChildrenOnContact)) */
typedef struct {
  uint8_t mode, kind, st, flags;
  uint16_t ent;                   /* its record (NO_ENT: spawned) */
  int8_t damage;                  /* DamageHero's */
  uint8_t geo_s, geo_m, geo_l;    /* the geo it drops */
  uint8_t rq;                     /* its rotation (quarter turns) */
  int8_t sy;                      /* its y scale's sign */
  float z;
  Body body;
  Anim anim;
  float sx;   /* its transform's x scale: which way it faces */
  int16_t hp;
  float evasion;
  /* Recoil */
  uint8_t rc_state, rc_flags;
  int8_t rc_dir;
  bool rc_sweep;
  float rc_time, rc_speed, rc_base, rc_dur;
  /* Walker */
  uint8_t wk_state, wk_stop;
  int8_t wk_facing, wk_turning;
  float wk_walk, wk_pause, wk_turn_cd;
  uint16_t wk_rec;   /* its record (0: none) */
  /* SpriteFlash (flashInfected) */
  float flash_t;
  bool flashing;
  uint8_t c2, c3, c4;          /* (more counters) */
  /* the FSM's state */
  float t0, t1, wait, start_x, start_y, ax, ay, pause0, pause1, jx, jy;
  float ang, qx, qy, tx, ty;   /* (its rotation, degrees; a tween's from and to) */
  bool b0, b1;
  uint8_t c0, c1;              /* (counters) */
  uint8_t hb_on, sub_hb;       /* its attacks on (ET_HITBOX's bits); the one whose sprite plays (k + 1) */
  int16_t col_sprite;          /* (the frame whose collider it has, + 1) */
  Anim sub;                    /* (that sprite's) */
  /* alert range (local circle, or box: ar_hy >= 0) and sight */
  float ar_x, ar_y, ar_r, ar_hy;
  bool in_alert, can_see;
  uint8_t inv_dir;   /* (HealthManager.invincibleFromDirection) */
  uint8_t fx;        /* (an effect its sub sprite plays, + 1) */
} Enemy;
static Enemy en[MAX_ENEMIES];
static uint32_t swing_bits;   /* (a bit an enemy: hit this swing) */

/* its transform's x scale (its collider's offset with it: along its own x, turned) */
static void set_scale_x(Enemy *e, float sx) {
  if ((sx < 0) != (e->sx < 0)) {
    if (e->rq & 1) e->body.oy = -e->body.oy, e->ar_y = -e->ar_y;
    else e->body.ox = -e->body.ox, e->ar_x = -e->ar_x;
  }
  e->sx = sx;
}

/* a direction in its own axes -> the world's (TransformDirection: its rotation, not its scale) */
static void enemy_dir(const Enemy *e, float lx, float ly, float *wx, float *wy) {
  switch (e->rq & 3) {
    case 0: *wx = lx, *wy = ly; break;
    case 1: *wx = -ly, *wy = lx; break;
    case 2: *wx = -lx, *wy = -ly; break;
    default: *wx = ly, *wy = -lx; break;
  }
}

/* a place in its own units (sized, not signed) -> the world's offset from it: its scale's signs, then its rotation */
static void enemy_point(const Enemy *e, float lx, float ly, float *wx, float *wy) {
  enemy_dir(e, e->sx < 0 ? -lx : lx, e->sy < 0 ? -ly : ly, wx, wy);
}

/* its collider: a box in its own units */
static void enemy_set_box(Enemy *e, float cx, float cy, float hx, float hy) {
  enemy_point(e, cx, cy, &e->body.ox, &e->body.oy);
  if (e->rq & 1) e->body.hx = hy, e->body.hy = hx;
  else e->body.hx = hx, e->body.hy = hy;
}

static const Ent *ent_at(int i) {
  int n;
  return &room_ents(&n)[i];
}

/* an enemy's record of a tag (ET_*), or NULL */
static const Ent *enemy_rec(const Enemy *e, int tag) {
  if (e->ent == NO_ENT) return NULL;
  const Ent *d = ent_at(e->ent);
  for (int i = 1; i <= d->s0; i++)
    if (d[i].type == ENT_BOX && d[i].flags == tag) return &d[i];
  return NULL;
}

/* a range (ET_ALERT, ET_RANGE) -> its center's offset, and its radius (hy < 0) or half sizes, turned as it is */
static void enemy_range(const Enemy *e, const Ent *r, float *x, float *y, float *rx, float *hy) {
  enemy_point(e, r->x0, r->y0, x, y);
  *rx = r->x1, *hy = r->y1;
  if (r->y1 >= 0 && (e->rq & 1)) *rx = r->y1, *hy = r->x1;
}

/* the terrain it holds (a child's colliders) gone: it died */
static void enemy_terrain_off(const Ent *d) {
  for (int i = 1; i <= d->s0; i++)
    if (d[i].type == ENT_BOX && d[i].flags == ET_TERRAIN)
      for (int c = d[i].a; c < d[i].a + d[i].group; c++) phys_collider_enable(c, false);
}

/* 2D Toolkit: the frame shown sets the collider (a box: its body's; none: off) */
static void frame_collider(Enemy *e) {
  if (e->anim.sprite + 1 == e->col_sprite) return;
  e->col_sprite = (int16_t)(e->anim.sprite + 1);
  const float *d;
  int n, t = sprite_collider(e->anim.sprite, &d, &n);
  float k = fabsf(e->sx);
  if (t == SC_BOX) {
    enemy_set_box(e, d[0] * k, d[1] * k, d[2] * k, d[3] * k);
    e->flags &= (uint8_t)~16;
  } else if (t == SC_NONE)
    e->flags |= 16;
}

/* the collider's world box */
static void enemy_box(const Enemy *e, float *x0, float *y0, float *x1, float *y1) {
  float cx = e->body.x + e->body.ox, cy = e->body.y + e->body.oy;
  *x0 = cx - e->body.hx, *x1 = cx + e->body.hx, *y0 = cy - e->body.hy, *y1 = cy + e->body.hy;
}

/* ---------------------------------------------------------------- geo (GeoControl) */
/* (kept small, many at once: its body's place, velocity and first two contacts; a whole Body only as it steps) */
#define GEO_CONTACTS 2
typedef struct {
  bool on;
  uint8_t type;   /* 0 small (1), 1 medium (5), 2 large (25) */
  uint8_t ncontacts, ccol[GEO_CONTACTS];
  uint8_t getter;   /* (Gathering Swarm: its bug's chances, 1 + a number; 0: none) */
  int8_t cnx[GEO_CONTACTS], cny[GEO_CONTACTS];   /* (the contacts' normals, in 127ths) */
  float x, y, vx, vy;
  Anim anim;
  float age;      /* (since it was flung: picked up after 0.25 s) */
} Geo;
static Geo geo[MAX_GEO];
static const struct {
  int air, idle, value;
  float gravity, bounce, ox, oy, hx, hy;
} geo_kinds[3] = {
    {CLIP_GEO_SMALL_AIR, CLIP_GEO_SMALL_IDLE, 1, 1.5f, 0.7f, -0.0078f, -0.0391f, 0.2969f / 2, 0.2969f / 2},
    {CLIP_GEO_MED_AIR, CLIP_GEO_MED_IDLE, 5, 1.5f, 0.6f, -0.0078f, 0, 0.3281f / 2, 0.5312f / 2},
    {CLIP_GEO_LARGE_AIR, CLIP_GEO_LARGE_IDLE, 25, 1.75f, 0.4f, 0.0078f, 0, 0.4531f / 2, 0.4688f / 2},
};
#define GEO_SCALE 1.5f

/* FlingUtils.SpawnAndFling: n of a kind, each at a random speed and angle */
static void geo_fling(int type, int n, float x, float y, float smin, float smax, float amin, float amax) {
  geo_fling_at(type, n, x, y, smin, smax, amin, amax, 0);
}

void geo_fling_at(int type, int n, float x, float y, float smin, float smax, float amin, float amax, float spread) {
  for (int k = 0; k < n; k++) {
    Geo *g = NULL;
    for (int i = 0; i < MAX_GEO && !g; i++)
      if (!geo[i].on) g = &geo[i];
    if (!g) return;
    memset(g, 0, sizeof *g);
    g->on = true, g->type = (uint8_t)type;
    g->x = x + rand_range(-spread, spread), g->y = y + rand_range(-spread, spread);
    float s = rand_range(smin, smax), a = rand_range(amin, amax) * (float)M_PI / 180;
    g->vx = cosf(a) * s, g->vy = sinf(a) * s;
    anim_play(&g->anim, geo_kinds[type].air);
    /* (GeoControl.OnEnable: the Getter with Gathering Swarm, but at the Grubfather's) */
    if (charm_on(1) && strcmp(room_name(g_room.id), "Crossroads_38")) g->getter = (uint8_t)(1 + rand_range(0, 254.999f));
  }
}

/* the Getter's chances, from its number: the wait before the bug comes, its ease time, where it comes from */
static float getter_rand(const Geo *g, int k, float lo, float hi) {
  uint32_t h = (uint32_t)g->getter * 2654435761u + (uint32_t)k * 40503u;
  h ^= h >> 13, h *= 0x5bd1e995u, h ^= h >> 15;
  return lo + (hi - lo) * (float)(h & 0xffff) / 65535.0f;
}
#define GETTER_X (-0.06624349f)   /* (the bug's place on the geo) */
#define GETTER_Y 0.1932119f
static float getter_wait(const Geo *g) { return getter_rand(g, 0, 1, 1.7f); }
static float getter_ease(const Geo *g) { return getter_rand(g, 1, 0.3f, 0.5f); }
static bool geo_attracted(const Geo *g) { return g->getter && g->age >= getter_wait(g) + getter_ease(g); }

/* ObjectBounce: off what it hit, at its speed before, times the bounce factor (and a little chance) */
void body_bounce(Body *b, float pvx, float pvy, int had, float factor) {
  if (factor < 0 || b->ncontacts <= had) return;
  float speed = sqrtf(pvx * pvx + pvy * pvy);
  if (speed <= 1) return;
  float nx = b->cnx[b->ncontacts - 1], ny = b->cny[b->ncontacts - 1];
  float ux = pvx / speed, uy = pvy / speed, d = ux * nx + uy * ny;
  float rx = ux - 2 * d * nx, ry = uy - 2 * d * ny, k = speed * factor * rand_range(0.8f, 1.2f);
  b->vx = rx * k, b->vy = ry * k;
}

static void geo_tick(void) {
  const Hero *h = &g_hero;
  float k = h->cs.facing_right ? -1.0f : 1.0f;
  float hx = h->body.x + 0.0055733f * k, hy = h->body.y - 0.6942673f;   /* (the HeroBox) */
  for (int i = 0; i < MAX_GEO; i++) {
    Geo *g = &geo[i];
    if (!g->on) continue;
    g->age += DT;
    float ox = geo_kinds[g->type].ox * GEO_SCALE, oy = geo_kinds[g->type].oy * GEO_SCALE;
    float bhx = geo_kinds[g->type].hx * GEO_SCALE, bhy = geo_kinds[g->type].hy * GEO_SCALE;
    if (geo_attracted(g)) {
      /* (Gathering Swarm, attracted: no gravity, a trigger through all; pulled to below the Knight, at most 20 a
       * second) */
      float dx = h->body.x - g->x, dy = h->body.y - 0.5f - g->y, d = sqrtf(dx * dx + dy * dy);
      if (d > 1) dx /= d, dy /= d;
      g->vx += dx * 150 * DT, g->vy += dy * 150 * DT;
      float v = sqrtf(g->vx * g->vx + g->vy * g->vy);
      if (v > 20) g->vx *= 20 / v, g->vy *= 20 / v;
      g->x += g->vx * DT, g->y += g->vy * DT;
      g->ncontacts = 0;
    } else {
      Body b;
      memset(&b, 0, sizeof b);
      b.x = g->x, b.y = g->y, b.vx = g->vx, b.vy = g->vy;
      b.ox = ox, b.oy = oy, b.hx = bhx, b.hy = bhy;
      b.gravity_scale = geo_kinds[g->type].gravity, b.friction = 0.2f, b.mask = CF_TERRAIN;
      b.ncontacts = g->ncontacts;
      for (int c = 0; c < g->ncontacts; c++) b.ccol[c] = g->ccol[c], b.cnx[c] = g->cnx[c] / 127.0f, b.cny[c] = g->cny[c] / 127.0f;
      float pvx = b.vx, pvy = b.vy;
      int had = b.ncontacts;
      body_step(&b, DT);
      bool hit = b.ncontacts > had;
      if (hit) body_bounce(&b, pvx, pvy, had, geo_kinds[g->type].bounce);
      g->x = b.x, g->y = b.y, g->vx = b.vx, g->vy = b.vy;
      g->ncontacts = (uint8_t)(b.ncontacts < GEO_CONTACTS ? b.ncontacts : GEO_CONTACTS);
      for (int c = 0; c < g->ncontacts; c++)
        g->ccol[c] = b.ccol[c], g->cnx[c] = (int8_t)lrintf(b.cnx[c] * 127), g->cny[c] = (int8_t)lrintf(b.cny[c] * 127);
      if (hit) {
        /* (OnCollisionEnter2D: the idle animation from a random frame) */
        anim_play_from_frame(&g->anim, geo_kinds[g->type].idle, (int)rand_range(0, (float)clip_frames_count(geo_kinds[g->type].idle) - 0.001f));
      }
    }
    g->anim.events = 0;
    anim_update(&g->anim, DT);
    float x0 = g->x + ox - bhx, x1 = g->x + ox + bhx, y0 = g->y + oy - bhy, y1 = g->y + oy + bhy;
    if (g->age >= 0.25f && !h->hidden && x1 > hx - 0.2277069f && x0 < hx + 0.2277069f && y1 > hy - 0.5848932f &&
        y0 < hy + 0.5848932f) {
      hero_add_geo(geo_kinds[g->type].value);
      g->on = false;
    }
    if (g->y < -10) g->on = false;
  }
}

/* ---------------------------------------------------------------- SpriteFlash */
static uint8_t flash_tint_at(bool on, float t, int slot) {
  if (!on || t > 0.27f) return 0;
  /* flashInfected: up 0.01 s, stays 0.01 s, down over 0.25 s, to 0.9 of orange */
  float k;
  if (t < 0.01f) k = t / 0.01f;
  else if (t < 0.02f) k = 1;
  else k = 1 - (t - 0.02f) / 0.25f;
  if (k < 0) k = 0;
  return gfx_dyn_flash(slot, 255, 255, 255, 255, 255, 79, 0, (uint8_t)(0.9f * k * 255));
}

/* (uninfected (EnemyDeathEffectsUninfected, EnemyHitEffectsUninfected): flashFocusHeal, white: up 0.01 s, stays 0.01 s,
 * down over 0.35 s, to 0.85) */
static uint8_t flash_white_at(bool on, float t, int slot) {
  if (!on || t > 0.37f) return 0;
  float k = t < 0.01f ? t / 0.01f : t < 0.02f ? 1 : 1 - (t - 0.02f) / 0.35f;
  if (k < 0) k = 0;
  return gfx_dyn_flash(slot, 255, 255, 255, 255, 255, 255, 255, (uint8_t)(0.85f * k * 255));
}

static bool uninfected(const Enemy *e) { return e->ent != NO_ENT && (ent_at(e->ent)->s1 >> 12 & 3) == 1; }

static uint8_t flash_tint(const Enemy *e, int slot) {
  return uninfected(e) ? flash_white_at(e->flashing, e->flash_t, slot) : flash_tint_at(e->flashing, e->flash_t, slot);
}

/* ---------------------------------------------------------------- Recoil */
#define RF_NONE 0x80   /* (no Recoil component) */
#define RF_HIT 0x40    /* (HIT LEFT / RIGHT / UP / DOWN this frame: rc_dir its direction) */
#define RF_BLOCKED 0x20   /* (BLOCKED HIT this frame) */
#define RF_DIR_SHIFT 3    /* (bits 3-4: the last attack's direction, HealthManager.GetAttackDirection) */
static void climber_stun(Enemy *e);
static void recoil_by_direction(Enemy *e, int dir, float magnitude) {
  if (e->rc_state != RC_READY || (e->rc_flags & RF_NONE)) return;
  if (e->rc_flags & 2) {
    /* freezeInPlace: a climber is stunned (its own handling); others stop still a while */
    if (FSM(e) == EF_CLIMBER) climber_stun(e);
    else e->rc_state = RC_FROZEN, e->rc_time = e->rc_dur, e->body.vx = e->body.vy = 0;
    return;
  }
  if (dir == 1 && (e->rc_flags & 4)) return;   /* (preventRecoilUp) */
  e->rc_state = RC_RECOILING;
  e->rc_speed = e->rc_base * magnitude;
  e->rc_dir = (int8_t)dir;
  e->rc_sweep = true;
  e->rc_time = e->rc_dur;
  e->rc_flags |= RF_HIT;
  if (dir == 0 || dir == 2) e->flags |= 2;   /* (RECOIL HORIZONTAL to its FSM) */
}

/* Sweep.Check: three rays from the collider's side, the distance clipped by the terrain */
static void recoil_fixed(Enemy *e) {
  if (e->rc_state == RC_FROZEN) {
    e->body.vx = e->body.vy = 0;
    if ((e->rc_time -= DT) <= 0) e->rc_state = RC_READY;
    return;
  }
  if (e->rc_state != RC_RECOILING) return;
  if (e->rc_sweep) {
    float dx = (float)(e->rc_dir == 0) - (float)(e->rc_dir == 2), dy = (float)(e->rc_dir == 1) - (float)(e->rc_dir == 3);
    float dist = e->rc_speed * DT, d = dist;
    float cx = e->body.x + e->body.ox + e->body.hx * dx, cy = e->body.y + e->body.oy + e->body.hy * dy;
    for (int i = 0; i < 3; i++) {
      float f = (float)i - 1;
      float x = cx + e->body.hx * fabsf(dy) * f - dx * 0.1f, y = cy + e->body.hy * fabsf(dx) * f - dy * 0.1f;
      PhysHit hit;
      if (phys_ray(x, y, dx, dy, d + 0.1f, CF_TERRAIN, &hit) && hit.dist - 0.1f < d) d = hit.dist - 0.1f;
    }
    if (dist - d > 1e-6f) e->rc_sweep = false;
    if (d > 1e-6f) e->body.x += dx * d, e->body.y += dy * d;
  }
  e->rc_time -= DT;
  if (e->rc_time <= 0) e->rc_state = RC_READY;
}

/* ---------------------------------------------------------------- death */
static void corpse_start(Enemy *e, float direction, bool has_direction) {
  const Ent *c = enemy_rec(e, ET_CORPSE);
  if (!c || kinds[e->kind].clip[R_DEATH_AIR] < 0) {
    e->mode = EM_OFF;   /* (no corpse) */
    return;
  }
  if (FSM(e) == EF_PLANTTRAP) {
    /* (its corpse: its Death played where it was, turned as it was; then its renderer off) */
    e->mode = EM_CORPSE, e->st = CS_DEATH_ANIM;
    e->body.vx = e->body.vy = 0, e->body.gravity_scale = 0, e->body.mask = 0;
    anim_play_from_frame(&e->anim, kinds[e->kind].clip[R_DEATH_AIR], 0);
    e->flashing = true, e->flash_t = 0;
    return;
  }
  e->mode = EM_CORPSE, e->st = CS_AIR;
  e->body.y += c->p3;
  e->body.ox = c->x0, e->body.oy = c->y0, e->body.hx = c->x1, e->body.hy = c->y1;
  e->body.gravity_scale = c->p0, e->body.friction = 0.2f, e->body.mask = (c->a & CF_NO_COLLIDER) ? 0 : CF_SOLID;
  e->body.ncontacts = 0;
  float speed = c->p2, angle = 90, sign = e->sx < 0 ? -1.0f : 1.0f;
  switch (cardinal(direction)) {
    case 0: angle = 60, e->sx = sign; break;    /* (corpseFacesRight: no) */
    case 2: angle = 120, e->sx = -sign; break;
    case 3: angle = 270; break;
    default: angle = rand_range(75, 105), speed *= 1.3f; break;
  }
  if (!has_direction) speed = 0;
  float a = angle * (float)M_PI / 180;
  e->body.vx = cosf(a) * speed, e->body.vy = sinf(a) * speed;
  anim_play(&e->anim, kinds[e->kind].clip[R_DEATH_AIR]);
  e->flashing = true, e->flash_t = 0;   /* (EmitInfectedEffects: the corpse flashes) */
}

/* EnemyDeathEffects.EmitEffects: the camera's shake by its enemyDeathType (Infected, LargeInfected; SmallInfected none) */
static void death_shake(const Enemy *e) {
  int type = e->ent != NO_ENT ? ent_at(e->ent)->s1 >> EF_DEATH_SHIFT & 15 : 0;
  if (type == 0) cam_shake(SHAKE_ENEMY_KILL);
  else if (type == 5) cam_shake(SHAKE_AVERAGE);
}

/* HealthManager.Die, EnemyDeathEffects */
static void shade_killed(Enemy *e);
static void blocker_die(Enemy *e);
static void gfly_die(Enemy *e);
static void hatchling_reset(Enemy *e);
static void hatcher_corpse_smash(const Enemy *e);
static void hornet_corpse_start(Enemy *e);
static void enemy_die(Enemy *e, float direction, bool has_direction) {
  if (FSM(e) == EF_SHADE) {
    shade_killed(e);
    return;
  }
  if (FSM(e) == EF_BLOCKER) {
    blocker_die(e);
    return;
  }
  /* the geo: SpawnAndFling, small, medium then large */
  float x = e->body.x, y = e->body.y;
  geo_fling(0, e->geo_s, x, y, 15, 30, 80, 100);
  geo_fling(1, e->geo_m, x, y, 15, 30, 80, 100);
  geo_fling(2, e->geo_l, x, y, 15, 30, 80, 100);
  if (e->ent != NO_ENT) {
    const Ent *d = ent_at(e->ent);
    persist_set(d->persist), enemy_terrain_off(d);
    if (d->s1 & EF_BATTLE) arena_enemy_died();   /* (its battleScene's Battle Enemies) */
  }
  if (FSM(e) == EF_MENDER) g_pd.mender_state = 2;   /* (Killed) */
  if (FSM(e) == EF_GFLY) gfly_die(e);
  else if (FSM(e) == EF_HORNET) hornet_corpse_start(e);
  else if (FSM(e) == EF_HATCHLING) {
    /* (deathReset: back to its cage) */
    death_shake(e);
    FREEZE_MOMENT_1();
    hatchling_reset(e);
    return;
  } else corpse_start(e, direction, has_direction);
  death_shake(e);
  FREEZE_MOMENT_1();
}

/* HealthManager.IsBlockingByDirection: invincible, from every way or from some (a spell gets through to what is Spell
 * Vulnerable) */
static bool blocking_by_direction(const Enemy *e, int dir, bool nail) {
  if (!(e->flags & 4)) return false;
  if (!nail && FSM(e) == EF_MOSSKNIGHT && (e->c4 & 0x80)) return false;
  int d = e->inv_dir;
  switch (dir) {
    case 0: return d == 0 || d == 1 || d == 5 || d == 8 || d == 10;
    case 1: return d == 0 || d == 2 || (d >= 5 && d <= 9);
    case 2: return d == 0 || d == 3 || d == 6 || d == 9 || d == 11;
    default: return d == 0 || d == 4 || (d >= 7 && d <= 11);
  }
}

/* HealthManager.Hit (a nail's): evasion, damage, recoil, flash, soul */
static void blocker_hit(Enemy *e);
static void fk_head_hit(void);
static void fk_stun(Enemy *e);
static void fk_head_stun_end(Enemy *h);
static void hornet_hit(Enemy *e);
static void enemy_hit_by(Enemy *e, float direction, int damage, bool nail, float magnitude) {
  if (e->mode != EM_ALIVE || e->evasion > 0 || damage <= 0) return;
  int dir = cardinal(direction);
  e->rc_flags = (uint8_t)((e->rc_flags & ~(3 << RF_DIR_SHIFT)) | dir << RF_DIR_SHIFT);   /* (directionOfLastAttack) */
  if (blocking_by_direction(e, dir, nail)) {
    /* Invincible: the hit blocked (BLOCKED HIT), the Knight recoiling off it (a nail's) */
    e->rc_flags |= RF_BLOCKED;
    if (nail && dir == 0) hero_recoil_left();
    else if (nail && dir == 2) hero_recoil_right();
    FREEZE_MOMENT_1();
    cam_shake(SHAKE_ENEMY_KILL);
    e->evasion = 0.15f;
    return;
  }
  recoil_by_direction(e, dir, magnitude);
  if (nail && FSM(e) != EF_SHADE && FSM(e) != EF_FK) hero_soul_gain();   /* (enemyType 3, a shade, or 6: no soul) */
  e->flashing = true, e->flash_t = 0;
  e->flags |= 8;
  e->hp = (int16_t)(e->hp - damage < -50 ? -50 : e->hp - damage);
  if ((FSM(e) == EF_BUZZER || FSM(e) == EF_SHADE || FSM(e) == EF_HUSK) && e->st == 0) e->b1 = true;   /* (TOOK DAMAGE, in Idle / Ready) */
  if (FSM(e) == EF_BLOCKER && e->hp > 0) blocker_hit(e);
  if (FSM(e) == EF_FKHEAD) fk_head_hit();   /* (its sendHitTo) */
  if (e->hp > 0) {
    e->evasion = 0.2f;
    if (FSM(e) == EF_HORNET) hornet_hit(e);
  } else if (FSM(e) == EF_FK || FSM(e) == EF_FKHEAD) {
    /* (hasSpecialDeath: ZERO HP to its FSMs, then NonFatalHit) */
    if (FSM(e) == EF_FK) fk_stun(e);
    else fk_head_stun_end(e);
    e->evasion = 0.2f;
  } else
    enemy_die(e, direction, true);
}

static void enemy_hit(Enemy *e, float direction, int damage) { enemy_hit_by(e, direction, damage, true, 1); }

/* ---------------------------------------------------------------- PlayMaker actions */
/* the hero's position (its transform) */
static float hero_x(void) { return g_hero.body.x; }
static float hero_y(void) { return g_hero.body.y; }

/* FaceDirection (sprite facing left): turns to its velocity, playing a clip, pausing between turns */
static void face_direction_p(Enemy *e, float *pause, int clip, float pause_time) {
  if (*pause > 0) {
    *pause -= DT;
    return;
  }
  float want = e->body.vx > 0 ? -fabsf(e->sx) : fabsf(e->sx);
  if (e->sx != want) {
    *pause = pause_time;
    set_scale_x(e, want);
    if (clip >= 0) anim_play_from_frame(&e->anim, clip, 0);
  }
}

static void face_direction(Enemy *e, float *pause, int clip) { face_direction_p(e, pause, clip, 0.5f); }

/* LineOfSightDetector, AlertRange: the hero in the circle, and no terrain between */
static void sight_update(Enemy *e) {
  float cx = g_hero.body.x + g_hero.body.ox, cy = g_hero.body.y + g_hero.body.oy;
  float ax = e->body.x + e->ar_x, ay = e->body.y + e->ar_y;
  if (e->ar_hy >= 0) {
    /* a box against the Knight's */
    e->in_alert = !g_hero.hidden && fabsf(cx - ax) < e->ar_r + g_hero.body.hx && fabsf(cy - ay) < e->ar_hy + g_hero.body.hy;
  } else {
    /* a circle against the Knight's box */
    float qx = ax < cx - g_hero.body.hx ? cx - g_hero.body.hx : ax > cx + g_hero.body.hx ? cx + g_hero.body.hx : ax;
    float qy = ay < cy - g_hero.body.hy ? cy - g_hero.body.hy : ay > cy + g_hero.body.hy ? cy + g_hero.body.hy : ay;
    e->in_alert = !g_hero.hidden && e->ar_r > 0 && (qx - ax) * (qx - ax) + (qy - ay) * (qy - ay) < e->ar_r * e->ar_r;
  }
  if (!e->in_alert) {
    e->can_see = false;
    return;
  }
  float dx = hero_x() - e->body.x, dy = hero_y() - e->body.y, l = sqrtf(dx * dx + dy * dy);
  e->can_see = l < 1e-4f || !phys_ray(e->body.x, e->body.y, dx / l, dy / l, l, CF_TERRAIN, NULL);
}

/* ---------------------------------------------------------------- Crawler: WalkLeftRight (sprite facing left) */
enum { CR_WALK, CR_WAIT, CR_TURN };
static float crawl_dir(const Enemy *e) { return e->sx < 0 ? 1.0f : -1.0f; }

static bool crawl_ray(const Enemy *e, float x, float dx, float dy, float len) {
  float y0 = e->body.y + e->body.oy - e->body.hy;
  return phys_ray(x, y0 + 0.5f, dx, dy, len, CF_TERRAIN, NULL);
}

static void crawler_start(Enemy *e, const Ent *d) {
  if (d->s1 & 1) {
    e->st = CR_WAIT;   /* (First Crawler: waits for GO LEFT or GO RIGHT) */
    anim_play(&e->anim, CLIP_CRAWLER_WALK);
    return;
  }
  e->st = CR_WALK;
  e->b0 = rand_range(0, 100) <= 50;   /* (shouldTurn: half the time) */
  anim_play(&e->anim, CLIP_CRAWLER_WALK);
}

static void crawler_fixed(Enemy *e) {
  if (e->st == CR_TURN) {
    e->t0 -= DT;
    if (e->t0 <= 0) {
      set_scale_x(e, -e->sx);
      anim_play(&e->anim, CLIP_CRAWLER_WALK);
      e->st = CR_WALK;
    }
    return;
  }
  if (e->st != CR_WALK) return;
  float dir = crawl_dir(e);
  e->body.vx = 4 * dir;
  float cx = e->body.x + e->body.ox, w = e->body.hx;
  bool grounded = crawl_ray(e, cx, 0, -1, 1);
  bool wall = crawl_ray(e, cx, dir, 0, w + 0.1f);
  bool edge = !crawl_ray(e, cx + (w + 0.1f) * dir, 0, -1, 1);
  if (e->b0 || (grounded && (wall || edge) && g_game.time >= e->t1)) {
    e->b0 = false;
    e->t1 = g_game.time + 1;   /* (turnDelay) */
    e->body.vx = 0;
    anim_play(&e->anim, CLIP_CRAWLER_TURN);
    e->t0 = clip_duration(CLIP_CRAWLER_TURN);
    e->st = CR_TURN;
  }
}

/* ---------------------------------------------------------------- Buzzer: the chaser FSM */
enum { BZ_IDLE, BZ_STARTLE, BZ_CHASE, BZ_STOP };

static void buzzer_idle_enter(Enemy *e) {
  e->st = BZ_IDLE;
  anim_play(&e->anim, CLIP_BUZZER_IDLE);
  e->start_x = e->body.x, e->start_y = e->body.y;   /* (IdleBuzz) */
  e->wait = 0, e->ax = e->ay = 0;
}

static void chase_start(Enemy *e) {
  anim_play_from_frame(&e->anim, CLIP_BUZZER_CHASE, 3);
  e->st = BZ_CHASE, e->t0 = 0;
}

static void buzzer_alerted(Enemy *e, const Ent *d) {
  if (d->s1 & 2) {
    /* Startle: still, facing the hero, till the clip ends */
    e->st = BZ_STARTLE;
    e->body.vx = e->body.vy = 0;
    anim_play(&e->anim, CLIP_BUZZER_STARTLE);
    set_scale_x(e, hero_x() > e->body.x ? -fabsf(e->sx) : fabsf(e->sx));
  } else
    chase_start(e);
}

static void buzzer_start(Enemy *e, const Ent *d) {
  if (d->s1 & 1) chase_start(e);
  else buzzer_idle_enter(e);
}

/* IdleBuzz: drifts about its start, accelerating at random */
static void idle_buzz_p(Enemy *e, float wmin, float wmax, float vmax, float amax, float range) {
  float vx = e->body.vx, vy = e->body.vy, x = e->body.x, y = e->body.y;
  if (y < e->start_y - range) {
    if (vy < 0) e->ay = amax / 2000, vy /= 1.125f, e->wait = rand_range(wmin, wmax);
  } else if (y > e->start_y + range && vy > 0)
    e->ay = -amax / 2000, vy /= 1.125f, e->wait = rand_range(wmin, wmax);
  if (x < e->start_x - range) {
    if (vx < 0) e->ax = amax / 2000, vx /= 1.125f, e->wait = rand_range(wmin, wmax);
  } else if (x > e->start_x + range && vx > 0)
    e->ax = -amax / 2000, vx /= 1.125f, e->wait = rand_range(wmin, wmax);
  if (e->wait <= 1.1920929e-7f) {
    e->ay = y < e->start_y - range ? rand_range(0, amax) : y > e->start_y + range ? rand_range(-amax, 0) : rand_range(-amax, amax);
    e->ax = x < e->start_x - range ? rand_range(0, amax) : x > e->start_x + range ? rand_range(-amax, 0) : rand_range(-amax, amax);
    e->ay /= 2000, e->ax /= 2000;
    e->wait = rand_range(wmin, wmax);
  }
  if (e->wait > 1.1920929e-7f) e->wait -= DT;
  vx += e->ax, vy += e->ay;
  e->body.vx = vx > vmax ? vmax : vx < -vmax ? -vmax : vx;
  e->body.vy = vy > vmax ? vmax : vy < -vmax ? -vmax : vy;
}

static void idle_buzz(Enemy *e) { idle_buzz_p(e, 0.75f, 1, 1.75f, 15, 1); }

/* ChaseObject: towards the hero, a little faster each step */
static void chase_object(Enemy *e, float vmax, float accel) {
  float vx = e->body.vx + (e->body.x < hero_x() ? accel : -accel);
  float vy = e->body.vy + (e->body.y < hero_y() ? accel : -accel);
  e->body.vx = vx > vmax ? vmax : vx < -vmax ? -vmax : vx;
  e->body.vy = vy > vmax ? vmax : vy < -vmax ? -vmax : vy;
}

static void decelerate(Enemy *e, float d) {
  float vx = e->body.vx, vy = e->body.vy;
  vx = vx < 0 ? (vx + d > 0 ? 0 : vx + d) : vx > 0 ? (vx - d < 0 ? 0 : vx - d) : 0;
  vy = vy < 0 ? (vy + d > 0 ? 0 : vy + d) : vy > 0 ? (vy - d < 0 ? 0 : vy - d) : 0;
  e->body.vx = vx, e->body.vy = vy;
}

static void buzzer_fixed(Enemy *e) {
  if (e->st == BZ_IDLE) idle_buzz(e);
  else if (e->st == BZ_CHASE) chase_object(e, 5, 0.045f);
  else if (e->st == BZ_STOP) decelerate(e, 0.12f);
}

static void buzzer_update(Enemy *e, const Ent *d) {
  switch (e->st) {
    case BZ_IDLE:
      face_direction(e, &e->pause0, CLIP_BUZZER_TURNTOIDLE);
      if ((e->can_see && e->in_alert) || e->b1) {
        e->b1 = false;
        buzzer_alerted(e, d);
      }
      break;
    case BZ_STARTLE:
      if (e->anim.events & ANIM_DONE) chase_start(e);
      break;
    case BZ_CHASE:
      /* (In Sight and Out of Sight in turn: the attention span counts while it cannot see the hero) */
      face_direction(e, &e->pause1, CLIP_BUZZER_TURNTOFLY);
      if (e->can_see && e->in_alert) e->t0 = 0;
      else if ((e->t0 += DT) >= 10) {
        e->st = BZ_STOP, e->t0 = 0;
        anim_play_from_frame(&e->anim, CLIP_BUZZER_IDLE, 3);
      }
      break;
    case BZ_STOP:
      if ((e->t0 += DT) >= 1) buzzer_idle_enter(e);
      break;
  }
}

/* ---------------------------------------------------------------- Walker */
enum { WK_NOT_READY, WK_WAITING, WK_STOPPED, WK_WALKING, WK_TURNING };
enum { STOP_BORED, STOP_CONTROLLED };

static const Ent *walker_rec(const Enemy *e) {
  int n;
  return e->wk_rec ? &room_ents(&n)[e->wk_rec] : NULL;
}

/* (its transform's x scale: facing times rightScale) */
static void walker_scale(Enemy *e, int facing) {
  const Ent *w = walker_rec(e);
  if (w->a & WF_NO_SCALE) return;
  float rs = (w->a & WF_RIGHT_NEG) ? -1.0f : 1.0f;
  set_scale_x(e, (float)facing * rs * fabsf(e->sx));
}

static void walker_begin_walking(Enemy *e, int facing) {
  const Ent *w = walker_rec(e);
  e->wk_state = WK_WALKING;
  anim_play(&e->anim, CLIP(e, R_WALK));
  walker_scale(e, facing);
  e->wk_walk = rand_range(w->p0, w->p1);
  e->body.vx = facing > 0 ? w->y0 : w->x0;
}

static void walker_begin_turning(Enemy *e, int facing) {
  const Ent *w = walker_rec(e);
  e->wk_state = WK_TURNING;
  e->wk_turning = (int8_t)facing;
  if (w->a & WF_PREVENT_TURN) {
    e->wk_facing = e->wk_turning;
    walker_begin_walking(e, e->wk_facing);
    return;
  }
  e->wk_turn_cd = w->p2;
  e->body.vx = 0;
  anim_play_from_frame(&e->anim, CLIP(e, R_TURN), 0);
}

static void walker_walk_or_turn(Enemy *e, int facing) {
  if (e->wk_facing == facing) walker_begin_walking(e, facing);
  else walker_begin_turning(e, facing);
}

static void walker_end_stopping(Enemy *e) {
  const Ent *w = walker_rec(e);
  if (e->wk_facing == 0) walker_walk_or_turn(e, rand_range(0, 2) < 1 ? 1 : -1);
  else if (rand_range(0, 100) < w->group) walker_begin_turning(e, -e->wk_facing);
  else walker_begin_walking(e, e->wk_facing);
}

static void walker_stop(Enemy *e, int reason) {
  const Ent *w = walker_rec(e);
  e->wk_state = WK_STOPPED, e->wk_stop = (uint8_t)reason;
  if (reason == STOP_BORED) {
    anim_play(&e->anim, CLIP(e, R_IDLE));
    e->body.vx = 0;
    if (w->a & WF_PAUSES) e->wk_pause = rand_range(w->x1, w->y1);
    else walker_end_stopping(e);
  }
}

static void walker_update(Enemy *e);
/* StartMoving */
static void walker_start(Enemy *e) {
  if (e->wk_state == WK_STOPPED || e->wk_state == WK_WAITING) {
    int facing = e->wk_facing ? e->wk_facing : (rand_range(0, 2) < 1 ? 1 : -1);
    walker_walk_or_turn(e, facing);
  }
  walker_update(e);
}

/* (Start: its facing from its scale, then waiting for its conditions) */
static void walker_init(Enemy *e) {
  const Ent *w = walker_rec(e);
  float rs = (w->a & WF_RIGHT_NEG) ? -1.0f : 1.0f;
  e->wk_facing = e->sx * rs >= 0 ? 1 : -1;
  e->wk_turn_cd = -1e-6f;
  e->wk_state = WK_WAITING;
}

/* Sweep.Check: rays from a side of the collider, from (x, y), along a cardinal direction: is there terrain within? */
static bool sweep(const Enemy *e, float x, float y, int dir, float dist) {
  float dx = (float)(dir == 0) - (float)(dir == 2), dy = (float)(dir == 1) - (float)(dir == 3);
  float bx = e->body.ox + e->body.hx * dx, by = e->body.oy + e->body.hy * dy;
  for (int i = 0; i < 3; i++) {
    float f = (float)i - 1;
    float rx = x + bx + e->body.hx * fabsf(dy) * f - dx * 0.1f, ry = y + by + e->body.hy * fabsf(dx) * f - dy * 0.1f;
    PhysHit hit;
    if (phys_ray(rx, ry, dx, dy, dist + 0.1f, CF_TERRAIN, &hit) && hit.dist - 0.1f < dist) return true;
  }
  return false;
}

static void walker_update(Enemy *e) {
  const Ent *w = walker_rec(e);
  if (!w) return;
  e->wk_turn_cd -= DT;
  switch (e->wk_state) {
    case WK_WAITING: {
      /* (near the camera; at the hero's x if it waits for that; not an ambush nor inactive) */
      bool hero_x_ok = !(w->a & WF_WAIT_HERO_X) || fabsf(hero_x() - w->s1 / 16.0f) < 1;
      if (hero_x_ok && !(w->a & (WF_START_INACTIVE | WF_AMBUSH))) {
        walker_stop(e, STOP_BORED);
        walker_start(e);
      }
      break;
    }
    case WK_STOPPED:
      if (e->wk_stop == STOP_BORED && (e->wk_pause -= DT) <= 0) walker_end_stopping(e);
      break;
    case WK_WALKING:
      if (e->wk_turn_cd <= 0) {
        float ext = e->body.hx;
        if (sweep(e, e->body.x, e->body.y, 1 - e->wk_facing, ext + 0.5f)) {
          walker_begin_turning(e, -e->wk_facing);
          return;
        }
        if (!(w->a & WF_NO_TURN_TO_HERO) && (hero_x() > e->body.x) != (e->wk_facing > 0) && e->can_see && e->in_alert) {
          walker_begin_turning(e, -e->wk_facing);
          return;
        }
        if (!(w->a & WF_IGNORE_HOLES) &&
            !sweep(e, e->body.x + (ext + 0.5f + w->p3) * e->wk_facing, e->body.y, 3, 0.25f)) {
          walker_begin_turning(e, -e->wk_facing);
          return;
        }
      }
      if ((w->a & WF_PAUSES) && (e->wk_walk -= DT) <= 0) {
        walker_stop(e, STOP_BORED);
        return;
      }
      e->body.vx = e->wk_facing > 0 ? w->y0 : w->x0;
      break;
    case WK_TURNING:
      e->body.vx = 0;
      if (!e->anim.playing) {
        e->wk_facing = e->wk_turning;
        walker_begin_walking(e, e->wk_facing);
      }
      break;
  }
}

/* ---------------------------------------------------------------- Tiktiks: Climber, round the terrain's edges */
enum { CL_WALK, CL_TURN, CL_STUN };
enum { DIR_RIGHT, DIR_DOWN, DIR_LEFT, DIR_UP };

/* its collider at its rotation (quarter turns) */
static void climber_box(Enemy *e) {
  const Ent *c = enemy_rec(e, ET_COLLIDER);
  if (!c) return;
  float ox = c->x0 * (e->sx < 0 ? -1 : 1), oy = c->y0;
  int q = ((int)lrintf(e->ang / 90) % 4 + 4) % 4;
  float rx = q == 0 ? ox : q == 1 ? -oy : q == 2 ? -ox : oy, ry = q == 0 ? oy : q == 1 ? ox : q == 2 ? -oy : -ox;
  e->body.ox = rx, e->body.oy = ry;
  e->body.hx = q & 1 ? c->y1 : c->x1, e->body.hy = q & 1 ? c->x1 : c->y1;
}

/* FireRayLocal: from its collider's center along a direction of its own */
static bool climber_ray(const Enemy *e, float lx, float ly, float len) {
  const Ent *c = enemy_rec(e, ET_COLLIDER);
  float a = e->ang * (float)M_PI / 180, ca = cosf(a), sa = sinf(a);
  float ox = c ? c->x0 * (e->sx < 0 ? -1 : 1) : 0, oy = c ? c->y0 : 0;
  float x = e->body.x + ox * ca - oy * sa, y = e->body.y + ox * sa + oy * ca;
  return phys_ray(x, y, lx * ca - ly * sa, lx * sa + ly * ca, len, CF_TERRAIN, NULL);
}

static void climber_velocity(Enemy *e) {
  const Ent *v = enemy_rec(e, ET_VARS);
  float sp = v ? v->p0 : 2;
  int d = e->wk_facing;
  e->body.vx = d == DIR_RIGHT ? sp : d == DIR_LEFT ? -sp : 0;
  e->body.vy = d == DIR_UP ? sp : d == DIR_DOWN ? -sp : 0;
}

static void climber_walk(Enemy *e) {
  e->st = CL_WALK;
  anim_play(&e->anim, CLIP(e, R_WALK));
  climber_velocity(e);
}

static void climber_start(Enemy *e) {
  const Ent *v = enemy_rec(e, ET_VARS);
  e->ang = v ? 90.0f * v->s1 : 0;
  climber_box(e);
  /* StickToGround: onto the ground below, within 2 */
  const Ent *c = enemy_rec(e, ET_COLLIDER);
  float a = e->ang * (float)M_PI / 180, ca = cosf(a), sa = sinf(a);
  float ox = c ? c->x0 * (e->sx < 0 ? -1 : 1) : 0, oy = c ? c->y0 : 0;
  PhysHit hit;
  if (phys_ray(e->body.x + ox * ca - oy * sa, e->body.y + ox * sa + oy * ca, sa, -ca, 2, CF_TERRAIN, &hit))
    e->body.x = hit.x, e->body.y = hit.y;
  float k = e->sx < 0 ? -1.0f : 1.0f;
  if (v && !(v->a & 1)) k = -k;
  e->b0 = k > 0;   /* (clockwise) */
  float r = fmodf(e->ang, 360);
  if (r < 0) r += 360;
  if (r >= 45 && r <= 135) e->wk_facing = e->b0 ? DIR_UP : DIR_DOWN;
  else if (r >= 135 && r <= 225) e->wk_facing = e->b0 ? DIR_LEFT : DIR_RIGHT;
  else if (r >= 225 && r <= 315) e->wk_facing = e->b0 ? DIR_DOWN : DIR_UP;
  else e->wk_facing = e->b0 ? DIR_RIGHT : DIR_LEFT;
  e->start_x = e->body.x, e->start_y = e->body.y;
  e->ax = e->body.x, e->ay = e->body.y;   /* (previousTurnPos: none yet, its place) */
  e->ax = 1e9f;
  climber_walk(e);
}

static void climber_turn(Enemy *e, bool cw, bool tween) {
  const Ent *c = enemy_rec(e, ET_COLLIDER), *v = enemy_rec(e, ET_VARS);
  float w = c ? c->x1 : 0.5f, h = c ? c->y1 : 0.5f, pad = v ? v->p2 : 0.1f;   /* (half sizes) */
  e->st = CL_TURN, e->t0 = 0;
  e->body.vx = e->body.vy = 0;
  e->pause0 = e->ang, e->pause1 = e->ang + (cw ? -90.0f : 90.0f);
  e->qx = e->body.x, e->qy = e->body.y, e->b1 = tween;
  float tx = 0, ty = 0;   /* GetTweenPos */
  switch (e->wk_facing) {
    case DIR_RIGHT: tx = w + pad, ty = e->b0 ? h : -h; break;
    case DIR_UP: tx = e->b0 ? -w : w, ty = h + pad; break;
    case DIR_DOWN: tx = e->b0 ? w : -w, ty = -h - pad; break;
    case DIR_LEFT: tx = -w - pad, ty = e->b0 ? -h : h; break;
  }
  e->tx = e->body.x + tx, e->ty = e->body.y + ty;
  e->wait = cw ? 1 : -1;
}

static void climber_stun(Enemy *e) {
  if (e->st == CL_TURN) return;
  e->st = CL_STUN;
  e->body.vx = e->body.vy = 0;
  anim_play_from_frame(&e->anim, CLIP(e, R_A1), 0);
}

static void climber_update(Enemy *e) {
  const Ent *v = enemy_rec(e, ET_VARS);
  switch (e->st) {
    case CL_WALK: {
      /* (constrain: held to its line) */
      bool moved = false;
      if (fabsf(e->body.x - e->start_x) > 0.1f) e->body.x = e->start_x, moved = true;
      if (fabsf(e->body.y - e->start_y) > 0.1f) e->body.y = e->start_y, moved = true;
      if (!moved) e->start_x = e->body.x, e->start_y = e->body.y;
      float dx = e->body.x - e->ax, dy = e->body.y - e->ay;
      if (e->ax > 1e8f || dx * dx + dy * dy >= (v ? v->p3 * v->p3 : 0.0625f)) {
        const Ent *c = enemy_rec(e, ET_COLLIDER);
        float w = c ? c->x1 : 0.5f, pad = v ? v->p2 : 0.1f;
        if (!climber_ray(e, 0, -1, 1)) climber_turn(e, e->b0, false);
        else if (climber_ray(e, e->b0 ? 1 : -1, 0, w + pad)) climber_turn(e, !e->b0, true);
      }
      break;
    }
    case CL_TURN: {
      float spin = v ? v->p1 : 0.25f;
      e->t0 += DT;
      float t = e->t0 >= spin ? 1 : e->t0 / spin;
      e->ang = e->pause0 + (e->pause1 - e->pause0) * t;
      if (e->b1) e->body.x = e->qx + (e->tx - e->qx) * t, e->body.y = e->qy + (e->ty - e->qy) * t;
      if (e->t0 >= spin) {
        e->ang = e->pause1;
        e->wk_facing = (int8_t)((e->wk_facing + (e->wait > 0 ? 1 : -1) + 4) % 4);
        climber_box(e);
        e->start_x = e->ax = e->body.x, e->start_y = e->ay = e->body.y;
        climber_walk(e);
      }
      break;
    }
    case CL_STUN:
      if (e->anim.events & ANIM_DONE) climber_walk(e);
      break;
  }
}

/* ---------------------------------------------------------------- Gruzzers: the Bouncer Control FSM */
enum { BO_WAIT, BO_FLY, BO_STOPPED };

static void bouncer_aim(Enemy *e, float lo, float hi) {
  e->wait = rand_range(lo, hi);   /* (Angle) */
  float a = fmodf(e->wait, 360);
  if (a < 0) a += 360;
  e->b0 = a < 90 || a >= 270;     /* (Left or Right?: Facing Right) */
  e->st = BO_FLY;
}

static void bouncer_start(Enemy *e) {
  const Ent *v = enemy_rec(e, ET_VARS);
  e->st = v && (v->a & 1) ? BO_STOPPED : BO_WAIT;
  anim_play(&e->anim, CLIP(e, R_A1));
}

/* CheckCollisionSide: three short rays from a side of its collider (0 right, 1 top, 2 left, 3 bottom) */
static bool side_hit(const Enemy *e, int side) {
  float x0, y0, x1, y1;
  enemy_box(e, &x0, &y0, &x1, &y1);
  const float d = 0.08f;
  float dx = side == 0 ? 1 : side == 2 ? -1 : 0, dy = side == 1 ? 1 : side == 3 ? -1 : 0;
  float x = dx > 0 ? x1 : x0, y = dy > 0 ? y1 : y0;
  for (int i = 0; i < 3; i++) {
    float k = (float)i / 2;
    float rx = dx ? x : x0 + (x1 - x0) * k, ry = dy ? y : y0 + (y1 - y0) * k;
    if (phys_ray(rx, ry, dx, dy, d, CF_TERRAIN, NULL)) return true;
  }
  return false;
}

/* (the side it bumped, top first) */
static int bouncer_side(const Enemy *e) {
  static const int order[4] = {1, 0, 3, 2};
  for (int i = 0; i < 4; i++)
    if (side_hit(e, order[i])) return order[i];
  return -1;
}

static void bouncer_fixed(Enemy *e) {
  if (e->st != BO_FLY) return;
  const Ent *v = enemy_rec(e, ET_VARS);
  float sp = v ? v->p0 : 5.2f, a = e->wait * (float)M_PI / 180;
  e->body.vx = cosf(a) * sp, e->body.vy = sinf(a) * sp;   /* (SetVelocityAsAngle) */
}

static void bouncer_update(Enemy *e) {
  switch (e->st) {
    case BO_WAIT: {
      /* (GetDistance to the main camera, its depth included) */
      float dx = e->body.x - g_cam_x, dy = e->body.y - g_cam_y, dz = 0.01f - CAM_Z;
      if (dx * dx + dy * dy + dz * dz < 44 * 44) {
        const Ent *v = enemy_rec(e, ET_VARS);
        if (v && (v->a & 2)) bouncer_aim(e, 90, 90);   /* (Start Up) */
        else bouncer_aim(e, 0, 360);
      }
      break;
    }
    case BO_FLY: {
      /* FaceDirection (sprite facing left), no pause */
      float want = e->body.vx > 0 ? -fabsf(e->sx) : fabsf(e->sx);
      if (e->body.vx != 0 && e->sx != want) set_scale_x(e, want);
      if (!e->body.ncontacts) break;
      float a = fmodf(e->wait, 360);
      if (a < 0) a += 360;
      switch (bouncer_side(e)) {
        case 1: if (e->b0) bouncer_aim(e, 320, 350); else bouncer_aim(e, 190, 220); break;   /* (Hit Up) */
        case 3: if (e->b0) bouncer_aim(e, 10, 40); else bouncer_aim(e, 140, 170); break;     /* (Hit Down) */
        case 0: if (a < 180) bouncer_aim(e, 140, 170); else bouncer_aim(e, 190, 220); break;  /* (Hit Right) */
        case 2: if (a < 180) bouncer_aim(e, 10, 40); else bouncer_aim(e, 320, 350); break;    /* (Hit Left) */
      }
      break;
    }
  }
}

/* DistanceFly: keeps a distance from the hero (targeting its height, or its distance in y too) */
static void distance_fly_at(Enemy *e, float dist, float vmax, float accel, bool height, float hoff) {
  float vx = e->body.vx, vy = e->body.vy;
  float dx = e->body.x - hero_x(), dy = e->body.y - hero_y(), d = sqrtf(dx * dx + dy * dy);
  bool left = e->body.x < hero_x(), below = e->body.y < hero_y();
  vx += (d > dist) == left ? accel : -accel;
  if (!height) vy += (d > dist) == below ? accel : -accel;
  else {
    if (e->body.y < hero_y() + hoff) vy += accel;
    if (e->body.y > hero_y() + hoff) vy -= accel;
  }
  e->body.vx = vx > vmax ? vmax : vx < -vmax ? -vmax : vx;
  e->body.vy = vy > vmax ? vmax : vy < -vmax ? -vmax : vy;
}

/* ---------------------------------------------------------------- enemies' shots (EnemyBullet) */
#define MAX_BULLETS 8
typedef struct {
  bool on, active;
  uint8_t kind;   /* (0 a spit shot, 1 a Moss Knight's grass ball) */
  float x, y, vx, vy, scale, ang, gravity;
  float spin, age;   /* (a grass ball's: degrees a second, its time) */
  Anim anim;
} Bullet;
static Bullet bullets[MAX_BULLETS];
#define BULLET_HX (0.6406f / 2)
#define BULLET_HY (0.5625f / 2)

/* a shot from (x, y) at a velocity, falling at a gravity scale */
static Bullet *bullet_new(float x, float y, float vx, float vy, float gravity) {
  Bullet *b = NULL;
  for (int i = 0; i < MAX_BULLETS && !b; i++)
    if (!bullets[i].on) b = &bullets[i];
  if (!b) return NULL;
  memset(b, 0, sizeof *b);
  b->on = b->active = true;
  b->x = x, b->y = y, b->vx = vx, b->vy = vy, b->gravity = gravity;
  b->scale = rand_range(1.15f, 1.45f);
  anim_play(&b->anim, CLIP_BULLET_IDLE);
  return b;
}

/* FireAtTarget: from (x, y) at the hero, at a speed */
static void distance_fly(Enemy *e, float dist, float vmax, float accel, bool height) {
  distance_fly_at(e, dist, vmax, accel, height, 0);
}

static void bullet_fire(float x, float y, float speed) {
  Bullet *b = NULL;
  for (int i = 0; i < MAX_BULLETS && !b; i++)
    if (!bullets[i].on) b = &bullets[i];
  if (!b) return;
  memset(b, 0, sizeof *b);
  b->on = b->active = true;
  b->x = x, b->y = y;
  b->scale = rand_range(1.15f, 1.45f) * 0.7f;   /* (its random scale, on the prefab's) */
  b->gravity = 0.05f;
  float dx = hero_x() - x, dy = hero_y() - y, l = sqrtf(dx * dx + dy * dy);
  if (l < 1e-4f) l = 1, dx = 1;
  b->vx = dx / l * speed, b->vy = dy / l * speed;
  anim_play(&b->anim, CLIP_BULLET_IDLE);
}

/* a Moss Knight's Grass Ball (grass ball control, SpinSelf): from (x, y) at a velocity, falling */
#define GRASS_BALL_R 0.4f
static void grass_ball_spawn(float x, float y, float vx, float vy) {
  Bullet *b = bullet_new(x, y, vx, vy, 1);
  if (!b) return;
  b->kind = 1;
  b->ang = rand_range(0, 360);
  b->spin = vx * 15 / (0.5f * GRASS_BALL_R * GRASS_BALL_R) * DT * 180 / (float)M_PI;   /* (its one push of torque) */
}

static void bullet_impact(Bullet *b) {
  if (b->kind == 1) {
    /* (Break: gone, with a shake) */
    cam_shake(SHAKE_ENEMY_KILL);
    b->on = false;
    return;
  }
  b->active = false;
  b->vx = b->vy = 0;
  anim_play_from_frame(&b->anim, CLIP_BULLET_IMPACT, 0);
}

static void bullets_tick(void) {
  for (int i = 0; i < MAX_BULLETS; i++) {
    Bullet *b = &bullets[i];
    if (!b->on) continue;
    b->anim.events = 0;
    anim_update(&b->anim, DT);
    if (!b->active) {
      if (b->anim.events & ANIM_DONE) b->on = false;
      continue;
    }
    b->vy += -60 * b->gravity * DT;   /* (its Rigidbody2D's gravity) */
    float dx = b->vx * DT, dy = b->vy * DT, l = sqrtf(dx * dx + dy * dy);
    PhysHit hit;
    if (b->kind == 1) {
      /* (its circle meets the terrain: Break; its scale tweened up from half in 0.1 s) */
      b->age += DT;
      b->scale = b->age < 0.1f ? 0.5f + 5 * b->age : 1;
      b->ang += b->spin * DT;
      b->x += dx, b->y += dy;
      if (phys_ray(b->x - GRASS_BALL_R, b->y, 1, 0, 2 * GRASS_BALL_R, CF_TERRAIN, NULL) ||
          phys_ray(b->x, b->y - GRASS_BALL_R, 0, 1, 2 * GRASS_BALL_R, CF_TERRAIN, NULL))
        bullet_impact(b);
      if (b->y < -10) b->on = false;
      continue;
    }
    if (l > 0 && phys_ray(b->x, b->y, dx / l, dy / l, l + BULLET_HX, CF_SOLID, &hit)) {
      b->x = hit.x - dx / l * BULLET_HX, b->y = hit.y - dy / l * BULLET_HX;
      b->ang = atan2f(hit.ny, hit.nx) * 180 / (float)M_PI + 90;
      bullet_impact(b);
      continue;
    }
    b->x += dx, b->y += dy;
    b->ang = atan2f(b->vy, b->vx) * 180 / (float)M_PI;
    if (b->y < -10) b->on = false;
  }
}

/* (a shot that touched the HeroBox bursts there) */
static int bullets_touch_hero(float x0, float y0, float x1, float y1, int *side) {
  for (int i = 0; i < MAX_BULLETS; i++) {
    Bullet *b = &bullets[i];
    if (!b->on || !b->active) continue;
    float hx = BULLET_HX * b->scale, hy = BULLET_HY * b->scale;
    if (b->kind == 1) hx = hy = GRASS_BALL_R * b->scale;
    if (x1 > b->x - hx && x0 < b->x + hx && y1 > b->y - hy && y0 < b->y + hy) {
      *side = b->vx < 0 ? SIDE_RIGHT : SIDE_LEFT;
      bullet_impact(b);
      return 1;
    }
  }
  return 0;
}

static void bullets_draw(void) {
  for (int i = 0; i < MAX_BULLETS; i++) {
    const Bullet *b = &bullets[i];
    if (!b->on) continue;
    Inst in;
    if (b->kind == 1) {
      sprite_inst_rot(SPRITE_GRASS_BALL, b->x, b->y, -0.01f, b->scale, b->scale, b->ang, 0, &in);
      gfx_actor(&in, SORT_KEY(0, 0));
      continue;
    }
    /* (stretched along its flight) */
    float sp = sqrtf(b->vx * b->vx + b->vy * b->vy), sy = 1 - sp * 1.2f * 0.01f, sx = 1 + sp * 1.2f * 0.01f;
    if (!b->active) sx = sy = 1;
    sprite_inst_rot(b->anim.sprite, b->x, b->y, -0.01f, sx * b->scale, sy * b->scale, b->ang, 0, &in);
    gfx_actor(&in, SORT_KEY(0, 0));
  }
}

/* ---------------------------------------------------------------- Aspid Hunters: the spitter FSM */
enum { SP_IDLE, SP_DISTANCE_FLY, SP_RAYCAST, SP_FLY_BACK, SP_FIRE_ANTIC, SP_FIRE, SP_DRIBBLE };

static bool spitter_unalert_range(const Enemy *e) {
  /* the hero in its Unalert Range (its circle) */
  const Ent *d = ent_at(e->ent);
  for (int i = 1; i <= d->s0; i++) {
    const Ent *r = &d[i];
    if (r->type != ENT_BOX || r->flags != ET_RANGE) continue;
    float dx = hero_x() - (e->body.x + r->x0 * (e->sx < 0 ? -1 : 1)), dy = hero_y() - (e->body.y + r->y0);
    return dx * dx + dy * dy < r->x1 * r->x1;
  }
  return true;
}

static void spitter_idle(Enemy *e) {
  e->st = SP_IDLE;
  anim_play_from_frame(&e->anim, CLIP(e, R_A1), 2);
  e->start_x = e->body.x, e->start_y = e->body.y;
  e->wait = 0, e->ax = e->ay = 0;
}

static void spitter_distance_fly(Enemy *e) {
  e->st = SP_DISTANCE_FLY;
  anim_play_from_frame(&e->anim, CLIP(e, R_A1), 2);
  e->t0 = rand_range(1.5f, 2.25f);
}

static void spitter_start(Enemy *e, const Ent *d) {
  spitter_idle(e);
  if (d->s1 & 1) spitter_distance_fly(e);   /* (startAlert) */
}

static void spitter_face(Enemy *e, bool turn_clip) {
  float want = hero_x() > e->body.x ? -fabsf(e->sx) : fabsf(e->sx);
  if (e->sx != want) {
    set_scale_x(e, want);
    if (turn_clip) anim_play_from_frame(&e->anim, CLIP(e, R_A2), 0);
  }
}

static void spitter_fixed(Enemy *e) {
  switch (e->st) {
    case SP_IDLE: idle_buzz(e); break;
    case SP_DISTANCE_FLY:
    case SP_RAYCAST: distance_fly(e, 7, 4, 0.1f, false); break;
    case SP_FLY_BACK: distance_fly(e, 8.25f, 4, 0.1f, false); break;
    case SP_FIRE_ANTIC: distance_fly(e, 9, 2, 0.1f, false); break;
    default: break;
  }
}

static void spitter_update(Enemy *e) {
  /* the range out timer: out of its unalert range 8 s, it gives up */
  if (e->st >= SP_DISTANCE_FLY && e->st <= SP_RAYCAST) {
    if (spitter_unalert_range(e)) e->t1 = 0;
    else if ((e->t1 += DT) > 8) {
      spitter_idle(e);
      return;
    }
  }
  switch (e->st) {
    case SP_IDLE:
      face_direction(e, &e->pause0, CLIP(e, R_A2));
      /* (ALERT: it sees the Knight in its alert range, or it starts alert) */
      if ((e->can_see && e->in_alert) || (e->ent != NO_ENT && (ent_at(e->ent)->s1 & EF_START))) spitter_distance_fly(e);
      break;
    case SP_DISTANCE_FLY:
      spitter_face(e, true);
      if ((e->t0 -= DT) <= 0) {
        /* Raycast: near enough, nothing between: fire */
        float dx = hero_x() - e->body.x, dy = hero_y() - e->body.y, l = sqrtf(dx * dx + dy * dy);
        if (l > 14) spitter_distance_fly(e);
        else {
          bool blocked = l > 1e-4f && phys_ray(e->body.x, e->body.y, dx / l, dy / l, l, CF_TERRAIN, NULL);
          if (blocked) spitter_distance_fly(e);
          else e->st = SP_FLY_BACK, e->t0 = 0.5f;
        }
      }
      break;
    case SP_FLY_BACK:
      spitter_face(e, true);
      if ((e->t0 -= DT) <= 0) {
        e->st = SP_FIRE_ANTIC;
        anim_play_from_frame(&e->anim, CLIP(e, R_A3), 0);
      }
      break;
    case SP_FIRE_ANTIC:
      spitter_face(e, false);
      if (e->anim.events & ANIM_TRIGGER) {
        /* Fire: its shot at the hero */
        bullet_fire(e->body.x, e->body.y, 15);
        spitter_face(e, false);
        e->st = SP_DRIBBLE;
      }
      break;
    case SP_DRIBBLE:
      if ((e->anim.events & ANIM_DONE) || !e->anim.playing) spitter_distance_fly(e);
      break;
  }
}

/* ---------------------------------------------------------------- spawned enemies (no record of the room's) */
static Enemy *enemy_spawn(int kind, float x, float y, int hp) {
  Enemy *e = NULL;
  for (int i = 0; i < MAX_ENEMIES && !e; i++)
    if (en[i].mode == EM_OFF) e = &en[i];
  if (!e) return NULL;
  memset(e, 0, sizeof *e);
  e->mode = EM_ALIVE, e->kind = (uint8_t)kind, e->ent = NO_ENT;
  e->hp = (int16_t)hp, e->damage = 1, e->sx = 1;
  e->rc_base = 15, e->rc_dur = 0.15f;
  e->body.x = x, e->body.y = y;
  e->body.friction = 0, e->body.mask = CF_SOLID;   /* (the default material, frictionless) */
  return e;
}

/* the hero in a box range of its (local: center, half size) */
static bool hero_in_box(const Enemy *e, float cx, float cy, float hx, float hy) {
  float x = e->body.x + cx * (e->sx < 0 ? -1 : 1), y = e->body.y + cy;
  float kx = g_hero.body.x + g_hero.body.ox, ky = g_hero.body.y + g_hero.body.oy;
  return !g_hero.hidden && fabsf(kx - x) < hx + g_hero.body.hx && fabsf(ky - y) < hy + g_hero.body.hy;
}

/* ---------------------------------------------------------------- Baldurs: the Roller FSM */
enum { RO_IDLE, RO_START, RO_ROLL, RO_COLLIDE, RO_AIR, RO_STOP, RO_REST };
/* (its variables, or a spawned one's) */
static float roller_var(const Enemy *e, int i) {
  static const float dflt[5] = {0.45f, 11, 2, 3, 0.5f};
  const Ent *v = enemy_rec(e, ET_VARS);
  if (!v) return dflt[i];
  return i == 0 ? v->p0 : i == 1 ? v->p1 : i == 2 ? v->p2 : i == 3 ? v->p3 : v->x0;
}

static void roller_roll(Enemy *e) {
  e->st = RO_ROLL;
  set_scale_x(e, e->b0 ? -fabsf(e->sx) : fabsf(e->sx));
  anim_play(&e->anim, CLIP(e, R_A2));
}

static void roller_start(Enemy *e) {
  e->st = RO_IDLE;
  anim_play(&e->anim, CLIP(e, R_IDLE));
}

/* (a spawned one: from the air, rolling its way) */
static void roller_spawned(Enemy *e, bool right) {
  e->b0 = right;
  set_scale_x(e, right ? -1.0f : 1.0f);
  e->body.gravity_scale = 0.8f;
  e->body.ox = -0.0156f, e->body.oy = -0.1094f, e->body.hx = e->body.hy = 1.0938f / 2;
  e->ar_x = 0, e->ar_y = 0, e->ar_r = 39.35f / 2, e->ar_hy = 1.9f / 2;
  e->wait = rand_range(roller_var(e, 2), roller_var(e, 3));
  anim_play(&e->anim, CLIP(e, R_A2));
  e->st = RO_AIR;
}

static void roller_fixed(Enemy *e) {
  if (e->st == RO_IDLE) e->body.vx = 0;
  else if (e->st == RO_ROLL) {
    float a = roller_var(e, 0), m = roller_var(e, 1), vx = e->body.vx + (e->b0 ? a : -a);
    e->body.vx = vx > m ? m : vx < -m ? -m : vx;
  }
}

static void roller_update(Enemy *e) {
  bool done = (e->anim.events & ANIM_DONE) != 0;
  if (e->flags & 2) {   /* (RECOIL HORIZONTAL: Recoil Decel) */
    e->flags &= (uint8_t)~2;
    if (e->st == RO_ROLL) e->body.vx = 0;
  }
  switch (e->st) {
    case RO_IDLE: {
      float want = hero_x() > e->body.x ? -fabsf(e->sx) : fabsf(e->sx);
      if (e->sx != want) set_scale_x(e, want), anim_play_from_frame(&e->anim, CLIP(e, R_IDLE), 0);
      if (e->can_see && e->in_alert) {
        /* Facing Check, Start */
        e->b0 = hero_x() >= e->body.x;
        set_scale_x(e, e->b0 ? -fabsf(e->sx) : fabsf(e->sx));
        anim_play_from_frame(&e->anim, CLIP(e, R_A1), 0);
        e->wait = rand_range(roller_var(e, 2), roller_var(e, 3));
        e->st = RO_START;
      }
      break;
    }
    case RO_START:
      if (done) roller_roll(e);
      break;
    case RO_ROLL: {
      if (e->body.ncontacts && side_hit(e, e->b0 ? 0 : 2)) {
        /* Collide: off the wall, up and back the other way */
        e->b0 = !e->b0;
        set_scale_x(e, fabsf(e->sx));
        float a = (e->b0 ? 65 : 115) * (float)M_PI / 180;
        e->body.vx = cosf(a) * 12, e->body.vy = sinf(a) * 12;
        e->st = RO_COLLIDE;
        break;
      }
      if ((e->wait -= DT) <= 0) {
        e->body.vx = e->body.vy = 0;
        anim_play_from_frame(&e->anim, CLIP(e, R_A3), 0);
        e->st = RO_STOP;
      }
      break;
    }
    case RO_COLLIDE:
      e->st = RO_AIR;   /* (the next frame) */
      break;
    case RO_AIR:
      e->wait -= DT;
      if (e->body.ncontacts && side_hit(e, 3)) roller_roll(e);   /* (GROUND: Land, Left or right?) */
      break;
    case RO_STOP:
      if (done) {
        anim_play(&e->anim, CLIP(e, R_IDLE));
        e->st = RO_REST, e->t0 = 0;
      }
      break;
    case RO_REST:
      if ((e->t0 += DT) >= roller_var(e, 4)) e->st = RO_IDLE;
      break;
  }
}

/* ---------------------------------------------------------------- Elder Baldurs: the Blocker Control FSM */
enum { BL_DORMANT, BL_OPEN, BL_IDLE, BL_CLOSE1, BL_CLOSE2, BL_CLOSED, BL_SHOT_ANTIC, BL_SHOT_END, BL_HIT_PAUSE, BL_HIT };
#define BL_SHOT_ORIGIN_X 2.76f
#define BL_SHOT_ORIGIN_Y 0.78f

/* the hero in its alert range (ET_ALERT) or its attack range (ET_RANGE), boxes */
static bool blocker_in(const Enemy *e, int tag) {
  const Ent *r = enemy_rec(e, tag);
  return r && hero_in_box(e, r->x0, r->y0, r->x1, r->y1);
}

static void blocker_start(Enemy *e) {
  e->flags |= 4;   /* (SetInvincible) */
  e->start_x = e->body.x, e->start_y = e->body.y;
  anim_play(&e->anim, CLIP(e, R_A1));   /* (Closed) */
  e->st = BL_DORMANT;
}

static void blocker_idle(Enemy *e) {
  e->st = BL_IDLE;
  e->body.x = e->start_x, e->body.y = e->start_y;
  anim_play(&e->anim, CLIP(e, R_IDLE));
  e->wait = rand_range(0.8f, 1.2f);
}

static void blocker_close(Enemy *e) {
  e->flags |= 4;
  e->body.x = e->start_x, e->body.y = e->start_y;
  anim_play_from_frame(&e->anim, CLIP(e, R_A3), 0);
  e->st = BL_CLOSE1;
}

static void blocker_hit(Enemy *e) {
  /* TOOK DAMAGE: Hit Pause, Hit */
  e->st = BL_HIT_PAUSE;
}

static void blocker_fire(Enemy *e) {
  const Ent *v = enemy_rec(e, ET_VARS);
  bool right = v && (v->a & 1);
  float ox = right ? BL_SHOT_ORIGIN_X : -BL_SHOT_ORIGIN_X;
  float vx = right ? rand_range(3, 15) : rand_range(-15, -1), vy = v ? v->p0 : 20;
  if (e->b1) {
    /* a roller, rolling its way */
    Enemy *r = enemy_spawn(EK_BALDUR, e->body.x + ox, e->body.y + BL_SHOT_ORIGIN_Y, 15);
    if (r) roller_spawned(r, right), r->body.vx = vx, r->body.vy = vy, r->rc_base = 25;
  } else {
    /* goop (Shot Mawlek) */
    bullet_new(e->body.x + ox, e->body.y + BL_SHOT_ORIGIN_Y, vx, vy, 0.6f);
  }
  anim_play_from_frame(&e->anim, CLIP(e, R_A6), 0);   /* (Shoot CD) */
  e->st = BL_SHOT_END;
}

static void blocker_update(Enemy *e) {
  bool done = (e->anim.events & ANIM_DONE) != 0;
  switch (e->st) {
    case BL_DORMANT:
      if (blocker_in(e, ET_ALERT)) {
        e->flags &= (uint8_t)~4;
        anim_play_from_frame(&e->anim, CLIP(e, R_A2), 0);   /* (Open) */
        e->st = BL_OPEN;
      }
      break;
    case BL_OPEN:
      if (done) blocker_idle(e);
      break;
    case BL_IDLE:
      if (blocker_in(e, ET_RANGE)) blocker_close(e);
      else if ((e->wait -= DT) <= 0) {
        /* Attack Choose: goop, or a roller if the Knight has his fireball and none is about */
        bool roller = rand_range(0, 1) < 0.5f && g_pd.fireball_level > 0;
        if (roller)
          for (int i = 0; i < MAX_ENEMIES; i++)
            if (en[i].mode == EM_ALIVE && en[i].ent == NO_ENT && FSM(&en[i]) == EF_ROLLER) roller = false;
        e->b1 = roller;
        anim_play_from_frame(&e->anim, CLIP(e, R_A5), 0);   /* (Shoot Antic) */
        e->st = BL_SHOT_ANTIC;
      }
      break;
    case BL_SHOT_ANTIC:
      if (done) blocker_fire(e);
      break;
    case BL_SHOT_END:
      if (blocker_in(e, ET_RANGE)) blocker_close(e);
      else if (done) blocker_idle(e);
      break;
    case BL_CLOSE1:
      if (done) anim_play_from_frame(&e->anim, CLIP(e, R_A4), 0), e->st = BL_CLOSE2;
      break;
    case BL_CLOSE2:
      if (done) anim_play(&e->anim, CLIP(e, R_A1)), e->st = BL_CLOSED;
      break;
    case BL_CLOSED:
      if (!blocker_in(e, ET_RANGE)) {
        e->flags &= (uint8_t)~4;
        anim_play_from_frame(&e->anim, CLIP(e, R_A2), 0);
        e->st = BL_OPEN;
      }
      break;
    case BL_HIT_PAUSE:
      e->st = BL_HIT;
      anim_play_from_frame(&e->anim, CLIP(e, R_A7), 0);
      break;
    case BL_HIT:
      e->jx = rand_range(-0.05f, 0.05f), e->jy = rand_range(-0.05f, 0.05f);
      if (blocker_in(e, ET_RANGE)) e->jx = e->jy = 0, blocker_close(e);
      else if (done) e->jx = e->jy = 0, blocker_idle(e);
      break;
  }
}

/* its corpse (Corpse Blocker): stunned a second, then its death, staying */
static void blocker_die(Enemy *e) {
  geo_fling(0, e->geo_s, e->body.x, e->body.y, 15, 30, 80, 100);
  geo_fling(1, e->geo_m, e->body.x, e->body.y, 15, 30, 80, 100);
  geo_fling(2, e->geo_l, e->body.x, e->body.y, 15, 30, 80, 100);
  if (e->ent != NO_ENT) persist_set(ent_at(e->ent)->persist), enemy_terrain_off(ent_at(e->ent));
  death_shake(e);
  FREEZE_MOMENT_1();
  /* (its corpse's Shaman Update: the Snail Shaman hears of it, behind his gate) */
  if (g_pd.shaman == 2 || g_pd.shaman == 3) {
    static const uint8_t update[NUM_ROOMS] = VMEV_UPDATE_SHAMAN;
    g_pd.shaman = g_pd.shaman == 3 ? 4 : 5;
    if (update[g_room.id] != 255) vm_broadcast(update[g_room.id]);
  }
  e->mode = EM_CORPSE, e->st = 0, e->t0 = 0;
  e->body.vx = e->body.vy = 0, e->body.gravity_scale = 0, e->body.mask = 0;
  anim_play(&e->anim, CLIP(e, R_A8));
}

static void blocker_corpse(Enemy *e) {
  e->t0 += DT;
  if (e->st == 0) {
    e->jx = rand_range(-0.05f, 0.05f), e->jy = rand_range(-0.05f, 0.05f);
    if (e->t0 >= 1) {
      e->jx = e->jy = 0;
      anim_play_from_frame(&e->anim, CLIP(e, R_DEATH_LAND), 0);
      e->st = 1;
    }
  }
}

/* ---------------------------------------------------------------- Husks: the Zombie Swipe FSM, a Walker */
enum { ZS_READY, ZS_ANTICIPATE, ZS_LUNGE, ZS_COOLDOWN, ZS_IDLE };
#define ZS_LUNGE_SPEED 6.0f
#define ZS_IDLE_TIME 0.25f

static void husk_start(Enemy *e) {
  walker_init(e);
  e->st = ZS_READY;
}

/* (an older Zombie Swipe: no Coward state, Ready deaf to TOOK DAMAGE, Reset playing Idle) */
static bool husk_old(const Enemy *e) {
  const Ent *v = enemy_rec(e, ET_VARS);
  return v && (v->a & 2);
}

static void husk_ready(Enemy *e) {
  /* Reset: StartWalker; (Coward: no) Ready */
  if (husk_old(e)) anim_play(&e->anim, CLIP(e, R_IDLE));
  e->st = ZS_READY;
  walker_start(e);
}

static void husk_attack(Enemy *e) {
  /* Left or Right?: the walker stopped, facing the hero (Face Left / Face Right) */
  walker_stop(e, STOP_CONTROLLED);
  bool right = hero_x() > e->body.x;
  const Ent *v = enemy_rec(e, ET_VARS);
  float speed = v ? v->p0 : ZS_LUNGE_SPEED;
  e->ax = right ? speed : -speed;
  set_scale_x(e, right ? -fabsf(e->sx) : fabsf(e->sx));
  e->wk_facing = right ? 1 : -1;   /* (SetWalkerFacing: ChangeFacing) */
  /* Anticipate */
  e->body.vx = e->body.vy = 0;
  anim_play_from_frame(&e->anim, CLIP(e, R_A1), 0);
  e->st = ZS_ANTICIPATE;
}

static void husk_update(Enemy *e) {
  bool done = (e->anim.events & ANIM_DONE) != 0;
  if (e->flags & 2) {   /* (RECOIL HORIZONTAL: Reset) */
    e->flags &= (uint8_t)~2;
    if (e->st != ZS_READY) {
      husk_ready(e);
      return;
    }
  }
  switch (e->st) {
    case ZS_READY:
      walker_update(e);
      if ((e->can_see && e->in_alert) || (e->b1 && !husk_old(e))) {
        e->b1 = false;
        husk_attack(e);
      }
      e->b1 = false;
      break;
    case ZS_ANTICIPATE:
      if (done) {
        e->st = ZS_LUNGE;
        anim_play_from_frame(&e->anim, CLIP(e, R_A2), 0);
      }
      break;
    case ZS_LUNGE:
      if (done) {
        e->st = ZS_COOLDOWN;
        anim_play_from_frame(&e->anim, CLIP(e, R_A3), 0);
        e->body.vx = 0;
      }
      break;
    case ZS_COOLDOWN:
      if (done) {
        e->st = ZS_IDLE, e->t0 = 0;
        anim_play(&e->anim, CLIP(e, R_IDLE));
      }
      break;
    case ZS_IDLE: {
      const Ent *v = enemy_rec(e, ET_VARS);
      if ((e->t0 += DT) >= (v ? v->p1 : ZS_IDLE_TIME)) husk_ready(e);
      break;
    }
      break;
  }
}

static void husk_fixed(Enemy *e) {
  if (e->st == ZS_LUNGE) e->body.vx = e->ax;   /* (SetVelocity2d every frame) */
  else if (e->st == ZS_READY && e->wk_state == WK_WALKING) {
    const Ent *w = walker_rec(e);
    if (w) e->body.vx = e->wk_facing > 0 ? w->y0 : w->x0;
  }
}

/* ---------------------------------------------------------------- attacks: children that hurt the Knight */
/* its hitbox k (ET_HITBOX's, in order), or NULL */
static const Ent *hitbox_rec(const Enemy *e, int k) {
  if (e->ent == NO_ENT) return NULL;
  const Ent *d = ent_at(e->ent);
  for (int i = 1; i <= d->s0; i++)
    if (d[i].type == ENT_BOX && d[i].flags == ET_HITBOX && k-- == 0) return &d[i];
  return NULL;
}

/* (the child on: its collider, and the sprite it plays till its clip ends: DeactivateAfter2dtkAnimation) */
static void hitbox_on(Enemy *e, int k, int clip) {
  e->hb_on |= (uint8_t)(1 << k);
  if (clip >= 0) anim_play_from_frame(&e->sub, clip, 0), e->sub_hb = (uint8_t)(k + 1);
}

static void hitbox_tick(Enemy *e) {
  if (!e->sub_hb) return;
  e->sub.events = 0;
  anim_update(&e->sub, DT);
  if ((e->sub.events & ANIM_DONE) || !e->sub.playing) e->hb_on &= (uint8_t)~(1 << (e->sub_hb - 1)), e->sub_hb = 0;
}

/* the Knight's box and its hitboxes on: the damage of the first it meets */
static int hitbox_touch(const Enemy *e, float x0, float y0, float x1, float y1) {
  float k = e->sx < 0 ? -1.0f : 1.0f;
  for (int i = 0; i < 8 && e->hb_on >> i; i++) {
    if (!(e->hb_on >> i & 1)) continue;
    const Ent *h = hitbox_rec(e, i);
    if (!h) continue;
    if (e->sub_hb == i + 1) {
      /* (its sprite's frame sets its collider) */
      const float *d;
      int n, t = sprite_collider(e->sub.sprite, &d, &n);
      if (t == SC_NONE) continue;
      float ks = fabsf(e->sx);
      if (t == SC_BOX) {
        float cx = e->body.x + d[0] * e->sx, cy = e->body.y + d[1] * ks;
        if (x1 > cx - d[2] * ks && x0 < cx + d[2] * ks && y1 > cy - d[3] * ks && y0 < cy + d[3] * ks) return (int)h->p1;
        continue;
      }
      if (t == SC_SHAPE) {
        float pts[16];
        for (int j = 0; j < n && j < 8; j++) pts[2 * j] = e->body.x + d[2 * j] * e->sx, pts[2 * j + 1] = e->body.y + d[2 * j + 1] * ks;
        if (box_meets_shape(x0, y0, x1, y1, pts, n < 8 ? n : 8)) return (int)h->p1;
        continue;
      }
    }
    /* (a child scaled from its place: the Mosskin's gas) */
    float gs = 1, gx = 0, gy = 0;
    const Ent *gv = FSM(e) == EF_SHAKER ? enemy_rec(e, ET_VARS) : NULL;
    if (gv && gv->p2 > 0) gs = e->qx / gv->p2, gx = gv->p0, gy = gv->p1;
    float hx0 = gx + (h->x0 - gx) * gs, hx1 = gx + (h->x1 - gx) * gs, hy0 = gy + (h->y0 - gy) * gs, hy1 = gy + (h->y1 - gy) * gs;
    float a0 = e->body.x + (k > 0 ? hx0 : -hx1), a1 = e->body.x + (k > 0 ? hx1 : -hx0);
    if (!(x1 > a0 && x0 < a1 && y1 > e->body.y + hy0 && y0 < e->body.y + hy1)) continue;
    float pts[16];
    int np = (int)h->p0;
    for (int j = 0; j < np && j < 8; j++) {
      const float *f = &h[1 + j / 4].x0;
      pts[2 * j] = e->body.x + (gx + (f[2 * (j & 3)] - gx) * gs) * k, pts[2 * j + 1] = e->body.y + gy + (f[2 * (j & 3) + 1] - gy) * gs;
    }
    if (box_meets_shape(x0, y0, x1, y1, pts, np < 8 ? np : 8)) return (int)h->p1;
  }
  return 0;
}

/* ---------------------------------------------------------------- shockwaves (the shockwave FSM, its spurts) */
#define MAX_WAVES 4
#define MAX_SPURTS 32
typedef struct {   /* (its small fields together: many at once) */
  float x, y, speed, inc, scale, t;
  bool on, spurting;
  int8_t dir;
} Wave;
typedef struct {
  float x, y, scale, t;
  Anim anim;
  bool on;
  int8_t dir;
} Spurt;
static Wave waves[MAX_WAVES];
static Spurt spurts[MAX_SPURTS];

/* a wave from (x, y) along the ground, its speed and scale (Start, Right / Left, Start Move) */
static void wave_spawn(float x, float y, bool right, float speed, float scale) {
  Wave *w = NULL;
  for (int i = 0; i < MAX_WAVES && !w; i++)
    if (!waves[i].on) w = &waves[i];
  if (!w) return;
  memset(w, 0, sizeof *w);
  w->on = w->spurting = true;
  w->x = x, w->y = y, w->scale = scale, w->dir = right ? 1 : -1;
  w->inc = speed * 2 * w->dir, w->speed = speed * 0.025f * w->dir;
}

static void spurt_spawn(const Wave *w) {
  Spurt *s = NULL;
  for (int i = 0; i < MAX_SPURTS && !s; i++)
    if (!spurts[i].on) s = &spurts[i];
  if (!s) return;
  s->on = true, s->x = w->x, s->y = w->y, s->scale = w->scale, s->t = 0, s->dir = w->dir;
  anim_play_from_frame(&s->anim, CLIP_BULLET_SHOCKWAVE_SPURT, 0);
}

static void waves_tick(void) {
  for (int i = 0; i < MAX_WAVES; i++) {
    Wave *w = &waves[i];
    if (!w->on) continue;
    w->t += DT;
    if (w->spurting) {
      /* Move: faster and faster; into a wall (its trigger meeting the terrain), or past the ground's end: done. (Its
       * trigger: scaled by the wave's x scale, which Left sets to -1; the ground ray from a unit ahead, unscaled) */
      w->speed += w->inc * DT;
      float dx = w->speed * DT, kx = w->dir > 0 ? w->scale : 1;
      float front = w->x + (-0.2374f * w->dir + 0.32855f * w->dir) * kx;
      if (w->t >= 0)
        for (int k = 0; k < 3; k++)
          if (phys_ray(front, w->y + 0.4f + 0.65f * (float)k, w->dir, 0, fabsf(dx), CF_TERRAIN, NULL)) {
            w->t = -0.15f;   /* (End Pause: spurts a moment more) */
            break;
          }
      w->x += dx;
      if (w->t >= 0 && !phys_ray(w->x + w->dir, w->y + 1, 0, -1, 1.6f, CF_TERRAIN, NULL))
        w->spurting = false, w->t = 0;   /* (HIT: End Particle) */
      else if (w->t < 0 && w->t + DT >= 0)
        w->spurting = false, w->t = 0;
      if (w->spurting) spurt_spawn(w);
    } else {
      w->x += w->speed * DT;
      if (w->t >= 1) w->on = false;   /* (End Particle: a second, then recycled) */
    }
  }
  for (int i = 0; i < MAX_SPURTS; i++) {
    Spurt *s = &spurts[i];
    if (!s->on) continue;
    s->t += DT;
    s->anim.events = 0;
    anim_update(&s->anim, DT);
    if ((s->anim.events & ANIM_DONE) || !s->anim.playing) s->on = false;   /* (RecycleAfter2dtkAnimation) */
  }
}

/* (Damage timing: a spurt hurts from 0.05 s to 0.1 s) */
static int spurts_touch(float x0, float y0, float x1, float y1, int *side) {
  for (int i = 0; i < MAX_SPURTS; i++) {
    const Spurt *s = &spurts[i];
    if (!s->on || s->t < 0.05f || s->t >= 0.1f + DT / 2) continue;
    float cx = s->x - 0.0473f * s->dir * s->scale, cy = s->y + 0.87f * s->scale;
    float hx = 0.2022f * s->scale, hy = 0.8718f * s->scale;
    if (x1 > cx - hx && x0 < cx + hx && y1 > cy - hy && y0 < cy + hy) {
      *side = s->x > g_hero.body.x ? SIDE_RIGHT : SIDE_LEFT;
      return 1;
    }
  }
  return 0;
}

static void spurts_draw(void) {
  for (int i = 0; i < MAX_SPURTS; i++) {
    const Spurt *s = &spurts[i];
    if (!s->on || s->anim.sprite < 0) continue;
    Inst in;
    sprite_inst(s->anim.sprite, s->x, s->y, -0.12f, s->scale * s->dir, s->scale, 0, &in);
    gfx_actor_minor(&in, SORT_KEY(0, 0));
  }
}

/* ---------------------------------------------------------------- Leaping Husks: the Zombie Leap FSM, a Walker */
enum { ZL_READY, ZL_ANTICIPATE, ZL_LAUNCH, ZL_LUNGE, ZL_COOLDOWN, ZL_IDLE };

static void leaper_start(Enemy *e) {
  walker_init(e);
  e->st = ZL_READY;
}

static void leaper_update(Enemy *e) {
  bool done = (e->anim.events & ANIM_DONE) != 0;
  switch (e->st) {
    case ZL_READY:
      walker_update(e);
      if (e->can_see && e->in_alert) {
        /* Left or Right?: the walker stopped, its leap aimed a little past the hero, facing it */
        walker_stop(e, STOP_CONTROLLED);
        bool right = hero_x() > e->body.x;
        e->ax = (hero_x() - e->body.x) * 1.25f;
        set_scale_x(e, right ? -1.0f : 1.0f);
        e->wk_facing = right ? 1 : -1;
        /* Anticipate */
        e->body.vx = e->body.vy = 0;
        anim_play_from_frame(&e->anim, CLIP(e, R_A1), 0);
        e->st = ZL_ANTICIPATE;
      }
      break;
    case ZL_ANTICIPATE:
      if (e->anim.events & ANIM_TRIGGER) {
        e->body.vx = e->ax, e->body.vy = 20;   /* (Launch) */
        e->st = ZL_LAUNCH;
      }
      break;
    case ZL_LAUNCH:
      e->st = ZL_LUNGE;
      break;
    case ZL_LUNGE:
      if (e->body.ncontacts && side_hit(e, 3)) {
        anim_play_from_frame(&e->anim, CLIP(e, R_A2), 0);   /* (Cooldown: Land) */
        e->body.vx = 0;
        e->st = ZL_COOLDOWN;
      }
      break;
    case ZL_COOLDOWN:
      if (done) {
        anim_play(&e->anim, CLIP(e, R_IDLE));
        e->st = ZL_IDLE, e->t0 = 0;
      }
      break;
    case ZL_IDLE: {
      const Ent *v = enemy_rec(e, ET_VARS);
      if ((e->t0 += DT) >= (v ? v->p0 : 0.5f)) {
        /* Reset: StartWalker */
        anim_play(&e->anim, CLIP(e, R_IDLE));
        walker_start(e);
        e->st = ZL_READY;
      }
      break;
    }
  }
}

static void leaper_fixed(Enemy *e) {
  if (e->st == ZL_READY && e->wk_state == WK_WALKING) {
    const Ent *w = walker_rec(e);
    if (w) e->body.vx = e->wk_facing > 0 ? w->y0 : w->x0;
  }
}

/* ---------------------------------------------------------------- Husk Guards: the Zombie Guard FSM */
enum {
  GD_DORMANT, GD_WAKE, GD_COOLDOWN, GD_IDLE, GD_WAIT, GD_STARTLE, GD_TURN, GD_WALK, GD_RUN, GD_STOP, GD_TURN_BACK,
  GD_RETURN, GD_ANTIC, GD_ATTACK, GD_RECOIL, GD_ATTACK_END, GD_STOMP_ANTIC, GD_STOMP_AIR, GD_STOMP_LAND, GD_STOMP_CD
};
#define GD_ATTACK_SCALE 10.0f   /* (Wake: its Attack Range's x scale) */
/* (e->b0 facing right, b1 running, wk_stop woken; ax walk speed, ay run speed; qx, qy its roam's ends; tx its idle
 * spot; c0, c1 clubs, stomps in a row) */

static float guard_var(const Enemy *e, int i) {
  const Ent *v = enemy_rec(e, ET_VARS);
  return v ? (i == 0 ? v->p0 : v->p1) : (i == 0 ? 9 : 23.5f);
}

/* in its Attack Range (smaller once woken) */
static bool guard_in_attack(const Enemy *e) {
  const Ent *r = enemy_rec(e, ET_RANGE);
  if (!r) return false;
  float k = e->st == GD_DORMANT || r->p0 <= 0 ? 1 : GD_ATTACK_SCALE / r->p0;
  return hero_in_box(e, r->x0 * k, r->y0, r->x1 * k, r->y1);
}

static void guard_face(Enemy *e, bool right) {
  e->b0 = right;
  set_scale_x(e, right ? 1.0f : -1.0f);
}

static void guard_idle(Enemy *e) {
  e->b1 = false;
  e->body.vx = 0;
  anim_play(&e->anim, CLIP_GUARD_IDLE);
  cam_rumble(RUMBLE_OFF);
  e->st = GD_IDLE, e->t0 = 0;
}

static bool guard_hero_in_roam(const Enemy *e) { return hero_x() >= e->qx && hero_x() <= e->qy; }

/* Face Hero: towards the hero (Turn), else an attack (Attack Choice) */
static void guard_face_hero(Enemy *e) {
  bool right = hero_x() > e->body.x;
  if (hero_x() == e->body.x) return;
  e->ax = right ? 5 : -5, e->ay = right ? 10 : -10;
  cam_rumble(RUMBLE_OFF);
  if (right != e->b0) {
    guard_face(e, right);
    anim_play_from_frame(&e->anim, CLIP_GUARD_TURN, 0);
    e->st = GD_TURN;
    return;
  }
  for (;;) {
    bool club = rand_range(0, 1) < 0.75f;
    if (club && e->c0 < 4) {
      e->c0++, e->c1 = 0;
      e->b1 = false;
      anim_play_from_frame(&e->anim, CLIP_GUARD_ANTICIPATE, 0);
      e->body.vx = 0;
      e->st = GD_ANTIC;
      return;
    }
    if (!club && e->c1 < 2) {
      e->c1++, e->c0 = 0;
      anim_play_from_frame(&e->anim, CLIP_GUARD_STOMP_ANTIC, 0);
      e->body.vx = 0;
      e->st = GD_STOMP_ANTIC, e->t0 = 0;
      return;
    }
  }
}

/* Alert: towards the hero, walking or running (Chase), or turning first */
static void guard_alert(Enemy *e) {
  float dx = hero_x() - e->body.x;
  if (dx == 0) return;
  bool right = dx > 0;
  e->ax = right ? 5 : -5, e->ay = right ? 10 : -10;
  if (right != e->b0) {
    cam_rumble(RUMBLE_OFF);
    guard_face(e, right);
    anim_play_from_frame(&e->anim, CLIP_GUARD_TURN, 0);
    e->st = GD_TURN;
    return;
  }
  float dy = hero_y() - e->body.y, dist = sqrtf(dx * dx + dy * dy);
  if (e->b1 || dist > guard_var(e, 0)) {
    if (e->st != GD_RUN) cam_shake(SHAKE_SMALL), cam_rumble(RUMBLE_SMALL);   /* (SmallRumble) */
    e->b1 = true;
    anim_play(&e->anim, CLIP_GUARD_RUN);
    e->st = GD_RUN;
  } else {
    cam_rumble(RUMBLE_OFF);
    anim_play(&e->anim, CLIP_GUARD_WALK);
    e->st = GD_WALK;
  }
}

static void guard_start(Enemy *e) {
  /* Initiate: its roam (and idle spot) about where it starts; Start Left / Right */
  float roam = guard_var(e, 1);
  e->qx = e->body.x - roam, e->qy = e->body.x + roam, e->tx = e->body.x;
  const Ent *v = enemy_rec(e, ET_VARS);
  guard_face(e, !(v && (v->a & 1)));
  e->wk_stop = 1;   /* (Woken) */
  anim_play(&e->anim, CLIP_GUARD_DORMANT);
  e->st = GD_DORMANT;
}

static bool guard_overhead(const Enemy *e) { return hero_in_box(e, 0, 1.9629f, 1.545f, 1.7664f); }

static void guard_update(Enemy *e) {
  bool done = (e->anim.events & ANIM_DONE) != 0, hit = (e->flags & 8) != 0;
  bool attack = e->can_see && guard_in_attack(e), alert = e->can_see && e->in_alert;
  bool lose = !e->can_see || !e->in_alert || hero_x() > e->qy || hero_x() < e->qx;
  e->t0 += DT;
  switch (e->st) {
    case GD_DORMANT:
      if (attack || hit) anim_play_from_frame(&e->anim, CLIP_GUARD_WAKE, 0), e->st = GD_WAKE;
      break;
    case GD_WAKE:
      if (done) anim_play(&e->anim, CLIP_GUARD_IDLE), e->st = GD_COOLDOWN, e->t0 = 0;
      break;
    case GD_COOLDOWN:
      if (e->t0 >= 0.21f) guard_idle(e);
      break;
    case GD_IDLE:
      if (attack) guard_face_hero(e);
      else if (alert && guard_hero_in_roam(e)) {
        /* In Roam Distance?, Woken?: startled first if it went back to its spot */
        if (e->wk_stop) guard_alert(e);
        else anim_play_from_frame(&e->anim, CLIP_GUARD_STARTLE, 0), e->wk_stop = 1, e->st = GD_STARTLE;
      } else if (e->t0 >= 4) {
        /* Return Check: back to its spot */
        if (e->body.x > e->tx + 1 || e->body.x < e->tx - 1) {
          bool right = e->body.x < e->tx - 1;
          if (right != e->b0) {
            guard_face(e, right);
            anim_play_from_frame(&e->anim, CLIP_GUARD_TURN, 0);
            e->st = GD_TURN_BACK;
          } else
            e->st = GD_RETURN, anim_play(&e->anim, CLIP_GUARD_WALK), e->wk_stop = 0, e->b1 = false;
        } else
          e->st = GD_WAIT;
      }
      break;
    case GD_WAIT:
      guard_idle(e);
      break;
    case GD_STARTLE:
      if (done) guard_alert(e);
      break;
    case GD_TURN:
      if (guard_overhead(e)) {
        e->b1 = false;
        anim_play_from_frame(&e->anim, CLIP_GUARD_ANTICIPATE, 0);
        e->body.vx = 0;
        e->st = GD_ANTIC;
      } else if (done)
        guard_idle(e);
      break;
    case GD_WALK:
    case GD_RUN: {
      float dx = hero_x() - e->body.x, dy = hero_y() - e->body.y;
      if (e->st == GD_WALK && sqrtf(dx * dx + dy * dy) > guard_var(e, 0)) guard_alert(e);
      else if (attack) guard_face_hero(e);
      else if (lose) {
        cam_rumble(RUMBLE_OFF);
        e->body.vx = 0;
        anim_play_from_frame(&e->anim, e->st == GD_RUN ? CLIP_GUARD_STOP_RUN : CLIP_GUARD_STOP_WALK, 0);
        e->b1 = false;
        e->st = GD_STOP;
      } else if (e->t0 > 0)
        guard_alert(e), e->t0 = 0;   /* (WAIT: Alert again, the next frame) */
      break;
    }
    case GD_STOP:
      if (done) guard_idle(e);
      break;
    case GD_TURN_BACK:
      if (done) e->st = GD_RETURN, anim_play(&e->anim, CLIP_GUARD_WALK), e->wk_stop = 0, e->b1 = false;
      break;
    case GD_RETURN:
      if (attack) guard_face_hero(e);
      else if (alert && guard_hero_in_roam(e)) guard_alert(e);
      else if (e->b0 ? e->body.x > e->tx - 1 : e->body.x < e->tx + 1) guard_idle(e);
      break;
    case GD_ANTIC:
      if (done) {
        /* Attack: its club swung (Swipe), then the impact */
        hitbox_on(e, 0, CLIP_GUARD_SWIPE);
        anim_play_from_frame(&e->anim, CLIP_GUARD_ATTACK2, 0);
        e->st = GD_ATTACK, e->t0 = 0;
      }
      break;
    case GD_ATTACK:
      if (e->t0 >= 0.201f) {
        cam_shake(SHAKE_AVERAGE);   /* (Attack Recoil) */
        e->st = GD_RECOIL, e->t0 = 0;
      }
      break;
    case GD_RECOIL:
      if (e->t0 >= 0.14f) e->body.vx = 0, e->st = GD_ATTACK_END;
      break;
    case GD_ATTACK_END:
      if (done || !e->anim.playing) anim_play(&e->anim, CLIP_GUARD_IDLE), e->st = GD_COOLDOWN, e->t0 = 0;
      break;
    case GD_STOMP_ANTIC:
      if (e->t0 >= 0.35f) anim_play_from_frame(&e->anim, CLIP_GUARD_STOMP_JUMP, 0), e->st = GD_STOMP_AIR;
      break;
    case GD_STOMP_AIR:
      if (done) {
        /* Land: two shockwaves, either way */
        e->body.vx = 0;
        anim_play_from_frame(&e->anim, CLIP_GUARD_STOMP_LAND, 0);
        wave_spawn(e->body.x, e->body.y - 4, true, 18, 1.25f);
        wave_spawn(e->body.x, e->body.y - 4, false, 18, 1.25f);
        cam_shake(SHAKE_AVERAGE);
        e->st = GD_STOMP_LAND;
      }
      break;
    case GD_STOMP_LAND:
      if (done) anim_play(&e->anim, CLIP_GUARD_IDLE), e->st = GD_STOMP_CD, e->t0 = 0;
      break;
    case GD_STOMP_CD:
      if (e->t0 >= 0.4f) e->st = GD_COOLDOWN, e->t0 = 0;
      break;
  }
}

static void guard_fixed(Enemy *e) {
  switch (e->st) {
    case GD_WALK: e->body.vx = e->ax; break;
    case GD_RUN: e->body.vx = e->ay; break;
    case GD_RETURN: e->body.vx = e->b0 ? 5 : -5; break;
    case GD_RECOIL: e->body.vx = e->b0 ? -5 : 5; break;            /* (Recoil) */
    case GD_STOMP_AIR: e->body.vx = e->b0 ? -10 : 10; break;       /* (Jump Velocity: back) */
  }
}

/* ---------------------------------------------------------------- the False Knight: FalseyControl, Check Health */
enum {
  FK_DORMANT, FK_START_FALL, FK_STATE1, FK_FIRST_IDLE, FK_IDLE, FK_TURN, FK_JUMP_ANTIC, FK_RISE, FK_FALL,
  FK_JA_ANTIC, FK_JA_RISE, FK_JA_FALL, FK_JA_HIT, FK_JA_SLAM, FK_JA_RECOIL, FK_JA_RECOIL2, FK_JA_END,
  FK_S_ANTIC, FK_S_RISE, FK_S_FALL, FK_S_LAND, FK_S_ATTACK_ANTIC, FK_S_ATTACK, FK_SLAM, FK_S_RECOVER,
  FK_RUN_ANTIC, FK_RUN, FK_STUN_START, FK_STUN_AIR, FK_STUN_LAND, FK_ROLL_END, FK_OPEN, FK_OPENED, FK_HIT,
  FK_STUN_FAIL, FK_RECOVER, FK_IDLE_PAUSE, FK_RAGE_ANTIC, FK_RISE2, FK_FALL2, FK_STATE2, FK_R_ATTACK_ANTIC,
  FK_RAGE_BEGIN, FK_RAGE, FK_PARTICLE_PAUSE, FK_ANIM_END, FK_RAGE_END, FK_JA_ANTIC2, FK_JA_RISE2, FK_JA_FALL2,
  FK_JA_HIT2, FK_FLOOR_BREAK, FK_DEATH_LAND, FK_DEATH_OPEN, FK_OPENED2, FK_HIT2, FK_DEATH_ANIM, FK_STEAM, FK_READY,
  FK_BLOW, FK_HEAD_LAND, FK_DEAD
};
#define FK_SCALE 1.3f
#define FK_RECOVER_HP 65     /* (Check Health: Recover HP, its HP at the start) */
#define FK_HEAD_HP 40
#define FK_HEAD_X 2.29f      /* (the Head's place in the False Knight: shown, or out of the way) */
#define FK_HEAD_Y -3.2709f
#define MAX_BARRELS 12

typedef struct {
  bool on, broken, deflected;
  float x, y, vx, vy, ang, spin, scale, gravity, t;
} Barrel;

static struct {
  int8_t fk, head;              /* (their slots) */
  bool head_shown, first_jump, inert, going_right, kinematic;
  uint8_t turns, jump_count, ja_row, slam_row, stunned, rages;
  float jump_x, recoil, stun_x, idle_min, idle_max, shock_x;
  /* the Barrel Summoner (summon) */
  uint8_t summon_st, spawns;
  float summon_t, summon_x0, summon_x1, summon_y;
  Barrel barrels[MAX_BARRELS];
  /* its arena (Battle Control) and floor (Floor Control) */
  int16_t battle, floor;        /* (their records, -1: none) */
  uint8_t battle_st, floor_st;
  float battle_t;
  /* what is left: the Death Head, the staff */
  bool dhead_on, staff_on;
  Body dhead, staff;
  Anim dhead_anim;
  float staff_ang, staff_w;
  Body bits[3];
  bool bits_on;
} fk;

enum { BT_DETECT, BT_FIGHT, BT_END_WAIT, BT_DONE };
enum { FL_IDLE, FL_CRACKED, FL_BROKEN };

static Enemy *fk_body(void) { return fk.fk >= 0 && en[fk.fk].mode != EM_OFF ? &en[fk.fk] : NULL; }
static Enemy *fk_head(void) { return fk.head >= 0 && en[fk.head].mode != EM_OFF ? &en[fk.head] : NULL; }

static void fk_face(Enemy *e, bool right) {
  fk.stun_x = right ? -10 : 10;
  e->b0 = right;
  set_scale_x(e, right ? FK_SCALE : -FK_SCALE);
}

static bool fk_ground(const Enemy *e) { return e->body.ncontacts && side_hit(e, 3); }

/* (Rise / Fall: the speed up slowed, the speed down quickened, a fixed step at a time) */
static void fk_air_fixed(Enemy *e) {
  switch (e->st) {
    case FK_RISE: case FK_JA_RISE: case FK_S_RISE: case FK_RISE2: case FK_JA_RISE2: e->body.vy *= 0.85f; break;
    case FK_FALL: case FK_JA_FALL: case FK_S_FALL: case FK_FALL2: case FK_JA_FALL2: e->body.vy *= 1.15f; break;
    case FK_RUN: e->body.vx = fk.jump_x; break;   /* (Run Speed) */
  }
}

static void fk_hitter(Enemy *e, int clip) {
  if (clip < 0) {
    e->hb_on = 0, e->sub_hb = 0;
    return;
  }
  e->hb_on |= 1;
  anim_play_from_frame(&e->sub, clip, 0);
  e->sub_hb = 1;
}

static void fk_jump(Enemy *e, float gravity, int clip, float vy, int st) {
  e->body.gravity_scale = gravity;
  anim_play_from_frame(&e->anim, clip, 0);
  e->body.vx = fk.jump_x, e->body.vy = vy;
  cam_shake(SHAKE_ENEMY_KILL);
  e->st = st;
}

static void fk_idle(Enemy *e) {
  e->body.gravity_scale = 0.39f;
  anim_play(&e->anim, CLIP_FK_IDLE);
  fk_hitter(e, -1);
  e->wait = rand_range(fk.idle_min, fk.idle_max);
  e->t0 = 0;
  e->st = FK_IDLE;
}

/* the hero's x, its own */
static bool fk_hero_right(const Enemy *e) { return hero_x() > e->body.x; }

/* JA Check Hero Pos .. JA Antic */
static void fk_jump_attack(Enemy *e) {
  fk.jump_count = 0;
  cam_rumble(RUMBLE_OFF);
  bool right = fk_hero_right(e);
  fk.jump_x = hero_x() + (right ? -3 : 3) - e->body.x;
  fk.recoil = right ? -3 : 3;
  e->body.vx = 0;
  fk.jump_x *= 0.58f;
  fk.jump_x = fk.jump_x > 12 ? 12 : fk.jump_x < -12 ? -12 : fk.jump_x;
  anim_play_from_frame(&e->anim, CLIP_FK_JUMP_ANTIC, 0);
  e->st = FK_JA_ANTIC;
}

static void fk_slam_antic(Enemy *e) {
  anim_play_from_frame(&e->anim, CLIP_FK_ATTACK_ANTIC, 0);
  e->st = FK_S_ATTACK_ANTIC, e->t0 = 0;
}

static void fk_walls_jump(Enemy *e);

/* Move Choice: a run at a far hero, else a slam, a jump attack or a jump */
static void fk_move_choice(Enemy *e) {
  fk.turns = 0;
  for (int guard = 0; guard < 16; guard++) {
    float dx = hero_x() - e->body.x, dy = hero_y() - e->body.y;
    if (dx * dx + dy * dy > 21 * 21) {
      /* Run Antic: at its scale times 14 */
      anim_play_from_frame(&e->anim, CLIP_FK_RUN_ANTIC, 0);
      fk.jump_x = e->sx * 14;
      e->st = FK_RUN_ANTIC;
      return;
    }
    int r = (int)rand_range(0, 3);
    if (r == 0) {
      /* S Check Hero Pos */
      if (fk.slam_row > 2) continue;
      fk.ja_row = 0, fk.slam_row++, fk.jump_count = 0;
      bool right = fk_hero_right(e);
      fk.jump_x = hero_x() + (right ? -1 : 1) * rand_range(12, 18) - e->body.x;
      fk.recoil = right ? -5 : 5, fk.shock_x = right ? 5.5f : -5.5f, fk.going_right = right;
      /* S Antic: a smash from where it is if the hero is far, else a jump back first */
      if (dx * dx + dy * dy >= 12 * 12) {
        fk_slam_antic(e);
        return;
      }
      fk.jump_x *= 0.9f;
      fk.jump_x = fk.jump_x > 12 ? 12 : fk.jump_x < -12 ? -12 : fk.jump_x;
      anim_play_from_frame(&e->anim, CLIP_FK_JUMP_ANTIC, 0);
      e->st = FK_S_ANTIC;
      return;
    }
    if (r == 1) {
      /* Row Check */
      if (fk.ja_row > 3) continue;
      fk.slam_row = 0, fk.ja_row++;
      fk_jump_attack(e);
      return;
    }
    /* Determine Jump */
    if (fk.jump_count > 0 || fk.stunned == 2) continue;
    fk.jump_count++;
    if (rand_range(0, 1) < 0.5f) fk_walls_jump(e);
    else {
      /* Towards */
      float hx = hero_x() < 15 ? 15 : hero_x() > 42 ? 42 : hero_x();
      fk.jump_x = (hx - e->body.x) * 0.9f;
      fk.jump_x = fk.jump_x > 12 ? 12 : fk.jump_x < -12 ? -12 : fk.jump_x;
    }
    anim_play_from_frame(&e->anim, CLIP_FK_JUMP_ANTIC, 0);
    e->st = FK_JUMP_ANTIC;
    return;
  }
  fk_idle(e);
}

/* Walls Check, Random: away from a near wall, else either way */
static void fk_walls_jump(Enemy *e) {
  if (phys_ray(e->body.x, e->body.y, -1, 0, 8, CF_TERRAIN, NULL)) fk.jump_x = 5;          /* (Random R, Random Right) */
  else if (phys_ray(e->body.x, e->body.y, 1, 0, 8, CF_TERRAIN, NULL)) fk.jump_x = -5;     /* (Random L, Random Left) */
  else {
    fk.jump_x = rand_range(-10, 10);
    fk.jump_x = fk.jump_x <= 0 ? (fk.jump_x > -5 ? -5 : fk.jump_x) : (fk.jump_x < 5 ? 5 : fk.jump_x);
  }
}

static void fk_head_show(bool on) {
  Enemy *h = fk_head();
  fk.head_shown = on;
  if (h) {
    if (on) h->flags &= (uint8_t)~1;
    else h->flags |= 1;
  }
}

/* Recover / Stun Fail: up again */
static void fk_recover(Enemy *e, bool stunned) {
  if (stunned) fk.stunned++, cam_shake(SHAKE_ENEMY_KILL);
  fk_head_show(false);
  fk.kinematic = false;
  anim_play_from_frame(&e->anim, CLIP_FK_STUN_RECOVER, 0);
  e->damage = 1;
  if (!stunned) e->flags &= (uint8_t)~4;
  e->st = stunned ? FK_RECOVER : FK_STUN_FAIL;
}

static void barrel_summon(void);
static void fk_floor_event(int ev);
static void fk_battle_end(void);

/* (Check Health: its health gone, STUN, its health back) */
static void fk_stun(Enemy *e) {
  if (fk.inert) return;
  e->hp = FK_RECOVER_HP;
  /* Check Direction: facing the hero; Stun Start */
  fk_hitter(e, -1);
  bool right = fk_hero_right(e);
  if (e->b0 != right) fk_face(e, right), fk.jump_x = right ? 14 : -14;
  e->flags |= 4;
  e->damage = 0;
  e->body.gravity_scale = 1;
  anim_play_from_frame(&e->anim, CLIP_FK_STUN_ROLL, 0);
  e->body.vx = fk.stun_x, e->body.vy = 20;
  cam_shake(SHAKE_BIG);
  cam_rumble(RUMBLE_OFF);
  e->st = FK_STUN_START;
}

static void fk_start(Enemy *e) {
  fk.idle_min = fk.idle_max = 1;
#ifdef HOST
  if (getenv("FKSTUNNED")) fk.stunned = (uint8_t)atoi(getenv("FKSTUNNED"));   /* (tests: the fight further on) */
#endif
  fk.fk = (int8_t)(e - en);
  e->body.gravity_scale = 0;
  e->flags |= 32;   /* (its renderer off till it falls) */
  fk_face(e, e->sx > 0);
  e->st = FK_DORMANT;
  fk_head_show(false);
}

static void fk_head_start(Enemy *e) {
  fk.head = (int8_t)(e - en);
  e->body.gravity_scale = 0;
  e->flags |= 1;
  e->damage = 0;
}

/* the Head, where its FSM puts it in the False Knight */
static void fk_head_place(void) {
  Enemy *h = fk_head(), *e = fk_body();
  if (!h || !e) return;
  h->sx = e->sx;
  h->body.x = e->body.x + FK_HEAD_X * e->sx, h->body.y = e->body.y + FK_HEAD_Y * FK_SCALE;
  h->body.vx = h->body.vy = 0;
}

/* (BATTLE START) */
static void fk_battle_start(void) {
  Enemy *e = fk_body();
  if (!e || e->st != FK_DORMANT) return;
  /* Start Fall */
  e->flags &= (uint8_t)~32;
  cam_shake(SHAKE_BIG);
  e->body.gravity_scale = 1;
  e->st = FK_START_FALL;
}

static void fk_update(Enemy *e) {
  bool done = (e->anim.events & ANIM_DONE) != 0, sub_done = e->sub_hb && ((e->sub.events & ANIM_DONE) || !e->sub.playing);
  bool hit = (e->flags & 8) != 0;
  (void)hit;
  e->t0 += DT;
  switch (e->st) {
    case FK_DORMANT:
      break;
    case FK_START_FALL:
      if (fk_ground(e)) {
        /* Rubble End: KILL ALL ENEMIES; State 1 */
        for (int i = 0; i < MAX_ENEMIES; i++)
          if (en[i].mode == EM_ALIVE && en[i].ent != NO_ENT && (ent_at(en[i].ent)->s1 & EF_PREBATTLE)) {
            en[i].flags &= (uint8_t)~4, en[i].evasion = 0;
            enemy_hit(&en[i], 90, 9999);
          }
        e->body.vx = 0;
        anim_play_from_frame(&e->anim, CLIP_FK_LAND, 0);
        cam_shake(SHAKE_AVERAGE);
        e->st = FK_STATE1;
      }
      break;
    case FK_STATE1:
      if (done) {
        if (fk.first_jump) fk_idle(e);
        else {
          /* Music: its title (on the right), First Idle */
          title_show(TITLE_FALSE_KNIGHT, TF_VISITED | TF_RIGHT);
          e->body.gravity_scale = 0.39f;
          anim_play(&e->anim, CLIP_FK_IDLE);
          fk_hitter(e, -1);
          fk.first_jump = true;
          e->st = FK_FIRST_IDLE, e->t0 = 0;
        }
      }
      break;
    case FK_FIRST_IDLE:
      if (e->t0 >= 1.5f) {
        fk_walls_jump(e);   /* (Random) */
        fk.jump_x = rand_range(-10, 10);
        fk.jump_x = fk.jump_x <= 0 ? (fk.jump_x > -5 ? -5 : fk.jump_x) : (fk.jump_x < 5 ? 5 : fk.jump_x);
        anim_play_from_frame(&e->anim, CLIP_FK_JUMP_ANTIC, 0);
        e->st = FK_JUMP_ANTIC;
      }
      break;
    case FK_IDLE: {
      bool right = fk_hero_right(e);
      if (e->b0 != right) {
        /* Turn L / R: three turns in a row, then a move */
        if (fk.turns == 3) {
          fk_move_choice(e);
          break;
        }
        fk.turns++;
        fk_face(e, right);
        anim_play_from_frame(&e->anim, CLIP_FK_TURN, 0);
        e->st = FK_TURN;
      } else if (e->t0 >= e->wait)
        fk_move_choice(e);
      break;
    }
    case FK_TURN:
      if (done) fk_idle(e);
      break;
    case FK_JUMP_ANTIC:
      if (done) fk_jump(e, 0.125f, CLIP_FK_JUMP, 90, FK_RISE);
      break;
    case FK_RISE: case FK_S_RISE: case FK_RISE2:
      if (e->body.vy < 0) e->st++;   /* (FALL) */
      break;
    case FK_FALL: case FK_FALL2:
      if (fk_ground(e)) {
        /* State 1 / State 2: landed */
        e->body.vx = 0;
        anim_play_from_frame(&e->anim, CLIP_FK_LAND, 0);
        cam_shake(SHAKE_AVERAGE);
        e->st = e->st == FK_FALL ? FK_STATE1 : FK_STATE2;
      }
      break;
    case FK_JA_ANTIC:
      if (done) fk_jump(e, 0.12f, CLIP_FK_JUMP_ATTACK_UP, 90, FK_JA_RISE);
      break;
    case FK_JA_RISE: case FK_JA_RISE2:
      if (e->body.vy < 0) e->st++;
      break;
    case FK_JA_FALL: case FK_JA_FALL2:
      if (e->st == FK_JA_FALL2) fk.inert = true;   /* (FALLEN: Check Health inert) */
      if (phys_ray(e->body.x, e->body.y, 0, -1, 9.5f, CF_TERRAIN, NULL)) {
        /* JA Hit: the mace down */
        fk_hitter(e, CLIP_FK_JUMP_ATTACK_HIT_1);
        anim_play(&e->anim, CLIP_FK_BLANK);
        e->st = e->st == FK_JA_FALL ? FK_JA_HIT : FK_JA_HIT2;
      }
      break;
    case FK_JA_HIT:
      if (fk_ground(e)) {
        /* JA Slam */
        e->body.vx = 0;
        cam_shake(SHAKE_BIG);
        e->st = FK_JA_SLAM, e->t0 = 0;
      }
      break;
    case FK_JA_SLAM:
      if (e->t0 >= 0.083f || sub_done) {
        /* (Barrels?) JA Recoil: back off, barrels summoned */
        fk_hitter(e, -1);
        anim_play_from_frame(&e->anim, CLIP_FK_JUMP_ATTACK_HIT_2, 0);
        e->body.vx = fk.recoil;
        barrel_summon();
        e->st = FK_JA_RECOIL;
      }
      break;
    case FK_JA_RECOIL:
      if (done) {
        anim_play_from_frame(&e->anim, CLIP_FK_JUMP_ATTACK_HIT_3, 0);
        fk.recoil /= 2;
        e->body.vx = fk.recoil;
        e->st = FK_JA_RECOIL2, e->t0 = 0;
      }
      break;
    case FK_JA_RECOIL2:
      if (e->t0 >= 0.084f) e->body.gravity_scale = 0.39f, e->body.vx = 0, e->st = FK_JA_END;
      break;
    case FK_JA_END:
      if (done || !e->anim.playing) fk_idle(e);
      break;
    case FK_S_ANTIC:
      if (done) {
        /* Check Jump Dir, Walls Check 2: no jump back into a wall */
        float dir = fk.jump_x <= 0 ? -1 : 1;
        if (phys_ray(e->body.x, e->body.y, dir, 0, 8, CF_TERRAIN, NULL)) fk_slam_antic(e);
        else {
          cam_shake(SHAKE_ENEMY_KILL);
          anim_play_from_frame(&e->anim, CLIP_FK_JUMP, 0);
          e->body.vx = fk.jump_x, e->body.vy = 105;
          e->st = FK_S_RISE;
        }
      }
      break;
    case FK_S_FALL:
      if (fk_ground(e)) {
        cam_shake(SHAKE_AVERAGE);
        e->body.vx = 0;
        anim_play_from_frame(&e->anim, CLIP_FK_LAND, 0);
        e->st = FK_S_LAND;
      }
      break;
    case FK_S_LAND:
      if (done) fk_slam_antic(e);
      break;
    case FK_S_ATTACK_ANTIC:
      if (e->t0 >= 1.2f) {
        /* S Attack: the mace swung */
        fk_hitter(e, CLIP_FK_ATTACK);
        anim_play(&e->anim, CLIP_FK_BLANK);
        e->st = FK_S_ATTACK, e->t0 = 0;
      }
      break;
    case FK_S_ATTACK:
      if (e->t0 >= 0.12f) cam_shake(SHAKE_BIG), e->st = FK_SLAM;
      break;
    case FK_SLAM:
      if (sub_done) {
        /* S Attack Recover: barrels, a shockwave the way it faced */
        barrel_summon();
        wave_spawn(e->body.x + fk.shock_x, e->body.y - 5.8f, fk.going_right, 22, 1);
        anim_play_from_frame(&e->anim, CLIP_FK_ATTACK_RECOVER, 0);
        fk_hitter(e, -1);
        e->st = FK_S_RECOVER;
      }
      break;
    case FK_S_RECOVER:
      if (done) fk_idle(e);
      break;
    case FK_RUN_ANTIC:
      if (done) {
        cam_rumble(RUMBLE_MED);
        anim_play(&e->anim, CLIP_FK_RUN);
        e->st = FK_RUN;
      }
      break;
    case FK_RUN: {
      float dx = hero_x() - e->body.x, dy = hero_y() - e->body.y;
      if (dx * dx + dy * dy < 14 * 14) fk_jump_attack(e);
      break;
    }
    case FK_STUN_START:
      e->st = FK_STUN_AIR;
      break;
    case FK_STUN_AIR:
      if (fk_ground(e)) e->st = FK_STUN_LAND, e->t0 = 0;
      break;
    case FK_STUN_LAND:
      /* (END: rolling left fast, or half a second) */
      if (e->body.vx < -3 || e->t0 >= 0.5f) {
        /* Roll End: lying there a while */
        fk.kinematic = true;
        e->body.vx = e->body.vy = 0;
        anim_play_from_frame(&e->anim, CLIP_FK_STUN_ROLL_END, 0);
        e->wait = pd_flag(PDF_FK_FIRST_PLOP) ? 1.2f : 2.5f;
        pd_set_flag(PDF_FK_FIRST_PLOP, true);
        e->st = FK_ROLL_END, e->t0 = 0;
      }
      break;
    case FK_ROLL_END:
      if (e->t0 >= e->wait) anim_play_from_frame(&e->anim, CLIP_FK_STUN_OPEN, 0), e->st = FK_OPEN;
      break;
    case FK_OPEN:
      if (done) {
        /* Head Reset, Opened: the head out, for five seconds */
        Enemy *h = fk_head();
        if (h) anim_play_from_frame(&h->anim, h->anim.clip, 0);
        anim_play(&e->anim, CLIP_FK_STUN_OPENED);
        fk_head_show(true);
        e->st = FK_OPENED, e->t0 = 0;
      }
      break;
    case FK_OPENED:
      if (e->t0 >= 5) fk_recover(e, false);
      break;
    case FK_HIT: case FK_HIT2:
      if (done) anim_play(&e->anim, CLIP_FK_STUN_OPENED), e->st = e->st == FK_HIT ? FK_OPENED : FK_OPENED2, e->t0 = 0;
      break;
    case FK_STUN_FAIL:
      if (done) fk_idle(e);
      break;
    case FK_RECOVER:
      if (done) {
        anim_play(&e->anim, CLIP_FK_IDLE);
        e->flags &= (uint8_t)~4;
        e->st = FK_IDLE_PAUSE, e->t0 = 0;
      }
      break;
    case FK_IDLE_PAUSE:
      if (e->t0 >= 0.5f) {
        /* Rage Jump Antic: to the arena's middle */
        fk.jump_x = (28.9f - e->body.x) * 1.05f;
        anim_play_from_frame(&e->anim, CLIP_FK_JUMP_ANTIC, 0);
        e->st = FK_RAGE_ANTIC;
      }
      break;
    case FK_RAGE_ANTIC:
      if (done) fk_jump(e, 0.3f, CLIP_FK_JUMP, 105, FK_RISE2);
      break;
    case FK_STATE2:
      if (done) {
        fk.rages = 8;
        anim_play_from_frame(&e->anim, CLIP_FK_ATTACK_ANTIC, 0);
        e->st = FK_R_ATTACK_ANTIC, e->t0 = 0;
      }
      break;
    case FK_R_ATTACK_ANTIC:
      if (e->t0 >= 0.7f) {
        fk_hitter(e, CLIP_FK_RAGE);
        anim_play(&e->anim, CLIP_FK_BLANK);
        e->st = FK_RAGE_BEGIN;
      }
      break;
    case FK_RAGE_BEGIN:
      e->st = FK_RAGE, e->t0 = 0;
      fk_hitter(e, CLIP_FK_RAGE);
      break;
    case FK_RAGE:
      if (e->t0 >= 0.249f) {
        /* Rage Slam; Floor Crack? (the last of the second rage's) */
        cam_shake(SHAKE_AVERAGE);
        if (fk.stunned == 2 && fk.rages == 1) fk_floor_event(FL_CRACKED);
        e->st = FK_PARTICLE_PAUSE, e->t0 = 0;
      }
      break;
    case FK_PARTICLE_PAUSE:
      if (e->t0 >= 0.1f) e->st = FK_ANIM_END;
      break;
    case FK_ANIM_END:
      if (sub_done) {
        /* Turn: barrels, the other way */
        barrel_summon();
        fk.rages--;
        set_scale_x(e, -e->sx);
        if (fk.rages <= 0) {
          /* Rage Check */
          if (fk.stunned >= 3) {
            /* JA Antic 2: the last jump, onto the floor's middle */
            e->flags |= 4;
            fk_hitter(e, -1);
            fk.jump_x = (34 - e->body.x) * 0.76f;
            anim_play_from_frame(&e->anim, CLIP_FK_JUMP_ANTIC, 0);
            fk_face(e, true);
            e->st = FK_JA_ANTIC2;
            break;
          }
          /* To Phase 2 / 3, Rage End */
          fk.idle_min = 0.8f, fk.idle_max = 1;
          fk_hitter(e, -1);
          anim_play(&e->anim, CLIP_FK_IDLE);
          e->st = FK_RAGE_END, e->t0 = 0;
          vm_broadcast(VMEV_FK_RAGE_COMPLETE);   /* (the arena's wall: breakable) */
        } else
          e->st = FK_RAGE, e->t0 = 0, fk_hitter(e, CLIP_FK_RAGE);
      }
      break;
    case FK_RAGE_END:
      if (e->t0 >= 0.75f) fk_idle(e);
      break;
    case FK_JA_ANTIC2:
      if (done) {
        e->body.gravity_scale = 0.2f;
        anim_play_from_frame(&e->anim, CLIP_FK_JUMP_ATTACK_UP, 0);
        e->body.vx = fk.jump_x, e->body.vy = 90;
        e->st = FK_JA_RISE2;
      }
      break;
    case FK_JA_HIT2:
      if (fk_ground(e)) {
        /* Floor Break: through the floor */
        fk_hitter(e, -1);
        fk_floor_event(FL_BROKEN);
        e->body.gravity_scale = 1;
        anim_play_from_frame(&e->anim, CLIP_FK_DEATH_FALL, 0);
        e->body.vx = 0, e->body.vy = 15;
        cam_shake(SHAKE_BIG);
        e->damage = 0;
        e->st = FK_FLOOR_BREAK;
      }
      break;
    case FK_FLOOR_BREAK:
      if (e->body.vy <= 0 && fk_ground(e)) {
        anim_play_from_frame(&e->anim, CLIP_FK_DEATH_LAND, 0);
        fk.kinematic = true;
        e->body.vx = e->body.vy = 0;
        e->st = FK_DEATH_LAND, e->t0 = 0;
      }
      break;
    case FK_DEATH_LAND:
      if (e->t0 >= 2) anim_play_from_frame(&e->anim, CLIP_FK_STUN_OPEN, 0), e->st = FK_DEATH_OPEN;
      break;
    case FK_DEATH_OPEN:
      if (done) {
        Enemy *h = fk_head();
        if (h) anim_play_from_frame(&h->anim, h->anim.clip, 0);
        anim_play(&e->anim, CLIP_FK_STUN_OPENED);
        fk_head_show(true);
        e->st = FK_OPENED2;
      }
      break;
    case FK_DEATH_ANIM:
      if (e->t0 >= 1) {
        /* Open Map Shop and Journal; Steam */
        pd_set_flag(PDF_OPENED_MAPPER_SHOP, true);
        pd_set_flag(PDF_CORN_CROSSROADS_LEFT, true);
        e->flags |= 1;
        cam_shake(SHAKE_BIG);
        e->st = FK_STEAM, e->t0 = 0;
      }
      break;
    case FK_STEAM:
      if (e->t0 >= 3) e->st = FK_READY, e->t0 = 0;
      break;
    case FK_READY:
      if (e->t0 >= 1) {
        /* Blow: FK DEATH, the maggot out, the staff flung */
        vm_broadcast(VMEV_FK_DEATH);
        Enemy *h = fk_head();
        if (h) h->mode = EM_OFF;
        fk.head_shown = false;
        cam_shake(SHAKE_BIG);
        anim_play(&e->anim, CLIP_FK_BODY);
        fk.dhead_on = true;
        memset(&fk.dhead, 0, sizeof fk.dhead);
        fk.dhead.x = e->body.x + 3.11f * e->sx, fk.dhead.y = e->body.y - 2.86f * FK_SCALE;
        fk.dhead.vx = e->b0 ? 4 : -4;
        fk.dhead.ox = -0.1094f * 0.975f * (e->sx < 0 ? -1 : 1), fk.dhead.oy = -0.7812f * 0.975f;
        fk.dhead.hx = 2.8125f / 2 * 0.975f, fk.dhead.hy = 2.1562f / 2 * 0.975f;
        fk.dhead.gravity_scale = 1, fk.dhead.friction = 0.2828f, fk.dhead.mask = CF_SOLID;
        anim_play_from_frame(&fk.dhead_anim, CLIP_FK_DEATH_HEAD_1, 0);
        fk.staff_on = true;
        memset(&fk.staff, 0, sizeof fk.staff);
        fk.staff.x = e->body.x - 1.45f * e->sx, fk.staff.y = e->body.y + 0.04f * FK_SCALE;
        fk.staff.vx = cosf(150 * (float)M_PI / 180) * 20, fk.staff.vy = sinf(150 * (float)M_PI / 180) * 20;
        fk.staff.hx = fk.staff.hy = 0.2f;
        fk.staff.gravity_scale = 1, fk.staff.friction = 0.2828f, fk.staff.mask = CF_SOLID;
        fk.staff_ang = 0, fk.staff_w = 700;
        e->st = FK_BLOW;
      }
      break;
    case FK_BLOW:
      if ((fk.dhead_anim.events & ANIM_DONE) || !fk.dhead_anim.playing) {
        fk.dhead.vx = 0;
        anim_play_from_frame(&fk.dhead_anim, CLIP_FK_DEATH_HEAD_2, 0);
        e->st = FK_HEAD_LAND, e->t0 = 0;
      }
      break;
    case FK_HEAD_LAND:
      if (e->t0 >= 1.5f) {
        /* Decrement Battle Enemies, Cough */
        fk_battle_end();
        e->st = FK_DEAD;
      }
      break;
  }
}

static void fk_fixed(Enemy *e) {
  if (e->st == FK_DORMANT) e->body.vx = e->body.vy = 0;
  fk_air_fixed(e);
}

/* the head's HP gone (its Health Check: STUN END, its HP back) */
static void fk_head_stun_end(Enemy *h) {
  h->hp = FK_HEAD_HP;
  anim_play(&h->anim, CLIP_FKHEAD_HEAD_IDLE);
  Enemy *e = fk_body();
  if (!e) return;
  if (e->st == FK_OPENED || e->st == FK_HIT) fk_recover(e, true);
  else if (e->st == FK_OPENED2 || e->st == FK_HIT2) {
    /* Death Anim Start */
    pd_set_flag(PDF_FALSE_KNIGHT_DEFEATED, true);
    cam_shake(SHAKE_ENEMY_KILL);
    anim_play(&h->anim, CLIP_FKHEAD_HEAD_SPAZ);
    h->flags |= 1;
    fk.kinematic = false;
    anim_play(&e->anim, CLIP_FK_DEATH_SPAZ);
    e->st = FK_DEATH_ANIM, e->t0 = 0;
  }
}

/* (the head's HealthManager: HIT to the False Knight) */
static void fk_head_hit(void) {
  Enemy *e = fk_body(), *h = fk_head();
  if (!e) return;
  if (e->st == FK_OPENED || e->st == FK_OPENED2 || e->st == FK_HIT || e->st == FK_HIT2) {
    anim_play_from_frame(&e->anim, CLIP_FK_STUN_HIT, 0);
    if (h) anim_play_from_frame(&h->anim, CLIP_FKHEAD_HEAD_HIT, 0);
    e->st = e->st == FK_OPENED || e->st == FK_HIT ? FK_HIT : FK_HIT2;
  }
}

/* ---------------------------------------------------------------- the barrels the False Knight brings down */
/* (summon: Determine Spawns, Spawn, Check Spawns: six to eight, a fifth of a second or so apart) */
static void barrel_summon(void) {
  if (fk.summon_st) return;
  fk.spawns = (uint8_t)(6 + (int)rand_range(0, 3));
  fk.summon_t = rand_range(0.15f, 0.25f);
  fk.summon_st = 1;
}

static void barrels_tick(void) {
  if (fk.summon_st && (fk.summon_t -= DT) <= 0) {
    if (fk.spawns == 0) fk.summon_st = 0;
    else {
      for (int i = 0; i < MAX_BARRELS; i++) {
        Barrel *b = &fk.barrels[i];
        if (b->on) continue;
        memset(b, 0, sizeof *b);
        b->on = true;
        b->x = rand_range(fk.summon_x0, fk.summon_x1), b->y = fk.summon_y;
        b->scale = rand_range(0.8f, 1);
        b->gravity = 0.325f;
        b->spin = rand_range(0, 1) < 0.5f ? -720 : 720;
        break;
      }
      fk.spawns--;
      fk.summon_t = rand_range(0.15f, 0.25f);
    }
  }
  for (int i = 0; i < MAX_BARRELS; i++) {
    Barrel *b = &fk.barrels[i];
    if (!b->on) continue;
    if (b->broken) {
      if ((b->t += DT) >= 3) b->on = false;
      continue;
    }
    b->vy -= 60 * b->gravity * DT;
    b->ang += b->spin * DT;
    float hx = 0.59f * b->scale, hy = 0.555f * b->scale;
    b->x += b->vx * DT, b->y += b->vy * DT;
    /* (its trigger: the terrain, the hero or an enemy breaks it) */
    bool brk = false;
    for (int k = -1; k <= 1 && !brk; k += 2)
      brk = phys_ray(b->x, b->y, 0, (float)k, hy, CF_TERRAIN, NULL) || phys_ray(b->x, b->y, (float)k, 0, hx, CF_TERRAIN, NULL);
    for (int j = 0; j < MAX_ENEMIES && !brk; j++) {
      Enemy *e = &en[j];
      if (e->mode != EM_ALIVE || (e->flags & 17)) continue;
      float x0, y0, x1, y1;
      enemy_box(e, &x0, &y0, &x1, &y1);
      if (b->x + hx > x0 && b->x - hx < x1 && b->y + hy > y0 && b->y - hy < y1) {
        brk = true;
        if (b->deflected) enemy_hit(e, b->vx >= 0 ? 0 : 180, 10);   /* (damages_enemy: 10, once deflected) */
      }
    }
    if (brk || b->y < -20) b->broken = true, b->t = 0;
  }
}

static int barrels_touch(float x0, float y0, float x1, float y1, int *side) {
  for (int i = 0; i < MAX_BARRELS; i++) {
    Barrel *b = &fk.barrels[i];
    if (!b->on || b->broken) continue;
    float hx = 0.59f * b->scale, hy = 0.555f * b->scale;
    if (x1 > b->x - hx && x0 < b->x + hx && y1 > b->y - hy && y0 < b->y + hy) {
      b->broken = true, b->t = 0;
      *side = b->x > g_hero.body.x ? SIDE_RIGHT : SIDE_LEFT;
      return 1;
    }
  }
  return 0;
}

/* a nail's swing at the barrels: struck the way it swung (Check Direct) */
static void barrels_nail(const float *pts, int npts, float direction) {
  for (int i = 0; i < MAX_BARRELS; i++) {
    Barrel *b = &fk.barrels[i];
    if (!b->on || b->broken || b->deflected) continue;
    float hx = 0.59f * b->scale, hy = 0.555f * b->scale;
    if (!box_meets_shape(b->x - hx, b->y - hy, b->x + hx, b->y + hy, pts, npts)) continue;
    float a = direction * (float)M_PI / 180;
    b->vx = cosf(a) * 45, b->vy = sinf(a) * 45;
    b->gravity = 0.3f;
    b->deflected = true;
  }
}

static void barrels_draw(void) {
  for (int i = 0; i < MAX_BARRELS; i++) {
    const Barrel *b = &fk.barrels[i];
    if (!b->on || b->broken) continue;
    Inst in;
    sprite_inst_rot(SPRITE_FK_BARREL, b->x, b->y, 0.006f, b->scale, b->scale, b->ang, 0, &in);
    gfx_actor(&in, SORT_KEY(0, 0));
  }
}

/* ---------------------------------------------------------------- its arena and floor */
static void fk_floor_event(int ev) {
  if (fk.floor < 0 || fk.floor_st >= ev) return;
  const Ent *f = ent_at(fk.floor);
  fk.floor_st = (uint8_t)ev;
  group_fade(f->group2, 0, 0);   /* (Normal 1, 2 off) */
  if (ev == FL_BROKEN) {
    for (int c = f->a; c < f->a + f->group; c++) phys_collider_enable(c, false);   /* (Break Floor off) */
    fk.bits_on = true;
    for (int k = 0; k < 3; k++) {
      const Ent *r = f + 1;
      for (int j = 1; j <= f->s0; j++)
        if (f[j].flags == 3 + k) r = &f[j];
      Body *b = &fk.bits[k];
      memset(b, 0, sizeof *b);
      b->x = r->x0, b->y = r->y0, b->vx = r->p0, b->vy = r->p1;
      b->gravity_scale = 1;
    }
  }
}

static void fk_battle_end(void) {
  if (fk.battle < 0 || fk.battle_st != BT_FIGHT) return;
  const Ent *b = ent_at(fk.battle);
  persist_set(b->persist);
  if (b->p0 >= 0) ent_set_enabled((int)b->p0, false);
  if (b->p1 >= 0) ent_set_enabled((int)b->p1, false);
  cam_rumble(RUMBLE_OFF);
  fk.battle_st = BT_END_WAIT, fk.battle_t = 0;
}

/* the arena's records: Battle Control (Init; Activate if its fight is over), Floor Control */
static void fk_arena_enter(void) {
  memset(&fk, 0, sizeof fk);
  fk.battle = fk.floor = -1;
  fk.fk = fk.head = -1;
  int n;
  const Ent *es = room_ents(&n);
  for (int i = 0; i < n; i++) {
    if (es[i].type != ENT_OBJ) continue;
    if (es[i].flags == OK_BATTLE) fk.battle = (int16_t)i;
    if (es[i].flags == OK_FK_FLOOR) fk.floor = (int16_t)i;
  }
  if (fk.battle < 0) return;
  const Ent *b = &es[fk.battle];
  fk.summon_x0 = 13.18f, fk.summon_x1 = 44.27f, fk.summon_y = 40.98f;   /* (summon: Summon Min, Max; its y) */
  if (persist_get(b->persist)) {
    /* Activate: the fight over, the gates open, the floor broken, the armour there */
    gates_event(BG_QUICK_OPEN);
    if (b->p0 >= 0) ent_set_enabled((int)b->p0, false);
    if (b->p1 >= 0) ent_set_enabled((int)b->p1, false);
    fk.battle_st = BT_DONE;
    if (fk.floor >= 0) {
      const Ent *f = &es[fk.floor];
      fk.floor_st = FL_BROKEN;
      group_fade(f->group2, 0, 0);
      for (int c = f->a; c < f->a + f->group; c++) phys_collider_enable(c, false);
    }
  } else {
    if (b->p0 >= 0) ent_set_enabled((int)b->p0, false);   /* (Init: CameraLock 1 off) */
    if (b->group) group_fade(b->group, 0, 0);              /* (the armour not there yet) */
    fk.battle_st = BT_DETECT;
    fk.floor_st = FL_IDLE;
  }
}

static void fk_arena_tick(void) {
  if (fk.battle < 0) return;
  const Ent *b = ent_at(fk.battle);
  switch (fk.battle_st) {
    case BT_DETECT: {
      const Body *k = &g_hero.body;
      float x0 = k->x + k->ox - k->hx, x1 = k->x + k->ox + k->hx, y0 = k->y + k->oy - k->hy, y1 = k->y + k->oy + k->hy;
      if (x1 > b->x0 && x0 < b->x1 && y1 > b->y0 && y0 < b->y1) {
        /* Start: the camera locked to the arena, BATTLE START */
        if (b->p0 >= 0) ent_set_enabled((int)b->p0, true);
        if (b->p1 >= 0) ent_set_enabled((int)b->p1, false);
        fk.battle_st = BT_FIGHT;
        gates_event(BG_CLOSE);
        fk_battle_start();
      }
      break;
    }
    case BT_END_WAIT:
      if ((fk.battle_t += DT) >= 2) fk.battle_st = BT_DONE, gates_event(BG_OPEN);   /* (End) */
      break;
  }
  barrels_tick();
  /* what is left of it */
  if (fk.dhead_on) {
    fk.dhead_anim.events = 0;
    anim_update(&fk.dhead_anim, DT);
    body_step(&fk.dhead, DT);
  }
  if (fk.staff_on) {
    body_step(&fk.staff, DT);
    fk.staff_ang += fk.staff_w * DT;
    if (fk.staff.ncontacts) fk.staff_w *= 0.9f;
  }
  if (fk.bits_on)
    for (int k = 0; k < 3; k++) {
      Body *bb = &fk.bits[k];
      bb->vy -= 60 * bb->gravity_scale * DT;
      bb->x += bb->vx * DT, bb->y += bb->vy * DT;
    }
}

static void fk_arena_draw(void) {
  if (fk.battle < 0) return;
  barrels_draw();
  Inst in;
  if (fk.dhead_on && fk.dhead_anim.sprite >= 0) {
    Enemy *e = fk_body();
    float s = 0.975f * (e && e->sx < 0 ? -1 : 1);
    sprite_inst(fk.dhead_anim.sprite, fk.dhead.x, fk.dhead.y, 0.0054f, s, 0.975f, 0, &in);
    gfx_actor(&in, SORT_KEY(0, 0));
  }
  if (fk.staff_on) {
    sprite_inst_rot(SPRITE_FK_STAFF, fk.staff.x, fk.staff.y, 0.005f, 1, 1, fk.staff_ang, 0, &in);
    gfx_actor(&in, SORT_KEY(0, 0));
  }
  if (fk.floor >= 0 && fk.floor_st != FL_IDLE) {
    const Ent *f = ent_at(fk.floor);
    for (int j = 1; j <= f->s0; j++) {
      const Ent *r = &f[j];
      bool show = fk.floor_st == FL_CRACKED ? r->flags <= 1 : r->flags == 2 || (r->flags >= 3 && fk.bits_on);
      if (!show) continue;
      float x = r->x0, y = r->y0;
      if (r->flags >= 3 && fk.bits_on) x = fk.bits[r->flags - 3].x, y = fk.bits[r->flags - 3].y;
      sprite_inst(r->s0, x, y, r->x1, 1, 1, 0, &in);
      gfx_actor(&in, SORT_KEY(r->group, (int)r->a - 32768));
    }
  }
}

/* ---------------------------------------------------------------- the Hollow Shade: its Shade Control FSM */
enum {
  SH_IDLE, SH_STARTLE, SH_FLY, SH_POSITION, SH_SLASH_ANTIC, SH_SLASH, SH_SLASH_BOX, SH_SLASH_CD, SH_FIREBALL_POS,
  SH_CAST_ANTIC, SH_CAST_CHARGE, SH_CHARGE_WAIT, SH_CAST, SH_DECEL, SH_COOLDOWN, SH_RETREAT_START, SH_RETREAT,
  SH_RETREAT_END, SH_DEATH_START, SH_DEATH, SH_DEATH_END, SH_DISSIPATE, SH_LEAVE_PAUSE, SH_DEPART, SH_GONE
};
#define SHADE_MAX_ROAM 25.0f
#define MAX_BALLS 3
/* the shade's (Shadow Ball): flies 1 s, then slows and fades */
typedef struct {
  bool on, ending;
  float x, y, vx, t;
  Anim anim;
} Ball;
static Ball balls[MAX_BALLS];
static struct {
  bool on, slash_on;   /* (its Slash child's collider) */
  Anim slash;
  int8_t sp;           /* its soul: casts left */
  float qx, qy;        /* (the tween back to its start) */
} sh;

/* the Slash's polygon (Slash's local points, at its place and x scale), on the shade facing its way */
static const float shade_slash_pts[5][2] = {{-1.8682f, -0.2095f}, {0.8395f, 0.0287f}, {-0.9918f, -0.9513f}, {-2.0697f, -0.8551f},
                                            {-2.4798f, -0.5897f}};

static void shade_face_hero(Enemy *e, int clip) {
  /* FaceObject (sprite facing left), playing its turn clip on turning */
  float want = hero_x() > e->body.x ? -1.0f : 1.0f;
  if (e->sx != want) {
    set_scale_x(e, want);
    if (clip >= 0) anim_play_from_frame(&e->anim, clip, 0);
  }
}

static float hero_dist(const Enemy *e) {
  float dx = e->body.x - hero_x(), dy = e->body.y - hero_y();
  return sqrtf(dx * dx + dy * dy);
}

static bool roamed_off(const Enemy *e) {
  float dx = e->body.x - e->start_x, dy = e->body.y - e->start_y;
  return dx * dx + dy * dy > SHADE_MAX_ROAM * SHADE_MAX_ROAM;
}

static void shade_retreat(Enemy *e) {
  /* Retreat Start: no collider, slowing */
  e->st = SH_RETREAT_START;
  e->flags |= 1;   /* (collider off) */
  anim_play_from_frame(&e->anim, CLIP_SHADE_RETREAT_START, 0);
}

static void shade_fly(Enemy *e) {
  e->st = SH_FLY;
  anim_play(&e->anim, CLIP_SHADE_FLY);
  e->wait = rand_range(1, 2);
}

/* Attack Choice (after Quake? and Scream?: the shade has neither here): a fireball a fifth of the time, if it can */
static void shade_attack_choice(Enemy *e) {
  for (;;) {
    if (rand_range(0, 1) < 0.2f) {
      /* Sp Check */
      if (g_pd.shade_fireball_level >= 1 && sh.sp >= 1) {
        sh.sp--;
        e->st = SH_FIREBALL_POS, e->t0 = 0;
        return;
      }
      continue;   /* (RETURN: chooses again) */
    }
    /* Q Other?: nothing else to choose; Position */
    e->st = SH_POSITION, e->t0 = 0;
    return;
  }
}

static void shade_spawn(void) {
  Enemy *e = NULL;
  for (int i = 0; i < MAX_ENEMIES && !e; i++)
    if (en[i].mode == EM_OFF) e = &en[i];
  if (!e) return;
  memset(e, 0, sizeof *e);
  memset(&sh, 0, sizeof sh);
  memset(balls, 0, sizeof balls);
  sh.on = true;
  e->mode = EM_ALIVE, e->kind = EK_SHADE, e->ent = NO_ENT;
  e->damage = 1, e->z = 0.006f;
  e->rc_base = 15, e->rc_dur = 0.15f;
  /* (Init: its hp the shade's health in nail hits) */
  e->hp = (int16_t)(g_pd.nail_damage * g_pd.shade_health);
  sh.sp = (int8_t)g_pd.shade_mp;
  Body *b = &e->body;
  b->x = g_pd.shade_x, b->y = g_pd.shade_y;
  b->ox = 0, b->oy = -0.37f, b->hx = 0.25f, b->hy = 0.57f;
  b->gravity_scale = 0, b->mask = CF_SOLID;
  e->ar_r = 7.27f, e->ar_hy = -1;
  e->sx = 1;
  e->start_x = b->x, e->start_y = b->y;
  shade_face_hero(e, -1);
  e->st = SH_IDLE;
  anim_play(&e->anim, CLIP_SHADE_IDLE);
}

void shade_spawn_check(void) {
  if (g_pd.soul_limited && !strcmp(g_pd.shade_scene, room_name(g_room.id))) shade_spawn();
}

/* (Killed: the Hollow Shade Death in its place) */
static void shade_killed(Enemy *e) {
  /* Death Start: the soul unlimited, the geo back, the shade gone from the save */
  g_pd.soul_limited = false;
  g_pd.max_mp = 99;
  int geo_back = g_pd.geo_pool;
  g_pd.geo += geo_back;
  g_pd.geo_pool = 0;
  strcpy(g_pd.shade_scene, "None");
  e->body.vx = e->body.vy = 0;
  e->flags |= 1;
  sh.slash_on = false;
  cam_shake(SHAKE_AVERAGE);
  anim_play(&e->anim, CLIP_SHADE_DEATH_START);
  e->st = SH_DEATH_START, e->t0 = 0;
  e->mode = EM_CORPSE;
}

/* HERO LEAVE: the shade departs (Hollow Shade Depart) */
void enemies_hero_leave(void) {
  for (int i = 0; i < MAX_ENEMIES; i++) {
    Enemy *e = &en[i];
    if (FSM(e) != EF_SHADE || e->mode != EM_ALIVE) continue;
    e->mode = EM_CORPSE, e->flags |= 1, sh.slash_on = false;
    anim_play(&e->anim, CLIP_SHADE_FLY);
    shade_face_hero(e, CLIP_SHADE_TURNTOFLY);
    e->st = SH_LEAVE_PAUSE, e->t0 = 0;
  }
}

static void shade_fixed(Enemy *e) {
  switch (e->st) {
    case SH_FLY: {
      chase_object(e, 4, 0.2f);
      /* ChaseObjectV2: a force towards the hero, the speed clamped (the force acts in the step) */
      float dx = hero_x() - e->body.x, dy = hero_y() - e->body.y, l = sqrtf(dx * dx + dy * dy);
      if (l > 1) dx /= l, dy /= l;
      float sp = sqrtf(e->body.vx * e->body.vx + e->body.vy * e->body.vy);
      if (sp > 4) e->body.vx *= 4 / sp, e->body.vy *= 4 / sp;
      e->body.vx += dx * 8 * DT, e->body.vy += dy * 8 * DT;
      break;
    }
    case SH_POSITION:
    case SH_SLASH_ANTIC: distance_fly(e, 3, 4, 0.2f, true); break;
    case SH_FIREBALL_POS: distance_fly(e, 12, 4, 0.2f, true); break;
    case SH_CAST_ANTIC: e->body.vx *= 0.8f, e->body.vy *= 0.8f; break;
    case SH_DECEL:
    case SH_COOLDOWN:
    case SH_LEAVE_PAUSE: e->body.vx *= 0.9f, e->body.vy *= 0.9f; break;
    case SH_RETREAT_START: e->body.vx *= 0.85f, e->body.vy *= 0.85f; break;
    default: break;
  }
}

static void shade_update(Enemy *e) {
  bool done = (e->anim.events & ANIM_DONE) != 0;
  e->t0 += DT;
  switch (e->st) {
    case SH_IDLE:
      if ((e->in_alert && e->can_see) || e->b1) {
        e->b1 = false;
        e->st = SH_STARTLE;
        anim_play_from_frame(&e->anim, CLIP_SHADE_STARTLE, 0);
      }
      break;
    case SH_STARTLE:
      if (done) shade_fly(e);
      break;
    case SH_FLY:
      shade_face_hero(e, CLIP_SHADE_TURNTOFLY);
      if (roamed_off(e)) shade_retreat(e);
      else if ((e->wait -= DT) <= 0) shade_attack_choice(e);
      break;
    case SH_POSITION: {
      shade_face_hero(e, CLIP_SHADE_TURNTOFLY);
      bool same_y = fabsf(hero_y() - e->body.y) <= 0.2f;
      if (hero_dist(e) < 5 && same_y) {
        e->st = SH_SLASH_ANTIC;
        anim_play_from_frame(&e->anim, CLIP_SHADE_SLASH_ANTIC, 0);
      } else if (roamed_off(e))
        shade_retreat(e);
      else if (e->t0 >= 6)
        shade_attack_choice(e);   /* (END: Quake?, Scream?, Attack Choice) */
      break;
    }
    case SH_SLASH_ANTIC:
      if (done) {
        /* Check Dir, Right / Left: a lunge its way; Slash */
        e->body.vx = e->sx < 0 ? 8 : -8;
        sh.slash_on = true;
        anim_play_from_frame(&sh.slash, CLIP_SHADE_SLASH_EFFECT, 0);
        anim_play_from_frame(&e->anim, CLIP_SHADE_SLASH, 0);
        e->st = SH_SLASH, e->t0 = 0;
      }
      break;
    case SH_SLASH:
      if (e->t0 >= 0.083f) e->st = SH_SLASH_BOX;   /* (its clip may have ended meanwhile: Slash Box sees that) */
      break;
    case SH_SLASH_BOX:
      if (done || !e->anim.playing) {
        sh.slash_on = false;
        e->st = SH_SLASH_CD;
        anim_play_from_frame(&e->anim, CLIP_SHADE_SLASH_CD, 0);
      }
      break;
    case SH_SLASH_CD:
      if (done) shade_fly(e);
      break;
    case SH_FIREBALL_POS: {
      shade_face_hero(e, CLIP_SHADE_TURNTOFLY);
      bool same_y = fabsf(hero_y() - e->body.y) <= 0.2f, at = fabsf(hero_dist(e) - 12) <= 1;
      if ((at && same_y) || e->t0 >= 3) {
        e->st = SH_CAST_ANTIC, e->t0 = 0;
        anim_play(&e->anim, CLIP_SHADE_CAST_ANTIC);
      } else if (roamed_off(e))
        shade_retreat(e);
      break;
    }
    case SH_CAST_ANTIC:
      if (e->t0 >= 0.5f) {
        e->st = SH_CAST_CHARGE, e->t0 = 0;
        anim_play(&e->anim, CLIP_SHADE_CAST_CHARGE);
        e->body.vx = e->body.vy = 0;
        /* Check Dir 2: it will be pushed back as it casts */
        e->ax = e->sx < 0 ? -10 : 10;
        e->st = SH_CHARGE_WAIT;
      }
      break;
    case SH_CHARGE_WAIT:
      if (e->t0 >= 0.25f) {
        /* Shoot R / L: a Shadow Ball, fast its way */
        for (int i = 0; i < MAX_BALLS; i++)
          if (!balls[i].on) {
            Ball *b = &balls[i];
            float k = e->sx < 0 ? 1.0f : -1.0f;
            b->on = true, b->ending = false, b->t = 0;
            b->x = e->body.x + 1.764f * k, b->y = e->body.y + 0.12f, b->vx = 20 * k;
            anim_play_from_frame(&b->anim, CLIP_SHADE_FIREBALL, 0);
            break;
          }
        cam_shake(SHAKE_AVERAGE);
        anim_play_from_frame(&e->anim, CLIP_SHADE_CAST, 0);
        e->body.vx = e->ax;
        e->st = SH_DECEL, e->t0 = 0;
      }
      break;
    case SH_DECEL:
      if (done) {
        e->st = SH_COOLDOWN, e->t0 = 0;
        anim_play(&e->anim, CLIP_SHADE_FLY);
      }
      break;
    case SH_COOLDOWN:
      if (e->t0 >= 0.25f) shade_fly(e);
      break;
    case SH_RETREAT_START:
      if (done) {
        e->body.vx = e->body.vy = 0;
        sh.qx = e->body.x, sh.qy = e->body.y;
        e->st = SH_RETREAT, e->t0 = 0;
      }
      break;
    case SH_RETREAT: {
      /* iTweenMoveTo its start, 1 s, easeInOutCubic */
      float t = e->t0 >= 1 ? 1 : e->t0, k = t < 0.5f ? 4 * t * t * t : 1 - powf(-2 * t + 2, 3) / 2;
      e->body.x = sh.qx + (e->start_x - sh.qx) * k, e->body.y = sh.qy + (e->start_y - sh.qy) * k;
      if (e->t0 >= 1) {
        e->st = SH_RETREAT_END;
        anim_play_from_frame(&e->anim, CLIP_SHADE_RETREAT_END, 0);
      }
      break;
    }
    case SH_RETREAT_END:
      if (done) {
        anim_play(&e->anim, CLIP_SHADE_IDLE);
        e->flags &= (uint8_t)~1;
        e->st = SH_IDLE;
      }
      break;
    /* (Hollow Shade Death) */
    case SH_DEATH_START:
      e->jx = rand_range(-0.1f, 0.1f), e->jy = rand_range(-0.1f, 0.1f);   /* (ObjectJitter) */
      if (e->t0 >= 0.5f) {
        e->jx = e->jy = 0;
        anim_play_from_frame(&e->anim, CLIP_SHADE_DEATH, 0);
        cam_shake(SHAKE_AVERAGE);
        e->st = SH_DEATH_END, e->t0 = 0;
      }
      break;
    case SH_DEATH_END:
      if (e->t0 >= 0.75f) e->st = SH_DISSIPATE, e->t0 = 0;
      break;
    case SH_DISSIPATE:
      if (e->t0 >= 1) {
        hud_soul_limiter(false);   /* (Give Geo: SOUL LIMITER DOWN) */
        e->mode = EM_OFF, sh.on = false;
      }
      break;
    case SH_LEAVE_PAUSE:
      if (e->t0 >= 2) {
        e->body.vx = e->body.vy = 0;
        anim_play_from_frame(&e->anim, CLIP_SHADE_DEPART, 0);
        e->st = SH_DEPART, e->t0 = 0;
      }
      break;
    case SH_DEPART:
      if (e->t0 >= 0.6f) e->mode = EM_OFF, sh.on = false;
      break;
  }
  if (sh.slash_on) anim_update(&sh.slash, DT);
}

static void balls_tick(void) {
  for (int i = 0; i < MAX_BALLS; i++) {
    Ball *b = &balls[i];
    if (!b->on) continue;
    b->t += DT;
    b->anim.events = 0;
    anim_update(&b->anim, DT);
    if (!b->ending && b->t >= 1) {
      b->ending = true;
      anim_play_from_frame(&b->anim, CLIP_SHADE_FIREBALL_END, 0);
    }
    if (b->ending) {
      b->vx *= 0.1f;
      if (b->anim.events & ANIM_DONE) b->on = false;
    }
    b->x += b->vx * DT;
  }
}

/* ---------------------------------------------------------------- the room's */
/* ---------------------------------------------------------------- Aspid Mother (Hatcher) and her young (Hatcher Baby
 * Spawner), who wait in their cage till she sends them out, and go back to it as they die */
enum { HT_IDLE, HT_DISTANCE_FLY, HT_FIRE_ANTIC, HT_FIRE };
enum { HL_INERT, HL_CHASE };

/* (her young in the cage: their count; one of them at random, or NULL) */
static Enemy *hatchling_caged(int pick, int *count) {
  int n = 0;
  Enemy *out = NULL;
  for (int i = 0; i < MAX_ENEMIES; i++) {
    Enemy *q = &en[i];
    if (q->mode != EM_ALIVE || FSM(q) != EF_HATCHLING || q->st != HL_INERT) continue;
    if (n++ == pick) out = q;
  }
  if (count) *count = n;
  return out;
}

static void hatchling_spawn(Enemy *q, float x, float y) {
  /* SPAWN: Chase, from where it is put */
  q->body.x = x, q->body.y = y;
  q->st = HL_CHASE, q->t1 = 0, q->tx = 0, q->pause0 = 0;
}

static void hatcher_distance_fly(Enemy *e) {
  anim_play_from_frame(&e->anim, CLIP(e, R_A1), 2);
  e->t0 = rand_range(2, 3);
  e->st = HT_DISTANCE_FLY;
}

static void hatcher_start(Enemy *e, const Ent *d) {
  e->st = HT_IDLE;
  anim_play_from_frame(&e->anim, CLIP(e, R_A1), 2);
  e->start_x = e->body.x, e->start_y = e->body.y;
  e->wait = 0, e->ax = e->ay = 0;
  if (d->s1 & EF_START) hatcher_distance_fly(e);   /* (startAlert) */
}

static void hatcher_fixed(Enemy *e) {
  if (e->st == HT_IDLE) idle_buzz(e);
  else if (e->st == HT_DISTANCE_FLY) distance_fly_at(e, 6, 3.5f, 0.1f, true, 3.5f);
}

static void hatcher_update(Enemy *e) {
  switch (e->st) {
    case HT_IDLE:
      /* FaceDirection; ALERT: it sees the Knight in its alert range */
      if (e->sx != (e->body.vx > 0 ? -fabsf(e->sx) : fabsf(e->sx))) set_scale_x(e, -e->sx);
      if (e->can_see && e->in_alert) hatcher_distance_fly(e);
      break;
    case HT_DISTANCE_FLY: {
      /* FaceObject: its clip from the start as it turns */
      float want = hero_x() > e->body.x ? -fabsf(e->sx) : fabsf(e->sx);
      if (hero_x() != e->body.x && e->sx != want) set_scale_x(e, want), anim_play_from_frame(&e->anim, CLIP(e, R_A1), 0);
      if ((e->t0 -= DT) <= 0) {
        /* Hatched Max Check: young left in the cage, or on */
        int n;
        hatchling_caged(-1, &n);
        if (n > 0) {
          e->body.vx = e->body.vy = 0;
          anim_play_from_frame(&e->anim, CLIP(e, R_A2), 0);
          e->st = HT_FIRE_ANTIC, e->t0 = 0.335f;
        } else
          hatcher_distance_fly(e);
      }
      break;
    }
    case HT_FIRE_ANTIC:
      if ((e->t0 -= DT) <= 0) {
        /* Fire: one of her young out below her, downwards */
        int n;
        hatchling_caged(-1, &n);
        Enemy *q = n ? hatchling_caged((int)rand_range(0, (float)n - 0.001f), NULL) : NULL;
        if (!q) {
          hatcher_distance_fly(e);   /* (CANCEL) */
          break;
        }
        hatchling_spawn(q, e->body.x, e->body.y - 1);
        q->body.vx = 0, q->body.vy = -5;
        e->st = HT_FIRE;
      }
      break;
    case HT_FIRE:
      if ((e->anim.events & ANIM_DONE) || !e->anim.playing) hatcher_distance_fly(e);
      break;
  }
}

/* (her corpse smashes: two of her young out where it lands) */
static void hatcher_corpse_smash(const Enemy *e) {
  for (int k = 0; k < 2; k++) {
    int n;
    hatchling_caged(-1, &n);
    Enemy *q = n ? hatchling_caged((int)rand_range(0, (float)n - 0.001f), NULL) : NULL;
    if (q) hatchling_spawn(q, e->body.x, e->body.y);
  }
}

static void hatchling_start(Enemy *e) {
  e->st = HL_INERT;
  e->body.vx = e->body.vy = 0;
  anim_play(&e->anim, CLIP(e, R_A1));
}

static void hatchling_fixed(Enemy *e) {
  if (e->st != HL_CHASE) return;
  /* ChaseObject: at the Knight, off by a spread it picks again every 1 to 2 s */
  if (e->t1 >= e->tx) {
    e->qx = rand_range(-1.5f, 1.5f), e->qy = rand_range(-1.5f, 1.5f);
    e->t1 = 0, e->tx = rand_range(1, 2);
  } else
    e->t1 += DT;
  float vx = e->body.vx + (e->body.x < hero_x() + e->qx ? 0.1f : -0.1f);
  float vy = e->body.vy + (e->body.y < hero_y() + e->qy ? 0.1f : -0.1f);
  e->body.vx = vx > 5 ? 5 : vx < -5 ? -5 : vx;
  e->body.vy = vy > 5 ? 5 : vy < -5 ? -5 : vy;
}

static void hatchling_update(Enemy *e) {
  if (e->st == HL_CHASE) face_direction_p(e, &e->pause0, -1, 0.4f);   /* (FaceDirection, pausing between turns) */
}

/* (its death: back to the cage, whole again: CENTIPEDE DEATH) */
static void hatchling_reset(Enemy *e) {
  const Ent *d = ent_at(e->ent);
  e->hp = 5, e->damage = 1;
  e->body.x = d->x0, e->body.y = d->y0;
  e->flashing = false;
  hatchling_start(e);
}

/* ---------------------------------------------------------------- maggots (Prayer Slug: Control): praying, then off
 * away from the Knight as he comes near (its Wake Region) */
enum { SL_IDLE_UP, SL_TO_DOWN, SL_TO_UP, SL_STARTLE, SL_RUN_R, SL_RUN_L, SL_BUMP_R, SL_BUMP_L };
#define SL_ACCEL (0.3f * 1.2f)   /* (0.3 a frame of the game's 60 a second) */

static void slug_pray(Enemy *e, int st) {
  static const float wait[3][2] = {{0, 2}, {1.5f, 3}, {1.5f, 3}};
  anim_play(&e->anim, st == SL_IDLE_UP ? CLIP(e, R_IDLE) : st == SL_TO_DOWN ? CLIP(e, R_A1) : CLIP(e, R_A2));
  e->t0 = rand_range(wait[st][0], wait[st][1]);
  e->st = (uint8_t)st;
}

static void slug_start(Enemy *e) {
  slug_pray(e, SL_IDLE_UP);
  e->b0 = false;
}

static void slug_run(Enemy *e, bool right) {
  set_scale_x(e, right ? 1 : -1);
  if (right) e->qx = rand_range(3.5f, 5.5f);   /* (Max Speed R) */
  else e->qy = rand_range(-5.5f, -3.5f);       /* (Max Speed L) */
  anim_play(&e->anim, CLIP(e, R_A4));
  e->st = right ? SL_RUN_R : SL_RUN_L;
}

static void slug_update(Enemy *e) {
  /* (its Wake Region: WAKE as the Knight comes in) */
  const Ent *r = enemy_rec(e, ET_RANGE);
  bool in = r && hero_in_box(e, r->x0, r->y0, r->x1, r->y1), wake = in && !e->b0;
  e->b0 = in;
  bool done = (e->anim.events & ANIM_DONE) != 0;
  switch (e->st) {
    case SL_IDLE_UP:
    case SL_TO_DOWN:
    case SL_TO_UP:
      if (wake) {
        anim_play_from_frame(&e->anim, CLIP(e, R_A3), 0);
        set_scale_x(e, hero_x() > e->body.x ? 1 : -1);   /* (FaceObject, sprite facing right) */
        e->st = SL_STARTLE;
      } else if ((e->t0 -= DT) <= 0)
        slug_pray(e, e->st == SL_TO_DOWN ? SL_TO_UP : SL_TO_DOWN);
      break;
    case SL_STARTLE:
      if (done) e->qx = 4.5f, e->qy = -4.5f, slug_run(e, hero_x() <= e->body.x);   /* (Direction: away) */
      break;
    case SL_BUMP_R:
    case SL_BUMP_L:
      if (done) slug_run(e, e->st == SL_BUMP_R);
      break;
  }
}

static void slug_fixed(Enemy *e) {
  if (e->st != SL_RUN_R && e->st != SL_RUN_L) return;
  bool right = e->st == SL_RUN_R;
  float v = e->body.vx + (right ? SL_ACCEL : -SL_ACCEL);
  e->body.vx = v < e->qy ? e->qy : v > e->qx ? e->qx : v;
  if (right ? hero_x() > e->body.x : hero_x() < e->body.x) {
    slug_run(e, !right);   /* (the Knight that way: the other) */
    return;
  }
  if (e->body.ncontacts && side_hit(e, right ? 0 : 2)) {
    /* Bump: up and back off the wall */
    e->body.vx = right ? rand_range(-5, -2.5f) : rand_range(2.5f, 5), e->body.vy = 10;
    anim_play_from_frame(&e->anim, CLIP(e, R_A5), 0);
    e->st = right ? SL_BUMP_R : SL_BUMP_L;
  }
}

/* ---------------------------------------------------------------- Gruz Mother: Big Fly Control (with her bouncer_control);
 * her corpse (corpse) and its burster (burster), which brings out her young */
enum { GF_INVINCIBLE, GF_SLEEP, GF_WAKE, GF_FLY, GF_BUZZ, GF_CHARGE_ANTIC, GF_CHARGE, GF_CHARGE_RECOVER, GF_SUPER_END,
       GF_SLAM_ANTIC, GF_LAUNCH, GF_FLYING, GF_SLAM_HIT, GF_SLAM_END };
#define GF_SCALE 1.25f
#define GF_BUZZ_SPEED 5.0f   /* (bouncer_control's Speed) */
#define GF_SLAM_SPEED 50.0f
/* (her FSM's variables: wait the bouncer's Angle, b1 its Facing Right; ax Charge Angle or Slamming Angle; ay Up Angle,
 * qx Down Angle; qy Super Wait; tx Slam Time; ty Super End Time; t1 Timer; pause0, pause1 Self Vel; c0, c1 Ct Charge,
 * Ct Slam; b0 Slamming Up; start_x, start_y where ObjectJitter jitters about; jy Slam Down's dip) */

/* a box and a polygon (any shape: its outline's points) overlap? */
static bool box_meets_poly(float x0, float y0, float x1, float y1, const float *p, int n) {
  bool in = false;
  float cx = (x0 + x1) / 2, cy = (y0 + y1) / 2;
  for (int i = 0, j = n - 1; i < n; j = i++) {
    float ax = p[2 * j], ay = p[2 * j + 1], bx = p[2 * i], by = p[2 * i + 1];
    if (bx >= x0 && bx <= x1 && by >= y0 && by <= y1) return true;   /* (a corner in the box) */
    if ((ay > cy) != (by > cy) && cx < ax + (bx - ax) * (cy - ay) / (by - ay)) in = !in;
    /* (an edge through the box: clipped to it, something left) */
    float t0 = 0, t1 = 1, dx = bx - ax, dy = by - ay;
    const float q[4][2] = {{-dx, ax - x0}, {dx, x1 - ax}, {-dy, ay - y0}, {dy, y1 - ay}};
    bool cut = true;
    for (int k = 0; k < 4 && cut; k++) {
      if (q[k][0] == 0) {
        if (q[k][1] < 0) cut = false;
        continue;
      }
      float r = q[k][1] / q[k][0];
      if (q[k][0] < 0) t0 = r > t0 ? r : t0;
      else t1 = r < t1 ? r : t1;
      if (t0 > t1) cut = false;
    }
    if (cut) return true;
  }
  return in;   /* (the box's middle in it: the box inside) */
}

/* the Knight in her Battle Range (ET_ZONE: HERO ENTER, HERO EXIT) */
static bool gfly_in_range(const Enemy *e) {
  const Ent *z = enemy_rec(e, ET_ZONE);
  if (!z || g_hero.hidden) return false;
  const Body *k = &g_hero.body;
  float x0 = k->x + k->ox - k->hx, x1 = k->x + k->ox + k->hx, y0 = k->y + k->oy - k->hy, y1 = k->y + k->oy + k->hy;
  float s = e->sx < 0 ? -1.0f : 1.0f, a0 = e->body.x + (s > 0 ? z->x0 : -z->x1), a1 = e->body.x + (s > 0 ? z->x1 : -z->x0);
  if (!(x1 > a0 && x0 < a1 && y1 > e->body.y + z->y0 && y0 < e->body.y + z->y1)) return false;
  float pts[16];
  int np = (int)z->p0 < 8 ? (int)z->p0 : 8;
  for (int j = 0; j < np; j++) {
    const float *f = &z[1 + j / 4].x0;
    pts[2 * j] = e->body.x + f[2 * (j & 3)] * s, pts[2 * j + 1] = e->body.y + f[2 * (j & 3) + 1];
  }
  return box_meets_poly(x0, y0, x1, y1, pts, np);
}

/* FaceObject (sprite facing left) */
static void gfly_face_hero(Enemy *e) {
  if (hero_x() > e->body.x) set_scale_x(e, -GF_SCALE);
  else if (hero_x() < e->body.x) set_scale_x(e, GF_SCALE);
}

static void gfly_velocity(Enemy *e, float degrees, float speed) {
  float a = degrees * (float)M_PI / 180;
  e->body.vx = cosf(a) * speed, e->body.vy = sinf(a) * speed;
}

/* bouncer_control's Aim and the angles it takes off what it bumps (Facing Right) */
static void gfly_aim(Enemy *e, float lo, float hi) {
  e->wait = rand_range(lo, hi);
  float a = fmodf(e->wait, 360);
  if (a < 0) a += 360;
  e->b1 = a < 90 || a >= 270;
}

static void gfly_buzz(Enemy *e) {
  /* Buzz: the bouncer woken (Aim), a while */
  gfly_aim(e, 0, 360);
  e->qy = rand_range(2, 2.8f);
  e->st = GF_BUZZ, e->t0 = 0;
}

static void gfly_super_end(Enemy *e) {
  anim_play(&e->anim, CLIP_GFLY_FLY);
  e->st = GF_SUPER_END, e->t0 = 0;
  if (e->ty <= 0) gfly_buzz(e);   /* (Wait 0: done at once) */
}

static void gfly_launch(Enemy *e, bool up) {
  /* Launch Up / Launch Down: off at its angle; Flying the next frame */
  e->ax = up ? e->ay : e->qx;
  gfly_velocity(e, e->ax, GF_SLAM_SPEED);
  e->b0 = up;
  anim_play(&e->anim, CLIP_GFLY_FLY);
  e->st = GF_LAUNCH;
}

static void gfly_choose(Enemy *e) {
  /* Super Choose: still, the bouncer stopped; a charge or a slam (SendRandomEventV2: three charges, two slams in a
   * row at most) */
  e->body.vx = e->body.vy = 0;
  for (;;) {
    bool charge = rand_range(0, 1) < 0.5f;
    if (charge && e->c0 < 3) {
      e->c0++, e->c1 = 0;
      /* Charge Antic: back from the Knight a little, then at it */
      anim_play_from_frame(&e->anim, CLIP_GFLY_CHARGE_ANTIC, 0);
      gfly_face_hero(e);
      float a = atan2f(hero_y() - e->body.y, hero_x() - e->body.x) * 180 / (float)M_PI;
      if (a < 0) a += 360;
      e->ax = a;
      gfly_velocity(e, a + 180, 3);
      e->st = GF_CHARGE_ANTIC, e->t0 = 0;
      return;
    }
    if (!charge && e->c1 < 2) {
      e->c1++, e->c0 = 0;
      /* Slam Antic: facing the Knight, shaking */
      anim_play_from_frame(&e->anim, CLIP_GFLY_CHARGE_ANTIC, 0);
      gfly_face_hero(e);
      e->qy = hero_x() - e->body.x;
      e->t1 = 0, e->tx = rand_range(2.5f, 3);
      e->start_x = e->body.x, e->start_y = e->body.y;
      e->st = GF_SLAM_ANTIC, e->t0 = 0;
      return;
    }
  }
}

static void gfly_start(Enemy *e) {
  e->st = GF_INVINCIBLE, e->flags |= 4;
  e->sx = GF_SCALE;
  anim_play(&e->anim, CLIP(e, R_IDLE));
}

static int gfly_side(const Enemy *e) {
  static const int order[4] = {1, 0, 3, 2};   /* (CheckCollisionSide: up, right, down, left) */
  for (int i = 0; i < 4; i++)
    if (side_hit(e, order[i])) return order[i];
  return -1;
}

static void gfly_fixed(Enemy *e) {
  switch (e->st) {
    case GF_BUZZ:
      gfly_velocity(e, e->wait, GF_BUZZ_SPEED);   /* (Fly 2: SetVelocityAsAngle) */
      break;
    case GF_SLAM_ANTIC:
      e->body.x = e->start_x + rand_range(-0.1f, 0.1f), e->body.y = e->start_y + rand_range(-0.1f, 0.1f);
      break;
    case GF_FLYING:
      gfly_velocity(e, e->ax, GF_SLAM_SPEED);
      break;
    case GF_SLAM_END:
      e->body.vx *= 0.85f, e->body.vy *= 0.85f;   /* (DecelerateV2) */
      break;
  }
  /* (Slam Down's half unit into the floor, pushed out over a few steps: Box2D's position correction) */
  if (e->jy < 0) e->jy = e->jy * 0.8f > -0.005f ? 0 : e->jy * 0.8f;
}

static void gfly_update(Enemy *e) {
  bool done = (e->anim.events & ANIM_DONE) != 0, hit = (e->flags & 8) != 0;
  e->t0 += DT;
  switch (e->st) {
    case GF_INVINCIBLE:
      if (gfly_in_range(e)) e->st = GF_SLEEP, e->flags &= (uint8_t)~4;
      break;
    case GF_SLEEP:
      if (hit) {
        /* Wake: her title, the arena's fight starts */
        title_show(TITLE_BIGFLY, TF_VISITED);
        cam_shake(SHAKE_AVERAGE);
        arena_start();
        anim_play_from_frame(&e->anim, CLIP_GFLY_WAKE, 0);
        e->body.vy = 2.5f;
        e->st = GF_WAKE, e->t0 = 0;
      } else if (!gfly_in_range(e))
        e->st = GF_INVINCIBLE, e->flags |= 4;
      break;
    case GF_WAKE:
      if (done) {
        /* Fly: still a moment; she hurts now (Hero Damager) */
        anim_play(&e->anim, CLIP_GFLY_FLY);
        e->body.vx = e->body.vy = 0;
        e->hb_on |= 1;
        e->st = GF_FLY, e->t0 = 0;
      }
      break;
    case GF_FLY:
      if (e->t0 >= 1) gfly_buzz(e);
      break;
    case GF_BUZZ: {
      /* (the bouncer: FaceDirection, sprite facing left; off what it bumps) */
      if (e->body.vx != 0) set_scale_x(e, e->body.vx > 0 ? -GF_SCALE : GF_SCALE);
      if (e->body.ncontacts) {
        float a = fmodf(e->wait, 360);
        if (a < 0) a += 360;
        switch (gfly_side(e)) {
          case 1: if (e->b1) gfly_aim(e, 320, 350); else gfly_aim(e, 190, 220); break;
          case 3: if (e->b1) gfly_aim(e, 10, 40); else gfly_aim(e, 140, 170); break;
          case 0: if (a < 180) gfly_aim(e, 140, 170); else gfly_aim(e, 190, 220); break;
          case 2: if (a < 180) gfly_aim(e, 10, 40); else gfly_aim(e, 320, 350); break;
        }
      }
      if (e->t0 >= e->qy) gfly_choose(e);
      break;
    }
    case GF_CHARGE_ANTIC:
      if (e->t0 >= 0.75f) {
        anim_play(&e->anim, CLIP_GFLY_CHARGE);
        gfly_velocity(e, e->ax, 26);
        e->pause0 = e->body.vx, e->pause1 = e->body.vy;
        e->st = GF_CHARGE, e->t0 = 0;
      }
      break;
    case GF_CHARGE: {
      int side = e->body.ncontacts ? gfly_side(e) : -1;
      if (side < 0) break;
      /* Charge Recover: off the wall, half as fast */
      cam_shake(SHAKE_AVERAGE);
      anim_play_from_frame(&e->anim, CLIP_GFLY_CHARGE_RECOVER, 0);
      if (side == 0 || side == 2) e->body.vx = -e->pause0 / 2, e->body.vy = e->pause1 / 2;
      else e->body.vx = e->pause0 / 2, e->body.vy = -e->pause1 / 2;
      e->st = GF_CHARGE_RECOVER, e->t0 = 0;
      break;
    }
    case GF_CHARGE_RECOVER:
      if (e->t0 >= 0.3f) {
        e->body.vx = e->body.vy = 0;   /* (Recover End) */
        e->ty = 0.5f;
        gfly_super_end(e);
      }
      break;
    case GF_SUPER_END:
      if (e->t0 >= e->ty) gfly_buzz(e);
      break;
    case GF_SLAM_ANTIC:
      if (e->t0 >= 0.5f) {
        /* Check Direction: up and down angles towards the Knight */
        if (e->qy <= 0) e->ay = 100, e->qx = 260;
        else e->ay = 80, e->qx = 280;
        gfly_launch(e, true);
      }
      break;
    case GF_LAUNCH:
      e->st = GF_FLYING;
      break;
    case GF_FLYING: {
      e->t1 += DT;
      int side = e->body.ncontacts ? gfly_side(e) : -1;
      if (side == 1 || side == 3) {
        /* Slam Up / Slam Down: stopped against it a moment */
        e->body.vx = e->body.vy = 0;
        anim_play_from_frame(&e->anim, side == 3 ? CLIP_GFLY_SLAM_DOWN : CLIP_GFLY_SLAM_UP, 0);
        if (side == 3) e->jy = -0.5f;
        cam_shake(SHAKE_AVERAGE);
        e->b0 = side == 3;   /* (then up after a slam down, down after a slam up) */
        e->st = GF_SLAM_HIT;
      } else if (side == 0 || side == 2) {
        /* Turn Left / Turn Right, then on the same way up or down */
        set_scale_x(e, side == 0 ? GF_SCALE : -GF_SCALE);
        if (side == 0) e->ay = 100, e->qx = 260;
        else e->ay = 80, e->qx = 280;
        gfly_launch(e, e->b0);
      } else if (e->t1 > e->tx) {
        /* Slam End */
        anim_play_from_frame(&e->anim, CLIP_GFLY_SLAM_END, 0);
        e->ty = 0;
        e->st = GF_SLAM_END, e->t0 = 0;
      }
      break;
    }
    case GF_SLAM_HIT:
      if (done) gfly_launch(e, e->b0);
      break;
    case GF_SLAM_END:
      if (e->t0 >= 0.75f) gfly_super_end(e);
      break;
  }
}

/* her corpse: Init, Steam, Ready, Blow; its burster: Initiate, Geo, In Air, Landed, Stop Emit, Stop, Gurgles, Burst,
 * Spawn */
enum { GC_NONE, GC_INIT, GC_STEAM, GC_READY };
enum { GB_NONE, GB_INITIATE, GB_IN_AIR, GB_LANDED, GB_STOP_EMIT, GB_STOP, GB_GURG1, GB_GURG2, GB_GURG3, GB_BURST, GB_DONE };
static struct {
  uint8_t corpse, burster;
  float ct, bt, flash_t;
  float x, y, sx;      /* (the corpse) */
  float spawn_x, spawn_y;   /* (Fly Spawn's place) */
  Anim canim, banim;
  Body body;           /* (the burster) */
} gm;

static void gfly_die(Enemy *e) {
  memset(&gm, 0, sizeof gm);
  const Ent *v = enemy_rec(e, ET_VARS);
  gm.spawn_x = v ? v->p0 : e->body.x, gm.spawn_y = v ? v->p1 : e->body.y;
  /* (the corpse where she was, her way; no body: it stays there) */
  gm.x = e->body.x, gm.y = e->body.y, gm.sx = e->sx;
  anim_play(&gm.canim, CLIP_GFLY_FLY);   /* (its default clip, till Steam) */
  gm.corpse = GC_INIT, gm.ct = 0;
  arena_set_activated();
  e->mode = EM_OFF;
}

static void gfly_corpse_tick(void) {
  if (gm.corpse) {
    gm.canim.events = 0;
    anim_update(&gm.canim, DT);
    gm.ct += DT, gm.flash_t += DT;
    if (gm.corpse == GC_INIT && gm.ct >= 0.5f) {
      anim_play_from_frame(&gm.canim, CLIP_GFLY_DEATH, 0);
      cam_shake(SHAKE_BIG);
      cam_rumble(RUMBLE_MED);
      gm.corpse = GC_STEAM, gm.ct = 0;
    } else if (gm.corpse == GC_STEAM && gm.ct >= 3)
      gm.corpse = GC_READY, gm.ct = 0;
    else if (gm.corpse == GC_READY && gm.ct >= 1) {
      /* Blow: the burster out, up and its way */
      Body *b = &gm.body;
      memset(b, 0, sizeof *b);
      float k = fabsf(gm.sx);
      b->x = gm.x, b->y = gm.y;
      b->ox = 0.0469f * gm.sx, b->oy = -0.6484f * GF_SCALE, b->hx = 2.4375f / 2 * k, b->hy = 1.7031f / 2 * GF_SCALE;
      b->vx = gm.sx * 10, b->vy = 20;
      b->gravity_scale = 1, b->friction = 0.2f, b->mask = CF_TERRAIN;
      anim_play(&gm.banim, CLIP_GFLY_FALL);
      gm.burster = GB_INITIATE, gm.bt = 0;
      cam_rumble(RUMBLE_OFF);
      gm.corpse = GC_NONE;
    }
  }
  if (gm.burster) {
    Body *b = &gm.body;
    float pvx = b->vx, pvy = b->vy;
    int had = b->ncontacts;
    body_step(b, DT);
    body_bounce(b, pvx, pvy, had, 0.5f);   /* (ObjectBounce) */
    gm.banim.events = 0;
    anim_update(&gm.banim, DT);
    gm.bt += DT;
    switch (gm.burster) {
      case GB_INITIATE:
        if (gm.bt >= 0.1f) {
          geo_fling_at(0, 50, b->x, b->y, 15, 30, 80, 100, 0.75f);   /* (Geo) */
          gm.burster = GB_IN_AIR;
        }
        break;
      case GB_IN_AIR: {
        bool ground = false;
        for (int c = 0; c < b->ncontacts; c++) ground |= b->cny[c] > 0.5f;
        if (ground) anim_play_from_frame(&gm.banim, CLIP_GFLY_WIGGLE, 0), gm.burster = GB_LANDED, gm.bt = 0;
        break;
      }
      case GB_LANDED:
        if (gm.bt >= 1) gm.burster = GB_STOP_EMIT, gm.bt = 0;
        break;
      case GB_STOP_EMIT:
        if (gm.bt >= 0.5f) anim_play_from_frame(&gm.banim, CLIP_GFLY_STOP, 0), gm.burster = GB_STOP, gm.bt = 0;
        break;
      case GB_STOP:
      case GB_GURG1:
        if (gm.bt >= 2) anim_play_from_frame(&gm.banim, CLIP_GFLY_GURGLE_ONCE, 0), gm.burster++, gm.bt = 0;
        break;
      case GB_GURG2:
        if (gm.bt >= 2) {
          cam_rumble(RUMBLE_SMALL);
          anim_play_from_frame(&gm.banim, CLIP_GFLY_GURGLE_LOOP, 0);
          gm.burster = GB_GURG3, gm.bt = 0;
        }
        break;
      case GB_GURG3:
        if (gm.bt >= 1.9f) anim_play_from_frame(&gm.banim, CLIP_GFLY_BURST, 0), gm.burster = GB_BURST, gm.bt = 0;
        break;
      case GB_BURST:
        if (gm.bt >= 0.16f) {
          /* Spawn: her young where it is (Fly Spawn moved there) */
          cam_rumble(RUMBLE_OFF);
          cam_shake(SHAKE_AVERAGE);
          for (int i = 0; i < MAX_ENEMIES; i++) {
            Enemy *f = &en[i];
            if (f->mode != EM_ALIVE || f->ent == NO_ENT || !(ent_at(f->ent)->s1 & EF_SPAWNED)) continue;
            const Ent *d = ent_at(f->ent);
            f->body.x = b->x + d->x0 - gm.spawn_x, f->body.y = b->y + d->y0 - gm.spawn_y;
          }
          gm.burster = GB_DONE;
        }
        break;
    }
  }
}

static void gfly_corpse_draw(void) {
  Inst in;
  if (gm.corpse) {
    /* (EmitLargeInfectedEffects: the corpse flashes) */
    sprite_inst(gm.canim.sprite, gm.x, gm.y, 0.0085f, gm.sx, GF_SCALE, flash_tint_at(true, gm.flash_t, 10), &in);
    gfx_actor(&in, SORT_KEY(0, 0));
  }
  if (gm.burster) {
    sprite_inst(gm.banim.sprite, gm.body.x, gm.body.y, 0.0085f, gm.sx, GF_SCALE, 0, &in);
    gfx_actor(&in, SORT_KEY(0, 0));
  }
}

/* ---------------------------------------------------------------- Mosscreeps: Moss Walker (waits in the moss, wakes as
 * the Knight comes in sight, walks its floor or wall, buries itself again when the Knight is gone) */
enum { MW_REST, MW_WAKE_PAUSE, MW_SHAKE, MW_APPEAR, MW_WALK_START, MW_WALKING, MW_CANCEL_FRAME, MW_TURN, MW_HIDE };
#define MW_SPEED 3.0f
/* (c0: on a floor 0, a roof 1, a wall 2; c1: bit 0 Edge Range, bit 1 Wall Range, bits 2-3 frames till its rays again;
 * b0: Encountered Hero; b1: its body kinematic; t0: Hide Timer; wait: a state's; qx, qy: Current Velocity) */
static bool mw_wake(const Enemy *e) { return e->in_alert && e->can_see; }

/* RayCast2dV2 from a child (its place in ET_VARS: Edge Range 0, Wall Range 1, Ground Range 2) along its own
 * direction (dx, dy): terrain within dist? */
static bool mw_ray(const Enemy *e, int child, float dx, float dy, float dist) {
  const Ent *v = enemy_rec(e, ET_VARS);
  if (!v) return false;
  const float *c = child == 0 ? &v->p0 : child == 1 ? &v->p2 : &v->x0;
  float px, py, wx, wy;
  enemy_point(e, c[0], c[1], &px, &py);
  enemy_dir(e, dx, dy, &wx, &wy);
  return phys_ray(e->body.x + px, e->body.y + py, wx, wy, dist, CF_TERRAIN, NULL);
}

/* (Walking's two rays, every third frame: Edge Range down, Wall Range ahead) */
static void mw_rays(Enemy *e) {
  e->c1 = (uint8_t)((mw_ray(e, 0, 0, -1, 1) ? 1 : 0) | (mw_ray(e, 1, -1, 0, 0.5f) ? 2 : 0) | 3 << 2);
}

static bool mw_turn(const Enemy *e) { return !(e->c1 & 1) || (e->c1 & 2); }

/* Rest: waiting for the Knight in its Wake Range */
static void mw_rest(Enemy *e) {
  e->st = MW_REST;
  e->b0 = true;   /* (Encountered Hero) */
  if (mw_wake(e)) e->st = MW_WAKE_PAUSE, e->wait = rand_range(0, 1);
}

/* Turn Check: the ground under it, else on walking a frame later */
static void mw_turn_check(Enemy *e) {
  if (mw_ray(e, 2, 0, -1, 1)) {
    e->st = MW_TURN;
    e->body.vx = e->body.vy = 0;
    anim_play_from_frame(&e->anim, CLIP(e, R_TURN), 0);
  } else
    e->st = MW_CANCEL_FRAME;
}

static void mw_check_hide(Enemy *e);
static void mw_walking(Enemy *e) {
  e->st = MW_WALKING;
  if ((e->t0 -= DT) <= 0) {
    mw_check_hide(e);
    return;
  }
  e->c1 = 1;
  mw_rays(e);
  if (mw_turn(e)) mw_turn_check(e);
}

/* Set Encountered, Check Hide: hides if it has met the Knight and he is out of sight, else walks on */
static void mw_check_hide(Enemy *e) {
  if (!e->b0 && mw_wake(e)) e->b0 = true;
  e->t0 = 1;
  if (!e->b0 || mw_wake(e)) {
    e->body.vx = e->qx, e->body.vy = e->qy;   /* (Restart Velocity) */
    mw_walking(e);
    return;
  }
  /* Hide: buries itself, harmless and invincible */
  anim_play_from_frame(&e->anim, CLIP(e, R_A3), 0);
  e->damage = 0, e->flags |= 4;
  e->body.vx = e->body.vy = 0;
  e->st = MW_HIDE;
}

/* Check Dir (its x scale), Up Right or Left Down, Walk Start */
static void mw_walk_start(Enemy *e) {
  float v = e->sx < 0 ? MW_SPEED : -MW_SPEED;
  e->qx = e->c0 == 2 ? 0 : v, e->qy = e->c0 == 2 ? v : 0;
  e->st = MW_WALK_START, e->wait = 0.1f;
  anim_play(&e->anim, CLIP(e, R_WALK));
  e->body.vx = e->qx, e->body.vy = e->qy;
  if ((e->t0 -= DT) <= 0) mw_check_hide(e);
}

/* Activate: harmful and hittable, then on its way */
static void mw_activate(Enemy *e) {
  e->damage = 1, e->flags &= (uint8_t)~4;
  mw_walk_start(e);
}

static void mosswalker_start(Enemy *e) {
  /* (as the scene has it: kinematic, its sprite the walk's first frame) */
  const Ent *c = enemy_rec(e, ET_COLLIDER);
  e->b1 = c && c->p3 != 0;
  anim_play(&e->anim, CLIP(e, R_WALK));
  e->anim.playing = false;
  /* Check Type: a wall (turned a quarter), a roof (upside down), else a floor */
  e->c0 = e->rq == 1 ? 2 : e->sy < 0 ? 1 : 0;
  if (e->c0 == 0) e->body.gravity_scale = 1, e->rc_base = 15, e->rc_flags &= (uint8_t)~2, e->b1 = false;
  else e->body.gravity_scale = 0, e->rc_base = 0, e->rc_flags |= 2;
  const Ent *v = enemy_rec(e, ET_VARS);
  if (v && (v->a & 1)) mw_activate(e);   /* (Roams) */
  else mw_rest(e);
}

static void mosswalker_update(Enemy *e) {
  bool done = (e->anim.events & ANIM_DONE) != 0;
  switch (e->st) {
    case MW_REST:
      mw_rest(e);
      break;
    case MW_WAKE_PAUSE:
      if ((e->wait -= DT) <= 0) {
        e->st = MW_SHAKE, e->wait = 1.2f;
        anim_play_from_frame(&e->anim, CLIP(e, R_A1), 0);
      }
      break;
    case MW_SHAKE:
      if ((e->wait -= DT) <= 0) {
        /* Wake: up out of the moss */
        e->st = MW_APPEAR, e->b1 = false;
        anim_play_from_frame(&e->anim, CLIP(e, R_A2), 0);
        e->t0 = rand_range(3, 5);
      }
      break;
    case MW_APPEAR:
      if (done) mw_activate(e);
      break;
    case MW_WALK_START: {
      bool fin = (e->wait -= DT) <= 0, hide = (e->t0 -= DT) <= 0;
      if (hide) mw_check_hide(e);   /* (the last event of the frame wins) */
      else if (fin) mw_walking(e);
      break;
    }
    case MW_WALKING: {
      bool hide = (e->t0 -= DT) <= 0;
      uint8_t n = (uint8_t)((e->c1 >> 2) - 1);
      if (n == 0) mw_rays(e);
      else e->c1 = (uint8_t)((e->c1 & 3) | n << 2);
      if (mw_turn(e)) mw_turn_check(e);   /* (TURN, after CHECK HIDE, wins) */
      else if (hide) mw_check_hide(e);
      break;
    }
    case MW_CANCEL_FRAME:
      mw_walking(e);
      break;
    case MW_TURN:
      if (done) {
        /* Flip, then Check Dir */
        set_scale_x(e, -e->sx);
        mw_walk_start(e);
      }
      break;
    case MW_HIDE:
      if (done) mw_rest(e);
      break;
  }
}

static void mosswalker_fixed(Enemy *e) {
  if (e->b1) e->body.x += e->body.vx * DT, e->body.y += e->body.vy * DT;   /* (kinematic: nothing stops it) */
}

/* ---------------------------------------------------------------- the birds: Pigeon (idle on a ledge, flies off as the
 * Knight comes in sight or a spell is cast, or another flies off near it) */
enum { PG_IDLE, PG_CHECK, PG_FLY };
/* (ang: its rotation flying, from its velocity; b0: flying left (its sprite turned); b1: its Waker on; t0: flying's
 * time; ax, ay: its rise and side forces; c0: the range records' order: Hero Range, Enemy Range) */
static float pg_size(const Enemy *e) { return fabsf(e->sx); }

/* a child range (its record's k-th ET_RANGE) at the bird's size: is the Knight in sight from it? (the range's own FSM:
 * something in its trigger, then a ray from it to the Knight) */
static const Ent *pg_range(const Enemy *e, int k) {
  const Ent *d = ent_at(e->ent);
  for (int i = 1; i <= d->s0; i++)
    if (d[i].type == ENT_BOX && d[i].flags == ET_RANGE && k-- == 0) return &d[i];
  return NULL;
}

static bool pg_sees_hero(const Enemy *e, float cx, float cy) {
  float dx = hero_x() - cx, dy = hero_y() - cy, l = sqrtf(dx * dx + dy * dy);
  return l < 1e-4f || !phys_ray(cx, cy, dx / l, dy / l, l, CF_TERRAIN, NULL);
}

/* Hero Range: the Knight's box in the circle, and in sight */
static bool pg_hero_range(const Enemy *e) {
  const Ent *r = pg_range(e, 0);
  if (!r || g_hero.hidden) return false;
  float k = pg_size(e), cx = e->body.x + r->x0 * (e->sx < 0 ? -k : k), cy = e->body.y + r->y0 * k, rad = r->x1 * k;
  const Body *h = &g_hero.body;
  float hx = h->x + h->ox, hy = h->y + h->oy;
  float qx = cx < hx - h->hx ? hx - h->hx : cx > hx + h->hx ? hx + h->hx : cx;
  float qy = cy < hy - h->hy ? hy - h->hy : cy > hy + h->hy ? hy + h->hy : cy;
  return (qx - cx) * (qx - cx) + (qy - cy) * (qy - cy) < rad * rad && pg_sees_hero(e, cx, cy);
}

/* Enemy Range: an enemy's collider (or a flying bird's Waker) in the circle, and the Knight in sight */
static bool pg_enemy_range(const Enemy *e) {
  const Ent *r = pg_range(e, 1);
  if (!r) return false;
  float k = pg_size(e), cx = e->body.x + r->x0 * (e->sx < 0 ? -k : k), cy = e->body.y + r->y0 * k, rad = r->x1 * k;
  bool near = false;
  for (int i = 0; i < MAX_ENEMIES && !near; i++) {
    const Enemy *q = &en[i];
    if (q == e || q->mode != EM_ALIVE) continue;
    if (FSM(q) == EF_PIGEON) {
      /* (its Waker: a circle on the Enemies layer, as big as its Enemy Range) */
      if (!q->b1) continue;
      const Ent *qr = pg_range(q, 1);
      float qrad = qr ? qr->x1 : 0, dx = q->body.x - cx, dy = q->body.y - cy;   /* (its x scale 1 by then) */
      near = dx * dx + dy * dy < (rad + qrad) * (rad + qrad);
      continue;
    }
    if (q->flags & 17) continue;
    float x0, y0, x1, y1;
    enemy_box(q, &x0, &y0, &x1, &y1);
    float qx = cx < x0 ? x0 : cx > x1 ? x1 : cx, qy = cy < y0 ? y0 : cy > y1 ? y1 : cy;
    near = (qx - cx) * (qx - cx) + (qy - cy) * (qy - cy) < rad * rad;
  }
  return near && pg_sees_hero(e, cx, cy);
}

static void pigeon_start(Enemy *e) {
  /* Set Size (its collider with it), Set Anim (one of three idles), Set Frame (from a random frame; half turned) */
  float size = rand_range(0.8f, 1), k = size / fabsf(e->sx);
  set_scale_x(e, size);
  e->body.ox *= k, e->body.oy *= k, e->body.hx *= k, e->body.hy *= k;
  int clip = CLIP(e, R_A1 + (int)rand_range(0, 2.999f));
  anim_play_from_frame(&e->anim, clip, (int)rand_range(0, 41.999f));
  if (rand_range(0, 2) < 1) set_scale_x(e, -e->sx);
  e->st = PG_IDLE;
}

static void pigeon_fly(Enemy *e) {
  e->st = PG_FLY, e->t0 = 0;
  e->body.y += 0.5f;
  e->ax = rand_range(10, 35);
  anim_play(&e->anim, CLIP(e, R_A4));
  /* (away from the Knight: Right or Left) */
  bool left = hero_x() > e->body.x;
  e->b0 = left;
  e->ay = left ? rand_range(-75, -35) : rand_range(35, 75);
  e->sx = 1;   /* (SetScale x: 1; its y as it was: its size, kept in qx) */
}

/* (a spell cast: HERO CAST SPELL to all) */
static void pigeon_spell(Enemy *e) {
  if (e->st != PG_IDLE) return;
  e->st = PG_CHECK;   /* (Check: near enough, it flies; else it waits there for ever) */
  float dx = hero_x() - e->body.x, dy = hero_y() - e->body.y;
  if (dx * dx + dy * dy <= 60 * 60) pigeon_fly(e);
}

static void pigeon_update(Enemy *e) {
  if (e->st == PG_IDLE) {
    e->qx = pg_size(e);
    if (pg_hero_range(e) || pg_enemy_range(e)) pigeon_fly(e);
  } else if (e->st == PG_FLY) {
    e->t0 += DT;
    e->b1 = e->t0 >= 0.25f;   /* (its Waker: on after its pause, following it) */
    e->ang = atan2f(e->body.vy, e->body.vx) * 180 / (float)M_PI + (e->b0 ? 180 : 0);   /* (FaceAngle) */
    if (e->t0 >= 5) e->mode = EM_OFF;   /* (Destroy) */
  }
}

static void pigeon_fixed(Enemy *e) {
  if (e->st == PG_FLY) e->body.vx += e->ay * DT, e->body.vy += e->ax * DT;   /* (AddForce2d, its mass 1) */
  e->body.x += e->body.vx * DT, e->body.y += e->body.vy * DT;   /* (a trigger: touches nothing) */
}

/* ---------------------------------------------------------------- the Mender Bug (Mender Bug Ctrl): by the sign once it
 * was broken, one time in fifty; startled as the Knight comes near, it flies off (by 15 across, 20 up in a second,
 * easeInSine) and is gone. Killed, menderState 2. */
enum { MB_IDLE, MB_STARTLE, MB_FLY };
static void mender_start(Enemy *e) {
  /* Dead?, Sign Broken?, Chance (the sign mended either way) */
  if (g_pd.mender_state == 2 || !pd_flag(PDF_MENDER_SIGN_BROKEN)) {
    e->mode = EM_OFF;
    return;
  }
  pd_set_flag(PDF_MENDER_SIGN_BROKEN, false);
#ifdef HOST
  bool here = getenv("HKMENDER") != NULL;   /* (tests: always) */
#else
  bool here = false;
#endif
  if (!here && (int)rand_range(1, 50.999f) != 50) {
    e->mode = EM_OFF;
    return;
  }
  e->st = MB_IDLE;
}

static void mender_update(Enemy *e) {
  bool done = (e->anim.events & ANIM_DONE) != 0;
  if (e->st == MB_IDLE) {
    if (!e->in_alert) return;
    /* (HERO ENTER) Direction: away from the Knight (flying right, turned) */
    bool right = hero_x() <= e->body.x;
    if (right) set_scale_x(e, -e->sx);
    e->tx = right ? 15.0f : -15.0f, e->ty = 20;
    e->st = MB_STARTLE;
    anim_play_from_frame(&e->anim, CLIP(e, R_A1), 0);
  } else if (e->st == MB_STARTLE) {
    if (!done && e->anim.playing) return;
    /* Fly: its collider off, by its vector in a second */
    e->st = MB_FLY, e->t0 = 0, e->start_x = e->body.x, e->start_y = e->body.y;
    e->flags |= 1;
    anim_play(&e->anim, CLIP(e, R_A2));
  } else {
    e->t0 += DT;
    float q = e->t0 >= 1 ? 1 : e->t0, k = 1 - cosf(q * 1.5707964f);
    e->body.x = e->start_x + e->tx * k, e->body.y = e->start_y + e->ty * k;
    if (e->t0 >= 1) e->mode = EM_OFF;   /* (DESTROY) */
  }
}

/* ---------------------------------------------------------------- Fool Eaters: Plant Trap Control (snaps as the Knight
 * steps over its Detector; only its frames have colliders) */
enum { PT_IDLE, PT_READY, PT_SNAP, PT_RETRACT, PT_COOLDOWN };
static void planttrap_update(Enemy *e) {
  bool done = (e->anim.events & ANIM_DONE) != 0;
  switch (e->st) {
    case PT_IDLE:
      /* (DETECT: the Knight in its Detector) */
      if (e->in_alert) e->st = PT_READY, e->wait = 0.75f, anim_play_from_frame(&e->anim, CLIP(e, R_A1), 0);
      break;
    case PT_READY:
      if ((e->wait -= DT) <= 0) e->st = PT_SNAP, e->wait = 1, anim_play_from_frame(&e->anim, CLIP(e, R_A2), 0);
      break;
    case PT_SNAP:
      if ((e->wait -= DT) <= 0) e->st = PT_RETRACT, anim_play_from_frame(&e->anim, CLIP(e, R_A3), 0);
      break;
    case PT_RETRACT:
      if (done) e->st = PT_COOLDOWN, e->wait = 0.5f;
      break;
    case PT_COOLDOWN:
      if ((e->wait -= DT) <= 0) e->st = PT_IDLE;   /* (Init, Idle) */
      break;
  }
}

static void planttrap_start(Enemy *e) {
  /* (its sprite as the scene has it: Retract's last frame, no collider) */
  anim_play_from_frame(&e->anim, CLIP(e, R_A3), clip_frames_count(CLIP(e, R_A3)) - 1);
  e->anim.playing = false;
  e->st = PT_IDLE;
}

/* ---------------------------------------------------------------- Volatile Mosskin: Fungus Zombie Attack (a Walker;
 * stops as the Knight comes in sight in its Attack Range and bursts out a cloud of gas) */
enum { SK_READY, SK_DELAY, SK_ANTIC, SK_ATTACK, SK_CD, SK_IDLE_PAUSE };
#define SK_GAS 0   /* (its hitbox: Gas Hit Box) */
/* (wait: a state's; t0: the gas's time; qx: its scale) */
static void shaker_start(Enemy *e) {
  walker_init(e);
  e->st = SK_READY;
}

/* its gas's scale: SetScale 0.2, then iTweenScaleTo 1 over 0.4 s (easeOutCirc, after 0.005 s) */
static void shaker_gas(Enemy *e) {
  float k = (e->t0 - 0.005f) / 0.4f;
  k = k < 0 ? 0 : k > 1 ? 1 : k;
  e->qx = 0.2f + 0.8f * sqrtf(1 - (k - 1) * (k - 1));
}

static void shaker_update(Enemy *e) {
  walker_update(e);
  switch (e->st) {
    case SK_READY:
      if (e->can_see && e->in_alert) e->st = SK_DELAY, e->wait = rand_range(0, 0.75f);
      break;
    case SK_DELAY:
      if ((e->wait -= DT) <= 0) {
        /* Attack Antic: still, the walker stopped */
        e->st = SK_ANTIC, e->wait = 0.75f;
        e->body.vx = 0;
        walker_stop(e, STOP_CONTROLLED);
        anim_play(&e->anim, CLIP(e, R_A1));
      }
      break;
    case SK_ANTIC:
      if ((e->wait -= DT) <= 0) {
        /* Attack: the gas out, the camera shaken */
        e->st = SK_ATTACK, e->wait = 0.8f, e->t0 = 0;
        cam_shake(SHAKE_ENEMY_KILL);
        e->hb_on |= 1 << SK_GAS;
        shaker_gas(e);
      }
      break;
    case SK_ATTACK:
      e->t0 += DT;
      shaker_gas(e);
      if ((e->wait -= DT) <= 0) e->st = SK_CD, e->wait = 0.5f, e->hb_on &= (uint8_t)~(1 << SK_GAS);
      break;
    case SK_CD:
      if ((e->wait -= DT) <= 0) e->st = SK_IDLE_PAUSE, e->wait = 0.5f, anim_play(&e->anim, CLIP(e, R_IDLE));
      break;
    case SK_IDLE_PAUSE:
      if ((e->wait -= DT) <= 0) {
        e->st = SK_READY;   /* (Reset: StartWalker) */
        walker_start(e);
      }
      break;
  }
}

static void shaker_fixed(Enemy *e) {
  if (e->wk_state == WK_WALKING) {
    const Ent *w = walker_rec(e);
    if (w) e->body.vx = e->wk_facing > 0 ? w->y0 : w->x0;
  }
}

/* ---------------------------------------------------------------- Squits: Mozzie (buzzes idle; seen, keeps its distance
 * from the Knight, then lunges at him; a hit turns its lunge, a wall stops it dead) */
enum { SQ_IDLE, SQ_STARTLE, SQ_IN_SIGHT, SQ_OUT_OF_SIGHT, SQ_STOP, SQ_ATTACK_PAUSE, SQ_STILL_IN_RANGE, SQ_ATTACK_ANTIC,
       SQ_ATTACK_AIM, SQ_LUNGING, SQ_PULL_OUT, SQ_RECOVER };
#define SQ_LUNGE_SPEED 18.0f
/* (pause0: FaceDirection's; wait: a state's; t0: its attention span; t1: Lunge Wait; qx: Attack Angle; ax, ay: its
 * velocity as it lunges (X Speed, Y Speed); b0: its TileDetector off; b1: it touched something (OnCollisionEnter2D);
 * ang: its rotation (FaceAngle)) */
static void sq_distance_fly(Enemy *e) { distance_fly_at(e, 8, 5.5f, 0.1f, true, 1); }

/* FaceObject (sprite facing left): towards the Knight, with a clip if it turns */
static void sq_face(Enemy *e, int clip) {
  float want = e->body.x < hero_x() ? -fabsf(e->sx) : fabsf(e->sx);
  if (e->sx != want) {
    set_scale_x(e, want);
    if (clip >= 0) anim_play_from_frame(&e->anim, clip, 0);
  }
}

static void sq_idle(Enemy *e) {
  e->st = SQ_IDLE, e->ang = 0;
  anim_play(&e->anim, CLIP(e, R_IDLE));
  e->start_x = e->body.x, e->start_y = e->body.y, e->wait = 0, e->ax = e->ay = 0;   /* (IdleBuzz) */
}

static void sq_out_of_sight(Enemy *e);
/* Chase - In Sight: near enough, it pauses to attack; else on next frame (Out of Sight) */
static void sq_in_sight(Enemy *e) {
  e->st = SQ_IN_SIGHT, e->ang = 0;
  anim_play(&e->anim, CLIP(e, R_IDLE));
  sq_face(e, CLIP(e, R_A1));
  sq_distance_fly(e);
  float dx = hero_x() - e->body.x, dy = hero_y() - e->body.y;
  if (dx * dx + dy * dy <= 10 * 10) {
    /* Attack Pause */
    e->st = SQ_ATTACK_PAUSE, e->wait = rand_range(0.25f, 1);
    sq_distance_fly(e);
    sq_face(e, -1);
  }
}

/* Chase - Out of Sight: chasing; seen again, In Sight; not for its attention span, Stop */
static void sq_out_of_sight(Enemy *e) {
  e->st = SQ_OUT_OF_SIGHT, e->t0 = 8;
  sq_distance_fly(e);
  sq_face(e, CLIP(e, R_A1));
  if (e->can_see && e->in_alert) sq_in_sight(e);
}

/* a lunge: at an angle, its scale and rotation (Lunge L / R, Hit Left / Right / Up / Down) */
static void sq_lunge(Enemy *e, float angle, bool right, float wait) {
  set_scale_x(e, right ? -fabsf(e->sx) : fabsf(e->sx));
  float a = angle * (float)M_PI / 180;
  e->body.vx = cosf(a) * SQ_LUNGE_SPEED, e->body.vy = sinf(a) * SQ_LUNGE_SPEED;
  e->ang = atan2f(e->body.vy, e->body.vx) * 180 / (float)M_PI + (right ? 0 : 180);
  e->st = SQ_LUNGING, e->t1 = wait, e->b1 = false;
}

static void mosquito_start(Enemy *e, const Ent *d) {
  if (d->s1 & EF_START) sq_in_sight(e);   /* (Start Alert) */
  else sq_idle(e);
}

static void mosquito_fixed(Enemy *e) {
  switch (e->st) {
    case SQ_IDLE: idle_buzz_p(e, 0.75f, 1, 3, 19, 1); break;
    case SQ_IN_SIGHT: case SQ_OUT_OF_SIGHT: case SQ_ATTACK_PAUSE: sq_distance_fly(e); break;
    case SQ_STOP: decelerate(e, 0.3f); break;
    case SQ_ATTACK_AIM: e->body.vx = 0, e->body.vy = 1; break;
    case SQ_PULL_OUT: decelerate(e, 0.2f); break;
    case SQ_RECOVER: e->body.vx *= 0.9f, e->body.vy *= 0.9f; break;   /* (DecelerateV2) */
  }
}

static void mosquito_update(Enemy *e) {
  bool done = (e->anim.events & ANIM_DONE) != 0;
  /* (the hit's direction, its Recoil's HIT event: in a lunge, it turns it) */
  if ((e->rc_flags & RF_HIT) && e->st == SQ_LUNGING) {
    switch (e->rc_dir) {
      case 0: sq_lunge(e, rand_range(-5, 5), true, 0.5f); break;
      case 2: sq_lunge(e, rand_range(175, 185), false, 0.5f); break;
      case 1: sq_lunge(e, rand_range(85, 95), false, 0.5f); break;
      default: sq_lunge(e, rand_range(265, 275), false, 0.5f); break;
    }
    return;
  }
  switch (e->st) {
    case SQ_IDLE:
      e->ang = 0;
      face_direction_p(e, &e->pause0, CLIP(e, R_A1), 0.5f);
      if (e->can_see && e->in_alert) {
        /* Startle: facing the Knight, till the clip ends */
        e->st = SQ_STARTLE;
        anim_play_from_frame(&e->anim, CLIP(e, R_A2), 0);
        sq_face(e, -1);
      }
      break;
    case SQ_STARTLE:
      if (done) sq_in_sight(e);
      break;
    case SQ_IN_SIGHT:
      sq_face(e, CLIP(e, R_A1));
      sq_out_of_sight(e);   /* (NextFrameEvent) */
      break;
    case SQ_OUT_OF_SIGHT:
      sq_face(e, CLIP(e, R_A1));
      if (e->can_see && e->in_alert) sq_in_sight(e);
      else if ((e->t0 -= DT) <= 0) {
        e->st = SQ_STOP, e->wait = 0.75f;
        anim_play(&e->anim, CLIP(e, R_IDLE));
        decelerate(e, 0.3f);
      }
      break;
    case SQ_STOP:
      if ((e->wait -= DT) <= 0) sq_idle(e);
      break;
    case SQ_ATTACK_PAUSE:
      sq_face(e, -1);
      if ((e->wait -= DT) <= 0) {
        /* Still In Range?: then Attack Antic: up a little, its TileDetector off */
        if (!e->in_alert || !e->can_see) sq_in_sight(e);
        else {
          e->st = SQ_ATTACK_ANTIC, e->wait = 0.25f, e->b0 = true;
          e->body.vx = 0, e->body.vy = 1;
          anim_play_from_frame(&e->anim, CLIP(e, R_A3), 0);
          sq_face(e, -1);
        }
      }
      break;
    case SQ_ATTACK_ANTIC:
      if (done || (e->wait -= DT) <= 0) {
        /* Attack Aim: at the Knight, a little below; till its antic ends */
        float dx = hero_x() - e->body.x, dy = hero_y() - 0.5f - e->body.y;
        e->qx = atan2f(dy, dx) * 180 / (float)M_PI;
        if (e->qx < 0) e->qx += 360;
        e->st = SQ_ATTACK_AIM;
        e->body.vx = 0, e->body.vy = 1;
      }
      break;
    case SQ_ATTACK_AIM:
      if (!e->anim.playing) {
        /* Check Dir: no recoil now; Lunge R or L */
        e->rc_base = 0;
        anim_play(&e->anim, CLIP(e, R_A4));
        sq_lunge(e, e->qx, e->qx < 90 || e->qx > 270, 1.25f);
      }
      break;
    case SQ_LUNGING:
      if (e->b1) {
        /* HIT WALL: Pull Out, bouncing back */
        e->st = SQ_PULL_OUT, e->ang = 0;
        e->rc_base = 20;
        anim_play_from_frame(&e->anim, CLIP(e, R_DEATH_AIR), 0);
        e->body.vx = e->ax * -0.3f, e->body.vy = e->ay * -0.3f;
        decelerate(e, 0.2f);
        break;
      }
      e->ax = e->body.vx, e->ay = e->body.vy;
      if ((e->t1 -= DT) <= 0) {
        e->st = SQ_RECOVER, e->wait = 0.5f, e->ang = 0;
        anim_play(&e->anim, CLIP(e, R_IDLE));
        e->body.vx *= 0.9f, e->body.vy *= 0.9f;
      }
      break;
    case SQ_PULL_OUT:
      if (done) {
        e->st = SQ_RECOVER, e->wait = 0.5f, e->ang = 0;
        anim_play(&e->anim, CLIP(e, R_IDLE));
        e->body.vx *= 0.9f, e->body.vy *= 0.9f;
      }
      break;
    case SQ_RECOVER:
      if ((e->wait -= DT) <= 0) sq_in_sight(e);
      break;
  }
}

/* ---------------------------------------------------------------- Obbles: Fatty Fly Attack and fat fly bounce (it
 * bounces off the walls at a steady speed; every few seconds, or as it is hit, it stops and spits four shots) */
enum { FA_SLEEP, FA_WAIT, FA_ANTIC, FA_ANTIC2, FA_ATTACK, FA_CD };
enum { FB_INIT, FB_FLY, FB_STOPPED };
#define FB_SPEED 4.0f
/* (st: Fatty Fly Attack's state; c0: fat fly bounce's; qx: its Angle; b0: Facing Right; b1: it touched something this
 * step (OnCollisionStay2D), its normal in ax, ay; wait: the attack's timer) */

/* Left or Right?, Face Left / Right, Fly 2 */
static void fb_fly(Enemy *e) {
  float a = e->qx;
  e->b0 = !(a >= 90 && a < 270);
  set_scale_x(e, e->b0 ? -fabsf(e->sx) : fabsf(e->sx));
  e->c0 = FB_FLY;
}

/* Collision Check: off the wall it met, by its normal (else a way at random) */
static void fb_bounce(Enemy *e) {
  float nx = e->ax, ny = e->ay;
  int ev = nx == -1 ? 0 : nx == 1 ? 1 : ny == -1 ? 2 : ny == 1 ? 3 : (int)rand_range(0, 3.999f);
  bool up = e->qx < 180;
  switch (ev) {
    case 0: e->qx = up ? rand_range(140, 170) : rand_range(190, 220); break;   /* (Hit Right) */
    case 1: e->qx = up ? rand_range(10, 40) : rand_range(320, 350); break;     /* (Hit Left) */
    case 2: e->qx = e->b0 ? rand_range(320, 350) : rand_range(190, 220); break; /* (Hit Up) */
    default: e->qx = e->b0 ? rand_range(10, 40) : rand_range(140, 170); break; /* (Hit Down) */
  }
  fb_fly(e);
}

static void fatfly_wait(Enemy *e) {
  e->st = FA_WAIT, e->wait = rand_range(2, 3);
  if (e->c0 == FB_STOPPED) fb_fly(e);   /* (WAKE) */
}

static void fatfly_antic(Enemy *e) {
  e->st = FA_ANTIC, e->wait = 0.35f;
  decelerate(e, 0.1f);
}

static void fatfly_start(Enemy *e) {
  e->st = FA_SLEEP, e->c0 = FB_INIT;
  anim_play(&e->anim, CLIP(e, R_IDLE));
}

static void fatfly_fixed(Enemy *e) {
  if (e->st == FA_ANTIC || e->st == FA_ANTIC2) decelerate(e, 0.1f);
}

static void fatfly_update(Enemy *e) {
  bool done = (e->anim.events & ANIM_DONE) != 0, trig = (e->anim.events & ANIM_TRIGGER) != 0;
  /* fat fly bounce */
  switch (e->c0) {
    case FB_INIT: {
      float dx = hero_x() - e->body.x, dy = hero_y() - e->body.y;
      if (dx * dx + dy * dy < 25 * 25) {
        /* Aim: wakes its attack, flies at the Knight */
        if (e->st == FA_SLEEP) fatfly_wait(e);
        e->qx = atan2f(dy, dx) * 180 / (float)M_PI;
        if (e->qx < 0) e->qx += 360;
        fb_fly(e);
      }
      break;
    }
    case FB_FLY:
      if (e->b1) fb_bounce(e);
      break;
  }
  e->b1 = false;
  /* Fatty Fly Attack (TAKE DAMAGE: its attack at once) */
  if (e->flags & 8) {
    fatfly_antic(e);
    return;
  }
  switch (e->st) {
    case FA_WAIT:
      if ((e->wait -= DT) <= 0) fatfly_antic(e);
      break;
    case FA_ANTIC:
      if ((e->wait -= DT) <= 0) {
        /* Attack Antic 2: the bouncing stopped (its angle kept from its velocity), its spit coming */
        e->st = FA_ANTIC2;
        if (e->c0 == FB_FLY || e->c0 == FB_INIT) {
          e->qx = atan2f(e->body.vy, e->body.vx) * 180 / (float)M_PI;
          if (e->qx < 0) e->qx += 360;
          e->c0 = FB_STOPPED;
        }
        anim_play_from_frame(&e->anim, CLIP(e, R_A1), 0);
        decelerate(e, 0.1f);
      }
      break;
    case FA_ANTIC2:
      if (trig || done) {
        /* Attack: four shots, corner-wise */
        for (int k = 0; k < 4; k++) {
          float a = (45 + 90 * k) * (float)M_PI / 180;
          Bullet *b = bullet_new(e->body.x, e->body.y, cosf(a) * 12, sinf(a) * 12, 0.05f);
          if (b) b->scale *= 0.7f;
        }
        e->st = FA_ATTACK, e->wait = 0.5f;
      }
      break;
    case FA_ATTACK:
      if (!e->anim.playing || (e->wait -= DT) <= 0) {
        e->st = FA_CD, e->wait = 0.5f;
        anim_play(&e->anim, CLIP(e, R_IDLE));
      }
      break;
    case FA_CD:
      if ((e->wait -= DT) <= 0) fatfly_wait(e);
      break;
  }
  /* (Fly 2: its velocity at its angle, every frame) */
  if (e->c0 == FB_FLY) {
    float a = e->qx * (float)M_PI / 180;
    e->body.vx = cosf(a) * FB_SPEED, e->body.vy = sinf(a) * FB_SPEED;
  }
}

/* ---------------------------------------------------------------- Moss Chargers: Mossy Control (hidden in the grass;
 * as the Knight comes into its range it bursts up a way off and charges, invincible, along the ground; struck, it is
 * thrown out stunned, then runs from the Knight and digs back in) */
enum { MC_INIT_PAUSE, MC_HIDDEN, MC_EMERGE_PAUSE, MC_EMERGE_SIDE, MC_APPEAR, MC_CHARGE, MC_SUBMERGE, MC_SUBMERGE_GRASS,
       MC_SUBMERGE_CD, MC_FLY, MC_IN_AIR, MC_GET_UP, MC_RUN, MC_DIG_START, MC_DIG };
#define MC_CHARGE_SPEED 15.0f
/* (wait: a state's; start_x, start_y: where it hides; ax, ay: X Min, X Max; qx: Appear X; qy: Current Charge Speed;
 * b0: hidden (its renderer off); b1: it touched something new this step; c0: the side it emerges (0 right, 1 left);
 * c1: its charge's rays' frame count) */
static void mc_collider(Enemy *e, bool on) {
  if (on) e->flags &= (uint8_t)~1, e->body.mask = CF_SOLID;
  else e->flags |= 1, e->body.mask = 0;
}

/* Hero Beyond?, Left or Right?, Emerge Right / Left (a frame later the other way, if beyond its range) */
static void mc_emerge_side(Enemy *e, int side) {
  e->st = MC_EMERGE_SIDE, e->c0 = (uint8_t)side;
  if (side == 0) {
    e->qx = hero_x() + 14, set_scale_x(e, -fabsf(e->sx)), e->qy = -MC_CHARGE_SPEED;
    if (e->qx > e->ay) return;   /* (LEFT: Pause, then Emerge Left) */
  } else {
    e->qx = hero_x() - 14, set_scale_x(e, fabsf(e->sx)), e->qy = MC_CHARGE_SPEED;
    if (e->qx < e->ax) return;   /* (RIGHT: Pause 2, then Emerge Right) */
  }
  /* Emerge: up out of the grass a little way off the Knight, sliding his way */
  cam_shake(SHAKE_AVERAGE);
  e->body.x = e->qx, e->body.y = e->start_y;
  e->body.vx = e->qy * 0.25f, e->body.vy = 0;
  mc_collider(e, true);
  e->b0 = false;
  anim_play_from_frame(&e->anim, CLIP(e, R_A1), 0);
  e->st = MC_APPEAR;
}

/* its two rays as it charges: a wall ahead, no ground ahead: SUBMERGE */
static bool mc_charge_rays(const Enemy *e) {
  float dir = e->c0 == 0 ? -1.0f : 1.0f;
  return phys_ray(e->body.x, e->body.y - 0.5f, dir, 0, 5.5f, CF_TERRAIN, NULL) ||
         !phys_ray(e->body.x + 6.5f * dir, e->body.y - 0.5f, 0, -1, 3, CF_TERRAIN, NULL);
}

static void mc_submerge(Enemy *e) {
  e->st = MC_SUBMERGE;
  decelerate(e, 0.7f);
  anim_play_from_frame(&e->anim, CLIP(e, R_A3), 0);
}

static void mc_submerge_cd(Enemy *e) {
  e->st = MC_SUBMERGE_CD, e->wait = 0.35f;
  mc_collider(e, false);
  e->body.vx = e->body.vy = 0;
  e->flags |= 4;   /* (invincible) */
  e->b0 = true;
  e->body.x = e->start_x, e->body.y = e->start_y;
}

/* Line Loop, Burst: struck, thrown out stunned, away from the blow */
static void mc_burst(Enemy *e) {
  e->body.gravity_scale = 1.5f;
  cam_shake(SHAKE_ENEMY_KILL);
  anim_play(&e->anim, CLIP(e, R_A4));
  float a = 90, speed = 20;
  switch (e->rc_flags >> RF_DIR_SHIFT & 3) {
    case 0: a = 70, speed = 18; break;
    case 2: a = 110, speed = 18; break;
    case 3: a = 270, speed = 10; break;
  }
  a *= (float)M_PI / 180;
  e->body.vx = cosf(a) * speed, e->body.vy = sinf(a) * speed;
  e->st = MC_FLY;   /* (a frame later, In Air) */
}

/* Direction (after Get Up): runs from the Knight */
static void mc_run(Enemy *e, bool right) {
  e->st = MC_RUN, e->wait = 1, e->b1 = false;
  anim_play_from_frame(&e->anim, CLIP(e, R_A6), 0);
  set_scale_x(e, right ? fabsf(e->sx) : -fabsf(e->sx));
  e->c0 = right ? 1 : 0;
}

static void mc_dig_start(Enemy *e) {
  /* Dig Start: its collider off, no gravity, slowing; falls if nothing is under it */
  e->st = MC_DIG_START;
  e->rc_state = RC_READY;   /* (CANCEL RECOIL) */
  mc_collider(e, false);
  e->body.gravity_scale = 0;
  decelerate(e, 0.4f);
  anim_play_from_frame(&e->anim, CLIP(e, R_A7), 0);
}

static bool mc_dig_check(const Enemy *e) { return phys_ray(e->body.x, e->body.y, 0, -1, 1, CF_SOLID, NULL); }

static void mc_in_air(Enemy *e) {
  e->st = MC_IN_AIR;
  e->flags &= (uint8_t)~4;   /* (not invincible) */
  mc_collider(e, true);
  e->body.gravity_scale = 1.5f;
  anim_play(&e->anim, CLIP(e, R_A4));
}

static void mosscharger_start(Enemy *e) {
  e->st = MC_INIT_PAUSE, e->wait = 0.5f;
}

static void mosscharger_fixed(Enemy *e) {
  /* (ObjectBounce samples its speed every fourth step) */
  if ((e->pause0 += 1) > 3) e->pause0 = 0, e->pause1 = sqrtf(e->body.vx * e->body.vx + e->body.vy * e->body.vy);
  switch (e->st) {
    case MC_SUBMERGE: case MC_SUBMERGE_GRASS: decelerate(e, 0.7f); break;
    case MC_RUN: {
      float vx = e->body.vx + (e->c0 ? 0.5f : -0.5f);
      e->body.vx = vx > 10 ? 10 : vx < -10 ? -10 : vx;   /* (AccelerateVelocity) */
      break;
    }
    case MC_DIG_START: decelerate(e, 0.4f); break;
  }
}

static void mosscharger_update(Enemy *e) {
  bool done = (e->anim.events & ANIM_DONE) != 0, trig = (e->anim.events & ANIM_TRIGGER) != 0;
  /* (BLOCKED HIT, everywhere; TAKE DAMAGE as it charges: Line Loop, Burst) */
  if ((e->rc_flags & RF_BLOCKED) || (e->st == MC_CHARGE && (e->flags & 8))) {
    mc_burst(e);
    return;
  }
  switch (e->st) {
    case MC_INIT_PAUSE:
      if ((e->wait -= DT) <= 0) {
        /* Init: hidden, out of reach; its range's length from where it starts */
        mc_collider(e, false);
        e->b0 = true;
        e->start_x = e->body.x, e->start_y = e->body.y;
        float len = e->ar_r;
        e->ax = e->start_x - len + 2, e->ay = e->start_x + len - 2;
        e->st = MC_HIDDEN;
      }
      break;
    case MC_HIDDEN:
      if (e->in_alert) e->st = MC_EMERGE_PAUSE, e->wait = rand_range(0.5f, 1);
      break;
    case MC_EMERGE_PAUSE:
      if ((e->wait -= DT) <= 0) {
        if (hero_x() > e->ay || hero_x() < e->ax) e->st = MC_HIDDEN;
        else mc_emerge_side(e, rand_range(0, 2) < 1 ? 0 : 1);
      }
      break;
    case MC_EMERGE_SIDE:
      mc_emerge_side(e, 1 - e->c0);
      break;
    case MC_APPEAR:
      if (done) {
        e->st = MC_CHARGE, e->c1 = 1;
        anim_play(&e->anim, CLIP(e, R_A2));
        e->body.vx = e->qy;
      }
      break;
    case MC_CHARGE:
      if (--e->c1 == 0) {
        e->c1 = 2;
        if (mc_charge_rays(e)) mc_submerge(e);
      }
      break;
    case MC_SUBMERGE:
      if (trig || done) e->st = MC_SUBMERGE_GRASS;
      break;
    case MC_SUBMERGE_GRASS:
      if (!e->anim.playing) mc_submerge_cd(e);
      break;
    case MC_SUBMERGE_CD:
      if ((e->wait -= DT) <= 0) e->st = MC_HIDDEN;   /* (Play Range) */
      break;
    case MC_FLY:
      mc_in_air(e);
      break;
    case MC_IN_AIR: {
      bool ground = false;
      for (int c = 0; c < e->body.ncontacts; c++) ground |= e->body.cny[c] > 0.5f;
      if (ground) {
        /* Land, Get Up */
        e->body.vx = 0;
        e->st = MC_GET_UP;
        anim_play_from_frame(&e->anim, CLIP(e, R_A5), 0);
      }
      break;
    }
    case MC_GET_UP:
      if (done) {
        /* Direction: from its turn's third frame, away from the Knight */
        anim_play_from_frame(&e->anim, CLIP(e, R_A6), 2);
        mc_run(e, hero_x() <= e->body.x);
        e->anim.playing = true;
      }
      break;
    case MC_RUN: {
      bool right = e->c0 == 1;
      /* (the Knight passed: the other way) */
      if (right ? hero_x() > e->body.x : hero_x() < e->body.x) {
        mc_run(e, !right);
        break;
      }
      float dir = right ? 1.0f : -1.0f;
      bool wall = phys_ray(e->body.x, e->body.y, dir, 0, 2, CF_SOLID, NULL);
      bool ground = phys_ray(e->body.x + 3 * dir, e->body.y, 0, -1, 1.3f, CF_SOLID, NULL);
      if (wall || !ground || (e->wait -= DT) <= 0) mc_dig_start(e);   /* (On Ground?, Dig Start) */
      break;
    }
    case MC_DIG_START:
      if (!mc_dig_check(e)) mc_in_air(e);   /* (FALL) */
      else if (trig) {
        e->st = MC_DIG;
        e->body.vx = 0;
      }
      break;
    case MC_DIG:
      if (!mc_dig_check(e)) mc_in_air(e);
      else if (!e->anim.playing) mc_submerge_cd(e);
      break;
  }
}

/* ---------------------------------------------------------------- Moss Knights: Moss Knight Control (a Walker; with the
 * Knight in its Attack Range it raises its shield against where he is, then slashes (once or twice), jumps back, or
 * spits grass balls; some sleep in the moss till struck or till their battle starts) */
enum { MK_PAUSE, MK_DETECT, MK_SHIELD, MK_BLOCK, MK_UNSHIELD, MK_A1_ANTIC, MK_A1_LUNGE, MK_A1_ON, MK_A1_OFF, MK_A1_END,
       MK_A2_ANTIC, MK_A2_SLASH, MK_A2_END, MK_SLASH_END, MK_EVADE_ANTIC, MK_EVADE, MK_EVADE_END, MK_SHOOT_ANTIC, MK_SHOOT,
       MK_SHOT_END, MK_SLEEP, MK_SHAKE, MK_WAKE };
enum { MKS_RIGHT_LOW, MKS_RIGHT_HIGH, MKS_LEFT_HIGH, MKS_LEFT_LOW };
enum { MKF_SPIT = 1, MKF_QUICK = 2, MKF_LOW6 = 4 };   /* (c4: After Evade SPIT, Slash Quick, Low Block Direction 6) */
#define MK_DORMANT 1
#define MK_START_BATTLE 4
#define MK_EFFECT_SLASH2 1   /* (its effect's sprite: Slash2 Effect's place) */
/* (c0: its shield; c1: Shot Repeats; c2: Ct Slash, Ct Jump Back; c3: Ct Single, Ct Double (2 bits each); c4: MKF_*;
 * qx: Attack Pause; qy: Lunge1 Speed; ax: Evade Speed; ay: Spit X; t0, t1: its Spit and Evade Ranges' on (> 0) or off
 * (< 0) time left; sub: its slash effect, fx its kind + 1 (drawn while it plays)) */

/* a named range of its own (ET_RANGE by its child's name): the Knight in it? */
static bool mk_in(const Enemy *e, int name) {
  const Ent *d = ent_at(e->ent);
  for (int i = 1; i <= d->s0; i++)
    if (d[i].type == ENT_BOX && d[i].flags == ET_RANGE && d[i].s0 == name) {
      float x, y, rx, hy;
      enemy_range(e, &d[i], &x, &y, &rx, &hy);
      const Body *h = &g_hero.body;
      return !g_hero.hidden && fabsf(h->x + h->ox - e->body.x - x) < rx + h->hx && fabsf(h->y + h->oy - e->body.y - y) < hy + h->hy;
    }
  return false;
}
static bool mk_attack_range(const Enemy *e) { return mk_in(e, STR_ATTACK_RANGE); }
static bool mk_evade_range(const Enemy *e) { return e->t1 > 0 && mk_in(e, STR_EVADE_RANGE); }
static bool mk_spit_range(const Enemy *e) { return e->t0 > 0 && mk_in(e, STR_SPIT_RANGE); }
/* (LineOfSightDetector: the Knight in any of its ranges, nothing between) */
static bool mk_sees(const Enemy *e) {
  if (!(e->in_alert || mk_attack_range(e) || mk_evade_range(e) || mk_spit_range(e))) return false;
  float dx = hero_x() - e->body.x, dy = hero_y() - e->body.y, l = sqrtf(dx * dx + dy * dy);
  return l < 1e-4f || !phys_ray(e->body.x, e->body.y, dx / l, dy / l, l, CF_TERRAIN, NULL);
}

static const Ent *mk_vars(const Enemy *e) {
  const Ent *v = enemy_rec(e, ET_VARS);
  return v;
}

/* SendRandomEventV2: of two, by weight, neither more than its most in a row (2-bit counts in *ct) */
static int random_v2(uint8_t *ct, float w0, int max0, int max1) {
  for (;;) {
    int k = rand_range(0, 1) < w0 ? 0 : 1, n = (*ct >> (2 * k)) & 3;
    if (n < (k ? max1 : max0)) {
      *ct = (uint8_t)((n + 1) << (2 * k));
      return k;
    }
  }
}

static void mk_slash_effect(Enemy *e, int clip, int kind) {
  anim_play_from_frame(&e->sub, clip, 0);
  e->fx = (uint8_t)(kind + 1);
}

/* a child's Randomise: its collider on for a while, then off (on: t > 0, the time left; off: t < 0) */
static void randomise(float *t, float on0, float on1, float off0, float off1) {
  if (*t > 0) {
    if ((*t -= DT) <= 0) *t = -rand_range(off0, off1);
  } else if ((*t += DT) >= 0)
    *t = rand_range(on0, on1);
}

static void mk_hitbox(Enemy *e, int name, bool on) {
  for (int k = 0; k < 8; k++) {
    const Ent *h = hitbox_rec(e, k);
    if (!h) break;
    if (h->s0 == name) e->hb_on = on ? (uint8_t)(e->hb_on | 1 << k) : (uint8_t)(e->hb_on & ~(1 << k));
  }
}

/* Reset: the shield's directions gone (its invincibility as it was), walking; Detect */
static void mk_reset(Enemy *e) {
  e->inv_dir = 0;
  e->st = MK_DETECT;
  walker_start(e);
}

/* the shield its stance shows (Shield Right / Left, High / Low) */
static void mk_shield(Enemy *e, int s) {
  static const uint8_t inv[4] = {6, 4, 4, 5};
  bool right = s == MKS_RIGHT_LOW || s == MKS_RIGHT_HIGH, high = s == MKS_RIGHT_HIGH || s == MKS_LEFT_HIGH;
  e->st = MK_SHIELD, e->c0 = (uint8_t)s;
  e->c4 = (uint8_t)((e->c4 & ~MKF_LOW6) | (right ? MKF_LOW6 : 0));
  e->inv_dir = inv[s];
  e->wk_facing = right ? 1 : -1;   /* (SetWalkerFacing) */
  anim_play_from_frame(&e->anim, high ? CLIP_MOSSKNIGHT_SHIELD_TOP : CLIP_MOSSKNIGHT_SHIELD_FRONT, 0);
  e->qy = right ? 11 : -11;
  set_scale_x(e, right ? -fabsf(e->sx) : fabsf(e->sx));
}

/* (where the Knight is: right of it, above its middle) */
static bool mk_hero_right(const Enemy *e) { return hero_x() > e->body.x; }
static bool mk_hero_left(const Enemy *e) { return hero_x() < e->body.x; }
static bool mk_hero_high(const Enemy *e) { return hero_y() > e->body.y + 2.5f; }

static void mk_attack1(Enemy *e) {
  /* Attack 1 Antic: its slash (or the quick one) and its effect; its shield's low side still up */
  bool quick = (e->c4 & MKF_QUICK) != 0;
  e->st = MK_A1_ANTIC;
  anim_play_from_frame(&e->anim, quick ? CLIP_MOSSKNIGHT_SLASH_QUICK : CLIP_MOSSKNIGHT_SLASH, 0);
  mk_slash_effect(e, quick ? CLIP_MOSSKNIGHT_SLASH_EFFECT_QUICK : CLIP_MOSSKNIGHT_SLASH_EFFECT, 0);
  e->inv_dir = (e->c4 & MKF_LOW6) ? 6 : 5;
}

/* Attack Choice: a slash, or a jump back (then a spit or a slash) */
static void mk_evade_check(Enemy *e);
static void mk_attack_choice(Enemy *e) {
  e->body.vx = 0;
  e->c4 &= (uint8_t)~MKF_QUICK;
  if (random_v2(&e->c2, 0.75f, 3, 2) == 0) mk_attack1(e);
  else {
    /* After Evade Choice: then a spit or a slash, even odds */
    if (rand_range(0, 2) < 1) e->c4 |= MKF_SPIT;
    else e->c4 &= (uint8_t)~MKF_SPIT;
    mk_evade_check(e);
  }
}

/* Shoot Check: not invincible; a spit at the Knight, facing him */
static void mk_shoot_check(Enemy *e) {
  e->flags &= (uint8_t)~4;
  e->c1--;
  anim_play_from_frame(&e->anim, CLIP_MOSSKNIGHT_SHOOT, 0);
  if (mk_hero_right(e)) set_scale_x(e, -fabsf(e->sx));
  else if (mk_hero_left(e)) set_scale_x(e, fabsf(e->sx));
  /* Shoot Antic: where it aims */
  float x = (hero_x() - e->body.x) * 1.1f + rand_range(-1, 1);
  e->ay = x < -15 ? -15 : x > 15 ? 15 : x;
  e->st = MK_SHOOT_ANTIC;
}

static void mk_set_repeater(Enemy *e) {
  e->body.vx = 0;
  walker_stop(e, STOP_CONTROLLED);
  e->c1 = 2;
  mk_shoot_check(e);
}

/* Evade Check: not invincible; away from the Knight, unless a wall is behind (Evade Cancel) */
static void mk_evade_check(Enemy *e) {
  e->body.vx = 0;
  e->flags &= (uint8_t)~4;
  anim_play_from_frame(&e->anim, CLIP_MOSSKNIGHT_EVADE, 0);
  bool right = mk_hero_right(e);
  if (!right && !mk_hero_left(e)) {
    e->st = MK_EVADE_ANTIC;   /* (neither: it stays there) */
    return;
  }
  bool wall = phys_ray(e->body.x, e->body.y, -1, 0, 4, CF_TERRAIN, NULL);   /* (from its Centre Point, to its own left) */
  set_scale_x(e, right ? -fabsf(e->sx) : fabsf(e->sx));
  e->ax = right ? -28 : 28;
  e->qy = right ? 13 : -13;
  if (wall) {
    /* Evade Cancel: a spit's evade slashes; a slash's evades all the same */
    if (e->c4 & MKF_SPIT) mk_attack1(e);
    else e->st = MK_EVADE_ANTIC;
    return;
  }
  e->st = MK_EVADE_ANTIC;
}

static void mossknight_wake(Enemy *e) {
  /* Shake, then Wake */
  if (e->st != MK_SLEEP) return;
  e->st = MK_SHAKE, e->wait = 1;
  anim_play(&e->anim, CLIP_MOSSKNIGHT_SHAKE);
}

static void mossknight_start(Enemy *e) {
  walker_init(e);
  e->t0 = rand_range(1, 2), e->t1 = rand_range(2, 2.5f);   /* (its ranges' Randomise: On) */
  const Ent *v = mk_vars(e);
  if (v && (v->a & MK_DORMANT)) {
    /* Sleep: kinematic, harmless, untouchable, in the moss */
    e->st = MK_SLEEP;
    walker_stop(e, STOP_CONTROLLED);
    anim_play(&e->anim, rand_range(0, 2) < 1 ? CLIP_MOSSKNIGHT_DORMANT_1 : CLIP_MOSSKNIGHT_DORMANT_2);
    e->flags |= 4 | 1;
    e->inv_dir = 0;
    e->damage = 0;
    e->body.vx = e->body.vy = 0, e->body.gravity_scale = 0;
  } else
    e->st = MK_PAUSE;
}

/* (its Wake Box struck by the nail, the Knight near it: it wakes, or its battle starts) */
static void mossknight_nailed(Enemy *e) {
  if (e->st != MK_SLEEP || fabsf(hero_x() - e->body.x) >= 8) return;
  const Ent *v = mk_vars(e);
  if (v && (v->a & MK_START_BATTLE)) arena_start();   /* (BATTLE EARLY START) */
  else mossknight_wake(e);
}

static void mossknight_update(Enemy *e) {
  bool done = (e->anim.events & ANIM_DONE) != 0, trig = (e->anim.events & ANIM_TRIGGER) != 0;
  /* its Spit and Evade Ranges' Randomise: on 1-2 s, off 1-2 s; on 2-2.5 s, off 2.5-3.5 s */
  randomise(&e->t0, 1, 2, 1, 2);
  randomise(&e->t1, 2, 2.5f, 2.5f, 3.5f);
  if (e->fx) {
    e->sub.events = 0;
    anim_update(&e->sub, DT);
    if (!e->sub.playing) e->fx = 0;
  }
  switch (e->st) {
    case MK_PAUSE:
      e->st = MK_DETECT;   /* (Pause Frame, Initialise) */
      break;
    case MK_DETECT: {
      walker_update(e);
      bool see = mk_sees(e), spit = mk_spit_range(e), evade = mk_evade_range(e), attack = see && mk_attack_range(e);
      /* (the frame's last event wins: ATTACK, EVADE, SPIT) */
      if (attack) {
        /* Shield Start: its pause before it attacks, still and invincible; then its stance */
        e->qx = rand_range(0.75f, 1.1f);
        walker_stop(e, STOP_CONTROLLED);
        e->body.vx = 0;
        e->flags |= 4;
        bool right = mk_hero_right(e), high = mk_hero_high(e);
        mk_shield(e, high && right ? MKS_RIGHT_HIGH : high ? MKS_LEFT_HIGH : right ? MKS_RIGHT_LOW : MKS_LEFT_LOW);
      } else if (evade) {
        walker_stop(e, STOP_CONTROLLED);
        e->c4 &= (uint8_t)~MKF_SPIT, e->c4 |= MKF_QUICK;   /* (Evade Hero: then a quick slash) */
        mk_evade_check(e);
      } else if (spit)
        mk_set_repeater(e);
      break;
    }
    case MK_SHIELD: {
      if (e->rc_flags & RF_BLOCKED) {
        /* Block: its shield bumped, then its attack */
        bool high = e->c0 == MKS_RIGHT_HIGH || e->c0 == MKS_LEFT_HIGH;
        anim_play_from_frame(&e->anim, high ? CLIP_MOSSKNIGHT_SHIELD_TOP_BUMP : CLIP_MOSSKNIGHT_SHIELD_FRONT_BUMP, 0);
        e->st = MK_BLOCK, e->wait = 0.4f;
        break;
      }
      bool left = mk_hero_left(e), right = mk_hero_right(e), high = mk_hero_high(e), low = !high;
      int s = e->c0, to = -1;
      /* (the other stances' events, in its actions' order; the last of the frame wins) */
      if (s != MKS_LEFT_HIGH && high && left) to = MKS_LEFT_HIGH;
      if (s != MKS_RIGHT_HIGH && high && right) to = MKS_RIGHT_HIGH;
      if (s != MKS_LEFT_LOW && low && left) to = MKS_LEFT_LOW;
      if (s != MKS_RIGHT_LOW && low && right) to = MKS_RIGHT_LOW;
      int ev = to >= 0 ? 1 : 0;   /* (1 a stance, 2 COUNTER END, 3 LEFT RANGE, 4 EVADE) */
      if ((e->qx -= DT) <= 0) ev = 2;
      if (!(mk_attack_range(e) && mk_sees(e))) ev = 3;
      if (mk_evade_range(e)) ev = 4;
      if (ev == 1) mk_shield(e, to);
      else if (ev == 2) mk_attack_choice(e);
      else if (ev == 3) {
        /* Unshield: its shield down, then Reset */
        bool hi = s == MKS_RIGHT_HIGH || s == MKS_LEFT_HIGH;
        anim_play_from_frame(&e->anim, hi ? CLIP_MOSSKNIGHT_UNSHIELD_TOP : CLIP_MOSSKNIGHT_UNSHIELD_FRONT, 0);
        e->st = MK_UNSHIELD;
      } else if (ev == 4) {
        e->c4 &= (uint8_t)~MKF_SPIT, e->c4 |= MKF_QUICK;
        mk_evade_check(e);
      }
      break;
    }
    case MK_BLOCK:
      if ((e->wait -= DT) <= 0) mk_attack_choice(e);
      break;
    case MK_UNSHIELD:
      if (!e->anim.playing) mk_reset(e);
      break;
    case MK_A1_ANTIC:
      if (trig) {
        /* Attack1 Lunge: vulnerable, lunging at the Knight */
        e->flags &= (uint8_t)~4, e->inv_dir = 0;
        e->body.vx = e->qy;
        e->st = MK_A1_LUNGE;
      }
      break;
    case MK_A1_LUNGE:
      if (trig) mk_hitbox(e, STR_SLASH_HITBOX, true), e->st = MK_A1_ON;
      break;
    case MK_A1_ON:
      if (trig) mk_hitbox(e, STR_SLASH_HITBOX, false), e->st = MK_A1_OFF;
      break;
    case MK_A1_OFF:
      if (trig) e->body.vx = 0, e->st = MK_A1_END;
      break;
    case MK_A1_END:
      if (!e->anim.playing) {
        /* Slash 2?: facing the Knight still, a second slash or not */
        bool face_right = e->sx < 0, hero_right = mk_hero_right(e);
        if (hero_right != face_right || random_v2(&e->c3, 0.5f, 2, 2) == 1) {
          e->st = MK_SLASH_END;
          anim_play_from_frame(&e->anim, CLIP_MOSSKNIGHT_SLASH_END, 0);
        } else {
          e->st = MK_A2_ANTIC;
          anim_play_from_frame(&e->anim, CLIP_MOSSKNIGHT_SLASH_2, 0);
          mk_slash_effect(e, CLIP_MOSSKNIGHT_SLASH_2_EFFECT, MK_EFFECT_SLASH2);
        }
      }
      break;
    case MK_A2_ANTIC:
      if (trig) {
        mk_hitbox(e, STR_SLASH2_HITBOX, true);
        e->body.vx = e->qy;
        e->st = MK_A2_SLASH;
      }
      break;
    case MK_A2_SLASH:
      if (trig) {
        mk_hitbox(e, STR_SLASH2_HITBOX, false);
        e->body.vx = 0;
        e->st = MK_A2_END;
      }
      break;
    case MK_A2_END:
    case MK_SLASH_END:
    case MK_SHOT_END:
      if (!e->anim.playing) mk_reset(e);
      break;
    case MK_EVADE_ANTIC:
      if (trig) e->body.vx = e->ax, e->st = MK_EVADE;
      break;
    case MK_EVADE:
      if (trig) e->body.vx = 0, e->st = MK_EVADE_END;
      break;
    case MK_EVADE_END:
      if (!e->anim.playing) {
        /* Evade Move Check: its After Evade */
        if (e->c4 & MKF_SPIT) mk_set_repeater(e);
        else mk_attack1(e);
      }
      break;
    case MK_SHOOT_ANTIC:
      if (trig) {
        /* Shoot: a grass ball from its Shot Point, up and at the Knight */
        grass_ball_spawn(e->body.x + (e->sx < 0 ? 2.07f : -2.07f), e->body.y + 1.17f, e->ay, 20);
        e->st = MK_SHOOT;
      }
      break;
    case MK_SHOOT:
      if (!e->anim.playing) {
        /* Repeat Check */
        if (e->c1 == 0 || rand_range(0, 1) < 0.6f) {
          e->st = MK_SHOT_END;
          anim_play_from_frame(&e->anim, CLIP_MOSSKNIGHT_SHOOT_END, 0);
        } else
          mk_shoot_check(e);
      }
      break;
    case MK_SLEEP:
      break;
    case MK_SHAKE:
      if ((e->wait -= DT) <= 0) {
        /* Wake: up, harmless no more; its Spit Range off a while (To Off) */
        e->st = MK_WAKE;
        cam_shake(SHAKE_ENEMY_KILL);
        e->t0 = -1 - rand_range(1, 2);
        anim_play_from_frame(&e->anim, CLIP_MOSSKNIGHT_WAKE, 0);
        e->flags &= (uint8_t)~(4 | 1);
        e->c4 |= 0x80;   /* (Spell Vulnerable) */
        e->damage = 1;
        e->body.gravity_scale = 1;
      }
      break;
    case MK_WAKE:
      if (done) mk_reset(e);
      break;
  }
}

static void mossknight_fixed(Enemy *e) {
  if (e->st == MK_DETECT && e->wk_state == WK_WALKING) {
    const Ent *w = walker_rec(e);
    if (w) e->body.vx = e->wk_facing > 0 ? w->y0 : w->x0;
  }
}

/* ---------------------------------------------------------------- Hornet (Greenpath): her Control and Stun Control;
 * her Needle and its thread, her Sphere Ball, her effects; then her corpse (Corpse Hornet 1): wounded, she leaves */
enum { HO_INERT, HO_REFIGHT_READY, HO_FLOURISH, HO_IDLE, HO_RUN_ANTIC, HO_RUN, HO_DMG_IDLE, HO_GDASH_ANTIC, HO_GDASH,
       HO_GDASH_REC1, HO_GDASH_REC2, HO_JUMP_ANTIC, HO_JUMP, HO_IN_AIR, HO_LAND, HO_ADASH_ANTIC, HO_ADASH, HO_WALL,
       HO_HARD_LAND, HO_SPHERE_ANTIC_G, HO_SPHERE_G, HO_SPHERE_REC_G, HO_SPHERE_ANTIC_A, HO_SPHERE_A, HO_SPHERE_REC_A,
       HO_THROW_ANTIC, HO_THROW, HO_THROWN, HO_THROW_REC, HO_EVADE_ANTIC, HO_EVADE, HO_EVADE_LAND, HO_STUN_START,
       HO_STUN_AIR, HO_STUN_LAND };
/* (her corpse's) */
enum { HC_LAUNCH, HC_IN_AIR, HC_LAND, HC_JUMP, HC_THROW_START, HC_THROW, HC_YANK, HC_END };
/* (her FSM's: the arena's) */
#define HO_FLOOR_Y 27.55f
#define HO_ROOF_Y 40.54f
#define HO_WALL_L 15.13f
#define HO_WALL_R 37.9f
#define HO_LEFT_X 16.06f
#define HO_RIGHT_X 36.53f
#define HO_SPHERE_Y 33.8f
#define HO_THROW_X_L 22.51f
#define HO_THROW_X_R 30.16f
#define HO_GRAVITY 1.5f
/* her collider by what she does: offset, size (Box Off / Box Size: Idle, Antic, GDash, ADash, Throw, Throwing) */
enum { HB_IDLE, HB_ANTIC, HB_GDASH, HB_ADASH, HB_THROW, HB_THROWING };
static const float ho_boxes[6][4] = {
    {0.1201f, -0.2645f, 0.8947f, 2.5647f}, {1.0812f, -0.8565f, 1.229f, 1.3807f}, {0.0504f, -0.7939f, 1.5633f, 1.506f},
    {0.1019f, 0, 1.4602f, 1.0251f},       {0.9968f, -0.2645f, 0.9817f, 2.5647f}, {0.1484f, -0.9688f, 1.3936f, 1.1562f}};
/* (Hit GDash's triangle; Hit ADash's the same, 0.51 up; the Needle's, from it) */
static const float ho_hit_pts[3][2] = {{-0.3343f, -0.6715f}, {-0.3788f, -0.9065f}, {-2.149f, -0.8611f}};
static const float ho_needle_pts[3][2] = {{0.6598f, 0.0988f}, {0.653f, -0.1331f}, {-1.8165f, -0.0018f}};
/* the counts her random choices keep (Ct ..., Ms ...) */
enum { HC_CT_IDLE, HC_CT_RUN, HC_CT_GSPHERE, HC_CT_MISS, HC_CT_AIRDASH, HC_CT_ASPHERE, HC_CT_GDASH, HC_CT_THROW,
       HC_MS_AIRDASH, HC_MS_ASPHERE, HC_MS_GDASH, HC_MS_THROW, HC_NUM };
/* her effects (children that play a clip once where she put them) */
typedef struct {
  float x, y, sx, sy, ang;
  Anim anim;
} HoFx;
#define HO_FX 2
static struct {
  uint8_t ct[HC_NUM];
  uint8_t loops[2];    /* (SendRandomEventV3's, Move Choice A's and B's: never reset) */
  uint8_t box;         /* (her collider's: HB_*) */
  bool escalated, will_sphere, kinematic, evade_bool, evade_on, evade_in;
  uint8_t hits_on;     /* (Hit GDash 1, Hit ADash 2) */
  float air_dash_pause, run_wait_min, run_wait_max, idle_wait_min, idle_wait_max, angle, return_x_scale;
  float evade_t;       /* (Evade Range's Fluctuate: its time left on or off) */
  /* Stun Control */
  uint8_t stun_st, combo, hits, stun_combo, stun_hit_max;
  float combo_t;
  /* her Needle: out, slowing, back (0 off); its place, velocity, time, where it went from, its rotation; its Tink */
  uint8_t needle_st;
  bool tink, thread_on, ball_started;
  float nx, ny, nvx, nvy, nt, n0x, n0y, nrx, nry, nang, tink_t;
  Anim needle, thread;
  /* her Sphere Ball (Grow) */
  bool ball_on;
  float ball_t;
  Anim ball;
  HoFx fx[HO_FX];
  /* her corpse: its Leave Anim, its Thread; its tween (iTweenMoveBy) */
  bool leave_on, cthread_on, corpse_shown;
  float tw_t, tw_x0, tw_y0, tw_dx, tw_dy, tw_time, rot, leave_dx;
  uint8_t tw_ease;
  Anim leave, cthread;
} ho;
enum { HSC_IDLE, HSC_COMBO, HSC_STOP };

static float ho_xscale(const Enemy *e) { return e->sx < 0 ? -1.0f : 1.0f; }

/* (a point in her own units -> the world: her scale's signs, then her rotation) */
static void ho_point(const Enemy *e, float lx, float ly, float *wx, float *wy) {
  lx *= ho_xscale(e), ly *= (float)e->sy;
  float a = e->ang * (float)M_PI / 180, c = cosf(a), s = sinf(a);
  *wx = e->body.x + lx * c - ly * s, *wy = e->body.y + lx * s + ly * c;
}

/* SetBoxCollider2DSizeVector (turned with her: its bounds) */
static void ho_box(Enemy *e, int b) {
  ho.box = (uint8_t)b;
  const float *q = ho_boxes[b];
  float cx, cy, a = e->ang * (float)M_PI / 180, c = fabsf(cosf(a)), s = fabsf(sinf(a));
  ho_point(e, q[0], q[1], &cx, &cy);
  e->body.ox = cx - e->body.x, e->body.oy = cy - e->body.y;
  e->body.hx = (q[2] * c + q[3] * s) / 2, e->body.hy = (q[2] * s + q[3] * c) / 2;
}

/* (put into the floor by a snap to the wall: the physics pushes her out of it, up, the least way) */
static void ho_unsink(Enemy *e, bool left) {
  float x = e->body.x + e->body.ox + (left ? 0.5f : -0.5f), top = e->body.y + e->body.oy + e->body.hy;
  float bottom = top - 2 * e->body.hy;
  PhysHit h;
  if (phys_ray(x, top, 0, -1, top - bottom, CF_TERRAIN, &h) && h.y > bottom) e->body.y += h.y - bottom + 0.01f;
}

/* SetBoxColliderTrigger: a trigger touches nothing */
static void ho_trigger(Enemy *e, bool on) { e->body.mask = on ? 0 : CF_SOLID; }

/* FaceObject (her sprite faces left) */
static void ho_face(Enemy *e) {
  if (hero_x() > e->body.x) set_scale_x(e, -1);
  else if (hero_x() < e->body.x) set_scale_x(e, 1);
}

/* (SetScale x: her collider's offset with it) */
static void ho_scale_x(Enemy *e, float sx) {
  set_scale_x(e, sx);
  ho_box(e, ho.box);
}

/* (the Knight's box in one of her ranges: a box in her own units) */
static bool ho_hero_in_box(const Enemy *e, float cx, float cy, float w, float h) {
  const Body *k = &g_hero.body;
  float x = e->body.x + cx * ho_xscale(e), y = e->body.y + cy;
  return !g_hero.hidden && fabsf(k->x + k->ox - x) < w / 2 + k->hx && fabsf(k->y + k->oy - y) < h / 2 + k->hy;
}
static bool ho_run_away_check(const Enemy *e) { return ho_hero_in_box(e, 0, 0.928f, 9.49f, 4.455f); }
static bool ho_a_sphere_range(const Enemy *e) { return ho_hero_in_box(e, 0, 0.7219f, 13.05f, 35.9087f); }
/* (Sphere Range: a circle) */
static bool ho_sphere_range(const Enemy *e) {
  const Body *k = &g_hero.body;
  float cx = e->body.x - 0.0625f * ho_xscale(e), cy = e->body.y - 0.2812f, hx = k->x + k->ox, hy = k->y + k->oy;
  float qx = fminf(fmaxf(cx, hx - k->hx), hx + k->hx) - cx, qy = fminf(fmaxf(cy, hy - k->hy), hy + k->hy) - cy;
  return !g_hero.hidden && qx * qx + qy * qy < 3.43f * 3.43f;
}
/* (Evade Check: the terrain in a box behind her) */
static bool ho_evade_check(const Enemy *e) {
  float cx = e->body.x + 1.2562f * ho_xscale(e);
  for (int i = 0; i < 3; i++)
    if (phys_ray(cx - 1.7562f, e->body.y - 0.5f + 0.5f * (float)i, 1, 0, 3.5124f, CF_TERRAIN, NULL)) return true;
  return false;
}
/* (A Dash Range: a polygon round her, the Knight's place in it) */
static bool ho_a_dash_range(const Enemy *e) {
  static const float p[8][2] = {{0.0271f, 0.2417f}, {25.4355f, 18.4062f}, {26.4515f, -21.5487f}, {7.3552f, -21.3394f},
                                {0.008f, -0.1862f}, {-6.961f, -21.2192f}, {-25.1917f, -21.3371f}, {-24.868f, 19.831f}};
  float pts[16];
  for (int i = 0; i < 8; i++) pts[2 * i] = e->body.x + p[i][0] * ho_xscale(e), pts[2 * i + 1] = e->body.y + p[i][1];
  const Body *k = &g_hero.body;
  float x = k->x + k->ox, y = k->y + k->oy;
  return !g_hero.hidden && box_meets_shape(x - k->hx, y - k->hy, x + k->hx, y + k->hy, pts, 8);
}

/* GetRandomWeightedIndex */
static int ho_weighted(const float *w, int n) {
  float sum = 0, s = 0;
  for (int i = 0; i < n; i++) sum += w[i];
  float r = rand_range(0, sum);
  for (int i = 0; i < n; i++)
    if (r < (s += w[i])) return i;
  return n - 1;
}

/* SendRandomEventV2: none more than its most in a row */
static int ho_random_v2(int c0, const float *w, const uint8_t *max, int n) {
  for (;;) {
    int k = ho_weighted(w, n);
    if (ho.ct[c0 + k] < max[k]) {
      uint8_t v = (uint8_t)(ho.ct[c0 + k] + 1);
      for (int i = 0; i < n; i++) ho.ct[c0 + i] = 0;
      ho.ct[c0 + k] = v;
      return k;
    }
  }
}

/* SendRandomEventV3: as V2, but one missed too long is taken; past its hundredth time, the first as well (it wins) */
static int ho_random_v3(int which, const float *w, const uint8_t *max, const uint8_t *missed, int n) {
  int out = -1;
  while (out < 0) {
    int k = ho_weighted(w, n), forced = -1;
    for (int i = 0; i < n; i++)
      if (ho.ct[HC_MS_AIRDASH + i] >= missed[i]) forced = i;
    if (forced >= 0 || ho.ct[HC_CT_AIRDASH + k] < max[k]) {
      int pick = forced >= 0 ? forced : k;
      uint8_t v = forced >= 0 ? 1 : (uint8_t)(ho.ct[HC_CT_AIRDASH + k] + 1);
      for (int i = 0; i < n; i++) ho.ct[HC_CT_AIRDASH + i] = 0, ho.ct[HC_MS_AIRDASH + i]++;
      ho.ct[HC_CT_AIRDASH + pick] = v, ho.ct[HC_MS_AIRDASH + pick] = 0;
      out = pick;
    }
    if (ho.loops[which] < 255) ho.loops[which]++;
    if (ho.loops[which] > 100) out = 0;
  }
  return out;
}

/* an effect: its clip at a place in her own units, turned and scaled as she is (then left there) */
static void ho_fx(Enemy *e, int clip, float lx, float ly, float sx, float sy) {
  HoFx *f = &ho.fx[0];
  for (int i = 0; i < HO_FX; i++)
    if (!ho.fx[i].anim.playing) f = &ho.fx[i];
  ho_point(e, lx, ly, &f->x, &f->y);
  f->sx = ho_xscale(e) * sx, f->sy = (float)e->sy * sy, f->ang = e->ang;
  anim_play_from_frame(&f->anim, clip, 0);
}

static void ho_idle(Enemy *e);
static void ho_g_sphere(Enemy *e);
static void ho_jump_antic(Enemy *e);

/* Escalation: at 90 HP or less, quicker */
static void ho_escalation(Enemy *e) {
  if (!ho.escalated && e->hp <= 90) {
    ho.escalated = true;
    ho.run_wait_min = 0.35f, ho.run_wait_max = 0.75f, ho.idle_wait_min = 0.1f, ho.idle_wait_max = 0.4f;
  }
  ho_idle(e);
}

/* Evade Antic: not with the terrain behind her */
static void ho_evade_antic(Enemy *e) {
  if (ho_evade_check(e)) {
    ho_g_sphere(e);
    return;
  }
  ho.evade_bool = false;
  e->body.vx = e->body.vy = 0;
  ho_face(e);
  anim_play_from_frame(&e->anim, CLIP_HORNET_EVADE_ANTIC, 0);
  e->st = HO_EVADE_ANTIC;
}

/* Flip? (half the time turned), Run Away? (from the Knight, near), Run Antic */
static void ho_flip(Enemy *e) {
  if (rand_range(0, 1) < 0.5f) ho_scale_x(e, -e->sx);
  if (ho_run_away_check(e)) {
    ho_face(e);
    ho_scale_x(e, -e->sx);
  }
  anim_play_from_frame(&e->anim, CLIP_HORNET_EVADE_ANTIC, 0);
  e->st = HO_RUN_ANTIC;
}

static void ho_idle(Enemy *e) {
  e->st = HO_IDLE;
  ho_face(e);
  ho.air_dash_pause = 999;
  ho_box(e, HB_IDLE);
  anim_play(&e->anim, CLIP_HORNET_IDLE);
  e->body.vx = 0;
  if (ho.evade_bool) {
    ho_evade_antic(e);
    return;
  }
  static const float w[2] = {0.5f, 0.5f};
  static const uint8_t mx[2] = {2, 2};
  if (rand_range(0, 1) < 0.5f || ho_random_v2(HC_CT_IDLE, w, mx, 2) == 1) {
    ho_flip(e);   /* (RUN) */
    return;
  }
  e->wait = rand_range(ho.idle_wait_min, ho.idle_wait_max);
}

static void ho_gdash_antic(Enemy *e) {
  ho_box(e, HB_ANTIC);
  anim_play_from_frame(&e->anim, CLIP_HORNET_G_DASH_ANTIC, 0);
  ho_face(e);
  e->body.vx = 0;
  e->st = HO_GDASH_ANTIC;
}

static void ho_throw_antic(Enemy *e) {
  float a = atan2f(hero_y() - e->body.y, hero_x() - e->body.x) * 180 / (float)M_PI;
  ho.angle = a < 0 ? a + 360 : a;
  ho.stun_st = HSC_STOP;   /* (STUN CONTROL STOP) */
  ho_face(e);
  ho_box(e, HB_THROW);
  e->body.vx = 0;
  anim_play_from_frame(&e->anim, CLIP_HORNET_THROW_ANTIC, 0);
  e->st = HO_THROW_ANTIC;
}

/* Move Choice A (she may throw) or B: an air dash, a sphere in the air, a ground dash, a throw */
static void ho_move_choice(Enemy *e, bool can_throw) {
  static const float wa[4] = {0.25f, 0.25f, 0.25f, 0.25f}, wb[3] = {0.33f, 0.33f, 0.34f};
  static const uint8_t mx[4] = {2, 1, 2, 1}, ms[4] = {5, 7, 5, 3};
  int k = can_throw ? ho_random_v3(0, wa, mx, ms, 4) : ho_random_v3(1, wb, mx, ms, 3);
  if (k == 0) ho.air_dash_pause = rand_range(0.15f, 0.4f), ho.will_sphere = false, ho_jump_antic(e);   /* (Set ADash) */
  else if (k == 1) ho.will_sphere = true, ho.air_dash_pause = 999, ho_jump_antic(e);                 /* (Set Sphere A) */
  else if (k == 2) ho_gdash_antic(e);
  else ho_throw_antic(e);
}

/* G Sphere? (the Knight near: a sphere on the ground, a fifth of the time), Can Throw? */
static void ho_g_sphere(Enemy *e) {
  static const float w[2] = {0.2f, 0.8f};
  static const uint8_t mx[2] = {1, 5};
  if (ho_sphere_range(e) && ho_random_v2(HC_CT_GSPHERE, w, mx, 2) == 0) {
    e->body.vx = e->body.vy = 0;
    ho_box(e, HB_IDLE);
    anim_play_from_frame(&e->anim, CLIP_HORNET_SPHERE_ANTIC_G, 0);
    ho_face(e);
    e->st = HO_SPHERE_ANTIC_G;
    return;
  }
  bool right = hero_x() > e->body.x;
  ho_move_choice(e, (e->body.x > HO_THROW_X_R && !right) || (e->body.x < HO_THROW_X_L && right));
}

static void ho_jump_antic(Enemy *e) {
  ho_box(e, HB_IDLE);
  e->body.vx = e->body.vy = 0;
  anim_play_from_frame(&e->anim, CLIP_HORNET_JUMP_ANTIC, 0);
  ho_face(e);
  e->st = HO_JUMP_ANTIC;
}

/* Set Jump Only */
static void ho_jump_only(Enemy *e) {
  ho.air_dash_pause = 999, ho.will_sphere = false;
  ho_jump_antic(e);
}

static void ho_in_air(Enemy *e) {
  e->st = HO_IN_AIR;
  e->wait = ho.air_dash_pause;
}

/* Land, Hard Land: her collider, gravity and turn as they were */
static void ho_grounded(Enemy *e) {
  ho_trigger(e, false);
  e->body.gravity_scale = HO_GRAVITY;
  e->ang = 0, e->sy = 1;
  ho_box(e, HB_IDLE);
}

/* Dmg Response: an evade, a jump, an attack, or a moment still */
static void ho_dmg_response(Enemy *e) {
  float r = rand_range(0, 1);
  if (r < 0.3f) ho_evade_antic(e);
  else if (r < 0.45f) ho_jump_only(e);
  else if (r < 0.6f) ho_g_sphere(e);
  else e->st = HO_DMG_IDLE, e->wait = rand_range(0.25f, 0.4f);
}

/* (the needle gone, its tink off; her attacks off) */
static void ho_needle_off(void) { ho.needle_st = 0, ho.thread_on = false, ho.tink = false; }

/* STUN (from her Stun Control): Stun Start */
static void ho_stun(Enemy *e) {
  ho_box(e, HB_IDLE);
  ho.ball_on = false;
  ho_trigger(e, false);
  e->body.gravity_scale = HO_GRAVITY;
  e->ang = 0, e->sy = 1;
  ho_face(e);
  anim_play_from_frame(&e->anim, CLIP_HORNET_STUN_AIR, 0);
  e->body.vx = 10 * ho_xscale(e), e->body.vy = 20;
  ho_needle_off();
  ho.hits_on = 0;
  e->st = HO_STUN_START;
}

/* HealthManager: TOOK DAMAGE (to her Control), then STUN DAMAGE (to her Stun Control) */
static void hornet_hit(Enemy *e) {
  if (e->st == HO_IDLE || e->st == HO_RUN) ho_dmg_response(e);
  else if (e->st == HO_STUN_LAND) ho_jump_only(e);   /* (Stun Recover) */
  bool stun = false;
  if (ho.stun_st == HSC_STOP) ho.hits++;   /* (Unstun Increment) */
  else if (ho.hits >= ho.stun_hit_max) stun = true;   /* (Max Check, Continue Combo) */
  else {
    /* In Combo: its two seconds again */
    ho.combo++, ho.hits++, ho.combo_t = 2, ho.stun_st = HSC_COMBO;
    stun = ho.combo == ho.stun_combo;
  }
  if (stun) {
    ho.combo = ho.hits = 0, ho.stun_st = HSC_IDLE;
    ho_stun(e);
  }
}

static void hornet_wake(Enemy *e) {
  /* Wake: solid, shown; Music; Flourish (and her title) */
  ho.kinematic = false;
  e->flags &= (uint8_t)~(1 | 32);
  ho_box(e, HB_IDLE);
  title_show(TITLE_HORNET, TF_VISITED);
  anim_play_from_frame(&e->anim, CLIP_HORNET_FLOURISH, 0);
  e->st = HO_FLOURISH;
}

static Enemy *hornet(void) {
  for (int i = 0; i < MAX_ENEMIES; i++)
    if (en[i].mode == EM_ALIVE && FSM(&en[i]) == EF_HORNET) return &en[i];
  return NULL;
}

/* WAKE (from her encounter): Set Scale, Wake */
void enemies_hornet_wake(void) {
  Enemy *e = hornet();
  if (e && e->st == HO_INERT) {
    ho_scale_x(e, -1);
    hornet_wake(e);
  }
}

static void hornet_start(Enemy *e) {
  memset(&ho, 0, sizeof ho);
  ho.run_wait_min = 0.5f, ho.run_wait_max = 1, ho.idle_wait_min = 0.5f, ho.idle_wait_max = 0.75f;
  /* (Stun Control's Heavy Blow: with Heavy Blow worn, one less of each) */
  ho.stun_hit_max = 10, ho.stun_combo = 6;
  ho.evade_t = rand_range(2, 3);
  ho.box = HB_THROWING;
  e->body.gravity_scale = HO_GRAVITY;
  e->hb_on = 0;
  /* Inert: kinematic, unseen, untouched; with hornetGreenpath 4 or more, Refight Ready */
  ho.kinematic = true;
  e->flags |= 1 | 32;
  e->st = HO_INERT;
  if (g_pd.hornet_greenpath >= 4) {
    ho.kinematic = false;
    e->flags &= (uint8_t)~(1 | 32);
    anim_play(&e->anim, CLIP_HORNET_IDLE);
    ho_scale_x(e, -1);
    e->st = HO_REFIGHT_READY;
  }
#ifdef HOST
  if (getenv("HKHORNET")) ho_scale_x(e, -1), hornet_wake(e), gates_event(BG_CLOSE);   /* (tests: awake at once) */
#endif
}

static void ho_needle_tick(Enemy *e) {
  if (!ho.needle_st) return;
  ho.needle.events = 0;
  anim_update(&ho.needle, DT);
  ho.nt += DT;
  if (ho.needle_st == 1) {
    /* Out: a while, turned to its way */
    ho.nang = atan2f(ho.nvy, ho.nvx) * 180 / (float)M_PI + 180;
    if (ho.nt >= 0.3f) ho.needle_st = 2;
  } else if (ho.needle_st == 2) {
    /* Decel: till nearly still; Return: its thread out */
    if (sqrtf(ho.nvx * ho.nvx + ho.nvy * ho.nvy) <= 0.5f) {
      ho.needle_st = 3, ho.nt = 0, ho.nrx = ho.nx, ho.nry = ho.ny;
      anim_play_from_frame(&ho.thread, CLIP_HORNET_NEEDLE_THREAD, 0), ho.thread_on = true;
    }
  } else {
    /* Return: back where it was thrown from, 30 a second, easeInSine */
    float dx = ho.n0x - ho.nrx, dy = ho.n0y - ho.nry, time = sqrtf(dx * dx + dy * dy) / 30;
    float k = time > 0 ? ho.nt / time : 1;
    if (k >= 1) {
      ho.nx = ho.n0x, ho.ny = ho.n0y;
      /* Notify: NEEDLE RETURN; Throw Recover */
      ho_needle_off();
      if (e->st == HO_THROWN) {
        anim_play_from_frame(&e->anim, CLIP_HORNET_THROW_RECOVER, 0);
        e->rc_base = 15;
        if (ho.stun_st == HSC_STOP) ho.stun_st = HSC_IDLE, ho.combo = 0;   /* (STUN CONTROL START: Reset Counter) */
        e->st = HO_THROW_REC;
      }
      return;
    }
    float f = 1 - cosf(k * (float)M_PI / 2);
    ho.nx = ho.nrx + dx * f, ho.ny = ho.nry + dy * f;
  }
  if (ho.thread_on) {
    ho.thread.events = 0;
    anim_update(&ho.thread, DT);
    if (ho.thread.events & ANIM_DONE) ho.thread_on = false;   /* (DeactivateAfter2dtkAnimation) */
  }
}

static void hornet_fixed(Enemy *e) {
  switch (e->st) {
    case HO_GDASH_REC1: e->body.vx *= 0.77f; break;
    case HO_GDASH_REC2: e->body.vx *= 0.75f; break;
    case HO_HARD_LAND: e->body.vx *= 0.8f; break;
    case HO_ADASH_ANTIC: e->body.vx = e->body.vy = 0; break;
    case HO_SPHERE_ANTIC_A: case HO_SPHERE_A: e->body.vx *= 0.78f, e->body.vy *= 0.78f; break;
  }
  if (fabsf(e->body.vx) < 0.001f) e->body.vx = 0;
  if (ho.needle_st == 1 || ho.needle_st == 2) {
    ho.nx += ho.nvx * DT, ho.ny += ho.nvy * DT;
    if (ho.needle_st == 2) ho.nvx *= 0.8f, ho.nvy *= 0.8f;
  }
}

/* CheckCollisionSide: touching the terrain at her side, or under her */
static bool ho_wall_side(const Enemy *e) {
  for (int c = 0; c < e->body.ncontacts; c++)
    if (fabsf(e->body.cnx[c]) > 0.5f) return true;
  return false;
}
static bool ho_on_ground(const Enemy *e) {
  for (int c = 0; c < e->body.ncontacts; c++)
    if (e->body.cny[c] > 0.5f) return true;
  return false;
}

static void hornet_update(Enemy *e) {
  bool done = (e->anim.events & ANIM_DONE) != 0;
  ho_needle_tick(e);
  for (int i = 0; i < HO_FX; i++) {
    ho.fx[i].anim.events = 0;
    if (ho.fx[i].anim.playing) anim_update(&ho.fx[i].anim, DT);
  }
  /* her Evade Range: its Fluctuate (off 2-3 s, on 1-2 s); ENTER and EXIT set her bool */
  if ((ho.evade_t -= DT) <= 0) {
    ho.evade_on = !ho.evade_on;
    ho.evade_t = ho.evade_on ? rand_range(1, 2) : rand_range(2, 3);
  }
  bool in = ho.evade_on && !(e->flags & 1) && ho_hero_in_box(e, 0, 1.8175f, 7.6201f, 6.2341f);
  if (in != ho.evade_in) ho.evade_bool = in, ho.evade_in = in;
  /* Stun Control: In Combo's two seconds */
  if (ho.stun_st == HSC_COMBO && (ho.combo_t -= DT) <= 0) ho.stun_st = HSC_IDLE, ho.combo = 0;
  if (ho.ball_on) {
    ho.ball.events = 0;
    anim_update(&ho.ball, DT);
    ho.ball_t += DT;
  }
  if (ho.tink_t > 0) ho.tink_t -= DT;
  switch (e->st) {
    case HO_REFIGHT_READY:
      if (ho_hero_in_box(e, -1.0777f, 0.081f, 43.8482f, 3.6616f)) {
        /* Refight Wake: the gates closed, her saver's walls up */
        gates_event(BG_CLOSE);
        enemies_hornet_saver(true);
        hornet_wake(e);
      }
      break;
    case HO_FLOURISH:
      if (done) ho_idle(e);
      break;
    case HO_IDLE:
      if ((e->wait -= DT) <= 0) ho_g_sphere(e);
      break;
    case HO_DMG_IDLE:
      if ((e->wait -= DT) <= 0 || ho_wall_side(e)) ho_g_sphere(e);
      break;
    case HO_RUN_ANTIC:
      if (done) {
        /* Run: away, a while */
        if (ho.evade_bool) {
          ho_evade_antic(e);
          break;
        }
        e->body.vx = -8 * ho_xscale(e);
        anim_play(&e->anim, CLIP_HORNET_RUN);
        e->wait = rand_range(ho.run_wait_min, ho.run_wait_max);
        e->st = HO_RUN;
      }
      break;
    case HO_RUN:
      if ((e->wait -= DT) <= 0 || ho_wall_side(e)) ho_g_sphere(e);
      break;
    case HO_GDASH_ANTIC:
      if (done) {
        /* G Dash: along the ground, hurting */
        anim_play(&e->anim, CLIP_HORNET_G_DASH);
        ho_fx(e, CLIP_HORNET_G_DASH_EFFECT, 6.71f, 1, 1, 1);
        cam_shake(SHAKE_ENEMY_KILL);
        ho_box(e, HB_GDASH);
        ho.hits_on = 1;
        e->body.vx = -25 * ho_xscale(e);
        e->wait = 0.35f;
        e->st = HO_GDASH;
      }
      break;
    case HO_GDASH:
      if ((e->wait -= DT) <= 0 || ho_wall_side(e)) {
        anim_play_from_frame(&e->anim, CLIP_HORNET_G_DASH_RECOVER1, 0);
        ho_box(e, HB_ANTIC);
        ho.hits_on = 0;
        e->st = HO_GDASH_REC1;
      }
      break;
    case HO_GDASH_REC1:
      if (done) {
        anim_play_from_frame(&e->anim, CLIP_HORNET_G_DASH_RECOVER2, 0);
        ho_box(e, HB_IDLE);
        e->st = HO_GDASH_REC2;
      }
      break;
    case HO_GDASH_REC2:
    case HO_LAND:
    case HO_HARD_LAND:
    case HO_SPHERE_REC_G:
    case HO_THROW_REC:
      if (done) ho_escalation(e);
      break;
    case HO_JUMP_ANTIC:
      if (done) {
        /* Aim Jump: somewhere across the arena, not near; Jump */
        float jx;
        do jx = rand_range(HO_LEFT_X, HO_RIGHT_X) - e->body.x;
        while (jx >= -2.5f && jx <= 2.5f);
        anim_play(&e->anim, CLIP_HORNET_JUMP);
        e->body.vx = jx, e->body.vy = 41;
        e->st = HO_JUMP;
      }
      break;
    case HO_JUMP:
      ho_in_air(e);
      break;
    case HO_IN_AIR: {
      if (ho_on_ground(e)) {
        anim_play_from_frame(&e->anim, CLIP_HORNET_LAND, 0);
        e->body.vx = e->body.vy = 0;
        ho_grounded(e);
        e->st = HO_LAND;
        break;
      }
      bool airdash = (e->wait -= DT) <= 0;
      if (e->body.vy < 0 && e->body.y < HO_SPHERE_Y && ho.will_sphere) {
        /* Do Sphere?: the Knight near enough */
        ho.will_sphere = false;
        if (ho_a_sphere_range(e)) {
          anim_play_from_frame(&e->anim, CLIP_HORNET_SPHERE_ANTIC_A, 0);
          e->body.gravity_scale = 0;
          ho_face(e);
          e->st = HO_SPHERE_ANTIC_A;
        } else
          ho_in_air(e);
      } else if (airdash) {
        /* ADash Antic: only with the Knight in her A Dash Range */
        if (!ho_a_dash_range(e)) {
          ho_in_air(e);
          break;
        }
        float a = atan2f(hero_y() - 0.5f - e->body.y, hero_x() - e->body.x) * 180 / (float)M_PI;
        ho.angle = a < 0 ? a + 360 : a;
        anim_play_from_frame(&e->anim, CLIP_HORNET_A_DASH_ANTIC, 0);
        ho_face(e);
        e->body.vx = e->body.vy = 0, e->body.gravity_scale = 0;
        e->st = HO_ADASH_ANTIC;
      }
      break;
    }
    case HO_ADASH_ANTIC:
      if (done) {
        /* Fire: at the Knight, a trigger, turned to her way (Firing R: upside down, going right) */
        ho_trigger(e, true);
        float a = ho.angle * (float)M_PI / 180;
        e->body.vx = cosf(a) * 30, e->body.vy = sinf(a) * 30;
        set_scale_x(e, 1);
        e->sy = 1;
        e->ang = fmodf(ho.angle + 180, 360);
        if (e->ang >= 90 && e->ang <= 270) e->sy = -1, ho.return_x_scale = -1;
        else ho.return_x_scale = 1;
        ho_box(e, HB_ADASH);
        ho.hits_on = 2;
        /* A Dash */
        ho_fx(e, CLIP_HORNET_AIR_DASH_EFFECT, 2.99f, 0.25f, 1, 1);
        cam_shake(SHAKE_ENEMY_KILL);
        ho.air_dash_pause = 999;
        anim_play(&e->anim, CLIP_HORNET_A_DASH);
        e->st = HO_ADASH;
      }
      break;
    case HO_ADASH: {
      int ev = 0;   /* (the last of the frame: LAND, ROOF, WALL L, WALL R) */
      if (e->body.y <= HO_FLOOR_Y) ev = 1;
      if (e->body.y >= HO_ROOF_Y) ev = 2;
      if (e->body.x <= HO_WALL_L) ev = 3;
      if (e->body.x >= HO_WALL_R) ev = 4;
      if (!ev) break;
      ho.hits_on = 0;
      if (ev == 1) {
        /* Land Y, Hard Land */
        e->body.y = HO_FLOOR_Y;
        set_scale_x(e, ho.return_x_scale);
        anim_play_from_frame(&e->anim, CLIP_HORNET_HARD_LAND, 0);
        e->body.vy = 0;
        ho_grounded(e);
        e->st = HO_HARD_LAND;
      } else if (ev == 2) {
        /* Hit Roof: down from there */
        set_scale_x(e, ho.return_x_scale);
        e->sy = 1, e->ang = 0;
        e->body.y = HO_ROOF_Y;
        e->body.vx = e->body.vy = 0;
        ho_trigger(e, false);
        e->body.gravity_scale = 2;
        ho_box(e, HB_IDLE);
        ho_in_air(e);
      } else {
        /* Wall L / R: against it a moment */
        bool left = ev == 3;
        e->ang = 0, e->sy = 1;
        e->body.vx = e->body.vy = 0;
        set_scale_x(e, left ? 1 : -1);
        e->body.x = left ? HO_WALL_L : HO_WALL_R;
        ho_trigger(e, false);
        ho_box(e, HB_IDLE);
        ho_unsink(e, left);
        anim_play_from_frame(&e->anim, CLIP_HORNET_WALL_IMPACT, 0);
        e->c0 = left;
        e->st = HO_WALL;
      }
      break;
    }
    case HO_WALL:
      if (done) {
        /* Jump R / L: off it */
        bool left = e->c0;
        e->body.vx = left ? 10 : -10, e->body.vy = 20;
        anim_play(&e->anim, CLIP_HORNET_JUMP);
        e->body.gravity_scale = 2;
        ho_scale_x(e, left ? -1 : 1);
        ho_in_air(e);
      }
      break;
    case HO_SPHERE_ANTIC_G:
    case HO_SPHERE_ANTIC_A:
      if (done) {
        /* Sphere: her ball out, a flash, a shake; a second */
        ho.ball_on = true, ho.ball_t = 0;
        if (!ho.ball_started) anim_play_from_frame(&ho.ball, CLIP_HORNET_SPHERE_BALL, 0), ho.ball_started = true;
        ho_fx(e, CLIP_HORNET_FLASH, 0, 0, 1.3849f, 1.05f);
        cam_shake(SHAKE_ENEMY_KILL);
        anim_play(&e->anim, CLIP_HORNET_SPHERE_ATTACK);
        e->wait = 1;
        e->st = e->st == HO_SPHERE_ANTIC_G ? HO_SPHERE_G : HO_SPHERE_A;
      }
      break;
    case HO_SPHERE_G:
    case HO_SPHERE_A:
      if ((e->wait -= DT) <= 0) {
        bool g = e->st == HO_SPHERE_G;
        anim_play_from_frame(&e->anim, g ? CLIP_HORNET_SPHERE_RECOVER_G : CLIP_HORNET_SPHERE_RECOVER_A, 0);
        ho.ball_on = false;
        e->st = g ? HO_SPHERE_REC_G : HO_SPHERE_REC_A;
      }
      break;
    case HO_SPHERE_REC_A:
      if (done) {
        /* Sphere A End: falling */
        anim_play(&e->anim, CLIP_HORNET_FALL);
        e->body.gravity_scale = HO_GRAVITY;
        ho_in_air(e);
      }
      break;
    case HO_THROW_ANTIC:
      if (done) {
        /* Lock?: straight across, her way; Throw: her needle out of her hand */
        ho.angle = ho.angle <= 90 ? 0 : ho.angle <= 270 ? 180 : 0;
        ho_fx(e, CLIP_HORNET_THROW_EFFECT, 1.34f, -0.05f, 1, 1);
        ho_box(e, HB_THROWING);
        ho.nx = ho.n0x = e->body.x, ho.ny = ho.n0y = e->body.y - 0.5f;
        float a = ho.angle * (float)M_PI / 180;
        ho.nvx = cosf(a) * 38, ho.nvy = sinf(a) * 38;
        ho.nang = ho.angle + 180;
        ho.needle_st = 1, ho.nt = 0;
        anim_play_from_frame(&ho.needle, CLIP_HORNET_NEEDLE, 0);
        anim_play(&e->anim, CLIP_HORNET_THROW);
        e->rc_base = 0;
        ho.tink = true;
        e->st = HO_THROW;
      }
      break;
    case HO_THROW:
      e->st = HO_THROWN;
      break;
    case HO_EVADE_ANTIC:
      if (done) {
        anim_play(&e->anim, CLIP_HORNET_EVADE);
        e->body.vx = 22 * ho_xscale(e);
        e->wait = 0.25f;
        e->st = HO_EVADE;
      }
      break;
    case HO_EVADE:
      if ((e->wait -= DT) <= 0 || ho_wall_side(e)) {
        anim_play_from_frame(&e->anim, CLIP_HORNET_LAND, 0);
        e->body.vx = e->body.vy = 0;
        e->st = HO_EVADE_LAND;
      }
      break;
    case HO_EVADE_LAND:
      if (done) {
        /* After Evade: an attack, or idle */
        if (rand_range(0, 1) < 0.5f) ho_g_sphere(e);
        else ho_escalation(e);
      }
      break;
    case HO_STUN_START:
      e->st = HO_STUN_AIR;
      break;
    case HO_STUN_AIR:
      if (ho_on_ground(e)) {
        anim_play(&e->anim, CLIP_HORNET_STUN);
        e->body.vx = e->body.vy = 0;
        e->wait = 3;
        e->st = HO_STUN_LAND;
      }
      break;
    case HO_STUN_LAND:
      if ((e->wait -= DT) <= 0) ho_jump_only(e);   /* (Stun Recover) */
      break;
  }
}

/* her attacks the Knight's box meets (Hit GDash, Hit ADash, the Needle, the Sphere Ball) */
static bool hornet_touch(const Enemy *e, float x0, float y0, float x1, float y1) {
  float pts[6];
  if (ho.hits_on) {
    float oy = ho.hits_on == 2 ? 0.51f : 0;
    for (int j = 0; j < 3; j++) ho_point(e, ho_hit_pts[j][0], ho_hit_pts[j][1] + oy, &pts[2 * j], &pts[2 * j + 1]);
    if (box_meets_shape(x0, y0, x1, y1, pts, 3)) return true;
  }
  if (ho.needle_st) {
    float a = ho.nang * (float)M_PI / 180, c = cosf(a), s = sinf(a);
    for (int j = 0; j < 3; j++)
      pts[2 * j] = ho.nx + ho_needle_pts[j][0] * c - ho_needle_pts[j][1] * s,
      pts[2 * j + 1] = ho.ny + ho_needle_pts[j][0] * s + ho_needle_pts[j][1] * c;
    if (box_meets_shape(x0, y0, x1, y1, pts, 3)) return true;
  }
  if (ho.ball_on) {
    /* (Grow: from 0.8 to 1.5 in a quarter second, easeOutSine) */
    float k = ho.ball_t >= 0.25f ? 1 : sinf(ho.ball_t / 0.25f * (float)M_PI / 2), sc = 0.8f + 0.7f * k;
    float cx = e->body.x - 0.0625f * sc * ho_xscale(e), cy = e->body.y - 0.2812f * sc, r = 2.53f * sc;
    float qx = fminf(fmaxf(cx, x0), x1) - cx, qy = fminf(fmaxf(cy, y0), y1) - cy;
    if (qx * qx + qy * qy < r * r) return true;
  }
  return false;
}

/* her Needle Tink: the nail meets it, the Knight thrown back */
static int hornet_nail(const float *pts, int npts, float direction) {
  if (!ho.tink || ho.tink_t > 0) return 0;
  float a = ho.nang * (float)M_PI / 180, c = cosf(a), s = sinf(a), q[8];
  static const float b[4][2] = {{-1.6935f, -0.1256f}, {1.5531f, -0.1256f}, {1.5531f, 0.1256f}, {-1.6935f, 0.1256f}};
  for (int j = 0; j < 4; j++) q[2 * j] = ho.nx + b[j][0] * c - b[j][1] * s, q[2 * j + 1] = ho.ny + b[j][0] * s + b[j][1] * c;
  float x0 = q[0], y0 = q[1], x1 = q[0], y1 = q[1];
  for (int j = 1; j < 4; j++) x0 = fminf(x0, q[2 * j]), x1 = fmaxf(x1, q[2 * j]), y0 = fminf(y0, q[2 * j + 1]), y1 = fmaxf(y1, q[2 * j + 1]);
  if (!box_meets_shape(x0, y0, x1, y1, pts, npts)) return 0;
  ho.tink_t = 0.25f;
  cam_shake(SHAKE_ENEMY_KILL);
  switch (cardinal(direction)) {
    case 0: hero_recoil_left(); break;
    case 1: hero_recoil_down(); break;
    case 2: hero_recoil_right(); break;
  }
  return HB_RECOIL;
}

/* ---------------------------------------------------------------- her corpse (Corpse Hornet 1) */
static void hornet_corpse_start(Enemy *e) {
  const Ent *c = enemy_rec(e, ET_CORPSE);
  e->mode = EM_CORPSE;
  ho_needle_off();
  ho.ball_on = false, ho.hits_on = 0;
  for (int i = 0; i < HO_FX; i++) ho.fx[i].anim.playing = false;
  e->ang = 0, e->sy = 1;
  if (c) e->body.ox = c->x0, e->body.oy = c->y0, e->body.hx = c->x1, e->body.hy = c->y1, e->body.gravity_scale = c->p0;
  e->body.mask = CF_SOLID, e->body.friction = 0.2f, e->body.ncontacts = 0;
  /* Limit Pos; Set PD; Blow */
  e->body.y = fmaxf(e->body.y, 28.75f), e->body.x = fminf(fmaxf(e->body.x, 16), 37);
  pd_set_flag(PDF_HORNET1_DEFEATED, true);
  game_freeze(0.01f, 0.35f, 0.1f, 0, false);
  cam_shake(SHAKE_AVERAGE);
  /* Launch: up, a little toward the Knight */
  ho_face(e);
  e->body.vx = ho_xscale(e) * 5, e->body.vy = 20;
  anim_play(&e->anim, CLIP_HORNET_DEATH_AIR);
  ho.corpse_shown = true;
  e->st = HC_LAUNCH;
}

/* (iTweenMoveBy: easeOutCubic, or linear) */
static void hc_tween(Enemy *e, float dx, float dy, float time, uint8_t ease) {
  ho.tw_t = 0, ho.tw_x0 = e->body.x, ho.tw_y0 = e->body.y, ho.tw_dx = dx, ho.tw_dy = dy, ho.tw_time = time;
  ho.tw_ease = ease;
}

static void hornet_corpse_fixed(Enemy *e) {
  if (e->st <= HC_LAND) {
    if (e->st != HC_LAND || e->body.vx || e->body.vy) body_step(&e->body, DT);
    return;
  }
  if (ho.tw_time > 0) {
    ho.tw_t += DT;
    float k = ho.tw_t >= ho.tw_time ? 1 : ho.tw_t / ho.tw_time;
    if (ho.tw_ease) k = 1 - (1 - k) * (1 - k) * (1 - k);
    e->body.x = ho.tw_x0 + ho.tw_dx * k, e->body.y = ho.tw_y0 + ho.tw_dy * k;
    if (ho.tw_t >= ho.tw_time) ho.tw_time = 0;
  }
}

static void hornet_corpse_update(Enemy *e) {
  if (ho.leave_on) {
    ho.leave.events = 0;
    anim_update(&ho.leave, DT);
  }
  bool tdone = false;
  if (ho.cthread_on) {
    ho.cthread.events = 0;
    anim_update(&ho.cthread, DT);
    tdone = (ho.cthread.events & ANIM_DONE) != 0;
  }
  switch (e->st) {
    case HC_LAUNCH:
      e->st = HC_IN_AIR;
      break;
    case HC_IN_AIR:
      if (ho_on_ground(e)) {
        /* Land: wounded, a while */
        anim_play(&e->anim, CLIP_HORNET_WOUNDED);
        e->body.vx = e->body.vy = 0;
        e->wait = 3;
        e->st = HC_LAND;
      }
      break;
    case HC_LAND:
      if ((e->wait -= DT) <= 0) {
        /* Check Pos: away from the nearer side; Jump: her cutscene self leaps */
        bool l = e->body.x < 27.3f;
        set_scale_x(e, l ? -1 : 1);
        ho.leave_dx = l ? 8 : -8, ho.rot = l ? 45 : -45;
        ho.corpse_shown = false;
        ho.leave_on = true;
        anim_play_from_frame(&ho.leave, CLIP_HORNETCS_JUMP_FULL, 0);
        e->body.vx = e->body.vy = 0, e->body.gravity_scale = 0, e->body.mask = 0;
        hc_tween(e, 0, 6, 0.7f, 1);
        e->wait = 0.415f;
        e->st = HC_JUMP;
      }
      break;
    case HC_JUMP:
      if ((e->wait -= DT) <= 0) {
        anim_play_from_frame(&ho.leave, CLIP_HORNETCS_THROW_SIDE_START, 0);
        e->st = HC_THROW_START;
      }
      break;
    case HC_THROW_START:
      if (ho.leave.events & ANIM_DONE) {
        /* Throw: her thread out */
        ho.cthread_on = true;
        anim_play_from_frame(&ho.cthread, CLIP_HORNETCS_THREAD_1, 0);
        anim_play_from_frame(&ho.leave, CLIP_HORNETCS_THROW_SIDE, 0);
        e->st = HC_THROW;
      }
      break;
    case HC_THROW:
      if (tdone) {
        /* Yank: she turns (her thread not: it is let go of then), pulled to it */
        anim_play_from_frame(&ho.leave, CLIP_HORNETCS_HARPOON_SIDE, 0);
        e->ang = ho.rot;
        hc_tween(e, ho.leave_dx, 8, 0.15f, 0);
        e->st = HC_YANK;
      }
      break;
    case HC_YANK:
      if (ho.tw_time <= 0) {
        /* End: gone; HORNET LEAVE */
        cam_shake(SHAKE_ENEMY_KILL);
        ho.leave_on = false;
        vm_broadcast(VMEV_HORNET_LEAVE);
        e->st = HC_END;
      }
      break;
  }
}

static void hornet_draw(const Enemy *e, int i, float z) {
  Inst in;
  uint8_t tint = flash_tint(e, 2 + i % 5);
  if (e->mode == EM_ALIVE || ho.corpse_shown) {
    sprite_inst_rot(e->anim.sprite, e->body.x, e->body.y, z, e->sx, (float)e->sy, e->ang, tint, &in);
    gfx_actor(&in, SORT_KEY(0, 0));
  }
  if (e->mode == EM_ALIVE) {
    if (ho.ball_on && ho.ball.sprite >= 0) {
      float k = ho.ball_t >= 0.25f ? 1 : sinf(ho.ball_t / 0.25f * (float)M_PI / 2), sc = 0.8f + 0.7f * k;
      sprite_inst(ho.ball.sprite, e->body.x, e->body.y, z - 0.001f, sc * ho_xscale(e), sc, 0, &in);
      gfx_actor(&in, SORT_KEY(0, 0));
    }
    for (int k = 0; k < HO_FX; k++) {
      const HoFx *f = &ho.fx[k];
      if (!f->anim.playing || f->anim.sprite < 0) continue;
      sprite_inst_rot(f->anim.sprite, f->x, f->y, z - 0.002f, f->sx, f->sy, f->ang, 0, &in);
      gfx_actor(&in, SORT_KEY(0, 0));
    }
  }
  if (ho.needle_st && ho.needle.sprite >= 0) {
    sprite_inst_rot(ho.needle.sprite, ho.nx, ho.ny, z - 0.001f, 1, 1, ho.nang, 0, &in);
    gfx_actor(&in, SORT_KEY(0, 0));
    if (ho.thread_on && ho.thread.sprite >= 0) {
      float a = ho.nang * (float)M_PI / 180;
      sprite_inst_rot(ho.thread.sprite, ho.nx + 5.2f * cosf(a), ho.ny + 5.2f * sinf(a), z - 0.001f, 1, 1, ho.nang, 0, &in);
      gfx_actor(&in, SORT_KEY(0, 0));
    }
  }
  if (e->mode == EM_CORPSE) {
    if (ho.leave_on && ho.leave.sprite >= 0) {
      sprite_inst_rot(ho.leave.sprite, e->body.x, e->body.y, z, e->sx, 1, e->ang, 0, &in);
      gfx_actor(&in, SORT_KEY(0, 0));
    }
    if (ho.cthread_on && ho.cthread.sprite >= 0) {
      /* (her child, turned 45 degrees its way; not turned with her as she yanks) */
      float k = ho_xscale(e);
      sprite_inst_rot(ho.cthread.sprite, e->body.x - 6.37f * k, e->body.y + 5.44f, z + 0.001f, 2.3241f * k, 1, -45 * k, 0, &in);
      gfx_actor(&in, SORT_KEY(0, 0));
    }
  }
}

/* the collisions a step brought (Collision2dEvent, ObjectBounce): the kinds that want them */
static BodyEvent body_events[MAX_EVENTS];
static bool wants_contacts(const Enemy *e) { return FSM(e) == EF_MOSQUITO || FSM(e) == EF_FATFLY || FSM(e) == EF_MOSSCHARGER; }

static void enemy_contacts(Enemy *e, float pvx, float pvy, int had) {
  for (int i = 0; i < e->body.nevents; i++) {
    const BodyEvent *v = &body_events[i];
    if (FSM(e) == EF_MOSQUITO && v->kind == EV_ENTER) e->b1 = true;   /* (HIT WALL) */
    if (FSM(e) == EF_FATFLY && v->kind == EV_STAY && !e->b1) e->b1 = true, e->ax = v->nx, e->ay = v->ny;
    if (FSM(e) == EF_MOSSCHARGER && v->kind == EV_ENTER && e->pause1 > 1) {
      body_bounce(&e->body, pvx, pvy, had, 0.5f);   /* (ObjectBounce, at its speed as last sampled) */
      e->b1 = true;
    }
  }
}

/* a Squit's TileDetector (a second box, there till it lunges): one box round both, for the terrain */
static void mosquito_box(Enemy *e, float *save) {
  const Ent *t = enemy_rec(e, ET_VARS);
  save[0] = e->body.ox, save[1] = e->body.oy, save[2] = e->body.hx, save[3] = e->body.hy;
  if (!t || e->b0) return;
  float tx, ty;
  enemy_point(e, t->x0, t->y0, &tx, &ty);
  float x0 = fminf(e->body.ox - e->body.hx, tx - t->x1), x1 = fmaxf(e->body.ox + e->body.hx, tx + t->x1);
  float y0 = fminf(e->body.oy - e->body.hy, ty - t->y1), y1 = fmaxf(e->body.oy + e->body.hy, ty + t->y1);
  e->body.ox = (x0 + x1) / 2, e->body.oy = (y0 + y1) / 2, e->body.hx = (x1 - x0) / 2, e->body.hy = (y1 - y0) / 2;
}

/* its FSMs start */
static void enemy_fsm_start(Enemy *e, const Ent *d) {
  if (FSM(e) == EF_CRAWLER) crawler_start(e, d);
  else if (FSM(e) == EF_BUZZER) buzzer_start(e, d);
  else if (FSM(e) == EF_HUSK) husk_start(e);
  else if (FSM(e) == EF_CLIMBER) climber_start(e);
  else if (FSM(e) == EF_BOUNCER) bouncer_start(e);
  else if (FSM(e) == EF_SPITTER) spitter_start(e, d);
  else if (FSM(e) == EF_ROLLER) roller_start(e);
  else if (FSM(e) == EF_BLOCKER) blocker_start(e);
  else if (FSM(e) == EF_LEAPER) leaper_start(e);
  else if (FSM(e) == EF_GUARD) guard_start(e);
  else if (FSM(e) == EF_FK) fk_start(e);
  else if (FSM(e) == EF_FKHEAD) fk_head_start(e);
  else if (FSM(e) == EF_GFLY) gfly_start(e);
  else if (FSM(e) == EF_HATCHER) hatcher_start(e, d);
  else if (FSM(e) == EF_HATCHLING) hatchling_start(e);
  else if (FSM(e) == EF_SLUG) slug_start(e);
  else if (FSM(e) == EF_MOSSWALKER) mosswalker_start(e);
  else if (FSM(e) == EF_PIGEON) pigeon_start(e);
  else if (FSM(e) == EF_MENDER) mender_start(e);
  else if (FSM(e) == EF_PLANTTRAP) planttrap_start(e);
  else if (FSM(e) == EF_SHAKER) shaker_start(e);
  else if (FSM(e) == EF_MOSQUITO) mosquito_start(e, d);
  else if (FSM(e) == EF_FATFLY) fatfly_start(e);
  else if (FSM(e) == EF_MOSSCHARGER) mosscharger_start(e);
  else if (FSM(e) == EF_MOSSKNIGHT) mossknight_start(e);
  else if (FSM(e) == EF_HORNET) hornet_start(e);
}

/* ActiveRegion (a 50 by 35 box round the camera) meets its collider: FSMActivator turns its FSMs on */
static void enemy_dormant_check(Enemy *e) {
  float x0, y0, x1, y1;
  enemy_box(e, &x0, &y0, &x1, &y1);
  if (x1 > g_cam_x - 25 && x0 < g_cam_x + 25 && y1 > g_cam_y - 17.5f && y0 < g_cam_y + 17.5f) {
    e->flags &= (uint8_t)~64;
    e->anim.paused = false;
    enemy_fsm_start(e, ent_at(e->ent));
  }
}

/* ---------------------------------------------------------------- summoners (summon: an enemy brought in from the
 * background as its wave starts) */
#define MAX_SUMMONS 2
enum { SM_IDLE, SM_PAUSE, SM_ENTER, SM_DONE };
typedef struct {
  uint8_t st;
  uint16_t ent;   /* its record */
  float t, tween, sx;
  Anim anim;
} Summon;
static Summon summons[MAX_SUMMONS];

static void summons_enter(void) {
  memset(summons, 0, sizeof summons);
  int n, k = 0;
  const Ent *es = room_ents(&n);
  for (int i = 0; i < n && k < MAX_SUMMONS; i++)
    if (es[i].type == ENT_OBJ && es[i].flags == OK_SUMMON && es[i].p0 >= 0) summons[k].ent = (uint16_t)i, summons[k++].st = SM_IDLE;
    else if (es[i].type == ENT_OBJ && es[i].flags == OK_SUMMON) summons[k++].st = SM_DONE;
  for (; k < MAX_SUMMONS; k++) summons[k].st = SM_DONE;
}

void enemies_summon(void) {
  for (int k = 0; k < MAX_SUMMONS; k++)
    if (summons[k].st == SM_IDLE) summons[k].st = SM_PAUSE, summons[k].t = rand_range(0.25f, 1);   /* (Random Pause) */
}

void enemies_battle_start(void) {
  /* (BATTLE START, to all: sleeping Moss Knights wake) */
  for (int i = 0; i < MAX_ENEMIES; i++)
    if (en[i].mode == EM_ALIVE && FSM(&en[i]) == EF_MOSSKNIGHT) mossknight_wake(&en[i]);
}

static void summons_tick(void) {
  for (int k = 0; k < MAX_SUMMONS; k++) {
    Summon *s = &summons[k];
    if (s->st == SM_PAUSE && (s->t -= DT) <= 0) {
      /* Enter: from the background, facing the Knight, out of the dark */
      const Ent *e = ent_at(s->ent), *d = ent_at((int)e->p0);
      s->sx = hero_x() > e->x0 ? -fabsf(e->y1) : fabsf(e->y1);
      s->tween = rand_range(0.75f, 1.2f), s->t = 0;
      anim_play(&s->anim, kinds[d->a].clip[R_A1]);
      s->st = SM_ENTER;
    } else if (s->st == SM_ENTER) {
      s->anim.events = 0;
      anim_update(&s->anim, DT);
      if ((s->t += DT) >= s->tween) {
        /* Summon: the enemy there, its way, alerted; the summoner gone */
        const Ent *e = ent_at(s->ent);
        for (int i = 0; i < MAX_ENEMIES; i++) {
          Enemy *q = &en[i];
          if (q->mode != EM_ALIVE || q->ent != (uint16_t)e->p0) continue;
          set_scale_x(q, s->sx);
          q->body.x = e->x0, q->body.y = e->y0;
          if (FSM(q) == EF_SPITTER && q->st == SP_IDLE && !(q->flags & 64)) spitter_distance_fly(q);   /* (ALERT) */
        }
        s->st = SM_DONE;
      }
    }
  }
}

static void summons_draw(void) {
  for (int k = 0; k < MAX_SUMMONS; k++) {
    if (summons[k].st != SM_ENTER || summons[k].anim.sprite < 0) continue;
    const Ent *e = ent_at(summons[k].ent);
    /* (iTweenMoveBy, easeOutSine, from 8 up and 17 back; its colour from black to white over a second) */
    float f = sinf(summons[k].t / summons[k].tween * (float)M_PI / 2), c = summons[k].t < 1 ? summons[k].t : 1;
    uint8_t v = (uint8_t)(c * 255), tint = gfx_dyn_tint(11 + 4 * k, v, v, v, 255);
    Inst in;
    sprite_inst(summons[k].anim.sprite, e->x0, e->y0 + 8 - 8 * f, e->x1 + 16.996f - 16.99f * f, summons[k].sx, 1, tint, &in);
    gfx_actor(&in, SORT_KEY(0, 0));
  }
}

void enemies_enter(void) {
  memset(en, 0, sizeof en);
  memset(geo, 0, sizeof geo);
  fk_arena_enter();
  int n, k = 0;
  const Ent *es = room_ents(&n);
  for (int i = 0; i < n && k < MAX_ENEMIES; i++) {
    const Ent *d = &es[i];
    if (d->type != ENT_OBJ || d->flags != OK_ENEMY) continue;
    bool off = false;   /* (DeactivateIfPlayerdata: ET_COND) */
    for (int j = 1; j <= d->s0; j++)
      if (d[j].type == ENT_BOX && d[j].flags == ET_COND && pd_flag((int)d[j].p0) == (d[j].p1 != 0)) off = true;
    if (off || persist_get(d->persist) || ((d->s1 & EF_ARENA_GONE) && arena_done()) || ((d->s1 & EF_ARENA_LATER) && !arena_done())) {
      enemy_terrain_off(d);
      continue;
    }
    Enemy *e = &en[k++];
    e->mode = EM_ALIVE, e->kind = (uint8_t)d->a, e->ent = (uint16_t)i;
    e->damage = (int8_t)d->p1, e->geo_s = (uint8_t)d->p2, e->geo_m = (uint8_t)d->p3, e->geo_l = (uint8_t)d->group;
    e->z = d->x1;
    e->sx = d->y1 ? d->y1 : 1;
    e->hp = (int16_t)d->p0;
    Body *b = &e->body;
    b->x = d->x0, b->y = d->y0;
    const Ent *box = enemy_rec(e, ET_COLLIDER);
    e->sy = 1;
    if (box) {
      if (FSM(e) != EF_CLIMBER) e->rq = (uint8_t)box->p1, e->sy = box->p2 < 0 ? -1 : 1;   /* (a climber turns itself) */
      enemy_set_box(e, box->x0, box->y0, box->x1, box->y1);
      b->gravity_scale = box->p0;
    }
    if (d->s1 & EF_INVINCIBLE) e->flags |= 4;
    b->friction = 0, b->mask = CF_SOLID;   /* (the default material, frictionless) */
    /* its sight: the alert range, else its first other range */
    const Ent *ar = enemy_rec(e, ET_ALERT);
    if (!ar) ar = enemy_rec(e, ET_RANGE);
    if (ar) enemy_range(e, ar, &e->ar_x, &e->ar_y, &e->ar_r, &e->ar_hy);
    const Ent *rc = enemy_rec(e, ET_RECOIL);
    e->rc_base = rc ? rc->x0 : 15, e->rc_dur = rc ? rc->y0 : 0.5f, e->rc_flags = rc ? (uint8_t)rc->a : RF_NONE;
    const Ent *wk = enemy_rec(e, ET_WALKER);
    e->wk_rec = wk ? (uint16_t)(wk - room_ents(&n)) : 0;
    if (d->s1 & EF_CONTACT) {
      /* (its object off: nothing of it there) */
      e->flags |= 128 | 32 | 1;
    } else if (d->s1 & EF_DORMANT) {
      /* (its FSMs off till the active region round the camera meets it: FSMActivator; its sprite still) */
      int c = FSM(e) == EF_MOSSWALKER ? CLIP(e, R_WALK) : CLIP(e, R_IDLE) >= 0 ? CLIP(e, R_IDLE) : CLIP(e, R_A1) >= 0 ? CLIP(e, R_A1) : CLIP(e, R_WALK);
      if (c >= 0) anim_play(&e->anim, c), e->anim.paused = true;
      e->flags |= 64;
    } else
      enemy_fsm_start(e, d);
    for (int h = 0; h < 8; h++) {
      const Ent *r = hitbox_rec(e, h);
      if (!r) break;
      if (r->a) e->hb_on |= (uint8_t)(1 << h);
    }
  }
#ifdef HOST
  if (getenv("ENEMYCOUNT")) {
    int c = 0;
    for (int i = 0; i < n; i++) c += es[i].type == ENT_OBJ && es[i].flags == OK_ENEMY;
    fprintf(stderr, "enemies %d\n", c);
  }
#endif
  memset(&sh, 0, sizeof sh);
  memset(balls, 0, sizeof balls);
  memset(bullets, 0, sizeof bullets);
  memset(waves, 0, sizeof waves);
  memset(spurts, 0, sizeof spurts);
  memset(&gm, 0, sizeof gm);
  summons_enter();
  shade_spawn_check();
}

/* FixedUpdate and the physics step */
void enemies_fixed(void) {
  for (int i = 0; i < MAX_ENEMIES; i++) {
    Enemy *e = &en[i];
    if (e->mode == EM_OFF) continue;
    if (FSM(e) == EF_SHADE) {
      if (e->mode == EM_ALIVE) shade_fixed(e), recoil_fixed(e);
      else shade_fixed(e);
      if (e->st != SH_RETREAT) body_step(&e->body, DT);
      continue;
    }
    if (e->mode == EM_ALIVE && FSM(e) == EF_FK) {
      fk_fixed(e);
      if (fk.kinematic) e->body.vx = e->body.vy = 0;
      else body_step(&e->body, DT);
      continue;
    }
    if (FSM(e) == EF_HORNET) {
      if (e->mode == EM_CORPSE) hornet_corpse_fixed(e);
      else if (!ho.kinematic) hornet_fixed(e), recoil_fixed(e), body_step(&e->body, DT);
      continue;
    }
    if (e->mode == EM_ALIVE && (FSM(e) == EF_FKHEAD || (e->flags & 128))) continue;   /* (the head: its body's; off) */
    if (e->mode == EM_ALIVE && ((FSM(e) == EF_MOSSWALKER && e->b1) || FSM(e) == EF_PIGEON || FSM(e) == EF_PLANTTRAP)) {
      /* (a kinematic body, a trigger, frames' colliders: nothing stops them) */
      recoil_fixed(e);
      if (FSM(e) == EF_MOSSWALKER) mosswalker_fixed(e);
      else if (FSM(e) == EF_PIGEON) pigeon_fixed(e);
      continue;
    }
    if (e->mode == EM_ALIVE && FSM(e) == EF_CLIMBER) {
      /* (a kinematic body: its velocity, nothing in its way) */
      recoil_fixed(e);
      e->body.x += e->body.vx * DT, e->body.y += e->body.vy * DT;
      continue;
    }
    if (e->mode == EM_ALIVE) {
      if (e->flags & 64) {}   /* (dormant: its FSMs off) */
      else if (FSM(e) == EF_CRAWLER) crawler_fixed(e);
      else if (FSM(e) == EF_BUZZER) buzzer_fixed(e);
      else if (FSM(e) == EF_HUSK) husk_fixed(e);
      else if (FSM(e) == EF_BOUNCER) bouncer_fixed(e);
      else if (FSM(e) == EF_SPITTER) spitter_fixed(e);
      else if (FSM(e) == EF_ROLLER) roller_fixed(e);
      else if (FSM(e) == EF_LEAPER) leaper_fixed(e);
      else if (FSM(e) == EF_GUARD) guard_fixed(e);
      else if (FSM(e) == EF_GFLY) gfly_fixed(e);
      else if (FSM(e) == EF_HATCHER) hatcher_fixed(e);
      else if (FSM(e) == EF_HATCHLING) hatchling_fixed(e);
      else if (FSM(e) == EF_SLUG) slug_fixed(e);
      else if (FSM(e) == EF_SHAKER) shaker_fixed(e);
      else if (FSM(e) == EF_MOSQUITO) mosquito_fixed(e);
      else if (FSM(e) == EF_FATFLY) fatfly_fixed(e);
      else if (FSM(e) == EF_MOSSCHARGER) mosscharger_fixed(e);
      else if (FSM(e) == EF_MOSSKNIGHT) mossknight_fixed(e);
      recoil_fixed(e);
      if (wants_contacts(e)) {
        float pvx = e->body.vx, pvy = e->body.vy, box[4] = {0};
        int had = e->body.ncontacts;
        e->body.events = body_events;
        if (FSM(e) == EF_MOSQUITO) mosquito_box(e, box);
        body_step(&e->body, DT);
        if (FSM(e) == EF_MOSQUITO) e->body.ox = box[0], e->body.oy = box[1], e->body.hx = box[2], e->body.hy = box[3];
        enemy_contacts(e, pvx, pvy, had);
        e->body.events = NULL, e->body.nevents = 0;
      } else
        body_step(&e->body, DT);
    } else if (FSM(e) != EF_BLOCKER) {
      /* Corpse: falls, lands (or smashes, a breaker) and stays */
      const Ent *cr = enemy_rec(e, ET_CORPSE);
      bool breaker = cr && (cr->a & CF_BREAKER);
      float cbounce = cr ? cr->p1 : -1;
      float pvx = e->body.vx, pvy = e->body.vy;
      int had = e->body.ncontacts;
      if (e->st != CS_LANDED || e->body.vx || e->body.vy) body_step(&e->body, DT);
      if (e->st == CS_AIR) {
        bool ground = false;
        for (int c = 0; c < e->body.ncontacts; c++) ground |= e->body.cny[c] > 0.5f;
        if (ground) {
          if (breaker) {
            anim_play(&e->anim, kinds[e->kind].clip[R_DEATH_LAND]);
            e->body.vx = e->body.vy = 0;
            e->st = CS_DEATH_ANIM;
            if (FSM(e) == EF_HATCHER) hatcher_corpse_smash(e);
          } else {
            body_bounce(&e->body, pvx, pvy, had, cbounce);
            anim_play(&e->anim, kinds[e->kind].clip[R_DEATH_LAND]);
            e->st = CS_LANDED;
          }
        } else
          body_bounce(&e->body, pvx, pvy, had, cbounce);
        if (e->body.y < -10) e->mode = EM_OFF;
      }
    }
  }
  geo_tick();
  balls_tick();
  bullets_tick();
  waves_tick();
  fk_arena_tick();
  fk_head_place();
  gfly_corpse_tick();
  summons_tick();
}

/* Update: the FSMs' every-frame actions, the animations, the timers */
void enemies_update(void) {
  for (int i = 0; i < MAX_ENEMIES; i++) {
    Enemy *e = &en[i];
    if (e->mode == EM_OFF) continue;
    e->anim.events = 0;
    anim_update(&e->anim, DT);
    if (e->flashing && (e->flash_t += DT) > (uninfected(e) ? 0.37f : 0.27f)) e->flashing = false;
    if (e->mode == EM_ALIVE) {
      frame_collider(e);
      if (e->evasion > 0) e->evasion -= DT;
      if (e->ar_r > 0) sight_update(e);
      if (e->flags & 128) {
        /* ActivateChildrenOnContact: on as the Knight touches the trigger */
        const Ent *t = enemy_rec(e, ET_CONTACT);
        const Body *k = &g_hero.body;
        if (t && !g_hero.hidden && k->x + k->ox + k->hx > t->x0 && k->x + k->ox - k->hx < t->x1 &&
            k->y + k->oy + k->hy > t->y0 && k->y + k->oy - k->hy < t->y1) {
          e->flags &= (uint8_t)~(128 | 32 | 1);
          enemy_fsm_start(e, ent_at(e->ent));
        }
        continue;
      }
      if (e->flags & 64) enemy_dormant_check(e);
      if (e->flags & 64) {}   /* (dormant) */
      else if (FSM(e) == EF_BUZZER) buzzer_update(e, ent_at(e->ent));
      else if (FSM(e) == EF_SHADE) shade_update(e);
      else if (FSM(e) == EF_HUSK) husk_update(e);
      else if (FSM(e) == EF_CLIMBER) climber_update(e);
      else if (FSM(e) == EF_BOUNCER) bouncer_update(e);
      else if (FSM(e) == EF_SPITTER) spitter_update(e);
      else if (FSM(e) == EF_ROLLER) roller_update(e);
      else if (FSM(e) == EF_BLOCKER) blocker_update(e);
      else if (FSM(e) == EF_LEAPER) leaper_update(e);
      else if (FSM(e) == EF_GUARD) guard_update(e);
      else if (FSM(e) == EF_FK) fk_update(e);
      else if (FSM(e) == EF_GFLY) gfly_update(e);
      else if (FSM(e) == EF_HATCHER) hatcher_update(e);
      else if (FSM(e) == EF_HATCHLING) hatchling_update(e);
      else if (FSM(e) == EF_SLUG) slug_update(e);
      else if (FSM(e) == EF_MOSSWALKER) mosswalker_update(e);
      else if (FSM(e) == EF_PIGEON) pigeon_update(e);
      else if (FSM(e) == EF_MENDER) mender_update(e);
      else if (FSM(e) == EF_PLANTTRAP) planttrap_update(e);
      else if (FSM(e) == EF_SHAKER) shaker_update(e);
      else if (FSM(e) == EF_MOSQUITO) mosquito_update(e);
      else if (FSM(e) == EF_FATFLY) fatfly_update(e);
      else if (FSM(e) == EF_MOSSCHARGER) mosscharger_update(e);
      else if (FSM(e) == EF_MOSSKNIGHT) mossknight_update(e);
      else if (FSM(e) == EF_HORNET) hornet_update(e);
      if (FSM(e) == EF_FK) {
        /* (its Hitter: on till its FSM turns it off) */
        e->sub.events = 0;
        if (e->sub_hb) anim_update(&e->sub, DT);
      } else if (FSM(e) == EF_MOSSKNIGHT || FSM(e) == EF_HORNET) {
        /* (its hitboxes its own; its effect updated with it) */
      } else
        hitbox_tick(e);
      e->flags &= (uint8_t)~8;
      e->rc_flags &= (uint8_t)~(RF_HIT | RF_BLOCKED);
    } else if (FSM(e) == EF_SHADE)
      shade_update(e);
    else if (FSM(e) == EF_HORNET)
      hornet_corpse_update(e);
    else if (FSM(e) == EF_BLOCKER)
      blocker_corpse(e);
    else if (e->st == CS_DEATH_ANIM && !e->anim.playing)
      e->mode = EM_OFF;
  }
}

void enemies_draw(void) {
  for (int i = 0; i < MAX_ENEMIES; i++) {
    const Enemy *e = &en[i];
    if (e->mode == EM_OFF) continue;
    Inst in;
    float z = e->mode == EM_CORPSE && FSM(e) != EF_SHADE ? 0.0085f : e->z;
    if (FSM(e) == EF_SHADE && e->st == SH_DISSIPATE) continue;   /* (its renderer off) */
    if ((e->flags & 32) || (FSM(e) == EF_FKHEAD && !fk.head_shown && e->mode == EM_ALIVE)) continue;
    if (FSM(e) == EF_MOSSCHARGER && e->mode == EM_ALIVE && e->b0) continue;   /* (its renderer off) */
    if (FSM(e) == EF_HORNET) {
      hornet_draw(e, i, z);
      continue;
    }
    float sy = fabsf(e->sx) > 0 ? fabsf(e->sx) : 1;
    if (FSM(e) == EF_PIGEON && e->st == PG_FLY)
      /* (its x scale 1, its y its size; flying left its sprite turned, a half turn more) */
      sprite_inst_rot(e->anim.sprite, e->body.x, e->body.y, z, e->b0 ? -1.0f : 1.0f, e->qx, e->ang, flash_tint(e, 2 + i % 5), &in);
    else if (e->ang != 0 && e->mode == EM_ALIVE)
      sprite_inst_rot(e->anim.sprite, e->body.x, e->body.y, z, e->sx, 1, e->ang, flash_tint(e, 2 + i % 5), &in);
    else if ((e->rq || e->sy < 0) && (e->mode == EM_ALIVE || FSM(e) == EF_PLANTTRAP))
      /* (turned: a y scale of -1 is a half turn more with x flipped) */
      sprite_inst_rot(e->anim.sprite, e->body.x, e->body.y, z, e->sy < 0 ? -e->sx : e->sx, sy,
                      (float)(e->rq * 90 + (e->sy < 0 ? 180 : 0)), flash_tint(e, 2 + i % 5), &in);
    else
      sprite_inst(e->anim.sprite, e->body.x + e->jx, e->body.y + e->jy, z, e->sx, sy, flash_tint(e, 2 + i % 5), &in);
    gfx_actor(&in, SORT_KEY(0, 0));
    if (e->sub_hb && e->sub.sprite >= 0 && e->mode == EM_ALIVE) {
      sprite_inst(e->sub.sprite, e->body.x, e->body.y, z - 0.001f, e->sx, sy, flash_tint(e, 2 + i % 5), &in);
      gfx_actor(&in, SORT_KEY(0, 0));
    }
    if (FSM(e) == EF_MOSSKNIGHT && e->fx && e->sub.sprite >= 0 && e->mode == EM_ALIVE) {
      /* (Slash Effect at it; Slash2 Effect at its child's place and scale) */
      bool s2 = e->fx == 1 + MK_EFFECT_SLASH2;
      sprite_inst(e->sub.sprite, e->body.x + (s2 ? (e->sx < 0 ? 1.14f : -1.14f) : 0), e->body.y + (s2 ? -0.59f : 0), z - 0.001f,
                  e->sx * (s2 ? 0.7655f : 1), sy, flash_tint(e, 2 + i % 5), &in);
      gfx_actor(&in, SORT_KEY(0, 0));
    }
    if (FSM(e) == EF_SHADE && sh.slash_on) {
      sprite_inst(sh.slash.sprite, e->body.x - 0.0486f * e->sx, e->body.y - 0.0117f, z - 0.001f, 1.27f * e->sx, 1, 0, &in);
      gfx_actor(&in, SORT_KEY(0, 0));
    }
  }
  bullets_draw();
  spurts_draw();
  fk_arena_draw();
  gfly_corpse_draw();
  summons_draw();
  for (int i = 0; i < MAX_BALLS; i++) {
    const Ball *b = &balls[i];
    if (!b->on) continue;
    Inst in;
    sprite_inst(b->anim.sprite, b->x, b->y, -0.1f, b->vx < 0 ? -1.0f : 1.0f, 1, 0, &in);
    gfx_actor(&in, SORT_KEY(0, 0));
  }
  for (int i = 0; i < MAX_GEO; i++) {
    const Geo *g = &geo[i];
    if (!g->on) continue;
    Inst in;
    sprite_inst(g->anim.sprite, g->x, g->y, 0.0015f, GEO_SCALE, GEO_SCALE, 0, &in);
    gfx_actor_minor(&in, SORT_KEY(0, 0));
    /* (Gathering Swarm: its bug, eased in from above it to its place, then carrying it; Lamp_Bug_idle from its
     * start) */
    float t = g->getter ? g->age - getter_wait(g) : -1;
    if (t < 0) continue;
    float e = getter_ease(g), q = t >= e ? 1 : sinf(t / e * 1.5707964f);
    float sx = GETTER_X + getter_rand(g, 2, -1, 1), sy = GETTER_Y + getter_rand(g, 3, 0.5f, 1.5f);
    float bx = sx + (GETTER_X - sx) * q, by = sy + (GETTER_Y - sy) * q;
    int n = clip_frames_count(CLIP_GEOBUG_LAMP_BUG_IDLE);
    sprite_inst(clip_frame_sprite(CLIP_GEOBUG_LAMP_BUG_IDLE, (int)(t * 12) % n), g->x + bx * GEO_SCALE,
                g->y + by * GEO_SCALE, 0.0005f, GEO_SCALE * 1.4838f, GEO_SCALE * 1.4838f, 0, &in);
    gfx_actor_minor(&in, SORT_KEY(0, 0));
  }
}

/* ---------------------------------------------------------------- what touches them */
void enemies_swing_start(void) { swing_bits = 0; }

/* HERO CAST SPELL (to all) */
void enemies_hero_cast_spell(void) {
  for (int i = 0; i < MAX_ENEMIES; i++)
    if (en[i].mode == EM_ALIVE && FSM(&en[i]) == EF_PIGEON && !(en[i].flags & 64)) pigeon_spell(&en[i]);
}

/* the slash's shape: enemies it touches are hit, once a swing -> HB_* */
/* the slash's shape's bounds */
static void shape_bounds(const float *pts, int n, float *x0, float *y0, float *x1, float *y1) {
  *x0 = *x1 = pts[0], *y0 = *y1 = pts[1];
  for (int k = 1; k < n; k++) {
    *x0 = fminf(*x0, pts[2 * k]), *x1 = fmaxf(*x1, pts[2 * k]);
    *y0 = fminf(*y0, pts[2 * k + 1]), *y1 = fmaxf(*y1, pts[2 * k + 1]);
  }
}

/* the Moss Knights' own: its Wake Box struck, its slash met (a parry: the Knight thrown back, unhurt a moment) */
static int mossknights_nail(const float *pts, int npts) {
  int out = 0;
  float x0, y0, x1, y1;
  shape_bounds(pts, npts, &x0, &y0, &x1, &y1);
  for (int i = 0; i < MAX_ENEMIES; i++) {
    Enemy *e = &en[i];
    if (e->mode != EM_ALIVE || FSM(e) != EF_MOSSKNIGHT) continue;
    if (e->st == MK_SLEEP) {
      const Ent *d = ent_at(e->ent);
      for (int k = 1; k <= d->s0; k++)
        if (d[k].type == ENT_BOX && d[k].flags == ET_RANGE && d[k].s0 == STR_WAKE_BOX) {
          float cx, cy, rx, hy;
          enemy_range(e, &d[k], &cx, &cy, &rx, &hy);
          cx += e->body.x, cy += e->body.y;
          if (box_meets_shape(cx - rx, cy - hy, cx + rx, cy + hy, pts, npts)) mossknight_nailed(e);
        }
      continue;
    }
    if (e->hb_on && hitbox_touch(e, x0, y0, x1, y1)) {
      FREEZE_MOMENT_1();
      cam_shake(SHAKE_ENEMY_KILL);
      hero_nail_parry();
      out |= HB_BOUNCE | HB_RECOIL;
    }
  }
  return out;
}

/* grass balls the nail or a spell meets: Break */
static void grass_balls_hit(float x0, float y0, float x1, float y1) {
  for (int i = 0; i < MAX_BULLETS; i++) {
    Bullet *b = &bullets[i];
    float r = GRASS_BALL_R * b->scale;
    if (b->on && b->kind == 1 && x1 > b->x - r && x0 < b->x + r && y1 > b->y - r && y0 < b->y + r) bullet_impact(b);
  }
}

int enemies_nail(const float *pts, int npts, float direction, int damage) {
  int out = mossknights_nail(pts, npts);
  if (hornet()) out |= hornet_nail(pts, npts, direction);
  float bx0, by0, bx1, by1;
  shape_bounds(pts, npts, &bx0, &by0, &bx1, &by1);
  grass_balls_hit(bx0, by0, bx1, by1);
  barrels_nail(pts, npts, direction);
  for (int i = 0; i < MAX_ENEMIES; i++) {
    Enemy *e = &en[i];
    if (e->mode != EM_ALIVE || (e->flags & 17)) continue;
    float x0, y0, x1, y1;
    enemy_box(e, &x0, &y0, &x1, &y1);
    if (!box_meets_shape(x0, y0, x1, y1, pts, npts)) continue;
    out |= HB_BOUNCE | HB_RECOIL;
    if (swing_bits >> i & 1) continue;
    swing_bits |= 1u << i;
    enemy_hit(e, direction, damage);
  }
  return out;
}

uint32_t enemies_spell(float x0, float y0, float x1, float y1, float direction, int damage, float magnitude, uint32_t done) {
  uint32_t out = 0;
  grass_balls_hit(x0, y0, x1, y1);
  for (int i = 0; i < MAX_ENEMIES; i++) {
    Enemy *e = &en[i];
    if (e->mode != EM_ALIVE || (e->flags & 17) || (done >> i & 1)) continue;
    float a0, b0, a1, b1;
    enemy_box(e, &a0, &b0, &a1, &b1);
    if (!(x1 > a0 && x0 < a1 && y1 > b0 && y0 < b1)) continue;
    out |= 1u << i;
    enemy_hit_by(e, direction, damage, false, magnitude);
  }
  return out;
}

/* DamageHero: the first enemy touching the HeroBox -> its damage (0: none), which side it is on */
int enemies_touch_hero(float x0, float y0, float x1, float y1, int *side) {
  for (int i = 0; i < MAX_ENEMIES; i++) {
    const Enemy *e = &en[i];
    if (e->mode != EM_ALIVE || e->damage <= 0 || (e->flags & 17)) continue;
    float a0, b0, a1, b1;
    enemy_box(e, &a0, &b0, &a1, &b1);
    if (x1 > a0 && x0 < a1 && y1 > b0 && y0 < b1) {
      *side = e->body.x > g_hero.body.x ? SIDE_RIGHT : SIDE_LEFT;
      return e->damage;
    }
    int hd = e->hb_on ? hitbox_touch(e, x0, y0, x1, y1) : FSM(e) == EF_HORNET && hornet_touch(e, x0, y0, x1, y1);
    if (hd) {
      *side = e->body.x > g_hero.body.x ? SIDE_RIGHT : SIDE_LEFT;
      return hd;
    }
    if (FSM(e) == EF_SHADE && sh.slash_on) {
      /* its Slash's polygon */
      float pts[10];
      for (int k = 0; k < 5; k++)
        pts[2 * k] = e->body.x + (-0.0486f + shade_slash_pts[k][0] * 1.27f) * e->sx, pts[2 * k + 1] = e->body.y - 0.0117f + shade_slash_pts[k][1];
      if (box_meets_shape(x0, y0, x1, y1, pts, 5)) {
        *side = e->body.x > g_hero.body.x ? SIDE_RIGHT : SIDE_LEFT;
        return 1;
      }
    }
  }
  int dmg = bullets_touch_hero(x0, y0, x1, y1, side);
  if (dmg) return dmg;
  dmg = spurts_touch(x0, y0, x1, y1, side);
  if (dmg) return dmg;
  dmg = barrels_touch(x0, y0, x1, y1, side);
  if (dmg) return dmg;
  for (int i = 0; i < MAX_BALLS; i++) {
    const Ball *b = &balls[i];
    if (!b->on || b->ending) continue;
    float k = b->vx < 0 ? -1.0f : 1.0f, cx = b->x + 0.26f * k, cy = b->y - 0.0496f;
    if (x1 > cx - 0.99f && x0 < cx + 0.99f && y1 > cy - 0.6204f && y0 < cy + 0.6204f) {
      *side = b->x > g_hero.body.x ? SIDE_RIGHT : SIDE_LEFT;
      return 1;
    }
  }
  return 0;
}

#ifdef HOST
void enemies_debug(void) {
  for (int i = 0; i < MAX_ENEMIES; i++) {
    const Enemy *e = &en[i];
    if (e->mode == EM_OFF) continue;
    printf("   enemy %d kind %d mode %d st %d hp %d pos %.2f,%.2f v %.2f,%.2f sx %.0f rc %d see %d/%d\n", i, e->kind, e->mode,
           e->st, e->hp, e->body.x, e->body.y, e->body.vx, e->body.vy, e->sx, e->rc_state, e->in_alert, e->can_see);
  }
  int ng = 0;
  for (int i = 0; i < MAX_GEO; i++) ng += geo[i].on;
  printf("   geo %d pieces, %d in purse, mp %d\n", ng, g_pd.geo, g_pd.mp);
}

/* (tests: the enemy of the room's record ent out and alive) */
bool enemies_alive_ent(int ent) {
  for (int i = 0; i < MAX_ENEMIES; i++)
    if (en[i].mode == EM_ALIVE && en[i].ent == ent) return true;
  return false;
}

/* (tests: a hit on every enemy near the Knight, invincible or not) */
void enemies_debug_hit(int damage) {
  for (int i = 0; i < MAX_ENEMIES; i++) {
    Enemy *e = &en[i];
    float dx = e->body.x - g_hero.body.x, dy = e->body.y - g_hero.body.y;
    float r = getenv("HKHITR") ? (float)atof(getenv("HKHITR")) : 15;
    if (e->mode != EM_ALIVE || (e->flags & 5) || dx * dx + dy * dy > r * r) continue;
    if (FSM(e) == EF_FK && e->st == FK_DORMANT) continue;
    enemy_hit(e, dx > 0 ? 0 : 180, damage);
  }
}

/* (tests: the False Knight come down from his roof, his fight begun) */
bool enemies_fk_awake(void) {
  const Enemy *e = fk_body();
  return e && e->st != FK_DORMANT;
}
#endif

/* her Hornet Saver (walls round her arena): ActivateAllChildren */
void enemies_hornet_saver(bool on) { vm_activate_children(VMSTR_HORNET_SAVER, on); }
