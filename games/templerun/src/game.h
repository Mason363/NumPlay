/* Temple Run: the run (game.c) - the path, the runner, the pursuers, coins and power-ups - and the progress
 * kept between runs. The numbers are the game's own (its Android version 1.0.8), in its units: a path tile
 * is 60 across, the runner 9.6 tall, speeds in units a second. */
#ifndef TR_GAME_H
#define TR_GAME_H
#include <stdbool.h>
#include <stdint.h>

#define TILE 60.0f
#define HALF_W 10.0f           /* half the walkway */
#define NTILES 40              /* the path kept: behind, under and ahead of the runner */
#define NSTUB 2                /* a junction's two ways, this many tiles each, until one is taken */

enum { TH_TEMPLE, TH_PLANK, TH_CLIFF, TH_COUNT };
enum { PK_STRAIGHT, PK_HOLE, PK_HOLE_LONG, PK_NARROW_L, PK_NARROW_R, PK_CORNER_L, PK_CORNER_R, PK_T, PK_STEP_UP,
       PK_STEP_DOWN, PK_COUNT };
enum { OB_NONE, OB_ROOT, OB_TREE, OB_BRANCH, OB_FIRE };
enum { IT_NONE, IT_COIN_BONUS, IT_INVISIBILITY, IT_MAGNET, IT_BOOST };
enum { COIN_NONE, COIN_CENTRE, COIN_LEFT, COIN_RIGHT, COIN_SLANT_L, COIN_SLANT_R, COIN_ARC };
enum { DEATH_NONE, DEATH_FALL, DEATH_TREE, DEATH_BRANCH, DEATH_FIRE, DEATH_CAUGHT };
/* upgrades (levels 0..5) and things to use (counts) */
enum { UP_MAGNET, UP_INVISIBILITY, UP_COIN_BONUS, UP_COIN_VALUE, UP_BOOST, UP_COUNT };
enum { USE_WINGS, USE_HEADSTART, USE_MEGA, USE_COUNT };
#define UP_MAX 5

typedef struct {
  int16_t gx, gz;          /* its square: centre (gx, gz) x 60 */
  uint8_t dir;             /* the way the run goes into it: 0 +z, 1 +x, 2 -z, 3 -x */
  uint8_t kind, theme, ob;
  uint8_t coin, coin_kind; /* how its coins lie (COIN_*), and which (1 yellow, 2 red, 3 blue) */
  uint8_t coins;           /* the ones still there (bits) */
  uint8_t item;            /* a power-up at its middle (IT_*) */
  uint8_t scenery;         /* 1 a jungle tree left, 2 right, 4 a tall one, 8 a pillar */
  uint8_t hit;             /* its obstacle already hit (one stumble a root) */
  float y;                 /* the floor where the run comes in */
} Tile;

typedef struct {
  float x, y, z;           /* feet */
  float v;                 /* run speed */
  float vy;
  uint8_t dir;             /* the way it runs */
  float lean;              /* -6 left, 0, 6 right: where it keeps across the path (4 and 6) */
  float ground;            /* the floor under it */
  bool over, on_ground, jumping, falling, sliding, stumbling, dead;
  float t_slide, slide_len, t_stumble, stumble_len, t_fall;
  float stumble_timer;     /* time since the last stumble: under 10, another one gets it caught */
  int death;
  float t_dead;
  float death_v;           /* its speed when it last died (it catches up after being saved) */
  /* power-ups */
  float invis, magnet, wings, wings_recharge;
  bool boost, mega;
  float boost_left, v_before_boost;
  float since_power;
  int turn_wish;           /* -1 left, 1 right: pressed before the corner (kept 1 s) */
  float t_wish;
  int tile;                /* the tile it is on (an index in path) */
  float anim;              /* the run cycle */
} Runner;

typedef struct {
  uint32_t score, coins, bonus_coins, bonus_level;
  float dist, dist_left;   /* distance (the game's meters), and what is not yet in the score */
  float dist_no_coins;     /* distance before the first coin */
  float dist_no_trip;      /* the longest stretch without a stumble */
  float since_trip;
  uint32_t score_before_powers;
  bool used_powers;
  int saved;               /* times the wings saved it */
  int magnets, coin_bonuses, invisibilities, headstarts;
  int turns, stumbles;
  float time;
} RunStats;

/* what is kept between runs (save.c) */
typedef struct {
  uint32_t coins, best, best_dist, runs;
  uint32_t life_coins, life_dist;
  uint8_t up[UP_COUNT];
  uint8_t help;            /* how to play seen */
  uint8_t multiplier;      /* the score multiplier: 10, one more an objective */
  uint16_t use[USE_COUNT];
  uint16_t pad;
  uint32_t done[2];        /* objectives done (bits) */
} Progress;

extern Progress pg;
extern Runner pl;
extern RunStats run;
extern Tile path[NTILES];
extern int path_first, path_count;   /* the oldest tile, and how many */
extern Tile stub[2][NSTUB];          /* a junction's ways, left [0] and right [1], until one is taken */
extern bool stub_on;

/* coins flying to the runner (the coin magnet) */
#define NFLY 24
typedef struct {
  float x, y, z;
  uint8_t kind, on;
} Fly;
extern Fly fly[NFLY];

/* the pursuers: where each one is (feet) and the way it faces (radians), whether they are seen */
typedef struct {
  float x, y, z, yaw, phase;
} Pursuer;
#define NPURSUERS 3
extern Pursuer pursuers[NPURSUERS];
extern bool pursuers_on;

static inline Tile *tile_at(int i) { return &path[(path_first + i) % NTILES]; }
void game_new_run(void);
/* a 60th of a second: keys held (lean) and pressed (turn, jump, slide) */
enum { IN_LEFT = 1, IN_RIGHT = 2, IN_JUMP = 4, IN_SLIDE = 8, IN_LEAN_L = 16, IN_LEAN_R = 32, IN_WINGS = 64,
       IN_HEADSTART = 128, IN_MEGA = 256 };
void game_step(uint32_t in);
/* the floor at a world point (false: none there); *t: the tile found (or NULL) */
bool game_floor(float x, float z, float *y, const Tile **t);
/* where a tile's coin k is (world), false if it has none there */
bool game_coin_pos(const Tile *t, int k, float *x, float *y, float *z);
float game_tile_cx(const Tile *t);
float game_tile_cz(const Tile *t);
/* can a head start be used now, which (1 head start, 3 mega: bits) */
int game_headstart_ok(void);
bool game_wings_ok(void);
/* the run is over (dead, and its fall or catch seen): the results can be shown */
bool game_over(void);
/* upgrades: the price of the next level (0: all done) */
uint32_t game_up_price(int up);
uint32_t game_use_price(int use);
/* objectives */
#define NOBJ 40
const char *game_obj_text(int i);
bool game_obj_done(int i);
/* after a run: the objectives it met (their numbers into out, how many) */
int game_award(int *out, int max);
float game_power_left(int *kind);   /* the running power-up's time left (0..1) and which (IT_*) */
#endif
