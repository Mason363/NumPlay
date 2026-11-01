/* Drawing a night: the office (turning, the hallway and the vents in the light, who is in the room, the mask),
 * the monitor and its cameras (the feeds, their static and light, the map, the music box), and around them the
 * clock, the flashlight's battery, the music box's warning and the captions of what can be heard. */
#include <stddef.h>
#include "game.h"
#include "gfx.h"
#include "res.h"

#define N g.n
#define GREY RGB(0xB8B8B8)

/* chosen once a frame (the bands of a frame must agree) */
static struct {
  bool flick_off;    /* the lights flickering: this frame dark */
  int noise;
  uint32_t seed;
  int shake;
} f;

static void prepare(void) {
  f.flick_off = N.flicker > 0 && rnd() % 3 == 0;
  f.noise = 4 + (int)(rnd() % 4) + (rnd() % 16 == 0 ? 6 : 0);
  f.seed = rnd();
  f.shake = (int)(rnd() % 9) - 4;
}

/* ------------------------------------------------------------------ helpers */
static void number(int font, int x, int y, int v, uint16_t c) {
  char s[4];
  int n = 0;
  if (v >= 100) s[n++] = (char)('0' + v / 100);
  if (v >= 10) s[n++] = (char)('0' + v / 10 % 10);
  s[n++] = (char)('0' + v % 10);
  s[n] = 0;
  gfx_text(font, x, y, s, c);
}

/* a filled disc, clipped to the band */
static void disc(int cx, int cy, int r, uint16_t c) {
  for (int dy = -r; dy <= r; dy++) {
    int y = cy + dy;
    if (y < gfx_y0 || y >= gfx_y0 + BAND_H) continue;
    int w = 0;
    while ((w + 1) * (w + 1) + dy * dy <= r * r) w++;
    gfx_fill(cx - w, y, 2 * w + 1, 1, c);
  }
}

/* the music box's warning: a triangle with "!" (orange, then red and blinking) */
static void warning_sign(int x, int y) {
  if (!box_warning()) return;
  bool red = N.box <= BOX_FULL / 20;
  if (red && ((int)(g.t * 4) & 1)) return;
  uint16_t c = red ? RGB(0xE02020) : RGB(0xF09020);
  for (int r = 0; r < 18; r++) gfx_fill(x + 10 - r * 10 / 18, y + r, 1 + 2 * (r * 10 / 18), 1, c);
  gfx_fill(x + 9, y + 5, 3, 7, BLACK);
  gfx_fill(x + 9, y + 14, 3, 2, BLACK);
}

/* ------------------------------------------------------------------ around the view */
static void clock_and_night(void) {
  char h[3] = {'1', '2', 0};   /* 12 AM, then 1 to 5 */
  if (N.hour) h[0] = (char)('0' + N.hour), h[1] = 0;
  int x = 312 - gfx_text_w(FONT_BIG, " AM") - gfx_text_w(FONT_CLOCK, h);
  x += gfx_text(FONT_CLOCK, x, 4, h, WHITE);
  gfx_text(FONT_BIG, x, 8, " AM", WHITE);
  int nx = 312 - gfx_text_w(FONT_HUD, "Night 0");
  nx += gfx_text(FONT_HUD, nx, 30, "Night ", WHITE);
  number(FONT_HUD, nx, 30, N.night, WHITE);
}

static void battery_hud(void) {
  bool ok = lights_work() || N.flip >= 1;
  uint16_t c = ok ? WHITE : RGB(0x707070);
  gfx_frame_rect(8, 8, 44, 16, c);
  gfx_fill(52, 12, 3, 8, c);
  int bars = battery_bars();
  for (int i = 0; i < bars; i++) gfx_fill(11 + 10 * i, 11, 8, 10, ok ? RGB(0x9CD94A) : RGB(0x5A5A5A));
  gfx_text(FONT_TEXT, 60, 10, "Flashlight", c);
}

void view_captions(void) {
  int y = g.scene == SC_NIGHT && g.n.flip >= 1 ? 182 : 196;
  for (int i = 3; i >= 0; i--) {
    if (!g.cap[i] || g.cap_t[i] <= 0) continue;
    int a = g.cap_t[i] > 0.5f ? 32 : (int)(g.cap_t[i] * 64);
    gfx_text(FONT_TEXT, 9, y + 1, g.cap[i], BLACK);
    gfx_text(FONT_TEXT, 8, y, g.cap[i], gfx_mix(RGB(0xD8E8FF), BLACK, a));
    y -= 13;
  }
}

/* the bars at the bottom: the mask on the left, the monitor on the right, with their keys */
static void bottom_bars(bool cams) {
  if (!cams) {
    gfx_blend(8, 226, 116, 12, RGB(0xC02020), 12);
    gfx_frame_rect(8, 226, 116, 12, RGB(0xE06060));
    gfx_text_c(FONT_TEXT, 66, 226, "2 or down: mask", WHITE);
  }
  gfx_blend(196, 226, 116, 12, WHITE, 6);
  gfx_frame_rect(196, 226, 116, 12, gfx_mix(WHITE, BLACK, 14));
  gfx_text_c(FONT_TEXT, 254, 226, cams ? "OK: put it down" : "OK: monitor", WHITE);
}

/* the first night, and the first seconds of every other: what the other keys do */
static void key_line(bool cams) {
  if (N.night > 1 && N.t > 15) return;
  if (cams) {
    gfx_blend(8, 198, 152, 27, BLACK, 20);
    gfx_text(FONT_TEXT, 12, 199, "0-9 camera, 1 again: 11 12", RGB(0xE0E0B0));
    gfx_text(FONT_TEXT, 12, 211, "hold up: light, wind on 11", RGB(0xE0E0B0));
    return;
  }
  gfx_blend(0, 210, SCREEN_W, 14, BLACK, 20);
  gfx_text_c(FONT_TEXT, 160, 211, "4 6 look   5 or up: hall light   7 9: vent lights", RGB(0xE0E0B0));
}

/* ------------------------------------------------------------------ the office */
static int hall_picture(void) {
  if (N.gf == GF_HALL) return PIC_HALL_GF;
  if (N.foxy_hall) return PIC_HALL_FOXY;
  static const struct { uint8_t who, pic; } in_hall[] = {
    {WF, PIC_HALL_WF}, {TF, PIC_HALL_TF}, {WB, PIC_HALL_WB}, {TC, PIC_HALL_TC}, {MANGLE, PIC_HALL_MANGLE}};
  for (unsigned i = 0; i < sizeof in_hall / sizeof in_hall[0]; i++)
    if (bot_at(in_hall[i].who) == L_HALL) return in_hall[i].pic;
  if (bot_at(TF) == L_HALL_FAR) return PIC_HALL_TFFAR;
  return PIC_HALL_LIT;
}

static int vent_picture(int side) {
  if (side == 0) return bot_at(TC) == L_LSPOT ? PIC_LVENT_TC : bot_at(BB) == L_LSPOT ? PIC_LVENT_BB : PIC_LVENT_LIT;
  return bot_at(TB) == L_RSPOT ? PIC_RVENT_TB : bot_at(MANGLE) == L_RSPOT ? PIC_RVENT_MANGLE : PIC_RVENT_LIT;
}

static int visitor_picture(void) {
  if (N.gf == GF_SHOWN || N.gf == GF_FADING) return PIC_OCC_GF;
  if (N.occ_state != OCC_SHOWN && N.occ_state != OCC_MASKED) return -1;
  static const uint8_t pics[WALKERS] = {[TF] = PIC_OCC_TF, [TB] = PIC_OCC_TB, [TC] = PIC_OCC_TC,
                                        [WF] = PIC_OCC_WF, [WB] = PIC_OCC_WB, [WC] = PIC_OCC_WC};
  return N.occ < WALKERS ? pics[N.occ] : -1;
}

/* the Freddy mask: black but for its eyes (where the game's are), coming up from the bottom */
static void mask_view(void) {
  int top = SCREEN_H - (int)(N.mask_anim * SCREEN_H);   /* the mask's top row */
  int y0 = gfx_y0 > top ? gfx_y0 : top, y1 = gfx_y0 + BAND_H;
  for (int y = y0; y < y1; y++) {
    int my = y - top;   /* in the mask */
    /* the eyes: two ellipses */
    int spans[2][2], n = 0;
    for (int e = 0; e < 2; e++) {
      int cx = e ? 215 : 107, cy = 112, rx = 41, ry = 32, dy = my - cy;
      if (dy * dy >= ry * ry) continue;
      int w = (int)(rx * __builtin_sqrtf(1.0f - (float)(dy * dy) / (float)(ry * ry)));
      spans[n][0] = cx - w, spans[n][1] = cx + w, n++;
    }
    int x = 0;
    for (int k = 0; k <= n; k++) {
      int end = k < n ? spans[k][0] : SCREEN_W;
      if (end > x) {
        gfx_dim(x, y, end - x, 1, 1);
        gfx_blend(x, y, end - x, 1, RGB(0x120808), 6);
      }
      if (k < n) {
        gfx_dim(spans[k][0], y, spans[k][1] - spans[k][0], 1, 26);
        x = spans[k][1];
      }
    }
  }
}

static void office(void) {
  int px = (int)N.pan;
  if (N.blackout > 0) {
    gfx_fill(0, gfx_y0, SCREEN_W, BAND_H, BLACK);
    return;
  }
  pic_part(PIC_OFFICE, 0, 0, px, 0, SCREEN_W, SCREEN_H);
  if (N.hall_light) pic_draw(hall_picture(), HALL_X - px, HALL_Y);
  if (N.vent_light == 1) pic_draw(vent_picture(0), LVENT_X - px, LVENT_Y);
  if (N.vent_light == 2) pic_draw(vent_picture(1), RVENT_X - px, RVENT_Y);
  if (N.mangle_in) pic_draw(PIC_OFFICE_MANGLE, MANGLE_X - px, MANGLE_Y);
  if (N.bb_in) pic_draw(PIC_OFFICE_BB, BBO_X - px, BBO_Y);
  int v = visitor_picture();
  if (v >= 0) {
    pic_draw(v, OCC_X - px, OCC_Y);
    if (N.gf == GF_FADING) gfx_dim(OCC_X - px, gfx_y0, pic_w(v), BAND_H, 32 - (int)(N.gf_t / 1.5f * 32));
  }
  if (f.flick_off) gfx_dim(0, gfx_y0, SCREEN_W, BAND_H, 6);
}

/* ------------------------------------------------------------------ the cameras */
static const char *const cam_names[CAM_COUNT] = {
  "Party Room 1", "Party Room 2", "Party Room 3", "Party Room 4", "Left Air Vent", "Right Air Vent", "Main Hall",
  "Parts/Service", "Show Stage", "Game Area", "Prize Corner", "Kid's Cove"};

static bool at(int who, int loc) { return bot_at(who) == loc; }
static bool puppet_at(int loc) { return N.puppet >= 4 && N.puppet_at == loc; }

/* the feed: each room as the game's pictures show it, in the camera's light or not (a few show who stands close
   in the dark) */
static int cam_picture(bool lit) {
  switch (N.cam) {
  case L_PR1:
    if (!lit) return PIC_CAM01_DARK;
    return puppet_at(L_PR1) ? PIC_CAM01_PUPPET : at(WB, L_PR1) ? PIC_CAM01_WB : at(TC, L_PR1) ? PIC_CAM01_TC
         : at(MANGLE, L_PR1) ? PIC_CAM01_MANGLE : PIC_CAM01_LIT;
  case L_PR2:
    if (!lit) return at(WC, L_PR2) ? PIC_CAM02_DWC : PIC_CAM02_DARK;
    return puppet_at(L_PR2) ? PIC_CAM02_PUPPET : at(WC, L_PR2) ? PIC_CAM02_WC : at(TB, L_PR2) ? PIC_CAM02_TB
         : at(MANGLE, L_PR2) ? PIC_CAM02_MANGLE : PIC_CAM02_LIT;
  case L_PR3:
    if (!lit) return at(WF, L_PR3) ? PIC_CAM03_DWF : PIC_CAM03_DARK;
    return puppet_at(L_PR3) ? PIC_CAM03_PUPPET : at(WF, L_PR3) ? PIC_CAM03_WF : at(TB, L_PR3) ? PIC_CAM03_TB
         : PIC_CAM03_LIT;
  case L_PR4:
    if (!lit) return at(TB, L_PR4) ? PIC_CAM04_DTB : PIC_CAM04_DARK;
    return puppet_at(L_PR4) ? PIC_CAM04_PUPPET : at(WC, L_PR4) ? PIC_CAM04_WC : at(TB, L_PR4) ? PIC_CAM04_TB
         : PIC_CAM04_LIT;
  case L_LVENT:
    if (!lit) return PIC_CAM05_DARK;
    return at(WB, L_LVENT) ? PIC_CAM05_WB : at(TC, L_LVENT) ? PIC_CAM05_TC : at(BB, L_LVENT) ? PIC_CAM05_BB
         : PIC_CAM05_LIT;
  case L_RVENT:
    if (!lit) return PIC_CAM06_DARK;
    return at(WC, L_RVENT) ? PIC_CAM06_WC : at(TB, L_RVENT) ? PIC_CAM06_TB : at(MANGLE, L_RVENT) ? PIC_CAM06_MANGLE
         : PIC_CAM06_LIT;
  case L_MAIN:
    if (!lit) return PIC_CAM07_DARK;
    return puppet_at(L_MAIN) ? PIC_CAM07_PUPPET : at(WF, L_MAIN) ? PIC_CAM07_WF : at(WB, L_MAIN) ? PIC_CAM07_WB
         : at(TC, L_MAIN) ? PIC_CAM07_TC : at(MANGLE, L_MAIN) ? PIC_CAM07_MANGLE : PIC_CAM07_EMPTY;
  case L_PARTS: {
    if (!lit) return PIC_CAM08_DARK;
    int m = at(WF, L_PARTS) | at(WB, L_PARTS) << 1 | at(WC, L_PARTS) << 2;
    if (!m && N.ai[FOXY] >= 0 && !N.foxy_hall) return PIC_CAM08_FOXY;
    return PIC_CAM08_S0 + m;
  }
  case L_STAGE:
    return (lit ? PIC_CAM09_S0 : PIC_CAM09_D0) + (at(TF, L_STAGE) | at(TB, L_STAGE) << 1 | at(TC, L_STAGE) << 2);
  case L_GAME:
    if (!lit) return PIC_CAM10_DARK;
    if (at(TF, L_GAME) && at(BB, L_GAME)) return PIC_CAM10_TFBB;
    return at(TF, L_GAME) ? PIC_CAM10_TF : at(BB, L_GAME) ? PIC_CAM10_BB : at(MANGLE, L_GAME) ? PIC_CAM10_MANGLE
         : PIC_CAM10_EMPTY;
  case L_PRIZE:
    if (!lit) return PIC_CAM11_DARK;
    if (N.puppet < 4) return N.puppet == 0 && at(MANGLE, L_PRIZE) ? PIC_CAM11_MANGLE : PIC_CAM11_P0 + N.puppet;
    return at(MANGLE, L_PRIZE) ? PIC_CAM11_MANGLE : PIC_CAM11_EMPTY;
  default: return !lit ? PIC_CAM12_DARK : at(MANGLE, L_KIDS) ? PIC_CAM12_MANGLE : PIC_CAM12_EMPTY;
  }
}

static void tiny_label(int x, int y, int cam, uint16_t c) {
  char s[3] = {(char)('0' + (cam + 1) / 10), (char)('0' + (cam + 1) % 10), 0};
  gfx_text_c(FONT_TEXT, x, y, s, c);
}

static void camera_map(void) {
  const uint8_t *m = section(SEC_MAP);
  int mw = (int)rd16(m), mh = (int)rd16(m + 2), stride = (mw + 7) >> 3;
  int mx = SCREEN_W - mw - 6, my = 216 - mh;
  for (int r = 0; r < mh; r++) {
    int y = my + r;
    if (y < gfx_y0 || y >= gfx_y0 + BAND_H) continue;
    const uint8_t *row = m + 4 + r * stride;
    uint16_t *d = gfx_band + (y - gfx_y0) * SCREEN_W + mx;
    for (int c = 0; c < mw; c++)
      if (row[c >> 3] >> (c & 7) & 1) d[c] = WHITE;
  }
  const uint8_t *centres = m + 4 + mh * stride;
  bool blink = !((int)(g.t * 3) & 1);
  for (int k = 0; k < CAM_COUNT; k++) {
    int cx = mx + (int)rd16(centres + 4 * k), cy = my + (int)rd16(centres + 4 * k + 2);
    bool sel = k == N.cam;
    gfx_fill(cx - 9, cy - 6, 19, 13, sel && blink ? RGB(0x8BC34A) : RGB(0x3A3A3A));
    gfx_frame_rect(cx - 9, cy - 6, 19, 13, RGB(0xD0D0D0));
    tiny_label(cx + 1, cy - 6, k, WHITE);
    if (k == L_PRIZE) warning_sign(cx + 11, cy - 9);
  }
  gfx_text(FONT_HUD, 12, 34, cam_names[N.cam], WHITE);
  char s[] = "CAM 00";
  s[4] = (char)('0' + (N.cam + 1) / 10), s[5] = (char)('0' + (N.cam + 1) % 10);
  gfx_text(FONT_HUD, 12, 48, s, RGB(0xC0C0C0));
}

/* CAM 11: the music box's button and how wound it is (20 marks round a circle) */
static void music_box(void) {
  int cx = 70, cy = 106;
  gfx_blend(14, 76, 112, 58, BLACK, 18);
  gfx_frame_rect(14, 76, 112, 58, N.winding ? WHITE : GREY);
  gfx_text_c(FONT_HUD, 70, 118, N.winding ? "winding..." : "hold up: wind", WHITE);
  int marks = (int)(N.box * 20 / BOX_FULL + 0.999f);
  static const int8_t ring[20][2] = {{0, -13}, {4, -12}, {8, -11}, {11, -8}, {12, -4}, {13, 0}, {12, 4}, {11, 8},
                                     {8, 11}, {4, 12}, {0, 13}, {-4, 12}, {-8, 11}, {-11, 8}, {-12, 4}, {-13, 0},
                                     {-12, -4}, {-11, -8}, {-8, -11}, {-4, -12}};
  for (int i = 0; i < 20; i++)
    gfx_fill(cx + ring[i][0] - 1, cy - 10 + ring[i][1] - 1, 3, 3, i < marks ? WHITE : RGB(0x404040));
}

static void cameras(void) {
  bool lit = N.cam_light;
  /* (the custom night has no CAM 08 and CAM 09, as in the game) */
  if (N.glitch > 0 || (N.night == 7 && (N.cam == L_PARTS || N.cam == L_STAGE))) {
    gfx_fill(0, gfx_y0, SCREEN_W, BAND_H, BLACK);
    gfx_static(0, gfx_y0, SCREEN_W, BAND_H, 18, f.seed);
  } else {
    /* (CAM 07 to 12 are wider than the screen: the camera pans, 5 seconds across, 2 still, 5 back, 2 still) */
    static float since;
    static int was = -1;
    if (N.cam != was || g.t < since) was = N.cam, since = g.t;   /* (a new camera, or the clock started again) */
    float c = g.t - since;
    while (c >= 14) c -= 14;
    float a = c < 5 ? c / 5 : c < 7 ? 1 : c < 12 ? 1 - (c - 7) / 5 : 0;
    int pan = (int)(a * CAM_PAN);
    if (N.cam >= L_MAIN) pic_part(cam_picture(lit), 0, 0, pan < 0 ? 0 : pan > CAM_PAN ? CAM_PAN : pan, 0, SCREEN_W, SCREEN_H);
    else pic_draw(cam_picture(lit), 0, 0);
    gfx_static(0, gfx_y0, SCREEN_W, BAND_H, f.noise + (N.cam == N.puppet_at && N.puppet >= 4 ? 8 : 0), f.seed);
  }
  if (g.blip > 0) gfx_blend(0, gfx_y0, SCREEN_W, BAND_H, WHITE, (int)(g.blip * 120));
  gfx_frame_rect(5, 5, 310, 230, gfx_mix(WHITE, BLACK, 20));
  if ((int)(g.t * 1.5f) & 1) disc(64, 53, 4, RGB(0xE02020));
  camera_map();
  if (N.cam == L_PRIZE) music_box();
}

/* the monitor coming up, or going down */
static void tablet(void) {
  int top = (int)((1 - N.flip) * SCREEN_H);
  gfx_fill(6, top, SCREEN_W - 12, SCREEN_H - top, RGB(0x1C1F22));
  gfx_frame_rect(6, top, SCREEN_W - 12, SCREEN_H - top + 2, RGB(0x50565C));
  gfx_fill(20, top + 12, SCREEN_W - 40, SCREEN_H - top - 12, RGB(0x0A0C0D));
}

/* ------------------------------------------------------------------ the night */
static void night_draw(void) {
  bool cams = N.flip >= 1;
  if (cams) cameras();
  else {
    office();
    if (N.mask_anim > 0) mask_view();
    if (N.flip > 0) tablet();
  }
  if (N.blackout <= 0 || cams) {
    clock_and_night();
    battery_hud();
    if (!cams) warning_sign(286, 202);
    key_line(cams);
    if (N.mask_anim <= 0 || cams) bottom_bars(cams);
  }
  view_captions();
}

void view_night(void) {
  if (gfx_y0 == 0) prepare();
  night_draw();
}

/* ------------------------------------------------------------------ jumpscares */
void view_scare(void) {
  static const uint8_t first[BOTS] = {[TF] = PIC_SCARE_TF0, [TB] = PIC_SCARE_TB0, [TC] = PIC_SCARE_TC0,
                                      [MANGLE] = PIC_SCARE_MANGLE0, [WF] = PIC_SCARE_WF0, [WB] = PIC_SCARE_WB0,
                                      [WC] = PIC_SCARE_WC0, [FOXY] = PIC_SCARE_FOXY0, [GF] = PIC_SCARE_GF0,
                                      [PUPPET] = PIC_SCARE_PUPPET0};
  static const uint8_t count[BOTS] = {[TF] = SCARE_TF_N, [TB] = SCARE_TB_N, [TC] = SCARE_TC_N,
                                      [MANGLE] = SCARE_MANGLE_N, [WF] = SCARE_WF_N, [WB] = SCARE_WB_N,
                                      [WC] = SCARE_WC_N, [FOXY] = SCARE_FOXY_N, [GF] = SCARE_GF_N,
                                      [PUPPET] = SCARE_PUPPET_N};
  if (gfx_y0 == 0) prepare();
  int w = g.scare < BOTS && count[g.scare] ? g.scare : TF;
  int k = (int)(g.scare_t * 24) % count[w];
  int dx = g.scare_t > 0.08f ? f.shake : 0;
  gfx_fill(0, gfx_y0, SCREEN_W, BAND_H, BLACK);
  pic_draw(first[w] + k, dx, 0);
}
