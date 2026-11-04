/* Sudoku: fill the grid so every row, column and 3 x 3 box holds 1 to 9 once.
 *
 * Played like the phone apps: the number keys write a digit in the cell picked with the arrows,
 * OK switches to notes (small pencil digits), 0 erases, the backspace undoes, shift gives a hint.
 * The row, column and box of the cell light up, and so does every cell with its digit; a digit
 * placed takes its notes off the cells it sees. Wrong digits turn red and count as mistakes.
 *
 * Every puzzle is made on the calculator: a full grid solved at random, then clues taken out in
 * pairs facing each other (the grid stays symmetric) as long as one solution is left. Easy and
 * Medium can be solved with singles alone (a cell with one digit left, a digit with one place left
 * in a row, column or box), Medium with fewer clues; Hard needs more: a digit that in a box can
 * only go in one of its rows or columns (pointing), or the other way round; Expert needs more still.
 *
 * Inside NumPlay it draws with the launcher's strips, shapes and fonts (launcher/src/gfx.c) and
 * saves through its file functions; on its own (Sudoku.nwa) it brings them along. */
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
#ifndef HOST /* (a computer build of the tests has no app header) */
#ifndef NP_PORT /* (another target: platform/port.mk names the app) */
const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "Sudoku";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;
#endif
#endif
void np_push(int x, int y, int w, int h, const color_t *px) {
  eadk_display_push_rect((eadk_rect_t){(uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h}, px);
}
#endif

/* the keys used, in 32 bits: the number pad's 9, 4 to 6, 1 to 3, 0 and EXE come down from the high word */
#define KEY(k) ((uint32_t)1 << ((k) < 32 ? (k) : (k) - 23))
static uint32_t keys(void) {
  uint64_t s = eadk_keyboard_scan();
  return ((uint32_t)s & 0xC00211FFu) | ((uint32_t)(s >> 32) & 0x00111C71u) << 9;
}
static const uint8_t digit_key[9] = {19, 20, 21, 13, 14, 15, 30, 31, 9}; /* the bits of keys 1 to 9 */
#define PC 280 /* the middle of the panel right of the grid */
#define WHITE 0xFFFF
#define NAVY HEX(0x344861)  /* the thick lines, the clues */
#define BLUE HEX(0x325AAF)  /* the digits written, the buttons */
#define GREY HEX(0x6E7C8C)  /* notes, labels */
#define LINE HEX(0xBEC6D4)  /* the thin lines */
#define RED HEX(0xE55C6C)
#define PALE HEX(0xE2EBF3)  /* the row, column and box of the cell picked */
#define SAME HEX(0xC3D7EA)  /* the cells with its digit */
#define PICK HEX(0xBBDEFB)  /* the cell picked */
#define TILE HEX(0xEAEEF4)

/* sudoku.sav: the game in progress and the best times */
static struct {
  uint8_t magic, version, flags, diff; /* flags: 1 a game to continue, 2 the guide was read */
  uint8_t sel, mistakes, pad[2];
  uint32_t secs, best[4];               /* seconds played; the best time of each level, 0 for none */
  uint8_t sol[81];                      /* the solution */
  uint8_t val[81];                      /* the digit in each cell, 0 for none; a clue's with 16 added */
  uint16_t notes[81];                   /* the notes in each cell, bit d for digit d */
} sv;
#define SAVE_NAME "sudoku.sav"

static color_t strip[SCREEN_W * STRIP_H];
static uint32_t seed;
static int ov, sel, menu, quit_ok, notes, best_new; /* sel: the cell picked; menu: the button picked on a card */

static uint32_t rnd(void) {
  seed ^= seed << 13;
  seed ^= seed >> 17;
  return seed ^= seed << 5;
}

/* ------------------------------------------------------------ the rules */
/* units: rows 0-8, columns 9-17, boxes 18-26 */
static uint8_t unit[81][3], cells[27][9]; /* each cell's row, column and box; each unit's cells, in order */
static void units(void) {
  uint8_t n[27] = {0};
  for (int c = 0; c < 81; c++) {
    unit[c][0] = (uint8_t)(c / 9), unit[c][1] = (uint8_t)(9 + c % 9), unit[c][2] = (uint8_t)(18 + c / 27 * 3 + c % 9 / 3);
    for (int j = 0; j < 3; j++) cells[unit[c][j]][n[unit[c][j]]++] = (uint8_t)c;
  }
}
static bool in(int u, int c) { return unit[c][0] == u || unit[c][1] == u || unit[c][2] == u; }
static bool sees(int a, int b) { return unit[a][0] == unit[b][0] || unit[a][1] == unit[b][1] || unit[a][2] == unit[b][2]; }
#define at(u, i) cells[u][i]

static uint8_t g[81];      /* the grid being solved */
static uint16_t used[27];  /* the digits already in each row, column and box */
static uint16_t cand[81];  /* the digits each cell can still take */
static int nsol, wild;     /* solutions found; wild: digits tried in a random order */

static void mark(int c, int d) {
  for (int j = 0; j < 3; j++) used[unit[c][j]] ^= (uint16_t)(1 << d);
}
static uint16_t free_at(int c) { return ~(used[unit[c][0]] | used[unit[c][1]] | used[unit[c][2]]) & 0x3FE; }
static void load_grid(const uint8_t *p) {
  for (int u = 0; u < 27; u++) used[u] = 0;
  for (int c = 0; c < 81; c++)
    if ((g[c] = p ? p[c] : 0)) mark(c, g[c]);
}

/* Counts the solutions, up to two, filling first the cell with fewest digits left; with wild,
   the first one goes in sv.sol. */
static bool search(void) {
  int best = -1, bn = 10;
  for (int c = 0; c < 81; c++)
    if (!g[c]) {
      int n = __builtin_popcount(free_at(c));
      if (n < bn) bn = n, best = c;
    }
  if (best < 0) {
    if (!nsol++ && wild)
      for (int c = 0; c < 81; c++) sv.sol[c] = g[c];
    return nsol > 1;
  }
  uint16_t m = free_at(best);
  int r = wild ? (int)(rnd() % 9) : 0;
  for (int i = 0; i < 9; i++) {
    int d = 1 + (i + r) % 9;
    if (!(m >> d & 1)) continue;
    g[best] = (uint8_t)d, mark(best, d);
    bool stop = search();
    g[best] = 0, mark(best, d);
    if (stop) return true;
  }
  return false;
}

/* How a player would solve it: 1 with singles alone, 2 with pointing too, 3 not with these. */
static int logic(const uint8_t *p) {
  int level = 1;
  load_grid(p);
  for (int c = 0; c < 81; c++) cand[c] = g[c] ? 0 : free_at(c);
  for (;;) {
    int left = 0, c = -1, u, d;
    for (int k = 0; k < 81; k++) /* a cell with one digit left */
      if (!g[k]) {
        left++;
        if (__builtin_popcount(cand[k]) == 1) c = k;
      }
    if (!left) return level;
    for (u = 0; u < 27 && c < 0; u++) /* a digit with one place left in a unit */
      for (d = 1; d <= 9; d++) {
        int n = 0, one = 0;
        for (int i = 0; i < 9; i++)
          if (cand[at(u, i)] >> d & 1) n++, one = at(u, i);
        if (n == 1) {
          cand[c = one] = (uint16_t)(1 << d);
          break;
        }
      }
    if (c >= 0) {
      d = __builtin_ctz(cand[c]);
      g[c] = (uint8_t)d;
      for (int k = 0; k < 81; k++)
        if (sees(c, k)) cand[k] &= (uint16_t)~(1 << d);
      continue;
    }
    /* a digit that, in a box, a row or a column, can only go where it meets another of them: it
       goes nowhere else in that other one */
    for (u = 0; u < 27; u++)
      for (int v = 0; v < 27; v++)
        for (d = 1; d <= 9 && (u < 18) != (v < 18); d++) {
          int n = 0, out = 0, gone = 0;
          for (int i = 0; i < 9; i++)
            if (cand[at(u, i)] >> d & 1) n++, out |= !in(v, at(u, i));
          for (int i = 0; i < 9 && n && !out; i++) {
            c = at(v, i);
            if (!in(u, c) && cand[c] >> d & 1) cand[c] ^= (uint16_t)(1 << d), gone = 1;
          }
          if (gone) goto harder;
        }
    return 3;
  harder:
    level = 2;
  }
}

static int solutions(const uint8_t *p) {
  load_grid(p);
  nsol = wild = 0;
  search();
  return nsol;
}

/* A new puzzle of level sv.diff (0-3) in sv.val, its solution in sv.sol. The pairs of clues are
   tried in a random order: a random start and step around the 41 pairs (41 is prime). */
static void generate(void) {
  static const uint8_t clues[4] = {36, 30, 0, 0};
  uint8_t p[81];
  int diff = sv.diff;
  for (int tries = 0;; tries++) {
    load_grid(0);
    nsol = 0, wild = 1;
    search();
    for (int c = 0; c < 81; c++) p[c] = sv.sol[c];
    int left = 81, at0 = (int)(rnd() % 41), step = 1 + (int)(rnd() % 40);
    for (int i = 0; i < 41; i++) {
      int c = (at0 + i * step) % 41, n = c == 40 ? 1 : 2;
      if (left - n < clues[diff]) continue;
      p[c] = p[80 - c] = 0;
      if (solutions(p) != 1 || (diff < 3 && logic(p) > (diff < 2 ? 1 : 2))) p[c] = sv.sol[c], p[80 - c] = sv.sol[80 - c];
      else left -= n;
    }
    if (diff < 2 || logic(p) == diff || tries > 80) break; /* (a Hard try ends Hard about 1 time in 10) */
  }
  for (int c = 0; c < 81; c++) sv.val[c] = (uint8_t)(p[c] ? p[c] + 16 : 0), sv.notes[c] = 0;
}

/* ------------------------------------------------------------ playing */
static uint32_t undo[256]; /* each change: the cell, its digit and notes before, and whether a move starts there */
static uint8_t utop, ucount;

static void set(int c, int v, int nt, int first) {
  undo[utop++] = (uint32_t)c | (uint32_t)sv.val[c] << 7 | (uint32_t)sv.notes[c] << 12 | (uint32_t)first << 31;
  if (ucount < 255) ucount++;
  sv.val[c] = (uint8_t)v, sv.notes[c] = (uint16_t)nt;
}

/* digit d in cell c: its notes go, and the cells it sees lose that note */
static void write(int c, int d) {
  if (sv.val[c] > 15 || sv.val[c] == d) return;
  set(c, d, 0, 1);
  sv.mistakes += d != sv.sol[c];
  int right = 0;
  for (int k = 0; k < 81; k++) {
    if (k != c && sees(c, k) && sv.notes[k] >> d & 1) set(k, sv.val[k], sv.notes[k] ^ 1 << d, 0);
    right += (sv.val[k] & 15) == sv.sol[k];
  }
  if (right == 81) { /* solved */
    sv.flags &= (uint8_t)~1, ov = 4, menu = 0; /* (OV_WON) */
    best_new = !sv.best[sv.diff] || sv.secs < sv.best[sv.diff];
    if (best_new) sv.best[sv.diff] = sv.secs;
  }
}

static void undo_move(void) {
  while (ucount) {
    uint32_t e = undo[--utop];
    ucount--;
    sv.val[e & 127] = (uint8_t)(e >> 7 & 31), sv.notes[e & 127] = (uint16_t)(e >> 12 & 0x3FE);
    if (e >> 31) break;
  }
}

/* a hint: the cell picked if it isn't right yet, otherwise the next one that isn't */
static void hint(void) {
  for (int i = 0; i < 81; i++) {
    int c = (sel + i) % 81;
    if ((sv.val[c] & 15) != sv.sol[c]) {
      write(sel = c, sv.sol[c]);
      return;
    }
  }
}

/* ------------------------------------------------------------ drawing */
static int cell_at(int i) { return 7 + 25 * i + i / 3; } /* the left (or top) of column (or row) i */

static char *num(char *o, uint32_t v) { /* v in o; the end of it */
  char t[10];
  int n = 0;
  do t[n++] = (char)('0' + v % 10); while (v /= 10);
  while (n) *o++ = t[--n];
  *o = 0;
  return o;
}
static char *clock_text(char *o, uint32_t s) { /* 4:05 */
  char *e = num(o, s / 60);
  *e++ = ':', *e++ = (char)('0' + s % 60 / 10), *e++ = (char)('0' + s % 10), *e = 0;
  return o;
}
static void say(const np_font_t *f, int x, int y, const char *s, color_t c) { gfx_text_center(f, x, y, s, c, 256); }
static void say_digit(const np_font_t *f, int x, int y, int d, color_t c) {
  char s[2] = {(char)('0' + d), 0};
  say(f, x, y, s, c);
}

enum { OV_NONE, OV_MENU, OV_LEVEL, OV_HELP, OV_WON };
/* each card: its title, then its buttons; what each button leads to (QUIT, NEW: a new game, made in
   a fraction of a second) */
static const char *const cards[5][5] = {
  {0},
  {T("Paused"), T("Resume"), T("New game"), T("How to play"), T("Quit game")},
  {T("New game"), T("Easy"), T("Medium"), T("Hard"), T("Expert")},
  {T("How to play"), T("Play")},
  {T("Solved!"), T("New game"), T("Quit game")},
};
#define QUIT 6
#define NEW 7
static const uint8_t buttons[5] = {0, 4, 4, 1, 2}, leads[5][4] = {
  {0}, {OV_NONE, OV_LEVEL, OV_HELP, QUIT}, {NEW, NEW, NEW, NEW}, {OV_NONE}, {OV_LEVEL, QUIT}};
#define LEVEL(i) cards[OV_LEVEL][1 + (i)]

static void overlay(void) {
  char s[16];
  int n = buttons[ov], extra = ov == OV_HELP || ov == OV_WON ? 56 : 0, y = (SCREEN_H - 54 - extra - 32 * n) / 2;
  /* drawn like a box of the grid: a thick navy edge, the choices in cells split by thin lines, the one
     picked shaded as the cell picked is */
  gfx_fill_alpha(0, 0, SCREEN_W, SCREEN_H, NAVY, 110);
  gfx_fill(58, y - 2, 204, 58 + extra + 32 * n, NAVY);
  gfx_fill(60, y, 200, 54 + extra + 32 * n, WHITE);
  gfx_fill(76, y + 48 + extra - 1, 168, 32 * n + 1, LINE);
  say(&np_font_title, 160, y + 34, cards[ov][0], NAVY);
  y += 48;
  if (ov == OV_HELP)
    gfx_paragraph(&np_font_body, 160, y + 12, 176, 15, T("Every row, column and box holds 1 to 9 once."), NAVY, 256);
  if (ov == OV_WON) {
    say(&np_font_title, 160, y + 16, clock_text(s, sv.secs), BLUE);
    say(&np_font_body, 160, y + 38, best_new ? T("New best!") : LEVEL(sv.diff), GREY);
  }
  y += extra;
  for (int i = 0; i < n; i++, y += 32) {
    color_t c = menu == i ? WHITE : NAVY;
    gfx_fill(77, y, 166, 31, menu == i ? PICK : WHITE);
    c = NAVY;
    say(&np_font_body, 160, y + 18, ov == OV_MENU && i == 3 && quit_ok ? T("OK again to quit") : cards[ov][i + 1], c);
    if (ov == OV_LEVEL && sv.best[i]) gfx_text_right(&np_font_small, 232, y + 17, clock_text(s, sv.best[i]), c, 256);
  }
}

static void scene(void *ctx) {
  static const char *const keys[] = {T("OK  notes"), T("0  erase"), T("DEL  undo"), T("shift  hint"), T("Back  menu")};
  char s[16];
  int pick = sv.val[sel] & 15, placed[10] = {0};
  gfx_fill(0, 0, SCREEN_W, SCREEN_H, WHITE);
  /* the grid: thin lines, the thick ones around the boxes, then the cells */
  gfx_fill(5, 5, 230, 230, LINE);
  for (int i = 0; i < 4; i++) gfx_fill(5 + 76 * i, 5, 2, 230, NAVY), gfx_fill(5, 5 + 76 * i, 230, 2, NAVY);
  for (int c = 0; c < 81; c++) {
    int x = cell_at(c % 9), y = cell_at(c / 9), v = sv.val[c] & 15;
    placed[v]++;
    gfx_fill(x, y, 24, 24, c == sel ? PICK : pick && v == pick ? SAME : sees(c, sel) ? PALE : WHITE);
    if (v) say_digit(&np_font_title, x + 12, y + 20, v, sv.val[c] > 15 ? NAVY : v != sv.sol[c] ? RED : BLUE);
    for (int d = 0; d < 9; d++)
      if (sv.notes[c] >> (d + 1) & 1) say_digit(&np_font_small, x + 4 + d % 3 * 8, y + 8 + d / 3 * 8, d + 1, GREY);
  }
  /* the panel: the level, the clock and the mistakes; the digits laid out as on the keypad, faded once
     all nine are placed; the keys, OK's lit while it writes notes */
  say(&np_font_body, PC, 17, LEVEL(sv.diff), GREY);
  say(&np_font_title, PC, 42, clock_text(s, sv.secs), NAVY);
  char *e = s;
  for (const char *m = T("Mistakes: "); *m;) *e++ = *m++;
  num(e, sv.mistakes);
  say(&np_font_small, PC, 56, s, sv.mistakes ? RED : GREY);
  for (int d = 0; d < 9; d++) {
    int x = 242 + d % 3 * 26, y = 124 - d / 3 * 28, on = d + 1 == pick;
    gfx_rrect(x, y, 24, 24, 5, on ? BLUE : TILE, 256);
    say_digit(&np_font_title, x + 12, y + 20, d + 1, on ? WHITE : placed[d + 1] < 9 ? NAVY : LINE);
  }
  if (notes) gfx_rrect(242, 152, 76, 20, 10, BLUE, 256);
  for (int i = 0; i < 5; i++) say(&np_font_small, PC, 166 + 15 * i, keys[i], i || !notes ? GREY : WHITE);
  if (ov) overlay();
}

static void draw(void) {
  eadk_display_wait_for_vblank();
  gfx_render(scene, 0, 0, SCREEN_H);
}

/* ------------------------------------------------------------ the game */
static void save(void) {
  sv.magic = 'S', sv.version = 1, sv.sel = (uint8_t)sel;
  np_file_write(SAVE_NAME, &sv, sizeof sv);
}

int main(void) {
  np_app_begin();
  color_t *their_strip = gfx_buf;
  gfx_buf = strip;
  units();
  uint32_t now = (uint32_t)eadk_timing_millis(), tick = now, repeat_at = 0, shown = ~0u, len = 0;
  seed = eadk_random() ^ now;
  if (!seed) seed = 1;
  /* the save; whatever it holds, a game this one could have saved */
  const uint8_t *d = np_file_read(SAVE_NAME, &len);
  bool ok = d && len == sizeof sv && d[0] == 'S' && d[1] == 1;
  for (uint32_t i = 0; i < sizeof sv; i++) ((uint8_t *)&sv)[i] = ok ? d[i] : 0;
  for (int c = 0; c < 81; c++)
    if (sv.sol[c] - 1u > 8 || (sv.val[c] & 15) > 9) sv.flags &= (uint8_t)~1;
  sel = sv.sel % 81, sv.diff &= 3;
  if (!(sv.flags & 2)) ov = OV_HELP, sv.flags |= 2;
  uint32_t held = keys(), stale = held; /* keys still down from the launcher wait to be let go */
  for (;;) {
    now = (uint32_t)eadk_timing_millis();
    uint32_t k = keys(), hit = k & ~held, arrows = k & 15 & ~(stale &= k);
    if (hit & 15) repeat_at = now + 250; /* an arrow held repeats */
    else if (arrows && (int32_t)(now - repeat_at) >= 0) hit |= arrows, repeat_at = now + 80;
    held = k;
    if (k & (KEY(eadk_key_home) | KEY(eadk_key_on_off))) break;
    int dx = !!(hit & KEY(eadk_key_right)) - !!(hit & KEY(eadk_key_left));
    int dy = !!(hit & KEY(eadk_key_down)) - !!(hit & KEY(eadk_key_up));
    ok = hit & (KEY(eadk_key_ok) | KEY(eadk_key_exe));
    if (ov) {
      int i = menu = (menu + dy) & (buttons[ov] - 1), to = ok ? leads[ov][i] : ov; /* (1, 2 or 4 buttons) */
      if (dy) quit_ok = 0;
      if (hit & KEY(eadk_key_back)) {
        if (ov == OV_LEVEL && !(sv.flags & 1)) break; /* nothing to go back to */
        if (ov != OV_WON) to = OV_NONE;
      }
      if (to == QUIT && (ov == OV_WON || quit_ok)) break;
      if (to == QUIT) to = OV_MENU, quit_ok = 1; /* Quit game asks first */
      if (to != ov) menu = to == OV_LEVEL ? sv.diff : 0;
      if (to == NEW) {
        sv.diff = (uint8_t)i, sv.secs = sv.mistakes = 0, sv.flags |= 1, ucount = 0, sel = 40;
        generate();
        to = OV_NONE;
      }
      ov = to;
    } else if (hit & KEY(eadk_key_back)) {
      ov = OV_MENU, menu = quit_ok = 0;
    } else if (hit) {
      int c = sel = (unit[sel][0] + dy + 9) % 9 * 9 + (unit[sel][1] + dx) % 9; /* (around the edges) */
      notes ^= ok;
      for (int d = 1; d <= 9; d++)
        if (hit >> digit_key[d - 1] & 1) {
          if (!notes) write(c, d);
          else if (!sv.val[c]) set(c, 0, sv.notes[c] ^ 1 << d, 1);
        }
      if (hit & KEY(eadk_key_zero) && sv.val[c] < 16) set(c, 0, 0, 1);
      if (hit & KEY(eadk_key_backspace)) undo_move();
      if (hit & KEY(eadk_key_shift)) hint();
    }
    if (!ov && !(sv.flags & 1)) ov = OV_LEVEL, menu = sv.diff; /* no game: choose one */
    if (hit) save();
    /* the clock runs while a game is played */
    if (!ov) sv.secs += (now - tick) / 1000, tick += (now - tick) / 1000 * 1000;
    else tick = now;
    if (hit || sv.secs != shown) shown = sv.secs, draw();
    eadk_timing_msleep(20);
  }
  save();
  gfx_buf = their_strip;
  return np_app_end();
}
