/* Block Blast: fit the blocks on the 8 x 8 board; fill a row or a column and it clears.
 *
 * Hungry Studio's puzzle as it plays on phones: three blocks at a time, placed in any order,
 * none of them turns; full rows and columns clear together, clearing again within three blocks
 * keeps a combo going, and the game ends when none of the blocks left fits. Its 41 blocks, how
 * often each one comes, its scoring and its colours follow what players measured from the app.
 *
 * Inside NumPlay it draws with the launcher's strips, shapes and fonts (launcher/src/gfx.c) and
 * saves through its file functions, so the game is only its rules and its screen. On its own
 * (BlockBlast.nwa) it brings them along. */
#include <eadk.h>
#include "../../../launcher/src/gfx.h"
#include "../../common/np_text.h"

#if NP_MODULE /* (np_file_read and np_file_write: NumPlay's, launcher/src/np.h) */
#define np_app_begin()
#define np_app_end() 0
#else
#include "../../common/epsilon_app.h"
#include "../../common/epsilon_files.h"
#define np_file_read ef_read
#define np_file_write ef_write
#ifndef NP_PORT /* (another target: platform/port.mk names the app) */
const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "Block Blast";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;
#endif
void np_push(int x, int y, int w, int h, const color_t *px) {
  eadk_display_push_rect((eadk_rect_t){(uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h}, px);
}
#endif

#define KEY(k) (1ull << (k))
#define N 8
#define P 25   /* from one cell of the board to the next */
#define BX 8   /* the board's first cell */
#define BY 35
#define TX 216 /* the tray */
#define TS 12  /* a cell of the blocks waiting there */
#define WHITE 0xFFFF
#define NAVY HEX(0x1A264C)
#define GOLD HEX(0xFDE70A)

/* The blocks, as five rows of five cells (the first one leftmost), and how often each comes
   (in the top 7 bits): every way a block faces is a block of its own. */
#define B(w, a, b, c, d, e) \
  ((uint32_t)(w) << 25 | (uint32_t)(a) << 20 | (uint32_t)(b) << 15 | (uint32_t)(c) << 10 | (uint32_t)(d) << 5 | (uint32_t)(e))
#define I 0x10
static const uint32_t pieces[] = {
  B(9, I, 0, 0, 0, 0),                                           /* one */
  B(14, 030, 0, 0, 0, 0), B(14, I, I, 0, 0, 0),                    /* lines of 2 to 5 */
  B(10, 034, 0, 0, 0, 0), B(4, I, I, I, 0, 0),
  B(6, 036, 0, 0, 0, 0), B(14, I, I, I, I, 0),
  B(3, 037, 0, 0, 0, 0), B(4, I, I, I, I, I),
  B(16, 030, 030, 0, 0, 0), B(9, 034, 034, 0, 0, 0),             /* squares and rectangles */
  B(9, 030, 030, 030, 0, 0), B(4, 034, 034, 034, 0, 0),
  B(6, 030, I, 0, 0, 0), B(6, 030, 010, 0, 0, 0),                /* small corners */
  B(6, I, 030, 0, 0, 0), B(6, 010, 030, 0, 0, 0),
  B(3, I, I, 030, 0, 0), B(3, 010, 010, 030, 0, 0),              /* L and J */
  B(3, 030, I, I, 0, 0), B(3, 030, 010, 010, 0, 0),
  B(3, 034, I, 0, 0, 0), B(3, 034, 004, 0, 0, 0),
  B(3, I, 034, 0, 0, 0), B(3, 004, 034, 0, 0, 0),
  B(2, 034, I, I, 0, 0), B(2, 034, 004, 004, 0, 0),              /* big corners */
  B(2, I, I, 034, 0, 0), B(2, 004, 004, 034, 0, 0),
  B(2, I, 010, 0, 0, 0), B(2, 010, I, 0, 0, 0),                  /* diagonals */
  B(2, I, 010, 004, 0, 0), B(2, 004, 010, I, 0, 0),
  B(3, 014, 030, 0, 0, 0), B(3, 030, 014, 0, 0, 0),              /* S and Z */
  B(3, I, 030, 010, 0, 0), B(3, 010, 030, I, 0, 0),
  B(3, 034, 010, 0, 0, 0), B(3, 010, 034, 0, 0, 0),              /* T */
  B(3, I, 030, I, 0, 0), B(3, 010, 030, 010, 0, 0),
};
#define NP (int)(sizeof pieces / sizeof pieces[0])
/* every cell of block p, row r and column c (shifted up, its first cell is the top bit) */
#define EACH(p)                                                      \
  for (uint32_t m_ = pieces[p] << 7, b_ = 0; m_; m_ <<= 1, b_++) \
    for (int r = (int)b_ / 5, c = (int)b_ % 5, o_ = (int32_t)m_ < 0; o_; o_ = 0)

/* blue, orange, green, purple, yellow, cyan, red, and grey for what can't go anywhere */
static const uint32_t faces[9] = {0, 0x345CEF, 0xF35C17, 0x28CB30, 0x9655E5, 0xFBD008, 0x19C4F0, 0xD53D38, 0x737A9C};
static color_t shade[9][5]; /* bottom, right, left, top, face */

/* blockblast.sav: the best score, and the game in progress (kept as it is played) */
static struct {
  uint8_t magic, version, flags, combo; /* flags: 1 a game to continue, 2 the guide was read */
  uint8_t miss, cur, cx, cy;            /* miss: blocks placed since a line last cleared */
  uint32_t best, score, best0; /* best0: the best when this game started */
  int8_t tray[3]; /* the blocks waiting, -1 once placed */
  uint8_t tcol[3];
  uint8_t board[N * N]; /* each cell's colour, 0 if empty */
} sv;
#define SAVE_NAME "blockblast.sav"

static color_t strip[SCREEN_W * STRIP_H];
static uint32_t now, seed, total, shown, fx_t, pop_t, gain, tray_t, over_t;
static uint8_t fx[N * N];  /* the cells that just cleared, bursting: their colour */
static int fx_x, fx_y, pop_lines, pop_combo, pop_clear, hl, ov, sel, w, h, lines;
static bool over, fits_here, can[3];

static uint32_t rnd(void) {
  seed ^= seed << 13;
  seed ^= seed >> 17;
  return seed ^= seed << 5;
}
static char *num(char *o, uint32_t v) { /* v in o, then the end of it */
  char t[10];
  int n = 0;
  do t[n++] = (char)('0' + v % 10); while (v /= 10);
  while (n) *o++ = t[--n];
  *o = 0;
  return o;
}
/* a text, then a number */
static char *label(char *o, const char *s, uint32_t v) {
  char *e = o;
  while (*s) *e++ = *s++;
  num(e, v);
  return o;
}
static uint32_t ago(uint32_t t) { return now - t; }

/* ------------------------------------------------------------ the rules */
static void measure(int p) { /* w and h: the columns any of its rows reach, the rows down to its last cell */
  uint32_t m = pieces[p] << 7, cols = (m | m << 5 | m << 10 | m << 15 | m << 20) >> 27;
  w = 5 - __builtin_ctzl(cols), h = (31 - __builtin_ctzl(m)) / 5 + 1;
}
static bool fits(int p, int x, int y) {
  EACH(p) if (x + c >= N || y + r >= N || sv.board[(y + r) * N + x + c]) return false;
  return true;
}
static bool placeable(int p) {
  for (int k = 0; k < N * N; k++)
    if (fits(p, k % N, k / N)) return true;
  return false;
}
/* the full rows (bits 0-7) and columns (bits 8-15) of b; how many in lines */
static int full(const uint8_t *b) {
  int m = 0;
  lines = 0;
  for (int i = 0; i < N; i++) {
    int row = 1, col = 1;
    for (int j = 0; j < N; j++) row &= b[i * N + j] != 0, col &= b[j * N + i] != 0;
    m |= row << i | col << (i + 8), lines += row + col;
  }
  return m;
}
static bool in_line(int m, int k) { return (m >> (k / N) & 1) || (m >> (N + k % N) & 1); }

/* keeps the block in hand on the board */
static void clamp(void) {
  measure(sv.tray[sv.cur]);
  if (sv.cx > N - w) sv.cx = (uint8_t)(N - w);
  if (sv.cy > N - h) sv.cy = (uint8_t)(N - h);
}

static void save(void) {
  sv.magic = 'B', sv.version = 1;
  np_file_write(SAVE_NAME, &sv, sizeof sv);
}

/* three new blocks, each its own colour; at least one of them fits somewhere */
static void new_tray(void) {
  for (int t = 0; t < 40; t++) {
    bool any = false;
    for (int k = 0; k < 3; k++) {
      int r = (int)(rnd() % total), p = 0;
      while (r >= (int)(pieces[p] >> 25)) r -= (int)(pieces[p++] >> 25);
      sv.tray[k] = (int8_t)p;
      do sv.tcol[k] = (uint8_t)(1 + rnd() % 7);
      while ((k && sv.tcol[k] == sv.tcol[0]) || (k == 2 && sv.tcol[2] == sv.tcol[1]));
      any |= placeable(p);
    }
    if (any) break;
  }
  sv.cur = 0, tray_t = now;
  clamp();
}

/* the next block to take in hand, after the one just placed: one that fits if there is one */
static void next_block(void) {
  for (int pass = 0; pass < 2; pass++)
    for (int i = 1; i <= 3; i++) {
      int k = (sv.cur + i) % 3;
      if (sv.tray[k] >= 0 && (pass || placeable(sv.tray[k]))) {
        sv.cur = (uint8_t)k;
        clamp();
        return;
      }
    }
  new_tray();
}

static void check_over(void) {
  for (int k = 0; k < 3; k++)
    if (sv.tray[k] >= 0 && placeable(sv.tray[k])) return;
  over = true, over_t = now, sv.flags &= (uint8_t)~1;
}

static void new_game(void) {
  for (int k = 0; k < N * N; k++) sv.board[k] = fx[k] = 0;
  sv.score = shown = 0, sv.combo = sv.miss = 0, sv.flags |= 1, sv.best0 = sv.best;
  sv.cx = sv.cy = 3, over = false, ov = 0, pop_t = now - 9999;
  new_tray();
  save();
}

/* Scoring, as the app does it: a point a cell placed; lines cleared at once bring 10, 20, 60,
   120, 200... times the combo (how many placements in a row cleared lines, none of them more
   than three blocks apart), and an empty board 300 more. */
static void place(void) {
  int p = sv.tray[sv.cur], col = sv.tcol[sv.cur], cells = 0;
  measure(p);
  EACH(p) sv.board[(sv.cy + r) * N + sv.cx + c] = (uint8_t)col, cells++;
  int m = full(sv.board);
  bool empty = true;
  for (int k = 0; k < N * N; k++) {
    fx[k] = 0;
    if (in_line(m, k)) fx[k] = (uint8_t)col, sv.board[k] = 0;
    if (sv.board[k]) empty = false;
  }
  fx_t = now, fx_x = 2 * sv.cx + w, fx_y = 2 * sv.cy + h; /* the bursts start from its middle */
  sv.score += (uint32_t)cells;
  if (lines) {
    sv.combo++, sv.miss = 0;
    gain = sv.combo * (uint32_t)(lines == 1 ? 10 : 10 * lines * (lines - 1)) + (empty ? 300 : 0);
    sv.score += gain, pop_t = now, pop_lines = lines, pop_combo = sv.combo, pop_clear = empty;
  } else if (++sv.miss >= 3) {
    sv.combo = 0;
  }
  if (sv.score > sv.best) sv.best = sv.score;
  sv.tray[sv.cur] = -1;
  next_block();
  check_over();
  save();
}

/* ------------------------------------------------------------ drawing */
/* a block, s pixels a side: its face framed by bevels, the top lightest, then the left, the
   right, and the bottom darkest, meeting on the diagonals */
static void block(int x, int y, int s, int c) {
  const color_t *k = shade[c];
  gfx_fill(x, y, s, s, k[4]);
  for (int i = 0, b = s > 14 ? 4 : 2; i < b; i++) {
    int e = s - 2 * i;
    gfx_fill(x + i, y + i, e, 1, k[3]);
    gfx_fill(x + i, y + s - 1 - i, e, 1, k[0]);
    gfx_fill(x + i, y + i + 1, 1, e - 2, k[2]);
    gfx_fill(x + s - 1 - i, y + i + 1, 1, e - 2, k[1]);
  }
}

static void crown(int x, int y, int k, color_t c) {
  gfx_fill(x, y + 3 * k, 8 * k, 4 * k, c);
  gfx_fill(x, y + k, 2 * k, 2 * k, c);
  gfx_fill(x + 3 * k, y, 2 * k, 3 * k, c);
  gfx_fill(x + 6 * k, y + k, 2 * k, 2 * k, c);
}

/* text with a dark rim and a shadow, to read over any block */
static void say(const np_font_t *f, int cx, int base, const char *s, color_t c, int a) {
  for (int i = 0; i < 5; i++) gfx_text_center(f, cx + (i & 1 ? 1 : -1) * (i < 4), base + (i & 2 ? 1 : -1) + 3 * (i == 4), s, NAVY, a * 3 >> 2);
  gfx_text_center(f, cx, base, s, c, a);
}

static void button(int y, const char *s, bool on) {
  gfx_rrect(90, y, 140, 26, 13, on ? GOLD : HEX(0x3F56B4), 256);
  gfx_text_center(&np_font_body, 160, y + 18, s, on ? NAVY : WHITE, 256);
}

enum { OV_NONE, OV_MENU, OV_HELP, OV_OVER };
static const char *const menu_items[] = {T("Resume"), T("New game"), T("How to play"), T("Quit")};
static const char *const over_items[] = {T("Play again"), T("Quit")};
static const char *const help_keys[][2] = {
  {T("1 2 3"), T("choose a block")}, {T("Arrows"), T("move it")}, {T("OK"), T("place it")}, {T("Back"), T("menu")}};

static void overlay(void) {
  char s[12];
  gfx_fill_alpha(0, 0, SCREEN_W, SCREEN_H, 0, 120);
  gfx_rrect(56, 34, 208, 200, 14, HEX(0x2B3E8E), 256);
  gfx_rrect_outline(56, 34, 208, 200, 14, 2, HEX(0x6F8BE0), 256);
  if (ov == OV_MENU) {
    say(&np_font_title, 160, 68, T("Paused"), WHITE, 256);
    for (int i = 0; i < 4; i++) button(84 + 34 * i, menu_items[i], sel == i);
  } else if (ov == OV_HELP) {
    say(&np_font_title, 160, 64, T("How to play"), WHITE, 256);
    gfx_paragraph(&np_font_body, 160, 86, 184, 15, T("Fill a row or a column to clear it. Blocks never turn."),
                  WHITE, 256);
    for (int i = 0; i < 4; i++) {
      gfx_text_right(&np_font_body, 150, 132 + 17 * i, help_keys[i][0], GOLD, 256);
      gfx_text(&np_font_body, 160, 132 + 17 * i, help_keys[i][1], WHITE, 256);
    }
    button(200, T("Play"), true);
  } else { /* the score, under a crown and a ribbon when it is the best yet */
    bool rec = sv.score > sv.best0;
    crown(148, 44, 3, rec ? GOLD : HEX(0x6F8BE0));
    if (rec) gfx_rrect(96, 74, 128, 24, 6, HEX(0x8A3FD1), 256);
    say(&np_font_title, 160, 93, rec ? T("New best!") : T("Game over"), WHITE, 256);
    num(s, sv.score);
    say(&np_font_title, 160, 128, s, GOLD, 256);
    if (!rec) gfx_text_center(&np_font_body, 160, 150, label(s, T("Best "), sv.best), HEX(0xB8C6F5), 256);
    for (int i = 0; i < 2; i++) button(166 + 34 * i, over_items[i], sel == i);
  }
}

static void scene(void *ctx) {
  char s[12];
  gfx_vgrad(0, 0, SCREEN_W, SCREEN_H, 0x3F56B4, 0x516FC5);
  /* the best score with its crown, the score, the keys */
  crown(8, 9, 2, GOLD);
  num(s, sv.best);
  gfx_text(&np_font_body, 28, 22, s, GOLD, 256);
  num(s, shown);
  say(&np_font_title, BX + 4 * P, 27, s, WHITE, 256);
  gfx_text_right(&np_font_small, 314, 13, T("OK  place"), HEX(0xD8E2FF), 220);
  gfx_text_right(&np_font_small, 314, 26, T("Back  menu"), HEX(0xD8E2FF), 220);

  /* the board; the lines the block in hand would fill take its colour */
  gfx_rrect(BX - 3, BY - 3, 8 * P + 5, 8 * P + 5, 5, NAVY, 256);
  int p = sv.tray[sv.cur], col = sv.tcol[sv.cur];
  for (int k = 0; k < N * N; k++) {
    int x = BX + k % N * P, y = BY + k / N * P, c = sv.board[k];
    if (y >= gfx_y1 || y + P <= gfx_y0) continue;
    if (c && over) c = ago(over_t) > (uint32_t)(N - 1 - k / N) * 60 ? 8 : c; /* greying, from the bottom up */
    if (c && in_line(hl, k)) c = col;
    if (c) block(x, y, P - 1, c);
    else gfx_fill(x, y, P - 1, P - 1, HEX(0x242C53));
    if (fx[k]) { /* bursting: flashes, then shrinks away, from where the block went outwards */
      int ax = 2 * (k % N) + 1 - fx_x, ay = 2 * (k / N) + 1 - fx_y;
      int32_t t = (int32_t)ago(fx_t) - ((ax < 0 ? -ax : ax) + (ay < 0 ? -ay : ay)) * 14;
      if (t < 0) t = 0;
      if (t < 240) {
        int z = (P - 1) * (240 - t) / 240, o = (P - 1 - z) / 2;
        block(x + o, y + o, z, fx[k]);
        gfx_fill_alpha(x + o, y + o, z, z, WHITE, 230 - t);
      }
    }
  }
  if (over) gfx_fill_alpha(BX - 3, BY - 3, 8 * P + 5, 8 * P + 5, 0, ago(over_t) > 600 ? 110 : (int)ago(over_t) * 110 / 600);
  else if (p >= 0) { /* the block in hand: glowing where it fits, see-through where it doesn't */
    int g = (int)(now % 1000), a = g < 500 ? g : 1000 - g;
    EACH(p) {
      int x = BX + (sv.cx + c) * P, y = BY + (sv.cy + r) * P;
      if (fits_here) {
        block(x, y, P - 1, col);
        gfx_fill_alpha(x, y, P - 1, P - 1, WHITE, a / 6);
      } else {
        gfx_fill_alpha(x, y, P - 1, P - 1, HEX(faces[col]), 150);
      }
    }
  }

  /* the tray: each block in its place, under the key that takes it */
  for (int k = 0; k < 3; k++) {
    int y0 = 32 + 69 * k, q = sv.tray[k], on = k == sv.cur && !over;
    if (on) {
      gfx_rrect(TX, y0, 100, 67, 10, WHITE, 40);
      gfx_rrect_outline(TX, y0, 100, 67, 10, 2, WHITE, 150);
    }
    gfx_circle(TX + 11, y0 + 11, 7, on ? GOLD : HEX(0x2B3E8E), 256);
    s[0] = (char)('1' + k), s[1] = 0;
    gfx_text_center(&np_font_small, TX + 11, y0 + 15, s, on ? NAVY : WHITE, 256);
    if (q < 0) continue;
    int z = ago(tray_t) < 160 ? TS * (int)ago(tray_t) / 160 : TS; /* new blocks grow in */
    measure(q);
    EACH(q) block(TX + 50 + (2 * c - w) * z / 2, y0 + 34 + (2 * r - h) * z / 2, z - 1, can[k] ? sv.tcol[k] : 8);
  }

  /* what the last clear was worth */
  uint32_t t = ago(pop_t);
  if (t < 1100) {
    static const char *const praise[] = {T("Good!"), T("Great!"), T("Excellent!"), T("Amazing!"), T("Unbelievable!")};
    int a = t < 700 ? 256 : (int)(1100 - t) * 256 / 400, y = 140 - (int)t / 60;
    if (pop_combo > 1) say(&np_font_title, BX + 4 * P, y - 26, label(s, T("Combo "), (uint32_t)pop_combo), WHITE, a);
    if (pop_clear) say(&np_font_title, BX + 4 * P, y, T("Board clear!"), GOLD, a);
    else if (pop_lines > 1) say(&np_font_title, BX + 4 * P, y, praise[pop_lines > 6 ? 4 : pop_lines - 2], GOLD, a);
    say(&np_font_body, BX + 4 * P, y + 20, label(s, "+", gain), WHITE, a);
  }
  if (ov) overlay();
}

/* ------------------------------------------------------------ the game */
static void load(void) {
  uint32_t len = 0;
  const uint8_t *d = np_file_read(SAVE_NAME, &len);
  uint8_t *v = (uint8_t *)&sv;
  bool ok = d && len == sizeof sv && d[0] == 'B' && d[1] == 1;
  for (uint32_t i = 0; i < sizeof sv; i++) v[i] = ok ? d[i] : 0;
  /* whatever it holds, a game this one could have saved */
  for (int k = 0; k < N * N; k++)
    if (sv.board[k] > 7) sv.board[k] = 0;
  for (int k = 0; k < 3; k++) {
    if (sv.tray[k] >= NP || sv.tray[k] < -1) sv.flags &= (uint8_t)~1;
    if (sv.tcol[k] < 1 || sv.tcol[k] > 7) sv.tcol[k] = 1;
  }
  if (sv.cur > 2 || sv.tray[sv.cur] < 0 || sv.cx >= N || sv.cy >= N) sv.flags &= (uint8_t)~1;
  if (sv.best < sv.score) sv.best = sv.score;
}

int main(void) {
  np_app_begin();
  color_t *their_strip = gfx_buf;
  gfx_buf = strip;
  for (int c = 1; c < 9; c++) {
    static const uint8_t by[5] = {115, 60, 50, 120, 0}; /* towards black, then white */
    for (int i = 0; i < 5; i++) shade[c][i] = gfx_rgb(gfx_lerp888(faces[c], i < 2 ? 0 : 0xFFFFFF, by[i]));
  }
  for (int p = 0; p < NP; p++) total += pieces[p] >> 25;
  now = (uint32_t)eadk_timing_millis();
  seed = eadk_random() ^ now;
  if (!seed) seed = 1;
  load();
  shown = sv.score, pop_t = now - 9999;
  if (sv.flags & 1) clamp(), check_over();
  else new_game();
  if (!(sv.flags & 2)) ov = OV_HELP, sv.flags |= 2;
  uint64_t held = eadk_keyboard_scan(), stale = held; /* keys still down from the launcher wait to be let go */
  uint32_t repeat_at = 0;
  for (;;) {
    now = (uint32_t)eadk_timing_millis();
    uint64_t k = eadk_keyboard_scan(), hit = k & ~held;
    const uint64_t arrows = KEY(eadk_key_left) | KEY(eadk_key_right) | KEY(eadk_key_up) | KEY(eadk_key_down);
    stale &= k;
    if (hit & arrows) repeat_at = now + 220;
    else if ((k & arrows & ~stale) && (int32_t)(now - repeat_at) >= 0) hit |= k & arrows & ~stale, repeat_at = now + 70;
    held = k;
    if (k & (KEY(eadk_key_home) | KEY(eadk_key_on_off))) break;
    bool ok = hit & (KEY(eadk_key_ok) | KEY(eadk_key_exe));
    int dx = !!(hit & KEY(eadk_key_right)) - !!(hit & KEY(eadk_key_left));
    int dy = !!(hit & KEY(eadk_key_down)) - !!(hit & KEY(eadk_key_up));
    if (over && !ov && ago(over_t) > 1300) ov = OV_OVER, sel = 0;
    if (ov) {
      int n = ov == OV_MENU ? 4 : ov == OV_OVER ? 2 : 1;
      sel = (sel + dy + n) % n;
      if (hit & KEY(eadk_key_back) && ov != OV_OVER) ov = OV_NONE;
      if (ok) {
        int choice = ov == OV_OVER ? (sel ? 3 : 1) : ov == OV_MENU ? sel : 0;
        ov = OV_NONE;
        if (choice == 1) new_game();
        else if (choice == 2) ov = OV_HELP;
        else if (choice == 3) break;
      }
    } else if (hit & KEY(eadk_key_back)) {
      ov = OV_MENU, sel = 0;
      save();
    } else if (!over) {
      for (int i = 0; i < 3; i++)
        if (hit & KEY(eadk_key_one + i) && sv.tray[i] >= 0) sv.cur = (uint8_t)i, clamp();
      measure(sv.tray[sv.cur]);
      if (sv.cx + dx >= 0 && sv.cx + dx <= N - w) sv.cx = (uint8_t)(sv.cx + dx);
      if (sv.cy + dy >= 0 && sv.cy + dy <= N - h) sv.cy = (uint8_t)(sv.cy + dy);
      if (ok && fits(sv.tray[sv.cur], sv.cx, sv.cy)) place();
    }
    /* what the frame shows: where the block in hand would go, and what it would clear */
    int p = sv.tray[sv.cur];
    fits_here = !over && p >= 0 && fits(p, sv.cx, sv.cy), hl = 0;
    if (fits_here) {
      uint8_t b[N * N];
      for (int i = 0; i < N * N; i++) b[i] = sv.board[i];
      EACH(p) b[(sv.cy + r) * N + sv.cx + c] = 1;
      hl = full(b);
    }
    for (int i = 0; i < 3; i++) can[i] = sv.tray[i] >= 0 && placeable(sv.tray[i]);
    if (shown != sv.score) shown += (sv.score - shown + 5) / 6;
    eadk_display_wait_for_vblank();
    gfx_render(scene, 0, 0, SCREEN_H);
    uint32_t spent = (uint32_t)eadk_timing_millis() - now;
    if (spent < 30) eadk_timing_msleep(30 - spent);
  }
  save();
  gfx_buf = their_strip;
  return np_app_end();
}
