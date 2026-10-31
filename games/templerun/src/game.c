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
Tile path[NTILES];
int path_first, path_count;
Tile stub[2][NSTUB];
bool stub_on;
Fly fly[NFLY];
Pursuer pursuers[NPURSUERS];
bool pursuers_on;

#define DT (1.0f / 60.0f)
#define START_V 100.0f
#define MAX_V 210.0f
#define GRAVITY 250.0f
#define AHEAD 16                 /* tiles laid ahead of the runner */
#define BEHIND 4
#define BOX 1.75f                /* the runner's half width; it is 9.59 tall (sliding: 4) */

static const int8_t DX[4] = {0, 1, 0, -1}, DZ[4] = {1, 0, -1, 0};
static const float LEVEL_Y[TH_COUNT] = {0, -15, 15};

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

/* ---------------------------------------------------------------- laying the path */
static struct {
  int main_dir;                       /* the path never heads back against this */
  int since_turn, since_obstacle, back_to_back, since_coins, since_bonus;
  int theme, want_theme;
  int coin_left, coin_layout, coin_kind;
  float since_level, since_section;
  bool fast;
  /* by distance (the game's pacing) */
  int gap_coins, gap_turns, gap_obs;
  float dbl;
  bool branches, fire, narrows;
  float level_temple, level_other, turn_section;
  float red_at, blue_at;
} gen;

float game_tile_cx(const Tile *t) { return t->gx * TILE; }
float game_tile_cz(const Tile *t) { return t->gz * TILE; }

static int exit_dir(const Tile *t) {
  if (t->kind == PK_CORNER_L) return (t->dir + 3) & 3;
  if (t->kind == PK_CORNER_R) return (t->dir + 1) & 3;
  return t->dir;
}

static float exit_y(const Tile *t) {
  return t->y + (t->kind == PK_STEP_UP ? 15.0f : t->kind == PK_STEP_DOWN ? -15.0f : 0.0f);
}

static bool is_turn(const Tile *t) { return t->kind == PK_CORNER_L || t->kind == PK_CORNER_R || t->kind == PK_T; }

static void push_tile(const Tile *t) {
  if (path_count == NTILES) {
    path_first = (path_first + 1) % NTILES;
    path_count--;
    pl.tile--;
  }
  path[(path_first + path_count) % NTILES] = *t;
  path_count++;
}

static void pacing(void) {
  float d = run.dist;
  if (d < 250) gen.gap_coins = 12, gen.gap_turns = 12, gen.gap_obs = 6, gen.dbl = 0;
  else if (d < 500) gen.gap_coins = 10, gen.gap_turns = 10, gen.gap_obs = 5, gen.dbl = 0;
  else if (d < 1000) gen.gap_coins = 9, gen.gap_turns = 8, gen.gap_obs = 4, gen.dbl = 0.05f;
  else if (d < 1500) gen.gap_coins = 8, gen.gap_turns = 6, gen.gap_obs = 4, gen.dbl = 0.05f;
  else if (d < 2000) gen.gap_coins = 7, gen.gap_turns = 5, gen.gap_obs = 3, gen.dbl = 0.1f;
  else if (d < 3250) gen.gap_coins = 6, gen.gap_turns = 4, gen.gap_obs = 3, gen.dbl = 0.15f;
  else if (d < 4500) gen.gap_coins = 6, gen.gap_turns = 3, gen.gap_obs = 3, gen.dbl = 0.175f;
  else gen.gap_coins = 6, gen.gap_turns = 2, gen.gap_obs = 2, gen.dbl = 0.2f;
  gen.branches = gen.fire = d >= 500;
  gen.narrows = d >= 1500;
  if (d >= 3250) gen.level_temple = 375, gen.level_other = 300, gen.turn_section = 750;
  else if (d >= 1000) gen.level_temple = 450, gen.level_other = 250, gen.turn_section = 850;
  else gen.level_temple = 500, gen.level_other = 200, gen.turn_section = 1000;
  if (gen.fast) {
    gen.gap_turns = (int)(gen.gap_turns * 0.65f);
    if (gen.gap_turns < 2) gen.gap_turns = 2;
    gen.gap_coins = 0;
  }
}

static void coin_value_level(void) {
  static const float red[6] = {1e9f, 1500, 1000, 1000, 1000, 1000}, blue[6] = {1e9f, 1e9f, 1e9f, 3000, 2500, 2000};
  int l = pg.up[UP_COIN_VALUE] > 5 ? 5 : pg.up[UP_COIN_VALUE];
  gen.red_at = red[l], gen.blue_at = blue[l];
}

static void change_level(void) {
  if (gen.theme == TH_PLANK) gen.want_theme = TH_TEMPLE;
  else if (gen.theme == TH_TEMPLE) gen.want_theme = rnd(2) == 0 ? TH_PLANK : TH_CLIFF;
  else gen.want_theme = rnd(2) != 0 ? TH_PLANK : TH_TEMPLE;
}

static void scenery(Tile *t) {
  t->scenery = 0;
  if (t->kind != PK_STRAIGHT && t->kind != PK_HOLE && t->kind != PK_HOLE_LONG) return;
  if (t->theme != TH_CLIFF && rnd(10) < 3) t->scenery |= 1;
  if (rnd(10) < 3) t->scenery |= 2;
  if (rnd(3) == 0) t->scenery |= 4;
  if (t->theme == TH_TEMPLE && rnd(6) == 0) t->scenery |= 8;
}

static void coins_for(Tile *t) {
  t->coin = COIN_NONE, t->coins = 0;
  if (t->kind == PK_STEP_UP || t->kind == PK_STEP_DOWN || t->item) return;
  if (gen.coin_left <= 0) {
    if (gen.since_coins < gen.gap_coins) return;
    gen.coin_left = 1 + rnd(3);
    static const uint8_t layouts[6] = {COIN_CENTRE, COIN_CENTRE, COIN_LEFT, COIN_RIGHT, COIN_SLANT_L, COIN_SLANT_R};
    gen.coin_layout = layouts[rnd(6)];
    uint8_t kinds[3] = {1, 0, 0};
    int n = 1;
    if (run.dist >= gen.red_at) kinds[n++] = 2;
    if (run.dist >= gen.blue_at) kinds[n++] = 3;
    gen.coin_kind = kinds[rnd(n)];
  }
  gen.coin_left--;
  gen.since_coins = 0;
  if (t->ob == OB_BRANCH || t->kind == PK_NARROW_L || t->kind == PK_NARROW_R) return;
  t->coin_kind = (uint8_t)gen.coin_kind;
  if (t->ob != OB_NONE || t->kind == PK_HOLE || t->kind == PK_HOLE_LONG) t->coin = COIN_ARC;
  else if (is_turn(t)) t->coin = COIN_CENTRE;
  else t->coin = (uint8_t)gen.coin_layout;
  t->coins = t->kind == PK_T ? 0x07 : 0x1F;
}

/* the next tile after the last (false: the last is a junction not yet taken) */
static bool lay(void) {
  const Tile *last = tile_at(path_count - 1);
  if (last->kind == PK_T) return false;
  int dir = exit_dir(last);
  Tile t;
  memset(&t, 0, sizeof t);
  t.gx = (int16_t)(last->gx + DX[dir]), t.gz = (int16_t)(last->gz + DZ[dir]);
  t.dir = (uint8_t)dir, t.y = exit_y(last), t.kind = PK_STRAIGHT, t.theme = (uint8_t)gen.theme;
  gen.since_turn++, gen.since_obstacle++, gen.since_coins++, gen.since_bonus++;
  if (gen.want_theme != gen.theme) {
    /* a change of place: up or down the steps between them, on a quiet stretch */
    if (gen.since_turn > 2 && gen.since_obstacle > 2) {
      float wy = LEVEL_Y[gen.want_theme];
      if (wy > t.y + 1) t.kind = PK_STEP_UP;
      else if (wy < t.y - 1) t.kind = PK_STEP_DOWN;
      if (fabsf_(exit_y(&t) - wy) < 1) gen.theme = gen.want_theme;
    }
    scenery(&t);
    push_tile(&t);
    gen.back_to_back = 0;
    return true;
  }
  bool can_turn = gen.since_turn >= gen.gap_turns;
  bool can_obs = gen.since_turn != 1 && gen.back_to_back < 2 &&
                 (gen.since_obstacle >= gen.gap_obs || (float)rnd(1000) < 1000.0f * gen.dbl);
  uint8_t cand[8];
  int n = 0;
  cand[n++] = PK_STRAIGHT;
  if (can_turn) {
    int rel = (dir - gen.main_dir) & 3;
    if (rel == 0) cand[n++] = PK_CORNER_L, cand[n++] = PK_CORNER_R, cand[n++] = PK_T;
    else if (rel == 1) cand[n++] = PK_CORNER_L;
    else cand[n++] = PK_CORNER_R;
  }
  if (can_obs) {
    if (rnd(2)) cand[n++] = run.dist >= 500 && rnd(2) ? PK_HOLE_LONG : PK_HOLE;
    if (gen.fire && gen.theme != TH_CLIFF && rnd(2)) cand[n++] = 100;   /* (fire) */
    if (gen.narrows && gen.theme == TH_TEMPLE && gen.since_turn >= 2 && rnd(2)) cand[n++] = rnd(2) ? PK_NARROW_L : PK_NARROW_R;
  }
  int k = cand[rnd(n)];
  if (k == 100) t.kind = PK_STRAIGHT, t.ob = OB_FIRE;
  else t.kind = (uint8_t)k;
  if (t.kind == PK_STRAIGHT && t.ob == OB_NONE && can_obs && rnd(2)) {
    uint8_t obs[3];
    int m = 0;
    if (gen.theme == TH_TEMPLE) obs[m++] = OB_ROOT, obs[m++] = OB_TREE;
    if (gen.branches && gen.theme != TH_PLANK) obs[m++] = OB_BRANCH;
    if (m) t.ob = obs[rnd(m)];
  }
  bool obstacle = t.ob != OB_NONE || t.kind == PK_HOLE || t.kind == PK_HOLE_LONG || t.kind == PK_NARROW_L ||
                  t.kind == PK_NARROW_R;
  if (is_turn(&t)) gen.since_turn = 0;
  if (obstacle) gen.since_obstacle = 0, gen.back_to_back++;
  else gen.back_to_back = 0;
  /* a power-up, now and then, where nothing else is */
  if (!obstacle && t.kind == PK_STRAIGHT && gen.since_bonus >= 50 && pl.since_power >= 15 && rnd(2)) {
    uint8_t items[4];
    int m = 0;
    if (pg.up[UP_COIN_BONUS]) items[m++] = IT_COIN_BONUS;
    if (pg.up[UP_INVISIBILITY]) items[m++] = IT_INVISIBILITY;
    if (pg.up[UP_MAGNET]) items[m++] = IT_MAGNET;
    if (pg.up[UP_BOOST]) items[m++] = IT_BOOST;
    if (m) t.item = items[rnd(m)], gen.since_bonus = 0;
  }
  coins_for(&t);
  scenery(&t);
  push_tile(&t);
  if (t.kind == PK_T) {
    /* its two ways, plain, until one is taken */
    for (int s = 0; s < 2; s++) {
      int d = (dir + (s ? 1 : 3)) & 3;
      for (int i = 0; i < NSTUB; i++) {
        Tile *u = &stub[s][i];
        memset(u, 0, sizeof *u);
        u->gx = (int16_t)(t.gx + DX[d] * (i + 1)), u->gz = (int16_t)(t.gz + DZ[d] * (i + 1));
        u->dir = (uint8_t)d, u->kind = PK_STRAIGHT, u->theme = t.theme, u->y = t.y;
        scenery(u);
      }
    }
    stub_on = true;
  }
  return true;
}

static void lay_ahead(void) {
  while (path_count - 1 - pl.tile < AHEAD && lay()) {
  }
}

/* ---------------------------------------------------------------- the floor */
static bool on_floor(const Tile *t, float a, float s, float *h) {
  float as = fabsf_(s), aa = fabsf_(a);
  *h = t->y;
  switch (t->kind) {
  case PK_HOLE: return as <= HALF_W && aa >= 10;
  case PK_HOLE_LONG: return as <= HALF_W && aa >= 17;
  case PK_NARROW_L: return (s >= -HALF_W && s <= -2) || (as <= HALF_W && aa >= 26);
  case PK_NARROW_R: return (s >= 2 && s <= HALF_W) || (as <= HALF_W && aa >= 26);
  case PK_CORNER_R: return (as <= HALF_W && a <= HALF_W) || (s >= HALF_W && s <= 30 && aa <= HALF_W);
  case PK_CORNER_L: return (as <= HALF_W && a <= HALF_W) || (s <= -HALF_W && s >= -30 && aa <= HALF_W);
  case PK_T: return (as <= HALF_W && a <= -HALF_W) || (aa <= HALF_W && as <= 30);
  case PK_STEP_UP:
  case PK_STEP_DOWN: {
    int i = (int)((a + 30) / 12);
    if (i < 0) i = 0;
    if (i > 4) i = 4;
    *h = t->y + (t->kind == PK_STEP_UP ? 3.0f * (float)(i + 1) : -3.0f * (float)i);
    return as <= HALF_W;
  }
  default: return as <= HALF_W;
  }
}

static void local(const Tile *t, float x, float z, float *a, float *s) {
  float dx = x - game_tile_cx(t), dz = z - game_tile_cz(t);
  *a = dx * DX[t->dir] + dz * DZ[t->dir];
  *s = dx * DZ[t->dir] - dz * DX[t->dir];
}

static const Tile *find_tile(int gx, int gz) {
  for (int i = path_count - 1; i >= 0; i--) {
    const Tile *t = tile_at(i);
    if (t->gx == gx && t->gz == gz) return t;
  }
  if (stub_on)
    for (int s = 0; s < 2; s++)
      for (int i = 0; i < NSTUB; i++)
        if (stub[s][i].gx == gx && stub[s][i].gz == gz) return &stub[s][i];
  return NULL;
}

static int round_(float v) { return (int)(v >= 0 ? v + 0.5f : v - 0.5f); }

bool game_floor(float x, float z, float *y, const Tile **found) {
  const Tile *t = find_tile(round_(x / TILE), round_(z / TILE));
  if (found) *found = t;
  if (!t) return false;
  float a, s;
  local(t, x, z, &a, &s);
  return on_floor(t, a, s, y);
}

bool game_coin_pos(const Tile *t, int k, float *x, float *y, float *z) {
  if (t->coin == COIN_NONE || !(t->coins >> k & 1)) return false;
  float a = -25 + 12.5f * (float)k, s = 0, h = 0;
  switch (t->coin) {
  case COIN_LEFT: s = -6; break;
  case COIN_RIGHT: s = 6; break;
  case COIN_SLANT_L: s = 6 - 3.0f * (float)k; break;
  case COIN_SLANT_R: s = -6 + 3.0f * (float)k; break;
  case COIN_ARC: h = 10 * (1 - (a / 30) * (a / 30)); break;
  default: break;
  }
  if (is_turn(t) && k >= 3) {
    a = 0;
    s = (t->kind == PK_CORNER_L ? -12.5f : 12.5f) * (float)(k - 2);
  }
  int d = t->dir;
  *x = game_tile_cx(t) + a * DX[d] + s * DZ[d];
  *z = game_tile_cz(t) + a * DZ[d] - s * DX[d];
  *y = t->y + h;
  return true;
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

/* the wings bring it back on the next quiet tile ahead (false: none near) */
static bool resurrect(void) {
  for (int i = pl.tile + 1; i < path_count && i <= pl.tile + 10; i++) {
    const Tile *t = tile_at(i);
    if (t->kind != PK_STRAIGHT || t->ob != OB_NONE) continue;
    pl.x = game_tile_cx(t) - 20 * DX[t->dir], pl.z = game_tile_cz(t) - 20 * DZ[t->dir];
    pl.y = pl.ground = t->y, pl.vy = 0, pl.dir = t->dir, pl.tile = i;
    pl.death_v = pl.v > START_V ? pl.v : 0;
    pl.v = START_V;
    pl.jumping = pl.falling = pl.sliding = pl.stumbling = false;
    pl.on_ground = pl.over = true;
    pl.wings = 0, pl.wings_recharge = 45, pl.stumble_timer = 10;
    run.saved++;
    trail_n = 0;
    used_powers();
    return true;
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

/* pressed Left or Right: turn at the next corner (if it is reached within a second) */
static void try_turn(void) {
  const Tile *t = tile_at(pl.tile);
  if (!is_turn(t) || (t->hit & 2) || t->dir != pl.dir) return;
  float dx = pl.x - game_tile_cx(t), dz = pl.z - game_tile_cz(t);
  float r = pl.ground > 7.5f ? 6.5f : 10.0f;
  if (dx * dx + dz * dz > r * r) return;
  int want = pl.turn_wish;
#ifdef TR_PERF
  if (true)
#else
  if (pl.boost)
#endif
    want = t->kind == PK_CORNER_L ? -1 : t->kind == PK_CORNER_R ? 1 : (rnd(2) ? 1 : -1);
  if (!want) return;
  if ((want < 0 && t->kind == PK_CORNER_R) || (want > 0 && t->kind == PK_CORNER_L)) return;
  pl.dir = (uint8_t)((pl.dir + (want > 0 ? 1 : 3)) & 3);
  pl.turn_wish = 0;
  run.turns++;
  tile_at(pl.tile)->hit |= 2;
  if (t->kind == PK_T) {
    for (int i = 0; i < NSTUB; i++) push_tile(&stub[want > 0][i]);
    stub_on = false;
    gen.since_turn = 0;
    lay_ahead();
  }
}

static void follow_tile(void) {
  for (int i = pl.tile - 1; i <= pl.tile + 2 && i < path_count; i++) {
    if (i < 0) continue;
    const Tile *t = tile_at(i);
    if (t->gx == round_(pl.x / TILE) && t->gz == round_(pl.z / TILE)) {
      pl.tile = i;
      break;
    }
  }
  while (pl.tile > BEHIND) {
    path_first = (path_first + 1) % NTILES;
    path_count--;
    pl.tile--;
  }
  lay_ahead();
}

static void obstacles(float a0, float a1) {
  Tile *t = tile_at(pl.tile);
  if (t->ob == OB_NONE || (t->hit & 1)) return;
  float lo = minf(a0, a1), hi = maxf(a0, a1);
  static const float depth[5] = {0, 0.9f, 1.8f, 1.3f, 1.4f};
  float d = depth[t->ob] + BOX;
  if (hi < -d || lo > d) return;
  float feet = pl.y - t->y, top = feet + (pl.sliding ? 4.0f : 9.59f);
  bool hit = false;
  switch (t->ob) {
  case OB_ROOT: hit = feet < 1.9f; break;
  case OB_TREE: hit = feet < 4.2f; break;
  case OB_BRANCH: hit = top > 5.2f && feet < 7.4f; break;
  case OB_FIRE: hit = feet < 4.0f; break;
  default: break;
  }
  if (!hit || pl.invis > 0 || pl.boost) return;
  t->hit |= 1;
  if (t->ob == OB_ROOT) stumble();
  else die(t->ob == OB_TREE ? DEATH_TREE : t->ob == OB_BRANCH ? DEATH_BRANCH : DEATH_FIRE);
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
  for (int i = pl.tile - 1; i <= pl.tile + 3 && i < path_count; i++) {
    if (i < 0) continue;
    Tile *t = tile_at(i);
    for (int k = 0; k < 5; k++) {
      float x, y, z;
      if (!game_coin_pos(t, k, &x, &y, &z)) continue;
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
      float x = game_tile_cx(t), z = game_tile_cz(t);
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
  memset(&gen, 0, sizeof gen);
  memset(fly, 0, sizeof fly);
  memset(power_len, 0, sizeof power_len);
  if (pg.multiplier < 10) pg.multiplier = 10;
  path_first = path_count = 0;
  stub_on = false;
  gen.theme = gen.want_theme = TH_TEMPLE;
  coin_value_level();
  pacing();
  Tile t;
  memset(&t, 0, sizeof t);
  t.kind = PK_STRAIGHT;
  push_tile(&t);
  pl.tile = 0;
  pl.v = START_V, pl.z = -20, pl.on_ground = pl.over = true;
  pl.stumble_timer = 7;
  pl.since_power = 15;
  lay_ahead();
  trail_n = 0, trail_head = 0, trail_gap = 0;
  for (int i = 0; i < 40; i++) {
    trail_add();
    trail[trail_head].z = pl.z - (float)(40 - i);
  }
  pursue();
}

void game_step(uint32_t in) {
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
  /* along, and across toward the path's middle (and the lean) */
  const Tile *t = tile_at(pl.tile);
  float a0, s0;
  local(t, pl.x, pl.z, &a0, &s0);
  float fx = (float)DX[pl.dir], fz = (float)DZ[pl.dir], rx = fz, rz = -fx;
  pl.x += fx * pl.v * DT, pl.z += fz * pl.v * DT;
  if (!pl.falling) {
    float side = (pl.x - game_tile_cx(t)) * rx + (pl.z - game_tile_cz(t)) * rz;
    float move = (pl.lean - side) * 0.1f * 50 * 1.125f * DT;
    pl.x += rx * move, pl.z += rz * move;
  }
  pl.vy -= GRAVITY * DT;
  pl.y += pl.vy * DT;
  follow_tile();
  t = tile_at(pl.tile);
  float a1, s1;
  local(t, pl.x, pl.z, &a1, &s1);
  if (a1 - a0 > 20 || a0 - a1 > 20) a0 = a1;   /* (a new tile) */
  /* the floor */
  float fy;
  bool over = game_floor(pl.x, pl.z, &fy, NULL);
#ifdef TR_PERF
  if (!over) over = true, fy = pl.ground;
#endif
  if (!over && pl.boost && !pl.falling) over = true, fy = pl.ground;   /* (the boost carries it over gaps) */
  pl.over = over;
  if (over && pl.y <= fy + 0.001f && pl.y >= fy - 4) {
    pl.y = fy, pl.vy = 0;
    pl.on_ground = true, pl.jumping = false, pl.falling = false;
    pl.ground = fy;
  } else {
    pl.on_ground = false;
    if (!over || pl.y < fy - 4) {
      if (!pl.falling) pl.falling = true, pl.t_fall = 0, pl.sliding = false;
      pl.t_fall += DT;
    } else if (over && !pl.jumping)
      pl.ground = fy;
  }
  if (pl.y < pl.ground - 40) {
    die(DEATH_FALL);
    if (pl.dead) return;
  }
  obstacles(a0, a1);
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
    gen.since_level += d, gen.since_section += d;
    if (gen.since_level > (gen.theme == TH_TEMPLE ? gen.level_temple : gen.level_other)) {
      gen.since_level = 0;
      if (rnd(4) <= 2 && gen.want_theme == gen.theme) change_level();
    }
    if ((!gen.fast && gen.since_section > gen.turn_section) || (gen.fast && gen.since_section > 100)) {
      gen.since_section = 0;
      if (rnd(4) <= 2) gen.fast = !gen.fast;
    }
    pacing();
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
