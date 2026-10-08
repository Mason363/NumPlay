#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")   /* not drawn every frame: smaller over faster */
#endif
/* The level: loading rooms out of data.bin, the camera, room transitions,
 * death and respawn. Follows Celeste's Level and LevelLoader. */
#include "level.h"
#include "player.h"
#include "fx.h"
#include "wipe.h"
#include "entities.h"

Level g_level;

/* ---------------------------------------------------------------- chapter data */
static const uint8_t *chapter_rec(int c) {
  const uint8_t *s = section(SEC_CHAPTERS);
  return s + rd32(s + 4 + 4 * c);
}
static const uint8_t *room_rec(int i) {
  const uint8_t *c = g_level.ch;
  return c + rd32(c + CH_ROOMS + 4 * i);
}
/* room record: u16 name, u16 pack, u32 off, u32 size, s32 x, s32 y, u16 w, u16 h, u8 flags, u8 wind,
 * s8 camx, s8 camy, u32 style mask lo, hi, u16 music, u8 enforce dashes, u8 first torch bit / 2 (RR_TORCHES) */
#define RR_NAME 0
#define RR_PACK 2
#define RR_OFF 4
#define RR_SIZE 8
#define RR_X 12
#define RR_Y 16
#define RR_W 20
#define RR_H 22
#define RR_FLAGS 24
#define RR_WIND 25
#define RR_CAMX 26
#define RR_CAMY 27
#define RR_STYLE 28

int chapter_find_room(int chapter, const char *name) {
  const uint8_t *c = chapter_rec(chapter);
  for (int i = 0; i < rd16(c + 2); i++)
    if (!strcmp(str(rd16(c + rd32(c + CH_ROOMS + 4 * i) + RR_NAME)), name)) return i;
  return -1;
}
const char *chapter_room_name(int chapter, int i) {   /* NULL past its last room */
  const uint8_t *c = chapter_rec(chapter);
  return i < rd16(c + 2) ? str(rd16(c + rd32(c + CH_ROOMS + 4 * i) + RR_NAME)) : NULL;
}
const char *level_room_name_of(int index) { return str(rd16(room_rec(index) + RR_NAME)); }
const char *level_room_name(void) { return g_level.room && g_level.room->info ? str(rd16(g_level.room->info)) : ""; }

/* ---------------------------------------------------------------- loading a room */
typedef struct { uint8_t *dst; uint32_t n, cap; } Copy;
static void copy_sink(const uint8_t *p, uint32_t n, void *ctx) {
  Copy *c = ctx;
  if (c->n + n > c->cap) n = c->cap > c->n ? c->cap - c->n : 0;
  memcpy(c->dst + c->n, p, n);
  c->n += n;
}

/* decodes a room's blob into scratch RAM; NULL if it does not fit */
static const uint8_t *room_blob(int index, uint32_t *size) {
  const uint8_t *r = room_rec(index);
  uint32_t cap;
  uint32_t n = rd32(r + RR_SIZE);
  uint8_t *scratch = res_scratch(n, &cap);
  if (n > cap) return NULL;
  Copy c = {scratch, 0, cap};
  res_stream(rd16(r + RR_PACK), rd32(r + RR_OFF), n, copy_sink, &c);
  *size = n;
  return scratch;
}

/* both loaded rooms live in one pool: slot 0 from the start, slot 1 from the end */
static uint32_t room_pool[ROOM_POOL_BYTES / 4];

static void setup_layer(TileLayer *L, const uint8_t *p, int th) {
  L->bits = p[1];
  L->types = p + 2;
  const uint8_t *rows = p + ((2 + p[0] + 1) & ~1);
  L->rows = (const uint16_t *)(const void *)rows;
  L->data = rows + 2 * th;
}

/* a tile layer as stored (its rows one after another) -> as kept (row offsets, each different row once) */
static void tiles_unpack(uint8_t *dst, const uint8_t *src, int tw, int th) {
  int nt = src[0], bits = src[1], mask = (1 << bits) - 1;
  memcpy(dst, src, 2u + nt);
  uint16_t *rows = (uint16_t *)(void *)(dst + ((2 + nt + 1) & ~1));
  uint8_t *data = (uint8_t *)(rows + th);
  const uint8_t *p = src + 2 + nt;
  uint32_t n = 0;
  for (int y = 0; y < th; y++) {
    int len = 0;
    for (int x = 0; x < tw; len++) x += (p[len] & mask) + 1;
    int j = 0;
    for (; j < y; j++) {   /* the same row as one before? */
      const uint8_t *q = data + rows[j];
      int k = 0;
      while (k < len && q[k] == p[k]) k++;
      if (k == len) break;
    }
    if (j < y) rows[y] = rows[j];
    else {
      rows[y] = (uint16_t)n;
      memcpy(data + n, p, (size_t)len);
      n += (uint32_t)len;
    }
    p += len;
  }
}

static void session_enter(int index, int intro);
static bool room_load(int slot, int index, int intro) {
  Room *rm = &g_level.rooms[slot];
  rm->index = -1;
  uint32_t size;
  const uint8_t *blob = room_blob(index, &size);
  if (!blob) return false;
  const uint8_t *r = room_rec(index);
  /* what stays: everything before the entities (the tiles unpacked: what follows them moves by `shift`),
   * and a bit per spinner */
  uint32_t nspin = rd16(blob + rd16(blob + 2 * RB_SPIN));
  int shift = (int16_t)rd16(blob + 2 * RB_SHIFT);
  uint32_t from = rd16(blob + 2 * RB_EXTRAS), keep = rd16(blob + 2 * RB_ENTS) + shift;
  uint32_t need = keep + (((nspin + 7) / 8 + 3) & ~3u);
  const Room *other = &g_level.rooms[slot ^ 1];
  uint8_t *pool = (uint8_t *)room_pool, *mem;
  if (slot == 0) {
    uint32_t free = other->index >= 0 ? (uint32_t)(other->mem - pool) : ROOM_POOL_BYTES;
    if (need > free) return false;
    mem = pool;
  } else {
    uint32_t free = ROOM_POOL_BYTES - (other->index >= 0 ? other->used : 0);
    if (need > free) return false;
    mem = pool + ROOM_POOL_BYTES - need;
  }
  rm->mem = mem;
  rm->used = need;
  rm->info = r;
  rm->x = rds32(r + RR_X), rm->y = rds32(r + RR_Y), rm->w = rd16(r + RR_W), rm->h = rd16(r + RR_H);
  rm->tx = rm->x / 8 - MARGIN, rm->ty = rm->y / 8 - MARGIN;
  rm->tw = rm->w / 8 + 2 * MARGIN, rm->th = rm->h / 8 + 2 * MARGIN;
  uint16_t *hdr = (uint16_t *)(void *)mem;
  memcpy(hdr, blob, 2 * RB_HEADER);
  hdr[RB_BG] = hdr[RB_BG_RAM];
  for (int k = RB_EXTRAS; k <= RB_END; k++) hdr[k] = (uint16_t)(hdr[k] + shift);
  tiles_unpack(mem + hdr[RB_FG], blob + rd16(blob + 2 * RB_FG), rm->tw, rm->th);
  tiles_unpack(mem + hdr[RB_BG], blob + rd16(blob + 2 * RB_BG), rm->tw, rm->th);
  memcpy(mem + hdr[RB_EXTRAS], blob + from, keep - hdr[RB_EXTRAS]);
  setup_layer(&rm->layer[0], mem + rd16(mem + 2 * RB_FG), rm->th);
  setup_layer(&rm->layer[1], mem + rd16(mem + 2 * RB_BG), rm->th);
  /* extras: objtiles, then scenery over fg and bg */
  uint8_t *o = mem + rd16(mem + 2 * RB_EXTRAS);
  rm->nobj = rd16(o);
  rm->obj = o + 2;
  o += 2 + rm->nobj * 6u;
  for (int k = 0; k < 2; k++) {
    rm->nscen[k] = rd16(o);
    rm->scen[k] = o + 2;
    o += 2 + rm->nscen[k] * 6u;
  }
  /* decals: u16 count, then (u16 tex | flips, s16 x, s16 y) */
  uint8_t *d = mem + rd16(mem + 2 * RB_DFG);
  rm->nfg = rd16(d);
  rm->decals_fg = d + 2;   /* u8 textures, u16 ids, then 5-byte records */
  d = mem + rd16(mem + 2 * RB_DBG);
  rm->nbg = rd16(d);
  rm->decals_bg = d + 2;
  /* spinners */
  const uint8_t *sp = mem + rd16(mem + 2 * RB_SPIN);
  rm->nspin = (int)nspin;
  rm->spin_row0 = rds16(sp + 2);
  rm->spin_rows = rd16(sp + 4);
  rm->spin_first = (const uint16_t *)(const void *)(sp + 6);
  rm->spin = rm->spin_first + rm->spin_rows + 1;
  rm->spin_mask = (const uint8_t *)(rm->spin + nspin);
  {   /* Level.LoadLevel: the Celestial Resort's spinners, and the Summit's d- rooms', are dust */
    const char *nm = str(rd16(room_rec(index) + RR_NAME));
    bool dusty = g_session.area == 3 || (g_session.area == 7 && nm[0] == 'd' && nm[1] == '-');
    rm->spin_dust = dusty ? rm->spin_mask + (nspin + 1) / 2 : NULL;
  }
  rm->spin_gone = mem + keep;
  rm->need = mem + rd16(mem + 2 * RB_NEED);
  memset(rm->spin_gone, 0, (nspin + 7) / 8);
  rm->index = index;
  rm->decal_t = rm->flower_t = rm->smoke_timer = 0, rm->flower_wind = 1, rm->flower_sign = 1;
  /* entities and triggers */
  int was = g_level.room_slot;
  Room *was_room = g_level.room;
  g_level.room_slot = slot, g_level.room = rm;   /* the room being loaded is the level's (Session.Level) */
  g_res_can_load = false;   /* the room's data is in the cache's free space */
  g_level.has_cassette_blocks = false, g_level.cassette_beats = 2;
  g_level.cam_lock = 0, g_level.cam_locker = NULL;   /* Level.LoadLevel: CameraLockMode = None */
  g_level.last_intro = (uint8_t)intro;
  g_level.has_start_position = false;
  g_level.in_space = (r[RR_FLAGS] & 2) != 0;   /* InSpace (LevelData.Space) */
  session_enter(index, intro);
  if (g_session.area == 0 && !g_save.cheat_mode && !strcmp(str(rd16(r + RR_NAME)), "-1")) unlock_everything_new();
  entities_load(rm, blob, slot);
  g_res_can_load = true;
  g_level.room_slot = was, g_level.room = was_room;
  return true;
}

/* textures beyond the rooms' (a textbox's portrait) */
static const uint16_t *extra_tex;
static int nextra_tex;
static void need_textures(void) {
  res_begin_need();
  for (int s = 0; s < 2; s++) {
    Room *rm = &g_level.rooms[s];
    if (rm->index >= 0) res_need_list(rm->need + 2, rd16(rm->need));
  }
  for (int i = 0; i < nextra_tex; i++) res_need(extra_tex[i]);
  res_load_needed();
}
void level_extra_textures(const uint16_t *ids, int n) {
  extra_tex = ids, nextra_tex = n;
  need_textures();
}

/* ---------------------------------------------------------------- tiles of the room */
static char rle_at(const Room *rm, int layer, int cx, int cy) {
  int lx = cx - rm->tx, ly = cy - rm->ty;
  if (lx < 0 || ly < 0 || lx >= rm->tw || ly >= rm->th) return '0';
  const TileLayer *L = &rm->layer[layer];
  const uint8_t *p = L->data + L->rows[ly];
  int bits = L->bits, mask = (1 << bits) - 1;
  for (;; p++) {
    int n = (*p & mask) + 1;
    if (lx < n) return (char)L->types[*p >> bits];
    lx -= n;
  }
}

char level_tile_type(int layer, int cx, int cy) {
  Room *rm = g_level.room;
  if (!rm) return '0';
  char c = rle_at(rm, layer, cx, cy);
  if (g_level.transitioning) {
    Room *o = &g_level.rooms[g_level.room_slot ^ 1];
    if (o->index >= 0) {
      int px = cx * 8, py = cy * 8;
      bool in_cur = px >= rm->x && px < rm->x + rm->w && py >= rm->y && py < rm->y + rm->h;
      if (!in_cur && px >= o->x && px < o->x + o->w && py >= o->y && py < o->y + o->h) c = rle_at(o, layer, cx, cy);
    }
  }
  return c;
}

/* the room's tile types of cells [x0, x1) of row cy (as rle_at) into out (out[0]: cell x0) */
static void rle_span(const Room *rm, int layer, int cy, int x0, int x1, char *out) {
  int ly = cy - rm->ty, lx = x0 - rm->tx, end = x1 - rm->tx;
  memset(out, '0', (size_t)(x1 - x0));
  if (ly < 0 || ly >= rm->th) return;
  if (end > rm->tw) end = rm->tw;
  const TileLayer *L = &rm->layer[layer];
  const uint8_t *p = L->data + L->rows[ly];
  int mask = (1 << L->bits) - 1;
  if (lx < 0) lx = 0;
  for (int x = 0; lx < end; p++) {   /* x: where the run starts */
    int x2 = x + (*p & mask) + 1;
    char c = (char)L->types[*p >> L->bits];
    for (; lx < x2 && lx < end; lx++) out[lx - (x0 - rm->tx)] = c;
    x = x2;
  }
}

/* level_tile_type of cells [cx, cx + n) of row cy (n <= 64) */
void level_tile_row(int layer, int cx, int cy, int n, char *out) {
  Room *rm = g_level.room;
  if (!rm) {
    memset(out, '0', (size_t)n);
    return;
  }
  rle_span(rm, layer, cy, cx, cx + n, out);
  Room *o = &g_level.rooms[g_level.room_slot ^ 1];
  int py = cy * 8;
  if (!g_level.transitioning || o->index < 0 || py < o->y || py >= o->y + o->h) return;
  bool row_cur = py >= rm->y && py < rm->y + rm->h;
  char other[64];
  rle_span(o, layer, cy, cx, cx + n, other);
  for (int k = 0; k < n; k++) {
    int px = (cx + k) * 8;
    bool in_cur = row_cur && px >= rm->x && px < rm->x + rm->w;
    if (!in_cur && px >= o->x && px < o->x + o->w) out[k] = other[k];
  }
}

/* any solid tile in the cells [cx0, cx1] x [cy0, cy1] (inclusive) */
bool level_solid_cells(int cx0, int cy0, int cx1, int cy1) {
  Room *rm = g_level.room;
  if (!rm) return false;
  const TileLayer *L = &rm->layer[0];
  int bits = L->bits, mask = (1 << bits) - 1;
  for (int cy = cy0; cy <= cy1; cy++) {
    int ly = cy - rm->ty;
    if (ly < 0 || ly >= rm->th) continue;
    const uint8_t *p = L->data + L->rows[ly];
    for (int x = rm->tx, end = rm->tx + rm->tw; x < end && x <= cx1; p++) {
      int x2 = x + (*p & mask) + 1;
      if (L->types[*p >> bits] != '0' && x2 > cx0) return true;
      x = x2;
    }
  }
  return false;
}

bool level_grid_collide(const Ent *grid, float l, float t, float r, float b) {
  (void)grid, (void)l, (void)t, (void)r, (void)b;
  return false;
}

/* ---------------------------------------------------------------- flags and ids */
static uint32_t hash_str(const char *s) {
  uint32_t h = 2166136261u;
  while (*s) h = (h ^ (uint8_t)*s++) * 16777619u;
  return h;
}
void level_set_flag(const char *flag, bool on) {
  Session *ss = &g_session;
  uint32_t h = hash_str(flag);
  for (int i = 0; i < ss->nflags; i++)
    if (ss->flags[i] == h) {
      if (!on) ss->flags[i] = ss->flags[--ss->nflags];
      return;
    }
  if (on && ss->nflags < 32) ss->flags[ss->nflags++] = h;
}
bool level_get_flag(const char *flag) {
  uint32_t h = hash_str(flag);
  for (int i = 0; i < g_session.nflags; i++)
    if (g_session.flags[i] == h) return true;
  return false;
}
uint32_t level_entity_id(int room, int eid) { return (uint32_t)room << 16 | (uint16_t)eid; }
static uint32_t *dnl_at(int i) { return i < 32 ? &g_session.dnl[i] : &g_session.dnl_more[i - 32]; }
bool level_do_not_load(uint32_t id) {
  for (int i = 0; i < g_session.ndnl; i++)
    if (*dnl_at(i) == id) return true;
  return false;
}
void level_set_do_not_load(uint32_t id) {
  if (!level_do_not_load(id) && g_session.ndnl < 48) *dnl_at(g_session.ndnl++) = id;
}

/* ---------------------------------------------------------------- the player */
static Player *player_of(void) { return g_player.ent && g_player.ent->cls ? &g_player : NULL; }
Ent *level_player_ent(void) { return player_of() ? g_player.ent : NULL; }
Player *level_player(void) { return player_of(); }
float level_player_speed_x(void) { return g_player.speed.x; }
bool level_player_climbing(void) { return g_player.state == ST_CLIMB; }
int level_player_facing(void) { return g_player.facing; }

/* ---------------------------------------------------------------- time */
bool level_on_interval(float interval) {
  return floorf((g_level.time_active - DT) / interval) < floorf(g_level.time_active / interval);
}
bool level_on_raw_interval(float interval) {
  return floorf((g_level.raw_time_active - RAW_DT) / interval) < floorf(g_level.raw_time_active / interval);
}
void level_shake(float t) {
  if (g_save.options & 1) return;   /* Settings: Screen Shake Effects off */
  g_level.shake_dir = v2(0, 0);
  g_level.shake_timer = fmaxf(g_level.shake_timer, t);
}
void level_dir_shake(V2 dir, float t) {
  if (g_save.options & 1) return;
  g_level.shake_dir = v2norm(dir);
  g_level.last_dir_shake = 0;
  g_level.shake_timer = fmaxf(g_level.shake_timer, t);
}
void level_freeze(float t) {
  if (t > g_level.freeze) g_level.freeze = t;
}

/* ---------------------------------------------------------------- spawn points */
V2 level_spawn_near(V2 at) {
  /* LevelData.Spawns.ClosestTo: the room's player entities */
  return player_spawn_near(at);
}

bool level_in_bounds(V2 p, float pad) {   /* Level.IsInBounds(position, pad): the room grown by pad */
  Room *rm = g_level.room;
  return p.x >= rm->x - pad && p.x < rm->x + rm->w + pad && p.y >= rm->y - pad && p.y < rm->y + rm->h + pad;
}

static int room_at(V2 p) {
  for (int i = 0; i < g_level.nrooms; i++) {
    const uint8_t *r = room_rec(i);
    int x = rds32(r + RR_X), y = rds32(r + RR_Y), w = rd16(r + RR_W), h = rd16(r + RR_H);
    if (p.x >= x && p.x < x + w && p.y >= y && p.y < y + h) return i;
  }
  return -1;
}

/* ---------------------------------------------------------------- level start / reload */
/* Level.LoadLevel's bookkeeping in the session for room `index`, before its entities */
static void session_enter(int index, int intro) {
  Session *ss = &g_session;
  ss->level = (uint8_t)index;
  if (intro != INTRO_FALL || !strcmp(str(rd16(room_rec(index) + RR_NAME)), "0")) {
    uint8_t *v = &ss->visited[index >> 3], bit = (uint8_t)(1 << (index & 7));
    if (!(*v & bit)) ss->furthest_seen = (uint8_t)(index + 1);   /* FurthestSeenLevel */
    *v |= bit;                                                     /* LevelFlags */
    ss->dashes_at_level_start = ss->dashes;                        /* UpdateLevelStartDashes */
  }
}

static float spawn_fx = 0, spawn_fy = 1;   /* LoadLevel's spawn: the one nearest this point of the room (fractions) */
static bool just_started;                  /* Session.JustStarted: the session's first room is loading */
static void load_into_current(int index, int intro) {
  ents_clear(false);
  blocks_free_tiles(1);   /* (slot 0's go when it loads) */
  g_level.rooms[0].index = g_level.rooms[1].index = -1;
  g_level.room_slot = 0;
  g_level.room = &g_level.rooms[0];
  level_tiles_entities();
  spinners_entities();
  dust_entities();
  if (!room_load(0, index, intro)) {   /* no room to read it: everything loaded goes first */
    res_flush();
    room_load(0, index, intro);
  }
  need_textures();
  g_level.room = &g_level.rooms[0];
  Room *rm = g_level.room;
  g_level.core_mode = g_session.core_mode;
  if (!g_session.has_respawn) {   /* DefaultSpawnPoint, or the one LoadLevel was given; the checkpoint's at a start there */
    V2 from = v2(rm->x + rm->w * spawn_fx, rm->y + rm->h * spawn_fy);
    if (just_started && !g_session.started_from_beginning && g_level.has_start_position) from = g_level.start_position;
    V2 at = player_spawn_near(from);
    spawn_fx = 0, spawn_fy = 1;
    g_session.rx = (int32_t)at.x, g_session.ry = (int32_t)at.y;
    g_session.has_respawn = 1;
  }
  player_spawn(v2((float)g_session.rx, (float)g_session.ry), intro);
  ents_awake_new();
  cassette_level_start(false);
  g_level.cam_offset = v2(rds8(rm->info + RR_CAMX) * 48.f, rds8(rm->info + RR_CAMY) * 32.f);
  g_level.cam = player_camera_target(&g_player);
  g_level.cam_upward_max_y = g_level.cam.y + 180;
  /* the wipe in: a spotlight on Madeline when the level begins, the area's own after a death */
  if (intro == INTRO_RESPAWN || intro == INTRO_FALL) wipe_area(true, NULL);
  else {
    wipe_start(WIPE_SPOTLIGHT, true, NULL);
  }
  g_wipe.focus = v2((float)g_session.rx - g_level.cam.x, (float)g_session.ry - g_level.cam.y);
  g_level.wind_pattern = rm->info[RR_WIND];
  g_level.transitioning = false;
  tiles_invalidate();
}

bool level_visited(const char *room) {
  int i = chapter_find_room(g_session.chapter, room);
  return i >= 0 && (g_session.visited[i >> 3] >> (i & 7) & 1);
}

int level_start_room(void) {
  int i = room_at(v2(0, 0));
  return i < 0 ? 0 : i;
}

/* Level.RegisterAreaComplete and CompleteArea: a wipe, then the chapter's end (LevelExit) */
void level_register_complete(void) {
  if (g_level.completed) return;
  berries_collect_all();
  g_level.completed = true;
  save_register_completion();
}
static void exit_completed(void) { game_level_exit(LEXIT_COMPLETED); }
/* the pause menu's ways out, behind the area's wipe (DoScreenWipe) */
static uint8_t exit_mode;
static void exit_now(void) { game_level_exit(exit_mode); }
void level_save_and_quit(void) {
  exit_mode = LEXIT_SAVEQUIT;
  g_level.pause_lock = true;
  wipe_area(false, exit_now);
}
void level_give_up(bool restart) {
  exit_mode = restart ? LEXIT_RESTART : LEXIT_GIVEUP;
  g_level.pause_lock = true;
  wipe_area(false, exit_now);
}
void level_complete_area(bool spotlight, bool skip_wipe) {
  level_register_complete();
  g_level.pause_lock = true;
  if (!g_level.skipping_cutscene && !skip_wipe) {
    if (spotlight) {
      wipe_start(WIPE_SPOTLIGHT, false, exit_completed);
      g_wipe.focus = v2(g_player.ent->x - g_level.cam.x, g_player.ent->y - g_level.cam.y - 8);
    } else
      wipe_start(WIPE_FADE, false, exit_completed);
    return;
  }
  exit_completed();
}

/* the memory each chapter file's code keeps (chN.c), 0 by default */
WEAK uint32_t ch1_ram(void) { return 0; }
WEAK uint32_t ch8_ram(void) { return 0; }
WEAK uint32_t ch2_ram(void) { return 0; }
WEAK uint32_t ch3_ram(void) { return 0; }
WEAK uint32_t ch4_ram(void) { return 0; }
WEAK uint32_t ch5_ram(void) { return 0; }
WEAK uint32_t ch6_ram(void) { return 0; }
WEAK uint32_t ch7_ram(void) { return 0; }
WEAK uint32_t ch9_ram(void) { return 0; }
WEAK uint32_t heart_ram(void) { return 0; }
WEAK uint32_t cassette_ram(void) { return 0; }
WEAK uint32_t dust_ram(void) { return 0; }
uint8_t *g_chram[16];
/* the files whose entities the chapter has get their memory side by side, from the top of the cache */
static void chapter_memory(const uint8_t *ch) {
  uint32_t (*const RAM[16])(void) = {[1] = ch1_ram, [2] = ch2_ram, [3] = ch3_ram, [4] = ch4_ram, [5] = ch5_ram,
                                     [6] = ch6_ram, [7] = ch7_ram, [8] = ch8_ram, [9] = ch9_ram,
                                     [13] = dust_ram, [14] = cassette_ram, [15] = heart_ram};
  uint16_t files = rd16(ch + CH_FILES) | 0xC000;   /* 14, 15: every chapter's (cassettes, hearts) */
  if (ch[CH_AREA] == 3 || ch[CH_AREA] == 7) files |= 1 << 13;   /* 13: the dust's (dust.c) */
  uint32_t total = 0, at[16] = {0};
  for (int i = 0; i < 16; i++)
    if ((files >> i & 1) && RAM[i]) {
      at[i] = total;
      total += (RAM[i]() + 7) & ~7u;
    }
  res_chapter_ram(total);
  for (int i = 0; i < 16; i++) g_chram[i] = g_chapter_ram + at[i];
}

/* LevelLoader: the session's chapter, at its level */
void level_start(int intro) {
  memset(&g_level, 0, sizeof g_level);
  trail_clear();   /* (a new Level: a new TrailManager, new particle systems) */
  particles_clear();
  g_level.chapter = g_session.chapter;
  g_level.ch = chapter_rec(g_session.chapter);
  chapter_memory(g_level.ch);
  g_level.nrooms = rd16(g_level.ch + 2);
  g_level.rooms[0].index = g_level.rooms[1].index = -1;
  g_level.can_retry = true;
  g_level.tr_duration = 0.65f;
  g_level.zoom = g_level.zoom_target = 1;   /* ResetZoom */
  g_level.zoom_focus = v2(160, 90);
  int idx = g_session.level;
  if (idx >= g_level.nrooms) idx = g_session.level = (uint8_t)level_start_room();   /* MapData.StartLevel */
  just_started = !g_session.has_respawn;
  load_into_current(idx, intro);
  just_started = false;
  style_init();
}

/* Level.Reload: after a death */
void level_reload(void) {
  Session *ss = &g_session;
  if (ss->first_level && !ss->berries && !ss->cassette && !ss->heart && !ss->hit_checkpoint) {
    ss->time = 0, ss->deaths = 0;
    g_level.timer_started = false;
  }
  ss->dashes = ss->dashes_at_level_start;
  g_time_rate = 1;   /* Engine.TimeRate */
  particles_clear();
  trail_clear();
  int idx = g_level.room ? g_level.room->index : 0;
  load_into_current(idx, INTRO_RESPAWN);
  ss->first_level = 0;
  ss->deaths_in_current_level = 0;
}

/* ---------------------------------------------------------------- transitions */
static void transition_to(int index, V2 dir) {
  int slot = g_level.room_slot ^ 1;
  ents_remove_room(slot);
  /* Player.CleanUpTriggers: the triggers the player is in are left; the room's triggers do nothing more (they go at the
   * transition's end in the game): they go now, which leaves their entity slots to the next room's (7A g-01 needs them) */
  for (int i = 0; i < g_nents; i++) {
    Ent *t = &g_ents[i];
    if (!t->cls || !(t->kind & KIND_TRIGGER) || !t->triggered) continue;
    t->triggered = 0;
    if (MORE(t->cls)->on_leave) MORE(t->cls)->on_leave(t, &g_player);
  }
  ents_remove_room_kind(g_level.room_slot, KIND_TRIGGER);
  uint8_t first = g_session.first_level;
  uint32_t deaths = g_session.deaths_in_current_level;
  g_session.first_level = 0;
  g_session.deaths_in_current_level = 0;
  if (!room_load(slot, index, INTRO_TRANSITION)) {
    g_session.first_level = first, g_session.deaths_in_current_level = deaths;
    return;
  }
  Room *old = g_level.room;
  g_level.room_slot = slot;
  g_level.room = &g_level.rooms[slot];
  need_textures();
  tiles_invalidate();   /* (the tiles at its edges with the other room) */
  ents_awake_new();
  cassette_level_start(true);
  Room *rm = g_level.room;
  g_level.transitioning = true;
  g_level.tr_dir = dir;
  g_level.tr_at = 0;
  g_level.tr_cam_from = g_level.cam;
  V2 pad = v2mul(dir, dir.y > 0 ? 12.f : 4.f);
  Ent *pe = g_player.ent;
  V2 to = v2(pe->x, pe->y);
  if (dir.x != 0)
    while (to.y >= rm->y + rm->h) to.y -= 1;
  for (int guard = 0; guard < 400; guard++) {   /* IsInBounds(playerTo, dirPad): the pad on the side entered */
    if (to.x >= rm->x + fmaxf(pad.x, 0) && to.y >= rm->y + fmaxf(pad.y, 0) && to.x < rm->x + rm->w - fmaxf(-pad.x, 0) &&
        to.y < rm->y + rm->h - fmaxf(-pad.y, 0))
      break;
    to = v2add(to, dir);
  }
  g_level.tr_player_to = to;
  V2 was = v2(pe->x, pe->y);
  pe->x = to.x, pe->y = to.y;
  g_level.cam_offset = v2(rds8(rm->info + RR_CAMX) * 48.f, rds8(rm->info + RR_CAMY) * 32.f);
  g_level.tr_cam_to = player_camera_target(&g_player);
  pe->x = was.x, pe->y = was.y;
  (void)old;
}

static void transition_update(void) {
  Ent *pe = g_player.ent;
  bool arrived = player_transition_to(&g_player, g_level.tr_player_to);
  g_level.tr_at = approach(g_level.tr_at, 1, DT / g_level.tr_duration);
  if (g_level.tr_at > 0.9f) g_level.cam = g_level.tr_cam_to;
  else g_level.cam = v2add(g_level.tr_cam_from, v2mul(v2sub(g_level.tr_cam_to, g_level.tr_cam_from), ease_cube_out(g_level.tr_at)));
  if (arrived && g_level.tr_at >= 1) {
    int old = g_level.room_slot ^ 1;
    ents_remove_room(old);
    blocks_free_tiles(old);
    tiles_invalidate();   /* (its tiles go from the cache, the edges change) */
    g_level.rooms[old].index = -1;
    Room *nr = g_level.room;   /* ClearRect(Bounds inflated by 16, inside: false) */
    particles_clear_outside((float)nr->x, (float)nr->y, (float)(nr->x + nr->w), (float)(nr->y + nr->h));
    V2 sp = player_spawn_near(v2(pe->x, pe->y));   /* Session.RespawnPoint = the closest spawn */
    g_session.rx = (int32_t)sp.x, g_session.ry = (int32_t)sp.y;
    player_on_transition(&g_player);
    g_level.cam_upward_max_y = g_level.cam.y + 180;
    g_level.tr_duration = 0.65f;
    g_level.transitioning = false;
    g_level.wind_pattern = g_level.room->info[RR_WIND];
    need_textures();
  }
}

/* Level.EnforceBounds */
void level_enforce_bounds(Player *p) {
  Ent *e = p->ent;
  Room *rm = g_level.room;
  if (g_level.transitioning) return;
  float bl = (float)rm->x, bt = (float)rm->y, br = (float)(rm->x + rm->w), bb = (float)(rm->y + rm->h);
  float cl = (float)(int)g_level.cam.x, ct = (float)(int)g_level.cam.y, cr = cl + 320, cb = ct + 180;
  int lock = g_level.cam_lock;
  if (lock == 2 && e_left(e) < cl) {   /* FinalBoss: the camera's edges hold */
    e->x += cl - e_left(e);
    player_on_bounds_h(p);
  } else if (e_left(e) < bl) {
    V2 at = v2(e_cxm(e) - 8, e_cym(e));
    int next = room_at(at);
    if (e_top(e) >= bt && e_bottom(e) < bb && next >= 0 && next != rm->index) {
      transition_to(next, v2(-1, 0));
      return;
    }
    e->x += bl - e_left(e);
    player_on_bounds_h(p);
  }
  if (lock == 2 && e_right(e) > cr && cr < br - 4) {
    e->x += cr - e_right(e);
    player_on_bounds_h(p);
  } else if (e_right(e) > br - 1 && theo_left_behind(p)) {
    e->x += (br - 1) - e_right(e);
  } else if (e_right(e) > br) {
    V2 at = v2(e_cxm(e) + 8, e_cym(e));
    int next = room_at(at);
    if (e_top(e) >= bt && e_bottom(e) < bb && next >= 0 && next != rm->index) {
      transition_to(next, v2(1, 0));
      return;
    }
    e->x += br - e_right(e);
    player_on_bounds_h(p);
  }
  if (lock && e_top(e) < ct) {
    e->y += ct - e_top(e);
    player_on_bounds_v(p);
  } else if (e_cym(e) < bt) {
    V2 at = v2(e_cxm(e), e_cym(e) - 12);
    int next = room_at(at);
    if (next >= 0 && next != rm->index) {
      player_before_up_transition(p);
      transition_to(next, v2(0, -1));
      return;
    }
    if (e_top(e) < bt - 24) {
      e->y += (bt - 24) - e_top(e);
      player_on_bounds_v(p);
    }
  }
  if (lock && cb < bb - 4 && e_top(e) > cb) {   /* below a locked camera: dead */
    player_die(p, v2(0, 0), false);
    return;
  }
  if (e_bottom(e) > bb) {
    V2 at = v2(e_cxm(e), e_cym(e) + 12);
    int next = room_at(at);
    bool down_ok = !(rm->info[RR_FLAGS] & 16);
    if (next >= 0 && next != rm->index && down_ok) {
      if (!collide_solid(e, e->x, e->y + 4)) {
        player_before_down_transition(p);
        transition_to(next, v2(0, 1));
      }
      return;
    }
  }
  if (e_top(e) > bb) player_die(p, v2(0, 0), false);
}

/* ---------------------------------------------------------------- update / render */
/* Level.UpdateTime: the save's time always, the session's once the player can move */
static void update_time(void) {
  if (g_level.in_credits || g_session.area == 8 || g_level.timer_stopped) return;
  save_add_time(1);
  if (!g_level.timer_started && !g_level.in_cutscene) {
    Player *p = level_player();
    int st = p ? p->state : 0;
    if (p && !p->dead && st != ST_SUMMITLAUNCH && !(st >= ST_INTROWALK && st <= ST_INTROWAKEUP) && st != ST_INTROTHINKFORABIT)
      g_level.timer_started = true;
  }
  if (!g_level.completed && g_level.timer_started) g_session.time++;
}

void level_camera_locker(Ent *e, int mode, float max_dx, float max_dy) {
  g_level.cam_lock = mode;
  g_level.cam_locker = e, g_level.cam_lock_dx = max_dx, g_level.cam_lock_dy = max_dy;
}

/* Level.LoadLevel into another room with an intro, at the end of the frame (OnEndOfFrame) */
static int pending_room = -1, pending_intro;
void level_load_room(int index, int intro) { level_load_room_near(index, intro, 0, 1); }
void level_load_room_near(int index, int intro, float fx, float fy) {
  pending_room = index, pending_intro = intro;
  spawn_fx = fx, spawn_fy = fy;
}

void level_flash(uint16_t color, bool draw_player_over) { level_flash_a(color, 1, draw_player_over); }
void level_flash_a(uint16_t color, float alpha, bool draw_player_over) {
  if (g_save.options & 2) return;   /* Photosensitive Mode */
  g_level.do_flash = true;
  g_level.flash_draw_player = draw_player_over;
  g_level.flash = 1;
  g_level.flash_color = color;
  g_level.flash_alpha = (uint8_t)(alpha * 255);
}

void level_update(void) {
  if (g_level.paused) {   /* only what updates while paused (Tags.PauseUpdate) */
    ents_update();
    ents_flush_removed();
    return;
  }
  /* CanPause: backspace (Input.Pause) */
  Player *pp = level_player();
  if (btn_pressed(&g_in.pause) && pp && !pp->dead && !g_level.pause_lock && !g_level.skipping_cutscene &&
      !g_level.transitioning && !g_wipe.active) {
    btn_consume(&g_in.pause);
    menu_pause();
    return;
  }
  if (g_level.freeze > 0) {
    g_level.freeze = fmaxf(g_level.freeze - RAW_DT, 0);   /* (Engine.FreezeTimer, in raw time) */
    return;
  }
  update_time();
  wipe_update();
  if (g_level.ending_delay > 0 && (g_level.ending_delay -= DT) <= 0) level_complete_area(false, false);
  g_level.prev_time_active = g_level.time_active;
  g_level.time_active += DT;
  g_level.raw_time_active += RAW_DT;
  if (g_level.shake_timer > 0) {   /* as many pixels as tenths of a second left, rounded up */
    if (level_on_raw_interval(0.04f)) {
      int n = (int)ceilf(g_level.shake_timer * 10);
      if (g_level.shake_dir.x == 0 && g_level.shake_dir.y == 0)
        g_level.shake_vec = v2((float)(rndi(n * 2 + 1) - n), (float)(rndi(n * 2 + 1) - n));
      else {
        g_level.last_dir_shake = (int8_t)(g_level.last_dir_shake ? -g_level.last_dir_shake : 1);
        g_level.shake_vec = v2mul(g_level.shake_dir, (float)(-g_level.last_dir_shake * n));
      }
    }
    g_level.shake_timer -= RAW_DT;
  } else
    g_level.shake_vec = v2(0, 0);
  if (g_level.do_flash) {
    g_level.flash = approach(g_level.flash, 1, DT * 10);
    if (g_level.flash >= 1) g_level.do_flash = false;
  } else if (g_level.flash > 0)
    g_level.flash = approach(g_level.flash, 0, DT * 3);
  if (g_level.transitioning) {
    transition_update();
    ents_update();   /* TransitionUpdate entities only */
  } else {
    if (g_level.in_space && pp) {   /* SpaceController: out of view at the top or bottom, back in at the other end */
      Ent *pe = pp->ent;
      float top = g_level.cam.y, bottom = top + 180;
      if (e_top(pe) > bottom + 12) pe->y += top - 4 - e_bottom(pe);
      else if (e_bottom(pe) < top - 4) pe->y += bottom + 12 - e_top(pe);
    }
    ents_update();
  }
  if (!g_level.frozen) {   /* !FrozenOrPaused: the wind's sway (Wire) */
    g_level.wind_sine_timer += DT;
    g_level.wind_sine = (sinf(g_level.wind_sine_timer) + 1) / 2;
  }
  player_after_update(&g_player);
  ents_flush_removed();
  ents_awake_new();
  style_update();
  if (pending_room >= 0) {
    int r = pending_room;
    pending_room = -1;
    g_session.has_respawn = 0;   /* the new room's own spawn */
    load_into_current(r, pending_intro);
    return;
  }
  if (g_level.dead_reload) {
    g_level.dead_reload = false;
    int golden = g_player.golden_room;
    if (golden >= 0) {   /* LevelExit.Mode.GoldenBerryRestart */
      session_restart(golden);
      level_start(INTRO_RESPAWN);
    } else
      level_reload();
  }
}

