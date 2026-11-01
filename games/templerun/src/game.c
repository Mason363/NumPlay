#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif
/* The run (game.h): the path laid ahead tile by tile as the game lays it, the runner's speed, jumps, slides,
 * stumbles and turns, the pursuers, coins and power-ups, the score, the objectives. Every number is the
 * game's (Temple Run for Android 1.0.8), in its units; a step is a 60th of a second. */
#include <string.h>
#include "game.h"
#include "plat.h"

Progress pg;
Runner pl;
RunStats run;
Fly fly[NFLY];
Pursuer pursuers[NPURSUERS];
bool pursuers_on;

#define DT (1.0f / 60.0f)
#define START_V 100.0f
#define MAX_V 210.0f
#define GRAVITY 250.0f
#define BOX 1.75f                /* the runner's half width; it is 9.59 tall (sliding: 4) */

static const int8_t DX[4] = {0, 1, 0, -1}, DZ[4] = {1, 0, -1, 0};

static inline float fabsf_(float x) { return x < 0 ? -x : x; }
static inline float minf(float a, float b) { return a < b ? a : b; }
static inline float maxf(float a, float b) { return a > b ? a : b; }
static int rnd(int n) { return n > 1 ? (int)(plat_random() % (uint32_t)n) : 0; }
static float sqrtf_(float x) {
  if (x <= 0) return 0;
  float r = x > 1 ? x : 1;
  for (int i = 0; i < 12; i++) r = 0.5f * (r + x / r);
  return r;
}

/* ---------------------------------------------------------------- the pursuers' trail */
#define NTRAIL 128
static struct {
  float x, y, z;
} trail[NTRAIL];
static int trail_n, trail_head;
static float trail_gap;           /* the runner's distance since the last point */

static void trail_add(void) {
  trail_head = (trail_head + 1) % NTRAIL;
  trail[trail_head].x = pl.x, trail[trail_head].y = pl.ground, trail[trail_head].z = pl.z;
  if (trail_n < NTRAIL) trail_n++;
}

/* the point of the trail d behind the runner, and the way it goes there */
static void trail_back(float d, float *x, float *y, float *z, float *yaw) {
  float px = pl.x, py = pl.ground, pz = pl.z;
  float fx = (float)DX[pl.dir], fz = (float)DZ[pl.dir];
  for (int i = 0; i < trail_n; i++) {
    const int k = (trail_head - i + NTRAIL) % NTRAIL;
    float dx = trail[k].x - px, dz = trail[k].z - pz, l = sqrtf_(dx * dx + dz * dz);
    if (l >= d && l > 0.01f) {
      float f = d / l;
      *x = px + dx * f, *z = pz + dz * f, *y = py + (trail[k].y - py) * f;
      fx = -dx / l, fz = -dz / l;
      goto done;
    }
    if (l > 0.01f) fx = -dx / l, fz = -dz / l;
    d -= l;
    px = trail[k].x, py = trail[k].y, pz = trail[k].z;
  }
  *x = px - fx * d, *y = py, *z = pz - fz * d;
done:
  *yaw = fx == 0 && fz == 0 ? 0 : (fabsf_(fx) > fabsf_(fz) ? (fx > 0 ? 1.5708f : -1.5708f) : (fz > 0 ? 0 : 3.14159f));
  /* (smooth across a corner) */
  if (fabsf_(fx) > 0.05f && fabsf_(fz) > 0.05f) {
    float a = fabsf_(fx) / (fabsf_(fx) + fabsf_(fz));
    float y0 = fz > 0 ? 0 : 3.14159f, y1 = fx > 0 ? 1.5708f : -1.5708f;
    if (y0 > 3 && y1 < 0) y1 += 6.28318f;
    *yaw = y0 + (y1 - y0) * a;
  }
}

static void pursue(void) {
  float t = pl.stumble_timer, behind;
  if (pl.dead && pl.death == DEATH_CAUGHT) behind = maxf(3.0f, 9 - pl.t_dead * 12);
  else if (t < 10) behind = 9 + 14 * t / 10;
  else behind = 23 + 60 * (t - 10);
  pursuers_on = behind < 70 && !(pl.dead && pl.death == DEATH_FALL);
  static const float side[NPURSUERS] = {0, -4.5f, 4.5f}, back[NPURSUERS] = {0, 4, 3};
  for (int i = 0; i < NPURSUERS; i++) {
    Pursuer *p = &pursuers[i];
    float x, y, z, yaw;
    trail_back(behind + back[i], &x, &y, &z, &yaw);
    float c = 1, s = 0;
    if (yaw > 1 && yaw < 2) c = 0, s = -1;
    else if (yaw < -1) c = 0, s = 1;
    else if (yaw > 2) c = -1, s = 0;
    p->x = x + side[i] * c, p->z = z + side[i] * s, p->y = y, p->yaw = yaw;
    p->phase += DT * (pl.dead ? 2.0f : pl.v / 14.0f);
  }
}

/* ---------------------------------------------------------------- the runner */
static void add_score(uint32_t n) { run.score += n * pg.multiplier; }

static void add_coins(uint32_t n) {
  run.coins += n;
  run.bonus_coins += n;
  if (run.bonus_coins >= 100) {
    add_score(500);
    run.bonus_level++;
    run.bonus_coins -= 100;
    static const uint16_t pts[4] = {100, 250, 500, 1000};
    int k = run.bonus_level >= 4 ? 3 : (int)run.bonus_level - 1;   /* (the game's levels start at 1: this is level + 1) */
    run.totem_pts += pts[k], run.totem_pic = (uint8_t)(k + 1), run.totem_t = 1;
  }
}

static void used_powers(void) {
  if (!run.used_powers) run.used_powers = true, run.score_before_powers = run.score;
}

static float power_len[5];        /* the running power-ups' full times (for the bar) */

static void start_invisibility(float d) {
  pl.invis = d;
  power_len[IT_INVISIBILITY] = d;
  pl.since_power = 0;
}

static void start_boost(float dist, bool mega) {
  bool had = pl.boost;
  pl.boost = true, pl.mega = mega;
  if (!had) {
    pl.v_before_boost = mega ? MAX_V : pl.v;
    start_invisibility(4);
  }
  pl.boost_left = dist + 50;
  power_len[IT_BOOST] = pl.boost_left;
  pl.since_power = 0;
  pl.stumble_timer += 10;
  used_powers();
}

static void die(int how);

static void stumble(void) {
  if (pl.stumble_timer < 10) {
    die(DEATH_CAUGHT);
    return;
  }
  pl.v *= 0.85f;
  pl.stumbling = true, pl.t_stumble = 0;
  pl.stumble_len = minf(pl.v * 0.005f, 0.5f);
  pl.stumble_timer = 0;
  run.since_trip = 0;
  run.stumbles++;
}

static void jump(void) {
  if (pl.jumping || pl.stumbling || pl.falling || !pl.on_ground) return;
  pl.vy = pl.v < 80 ? 80 : pl.v > 100 ? 100 : pl.v;
  pl.jumping = true, pl.sliding = false, pl.on_ground = false;
}

static void slide(void) {
  if (pl.sliding || pl.jumping || pl.falling) return;
  pl.sliding = true, pl.t_slide = 0;
  pl.slide_len = minf(pl.v * 0.007f, 0.75f);
}

/* ResurrectPlayer: the wings bring it back on the next quiet piece ahead (false: none near) */
static bool resurrect(void) {
  int i = path_root, chain[12], n = 0;
  for (int k = 0; k < 10 && i >= 0; k++) {
    const Tile *t = &tiles[i];
    if (k && !t->has_obstacles && !path_is_turn(t) && t->mesh >= 0) {
      for (int j = 0; j < n; j++) path_advance(chain[j]);
      path_advance(i);
      pl.x = path_cx(t) - 20 * DX[t->dir], pl.z = path_cz(t) - 20 * DZ[t->dir];
      pl.y = pl.ground = t->y, pl.vy = 0, pl.dir = t->dir;
      pl.death_v = pl.v > START_V ? pl.v : 0;
      pl.v = START_V;
      pl.jumping = pl.falling = pl.sliding = pl.stumbling = pl.higher = false;
      pl.on_ground = pl.over = true;
      pl.wings = 0, pl.wings_recharge = 45, pl.stumble_timer = 10;
      run.saved++;
      trail_n = 0;
      used_powers();
      return true;
    }
    if (k) chain[n++] = i;
    if (path_is_turn(t) && k) break;
    i = path_next(i, pl.dir);
  }
  return false;
}

static void die(int how) {
#ifdef TR_PERF
  (void)how;   /* (measuring the frame rate on a long run: nothing ends it) */
  return;
#endif
  if (pl.dead) return;
  if (pl.wings > 0 && resurrect()) return;
  pl.dead = true, pl.death = how, pl.t_dead = 0;
  pl.v = 0;
  if (how != DEATH_FALL) pl.vy = 0;
}

/* pressed Left or Right: turn at the next corner (if it is reached within a second), where the piece opens that way */
static void try_turn(void) {
  Tile *t = &tiles[path_root];
  if (!path_is_turn(t) || t->turned) return;
  float dx = pl.x - path_cx(t), dz = pl.z - path_cz(t);
  float r = pl.ground > 7.5f ? 6.5f : 10.0f;
  if (dx * dx + dz * dz > r * r) return;
  int left = (pl.dir + 3) & 3, right = (pl.dir + 1) & 3;
  bool can_l = path_opens(t, left), can_r = path_opens(t, right);
  int want = pl.turn_wish;
#ifdef TR_PERF
  if (true)
#else
  if (pl.boost)
#endif
    want = can_l && can_r ? (rnd(2) ? 1 : -1) : can_l ? -1 : can_r ? 1 : 0;
  if (!want || (want < 0 && !can_l) || (want > 0 && !can_r)) return;
  pl.dir = (uint8_t)(want > 0 ? right : left);
  pl.turn_wish = 0;
  run.turns++;
  t->turned = 1;
}

/* the obstacles of the piece under it: what the step from (x0, z0) ran into */
static void obstacles(const Tile *t, float x0, float z0) {
  int h = path_hit(t, x0, z0, pl.x, pl.z, pl.y, pl.sliding);
  if (!h || pl.invis > 0) return;
  if (!(h & FO_KILL)) stumble();
  else if ((h & FO_JUMP) && (h & FO_SLIDE)) die(t->level == TH_PLANK ? DEATH_TANGLE : DEATH_FIRE);
  else die(h & FO_JUMP ? DEATH_TREE : DEATH_BRANCH);
}

static bool box_hits(float x, float y, float z, float r) {
  float dx = maxf(0, fabsf_(x - pl.x) - BOX), dz = maxf(0, fabsf_(z - pl.z) - BOX);
  float dy = y < pl.y - 0.16f ? pl.y - 0.16f - y : y > pl.y + 9.59f ? y - pl.y - 9.59f : 0;
  return dx * dx + dy * dy + dz * dz <= r * r;
}

static void collect(void) {
  static const uint8_t value[4] = {0, 5, 10, 15};
  int mult = 1;
  if (pl.magnet > 0) {
    static const uint8_t m[6] = {1, 1, 1, 2, 2, 3};
    mult = m[pg.up[UP_MAGNET] > 5 ? 5 : pg.up[UP_MAGNET]];
  }
  float cy = pl.y + 4.8f;
  for (int i = 0; i < NTILES; i++) {
    Tile *t = &tiles[i];
    if (!t->alive || (!t->coins && !t->item)) continue;
    float tx = path_cx(t) - pl.x, tz = path_cz(t) - pl.z;
    if (tx * tx + tz * tz > 150.0f * 150.0f) continue;
    for (int k = 0; k < 5; k++) {
      float x, y, z;
      if (!path_coin_pos(t, k, &x, &y, &z)) continue;
      y += 3;
      if (box_hits(x, y, z, t->coin_kind == 1 ? 3.0f : 2.25f)) {
        t->coins &= (uint8_t)~(1 << k);
        add_score(value[t->coin_kind]);
        add_coins((uint32_t)(t->coin_kind * mult));
        continue;
      }
      float dx = x - pl.x, dy = y - cy, dz = z - pl.z;
      if (pl.magnet > 0 && dx * dx + dy * dy + dz * dz <= 8100) {
        int f = 0;
        while (f < NFLY && fly[f].on) f++;
        t->coins &= (uint8_t)~(1 << k);
        if (f == NFLY) {
          add_score(value[t->coin_kind]);
          add_coins((uint32_t)(t->coin_kind * mult));
        } else
          fly[f] = (Fly){x, y, z, t->coin_kind, 1};
      }
    }
    if (t->item) {
      float x = path_cx(t), z = path_cz(t);
      float r = t->item == IT_INVISIBILITY || t->item == IT_MAGNET ? 3.0f : 2.25f;
      if (box_hits(x, t->y + 4.2f, z, r)) {
        int it = t->item, l = pg.up[it == IT_COIN_BONUS ? UP_COIN_BONUS : it == IT_INVISIBILITY ? UP_INVISIBILITY : it == IT_MAGNET ? UP_MAGNET : UP_BOOST];
        if (l > 5) l = 5;
        if (l < 1) l = 1;
        t->item = IT_NONE;
        add_score(25);
        used_powers();
        if (it == IT_COIN_BONUS) {
          static const uint8_t bonus[6] = {50, 50, 75, 100, 125, 150};
          add_coins(bonus[l]);
          pl.since_power = 0;
          run.coin_bonuses++;
        } else if (it == IT_INVISIBILITY) {
          static const float d[6] = {15, 15, 17.5f, 20, 22.5f, 25};
          start_invisibility(d[l]);
          run.invisibilities++;
        } else if (it == IT_MAGNET) {
          static const float d[6] = {15, 15, 20, 20, 25, 25};
          pl.magnet = d[l], power_len[IT_MAGNET] = d[l];
          pl.since_power = 0;
          run.magnets++;
        } else {
          static const float d[6] = {250, 250, 375, 500, 625, 750};
          start_boost(d[l], false);
        }
      }
    }
  }
  /* coins flying in */
  for (int f = 0; f < NFLY; f++) {
    if (!fly[f].on) continue;
    float dx = pl.x - fly[f].x, dy = cy - fly[f].y, dz = pl.z - fly[f].z;
    float l = sqrtf_(dx * dx + dy * dy + dz * dz), step = maxf(pl.v * 1.6f, 250) * DT;
    if (l <= step + 3) {
      fly[f].on = 0;
      add_score(value[fly[f].kind]);
      add_coins((uint32_t)(fly[f].kind * mult));
    } else
      fly[f].x += dx / l * step, fly[f].y += dy / l * step, fly[f].z += dz / l * step;
  }
}

void game_new_run(void) {
  memset(&pl, 0, sizeof pl);
  memset(&run, 0, sizeof run);
  memset(fly, 0, sizeof fly);
  memset(power_len, 0, sizeof power_len);
  if (pg.multiplier < 10) pg.multiplier = 10;
  path_start();
  pl.dir = D_N;
  pl.v = START_V, pl.z = -10, pl.on_ground = pl.over = true;   /* (the game's: in the temple's dark hall, out through its door) */
  pl.stumble_timer = 7;
  pl.since_power = 15;
  trail_n = 0, trail_head = 0, trail_gap = 0;
  for (int i = 0; i < 40; i++) {
    trail_add();
    trail[trail_head].z = pl.z - (float)(40 - i);
  }
  pursue();
}

void game_step(uint32_t in) {
  if (run.totem_t > 0 && (run.totem_t -= DT) <= 0) run.totem_t = 0, add_score(run.totem_pts), run.totem_pts = 0;
  run.time += DT;
  if (pl.dead) {
    pl.t_dead += DT;
    if (pl.death == DEATH_FALL) {
      pl.vy -= GRAVITY * DT;
      pl.y += pl.vy * DT;
    }
    pursue();
    return;
  }
  /* keys */
  if (in & (IN_LEFT | IN_RIGHT)) pl.turn_wish = in & IN_LEFT ? -1 : 1, pl.t_wish = 0;
  if (pl.turn_wish && (pl.t_wish += DT) > 1) pl.turn_wish = 0;
  pl.lean = in & IN_LEAN_L ? -6.0f : in & IN_LEAN_R ? 6.0f : 0.0f;
  if (in & IN_JUMP) jump();
  if (in & IN_SLIDE) slide();
  if ((in & IN_WINGS) && game_wings_ok()) {
    pg.use[USE_WINGS]--;
    pl.wings = 30;
    used_powers();
  }
  int hs = game_headstart_ok();
  if ((in & IN_HEADSTART) && (hs & 1)) pg.use[USE_HEADSTART]--, run.headstarts++, start_boost(950 - run.dist, false);
  else if ((in & IN_MEGA) && (hs & 2)) pg.use[USE_MEGA]--, run.headstarts++, start_boost(2350 - run.dist, true);
  /* power-ups running out */
  if (pl.invis > 0 && (!pl.boost || pl.boost_left < 50)) {
    pl.invis -= DT;
    if (pl.invis <= 0) pl.invis = 0, pl.boost = pl.mega = false;
  }
  if (pl.wings > 0) pl.wings = maxf(0, pl.wings - DT);
  else if (pl.wings_recharge > 0) pl.wings_recharge = maxf(0, pl.wings_recharge - DT);
  if (pl.magnet > 0) pl.magnet = maxf(0, pl.magnet - DT);
  pl.since_power += DT;
  /* speed */
  float maxv = pl.boost ? (pl.mega ? MAX_V * 2.5f : MAX_V * 2) : MAX_V;
  if ((pl.v < maxv || (pl.boost && pl.boost_left <= 50)) && !pl.jumping && !pl.sliding) {
    float r = pl.v / maxv, acc;
    if (pl.death_v > 0) {
      acc = r < 0.5f ? 30 : r < 0.65f ? 10 : r < 0.85f ? 7.5f : 2.5f;
      if (pl.v >= pl.death_v * 0.85f) pl.death_v = 0;
    } else
      acc = r < 0.5f ? 30 : r <= 0.65f ? 10 : r <= 0.85f ? 1.5f : 0.5f;
    if (pl.boost) acc = pl.boost_left > 50 ? 150 : pl.v > pl.v_before_boost ? -150 : acc;
    pl.v += acc * DT;
    if (acc > 0 && pl.v > maxv) pl.v = maxv;
  }
  try_turn();
  /* along, and across toward the middle of the piece under it (and the lean) */
  const Tile *t = &tiles[path_root];
  float x0 = pl.x, z0 = pl.z;
  float fx = (float)DX[pl.dir], fz = (float)DZ[pl.dir], rx = fz, rz = -fx;
  pl.x += fx * pl.v * DT, pl.z += fz * pl.v * DT;
  if (!pl.falling) {
    float side = (pl.x - path_cx(t)) * rx + (pl.z - path_cz(t)) * rz;
    float move = (pl.lean - side) * 0.1f * 50 * 1.125f * DT;
    pl.x += rx * move, pl.z += rz * move;
  }
  pl.vy -= GRAVITY * DT;
  pl.y += pl.vy * DT;
  /* the piece under it (the root moves on, what is behind and the ways not taken pruned, the path laid on) */
  int ni = path_at(pl.x, pl.z);
  if (ni >= 0 && ni != path_root) path_advance(ni), ni = path_root;
  t = ni >= 0 ? &tiles[ni] : NULL;
  /* over ground: inside the piece's ground, or invisible (then over everything) */
  bool inv = pl.invis > 0, over = inv || (t && path_ground(t, pl.x, pl.z));
#ifdef TR_PERF
  over = true;
#endif
  if (over) {
    float g = t ? t->y : pl.ground;
    if (inv && t) {
      /* (the game's safety net: over gaps and level changes the ground goes from one level to the next) */
      int f = t->next[pl.dir];
      const Tile *n = f >= 0 ? &tiles[f] : NULL;
      if (t->obst == OB_HOLE || t->obst == OB_NARROW || t->obst == OB_LEVEL || (n && n->obst == OB_LEVEL && t->level != TH_CLIFF)) {
        float a = (pl.x - path_cx(t)) * fx + (pl.z - path_cz(t)) * fz;
        g = t->y + (a / TILE + 0.5f) * ((n ? n->y : t->y) - t->y);
      }
    }
    if ((pl.ground < g || !pl.over) && pl.y < g - 6 && !inv) {
      die(DEATH_LEDGE);   /* (into the side of the higher ground) */
      if (pl.dead) {
        pursue();
        return;
      }
    }
    if (g > pl.ground) pl.higher = true;
    pl.ground = g;
  }
  pl.over = over;
  float g = pl.ground;
  if (over && ((!pl.falling && pl.y <= g) || (pl.falling && pl.y <= g && pl.y >= g - 6))) {
    pl.jumping = false;
    if (pl.falling && pl.y < g - 2 && !inv) stumble();   /* (caught the edge) */
    if (pl.higher && pl.y >= g - 6 && pl.y < g - 2 && !inv && !pl.dead) stumble();   /* (up onto a step) */
    pl.y = g, pl.vy = 0;
    pl.on_ground = true, pl.higher = false;
  } else
    pl.on_ground = false;
  if (pl.y < g) {
    if (!pl.falling) pl.falling = true, pl.t_fall = 0, pl.sliding = false;
    pl.t_fall += DT;
  } else
    pl.falling = false;
  if (!pl.dead && pl.y < -35) die(DEATH_FALL);
  if (!pl.dead && t) obstacles(t, x0, z0);
  if (pl.dead) {
    pursue();
    return;
  }
  if (pl.sliding && (pl.t_slide += DT) > pl.slide_len) pl.sliding = false;
  if (pl.stumbling && (pl.t_stumble += DT) > pl.stumble_len) pl.stumbling = false;
  pl.stumble_timer += DT;
  /* the distance, the score */
  if (!pl.falling) {
    float u = pl.v / 60, d = u * u / 20;
    run.dist += d, run.dist_left += d;
    uint32_t w = (uint32_t)run.dist_left;
    run.dist_left -= (float)w;
    add_score(w);
    if (pl.boost) pl.boost_left -= d;
    if (run.coins == 0) run.dist_no_coins = run.dist;
    run.since_trip += d;
    if (run.since_trip > run.dist_no_trip) run.dist_no_trip = run.since_trip;
    path_pace(run.dist, pl.since_power, d);
  }
  collect();
  pl.anim += DT * pl.v / 22.0f;
  trail_gap += pl.v * DT;
  if (trail_gap >= 2) trail_gap = 0, trail_add();
  pursue();
}

int game_headstart_ok(void) {
  if (pl.dead || pl.boost || run.dist < 25 || run.dist > 100) return 0;
  return (pg.use[USE_HEADSTART] ? 1 : 0) | (pg.use[USE_MEGA] ? 2 : 0);
}

bool game_wings_ok(void) {
  return !pl.dead && !pl.falling && pl.wings <= 0 && pl.wings_recharge <= 0 && pg.use[USE_WINGS] > 0;
}

bool game_over(void) { return pl.dead && pl.t_dead > 1.6f; }

float game_power_left(int *kind) {
  *kind = IT_NONE;
  if (pl.boost && pl.boost_left > 50 && power_len[IT_BOOST] > 0) {
    *kind = IT_BOOST;
    return maxf(0, (pl.boost_left - 50) / (power_len[IT_BOOST] - 50));
  }
  if (pl.invis > 0 && !pl.boost && power_len[IT_INVISIBILITY] > 0) {
    *kind = IT_INVISIBILITY;
    return pl.invis / power_len[IT_INVISIBILITY];
  }
  if (pl.magnet > 0 && power_len[IT_MAGNET] > 0) {
    *kind = IT_MAGNET;
    return pl.magnet / power_len[IT_MAGNET];
  }
  return 0;
}

/* ---------------------------------------------------------------- the store */
uint32_t game_up_price(int up) {
  static const uint16_t price[UP_MAX] = {250, 1000, 2500, 5000, 7500};
  return pg.up[up] < UP_MAX ? price[pg.up[up]] : 0;
}

uint32_t game_use_price(int use) {
  static const uint16_t price[USE_COUNT] = {500, 2500, 10000};
  return price[use];
}

/* ---------------------------------------------------------------- objectives (the game's, in its order) */
enum { O_DIST, O_COINS, O_SCORE, O_DIST_NOCOIN, O_SAVED, O_UPGRADE, O_HEADSTART, O_NOTRIP, O_SCORE_NOPOWERS,
       O_RUNS, O_LIFE_COINS, O_LIFE_DIST, O_MAGNETS, O_BONUSES, O_INVIS };
static const struct {
  uint8_t kind;
  uint32_t n;
} objs[NOBJ] = {
  {O_DIST, 500}, {O_COINS, 100}, {O_SCORE, 25000}, {O_DIST, 1000}, {O_DIST_NOCOIN, 500}, {O_COINS, 250},
  {O_SCORE, 50000}, {O_DIST, 2500}, {O_COINS, 500}, {O_SAVED, 1}, {O_UPGRADE, 1}, {O_SCORE, 100000},
  {O_COINS, 750}, {O_HEADSTART, 1}, {O_NOTRIP, 2500}, {O_DIST_NOCOIN, 1000}, {O_DIST, 5000}, {O_NOTRIP, 5000},
  {O_SCORE, 250000}, {O_SAVED, 2}, {O_COINS, 1000}, {O_SCORE, 500000}, {O_UPGRADE, 5}, {O_SCORE, 1000000},
  {O_COINS, 2500}, {O_DIST, 10000}, {O_COINS, 5000}, {O_SCORE, 2500000}, {O_SCORE, 5000000},
  {O_SCORE_NOPOWERS, 1000000}, {O_SCORE, 10000000}, {O_RUNS, 1000}, {O_LIFE_COINS, 100000},
  {O_LIFE_DIST, 100000}, {O_RUNS, 5000}, {O_LIFE_COINS, 500000}, {O_LIFE_DIST, 1000000}, {O_MAGNETS, 5},
  {O_BONUSES, 5}, {O_INVIS, 5},
};

static char *put_num(char *p, uint32_t n) {
  char b[16];
  int k = 0, g = 0;
  do {
    if (g == 3) b[k++] = ',', g = 0;
    b[k++] = (char)('0' + n % 10), n /= 10, g++;
  } while (n);
  while (k) *p++ = b[--k];
  return p;
}

static char *put(char *p, const char *s) {
  while (*s) *p++ = *s++;
  return p;
}

const char *game_obj_text(int i) {
  static char b[48];
  char *p = b;
  uint32_t n = objs[i].n;
  switch (objs[i].kind) {
  case O_DIST: p = put(put_num(put(p, "Run "), n), " m"); break;
  case O_COINS: p = put(put_num(put(p, "Collect "), n), " coins"); break;
  case O_SCORE: p = put_num(put(p, "Score "), n); break;
  case O_DIST_NOCOIN: p = put(put_num(put(p, "Run "), n), " m with no coins"); break;
  case O_SAVED: p = put(p, n > 1 ? "Be saved twice in a run" : "Be saved by the wings"); break;
  case O_UPGRADE: p = put(p, n > 1 ? "Fully upgrade a power-up" : "Buy an upgrade"); break;
  case O_HEADSTART: p = put(p, "Use a head start"); break;
  case O_NOTRIP: p = put(put_num(put(p, "Run "), n), " m without tripping"); break;
  case O_SCORE_NOPOWERS: p = put(put_num(put(p, "Score "), n), " without powers"); break;
  case O_RUNS: p = put(put_num(put(p, "Play "), n), " runs"); break;
  case O_LIFE_COINS: p = put(put_num(put(p, "Collect "), n), " coins in all"); break;
  case O_LIFE_DIST: p = put(put_num(put(p, "Run "), n), " m in all"); break;
  case O_MAGNETS: p = put(put_num(put(p, "Pick up "), n), " coin magnets"); break;
  case O_BONUSES: p = put(put_num(put(p, "Pick up "), n), " coin bonuses"); break;
  default: p = put(put_num(put(p, "Pick up "), n), " invisibilities"); break;
  }
  *p = 0;
  return b;
}

bool game_obj_done(int i) { return pg.done[i >> 5] >> (i & 31) & 1; }

static bool met(int i) {
  uint32_t n = objs[i].n;
  int best_up = 0;
  for (int k = 0; k < UP_COUNT; k++)
    if (pg.up[k] > best_up) best_up = pg.up[k];
  switch (objs[i].kind) {
  case O_DIST: return run.dist >= (float)n;
  case O_COINS: return run.coins >= n;
  case O_SCORE: return run.score >= n;
  case O_DIST_NOCOIN: return run.dist_no_coins >= (float)n;
  case O_SAVED: return run.saved >= (int)n;
  case O_UPGRADE: return best_up >= (int)n;
  case O_HEADSTART: return run.headstarts >= (int)n;
  case O_NOTRIP: return run.dist_no_trip >= (float)n;
  case O_SCORE_NOPOWERS: return (run.used_powers ? run.score_before_powers : run.score) >= n;
  case O_RUNS: return pg.runs >= n;
  case O_LIFE_COINS: return pg.life_coins >= n;
  case O_LIFE_DIST: return pg.life_dist >= n;
  case O_MAGNETS: return run.magnets >= (int)n;
  case O_BONUSES: return run.coin_bonuses >= (int)n;
  default: return run.invisibilities >= (int)n;
  }
}

int game_award(int *out, int max) {
  int n = 0;
  for (int i = 0; i < NOBJ; i++)
    if (!game_obj_done(i) && met(i)) {
      pg.done[i >> 5] |= 1u << (i & 31);
      if (pg.multiplier < 255) pg.multiplier++;
      if (n < max) out[n] = i;
      n++;
    }
  return n < max ? n : max;
}
