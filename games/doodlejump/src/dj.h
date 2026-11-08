/* Doodle Jump: what the game's files share.
 *
 * The page is the original's, 320 x 480 points, y going up from where the climb starts; the screen shows it at
 * half size in its middle. The rules run 60 times a second, as the original's, in its points and frames (the
 * numbers and where they come from: gamesrc/doodlejump/spec/). */
#ifndef DJ_H
#define DJ_H
#include <stdbool.h>
#include <stdint.h>
#include "data.h"

/* ---------------------------------------------------------------- the numbers */
#define PAGE_W 320.0f /* the page, 320 x 480 */
#define VIEW 480.0f
#define GRAV 0.35f    /* gravity, points a frame a frame */
#define JUMP 11.0f    /* a jump: 173 points up */
#define SPRING 20.0f
#define TRAMP 26.0f
#define PROP 8.0f     /* the propeller hat lifts at 8 points a frame, the jetpack at 16, for 150 frames */
#define JET 16.0f
#define FLY 150
#define ACCEL 0.6f    /* steering */
#define VMAX 7.0f
#define FEET 20.0f    /* half the doodler's feet */
#define PW 28.0f      /* half a platform (57 x 15) */
#define BODY 50.0f    /* how tall he is, to monsters */
#define SHOT_V 15.0f  /* a bullet, points a frame */

enum { P_GREEN, P_BLUE, P_BROWN, P_WHITE, P_BOMB };
enum { I_NONE, I_SPRING, I_TRAMP, I_PROP, I_JET };
enum { M_NONE, M_FLY, M_BAT, M_EYE, M_BLOB, M_CROC, M_HAIRY, M_BUG, M_RED, M_PURPLE, M_HOLE, M_UFO, M_KINDS };
enum { D_ALIVE, D_MONSTER, D_HOLE, D_UFO, D_FELL }; /* how a climb ends (the original's "how you ended") */
#define NP 32 /* platforms */
#define NM 4  /* monsters */
#define NS 3  /* bullets */

typedef struct {
  float x, y, t;             /* its middle, its top; frames since it broke, went or was lit (a bomb) */
  int8_t dir;                /* a blue one's way */
  uint8_t type, item, state; /* state: 0 there, 1 broken / gone / blown up, 2 its spring or trampoline used */
} plat_t;

typedef struct {
  float x, y, t;      /* its middle; frames it has lived */
  uint8_t kind, state; /* state: 0 alive, 1 falling away */
  uint8_t pad[2];
} mon_t;

/* a climb, all of it (kept in the save when you leave) */
typedef struct {
  uint32_t rng, score, frame;
  float x, y, vx, vy;   /* the doodler: the middle of his feet */
  float cam, top;       /* the page's bottom; where the next platform goes */
  int32_t fly;          /* frames of flying left */
  uint8_t item, face;   /* what he flies with; 1 facing right */
  uint8_t dead, flip;   /* how it ended (D_*); frames of a trampoline's flip left */
  int8_t side;          /* after a monster, the half of the page the next platform goes in */
  uint8_t drop, falling; /* the hat or jetpack falling away; 1 once he is off the page, falling past it */
  uint8_t pad;
  uint32_t land, shot;  /* the frame he last landed, last shot */
  uint32_t dead_t;      /* frames since it ended */
  float hx, hy;         /* the black hole or UFO that took him */
  float drop_x, drop_y, drop_vy;
  float shot_x[NS], shot_y[NS];
  uint8_t shot_on[NS], pad2;
  uint32_t jumps, mon_shot, mon_stomped, ufo_shot, ufo_stomped, jets, hats; /* for the stats */
  plat_t plat[NP];
  mon_t mon[NM];
} world_t;

extern world_t W;

/* game.c */
void game_new(uint32_t seed);
void game_step(int steer);        /* a 60th of a second; steer -1, 0, 1 */
bool game_shoot(void);            /* false if he can't now */
bool game_over(void);             /* the climb has ended and he is off the page */
void mon_box(int kind, float *hw, float *hh);
float mon_x(const mon_t *m);      /* where it is now (the fliers sway) */
float blue_speed(void);
uint32_t game_rand(void);

/* draw.c */
void draw_page(void);             /* the page, into the view (gfx.h) */
void draw_doodler_at(int x, int y, bool right, bool bent);

#endif
