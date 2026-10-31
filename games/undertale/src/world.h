/* Undertale: the overworld. Rooms, the camera, Frisk, the room's objects.
 *
 * Positions are the area map's pixels (tools/rooms.py): the rooms of an area
 * keep their places on it, so walking out of a room's rectangle walks into the
 * next room, after a fade, as in the game. */
#ifndef UT_WORLD_H
#define UT_WORLD_H
#include <stdbool.h>
#include <stdint.h>
#include "res.h"

enum { DIR_DOWN, DIR_UP, DIR_LEFT, DIR_RIGHT };

/* an object of the room: a character, a SAVE Point, a sign, a trigger... */
enum {
  OBJ_NONE, OBJ_NPC, OBJ_SAVE, OBJ_SIGN, OBJ_TRIGGER, OBJ_DECO, OBJ_ROCK, OBJ_SPIKES, OBJ_SWITCH, OBJ_ITEM,
  OBJ_MARK
};
enum {
  OF_SOLID = 1,       /* Frisk can't walk through its box */
  OF_TALK = 2,        /* pressing Z facing it runs its event */
  OF_TOUCH = 4,       /* walking into its box runs its event */
  OF_HIDDEN = 8,      /* not drawn */
  OF_ANIM = 16,       /* its frames cycle (anim_speed) even standing */
  OF_WALKANIM = 32,   /* its frames cycle while it walks */
  OF_FRONT = 64,      /* drawn over everything else */
  OF_BACK = 128,      /* drawn under everything else */
  OF_DARK = 256,      /* a silhouette */
  OF_FLIP = 512,      /* drawn mirrored */
  OF_FLOOR = 1024     /* its box is floor to walk on (a bloomed bridge seed over water) */
};
#define MAX_OBJS 48
typedef struct {
  uint8_t kind, dir;
  uint16_t flags;
  int16_t x, y;              /* its sprite's origin (the frame's top-left for the game's sprites) */
  int16_t bx, by, bw, bh;    /* its box, from (x, y) */
  uint16_t spr[4];           /* its sprite for each direction (spr[0] if it has one) */
  uint8_t frame, anim_speed, anim_t, depth_bias;
  uint16_t event;            /* EV_* run when talked to or touched */
  int16_t arg, arg2;         /* the kind's own (a sign's text, a flag...) */
  /* walking somewhere: (tx, ty) at speed pixels a frame */
  int16_t tx, ty;
  uint8_t speed, walking;
  uint8_t alpha;
  uint8_t name;              /* an ACTOR_* (scripts find it by it) */
  int16_t fx, fy;            /* fractions (1/16 pixel) for slow walks */
  const char *text;          /* what a sign or a plain thing says */
} Obj;
extern Obj objs[MAX_OBJS];

typedef struct {
  int16_t x, y;              /* Frisk's sprite's top-left */
  uint8_t dir, frame, anim, moving;
  bool frozen;               /* a cutscene has control */
  bool hidden;
  uint16_t steps;            /* towards an encounter */
} Player;
extern Player pl;

extern int g_room;            /* ROOM_* */
extern int cam_x, cam_y;      /* the screen's top-left on the map */
extern bool cam_locked;       /* a cutscene moves the camera */

const RoomInfo *world_room_info(void);
void world_enter(int room, int x, int y, int dir);   /* (x, y) Frisk's top-left; no fade */
void world_update(uint32_t keys, uint32_t pressed);
void world_draw(void);
#define WALK_FLOOR_ONLY (-2)   /* (world_walkable's ignore: the floor alone, nothing solid standing on it) */
bool world_walkable(int x, int y, int w, int h, int ignore);   /* a box on the map, objects too (not `ignore`) */
void world_reach(int *x, int *y, int *w, int *h);              /* the box Z reaches, in front of Frisk */
Obj *world_talk_target(void);                                  /* what Z there would talk to, NULL nothing */
void world_camera(void);       /* follows Frisk, inside the room */
Obj *obj_new(int kind, int x, int y);
Obj *obj_find(int name);
Obj *obj_get(int name);   /* obj_find, or a spare object when there is none (events can't crash on it) */
void obj_clear_all(void);
/* the object walks through n points (x, y, ...) at sp pixels a frame, on its own: whoever started it goes on (Frisk
 * walks); at the last point it's gone (end -1) or faces that way (DIR_*) */
void obj_walk_off(Obj *o, int sp, int end, int n, ...);
void obj_walk_cancel(Obj *o);   /* (a cutscene walks it itself) */
/* the room's things, made by rooms.c */
Obj *obj_npc(int name, const uint16_t spr[4], int x, int y, int dir, int event);
Obj *obj_toriel(int x, int y, int dir);
Obj *obj_sign(int x, int y, int w, int h, const char *text);
Obj *obj_save(int x, int y, const char *text);
Obj *obj_deco(uint16_t spr, int x, int y, bool solid);
Obj *obj_trigger(int x, int y, int w, int h, int event, int name);
Obj *obj_spikes(int x, int y, int cols, int rows, bool raised, int name);
void obj_spikes_set(Obj *o, bool raised);
Obj *obj_rock(int x, int y, int name, int event);   /* pushed by walking into it; with an event, it talks */
void obj_set_box(Obj *o, int bx, int by, int bw, int bh);
bool near_frisk(const Obj *o, int d);      /* within d pixels (their feet) */
bool room_ice(int x, int y);               /* rooms.c: ice there (Frisk slides) */
int room_speed(int x, int y);              /* rooms.c: Frisk's steps a frame there (3; webs slow him) */
/* rooms.c: a pushed rock that the floor stops (it may go into water, a bridge seed), then where it went */
bool room_rock_enters(Obj *o, int x, int y);
void room_rock_moved(Obj *o);
bool pl_sliding(void);
/* the room fades out, then in: room changes in the middle */
void world_fade_to(int room, int x, int y, int dir);
void world_fade_hold(int room, int x, int y, int dir, int hold);   /* black for `hold` frames in the middle */
bool world_fading(void);
extern uint8_t g_fade;         /* 0 clear, 255 black: drawn over the room */
extern int g_shake;            /* frames the view shakes */
#endif
