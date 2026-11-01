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
bool app_in_run(void) { return state == S_RUN; }
static int sel, top;            /* the button or line in focus, the first line shown */
static float state_t;
static bool new_best;
static bool quit_new;            /* the question is for a new run (New Game), not the results (Main Menu) */
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
      else quit_new = sel == 1, go(S_QUIT);
    }
    break;
  case S_QUIT: {
    int back = quit_new ? 1 : 2;
    if (pressed & (left | right)) sel ^= 1;
    if (pressed & K_BACK) go(S_PAUSE), sel = back;
    else if (pressed & K_OK) {
      if (sel == 1) {
        end_run();
        if (quit_new) start_run();
        else go(S_RESULTS);
      } else
        go(S_PAUSE), sel = back;
    }
    break;
  }
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
  d_label(f, x, y, s, C(c), C(INK), 1, align);   /* (a pixel's outline: the big font's thicker one is never drawn) */
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

/* the run's screen, the game's: the totem on the left with its coin meter (full at 100 coins) and an emblem for
 * each bonus level, vines along the top, the score and the run's coins on their plates at the top right, the idol
 * at the bottom right; a board drops with the distance at 250, 500, 750 and 1000 m, then each 500 m */
#define TX (-2)
#define TY (-12)
static int board_dist;
static float board_t = -1;

static void board(void) {
  int d = (int)run.dist;
  if (d < board_dist) board_dist = 0;
  int next = board_dist < 1000 ? board_dist + 250 : board_dist + 500;
  if (d >= next) board_dist = next, board_t = 0;
  if (board_t < 0) return;
  board_t += 1.0f / 30;
  float t = board_t, down = t < 0.25f ? t * 4 : t < 2.25f ? 1 : t < 2.5f ? (2.5f - t) * 4 : -1;
  if (down < 0) {
    board_t = -1;
    return;
  }
  const HudPic *bp = &hud_pics[HP_BOARD];
  int y = -bp->h + (int)(down * (bp->h - 34));
  d_pic(HP_BOARD, (SCREEN_W - bp->w) / 2, y, 255, 0, 0);
  char b[16];
  fmt(b, (uint32_t)board_dist);
  cat(b, "m");
  d_gold(&hud_gold, SCREEN_W / 2, y + bp->h - 31, b, 0, 255);
}

static void hud(void) {
  char b[40];
  /* the totem */
  d_pic(HP_JUNGLE_A, 34, -4, 255, 0, 0);
  d_pic(HP_JUNGLE_C, 214, 40, 255, 0, 0);
  d_pic(HP_TOTEM, TX, TY, 255, 0, 0);
  const HudPic *bar = &hud_pics[HP_TOTEM_BAR];
  int full = (int)(run.bonus_coins * (uint32_t)bar->h / 100);
  d_pic(HP_TOTEM_BAR, TX + 3, TY + 64, 255, 0, bar->h - full);
  d_pic(HP_TOTEM_CAP, TX - 1, TY + 58, 255, 0, 0);
  static const int8_t em[4][2] = {{1, 15}, {17, 13}, {16, 29}, {11, 25}};
  for (int i = 0; i < 4; i++)
    if ((int)run.bonus_level > i) d_pic(HP_EMBLEM1 + i, TX + 20 + em[i][0], TY + em[i][1], 255, 0, 0);
  /* the score and the coins */
  d_pic(HP_SCORE_PLATE, 172, 6, 255, 0, 0);
  d_pic(HP_COINS_PLATE, 178, 34, 255, 0, 0);
  d_pic(HP_SCOREBOARD, 279, -3, 255, 0, 0);
  fmt(b, run.score);
  d_gold(&hud_gold, 276, 8, b, 1, 255);
  d_pic(HP_COIN, 188, 37, 255, 0, 0);
  fmt(b, run.coins);
  d_gold(&hud_gold_small, 276, 38, b, 1, 255);
  if (pl.wings > 0) {
    d_pic(HP_WINGS_PLATE, 226, 58, 255, 0, 0);
    fmt(b, (uint32_t)pl.wings + 1);
    d_gold(&hud_gold_small, 274, 60, b, 1, 255);
  }
  /* the idol */
  d_pic(HP_IDOL, SCREEN_W - hud_pics[HP_IDOL].w - 2, SCREEN_H - hud_pics[HP_IDOL].h, 255, 0, 0);
  d_pic(HP_PAUSE, SCREEN_W - hud_pics[HP_IDOL].w / 2 - 2 - hud_pics[HP_PAUSE].w / 2, SCREEN_H - 20, 255, 0, 0);
  /* the totem's bonus, flying to the score */
  if (run.totem_t > 0) {
    float u = 1 - run.totem_t;
    int pic = HP_BONUS100 + run.totem_pic - 1;
    const HudPic *bp = &hud_pics[pic];
    int x = (int)(30 + u * 180) - bp->w / 2, y = 40 - bp->h / 2;
    d_pic(pic, x, y, (int)(192 * (1 - u)), 0, 0);
  }
  board();
  const char *hint = NULL;
  static char hb[48];
  int hs = game_headstart_ok();
  if (hs) {
    /* the game's head start buttons, with their keys */
    int x = hs == 3 ? 52 : 114;
    if (hs & 1) {
      d_pic(HP_HEADSTART, x, 190, 255, 0, 0);
      d_gold(&hud_gold, x - 4, 194, "1", 1, 255);
      x += 124;
    }
    if (hs & 2) {
      d_pic(HP_HEADSTART_MEGA, x, 190, 255, 0, 0);
      d_gold(&hud_gold, x - 4, 194, "3", 1, 255);
    }
    return;
  }
  if (state_t < 4.5f && pg.runs < 5) hint = "Left Right turn  -  Up jump  -  Down slide  -  4 6 lean";
  else if (game_wings_ok() && state_t < 9) {
    strcpy(hb, "OK wings (");
    fmt(hb + strlen(hb), pg.use[USE_WINGS]);
    cat(hb, ")");
    hint = hb;
  }
  if (hint) keys_line(hint);
}

/* a parchment box, the game's colours, with dark lettering */
static void parchment(int x, int y, int w, int h) {
  d_panel(x, y, w, h, 6, 3, 0x6A4A1E, 0xE6CF96, 0xC9A866);
}

static void help_overlay(void) {
  d_dim(150);
  parchment(14, 8, 292, 212);
  d_gold(&hud_gold, 160, 12, "How to play", 0, 255);
  static const char *const lines[] = {
    "Run from the temple: they are right behind you.",
    "Left / Right   turn at a corner",
    "Up   jump roots, trees, fire and gaps",
    "Down   slide under branches",
    "4 / 6 (or shift + arrows)   lean for coins",
    "     and along narrow paths",
    "OK   wings: saved once (buy them in the store)",
    "Trip once and they catch up; trip again soon",
    "and they get you. Each 100 coins: a bonus.",
    "Back pauses. Home saves and quits."};
  for (int i = 0; i < 10; i++) d_text(&font_small, 26, 52 + i * 16, lines[i], C(i == 0 ? 0x8A1A08 : 0x2A1404), 255);
  keys_line("OK continue");
}

/* the game's menus: its art, the one in focus bright and pointed at, the others dimmer */
#define DIMMED 170
static void pointer(int x, int y) {   /* a golden arrow at the left of what is in focus, its tip at (x, y) */
  float xy[6] = {(float)x, (float)y, (float)x - 9, (float)y - 6, (float)x - 9, (float)y + 6};
  d_poly(xy, 3, C(0x3A2208), 255);
  float in[6] = {(float)x - 2, (float)y, (float)x - 8, (float)y - 4, (float)x - 8, (float)y + 4};
  d_poly(in, 3, C(0xFFD040), 255);
}

static void coins_corner(uint32_t coins) {
  char b[16];
  fmt(b, coins);
  int w = d_gold_width(&hud_gold_small, b);
  d_rrect(SCREEN_W - 14 - w - hud_pics[HP_COIN].w, 2, w + hud_pics[HP_COIN].w + 12, 21, 6, C(0x1A1008), 170);
  d_pic(HP_COIN, SCREEN_W - 8 - w - hud_pics[HP_COIN].w - 3, 5, 255, 0, 0);
  d_gold(&hud_gold_small, SCREEN_W - 6, 6, b, 1, 255);
}

static void title_overlay(void) {
  d_rrect(4, 92, 206, 84, 10, C(0x1A1008), 120);
  d_pic(HP_LOGO, 2, 0, 255, 0, 0);
  d_pic(HP_PLAY_IDOL, 14, 94, sel == 0 ? 255 : DIMMED, 0, 0);
  static const int pic[2] = {HP_M_STORE, HP_M_OBJECTIVES};
  for (int i = 0; i < 2; i++) d_pic(pic[i], 92, 102 + i * 26, sel == i + 1 ? 255 : DIMMED, 0, 0);
  d_gold(&hud_gold, 92, 152, "How to play", -1, sel == 3 ? 255 : DIMMED);
  static const uint8_t py[4] = {0, 113, 138, 162};
  if (sel) pointer(88, py[sel]);
  else pointer(12, 130);
  char b[32] = "Best ";
  fmt(b + 5, pg.best);
  d_rrect(8, 178, d_gold_width(&hud_gold, b) + 12, 26, 7, C(0x1A1008), 150);
  d_gold(&hud_gold, 14, 180, b, -1, 255);
  coins_corner(pg.coins);
  keys_line("Up Down choose  -  OK select  -  Home quit");
}

static void scroll_buttons(const int *pics, int n, int y0, int step) {
  for (int i = 0; i < n; i++) {
    const HudPic *p = &hud_pics[pics[i]];
    int x = (SCREEN_W - p->w) / 2, y = y0 + i * step;
    d_pic(pics[i], x, y, sel == i ? 255 : DIMMED, 0, 0);
    if (sel == i) pointer(x - 2, y + p->h / 2);
  }
}

static void pause_overlay(void) {
  d_dim(140);
  d_pic(HP_H_PAUSED, (SCREEN_W - hud_pics[HP_H_PAUSED].w) / 2, 30, 255, 0, 0);
  static const int pics[3] = {HP_B_RESUME, HP_B_NEW_GAME, HP_B_MAIN_MENU};
  scroll_buttons(pics, 3, 74, 44);
  keys_line("Up Down choose  -  OK select  -  Back resume");
}

static void quit_overlay(void) {
  d_dim(150);
  parchment(50, 64, 220, 110);
  const char *q = quit_new ? "Start a new run?" : "Quit this run?";
  d_text(&font_mid, 160 - d_text_width(&font_mid, q) / 2, 92, q, C(0x3A2208), 255);
  d_text(&font_small, 160 - d_text_width(&font_small, "Your coins are kept.") / 2, 112, "Your coins are kept.", C(0x5A3A14), 255);
  button(70, 128, 80, 28, "No", sel == 0);
  button(170, 128, 80, 28, "Yes", sel == 1);
  keys_line("Left Right choose  -  OK select");
}

static void results_overlay(void) {
  d_dim(110);
  const HudPic *pp = &hud_pics[HP_PARCHMENT];
  int px = 8, py = 4;
  d_pic(HP_PARCHMENT, px, py, 255, 0, 0);
  int cx = px + pp->w / 2;
  d_pic(HP_H_SCORE, cx - hud_pics[HP_H_SCORE].w / 2, py + 8, 255, 0, 0);
  char b[48];
  fmt(b, run.score);
  d_gold(&hud_gold, cx, py + 38, b, 0, 255);
  if (new_best) d_text(&font_small, cx - d_text_width(&font_small, "New best!") / 2, py + 74, "New best!", C(0x8A1A08), 255);
  struct {
    const char *k;
    uint32_t v;
  } rows[4] = {{"Coins", run.coins}, {"Distance", (uint32_t)run.dist}, {"Best", pg.best}, {"Multiplier", pg.multiplier}};
  for (int i = 0; i < 4; i++) {
    int y = py + 94 + i * 16;
    d_text(&font_small, px + 34, y, rows[i].k, C(0x3A2208), 255);
    fmt(b, rows[i].v);
    if (i == 1) cat(b, " m");
    if (i == 3) cat(b, "x");
    d_text(&font_small, px + pp->w - 34 - d_text_width(&font_small, b), y, b, C(0x2A1404), 255);
  }
  if (nawarded) {
    d_text(&font_small, px + 34, py + 166, "Objectives done", C(0x8A1A08), 255);
    for (int i = 0; i < nawarded && i < 2; i++) {
      const char *t = game_obj_text(awarded[i]);
      d_pic(HP_CHECK_FULL, px + 32, py + 172 + i * 17, 255, 0, 0);
      d_text(&font_small, px + 50, py + 183 + i * 17, t, C(0x2A1404), 255);
    }
  }
  /* the game's buttons, with their keys */
  int bx = 186 + (SCREEN_W - 186 - hud_pics[HP_B_RUN_AGAIN].w) / 2;
  d_pic(HP_B_RUN_AGAIN, bx, 70, 255, 0, 0);
  d_pic(HP_B_MAIN_MENU, bx, 120, 255, 0, 0);
  label(&font_small, bx + hud_pics[HP_B_RUN_AGAIN].w / 2, 116, "OK", 0xFFFFFF, 0);
  label(&font_small, bx + hud_pics[HP_B_MAIN_MENU].w / 2, 166, "Back", 0xFFFFFF, 0);
  keys_line("OK run again  -  Back menu");
}

static void store_overlay(void) {
  d_dim(170);
  d_pic(HP_H_STORE, 6, 4, 255, 0, 0);
  coins_corner(pg.coins);
  static const uint8_t icon[NSTORE] = {HP_I_MAGNET, HP_I_INVIS, HP_I_COIN_BONUS, HP_I_COIN_VALUE, HP_I_BOOST,
                                       HP_I_WINGS, HP_I_HEADSTART, HP_I_HEADSTART_MEGA};
  char b[40];
  for (int i = 0; i < NSTORE; i++) {
    int y = 34 + i * 22;
    bool f = sel == i;
    if (f) d_rrect(4, y - 1, 312, 22, 5, C(0xFFD040), 70);
    d_pic(icon[i], 8, y - 1, 255, 0, 0);
    d_text(&font_small, 36, y + 14, store_name[i], C(f ? 0xFFFFFF : 0xE8D6AE), 255);
    if (i < UP_COUNT) {
      d_pic(HP_BAR_EMPTY, 150, y + 5, 255, 0, 0);
      for (int k = 0; k < pg.up[i] && k < 5; k++) d_pic(HP_BAR_FULL1 + k, 150 + k * 16, y + 5, 255, 0, 0);
    } else {
      fmt(b, pg.use[i - UP_COUNT]);
      char o[16] = "own ";
      cat(o, b);
      d_text(&font_small, 150, y + 14, o, C(0xE8D6AE), 255);
    }
    uint32_t p = store_price(i);
    if (p) {
      fmt(b, p);
      int w = d_gold_width(&hud_gold_small, b);
      d_gold(&hud_gold_small, 310, y + 3, b, 1, p <= pg.coins ? 255 : 120);
      d_pic(HP_COIN, 306 - w - hud_pics[HP_COIN].w, y + 1, p <= pg.coins ? 255 : 120, 0, 0);
    } else {
      d_text(&font_small, 302 - d_text_width(&font_small, "max"), y + 14, "max", C(0xB09070), 255);
    }
    if (i + 1 < NSTORE) d_pic(HP_SEPARATOR, 8, y + 20, 160, 0, 0);
  }
  const char *w = note_t > 0 ? note : store_what[sel];
  d_text(&font_small, 160 - d_text_width(&font_small, w) / 2, 214, w, C(note_t > 0 ? 0xFFD040 : 0xF2E2BC), 255);
  keys_line("Up Down choose  -  OK buy  -  Back menu");
}

static void objectives_overlay(void) {
  d_dim(170);
  d_pic(HP_H_OBJECTIVES, 6, 4, 255, 0, 0);
  char b[32] = "Multiplier ";
  fmt(b + strlen(b), pg.multiplier);
  cat(b, "x");
  label(&font_small, SCREEN_W - 10, 22, b, 0xFFFFFF, 1);
  for (int i = 0; i < 8; i++) {
    int k = top + i, y = 34 + i * 23;
    bool done = game_obj_done(k);
    d_pic(done ? HP_CHECK_FULL : HP_CHECK_EMPTY, 10, y + 3, 255, 0, 0);
    d_text(&font_small, 32, y + 15, game_obj_text(k), C(done ? 0xFFE7A0 : 0xD8C8A8), 255);
    if (i < 7) d_pic(HP_SEPARATOR, 8, y + 21, 120, 0, 0);
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
