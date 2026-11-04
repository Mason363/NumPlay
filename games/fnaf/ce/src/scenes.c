/* The scenes around the nights on the CE: game.c's steps (the NumWorks version's, as they are), and drawings of
 * the CE's own for the title, the newspaper, how to play, the night's card, the pause, 6 AM, the paychecks, the
 * game over, Golden Freddy and the custom night. */
#define FNAF_CE 1
#include "fnaf.h"
#include "../../src/game.c"


static int fade(float t, float in, float hold, float out) {   /* 0 black to 32 */
  float a = t < in ? t / in : t < hold ? 1 : t < hold + out ? 1 - (t - hold) / out : 0;
  return (int)(a * 32);
}

/* ------------------------------------------------------------------ the title */

static void title_draw(void) {
  static const int faces[4][5] = {{0}, {PIC_TITLE1, TITLE1_X, TITLE1_Y, TITLE1_W, TITLE1_H},
                                  {PIC_TITLE2, TITLE2_X, TITLE2_Y, TITLE2_W, TITLE2_H},
                                  {PIC_TITLE3, TITLE3_X, TITLE3_Y, TITLE3_W, TITLE3_H}};
  scene_want(PIC_TITLE0);
  d_blit(scene, SCENE_W, 0, 0, SCR_W, SCENE_H, 0, 0, true);
  if (face) {
    const int *fc = faces[face];
    extra_want(fc[0], fc[3]);
    d_blit(extra, fc[3], 0, 0, fc[3], fc[4], fc[1], 2 * fc[2], true);
  }
  int grp = pic_group_of(PIC_TITLE0);
  pal_set(grp, face_light, static_phases(7), 0);
  /* the slow light band going down, and lines that flash */
  const uint8_t *light = map_remap(grp, 1);
  int sy = (int)(g.t / 20 * (SCR_H + 10)) % (SCR_H + 10) - 10;
  for (int r = 1; r < 10; r += 4) d_remap(0, sy + r, SCR_W, 1, light);
  if ((int)(g.t * 10) % 7 == 0) d_remap(0, (int)(g.t * 997) % SCR_H, SCR_W, 1, light);
  d_img(asset(ASSET_IMG_TITLE), 20, 18, INK_WHITE);
  for (int i = 0; i < 3; i++)
    if (g.stars >> i & 1) d_img(asset(ASSET_IMG_STAR), 20 + 27 * i, 132, INK_WHITE);
  static const char *const items[] = {"New Game", "Continue", "6th Night", "Custom Night"};
  int y0 = g.stars ? 160 : 150;
  for (int i = 0; i < g.menu_n; i++) {
    int y = y0 + 18 * i;
    d_text(FONT_BIG, 44, y, items[i], INK_WHITE);
    if (g.menu == i) d_text(FONT_BIG, 20, y, ">>", INK_WHITE);
  }
  if (g.menu == M_CONTINUE) {
    char s[] = "Night 0";
    s[6] = (char)('0' + g.reached);
    d_text(FONT_HUD, 44, y0 + 18 * g.menu_n + 4, s, INK_WHITE);
  }
}

/* ------------------------------------------------------------------ the newspaper, how to play, the card */
static void news_draw(void) {
  still(PIC_NEWSPAPER);
  pal_set(pic_group_of(PIC_NEWSPAPER), fade(g.t, 2, 7, 2), NULL, 0);
}

static void keys_draw(void) {
  static const char *const rows[][2] = {
    {"In the office", NULL},
    {"4 6, < >", "look left and right"},
    {"7   1", "left door, left light"},
    {"9   3", "right door, right light"},
    {"2nd, 8", "the monitor up and down"},
    {"0", "mute the call"},
    {"On the monitor", NULL},
    {"1 to 7", "a camera (again: 1B, 1C...)"},
    {"arrows", "around the map"},
    {"clear", "pause"},
  };
  d_fill(0, 0, SCR_W, SCR_H, UI_BLACK);
  d_text_c(FONT_BIG, 160, 8, "How to survive", INK_WHITE);
  int y = 34;
  for (unsigned i = 0; i < sizeof rows / sizeof rows[0]; i++) {
    if (!rows[i][1]) {
      d_text(FONT_HUD, 16, y + 2, rows[i][0], INK_MINT);
      y += 17;
      continue;
    }
    d_text(FONT_HUD, 26, y, rows[i][0], INK_WHITE);
    d_text(FONT_TEXT, 108, y, rows[i][1], INK_GREY);
    y += 15;
  }
  d_text_c(FONT_TEXT, 160, 196, "Watch the power: every door, light and", INK_GREY);
  d_text_c(FONT_TEXT, 160, 209, "camera drains it. Survive until 6 AM.", INK_GREY);
  if ((int)(g.t * 2) & 1) d_text_c(FONT_HUD, 160, 226, "2nd", INK_WHITE);
  pal_set(-1, 32, NULL, 0);
}

static const char *const nth[] = {"", "1st Night", "2nd Night", "3rd Night", "4th Night", "5th Night", "6th Night",
                                  "7th Night"};

static void intro_draw(void) {
  d_fill(0, 0, SCR_W, SCR_H, UI_BLACK);
  if (g.t < 0.15f) d_noise(0, 0, SCR_W, SCR_H, rnd(), (const uint8_t[8]){UI_BLACK, UI_BOX, UI_EDGE, UI_BLACK,
                                                                         UI_GREY1, UI_BOX, UI_BLACK, UI_EDGE});
  d_text_c(FONT_HUGE, 160, 92, "12:00 AM", INK_WHITE);
  d_text_c(FONT_BIG, 160, 124, nth[g.n.night], INK_WHITE);
  pal_set(-1, 32, NULL, 0);
  if (g.t > 1.2f) pal_ui((int)((2.2f - g.t) * 32));
}

/* ------------------------------------------------------------------ the night, paused */
static void pause_draw(void) {
  view_paused();
  if (g.confirm) {
    d_text_c(FONT_BIG, 160, 84, "Quit this night?", INK_WHITE);
    d_text_c(FONT_TEXT, 160, 106, "It starts over next time.", INK_SILVER);
    d_text(FONT_BIG, 104, 132, "No", INK_WHITE);
    d_text(FONT_BIG, 190, 132, "Yes", INK_WHITE);
    d_text(FONT_BIG, g.pause_item ? 168 : 82, 132, ">>", INK_WHITE);
    return;
  }
  d_text_c(FONT_HUGE, 160, 62, "Paused", INK_WHITE);
  static const char *const items[] = {"Resume", "How to play", "Quit night"};
  for (int i = 0; i < 3; i++) {
    d_text(FONT_BIG, 112, 104 + 20 * i, items[i], INK_WHITE);
    if (g.pause_item == i) d_text(FONT_BIG, 88, 104 + 20 * i, ">>", INK_WHITE);
  }
}

/* ------------------------------------------------------------------ 6 AM, the paychecks */
static void six_draw(void) {
  d_fill(0, 0, SCR_W, SCR_H, UI_BLACK);
  /* 5 rolls up into 6 */
  float r = g.t < 1 ? 0 : g.t > 6 ? 1 : (g.t - 1) / 5;
  int cy = 104, dh = 30, off = (int)(r * dh);
  d_text(FONT_HUGE, 118, cy - off, "5", INK_WHITE);
  d_text(FONT_HUGE, 118, cy + dh - off, "6", INK_WHITE);
  d_fill(110, cy - dh, 30, dh, UI_BLACK);    /* (only the window between shows) */
  d_fill(110, cy + dh, 30, dh, UI_BLACK);
  d_text(FONT_HUGE, 148, cy, "AM", INK_WHITE);
  d_text_c(FONT_TEXT, 160, 170, g.t > 6.2f ? "*children cheering*" : "*chimes*", INK_SILVER);
  pal_set(-1, 32, NULL, 0);
  pal_ui(fade(g.t, 1, 10.5f, 1));
}

static void paper_draw(void) {
  static const int pics[3] = {PIC_PAYCHECK, PIC_OVERTIME, PIC_PINKSLIP};
  still(pics[g.paper]);
  pal_set(pic_group_of(pics[g.paper]), fade(g.t, 2, 17, 2), NULL, 0);
}

/* ------------------------------------------------------------------ the end of a night */
static const uint8_t GREYS[8] = {UI_BLACK, UI_WHITE1, UI_GREY1, UI_SPARE, UI_DIM, UI_GREY2, UI_EDGE, UI_BOX};

static void over_draw(void) {
  if (g.t < 2.5f) {
    still_forget();
    d_noise(0, 0, SCR_W, SCR_H, rnd(), GREYS);
    pal_set(-1, 32, NULL, 0);
    return;
  }
  if (!still(PIC_GAMEOVER)) d_text_r(FONT_HUGE, 306, 200, "Game Over", INK_WHITE);
  pal_set(pic_group_of(PIC_GAMEOVER), 32, static_phases(g.t < 3 ? 26 : 6), 0);
}

static void golden_draw(void) {
  still(PIC_GOLDEN);
  pal_set(pic_group_of(PIC_GOLDEN), 32, static_phases(6 + (int)(rnd() % 8)), 0);
}

/* ------------------------------------------------------------------ the custom night */
static const int face_pics[4] = {PIC_FACE_FREDDY, PIC_FACE_BONNIE, PIC_FACE_CHICA, PIC_FACE_FOXY};
static const char *const names[4] = {"Freddy", "Bonnie", "Chica", "Foxy"};

static void custom_draw(void) {
  /* the faces, in extra once */
  if (extra_pic != EXTRA_FACES) {
    for (int i = 0; i < 4; i++) pic_full(face_pics[i], extra + 64 * 64 * i, 64);
    extra_pic = EXTRA_FACES;
  }
  d_fill(0, 0, SCR_W, SCR_H, UI_BLACK);
  d_text_c(FONT_BIG, 160, 14, "Customize Night", INK_WHITE);
  for (int i = 0; i < 4; i++) {
    int x = 12 + 76 * i, y = 52;
    d_blit(extra + 64 * 64 * i, 64, 0, 0, 64, 64, x, y, false);
    d_frame(x - 2, y - 2, 68, 68, g.custom_sel == i ? UI_WHITE : UI_EDGE);
    d_text_c(FONT_HUD, x + 32, y + 72, names[i], INK_WHITE);
    d_text_c(FONT_TEXT, x + 32, y + 88, "A.I. Level", INK_SILVER);
    char s[3] = {(char)('0' + g.custom[i] / 10), (char)('0' + g.custom[i] % 10), 0};
    d_text_c(FONT_HUGE, x + 32, y + 104, g.custom[i] >= 10 ? s : s + 1, INK_WHITE);
    if (g.custom_sel == i) {
      d_text(FONT_BIG, x + 2, y + 106, "<", INK_WHITE);
      d_text(FONT_BIG, x + 52, y + 106, ">", INK_WHITE);
    }
  }
  d_text(FONT_TEXT, 12, 214, "< > pick   up down: level", INK_DIM);
  d_text_r(FONT_BIG, 308, 208, "Ready", (int)(g.t * 2) & 1 ? INK_WHITE : INK_GREY);
  pal_set(pic_group_of(PIC_FACE_FREDDY), 32, NULL, 0);
}

/* ------------------------------------------------------------------ a frame */
void game_draw(void) {
  switch (g.scene) {
  case SC_NEWS: news_draw(); return;
  case SC_PAPER: paper_draw(); return;
  case SC_GOLDEN: golden_draw(); return;
  case SC_OVER: over_draw(); return;
  }
  still_forget();
  switch (g.scene) {
  case SC_TITLE: title_draw(); break;
  case SC_KEYS: keys_draw(); break;
  case SC_INTRO: intro_draw(); break;
  case SC_NIGHT:
    if (g.paused) pause_draw();
    else view_night();
    break;
  case SC_SCARE: view_scare(); break;
  case SC_SIX: six_draw(); break;
  case SC_CUSTOM: custom_draw(); break;
  }
}
