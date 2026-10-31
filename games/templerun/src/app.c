#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")   /* screens over the game: smaller over faster */
#endif
/* The screens and the way between them (app.h): how to play, the title, the run, pause, the results, the store,
 * the objectives. */
#include <string.h>
#include "app.h"
#include "draw.h"
#include "game.h"
#include "plat.h"
#include "save.h"
#include "scene.h"

enum { S_HELP, S_TITLE, S_RUN, S_PAUSE, S_QUIT, S_RESULTS, S_STORE, S_OBJECTIVES };
bool app_running = true;
static int state, help_back;    /* the screen; where How to play goes back to */
static int sel, top;            /* the button or line in focus, the first line shown */
static float state_t;
static bool new_best;
static int awarded[4], nawarded, awarded_all;
static char note[40];
static float note_t;

#define GOLD 0xFFE7A0
#define INK 0x2E1C0C
#define DIM 0xC9B48A

static void fmt(char *out, uint32_t v) {
  char t[16];
  int n = 0;
  do {
    if (n % 4 == 3) t[n++] = ',';
    t[n++] = (char)('0' + v % 10), v /= 10;
  } while (v);
  for (int i = 0; i < n; i++) out[i] = t[n - 1 - i];
  out[n] = 0;
}

static void cat(char *out, const char *a) {
  while (*out) out++;
  while (*a) *out++ = *a++;
  *out = 0;
}

static void go(int s) {
  state = s, state_t = 0, sel = 0, top = 0;
}

static void title_world(void) {
  game_new_run();
  scene_reset();
}

void app_init(void) {
  save_load();
  scene_init();
  title_world();
  if (!pg.help) {
    go(S_HELP);
    help_back = S_TITLE;
  } else
    go(S_TITLE);
}

static void start_run(void) {
  game_new_run();
  scene_reset();
  go(S_RUN);
}

/* the run is over (or quit): its coins kept, the best, the objectives */
static void end_run(void) {
  pg.coins += run.coins;
  new_best = run.score > pg.best;
  if (new_best) pg.best = run.score;
  if ((uint32_t)run.dist > pg.best_dist) pg.best_dist = (uint32_t)run.dist;
  pg.runs++;
  pg.life_coins += run.coins;
  pg.life_dist += (uint32_t)run.dist;
  nawarded = game_award(awarded, 4);
  awarded_all = nawarded;
  save_write();
}

static void say(const char *s) {
  strcpy(note, s);
  note_t = 2;
}

/* the store's lines: the five upgrades, then the three things to use */
#define NSTORE (UP_COUNT + USE_COUNT)
static const char *const store_name[NSTORE] = {"Coin magnet", "Invisibility", "Coin bonus", "Coin value", "Boost",
                                               "Wings", "Head start", "Mega head start"};
static const char *const store_what[NSTORE] = {
  "Pulls in the coins near you", "Run through anything", "Coins all at once", "Red and blue coins sooner",
  "A burst of speed", "OK in a run: saved once in 30 s", "1 at the start: run to 950 m", "3 at the start: run to 2,350 m"};

static uint32_t store_price(int i) { return i < UP_COUNT ? game_up_price(i) : game_use_price(i - UP_COUNT); }

static void store_buy(void) {
  uint32_t p = store_price(sel);
  if (!p) {
    say("All done");
    return;
  }
  if (pg.coins < p) {
    say("Not enough coins");
    return;
  }
  pg.coins -= p;
  if (sel < UP_COUNT) pg.up[sel]++;
  else if (pg.use[sel - UP_COUNT] < 999) pg.use[sel - UP_COUNT]++;
  /* (the upgrade objectives are met as soon as they are bought) */
  int got[2];
  game_award(got, 2);
  save_write();
  say("Bought");
}

void app_step(uint32_t keys, uint32_t pressed) {
  state_t += 1.0f / 60;
  if (note_t > 0) note_t -= 1.0f / 60;
  if (pressed & K_HOME) {
    if (state == S_RUN || state == S_PAUSE || state == S_QUIT) end_run();   /* (the results not shown yet) */
    else
      save_write();
    app_running = false;
    return;
  }
  uint32_t left = K_LEFT | K_LEAN_L, right = K_RIGHT | K_LEAN_R;
  switch (state) {
  case S_HELP:
    if (pressed & (K_OK | K_BACK)) {
      if (!pg.help) pg.help = 1, save_write();
      go(help_back);
      if (help_back == S_PAUSE) sel = 1;
    }
    break;
  case S_TITLE:
    if (pressed & K_UP) sel = (sel + 3) % 4;
    if (pressed & K_DOWN) sel = (sel + 1) % 4;
    if (pressed & K_OK) {
      if (sel == 0) start_run();
      else if (sel == 1) go(S_STORE);
      else if (sel == 2) go(S_OBJECTIVES);
      else help_back = S_TITLE, go(S_HELP), sel = 3;
    }
    break;
  case S_RUN: {
    if (pressed & K_BACK) {
      go(S_PAUSE);
      break;
    }
    uint32_t in = 0;
    bool shift = keys & K_SHIFT;
    if (!shift) {
      if (pressed & K_LEFT) in |= IN_LEFT;
      if (pressed & K_RIGHT) in |= IN_RIGHT;
    }
    if ((keys & K_LEAN_L) || (shift && (keys & K_LEFT))) in |= IN_LEAN_L;
    else if ((keys & K_LEAN_R) || (shift && (keys & K_RIGHT))) in |= IN_LEAN_R;
    if (pressed & K_UP) in |= IN_JUMP;
    if (pressed & K_DOWN) in |= IN_SLIDE;
    if (pressed & K_OK) in |= IN_WINGS;
    if (pressed & K_ONE) in |= IN_HEADSTART;
    if (pressed & K_THREE) in |= IN_MEGA;
    uint16_t before = (uint16_t)(pg.use[0] + pg.use[1] + pg.use[2]);
    game_step(in);
    scene_follow();
    if (pg.use[0] + pg.use[1] + pg.use[2] != before) save_write();
    if (game_over()) {
      end_run();
      go(S_RESULTS);
    }
    break;
  }
  case S_PAUSE:
    if (pressed & K_UP) sel = (sel + 2) % 3;
    if (pressed & K_DOWN) sel = (sel + 1) % 3;
    if (pressed & K_BACK) go(S_RUN);
    else if (pressed & K_OK) {
      if (sel == 0) go(S_RUN);
      else if (sel == 1) help_back = S_PAUSE, go(S_HELP);
      else go(S_QUIT);
    }
    break;
  case S_QUIT:
    if (pressed & (left | right)) sel ^= 1;
    if (pressed & K_BACK) go(S_PAUSE), sel = 2;
    else if (pressed & K_OK) {
      if (sel == 1) {
        end_run();
        go(S_RESULTS);
      } else
        go(S_PAUSE), sel = 2;
    }
    break;
  case S_RESULTS:
    if (state_t < 0.5f) break;
    if (pressed & K_OK) start_run();
    else if (pressed & K_BACK) title_world(), go(S_TITLE);
    break;
  case S_STORE:
    if (pressed & K_UP) sel = (sel + NSTORE - 1) % NSTORE;
    if (pressed & K_DOWN) sel = (sel + 1) % NSTORE;
    if (pressed & K_OK) store_buy();
    if (pressed & K_BACK) go(S_TITLE), sel = 1;
    break;
  case S_OBJECTIVES:
    if (pressed & K_UP) top = top > 0 ? top - 1 : 0;
    if (pressed & K_DOWN) top = top < NOBJ - 8 ? top + 1 : NOBJ - 8;
    if (pressed & (K_BACK | K_OK)) go(S_TITLE), sel = 2;
    break;
  }
}

/* ---------------------------------------------------------------- drawing */
static void label(const Font *f, int x, int y, const char *s, uint32_t c, int align) {
  d_label(f, x, y, s, C(c), C(INK), f == &font_big ? 2 : 1, align);
}

/* the key line at the bottom: plain words */
static void keys_line(const char *s) {
  d_fill_a(0, 222, SCREEN_W, 18, C(0x1A1008), 190);
  d_text(&font_small, (SCREEN_W - d_text_width(&font_small, s)) / 2, 235, s, C(0xF2E2BC), 255);
}

static void button(int x, int y, int w, int h, const char *s, bool focus) {
  d_panel(x, y, w, h, 6, focus ? 3 : 1, focus ? 0xFFD040 : 0x6A4A28, focus ? 0x8A5A26 : 0x5A3A1A, focus ? 0x6A4018 : 0x3E2810);
  label(&font_mid, x + w / 2, y + h / 2 + 6, s, focus ? 0xFFFFFF : DIM, 0);
}

static void panel(int x, int y, int w, int h) {
  d_panel(x, y, w, h, 8, 2, 0xB08848, 0x4A3018, 0x2E1C0C);
}

static void coin_icon(int x, int y) {
  d_disc((float)x, (float)y, 5, C(0xC89A10), 255);
  d_disc((float)x, (float)y, 3.2f, C(0xFFD21E), 255);
}

static void hud(void) {
  char b[40];
  fmt(b, run.score);
  label(&font_mid, 8, 20, b, 0xFFFFFF, -1);
  coin_icon(13, 32);
  fmt(b, run.coins);
  label(&font_small, 22, 37, b, GOLD, -1);
  fmt(b, (uint32_t)run.dist);
  cat(b, " m");
  label(&font_small, SCREEN_W - 8, 18, b, 0xFFFFFF, 1);
  /* the coin meter: every 100 coins, 500 points */
  if (r_y0 < 150 && r_y1 > 50) {
    int h = 80, f = (int)(run.bonus_coins * (uint32_t)h / 100);
    d_fill_a(8, 52, 6, h, C(0x1A1008), 110);
    d_fill(9, 52 + h - f, 4, f, C(0xFFC020));
  }
  int kind;
  float left = game_power_left(&kind);
  if (kind) {
    static const char *const nm[5] = {"", "", "Invisible", "Coin magnet", "Boost"};
    static const uint32_t col[5] = {0, 0, 0x9FE8FF, 0xE04040, 0x56E070};
    label(&font_small, SCREEN_W / 2, 18, nm[kind], 0xFFFFFF, 0);
    d_fill_a(SCREEN_W / 2 - 40, 22, 80, 5, C(0x1A1008), 170);
    d_fill(SCREEN_W / 2 - 40, 22, (int)(80 * left), 5, C(col[kind]));
  }
  if (pl.wings > 0) {
    fmt(b, (uint32_t)pl.wings + 1);
    char w[24] = "Wings ";
    cat(w, b);
    label(&font_small, SCREEN_W - 8, 34, w, 0xFFFFFF, 1);
  }
  const char *hint = NULL;
  static char hb[48];
  int hs = game_headstart_ok();
  if (hs == 3) hint = "1 head start  -  3 mega head start";
  else if (hs == 1) hint = "1 head start";
  else if (hs == 2) hint = "3 mega head start";
  else if (state_t < 4.5f && pg.runs < 5) hint = "Left Right turn  -  Up jump  -  Down slide  -  4 6 lean";
  else if (game_wings_ok() && state_t < 9) {
    strcpy(hb, "OK wings (");
    fmt(hb + strlen(hb), pg.use[USE_WINGS]);
    cat(hb, ")");
    hint = hb;
  }
  if (hint) keys_line(hint);
}

static void help_overlay(void) {
  d_dim(150);
  panel(14, 8, 292, 212);
  label(&font_mid, 160, 30, "How to play", GOLD, 0);
  static const char *const lines[] = {
    "Run from the temple: they are right behind you.",
    "Left / Right   turn at a corner",
    "Up   jump roots, trees, fire and gaps",
    "Down   slide under branches",
    "4 / 6 (or shift + arrows)   lean for coins",
    "     and along narrow paths",
    "OK   wings: saved once (buy them in the store)",
    "Trip once and they catch up; trip again soon",
    "and they get you. Every 100 coins: 500 points.",
    "Back pauses. Home saves and quits."};
  for (int i = 0; i < 10; i++) d_text(&font_small, 26, 52 + i * 16, lines[i], C(i == 0 ? GOLD : 0xF2E2BC), 255);
  keys_line("OK continue");
}

static void title_overlay(void) {
  label(&font_big, 90, 44, "TEMPLE RUN", 0xFFD04A, 0);
  char b[40] = "Best ";
  fmt(b + 5, pg.best);
  label(&font_small, 90, 62, b, 0xFFFFFF, 0);
  static const char *const items[4] = {"Run", "Store", "Objectives", "How to play"};
  for (int i = 0; i < 4; i++) button(30, 80 + i * 32, 120, 26, items[i], sel == i);
  coin_icon(SCREEN_W - 60, 14);
  fmt(b, pg.coins);
  label(&font_small, SCREEN_W - 50, 19, b, GOLD, -1);
  keys_line("Up Down choose  -  OK select  -  Home quit");
}

static void pause_overlay(void) {
  d_dim(130);
  panel(80, 40, 160, 150);
  label(&font_mid, 160, 64, "Paused", GOLD, 0);
  static const char *const items[3] = {"Resume", "How to play", "Quit game"};
  for (int i = 0; i < 3; i++) button(96, 78 + i * 34, 128, 28, items[i], sel == i);
  keys_line("Up Down choose  -  OK select  -  Back resume");
}

static void quit_overlay(void) {
  d_dim(150);
  panel(50, 64, 220, 110);
  label(&font_mid, 160, 90, "Quit this run?", GOLD, 0);
  d_text(&font_small, 160 - d_text_width(&font_small, "Your coins are kept.") / 2, 110, "Your coins are kept.", C(0xF2E2BC), 255);
  button(70, 128, 80, 28, "No", sel == 0);
  button(170, 128, 80, 28, "Yes", sel == 1);
  keys_line("Left Right choose  -  OK select");
}

static void results_overlay(void) {
  d_dim(140);
  panel(30, 10, 260, 206);
  char b[48];
  label(&font_mid, 160, 34, new_best ? "New best!" : "Game over", GOLD, 0);
  fmt(b, run.score);
  label(&font_big, 160, 66, b, 0xFFFFFF, 0);
  struct {
    const char *k;
    uint32_t v;
  } rows[3] = {{"Coins", run.coins}, {"Distance", (uint32_t)run.dist}, {"Best", pg.best}};
  for (int i = 0; i < 3; i++) {
    d_text(&font_small, 60, 92 + i * 16, rows[i].k, C(DIM), 255);
    fmt(b, rows[i].v);
    if (i == 1) cat(b, " m");
    d_text(&font_small, 260 - d_text_width(&font_small, b), 92 + i * 16, b, C(0xFFFFFF), 255);
  }
  if (nawarded) {
    label(&font_small, 160, 152, "Objectives done", GOLD, 0);
    for (int i = 0; i < nawarded && i < 3; i++) {
      const char *t = game_obj_text(awarded[i]);
      d_text(&font_small, 160 - d_text_width(&font_small, t) / 2, 168 + i * 13, t, C(0xF2E2BC), 255);
    }
  }
  strcpy(b, "Score multiplier ");
  fmt(b + strlen(b), pg.multiplier);
  cat(b, "x");
  d_text(&font_small, 160 - d_text_width(&font_small, b) / 2, nawarded ? 210 : 160, b, C(0xFFFFFF), 255);
  keys_line("OK run again  -  Back menu");
}

static void store_overlay(void) {
  d_dim(170);
  label(&font_mid, 12, 22, "Store", GOLD, -1);
  char b[40];
  coin_icon(SCREEN_W - 70, 16);
  fmt(b, pg.coins);
  label(&font_small, SCREEN_W - 60, 21, b, GOLD, -1);
  for (int i = 0; i < NSTORE; i++) {
    int y = 32 + i * 21;
    bool f = sel == i;
    d_panel(8, y, 304, 19, 4, f ? 2 : 1, f ? 0xFFD040 : 0x5A3E22, f ? 0x6A4420 : 0x3A2612, f ? 0x523214 : 0x2A1A0A);
    d_text(&font_small, 16, y + 14, store_name[i], C(f ? 0xFFFFFF : 0xE8D6AE), 255);
    if (i < UP_COUNT)
      for (int k = 0; k < UP_MAX; k++) d_fill(150 + k * 12, y + 6, 9, 7, C(k < pg.up[i] ? 0x56E070 : 0x5A4630));
    else {
      fmt(b, pg.use[i - UP_COUNT]);
      char o[16] = "own ";
      cat(o, b);
      d_text(&font_small, 150, y + 14, o, C(0xE8D6AE), 255);
    }
    uint32_t p = store_price(i);
    if (p) fmt(b, p);
    else strcpy(b, "max");
    d_text(&font_small, 302 - d_text_width(&font_small, b), y + 14, b, C(p <= pg.coins ? GOLD : 0xB09070), 255);
  }
  const char *w = note_t > 0 ? note : store_what[sel];
  d_text(&font_small, 160 - d_text_width(&font_small, w) / 2, 214, w, C(note_t > 0 ? 0xFFD040 : 0xF2E2BC), 255);
  keys_line("Up Down choose  -  OK buy  -  Back menu");
}

static void objectives_overlay(void) {
  d_dim(170);
  label(&font_mid, 12, 22, "Objectives", GOLD, -1);
  char b[32] = "Multiplier ";
  fmt(b + strlen(b), pg.multiplier);
  cat(b, "x");
  label(&font_small, SCREEN_W - 10, 20, b, 0xFFFFFF, 1);
  for (int i = 0; i < 8; i++) {
    int k = top + i, y = 32 + i * 23;
    bool done = game_obj_done(k);
    d_panel(8, y, 304, 21, 4, 1, 0x5A3E22, done ? 0x3A4A1E : 0x3A2612, done ? 0x2A3814 : 0x2A1A0A);
    if (done) d_disc(22, (float)y + 10.5f, 5, C(0x7AE060), 255);
    else d_ring(22, (float)y + 10.5f, 5, 1.2f, C(0x9A8060), 255);
    d_text(&font_small, 36, y + 15, game_obj_text(k), C(done ? 0xD8F0C0 : 0xF2E2BC), 255);
  }
  keys_line("Up Down scroll  -  Back menu");
}

static void overlay(void) {
  switch (state) {
  case S_HELP: help_overlay(); break;
  case S_TITLE: title_overlay(); break;
  case S_RUN: hud(); break;
  case S_PAUSE: pause_overlay(); break;
  case S_QUIT: quit_overlay(); break;
  case S_RESULTS: results_overlay(); break;
  case S_STORE: store_overlay(); break;
  case S_OBJECTIVES: objectives_overlay(); break;
  }
}

void app_draw(void) {
  bool title = state == S_TITLE || state == S_STORE || state == S_OBJECTIVES || (state == S_HELP && help_back == S_TITLE);
  scene_draw(overlay, title);
}
