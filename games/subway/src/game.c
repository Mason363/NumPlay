/* The run (game.h): the runner's moves, what it runs into, coins and power-ups, the score, and the
 * missions and prices kept between runs. */
#include <string.h>
#include "game.h"
#include "plat.h"
#include "save.h"

/* the game's own numbers (its character motor), in metres */
#define GRAVITY (200 * U)
#define JUMP_V 11.63f      /* 20 units up (2.6 m): sqrt(2 g h) */
#define SUPER_V 16.45f     /* Super Sneakers: twice as high, onto the trains */
#define DIVE_V (-90 * U)   /* down in the air: a dive into a roll */
#define ROLL_T 0.6f
#define LANE_T 0.27f       /* from one track to the next */
#define STAND_H (9.2f * U) /* the runner's collider */
#define ROLL_H (4.5f * U)
#define FLY_Y 8.0f
#define FLY_LOW 5.0f       /* (in a tunnel, under a roof) */
#define PW 0.3f            /* the runner's half width */
#define SPEED0 (110 * U)   /* from 110 units a second to 220, over 179 s */
#define SPEED1 (220 * U)
#define SPEED_T 179.0f

Runner pl;
Run run;
Progress pg;
Spark sparks[NSPARKS];
static int next_spark;

const char *const char_names[NCHARS] = {"Jake", "Tricky", "Fresh", "Spike"};
const uint32_t char_price[NCHARS] = {0, 6000, 12000, 20000};
const char *const board_names[NBOARDS] = {"Hoverboard", "Bouncer", "Daredevil"};
const char *const board_power[NBOARDS] = {"The classic", "Super jump", "Speed up"};
const uint32_t board_price[NBOARDS] = {0, 7500, 15000};

float power_time(int pw) {
  /* the game's power-up configs: each level's time */
  static const float t[2][MAXUPG + 1] = {{8, 9, 10.5f, 12.5f, 15, 19, 25}, {10, 11.5f, 13.4f, 15.8f, 19, 24, 30}};
  int l = pg.upg[pw] > MAXUPG ? MAXUPG : pg.upg[pw];
  return t[pw != PW_JETPACK][l];
}

int upgrade_cost(int level) {
  static const uint16_t c[MAXUPG] = {500, 1000, 2500, 5000, 15000, 50000};
  return level < MAXUPG ? (int)c[level] : 0;
}

int multiplier(void) {
  int m = 1 + pg.mset;
  if (m > 30) m = 30;
  return m + (run.boosted ? 5 : 0);
}

static void toast(const char *s) {
  int i = 0;
  for (; s[i] && i < (int)sizeof run.toast - 1; i++) run.toast[i] = s[i];
  run.toast[i] = 0;
  run.toast_t = 2.2f;
}

/* ---------------------------------------------------------------- missions */
enum {
  M_COINS, M_COINS_RUN, M_JUMPS, M_JUMPS_RUN, M_ROLLS, M_ROLLS_RUN, M_SCORE_RUN, M_POWERUPS, M_POWERUPS_RUN,
  M_BOARDS, M_STUMBLES, M_LANES_RUN, M_DIST_RUN, M_ROOF_RUN, M_JETPACKS, M_MAGNETS, M_MYSTERY, M_RUNS, M_KINDS
};
static const char *const mission_fmt[M_KINDS] = {
  "Collect # coins", "Collect # coins in one run", "Jump # times", "Jump # times in one run", "Roll # times",
  "Roll # times in one run", "Score # points in one run", "Pick up # power-ups", "Pick up # power-ups in one run",
  "Use # hoverboards", "Stumble # times", "Change lanes # times in one run", "Run # m in one run",
  "Run # m on top of trains in one run", "Pick up # jetpacks", "Pick up # coin magnets", "Open # mystery boxes",
  "Play # games",
};
/* what each mission counts: a total kept between runs, or -1 for the run's own */
static const int8_t mission_count[M_KINDS] = {
  CNT_COINS, -1, CNT_JUMPS, -1, CNT_ROLLS, -1, -1, CNT_POWERUPS, -1, CNT_BOARDS, CNT_STUMBLES, -1, -1, -1,
  CNT_JETPACKS, CNT_MAGNETS, CNT_MYSTERY, CNT_RUNS,
};
static const uint32_t first_sets[8][3][2] = {
  {{M_COINS, 100}, {M_JUMPS, 10}, {M_ROLLS, 10}},
  {{M_SCORE_RUN, 5000}, {M_POWERUPS_RUN, 2}, {M_LANES_RUN, 30}},
  {{M_COINS_RUN, 150}, {M_BOARDS, 1}, {M_STUMBLES, 3}},
  {{M_DIST_RUN, 1000}, {M_JUMPS_RUN, 25}, {M_MAGNETS, 3}},
  {{M_SCORE_RUN, 15000}, {M_ROLLS_RUN, 15}, {M_ROOF_RUN, 150}},
  {{M_COINS_RUN, 400}, {M_JETPACKS, 3}, {M_MYSTERY, 1}},
  {{M_RUNS, 10}, {M_POWERUPS, 10}, {M_LANES_RUN, 80}},
  {{M_SCORE_RUN, 30000}, {M_COINS, 2500}, {M_ROOF_RUN, 400}},
};

void mission_get(int set, int i, Mission *m) {
  if (set < 8) {
    m->kind = (uint8_t)first_sets[set][i][0], m->target = first_sets[set][i][1];
    return;
  }
  /* later sets: three different kinds in turn, each harder than the last time */
  static const uint8_t cycle[12] = {M_SCORE_RUN, M_COINS_RUN, M_JUMPS_RUN, M_ROLLS_RUN, M_DIST_RUN, M_POWERUPS_RUN,
                                    M_ROOF_RUN, M_COINS, M_LANES_RUN, M_BOARDS, M_MAGNETS, M_JETPACKS};
  uint32_t s = (uint32_t)(set - 8);
  int k = cycle[(s * 3 + (uint32_t)i) % 12];
  uint32_t n = s / 4;
  uint32_t t;
  switch (k) {
    case M_SCORE_RUN: t = 40000 + 15000 * n; break;
    case M_COINS_RUN: t = 450 + 80 * n; break;
    case M_JUMPS_RUN: t = 30 + 6 * n; break;
    case M_ROLLS_RUN: t = 20 + 5 * n; break;
    case M_DIST_RUN: t = 1500 + 300 * n; break;
    case M_POWERUPS_RUN: t = 3 + n / 2; break;
    case M_ROOF_RUN: t = 400 + 100 * n; break;
    case M_COINS: t = 3000 + 1000 * n; break;
    case M_LANES_RUN: t = 100 + 20 * n; break;
    case M_BOARDS: t = 2 + n; break;
    case M_MAGNETS: t = 3 + n; break;
    default: t = 3 + n; break;
  }
  m->kind = (uint8_t)k, m->target = t;
}

void mission_text(const Mission *m, char *out, int n) {
  char num[12];
  int k = 0;
  uint32_t v = m->target;
  do num[k++] = (char)('0' + v % 10), v /= 10;
  while (v);
  int o = 0;
  for (const char *s = mission_fmt[m->kind]; *s && o < n - 1; s++) {
    if (*s != '#') {
      out[o++] = *s;
      continue;
    }
    for (int i = k - 1; i >= 0 && o < n - 1; i--) {
      out[o++] = num[i];
      /* thousands with a comma */
      if (i && i % 3 == 0 && o < n - 1) out[o++] = ',';
    }
  }
  out[o] = 0;
}

/* the run's own count of what a mission counts (not yet in the totals) */
static uint32_t run_count(int cnt) {
  switch (cnt) {
    case CNT_COINS: return run.coins;
    case CNT_JUMPS: return (uint32_t)run.jumps;
    case CNT_ROLLS: return (uint32_t)run.rolls;
    case CNT_POWERUPS: return (uint32_t)run.powerups;
    case CNT_BOARDS: return (uint32_t)run.boards_used;
    case CNT_STUMBLES: return (uint32_t)run.stumbles;
    case CNT_JETPACKS: return (uint32_t)run.jetpacks;
    case CNT_MAGNETS: return (uint32_t)run.magnets;
    case CNT_MYSTERY: return (uint32_t)run.mystery;
    default: return 0;
  }
}

static bool in_run;   /* the run's counts are not in the totals yet */

uint32_t mission_progress(int i) {
  Mission m;
  mission_get(pg.mset, i, &m);
  int cnt = mission_count[m.kind];
  if (cnt >= 0) {
    uint32_t now = pg.total[cnt] + (in_run ? run_count(cnt) : 0);
    return now > pg.mbase[i] ? now - pg.mbase[i] : 0;
  }
  if (!in_run) return 0;
  switch (m.kind) {
    case M_COINS_RUN: return run.coins;
    case M_JUMPS_RUN: return (uint32_t)run.jumps;
    case M_ROLLS_RUN: return (uint32_t)run.rolls;
    case M_SCORE_RUN: return run.score;
    case M_POWERUPS_RUN: return (uint32_t)run.powerups;
    case M_LANES_RUN: return (uint32_t)run.lanes;
    case M_DIST_RUN: return (uint32_t)run.dist;
    case M_ROOF_RUN: return (uint32_t)run.roof;
    default: return 0;
  }
}

static void new_set(void) {
  for (int i = 0; i < 3; i++) {
    Mission m;
    mission_get(pg.mset, i, &m);
    int cnt = mission_count[m.kind];
    pg.mbase[i] = cnt >= 0 ? pg.total[cnt] + (in_run ? run_count(cnt) : 0) : 0;
  }
}

void missions_check(void) {
  for (int i = 0; i < 3; i++) {
    if (pg.mdone & 1 << i) continue;
    Mission m;
    mission_get(pg.mset, i, &m);
    if (mission_progress(i) >= m.target) {
      pg.mdone |= (uint8_t)(1 << i);
      toast("Mission complete!");
    }
  }
  if (pg.mdone == 7) {
    if (pg.mset < 250) pg.mset++;
    pg.mdone = 0;
    new_set();
    char s[24] = "Multiplier x";
    int m = 1 + pg.mset > 30 ? 30 : 1 + pg.mset, k = 12;
    if (m >= 10) s[k++] = (char)('0' + m / 10);
    s[k++] = (char)('0' + m % 10);
    s[k] = 0;
    toast(s);
  }
}

/* ---------------------------------------------------------------- the run */
void run_start(void) {
  memset(&run, 0, sizeof run);
  memset(&pl, 0, sizeof pl);
  for (int i = 0; i < NSPARKS; i++) sparks[i].t = 1;
  pl.lane = pl.from_lane = 1;
  pl.speed = SPEED0;
  pl.chase = 3.2f;   /* the Inspector and his dog, right behind from the start */
  in_run = true;
  run.multiplier = multiplier();
}

int run_save_cost(void) { return 1 << (run.saves > 6 ? 6 : run.saves); }

void run_revive(void) {
  int cost = run_save_cost();
  if (pg.keys < (uint32_t)cost) return;
  pg.keys -= (uint32_t)cost;
  run.saves++;
  pl.dead = false;
  pl.state = ST_RUN;
  pl.crash_t = 0;
  pl.safe = 2.5f;
  pl.chase = 0;
  pl.stumble = 0;
  pl.roll = 0;
  pl.x = LANE_X(pl.lane);
  world_clear(pl.z - 6, pl.z + 40);
  pl.y = world_ground(pl.x, pl.z, 100);
  pl.vy = 0;
  save_write();
}

bool run_over(void) { return pl.dead && pl.crash_t > 1.2f; }

void run_bank(void) {
  if (!in_run) return;
  pg.coins += run.coins;
  pg.keys += run.keys;
  if (run.score > pg.best) pg.best = run.score;
  pg.runs++;
  for (int c = 0; c < CNT_COUNT; c++)
    if (c != CNT_RUNS && c != CNT_DIST) pg.total[c] += run_count(c);
  pg.total[CNT_RUNS]++;
  pg.total[CNT_DIST] += (uint32_t)run.dist;
  missions_check();
  in_run = false;
  /* (missions done in this run are kept, the next run's count from here) */
  save_write();
}

static void crash(void) {
  pl.ev |= EV_CRASH;
  if (pl.hover > 0) {
    /* the board breaks instead */
    pl.hover = 0;
    pl.safe = 1.6f;
    toast("Hoverboard lost!");
    return;
  }
  pl.dead = true;
  pl.state = ST_CRASH;
  pl.crash_t = 0;
  pl.chase = 1;
}

static void stumble(void) {
  run.stumbles++;
  pl.stumble = 0.45f;
  pl.ev |= EV_STUMBLE;
  if (pl.chase > 0 && pl.hover <= 0) {
    /* stumbling twice: the Inspector catches up */
    pl.dead = true;
    pl.state = ST_CRASH;
    pl.crash_t = 0;
    return;
  }
  pl.chase = 5.0f;   /* (the game's stumble: the Inspector stays right behind for 5 s) */
}

static void collect(Item *it) {
  Spark *sp = &sparks[next_spark++ % NSPARKS];
  sp->x = it->x, sp->y = it->y, sp->z = it->z, sp->t = 0;
  switch (it->kind) {
    case I_COIN:
      run.coins++;
      break;
    case I_KEY:
      run.keys++;
      toast("+1 key");
      break;
    case I_MYSTERY: {
      run.mystery++;
      run.powerups++;
      uint32_t r = plat_random() % 100;
      if (r < 50) {
        uint32_t c = 100 + plat_random() % 5 * 100;
        run.coins += c;
        toast(c >= 400 ? "Mystery Box: lots of coins!" : "Mystery Box: coins!");
      } else if (r < 65) run.keys++, toast("Mystery Box: a key!");
      else if (r < 80) pg.boards++, toast("Mystery Box: a hoverboard!");
      else if (r < 90) pg.headstarts++, toast("Mystery Box: a headstart!");
      else if (r < 98) pg.boosters++, toast("Mystery Box: a score booster!");
      else run.coins += 5000, toast("Mystery Box: JACKPOT!");
      break;
    }
    default: {
      int pw = it->kind == I_JETPACK ? PW_JETPACK : it->kind == I_SNEAKERS ? PW_SNEAKERS : it->kind == I_MAGNET ? PW_MAGNET : PW_DOUBLE;
      pl.power[pw] = power_time(pw);
      run.powerups++;
      if (pw == PW_JETPACK) {
        run.jetpacks++;
        pl.fly = FLY_Y;
        pl.roll = 0;
        world_sky_coins(pl.z + 12, pl.z + 12 + pl.speed * pl.power[pw]);
      }
      if (pw == PW_MAGNET) run.magnets++;
    }
  }
}

/* what the runner runs into at its place */
static void collide(float prev_x) {
  float h = pl.roll > 0 ? ROLL_H : STAND_H;
  for (int i = 0; i < nobs; i++) {
    Obstacle *o = &obs[i];
    if (o->z1 < pl.z - 0.25f || o->z0 > pl.z + 0.35f) continue;
    float lx = LANE_X(o->lane), hw = o->type <= O_RAMP ? TRAIN_W : 1.05f;
    if (pl.x < lx - hw - PW || pl.x > lx + hw + PW) continue;
    if (pl.stumble > 0 && o->lane != pl.lane) continue;   /* (bouncing back off it) */
    float bottom = 0, topy;
    switch (o->type) {
      case O_TRAIN:
      case O_MOVING: topy = TRAIN_H; break;
      case O_RAMP: topy = TRAIN_H * (pl.z - o->z0) / RAMP_LEN; break;
      /* (the barriers' shapes: the low one solid up to 12.5 units; the tall one open below 9 units, its board
       * and sign up to 29; the one on trestles open below 9.5, its board up to 15) */
      case O_HURDLE: topy = 12.5f * U; break;
      case O_HIGHBAR: bottom = 8.5f * U, topy = 29 * U; break;
      default: bottom = 8.5f * U, topy = 15 * U; break;
    }
    if (pl.y >= topy - 0.08f || pl.y + h <= bottom) continue;
    if ((o->type == O_TRAIN || o->type == O_MOVING) && pl.y >= topy - 0.6f) {
      /* nearly up there: onto the roof (or over it, on the way up) */
      if (pl.vy <= 0) pl.y = pl.ground = topy, pl.vy = 0, pl.state = ST_RUN;
      continue;
    }
    if (pl.safe > 0) continue;
    bool from_side = prev_x < lx - hw - PW + 0.15f || prev_x > lx + hw + PW - 0.15f;
    if (o->type == O_RAMP) {
      if (from_side && pl.y < topy - 0.7f) stumble(), pl.lane = pl.from_lane;
      continue;
    }
    if (from_side) {
      stumble();
      pl.lane = pl.from_lane;
      return;
    }
    crash();
    return;
  }
}

void run_step(uint32_t keys, uint32_t pressed) {
  if (run.toast_t > 0) run.toast_t -= DT;
  for (int i = 0; i < NSPARKS; i++)
    if (sparks[i].t < 1) sparks[i].t += DT;
  if (pl.dead) {
    pl.crash_t += DT;
    if (pl.y > pl.ground) pl.y -= 4 * DT;
    return;
  }
  run.start_t += DT;
  pl.t += DT;
#if defined(SS_GOD) || defined(SS_BENCH)
  pl.safe = 0.1f;   /* (a build for measuring speed: nothing hurts) */
#endif
  /* faster and faster: the game's speed, from 110 units a second to 220 over 179 s */
  pl.speed = SPEED0 + (SPEED1 - SPEED0) * (pl.t < SPEED_T ? pl.t / SPEED_T : 1);
  float speed = pl.speed;
  if (pl.hover > 0 && pg.board == 2) speed *= 1.12f;
  if (pl.headstart > 0) speed = speed * 3 > 50 ? speed * 3 : 50;
  pl.ev = 0;

  /* the first seconds: a headstart (1) or a score booster (3) */
  if (run.start_t < 5) {
    if (pressed & K_ONE && pg.headstarts && pl.headstart <= 0) {
      pg.headstarts--;
      pl.headstart = 6;   /* (the game's headstart: 6 s)*/
      pl.fly = FLY_Y;
      world_clear(pl.z, pl.z + 260);
      world_sky_coins(pl.z + 10, pl.z + 240);
      run.powerups++;
    }
    if (pressed & K_THREE && pg.boosters && !run.boosted) {
      pg.boosters--;
      run.boosted = true;
      run.multiplier = multiplier();
      toast("Score booster: +5");
    }
  }
  /* the hoverboard */
  if (pl.board_wait > 0) pl.board_wait -= DT;
  if (pressed & K_OK && pl.hover <= 0 && pg.boards && pl.fly <= 0 && pl.board_wait <= 0) {
    pg.boards--;
    pl.hover = 30;   /* (the game's hoverboard: 30 s, then 5 s before the next) */
    pl.board_wait = 35;
    run.boards_used++;
  }

  /* across */
  float prev_x = pl.x;
  if (pressed & K_LEFT && pl.lane > 0) pl.from_lane = pl.lane, pl.lane--, run.lanes++, pl.ev |= EV_LEFT;
  if (pressed & K_RIGHT && pl.lane < 2) pl.from_lane = pl.lane, pl.lane++, run.lanes++, pl.ev |= EV_RIGHT;
  /* (the game's lane change: 0.27 s, straight across) */
  float tx = LANE_X(pl.lane), dx = tx - pl.x, step = LANE / LANE_T * DT;
  pl.x += dx > step ? step : dx < -step ? -step : dx;
  pl.lean += ((tx - pl.x) * 0.22f - pl.lean) * 0.3f;

  /* up and down */
  bool super = pl.power[PW_SNEAKERS] > 0 || (pl.hover > 0 && pg.board == 1);
  if (pl.fly > 0) {
    float roof = world_roof(pl.z) < world_roof(pl.z + 12) ? world_roof(pl.z) : world_roof(pl.z + 12);
    float target = roof < pl.fly + 1.5f ? roof - 1.6f : pl.fly;
    if (target < 1.2f) target = 1.2f;
    pl.y += (target - pl.y) * 0.08f;
    pl.vy = 0;
    pl.state = ST_AIR;
  } else {
    pl.ground = world_ground(pl.x, pl.z, pl.y);
    bool on = pl.y <= pl.ground + 0.02f && pl.vy <= 0;
    if (on) {
      pl.y = pl.ground, pl.vy = 0;
      if (pl.state == ST_AIR) pl.state = ST_RUN;
      if (pl.state == ST_AIR) pl.ev |= EV_LAND;
      if (pressed & K_UP) {
        pl.vy = super ? SUPER_V : JUMP_V;
        /* (not through a low roof) */
        float room = world_roof(pl.z + 3) - pl.y - STAND_H;
        if (room < 2.6f) pl.vy = room > 0.4f ? __builtin_sqrtf(2 * GRAVITY * room) : 0;
        pl.state = ST_AIR;
        pl.roll = 0;
        run.jumps++;
        pl.ev |= super ? EV_JUMP | EV_SUPER : EV_JUMP;
      } else if (pressed & K_DOWN) {
        pl.roll = ROLL_T;
        run.rolls++;
        pl.ev |= EV_ROLL;
      }
    } else {
      pl.vy -= GRAVITY * DT;
      pl.y += pl.vy * DT;
      pl.state = ST_AIR;
      if (pressed & K_DOWN) {
        pl.vy = DIVE_V < pl.vy ? DIVE_V : pl.vy;
        pl.roll = ROLL_T;
        run.rolls++;
        pl.ev |= EV_ROLL;
      }
      if (pl.y <= pl.ground) pl.y = pl.ground, pl.vy = 0, pl.state = ST_RUN, pl.ev |= EV_LAND;
    }
  }
  if (pl.roll > 0) pl.roll -= DT;
  if (pressed & K_UP && pl.roll > 0 && pl.state == ST_RUN) pl.roll = 0;

  /* along */
  pl.z += speed * DT;
  run.dist += speed * DT;
  pl.phase += DT * (8 + speed * 0.22f);
  pl.on_roof = pl.fly <= 0 && pl.ground > TRAIN_H - 0.1f && pl.y <= pl.ground + 0.05f;
  if (pl.on_roof) run.roof += speed * DT;

  /* timers */
  for (int i = 0; i < PW_COUNT; i++)
    if (pl.power[i] > 0) pl.power[i] -= DT;
  if (pl.hover > 0) pl.hover -= DT;
  if (pl.safe > 0) pl.safe -= DT;
  if (pl.stumble > 0) pl.stumble -= DT;
  if (pl.chase > 0) pl.chase -= DT;
  if (pl.headstart > 0) {
    pl.headstart -= DT;
    if (pl.headstart <= 0) pl.fly = 0, pl.safe = 1.2f, world_clear(pl.z - 2, pl.z + 45);
  } else if (pl.fly > 0 && pl.power[PW_JETPACK] <= 0) {
    /* the jetpack is out: down, somewhere safe */
    pl.fly = 0;
    pl.safe = 1.2f;
    world_clear(pl.z - 2, pl.z + 45);
  }

  world_update(pl.z, pl.speed);
  if (pl.fly <= 0) collide(prev_x);

  /* coins and power-ups */
  float mid = pl.y + (pl.roll > 0 ? 0.45f : 0.9f);
  bool magnet = pl.power[PW_MAGNET] > 0;
  for (int i = 0; i < nitems; i++) {
    Item *it = &items[i];
    float dz = it->z - pl.z;
    if (dz > 16 || dz < -2) continue;
    if (magnet && it->kind == I_COIN && dz < 14 && dz > -1) it->flying = 1;
    if (it->flying) {
      /* drawn in by the magnet */
      float k = 0.25f;
      it->x += (pl.x - it->x) * k, it->y += (mid - it->y) * k, it->z += (pl.z + 0.3f - it->z) * k + speed * DT;
    }
    float ddx = it->x - pl.x, ddy = it->y - mid, ddz = it->z - pl.z;
    if (ddx < 0.95f && ddx > -0.95f && ddz < 1.0f && ddz > -0.8f && ddy < 1.25f && ddy > -1.25f) {
      collect(it);
      items[i] = items[--nitems];
      i--;
    }
  }

  /* the score: points for the way run, times the multiplier, twice with the 2x */
  float mul = (float)run.multiplier * (pl.power[PW_DOUBLE] > 0 ? 2 : 1);
  run.fscore += speed * DT * 5 * mul;
  run.score = (uint32_t)run.fscore;
  missions_check();
}
