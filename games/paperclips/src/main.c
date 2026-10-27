/* Universal Paperclips on the NumWorks calculator: Frank Lantz's game, its rules and numbers from its web build
 * (sim.c, projects.c), on a screen made for the keypad (ui.c).
 *
 * Keys: Up and Down choose a button, Left and Right change panel, OK presses the button (held, it presses again and
 * again, like clicking fast), Back pauses (Resume, How to play, Quit game), Home saves and quits.
 *
 * Time: the game's timers run on the calculator's clock, caught up every frame (up to half a second at once), so
 * the game keeps its pace on the slower N0110 and N0115. */
#include <string.h>
#include "pc.h"
#include "plat.h"
#include "save.h"
#include "ui.h"

#ifndef PC_CATCH_UP
#define PC_CATCH_UP 500   /* the most game time one frame catches up (ms) */
#endif

static pc_game game;
static pc_fleet fleet;
static pc_ui ui;
static uint8_t guide_read;
static uint32_t saved_at, now;
/* frames drawn, the slowest (ms) and those over 33 ms: read by tools/emu.py */
#ifndef HOST
__attribute__((used, externally_visible))
#endif
uint32_t perf_frames, perf_max, perf_slow;

static void save(void) {
  save_write(&game, guide_read, (uint8_t)ui.tab);
  saved_at = now;
}

static void fresh(double prestigeU, double prestigeS) {
  pc_new(&game, plat_random() ^ now ^ 0x9A9E5C11u, prestigeU, prestigeS);
  int overlay = ui.overlay;
  ui_init(&ui);
  ui.overlay = overlay;
}

/* a new universe (the Accept projects) or Quantum Temporal Reversion: the game starts over, prestige kept */
static void restart_if_asked(void) {
  if (!game.resetPending) return;
  fresh(game.prestigeU, game.prestigeS);
  ui.overlay = O_NONE;
  save();
}

static bool paused(void) { return ui.overlay != O_NONE; }

static int menu_keys(uint32_t hit, int n) {
  if (hit & PK_UP) ui.sel = (ui.sel + n - 1) % n;
  if (hit & PK_DOWN) ui.sel = (ui.sel + 1) % n;
  return ui.sel;
}

/* the keys over the game: false to quit */
static bool overlay_keys(uint32_t hit) {
  switch (ui.overlay) {
    case O_GUIDE:
      if (hit & (PK_OK | PK_BACK)) {
        ui.overlay = O_NONE, guide_read = 1;
        save();
      }
      break;
    case O_PAUSE:
      menu_keys(hit, ui_pause_count());
      if (hit & PK_BACK) ui.overlay = O_NONE;
      else if (hit & PK_OK) switch (ui.sel) {
        case PAUSE_RESUME: ui.overlay = O_NONE; break;
        case PAUSE_HELP: ui.overlay = O_HELP, ui.page = 0; break;
        case PAUSE_QUIT: ui.overlay = O_QUIT, ui.sel = 0; break;
        default: ui.overlay = O_NEW, ui.sel = 1; break;
      }
      break;
    case O_HELP:
      if ((hit & PK_RIGHT) && ui.page + 1 < PC_HELP_PAGES) ui.page++;
      else if ((hit & PK_LEFT) && ui.page > 0) ui.page--;
      else if (hit & (PK_OK | PK_BACK)) ui.overlay = O_PAUSE, ui.sel = PAUSE_HELP;
      break;
    case O_QUIT:
      menu_keys(hit, 2);
      if (hit & PK_BACK) ui.overlay = O_PAUSE, ui.sel = PAUSE_QUIT;
      else if (hit & PK_OK) {
        if (ui.sel == 0) return false;
        ui.overlay = O_PAUSE, ui.sel = PAUSE_QUIT;
      }
      break;
    case O_RESTART:
      menu_keys(hit, 2);
      if (hit & PK_BACK) ui.overlay = O_NONE;
      else if (hit & PK_OK) {
        ui.overlay = O_NONE;
        if (ui.sel == 0) pc_press(B_PROJECT + P217), restart_if_asked();
      }
      break;
    case O_NEW:
      menu_keys(hit, 2);
      if (hit & PK_BACK) ui.overlay = O_PAUSE, ui.sel = PAUSE_NEW;
      else if (hit & PK_OK) {
        if (ui.sel == 0) {
          fresh(game.prestigeU, game.prestigeS);
          ui.overlay = O_NONE;
          save();
        } else {
          ui.overlay = O_PAUSE, ui.sel = PAUSE_NEW;
        }
      }
      break;
    default: break;
  }
  return true;
}

int main(void) {
  plat_begin();
  G = &game, F = &fleet;
  memset(&fleet, 0, sizeof fleet);
  now = plat_millis();
  ui_init(&ui);
  uint8_t tab = 0;
  if (save_load(&game, &guide_read, &tab)) {
    pc_loaded(&game);
    ui.tab = tab < T_COUNT ? tab : 0;
  } else {
    fresh(0, 0);
    guide_read = 0;
  }
  if (!guide_read) ui.overlay = O_GUIDE;
  saved_at = now;
  uint32_t stale = plat_keys(), prev = stale, last = now;
  uint32_t ud_at = 0, lr_at = 0, ok_at = 0;
  int ok_button = -1;
  for (;;) {
    now = plat_millis();
    uint32_t t0 = now;
    uint32_t keys = plat_keys();
    stale &= keys;   /* keys still down from before don't count */
    uint32_t live = keys & ~stale, hit = live & ~prev;
    /* held: Up and Down again after 350 ms, then every 90; Left and Right after 450, then every 220 */
    uint32_t ud = live & (PK_UP | PK_DOWN), lr = live & (PK_LEFT | PK_RIGHT);
    if (hit & ud) ud_at = now + 350;
    else if (ud && (int32_t)(now - ud_at) >= 0) hit |= ud, ud_at = now + 90;
    if (hit & lr) lr_at = now + 450;
    else if (lr && (int32_t)(now - lr_at) >= 0) hit |= lr, lr_at = now + 220;
    prev = live;
    if (keys & PK_HOME) break;
    if (paused()) {
      int before = ui.overlay;
      if (!overlay_keys(hit)) break;
      if (ui.overlay != before) stale = keys, prev = keys;
      ok_button = -1;
    } else {
      if (hit & PK_BACK) {
        ui.overlay = O_PAUSE, ui.sel = PAUSE_RESUME;
        save();
      } else {
        if (hit & PK_UP) ui_up(&ui);
        if (hit & PK_DOWN) ui_down(&ui);
        if (hit & PK_LEFT) ui_tab(&ui, -1);
        if (hit & PK_RIGHT) ui_tab(&ui, 1);
        /* OK presses the button; held, a button that can be pressed again is, after 400 ms, every 70 */
        if (hit & PK_OK) {
          int b = ui_focused(&ui);
          ok_button = -1;
          if (b == B_PROJECT + P217) {
            ui.overlay = O_RESTART, ui.sel = 1;
            stale = keys, prev = keys;
          } else if (b >= 0) {
            pc_press(b);
            if (ui_repeats(b)) ok_button = b, ok_at = now + 400;
          }
        } else if ((live & PK_OK) && ok_button >= 0 && (int32_t)(now - ok_at) >= 0) {
          if (ui_focused(&ui) == ok_button) pc_press(ok_button), ok_at = now + 70;
          else ok_button = -1;
        }
        if (!(live & PK_OK)) ok_button = -1;
      }
    }
    uint32_t dt = now - last;
    last = now;
    if (dt > PC_CATCH_UP) dt = PC_CATCH_UP;
    if (!paused()) {
      pc_advance(dt);
      restart_if_asked();
    }
    if ((int32_t)(now - saved_at) >= 25000) save();
    ui.clock = now;
    ui_render(&ui);
    uint32_t spent = plat_millis() - t0;
    perf_frames++;
    if (spent > perf_max) perf_max = spent;
    if (spent > 33) perf_slow++;
    if (spent < 30) plat_sleep(30 - spent);
  }
  save();
  return plat_end();
}
