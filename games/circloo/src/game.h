/* circloO: the game's state, its levels and its screens. */
#ifndef CO_GAME_H
#define CO_GAME_H
#include <stdbool.h>
#include <stdint.h>

#define NBASE 14
#define NHARD 6
#define MAX_OBJ 320      /* a level's bodies, in its script's order */
#define MAX_MC 96
#define MAX_TMC 4
#define MAX_TIMED 16
#define MAX_PJ 40

/* the save: what the game keeps (levelsunlocked, unlockedaddtimes, the levels done), and the best times */
typedef struct {
  uint8_t unlocked[2], bonus[2];       /* [0] normal, [1] hard */
  uint8_t done[2][16];                 /* level 1.. */
  uint16_t best[2][16];                /* frames (1/60 s), 0: none */
  uint8_t hard;                        /* the mode chosen on the main menu */
  uint8_t pad[3];
} Save;

typedef struct { float x, y; uint8_t io, taken; } Coll;
typedef struct { float x, y, r, dens, dist; int16_t body, obj; } Mc;
typedef struct { float x, y, r, dens, dist; int16_t life, period, alarm; } Tmc;
typedef struct { int16_t body, life, age; } Timed;
typedef struct {
  uint8_t type, fade;                  /* fade: 0..5, the joint's line appearing */
  int16_t oa, ob, joint;
  float ax, ay, bx, by, len, f, t;
} PJoint;

typedef struct {
  int level;                           /* 1.. */
  bool hard;
  const int16_t *data;                 /* the level's records (levels.h) */
  float R;                             /* the level's radius, pixels */
  int grow;                            /* frames before it grows (0: not growing) */
  int frames;                          /* time played, 1/60 s */
  int ball;
  Coll coll[8];
  int ncoll, taken;
  Mc mc[MAX_MC];
  int nmc;
  Tmc tmc[MAX_TMC];
  int ntmc;
  Timed timed[MAX_TIMED];
  PJoint pj[MAX_PJ];
  int npj;
  int16_t objbody[MAX_OBJ];            /* each body object: its body (0: the ground, -1: not made yet) */
  int nobj;
  int8_t info[8];                      /* the text shown at each ring */
  bool complete;
} Level;

enum { S_MENU, S_PLAY, S_PAUSE, S_DONE, S_CREDITS, S_HELP };

typedef struct {
  bool running;
  int state;
  uint32_t keys, prev;                 /* held now, held last tick */
  /* the menu (a row of circles) */
  int items[6], nitems, sel, from, anim;   /* anim: frames left of the slide from `from` */
  int pick[2];                         /* the level picked on the main menu, each mode */
  int zoom;                            /* the circle growing over the screen into a level */
  int msg, msg_t;                      /* a notification ("You unlocked hard mode!") */
  int last_time;                       /* the level just done: its time, and whether it is the best */
  bool new_best;
  uint16_t hue_a, hue_b, hue_c;        /* the colours (A outside and things, B inside, C ropes) */
  float camx, camy;
} Game;

extern Game g;
extern Save save;
extern Level L;

void level_load(int level, bool hard);
void level_tick(uint32_t keys);
void level_draw(void);
void colours_for(float level);
void game_init(void);
void game_tick(uint32_t keys);
void game_draw(void);
void save_read(void);
bool save_write(void);
#endif
