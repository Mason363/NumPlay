/* FNAF World: the scenes around the map, the keys and the saves.
 *
 * As the game: the title, the files (three, each Adventure or Fixed Party, Normal or Hard: the frame "file setup"),
 * the party (eight of the characters you have: "character select"), the map and its battles, the chips and bytes
 * worn ("chips", "Bytes"), Lolbit's byte store ("shop") and Mendo's armor ("armor shop"), game over. */
#include <string.h>
#include "anims.h"
#include "data.h"
#include "game.h"
#include "gfx.h"
#include "plat.h"

Game g;
void world_after_battle(void);

static uint32_t seed = 0x2545F491u;
uint32_t rnd(void) {
  seed ^= seed << 13, seed ^= seed >> 17, seed ^= seed << 5;
  return seed;
}
int rnd_n(int n) { return n > 0 ? (int)(rnd() % (uint32_t)n) : 0; }

#define OKS (K_OK | K_EXE)
#define UPS (K_UP | K_DIGIT(8))
#define DOWNS (K_DOWN | K_DIGIT(2))
#define LEFTS (K_LEFT | K_DIGIT(4))
#define RIGHTS (K_RIGHT | K_DIGIT(6))
static bool pressed(uint32_t k) { return (g.pressed & k) != 0; }
static const uint16_t HILITE = 0xFE88, KEYS = 0xC67C;

static void go(int scene) { g.scene = (uint8_t)scene, g.t = 0, g.menu = 0, g.sub = 0; }

void game_init(void) {
  memset(&g, 0, sizeof g);
  g.running = true;
  seed ^= plat_millis() * 2654435761u + 1;
  save_restore_copy();
  go(SC_TITLE);
}

static void num(char *out, int32_t v) {
  char t[12];
  int p = 11;
  bool neg = v < 0;
  if (neg) v = -v;
  t[p] = 0;
  do t[--p] = (char)('0' + v % 10), v /= 10; while (v && p > 1);
  if (neg) t[--p] = '-';
  strcpy(out, t + p);
}

/* ------------------------------------------------------------------ the title and the files */
static void start_play(void) {
  if (!g.f.locked && g.f.mode != 2) {   /* the party first (the character select after a new game) */
    g.pick = 0, g.pick_slot = 0, g.pick_mode = 0;
    go(SC_PARTY);
    g.back = SC_WORLD;
    return;
  }
  world_enter();
  go(SC_WORLD);
}

static void files_step(void) {
  /* menu stages (2-31): 0 the files, 1 new/continue, 2 erase?, 4 the mode, 5 the difficulty */
  int n = g.sub == 0 ? 3 : 2;
  if (pressed(UPS)) g.menu = (uint8_t)((g.menu + n - 1) % n);
  if (pressed(DOWNS)) g.menu = (uint8_t)((g.menu + 1) % n);
  if (pressed(K_BACK)) {
    if (g.sub == 0) go(SC_TITLE);
    else g.sub = 0, g.menu = (uint8_t)(g.slot - 1);
    return;
  }
  if (!pressed(OKS)) return;
  switch (g.sub) {
    case 0:
      g.slot = (uint8_t)(g.menu + 1);
      g.sub = 1, g.menu = 0;
      if (!load_file(g.slot, &g.f)) memset(&g.f, 0, sizeof g.f);
      break;
    case 1:
      if (g.menu == 0) g.sub = 2, g.menu = 1;
      else if (g.f.started) start_play();
      break;
    case 2:
      if (g.menu == 0) g.sub = 4, g.menu = 0;   /* 17: YES */
      else g.sub = 0, g.menu = (uint8_t)(g.slot - 1);
      break;
    case 4:   /* 23-25 */
      memset(&g.f, 0, sizeof g.f);
      g.f.mode = (uint8_t)(g.menu + 1);
      for (int c = 1; c <= (g.f.mode == 2 ? 16 : 8); c++) g.f.have[c] = 1;
      for (int k = 0; k < 8; k++) g.f.slot[k] = (uint8_t)(k + 1);   /* 42 */
      if (g.f.mode == 2) {
        for (int k = 0; k < 8; k++) g.f.slot[k] = (uint8_t)(k + 9);
        g.f.locked = 1;
      }
      g.sub = 5, g.menu = 0;
      break;
    case 5:   /* 28-29 */
      g.f.diff = (uint8_t)(g.menu + 1);
      g.f.started = 1;
      save_new(g.slot);
      start_play();
      break;
  }
}

static void button_pic(int pic, int cx, int y, bool sel) {
  pic_draw(pic, cx, y);
  if (sel) {
    int w = pic_w(pic), h = pic_h(pic), x0 = cx - pic_hx(pic), y0 = y - pic_hy(pic);
    gfx_frame_rect(x0 - 2, y0 - 2, w + 4, h + 2, HILITE);
    gfx_frame_rect(x0 - 3, y0 - 3, w + 6, h + 4, HILITE);
  }
}

static struct { uint8_t mode, diff, h, m; bool st; } summary[3];
static void files_band(void) {
  pic_part(PIC_MENU, 0, 0, 0, 0, 320, 240);
  static const char *const MODE[3] = {"Empty", "Adventure", "Fixed Party"};
  if (g.sub == 0) {
    gfx_text_c(FONT_BIG, 160, 14, "Pick a file", WHITE);
    for (int i = 0; i < 3; i++) {
      uint8_t mode = summary[i].mode, diff = summary[i].diff, h = summary[i].h, m = summary[i].m;
      bool st = summary[i].st;
      int y = 50 + 52 * i;
      gfx_fill(40, y, 240, 42, g.menu == i ? RGB(0x5A3A9A) : RGB(0x2A1A5A));
      gfx_frame_rect(40, y, 240, 42, g.menu == i ? HILITE : WHITE);
      char s[48], t[8];
      strcpy(s, "File ");
      s[5] = (char)('1' + i), s[6] = 0;
      gfx_text(FONT_BIG, 52, y + 4, s, WHITE);
      if (st) {
        strcpy(s, MODE[mode < 3 ? mode : 0]);
        strcat(s, diff == 2 ? ", Hard " : ", Normal ");
        num(t, h);
        strcat(s, t);
        strcat(s, m < 10 ? ":0" : ":");
        num(t, m);
        strcat(s, t);
      } else
        strcpy(s, "Empty");
      gfx_text(FONT_SMALL, 52, y + 26, s, RGB(0xD0C8F0));
    }
  } else if (g.sub == 1) {
    gfx_text_c(FONT_BIG, 160, 30, "File", WHITE);
    button_pic(PIC_B_NEWGAME, 160, 100, g.menu == 0);
    button_pic(PIC_B_CONTINUE, 160, 150, g.menu == 1);
    if (!g.f.started) gfx_blend(160 - 70, 150 - 18, 140, 36, BLACK, 18);
  } else if (g.sub == 2) {
    gfx_text_c(FONT_BIG, 160, 30, "Erase this file?", WHITE);
    button_pic(PIC_B_YES, 160, 100, g.menu == 0);
    button_pic(PIC_B_NO, 160, 150, g.menu == 1);
  } else if (g.sub == 4) {
    gfx_text_c(FONT_BIG, 160, 22, "Game mode", WHITE);
    button_pic(PIC_B_ADVENTURE, 160, 80, g.menu == 0);
    gfx_text_c(FONT_SMALL, 160, 100, "Find new characters as you play.", WHITE);
    button_pic(PIC_B_FIXED, 160, 148, g.menu == 1);
    gfx_text_c(FONT_SMALL, 160, 168, "Start with two fixed teams.", WHITE);
    gfx_text_c(FONT_SMALL, 160, 181, "(more difficult)", WHITE);
  } else {
    gfx_text_c(FONT_BIG, 160, 22, "Difficulty", WHITE);
    button_pic(PIC_B_NORMAL, 160, 90, g.menu == 0);
    button_pic(PIC_B_HARD, 160, 150, g.menu == 1);
  }
  gfx_blend(0, 226, 320, 14, BLACK, 18);
  gfx_text(FONT_SMALL, 4, 227, "Arrows pick  OK choose  Back go back", KEYS);
}

static void title_band(void) {
  pic_part(PIC_TITLE, 0, 0, 0, 0, 320, 240);
  if ((g.t / 30) & 1) gfx_frame_rect(160 - 49, 240 - 33, 98, 27, HILITE);
  gfx_blend(0, 0, 320, 13, BLACK, 14);
  gfx_text(FONT_SMALL, 3, 0, "OK start   Back quit", KEYS);
}

/* ------------------------------------------------------------------ the party (character select) */
static void party_step(void) {
  if (g.pick_mode == 0) {   /* the slots: which of the eight to change */
    if (pressed(LEFTS)) g.pick_slot = (uint8_t)((g.pick_slot + 7) % 8);
    if (pressed(RIGHTS)) g.pick_slot = (uint8_t)((g.pick_slot + 1) % 8);
    if (pressed(UPS) || pressed(DOWNS)) g.pick_slot ^= 4;
    if (pressed(OKS) && !g.f.locked) {
      g.pick_mode = 1;
      g.pick = (uint8_t)(g.f.slot[g.pick_slot] ? g.f.slot[g.pick_slot] - 1 : 0);
    }
    if (pressed(K_DIGIT(0)) || pressed(K_BACK)) {   /* Done (34): all eight there */
      bool full = true;
      for (int k = 0; k < 8; k++) full &= g.f.slot[k] != 0;
      if (!full) return;
      g.f.started = 1;
      if (g.f.mode != 2) g.f.locked = 0;
      g.dirty = 1;
      save_file();
      if (g.back == SC_WORLD && g.w.x == 0) world_enter();
      go(SC_WORLD);
    }
    return;
  }
  /* the characters: 8 x 6 */
  if (pressed(LEFTS)) g.pick = (uint8_t)((g.pick + 47) % 48);
  if (pressed(RIGHTS)) g.pick = (uint8_t)((g.pick + 1) % 48);
  if (pressed(UPS)) g.pick = (uint8_t)((g.pick + 40) % 48);
  if (pressed(DOWNS)) g.pick = (uint8_t)((g.pick + 8) % 48);
  if (pressed(K_BACK)) g.pick_mode = 0;
  if (pressed(OKS)) {   /* 23-30: one you have, not in the party yet */
    int c = g.pick + 1;
    bool in = false;
    for (int k = 0; k < 8; k++) in |= g.f.slot[k] == c && k != g.pick_slot;
    if (g.f.have[c] && !in) {
      g.f.slot[g.pick_slot] = (uint8_t)c;
      g.pick_mode = 0;
      g.pick_slot = (uint8_t)((g.pick_slot + 1) % 8);
    }
  }
}

static void party_band(void) {
  pic_part(PIC_MENU, 0, 0, 0, 0, 320, 240);
  gfx_blend(0, 0, 320, 240, BLACK, 10);
  /* the faces */
  for (int i = 0; i < 48; i++) {
    int x = 4 + 29 * (i % 8), y = 4 + 29 * (i / 8);
    if (y > gfx_y0 + BAND_H || y + 28 < gfx_y0) continue;
    pic_part(PIC_FACE1 + i, x, y, 0, 0, 28, 28);
    if (!g.f.have[i + 1]) gfx_blend(x, y, 28, 28, BLACK, 26);
    for (int k = 0; k < 8; k++)
      if (g.f.slot[k] == i + 1) {
        gfx_fill(x + 18, y + 18, 10, 10, k < 4 ? RGB(0x2050E0) : RGB(0xC03030));
        char s[2] = {(char)('1' + k), 0};
        gfx_text(FONT_SMALL, x + 20, y + 16, s, WHITE);
      }
    if (g.pick_mode == 1 && g.pick == i) {
      gfx_frame_rect(x - 1, y - 1, 30, 30, HILITE);
      gfx_frame_rect(x - 2, y - 2, 32, 32, HILITE);
    }
  }
  /* the party's two rows */
  for (int k = 0; k < 8; k++) {
    int x = 4 + 29 * (k & 3) + (k >= 4 ? 120 : 0), y = 182;
    gfx_fill(x - 1, y - 1, 30, 30, k < 4 ? RGB(0x2050E0) : RGB(0xC03030));
    if (g.f.slot[k]) pic_part(PIC_FACE1 + g.f.slot[k] - 1, x, y, 0, 0, 28, 28);
    if (g.pick_slot == k) {
      gfx_frame_rect(x - 2, y - 2, 32, 32, g.pick_mode == 0 ? HILITE : WHITE);
      gfx_frame_rect(x - 3, y - 3, 34, 34, g.pick_mode == 0 ? HILITE : WHITE);
    }
  }
  /* the one picked: its name, level, attacks */
  int c = g.pick_mode == 1 ? g.pick + 1 : g.f.slot[g.pick_slot];
  gfx_fill(240, 4, 76, 206, RGB(0x141A40));
  gfx_frame_rect(240, 4, 76, 206, RGB(0x6070C0));
  if (c && g.f.have[c]) {
    int an = AN_IDLE1 + 2 * (c - 1);
    int pic = ANIM_PICS[ANIMS[an].first + (g.t / 2) % ANIMS[an].n];
    pic_draw(pic, 278, 110);
    char s[32], t[12];
    const char *name = CHAR_NAME[c];
    int w = gfx_text_w(FONT_SMALL, name);
    if (w > 72) gfx_text(FONT_SMALL, 242, 112, name, WHITE);
    else gfx_text_c(FONT_SMALL, 278, 112, name, WHITE);
    strcpy(s, "Level ");
    num(t, g.f.lv[c] + 1);
    strcat(s, t);
    gfx_text(FONT_SMALL, 244, 126, s, WHITE);
    strcpy(s, "Next ");
    num(t, exp_needed(c, g.f.lv[c]) - (int32_t)g.f.next[c]);
    strcat(s, t);
    gfx_text(FONT_SMALL, 244, 139, s, WHITE);
    for (int i = 0; i < 3; i++) {
      int a = CHAR_ATTACKS[c][i];
      gfx_fill(243, 155 + 17 * i, 70, 15, RGB(0x34488C));
      gfx_text_c(FONT_SMALL, 278, 156 + 17 * i, ATTACK_NAME[a], WHITE);
    }
  } else if (c) {
    gfx_text_c(FONT_TEXT, 278, 100, "???", WHITE);
  }
  gfx_blend(0, 226, 320, 14, BLACK, 18);
  if (g.f.locked) gfx_text(FONT_SMALL, 4, 227, "Fixed party.  0 or Back done", KEYS);
  else if (g.pick_mode == 0) gfx_text(FONT_SMALL, 4, 227, "Arrows slot  OK change  0 done", KEYS);
  else gfx_text(FONT_SMALL, 4, 227, "Arrows pick  OK put in  Back cancel", KEYS);
}

/* ------------------------------------------------------------------ the pause menu */
enum { P_RESUME, P_PARTY, P_CHIPS, P_BYTES, P_JUMP, P_SAVE, P_HOWTO, P_QUIT, P_COUNT };
static const char *const PAUSE_ITEM[P_COUNT] = {"Resume", "Party", "Chips", "Bytes", "Jump", "Save", "How to play",
                                                "Quit"};
static bool warp_open(int k) {   /* 1798-1803 */
  switch (k) {
    case 0: return true;
    case 1: return g.f.sw[1];
    case 2: return g.f.w3;
    case 3: return g.f.sw[2];
    case 4: return g.f.sw[3];
    default: return g.f.sw[4];
  }
}
static const int16_t WARP[6][2] = {{1829, 1024}, {1673, 1628}, {918, 1726}, {2787, 833}, {3275, 1922}, {2544, 3685}};
static const char *const WARP_NAME[6] = {"Fazbear Hills", "Choppy's Woods", "Dusting Fields", "Lilygear Lake",
                                         "Blacktomb Yard", "Pinwheel Circus"};

static void pause_step(void) {
  if (g.confirm) {
    if (pressed(OKS)) {
      save_file();
      g.running = false;
    }
    if (pressed(K_BACK)) g.confirm = 0;
    return;
  }
  if (pressed(UPS)) g.menu = (uint8_t)((g.menu + P_COUNT - 1) % P_COUNT);
  if (pressed(DOWNS)) g.menu = (uint8_t)((g.menu + 1) % P_COUNT);
  if (pressed(K_BACK)) {
    go(g.back);
    return;
  }
  if (!pressed(OKS)) return;
  bool battle = g.back == SC_BATTLE;
  switch (g.menu) {
    case P_RESUME: go(g.back); break;
    case P_PARTY:
      if (battle || g.f.mode == 2) break;   /* 1194: not in Fixed Party */
      g.pick_mode = 0, g.pick_slot = 0;
      go(SC_PARTY);
      g.back = SC_WORLD;
      break;
    case P_CHIPS: if (!battle) go(SC_CHIPS); break;
    case P_BYTES: if (!battle) go(SC_BYTES); break;
    case P_JUMP:
      if (!battle && g.w.area != 23 && g.w.area != 50) go(SC_WARP);
      break;
    case P_SAVE:
      if (!battle) {
        world_save_pos();
        save_file();
        go(SC_WORLD);
      }
      break;
    case P_HOWTO: go(SC_HOWTO); break;
    case P_QUIT: g.confirm = 1; break;
  }
}

static void menu_list(const char *const *items, int n, int sel, int x, int y, int w, const uint8_t *dim) {
  for (int i = 0; i < n; i++) {
    int yy = y + 22 * i;
    gfx_fill(x, yy, w, 19, i == sel ? RGB(0x3A58B8) : RGB(0x1A2858));
    if (i == sel) gfx_frame_rect(x - 1, yy - 1, w + 2, 21, HILITE);
    gfx_text(FONT_TEXT, x + 8, yy + 2, items[i], dim && dim[i] ? RGB(0x707890) : WHITE);
  }
}

static void pause_band(void) {
  if (g.back == SC_BATTLE) battle_draw_band_dim();
  else world_band_dim();
  gfx_blend(0, 0, 320, 240, BLACK, 18);
  gfx_text_c(FONT_BIG, 160, 8, "Paused", WHITE);
  uint8_t dim[P_COUNT] = {0};
  bool battle = g.back == SC_BATTLE;
  dim[P_PARTY] = battle || g.f.mode == 2;
  dim[P_CHIPS] = dim[P_BYTES] = dim[P_SAVE] = battle;
  dim[P_JUMP] = battle || g.w.area == 23 || g.w.area == 50;
  menu_list(PAUSE_ITEM, P_COUNT, g.menu, 90, 36, 140, dim);
  if (g.confirm) {
    gfx_fill(40, 90, 240, 56, RGB(0x0A1430));
    gfx_frame_rect(40, 90, 240, 56, WHITE);
    gfx_text_c(FONT_TEXT, 160, 98, "Save and quit?", WHITE);
    gfx_text_c(FONT_SMALL, 160, 122, "OK yes   Back no", KEYS);
  }
  gfx_blend(0, 226, 320, 14, BLACK, 18);
  gfx_text(FONT_SMALL, 4, 227, "Arrows pick  OK choose  Back resume", KEYS);
}

/* ------------------------------------------------------------------ jumps */
static void warp_step(void) {
  if (pressed(UPS)) g.menu = (uint8_t)((g.menu + 5) % 6);
  if (pressed(DOWNS)) g.menu = (uint8_t)((g.menu + 1) % 6);
  if (pressed(K_BACK)) go(SC_PAUSE);
  if (pressed(OKS) && warp_open(g.menu)) {   /* 1804-1809 */
    g.w.x = WARP[g.menu][0], g.w.y = WARP[g.menu][1];
    world_save_pos();
    go(SC_WORLD);
  }
}

static void warp_band(void) {
  world_band_dim();
  gfx_blend(0, 0, 320, 240, BLACK, 18);
  gfx_text_c(FONT_BIG, 160, 8, "Jump", WHITE);
  uint8_t dim[6];
  for (int k = 0; k < 6; k++) dim[k] = !warp_open(k);
  menu_list(WARP_NAME, 6, g.menu, 70, 40, 180, dim);
  gfx_blend(0, 226, 320, 14, BLACK, 18);
  gfx_text(FONT_SMALL, 4, 227, "Arrows pick  OK jump  Back go back", KEYS);
}

/* ------------------------------------------------------------------ chips and bytes (4 worn each) */
static void wear_step(bool bytes) {
  uint8_t *worn = bytes ? g.f.bytes : g.f.chip;
  int n = bytes ? NBYTE : NCHIP;
  if (pressed(UPS)) g.menu = (uint8_t)((g.menu + n - 1) % n);
  if (pressed(DOWNS)) g.menu = (uint8_t)((g.menu + 1) % n);
  if (pressed(LEFTS)) g.menu = (uint8_t)((g.menu + n - 7) % n);
  if (pressed(RIGHTS)) g.menu = (uint8_t)((g.menu + 7) % n);
  if (pressed(K_BACK)) {
    g.dirty = 1;
    go(SC_PAUSE);
    g.menu = bytes ? P_BYTES : P_CHIPS;
    return;
  }
  if (!pressed(OKS)) return;
  int id = g.menu + 1;
  bool have = bytes ? g.f.byte_have[id] : g.f.chest[id];
  if (!have) return;
  for (int k = 0; k < 4; k++)
    if (worn[k] == id) {   /* off */
      worn[k] = 0;
      return;
    }
  for (int k = 0; k < 4; k++)
    if (!worn[k]) {
      worn[k] = (uint8_t)id;
      return;
    }
}

static void wear_band(bool bytes) {
  pic_part(PIC_MENU, 0, 0, 0, 0, 320, 240);
  gfx_blend(0, 0, 320, 240, BLACK, 16);
  const uint8_t *worn = bytes ? g.f.bytes : g.f.chip;
  int n = bytes ? NBYTE : NCHIP;
  gfx_text_c(FONT_BIG, 160, 2, bytes ? "Bytes" : "Chips", WHITE);
  for (int i = 0; i < n; i++) {
    int col = i / 7, row = i % 7;
    int x = 4 + 105 * col, y = 26 + 22 * row;
    int id = i + 1;
    bool have = bytes ? g.f.byte_have[id] : g.f.chest[id];
    bool on = false;
    for (int k = 0; k < 4; k++) on |= worn[k] == id;
    gfx_fill(x, y, 102, 19, on ? RGB(0x2A7A3A) : RGB(0x1A2858));
    if (g.menu == i) gfx_frame_rect(x - 1, y - 1, 104, 21, HILITE);
    const char *name = have ? (bytes ? BYTE_NAME[id] : CHIP_NAME[id]) : "???";
    gfx_text(FONT_SMALL, x + 3, y + 3, name, have ? WHITE : RGB(0x707890));
  }
  gfx_text(FONT_SMALL, 6, 184, "Worn:", WHITE);
  for (int k = 0; k < 4; k++) {
    const char *name = worn[k] ? (bytes ? BYTE_NAME[worn[k]] : CHIP_NAME[worn[k]]) : "-";
    gfx_text(FONT_SMALL, 44 + 70 * k, 184, name, RGB(0xFFD040));
  }
  gfx_blend(0, 226, 320, 14, BLACK, 18);
  gfx_text(FONT_SMALL, 4, 227, "Arrows pick  OK wear or take off  Back done", KEYS);
}

/* ------------------------------------------------------------------ the shops */
static int shop_first(int kind) {   /* the shop's events 56-62 */
  switch (kind) {
    case 1: return 1;
    case 2: return 4;
    case 5: return 7;
    case 20: return 10;
    case 11: return 13;
    case 14: return 16;
    case 17: return 19;
    default: return 1;
  }
}

static void shop_step(bool armor) {
  if (pressed(UPS)) g.menu = (uint8_t)((g.menu + 2) % 3);
  if (pressed(DOWNS)) g.menu = (uint8_t)((g.menu + 1) % 3);
  if (pressed(K_BACK)) {
    save_file();
    go(SC_WORLD);
    return;
  }
  if (!pressed(OKS)) return;
  if (armor) {   /* the armor shop: Reinforced, Steel, Titanium */
    static const uint16_t PRICE[3] = {250, 1500, 9000};
    static const uint8_t VALUE[3] = {1, 2, 10};
    int i = g.menu;
    if (!g.f.armor_have[i + 1] && g.f.tokens >= PRICE[i]) {
      g.f.tokens -= PRICE[i];
      g.f.armor = VALUE[i];
      for (int k = 1; k <= i + 1; k++) g.f.armor_have[k] = 1;
      g.dirty = 1;
    }
  } else {
    int id = shop_first(g.shop_kind) + g.menu;
    if (!g.f.byte_have[id] && g.f.tokens >= BYTE_PRICE[id]) {
      g.f.tokens -= BYTE_PRICE[id];
      g.f.byte_have[id] = 1;
      g.dirty = 1;
    }
  }
}

static void shop_band(bool armor) {
  pic_part(PIC_MENU, 0, 0, 0, 0, 320, 240);
  gfx_blend(0, 0, 320, 240, BLACK, 14);
  gfx_text_c(FONT_BIG, 160, 6, armor ? "Mendo's Armor" : "Lolbit's Byte Store", WHITE);
  static const char *const DESC[8] = {"These Bytes shoot stingers at enemies for extra damage!",
                                      "These Medpods will heal you during battles!",
                                      "These Reapers have a chance to instantly kill enemies!",
                                      "These UFO's shoot lasers for very high damage!",
                                      "These Shields block base damage from all attacks!",
                                      "These Bombs detonate once per battle for instant damage!",
                                      "These Eyebots work on bosses and steadily drain their life!", ""};
  int first = shop_first(g.shop_kind);
  for (int i = 0; i < 3; i++) {
    int y = 48 + 40 * i;
    const char *name;
    uint32_t price;
    bool sold;
    if (armor) {
      static const char *const AN[3] = {"Reinforced", "Steel", "Titanium"};
      static const uint16_t AP[3] = {250, 1500, 9000};
      name = AN[i], price = AP[i], sold = g.f.armor_have[i + 1];
    } else {
      name = BYTE_NAME[first + i], price = BYTE_PRICE[first + i], sold = g.f.byte_have[first + i];
    }
    gfx_fill(50, y, 220, 34, g.menu == i ? RGB(0x3A58B8) : RGB(0x1A2858));
    if (g.menu == i) gfx_frame_rect(49, y - 1, 222, 36, HILITE);
    if (!armor) pic_draw(ANIM_PICS[ANIMS[AN_BYTE1 + first + i - 1].first], 72, y + 17);
    gfx_text(FONT_TEXT, 96, y + 3, name, WHITE);
    char s[24];
    if (sold) strcpy(s, "Sold out");
    else num(s, (int32_t)price), strcat(s, " tokens");
    gfx_text(FONT_SMALL, 96, y + 19, s, sold ? RGB(0x90A0C0) : RGB(0xFFD040));
  }
  if (!armor) {
    int d = first == 1 ? 0 : first == 4 ? 1 : first == 7 ? 2 : first == 10 ? 3 : first == 13 ? 4 : first == 16 ? 5 : 6;
    gfx_text_c(FONT_SMALL, 160, 172, DESC[d], WHITE);
  } else
    gfx_text_c(FONT_SMALL, 160, 172, "Armor takes damage off every hit.", WHITE);
  char s[32];
  num(s, (int32_t)g.f.tokens);
  strcat(s, " Faz Tokens");
  gfx_text_c(FONT_TEXT, 160, 194, s, RGB(0xFFD040));
  gfx_blend(0, 226, 320, 14, BLACK, 18);
  gfx_text(FONT_SMALL, 4, 227, "Arrows pick  OK buy  Back leave", KEYS);
}

/* ------------------------------------------------------------------ how to play, game over */
static void howto_band(void) {
  pic_part(PIC_MENU, 0, 0, 0, 0, 320, 240);
  gfx_blend(0, 0, 320, 240, BLACK, 20);
  gfx_text_c(FONT_BIG, 160, 4, "How to play", WHITE);
  static const char *const L[] = {
    "Walk the map with the arrows. Enemies jump",
    "you as you walk. Chests hold chips.",
    "In battle each member charges up. When one",
    "is ready, pick an attack with the arrows",
    "and OK (or 1, 2, 3). The other row of four",
    "comes in with the backspace key. Hold 0 to",
    "run. Win for exp, levels and Faz Tokens.",
    "Spend tokens at Lolbit's for bytes, at",
    "Mendo's for armor. Wear chips and bytes",
    "from the menu (Back). Beat challengers to",
    "get new members.",
  };
  for (unsigned i = 0; i < sizeof L / sizeof L[0]; i++) gfx_text(FONT_SMALL, 10, 32 + 16 * (int)i, L[i], WHITE);
  gfx_blend(0, 226, 320, 14, BLACK, 18);
  gfx_text(FONT_SMALL, 4, 227, "Back go back", KEYS);
}

static void gameover_band(void) {
  gfx_fill(0, gfx_y0, 320, BAND_H, BLACK);
  gfx_text_c(FONT_HUGE, 160, 90, "GAME OVER", RGB(0xE03030));
  if (g.t > 60) gfx_text_c(FONT_SMALL, 160, 140, "OK go on", KEYS);
}

/* ------------------------------------------------------------------ the steps */
void game_step(uint32_t keys) {
  g.pressed = keys & ~g.keys;
  g.keys = keys;
  g.t++;
  g.now += 50;
  if (pressed(K_HOME)) {   /* Home: keep it all and quit */
    if (g.scene != SC_TITLE && g.scene != SC_FILES && g.f.started) {
      if (g.scene == SC_WORLD || g.scene == SC_SHOP || g.scene == SC_ARMOR) world_save_pos();
      save_file();
    }
    g.running = false;
    return;
  }
  switch (g.scene) {
    case SC_TITLE:
      if (pressed(OKS)) {
        go(SC_FILES);
        for (int i = 0; i < 3; i++) save_summary(i + 1, &summary[i].mode, &summary[i].diff, &summary[i].h, &summary[i].m, &summary[i].st);
      }
      if (pressed(K_BACK)) g.running = false;
      break;
    case SC_FILES: files_step(); break;
    case SC_PARTY: party_step(); break;
    case SC_WORLD:
      if (pressed(K_BACK)) {
        g.back = SC_WORLD;
        go(SC_PAUSE);
        break;
      }
      world_step();
      if (g.t % 1800 == 0) world_save_pos(), save_file();   /* now and then */
      break;
    case SC_BATTLE:
      if (pressed(K_BACK)) {
        g.back = SC_BATTLE;
        go(SC_PAUSE);
        break;
      }
      battle_keys(g.pressed, g.keys);
      battle_step();
      if (!g.b.on && g.scene == SC_BATTLE) {
        world_after_battle();
        save_file();
        go(SC_WORLD);
      }
      break;
    case SC_PAUSE: pause_step(); break;
    case SC_WARP: warp_step(); break;
    case SC_CHIPS: wear_step(false); break;
    case SC_BYTES: wear_step(true); break;
    case SC_SHOP: shop_step(false); break;
    case SC_ARMOR: shop_step(true); break;
    case SC_HOWTO:
      if (pressed(K_BACK) || pressed(OKS)) go(SC_PAUSE), g.menu = P_HOWTO;
      break;
    case SC_GAMEOVER:   /* (the frame "game over", then the start again) */
      if (g.t > 60 && pressed(OKS)) {
        save_file();
        world_enter();
        go(SC_WORLD);
      }
      break;
  }
  if (g.dirty && g.t % 600 == 0 && g.f.started) save_file();
}

static void scene_band(void) {
  switch (g.scene) {
    case SC_TITLE: title_band(); break;
    case SC_FILES: files_band(); break;
    case SC_PARTY: party_band(); break;
    case SC_PAUSE: pause_band(); break;
    case SC_WARP: warp_band(); break;
    case SC_CHIPS: wear_band(false); break;
    case SC_BYTES: wear_band(true); break;
    case SC_SHOP: shop_band(false); break;
    case SC_ARMOR: shop_band(true); break;
    case SC_HOWTO: howto_band(); break;
    case SC_GAMEOVER: gameover_band(); break;
  }
}

void game_draw(void) {
  if (g.scene == SC_WORLD) world_draw();
  else if (g.scene == SC_BATTLE) battle_draw();
  else {
    world_prepare_view();
    battle_prepare_view();
    gfx_frame(scene_band);
  }
}
