/* Flappy Dunk: the game (game.c), its pictures (draw.c), its screens and saves (main.c).
 * World units and seconds as in the original (spec: gamesrc/flappydunk/spec). */
#ifndef FD_H
#define FD_H
#include <stdbool.h>
#include <stdint.h>
#include "data.h"

#define FD_DT 0.01f        /* Unity's fixed timestep in the game (100 steps a second) */
#define FD_R 0.6f          /* the ball */
#define FD_TOP 9.5f        /* the floor's and the ceiling's edges */
#define FD_TOP_NARROW 4.0f /* in "narrow" challenges */
#define FD_SPAWN_X0 -6.0f  /* the spawner's start; each hoop 9 after the last in endless */
#define FD_SPAWN_GAP 9.0f
#define FD_BALL_X0 -3.5f
#define FD_MAXH 48 /* hoops alive at once (a challenge's are all there from the start: 42 at most) */
#define FD_NPART 40

enum { H_STATIC, H_VERTICAL, H_LATERAL, H_WTF };
enum { ST_READY, ST_PLAY, ST_DEAD, ST_WON };
/* why a run ended */
enum { END_NONE, END_GROUND, END_MISS, END_BELOW, END_RULE, END_WIN };

typedef struct {
  float x, y;         /* the root (the spawner's position) */
  float angle, c, s;  /* the tilt, degrees counter-clockwise, and its cosine and sine */
  float w;            /* the width (the Net's x scale) */
  float bound, speed, pos; /* moving hoops: half the track, signed speed, place along the track */
  float hx, hy;       /* the hoop's centre now (root + its movement) */
  float fail_x;       /* the fail trigger's x, from the root */
  float t;            /* the mad hoop's clock; after it is passed, the fade */
  uint8_t type, passed, inside, burn;
} hoop_t;

typedef struct {
  float x, y, vx, vy, vang; /* (life, kind in vx/vy for some) */
  float life, max;
  uint8_t kind, colour;
} part_t;
enum { PT_STAR, PT_SMOKE, PT_FIRE, PT_RING, PT_BIGRING };

typedef struct {
  /* the ball */
  float x, y, vx, vy, ang, spin; /* spin: angular speed, radians a second (positive: counter-clockwise) */
  float wing;                    /* the wings' angle, degrees, 150 after a flap easing back to 0 */
  float cam;                     /* the camera's x (the ball's + 3.5 while it flies) */
  float cam_v;                   /* after a crash, the camera's speed easing to 0 */
  /* the run */
  int8_t challenge;  /* -1: endless */
  uint8_t mirror_mode, state, end, fever; /* fever: 0 none, 1 smoke, 2 fire */
  uint32_t score, mult, swishes, streak, best_streak, notswish, notswish_streak, best_notswish, hoops;
  int last_touched;            /* the hoop whose rim the ball touched since the last pass, or -1 */
  int bounces, best_bounces;   /* rim knocks on the same hoop */
  float since_hoop, rot_since, min_y; /* time, turning and the lowest point since the last hoop */
  float gravity, flap, scroll; /* a challenge's */
  float top;
  float finish_x;              /* a challenge's finish line */
  uint8_t flap_centered, gravity_centered, pixel, hflip, vflip;
  float time;                  /* seconds since the run started */
  /* the hoops */
  hoop_t h[FD_MAXH];
  int nh, next;               /* hoops in h, the next one to pass */
  int spawned;                /* endless: hoops made so far (the parameters' "score") */
  float spawn_x, spawn_y;
  uint32_t rng;
  /* effects */
  part_t p[FD_NPART];
  float pop_t; int pop_n; float pop_x, pop_y; /* the last swish's words */
} fd_game_t;

extern fd_game_t G;
extern bool fd_quiet; /* the run's events not told (a test's look ahead) */

void game_new(int challenge, bool mirror, uint32_t seed);
void game_flap(void);
void game_step(void);             /* one 0.01 s step */
float param_sample(int p, int n); /* the endless mode's parameter after n hoops */
float curve_eval(int p, float t);
uint32_t game_rand(void);
float game_frand(void);           /* 0..1 */

/* what the run tells the rest (main.c): a hoop passed with these points (1 basket, 2+ swish) */
void on_pass(int points);
void on_touch(void);  /* a rim knocked */
void on_end(void);    /* the run is over (G.end says why) */
#endif
