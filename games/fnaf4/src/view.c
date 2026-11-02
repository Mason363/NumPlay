/* Five Nights at Freddy's 4: drawing the night. Each of the game's views fills its screen (the bedroom, a door, the
 * closet, the bed, a scare), so the one made last is the one seen: the bedroom pans with where you look (the game's
 * 1000-wide room at half size, 320 of it), the others are the middle 640 of the game's 800 at half size. Over them:
 * the dark flickering in, the night's start from black, the hour, the first night's warning at the doors. */
#include <stdio.h>
#include "game.h"
#include "gfx.h"
#include "data.h"

#define N g.n

static void draw_ob(int o) {
  if (ob_wide(o)) {
    int x = N.x < 400 ? 400 : N.x > 600 ? 600 : N.x;
    pic_part(ob_pic(o), 0, 0, x / 2 - 160 - BED_X0, 0, SCREEN_W, SCREEN_H);
  } else
    pic_draw(ob_pic(o), 0, 0);
}

static void hud(void) {
  char t[4];
  snprintf(t, sizeof t, "%d", N.hour);
  int w = gfx_img_w(IMG_AM);
  gfx_text_r(FONT_CLOCK, SCREEN_W - 10 - w, 4, t, WHITE);
  gfx_img(IMG_AM, SCREEN_W - 6 - w, 11, WHITE);
}

void view_night(void) {
  int top = night_top();
  if (top < 0) gfx_fill(0, 0, SCREEN_W, SCREEN_H, BLACK);
  else draw_ob(top);
  bool scare = top >= O_JUMP;
  if (N.flash_vis && !scare) {
    int a = 255 - (N.bf_alpha < 0 ? 0 : N.bf_alpha > 255 ? 255 : N.bf_alpha);
    gfx_dim(0, 0, SCREEN_W, SCREEN_H, a * 32 / 255);
  }
  if (N.fade < 255) gfx_dim(0, 0, SCREEN_W, SCREEN_H, (255 - N.fade) * 32 / 255);
  if (N.warning && (N.s0 == 10 || N.s0 == 17))
    gfx_img(IMG_WARNING, SCREEN_W / 2 - gfx_img_w(IMG_WARNING) / 2, 54 - gfx_img_h(IMG_WARNING) / 2, RGB(0xB8B8B8));
  if (!scare) hud();
}

void view_plush(void) {
  pic_draw(plush_pic(), 0, 0);
  if (g.p.s0 == 98) gfx_img(IMG_GREAT, SCREEN_W / 2 - gfx_img_w(IMG_GREAT) / 2, 88, WHITE);
  if (g.p.frame > 6 && g.p.counter > 0) {
    char t[8];
    snprintf(t, sizeof t, "%d", g.p.counter);
    gfx_text_r(FONT_CLOCK, SCREEN_W - 10, 30, t, WHITE);
  }
}
