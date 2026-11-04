/* Moto X3M: a level in play: its bodies in the physics, its objects and what they do (motox3m4.js: RaceBuilder,
 * ShaperObject, ToggleObject, MoverPatherObject, MotorObject, PivotObject, TntObject, RemovedBodyObjects, ...). */
#ifndef MX_LEVEL_H
#define MX_LEVEL_H
#include <stdbool.h>
#include "nape.h"
#include "res.h"

/* the game's callback types (tools/levels.py CB), and a flag for what counts as ground */
enum {
  CB_GROUND = 1, CB_SPIKES = 2, CB_SAW = 4, CB_FINISH = 8, CB_SAFE = 16, CB_TOGGLE = 32, CB_TOGGLE_OFF = 64,
  CB_GLASS = 128, CB_WATER = 256, CB_BOOST = 512, CB_GEYSER = 1024, CB_WHEEL = 2048, CB_BODY = 4096, CB_DRIVER = 8192,
  CB_PART = 16384
};
/* filters (tools/levels.py FILTERS) */
enum { FL_DEFAULT, FL_NOTHING, FL_SENSOR, FL_EARTH, FL_WHEEL, FL_PART, FL_GROUND, FL_PLAYER };

typedef struct {
  int8_t dir, prev_dir, pdir;
  uint8_t active;
  int16_t pid;
  float speed, ttp, ttp_total, vx, vy, start_time, ang_speed, side_target, min_speed, accel, total, base_rot;
} Mover;

/* what only joints, movers and glass need */
typedef struct {
  Con *con[2];              /* pivot, motor */
  Mover *mv;
  float gx, gy, grot;       /* a picture's place (motor, pivot), where glass was hit */
} ObjExt;

typedef struct Obj {
  const ObjRec *r;
  Body *body, *body2;
  ObjExt *x;
  int16_t sbody;
  uint8_t active, hidden, sens, pad;
  float t1, t2, alpha;      /* timers (each kind its own use) */
} Obj;

typedef struct {
  const LevelHdr *h;
  int n;                    /* 0-based */
  Obj *objs;
  int nobj;
  Body *dbodies;            /* the level's dynamic and kinematic bodies */
  int ndbodies;
  const float *params, *aux;
  const DrawRec *draws;
  const int16_t *dpts;
  float wave;               /* water's surface (it stays still in the game: its objects are never active) */
} Level;

extern Level lv;

#ifdef HOST
#define ARENA_SIZE 100000   /* (pointers are twice as big on a computer) */
#else
#define ARENA_SIZE 70000
#endif
bool level_load(int n);
/* hard restart: every object as at the start */
void level_restart(void);
/* back to the last safe point: every object that can is reset (MaxGameCore.restartFromSafePoint) */
void level_review(void);
void level_update(float dt);
/* MaxGameCore.activate */
void level_activate(int id, bool on);
/* after each physics step: the space's begin and end events */
void level_events(void);
float level_param(const Obj *o, int k);
static inline const float *obj_params(const Obj *o) { return lv.params + o->r->params; }
/* the camera paths: the lowest path point under x not above y, or NAN */
float level_cam_y(float x, float y, int *path);

/* memory for a level's bodies, shapes and constraints, and the bike's (bump allocated, reset each load) */
void *arena_alloc(int size);
extern int arena_used, arena_peak;
#endif
