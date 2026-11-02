/* The title, the world map and its stage window (Map.as, LevelSelect.as), the upgrades (MenuUpgrades.as), the
 * help page. */
#include <stdio.h>
#include <string.h>
#include "game.h"
#include "gfx.h"
#include "kr.h"
#include "strings.h"

#define C_TEXT RGB(0xF4E6C8)
#define C_GOLD RGB(0xFFD32A)
#define C_DIM RGB(0x9A8A70)

/* Map.§_-2§: the stages' flags on the map (at full size) */
static const int16_t flags[12][2] = {{104, 482}, {87, 422}, {144, 398}, {206, 373}, {152, 290}, {141, 186},
                                     {278, 123}, {359, 118}, {437, 151}, {530, 206}, {526, 301}, {557, 393}};
static int fx(int i) { return flags[i][0] * 32 / 70; }
static int fy(int i) { return flags[i][1] * 32 / 70 - MAP_Y0; }

static bool unlocked(int i) { return i == 0 || g.p.camp[i - 1] > 0; }

static void panel(int x, int y, int w, int h) {
  gfx_panel(x, y, w, h, RGB(0x3A2614), RGB(0xC09A5A));
  gfx_frame_rect(x + 2, y + 2, w - 4, h - 4, RGB(0x6A4A26));
}

static void key_line(const char *s) {
  if (!gfx_rows(228, 12)) return;
  gfx_blend(0, 228, 320, 12, BLACK, 22);
  gfx_text_c(FONT_SMALL, 160, 229, s, C_TEXT);
}

/* ------------------------------------------------------------------ title */
static uint8_t title_sel;

void title_step(void) {
  if (g.pressed & (K_OK | K_EXE)) {
    g.screen = SCR_MAP;
    /* the first stage not yet won */
    g.map_sel = 0;
    for (int i = 0; i < NLEVELS; i++)
      if (unlocked(i)) g.map_sel = (uint8_t)i;
  }
  if (g.pressed & K_BACK) game_quit();
  (void)title_sel;
}

static void title_band(void) {
  pic_draw(PIC_TITLE, 0, 0);
  key_line("OK start   Back quit");
}
void title_draw(void) { gfx_frame(title_band); }

/* ------------------------------------------------------------------ the map */
static uint8_t win_open, win_mode;

static bool mode_open(int i, int m) { return m == 0 || g.p.camp[i] >= 3; }

void map_step(void) {
  uint32_t pr = g.pressed;
  if (win_open) {
    if (pr & K_BACK) win_open = 0;
    if (play_key(K_LEFT)) win_mode = (uint8_t)((win_mode + 2) % 3);
    if (play_key(K_RIGHT)) win_mode = (uint8_t)((win_mode + 1) % 3);
    if (pr & (K_OK | K_EXE)) {
      if (!mode_open(g.map_sel, win_mode)) return;
      win_open = 0;
      play_start(g.map_sel, win_mode);
    }
    return;
  }
  if (pr & K_BACK) {
    g.screen = SCR_TITLE;
    return;
  }
  if (pr & (K_OK | K_EXE)) {
    win_open = 1, win_mode = 0;
    return;
  }
  if (pr & K_DIGIT(0) || pr & K_DEL) {
    g.screen = SCR_UPGRADES;
    return;
  }
  int dx = 0, dy = 0;
  if (play_key(K_LEFT)) dx = -1;
  else if (play_key(K_RIGHT)) dx = 1;
  else if (play_key(K_UP)) dy = -1;
  else if (play_key(K_DOWN)) dy = 1;
  if (dx || dy) {
    /* the unlocked stage nearest that way */
    int cur = g.map_sel, best = -1;
    int bs = 1 << 30;
    for (int i = 0; i < NLEVELS; i++) {
      if (i == cur || !unlocked(i)) continue;
      int vx = fx(i) - fx(cur), vy = fy(i) - fy(cur);
      int along = vx * dx + vy * dy, across = vx * dy - vy * dx;
      if (across < 0) across = -across;
      if (along <= 0) continue;
      int sc = along + 2 * across;
      if (sc < bs) bs = sc, best = i;
    }
    if (best >= 0) g.map_sel = (uint8_t)best;
  }
}

static void map_band(void) {
  pic_draw(PIC_MAP, 0, 0);
  /* the road so far: to the stage after the last one won */
  int won = 0;
  while (won < NLEVELS && g.p.camp[won]) won++;
  if (won > 11) won = 11;
  if (won) spr_anim(AN_UI_ROAD, won - 1, false, 0, -MAP_Y0, false);
  for (int i = 0; i < NLEVELS; i++) {
    if (!unlocked(i)) continue;
    int x = fx(i), y = fy(i);
    if (i == g.map_sel) {
      gfx_ellipse(x, y + 1, 12, 5, RGB(0xFFE14D), 32);
      gfx_ellipse(x, y + 1, 13, 6, RGB(0x6A4A00), 32);
    }
    if (g.p.camp[i]) {
      spr_anim(AN_UI_FLAGDONE, 0, false, x, y, false);
      for (int k = 0; k < 3; k++) spr_anim(AN_UI_FLAGSTAR, k < g.p.camp[i] ? 1 : 0, false, x - 13 + 7 * k, y + 3, false);
      if (g.p.heroic[i]) spr_anim(AN_UI_FLAGHEROIC, 0, false, x, y, false);
    } else
      spr_anim(AN_UI_FLAGNEW, 0, false, x, y, false);
  }
  /* the stars: won, and free to spend */
  if (gfx_rows(0, 30)) {   /* MapStarProgress: the stars won */
    spr_anim(AN_UI_STARBOARD, 0, false, 248, 0, false);
    char s[24];
    snprintf(s, sizeof s, "%d/%d", stars_won(), stars_max());
    gfx_text_oc(FONT_HUD, 278, 6, s, WHITE);
  }
  if (!win_open) {
    char s[64];
    snprintf(s, sizeof s, "OK play %s    0 upgrades", level_title(g.map_sel));
    key_line(s);
    return;
  }
  /* LevelSelect: the stage's title, its story, its three modes */
  int i = g.map_sel;
  if (gfx_rows(4, 220)) {
    panel(4, 4, 312, 220);
    gfx_text_oc(FONT_BIG, 160, 8, level_title(i), C_GOLD);
    gfx_text_box(FONT_TEXT, 16, 36, 290, level_story(i), C_TEXT);
    static const char *const names[3] = {"Campaign", "Heroic", "Iron"};
    for (int m = 0; m < 3; m++) {
      int x = 75 + 85 * m, y = 180;
      bool open = mode_open(i, m), sel = m == win_mode;
      gfx_panel(x - 36, y, 72, 20, sel ? RGB(0xE0A020) : RGB(0x5A3E20), sel ? WHITE : RGB(0xC09A5A));
      gfx_text_c(FONT_HUD, x, y + 3, names[m], !open ? C_DIM : sel ? RGB(0x2A1A0C) : C_TEXT);
      bool done = m == 0 ? g.p.camp[i] > 0 : m == 1 ? g.p.heroic[i] : g.p.iron[i];
      if (done) spr_anim(AN_UI_STAR, 0, false, x + 24, y - 8, false);
    }
    const char *rule = win_mode == 0 ? "Lose a life for each enemy that gets through."
                       : !mode_open(i, win_mode) ? "Win this stage with 3 stars to open it."
                       : win_mode == 1 ? "6 elite waves, 1 life, upgrades capped."
                                       : "1 big wave, 1 life, some towers barred.";
    gfx_text_c(FONT_SMALL, 160, 206, rule, C_DIM);
  }
  key_line("Left Right mode   OK fight   Back close");
}
void map_draw(void) { gfx_frame(map_band); }

/* ------------------------------------------------------------------ upgrades (MenuUpgrades) */
/* the costs in stars, by line and level (MenuUpgrades.§_-C9§) */
static const uint8_t up_cost[6][5] = {{1, 1, 2, 2, 3}, {1, 1, 2, 2, 3}, {1, 1, 2, 2, 3},
                                      {1, 1, 3, 3, 3}, {1, 1, 2, 2, 3}, {2, 3, 3, 3, 4}};
/* the panel's columns in the game's order (archers, barracks, mages, artillery, rain, reinforcements) */
static const uint8_t col_x[6] = {21, 53, 84, 115, 146, 177};
static const uint8_t row_y[5] = {165, 144, 124, 102, 82};
static uint8_t up_col, up_row;

int stars_max(void) { return 3 * NLEVELS + 2 * NLEVELS; }   /* 3 a campaign, 1 a heroic, 1 an iron challenge */
int stars_won(void) {
  int n = 0;
  for (int i = 0; i < NLEVELS; i++) n += g.p.camp[i] + g.p.heroic[i] + g.p.iron[i];
  return n;
}
int stars_free(void) {
  int spent = 0;
  for (int l = 0; l < 6; l++)
    for (int k = 0; k < g.p.ups[l]; k++) spent += up_cost[l][k];
  return stars_won() - spent;
}

void upgrades_step(void) {
  uint32_t pr = g.pressed;
  if (pr & (K_BACK | K_DIGIT(0))) {
    save_progress();
    g.screen = SCR_MAP;
    return;
  }
  if (play_key(K_LEFT)) up_col = (uint8_t)((up_col + 5) % 6);
  if (play_key(K_RIGHT)) up_col = (uint8_t)((up_col + 1) % 6);
  if (play_key(K_UP) && up_row < 4) up_row++;
  if (play_key(K_DOWN) && up_row > 0) up_row--;
  if (pr & (K_OK | K_EXE)) {
    int have = g.p.ups[up_col];
    if (up_row == have && have < 5 && stars_free() >= up_cost[up_col][up_row]) {
      g.p.ups[up_col]++;
      save_progress();
    }
  }
  if (pr & K_DEL) {   /* reset: every star back */
    memset(g.p.ups, 0, sizeof g.p.ups);
    save_progress();
  }
}

static void upgrades_band(void) {
  pic_draw(PIC_UPGRADES, 0, 0);
  /* the game's icon order: barracks first, then archers */
  static const uint8_t icon_line[6] = {1, 0, 2, 3, 4, 5};
  for (int c = 0; c < 6; c++)
    for (int r = 0; r < 5; r++) {
      int x = UPG_X + col_x[c], y = row_y[r];
      int have = g.p.ups[c];
      int state = r < have ? 2 : r == have ? 0 : 1;
      spr_anim(AN_UI_UPITEM, state, false, x, y, false);
      int icon = icon_line[c] * 5 + r;
      if (state == 1) spr_frame_tint(anim_slot(AN_UI_UPICONS, icon, false), x, y, false, RGB(0x202020), 20);
      else spr_anim(AN_UI_UPICONS, icon, false, x, y, false);
      if (state == 0) {
        char s[4];
        snprintf(s, sizeof s, "%d", up_cost[c][r]);
        gfx_text_oc(FONT_SMALL, x + 6, y + 6, s, stars_free() >= up_cost[c][r] ? C_GOLD : RGB(0xFF6040));
      }
      if (c == up_col && r == up_row) {
        gfx_frame_rect(x - 10, y - 10, 21, 21, RGB(0xFFE14D));
        gfx_frame_rect(x - 11, y - 11, 23, 23, RGB(0x6A4A00));
      }
    }
  /* stars to spend, and what the chosen one does */
  char s[48];
  snprintf(s, sizeof s, "%d", stars_free());
  spr_anim(AN_UI_STAR, 0, false, UPG_X + 8, 213, false);
  gfx_text_o(FONT_HUD, UPG_X + 25, 213, s, C_GOLD);
  if (gfx_rows(0, 240)) {
    const char *nm = upgrade_name(up_col, up_row), *ds = upgrade_desc(up_col, up_row);
    gfx_fill(0, 0, UPG_X - 2, 240, RGB(0x1E140B));
    gfx_fill(UPG_X + 217, 0, 320 - UPG_X - 217, 240, RGB(0x1E140B));
    gfx_text_box(FONT_SMALL, UPG_X + 220, 40, 48, nm, C_GOLD);
    gfx_text_box(FONT_SMALL, UPG_X + 220, 80, 48, ds, C_TEXT);
    gfx_text_box(FONT_SMALL, 2, 40, 48, "OK buy", C_TEXT);
    gfx_text_box(FONT_SMALL, 2, 64, 48, "Clear: all stars back", C_DIM);
    gfx_text_box(FONT_SMALL, 2, 110, 48, "Back map", C_TEXT);
  }
}
void upgrades_draw(void) { gfx_frame(upgrades_band); }

/* ------------------------------------------------------------------ how to play */
void help_step(void) {
  if (g.pressed & (K_BACK | K_OK | K_EXE)) g.screen = SCR_BATTLE;
}

static void help_band(void) {
  gfx_fill(0, gfx_y0, 320, BAND_H, RGB(0x2A1C10));
  gfx_text_oc(FONT_BIG, 160, 6, "How to play", C_GOLD);
  static const char *const lines[] = {
    "Enemies walk the roads. Each one that gets through costs lives.",
    "Arrows: hop between build spots, wave flags and enemies.",
    "OK on a spot: the tower menu. Arrows pick, OK buys.",
    "Archers and mages shoot. Bombards blast ground enemies.",
    "Barracks send soldiers to block the road (rally point).",
    "1: rain of fire.  2: reinforcements. Aim with the arrows.",
    "Clear key (or the flag): the next wave now, for gold.",
    "Back: pause.  Home: save and quit.",
  };
  for (int i = 0; i < (int)(sizeof lines / sizeof lines[0]); i++) gfx_text(FONT_TEXT, 12, 40 + 22 * i, lines[i], C_TEXT);
  gfx_text_c(FONT_SMALL, 160, 226, "OK back", C_DIM);
}
void help_draw(void) { gfx_frame(help_band); }
