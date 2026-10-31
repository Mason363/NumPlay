/* Temple Run: the path (path.c), laid as the game's PathElement lays it: a tree of pieces of 60 x 60, each the
 * game's prefab for its type and place (data.h's first meshes, floor.c's ground and obstacles), grown ten pieces
 * deep along every way from the piece under the runner, the ways not taken pruned as it goes. Pieces are laid as
 * they are, unturned: "V" ones run along z, "H" ones along x. */
#ifndef TR_PATH_H
#define TR_PATH_H
#include <stdbool.h>
#include <stdint.h>
#include "floor.h"

#define TILE 60.0f
#define NTILES 128

/* the game's PathType, in its order */
enum {
  PT_NONE, PT_STRAIGHT_V, PT_STRAIGHT_H, PT_TURN_A, PT_TURN_B, PT_TURN_C, PT_TURN_D, PT_T_LEFT, PT_T_RIGHT, PT_T_UP,
  PT_T_DOWN, PT_HOLE_V, PT_HOLE_LA_V, PT_HOLE_LB_V, PT_HOLE_H, PT_HOLE_LA_H, PT_HOLE_LB_H, PT_NARROW_V, PT_NARROW_H,
  PT_NL_LEFT_V, PT_NL_LEFT_A_V, PT_NL_LEFT_B_V, PT_NL_RIGHT_V, PT_NL_RIGHT_A_V, PT_NL_RIGHT_B_V, PT_NL_UP_H,
  PT_NL_UP_A_H, PT_NL_UP_B_H, PT_NL_DOWN_H, PT_NL_DOWN_A_H, PT_NL_DOWN_B_H, PT_FLAME_H, PT_FLAME_V, PT_CAP_A_H,
  PT_CAP_B_H, PT_CAP_A_V, PT_CAP_B_V, PT_TEMPLE, PT_MAX
};
/* the places (the game's PathLevel), and their heights */
enum { TH_TEMPLE, TH_PLANK, TH_CLIFF, TH_COUNT };
/* the game's ObstacleType */
enum { OB_NONE, OB_HOLE, OB_NARROW, OB_STUMBLE, OB_JUMP, OB_SLIDE, OB_FLAME, OB_LEVEL };
enum { D_N, D_E, D_S, D_W };   /* +z, +x, -z, -x */
enum { COIN_NONE, COIN_CENTRE, COIN_LEFT, COIN_RIGHT, COIN_ARC };
enum { IT_NONE, IT_COIN_BONUS, IT_INVISIBILITY, IT_MAGNET, IT_BOOST };

typedef struct {
  int16_t gx, gz;          /* its square: centre (gx, gz) x 60 */
  uint8_t alive, type, level;
  int16_t mesh;            /* its prefab (data.h), -1: none (a gap) */
  uint8_t obst, has_obstacles;
  uint8_t dir;             /* the way the run goes into it */
  int8_t next[4];          /* the pieces on its sides (N E S W), -1 */
  /* the game's spacing, from its parent on */
  uint8_t since_turn, since_obstacle, back_to_back, since_coins, coin_count, since_bonus;
  uint8_t coin, coin_kind, coins;   /* how its coins lie (COIN_*), which (1 yellow, 2 red, 3 blue), those still there */
  uint8_t item, skipped_item;
  uint8_t turned;
  float y;                 /* its level's height */
} Tile;

extern Tile tiles[NTILES];
extern int path_root;      /* the piece under the runner */

/* the game's pacing (GameController's HandlePacing): by distance run, the time since the last power-up */
void path_pace(float dist, float since_power, float dd);
void path_start(void);                    /* the temple, and the path from it */
void path_advance(int to);                /* the runner is on the piece next to the root: the rest pruned, laid on */
int path_at(float x, float z);            /* the piece at a point: the root or one next to it (-1: none) */
bool path_ground(const Tile *t, float x, float z);   /* inside the piece's ground */
bool path_opens(const Tile *t, int side);
bool path_is_turn(const Tile *t);
bool path_is_straight(const Tile *t);    /* a plain straight piece (no obstacle) */
int path_next(int i, int d);             /* the piece on that side (-1) */
/* the obstacle the runner's step from (x0, z0) to (x1, z1) runs into (FO_* | 8, 0: none); its feet at y */
int path_hit(const Tile *t, float x0, float z0, float x1, float z1, float y, bool sliding);
bool path_coin_pos(const Tile *t, int k, float *x, float *y, float *z);
static inline float path_cx(const Tile *t) { return t->gx * TILE; }
static inline float path_cz(const Tile *t) { return t->gz * TILE; }
#endif
