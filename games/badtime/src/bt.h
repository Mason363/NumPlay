/* Bad Time Simulator: the fight's state and rules.
 *
 * Everything runs in the original's 640 x 480 pixels, at 30 ticks a second (Undertale's own rate): the scripts'
 * numbers are used as they are, a speed v moves v / 30 px a tick, and the screen shows it at half size. The fight
 * is deterministic: the same keys and seed give the same fight on every computer (no libm, no qsort, no rand;
 * the scripts' sums in doubles, the rest in floats, -ffp-contract=off). */
#ifndef BT_H
#define BT_H
#include <stdbool.h>
#include <stdint.h>
#include "attacks.h"

/* keys (the game's own names: plat_eadk.c reads the calculator's) */
enum { BK_LEFT = 1, BK_RIGHT = 2, BK_UP = 4, BK_DOWN = 8, BK_OK = 16, BK_BACK = 32, BK_HOME = 64, BK_SLOW = 128 };

#define MAX_HP 92
#define LAYOUT_W 640.0f
#define LAYOUT_H 480.0f
#define MAX_BONES 288
#define MAX_STABS 8
#define MAX_BLASTERS 56
#define MAX_PLATS 24
#define MAX_MBONES 12
#define MAX_VARS 40
#define MAX_SHARDS 6

enum { DIR_RIGHT, DIR_DOWN, DIR_LEFT, DIR_UP };
enum { COL_WHITE, COL_BLUE, COL_ORANGE };
enum { HEART_RED, HEART_BLUE };
/* the opponent's poses (the scripts' names) */
enum { ANIM_NONE, ANIM_IDLE, ANIM_HEADBOB, ANIM_TIRED };
enum { BODY_UP, BODY_DOWN, BODY_LEFT, BODY_RIGHT, BODY_NONE };
enum { TORSO_DEFAULT, TORSO_SHRUG };
enum { HEAD_DEFAULT, HEAD_LOOKLEFT, HEAD_WINK, HEAD_CLOSED, HEAD_NOEYES, HEAD_BLUEEYE, HEAD_TIRED1, HEAD_TIRED2 };

typedef struct {
  float x, y, w, h;      /* top-left and size */
  float vx, vy;          /* px a tick */
  uint8_t dir, color, karma, vertical;
} bone_t;

typedef struct {
  float x, y, w, h;      /* the bones (or, while warning, the warning box) */
  float dest_x, dest_y, speed;
  uint8_t dir, state, karma, pad;
  int16_t warn, stay;    /* ticks */
  float distance;
} stab_t;
enum { STAB_WARN, STAB_IN, STAB_STAY, STAB_OUT };

typedef struct {
  float x, y, end_x, end_y, ang, end_ang;
  float sx, sy;          /* its scale across and along (size 0: 2 x 1) */
  float scale;           /* the beam's scale: 1, 2 or 3 */
  float leave;           /* backing off, px/s */
  float base, sine;      /* the beam's width (35 * scale when full) and its wobble */
  int16_t timer;         /* ticks: waiting, then opening */
  int16_t blast;         /* the beam's hold, ticks */
  int16_t beam_t;        /* ticks since it fired */
  uint8_t state, size, karma, hurts;
  uint8_t opacity;       /* the beam's, 0-100 */
  uint8_t pad[3];
} blaster_t;
enum { BL_ENTER, BL_WAIT, BL_FIRE, BL_LEAVE };

typedef struct {
  float x, y, w, vx;
  uint8_t dir, reverse, pad[2];
} plat_t;

typedef struct {
  float x, y, t;
  uint8_t kind, state, button, pad;
} mbone_t;
enum { MB_LEFT, MB_BOTTOM };

typedef struct {
  float x, y, vx, vy;
} shard_t;

/* the script interpreter */
typedef struct {
  double vars[MAX_VARS];
  int16_t attack;        /* the attack being played, -1 for none */
  uint16_t line;         /* 1-based, as the scripts count */
  int32_t t;             /* ticks since the current line was due */
  uint8_t running, finish, talk, ended;
  int16_t talk_line;     /* what the opponent says (text.c), -1 for nothing */
} script_t;

typedef struct {
  /* the zone: its edges now and where they're going */
  float zl, zt, zr, zb;
  float tl, tt, tr, tb;
  float zone_speed;
  uint8_t zone_hidden, black;
  /* the heart */
  float hx, hy, hvx, hvy;
  float max_fall;
  uint8_t heart_mode, heart_dir, slammed, slam_damage;
  uint8_t heart_on, moving, jump_held, heart_free;   /* shown (and hit), moving, ..., moved by the arrows */
  /* the player */
  int16_t hp, kr;
  int16_t kr_t;
  uint16_t hits;         /* ticks hurt (for the tests) */
  /* the opponent */
  float ox, ox_speed;
  uint8_t anim, body, torso, head, sweat, pad2[3];
  int16_t anim_t;        /* ticks */
  uint8_t dodge, pad3;
  int16_t dodge_t;
  /* the screen's shake */
  int16_t shake, shake_t;
  /* things */
  uint16_t nbones, nstabs, nblasters, nplats, nmbones;
  bone_t bones[MAX_BONES];
  stab_t stabs[MAX_STABS];
  blaster_t blasters[MAX_BLASTERS];
  plat_t plats[MAX_PLATS];
  mbone_t mbones[MAX_MBONES];
  uint8_t menu_left, menu_bottom;
  float bottom_t;
  uint8_t bottom_alt, pad4[3];
  script_t sc;
  uint32_t rng;          /* the fight's random numbers (xorshift32) */
  uint32_t fx_rng;       /* the look's (shake, shards): never the fight's */
  uint32_t tick;
  /* while checking the scripts: GetHeartPos says (320, 304) and every spawn goes to a log */
  uint8_t checking, pad5[3];
} world_t;

/* floor in floats (|x| < 2^31): the drawing's and the moves' (the scripts' sums are doubles) */
static inline int bt_ifloor(float x) {
  int i = (int)x;
  return (float)i > x ? i - 1 : i;
}

/* sim.c */
double bt_sin_deg(double d);
double bt_cos_deg(double d);
double bt_atan2_deg(double y, double x);
double bt_floor(double x);
uint32_t bt_rand(uint32_t *s);
uint32_t bt_below(uint32_t *s, uint32_t n);
float bt_sinf_deg(float d);

void sim_reset(world_t *w, uint32_t seed);
void sim_start_attack(world_t *w, int attack);
void sim_stop_attack(world_t *w);
void sim_tick(world_t *w, uint32_t keys, uint32_t pressed);
void sim_zone_to(world_t *w, float l, float t, float r, float b, bool finish);
bool sim_zone_there(const world_t *w);
void sim_clear_attacks(world_t *w);
void sim_menu_bones(world_t *w, bool left, bool bottom);
void sim_heart_mode(world_t *w, int mode);
void sim_resume(world_t *w);
void sim_shake(world_t *w, int n);
/* the heart's centre point is inside a rectangle */
bool sim_heart_in(const world_t *w, float x, float y, float rw, float rh);
/* the beam's corners, for drawing and hurting: its band from `from` to `to` px in front of the blaster, `half`
 * px either side */
void blaster_beam(const blaster_t *b, float from, float to, float half, float qx[4], float qy[4]);
/* the log while checking (tests): a line per thing made */
extern void (*sim_log)(const char *line);
#endif
