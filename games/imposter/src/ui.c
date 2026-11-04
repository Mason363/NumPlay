/* Imposter: the screens, drawn in strips with NumPlay's launcher (launcher/src/gfx.c and its Nunito fonts), and
 * what the keys do on each. */
#include "ui.h"
#include <string.h>
#include "../../../launcher/src/gfx.h"
#include "game.h"
#include "plat.h"
#ifdef HOST
#include <stdio.h>
extern int host_trace;
#endif

/* the launcher's drawing sends its strips here */
void np_push(int x, int y, int w, int h, const color_t *px) { plat_push(x, y, w, h, px); }
#ifndef NP_PORT /* (the other targets bring their own: platform/common/np_alloc.c) */
void *np_alloc(uint32_t size) { /* (gfx_init's: never called, the strip is ours) */
  (void)size;
  return 0;
}
#endif
static color_t strip[SCREEN_W * STRIP_H];

#define WHITE 0xFFFF
#define GOLD HEX(0xFFC83D)
#define INK HEX(0x171B3D)     /* text on gold */
#define SOFT HEX(0xB4BCE8)    /* quieter text */
#define CARD HEX(0x2C3570)
#define CARD_ON HEX(0x3D4894)
#define BAR HEX(0x0B0E22)
#define RED_TEXT HEX(0xFF6F6F)
#define BG_TOP 0x262D5E
#define BG_BOT 0x10142B
#define KEY_Y 222 /* the key line */

enum { SC_HELP, SC_SETUP, SC_PLAYERS, SC_NAME, SC_CATS, SC_PASS, SC_ROLE, SC_TALK, SC_REVEAL };
enum { DL_NONE, DL_PAUSE, DL_QUIT, DL_REVEAL, DL_REMOVE };
enum { ROW_START, ROW_PLAYERS, ROW_CATS, ROW_IMPS, ROW_HINT, ROW_TIMER, ROW_HELP, ROW_QUIT, N_ROWS };

static struct {
  int screen, dialog, dsel, dialog_back, help_back;
  int setup_sel, players_sel, cats_sel, cats_top;
  int name_idx, name_len, gr, gc; /* the name being typed; the letter chosen on the screen (row, column) */
  bool name_new, dirty;
  char name[NAME_MAX + 1];
  uint32_t cmask; /* the categories ticked on their screen */
  uint32_t now, msg_until, prev_keys, prev_letters, stale, repeat_at;
  uint32_t guard_until; /* a role shows, or the next player is named: OK waits a moment (a double press) */
  int shown_sec;
} u;

/* ------------------------------------------------------------ text */
static char *num(char *o, int v) { /* v in o; the end of it */
  char t[8];
  int n = 0;
  do t[n++] = (char)('0' + v % 10); while (v /= 10);
  while (n) *o++ = t[--n];
  *o = 0;
  return o;
}
static char *cat(char *o, const char *s) {
  while (*s) *o++ = *s++;
  *o = 0;
  return o;
}

/* ---- big letters: the title font scaled, each pixel sampled between the font's own and sharpened */
#define BIG (&np_font_title)
static const np_glyph_t *glyph_of(char ch) {
  unsigned i = (unsigned)(uint8_t)ch - BIG->first;
  return &BIG->glyphs[i < BIG->count ? i : (unsigned)('?' - BIG->first)];
}
static int width1(const char *s, int n) { /* at the font's size */
  int w = 0;
  for (int i = 0; i < n; i++) w += glyph_of(s[i])->advance;
  return w;
}
static int cap_height(void) { return -glyph_of('H')->y; }

static int cov_at(const uint8_t *d, int w, int h, int i, int j) {
  if ((unsigned)i >= (unsigned)w || (unsigned)j >= (unsigned)h) return 0;
  int n = j * w + i;
  return (d[n >> 1] >> ((n & 1) * 4)) & 15;
}

/* Catmull-Rom's weights at t (in 1/256), in 1/256 */
static void cubic(int t, int32_t *w) {
  int32_t t2 = (int32_t)t * t >> 8, t3 = t2 * t >> 8;
  w[0] = (-t3 + 2 * t2 - t) / 2;
  w[1] = (3 * t3 - 5 * t2 + 512) / 2;
  w[2] = (-3 * t3 + 4 * t2 + t) / 2;
  w[3] = 256 - w[0] - w[1] - w[2];
}

#ifndef BIG_SHARP
#define BIG_SHARP 2
#endif
/* n letters of s, s256/256 times the font's size, from x on the baseline: each pixel the font's coverage
 * between its pixels (bicubic), sharpened as much as the letters grow */
static void big_run(const char *s, int n, int x, int base, int s256, color_t c) {
#ifdef __TICE__
  if (gfx_listing) { /* (what it depends on, in the strips' lists) */
    const int v[] = {n, x, base, s256, c};
    gfx_list(v, sizeof v);
    gfx_list(s, (unsigned)n);
    return;
  }
#endif
  int32_t pen = (int32_t)x * 256; /* in 1/256 pixels */
  int32_t du = (int32_t)(((uint32_t)1 << 24) / (uint32_t)s256); /* the font's 1/65536 pixels a screen pixel */
  int sharp = 16 + (s256 - 256) * BIG_SHARP / 64;  /* (in 1/16) */
  for (int k = 0; k < n; k++) {
    const np_glyph_t *g = glyph_of(s[k]);
    if (g->w) {
      const uint8_t *d = BIG->data + g->offset;
      int gw = g->w, gh = g->h;
      int32_t gx = pen + (int32_t)g->x * s256, gy = (int32_t)base * 256 + (int32_t)g->y * s256;
      int x0 = (int)((gx - s256) >> 8), x1 = (int)((gx + (int32_t)(gw + 1) * s256) >> 8) + 1;
      int y0 = (int)((gy - s256) >> 8), y1 = (int)((gy + (int32_t)(gh + 1) * s256) >> 8) + 1;
      if (y0 < gfx_y0) y0 = gfx_y0;
      if (y1 > gfx_y1) y1 = gfx_y1;
      if (x0 < 0) x0 = 0;
      if (x1 > SCREEN_W) x1 = SCREEN_W;
      for (int Y = y0; Y < y1; Y++) {
        int32_t v = ((int32_t)Y * 256 + 128 - gy) * 256 / s256 - 128; /* in the font's 1/256 pixels */
        int j = (int)(v >> 8);
        int32_t wy[4], wx[4];
        cubic((int)(v & 255), wy);
        int32_t uu = (int32_t)(((int32_t)x0 * 256 + 128 - gx) * (int32_t)65536 / s256) - 32768;
        color_t *p = gfx_buf + (Y - gfx_y0) * SCREEN_W;
        for (int X = x0; X < x1; X++, uu += du) {
          int i = (int)(uu >> 16);
          cubic((int)((uu >> 8) & 255), wx);
          int32_t val = 0;
          for (int r = 0; r < 4; r++) {
            int jj = j - 1 + r;
            if ((unsigned)jj >= (unsigned)gh) continue;
            int32_t row = 0;
            for (int q = 0; q < 4; q++) row += wx[q] * cov_at(d, gw, gh, i - 1 + q, jj);
            val += row * wy[r];
          }
          val >>= 8; /* 0 to 3840, a little past at the edges */
          if (val <= 0) continue;
          val = ((val - 1920) * sharp >> 4) + 1920;
          if (val <= 0) continue;
          int a = val >= 3840 ? 32 : (int)((val + 60) / 120);
          p[X] = gfx_mix(p[X], c, a);
        }
      }
    }
    pen += (int32_t)g->advance * s256;
  }
}

/* s as big as it goes (s256max at most) in a box maxw wide and maxh high, on one line or two, its middle at
 * (cx, cy) */
static void big_text(const char *s, int cx, int cy, int maxw, int maxh, int smax, color_t c) {
  int n = (int)strlen(s), lh = BIG->ascent + BIG->descent, w = width1(s, n), cap = cap_height();
  if (!w) return;
  int s1 = smax;
  if (maxw * 256 / w < s1) s1 = maxw * 256 / w;
  if (maxh * 256 / lh < s1) s1 = maxh * 256 / lh;
  /* two lines, split at the space that evens them best */
  int split = -1, best = 1 << 20;
  for (int k = 1; k < n - 1; k++)
    if (s[k] == ' ') {
      int a = width1(s, k), b = width1(s + k + 1, n - k - 1), m = a > b ? a : b;
      if (m < best) best = m, split = k;
    }
  if (split > 0) {
    int s2 = smax, gap = lh + cap; /* (two baselines apart, and the lines) */
    if (maxw * 256 / best < s2) s2 = maxw * 256 / best;
    if (maxh * 256 / gap < s2) s2 = maxh * 256 / gap;
    if (s2 > s1 * 9 / 8) {
      int step = (lh - 2) * s2 / 256, b0 = cy - step / 2 + cap * s2 / 512;
      int wa = width1(s, split) * s2 / 256, wb = width1(s + split + 1, n - split - 1) * s2 / 256;
      big_run(s, split, cx - wa / 2, b0, s2, c);
      big_run(s + split + 1, n - split - 1, cx - wb / 2, b0 + step, s2, c);
      return;
    }
  }
  big_run(s, n, cx - w * s1 / 256 / 2, cy + cap * s1 / 512, s1, c);
}

/* ---- the pieces of every screen */
static void background(uint32_t top, uint32_t bottom) { gfx_vgrad(0, 0, SCREEN_W, SCREEN_H, top, bottom); }

/* the keys at the bottom: key, what it does, key, what it does... */
static void keyline(const char *const *p) {
  if (gfx_y1 <= KEY_Y) return;
  gfx_fill(0, KEY_Y, SCREEN_W, SCREEN_H - KEY_Y, BAR);
  const np_font_t *f = &np_font_small;
  int w = 0;
  for (int i = 0; p[i]; i += 2)
    w += (i ? 16 : 0) + (p[i][0] ? gfx_text_width(f, p[i]) + 5 : 0) + gfx_text_width(f, p[i + 1]);
  int x = SCREEN_W / 2 - w / 2;
  for (int i = 0; p[i]; i += 2) {
    if (i) x += 16;
    if (p[i][0]) x = gfx_text(f, x, KEY_Y + 13, p[i], GOLD, 256) + 5;
    x = gfx_text(f, x, KEY_Y + 13, p[i + 1], SOFT, 256);
  }
}
#define KEYS(...) keyline((const char *const[]){__VA_ARGS__, 0})

static void heading(const char *s) { gfx_text_center(&np_font_title, SCREEN_W / 2, 27, s, WHITE, 256); }

static void button(int x, int y, int w, int h, const char *s, bool on) {
  gfx_rrect(x, y, w, h, h * 2 / 5, on ? GOLD : CARD, 256);
  gfx_text_center(&np_font_title, x + w / 2, y + h / 2 + 7, s, on ? INK : WHITE, 256);
}

/* the frame round what's chosen in a list */
static void focus(int x, int y, int w, int h, int r) {
  gfx_rrect(x, y, w, h, r, CARD_ON, 256);
  gfx_rrect_outline(x, y, w, h, r, 2, GOLD, 256);
}

static void chip(int cx, int y, const char *s, color_t fg, int alpha) {
  int w = gfx_text_width(&np_font_body, s) + 24;
  gfx_rrect(cx - w / 2, y, w, 24, 12, WHITE, alpha);
  gfx_text_center(&np_font_body, cx, y + 16, s, fg, 256);
}

/* ------------------------------------------------------------ the screens */
static void open_screen(int s) {
  if ((s == SC_PASS && u.screen == SC_ROLE) || s == SC_ROLE) u.guard_until = u.now + 600;
  u.screen = s;
  u.dirty = true;
}
static bool guarded(void) { return (int32_t)(u.guard_until - u.now) > 0; }

static void open_name(int idx, bool fresh) {
  u.name_idx = idx, u.name_new = fresh;
  memset(u.name, 0, sizeof u.name);
  if (!fresh) memcpy(u.name, sv.names[idx], NAME_MAX);
  u.name_len = (int)strlen(u.name);
  u.gr = u.gc = 0, u.msg_until = 0;
  open_screen(SC_NAME);
}

static void open_cats(void) {
  u.cmask = sv.cats ? sv.cats : ((uint32_t)1 << N_CATS) - 1;
  u.cats_sel = 0, u.cats_top = 0;
  open_screen(SC_CATS);
}

static void open_help(int back) {
  u.help_back = back;
  open_screen(SC_HELP);
}

/* the round's screen for where it stands */
static void open_round(void) {
  open_screen(sv.phase == PH_TALK ? SC_TALK : sv.phase == PH_REVEAL ? SC_REVEAL : SC_PASS);
  u.shown_sec = -1;
}

static void open_dialog(int d, int sel) {
  u.dialog_back = u.dialog;
  u.dialog = d, u.dsel = sel;
  u.dirty = true;
}

void app_init(uint32_t now) {
  memset(&u, 0, sizeof u);
  gfx_buf = strip;
  rnd_seed(plat_random() ^ now * 2654435761u);
  game_load();
  u.now = now;
  uint32_t l;
  u.stale = plat_keys(&l); /* keys still down from the launcher wait to be let go */
  if (sv.phase) open_round();
  else open_screen(SC_SETUP);
  if (!(sv.flags & 1)) {
    sv.flags |= 1;
    game_save();
    open_help(u.screen);
  }
}

bool app_dirty(void) { return u.dirty; }

/* ---- Setup */
static bool row_changes(int r) { return (r == ROW_IMPS && max_imposters(sv.nplayers) > 1) || r == ROW_HINT || r == ROW_TIMER; }

static void setup_change(int r, int d, bool wrap) {
  if (r == ROW_IMPS) {
    int m = max_imposters(sv.nplayers), v = imposters_now() + d;
    if (v < 1) v = wrap ? m : 1;
    if (v > m) v = wrap ? 1 : m;
    sv.imposters = (uint8_t)v;
  } else if (r == ROW_HINT) {
    sv.hint = !sv.hint;
  } else if (r == ROW_TIMER) {
    int v = sv.timer + d;
    if (v < 0) v = wrap ? N_TIMERS - 1 : 0;
    if (v >= N_TIMERS) v = wrap ? 0 : N_TIMERS - 1;
    sv.timer = (uint8_t)v;
  }
  game_save();
}

static bool setup_key(uint32_t hit) {
  int dy = !!(hit & KB_DOWN) - !!(hit & KB_UP), dx = !!(hit & KB_RIGHT) - !!(hit & KB_LEFT);
  u.setup_sel = (u.setup_sel + dy + N_ROWS) % N_ROWS;
  if (dx && row_changes(u.setup_sel)) setup_change(u.setup_sel, dx, false);
  if (hit & KB_BACK) open_dialog(DL_QUIT, 0);
  else if (hit & (KB_OK | KB_EXE)) switch (u.setup_sel) {
    case ROW_START:
      if (sv.nplayers < MIN_PLAYERS) {
        u.players_sel = sv.nplayers;
        open_screen(SC_PLAYERS);
      } else {
        round_new();
        open_round();
      }
      break;
    case ROW_PLAYERS: u.players_sel = 0; open_screen(SC_PLAYERS); break;
    case ROW_CATS: open_cats(); break;
    case ROW_HELP: open_help(SC_SETUP); break;
    case ROW_QUIT: open_dialog(DL_QUIT, 0); break;
    default:
      if (row_changes(u.setup_sel)) setup_change(u.setup_sel, 1, true);
  }
  return true;
}

static void setup_row(int r, const char *label, const char *value) {
  int y = 74 + (r - 1) * 21;
  bool on = u.setup_sel == r, arrows = on && row_changes(r);
  if (on) focus(16, y, 288, 20, 8);
  gfx_text(&np_font_body, 28, y + 14, label, on ? WHITE : SOFT, 256);
  if (!value) return;
  int rx = arrows ? 280 : 292;
  gfx_text_right(&np_font_body, rx, y + 14, value, on ? GOLD : WHITE, 256);
  if (arrows) {
    gfx_text_right(&np_font_body, rx - gfx_text_width(&np_font_body, value) - 7, y + 14, "<", GOLD, 256);
    gfx_text(&np_font_body, 286, y + 14, ">", GOLD, 256);
  }
}

static void draw_setup(void) {
  char a[16], b[16], t[16];
  background(BG_TOP, BG_BOT);
  heading("Imposter");
  bool ready = sv.nplayers >= MIN_PLAYERS;
  button(16, 38, 288, 30, ready ? "Start" : "Add players", u.setup_sel == ROW_START);
  if (sv.nplayers) num(a, sv.nplayers);
  else cat(a, "None");
  if (sv.cats) cat(num(b, cats_chosen()), " of 30");
  else cat(b, "All");
  if (sv.timer) cat(num(t, timer_minutes[sv.timer]), " min");
  else cat(t, "Off");
  char im[4];
  num(im, imposters_now());
  setup_row(ROW_PLAYERS, "Players", a);
  setup_row(ROW_CATS, "Categories", b);
  setup_row(ROW_IMPS, "Imposters", im);
  setup_row(ROW_HINT, "Imposter sees", sv.hint ? "The category" : "Nothing");
  setup_row(ROW_TIMER, "Timer", t);
  setup_row(ROW_HELP, "How to play", 0);
  setup_row(ROW_QUIT, "Quit game", 0);
  switch (u.setup_sel) {
    case ROW_START: KEYS("OK", ready ? "start" : "add players", "Back", "quit"); break;
    case ROW_PLAYERS: case ROW_CATS: case ROW_HELP: KEYS("OK", "open", "Back", "quit"); break;
    case ROW_IMPS:
      if (max_imposters(sv.nplayers) > 1) KEYS("Left Right", "change", "Back", "quit");
      else KEYS("", "2 imposters from 5 players");
      break;
    case ROW_HINT: case ROW_TIMER: KEYS("Left Right", "change", "Back", "quit"); break;
    default: KEYS("OK", "quit");
  }
}

/* ---- Players: two columns of six */
static int players_last(void) { return sv.nplayers < MAX_PLAYERS ? sv.nplayers : MAX_PLAYERS - 1; }

static bool players_key(uint32_t hit) {
  int s = u.players_sel, last = players_last();
  if (hit & KB_UP) s = s > 0 ? s - 1 : last;
  if (hit & KB_DOWN) s = s < last ? s + 1 : 0;
  if (hit & (KB_LEFT | KB_RIGHT)) {
    int t = s < 6 ? s + 6 : s - 6;
    if (t <= last) s = t;
    else if (s < 6 && last >= 6) s = last;
  }
  u.players_sel = s;
  if (hit & KB_BACK) {
    u.setup_sel = ROW_START;
    open_screen(SC_SETUP);
  } else if (hit & (KB_OK | KB_EXE)) {
    open_name(s, s >= sv.nplayers);
  } else if ((hit & KB_ERASE) && s < sv.nplayers) {
    open_dialog(DL_REMOVE, 0);
  }
  return true;
}

static void draw_players(void) {
  background(BG_TOP, BG_BOT);
  heading("Players");
  gfx_text_right(&np_font_small, 304, 25, "3 to 12", SOFT, 256);
  for (int i = 0; i <= players_last(); i++) {
    int x = 16 + i / 6 * 148, y = 40 + i % 6 * 29;
    bool on = u.players_sel == i;
    if (on) focus(x, y, 140, 26, 9);
    else gfx_rrect(x, y, 140, 26, 9, i < sv.nplayers ? CARD : HEX(0x1D2350), 256);
    if (i < sv.nplayers) {
      char n[4];
      num(n, i + 1);
      gfx_text_center(&np_font_small, x + 14, y + 17, n, on ? GOLD : SOFT, 256);
      gfx_text(&np_font_body, x + 28, y + 18, sv.names[i], WHITE, 256);
    } else {
      gfx_text(&np_font_body, x + 28, y + 18, "+ Add player", on ? GOLD : SOFT, 256);
    }
  }
  if (u.players_sel < sv.nplayers) KEYS("OK", "rename", "Backspace", "remove", "Back", "done");
  else KEYS("OK", "add", "Back", "done");
}

/* ---- a name: typed with the letters on the keys, or chosen among the screen's, laid out as on the keys */
static const uint8_t grid_cols[6] = {6, 6, 5, 5, 5, 2};
static const uint8_t grid_first[6] = {0, 6, 12, 17, 22, 27};
#define CELL_SPACE 26
#define CELL_ERASE 27
#define CELL_DONE 28

static void type_letter(int l) { /* 0 to 25, 26 a space */
  if (u.name_len >= NAME_MAX) return;
  if (l == CELL_SPACE) {
    if (!u.name_len || u.name[u.name_len - 1] == ' ') return;
    u.name[u.name_len++] = ' ';
  } else {
    bool up = !u.name_len || u.name[u.name_len - 1] == ' ';
    u.name[u.name_len++] = (char)((up ? 'A' : 'a') + l);
  }
  u.name[u.name_len] = 0;
}
static void erase_letter(void) {
  if (u.name_len) u.name[--u.name_len] = 0;
}

static bool same_name(const char *a, const char *b) {
  for (; *a && *b; a++, b++)
    if ((*a | 32) != (*b | 32)) return false;
  return *a == *b;
}

/* done with the name: kept (and the next one opened, after Done on a new player), or the screen's message */
static void name_done(bool next) {
  while (u.name_len && u.name[u.name_len - 1] == ' ') u.name[--u.name_len] = 0;
  bool taken = false;
  for (int i = 0; i < sv.nplayers; i++) taken |= i != u.name_idx && same_name(sv.names[i], u.name);
  if (u.name_len && taken && next) { /* (Back leaves it as it was) */
    u.msg_until = u.now + 1500;
    return;
  }
  if (u.name_len && !taken) {
    memcpy(sv.names[u.name_idx], u.name, NAME_MAX + 1);
    if (u.name_new) sv.nplayers++;
    game_save();
    if (next && u.name_new && sv.nplayers < MAX_PLAYERS) {
      open_name(sv.nplayers, true);
      return;
    }
  }
  u.players_sel = u.name_new ? players_last() : u.name_idx;
  open_screen(SC_PLAYERS);
}

static bool name_key(uint32_t hit, uint32_t letters) {
  for (int l = 0; l < 27; l++)
    if (letters >> l & 1) type_letter(l);
  if (hit & KB_UP) u.gc = (2 * u.gc + 1) * grid_cols[(u.gr + 5) % 6] / (2 * grid_cols[u.gr]), u.gr = (u.gr + 5) % 6;
  if (hit & KB_DOWN) u.gc = (2 * u.gc + 1) * grid_cols[(u.gr + 1) % 6] / (2 * grid_cols[u.gr]), u.gr = (u.gr + 1) % 6;
  if (hit & KB_LEFT) u.gc = (u.gc + grid_cols[u.gr] - 1) % grid_cols[u.gr];
  if (hit & KB_RIGHT) u.gc = (u.gc + 1) % grid_cols[u.gr];
  if (hit & KB_ERASE) erase_letter();
  if (hit & KB_OK) {
    int cell = grid_first[u.gr] + u.gc;
    if (cell == CELL_ERASE) erase_letter();
    else if (cell == CELL_DONE) name_done(true);
    else type_letter(cell);
  } else if (hit & KB_EXE) {
    name_done(true);
  } else if (hit & KB_BACK) {
    name_done(false);
  }
  return true;
}

static void draw_name(void) {
  char t[16];
  background(BG_TOP, BG_BOT);
  if ((int32_t)(u.msg_until - u.now) > 0) gfx_text_center(&np_font_title, SCREEN_W / 2, 27, "Already a player", RED_TEXT, 256);
  else num(cat(t, "Player "), u.name_idx + 1), heading(t);
  gfx_rrect(16, 36, 288, 44, 12, HEX(0x0C1029), 256);
  gfx_rrect_outline(16, 36, 288, 44, 12, 2, HEX(0x46519C), 256);
  int s = 420, cap = cap_height(), w = width1(u.name, u.name_len) * s / 256, x = SCREEN_W / 2 - (w + 6) / 2;
  int base = 58 + cap * s / 512;
  big_run(u.name, u.name_len, x, base, s, WHITE);
  gfx_fill(x + w + 3, base - cap * s / 256, 3, cap * s / 256, GOLD); /* where the next letter goes */
  bool next = u.name_new && u.name_idx < MAX_PLAYERS - 1; /* (Done opens the next player's) */
  for (int r = 0; r < 6; r++)
    for (int c = 0; c < grid_cols[r]; c++) {
      int cell = grid_first[r] + c, x0 = 16 + c * 288 / grid_cols[r], cw = 288 / grid_cols[r] - 3, y = 86 + r * 22;
      bool on = u.gr == r && u.gc == c;
      gfx_rrect(x0, y, cw, 20, 7, on ? GOLD : CARD, 256);
      char l[2] = {(char)('A' + cell), 0};
      const char *label = cell == CELL_SPACE ? "Space" : cell == CELL_ERASE ? "Erase" :
                          cell == CELL_DONE ? (next ? "Next" : "Done") : l;
      const np_font_t *f = cell < CELL_SPACE ? &np_font_title : &np_font_body;
      gfx_text_center(f, x0 + cw / 2, y + (cell < CELL_SPACE ? 17 : 14), label, on ? INK : WHITE, 256);
    }
  const char *first = plat_typing() ? "Letter keys" : "OK", *what = plat_typing() ? "type" : "pick";
  if (next) KEYS(first, what, "EXE", "next player", "Back", "done");
  else KEYS(first, what, "Backspace", "erase", "EXE", "done");
}

/* ---- Categories: two columns, ten rows shown */
#define CAT_ROWS 10
static bool cats_key(uint32_t hit) {
  const uint32_t all = ((uint32_t)1 << N_CATS) - 1;
  int s = u.cats_sel;
  if (hit & KB_UP) s = s < 0 ? N_CATS - 2 : s < 2 ? -1 : s - 2;
  if (hit & KB_DOWN) s = s < 0 ? 0 : s + 2 >= N_CATS ? -1 : s + 2;
  if ((hit & (KB_LEFT | KB_RIGHT)) && s >= 0) s ^= 1;
  u.cats_sel = s;
  int row = s < 0 ? 0 : s / 2;
  if (row < u.cats_top) u.cats_top = row;
  if (row >= u.cats_top + CAT_ROWS) u.cats_top = row - CAT_ROWS + 1;
  if (hit & (KB_OK | KB_EXE)) {
    if (s < 0) u.cmask = u.cmask == all ? 0 : all;
    else u.cmask ^= (uint32_t)1 << s;
    sv.cats = u.cmask == all ? 0 : u.cmask; /* (none ticked: all, once left) */
    game_save();
  }
  if (hit & KB_BACK) {
    u.setup_sel = ROW_CATS;
    open_screen(SC_SETUP);
  }
  return true;
}

static const uint8_t tick[40] = {0,   0,   0,   16,  73,  0,   0,   0,   193, 191, 0,   0,  16,  252,
                                 62,  182, 3,   144, 239, 3,   250, 62,  249, 62,  0,   193, 255, 255,
                                 6,   0,   16,  252, 111, 0,   0,   0,   145, 5,   0,   0};
static void checkbox(int x, int y, bool on, bool focused) {
  if (on) {
    gfx_rrect(x, y, 14, 14, 4, GOLD, 256);
    gfx_mask(tick, 10, 8, x + 2, y + 3, INK, 256);
  } else {
    gfx_rrect_outline(x, y, 14, 14, 4, 2, focused ? GOLD : SOFT, 256);
  }
}

static void draw_cats(void) {
  const uint32_t all = ((uint32_t)1 << N_CATS) - 1;
  background(BG_TOP, BG_BOT);
  heading("Categories");
  bool on = u.cats_sel < 0;
  gfx_rrect(242, 8, 64, 24, 12, on ? GOLD : CARD, 256);
  if (on) gfx_rrect_outline(242, 8, 64, 24, 12, 2, GOLD, 256);
  checkbox(250, 13, u.cmask == all, on);
  gfx_text(&np_font_body, 270, 25, "All", on ? INK : WHITE, 256);
  for (int r = 0; r < CAT_ROWS; r++)
    for (int c = 0; c < 2; c++) {
      int k = (u.cats_top + r) * 2 + c;
      if (k >= N_CATS) continue;
      int x = c ? 166 : 12, y = 38 + r * 18;
      bool f = u.cats_sel == k;
      if (f) focus(x, y, c ? 138 : 150, 17, 6);
      checkbox(x + 5, y + 1, u.cmask >> k & 1, f);
      gfx_text(&np_font_body, x + 24, y + 13, categories[k].name, f ? WHITE : SOFT, 256);
    }
  /* where the list is */
  int rows = (N_CATS + 1) / 2, h = CAT_ROWS * 18 - 2;
  gfx_rrect(310, 38, 3, h, 1, HEX(0x262E62), 256);
  gfx_rrect(310, 38 + u.cats_top * h / rows, 3, CAT_ROWS * h / rows, 1, SOFT, 256);
  if (on) KEYS("OK", u.cmask == all ? "none" : "all", "Back", "done");
  else KEYS("OK", "on or off", "Back", "done");
}

/* ---- How to play */
static bool help_key(uint32_t hit) {
  if (hit & (KB_OK | KB_EXE | KB_BACK)) open_screen(u.help_back);
  return true;
}
static void draw_help(void) {
  background(BG_TOP, BG_BOT);
  heading("How to play");
  int y = gfx_paragraph(&np_font_body, 160, 52, 292, 15, "Everyone gets the same secret word, except the imposter.",
                        WHITE, 256);
  y = gfx_paragraph(&np_font_body, 160, y + 7, 292, 15,
                    "Pass the calculator around. Each player sees their role alone, then hides it.", WHITE, 256);
  y = gfx_paragraph(&np_font_body, 160, y + 7, 292, 15,
                    "In turn, everyone says one word about the secret. Then vote out loud for the imposter.", WHITE,
                    256);
  gfx_paragraph(&np_font_body, 160, y + 7, 292, 15, "Caught, the imposter loses. Not caught, the imposter wins.",
                WHITE, 256);
  gfx_text_center(&np_font_small, 160, 198, "Inspired by the party game Imposter.", SOFT, 256);
  gfx_text_center(&np_font_small, 160, 211, "Made by Mason Chen as part of NumPlay.", SOFT, 256);
  KEYS("OK", "done");
}

/* ---- the round */
static bool pass_key(uint32_t hit) {
  if (hit & KB_BACK) open_dialog(DL_PAUSE, 0);
  else if ((hit & (KB_OK | KB_EXE)) && !guarded()) open_screen(SC_ROLE);
  return true;
}
static void draw_pass(void) {
  char t[16];
  background(BG_TOP, BG_BOT);
  gfx_text_center(&np_font_title, 160, 76, "Pass to", SOFT, 256);
  big_text(sv.names[sv.turn], 160, 118, 288, 64, 768, WHITE);
  num(cat(num(t, sv.turn + 1), " of "), sv.nplayers);
  gfx_text_center(&np_font_body, 160, 172, t, SOFT, 256);
  KEYS("OK", "see your role", "Back", "menu");
}

static bool role_key(uint32_t hit) {
  if (!(hit & (KB_OK | KB_EXE | KB_BACK)) || guarded()) return true;
  if (++sv.turn >= sv.nplayers) {
    sv.turn = 0, sv.phase = PH_TALK;
    game_save();
    open_round();
  } else {
    game_save();
    open_screen(SC_PASS);
  }
  return true;
}
static void draw_role(void) {
  char w[WORD_MAX + 1], h[WORD_MAX + 8];
  int c = word_text(sv.word, w);
  if (is_imposter(sv.turn)) {
    background(0xE3343F, 0x7A0D1B);
    gfx_text_center(&np_font_title, 160, 66, "You are the", WHITE, 256);
    big_text("Imposter", 160, 114, 296, 64, 768, WHITE);
    if (sv.hint) cat(cat(h, "Hint: "), categories[c].name), chip(160, 160, h, WHITE, 60);
  } else {
    background(0x2C6CE3, 0x14388C);
    gfx_text_center(&np_font_body, 160, 46, "The secret word", HEX(0xDCE6FF), 256);
    big_text(w, 160, 108, 296, 96, 720, WHITE);
    chip(160, 166, categories[c].name, WHITE, 60);
  }
  KEYS("OK", "hide");
}

static bool talk_key(uint32_t hit) {
  if (hit & KB_BACK) open_dialog(DL_PAUSE, 0);
  else if (hit & (KB_OK | KB_EXE)) open_dialog(DL_REVEAL, 0);
  return true;
}
static void draw_talk(void) {
  char t[24];
  background(BG_TOP, BG_BOT);
  int y = sv.timer ? 0 : 26; /* (without the timer, the rest in the middle) */
  cat(cat(t, sv.names[sv.starter]), " starts");
  big_text(t, 160, 70 + y, 296, 70, 560, WHITE);
  gfx_text_center(&np_font_body, 160, 128 + y, "One word each, in turn. Then vote out loud.", SOFT, 256);
  if (sv.timer) {
    if (sv.time_left) {
      int s = (int)((sv.time_left + 999) / 1000);
      char *e = num(t, s / 60);
      *e++ = ':', *e++ = (char)('0' + s % 60 / 10), *e++ = (char)('0' + s % 10), *e = 0;
      big_text(t, 160, 178, 200, 50, 640, s <= 10 ? GOLD : WHITE);
    } else {
      big_text("Time to vote", 160, 178, 280, 40, 400, GOLD);
    }
  }
  KEYS("OK", "reveal", "Back", "menu");
}

static bool reveal_key(uint32_t hit) {
  if (hit & (KB_OK | KB_EXE)) {
    round_new();
    open_round();
  } else if (hit & KB_BACK) {
    round_end();
    u.setup_sel = ROW_START;
    open_screen(SC_SETUP);
  }
  return true;
}
static void draw_reveal(void) {
  char w[WORD_MAX + 1], who[48];
  int c = word_text(sv.word, w), k = 0, n = imposters_now();
  background(BG_TOP, BG_BOT);
  gfx_text_center(&np_font_body, 160, 38, "The word was", SOFT, 256);
  big_text(w, 160, 70, 296, 50, 560, WHITE);
  chip(160, 104, categories[c].name, WHITE, 40);
  char *o = who;
  for (int p = 0; p < sv.nplayers; p++) {
    if (!is_imposter(p)) continue;
    if (k) o = cat(o, k == n - 1 ? " and " : ", ");
    o = cat(o, sv.names[p]);
    k++;
  }
  gfx_text_center(&np_font_body, 160, 154, k > 1 ? "The imposters were" : "The imposter was", SOFT, 256);
  big_text(who, 160, 186, 296, 50, 560, RED_TEXT);
  KEYS("OK", "next round", "Back", "settings");
}

/* ------------------------------------------------------------ dialogs */
static const char *const pause_items[] = {"Resume", "New round", "Settings", "Quit game"};
static const char *const quit_items[] = {"Quit", "Cancel"};
static const char *const reveal_items[] = {"Reveal", "Not yet"};
static const char *const remove_items[] = {"Remove", "Cancel"};

static int dialog_items(const char *const **items, char *q) {
  switch (u.dialog) {
    case DL_PAUSE: *items = pause_items, cat(q, "Paused"); return 4;
    case DL_QUIT: *items = quit_items, cat(q, "Quit Imposter?"); return 2;
    case DL_REVEAL: *items = reveal_items, cat(q, imposters_now() > 1 ? "Reveal the imposters?" : "Reveal the imposter?"); return 2;
    default: *items = remove_items, cat(cat(cat(q, "Remove "), sv.names[u.players_sel]), "?"); return 2;
  }
}

static void close_dialog(void) {
  u.dialog = u.dialog_back, u.dialog_back = DL_NONE, u.dsel = 0;
  u.dirty = true;
}

static bool dialog_key(uint32_t hit) {
  const char *const *items;
  char q[32];
  int n = dialog_items(&items, q);
  int dy = !!(hit & KB_DOWN) - !!(hit & KB_UP);
  u.dsel = (u.dsel + dy + n) % n;
  if (hit & KB_BACK) {
    close_dialog();
    return true;
  }
  if (!(hit & (KB_OK | KB_EXE))) return true;
  int d = u.dialog, s = u.dsel;
  u.dialog = DL_NONE, u.dialog_back = DL_NONE;
  if (d == DL_PAUSE) {
    if (s == 1) round_new(), open_round();
    else if (s == 2) round_end(), u.setup_sel = ROW_START, open_screen(SC_SETUP);
    else if (s == 3) u.dialog = DL_NONE, open_dialog(DL_QUIT, 0), u.dialog_back = DL_PAUSE;
  } else if (d == DL_QUIT) {
    if (s == 0) {
      game_save();
      return false;
    }
    if (u.screen != SC_SETUP) u.dialog = DL_PAUSE, u.dsel = 3;
  } else if (d == DL_REVEAL) {
    if (s == 0) {
      sv.phase = PH_REVEAL;
      game_save();
      open_round();
    }
  } else if (d == DL_REMOVE && s == 0) {
    int p = u.players_sel;
    for (int i = p; i + 1 < sv.nplayers; i++) memcpy(sv.names[i], sv.names[i + 1], NAME_MAX + 1);
    sv.nplayers--;
    memset(sv.names[sv.nplayers], 0, NAME_MAX + 1);
    if (u.players_sel > players_last()) u.players_sel = players_last();
    game_save();
  }
  u.dirty = true;
  return true;
}

static void draw_dialog(void) {
  const char *const *items;
  char q[32];
  int n = dialog_items(&items, q);
  int h = 58 + n * 34, y = (KEY_Y - h) / 2, x = 24, w = 272;
  gfx_fill_alpha(0, 0, SCREEN_W, KEY_Y, 0, 150);
  gfx_rrect(x, y, w, h, 14, HEX(0x1B2150), 256);
  gfx_rrect_outline(x, y, w, h, 14, 2, HEX(0x4A56A8), 256);
  gfx_text_center(&np_font_title, 160, y + 32, q, WHITE, 256);
  for (int i = 0; i < n; i++) button(x + 24, y + 46 + i * 34, w - 48, 28, items[i], u.dsel == i);
  KEYS("OK", "choose", "Back", u.dialog == DL_PAUSE ? "resume" : "cancel");
}

/* ------------------------------------------------------------ the frame, the keys */
static void scene(void *ctx) {
  (void)ctx;
  switch (u.screen) {
    case SC_HELP: draw_help(); break;
    case SC_SETUP: draw_setup(); break;
    case SC_PLAYERS: draw_players(); break;
    case SC_NAME: draw_name(); break;
    case SC_CATS: draw_cats(); break;
    case SC_PASS: draw_pass(); break;
    case SC_ROLE: draw_role(); break;
    case SC_TALK: draw_talk(); break;
    default: draw_reveal(); break;
  }
  if (u.dialog) draw_dialog();
}

#ifdef HOST
/* what the screen shows, for tests/check.py */
static void trace(void) {
  static const char *const names[] = {"help", "setup", "players", "name", "cats", "pass", "role", "talk", "reveal"};
  char w[WORD_MAX + 1];
  int c = word_text(sv.word, w);
  printf("DRAW %s dialog %d", names[u.screen], u.dialog);
  if (u.screen == SC_PASS) printf(" turn %d name %s", sv.turn, sv.names[sv.turn]);
  if (u.screen == SC_ROLE) {
    if (is_imposter(sv.turn)) printf(" turn %d imposter hint %s", sv.turn, sv.hint ? categories[c].name : "-");
    else printf(" turn %d word %s cat %s", sv.turn, w, categories[c].name);
  }
  if (u.screen == SC_TALK) printf(" starter %d %s left %u", sv.starter, sv.names[sv.starter], (unsigned)sv.time_left);
  if (u.screen == SC_REVEAL) printf(" word %s imps %u", w, (unsigned)sv.imps);
  if (u.screen == SC_NAME) printf(" typed %s", u.name);
  printf("\n");
}
#endif

void app_draw(void) {
  u.dirty = false;
  gfx_render(scene, 0, 0, SCREEN_H);
#ifdef HOST
  if (host_trace) trace();
#endif
}

bool app_step(uint32_t keys, uint32_t letters, uint32_t now) {
  uint32_t dt = now - u.now;
  u.now = now;
  if (keys & KB_HOME) {
    if (u.screen == SC_NAME) name_done(false); /* (the name typed so far, kept as Back keeps it) */
    game_save();
    return false;
  }
  u.stale &= keys;
  uint32_t live = keys & ~u.stale, hit = live & ~u.prev_keys, lhit = letters & ~u.prev_letters;
  const uint32_t rep = KB_LEFT | KB_RIGHT | KB_UP | KB_DOWN | KB_ERASE;
  if (hit & rep) u.repeat_at = now + 350;
  else if ((live & rep) && (int32_t)(now - u.repeat_at) >= 0) hit |= live & rep, u.repeat_at = now + 90;
  u.prev_keys = live, u.prev_letters = letters;
  if (u.screen != SC_NAME) lhit = 0;
  /* the discussion timer runs on its screen */
  if (u.screen == SC_TALK && !u.dialog && sv.timer && sv.time_left) {
    sv.time_left = dt >= sv.time_left ? 0 : sv.time_left - dt;
    int s = (int)((sv.time_left + 999) / 1000);
    if (s != u.shown_sec) u.shown_sec = s, u.dirty = true;
  }
  if (u.msg_until && (int32_t)(now - u.msg_until) >= 0) u.msg_until = 0, u.dirty = true;
  hit &= ~(uint32_t)KB_ANY;
  if (!hit && !lhit) return true;
  u.dirty = true;
  rnd_stir(now);
  if (u.dialog) return dialog_key(hit);
  switch (u.screen) {
    case SC_HELP: return help_key(hit);
    case SC_SETUP: return setup_key(hit);
    case SC_PLAYERS: return players_key(hit);
    case SC_NAME: return name_key(hit, lhit);
    case SC_CATS: return cats_key(hit);
    case SC_PASS: return pass_key(hit);
    case SC_ROLE: return role_key(hit);
    case SC_TALK: return talk_key(hit);
    default: return reveal_key(hit);
  }
}
