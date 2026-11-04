/* Moto X3M: the HUD and the menus, drawn a band at a time (GameplayState's popups, ChooseZone, Garage, Menu). */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "bike.h"
#include "data.h"
#include "fx.h"
#include "game.h"
#include "gfx.h"
#include "level.h"
#include "view.h"

#define ORANGE 0xFC60     /* #FF8F00 */
#define WHITE 0xFFFF
#define BLACK 0x0000
#define GREY 0xDEDA       /* #DDD9D7 */

const int *shop_prices(void);

static void fmt_time(char *s, float t, bool pad2) {
  int w = (int)ceilf(100 * t);
  if (w < 0) w = 0;
  int p = w / 100;
  w -= 100 * p;
  if (pad2) snprintf(s, 16, "%02d.%02d", p, w);
  else snprintf(s, 16, "%d.%02d", p, w);
}

/* text in the game's titles' look: an outline, then the letters */
static void title(int font, float x, float y, const char *s, uint16_t c, int align) {
  gfx_text(font, x, y, s, c, WHITE, 2, align);
  gfx_text(font, x, y, s, c, BLACK, 1, align);
}

static void bike_pose(int layout, float cx, float cy, float scale) {
  for (int i = 0; i < 11; i++) {
    const BikePart *p = &BIKE_LAYOUT[layout][i];
    gfx_pic(p->pic, cx + p->x * 0.25f * scale, cy + p->y * 0.25f * scale, scale, scale, p->deg * 0.0174533f, 256);
  }
}

static void button(int pic_id, int icon, float x, float y, bool sel) {
  float k = sel ? 0.84f + 0.04f * sinf(view_ticks * 0.3f) : 0.8f;
  gfx_pic(pic_id, x, y, k, k, 0, sel ? 256 : 200);
  if (icon >= 0) gfx_pic(icon, x, y - 3 * k, k, k, 0, 256);
  if (sel) {
    const Pic *p = pic(pic_id);
    float w = p->w * k, h = p->h * k;
    gfx_fill((int)(x - w / 3), (int)(y + h / 2 - 4), (int)(w * 2 / 3), 2, ORANGE, 256);
  }
}

/* ---- in a run ---- */

static void hud(void) {
  char s[24];
  gfx_pic(PIC_UI_TIMER_BG, 160, 12, 1, 1, 0, 256);
  fmt_time(s, g.timer, true);
  gfx_text(FONT_SMALL, 159, 17, s, GREY, BLACK, 0, 0);
  snprintf(s, sizeof s, "#%d", g.level + 1);
  gfx_text(FONT_SMALL, 6, 15, s, GREY, BLACK, 1, 0);
  if (g.flip_t < 1.1f && !bike.finished) {
    char a[16], b[16];
    if (g.flip_n > 1) snprintf(a, sizeof a, "FLIP %dX", g.flip_n);
    else snprintf(a, sizeof a, "FLIP");
    float d = 0.5f * g.flip_n;
    if (d == (int)d) snprintf(b, sizeof b, "-%d sec", (int)d);
    else snprintf(b, sizeof b, "-%d.5 sec", (int)d);
    title(FONT_BIG, 160, 50, a, ORANGE, 1);
    gfx_text(FONT_MID, 160, 66, b, ORANGE, BLACK, 1, 1);
  }
  if (g.help) {
    gfx_fill(70, 200, 180, 26, BLACK, 180);
    gfx_pic(PIC_UI_ARROWS + (int)(view_ticks / 15 % 2), 76, 199, 0.85f, 0.85f, 0, 256);
    gfx_text(FONT_SMALL, 128, 218, "Use arrows to move", WHITE, BLACK, 0, 0);
  } else if (bike.dead && !bike.finished && g.dead_t > 0.3f) {
    gfx_fill(70, 200, 180, 22, BLACK, 180);
    gfx_text(FONT_SMALL, 160, 215, "Press OK to continue", WHITE, BLACK, 0, 1);
  }
}

static void popup_back(void) {
  gfx_fill(0, 0, SCREEN_W, SCREEN_H, BLACK, 110);
  gfx_pic(PIC_UI_0POPUP_BG, 160, 124, 0.95f, 1.05f, 0, 256);
}

static void pause_menu(void) {
  popup_back();
  title(FONT_BIG, 160, 46, g.confirm ? "QUIT GAME?" : "GAME PAUSED", WHITE, 1);
  if (g.confirm) {
    bike_pose(0, 160, 120, 0.8f);
    const char *opts[2] = {"YES", "NO"};
    for (int i = 0; i < 2; i++) {
      bool sel = g.sel == i;
      float x = i ? 205 : 115;
      title(FONT_MID, x, 196, opts[i], sel ? ORANGE : WHITE, 1);
      if (sel) gfx_fill((int)x - 18, 200, 36, 2, ORANGE, 256);
    }
    return;
  }
  bike_pose(0, 110, 128, 0.8f);
  static const char *items[4] = {"RESUME", "RESTART", "LEVELS", "QUIT GAME"};
  for (int i = 0; i < 4; i++) {
    bool sel = g.sel == i;
    float y = 92 + i * 26;
    title(FONT_MID, 205, y, items[i], sel ? ORANGE : WHITE, 1);
    if (sel) gfx_fill(170, (int)y + 4, 70, 2, ORANGE, 256);
  }
}

static void star(float x, float y, float sc, float ang, bool on, float age) {
  gfx_pic(PIC_UI_STAR0, x, y, sc, sc, ang, 256);
  if (on && age >= 0) {
    /* comes down from three times its size */
    float t = age / 0.4f, k = t >= 1 ? sc : sc * (3 - 2 * t);
    gfx_pic(PIC_UI_STAR1, x, y, k, k, ang, t >= 1 ? 256 : (int)(256 * t));
  }
}

static void finish_menu(void) {
  char s[40], t[16];
  popup_back();
  snprintf(s, sizeof s, g.level + 1 < 10 ? "LEVEL 0%d COMPLETE" : "LEVEL %d COMPLETE", g.level + 1);
  title(FONT_BIG, 160, 30, s, ORANGE, 1);
  /* stars: the times for one, two and three */
  float a = g.finish_t - 0.2f;
  float times[3] = {lv.h->prize[2], lv.h->prize[1], lv.h->prize[0]};
  float xs[3] = {94, 160, 226}, ys[3] = {82, 76, 82}, ang[3] = {-0.1745f, 0, 0.1745f}, sc[3] = {0.8f, 0.9f, 0.8f};
  float ages[3] = {a, a - 0.4f, a - 1.1f};
  for (int i = 0; i < 3; i++) {
    star(xs[i], ys[i], sc[i], ang[i], g.stars > i, ages[i]);
    snprintf(t, sizeof t, "%d sec.", (int)times[i]);
    gfx_text(FONT_SMALL, xs[i], ys[i] + 40 - (i == 1 ? 4 : 0), t, GREY, BLACK, 1, 1);
  }
  fmt_time(t, g.result, false);
  snprintf(s, sizeof s, "Your time: %s sec.", t);
  gfx_text(FONT_SMALL, 160, 143, s, GREY, BLACK, 1, 1);
  if (g.improved) {
    gfx_pic(PIC_UI_IMPROVED_BG, 266, 150, 0.8f, 0.8f, 0.4363f, 256);
    gfx_text(FONT_SMALL, 264, 147, "NEW BEST", WHITE, BLACK, 1, 1);
    gfx_text(FONT_SMALL, 268, 158, "TIME", WHITE, BLACK, 1, 1);
  }
  button(PIC_UI_ORANGE_BTN, PIC_UI_EXIT_BTN, 44, 214, g.sel == 0);
  button(PIC_UI_ORANGE_BTN, PIC_UI_NEXT_BTN, 276, 214, g.sel == 1);
  {
    bool sel = g.sel == 2;
    float k = sel ? 0.92f + 0.03f * sinf(view_ticks * 0.3f) : 0.9f;
    gfx_pic(PIC_UI_ORANGE_BTN_BIG, 160, 196, k, k, 0, sel ? 256 : 200);
    title(FONT_MID, 160, 200, "CHANGE BIKE", WHITE, 1);
    if (sel) gfx_fill(108, 216, 104, 2, ORANGE, 256);
  }
}

/* ---- menus ---- */

static void title_screen(void) {
  gfx_fill(0, 0, SCREEN_W, SCREEN_H, BLACK, 256);
  gfx_pic(PIC_UI_0POPUP_BG, 230, 60, 0.6f, 0.7f, 0, 256);
  gfx_pic(PIC_UI_0POPUP_BG, 230, 190, 0.6f, 0.7f, 0, 256);
  gfx_pic(PIC_UI_LOGO, 92, 70, 1, 1, 0, 256);
  bike_pose(0, 92, 190, 0.9f);
  static const char *items[3] = {"PLAY", "BIKE SHOP", "HOW TO PLAY"};
  for (int i = 0; i < 3; i++) {
    bool sel = g.sel == i;
    float y = i == 0 ? 112 : i == 1 ? 160 : 196;
    title(i ? FONT_BIG : FONT_HUGE, 232, y, items[i], sel ? ORANGE : WHITE, 1);
    if (sel) gfx_fill(176, (int)y + 6, 112, 3, ORANGE, 256);
  }
  char s[16];
  snprintf(s, sizeof s, "%d", total_stars());
  gfx_pic(PIC_UI_W_STAR, 298, 18, 1, 1, 0, 256);
  gfx_text(FONT_MID, 284, 24, s, WHITE, BLACK, 1, 2);
}

static void map_screen(void) {
  const Pic *bg = pic(PIC_UI_MAPBG);
  /* keep the chosen level in view */
  float want = MAP_PTS[g.map_sel][0] * 0.5f - 160;
  float maxx = bg->w - SCREEN_W;
  if (want < 0) want = 0;
  if (want > maxx) want = maxx;
  if (gfx_y0 == 0) {
    g.map_x += (want - g.map_x) * 0.3f;
    if (fabsf(want - g.map_x) < 0.5f) g.map_x = want;
  }
  gfx_pic_at(PIC_UI_MAPBG, -floorf(g.map_x), 0, 1, 256);
  for (int i = 0; i < NLEVELS; i++) {
    float x = MAP_PTS[i][0] * 0.5f - floorf(g.map_x), y = MAP_PTS[i][1] * 0.5f;
    if (x < -30 || x > SCREEN_W + 30) continue;
    bool open = save.stars[i] != 0xFF, sel = i == g.map_sel;
    float k = sel ? 1.05f + 0.05f * sinf(view_ticks * 0.25f) : 1;
    gfx_pic(PIC_UI_MAP, x, y, k, k, 0, 256);
    gfx_pic(PIC_UI_MAP + (open ? save.stars[i] + 2 : 1), x, y, k, k, 0, 256);
    if (open) {
      char s[4];
      snprintf(s, sizeof s, "%02d", i + 1);
      gfx_text(FONT_MID, x, y + 2, s, sel ? ORANGE : WHITE, BLACK, 1, 1);
    }
  }
  /* the top bar: stars; the bottom: the keys */
  gfx_pic(PIC_UI_TOP_BG, 0, 0, 1, 1, 0, 256);
  char s[16];
  snprintf(s, sizeof s, "%d", total_stars());
  gfx_pic(PIC_UI_W_STAR, 18, 16, 0.8f, 0.8f, 0, 256);
  gfx_text(FONT_MID, 34, 22, s, WHITE, BLACK, 1, 0);
  gfx_fill(0, 222, SCREEN_W, 18, BLACK, 170);
  gfx_text(FONT_SMALL, 160, 235, "Arrows choose      OK ride      Back menu", WHITE, BLACK, 0, 1);
}

static void shop_screen(void) {
  gfx_fill(0, 0, SCREEN_W, SCREEN_H, BLACK, 256);
  gfx_pic(PIC_UI_0POPUP_BG, 160, 120, 0.95f, 1.1f, 0, 256);
  title(FONT_HUGE, 160, 40, "BIKE SHOP", ORANGE, 1);
  char s[24];
  snprintf(s, sizeof s, "%d", total_stars());
  gfx_pic(PIC_UI_W_STAR, 300, 18, 1, 1, 0, 256);
  gfx_text(FONT_MID, 286, 24, s, WHITE, BLACK, 1, 2);
  int k = g.shop_sel;
  bike_pose(k + 1, 160, 120, 1.2f);
  if (k > 0) gfx_pic(PIC_UI_ARROW_BTN, 22, 120, -1, 1, 0, 256);
  if (k < NSKINS - 1) gfx_pic(PIC_UI_ARROW_BTN, 298, 120, 1, 1, 0, 256);
  bool owned = save.owned & (1 << k), equipped = save.skin == k + 1;
  gfx_pic(equipped ? PIC_UI_ORANGE_BTN_BIG_GREEN : PIC_UI_ORANGE_BTN_BIG, 160, 214, 0.45f, 0.45f, 0, 256);
  title(FONT_MID, 160, 219, equipped ? "EQUIPPED" : "EQUIP IT", WHITE, 1);
  if (!owned) {
    int price = shop_prices()[k];
    if (price) {
      snprintf(s, sizeof s, "PRICE: %d", price);
      int w = gfx_text(FONT_MID, 152, 188, s, total_stars() >= price ? WHITE : GREY, BLACK, 1, 1);
      gfx_pic(PIC_UI_W_STAR, 152 + w / 2 + 12, 182, 0.7f, 0.7f, 0, 256);
    } else title(FONT_MID, 160, 188, "FREE", WHITE, 1);
  }
}

static void help_screen(void) {
  gfx_fill(0, 0, SCREEN_W, SCREEN_H, BLACK, 256);
  gfx_pic(PIC_UI_0POPUP_BG, 160, 124, 0.95f, 1.1f, 0, 256);
  title(FONT_BIG, 160, 34, "HOW TO PLAY", ORANGE, 1);
  static const char *keys[] = {"Up", "Down", "Left, Right", "Back", "OK"};
  static const char *what[] = {"gas", "brake", "lean, flip in the air", "pause", "go on after a crash"};
  for (int i = 0; i < 5; i++) {
    gfx_text(FONT_SMALL, 150, 66 + i * 17, keys[i], ORANGE, BLACK, 1, 2);
    gfx_text(FONT_SMALL, 162, 66 + i * 17, what[i], WHITE, BLACK, 1, 0);
  }
  gfx_text(FONT_SMALL, 160, 168, "Each flip takes half a second off your time.", WHITE, BLACK, 1, 1);
  gfx_text(FONT_SMALL, 160, 184, "Beat a level's times for stars.", WHITE, BLACK, 1, 1);
  gfx_text(FONT_SMALL, 160, 198, "More stars open new bikes.", WHITE, BLACK, 1, 1);
}

void ui_draw(void) {
  switch (g.state) {
  case S_TITLE: title_screen(); break;
  case S_HELP: help_screen(); break;
  case S_MAP: map_screen(); break;
  case S_SHOP: shop_screen(); break;
  default:
    view_draw_world();
    if (g.state == S_PLAY) hud();
    else if (g.state == S_PAUSE) pause_menu();
    else if (g.state == S_FINISH) finish_menu();
    break;
  }
}
