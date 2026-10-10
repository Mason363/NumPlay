/* Matrices: NumPlay dressed as a calculator app, drawn like the calculator's
 * own apps (their fonts, colours and layout): a matrix to fill in, its results,
 * and a menu on the Toolbox key. The secret chosen in Settings (a key, the
 * Examples item of the menu, or the Konami code) opens NumPlay; Home quits. */
#include <eadk.h>
#include <string.h>
#include "np.h"
#include "sys.h"
#include "../../games/common/np_text.h"

#define C_BAR RGB(0xFF, 0xB6, 0x31)
#define C_TAB RGB(0x63, 0x69, 0x73)
#define C_BG RGB(0xE7, 0xE7, 0xEF)
#define C_WHITE 0xFFFF
#define C_SEL RGB(0xD6, 0xD7, 0xE7)
#define C_LINE RGB(0xC6, 0xC3, 0xC6)
#define C_TEXT 0x0000
#define C_GRAY RGB(0x94, 0x9A, 0xA5)

#define MAXN 4
#define TOP 40 /* under the status and tab bars */

/* calculator keys (eadk numbering) */
enum {
  KEY_LEFT = 0, KEY_UP = 1, KEY_DOWN = 2, KEY_RIGHT = 3, KEY_OK = 4, KEY_BACK = 5, KEY_HOME = 6, KEY_ONOFF = 8,
  KEY_XNT = 14, KEY_VAR = 15, KEY_TOOLBOX = 16, KEY_BACKSPACE = 17, KEY_PI = 27, KEY_SQRT = 28, KEY_MINUS = 46,
  KEY_DOT = 49, KEY_EXE = 52
};
static const int8_t digit_keys[10] = {48, 42, 43, 44, 36, 37, 38, 30, 31, 32};
static const uint8_t secret_keys[NP_SECRET_COUNT] = {KEY_XNT, KEY_VAR, KEY_TOOLBOX, KEY_PI, KEY_SQRT, 255, 255};
/* the hint, if Settings keeps it: the secret's name, bottom left in light gray */
static const char *const secret_hints[NP_SECRET_COUNT] = {"x,n,t", "var", T("Toolbox"), "\xCF\x80", "\xE2\x88\x9A", T("Examples"),
                                                          T("Konami code")};
/* the Konami code: up, up, down, down, left, right, left, right (an idea of LanoCodes17's) */
static const uint8_t konami[8] = {KEY_UP, KEY_UP, KEY_DOWN, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_LEFT, KEY_RIGHT};
static const char *hint;

static struct {
  int n, tab, row, col; /* row -1: the dimension line */
  float a[MAXN][MAXN];
  char edit[12];        /* the cell being typed, or empty */
  int menu;             /* -1 closed, else the selected item */
} M;

static void fill(int x, int y, int w, int h, color_t c) {
  if (w > 0 && h > 0) eadk_display_push_rect_uniform((eadk_rect_t){x, y, w, h}, c);
}
static void text(const char *s, int x, int y, bool big, color_t fg, color_t bg) {
  np_display_draw_string(s, (eadk_point_t){x, y}, big, fg, bg);
}
static int width(const char *s, bool big) {
  int n = 0;
  /* characters, not UTF-8 bytes; a Chinese one takes two cells (launcher/src/compat.c) */
  for (; *s; s++) n += (*s & 0xC0) == 0x80 ? 0 : NP_TEXT_EXTRA && (uint8_t)*s >= 0xE3 ? 2 : 1;
  return n * (big ? 10 : 7);
}
static void ctext(const char *s, int cx, int y, bool big, color_t fg, color_t bg) {
  text(s, cx - width(s, big) / 2, y, big, fg, bg);
}

/* ---- numbers, without printf */
static void fmt(float v, char *o) {
  if (v != v) {
    strcpy(o, "undef");
    return;
  }
  bool neg = v < 0;
  if (neg) v = -v;
  if (v >= 1e9f) {
    strcpy(o, neg ? "-1E9+" : "1E9+");
    return;
  }
  /* three decimals at most, trailing zeros dropped */
  uint32_t ip, fp;
  if (v >= 1e6f) ip = (uint32_t)(v + 0.5f), fp = 0;
  else {
    uint32_t scaled = (uint32_t)(v * 1000 + 0.5f);
    ip = scaled / 1000, fp = scaled % 1000;
  }
  if (neg && (ip || fp)) *o++ = '-';
  char t[12];
  int n = 0;
  do t[n++] = (char)('0' + ip % 10);
  while (ip /= 10);
  while (n) *o++ = t[--n];
  if (fp) {
    *o++ = '.';
    for (int d = 100; d && fp; d /= 10) *o++ = (char)('0' + fp / d), fp %= d;
  }
  *o = 0;
}

static float parse(const char *s) {
  float sign = 1, v = 0, scale = 0;
  if (*s == '-') sign = -1, s++;
  for (; *s; s++) {
    if (*s == '.') scale = scale ? scale : 1;
    else if (*s >= '0' && *s <= '9') {
      if (scale) scale /= 10, v += (float)(*s - '0') * scale;
      else v = v * 10 + (float)(*s - '0');
    }
  }
  return sign * v;
}

/* ---- linear algebra */
static float det(float m[MAXN][MAXN], int n) {
  float a[MAXN][MAXN], d = 1;
  memcpy(a, m, sizeof a);
  for (int c = 0; c < n; c++) {
    int p = c;
    for (int r = c + 1; r < n; r++)
      if ((a[r][c] < 0 ? -a[r][c] : a[r][c]) > (a[p][c] < 0 ? -a[p][c] : a[p][c])) p = r;
    if (a[p][c] == 0) return 0;
    if (p != c) {
      for (int k = 0; k < n; k++) {
        float t = a[c][k];
        a[c][k] = a[p][k], a[p][k] = t;
      }
      d = -d;
    }
    d *= a[c][c];
    for (int r = c + 1; r < n; r++) {
      float f = a[r][c] / a[c][c];
      for (int k = c; k < n; k++) a[r][k] -= f * a[c][k];
    }
  }
  return d;
}

static bool inverse(float m[MAXN][MAXN], int n, float out[MAXN][MAXN]) {
  float a[MAXN][2 * MAXN];
  for (int r = 0; r < n; r++)
    for (int c = 0; c < 2 * n; c++) a[r][c] = c < n ? m[r][c] : (c - n == r);
  for (int c = 0; c < n; c++) {
    int p = c;
    for (int r = c + 1; r < n; r++)
      if ((a[r][c] < 0 ? -a[r][c] : a[r][c]) > (a[p][c] < 0 ? -a[p][c] : a[p][c])) p = r;
    if ((a[p][c] < 0 ? -a[p][c] : a[p][c]) < 1e-6f) return false;
    for (int k = 0; k < 2 * n; k++) {
      float t = a[c][k];
      a[c][k] = a[p][k], a[p][k] = t;
    }
    float d = a[c][c];
    for (int k = 0; k < 2 * n; k++) a[c][k] /= d;
    for (int r = 0; r < n; r++) {
      if (r == c) continue;
      float f = a[r][c];
      for (int k = 0; k < 2 * n; k++) a[r][k] -= f * a[c][k];
    }
  }
  for (int r = 0; r < n; r++)
    for (int c = 0; c < n; c++) out[r][c] = a[r][n + c];
  return true;
}

/* ---- the calculator's chrome */
static void status_bar(void) {
  fill(0, 0, SCREEN_W, 18, C_BAR);
  text("rad", 4, 2, false, C_WHITE, C_BAR);
  ctext(T("MATRICES"), 160, 2, false, C_WHITE, C_BAR);
  /* battery */
  int level = np_battery_level();
  fill(296, 5, 15, 8, C_WHITE);
  fill(297, 6, 13, 6, C_BAR);
  fill(311, 7, 2, 4, C_WHITE);
  int bars = level < 0 ? 3 : NP_MIN(level, 3);
  fill(298, 7, bars * 11 / 3, 4, C_WHITE);
}

static void tabs(void) {
  static const char *const names[2] = {T("Matrix"), T("Results")};
  for (int t = 0; t < 2; t++) {
    color_t bg = t == M.tab ? C_WHITE : C_TAB, fg = t == M.tab ? C_TAB : C_WHITE;
    fill(t * 160, 18, 160, 22, bg);
    ctext(names[t], t * 160 + 80, 22, false, fg, bg);
  }
}

/* ---- the Matrix tab: its dimension, then a table like the Statistics app's */
static int cell_w(void) { return M.n <= 3 ? 80 : 64; }
static int grid_x(void) { return (SCREEN_W - (24 + M.n * cell_w())) / 2 + 24; }
#define GRID_Y 92
#define CELL_H 26

static void dimension_line(void) {
  color_t bg = M.row < 0 ? C_SEL : C_WHITE;
  fill(0, 46, SCREEN_W, 30, bg);
  text(T("Dimension"), 10, 53, true, C_TEXT, bg);
  char s[8] = "0\xC3\x97" "0"; /* n×n */
  s[0] = (char)('0' + M.n), s[3] = (char)('0' + M.n);
  text(s, SCREEN_W - 12 - width(s, true), 53, true, C_TEXT, bg);
}

static void cell(int r, int c) {
  int x = grid_x() + c * cell_w(), y = GRID_Y + 22 + r * CELL_H, w = cell_w() - 1;
  bool sel = M.tab == 0 && M.row == r && M.col == c;
  color_t bg = sel ? C_SEL : C_WHITE;
  fill(x, y, w, CELL_H - 1, bg);
  char s[14];
  if (sel && M.edit[0]) {
    strcpy(s, M.edit);
    text(s, x + w - 6 - width(s, false), y + 6, false, C_TEXT, bg);
    fill(x + w - 5, y + 5, 1, 14, C_TEXT); /* cursor */
  } else {
    fmt(M.a[r][c], s);
    text(s, x + w - 6 - width(s, false), y + 6, false, C_TEXT, bg);
  }
}

static void matrix_tab(void) {
  fill(0, TOP, SCREEN_W, SCREEN_H - TOP, C_BG);
  dimension_line();
  int gx = grid_x(), cw = cell_w();
  /* column headers */
  fill(gx - 24, GRID_Y, 24 + M.n * cw, 22, C_WHITE);
  for (int c = 0; c < M.n; c++) {
    char h[4] = "C1";
    h[1] = (char)('1' + c);
    ctext(h, gx + c * cw + cw / 2, GRID_Y + 4, false, C_GRAY, C_WHITE);
  }
  fill(gx - 24, GRID_Y + 21, 24 + M.n * cw, 1, C_LINE);
  for (int r = 0; r < M.n; r++) {
    fill(gx - 24, GRID_Y + 22 + r * CELL_H, 23, CELL_H - 1, C_WHITE);
    char h[4] = "R1";
    h[1] = (char)('1' + r);
    ctext(h, gx - 12, GRID_Y + 28 + r * CELL_H, false, C_GRAY, C_WHITE);
    fill(gx - 24, GRID_Y + 21 + (r + 1) * CELL_H, 24 + M.n * cw, 1, C_LINE);
    for (int c = 0; c < M.n; c++) {
      fill(gx + c * cw + cw - 1, GRID_Y + 22 + r * CELL_H, 1, CELL_H, C_LINE);
      cell(r, c);
    }
  }
  fill(gx - 1, GRID_Y + 22, 1, M.n * CELL_H, C_LINE);
}

/* ---- the Results tab */
static void small_matrix(float m[MAXN][MAXN], int x, int y) {
  int cw = 46;
  fill(x, y, 2, M.n * 16, C_TEXT);
  fill(x, y, 5, 1, C_TEXT);
  fill(x, y + M.n * 16 - 1, 5, 1, C_TEXT);
  int x1 = x + 8 + M.n * cw;
  fill(x1, y, 2, M.n * 16, C_TEXT);
  fill(x1 - 3, y, 5, 1, C_TEXT);
  fill(x1 - 3, y + M.n * 16 - 1, 5, 1, C_TEXT);
  for (int r = 0; r < M.n; r++)
    for (int c = 0; c < M.n; c++) {
      char s[14];
      fmt(m[r][c], s);
      text(s, x + 4 + c * cw + cw - width(s, false), y + 1 + r * 16, false, C_TEXT, C_WHITE);
    }
}

static void results_tab(void) {
  fill(0, TOP, SCREEN_W, SCREEN_H - TOP, C_BG);
  char s[16];
  int y = 46;
  fill(0, y, SCREEN_W, 28, C_WHITE);
  text(T("Determinant"), 10, y + 7, false, C_GRAY, C_WHITE);
  fmt(det(M.a, M.n), s);
  text(s, SCREEN_W - 12 - width(s, true), y + 5, true, C_TEXT, C_WHITE);
  y += 29;
  fill(0, y, SCREEN_W, 28, C_WHITE);
  text(T("Trace"), 10, y + 7, false, C_GRAY, C_WHITE);
  float tr = 0;
  for (int i = 0; i < M.n; i++) tr += M.a[i][i];
  fmt(tr, s);
  text(s, SCREEN_W - 12 - width(s, true), y + 5, true, C_TEXT, C_WHITE);
  y += 29;
  int h = M.n * 16 + 12;
  fill(0, y, SCREEN_W, SCREEN_H - y, C_WHITE);
  text(T("Inverse"), 10, y + 6, false, C_GRAY, C_WHITE);
  float inv[MAXN][MAXN];
  if (inverse(M.a, M.n, inv)) small_matrix(inv, SCREEN_W - 20 - (8 + M.n * 46), y + 6);
  else text(T("Not invertible"), SCREEN_W - 12 - width(T("Not invertible"), false), y + 6, false, C_TEXT, C_WHITE);
  y += h;
  fill(0, y, SCREEN_W, 1, C_LINE);
  text(T("Transpose"), 10, y + 6, false, C_GRAY, C_WHITE);
  float t[MAXN][MAXN];
  for (int r = 0; r < M.n; r++)
    for (int c = 0; c < M.n; c++) t[r][c] = M.a[c][r];
  small_matrix(t, SCREEN_W - 20 - (8 + M.n * 46), y + 6);
}

/* ---- the Toolbox menu */
static const char *const menu_items[3] = {T("Identity matrix"), T("Clear"), T("Examples")};

static void menu_draw(void) {
  int x = 150, y = 60, w = 162;
  fill(x - 1, y - 1, w + 2, 1 + 22 + 3 * 30 + 1, C_LINE);
  fill(x, y, w, 22, C_TAB);
  text(T("Matrices"), x + 8, y + 4, false, C_WHITE, C_TAB);
  for (int i = 0; i < 3; i++) {
    color_t bg = i == M.menu ? C_SEL : C_WHITE;
    fill(x, y + 22 + i * 30, w, 29, bg);
    text(menu_items[i], x + 8, y + 22 + i * 30 + 7, false, C_TEXT, bg);
    fill(x, y + 22 + i * 30 + 29, w, 1, C_LINE);
  }
}

static void redraw(void) {
  tabs();
  if (M.tab == 0) matrix_tab();
  else results_tab();
  if (hint) text(hint, 4, SCREEN_H - 16, false, M.tab ? C_LINE : RGB(0xC6, 0xC7, 0xD2), M.tab ? C_WHITE : C_BG);
  if (M.menu >= 0) menu_draw();
}

static void commit(void) {
  if (!M.edit[0]) return;
  M.a[M.row][M.col] = parse(M.edit);
  M.edit[0] = 0;
}

static void example(void) {
  static const int8_t ex[MAXN][MAXN] = {{2, 1, 0, 3}, {1, 3, 2, 0}, {0, 2, 4, 1}, {3, 0, 1, 2}};
  for (int r = 0; r < MAXN; r++)
    for (int c = 0; c < MAXN; c++) M.a[r][c] = ex[r][c];
}

bool np_matrices(const np_config_t *cfg) {
  memset(&M, 0, sizeof M);
  M.n = 3;
  M.menu = -1;
  for (int i = 0; i < MAXN; i++) M.a[i][i] = 1;
  hint = cfg->hint ? secret_hints[cfg->secret] : NULL;
  status_bar();
  redraw();
  uint64_t prev = eadk_keyboard_scan();
  uint32_t repeat_at = 0;
  int code = 0; /* the Konami code's keys pressed so far */
  for (int frame = 0;; frame++) {
    uint64_t k = eadk_keyboard_scan(), hit = k & ~prev;
    if (cfg->secret == NP_SECRET_KONAMI && hit) { /* fresh presses only, not held arrows repeating */
      if (hit == 1ull << konami[code]) code++;
      else code = hit == 1ull << KEY_UP ? (code == 2 ? 2 : 1) : 0; /* up, up, up... still the start */
      if (code == 8) return true;
    }
    uint32_t now = np_millis();
    const uint64_t arrows = 0xF;
    if (hit & arrows) repeat_at = now + 400;
    else if ((k & arrows) && (int32_t)(now - repeat_at) >= 0) hit |= k & arrows, repeat_at = now + 110;
    prev = k;
#define HIT(key) ((hit >> (key)) & 1)
    if (HIT(KEY_HOME) || HIT(KEY_ONOFF)) return false;
    uint8_t sk = secret_keys[cfg->secret];
    if (sk != 255 && HIT(sk)) return true;
    if (frame % 600 == 0) status_bar(); /* the battery, now and then */
    if (!hit) {
      np_sleep(16);
      continue;
    }
    bool dirty = false;
    if (M.menu >= 0) {
      if (HIT(KEY_UP) && M.menu > 0) M.menu--, dirty = true;
      if (HIT(KEY_DOWN) && M.menu < 2) M.menu++, dirty = true;
      if (HIT(KEY_BACK) || HIT(KEY_TOOLBOX)) M.menu = -1, dirty = true;
      if (HIT(KEY_OK) || HIT(KEY_EXE)) {
        if (M.menu == 2 && cfg->secret == NP_SECRET_MENU) return true;
        if (M.menu == 0) {
          memset(M.a, 0, sizeof M.a);
          for (int i = 0; i < MAXN; i++) M.a[i][i] = 1;
        } else if (M.menu == 1) {
          memset(M.a, 0, sizeof M.a);
        } else {
          example();
        }
        M.menu = -1;
        dirty = true;
      }
      if (dirty) redraw();
      continue;
    }
    if (HIT(KEY_TOOLBOX)) {
      commit();
      M.menu = 0;
      redraw();
      continue;
    }
    /* tabs: Up from the top goes to the tab bar, like the calculator's apps */
    if (M.tab == 1) {
      if (HIT(KEY_LEFT)) M.tab = 0, dirty = true;
      if (dirty) redraw();
      continue;
    }
    if (HIT(KEY_RIGHT) && M.row < 0) {
      if (M.n < MAXN) M.n++, dirty = true;
    } else if (HIT(KEY_LEFT) && M.row < 0) {
      if (M.n > 1) M.n--, dirty = true;
    } else if (HIT(KEY_UP) || HIT(KEY_DOWN) || HIT(KEY_LEFT) || HIT(KEY_RIGHT)) {
      commit();
      int r = M.row, c = M.col;
      if (HIT(KEY_UP)) r--;
      if (HIT(KEY_DOWN)) r = NP_MIN(r + 1, M.n - 1);
      if (HIT(KEY_LEFT)) c = NP_MAX(c - 1, 0);
      if (HIT(KEY_RIGHT)) {
        if (c == M.n - 1) {
          M.tab = 1; /* past the last column: the results */
          redraw();
          continue;
        }
        c++;
      }
      r = NP_MAX(r, -1);
      int or_ = M.row, oc = M.col;
      M.row = r, M.col = c;
      if (or_ < 0 || r < 0) {
        dimension_line();
        if (or_ >= 0) cell(or_, oc);
        if (r >= 0) cell(r, c);
      } else {
        cell(or_, oc);
        cell(r, c);
      }
      continue;
    } else if (M.row >= 0) {
      size_t len = strlen(M.edit);
      for (int d = 0; d < 10; d++)
        if (HIT(digit_keys[d]) && len < 9) M.edit[len++] = (char)('0' + d), M.edit[len] = 0;
      if (HIT(KEY_DOT) && len < 9 && !strchr(M.edit, '.')) M.edit[len++] = '.', M.edit[len] = 0;
      if (HIT(KEY_MINUS) && len == 0) M.edit[len++] = '-', M.edit[len] = 0;
      if (HIT(KEY_BACKSPACE)) {
        if (len) M.edit[--len] = 0;
        else M.a[M.row][M.col] = 0;
      }
      if (HIT(KEY_OK) || HIT(KEY_EXE)) {
        commit();
        if (M.col < M.n - 1) M.col++;
        else if (M.row < M.n - 1) M.row++, M.col = 0;
        matrix_tab();
        continue;
      }
      cell(M.row, M.col);
      continue;
    }
    if (dirty) {
      M.row = NP_MIN(M.row, M.n - 1), M.col = NP_MIN(M.col, M.n - 1);
      redraw();
    }
  }
}
