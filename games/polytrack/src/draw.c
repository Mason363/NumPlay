/* PolyTrack's screens (draw.h), in its colours: dark blue slanted boxes (#28346a, #212b58, #192042), white text,
 * green and red for the time against the record. The title and the menus sit over the track in 3D. */
#include <string.h>
#include "../../../launcher/src/gfx.h"
#include "data.h"
#include "draw.h"
#include "logo.h"
#include "plat.h"
#include "world.h"

#define WHITE 0xFFFF
#define SURFACE HEX(0x28346A)
#define SURFACE2 HEX(0x212B58)
#define SURFACE3 HEX(0x192042)
#define BUTTON HEX(0x112052)
#define HOVER HEX(0x334B77)
#define LAVENDER HEX(0xB3C1FF)
#define MUTED HEX(0xA9B3DE)
#define DIM HEX(0x5D6A7C)
#define GREEN HEX(0x55FF55)
#define RED HEX(0xFF5555)
#define YELLOW HEX(0xE2C026)
#define STRIP 24

void plat_push(int x, int y, int w, int h, const uint16_t *px);
void np_push(int x, int y, int w, int h, const color_t *px) { plat_push(x, y, w, h, px); }
static color_t strip[SCREEN_W * STRIP] __attribute__((aligned(4)));   /* (render.c fills it a word at a time) */

const char *const pt_env_name[3] = {"Summer", "Winter", "Desert"};
static const char *const PAINT_NAMES[PT_PAINTS] = {"Red", "Blue", "Green", "Purple", "Orange", "Teal", "Black", "Silver"};

static const pt_file *F;
static const car_t *CAR;
static const pt_ui *U;

/* ------------------------------------------------------------------ text */
static char *cat(char *p, const char *s) {
  while (*s) *p++ = *s++;
  *p = 0;
  return p;
}
static char *num(char *p, uint32_t v) {
  char t[12];
  int n = 0;
  do t[n++] = (char)('0' + v % 10); while ((v /= 10) && n < 11);
  while (n) *p++ = t[--n];
  *p = 0;
  return p;
}
/* PolyTrack's clock: 00:00.000 */
static char *clock_of(char *p, uint32_t ms) {
  uint32_t m = ms / 60000;
  if (m > 99) m = 99;
  *p++ = (char)('0' + m / 10), *p++ = (char)('0' + m % 10), *p++ = ':';
  *p++ = (char)('0' + ms / 10000 % 6), *p++ = (char)('0' + ms / 1000 % 10), *p++ = '.';
  *p++ = (char)('0' + ms / 100 % 10), *p++ = (char)('0' + ms / 10 % 10), *p++ = (char)('0' + ms % 10);
  *p = 0;
  return p;
}
static char *diff_of(char *p, int32_t d) {
  *p++ = d < 0 ? '-' : '+';
  return clock_of(p, (uint32_t)(d < 0 ? -d : d));
}

static void text(const np_font_t *f, int x, int y, const char *s, color_t c) { gfx_text(f, x, y, s, c, 256); }
static void center(const np_font_t *f, int x, int y, const char *s, color_t c) { gfx_text_center(f, x, y, s, c, 256); }
static void right(const np_font_t *f, int x, int y, const char *s, color_t c) { gfx_text_right(f, x, y, s, c, 256); }

/* ------------------------------------------------------------------ pieces */
/* PolyTrack's box: a parallelogram, its sides slanted by `s` pixels (s < 0 slants the other way) */
static void slant(int x, int y, int w, int h, int s, color_t c, int a) {
  int as = s < 0 ? -s : s, aw = w - as, d = h > 1 ? h - 1 : 1;
  int ya = y > gfx_y0 ? y : gfx_y0, yb = y + h < gfx_y1 ? y + h : gfx_y1;
  for (int y1 = ya; y1 < yb; y1++) {
    int j = y1 - y;
    int o = s >= 0 ? s * (h - 1 - j) / d : as * j / d;
    if (a >= 256) gfx_fill(x + o, y1, aw, 1, c);
    else gfx_fill_alpha(x + o, y1, aw, 1, c, a);
  }
}

/* a line of keys: "Up=gas|Down=brake", each key in lavender, what it does after it in white */
static void key_line(int y, const char *s) {
  char buf[48];
  int w = 0;
  for (int pass = 0; pass < 2; pass++) {
    int x = 160 - w / 2;
    w = 0;
    const char *q = s;
    while (*q) {
      int k = 0;
      while (q[k] && q[k] != '=' && q[k] != '|' && k < (int)sizeof buf - 1) k++;
      memcpy(buf, q, (size_t)k), buf[k] = 0;
      bool key = q[k] == '=';
      int tw = gfx_text_width(&np_font_small, buf);
      if (pass) text(&np_font_small, x + w, y, buf, key ? LAVENDER : WHITE);
      w += tw + (key ? 3 : 0);
      q += k;
      if (*q == '=') q++;
      else if (*q == '|') q++, w += 9;
    }
  }
}

static void keys(const char *s) {
  gfx_fill(0, 226, 320, 14, SURFACE3);
  key_line(236, s);
}

static void header(const char *title) {
  slant(-8, 10, 200, 28, 8, SURFACE3, 256);
  text(&np_font_title, 12, 31, title, WHITE);
}

static void page(const char *title) {
  gfx_vgrad(0, 0, 320, 240, 0x28346A, 0x151B3C);
  header(title);
}

/* a button: PolyTrack's dark slanted box, the chosen one lighter with a lavender edge */
static void button(int x, int y, int w, const char *s, bool on) {
  if (on) slant(x - 2, y - 2, w + 4, 28, 8, LAVENDER, 256);
  slant(x, y, w, 24, 7, on ? HOVER : BUTTON, on ? 256 : 235);
  center(&np_font_body, x + w / 2 + 2, y + 17, s, WHITE);
}

static void dim(void) { gfx_fill_alpha(0, 0, 320, 240, HEX(0x05081A), 140); }

static void not_saved(int y) {
  if (!U->save_failed) return;
  slant(40, y, 240, 17, 6, HEX(0x5A1E1A), 240);
  center(&np_font_small, 162, y + 12, "Storage full: progress not saved", WHITE);
}

/* PolyTrack's logo (logo.h): lavender letters edged in dark blue, blended over the picture */
static void logo(int x0, int y0) {
  for (int y = 0; y < PT_LOGO_H; y++) {
    int yy = y0 + y;
    if (yy < gfx_y0 || yy >= gfx_y1) continue;
    for (int x = 0; x < PT_LOGO_W; x++) {
      int k = y * PT_LOGO_W + x, v = (pt_logo[k >> 1] >> ((k & 1) * 4)) & 15;
      if (!(v & 7)) continue;
      color_t c = v & 8 ? BUTTON : LAVENDER;
      if ((v & 7) == 7) gfx_fill(x0 + x, yy, 1, 1, c);
      else gfx_fill_alpha(x0 + x, yy, 1, 1, c, (v & 7) * 256 / 7);
    }
  }
}

/* a track's map (createThumbnail): a square a part, the start blue, checkpoints yellow, the finish red */
static void track_map(int track, int cx, int cy, int maxw, int maxh) {
  const pt_track *t = &pt_tracks[track];
  int s = maxw / t->tw;
  if (maxh / t->th < s) s = maxh / t->th;
  if (s < 1) s = 1;
  if (s > 6) s = 6;
  int w = t->tw * s, h = t->th * s;
  int x0 = cx - w / 2, y0 = cy - h / 2;
  static const uint32_t ENVC[3] = {0xFFFFFF, 0xBED8F7, 0xEDE2AF};
  color_t pal[5] = {0, HEX(ENVC[t->env]), HEX(0xE2C026), HEX(0x338CE0), HEX(0xD12929)};
  for (int j = 0; j < t->th; j++) {
    int yy = y0 + j * s;
    if (yy + s <= gfx_y0 || yy >= gfx_y1) continue;
    for (int i = 0; i < t->tw; i++) {
      int k = j * t->tw + i;
      int v = (pt_thumbs[t->thumb + k / 2] >> ((k & 1) * 4)) & 15;
      if (v) gfx_fill(x0 + i * s, yy, s, s, pal[v < 5 ? v : 1]);
    }
  }
}

/* ------------------------------------------------------------------ the race */
static const char *start_keys(void) { return "Up=start driving|Left Right=steer|Back=pause"; }

static void time_box(int x, int y, int w, int slope, const char *title, const char *t, color_t tc, bool big) {
  slant(x, y + 13, w, big ? 22 : 18, slope, SURFACE3, 256);
  if (title[0]) center(&np_font_small, x + w / 2, y + 10, title, WHITE);
  center(big ? &np_font_body : &np_font_small, x + w / 2, y + (big ? 30 : 27), t, tc);
}

static void hud(void) {
  const car_t *c = CAR;
  char s[40], s2[40], s3[40];
  int track = F->p.track;
  uint32_t best = F->p.best[track];
  int n = car_checkpoints();
  /* top left: the checkpoints collected */
  slant(-8, 4, 74, 20, 6, SURFACE, 220);
  gfx_fill(6, 9, 2, 11, WHITE);
  gfx_fill(8, 9, 7, 6, YELLOW);
  char *p = num(s, c->next_cp);
  p = cat(p, "/");
  num(p, (uint32_t)n);
  text(&np_font_body, 20, 19, s, WHITE);
  /* top right: the speed */
  slant(244, 4, 84, 20, -6, SURFACE, 220);
  float k = c->speed < 0.0f ? -c->speed : c->speed;
  num(s, (uint32_t)(k + 0.5f));
  right(&np_font_body, 286, 19, s, WHITE);
  text(&np_font_small, 289, 18, "km/h", MUTED);
  /* bottom: record, current, difference (at a checkpoint: their times there, a few seconds) */
  uint32_t cur = c->finish ? c->finish : c->frames;
  bool split = U->split_at && U->clock - U->split_at < 3500 && !c->finish;
  if (best) clock_of(s, split && U->split_cp < PT_SPLITS ? F->p.split[track][U->split_cp] : best);
  else cat(s, "- - -");
  clock_of(s2, split ? c->cp_frames[U->split_cp < 64 ? U->split_cp : 0] : cur);
  color_t dc = WHITE;
  s3[0] = 0;
  if (split && U->split_diff != 0x7FFFFFFF) diff_of(s3, U->split_diff), dc = U->split_diff < 0 ? GREEN : RED;
  else if (c->finish && best && U->finish_at) {
    int32_t d = U->new_record ? U->finish_diff : (int32_t)c->finish - (int32_t)best;
    if (d || !U->new_record) diff_of(s3, d), dc = d < 0 ? GREEN : RED;
  }
  if (!s3[0]) cat(s3, "- - -");
  time_box(6, 192, 94, 6, "Record", s, WHITE, false);
  time_box(104, 188, 112, 0, "Current", s2, WHITE, true);
  time_box(220, 192, 94, -6, "Difference", s3, dc, false);
  /* PolyTrack's hints */
  const char *h1 = NULL, *h2 = NULL;
  if (!U->started) {
    h1 = "Use the arrows to start driving.";
    h2 = U->has_ghost ? "The ghost is your best run." : NULL;
  } else if (c->finish) {
    h1 = U->new_record ? "New record!" : "Finished!";
    h2 = "Backspace: try again and improve your time.";
  } else if (c->next_cp == 0 && c->frames < 8000) {
    h1 = "Reach the first checkpoint!";
    h2 = "Checkpoints have a yellow banner.";
  } else if (U->still_ms > 2500) {
    h1 = c->has_respawn ? "OK: back to the last checkpoint." : "OK: back to the start.";
    h2 = "Backspace: start over.";
  }
  if (h1) {
    int w1 = gfx_text_width(&np_font_body, h1), w2 = h2 ? gfx_text_width(&np_font_small, h2) : 0;
    int w = (w1 > w2 ? w1 : w2) + 24;
    int hgt = h2 ? 38 : 24;
    slant(160 - w / 2, 34, w, hgt, 6, SURFACE, 215);
    center(&np_font_body, 163, 51, h1, U->new_record && c->finish ? GREEN : WHITE);
    if (h2) center(&np_font_small, 163, 66, h2, MUTED);
  }
  keys(U->started ? "OK=last checkpoint|Backspace=restart|Back=pause" : start_keys());
}

/* ------------------------------------------------------------------ screens */
int tab_rows(int tab) {
  int n = 0;
  for (int t = 0; t < PT_NTRACKS; t++) n += pt_tracks[t].env == tab;
  return n;
}
int track_of_tab(int tab, int row) {
  for (int t = 0; t < PT_NTRACKS; t++)
    if (pt_tracks[t].env == tab && row-- == 0) return t;
  return -1;
}

static void title(void) {
  for (int y = 0; y < 80; y += 4) gfx_fill_alpha(0, y, 320, 4, HEX(0x0A0F2A), 150 - y * 150 / 80);
  logo(14, 18);
  static const char *const NAMES[MENU_ROWS] = {"Play", "Car", "How to play"};
  for (int i = 0; i < MENU_ROWS; i++) button(18, 96 + i * 34, 136, NAMES[i], U->sel == i);
  text(&np_font_small, 18, 214, "Kodub's PolyTrack, made for NumPlay", WHITE);
  not_saved(190);
  keys("Up Down=choose|OK=open|Back=quit");
}

static void tracks(void) {
  page("TRACKS");
  /* the tabs */
  for (int t = 0; t < 3; t++) {
    bool on = U->tab == t;
    slant(150 + t * 56, 14, 56, 22, 6, on ? HOVER : BUTTON, 256);
    if (on) gfx_fill(156 + t * 56, 34, 44, 2, LAVENDER);
    center(&np_font_small, 180 + t * 56, 29, pt_env_name[t], on ? WHITE : MUTED);
  }
  int n = tab_rows(U->tab);
  char s[24];
  for (int r = 0; r < n; r++) {
    int t = track_of_tab(U->tab, r);
    int y = 46 + r * 25;
    bool on = U->sel == r;
    if (on) slant(6, y - 2, 172, 25, 6, LAVENDER, 256);
    slant(8, y, 168, 21, 5, on ? HOVER : SURFACE2, 256);
    text(&np_font_small, 18, y + 15, pt_tracks[t].name, WHITE);
    if (F->p.best[t]) clock_of(s, F->p.best[t]);
    else cat(s, "- - -");
    right(&np_font_small, 168, y + 15, s, F->p.best[t] ? LAVENDER : DIM);
  }
  /* the chosen track: its map and record */
  int t = track_of_tab(U->tab, U->sel);
  slant(184, 46, 132, 172, 0, SURFACE3, 230);
  center(&np_font_body, 250, 64, pt_tracks[t].name, WHITE);
  track_map(t, 250, 128, 116, 96);
  if (F->p.best[t]) {
    clock_of(s, F->p.best[t]);
    center(&np_font_small, 250, 196, "Record", MUTED);
    center(&np_font_body, 250, 211, s, WHITE);
  } else {
    center(&np_font_small, 250, 204, "Not finished yet", DIM);
  }
  not_saved(206);
  keys("Up Down=track|Left Right=season|OK=race|Back=menu");
}

static void car_screen(void) {
  header("CAR");
  slant(30, 174, 260, 46, 8, SURFACE3, 235);
  char s[24];
  char *p = cat(s, "< ");
  p = cat(p, PAINT_NAMES[F->p.paint % PT_PAINTS]);
  cat(p, " >");
  center(&np_font_body, 162, 194, s, WHITE);
  center(&np_font_small, 162, 212, F->p.view ? "View: from the cockpit" : "View: behind the car", MUTED);
  keys("Left Right=colour|Up Down=view|OK=done");
}

static void help_page(int k, bool guide) {
  page(guide ? "BEFORE YOU RACE" : "HOW TO PLAY");
  static const char *const TITLES[PT_HELP_PAGES] = {"Keys", "The race", "Records"};
  center(&np_font_body, 160, 62, TITLES[k], LAVENDER);
  if (k == 0) {
    static const char *const K[7][2] = {{"Up", "Accelerate"}, {"Down", "Brake, reverse"}, {"Left Right", "Steer"},
                                        {"OK or EXE", "Back to the last checkpoint"}, {"Backspace", "Start over"},
                                        {"Back", "Pause"}, {"Home", "Save and quit"}};
    for (int i = 0; i < 7; i++) {
      int y = 72 + i * 20;
      slant(30, y, 260, 18, 4, SURFACE2, 256);
      text(&np_font_small, 42, y + 13, K[i][0], LAVENDER);
      text(&np_font_small, 128, y + 13, K[i][1], WHITE);
    }
  } else {
    static const char *const L[2][6] = {
        {"Drive through every checkpoint, the ones", "with a yellow banner, in order, then over", "the finish line, as fast as you can.",
         "The clock starts when you start driving.", "Fell off or crashed? OK takes you back", "to the last checkpoint."},
        {"Your best time on each track is kept,", "with its checkpoint times: green when", "you are ahead of your record, red when",
         "behind. A ghost car drives your best run.", "The ghosts of the last tracks you set a", "record on are kept between games."}};
    for (int i = 0; i < 6; i++) center(&np_font_small, 160, 92 + i * 19, L[k - 1][i], WHITE);
  }
  for (int i = 0; i < PT_HELP_PAGES; i++) gfx_circle(148 + i * 12, 216, 3, i == k ? LAVENDER : DIM, 256);
  keys(guide ? (k + 1 < PT_HELP_PAGES ? "OK=next|Left Right=page|Back=tracks" : "OK=race|Left Right=page|Back=tracks")
             : "Left Right=page|Back=return");
}

static void confirm_box(const char *q, const char *note, int n, const char *const *labels) {
  dim();
  int h = 64 + n * 32;
  int y0 = 112 - h / 2;
  slant(36, y0, 248, h, 8, SURFACE, 250);
  center(&np_font_body, 162, y0 + 26, q, WHITE);
  if (note) center(&np_font_small, 162, y0 + 43, note, MUTED);
  for (int i = 0; i < n; i++) button(64, y0 + 54 + i * 32, 192, labels[i], U->sel == i);
}

static void result(void) {
  dim();
  const car_t *c = CAR;
  char s[40];
  int track = F->p.track;
  slant(30, 22, 260, 196, 8, SURFACE, 250);
  center(&np_font_small, 162, 42, pt_tracks[track].name, MUTED);
  center(&np_font_title, 162, 70, U->new_record ? "New record!" : "Finished", U->new_record ? GREEN : WHITE);
  clock_of(s, c->finish);
  center(&np_font_title, 162, 96, s, WHITE);
  if (U->new_record && U->finish_diff) {
    diff_of(s, U->finish_diff);
    center(&np_font_small, 162, 113, s, GREEN);
  } else if (!U->new_record) {
    char *p = cat(s, "Record ");
    clock_of(p, F->p.best[track]);
    center(&np_font_small, 162, 113, s, MUTED);
  }
  static const char *const L[RES_ROWS] = {"Race again", "Next track", "Tracks"};
  for (int i = 0; i < RES_ROWS; i++) button(64, 124 + i * 30, 192, L[i], U->sel == i);
  not_saved(200);
  keys("Up Down=choose|OK=select|Backspace=race again");
}

static void scene(void) {
  switch (U->screen) {
    case V_TRACKS: tracks(); break;
    case V_GUIDE: help_page(U->page, true); break;
    case V_HELP: help_page(U->page, false); break;
    case V_CAR:
      rd_draw();
      car_screen();
      break;
    case V_TITLE:
    case V_QUIT:
      rd_draw();
      title();
      if (U->screen == V_QUIT) {
        static const char *const L[2] = {"Stay", "Quit"};
        confirm_box("Quit PolyTrack?", NULL, 2, L);
        keys("Up Down=choose|OK=select|Back=return");
      }
      break;
    default:
      rd_draw();
      hud();
      if (U->screen == V_PAUSE) {
        static const char *const L[PAUSE_ROWS] = {"Resume", "Restart", "How to play", "Quit game"};
        dim();
        slant(56, 26, 208, 180, 8, SURFACE, 250);
        center(&np_font_title, 162, 56, "PAUSED", WHITE);
        for (int i = 0; i < PAUSE_ROWS; i++) button(78, 70 + i * 32, 168, L[i], U->sel == i);
        keys("Up Down=choose|OK=select|Back=resume");
      } else if (U->screen == V_LEAVE) {
        static const char *const L[3] = {"Keep racing", "Tracks", "Quit PolyTrack"};
        confirm_box("Leave the race?", "This run's time is lost.", 3, L);
        keys("Up Down=choose|OK=select|Back=pause");
      } else if (U->screen == V_RESULT) {
        result();
      }
      break;
  }
}

void pt_render(const pt_file *f, const car_t *car, const camera_t *cam, const car_look *look, const car_look *ghost,
               const pt_ui *ui) {
  F = f, CAR = car, U = ui;
  int s = ui->screen;
  if (s == V_RACE || s == V_PAUSE || s == V_LEAVE || s == V_RESULT) {
    /* (the cars first: they always have room) */
    rd_begin(cam, W.t->env, W.t->sun, false);
    if (!f->p.view) {
      rd_car(look);
      rd_shadow(look, car);
    }
    if (ghost) rd_car(ghost);
    rd_track();
  } else if (s == V_TITLE || s == V_QUIT || s == V_CAR) {
    /* the track's start from a camera going round the car */
    camera_t c;
    float a = (float)(ui->clock % 40000u) * (2.0f * PT_PI / 40000.0f);
    v3 at = car->pos;
    float d = s == V_CAR ? 6.5f : 9.0f;
    v3 eye = V3(at.x + pt_cosf(a) * d, at.y + (s == V_CAR ? 1.8f : 3.2f), at.z + pt_sinf(a) * d);
    v3 z = vnorm(vsub(eye, vadd(at, V3(0.0f, 0.5f, 0.0f))));
    v3 x = vnorm(vcross(V3(0.0f, 1.0f, 0.0f), z));
    c.pos = eye, c.right = x, c.up = vcross(z, x), c.fwd = vneg(z), c.fov = 60.0f;
    rd_begin(&c, W.t->env, W.t->sun, false);
    rd_car(look);
    rd_shadow(look, car);
    rd_track();
  }
  color_t *old = gfx_buf;
  gfx_buf = strip;
  for (int y = 0; y < SCREEN_H; y += STRIP) {
    gfx_y0 = y, gfx_y1 = y + STRIP;
    scene();
    plat_push(0, y, SCREEN_W, STRIP, strip);
  }
  gfx_buf = old;
}
