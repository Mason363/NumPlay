#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif
/* The Field's and the Great Board's enemies (battle.h): C. Round (obj_smallcheckers_enemy), Ponman
 * (obj_ponman_enemy), Jigsawry (obj_jigsawryenemy, obj_jigsawbullet) and K. Round (obj_checkers_enemy,
 * obj_checkers_leap). Their words, ACTs and bullets follow the original's code; angles are its degrees. */
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "battle.h"
#include "event.h"
#include "foes.h"
#include "gmtexts.h"
#include "plat.h"
#include "text.h"
#include "game.h"
#include "gfx.h"
#include "gmath.h"
#include "res.h"
#include "tables.h"

/* ---------------------------------------------------------------- shared */
static void say1(uint16_t id) { battle_say(&id, 1); }
/* the usual flavour text: one of n, then (tired), (low HP), (spareable) */
void flavor_std(Enemy *e, const uint16_t *ids, int n, uint16_t tired, uint16_t low, uint16_t spare) {
  uint16_t s = ids[rnd_n(n)];
  if (e->tired && tired != 0xFFFF) s = tired;
  if (e->hp <= e->maxhp / 3 && low != 0xFFFF) s = low;
  if (e->mercy >= 100 && spare != 0xFFFF) s = spare;
  say1(s);
}
/* a bullet moving as GameMaker's do (speed in units a frame, direction in degrees) */
Bullet *gm_bullet(uint16_t spr, float x, float y, float sp, float dir, int dmg) {
  Bullet *b = bullet_new(spr, (int)x, (int)y);
  b->upd = bullet_gm;
  b->fx = x, b->fy = y, b->fvx = lengthdir_x(sp, dir), b->fvy = lengthdir_y(sp, dir);
  b->damage = (int16_t)dmg;
  b->angle = (int16_t)(-dir * 256 / 360);
  return b;
}
static float soul_x(void) { return (float)heart_x(); }
static float soul_y(void) { return (float)heart_y(); }

/* ================================================================ C. Round, with Susie */
static void cround_talk(Enemy *e) { (void)e; }
static void cround_attack(Enemy *e, int t) {
  /* obj_spinheart, type 0 (Hathy's alone) */
  spinheart_attack(e, t);
}
static void cround_flavor(Enemy *e) {
  (void)e;
  say1(obj_smallcheckers_enemy_slash_Step_0_gml_83_0);
}
/* ACT 2 and 3: Susie strikes (or misses, warned); C. Round runs; flag 211 says how */
static int cround_after;
static bool cround_act(Enemy *e, int act) {
  if (act == 1) {
    say1(obj_smallcheckers_enemy_slash_Step_0_gml_127_0);
    return true;
  }
  if (act == 2) {
    static const int ids[] = {obj_smallcheckers_enemy_slash_Step_0_gml_136_0, F_('S', 0), obj_smallcheckers_enemy_slash_Step_0_gml_138_0,
                              obj_smallcheckers_enemy_slash_Step_0_gml_139_0, F_('R', 0), obj_smallcheckers_enemy_slash_Step_0_gml_141_0,
                              obj_smallcheckers_enemy_slash_Step_0_gml_142_0, F_('S', 0), obj_smallcheckers_enemy_slash_Step_0_gml_144_0,
                              obj_smallcheckers_enemy_slash_Step_0_gml_145_0, obj_smallcheckers_enemy_slash_Step_0_gml_146_0,
                              obj_smallcheckers_enemy_slash_Step_0_gml_147_0, -1};
    battle_say_ids(0, 0, ids);
    cround_after = 1;
    return true;
  }
  if (act == 3) {
    static const int ids[] = {obj_smallcheckers_enemy_slash_Step_0_gml_155_0, F_('S', 0), obj_smallcheckers_enemy_slash_Step_0_gml_157_0,
                              F_('R', 6), obj_smallcheckers_enemy_slash_Step_0_gml_159_0, F_('S', 0), obj_smallcheckers_enemy_slash_Step_0_gml_161_0,
                              obj_smallcheckers_enemy_slash_Step_0_gml_162_0, obj_smallcheckers_enemy_slash_Step_0_gml_163_0, F_('R', 8),
                              obj_smallcheckers_enemy_slash_Step_0_gml_165_0, F_('S', 0), obj_smallcheckers_enemy_slash_Step_0_gml_167_0,
                              obj_smallcheckers_enemy_slash_Step_0_gml_168_0, F_('R', 0), obj_smallcheckers_enemy_slash_Step_0_gml_170_0,
                              F_('S', 4), obj_smallcheckers_enemy_slash_Step_0_gml_172_0, -1};
    e->comment = str(obj_smallcheckers_enemy_slash_Step_0_gml_174_0);
    e->automiss = true;
    battle_say_ids(0, 0, ids);
    cround_after = 1;
    return true;
  }
  return false;
}
/* after the ACT's words: Susie's strike (AT 90), then 50 frames, then it is gone */
static int cround_script(Enemy *e, int ev, int arg) {
  if (ev == BS_CANMOVE) return 1;
  if (ev != BS_STEP) return 0;
  if (arg) return cround_after != 0;
  if (!cround_after || battle_text_on()) return 0;
  if (cround_after == 1) {
    int at = sv.at[CH_SUSIE];
    sv.at[CH_SUSIE] = 90;
    hero_auto_attack(CH_SUSIE, e->automiss ? 0 : 100 + rnd_n(41));
    sv.at[CH_SUSIE] = (int16_t)at;
    mercy_add(e, 200);
    cround_after = 2, e->v[7] = 0;
  } else if (cround_after == 2 && ++e->v[7] >= 50) {
    enemy_leave(e);
    e->outcome = 4;
    if (e->automiss) {
      static const int ids[] = {obj_smallcheckers_enemy_slash_Step_0_gml_224_0, F_('R', 3), obj_smallcheckers_enemy_slash_Step_0_gml_226_0,
                                obj_smallcheckers_enemy_slash_Step_0_gml_227_0, -1};
      set_flag(211, 2);
      battle_say_ids(FC_SUSIE, 0, ids);
    } else {
      static const int ids[] = {obj_smallcheckers_enemy_slash_Step_0_gml_217_0, F_('R', 3), obj_smallcheckers_enemy_slash_Step_0_gml_220_0, -1};
      set_flag(211, 1);
      battle_say_ids(FC_SUSIE, 0, ids);
    }
    cround_after = 0;
  }
  return 0;
}

/* ================================================================ Ponman */
/* its eye sweeps and shoots diamonds at the SOUL when it points at it; Goodnight and Lullaby put it to sleep */
static bool pon_lullabied;
static int ponman_alive(void) {
  int n = 0;
  for (int i = 0; i < MAX_EN; i++)
    if (en[i].alive && en[i].def && en[i].def->obj == obj_ponman_enemy && en[i].mercy < 100) n++;
  return n;
}
static void ponman_talk(Enemy *e) { (void)e; }
static void diamond_fric(Bullet *b) { bullet_gm(b); }
static void ponman_attack(Enemy *e, int t) {
  /* v: 0 shotcount, 1 totalshotcount, 2 maxshot, 3 shotbuffer, 4 eyecon; f: 0 siner, 1 spinspeed, 2 eye angle, 3 eye radius */
  if (t == 0) {
    int pt = ponman_alive();
    e->v[2] = pt == 3 ? 3 : pt == 2 ? 4 : pt == 1 ? 5 : 6;
    e->v[0] = e->v[1] = 0, e->v[3] = 8, e->v[4] = e->mercy < 100 ? 20 : 0, e->v[5] = pt;
    e->f[1] = 0;
  }
  if (e->v[6]) e->v[4] = 999;   /* asleep */
  int pt = e->v[5];
  if (e->v[4] == 20) {
    if (e->f[1] < 10) e->f[1] += 1;
    e->f[0] += pt > 1 ? e->f[1] / 8 : e->f[1] / 20;
    e->f[2] = 180 + gm_dsin(e->f[0] / 8 * 57.2958f) * 70;
    if (e->f[3] < 8) e->f[3] += 1;
    float ex = e->x + 28 + lengthdir_x(e->f[3], e->f[2]), ey = e->y + 32 + lengthdir_y(e->f[3], e->f[2]);
    float xa = point_direction(ex, ey, soul_x(), soul_y());
    if (fabsf(xa - e->f[2]) < 25 && e->f[2] >= 120 && e->f[2] <= 240 && e->v[3] < 0 && e->v[0] < 3 && e->v[1] < e->v[2]) {
      e->v[0] += pt == 1 ? 1 : pt == 2 ? 2 : 3;
      if (e->v[0] >= 3) {
        e->v[1]++, e->v[0] = 0;
        e->v[3] = pt == 1 ? 10 : pt == 2 ? 13 : 22;
      }
      Bullet *b = gm_bullet(spr_diamondbullet, ex, ey, 2, e->f[2], e->at * 5);
      b->fric = pt == 1 ? -0.12f : -0.11f, b->upd = diamond_fric, b->r = 6, b->scale = 128, b->timep = 2;
    }
    e->v[3]--;
  }
  e->f[0] += 1;
}
static void ponman_flavor(Enemy *e) {
  static const uint16_t f[5] = {obj_ponman_enemy_slash_Step_0_gml_64_0, obj_ponman_enemy_slash_Step_0_gml_65_0,
                                obj_ponman_enemy_slash_Step_0_gml_66_0, obj_ponman_enemy_slash_Step_0_gml_67_0,
                                obj_ponman_enemy_slash_Step_0_gml_68_0};
  if (e->comment && !strcmp(e->comment, str(obj_ponman_enemy_slash_Step_0_gml_72_0)) && pon_lullabied) {
    say1(obj_ponman_enemy_slash_Step_0_gml_72_1);
    return;
  }
  flavor_std(e, f, 5, obj_ponman_enemy_slash_Step_0_gml_69_0, obj_ponman_enemy_slash_Step_0_gml_70_0,
             obj_ponman_enemy_slash_Step_0_gml_71_0);
}
static bool ponman_act(Enemy *e, int act) {
  switch (act) {
    case 1: say1(obj_ponman_enemy_slash_Step_0_gml_106_0); return true;
    case 2:
      say1(obj_ponman_enemy_slash_Step_0_gml_114_0);
      e->v[6] = 1;   /* sleeping */
      mercy_add(e, 100);
      return true;
    case 3: {
      /* Lullaby: Ralsei sings; everyone sleeps (Susie too) */
      pon_lullabied = !pon_lullabied;
      static const int ids[] = {obj_ponman_enemy_slash_Step_0_gml_134_0, obj_ponman_enemy_slash_Step_0_gml_186_0, -1};
      battle_say_ids(0, 0, ids);
      for (int i = 0; i < MAX_EN; i++)
        if (en[i].alive) {
          en[i].v[6] = 1, mercy_add(&en[i], 100);
          en[i].comment = str(obj_ponman_enemy_slash_Step_0_gml_208_0), en[i].tired = true;
        }
      return true;
    }
    case 4: {
      int pop = enemies_alive();
      if (pop > 1) {
        static const uint16_t ids[2] = {obj_ponman_enemy_slash_Step_0_gml_150_0, obj_ponman_enemy_slash_Step_0_gml_152_0};
        battle_say(ids, 2);
      } else {
        say1(obj_ponman_enemy_slash_Step_0_gml_150_0);
      }
      for (int i = 0; i < MAX_EN; i++)
        if (en[i].alive) en[i].comment = str(obj_ponman_enemy_slash_Step_0_gml_155_0), en[i].automiss = true;
      return true;
    }
    default: return false;
  }
}
static void ponman_draw(Enemy *e, int x, int y) {
  int a = e->fade ? max_i(0, 255 - e->fade * 12) : 255;
  if (e->v[6]) {
    gfx_sprite(spr_ponman_appear, 5, x + e->shake / 2, y, 0, (uint8_t)a);
    return;
  }
  gfx_sprite(e->hurt_t ? spr_ponman_idle : spr_ponman_idle, e->hurt_t ? 0 : e->siner / 4, x + e->shake / 2, y, 0, (uint8_t)a);
  gfx_sprite(spr_ponman_eye, 0, x + 14 + (int)lengthdir_x(e->f[3], e->f[2]) / 2, y + 16 + (int)lengthdir_y(e->f[3], e->f[2]) / 2, 0,
             (uint8_t)a);
}

/* ================================================================ Jigsawry */
static void jigsawry_talk(Enemy *e) {
  int rr = rnd_n(4);
  static const uint16_t l[4] = {obj_jigsawryenemy_slash_Step_0_gml_11_0, obj_jigsawryenemy_slash_Step_0_gml_12_0,
                                obj_jigsawryenemy_slash_Step_0_gml_13_0, obj_jigsawryenemy_slash_Step_0_gml_14_0};
  uint16_t s = l[rr];
  if (e->mercy >= 100) s = rr < 2 ? obj_jigsawryenemy_slash_Step_0_gml_18_0 : obj_jigsawryenemy_slash_Step_0_gml_19_0;
  enemy_say(e, &s, 1);
}
/* obj_jigsawbullet: two pieces track the SOUL from opposite sides, then snap together */
static void jigsaw_upd(Bullet *b) {
  /* a: side (0 right of it, 1 below, 2 left, 3 above), b: timer, c: con, d: locked timer */
  if (b->alpha < 255 && !b->c) {
    b->alpha = (uint8_t)min_i(255, b->alpha + 26);
    if (b->alpha == 255) b->harmless = false;
  }
  float hx = soul_x(), hy = soul_y();
  if (b->c == 0) {
    if (b->a == 1 || b->a == 3) {
      if (hx - b->fx >= 10) b->fx += 3;
      if (hx - b->fx <= -10) b->fx -= 3;
    } else {
      if (hy - b->fy >= 10) b->fy += 3;
      if (hy - b->fy <= -10) b->fy -= 3;
    }
  }
  b->b++;
  if (b->b >= 30 && b->c == 0) {
    float dir = b->a * 90 + 180;
    b->fvx = lengthdir_x(4, dir), b->fvy = lengthdir_y(4, dir);
    b->gdir = (float)(b->a * 90), b->grav = 1.2f, b->c = 1;
  }
  if (b->b >= 40) b->grav = 0;
  if (b->d) {
    if (++b->d >= 12) b->alpha = (uint8_t)max_i(0, b->alpha - 51), b->harmless = true;
    if (b->d >= 17) b->on = false;
    b->x = (int32_t)(b->fx * BFX), b->y = (int32_t)(b->fy * BFX);
    return;
  }
  bullet_gm(b);
  /* meeting its other half: both stop, locked */
  for (int i = 0; i < MAX_BUL; i++) {
    Bullet *o = &bul[i];
    if (o == b || !o->on || o->upd != jigsaw_upd || o->d || o->c != 1 || b->c != 1) continue;
    if (fabsf(o->fx - b->fx) < 30 && fabsf(o->fy - b->fy) < 30) {
      b->fvx = b->fvy = o->fvx = o->fvy = 0, b->grav = o->grav = 0;
      b->d = o->d = 1;
    }
  }
}
static void jigsawry_attack(Enemy *e, int t) {
  /* obj_dbulletcontroller types 81-84: one controller for all the Jigsawries (the first makes it) */
  for (int i = 0; i < MAX_EN; i++)
    if (en[i].alive && en[i].def == e->def) {
      if (&en[i] != e) return;
      break;
    }
  int n = 0;
  for (int i = 0; i < MAX_EN; i++) n += en[i].alive && en[i].def == e->def;
  int type = 80 + n, maxtimer = type == 81 ? 30 : type == 82 ? 26 : type == 83 ? 19 : 14;
  if (t == 0) e->v[0] = 99, e->v[1] = 0;
  if (++e->v[0] < maxtimer) return;
  e->v[0] = 0;
  int side = rnd_n(2);
  if (type == 81 || type == 84) side = e->v[1], e->v[1] = !e->v[1];
  int timer0 = type == 82 ? 10 : type >= 83 ? 15 : 0;
  float hx = soul_x(), hy = soul_y();
  for (int k = 0; k < 2; k++) {
    float x = side == 0 ? hx : (k ? box_cx() - 150 : box_cx() + 150), y = side == 0 ? (k ? box_cy() + 150 : box_cy() - 150) : hy;
    Bullet *b = gm_bullet(spr_jigsawbullet, x, y, 0, 0, e->at * 5);
    b->upd = jigsaw_upd, b->angle = 0;
    b->a = (int16_t)(side == 0 ? (k ? 1 : 3) : (k ? 0 : 2));
    b->b = (int16_t)timer0, b->alpha = 0, b->harmless = true, b->r = 0, b->w = 12, b->h = 12, b->scale = 256;
    b->gp = 4, b->timep = 2;
  }
}
static void jigsawry_flavor(Enemy *e) {
  static const uint16_t f[5] = {obj_jigsawryenemy_slash_Step_0_gml_89_0, obj_jigsawryenemy_slash_Step_0_gml_90_0,
                                obj_jigsawryenemy_slash_Step_0_gml_91_0, obj_jigsawryenemy_slash_Step_0_gml_92_0,
                                obj_jigsawryenemy_slash_Step_0_gml_93_0};
  flavor_std(e, f, 5, obj_jigsawryenemy_slash_Step_0_gml_94_0, obj_jigsawryenemy_slash_Step_0_gml_95_0,
             obj_jigsawryenemy_slash_Step_0_gml_96_0);
}
static bool jigsawry_act(Enemy *e, int act) {
  switch (act) {
    case 1: say1(obj_jigsawryenemy_slash_Step_0_gml_129_0); return true;
    case 2: {
      say1(obj_jigsawryenemy_slash_Step_0_gml_136_0);
      mercy_add(e, 100);
      static const uint16_t l[4] = {obj_jigsawryenemy_slash_Step_0_gml_152_0, obj_jigsawryenemy_slash_Step_0_gml_153_0,
                                    obj_jigsawryenemy_slash_Step_0_gml_154_0, obj_jigsawryenemy_slash_Step_0_gml_155_0};
      uint16_t s = l[rnd_n(4)];
      enemy_say(e, &s, 1);
      return true;
    }
    case 3: {
      if (enemies_alive() > 1) {
        static const uint16_t ids[2] = {obj_jigsawryenemy_slash_Step_0_gml_164_0, obj_jigsawryenemy_slash_Step_0_gml_166_0};
        battle_say(ids, 2);
      } else {
        say1(obj_jigsawryenemy_slash_Step_0_gml_164_0);
      }
      for (int i = 0; i < MAX_EN; i++)
        if (en[i].alive) en[i].comment = str(obj_jigsawryenemy_slash_Step_0_gml_169_0), en[i].automiss = true;
      return true;
    }
    default: return false;
  }
}

/* ================================================================ K. Round */
/* its crown: Bow (and Ralsei's) loosen it; at 100 it falls and K. Round is C. Round again. Below 1000 HP it
 * drinks milk. Its attack is itself, leaping at the SOUL (obj_checkers_leap, four ways in turn). */
static struct {
  int crown, attacktype, milk_counter, scon, milktimer, ralsei_lecture, warned, checked;
  int bowcon, falltimer;
  /* the leap: k.x, k.y its sprite's place (units), floor, x start */
  float kx, ky, vx, vy, grav, floory, xstart, ystart, targetx;
  int jumpcon, jumptimer, amt, leapmode, s_timer, magamt;
  float siner;
  uint16_t spr;
  int img;
  int8_t body;               /* the bullet that is its hit box */
} K;

static void kround_talk(Enemy *e) {
  (void)e;
}

static int floor_y(void) { return box_cy() + 75; }

static void shrap_upd(Bullet *b) { bullet_gm(b); }
static Bullet *shrapnel(float x, float y, float sp, float dir, int dmg) {
  Bullet *b = gm_bullet(spr_checkershrapnel, x, y, sp, dir, dmg);
  b->upd = shrap_upd, b->r = 6, b->scale = 192, b->gp = 6, b->timep = 2;
  return b;
}

static void kround_body(bool on) {
  if (!on) {
    if (K.body >= 0 && K.body < MAX_BUL) bul[(int)K.body].on = false;
    K.body = -1;
    return;
  }
  Bullet *b = K.body >= 0 ? &bul[(int)K.body] : NULL;
  if (!b || !b->on) {
    b = bullet_new(0xFFFF, 0, 0);
    b->keep = true, b->w = 36, b->h = 66, b->r = 0;
    K.body = (int8_t)(b - bul);
  }
  b->x = (int32_t)((K.kx + 43) * BFX), b->y = (int32_t)((K.ky + 77) * BFX);
  b->spr = 0xFFFF;
}

static bool kround_milk(Enemy *e, int t);
static void kround_attack(Enemy *e, int t) {
  float heart = soul_x();
  int dmg = e->at * 5;
  if (t == 0) K.scon = e->hp <= (K.milk_counter > 0 ? 600 : 1000) ? 1 : 0;
  if (K.scon) {
    if (kround_milk(e, t)) battle_end_attack();
    return;
  }
  if (t == 0) {
    static const int modes[4] = {0, 3, 1, 2};
    memset(&K.kx, 0, sizeof K - offsetof(typeof(K), kx));
    K.leapmode = modes[K.attacktype];
    K.attacktype = (K.attacktype + 1) & 3;
    K.kx = K.xstart = (float)e->x, K.ky = K.ystart = (float)e->y, K.body = -1;
    K.spr = spr_checkers_crouch;
    e->hidden = true;
  }
  bool done = false;
  float fy = (float)floor_y() - 154;   /* (its sprite's top when standing on the box's floor) */
  switch (K.leapmode) {
    case 0:
      if (K.jumpcon == 1 && K.vy >= 0 && K.ky >= fy) {
        g_shake = 6;
        K.ky = fy, K.jumptimer = 0, K.vy = K.grav = K.vx = 0, K.spr = spr_checkers_crouch, K.jumpcon = 0;
      }
      if (K.jumpcon == 3 && K.vy >= 0 && K.ky >= K.floory - 2) {
        g_shake = 6;
        K.ky = K.floory, K.kx = K.xstart, K.vy = K.grav = K.vx = 0, K.spr = spr_checkers_crouch, K.jumpcon = 4;
      }
      if (K.jumpcon == 0) {
        K.spr = spr_checkers_crouch;
        int jumpmax = K.amt >= 1 ? 10 : 20;
        if (++K.jumptimer >= jumpmax) {
          K.floory = K.ky, K.jumptimer = 0, K.jumpcon = 1, K.amt++;
          K.spr = spr_checkers_leap;
          K.targetx = heart - 43;
          K.vy = K.amt == 1 ? -17 : -15, K.grav = 1, K.vx = (K.targetx - K.kx) / 28;
          if (K.amt >= 4) {
            K.jumpcon = 3, K.targetx = K.xstart, K.grav = 2, K.vx = (K.targetx - K.kx) / 16;
            K.floory = K.ystart;
          }
        }
      }
      if (K.jumpcon == 4 && ++K.jumptimer >= 10) done = true;
      break;
    case 1:
      if (K.jumpcon == 7) {
        K.jumptimer++;
        if (K.jumptimer >= 10) bullets_fade_out();
        if (K.jumptimer >= 20) done = true;
      }
      if (K.jumpcon == 6 && K.ky >= K.floory - 2) {
        K.ky = K.floory, K.kx = K.xstart, K.vy = K.grav = K.vx = 0, K.spr = spr_checkers_crouch, K.jumpcon = 7;
      }
      if (K.jumpcon == 0) {
        K.spr = spr_checkers_crouch;
        if (++K.jumptimer >= 20) {
          K.floory = K.ystart, K.jumptimer = 0, K.jumpcon = 1, K.spr = spr_checkers_leap;
          K.targetx = heart - 43, K.vx = (K.targetx - K.kx) / 17, K.vy = -17, K.grav = 1, K.amt++;
          if (K.amt >= 3) K.jumpcon = 6, K.targetx = K.xstart, K.grav = 2, K.vx = (K.targetx - K.kx) / 16;
        }
      }
      if (K.jumpcon == 1 && K.vy >= 0) K.grav = K.vy = K.vx = 0, K.jumpcon = 2;
      if (K.jumpcon == 2) {
        K.img++;
        if (++K.jumptimer >= 15) K.img = 0, K.vy = 32, K.jumpcon = 3, K.jumptimer = 0;
      }
      if (K.jumpcon == 3 && K.ky >= fy) {
        g_shake = 6;
        for (int i = 0; i < 6; i++) {
          Bullet *b = shrapnel(K.kx + 43 - 15 + i * 5, (float)floor_y(), 6 + rnd_n(100) / 100.f, 130 - rnd_n(10) - 70 * (i / 5.f), dmg);
          b->grav = 0.25f, b->gdir = 270;
        }
        K.ky = fy, K.vy = K.grav = K.vx = 0, K.spr = spr_checkers_crouch, K.jumpcon = 0;
      }
      break;
    case 2:
      /* magnificent: it rises, raining shrapnel, then crashes back in */
      if (K.jumpcon == 0) K.spr = spr_checkers_magnificent, K.img = 0, K.jumpcon = 1, K.magamt = 0;
      if (K.jumpcon == 1) {
        K.img += 1;
        if (K.img >= 8) K.img = 8, K.vx = -4, K.grav = -0.12f, K.jumpcon = 2, K.siner = 0;
      }
      if (K.jumpcon == 2) {
        K.siner++;
        K.ky += gm_dsin(K.siner / 3 * 57.2958f) * 4;
        if (++K.s_timer >= 24) {
          Bullet *b = shrapnel(K.kx + rnd_n(43), K.ky + 154 - 50, 0, 270, dmg);
          b->fvy = 3, b->gdir = (float)(135 + rnd_n(180)), b->grav = 0.06f;
          if (K.magamt == 6 || K.magamt == 12) {
            float d = point_direction(b->fx, b->fy, soul_x(), soul_y());
            b->grav = 0, b->fvx = lengthdir_x(3, d), b->fvy = lengthdir_y(3, d);
          }
          K.s_timer = 21, K.magamt++;
        }
        if (K.ky < -200) {
          K.vx = -30, K.vy = 10, K.grav = 0, K.ky = K.ystart - 100, K.kx = K.xstart + 300, K.jumpcon = 3;
        }
      }
      if (K.jumpcon == 3 && ++K.jumptimer >= 10) {
        g_shake = 6;
        K.kx = K.xstart, K.ky = K.ystart, K.vx = K.vy = 0, K.jumpcon = 4, K.jumptimer = 0, K.spr = spr_checkers_idle;
      }
      if (K.jumpcon == 4) {
        K.jumptimer++;
        if (K.jumptimer >= 20) bullets_fade_out();
        if (K.jumptimer >= 30) done = true;
      }
      break;
    default:
      /* its leg: four kicks of shrapnel at the SOUL */
      for (int i = 0; i < MAX_BUL; i++)
        if (bul[i].on && bul[i].upd == shrap_upd && bul[i].scale < 384) bul[i].scale += 1;
      if (K.jumpcon == 0) K.spr = spr_checkers_leg, K.img = 0, K.jumpcon = 1;
      if (K.jumpcon == 1) {
        K.img++;
        if (K.img == 6)
          for (int i = 0; i < 4; i++) {
            float bx = K.kx - 40, by = K.ky + 100;
            shrapnel(bx, by, 3.5f + rnd_n(180) / 100.f, point_direction(bx, by, soul_x(), soul_y()) - 10 * i + rnd_n(i * 20 + 1), dmg);
          }
        if (K.img >= 10) K.img = 10, K.jumpcon = 2;
      }
      if (K.jumpcon == 2) {
        K.jumptimer++;
        if (K.jumptimer >= 10) K.spr = spr_checkers_idle, K.img = 0;
        if (K.jumptimer >= 20) {
          K.jumptimer = 0, K.jumpcon = 0, K.amt++;
          if (K.amt >= 4) K.jumpcon = 3;
        }
      }
      if (K.jumpcon == 3) {
        K.jumptimer++;
        if (K.jumptimer >= 20) bullets_fade_out();
        if (K.jumptimer >= 30) done = true;
      }
      break;
  }
  K.vy += K.grav;
  K.kx += K.vx, K.ky += K.vy;
  kround_body(K.leapmode == 0 || K.leapmode == 1 ? K.jumpcon != 3 && K.jumpcon != 4 && K.jumpcon < 6 : false);
  if (done) {
    kround_body(false);
    e->hidden = false;
    battle_end_attack();
  }
}

static void kround_draw_front(Enemy *e) {
  if (!e->hidden || !e->alive) return;
  uint16_t s = K.spr ? K.spr : spr_checkers_idle;
  int n = spr_frames(s);
  gfx_sprite(s, n ? (K.img / 2) % n : 0, (int)K.kx / 2, (int)K.ky / 2, 0, 255);
}

static void kround_flavor(Enemy *e) {
  static char buf[96];
  if (K.crown > 0) {
    const char *f = str(obj_checkers_enemy_slash_Step_0_gml_82_0);
    int n = 0;
    for (int i = 0; f[i] && n < 90; i++) {
      if (f[i] == '~' && f[i + 1] == '1') {
        n += snprintf(buf + n, (size_t)(90 - n), "%d", K.crown);
        i++;
      } else {
        buf[n++] = f[i];
      }
    }
    buf[n] = 0;
    const char *m[1] = {buf};
    battle_say_strs(m, 1);
    return;
  }
  uint16_t s = obj_checkers_enemy_slash_Step_0_gml_77_0;
  if (e->tired) s = obj_checkers_enemy_slash_Step_0_gml_79_0;
  if (e->hp <= e->maxhp / 3) s = obj_checkers_enemy_slash_Step_0_gml_80_0;
  say1(s);
}

static int kround_bow_t;
static bool kround_act(Enemy *e, int act) {
  switch (act) {
    case 1:
      if (!K.checked) say1(obj_checkers_enemy_slash_Step_0_gml_263_0);
      else say1(obj_checkers_enemy_slash_Step_0_gml_268_0);
      set_flag(246, 1);
      K.checked = 1;
      return true;
    case 2: {
      K.crown += 15;
      say1(obj_checkers_enemy_slash_Step_0_gml_296_0);
      battle_hide_hero(CH_KRIS, true);
      K.bowcon = 20, kround_bow_t = 0;
      return true;
    }
    case 3: {
      K.crown += 20;
      static const int ids[] = {obj_checkers_enemy_slash_Step_0_gml_398_0, -1};
      battle_say_ids(FC_RALSEI, 8, ids);
      battle_hide_hero(CH_KRIS, true), battle_hide_hero(CH_RALSEI, true);
      K.bowcon = 30, kround_bow_t = 0;
      return true;
    }
    case 4: {
      static const int ids0[] = {obj_checkers_enemy_slash_Step_0_gml_531_0, obj_checkers_enemy_slash_Step_0_gml_532_0, -1};
      static const int ids1[] = {obj_checkers_enemy_slash_Step_0_gml_535_0, obj_checkers_enemy_slash_Step_0_gml_536_0, -1};
      static const int ids2[] = {obj_checkers_enemy_slash_Step_0_gml_540_0, obj_checkers_enemy_slash_Step_0_gml_541_0,
                                 obj_checkers_enemy_slash_Step_0_gml_542_0, -1};
      battle_say_ids(FC_RALSEI, 6, K.warned == 0 ? ids0 : K.warned == 1 ? ids1 : ids2);
      K.warned++;
      return true;
    }
    default: return false;
  }
}

/* its own: the bows (both bow, it bows back), the milk, the crown off */
static int kround_script(Enemy *e, int ev, int arg) {
  if (ev == BS_CANMOVE) return 1;
  if (!e->v[0]) {
    memset(&K, 0, sizeof K);
    K.body = -1;
    e->v[0] = 1;
  }
  if (ev == BS_DRAW) {
    if (K.bowcon >= 21 && K.bowcon <= 31) {
      int kx, ky, rx, ry;
      battle_hero_pos(CH_KRIS, &kx, &ky), battle_hero_pos(CH_RALSEI, &rx, &ry);
      gfx_sprite(spr_krisb_bow, 0, kx / 2, ky / 2, 0, 255);
      if (K.bowcon >= 30) gfx_sprite(spr_ralseib_bow, 0, rx / 2, ry / 2, 0, 255);
      gfx_sprite(spr_checkers_bow, kround_bow_t / 3 % max_i(1, spr_frames(spr_checkers_bow)), e->x / 2, e->y / 2, 0, 255);
    }
    if (K.scon >= 2 && K.scon <= 4) {
      int a = K.scon == 2 ? K.milktimer * 25 : K.scon == 3 ? 255 : max_i(0, 255 - K.milktimer * 25);
      gfx_sprite(spr_checkers_milk, 0, (e->x - 100 + (K.scon == 4 ? K.milktimer * K.milktimer : 0)) / 2, (e->y + 60) / 2, 0,
                 (uint8_t)clamp_i(a, 0, 255));
    }
    if (e->v[1] > 0) {
      /* the crown, knocked off (obj_checker_animtest, type 1) */
      gfx_sprite(spr_smallchecker_transform3, e->v[1] > 30, e->x / 2, e->y / 2, 0, 255);
    }
    return 0;
  }
  if (ev != BS_STEP) return 0;
  if (arg) return K.bowcon != 0 || e->v[1] > 0;
  if (K.bowcon == 20 || K.bowcon == 30) {
    if (battle_text_on()) return 0;
    e->hidden = true;
    K.bowcon++, kround_bow_t = 0;
    if (K.bowcon == 21) {
      static const int a[] = {obj_checkers_enemy_slash_Step_0_gml_327_0, -1};
      static const int b[] = {obj_checkers_enemy_slash_Step_0_gml_327_0, obj_checkers_enemy_slash_Step_0_gml_330_0, F_('R', 0),
                              obj_checkers_enemy_slash_Step_0_gml_332_0, obj_checkers_enemy_slash_Step_0_gml_333_0,
                              obj_checkers_enemy_slash_Step_0_gml_334_0, F_('S', 0), obj_checkers_enemy_slash_Step_0_gml_336_0,
                              obj_checkers_enemy_slash_Step_0_gml_337_0, F_('R', 1), obj_checkers_enemy_slash_Step_0_gml_339_0, -1};
      battle_say_ids(0, 0, K.ralsei_lecture ? a : b);
    } else {
      static const int a[] = {obj_checkers_enemy_slash_Step_0_gml_442_0, -1};
      static const int b[] = {obj_checkers_enemy_slash_Step_0_gml_442_0, obj_checkers_enemy_slash_Step_0_gml_445_0, F_('R', 0),
                              obj_checkers_enemy_slash_Step_0_gml_447_0, obj_checkers_enemy_slash_Step_0_gml_448_0,
                              obj_checkers_enemy_slash_Step_0_gml_449_0, F_('S', 0), obj_checkers_enemy_slash_Step_0_gml_451_0,
                              obj_checkers_enemy_slash_Step_0_gml_452_0, F_('R', 1), obj_checkers_enemy_slash_Step_0_gml_454_0, -1};
      battle_say_ids(FC_RALSEI, 8, K.ralsei_lecture ? a : b);
    }
    K.ralsei_lecture = 1;
    return 0;
  }
  if (K.bowcon == 21 || K.bowcon == 31) {
    kround_bow_t++;
    if (battle_text_on()) return 0;
    e->hidden = false;
    battle_hide_hero(CH_KRIS, false), battle_hide_hero(CH_RALSEI, false);
    K.bowcon = 0;
    if (K.crown >= 100) {
      mercy_add(e, 999);
      e->v[1] = 1;   /* the crown comes off */
    }
    return 0;
  }
  if (e->v[1] > 0) {
    /* it shakes the crown off, tucks its legs and rolls away: the battle is over */
    if (++e->v[1] >= 120) {
      enemy_leave(e);
      e->outcome = 4;
      e->v[1] = 0;
    }
    return 0;
  }
  return 0;
}

/* the milk (below 1000 HP, 600 after the first): "stressed out", then self-care; it heals and grows stronger */
static bool kround_milk(Enemy *e, int t) {
  if (t == 0) {
    K.milktimer = 0;
    if (K.milk_counter > 0) K.scon = 2, say1(obj_checkers_enemy_slash_Step_0_gml_129_0);
    else say1(obj_checkers_enemy_slash_Step_0_gml_119_0);
    return false;
  }
  if (K.scon == 1) {
    if (!battle_text_on()) say1(obj_checkers_enemy_slash_Step_0_gml_129_0), K.scon = 2, K.milktimer = 0;
    return false;
  }
  if (K.scon >= 2 && K.scon <= 5) {
    K.milktimer++;
    if (K.scon == 2 && K.milktimer >= 10) K.scon = 3, K.milktimer = 0;
    else if (K.scon == 3 && K.milktimer >= 25) K.scon = 4, K.milktimer = 0;
    else if (K.scon == 4) {
      if (K.milktimer == 10) {
        if (e->at < 10) e->at = (int16_t)(e->at + 1);   /* (+0.5: kept whole) */
        int heal = K.milk_counter == 0 ? 300 : 700;
        e->hp = (int16_t)min_i(e->hp + heal, 32000);
        e->dmg_show = heal, e->dmg_t = 40, e->dmg_kind = 3;
      }
      if (K.milktimer >= 15) K.scon = 5, K.milktimer = 0;
    } else if (K.scon == 5 && K.milktimer >= 30) {
      if (K.milk_counter == 0) {
        static const int ids[] = {obj_checkers_enemy_slash_Step_0_gml_226_0, F_('R', 3), obj_checkers_enemy_slash_Step_0_gml_228_0,
                                  F_('S', 3), obj_checkers_enemy_slash_Step_0_gml_230_0, -1};
        battle_say_ids(0, 0, ids);
      }
      K.milk_counter++;
      K.scon = 6;
    }
    return false;
  }
  if (K.scon == 6) {
    if (battle_text_on()) return false;
    K.scon = 0;
    return true;
  }
  return false;
}

/* ================================================================ Rabbick */
/* a dusty bunny: Blow On (press OK quickly for 90 frames) blows its dust away; its bunnies hop across the box, or
 * one jumps on top and throws carrots down */
static void rabbick_talk(Enemy *e) {
  int rr = rnd_n(4);
  if (rnd_n(50) == 0) rr = 4;
  static const uint16_t l[5] = {obj_rabbick_enemy_slash_Step_0_gml_13_0, obj_rabbick_enemy_slash_Step_0_gml_14_0,
                                obj_rabbick_enemy_slash_Step_0_gml_15_0, obj_rabbick_enemy_slash_Step_0_gml_16_0,
                                obj_rabbick_enemy_slash_Step_0_gml_17_0};
  static const uint16_t b[4] = {obj_rabbick_enemy_slash_Step_0_gml_21_0, obj_rabbick_enemy_slash_Step_0_gml_22_0,
                                obj_rabbick_enemy_slash_Step_0_gml_23_0, obj_rabbick_enemy_slash_Step_0_gml_24_0};
  uint16_t s = e->v[3] ? b[rr == 4 ? 0 : rr] : l[rr];
  enemy_say(e, &s, 1);
}
static int box_left(void) { return box_cx() - 75; }
static int box_right(void) { return box_cx() + 75; }
static int box_top(void) { return box_cy() - 75; }
static int box_bottom(void) { return box_cy() + 75; }
/* obj_rabbitbullet: hops from the right to the left */
static void rabbit_upd(Bullet *b) {
  /* f: fx, fy; a: jumpsiner, b: jumpspeed, c: jumpheight */
  b->a++;
  if (b->alpha < 255 && b->harmless && !b->d) {
    b->alpha = (uint8_t)min_i(255, b->alpha + 26);
    if (b->alpha == 255) b->harmless = false;
  }
  float bottom = (float)box_bottom() - 20, js = gm_dsin(b->a / (float)b->b * 57.2958f) * b->c;
  b->fx += b->fvx;
  b->fy = bottom + js - b->c;
  b->img = (int16_t)((js > b->c / 2.f ? 0 : js > 0 ? 2 : 1) * 16);
  if (b->fx <= box_left() - 40) {
    b->d = 1, b->harmless = true;
    b->alpha = (uint8_t)max_i(0, b->alpha - 26);
    if (!b->alpha) b->on = false;
  }
  b->x = (int32_t)(b->fx * BFX), b->y = (int32_t)(b->fy * BFX), b->vx = b->vy = 0;
}
/* obj_carrotthrower: jumps up, walks the box's top, drops carrots over the SOUL */
static void carrot_upd(Bullet *b) { bullet_gm(b); }
static void thrower_upd(Bullet *b) {
  /* a: buffer, b: dir, c: con, d: timer */
  b->a++;
  if (b->a < 19) {
    b->fvy += 1;
    bullet_gm(b);
    return;
  }
  if (b->a == 19) b->fvy = 0, b->fvx = 4.f * b->b;
  b->fy = (float)box_top();
  if (b->fx <= box_left() + 20 && b->fvx < 0) b->fx += 4, b->fvx = -b->fvx;
  if (b->fx >= box_right() - 20 && b->fvx > 0) b->fx -= 4, b->fvx = -b->fvx;
  if (b->c == 0) {
    b->fx += b->fvx;
    if (b->a >= 20 && fabsf(b->fx - soul_x()) < 30) b->c = 5, b->img = 0;
  } else if (b->c == 5) {
    b->img += 8;
    if (b->img >= 32 && !b->d) {
      b->d = 1;
      int pop = enemies_alive();
      Bullet *c = gm_bullet(spr_carrotbullet, b->fx, b->fy, (float)(pop >= 3 ? 5 : pop == 2 ? 6 : 7), 270, b->damage);
      c->upd = carrot_upd, c->r = 6, c->scale = 256, c->ispd = 4, c->angle = 0, c->gp = 9, c->timep = 3;
    }
    if (b->img >= 48) {
      int pop = enemies_alive();
      b->c = 7, b->d = pop >= 3 ? 30 : pop == 2 ? 22 : 15, b->img = 0;
    }
  } else if (b->c == 7) {
    if (--b->d <= 0) b->c = 0, b->d = 0;
  }
  b->x = (int32_t)(b->fx * BFX), b->y = (int32_t)(b->fy * BFX), b->vx = b->vy = 0;
}
static bool rabbit_thrower_exists(void) {
  for (int i = 0; i < MAX_BUL; i++)
    if (bul[i].on && bul[i].upd == thrower_upd) return true;
  return false;
}
static void rabbick_attack(Enemy *e, int t) {
  int pop = enemies_alive(), bmax = pop >= 3 ? 60 : pop == 2 ? 46 : 34;
  if (t == 0) {
    e->v[4] = rnd_n(2) ? 32 : 30, e->v[5] = 99;
    if (e->v[4] == 32 && rabbit_thrower_exists()) e->v[4] = 30;
    if (e->v[4] == 32) {
      Bullet *b = gm_bullet(spr_rabbitthrower, (float)box_cx(), (float)box_cy(), 0, 0, e->at * 5);
      b->upd = thrower_upd, b->b = (int16_t)(rnd_n(2) ? 1 : -1), b->fvy = -14, b->fvx = b->b * rnd_n(200) / 100.f;
      b->harmless = false, b->r = 10, b->angle = 0, b->gp = 9, b->timep = 3;
      return;
    }
  }
  if (e->v[4] != 30) return;
  if (++e->v[5] < bmax) return;
  e->v[5] = 0;
  Bullet *b = gm_bullet(spr_rabbitbullet, (float)box_right(), (float)box_cy(), 0, 0, e->at * 5);
  b->upd = rabbit_upd, b->fvx = -3 - rnd_n(100) / 100.f, b->a = (int16_t)rnd_n(100), b->b = (int16_t)(8 + rnd_n(4));
  b->c = (int16_t)(50 + rnd_n(10)), b->alpha = 0, b->harmless = true, b->r = 10, b->angle = 0, b->gp = 9, b->timep = 3;
}
static void rabbick_flavor(Enemy *e) {
  static const uint16_t f[4] = {obj_rabbick_enemy_slash_Step_0_gml_79_0, obj_rabbick_enemy_slash_Step_0_gml_80_0,
                                obj_rabbick_enemy_slash_Step_0_gml_81_0, obj_rabbick_enemy_slash_Step_0_gml_82_0};
  flavor_std(e, f, 4, 0xFFFF, obj_rabbick_enemy_slash_Step_0_gml_83_0, obj_rabbick_enemy_slash_Step_0_gml_84_0);
}
/* v: 0 blowing (frames left), 1 blowamt, 2 blowanimtimer, 3 blown, 6 blow all, 7 after the text: 1 spare it, 2 all */
static char blow_text[80];
static void blow_say(uint16_t id) {
  txt_subst_into(blow_text, sizeof blow_text, str(id), "OK");
  const char *m[1] = {blow_text};
  battle_say_strs(m, 1);
}
static bool rabbick_act(Enemy *e, int act) {
  switch (act) {
    case 1: say1(obj_rabbick_enemy_slash_Step_0_gml_118_0); return true;
    case 2:
      if (e->v[3]) {
        say1(obj_rabbick_enemy_slash_Step_0_gml_138_0);
        e->v[7] = 1;
        return true;
      }
      blow_say(obj_rabbick_enemy_slash_Step_0_gml_128_0);
      e->v[0] = 90, e->v[6] = 0;
      battle_text_hold(true);
      return true;
    case 3: {
      int left = 0;
      for (int i = 0; i < MAX_EN; i++)
        if (en[i].alive && en[i].def == e->def && !en[i].v[3]) en[i].v[0] = 90, en[i].v[6] = 1, left++;
      if (!left) {
        say1(obj_rabbick_enemy_slash_Step_0_gml_175_0);
        e->v[7] = 2;
        return true;
      }
      blow_say(obj_rabbick_enemy_slash_Step_0_gml_150_0);
      battle_text_hold(true);
      return true;
    }
    case 4: {
      if (enemies_alive() > 1) {
        static const uint16_t ids[2] = {obj_rabbick_enemy_slash_Step_0_gml_186_0, obj_rabbick_enemy_slash_Step_0_gml_188_0};
        battle_say(ids, 2);
      } else {
        say1(obj_rabbick_enemy_slash_Step_0_gml_186_0);
      }
      for (int i = 0; i < MAX_EN; i++)
        if (en[i].alive) en[i].comment = str(obj_rabbick_enemy_slash_Step_0_gml_191_0), en[i].automiss = true;
      return true;
    }
    default: return false;
  }
}
static int rabbick_script(Enemy *e, int ev, int arg) {
  if (ev == BS_CANMOVE) return 1;
  if (ev != BS_STEP) return 0;
  if (arg) return e->v[0] > 0 || e->v[7] != 0;
  if (e->v[7] && !battle_text_on()) {
    if (e->v[7] == 2) {
      for (int i = 0; i < MAX_EN; i++)
        if (en[i].alive && en[i].def == e->def) enemy_spare(&en[i]);
    } else {
      enemy_spare(e);
    }
    e->v[7] = 0;
  }
  if (e->v[0] <= 0) return 0;
  /* blowing: OK quickly; the dust shrinks */
  if (--e->v[2] <= 0 && e->v[1] > 0) e->v[1]--;
  if (g_pressed & K_Z) e->v[1] += 12, e->v[2] = 20, e->shake = 5;
  if (--e->v[0] <= 0 || e->v[1] >= 100) {
    if (e->v[1] >= 100 && !e->v[3]) mercy_add(e, 100), e->v[3] = 1, e->v[1] = 0;
    e->v[0] = 0;
    bool any = false;
    for (int i = 0; i < MAX_EN; i++)
      if (en[i].alive && en[i].def == e->def && en[i].v[0] > 0) any = true;
    if (!any) {
      battle_text_hold(false);
      battle_end_text();
    }
  }
  return 0;
}
static void rabbick_draw(Enemy *e, int x, int y) {
  int a = e->fade ? max_i(0, 255 - e->fade * 12) : 255;
  int sc = e->v[3] ? 256 : 256 - e->v[1] * 128 / 100;   /* (image_xscale 2 - blowamt / 100) */
  uint16_t spr = e->hurt_t ? spr_rabbick_enemy_hurt : (e->mercy >= 100 || e->v[3]) ? spr_rabbick_enemy_spared : spr_rabbick_enemy;
  if (sc != 256) gfx_sprite_ex(spr, 0, x + e->shake / 2 + (37 - 37 * sc / 256) / 2, y, sc, 256, 0, 0, (uint8_t)a);
  else gfx_sprite(spr, e->hurt_t ? 0 : e->siner / 5, x + e->shake / 2, y, 0, (uint8_t)a);
}

/* ================================================================ the table */
const EnemyDef foes_defs[] = {
  {obj_smallcheckers_enemy, spr_smallchecker_idle, spr_smallchecker_idle, spr_smallchecker_idle, 4, cround_talk, cround_attack,
   cround_flavor, cround_act, NULL, 140, NULL, 0, 0, 0, cround_script, 0},
  {obj_ponman_enemy, spr_ponman_idle, spr_ponman_idle, spr_ponman_idle, 4, ponman_talk, ponman_attack, ponman_flavor, ponman_act,
   ponman_draw, 150, NULL, 0, 0, 0, NULL, 0},
  {obj_jigsawryenemy, spr_jigsawry_idle, spr_jigsawry_hurt, spr_jigsawry_idle, 5, jigsawry_talk, jigsawry_attack, jigsawry_flavor,
   jigsawry_act, NULL, 140, NULL, 0, 0, 0, NULL, 0},
  {obj_rabbick_enemy, spr_rabbick_enemy, spr_rabbick_enemy_hurt, spr_rabbick_enemy_spared, 5, rabbick_talk, rabbick_attack,
   rabbick_flavor, rabbick_act, rabbick_draw, 170, NULL, 0, 0, 0, rabbick_script, 0},
  {obj_checkers_enemy, spr_checkers_idle, spr_checkers_idle, spr_checkers_idle, 3, kround_talk, kround_attack, kround_flavor,
   kround_act, NULL, 999, kround_draw_front, 0, 0, 0, kround_script, 0},
};
const int foes_count = (int)(sizeof foes_defs / sizeof foes_defs[0]);
