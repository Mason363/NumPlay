/* Five Nights at Freddy's 3: the scenes around the nights, the keys and the saves.
 *
 * As the game: the title (New Game, Continue, the 6th night and Extras once the game is beaten, a star for each
 * ending), the night's card, the night, 5 AM turning 6 AM, the static and game over when Springtrap gets you, and
 * the endings after the 5th and the 6th night. */
#include <stdio.h>
#include <string.h>
#include "game.h"
#include "gfx.h"
#include "data.h"
#include "plat.h"
#include "mini.h"

Game g;

static uint32_t seed = 0x3F3A2B1u;
uint32_t rnd(void) {
  seed = seed * 1664525u + 1013904223u;
  return seed >> 8;
}
uint32_t rnd_n(uint32_t n) { return n ? rnd() % n : 0; }

static bool pressed(uint32_t k) { return (g.pressed & k) != 0; }
#define GREY_TEXT RGB(0x9A9A9A)
#define GREEN_TEXT RGB(0x4CD964)
static void game_band(void);

void caption(const char *s) {
  for (int i = 0; i < 3; i++) g.cap[i] = g.cap[i + 1], g.cap_t[i] = g.cap_t[i + 1];
  g.cap[3] = s, g.cap_t[3] = 3;
}

static void go(int scene) { g.scene = scene, g.t = 0, g.menu = 0; }

void start_night(int night) {
  night_begin(night);
  memset(g.cap, 0, sizeof g.cap);
  go(SC_DAY);
}

void game_init(void) {
  memset(&g, 0, sizeof g);
  g.running = true;
  seed ^= plat_millis() * 2654435761u;
  save_restore_copy();
  save_load();
  if (save_resume_load()) g.scene = SC_NIGHT, g.paused = 1;
}

/* ------------------------------------------------------------ the title */
enum { M_NEW, M_CONTINUE, M_SIXTH, M_EXTRAS };
static int title_items(uint8_t *items) {
  int n = 0;
  items[n++] = M_NEW, items[n++] = M_CONTINUE;
  if (g.beatgame) items[n++] = M_SIXTH, items[n++] = M_EXTRAS;
  return n;
}

static void title_step(void) {
  uint8_t items[4];
  int n = title_items(items);
  if (pressed(K_UP | K_DIGIT(8))) g.menu = (uint8_t)((g.menu + n - 1) % n);
  if (pressed(K_DOWN | K_DIGIT(2))) g.menu = (uint8_t)((g.menu + 1) % n);
  if (pressed(K_BACK)) g.running = false;
  if (!pressed(K_OK | K_EXE)) return;
  switch (items[g.menu]) {
  case M_NEW: g.level = 1, save_progress(), start_night(1); break;
  case M_CONTINUE: start_night(g.level > 5 ? 5 : g.level); break;
  case M_SIXTH: start_night(6); break;
  case M_EXTRAS: go(SC_EXTRAS); break;
  }
}

static void title_draw(void) {
  int r = (int)(g.t * 60) % 240;
  int pic = r < 4 ? PIC_TITLE12 : r < 7 ? PIC_TITLE13 : r == 100 ? PIC_TITLE14 : PIC_TITLE;
  pic_draw(pic, 0, 0);
  gfx_static(0, 0, SCREEN_W, SCREEN_H, 4 + (int)rnd_n(4), (uint32_t)(g.t * 60));
  gfx_text_s(FONT_BIG, 14, 14, "Five", WHITE);
  gfx_text_s(FONT_BIG, 14, 32, "Nights", WHITE);
  gfx_text_s(FONT_BIG, 14, 50, "at", WHITE);
  gfx_text_s(FONT_BIG, 14, 68, "Freddy's", WHITE);
  gfx_text_s(FONT_HUGE, 120, 50, "3", WHITE);
  uint8_t items[4];
  int n = title_items(items);
  static const char *const names[4] = {"New Game", "Continue", "6th Night", "Extras"};
  for (int i = 0; i < n; i++) {
    int y = 120 + 22 * i;
    if (i == g.menu) gfx_text_s(FONT_TEXT, 14, y, ">>", WHITE);
    gfx_text_s(FONT_TEXT, 34, y, names[items[i]], WHITE);
    if (items[i] == M_CONTINUE) {
      char t[12];
      snprintf(t, sizeof t, "Night %d", g.level > 5 ? 5 : g.level);
      gfx_text_s(FONT_HUD, 112, y + 1, t, GREY_TEXT);
    }
  }
  int stars = g.beatgame + g.beat6 + g.goodend;
  for (int i = 0; i < stars; i++) gfx_img(IMG_STAR, 14 + 26 * i, 96, WHITE);
  gfx_text(FONT_HUD, 8, SCREEN_H - 14, "Up Down choose   OK start   Back quit", WHITE);
}

/* ------------------------------------------------------------ the minigames */
/* after a minigame: the night again (BB's, from the camera), the next night, the ending or Extras */
enum { AFTER_NIGHT, AFTER_NEXT, AFTER_END, AFTER_EXTRAS };
static void mini_go(int level, int after) {
  mini_begin(level, after == AFTER_EXTRAS);
  g.mini_after = (uint8_t)after;
  go(SC_MINI);
}

static void mini_over(void) {
  switch (g.mini_after) {
  case AFTER_NIGHT: start_night(g.n.night); break;
  case AFTER_NEXT: start_night(g.n.night + 1); break;
  case AFTER_END: g.ending = g.goodend ? 3 : 1, go(SC_END); break;
  default: go(SC_EXTRAS); break;
  }
}

/* ------------------------------------------------------------ extras */
static const char *const mini_names[MINI_COUNT] = {"BB's Air Adventure", "Mangle's Quest", "Chica's Party",
                                                    "Stage 01", "Shadow Bonnie", "Happiest Day"};
/* Extras: the cheats (after the 6th night and the good ending), the minigames (once the game is beaten), Exit */
static int extras_n(void) { return (g.goodend && g.beat6 ? 4 : 0) + (g.beatgame ? 1 : 0) + 1; }

static void extras_step(void) {
  if (g.extras_sub) {   /* the minigames */
    if (pressed(K_UP | K_DIGIT(8))) g.menu = (uint8_t)((g.menu + MINI_COUNT - 1) % MINI_COUNT);
    if (pressed(K_DOWN | K_DIGIT(2))) g.menu = (uint8_t)((g.menu + 1) % MINI_COUNT);
    if (pressed(K_BACK)) g.extras_sub = 0;
    if (pressed(K_OK | K_EXE)) mini_go(g.menu, AFTER_EXTRAS);
    return;
  }
  bool cheats = g.goodend && g.beat6;
  int n = extras_n();
  if (pressed(K_UP | K_DIGIT(8))) g.extras_sel = (uint8_t)((g.extras_sel + n - 1) % n);
  if (pressed(K_DOWN | K_DIGIT(2))) g.extras_sel = (uint8_t)((g.extras_sel + 1) % n);
  if (pressed(K_BACK)) go(SC_TITLE);
  if (pressed(K_OK | K_EXE)) {
    int c = cheats ? 4 : 0;
    if (g.extras_sel == n - 1) go(SC_TITLE);
    else if (g.extras_sel < c) g.cheats ^= (uint8_t)(1 << g.extras_sel), save_progress();
    else g.extras_sub = 1, g.menu = 0;
  }
}

static void extras_draw(void) {
  gfx_fill(0, 0, SCREEN_W, SCREEN_H, BLACK);
  if (g.extras_sub) {
    gfx_text_s(FONT_BIG, 14, 12, "Minigames", WHITE);
    for (int i = 0; i < MINI_COUNT; i++) {
      int y = 50 + 22 * i;
      if (i == g.menu) gfx_text(FONT_TEXT, 14, y, ">>", WHITE);
      gfx_text(FONT_TEXT, 34, y, mini_names[i], WHITE);
    }
    gfx_text(FONT_HUD, 8, SCREEN_H - 14, "Up Down choose   OK play   Back extras", WHITE);
    return;
  }
  gfx_text_s(FONT_BIG, 14, 12, "Extras", WHITE);
  bool cheats = g.goodend && g.beat6;
  static const char *const names[4] = {"Fast Nights", "Radar", "Aggressive", "No Errors"};
  int i = 0, y = 50;
  if (cheats)
    for (; i < 4; i++, y += 22) {
      if (i == g.extras_sel) gfx_text(FONT_TEXT, 14, y, ">>", WHITE);
      gfx_text(FONT_TEXT, 34, y, names[i], WHITE);
      gfx_text(FONT_TEXT, 160, y, g.cheats >> i & 1 ? "ON" : "OFF", g.cheats >> i & 1 ? GREEN_TEXT : GREY_TEXT);
    }
  else
    gfx_text(FONT_TEXT, 14, y, "The cheats open after the 6th night and the good ending.", GREY_TEXT), y += 22;
  if (g.beatgame) {
    if (i == g.extras_sel) gfx_text(FONT_TEXT, 14, y, ">>", WHITE);
    gfx_text(FONT_TEXT, 34, y, "Minigames", WHITE);
    i++, y += 22;
  }
  if (i == g.extras_sel) gfx_text(FONT_TEXT, 14, y, ">>", WHITE);
  gfx_text(FONT_TEXT, 34, y, "Exit", WHITE);
  gfx_text(FONT_HUD, 8, SCREEN_H - 14, "Up Down choose   OK pick   Back title", WHITE);
}

/* ------------------------------------------------------------ the night and around it */
static void captions_draw(void) {
  int y = SCREEN_H - 30;
  for (int i = 3; i >= 0; i--) {
    if (!g.cap[i] || g.cap_t[i] <= 0) continue;
    gfx_text_s(FONT_TEXT, 8, y, g.cap[i], WHITE);
    y -= 13;
  }
}

static const char *key_line(void) {
  if (g.n.viewing >= 2) return g.n.vents_map ? "Arrows cams   OK seal   Del map   EXE down" : "Arrows cams   OK audio   Del map   EXE down";
  if (g.n.panel >= 2) return g.n.rebooting ? "Rebooting..." : "Up Down choose   OK reboot   EXE exit";
  return "Left Right look   OK monitor   Down panel";
}

static void pause_step(void) {
  if (g.confirm) {
    if (pressed(K_OK | K_EXE)) save_resume(), g.n.night = 0, go(SC_TITLE), g.paused = 0, g.confirm = 0;
    if (pressed(K_BACK)) g.confirm = 0;
    return;
  }
  if (pressed(K_UP | K_DOWN | K_DIGIT(8) | K_DIGIT(2))) g.pause_item ^= 1;
  if (pressed(K_BACK)) g.paused = 0;
  if (pressed(K_OK | K_EXE)) {
    if (g.pause_item == 0) g.paused = 0;
    else g.confirm = 1;
  }
}

static void pause_draw(void) {
  gfx_dim(0, 60, SCREEN_W, 120, 22);
  if (g.confirm) {
    gfx_text_c(FONT_TEXT, SCREEN_W / 2, 90, "Quit the night?", WHITE);
    gfx_text_c(FONT_HUD, SCREEN_W / 2, 120, "OK quit   Back stay", WHITE);
    return;
  }
  gfx_text_c(FONT_BIG, SCREEN_W / 2, 74, "Paused", WHITE);
  gfx_text_c(FONT_TEXT, SCREEN_W / 2, 110, g.pause_item == 0 ? "> Resume <" : "Resume", WHITE);
  gfx_text_c(FONT_TEXT, SCREEN_W / 2, 134, g.pause_item == 1 ? "> Quit game <" : "Quit game", WHITE);
}

/* 6 AM: after each of the first five nights its minigame (the phone builds' "cutscenes"), Happiest Day the last */
static void six_done(void) {
  int night = g.n.night;
  if (night == 5) g.beatgame = 1;
  if (night == 6) g.beat6 = 1;
  if (night < 5) g.level = (uint8_t)(night + 1);
  save_progress();
  static const uint8_t after_night[6] = {0, MINI_MANGLE, MINI_CHICA, MINI_STAGE, MINI_SHADOW, MINI_HAPPY};
  if (night == 6) g.ending = 2, go(SC_END);
  else mini_go(after_night[night], night == 5 ? AFTER_END : AFTER_NEXT);
}

void game_step(uint32_t keys) {
  g.pressed = keys & ~g.held;
  g.held = keys;
  if (keys & K_HOME) {
    if (g.scene == SC_NIGHT && !g.n.got_you) save_resume();
    g.running = false;
    return;
  }
  for (int i = 0; i < 4; i++)
    if (g.cap_t[i] > 0) g.cap_t[i] -= 1.0f / 60;
  switch (g.scene) {
  case SC_TITLE: title_step(); break;
  case SC_EXTRAS: extras_step(); break;
  case SC_DAY:
    if (g.t > 160 / 60.0f) go(SC_NIGHT);
    break;
  case SC_NIGHT:
    if (g.paused) {
      pause_step();
      break;
    }
    if (pressed(K_BACK)) {
      g.paused = 1, g.pause_item = 0;
      break;
    }
    night_step();
    if (g.mini_next) mini_go(g.mini_next - 1, AFTER_NIGHT), g.mini_next = 0;
    break;
  case SC_STATIC:
    if (g.t > 5) go(SC_OVER);
    break;
  case SC_OVER:
    if (g.t > 5 || (g.t > 0.5f && pressed(K_OK | K_EXE | K_BACK))) go(SC_TITLE);
    break;
  case SC_SIX:
    if (g.t > 400 / 60.0f || (g.t > 2 && pressed(K_OK | K_EXE))) six_done();
    break;
  case SC_END:
    if (g.t > 20 || (g.t > 3 && pressed(K_OK | K_EXE | K_BACK))) go(SC_TITLE);
    break;
  case SC_MINI:
    mini_step();
    if (mini_done() || pressed(K_BACK)) mini_over();
    break;
  }
  g.t += 1.0f / 60;
}

void game_draw(void) { gfx_frame(game_band); }

/* (gfx_frame calls this for each band) */
static void game_band(void) {
  switch (g.scene) {
  case SC_TITLE: title_draw(); break;
  case SC_EXTRAS: extras_draw(); break;
  case SC_DAY: {
    gfx_fill(0, 0, SCREEN_W, SCREEN_H, BLACK);
    gfx_text_c(FONT_CLOCK, SCREEN_W / 2, 90, "12:00 AM", WHITE);
    char t[12];
    snprintf(t, sizeof t, "Night %d", g.n.night);
    gfx_text_c(FONT_BIG, SCREEN_W / 2, 126, t, WHITE);
    break;
  }
  case SC_NIGHT:
    view_night();
    captions_draw();
    if (g.n.frame < 600 || g.n.viewing >= 2 || g.n.panel >= 2) {
      gfx_dim(0, SCREEN_H - 14, SCREEN_W, 14, 20);
      gfx_text(FONT_HUD, 6, SCREEN_H - 13, key_line(), WHITE);
    }
    if (g.paused) pause_draw();
    break;
  case SC_STATIC: gfx_static(0, 0, SCREEN_W, SCREEN_H, 32, (uint32_t)(g.t * 60)); break;
  case SC_OVER: pic_draw(PIC_GAMEOVER, 0, 0); break;
  case SC_SIX: {
    gfx_fill(0, 0, SCREEN_W, SCREEN_H, BLACK);
    float t = g.t * 60;   /* (the 5 rolls up for 100 frames, the 6 comes up for 100) */
    int h = t < 100 ? 5 : 6, dy = t < 100 ? -(int)(t * 30 / 100) : t < 200 ? 30 - (int)((t - 100) * 30 / 100) : 0;
    char s[8];
    snprintf(s, sizeof s, "%d", h);
    gfx_text_r(FONT_HUGE, 150, 100 + dy, s, WHITE);
    gfx_text(FONT_HUGE, 156, 100, "AM", WHITE);
    break;
  }
  case SC_END: pic_draw(g.ending == 3 ? PIC_GOODEND : g.ending == 2 ? PIC_THEEND : PIC_BADEND, 0, 0); break;
  case SC_MINI: mini_draw(); break;
  }
}
