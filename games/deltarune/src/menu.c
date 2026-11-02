#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif
/* The menus (menu.h). */
#include <stdio.h>
#include <string.h>
#include "game.h"
#include "gfx.h"
#include "menu.h"
#include "plat.h"
#include "res.h"
#include "rooms.h"
#include "save.h"
#include "tables.h"
#include "text.h"
#include "world.h"

#define GREY rgb565(0x808080)

/* ---------------------------------------------------------------- the vessel's name */
static int nm_sel, nm_len;
static const char nm_letters[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";

void menu_name_start(void) {
  memset(sv.name, 0, sizeof sv.name);
  nm_sel = 0, nm_len = 0;
  g_mode = MODE_INTRO;
}

void menu_name_update(void) {
  /* 26 letters on 4 rows of 7, then BACK and END */
  int n = 26 + 2;
  if (g_pressed & K_LEFT) nm_sel = (nm_sel + n - 1) % n;
  if (g_pressed & K_RIGHT) nm_sel = (nm_sel + 1) % n;
  if (g_pressed & K_UP) nm_sel = nm_sel >= 26 ? 21 + (nm_sel - 26) * 3 : nm_sel >= 7 ? nm_sel - 7 : 26;
  if (g_pressed & K_DOWN) nm_sel = nm_sel >= 26 ? nm_sel - 26 : nm_sel + 7 < 26 ? nm_sel + 7 : 26 + (nm_sel % 7 > 3);
  if (g_pressed & K_X) {
    if (nm_len) sv.name[--nm_len] = 0;
  }
  if (g_pressed & K_Z) {
    if (nm_sel < 26) {
      if (nm_len < 12) sv.name[nm_len++] = nm_letters[nm_sel];
    } else if (nm_sel == 26) {
      if (nm_len) sv.name[--nm_len] = 0;
    } else if (nm_len) {
      game_new();
    }
  }
}

void menu_name_draw(void) {
  gfx_rect(0, 0, SCREEN_W, SCREEN_H, C_BLACK, 255);
  const char *t = "ENTER YOUR NAME.";
  gfx_text(FONT_MAIN, 160 - gfx_text_width(FONT_MAIN, t, 64) / 2, 30, t, C_WHITE, 255);
  gfx_text(FONT_MAIN, 160 - nm_len * 4, 60, sv.name, C_WHITE, 255);
  for (int i = 0; i < 26; i++) {
    char s[2] = {nm_letters[i], 0};
    gfx_text(FONT_MAIN, 76 + (i % 7) * 26, 100 + (i / 7) * 22, s, nm_sel == i ? C_YELLOW : C_WHITE, 255);
  }
  gfx_text(FONT_MAIN, 80, 196, "BACK", nm_sel == 26 ? C_YELLOW : C_WHITE, 255);
  gfx_text(FONT_MAIN, 200, 196, "END", nm_sel == 27 ? C_YELLOW : C_WHITE, 255);
  gfx_text(FONT_SMALL, 6, 228, "OK letter  Back erase", GREY, 255);
}

/* ---------------------------------------------------------------- the SAVE menu (obj_savemenu) */
static int sm_state;      /* 0 Save/Return, 1 saved, 2 done */
static int sm_sel;
static bool menu_from_save;

void menu_save_open(void) {
  sm_state = 0, sm_sel = 0;
  menu_from_save = true;
  g_mode = MODE_MENU;
}
bool menu_save_done(void) { return !menu_from_save; }

static void time_text(char *buf, uint32_t frames) {
  uint32_t s = frames / 30;
  snprintf(buf, 16, "%lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
}

static void save_update(void) {
  if (sm_state == 0) {
    if (g_pressed & (K_LEFT | K_RIGHT)) sm_sel ^= 1;
    if (g_pressed & K_Z) {
      if (sm_sel == 0) {
        sv.room = (int16_t)g_room, sv.x = (int16_t)pl.x, sv.y = (int16_t)pl.y, sv.facing = pl.dir;
        save_write();
        sm_state = 1;
      } else {
        sm_state = 2;
      }
    }
    if (g_pressed & K_X) sm_state = 2;
  } else if (sm_state == 1) {
    if (g_pressed & (K_Z | K_X)) sm_state = 2;
  }
  if (sm_state == 2) {
    menu_from_save = false;
    g_mode = MODE_WORLD;
  }
}

static void save_draw(void) {
  world_draw_scene();
  bool dark = world_dark();
  int x0 = 54, y0 = 49, w = 212, h = 82;
  if (dark) {
    gfx_rect(x0, y0, w, h, C_BLACK, 255);
    gfx_frame_rect(x0, y0, w, h, 3, C_WHITE);
  } else {
    gfx_rect(x0, y0, w, h, C_WHITE, 255);
    gfx_rect(x0 + 3, y0 + 3, w - 6, h - 6, C_BLACK, 255);
  }
  uint16_t col = sm_state == 1 ? C_YELLOW : C_WHITE;
  char buf[32];
  gfx_text(FONT_MAIN, x0 + 16, y0 + 12, sv.name, col, 255);
  snprintf(buf, sizeof buf, "LV %d", sv.lv);
  gfx_text(FONT_MAIN, x0 + 100, y0 + 12, buf, col, 255);
  time_text(buf, sv.time);
  gfx_text(FONT_MAIN, x0 + w - 16 - gfx_text_width(FONT_MAIN, buf, 16), y0 + 12, buf, col, 255);
  gfx_text(FONT_MAIN, x0 + 16, y0 + 32, room_title(g_room), col, 255);
  if (sm_state == 1) {
    gfx_text(FONT_MAIN, x0 + 30, y0 + 58, "File saved.", C_YELLOW, 255);
  } else {
    gfx_text(FONT_MAIN, x0 + 30, y0 + 58, "Save", sm_sel == 0 ? C_YELLOW : C_WHITE, 255);
    gfx_text(FONT_MAIN, x0 + 120, y0 + 58, "Return", sm_sel == 1 ? C_YELLOW : C_WHITE, 255);
    gfx_sprite(spr_heartsmall, 0, x0 + (sm_sel ? 106 : 16), y0 + 62, 0, 255);
  }
}

/* ---------------------------------------------------------------- the C menu */
static int cm_sel, cm_sub, cm_state;

void menu_open(void) {
  menu_from_save = false;
  cm_sel = 0, cm_sub = 0, cm_state = 0;
  g_mode = MODE_MENU;
}

static int item_slots(void) {
  int n = 0;
  while (n < NITEMS && sv.item[n]) n++;
  return n;
}

static void cmenu_update(void) {
  if (cm_state == 0) {
    if (g_pressed & K_LEFT) cm_sel = (cm_sel + 3) % 4;
    if (g_pressed & K_RIGHT) cm_sel = (cm_sel + 1) % 4;
    if (g_pressed & (K_X | K_C)) g_mode = MODE_WORLD;
    if ((g_pressed & K_Z) && cm_sel == 0 && item_slots()) cm_state = 1, cm_sub = 0;
  } else {
    int n = item_slots();
    if (g_pressed & K_UP) cm_sub = (cm_sub + n - 1) % (n ? n : 1);
    if (g_pressed & K_DOWN) cm_sub = (cm_sub + 1) % (n ? n : 1);
    if (g_pressed & K_X) cm_state = 0;
  }
}

static void cmenu_draw(void) {
  world_draw_scene();
  /* the top bar: four buttons, and the money */
  gfx_rect(0, 0, SCREEN_W, 40, C_BLACK, 255);
  static const char *names[4] = {"ITEM", "EQUIP", "POWER", "CONFIG"};
  for (int i = 0; i < 4; i++) {
    int x = 60 + i * 50;
    gfx_text(FONT_SMALL, x, 6, names[i], cm_sel == i ? C_YELLOW : GREY, 255);
  }
  char buf[24];
  snprintf(buf, sizeof buf, "D$ %ld", (long)sv.gold);
  gfx_text(FONT_MAIN, 250, 12, buf, C_WHITE, 255);
  if (cm_state == 1) {
    gfx_rect(40, 60, 240, 120, C_BLACK, 255);
    gfx_frame_rect(40, 60, 240, 120, 2, C_WHITE);
    for (int i = 0; i < item_slots(); i++) {
      const ItemDef *d = &item_defs[sv.item[i] < item_count ? sv.item[i] : 0];
      gfx_text(FONT_MAIN, 66 + (i % 2) * 110, 70 + (i / 2) * 18, d->name != 0xFFFF ? str(d->name) : "?",
               cm_sub == i ? C_YELLOW : C_WHITE, 255);
    }
  }
}

void menu_update(void) {
  if (menu_from_save) save_update();
  else cmenu_update();
}
void menu_draw(void) {
  if (menu_from_save) save_draw();
  else cmenu_draw();
}

/* ---------------------------------------------------------------- the game over */
static int go_t;
void menu_gameover_start(void) {
  go_t = 0;
  g_mode = MODE_GAMEOVER;
}
void menu_gameover_update(void) {
  go_t++;
  if (go_t > 60 && (g_pressed & K_Z)) {
    if (!save_load()) {
      g_mode = MODE_TITLE;
    }
  }
}
void menu_gameover_draw(void) {
  gfx_rect(0, 0, SCREEN_W, SCREEN_H, C_BLACK, 255);
  const char *t = "GAME OVER";
  gfx_text(FONT_MAIN, 160 - gfx_text_width(FONT_MAIN, t, 16) / 2, 100, t, C_WHITE, 255);
}
