/* The game's screens: which one is up, the pause menu and the end of a run (the menus are in menu.c). */
#include <stdio.h>
#include <string.h>
#include "game.h"
#include "gfx.h"

int g_mode;
bool g_running = true;
bool g_nohud;   /* (pictures for the README: the road without the dashboard) */
uint32_t g_frame;
static uint32_t held;
static int pause_sel, pause_ask, end_t;

void game_init(void) {
  save_restore_copy();
  save_load();
  menu_enter(MODE_TITLE);
}

void game_quit(void) {
  if (g_mode == MODE_RUN || g_mode == MODE_PAUSE) run_finish();   /* (a run left halfway: its coins are kept) */
  else save_write();
  g_running = false;
}

static void start_run(void) {
  run_start(sv.vehicle, sv.stage);
  g_mode = MODE_RUN;
}

/* ------------------------------------------------------------------ pause */
static const char *const pause_items[] = {"RESUME", "RESTART", "GARAGE", "QUIT GAME"};

static void pause_frame(uint32_t pressed) {
  if (pause_ask) {
    if (pressed & K_OK) game_quit();
    else if (pressed & K_BACK) pause_ask = 0;
    return;
  }
  if (pressed & K_UP) pause_sel = (pause_sel + 3) % 4;
  if (pressed & K_DOWN) pause_sel = (pause_sel + 1) % 4;
  if (pressed & K_BACK) g_mode = MODE_RUN;
  if (pressed & K_OK) {
    switch (pause_sel) {
      case 0: g_mode = MODE_RUN; break;
      case 1: run_finish(), start_run(); break;
      case 2: run_finish(), menu_enter(MODE_GARAGE); break;
      case 3: pause_ask = 1; break;
    }
  }
}

static void pause_draw(void) {
  run_draw(true);
  gfx_fill(0, 0, SCREEN_W, SCREEN_H, C_BLACK, 130);
  gfx_rrect(90, 44, 140, pause_ask ? 138 : 156, 10, rgb565(0x2A2D33), 240);
  gfx_rrect(90, 44, 140, 30, 10, rgb565(0x4A4F57), 255);
  gfx_fill(90, 64, 140, 10, rgb565(0x4A4F57), 255);
  gfx_text(FONT_L, 160, 67, "PAUSED", C_WHITE, C_BLACK, 1);
  if (pause_ask) {
    gfx_text(FONT_M, 160, 110, "Quit the game?", C_WHITE, C_BLACK, 1);
    gfx_rrect(104, 132, 54, 26, 6, rgb565(0x3DAA3D), 255);
    gfx_rrect(162, 132, 54, 26, 6, rgb565(0x8A2E2E), 255);
    gfx_text(FONT_M, 131, 151, "OK", C_WHITE, C_BLACK, 1);
    gfx_text(FONT_M, 189, 151, "BACK", C_WHITE, C_BLACK, 1);
    return;
  }
  for (int i = 0; i < 4; i++) {
    int y = 84 + i * 28;
    bool on = i == pause_sel;
    gfx_rrect(102, y, 116, 24, 6, on ? rgb565(0xF2B531) : rgb565(0x40444B), 255);
    gfx_text(FONT_M, 160, y + 17, pause_items[i], on ? C_BLACK : C_WHITE, on ? rgb565(0xF2B531) : C_BLACK, 1);
  }
}

/* ------------------------------------------------------------------ the end of a run */
static void end_frame(uint32_t pressed) {
  end_t++;
  if (end_t < 20) return;
  if (pressed & K_OK) start_run();
  else if (pressed & K_BACK) menu_enter(MODE_GARAGE);
}

static void end_draw(void) {
  run_draw(false);
  gfx_fill(0, 0, SCREEN_W, SCREEN_H, C_BLACK, 110);
  const char *title = run.end == END_DRIVER_DOWN ? "DRIVER DOWN" : "OUT OF FUEL";
  gfx_text(FONT_XL, 160, 54, title, run.end == END_DRIVER_DOWN ? rgb565(0xFF5A3C) : rgb565(0xFFC21A), C_BLACK, 1);
  char t[48], n[20];
  gfx_rrect(64, 74, 192, 112, 10, rgb565(0x2A2D33), 235);
  snprintf(t, sizeof t, "%dm", (int)run.dist);
  gfx_text(FONT_L, 160, 102, t, C_WHITE, C_BLACK, 1);
  if (run.new_best && (end_t / 15) % 2 == 0) gfx_text(FONT_M, 160, 120, "NEW RECORD!", rgb565(0xFFE14A), C_BLACK, 1);
  else {
    snprintf(t, sizeof t, "Best %dm", (int)sv.best[run.stage][run.vehicle]);
    gfx_text(FONT_M, 160, 120, t, rgb565(0xC8CDD4), C_BLACK, 1);
  }
  format_number(n, run.coins);
  gfx_text(FONT_S, 80, 140, "Coins", rgb565(0xC8CDD4), C_BLACK, 0);
  gfx_text(FONT_S, 240, 140, n, C_WHITE, C_BLACK, 2);
  format_number(n, run.bonus);
  gfx_text(FONT_S, 80, 154, "Tricks and levels", rgb565(0xC8CDD4), C_BLACK, 0);
  gfx_text(FONT_S, 240, 154, n, C_WHITE, C_BLACK, 2);
  gfx_fill(80, 160, 160, 1, rgb565(0x6A6F77), 255);
  gfx_text(FONT_M, 80, 176, "Total", C_WHITE, C_BLACK, 0);
  draw_coins_counter(240, 176, run_total(), 2);
  gfx_rrect(58, 196, 100, 28, 7, rgb565(0x3DAA3D), 255);
  gfx_rrect(162, 196, 100, 28, 7, rgb565(0x4A4F57), 255);
  gfx_text(FONT_M, 108, 216, "OK: Retry", C_WHITE, C_BLACK, 1);
  gfx_text(FONT_M, 212, 216, "Back: Garage", C_WHITE, C_BLACK, 1);
}

/* ------------------------------------------------------------------ the frame */
void game_frame(uint32_t keys) {
  uint32_t pressed = keys & ~held;
  held = keys;
  g_frame++;
  if (keys & K_HOME) {
    game_quit();
    return;
  }
  switch (g_mode) {
    case MODE_RUN:
      if (pressed & K_BACK && !run.over) {
        g_mode = MODE_PAUSE, pause_sel = 0, pause_ask = 0;
        break;
      }
      run_frame(keys);
      if (run.over && run.over_t > 1) {
        run_finish();
        g_mode = MODE_END, end_t = 0;
      }
      break;
    case MODE_PAUSE: pause_frame(pressed); break;
    case MODE_END: end_frame(pressed); break;
    default: menu_frame(keys, pressed); break;
  }
}

void game_draw(void) {
  gfx_begin();
  switch (g_mode) {
    case MODE_RUN: run_draw(!g_nohud); break;
    case MODE_PAUSE: pause_draw(); break;
    case MODE_END: end_draw(); break;
    default: menu_draw(); break;
  }
  gfx_end();
}
