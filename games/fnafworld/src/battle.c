/* FNAF World: a battle, as the overworld frame's events run it (their numbers in the comments), 60 steps a second.
 *
 * The party: two rows of four ("load row"), each member's charge fills a point a second (0.7 s with a speed boost)
 * while nobody else in the row is full; a full member shows its three attacks. The enemies ("target") fill their own
 * speed and hit a random member. Positions are the game's (its battle is the 800 x 480 view from 23, 14), drawn at
 * 0.4. */
#include <string.h>
#include "anims.h"
#include "data.h"
#include "game.h"
#include "gfx.h"

#define B g.b
#define VIEW_X 23
#define VIEW_Y 14
#define SX(x) (((x) - VIEW_X) * 2 / 5)
#define SY(y) (((y) - VIEW_Y) * 2 / 5)
#define FIELD_H 192

/* ------------------------------------------------------------------ the game's paths (their nodes: dx, dy) */
static const int8_t PATH_IN[39][2] = {   /* "character" movement 1: the hop in */
  {-6, -19}, {-1, -1}, {-5, -7}, {-4, -6}, {-12, -16}, {-10, -10}, {-11, -8}, {-8, -8}, {-14, -9}, {-17, -7},
  {-16, -6}, {-17, -6}, {-19, -4}, {-16, -3}, {-19, -1}, {-18, 0}, {-21, 3}, {-19, 4}, {-18, 5}, {-15, 4}, {-19, 5},
  {-14, 3}, {-15, 5}, {-13, 6}, {-14, 6}, {-5, 5}, {-10, 7}, {-7, 7}, {-10, 7}, {0, 1}, {-5, 6}, {-2, 2}, {-2, 2},
  {-3, 6}, {-1, 1}, {-2, 2}, {-3, 5}, {-3, 3}, {0, 1}};
static const int8_t ENEMY_IN[27][2] = {   /* "target" movement 1 */
  {-1, -3}, {0, -2}, {9, -22}, {6, -16}, {21, -31}, {19, -23}, {24, -18}, {27, -13}, {23, -6}, {34, 1}, {22, 6},
  {11, 4}, {24, 12}, {19, 12}, {17, 14}, {6, 8}, {16, 24}, {6, 12}, {5, 13}, {4, 11}, {0, 1}, {0, 1}, {1, 4}, {1, 3},
  {0, 3}, {0, 1}, {0, 1}};
static const int8_t ENEMY_LUNGE[12][2] = {   /* "target" movement 2 ("Movement" 1), back at its end */
  {0, -6}, {0, -1}, {6, -5}, {8, -7}, {14, -8}, {15, -3}, {16, -1}, {18, 2}, {16, 6}, {11, 8}, {8, 7}, {5, 9}};
/* where they come from: "make character" 1-4, "spawn enemy" 1-4 */
static const int16_t MAKE[4][2] = {{991, 291}, {1104, 363}, {931, 423}, {1057, 501}};
static const int16_t SPAWN[4][2] = {{-80, 279}, {-173, 352}, {26, 340}, {-64, 430}};
static const int16_t STOP[4][2] = {{623, 263}, {737, 339}, {549, 399}, {677, 476}};   /* "stop spot" 1-4 */
#define PATH_STEP 12   /* (a path's speed 100: about 12 pixels a frame) */

/* ------------------------------------------------------------------ time */
/* the game's "every N ms": each its own timer (T_*), the clock in thirds of a millisecond (50 a step) */
enum {
  T_CHARGE, T_CHARGE_FAST, T_SPEED, T_SPEED_SLOW, T_ENEMY_GO, T_WHO, T_STRENGTH, T_SECOND, T_SPECIAL, T_ROLL,
  T_SCREW, T_CHEESE, T_4000, T_CONFUSE, T_5000, T_POISON, T_COMET, T_PFURY, T_FURY, T_PIZZA, T_PIZZA2, T_FREDDLE,
  T_STARS, T_DRIP, T_PUDDLE, T_POPPER, T_CLOUDS, T_FAVORS, T_MYSTERY, T_HOCUS, T_VIRUS, T_UNSCREW, T_LAVA, T_EXP,
  T_TOKENS, T_RUN, T_SNOW, T_COUNT
};
static uint32_t ms3;   /* the battle's clock */
static bool every(int id, int ms) {
  if (!B.timers[id]) {   /* (the first after a whole period) */
    B.timers[id] = ms3 + (uint32_t)ms * 3;
    return false;
  }
  if (ms3 < B.timers[id]) return false;
  B.timers[id] += (uint32_t)ms * 3;
  if (B.timers[id] <= ms3) B.timers[id] = ms3 + (uint32_t)ms * 3;
  return true;
}
/* each byte has its own timer */
static uint32_t byte_t[4];
static bool byte_every(int k, int ms) {
  if (ms3 < byte_t[k]) return false;
  byte_t[k] = ms3 + (uint32_t)ms * 3;
  return true;
}

/* ------------------------------------------------------------------ helpers */
static int field_char(int k) { return g.f.slot[(B.row - 1) * 4 + k]; }   /* the member in field place k (0-3) */
static int32_t *life_of(int k) { return &B.life[(B.row - 1) * 4 + k]; }
static int enemies_alive(void) {
  int n = 0;
  for (int i = 0; i < 4; i++) n += B.en[i].alive;
  return n;
}
static int pick_enemy(void) {   /* Clickteam's "pick one at random" */
  int n = enemies_alive();
  if (!n) return -1;
  int k = rnd_n(n);
  for (int i = 0; i < 4; i++)
    if (B.en[i].alive && k-- == 0) return i;
  return -1;
}
static int32_t max_life(void) { return B.boss ? B.boss_life : B.enemy_max; }
static int level_of(int k) { return g.f.lv[field_char(k)]; }

static Fx *fx_new(int kind, int an, int x, int y) {
  for (int i = 0; i < NFX; i++)
    if (!B.fx[i].kind) {
      Fx *f = &B.fx[i];
      memset(f, 0, sizeof *f);
      f->kind = (uint8_t)kind, f->an = (uint8_t)an, f->x = (int16_t)x, f->y = (int16_t)y, f->target = -1;
      f->plays = ANIMS[an].plays;
      return f;
    }
  return NULL;
}
/* an animation's step: its speed counter; true when it ends (it plays a set number of times) */
static bool anim_step(uint8_t an, uint8_t *img, uint8_t *cnt, uint8_t *plays) {
  const AnimDef *a = &ANIMS[an];
  int c = *cnt + a->speed;
  bool end = false;
  while (c >= 100) {
    c -= 100;
    if (*img + 1 < a->n) (*img)++;
    else if (a->plays == 0) *img = a->back;
    else if (*plays > 1) (*plays)--, *img = a->back;
    else end = true;
  }
  *cnt = (uint8_t)c;
  return end;
}
static int pic_of(int an, int img) { return ANIM_PICS[ANIMS[an].first + (img < ANIMS[an].n ? img : ANIMS[an].n - 1)]; }

/* floating numbers: a hit (white), a heal (green), a member's hurt (red) */
enum { NUM_HIT, NUM_HEAL, NUM_HURT };
static struct { int16_t x, y, t; int32_t v; uint8_t kind; } nums[12];
static void number(int x, int y, int32_t v, int kind) {
  int best = 0;
  for (int i = 0; i < 12; i++)
    if (nums[i].t <= 0 || nums[i].t < nums[best].t) best = i;
  nums[best].x = (int16_t)x, nums[best].y = (int16_t)y, nums[best].v = v, nums[best].t = 60, nums[best].kind = (uint8_t)kind;
}

static void sparks(int x, int y) {
  for (int k = 0; k < 3; k++) {
    Fx *f = fx_new(1, AN_SPARK, x + rnd_n(60) - 30, y + rnd_n(50) - 40);
    if (f) f->vy = (int16_t)(16 + rnd_n(32)), f->img = (uint8_t)rnd_n(4);
  }
}

/* ------------------------------------------------------------------ effects (FX_*: their behaviour) */
enum {
  FX_NONE, FX_SPARK, FX_SHOW,    /* a picture that plays once where it is */
  FX_MIC, FX_PIZZA, FX_BITE, FX_HOOK, FX_CLAW, FX_TORNADO, FX_BALLOON, FX_HEAD, FX_TOXICBITE, FX_TBALLOON,
  FX_BADPIZZA, FX_GLOOM, FX_CLOUD, FX_RAIN, FX_STAR, FX_DRIP, FX_PUDDLE, FX_MUNCHIE, FX_FREDDLE, FX_POPPER,
  FX_PRIZE, FX_HAUNT, FX_ENDO, FX_DAGGER, FX_WALL, FX_JACKO, FX_SAW, FX_ENEMY_BALLOON, FX_STINGER, FX_SYTH,
  FX_LASER, FX_NOTE, FX_SCREW, FX_HIT, FX_DEATH
};

/* ------------------------------------------------------------------ the start */
static void party_enter(void) {
  for (int k = 0; k < 4; k++) {
    B.pm[k].x = MAKE[k][0], B.pm[k].y = MAKE[k][1];
    B.pm[k].state = 1, B.pm[k].node = 0, B.pm[k].t = (int16_t)(5 * k);   /* (made 5 frames apart: 22-29) */
    B.pm[k].img = 0, B.pm[k].cnt = 0;
  }
  B.moving = 1;
}

static int enemy_an(int type, int alt) {
  static const uint8_t A[30] = {0, 0, 12, 14, 16, 20, 15, 18, 25, 30, 29, 28, 23, 21, 24, 17, 22, 13, 19, 26, 27, 31,
                                32, 33, 13, 33, 15, 35, 36, 37};
  int a;
  if (type >= 51) a = 38 + (type - 51);
  else if (alt && type == 24) a = 14;
  else if (alt && type == 25) a = 12;
  else if (alt && type == 26) a = 17;
  else a = A[type < 30 ? type : 1];
  return a == 0 ? AN_ENEMY0 : AN_ENEMY12 + (a - 12);
}

void battle_start(int boss) {
  World *w = &g.w;
  memset(&B, 0, sizeof B);
  memset(nums, 0, sizeof nums);
  ms3 = 0;
  for (int k = 0; k < 4; k++) byte_t[k] = 0;
  B.on = 1, B.boss = (uint8_t)boss, B.row = 1, B.stage = 1, B.ready_turn = 1;
  int area = w->area, diff = g.f.diff ? g.f.diff : 1;
  B.etype = (uint8_t)(area == 50 ? 51 + rnd_n(3) : area + rnd_n(3) + 1);   /* 939 */
  B.chal_roll = (uint8_t)(rnd_n(5) + 1);                                      /* 940 */
  for (int c = 0; c < 4; c++)
    if (g.f.chip[c] == 14) B.chal_roll = 1;                                   /* 942: Find: Characters */
  for (int k = 0; k < 8; k++) {
    int c = g.f.slot[k];
    B.life_max[k] = B.life[k] = life_max(c, g.f.lv[c]);                       /* 1017 */
    B.charge[k] = (uint8_t)(rnd_n(5) + 5);                                    /* 107 */
  }
  /* the enemies (943-948) */
  if (g.w.note == 99) {   /* (a challenger, after the banner: 1202) */
    g.w.note = 0;
  }
  if (B.chal || boss) B.count = 1;
  else B.count = (uint8_t)(rnd_n(4) + 1);
  for (int i = 0; i < B.count; i++) {
    Enemy *e = &B.en[i];
    e->alive = 1;
    e->x = SPAWN[i][0], e->y = (int16_t)(SPAWN[i][1] + (B.count == 1 && (B.chal || boss) ? 43 : 0));
    e->path = 1, e->node = 0;
    e->speed = (int16_t)(boss ? 1 : rnd_n(75));      /* 1007-1008 */
    e->an = (uint8_t)enemy_an(B.etype, B.etype >= 24 && B.etype <= 26 && rnd_n(3) == 1);
    e->img = (uint8_t)rnd_n(ANIMS[e->an].n);
  }
  B.enemy_max = enemy_life(B.etype, diff);
  B.special = (uint8_t)enemy_special(B.etype);
  if (boss) {
    int sp, an;
    boss_info(boss, diff, &B.boss_life, &B.boss_speed, &B.boss_strength, &sp, &an);
    B.special = (uint8_t)sp;
    B.en[0].an = (uint8_t)(AN_BOSS51 + (an - 51));
  }
  party_enter();
  /* the chips worn (1505-1540): applied as the game cycles through them in its first second */
  for (int c = 0; c < 4; c++) {
    switch (g.f.chip[c]) {
      case 1: B.def_boost = 15; break;
      case 2: B.atk_boost = 15; break;
      case 3: B.spd_boost = 15; break;
      case 4: B.comet = B.comet > 1 ? B.comet : 1; break;
      case 5:
        for (int k = 0; k < 8; k++) B.charge[k] = (uint8_t)(7 + rnd_n(2));
        B.quick_t = 200;
        break;
      case 6: B.block_confuse = 1; break;
      case 7: B.run_luck = 1; break;
      case 11: B.comet = 3; break;
      case 12: for (int k = 0; k < 4; k++) B.gift[k] = 100; break;
      case 13: B.regen = 120; break;
      case 15: B.pwr_down = B.spd_down = B.spd_down2 = 45; break;
      case 16: B.freddle_fury = 1; break;
      case 17: B.shield = 30; break;
      case 18: B.mimic = 30; B.mimic_attack = 49; break;
      case 19: B.counter_bite = 1; break;
      case 20: B.pizza_fury = 1; break;
      case 21: B.block_unscrew = 1; break;
    }
  }
  B.attacked_who = 1;
  B.team_ok[0] = B.team_ok[1] = 1;
  memcpy(B.slots, g.f.slot, 8);
  w->grace = 500;          /* 1195 */
  world_save_pos();
}

/* a challenger after a won battle (1198, 1202): its own battle */
void battle_challenger(int c) {
  battle_start(0);
  B.chal = 2, B.chal_c = (uint8_t)c;
  B.count = 1;
  memset(&B.en[1], 0, sizeof B.en[1] * 3);
  Enemy *e = &B.en[0];
  e->y = SPAWN[0][1] + 43;
  e->an = (uint8_t)(AN_CHAL9 + (c - 9));
  e->img = 0;
  B.enemy_max = challenger_life(c, g.f.diff);   /* 1004 */
  /* 1205-1236: its special */
  static const uint8_t SP[41] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 2, 3, 2, 7, 6, 3, 0, 5, 4, 3, 5, 7, 7, 0, 0, 0,
                                 0, 8, 4, 0, 0, 6, 8, 0, 0, 0, 0, 0, 0, 0};
  B.special = c <= 40 ? SP[c] : 0;
}

/* ------------------------------------------------------------------ hits */
static void hit_enemy(int i, int32_t d) {   /* "target" A = d (the game sets it; 479 takes it) */
  if (i >= 0 && i < 4 && B.en[i].alive && d > 0) B.en[i].dmg = d;
}
static void hit_all(int32_t base, int r) {   /* each its own roll */
  for (int i = 0; i < 4; i++)
    if (B.en[i].alive) hit_enemy(i, base + rnd_n(r));
}
static void kill_enemy(int i) {   /* 741, 827: "broken", the flash */
  Enemy *e = &B.en[i];
  if (!e->alive) return;
  e->alive = 0;
  B.flash = 200;
  Fx *f = fx_new(FX_SHOW, AN_BROKEN, e->x, e->y - 96);
  (void)f;
  if (B.boss) {
    fx_new(FX_SHOW, AN_POW, e->x, e->y - 100);
    B.shake = 10;
  }
}

static void apply_hits(void) {   /* 479: each target's A taken from its life */
  for (int i = 0; i < 4; i++) {
    Enemy *e = &B.en[i];
    if (!e->alive || e->dmg <= 0) continue;
    e->hurt -= e->dmg;
    e->s5 = 20;
    B.shake = 10;
    number(e->x + 25, e->y - 67, e->dmg, NUM_HIT);
    sparks(e->x, e->y - 100);
    B.flash = 100;
    e->dmg = 0;
  }
  for (int i = 0; i < 4; i++) {   /* 960-962: out of life */
    Enemy *e = &B.en[i];
    if (e->alive && e->hurt < -max_life() && !e->s17) {
      e->s17 = 1, e->s25 = 1;
      if (B.boss == 19) g.f.resetpos = 1;
    }
    /* 740-741: the roll of an instant end (Esc Key, Unscrew) or the end of its life */
    if (e->alive && e->s25 > 0) {
      int r = e->s25;
      e->s25 = 0;
      if (r <= 10) kill_enemy(i);
      else fx_new(FX_SHOW, AN_SPARK, e->x - 54, e->y - 92);
    }
  }
}

static void party_hit(int k, int32_t d) {   /* "damage p N" (1025-1037), k 0-3 */
  if (d <= 0 || B.shield2 > 0) return;
  int32_t *l = life_of(k);
  if (*l <= 0) return;
  *l -= d;
  if (*l < 0) *l = 0;
  number(STOP[k][0], STOP[k][1] - 125, d, NUM_HURT);
  B.pm[k].t = 30;                      /* "character" F: blinks */
  B.shake = 10, B.flash = 100;
  B.hit_slot = (uint8_t)k;
  if (B.counter_bite) {                /* 1536: Counter: Bite */
    int t = pick_enemy();
    if (t >= 0) {
      Fx *f = fx_new(FX_BITE, AN_BITE2, B.en[t].x - 6, B.en[t].y - 85);
      if (f) f->target = (int8_t)t, f->a = (int16_t)(200 + B.cur_level * B.multiplier), f->b = 50;
    }
  }
}

static void party_heal(int32_t amount) {   /* "healed amount" -> "heal p 1" to 4 (632, 594-597) */
  for (int k = 0; k < 4; k++) {
    int32_t *l = life_of(k);
    if (*l <= 0) continue;
    int32_t h = amount + rnd_n(5);
    *l += h;
    int32_t m = B.life_max[(B.row - 1) * 4 + k];
    if (*l > m) *l = m;
    number(STOP[k][0] - 44, STOP[k][1] - 102, h, NUM_HEAL);
  }
}

/* ------------------------------------------------------------------ the attacks (467-476, 600-608, 659-675, 844-908,
 * 2041-2049): what each does at its picture */
static int attack_frame(int a) {
  switch (a) {
    case 6: case 7: case 43: return 1;
    case 8: return 2;
    case 32: return 3;
    case 3: case 4: case 11: case 13: case 15: return 8;
    case 9: case 17: case 21: case 22: case 23: case 24: case 25: case 31: case 35: case 36: case 37: case 38:
    case 39: case 40: case 41: case 42: return 7;
    default: return 6;
  }
}

static void do_attack(int a) {
  int power = B.cur_level * B.multiplier;   /* 587: "current attack power" */
  int level = B.cur_level;
  int att_x = B.pm[B.att_slot - 1].x, att_y = B.pm[B.att_slot - 1].y;
  int t;
  Fx *f;
  switch (a) {
    case 1:   /* Mic Toss: the mic flies to a target (478) */
      t = pick_enemy();
      if (t >= 0 && (f = fx_new(FX_MIC, AN_MICSPIN, att_x, att_y - 71))) f->target = (int8_t)t, f->a = (int16_t)power;
      break;
    case 2: case 5: case 19: case 51:   /* Bite, Bite 2, Toxic Bite 2, Mega Bite (468, 844-851) */
      t = pick_enemy();
      if (t < 0) break;
      f = fx_new(FX_BITE, a == 2 ? AN_BITE : a == 5 ? AN_BITE2 : a == 19 ? AN_BITE3 : AN_MEGABITE, B.en[t].x, B.en[t].y - 83);
      if (!f) break;
      f->target = (int8_t)t;
      f->a = (int16_t)(a == 2 ? 20 + power : a == 5 ? 200 + power : a == 19 ? 150 + power : 300 + power * 2);
      f->b = (int16_t)(a == 2 ? 5 : a == 51 ? 100 : 50);
      if (a == 19) f->t = 1;   /* (poisons) */
      B.shake = a == 51 ? 50 : 10;
      break;
    case 16:   /* Toxic Bite (469, 558) */
      t = pick_enemy();
      if (t >= 0 && (f = fx_new(FX_TOXICBITE, AN_TOXICBITE, B.en[t].x, B.en[t].y - 71))) f->target = (int8_t)t;
      break;
    case 3:   /* Hook (603, 634) */
      t = pick_enemy();
      if (t >= 0 && (f = fx_new(FX_HOOK, AN_HOOK, B.en[t].x, B.en[t].y - 72))) f->target = (int8_t)t, f->a = (int16_t)power;
      break;
    case 4:   /* Eye Beam: the beam over them all (604, 635) */
      if ((f = fx_new(FX_CLAW, AN_CLAW, att_x - 47, att_y - 156))) f->a = (int16_t)(power * 2);
      break;
    case 6: case 43:   /* Pizza Wheel, Pizza Wheel 2 (470, 471) */
      if (a == 6) B.pizzas = (int16_t)(3 + rnd_n(4)), B.pizza_pow = (int16_t)power;
      else B.pizzas2 = (int16_t)(3 + rnd_n(4)), B.pizza2_pow = (int16_t)(power + 20);
      B.shake = 100;
      break;
    case 7:   /* Bash Jam (472, 562) */
      if ((f = fx_new(FX_TORNADO, AN_TORNADO, 379 + 335, 319 + 189))) f->a = (int16_t)(power + rnd_n(20));
      B.shake = 100;
      break;
    case 8:   /* Jumpscare (605-606) */
      B.stunned = (int16_t)(B.boss ? 1 + rnd_n(3) : 2 + rnd_n(7));
      B.shake = 50;
      for (int i = 0; i < 4; i++)
        if (B.en[i].alive) fx_new(FX_SHOW, AN_SCARE, B.en[i].x, B.en[i].y - 177);
      break;
    case 9:   /* Balloons (664) */
      for (int k = 0; k < 3; k++)
        if ((f = fx_new(FX_BALLOON, AN_BALLOON0, att_x - 48, att_y - 186))) {
          f->b = (int16_t)rnd_n(4), f->an = (uint8_t)(AN_BALLOON0 + f->b), f->a = (int16_t)power;
          f->vx = (int16_t)(-20 - rnd_n(40)), f->vy = (int16_t)(-60 - rnd_n(40));
        }
      break;
    case 10:   /* Springlocks (899, 905) */
      t = pick_enemy();
      if (t >= 0 && (f = fx_new(FX_HEAD, AN_HEAD, B.en[t].x, B.en[t].y - 113))) f->target = (int8_t)t, f->a = (int16_t)power;
      break;
    case 11:   /* Cupcake (600, 617) */
      B.cup_t = 100, B.cup_level = (int16_t)level;
      for (int k = 0; k < 4; k++) fx_new(FX_SHOW, AN_SPARKLE, STOP[k][0], STOP[k][1] - 100);
      break;
    case 12: case 14:   /* Happy Jam, Happy Jam 2 (473, 857, 590) */
      B.hj_t = 60, B.hj_level = (int16_t)(a == 12 ? level : level + 75), B.hj_kind = (int16_t)(a == 12 ? 1 : 2);
      break;
    case 13:   /* Party Favors (601, 623) */
      B.favors_t = 15, B.favors_level = (int16_t)level;
      break;
    case 15:   /* Regen Song (602, 630) */
      B.regen_n = 10;
      for (int k = 0; k < 4; k++) fx_new(FX_NOTE, AN_NOTE, STOP[k][0], STOP[k][1] - 120);
      break;
    case 17:   /* Toxic Balloon (665, 715) */
      if ((f = fx_new(FX_TBALLOON, AN_BALLOON2, att_x - 57, att_y - 144))) f->vx = -30, f->vy = -60;
      break;
    case 20:   /* Bad Pizza (847, 856) */
      if ((f = fx_new(FX_BADPIZZA, AN_BADPIZZA, 215 + 200, 7 + 200))) f->a = (int16_t)power;
      B.shake = 100;
      break;
    case 21:   /* Gloom Balloon (666, 784) */
      if ((f = fx_new(FX_GLOOM, AN_BALLOON3, att_x - 58, att_y - 109))) f->vx = -40, f->vy = -40;
      break;
    case 22:   /* Gloom Song (669, 748) */
      B.pwr_cloud_t = 20;
      for (int k = 0; k < 3; k++) fx_new(FX_NOTE, AN_BADNOTE, att_x - 62 - 30 * k, att_y - 142);
      B.shake = 50;
      break;
    case 23:   /* Sludge (670) */
      B.spd_down = 30;
      for (int i = 0; i < 4; i++)
        if (B.en[i].alive) fx_new(FX_SHOW, AN_DRIP, B.en[i].x, B.en[i].y - 60);
      break;
    case 24: case 25:   /* Rainy Day, Rainy Day 2 (671-672, 793-794, 821) */
      B.pwr_cloud = 2, B.pwr_cloud_t = (int16_t)(a == 24 ? 30 : 60);
      fx_new(FX_CLOUD, AN_CLOUD, 214 + 120, 155);
      B.stars2 = (int16_t)(a == 25 ? 1 : 0);
      break;
    case 26:   /* Birthday (474) */
      B.atk_boost += 10, B.def_boost += 10, B.spd_boost += 10;
      break;
    case 27: B.atk_boost = 60, B.shake = 100; break;   /* Power Song (858) */
    case 28: B.def_boost = 60; break;                  /* Armor Song (848) */
    case 29: B.spd_boost = 60; break;                  /* Speed Song (659) */
    case 30:   /* Cosmic Song (866, 873-874) */
      B.stars = (int16_t)(7 + rnd_n(5)), B.star_pow = (int16_t)power;
      B.shake = 100;
      break;
    case 31: case 35:   /* Hot Cheese (607, 608, 644-649) */
      B.cheese_pow = (int16_t)(a == 31 ? level + 2 : level + 50);
      B.cheese_t = (int16_t)(a == 31 ? 3 : 7);
      break;
    case 32:   /* Munchies (661, 683-687) */
      for (int i = 0; i < 4; i++)
        if (B.en[i].alive) B.en[i].s12 = 1;
      B.pizza_pow = (int16_t)power;
      break;
    case 33:   /* Freddles (906, 909-918) */
      B.freddles = (int16_t)(5 + rnd_n(10)), B.freddle_pow = (int16_t)power;
      B.shake = 100;
      break;
    case 34:   /* Poppers (660, 676-681) */
      B.popper_n = (int16_t)(3 + rnd_n(6)), B.popper_pow = (int16_t)(power + 50 + rnd_n(50));
      break;
    case 36: case 37:   /* Prize Ball, Prize Ball 2 (662-663, 690-704, 815-825) */
      if ((f = fx_new(FX_PRIZE, a == 36 ? AN_PRIZE : AN_PRIZE2, 273 + 3, 82 - 97 + 150))) f->b = (int16_t)(a == 36 ? 0 : 2);
      break;
    case 38: case 39:   /* Mystery Box, Mystery Box 2 (674-675, 837-838) */
      if (B.team_ok[0] && B.team_ok[1]) B.mystery = 20, B.mystery_kind = (uint8_t)(a - 37);
      break;
    case 40:   /* Esc Key (673, 831-832) */
      B.dark = 100, B.dark_kind = 1;
      break;
    case 41: case 42:   /* Unscrew, Unscrew 2 (667-668, 717-739) */
      t = pick_enemy();
      if (t >= 0) {
        if (a == 41) B.en[t].s20 = (int16_t)(10 + rnd_n(5));
        else B.en[t].s21 = (int16_t)(10 + rnd_n(5));
      }
      break;
    case 44: case 45:   /* Waterhose, Waterhose 2 (475-476, 1005-1006) */
      f = fx_new(FX_SHOW, a == 44 ? AN_WATER : AN_WATER2, 400, 231);
      B.hose = (int16_t)(a == 44 ? 60 : -100);
      B.shake = 50;
      break;
    case 46:   /* Haunting (907, 921) */
      if ((f = fx_new(FX_HAUNT, AN_PBUBBLE, 868, 6 + 150))) f->vx = -80, f->vy = 30;
      break;
    case 47:   /* Endo Army (867, 879-885) */
      if (!B.endo_t) {
        B.endo_t = 1;
        for (int k = 0; k < 3; k++)
          if ((f = fx_new(FX_ENDO, AN_ENDO, 619 - 125 - 140 * k, 261 + 27 + 30 * k))) f->t = (int16_t)(30 * k);
      }
      break;
    case 48: B.shield = 30, B.flash = 200; break;   /* Neon Wall (898) */
    case 49: B.mimic = 10, B.mimic_attack = 49; break;   /* Mimic Ball (868) */
    case 50:   /* Gift Boxes (908) */
      for (int k = 0; k < 4; k++) B.gift[k] = 100;
      break;
    case 52:   /* Slasher (2041, 2050-2051) */
      if ((f = fx_new(FX_DAGGER, AN_DAGGER, att_x - 52, att_y - 123))) f->a = (int16_t)(rnd_n(10) + 1);
      break;
    case 53:   /* 4th Wall (2042, 2052) */
      fx_new(FX_WALL, AN_WALL, 379 + 49, 319 + 132);
      break;
    case 54:   /* Jack-O-Bomb (2043, 2053-2054) */
      if ((f = fx_new(FX_JACKO, AN_JACKO, 300 + rnd_n(200), 150))) f->vy = 0;
      break;
    case 55: B.newnums0 += (int16_t)(30 + rnd_n(30)); break;   /* Bubble Breath (2044) */
    case 56:   /* Balloons 2 (2045, 2060) */
      for (int k = 0; k < 9; k++)
        if ((f = fx_new(FX_BALLOON, AN_BALLOON0, att_x - 2, att_y - 115))) {
          f->b = (int16_t)rnd_n(4), f->an = (uint8_t)(AN_BALLOON0 + f->b), f->a = (int16_t)(power * 2 + 50);
          f->vx = (int16_t)(-20 - rnd_n(60)), f->vy = (int16_t)(-40 - rnd_n(60));
        }
      break;
    case 57:   /* Buzzsaw (2046, 2063) */
      if ((f = fx_new(FX_SAW, AN_SAW, att_x - 37, att_y - 123))) f->vx = -120;
      break;
    case 58: B.newnums1 += 60; break;   /* Mega Virus (2047, 2085) */
    case 59: B.newnums2 = 15, B.mystery_t = (uint8_t)rnd_n(2); break;   /* Hocus Pocus (2048, 2071) */
    case 60: B.shield2 = 5, B.flash = 200; break;   /* Neon Wall 2 (2049) */
  }
}

/* ------------------------------------------------------------------ the effects' steps */
static bool near(Fx *f, Enemy *e, int r) {
  int dx = f->x - e->x, dy = f->y - (e->y - 80);
  return dx * dx + dy * dy < r * r;
}
static void home(Fx *f, int tx, int ty, int sp) {   /* toward (tx, ty) at sp/16 pixels a frame */
  int dx = tx - f->x, dy = ty - f->y;
  int ad = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
  if (!ad) return;
  f->vx = (int16_t)(dx * sp / ad), f->vy = (int16_t)(dy * sp / ad);
}

static void fx_step(void) {
  for (int i = 0; i < NFX; i++) {
    Fx *f = &B.fx[i];
    if (!f->kind) continue;
    bool end = anim_step(f->an, &f->img, &f->cnt, &f->plays);
    Enemy *e = f->target >= 0 ? &B.en[f->target] : NULL;
    if (e && !e->alive) {   /* its target gone: another one */
      int t = pick_enemy();
      f->target = (int8_t)t;
      e = t >= 0 ? &B.en[t] : NULL;
    }
    f->t++;
    switch (f->kind) {
      case FX_SPARK:   /* "Active 5": falls, gone at its end (493-494) */
        f->y += f->vy / 16;
        if (end) f->kind = 0;
        break;
      case FX_SHOW: case FX_NOTE: case FX_HIT:
        f->y += f->vy / 16;
        if (end || f->t > 180) f->kind = 0;
        break;
      case FX_MIC:   /* 478: the mic flies to its target */
        if (!e) { f->kind = 0; break; }
        home(f, e->x, e->y - 80, 16 * 18);
        f->x += f->vx / 16, f->y += f->vy / 16;
        if (near(f, e, 40)) {
          hit_enemy(f->target, f->a + 10 + rnd_n(5));
          f->kind = 0;
        }
        break;
      case FX_PIZZA:   /* 483, 486: faster and faster to a target */
        if (!e) { f->kind = 0; break; }
        if (f->b < 100) f->b += 2;
        home(f, e->x, e->y - 80, f->b * 4 + 64);
        f->x += f->vx / 16, f->y += f->vy / 16;
        if (near(f, e, 40)) {
          hit_enemy(f->target, f->a + rnd_n(2) + 1);
          f->kind = 0;
        }
        break;
      case FX_BITE:   /* 557, 849-851: at the end of the bite */
        if (end) {
          if (e) {
            hit_enemy(f->target, f->a + rnd_n(f->b));
            if (f->t && e) e->s15 = (int16_t)(5 + rnd_n(4));
          }
          f->kind = 0;
        }
        break;
      case FX_TOXICBITE:   /* 558 */
        if (end) {
          if (e) {
            e->s15 = (int16_t)(5 + rnd_n(6)), e->s3 = 20;
            hit_enemy(f->target, 25 + rnd_n(25));
            for (int k = 0; k < 4; k++) fx_new(FX_SHOW, AN_PBUBBLE, e->x + rnd_n(80) - 40, e->y - 60 - rnd_n(60));
          }
          f->kind = 0;
        }
        break;
      case FX_HOOK:   /* 634 */
        if (end) {
          if (e) hit_enemy(f->target, f->a + 15 + rnd_n(5));
          f->kind = 0;
        }
        break;
      case FX_CLAW:   /* 635: the beam crosses them all */
        f->x -= 24;
        for (int k = 0; k < 4; k++)
          if (B.en[k].alive && !(f->b & 1 << k) && f->x < B.en[k].x) {
            f->b |= (int16_t)(1 << k);
            hit_enemy(k, f->a + 50 + rnd_n(30) + rnd_n(11) / 10 * 1000);
          }
        if (f->x < -100) f->kind = 0;
        break;
      case FX_TORNADO:   /* 561-562: after 20 frames, each of them */
        f->x -= 6;
        if (f->t > 20 && !f->b) {
          f->b = 1;
          for (int k = 0; k < 4; k++)
            if (B.en[k].alive) hit_enemy(k, f->a + rnd_n(6) + 10);
        }
        if (f->t > 60) f->kind = 0;
        break;
      case FX_BALLOON:   /* 705-716: up, then to a target */
        if (f->t < 40) {
          f->x += f->vx / 16, f->y += f->vy / 16;
          break;
        }
        if (!e) {
          int t = pick_enemy();
          if (t < 0) { f->kind = 0; break; }
          f->target = (int8_t)t, e = &B.en[t];
        }
        home(f, e->x, e->y - 102, 16 * 10);
        f->x += f->vx / 16, f->y += f->vy / 16;
        if (near(f, e, 36)) {
          hit_enemy(f->target, (f->a + rnd_n(3) + 5) * f->b + 1);   /* 714 */
          f->kind = 0;
        }
        break;
      case FX_TBALLOON:   /* 715: poison */
        if (f->t < 40) { f->x += f->vx / 16, f->y += f->vy / 16; break; }
        if (!e) {
          int t = pick_enemy();
          if (t < 0) { f->kind = 0; break; }
          f->target = (int8_t)t, e = &B.en[t];
        }
        home(f, e->x, e->y - 88, 16 * 8);
        f->x += f->vx / 16, f->y += f->vy / 16;
        if (near(f, e, 36)) {
          e->s3 = 20, e->s15 += (int16_t)(5 + rnd_n(6));
          B.shake = 10;
          f->kind = 0;
        }
        break;
      case FX_HEAD:   /* 905: after 100 frames */
        if (f->t > 100) {
          if (e) hit_enemy(f->target, f->a + 1 + rnd_n(1000) + 200);
          B.shake = 100, B.flash = 200;
          f->kind = 0;
        }
        break;
      case FX_BADPIZZA:   /* 856: after 25 frames, each */
        if (f->t > 25) {
          for (int k = 0; k < 4; k++)
            if (B.en[k].alive) B.en[k].s3 = 20, hit_enemy(k, f->a + rnd_n(700) + 1);
          f->kind = 0;
        }
        break;
      case FX_GLOOM:   /* 784: then their power down */
        f->x += f->vx / 16, f->y += f->vy / 16;
        if (f->t > 40) B.pwr_down = 30, f->kind = 0;
        break;
      case FX_CLOUD:
        f->x -= 2;
        if (f->t > 150) f->kind = 0;
        if (B.stars2 && f->t % 16 == 0 && rnd_n(2) == 1) {   /* 821: Rainy Day 2's rain */
          int t = pick_enemy();
          if (t >= 0) hit_enemy(t, B.cur_level * B.multiplier + rnd_n(5) + 5);
        }
        break;
      case FX_STAR: {   /* 874: from the sky to its target */
        if (!e) { f->kind = 0; break; }
        home(f, e->x, e->y - 80, 16 * 14);
        f->x += f->vx / 16, f->y += f->vy / 16;
        if (near(f, e, 40)) {
          hit_enemy(f->target, 25 + rnd_n(10) + f->a);
          f->kind = 0;
        }
        break;
      }
      case FX_FREDDLE:   /* 909-918: runs at them, bites now and then */
        f->x -= 4;
        if (f->t % 6 == 0)
          for (int k = 0; k < 4; k++)
            if (B.en[k].alive && f->x < B.en[k].x + 60 && f->x > B.en[k].x - 60 && rnd_n(3) == 1) {
              Fx *b = fx_new(FX_BITE, AN_BITE4, B.en[k].x + 2, B.en[k].y - 95);
              if (b) b->target = (int8_t)k, b->a = (int16_t)(f->a + 20), b->b = 10;
            }
        if (f->x < -100) f->kind = 0;
        break;
      case FX_MUNCHIE:   /* 683-687: three bites */
        if (!e) { f->kind = 0; break; }
        if (f->an == AN_MUNCHIE && f->t % 30 == 0 && rnd_n(2) == 1) f->an = AN_MUNCHIE2, f->img = 0, f->cnt = 0, f->plays = 1;
        else if (f->an == AN_MUNCHIE2 && end) {
          hit_enemy(f->target, f->a + 5 + rnd_n(5));
          f->an = AN_MUNCHIE, f->img = 0;
          if (++f->b > 2) f->kind = 0;
        }
        break;
      case FX_PRIZE:   /* 692-704, 815-825 */
        if (f->t == 31) {
          f->an = (uint8_t)(f->b ? AN_PRIZE2 : AN_PRIZE), f->img = 0, f->cnt = 0;
          f->a = (int16_t)(f->b ? rnd_n(9) + 1 : rnd_n(100) + 1);
        }
        if (f->t > 31 && end) {
          int power = B.cur_level * B.multiplier;
          B.flash = 100;
          if (!f->b) {
            int s = f->a;
            if (s == 1) hit_all(99, 99);
            else if (s < 15) B.cup_t = 100, B.cup_level = (int16_t)rnd_n(50);
            else if (s < 30) B.hose = 60;
            else if (s < 45) { for (int k = 0; k < 4; k++) if (B.en[k].alive) B.en[k].s12 = 1; B.pizza_pow = (int16_t)power; }
            else if (s < 65) B.stunned = (int16_t)(2 + rnd_n(6)), B.shake = 50;
            else if (s < 85) B.atk_boost = (int16_t)(10 + rnd_n(10)), B.def_boost = (int16_t)(10 + rnd_n(10)), B.spd_boost = (int16_t)(10 + rnd_n(10));
            else B.cheese_pow = (int16_t)(rnd_n(20) + 20), B.cheese_t = 5;
          } else {
            int s = f->a;
            Fx *n;
            if (s == 1)
              for (int k = 0; k < 6; k++) {
                if ((n = fx_new(FX_BALLOON, AN_BALLOON0, f->x, f->y - 109))) {
                  n->b = (int16_t)rnd_n(4), n->an = (uint8_t)(AN_BALLOON0 + n->b), n->a = (int16_t)power;
                  n->vx = (int16_t)(rnd_n(80) - 40), n->vy = (int16_t)(-40 - rnd_n(40));
                }
              }
            else if (s == 2) { if ((n = fx_new(FX_TBALLOON, AN_BALLOON2, f->x, f->y - 109))) n->vy = -60; }
            else if (s == 3) B.pwr_cloud = 2, B.pwr_cloud_t = 60, B.stars2 = 1, fx_new(FX_CLOUD, AN_CLOUD, 334, 155);
            else if (s == 4) { if ((n = fx_new(FX_GLOOM, AN_BALLOON3, f->x, f->y - 71))) n->vx = -40; }
            else if (s == 5) { int t = pick_enemy(); if (t >= 0) B.en[t].s21 = 10; }
            else if (s == 6) hit_all(99, 99);
            else if (s == 7) B.pizzas2 = (int16_t)(3 + rnd_n(4)), B.pizza2_pow = (int16_t)(power + 5);
            else if (s == 8) B.hose = -100;
            else B.freddles = (int16_t)(5 + rnd_n(10)), B.freddle_pow = (int16_t)power;
          }
          f->kind = 0;
        }
        break;
      case FX_HAUNT:   /* 921: the ghost over them makes them wait */
        f->x += f->vx / 16, f->y += f->vy / 16;
        if (f->t % 15 == 0)
          for (int k = 0; k < 4; k++)
            if (B.en[k].alive && !B.en[k].s18 && f->x < B.en[k].x + 80 && f->x > B.en[k].x - 80 && rnd_n(2) == 1)
              B.en[k].s18 = (int16_t)(5 + rnd_n(25));
        if (f->x < -100) f->kind = 0;
        break;
      case FX_ENDO:   /* 879-885: they claw now and then, about 25 s */
        if (f->t > 50 && f->t % 15 == 0 && rnd_n(2) == 1) {
          Fx *c = fx_new(FX_CLAW, AN_CLAW2, f->x - 85, f->y - 122);
          if (c) c->a = 0, c->b = 0;
          f->an = AN_ENDO2, f->img = 0;
        }
        if (f->t > 1500) f->kind = 0, B.endo_t = 0;
        break;
      case FX_DAGGER:   /* 2051: one in ten ends them all */
        f->x -= 10;
        if (end || f->t > 60) {
          if (f->a == 1) hit_all(99999, 1);
          f->kind = 0;
        }
        break;
      case FX_WALL:   /* 2052 */
        if (end || f->t > 60) {
          hit_all(3000, 3000);
          B.shake = 100;
          f->kind = 0;
        }
        break;
      case FX_JACKO:   /* 2054: falls, then pow */
        f->y += 8;
        if (end || f->t > 50) {
          fx_new(FX_SHOW, AN_POW, f->x, f->y);
          for (int k = 0; k < 4; k++)
            if (B.en[k].alive) hit_enemy(k, rnd_n(1000));
          B.shake = 100;
          f->kind = 0;
        }
        break;
      case FX_SAW:   /* 2063: each it meets */
        f->x += f->vx / 16;
        for (int k = 0; k < 4; k++)
          if (B.en[k].alive && !(f->b & 1 << k) && f->x < B.en[k].x + 40 && f->x > B.en[k].x - 40) {
            f->b |= (int16_t)(1 << k);
            hit_enemy(k, 999 * rnd_n(10));
          }
        if (f->x < -100) f->kind = 0;
        break;
      case FX_DEATH: {   /* 1814-1819, 1825: 9999 to a member, or three seconds off Neon Wall */
        if (f->t < 0) break;
        int tx = STOP[f->b][0], ty = STOP[f->b][1] - 90;
        home(f, tx, ty, 16 * 14);
        f->x += f->vx / 16, f->y += f->vy / 16;
        if ((f->x - tx) * (f->x - tx) + (f->y - ty) * (f->y - ty) < 1600) {
          if (B.shield > 0) B.shield = (int16_t)(B.shield > 3 ? B.shield - 3 : 0);
          else party_hit(f->b, 9999), fx_new(FX_SHOW, AN_POW, tx, ty);
          f->kind = 0;
        }
        break;
      }
      case FX_ENEMY_BALLOON:   /* 1316, 1325: an enemy's balloon to a member */
        home(f, STOP[f->b][0], STOP[f->b][1] - 90, 16 * 8);
        f->x += f->vx / 16, f->y += f->vy / 16;
        if ((f->x - STOP[f->b][0]) * (f->x - STOP[f->b][0]) + (f->y - STOP[f->b][1] + 90) * (f->y - STOP[f->b][1] + 90) < 1600) {
          party_hit(f->b, g.w.area * 2 + rnd_n(11));   /* 1317-1324 */
          f->kind = 0;
        }
        break;
      case FX_STINGER: case FX_LASER: case FX_SYTH:   /* the bytes' shots (484-485, 1743-1744) */
        if (!e) { f->kind = 0; break; }
        home(f, e->x, e->y - 70, 16 * 16);
        f->x += f->vx / 16, f->y += f->vy / 16;
        if (near(f, e, 40)) {
          if (f->kind == FX_STINGER) hit_enemy(f->target, 15 + rnd_n(10));
          else if (f->kind == FX_LASER) hit_enemy(f->target, 100 + rnd_n(50));
          else if (g.w.area != 50 && ((!B.boss && rnd_n(10) == 1) || (B.boss && rnd_n(2000) == 1))) kill_enemy(f->target);
          f->kind = 0;
        }
        break;
      case FX_POPPER:
        if (f->t > 600 || !enemies_alive()) f->kind = 0;
        break;
      case FX_SCREW:
        f->y += 6;
        if (f->t > 30) f->kind = 0;
        break;
      case FX_DRIP: case FX_PUDDLE:
        if (f->t > 600) f->kind = 0;
        break;
      default:
        if (end) f->kind = 0;
    }
  }
}

/* ------------------------------------------------------------------ a step */
static void row_switch(int to) {   /* 83-86: the other row comes */
  B.row = (uint8_t)to;
  for (int k = 0; k < 4; k++) B.pm[k].state = 3;   /* (they leave: 89-90) */
  B.ready = 0, B.att_slot = 0, B.used = 0;
  B.leave_t = 20;
}

static void enemy_attack_step(void) {
  int diff = g.f.diff ? g.f.diff : 1;
  int div = divisor(B.def_boost > 0, B.shield > 0, B.pwr_down > 0);
  int block = 0, armor = g.f.armor;
  for (int k = 0; k < 4; k++) {
    int b = g.f.bytes[k];
    block += b == 13 ? 5 : b == 14 ? 20 : b == 15 ? 50 : 0;   /* 1752-1755 */
  }
  if (every(T_STRENGTH, 250)) {   /* 1061, 1238, 1560 */
    if (B.boss) {
      int32_t s;
      boss_info(B.boss, diff, NULL, NULL, &s, NULL, NULL);
      B.strength = s * diff / div - block - armor * 10;
    } else if (B.chal) {
      B.strength = (B.chal_c * 5 * diff + rnd_n(5)) / div - block - armor * 10;
    } else {
      B.strength = enemy_strength(B.etype, diff, div, block, armor);
    }
    B.special_strength = (B.special == 4 ? 20 : B.special == 10 ? 1000 : 10) + rnd_n(5) / div - block;   /* 1331-1341 */
  }
  if (every(T_WHO, 200)) B.attacked_who = (uint8_t)(rnd_n(4) + 1);   /* 1038 */
  for (int n = 0; n < 4 && B.attacked_who; n++) {   /* 1276-1279: past the ones out */
    if (*life_of(B.attacked_who - 1) > 0) break;
    B.attacked_who = (uint8_t)(B.attacked_who % 4 + 1);
  }
  /* their speed (1009-1010), stopped while stunned, haunted or waiting (Quickstart) */
  bool tick = B.spd_down > 0 ? every(T_SPEED_SLOW, 100) : every(T_SPEED, 70);
  for (int i = 0; i < 4; i++) {
    Enemy *e = &B.en[i];
    if (!e->alive) continue;
    if (B.quick_t > 0 || e->s18 > 0) {   /* 1521-1522 */
      e->speed = (int16_t)(rnd_n(10) + 1);
      continue;
    }
    if (tick && !B.stunned) e->speed += (int16_t)(1 + rnd_n(2));
  }
  int32_t go = B.boss ? B.boss_speed : 100;
  if (every(T_ENEMY_GO, 500)) {   /* 1011-1012: one of the ready ones attacks */
    int cand[4], n = 0;
    for (int i = 0; i < 4; i++)
      if (B.en[i].alive && B.en[i].speed >= go && !B.en[i].s4) cand[n++] = i;
    if (n) {
      Enemy *e = &B.en[cand[rnd_n(n)]];
      e->s4 = 1, e->s9 = 1, e->path = 2, e->node = 0, e->node_t = 0;
    }
  }
  for (int i = 0; i < 4; i++) {
    Enemy *e = &B.en[i];
    if (!e->alive) continue;
    if (e->s4 > 0 && ++e->s4 > 30) e->s4 = 0, e->speed = 1;   /* 1013-1014 */
    if (e->speed >= go + 5 && e->s9 && B.attacked_who) {        /* 1015-1024 */
      e->s9 = 0;
      int k = B.attacked_who - 1;
      B.attacked_who = 0;
      int fxk = B.boss ? boss_info(B.boss, diff, NULL, NULL, NULL, NULL, NULL) : enemy_hit_effect(B.etype);
      fx_new(FX_HIT, fxk == 1 ? AN_HIT0 : AN_HIT12 + (fxk - 2), STOP[k][0], STOP[k][1] - 91);
      party_hit(k, B.strength);
    }
  }
}

static void special_step(void) {
  /* 1300: every 10 s, one in "special chance" */
  static const uint8_t CHANCE[12] = {0, 2, 2, 3, 3, 4, 3, 3, 3, 2, 4, 2};
  int sp = B.special;
  if (B.boss == 11 && !B.special_t && every(T_ROLL, 500)) sp = B.special = (uint8_t)(rnd_n(5) + 5);
  if (B.boss == 15 && !B.special_t && every(T_ROLL, 500)) sp = B.special = (uint8_t)(rnd_n(6) + 5);
  if (B.boss == 12 && !B.special_t && every(T_ROLL, 500)) sp = B.special = (uint8_t)(rnd_n(3) + 3);
  if (!B.boss && g.w.area == 50 && !B.special_t && every(T_ROLL, 500)) sp = B.special = (uint8_t)(rnd_n(7) + 5);
  if (sp && enemies_alive() && B.stage < 2 && every(T_SPECIAL, 10000) && rnd_n(CHANCE[sp]) == 1) B.special_t = 60;
  if (B.special_t > 0 && --B.special_t == 0 && enemies_alive()) {   /* 1299, 1301-1314 */
    int k;
    switch (sp) {
      case 1:   /* balloons at the party */
        for (int n = 0; n < 4; n++) {
          Fx *f = fx_new(FX_ENEMY_BALLOON, AN_BALLOON0 + rnd_n(3) + 1, B.en[0].x + 5, B.en[0].y - 215);
          if (f) f->b = (int16_t)rnd_n(4);
        }
        break;
      case 2:   /* the bad note: the party's boosts and gifts gone */
        B.shield = 0, B.atk_boost = B.def_boost = B.spd_boost = 0;
        for (k = 0; k < 4; k++) B.gift[k] = 0;
        fx_new(FX_SHOW, AN_BADNOTE, B.en[0].x + 25, B.en[0].y - 116);
        break;
      case 3:   /* unscrew a member */
        if (!B.block_unscrew) B.screw_t = 20, B.screw_who = (int16_t)(rnd_n(4) + 1);
        break;
      case 4: B.cheese = 5; break;   /* cheese */
      case 5: if (!B.block_unscrew) B.dark = 100, B.dark_kind = 2; break;
      case 6: B.poison_all = (int16_t)(5 + rnd_n(6)); break;   /* toxic */
      case 7: if (!B.block_confuse) B.confuse = (int16_t)(5 + rnd_n(10)); break;
      case 8: B.hose = 75, B.shake = 100; break;   /* waterhose */
      case 9: B.special18 = 20, B.shake = 100; break;
      case 10: B.wall_t = 60, B.shake = 100; break;
      case 11:   /* 1820-1824: five scythes at the party, one at a time */
        B.screw_t = 0;
        for (int n = 0; n < 5; n++) {
          Fx *f = fx_new(FX_DEATH, AN_DEATH, B.en[0].x - 3, B.en[0].y - 118);
          if (f) f->t = (int16_t)(-30 * n), f->b = (int16_t)rnd_n(4);
        }
        break;
    }
  }
  /* the specials playing */
  if (B.screw_t > 0 && every(T_SCREW, 100)) {   /* 1345-1356 */
    fx_new(FX_SCREW, AN_SCREW, STOP[B.screw_who - 1][0], STOP[B.screw_who - 1][1] - 160);
    if (--B.screw_t == 1) {
      *life_of(B.screw_who - 1) = 0;
      B.screw_t = 0;
    }
  }
  if (B.cheese > 0 && every(T_CHEESE, 1000)) {   /* 1357-1365 */
    B.cheese--;
    if (rnd_n(2) == 1 && B.attacked_who && *life_of(B.attacked_who - 1) > 0) party_hit(B.attacked_who - 1, B.special_strength);
  }
  if (B.poison_all > 0 && every(T_4000, 4000)) {   /* 1374-1375 */
    B.poison_all--;
    for (int n = 0; n < 4; n++) party_hit(n, B.special_strength + rnd_n(10));
  }
  if (B.confuse > 0) {   /* 1377-1382 */
    for (int n = 0; n < 4; n++) B.charge[(B.row - 1) * 4 + n] = (uint8_t)(1 + rnd_n(5));
    if (every(T_CONFUSE, 1000)) B.confuse--;
  }
  if (B.hose > 0 && sp == 8 && B.stage < 2) {   /* 1383-1392: half life or less: out */
    if (--B.hose == 1)
      for (int n = 0; n < 4; n++) {
        int32_t *l = life_of(n);
        if (*l < B.life_max[(B.row - 1) * 4 + n] / 2) *l = 0;
      }
  }
  if (B.dark > 0 && --B.dark == 1) {   /* 831-834 */
    if (B.dark_kind == 1) {
      for (int i = 0; i < 4; i++)
        if (B.en[i].alive && g.w.area != 50) B.en[i].s25 = (int16_t)(B.boss ? rnd_n(1000) + 1 : rnd_n(60) + 1);
    } else {
      for (int n = 0; n < 4; n++) {
        int32_t *l = life_of(n);
        *l -= 5000 * rnd_n(2);
        if (*l < 0) *l = 0;
      }
    }
  }
  if (B.wall_t > 0 && --B.wall_t == 1)   /* 1781-1782: the wall reaches them */
    for (int n = 0; n < 4; n++) party_hit(n, B.boss ? B.strength + rnd_n(50) : rnd_n(100) + 200);
  if (B.special18 > 0 && every(T_SNOW, 100) && --B.special18 == 1) {   /* 1776-1778: the snowballs */
    B.special18 = 0;
    for (int n = 0; n < 4; n++) party_hit(n, rnd_n(*life_of(n)));
  }
}

void battle_step(void) {
  ms3 += 50;
  B.step++;
  int diff = g.f.diff ? g.f.diff : 1;
  (void)diff;
  /* the members' places */
  B.moving = 0;
  for (int k = 0; k < 4; k++) {
    if (B.pm[k].state == 1) {   /* coming in (22-29, "character" movement 1) */
      if (B.pm[k].t > 0) { B.pm[k].t--; B.moving = 1; continue; }
      int step = PATH_STEP;
      while (step > 0 && B.pm[k].node < 39) {
        B.pm[k].x += PATH_IN[B.pm[k].node][0] * 0, B.pm[k].y += 0;
        int dx = PATH_IN[B.pm[k].node][0], dy = PATH_IN[B.pm[k].node][1];
        B.pm[k].x += (int16_t)dx, B.pm[k].y += (int16_t)dy;
        int len = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy) ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy);
        step -= len ? len : 1;
        B.pm[k].node++;
      }
      if (B.pm[k].node >= 39) B.pm[k].state = 2;
      B.moving = 1;
    } else if (B.pm[k].state == 3) {   /* leaving (89) */
      B.pm[k].x += 15;
      B.moving = 1;
      if (B.pm[k].x > 1100) B.pm[k].state = 0;
    }
    if (B.pm[k].state == 2 && B.pm[k].t > 0) B.pm[k].t--;
  }
  if (B.leave_t > 0 && --B.leave_t == 0) party_enter();   /* 87-88: the new row comes */
  if (B.att_slot) B.moving = 1;
  if (B.mimic_t > 0) B.moving = 1;
  for (int k = 0; k < 4; k++) {
    if (B.pm[k].state) anim_step(*life_of(k) > 0 ? AN_IDLE1 + 2 * (field_char(k) - 1) : AN_KO, &B.pm[k].img, &B.pm[k].cnt, &(uint8_t){0});
  }
  /* the enemies' places */
  for (int i = 0; i < 4; i++) {
    Enemy *e = &B.en[i];
    if (!e->alive) continue;
    uint8_t p = 1;
    anim_step(e->an, &e->img, &e->cnt, &p);
    if (e->path == 1 && e->node < 27) {
      int step = PATH_STEP;
      while (step > 0 && e->node < 27) {
        int dx = ENEMY_IN[e->node][0], dy = ENEMY_IN[e->node][1];
        e->x += (int16_t)dx, e->y += (int16_t)dy;
        int len = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy) ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy);
        step -= len ? len : 1;
        e->node++;
      }
    } else if (e->path == 2) {   /* the lunge and back */
      int n = e->node;
      if (n < 12) {
        e->x += ENEMY_LUNGE[n][0] / 2, e->y += ENEMY_LUNGE[n][1] / 2;
      } else if (n < 24) {
        e->x -= ENEMY_LUNGE[23 - n][0] / 2, e->y -= ENEMY_LUNGE[23 - n][1] / 2;
      } else
        e->path = 0;
      e->node++;
    }
    if (e->s5 > 0) e->s5--;
    if (e->s3 > 0) e->s3--;
  }
  /* charges (91-106): one point a second (0.7 with a speed boost) while nobody else in the row is full */
  bool fast = B.spd_boost > 0;
  if (B.stage < 2 && (fast ? every(T_CHARGE_FAST, 700) : every(T_CHARGE, 1000))) {
    uint8_t *c = &B.charge[(B.row - 1) * 4];
    for (int k = 0; k < 4; k++) {
      bool other_full = false;
      for (int j = 0; j < 4; j++)
        if (j != k && c[j] == 10) other_full = true;
      if (c[k] < 10 && !other_full) c[k]++;
    }
  }
  for (int k = 0; k < 8; k++)
    if (B.charge[k] > 10) B.charge[k] = 10;
  for (int k = 0; k < 8; k++)
    if (B.life[k] <= 0) B.charge[k] = 0;   /* 1268-1275 */
  /* the attack buttons (117-151, 1520) */
  B.ready_turn = !B.ready && !B.att_slot && B.mimic_t <= 0;
  if (!B.ready && !B.moving && B.stage < 2 && !B.mystery)
    for (int k = 0; k < 4; k++)
      if (B.charge[(B.row - 1) * 4 + k] >= 10 && *life_of(k) > 0) {
        B.ready = (uint8_t)(k + 1), B.sel = 0;
        break;
      }
  if (B.ready && (*life_of(B.ready - 1) <= 0 || B.stage >= 2)) B.ready = 0;
  /* the attack playing (419-466, 509-556) */
  if (B.att_slot) {
    int prev = B.att_img;
    bool end = anim_step(B.att_an, &B.att_img, &B.att_cnt, &B.att_plays);
    if (B.att_img != prev && B.att_img == attack_frame(B.used) && !B.att_frame_done) {
      B.att_frame_done = 1;
      do_attack(B.used);
      if (B.mimic > 0 && B.used != 49 && B.used != B.mimic_attack) B.mimic_attack = B.used;   /* 889-890 */
    }
    if (end) {
      if (!B.att_frame_done) do_attack(B.used);
      B.att_slot = 0;
      if (B.mimic > 0 && B.mimic_attack != 49 && B.used != 49) B.mimic_t = 175;   /* 890: the mimic does it again */
    }
  }
  if (B.mimic_t > 0 && --B.mimic_t == 0 && B.mimic > 0 && enemies_alive()) {   /* 892 */
    B.mimic--;
    do_attack(B.mimic_attack);
  }
  /* attack power (587, 809-812) */
  B.multiplier = (int16_t)((B.atk_boost > 0 ? 2 : 1) * (B.spd_down2 > 0 ? 2 : 1));
  /* the timed parts of attacks */
  if (B.cup_t > 0 && --B.cup_t == 0) party_heal(B.cup_level * 2 + 30 + rnd_n(10));   /* 617 */
  if (B.hj_t > 0) {   /* 590-592 */
    if (B.hj_t % 3 == 0) fx_new(FX_NOTE, B.hj_kind == 1 ? AN_NOTE : AN_NOTE2, 672 - 61 + rnd_n(120) - 60, 471 + 105 - 200 - rnd_n(80));
    if (--B.hj_t == 0) party_heal(B.hj_level * 2 + 30 + rnd_n(20));
  }
  if (B.favors_t > 0 && every(T_FAVORS, 70) && --B.favors_t == 1) {   /* 623 */
    B.favors_t = 0;
    party_heal(B.favors_level + 1);
    hit_all(B.favors_level + 1 + 1, 5);
  }
  if (B.regen_n > 0 && B.regen <= 0) B.regen = B.regen_n, B.regen_n = 0, party_heal(g.w.area + 1);   /* 630 */
  if (B.regen > 0 && every(T_5000, 5000)) B.regen--, party_heal(g.w.area + 1);                         /* 631 */
  if (B.pizzas > 0 && every(T_PIZZA, 200)) {   /* 481 */
    B.pizzas--;
    Fx *f = fx_new(FX_PIZZA, AN_PIZZA, 870, 158 + 100);
    if (f) f->target = (int8_t)pick_enemy(), f->a = B.pizza_pow;
  }
  if (B.pizzas2 > 0 && every(T_PIZZA2, 200)) {   /* 482 */
    B.pizzas2--;
    Fx *f = fx_new(FX_PIZZA, AN_PIZZA2, 870, 158 + 100);
    if (f) f->target = (int8_t)pick_enemy(), f->a = B.pizza2_pow;
  }
  if (B.freddles > 0 && every(T_FREDDLE, 200)) {   /* 909 */
    B.freddles--;
    Fx *f = fx_new(FX_FREDDLE, AN_FREDDLE, 870 + 1, 158 + 62 + 150);
    if (f) f->a = B.freddle_pow;
  }
  if (B.stars > 0 && every(T_STARS, 250)) {   /* 873 */
    int t = pick_enemy();
    if (t >= 0) {
      B.stars--;
      Fx *f = fx_new(FX_STAR, AN_STAR, B.en[t].x + 267, B.en[t].y - 279);
      if (f) f->target = (int8_t)t, f->a = B.star_pow;
    }
  }
  if (B.cheese_t > 0 && every(T_DRIP, 200)) {   /* 644-645: the drips, then puddles */
    B.cheese_t--;
    fx_new(FX_PUDDLE, AN_PUDDLE, 100 + rnd_n(250), 330 + rnd_n(90));
    B.cheese_pow = B.cheese_pow;
  }
  if (every(T_PUDDLE, 250)) {
    bool puddle = false;
    for (int i = 0; i < NFX; i++) puddle |= B.fx[i].kind == FX_PUDDLE;
    if (puddle && rnd_n(5) == 1) {   /* 649 */
      int t = pick_enemy();
      hit_enemy(t, B.cheese_pow + rnd_n(4) + 3);
    }
    for (int i = 0; i < 4; i++)   /* 683: munchies */
      if (B.en[i].alive && B.en[i].s12) {
        B.en[i].s12 = 0;
        Fx *f = fx_new(FX_MUNCHIE, AN_MUNCHIE, B.en[i].x + 69, B.en[i].y - 193 + 100);
        if (f) f->target = (int8_t)i, f->a = B.pizza_pow;
        break;
      }
  }
  if (B.popper_n > 0 && every(T_POPPER, 100)) {   /* 676 */
    B.popper_n--;
    fx_new(FX_POPPER, AN_POPPER, 200 + rnd_n(250), 300 + rnd_n(120));
    B.poppers_on = 1;
  }
  if (B.poppers_on && !B.popper_n && ++B.poppers_wait > 600) {   /* 678-681 */
    B.poppers_on = 0, B.poppers_wait = 0;
    for (int i = 0; i < NFX; i++)
      if (B.fx[i].kind == FX_POPPER) B.fx[i].kind = 0;
    hit_all(B.popper_pow, 5);
    B.shake = 50;
  }
  if (B.pwr_cloud > 0 && every(T_CLOUDS, 750)) {   /* 793-794: three clouds, then the curse */
    fx_new(FX_CLOUD, AN_CLOUD, 214 + rnd_n(300), 120 + rnd_n(60));
    if (--B.pwr_cloud == 0) B.spd_down2 = B.pwr_cloud_t;
  }
  if (B.pwr_cloud_t > 0 && B.pwr_cloud == 0 && !B.spd_down2 && !B.stars2) {   /* Gloom Song (746-748) */
    if (--B.pwr_cloud_t == 1) B.pwr_down = 30, B.pwr_cloud_t = 0;
  }
  if (B.hose != 0 && !B.boss) {   /* 1005-1006, 650-651 */
    int32_t m = B.enemy_max;
    for (int i = 0; i < 4; i++) {
      Enemy *e = &B.en[i];
      if (!e->alive) continue;
      if ((B.hose > 0 && e->hurt < -(m - m / 3)) || (B.hose < 0 && e->hurt < -(m - m / 2))) e->hurt = -m - 1;
    }
    if (B.hose > 0) B.hose--;
    else B.hose++;
  }
  if (B.mystery > 0 && every(T_MYSTERY, 70) && --B.mystery == 1) {   /* 837-838: a new party, for this battle */
    B.mystery = 0;
    int extra = rnd_n(5) + 1;
    int base = B.mystery_kind == 1 ? 1 : 9;
    for (int k = 0; k < 8; k++) {
      int c = rnd_n(4) + base + 4 * (k & 3) + extra;
      g.f.slot[k] = (uint8_t)(c > NCHAR ? NCHAR : c);
    }
    B.team_ok[0] = B.team_ok[1] = 1;
    row_switch(B.row == 1 ? 2 : 1);
  }
  if (B.newnums2 > 0 && every(T_HOCUS, 70) && --B.newnums2 == 1) {   /* 2071: Hocus Pocus */
    B.newnums2 = 0;
    if (B.mystery_t) B.etype = (uint8_t)(B.etype / (rnd_n(5) + 2));
  }
  if (B.newnums1 > 0 && every(T_VIRUS, 250) && rnd_n(2) == 1) hit_enemy(pick_enemy(), 99 * rnd_n(10));   /* 2085 */
  /* unscrews (717-741) */
  if (every(T_UNSCREW, 100))
    for (int i = 0; i < 4; i++) {
      Enemy *e = &B.en[i];
      if (!e->alive) continue;
      if (e->s20 > 0) {
        fx_new(FX_SCREW, AN_SCREW, e->x - 2, e->y - 160);
        if (--e->s20 == 1 && g.w.area != 50) e->s25 = (int16_t)(B.boss ? rnd_n(600) + 1 : rnd_n(30) + 1);
      }
      if (e->s21 > 0) {
        fx_new(FX_SCREW, AN_SCREW, e->x - 5, e->y - 160);
        if (--e->s21 == 1 && g.w.area != 50) e->s25 = (int16_t)(B.boss ? rnd_n(500) + 1 : rnd_n(20) + 1);
      }
    }
  /* poison (776): every 500 ms one in eight, a poisoned one */
  if (every(T_POISON, 500) && rnd_n(8) == 0) {
    int cand[4], n = 0;
    for (int i = 0; i < 4; i++)
      if (B.en[i].alive && B.en[i].s15 > 0) cand[n++] = i;
    if (n) {
      int i = cand[rnd_n(n)];
      B.en[i].s15--;
      hit_enemy(i, g.w.area * 2 + 10 + rnd_n(20));
    }
  }
  /* chips in battle: Evercomet (1519), Freddle: Fury (1532), Pizza: Fury (1538) */
  if (B.stage < 2 && B.comet && every(T_COMET, 1000) && rnd_n(10) < B.comet) {
    int t = pick_enemy();
    Fx *f = t >= 0 ? fx_new(FX_STAR, AN_STAR, B.en[t].x + 267, B.en[t].y - 279) : NULL;
    if (f) f->target = (int8_t)t, f->a = (int16_t)(B.cur_level * B.multiplier);
  }
  if (B.stage < 2 && B.freddle_fury && every(T_FURY, 250) && rnd_n(5) == 1) B.freddles++;
  if (B.stage < 2 && B.pizza_fury && every(T_PFURY, 1000) && rnd_n(3) == 1)
    B.pizzas = (int16_t)(rnd_n(3) + 3), B.pizza_pow = (int16_t)(B.cur_level * B.multiplier + rnd_n(3));
  /* bytes (1731-1759) */
  if (B.stage < 2 && enemies_alive())
    for (int k = 0; k < 4; k++) {
      int b = g.f.bytes[k];
      static const uint16_t PERIOD[22] = {0, 2000, 1000, 500, 5050, 5550, 6050, 5000, 2910, 890, 1000, 600, 400,
                                          0, 0, 0, 500, 410, 670, 0, 0, 0};
      if (!b) continue;
      if (b >= 19) {   /* BossDrain: a boss's life a frame */
        if (B.boss) B.en[0].hurt -= b - 18;
        continue;
      }
      if (!PERIOD[b] || !byte_every(k, PERIOD[b])) continue;
      int bx = 100 + 60 * k, by = 40;
      Fx *f;
      int t;
      switch (b) {
        case 1: case 2: case 3:
          if (rnd_n(3) == 1 && (t = pick_enemy()) >= 0 && (f = fx_new(FX_STINGER, AN_STINGER, bx, by))) f->target = (int8_t)t;
          break;
        case 4: party_heal(5 + rnd_n(6)); break;
        case 5: party_heal(10 + rnd_n(6)); break;
        case 6: party_heal(25 + rnd_n(6)); break;
        case 7: case 8: case 9:
          if (rnd_n(2) == 1 && (t = pick_enemy()) >= 0 && (f = fx_new(FX_SYTH, AN_SYTH, bx, by))) f->target = (int8_t)t;
          break;
        case 10: case 11: case 12:
          if (rnd_n(3) == 1 && (t = pick_enemy()) >= 0 && (f = fx_new(FX_LASER, AN_STINGER, bx, by))) f->target = (int8_t)t;
          break;
        case 16: case 17: case 18:
          if (!(B.bomb_used & 1 << k) && rnd_n(10) == 1) {
            B.bomb_used |= (uint8_t)(1 << k);
            fx_new(FX_SHOW, AN_POW, bx + 46, by + 49 + 100);
            if (b == 16) hit_all(100, 100);
            else if (b == 17) hit_all(200, 900);
            else hit_all(1000, 4000);
          }
          break;
      }
    }
  /* the second's countdowns (584-586, 758-760, 779, 902, 2059, 2066, 2074) */
  if (every(T_SECOND, 1000)) {
    if (B.atk_boost > 0) B.atk_boost--;
    if (B.def_boost > 0) B.def_boost--;
    if (B.spd_boost > 0) B.spd_boost--;
    if (B.pwr_down > 0) B.pwr_down--;
    if (B.spd_down > 0) B.spd_down--;
    if (B.spd_down2 > 0) B.spd_down2--;
    if (B.stunned > 0) B.stunned--;
    if (B.shield > 0) B.shield--;
    if (B.shield2 > 0) B.shield2--;
    if (B.newnums0 > 0) B.newnums0--;
    if (B.newnums1 > 0) B.newnums1--;
    for (int i = 0; i < 4; i++)
      if (B.en[i].s18 > 0) B.en[i].s18--;
  }
  if (B.quick_t > 0) B.quick_t--;
  /* the enemies */
  if (B.stage < 2) {
    enemy_attack_step();
    special_step();
  }
  fx_step();
  apply_hits();
  /* Bubble Breath's lava (2090): area 50 drains life */
  if (g.w.area == 50 && B.stage < 2 && B.newnums0 <= 0 && every(T_LAVA, 30))
    for (int n = 0; n < 4; n++)
      if (*life_of(n) > 0) --*life_of(n);
  /* gift boxes (1244-1251) */
  for (int n = 0; n < 4; n++)
    if (*life_of(n) <= 0 && B.gift[n] > 0) {
      *life_of(n) += 1000;
      int32_t m = B.life_max[(B.row - 1) * 4 + n];
      if (*life_of(n) > m) *life_of(n) = m;
      B.gift[n] = 0;
      B.flash = 100;
    }
  /* the rows (1280-1284, 84-85) */
  for (int r = 0; r < 2; r++) {
    B.team_ok[r] = 0;
    for (int n = 0; n < 4; n++) B.team_ok[r] |= B.life[r * 4 + n] > 0;
  }
  if (!B.team_ok[0] && !B.team_ok[1] && B.stage < 2) {   /* 1283-1284: lost */
    B.lost++;
    if (B.lost > 6) {
      g.f.resetpos = 1;
      B.on = 0;
      g.scene = SC_GAMEOVER, g.t = 0;
      return;
    }
  }
  if (!B.moving && B.leave_t == 0 && B.stage < 2 && !B.team_ok[B.row - 1] && B.team_ok[2 - B.row]) row_switch(3 - B.row);
  /* victory (964-965, 1076-1132) */
  if (B.stage == 1 && !enemies_alive()) {
    B.stage = 2;
    B.ready = 0;
    for (int k = 0; k < 8; k++) B.charge[k] = 0;
    int diff2 = g.f.diff ? g.f.diff : 1;
    B.exp = victory_exp(B.count, B.etype, B.boss, g.w.area, diff2);
    B.tokens = victory_tokens(B.count, B.etype, B.boss, g.w.area, diff2);
    if (B.chal) B.chal_won = 1;
    B.vt0 = B.vt1 = B.vt2 = 0;
  }
  if (B.stage == 2) {
    if (++B.vt0 > 50 && B.exp > 0 && every(T_EXP, 50)) {   /* 1086-1107 */
      int k = exp_tick(B.exp);
      B.exp -= k;
      for (int s = 0; s < 8; s++) {
        int c = g.f.slot[s];
        g.f.next[c] += (uint32_t)k;
        if ((int32_t)g.f.next[c] > exp_needed(c, g.f.lv[c])) {
          if (g.f.lv[c] < 255) g.f.lv[c]++;
          g.f.next[c] = 0;
          B.life[s] += 1000;
          if (B.life[s] > (B.life_max[s] = life_max(c, g.f.lv[c]))) B.life[s] = B.life_max[s];
          if (s / 4 == B.row - 1) B.leveled[s & 3] = 60;
        }
      }
    }
    if (B.vt0 > 50 && B.exp <= 0) B.vt1++;
    if (B.vt1 > 50 && B.tokens > 0 && every(T_TOKENS, 50)) {   /* 1095-1098 */
      for (int t = 0; t < 4; t++) {
        static const int32_t over[4] = {0, 10, 20, 50};
        if (B.tokens > over[t]) B.tokens--, g.f.tokens++;
      }
    }
    if (B.vt1 > 50 && B.tokens <= 0 && ++B.vt2 > 100) {   /* 1131-1132 */
      B.won = 1;
      B.on = 0;
    }
  }
  for (int k = 0; k < 4; k++)
    if (B.leveled[k] > 0) B.leveled[k]--;
  for (int i = 0; i < 12; i++)
    if (nums[i].t > 0) nums[i].t--, nums[i].y -= 1;
  if (B.shake > 0) B.shake--;
  if (B.flash > 0) B.flash = (int16_t)(B.flash > 10 ? B.flash - 10 : 0);   /* 689 */
}

/* ------------------------------------------------------------------ keys */
void battle_keys(uint32_t pressed, uint32_t held) {
  if (B.ready && !B.att_slot) {
    if (pressed & (K_UP | K_DIGIT(8))) B.sel = (uint8_t)((B.sel + 2) % 3);
    if (pressed & (K_DOWN | K_DIGIT(2))) B.sel = (uint8_t)((B.sel + 1) % 3);
    int pick = -1;
    if (pressed & (K_OK | K_EXE)) pick = B.sel;
    for (int k = 1; k <= 3; k++)
      if (pressed & K_DIGIT(k)) pick = k - 1;
    if (pick >= 0) {   /* 240-247 */
      int k = B.ready - 1, c = field_char(k);
      B.charge[(B.row - 1) * 4 + k] = 0;
      B.used = CHAR_ATTACKS[c][pick];
      B.att_slot = (uint8_t)(k + 1);
      B.att_an = (uint8_t)(AN_ATTACK1 + 2 * (c - 1));
      B.att_img = 0, B.att_cnt = 0, B.att_plays = ANIMS[B.att_an].plays, B.att_frame_done = 0;
      B.cur_level = (int16_t)level_of(k);
      B.ready = 0;
    }
  }
  /* SWITCH (83, 86) */
  if ((pressed & K_DEL) && !B.moving && B.stage < 2 && !B.mystery && B.leave_t == 0 && B.team_ok[2 - B.row])
    row_switch(3 - B.row);
  /* hold 0 to run (1602-1605) */
  B.running = (held & K_DIGIT(0)) && !B.boss && B.stage < 2 && !B.moving;
  if (B.running) {
    if (every(T_RUN, B.run_luck ? 100 : 200)) {
      int n = B.run_luck ? 5 + g.w.area : 10 + g.w.area * 2;
      if (rnd_n(n) == 1) B.on = 0, B.won = 0;
    }
    for (int k = 0; k < 8; k++) B.charge[k] = (uint8_t)(3 + rnd_n(5));
    B.ready = 0;
  }
}

/* ------------------------------------------------------------------ drawing */
static int shake_x, shake_y;

static void draw_actor_pic(int pic, int gx, int gy, int fade) {
  pic_fade(pic, SX(gx) + shake_x, SY(gy) + shake_y, fade);
}

static void bar(int x, int y, int w, int h, int32_t v, int32_t m, uint16_t c) {
  gfx_fill(x - 1, y - 1, w + 2, h + 2, BLACK);
  gfx_fill(x, y, w, h, RGB(0x303040));
  if (m > 0 && v > 0) gfx_fill(x, y, (int)(v * w / m < w ? v * w / m : w), h, c);
}

static const uint16_t CAP[8] = {0xFC00, 0xF800, 0xF81F, 0x07E0, 0x801F, 0xFFFF, 0xFFE0, 0x2104};

static void draw_button(int x, int y, int w, int a, bool sel) {
  gfx_fill(x, y, w, 18, sel ? RGB(0x5A78C8) : RGB(0x34488C));
  gfx_fill(x, y, 6, 18, CAP[ATTACK_COLOUR[a]]);
  gfx_frame_rect(x - 1, y - 1, w + 2, 20, sel ? WHITE : RGB(0x1A2448));
  if (sel) gfx_frame_rect(x - 2, y - 2, w + 4, 22, RGB(0xFFD040));
  gfx_text_c(FONT_SMALL, x + 3 + w / 2, y + 2, ATTACK_NAME[a], WHITE);
}

void battle_draw_band_dim(void) {
  int bg = 0;
  switch (g.w.area) {
    case 2: bg = PIC_BG3; break;
    case 5: bg = PIC_BG2; break;
    case 8: bg = PIC_BG4; break;
    case 11: case 17: bg = PIC_BG5; break;
    case 14: bg = PIC_BG6; break;
    case 20: bg = PIC_BG7; break;
    case 23: bg = PIC_BG8; break;
    case 26: bg = PIC_BG9; break;
    case 50: bg = PIC_BG11; break;
    default: bg = PIC_BG0;
  }
  if (gfx_y0 < FIELD_H) {
    pic_part(bg, shake_x - 2, shake_y - 2, 0, 0, 324, 196);
    /* actors by their feet */
    int order[8], n = 0;
    for (int i = 0; i < 4; i++)
      if (B.en[i].alive) order[n++] = i;
    for (int k = 0; k < 4; k++)
      if (B.pm[k].state) order[n++] = 4 + k;
    for (int i = 1; i < n; i++)
      for (int j = i; j > 0; j--) {
        int ya = order[j - 1] < 4 ? B.en[order[j - 1]].y : B.pm[order[j - 1] - 4].y;
        int yb = order[j] < 4 ? B.en[order[j]].y : B.pm[order[j] - 4].y;
        if (ya > yb) { int t = order[j]; order[j] = order[j - 1]; order[j - 1] = t; }
      }
    for (int i = 0; i < n; i++) {
      if (order[i] < 4) {
        Enemy *e = &B.en[order[i]];
        if (e->s5 && (e->s5 / 3) & 1) continue;   /* 487-488: blinks when hit */
        int fade = e->s18 > 0 ? 16 : 32;
        draw_actor_pic(pic_of(e->an, e->img), e->x, e->y, fade);
      } else {
        int k = order[i] - 4;
        if (B.att_slot == k + 1) {
          draw_actor_pic(pic_of(B.att_an, B.att_img), B.pm[k].x, B.pm[k].y, 32);
          continue;
        }
        if (B.pm[k].t > 0 && (B.pm[k].t / 3) & 1 && B.pm[k].state == 2) continue;
        int an = *life_of(k) > 0 ? AN_IDLE1 + 2 * (field_char(k) - 1) : AN_KO;
        draw_actor_pic(pic_of(an, *life_of(k) > 0 ? B.pm[k].img : 0), B.pm[k].x, B.pm[k].y, 32);
      }
    }
    for (int i = 0; i < NFX; i++) {
      Fx *f = &B.fx[i];
      if (!f->kind) continue;
      draw_actor_pic(pic_of(f->an, f->img), f->x, f->y, f->kind == FX_PUDDLE ? 24 : 32);
    }
    if (B.shield > 0) gfx_blend(SX(375), 0, 320 - SX(375), FIELD_H, RGB(0xC040FF), 5);
    if (B.shield2 > 0) gfx_blend(SX(375), 0, 320 - SX(375), FIELD_H, RGB(0x40FFFF), 6);
    if (B.dark > 0) gfx_dim(0, 0, 320, FIELD_H, 8);
    if (B.flash > 0) gfx_blend(0, 0, 320, FIELD_H, WHITE, B.flash > 255 ? 12 : B.flash * 12 / 255);
    for (int i = 0; i < 12; i++)
      if (nums[i].t > 0) {
        char s[12];
        int32_t v = nums[i].v;
        int p = 11;
        s[p] = 0;
        do s[--p] = (char)('0' + v % 10), v /= 10; while (v && p > 0);
        uint16_t c = nums[i].kind == NUM_HEAL ? RGB(0x60FF60) : nums[i].kind == NUM_HURT ? RGB(0xFF5050) : WHITE;
        gfx_text_s(FONT_TEXT, SX(nums[i].x) + shake_x, SY(nums[i].y) + shake_y, s + p, c);
      }
    /* the enemies' name, a boss's life */
    if (enemies_alive() && B.boss != 10) {
      const char *name = B.chal ? (B.chal_c >= 34 ? "???" : CHAR_NAME[B.chal_c]) : B.boss ? BOSS_NAME[B.boss] : ENEMY_NAME[B.etype];
      gfx_text_s(FONT_TEXT, 4, 3, name, WHITE);
      if (B.boss && B.boss != 15 && B.boss != 19) {
        int32_t l = B.boss_life + B.en[0].hurt;
        bar(110, 6, 120, 6, l, B.boss_life, RGB(0xE03030));
      }
    }
    if (B.special_t > 0 && B.special) {   /* the special's name ("enemy attack") */
      int an = B.special == 1 ? AN_SPECIAL0 : AN_SPECIAL12 + (B.special - 2);
      pic_draw(pic_of(an, 0), 160, 40);
    }
    if (B.stunned > 0)
      for (int i = 0; i < 4; i++)
        if (B.en[i].alive) pic_draw(pic_of(AN_DIZZY, (B.step / 3) % ANIMS[AN_DIZZY].n), SX(B.en[i].x + 4), SY(B.en[i].y - 118));
    /* the attacks of the member that is ready (137-151) */
    if (B.ready && !B.att_slot) {
      int k = B.ready - 1, c = field_char(k);
      int bx = SX(B.pm[k].x) - 150, by = SY(B.pm[k].y) - 80;
      if (bx < 4) bx = 4;
      if (by < 22) by = 22;
      if (by > FIELD_H - 70) by = FIELD_H - 70;
      for (int i = 0; i < 3; i++) draw_button(bx + 4 * i, by + 23 * i, 112, CHAR_ATTACKS[c][i], B.sel == i);
      gfx_img(IMG_UP, SX(B.pm[k].x) - 4, SY(B.pm[k].y - 260) + 2, RGB(0xFFD040));
    }
    if (B.stage == 2) {   /* VICTORY! Exp earned, Faz Tokens */
      int vp = pic_of(AN_VICTORY, ANIMS[AN_VICTORY].n - 1);
      pic_draw(vp, 160 - 50 + pic_hx(vp), 30 + pic_hy(vp));
      char s[16];
      int32_t v = B.exp;
      int p = 15;
      s[p] = 0;
      do s[--p] = (char)('0' + v % 10), v /= 10; while (v && p > 0);
      gfx_text_c(FONT_TEXT, 160, 30 + 43, s + p, RGB(0xFFE060));
      v = B.tokens, p = 15;
      do s[--p] = (char)('0' + v % 10), v /= 10; while (v && p > 0);
      gfx_text_c(FONT_TEXT, 160, 30 + 74, s + p, RGB(0xFFE060));
      for (int k = 0; k < 4; k++)
        if (B.leveled[k]) pic_draw(pic_of(AN_LEVELUP, 0), SX(B.pm[k].x + 4), SY(B.pm[k].y - 181));
    }
    if (B.running) pic_draw(pic_of(AN_RUN, (B.step / 4) & 1), 160, 90);
  }
  /* the party's panel */
  if (gfx_y0 + BAND_H > FIELD_H) {
    gfx_fill(0, FIELD_H, 320, 48, RGB(0x10183A));
    gfx_fill(0, FIELD_H, 320, 1, RGB(0x6080D0));
    for (int k = 0; k < 4; k++) {
      int x = 2 + 80 * k, c = field_char(k);
      int32_t l = *life_of(k), m = B.life_max[(B.row - 1) * 4 + k];
      bool ready = B.ready == k + 1;
      if (ready) gfx_fill(x - 1, FIELD_H + 2, 78, 30, RGB(0x2A4A9A));
      char name[24];
      int n = 0;
      for (const char *s = CHAR_NAME[c]; *s && n < 23; s++) name[n++] = *s;
      name[n] = 0;
      while (gfx_text_w(FONT_SMALL, name) > 74 && n > 0) name[--n] = 0;
      gfx_text(FONT_SMALL, x + 1, FIELD_H + 3, name, l > 0 ? WHITE : RGB(0x707070));
      bar(x + 1, FIELD_H + 18, 72, 5, l, m, l > m / 3 ? RGB(0x40E040) : RGB(0xE04040));
      bar(x + 1, FIELD_H + 26, 72, 3, B.charge[(B.row - 1) * 4 + k], 10, RGB(0xFFD040));
    }
    const char *keys = B.stage == 2 ? "" : B.ready ? "OK attack  1 2 3  Back pause" : "Back pause";
    gfx_text(FONT_SMALL, 3, FIELD_H + 34, keys, RGB(0xB0C0E0));
    if (B.stage < 2 && B.team_ok[2 - B.row] && !B.moving) {
      gfx_text_r(FONT_SMALL, 317, FIELD_H + 34, "switch  hold 0 run", RGB(0xB0C0E0));
      gfx_img(IMG_DEL, 317 - gfx_text_w(FONT_SMALL, "switch  hold 0 run") - 16, FIELD_H + 36, RGB(0xB0C0E0));
    }
    else if (B.stage < 2 && !B.boss) gfx_text_r(FONT_SMALL, 317, FIELD_H + 34, "hold 0 run", RGB(0xB0C0E0));
  }
}

void battle_prepare_view(void) { shake_x = shake_y = 0; }

void battle_draw(void) {
  shake_x = shake_y = 0;
  if (B.shake > 0) shake_x = rnd_n(5) - 2, shake_y = rnd_n(5) - 2;   /* "follow 2": the camera shakes */
  gfx_frame(battle_draw_band_dim);
}
