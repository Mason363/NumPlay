/* Cat Ninja on the NumWorks calculator: Richman Stewart's Flash platformer (TrackMill, 2013), an app of its own.
 * The game (game.c, env.c) is the original's code in C, drawn with its own pictures and fonts (draw.c,
 * data.c). This file has the keys, the pause, the How to play page and the saves.
 *
 * Keys: Left and Right run, Up, OK or EXE jump (and fly the jetpack up), Down rolls and slams (and flies it
 * down), Shift turns the jetpack on and off (the original's space), Backspace goes back to the checkpoint
 * (its R). Back pauses (Resume, Retry, Title, How to play, Quit game, asked first). Home saves and quits.
 * The game runs at the original's 60 frames a second whatever the calculator manages to draw. */
#include <stddef.h>
#include <string.h>
#include "cn.h"
#include "gfx.h"
#include "plat.h"

#define SAVE_NAME "catninja.sav"
#define COPY_NAME "catninja_saves.py"
#define MAGIC 0x314E5443u /* "CTN1" */
#define VERSION 1

/* ---------------------------------------------------------------- the save */
typedef struct {
  uint32_t magic;
  uint8_t version, flags, collected, pad; /* flags: 1 How to play seen */
  int8_t spawnlevel, level;               /* the checkpoint's level; the level the cat was in (-1: none) */
  int16_t spawnx, spawny, spawnrot, x, y, rot;
  uint32_t deaths;
  uint8_t crystals[(CN_CRYSTALS + 7) / 8];
  uint32_t sum;
} save_t;
static save_t sv;

static uint32_t fnv(const void *d, uint32_t n) {
  const uint8_t *p = d;
  uint32_t h = 2166136261u;
  for (uint32_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
  return h;
}
static bool save_ok(const save_t *s) {
  if (s->magic != MAGIC || s->version != VERSION || s->sum != fnv(s, offsetof(save_t, sum))) return false;
  if (s->spawnlevel < -1 || s->spawnlevel > 29 || s->level < -1 || s->level > 29) return false;
  int r[2] = {s->spawnrot, s->rot};
  for (int k = 0; k < 2; k++)
    if (r[k] != 0 && r[k] != 90 && r[k] != -90 && r[k] != 180) return false;
  if (s->collected > CN_CRYSTALS) return false;
  return true;
}
static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
#define TAG "#>" SAVE_NAME ":" /* the copy's line */
static const char head[] = "# Cat Ninja's saves: keep this file\n" TAG;
static uint8_t copy[1 + sizeof head + (sizeof(save_t) + 2) / 3 * 4 + 2];

static void sync_out(void) {
  extern void game_get_resume(int *lvl, int *x, int *y, int *rot);
  int lvl, x, y, rot;
  game_get_resume(&lvl, &x, &y, &rot);
  sv.level = (int8_t)lvl, sv.x = (int16_t)x, sv.y = (int16_t)y, sv.rot = (int16_t)rot;
  sv.spawnlevel = (int8_t)P.spawnlevel, sv.spawnx = (int16_t)P.spawnx, sv.spawny = (int16_t)P.spawny;
  sv.spawnrot = (int16_t)P.spawnrotation;
  sv.deaths = (uint32_t)pdeaths, sv.collected = (uint8_t)collected;
  memcpy(sv.crystals, crystals_got, sizeof sv.crystals);
}
static void save(void) {
  sync_out();
  sv.magic = MAGIC, sv.version = VERSION;
  sv.sum = fnv(&sv, offsetof(save_t, sum));
  plat_save(SAVE_NAME, &sv, sizeof sv);
  uint8_t *p = copy;
  const uint8_t *f = (const uint8_t *)&sv;
  *p++ = 0; /* (a script's first byte: not imported) */
  memcpy(p, head, sizeof head - 1), p += sizeof head - 1;
  for (uint32_t i = 0; i < sizeof sv; i += 3, p += 4) {
    uint32_t left = sizeof sv - i;
    uint32_t v = (uint32_t)f[i] << 16 | (left > 1 ? (uint32_t)f[i + 1] << 8 : 0) | (left > 2 ? f[i + 2] : 0);
    p[0] = (uint8_t)b64[v >> 18 & 63], p[1] = (uint8_t)b64[v >> 12 & 63];
    p[2] = left > 1 ? (uint8_t)b64[v >> 6 & 63] : '=', p[3] = left > 2 ? (uint8_t)b64[v & 63] : '=';
  }
  *p++ = '\n';
  *p++ = 0; /* (a script's text ends with a zero) */
  plat_save(COPY_NAME, copy, (uint32_t)(p - copy));
}
static int b64v(uint8_t c) {
  for (int i = 0; i < 64; i++)
    if ((uint8_t)b64[i] == c) return i;
  return c == '=' ? 0 : -1;
}
static bool copy_read(save_t *out) {
  uint32_t n;
  const uint8_t *c = plat_load(COPY_NAME, &n);
  if (!c) return false;
  const uint32_t hl = sizeof TAG - 1;
  for (uint32_t i = 0; i + hl <= n; i++) {
    if (memcmp(c + i, head + sizeof head - sizeof TAG, hl)) continue;
    const uint8_t *l = c + i + hl;
    uint32_t m = 0;
    while (i + hl + m < n && b64v(l[m]) >= 0 && l[m] != '\n') m++;
    if (m % 4 || m != (sizeof(save_t) + 2) / 3 * 4) return false;
    static uint8_t buf[(sizeof(save_t) + 2) / 3 * 3];
    for (uint32_t k = 0; k < m; k += 4) {
      uint32_t w = (uint32_t)(b64v(l[k]) << 18 | b64v(l[k + 1]) << 12 | b64v(l[k + 2]) << 6 | b64v(l[k + 3]));
      buf[k / 4 * 3] = (uint8_t)(w >> 16), buf[k / 4 * 3 + 1] = (uint8_t)(w >> 8), buf[k / 4 * 3 + 2] = (uint8_t)w;
    }
    memcpy(out, buf, sizeof *out);
    return save_ok(out);
  }
  return false;
}
static void load(void) {
  uint32_t n;
  const uint8_t *d = plat_load(SAVE_NAME, &n);
  static save_t t;
  memset(&sv, 0, sizeof sv);
  sv.level = sv.spawnlevel = -1;
  if (d && n == sizeof t) {
    memcpy(&t, d, sizeof t);
    if (save_ok(&t)) {
      sv = t;
      return;
    }
  }
  if (copy_read(&t)) sv = t;
}
/* the game as the save left it: the title map, the way right leading back to the cat */
static void start_game(void) {
  game_new();
  pdeaths = (int)sv.deaths, collected = sv.collected;
  memcpy(crystals_got, sv.crystals, sizeof crystals_got);
  if (sv.level >= 0) {
    game_resume_at(sv.level, sv.x, sv.y, sv.rot);
    if (sv.spawnlevel >= 0)
      P.spawnlevel = sv.spawnlevel, P.spawnx = sv.spawnx, P.spawny = sv.spawny, P.spawnrotation = sv.spawnrot;
  }
  info_update(true);
}

/* ---------------------------------------------------------------- the pause */
enum { OV_NONE, OV_PAUSE, OV_QUIT, OV_HELP };
static int ov = OV_NONE, sel, help_back;
#define N_ITEMS 5
static const char *const items[N_ITEMS] = {"Resume", "Retry", "Title", "How to play", "Quit game"};
static bool item_on(int i) { return i != 2 || game_can_exit(); }

/* ---------------------------------------------------------------- drawing */
extern void draw_init(void), draw_frame(void), draw_world(void);
static void text_c(const np_font_t *f, int cx, int base, const char *s, color_t c) { gfx_text_center(f, cx, base, s, c, 256); }
static void keyline(const char *s) {
  gfx_fill_alpha(0, 222, 320, 18, 0, 170);
  text_c(&np_font_small, 160, 235, s, HEX(0xD8D0E8));
}
static void box(int x, int y, int w, int h) {
  gfx_rrect(x, y, w, h, 8, HEX(0x1A1030), 235);
  gfx_rrect_outline(x, y, w, h, 8, 2, HEX(0xB95BEC), 256);
}
static void button(int x, int y, int w, int h, const char *s, bool on, bool can) {
  if (on) gfx_rrect(x, y, w, h, 6, HEX(0xE473EE), 256);
  else gfx_rrect_outline(x, y, w, h, 6, 1, HEX(0x7C6A90), 256);
  text_c(&np_font_body, x + w / 2, y + h / 2 + 5, s, on ? HEX(0x101010) : can ? HEX(0xFFFFFF) : HEX(0x6A6078));
}
static const char *const help[] = {
    "Collect the energy crystals and get to the end.",
    "Left / Right: run",
    "Up, OK or EXE: jump (again in the air: double jump)",
    "Jump against a wall: wall jump",
    "Down: roll (hold it and run); in the air: slam",
    "A slam breaks glass and presses red buttons.",
    "Shift: jetpack on or off (once you have one)",
    "Backspace: back to the last checkpoint",
    "Back: pause    Home: save and quit",
};
static void scene(void *ctx) {
  (void)ctx;
  if (ov == OV_HELP) {
    gfx_vgrad(0, 0, 320, 240, 0x2A1450, 0x0C0618);
    text_c(&np_font_title, 160, 34, "How to play", HEX(0xE473EE));
    for (int i = 0; i < (int)(sizeof help / sizeof help[0]); i++)
      text_c(&np_font_small, 160, 62 + i * 18 + (i > 0) * 6, help[i], i ? HEX(0xFFFFFF) : HEX(0x12DFF5));
    keyline("OK or Back: close");
    return;
  }
  draw_world();
  if (ov == OV_PAUSE || ov == OV_QUIT) {
    gfx_fill_alpha(0, gfx_y0, 320, gfx_y1 - gfx_y0, 0, 102);
    box(80, 36, 160, 168);
    text_c(&np_font_title, 160, 62, "Paused", HEX(0xFFFFFF));
    for (int i = 0; i < N_ITEMS; i++)
      button(96, 72 + i * 25, 128, 21, i == N_ITEMS - 1 && ov == OV_QUIT ? "Sure? OK quits" : items[i], sel == i,
             item_on(i));
    keyline("Up Down: choose   OK: go   Back: resume");
    return;
  }
  if (level == -1) keyline("Left Right: run   Up: jump   Down: roll   Back: pause");
  else if (key_hint_frames > 0 && P.hasJetPack) keyline("Shift: jetpack on or off   Up Down: fly");
}

/* ---------------------------------------------------------------- the loop */
#ifndef HOST
__attribute__((used, externally_visible))
#endif
uint32_t perf_frames, perf_max, perf_slow;
static color_t strip[SCREEN_W * STRIP_H];
static uint32_t game_keys_of(uint32_t k) { /* the original's keys: left, right, up, down, space, R */
  uint32_t g = 0;
  if (k & PK_LEFT) g |= 1 << GK_LEFT;
  if (k & PK_RIGHT) g |= 1 << GK_RIGHT;
  if (k & (PK_UP | PK_OK | PK_EXE)) g |= 1 << GK_UP;
  if (k & PK_DOWN) g |= 1 << GK_DOWN;
  if (k & PK_SHIFT) g |= 1 << GK_JET;
  if (k & PK_DEL) g |= 1 << GK_R;
  return g;
}

int main(void) {
  plat_begin();
  gfx_buf = strip;
  rng ^= plat_random() ^ plat_millis();
  if (!rng) rng = 1;
  draw_init();
  load();
  start_game();
#ifdef PERF_LEVEL /* (make perf: a level straight away) */
  {
    extern void game_debug_goto(int lv);
    game_debug_goto(PERF_LEVEL), sv.flags |= 1;
#ifdef PERF_TP /* (and the cat on that teleporter of the level) */
    extern int game_debug_teleports(int out[16][4]);
    for (int i = 0; i < 30; i++) game_step();
    int t[16][4];
    if (game_debug_teleports(t) > PERF_TP) P.x = t[PERF_TP][0] * 20 + 10, P.y = t[PERF_TP][1] * 20 + 10, P.speedx = 0;
#endif
  }
#endif
  if (!(sv.flags & 1)) ov = OV_HELP, help_back = OV_NONE, sv.flags |= 1;
  uint32_t held = plat_keys(), stale = held, gheld = 0, last = plat_millis(), saved = last, acc = 0;
  int up_held = 0, spawn_seen = P.spawnx * 7 + P.spawny * 3 + P.spawnlevel;
  for (;;) {
    uint32_t k = plat_keys();
    if (k & PK_HOME) break;
    stale &= k;
    k &= ~stale; /* (keys still down from before the app wait to be let go) */
    uint32_t now = plat_millis(), dt = now - last, hit = k & ~held;
    last = now;
    held = k;
    if (dt > 200) dt = 200;
    bool quit = false;
    int dy = !!(hit & PK_DOWN) - !!(hit & PK_UP);
    if (ov == OV_HELP) {
      if (hit & (PK_OK | PK_EXE | PK_BACK)) ov = help_back, save();
    } else if (ov == OV_PAUSE) {
      do sel = (sel + dy + N_ITEMS) % N_ITEMS;
      while (dy && !item_on(sel));
      if (hit & PK_BACK || (hit & (PK_OK | PK_EXE) && sel == 0)) ov = OV_NONE;
      else if (hit & (PK_OK | PK_EXE) && sel == 1) ov = OV_NONE, key_press(GK_R), key_release(GK_R);
      else if (hit & (PK_OK | PK_EXE) && sel == 2 && game_can_exit()) ov = OV_NONE, game_exit_to_menu(), save();
      else if (hit & (PK_OK | PK_EXE) && sel == 3) ov = OV_HELP, help_back = OV_PAUSE;
      else if (hit & (PK_OK | PK_EXE) && sel == 4) ov = OV_QUIT;
    } else if (ov == OV_QUIT) {
      if (dy) sel = (sel + dy + N_ITEMS) % N_ITEMS, ov = OV_PAUSE;
      else if (hit & (PK_OK | PK_EXE)) quit = true;
      else if (hit & PK_BACK) ov = OV_PAUSE;
    } else if (hit & PK_BACK) {
      ov = OV_PAUSE, sel = 0;
      save();
    }
    if (quit) break;
#ifdef HOST
    acc = 1000; /* (tests: one step a frame) */
    (void)dt;
#else
    acc += dt * 60;
#endif
    int steps = 0;
    while (ov == OV_NONE && acc >= 1000 && steps < 4) {
      acc -= 1000, steps++;
      /* the original's key events, then its frame; a key held through a respawn comes back as its key repeat would */
      uint32_t g = game_keys_of(k), on = g & ~gheld, off = gheld & ~g;
      for (int i = 0; i < 6; i++) {
        if (off >> i & 1) key_release(i);
        if (on >> i & 1) key_press(i);
      }
      gheld = g;
      up_held = g >> GK_UP & 1 ? up_held + 1 : 0;
      if ((g >> GK_LEFT & 1) && !key_left) key_press(GK_LEFT);
      if ((g >> GK_RIGHT & 1) && !key_right) key_press(GK_RIGHT);
      if ((g >> GK_DOWN & 1) && !key_down) key_press(GK_DOWN);
      if (up_held > 30 && !key_up) key_press(GK_UP);
      game_step();
    }
    if (ov != OV_NONE) acc = 0;
    if (acc > 4000) acc = 0;
    int spawn_now = P.spawnx * 7 + P.spawny * 3 + P.spawnlevel;
    if (spawn_now != spawn_seen && !P.dead) spawn_seen = spawn_now, save(), saved = now; /* a checkpoint */
    if (now - saved > 60000) save(), saved = now;
    uint32_t t0 = plat_millis();
    draw_frame();
    gfx_render(scene, NULL, 0, 240);
    uint32_t t = plat_millis() - t0;
    perf_frames++, perf_max = t > perf_max ? t : perf_max, perf_slow += t > 33;
#ifndef HOST
    uint32_t spent = plat_millis() - now;
    if (spent < 16) plat_sleep(16 - spent);
#endif
  }
  save();
  return plat_end();
}
