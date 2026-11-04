/* Drawing a night on the CE: the office (turning, its lights, doors, buttons and fan), the monitor and its
 * cameras (the feed's slow turn, its static, the map), and around them the clock, the power, the call's subtitles
 * and the captions of what can be heard. As the NumWorks version's view.c, with the CE's pictures: the office and
 * the cameras at half height in RAM (scene), drawn with each row twice. */
#include <stddef.h>
#include <string.h>
#include "fnaf.h"

#define N g.n

/* the office's small pictures, decoded once */
#define DOOR_LW 72
#define DOOR_RW 64
#define PATCH_MAX (LIGHT_R_W * LIGHT_R_H > LIGHT_L_W * LIGHT_L_H ? LIGHT_R_W * LIGHT_R_H : LIGHT_L_W * LIGHT_L_H)
static uint8_t door_l[DOOR_LW * 120], door_r[DOOR_RW * 120];
static uint8_t panel_l[4][32 * 40], panel_r[4][48 * 40], fans[3][48 * 24];
static uint8_t patch[PATCH_MAX > DARK_FREDDY_W * DARK_FREDDY_H ? PATCH_MAX : DARK_FREDDY_W * DARK_FREDDY_H];
static bool office_ready;
static int patch_pic = -1;

void view_reset(void) { patch_pic = -1; }

static void want_patch(int pic, int w) {
  if (patch_pic == pic) return;
  int base = pic_base(asset(PIC_FIRST + pic));
  if (base >= 0 && patch_pic != base) pic_into(base, patch, w);
  pic_into(pic, patch, w);
  patch_pic = pic;
}


static void office_pictures(void) {
  if (office_ready) return;
  pic_full(PIC_DOOR_L, door_l, DOOR_LW);
  pic_full(PIC_DOOR_R, door_r, DOOR_RW);
  for (int k = 0; k < 4; k++) {
    pic_full(PIC_PANEL_L0 + k, panel_l[k], 32);
    pic_full(PIC_PANEL_R0 + k, panel_r[k], 48);
  }
  for (int k = 0; k < 3; k++) pic_full(PIC_FAN0 + k, fans[k], 48);
  office_ready = true;
}

/* chosen once a frame */
static struct {
  bool light_off;    /* the door light flickers */
  bool hall_lit;     /* 2A's light flickers on */
  int fan;
  int halluc;        /* 0, or what flashes: 1 Golden Freddy, 2 the endoskeleton, 3 IT'S ME */
  int freddy_face;   /* in the dark */
  int noise, shake;
  uint32_t seed;
} f;

static void prepare(void) {
  f.light_off = rnd() % 10 <= 1;
  f.hall_lit = rnd() % 10 < 3;
  f.fan = (int)(N.t * 30) % 3;
  f.halluc = N.halluc > 0 && rnd() % 10 == 0 ? 1 + (int)(rnd() % 3) : 0;
  f.freddy_face = rnd() % 4 == 0;
  f.noise = 4 + (int)(rnd() % 4) + (rnd() % 16 == 0 ? 6 : 0);
  f.shake = (int)(rnd() % 7) - 3;
  f.seed = rnd();
}

/* the static's four phases, from its strength (of 32): each a share of it, toward a grey of its own (the
 * NumWorks version's static: each pixel toward a random grey, 0 to 143) */
const uint8_t *static_phases(int a) {
  static uint8_t ph[8];
  for (int k = 0; k < 4; k++) {
    uint32_t r = rnd();
    ph[k] = (uint8_t)(a * (int)(r % 5) / 4);
    ph[4 + k] = (uint8_t)(r >> 8 & 15);
  }
  return ph;
}

static const uint8_t GREYS[8] = {UI_BLACK, UI_WHITE1, UI_GREY1, UI_SPARE, UI_DIM, UI_GREY2, UI_EDGE, UI_BOX};
static const uint8_t DARK_GREYS[8] = {UI_BLACK, UI_BLACK, UI_BOX, UI_BLACK, UI_EDGE, UI_BLACK, UI_BOX, UI_GREY1};

/* ------------------------------------------------------------------ text helpers */
static void small_digits(int font, int x, int y, int v, Ink ink) {
  char s[4];
  int n = 0;
  if (v >= 100) s[n++] = (char)('0' + v / 100);
  if (v >= 10) s[n++] = (char)('0' + v / 10 % 10);
  s[n++] = (char)('0' + v % 10);
  s[n] = 0;
  d_text(font, x, y, s, ink);
}

/* a 3 x 5 font for the map's buttons: 1 to 7, A, B, C */
static const uint16_t tiny[10] = {0x2C97, 0x73E7, 0x73CF, 0x5BC9, 0x79CF, 0x79EF, 0x7249, 0x2BED, 0x6BAE, 0x3923};
static void tiny_text(int x, int y, const char *s, uint8_t c) {
  for (; *s; s++, x += 4) {
    int k = *s >= '1' && *s <= '7' ? *s - '1' : *s - 'A' + 7;
    uint16_t b = tiny[k];
    for (int r = 0; r < 5; r++)
      for (int col = 0; col < 3; col++)
        if (b >> (14 - r * 3 - col) & 1) scr[(y + r) * SCR_W + x + col] = c;
  }
}

/* wraps s to lines of at most w pixels: the start and length of line k, false past the last */
static bool wrap(int font, const char *s, int w, int k, const char **start, int *len) {
  for (;;) {
    while (*s == ' ') s++;
    if (!*s) return false;
    int n = 0, last_space = -1, width = 0;
    while (s[n]) {
      int cw = d_text_wn(font, s + n, 1);
      if (width + cw > w) break;
      if (s[n] == ' ') last_space = n;
      width += cw;
      n++;
    }
    if (s[n] && last_space > 0) n = last_space;
    if (!k--) {
      *start = s, *len = n;
      return true;
    }
    s += n;
  }
}

static void text_shadow(int font, int x, int y, const char *s, int n, Ink ink) {
  d_text_n(font, x + 1, y + 1, s, n, INK_BLACK);
  d_text_n(font, x, y, s, n, ink);
}

/* ------------------------------------------------------------------ around the view */
static void clock_and_night(void) {
  char h[3] = {'1', '2', 0};   /* 12 AM, then 1 to 5 */
  if (N.hour) h[0] = (char)('0' + N.hour), h[1] = 0;
  int x = 310 - d_text_w(FONT_BIG, " AM") - d_text_w(FONT_CLOCK, h);
  x += d_text(FONT_CLOCK, x, 6, h, INK_WHITE);
  d_text(FONT_BIG, x, 8, " AM", INK_WHITE);
  int nx = 310 - d_text_w(FONT_HUD, "Night ") - d_text_w(FONT_PIXEL, "0");
  nx += d_text(FONT_HUD, nx, 31, "Night ", INK_WHITE);
  small_digits(FONT_PIXEL, nx, 30, N.night, INK_WHITE);
}

static void power_hud(void) {
  int x = 8 + d_text(FONT_HUD, 8, 206, "Power left: ", INK_WHITE);
  int p = N.power / 10;
  small_digits(FONT_PIXEL, x, 205, p, INK_WHITE);
  d_text(FONT_HUD, x + d_text_w(FONT_PIXEL, p >= 10 ? "00" : "0") + 1, 206, "%", INK_WHITE);
  x = 8 + d_text(FONT_HUD, 8, 222, "Usage: ", INK_WHITE);
  static const uint8_t bars[5] = {UI_BAR_G, UI_BAR_G, UI_BAR_Y, UI_BAR_R, UI_BAR_R};
  static const uint8_t lit[5] = {UI_BAR_GL, UI_BAR_GL, UI_BAR_YL, UI_BAR_RL, UI_BAR_RL};
  for (int i = 0; i < N.usage && i < 5; i++) {
    d_fill(x + 9 * i, 222, 7, 12, bars[i]);
    d_fill(x + 9 * i + 1, 223, 5, 2, lit[i]);
  }
}

static void call_hud(int grp) {
  if (N.night > 5 || N.call_muted || N.t >= call_end(N.night)) return;
  if (N.t > 3) {
    d_remap(6, 6, 74, 15, map_remap(grp, 0));
    d_frame(6, 6, 74, 15, UI_SILVER);
    d_text(FONT_HUD, 12, 8, "MUTE CALL", INK_WHITE);
    d_text(FONT_TEXT, 84, 8, "0", INK_DIM);
  }
  const char *s = call_line(N.night, N.t);
  if (!s) return;
  const char *line;
  int len, n = 0;
  while (n < 3 && wrap(FONT_TEXT, s, 300, n, &line, &len)) n++;
  int y = 52;
  d_remap(4, y - 2, 312, 14 * n + 4, map_remap(grp, 0));
  for (int k = 0; k < n; k++) {
    wrap(FONT_TEXT, s, 300, k, &line, &len);
    int w = d_text_wn(FONT_TEXT, line, len);
    text_shadow(FONT_TEXT, 160 - w / 2, y + 14 * k, line, len, INK_CALL);
  }
}

void view_captions(void) {
  int y = 186;
  for (int i = 3; i >= 0; i--) {
    if (!g.cap[i] || g.cap_t[i] <= 0) continue;
    int a = g.cap_t[i] > 0.5f ? 32 : (int)(g.cap_t[i] * 64);
    d_text(FONT_TEXT, 9, y + 1, g.cap[i], INK_BLACK);
    d_text(FONT_TEXT, 8, y, g.cap[i], ink_fade(INK_CAPTION, a));
    y -= 13;
  }
}

/* the bar at the bottom to bring the monitor up or down */
static void flip_bar(int grp) {
  d_remap(118, 229, 84, 9, map_remap(grp, 1));
  d_frame(118, 229, 84, 9, UI_SPARE);
  for (int k = 0; k < 2; k++)
    for (int i = 0; i < 4; i++) {
      scr[(235 - i) * SCR_W + 152 + 9 * k + i] = UI_WHITE;
      scr[(235 - i) * SCR_W + 152 + 9 * k + 7 - i] = UI_WHITE;
    }
}

/* ------------------------------------------------------------------ the office */
int office_pan(void) { return (int)N.pan; }

static int office(void) {
  int px = (int)N.pan;
  if (N.dark) {
    if (N.dark == 3 && (N.dark_t > 0.35f || (int)(N.dark_t * 30) & 1)) {
      d_fill(0, 0, SCR_W, SCR_H, UI_BLACK);
      return pic_group_of(PIC_DARK);
    }
    scene_want(PIC_DARK);
    d_blit(scene, SCENE_W, px, 0, SCR_W, SCENE_H, 0, 0, true);
    if (N.dark == 2 && f.freddy_face) {
      want_patch(PIC_DARK_FREDDY, DARK_FREDDY_W);
      d_blit(patch, DARK_FREDDY_W, 0, 0, DARK_FREDDY_W, DARK_FREDDY_H, DARK_FREDDY_X - px, 2 * DARK_FREDDY_Y, true);
    }
    return pic_group_of(PIC_DARK);
  }
  office_pictures();
  scene_want(PIC_OFFICE);
  d_blit(scene, SCENE_W, px, 0, SCR_W, SCENE_H, 0, 0, true);
  if (N.light == 1 + LEFT && !f.light_off) {
    want_patch(N.pos[BONNIE] == AT_DOOR ? PIC_LIGHT_L_WHO : PIC_LIGHT_L, LIGHT_L_W);
    d_blit(patch, LIGHT_L_W, 0, 0, LIGHT_L_W, LIGHT_L_H, LIGHT_L_X - px, 2 * LIGHT_L_Y, true);
  }
  if (N.light == 1 + RIGHT && !f.light_off) {
    want_patch(N.pos[CHICA] == AT_DOOR ? PIC_LIGHT_R_WHO : PIC_LIGHT_R, LIGHT_R_W);
    d_blit(patch, LIGHT_R_W, 0, 0, LIGHT_R_W, LIGHT_R_H, LIGHT_R_X - px, 2 * LIGHT_R_Y, true);
  }
  d_blit(fans[f.fan], 48, 0, 0, 48, 24, FAN_X - px, FAN_Y, true);
  d_blit(panel_l[N.door[LEFT] | (N.light == 1 + LEFT) << 1], 32, 0, 0, 26, 40, PANEL_L_X - px, PANEL_Y, true);
  d_blit(panel_r[N.door[RIGHT] | (N.light == 1 + RIGHT) << 1], 48, 0, 0, 48, 40, PANEL_R_X - px, PANEL_Y, true);
  for (int s = 0; s < 2; s++) {
    int vis = (int)(N.door_anim[s] * 120 + 0.5f);
    if (vis > 0)
      d_blit(s ? door_r : door_l, s ? DOOR_RW : DOOR_LW, 0, 120 - vis, s ? DOOR_R_W : DOOR_L_W, vis,
             (s ? DOOR_R_X : DOOR_L_X) - px, 0, true);
  }
  if (N.gold == 3) {
    extra_want(PIC_OFFICE_GOLD, GOLD_W);
    d_blit(extra, GOLD_W, 0, 0, GOLD_W, GOLD_H / 2, GOLD_X - px, GOLD_Y, true);
  }
  return pic_group_of(PIC_OFFICE);
}

/* ------------------------------------------------------------------ the cameras */
static const char *const cam_names[CAM_COUNT] = {"Show Stage", "Dining Area", "Pirate Cove", "West Hall",
                                                 "W. Hall Corner", "Supply Closet", "East Hall", "E. Hall Corner",
                                                 "Backstage", "Kitchen", "Restrooms"};
static const char *const cam_labels[CAM_COUNT] = {"1A", "1B", "1C", "2A", "2B", "3", "4A", "4B", "5", "6", "7"};

static bool here(int who, int cam) { return N.pos[who] == cam; }

static int cam_picture(void) {
  bool b = here(BONNIE, N.cam), c = here(CHICA, N.cam), fr = here(FREDDY, N.cam);
  switch (N.cam) {
  case CAM_1A:
    if (!fr) return PIC_CAM1A_EMPTY;
    return b && c ? PIC_CAM1A_BCF : b ? PIC_CAM1A_BF : c ? PIC_CAM1A_CF : PIC_CAM1A_F;
  case CAM_1B: return PIC_CAM1B_EMPTY + (b && c && fr ? 7 : b && c ? 4 : b && fr ? 5 : c && fr ? 6 : b ? 1 : c ? 2 : fr ? 3 : 0);
  case CAM_1C: return PIC_CAM1C_0 + (N.foxy < 3 ? N.foxy : 3);
  case CAM_2A: return !f.hall_lit ? PIC_CAM2A_DARK : b ? PIC_CAM2A_B : PIC_CAM2A_EMPTY;
  case CAM_2B: return (N.gold == 1 || N.gold == 2) && !b ? PIC_CAM2B_GOLD : b ? PIC_CAM2B_B : PIC_CAM2B_EMPTY;
  case CAM_3: return b ? PIC_CAM3_B : PIC_CAM3_EMPTY;
  case CAM_4A: return c && fr ? PIC_CAM4A_CF : c ? PIC_CAM4A_C : fr ? PIC_CAM4A_F : PIC_CAM4A_EMPTY;
  case CAM_4B: return c ? PIC_CAM4B_C : fr ? PIC_CAM4B_F : PIC_CAM4B_EMPTY;
  case CAM_5: return b ? PIC_CAM5_B : PIC_CAM5_EMPTY;
  case CAM_7: return c && fr ? PIC_CAM7_CF : c ? PIC_CAM7_C : fr ? PIC_CAM7_F : PIC_CAM7_EMPTY;
  }
  return -1;
}

static void camera_map(void) {
  const uint8_t *m = asset(ASSET_CAMMAP);
  int size = m[0], runs = m[1] | m[2] << 8;
  int mx = SCR_W - size - 4, my = SCR_H - size - 12;
  const uint8_t *r = m + 3;
  for (int i = 0; i < runs; i++, r += 3) memset(scr + (my + r[0]) * SCR_W + mx + r[1], UI_WHITE, r[2]);
  bool blink = !((int)(g.t * 3) & 1);
  for (int k = 0; k < CAM_COUNT; k++) {
    int cx = mx + r[2 * k], cy = my + r[2 * k + 1];
    bool sel = k == N.cam;
    d_fill(cx - 8, cy - 5, 17, 11, sel && blink ? UI_MAP_ON : UI_MAP_OFF);
    d_frame(cx - 8, cy - 5, 17, 11, UI_MAP_LINE);
    tiny_text(cx - (cam_labels[k][1] ? 3 : 1), cy - 2, cam_labels[k], UI_WHITE);
  }
  d_text(FONT_HUD, mx - 2, my - 18, cam_names[N.cam], INK_WHITE);
}

static int cameras(bool paused) {
  float c = N.cam_t;   /* 5 seconds there, 2 still, 5 back, 2 still */
  float a = c < 5 ? c / 5 : c < 7 ? 1 : c < 12 ? 1 - (c - 7) / 5 : 0;
  int px = (int)(a * CAM_PAN), grp = -1, noise = f.noise;
  if (N.glitch > 0) {
    d_noise(0, 0, SCR_W, SCR_H, f.seed, GREYS);
  } else if (N.cam == CAM_6) {
    d_noise(0, 0, SCR_W, SCR_H, f.seed, DARK_GREYS);
    d_text_c(FONT_BIG, 100, 64, "CAMERA DISABLED", INK_WHITE);
    d_text_c(FONT_BIG, 100, 86, "-AUDIO ONLY-", INK_WHITE);
  } else if (N.cam == CAM_2A && N.foxy_run >= 0 && N.foxy_run < 35) {
    int pic = PIC_FOXYRUN0 + (int)(N.foxy_run / 35 * FOXYRUN_N);
    scene_want(pic);
    d_blit(scene, SCENE_W, CAM_PAN, 0, SCR_W, SCENE_H, 0, 0, true);
    grp = pic_group_of(pic);
  } else {
    int pic = cam_picture();
    scene_want(pic);
    d_blit(scene, SCENE_W, px, 0, SCR_W, SCENE_H, 0, 0, true);
    grp = pic_group_of(pic);
  }
  if (grp < 0) grp = pic_group_of(PIC_CAM1A_EMPTY);   /* (no picture shows: its colours unused) */
  pal_set(grp, paused ? 9 : 32, paused ? NULL : static_phases(noise),
          g.blip > 0 && !paused ? (int)(g.blip * 120) : 0);
  d_frame(5, 5, 310, 230, UI_FRAME);
  if ((int)(g.t * 1.5f) & 1) {   /* the recording dot */
    static const uint8_t dot[7] = {2, 1, 0, 0, 0, 1, 2};
    for (int r = 0; r < 7; r++) d_fill(14 + dot[r], 26 + r, 7 - 2 * dot[r], 1, UI_REC);
  }
  if (!paused) camera_map();
  return grp;
}

/* ------------------------------------------------------------------ the night */
static int hallucination(int grp) {
  if (!f.halluc) return grp;
  if (f.halluc == 3) {
    d_fill(0, 0, SCR_W, SCR_H, UI_BLACK);
    d_text_c(FONT_HUGE, 160, 104, "IT'S ME", INK_WHITE);
    return grp;
  }
  int pic = f.halluc == 1 ? PIC_HALLUC_GOLDEN : PIC_HALLUC_ENDO;
  extra_want(pic, 160);
  d_blit2(extra, 160, 0, UI_BLACK);
  return pic_group_of(pic);
}

/* the night: paused, the picture only, darker */
static void night_draw(bool paused) {
  bool cams = N.flip >= 1 && !N.dark;
  int grp;
  if (cams) {
    grp = cameras(paused);
  } else {
    grp = office();
    if (N.flip > 0) {   /* the monitor coming up, or down */
      int k = (int)(N.flip * FLIP_N);
      k = k < FLIP_N ? k : FLIP_N - 1;
      extra_want(PIC_FLIP0 + k, 160);
      d_blit2(extra, 160, 0, UI_BLACK);
      grp = pic_group_of(PIC_FLIP0 + k);
    }
    pal_set(grp, paused ? 9 : 32, NULL, 0);
  }
  if (paused) return;
  int shown = hallucination(grp);
  if (shown != grp) pal_set(shown, 32, NULL, 0);
  if (!N.dark) {
    clock_and_night();
    power_hud();
    call_hud(shown);
    if (!cams || N.flip >= 1) flip_bar(shown);
  }
  view_captions();
}

void view_night(void) {
  prepare();
  night_draw(false);
}

void view_paused(void) { night_draw(true); }

/* ------------------------------------------------------------------ jumpscares */
void view_scare(void) {
  static const int first[5] = {PIC_BONNIE0, PIC_CHICA0, PIC_FREDDY0, PIC_FOXY0, PIC_POWEROUT0};
  static const int count[5] = {BONNIE_N, CHICA_N, FREDDY_N, FOXY_N, POWEROUT_N};
  static const float fps[5] = {30, 30, 25, 25, 30};
  static const bool loops[5] = {true, true, false, false, false};
  prepare();
  int w = g.scare, k = (int)(g.scare_t * fps[w]);
  k = loops[w] ? k % count[w] : k < count[w] ? k : count[w] - 1;
  int dx = g.scare_t > 0.1f ? f.shake : 0;
  scene_want(first[w] + k);
  d_blit2(scene, SCENE_W, dx, UI_BLACK);
  pal_set(pic_group_of(first[w]), 32, NULL, 0);
}
