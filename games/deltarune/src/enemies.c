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

/* ================================================================ the Dummy (obj_dummyenemy) */
static void dummy_talk(Enemy *e) { e->nsay = 0; }
static void dummy_attack(Enemy *e, int t) { (void)e, (void)t; }
static bool dummy_act(Enemy *e, int act) {
  uint16_t s;
  switch (act) {
    case 1: s = obj_dummyenemy_slash_Step_0_gml_642_0; break;
    case 2: s = obj_dummyenemy_slash_Step_0_gml_666_0, mercy_add(e, 100); break;
    default: s = obj_dummyenemy_slash_Step_0_gml_666_0, mercy_add(e, 100); break;
  }
  battle_say(&s, 1);
  return true;
}

/* ================================================================ the table */
static const EnemyDef defs[] = {
  {obj_diamondenemy, spr_diamondm_idle, spr_diamondm_hurt, spr_diamondm_spared, 6, rudinn_talk, rudinn_attack,
   rudinn_flavor, rudinn_act, NULL, 140, NULL, 0, 0, 0},
  {obj_heartenemy, spr_heartenemy_idle, spr_heartenemy_hurt, spr_heartenemy_spared, 6, hathy_talk, hathy_attack,
   hathy_flavor, hathy_act, hathy_draw, 140, NULL, -100, 0, spr_battleblcon},
  {obj_lancerboss, spr_lancerbike, spr_lancerbike, spr_lancerbike, 4, lancer_talk, lancer_attack, lancer_flavor,
   lancer_act, lancer_draw, 999, lancer_front, -235, -65, 0},
  {obj_dummyenemy, spr_dummymonster, spr_dummymonster, spr_dummymonster, 6, dummy_talk, dummy_attack, NULL, dummy_act,
   NULL, 60, NULL, 0, 0, 0},
};

const EnemyDef *enemy_def(int obj) {
  for (unsigned i = 0; i < sizeof defs / sizeof defs[0]; i++)
    if (defs[i].obj == obj) return &defs[i];
  return &defs[0];
}
