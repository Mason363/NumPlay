/* Mr Bullet: a level being played (game.c): its objects, characters, shots and rules.
 *
 * Coordinates are the game's units (60 a metre), x right and y down, (0, 0) at the middle of the play field;
 * angles turn clockwise on the screen, 0 pointing right. */
#ifndef MB_GAME_H
#define MB_GAME_H
#include <stdbool.h>
#include <stdint.h>
#include "data.h"

#define MAX_OBJ 96
#define MAX_CHAR 24
#define MAX_PIN 48
#define MAX_SW 8
#define MAX_SHOT 16
#define MAX_BOOM 8
#define TRAIL 10

enum { MODE_CLASSIC, MODE_GRENADES, MODE_HOSTAGES, MODE_FRIENDLY };
enum { K_POLY, K_SEG, K_POINT };
enum { R_HERO, R_ENEMY, R_HOSTAGE };
enum { ST_PLAY, ST_WON, ST_LOST };
enum { LOSE_NONE, LOSE_AMMO, LOSE_HOSTAGE, LOSE_SELF };

/* the level's flags, in the order of tools/pack.py */
enum {
  F_KI = 1 << 0, F_GKI = 1 << 1, F_HKI = 1 << 2, F_HGKI = 1 << 3, F_VI = 1 << 4, F_HS = 1 << 5, F_RH = 1 << 6,
  F_SI = 1 << 7, F_B = 1 << 8, F_F = 1 << 9, F_CO = 1 << 10, F_BE = 1 << 11, F_HV = 1 << 12, F_BO = 1 << 13,
  F_BR = 1 << 14, F_BY = 1 << 15
};
#define F_SHOOTER (F_KI | F_GKI | F_HKI | F_HGKI)

/* a part of a character's ragdoll */
enum { P_TORSO, P_HEAD, P_ARM_F, P_FORE_F, P_ARM_B, P_FORE_B, P_THIGH_F, P_SHIN_F, P_THIGH_B, P_SHIN_B, P_COUNT };

typedef struct {
  uint8_t kind, idx, gone, sw;      /* gone: broken, switched away or not yet there; sw: its switch + 1 */
  uint32_t flags;
  int16_t body;                     /* -1: none */
  float cx, cy;                     /* the body's origin */
  float bx, by, r;                  /* a ball's centre (from the origin) and radius */
  const uint8_t *outline;           /* points (x 8 units, from the origin), then holes */
  uint16_t nout;
  uint8_t nholes;
  uint8_t kin;                      /* moved along a track (pin t 5) */
  float kx0, ky0, kdx, kdy, klen, kspeed, kpos;
} Obj;

typedef struct {
  uint8_t role, alive, left, hittable, obj;
  float h;                          /* height */
  float fx, fy, tilt;               /* where it stood: feet and lean (at the start) */
  int16_t body;                     /* standing: one body */
  int16_t part[P_COUNT];            /* fallen: the ragdoll */
} Char;

typedef struct {
  uint8_t kind, life_on;            /* kind 0 bullet, 1 grenade */
  int16_t body;
  int16_t life;                     /* steps left */
  float px, py;                     /* where it was the step before */
  float tx[TRAIL], ty[TRAIL];
  uint8_t tn;
} Shot;

typedef struct {
  uint8_t t, motor;
  int8_t a, b;                      /* objects (-1: the world) */
  int16_t joint;
  float x0, y0, x1, y1;             /* a track's ends */
} Pin;

typedef struct {
  uint8_t id, colour, on, nlinks;
  uint8_t link[12];
  int16_t body;
  float x0, y0, x1, y1;
} Switch;

typedef struct {
  float x, y;
  int16_t t;                        /* frames since */
} Boom;

typedef struct {
  int level, mode, state, lose;
  int tr, ammo, shots;              /* three stars at tr shots; tr + 3 bullets */
  int nobj, nchar, npin, nsw;
  Obj obj[MAX_OBJ];
  Char ch[MAX_CHAR];
  Pin pin[MAX_PIN];
  Switch sw[MAX_SW];
  Shot shot[MAX_SHOT];
  Boom boom[MAX_BOOM];
  int hero;                         /* the shooter's character */
  bool grenades;                    /* he throws grenades */
  float aim;                        /* screen angle */
  float power;                      /* grenades: 0 to 1 */
  int frame, quiet, end_wait;       /* steps played, steps since anything moved, steps to the result */
  int enemies_left;
} Game;

extern Game g;
/* memory for points, shared by loading and drawing */
#define SCRATCH_FLOATS 1200
extern float mb_scratch[SCRATCH_FLOATS];

int game_mode_of(int level);
bool game_load(int level, uint8_t *buffer);   /* buffer: LEVEL_MAX_BYTES, kept while it is played */
void game_end(void);
void game_step(void);
bool game_can_fire(void);
void game_fire(void);
void game_aim_by(float d);                    /* raise (d > 0) or lower the aim */
void game_turn(bool left);
int game_stars(void);                         /* for the shots so far */
/* the shooter's shoulder, hand and muzzle */
void game_gun(float *sx, float *sy, float *mx, float *my);
/* the grenade's path for the preview: up to n points */
int game_arc(float *xs, float *ys, int n);
/* where a character's part is: centre, sine and cosine of its turn (false: not there) */
bool game_part(const Char *c, int part, float *x, float *y, float *s, float *cs);
void game_part_size(const Char *c, int part, float *w, float *h);
#endif
