/* Settings: the installed games, each with Reset (its progress) and Uninstall
 * (behind a clear warning and a hold-to-confirm button); Reset all; starting
 * as Matrices, a calculator app with a secret way into NumPlay; and credits. */
#include <string.h>
#include "sys.h"
#include "ui.h"
#include "../../games/common/np_text.h"

#define BG_TOP 0x3B4252
#define BG_BOTTOM 0x171A21
#define DANGER 0xE5484D
#define DANGER_TOP 0x8C1D23
#define DANGER_BOTTOM 0x2A080B
#define OK_GREEN 0x2FB36B
#define ACCENT 0x7B43FF
#define ROW_H 34
#define ROWS_Y 50
#define HOLD_MS 1400

static char *fmt_kb(char *out, uint32_t bytes) {
  uint32_t kb = (bytes + 1023) / 1024;
  char t[8];
  int n = 0;
  do t[n++] = (char)('0' + kb % 10);
  while (kb /= 10);
  char *o = out;
  while (n) *o++ = t[--n];
  strcpy(o, T(" KB"));
  return out;
}

/* ---------------------------------------------------------------- list */
enum { ROW_GAME, ROW_RESET_ALL, ROW_DISGUISE, ROW_SECRET, ROW_HINT };
typedef struct {
  int n, sel, top, action; /* action: on a game, 0 Reset, 1 Uninstall */
  int8_t kind[NP_MAX_GAMES + 4], game[NP_MAX_GAMES + 4];
  int ngames;
  np_config_t cfg;
  float cursor;       /* animated selection */
} list_t;

static const char *const secret_names[NP_SECRET_COUNT] = {T("x,n,t key"), T("var key"), T("Toolbox key"), T("Pi key"),
                                                          T("Square root key"), T("Menu: Examples"),
                                                          T("Konami code")};

static void build_rows(list_t *l) {
  l->n = l->ngames = 0;
  for (int i = 0; i < np_game_count; i++)
    if (np_game_installed(i)) l->kind[l->n] = ROW_GAME, l->game[l->n++] = (int8_t)i, l->ngames++;
  l->kind[l->n++] = ROW_RESET_ALL;
  l->kind[l->n++] = ROW_DISGUISE;
  l->kind[l->n++] = ROW_SECRET;
  l->kind[l->n++] = ROW_HINT;
  l->sel = NP_MIN(l->sel, l->n - 1);
}

/* keeps the selection in view; the credits show under the last row */
static void scroll(list_t *l) {
  if (l->sel < l->top) l->top = l->sel;
  if (l->sel > l->top + 4) l->top = l->sel - 4;
  if (l->sel == l->n - 1) l->top = NP_MAX(0, l->n + 2 - 5);
}

static void draw_thumb(int game, int x, int y, int w, int h) {
  const np_game_t *g = &np_games[game];
  const uint8_t *px = ui_shot(game, 0);
  if (px) gfx_image(px, g->shots[0].palette, g->shots[0].ncolors, SHOT_W, SHOT_H, x, y, w, h, 5, 0);
  else gfx_rrect(x, y, w, h, 5, gfx_rgb(g->top), 256);
}

static void pill(int x, int y, int w, const char *label, bool on, uint32_t color) {
  if (on) gfx_rrect(x, y, w, 20, 10, gfx_rgb(color), 256);
  else gfx_rrect_outline(x, y, w, 20, 10, 1, 0xFFFF, 90);
  gfx_text_center(&np_font_small, x + w / 2, y + 14, label, 0xFFFF, on ? 256 : 190);
}

static void row_text(int y, const char *title, const char *sub, int a) {
  gfx_text(&np_font_body, 18, y + 15, title, 0xFFFF, a);
  gfx_text(&np_font_small, 18, y + 28, sub, 0xFFFF, a * 150 / 256);
}

static void toggle(int y, bool on, int a) {
  int tx = SCREEN_W - 58, ty = y + 8;
  gfx_rrect(tx, ty, 38, 18, 9, on ? gfx_rgb(OK_GREEN) : 0xFFFF, (on ? 256 : 70) * a / 256);
  gfx_circle(on ? tx + 29 : tx + 9, ty + 9, 7, 0xFFFF, a);
}

static void list_scene(void *ctx) {
  list_t *l = ctx;
  gfx_vgrad(0, 0, SCREEN_W, SCREEN_H, BG_TOP, BG_BOTTOM);
  if (gfx_y0 < 44) {
    gfx_icon(&np_icon_back, 14, 17, 0xFFFF, 200);
    gfx_text(&np_font_title, 36, 30, T("Settings"), 0xFFFF, 256);
    uint32_t total = 0;
    for (int i = 0; i < l->ngames; i++) total += np_game_size(l->game[i]);
    char kb[16];
    if (l->ngames) {
      gfx_text_right(&np_font_small, SCREEN_W - 14, 20, T("INSTALLED GAMES"), 0xFFFF, 150);
      gfx_text_right(&np_font_body, SCREEN_W - 14, 34, fmt_kb(kb, total), 0xFFFF, 230);
    }
  }
  /* selection highlight slides between rows */
  float cy = ROWS_Y + (l->cursor - l->top) * (ROW_H + 4);
  gfx_rrect(10, (int)cy, SCREEN_W - 20, ROW_H, 10, 0xFFFF, 46);
  for (int r = 0; r < 5; r++) {
    int i = l->top + r, y = ROWS_Y + r * (ROW_H + 4);
    if (y >= gfx_y1 || y + (i >= l->n ? 60 : ROW_H + 4) <= gfx_y0) continue; /* the credits are taller */
    if (i >= l->n) {
      /* credits, under the last row */
      if (i != l->n) continue;
      int w = gfx_text_width(&np_font_body, T("Made with ")) + 12 + gfx_text_width(&np_font_body, T(" by Mason Chen"));
      int x = 160 - w / 2;
      x = gfx_text(&np_font_body, x, y + 20, T("Made with "), 0xFFFF, 230);
      gfx_icon(&np_icon_heart, x + 1, y + 12, gfx_rgb(DANGER), 256);
      gfx_text(&np_font_body, x + 12, y + 20, T(" by Mason Chen"), 0xFFFF, 230);
      gfx_text_center(&np_font_small, 160, y + 36, "github.com/Mason363/NumPlay", 0xFFFF, 170);
      gfx_text_center(&np_font_small, 160, y + 50, "NumPlay " NP_VERSION, 0xFFFF, 120);
      continue;
    }
    bool on = i == l->sel;
    char kb[16];
    switch (l->kind[i]) {
      case ROW_GAME: {
        int game = l->game[i];
        draw_thumb(game, 16, y + 4, 35, 26);
        gfx_text(&np_font_body, 60, y + 15, np_games[game].title, 0xFFFF, 256);
        gfx_text(&np_font_small, 60, y + 28, fmt_kb(kb, np_game_size(game)), 0xFFFF, 150);
        if (on) {
          pill(SCREEN_W - 170, y + 7, 58, T("Reset"), l->action == 0, ACCENT);
          pill(SCREEN_W - 104, y + 7, 86, "", l->action == 1, DANGER);
          gfx_icon(&np_icon_trash, SCREEN_W - 96, y + 9, 0xFFFF, l->action == 1 ? 256 : 190);
          gfx_text(&np_font_small, SCREEN_W - 78, y + 21, T("Uninstall"), 0xFFFF, l->action == 1 ? 256 : 190);
        }
        break;
      }
      case ROW_RESET_ALL:
        row_text(y, T("Reset all games"), T("Deletes the progress of every game"), 256);
        if (on) pill(SCREEN_W - 76, y + 7, 58, T("Reset"), true, ACCENT);
        break;
      case ROW_DISGUISE:
        row_text(y, T("Start as Matrices"), T("A math app first; NumPlay opens in secret"), 256);
        toggle(y, l->cfg.disguise, 256);
        break;
      case ROW_HINT: {
        int a = l->cfg.disguise ? 256 : 130;
        row_text(y, T("Show a hint"), T("Matrices names the secret, small and gray"), a);
        toggle(y, l->cfg.hint, a);
        break;
      }
      case ROW_SECRET: {
        int a = l->cfg.disguise ? 256 : 130;
        row_text(y, T("Secret way in"), T("Opens NumPlay from Matrices"), a);
        const char *v = secret_names[l->cfg.secret];
        int w = gfx_text_width(&np_font_small, v);
        gfx_text_right(&np_font_small, SCREEN_W - 32, y + 21, v, 0xFFFF, a);
        if (on) {
          gfx_icon(&np_icon_left, SCREEN_W - 44 - w, y + 10, 0xFFFF, a);
          gfx_icon(&np_icon_right, SCREEN_W - 26, y + 10, 0xFFFF, a);
        }
        break;
      }
    }
  }
}

/* ---------------------------------------------------------------- dialogs */
enum { ACT_UNINSTALL, ACT_RESET, ACT_RESET_ALL };
typedef struct {
  int game;
  int choice;         /* 0 cancel, 1 uninstall or reset */
  float hold;         /* 0..1 while OK is held on "Uninstall" or "Reset" */
  bool hinted;
  int stage;          /* 0 confirm, 1 working, 2 done, 3 error */
  int error;
  float progress;
  uint32_t freed;
  int action;         /* ACT_* */
} dialog_t;

static const char *error_text(int e) {
  switch (e) {
    case NP_UNINSTALL_UNSUPPORTED:
      return T("Your calculator's software doesn't allow apps to uninstall parts of themselves. Update it to Epsilon 21 or newer at my.numworks.com, then try again.");
    case NP_UNINSTALL_BATTERY:
      return T("The battery is too low to safely erase flash memory. Plug in your calculator, then try again.");
    default:
      return T("Something went wrong while erasing. To be safe, reinstall NumPlay from my.numworks.com/apps.");
  }
}

/* The red button fills up while OK is held: a stray press of OK, right next
   to Back, can't erase anything. */
static void hold_button(const dialog_t *d, const char *label) {
  int bx = 172, by = 198, bw = 110, bh = 28;
  if (d->choice == 1) {
    gfx_rrect(bx, by + 2, bw, bh, bh / 2, 0, 64);
    gfx_rrect(bx, by, bw, bh, bh / 2, gfx_rgb(DANGER), 256);
    int fill = (int)(bw * d->hold);
    if (fill > 0) gfx_rrect(bx, by, NP_MAX(fill, bh), bh, bh / 2, 0xFFFF, 110);
    gfx_text_center(&np_font_body, bx + bw / 2, by + 18, d->hinted ? T("Hold OK") : label, 0xFFFF, 256);
  } else {
    gfx_rrect(bx, by, bw, bh, bh / 2, 0xFFFF, 40);
    gfx_text_center(&np_font_body, bx + bw / 2, by + 18, label, 0xFFFF, 200);
  }
}

/* Reset and Reset all: a question (Cancel first, hold OK to reset), then done */
static void reset_scene(dialog_t *d) {
  const char *title = d->action == ACT_RESET_ALL ? T("every game") : np_games[d->game].title;
  char line[128], body[384];
  if (d->stage == 2) {
    gfx_vgrad(0, 0, SCREEN_W, SCREEN_H, 0x1F5A3C, 0x0B1A12);
    gfx_circle(160, 74, 34, gfx_rgb(OK_GREEN), 256);
    gfx_icon(&np_icon_check, 145, 59, 0xFFFF, 256);
    strcpy(line, d->action == ACT_RESET_ALL ? T("Every game") : title);
    strcat(line, T(" was reset"));
    gfx_text_center(&np_font_title, 160, 142, line, 0xFFFF, 256);
    gfx_text_center(&np_font_body, 160, 166, T("Its progress starts over."), 0xFFFF, 200);
    ui_button(110, 192, 100, 28, T("OK"), true, OK_GREEN, 256);
    return;
  }
  gfx_vgrad(0, 0, SCREEN_W, SCREEN_H, DANGER_TOP, DANGER_BOTTOM);
  gfx_icon(&np_icon_warning, 137, 14, 0xFFFF, 256);
  strcpy(line, T("Reset "));
  strcat(line, title);
  strcat(line, T("?"));
  gfx_text_center(&np_font_title, 160, 80, line, 0xFFFF, 256);
  strcpy(body, T("This deletes the saved progress of "));
  strcat(body, title);
  strcat(body, T(": best scores, unlocked levels and settings. You can't undo this."));
  int y = gfx_paragraph(&np_font_body, 160, 102, 280, 16, body, 0xFFFF, 240);
  gfx_paragraph(&np_font_small, 160, y + 4, 290, 13, T("Games stay installed, and levels you made in an editor are kept."),
                0xFFFF, 170);
  ui_button(38, 198, 110, 28, T("Cancel"), d->choice == 0, 0x555D70, 256);
  hold_button(d, T("Reset"));
}

static void dialog_scene(void *ctx) {
  dialog_t *d = ctx;
  if (d->action != ACT_UNINSTALL) {
    reset_scene(d);
    return;
  }
  const np_game_t *g = &np_games[d->game];
  char line[128];
  if (d->stage == 2) {
    gfx_vgrad(0, 0, SCREEN_W, SCREEN_H, 0x1F5A3C, 0x0B1A12);
    gfx_circle(160, 74, 34, gfx_rgb(OK_GREEN), 256);
    gfx_icon(&np_icon_check, 145, 59, 0xFFFF, 256);
    strcpy(line, g->title);
    strcat(line, T(" was uninstalled"));
    gfx_text_center(&np_font_title, 160, 142, line, 0xFFFF, 256);
    char kb[16];
    strcpy(line, fmt_kb(kb, d->freed));
    strcat(line, T(" of flash memory freed"));
    gfx_text_center(&np_font_body, 160, 166, line, 0xFFFF, 200);
    ui_button(110, 192, 100, 28, T("OK"), true, OK_GREEN, 256);
    return;
  }
  gfx_vgrad(0, 0, SCREEN_W, SCREEN_H, DANGER_TOP, DANGER_BOTTOM);
  gfx_icon(&np_icon_warning, 137, 14, 0xFFFF, 256);
  if (d->stage == 3) {
    gfx_text_center(&np_font_title, 160, 84, T("Couldn't uninstall"), 0xFFFF, 256);
    gfx_paragraph(&np_font_body, 160, 112, 280, 17, error_text(d->error), 0xFFFF, 220);
    ui_button(110, 196, 100, 28, T("OK"), true, DANGER, 256);
    return;
  }
  if (d->stage == 1) {
    strcpy(line, T("Uninstalling "));
    strcat(line, g->title);
    strcat(line, T("..."));
    gfx_text_center(&np_font_title, 160, 96, line, 0xFFFF, 256);
    gfx_rrect(40, 118, 240, 10, 5, 0xFFFF, 60);
    int w = (int)(240 * d->progress);
    if (w >= 10) gfx_rrect(40, 118, w, 10, 5, 0xFFFF, 256);
    gfx_text_center(&np_font_body, 160, 152, T("Don't turn off your calculator."), 0xFFFF, 200);
    return;
  }
  strcpy(line, T("Uninstall "));
  strcat(line, g->title);
  strcat(line, T("?"));
  gfx_text_center(&np_font_title, 160, 80, line, 0xFFFF, 256);
  char body[384];
  strcpy(body, T("This deletes "));
  strcat(body, g->title);
  strcat(body, T(" and all of its saved progress. You can't undo this."));
  int y = gfx_paragraph(&np_font_body, 160, 102, 280, 16, body, 0xFFFF, 240);
  gfx_paragraph(&np_font_small, 160, y + 4, 290, 13,
                T("To play it again, reinstall NumPlay from my.numworks.com/apps. Your other games keep their progress."),
                0xFFFF, 170);
  ui_button(38, 198, 110, 28, T("Cancel"), d->choice == 0, 0x555D70, 256);
  hold_button(d, T("Uninstall"));
}

static dialog_t *active_dialog;
static void on_progress(int done, int total) {
  active_dialog->progress = total ? (float)done / total : 1;
  ui_frame(dialog_scene, active_dialog);
}

static void wait_ok(dialog_t *d) {
  ui_keys_t k = {np_keys(), 0, 0};
  ui_frame(dialog_scene, d);
  for (;;) {
    uint32_t p = ui_poll(&k);
    if (p & (K_OK | K_BACK | K_HOME)) return;
    np_sleep(16);
  }
}

/* Cancel, or OK held on the red button: true once it has been held long enough */
static bool hold_to_confirm(dialog_t *d) {
  ui_keys_t k = {np_keys(), 0, 0};
  uint32_t last = np_millis();
  bool dirty = true;
  for (;;) {
    uint32_t now = np_millis(), dt = now - last;
    last = now;
    uint32_t p = ui_poll(&k);
    if (p & (K_BACK | K_HOME)) return false;
    if (p & K_LEFT) d->choice = 0, d->hold = 0, dirty = true;
    if (p & K_RIGHT) d->choice = 1, dirty = true;
    if (p & K_OK) {
      if (d->choice == 0) return false;
      d->hinted = true; /* a short press only shows how */
      dirty = true;
    }
    if (d->choice == 1 && (k.held & K_OK)) {
      d->hold += dt / (float)HOLD_MS;
      dirty = true;
      if (d->hold >= 1) return true;
    } else if (d->hold > 0) {
      d->hold = NP_MAX(d->hold - dt / 300.f, 0.f);
      dirty = true;
    }
    if (dirty) {
      ui_frame(dialog_scene, d);
      dirty = false;
    } else {
      np_sleep(16);
    }
  }
}

/* Reset (game >= 0) or Reset all (game < 0), after asking. */
static void reset_flow(int game) {
  dialog_t d = {game < 0 ? 0 : game, 0, 0, false, 0, 0, 0, 0, game < 0 ? ACT_RESET_ALL : ACT_RESET};
  if (!hold_to_confirm(&d)) return;
  if (game >= 0) np_reset_game(game);
  else
    for (int i = 0; i < np_game_count; i++) np_reset_game(i);
  np_wait_release();
  d.stage = 2;
  wait_ok(&d);
}

/* Returns true if the game was uninstalled. */
static bool uninstall_flow(int game) {
  dialog_t d = {game, 0, 0, false, 0, 0, 0, 0, ACT_UNINSTALL};
  active_dialog = &d;
  if (!np_uninstall_supported() || !np_battery_ok()) {
    d.stage = 3;
    d.error = !np_uninstall_supported() ? NP_UNINSTALL_UNSUPPORTED : NP_UNINSTALL_BATTERY;
    wait_ok(&d);
    return false;
  }
  if (!hold_to_confirm(&d)) return false;
  d.stage = 1;
  d.freed = np_game_size(game);
  ui_frame(dialog_scene, &d);
  int r = np_uninstall(game, on_progress);
  np_wait_release();
  if (r == NP_UNINSTALL_OK) {
    d.stage = 2;
  } else {
    d.stage = 3;
    d.error = r;
  }
  wait_ok(&d);
  return r == NP_UNINSTALL_OK;
}

void np_settings(void) {
  ui_init();
  list_t l = {0};
  np_config_load(&l.cfg);
  build_rows(&l);
  ui_keys_t k = {np_keys(), 0, 0};
  uint32_t last = np_millis();
  bool dirty = true;
  for (;;) {
    uint32_t now = np_millis();
    float dt = NP_MIN((now - last) / 1000.f, 0.05f);
    last = now;
    uint32_t p = ui_poll(&k);
    if (p & (K_BACK | K_HOME)) return;
    int kind = l.kind[l.sel];
    if (p & K_UP) l.sel = NP_MAX(l.sel - 1, 0), l.action = 0;
    if (p & K_DOWN) l.sel = NP_MIN(l.sel + 1, l.n - 1), l.action = 0;
    if (kind == ROW_GAME && (p & (K_LEFT | K_RIGHT))) l.action = (p & K_RIGHT) != 0;
    if (kind == ROW_SECRET && (p & (K_LEFT | K_RIGHT | K_OK))) {
      l.cfg.secret = (uint8_t)((l.cfg.secret + ((p & K_LEFT) ? NP_SECRET_COUNT - 1 : 1)) % NP_SECRET_COUNT);
      np_config_save(&l.cfg);
    }
    if ((kind == ROW_DISGUISE || kind == ROW_HINT) && (p & (K_OK | K_LEFT | K_RIGHT))) {
      if (kind == ROW_DISGUISE) l.cfg.disguise = !l.cfg.disguise;
      else l.cfg.hint = !l.cfg.hint;
      np_config_save(&l.cfg);
    }
    bool go = (p & K_OK) || (kind == ROW_GAME && (p & K_BACKSPACE));
    if (go && (kind == ROW_GAME || kind == ROW_RESET_ALL)) {
      if (kind == ROW_RESET_ALL) reset_flow(-1);
      else if (l.action == 0 && !(p & K_BACKSPACE)) reset_flow(l.game[l.sel]);
      else if (uninstall_flow(l.game[l.sel])) build_rows(&l), l.cursor = l.sel;
      k.held = np_keys();
      last = np_millis();
    }
    scroll(&l);
    if (p) dirty = true;
    float c = ui_approach(l.cursor, (float)l.sel, dt, 0.05f);
    if (c != l.cursor) dirty = true;
    l.cursor = c;
    if (dirty) {
      ui_frame(list_scene, &l);
      dirty = false;
    } else {
      np_sleep(16);
    }
  }
}
