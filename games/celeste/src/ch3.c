#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")   /* not drawn every frame: smaller over faster */
#endif
/* Celestial Resort (chapter 3), ported from the game's classes: the clutter
 * (ClutterBlockBase, ClutterBlock and its generator, ClutterSwitch,
 * ClutterDoor, ClutterCabinet, ClutterAbsorbEffect), resort lanterns,
 * cobwebs, trigger spikes (dust tendrils), doors, trapdoors, sinking and
 * moving platforms, clotheslines, water and waterfalls, Oshiro the boss
 * (AngryOshiro) and his door, the resort mirror, the PICO-8 console, keys
 * and lock blocks, block fields, the roof's end, kill boxes, and the light
 * and bloom triggers. The story: Oshiro (NPC03_Oshiro_*, OshiroSprite, OshiroLobbyBell) and his
 * cutscenes (CS03_OshiroLobby, Hallway1 and 2, Clutter, Breakdown, MasterSuite, Rooftop), Theo
 * (NPC03_Theo_Escaping and _Vents, CS03_TheoEscape), the memo, diary and guestbook, and the ending
 * (CS03_Ending). */
#include <stdlib.h>
#include "npc.h"
#include "talk.h"

/* the story's shared pieces (ch1.c, ch2.c) */
void ch1_talk_add(Ent *owner, Talk *t, int x, int y, int w, int h, V2 draw_at);
void ch1_talk_remove(Ent *owner);
int ch1_counter(const char *name);
void ch1_set_counter(const char *name, int v);
void ch1_flash(uint32_t color);
bool save_flag(int bit);
void save_set_flag(int bit);
bool textbox_portrait(int *bank, int *idle);
typedef struct { V2 target; float speed, alpha; int8_t turn; uint8_t step, flags, pad; } Move;
enum { MV_FADEIN = 1, MV_REMOVE = 2, MV_NOY = 4, MV_TURN = 8 };
bool ch1_move_to(Ent *e, Sprite *s, Move *m, float maxspeed, int move_anim, int idle_anim);
Ent *ch2_bdummy_new(V2 at);
void ch2_bdummy_appear(Ent *e);
void ch2_bdummy_vanish(Ent *e);
void ch2_bdummy_face(Ent *e, int f);
void ch2_bdummy_clip(Ent *e, float x0, float y0, float x1, float y1, float alpha);
enum { BD_TURN = 1, BD_NOFACE = 2, BD_FADELIGHT = 4, BD_QUICKEND = 8 };
bool ch2_bdummy_float_to(Ent *e, V2 target, int turn, int flags);
bool ch2_bdummy_walk_to(Ent *e, float x, float speed);
bool ch2_bdummy_smash(Ent *e, V2 target);
void ch2_interact_new(const EData *d, void (*on_talk)(Ent *e, Player *p));
typedef struct { V2 pos; float alpha, h; uint8_t out; } PageCtl;
Ent *ch2_page_new(bool memo);

/* ---------------------------------------------------------------- the story's helpers */
#define EV_BEGIN       \
  if (c->wait > 0) {   \
    c->wait -= DT;     \
    return false;      \
  }                    \
  switch (c->co) {     \
    case 0:
#define EV_YIELD_(n) \
  do {               \
    c->co = (n);     \
    return false;    \
    case (n):;       \
  } while (0)
#define EV_YIELD EV_YIELD_(__COUNTER__ + 1)
#define EV_WAIT(t)   \
  do {               \
    c->wait = (t);   \
    EV_YIELD;        \
  } while (0)
#define EV_NEST(call)      \
  do {                     \
    EV_YIELD;              \
    while (call) EV_YIELD; \
    EV_YIELD;              \
  } while (0)
#define EV_END \
  default:;    \
  }            \
  return true
static Co *ev_start(Co *c, int8_t *at, int index) {
  if (*at != index) *at = (int8_t)index, memset(c, 0, sizeof *c);
  return c;
}
static void draw_npc(const Sprite *spr, float x, float y) {   /* Scale.X turns it */
  Sprite s = *spr;
  s.flipx = s.sx < 0;
  s.sx = fabsf(s.sx);
  spr_draw(&s, x, y);
}
static void set_depth(Ent *e, int d) {
  if (e->depth != d) e->depth = d, ents_mark_unsorted();
}
static Ent *cs_new(const EntClass *cls, void (*on_end)(Ent *e, bool skipped), bool fade_in, bool ending) {
  Ent *e = ent_new(cls, 0, 0);
  if (!e) return NULL;
  e->collidable = 0, e->visible = 0;
  cutscene_start(e, on_end, fade_in, ending);
  return e;
}
static void player_free(void) {   /* StateMachine.Locked = false, State = 0 */
  Player *p = level_player();
  if (p) p->state_locked = false, player_set_state(p, ST_NORMAL);
}
static void player_hold(Player *p) {   /* State = 11, Locked */
  player_set_state(p, ST_DUMMY);
  p->state_locked = true;
}
/* CutsceneEntity.CameraTo / PanCamera's x only: from where the camera is, eased */
typedef struct { V2 from; float p; } Pan;
static bool pan_to(Pan *s, V2 to, float duration, float (*ease)(float), bool x_only) {
  if (s->p < 0) s->from = g_level.cam, s->p = 0;
  float k = ease(fminf(s->p, 1));
  g_level.cam.x = lerpf(s->from.x, to.x, k);
  if (!x_only) g_level.cam.y = lerpf(s->from.y, to.y, k);
  if (s->p >= 1) return false;
  s->p += DT / duration;
  return true;
}


/* ---------------------------------------------------------------- helpers */
static uint16_t tex(const char *p) { return res_tex_by_name(p); }
static float shake_val(void) {   /* Calc.Random.ShakeVector */
  static const int8_t o[5] = {-1, -1, 0, 1, 1};
  return o[rndi(5)];
}
static const Room *room_ptr(const Ent *e) { return &g_level.rooms[e->room]; }
static int16_t ent_ref(const Ent *e) { return e ? (int16_t)(e - g_ents) : -1; }
static Ent *ent_at(int16_t i, const EntClass *cls) { return i >= 0 && g_ents[i].cls == cls && g_ents[i].dead != 1 ? &g_ents[i] : NULL; }
static float quad_curve(float a, float c, float b, float t) { return (1 - t) * (1 - t) * a + 2 * (1 - t) * t * c + t * t * b; }
static V2 curve_point(V2 a, V2 b, V2 c, float t) { return v2(quad_curve(a.x, c.x, b.x, t), quad_curve(a.y, c.y, b.y, t)); }
static uint32_t hash32(uint32_t x) {
  x ^= x >> 16, x *= 0x7feb352du, x ^= x >> 15, x *= 0x846ca68bu, x ^= x >> 16;
  return x;
}
static float hashf(uint32_t x) { return (hash32(x) >> 8) * (1.f / 16777216.f); }

/* drawing straight into a strip (gfx_custom layers): view coordinates */
static inline void sp_plot(uint16_t *strip, int sy0, int sy1, int x, int y, uint16_t c, int a) {
  if (y < sy0 || y >= sy1 || (unsigned)x >= VIEW_W) return;
  uint16_t *d = strip + (y - sy0) * VIEW_W + x;
  *d = a >= 256 ? c : blend565(*d, scale565(c, a), a);
}
static void sp_rect(uint16_t *strip, int sy0, int sy1, int x, int y, int w, int h, uint16_t c, int a) {
  int x0 = x < 0 ? 0 : x, x1 = x + w > VIEW_W ? VIEW_W : x + w;
  int y0 = y < sy0 ? sy0 : y, y1 = y + h > sy1 ? sy1 : y + h;
  if (x0 >= x1 || y0 >= y1 || a <= 0) return;
  uint16_t pc = a >= 256 ? c : scale565(c, a);
  uint32_t ia = 256 - (uint32_t)a, pr = pc >> 11, pg = (pc >> 5) & 63, pb = pc & 31;
  for (int r = y0; r < y1; r++) {
    uint16_t *d = strip + (r - sy0) * VIEW_W + x0;
    if (a >= 256)
      for (int i = x0; i < x1; i++) *d++ = c;
    else   /* blend565, its color's channels worked out once (the clutter's backing covers most of 3A's busiest room) */
      for (int i = x0; i < x1; i++, d++) {
        uint32_t v = *d, cr = pr + (((v >> 11) * ia) >> 8), cg = pg + ((((v >> 5) & 63) * ia) >> 8), cb = pb + (((v & 31) * ia) >> 8);
        *d = (uint16_t)((cr > 31 ? 31 : cr) << 11 | (cg > 63 ? 63 : cg) << 5 | (cb > 31 ? 31 : cb));
      }
  }
}
static void sp_line(uint16_t *strip, int sy0, int sy1, int x0, int y0, int x1, int y1, uint16_t c, int a) {
  if ((y0 < sy0 && y1 < sy0) || (y0 >= sy1 && y1 >= sy1)) return;
  int dx = abs(x1 - x0), dy = -abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
  for (int guard = 0; guard < 2048; guard++) {
    sp_plot(strip, sy0, sy1, x0, y0, c, a);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) err += dy, x0 += sx;
    if (e2 <= dx) err += dx, y0 += sy;
  }
}
static inline int vx(float x) { return (int)floorf(x + 0.5f) - g_camx; }
static inline int vy(float y) { return (int)floorf(y + 0.5f) - g_camy; }
/* the file's memory (g_chram[3]): what other chapters' maps use first (the water's rows, the trigger spikes), then
 * the Celestial Resort's own (its clutter) */
enum { OFF_rowbuf = 0, END_rowbuf = OFF_rowbuf + (int)sizeof(uint8_t[256]) };
#define rowbuf (*(uint8_t(*)[256])(void *)(g_chram[3] + OFF_rowbuf))
#define TSPIKES 600
enum { OFF_ts_lerp = ((END_rowbuf + 7) & ~7), END_ts_lerp = OFF_ts_lerp + (int)sizeof(uint8_t[TSPIKES]) };
#define ts_lerp (*(uint8_t(*)[TSPIKES])(void *)(g_chram[3] + OFF_ts_lerp))
enum { OFF_ts_state = ((END_ts_lerp + 7) & ~7), END_ts_state = OFF_ts_state + (int)sizeof(uint8_t[TSPIKES]) };
#define ts_state (*(uint8_t(*)[TSPIKES])(void *)(g_chram[3] + OFF_ts_state))
static void tex_row_idx(const Tex *t, int r, uint8_t *out) { tex_row(t, r, out); }
static inline void sp_texel(uint16_t *d, const uint16_t *col, const uint8_t *al, uint8_t v, uint16_t tint, int ga, bool sil) {
  int aa = al[v - 1] + (al[v - 1] >> 7);
  uint16_t c = sil ? (aa >= 256 ? tint : scale565(tint, aa)) : tint != 0xFFFF ? mul565(col[v - 1], tint) : col[v - 1];
  if (ga < 256) c = scale565(c, ga), aa = (aa * ga) >> 8;
  *d = aa >= 256 ? c : blend565(*d, c, aa);
}
/* a texture's frame scaled by s about its frame point (ox, oy), placed at view (px, py) */
static void sp_tex_scaled(uint16_t *strip, int sy0, int sy1, uint16_t id, float px, float py, float ox, float oy, float sx, float sy,
                          uint16_t tint, int ga, bool sil) {
  Tex t;
  if (!tex_get(id, &t) || t.w > (int)sizeof rowbuf || sx == 0 || sy == 0) return;
  int n;
  const uint16_t *col = pal_colors(t.pal, &n);
  const uint8_t *al = pal_alpha(t.pal);
  float x0 = px + (t.ox - ox) * sx, x1 = px + (t.ox + t.w - ox) * sx, y0 = py + (t.oy - oy) * sy, y1 = py + (t.oy + t.h - oy) * sy;
  if (x0 > x1) { float k = x0; x0 = x1, x1 = k; }
  if (y0 > y1) { float k = y0; y0 = y1, y1 = k; }
  int Y0 = (int)floorf(y0), Y1 = (int)ceilf(y1), X0 = (int)floorf(x0), X1 = (int)ceilf(x1);
  if (Y0 < sy0) Y0 = sy0;
  if (Y1 > sy1) Y1 = sy1;
  if (X0 < 0) X0 = 0;
  if (X1 > VIEW_W) X1 = VIEW_W;
  int cached = -1;
  for (int Y = Y0; Y < Y1; Y++) {
    int v = (int)floorf((Y + 0.5f - py) / sy + oy) - t.oy;
    if (v < 0 || v >= t.h) continue;
    if (v != cached) tex_row_idx(&t, v, rowbuf), cached = v;
    uint16_t *row = strip + (Y - sy0) * VIEW_W;
    for (int X = X0; X < X1; X++) {
      int u = (int)floorf((X + 0.5f - px) / sx + ox) - t.ox;
      if (u < 0 || u >= t.w || !rowbuf[u]) continue;
      sp_texel(row + X, col, al, rowbuf[u], tint, ga, sil);
    }
  }
}
/* a texture's frame with its top-left at view (x, y) */
static void sp_tex(uint16_t *strip, int sy0, int sy1, uint16_t id, int x, int y, uint16_t tint, int ga, bool sil) {
  Tex t;
  if (!tex_get(id, &t) || t.w > (int)sizeof rowbuf) return;
  if (y + t.oy >= sy1 || y + t.oy + t.h <= sy0 || x + t.ox >= VIEW_W || x + t.ox + t.w <= 0) return;
  int n;
  const uint16_t *col = pal_colors(t.pal, &n);
  const uint8_t *al = pal_alpha(t.pal);
  for (int r = 0; r < t.h; r++) {
    int Y = y + t.oy + r;
    if (Y < sy0 || Y >= sy1) continue;
    tex_row_idx(&t, r, rowbuf);
    uint16_t *row = strip + (Y - sy0) * VIEW_W;
    for (int i = 0; i < t.w; i++) {
      int X = x + t.ox + i;
      if (!rowbuf[i] || (unsigned)X >= VIEW_W) continue;
      sp_texel(row + X, col, al, rowbuf[i], tint, ga, sil);
    }
  }
}
/* a texture a gfx_custom layer reads, loaded now and marked drawn this frame: nothing loads while the
 * strips are drawn (res.c), and what was drawn longest ago goes first */
static void tex_touch(uint16_t id) {
  Tex t;
  if (id != 0xFFFF) tex_get(id, &t);
}
/* layers drawn once per frame for every entity of a class at one depth */
static bool layer_once(uint32_t *mark) {
  if (*mark == g_frame + 1) return false;
  *mark = g_frame + 1;
  return true;
}

/* ---------------------------------------------------------------- the clutter */
/* ClutterBlockBase: the solid areas (one manager entity per room holds them, ClutterBlock: the pictures
 * filling them, made by ClutterBlockGenerator at the room's load. Kept in two small pools. */
enum { CL_RED, CL_GREEN, CL_YELLOW };
#define CLUT_BASES 240
#define CLUT_BLOCKS 960
typedef struct { uint8_t x, y, w, h, f; } ClutBase;            /* f: color | enabled << 2 | slot << 3 */
typedef struct { uint8_t x, y, tex, f, since; } ClutBlock;     /* tex 0xFF: gone; f: see CB_ */
enum { CB_COLOR = 3, CB_GROUND = 4, CB_TOP = 8, CB_LEFT = 16, CB_RIGHT = 32, CB_FLY = 64, CB_SLOT = 128 };
enum { OFF_clut_base = ((END_ts_state + 7) & ~7), END_clut_base = OFF_clut_base + (int)sizeof(ClutBase[CLUT_BASES]) };
#define clut_base (*(ClutBase(*)[CLUT_BASES])(void *)(g_chram[3] + OFF_clut_base))
enum { OFF_clut_blk = ((END_clut_base + 7) & ~7), END_clut_blk = OFF_clut_blk + (int)sizeof(ClutBlock[CLUT_BLOCKS]) };
#define clut_blk (*(ClutBlock(*)[CLUT_BLOCKS])(void *)(g_chram[3] + OFF_clut_blk))
static int nclut_base, nclut_blk;
static uint16_t clut_tex[3][15];
/* the texture sets of each color, biggest first: (columns, rows, first, count) */
static const uint8_t CLUT_SETS[5][4] = {{3, 3, 14, 1}, {2, 3, 12, 1}, {3, 2, 13, 1}, {2, 2, 6, 6}, {1, 1, 0, 6}};
static const uint8_t CLUT_W[15] = {1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 3, 3}, CLUT_H[15] = {1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 2, 3};
static const char *CLUT_NAMES[3] = {"Red", "Green", "Yellow"};
static void clut_flag(char *buf, int color) { path2(buf, "oshiro_clutter_cleared_", NULL, -1), buf[23] = (char)('0' + color), buf[24] = 0; }
static bool clut_enabled(int color) {
  char f[32];
  clut_flag(f, color);
  return !level_get_flag(f);
}
static void clut_load_tex(void) {
  static const char *n[3] = {"objects/resortclutter/red_", "objects/resortclutter/green_", "objects/resortclutter/yellow_"};
  char p[48];
  for (int c = 0; c < 3; c++)
    for (int i = 0; i < 15; i++) clut_tex[c][i] = tex(path2(p, n[c], NULL, i));
}
static void clut_drop_slot(int slot) {   /* a room goes away: its bases and blocks too */
  int j = 0;
  for (int i = 0; i < nclut_base; i++)
    if (((clut_base[i].f >> 3) & 1) != slot) clut_base[j++] = clut_base[i];
  nclut_base = j;
  j = 0;
  for (int i = 0; i < nclut_blk; i++)
    if (((clut_blk[i].f & CB_SLOT) ? 1 : 0) != slot) clut_blk[j++] = clut_blk[i];
  nclut_blk = j;
}

typedef struct { int16_t rx, ry; uint8_t slot, generated; float timer; } Clutter;
static const EntClass CLUTTER;
static Ent *clutter_of_slot(int slot) {
  for (int i = 0; i < g_nents; i++)
    if (g_ents[i].cls == &CLUTTER && g_ents[i].dead != 1 && ST(&g_ents[i], Clutter)->slot == slot) return &g_ents[i];
  return NULL;
}
static bool clutter_collide_rect(const Ent *e, float l, float t, float r, float b) {
  const Clutter *c = ST(e, Clutter);
  for (int i = 0; i < nclut_base; i++) {
    const ClutBase *k = &clut_base[i];
    if (((k->f >> 3) & 1) != c->slot || !(k->f & 4)) continue;
    float x = c->rx + k->x * 8.f, y = c->ry + k->y * 8.f;
    if (r > x && b > y && l < x + k->w * 8 && t < y + k->h * 8) return true;
  }
  return false;
}
/* ClutterBlockGenerator.Generate. No grid of the room (RAM): the cells are the areas' cells, numbered
 * area by area, with a bit each for "taken" */
#define CLUT_CELLS 2048
enum { OFF_clut_taken = ((END_clut_blk + 7) & ~7), END_clut_taken = OFF_clut_taken + (int)sizeof(uint8_t[CLUT_CELLS / 8]) };
#define clut_taken (*(uint8_t(*)[CLUT_CELLS / 8])(void *)(g_chram[3] + OFF_clut_taken))
enum { OFF_clut_off = ((END_clut_taken + 7) & ~7), END_clut_off = OFF_clut_off + (int)sizeof(uint16_t[CLUT_BASES + 1]) };
#define clut_off (*(uint16_t(*)[CLUT_BASES + 1])(void *)(g_chram[3] + OFF_clut_off))   /* the first cell of each area (of the room's slot) */
/* the area (an index in clut_base) holding a cell, the last one like the generator's grid; -1: none */
static int clut_area_at(int slot, int x, int y) {
  for (int i = nclut_base - 1; i >= 0; i--) {
    const ClutBase *k = &clut_base[i];
    if (((k->f >> 3) & 1) == slot && (k->f & 4) && x >= k->x && x < k->x + k->w && y >= k->y && y < k->y + k->h) return i;
  }
  return -1;
}
static int clut_cell(int area, int x, int y) {
  const ClutBase *k = &clut_base[area];
  return clut_off[area] + (y - k->y) * k->w + (x - k->x);
}
static bool clut_is_taken(int c) { return c < CLUT_CELLS && (clut_taken[c >> 3] & (1 << (c & 7))); }
static int clut_block_at(int slot, int x, int y) {   /* the block covering a cell, -1 if none */
  for (int i = 0; i < nclut_blk; i++) {
    const ClutBlock *b = &clut_blk[i];
    if (((b->f & CB_SLOT) ? 1 : 0) == slot && b->tex != 0xFF && x >= b->x && x < b->x + CLUT_W[b->tex] && y >= b->y &&
        y < b->y + CLUT_H[b->tex])
      return i;
  }
  return -1;
}
static void clutter_generate(Ent *e) {
  Clutter *c = ST(e, Clutter);
  const Room *rm = room_ptr(e);
  int slot = c->slot, cols = rm->w / 8, rows = rm->h / 8 + 1;
  int ncells = 0;
  for (int i = 0; i < nclut_base; i++) {
    clut_off[i] = (uint16_t)ncells;
    const ClutBase *k = &clut_base[i];
    if (((k->f >> 3) & 1) == slot && (k->f & 4)) ncells += k->w * k->h;
  }
  clut_off[nclut_base] = (uint16_t)ncells;
  if (ncells > CLUT_CELLS) ncells = CLUT_CELLS;
  memset(clut_taken, 0, sizeof clut_taken);
  /* active.Shuffle(): a shuffled order of the cells (a stride through them) */
  int stride = 1;
  if (ncells > 1) {
    stride = 1 + rndi(ncells - 1);
    while (1) {   /* coprime with the count */
      int a = stride, b = ncells;
      while (b) { int t = a % b; a = b, b = t; }
      if (a == 1) break;
      stride++;
    }
  }
  int start = ncells ? rndi(ncells) : 0;
  int first = nclut_blk;
  for (int n = 0; n < ncells; n++) {
    int cell = (start + n * stride) % ncells;
    if (clut_is_taken(cell)) continue;
    int area = 0;
    while (area < nclut_base && clut_off[area + 1] <= cell) area++;
    const ClutBase *k = &clut_base[area];
    int local = cell - clut_off[area], x = k->x + local % k->w, y = k->y + local / k->w, color = k->f & 3;
    if (clut_area_at(slot, x, y) != area) continue;   /* (overlapping areas: the last one has it) */
    int set = 0;
    for (; set < 5; set++) {
      int w = CLUT_SETS[set][0], h = CLUT_SETS[set][1];
      if (x + w > cols || y + h > rows) continue;
      bool ok = true;
      for (int i = x; i < x + w && ok; i++)
        for (int j = y; j < y + h && ok; j++) {
          int a = (i >= k->x && i < k->x + k->w && j >= k->y && j < k->y + k->h) ? area : clut_area_at(slot, i, j);
          if (a < 0 || (clut_base[a].f & 3) != color || clut_is_taken(clut_cell(a, i, j))) ok = false;
        }
      if (ok) break;
    }
    if (set >= 5 || nclut_blk >= CLUT_BLOCKS) continue;
    ClutBlock *b = &clut_blk[nclut_blk++];
    b->x = (uint8_t)x, b->y = (uint8_t)y;
    b->tex = (uint8_t)(CLUT_SETS[set][2] + rndi(CLUT_SETS[set][3]));
    b->f = (uint8_t)(color | (slot ? CB_SLOT : 0));
    b->since = 6;   /* floatDelay 0: floating from the start */
    for (int i = x; i < x + CLUT_SETS[set][0]; i++)
      for (int j = y; j < y + CLUT_SETS[set][1]; j++) {
        int a = clut_area_at(slot, i, j), cc = a < 0 ? CLUT_CELLS : clut_cell(a, i, j);
        if (cc < CLUT_CELLS) clut_taken[cc >> 3] |= (uint8_t)(1 << (cc & 7));
      }
  }
  /* the open sides, and what rests on a wall: tiles[,].Empty is neither a wall nor clutter */
#define WALL(cx_, cy_) (level_tile_type(0, rm->x / 8 + (cx_), rm->y / 8 + (cy_)) != '0')
#define EMPTY(cx_, cy_) (!WALL(cx_, cy_) && clut_area_at(slot, cx_, cy_) < 0)
  for (int i = first; i < nclut_blk; i++) {
    ClutBlock *b = &clut_blk[i];
    int w = CLUT_W[b->tex], h = CLUT_H[b->tex];
    for (int k = b->x; k < b->x + w; k++) {
      if (b->y == 0 || EMPTY(k, b->y - 1)) b->f |= CB_TOP;
      if (b->y + h - 1 < rows - 1 && WALL(k, b->y + h)) b->f |= CB_GROUND;
    }
    for (int l = b->y; l < b->y + h; l++) {
      if (b->x == 0 || EMPTY(b->x - 1, l)) b->f |= CB_LEFT;
      if (b->x + w - 1 == cols - 1 || EMPTY(b->x + w, l)) b->f |= CB_RIGHT;
    }
  }
#undef EMPTY
#undef WALL
  /* SetAboveToOnGround: from the bottom up, a block on one on the ground is too */
  for (int y = rows - 1; y >= 0; y--)
    for (int i = first; i < nclut_blk; i++) {
      ClutBlock *b = &clut_blk[i];
      int h = CLUT_H[b->tex];
      if (b->y + h - 1 != y || (b->f & CB_GROUND)) continue;
      for (int k = b->x; k < b->x + CLUT_W[b->tex]; k++) {
        int o = clut_block_at(slot, k, b->y + h);
        if (o >= 0 && (clut_blk[o].f & CB_GROUND)) {
          b->f |= CB_GROUND;
          break;
        }
      }
    }
}
static bool clut_mine(const Clutter *c, const ClutBlock *b) { return ((b->f & CB_SLOT) ? 1 : 0) == c->slot && b->tex != 0xFF; }
/* ClutterBlock.WeightDown, through the blocks below */
static void clut_weight_down(const Clutter *c, int i, int depth) {
  ClutBlock *b = &clut_blk[i];
  if (depth < 16) {
    int bx0 = b->x, bx1 = b->x + CLUT_W[b->tex], by = b->y + CLUT_H[b->tex];
    for (int j = 0; j < nclut_blk; j++) {
      ClutBlock *o = &clut_blk[j];
      if (j == i || !clut_mine(c, o) || o->y != by || (o->f & CB_FLY)) continue;
      if (o->x < bx1 && o->x + CLUT_W[o->tex] > bx0) clut_weight_down(c, j, depth + 1);
    }
  }
  b->since = 0;   /* floatTarget 0, floatDelay 0.1 */
}
static float clut_wave(const Clutter *c, const ClutBlock *b) {
  int x = c->rx + b->x * 8;
  return -(sinf((float)(x / 16) * 0.25f + c->timer * 2) + 1) / 2 - 1;
}
static float clut_float(const Clutter *c, const ClutBlock *b) {
  if (b->f & CB_GROUND) return 0;
  if (b->since < 6) return 0;
  return fmaxf(clut_wave(c, b), -4 * (b->since - 6) * DT);
}
static void clutter_update(Ent *e) {
  Clutter *c = ST(e, Clutter);
  Player *p = level_player();
  Ent *pe = p ? p->ent : NULL;
  c->timer += DT;
  for (int i = 0; i < nclut_blk; i++) {
    ClutBlock *b = &clut_blk[i];
    if (!clut_mine(c, b) || (b->f & (CB_GROUND | CB_FLY))) continue;
    if (b->since >= 6 && pe) {
      float l = c->rx + b->x * 8.f, t = c->ry + b->y * 8.f, r = l + CLUT_W[b->tex] * 8, bt = t + CLUT_H[b->tex] * 8;
      bool climb = p->state == ST_CLIMB;
      if (((b->f & CB_TOP) && e_right(pe) > l && e_left(pe) < r && e_bottom(pe) >= t - 1 && e_bottom(pe) <= t + 4) ||
          (climb && (b->f & CB_LEFT) && e_right(pe) >= l - 1 && e_right(pe) < l + 4 && e_bottom(pe) > t && e_top(pe) < bt) ||
          (climb && (b->f & CB_RIGHT) && e_left(pe) <= r + 1 && e_left(pe) > r - 4 && e_bottom(pe) > t && e_top(pe) < bt))
        clut_weight_down(c, i, 0);
    }
    if (b->since < 255) b->since++;
  }
}
static void clutter_awake(Ent *e) {
  Clutter *c = ST(e, Clutter);
  clut_load_tex();
  if (!c->generated) c->generated = 1, clutter_generate(e);
}
static void clutter_removed(Ent *e) { clut_drop_slot(ST(e, Clutter)->slot); }
/* ClutterBlockBase.Render: a dark rectangle (deeper), then the blocks in front of the player */
/* (the room's place and the camera are whole pixels: what is in the strip is found with integers first, a block's
 * float, at most 2 pixels up, only for those) */
static void clut_base_layer(uint16_t *strip, int sy0, int sy1, void *ctx) {
  Ent *e = ctx;
  Clutter *c = ST(e, Clutter);
  int ox = c->rx - g_camx, oy = c->ry - g_camy;
  for (int i = 0; i < nclut_base; i++) {
    ClutBase *k = &clut_base[i];
    if (((k->f >> 3) & 1) != c->slot) continue;
    bool on = k->f & 4;
    int y = oy + k->y * 8, h = k->h * 8 + (on ? 2 : 0);
    if (y >= sy1 || y + h <= sy0) continue;
    sp_rect(strip, sy0, sy1, ox + k->x * 8, y, k->w * 8, h, 0, on ? 179 : 77);
  }
}
/* the blocks, drawn as their pictures are (spans of the palette's colors: what sp_tex did a pixel at a time) */
static void clut_blk_layer(uint16_t *strip, int sy0, int sy1, void *ctx) {
  Ent *e = ctx;
  Clutter *c = ST(e, Clutter);
  int ox = c->rx - g_camx, oy = c->ry - g_camy;
  Tex tx[3 * 15];
  uint64_t got = 0;   /* tx[i] filled */
  for (int i = 0; i < nclut_blk; i++) {
    ClutBlock *b = &clut_blk[i];
    if (!clut_mine(c, b) || (b->f & CB_FLY)) continue;
    int y0 = oy + b->y * 8, x = ox + b->x * 8, h = CLUT_H[b->tex] * 8;
    if (y0 - 2 >= sy1 || y0 + h <= sy0 || x >= VIEW_W || x + CLUT_W[b->tex] * 8 <= 0) continue;
    int y = vy(c->ry + b->y * 8.f + clut_float(c, b));
    if (y >= sy1 || y + h <= sy0) continue;
    int ti = (b->f & CB_COLOR) * 15 + b->tex;
    if (!(got >> ti & 1)) {
      if (!tex_get(clut_tex[b->f & CB_COLOR][b->tex], &tx[ti])) continue;
      got |= (uint64_t)1 << ti;
    }
    const Tex *t = &tx[ti];
    if (t->fmt == TF_RAW4) sp_tex(strip, sy0, sy1, clut_tex[b->f & CB_COLOR][b->tex], x, y, 0xFFFF, 256, false);
    else blit_tex(strip, sy0, sy1, t, x + t->ox, y + t->oy, 0, 0xFFFF, 255);
  }
}
static void clutter_render(Ent *e) { gfx_custom(clut_base_layer, e, 0, VIEW_H); }
/* the blocks (depth -9998) are drawn by a second entity of the same room */
typedef struct { int16_t mgr; } ClutterFront;
static void clutfront_render(Ent *e) {
  Ent *m = ent_at(ST(e, ClutterFront)->mgr, &CLUTTER);
  if (!m) return;
  Clutter *c = ST(m, Clutter);
  for (int i = 0; i < nclut_blk; i++) {
    ClutBlock *b = &clut_blk[i];
    if (!clut_mine(c, b) || (b->f & CB_FLY)) continue;
    int x = vx(c->rx + b->x * 8.f), y = vy(c->ry + b->y * 8.f);
    if (y < VIEW_H + 8 && y + CLUT_H[b->tex] * 8 > -8 && x < VIEW_W && x + CLUT_W[b->tex] * 8 > 0) tex_touch(clut_tex[b->f & CB_COLOR][b->tex]);
  }
  gfx_custom(clut_blk_layer, m, 0, VIEW_H);
}
static const EntClass CLUTTERFRONT = {.name = "clutterBlocks", .size = sizeof(ClutterFront), .render = clutfront_render};
static const EntClass CLUTTER = {.name = "clutterBase", .size = sizeof(Clutter), .update = clutter_update,
                                 .render = clutter_render, .awake = clutter_awake, .removed = clutter_removed, .kind = KIND_SOLID,
                                 .more = &(const EntMore){.collide_rect = clutter_collide_rect}};
/* yellowBlocks / greenBlocks / redBlocks: ClutterBlockGenerator.Add */
static void new_clutter_area(const EData *d, int color, float w, float h) {
  int slot = g_level.room_slot;
  Ent *m = clutter_of_slot(slot);
  if (!m) {
    clut_drop_slot(slot);
    m = ent_new(&CLUTTER, 0, 0);
    if (!m) return;
    Clutter *c = ST(m, Clutter);
    c->slot = (uint8_t)slot;
    c->rx = (int16_t)d->rx, c->ry = (int16_t)d->ry;
    m->depth = 8999;
    m->ctype = COL_LIST;
    m->safe = 1;
    Ent *f = ent_new(&CLUTTERFRONT, 0, 0);
    if (f) f->depth = -9998, f->collidable = 0, ST(f, ClutterFront)->mgr = ent_ref(m);
  }
  if (nclut_base >= CLUT_BASES) return;
  ClutBase *k = &clut_base[nclut_base++];
  k->x = (uint8_t)((d->x - d->rx) / 8), k->y = (uint8_t)((d->y - d->ry) / 8);
  k->w = (uint8_t)(w / 8), k->h = (uint8_t)(h / 8);
  k->f = (uint8_t)(color | (clut_enabled(color) ? 4 : 0) | slot << 3);
}

/* ClutterCabinet */
typedef struct { Sprite spr; uint8_t opened; } Cabinet;
static void cabinet_update(Ent *e) { spr_update(&ST(e, Cabinet)->spr); }
static void cabinet_render(Ent *e) { spr_draw(&ST(e, Cabinet)->spr, e->x + 8, e->y + 8); }
static const EntClass CABINET = {.name = "clutterCabinet", .size = sizeof(Cabinet), .update = cabinet_update, .render = cabinet_render};
static void cabinet_open(Ent *e) {
  Cabinet *c = ST(e, Cabinet);
  spr_play(&c->spr, A_clutterCabinet_open, false);
  c->opened = 1;
}
static void new_cabinet(const EData *d) {
  Ent *e = ent_new(&CABINET, d->x, d->y);
  if (!e) return;
  e->depth = -10001;
  e->collidable = 0;
  Cabinet *c = ST(e, Cabinet);
  spr_init(&c->spr, SB_clutterCabinet);
  spr_play(&c->spr, A_clutterCabinet_idle, true);
}

/* ClutterAbsorbEffect: the clutter flies into the cabinets. The flying pictures are worked out from
 * hashes (a delay, a cabinet, a curve) rather than kept: absorbed blocks, then 125 more from offscreen */
typedef struct { V2 target; float t, extra_t; uint8_t color, closing, closed; int16_t ncab; } Absorb;
static const EntClass ABSORB;
static Ent *cabinet_n(int k) {
  for (int i = 0; i < g_nents; i++)
    if (g_ents[i].cls == &CABINET && g_ents[i].dead != 1 && k-- == 0) return &g_ents[i];
  return NULL;
}
/* where a flying picture is at time t since it started (its delay included); false when gone */
static bool fly_at(Absorb *a, uint32_t seed, V2 from, float delay, bool shake, float t, V2 *pos, float *scale, Ent **cab_out) {
  t -= delay;
  if (t < 0) {
    *pos = from, *scale = 1;
    return true;
  }
  if (!a->ncab) return false;
  Ent *cab = cabinet_n((int)(hash32(seed) % (uint32_t)a->ncab));
  if (!cab) return false;
  *cab_out = cab;
  V2 to = v2add(v2add(v2(cab->x, cab->y), v2(8, 8)), v2((float)(hash32(seed + 1) % 16) - 8, (float)(hash32(seed + 2) % 4) - 2));
  if (shake) {
    if (t < 0.25f) {
      *pos = v2add(from, v2((float)(hash32(seed + (uint32_t)(t * 60) * 7) % 3) - 1, (float)(hash32(seed + (uint32_t)(t * 60) * 13) % 3) - 1));
      *scale = 1;
      return true;
    }
    t -= 0.25f;
  }
  if (t >= 1) return false;
  V2 dir = v2norm(v2sub(to, from));
  float len = v2len(v2sub(to, from)), side = (hash32(seed + 3) & 1) ? -1 : 1;
  V2 ctrl = v2add(v2mul(v2add(to, from), 0.5f), v2mul(v2(-dir.y, dir.x), (len / 4 + hashf(seed + 4) * 40) * side));
  *pos = curve_point(from, to, ctrl, ease_cube_inout(t));
  *scale = ease_cube_inout(1 - t * 0.5f);
  if (t > 0.5f) *cab_out = cab;
  else *cab_out = NULL;
  return true;
}
static void absorb_layer(uint16_t *strip, int sy0, int sy1, void *ctx) {
  Ent *e = ctx;
  Absorb *a = ST(e, Absorb);
  for (int s = 0; s < 2; s++) {
    Ent *m = clutter_of_slot(s);
    if (!m) continue;
    Clutter *c = ST(m, Clutter);
    for (int i = 0; i < nclut_blk; i++) {
      ClutBlock *b = &clut_blk[i];
      if (!clut_mine(c, b) || !(b->f & CB_FLY) || (b->f & CB_COLOR) != a->color) continue;
      uint32_t seed = (uint32_t)i * 977u + 31u;
      V2 from = v2(c->rx + b->x * 8 + CLUT_W[b->tex] * 4.f, c->ry + b->y * 8 + CLUT_H[b->tex] * 4.f);
      V2 pos;
      float sc;
      Ent *cab;
      if (!fly_at(a, seed, from, hashf(seed + 9) * 0.5f, true, a->t, &pos, &sc, &cab)) continue;
      sp_tex_scaled(strip, sy0, sy1, clut_tex[a->color][b->tex], pos.x - g_camx, pos.y - g_camy, CLUT_W[b->tex] * 4.f,
                    CLUT_H[b->tex] * 4.f, sc, sc, 0xFFFF, 256, false);
    }
  }
  if (a->extra_t > 0)
    for (int k = 0; k < 125; k++) {
      float start = (k / 5) * 0.05f;
      if (a->extra_t < start) break;
      uint32_t seed = 0x51ED + (uint32_t)k * 131u;
      V2 from = v2add(a->target, angle_vec(hashf(seed + 5) * 2 * PI_F, 320));
      V2 pos;
      float sc;
      Ent *cab;
      if (!fly_at(a, seed, from, 0, false, a->extra_t - start, &pos, &sc, &cab)) continue;
      uint8_t ti = (uint8_t)(hash32(seed + 6) % 15);
      sp_tex_scaled(strip, sy0, sy1, clut_tex[a->color][ti], pos.x - g_camx, pos.y - g_camy, CLUT_W[ti] * 4.f, CLUT_H[ti] * 4.f, sc,
                    sc, 0xFFFF, 256, false);
    }
}
static void absorb_update(Ent *e) {
  Absorb *a = ST(e, Absorb);
  a->t += DT;
  if (a->extra_t > 0 || a->t >= 1.5f) a->extra_t += DT;
  /* the cabinets open when the first pictures are half way */
  if (a->t > 0.75f && !a->closing)
    for (int i = 0; i < g_nents; i++)
      if (g_ents[i].cls == &CABINET && g_ents[i].dead != 1 && !ST(&g_ents[i], Cabinet)->opened) cabinet_open(&g_ents[i]);
  if (level_on_interval(0.25f)) {   /* ClutterSwitch.P_ClutterFly along the way */
    int n = 0;
    for (int i = 0; i < g_nents && n < 3; i++)
      if (g_ents[i].cls == &CABINET && g_ents[i].dead != 1 && ST(&g_ents[i], Cabinet)->opened && rndi(4) == 0)
        particles_emit1(PL_FG, &P_ClutterSwitch_P_ClutterFly, v2(g_ents[i].x + 8, g_ents[i].y + 8), P_ClutterSwitch_P_ClutterFly.direction),
            n++;
  }
}
static void absorb_render(Ent *e) {
  for (int i = 0; i < 15; i++) tex_touch(clut_tex[ST(e, Absorb)->color][i]);
  gfx_custom(absorb_layer, e, 0, VIEW_H);
}
static const EntClass ABSORB = {.name = "clutterAbsorb", .size = sizeof(Absorb), .update = absorb_update, .render = absorb_render};
/* CloseCabinetsRoutine: in rows, left to right, three at a time */
static void absorb_close_step(Ent *e) {
  Absorb *a = ST(e, Absorb);
  int done = 0, total = 0;
  Ent *order[64];
  for (int i = 0; i < g_nents && total < 64; i++)
    if (g_ents[i].cls == &CABINET && g_ents[i].dead != 1) order[total++] = &g_ents[i];
  for (int i = 1; i < total; i++)
    for (int j = i; j > 0; j--) {
      Ent *p = order[j - 1], *q = order[j];
      int c = fabsf(p->y - q->y) < 24 ? (int)signf(p->x - q->x) : (int)signf(p->y - q->y);
      if (c <= 0) break;
      order[j - 1] = q, order[j] = p;
    }
  for (int i = 0; i < total; i++) {
    Cabinet *c = ST(order[i], Cabinet);
    if (!c->opened) {
      done++;
      continue;
    }
    spr_play(&c->spr, A_clutterCabinet_close, false);
    c->opened = 0;
    if (++done % 3 == 1) break;
  }
  if (done >= total) a->closed = 1;
}

/* ClutterSwitch */
typedef struct {
  Sprite spr;
  float start_y, at_y, speed_y, target_sx, wait, t, light_t, light_from;
  int16_t effect;
  uint8_t color, pressed, was_on_top, step, closing_wait;
} CSwitch;
static float area_darkness(void);
static uint16_t cswitch_icon[3];
static void cswitch_be_pressed(Ent *e) {
  CSwitch *s = ST(e, CSwitch);
  s->pressed = 1;
  s->at_y += 10;
  e->y += 10;
  spr_play(&s->spr, A_clutterSwitch_active, false);
}
static int cswitch_dashed(Ent *e, Player *p, V2 dir) {
  CSwitch *s = ST(e, CSwitch);
  if (!s->pressed && dir.x == 0 && dir.y == 1) {
    level_freeze(0.2f);
    char f[32];
    clut_flag(f, s->color);
    level_set_flag(f, true);
    level_set_flag("oshiro_clutter_door_open", false);
    level_dir_shake(v2(0, 1), 0.6f);
    particles_emit(PL_MID, &P_ClutterSwitch_P_Pressed, 20, v2(e_cxm(e), e_top(e) - 10), v2(16, 8), P_ClutterSwitch_P_Pressed.direction);
    cswitch_be_pressed(e);
    s->spr.sx = 1.5f;
    s->step = 1;   /* AbsorbRoutine */
    (void)p;
  }
  return DASH_NORMAL;
}
static void cswitch_absorb(Ent *e, CSwitch *s) {
  Player *p = level_player();
  if (s->wait > 0) {
    s->wait -= DT;
    return;
  }
  switch (s->step) {
    case 1: {
      if (p) player_set_state(p, ST_DUMMY);
      Ent *fx = ent_new(&ABSORB, 0, 0);
      if (fx) {
        fx->depth = -10001;
        fx->collidable = 0;
        fx->tags = TAG_TRANSITION_UPDATE;
        Absorb *a = ST(fx, Absorb);
        a->target = v2(e->x + e->cw / 2, e->y);
        a->color = s->color;
        int n = 0;
        for (int i = 0; i < g_nents; i++)
          if (g_ents[i].cls == &CABINET && g_ents[i].dead != 1) n++;
        a->ncab = (int16_t)n;
      }
      s->effect = ent_ref(fx);
      spr_play(&s->spr, A_clutterSwitch_break, false);
      g_session.lighting_alpha_add -= 0.05f;   /* the lights: down to 0.05 in 2 s, back up 3 s later */
      s->light_from = g_level.lighting, s->light_t = 0.0001f;
      for (int i = 0; i < nclut_blk; i++)   /* every ClutterBlock of the color: Absorb */
        if (clut_blk[i].tex != 0xFF && (clut_blk[i].f & CB_COLOR) == s->color) clut_blk[i].f |= CB_FLY;
      for (int i = 0; i < nclut_base; i++)   /* every ClutterBlockBase of the color: Deactivate */
        if ((clut_base[i].f & 3) == s->color) clut_base[i].f &= (uint8_t)~4;
      s->wait = 1.5f, s->step = 2;
      break;
    }
    case 2:
      if (p) player_set_state(p, ST_NORMAL);
      s->t = 0, s->step = 3;   /* the 125 flying in from offscreen (the effect draws them), shaking */
      /* fall through */
    case 3:
      if (level_on_interval(0.05f)) level_shake(0.3f);
      s->t += DT;
      if (s->t < 25 * 0.05f) break;
      s->wait = 1.5f, s->step = 4;
      break;
    case 4: {
      Ent *fx = ent_at(s->effect, &ABSORB);
      if (fx) ST(fx, Absorb)->closing = 1;
      s->step = 5;
    }
      /* fall through */
    case 5: {
      Ent *fx = ent_at(s->effect, &ABSORB);
      if (fx && !ST(fx, Absorb)->closed) {
        absorb_close_step(fx);
        s->wait = 0.1f;
        break;
      }
      s->wait = 0.2f + 0.3f, s->step = 6;
      break;
    }
    case 6:
      s->step = 0;
      break;
  }
}
static void cswitch_update(Ent *e) {
  CSwitch *s = ST(e, CSwitch);
  spr_update(&s->spr);
  plat_update(e);
  if (s->step) cswitch_absorb(e, s);
  if (s->light_t > 0) {   /* the Level.Lighting tweens (SineInOut, 2 s each) */
    float t = s->light_t += DT;
    if (t <= 2) g_level.lighting = lerpf(s->light_from, 0.05f, ease_sine_inout(t / 2));
    else if (t >= 3) {
      float k = fminf((t - 3) / 2, 1);
      g_level.lighting = lerpf(0.05f, area_darkness() + g_session.lighting_alpha_add, ease_sine_inout(k));
      if (k >= 1) s->light_t = 0;
    }
  }
  if (solid_has_player_on_top(e)) {
    if (s->speed_y < 0) s->speed_y = 0;
    s->speed_y = approach(s->speed_y, 70, 200 * DT);
    plat_move_towards_y(e, s->at_y + (s->pressed ? 2 : 4), s->speed_y * DT);
    s->target_sx = 1.2f;
    s->was_on_top = 1;
  } else {
    if (s->speed_y > 0) s->speed_y = 0;
    s->speed_y = approach(s->speed_y, -150, 200 * DT);
    plat_move_towards_y(e, s->at_y, -s->speed_y * DT);
    s->target_sx = 1;
    s->was_on_top = 0;
  }
  s->spr.sx = approach(s->spr.sx, s->target_sx, 0.8f * DT);
}
static void cswitch_render(Ent *e) {
  CSwitch *s = ST(e, CSwitch);
  Sprite sp = s->spr;
  spr_draw(&sp, e->x + 16, e->y + 16 + (s->pressed ? 2 : 0));
  if (!s->pressed) gfx_tex_ex(cswitch_icon[s->color], e->x + 16, e->y + 8, 8, 8, 1, 1, 0, 0xFFFF, 255, 0);
  light_add(e->x + e->cw / 2, e->y - 1, 0x00FFFF, 1, s->pressed ? 24 : 32, s->pressed ? 48 : 64);
}
static const EntClass CSWITCH = {.name = "colorSwitch", .size = sizeof(CSwitch), .update = cswitch_update,
                                 .render = cswitch_render, .kind = KIND_SOLID,
                                 .more = &(const EntMore){.on_dash_collide = cswitch_dashed}};
static int clut_color_of(const char *s) {
  for (int i = 0; i < 3; i++)
    if (!strcmp(s, CLUT_NAMES[i])) return i;
  return CL_GREEN;
}
static void new_cswitch(const EData *d) {
  Ent *e = ent_new(&CSWITCH, d->x, d->y);
  if (!e) return;
  ent_box(e, 32, 16, 0, 0);
  e->safe = 1;
  CSwitch *s = ST(e, CSwitch);
  s->color = (uint8_t)clut_color_of(EAS(d, colorSwitch, type));
  s->start_y = s->at_y = d->y;
  s->target_sx = 1;
  s->effect = -1;
  spr_init(&s->spr, SB_clutterSwitch);
  spr_play(&s->spr, A_clutterSwitch_idle, true);
  static const char *ic[3] = {"objects/resortclutter/icon_red", "objects/resortclutter/icon_green", "objects/resortclutter/icon_yellow"};
  for (int i = 0; i < 3; i++) cswitch_icon[i] = tex(ic[i]);
  if (!clut_enabled(s->color)) cswitch_be_pressed(e);
}

/* ClutterDoor and MrOshiroDoor: ghost doors */
typedef struct { Sprite spr; Wiggler wig; uint8_t color, oshiro; } GDoor;
static bool gdoor_locked(GDoor *g) {
  char f[32];
  clut_flag(f, g->color);
  if (level_get_flag("oshiro_clutter_door_open")) return level_get_flag(f);
  return true;
}
static int gdoor_dashed(Ent *e, Player *p, V2 dir) {
  (void)p, (void)dir;
  wiggler_start(&ST(e, GDoor)->wig, 0.6f, 3);
  return DASH_BOUNCE;
}
static void gdoor_update(Ent *e) {
  GDoor *g = ST(e, GDoor);
  if (!g->oshiro) {
    Ent *p = level_player_ent();
    bool in = p && collide_ent_at(e, e->x, e->y, p);
    if (g_level.transitioning && in) e->visible = e->collidable = 0;
    else if (!e->collidable && gdoor_locked(g) && !in) {
      e->visible = e->collidable = 1;
      wiggler_start(&g->wig, 0.6f, 3);
    }
  }
  spr_update(&g->spr);
  wiggler_update(&g->wig);
  plat_update(e);
}
static void gdoor_render(Ent *e) {
  GDoor *g = ST(e, GDoor);
  Sprite s = g->spr;
  s.sx = s.sy = 1 - g->wig.value * 0.2f;
  spr_draw(&s, e->x + e->cw / 2, e->y + e->ch / 2);
}
static const EntClass GDOOR = {.name = "clutterDoor", .size = sizeof(GDoor), .update = gdoor_update, .render = gdoor_render,
                               .kind = KIND_SOLID, .more = &(const EntMore){.on_dash_collide = gdoor_dashed}};
static void new_gdoor(const EData *d, bool oshiro) {
  Ent *e = ent_new(&GDOOR, d->x, d->y);
  if (!e) return;
  GDoor *g = ST(e, GDoor);
  g->oshiro = oshiro;
  spr_init(&g->spr, SB_ghost_door);
  spr_play(&g->spr, A_ghost_door_idle, true);
  if (oshiro) {   /* MrOshiroDoor */
    ent_box(e, 32, 32, 0, 0);
    e->visible = e->collidable = !level_get_flag("oshiro_resort_talked_1");
    return;
  }
  ent_box(e, EA(d, clutterDoor, width), EA(d, clutterDoor, height), 0, 0);
  e->tags = TAG_TRANSITION_UPDATE;
  g->color = (uint8_t)clut_color_of(EAS(d, clutterDoor, type));
  if (!gdoor_locked(g)) e->visible = e->collidable = 0;   /* InstantUnlock */
}

static bool is_gdoor(const Ent *e) { return e->cls == &GDOOR && e->dead != 1 && !ST(e, GDoor)->oshiro; }
static bool gdoor_locked_ent(Ent *e) { return gdoor_locked(ST(e, GDoor)); }
static void gdoor_instant_unlock(Ent *e) { e->visible = e->collidable = 0; }   /* InstantUnlock */
static void gdoor_open(Ent *e, bool instant) {   /* MrOshiroDoor.Open / InstantOpen */
  if (instant) e->visible = e->collidable = 0;
  else if (e->collidable) spr_play(&ST(e, GDoor)->spr, A_ghost_door_open, false), e->collidable = 0;
}
/* ClutterDoor.UnlockRoutine: the camera goes to it, it opens (*p starts at -1) */
static V2 gdoor_cam(Ent *e) {
  const Room *rm = g_level.room;
  return v2(clampf(e->x - 160, (float)rm->x, (float)(rm->x + rm->w - 320)), clampf(e->y - 90, (float)rm->y, (float)(rm->y + rm->h - 180)));
}
static bool gdoor_unlock(Ent *e, float *p, V2 *from) {
  if (*p < 0) {
    *from = g_level.cam;
    *p = v2len(v2sub(*from, gdoor_cam(e))) > 8 ? 0 : 10 + 0.2f;   /* (10 + the wait: no camera move) */
  }
  if (*p < 1) {
    g_level.cam = v2lerp(*from, gdoor_cam(e), ease_cube_inout(*p));
    *p += DT;
    return true;
  }
  if (*p > 10) {
    if ((*p -= DT) > 10) return true;
    *p = 1;
  }
  if (*p < 2) {   /* it opens */
    spr_play(&ST(e, GDoor)->spr, A_ghost_door_open, false);
    e->collidable = 0;
    *p = 2;
  }
  if (*p < 2.4f) {
    g_level.cam = gdoor_cam(e);
    *p += DT;
    return true;
  }
  return false;
}

/* ---------------------------------------------------------------- ResortLantern */
typedef struct { Wiggler wig; float collide_timer, alpha_timer, anim_t; int8_t mult, flip; uint8_t frame; } Lantern;
static uint16_t lantern_holder, lantern_tex[3];
static void lantern_on_player(Ent *e, Player *p) {
  Lantern *l = ST(e, Lantern);
  if (l->collide_timer <= 0) {
    if (p->speed.x != 0 || p->speed.y != 0) {
      l->collide_timer = 0.5f;
      l->mult = rndi(2) ? -1 : 1;
      wiggler_restart(&l->wig);
    }
  } else
    l->collide_timer = 0.5f;
}
static void lantern_awake(Ent *e) {
  if (point_solid(e->x + 8, e->y)) ST(e, Lantern)->flip = 1;
}
static void lantern_update(Ent *e) {
  Lantern *l = ST(e, Lantern);
  static const uint8_t SEQ[5] = {0, 0, 1, 2, 1};   /* AddLoop("light", "lantern", 0.3, 0, 0, 1, 2, 1) */
  l->anim_t += DT;
  if (l->anim_t >= 0.3f) l->anim_t -= 0.3f, l->frame = (uint8_t)((l->frame + 1) % 5);
  (void)SEQ;
  wiggler_update(&l->wig);
  if (l->collide_timer > 0) l->collide_timer -= DT;
  l->alpha_timer += DT;
}
static void lantern_render(Ent *e) {
  Lantern *l = ST(e, Lantern);
  static const uint8_t SEQ[5] = {0, 0, 1, 2, 1};
  float sx = l->flip ? -1.f : 1.f;
  gfx_tex_ex(lantern_holder, e->x, e->y, 8, 12, sx, 1, 0, 0xFFFF, 255, 0);
  float rot = l->wig.value * l->mult * (PI_F / 180) * 30;
  gfx_tex_ex(lantern_tex[SEQ[l->frame]], e->x - 1 + (l->flip ? 2 : 0), e->y - 5, 7, 7, sx, 1, rot, 0xFFFF, 255, 0);
  float a = 0.95f + sinf(l->alpha_timer) * 0.05f;
  light_add(e->x, e->y, 0xFFFFFF, a, 32, 64);
  bloom_add(e->x, e->y, a, 8);
}
static const EntClass LANTERN = {.name = "resortLantern", .size = sizeof(Lantern), .update = lantern_update, .render = lantern_render,
                                 .awake = lantern_awake, .on_player = lantern_on_player, .kind = KIND_PCOLLIDE};
static void new_lantern(const EData *d) {
  Ent *e = ent_new(&LANTERN, d->x, d->y);
  if (!e) return;
  ent_box(e, 8, 8, -4, -4);
  e->depth = 2000;
  Lantern *l = ST(e, Lantern);
  wiggler_init(&l->wig, 2.5f, 1.2f);
  l->wig.start_zero = true;
  l->mult = 1;
  lantern_holder = tex("objects/resortLantern/holder");
  char p[40];
  for (int i = 0; i < 3; i++) lantern_tex[i] = tex(path2(p, "objects/resortLantern/lantern", NULL, i));
}

/* ---------------------------------------------------------------- Cobweb */
/* all of a room's cobwebs in one strip layer (a dozen lines each) */
typedef struct { V2 a, b, off[4]; float ending[4], wave; uint32_t color, edge; uint8_t noff; } Cobweb;
static const EntClass COBWEB;
static void cobweb_draw(uint16_t *strip, int sy0, int sy1, Cobweb *c, V2 a, V2 b, int steps, bool offshoots) {
  V2 ctrl = v2add(v2mul(v2add(a, b), 0.5f), v2(0, 8 + sinf(c->wave) * 4));
  if (offshoots)
    for (int i = 0; i < c->noff; i++) cobweb_draw(strip, sy0, sy1, c, c->off[i], curve_point(a, b, ctrl, c->ending[i]), 4, false);
  V2 prev = a;
  for (int j = 1; j <= steps; j++) {
    V2 pt = curve_point(a, b, ctrl, (float)j / steps);
    uint32_t col = (j <= 2 || j >= steps - 1) ? c->edge : c->color;
    sp_line(strip, sy0, sy1, vx(prev.x), vy(prev.y), vx(pt.x), vy(pt.y), rgb(col), 256);
    prev = v2add(pt, v2norm(v2sub(prev, pt)));
  }
}
static void cobweb_layer(uint16_t *strip, int sy0, int sy1, void *ctx) {
  (void)ctx;
  for (int i = 0; i < g_nents; i++)
    if (g_ents[i].cls == &COBWEB && g_ents[i].dead != 1) {
      Cobweb *c = ST(&g_ents[i], Cobweb);
      cobweb_draw(strip, sy0, sy1, c, c->a, c->b, 12, true);
    }
}
static uint32_t cobweb_mark;
static void cobweb_render(Ent *e) {
  (void)e;
  if (layer_once(&cobweb_mark)) gfx_custom(cobweb_layer, NULL, 0, VIEW_H);
}
static void cobweb_update(Ent *e) { ST(e, Cobweb)->wave += DT; }
static void cobweb_awake(Ent *e) {   /* Added: anchored to solids at both ends, offshoots too */
  Cobweb *c = ST(e, Cobweb);
  if (!rect_solid((int)c->a.x - 2, (int)c->a.y - 2, (int)c->a.x + 2, (int)c->a.y + 2) ||
      !rect_solid((int)c->b.x - 2, (int)c->b.y - 2, (int)c->b.x + 2, (int)c->b.y + 2)) {
    ent_remove(e);
    return;
  }
  for (int i = 0; i < c->noff; i++)
    if (!rect_solid((int)c->off[i].x - 2, (int)c->off[i].y - 2, (int)c->off[i].x + 2, (int)c->off[i].y + 2)) {
      memmove(c->off + i, c->off + i + 1, sizeof(V2) * (size_t)(c->noff - i - 1));
      memmove(c->ending + i, c->ending + i + 1, sizeof(float) * (size_t)(c->noff - i - 1));
      c->noff--, i--;
    }
}
static const EntClass COBWEB = {.name = "cobweb", .size = sizeof(Cobweb), .update = cobweb_update, .render = cobweb_render,
                                .awake = cobweb_awake};
static void new_cobweb(const EData *d) {
  Ent *e = ent_new(&COBWEB, d->x, d->y);
  if (!e) return;
  e->depth = -1;
  e->collidable = 0;
  Cobweb *c = ST(e, Cobweb);
  c->a = v2(d->x, d->y);
  c->b = ed_node(d, 0);
  for (int i = 1; i < d->nnodes && c->noff < 4; i++) {
    c->off[c->noff] = ed_node(d, i);
    c->ending[c->noff++] = 0.3f + rndf() * 0.4f;
  }
  c->wave = rndf();
  /* AreaData.CobwebColor */
  static const uint32_t F[3] = {0x42c192, 0xaf36a8, 0x3474a6};
  c->color = g_session.area == 5 ? 0x9f2166 : g_session.area == 10 ? F[rndi(3)] : 0x696a6a;
  c->edge = 0;
  int r = (int)(((c->color >> 16) & 255) * 0.8f + 0x0f * 0.2f), g = (int)(((c->color >> 8) & 255) * 0.8f + 0x0e * 0.2f),
      b = (int)((c->color & 255) * 0.8f + 0x17 * 0.2f);
  c->edge = (uint32_t)(r << 16 | g << 8 | b);
}

/* ---------------------------------------------------------------- TriggerSpikes (dust tendrils) */
/* each tendril: Lerp (0..255) and DelayTimer (frames) | Triggered (128); kept in a pool, the rest from hashes */
static int nts;
typedef struct { int16_t first, n; uint8_t dir; } TSpikes;
enum { TS_UP, TS_DOWN, TS_LEFT, TS_RIGHT };
static uint16_t ts_tent_v[9], ts_tent_h[9], ts_dust[3];
static const EntClass TSPIKES_CLASS;
static V2 ts_outwards(int dir) { return dir == TS_UP ? v2(0, -1) : dir == TS_DOWN ? v2(0, 1) : dir == TS_LEFT ? v2(-1, 0) : v2(1, 0); }
static void ts_index_range(Ent *e, TSpikes *s, Player *p, int *mn, int *mx) {
  Ent *pe = p->ent;
  *mn = *mx = -1;
  switch (s->dir) {
    case TS_UP: if (p->speed.y >= 0) *mn = (int)((e_left(pe) - e_left(e)) / 4), *mx = (int)((e_right(pe) - e_left(e)) / 4); break;
    case TS_DOWN: if (p->speed.y <= 0) *mn = (int)((e_left(pe) - e_left(e)) / 4), *mx = (int)((e_right(pe) - e_left(e)) / 4); break;
    case TS_LEFT: if (p->speed.x >= 0) *mn = (int)((e_top(pe) - e_top(e)) / 4), *mx = (int)((e_bottom(pe) - e_top(e)) / 4); break;
    default: if (p->speed.x <= 0) *mn = (int)((e_top(pe) - e_top(e)) / 4), *mx = (int)((e_bottom(pe) - e_top(e)) / 4); break;
  }
}
static void ts_on_player(Ent *e, Player *p) {
  TSpikes *s = ST(e, TSpikes);
  int mn, mx;
  ts_index_range(e, s, p, &mn, &mx);
  if (mx < 0 || mn >= s->n) return;
  if (mn < 0) mn = 0;
  if (mx > s->n - 1) mx = s->n - 1;
  for (int i = mn; i <= mx; i++) {   /* SpikeInfo.OnPlayer */
    int k = s->first + i;
    if (!(ts_state[k] & 128)) {
      ts_state[k] = 128 | 24;   /* Triggered, DelayTimer 0.4 */
    } else if (ts_lerp[k] >= 255) {
      player_die(p, ts_outwards(s->dir), false);
      return;
    }
  }
}
/* LedgeBlocker(UpSafeBlockCheck / SideSafeBlockCheck): a tendril out (Lerp 1) a hop ahead (up), or beside her (sides) */
bool tspikes_ledge(const Ent *e, const Ent *pe, int facing) {
  if (e->cls != &TSPIKES_CLASS) return false;
  TSpikes *s = ST(e, TSpikes);
  int mn, mx;
  if (s->dir == TS_DOWN) return false;
  if (s->dir == TS_UP) mn = (int)((e_left(pe) + 8 * facing - e_left(e)) / 4), mx = (int)((e_right(pe) + 8 * facing - e_left(e)) / 4);
  else mn = (int)((e_top(pe) - e_top(e)) / 4), mx = (int)((e_bottom(pe) - e_top(e)) / 4);
  for (int i = mn < 0 ? 0 : mn; i <= mx && i < s->n; i++)
    if (ts_lerp[s->first + i] >= 255) return true;
  return false;
}
static bool ts_player_check(Ent *e, TSpikes *s, int i) {
  Player *p = level_player();
  if (!p || !collide_ent_at(e, e->x, e->y, p->ent)) return false;
  int mn, mx;
  ts_index_range(e, s, p, &mn, &mx);
  return mn <= i + 1 && mx >= i - 1;
}
static void ts_update(Ent *e) {
  TSpikes *s = ST(e, TSpikes);
  for (int i = 0; i < s->n; i++) {
    int k = s->first + i;
    uint8_t st = ts_state[k];
    if (st & 128) {
      int delay = st & 127;
      if (delay > 0) {
        if (--delay == 0 && ts_player_check(e, s, i)) delay = 3;   /* 0.05 more while the player is on it */
        ts_state[k] = (uint8_t)(128 | delay);
      } else
        ts_lerp[k] = (uint8_t)(ts_lerp[k] + 34 > 255 ? 255 : ts_lerp[k] + 34);   /* Approach(1, 8 * DT) */
    } else
      ts_lerp[k] = (uint8_t)(ts_lerp[k] > 17 ? ts_lerp[k] - 17 : 0);
  }
}
static bool ts_riding(Ent *e, Ent *pl) {
  TSpikes *s = ST(e, TSpikes);
  if (pl->kind & KIND_JUMPTHRU) return s->dir == TS_UP && collide_ent_at(e, e->x, e->y + 1, pl);
  float dx = s->dir == TS_LEFT ? 1 : s->dir == TS_RIGHT ? -1 : 0, dy = s->dir == TS_UP ? 1 : s->dir == TS_DOWN ? -1 : 0;
  return collide_ent_at(e, e->x + dx, e->y + dy, pl) && !collide_ent_at(e, e->x, e->y, pl);   /* CollideCheckOutside */
}
#define TS_REACH 24
static void ts_layer(uint16_t *strip, int sy0, int sy1, void *ctx) {
  (void)ctx;
  /* DustStyles: the edges' colors */
  static const uint32_t EDGE3[3] = {0xf25a10, 0xff0000, 0xf21067}, EDGE5[3] = {0x245ebb, 0x17a0ff, 0x17a0ff};
  const uint32_t *edges = g_session.area == 5 ? EDGE5 : EDGE3;
  for (int q = 0; q < g_nents; q++) {
    Ent *e = &g_ents[q];
    if (e->cls != &TSPIKES_CLASS || e->dead == 1) continue;
    TSpikes *s = ST(e, TSpikes);
    V2 along = (s->dir == TS_UP || s->dir == TS_DOWN) ? v2(1, 0) : v2(0, 1), out = ts_outwards(s->dir);
    bool vert = s->dir == TS_UP || s->dir == TS_DOWN;
    /* only the strip's: a tendril or its dust reaches at most TS_REACH pixels from its place */
    int ey0 = vy(e->y) - TS_REACH, ey1 = vy(e->y + (vert ? 0 : 2 + s->n * 4.f)) + TS_REACH;
    int ex0 = vx(e->x) - TS_REACH, ex1 = vx(e->x + (vert ? 2 + s->n * 4.f : 0)) + TS_REACH;
    if (ey1 < sy0 || ey0 >= sy1 || ex1 < 0 || ex0 >= VIEW_W) continue;
    for (int i = 0; i < s->n; i++) {
      int k = s->first + i;
      V2 at = v2add(v2(e->x, e->y), v2mul(along, 2 + i * 4.f));
      int ay = vy(at.y), ax = vx(at.x);
      if (ay + TS_REACH < sy0 || ay - TS_REACH >= sy1 || ax + TS_REACH < 0 || ax - TS_REACH >= VIEW_W) continue;
      uint32_t h = hash32((uint32_t)e->eid * 104729u + (uint32_t)i);   /* its own (Added's randoms): not from where it is, it rides blocks */
      if (!(ts_state[k] & 128)) {   /* the tendril, waving */
        int frame = (int)(fmodf((h % 900) / 100.f + g_level.time_active * 12, 9));
        uint16_t t = vert ? ts_tent_v[frame] : ts_tent_h[frame];
        uint32_t col = edges[(h >> 12) % 3];
        uint32_t tc = (uint32_t)((int)(((col >> 16) & 255) * 0.6f + 0x48 * 0.4f) << 16 | (int)(((col >> 8) & 255) * 0.6f + 0x3D * 0.4f) << 8 |
                                 (int)((col & 255) * 0.6f + 0x8B * 0.4f));
        float sx = s->dir == TS_LEFT ? -1 : 1, sy = s->dir == TS_UP ? -1 : 1;
        float ox = vert ? 3 : 0, oy = vert ? 0 : 3;   /* Justify (0.5, 0) or (0, 0.5) of 6x3 / 3x6 */
        sp_tex_scaled(strip, sy0, sy1, t, vx(at.x + along.x) + 0.f, vy(at.y + along.y) + 0.f, ox, oy, sx, sy, 0, 256, true);
        sp_tex_scaled(strip, sy0, sy1, t, vx(at.x) + 0.f, vy(at.y) + 0.f, ox, oy, sx, sy, rgb(tc), 256, true);
      } else if (ts_lerp[k] > 0) {   /* the dust (DustEdge: an edge of its color around it) */
        float lerp = ts_lerp[k] / 255.f;
        static const uint8_t OUT[3] = {3, 4, 6};
        V2 pos = v2add(at, v2mul(out, -4 + lerp * OUT[(h >> 4) % 3]));
        float sc = 0.5f * lerp;
        uint16_t t = ts_dust[(h >> 8) % 3];
        float px = pos.x - g_camx, py = pos.y - g_camy;
        uint16_t ec = rgb(edges[(h >> 16) % 3]);
        for (int o = 0; o < 4; o++)
          sp_tex_scaled(strip, sy0, sy1, t, px + (o == 0 ? -1 : o == 1 ? 1 : 0), py + (o == 2 ? -1 : o == 3 ? 1 : 0), 8, 8, sc, sc, ec, 256, true);
        sp_tex_scaled(strip, sy0, sy1, t, px, py, 8, 8, sc, sc, 0xFFFF, 256, false);
      }
    }
  }
}
static uint32_t ts_mark;
static void ts_render(Ent *e) {
  (void)e;
  if (!layer_once(&ts_mark)) return;
  bool v = false, h = false;
  for (int q = 0; q < g_nents; q++)
    if (g_ents[q].cls == &TSPIKES_CLASS && g_ents[q].dead != 1) {
      int d = ST(&g_ents[q], TSpikes)->dir;
      if (d == TS_UP || d == TS_DOWN) v = true;
      else h = true;
    }
  for (int i = 0; i < 9; i++) {
    if (v) tex_touch(ts_tent_v[i]);
    if (h) tex_touch(ts_tent_h[i]);
  }
  for (int i = 0; i < 3; i++) tex_touch(ts_dust[i]);
  gfx_custom(ts_layer, NULL, 0, VIEW_H);
}
static void ts_removed(Ent *e) {   /* frees its tendrils: the later ones move down */
  TSpikes *s = ST(e, TSpikes);
  int a = s->first, n = s->n;
  memmove(ts_lerp + a, ts_lerp + a + n, (size_t)(nts - a - n));
  memmove(ts_state + a, ts_state + a + n, (size_t)(nts - a - n));
  nts -= n;
  for (int i = 0; i < g_nents; i++)
    if (g_ents[i].cls == &TSPIKES_CLASS && &g_ents[i] != e && ST(&g_ents[i], TSpikes)->first > a) ST(&g_ents[i], TSpikes)->first -= (int16_t)n;
  s->n = 0;
}
static const EntClass TSPIKES_CLASS = {.name = "triggerSpikes", .size = sizeof(TSpikes), .update = ts_update, .render = ts_render,
                                       .removed = ts_removed, .on_player = ts_on_player, .kind = KIND_PCOLLIDE | KIND_STATICMOVER,
                                       .more = &(const EntMore){.sm_riding = ts_riding}};
static void new_tspikes(const EData *d, int dir, int size) {
  int n = size / 4;
  if (nts + n > TSPIKES) return;
  Ent *e = ent_new(&TSPIKES_CLASS, d->x, d->y);
  if (!e) return;
  e->depth = -50;
  switch (dir) {
    case TS_UP: ent_box(e, (float)size, 4, 0, -4); break;
    case TS_DOWN: ent_box(e, (float)size, 4, 0, 0); break;
    case TS_LEFT: ent_box(e, 4, (float)size, -4, 0); break;
    default: ent_box(e, 4, (float)size, 0, 0); break;
  }
  TSpikes *s = ST(e, TSpikes);
  s->dir = (uint8_t)dir;
  s->first = (int16_t)nts, s->n = (int16_t)n;
  memset(ts_lerp + nts, 0, (size_t)n);
  memset(ts_state + nts, 0, (size_t)n);
  nts += n;
  char p[48];
  for (int i = 0; i < 9; i++) {
    ts_tent_v[i] = tex(path2(p, "danger/triggertentacle/wiggle_v", NULL, i));
    ts_tent_h[i] = tex(path2(p, "danger/triggertentacle/wiggle_h", NULL, i));
  }
  for (int i = 0; i < 3; i++) ts_dust[i] = tex(path2(p, "danger/dustcreature/base", NULL, i));
}

/* ---------------------------------------------------------------- Door (an Actor) */
typedef struct { ActorExt ext; Sprite spr; uint8_t disabled, metal; } Door;
static bool door_riding(Ent *e, Ent *s) { return collide_rect(s, (int)e->x - 2, (int)e->y - 2, (int)e->x + 2, (int)e->y + 2); }
static void door_squish(Ent *e, Collision *c) { (void)e, (void)c; }
static int door_anim(const Door *d, int which) {   /* 0 idle, 1 open, 2 close (the sprites rooms ask for) */
  (void)which;
#ifdef SB_metaldoor
  if (d->metal) return which == 0 ? A_metaldoor_idle : which == 1 ? A_metaldoor_open : A_metaldoor_close;
#endif
#ifdef SB_door
  if (!d->metal) return which == 0 ? A_door_idle : which == 1 ? A_door_open : A_door_close;
#endif
  (void)d;
  return 0;
}
static void door_open(Ent *e, float x) {
  Door *d = ST(e, Door);
  int idle = door_anim(d, 0), open = door_anim(d, 1), close = door_anim(d, 2);
  if (spr_is(&d->spr, idle)) {
    spr_play(&d->spr, open, false);
    if (e->x != x) d->spr.sx = signf(x - e->x);
  } else if (spr_is(&d->spr, close))
    spr_play(&d->spr, close, true);
}
static void door_on_player(Ent *e, Player *p) {
  if (!ST(e, Door)->disabled) door_open(e, p->ent->x);
}
static void door_update(Ent *e) {
  Door *d = ST(e, Door);
  spr_update(&d->spr);
  actor_update_lift(e);
  if (!d->disabled && collide_solid(e, e->x, e->y)) d->disabled = 1;
}
static void door_render(Ent *e) { spr_draw(&ST(e, Door)->spr, e->x, e->y); }
static const EntClass DOOR = {.name = "door", .size = sizeof(Door), .update = door_update, .render = door_render,
                              .on_player = door_on_player, .kind = KIND_ACTOR | KIND_PCOLLIDE,
                              .more = &(const EntMore){.on_squish = door_squish, .is_riding_solid = door_riding}};
static void new_door(const EData *d) {
  Ent *e = ent_new(&DOOR, d->x, d->y);
  if (!e) return;
  e->depth = 8998;
  ent_box(e, 12, 22, -6, -23);
  Door *s = ST(e, Door);
  const char *t = EAS(d, door, type);
  s->metal = *t && strcmp(t, "wood");
  int bank = -1;
#ifdef SB_metaldoor
  if (s->metal) bank = SB_metaldoor;
#endif
#ifdef SB_door
  if (!s->metal) bank = SB_door;
#endif
  if (bank >= 0) spr_init(&s->spr, bank), spr_play(&s->spr, door_anim(s, 0), true);
  else s->spr.visible = 0;
}

/* ---------------------------------------------------------------- Trapdoor */
typedef struct { Sprite spr; float wait; uint8_t step; } Hatch;
static void hatch_on_player(Ent *e, Player *p) {
  Hatch *h = ST(e, Hatch);
  e->collidable = 0;
  if (p->speed.y >= 0) spr_play(&h->spr, A_trapdoor_open, false);
  else {   /* OpenFromBottom */
    h->spr.sy = -1;
    spr_play(&h->spr, A_trapdoor_open_partial, true);
    h->step = 1;
  }
}
static void hatch_update(Ent *e) {
  Hatch *h = ST(e, Hatch);
  spr_update(&h->spr);
  if (h->wait > 0) {
    h->wait -= DT;
    return;
  }
  switch (h->step) {
    case 1:   /* PlayRoutine("open_partial") */
      if (spr_is(&h->spr, A_trapdoor_open_partial)) break;
      h->wait = 0.1f, h->step = 2;
      break;
    case 2:
      h->spr.rate = -1;   /* (from its first frame: it ends at once) */
      spr_play(&h->spr, A_trapdoor_open_partial, true);
      h->step = 3;
      break;
    case 3:
      if (spr_is(&h->spr, A_trapdoor_open_partial)) break;
      h->spr.sy = 1, h->spr.rate = 1;
      spr_play(&h->spr, A_trapdoor_open, true);
      h->step = 0;
      break;
  }
}
static void hatch_render(Ent *e) { spr_draw(&ST(e, Hatch)->spr, e->x, e->y + 6); }
static const EntClass HATCH = {.name = "trapdoor", .size = sizeof(Hatch), .update = hatch_update, .render = hatch_render,
                               .on_player = hatch_on_player, .kind = KIND_PCOLLIDE};
static void new_hatch(const EData *d) {
  Ent *e = ent_new(&HATCH, d->x, d->y);
  if (!e) return;
  e->depth = 8999;
  ent_box(e, 24, 4, 0, 6);
  Hatch *h = ST(e, Hatch);
  spr_init(&h->spr, SB_trapdoor);
  spr_play(&h->spr, A_trapdoor_idle, true);
}

/* ---------------------------------------------------------------- SinkingPlatform, MovingPlatform */
static uint16_t wood_platform_tex(void) {
  return tex(g_session.area == 4 ? "objects/woodPlatform/cliffside" : "objects/woodPlatform/default");
}
static void platform_draw(uint16_t t, float x, float y, float w) {   /* the four 8 px pieces */
  gfx_tex_part(t, x, y, 0, 0, 8, 8, 0, 0xFFFF, 255);
  for (int i = 8; (float)i < w - 8; i += 8) gfx_tex_part(t, x + i, y, 8, 0, 8, 8, 0, 0xFFFF, 255);
  gfx_tex_part(t, x + w - 8, y, 24, 0, 8, 8, 0, 0xFFFF, 255);
  gfx_tex_part(t, x + w / 2 - 4, y, 16, 0, 8, 8, 0, 0xFFFF, 255);
}
typedef struct { float speed, start_y, rise_timer, shake_timer; int8_t shx, shy; uint16_t tex; } Sinking;
static void sinking_update(Ent *e) {
  Sinking *s = ST(e, Sinking);
  plat_update(e);
  if (s->shake_timer > 0) {   /* Shaker */
    if (level_on_interval(0.02f)) s->shx = (int8_t)shake_val(), s->shy = (int8_t)shake_val();
    if ((s->shake_timer -= DT) <= 0) s->shx = s->shy = 0;
  }
  Player *p = level_player();
  bool rider = p && jumpthru_has_player_rider(e);
  if (rider) {
    if (s->rise_timer <= 0) s->shake_timer = 0.15f;
    s->rise_timer = 0.1f;
    s->speed = approach(s->speed, player_ducking(p) ? 60 : 30, 400 * DT);
  } else if (s->rise_timer > 0) {
    s->rise_timer -= DT;
    s->speed = approach(s->speed, 45, 600 * DT);
  } else
    s->speed = approach(s->speed, -50, 400 * DT);
  if (s->speed > 0) plat_move_v(e, s->speed * DT);
  else if (s->speed < 0 && e->y + e->remy > s->start_y) {
    plat_move_towards_y(e, s->start_y, -s->speed * DT);
    if (e->y + e->remy <= s->start_y) s->shake_timer = 0.1f;
  }
}
static void sinking_render(Ent *e) {
  Sinking *s = ST(e, Sinking);
  platform_draw(s->tex, e->x + s->shx, e->y + s->shy, e->cw);
}
static const EntClass SINKING = {.name = "sinkingPlatform", .size = sizeof(Sinking), .update = sinking_update, .render = sinking_render,
                                 .kind = KIND_JUMPTHRU};
/* SinkingPlatformLine, MovingPlatformLine */
typedef struct { V2 end; float height; uint32_t edge, inner; uint8_t moving; } PLine;
static void pline_render(Ent *e) {
  PLine *l = ST(e, PLine);
  if (!l->moving) {
    gfx_rect(e->x - 1, e->y, 3, l->height, rgb(l->edge), 255);
    gfx_rect(e->x, e->y + 1, 1, l->height, rgb(l->inner), 255);
    return;
  }
  V2 d = v2norm(v2sub(l->end, v2(e->x, e->y))), n = v2(-d.y, d.x);
  V2 a = v2(e->x, e->y), b = l->end;
  gfx_line(a.x - d.x - n.x, a.y - d.y - n.y, b.x + d.x - n.x, b.y + d.y - n.y, rgb(l->edge), 255);
  gfx_line(a.x - d.x, a.y - d.y, b.x + d.x, b.y + d.y, rgb(l->edge), 255);
  gfx_line(a.x - d.x + n.x, a.y - d.y + n.y, b.x + d.x + n.x, b.y + d.y + n.y, rgb(l->edge), 255);
  gfx_line(a.x, a.y, b.x, b.y, rgb(l->inner), 255);
}
static const EntClass PLINE = {.name = "platformLine", .size = sizeof(PLine), .render = pline_render};
static void new_sinking(const EData *d) {
  float w = EA(d, sinkingPlatform, width);
  Ent *e = ent_new(&SINKING, d->x, d->y);
  if (!e) return;
  ent_box(e, w, 5, 0, 0);
  e->depth = 1;
  Sinking *s = ST(e, Sinking);
  s->start_y = d->y;
  s->tex = wood_platform_tex();
  Ent *l = ent_new(&PLINE, d->x + w / 2, d->y + 2.5f);
  if (l) {
    l->depth = 9001;
    l->collidable = 0;
    PLine *pl = ST(l, PLine);
    pl->height = g_level.rooms[g_level.room_slot].h - (l->y - d->ry);   /* Bounds.Height - (Y - Bounds.Y) */
    pl->edge = 0x2a1923, pl->inner = 0x160b12;
  }
}
typedef struct { V2 start, end; float add_y, sink_timer; Tween tw; uint8_t reverse; uint16_t tex; } Moving;
static void moving_update(Ent *e) {
  Moving *m = ST(e, Moving);
  bool end = tween_update(&m->tw);   /* the yoyo tween (SineInOut, 2 s) */
  float t = ease_sine_inout(m->reverse ? 1 - m->tw.percent : m->tw.percent);
  plat_move_to(e, lerpf(m->start.x, m->end.x, t), lerpf(m->start.y, m->end.y, t) + m->add_y);
  if (end) tween_start(&m->tw, 2), m->reverse = !m->reverse;
  plat_update(e);
  if (jumpthru_has_player_rider(e)) {
    m->sink_timer = 0.2f;
    m->add_y = approach(m->add_y, 3, 50 * DT);
  } else if (m->sink_timer > 0) {
    m->sink_timer -= DT;
    m->add_y = approach(m->add_y, 3, 50 * DT);
  } else
    m->add_y = approach(m->add_y, 0, 20 * DT);
}
static void moving_trigger(Ent *e, Ent *m) { (void)m, ST(e, Moving)->sink_timer = 0.4f; }
static void moving_render(Ent *e) { platform_draw(ST(e, Moving)->tex, e->x, e->y, e->cw); }
static void moving_awake(Ent *e) { plat_static_movers_attach(e); }
static const EntClass MOVING = {.name = "movingPlatform", .size = sizeof(Moving), .update = moving_update,
                                .render = moving_render, .awake = moving_awake, .kind = KIND_JUMPTHRU,
                                .more = &(const EntMore){.on_staticmover_trigger = moving_trigger}};
static void new_moving(const EData *d) {
  float w = EA(d, movingPlatform, width);
  Ent *e = ent_new(&MOVING, d->x, d->y);
  if (!e) return;
  ent_box(e, w, 5, 0, 0);
  Moving *m = ST(e, Moving);
  m->start = v2(d->x, d->y);
  m->end = ed_node(d, 0);
  m->tex = wood_platform_tex();
  tween_start(&m->tw, 2);
  V2 half = v2(w / 2, (5 + 4) / 2.f);
  Ent *l = ent_new(&PLINE, m->start.x + half.x, m->start.y + half.y);
  if (l) {
    l->depth = 9001;
    l->collidable = 0;
    PLine *pl = ST(l, PLine);
    pl->moving = 1;
    pl->end = v2add(m->end, half);
    if (g_session.area == 4) pl->edge = 0xa4464a, pl->inner = 0x86354e;
    else pl->edge = 0x2a1923, pl->inner = 0x160b12;
  }
}

/* ---------------------------------------------------------------- Clothesline (Flagline) */
/* all of a room's clotheslines in one strip layer: a rectangle per column of cloth */
typedef struct { V2 from, to; float wave; uint32_t seed; } Cloth;
static const EntClass CLOTHESLINE;
static const uint32_t CLOTH_COLORS[4] = {0x0d2e6b, 0x3d2688, 0x4f6e9d, 0x47194a};
static uint32_t cl_lerp(uint32_t a, uint32_t b, float t) {
  int r = (int)(((a >> 16) & 255) + (((int)((b >> 16) & 255) - (int)((a >> 16) & 255)) * t));
  int g = (int)(((a >> 8) & 255) + (((int)((b >> 8) & 255) - (int)((a >> 8) & 255)) * t));
  int bl = (int)((a & 255) + (((int)(b & 255) - (int)(a & 255)) * t));
  return (uint32_t)(r << 16 | g << 8 | bl);
}
/* Calc.Random.Next(min, max) of the cloths, from the line's seed: color, height, length, step */
static int cloth_val(uint32_t seed, int j, int k, int lo, int hi) { return lo + (int)(hash32(seed + (uint32_t)(j * 4 + k)) % (uint32_t)(hi - lo)); }
static void flagline_draw(uint16_t *strip, int sy0, int sy1, V2 from, V2 to, float wave, uint32_t seed, const uint32_t *colors, int ncol,
                          uint32_t line, uint32_t pin, int minh, int maxh, int minl, int maxl, int mins, int maxs, float droop) {
  V2 a = from.x < to.x ? from : to, b = from.x < to.x ? to : from;
  float num = v2len(v2sub(a, b)), num2 = num / 8;
  V2 ctrl = v2add(v2mul(v2add(a, b), 0.5f), v2(0, num2 + sinf(wave) * num2 * 0.3f));
  V2 p4 = a;
  float t = 0;
  int k = 0;
  bool flag = false;
  uint16_t lc = rgb(line), pc = rgb(pin);
  for (int guard = 0; t < 1 && guard < 400; guard++) {
    int j = k % 10;
    int length = cloth_val(seed, j, 2, minl, maxl), step = cloth_val(seed, j, 3, mins, maxs);
    t += (float)(flag ? length : step) / num;
    V2 p5 = curve_point(a, b, ctrl, t);
    sp_line(strip, sy0, sy1, vx(p4.x), vy(p4.y), vx(p5.x), vy(p5.y), lc, 256);
    if (t < 1 && flag) {
      int col = cloth_val(seed, j, 0, 0, ncol), height = cloth_val(seed, j, 1, minh, maxh);
      float n5 = length * droop;
      V2 c2 = v2add(v2mul(v2add(p4, p5), 0.5f), v2(0, n5 + sinf(wave * 2 + t) * n5 * 0.4f));
      V2 p6 = p4;
      uint16_t cc = rgb(colors[col]), hc = rgb(cl_lerp(colors[col], 0xFFFFFF, 0.1f));
      for (float n6 = 1; n6 <= (float)length; n6 += 1) {
        V2 pt = curve_point(p4, p5, c2, n6 / length);
        if (pt.x != p6.x) {
          sp_rect(strip, sy0, sy1, vx(p6.x), vy(p6.y), (int)floorf(pt.x - p6.x + 1 + 0.5f), height, cc, 256);
          p6 = pt;
        }
      }
      sp_rect(strip, sy0, sy1, vx(p4.x), vy(p4.y), 1, height, hc, 256);
      sp_rect(strip, sy0, sy1, vx(p5.x), vy(p5.y), 1, height, hc, 256);
      sp_rect(strip, sy0, sy1, vx(p4.x), vy(p4.y - 1), 1, 3, pc, 256);
      sp_rect(strip, sy0, sy1, vx(p5.x), vy(p5.y - 1), 1, 3, pc, 256);
      k++;
    }
    p4 = p5;
    flag = !flag;
  }
}
static void cloth_layer(uint16_t *strip, int sy0, int sy1, void *ctx) {
  (void)ctx;
  for (int i = 0; i < g_nents; i++)
    if (g_ents[i].cls == &CLOTHESLINE && g_ents[i].dead != 1) {
      Cloth *c = ST(&g_ents[i], Cloth);
      /* lineColor = Color.Lerp(Gray, DarkBlue, 0.25), pinColor = Gray */
      flagline_draw(strip, sy0, sy1, c->from, c->to, c->wave, c->seed, CLOTH_COLORS, 4, 0x606082, 0x808080, 8, 20, 8, 16, 2, 8, 0.6f);
    }
}
static uint32_t cloth_mark;
static void cloth_render(Ent *e) {
  (void)e;
  if (layer_once(&cloth_mark)) gfx_custom(cloth_layer, NULL, 0, VIEW_H);
}
static void cloth_update(Ent *e) { ST(e, Cloth)->wave += DT; }
static const EntClass CLOTHESLINE = {.name = "clothesline", .size = sizeof(Cloth), .update = cloth_update, .render = cloth_render};
static void new_clothesline(const EData *d) {
  Ent *e = ent_new(&CLOTHESLINE, d->x, d->y);
  if (!e) return;
  e->depth = 8999;
  e->collidable = 0;
  Cloth *c = ST(e, Cloth);
  c->from = v2(d->x, d->y);
  c->to = ed_node(d, 0);
  c->wave = rndf() * 2 * PI_F;
  c->seed = (uint32_t)rnd_next(&g_rnd);
}

/* ---------------------------------------------------------------- Water */
#define WATER_RIPPLES 6
typedef struct { float pos, speed, height, percent; } Ripple;
typedef struct {
  float timer[2];
  Ripple rip[2][WATER_RIPPLES];
  uint8_t nrip[2];
  uint8_t top, bottom, player_in, tension_on;
  float tension_pos, tension_str;
  uint32_t ray_seed;
} Water;
static const EntClass WATER;
/* Water.Surface; k: 0 top, 1 bottom */
static float water_surface_height(const Ent *e, const Water *w, int k, float position) {
  float W = e->cw;
  if (position < 0 || position > W) return 0;
  float num = 0;
  for (int i = 0; i < w->nrip[k]; i++) {
    const Ripple *r = &w->rip[k][i];
    float d = fabsf(r->pos - position);
    float v = d < 12 ? clamped_map(d, 0, 16, 1, -0.75f) : clamped_map(d, 16, 32, -0.75f, 0);
    num += v * r->height * ease_cube_in(1 - r->percent);
  }
  num = clampf(num, -4, 4);
  if (k == 1 && w->tension_on) num += ease_cube_out(clamped_map(fabsf(w->tension_pos - position), 0, 24, 1, 0)) * w->tension_str * 12;
  float val = position / W;
  num *= clamped_map(val, 0, 0.1f, 0.5f, 1);
  num *= clamped_map(val, 0.9f, 1, 1, 0.5f);
  num += sinf(w->timer[k] + position * 0.1f);
  return num + 6;
}
static void water_ripple(Ent *e, Water *w, int k, float x, float mult) {   /* Surface.DoRipple */
  float W = e->cw, dur = 3;
  float along = clampf(x - e->x, 0, W);
  if (W < 200) dur *= clamped_map(W, 0, 200, 0.25f, 1), mult *= clamped_map(W, 0, 200, 0.5f, 1);
  for (int s = -1; s <= 1; s += 2) {
    if (w->nrip[k] >= WATER_RIPPLES) {   /* full: the oldest goes */
      memmove(&w->rip[k][0], &w->rip[k][1], sizeof(Ripple) * (WATER_RIPPLES - 1));
      w->nrip[k]--;
    }
    w->rip[k][w->nrip[k]++] = (Ripple){along, s * 80.f, 2 * mult, 0};
  }
  (void)dur;
  /* the duration only depends on the width: kept out of the record */
}
static float water_ripple_duration(const Ent *e) { return e->cw < 200 ? 3 * clamped_map(e->cw, 0, 200, 0.25f, 1) : 3; }
static void water_update(Ent *e) {
  Water *w = ST(e, Water);
  float W = e->cw, dur = water_ripple_duration(e);
  for (int k = 0; k < 2; k++) {
    if (!(k ? w->bottom : w->top)) continue;
    w->timer[k] += DT;
    for (int i = w->nrip[k] - 1; i >= 0; i--) {
      Ripple *r = &w->rip[k][i];
      if (r->percent > 1) {
        memmove(r, r + 1, sizeof(Ripple) * (size_t)(w->nrip[k] - i - 1));
        w->nrip[k]--;
        continue;
      }
      r->pos += r->speed * DT;
      if (r->pos < 0 || r->pos > W) r->speed = -r->speed, r->pos = clampf(r->pos, 0, W);
      r->percent += DT / dur;
    }
  }
  /* the player's WaterInteraction */
  Player *p = level_player();
  if (p) {
    Ent *pe = p->ent;
    bool in = collide_ent_at(e, e->x, e->y, pe);
    if (in != w->player_in) {
      V2 c = e_center(pe);
      if (c.y <= e_cym(e) && w->top) water_ripple(e, w, 0, c.x, 1);
      else if (c.y > e_cym(e) && w->bottom) water_ripple(e, w, 1, c.x, 1);
      w->player_in = in;
    }
    if (w->bottom) {
      if (in && pe->y > e_bottom(e) - 8) {
        w->tension_on = 1;
        w->tension_pos = pe->x - e->x;
        w->tension_str = clamped_map(pe->y, e_bottom(e) - 8, e_bottom(e) + 4, 0, 1);
      } else
        w->tension_on = 0;
    }
  }
}
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC push_options
#pragma GCC optimize("O2")   /* drawn every frame */
#endif
/* blend565(d, pc, 256 - ia) where pc is a color scaled by that alpha (scale565): no field can go past its top, so
 * the same pixels without its clamps, and inlined (the water blends thousands a frame) */
static inline __attribute__((always_inline)) uint16_t water_blend(uint16_t d, uint16_t pc, uint32_t ia) {
  uint32_t r = (uint32_t)(pc >> 11) + (((uint32_t)(d >> 11) * ia) >> 8);
  uint32_t g = ((pc >> 5) & 63u) + ((((d >> 5) & 63u) * ia) >> 8);
  uint32_t b = (pc & 31u) + (((d & 31u) * ia) >> 8);
  return (uint16_t)(r << 11 | g << 5 | b);
}
/* a span of view columns [X0, X1) on view row Y, blended with c at alpha a (0..255) */
static void water_span(uint16_t *strip, int sy0, int sy1, int Y, int X0, int X1, uint16_t c, int a) {
  if (Y < sy0 || Y >= sy1) return;
  if (X0 < 0) X0 = 0;
  if (X1 > VIEW_W) X1 = VIEW_W;
  uint16_t pc = scale565(c, a), *d = strip + (Y - sy0) * VIEW_W;
  uint32_t ia = 256u - (uint32_t)a;
  for (int X = X0; X < X1; X++) d[X] = water_blend(d[X], pc, ia);
}
/* a column of rows [y0, y1) (world, centers inside) at view column X, blended with pc (a color scaled by 256 - ia) */
static void water_column(uint16_t *strip, int sy0, int sy1, int X, float y0, float y1, uint16_t pc, uint32_t ia) {
  int Y0 = (int)ceilf(y0 - 0.5f) - g_camy, Y1 = (int)ceilf(y1 - 0.5f) - g_camy;
  if (Y0 < sy0) Y0 = sy0;
  if (Y1 > sy1) Y1 = sy1;
  for (uint16_t *d = strip + (Y0 - sy0) * VIEW_W + X; Y0 < Y1; Y0++, d += VIEW_W) *d = water_blend(*d, pc, ia);
}
#if NP_SOFT_FLOAT
/* No FPU (the TI-Nspire): each light ray worked out once a frame, not once a strip (the same rays; the lake of
   chapter 6 spent a sixth of its frames on them) */
typedef struct {
  float depth, a, top;
  int32_t al0, al1, sk;   /* in 65536ths, at row d's middle t = d + 1/2: alpha al0 - al1 t, edges x9, x10 - sk t */
  int64_t x9, x10;
} WRay;
#define WRAYS 320
static const WRay *water_rays(const Ent *e, const Water *w, int k, float base, float out, int nrays) {
  static struct { const Ent *e; uint32_t frame; WRay r[WRAYS]; } cache[2][2];   /* two waters, top and bottom */
  int slot = cache[0][k].e == e ? 0 : cache[1][k].e == e ? 1 : cache[0][k].frame == g_frame ? 1 : 0;
  if (cache[slot][k].e == e && cache[slot][k].frame == g_frame) return cache[slot][k].r;
  cache[slot][k].e = e, cache[slot][k].frame = g_frame;
  float W = e->cw, H = e->ch;
  for (int r = 0; r < nrays; r++) {
    WRay *o = &cache[slot][k].r[r];
    uint32_t h = hash32(w->ray_seed + (uint32_t)r * 2654435761u);
    float duration = 2 + hashf(h) * 6;
    float t = w->timer[k] / duration + hashf(h + 1);
    uint32_t cycle = (uint32_t)floorf(t);
    float pct = t - cycle;
    uint32_t hc = hash32(h + cycle * 7919u);
    float rpos = hashf(hc) * W, rw = (float)(2 + hash32(hc + 1) % 14), len = 8 + hashf(hc + 2) * 120;
    float a = pct < 0.1f ? clamped_map(pct, 0, 0.1f, 0, 1) : pct > 0.9f ? clamped_map(pct, 0.9f, 1, 1, 0) : 1;
    float n9 = fmaxf(0, rpos - rw / 2), n10 = fminf(W, rpos + rw / 2);
    float depth = fminf(H, 0.7f * len), skew = 0.3f * len;
    o->a = n10 <= n9 || depth <= 0 || a <= 0 ? 0 : a;   /* (0: not drawn) */
    if (!o->a) continue;
    o->depth = depth, o->top = base + out * water_surface_height(e, w, k, (n9 + n10) / 2);
    float A = 0.6f * a * 256 * 65536;
    o->al0 = (int32_t)A, o->al1 = (int32_t)(A / depth);
    o->x9 = (int64_t)((e->x + n9 + 0.5f) * 65536), o->x10 = (int64_t)((e->x + n10 + 0.5f) * 65536);
    o->sk = (int32_t)(skew / depth * 65536);
  }
  return cache[slot][k].r;
}
#endif
/* the fill, the light rays and the surfaces, like the surfaces' meshes. A strip draws only what crosses its rows, and
 * the surfaces' heights at the mesh's points in view once */
static void water_layer(uint16_t *strip, int sy0, int sy1, void *ctx) {
  Ent *e = ctx;
  Water *w = ST(e, Water);
  float W = e->cw, H = e->ch;
  uint16_t sky = rgb(0x87CEFA);
  int fill_a = 77, surf_a = 204;   /* LightSkyBlue * 0.3, * 0.8 */
  uint16_t fill_c = scale565(sky, fill_a), surf_c = scale565(sky, surf_a);
  /* the fill between the surfaces' bases */
  float ft = e->y + (w->top ? 8 : 0), fb = e->y + H - (w->bottom ? 8 : 0);
  int iw = (int)W, X0 = vx(e->x);
  for (int Y = vy(ft) > sy0 ? vy(ft) : sy0, Y1 = vy(fb) < sy1 ? vy(fb) : sy1; Y < Y1; Y++)
    water_span(strip, sy0, sy1, Y, X0, X0 + iw, sky, fill_a);
  /* the columns in view: X = X0 + x */
  int xa = X0 < 0 ? -X0 : 0, xb = VIEW_W - X0 < iw ? VIEW_W - X0 : iw;
  for (int k = 0; k < 2; k++) {
    if (!(k ? w->bottom : w->top)) continue;
    float base = k ? e->y + H - 8 : e->y + 8, out = k ? 1.f : -1.f;
    int bv = vy(base);
    /* a surface is 1 to 24 px from its base (water_surface_height: 6, +-4 of ripples, +12 of tension, +-1 of wave) */
    if (xa < xb && bv + 26 >= sy0 && bv - 26 < sy1) {
      /* surface heights at the mesh's points, every 4 px (point m at min(4 m, W)) */
#if NP_SOFT_FLOAT
      int m0 = xa / 4, m1 = (xb - 1) / 4 + 1;
      /* (no FPU: the heights worked out once a frame, for the strips the surface crosses) */
      static float hps[2][2][VIEW_W / 4 + 3];
      static struct { const Ent *e; uint32_t frame; int m0, m1; } at[2][2];
      int slot = at[0][k].e == e && at[0][k].frame == g_frame ? 0 : at[1][k].e == e && at[1][k].frame == g_frame ? 1
                 : at[0][k].frame == g_frame ? 1 : 0;
      float *hp = hps[slot][k];
      if (at[slot][k].e != e || at[slot][k].frame != g_frame || at[slot][k].m0 != m0 || at[slot][k].m1 != m1) {
        at[slot][k].e = e, at[slot][k].frame = g_frame, at[slot][k].m0 = m0, at[slot][k].m1 = m1;
        for (int m = m0; m <= m1; m++) hp[m - m0] = water_surface_height(e, w, k, (float)(4 * m < iw ? 4 * m : iw));
      }
#else
      float hp[VIEW_W / 4 + 3];
      int m0 = xa / 4, m1 = (xb - 1) / 4 + 1;
      for (int m = m0; m <= m1; m++) hp[m - m0] = water_surface_height(e, w, k, (float)(4 * m < iw ? 4 * m : iw));
#endif
      for (int x = xa; x < xb; x++) {
        int x0 = x / 4 * 4, x1 = x0 + 4 < iw ? x0 + 4 : iw;
        float h0 = hp[x / 4 - m0], h1 = hp[x / 4 + 1 - m0];
        float h = x1 > x0 ? lerpf(h0, h1, (x + 0.5f - x0) / (x1 - x0)) : h0;
        /* fill from the base to the surface, then the surface line, a pixel further out */
        float s = base + out * h, s2 = base + out * (h + 1);
        water_column(strip, sy0, sy1, X0 + x, fminf(base, s), fmaxf(base, s), fill_c, 256 - fill_a);
        water_column(strip, sy0, sy1, X0 + x, fminf(s, s2), fmaxf(s, s2), surf_c, 256 - surf_a);
      }
    }
    /* the rays (Surface.Rays, from hashes: each its own cycle) */
    int nrays = (int)(W * 0.2f);
#if NP_SOFT_FLOAT
    if (nrays <= WRAYS) {
      const WRay *ray = water_rays(e, w, k, base, out, nrays);
      for (int r = 0; r < nrays; r++, ray++) {
        if (ray->a <= 0) continue;
        /* row d is vy(top - out (d + 0.5) - 0.5) = yt - out d (out is 1 or -1): the strip's rows alone */
        int nd = (int)ray->depth, yt = (int)floorf(ray->top - out * 0.5f) - g_camy, d0, d1;
        if (out < 0) d0 = sy0 - yt, d1 = sy1 - yt;
        else d0 = yt - sy1 + 1, d1 = yt - sy0 + 1;
        if (d0 < 0) d0 = 0;
        if (d1 > nd) d1 = nd;
        for (int d = d0; d < d1; d++) {
          int Y = yt - (out < 0 ? -d : d);
          int64_t t2 = 2 * d + 1;   /* (2 t) */
          int32_t alpha = (int32_t)((ray->al0 - ((ray->al1 * t2) >> 1)) >> 16);
          if (alpha <= 0) continue;
          int64_t sk = (ray->sk * t2) >> 1;
          water_span(strip, sy0, sy1, Y, (int)((ray->x9 - sk) >> 16) - g_camx, (int)((ray->x10 - sk) >> 16) - g_camx, sky, alpha);
        }
      }
      continue;
    }
#endif
    for (int r = 0; r < nrays; r++) {
      uint32_t h = hash32(w->ray_seed + (uint32_t)r * 2654435761u);
      float duration = 2 + hashf(h) * 6;
      float t = w->timer[k] / duration + hashf(h + 1);
      uint32_t cycle = (uint32_t)floorf(t);
      float pct = t - cycle;
      uint32_t hc = hash32(h + cycle * 7919u);
      float rpos = hashf(hc) * W, rw = (float)(2 + hash32(hc + 1) % 14), len = 8 + hashf(hc + 2) * 120;
      float a = pct < 0.1f ? clamped_map(pct, 0, 0.1f, 0, 1) : pct > 0.9f ? clamped_map(pct, 0.9f, 1, 1, 0) : 1;
      float n9 = fmaxf(0, rpos - rw / 2), n10 = fminf(W, rpos + rw / 2);
      float depth = fminf(H, 0.7f * len), skew = 0.3f * len;
      if (n10 <= n9 || depth <= 0 || a <= 0) continue;
      /* row d is about bv - out * d, give or take the surface's 24 px: only the rows of this strip */
      int nd = (int)depth, d0, d1;
      if (out < 0) d0 = sy0 - bv - 2, d1 = sy1 - bv + 26;
      else d0 = bv - sy1 - 2, d1 = bv - sy0 + 26;
      if (d0 < 0) d0 = 0;
      if (d1 > nd) d1 = nd;
      if (d0 >= d1) continue;
      float top = base + out * water_surface_height(e, w, k, (n9 + n10) / 2);
      for (int d = d0; d < d1; d++) {
        int Y = vy(top - out * (d + 0.5f) - 0.5f);
        if (Y < sy0 || Y >= sy1) continue;
        float k2 = (d + 0.5f) / depth;
        int alpha = (int)(0.6f * a * (1 - k2) * 256);
        if (alpha <= 0) continue;
        water_span(strip, sy0, sy1, Y, vx(e->x + n9 - skew * k2), vx(e->x + n10 - skew * k2), sky, alpha);
      }
    }
  }
}
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC pop_options
#endif
static void water_render(Ent *e) {
  float t = e->y - 8 - g_level.cam.y;
  gfx_custom(water_layer, e, (int)t, (int)(t + e->ch + 16));
}
static const EntClass WATER = {.name = "water", .size = sizeof(Water), .update = water_update, .render = water_render, .kind = KIND_WATER};
static void new_water(const EData *d) {
  Ent *e = ent_new(&WATER, d->x, d->y);
  if (!e) return;
  e->tags = TAG_TRANSITION_UPDATE;
  e->depth = -9999;
  ent_box(e, EA(d, water, width), EA(d, water, height), 0, 0);
  Water *w = ST(e, Water);
  w->top = 1;
  w->bottom = EAB(d, water, hasBottom);
  w->ray_seed = (uint32_t)rnd_next(&g_rnd);
}

/* ---------------------------------------------------------------- WaterFall */
typedef struct { float height; int16_t water; uint8_t solid; } Fall;
static void fall_awake(Ent *e) {
  Fall *f = ST(e, Fall);
  const Room *rm = room_ptr(e);
  f->height = 8;
  f->water = -1;
  for (;;) {
    float y = e->y + f->height;
    if (!(y < rm->y + rm->h)) break;
    Ent *w = NULL;
    for (int i = 0; i < g_nents && !w; i++)
      if (g_ents[i].cls == &WATER && g_ents[i].dead != 1 && collide_rect(&g_ents[i], (int)e->x, (int)y, (int)e->x + 8, (int)y + 8)) w = &g_ents[i];
    if (w) {
      f->water = ent_ref(w);
      break;
    }
    bool blocked = false;   /* a solid there that blocks waterfalls (Solid.BlockWaterfalls) */
    for (int i = 0; i < g_nents && !blocked; i++) {
      Ent *o = &g_ents[i];
      blocked = (o->kind & KIND_SOLID) && o->cls && o->collidable && o->dead != 1 && solid_blocks_waterfalls(o) &&
                collide_rect(o, (int)e->x, (int)y, (int)e->x + 8, (int)y + 8);
    }
    if (blocked) {
      f->solid = 1;
      break;
    }
    f->height += 8;
  }
}
static void fall_update(Ent *e) {
  Fall *f = ST(e, Fall);
  Ent *w = ent_at(f->water, &WATER);
  if (w && level_on_interval(0.3f) && ST(w, Water)->top) water_ripple(w, ST(w, Water), 0, e->x + 4, 0.75f);
  if (w || f->solid)
    particles_emit(PL_FG, &P_Water_P_Splash, 1, v2(e->x + 4, e->y + f->height + 2), v2(8, 2), -PI_F / 2);
}
static void fall_render(Ent *e) {
  Fall *f = ST(e, Fall);
  uint16_t sky = rgb(0x87CEFA);
  Ent *w = ent_at(f->water, &WATER);
  if (!w || !ST(w, Water)->top) {
    gfx_rect(e->x + 1, e->y, 6, f->height, sky, 77);
    gfx_rect(e->x - 1, e->y, 2, f->height, sky, 204);
    gfx_rect(e->x + 7, e->y, 2, f->height, sky, 204);
    return;
  }
  Water *wa = ST(w, Water);
  float num = f->height + (w->y + 8) - w->y;
  for (int i = 0; i < 6; i++)
    gfx_rect(e->x + i + 1, e->y, 1, num - water_surface_height(w, wa, 0, e->x + 1 + i - w->x), sky, 77);
  gfx_rect(e->x - 1, e->y, 2, num - water_surface_height(w, wa, 0, e->x - w->x), sky, 204);
  gfx_rect(e->x + 7, e->y, 2, num - water_surface_height(w, wa, 0, e->x + 8 - w->x), sky, 204);
}
static const EntClass FALL = {.name = "waterfall", .size = sizeof(Fall), .update = fall_update, .render = fall_render, .awake = fall_awake};
static void new_waterfall(const EData *d) {
  Ent *e = ent_new(&FALL, d->x, d->y);
  if (!e) return;
  e->depth = -9999;
  e->tags = TAG_TRANSITION_UPDATE;
  e->collidable = 0;
}

/* ---------------------------------------------------------------- AngryOshiro (friendlyGhost) */
enum { OS_CHASE, OS_CHARGE, OS_ATTACK, OS_DUMMY, OS_WAITING, OS_HURT };
/* its sprites (oshiro_boss, oshiro_boss_lightning) as tables of the frames the boss plays (smaller than
 * two Sprites in its state); an animation loads when first drawn, the room loads only "idle" */
enum { OA_RESPAWN, OA_IDLE, OA_CHARGE, OA_DASH, OA_HURT, OA_LIGHTNING, OA_BACK, OA_NONE = 255 };
/* transformBack (CS03_Ending): boss38-53, 53 four times more, 53-67 */
static const struct { uint8_t first, n, loop, next; float delay; } OANIM[7] = {
    {0, 13, 0, OA_IDLE, 0.06f}, {13, 8, 1, OA_IDLE, 0.09f}, {21, 13, 0, OA_DASH, 0.08f},
    {34, 4, 1, OA_DASH, 0.06f}, {68, 5, 0, OA_NONE, 0.04f}, {0, 7, 0, OA_NONE, 0.03f}, {38, 35, 0, OA_NONE, 0.06f}};
static int oback(int f) { return f < 16 ? 38 + f : f < 20 ? 53 : 33 + f; }
typedef struct { uint8_t anim, last, frame; float timer; } OSpr;   /* anim OA_NONE: ended on its last frame */
static uint16_t osh_boss[73], osh_light[7];
static void ospr_play(OSpr *s, int a, bool restart) {
  if (s->anim == a && !restart) return;
  s->anim = s->last = (uint8_t)a, s->frame = 0, s->timer = 0;
}
static void ospr_update(OSpr *s) {
  if (s->anim == OA_NONE) return;
  s->timer += DT;
  if (s->timer < OANIM[s->anim].delay) return;
  s->timer -= OANIM[s->anim].delay;
  if (++s->frame < OANIM[s->anim].n) return;
  if (OANIM[s->anim].loop) s->frame = 0;
  else if (OANIM[s->anim].next != OA_NONE) s->anim = s->last = OANIM[s->anim].next, s->frame = 0;
  else s->frame = (uint8_t)(OANIM[s->anim].n - 1), s->anim = OA_NONE;
}
static uint16_t ospr_tex(const OSpr *s) {
  int a = s->anim != OA_NONE ? s->anim : s->last;
  return a == OA_LIGHTNING ? osh_light[s->frame] : a == OA_BACK ? osh_boss[oback(s->frame)] : osh_boss[OANIM[a].first + s->frame];
}
typedef struct {
  OSpr spr, lightning;
  float sx, sy, rot;
  float cam_x_off, y_speed, attack_speed, shake_timer, co_wait, col_x;
  int8_t shx, shy;
  uint8_t st, co_step, attack_index, lightning_on, ease_back, respawn_anim, leaving, from_cutscene;
} Osh;
static const EntClass OSHIRO;
static float osh_target_y(Ent *e) {
  Player *p = level_player();
  if (!p) return e->y;
  const Room *rm = room_ptr(e);
  return clampf(e_cym(p->ent), rm->y + 8.f, rm->y + rm->h - 8.f);
}
static float osh_cy(Ent *e) { return e->y + 4; }   /* Circle(14) at (3, 4) */
static void osh_set_cy(Ent *e, float cy) { e->y = cy - 4; }
static void osh_box(Ent *e) {   /* covers the Circle and the bounce Hitbox(28, 6, -11, -11) */
  Osh *o = ST(e, Osh);
  float l = fminf(o->col_x - 14, -11), r = fmaxf(o->col_x + 14, 17);
  ent_box(e, r - l, 29, l, -11);
}
static void osh_state(Ent *e, Osh *o, int st) {   /* StateMachine.State */
  if (o->st == st) return;
  int prev = o->st;
  o->st = (uint8_t)st;
  if (prev == OS_CHARGE) o->shx = o->shy = 0;   /* ChargeUpEnd: Sprite.Position = 0 */
  switch (st) {   /* begins */
    case OS_CHASE: ospr_play(&o->spr, OA_IDLE, false); break;
    case OS_ATTACK: o->attack_speed = 0, level_dir_shake(v2(1, 0), 0.3f); break;
    case OS_HURT: ospr_play(&o->spr, OA_HURT, true); break;
  }
  o->co_step = 0, o->co_wait = 0;   /* the state's coroutine starts over */
}
static void osh_on_player(Ent *e, Player *p) {
  Osh *o = ST(e, Osh);
  Ent *pe = p->ent;
  /* bounce collider first */
  if (collide_rect(pe, e->x - 11, e->y - 11, e->x + 17, e->y - 5) && o->st == OS_ATTACK && e_bottom(pe) <= e->y + o->col_x * 0 + (4 - 14) + 6) {
    level_freeze(0.2f);
    player_bounce(p, e->y - 10 + 2);
    osh_state(e, o, OS_HURT);
    return;
  }
  /* the Circle */
  float cx = e->x + o->col_x, cy = osh_cy(e);
  float nx = clampf(cx, e_left(pe), e_right(pe)), ny = clampf(cy, e_top(pe), e_bottom(pe));
  if ((cx - nx) * (cx - nx) + (cy - ny) * (cy - ny) >= 14 * 14) return;
  if (o->st != OS_HURT && (cx < e_cxm(pe) + 4 || o->spr.anim != OA_RESPAWN)) {
    V2 d = v2sub(e_center(pe), v2(cx, cy));
    player_die(p, v2len(d) > 0 ? v2norm(d) : v2(1, 0), false);
  }
}
static void osh_coroutine(Ent *e, Osh *o) {
  if (o->co_wait > 0) {
    o->co_wait -= DT;
    return;
  }
  switch (o->st) {
    case OS_CHASE:   /* ChaseCoroutine */
      if (o->co_step == 0) {
        static const float WAIT[5] = {1, 2, 3, 2, 3};
        if (g_session.mode != M_A) o->co_wait = 1;
        else o->co_wait = WAIT[o->attack_index], o->attack_index = (uint8_t)((o->attack_index + 1) % 5);
        o->co_step = 1;
      } else if (o->co_step == 1) {
        ospr_play(&o->spr, OA_CHARGE, false);
        o->co_wait = 0.7f, o->co_step = 2;
      } else if (o->co_step == 2) {
        o->co_step = 3;
        if (level_player()) osh_state(e, o, OS_CHARGE);
        else ospr_play(&o->spr, OA_IDLE, false);
      }
      break;
    case OS_CHARGE:   /* ChargeUpCoroutine */
      if (o->co_step == 0) {
        level_freeze(0.05f);
        o->lightning_on = 1;
        ospr_play(&o->lightning, OA_LIGHTNING, true);
        o->co_wait = 0.3f, o->co_step = 1;
      } else if (o->co_step == 1) {
        o->co_step = 2;
        osh_state(e, o, level_player() ? OS_ATTACK : OS_CHASE);
      }
      break;
    case OS_ATTACK:   /* AttackCoroutine */
      if (o->co_step == 0) o->co_wait = 0.1f, o->co_step = 1;
      break;
  }
}
static void osh_update(Ent *e) {
  Osh *o = ST(e, Osh);
  const Room *rm = room_ptr(e);
  float cl = g_level.cam.x;
  if (g_level.transitioning) {   /* TransitionListener.OnOut */
    if (e->room != g_level.room_slot) {
      ospr_update(&o->lightning);
      if (o->ease_back) e->x -= 128 * DT;
    }
    return;
  }
  /* base.Update(): the sprites, the state machine (its update, then its coroutine), the shaker */
  ospr_update(&o->spr);
  ospr_update(&o->lightning);
  if (o->lightning_on && o->lightning.anim != OA_LIGHTNING) o->lightning_on = 0;   /* OnFinish */
  int next = o->st;
  Player *p = level_player();
  switch (o->st) {
    case OS_CHASE:
      if (o->respawn_anim && o->cam_x_off >= 0) {
        o->col_x = -48;
        e->visible = 1;
        ospr_play(&o->spr, OA_RESPAWN, false);
        o->respawn_anim = 0;
      }
      o->cam_x_off = approach(o->cam_x_off, 20, 80 * DT);
      e->x = cl + o->cam_x_off;
      o->col_x = approach(o->col_x, 3, DT * 128);
      e->collidable = e->visible;
      if (p && o->spr.anim != OA_RESPAWN) osh_set_cy(e, approach(osh_cy(e), osh_target_y(e), o->y_speed * DT));
      next = OS_CHASE;
      break;
    case OS_CHARGE:
      if (level_on_interval(0.05f)) o->shx = (int8_t)shake_val(), o->shy = (int8_t)shake_val();
      o->cam_x_off = approach(o->cam_x_off, 0, 40 * DT);
      e->x = cl + o->cam_x_off;
      if (p) osh_set_cy(e, approach(osh_cy(e), clampf(e_cym(p->ent), rm->y + 8.f, rm->y + rm->h - 8.f), 30 * DT));
      next = OS_CHARGE;
      break;
    case OS_ATTACK:
      e->x += o->attack_speed * DT;
      o->attack_speed = approach(o->attack_speed, 500, 2000 * DT);
      next = OS_ATTACK;
      if (e->x >= cl + 320 + 48) {
        if (o->leaving) {
          ent_remove(e);
          return;
        }
        e->x = cl - 48;
        o->cam_x_off = -48;
        o->respawn_anim = 1;
        e->visible = 0;
        next = OS_CHASE;
        break;
      }
      if (level_on_interval(0.05f)) {
        trail_add(e->x, e->y, ospr_tex(&o->spr), 48, 48, o->sx, o->sy, 0x99990000 /* Color.Red * 0.6 */, 0.5f, e->depth + 1);
      }
      break;
    case OS_WAITING:
      next = (p && (p->speed.x != 0 || p->speed.y != 0) && p->ent->x > rm->x + 48) ? OS_CHASE : OS_WAITING;
      break;
    case OS_HURT:
      e->x += 100 * DT, e->y += 200 * DT;
      next = OS_HURT;
      if (e->y - 10 > rm->y + rm->h + 20) {
        if (o->leaving) {
          ent_remove(e);
          return;
        }
        e->x = cl - 48;
        o->cam_x_off = -48;
        o->respawn_anim = 1;
        e->visible = 0;
        next = OS_CHASE;
      }
      break;
  }
  osh_state(e, o, next);
  if (o->st == OS_CHASE || o->st == OS_CHARGE || o->st == OS_ATTACK) osh_coroutine(e, o);
  if (o->shake_timer > 0) {   /* Shaker */
    if ((o->shake_timer -= DT) <= 0) o->shx = o->shy = 0;
  }
  /* Update's own */
  o->sx = approach(o->sx, 1, 0.6f * DT);
  o->sy = approach(o->sy, 1, 0.6f * DT);
  if (!o->respawn_anim) e->visible = e->x > rm->x - 14;
  o->y_speed = approach(o->y_speed, 100, 300 * DT);
  /* Engine.TimeRate: slow motion while it attacks just behind the player (canControlTimeRate: nothing
   * here stops it); TODO Distort.GameRate / Distort.Anxiety (no distortion shader) */
  if (o->st != OS_DUMMY) {
    Player *pl = level_player();
    float cx = e->x + o->col_x;
    if (o->st == OS_ATTACK && o->attack_speed > 200 && pl && !pl->dead && cx < e_cxm(pl->ent) + 4)
      g_time_rate = lerpf(clamped_map(e_cxm(pl->ent) - cx, 30, 80, 0.5f, 1), 1, clamped_map(fabsf(e_cym(pl->ent) - osh_cy(e)), 32, 48, 0, 1));
    else
      g_time_rate = 1;
  }
  osh_box(e);
}
/* an animation not loaded yet: all its frames at once (as spr_draw does) */
static void ospr_load(const OSpr *s) {
  if (res_cached(ospr_tex(s))) return;
  int a = s->anim != OA_NONE ? s->anim : s->last;
  uint16_t ids[36];
  for (int i = 0; i < OANIM[a].n; i++) ids[i] = a == OA_LIGHTNING ? osh_light[i] : a == OA_BACK ? osh_boss[oback(i)] : osh_boss[OANIM[a].first + i];
  res_load(ids, OANIM[a].n);
}
static void osh_render(Ent *e) {
  Osh *o = ST(e, Osh);
  if (o->lightning_on) ospr_load(&o->lightning);
  ospr_load(&o->spr);
  if (o->lightning_on) gfx_tex(ospr_tex(&o->lightning), g_level.cam.x - 2, e->y - 10 + 16 - 32, 0, 0xFFFF, 255);   /* Justify (0, 0.5) */
  float sh = o->shake_timer > 0 ? 2.f : 0.f;   /* Sprite.Position = shaker.Value * 2 */
  gfx_tex_ex(ospr_tex(&o->spr), e->x + o->shx * sh, e->y + o->shy * sh, 48, 48, o->sx, o->sy, o->rot, 0xFFFF, 255, 0);
  light_add(e->x, e->y, 0xFFFFFF, 1, 32, 64);
}
static const EntClass OSHIRO = {.name = "friendlyGhost", .size = sizeof(Osh), .update = osh_update, .render = osh_render,
                                .on_player = osh_on_player, .kind = KIND_PCOLLIDE};
static void new_oshiro(float x, float y, bool from_cutscene) {
  if (level_get_flag("oshiroEnding") || (!level_get_flag("oshiro_resort_roof") && !strcmp(level_room_name_of(g_level.rooms[g_level.room_slot].index), "roof00")))
    return;
  Ent *e = ent_new(&OSHIRO, x, y);
  if (!e) return;
  e->depth = -12500;
  e->visible = 0;
  e->tags = TAG_TRANSITION_UPDATE;
  Osh *o = ST(e, Osh);
  char pth[40];
  for (int i = 0; i < 73; i++) osh_boss[i] = tex(path2(pth, "characters/oshiro/boss", NULL, i));
  for (int i = 0; i < 7; i++) osh_light[i] = tex(path2(pth, "characters/oshiro/lightning", NULL, i));
  ospr_play(&o->spr, OA_IDLE, true);
  o->lightning.anim = o->lightning.last = OA_NONE;
  o->lightning.last = OA_LIGHTNING;
  o->sx = o->sy = 1;
  o->col_x = 3;
  o->y_speed = from_cutscene ? 0 : 100;
  o->from_cutscene = from_cutscene;
  o->st = 0xFF;
  osh_state(e, o, OS_CHASE);   /* the StateMachine's EntityAdded */
  if (!from_cutscene) {        /* Added */
    osh_state(e, o, OS_WAITING);
    osh_set_cy(e, osh_target_y(e));
    o->cam_x_off = -48;
  } else
    o->cam_x_off = x - g_level.cam.x;
  osh_box(e);
}
/* OshiroTrigger */
typedef struct { uint8_t state; } OshTrig;
static void oshtrig_enter(Ent *e, Player *p) {
  (void)p;
  if (ST(e, OshTrig)->state) {
    const Room *rm = room_ptr(e);
    new_oshiro(rm->x - 32.f, rm->y + rm->h / 2.f, false);
  } else
    for (int i = 0; i < g_nents; i++)
      if (g_ents[i].cls == &OSHIRO && g_ents[i].dead != 1) {
        ST(&g_ents[i], Osh)->leaving = 1;
        break;
      }
  ent_remove(e);
}
static const EntClass OSHTRIG = {.name = "oshiroTrigger", .size = sizeof(OshTrig), .kind = KIND_TRIGGER,
                                 .more = &(const EntMore){.on_enter = oshtrig_enter}};

/* ---------------------------------------------------------------- ResortMirror */
/* the glass (glassbg's middle), its shine (glassfg) at shineAlpha x mirrorAlpha, and the frame (its own entity,
 * in front); the Badeline of the suite's cutscene walks in it (a BadelineDummy cut to the glass). The
 * reflections of the player and of Oshiro are not drawn */
typedef struct { Sprite glass; float shine, alpha; int16_t evil; uint8_t smashed, breaking, appear, fading, step; } RMirror;
static uint16_t rmirror_frame, rmirror_bg, rmirror_broken, rmirror_fg;
static const EntClass RMIRROR;
static void rmirror_layer(uint16_t *strip, int sy0, int sy1, void *ctx) {
  Ent *e = ctx;
  RMirror *m = ST(e, RMirror);
  Tex fg;
  if (!tex_get(rmirror_fg, &fg) || fg.w > (int)sizeof rowbuf) return;
  int n;
  const uint16_t *col = pal_colors(fg.pal, &n);
  const uint8_t *al = pal_alpha(fg.pal);
  const int W = 30, H = 27;
  int ox = vx(e->x - W / 2.f), oy = vy(e->y - H);
  int num = -(int)fmodf(g_level.cam.y * 0.8f, 73);
  for (int k = 0; k < 2; k++) {   /* DrawJustified((W / 2, num), (0.5, 1)) and the second one (it uses H / 2) */
    int x0 = k ? H / 2 - fg.fw / 2 : W / 2 - fg.fw / 2, y0 = (k ? num - fg.fh : num) - fg.fh;
    for (int r = 0; r < fg.h; r++) {
      int y = y0 + fg.oy + r, Y = oy + y;
      if (y < 0 || y >= H || Y < sy0 || Y >= sy1) continue;
      tex_row_idx(&fg, r, rowbuf);
      for (int i = 0; i < fg.w; i++) {
        int x = x0 + fg.ox + i, X = ox + x;
        if (!rowbuf[i] || x < 0 || x >= W || (unsigned)X >= VIEW_W) continue;
        sp_texel(strip + (Y - sy0) * VIEW_W + X, col, al, rowbuf[i], 0xFFFF, (int)(m->shine * m->alpha * 256), false);
      }
    }
  }
}
static Ent *rmirror_evil(RMirror *m) { return m->evil >= 0 && g_ents[m->evil].cls && g_ents[m->evil].dead != 1 ? &g_ents[m->evil] : NULL; }
static void rmirror_update(Ent *e) {
  RMirror *m = ST(e, RMirror);
  Ent *ev = rmirror_evil(m);
  if (m->appear && ev && !ch2_bdummy_walk_to(ev, e->x, 64)) m->appear = 0;   /* EvilAppearRoutine */
  if (m->fading) {   /* FadeLights */
    g_level.lighting = approach(g_level.lighting, 0.35f, DT * 0.1f);
    if (g_level.lighting == 0.35f) m->fading = 0;
  }
  if (m->breaking) spr_update(&m->glass);
}
static void rmirror_render(Ent *e) {
  RMirror *m = ST(e, RMirror);
  gfx_tex_part(m->smashed ? rmirror_broken : rmirror_bg, e->x - 15, e->y - 27, 12, 2, 30, 27, 0, 0xFFFF, 255);
}
static const EntClass RMIRROR = {.name = "resortmirror", .size = sizeof(RMirror), .update = rmirror_update, .render = rmirror_render};
static void rmframe_render(Ent *e) {   /* the target's glass over what it shows, then the frame */
  Ent *me = &g_ents[*ST(e, int16_t)];
  if (me->cls == &RMIRROR && !ST(me, RMirror)->smashed) {
    RMirror *m = ST(me, RMirror);
    if (m->breaking) {   /* the breaking glass (a 54 x 29 frame cut to the 30 x 27 glass) */
      Tex t;
      uint16_t tx = spr_tex(&m->glass);
      if (tx != 0xFFFF && tex_get(tx, &t))
        gfx_tex_part(tx, me->x - 15, me->y - 27, 12, 2, 30, 27, 0, 0xFFFF, (uint8_t)(clampf(m->shine * m->alpha, 0, 1) * 255));
    } else
      tex_touch(rmirror_fg), gfx_custom(rmirror_layer, me, vy(me->y - 27), vy(me->y));
  }
  gfx_tex(rmirror_frame, e->x - 16, e->y - 39, 0, 0xFFFF, 255);
}
static const EntClass RMFRAME = {.name = "resortmirrorFrame", .size = sizeof(int16_t), .render = rmframe_render};
static void rmirror_evil_appear(Ent *e) {   /* EvilAppear */
  RMirror *m = ST(e, RMirror);
  Ent *ev = ch2_bdummy_new(v2(e->x + 23, e->y));   /* (mirror.Width + 8, mirror.Height) in the glass */
  m->evil = ev ? (int16_t)(ev - g_ents) : -1;
  if (ev) set_depth(ev, 9499), ch2_bdummy_clip(ev, e->x - 15, e->y - 27, e->x + 15, e->y, m->alpha);
  m->appear = m->fading = 1;
}
static bool rmirror_smash(Ent *e) {   /* SmashRoutine */
  RMirror *m = ST(e, RMirror);
  Ent *ev = rmirror_evil(m);
  switch (m->step) {
    case 0:
      if (ev && ch2_bdummy_float_to(ev, v2(e->x, e->y - 8), 0, 0)) return true;
      spr_init(&m->glass, SB_glass);
      spr_play(&m->glass, A_glass_break, false);
      m->breaking = 1, m->step = 1;
      return true;
    case 1:
      if (m->glass.anim == A_glass_break) {
        if (m->glass.frame == 7) level_shake(0.3f);
        m->shine = approach(m->shine, 1, DT * 2);
        m->alpha = approach(m->alpha, 1, DT * 2);
        if (ev) ch2_bdummy_clip(ev, e->x - 15, e->y - 27, e->x + 15, e->y, m->alpha);
        return true;
      }
      level_shake(0.3f);
      for (float x = -27; x < 27; x += 8)
        for (float y = -29; y < 0; y += 8)
          if (rndf() < 0.5f) particles_emit(PL_MID, &P_DreamMirror_P_Shatter, 2, v2(e->x + x + 4, e->y + y + 4), v2(8, 8), atan2f(y, x));
      if (ev) ent_remove(ev);   /* evil = null: no longer drawn in the glass */
      m->evil = -1;
      m->step = 2;
      /* fall through */
    default:
      return false;
  }
}
static void rmirror_set_broken(Ent *e) {   /* Broken */
  RMirror *m = ST(e, RMirror);
  Ent *ev = rmirror_evil(m);
  if (ev) ent_remove(ev);
  m->evil = -1;
  m->smashed = 1, m->breaking = 0;
}
static void new_rmirror(const EData *d) {
  Ent *e = ent_new(&RMIRROR, d->x, d->y);
  if (!e) return;
  e->depth = 9500;
  e->collidable = 0;
  RMirror *m = ST(e, RMirror);
  m->smashed = level_get_flag("oshiro_resort_suite");
  m->shine = m->alpha = 0.7f;
  m->evil = -1;
  rmirror_frame = tex("objects/mirror/resortframe");
  rmirror_bg = tex("objects/mirror/glassbg");
  rmirror_broken = tex("objects/mirror/glassbreak09");
  rmirror_fg = tex("objects/mirror/glassfg");
  Ent *f = ent_new(&RMFRAME, d->x, d->y);
  if (f) f->depth = 9000, f->collidable = 0, *ST(f, int16_t) = (int16_t)(e - g_ents);
}

/* ---------------------------------------------------------------- PicoConsole */
typedef struct { Talk talk; Walk walk; float wait; uint8_t step; } Pico;
static uint16_t pico_tex;
static void pico_update(Ent *e) {
  Pico *c = ST(e, Pico);
  Player *p = level_player();
  if (talk_update(&c->talk, e) && !c->step && p) {   /* OnInteract */
    c->step = 1;
    player_set_state(p, ST_DUMMY);
    c->walk = walk_exact((int)floorf(e->x) - 6);   /* yield return DummyWalkToExact */
    return;
  }
  if (!c->step || !p) return;
  if (c->wait > 0) {
    c->wait -= DT;
    return;
  }
  switch (c->step) {
    case 1:
      if (player_walk(p, &c->walk)) break;
      p->facing = 1;
      c->wait = 0.5f, c->step = 2;
      break;
    case 2:   /* TODO: the PICO-8 game (Emulator) behind a SpotlightWipe */
      c->wait = 0.25f, c->step = 3;
      break;
    default:
      player_set_state(p, ST_NORMAL);
      c->step = 0;
      break;
  }
}
static void pico_render(Ent *e) { gfx_tex(pico_tex, e->x - 24, e->y - 32, 0, 0xFFFF, 255); }
static const EntClass PICO = {.name = "picoconsole", .size = sizeof(Pico), .update = pico_update, .render = pico_render, .kind = KIND_TALK};
static void new_pico(const EData *d) {
  Ent *e = ent_new(&PICO, d->x, d->y);
  if (!e) return;
  e->depth = 1000;
  e->tags = TAG_TRANSITION_UPDATE | TAG_PAUSE_UPDATE;
  e->collidable = 0;
  ch1_talk_add(e, &ST(e, Pico)->talk, -12, -8, 24, 8, v2(0, -24));
  pico_tex = tex("objects/pico8Console");
}

/* ---------------------------------------------------------------- Key */
typedef struct {
  Sprite spr;
  Wiggler wig;
  Tween tw;
  V2 from, to, ctrl;
  float wobble, wait, alarm, light;
  uint32_t id;
  uint8_t used, started, turning, step, wobble_on, gain_pending;
} Key;
static const EntClass KEY;
static bool keys_restored;
static bool session_has_key(uint32_t id) {
  for (int i = 0; i < g_session.nkeys && i < 4; i++)
    if (g_session.keys[i] == id) return true;
  return false;
}
static void session_add_key(uint32_t id) {
  if (!session_has_key(id) && g_session.nkeys < 4) g_session.keys[g_session.nkeys++] = id;
}
static void session_remove_key(uint32_t id) {
  for (int i = 0; i < g_session.nkeys; i++)
    if (g_session.keys[i] == id) {
      memmove(g_session.keys + i, g_session.keys + i + 1, sizeof(uint32_t) * (size_t)(g_session.nkeys - i - 1));
      g_session.nkeys--;
      return;
    }
}
static void key_on_player(Ent *e, Player *p) {
  Key *k = ST(e, Key);
  (void)p;
  particles_emit(PL_MID, &P_Key_P_Collect, 10, v2(e->x, e->y), v2(3, 3), P_Key_P_Collect.direction);
  leader_gain(e, 0.5f, true);
  e->collidable = 0;
  level_set_do_not_load(k->id);
  session_add_key(k->id);
  g_session.dashes_at_level_start = g_session.dashes;   /* UpdateLevelStartDashes */
  wiggler_restart(&k->wig);
  e->depth = -1000000;
  ents_mark_unsorted();
  /* (keys with two nodes start a cassette fly: none here) */
}
/* Key.UseRoutine, stepped */
static void key_use_step(Ent *e, Key *k) {
  if (k->wait > 0) {
    k->wait -= DT;
    return;
  }
  switch (k->step) {
    case 1: {
      bool end = tween_update(&k->tw);
      V2 at = curve_point(k->from, k->to, k->ctrl, ease_cube_out(k->tw.percent));
      e->x = at.x, e->y = at.y;
      k->spr.rate = 1 + ease_cube_out(k->tw.percent) * 2;
      if (end) k->step = 2;
      break;
    }
    case 2:
      if (k->spr.frame != 4) break;
      for (int i = 0; i < 16; i++) particles_emit1(PL_FG, &P_Key_P_Insert, e_center(e), PI_F / 8 * i);
      spr_play(&k->spr, A_key_enter, false);
      k->wait = 0.3f, k->step = 3;
      break;
    case 3:
      tween_start(&k->tw, 0.3f);
      k->step = 4;
      break;
    case 4: {
      bool end = tween_update(&k->tw);
      k->spr.rot = ease_cube_in(k->tw.percent) * (PI_F / 2);
      if (!end) break;
      k->alarm = 1;   /* then the light fades over 1 s and the key goes */
      k->wait = 0.2f, k->step = 5;
      break;
    }
    case 5:
      for (int i = 0; i < 8; i++) particles_emit1(PL_FG, &P_Key_P_Insert, e_center(e), PI_F / 4 * i);
      k->spr.visible = 0;
      k->turning = 0;
      k->step = 6;
      break;
  }
}
static void key_update(Ent *e) {
  Key *k = ST(e, Key);
  keys_restored = false;
  if (k->gain_pending && level_player()) {   /* Key(player, id): it follows at once */
    Player *p = level_player();
    e->x = p->ent->x - 12 * p->facing, e->y = p->ent->y - 8;
    leader_gain(e, 0.5f, true);
    k->gain_pending = 0;
  }
  if (g_level.transitioning) {   /* TransitionListener.OnOut */
    k->started = 0;
    if (!k->used && k->step) {
      k->step = 0, k->turning = 0, k->tw.active = false;
      e->visible = 1, k->spr.visible = 1, k->spr.rate = 1, k->spr.sx = k->spr.sy = 1, k->spr.rot = 0;
      spr_play(&k->spr, A_key_idle, false);
      k->wig.active = false;
      leader_set_move(e, true);
    }
    return;
  }
  if (k->wobble_on) k->wobble += DT * 4;
  /* base.Update(): follower, sprite, wiggler, the shimmer, the coroutine, the alarm */
  leader_tick(e);
  spr_update(&k->spr);
  wiggler_update(&k->wig);
  if (k->spr.visible && level_on_interval(0.1f))
    particles_emit(PL_FG, &P_Key_P_Shimmer, 1, v2(e->x, e->y), v2(6, 6), P_Key_P_Shimmer.direction);
  if (k->step) key_use_step(e, k);
  if (k->alarm > 0 && (k->alarm -= DT) <= 0) k->light = 1, k->alarm = -1;
  if (k->alarm < 0) {
    k->light -= DT;
    if (k->light <= 0) ent_remove(e);
  }
}
static void key_render(Ent *e) {
  Key *k = ST(e, Key);
  Sprite s = k->spr;
  s.sx = s.sy = (k->step ? 1 : 1) * (1 + k->wig.value * 0.35f);
  spr_draw(&s, e->x, e->y + (k->wobble_on ? sinf(k->wobble) : 0));
  float la = k->alarm < 0 ? fmaxf(k->light, 0) : 1;
  light_add(e->x, e->y, 0xFFFFFF, la, 32, 48);
}
static const EntClass KEY = {.name = "key", .size = sizeof(Key), .update = key_update, .render = key_render, .on_player = key_on_player,
                             .kind = KIND_PCOLLIDE};
static Ent *make_key(float x, float y, uint32_t id) {
  Ent *e = ent_new(&KEY, x, y);
  if (!e) return NULL;
  ent_box(e, 12, 12, -6, -6);
  e->tags = TAG_TRANSITION_UPDATE;
  Key *k = ST(e, Key);
  k->id = id;
  spr_init(&k->spr, SB_key);
  spr_play(&k->spr, A_key_idle, true);
  wiggler_init(&k->wig, 0.4f, 4);
  return e;
}
static void new_key(const EData *d) {
  make_key(d->x, d->y, level_entity_hash(d));
}
/* Level.LoadLevel: the session's keys follow the new player */
static void restore_keys(void) {
  for (int i = 0; i < g_session.nkeys && i < 4; i++) {
    Ent *e = make_key((float)g_session.rx, (float)g_session.ry - 8, g_session.keys[i]);
    if (!e) continue;
    e->collidable = 0;
    e->depth = -1000000;
    ST(e, Key)->gain_pending = 1;
  }
}

/* ---------------------------------------------------------------- LockBlock */
typedef struct { Sprite spr; float wait; int16_t key; uint32_t id; uint8_t opening, step, bank; } Lock;
/* the sprites lockdoor_<sprite>: only those some map's assets ask for exist in data.bin */
static int lock_anim(const Lock *l, int which) {   /* 0 idle, 1 open, 2 burst */
  (void)which;
  switch (l->bank) {
#ifdef SB_lockdoor_temple_a
    case 1: return which == 0 ? A_lockdoor_temple_a_idle : which == 1 ? A_lockdoor_temple_a_open : A_lockdoor_temple_a_burst;
#endif
#ifdef SB_lockdoor_temple_b
    case 2: return which == 0 ? A_lockdoor_temple_b_idle : which == 1 ? A_lockdoor_temple_b_open : A_lockdoor_temple_b_burst;
#endif
#ifdef SB_lockdoor_moon
    case 3: return which == 0 ? A_lockdoor_moon_idle : which == 1 ? A_lockdoor_moon_open : A_lockdoor_moon_burst;
#endif
#ifdef SB_lockdoor_wood
    case 0: return which == 0 ? A_lockdoor_wood_idle : which == 1 ? A_lockdoor_wood_open : A_lockdoor_wood_burst;
#endif
    default: return 0;
  }
}
static int lock_bank(int b) {
  switch (b) {
#ifdef SB_lockdoor_temple_a
    case 1: return SB_lockdoor_temple_a;
#endif
#ifdef SB_lockdoor_temple_b
    case 2: return SB_lockdoor_temple_b;
#endif
#ifdef SB_lockdoor_moon
    case 3: return SB_lockdoor_moon;
#endif
#ifdef SB_lockdoor_wood
    case 0: return SB_lockdoor_wood;
#endif
    default: return -1;
  }
}
/* its PlayerCollider is Circle(60) at (16, 16), not its solid box: checked from its update */
static void lock_on_player(Ent *e, Player *p) {
  Lock *l = ST(e, Lock);
  if (l->opening) return;
  float cx = e->x + 16, cy = e->y + 16;
  Ent *pe = p->ent;
  float nx = clampf(cx, e_left(pe), e_right(pe)), ny = clampf(cy, e_top(pe), e_bottom(pe));
  if ((cx - nx) * (cx - nx) + (cy - ny) * (cy - ny) >= 60 * 60) return;
  for (int i = 0; i < leader_count(); i++) {
    Ent *f = leader_follower(i);
    if (f->cls != &KEY || ST(f, Key)->started) continue;
    /* TryOpen: nothing solid between the player and the block */
    e->collidable = 0;
    V2 a = player_center(p), b = e_center(e);
    bool blocked = false;
    for (int s = 0; s <= 16 && !blocked; s++) {
      V2 q = v2add(a, v2mul(v2sub(b, a), s / 16.f));
      blocked = point_solid(q.x, q.y);
    }
    if (!blocked) {
      l->opening = 1;
      ST(f, Key)->started = 1;
      l->key = ent_ref(f);
      /* Key.UseRoutine(Center + (0, 2)) */
      Key *k = ST(f, Key);
      k->turning = 1;
      leader_set_move(f, false);
      wiggler_restart(&k->wig);
      k->wobble_on = 0;
      k->from = v2(f->x, f->y);
      k->to = v2add(b, v2(0, 2));
      k->ctrl = v2add(v2mul(v2add(k->to, k->from), 0.5f), v2(0, -48));
      tween_start(&k->tw, 1);
      k->step = 1;
      l->wait = 1.2f, l->step = 1;
    }
    e->collidable = 1;
    break;
  }
}
static void lock_update(Ent *e) {
  Lock *l = ST(e, Lock);
  spr_update(&l->spr);
  plat_update(e);
  Player *p = level_player();
  if (p && !p->dead && !l->opening) lock_on_player(e, p);
  if (!l->step) return;
  if (l->wait > 0) {
    l->wait -= DT;
    return;
  }
  Ent *k = ent_at(l->key, &KEY);
  switch (l->step) {
    case 1:
      level_set_do_not_load(l->id);
      if (k) {   /* Key.RegisterUsed */
        ST(k, Key)->used = 1;
        leader_lose(k);
        session_remove_key(ST(k, Key)->id);
      }
      l->step = 2;
      /* fall through */
    case 2:
      if (k && ST(k, Key)->turning) break;
      e->tags |= TAG_TRANSITION_UPDATE;
      e->collidable = 0;
      spr_play(&l->spr, lock_anim(l, 1), false);
      l->step = 3;
      break;
    case 3:
      if (spr_is(&l->spr, lock_anim(l, 1))) break;
      level_shake(0.3f);
      spr_play(&l->spr, lock_anim(l, 2), false);
      l->step = 4;
      break;
    case 4:
      if (spr_is(&l->spr, lock_anim(l, 2))) break;
      ent_remove(e);
      break;
  }
}
static void lock_render(Ent *e) { spr_draw(&ST(e, Lock)->spr, e->x + 16, e->y + 16); }
static void lock_awake(Ent *e) { plat_static_movers_attach(e); }
static const EntClass LOCK = {.name = "lockBlock", .size = sizeof(Lock), .update = lock_update, .render = lock_render, .awake = lock_awake,
                              .kind = KIND_SOLID};
static void new_lock(const EData *d) {
  uint32_t id = level_entity_hash(d);
  Ent *e = ent_new(&LOCK, d->x, d->y);
  if (!e) return;
  ent_box(e, 32, 32, 0, 0);
  Lock *l = ST(e, Lock);
  l->id = id;
  l->key = -1;
  const char *sp = EAS(d, lockBlock, sprite);
  l->bank = !strcmp(sp, "temple_a") ? 1 : !strcmp(sp, "temple_b") ? 2 : !strcmp(sp, "moon") ? 3 : 0;
  if (lock_bank(l->bank) >= 0) spr_init(&l->spr, lock_bank(l->bank)), spr_play(&l->spr, lock_anim(l, 0), true);
  else l->spr.visible = 0;
  /* TODO: stepMusicProgress (no music) */
}

/* ---------------------------------------------------------------- BlockField, Killbox */
static const EntClass BLOCKFIELD = {.name = "blockField", .size = 0, .kind = KIND_BLOCKFIELD};
static void killbox_on_player(Ent *e, Player *p) { (void)e, player_die(p, v2(0, 0), false); }
static void killbox_update(Ent *e) {
  Ent *p = level_player_ent();
  if (!e->collidable) {
    if (p && e_bottom(p) < e_top(e) - 32) e->collidable = 1;
  } else if (p && e_top(p) > e_bottom(e) + 32)
    e->collidable = 0;
}
static const EntClass KILLBOX3 = {.name = "killbox", .size = 0, .update = killbox_update, .on_player = killbox_on_player,
                                  .kind = KIND_KILLBOX | KIND_PCOLLIDE};

static float area_darkness(void);
/* ---------------------------------------------------------------- Oshiro: NPC03_Oshiro_* and OshiroSprite */
enum { OK_LOBBY, OK_HALL1, OK_HALL2, OK_CLUTTER, OK_BREAKDOWN, OK_SUITE, OK_ROOF };
typedef struct {
  Talk talk;
  Sprite spr;
  Wiggler wig;              /* OshiroSprite's */
  Move move;                /* MoveToAndRemove, the pacing's MoveTo */
  NpcWalk nw;
  Co co;                    /* its coroutines: the pacing, the suite's talk */
  Ent *tb;
  V2 home, nodes[3];
  float light, light_y, bloom, pace_t;
  uint8_t kind, has_talk, light_on, changes, turn_invisible, talked, in_routine, sections, removing, pacing;
} Osh3;
static const EntClass OSH3;
static void osh_pop(Osh3 *o, int anim, bool flip) {   /* OshiroSprite.Pop */
  if (o->spr.anim == anim) return;
  spr_play(&o->spr, anim, false);
  if (flip) o->spr.sx = -o->spr.sx;
  wiggler_restart(&o->wig);
}
static void osh_remove_at(Ent *e, Osh3 *o, V2 to) {   /* MoveToAndRemove */
  memset(&o->move, 0, sizeof o->move);
  o->move.target = to, o->move.flags = MV_REMOVE;
  o->removing = 1, o->pacing = 0;
  (void)e;
}
static bool osh_move(Ent *e, Move *m) { return ch1_move_to(e, &ST(e, Osh3)->spr, m, 80, A_oshiro_move, A_oshiro_idle); }
static void osh_talk(Ent *e, Osh3 *o, Player *p);
static void osh_trigger(Ent *e, Osh3 *o, Player *p);
static void osh_pace(Ent *e, Osh3 *o) {   /* NPC03_Oshiro_Cluttter.Pace */
  Co *c = &o->co;
  CO_BEGIN(c);
  for (;;) {
    wiggler_restart(&o->wig);
    memset(&o->move, 0, sizeof o->move), o->move.target = v2add(o->home, v2(-20, 0));   /* PaceLeft */
    CO_NEST(c, osh_move(e, &o->move));
    while (o->pace_t < 2.266f) CO_YIELD(c);
    o->pace_t = 0;
    wiggler_restart(&o->wig);
    memset(&o->move, 0, sizeof o->move), o->move.target = o->home;   /* PaceRight */
    CO_NEST(c, osh_move(e, &o->move));
    while (o->pace_t < 2.266f) CO_YIELD(c);
    o->pace_t = 0;
  }
  CO_END(c);
}
static void osh_suite_talk(Ent *e, Osh3 *o) {   /* NPC03_Oshiro_Suite.Talk */
  static const char *const SAD[7] = {"CH3_OSHIRO_SUITE_SAD0", "CH3_OSHIRO_SUITE_SAD1", "CH3_OSHIRO_SUITE_SAD2", "CH3_OSHIRO_SUITE_SAD3",
                                     "CH3_OSHIRO_SUITE_SAD4", "CH3_OSHIRO_SUITE_SAD5", "CH3_OSHIRO_SUITE_SAD6"};
  Co *c = &o->co;
  CO_BEGIN(c);
  o->in_routine = (uint8_t)ch1_counter("oshiroSuiteSadConversation");
  CO_NEST(c, npc_approach(&o->nw, e, NULL, 12, 0));
  CO_SAY(c, o->tb, SAD[o->in_routine % 7], NULL, NULL);
  player_free();   /* PlayerLeave */
  CO_YIELD(c);
  o->in_routine = (uint8_t)((o->in_routine + 1) % 7);
  if (!o->in_routine) o->in_routine = 1;
  ch1_set_counter("oshiroSuiteSadConversation", o->in_routine);
  o->pacing = 0;
  CO_END(c);
}
static void osh3_update(Ent *e) {
  Osh3 *o = ST(e, Osh3);
  Player *p = level_player();
  /* the components: the sprite (OshiroSprite.Update), its wiggler, the talker, the coroutines */
  spr_update(&o->spr);
  int bank, idle;
  if (o->changes && textbox_portrait(&bank, &idle)) {
    if (bank == SB_portrait_oshiro && (idle == A_portrait_oshiro_idle_sidehappy || idle == A_portrait_oshiro_idle_sideworried ||
                                       idle == A_portrait_oshiro_idle_sidesuspicious)) {
      if (o->spr.anim == A_oshiro_idle) osh_pop(o, A_oshiro_side, true);
    } else if (o->spr.anim == A_oshiro_side)
      osh_pop(o, A_oshiro_idle, true);
  }
  if (o->turn_invisible && o->spr.visible) {
    const Room *rm = g_level.room;
    o->spr.visible = e->x > rm->x - 8 && e->y > rm->y - 8 && e->x < rm->x + rm->w + 8 && e->y < rm->y + rm->h + 16;
  }
  if (o->wig.active) {
    wiggler_update(&o->wig);
    float k = o->wig.value * 0.2f;
    o->spr.sx = (o->spr.sx < 0 ? -1 : 1) * (1 + k), o->spr.sy = 1 - k;
  }
  if (o->has_talk && talk_update(&o->talk, e) && p) osh_talk(e, o, p);
  if (o->pacing == 1) osh_pace(e, o);
  else if (o->pacing == 2) osh_suite_talk(e, o);
  if (o->removing) osh_move(e, &o->move);
  if (o->light_on == 1) {   /* NPC.Update: its Light in the level's bounds */
    const Room *rm = g_level.room;
    bool in = e->x > rm->x - 16 && e->y > rm->y - 16 && e->x < rm->x + rm->w + 16 && e->y < rm->y + rm->h + 16;
    o->light = approach(o->light, in && !g_level.transitioning ? 1 : 0, DT * 2);
  }
  osh_trigger(e, o, p);
}
static void osh3_render(Ent *e) {
  Osh3 *o = ST(e, Osh3);
  if (o->spr.visible) draw_npc(&o->spr, e->x, e->y);
  if (o->light_on) light_add(e->x, e->y + (o->light_on == 2 ? -8 + o->light_y : -16), 0xFFFFFF, o->light, 32, 64);
  if (o->bloom > 0) bloom_add(e->x, e->y - 8, o->bloom, 16);
}
static const EntClass OSH3 = {.size = sizeof(Osh3), .name = "NPC03_Oshiro", .update = osh3_update, .render = osh3_render};
static Ent *find_osh(int kind) {
  for (int i = 0; i < g_nents; i++)
    if (g_ents[i].cls == &OSH3 && g_ents[i].dead != 1 && ST(&g_ents[i], Osh3)->kind == kind) return &g_ents[i];
  return NULL;
}

/* OshiroLobbyBell: talking to it rings it (a sound), once Oshiro has gone */
static void bell_update(Ent *e) {
  Talk *t = ST(e, Talk);
  if (!t->enabled && !find_osh(OK_LOBBY)) t->enabled = true;
  talk_update(t, e);
}
static const EntClass BELL = {.size = sizeof(Talk), .name = "OshiroLobbyBell", .update = bell_update};

/* ---------------------------------------------------------------- CS03_OshiroLobby */
typedef struct {
  Cutscene cs;
  Ent *osh, *tb;
  Walk walk;
  Step step;
  Co ev;
  Tween tw;
  float start_light, from_y;
  int8_t evi;
  uint8_t sparks, tw_kind;
} Lobby;
static bool lobby_event(void *ctx, int index) {   /* ZoomOut */
  Lobby *s = ST((Ent *)ctx, Lobby);
  Co *c = ev_start(&s->ev, &s->evi, index);
  EV_BEGIN;
  EV_WAIT(0.2f);
  step_reset(&s->step);
  EV_NEST(zoom_back(&s->step, 0.5f));
  EV_WAIT(0.2f);
  EV_END;
}
static void lobby_doors(bool instant) {   /* every MrOshiroDoor (a GDOOR marked oshiro): Open / InstantOpen */
  for (int i = 0; i < g_nents; i++)
    if (g_ents[i].cls == &GDOOR && g_ents[i].dead != 1 && ST(&g_ents[i], GDoor)->oshiro) gdoor_open(&g_ents[i], instant);
}
static void lobby_end(Ent *e, bool skipped) {
  Lobby *s = ST(e, Lobby);
  player_free();
  lobby_doors(skipped);   /* skipped: InstantOpen (played through, Open has opened them already: this does nothing) */
  g_level.lighting = s->start_light;
  level_set_flag("oshiro_resort_talked_1", true);
  if (skipped && s->osh->cls == &OSH3) ent_remove(s->osh);
}
static void lobby_update(Ent *e) {
  Lobby *s = ST(e, Lobby);
  Ent *oe = s->osh;
  Osh3 *o = ST(oe, Osh3);
  Player *p = &g_player;
  if (s->sparks && level_on_interval(0.025f)) {
    V2 at = v2add(v2(oe->x, oe->y - 12), v2((float)((4 + rndi(8)) * (rndi(2) ? 1 : -1)), (float)((4 + rndi(8)) * (rndi(2) ? 1 : -1))));
    V2 d = v2sub(at, v2(oe->x, oe->y));
    particles_emit1(PL_MID, &P_NPC03_Oshiro_Lobby_P_AppearSpark, at, atan2f(d.y, d.x));
  }
  Co *c = &s->cs.co;
  CO_BEGIN(c);
  s->start_light = g_level.lighting;
  s->from_y = oe->y;
  player_hold(p);
  CO_WAIT(c, 0.5f);
  s->walk = walk_to(oe->x - 16);
  CO_NEST(c, player_walk(p, &s->walk));
  p->facing = 1;
  CO_WAIT(c, 1.4f);
  level_shake(0.3f);
  for (g_level.lighting += 0.5f; g_level.lighting > s->start_light;) {
    g_level.lighting -= DT * 4;
    CO_YIELD(c);
  }
  o->light_on = 2, o->light = 0, o->light_y = -48;   /* its VertexLight and BloomPoint (the level's spotlight) */
  oe->y -= 16;
  tween_start(&s->tw, 0.5f), s->tw_kind = 1;
  CO_YIELD(c);
  while (s->tw.active) {
    tween_update(&s->tw);
    o->light = o->bloom = s->tw.percent;
    o->light_y = lerpf(-48, 0, s->tw.percent);
    g_level.lighting = lerpf(s->start_light, 1, ease_cube_out(s->tw.percent));
    if (s->tw.active) CO_YIELD(c);
  }
  CO_WAIT(c, 0.2f);
  step_reset(&s->step);
  CO_NEST(c, zoom_to(&s->step, v2(170, 126), 2, 0.5f));
  CO_WAIT(c, 0.6f);
  level_shake(0.3f);
  o->spr.visible = 1;
  spr_play(&o->spr, A_oshiro_appear, false);
  s->walk = walk_exact((int)(p->ent->x - 12));
  s->walk.flags = WALK_BACKWARDS;
  CO_NEST(c, player_walk(p, &s->walk));
  p->dummy_auto_animate = false;
  spr_play(&p->spr, A_player_shaking, false);
  CO_WAIT(c, 0.6f);
  s->sparks = 1;
  CO_WAIT(c, 0.4f);
  s->sparks = 0;
  CO_WAIT(c, 0.2f);
  level_shake(0.3f);
  CO_WAIT(c, 1.4f);
  tween_start(&s->tw, 0.5f);   /* the lights back: in parallel with Oshiro going down */
  for (;;) {
    if (s->tw.active) {
      tween_update(&s->tw);
      g_level.lighting = lerpf(1, s->start_light, s->tw.percent);
      o->bloom = 1 - s->tw.percent;
    }
    if (oe->y == s->from_y) break;
    oe->y = approach(oe->y, s->from_y, DT * 40);
    CO_YIELD(c);
  }
  while (s->tw.active) {
    CO_YIELD(c);
    tween_update(&s->tw);
    g_level.lighting = lerpf(1, s->start_light, s->tw.percent);
    o->bloom = 1 - s->tw.percent;
  }
  p->dummy_auto_animate = true;
  s->evi = -1;
  CO_SAY(c, s->tb, "CH3_OSHIRO_FRONT_DESK", lobby_event, e);
  lobby_doors(false);
  osh_remove_at(oe, o, v2((float)(g_level.room->x + g_level.room->w + 64), oe->y));
  CO_WAIT(c, 1.5f);
  cutscene_end(e);
  return;
  CO_END(c);
}
static const EntClass LOBBY = {.size = sizeof(Lobby), .name = "CS03_OshiroLobby", .update = lobby_update};

/* ---------------------------------------------------------------- CS03_OshiroHallway1, CS03_OshiroHallway2 */
typedef struct { Cutscene cs; Ent *osh, *tb; uint8_t two; } Hall;
static void hall_end(Ent *e, bool skipped) {
  Hall *s = ST(e, Hall);
  player_free();
  level_set_flag(s->two ? "oshiro_resort_talked_3" : "oshiro_resort_talked_2", true);
  if (skipped && s->osh->cls == &OSH3) ent_remove(s->osh);
}
static void hall_update(Ent *e) {
  Hall *s = ST(e, Hall);
  Co *c = &s->cs.co;
  CO_BEGIN(c);
  player_hold(&g_player);
  CO_SAY(c, s->tb, s->two ? "CH3_OSHIRO_HALLWAY_B" : "CH3_OSHIRO_HALLWAY_A", NULL, NULL);
  if (s->osh->cls == &OSH3) osh_remove_at(s->osh, ST(s->osh, Osh3), v2((float)(g_level.room->x + g_level.room->w + 64), s->osh->y));
  CO_WAIT(c, 1);
  cutscene_end(e);
  return;
  CO_END(c);
}
static const EntClass HALL = {.size = sizeof(Hall), .name = "CS03_OshiroHallway", .update = hall_update};

/* ---------------------------------------------------------------- NPC03_Oshiro_Cluttter's talk and CS03_OshiroClutter */
static V2 oclutter_zoom(Ent *oe) {   /* ZoomPoint */
  Osh3 *o = ST(oe, Osh3);
  return v2(oe->x - g_level.cam.x, oe->y - (o->sections < 2 ? 30 : 15) - g_level.cam.y);
}
static bool gdoor_locked_ent(Ent *e);
static bool gdoor_unlock(Ent *e, float *p, V2 *from);
static void gdoor_instant_unlock(Ent *e);
static bool is_gdoor(const Ent *e);
typedef struct {
  Cutscene cs;
  Ent *osh, *tb;
  int16_t doors[6];
  Walk walk;
  Step step;
  Pan pan;
  Move move;              /* the coroutines' own MoveTo */
  Move pace;              /* Add(new Coroutine(oshiro.PaceRight())) */
  NpcWalk nw;
  Co ev;
  float p;
  V2 from;
  int8_t evi, index, ndoors, door, pacing, talk;
} OClutter;
static bool oclutter_event(void *ctx, int index) {   /* Collapse, PaceLeft, PaceRight; StandUp */
  OClutter *s = ST((Ent *)ctx, OClutter);
  Ent *oe = s->osh;
  Osh3 *o = ST(oe, Osh3);
  Co *c = ev_start(&s->ev, &s->evi, index);
  if (s->talk) {   /* StandUp */
    EV_BEGIN;
    osh_pop(o, A_oshiro_idle, false);
    EV_WAIT(0.25f);
    EV_END;
  }
  EV_BEGIN;
  if (index == 0) {
    spr_play(&o->spr, A_oshiro_fall, false);
    EV_WAIT(0.5f);
  } else {
    memset(&s->move, 0, sizeof s->move);
    s->move.target = index == 1 ? v2add(o->home, v2(-20, 0)) : o->home;
    EV_NEST(osh_move(oe, &s->move));
  }
  EV_END;
}
static void oclutter_end(Ent *e, bool skipped) {
  OClutter *s = ST(e, OClutter);
  Ent *oe = s->osh;
  Osh3 *o = ST(oe, Osh3);
  if (s->talk) {   /* NPC03_Oshiro_Cluttter.EndTalkRoutine */
    osh_pop(o, A_oshiro_idle, false);
    player_free();
    return;
  }
  player_free();
  if (o->spr.anim == A_oshiro_side) osh_pop(o, A_oshiro_idle, true);
  if (s->index < 3) {
    char f[32];
    level_set_flag("oshiro_clutter_door_open", true);
    path2(f, "oshiro_clutter_", NULL, s->index);
    f[15] = (char)('0' + s->index), f[16] = 0;
    level_set_flag(f, true);
    for (int i = 0; i < s->ndoors; i++) {
      Ent *d = &g_ents[s->doors[i]];
      if (is_gdoor(d) && !gdoor_locked_ent(d)) gdoor_instant_unlock(d);
    }
    if (skipped && s->index == 0) spr_play(&o->spr, A_oshiro_idle_ground, false);
  } else {
    level_set_flag("oshiro_clutter_finished", true);
    ent_remove(oe);
  }
}
static void oclutter_update(Ent *e) {
  static const char *const KEYS[3] = {"CH3_OSHIRO_CLUTTER0", "CH3_OSHIRO_CLUTTER1", "CH3_OSHIRO_CLUTTER2"};
  static const char *const KEYS_B[3] = {"CH3_OSHIRO_CLUTTER0_B", "CH3_OSHIRO_CLUTTER1_B", "CH3_OSHIRO_CLUTTER2_B"};
  OClutter *s = ST(e, OClutter);
  Ent *oe = s->osh;
  Osh3 *o = ST(oe, Osh3);
  Player *p = &g_player;
  if (s->pacing && !osh_move(oe, &s->pace)) s->pacing = 0;
  Co *c = &s->cs.co;
  CO_BEGIN(c);
  s->evi = -1;
  if (s->talk) {   /* NPC03_Oshiro_Cluttter.TalkRoutine */
    CO_NEST(c, npc_approach(&s->nw, oe, &o->spr, 24, s->index != 1 && s->index != 2 ? 1 : -1));
    step_reset(&s->step);
    CO_NEST(c, zoom_to(&s->step, oclutter_zoom(oe), 2, 0.5f));
    CO_SAY(c, s->tb, KEYS_B[s->index], oclutter_event, e);
    step_reset(&s->step);
    CO_NEST(c, zoom_back(&s->step, 0.5f));
    cutscene_end(e);
    return;
  }
  player_hold(p);
  if (s->index == 1 || s->index == 2) {
    s->walk = walk_exact((int)oe->x - 24);
    CO_NEST(c, player_walk(p, &s->walk));
    p->facing = 1;
    o->spr.sx = -1;
  } else {
    memset(&s->pace, 0, sizeof s->pace), s->pace.target = o->home, s->pacing = 1;
    s->walk = walk_exact((int)o->home.x + 24);
    CO_NEST(c, player_walk(p, &s->walk));
    p->facing = -1;
    o->spr.sx = 1;
  }
  if (s->index < 3) {
    step_reset(&s->step);
    CO_NEST(c, zoom_to(&s->step, oclutter_zoom(oe), 2, 0.5f));
    CO_SAY(c, s->tb, KEYS[s->index], oclutter_event, e);
    step_reset(&s->step);
    CO_NEST(c, zoom_back(&s->step, 0.5f));
    level_set_flag("oshiro_clutter_door_open", true);
    for (s->door = 0; s->door < s->ndoors; s->door++) {
      if (!is_gdoor(&g_ents[s->doors[s->door]]) || gdoor_locked_ent(&g_ents[s->doors[s->door]])) continue;
      s->p = -1;
      CO_NEST(c, gdoor_unlock(&g_ents[s->doors[s->door]], &s->p, &s->from));
    }
  } else {
    s->pan.p = -1;
    CO_NEST(c, pan_to(&s->pan, v2((float)g_level.room->x, (float)g_level.room->y), 0.5f, ease_cube_inout, false));
    step_reset(&s->step);
    CO_NEST(c, zoom_to(&s->step, v2(90, 60), 2, 0.5f));
    CO_SAY(c, s->tb, "CH3_OSHIRO_CLUTTER_ENDING", NULL, NULL);
    memset(&s->move, 0, sizeof s->move), s->move.target = v2(oe->x, (float)(g_level.room->y - 32));
    CO_NEST(c, osh_move(oe, &s->move));
    step_reset(&s->step);
    CO_NEST(c, zoom_back(&s->step, 0.5f));
  }
  cutscene_end(e);
  return;
  CO_END(c);
}
static const EntClass CLUTTER3 = {.size = sizeof(OClutter), .name = "CS03_OshiroClutter", .update = oclutter_update};
static void oclutter_start(Ent *oe, Osh3 *o, bool talk) {
  Ent *e = cs_new(&CLUTTER3, oclutter_end, true, false);
  if (!e) return;
  OClutter *s = ST(e, OClutter);
  s->osh = oe;
  s->index = (int8_t)o->sections;
  s->talk = talk;
  for (int i = 0; i < g_nents && s->ndoors < 6; i++)   /* the ClutterDoors, by y */
    if (is_gdoor(&g_ents[i])) {
      int k = s->ndoors++;
      while (k > 0 && g_ents[s->doors[k - 1]].y > g_ents[i].y) s->doors[k] = s->doors[k - 1], k--;
      s->doors[k] = (int16_t)i;
    }
}

/* ---------------------------------------------------------------- CS03_OshiroBreakdown */
/* (DustStaticSpinners are not ported: the creatures it hides and sends back home are left out, with
 * their timing) */
typedef struct {
  Cutscene cs;
  Ent *osh, *tb;
  Walk walk, walk2;
  Step step;
  Pan pan;
  Move move, leave;
  Co ev;
  V2 origin;
  float anxiety;
  int8_t evi, i;
  uint8_t walking, leaving;
} Breakdown;
static bool breakdown_event(void *ctx, int index) {   /* WalkLeft, WalkRight, CreateDustA, CreateDustB */
  Breakdown *s = ST((Ent *)ctx, Breakdown);
  Ent *oe = s->osh;
  Osh3 *o = ST(oe, Osh3);
  Player *p = &g_player;
  Co *c = ev_start(&s->ev, &s->evi, index);
  EV_BEGIN;
  if (index < 2) {
    o->changes = 0;
    memset(&s->move, 0, sizeof s->move);
    s->move.target = v2add(s->origin, v2(index == 0 ? -24.f : 0.f, 0));
    EV_NEST(osh_move(oe, &s->move));
    o->changes = 1;
  } else if (index == 2) {
    o->changes = 0;
    spr_play(&o->spr, A_oshiro_fall, false);
    for (s->i = 0; s->i < 4; s->i++) {   /* MoveDust(creatures[i]) */
      if (s->i % 4 == 0) {
        level_shake(0.3f);
        EV_WAIT(0.4f);
      } else
        EV_WAIT(0.1f);
    }
    EV_WAIT(0.5f);
  } else {
    EV_WAIT(1);   /* (no creatures beyond the first four) */
    for (s->anxiety = 0.1f + rndf() * 0.1f; s->anxiety > 0; s->anxiety -= DT) EV_YIELD;   /* Distort.Anxiety */
    step_reset(&s->step);
    EV_NEST(zoom_back(&s->step, 0.5f));
    s->walk2 = walk_exact(g_level.room->x + 200);
    EV_NEST(player_walk(p, &s->walk2));
    EV_WAIT(1);
    spr_play(&o->spr, A_oshiro_recover, false);
    EV_WAIT(0.7f);
    o->spr.sx = 1;
    EV_WAIT(0.5f);
  }
  EV_END;
}
static void breakdown_end(Ent *e, bool skipped) {
  Breakdown *s = ST(e, Breakdown);
  Player *p = level_player();
  player_free();
  if (skipped && p) {
    p->ent->x = (float)(g_level.room->x + 200);
    for (int i = 0; i < 400 && !actor_on_ground(p->ent, 1); i++) p->ent->y++;
  }
  if (p) g_level.cam = player_camera_target(p);
  if (s->osh->cls == &OSH3) ent_remove(s->osh);
  level_set_flag("oshiro_breakdown", true);
}
static void breakdown_update(Ent *e) {
  Breakdown *s = ST(e, Breakdown);
  Ent *oe = s->osh;
  Player *p = &g_player;
  if (s->walking && !player_walk(p, &s->walk)) s->walking = 0;   /* Add(new Coroutine(player.DummyWalkTo(player.X - 64))) */
  if (s->leaving && !osh_move(oe, &s->leave)) s->leaving = 0;
  Co *c = &s->cs.co;
  CO_BEGIN(c);
  player_hold(p);
  s->walk = walk_to(p->ent->x - 64), s->walking = 1;
  s->pan.p = -1;
  CO_NEST(c, pan_to(&s->pan, v2((float)g_level.room->x, 0), 1, ease_cube_inout, true));
  CO_WAIT(c, 0.2f);
  step_reset(&s->step);
  CO_NEST(c, zoom_to(&s->step, v2(100, 120), 2, 0.5f));
  s->evi = -1;
  CO_SAY(c, s->tb, "CH3_OSHIRO_BREAKDOWN", breakdown_event, e);
  memset(&s->leave, 0, sizeof s->leave), s->leave.target = v2((float)(g_level.room->x - 64), oe->y), s->leaving = 1;
  CO_WAIT(c, 0.25f);
  s->pan.p = -1;
  CO_NEST(c, pan_to(&s->pan, player_camera_target(p), 1, ease_cube_inout, true));
  cutscene_end(e);
  return;
  CO_END(c);
}
static const EntClass BREAKDOWN = {.size = sizeof(Breakdown), .name = "CS03_OshiroBreakdown", .update = breakdown_update};

/* ---------------------------------------------------------------- CS03_OshiroMasterSuite */
typedef struct {
  Cutscene cs;
  Ent *osh, *tb, *evil, *mirror;
  Walk walk, pwalk;
  Step step;
  Co ev;
  V2 from;
  float p;
  int8_t evi;
  uint8_t walking;
} Suite;
static bool suite_event(void *ctx, int index) {
  /* SuiteShadowAppear, SuiteShadowDisrupt, SuiteShadowCeiling, Wander, Console, JumpBack, Collapse, AwkwardPause */
  Suite *s = ST((Ent *)ctx, Suite);
  Ent *oe = s->osh;
  Osh3 *o = ST(oe, Osh3);
  Player *p = &g_player;
  Co *c = ev_start(&s->ev, &s->evi, index);
  switch (index) {
    case 0: {
      EV_BEGIN;
      if (!s->mirror) return true;
      rmirror_evil_appear(s->mirror);
      s->from = g_level.zoom_focus;
      for (s->p = 0; s->p < 1; s->p += DT * 2) {
        g_level.zoom_focus = v2lerp(s->from, v2(216, 110), ease_sine_inout(s->p));
        EV_YIELD;
      }
      EV_YIELD;
      EV_END;
    }
    case 1: {
      EV_BEGIN;
      if (!s->mirror) return true;
      EV_NEST(rmirror_smash(s->mirror));
      s->evil = ch2_bdummy_new(v2add(v2(s->mirror->x, s->mirror->y), v2(0, -8)));
      EV_WAIT(1.2f);
      o->spr.sx = 1;
      if (s->evil) EV_NEST(ch2_bdummy_float_to(s->evil, v2add(v2(oe->x, oe->y), v2(32, -24)), 0, 0));
      EV_END;
    }
    case 2: {
      EV_BEGIN;
      step_reset(&s->step);
      EV_NEST(zoom_back(&s->step, 0.5f));
      if (!s->evil) return true;
      s->from = v2((float)(g_level.room->x + 96), s->evil->y - 16);
      EV_NEST(ch2_bdummy_float_to(s->evil, s->from, 1, BD_TURN));
      p->facing = -1;
      EV_WAIT(0.25f);
      level_dir_shake(v2(0, -1), 0.3f);
      s->from = v2add(v2(s->evil->x, s->evil->y), v2(0, -32));
      EV_NEST(ch2_bdummy_smash(s->evil, s->from));
      EV_WAIT(0.8f);
      EV_END;
    }
    case 3: {
      EV_BEGIN;
      EV_WAIT(0.5f);
      p->facing = 1;
      EV_WAIT(0.1f);
      s->walk = walk_exact((int)oe->x + 48);
      EV_NEST(player_walk(p, &s->walk));
      EV_WAIT(1);
      p->facing = -1;
      EV_WAIT(0.2f);
      s->walk = walk_exact((int)oe->x - 32);
      EV_NEST(player_walk(p, &s->walk));
      EV_WAIT(0.1f);
      o->spr.sx = -1;
      EV_WAIT(0.2f);
      p->dummy_auto_animate = false;
      spr_play(&p->spr, A_player_lookUp, false);
      EV_WAIT(1);
      p->dummy_auto_animate = true;
      EV_WAIT(0.4f);
      p->facing = 1;
      EV_WAIT(0.2f);
      s->walk = walk_exact((int)oe->x - 24);
      EV_NEST(player_walk(p, &s->walk));
      EV_WAIT(0.5f);
      step_reset(&s->step);
      EV_NEST(zoom_to(&s->step, v2(190, 110), 2, 0.5f));
      EV_END;
    }
    case 4: {
      EV_BEGIN;
      s->walk = walk_exact((int)oe->x - 16);
      EV_NEST(player_walk(p, &s->walk));
      EV_END;
    }
    case 5: {
      EV_BEGIN;
      s->walk = walk_exact((int)oe->x - 24);
      s->walk.flags = WALK_BACKWARDS;
      EV_NEST(player_walk(p, &s->walk));
      EV_WAIT(0.8f);
      EV_END;
    }
    case 6: {
      EV_BEGIN;
      spr_play(&o->spr, A_oshiro_fall, false);
      EV_YIELD;
      EV_END;
    }
    default: {
      EV_BEGIN;
      EV_WAIT(2);
      EV_END;
    }
  }
}
static void suite_end(Ent *e, bool skipped) {
  Suite *s = ST(e, Suite);
  Ent *oe = s->osh;
  Osh3 *o = ST(oe, Osh3);
  if (skipped) {
    if (s->evil && s->evil->cls) ent_remove(s->evil);
    if (s->mirror) rmirror_set_broken(s->mirror);
    for (int i = 0; i < g_nents; i++)   /* the DashBlock: RemoveAndFlagAsGone */
      if (g_ents[i].cls && g_ents[i].dead != 1 && level_is_dash_block(&g_ents[i])) {
        level_break_dash_block(&g_ents[i], v2(0, -1));
        break;
      }
    spr_play(&o->spr, A_oshiro_idle_ground, false);
  }
  o->talk.enabled = true;
  player_free();
  g_level.lighting = area_darkness();   /* BaseLightingAlpha */
  level_set_flag("oshiro_resort_suite", true);
}
static void suite_update(Ent *e) {
  Suite *s = ST(e, Suite);
  Ent *oe = s->osh;
  Player *p = level_player();
  if (s->walking && p && !player_walk(p, &s->pwalk)) s->walking = 0;
  Co *c = &s->cs.co;
  CO_BEGIN(c);
  while (!p) {
    CO_YIELD(c);
    p = level_player();
  }
  CO_WAIT(c, 0.4f);
  player_hold(p);
  s->pwalk = walk_to(oe->x + 32), s->walking = 1;   /* Add(new Coroutine(player.DummyWalkTo(...))) */
  CO_WAIT(c, 1);
  s->evi = -1;
  CO_SAY(c, s->tb, "CH3_OSHIRO_SUITE", suite_event, e);
  if (s->evil) s->from = v2(s->evil->x, (float)(g_level.room->y - 32));
  if (s->evil) CO_NEST(c, ch2_bdummy_float_to(s->evil, s->from, 0, 0));
  if (s->evil) ent_remove(s->evil);
  s->evil = NULL;
  while (g_level.lighting != area_darkness()) {
    g_level.lighting = approach(g_level.lighting, area_darkness(), DT * 0.5f);
    CO_YIELD(c);
  }
  cutscene_end(e);
  return;
  CO_END(c);
}
static const EntClass SUITE = {.size = sizeof(Suite), .name = "CS03_OshiroMasterSuite", .update = suite_update};

/* ---------------------------------------------------------------- CS03_OshiroRooftop */
typedef struct {
  Cutscene cs;
  Ent *osh, *tb, *evil;
  Walk walk;
  Step step;
  Move move;
  Pan pan;
  Co ev;
  V2 spawn, from, to;
  float p, anxiety, offset;
  int8_t evi;
  uint8_t walking, floating, moving, cam_out;
} Roof3;
static bool roof3_event(void *ctx, int index) {   /* MaddyWalkAway, MaddyTurnAround, EnterOshiro, OshiroGetsAngry */
  Roof3 *s = ST((Ent *)ctx, Roof3);
  Ent *oe = s->osh;
  Osh3 *o = ST(oe, Osh3);
  Player *p = &g_player;
  Co *c = ev_start(&s->ev, &s->evi, index);
  switch (index) {
    case 0: {
      EV_BEGIN;
      s->walk = walk_to((float)g_level.room->x + 170), s->walking = 1;
      EV_WAIT(0.2f);
      if (s->evil) s->to = v2add(v2(s->evil->x, s->evil->y), v2(80, 30)), s->floating = 1;
      EV_YIELD;
      EV_END;
    }
    case 1: {
      EV_BEGIN;
      EV_WAIT(0.25f);
      p->facing = -1;
      EV_WAIT(0.1f);
      step_reset(&s->step);
      EV_NEST(zoom_to(&s->step, v2(150, s->spawn.y - g_level.room->y - 8), 2, 0.5f));
      EV_END;
    }
    case 2: {
      EV_BEGIN;
      EV_WAIT(0.3f);
      s->offset = (0.5f - 0.667f) * 96;   /* the boss sprite's Justify less Oshiro's, times its height */
      oe->visible = 1;
      o->spr.sx = 1;
      memset(&s->move, 0, sizeof s->move), s->move.target = v2sub(s->spawn, v2(0, s->offset)), s->moving = 1;
      s->from.x = g_level.zoom_focus.x;
      for (s->p = 0; s->p < 1; s->p += DT / 0.7f) {
        g_level.zoom_focus.x = s->from.x + (126 - s->from.x) * ease_cube_inout(s->p);
        EV_YIELD;
      }
      EV_WAIT(0.3f);
      p->facing = -1;
      EV_WAIT(0.1f);
      if (s->evil) ch2_bdummy_face(s->evil, -1);
      EV_END;
    }
    default: {
      EV_BEGIN;
      EV_WAIT(0.1f);
      if (s->evil) ch2_bdummy_vanish(s->evil);
      s->evil = NULL, s->floating = 0;
      EV_WAIT(0.8f);
      spr_init(&o->spr, SB_oshiro_boss);   /* the boss's sprite instead */
      spr_play(&o->spr, A_oshiro_boss_transformStart, false);
      o->changes = 0;
      oe->y += s->offset;
      set_depth(oe, -12500);
      EV_WAIT(1);
      EV_END;
    }
  }
}
static void roof3_end(Ent *e, bool skipped) {
  Roof3 *s = ST(e, Roof3);
  Player *p = level_player();
  if (s->evil && s->evil->cls) ent_remove(s->evil);
  if (p) {
    p->state_locked = false;
    player_set_state(p, ST_NORMAL);
    p->ent->x = (float)g_level.room->x + 170;
    p->speed.y = 0;
    for (int i = 0; i < 400 && collide_solid(p->ent, p->ent->x, p->ent->y); i++) p->ent->y--;
    g_level.cam = player_camera_target(p);
  }
  (void)skipped;
  if (s->osh->cls == &OSH3) ent_remove(s->osh);
  level_set_flag("oshiro_resort_roof", true);   /* (first: the boss checks it) */
  new_oshiro(s->spawn.x, s->spawn.y, true);
  g_session.rx = (int32_t)(g_level.room->x + 170), g_session.ry = g_level.room->y + 160;
  g_session.has_respawn = 1;
}
static void roof3_update(Ent *e) {
  Roof3 *s = ST(e, Roof3);
  Ent *oe = s->osh;
  Osh3 *o = ST(oe, Osh3);
  Player *p = level_player();
  if (s->walking && p && !player_walk(p, &s->walk)) s->walking = 0;
  if (s->floating && s->evil && !ch2_bdummy_float_to(s->evil, s->to, 0, 0)) s->floating = 0;
  if (s->moving && !osh_move(oe, &s->move)) s->moving = 0;
  if (s->cam_out) {   /* AnxietyAndCameraOut */
    s->anxiety = approach(s->anxiety, 0, DT * 4);
    if (!pan_to(&s->pan, s->to, 0.5f, ease_cube_inout, false)) s->cam_out = 0;
  }
  Co *c = &s->cs.co;
  CO_BEGIN(c);
  while (!p) {
    CO_YIELD(c);
    p = level_player();
  }
  player_hold(p);
  while (!actor_on_ground(p->ent, 1) || p->speed.y < 0) CO_YIELD(c);
  CO_WAIT(c, 0.6f);
  s->evil = ch2_bdummy_new(v2(oe->x - 40, (float)(g_level.room->y + g_level.room->h - 60)));
  if (s->evil) ch2_bdummy_face(s->evil, 1), ch2_bdummy_appear(s->evil);
  CO_WAIT(c, 0.1f);
  p->facing = -1;
  s->evi = -1;
  CO_SAY(c, s->tb, "CH3_OSHIRO_START_CHASE", roof3_event, e);
  CO_WAIT(c, 0.2f);   /* OshiroTransform */
  spr_play(&o->spr, A_oshiro_boss_transformFinish, false);
  level_shake(0.5f);
  while (s->anxiety < 0.5f) {
    s->anxiety = approach(s->anxiety, 0.5f, DT * 0.5f);
    CO_YIELD(c);
  }
  CO_WAIT(c, 0.25f);
  s->pan.p = -1, s->to = player_camera_target(p), s->cam_out = 1;
  step_reset(&s->step);
  CO_NEST(c, zoom_back(&s->step, 0.5f));
  CO_WAIT(c, 0.25f);
  cutscene_end(e);
  return;
  CO_END(c);
}
static const EntClass ROOF3 = {.size = sizeof(Roof3), .name = "CS03_OshiroRooftop", .update = roof3_update};

/* the Oshiros: Added, OnTalk, and the checks of their Update */
static void osh_talk(Ent *e, Osh3 *o, Player *p) {
  (void)p;
  switch (o->kind) {
    case OK_LOBBY: {
      Ent *c = cs_new(&LOBBY, lobby_end, true, false);
      if (c) ST(c, Lobby)->osh = e;
      o->talk.enabled = false;
      break;
    }
    case OK_CLUTTER: {   /* OnTalk */
      char f[40];
      o->talked = 1;
      o->pacing = 0;
      path2(f, "oshiro_clutter_", NULL, -1);
      f[15] = (char)('0' + o->sections), f[16] = 0;
      if (!level_get_flag(f)) {
        oclutter_start(e, o, false);
        break;
      }
      path2(f, "oshiro_clutter_optional_", NULL, -1);
      f[24] = (char)('0' + o->sections), f[25] = 0;
      level_set_flag(f, true);
      oclutter_start(e, o, true);   /* Level.StartCutscene(EndTalkRoutine), TalkRoutine */
      o->talk.enabled = false;
      break;
    }
    case OK_SUITE:   /* Add(new Coroutine(Talk(player))) */
      memset(&o->co, 0, sizeof o->co), memset(&o->nw, 0, sizeof o->nw);
      o->pacing = 2;
      break;
  }
}
static void osh_trigger(Ent *e, Osh3 *o, Player *p) {
  switch (o->kind) {
    case OK_LOBBY:
      if (e->x >= o->home.x + 12) set_depth(e, 1000);
      break;
    case OK_HALL1:
    case OK_HALL2:
      if (!o->talked && p && p->ent->x > e->x - 60) {
        Ent *c = cs_new(&HALL, hall_end, true, false);
        if (c) ST(c, Hall)->osh = e, ST(c, Hall)->two = o->kind == OK_HALL2;
        o->talked = 1;
      }
      break;
    case OK_CLUTTER:
      o->pace_t += DT;
      if (o->sections == 3 && !o->in_routine && p && p->ent->x < e->x + 32 && p->ent->y <= e->y) {
        osh_talk(e, o, p);
        o->in_routine = 1;
      }
      break;
    case OK_BREAKDOWN:
      if (!o->talked && p) {
        const Room *rm = g_level.room;
        if ((p->ent->x <= rm->x + 370 && p->on_safe_ground && p->ent->y < rm->y + rm->h / 2) || p->ent->x <= rm->x + 320) {
          Ent *c = cs_new(&BREAKDOWN, breakdown_end, true, false);
          if (c) ST(c, Breakdown)->osh = e, ST(c, Breakdown)->origin = v2(e->x, e->y);
          o->talked = 1;
        }
      }
      break;
  }
}
static void new_osh(const EData *d, int kind) {
  static const char *const DONE[7] = {"oshiro_resort_talked_1", "oshiro_resort_talked_2", "oshiro_resort_talked_3",
                                      "oshiro_clutter_finished", "oshiro_breakdown", NULL, "oshiro_resort_roof"};
  if (DONE[kind] && level_get_flag(DONE[kind])) return;   /* Added: RemoveSelf */
  Ent *e = ent_new(&OSH3, d->x, d->y);
  if (!e) return;
  e->depth = 1000;
  ent_box(e, 8, 8, -4, -8);
  Osh3 *o = ST(e, Osh3);
  o->kind = (uint8_t)kind;
  spr_init(&o->spr, SB_oshiro);
  spr_play(&o->spr, A_oshiro_idle, false);
  o->spr.sx = kind <= OK_CLUTTER ? -1 : 1;   /* OshiroSprite(facing) */
  wiggler_init(&o->wig, 0.3f, 2);
  o->changes = o->turn_invisible = 1;
  o->light_on = kind != OK_LOBBY, o->light = 1;
  o->home = v2(d->x, d->y);
  switch (kind) {
    case OK_LOBBY: {
      o->spr.visible = 0;
      e->depth = 9001;
      ch1_talk_add(e, &o->talk, -30, -16, 42, 32, v2(-12, -24));   /* (its "hover/resort" note: the plain prompt) */
      o->talk.must_face = false;
      o->has_talk = 1;
      Ent *b = ent_new(&BELL, d->x - 14, d->y);
      if (b) {
        b->collidable = 0;
        ch1_talk_add(b, ST(b, Talk), -8, -8, 16, 16, v2(0, -24));
        ST(b, Talk)->enabled = false;
      }
      break;
    }
    case OK_HALL2: g_session.lighting_alpha_add = 0.15f; break;
    case OK_CLUTTER: {
      for (int i = 0; i < 3 && i < d->nnodes; i++) o->nodes[i] = ed_node(d, i);
      char f[32];
      for (int i = 0; i < 3; i++) {
        path2(f, "oshiro_clutter_cleared_", NULL, -1);
        f[23] = (char)('0' + i), f[24] = 0;
        if (level_get_flag(f)) o->sections++;
      }
      if (o->sections == 0 || o->sections == 3) o->spr.sx = 1;
      if (o->sections > 0) e->x = o->nodes[o->sections - 1].x, e->y = o->nodes[o->sections - 1].y;
      else if (!level_get_flag("oshiro_clutter_0")) o->pacing = 1;
      if (o->sections == 0 && level_get_flag("oshiro_clutter_0") && !level_get_flag("oshiro_clutter_optional_0"))
        spr_play(&o->spr, A_oshiro_idle_ground, false);
      path2(f, "oshiro_clutter_optional_", NULL, -1);
      f[24] = (char)('0' + o->sections), f[25] = 0;
      o->has_talk = !(o->sections == 3 || level_get_flag(f));
      ch1_talk_add(e, &o->talk, -24, -8, 48, 8, v2(0, -24));
      if (!o->has_talk) ch1_talk_remove(e);
      o->home = v2(e->x, e->y);
      break;
    }
    case OK_SUITE:
      ch1_talk_add(e, &o->talk, -16, -8, 32, 8, v2(0, -24));
      o->has_talk = 1;
      if (!level_get_flag("oshiro_resort_suite")) {
        o->talk.enabled = false;
        Ent *c = cs_new(&SUITE, suite_end, true, false);
        if (c) {
          ST(c, Suite)->osh = e;
          for (int i = 0; i < g_nents; i++)
            if (g_ents[i].cls == &RMIRROR && g_ents[i].dead != 1) ST(c, Suite)->mirror = &g_ents[i];
        }
      } else
        spr_play(&o->spr, A_oshiro_idle_ground, false);
      break;
    case OK_ROOF: {
      o->turn_invisible = 0;
      e->visible = 0;
      Ent *c = cs_new(&ROOF3, roof3_end, true, false);
      if (c) ST(c, Roof3)->osh = e, ST(c, Roof3)->spawn = v2(d->x, (float)(g_level.rooms[g_level.room_slot].y + g_level.rooms[g_level.room_slot].h - 40));
      break;
    }
  }
}

/* ---------------------------------------------------------------- Theo: NPC03_Theo_Escaping, NPC03_Theo_Vents */
/* their grates: the vent's (falls up and left, turning) and the ceiling's (shakes, then drops) */
typedef struct { V2 speed; float alpha, rot, shake; int8_t x; uint8_t falling, ceiling; } Grate;
static void grate_update(Ent *e) {
  Grate *g = ST(e, Grate);
  if (g->shake > 0) {
    g->shake -= DT;
    if (level_on_interval(0.05f)) g->x = (int8_t)(1 - g->x);
  }
  if (!g->falling) return;
  g->speed.x = approach(g->speed.x, 0, DT * (g->ceiling ? 80 : 120));
  g->speed.y += (g->ceiling ? 200 : 400) * DT;
  e->x += g->speed.x * DT, e->y += g->speed.y * DT;
  if (collide_solid(e, e->x, e->y + 2) && g->speed.y > 0) g->speed.y = -g->speed.y * 0.25f;
  g->alpha -= DT;
  g->rot += DT * (g->ceiling ? 1 : v2len(g->speed) * 0.05f);
  if (g->alpha <= 0) ent_remove(e);
}
static void grate_render(Ent *e) {
  Grate *g = ST(e, Grate);
  /* JustifyOrigin(0.5, 0) of a 16 x 8 picture; the vent's lies on its side (Rotation PI / 2) */
  gfx_tex_ex(T_scenery_grate, e->x + g->x, e->y, 8, 0, 1, 1, g->rot, 0xFFFF, (uint8_t)(clampf(g->alpha, 0, 1) * 255), 0);
}
static const EntClass GRATE = {.size = sizeof(Grate), .name = "Grate", .update = grate_update, .render = grate_render};
static void grate_fall(Ent *e) {
  Grate *g = ST(e, Grate);
  if (g->falling) return;
  g->falling = 1;
  g->speed = g->ceiling ? v2(40, 200) : v2(-120, -120);
  ent_box(e, 2, 2, g->ceiling ? -1 : -2, g->ceiling ? 0 : -1);
}

enum { TK_ESCAPING, TK_VENTS };
typedef struct {
  Talk talk;
  Sprite spr;
  Move move;
  Co co;
  NpcWalk nw;
  Step step;
  Pan pan;
  Ent *tb;
  int16_t grate;
  float light, particles, spr_y, from;
  uint8_t kind, has_talk, talked, appeared, crawling, talking;
} Theo3;
static const EntClass THEO3;
static Ent *theo3_grate(Theo3 *t) { return t->grate >= 0 && g_ents[t->grate].cls == &GRATE ? &g_ents[t->grate] : NULL; }

/* CS03_TheoEscape */
typedef struct {
  Cutscene cs;
  Ent *theo, *tb;
  Walk walk;
  Step step;
  Co ev;
  V2 start;
  int8_t evi;
} Escape;
static bool escape_event(void *ctx, int index) {   /* StopRemovingVent, StartRemoveVent, RemoveVent, GivePhone */
  Escape *s = ST((Ent *)ctx, Escape);
  Ent *te = s->theo;
  Theo3 *t = ST(te, Theo3);
  Player *p = &g_player;
  Co *c = ev_start(&s->ev, &s->evi, index);
  switch (index) {
    case 0: {
      EV_BEGIN;
      spr_play(&t->spr, A_theo_idle, false);
      EV_WAIT(0.1f);
      t->spr.sx = -1;
      EV_END;
    }
    case 1: {
      EV_BEGIN;
      t->spr.sx = 1;
      EV_WAIT(0.1f);
      spr_play(&t->spr, A_theo_goToVent, false);
      EV_WAIT(0.25f);
      EV_END;
    }
    case 2: {
      EV_BEGIN;
      EV_WAIT(0.8f);
      spr_play(&t->spr, A_theo_fallVent, false);
      EV_WAIT(0.8f);
      if (theo3_grate(t)) grate_fall(theo3_grate(t));
      EV_WAIT(0.8f);
      t->spr.sx = -1;
      EV_WAIT(0.25f);
      EV_END;
    }
    default: {
      EV_BEGIN;
      spr_play(&t->spr, A_theo_walk, false);
      t->spr.sx = -1;
      while (te->x > p->ent->x + 24) {
        te->x -= 48 * DT;
        EV_YIELD;
      }
      spr_play(&t->spr, A_theo_idle, false);
      EV_WAIT(1);
      EV_END;
    }
  }
}
static void theo3_crawl(Ent *e, Theo3 *t) {   /* CrawlUntilOut */
  t->spr.sx = 1;
  spr_play(&t->spr, A_theo_crawl, false);
  e->tags |= TAG_GLOBAL;
  t->crawling = 1;
}
static void escape_end(Ent *e, bool skipped) {
  Escape *s = ST(e, Escape);
  Ent *te = s->theo;
  player_free();
  level_set_flag("resort_theo", true);
  save_set_flag(0), save_set_flag(1);
  if (skipped && te->cls == &THEO3) {
    Theo3 *t = ST(te, Theo3);
    te->x = s->start.x, te->y = s->start.y;
    theo3_crawl(te, t);
    if (theo3_grate(t)) ent_remove(theo3_grate(t));
  }
}
static void escape_update(Ent *e) {
  Escape *s = ST(e, Escape);
  Ent *te = s->theo;
  Theo3 *t = ST(te, Theo3);
  Player *p = &g_player;
  Co *c = &s->cs.co;
  CO_BEGIN(c);
  player_hold(p);
  s->walk = walk_to(te->x - 64);
  CO_NEST(c, player_walk(p, &s->walk));
  p->facing = 1;
  step_reset(&s->step);
  CO_NEST(c, zoom_to(&s->step, v2(240, 135), 2, 0.5f));
  s->evi = -1;
  CO_SAY(c, s->tb, !save_flag(0) ? "CH3_THEO_NEVER_MET" : !save_flag(1) ? "CH3_THEO_NEVER_INTRODUCED" : "CH3_THEO_INTRO", escape_event, e);
  t->spr.sx = 1;
  CO_WAIT(c, 0.2f);
  spr_play(&t->spr, A_theo_walk, false);
  while (!collide_solid(te, te->x + 2, te->y)) {
    CO_YIELD(c);
    te->x += 48 * DT;
  }
  spr_play(&t->spr, A_theo_idle, false);
  CO_WAIT(c, 0.2f);
  spr_play(&t->spr, A_theo_duck, false);
  CO_WAIT(c, 0.5f);
  level_set_flag("resort_theo", true);
  p->state_locked = false;
  player_set_state(p, ST_NORMAL);
  theo3_crawl(te, t);
  step_reset(&s->step);
  CO_NEST(c, zoom_back(&s->step, 0.5f));
  cutscene_end(e);
  return;
  CO_END(c);
}
static const EntClass ESCAPE = {.size = sizeof(Escape), .name = "CS03_TheoEscape", .update = escape_update};

/* NPC03_Theo_Vents' talk: Level.StartCutscene(OnTalkEnd) and its routine */
typedef struct { Cutscene cs; Ent *theo; } VentTalk;
static void vents_talk_end(Ent *e, bool skipped) {   /* OnTalkEnd */
  (void)skipped;
  Ent *te = ST(e, VentTalk)->theo;
  Player *p = level_player();
  if (p) p->dummy_auto_animate = true;
  player_free();
  level_set_flag("theoVentsTalked", true);
  if (te->cls == &THEO3) ent_remove(te);
}
static void vents_talk_update(Ent *e) {
  Ent *te = ST(e, VentTalk)->theo;
  Theo3 *t = ST(te, Theo3);
  Player *p = &g_player;
  Co *c = &ST(e, VentTalk)->cs.co;
  CO_BEGIN(c);
  CO_NEST(c, npc_approach(&t->nw, te, &t->spr, 10, -1));
  p->dummy_auto_animate = false;
  spr_play(&p->spr, A_player_lookUp, false);
  t->pan.p = -1;
  CO_NEST(c, pan_to(&t->pan, v2((float)(g_level.room->x + g_level.room->w - 320), (float)g_level.room->y), 0.5f, ease_cube_inout, false));
  step_reset(&t->step);
  CO_NEST(c, zoom_to(&t->step, v2(240, 70), 2, 0.5f));
  CO_SAY(c, t->tb, "CH3_THEO_VENTS", NULL, NULL);
  t->from = t->spr_y;   /* Disappear */
  for (t->particles = 0; t->particles < 1; t->particles += DT * 2) {
    CO_YIELD(c);
    particles_emit(PL_FG, &P_VentDust, 1, v2(te->x, te->y), v2(6, 0), P_VentDust.direction);
    t->spr_y = t->from + (-24 - t->from) * ease_back_in(t->particles);
  }
  CO_WAIT(c, 0.25f);
  step_reset(&t->step);
  CO_NEST(c, zoom_back(&t->step, 0.5f));
  cutscene_end(e);
  return;
  CO_END(c);
}
static const EntClass VENTTALK = {.size = sizeof(VentTalk), .name = "NPC03_Theo_Vents.Talk", .update = vents_talk_update};

static void theo3_appear(Ent *e, Theo3 *t) {   /* NPC03_Theo_Vents.Appear */
  Player *p = level_player();
  Co *c = &t->co;
  CO_BEGIN(c);
  if (!level_get_flag("theoVentsAppeared")) {
    {
      Ent *g = ent_new(&GRATE, e->x, e->y);
      t->grate = g ? (int16_t)(g - g_ents) : -1;
      if (g) g->collidable = 0, ST(g, Grate)->ceiling = 1, ST(g, Grate)->alpha = 1;
    }
    do {
      CO_YIELD(c);
      p = level_player();
    } while (!p || !(p->ent->x > e->x - 32));
    particles_emit(PL_FG, &P_VentDust, 24, v2(e->x, e->y), v2(6, 0), P_VentDust.direction);
    if (theo3_grate(t)) grate_fall(theo3_grate(t));
    for (t->particles = 0; t->particles < 1; t->particles += DT * 2) {
      CO_YIELD(c);
      e->visible = 1;
      t->spr_y = -24 + 16 * ease_cube_out(t->particles);
    }
    level_set_flag("theoVentsAppeared", true);
  }
  t->appeared = 1;
  t->spr_y = -8;
  e->visible = 1;
  ch1_talk_add(e, &t->talk, -16, 0, 32, 100, v2(0, -8));
  t->has_talk = 1;
  CO_END(c);
}
static void theo3_update(Ent *e) {
  Theo3 *t = ST(e, Theo3);
  Player *p = level_player();
  spr_update(&t->spr);
  if (t->has_talk && talk_update(&t->talk, e) && !t->talking) {   /* OnTalk */
    Ent *c = cs_new(&VENTTALK, vents_talk_end, true, false);
    if (c) ST(c, VentTalk)->theo = e, t->talking = 1, memset(&t->nw, 0, sizeof t->nw);
  }
  if (t->crawling) {   /* CrawlUntilOutRoutine */
    float target = (float)(g_level.room->x + g_level.room->w + 280);
    if (e->x == target) ent_remove(e);
    else e->x = approach(e->x, target, 20 * DT);
  }
  if (t->kind == TK_VENTS) {
    if (!t->appeared) {
      theo3_appear(e, t);
      if ((t->particles -= DT) <= 0 && !t->appeared) {   /* (the appear routine's timer is its own) */
      }
    }
    return;
  }
  if (p && !t->talked && p->ent->x > e->x - 100) {   /* Talk */
    t->talked = 1;
    Ent *c = cs_new(&ESCAPE, escape_end, true, false);
    if (c) ST(c, Escape)->theo = e, ST(c, Escape)->start = v2(e->x, e->y);
  }
  Ent *g = theo3_grate(t);
  if (g) ST(g, Grate)->x = (int8_t)(t->spr.anim == A_theo_pullVent && t->spr.frame > 0 ? 0 : 1);
}
static void theo3_render(Ent *e) {
  Theo3 *t = ST(e, Theo3);
  if (t->kind == TK_VENTS) {
    Sprite s = t->spr;   /* Scale (-1, -1): upside down, below its origin */
    s.sy = -1;
    spr_draw(&s, e->x, e->y + t->spr_y);
    return;
  }
  draw_npc(&t->spr, e->x - 4, e->y);
  light_add(e->x, e->y - 4, 0xFFFFFF, 1, 32, 64);
}
static const EntClass THEO3 = {.size = sizeof(Theo3), .name = "NPC03_Theo", .update = theo3_update, .render = theo3_render};
static void new_theo3(const EData *d, int kind) {
  if (kind == TK_ESCAPING ? level_get_flag("resort_theo") : level_get_flag("theoVentsTalked")) return;
  Ent *e = ent_new(&THEO3, d->x, d->y);
  if (!e) return;
  e->depth = 1000;
  ent_box(e, 8, 8, -4, -8);
  Theo3 *t = ST(e, Theo3);
  t->kind = (uint8_t)kind;
  t->grate = -1;
  spr_init(&t->spr, SB_theo);
  spr_play(&t->spr, A_theo_idle, false);
  if (kind == TK_VENTS) {
    e->tags = TAG_TRANSITION_UPDATE;
    t->spr.sx = -1;
    e->visible = 0;
    return;
  }
  for (int i = 0; i < 64 && !collide_solid(e, e->x + 1, e->y); i++) e->x++;   /* Added */
  Ent *g = ent_new(&GRATE, e->x + 4, e->y - 8);   /* Position + (Width / 2, -8) */
  if (g) {
    g->collidable = 0;
    ST(g, Grate)->rot = PI_F / 2, ST(g, Grate)->alpha = 1;
    t->grate = (int16_t)(g - g_ents);
  }
  spr_play(&t->spr, A_theo_goToVent, false);
}

/* ---------------------------------------------------------------- CS03_Memo, CS03_Diary, CS03_Guestbook */
typedef struct { Cutscene cs; Ent *tb, *page; float p; V2 from; int8_t which, index; } Read3;
static void read3_end(Ent *e, bool skipped) {
  (void)skipped;
  Read3 *s = ST(e, Read3);
  player_free();
  if (s->which == 2) {
    level_set_flag("memo_read", true);
    if (s->page && s->page->cls) ent_remove(s->page);
  }
}
static bool read3_pressed(const Button *b) { return btn_pressed(b); }
static void read3_update(Ent *e) {
  static const char *const KEYS[3] = {"CH3_DIARY", "ch3_guestbook", "ch3_memo_opening"};
  Read3 *s = ST(e, Read3);
  Player *p = &g_player;
  PageCtl *pg = s->page && s->page->cls && s->page->dead != 1 ? ST(s->page, PageCtl) : NULL;
  Co *c = &s->cs.co;
  CO_BEGIN(c);
  player_hold(p);
  if (s->which < 2 || !level_get_flag("memo_read")) {
    CO_SAY(c, s->tb, KEYS[s->which], NULL, NULL);
    CO_WAIT(c, 0.1f);
  }
  if (s->which < 2) goto done;
  s->page = ch2_page_new(true);   /* MemoPage */
  if (!s->page) goto done;
  CO_YIELD(c);
  for (s->p = 0; s->p < 1; s->p += DT) {   /* EaseIn */
    pg->pos = v2lerp(v2(960, 1180), v2(960, 390), ease_cube_out(s->p));
    pg->alpha = ease_cube_out(s->p);
    CO_YIELD(c);
  }
  for (s->from.y = pg->pos.y, s->index = 0; !read3_pressed(&g_in.back);) {   /* Wait: three places down the page */
    /* (400 down each time; more if the page is longer than the game's) */
    pg->pos.y += (s->from.y - s->index * fmaxf(400, (390 + pg->h * 6 - 1020) / 2) - pg->pos.y) * (1 - powf(0.01f, DT));
    if (g_in.aim_y < 0 && (g_in.keys & ~g_in.prev & K_UP) && s->index > 0) s->index--;
    else if (s->index < 2) {
      if (((g_in.keys & ~g_in.prev & K_DOWN)) || read3_pressed(&g_in.confirm) || read3_pressed(&g_in.jump)) s->index++;
    } else if (read3_pressed(&g_in.confirm) || read3_pressed(&g_in.jump))
      break;
    CO_YIELD(c);
  }
  pg->out = 1;   /* EaseOut */
  s->from = pg->pos;
  for (s->p = 0; s->p < 1; s->p += DT * 1.5f) {
    pg->pos = v2lerp(s->from, v2(960, -1380), ease_cube_in(s->p));
    pg->alpha = 1 - ease_cube_in(s->p);
    CO_YIELD(c);
  }
  ent_remove(s->page);
  s->page = NULL;
done:
  cutscene_end(e);
  return;
  CO_END(c);
}
static const EntClass READ3 = {.size = sizeof(Read3), .name = "CS03_Memo", .update = read3_update};
static void read3_start(Ent *trig, Player *p, int which) {
  (void)trig, (void)p;
  Ent *e = cs_new(&READ3, read3_end, true, false);
  if (e) ST(e, Read3)->which = (int8_t)which;
}

/* ---------------------------------------------------------------- ResortRoofEnding and CS03_Ending */
/* the roof's pictures (all but the first edge) wobble when Oshiro slams it; at the last slam they snap
 * and fall. phase: 0 still, 1 waiting its delay, 2 wobbling, 3 shaking until it falls, 4 falling, 5 gone */
#define ROOF_N 12
typedef struct { float x, y, rot, delay, p, amount, speed_x, speed_y, off; uint8_t tex, phase, fall; } RoofImg;
typedef struct { RoofImg img[ROOF_N]; uint8_t n, begin_falling; } Roof;
static uint16_t roof_tex[8];   /* the four centers, the edge, the snapped a, b, c */
static uint16_t roof_edge_d;
static void roof_render(Ent *e) {
  Roof *r = ST(e, Roof);
  if (roof_edge_d == 0xFFFF) return;
  V2 sh = e_shake(e);
  float x = e->x + sh.x, y = e->y + sh.y;
  gfx_tex_ex(roof_edge_d, x + 8, y + 4, 8, 12, 1, 1, 0, 0xFFFF, 255, 0);
  for (int i = 0; i < r->n; i++) {
    RoofImg *m = &r->img[i];
    Tex t;
    if (m->phase < 5 && tex_get(roof_tex[m->tex], &t))
      gfx_tex_ex(roof_tex[m->tex], x + m->x, y + m->y, t.fw / 2.f, t.fh / 2.f, 1, 1, m->rot, 0xFFFF, 255, 0);
  }
}
static bool interval_off(float t, float off) {   /* Scene.OnInterval(interval, offset) */
  float now = g_level.time_active - off;
  return floorf((now - DT) / t) < floorf(now / t);
}
static void roof_update(Ent *e) {
  Roof *r = ST(e, Roof);
  plat_update(e);
  for (int i = 0; i < r->n; i++) {   /* the WobbleImage coroutines */
    RoofImg *m = &r->img[i];
    switch (m->phase) {
      case 1:
        if (m->delay > 0) {
          m->delay -= DT;
          break;
        }
        for (int k = 0; k < 2; k++) {
          float dx = e->x + m->x - 4 + k * 8, dy = e->y + m->off + rndi(8);
          level_debris(dx, dy, '9', v2(dx, dy + 8));
        }
        m->phase = m->fall ? 3 : 2, m->p = 0, m->amount = 5;
        break;
      case 2:
        m->p += DT * 16;
        m->amount = approach(m->amount, 1, DT * 5);
        m->y = m->off + sinf(m->p) * m->amount;
        break;
      case 3:
        if (!r->begin_falling) {
          if ((m->delay -= DT) <= 0) m->y = m->off + (float)rndi(2), m->delay = 0.01f + DT;   /* yield return 0.01f */
          break;
        }
        m->tex = (uint8_t)(5 + rndi(3));
        e->collidable = 0;
        m->amount = rndf();
        m->speed_x = -24 + rndf() * 48;
        m->speed_y = -(80 + rndf() * 80);
        m->off = rndf();
        m->p = 0, m->phase = 4;
        /* fall through */
      case 4:
        m->x += m->speed_x * DT, m->y += m->speed_y * DT;
        m->rot += m->amount * ease_cube_in(m->p);
        m->speed_x = approach(m->speed_x, 0, DT * 200);
        m->speed_y += 600 * DT;
        if (interval_off(0.1f, m->off)) dust_burst(v2(e->x + m->x, e->y + m->y), -PI_F / 2, 1);
        if ((m->p += DT) >= 4) m->phase = 5;
        break;
    }
  }
}
static void roof_wobble(Ent *e, Ent *ghost, bool fall) {   /* Wobble */
  Roof *r = ST(e, Roof);
  for (int i = 0; i < r->n; i++) {
    RoofImg *m = &r->img[i];
    if (m->phase >= 4) continue;
    m->off = m->y;   /* orig */
    m->delay = fabsf(e->x + m->x - ghost->x) * 0.001f;
    m->phase = 1, m->fall = fall;
  }
}
static void roof_awake(Ent *e);
static const EntClass ROOF = {.name = "resortRoofEnding", .size = sizeof(Roof), .update = roof_update, .render = roof_render,
                              .awake = roof_awake, .kind = KIND_SOLID};
static void new_roof(const EData *d) {
  Ent *e = ent_new(&ROOF, d->x, d->y);
  if (!e) return;
  float w = EA(d, resortRoofEnding, width);
  ent_box(e, w, 2, 0, 0);
  e->safe = 1;
  Roof *r = ST(e, Roof);
  /* (decals have no run-time names: their ids) */
#if defined(T_decals_3_resort_roofEdge_d) && defined(T_decals_3_resort_roofCenter_snapped_a)
  static const uint16_t T[8] = {T_decals_3_resort_roofCenter, T_decals_3_resort_roofCenter_b, T_decals_3_resort_roofCenter_c,
                                T_decals_3_resort_roofCenter_d, T_decals_3_resort_roofEdge, T_decals_3_resort_roofCenter_snapped_a,
                                T_decals_3_resort_roofCenter_snapped_b, T_decals_3_resort_roofCenter_snapped_c};
  memcpy(roof_tex, T, sizeof roof_tex);
  roof_edge_d = T_decals_3_resort_roofEdge_d;
#else
  roof_edge_d = 0xFFFF;
#endif
  int i;
  for (i = 0; (float)i < w && r->n < ROOF_N - 1; i += 16) {
    RoofImg *m = &r->img[r->n++];
    m->x = (float)(i + 8), m->y = 4, m->tex = (uint8_t)rndi(4);
  }
  RoofImg *m = &r->img[r->n++];
  m->x = (float)(i + 8), m->y = 4, m->tex = 4;
}

/* BgFlash: black behind the level while the lightning strikes */
static void bgflash_render(Ent *e) {
  gfx_rect(g_level.cam.x - 10, g_level.cam.y - 10, 340, 200, 0, (uint8_t)(clampf(*ST(e, float), 0, 1) * 255));
}
static const EntClass BGFLASH = {.size = sizeof(float), .name = "BgFlash", .render = bgflash_render};
/* the Oshiro left on the roof: an Entity with his sprite and a light */
typedef struct { Sprite spr; float y; } Osh0;
static void osh0_update(Ent *e) { spr_update(&ST(e, Osh0)->spr); }
static void osh0_render(Ent *e) {
  Osh0 *o = ST(e, Osh0);
  draw_npc(&o->spr, e->x, e->y + o->y);
  light_add(e->x, e->y - 8, 0xFFFFFF, 1, 16, 32);
}
static const EntClass OSH0 = {.size = sizeof(Osh0), .name = "oshiro", .update = osh0_update, .render = osh0_render};
/* the player's RunPlayerRight, which goes on after the cutscene */
typedef struct { Walk run; float wait; } Runner;
static void runner_update(Ent *e) {
  Runner *r = ST(e, Runner);
  Player *p = level_player();
  if (r->wait > 0) {
    r->wait -= DT;
    if (r->wait <= 0 && p) r->run = walk_to(p->ent->x + 128), r->run.mode = RUN_TO;
    return;
  }
  if (!p || !player_walk(p, &r->run)) ent_remove(e);
}
static const EntClass RUNNER = {.size = sizeof(Runner), .name = "RunPlayerRight", .update = runner_update};

typedef struct {
  Cutscene cs;
  Ent *roof, *ghost, *tb, *bg, *osh;
  Walk walk, run;
  Step step;
  Pan pan, pan2;
  Co ev, smash;
  Tween tw;
  V2 fall, ghost_to, lpos, cam_to;
  float t, sp, from, to, ground, lrot, light_t, wait;
  int8_t evi;
  uint8_t running, moving, lightning, cam2, sm_final;
} End3;
static bool end3_ghost_to(End3 *s) {   /* MoveGhostTo */
  Ent *g = s->ghost;
  if (!g || !g->cls) return false;
  V2 d = v2sub(s->ghost_to, v2(g->x, g->y));
  float l = v2len(d), k = 64 * DT;
  if (l <= k) {
    g->x = s->ghost_to.x, g->y = s->ghost_to.y;
    return false;
  }
  g->x += d.x / l * k, g->y += d.y / l * k;
  return true;
}
/* GhostSmash(topDelay, final): a nested step */
static bool end3_smash(End3 *s, float top_delay, bool final) {
  Ent *g = s->ghost;
  Player *p = &g_player;
  Co *c = &s->smash;
  if (!g || !g->cls) return false;
  Osh *o = ST(g, Osh);
  if (c->wait > 0) {
    c->wait -= DT;
    return true;
  }
  switch (c->co) {
    case 0:
      s->from = g->y, s->to = g->y - 32;
      s->sp = 0, c->co = 1;
      /* fall through */
    case 1:
      if (s->sp < 1) {
        g->y = lerpf(s->from, s->to, ease_cube_out(s->sp));
        s->sp += DT * 2;
        return true;
      }
      c->wait = top_delay, c->co = 2;
      return true;
    case 2:
      s->ground = s->from + 20;
      s->sp = 0, c->co = 3;
      /* fall through */
    case 3:
      if (s->sp < 1) {
        g->y = lerpf(s->to, s->ground, ease_cube_out(s->sp));
        s->sp += DT * 8;
        return true;
      }
      o->sx = 1.3f, o->sy = 0.5f, o->shake_timer = 0.5f;   /* Squish */
      level_shake(0.5f);
      plat_start_shaking(s->roof, 0.5f);
      s->sp = 0, c->co = 4;
      if (final) {
        g->y = (s->ground + s->from) / 2;
        goto wobble;
      }
      /* fall through */
    case 4:
      if (s->sp < 1) {
        g->y = lerpf(s->ground, s->from, ease_cube_out(s->sp));
        s->sp += DT * 16;
        return true;
      }
    wobble:
      p->dummy_auto_animate = false;
      spr_play(&p->spr, A_player_shaking, false);
      roof_wobble(s->roof, g, final);
      c->co = 5;
      if (!final) {
        c->wait = 0.5f;
        return true;
      }
      /* fall through */
    default:
      c->co = 0;
      return false;
  }
}
static bool end3_event(void *ctx, int index) {   /* GhostSmash; then CH3_ENDING's OshiroTurns */
  End3 *s = ST((Ent *)ctx, End3);
  Co *c = ev_start(&s->ev, &s->evi, index);
  EV_BEGIN;
  if (s->osh) {
    EV_WAIT(1);
    ST(s->osh, Osh0)->spr.sx = -1;
    EV_WAIT(0.2f);
    return true;
  }
  memset(&s->smash, 0, sizeof s->smash);
  EV_NEST(end3_smash(s, 0, false));
  EV_END;
}
static void end3_end(Ent *e, bool skipped) {
  (void)e, (void)skipped;
  level_complete_area(true, false);
  g_wipe.focus = v2(192, 120);
}
static void end3_render(Ent *e) {   /* its Sprite: the lightning ("oshiro_boss_lightning", Justify (0, 0.5)) */
  End3 *s = ST(e, End3);
  if (!s->lightning) return;
  int f = (int)(s->light_t / 0.03f);
  if (f > 6) return;
  gfx_tex_ex(osh_light[f], s->lpos.x, s->lpos.y, 0, 32, 1, 1, s->lrot, 0xFFFF, 255, 0);
}
static void end3_update(Ent *e) {
  End3 *s = ST(e, End3);
  Ent *g = s->ghost;
  Player *p = &g_player;
  if (s->running && !player_walk(p, &s->run)) s->running = 0;   /* Add(new Coroutine(player.DummyRunTo(...))) */
  if (s->moving && !end3_ghost_to(s)) s->moving = 0;
  if (s->lightning) s->light_t += DT;
  if (s->tw.active && g && g->cls) {   /* the falling Oshiro's turn */
    tween_update(&s->tw);
    ST(g, Osh)->rot = ease_sine_inout(s->tw.percent) * -100 * (PI_F / 180);
  }
  if (s->cam2 && !pan_to(&s->pan2, s->cam_to, s->cam2 == 1 ? 3 : 2, ease_cube_inout, false)) s->cam2 = 0;
  Co *c = &s->cs.co;
  CO_BEGIN(c);
  player_hold(p);
  p->force_camera_update = false;
  s->run = walk_to(e_right(s->roof) - 32), s->run.mode = RUN_TO, s->run.flags = RUN_FAST, s->running = 1;
  CO_YIELD(c);
  p->dummy_auto_animate = false;
  CO_WAIT(c, 0.5f);
  for (int i = 0; i < g_nents && !s->ghost; i++)
    if (g_ents[i].cls == &OSHIRO && g_ents[i].dead != 1) s->ghost = &g_ents[i];
  g = s->ghost;
  if (g) {   /* MoveGhostTo(roof + (40, -12)): its target less half its height */
    s->ghost_to = v2(s->roof->x + 40, s->roof->y - 12 - 14);
    osh_state(g, ST(g, Osh), OS_DUMMY);
    g->collidable = 0;
    s->moving = 1;
  }
  CO_WAIT(c, 1);
  p->dummy_auto_animate = true;
  step_reset(&s->step);
  CO_NEST(c, zoom_to(&s->step, v2(130, 60), 2, 0.5f));
  p->facing = -1;
  CO_WAIT(c, 0.5f);
  s->evi = -1;
  CO_SAY(c, s->tb, "CH3_OSHIRO_CHASE_END", end3_event, e);
  memset(&s->smash, 0, sizeof s->smash);
  CO_NEST(c, end3_smash(s, 0.5f, true));
  s->bg = ent_new(&BGFLASH, 0, 0);
  if (s->bg) s->bg->depth = 10100, s->bg->collidable = 0, *ST(s->bg, float) = 1;
  if (g && g->cls) {
    s->lpos = v2add(v2(g->x, g->y), v2(140, -100));
    V2 d = v2sub(v2add(v2(g->x, g->y), v2(0, 10)), s->lpos);
    s->lrot = atan2f(d.y, d.x);
    s->lightning = 1, s->light_t = 0;
  }
  CO_YIELD(c);
  level_freeze(0.3f);
  CO_YIELD(c);
  level_shake(0.3f);
  CO_WAIT(c, 0.2f);
  ch1_flash(0xFFFFFF);
  p->dummy_gravity = false;
  if (g && g->cls) ospr_play(&ST(g, Osh)->spr, OA_BACK, false);
  spr_play(&p->spr, A_player_fall, false);
  ST(s->roof, Roof)->begin_falling = 1;
  CO_YIELD(c);
  g_time_rate = 0.01f;
  spr_play(&p->spr, A_player_fallFast, false);
  p->dummy_gravity = true;
  p->speed = v2(300, -200);
  s->fall = v2(-100, -250);
  tween_start(&s->tw, 1.5f);
  for (s->t = 0; s->t < 2; s->t += DT) {
    s->fall.x = approach(s->fall.x, 0, DT * 400);
    s->fall.y += DT * 800;
    if (g && g->cls) g->x += s->fall.x * DT, g->y += s->fall.y * DT;
    if (s->bg) *ST(s->bg, float) = approach(*ST(s->bg, float), 0, RAW_DT);
    g_time_rate = approach(g_time_rate, 1, RAW_DT * 0.6f);
    CO_YIELD(c);
  }
  level_dir_shake(v2(0, -1), 0.5f);
  CO_WAIT(c, 1);
  for (int i = 0; i < 400 && !actor_on_ground(p->ent, 1); i++) actor_move_v(p->ent, 1, NULL, NULL);
  p->dummy_auto_animate = false;
  spr_play(&p->spr, A_player_tired, false);
  if (g && g->cls) ent_remove(g);
  s->ghost = NULL;
  s->osh = ent_new(&OSH0, (float)(g_level.room->x + 110), p->ent->y);
  if (s->osh) {
    ent_box(s->osh, 8, 8, -4, -8);
    Osh0 *o = ST(s->osh, Osh0);
    spr_init(&o->spr, SB_oshiro);
    spr_play(&o->spr, A_oshiro_fall, false);
  }
  s->pan.p = -1;
  CO_NEST(c, pan_to(&s->pan, v2add(player_camera_target(p), v2(0, 40)), 1, ease_cube_out, false));
  CO_WAIT(c, 1.5f);
  CO_WAIT(c, 3);
  if (!s->osh) goto out;
  spr_play(&ST(s->osh, Osh0)->spr, A_oshiro_recover, false);
  s->to = s->osh->y + 4;
  while (s->osh->y != s->to) {
    s->osh->y = approach(s->osh->y, s->to, 6 * DT);
    CO_YIELD(c);
  }
  CO_WAIT(c, 0.6f);
  s->evi = -1;
  CO_SAY(c, s->tb, "CH3_ENDING", end3_event, e);
  s->pan2.p = -1, s->cam_to = v2add(g_level.cam, v2(-80, 0)), s->cam2 = 1;
  CO_WAIT(c, 0.5f);
  ST(s->osh, Osh0)->spr.sx = -1;
  CO_WAIT(c, 0.2f);
  for (s->t = 0; s->osh->x > g_level.room->x - 16;) {
    s->osh->x -= 40 * DT;
    s->t += DT * 2;
    ST(s->osh, Osh0)->y = sinf(s->t) * 2;
    for (int i = 0; i < g_nents; i++)
      if (g_ents[i].cls == &DOOR && g_ents[i].dead != 1 && collide_ent_at(s->osh, s->osh->x, s->osh->y, &g_ents[i])) {
        door_open(&g_ents[i], s->osh->x);
        break;
      }
    CO_YIELD(c);
  }
  s->pan2.p = -1, s->cam_to = v2add(g_level.cam, v2(80, 0)), s->cam2 = 2;
  CO_WAIT(c, 1.2f);
out:
  p->dummy_auto_animate = true;
  s->walk = walk_to(p->ent->x - 16);
  CO_NEST(c, player_walk(p, &s->walk));
  CO_WAIT(c, 2);
  p->facing = 1;
  CO_WAIT(c, 1);
  p->force_camera_update = false;
  {
    Ent *r = ent_new(&RUNNER, 0, 0);
    if (r) r->tags = TAG_PERSISTENT, r->collidable = 0, r->visible = 0, ST(r, Runner)->wait = 0.75f;
  }
  cutscene_end(e);
  return;
  CO_END(c);
}
static const EntClass END3 = {.size = sizeof(End3), .name = "CS03_Ending", .update = end3_update, .render = end3_render};
static void roof_awake(Ent *e) {
  plat_static_movers_attach(e);
  Player *p = level_player();
  if (!level_get_flag("oshiroEnding") && p) {   /* Scene.Add(new CS03_Ending(this, player)) */
    Ent *c = ent_new(&END3, 0, 0);
    if (!c) return;
    c->collidable = 0;
    c->depth = -1000000;
    ST(c, End3)->roof = e;
    level_register_complete();
    cutscene_start(c, end3_end, false, true);
  }
}

/* ---------------------------------------------------------------- LightFadeTrigger, BloomFadeTrigger */
typedef struct { float from, to; uint8_t mode, bloom; } Fade;
static int position_mode(const char *m) {
  static const char *modes[] = {"NoEffect", "LeftToRight", "RightToLeft", "TopToBottom", "BottomToTop", "HorizontalCenter", "VerticalCenter"};
  for (int i = 0; i < 7; i++)
    if (!strcmp(m, modes[i])) return i;
  return 0;
}
static float position_lerp(Ent *e, Player *p, int mode) {   /* Trigger.GetPositionLerp */
  float px = p->ent->x, py = p->ent->y;
  float l = e_left(e), r = e_right(e), t = e_top(e), b = e_bottom(e), cx = e_cxm(e), cy = e_cym(e);
  switch (mode) {
    case 1: return clamped_map(px, l, r, 0, 1);
    case 2: return clamped_map(px, r, l, 0, 1);
    case 3: return clamped_map(py, t, b, 0, 1);
    case 4: return clamped_map(py, b, t, 0, 1);
    case 5: return fminf(clamped_map(px, l, cx, 0, 1), clamped_map(px, r, cx, 0, 1));
    case 6: return fminf(clamped_map(py, t, cy, 0, 1), clamped_map(py, b, cy, 0, 1));
    default: return 1;
  }
}
static float area_darkness(void) {   /* AreaData.DarknessAlpha */
  static const float D[11] = {0.05f, 0.05f, 0.15f, 0.15f, 0.1f, 0.15f, 0.15f, 0.05f, 0.05f, 0.05f, 0.05f};
  return g_session.area < 11 ? D[g_session.area] : 0.05f;
}
static float area_bloom(void) {   /* AreaData.BloomBase */
  static const float B[11] = {0, 0, 0.5f, 0, 0.25f, 0, 0, 0, 0, 0, 0};
  return g_session.area < 11 ? B[g_session.area] : 0;
}
static void fade_stay(Ent *e, Player *p) {
  Fade *f = ST(e, Fade);
  float v = f->from + (f->to - f->from) * clampf(position_lerp(e, p, f->mode), 0, 1);
  if (f->bloom) {
    g_session.bloom_base_add = v;
    g_level.bloom = area_bloom() + v;
  } else {
    g_session.lighting_alpha_add = v;
    g_level.lighting = area_darkness() + v;
  }
}
static const EntClass FADETRIG = {.name = "fadeTrigger", .size = sizeof(Fade), .kind = KIND_TRIGGER,
                                  .more = &(const EntMore){.on_stay = fade_stay}};

/* ---------------------------------------------------------------- the factories */
bool ents_ch3(const EData *d) {
  if (!keys_restored && !level_player_ent()) {   /* a level (re)load: the session's keys */
    keys_restored = true;
    restore_keys();
  }
  switch (d->type) {
    case ET_yellowBlocks: new_clutter_area(d, CL_YELLOW, EA(d, yellowBlocks, width), EA(d, yellowBlocks, height)); return true;
    case ET_greenBlocks: new_clutter_area(d, CL_GREEN, EA(d, greenBlocks, width), EA(d, greenBlocks, height)); return true;
    case ET_redBlocks: new_clutter_area(d, CL_RED, EA(d, redBlocks, width), EA(d, redBlocks, height)); return true;
    case ET_clutterCabinet: new_cabinet(d); return true;
    case ET_colorSwitch: new_cswitch(d); return true;
    case ET_clutterDoor: new_gdoor(d, false); return true;
    case ET_oshirodoor: new_gdoor(d, true); return true;
    case ET_resortLantern: new_lantern(d); return true;
    case ET_cobweb: new_cobweb(d); return true;
    case ET_triggerSpikesUp: new_tspikes(d, TS_UP, (int)EA(d, triggerSpikesUp, width)); return true;
    case ET_triggerSpikesDown: new_tspikes(d, TS_DOWN, (int)EA(d, triggerSpikesDown, width)); return true;
    case ET_triggerSpikesLeft: new_tspikes(d, TS_LEFT, (int)EA(d, triggerSpikesLeft, height)); return true;
    case ET_triggerSpikesRight: new_tspikes(d, TS_RIGHT, (int)EA(d, triggerSpikesRight, height)); return true;
    case ET_door: new_door(d); return true;
    case ET_trapdoor: new_hatch(d); return true;
    case ET_sinkingPlatform: new_sinking(d); return true;
    case ET_movingPlatform: new_moving(d); return true;
    case ET_clothesline: new_clothesline(d); return true;
    case ET_water: new_water(d); return true;
    case ET_waterfall: new_waterfall(d); return true;
    case ET_friendlyGhost: new_oshiro(d->x, d->y, false); return true;
    case ET_resortmirror: new_rmirror(d); return true;
    case ET_picoconsole: new_pico(d); return true;
    case ET_key: new_key(d); return true;
    case ET_lockBlock: new_lock(d); return true;
    case ET_blockField: {
      Ent *e = ent_new(&BLOCKFIELD, d->x, d->y);
      if (e) ent_box(e, EA(d, blockField, width), EA(d, blockField, height), 0, 0), e->visible = 0;
      return true;
    }
    case ET_killbox: {
      Ent *e = ent_new(&KILLBOX3, d->x, d->y);
      if (e) ent_box(e, EA(d, killbox, width), 32, 0, 0), e->visible = 0, e->collidable = 0;
      return true;
    }
    case ET_resortRoofEnding: new_roof(d); return true;
    case ET_npc: {
      static const char *const NPCS[9] = {"oshiro_03_lobby", "oshiro_03_hallway", "oshiro_03_hallway2", "oshiro_03_bigroom",
                                          "oshiro_03_breakdown", "oshiro_03_suite", "oshiro_03_rooftop", "theo_03_escaping",
                                          "theo_03_vents"};
      const char *n = EAS(d, npc, npc);
      for (int i = 0; i < 9; i++)
        if (!strcmp(n, NPCS[i])) {
          if (i < 7) new_osh(d, i);
          else new_theo3(d, i - 7);
          return true;
        }
      return false;
    }
  }
  return false;
}

static void diary_start(Ent *e, Player *p) { read3_start(e, p, 0); }
static void guestbook_start(Ent *e, Player *p) { read3_start(e, p, 1); }
static void memo_start(Ent *e, Player *p) { read3_start(e, p, 2); }
bool trigs_ch3(const EData *d) {
  switch (d->type) {
    case TT_interactTrigger: {   /* CS03_Diary, CS03_Guestbook, CS03_Memo */
      static const char *const EV[3] = {"ch3_diary", "ch3_guestbook", "ch3_memo"};
      static void (*const FN[3])(Ent *e, Player *p) = {diary_start, guestbook_start, memo_start};
      const char *ev = EAS(d, interactTrigger, event);
      for (int i = 0; i < 3; i++)
        if (!strcmp(ev, EV[i])) {
          ch2_interact_new(d, FN[i]);
          return true;
        }
      return false;
    }
    case TT_oshiroTrigger: {
      Ent *e = ent_new(&OSHTRIG, d->x, d->y);
      if (e) ent_box(e, EA(d, oshiroTrigger, width), EA(d, oshiroTrigger, height), 0, 0), e->visible = 0,
             ST(e, OshTrig)->state = EAB(d, oshiroTrigger, state);
      return true;
    }
    case TT_lightFadeTrigger:
    case TT_bloomFadeTrigger: {
      bool bloom = d->type == TT_bloomFadeTrigger;
      Ent *e = ent_new(&FADETRIG, d->x, d->y);
      if (!e) return true;
      e->visible = 0;
      Fade *f = ST(e, Fade);
      f->bloom = bloom;
      if (bloom) {
        ent_box(e, EA(d, bloomFadeTrigger, width), EA(d, bloomFadeTrigger, height), 0, 0);
        f->from = EA(d, bloomFadeTrigger, bloomAddFrom), f->to = EA(d, bloomFadeTrigger, bloomAddTo);
        f->mode = (uint8_t)position_mode(EAS(d, bloomFadeTrigger, positionMode));
      } else {
        ent_box(e, EA(d, lightFadeTrigger, width), EA(d, lightFadeTrigger, height), 0, 0);
        f->from = EA(d, lightFadeTrigger, lightAddFrom), f->to = EA(d, lightFadeTrigger, lightAddTo);
        f->mode = (uint8_t)position_mode(EAS(d, lightFadeTrigger, positionMode));
        e->tags = TAG_TRANSITION_UPDATE;
      }
      return true;
    }
  }
  return false;
}

/* this chapter's tables: in the memory it is given while it is played (res_chapter_ram) */
uint32_t ch3_ram(void) { return g_session.area == 3 ? END_clut_off : END_ts_state; }   /* the clutter: the Resort's */
