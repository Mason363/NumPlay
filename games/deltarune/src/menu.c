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

void draw_darkbox(int x0, int y0, int x1, int y1);

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
/* the Dark World's (obj_darkcontroller): ITEM (USE, TOSS, KEY), EQUIP, POWER, CONFIG, D$ and the party; the
 * Light World's (obj_overworldc): ITEM, STAT, CELL */
static int cm_sel, cm_state, cm_sub, cm_who, cm_mode;
static const char *cm_msg;

void menu_open(void) {
  menu_from_save = false;
  cm_sel = 0, cm_state = 0, cm_sub = 0, cm_who = 0, cm_mode = 0, cm_msg = NULL;
  g_mode = MODE_MENU;
}

static int item_slots(void) {
  int n = 0;
  while (n < NITEMS && sv.item[n]) n++;
  return n;
}
static int party_n(void) { return party_count(); }
static int heal_amount(int it, int c) {
  switch (it) {
    case 1: return 40;
    case 5: return 20;
    case 6: return 160;
    case 7: return 80;
    case 8: return 70;
    case 9: return 50;
    case 10: return 4;
    case 11: return 30;
    case 12: return c == CH_KRIS ? 10 : c == CH_SUSIE ? 90 : 60;
    case 13: return c == CH_KRIS ? 80 : 30;
    case 14: return 500;
    case 15: return 60;
    default: return 0;
  }
}

static void cmenu_update(void) {
  bool dark = world_dark();
  if (cm_msg) {
    if (g_pressed & (K_Z | K_X)) cm_msg = NULL;
    return;
  }
  if (cm_state == 0) {
    int n = dark ? 4 : 3;
    if (g_pressed & (dark ? K_LEFT : K_UP)) cm_sel = (cm_sel + n - 1) % n;
    if (g_pressed & (dark ? K_RIGHT : K_DOWN)) cm_sel = (cm_sel + 1) % n;
    if (g_pressed & (K_X | K_C)) g_mode = MODE_WORLD;
    if (g_pressed & K_Z) {
      if (dark && cm_sel == 0) cm_state = 1, cm_mode = 0, cm_sub = 0;
      else if (dark) cm_state = 1, cm_who = 0, cm_sub = 0;
      else if (cm_sel == 0 && sv.litem[0]) cm_state = 1, cm_sub = 0;
      else if (cm_sel == 1) cm_state = 1;
    }
    return;
  }
  if (g_pressed & K_X) {
    cm_state--;
    return;
  }
  if (!dark) {
    if (cm_sel == 0) {
      int n = 0;
      while (n < 8 && sv.litem[n]) n++;
      if (g_pressed & K_UP) cm_sub = (cm_sub + n - 1) % (n ? n : 1);
      if (g_pressed & K_DOWN) cm_sub = (cm_sub + 1) % (n ? n : 1);
    }
    return;
  }
  if (cm_sel == 0) {
    /* ITEM: USE, TOSS, KEY, then an item, then (USE) whom */
    if (cm_state == 1) {
      if (g_pressed & K_LEFT) cm_mode = (cm_mode + 2) % 3;
      if (g_pressed & K_RIGHT) cm_mode = (cm_mode + 1) % 3;
      if ((g_pressed & K_Z) && (cm_mode == 2 || item_slots())) cm_state = 2, cm_sub = 0;
    } else if (cm_state == 2) {
      int n = cm_mode == 2 ? 1 : item_slots();
      if (n <= 0) n = 1;
      if (g_pressed & K_RIGHT && cm_sub % 2 == 0 && cm_sub + 1 < n) cm_sub++;
      if (g_pressed & K_LEFT && cm_sub % 2 == 1) cm_sub--;
      if (g_pressed & K_DOWN && cm_sub + 2 < n) cm_sub += 2;
      if (g_pressed & K_UP && cm_sub >= 2) cm_sub -= 2;
      if ((g_pressed & K_Z) && cm_mode == 1 && item_slots()) {
        memmove(sv.item + cm_sub, sv.item + cm_sub + 1, NITEMS - cm_sub - 1);
        sv.item[NITEMS - 1] = 0;
        if (cm_sub >= item_slots() && cm_sub) cm_sub--;
        if (!item_slots()) cm_state = 1;
      } else if ((g_pressed & K_Z) && cm_mode == 0 && item_slots()) {
        int it = sv.item[cm_sub];
        if (item_defs[it].target == 0 || !item_defs[it].usable) return;
        cm_state = 3, cm_who = 0;
      }
    } else {
      int n = party_n();
      if (g_pressed & K_LEFT) cm_who = (cm_who + n - 1) % n;
      if (g_pressed & K_RIGHT) cm_who = (cm_who + 1) % n;
      if (g_pressed & K_Z) {
        int it = sv.item[cm_sub];
        for (int k = 0; k < n; k++) {
          if (item_defs[it].target != 2 && k != cm_who) continue;
          int c = sv.chr[k];
          sv.hp[c] = (int16_t)min_i(sv.maxhp[c], sv.hp[c] + heal_amount(it, c));
        }
        memmove(sv.item + cm_sub, sv.item + cm_sub + 1, NITEMS - cm_sub - 1);
        sv.item[NITEMS - 1] = 0;
        cm_state = item_slots() ? 2 : 1;
        if (cm_sub >= item_slots() && cm_sub) cm_sub--;
      }
    }
  } else if (cm_sel == 1 || cm_sel == 2) {
    /* EQUIP and POWER: whose */
    int n = party_n();
    if (g_pressed & K_LEFT) cm_who = (cm_who + n - 1) % n;
    if (g_pressed & K_RIGHT) cm_who = (cm_who + 1) % n;
  } else {
    /* CONFIG: Auto-Run (flag 11) */
    if (g_pressed & K_Z) set_flag(11, !flag(11));
  }
}

static const char *ch_name(int c) {
  return c == CH_KRIS ? str(scr_gamestart_slash_scr_gamestart_gml_98_0)
         : c == CH_SUSIE ? str(scr_gamestart_slash_scr_gamestart_gml_99_0) : str(scr_gamestart_slash_scr_gamestart_gml_100_0);
}

/* the party's boxes at the bottom (scr_charbox, out of battle) */
static void party_boxes(void) {
  static const int16_t chunks[4][3] = {{0, 0, 0}, {106, 0, 0}, {53, 163, 0}, {0, 106, 212}};
  int n = party_n();
  gfx_rect(0, 202, SCREEN_W, 38, C_BLACK, 255);
  static const uint16_t heads[4] = {0, spr_headkris_h, spr_headsusie_h, spr_headralsei_h};
  for (int k = 0; k < n; k++) {
    int c = sv.chr[k], xc = chunks[n][k], by = 215;
    uint16_t cc = c == CH_KRIS ? rgb565(0x00FFFF) : c == CH_SUSIE ? rgb565(0xFF00FF) : rgb565(0x00FF00);
    if (cm_state && cm_who == k && (cm_sel || cm_state == 3)) gfx_frame_rect(xc + 1, 205, 104, 32, 1, cc);
    gfx_sprite(heads[c], 0, xc + 6, by + 1, 0, 255);
    gfx_text(FONT_SMALL, xc + 25, by + 3, c == CH_KRIS ? "KRIS" : c == CH_SUSIE ? "SUSIE" : "RALSEI", C_WHITE, 255);
    char s[16];
    snprintf(s, sizeof s, "%d/%d", sv.hp[c], sv.maxhp[c]);
    gfx_text(FONT_SMALL, xc + 102 - gfx_text_width(FONT_SMALL, s, 16), by - 1, s, C_WHITE, 255);
    gfx_rect(xc + 64, by + 6, 38, 4, rgb565(0x800000), 255);
    if (sv.hp[c] > 0) gfx_rect(xc + 64, by + 6, (sv.hp[c] * 38 + sv.maxhp[c] - 1) / sv.maxhp[c], 4, cc, 255);
  }
}

static void dark_menu_draw(void) {
  /* the top: the hovered menu's caption, the four buttons, the D$ */
  gfx_rect(0, 0, SCREEN_W, 40, C_BLACK, 255);
  gfx_sprite(spr_darkmenudesc, cm_sel, 10, 12, 0, 255);
  static const uint16_t bts[4] = {spr_darkitembt, spr_darkequipbt, spr_darktechbt, spr_darkconfigbt};
  for (int i = 0; i < 4; i++) gfx_sprite(bts[i], cm_sel == i ? 0 : 1, 60 + i * 50, 10, 0, 255);
  char buf[24];
  snprintf(buf, sizeof buf, "%s%ld", str(obj_darkcontroller_slash_Draw_0_gml_47_0), (long)sv.gold);
  gfx_text(FONT_MAIN, 260, 10, buf, C_WHITE, 255);
  party_boxes();
  if (!cm_state) return;
  gfx_rect(30, 45, 260, 155, C_BLACK, 255);
  draw_darkbox(26, 41, 293, 203);
  if (cm_sel == 0) {
    static const uint16_t modes[3] = {obj_darkcontroller_slash_Draw_0_gml_807_0, obj_darkcontroller_slash_Draw_0_gml_809_0,
                                      obj_darkcontroller_slash_Draw_0_gml_811_0};
    for (int i = 0; i < 3; i++)
      gfx_text(FONT_MAIN, 70 + i * 70, 52, str(modes[i]), cm_mode == i ? C_YELLOW : C_WHITE, 255);
    if (cm_state == 1) gfx_sprite(spr_heart_h, 0, 58 + cm_mode * 70, 56, 0, 255);
    if (cm_mode == 2) {
      gfx_text(FONT_MAIN, 55, 75, "Cell Phone", C_WHITE, 255);
    } else {
      for (int i = 0; i < item_slots(); i++) {
        int x = 55 + (i % 2) * 115, y = 75 + (i / 2) * 18;
        gfx_text(FONT_MAIN, x, y, str(item_defs[sv.item[i]].name), C_WHITE, 255);
        if (cm_state >= 2 && cm_sub == i) gfx_sprite(spr_heart_h, 0, x - 12, y + 4, 0, 255);
      }
      if (cm_state >= 2 && item_slots())
        gfx_text(FONT_SMALL, 40, 186, str(item_defs[sv.item[cm_sub]].desc), rgb565(0x808080), 255);
    }
  } else if (cm_sel == 1 || cm_sel == 2) {
    int c = sv.chr[cm_who % party_n()];
    gfx_text(FONT_MAIN, 45, 50, ch_name(c), C_WHITE, 255);
    char b[32];
    if (cm_sel == 1) {
      gfx_text(FONT_MAIN, 45, 80, str(weapon_defs[sv.weapon[c]].name), C_WHITE, 255);
      gfx_text(FONT_MAIN, 45, 98, sv.armor1[c] ? str(armor_defs[sv.armor1[c]].name) : str(obj_darkcontroller_slash_Draw_0_gml_423_0), C_WHITE, 255);
      gfx_text(FONT_MAIN, 45, 116, sv.armor2[c] ? str(armor_defs[sv.armor2[c]].name) : str(obj_darkcontroller_slash_Draw_0_gml_435_0), C_WHITE, 255);
    } else {
      int at = sv.at[c] + weapon_defs[sv.weapon[c]].at, df = sv.df[c] + armor_defs[sv.armor1[c]].df + armor_defs[sv.armor2[c]].df;
      int mg = sv.mag[c] + weapon_defs[sv.weapon[c]].mag;
      static const uint16_t descs[4] = {0, obj_darkcontroller_slash_Draw_0_gml_239_0, obj_darkcontroller_slash_Draw_0_gml_255_0,
                                        obj_darkcontroller_slash_Draw_0_gml_272_0};
      const char *d = str(descs[c]);
      char line[40];
      int y = 70, n = 0;
      for (const char *p = d;; p++) {
        if (*p == '#' || !*p) {
          line[n] = 0;
          gfx_text(FONT_MAIN, 130, y, line, C_WHITE, 255);
          y += 16, n = 0;
          if (!*p) break;
        } else if (*p == '^') {
          p++;
        } else if (n < 39) {
          line[n++] = *p;
        }
      }
      snprintf(b, sizeof b, "%s%d", str(obj_darkcontroller_slash_Draw_0_gml_207_0), at);
      gfx_text(FONT_MAIN, 45, 130, b, C_WHITE, 255);
      snprintf(b, sizeof b, "%s%d", str(obj_darkcontroller_slash_Draw_0_gml_208_0), df);
      gfx_text(FONT_MAIN, 45, 148, b, C_WHITE, 255);
      snprintf(b, sizeof b, "%s%d", str(obj_darkcontroller_slash_Draw_0_gml_209_0), mg);
      gfx_text(FONT_MAIN, 45, 166, b, C_WHITE, 255);
    }
  } else {
    gfx_text(FONT_MAIN, 135, 50, str(obj_darkcontroller_slash_Draw_0_gml_74_0), C_WHITE, 255);
    gfx_sprite(spr_heart_h, 0, 72, 84, 0, 255);
    gfx_text(FONT_MAIN, 85, 80, "Auto-Run", C_YELLOW, 255);
    gfx_text(FONT_MAIN, 215, 80, flag(11) ? str(obj_darkcontroller_slash_Draw_0_gml_79_1) : str(obj_darkcontroller_slash_Draw_0_gml_79_0),
             C_YELLOW, 255);
  }
}

static void light_menu_draw(void) {
  /* obj_overworldc: the name box and ITEM, STAT, CELL */
  int top = world_kris_low() ? 0 : 0;
  (void)top;
  gfx_rect(16, 26, 71, 55, C_WHITE, 255);
  gfx_rect(19, 29, 65, 49, C_BLACK, 255);
  gfx_text(FONT_MAIN, 23, 30, "Kris", C_WHITE, 255);
  char b[24];
  snprintf(b, sizeof b, "%s%d", str(obj_overworldc_slash_Draw_0_gml_37_0), 1);
  gfx_text(FONT_SMALL, 23, 49, b, C_WHITE, 255);
  snprintf(b, sizeof b, "%s%d/%d", str(obj_overworldc_slash_Draw_0_gml_36_0), sv.lhp, sv.lmaxhp);
  gfx_text(FONT_SMALL, 23, 58, b, C_WHITE, 255);
  snprintf(b, sizeof b, "%s%ld", str(obj_overworldc_slash_Draw_0_gml_39_0), (long)sv.lgold);
  gfx_text(FONT_SMALL, 23, 67, b, C_WHITE, 255);
  gfx_rect(16, 84, 71, 74, C_WHITE, 255);
  gfx_rect(19, 87, 65, 68, C_BLACK, 255);
  static const uint16_t opts[3] = {obj_overworldc_slash_Draw_0_gml_42_0, obj_overworldc_slash_Draw_0_gml_43_0,
                                   obj_overworldc_slash_Draw_0_gml_44_0};
  for (int i = 0; i < 3; i++) {
    uint16_t col = i == 0 && !sv.litem[0] ? rgb565(0x808080) : C_WHITE;
    gfx_text(FONT_MAIN, 42, 93 + i * 18, str(opts[i]), col, 255);
    if (cm_sel == i && !cm_state) gfx_sprite(spr_heart_h, 0, 28, 97 + i * 18, 0, 255);
  }
  if (cm_state && cm_sel == 1) {
    gfx_rect(94, 26, 173, 209, C_WHITE, 255);
    gfx_rect(97, 29, 167, 203, C_BLACK, 255);
    gfx_text(FONT_MAIN, 108, 40, "\"Kris\"", C_WHITE, 255);
    snprintf(b, sizeof b, "%s1", str(obj_overworldc_slash_Draw_0_gml_67_0));
    gfx_text(FONT_MAIN, 108, 70, b, C_WHITE, 255);
    snprintf(b, sizeof b, "HP  %d / %d", sv.lhp, sv.lmaxhp);
    gfx_text(FONT_MAIN, 108, 86, b, C_WHITE, 255);
    gfx_text(FONT_MAIN, 108, 118, "AT  10 (1)", C_WHITE, 255);
    gfx_text(FONT_MAIN, 108, 134, "DF  10 (0)", C_WHITE, 255);
    snprintf(b, sizeof b, "WEAPON: %s", str(obj_overworldc_slash_Draw_0_gml_74_0));
    gfx_text(FONT_MAIN, 108, 170, b, C_WHITE, 255);
    snprintf(b, sizeof b, "MONEY: $%ld", (long)sv.lgold);
    gfx_text(FONT_MAIN, 108, 202, b, C_WHITE, 255);
  }
}

static void cmenu_draw(void) {
  world_draw_scene();
  if (world_dark()) dark_menu_draw();
  else light_menu_draw();
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
