/* The menus: the intro, the title, naming the fallen human, the C menu (ITEM, STAT, CELL), SAVE, and GAME OVER
 * (menu.h). Their texts are the game's. */
#include <stdio.h>
#include <string.h>
#include "battle.h"
#include "event.h"
#include "events.h"
#include "game.h"
#include "gfx.h"
#include "items.h"
#include "menu.h"
#include "res.h"
#include "save.h"
#include "shop.h"
#include "text.h"
#include "world.h"

static int st, sel, sub, t;          /* the screen's step, what is selected, a second level, its frames */
static Writer w;
static char buf[400];
static char name[8];
static int name_len;
static int letter;                    /* the naming screen's cursor */
static int item_sel, item_act;
static bool msg_open;                 /* a text in the dialogue box over the menu */

enum { S_INTRO, S_TITLE, S_INSTR, S_NAME, S_CONFIRM, S_CONTINUE, S_GAMEOVER, S_FADEIN, S_SETTINGS };

/* ---------------------------------------------------------------- the intro */
static const char *const intro_lines[] = {
  "Long ago, two races\nruled over Earth:\nHUMANS and MONSTERS.",
  "One day, war broke\nout between the two\nraces.",
  "After a long battle,\nthe humans were\nvictorious.",
  "They sealed the monsters\nunderground with a magic\nspell.",
  "Many years later...",
  "      MT. EBOTT\n         201X",
  "Legends say that those\nwho climb the mountain\nnever return.",
  "", "", "", "",
};
#define INTRO_SLIDES 11
static int slide;

static void intro_slide(int k) {
  slide = k, t = 0;
  writer_start(&w, intro_lines[k], 60, 152, FONT_MAIN);
  w.line_h = 16, w.speed = 2;
}

void menu_title_open(void) {
  g_mode = MODE_INTRO;
  st = S_INTRO;
  intro_slide(0);
}

static void title_open(void) {
  g_mode = MODE_TITLE;
  st = S_TITLE, t = 0, sel = 0;
}

/* ---------------------------------------------------------------- settings (the title's, and the C menu's) */
static int set_sel, set_back;   /* EXIT or WALK; the screen to go back to */
static void settings_open(int back) { set_sel = 1, set_back = back; }
/* true when it closes */
static bool settings_input(uint32_t pressed) {
  if (pressed & (K_UP | K_DOWN)) set_sel ^= 1;
  if (set_sel == 1 && (pressed & (K_Z | K_LEFT | K_RIGHT))) {
    g_walk_fast ^= 1;
    config_save();
    return false;
  }
  return (pressed & K_X) || (set_sel == 0 && (pressed & K_Z));
}
static void settings_draw(int x, int y) {
  gfx_text(FONT_MENU, x, y, "SETTINGS", C_WHITE, 255);
  gfx_text(FONT_MENU, x + 14, y + 26, "EXIT", set_sel == 0 ? C_YELLOW : C_WHITE, 255);
  gfx_text(FONT_MENU, x + 14, y + 46, "WALK", set_sel == 1 ? C_YELLOW : C_WHITE, 255);
  gfx_text(FONT_MENU, x + 74, y + 46, g_walk_fast ? "FAST" : "NORMAL", set_sel == 1 ? C_YELLOW : C_WHITE, 255);
  gfx_text(FONT_SMALL, x + 14, y + 66, g_walk_fast ? "Twice as fast." : "As in the game.", rgb565(0x808080), 255);
  gfx_sprite(SPR_HEART, 0, x + 4, y + 32 + 20 * set_sel, 0, 255);
}

/* ---------------------------------------------------------------- naming */
static const char *const special_forbidden[][2] = {
  {"alphys", "D-don't do that."}, {"asgore", "You cannot."}, {"asriel", "..."}, {"flowey", "I already CHOSE that name."},
  {"sans", "nope."}, {"toriel", "I think you should\nthink of your own\nname, my child."}, {"undyne", "Get your OWN name!"},
};
static const char *const special_allowed[][2] = {
  {"frisk", "WARNING: This name will\nmake your life hell.\nProceed anyway?"}, {"murder", "That's a little on-the-\nnose, isn't it...?"},
  {"mercy", "That's a little on-the-\nnose, isn't it...?"}, {"aaaaaa", "Not very creative...?"},
  {"aaron", "Is this name correct? ;)"}, {"alphy", "Uh... OK?"}, {"bpants", "You are really scraping\nthe bottom of the barrel."},
  {"bratty", "Like, OK I guess."}, {"catty", "Bratty! Bratty!\nThat's MY name!"}, {"chara", "The true name."},
  {"gerson", "Wah ha ha! Why not?"}, {"jerry", "Jerry."}, {"metta", "OOOOH!!! ARE YOU\nPROMOTING MY BRAND?"},
  {"mett", "OOOOH!!! ARE YOU\nPROMOTING MY BRAND?"}, {"mtt", "OOOOH!!! ARE YOU\nPROMOTING MY BRAND?"},
  {"napsta", "............\n(They're powerless to\nstop you.)"}, {"blooky", "............\n(They're powerless to\nstop you.)"},
  {"papyru", "I'LL ALLOW IT!!!!"}, {"shyren", "...?"}, {"temmie", "hOI!"}, {"woshua", "Clean name."},
};
static const char *confirm_text;
static bool confirm_allowed;

static bool same_lower(const char *a, const char *b) {
  for (; *a && *b; a++, b++) {
    char x = *a >= 'A' && *a <= 'Z' ? (char)(*a + 32) : *a;
    if (x != *b) return false;
  }
  return !*a && !*b;
}

static void name_done(void) {
  name[name_len] = 0;
  if (!name_len) {
    confirm_text = "You must choose a name.";
    confirm_allowed = false;
  } else {
    confirm_text = "Is this name correct?", confirm_allowed = true;
    for (size_t i = 0; i < sizeof special_forbidden / sizeof special_forbidden[0]; i++)
      if (same_lower(name, special_forbidden[i][0])) confirm_text = special_forbidden[i][1], confirm_allowed = false;
    for (size_t i = 0; i < sizeof special_allowed / sizeof special_allowed[0]; i++)
      if (same_lower(name, special_allowed[i][0])) confirm_text = special_allowed[i][1];
    if (same_lower(name, "gaster")) {
      /* the game starts over */
      name_len = 0;
      menu_title_open();
      return;
    }
  }
  st = S_CONFIRM, sel = 0, t = 0;
}

/* the letters: A-Z, then a-z, 7 a row, then Quit / Backspace / Done */
#define LETTERS 52
static char letter_at(int i) { return (char)(i < 26 ? 'A' + i : 'a' + i - 26); }
static void letter_xy(int i, int *x, int *y) {
  int k = i < 26 ? i : i - 26, row = k / 7, col = k % 7;
  *x = 60 + col * 32, *y = (i < 26 ? 58 : 122) + row * 14;
}

static void name_input(uint32_t pressed) {
  /* letter: 0..51, 52 Quit, 53 Backspace, 54 Done */
  if (pressed & K_RIGHT) letter = letter >= 52 ? (letter == 54 ? 52 : letter + 1) : (letter + 1) % LETTERS;
  if (pressed & K_LEFT) letter = letter >= 52 ? (letter == 52 ? 54 : letter - 1) : (letter + LETTERS - 1) % LETTERS;
  if (pressed & K_DOWN) {
    if (letter >= 52) letter = letter == 52 ? 0 : letter == 53 ? 3 : 6;
    else {
      int k = letter < 26 ? letter : letter - 26;
      if (k + 7 < 26) letter += 7;
      else if (letter < 26) letter = 26 + k % 7;
      else letter = k % 7 < 2 ? 52 : k % 7 < 5 ? 53 : 54;
    }
  }
  if (pressed & K_UP) {
    if (letter >= 52) letter = 26 + 21 + (letter == 52 ? 0 : letter == 53 ? 2 : 4);
    else {
      int k = letter < 26 ? letter : letter - 26;
      if (k >= 7) letter -= 7;
      else if (letter >= 26) letter = (k + 21 < 26) ? k + 21 : k + 14;
      else letter = k < 2 ? 52 : k < 5 ? 53 : 54;
    }
  }
  if (pressed & K_X && name_len) name_len--;
  if (pressed & K_Z) {
    if (letter < LETTERS) {
      if (name_len < 6) name[name_len++] = letter_at(letter);
    } else if (letter == 52) title_open();
    else if (letter == 53) {
      if (name_len) name_len--;
    } else name_done();
  }
}

/* ---------------------------------------------------------------- the C menu */
enum { M_MAIN, M_ITEMS, M_ITEM_ACT, M_STAT, M_CELL, M_SETTINGS };
/* the CELL's list: Toriel's phone (0); Papyrus's (1) once he gave his number; the Dimensional Box (2) once Alphys
 * upgraded the phone */
static int cell_entries(int *calls) {
  int n = 0;
  calls[n++] = 0;
  if (flag(F_PAP_DATE) == 2) calls[n++] = 1;
  if (flag(F_HT_LAB) >= 2) calls[n++] = 2;
  return n;
}

void menu_open(void) {
  g_mode = MODE_MENU;
  st = M_MAIN, sel = 0;
  msg_open = false;
}

static void menu_close(void) { g_mode = MODE_WORLD; }

static void menu_say(const char *s) {
  snprintf(buf, sizeof buf, "%s", s);
  text_set_top(-1);
  text_open(FACE_NONE, buf);
  msg_open = true;
}

static void c_menu_input(uint32_t pressed) {
  if (msg_open) {
    text_update(pressed);
    if (!text_active()) {
      msg_open = false;
      if (st != M_MAIN) {
        menu_close();
        return;
      }
    }
    return;
  }
  switch (st) {
    case M_MAIN: {
      int n = flag(F_PHONE) ? 4 : 3;   /* (SETTINGS last) */
      if (pressed & K_UP) sel = (sel + n - 1) % n;
      if (pressed & K_DOWN) sel = (sel + 1) % n;
      if (pressed & (K_X | K_C)) menu_close();
      if (pressed & K_Z) {
        if (sel == 0 && item_count()) st = M_ITEMS, item_sel = 0;
        else if (sel == 1) st = M_STAT;
        else if (sel == n - 1) st = M_SETTINGS, settings_open(M_MAIN);
        else if (sel == 2) st = M_CELL, sub = 0;
      }
      break;
    }
    case M_SETTINGS:
      if (settings_input(pressed)) st = M_MAIN;
      break;
    case M_ITEMS: {
      int n = item_count();
      if (pressed & K_UP) item_sel = (item_sel + n - 1) % n;
      if (pressed & K_DOWN) item_sel = (item_sel + 1) % n;
      if (pressed & K_X) st = M_MAIN;
      if (pressed & K_Z) st = M_ITEM_ACT, item_act = 0;
      break;
    }
    case M_ITEM_ACT:
      if (pressed & K_LEFT) item_act = (item_act + 2) % 3;
      if (pressed & K_RIGHT) item_act = (item_act + 1) % 3;
      if (pressed & K_X) st = M_ITEMS;
      if (pressed & K_Z) {
        if (item_act != 1 && sv.items[item_sel] == ITEM_ANNOYING_DOG) {
          /* used or dropped, the dog is deployed (wf1.c) */
          menu_close();
          event_start(EV_WF_DOG, NULL);
          event_cur_arg(item_sel);
        } else if (item_act == 0) {
          item_use(item_sel, buf, sizeof buf, 0);
          menu_say(buf);
        } else if (item_act == 1) {
          char s[300];
          item_info_text(sv.items[item_sel], s, sizeof s);
          menu_say(s);
        } else {
          char s[120];
          snprintf(s, sizeof s, "* The %s was\n  thrown away.", item_info(sv.items[item_sel])->name);
          item_remove_at(item_sel);
          menu_say(s);
        }
      }
      break;
    case M_STAT:
      if (pressed & (K_X | K_Z)) st = M_MAIN;
      break;
    case M_CELL: {
      int calls[3], n = cell_entries(calls);
      if (pressed & K_UP) sub = (sub + n - 1) % n;
      if (pressed & K_DOWN) sub = (sub + 1) % n;
      if (pressed & K_X) st = M_MAIN;
      if (pressed & K_Z) {
        menu_close();
        if (calls[sub] == 2) {
          box_open();
        } else {
          event_start(EV_PHONE_MENU, NULL);
          event_cur_arg(calls[sub]);
        }
      }
      break;
    }
  }
}

static void panel(int x, int y, int w, int h) {
  gfx_rect(x, y, w, h, C_WHITE, 255);
  gfx_rect(x + 3, y + 3, w - 6, h - 6, C_BLACK, 255);
}

static void draw_c_menu(void) {
  if (msg_open) return; /* (an item's text: the menu closes for its box, as in the game) */
  bool low = pl.y + 15 - cam_y < 130;
  int oy = low ? 135 : 0;
  char s[64];
  /* the name and its numbers */
  oy = low ? 135 : 0;
  panel(16, 26 + oy, 71, 55);
  gfx_text(FONT_MENU, 23, 30 + oy, sv.name, C_WHITE, 255);
  snprintf(s, sizeof s, "LV  %d", sv.lv);
  gfx_text(FONT_SMALL, 23, 50 + oy, s, C_WHITE, 255);
  snprintf(s, sizeof s, "HP  %d/%d", sv.hp, sv.maxhp);
  gfx_text(FONT_SMALL, 23, 59 + oy, s, C_WHITE, 255);
  snprintf(s, sizeof s, "G   %d", sv.gold);
  gfx_text(FONT_SMALL, 23, 68 + oy, s, C_WHITE, 255);
  /* ITEM, STAT, CELL, and the settings */
  int n = flag(F_PHONE) ? 4 : 3;
  panel(16, 84, 71, 18 + 18 * n);
  static const char *const opts[] = {"ITEM", "STAT", "CELL"};
  for (int i = 0; i < n; i++) {
    uint16_t c = (i == 0 && !item_count()) ? rgb565(0x808080) : C_WHITE;
    gfx_text(FONT_MENU, 41, 92 + 18 * i, i == n - 1 ? "SETUP" : opts[i], c, 255);
  }
  if (st == M_MAIN) gfx_sprite(SPR_HEART, 0, 32, 98 + 18 * sel, 0, 255);
  if (st == M_ITEMS || st == M_ITEM_ACT) {
    panel(94, 26, 173, 181);
    for (int i = 0; i < item_count(); i++) {
      gfx_text(FONT_MENU, 116, 38 + 16 * i, item_info(sv.items[i])->name, C_WHITE, 255);
      if (st == M_ITEMS && i == item_sel) gfx_sprite(SPR_HEART, 0, 107, 44 + 16 * i, 0, 255);
    }
    static const char *const acts[] = {"USE", "INFO", "DROP"};
    for (int i = 0; i < 3; i++) {
      gfx_text(FONT_MENU, 116 + i * 48, 180, acts[i], C_WHITE, 255);
      if (st == M_ITEM_ACT && i == item_act) gfx_sprite(SPR_HEART, 0, 107 + i * 48, 186, 0, 255);
    }
  }
  if (st == M_STAT) {
    panel(94, 26, 173, 209);
    snprintf(s, sizeof s, "\"%s\"", sv.name);
    gfx_text(FONT_MENU, 108, 38, s, C_WHITE, 255);
    snprintf(s, sizeof s, "LV %d", sv.lv);
    gfx_text(FONT_MENU, 108, 68, s, C_WHITE, 255);
    snprintf(s, sizeof s, "HP %d / %d", sv.hp, sv.maxhp);
    gfx_text(FONT_MENU, 108, 84, s, C_WHITE, 255);
    snprintf(s, sizeof s, "AT %d(%d)", sv.at - 10, weapon_at());
    gfx_text(FONT_MENU, 108, 114, s, C_WHITE, 255);
    snprintf(s, sizeof s, "DF %d(%d)", sv.df - 10, armor_df());
    gfx_text(FONT_MENU, 108, 130, s, C_WHITE, 255);
    snprintf(s, sizeof s, "EXP: %d", (int)sv.exp);
    gfx_text(FONT_MENU, 190, 114, s, C_WHITE, 255);
    snprintf(s, sizeof s, "NEXT: %d", sv.lv < 20 ? lv_exp_for(sv.lv + 1) - (int)sv.exp : 0);
    gfx_text(FONT_MENU, 190, 130, s, C_WHITE, 255);
    snprintf(s, sizeof s, "WEAPON: %s", item_info(sv.weapon)->name);
    gfx_text(FONT_MENU, 108, 160, s, C_WHITE, 255);
    snprintf(s, sizeof s, "ARMOR: %s", item_info(sv.armor)->name);
    gfx_text(FONT_MENU, 108, 176, s, C_WHITE, 255);
    snprintf(s, sizeof s, "GOLD: %d", sv.gold);
    gfx_text(FONT_MENU, 108, 200, s, C_WHITE, 255);
    if (sv.kills > 20) {
      snprintf(s, sizeof s, "KILLS: %d", sv.kills);
      gfx_text(FONT_MENU, 190, 200, s, C_WHITE, 255);
    }
  }
  if (st == M_SETTINGS) {
    panel(94, 26, 173, 104);
    settings_draw(108, 38);
  }
  if (st == M_CELL) {
    static const char *const names[3] = {"Toriel's Phone", "Papyrus's Phone", "Dimensional Box"};
    int calls[3], n = cell_entries(calls);
    panel(94, 26, 173, 24 + 18 * n);
    for (int i = 0; i < n; i++) gfx_text(FONT_MENU, 116, 38 + 18 * i, names[calls[i]], C_WHITE, 255);
    gfx_sprite(SPR_HEART, 0, 107, 44 + sub * 18, 0, 255);
  }
}

/* ---------------------------------------------------------------- SAVE */
static int save_state;    /* 0 the choice, 1 saved */

void menu_save_open(void) {
  g_mode = MODE_SAVE;
  sel = 0, save_state = 0;
}

static void draw_save(void) {
  panel(54, 58, 212, 82);
  char s[48];
  uint16_t c = save_state ? C_YELLOW : C_WHITE;
  const Save *shown = &sv;
  gfx_text(FONT_MENU, 68, 68, shown->name, c, 255);
  snprintf(s, sizeof s, "LV %d", shown->lv);
  gfx_text(FONT_MENU, 136, 68, s, c, 255);
  unsigned secs = shown->time / 30;
  snprintf(s, sizeof s, "%u:%02u", secs / 60, secs % 60);
  /* (100 minutes and more: the time moves left, to stay in the box) */
  gfx_text(FONT_MENU, min_i(222, 54 + 212 - 7 - gfx_text_width(FONT_MENU, s, (int)strlen(s))), 68, s, c, 255);
  RoomInfo ri;
  room_info(g_room, &ri);
  gfx_text(FONT_MENU, 68, 88, str(ri.name), c, 255);
  if (save_state) gfx_text(FONT_MENU, 84, 116, "File saved.", c, 255);
  else {
    gfx_text(FONT_MENU, 84, 116, "Save", C_WHITE, 255);
    gfx_text(FONT_MENU, 180, 116, "Return", C_WHITE, 255);
    gfx_sprite(SPR_HEART, 0, sel ? 170 : 74, 122, 0, 255);
  }
}

static void save_input(uint32_t pressed) {
  if (save_state) {
    if (pressed & (K_Z | K_X)) g_mode = MODE_WORLD;
    return;
  }
  if (pressed & (K_LEFT | K_RIGHT)) sel ^= 1;
  if (pressed & K_X) g_mode = MODE_WORLD;
  if (pressed & K_Z) {
    if (sel) g_mode = MODE_WORLD;
    else {
      sv.room = (int16_t)g_room, sv.x = pl.x, sv.y = pl.y;
      save_write();
      save_state = 1;
    }
  }
}

/* ---------------------------------------------------------------- GAME OVER */
void menu_gameover_open(void) {
  g_mode = MODE_GAMEOVER;
  st = S_GAMEOVER, t = 0;
  snprintf(buf, sizeof buf, "You cannot give\nup just yet...|%s!\nStay determined...", sv.name);
  writer_start(&w, buf, 90, 160, FONT_MAIN);
  w.speed = 2;
}

/* ---------------------------------------------------------------- the end of what there is */
void menu_end_open(const char *text) {
  g_mode = MODE_END;
  t = 0;
  writer_start(&w, text, 40, 90, FONT_MAIN);
  w.speed = 2;
}

/* ---------------------------------------------------------------- the frame */
void menu_update(uint32_t keys, uint32_t pressed) {
  (void)keys;
  t++;
  switch (g_mode) {
    case MODE_INTRO:
      if (pressed & K_Z) {
        title_open();
        return;
      }
      writer_update(&w, 0);
      if (t > (slide == 10 ? 300 : slide >= 7 ? 90 : 180)) {
        if (slide + 1 < INTRO_SLIDES) intro_slide(slide + 1);
        else title_open();
      }
      break;
    case MODE_TITLE:
      if (st == S_TITLE) {
        if (pressed & K_Z && t > 15) {
          if (save_exists()) st = S_CONTINUE, sel = 0;
          else st = S_INSTR, sel = 0;
          t = 0;
        }
      } else if (st == S_INSTR) {
        if (pressed & (K_UP | K_DOWN)) sel ^= 1;
        if (pressed & K_Z && t > 5) {
          if (sel == 0) st = S_NAME, letter = 0, name_len = 0, t = 0;
          else st = S_SETTINGS, settings_open(S_INSTR);
        }
      } else if (st == S_SETTINGS) {
        if (settings_input(pressed)) st = set_back, t = 0;
      } else if (st == S_NAME) {
        name_input(pressed);
      } else if (st == S_CONFIRM) {
        if (!confirm_allowed) {
          if (pressed & (K_Z | K_X)) st = S_NAME;
          break;
        }
        if (pressed & (K_LEFT | K_RIGHT)) sel ^= 1;
        if (pressed & K_X) st = S_NAME;
        if (pressed & K_Z) {
          if (sel == 0) st = S_NAME;
          else {
            game_new(name);
            if (same_lower(name, "frisk")) set_flag(F_HARDMODE, 1);
            st = S_FADEIN;
          }
        }
      } else if (st == S_CONTINUE) {
        /* Continue and Reset, Settings under them */
        if (pressed & (K_LEFT | K_RIGHT) && sel < 2) sel ^= 1;
        if (pressed & K_DOWN) sel = 2;
        if (pressed & K_UP) sel = 0;
        if (pressed & K_Z) {
          if (sel == 0) save_load();
          else if (sel == 2) st = S_SETTINGS, settings_open(S_CONTINUE);
          else st = S_NAME, letter = 0, name_len = 0;
        }
      }
      break;
    case MODE_MENU:
      c_menu_input(pressed);
      break;
    case MODE_SAVE:
      save_input(pressed);
      break;
    case MODE_GAMEOVER:
      if (t > 40 && !writer_update(&w, pressed)) {
        if (!save_load()) game_new(sv.name);
      }
      break;
    case MODE_END:
      if (t > 30 && !writer_update(&w, pressed)) menu_title_open();
      break;
  }
}

void menu_draw(void) {
  char s[64];
  switch (g_mode) {
    case MODE_INTRO:
      if (slide < 10) gfx_sprite(SPR_INTROIMAGE, slide, 0, 0, 0, (uint8_t)min_i(255, t * 16));
      else gfx_sprite(SPR_INTROLAST, 0, 60, 28 - min_i(t / 2, 110), 0, 255);
      writer_draw(&w, C_WHITE);
      break;
    case MODE_TITLE:
      if (st == S_TITLE) {
        gfx_sprite(SPR_UNDERTALETITLE, 0, 0, 0, 0, 255);
        if (t > 30) gfx_text(FONT_SMALL, 128, 184, "[PRESS OK]", rgb565(0x808080), 255);
      } else if (st == S_INSTR) {
        static const char *const lines[] = {" --- Instruction --- ", "[OK] - Confirm", "[Back] - Cancel",
                                            "[toolbox] - Menu (In-game)", "[Home] - Quit", "When HP is 0, you lose."};
        for (int i = 0; i < 6; i++) gfx_text(FONT_MAIN, 50, 30 + i * 18 + (i > 0) * 4, lines[i], rgb565(0xC0C0C0), 255);
        gfx_text(FONT_MAIN, 74, 160, "Begin Game", sel == 0 ? C_YELLOW : C_WHITE, 255);
        gfx_text(FONT_MAIN, 74, 180, "Settings", sel == 1 ? C_YELLOW : C_WHITE, 255);
      } else if (st == S_SETTINGS) {
        settings_draw(110, 60);
      } else if (st == S_NAME) {
        gfx_text(FONT_MAIN, 76, 18, "Name the fallen human.", C_WHITE, 255);
        memcpy(s, name, (size_t)name_len), s[name_len] = 0;
        gfx_text(FONT_MAIN, 128, 40, s, C_WHITE, 255);
        for (int i = 0; i < LETTERS; i++) {
          int x, y;
          letter_xy(i, &x, &y);
          char c[2] = {letter_at(i), 0};
          /* (the game's letters tremble; at half its size, one at a time) */
          bool shakes = (g_frame / 3 + (uint32_t)i * 7) % 23 == 0;
          gfx_text_ex(FONT_MAIN, x, y, c, 1, i == letter ? C_YELLOW : C_WHITE, shakes, (uint32_t)(i * 77 + g_frame / 3), 0);
        }
        static const char *const opts[] = {"Quit", "Backspace", "Done"};
        static const int16_t ox[] = {60, 122, 220};
        for (int i = 0; i < 3; i++) gfx_text(FONT_MAIN, ox[i], 200, opts[i], letter == 52 + i ? C_YELLOW : C_WHITE, 255);
      } else if (st == S_CONFIRM) {
        gfx_text(FONT_MAIN, 76, 18, confirm_text, C_WHITE, 255);
        memcpy(s, name, (size_t)name_len), s[name_len] = 0;
        /* the name grows */
        int k = min_i(t, 60);
        gfx_text_ex(FONT_MAIN, 128 - k / 2, 60 + k / 2, s, name_len, C_WHITE, 1, g_frame / 2, k / 10);
        if (confirm_allowed) {
          gfx_text(FONT_MAIN, 60, 200, "No", sel == 0 ? C_YELLOW : C_WHITE, 255);
          gfx_text(FONT_MAIN, 228, 200, "Yes", sel == 1 ? C_YELLOW : C_WHITE, 255);
        } else
          gfx_text(FONT_MAIN, 60, 200, "Go back", C_YELLOW, 255);
      } else if (st == S_CONTINUE) {
        Save saved;
        if (save_peek(&saved)) {
          gfx_text(FONT_MENU, 70, 60, saved.name, C_WHITE, 255);
          snprintf(s, sizeof s, "LV %d", saved.lv);
          gfx_text(FONT_MENU, 140, 60, s, C_WHITE, 255);
          unsigned secs = saved.time / 30;
          snprintf(s, sizeof s, "%u:%02u", secs / 60, secs % 60);
          gfx_text(FONT_MENU, 222, 60, s, C_WHITE, 255);
          RoomInfo ri;
          room_info(saved.room, &ri);
          gfx_text(FONT_MENU, 70, 80, str(ri.name), C_WHITE, 255);
        }
        gfx_text(FONT_MENU, 84, 110, "Continue", sel == 0 ? C_YELLOW : C_WHITE, 255);
        gfx_text(FONT_MENU, 190, 110, "Reset", sel == 1 ? C_YELLOW : C_WHITE, 255);
        gfx_text(FONT_MENU, 128, 130, "Settings", sel == 2 ? C_YELLOW : C_WHITE, 255);
      }
      break;
    case MODE_MENU:
      draw_c_menu();
      break;
    case MODE_SAVE:
      draw_save();
      break;
    case MODE_GAMEOVER:
      gfx_sprite(SPR_GAMEOVERBG, 0, 0, 0, 0, (uint8_t)min_i(255, t * 6));
      if (t > 40) writer_draw(&w, C_WHITE);
      break;
    case MODE_END:
      if (t > 30) writer_draw(&w, C_WHITE);
      break;
  }
}
