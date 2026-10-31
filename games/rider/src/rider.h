/* Rider on the NumWorks calculator: shared types. The numbers come from the game's own settings
 * (code.config.properties and friends, see gamesrc/rider/spec), in its content pixels and 60 frames a second. */
#ifndef RIDER_H
#define RIDER_H
#include <stdbool.h>
#include <stdint.h>

/* ---------------------------------------------------------------- the data (src/data.h, tools/data.py) */
typedef struct {
  uint8_t kind, tpl, flags;
  uint16_t anim;
  int16_t cx, cy;
  uint16_t rot; /* 1/100 degree, clockwise */
  float sx, sy; /* negative: flipped */
  int16_t x0, y0, x1, y1;
} rd_obj;
typedef struct {
  uint8_t on_touch;
  int8_t loops;
  uint16_t start_delay, fall_delay;
  int16_t px, py;
  uint16_t first;
  uint8_t nrot, nx, ny;
  uint16_t child;
} rd_anim;
typedef struct {
  float delta;
  uint32_t ms;
  uint8_t ease;
  uint16_t delay;
} rd_step;
typedef struct {
  uint16_t first, n, width;
  int16_t sx, sy;
  uint16_t finish;
} rd_track;
typedef struct {
  uint8_t cls, rule;
  uint16_t val;
  uint8_t front;
  uint16_t o0;
  uint8_t on;
  uint16_t h0;
  uint8_t hn;
  const char *name;
} rd_bike;

enum { K_GROUND, K_PLATFORM, K_ENEMY, K_BOOST, K_GEM, K_FINISH };
enum { F_SAFE = 1, F_FALLS = 2, F_DROP = 4 };

/* ---------------------------------------------------------------- the world */
#define MAXLIVE 640
#define MAXSEG 8
typedef struct {
  uint16_t obj;   /* in rd_objs */
  uint8_t seg;    /* the track piece it came with */
  uint8_t st;     /* ST_ bits */
  int16_t parent; /* the platform a ground moves with, or -1 */
  float t;        /* its animation's clock (ms), or the time since it was touched */
  float ft;       /* falling: time since it let go (ms) */
  float cx, cy, rot, dx, dy, dr; /* where it is now: centre, turn (radians), and how far it moved this frame */
  float x0, y0, x1, y1;          /* bounding box now */
} rd_live;
enum { ST_RUN = 1, ST_TOUCHED = 2, ST_FALL = 4, ST_GONE = 8 };
typedef struct {
  int16_t track;
  int32_t x;   /* where the piece begins in the world */
} rd_seg;

#define TRAIL 24
typedef struct {
  float x, y, vx, vy, a, w; /* centre of the body, its speed (px/s), turn (radians, clockwise), turn speed */
  float speed;              /* the button's speed (px/s) */
  bool held;                /* the button, this frame */
  bool ground;              /* a wheel trigger on a platform */
  bool fixed;               /* let go in the air: the turn eases to nothing, contacts can't turn it */
  float ease_t, ease_w0;    /* that easing: its clock (ms), the turn speed it started from */
  float air_ms;             /* in the air for */
  float boost_ms;           /* a speed pad's push left (ms) */
  float boost_dir;
  uint8_t wheel_down[2];
  float land_t[2];          /* when each wheel came down (ms of the run) */
  float flip_start, rot_abs;/* flips: degrees, as the game counts them */
  int air_flips;
  bool front_flip;
  float trail_x[TRAIL], trail_y[TRAIL];
  int trail_n;
} rd_bikestate;

typedef struct {
  float x, y, vx, vy, a, w;
  uint8_t c, size;
} rd_bit;

enum { RUN_ENDLESS, RUN_LEVEL };
enum { END_NONE, END_CRASH, END_FINISH };
#define NBITS 40
typedef struct {
  int mode, level, bike, theme;
  uint32_t seed;
  rd_seg seg[MAXSEG];
  int nseg;
  int32_t end_x; /* the world's right end so far */
  rd_live live[MAXLIVE];
  int nlive;
  rd_bikestate b;
  float ms;             /* the run's clock */
  int score, pending;   /* flips counted, flips not counted yet (in the air, or landed less than 500 ms ago) */
  int landed_flips;     /* of those, the ones landed */
  float pending_ms;     /* till the landed ones count */
  int flips, perfect, doubles, gems;
  int best_flips_jump;
  int ended;            /* END_ */
  float end_ms;         /* since it ended */
  float finish_x;
  float bottom;         /* below this is out of the world */
  float camx, camy;
  /* texts over the run */
  const char *text, *text2;
  float text_ms;
  rd_bit bits[NBITS];
  int nbits;
  float shake;
} rd_run;

/* ---------------------------------------------------------------- game.c */
void run_start(rd_run *r, int mode, int level, int bike, int theme, uint32_t seed);
void run_step(rd_run *r, bool held);  /* one frame, 1/60 s */
float run_level_time(const rd_run *r); /* ms, flips taken off */
uint32_t rd_rand(uint32_t *s);
void run_debug(rd_run *r); /* (tests) */

/* ---------------------------------------------------------------- draw.c */
extern uint8_t fb[240][320];
extern uint16_t pal[256];
void draw_theme(int theme);
void draw_run(const rd_run *r);
void draw_bike_at(int bike, float x, float y, float a, float zoom, int glow); /* in screen pixels */
void draw_flush(void (*hud)(void));
void fb_fill(int x, int y, int w, int h, uint8_t c);
void draw_gem_icon(float x, float y, float h);
void draw_star(float x, float y, float rad, uint8_t c);
void draw_line(float x0, float y0, float x1, float y1, uint8_t c);
void draw_disc(float x, float y, float rad, uint8_t c);
int obj_poly(const rd_run *r, int i, int k, float *xy);
enum {
  C_BG, C_GROUND, C_HALO, C_GLOW, C_CORE, C_EN_HALO, C_EN, C_BODY, C_BODY_DARK, C_WHITE, C_WHEEL_HUB, C_GEM_HALO,
  C_GEM, C_BOOST, C_BOOST_DIM, C_FINISH, C_SPARK, C_SHADE, C_TRAIL /* .. C_TRAIL + 7 */
};
#endif
