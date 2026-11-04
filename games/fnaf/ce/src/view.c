/* Drawing a night on the CE: the office (turning, its lights, doors, buttons and fan), the monitor and its
 * cameras (the feed's slow turn, its static, the map), and around them the clock, the power, the call's subtitles
 * and the captions of what can be heard. As the NumWorks version's view.c, with the CE's pictures: the office and
 * the cameras at half height in RAM (scene), drawn with each row twice. */
#include <stddef.h>
#include <string.h>
#include "fnaf.h"

#define N g.n

/* the office's small pictures, decoded once */
#define DOOR_LW 70
#define DOOR_RW 60
#define PATCH_MAX (LIGHT_R_W * LIGHT_R_H > LIGHT_L_W * LIGHT_L_H ? LIGHT_R_W * LIGHT_R_H : LIGHT_L_W * LIGHT_L_H)
static uint8_t door_l[DOOR_LW * 120], door_r[DOOR_RW * 120];
static uint8_t panel_l[3][32 * 40], panel_r[3][48 * 40], fans[3][48 * 24];   /* (the buttons lit: 1 to 3) */
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
  for (int k = 0; k < 3; k++) {
    pic_full(PIC_PANEL_L1 + k, panel_l[k], 32);
    pic_full(PIC_PANEL_R1 + k, panel_r[k], 48);
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

/* ------------------------------------------------------------------ drawing only what changed
 * The office mostly stands still. A frame of it is measured first: drawn with nothing drawn, sig_add taking what
 * decides the office, part and part_end around each part that changes on its own (the fan, the clock, a
 * caption...): what it shows, and where it goes (d_touch; a part shown before is known). When the buffer drawn
 * into holds the same office from 2 frames ago, only the parts that changed since are drawn again: the frame each
 * time, clipped to them, with the parts that it doesn't meet left out. */
#define PARTS 16
#define BASE 16
typedef struct {
  Box at;
  const void *what;   /* what a part shows: its text, a tag... (and how, where: always at the same place) */
  uint32_t how;
} Part;
typedef struct {
  bool ok;
  uint32_t frame;
  int n, nb;
  Part p[PARTS];
  int base[BASE];   /* what decides the office */
} Drawn;
static Drawn now, last, drawn[2];   /* this frame's; the last one measured (its parts' places); each buffer's */
static uint8_t *drawn_scr;          /* (the first buffer: drawn[0]) */
static bool measuring, partly;      /* measuring; drawing again where it changed (part_at: the part reached) */
static int part_at;
static const char tags[4];          /* (what the parts with no text of their own show) */
#ifdef HOST
int view_partial, view_wrong;
#endif

static void sig_add(int v) {
  if (measuring && now.nb < BASE) now.base[now.nb] = v;
  if (measuring) now.nb++;
}

/* a part that changes on its own: true when it's to be drawn (measured, part_end after it; or met by the clip) */
static bool part(const void *what, uint32_t how) {
  if (measuring) {
    int k = now.n++;
    if (k >= PARTS) return true;
    if (k < last.n && last.p[k].what == what && last.p[k].how == how) {   /* (known: drawn alike before) */
      now.p[k] = last.p[k];
      return false;
    }
    now.p[k] = (Part){BOX_NONE, what, how};
    d_touch = BOX_NONE;
    return true;
  }
  if (!partly) return true;
  int k = part_at++;
  return k >= PARTS || d_meets(now.p[k].at);
}
static void part_end(void) {
  if (measuring && now.n <= PARTS) now.p[now.n - 1].at = d_touch;
}

static bool has(const Drawn *d, const Part *q) {
  for (int i = 0; i < d->n; i++) {
    const Part *o = &d->p[i];
    if (o->what == q->what && o->how == q->how && o->at.x0 == q->at.x0 && o->at.y0 == q->at.y0 &&
        o->at.x1 == q->at.x1 && o->at.y1 == q->at.y1)
      return true;
  }
  return false;
}

static int add_box(Box *list, int n, Box b) {
  if (b.x0 >= b.x1 || b.y0 >= b.y1) return n;
  for (int i = 0; i < n; i++)
    if (list[i].x0 <= b.x0 && list[i].y0 <= b.y0 && list[i].x1 >= b.x1 && list[i].y1 >= b.y1) return n;
  list[n] = b;
  return n + 1;
}

/* draws a frame (draw) only where it changed since the buffer's last one */
static void draw_changes(void (*draw)(void)) {
  now.n = now.nb = 0;
  measuring = true;
  d_clip((Box){0, 0, 0, 0});
  draw();
  measuring = false;
  d_clip(BOX_ALL);
  if (!drawn_scr) drawn_scr = scr;
  Drawn *d = &drawn[scr != drawn_scr], *o = &drawn[scr == drawn_scr];
  bool fits = now.n <= PARTS && now.nb <= BASE;
  bool same = fits && d->ok && o->ok && d->frame + 2 == ce_frames && o->frame + 1 == ce_frames &&
              d->nb == now.nb && !memcmp(d->base, now.base, sizeof now.base[0] * (size_t)now.nb);
  if (!same) {
    draw();
  } else {
    Box dirty[2 * PARTS];
    int nd = 0;
    for (int i = 0; i < now.n; i++)
      if (!has(d, &now.p[i])) nd = add_box(dirty, nd, now.p[i].at);
    for (int i = 0; i < d->n; i++)
      if (!has(&now, &d->p[i])) nd = add_box(dirty, nd, d->p[i].at);
    partly = true;
    for (int i = 0; i < nd; i++) part_at = 0, d_clip(dirty[i]), draw();
    partly = false;
    d_clip(BOX_ALL);
#ifdef HOST
    static uint8_t as_drawn[SCR_W * SCR_H];
    memcpy(as_drawn, scr, sizeof as_drawn);
    draw();
    view_partial++;
    if (memcmp(as_drawn, scr, sizeof as_drawn)) view_wrong++;
#endif
  }
  now.ok = fits, now.frame = ce_frames;
  *d = now;
  if (fits) last = now;
}

static const uint8_t GREYS[8] = {UI_BLACK, UI_WHITE1, UI_GREY1, UI_SPARE, UI_DIM, UI_GREY2, UI_EDGE, UI_BOX};
static const uint8_t DARK_GREYS[8] = {UI_BLACK, UI_BLACK, UI_BOX, UI_BLACK, UI_EDGE, UI_BLACK, UI_BOX, UI_GREY1};

/* the static over a picture (the NumWorks version: each pixel a share of the way to a random grey): snow, and a
 * lighter line now and then */
void static_over(int a, int group) {
  d_snow(a * 110, rnd(), GREYS);
  uint32_t r = rnd();
  if ((r & 3) == 0) d_remap(0, (int)(r >> 8) % SCR_H, SCR_W, 1 + (int)(r >> 4 & 1), map_remap(group, 1));
}

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
    uint8_t *d = scr + y * SCR_W + x;
    for (int r = 0; r < 5; r++, d += SCR_W, b = (uint16_t)(b << 3)) {
      if (b & 0x4000) d[0] = c;
      if (b & 0x2000) d[1] = c;
      if (b & 0x1000) d[2] = c;
    }
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
  if (!part(tags, (uint32_t)(N.hour << 4 | N.night))) return;
  char h[3] = {'1', '2', 0};   /* 12 AM, then 1 to 5 */
  if (N.hour) h[0] = (char)('0' + N.hour), h[1] = 0;
  int x = 310 - d_text_w(FONT_BIG, " AM") - d_text_w(FONT_CLOCK, h);
  x += d_text(FONT_CLOCK, x, 6, h, INK_WHITE);
  d_text(FONT_BIG, x, 8, " AM", INK_WHITE);
  int nx = 310 - d_text_w(FONT_HUD, "Night ") - d_text_w(FONT_PIXEL, "0");
  nx += d_text(FONT_HUD, nx, 31, "Night ", INK_WHITE);
  small_digits(FONT_PIXEL, nx, 30, N.night, INK_WHITE);
  part_end();
}

static void power_hud(void) {
  int p = N.power / 10;
  if (!part(tags + 1, (uint32_t)(p << 3 | N.usage))) return;
  int x = 8 + d_text(FONT_HUD, 8, 206, "Power left: ", INK_WHITE);
  small_digits(FONT_PIXEL, x, 205, p, INK_WHITE);
  d_text(FONT_HUD, x + d_text_w(FONT_PIXEL, p >= 10 ? "00" : "0") + 1, 206, "%", INK_WHITE);
  x = 8 + d_text(FONT_HUD, 8, 222, "Usage: ", INK_WHITE);
  static const uint8_t bars[5] = {UI_BAR_G, UI_BAR_G, UI_BAR_Y, UI_BAR_R, UI_BAR_R};
  static const uint8_t lit[5] = {UI_BAR_GL, UI_BAR_GL, UI_BAR_YL, UI_BAR_RL, UI_BAR_RL};
  for (int i = 0; i < N.usage && i < 5; i++) {
    d_fill(x + 9 * i, 222, 7, 12, bars[i]);
    d_fill(x + 9 * i + 1, 223, 5, 2, lit[i]);
  }
  part_end();
}

static void call_hud(int grp) {
  if (N.night > 5 || N.call_muted || N.t >= call_end(N.night)) return;
  if (N.t > 3 && part(tags + 2, 0)) {
    d_remap(6, 6, 74, 15, map_remap(grp, 0));
    d_frame(6, 6, 74, 15, UI_SILVER);
    d_text(FONT_HUD, 12, 8, "MUTE CALL", INK_WHITE);
    d_text(FONT_TEXT, 84, 8, "0", INK_DIM);
    part_end();
  }
  const char *s = call_line(N.night, N.t);
  if (!s || !part(s, 0)) return;
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
  part_end();
}

void view_captions(void) {
  int y = 186;
  for (int i = 3; i >= 0; i--) {
    if (!g.cap[i] || g.cap_t[i] <= 0) continue;
    int a = g.cap_t[i] > 0.5f ? 32 : (int)(g.cap_t[i] * 64);
    if (part(g.cap[i], (uint32_t)(a << 8 | y))) {
      d_text(FONT_TEXT, 9, y + 1, g.cap[i], INK_BLACK);
      d_text(FONT_TEXT, 8, y, g.cap[i], ink_fade(INK_CAPTION, a));
      part_end();
    }
    y -= 13;
  }
}

/* the bar at the bottom to bring the monitor up or down */
static void flip_bar(int grp) {
  if (!part(tags + 3, 0)) return;
  d_remap(118, 229, 84, 9, map_remap(grp, 1));
  d_frame(118, 229, 84, 9, UI_SPARE);
  for (int k = 0; k < 2; k++)
    for (int i = 0; i < 4; i++) {
      d_fill(152 + 9 * k + i, 235 - i, 1, 1, UI_WHITE);
      d_fill(152 + 9 * k + 7 - i, 235 - i, 1, 1, UI_WHITE);
    }
  part_end();
}

/* ------------------------------------------------------------------ the office */
int office_pan(void) { return (int)N.pan; }

static int office(void) {
  int px = (int)N.pan;
  sig_add(px);
  sig_add(N.dark);
  if (N.dark) {
    bool black = N.dark == 3 && (N.dark_t > 0.35f || (int)(N.dark_t * 30) & 1);
    sig_add(black);
    sig_add(N.dark == 2 && f.freddy_face);
    if (black) {
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
  bool lit_l = N.light == 1 + LEFT && !f.light_off, lit_r = N.light == 1 + RIGHT && !f.light_off;
  sig_add(lit_l ? 1 + (N.pos[BONNIE] == AT_DOOR) : 0);
  sig_add(lit_r ? 1 + (N.pos[CHICA] == AT_DOOR) : 0);
  scene_want(PIC_OFFICE);
  d_blit(scene, SCENE_W, px, 0, SCR_W, SCENE_H, 0, 0, true);
  if (lit_l) {
    want_patch(N.pos[BONNIE] == AT_DOOR ? PIC_LIGHT_L_WHO : PIC_LIGHT_L, LIGHT_L_W);
    d_blit(patch, LIGHT_L_W, 0, 0, LIGHT_L_W, LIGHT_L_H, LIGHT_L_X - px, 2 * LIGHT_L_Y, true);
  }
  if (lit_r) {
    want_patch(N.pos[CHICA] == AT_DOOR ? PIC_LIGHT_R_WHO : PIC_LIGHT_R, LIGHT_R_W);
    d_blit(patch, LIGHT_R_W, 0, 0, LIGHT_R_W, LIGHT_R_H, LIGHT_R_X - px, 2 * LIGHT_R_Y, true);
  }
  if (part(fans, (uint32_t)(px << 2 | f.fan))) {
    d_blit(fans[f.fan], 48, 0, 0, 48, 24, FAN_X - px, FAN_Y, true);
    part_end();
  }
  /* the buttons (all off: as the office has them) */
  int bl = N.door[LEFT] | (N.light == 1 + LEFT) << 1, br = N.door[RIGHT] | (N.light == 1 + RIGHT) << 1;
  sig_add(bl << 2 | br);
  if (bl) d_blit(panel_l[bl - 1], 32, 0, 0, 26, 40, PANEL_L_X - px, PANEL_Y, true);
  if (br) d_blit(panel_r[br - 1], 48, 0, 0, 48, 40, PANEL_R_X - px, PANEL_Y, true);
  for (int s = 0; s < 2; s++) {
    int vis = (int)(N.door_anim[s] * 120 + 0.5f);
    sig_add(vis);
    if (vis > 0)
      d_blit(s ? door_r : door_l, s ? DOOR_RW : DOOR_LW, 0, 120 - vis, s ? DOOR_R_W : DOOR_L_W, vis,
             (s ? DOOR_R_X : DOOR_L_X) - px, 0, true);
  }
  sig_add(N.gold == 3);
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
  case CAM_2A: return b ? PIC_CAM2A_B : PIC_CAM2A_EMPTY;   /* (its light flickering: the palette) */
  case CAM_2B: return (N.gold == 1 || N.gold == 2) && !b ? PIC_CAM2B_GOLD : b ? PIC_CAM2B_B : PIC_CAM2B_EMPTY;
  case CAM_3: return b ? PIC_CAM3_B : PIC_CAM3_EMPTY;
  case CAM_4A: return c && fr ? PIC_CAM4A_CF : c ? PIC_CAM4A_C : fr ? PIC_CAM4A_F : PIC_CAM4A_EMPTY;
  case CAM_4B: return c ? PIC_CAM4B_C : fr ? PIC_CAM4B_F : PIC_CAM4B_EMPTY;
  case CAM_5: return b ? PIC_CAM5_B : PIC_CAM5_EMPTY;
  case CAM_7: return c && fr ? PIC_CAM7_CF : c ? PIC_CAM7_C : fr ? PIC_CAM7_F : PIC_CAM7_EMPTY;
  }
  return -1;
}

/* a button of the map: 17 x 11, its edge and its inside */
static void map_button(uint8_t *p, uint8_t fill) {
  memset(p, UI_MAP_LINE, 17);
  for (int r = 1; r < 10; r++) {
    p += SCR_W;
    p[0] = p[16] = UI_MAP_LINE;
    memset(p + 1, fill, 15);
  }
  memset(p + SCR_W, UI_MAP_LINE, 17);
}

static void camera_map(void) {
  const uint8_t *m = asset(ASSET_CAMMAP);
  int size = m[0], runs = m[1] | m[2] << 8;
  int mx = SCR_W - size - 4, my = SCR_H - size - 12;
  const uint8_t *r = m + 3;
  ce_runs(scr + my * SCR_W + mx, r, runs, UI_WHITE);   /* (the cameras are only drawn whole: not clipped) */
  r += 3 * runs;
  bool blink = !((int)(g.t * 3) & 1);
  for (int k = 0; k < CAM_COUNT; k++) {
    int cx = mx + r[2 * k], cy = my + r[2 * k + 1];
    bool sel = k == N.cam;
    map_button(scr + (cy - 5) * SCR_W + cx - 8, sel && blink ? UI_MAP_ON : UI_MAP_OFF);
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
  else if (!paused) static_over(noise, grp);
  int dim = N.cam == CAM_2A && !f.hall_lit && N.foxy_run < 0 ? 11 : 32;   /* 2A's lamp, off most of the time */
  pal_set(grp, paused ? 9 * dim / 32 : dim, g.blip > 0 && !paused ? (int)(g.blip * 120) : 0);
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

/* a frame of the monitor coming up or down (half the screen's size, independent): straight onto the screen, twice
 * across into every other row, then each row twice */
static int monitor(int pic) {
  const uint8_t *p = asset(PIC_FIRST + pic);
  if (pic_base(p) >= 0 || pic_keeps(p)) {
    extra_want(pic, 160);
    d_blit2(extra, 160, 0, UI_BLACK);
  } else {
    pic_into_wide(pic, scr, 2 * SCR_W);
    for (uint8_t *r = scr; r < scr + SCR_W * SCR_H; r += 2 * SCR_W) memcpy(r + SCR_W, r, SCR_W);
    d_touch = BOX_ALL;
  }
  return pic_group_of(pic);
}

/* the night: paused, the picture only, darker */
static void night_draw(bool paused);
static void office_frame(void) { night_draw(false); }

static void night_frame(bool paused) {
  if (!paused && N.flip <= 0 && !f.halluc) draw_changes(office_frame);
  else night_draw(paused);
}

static void night_draw(bool paused) {
  bool cams = N.flip >= 1 && !N.dark;
  int grp, k = (int)(N.flip * FLIP_N);   /* (the monitor's frame coming up or down) */
  k = k < FLIP_N ? k : FLIP_N - 1;
  if (cams) {
    grp = cameras(paused);
  } else if (N.flip > 0 && !N.dark) {   /* the monitor over the whole screen: the office only made ready behind */
    office_pictures();
    scene_want(PIC_OFFICE);
    grp = monitor(PIC_FLIP0 + k);
    pal_set(grp, paused ? 9 : 32, 0);
  } else {
    grp = office();
    if (N.flip > 0) grp = monitor(PIC_FLIP0 + k);
    pal_set(grp, paused ? 9 : 32, 0);
  }
  if (paused) return;
  int shown = hallucination(grp);
  if (shown != grp) pal_set(shown, 32, 0);
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
  night_frame(false);
}

void view_paused(void) { night_frame(true); }

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
  scene_want(first[w] + k);   /* (twice across: its rows doubled here) */
  d_blit(scene, SCR_W, 0, 0, SCR_W, SCR_H / 2, dx, 0, true);
  if (dx > 0) d_fill(0, 0, dx, SCR_H, UI_BLACK);
  if (dx < 0) d_fill(SCR_W + dx, 0, -dx, SCR_H, UI_BLACK);
  pal_set(pic_group_of(first[w]), 32, 0);
}
