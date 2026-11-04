/* launcher/src/gfx.c's drawing on the TI-84 Plus CE (gfx.c includes this file there).
 *
 * The CE runs about two million instructions a second, so a frame can't afford C work on every pixel:
 *  - a strip is drawn and sent only when what the scene draws in it changed. The scene runs once to list its
 *    calls (their arguments, the texts' letters), compared with the strip's list from the frame before; the screen's
 *    rows say which strip they show (np_ce_rows, cleared by anything else drawing there);
 *  - rows are filled by the C library's LDIR copies (wmemset, an overlapping memcpy for patterns);
 *  - colours mix with the eZ80's 8-bit multiplies (ce_mix) or tables, never 32-bit math;
 *  - rounded corners, outlines and shadows measure their distances in a table instead of with sqrtf.
 * The pictures are the NumWorks's, to a rounding of their anti-aliased edges. */
#include <stdlib.h>
#include <sys/lcd.h>
#include <wchar.h>
#include "ce_draw.h"

extern uint8_t np_ce_rows[SCREEN_H];

/* ------------------------------------------------------------ colours */
/* (a * (32 - t) + b * t) / 32 for each of red, green and blue, t = 0..32: gfx_mix's own result */
#define ce_mix gfx_ce_mix
__attribute__((naked, used)) color_t gfx_ce_mix(color_t a, color_t b, unsigned t) { __asm__(CE_MIX_ASM); }

color_t gfx_mix(color_t a, color_t b, int t) { return ce_mix(a, b, (unsigned)t); }

/* a run of pixels mixed with one colour: five pages of tables (256-aligned) for each of the last two colours and
 * levels: the red of a high byte, the blue of a low byte, the green from each byte, then the green mixed (its
 * high byte's part, and at +64 its low byte's) */
#define DMAX 48 /* (corners' distances: rounded_box below) */
static uint8_t *mixtabs[2];
static unsigned mix_key[2];
static uint8_t mix_next;

__attribute__((naked)) static void ce_blend(color_t *p, unsigned n, const uint8_t *t) { __asm__(CE_BLEND_ASM); }

static void setup(void);
static const uint8_t *mixtab(color_t c, unsigned t) {
  unsigned key = c | t << 16;
  for (int i = 0; i < 2; i++)
    if (mixtabs[i] && mix_key[i] == key) return mixtabs[i];
  int i = mix_next;
  if (!mixtabs[i]) setup();
  if (!mixtabs[i]) return 0;
  mix_next ^= 1;
  uint8_t *m = mixtabs[i];
  mix_key[i] = key;
  ce_tables(m, c, t);
  return m;
}

static void span(color_t *p, int n, color_t c) {
  if (n > 0) wmemset((wchar_t *)p, (wchar_t)c, (size_t)n);
}

static void span_alpha(color_t *p, int n, color_t c, int t) {
  if (t >= 32) {
    span(p, n, c);
    return;
  }
  if (t <= 0 || n <= 0) return;
  const uint8_t *m = n < 12 ? 0 : mixtab(c, (unsigned)t);
  if (!m) {
    for (; n; n--, p++) *p = ce_mix(*p, c, (unsigned)t);
    return;
  }
  for (; n > 0; n -= 256, p += 256) ce_blend(p, (unsigned)(n >= 256 ? 0 : n), m);
}

/* ------------------------------------------------------------ the strip cache */
/* Each strip has a list of the calls drawing it (unsigned values: the call's kind and length, its arguments; a
 * text's letters after them), kept from one frame to the next. A scene lists its calls either for one strip at a
 * time, then draws the strips whose list changed, or (gfx_pure) for all of them at once: drawing a strip is then
 * going through its list. When only some calls changed, in place, only the columns they cover (before and now)
 * are drawn again. */
#define SLOTS 15 /* one for each 16 rows, by where a strip starts */
bool gfx_listing, gfx_pure;
static uint8_t *lists; /* every strip's list in turn: off and cap bytes each (a list longer than its cap is drawn
                         by its scene every time, until the lists are laid out again) */
static unsigned pool, off[SLOTS], cap[SLOTS], need[SLOTS], list_len[SLOTS];
typedef struct {
  uint8_t *at, *end, *old_end;
  bool changed, whole, full; /* full: every column changed */
  int x0, x1;                /* else the columns that did */
} cursor_t;
static cursor_t cur[SLOTS];
static int list_base = -1;          /* the first strip's row */
static cursor_t *strip_of[SCREEN_H]; /* each row's strip, from there */
static int gx0, gx1 = SCREEN_W; /* the columns drawn */

enum { L_FILL = 1, L_ALPHA, L_VGRAD, L_RRECT, L_OUTLINE, L_SHADOW, L_IMAGE, L_MASK, L_CORNERS, L_TEXT, L_TEXTC, L_TEXTR, L_CALL };

/* the columns a call draws in (a little more for a text) */
static void widen(cursor_t *k, const uint8_t *e) {
  const int *v = (const int *)e + 1;
  int a, b;
  switch (e[0]) {
  case L_FILL:
  case L_ALPHA:
  case L_VGRAD:
  case L_RRECT:
  case L_OUTLINE:
  case L_CORNERS:
  case L_CALL: a = v[0], b = v[0] + v[2]; break;
  case L_SHADOW: a = v[0] - v[5], b = v[0] + v[2] + v[5]; break;
  case L_IMAGE: a = v[5], b = v[5] + v[7]; break;
  case L_MASK: a = v[3], b = v[3] + v[1]; break;
  case L_TEXT:
  case L_TEXTC:
  case L_TEXTR: {
    char t[256];
    memcpy(t, v + 6, (size_t)v[5]);
    t[v[5]] = 0;
    int w = gfx_text_width((const np_font_t *)v[0], t), kind = e[0];
    a = kind == L_TEXT ? v[1] : kind == L_TEXTC ? v[1] - w / 2 : v[1] - w;
    b = a + w + 8, a -= 8;
    break;
  }
  default: k->full = true; return;
  }
  if (a < k->x0) k->x0 = a;
  if (b > k->x1) k->x1 = b;
}

static void put(cursor_t *k, const void *v, unsigned n) {
  if (!k->whole) {
    k->at += n;
    return;
  }
  if ((unsigned)(k->end - k->at) < n) {
    k->whole = false, k->changed = k->full = true;
    k->at += n; /* (to know how much it needs) */
    return;
  }
  if (!k->full || !k->changed) {
    if (memcmp(k->at, v, n)) {
      if (k->at + n > k->old_end || *(const unsigned *)k->at != *(const unsigned *)v) k->full = true; /* (not in place) */
      else widen(k, k->at), widen(k, v);
      k->changed = true;
      memcpy(k->at, v, n);
    }
  } else {
    memcpy(k->at, v, n);
  }
  k->at += n;
}
/* a call drawing rows top to bottom: in the list of each strip there (n bytes) */
static void add(int top, int bottom, const void *v, unsigned n) {
  top = NP_MAX(top, gfx_y0), bottom = NP_MIN(bottom, gfx_y1);
  if (top >= bottom) return;
  cursor_t *k = strip_of[top], *e = strip_of[bottom - 1];
  for (; k <= e; k++) put(k, v, n);
}
void gfx_list(const void *p, unsigned bytes) {
  unsigned v[16];
  for (; bytes; p = (const uint8_t *)p + 45) {
    unsigned n = bytes < 45 ? bytes : 45;
    v[(n + 2) / 3] = 0;
    memcpy(v + 1, p, n);
    v[0] = (unsigned)(3 + (n + 2) / 3 * 3) << 8; /* (kind 0: drawn by the scene, never gone through) */
    add(gfx_y0, gfx_y1, v, 3 + (n + 2) / 3 * 3);
    bytes -= n;
  }
}
void gfx_list_always(void) {
  if (!gfx_listing) return;
  for (cursor_t *k = strip_of[gfx_y0], *e = strip_of[gfx_y1 - 1]; k <= e; k++)
    k->whole = false, k->changed = k->full = true;
}
#define LIST(top, bottom, kind, ...)                                                                      \
  do {                                                                                                    \
    if (gfx_listing) {                                                                                    \
      if ((top) < gfx_y1 && (bottom) > gfx_y0) {                                                          \
        unsigned v_[] = {0, __VA_ARGS__};                                                                 \
        v_[0] = (unsigned)(kind) | (unsigned)sizeof v_ << 8;                                              \
        add((top), (bottom), v_, sizeof v_);                                                              \
      }                                                                                                   \
      return;                                                                                             \
    }                                                                                                     \
  } while (0)

/* fn(a) draws in the box x, y, w, h what a[0..n) say: one call in the lists */
void gfx_call(void (*fn)(const int *a), const int *a, int n, int x, int y, int w, int h) {
  if (!gfx_listing || n > 8) {
    fn(a);
    return;
  }
  if (y >= gfx_y1 || y + h <= gfx_y0) return;
  unsigned v[7 + 8];
  v[0] = L_CALL | (unsigned)(7 + n) * 3 << 8, v[1] = (unsigned)x, v[2] = (unsigned)y, v[3] = (unsigned)w;
  v[4] = (unsigned)h, v[5] = (unsigned)fn, v[6] = (unsigned)n;
  memcpy(v + 7, a, (size_t)n * sizeof(int));
  add(y, y + h, v, (unsigned)(7 + n) * 3);
}

/* a text (gfx_text, gfx_text_center, gfx_text_right) in the lists */
void gfx_ce_text(int kind, const np_font_t *f, int x, int baseline, const char *s, color_t c, int a) {
  int top = baseline - f->ascent, bottom = baseline + f->descent;
  if (top >= gfx_y1 || bottom <= gfx_y0) return;
  unsigned v[7 + 86], n = (unsigned)strlen(s);
  if (n > 255) {
    int y0 = gfx_y0, y1 = gfx_y1;
    gfx_y0 = NP_MAX(top, y0), gfx_y1 = NP_MIN(bottom, y1);
    gfx_list_always();
    gfx_y0 = y0, gfx_y1 = y1;
    return;
  }
  unsigned words = 7 + (n + 2) / 3;
  v[0] = (unsigned)kind | words * 3 << 8, v[1] = (unsigned)f, v[2] = (unsigned)x, v[3] = (unsigned)baseline, v[4] = c;
  v[5] = (unsigned)a, v[6] = n;
  v[words - 1] = 0;
  memcpy(v + 7, s, n);
  add(top, bottom, v, words * 3);
}

/* the strip's list began (y its first row, h its rows) */
static void begin(int s, int y, int h, gfx_scene_t scene, void *ctx) {
  cursor_t *k = cur + s;
  k->at = lists + off[s], k->end = k->at + cap[s];
  k->old_end = list_len[s] == ~0u ? k->at : k->at + list_len[s];
  k->changed = k->full = false, k->whole = true, k->x0 = SCREEN_W, k->x1 = 0;
  const unsigned head[] = {18u << 8, (unsigned)y, (unsigned)h, (unsigned)scene, (unsigned)ctx, (unsigned)gfx_pure};
  put(k, head, sizeof head);
}

static void replay(const uint8_t *e, const uint8_t *end);
/* the strip's list ended: it's drawn again where it changed */
static void end(gfx_scene_t scene, void *ctx, int s, int y, int h) {
  cursor_t *k = cur + s;
  unsigned len = (unsigned)(k->at - (lists + off[s]));
  need[s] = len;
  if (!k->whole) len = ~0u;
  if (len != list_len[s]) k->changed = k->full = true;
  list_len[s] = len;
  int r = 0;
  while (r < h && np_ce_rows[y + r] == s + 1) r++;
  if (r < h) k->changed = k->full = true; /* (the screen there isn't the strip's) */
  if (!k->changed) return;
  int x0 = k->full ? 0 : NP_MAX(k->x0, 0), x1 = k->full ? SCREEN_W : NP_MIN(k->x1, SCREEN_W);
  if (x0 >= x1) return;
  gfx_y0 = y, gfx_y1 = y + h, gx0 = x0, gx1 = x1;
  if (gfx_pure && k->whole) replay(lists + off[s], lists + off[s] + len);
  else scene(ctx);
  gx0 = 0, gx1 = SCREEN_W;
  if (x1 - x0 == SCREEN_W) np_push(0, y, SCREEN_W, h, gfx_buf);
  else
    for (color_t *q = (color_t *)lcd_Ram + y * SCREEN_W + x0, *b = gfx_buf + x0, *e = b + h * SCREEN_W; b < e;
         q += SCREEN_W, b += SCREEN_W)
      memcpy(q, b, (size_t)(x1 - x0) * 2); /* (as np_push would, a row at a time) */
  memset(np_ce_rows + y, k->whole ? s + 1 : 0, (size_t)h);
}

/* a strip's list didn't fit: the room shared again by what each needs, when there's enough */
static void layout(void) {
  unsigned total = 0;
  bool short_ = false;
  for (int s = 0; s < SLOTS; s++) total += need[s] + need[s] / 8 + 8, short_ |= need[s] > cap[s];
  if (!short_ || total > pool) return;
  unsigned spare = (pool - total) / SLOTS, at = 0;
  for (int s = 0; s < SLOTS; s++) {
    off[s] = at, cap[s] = need[s] + need[s] / 8 + 8 + spare, at += cap[s];
    list_len[s] = ~0u; /* (drawn again) */
  }
}

static bool dist_upto(int n);
static void grads_alloc(void);
/* the memory it takes, the first time: the tables, then the lists, as much as there is up to 1152 bytes a strip
 * but 6 KB left for the game (none: every strip is drawn every time) */
static void setup(void) {
  static bool ready;
  if (ready) return;
  ready = true;
  for (int i = 0; i < 2; i++) {
    uint8_t *raw = malloc(CE_TABLES + 255);
    if (!raw) break;
    uint8_t *m = (uint8_t *)(((unsigned)raw + 255) & ~255u);
    m[CE_T_GHI + 255] = 0;
    mixtabs[i] = m, mix_key[i] = ~0u;
  }
  dist_upto(DMAX);
  grads_alloc();
  for (pool = SLOTS * 1152; pool >= SLOTS * 144; pool /= 2)
    if ((lists = malloc(pool))) {
      void *rest = malloc(6144);
      if (rest) {
        free(rest);
        break;
      }
      free(lists), lists = 0;
    }
  for (int s = 0; s < SLOTS; s++) off[s] = s * (pool / SLOTS), cap[s] = pool / SLOTS, list_len[s] = ~0u;
}

void gfx_render(gfx_scene_t scene, void *ctx, int y0, int y1) {
  setup();
  if (!lists) {
    for (int y = y0; y < y1; y += STRIP_H) {
      gfx_y0 = y, gfx_y1 = NP_MIN(y + STRIP_H, y1);
      scene(ctx);
      np_push(0, y, SCREEN_W, gfx_y1 - y, gfx_buf);
    }
    return;
  }
  if (list_base != y0) {
    list_base = y0;
    for (int y = y0; y < SCREEN_H; y++) strip_of[y] = cur + (y0 >> 4) + ((y - y0) >> 4);
  }
  if (gfx_pure) { /* the scene once for every strip, then each strip that changed from its list */
    for (int y = y0; y < y1; y += STRIP_H) begin(y >> 4, y, NP_MIN(STRIP_H, y1 - y), scene, ctx);
    gfx_y0 = y0, gfx_y1 = y1;
    gfx_listing = true;
    scene(ctx);
    gfx_listing = false;
    for (int y = y0; y < y1; y += STRIP_H) end(scene, ctx, y >> 4, y, NP_MIN(STRIP_H, y1 - y));
    layout();
    return;
  }
  for (int y = y0; y < y1; y += STRIP_H) {
    int s = y >> 4, h = NP_MIN(STRIP_H, y1 - y);
    begin(s, y, h, scene, ctx);
    gfx_y0 = y, gfx_y1 = y + h;
    gfx_listing = true;
    scene(ctx);
    gfx_listing = false;
    end(scene, ctx, s, y, h);
  }
  layout();
}

/* ------------------------------------------------------------ shapes */
static const unsigned row_at[STRIP_H] = {0, 320, 640, 960, 1280, 1600, 1920, 2240, 2560, 2880, 3200, 3520, 3840,
                                         4160, 4480, 4800};
static inline color_t *px_at(int x, int y) { return gfx_buf + row_at[y - gfx_y0] + x; }

static bool clip(int *x, int *y, int *w, int *h) {
  int x0 = NP_MAX(*x, gx0), y0 = NP_MAX(*y, gfx_y0);
  int x1 = NP_MIN(*x + *w, gx1), y1 = NP_MIN(*y + *h, gfx_y1);
  if (x1 <= x0 || y1 <= y0) return false;
  *x = x0, *y = y0, *w = x1 - x0, *h = y1 - y0;
  return true;
}

void gfx_fill(int x, int y, int w, int h, color_t c) {
  LIST(y, y + h, L_FILL, (unsigned)x, (unsigned)y, (unsigned)w, (unsigned)h, c);
  if (!clip(&x, &y, &w, &h)) return;
  color_t *p = px_at(x, y);
  if (w == SCREEN_W) {
    span(p, w * h, c);
    return;
  }
  if (w == 1)
    for (; h; h--, p += SCREEN_W) *p = c;
  else
    for (; h; h--, p += SCREEN_W) span(p, w, c);
}

void gfx_fill_alpha(int x, int y, int w, int h, color_t c, int a) {
  LIST(y, y + h, L_ALPHA, (unsigned)x, (unsigned)y, (unsigned)w, (unsigned)h, c, (unsigned)a);
  if (!clip(&x, &y, &w, &h)) return;
  int t = (a + 4) >> 3;
  for (color_t *p = px_at(x, y); h; h--, p += SCREEN_W) span_alpha(p, w, c, t);
}

/* Ordered dithering hides the banding of 16-bit colour in soft gradients. */
static const uint8_t bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

/* a gradient's rows, each 4 pixels (its dither): kept for the last two gradients drawn */
typedef struct {
  uint32_t top, bottom;
  int y0, h0;
  color_t pat[SCREEN_H][4];
} grad_t;
static grad_t *grads[2];
static void grads_alloc(void) { grads[0] = malloc(sizeof(grad_t)), grads[1] = malloc(sizeof(grad_t)); }
static uint8_t grad_next;

static void grad_row(color_t *pat, int row, int y0, int h0, uint32_t top, uint32_t bottom) {
  unsigned t = h0 > 1 ? (unsigned)((row - y0) * 256 / (h0 - 1)) : 0, u = 256 - t;
  unsigned r = ((unsigned)(top >> 16 & 0xFF) * u + (unsigned)(bottom >> 16 & 0xFF) * t) >> 8;
  unsigned g = ((unsigned)(top >> 8 & 0xFF) * u + (unsigned)(bottom >> 8 & 0xFF) * t) >> 8;
  unsigned b = ((unsigned)(top & 0xFF) * u + (unsigned)(bottom & 0xFF) * t) >> 8;
  for (int i = 0; i < 4; i++) {
    unsigned d = bayer[row & 3][i];
    unsigned r5 = NP_MIN(31u, (r + (d >> 1)) >> 3), g6 = NP_MIN(63u, (g + (d >> 2)) >> 2), b5 = NP_MIN(31u, (b + (d >> 1)) >> 3);
    pat[i] = (color_t)(r5 << 11 | g6 << 5 | b5);
  }
}

void gfx_vgrad(int x, int y, int w, int h, uint32_t top, uint32_t bottom) {
  int x0 = x, y0 = y, h0 = h;
  LIST(y, y + h, L_VGRAD, (unsigned)x, (unsigned)y, (unsigned)w, (unsigned)h, (unsigned)top, (unsigned)bottom);
  if (!clip(&x, &y, &w, &h)) return;
  (void)x0;
  grad_t *gr = 0;
  for (int i = 0; i < 2; i++)
    if (grads[i] && grads[i]->top == top && grads[i]->bottom == bottom && grads[i]->y0 == y0 && grads[i]->h0 == h0)
      gr = grads[i];
  if (!gr && y0 >= 0 && y0 + h0 <= SCREEN_H) {
    if (!grads[grad_next]) grads[grad_next] = malloc(sizeof(grad_t));
    gr = grads[grad_next];
    grad_next ^= 1;
    if (gr) {
      gr->top = top, gr->bottom = bottom, gr->y0 = y0, gr->h0 = h0;
      for (int row = y0; row < y0 + h0; row++) grad_row(gr->pat[row], row, y0, h0, top, bottom);
    }
  }
  for (int j = 0; j < h; j++) {
    int row = y + j;
    color_t local[4], *pat = local;
    if (gr) pat = gr->pat[row];
    else grad_row(local, row, y0, h0, top, bottom);
    color_t *p = px_at(x, row);
    for (int i = 0; i < 4 && i < w; i++) p[i] = pat[(x + i) & 3];
    if (w > 4) memcpy(p + 4, p, (size_t)(w - 4) * 2); /* (the C library's memcpy is a forward LDIR: it repeats them) */
  }
}

/* round(256 * the distance from a corner's centre to the middle of the pixel (a, b) from it), a table up to
 * DMAX pixels from it (made as far as it was needed) */
static uint16_t *dist;
static int dist_n;
static unsigned dist_f(int a, int b) {
  float fa = a + 0.5f, fb = b + 0.5f;
  return (unsigned)(256 * __builtin_sqrtf(fa * fa + fb * fb) + 0.5f);
}
/* the table up to n, or false (then dist_f) */
static bool dist_upto(int n) {
  if (n <= dist_n) return true;
  if (n > DMAX) return false;
  if (!dist && !(dist = malloc(DMAX * DMAX * sizeof(uint16_t)))) return false;
  n = n < 16 ? 16 : n;
  for (int i = 0; i < n; i++)
    for (int j = 0; j < n; j++)
      if (i >= dist_n || j >= dist_n) dist[i * DMAX + j] = (uint16_t)dist_f(i, j);
  dist_n = n;
  return true;
}
static unsigned dist_at(int a, int b) { return dist_upto(NP_MAX(a, b) + 1) ? dist[a * DMAX + b] : dist_f(a, b); }

/* Coverage (0..32) of a rounded corner's pixel: d its distance from the centre (dist_at), r the radius */
static int corner_cov(unsigned d, int r) {
  int c = 128 + 256 * r - (int)d; /* (in 256ths of a pixel) */
  c = c < 0 ? 0 : c > 256 ? 256 : c;
  return (c + 4) >> 3;
}

/* the row of a rounded box's corner a row is in: rows from the top or bottom edge, or -1 between the corners */
static int corner_row(int j, int h, int r) { return j < r ? j : j >= h - r ? h - 1 - j : -1; }

void gfx_rrect(int x, int y, int w, int h, int r, color_t c, int a) {
  LIST(y, y + h, L_RRECT, (unsigned)x, (unsigned)y, (unsigned)w, (unsigned)h, (unsigned)r, c, (unsigned)a);
  if (w <= 0 || h <= 0 || x >= gx1 || x + w <= gx0) return;
  if (2 * r > w) r = w / 2;
  if (2 * r > h) r = h / 2;
  int ya = NP_MAX(y, gfx_y0), yb = NP_MIN(y + h, gfx_y1);
  int t = (a + 4) >> 3;
  bool table = dist_upto(r);
  for (int row = ya; row < yb; row++) {
    int jj = corner_row(row - y, h, r);
    int solid0 = x, solid1 = x + w;
    color_t *p = px_at(0, row);
    if (jj >= 0) {
      const uint16_t *d = table ? dist + (r - 1 - jj) * DMAX + r - 1 : 0; /* d[-i]: pixel i of the corner */
      for (int i = 0; i < r; i++) {
        int cov = corner_cov(d ? d[-i] : dist_f(r - 1 - jj, r - 1 - i), r);
        if (cov >= 32) break;
        int k = cov * t >> 5;
        if (k > 0) {
          if (x + i >= gx0 && x + i < gx1) p[x + i] = ce_mix(p[x + i], c, (unsigned)k);
          int xr = x + w - 1 - i;
          if (xr >= gx0 && xr < gx1) p[xr] = ce_mix(p[xr], c, (unsigned)k);
        }
        solid0 = x + i + 1;
        solid1 = x + w - i - 1;
      }
    }
    int s0 = NP_MAX(solid0, gx0), s1 = NP_MIN(solid1, gx1);
    if (s1 > s0) span_alpha(p + s0, s1 - s0, c, t);
  }
}

/* Signed distance (256ths of a pixel, negative inside) from the middle of a pixel to a rounded box: qa and qb
 * are how far the pixel is past the corner's centre, in half pixels (odd numbers) */
static int box_d(int qa, int qb, int r) {
  int l;
  if (qa > 0 && qb > 0) l = (int)dist_at((qa - 1) / 2, (qb - 1) / 2);
  else l = (qa > qb ? qa : qb) * 128;
  return l - 256 * r;
}

/* an outline's level (0..32) at qa, qb (box_d), and its inner box's distance */
static int ring(int qa, int qb, int r, int ri, int shift, int t, int *d_in) {
  *d_in = box_d(qa + shift, qb + shift, ri);
  int co = 128 - box_d(qa, qb, r), ci = 128 - *d_in;
  co = co < 0 ? 0 : co > 256 ? 256 : co;
  ci = ci < 0 ? 0 : ci > 256 ? 256 : ci;
  return ((co - ci) * t + 128) >> 8;
}
static void ring_px(color_t *p, int sx, color_t c, int k) {
  if (k > 0 && sx >= gx0 && sx < gx1) p[sx] = k >= 32 ? c : ce_mix(p[sx], c, (unsigned)k);
}

void gfx_rrect_outline(int x, int y, int w, int h, int r, int th, color_t c, int a) {
  LIST(y, y + h, L_OUTLINE, (unsigned)x, (unsigned)y, (unsigned)w, (unsigned)h, (unsigned)r, (unsigned)th, c,
       (unsigned)a);
  if (w <= 0 || h <= 0 || x >= gx1 || x + w <= gx0) return;
  int ya = NP_MAX(y, gfx_y0), yb = NP_MIN(y + h, gfx_y1);
  int t = (a + 4) >> 3, d_in;
  int ri = r > th ? r - th : 0, shift = r > th ? 0 : 2 * (th - r); /* the inner box's corner, from the outer's */
  int m0 = r > th ? r : th; /* from there to its mirror, a row near the top or bottom is the same all along */
  for (int row = ya; row < yb; row++) {
    int j = row - y, jj = j < h - 1 - j ? j : h - 1 - j;
    int qb = 2 * (r - jj) - 1;
    color_t *p = px_at(0, row);
    if (jj <= r && w > 2 * m0) {
      for (int i = 0; i < m0; i++) {
        int k = ring(2 * (r - i) - 1, qb, r, ri, shift, t, &d_in);
        ring_px(p, x + i, c, k);
        ring_px(p, x + w - 1 - i, c, k);
      }
      int k = ring(2 * (r - m0) - 1, qb, r, ri, shift, t, &d_in);
      int s0 = NP_MAX(x + m0, gx0), s1 = NP_MIN(x + w - m0, gx1);
      if (k > 0 && s1 > s0) span_alpha(p + s0, s1 - s0, c, k);
      continue;
    }
    for (int i = 0; i < w; i++) {
      int ii = i < w - 1 - i ? i : w - 1 - i;
      int k = ring(2 * (r - ii) - 1, qb, r, ri, shift, t, &d_in);
      if (d_in < -128) {
        if (i < w / 2) i = w - 1 - i - 1; /* the rest up to the mirror pixel is inside */
        continue;
      }
      ring_px(p, x + i, c, k);
    }
  }
}

void gfx_shadow(int x, int y, int w, int h, int r, int blur, int a) {
  LIST(y - blur, y + h + blur, L_SHADOW, (unsigned)x, (unsigned)y, (unsigned)w, (unsigned)h, (unsigned)r,
       (unsigned)blur, (unsigned)a);
  if (blur <= 0 || x - blur >= gx1 || x + w + blur <= gx0) return;
  int ya = NP_MAX(y - blur, gfx_y0), yb = NP_MIN(y + h + blur, gfx_y1);
  /* how dark, by the distance in eighths of a pixel */
  uint8_t dark[256];
  int n8 = NP_MIN(blur * 8, 256);
  for (int d = 0; d < n8; d++) {
    float f = 1 - (d + 0.5f) / 8 / blur;
    dark[d] = (uint8_t)((int)(f * f * a) >> 3);
  }
  int xa = x - blur, xb = x + w + blur;
  for (int row = ya; row < yb; row++) {
    int j = row - y, jj = j < h - 1 - j ? j : h - 1 - j; /* (negative outside the box) */
    int qb = 2 * (r - jj) - 1;
    color_t *p = px_at(0, row);
    for (int sx = xa; sx < xb; sx++) {
      int i = sx - x, ii = i < w - 1 - i ? i : w - 1 - i, qa = 2 * (r - ii) - 1;
      int d = box_d(qa, qb, r);
      if (d <= 0) {
        int mirror = 2 * x + w - 1 - sx; /* inside: the card will cover it */
        if (mirror > sx) sx = mirror - 1;
        continue;
      }
      if (d >= blur * 256 || (d >> 5) >= n8 || sx < gx0 || sx >= gx1) continue;
      int k = dark[d >> 5];
      if (k > 0) p[sx] = ce_mix(p[sx], 0, (unsigned)NP_MIN(k, 32));
    }
  }
}

void gfx_circle(int cx, int cy, int r, color_t c, int a) { gfx_rrect(cx - r, cy - r, 2 * r, 2 * r, r, c, a); }

void gfx_image(const uint8_t *idx, const color_t *pal, int ncolors, int sw, int sh, int dx, int dy, int dw, int dh,
               int radius, int dim) {
  LIST(dy, dy + dh, L_IMAGE, (unsigned)idx, (unsigned)pal, (unsigned)ncolors, (unsigned)sw, (unsigned)sh,
       (unsigned)dx, (unsigned)dy, (unsigned)dw, (unsigned)dh, (unsigned)radius, (unsigned)dim, (unsigned)gfx_clip_x0,
       (unsigned)gfx_clip_x1);
  if (dw <= 0 || dh <= 0) return;
  int ya = NP_MAX(dy, gfx_y0), yb = NP_MIN(dy + dh, gfx_y1);
  if (ya >= yb || dx >= gx1 || dx + dw <= gx0) return;
  color_t dimmed[256];
  if (dim > 0) {
    int t = (dim + 4) >> 3;
    for (int i = 0; i < ncolors && i < 256; i++) dimmed[i] = ce_mix(pal[i], 0, (unsigned)t);
    pal = dimmed;
  }
  if (2 * radius > dw) radius = dw / 2;
  if (2 * radius > dh) radius = dh / 2;
  uint32_t step = ((uint32_t)sw << 16) / (uint32_t)dw;
  int x0 = NP_MAX(NP_MAX(dx, gfx_clip_x0), gx0), x1 = NP_MIN(NP_MIN(dx + dw, gfx_clip_x1), gx1);
  for (int row = ya; row < yb; row++) {
    int j = row - dy;
    const uint8_t *src = idx + (uint32_t)(j * sh / dh) * sw;
    int jj = corner_row(j, dh, radius);
    int in0 = 0, in1 = dw;
    color_t *p = px_at(0, row);
    if (jj >= 0) {
      for (int i = 0; i < radius; i++) {
        int cov = corner_cov(dist_at(radius - 1 - i, radius - 1 - jj), radius);
        if (cov >= 32) break;
        in0 = i + 1;
        in1 = dw - i - 1;
        if (cov <= 0) continue;
        int xl = dx + i, xr = dx + dw - 1 - i;
        if (xl >= x0 && xl < x1) p[xl] = ce_mix(p[xl], pal[src[(uint32_t)i * step >> 16]], (unsigned)cov);
        if (xr >= x0 && xr < x1) p[xr] = ce_mix(p[xr], pal[src[(uint32_t)(dw - 1 - i) * step >> 16]], (unsigned)cov);
      }
    }
    int a0 = NP_MAX(x0, dx + in0), a1 = NP_MIN(x1, dx + in1);
    uint32_t fx = (uint32_t)(a0 - dx) * step;
    for (int x = a0; x < a1; x++, fx += step) p[x] = pal[src[fx >> 16]];
  }
}

/* A mask (4-bit coverage) in one colour, drawn by ce_mask: its coverage as a mixing level (0..32) for the last
 * alpha, the colour, and the mixes it made, by level (a generation, the pixel, the mix), kept while the colour is
 * the same: letters lie on few colours */
__attribute__((used, aligned(16))) uint8_t gfx_ce_lvl[16];
__attribute__((used, aligned(256))) uint8_t gfx_ce_mixc[256];
__attribute__((used)) uint8_t gfx_ce_gen = 1;
__attribute__((used)) color_t gfx_ce_color;
static int lvl_a = -1;

/* rows of cols pixels from p (dest_skip bytes to the next row), the coverage from m's nibble odd on (skip nibbles
 * to the next row) */
__attribute__((naked)) static void ce_mask(color_t *p, const uint8_t *m, unsigned odd, unsigned cols, unsigned rows,
                                           unsigned skip, unsigned dest_skip) {
  __asm__(
    "ld iy, 0\n"
    "add iy, sp\n"
    "push ix\n"
    "ld ix, (iy + 3)\n"
    "ld de, (iy + 6)\n"
    "ld c, (iy + 9)\n"
    ".Lce_mask_row:\n"
    "ld b, (iy + 12)\n"
    ".Lce_mask_px:\n"
    "ld a, (de)\n"
    "bit 0, c\n"
    "jr z, .Lce_mask_lo\n"
    "rrca\n"
    "rrca\n"
    "rrca\n"
    "rrca\n"
    "inc de\n"
    ".Lce_mask_lo:\n"
    "and a, 15\n"
    "jp z, .Lce_mask_next\n"
    "ld hl, _gfx_ce_lvl\n"
    "or a, l\n"
    "ld l, a\n"
    "ld a, (hl)\n"
    "or a, a\n"
    "jp z, .Lce_mask_next\n"
    "cp a, 32\n"
    "jp nc, .Lce_mask_full\n"
    "push bc\n"
    "push de\n"
    "add a, a\n"
    "add a, a\n"
    "add a, a\n"
    "ld hl, _gfx_ce_mixc\n"
    "ld l, a\n"
    "ld a, (_gfx_ce_gen)\n"
    "cp a, (hl)\n"
    "jr nz, .Lce_mask_miss\n"
    "inc hl\n"
    "ld a, (ix + 0)\n"
    "cp a, (hl)\n"
    "jr nz, .Lce_mask_miss1\n"
    "inc hl\n"
    "ld a, (ix + 1)\n"
    "cp a, (hl)\n"
    "jr nz, .Lce_mask_miss2\n"
    "inc hl\n"
    "ld a, (hl)\n"
    "ld (ix + 0), a\n"
    "inc hl\n"
    "ld a, (hl)\n"
    "ld (ix + 1), a\n"
    "jr .Lce_mask_done\n"
    ".Lce_mask_miss2:\n"
    "dec hl\n"
    ".Lce_mask_miss1:\n"
    "dec hl\n"
    ".Lce_mask_miss:\n"
    "push hl\n"
    "push iy\n"
    "ld a, l\n"
    "rrca\n"
    "rrca\n"
    "rrca\n"
    "and a, 31\n"
    "ld e, a\n"
    "push de\n"
    "ld hl, (_gfx_ce_color)\n"
    "push hl\n"
    "ld hl, (ix + 0)\n"
    "push hl\n"
    "call _gfx_ce_mix\n"
    "pop de\n"
    "pop de\n"
    "pop de\n"
    "pop iy\n"
    "ex de, hl\n"
    "pop hl\n"
    "ld a, (_gfx_ce_gen)\n"
    "ld (hl), a\n"
    "inc hl\n"
    "ld a, (ix + 0)\n"
    "ld (hl), a\n"
    "inc hl\n"
    "ld a, (ix + 1)\n"
    "ld (hl), a\n"
    "inc hl\n"
    "ld (hl), e\n"
    "inc hl\n"
    "ld (hl), d\n"
    "ld (ix + 0), e\n"
    "ld (ix + 1), d\n"
    ".Lce_mask_done:\n"
    "pop de\n"
    "pop bc\n"
    "jp .Lce_mask_next\n"
    ".Lce_mask_full:\n"
    "ld hl, (_gfx_ce_color)\n"
    "ld (ix + 0), l\n"
    "ld (ix + 1), h\n"
    ".Lce_mask_next:\n"
    "ld a, c\n"
    "xor a, 1\n"
    "ld c, a\n"
    "lea ix, ix + 2\n"
    "dec b\n"
    "jp nz, .Lce_mask_px\n"
    "ld a, (iy + 18)\n"   /* the next row: skip nibbles on */
    "add a, c\n"
    "ld c, a\n"
    "srl a\n"
    "or a, a\n"
    "sbc hl, hl\n"
    "ld l, a\n"
    "add hl, de\n"
    "ex de, hl\n"
    "ld a, c\n"
    "and a, 1\n"
    "ld c, a\n"
    "push de\n"
    "ld de, (iy + 21)\n"
    "add ix, de\n"
    "pop de\n"
    "dec (iy + 15)\n"
    "jp nz, .Lce_mask_row\n"
    "pop ix\n"
    "ret\n");
}

void gfx_mask(const uint8_t *mask, int w, int h, int x, int y, color_t c, int a) {
  LIST(y, y + h, L_MASK, (unsigned)mask, (unsigned)w, (unsigned)h, (unsigned)x, (unsigned)y, c, (unsigned)a);
  int ya = NP_MAX(y, gfx_y0), yb = NP_MIN(y + h, gfx_y1);
  if (ya >= yb) return;
  int i0 = NP_MAX(x, gx0) - x, i1 = NP_MIN(x + w, gx1) - x;
  if (i1 <= i0) return;
  if (lvl_a != a) {
    lvl_a = a;
    for (int i = 0; i < 16; i++) gfx_ce_lvl[i] = (uint8_t)NP_MIN((i * a + 60) / 120, 32);
  }
  if (gfx_ce_color != c) {
    gfx_ce_color = c;
    if (!++gfx_ce_gen) memset(gfx_ce_mixc, 0, sizeof gfx_ce_mixc), gfx_ce_gen = 1;
  }
  unsigned n = (unsigned)((ya - y) * w + i0);
  int cols = i1 - i0;
  if (cols <= 255 && w - cols <= 254) {
    ce_mask(px_at(x + i0, ya), mask + (n >> 1), n & 1, (unsigned)cols, (unsigned)(yb - ya), (unsigned)(w - cols),
            (unsigned)(SCREEN_W - cols) * 2);
    return;
  }
  for (int row = ya; row < yb; row++, n += (unsigned)w) {
    color_t *p = px_at(x + i0, row);
    for (int i = 0; i < cols; i++, p++) {
      unsigned k = n + (unsigned)i, cov = mask[k >> 1] >> (k & 1 ? 4 : 0) & 15, l = gfx_ce_lvl[cov];
      if (l) *p = l >= 32 ? c : ce_mix(*p, c, l);
    }
  }
}

void gfx_corners(int x, int y, int w, int h, int r, color_t c) {
  LIST(y, y + h, L_CORNERS, (unsigned)x, (unsigned)y, (unsigned)w, (unsigned)h, (unsigned)r, c);
  if (x >= gx1 || x + w <= gx0) return;
  if (2 * r > w) r = w / 2;
  if (2 * r > h) r = h / 2;
  int ya = NP_MAX(y, gfx_y0), yb = NP_MIN(y + h, gfx_y1);
  for (int row = ya; row < yb; row++) {
    int jj = corner_row(row - y, h, r);
    if (jj < 0) continue;
    color_t *p = px_at(0, row);
    for (int i = 0; i < r; i++) {
      int cov = corner_cov(dist_at(r - 1 - i, r - 1 - jj), r);
      if (cov >= 32) break;
      int xl = x + i, xr = x + w - 1 - i;
      if (xl >= gx0 && xl < gx1) p[xl] = ce_mix(p[xl], c, (unsigned)(32 - cov));
      if (xr >= gx0 && xr < gx1) p[xr] = ce_mix(p[xr], c, (unsigned)(32 - cov));
    }
  }
}

/* a strip drawn from its list */
static void replay(const uint8_t *e, const uint8_t *end) {
  for (; e < end; e += *(const unsigned *)e >> 8) {
    const int *v = (const int *)e + 1;
    if ((e[0] <= L_OUTLINE || e[0] == L_CALL) && (v[0] >= gx1 || v[0] + v[2] <= gx0)) continue; /* (outside) */
    switch (e[0]) {
    case L_FILL: gfx_fill(v[0], v[1], v[2], v[3], (color_t)v[4]); break;
    case L_ALPHA: gfx_fill_alpha(v[0], v[1], v[2], v[3], (color_t)v[4], v[5]); break;
    case L_VGRAD: gfx_vgrad(v[0], v[1], v[2], v[3], (unsigned)v[4], (unsigned)v[5]); break;
    case L_RRECT: gfx_rrect(v[0], v[1], v[2], v[3], v[4], (color_t)v[5], v[6]); break;
    case L_OUTLINE: gfx_rrect_outline(v[0], v[1], v[2], v[3], v[4], v[5], (color_t)v[6], v[7]); break;
    case L_SHADOW: gfx_shadow(v[0], v[1], v[2], v[3], v[4], v[5], v[6]); break;
    case L_IMAGE: {
      int x0 = gfx_clip_x0, x1 = gfx_clip_x1;
      gfx_clip_x0 = v[11], gfx_clip_x1 = v[12];
      gfx_image((const uint8_t *)v[0], (const color_t *)v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8], v[9], v[10]);
      gfx_clip_x0 = x0, gfx_clip_x1 = x1;
      break;
    }
    case L_MASK: gfx_mask((const uint8_t *)v[0], v[1], v[2], v[3], v[4], (color_t)v[5], v[6]); break;
    case L_CORNERS: gfx_corners(v[0], v[1], v[2], v[3], v[4], (color_t)v[5]); break;
    case L_CALL: ((void (*)(const int *))v[4])(v + 6); break;
    case L_TEXT:
    case L_TEXTC:
    case L_TEXTR: {
      char t[256];
      memcpy(t, v + 6, (size_t)v[5]);
      t[v[5]] = 0;
      const np_font_t *f = (const np_font_t *)v[0];
      if (e[0] == L_TEXT) gfx_text(f, v[1], v[2], t, (color_t)v[3], v[4]);
      else if (e[0] == L_TEXTC) gfx_text_center(f, v[1], v[2], t, (color_t)v[3], v[4]);
      else gfx_text_right(f, v[1], v[2], t, (color_t)v[3], v[4]);
      break;
    }
    }
  }
}
