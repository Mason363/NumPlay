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
        b->scale = (int16_t)(256 * 3 - b->alpha * 2);
        /* (move_towards_point 2 while forming) */
        b->x += ldx(2 * 16, dir), b->y += ldy(2 * 16, dir);
      } else if (!b->c) {
        b->c = 1, b->t = 0, b->scale = 256;
        b->b = (int16_t)dir;
      } else if (b->t >= 5 && b->c == 1) {
        Bullet *s = bullet_new(spr_diamondbullet, bx, by);
        s->damage = b->damage, s->r = 8;
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
        b->scale = (int16_t)(256 * 3 - b->alpha * 2);
        if (b->alpha >= 250) {
          b->scale = 256, b->harmless = false, b->r = 8;
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
  /* its bubble is a picture (spr_heartblcon): drawn by hathy_draw */
  e->v[5] = 20;
  e->nsay = 0;
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
      b->damage = (int16_t)dmg, b->alpha = 0, b->scale = 256 * 2, b->harmless = true, b->r = 10;
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
          b->scale = (int16_t)(b->scale - 256 / 10);
          if (b->t >= 5) b->c = 1, b->t = 0, b->harmless = false, b->scale = 256;
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
        b->damage = (int16_t)dmg, b->alpha = 0, b->d = 4, b->a = (int16_t)k, b->r = 6;
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

/* obj_lancerbike: a race (up and down, two honks, a dash across) or a lap round the screen shooting spades */
static void lancer_attack(Enemy *e, int t) {
  static int bx, by, vx, vy, con, timer, maxr, angd;
  int dmg = 20;
  if (sv.hp[CH_KRIS] <= sv.maxhp[CH_KRIS] / 2) dmg = 10;
  if (sv.hp[CH_KRIS] <= sv.maxhp[CH_KRIS] / 4) dmg = 5;
  if (t == 0) {
    bx = e->x, by = e->y, con = (e->turns % 2) ? 10 : 1, timer = 0, angd = 0;
    vy = 14 * (rnd_n(2) ? 1 : -1), vx = 0, maxr = 15 + rnd_n(25);
    e->v[4] = 1;   /* (hidden: the bike is the bullet) */
  }
  timer++;
  int orx = e->x, ory = e->y;
  if (con == 1) {   /* racing up and down */
    by += vy;
    if (by < 120 + 10) vy = 12;
    if (by > 250 - 10) vy = -12;
    if (timer > maxr) con = 2, timer = 0;
  } else if (con == 2) {   /* honk, honk */
    if (timer >= 25) con = 3, vx = -20, timer = 0;
  } else if (con == 3) {
    bx += vx;
    angd = min_i(50, angd + timer * 2 + 4);
    if (bx <= -40) bx = 740, by = ory, vx = -12, con = 4, angd = 0;
  } else if (con == 4) {
    bx += vx;
    if (bx <= orx + 5) bx = orx, con = 99;
  } else if (con >= 10 && con < 20) {
    /* the lap: left along the bottom, up the left, right along the top, down the right, home */
    int sp = 16;
    if (con == 10) { if (timer > 10) con = 11; }
    else if (con == 11) { bx -= sp; if (bx <= 5) bx = 5, con = 12; }
    else if (con == 12) { by -= sp; if (by <= 5) by = 5, con = 13; }
    else if (con == 13) { bx += sp; if (bx >= 630) bx = 630, con = 14; }
    else if (con == 14) { by += sp; if (by >= ory) by = ory, con = 15; }
    else if (con == 15) { bx -= sp; if (bx <= orx) bx = orx, con = 16, timer = 0; }
    else if (con == 16) { if (timer >= 30) con = 99; }
    if (con >= 11 && con <= 15 && timer % 10 == 0) {
      Bullet *s = bullet_new(spr_spadebullet, bx, by);
      int d = dir_to(bx, by, heart_x(), heart_y());
      s->vx = ldx(4 * 16, d), s->vy = ldy(4 * 16, d);
      s->ax = ldx(16 * 4 / 10, d), s->ay = ldy(16 * 4 / 10, d);
      s->angle = (int16_t)angle_of(d), s->damage = (int16_t)(e->at * 5), s->r = 8;
      if (sv.hp[CH_KRIS] <= sv.maxhp[CH_KRIS] / 2) s->damage = (int16_t)((sv.hp[CH_KRIS] + 2) / 3);
    }
  }
  e->v[2] = (int16_t)(bx / 2), e->v[3] = (int16_t)(by / 2);
  /* the bike itself hurts */
  int dx = heart_x() - (bx + 40), dy = heart_y() - (by + 30);
  if (con != 99 && dx * dx + dy * dy < 40 * 40) {
    Bullet *b = bullet_new(0xFFFF, heart_x(), heart_y());
    b->damage = (int16_t)dmg, b->life = 1, b->r = 30;
  }
  if (con == 99) {
    e->v[4] = 0;
    e->turns++;
    if (e->turns >= 4) e->v[5] = 1;   /* out of fuel */
    for (int i = 0; i < MAX_BUL; i++) bul[i].on = false;
    battle_end_attack();
  }
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
  if (e->v[4]) x = e->v[2], y = e->v[3];
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
   rudinn_flavor, rudinn_act, NULL, 140},
  {obj_heartenemy, spr_heartenemy_idle, spr_heartenemy_hurt, spr_heartenemy_spared, 6, hathy_talk, hathy_attack,
   hathy_flavor, hathy_act, hathy_draw, 140},
  {obj_lancerboss, spr_lancerbike, spr_lancerbike, spr_lancerbike, 4, lancer_talk, lancer_attack, lancer_flavor,
   lancer_act, lancer_draw, 999},
  {obj_dummyenemy, spr_dummymonster, spr_dummymonster, spr_dummymonster, 6, dummy_talk, dummy_attack, NULL, dummy_act,
   NULL, 60},
};

const EnemyDef *enemy_def(int obj) {
  for (unsigned i = 0; i < sizeof defs / sizeof defs[0]; i++)
    if (defs[i].obj == obj) return &defs[i];
  return &defs[0];
}
