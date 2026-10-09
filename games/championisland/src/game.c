/* The scene manager, input and scoring (see game.h). */
#include "game.h"
#include <math.h>
#include <stdio.h>
#include "font.h"
#include "gfx.h"
#include "phys.h"
#include "spr.h"

Input in;
Game game;
bool game_running = true;

/* the big memory: the background layer (if the scene has one) and the sprite cache. On the calculator, the RAM after
 * the game's own (_heap_start to _heap_end), ARENA_BYTES of it at most: software 25.2 and newer gives apps 153676
 * bytes of RAM, 23.2 and 24 give 148928 and some builds of it less, and the sprite cache takes what there is */
#ifdef HOST
static uint8_t arena_mem[ARENA_BYTES] __attribute__((aligned(4)));
#define arena arena_mem
static uint32_t arena_size(void) { return ARENA_BYTES; }
#else
extern char _heap_start[], _heap_end[];
#define arena ((uint8_t *)(((uintptr_t)_heap_start + 7) & ~(uintptr_t)7))
static uint32_t arena_size(void) {
  uint32_t n = (uint32_t)((uintptr_t)_heap_end - (uintptr_t)arena);
  return n < ARENA_BYTES ? n : ARENA_BYTES;
}
#endif

/* the arena: the scene's state, then its background layer, then the sprite cache */
static uint32_t state_bytes;
static struct { int w, h; uint8_t sheet; BgPaint paint; } bg_def;

static void relayout(void) {
  uint32_t bg = bg_def.w > 0 && bg_def.h > 0 ? (uint32_t)(bg_def.w * bg_def.h + 3) & ~3u : 0;
  uint32_t size = arena_size();
  if (state_bytes + bg + MIN_CACHE > size) bg = 0;
  if (bg) bg_setup(arena + state_bytes, bg_def.w, bg_def.h, bg_def.sheet, bg_def.paint); else bg_off();
  spr_setup(arena + state_bytes + bg, size - state_bytes - bg);
}

void mem_layout(int bg_w, int bg_h, uint8_t sheet, BgPaint paint) {
  bg_def.w = bg_w;
  bg_def.h = bg_h;
  bg_def.sheet = sheet;
  bg_def.paint = paint;
  relayout();
}

void *scene_state(uint32_t size) {
  size = (size + 7) & ~7u;
  if (size > SCENE_STATE_MAX) size = SCENE_STATE_MAX;
  state_bytes = size;
  memset(arena, 0, size);
  relayout();
  return arena;
}

/* ---------------------------------------------------------------- input */
static uint32_t prev_keys;
/* OK or Back still down from a press a scene or an overlay already used: the
 * key is up for the game until it is let go (its release would otherwise
 * count: an island door opens when OK is let go) */
static bool latched[A_COUNT];

void input_latch(void) {
  latched[A_ACTION] = (prev_keys & K_ACTION) != 0;
  latched[A_BACK] = (prev_keys & K_BACK) != 0;
  for (int a = A_ACTION; a <= A_BACK; a++)
    if (latched[a]) in.held[a] = in.pressed[a] = in.released[a] = false;
}

void input_tick(void) {
  uint32_t k = plat_keys();
  float x = (float)((k & K_RIGHT ? 1 : 0) - (k & K_LEFT ? 1 : 0)), y = (float)((k & K_DOWN ? 1 : 0) - (k & K_UP ? 1 : 0));
  float len = sqrtf(x * x + y * y);
  if (len > 1) { x /= len; y /= len; }
  in.jx = x;
  in.jy = y;
  bool now[A_COUNT] = {x < -.1f, x > .1f, y < -.1f, y > .1f, (k & K_ACTION) != 0, (k & K_BACK) != 0};
  for (int i = 0; i < A_COUNT; i++) {
    if (latched[i]) {
      if (!now[i]) latched[i] = false;
      now[i] = false;
    }
    in.pressed[i] = now[i] && !in.held[i];
    in.released[i] = !now[i] && in.held[i];
    in.held[i] = now[i];
  }
  in.home = (k & K_HOME) && !(prev_keys & K_HOME);
  in.pause = (k & K_PAUSE) && !(prev_keys & K_PAUSE);
  prev_keys = k;
}

void input_consume(int a) {
  in.pressed[a] = false;
  in.released[a] = false;
}

/* ---------------------------------------------------------------- scoring */
static const ScoreRule rules[] = {
  {"archery", false, -4000, -2000, 0},
  {"climbing", false, 100, 200, 300}, {"climbing:hard", false, 100, 200, 300},
  {"marathon", true, 100, 200, 300}, {"marathon:400m", true, 100, 200, 300}, {"marathon:800m", true, 100, 200, 300},
  {"marathon:1500m", true, 100, 200, 300}, {"marathon:5000m", true, 100, 200, 300},
  {"pingpong", false, 25, 15, 1}, {"pingpong:game", false, 25, 15, 1}, {"pingpong:tutorial", false, 3, 2, 1},
  {"pingpong:hard", false, 50, 40, 30}, {"pingpong:ultra", false, 100, 90, 60},
  {"rugby", false, 5000, 1400, 800},
  {"skate", false, 3500, 2000, 800}, {"skate:park1", false, 3500, 2000, 800}, {"skate:park2", false, 20000, 10000, 5000},
  {"skate:park3", false, 30000, 20000, 10000},
  {"swim", false, 5000, 2000, 1000}, {"swim:ballad", false, 35000, 20000, 10000}, {"swim:disco", false, 35000, 20000, 10000},
  {"swim:rock", false, 35000, 20000, 10000},
};

const ScoreRule *score_rule(const char *key) {
  for (unsigned i = 0; i < sizeof rules / sizeof rules[0]; i++)
    if (!strcmp(rules[i].key, key)) return &rules[i];
  return NULL;
}

/* the doodle's Ao: 3 = gold ... 0 = none */
int score_rating(const char *key, float g) {
  const ScoreRule *r = score_rule(key);
  if (!r) return 0;
  /* Ao: the doodle compares the rule itself with "time", never true, so every
   * score is rated as points (bigger is better) */
  return g > r->gold ? 3 : g > r->silver ? 2 : g > r->bronze ? 1 : 0;
}

float score_best(const char *key, bool *has) {
  char k[40];
  snprintf(k, sizeof k, "%s_score", key);
  Value v = store_get(k);
  *has = v.type == SV_NUM;
  return v.type == SV_NUM ? v.num : 0;
}

int rating_of(const char *g) {
  char k[40];
  snprintf(k, sizeof k, "%s_rating", g);
  return (int)store_num(k, 0);
}

/* ---------------------------------------------------------------- scenes */
/* weak, so a test build can leave scenes out (make host SPORTS=...) */
extern const SceneDef scene_overworld, scene_interior, scene_cutscene, scene_video;
extern const SceneDef scene_archery __attribute__((weak)), scene_climbing __attribute__((weak)),
    scene_marathon __attribute__((weak)), scene_pingpong __attribute__((weak)), scene_rugby __attribute__((weak)),
    scene_skate __attribute__((weak)), scene_swim __attribute__((weak));
static const SceneDef *const scenes[] = {&scene_overworld, &scene_interior, &scene_archery, &scene_climbing, &scene_marathon,
                                         &scene_pingpong, &scene_rugby, &scene_skate, &scene_swim, &scene_cutscene, &scene_video};

static const char *const sports[] = {"archery", "climbing", "marathon", "pingpong", "rugby", "skate", "swim"};
bool game_is_sport(const char *name) {
  for (unsigned i = 0; i < 7; i++)
    if (!strcmp(sports[i], name)) return true;
  return false;
}
bool game_is_world(const char *name) { return !strcmp(name, "overworld") || !strcmp(name, "interior"); }

#ifdef HOST
#include <stdlib.h>
extern uint32_t host_time;
#endif
static char pending[80];
static bool has_pending;

void game_go(const char *spec) {
  snprintf(pending, sizeof pending, "%s", spec);
  has_pending = true;
}

void game_replay(void) {
  char s[80];
  snprintf(s, sizeof s, "%s%s%s", game.name, game.variant[0] ? ":" : "", game.variant);
  game_go(s);
}

static void switch_scene(void) {
  has_pending = false;
  char name[16] = "", variant[24] = "", location[32] = "";
  /* "name:variant@location" */
  const char *at = strchr(pending, '@'), *colon = strchr(pending, ':');
  size_t nl = strcspn(pending, ":@");
  memcpy(name, pending, nl < sizeof name ? nl : sizeof name - 1);
  if (colon && (!at || colon < at)) {
    size_t vl = (size_t)((at ? at : pending + strlen(pending)) - colon - 1);
    memcpy(variant, colon + 1, vl < sizeof variant ? vl : sizeof variant - 1);
  }
  if (at) snprintf(location, sizeof location, "%s", at + 1);
  const SceneDef *def = NULL;
  for (unsigned i = 0; i < sizeof scenes / sizeof scenes[0]; i++)
    if (scenes[i] && !strcmp(scenes[i]->name, name)) def = scenes[i];
  if (!def) return;
#ifdef HOST
  if (getenv("CI_LOG")) fprintf(stderr, "t%u scene %s\n", host_time / 33, pending);
#endif
  if (game.def && game.def->end) game.def->end();
  if (game.root) node_free(game.root);
#ifdef HOST
  if (getenv("CI_LEAK")) {   /* nodes nobody holds: not under a root (the HUD, the dialogue box, the menus) */
    extern NodeId node_first_used(NodeId after);
    int n = 0, orphans = 0;
    for (NodeId i = node_first_used(0); i; i = node_first_used(i)) {
      n++;
      NodeId r = i;
      while (nodes[r].parent) r = nodes[r].parent;
      if (nodes[r].sym != S_hud_Ap && nodes[r].sym != S_dialog_Spa && nodes[r].sym != S_menus_Obb) {
        orphans++;
        if (orphans <= 3) {
          fprintf(stderr, "  leaked %u sym %u:", i, nodes[i].sym);
          for (NodeId q = i; q; q = nodes[q].parent) fprintf(stderr, " %u(sym %u used %d)", q, nodes[q].sym, (nodes[q].flags & NF_USED) != 0);
          fprintf(stderr, "\n");
        }
        if (nodes[i].parent == 0) {
          int sub = 0;
          for (NodeId j = node_first_used(0); j; j = node_first_used(j)) {
            NodeId q = j;
            while (nodes[q].parent) q = nodes[q].parent;
            if (q == i) sub++;
          }
          fprintf(stderr, "  leaked root %u sym %u kind %u first %u (sym %u) nodes %d\n", i, nodes[i].sym, nodes[i].kind, nodes[i].first,
                  nodes[i].first ? nodes[nodes[i].first].sym : 0, sub);
        }
      }
    }
    fprintf(stderr, "t%u switch to %s: %d nodes used, %d not under a root\n", host_time / 33, pending, n, orphans);
  }
#endif
  toast_style("", 0, -1, -1);          /* Hq: a new scene takes the banner away */
  /* and its own physics world (the doodle's cannonWorld is the scene's): the
   * island's walls near the player, bodies of no entity, used to stay and fill
   * it up after a few doors, leaving the player without a body */
  phys_reset();
  state_bytes = 0;
  mem_layout(0, 0, 0, NULL);
  memset(&in.pressed, 0, sizeof in.pressed);
  input_latch();
  game.def = def;
  snprintf(game.name, sizeof game.name, "%s", name);
  snprintf(game.variant, sizeof game.variant, "%s", variant);
  snprintf(game.location, sizeof game.location, "%s", location);
  game.root = node_new(NK_CONT);
  game.enabled = true;
  game.paused = false;
  game.ticks = 0;
  game.fade = 10;
  gfx_view(0, VIEW_H);                 /* the stage; the island asks for more */
  def->start();
  if (variant[0] && def->goto_frame) def->goto_frame(variant);
  store_save();
}

void game_tick(void) {
  if (has_pending) switch_scene();
  input_tick();
  if (in.home) { game_quit(); return; }
  dialog_tick();
  menus_tick();
  hud_tick();
  bool overlay = dialog_active() || menus_active();
  in.enabled = !overlay;
  if (!game.paused && !overlay) node_tick(game.root);
  if (game.def && game.enabled && !overlay && game.def->tick) game.def->tick();
  node_update(game.root);
  game.ticks++;
  if (game.fade > 0) game.fade--;
  /* progress is saved as it changes, at most every few seconds */
  static uint32_t last_save;
  if (store_dirty() && game.ticks - last_save > 90) {
    store_save();
    last_save = game.ticks;
  }
}

/* ---------------------------------------------------------------- the banner */
/* To(): one message at a time at (480, 151) on the stage, in PixelMplus at
 * `size` px over a shadow (alpha .8, size / 12 lower) and an outline, in from
 * the right in 400 ms (cubicOut), still for 2 s, out to the left in 300 ms
 * (cubicIn); a new one takes the place of the last */
static struct { char text[64]; int t, size; int32_t shadow, outline, color; } ban = {"", -1, 0, -1, -1, 0xffffff};
void toast_full(const char *t, int size, int32_t shadow, int32_t outline, int32_t color) {
  snprintf(ban.text, sizeof ban.text, "%s", t ? t : "");
  ban.t = ban.text[0] ? 0 : -1;
  ban.size = size;
  ban.shadow = shadow;
  ban.outline = outline;
  ban.color = color;
}
void toast_style(const char *t, int size, int32_t shadow, int32_t outline) { toast_full(t, size, shadow, outline, 0xffffff); }
void toast(const char *t) { toast_style(t, 80, 0x111111, 0x555555); }
void toast_countdown(const char *t) { toast_style(t, 100, 0x222222, 0xaaaaaa); }

static uint16_t hex565(int32_t c) { return rgb565((uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c); }

static void banner_draw(void) {
  if (ban.t < 0) return;
  enum { IN = 12, HOLD = 60, OUT = 9 };
  int t = ban.t++;
  float off = 0;
  if (t < IN) { float u = 1 - t / (float)IN; off = 1000 * u * u * u; }
  else if (t >= IN + HOLD) { float u = (t - IN - HOLD) / (float)OUT; off = -1000 * u * u * u; }
  if (t >= IN + HOLD + OUT) { ban.t = -1; return; }
  int k3 = (ban.size + 5) / 10;
  Mat m = MAT_ID;
  m.tx = floorf((480 + off) / 3 + .5f);
  m.ty = floorf((540 * .28f - ban.size / 2.0f) / 3 + .5f) - k3 / 3;   /* the canvas sets the font a pixel higher */
  if (ban.shadow >= 0) {
    Mat s = m;
    s.ty += floorf(ban.size / 12.0f / 3 + .5f);
    gfx_text_k3(ban.text, s, hex565(ban.shadow), 1, 0, 0, 204, k3);
  }
  if (ban.outline >= 0) {
    static const int8_t o[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    for (int i = 0; i < 4; i++) {
      Mat q = m;
      q.tx += o[i][0];
      q.ty += o[i][1];
      gfx_text_k3(ban.text, q, hex565(ban.outline), 1, 0, 0, 255, k3);
    }
  }
  gfx_text_k3(ban.text, m, hex565(ban.color), 1, 0, 0, 255, k3);
}

void game_draw(void) {
  gfx_begin();
  if (game.def && game.def->draw_under) game.def->draw_under();
  node_draw(game.root, (Mat){1.0f / 3, 0, 0, 1.0f / 3, 0, 0});   /* the stage is 960 x 540 */
  if (game.def && game.def->draw_over) game.def->draw_over();
  hud_draw();
  dialog_draw();
  menus_draw();
  banner_draw();
  int top = gfx_view_top(), h = gfx_view_bottom() - top;
  if (game.fade > 0 && game.fade <= 8) gfx_rect(0, top, VIEW_W, h, 0, (uint8_t)(game.fade * 255 / 8));
  else if (game.fade > 8) gfx_rect(0, top, VIEW_W, h, 0, 255);
  gfx_end();
}

void game_quit(void) {
  store_save();
  game_running = false;
}
