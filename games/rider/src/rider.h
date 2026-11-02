/* Rider on the NumWorks calculator: shared types. The numbers come from the game's own settings
 * (code.config.properties and friends, see gamesrc/rider/spec), in its content pixels and 60 frames a second. */
#ifndef RIDER_H
#define RIDER_H
#include <stdbool.h>
#include <stdint.h>
#include "phys.h"

/* ---------------------------------------------------------------- the data (src/data.h, tools/data.py) */
typedef struct {
  uint8_t kind, tpl, flags;
  uint16_t anim;
  float cx, cy, rot; /* centre, turn (degrees, clockwise) */
  float sx, sy;      /* negative: flipped */
  int16_t x0, y0, x1, y1;
  int16_t pvx, pvy;  /* the pivot, from the centre */
  int16_t delay;     /* physicsActiveDelay (ms), -1 none */
} rd_obj;
typedef struct {
  uint8_t on_touch, stop_touch;
  int8_t loops;          /* -1 for ever, 0 only once touched */
  uint16_t start_delay;  /* ms */
  float ax, ay;          /* the anchor it turns about, from the centre */
  uint16_t first;
  uint8_t nrot, nx, ny;
} rd_anim;
typedef struct {
  float delta;
  uint32_t ms;
  uint8_t ease;
  uint16_t delay;
} rd_step;
typedef struct {
  uint16_t first, n, width;
  float sx, sy, srot; /* the bike's centre and turn at the start */
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
enum { F_SAFE = 1, F_ONCOLL = 2, F_ACTIVE = 4, F_PIVOT = 8, F_REMOVE = 16 };

/* ---------------------------------------------------------------- the world */
#define MAXLIVE 288
#define MAXDYN 64
#define MAXSEG 6
typedef struct {
  uint16_t obj;  /* in rd_objs */
  uint8_t seg;   /* the track piece it came with */
  uint8_t st;    /* ST_ bits */
  uint8_t dyn;   /* its moving part (rd_dyn), or NO_DYN */
  uint8_t landed;/* landings on it (GameObject.setLanded) */
} rd_live;
enum { ST_USED = 1, ST_IN = 2, ST_GONE = 4 }; /* (ST_IN: a moving object's body is in) */
#define NO_DYN 0xFF

/* an animation of one property (rotation, x or y) as GameObject.animateObjectProperty runs it, frame by frame */
typedef struct {
  uint8_t on;          /* its enterFrame listener */
  uint8_t on_touch;    /* still waiting for a touch to start */
  uint8_t delay_done;
  uint8_t cur;         /* the step */
  int16_t count;       /* loops left (0: for ever) */
  int16_t t;           /* frames into the step */
  float start;         /* the value the step began from (NAN: not yet) */
  float end, orig;     /* where the step ends, where the cycle began */
} rd_anst;

typedef struct {
  uint16_t live;       /* its object */
  uint8_t used, flags; /* DY_ bits */
  ph_body *body;
  ph_joint *touch, *pivot;
  float tx, ty, rot;   /* the touch joint's target and the picture's turn (degrees), kept while unloaded */
  float x, y;          /* where it is now */
  float pivot_ms;      /* the pivot goes then (ms of the run), or -1 */
  float start_ms;      /* its animations start then, or -1 */
  rd_anst an[3];
} rd_dyn;
enum { DY_ANIM = 1, DY_PIVOT = 2, DY_FREE = 4, DY_ALWAYS = 8 };

typedef struct {
  int16_t track;
  int32_t x;  /* where the piece begins in the world */
  uint8_t gems; /* its gems are out (the endless run puts them in every other piece) */
} rd_seg;

/* the bike: Box2D bodies, as GameCharacter builds them */
enum { BP_MAIN, BP_WHEEL1, BP_WHEEL2, BP_TRIG1, BP_TRIG2, BP_FRONT, BP_BOTTOM, BP_TOP, BP_BACK, BP_N };

#define TRAIL 24
typedef struct {
  ph_body *part[BP_N];
  uint8_t on_ground[BP_N]; /* the display objects' onGround (wheels, triggers, main body) */
  float landing_ms[2];     /* the triggers' landingTime */
  uint8_t top_seen[2];     /* the triggers have touched a platform top (their table exists) */
  bool started;            /* the run has begun (physics runs) */
  bool accelerating, accel_fresh, held;
  int accel_in_air;        /* accelerationStartedInAir */
  float speed;             /* the accelerate timer's speed (px/s) */
  int delta;               /* its step: 14, or 30 going backwards */
  float in_air_ms;         /* inAirStartTime, or -1 */
  bool in_air_flag;
  /* stopBikeRotation */
  bool stop_rot;
  float stop_rot_ms, stop_rot_w0;
  /* setBoost */
  bool boost;
  int boost_frames, boost_first;
  float boost_rot, boost_angle;
  bool boost_angle_set;
  /* flips (Board.checkFlip) */
  bool flip_zero_set;
  float flip_zero, flip_very_start, flip_abs_start;
  int8_t flip_dir;   /* 0 none, -1 LEFT (back), 1 RIGHT (front) */
  int air_flips;
  bool front_flip;
  /* what is drawn and read */
  float x, y, vx, vy, a, w; /* the main body: centre, speed (px/s), turn (radians, clockwise), turn speed */
  float wx[2], wy[2];       /* the wheels */
  bool ground;              /* a trigger on a platform */
  float max_x, odo;         /* (tests: how far it got, how far it rode forward) */
  float air_ms;
  float trail_x[TRAIL], trail_y[TRAIL];
  int trail_n;
} rd_bikestate;

typedef struct {
  float x, y, vx, vy, a, w;
  uint8_t c, size;
} rd_bit;

/* the collision tables of Board's collision listener: who touches what, and how many times (fixtures) */
#define MAXTOUCH 64
typedef struct {
  uint8_t table, who;
  uint16_t what;
  int16_t n;
} rd_touch;
enum { T_TOP, T_BOOST, T_DEADLY, T_BODY, T_SENS };

#define MAXTIMER 16
typedef struct {
  float at;      /* ms of the run */
  uint8_t kind;
  uint16_t a;
  uint8_t b;
} rd_timer;

enum { RUN_ENDLESS, RUN_LEVEL };
enum { END_NONE, END_CRASH, END_FINISH };
#define NBITS 40
typedef struct {
  int mode, level, bike, theme;
  uint32_t seed;
  rd_seg seg[MAXSEG];
  int nseg, nchunks;
  int32_t end_x; /* the world's right end so far */
  int32_t base_k; /* the ground base's blocks in the physics world: round this one */
  rd_live live[MAXLIVE];
  uint64_t pmask[MAXLIVE]; /* a static object's polygons in the physics world */
  int nlive;     /* slots in use up to here */
  rd_dyn dyn[MAXDYN];
  rd_bikestate b;
  rd_touch touch[MAXTOUCH];
  int ntouch;
  rd_timer timer[MAXTIMER];
  int ntimer;
  float ms;             /* the run's clock */
  uint32_t frame;       /* Board's frame number */
  int top_frame;        /* the top sensor's first hard knock (frame), or -1 */
  bool death_check, finish_passed, body_touch;
  int score, pending;   /* flips counted, flips not counted yet (in the air, or landed less than 500 ms ago) */
  int landed_flips;
  float pending_ms;
  int flips, perfect, doubles, gems;
  int best_flips_jump;
  int ended;            /* END_ */
  float end_ms;
  float finish_x;
  float bottom;         /* below this is out of the world */
  float camx, camy;
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
void obj_pose(const rd_run *r, int i, float *x, float *y, float *rot); /* centre and turn (radians) now */
void obj_box(const rd_run *r, int i, float *x0, float *y0, float *x1, float *y1);

/* ---------------------------------------------------------------- draw.c */
extern uint16_t pal[256];
void draw_theme(int theme);
void draw_clear(uint8_t c);
void draw_run(const rd_run *r);
void draw_bike_at(int bike, float x, float y, float a, float zoom, int glow); /* in screen pixels */
void draw_wheels_at(float x0, float y0, float x1, float y1, float zoom, int front);
void draw_bike_shape(int bike, float x, float y, float a, float zoom, uint8_t c); /* one colour, no wheels */
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
