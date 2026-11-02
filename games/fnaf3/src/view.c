/* Five Nights at Freddy's 3: drawing the night. The office is the game's at half size (600 x 240) and the
 * calculator shows 320 of it around the game's view; what changes in it (the window, the left door, the phantoms
 * standing in it) are patches tools/pack.py made from the game's pictures. The monitor is the camera feed with the
 * map over it, the maintenance panel its own picture with the menu's words. */
#include <stdio.h>
#include "game.h"
#include "gfx.h"
#include "data.h"

#define N g.n
#define GREY RGB(0xB0B0B0)
#define GREEN RGB(0x4CD964)
#define RED RGB(0xE03030)
#define BLUE RGB(0x4A90E2)

static int pan;   /* the office's left edge on the screen */

/* a picture placed in the office at (ox, oy), as much of it as is on the screen */
static void office_part(int id, int ox, int oy, int w, int h) {
  int x = ox - pan, sx = 0;
  if (x < 0) sx = -x, w += x, x = 0;
  if (x + w > SCREEN_W) w = SCREEN_W - x;
  if (w > 0) pic_part(id, x, oy, sx, 0, w, h);
}

static int seq(int t, int len, int n) { /* frame t of len, as one of n pictures */
  int k = t * n / (len > 0 ? len : 1);
  return k < 0 ? 0 : k >= n ? n - 1 : k;
}

static void office(void) {
  int xc = N.got_you == 2 ? XMAX : N.x;
  pan = (xc - 320) / 2;
  if (N.frozen) pan += (int)rnd_n(7) - 3;   /* (the view shakes during a scare) */
  pan = pan < 0 ? 0 : pan > 600 - SCREEN_W ? 600 - SCREEN_W : pan;
  pic_part(N.red_on ? PIC_OFFICE_RED : PIC_OFFICE, 0, 0, pan, 0, SCREEN_W, SCREEN_H);
  /* the window */
  int win = -1;
  if (N.window) win = PIC_WIN_SPRING;
  else if (N.ran_past && N.ran_past < 255) win = PIC_WIN_RUN0 + seq(N.ran_past - 1, 31, RUN_N);
  else if (N.mangle_win) {
    int t = N.mangle_win, i = t < 160 ? t * 10 / 160 : t < 190 ? 10 : 10 - (t - 190) * 10 / 16;
    win = PIC_WIN_MANGLE0 + (i < 0 ? 0 : i);
  } else if (N.flunge) win = PIC_WIN_FLUNGE0 + seq(N.flunge - 1, 28, FLUNGE_N);
  else if (N.fwalk) {
    int x = 1011 - N.fwalk_t * 3 / 8, i = (FREDDY_X0 - x) / 8;
    if (i >= 0 && i < FREDDY_N) win = PIC_WIN_FREDDY0 + i;
  }
  if (win >= 0) office_part(win, WIN_X, WIN_Y, WIN_W, WIN_H);
  /* the left door */
  if (N.head) office_part(PIC_DOOR_HEAD, DOOR_X, DOOR_Y, DOOR_W, DOOR_H);
  else if (N.hid && N.hid < 255) office_part(PIC_DOOR_HIDE0 + seq(N.hid - 1, 27, HIDE_N), DOOR_X, DOOR_Y, DOOR_W, DOOR_H);
  /* the phantoms in the office */
  if (N.scare == PH_BB)
    office_part(PIC_BB0 + (N.scare_t < 30 ? (N.scare_t / 15) % 2 : 2 + seq(N.scare_t - 30, 17, BB_N - 2)), BB_X, BB_Y, BB_W, BB_H);
  if (N.chica_in == 1) office_part(PIC_CHICA0, CHICA_X, CHICA_Y, CHICA_W, CHICA_H);
  if (N.scare == PH_CHICA) office_part(PIC_CHICA0 + 1 + seq(N.scare_t, 20, CHICA_N - 1), CHICA_X, CHICA_Y, CHICA_W, CHICA_H);
  if (N.foxy && N.scare != PH_FOXY) office_part(PIC_FOXY0, FOXY_X, FOXY_Y, FOXY_W, FOXY_H);
  if (N.scare == PH_FOXY) office_part(PIC_FOXY0 + 1 + seq(N.scare_t, 23, FOXY_N - 1), FOXY_X, FOXY_Y, FOXY_W, FOXY_H);
}

/* ------------------------------------------------------------ the monitor */
static const int16_t BX[16] = {0, BTN01_X, BTN02_X, BTN03_X, BTN04_X, BTN05_X, BTN06_X, BTN07_X, BTN08_X, BTN09_X,
                               BTN10_X, BTN11_X, BTN12_X, BTN13_X, BTN14_X, BTN15_X};
static const int16_t BY[16] = {0, BTN01_Y, BTN02_Y, BTN03_Y, BTN04_Y, BTN05_Y, BTN06_Y, BTN07_Y, BTN08_Y, BTN09_Y,
                               BTN10_Y, BTN11_Y, BTN12_Y, BTN13_Y, BTN14_Y, BTN15_Y};
static const int16_t SX[5] = {SEAL11_X, SEAL12_X, SEAL13_X, SEAL14_X, SEAL15_X};
static const int16_t SY[5] = {SEAL11_Y, SEAL12_Y, SEAL13_Y, SEAL14_Y, SEAL15_Y};

static int feed_pic(int a, int k) {
  switch (a) {
#define F(n) case n: return PIC_FEED##n##_0 + k % FEED##n##_N;
    F(12) F(13) F(14) F(15) F(16) F(17) F(21) F(23) F(24) F(25) F(26) F(27) F(28) F(29) F(30) F(31) F(32) F(33)
    F(34) F(35) F(43) F(45) F(46) F(47) F(48) F(49) F(50) F(51) F(52) F(53) F(54) F(55) F(56) F(57)
#undef F
  }
  return PIC_FEED17_0;
}

static void monitor(void) {
  int c = N.vents_map ? N.vent : N.cam;
  gfx_fill(0, 0, SCREEN_W, SCREEN_H, BLACK);
  bool video = N.camera > -10;
  if (video) {
    if (N.bb_shown) pic_draw(PIC_BBPEEK, 8, 0);
    else pic_draw(feed_pic(feed_anim(c), (int)(N.frame / 8)), 8, 0);
  }
  int s0 = (int)rnd_n(30) + N.static_s1 + N.static_s4 + 30, op = s0 - 45;
  op = op < 0 ? 0 : op > 255 ? 255 : op;
  gfx_static(8, 0, 304, 240, video ? op * 32 / 255 : 24, N.frame);
  gfx_img(N.vents_map ? IMG_VENTMAP : IMG_MAP, MAP_X, MAP_Y, N.vents_map ? BLUE : GREY);
  int lo = N.vents_map ? 11 : 1, hi = N.vents_map ? 15 : 10;
  for (int i = lo; i <= hi; i++) {
    int x = BX[i] - 11, y = BY[i] - 7;
    gfx_fill(x, y, 22, 15, i == c ? GREEN : RGB(0x505050));
    gfx_frame_rect(x, y, 22, 15, WHITE);
    char t[4];
    snprintf(t, sizeof t, "%02d", i);
    gfx_text_c(FONT_HUD, BX[i], y + 1, t, WHITE);
  }
  if (N.vents_map)
    for (int i = 0; i < 5; i++) gfx_fill(SX[i] - 6, SY[i] - 4, 12, 8, N.closed == 11 + i ? RED : GREEN);
  if ((g.cheats & CH_RADAR) && spring_cam()) {
    int s = spring_cam();
    if ((s >= 11) == (bool)N.vents_map) gfx_fill(BX[s] - 3, BY[s] - 3, 6, 6, RED);
  }
  if (N.sdfg && !N.vents_map) {   /* the lure's sound wave */
    int r = 4 + (133 - N.sdfg) % 20;
    gfx_frame_rect(BX[N.lure_cam] - r, BY[N.lure_cam] - r / 2, 2 * r, r, WHITE);
  }
  char cam[8];
  snprintf(cam, sizeof cam, "CAM %02d", c);
  gfx_text_s(FONT_BIG, 14, 6, cam, WHITE);
  int y = 30;
  if (N.audio <= -10) gfx_img(IMG_M_AUDIO, 14, y, RED), y += gfx_img_h(IMG_M_AUDIO) + 2;
  if (!video) gfx_img(IMG_M_VIDEO, 14, y, RED), y += gfx_img_h(IMG_M_VIDEO) + 2;
  if (N.vent_h <= -10) gfx_img(IMG_M_VENT, 14, y, RED);
  if (N.sealing) gfx_text_s(FONT_HUD, MAP_X, MAP_Y - 14, "SEALING...", WHITE);
  if (!N.vents_map && N.play < 7) {
    char p[4];
    snprintf(p, sizeof p, "%d", N.play);
    gfx_text_s(FONT_BIG, MAP_X, MAP_Y - 20, p, WHITE);
  }
}

static void panel(void) {
  int y0 = 0;
  if (N.panel == 1) y0 = SCREEN_H - N.panel_anim * SCREEN_H / 10;
  if (N.panel_dropping) y0 = N.panel_anim * SCREEN_H / 8;
  if (y0 > 0) office();
  if (y0 >= SCREEN_H) return;
  pic_part(PIC_PANEL, 0, y0, 0, 0, SCREEN_W, SCREEN_H - y0);
  static const uint8_t item_y[5] = {66, 96, 123, 176, 201};
  static const uint8_t ids[5] = {IMG_P_AUDIO, IMG_P_CAMERA, IMG_P_VENT, IMG_P_ALL, IMG_P_EXIT};
  for (int i = 0; i < 5; i++) {
    int y = y0 + item_y[i];
    gfx_img(ids[i], 40, y, WHITE);
    bool bad = (i == 0 && N.audio <= -10) || (i == 1 && N.camera <= -10) || (i == 2 && N.vent_h <= -10);
    if (bad && N.rebooting != i + 1) gfx_img(IMG_P_ERROR, 210, y, RED);
    if (N.rebooting == i + 1) gfx_img(IMG_P_DOTS0 + (int)(N.frame / 12) % DOTS_N, 210, y, WHITE);
    if (N.cursor == i + 1) gfx_img(IMG_P_CURSOR, 4, y, WHITE);
  }
}

/* the hour, the night, how close he is (the game's hum, shown) */
static void hud(void) {
  char t[12];
  if (N.hour == 0) snprintf(t, sizeof t, "12 AM");
  else snprintf(t, sizeof t, "%d AM", N.hour);
  gfx_text_r(FONT_CLOCK, SCREEN_W - 6, 2, t, WHITE);
  snprintf(t, sizeof t, "Night %d", N.night);
  gfx_text_r(FONT_TEXT, SCREEN_W - 6, 30, t, WHITE);
  for (int i = 0; i < 2; i++)
    gfx_fill(6 + 7 * i, 18 - 6 * i, 5, 6 + 6 * i, i < N.nearby ? (N.nearby == 2 ? RED : RGB(0xE0A030)) : RGB(0x303030));
}

void view_night(void) {
  if (N.got_you && N.death_t > 0) {
    int k = seq(N.death_t, 37, JUMP_N);
    pic_draw((N.got_you == 1 ? PIC_JUMPL0 : PIC_JUMPR0) + k, 0, 0);
    return;
  }
  if (N.scare == PH_FREDDY) {
    gfx_fill(0, 0, SCREEN_W, SCREEN_H, BLACK);
    pic_draw(PIC_FSCARE0 + seq(N.scare_t, 17, FSCARE_N), 0, 0);
    return;
  }
  if (N.viewing >= 2) monitor();
  else if (N.viewing == 1) gfx_static(0, 0, SCREEN_W, SCREEN_H, 28, N.frame);
  else if (N.panel) panel();
  else office();
  if (N.head_t > 0 || N.head_out) {
    int id = N.head_t > 0 ? IMG_PHEAD1 + N.head_anim : IMG_PHEAD0;
    gfx_img(id, N.head_x - gfx_img_w(id) / 2, SCREEN_H - gfx_img_h(id), RGB(0xE8E8E8));
  }
  if (N.black > 0) gfx_dim(0, 0, SCREEN_W, SCREEN_H, (N.black > 255 ? 255 : N.black) * 32 / 255);
  if (N.flash > 0) gfx_blend(0, 0, SCREEN_W, SCREEN_H, WHITE, N.flash * 32 / 51);
  if (N.viewing < 2 && !N.panel) hud();
}

void view_scare(void) { view_night(); }
