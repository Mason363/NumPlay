/* The level: a chapter's rooms (two at once during a transition), collisions with their tiles, the camera, the
 * transitions between rooms, deaths and respawns (Celeste's Level and LevelLoader, after src/level.c). */
#include "ce.h"

Level g_level;
#define GRID_BYTES GRID_MAX   /* a room's grids in RAM (the biggest of the chapters here) */
static uint8_t *grid_mem[2];
uint8_t *g_scratch;   /* 1 KB for passing things (a scaled sprite's pixels) */
uint8_t *g_cell_cache;   /* draw.c's */
uint8_t g_cells_gen;

/* the RAM plat_ram gives: the entities, the two rooms' grids and tile tables */
bool level_ram(void) {
  unsigned n = sizeof(Ent) * MAX_ENTS + 2 * (GRID_BYTES + 256 * sizeof(TEnt)) + 1024 + CELL_CACHE;
  uint8_t *m = plat_ram(n);
  if (!m) return false;
  memset(m, 0, n);
  g_ents = (Ent *)(void *)m, m += sizeof(Ent) * MAX_ENTS;
  g_scratch = m, m += 1024;
  g_cell_cache = m, m += CELL_CACHE;
  for (int k = 0; k < 2; k++) {
    g_level.rooms[k].tab = (TEnt *)(void *)m, m += 256 * sizeof(TEnt);
    grid_mem[k] = m, m += GRID_BYTES;
  }
  return true;
}
static int pending_room = -1, pending_intro;

/* a room record (tools/pack_ce.py Chapter.room) */
typedef struct { const uint8_t *name; int nlen; const uint8_t *after; } RoomName;
static const uint8_t *rec_after_name(const uint8_t *r) { return r + 1 + r[0]; }
static int rec_x(const uint8_t *r) { return S24(rec_after_name(r)); }
static int rec_y(const uint8_t *r) { return S24(rec_after_name(r) + 3); }
static int rec_w(const uint8_t *r) { return (int)R16(rec_after_name(r) + 6); }
static int rec_h(const uint8_t *r) { return (int)R16(rec_after_name(r) + 8); }

int level_room_at(int x, int y) {
  for (int i = 0; i < g_nrooms; i++) {
    const uint8_t *r = room_rec(i);
    int rx = rec_x(r), ry = rec_y(r);
    if (x >= rx && y >= ry && x < rx + rec_w(r) && y < ry + rec_h(r)) return i;
  }
  return -1;
}
int level_room_find(const char *name) {
  int n = (int)strlen(name);
  for (int i = 0; i < g_nrooms; i++) {
    const uint8_t *r = room_rec(i);
    if (r[0] == n && !memcmp(r + 1, name, n)) return i;
  }
  return -1;
}
const char *level_room_name(void) {
  static char buf[24];
  const uint8_t *r = g_level.room->rec;
  int n = imin(r[0], 23);
  memcpy(buf, r + 1, n);
  buf[n] = 0;
  return buf;
}

uint8_t g_bg8[320 * 180];
int g_bg_refx, g_bg_refy;
static const uint8_t *bg_loaded;
static void background_load(const Room *rm) {   /* the room's picture of the backgrounds: 4 bits a pixel, to 8 */
  if (!rm->bg || rm->bg == bg_loaded) return;
  uint8_t *src = g_bg8 + 320 * 180 / 2, *d = g_bg8;
  plat_unpack(src, rm->bg);
  for (unsigned i = 0; i < 320 * 180 / 2; i++) {   /* (in place: what is written never passes what is read) */
    uint8_t b = *src++;
    *d++ = g_bg_map[b & 15];
    *d++ = g_bg_map[b >> 4];
  }
  bg_loaded = rm->bg;
  g_bg_refx = rm->bgx, g_bg_refy = rm->bgy;
}

static bool room_load(int slot, int index) {
  Room *rm = &g_level.rooms[slot];
  const uint8_t *r = room_rec(index), *p = rec_after_name(r);
  rm->rec = r;
  rm->x = S24(p), rm->y = S24(p + 3), rm->w = (int)R16(p + 6), rm->h = (int)R16(p + 8);
  rm->flags = p[10], rm->camx = (int8_t)p[11], rm->camy = (int8_t)p[12];
  rm->tw = (int)R16(p + 13), rm->th = (int)R16(p + 15);
  rm->gx = rm->x - MARGIN * 8, rm->gy = rm->y - MARGIN * 8;
  rm->rb = (rm->tw + 7) >> 3;
  p += 17;
  rm->ntiles = p[0];
  for (int i = 0; i <= rm->ntiles; i++) {
    int t = (int)R16(p + 1 + 2 * i);
    rm->tab[i].c.tile = t ? tile_data(t) : NULL;
    rm->tab[i].c.opaque = g_tile_flags[2 * t], rm->tab[i].c.drawn = g_tile_flags[2 * t + 1];
  }
  p += 1 + 2 * (rm->ntiles + 1);
  int raw = (int)R16(p);
  if (raw > GRID_BYTES) return false;
  rm->grid = grid_mem[slot];
  plat_unpack(rm->grid, HPTR(p + 2));
  rm->decals = HPTR(p + 5);
  rm->ents = HPTR(p + 8);
  rm->trigs = HPTR(p + 11);
  rm->bgx = S24(p + 14), rm->bgy = S24(p + 17);
  rm->bg = R24(p + 20) == 0xFFFFFF ? NULL : HPTR(p + 20);
  rm->index = index;
  g_cells_gen++;
  return true;
}

/* ---------------------------------------------------------------- collisions with the tiles */
static bool cell_solid(int cx, int cy) {
  for (int k = 0; k < 2; k++) {
    const Room *rm = &g_level.rooms[k ? g_level.slot ^ 1 : g_level.slot];
    if (rm->index < 0) continue;
    int x = cx - (rm->gx >> 3), y = cy - (rm->gy >> 3);
    if ((unsigned)x >= (unsigned)rm->tw || (unsigned)y >= (unsigned)rm->th) continue;
    return rm->grid[2 * rm->tw * rm->th + y * rm->rb + (x >> 3)] >> (x & 7) & 1;
  }
  return false;
}
/* Grid.Collide: [l, r) x [t, b) in pixels */
bool tiles_solid(int l, int t, int r, int b) {
  if (r <= l || b <= t) return false;
  int cx0 = l >> 3, cy0 = t >> 3, cx1 = (r - 1) >> 3, cy1 = (b - 1) >> 3;
  for (int cy = cy0; cy <= cy1; cy++)
    for (int cx = cx0; cx <= cx1; cx++)
      if (cell_solid(cx, cy)) return true;
  return false;
}
Ent *solid_first(int l, int t, int r, int b, const Ent *self) {
  for (int i = 0; i < g_nents; i++) {
    Ent *o = &g_ents[i];
    if (o != self && (o->flags & (EF_SOLID | EF_COLLIDABLE)) == (EF_SOLID | EF_COLLIDABLE) && o->type != 255 &&
        rect_hits(l, t, r, b, o))
      return o;
  }
  return NULL;
}
static Ent tiles_ent = {.type = 254, .flags = EF_SAFE | EF_SOLID | EF_COLLIDABLE};   /* SolidTiles */
bool solid_at(int l, int t, int r, int b, const Ent *self) { return tiles_solid(l, t, r, b) || solid_first(l, t, r, b, self); }
/* CollideFirst<Solid>: an entity, or the tiles' stand-in */
Ent *solid_hit(int l, int t, int r, int b, const Ent *self) {
  if (tiles_solid(l, t, r, b)) return &tiles_ent;
  return solid_first(l, t, r, b, self);
}
/* CollideFirstOutside<JumpThru>: one e touches at (x, y) and not where it is */
Ent *jumpthru_first_outside(const Ent *e, int x, int y) {
  for (int i = 0; i < g_nents; i++) {
    Ent *o = &g_ents[i];
    if ((o->flags & (EF_JUMPTHRU | EF_COLLIDABLE)) == (EF_JUMPTHRU | EF_COLLIDABLE) && o != e &&
        ent_collide_at(e, x, y, o) && !ent_collide_at(e, e->x, e->y, o))
      return o;
  }
  return NULL;
}

/* ---------------------------------------------------------------- effects */
void level_shake(int frames) { g_level.shake_timer = imax(g_level.shake_timer, frames), g_level.shake_dirx = g_level.shake_diry = 0; }
void level_dir_shake(int dx, int dy, int frames) {
  g_level.shake_timer = imax(g_level.shake_timer, frames);
  g_level.shake_dirx = dx, g_level.shake_diry = dy;
}
void level_freeze(int t) { g_level.freeze = imax(g_level.freeze, t); }

/* ---------------------------------------------------------------- loading */
static int spawn_near(int x, int y) {   /* the room's spawn nearest (x, y), as an index into its records */
  const uint8_t *p = g_level.room->ents;
  int n = p[0], best = -1;
  int32_t bd = 0;
  p++;
  for (int i = 0; i < n; i++, p += p[1]) {
    if (p[0] != ET_player) continue;
    int sx = g_level.room->x + S16(p + 2), sy = g_level.room->y + S16(p + 4);
    int32_t d = (int32_t)(sx - x) * (sx - x) + (int32_t)(sy - y) * (sy - y);
    if (best < 0 || d < bd) best = i, bd = d, g_level.respawn_x = sx, g_level.respawn_y = sy;
  }
  return best;
}

static void load_into_current(int index, int intro) {
  ents_clear();
  g_level.rooms[0].index = g_level.rooms[1].index = -1;
  g_cells_gen++;
  g_level.slot = 0;
  g_level.room = &g_level.rooms[0];
  room_load(0, index);
  Room *rm = g_level.room;
  background_load(rm);
  if (intro != INTRO_RESPAWN) spawn_near(rm->x, rm->y + rm->h);
  ents_spawn_room(0);
  player_spawn(g_level.respawn_x, g_level.respawn_y, intro);
  g_level.cam_offx = rm->camx * 48, g_level.cam_offy = rm->camy * 32;
  g_level.camx = player_cam_targetx() * 256, g_level.camy = player_cam_targety() * 256;
  g_level.transitioning = false;
  g_level.wipe = -24;
}

bool level_load(int chapter, int room, int intro) {
  {   /* (the RAM's pointers stay) */
    Room keep[2];
    memcpy(keep, g_level.rooms, sizeof keep);
    memset(&g_level, 0, sizeof g_level);
    memcpy(g_level.rooms, keep, sizeof keep);
  }
  bg_loaded = NULL;
  g_level.chapter = chapter;
  g_level.rooms[0].index = g_level.rooms[1].index = -1;
  if (room < 0) room = imax(level_room_at(0, 0), 0);
  g_level.room = &g_level.rooms[0];
  room_load(0, room);
  spawn_near(g_level.room->x, g_level.room->y + g_level.room->h);
  load_into_current(room, intro);
  return true;
}

/* ---------------------------------------------------------------- transitions */
static void transition_to(int index, int dx, int dy) {
  int slot = g_level.slot ^ 1;
  ents_remove_room(slot);
  if (!room_load(slot, index)) return;
  g_level.slot = slot;
  g_level.room = &g_level.rooms[slot];
  ents_spawn_room(slot);
  Room *rm = g_level.room;
  background_load(rm);
  g_level.transitioning = true;
  g_level.tr_dirx = dx, g_level.tr_diry = dy;
  g_level.tr_t = 0;
  g_level.tr_fromx = g_level.camx, g_level.tr_fromy = g_level.camy;
  int padx = dx * 4, pady = dy * (dy > 0 ? 12 : 4);
  Ent *pe = g_player.e;
  int tx = pe->x, ty = pe->y;
  if (dx != 0)
    while (ty >= rm->y + rm->h) ty--;
  for (int guard = 0; guard < 400; guard++) {   /* IsInBounds(playerTo, dirPad) */
    if (tx >= rm->x + imax(padx, 0) && ty >= rm->y + imax(pady, 0) && tx < rm->x + rm->w - imax(-padx, 0) &&
        ty < rm->y + rm->h - imax(-pady, 0))
      break;
    tx += dx, ty += dy;
  }
  g_level.tr_px = tx, g_level.tr_py = ty;
  int wx = pe->x, wy = pe->y;
  pe->x = tx, pe->y = ty;
  g_level.cam_offx = rm->camx * 48, g_level.cam_offy = rm->camy * 32;
  g_level.tr_tox = player_cam_targetx() * 256, g_level.tr_toy = player_cam_targety() * 256;
  pe->x = wx, pe->y = wy;
}

static int ease_cube_out(int t) {   /* t, result: 0..256 */
  int u = 256 - t;
  return 256 - (int)((int32_t)u * u / 256 * u / 256);
}
static void transition_update(void) {
  bool arrived = player_transition_to(g_level.tr_px, g_level.tr_py);
  g_level.tr_t = imin(g_level.tr_t + DTU, TIM(0.65f));
  int t = g_level.tr_t * 256 / TIM(0.65f);
  if (t > 230) g_level.camx = g_level.tr_tox, g_level.camy = g_level.tr_toy;
  else {
    int e = ease_cube_out(t);
    g_level.camx = g_level.tr_fromx + (int)((int32_t)(g_level.tr_tox - g_level.tr_fromx) * e / 256);
    g_level.camy = g_level.tr_fromy + (int)((int32_t)(g_level.tr_toy - g_level.tr_fromy) * e / 256);
  }
  if (arrived && g_level.tr_t >= TIM(0.65f)) {
    int old = g_level.slot ^ 1;
    ents_remove_room(old);
    g_level.rooms[old].index = -1;
    g_cells_gen++;
    spawn_near(g_player.e->x, g_player.e->y);
    player_on_transition();
    g_level.transitioning = false;
  }
}

/* Level.EnforceBounds */
static void enforce_bounds(void) {
  Ent *e = g_player.e;
  Room *rm = g_level.room;
  int bl = rm->x, bt = rm->y, br = rm->x + rm->w, bb = rm->y + rm->h;
  if (e_left(e) < bl) {
    int next = level_room_at((e_left(e) + e_right(e)) / 2 - 8, (e_top(e) + e_bottom(e)) / 2);
    if (e_top(e) >= bt && e_bottom(e) < bb && next >= 0 && next != rm->index) {
      transition_to(next, -1, 0);
      return;
    }
    e->x += bl - e_left(e);
    g_player.sx = 0;
  }
  if (e_right(e) > br) {
    int next = level_room_at((e_left(e) + e_right(e)) / 2 + 8, (e_top(e) + e_bottom(e)) / 2);
    if (e_top(e) >= bt && e_bottom(e) < bb && next >= 0 && next != rm->index) {
      transition_to(next, 1, 0);
      return;
    }
    e->x += br - e_right(e);
    g_player.sx = 0;
  }
  int cym = e->y + e->hy + e->hh / 2;
  if (cym < bt) {
    int next = level_room_at((e_left(e) + e_right(e)) / 2, cym - 12);
    if (next >= 0 && next != rm->index) {
      player_before_up_transition();
      transition_to(next, 0, -1);
      return;
    }
    if (e_top(e) < bt - 24) {
      e->y += bt - 24 - e_top(e);
      g_player.sy = 0;
    }
  }
  if (e_bottom(e) > bb) {
    int next = level_room_at((e_left(e) + e_right(e)) / 2, cym + 12);
    if (next >= 0 && next != rm->index && !(rm->flags & 8)) {
      if (!solid_at(e_left(e), e_top(e) + 4, e_right(e), e_bottom(e) + 4, e)) {
        player_before_down_transition();
        transition_to(next, 0, 1);
      }
      return;
    }
  }
  if (e_top(e) > bb) player_die(0, 0);
}

/* ---------------------------------------------------------------- the frame */
void level_update(void) {
  if (g_level.freeze > 0) {   /* Engine.FreezeTimer */
    g_level.freeze -= DTU;
    return;
  }
  g_level.frame++;
  if (!g_level.completed) g_level.time++;
  if (g_level.shake_timer > 0) {   /* as many pixels as tenths of a second left, rounded up (every 0.04 s) */
    if (g_level.frame % 2 == 0) {
      int n = (g_level.shake_timer + 5) / 6;
      if (!g_level.shake_dirx && !g_level.shake_diry)
        g_level.shakex = rndn(n * 2 + 1) - n, g_level.shakey = rndn(n * 2 + 1) - n;
      else {
        g_level.last_dir_shake = (int8_t)(g_level.last_dir_shake ? -g_level.last_dir_shake : 1);
        g_level.shakex = g_level.shake_dirx * -g_level.last_dir_shake * n;
        g_level.shakey = g_level.shake_diry * -g_level.last_dir_shake * n;
      }
    }
    g_level.shake_timer--;
  } else
    g_level.shakex = g_level.shakey = 0;
  if (g_level.wipe < 0) g_level.wipe++;
  else if (g_level.wipe > 0 && g_level.wipe < 24) g_level.wipe++;
  if (g_level.transitioning) {
    transition_update();
  } else {
    ents_update();
    extern bool player_in_control(void);
    if (!g_player.dead && player_in_control() && !g_level.transitioning) enforce_bounds();
  }
  if (pending_room >= 0) {
    int r = pending_room;
    pending_room = -1;
    load_into_current(r, pending_intro);
    return;
  }
  if (g_level.dead_reload) {
    g_level.dead_reload = false;
    load_into_current(g_level.room->index, INTRO_RESPAWN);
  }
}

void level_render(void) {
  g_camx = (g_level.camx >> 8) + g_level.shakex;
  g_camy = (g_level.camy >> 8) + g_level.shakey;
  draw_layers_back();
  draw_decals(0);
  ents_render(32767, -10000);
  draw_layer_fg();
  draw_decals(1);
  ents_render(-10000, -32768);
}
