#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif
/* The Dark World's enemies on the overworld (obj_chaseenemy): each room's kind, how it paces, the "!" when
 * Kris comes near, the chase, and the battle when it touches him (obj_testoverworldenemy). They come back
 * each time the room is entered, as there. */
#include <string.h>
#include "battle.h"
#include "event.h"
#include "game.h"
#include "gfx.h"
#include "gmath.h"
#include "rooms.h"
#include "world.h"

#define MAX_CH 8
typedef struct {
  bool on;
  Obj *o;                     /* what the room draws (in its order) */
  float x, y, xstart, ystart;
  Motion m;
  float walk, pacetype, moveradius, pacespeed, remspeed;
  int chasetype, radius, alertcon, alerttimer, alarm, pacecon, pacecon2, pacetimer, facing, facetimer;
  bool frozen, hadfrozen, offscreen_frozen, cancelwalk, eraser, ignoresolid;
  int encounter, touchflag, exc_t;
  uint16_t spr, touchspr;
  float ispd;
  int8_t bl, br, bt, bb;      /* its sprite's box (the original's bbox margins) */
} Chaser;
static Chaser ch[MAX_CH];
static int touched = -1;

static void bbox_of(uint16_t spr, Chaser *c) {
  static const struct {
    uint16_t spr;
    int8_t l, r, t, b;
  } boxes[] = {{spr_diamond_overworld, 2, 32, 25, 39}, {spr_rabbick_enemy_overworld, 2, 34, 18, 40},
               {spr_blockguy_overworld, 0, 24, 42, 63}, {spr_daimond_knight_idle, 2, 32, 25, 39},
               {spr_hathyx_overworld, 10, 50, 27, 47}, {spr_smallchecker_front, 0, 24, 0, 32},
               {spr_jigsawry_idle, 0, 37, 20, 35}};
  for (unsigned i = 0; i < sizeof boxes / sizeof boxes[0]; i++)
    if (boxes[i].spr == spr) {
      c->bl = boxes[i].l, c->br = boxes[i].r, c->bt = boxes[i].t, c->bb = boxes[i].b;
      return;
    }
  c->bl = 0, c->br = (int8_t)(spr_w(spr) - 1), c->bt = 0, c->bb = (int8_t)(spr_h(spr) - 1);
}

/* obj_chaseenemy's create: the room's kind of enemy; false if it isn't there */
static bool chaser_create(Chaser *c, int x, int y) {
  const RoomInfo *r = world_room_info();
  memset(c, 0, sizeof *c);
  c->x = c->xstart = (float)x, c->y = c->ystart = (float)y;
  c->pacetype = 1, c->radius = 200, c->moveradius = 160, c->pacespeed = 1, c->offscreen_frozen = true;
  c->encounter = 4, c->spr = spr_diamond_overworld, c->touchspr = spr_diamondm_hurt;
  switch (g_room) {
    case room_field1: c->encounter = 4; break;
    case room_field2:
      c->encounter = 5;
      if (flag(501) > 0) return false;
      break;
    case room_field_puzzle1: c->encounter = 6, c->radius = 200, c->pacetype = 5; break;
    case room_field_getsusie:
      c->encounter = 7, c->spr = c->touchspr = spr_smallchecker_front, c->pacetype = 6, c->ispd = 0.2f;
      break;
    case room_field4: c->encounter = 33; break;
    case room_field_secret1:
      c->spr = c->touchspr = spr_jigsawry_idle, c->encounter = 23;
      if (plot() < 130) return false;
      break;
    case room_forest_area0:
      c->spr = c->touchspr = spr_rabbick_enemy_overworld, c->encounter = 16, c->alertcon = -1, c->pacetype = 10;
      break;
    case room_forest_area1: case room_forest_area3: case room_forest_secret1:
      c->pacetype = g_room == room_forest_area1 ? 11 : 7;
      c->spr = c->touchspr = spr_rabbick_enemy_overworld;
      c->encounter = g_room == room_forest_area3 ? 24 : g_room == room_forest_secret1 ? 32 : 17;
      c->touchflag = 505, c->alertcon = -1;
      break;
    case room_forest_maze1:
      c->touchflag = 505, c->spr = c->touchspr = spr_rabbick_enemy_overworld, c->encounter = 17, c->pacetype = 1;
      if (flag(505) == 1) c->chasetype = 3;
      break;
    case room_forest_maze2:
      c->touchflag = 506, c->encounter = 18, c->spr = c->touchspr = spr_blockguy_overworld, c->pacetype = 1;
      c->chasetype = flag(506) == 1 ? 3 : 4;
      break;
    case room_forest_beforeclover:
      c->encounter = 18, c->spr = c->touchspr = spr_blockguy_overworld, c->pacetype = 7, c->alertcon = -1;
      break;
    case room_forest_area4:
      c->encounter = 19, c->spr = c->touchspr = spr_blockguy_overworld;
      c->pacetype = x < r->w / 2 ? 7.5f : 7;
      c->eraser = true, c->touchflag = 506, c->chasetype = 4, c->radius = 90, c->ignoresolid = true;
      c->offscreen_frozen = false;
      if (flag(506) >= 1) c->alertcon = -1;
      break;
    case room_cc_1f:
      c->pacetype = 9, c->chasetype = 4, c->moveradius = 160, c->spr = c->touchspr = spr_daimond_knight_idle;
      c->encounter = 28, c->touchflag = 507;
      if (flag(507) == 1) c->alertcon = -1;
      break;
    case room_cc_3f:
      c->eraser = true, c->ignoresolid = true, c->chasetype = 4, c->pacetype = 9, c->radius = 165;
      c->moveradius = x >= r->w / 2 ? -160 : 160;
      c->spr = c->touchspr = spr_hathyx_overworld, c->encounter = 30, c->offscreen_frozen = false;
      c->touchflag = 508;
      if (flag(508) == 1) c->alertcon = -1;
      break;
    default: break;
  }
  bbox_of(c->spr, c);
  c->on = true;
  c->o = obj_new(c->spr, x, y);
  return true;
}

void chasers_setup(void) {
  memset(ch, 0, sizeof ch);
  if (!world_dark()) return;
  touched = -1;
  Inst in;
  for (int k = 0; k < MAX_CH && world_inst(obj_chaseenemy, k, &in); k++) chaser_create(&ch[k], in.x, in.y);
}

/* an enemy a cutscene puts in the room (getsusieevent's Hathy...) */
void chaser_add(int x, int y, int radius) {
  for (int k = 0; k < MAX_CH; k++)
    if (!ch[k].on) {
      if (chaser_create(&ch[k], x, y)) ch[k].radius = radius;
      return;
    }
}

/* the last one added jumps out (obj_rabbick_bush: vspeed 16) */
void chaser_kick(int vs) {
  for (int k = MAX_CH - 1; k >= 0; k--)
    if (ch[k].on) {
      mo_v(&ch[k].m, (float)vs);
      return;
    }
}

bool chasers_any(void) {
  for (int k = 0; k < MAX_CH; k++)
    if (ch[k].on) return true;
  return false;
}

static bool outside_camera(const Chaser *c, int by) {
  int w = spr_w(c->spr) * 2, h = spr_h(c->spr) * 2;
  return c->x > cam_x + 640 + by || c->x + w < cam_x - by || c->y > cam_y + 480 + by || c->y + h < cam_y - by;
}

/* GameMaker's distance_to_point: from the box's nearest edge */
static float box_distance(const Chaser *c, float px, float py) {
  float x0 = c->x + c->bl * 2, x1 = c->x + c->br * 2 + 2, y0 = c->y + c->bt * 2, y1 = c->y + c->bb * 2 + 2;
  float dx = px < x0 ? x0 - px : px > x1 ? px - x1 : 0, dy = py < y0 ? y0 - py : py > y1 ? py - y1 : 0;
  return sqrtf(dx * dx + dy * dy);
}

static void towards(Chaser *c, float tx, float ty, float sp) { mo_set(&c->m, sp, point_direction(c->x, c->y, tx, ty)); }

static void chaser_step(Chaser *c, bool interact) {
  int w = spr_w(c->spr) * 2, h = spr_h(c->spr) * 2;
  float targetx = pl.x + 19 - w / 2.f, targety = pl.y + 38 - h / 2.f;
  c->frozen = (c->offscreen_frozen && outside_camera(c, 200)) || interact;
  if (c->frozen && !c->hadfrozen) c->remspeed = mo_speed(&c->m), mo_speed_set(&c->m, 0), c->hadfrozen = true;
  if (c->exc_t > 0) c->exc_t--;
  if (c->frozen) return;
  if (c->hadfrozen) {
    if (mo_speed(&c->m) == 0) mo_speed_set(&c->m, c->remspeed);
    c->hadfrozen = false;
  }
  if (c->alarm > 0 && --c->alarm == 0) c->alertcon++;
  if (c->pacecon == 0) {
    int t = ++c->pacetimer;
    float pt = c->pacetype;
    if (pt == 1) {
      if (t == 10) mo_h(&c->m, 2);
      if (t == 34) mo_h(&c->m, 0);
      if (t == 50) mo_h(&c->m, -2);
      if (t == 74) mo_h(&c->m, 0);
      if (t == 80) c->pacetimer = 0;
    } else if (pt == 2) {
      mo_h(&c->m, sinf(t / 24.f) * 4), mo_v(&c->m, cosf(t / 24.f) * 4);
    } else if (pt == 5) {
      if (t == 1) mo_v(&c->m, 6);
      if (t == 25) mo_v(&c->m, -6);
      if (c->y < c->ystart) c->pacetimer = 0;
    } else if (pt == 6) {
      c->cancelwalk = true, c->walk += 0.25f;
    } else if (pt == 7) {
      mo_h(&c->m, -sinf(t / 30.f) * 10);
    } else if (pt == 7.5f) {
      mo_h(&c->m, sinf(t / 30.f) * 10);
    } else if (pt == 8) {
      mo_v(&c->m, -sinf(t / 25.f) * 12.5f);
    } else if (pt == 9) {
      mo_h(&c->m, sinf(t / (c->pacespeed * 30)) * (c->moveradius / 20));
      c->pacespeed = 0.8f;
    } else if (pt == 10) {
      if (c->pacecon2 == 0) {
        if (c->m.vs > 0) mo_v(&c->m, c->m.vs * 0.9f);
        if (c->m.vs <= 0.5f) c->pacecon2 = 1;
      }
      if (c->pacecon2 == 1) towards(c, (float)pl.x, (float)pl.y, 4);
    }
  }
  if (c->alertcon == 0 && ++c->alerttimer >= 6) {
    c->alerttimer -= 6;
    if (box_distance(c, targetx + w / 2.f, targety + h / 2.f) <= c->radius) {
      mo_speed_set(&c->m, 0);
      c->pacecon = 1, c->alerttimer = 0, c->alertcon = 1;
      c->exc_t = 20;
      c->alarm = 20;
    }
  }
  if (c->alertcon == 2) {
    float sp = mo_speed(&c->m);
    switch (c->chasetype) {
      case 0:
        if (sp < 4) sp = 4;
        if (sp < 7) sp += 0.5f;
        towards(c, targetx, targety, sp);
        break;
      case 1:
        if (c->alerttimer == 0) towards(c, targetx, targety, 10);
        c->alerttimer++;
        if (c->alerttimer >= 20) mo_speed_set(&c->m, mo_speed(&c->m) * 0.75f);
        if (c->alerttimer >= 25) mo_speed_set(&c->m, 0);
        if (c->alerttimer >= 27) c->alerttimer = 0;
        break;
      case 3:
        if (sp < 2) sp = 2;
        if (sp < 4) sp += 0.5f;
        towards(c, targetx, targety, sp);
        break;
      case 4:
        if (sp < 6) sp = 6;
        if (sp < 14) sp += 0.5f;
        towards(c, targetx, targety, sp);
        break;
      default: break;
    }
  }
  if (++c->facetimer >= 10) {
    if (c->m.hs <= -0.2f && c->facing == 1) c->facing = 0, c->facetimer = 0;
    if (c->m.hs >= 0.2f && c->facing == 0) c->facing = 1, c->facetimer = 0;
  }
  if (!c->cancelwalk) {
    c->walk += mo_speed(&c->m) / 20;
    if (mo_speed(&c->m) == 0) c->walk = 0;
  }
  if (c->ispd) c->walk += c->ispd;
  /* the move, then (End Step) back off the walls while chasing */
  c->x += c->m.hs, c->y += c->m.vs;
  if (c->alertcon >= 1 && !c->ignoresolid) {
    int x0 = (int)c->x + c->bl * 2, y0 = (int)c->y + c->bt * 2;
    if (!world_box_free(x0, y0, (int)c->x + c->br * 2 + 2, (int)c->y + c->bb * 2 + 2, true)) {
      c->x -= c->m.hs, c->y -= c->m.vs;
      c->m.hs = c->m.vs = 0;
    }
  }
}

static void chaser_sync(int k);
static bool touches_kris(const Chaser *c) {
  int kx0 = pl.x, ky0 = pl.y + 50, kx1 = pl.x + 38, ky1 = pl.y + 78;
  int x0 = (int)c->x + c->bl * 2, y0 = (int)c->y + c->bt * 2, x1 = (int)c->x + c->br * 2 + 2,
      y1 = (int)c->y + c->bb * 2 + 2;
  return kx1 > x0 && kx0 < x1 && ky1 > y0 && ky0 < y1;
}

void chasers_step(void) {
  bool interact = event_running() || text_active() || pl.frozen || world_fading();
  for (int k = 0; k < MAX_CH; k++) {
    Chaser *c = &ch[k];
    if (!c->on || k == touched) continue;
    chaser_step(c, interact);
    if (!interact && touches_kris(c)) {
      touched = k;
      if (c->touchflag > 0) set_flag(c->touchflag, 1);
      event_start(EV_CHASE_BATTLE, NULL);
      interact = true;
    }
  }
  for (int k = 0; k < MAX_CH; k++) chaser_sync(k);
}

/* where the room draws it: mirrored in place when it faces right */
static void chaser_sync(int k) {
  Chaser *c = &ch[k];
  Obj *o = c->o;
  if (!o || (o->flags & OF_GONE)) return;
  if (!c->on) {
    obj_free(o);
    c->o = NULL;
    return;
  }
  uint16_t spr = k == touched ? c->touchspr : c->spr;
  int n = spr_frames(spr);
  o->spr = spr, o->img = (n ? (int)c->walk % n : 0) * FX, o->ispd = 0;
  o->flags = (uint16_t)((o->flags & ~OF_FLIPX) | (c->facing == 1 ? OF_FLIPX : 0));
  o->x = (int32_t)((c->x + (c->facing == 1 ? spr_w(spr) * 2 - 2 : 0)) * FX), o->y = (int32_t)(c->y * FX);
}

/* the "!" over one that saw Kris */
void chasers_draw(void) {
  for (int k = 0; k < MAX_CH; k++) {
    Chaser *c = &ch[k];
    if (c->on && c->exc_t > 0)
      gfx_sprite(spr_exc, 0, ((int)c->x - cam_x) / 2 + spr_w(c->spr) / 2, ((int)c->y - cam_y) / 2 - 10, 0, 255);
  }
}

/* touched: its picture of surprise, then the battle (obj_testoverworldenemy); after it, gone (an eraser
 * takes all of the room's with it) */
int chase_battle_event(Ev *c) {
  EV_BEGIN;
  pl.frozen = true;
  WAIT(20);
  pl.frozen = false;
  if (touched >= 0) {
    c->v[0] = ch[touched].encounter;
    if (ch[touched].eraser)
      for (int k = 0; k < MAX_CH; k++) ch[k].on = false;
    ch[touched].on = false;
  }
  touched = -1;
  for (int k = 0; k < MAX_CH; k++) chaser_sync(k);
  BATTLE(c->v[0]);
  EV_END;
}
