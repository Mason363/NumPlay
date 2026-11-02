/* Deltarune: the overworld. Rooms, the camera, Kris and the party following him, the room's things.
 *
 * Positions are the original's room units: the Light World's are the screen's pixels, the Dark World's
 * twice them (its rooms are drawn halved). Things move as there, in 1/16 of a unit. */
#ifndef DR_WORLD_H
#define DR_WORLD_H
#include <stdbool.h>
#include <stdint.h>
#include "res.h"

enum { DIR_DOWN, DIR_RIGHT, DIR_UP, DIR_LEFT };   /* global.facing's order */

/* a thing of the room: a character, a sign, a SAVE point, a trigger... */
enum {
  OF_SOLID = 1,       /* Kris can't walk through its box */
  OF_TALK = 2,        /* Z facing it runs its event */
  OF_TOUCH = 4,       /* walking into its box runs its event */
  OF_HIDDEN = 8,      /* not drawn */
  OF_FRONT = 16,      /* drawn over everything else */
  OF_BACK = 32,       /* drawn under everything else */
  OF_FLIPX = 64,      /* drawn mirrored */
  OF_WALKANIM = 128,  /* its frames follow its walking (as the party's) */
  OF_GONE = 256       /* free */
};
#define MAX_OBJS 64
#define FX 16
typedef struct Obj {
  uint16_t flags;
  uint16_t obj;              /* the original's object (obj_* in data.h), 0xFFFF none */
  int16_t inst;              /* its place among the room's instances, -1 */
  int32_t x, y;              /* its sprite's origin, 1/16 unit */
  int32_t hs, vs;            /* speed (1/16 unit a frame) */
  uint16_t spr;              /* SPR (data.h), 0xFFFF none */
  int32_t img, ispd;         /* image_index and image_speed, 1/16 frame */
  int16_t bx, by, bw, bh;    /* its box (solid, talk, touch), units from (x, y) */
  uint16_t event;            /* EV_* when talked to or touched */
  int16_t arg;               /* the event's own */
  uint8_t alpha;
  int8_t depth_bias;         /* drawn as if this many units lower */
  uint16_t sprs[4];          /* when walking: its sprite each way (DIR_*), 0 none */
  uint8_t dir;
  int32_t tx, ty, wsp;       /* walking to (tx, ty) at wsp a frame, 1/16 unit */
  bool walking;
  uint8_t name;              /* an ACT_* (scripts find it by it) */
  int16_t scale;             /* drawn at this scale (1/256), 0: as its room's */
} Obj;
extern Obj objs[MAX_OBJS];

typedef struct {
  int32_t x, y;              /* Kris's top-left (the sprite's), units */
  uint8_t dir;
  bool frozen;               /* a cutscene has control */
  bool hidden;
  uint16_t spr;              /* a sprite a cutscene shows instead (0 none) */
  int img;
  int walktimer;             /* 1/2 */
  int walkbuffer;            /* 1/4 */
  int runtimer;
  bool run;
  uint16_t steps;
} Player;
extern Player pl;

extern int g_room;
extern int cam_x, cam_y;      /* the view's top-left, units */
extern bool cam_locked;       /* a cutscene moves the camera */
extern uint8_t g_fade;        /* 0 clear .. 255 g_fade_col, over the room */
extern uint16_t g_fade_col;   /* (black, 0, unless a cutscene says) */
extern int g_shake;

const RoomInfo *world_room_info(void);
bool world_dark(void);                  /* the room is the Dark World's (halved) */
int world_scale(void);                  /* 2 in the Dark World, 1 in the Light */
bool world_kris_low(void);              /* Kris low on the screen: the box goes on top */
void world_enter(int room, int x, int y, int dir);   /* Kris's top-left at (x, y); no fade */
void world_enter_marker(int room, int entrance);    /* at the room's obj_marker for entrance (1 A, 2 B...) */
void world_update(void);
void world_draw(void);
void world_draw_scene(void);            /* the room and its things, without the box */
bool world_place_free(int x, int y, int ignore);   /* Kris's box with its top-left at (x, y): no solid */
void world_camera(void);
void world_fade_to(int room, int entrance);
bool world_fading(void);
void world_fade_in(void);
bool world_marker(int obj, int *x, int *y);        /* where the room's instance of obj is */
bool world_inst(int obj, int k, Inst *out);        /* the room's k-th instance of obj */

/* things */
Obj *obj_new(uint16_t spr, int x, int y);          /* units */
Obj *obj_find(int name);
Obj *obj_find_inst(int inst);  /* by its place among the room's instances */
Obj *obj_get(int name);    /* obj_find, or a spare one (events can't crash on it) */
void obj_free(Obj *o);
void obj_clear_all(void);
void obj_set_box(Obj *o, int bx, int by, int bw, int bh);
void obj_walk_to(Obj *o, int x, int y, int sp);     /* units, sp units a frame */
int obj_px(const Obj *o);
int obj_py(const Obj *o);
void obj_place(Obj *o, int x, int y);

/* the party following Kris (obj_caterpillarchara) */
void follow_reset(void);       /* all at Kris */
void follow_show(bool on);
bool follow_shown(void);
Obj *follower(int c);          /* CH_SUSIE or CH_RALSEI's, NULL none */

/* Kris walked by a cutscene */
void pl_walk_to(int x, int y, int sp);
bool pl_walking(void);
void pl_face(int dir);
#endif
