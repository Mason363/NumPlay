/* The overworld: rooms, the camera, Frisk and the room's objects (world.h). */
#include <stdarg.h>
#include <string.h>
#include "event.h"
#include "game.h"
#include "gfx.h"
#include "res.h"
#include "text.h"
#include "world.h"
#include "events.h"

void pl_walk_step(void);   /* event.c */
void room_update(void);    /* rooms.c */

Obj objs[MAX_OBJS];
Player pl;
int g_room = -1;
static RoomInfo room;
static Door doors[16];
static int ndoors;
int cam_x, cam_y;
bool cam_locked;
uint8_t g_fade;

/* Frisk's box: his feet (from his sprite's top-left), as the game's mask */
#define PBX 3
#define PBY 20
#define PBW 14
#define PBH 9
#define SPEED 3

const RoomInfo *world_room_info(void) { return g_room >= 0 ? &room : NULL; }

/* the room of the same area whose rectangle holds (x, y), -1 none */
static int room_at(int x, int y) {
  if (x >= room.x && x < room.x + room.w && y >= room.y && y < room.y + room.h) return g_room;
  for (int r = 0; r < NUM_ROOMS; r++) {
    RoomInfo o;
    room_info(r, &o);
    if (o.area == room.area && x >= o.x && x < o.x + o.w && y >= o.y && y < o.y + o.h) return r;
  }
  return -1;
}

/* the 4-pixel cell at (x, y) walkable? (in whichever room of the area holds it) */
static bool cell_walkable(int x, int y) {
  const RoomInfo *ri = &room;
  RoomInfo o;
  if (!(x >= room.x && x < room.x + room.w && y >= room.y && y < room.y + room.h)) {
    int r = room_at(x, y);
    if (r < 0) return false;
    room_info(r, &o);
    ri = &o;
  }
  int cx = (x - ri->x) >> 2, cy = (y - ri->y) >> 2;
  if (cx < 0 || cy < 0 || cx >= ri->cw || cy >= ri->ch) return false;
  return ri->coll[cy * ((ri->cw + 7) >> 3) + (cx >> 3)] >> (cx & 7) & 1;
}

static bool boxes_meet(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh) {
  return ax < bx + bw && bx < ax + aw && ay < by + bh && by < ay + ah;
}

/* a floor object (a bloomed bridge seed) under the point */
static bool on_floor_obj(int x, int y) {
  for (int i = 0; i < MAX_OBJS; i++) {
    const Obj *o = &objs[i];
    if (o->kind != OBJ_NONE && (o->flags & OF_FLOOR) && x >= o->x + o->bx && x < o->x + o->bx + o->bw &&
        y >= o->y + o->by && y < o->y + o->by + o->bh)
      return true;
  }
  return false;
}

bool world_walkable(int x, int y, int w, int h, int ignore) {
  /* every cell the box covers (its corners and every 4 pixels between) */
  for (int yy = y;; yy += 4) {
    if (yy > y + h - 1) yy = y + h - 1;
    for (int xx = x;; xx += 4) {
      if (xx > x + w - 1) xx = x + w - 1;
      if (!cell_walkable(xx, yy) && !on_floor_obj(xx, yy)) return false;
      if (xx == x + w - 1) break;
    }
    if (yy == y + h - 1) break;
  }
  if (ignore == WALK_FLOOR_ONLY) return true;
  for (int i = 0; i < MAX_OBJS; i++) {
    const Obj *o = &objs[i];
    if (i == ignore || o->kind == OBJ_NONE || !(o->flags & OF_SOLID)) continue;
    if (boxes_meet(x, y, w, h, o->x + o->bx, o->y + o->by, o->bw, o->bh)) return false;
  }
  return true;
}

static bool player_free(int x, int y) { return world_walkable(x + PBX, y + PBY, PBW, PBH, -1); }

Obj *obj_new(int kind, int x, int y) {
  for (int i = 0; i < MAX_OBJS; i++)
    if (objs[i].kind == OBJ_NONE) {
      Obj *o = &objs[i];
      memset(o, 0, sizeof *o);
      o->kind = (uint8_t)kind, o->x = (int16_t)x, o->y = (int16_t)y, o->alpha = 255;
      return o;
    }
  return NULL;
}
Obj *obj_find(int name) {
  for (int i = 0; i < MAX_OBJS; i++)
    if (objs[i].kind != OBJ_NONE && objs[i].name == name) return &objs[i];
  return NULL;
}
Obj *obj_get(int name) {
  static Obj spare;
  Obj *o = obj_find(name);
  if (o) return o;
  memset(&spare, 0, sizeof spare);
  return &spare;
}
/* objects walking off on their own through a few points (a cutscene's last steps: the cutscene is over, Frisk
 * walks meanwhile, as in the game), gone at the last one if they leave */
#define MAX_LEAVERS 4
static struct {
  uint8_t obj, name, n, i, sp, solid;
  int8_t end;   /* -1 gone, or the way it faces */
  int16_t pts[6][2];
} leavers[MAX_LEAVERS];

void obj_clear_all(void) {
  memset(objs, 0, sizeof objs);
  memset(leavers, 0, sizeof leavers);
}

void obj_walk_off(Obj *o, int sp, int end, int n, ...) {
  if (!o || o < objs || o >= objs + MAX_OBJS || o->kind == OBJ_NONE || n < 1) return;
  obj_walk_cancel(o);
  int k = 0;
  while (k < MAX_LEAVERS && leavers[k].n) k++;
  if (k == MAX_LEAVERS) k = 0;
  va_list ap;
  va_start(ap, n);
  if (n > 6) n = 6;
  for (int i = 0; i < n; i++) {
    leavers[k].pts[i][0] = (int16_t)va_arg(ap, int);
    leavers[k].pts[i][1] = (int16_t)va_arg(ap, int);
  }
  va_end(ap);
  leavers[k].obj = (uint8_t)(o - objs), leavers[k].name = o->name, leavers[k].n = (uint8_t)n, leavers[k].i = 0;
  leavers[k].sp = (uint8_t)sp, leavers[k].end = (int8_t)end;
  /* (it walks through Frisk: solid again once it stays, and Frisk isn't in its way) */
  leavers[k].solid = (o->flags & OF_SOLID) != 0;
  o->flags &= (uint16_t)~OF_SOLID;
  o->tx = leavers[k].pts[0][0], o->ty = leavers[k].pts[0][1], o->speed = (uint8_t)sp, o->walking = 1;
}

void obj_walk_cancel(Obj *o) {
  for (int k = 0; k < MAX_LEAVERS; k++)
    if (leavers[k].n && &objs[leavers[k].obj] == o) {
      if (leavers[k].solid) o->flags |= OF_SOLID;
      leavers[k].n = 0;
    }
}

static void leavers_update(void) {
  for (int k = 0; k < MAX_LEAVERS; k++) {
    if (!leavers[k].n) continue;
    Obj *o = &objs[leavers[k].obj];
    if (o->kind == OBJ_NONE || o->name != leavers[k].name) {
      leavers[k].n = 0;
      continue;
    }
    if (o->walking) continue;
    if (leavers[k].i + 1 < leavers[k].n) {
      leavers[k].i++;
      o->tx = leavers[k].pts[leavers[k].i][0], o->ty = leavers[k].pts[leavers[k].i][1];
      o->speed = leavers[k].sp, o->walking = 1;
    } else if (leavers[k].end < 0) {
      o->kind = OBJ_NONE, leavers[k].n = 0;
    } else {
      o->dir = (uint8_t)leavers[k].end;
      if (leavers[k].solid && boxes_meet(pl.x + PBX, pl.y + PBY, PBW, PBH, o->x + o->bx, o->y + o->by, o->bw, o->bh)) continue;
      if (leavers[k].solid) o->flags |= OF_SOLID;
      leavers[k].n = 0;
    }
  }
}

void world_camera(void) {
  if (cam_locked) return;
  /* (inside the game's room: a room smaller than the screen in the middle) */
  int cx = pl.x + 10 - SCREEN_W / 2, cy = pl.y + 15 - SCREEN_H / 2;
  if (room.vw <= SCREEN_W) cx = room.vx + (room.vw - SCREEN_W) / 2;
  else if (cx < room.vx) cx = room.vx;
  else if (cx > room.vx + room.vw - SCREEN_W) cx = room.vx + room.vw - SCREEN_W;
  if (room.vh <= SCREEN_H) cy = room.vy + (room.vh - SCREEN_H) / 2;
  else if (cy < room.vy) cy = room.vy;
  else if (cy > room.vy + room.vh - SCREEN_H) cy = room.vy + room.vh - SCREEN_H;
  cam_x = cx, cam_y = cy;
}

void world_enter(int r, int x, int y, int dir) {
  int from = g_room;
  g_room = r;
  room_info(r, &room);
  pl.x = (int16_t)x, pl.y = (int16_t)y, pl.dir = (uint8_t)dir;
  pl.frame = 0, pl.anim = 0;
  obj_clear_all();
  ndoors = room_doors(r, doors, 16);
  cam_locked = false;
  room_setup(r, from);
  /* a door's spot a little off the floor: the nearest that isn't (the floor the room's things make counts: a
   * bridge of flowers; what stands about doesn't) */
  if (!world_walkable(pl.x + PBX, pl.y + PBY, PBW, PBH, WALK_FLOOR_ONLY))
    for (int d = 1; d <= 24; d++) {
      static const int8_t dx[4] = {0, 0, -1, 1}, dy[4] = {1, -1, 0, 0};
      int k;
      for (k = 0; k < 4 && !world_walkable(pl.x + dx[k] * d + PBX, pl.y + dy[k] * d + PBY, PBW, PBH, WALK_FLOOR_ONLY); k++) {}
      if (k < 4) {
        pl.x = (int16_t)(pl.x + dx[k] * d), pl.y = (int16_t)(pl.y + dy[k] * d);
        break;
      }
    }
  world_camera();
}

/* ---------------------------------------------------------------- fades */
static struct {
  int room, x, y, dir;
  int phase, t;    /* 1: going black, 2: coming back */
  int hold;        /* frames it stays black (Frisk sleeps) */
  bool walked;     /* Frisk walked out: what was happening in the room stops */
} fade;
#define FADE_FRAMES 4

void world_fade_to(int r, int x, int y, int dir) {
  fade.room = r, fade.x = x, fade.y = y, fade.dir = dir;
  fade.phase = 1, fade.t = 0, fade.walked = false, fade.hold = 0;
}
void world_fade_hold(int r, int x, int y, int dir, int hold) {
  world_fade_to(r, x, y, dir);
  fade.hold = hold;
}
bool world_fading(void) { return fade.phase != 0; }

static void fade_step(void) {
  if (fade.phase == 1) {
    if (fade.t >= FADE_FRAMES) {   /* in the new room, still black */
      g_fade = 255;
      if (fade.hold > 0) fade.hold--;
      else fade.phase = 2, fade.t = 0;
      return;
    }
    if (++fade.t >= FADE_FRAMES) {
      if (fade.walked) event_cancel();
      world_enter(fade.room, fade.x, fade.y, fade.dir);
      if (fade.hold > 0) {
        g_fade = 255;
        return;
      }
      fade.phase = 2, fade.t = 0;
    }
    g_fade = (uint8_t)(255 * fade.t / FADE_FRAMES);
  } else if (fade.phase == 2) {
    if (++fade.t >= FADE_FRAMES) fade.phase = 0;
    g_fade = (uint8_t)(255 - 255 * fade.t / FADE_FRAMES);
  } else {
    g_fade = 0;
  }
}

/* ---------------------------------------------------------------- Frisk */
static const uint8_t frames_of[4] = {4, 4, 2, 2};

static void face_from_keys(uint32_t keys, uint32_t pressed) {
  static const uint32_t bit[4] = {K_DOWN, K_UP, K_LEFT, K_RIGHT};
  /* a direction just pressed turns him; letting go of the one he faces turns him to one still held */
  for (int d = 0; d < 4; d++)
    if (pressed & bit[d]) pl.dir = (uint8_t)d;
  if (!(keys & bit[pl.dir]))
    for (int d = 0; d < 4; d++)
      if (keys & bit[d]) {
        pl.dir = (uint8_t)d;
        break;
      }
}

/* walking into a rock pushes it, a pixel a frame, if there's floor for it (a rock with an event talks back
 * instead) */
static uint32_t pushed_at;
static bool push_rock(int sx, int sy) {
  int fx = pl.x + PBX + sx, fy = pl.y + PBY + sy;
  for (int i = 0; i < MAX_OBJS; i++) {
    Obj *o = &objs[i];
    if (o->kind != OBJ_ROCK || !(o->flags & OF_SOLID)) continue;
    if (!boxes_meet(fx, fy, PBW, PBH, o->x + o->bx, o->y + o->by, o->bw, o->bh)) continue;
    if (o->event) {
      if (!event_running()) event_start(o->event, o);
      return false;
    }
    if (pushed_at == g_frame || o->walking) return false;
    if (!world_walkable(o->x + o->bx + sx, o->y + o->by + sy, o->bw, o->bh, i) &&
        !room_rock_enters(o, o->x + sx, o->y + sy))
      return false;
    pushed_at = g_frame;
    o->x = (int16_t)(o->x + sx), o->y = (int16_t)(o->y + sy);
    room_rock_moved(o);
    return player_free(pl.x + sx, pl.y + sy);
  }
  return false;
}

/* one pixel along an axis, sliding past a corner (diagonal walls are steps of cells) */
static bool step(int sx, int sy, bool slide) {
  if (player_free(pl.x + sx, pl.y + sy) || push_rock(sx, sy)) {
    pl.x = (int16_t)(pl.x + sx), pl.y = (int16_t)(pl.y + sy);
    return true;
  }
  if (!slide) return false;
  for (int k = 1; k <= 2; k++) {
    bool a = sx ? player_free(pl.x + sx, pl.y - k) : player_free(pl.x - k, pl.y + sy);
    bool b = sx ? player_free(pl.x + sx, pl.y + k) : player_free(pl.x + k, pl.y + sy);
    if (a != b) {
      int o = a ? -1 : 1;
      if (sx) pl.y = (int16_t)(pl.y + o), pl.x = (int16_t)(pl.x + sx);
      else pl.x = (int16_t)(pl.x + o), pl.y = (int16_t)(pl.y + sy);
      return true;
    }
    if (a && b) break;
  }
  return false;
}

static int8_t slide_dx, slide_dy;
bool pl_sliding(void) { return slide_dx || slide_dy; }

static void player_move(uint32_t keys, uint32_t pressed) {
  int dx = ((keys & K_RIGHT) != 0) - ((keys & K_LEFT) != 0);
  int dy = ((keys & K_DOWN) != 0) - ((keys & K_UP) != 0);
  face_from_keys(keys, pressed);
  /* on ice he slides the way he went on until he's off it, or something stops him */
  if (room_ice(pl.x + PBX + PBW / 2, pl.y + PBY + PBH / 2)) {
    if (!slide_dx && !slide_dy && (dx || dy)) {
      static const int8_t ddx[4] = {0, 0, -1, 1}, ddy[4] = {1, -1, 0, 0};
      slide_dx = ddx[pl.dir], slide_dy = ddy[pl.dir];
    }
    if (slide_dx || slide_dy) {
      bool moved = false;
      for (int i = 0; i < 4; i++) moved |= step(slide_dx, slide_dy, false);
      if (!moved) slide_dx = slide_dy = 0;
      pl.anim = 0, pl.frame = 0;
      pl.moving = moved;
      return;
    }
  } else {
    slide_dx = slide_dy = 0;
  }
  bool moved = false;
  int sp = room_speed(pl.x + PBX + PBW / 2, pl.y + PBY + PBH / 2);
  if (g_walk_fast) sp *= 2;   /* (Settings: fast walking) */
  for (int i = 0; i < sp; i++) {
    if (dx) moved |= step(dx, 0, !dy);
    if (dy) moved |= step(0, dy, !dx);
  }
  if (dx || dy) {
    pl.anim = (uint8_t)(pl.anim + 1 + g_walk_fast);
    pl.frame = (uint8_t)((1 + pl.anim / 6) % frames_of[pl.dir]);
    if (moved) pl.steps++;
  } else {
    pl.anim = 0, pl.frame = 0;
  }
  pl.moving = moved;
}

/* the box in front of Frisk, where Z looks */
static void front_box(int *x, int *y, int *w, int *h) {
  int fx = pl.x + PBX, fy = pl.y + PBY, fw = PBW, fh = PBH;
  switch (pl.dir) {
    case DIR_DOWN: fy += fh, fh = 12; break;
    case DIR_UP: fy -= 14, fh = 14; break;
    case DIR_LEFT: fx -= 12, fw = 12; break;
    default: fx += fw, fw = 12; break;
  }
  *x = fx, *y = fy, *w = fw, *h = fh;
}

/* a 4-pixel row of floor across (x .. x + w) at y? */
static bool row_floor(int x, int y, int w) {
  for (int xx = x;; xx += 4) {
    if (xx > x + w - 1) xx = x + w - 1;
    if (cell_walkable(xx, y) || on_floor_obj(xx, y)) return true;
    if (xx == x + w - 1) return false;
  }
}

/* what Z reaches: the box in front, and facing a wall, up the wall to what's drawn on it (a sign, a glyph, a bell:
 * the game reads them from the wall's foot), never past it to the floor beyond */
void world_reach(int *x, int *y, int *w, int *h) {
  front_box(x, y, w, h);
  if (pl.dir != DIR_UP) return;
  int top = *y;
  for (int k = 0; k < 10 && !row_floor(*x, top - 4, *w); k++) top -= 4;
  *h += *y - top, *y = top;
}

static void interact(void) {
  int x, y, w, h;
  world_reach(&x, &y, &w, &h);
  for (int i = 0; i < MAX_OBJS; i++) {
    Obj *o = &objs[i];
    if (o->kind == OBJ_NONE || !(o->flags & OF_TALK)) continue;
    if (boxes_meet(x, y, w, h, o->x + o->bx, o->y + o->by, o->bw, o->bh)) {
      event_start(o->event, o);
      return;
    }
  }
}

static void touches(void) {
  int x = pl.x + PBX, y = pl.y + PBY;
  for (int i = 0; i < MAX_OBJS; i++) {
    Obj *o = &objs[i];
    if (o->kind == OBJ_NONE || !(o->flags & OF_TOUCH)) continue;
    if (boxes_meet(x, y, PBW, PBH, o->x + o->bx, o->y + o->by, o->bw, o->bh)) {
      event_start(o->event, o);
      return;
    }
  }
}

/* ---------------------------------------------------------------- objects walking */
static void objs_update(void) {
  leavers_update();
  for (int i = 0; i < MAX_OBJS; i++) {
    Obj *o = &objs[i];
    if (o->kind == OBJ_NONE) continue;
    if (o->walking) {
      int dx = o->tx - o->x, dy = o->ty - o->y;
      int sp = o->speed ? o->speed : 2;
      if (dx) o->dir = dx < 0 ? DIR_LEFT : DIR_RIGHT;
      if (abs_i(dy) > abs_i(dx)) o->dir = dy < 0 ? DIR_UP : DIR_DOWN;
      o->x = (int16_t)(o->x + clamp_i(dx, -sp, sp));
      o->y = (int16_t)(o->y + clamp_i(dy, -sp, sp));
      if (o->x == o->tx && o->y == o->ty) o->walking = 0;
      if (o->flags & OF_WALKANIM) {
        o->anim_t++;
        o->frame = (uint8_t)(1 + o->anim_t / (o->anim_speed ? o->anim_speed : 6));
      }
      if (!o->walking && (o->flags & OF_WALKANIM)) o->frame = 0, o->anim_t = 0;
    } else if (o->flags & OF_ANIM) {
      if (++o->anim_t >= (o->anim_speed ? o->anim_speed : 6)) o->anim_t = 0, o->frame++;
    }
  }
}

void world_update(uint32_t keys, uint32_t pressed) {
  fade_step();
  if (fade.phase == 1) return;
  objs_update();
  bool busy = event_blocking() || text_active() || fade.phase;
  if (!busy && !pl.frozen) {
    player_move(keys, pressed);
    /* out of the room's rectangle: into the room that holds his feet */
    int fx = pl.x + PBX + PBW / 2, fy = pl.y + PBY + PBH / 2;
    int r = room_at(fx, fy);
    if (r >= 0 && r != g_room) {
      world_fade_to(r, pl.x, pl.y, pl.dir);
      fade.walked = true;
      return;
    }
    /* a door */
    for (int i = 0; i < ndoors; i++)
      if (boxes_meet(pl.x + PBX, pl.y + PBY, PBW, PBH, doors[i].x, doors[i].y, doors[i].w, doors[i].h)) {
        /* (a side door between rooms with a gap keeps Frisk's height: DOOR_KEEP) */
        world_fade_to(doors[i].room, doors[i].tx == DOOR_KEEP ? pl.x : doors[i].tx,
                      doors[i].ty == DOOR_KEEP ? pl.y : doors[i].ty, doors[i].dir);
        fade.walked = true;
        return;
      }
    room_update();
    if (!event_running()) {
      if (pressed & K_Z) interact();
      else touches();
      if (!event_running() && (pressed & K_C)) game_open_menu();
      if (!event_running()) encounter_step();
    }
  } else {
    pl_walk_step();
  }
  world_camera();
}

/* ---------------------------------------------------------------- drawing */
typedef struct {
  int16_t depth;
  int8_t who;     /* object index, -1 Frisk */
} DrawRef;

static uint16_t obj_sprite(const Obj *o) { return o->spr[o->spr[o->dir] ? o->dir : 0]; }

/* ---------------------------------------------------------------- the room's things */
Obj *obj_npc(int name, const uint16_t spr[4], int x, int y, int dir, int event) {
  Obj *o = obj_new(OBJ_NPC, x, y);
  if (!o) return NULL;
  for (int i = 0; i < 4; i++) o->spr[i] = spr[i];
  Frame f;
  frame_get(spr[0], 0, &f);
  o->name = (uint8_t)name, o->dir = (uint8_t)dir, o->event = (uint16_t)event;
  /* its box: the bottom of its sprite, a little narrower (from the sprite's origin: its top-left or its feet) */
  o->bx = (int16_t)(f.w / 5 - f.ox), o->bw = (int16_t)(f.w - 2 * (f.w / 5)), o->bh = (int16_t)(f.h / 4 < 8 ? 8 : f.h / 4);
  o->by = (int16_t)(f.h - o->bh - f.oy);
  o->flags = OF_SOLID | (event ? OF_TALK : 0) | OF_WALKANIM;
  return o;
}
Obj *obj_toriel(int x, int y, int dir) {
  static const uint16_t s[4] = {SPR_TORIEL_D, SPR_TORIEL_U, SPR_TORIEL_L, SPR_TORIEL_R};
  Obj *o = obj_npc(ACTOR_TORIEL, s, x, y, dir, 0);
  if (o) obj_set_box(o, 6, 44, 20, 12), o->anim_speed = 6;
  return o;
}
Obj *obj_sign(int x, int y, int w, int h, const char *text) {
  Obj *o = obj_new(OBJ_SIGN, x, y);
  if (!o) return NULL;
  o->bw = (int16_t)w, o->bh = (int16_t)h, o->flags = OF_TALK, o->event = EV_SIGN, o->text = text;
  return o;
}
Obj *obj_save(int x, int y, const char *text) {
  Obj *o = obj_new(OBJ_SAVE, x, y);
  if (!o) return NULL;
  o->spr[0] = SPR_SAVEPOINT, o->flags = OF_TALK | OF_SOLID | OF_ANIM, o->anim_speed = 6;
  o->bx = 2, o->by = 4, o->bw = 16, o->bh = 14;
  o->event = EV_SAVE, o->text = text;
  return o;
}
Obj *obj_deco(uint16_t spr, int x, int y, bool solid) {
  Obj *o = obj_new(OBJ_DECO, x, y);
  if (!o) return NULL;
  Frame f;
  frame_get(spr, 0, &f);
  o->spr[0] = spr;
  o->bx = 0, o->bw = (int16_t)f.w, o->bh = (int16_t)(f.h < 10 ? f.h : 10), o->by = (int16_t)(f.h - o->bh);
  if (solid) o->flags |= OF_SOLID;
  return o;
}
Obj *obj_trigger(int x, int y, int w, int h, int event, int name) {
  Obj *o = obj_new(OBJ_TRIGGER, x, y);
  if (!o) return NULL;
  o->bw = (int16_t)w, o->bh = (int16_t)h, o->flags = OF_TOUCH, o->event = (uint16_t)event, o->name = (uint8_t)name;
  return o;
}
Obj *obj_rock(int x, int y, int name, int event) {
  Obj *o = obj_new(OBJ_ROCK, x, y);
  if (!o) return NULL;
  o->spr[0] = SPR_O_ROCK, o->name = (uint8_t)name, o->event = (uint16_t)event;
  o->flags = OF_SOLID | (event ? OF_TALK : 0);
  obj_set_box(o, 1, 8, 18, 12);
  return o;
}
void obj_spikes_set(Obj *o, bool raised) {
  if (!o) return;
  o->frame = raised ? 0 : 1;
  if (raised) o->flags |= OF_SOLID;
  else o->flags &= (uint16_t)~OF_SOLID;
}
Obj *obj_spikes(int x, int y, int cols, int rows, bool raised, int name) {
  Obj *o = obj_new(OBJ_SPIKES, x, y);
  if (!o) return NULL;
  o->spr[0] = SPR_O_SPIKETILE, o->arg = (int16_t)cols, o->arg2 = (int16_t)rows, o->name = (uint8_t)name;
  o->bw = (int16_t)(cols * 20), o->bh = (int16_t)(rows * 20);
  o->frame = raised ? 0 : 1;
  o->flags = (raised ? OF_SOLID : 0) | OF_BACK;
  return o;
}
void obj_set_box(Obj *o, int bx, int by, int bw, int bh) {
  o->bx = (int16_t)bx, o->by = (int16_t)by, o->bw = (int16_t)bw, o->bh = (int16_t)bh;
}
bool near_frisk(const Obj *o, int d) {
  int ox = o->x + o->bx + o->bw / 2, oy = o->y + o->by + o->bh / 2;
  int px = pl.x + PBX + PBW / 2, py = pl.y + PBY + PBH / 2;
  return abs_i(ox - px) <= d && abs_i(oy - py) <= d;
}

bool cam_pan(int x, int y, int sp) {
  cam_locked = true;
  cam_x += clamp_i(x - cam_x, -sp, sp), cam_y += clamp_i(y - cam_y, -sp, sp);
  return cam_x == x && cam_y == y;
}
void cam_free(void) { cam_locked = false; }

void room_draw_floor(void);   /* rooms.c */
void room_draw_top(void);

int g_shake;   /* frames the view shakes */

void world_draw(void) {
  int keep_x = cam_x, keep_y = cam_y;
  if (g_shake > 0) cam_x += (rnd_n(3) - 1) * 2, cam_y += (rnd_n(3) - 1) * 2, g_shake--;
  gfx_tiles(cam_x, cam_y);
  room_draw_floor();
  /* the room's objects and Frisk, the lower ones in front */
  DrawRef order[MAX_OBJS + 1];
  int n = 0;
  for (int i = 0; i < MAX_OBJS; i++) {
    const Obj *o = &objs[i];
    if (o->kind == OBJ_NONE || (o->flags & OF_HIDDEN) || !o->spr[0]) continue;
    int d = o->y + o->by + o->bh + o->depth_bias;
    if (o->flags & OF_BACK) d = -30000;
    if (o->flags & OF_FRONT) d = 30000;
    order[n++] = (DrawRef){(int16_t)d, (int8_t)i};
  }
  if (!pl.hidden) order[n++] = (DrawRef){(int16_t)(pl.y + PBY + PBH), -1};
  for (int i = 1; i < n; i++)
    for (int j = i; j > 0 && order[j - 1].depth > order[j].depth; j--) {
      DrawRef t = order[j];
      order[j] = order[j - 1], order[j - 1] = t;
    }
  static const uint16_t frisk[4] = {SPR_FRISK_D, SPR_FRISK_U, SPR_FRISK_L, SPR_FRISK_R};
  if (g_exclaim) gfx_sprite(SPR_O_EXC, 0, pl.x - cam_x, pl.y - 12 - cam_y, 0, 255);
  for (int i = 0; i < n; i++) {
    if (order[i].who < 0) {
      gfx_sprite(frisk[pl.dir], pl.frame, pl.x - cam_x, pl.y - cam_y, 0, 255);
    } else {
      const Obj *o = &objs[order[i].who];
      if (o->kind == OBJ_SPIKES) {
        for (int r = 0; r < o->arg2; r++)
          for (int k = 0; k < o->arg; k++) gfx_sprite(o->spr[0], o->frame, o->x + k * 20 - cam_x, o->y + r * 20 - cam_y, 0, 255);
        continue;
      }
      if (o->flags & OF_DARK) gfx_sprite_color(obj_sprite(o), o->frame, o->x - cam_x, o->y - cam_y, 0, C_BLACK, o->alpha);
      else gfx_sprite(obj_sprite(o), o->frame, o->x - cam_x, o->y - cam_y, (o->flags & OF_FLIP) ? GFX_FLIPX : 0, o->alpha);
    }
  }
  room_draw_top();
  cam_x = keep_x, cam_y = keep_y;
}
