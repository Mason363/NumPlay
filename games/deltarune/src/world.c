/* The overworld (world.h): the original's obj_mainchara, obj_caterpillarchara, its doors and markers. */
#include <string.h>
#include "event.h"
#include "game.h"
#include "gfx.h"
#include "rooms.h"
#include "text.h"
#include "world.h"

Obj objs[MAX_OBJS];
Player pl;
int g_room = -1;
int cam_x, cam_y;
bool cam_locked;
uint8_t g_fade;
uint16_t g_fade_col;
int g_shake;
static RoomInfo ri;

const RoomInfo *world_room_info(void) { return g_room >= 0 ? &ri : NULL; }
bool world_dark(void) { return g_room >= 0 && (ri.flags & ROOM_DARK); }
int world_scale(void) { return world_dark() ? 2 : 1; }
static int view_w(void) { return 320 * world_scale(); }
static int view_h(void) { return 240 * world_scale(); }

/* Kris's sprites and box (spr_krisd's mask: x 0-18, y 25-38; drawn twice its size in the Dark World) */
static uint16_t kris_spr(int dir) {
  static const uint16_t lw[4] = {spr_krisd, spr_krisr, spr_krisu, spr_krisl};
  static const uint16_t dw[4] = {spr_krisd_dark, spr_krisr_dark, spr_krisu_dark, spr_krisl_dark};
  return world_dark() ? dw[dir & 3] : lw[dir & 3];
}
static void kris_box(int x, int y, int *x0, int *y0, int *x1, int *y1) {
  int s = world_scale();
  *x0 = x, *y0 = y + 25 * s, *x1 = x + 19 * s, *y1 = y + 39 * s;
}
bool world_kris_low(void) { return pl.y > cam_y + (world_dark() ? 250 : 130); }

/* ---------------------------------------------------------------- solids */
static bool box_hits_solid(int x0, int y0, int x1, int y1, const uint8_t *s) {
  int kind = s[0];
  int sx0 = rds16(s + 2), sy0 = rds16(s + 4), sx1 = rds16(s + 6), sy1 = rds16(s + 8);
  if (x1 <= sx0 || x0 >= sx1 || y1 <= sy0 || y0 >= sy1) return false;
  if (kind == 0) return true;
  int w = sx1 - sx0, h = sy1 - sy0;
  if (kind == 5) {
    /* an ellipse: the box's point nearest its middle */
    int cx = (sx0 + sx1) / 2, cy = (sy0 + sy1) / 2;
    int px = clamp_i(cx, x0, x1 - 1), py = clamp_i(cy, y0, y1 - 1);
    int64_t dx = (int64_t)(px - cx) * 2 * h, dy = (int64_t)(py - cy) * 2 * w;
    return dx * dx + dy * dy <= (int64_t)w * h * w * h;
  }
  /* a right triangle: the box's corner deepest into it */
  int px, py;
  switch (kind) {
    case 1: /* square corner bottom-left: solid below the line from top-left to bottom-right */
      px = max_i(x0, sx0), py = min_i(y1 - 1, sy1 - 1);
      return (int64_t)(py - sy0) * w >= (int64_t)(px - sx0) * h;
    case 2: /* bottom-right: below the line from top-right to bottom-left */
      px = min_i(x1 - 1, sx1 - 1), py = min_i(y1 - 1, sy1 - 1);
      return (int64_t)(py - sy0) * w >= (int64_t)(sx1 - 1 - px) * h;
    case 3: /* top-left: above the line from bottom-left to top-right */
      px = max_i(x0, sx0), py = max_i(y0, sy0);
      return (int64_t)(sy1 - 1 - py) * w >= (int64_t)(px - sx0) * h;
    default: /* top-right */
      px = min_i(x1 - 1, sx1 - 1), py = max_i(y0, sy0);
      return (int64_t)(sy1 - 1 - py) * w >= (int64_t)(sx1 - 1 - px) * h;
  }
}

static bool box_free(int x0, int y0, int x1, int y1, int ignore) {
  for (int i = 0; i < ri.nsolid; i++)
    if (box_hits_solid(x0, y0, x1, y1, ri.solids + 10 * i)) return false;
  for (int i = 0; i < MAX_OBJS; i++) {
    const Obj *o = &objs[i];
    if ((o->flags & (OF_SOLID | OF_GONE)) != OF_SOLID || i == ignore) continue;
    int ox = o->x / FX + o->bx, oy = o->y / FX + o->by;
    if (x1 > ox && x0 < ox + o->bw && y1 > oy && y0 < oy + o->bh) return false;
  }
  return true;
}

bool world_place_free(int x, int y, int ignore) {
  int x0, y0, x1, y1;
  kris_box(x, y, &x0, &y0, &x1, &y1);
  return box_free(x0, y0, x1, y1, ignore);
}

/* ---------------------------------------------------------------- things */
Obj *obj_new(uint16_t spr, int x, int y) {
  for (int i = 0; i < MAX_OBJS; i++)
    if (objs[i].flags & OF_GONE) {
      Obj *o = &objs[i];
      memset(o, 0, sizeof *o);
      o->obj = 0xFFFF, o->inst = -1;
      o->spr = spr, o->x = x * FX, o->y = y * FX;
      o->alpha = 255;
      return o;
    }
  static Obj spare;
  memset(&spare, 0, sizeof spare);
  spare.flags = OF_GONE;
  return &spare;
}
void obj_free(Obj *o) { o->flags = OF_GONE; }
void obj_clear_all(void) {
  for (int i = 0; i < MAX_OBJS; i++) objs[i].flags = OF_GONE;
}
Obj *obj_find(int name) {
  for (int i = 0; i < MAX_OBJS; i++)
    if (!(objs[i].flags & OF_GONE) && objs[i].name == name) return &objs[i];
  return NULL;
}
Obj *obj_find_inst(int inst) {
  for (int i = 0; i < MAX_OBJS; i++)
    if (!(objs[i].flags & OF_GONE) && objs[i].inst == inst) return &objs[i];
  return NULL;
}

Obj *obj_get(int name) {
  Obj *o = obj_find(name);
  if (o) return o;
  static Obj spare;
  memset(&spare, 0, sizeof spare);
  spare.flags = OF_GONE;
  return &spare;
}
void obj_set_box(Obj *o, int bx, int by, int bw, int bh) {
  o->bx = (int16_t)bx, o->by = (int16_t)by, o->bw = (int16_t)bw, o->bh = (int16_t)bh;
}
void obj_walk_to(Obj *o, int x, int y, int sp) {
  o->tx = x * FX, o->ty = y * FX, o->wsp = sp * FX, o->walking = true;
}
int obj_px(const Obj *o) { return o->x / FX; }
int obj_py(const Obj *o) { return o->y / FX; }
void obj_place(Obj *o, int x, int y) { o->x = x * FX, o->y = y * FX; }

static void obj_step(Obj *o) {
  if (o->walking) {
    int dx = o->tx - o->x, dy = o->ty - o->y;
    if (o->sprs[0]) {
      if (abs_i(dx) > abs_i(dy)) o->dir = dx < 0 ? DIR_LEFT : DIR_RIGHT;
      else if (dy) o->dir = dy < 0 ? DIR_UP : DIR_DOWN;
      o->spr = o->sprs[o->dir];
    }
    o->x += clamp_i(dx, -o->wsp, o->wsp);
    o->y += clamp_i(dy, -o->wsp, o->wsp);
    o->img += FX / 4;
    if (o->x == o->tx && o->y == o->ty) o->walking = false, o->img = 0;
  } else {
    o->x += o->hs, o->y += o->vs;
    o->img += o->ispd;
  }
}

/* ---------------------------------------------------------------- the party following (obj_caterpillarchara) */
#define HIST 26
static int32_t hx[HIST], hy[HIST];
static uint8_t hdir[HIST];
static bool follow_on = true;
typedef struct {
  int c;
  int target;            /* how far behind Kris (history steps) */
  int32_t x, y;
  int walktimer, walkbuffer;
  uint8_t dir;
} Follower;
static Follower fl[2];
static int nfl;

static void follower_sprites(int c, uint16_t s[4]) {
  if (c == CH_SUSIE) {
    s[0] = plot() >= 240 ? spr_susied_dark_eyes : spr_susied_dark, s[1] = plot() >= 240 ? spr_susier_dark_eyes : spr_susier_dark;
    s[2] = spr_susieu_dark, s[3] = plot() >= 240 ? spr_susiel_dark_eyes : spr_susiel_dark;
  } else {
    s[0] = spr_ralseid, s[1] = spr_ralseir, s[2] = spr_ralseiu, s[3] = spr_ralseil;
  }
}

void follow_reset(void) {
  for (int i = 0; i < HIST; i++) hx[i] = pl.x, hy[i] = pl.y, hdir[i] = pl.dir;
  nfl = 0;
  if (!world_dark()) return;
  for (int k = 1; k < 3; k++) {
    int c = sv.chr[k];
    if (c != CH_SUSIE && c != CH_RALSEI) continue;
    Follower *f = &fl[nfl];
    f->c = c, f->target = 12 * (nfl + 1);
    f->walktimer = 0, f->walkbuffer = 0, f->dir = pl.dir;
    f->x = pl.x - (c == CH_SUSIE ? 6 : 4) * 2 / 2, f->y = pl.y - (c == CH_SUSIE ? 16 : 12);
    nfl++;
  }
}
void follow_show(bool on) { follow_on = on; }
bool follow_shown(void) { return follow_on; }

static void follow_step(void) {
  bool moved = pl.x != hx[0] || pl.y != hy[0];
  if (moved) {
    for (int i = HIST - 1; i > 0; i--) hx[i] = hx[i - 1], hy[i] = hy[i - 1], hdir[i] = hdir[i - 1];
    hx[0] = pl.x, hy[0] = pl.y, hdir[0] = pl.dir;
  }
  for (int k = 0; k < nfl; k++) {
    Follower *f = &fl[k];
    if (!moved) {
      if (f->walkbuffer <= 0) f->walktimer = 0;
      f->walkbuffer -= 3;
      continue;
    }
    int t = f->target < HIST - 1 ? f->target : HIST - 2;
    int32_t nx = hx[t] - (f->c == CH_SUSIE ? 6 : 4), ny = hy[t] - (f->c == CH_SUSIE ? 16 : 12);
    bool run = abs_i(hx[t + 1] - hx[t]) > 4 || abs_i(hy[t + 1] - hy[t]) > 4;
    if (nx != f->x || ny != f->y) f->walkbuffer = 24;
    f->x = nx, f->y = ny, f->dir = hdir[t];
    if (f->walkbuffer > 12) {
      f->walktimer += run ? 6 : 3;
      if (f->walktimer >= 80) f->walktimer -= 80;
    }
  }
}

Obj *follower(int c) {
  (void)c;
  return NULL;
}

/* ---------------------------------------------------------------- entering a room */
void world_enter(int room, int x, int y, int dir) {
  g_room = room;
  room_info(room, &ri);
  sv.darkzone = (ri.flags & ROOM_DARK) ? 1 : 0;
  obj_clear_all();
  pl.x = x, pl.y = y, pl.dir = (uint8_t)dir;
  pl.frozen = false, pl.hidden = false, pl.spr = 0, pl.img = 0;
  pl.walktimer = 0, pl.walkbuffer = 0, pl.runtimer = 0;
  cam_locked = false;
  follow_on = true;
  follow_reset();
  room_setup(room);
  world_camera();
}

bool world_inst(int obj, int k, Inst *out) {
  for (int i = 0; i < ri.ninst; i++) {
    room_inst(&ri, i, out);
    if (out->obj == obj && k-- == 0) return true;
  }
  return false;
}

bool world_marker(int obj, int *x, int *y) {
  Inst in;
  if (!world_inst(obj, 0, &in)) return false;
  *x = in.x, *y = in.y;
  return true;
}

/* the marker each entrance lands on (obj_mainchara's create) */
static int marker_of(int entrance) {
  switch (entrance) {
    case 1: return obj_markerA;
    case 2: return obj_markerB;
    case 4: return obj_markerC;
    case 5: return obj_markerD;
    case 6: return obj_markerE;
    case 7: return obj_markerF;
    case 23: return obj_markerw;
    case 24: return obj_markerX;
    default: return -1;
  }
}

void world_enter_marker(int room, int entrance) {
  RoomInfo r;
  room_info(room, &r);
  int x = 0, y = 0, m = marker_of(entrance);
  bool found = false;
  for (int i = 0; i < r.ninst && m >= 0; i++) {
    Inst in;
    room_inst(&r, i, &in);
    if (in.obj == m) {
      x = in.x, y = in.y, found = true;
      break;
    }
  }
  if (!found) {
    /* (no marker: where the room puts Kris itself) */
    for (int i = 0; i < r.ninst; i++) {
      Inst in;
      room_inst(&r, i, &in);
      if (in.obj == obj_mainchara) x = in.x, y = in.y;
    }
  }
  world_enter(room, x, y, pl.dir);
}

/* ---------------------------------------------------------------- the camera */
void world_camera(void) {
  if (cam_locked) return;
  int s = world_scale();
  if (world_dark()) {
    cam_x = pl.x - (view_w() / 2 - 19);
    cam_y = pl.y - (view_h() / 2 - 38);
  } else {
    cam_x = pl.x + 11 - view_w() / 2;
    cam_y = pl.y + 17 - view_h() / 2;
  }
  (void)s;
  cam_x = clamp_i(cam_x, 0, max_i(0, ri.w - view_w()));
  cam_y = clamp_i(cam_y, 0, max_i(0, ri.h - view_h()));
}

/* ---------------------------------------------------------------- doors and fades */
static struct {
  int phase, t;            /* 1 fading out, 2 fading in */
  int room, entrance;
} fade;

void world_fade_to(int room, int entrance) {
  fade.phase = 1, fade.t = 0, fade.room = room, fade.entrance = entrance;
  g_fade_col = C_BLACK;
}
bool world_fading(void) { return fade.phase != 0; }
/* (from black, as obj_persistentfadein with image_alpha 1.2: three frames black, then 0.08 a frame) */
void world_fade_in(void) { fade.phase = 2, fade.t = 11, g_fade_col = C_BLACK, g_fade = 255; }

static void fade_step(void) {
  if (!fade.phase) return;
  if (fade.phase == 1) {
    fade.t++;
    g_fade = (uint8_t)min_i(255, fade.t * 255 / 8);
    if (fade.t >= 8) {
      world_enter_marker(fade.room, fade.entrance);
      fade.phase = 2, fade.t = 8;
    }
  } else {
    fade.t--;
    g_fade = (uint8_t)clamp_i(fade.t * 255 / 8, 0, 255);
    if (fade.t <= 0) fade.phase = 0, g_fade = 0;
  }
}

/* ---------------------------------------------------------------- Kris (obj_mainchara's step) */
static void kris_animate(bool walked, bool running) {
  if (walked) pl.walkbuffer = 24;
  if (pl.walkbuffer > 12) {
    pl.walktimer += running ? 6 : 3;
    if (pl.walktimer >= 80) pl.walktimer -= 80;
    pl.img = pl.walktimer / 20;
  }
  if (pl.walkbuffer <= 0) {
    pl.walktimer = pl.walktimer < 20 ? 19 : pl.walktimer < 40 ? 39 : pl.walktimer < 60 ? 59 : 79;
    pl.img = 0;
  }
  pl.walkbuffer -= 3;
}

static void kris_move(void) {
  uint32_t k = g_held;
  bool dark = world_dark();
  int bw = dark ? 4 : 3;
  bool run = (k & K_X) != 0;
  if (flag(11) == 1) run = !run;
  int ws = bw;
  if (run) {
    if (!dark) ws = bw + (pl.runtimer > 60 ? 3 : pl.runtimer > 10 ? 2 : 1);
    else ws = bw + (pl.runtimer > 60 ? 5 : pl.runtimer > 10 ? 4 : 2);
  }
  bool l = k & K_LEFT, r = k & K_RIGHT, u = k & K_UP, d = k & K_DOWN;
  int px = 0, py = 0, pressdir = -1;
  if (r) px = ws, pressdir = DIR_RIGHT;
  if (l) px = -ws, pressdir = DIR_LEFT;
  if (d) py = ws, pressdir = DIR_DOWN;
  if (u) py = -ws, pressdir = DIR_UP;
  /* facing: kept while its key is held */
  bool held_now = (pl.dir == DIR_DOWN && d) || (pl.dir == DIR_UP && u) || (pl.dir == DIR_LEFT && l) || (pl.dir == DIR_RIGHT && r);
  if (pressdir >= 0 && !held_now) pl.dir = (uint8_t)pressdir;
  if (pl.dir == DIR_UP && d && !u) pl.dir = DIR_DOWN;
  if (pl.dir == DIR_DOWN && u && !d) pl.dir = DIR_UP;
  if (pl.dir == DIR_LEFT && r && !l) pl.dir = DIR_RIGHT;
  if (pl.dir == DIR_RIGHT && l && !r) pl.dir = DIR_LEFT;
  int x = pl.x, y = pl.y;
  if (!world_place_free(x + px, y + py, -1)) {
    /* blocked going sideways: around a corner, up or down, as far as it walks */
    if (px && !world_place_free(x + px, y, -1)) {
      for (int g = ws; g > 0; g--) {
        if (!d && world_place_free(x + px, y - g, -1)) { y -= g, py = 0; break; }
        if (!u && world_place_free(x + px, y + g, -1)) { y += g, py = 0; break; }
      }
      int i = px;
      while (i && !world_place_free(x + i, y, -1)) i -= sgn_i(i);
      px = i;
    }
    if (py && !world_place_free(x + px, y + py, -1) && !world_place_free(x, y + py, -1)) {
      for (int g = ws; g > 0; g--) {
        if (!r && world_place_free(x - g, y + py, -1)) { x -= g, px = 0; break; }
        if (!l && world_place_free(x + g, y + py, -1)) { x += g, px = 0; break; }
      }
      int j = py;
      while (j && !world_place_free(x + px, y + j, -1)) j -= sgn_i(j);
      py = j;
    }
    if (!world_place_free(x + px, y + py, -1)) {
      if (world_place_free(x + px, y, -1)) py = 0;
      else if (world_place_free(x, y + py, -1)) px = 0;
      else px = py = 0;
    }
  }
  bool moving = px || py;
  if (run && moving) pl.runtimer++;
  else pl.runtimer = 0;
  pl.x = x + px, pl.y = y + py;
  kris_animate(moving || x != pl.x - px || y != pl.y - py, run && moving);
}

/* what Z reaches in front of Kris (the original's collision rectangles) */
static Obj *talk_target(void) {
  int d = world_scale(), w = 19 * d, h = 38 * d;
  int x0, y0, x1, y1, x = pl.x, y = pl.y;
  switch (pl.dir) {
    case DIR_RIGHT: x0 = x + w / 2, y0 = y + 6 * d + h / 2, x1 = x + w + 13 * d, y1 = y + h; break;
    case DIR_LEFT: x0 = x - 13 * d, y0 = y + 6 * d + h / 2, x1 = x + w / 2, y1 = y + h; break;
    case DIR_DOWN: x0 = x + 4 * d, y0 = y + 28 * d, x1 = x + w - 4 * d, y1 = y + h + 15 * d; break;
    default: x0 = x + 3 * d, y0 = y + 5 * d, x1 = x + w - 5 * d, y1 = y + h - 5 * d; break;
  }
  for (int i = 0; i < MAX_OBJS; i++) {
    Obj *o = &objs[i];
    if ((o->flags & (OF_TALK | OF_GONE)) != OF_TALK) continue;
    int ox = o->x / FX + o->bx, oy = o->y / FX + o->by;
    if (x1 >= ox && x0 < ox + o->bw && y1 >= oy && y0 < oy + o->bh) return o;
  }
  return NULL;
}

static Obj *touch_target(void) {
  int x0, y0, x1, y1;
  kris_box(pl.x, pl.y, &x0, &y0, &x1, &y1);
  for (int i = 0; i < MAX_OBJS; i++) {
    Obj *o = &objs[i];
    if ((o->flags & (OF_TOUCH | OF_GONE)) != OF_TOUCH) continue;
    int ox = o->x / FX + o->bx, oy = o->y / FX + o->by;
    if (x1 > ox && x0 < ox + o->bw && y1 > oy && y0 < oy + o->bh) return o;
  }
  return NULL;
}

void pl_face(int dir) { pl.dir = (uint8_t)dir; }

/* ---------------------------------------------------------------- Kris walked by a cutscene */
static struct {
  int x, y, sp;
  bool on;
} walk;
void pl_walk_to(int x, int y, int sp) { walk.x = x, walk.y = y, walk.sp = sp ? sp : 4, walk.on = true; }
bool pl_walking(void) { return walk.on; }
static void pl_walk_step(void) {
  if (!walk.on) {
    if (!pl.spr) kris_animate(false, false);   /* (a cutscene's own sprite keeps its frame) */
    return;
  }
  int dx = walk.x - pl.x, dy = walk.y - pl.y;
  if (dx) pl.dir = dx < 0 ? DIR_LEFT : DIR_RIGHT;
  if (abs_i(dy) > abs_i(dx)) pl.dir = dy < 0 ? DIR_UP : DIR_DOWN;
  pl.x += clamp_i(dx, -walk.sp, walk.sp);
  pl.y += clamp_i(dy, -walk.sp, walk.sp);
  kris_animate(true, false);
  if (pl.x == walk.x && pl.y == walk.y) walk.on = false;
}

void world_update(void) {
  for (int i = 0; i < MAX_OBJS; i++)
    if (!(objs[i].flags & OF_GONE)) obj_step(&objs[i]);
  fade_step();
  if (fade.phase) return;
  bool busy = text_active() || event_blocking() || pl.frozen;
  if (!busy) {
    kris_move();
    if (g_pressed & K_Z) {
      Obj *o = talk_target();
      if (o && o->event) {
        if (o->sprs[0]) o->dir = (uint8_t)((pl.dir + 2) & 3), o->spr = o->sprs[o->dir];
        event_start(o->event, o);
      }
    }
    if (!event_running()) {
      Obj *t = touch_target();
      if (t && t->event) event_start(t->event, t);
    }
    if ((g_pressed & K_C) && !event_running()) room_menu();
  } else if (!text_active()) {
    pl_walk_step();
  } else {
    kris_animate(false, false);
  }
  follow_step();
  world_camera();
  if (g_shake > 0) g_shake--;
}

/* ---------------------------------------------------------------- drawing */
typedef struct {
  int key;       /* the bottom, for the drawing order */
  int16_t i;     /* objs index, -1 Kris, -2 - k follower k */
} DrawRef;

static int sx_of(int x) { return (x - cam_x) / world_scale(); }
static int sy_of(int y) { return (y - cam_y) / world_scale(); }

void world_draw_scene(void) {
  int s = world_scale();
  int shx = g_shake ? (int)(rnd() % 5) - 2 : 0;
  int ox = cam_x / s - shx, oy = cam_y / s;
  if (ri.bgcol) gfx_rect(0, 0, SCREEN_W, SCREEN_H, ri.bgcol, 255);
  gfx_tiles(ox, oy, false);
  DrawRef refs[MAX_OBJS + 4];
  int n = 0;
  for (int i = 0; i < MAX_OBJS; i++) {
    const Obj *o = &objs[i];
    if (o->flags & (OF_GONE | OF_HIDDEN) || o->spr == 0xFFFF) continue;
    int key = o->y / FX + spr_h(o->spr) * s + o->depth_bias;
    if (o->flags & OF_BACK) key = -100000;
    if (o->flags & OF_FRONT) key = 1000000;
    refs[n].key = key, refs[n].i = (int16_t)i, n++;
  }
  if (!pl.hidden) refs[n].key = pl.y + 38 * s, refs[n].i = -1, n++;
  if (follow_on)
    for (int k = 0; k < nfl; k++) refs[n].key = fl[k].y + 46 * s - 1, refs[n].i = (int16_t)(-2 - k), n++;
  /* in order of their bottoms (insertion sort: few) */
  for (int a = 1; a < n; a++) {
    DrawRef t = refs[a];
    int b = a - 1;
    while (b >= 0 && refs[b].key > t.key) refs[b + 1] = refs[b], b--;
    refs[b + 1] = t;
  }
  for (int k = 0; k < n; k++) {
    int i = refs[k].i;
    if (i == -1) {
      uint16_t spr = pl.spr ? pl.spr : kris_spr(pl.dir);
      gfx_sprite(spr, pl.spr ? pl.img : pl.img, sx_of(pl.x) + shx, sy_of(pl.y), 0, 255);
    } else if (i <= -2) {
      const Follower *f = &fl[-2 - i];
      uint16_t sp[4];
      follower_sprites(f->c, sp);
      gfx_sprite(sp[f->dir & 3], f->walktimer / 20, sx_of(f->x) + shx, sy_of(f->y), 0, 255);
    } else {
      const Obj *o = &objs[i];
      int fr = o->img / FX;
      if (o->scale)
        gfx_sprite_ex(o->spr, fr, sx_of(o->x / FX) + shx, sy_of(o->y / FX), o->flags & OF_FLIPX ? -o->scale : o->scale,
                      o->scale, 0, 0, o->alpha);
      else
        gfx_sprite(o->spr, fr, sx_of(o->x / FX) + shx, sy_of(o->y / FX), o->flags & OF_FLIPX ? GFX_FLIPX : 0, o->alpha);
    }
  }
  if (ri.fgrid) gfx_tiles(ox, oy, true);
  room_draw_over();
}

void world_draw(void) {
  world_draw_scene();
  text_draw();
  if (g_fade) gfx_rect(0, 0, SCREEN_W, SCREEN_H, g_fade_col, g_fade);
}
