#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif
/* The enemies (battle.h): each the original's monster object, its words, its ACTs and its bullets
 * (obj_diamondenemy and obj_dbulletcontroller, obj_heartenemy and obj_heartshaper...). Angles are the
 * original's degrees (counterclockwise), speeds its units a frame. */
#include <string.h>
#include "battle.h"
#include "game.h"
#include "gfx.h"
#include "res.h"
#include "foes.h"
#include "tables.h"

/* GameMaker's lengthdir_x / lengthdir_y, in 1/16 unit */
static int ldx(int len16, int deg) { return len16 * gfx_cos(deg * 256 / 360) / 1024; }
static int ldy(int len16, int deg) { return -len16 * gfx_sin(deg * 256 / 360) / 1024; }
static int angle_of(int deg) { return -deg * 256 / 360; }
/* point_direction, in degrees */
static int dir_to(int x0, int y0, int x1, int y1) {
  int dx = x1 - x0, dy = y0 - y1;
  /* atan2 by the octants (good to a degree) */
  int ax = abs_i(dx), ay = abs_i(dy);
  if (!ax && !ay) return 0;
  int a = ax >= ay ? (ay * 45 + ax / 2) / ax : 90 - (ax * 45 + ay / 2) / ay;
  /* (a small correction: atan isn't linear) */
  int t = ax >= ay ? ay * 1000 / ax : ax * 1000 / ay;
  int corr = t * (1000 - t) / 4000 * 15 / 100;
  a = ax >= ay ? a + corr / 10 : a - corr / 10;
  if (dx < 0) a = 180 - a;
  if (dy < 0) a = 360 - a;
  return (a % 360 + 360) % 360;
}

static int pick4(int a, int b, int c, int d) {
  int r = rnd_n(4);
  return r == 0 ? a : r == 1 ? b : r == 2 ? c : d;
}

/* ================================================================ Rudinn (obj_diamondenemy) */
static void rudinn_talk(Enemy *e) {
  static const uint16_t lines[4] = {obj_diamondenemy_slash_Step_0_gml_11_0, obj_diamondenemy_slash_Step_0_gml_12_0,
                                    obj_diamondenemy_slash_Step_0_gml_13_0, obj_diamondenemy_slash_Step_0_gml_14_0};
  uint16_t s = lines[rnd_n(4)];
  if (e->acting == 2) s = obj_diamondenemy_slash_Step_0_gml_24_0;
  if (e->acting == 3) s = obj_diamondenemy_slash_Step_0_gml_28_0;
  enemy_say(e, &s, 1);
}

/* obj_dbulletcontroller, types 0 and 1: diamonds forming around the SOUL, then flying at it */
static int ratio16(void) { int n = enemies_alive(); return n >= 3 ? 37 : n == 2 ? 26 : 16; }
static void diamond_controller(Enemy *e, int t, int type) {
  int dmg = e->at * 5;
  if (t == 0) e->v[0] = 99, e->v[1] = 1;
  e->v[0]++;
  int hx = heart_x(), hy = heart_y();
  if (type == 0 && e->v[0] * 16 >= 12 * ratio16()) {
    e->v[0] = 0;
    int dir = 30 + rnd_n(120), radius = 140 + rnd_n(80);
    int x = hx + ldx(radius * 16, dir) / 16, y = hy + ldy(radius * 16, dir) / 16;
    if (y < 40) y = 40;
    Bullet *b = bullet_new(spr_diamondbullet_form, x, y);
    b->damage = (int16_t)dmg, b->a = 0, b->alpha = 0, b->harmless = true;
    b->d = 1;   /* a maker */
  }
  if (type == 1 && e->v[0] * 16 >= 9 * ratio16()) {
    e->v[0] = 0;
    int radius = 140 + rnd_n(40), side = e->v[1];
    int xx = -100 + rnd_n(200);
    if (rnd_n(4) == 3) xx = -10 + rnd_n(20);
    int y = clamp_i(hy + radius * side, 20, 460);
    Bullet *b = bullet_new(spr_diamondbullet_vert, hx + xx, y);
    b->damage = (int16_t)dmg, b->alpha = 0, b->harmless = true;
    b->d = 2;   /* a vertical one */
  }
  /* the makers: fade in, then shoot one diamond at the SOUL */
  for (int i = 0; i < MAX_BUL; i++) {
    Bullet *b = &bul[i];
    if (!b->on || !b->d) continue;
    int bx = b->x / BFX, by = b->y / BFX;
    if (b->d == 1) {
      int dir = dir_to(bx, by, hx, hy);
      b->angle = (int16_t)angle_of(dir);
      if (b->alpha < 250) {
        b->alpha = (uint8_t)min_i(255, b->alpha + 26);
        b->scale = (int16_t)((3 * 255 - 2 * b->alpha) * 128 / 255);   /* (3 - alpha * 2, the original's scale) */
        /* (move_towards_point 2 while forming) */
        b->x += ldx(2 * 16, dir), b->y += ldy(2 * 16, dir);
      } else if (!b->c) {
        b->c = 1, b->t = 0, b->scale = 128;
        b->b = (int16_t)dir;
      } else if (b->t >= 5 && b->c == 1) {
        Bullet *s = bullet_new(spr_diamondbullet, bx, by);
        s->damage = b->damage, s->r = 8, s->scale = 128;
        s->vx = ldx(6 * 16, b->b), s->vy = ldy(6 * 16, b->b);
        s->angle = (int16_t)angle_of(b->b);
        b->c = 2, b->t = 0;
      } else if (b->c == 2) {
        b->alpha = (uint8_t)max_i(0, b->alpha - 51);
        if (!b->alpha) b->on = false;
      }
    } else if (b->d == 2) {
      if (b->alpha < 250) {
        b->alpha = (uint8_t)min_i(255, b->alpha + 26);
        b->scale = (int16_t)((3 * 255 - 2 * b->alpha) * 128 / 255);
        if (b->alpha >= 250) {
          b->scale = 128, b->harmless = false, b->r = 8;
          if (hy < by) b->vy = 1 * 16, b->ay = -16 / 5;
          else b->vy = -2 * 16, b->ay = 16;
        }
      } else {
        if (b->vy > 8 * 16) b->vy = 8 * 16;
        if (b->vy < -8 * 16) b->vy = -8 * 16;
      }
    }
  }
  if (type == 1 && t > 0 && t % 60 == 0) e->v[1] = -e->v[1];
}

static void rudinn_attack(Enemy *e, int t) {
  if (t == 0) e->v[2] = rnd_n(2);
  if (t < 12) return;
  diamond_controller(e, t - 12, e->v[2]);
}

static void rudinn_flavor(Enemy *e) {
  static const uint16_t f[5] = {obj_diamondenemy_slash_Step_0_gml_77_0, obj_diamondenemy_slash_Step_0_gml_78_0,
                                obj_diamondenemy_slash_Step_0_gml_79_0, obj_diamondenemy_slash_Step_0_gml_80_0,
                                obj_diamondenemy_slash_Step_0_gml_81_0};
  uint16_t s = f[rnd_n(5)];
  if (e->tired) s = obj_diamondenemy_slash_Step_0_gml_82_0;
  if (e->hp <= e->maxhp / 3) s = obj_diamondenemy_slash_Step_0_gml_83_0;
  if (e->mercy >= 100) s = obj_diamondenemy_slash_Step_0_gml_84_0;
  battle_say(&s, 1);
}

static bool rudinn_act(Enemy *e, int act) {
  switch (act) {
    case 1: {
      uint16_t s = obj_diamondenemy_slash_Step_0_gml_154_0;
      battle_say(&s, 1);
      return true;
    }
    case 2: {
      uint16_t s = obj_diamondenemy_slash_Step_0_gml_163_0;
      set_flag(208, 1);
      mercy_add(e, 100);
      battle_say(&s, 1);
      return true;
    }
    case 3: {
      static const uint16_t s[2] = {obj_diamondenemy_slash_Step_0_gml_173_0, obj_diamondenemy_slash_Step_0_gml_174_0};
      for (int i = 0; i < MAX_EN; i++)
        if (en[i].alive) en[i].tired = true, en[i].comment = str(obj_diamondenemy_slash_Step_0_gml_177_0);
      mercy_add(e, 50);
      battle_say(s, 2);
      return true;
    }
    case 4: {
      uint16_t s = enemies_alive() > 1 ? obj_diamondenemy_slash_Step_0_gml_190_0 : obj_diamondenemy_slash_Step_0_gml_188_0;
      for (int i = 0; i < MAX_EN; i++)
        if (en[i].alive) en[i].comment = str(obj_diamondenemy_slash_Step_0_gml_193_0);
      battle_say(&s, 1);
      return true;
    }
    default: return false;
  }
}

/* ================================================================ Hathy (obj_heartenemy) */
static void hathy_talk(Enemy *e) {
  /* its bubble is a picture (obj_heartblcon): a heart, or what the ACT made it feel */
  e->pic = e->acting == 2 ? spr_heartblcon_2 : e->acting == 3 ? spr_heartblcon_3
                                                              : (rnd_n(2) ? spr_heartblcon_1 : spr_heartblcon_0);
}

/* obj_heartshaper: 16 hearts in a ring closing on the SOUL; obj_spinheart: one big heart that spins, then
 * flies the way it points */
static void hathy_attack(Enemy *e, int t) {
  if (t < 12) return;
  t -= 12;
  int dmg = e->at * 5;
  if (enemies_alive() == 1) {
    /* obj_spinheart */
    if (t == 0) {
      Bullet *b = bullet_new(spr_spinheart, heart_x(), heart_y());
      b->damage = (int16_t)dmg, b->alpha = 0, b->scale = 128 * 4, b->harmless = true, b->r = 10;
      b->angle = (int16_t)angle_of(-90), b->d = 3;
      e->v[0] = 0;
    }
    for (int i = 0; i < MAX_BUL; i++) {
      Bullet *b = &bul[i];
      if (!b->on || b->d != 3) continue;
      int deg = -b->angle * 360 / 256;
      switch (b->c) {
        case 0:
          b->alpha = (uint8_t)min_i(255, b->alpha + 51);
          b->scale = (int16_t)(b->scale - 128 / 5);   /* (4, less 0.2 a frame: 3) */
          if (b->t >= 5) b->c = 1, b->t = 0, b->harmless = false, b->scale = 128 * 3;
          break;
        case 1:
          if (b->t >= 10) b->c = 2, b->t = 0, b->b = (int16_t)pick4(26, 30, 34, 38);
          break;
        case 2:
          b->angle = (int16_t)angle_of(deg + 24);
          if (b->t >= b->b) b->c = 3, b->t = 0;
          break;
        case 3:
          if (b->t >= 19) {
            b->vx = ldx(40, deg), b->vy = ldy(40, deg);
            b->ax = ldx(8, deg), b->ay = ldy(8, deg);
            b->c = 4, b->t = 0;
          }
          break;
        default:
          if (b->t >= 10) b->ax = b->ay = 0, b->vx = b->vy = 0;
          if (b->t >= 20) b->harmless = true, b->alpha = (uint8_t)max_i(0, b->alpha - 51);
          if (b->t >= 25) b->on = false;
          break;
      }
    }
  } else {
    /* obj_heartshaper, type 0 */
    static int cx, cy;
    if (t == 0) {
      cx = heart_x(), cy = heart_y();
      e->v[0] = 160, e->v[1] = 0;
      for (int k = 0; k < 16; k++) {
        Bullet *b = bullet_new(spr_heartbullet, -20, -20);
        b->damage = (int16_t)dmg, b->alpha = 0, b->d = 4, b->a = (int16_t)k, b->r = 6, b->scale = 128;
        b->gp = 2, b->timep = 1;
      }
    }
    if (e->v[0] > 80) e->v[0] -= 5;
    e->v[1] += 3;   /* siner, x2 */
    for (int i = 0; i < MAX_BUL; i++) {
      Bullet *b = &bul[i];
      if (!b->on || b->d != 4) continue;
      if (b->alpha < 255) b->alpha = (uint8_t)min_i(255, b->alpha + 26);
      /* sin(pi t / 8 + siner / 20) */
      int a = b->a * 16 + e->v[1] * 256 / (2 * 20 * 2 * 3);
      b->x = (cx + e->v[0] * gfx_sin(a) / 1024) * BFX;
      b->y = (cy - e->v[0] * gfx_cos(a) / 1024) * BFX;
    }
  }
}

static void hathy_flavor(Enemy *e) {
  static const uint16_t f[5] = {obj_heartenemy_slash_Step_0_gml_79_0, obj_heartenemy_slash_Step_0_gml_80_0,
                                obj_heartenemy_slash_Step_0_gml_81_0, obj_heartenemy_slash_Step_0_gml_82_0,
                                obj_heartenemy_slash_Step_0_gml_83_0};
  uint16_t s = f[rnd_n(5)];
  if (e->tired) s = obj_heartenemy_slash_Step_0_gml_84_0;
  if (e->hp <= e->maxhp / 3) s = obj_heartenemy_slash_Step_0_gml_85_0;
  if (e->mercy >= 100) s = obj_heartenemy_slash_Step_0_gml_86_0;
  battle_say(&s, 1);
}

static bool hathy_act(Enemy *e, int act) {
  switch (act) {
    case 1: {
      uint16_t s = obj_heartenemy_slash_Step_0_gml_127_0;
      battle_say(&s, 1);
      return true;
    }
    case 2: {
      static const uint16_t f[3] = {obj_heartenemy_slash_Step_0_gml_136_0, obj_heartenemy_slash_Step_0_gml_137_0,
                                    obj_heartenemy_slash_Step_0_gml_138_0};
      uint16_t s = f[rnd_n(3)];
      mercy_add(e, 100);
      battle_say(&s, 1);
      return true;
    }
    case 3: {
      /* X-Flatter, with Ralsei: everyone's mercy full */
      static const char *m[3];
      static const uint16_t f[3] = {obj_heartenemy_slash_Step_0_gml_150_0, obj_heartenemy_slash_Step_0_gml_151_0,
                                    obj_heartenemy_slash_Step_0_gml_152_0};
      m[0] = str(obj_heartenemy_slash_Step_0_gml_147_0), m[1] = "\\F0\\E0\\FR%", m[2] = str(f[rnd_n(3)]);
      for (int i = 0; i < MAX_EN; i++)
        if (en[i].alive) mercy_add(&en[i], 100);
      battle_say_strs(m, 3);
      return true;
    }
    default: return false;
  }
}

static void hathy_draw(Enemy *e, int x, int y) {
  uint16_t spr = e->hurt_t ? spr_heartenemy_hurt : e->mercy >= 100 ? spr_heartenemy_spared : spr_heartenemy_idle;
  gfx_sprite(spr, e->siner / 6, x + e->shake / 2, y, 0, e->fade ? (uint8_t)max_i(0, 255 - e->fade * 12) : 255);
}

/* ================================================================ Lancer, the first time (obj_lancerboss) */
static void lancer_talk(Enemy *e) {
  static const uint16_t lines[4] = {obj_lancerboss_slash_Step_0_gml_17_0, obj_lancerboss_slash_Step_0_gml_18_0,
                                    obj_lancerboss_slash_Step_0_gml_19_0, obj_lancerboss_slash_Step_0_gml_20_0};
  uint16_t s = lines[min_i(e->turns, 3)];
  if (e->v[6]) {   /* compliment_just */
    s = e->v[7] == 1 ? obj_lancerboss_slash_Step_0_gml_26_0 : e->v[7] == 2 ? obj_lancerboss_slash_Step_0_gml_30_0
                                                                           : obj_lancerboss_slash_Step_0_gml_34_0;
    e->v[6] = 0;
  }
  enemy_say(e, &s, 1);
}

/* obj_lancerbike: the bike itself is the bullet. A race (up and down, two honks, a dash across the screen
 * and back from the other side) or a lap round the screen's edges shooting spades at the SOUL. */
static struct {
  int32_t x, y, hs, vs;        /* its origin (the wheels), 1/16 unit */
  int sp;                      /* speed (lap) */
  int ang16;                   /* turned clockwise this far (image_angle = -ang), 1/16 degree */
  int ysc;                     /* image_yscale, 1024 = 1 (2: as drawn) */
  int lcon, racecon, ltimer, shrink, btimer, rtimer, maxr, frame;
  int orx, ory, dmg;
  uint16_t spr;
  int8_t box;                  /* the bullet that is its hit box */
} bike;

/* sin of num/den radians, in 1/1024 */
static int sin_rad(int num, int den) { return gfx_sin((int)((int64_t)num * 256 * 1000 / ((int64_t)den * 6283))); }

static int bike_dmg(void) {
  int h = sv.hp[CH_KRIS], m = sv.maxhp[CH_KRIS];
  return h <= m / 2 ? (h + 2) / 3 : bike.dmg;
}

static void lancer_attack(Enemy *e, int t) {
  if (t == 0) {
    memset(&bike, 0, sizeof bike);
    bike.x = e->x * BFX, bike.y = e->y * BFX, bike.ysc = 2048, bike.spr = spr_lancerbike;
    bike.orx = e->x, bike.ory = e->y;
    bike.dmg = sv.hp[CH_KRIS] <= sv.maxhp[CH_KRIS] / 4 ? 5 : sv.hp[CH_KRIS] <= sv.maxhp[CH_KRIS] / 2 ? 10 : 20;
    if (e->turns % 2 == 0) {
      bike.racecon = 2, bike.vs = -14 * BFX * (rnd_n(2) ? 1 : -1), bike.maxr = 15 + rnd_n(25);
    } else {
      bike.lcon = 1;
    }
    Bullet *b = bullet_new(0xFFFF, e->x, e->y);
    b->keep = true, b->w = 25, b->h = 22, b->gp = 10, b->timep = 0;
    bike.box = (int8_t)(b - bul);
    e->v[4] = 1;   /* (Lancer is on the bike: not drawn where he stands) */
  }
  int x = bike.x / BFX, y = bike.y / BFX;
  bool end = false;
  /* the lap: spades every 10 frames, from the bike's front */
  if (bike.lcon >= 6 && bike.lcon < 10 && ++bike.btimer >= 10) {
    int a = -bike.ang16 / 16 - 20;
    Bullet *s = bullet_new(spr_spadebullet, x - ldx(40 * 16, a) / 16, y - ldy(40 * 16, a) / 16);
    int d = dir_to(s->x / BFX, s->y / BFX, heart_x(), heart_y());
    s->vx = ldx(4 * 16, d), s->vy = ldy(4 * 16, d);
    s->ax = ldx(64, d) / 10, s->ay = ldy(64, d) / 10;   /* (friction -0.4) */
    s->angle = (int16_t)angle_of(d), s->damage = (int16_t)bike_dmg(), s->r = 10, s->timep = 0, s->scale = 128;
    bike.btimer = 0;
  }
  switch (bike.lcon) {
    case 1:
      if (++bike.ltimer >= 10) bike.lcon = 2, bike.ltimer = 0;
      break;
    case 2: {   /* the wobble before it goes */
      bike.ltimer++, bike.shrink++;
      int lt = bike.ltimer, s5 = sin_rad(lt, 5);
      bike.hs = 5 * BFX * sin_rad(lt, 3) / 1024;
      bike.ysc = bike.shrink > 4 ? 2048 - s5 * 8 / bike.shrink : 2048 - s5;
      if (lt > 7) bike.ang16 += abs_i(s5) * 64 / 1024;
      if (lt > 4 && abs_i(s5) <= 61) bike.lcon = 5, bike.ysc = 2048, bike.sp = 4, bike.hs = -4 * BFX;
      break;
    }
    case 5:
      if (bike.sp < 16) bike.sp += 2;
      if (bike.ang16 < 45 * 16) bike.ang16 += 4 * 16;
      bike.hs = -bike.sp * BFX;
      if (x < 80) bike.lcon = 6;
      break;
    case 6:
      if (bike.ang16 < 135 * 16) bike.ang16 += 10 * 16;
      if (x <= 5) bike.lcon = 7, bike.hs = 0, bike.vs = -bike.sp * BFX;
      break;
    case 7:
      if (y < 80) {
        if (bike.ang16 < 225 * 16) bike.ang16 += 10 * 16;
        if (y <= 5) bike.lcon = 8, bike.vs = 0, bike.hs = bike.sp * BFX;
      } else if (bike.ang16 < 135 * 16) {
        bike.ang16 += 10 * 16;
      }
      break;
    case 8:
      if (x >= 540) {
        if (bike.ang16 < 315 * 16) bike.ang16 += 10 * 16;
        if (x >= 630) bike.lcon = 9, bike.hs = 0, bike.vs = bike.sp * BFX;
      } else if (bike.ang16 < 225 * 16) {
        bike.ang16 += 10 * 16;
      }
      break;
    case 9:
      if (y > bike.ory - 70) {
        if (bike.ang16 < 360 * 16) bike.ang16 += 10 * 16;
        if (y >= bike.ory - 5) bike.y = bike.ory * BFX, bike.lcon = 10, bike.vs = 0, bike.hs = -bike.sp * BFX;
      } else if (bike.ang16 < 315 * 16) {
        bike.ang16 += 10 * 16;
      }
      break;
    case 10:
      bike.ang16 = 0;
      if (x <= bike.orx) bike.hs = bike.vs = 0, bike.x = bike.orx * BFX, bike.lcon = 11, bike.ltimer = 0;
      break;
    case 11:
      bike.spr = spr_lancerbike_l;
      bike.ltimer++;
      if (bike.ltimer >= 25)
        for (int i = 0; i < MAX_BUL; i++)
          if (bul[i].on && bul[i].spr == spr_spadebullet) bul[i].harmless = true, bul[i].alpha = (uint8_t)max_i(0, bul[i].alpha - 51);
      if (bike.ltimer >= 30) end = true;
      break;
    default: break;
  }
  switch (bike.racecon) {
    case 2:   /* up and down between the box's top and bottom */
      if (y < 120 + 10) bike.vs = 12 * BFX;
      if (y > 250 - 10) bike.vs = -12 * BFX;
      if (++bike.rtimer > bike.maxr) bike.vs = 0, bike.racecon = 3, bike.rtimer = 0;
      break;
    case 3:   /* honk, honk */
      bike.rtimer++;
      if (bike.rtimer == 5 || bike.rtimer == 10) {
        Bullet *h = bullet_new(spr_lancernoise, x - 60, y - 40);
        h->harmless = true, h->scale = 128, h->d = 3;
      }
      if (bike.rtimer >= 25) bike.racecon = 4, bike.hs = -20 * BFX, bike.rtimer = 0, bike.ang16 = 0;
      break;
    case 4:   /* across, leaning back, and in again from the right */
      bike.rtimer++;
      bike.ang16 = min_i(50 * 16, bike.ang16 + (bike.rtimer * 2 + 4) * 16);
      if (x <= -40) bike.ang16 = 0, bike.x = 740 * BFX, bike.y = bike.ory * BFX, bike.hs = -12 * BFX, bike.racecon = 5;
      break;
    case 5:
      if (x <= bike.orx + 5) bike.hs = 0, bike.x = bike.orx * BFX, bike.racecon = 0, end = true;
      break;
    default: break;
  }
  bike.x += bike.hs, bike.y += bike.vs;
  bike.frame++;
  /* the honks grow and fade (obj_afterimage_grow) */
  for (int i = 0; i < MAX_BUL; i++)
    if (bul[i].on && bul[i].d == 3 && bul[i].spr == spr_lancernoise) {
      bul[i].scale = (int16_t)(bul[i].scale + 26);
      if (bul[i].alpha <= 26) bul[i].on = false;
      else bul[i].alpha = (uint8_t)(bul[i].alpha - 26);
    }
  /* its hit box: the sprite's (x 5-30, y 20-42 of 61 x 46, origin 28, 44; drawn at 2), turned with it */
  Bullet *b = &bul[bike.box];
  int ga = bike.ang16 * 256 / (360 * 16);
  int cx = -21, cy = -26 * bike.ysc / 2048;
  int ca = gfx_cos(ga), sa = gfx_sin(ga);
  b->x = bike.x + (cx * ca - cy * sa) * BFX / 1024, b->y = bike.y + (cx * sa + cy * ca) * BFX / 1024;
  b->angle = (int16_t)ga, b->h = (int16_t)max_i(4, 22 * bike.ysc / 2048), b->damage = (int16_t)bike_dmg();
  b->on = !end;
  if (end) {
    e->v[4] = 0;
    e->turns++;
    if (e->turns >= 4) e->v[5] = 1;   /* out of fuel */
    for (int i = 0; i < MAX_BUL; i++) bul[i].on = false;
    battle_end_attack();
  }
}

/* the bike over the box (bullets' depth) */
static void lancer_front(Enemy *e) {
  if (!e->v[4]) return;
  int ga = bike.ang16 * 256 / (360 * 16);
  gfx_sprite_ex(bike.spr, bike.frame % 6, bike.x / BFX / 2, bike.y / BFX / 2, 256, 256 * bike.ysc / 2048, ga, 0, 255);
}

static void lancer_flavor(Enemy *e) {
  static const uint16_t f[5] = {obj_lancerboss_slash_Step_0_gml_90_0, obj_lancerboss_slash_Step_0_gml_91_0,
                                obj_lancerboss_slash_Step_0_gml_92_0, obj_lancerboss_slash_Step_0_gml_93_0,
                                obj_lancerboss_slash_Step_0_gml_94_0};
  uint16_t s = f[rnd_n(5)];
  if (e->turns == 1) {
    s = obj_lancerboss_slash_Step_0_gml_100_0;
    battle_face(FC_SUSIE, 2);
  }
  battle_say(&s, 1);
  if (e->v[5]) {
    /* out of fuel: he says his goodbye and rides off */
    static const uint16_t bye[7] = {obj_lancerboss_slash_Step_0_gml_196_0, obj_lancerboss_slash_Step_0_gml_197_0,
                                    obj_lancerboss_slash_Step_0_gml_198_0, obj_lancerboss_slash_Step_0_gml_199_0,
                                    obj_lancerboss_slash_Step_0_gml_200_0, obj_lancerboss_slash_Step_0_gml_201_0,
                                    obj_lancerboss_slash_Step_0_gml_202_0};
    battle_say(bye, 7);
    if (plot() < 22) set_plot(22);
    enemy_spare(e);
  }
}

static bool lancer_act(Enemy *e, int act) {
  switch (act) {
    case 1: {
      static const uint16_t s[2] = {obj_lancerboss_slash_Step_0_gml_118_0, obj_lancerboss_slash_Step_0_gml_119_0};
      battle_say(s, 2);
      return true;
    }
    case 2: {
      uint16_t s = e->v[0] ? obj_lancerboss_slash_Step_0_gml_135_0 : obj_lancerboss_slash_Step_0_gml_130_0;
      e->v[0] = 1;
      battle_say(&s, 1);
      return true;
    }
    case 3: {
      static uint16_t s[2];
      int c = e->v[7];
      if (c >= 3) s[0] = obj_lancerboss_slash_Step_0_gml_145_0, battle_say(s, 1);
      else if (c == 2) s[0] = obj_lancerboss_slash_Step_0_gml_150_0, s[1] = obj_lancerboss_slash_Step_0_gml_151_0, battle_say(s, 2);
      else if (c == 1)
        s[0] = obj_lancerboss_slash_Step_0_gml_157_0, s[1] = obj_lancerboss_slash_Step_0_gml_158_0, e->at++, battle_say(s, 2);
      else s[0] = obj_lancerboss_slash_Step_0_gml_164_0, s[1] = obj_lancerboss_slash_Step_0_gml_165_0, e->at--, battle_say(s, 2);
      if (c < 3) mercy_add(e, 20);
      e->v[6] = 1, e->v[7]++;
      return true;
    }
    default: return false;
  }
}

static void lancer_draw(Enemy *e, int x, int y) {
  if (e->v[4]) return;
  gfx_sprite(spr_lancerbike, e->siner / 4, x + e->shake / 2, y, 0, e->fade ? (uint8_t)max_i(0, 255 - e->fade * 12) : 255);
}

/* ================================================================ the Dummy (obj_dummyenemy): Ralsei's lessons */
/* The tutorial is a battle of its own: the Dummy's turn first (Ralsei shows the SOUL, then a few bullets to dodge),
 * then one lesson a turn (plot): 1 FIGHT, 2 DEFEND, 3 Ralsei's SPELLs (only he moves), 4 ACT, 5 SPARE. After each
 * turn Ralsei says what he thinks of what Kris did, in the box; the Dummy never attacks again. It ends when Kris
 * SPAREs it at the end, or Ralsei runs out of things to teach. */
static struct {
  int8_t plot, attackcon, hitdum, misstime, defendtime, item_command, rwc, spare_command, win_spare;
  int8_t checked, hugtime, r_hugtime;
  bool won, pacifycon;
  int16_t dummyhp;
  /* a hug (actcon 10-20 and 30-40): who, its phase and frames, the marker's way */
  int8_t hug, hug_ph;
  int16_t hug_t, hug_img;
  int16_t fx, fy, tx, ty, kx, ky, rx, ry;
} D;
static const char *dm[10];
static char dz[2][128];

static void dm_clear(void) { memset(dm, 0, sizeof dm); }
static void M(int i, uint16_t id) { dm[i] = str(id); }
/* (a text with the key's name in it: scr_get_input_name, the OK key here) */
static void MZ(int i, uint16_t a, uint16_t b, int slot) {
  size_t n = strlen(str(a));
  if (n > 90) n = 90;
  memcpy(dz[slot], str(a), n);
  strcpy(dz[slot] + n, "OK");
  strncat(dz[slot], str(b), sizeof dz[slot] - n - 3);
  dm[i] = dz[slot];
}
/* the messages up to the one that ends the text (its last mark %) */
static int dm_count(void) {
  int i = 0;
  for (; i < 10 && dm[i]; i++) {
    size_t n = strlen(dm[i]);
    if (n && dm[i][n - 1] == '%') return i + 1;
  }
  return i;
}

static void dummy_talk(Enemy *e) {
  e->nsay = 0;
  dm_clear();
  M(0, obj_dummyenemy_slash_Step_0_gml_81_0);   /* "Skip": nothing said */
  int fe = 1, dial = 0;
  bool commanded = false, said = false;
  if (D.attackcon == 0) {
    /* the ambush: the SOUL, then dodging */
    D.attackcon = 1, fe = 0, said = true;
    set_flag(30, 0);
    M(0, obj_dummyenemy_slash_Step_0_gml_87_0), M(1, obj_dummyenemy_slash_Step_0_gml_88_0), M(2, obj_dummyenemy_slash_Step_0_gml_89_0), M(3, obj_dummyenemy_slash_Step_0_gml_90_0), M(4, obj_dummyenemy_slash_Step_0_gml_91_0), M(5, obj_dummyenemy_slash_Step_0_gml_92_0), M(6, obj_dummyenemy_slash_Step_0_gml_93_0), M(7, obj_dummyenemy_slash_Step_0_gml_94_0);
  }
  int kris = battle_action(CH_KRIS, NULL), ral = battle_action(CH_RALSEI, NULL);
  bool spare_used = D.plot != 3 && kris == A_SPARE;
  bool ral_wrong = D.plot == 3 && (ral == A_SPARE || ral == A_ITEM);
  if (D.plot >= 1) {
    if (kris == A_FIGHT) {
      if (D.dummyhp > e->hp) dial = 1;
      if (D.dummyhp == e->hp && D.plot == 1) dial = 2;
      if (D.misstime == 9 && D.plot == 2) dial = 3, set_flag(205, 6);
      if (dial == 1 && D.hitdum >= 3) D.hitdum = 4, dial = 3, set_flag(205, 4);
    }
    if (kris == A_DEFEND && !commanded) {
      fe = 0, said = true;
      M(0, obj_dummyenemy_slash_Step_0_gml_115_0), M(1, obj_dummyenemy_slash_Step_0_gml_116_0);
      if (D.defendtime == 1) M(0, obj_dummyenemy_slash_Step_0_gml_120_0), M(1, obj_dummyenemy_slash_Step_0_gml_121_0);
      if (D.defendtime == 2) fe = 1, M(0, obj_dummyenemy_slash_Step_0_gml_127_0), M(1, obj_dummyenemy_slash_Step_0_gml_128_0), M(2, obj_dummyenemy_slash_Step_0_gml_129_0);
      if (D.defendtime == 3) fe = 9, M(0, obj_dummyenemy_slash_Step_0_gml_136_0);
      if (D.plot == 2) {
        M(0, obj_dummyenemy_slash_Step_0_gml_142_0), M(1, obj_dummyenemy_slash_Step_0_gml_143_0), M(2, obj_dummyenemy_slash_Step_0_gml_144_0), M(3, obj_dummyenemy_slash_Step_0_gml_145_0), M(4, obj_dummyenemy_slash_Step_0_gml_146_0);
        if (e->hp == e->maxhp) M(2, obj_dummyenemy_slash_Step_0_gml_150_0), M(3, obj_dummyenemy_slash_Step_0_gml_151_0);
        e->tired = true;
        if (!e->comment || !strcmp(e->comment, " ")) e->comment = str(obj_dummyenemy_slash_Draw_0_gml_4_0);
        D.plot = 3;
        if (D.defendtime == 4) D.defendtime = 3;
      }
      if (D.defendtime >= 4) dial = 3, set_flag(205, 5);
      D.defendtime++;
      commanded = true;
    }
    if (kris == A_ITEM && !commanded) {
      fe = 0, said = true;
      M(0, obj_dummyenemy_slash_Step_0_gml_167_0), M(1, obj_dummyenemy_slash_Step_0_gml_168_0), M(2, obj_dummyenemy_slash_Step_0_gml_169_0);
      if (D.item_command == 1) fe = 1, M(0, obj_dummyenemy_slash_Step_0_gml_174_0), M(1, obj_dummyenemy_slash_Step_0_gml_175_0), M(2, obj_dummyenemy_slash_Step_0_gml_176_0), M(3, obj_dummyenemy_slash_Step_0_gml_177_0), M(4, obj_dummyenemy_slash_Step_0_gml_178_0);
      if (D.item_command == 2) fe = 6, M(0, obj_dummyenemy_slash_Step_0_gml_184_0), M(1, obj_dummyenemy_slash_Step_0_gml_185_0);
      if (D.item_command == 3) fe = 0, M(0, obj_dummyenemy_slash_Step_0_gml_191_0), M(1, obj_dummyenemy_slash_Step_0_gml_192_0);
      if (D.item_command >= 4) dial = 3;
      commanded = true;
      D.item_command++;
    }
    if (ral_wrong && !commanded) {
      said = true;
      M(0, obj_dummyenemy_slash_Step_0_gml_206_0);
      if (D.rwc == 0) M(0, obj_dummyenemy_slash_Step_0_gml_211_0);
      if (D.rwc == 1) M(0, obj_dummyenemy_slash_Step_0_gml_216_0), M(1, obj_dummyenemy_slash_Step_0_gml_217_0);
      if (D.rwc == 2) M(0, obj_dummyenemy_slash_Step_0_gml_222_0);
      if (D.rwc == 3) {
        M(0, obj_dummyenemy_slash_Step_0_gml_227_0), M(1, obj_dummyenemy_slash_Step_0_gml_228_0), M(2, obj_dummyenemy_slash_Step_0_gml_229_0), M(3, obj_dummyenemy_slash_Step_0_gml_230_0), M(4, obj_dummyenemy_slash_Step_0_gml_231_0);
        if (D.plot == 3) D.plot = 4;
        if (e->mercy >= 100) M(2, obj_dummyenemy_slash_Step_0_gml_237_0), M(3, obj_dummyenemy_slash_Step_0_gml_238_0), M(4, obj_dummyenemy_slash_Step_0_gml_239_0), D.plot = 5;
      }
      D.rwc++;
    }
    if (D.plot == 3 && ral == A_SPELL && !ral_wrong && !commanded) {
      said = true;
      M(0, obj_dummyenemy_slash_Step_0_gml_252_0), M(1, obj_dummyenemy_slash_Step_0_gml_253_0), M(2, obj_dummyenemy_slash_Step_0_gml_254_0), M(3, obj_dummyenemy_slash_Step_0_gml_255_0), M(4, obj_dummyenemy_slash_Step_0_gml_256_0);
      if (D.pacifycon) {
        M(0, obj_dummyenemy_slash_Step_0_gml_260_0);
        if (!e->tired) M(0, obj_dummyenemy_slash_Step_0_gml_262_0);
      }
      D.plot = 4;
      if (e->mercy >= 100) M(2, obj_dummyenemy_slash_Step_0_gml_268_0), M(3, obj_dummyenemy_slash_Step_0_gml_269_0), M(4, obj_dummyenemy_slash_Step_0_gml_270_0), D.plot = 5;
      commanded = true;
    }
    if (spare_used && !commanded) {
      said = true;
      if (D.plot < 5) {
        fe = 0;
        if (D.spare_command == 0) M(0, obj_dummyenemy_slash_Step_0_gml_282_0);
        if (D.spare_command == 1) M(0, obj_dummyenemy_slash_Step_0_gml_283_0);
        if (D.spare_command >= 2) M(0, obj_dummyenemy_slash_Step_0_gml_284_0);
        if (e->mercy >= 100) D.win_spare++;
        if (D.win_spare == 1) fe = 6, M(0, obj_dummyenemy_slash_Step_0_gml_289_0), M(1, obj_dummyenemy_slash_Step_0_gml_290_0), M(2, obj_dummyenemy_slash_Step_0_gml_291_0);
        if (D.win_spare == 2) fe = 6, M(0, obj_dummyenemy_slash_Step_0_gml_296_0), M(1, obj_dummyenemy_slash_Step_0_gml_297_0), M(2, obj_dummyenemy_slash_Step_0_gml_298_0), D.won = true;
      }
      if (D.plot == 5) M(0, obj_dummyenemy_slash_Step_0_gml_307_0), M(1, obj_dummyenemy_slash_Step_0_gml_308_0), D.won = true;
      D.spare_command++;
      commanded = true;
    }
    if (dial == 1) {
      said = true;
      if (D.plot == 1) {
        fe = 3;
        if (D.dummyhp > e->hp + 50) M(0, obj_dummyenemy_slash_Step_0_gml_327_0), M(1, obj_dummyenemy_slash_Step_0_gml_328_0);
        else M(0, obj_dummyenemy_slash_Step_0_gml_332_0), MZ(1, obj_dummyenemy_slash_Step_0_gml_333_0, obj_dummyenemy_slash_Step_0_gml_333_1, 0);
        if (D.misstime >= 6) fe = 8, M(0, obj_dummyenemy_slash_Step_0_gml_338_0), M(1, obj_dummyenemy_slash_Step_0_gml_339_0);
        M(2, obj_dummyenemy_slash_Step_0_gml_341_0), M(3, obj_dummyenemy_slash_Step_0_gml_342_0), M(4, obj_dummyenemy_slash_Step_0_gml_343_0), M(5, obj_dummyenemy_slash_Step_0_gml_344_0);
        D.plot = 2;
        if (D.defendtime >= 1) {
          M(2, obj_dummyenemy_slash_Step_0_gml_348_0), M(3, obj_dummyenemy_slash_Step_0_gml_349_0), M(4, obj_dummyenemy_slash_Step_0_gml_350_0), M(5, obj_dummyenemy_slash_Step_0_gml_351_0), M(6, obj_dummyenemy_slash_Step_0_gml_352_0), M(7, obj_dummyenemy_slash_Step_0_gml_353_0);
          D.plot = 3;
        }
      } else {
        if (D.hitdum == 0) fe = 1, M(0, obj_dummyenemy_slash_Step_0_gml_364_0), M(1, obj_dummyenemy_slash_Step_0_gml_365_0);
        if (D.hitdum == 1) fe = 1, M(0, obj_dummyenemy_slash_Step_0_gml_372_0), M(1, obj_dummyenemy_slash_Step_0_gml_373_0), M(2, obj_dummyenemy_slash_Step_0_gml_374_0);
        if (D.hitdum == 2) fe = 6, M(0, obj_dummyenemy_slash_Step_0_gml_380_0), M(1, obj_dummyenemy_slash_Step_0_gml_381_0), M(2, obj_dummyenemy_slash_Step_0_gml_382_0);
        if (D.hitdum >= 3) dial = 3;
        D.hitdum++;
      }
    }
    if (dial == 2) {
      said = true;
      fe = 3;
      M(0, obj_dummyenemy_slash_Step_0_gml_398_0), MZ(1, obj_dummyenemy_slash_Step_0_gml_399_0, obj_dummyenemy_slash_Step_0_gml_399_1, 0);
      if (D.misstime >= 1) {
        fe = 8;
        M(0, obj_dummyenemy_slash_Step_0_gml_403_0);
        if (D.misstime == 2) MZ(0, obj_dummyenemy_slash_Step_0_gml_404_0, obj_dummyenemy_slash_Step_0_gml_404_1, 1);
        if (D.misstime == 3) MZ(0, obj_dummyenemy_slash_Step_0_gml_405_0, obj_dummyenemy_slash_Step_0_gml_405_1, 1);
        if (D.misstime == 4) MZ(0, obj_dummyenemy_slash_Step_0_gml_406_0, obj_dummyenemy_slash_Step_0_gml_406_1, 1);
        if (D.misstime == 5) M(0, obj_dummyenemy_slash_Step_0_gml_407_0);
        if (D.misstime == 6) M(0, obj_dummyenemy_slash_Step_0_gml_408_0);
        if (D.misstime == 7) fe = 9, M(0, obj_dummyenemy_slash_Step_0_gml_412_0);
        if (D.misstime == 8) {
          fe = 1, M(0, obj_dummyenemy_slash_Step_0_gml_417_0);
          D.plot = 2;
          if (D.defendtime >= 1) {
            M(0, obj_dummyenemy_slash_Step_0_gml_421_0), M(1, obj_dummyenemy_slash_Step_0_gml_422_0), M(2, obj_dummyenemy_slash_Step_0_gml_423_0), M(3, obj_dummyenemy_slash_Step_0_gml_424_0), M(4, obj_dummyenemy_slash_Step_0_gml_425_0), M(5, obj_dummyenemy_slash_Step_0_gml_426_0), M(6, obj_dummyenemy_slash_Step_0_gml_427_0);
            D.plot = 3;
          }
        }
      }
      D.misstime++;
    }
    if (dial == 3) {
      said = true;
      fe = 9;
      M(0, obj_dummyenemy_slash_Step_0_gml_441_0), M(1, obj_dummyenemy_slash_Step_0_gml_442_0), M(2, obj_dummyenemy_slash_Step_0_gml_443_0), M(3, obj_dummyenemy_slash_Step_0_gml_444_0);
      if (D.hitdum >= 4) M(0, obj_dummyenemy_slash_Step_0_gml_447_0), M(1, obj_dummyenemy_slash_Step_0_gml_448_0), M(2, obj_dummyenemy_slash_Step_0_gml_449_0);
      D.won = true;
    }
  }
  /* (the "Skip" text isn't shown: the writer goes at once) */
  if (said && dm[0] != str(obj_dummyenemy_slash_Step_0_gml_81_0)) battle_box_talk(FC_RALSEI, fe, dm, dm_count());
  D.dummyhp = e->hp;
  e->rest = D.attackcon == 2 || D.won;
}

/* obj_dbulletcontroller type 14: small bullets from the right, every 10 frames */
static void dummy_attack(Enemy *e, int t) {
  if (t % 10) return;
  int half = box_half();
  Bullet *b = bullet_new(spr_smallbullet, 660, box_cy() - half + rnd_n(2 * half));
  b->vx = -8 * BFX, b->angle = (int16_t)angle_of(180), b->scale = 128, b->r = 6;
  b->damage = (int16_t)(e->at * 5);
}

/* the turn's end: after the ambush the FIGHT lesson; then "Let's try ..." for the lesson it is */
static void dummy_flavor(Enemy *e) {
  if (D.won) {
    enemy_leave(e);
    return;
  }
  if (D.attackcon == 1) {
    D.attackcon = 2;
    dm_clear();
    int fe = 0;
    M(0, obj_dummyenemy_slash_Step_0_gml_598_0);
    if (battle_hp_lost(CH_KRIS) > 0) fe = 6, M(0, obj_dummyenemy_slash_Step_0_gml_602_0);
    M(1, obj_dummyenemy_slash_Step_0_gml_604_0), M(2, obj_dummyenemy_slash_Step_0_gml_605_0), M(3, obj_dummyenemy_slash_Step_0_gml_606_0), M(4, obj_dummyenemy_slash_Step_0_gml_607_0);
    D.plot = 1;
    battle_lesson(FC_RALSEI, fe, dm, 5, obj_dummyenemy_slash_Step_0_gml_610_0);
    return;
  }
  static const uint16_t lets[5] = {obj_dummyenemy_slash_Step_0_gml_556_0, obj_dummyenemy_slash_Step_0_gml_557_0, obj_dummyenemy_slash_Step_0_gml_558_0, obj_dummyenemy_slash_Step_0_gml_559_0, obj_dummyenemy_slash_Step_0_gml_560_0};
  if (D.plot >= 1 && D.plot <= 5) battle_say(&lets[D.plot - 1], 1);
}

static void hug_start(int who) {
  int x = 80, y = 140;
  battle_hero_pos(CH_KRIS, &x, &y);
  D.hug = (int8_t)who, D.hug_ph = 0, D.hug_t = 0, D.hug_img = 0;
  D.fx = (int16_t)x, D.fy = (int16_t)y, D.kx = (int16_t)x, D.ky = (int16_t)y;
  if (who == 1) {
    Enemy *e = &en[0];
    /* (monsterx - 42, monstery - 30): the Dummy's middle, x + 45, y + 75 */
    D.tx = (int16_t)(e->x + 3), D.ty = (int16_t)(e->y + 45);
  } else {
    int rx = 80, ry = 140;
    battle_hero_pos(CH_RALSEI, &rx, &ry);
    D.rx = (int16_t)rx, D.ry = (int16_t)ry;
    D.tx = (int16_t)(rx - 24), D.ty = (int16_t)(ry + 10);
  }
  battle_hide_hero(CH_KRIS, true);
}

static bool dummy_act(Enemy *e, int act) {
  (void)e;
  dm_clear();
  switch (act) {
    case 1:
      M(0, obj_dummyenemy_slash_Step_0_gml_642_0);
      if (D.checked == 0) {
        M(0, obj_dummyenemy_slash_Step_0_gml_645_0);
        dm[1] = "\\F0\\E6\\FR%";
        M(2, obj_dummyenemy_slash_Step_0_gml_647_0), M(3, obj_dummyenemy_slash_Step_0_gml_648_0), M(4, obj_dummyenemy_slash_Step_0_gml_649_0);
      }
      D.checked++;
      battle_say_strs(dm, dm_count());
      return true;
    case 2:
      M(0, obj_dummyenemy_slash_Step_0_gml_666_0);
      battle_say_strs(dm, 1);
      hug_start(1);
      return true;
    case 3:
      M(0, obj_dummyenemy_slash_Step_0_gml_808_0);
      battle_say_strs(dm, 1);
      hug_start(2);
      return true;
    default: return false;
  }
}

/* the hug, step by step: there, the hug, Ralsei's words, the hug undone, back */
static int hug_step(void) {
  if (!D.hug) return 0;
  int t = ++D.hug_t;
  switch (D.hug_ph) {
    case 0:
      if (t <= 15) D.kx = (int16_t)(D.fx + (D.tx - D.fx) * t / 15), D.ky = (int16_t)(D.fy + (D.ty - D.fy) * t / 15);
      if (t == 25 && D.hug == 2) battle_hide_hero(CH_RALSEI, true);
      if (t > 25) D.hug_img = (int16_t)min_i(3, (t - 25) / 4);
      if (t >= 37) D.hug_ph = 1;
      break;
    case 1:
      if (battle_text_on()) break;
      dm_clear();
      if (D.hug == 1) {
        int fe = 0;
        set_flag(205, 1);
        M(0, obj_dummyenemy_slash_Step_0_gml_711_0);
        if (D.plot == 5) {
          M(0, obj_dummyenemy_slash_Step_0_gml_714_0);
          if (D.hugtime >= 1) M(0, obj_dummyenemy_slash_Step_0_gml_715_0);
          D.hugtime++;
        }
        if (D.plot == 1) {
          fe = 3;
          M(0, obj_dummyenemy_slash_Step_0_gml_722_0), M(1, obj_dummyenemy_slash_Step_0_gml_723_0), M(2, obj_dummyenemy_slash_Step_0_gml_724_0), M(3, obj_dummyenemy_slash_Step_0_gml_725_0), M(4, obj_dummyenemy_slash_Step_0_gml_726_0), M(5, obj_dummyenemy_slash_Step_0_gml_727_0), M(6, obj_dummyenemy_slash_Step_0_gml_728_0);
          D.plot = 2;
          if (D.defendtime >= 1) {
            M(3, obj_dummyenemy_slash_Step_0_gml_733_0), M(4, obj_dummyenemy_slash_Step_0_gml_734_0), M(5, obj_dummyenemy_slash_Step_0_gml_735_0), M(6, obj_dummyenemy_slash_Step_0_gml_736_0), M(7, obj_dummyenemy_slash_Step_0_gml_737_0), M(8, obj_dummyenemy_slash_Step_0_gml_738_0);
            D.plot = 3;
          }
        }
        if (D.plot == 4) fe = 8, M(0, obj_dummyenemy_slash_Step_0_gml_746_0), M(1, obj_dummyenemy_slash_Step_0_gml_747_0), M(2, obj_dummyenemy_slash_Step_0_gml_748_0), M(3, obj_dummyenemy_slash_Step_0_gml_749_0), M(4, obj_dummyenemy_slash_Step_0_gml_750_0), D.plot = 5;
        battle_face(FC_RALSEI, fe);
      } else {
        M(0, obj_dummyenemy_slash_Step_0_gml_846_0);
        if (D.r_hugtime == 0) M(0, obj_dummyenemy_slash_Step_0_gml_850_0), M(1, obj_dummyenemy_slash_Step_0_gml_851_0), M(2, obj_dummyenemy_slash_Step_0_gml_852_0), M(3, obj_dummyenemy_slash_Step_0_gml_853_0);
        if (D.r_hugtime == 1) M(0, obj_dummyenemy_slash_Step_0_gml_858_0), M(1, obj_dummyenemy_slash_Step_0_gml_859_0), M(2, obj_dummyenemy_slash_Step_0_gml_860_0);
        if (D.r_hugtime == 2) M(0, obj_dummyenemy_slash_Step_0_gml_868_0), M(1, obj_dummyenemy_slash_Step_0_gml_869_0), M(2, obj_dummyenemy_slash_Step_0_gml_870_0), M(3, obj_dummyenemy_slash_Step_0_gml_871_0);
        if (D.r_hugtime >= 3) M(0, obj_dummyenemy_slash_Step_0_gml_876_0);
        D.r_hugtime++;
        battle_face(FC_RALSEI, 2);
      }
      battle_say_strs(dm, dm_count());
      D.hug_ph = 2;
      break;
    case 2:
      if (!battle_text_on()) D.hug_ph = 3, D.hug_t = 0;
      break;
    case 3:
      D.hug_img = (int16_t)max_i(0, 3 - t / 4);
      if (t >= 12) D.hug_ph = 4, D.hug_t = 0;
      break;
    default:
      if (t <= 15) D.kx = (int16_t)(D.tx + (D.fx - D.tx) * t / 15), D.ky = (int16_t)(D.ty + (D.fy - D.ty) * t / 15);
      if (t >= 25) {
        if (D.hug == 1) mercy_add(&en[0], 100);
        else battle_hide_hero(CH_RALSEI, false);
        battle_hide_hero(CH_KRIS, false);
        D.hug = 0;
      }
      break;
  }
  return 1;
}

static int dummy_script(Enemy *e, int ev, int arg) {
  if (!e->v[0]) {
    /* (the battle's first call: its own state from the start) */
    memset(&D, 0, sizeof D);
    e->v[0] = 1;
    writer_img[0] = spr_btfight_h, writer_img[1] = spr_btdefend_h, writer_img[2] = spr_bttech_h;
    writer_img[3] = spr_btact_h, writer_img[4] = spr_btspare_h, writer_img[5] = spr_btitem_h;
  }
  switch (ev) {
    case BS_CANMOVE: return D.plot == 3 ? arg == CH_RALSEI : arg == CH_KRIS;
    case BS_STEP: return arg ? D.hug != 0 : hug_step();
    case BS_DRAW:
      if (D.hug) {
        gfx_sprite(spr_kris_hug, D.hug_img, D.kx / 2, D.ky / 2, 0, 255);
        if (D.hug == 2 && (D.hug_ph > 0 || D.hug_t >= 25)) gfx_sprite(spr_ralseil_blush, 0, D.rx / 2, D.ry / 2, 0, 255);
      }
      return 0;
    case BS_PACIFIED: D.pacifycon = true; return 0;
    default: return 0;
  }
}

/* ================================================================ the table */
static const EnemyDef defs[] = {
  {obj_diamondenemy, spr_diamondm_idle, spr_diamondm_hurt, spr_diamondm_spared, 6, rudinn_talk, rudinn_attack,
   rudinn_flavor, rudinn_act, NULL, 140, NULL, 0, 0, 0, NULL, 0},
  {obj_heartenemy, spr_heartenemy_idle, spr_heartenemy_hurt, spr_heartenemy_spared, 6, hathy_talk, hathy_attack,
   hathy_flavor, hathy_act, hathy_draw, 140, NULL, -100, 0, spr_battleblcon, NULL, 0},
  {obj_lancerboss, spr_lancerbike, spr_lancerbike, spr_lancerbike, 4, lancer_talk, lancer_attack, lancer_flavor,
   lancer_act, lancer_draw, 999, lancer_front, -235, -65, 0, NULL, 0},
  {obj_dummyenemy, spr_dummymonster, spr_dummymonster, spr_dummymonster, 6, dummy_talk, dummy_attack, dummy_flavor,
   dummy_act, NULL, 150, NULL, 0, 0, 0, dummy_script, EF_AMBUSH | EF_STAYS},
};

void spinheart_attack(Enemy *e, int t) { hathy_attack(e, t); }

const EnemyDef *enemy_def(int obj) {
  for (unsigned i = 0; i < sizeof defs / sizeof defs[0]; i++)
    if (defs[i].obj == obj) return &defs[i];
  for (int i = 0; i < foes_count; i++)
    if (foes_defs[i].obj == obj) return &foes_defs[i];
  return &defs[0];
}
