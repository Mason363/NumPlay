/* Temple Run: the bot of tests/play.c (and of a timing build, make bench): turns where the path goes on, jumps and
 * slides past what is in the way. */
#include <stdlib.h>
#ifdef HOST
#define bot_coin() (rand() & 1)
#else
static uint32_t bot_seed = 12345;   /* (the calculator's C library's rand needs more than an app has) */
#define bot_coin() ((bot_seed = bot_seed * 1103515245u + 12345u) >> 16 & 1)
#endif
#include "../src/data.h"
#include "../src/floor.h"
#include "../src/game.h"
#include "../src/path.h"
#include "../src/plat.h"
static const int DX[4] = {0, 1, 0, -1}, DZ[4] = {1, 0, -1, 0};

/* ground at a point, on any piece laid */
static bool ground_at(float x, float z) {
  int gx = (int)(x / TILE + (x >= 0 ? 0.5f : -0.5f)), gz = (int)(z / TILE + (z >= 0 ? 0.5f : -0.5f));
  for (int i = 0; i < NTILES; i++)
    if (tiles[i].alive && tiles[i].gx == gx && tiles[i].gz == gz && path_ground(&tiles[i], x, z)) return true;
  return false;
}

/* how far ahead the ground goes on, at lean l */
static float ground_ahead(float l, float upto) {
  int d = pl.dir;
  float fx = (float)DX[d], fz = (float)DZ[d], rx = fz, rz = -fx;
  const Tile *t = &tiles[path_root];
  float s = (pl.x - path_cx(t)) * rx + (pl.z - path_cz(t)) * rz;
  for (float a = 1; a <= upto; a += 2)
    if (!ground_at(pl.x + fx * a + rx * (l - s), pl.z + fz * a + rz * (l - s))) return a;
  return upto + 1;
}

static uint32_t bot(void) {
  static int wait, at_gx = 1 << 20, at_gz, side;
  if (pl.dead) return 0;
  uint32_t k = 0;
  int d = pl.dir;
  float fx = (float)DX[d], fz = (float)DZ[d];
  /* lean where the ground goes on farthest (narrow paths) */
  bool narrow = tiles[path_root].obst == OB_NARROW || (path_next(path_root, d) >= 0 && tiles[path_next(path_root, d)].obst == OB_NARROW);
  if (narrow) {
    float g0 = ground_ahead(0, 60), gl = ground_ahead(-6, 60), gr = ground_ahead(6, 60);
    if (g0 < 50 && (gl > g0 + 4 || gr > g0 + 4)) k |= gl >= gr ? K_LEAN_L : K_LEAN_R;
  }
  if (wait > 0) {
    wait--;
    return k;
  }
  /* corners and junctions: turn where the path goes on (straight on, if it does) */
  for (int i = path_root, n = 0; i >= 0 && n < 3; i = path_next(i, d), n++) {
    const Tile *t = &tiles[i];
    if (!path_is_turn(t) || t->turned) continue;
    float a = (path_cx(t) - pl.x) * fx + (path_cz(t) - pl.z) * fz;
    if (a > pl.v * 0.4f || a < -6) break;
    if (path_opens(t, d)) break;
    bool l = path_opens(t, (d + 3) & 3), r = path_opens(t, (d + 1) & 3);
    if (at_gx != t->gx || at_gz != t->gz) at_gx = t->gx, at_gz = t->gz, side = l && r ? (bot_coin() ? 1 : -1) : l ? -1 : 1;
    wait = 10;
    return k | (side < 0 ? K_LEFT : K_RIGHT);
  }
  /* obstacles: jump the roots and trunks, slide under branches and fire */
  for (int i = path_root, n = 0; i >= 0 && n < 2; i = path_next(i, d), n++) {
    const Tile *t = &tiles[i];
    for (int o = 0; o < floor_nobstacles; o++) {
      const FloorObstacle *ob = &floor_obstacles[o];
      if (ob->piece != t->mesh || ob->height <= t->y + 0.01f) continue;
      float lo = 1e9f, hi = -1e9f;
      for (int p = 0; p < ob->n; p++) {
        float a = (path_cx(t) + ob->p[p][0] * 0.25f - pl.x) * fx + (path_cz(t) + ob->p[p][1] * 0.25f - pl.z) * fz;
        lo = a < lo ? a : lo, hi = a > hi ? a : hi;
      }
      if (hi < 0) continue;
      if (ob->flags & FO_SLIDE) {
        float len = pl.v * 0.007f > 0.75f ? 0.75f : pl.v * 0.007f;
        if (lo < pl.v * len * 0.5f && !pl.sliding && !pl.jumping) {
          wait = 6;
          return k | K_DOWN;
        }
      } else if (lo < 4 + pl.v * 0.06f && pl.on_ground && !pl.stumbling) {
        wait = 12;
        return k | K_UP;
      }
    }
  }
  /* gaps, and steps up: jump at the edge */
  float lean = (k & K_LEAN_L) ? -6 : (k & K_LEAN_R) ? 6 : 0;
  float edge = ground_ahead(lean, 40);
  if (edge < 4 + pl.v * 0.05f && pl.on_ground && !pl.stumbling) {
    int next = path_next(path_root, d);
    const Tile *t = &tiles[path_root];
    bool corner = path_is_turn(t) && !path_opens(t, d);
    if (!corner && !(next >= 0 && tiles[next].y < t->y - 1 && tiles[next].obst != OB_HOLE)) {
      wait = 12;
      return k | K_UP;
    }
  }
  return k;
}
