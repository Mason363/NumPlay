#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif
/* The path (path.h), laid as the game's PathElement lays it: which piece types may follow which on each side,
 * when turns, gaps, narrow paths, fire and trees may come (the game's spacing, kept piece by piece from the
 * parent), how two gaps make a long one, how a change of place drops the path down a level or makes the runner
 * jump up one, and which of the game's prefabs each piece is. */
#include <string.h>
#include "data.h"
#include "game.h"
#include "path.h"
#include "plat.h"

_Static_assert(M_PIECES == FLOOR_PIECES, "data.h and floor.c list the same pieces");

Tile tiles[NTILES];
int path_root = -1;
static int root_origin = D_S;   /* the side of the root the runner came in by */

static const int8_t DX[4] = {0, 1, 0, -1}, DZ[4] = {1, 0, -1, 0};
static const float LEVEL_Y[TH_COUNT] = {0, -15, 15};
#define OPP(d) ((d) ^ 2)
#define B(t) (1ull << (t))
#define DEPTH 10   /* pieces laid ahead along every way (GameController: AddRandomPath(10)) */

static int rnd(int n) { return n > 1 ? (int)(plat_random() % (uint32_t)n) : 0; }
static bool half(void) { return rnd(2) == 0; }

/* ---------------------------------------------------------------- the game's switches (PathElement's statics) */
static struct {
  int level;                       /* sPathLevel: where the path goes */
  bool fast;                       /* a fast turn section */
  bool slide, flames, narrows, bonus;
  int gap_turns, gap_obs, gap_coins;
  float dbl;
  float since_level, since_section, red_at, blue_at;
} ps;

/* GameController's HandlePacing: what may come, how far apart, by the distance run */
void path_pace(float d, float since_power, float dd) {
  float level_temple = 500, level_other = 200, section = 1000;
  if (d < 250) ps.gap_coins = 12, ps.gap_turns = 12, ps.gap_obs = 6, ps.dbl = 0;
  else if (d < 500) ps.gap_coins = 10, ps.gap_turns = 10, ps.gap_obs = 5, ps.dbl = 0;
  else if (d < 1000) ps.gap_coins = 9, ps.gap_turns = 8, ps.gap_obs = 4, ps.dbl = 0.05f;
  else if (d < 1500) ps.gap_coins = 8, ps.gap_turns = 6, ps.gap_obs = 4, ps.dbl = 0.05f;
  else if (d < 2000) ps.gap_coins = 7, ps.gap_turns = 5, ps.gap_obs = 3, ps.dbl = 0.1f;
  else if (d < 3250) ps.gap_coins = 6, ps.gap_turns = 4, ps.gap_obs = 3, ps.dbl = 0.15f;
  else if (d < 4500) ps.gap_coins = 6, ps.gap_turns = 3, ps.gap_obs = 3, ps.dbl = 0.175f;
  else ps.gap_coins = 6, ps.gap_turns = 2, ps.gap_obs = 2, ps.dbl = 0.2f;
  if (d >= 3250) level_temple = 375, level_other = 300, section = 750;
  else if (d >= 1000) level_temple = 450, level_other = 250, section = 850;
  ps.slide = ps.flames = d >= 500;
  ps.narrows = d >= 1500;
  ps.bonus = since_power >= 15;
  /* a change of place now and then, fast turn sections */
  ps.since_level += dd, ps.since_section += dd;
  if (ps.since_level > (ps.level == TH_TEMPLE ? level_temple : level_other)) {
    ps.since_level = 0;
    if (rnd(4) <= 2) {
      if (ps.level == TH_PLANK) ps.level = TH_TEMPLE;
      else if (ps.level == TH_TEMPLE) ps.level = half() ? TH_PLANK : TH_CLIFF;
      else ps.level = half() ? TH_TEMPLE : TH_PLANK;
    }
  }
  if ((!ps.fast && ps.since_section > section) || (ps.fast && ps.since_section > 100)) {
    ps.since_section = 0;
    if (rnd(4) <= 2) ps.fast = !ps.fast;
  }
  if (ps.fast) {
    ps.gap_turns = (int)((float)ps.gap_turns * 0.65f);
    if (ps.gap_turns < 2) ps.gap_turns = 2;
    ps.gap_coins = 0;
  }
  static const float red[6] = {1e9f, 1500, 1000, 1000, 1000, 1000}, blue[6] = {1e9f, 1e9f, 1e9f, 3000, 2500, 2000};
  int l = pg.up[UP_COIN_VALUE] > 5 ? 5 : pg.up[UP_COIN_VALUE];
  ps.red_at = red[l], ps.blue_at = blue[l];
}

/* ---------------------------------------------------------------- the types */
static bool is_vert(int t) {
  return t == PT_STRAIGHT_V || (t >= PT_HOLE_V && t <= PT_HOLE_LB_V) || t == PT_NARROW_V ||
         (t >= PT_NL_LEFT_V && t <= PT_NL_RIGHT_B_V) || t == PT_FLAME_V || t == PT_CAP_A_V || t == PT_CAP_B_V ||
         t == PT_TEMPLE;
}
static bool is_horz(int t) {
  return t == PT_STRAIGHT_H || (t >= PT_HOLE_H && t <= PT_HOLE_LB_H) || t == PT_NARROW_H ||
         (t >= PT_NL_UP_H && t <= PT_NL_DOWN_B_H) || t == PT_FLAME_H || t == PT_CAP_A_H || t == PT_CAP_B_H;
}
static bool turn_type(int t) { return t >= PT_TURN_A && t <= PT_T_DOWN; }
static bool hole_type(int t) { return t >= PT_HOLE_V && t <= PT_HOLE_LB_H; }
static bool narrow_type(int t) { return t >= PT_NARROW_V && t <= PT_NL_DOWN_B_H; }
static bool cap_type(int t) { return t >= PT_CAP_A_H && t <= PT_CAP_B_V; }
static bool obstacle_type(int t) { return t >= PT_HOLE_V && t <= PT_CAP_B_V; }

/* CanPathExtendOnSide: the sides a type opens on */
static uint8_t opens(int t) {
  if (is_vert(t)) return 1 << D_N | 1 << D_S;
  if (is_horz(t)) return 1 << D_E | 1 << D_W;
  switch (t) {
  case PT_TURN_A: return 1 << D_W | 1 << D_S;
  case PT_TURN_B: return 1 << D_E | 1 << D_S;
  case PT_TURN_C: return 1 << D_W | 1 << D_N;
  case PT_TURN_D: return 1 << D_E | 1 << D_N;
  case PT_T_RIGHT: return 1 << D_N | 1 << D_E | 1 << D_S;
  case PT_T_LEFT: return 1 << D_N | 1 << D_S | 1 << D_W;
  case PT_T_UP: return 1 << D_N | 1 << D_E | 1 << D_W;
  case PT_T_DOWN: return 1 << D_E | 1 << D_S | 1 << D_W;
  default: return 0;
  }
}

/* the long narrow paths: the middle piece, its two ends (A at the south or west end) */
static const uint8_t narrow_groups[4][3] = {{PT_NL_LEFT_V, PT_NL_LEFT_A_V, PT_NL_LEFT_B_V},
                                            {PT_NL_RIGHT_V, PT_NL_RIGHT_A_V, PT_NL_RIGHT_B_V},
                                            {PT_NL_UP_H, PT_NL_UP_A_H, PT_NL_UP_B_H},
                                            {PT_NL_DOWN_H, PT_NL_DOWN_A_H, PT_NL_DOWN_B_H}};

/* ACertainTypeCheck: both pieces of the same long narrow path */
static bool same_narrow(int a, int b) {
  for (int g = 0; g < 4; g++) {
    const uint8_t *n = narrow_groups[g];
    if ((a == n[0] || a == n[1] || a == n[2]) && (b == n[0] || b == n[1] || b == n[2])) return true;
  }
  return false;
}

/* WillPieceTypeConnectOnSide: the types that may follow a piece of type p on side d */
static uint64_t may_follow(int p, int d) {
  static const uint64_t follow[4] = {
    B(PT_STRAIGHT_V) | B(PT_TURN_A) | B(PT_TURN_B) | B(PT_T_RIGHT) | B(PT_T_LEFT) | B(PT_T_DOWN) | B(PT_HOLE_V) |
      B(PT_HOLE_LA_V) | B(PT_NARROW_V) | B(PT_NL_LEFT_A_V) | B(PT_NL_RIGHT_A_V) | B(PT_FLAME_V) | B(PT_CAP_A_V) |
      B(PT_CAP_B_V) | B(PT_TEMPLE),
    B(PT_STRAIGHT_H) | B(PT_TURN_A) | B(PT_TURN_C) | B(PT_T_LEFT) | B(PT_T_UP) | B(PT_T_DOWN) | B(PT_HOLE_H) |
      B(PT_HOLE_LA_H) | B(PT_NARROW_H) | B(PT_NL_UP_A_H) | B(PT_NL_DOWN_A_H) | B(PT_FLAME_H) | B(PT_CAP_A_H) |
      B(PT_CAP_B_H),
    B(PT_STRAIGHT_V) | B(PT_TURN_C) | B(PT_TURN_D) | B(PT_T_RIGHT) | B(PT_T_LEFT) | B(PT_T_UP) | B(PT_HOLE_V) |
      B(PT_HOLE_LB_V) | B(PT_NARROW_V) | B(PT_NL_LEFT_B_V) | B(PT_NL_RIGHT_B_V) | B(PT_FLAME_V) | B(PT_CAP_A_V) |
      B(PT_CAP_B_V) | B(PT_TEMPLE),
    B(PT_STRAIGHT_H) | B(PT_TURN_B) | B(PT_TURN_D) | B(PT_T_RIGHT) | B(PT_T_UP) | B(PT_T_DOWN) | B(PT_HOLE_H) |
      B(PT_HOLE_LB_H) | B(PT_NARROW_H) | B(PT_NL_UP_B_H) | B(PT_NL_DOWN_B_H) | B(PT_FLAME_H) | B(PT_CAP_A_H) |
      B(PT_CAP_B_H)};
  bool vert = d == D_N || d == D_S;
  for (int g = 0; g < 4; g++) {
    const uint8_t *n = narrow_groups[g];
    if ((g < 2) != vert) continue;
    int first = d == D_N || d == D_E ? n[1] : n[2], last = d == D_N || d == D_E ? n[2] : n[1];
    if (p == first || p == n[0]) return B(n[0]) | B(last);
    if (p == last) return B(vert ? PT_STRAIGHT_V : PT_STRAIGHT_H);
  }
  if ((p == PT_NARROW_V && vert) || (p == PT_NARROW_H && !vert)) return B(vert ? PT_STRAIGHT_V : PT_STRAIGHT_H);
  if (d == D_N && p == PT_HOLE_LA_V) return B(PT_STRAIGHT_V) | B(PT_HOLE_LB_V);
  if (d == D_S && p == PT_HOLE_LB_V) return B(PT_STRAIGHT_V) | B(PT_HOLE_LA_V);
  if (d == D_E && p == PT_HOLE_LA_H) return B(PT_STRAIGHT_H) | B(PT_HOLE_LB_H);
  if (d == D_W && p == PT_HOLE_LB_H) return B(PT_STRAIGHT_H) | B(PT_HOLE_LA_H);
  return follow[d];
}

/* GetPrefabNameForType: the prefab of a type in a place (-1: none) */
static int prefab(int t, int level) {
  if (level == TH_PLANK) {
    if (t == PT_STRAIGHT_V) return M_plankStraightVert;
    if (t == PT_STRAIGHT_H) return M_plankStraightHorz;
    if (t >= PT_TURN_A && t <= PT_TURN_D) return M_plankTurnA + (t - PT_TURN_A);
    if (t >= PT_T_LEFT && t <= PT_T_DOWN) return M_plankTLeft + (t - PT_T_LEFT);
    if (t >= PT_HOLE_V && t <= PT_HOLE_LB_V) return M_plankStraightHoleVert;
    if (t >= PT_HOLE_H && t <= PT_HOLE_LB_H) return M_plankStraightHoleHorz;
    if (t == PT_FLAME_H) return M_plankStraightTowerHorz;
    if (t == PT_FLAME_V) return M_plankStraightTowerVert;
    return -1;
  }
  if (level == TH_CLIFF) {
    if (t == PT_STRAIGHT_V) return M_cliffStraightVert;
    if (t == PT_STRAIGHT_H) return M_cliffStraightHorz;
    if (t >= PT_TURN_A && t <= PT_TURN_D) return M_cliffTurnA + (t - PT_TURN_A);
    if (t >= PT_T_LEFT && t <= PT_T_DOWN) return M_cliffTLeft + (t - PT_T_LEFT);
    if (t == PT_HOLE_LA_V || t == PT_CAP_A_V) return M_cliffToTempleCapAVert;
    if (t == PT_HOLE_LB_V || t == PT_CAP_B_V) return M_cliffToTempleCapBVert;
    if (t == PT_HOLE_LA_H || t == PT_CAP_A_H) return M_cliffToTempleCapAHorz;
    if (t == PT_HOLE_LB_H || t == PT_CAP_B_H) return M_cliffToTempleCapBHorz;
    return -1;
  }
  switch (t) {
  case PT_STRAIGHT_V:
  case PT_STRAIGHT_H: {
    bool v = t == PT_STRAIGHT_V;
    if (rnd(20) == 0) return v ? M_templeStraightTowerVert : M_templeStraightTowerHorz;
    int n = rnd(10);
    if (n <= 5) return v ? M_templeStraightVert : M_templeStraightHorz;
    return (v ? M_templeStraightVert2 : M_templeStraightHorz2) + (n - 6);
  }
  case PT_HOLE_V: return M_templeStraightHoleVert;
  case PT_HOLE_LA_V: return M_templeStraightHoleLongAVert;
  case PT_HOLE_LB_V: return M_templeStraightHoleLongBVert;
  case PT_HOLE_H: return M_templeStraightHoleHorz;
  case PT_HOLE_LA_H: return M_templeStraightHoleLongAHorz;
  case PT_HOLE_LB_H: return M_templeStraightHoleLongBHorz;
  case PT_NARROW_V: return half() ? M_templeStraightNarrowSmallRightVert : M_templeStraightNarrowSmallLeftVert;
  case PT_NARROW_H: return M_templeStraightNarrowSmallDownHorz;
  case PT_FLAME_H: return M_templeStraightFlameTowerHighHorz;
  case PT_FLAME_V: return M_templeStraightFlameTowerHighVert;
  case PT_CAP_A_V: return M_templeToPlankCapAVert;
  case PT_CAP_A_H: return M_templeToPlankCapAHorz;
  case PT_CAP_B_V: return M_templeToPlankCapBVert;
  case PT_CAP_B_H: return M_templeToPlankCapBHorz;
  case PT_TEMPLE: return M_temple;
  default:
    if (t >= PT_TURN_A && t <= PT_TURN_D) return M_templeTurnA + (t - PT_TURN_A);
    if (t >= PT_T_LEFT && t <= PT_T_DOWN) return M_templeTLeft + (t - PT_T_LEFT);
    if (t >= PT_NL_LEFT_V && t <= PT_NL_DOWN_B_H) return M_templeStraightNarrowLongLeftVert + (t - PT_NL_LEFT_V);
    return -1;
  }
}

/* SetPathType and SetupTerrain: a piece's type, its prefab, and what that makes it */
static void set_type(Tile *t, int type, int level) {
  t->type = (uint8_t)type, t->level = (uint8_t)level;
  t->y = LEVEL_Y[level];
  t->mesh = (int16_t)prefab(type, level);
  if (t->mesh < 0) return;
  if (obstacle_type(type)) t->has_obstacles = 1;
  if (hole_type(type)) t->obst = OB_HOLE;
  else if (narrow_type(type)) t->obst = OB_NARROW;
  else if (type == PT_FLAME_H || type == PT_FLAME_V) t->obst = OB_FLAME;
  else if (cap_type(type)) t->obst = OB_LEVEL;
}

/* AddObstacle: a tree on a plain straight piece (its prefab becomes the tree's) */
static void add_obstacle(Tile *t, int ob) {
  if (t->has_obstacles || ob == OB_NONE || ob == OB_HOLE || ob == OB_NARROW || ob == OB_LEVEL || ob == OB_FLAME ||
      (t->type != PT_STRAIGHT_V && t->type != PT_STRAIGHT_H))
    return;
  bool v = t->type == PT_STRAIGHT_V;
  if (ob == OB_STUMBLE)
    t->mesh = v ? (half() ? M_templeStraightTreeTripLeftVert : M_templeStraightTreeTripRightVert)
                : (half() ? M_templeStraightTreeTripUpHorz : M_templeStraightTreeTripDownHorz);
  else if (ob == OB_JUMP)
    t->mesh = v ? (half() ? M_templeStraightTreeJumpLeftVert : M_templeStraightTreeJumpRightVert)
                : (half() ? M_templeStraightTreeJumpUpHorz : M_templeStraightTreeJumpDownHorz);
  else if (t->level != TH_TEMPLE)
    t->mesh = v ? M_cliffStraightTreeVert : M_cliffStraightTreeHorz;
  else
    t->mesh = v ? M_templeStraightTreeSlideVert : M_templeStraightTreeSlideHorz;
  t->obst = (uint8_t)ob;
  t->has_obstacles = 1;
}

static bool sides_have_obstacles(const Tile *t) {
  for (int d = 0; d < 4; d++)
    if (t->next[d] >= 0 && tiles[t->next[d]].has_obstacles) return true;
  return false;
}

/* ---------------------------------------------------------------- coins, power-ups, scenery */
static void coins_for(Tile *t, const Tile *p) {
  if (t->mesh < 0 || t->obst == OB_SLIDE || t->obst == OB_NARROW || t->obst == OB_LEVEL) return;
  if (!p->coin_count && p->since_coins < ps.gap_coins) return;
  if (t->obst == OB_FLAME && half()) return;
  if (p->coin_count && p->coin_kind) {
    t->coin_kind = p->coin_kind;
    t->coin = p->coin == COIN_ARC ? COIN_CENTRE : p->coin;
  } else {
    static const uint8_t lay[3] = {COIN_CENTRE, COIN_LEFT, COIN_RIGHT};
    uint8_t kinds[3] = {1, 0, 0};
    int n = 1;
    if (run.dist >= ps.red_at) kinds[n++] = 2;
    if (run.dist >= ps.blue_at) kinds[n++] = 3;
    t->coin_kind = kinds[rnd(n)];
    t->coin = lay[rnd(3)];
  }
  if (t->has_obstacles) t->coin = COIN_ARC;
  else if (path_is_turn(t)) t->coin = COIN_CENTRE;
  t->coins = path_is_turn(t) ? 0x07 : 0x1F;
}

/* AddBonusItems: a power-up now and then on a plain straight piece */
static void bonus_for(Tile *t, const Tile *p) {
  if (!path_is_straight(t) || t->coins || !ps.bonus || p->since_bonus < 50) return;
  uint8_t items[4];
  int m = 0;
  if (pg.up[UP_COIN_BONUS]) items[m++] = IT_COIN_BONUS;
  if (pg.up[UP_INVISIBILITY]) items[m++] = IT_INVISIBILITY;
  if (pg.up[UP_MAGNET]) items[m++] = IT_MAGNET;
  if (pg.up[UP_BOOST]) items[m++] = IT_BOOST;
  if (m && half()) t->item = items[rnd(m)];
  else t->skipped_item = 1;
}

static void scenery(Tile *t) {
  t->scenery = 0;
  if (!path_is_straight(t) && t->obst != OB_HOLE) return;
  if (t->level != TH_CLIFF && rnd(10) < 3) t->scenery |= 1;
  if (rnd(10) < 3) t->scenery |= 2;
  if (rnd(3) == 0) t->scenery |= 4;
  if (t->level == TH_TEMPLE && rnd(6) == 0) t->scenery |= 8;
}

static uint8_t inc(uint8_t v) { return v < 255 ? (uint8_t)(v + 1) : v; }

/* UpdatePathDescription: the spacing, from the parent on */
static void describe(Tile *t, const Tile *p) {
  t->since_turn = turn_type(t->type) ? 0 : inc(p->since_turn);
  if (t->has_obstacles) {
    t->since_obstacle = 0;
    if (sides_have_obstacles(t)) t->back_to_back = inc(p->back_to_back);   /* (its sides are empty yet) */
    if (t->obst == OB_SLIDE || t->obst == OB_FLAME) t->since_turn = 0;
  } else
    t->since_obstacle = inc(p->since_obstacle), t->back_to_back = 0;
  if (t->coins) {
    t->coin_count = inc(p->coin_count);
    t->since_coins = t->coin_count >= 5 ? 0 : p->since_coins;
  } else
    t->since_coins = inc(p->since_coins), t->coin_count = 0;
  if (t->item) t->since_bonus = 0, t->since_turn = 0, t->since_obstacle = 0;
  else if (t->skipped_item) t->since_bonus = 0;
  else t->since_bonus = inc(p->since_bonus);
  if (cap_type(t->type) || cap_type(p->type)) t->since_obstacle = 0, t->since_turn = 0;
}

/* ---------------------------------------------------------------- laying */
static int alloc(void) {
  for (int i = 0; i < NTILES; i++)
    if (!tiles[i].alive) {
      memset(&tiles[i], 0, sizeof tiles[i]);
      for (int d = 0; d < 4; d++) tiles[i].next[d] = -1;
      tiles[i].alive = 1;
      return i;
    }
  return -1;
}

/* GetRandomPathElementForSide: the next piece on side d of piece pi (-1: none) */
static int lay(int pi, int d) {
  Tile *p = &tiles[pi];
  if (p->next[d] >= 0 || !(opens(p->type) >> d & 1)) return -1;
  bool turn_ok = p->since_turn >= ps.gap_turns, level_change = false, obs_ok = true, side_clean = false, flame_ok = true;
  if (p->since_turn == 1) obs_ok = false;
  if (p->back_to_back >= 2) obs_ok = false;
  if (p->since_obstacle < ps.gap_obs && (float)rnd(1000) >= 1000.0f * ps.dbl) obs_ok = false;
  int ptype = p->type, level = ps.level, plevel = p->level;
  if (ptype >= PT_TURN_A && ptype <= PT_TURN_D) obs_ok = false;
  else if (ptype >= PT_T_LEFT && ptype <= PT_T_DOWN) {
    if (sides_have_obstacles(p)) obs_ok = false;
    else side_clean = true;
  }
  if (p->has_obstacles) {
    turn_ok = flame_ok = false;   /* (no turns right after an obstacle) */
    if (p->obst == OB_SLIDE || p->obst == OB_FLAME) obs_ok = false;
  }
  if (plevel != ps.level) {
    turn_ok = obs_ok = flame_ok = side_clean = false;
    if ((ptype == PT_STRAIGHT_V || ptype == PT_STRAIGHT_H) && !p->has_obstacles && p->since_turn > 2 && p->since_obstacle > 2) {
      /* a change of place: the piece before ends here (a cap at its level) */
      level_change = true;
      int cap = d == D_E ? PT_CAP_A_H : d == D_W ? PT_CAP_B_H : d == D_N ? PT_CAP_A_V : PT_CAP_B_V;
      if ((plevel == TH_TEMPLE && ps.level == TH_CLIFF) || (plevel == TH_CLIFF && ps.level != TH_CLIFF) ||
          (plevel == TH_TEMPLE && ps.level == TH_PLANK)) {
        set_type(p, cap, plevel);
        p->coins = 0, p->item = 0;
      }
    } else
      level = plevel;
  }
  if (p->obst == OB_LEVEL || level_change) turn_ok = flame_ok = obs_ok = side_clean = false;
  if (ps.fast) flame_ok = obs_ok = side_clean = false;
  uint64_t ok = may_follow(p->type, d);
  uint8_t cand[PT_MAX];
  int n = 0;
  for (int i = 1; i < PT_TEMPLE + 1; i++) {
    if (!(ok >> i & 1)) continue;
    if (!turn_ok && turn_type(i)) continue;
    if (ptype == PT_STRAIGHT_V && (i == PT_T_LEFT || i == PT_T_RIGHT)) continue;   /* (no side paths) */
    if (ptype == PT_STRAIGHT_H && (i == PT_T_UP || i == PT_T_DOWN)) continue;
    bool same = same_narrow(i, ptype);
    if (!obs_ok) {
      if (!same && obstacle_type(i)) continue;
    } else if (i == PT_HOLE_V || i == PT_HOLE_H) {
      if (half()) continue;
    } else if (i == PT_FLAME_H || i == PT_FLAME_V) {
      if (!ps.flames || level == TH_CLIFF || !flame_ok || side_clean || half()) continue;
    } else if (narrow_type(i) && (!ps.narrows || level != TH_TEMPLE || p->since_turn < 2 || side_clean || half()) && !same)
      continue;
    if (i == PT_HOLE_LA_V || i == PT_HOLE_LB_V || i == PT_HOLE_LA_H || i == PT_HOLE_LB_H || i >= PT_CAP_A_H) continue;
    cand[n++] = (uint8_t)i;
  }
  if (!n) return -1;
  int pick = cand[rnd(n)], ob = OB_NONE;
  if (pick == PT_HOLE_V || pick == PT_HOLE_H) ob = OB_HOLE;
  else if (narrow_type(pick)) ob = OB_NARROW;
  if (obs_ok && !side_clean && (pick == PT_STRAIGHT_V || pick == PT_STRAIGHT_H) && half()) {
    int ch[3], m = 0;
    if (level == TH_TEMPLE) ch[m++] = OB_STUMBLE, ch[m++] = OB_JUMP;
    if (ps.slide && flame_ok && level != TH_PLANK) ch[m++] = OB_SLIDE;
    if (m) ob = ch[rnd(m)];
  }
  /* a gap on the cliffs goes on into the next piece; two gaps make a long one */
  if (p->obst == OB_HOLE && level == TH_CLIFF && (ptype == PT_HOLE_H || ptype == PT_HOLE_V)) pick = ptype;
  if (p->obst == OB_HOLE && (ptype == PT_HOLE_H || ptype == PT_HOLE_V) && (pick == PT_HOLE_H || pick == PT_HOLE_V)) {
    bool fwd = d == D_E || d == D_N;
    if (pick == PT_HOLE_H) set_type(p, fwd ? PT_HOLE_LA_H : PT_HOLE_LB_H, level), pick = fwd ? PT_HOLE_LB_H : PT_HOLE_LA_H;
    else set_type(p, fwd ? PT_HOLE_LA_V : PT_HOLE_LB_V, level), pick = fwd ? PT_HOLE_LB_V : PT_HOLE_LA_V;
  }
  /* after a change of place, a cap where the new level begins (the runner drops to it, or jumps up to it) */
  if (level_change && ((plevel == TH_CLIFF && level == TH_TEMPLE) || (plevel == TH_TEMPLE && level == TH_CLIFF) ||
                       (plevel == TH_PLANK && level == TH_TEMPLE)))
    pick = d == D_E ? PT_CAP_B_H : d == D_W ? PT_CAP_A_H : d == D_N ? PT_CAP_B_V : PT_CAP_A_V;
  int ni = alloc();
  if (ni < 0) return -1;
  p = &tiles[pi];
  Tile *t = &tiles[ni];
  t->gx = (int16_t)(p->gx + DX[d]), t->gz = (int16_t)(p->gz + DZ[d]);
  t->dir = (uint8_t)d;
  set_type(t, pick, level);
  if (ob == OB_HOLE || ob == OB_NARROW) t->obst = (uint8_t)ob;
  else add_obstacle(t, ob);
  if (!side_clean) {
    coins_for(t, p);
    bonus_for(t, p);
  }
  describe(t, p);
  scenery(t);
  p->next[d] = (int8_t)ni;
  t->next[OPP(d)] = (int8_t)pi;
  return ni;
}

/* AddRandomPath: every way from a piece, depth pieces on */
static void grow(int pi, int depth, int origin) {
  if (!depth) return;
  for (int d = 0; d < 4; d++) {
    if (d == origin || !(opens(tiles[pi].type) >> d & 1)) continue;
    int c = tiles[pi].next[d];
    if (c < 0) c = lay(pi, d);
    if (c >= 0) grow(c, depth - 1, OPP(d));
  }
}

static void destroy(int i, int from) {
  for (int d = 0; d < 4; d++)
    if (d != from && tiles[i].next[d] >= 0) destroy(tiles[i].next[d], OPP(d));
  tiles[i].alive = 0;
}

void path_start(void) {
  memset(tiles, 0, sizeof tiles);
  memset(&ps, 0, sizeof ps);
  ps.level = TH_TEMPLE;
  path_pace(0, 15, 0);
  int i = alloc();
  set_type(&tiles[i], PT_TEMPLE, TH_TEMPLE);
  tiles[i].dir = D_N;
  path_root = i, root_origin = D_S;
  grow(i, DEPTH, root_origin);
}

void path_advance(int to) {
  int r = path_root, d = -1;
  for (int k = 0; k < 4; k++)
    if (tiles[r].next[k] == to) d = k;
  if (d < 0) return;
  for (int k = 0; k < 4; k++)
    if (k != d && tiles[r].next[k] >= 0) {
      destroy(tiles[r].next[k], OPP(k));
      tiles[r].next[k] = -1;
    }
  path_root = to, root_origin = OPP(d);
  grow(to, DEPTH, root_origin);
}

/* ---------------------------------------------------------------- the ground */
static int round_(float v) { return (int)(v >= 0 ? v + 0.5f : v - 0.5f); }

int path_at(float x, float z) {
  int gx = round_(x / TILE), gz = round_(z / TILE), r = path_root;
  if (r < 0) return -1;
  if (tiles[r].gx == gx && tiles[r].gz == gz) return r;
  for (int d = 0; d < 4; d++) {
    int c = tiles[r].next[d];
    if (c >= 0 && tiles[c].gx == gx && tiles[c].gz == gz) return c;
  }
  return -1;
}

bool path_ground(const Tile *t, float x, float z) {
  if (t->mesh < 0 || t->mesh >= FLOOR_PIECES || !floor_cells[t->mesh]) return false;
  float lx = x - path_cx(t) + 30, lz = z - path_cz(t) + 30;
  if (lx < -0.5f || lz < -0.5f || lx > 60.5f || lz > 60.5f) return false;
  lx = lx < 0 ? 0 : lx > 59.9f ? 59.9f : lx;   /* (on the edge between two squares) */
  lz = lz < 0 ? 0 : lz > 59.9f ? 59.9f : lz;
  return floor_cells[t->mesh][(int)(lz * 0.5f)] >> (int)(lx * 0.5f) & 1;
}

bool path_opens(const Tile *t, int side) { return opens(t->type) >> side & 1; }
bool path_is_turn(const Tile *t) { return turn_type(t->type); }
bool path_is_straight(const Tile *t) { return (t->type == PT_STRAIGHT_V || t->type == PT_STRAIGHT_H) && !t->has_obstacles; }
int path_next(int i, int d) { return i >= 0 ? tiles[i].next[d] : -1; }

/* ---------------------------------------------------------------- obstacles */
static bool inside(const FloorObstacle *o, float x, float z) {
  bool c = false;
  for (int i = 0, j = o->n - 1; i < o->n; j = i++) {
    float xi = o->p[i][0] * 0.25f, zi = o->p[i][1] * 0.25f, xj = o->p[j][0] * 0.25f, zj = o->p[j][1] * 0.25f;
    if ((zi > z) != (zj > z) && x < (xj - xi) * (z - zi) / (zj - zi) + xi) c = !c;
  }
  return c;
}

static bool crosses(float ax, float az, float bx, float bz, float cx, float cz, float dx, float dz) {
  float d1 = (dx - cx) * (az - cz) - (dz - cz) * (ax - cx), d2 = (dx - cx) * (bz - cz) - (dz - cz) * (bx - cx);
  float d3 = (bx - ax) * (cz - az) - (bz - az) * (cx - ax), d4 = (bx - ax) * (dz - az) - (bz - az) * (dx - ax);
  return ((d1 > 0) != (d2 > 0)) && ((d3 > 0) != (d4 > 0));
}

/* HandleCollisionWithObstacles: the step from (x0, z0) to (x1, z1) across an obstacle's edge, unless it is jumped
 * (the feet at its height) or slid under, or the step starts inside it */
int path_hit(const Tile *t, float x0, float z0, float x1, float z1, float y, bool sliding) {
  if (t->mesh < 0) return 0;
  float cx = path_cx(t), cz = path_cz(t);
  x0 -= cx, z0 -= cz, x1 -= cx, z1 -= cz;
  for (int k = 0; k < floor_nobstacles; k++) {
    const FloorObstacle *o = &floor_obstacles[k];
    if (o->piece != t->mesh || inside(o, x0, z0)) continue;
    if ((o->flags & FO_JUMP) && y >= o->height) continue;
    if ((o->flags & FO_SLIDE) && sliding) continue;
    for (int i = 0, j = o->n - 1; i < o->n; j = i++)
      if (crosses(x0, z0, x1, z1, o->p[j][0] * 0.25f, o->p[j][1] * 0.25f, o->p[i][0] * 0.25f, o->p[i][1] * 0.25f))
        return o->flags | 8;
  }
  return 0;
}

/* ---------------------------------------------------------------- coins */
bool path_coin_pos(const Tile *t, int k, float *x, float *y, float *z) {
  if (t->coin == COIN_NONE || !(t->coins >> k & 1)) return false;
  float a = -25 + 12.5f * (float)k, s = 0, h = 0;
  if (t->coin == COIN_LEFT) s = -6;
  else if (t->coin == COIN_RIGHT) s = 6;
  else if (t->coin == COIN_ARC) h = 10 * (1 - (a / 30) * (a / 30));
  int d = t->dir;
  *x = path_cx(t) + a * DX[d] + s * DZ[d];
  *z = path_cz(t) + a * DZ[d] - s * DX[d];
  *y = t->y + h;
  return true;
}
