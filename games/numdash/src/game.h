#ifndef NUMDASH_GAME_H
#define NUMDASH_GAME_H
#include <stdbool.h>
#include <stdint.h>
#include "level.h"
#include <math.h>

#define ND_HZ 240
#define ND_DT (1.0f / ND_HZ)
enum { MODE_CUBE = 0, MODE_SHIP = 1, MODE_BALL = 2, MODE_UFO = 3, MODE_WAVE = 4, MODE_ROBOT = 5, MODE_SPIDER = 6, MODE_SWING = 7 };
enum { BUF_NONE = 0, BUF_READY = 1, BUF_END = 2 };
/* Visible world: 480 x 360 GD units (20 px blocks); ground sits 90 units
 * above the bottom edge when the camera is at rest, as in GD. */
#define VIEW_W 480.0f
#define VIEW_H 360.0f
#define GROUND_OFFSET 90.0f
#define PLAYER_SCREEN_X 150.0f

typedef struct {
  float x, y, vy, gravity, rot, target_rot, time_since_ground, ceil_inv;
  float ground_y, ceiling_y, snap_diff;
  float spin;       /* ball: rolling speed, 1 on a surface, -0.7 in the air */
  float snap_ox, snap_oy;   /* the block the cube last landed on (x snapping) */
  int32_t snap_frame, frame, coyote;
  uint8_t mode, speed, buffer;
  bool upside, on_ground, on_ceiling, vel_override, snap_rot, left_ground, inverse_rot, jumped, rot_dir_neg, has_snap, mini, ufo_buf;
  bool second;          /* the dual mode's second body */
  bool old_on_ground, orb_touching;
  uint8_t hover;        /* robot: hover ticks left while the button stays held */
  bool dash;            /* dash ring: moving in a straight line while held */
  float dash_slope;     /* its dy / dx */
  /* GD 2.2 gameplay modifiers, armed for the tick a box is touched and the
     next: head collision (1859), flip on head hit (2866), no auto jump
     (1813), wave slide (1755) */
  uint8_t arm_head, arm_flip, arm_noauto, arm_slide;
  float band_h;         /* the flying band's height at zoom 1 (GD scales it by 1 / zoom) */
  /* GD's velocity-limit exemption: a slope launch or a red ring lets a ship,
     UFO or swing pass its terminal speed until it slows back inside */
  bool boost;
  bool no_jump;         /* a spider ring just landed it: no jump before the next tick */
  /* slopes (as gd3ds): the slope ridden, the one just left (coyote) and the
     ones touched last tick; orientation -1 = none */
  float sl[4], sl_t, co[4], co_t, pot[4][2];
  int8_t sl_o, co_o, pot_o[4];
  uint8_t co_ticks, npot;
  bool sl_down, co_down, pend_clear, has_new_vy;
  float t_elapsed, new_vy, delta_y;
} Player;

/* A colour channel: an RGB fade (t, dur in steps) plus, for 2.0+ levels,
 * opacity, blending and an optional copy of another channel with HSV. */
typedef struct {
  uint8_t cur[3], from[3], to[3], op, op_from, op_to, flags;
  uint16_t copy, t, dur;
  int16_t h, s, v;
} Channel;
#define MAX_CHANNELS 64       /* the channels colour triggers change (2.0 levels: LevelExt.ndyn) */
#define MAX_GROUPS 512
/* Group state: offset from moves, opacity, on / off (fades are actions). */
/* moved: the tick (low 16 bits, 0 = never) the group last moved vertically */
typedef struct { float dx, dy; uint8_t alpha, on; uint16_t moved; } GroupState;
/* Actions keep the group set of the trigger that started them (`src`), so
 * a stop trigger can find them. Moves: t counts ticks from -1 (GD starts a
 * move the tick after the crossing); dur is in ticks with its fraction, as
 * GD's seconds give it. flags: 1 lock x, 2 lock y. */
typedef struct {
  uint16_t group, src;
  uint8_t easing, flags;
  int16_t t;
  float tx, ty, done_x, done_y, rate, dur, xmod, ymod;
} MoveAction;
typedef struct { uint16_t target, copy, src; uint8_t flags, rgb[3]; int16_t h, s, v; uint16_t t, tin, hold, tout; } PulseAction;
typedef struct { uint16_t group, src; uint8_t from, to; uint16_t t, dur; } FadeAction;
/* Rotations: the action, and each turned group's angle about its pivot. */
typedef struct { uint16_t group, src; uint8_t easing, flags; int16_t t; float deg, done, rate, dur, px, py; } RotAction;
typedef struct { uint16_t group; float angle, px, py; } RotGroup;
typedef struct { uint16_t group, src; uint16_t ticks; uint8_t nremap; uint16_t remap[4][2]; } SpawnAction;
typedef struct { uint16_t group, follow, src; int16_t t; float xmod, ymod, dur, lx, ly; } FollowAction;
/* Listeners started by position triggers: count, collision and tap. */
typedef struct { uint8_t kind, a, b, on; int16_t count; uint16_t target, src; } Listener;
#define MAX_MOVES 32
#define MAX_PULSES 24
#define MAX_FADES 16
#define MAX_ROTS 12
#define MAX_ROTG 24
#define MAX_SPAWNS 24
#define MAX_FOLLOWS 6
#define MAX_LISTEN 12
#define MAX_ITEM_IDS 16
#define TRIG_CHANNELS 16

/* What a 2.0 level's triggers depend on besides the player's x: kept for
   the run so a practice checkpoint can rebuild the trigger state. `val`:
   the forward coordinate after a gameplay rotation. */
typedef struct { uint16_t tick; uint8_t kind, arg; float val; } LogEntry;
enum { LOG_SPEED = 1, LOG_TOUCH, LOG_FRAME, LOG_TAP, LOG_TOGGLE, LOG_PICKUP };
#define MAX_LOG 160

enum { FX_JUMP, FX_LAND, FX_PAD, FX_ORB, FX_PORTAL, FX_COIN, FX_DEATH, FX_GRAVITY, FX_ORB_TOUCH, FX_WALL, FX_SIZE, FX_SPEED };
typedef struct { uint8_t kind, arg; int16_t obj; float x, y; } FxEvent;

typedef struct {
  Player p;
  Player p2;            /* dual mode */
  const Level *L;
  float cam_x, cam_y, ground_x, bg_x, ground_gfx, cam_intended_y, wall_y, end_t, end_x0, end_y0, cam_wall_t;
  float cam_wall_y0, bg_wall_x0, ground_wall_x0, shake_t, shake_amp;
  Channel ch[MAX_CHANNELS];
  uint32_t tick;
  uint16_t next_event, jumps, death_obj;
  /* 2.0+ levels: trigger channels (a cursor into each one's list and the
     forward coordinate of its next trigger), groups and actions */
  uint32_t ch_pos[TRIG_CHANNELS];
  float ch_fwd[TRIG_CHANNELS];
  uint8_t active_ch, frame;     /* gameplay rotation: 0 right, 1 down, 2 left, 3 up */
  GroupState gr[MAX_GROUPS];
  MoveAction mv[MAX_MOVES];
  PulseAction pu[MAX_PULSES];
  FadeAction fa[MAX_FADES];
  RotAction ro[MAX_ROTS];
  RotGroup rg[MAX_ROTG];
  SpawnAction sp[MAX_SPAWNS];
  FollowAction fo[MAX_FOLLOWS];
  Listener li[MAX_LISTEN];
  int16_t items[MAX_ITEM_IDS];
  uint8_t nmv, npu, nfa, nro, nrg, nsp, nfo, nli, touch_bits[24];
  float time_mod;               /* time warp */
  float dist;                   /* forward distance travelled (progress in turned levels) */
  /* camera triggers: static target, offset, zoom */
  float cam_tx, cam_ty, cam_off_x, cam_off_y, zoom;
  /* the zoom eases (GD's band height follows it: 1 / zoom) */
  float zoom_from, zoom_to, zoom_rate;
  uint16_t zoom_t, zoom_dur;
  uint8_t zoom_ease;
  uint8_t cam_static, cam_axis;
  bool end_trig;                /* an end trigger fired */
  bool rebuilding;              /* practice: triggers being replayed */
  LogEntry log[MAX_LOG];
  uint16_t nlog;
  uint8_t fade_effect, coins, attempt_camera, touch_done[8];
  bool trail, dead, complete, ending, hold_prev, hold_prev2, pressed_prev, dual, dual_swap;
  float dual_y;
  bool menu_camera;   /* fixed camera, ground scrolling on its own (main menu) */
#if ND_EXTRA_LEVELS
  /* Meltdown's and SubZero's: the mirror portals' flip (0 normal .. 1
     mirrored, eased) and the controls an options trigger turned off */
  float mirror_t;
  bool mirror, no_input;
#endif
  float used_below;   /* objects left of this x count as used (practice) */
  uint8_t used[MAX_LEVEL_OBJS / 8];
  FxEvent fx[24];
  uint8_t fx_count;
} Game;

/* Practice checkpoint: everything needed to resume from a point. */
typedef struct {
  Player p;
  float cam_x, cam_y, ground_x, bg_x, ground_gfx, cam_intended_y;
  Channel ch[CH_COUNT];
  uint32_t tick;
  uint16_t next_event, jumps;
  uint8_t fade_effect, touch_done[8];
  bool trail;
  /* 2.0 levels: the run's log up to here, and the dual's other body */
  uint16_t nlog;
  bool dual;
  uint8_t p2_mode, p2_flags;
  float dual_y, p2_y, p2_vy, p2_rot, dist;
#if ND_EXTRA_LEVELS
  bool mirror;
#endif
} Checkpoint;

void game_start(Game *g, const Level *L, bool first_attempt);
void game_step(Game *g, bool hold);
float game_progress(const Game *g);
bool game_used(const Game *g, const RObj *o, unsigned gi);
void game_save_checkpoint(const Game *g, Checkpoint *c);
void game_load_checkpoint(Game *g, const Checkpoint *c);
unsigned game_coin_index(const Level *L, unsigned obj);
/* trig.c: 2.0+ triggers, groups and channels */
void trig_start(Game *g);
void trig_cross(Game *g);
void trig_step(Game *g, float player_dx);
void trig_touch(Game *g, unsigned idx);
void trig_log(Game *g, unsigned kind, unsigned arg, float val);
/* the player pressed: tap listeners spawn their groups */
void trig_tap(Game *g);
/* toggle ring: switch group on (bit 14) or off */
void trig_toggle_ring(Game *g, unsigned arg);
/* where group `grp`'s anchor object is now (false if it has none) */
bool trig_anchor(const Game *g, unsigned grp, float *x, float *y);
/* rotation of a group set: extra angle (degrees) and pivot; false if none */
bool gset_moved(const Game *g, unsigned set);
bool gset_rot(const Game *g, unsigned set, float *angle, float *px, float *py);
/* gameplay rotation (game.c): travel and gravity as GD's directions
   (1 up, 2 down, 3 left, 4 right), flags 2 = scale the new vy by vmod */
void game_rotate(Game *g, unsigned travel, unsigned gravity, unsigned flags, float vmod);
bool gset_on(const Game *g, unsigned set);
void gset_state(const Game *g, unsigned set, float *dx, float *dy, unsigned *alpha, bool *on);
float ease(int kind, float rate, float u);
float pulse_level(const PulseAction *p);
void hsv_shift(const uint8_t in[3], int h, int s64, int v64, unsigned flags, uint8_t out[3]);
/* Where an object is now (its groups' moves applied); false when its
   groups are switched off. */
/* Gameplay rotation: the player moves in a frame turned clockwise by a
   quarter turn per step; (u, v) = travel and up. */
static inline void to_world(const Game *g, float u, float v, float *X, float *Y) {
  switch (g->frame & 3) {
    case 1: *X = v; *Y = -u; break;
    case 2: *X = -u; *Y = -v; break;
    case 3: *X = -v; *Y = u; break;
    default: *X = u; *Y = v; break;
  }
}
static inline void to_local(const Game *g, float X, float Y, float *u, float *v) {
  switch (g->frame & 3) {
    case 1: *u = -Y; *v = X; break;
    case 2: *u = -X; *v = -Y; break;
    case 3: *u = Y; *v = -X; break;
    default: *u = X; *v = Y; break;
  }
}
/* Where an object is now (world), moved and turned by its groups; `angle`
   (may be NULL): the extra turn in degrees. False if its groups are off. */
static inline bool obj_where_rot(const Game *g, const RObj *o, float *x, float *y, float *angle) {
  *x = obj_x(o);
  *y = obj_y(o);
  if (angle) *angle = 0;
  if (!g->L->ext || !o->paint) return true;
  const LStyle *st = obj_style(g->L, o);
  if (!st->groups) return true;
  float dx, dy;
  unsigned a;
  bool on;
  gset_state(g, st->groups, &dx, &dy, &a, &on);
  *x += dx;
  *y += dy;
  float ang, px, py;
  if (gset_rot(g, st->groups, &ang, &px, &py) && ang != 0) {
    float r = -ang * 0.017453293f, c = cosf(r), s = sinf(r), rx = *x - px, ry = *y - py;
    *x = px + rx * c - ry * s;
    *y = py + rx * s + ry * c;
    if (angle) *angle = ang;
  }
  return on;
}
static inline bool obj_where(const Game *g, const RObj *o, float *x, float *y) { return obj_where_rot(g, o, x, y, NULL); }
/* World -> screen helpers (floating point pixels). */
static inline float wx_to_sx(const Game *g, float x) { return (x - g->cam_x) * (2.0f / 3.0f); }
static inline float wy_to_sy(const Game *g, float y) { return 240.0f - (GROUND_OFFSET + y - g->cam_y) * (2.0f / 3.0f); }
#endif
