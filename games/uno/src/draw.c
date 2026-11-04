/* The screens. Cards are drawn, not pictures: the white border, the colour, the tilted oval and the big number
 * or symbol of the classic deck, sized for 320x240. */
#include <string.h>
#include "gfx.h"
#include "draw.h"

void plat_push(int x, int y, int w, int h, const uint16_t *px);   /* (plat.h: its key names are the launcher's too) */

void np_push(int x, int y, int w, int h, const color_t *px) { plat_push(x, y, w, h, px); }
static color_t strip[SCREEN_W * STRIP_H];

#define WHITE 0xFFFF
#define INK HEX(0x111827)
#define FOCUS HEX(0xFFD43B)
#define MUTED HEX(0xA8B3C7)
#define PANEL HEX(0x1B2436)
#define CARD_BLACK HEX(0x1C1C1E)
static const color_t CARD[5] = {HEX(0xE53935), HEX(0xF7C21B), HEX(0x2EA34F), HEX(0x1E7BD8), HEX(0x1C1C1E)};
static const char *const COLOR_NAME[4] = {"Red", "Yellow", "Green", "Blue"};
static const char *const NAMES[4] = {"You", "Bot 1", "Bot 2", "Bot 3"};

const char *uno_name(int p) { return p >= 0 && p < 4 ? NAMES[p] : ""; }

static char *put(char *p, const char *s) {
  while (*s) *p++ = *s++;
  *p = 0;
  return p;
}
static char *num(char *p, unsigned v) {
  char t[8];
  int n = 0;
  do t[n++] = (char)('0' + v % 10); while ((v /= 10) && n < 8);
  while (n) *p++ = t[--n];
  *p = 0;
  return p;
}

void uno_card_name(int c, char *out) {
  if (c == C_WILD) { put(out, "Wild"); return; }
  if (c == C_WILD4) { put(out, "Wild +4"); return; }
  char *p = put(out, COLOR_NAME[uno_color(c)]);
  *p++ = ' ';
  int v = uno_value(c);
  if (v < 10) num(p, (unsigned)v);
  else put(p, v == V_SKIP ? "Skip" : v == V_REVERSE ? "Reverse" : "+2");
}

/* ------------------------------------------------------------------ shapes, smoothed */
static inline void put32(int x, int y, color_t c, int t) {
  if (t <= 0 || x < 0 || x >= SCREEN_W || y < gfx_y0 || y >= gfx_y1) return;
#ifdef __TICE__
  if (gfx_listing) { /* (its strip is drawn every time) */
    gfx_list_rows(y, y + 1);
    return;
  }
#endif
  color_t *d = &gfx_buf[(y - gfx_y0) * SCREEN_W + x];
  *d = t >= 32 ? c : gfx_mix(*d, c, t);
}
static inline void plot(int x, int y, color_t c, float cov) { put32(x, y, c, (int)(cov * 32 + 0.5f)); }
static inline float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
static inline float len2(float x, float y) { return __builtin_sqrtf(x * x + y * y); }

/* rows of a box that are in the strip */
static bool rows(float y0, float y1, int *a, int *b) {
  *a = NP_MAX((int)y0 - 1, gfx_y0), *b = NP_MIN((int)y1 + 2, gfx_y1);
  return *a < *b;
}

/* an ellipse turned so that its long axis (a) points up and to the right, as on the cards; quarters: the Wild's
 * four colours. Each row: the inside filled at once, the edges smoothed. */
static void oval(float cx, float cy, float a, float b, color_t c, const color_t *quarters) {
  const float ax = 0.5f, ay = -0.866f, bx = 0.866f, by = 0.5f;
  float ey = __builtin_sqrtf(a * a * ay * ay + b * b * by * by);
  int r0, r1;
  if (!rows(cy - ey, cy + ey, &r0, &r1)) return;
#ifdef __TICE__
  if (gfx_listing) { /* (what it depends on, in the strips' lists) */
    const float v[] = {cx, cy, a, b};
    gfx_list(v, sizeof v);
    gfx_list(&c, sizeof c);
    if (quarters) gfx_list(quarters, 4 * sizeof *quarters);
    return;
  }
#endif
  /* u^2 + v^2 = 1 as A x^2 + B x y + C y^2 = 1, for the ellipse grown and shrunk by 0.75 pixel */
  float Ao[2], Bo[2], Co[2];
  for (int k = 0; k < 2; k++) {
    float aa = a + (k ? -0.75f : 0.75f), bb = b + (k ? -0.75f : 0.75f);
    aa = aa < 0.5f ? 0.5f : aa, bb = bb < 0.5f ? 0.5f : bb;
    Ao[k] = ax * ax / (aa * aa) + bx * bx / (bb * bb);
    Bo[k] = 2 * (ax * ay / (aa * aa) + bx * by / (bb * bb));
    Co[k] = ay * ay / (aa * aa) + by * by / (bb * bb);
  }
  for (int y = r0; y < r1; y++) {
    float dy = y + 0.5f - cy, sp[2][2];
    bool has[2];
    for (int k = 0; k < 2; k++) {
      float qb = Bo[k] * dy, disc = qb * qb - 4 * Ao[k] * (Co[k] * dy * dy - 1);
      has[k] = disc > 0;
      if (has[k]) {
        float r = __builtin_sqrtf(disc);
        sp[k][0] = cx + (-qb - r) / (2 * Ao[k]), sp[k][1] = cx + (-qb + r) / (2 * Ao[k]);
      }
    }
    if (!has[0]) continue;
    int xo0 = (int)sp[0][0] - 1, xo1 = (int)sp[0][1] + 2;
    int xi0 = has[1] ? (int)sp[1][0] + 1 : xo1, xi1 = has[1] ? (int)sp[1][1] : xo1;
    if (xi1 < xi0) xi0 = xi1 = xo1;
    for (int x = NP_MAX(xo0, 0); x < NP_MIN(xo1, SCREEN_W); x++) {
      bool inside = x >= xi0 && x < xi1;
      float dx = x + 0.5f - cx;
      float u = (dx * ax + dy * ay) / a, v = (dx * bx + dy * by) / b;
      color_t k = quarters ? quarters[(u < 0) * 2 + ((u < 0) != (v > 0))] : c;
      if (inside) {
        if (!quarters) {   /* the whole inside of the row in one go */
          color_t *d = &gfx_buf[(y - gfx_y0) * SCREEN_W + x];
          for (int n = NP_MIN(xi1, SCREEN_W) - x; n > 0; n--) *d++ = c;
          x = NP_MIN(xi1, SCREEN_W) - 1;
        } else {
          gfx_buf[(y - gfx_y0) * SCREEN_W + x] = k;
        }
        continue;
      }
      float r = __builtin_sqrtf(u * u + v * v), l = len2(dx, dy), d = r > 0.001f ? l - l / r : -a;
      plot(x, y, k, clamp01(0.5f - d));
    }
  }
}

/* a stroke from (x0, y0) to (x1, y1), w wide, round ends */
static void stroke(float x0, float y0, float x1, float y1, float w, color_t c) {
  float r = w * 0.5f;
  int r0, r1;
  if (!rows((y0 < y1 ? y0 : y1) - r, (y0 > y1 ? y0 : y1) + r, &r0, &r1)) return;
  int xa = (int)((x0 < x1 ? x0 : x1) - r) - 1, xb = (int)((x0 > x1 ? x0 : x1) + r) + 2;
  float dx = x1 - x0, dy = y1 - y0, ll = dx * dx + dy * dy;
  for (int y = r0; y < r1; y++)
    for (int x = xa; x < xb; x++) {
      float px = x + 0.5f - x0, py = y + 0.5f - y0;
      float t = ll > 0 ? clamp01((px * dx + py * dy) / ll) : 0;
      plot(x, y, c, clamp01(0.5f - (len2(px - t * dx, py - t * dy) - r)));
    }
}

static void ring(float cx, float cy, float r, float w, color_t c) {
  int r0, r1;
  if (!rows(cy - r - w, cy + r + w, &r0, &r1)) return;
  for (int y = r0; y < r1; y++)
    for (int x = (int)(cx - r - w) - 1; x < (int)(cx + r + w) + 2; x++) {
      float d = __builtin_fabsf(len2(x + 0.5f - cx, y + 0.5f - cy) - r) - w * 0.5f;
      plot(x, y, c, clamp01(0.5f - d));
    }
}

static void tri(float ax, float ay, float bx, float by, float qx, float qy, float grow, color_t c) {
  float lo = ay < by ? ay : by, hi = ay > by ? ay : by;
  lo = lo < qy ? lo : qy, hi = hi > qy ? hi : qy;
  float le = ax < bx ? ax : bx, ri = ax > bx ? ax : bx;
  le = le < qx ? le : qx, ri = ri > qx ? ri : qx;
  lo -= grow, hi += grow, le -= grow, ri += grow;
  float area = (bx - ax) * (qy - ay) - (by - ay) * (qx - ax), s = area < 0 ? -1.0f : 1.0f;
  float ex[3] = {ax, bx, qx}, ey[3] = {ay, by, qy};
  int r0, r1;
  if (!rows(lo, hi, &r0, &r1)) return;
  for (int y = r0; y < r1; y++)
    for (int x = (int)le - 1; x < (int)ri + 2; x++) {
      float px = x + 0.5f, py = y + 0.5f, d = -1e9f;
      for (int k = 0; k < 3; k++) {
        float x0 = ex[k], y0 = ey[k], x1 = ex[(k + 1) % 3], y1 = ey[(k + 1) % 3];
        float nx = (y1 - y0) * s, ny = -(x1 - x0) * s, nl = len2(nx, ny);
        float e = ((px - x0) * nx + (py - y0) * ny) / nl;   /* > 0 outside this edge */
        if (e > d) d = e;
      }
      plot(x, y, c, clamp01(0.5f - (d - grow)));
    }
}

/* text from the font's 4-bit letters, scaled (q8: 256 = its size) and leant (shear: pixels per pixel up), with a
 * dark edge e pixels wide (one at the font's own size); scaled letters are smoothed and sharpened back */
static inline int cov_at(const uint8_t *d, const np_glyph_t *g, int i, int j) {
  if ((unsigned)i >= g->w || (unsigned)j >= g->h) return 0;
  int n = j * g->w + i;
  return d[g->offset + (n >> 1)] >> ((n & 1) * 4) & 15;
}
static float sample(const uint8_t *d, const np_glyph_t *g, float sx, float sy) {
  int ix = (int)(sx + 8) - 8, iy = (int)(sy + 8) - 8;
  float fx = sx - ix, fy = sy - iy;
  return (cov_at(d, g, ix, iy) * (1 - fx) + cov_at(d, g, ix + 1, iy) * fx) * (1 - fy) +
         (cov_at(d, g, ix, iy + 1) * (1 - fx) + cov_at(d, g, ix + 1, iy + 1) * fx) * fy;
}
static int big_width(const np_font_t *f, const char *s, int q8) {
  int w = 0;
  for (; *s; s++) w += f->glyphs[(uint8_t)*s - f->first].advance;
  return w * q8 >> 8;
}
static void big_text(const np_font_t *f, int x, int base, const char *s, int q8, float shear, color_t c, color_t edge,
                     int e) {
  int top = base - (f->ascent * q8 >> 8) - 3, bot = base + (f->descent * q8 >> 8) + 3;
  if (bot <= gfx_y0 || top >= gfx_y1) return;
  const uint8_t *data = f->data;
  float k = 256.0f / q8, pen = (float)x, ek = e * k;
  for (; *s; s++) {
    const np_glyph_t *g = &f->glyphs[(uint8_t)*s - f->first];
    float gx = pen + g->x * q8 / 256.0f, gy = base + g->y * q8 / 256.0f, gw = g->w * q8 / 256.0f, gh = g->h * q8 / 256.0f;
    int y0 = NP_MAX((int)gy - 1 - e, gfx_y0), y1 = NP_MIN((int)(gy + gh) + 2 + e, gfx_y1);
    for (int y = y0; y < y1; y++) {
      float lean = (base - (y + 0.5f)) * shear;
      int xa = (int)(gx + lean) - 1 - e, xb = (int)(gx + lean + gw) + 2 + e;
      if (q8 == 256) {   /* its own size: the letter's pixels, no smoothing needed */
        float fo = gx + lean;
        int ox = (int)(fo + 8) - 8, oy = (int)(gy + 0.5f), j = y - oy, fx = (int)((fo - ox) * 16);   /* (1/16ths) */
        for (int xx = xa; xx < xb; xx++) {
          int i = xx - ox, v = (cov_at(data, g, i, j) * (16 - fx) + cov_at(data, g, i - 1, j) * fx + 8) >> 4;
          if (e) {
            int m = 0;
            for (int dj = -1; dj <= 1; dj++)
              for (int di = -1; di <= 1; di++) {
                int w = cov_at(data, g, i + di, j + dj);
                m = w > m ? w : m;
              }
            if (m > v) put32(xx, y, edge, (m * 32 + 7) / 15);
          }
          put32(xx, y, c, (v * 32 + 7) / 15);
        }
        continue;
      }
      float sy = (y + 0.5f - gy) * k - 0.5f;
      for (int xx = xa; xx < xb; xx++) {
        float sx = (xx + 0.5f - gx - lean) * k - 0.5f;
        float v = clamp01((sample(data, g, sx, sy) / 15.0f - 0.5f) * 2.2f + 0.5f);
        if (e) {
          float m = sample(data, g, sx - ek, sy);
          float t = sample(data, g, sx + ek, sy);
          m = t > m ? t : m, t = sample(data, g, sx, sy - ek), m = t > m ? t : m, t = sample(data, g, sx, sy + ek);
          m = clamp01(((t > m ? t : m) / 15.0f - 0.5f) * 2.2f + 0.5f);
          if (m > v) plot(xx, y, edge, m);
        }
        plot(xx, y, c, v);
      }
    }
    pen += g->advance * q8 / 256.0f;
  }
}
/* the same, centred */
static void big_label(const np_font_t *f, int cx, int base, const char *s, int q8, float shear, color_t c, color_t edge,
                      int e) {
  big_text(f, cx - big_width(f, s, q8) / 2, base, s, q8, shear, c, edge, e);
}

static void text(int x, int y, const char *s, color_t c) { gfx_text(&np_font_small, x, y, s, c, 256); }
static void ctext(const np_font_t *f, int x, int y, const char *s, color_t c) { gfx_text_center(f, x, y, s, c, 256); }

/* ------------------------------------------------------------------ cards */
/* the symbol of a card, centred at (cx, cy), s high (s: about the height of a digit) */
static void symbol(int card, float cx, float cy, float s, color_t c, color_t edge) {
  int v = uno_value(card);
  float w = s * (v == V_REVERSE ? 0.13f : 0.17f) + 1.0f;
  for (int pass = 0; pass < 2; pass++) {
    color_t k = pass ? c : edge;
    float ww = pass ? w : w + 2.0f;
    if (v == V_SKIP) {
      float r = s * 0.42f;
      ring(cx, cy, r, ww, k);
      stroke(cx - r * 0.68f, cy - r * 0.68f, cx + r * 0.68f, cy + r * 0.68f, ww, k);
    } else {   /* Reverse: two arrows side by side, one each way */
      const float dx = 0.7071f, dy = -0.7071f, nx = 0.7071f, ny = 0.7071f;
      bool tiny = s < 14;   /* (in a corner: bigger heads) */
      float L = s * (tiny ? 0.55f : 0.44f), hl = s * (tiny ? 0.5f : 0.28f), hw = s * (tiny ? 0.32f : 0.2f), o = s * 0.22f;
      float g = pass ? 0 : tiny ? 0.8f : 1.0f;
      if (tiny) ww = pass ? 1.4f : 2.8f;
      for (int i = 0; i < 2; i++) {
        float sg = i ? -1.0f : 1.0f, ox = cx - sg * nx * o, oy = cy - sg * ny * o;
        float bx = ox + sg * dx * (L - hl), by = oy + sg * dy * (L - hl);
        stroke(ox - sg * dx * L, oy - sg * dy * L, bx, by, ww, k);
        tri(ox + sg * dx * L, oy + sg * dy * L, bx + nx * hw, by + ny * hw, bx - nx * hw, by - ny * hw, g, k);
      }
    }
  }
}

static void value_label(int card, char *b) {
  int v = uno_value(card);
  if (card == C_WILD4) put(b, "+4");
  else if (card == C_WILD) b[0] = 0;
  else if (v == V_DRAW2) put(b, "+2");
  else if (v < 10) num(b, (unsigned)v);
  else b[0] = 0;
}

/* the corner index, top left (and bottom right on big cards) */
static void corner(int card, int x, int y, int w, int h) {
  char b[4];
  value_label(card, b);
  int v = uno_value(card);
  bool small = w < 44;
  const np_font_t *f = small ? &np_font_small : &np_font_body;
  int ix = x + (small ? 3 : 5), iy = y + (small ? 11 : 15);
  for (int k = 0; k < (small ? 1 : 2); k++) {
    int px = k ? x + w - (small ? 3 : 5) : ix, py = k ? y + h - (small ? 5 : 6) : iy;
    if (card != C_WILD && (v < 10 || v == V_DRAW2 || card == C_WILD4)) {
      int tw = gfx_text_width(f, b);
      int tx = k ? px - tw : px;
      gfx_text(f, tx + 1, py + 1, b, CARD_BLACK, 200);
      gfx_text(f, tx, py, b, WHITE, 256);
    } else if (card != C_WILD) {
      float s = small ? 10.0f : 12.0f, cx = k ? px - s * 0.5f : px + s * 0.5f, cy = py - s * 0.4f;
      symbol(card, cx, cy, s, WHITE, CARD_BLACK);
    }
  }
}

static void card_face(int card, int x, int y, int w, int h) {
  if (y >= gfx_y1 || y + h <= gfx_y0) return;
  int r = w / 6 + 1, bd = w >= 44 ? 3 : 2;
  gfx_rrect(x - 1, y - 1, w + 2, h + 2, r + 1, HEX(0x374151), 150);
  gfx_rrect(x, y, w, h, r, WHITE, 256);
  int col = uno_color(card);
  gfx_rrect(x + bd, y + bd, w - 2 * bd, h - 2 * bd, r - 1, CARD[col], 256);
  float cx = x + w * 0.5f, cy = y + h * 0.5f, a = (h - 2 * bd) * 0.47f, b = (w - 2 * bd) * 0.37f;
  const np_font_t *big = &np_font_title;
  int q8 = w >= 44 ? 360 : 256;   /* the number's size */
  if (card == C_WILD) {
    static const color_t quarters[4] = {HEX(0xE53935), HEX(0x1E7BD8), HEX(0xF7C21B), HEX(0x2EA34F)};
    oval(cx, cy, a, b, WHITE, 0);
    oval(cx, cy, a - 1.5f, b - 1.5f, 0, quarters);
  } else if (card == C_WILD4) {
    /* four little cards, one of each colour */
    static const uint8_t order[4] = {BLUE, GREEN, RED, YELLOW};
    int mw = w * 26 / 100 + 1, mh = h * 30 / 100 + 1;
    for (int i = 0; i < 4; i++) {
      int mx = (int)(cx - mw * 0.5f + (i - 1.5f) * w * 0.12f), my = (int)(cy - mh * 0.5f + (1.5f - i) * h * 0.06f);
      gfx_rrect(mx, my, mw, mh, 2, WHITE, 256);
      gfx_rrect(mx + 1, my + 1, mw - 2, mh - 2, 2, CARD[order[i]], 256);
    }
  } else {
    oval(cx, cy, a, b, WHITE, 0);
    int v = uno_value(card);
    if (v == V_SKIP || v == V_REVERSE) {
      symbol(card, cx, cy, h * 0.4f, CARD[col], CARD_BLACK);
    } else {
      char t[4];
      value_label(card, t);
      int base = (int)(cy + (big->ascent * q8 >> 8) * 0.5f);
      big_label(big, (int)cx, base, t, q8, 0.18f, CARD[col], CARD_BLACK, 1);
      if (v == 6 || v == 9) gfx_fill((int)cx - 3, base + 2, 7, 2, CARD[col]);   /* 6 and 9 underlined */
    }
  }
  corner(card, x, y, w, h);
}

static void card_back(int x, int y, int w, int h) {
  if (y >= gfx_y1 || y + h <= gfx_y0) return;
  int r = w / 6 + 1, bd = w >= 30 ? 3 : w >= 16 ? 2 : 1;
  if (w >= 16) gfx_rrect(x - 1, y - 1, w + 2, h + 2, r + 1, HEX(0x374151), 150);
  gfx_rrect(x, y, w, h, r, WHITE, 256);
  gfx_rrect(x + bd, y + bd, w - 2 * bd, h - 2 * bd, r - 1, CARD_BLACK, 256);
  float cx = x + w * 0.5f, cy = y + h * 0.5f;
  oval(cx, cy, (h - 2 * bd) * 0.45f, (w - 2 * bd) * 0.36f, CARD[RED], 0);
  if (w >= 30) {
    int q8 = w * 256 / 66;
    big_label(&np_font_title, (int)cx, (int)(cy + (np_font_title.ascent * q8 >> 8) * 0.5f), "UNO", q8, 0.2f,
              CARD[YELLOW], CARD_BLACK, 1);
  }
}

static void dim_card(int x, int y, int w, int h) { gfx_rrect(x, y, w, h, w / 6 + 1, HEX(0x0B1020), 150); }

/* ------------------------------------------------------------------ pieces of the screens */
static void background(void) { gfx_vgrad(0, 0, SCREEN_W, SCREEN_H, 0x23304A, 0x0F1626); }
static void table_bg(void) { gfx_vgrad(0, 0, SCREEN_W, SCREEN_H, 0x1F6B4A, 0x0B3324); }

static void panel(int x, int y, int w, int h) {
  gfx_shadow(x, y, w, h, 10, 6, 160);
  gfx_rrect(x, y, w, h, 10, PANEL, 250);
  gfx_rrect_outline(x, y, w, h, 10, 1, HEX(0x3B4A66), 256);
}

static void button(int x, int y, int w, int h, const char *s, bool focus) {
  gfx_rrect(x, y, w, h, h / 2, focus ? FOCUS : HEX(0x2A3650), 256);
  if (focus) gfx_rrect_outline(x - 2, y - 2, w + 4, h + 4, h / 2 + 2, 2, WHITE, 256);
  ctext(&np_font_body, x + w / 2, y + h / 2 + 5, s, focus ? INK : WHITE);
}

/* the key line: "key|what|key|what...", keys in yellow */
static int keyline_draw(const char *s, bool draw) {
  char b[24];
  int x = 6, i = 0;
  while (*s) {
    int n = 0;
    while (*s && *s != '|' && n < 23) b[n++] = *s++;
    b[n] = 0;
    if (*s == '|') s++;
    if (draw) gfx_text(&np_font_small, x, 236, b, i % 2 ? WHITE : FOCUS, 256);
    x += gfx_text_width(&np_font_small, b) + (i % 2 ? 9 : 4);
    i++;
  }
  return x - 9;
}
int uno_keyline_width(const char *s) { return keyline_draw(s, false); }
int uno_text_width(const char *s) { return gfx_text_width(&np_font_small, s); }
static void keyline(const char *s) {
  gfx_fill(0, 226, SCREEN_W, 14, HEX(0x0A0F1A));
  keyline_draw(s, true);
}

static void logo(int cx, int cy, int q8) {
  oval((float)cx, (float)cy, 30.0f * q8 / 256, 52.0f * q8 / 256, CARD_BLACK, 0);
  oval((float)cx, (float)cy, 27.0f * q8 / 256, 49.0f * q8 / 256, CARD[RED], 0);
  int tq = q8 * 2;
  big_label(&np_font_title, cx, cy + (np_font_title.ascent * tq >> 8) / 2, "UNO", tq, 0.22f, CARD[YELLOW], CARD_BLACK, 2);
}

/* ------------------------------------------------------------------ the table */
typedef struct { int x, y; } seat_t;
static seat_t seat(int n, int p) {
  if (n == 2) return (seat_t){123, 4};
  if (n == 3) return p == 1 ? (seat_t){4, 50} : (seat_t){242, 50};
  return p == 1 ? (seat_t){4, 56} : p == 2 ? (seat_t){123, 4} : (seat_t){242, 56};
}

static void opponent(const uno_game *g, int p, bool live) {
  seat_t s = seat(g->r.players, p);
  int w = 74, h = 62, x = s.x, y = s.y;
  if (y >= gfx_y1 || y + h <= gfx_y0) return;
  bool turn = live && g->turn == p && g->phase < P_HAND_OVER;
  gfx_rrect(x, y, w, h, 9, HEX(0x0B1A14), 150);
  if (turn) gfx_rrect_outline(x - 2, y - 2, w + 4, h + 4, 11, 3, FOCUS, 256);
  gfx_text(&np_font_body, x + 6, y + 15, uno_name(p), turn ? FOCUS : WHITE, 256);
  char b[8];
  if (!g->r.one_hand) {
    num(b, g->score[p]);
    gfx_text_right(&np_font_small, x + w - 6, y + 14, b, MUTED, 256);
  }
  int n = g->count[p], fan = NP_MIN(n, 6);
  for (int i = 0; i < fan; i++) card_back(x + 6 + i * 5, y + 21, 13, 19);
  num(b, (unsigned)n);
  big_label(&np_font_title, x + w - 16, y + 41, b, 256, 0, WHITE, 0, 0);
  if (live && g->uno_open == p) {
    gfx_rrect(x + 6, y + 44, w - 12, 14, 7, FOCUS, 256);
    ctext(&np_font_small, x + w / 2, y + 55, "Forgot UNO!", INK);
  } else if (n == 1) {
    gfx_rrect(x + 6, y + 44, w - 12, 14, 7, CARD[RED], 256);
    ctext(&np_font_small, x + w / 2, y + 55, "UNO!", WHITE);
  }
}

/* two arrows round in a circle: the way play goes */
static void direction(int cx, int cy, int dir) {
  float r = 11;
  for (int pass = 0; pass < 2; pass++) {
    color_t c = pass ? HEX(0xE8F0EA) : HEX(0x0B3324);
    float w = pass ? 2.5f : 4.5f, g = pass ? 0 : 1.2f;
    for (int i = 0; i < 2; i++) {
      float a0 = i * 3.14159f + 0.5f, a1 = a0 + 1.75f, px = 0, py = 0;
      for (int k = 0; k <= 8; k++) {
        float a = a0 + (a1 - a0) * k / 8, qx = cx + r * __builtin_cosf(a), qy = cy + r * __builtin_sinf(a) * (float)dir;
        if (k) stroke(px, py, qx, qy, w, c);
        px = qx, py = qy;
      }
      /* the head, along the circle at its end */
      float tx = -__builtin_sinf(a1), ty = __builtin_cosf(a1) * (float)dir, nx = __builtin_cosf(a1), ny = __builtin_sinf(a1) * (float)dir;
      tri(px + tx * 6, py + ty * 6, px + nx * 4.5f, py + ny * 4.5f, px - nx * 4.5f, py - ny * 4.5f, g, c);
    }
  }
}

static int hand_layout(int n, int pick, int *first, int *shown) {
  int maxn = 28;
  *shown = NP_MIN(n, maxn);
  *first = NP_CLAMP(pick - *shown / 2, 0, n - *shown);
  return *shown > 1 ? NP_MIN(40, (310 - 36) / (*shown - 1)) : 0;
}

static void your_hand(const uno_game *g, const uno_ui *v) {
  int n = g->count[0];
  if (!n || gfx_y1 <= 156) return;
  int first, shown, sp = hand_layout(n, v->pick, &first, &shown);
  int w = 36, h = 54, total = w + (shown - 1) * sp, x0 = (SCREEN_W - total) / 2;
  bool mine = g->turn == 0 && g->phase < P_HAND_OVER && g->phase != P_COLOR;
  bool panel_up = g->phase == P_PENALTY && g->turn == 0 && g->challengeable;
  for (int k = 0; k < shown; k++) {
    int i = first + k, x = x0 + k * sp, y = i == v->pick ? 160 : 168;
    if (i == v->pick) continue;
    card_face(g->hand[0][i], x, y, w, h);
    if (mine && !panel_up && !uno_can_play(g, i)) dim_card(x, y, w, h);
  }
  if (v->pick >= first && v->pick < first + shown) {
    int x = x0 + (v->pick - first) * sp, y = 160;
    card_face(g->hand[0][v->pick], x, y, w, h);
    if (mine && !panel_up && !uno_can_play(g, v->pick)) dim_card(x, y, w, h);
    gfx_rrect_outline(x - 3, y - 3, w + 6, h + 6, 9, 3, mine ? FOCUS : WHITE, 256);
  }
  if (first) text(1, 200, "<", FOCUS);
  if (first + shown < n) text(313, 200, ">", FOCUS);
}

static void uno_button(const uno_game *g, const uno_ui *v) {
  (void)v;
  int x = 262, y = 137, w = 54, h = 18;
  if (y >= gfx_y1 || y + h <= gfx_y0) return;
  bool catchable = g->uno_open != UNO_NONE && g->uno_open != 0;
  bool mine = g->uno_open == 0 || (g->turn == 0 && g->count[0] == 2 && g->uno_said != 0 &&
                                   (g->phase == P_PLAY || g->phase == P_DRAWN || g->phase == P_PENALTY));
  if (g->phase >= P_HAND_OVER) catchable = mine = false;
  if (catchable) {
    gfx_rrect(x, y, w, h, 9, FOCUS, 256);
    ctext(&np_font_body, x + w / 2, y + 14, "Catch!", INK);
  } else if (mine) {
    gfx_rrect(x, y, w, h, 9, CARD[RED], 256);
    gfx_rrect_outline(x - 2, y - 2, w + 4, h + 4, 11, 2, FOCUS, 256);
    ctext(&np_font_body, x + w / 2, y + 14, "UNO!", WHITE);
  } else {
    gfx_rrect_outline(x, y, w, h, 9, 1, HEX(0x5C8A73), 256);
    ctext(&np_font_body, x + w / 2, y + 14, g->uno_said == 0 ? "Said!" : "UNO", HEX(0x7FA893));
  }
}

static void info(const uno_game *g) {
  if (gfx_y0 >= 44) return;
  char b[24], *p;
  if (g->r.one_hand) {
    text(6, 14, "One hand", MUTED);
  } else {
    p = put(b, "Hand ");
    num(p, g->hands);
    text(6, 14, b, MUTED);
    text(6 + gfx_text_width(&np_font_small, b) + 6, 14, "to 500", HEX(0x7FA893));
    p = put(b, "You  ");
    num(p, g->score[0]);
    gfx_text(&np_font_body, 6, 32, b, WHITE, 256);
  }
}

static void piles(const uno_game *g) {
  if (gfx_y0 >= 142 || gfx_y1 <= 46) return;
  card_back(96, 58, 40, 58);
  char b[8];
  num(b, g->dn);
  ctext(&np_font_small, 116, 128, b, MUTED);
  int top = g->pile[g->pn - 1], col = g->color;
  if (col < 4) gfx_rrect_outline(143, 47, 60, 84, 12, 4, CARD[col], 256);
  card_face(top, 147, 50, 52, 78);
  if (col < 4) ctext(&np_font_small, 173, 140, COLOR_NAME[col], col == RED ? HEX(0xFF8A80) : CARD[col]);
  if (g->r.players > 2) direction(222, 88, g->dir);
}

static void color_picker(const uno_ui *v) {
  panel(40, 54, 240, 112);
  ctext(&np_font_body, 160, 76, "Call a colour", WHITE);
  for (int i = 0; i < 4; i++) {
    int x = 56 + i * 54, y = 88, s = 44;
    bool f = v->sel == i;
    if (f) y -= 3, gfx_rrect_outline(x - 4, y - 4, s + 8, s + 8, 12, 3, WHITE, 256);
    gfx_rrect(x, y, s, s, 9, CARD[i], 256);
    ctext(&np_font_small, x + s / 2, 152, COLOR_NAME[i], f ? WHITE : MUTED);
  }
}

int uno_pen_count(const uno_game *g) { return 2 + uno_can_stack(g); }

static void penalty_panel(const uno_game *g, const uno_ui *v) {
  panel(36, 40, 248, 136);
  char b[48], *p = put(b, uno_name(g->offender));
  put(p, " played a Wild Draw Four");
  ctext(&np_font_body, 160, 60, b, WHITE);
  p = put(b, "The colour was ");
  put(p, COLOR_NAME[g->prev_color]);
  ctext(&np_font_small, 160, 78, b, MUTED);
  int n = uno_pen_count(g);
  for (int i = 0; i < n; i++) {
    int item = i;
    const char *s = item == PEN_TAKE ? "Take 4 cards" : item == PEN_CHALLENGE ? "Challenge: a bluff?" : "Play your Wild Draw Four";
    if (item == PEN_TAKE) {
      p = put(b, "Take ");
      p = num(p, g->pending);
      put(p, " cards");
      s = b;
    }
    button(56, 88 + i * 28, 208, 22, s, v->sel == i);
  }
}

static void end_panel(const uno_game *g, const uno_ui *v) {
  bool over = g->phase == P_GAME_OVER;
  int n = g->r.players, h = 96 + n * 18;
  int y = (226 - h) / 2;
  panel(28, y, 264, h);
  char b[48], *p;
  p = put(b, uno_name(g->winner));
  put(p, g->winner ? (over ? " wins the game" : " wins the hand") : (over ? " win the game!" : " win the hand!"));
  ctext(&np_font_body, 160, y + 22, b, g->winner == 0 ? FOCUS : WHITE);
  p = put(b, "+");
  p = num(p, g->gained);
  put(p, " points");
  ctext(&np_font_small, 160, y + 40, b, MUTED);
  for (int q = 0; q < n; q++) {
    int ry = y + 50 + q * 18;
    bool w = q == g->winner;
    gfx_text(&np_font_body, 48, ry + 13, uno_name(q), w ? FOCUS : WHITE, 256);
    num(b, g->score[q]);
    gfx_text_right(&np_font_body, 140, ry + 13, b, w ? FOCUS : WHITE, 256);
    if (!g->r.one_hand) {
      int bw = 120, fill = NP_MIN(bw, g->score[q] * bw / UNO_GOAL);
      gfx_rrect(152, ry + 5, bw, 8, 4, HEX(0x2A3650), 256);
      if (fill > 0) gfx_rrect(152, ry + 5, NP_MAX(fill, 8), 8, 4, w ? FOCUS : HEX(0x7FA8D8), 256);
    }
  }
  int by = y + 56 + n * 18;
  if (over) {
    button(48, by, 104, 24, "Play again", v->sel == 0);
    button(168, by, 104, 24, "Menu", v->sel == 1);
  } else {
    button(90, by, 140, 24, "Next hand", true);
  }
}

static void reveal_panel(const uno_ui *v) {
  panel(16, 36, 288, 160);
  char b[56], *p = put(b, uno_name(v->reveal_who));
  put(p, "'s hand");
  ctext(&np_font_body, 160, 58, b, WHITE);
  int n = v->reveal_n, sp = n > 1 ? NP_MIN(32, 228 / (n - 1)) : 0, w = 30, h = 45, x0 = 160 - (w + (n - 1) * sp) / 2;
  for (int i = 0; i < n; i++) {
    int c = v->reveal[i], x = x0 + i * sp;
    card_face(c, x, 70, w + 2, h);
    if (uno_color(c) != v->reveal_color) dim_card(x, 70, w + 2, h);
  }
  p = v->reveal_guilty ? put(b, "It had ") : put(b, "No ");
  p = put(p, COLOR_NAME[v->reveal_color]);
  put(p, v->reveal_guilty ? ": a bluff" : ": no bluff");
  ctext(&np_font_body, 160, 140, b, v->reveal_guilty ? FOCUS : WHITE);
  p = v->reveal_guilty ? put(b, uno_name(v->reveal_who)) : put(b, "You");
  put(p, v->reveal_guilty ? " takes 4 cards, and you play" : " take 6 cards");
  ctext(&np_font_small, 160, 160, b, MUTED);
}

static void table(const uno_game *g, const uno_ui *v) {
  table_bg();
  bool live = v->screen == V_PLAY || v->screen == V_REVEAL;
  info(g);
  for (int p = 1; p < g->r.players; p++) opponent(g, p, live);
  piles(g);
  if (gfx_y1 > 140 && gfx_y0 < 158) {
    ctext(&np_font_small, 130, 152, v->msg, WHITE);
    uno_button(g, v);
  }
  if (g->turn == 0 && g->phase < P_HAND_OVER && gfx_y1 > 156) {   /* your turn: your side lights up */
    gfx_fill_alpha(0, 157, SCREEN_W, 69, FOCUS, 30);
    gfx_fill(0, 156, SCREEN_W, 2, FOCUS);
  }
  your_hand(g, v);
  if (v->screen != V_PLAY) return;
  if (g->phase == P_COLOR && g->turn == 0) color_picker(v);
  else if (g->phase == P_PENALTY && g->turn == 0 && g->challengeable) penalty_panel(g, v);
  else if (g->phase >= P_HAND_OVER && v->show_end) end_panel(g, v);
}

/* ------------------------------------------------------------------ the other screens */
int uno_menu_count(const uno_ui *v) { return v->has_game ? 3 : 2; }
int uno_menu_item(const uno_ui *v, int n) { return v->has_game ? n : n + 1; }

static void title_screen(const uno_ui *v) {
  background();
  /* a fan of cards behind the name */
  static const uint8_t fan[5] = {7, 13 + V_SKIP, 26 + V_REVERSE, 39 + V_DRAW2, C_WILD4};
  for (int i = 0; i < 5; i++) {
    int x = 180 + i * 22, y = 26 + (i == 2 ? 0 : i == 1 || i == 3 ? 4 : 10);
    card_face(fan[i], x, y, 44, 66);
  }
  logo(84, 62, 250);
  int n = uno_menu_count(v);
  static const char *const items[3] = {"Continue", "New game", "How to play"};
  for (int i = 0; i < n; i++) button(60, 124 + i * 32, 200, 24, items[uno_menu_item(v, i)], v->sel == i);
}

static const char *const LEVELS[3] = {"Easy", "Normal", "Hard"};
static const char *const HOUSE[5] = {"Stack Draw Twos", "Stack Wild Draw Fours", "Draw until you can play",
                                     "Must play a drawn card", "UNO said for everyone"};

static void setup_screen(const uno_ui *v) {
  background();
  ctext(&np_font_body, 160, 20, "New game", WHITE);
  const uno_rules *r = &v->setup;
  char b[32], *p;
  for (int i = 0; i < 3; i++) {
    int y = 28 + i * 21;
    bool f = v->sel == i;
    gfx_rrect(10, y, 300, 19, 8, f ? FOCUS : HEX(0x1E2940), 256);
    static const char *const label[3] = {"Players", "Bots", "Game"};
    gfx_text(&np_font_body, 20, y + 14, label[i], f ? INK : WHITE, 256);
    if (i == 0) {
      p = put(b, "You + ");
      p = num(p, (unsigned)(r->players - 1));
      put(p, r->players == 2 ? " bot" : " bots");
    } else if (i == 1) {
      put(b, LEVELS[r->level]);
    } else {
      put(b, r->one_hand ? "One hand" : "To 500 points");
    }
    gfx_text_right(&np_font_body, f ? 286 : 298, y + 14, b, f ? INK : WHITE, 256);
    if (f) text(292, y + 13, ">", INK), text(286 - gfx_text_width(&np_font_body, b) - 10, y + 13, "<", INK);
  }
  text(14, 103, "House rules", MUTED);
  for (int k = 0; k < 5; k++) {
    int y = 107 + k * 18, row = ROW_HOUSE + k;
    bool f = v->sel == row, on = r->house >> k & 1;
    gfx_rrect(10, y, 300, 16, 7, f ? FOCUS : HEX(0x1E2940), 256);
    gfx_text(&np_font_small, 20, y + 12, HOUSE[k], f ? INK : WHITE, 256);
    gfx_rrect(262, y + 2, 40, 12, 6, on ? HEX(0x2EA34F) : f ? HEX(0xC9A92C) : HEX(0x37425A), 256);
    ctext(&np_font_small, 282, y + 12, on ? "On" : "Off", WHITE);
  }
  button(90, 199, 140, 22, "Deal", v->sel == ROW_DEAL);
}

static const char *const HELP_TITLE[UNO_HELP_PAGES] = {"Your turn", "Action cards", "Wild Draw Four", "UNO!", "Scoring"};
static const char *const HELP[UNO_HELP_PAGES] = {
    "Match the top card by colour, number or symbol, or play a Wild. Or draw a card, and play it if it goes. "
    "Play all your cards to win the hand.",
    "Skip: the next player misses a turn. Reverse: play changes direction (a Skip with two players). Draw Two: "
    "the next player draws 2 and misses a turn.",
    "Call a colour; the next player draws 4 and misses a turn. Only allowed with no card of the colour in play. "
    "Challenge it: a bluffer draws 4, else you draw 6.",
    "Press shift as you play your next-to-last card. Caught without it before the next player starts: draw 2. "
    "Shift also catches a bot that forgot.",
    "The winner scores the cards left in the others' hands: numbers as shown, Skip, Reverse, Draw Two 20, Wilds "
    "50. First to 500 points wins."};

/* where page `page`'s text ends (its last baseline + a line): for the tests */
int uno_help_bottom(int page) {
  int y0 = gfx_y0, y1 = gfx_y1;
  gfx_y0 = gfx_y1 = -100;
  int end = gfx_paragraph(&np_font_body, 160, 86, 268, 19, HELP[page], WHITE, 256);
  gfx_y0 = y0, gfx_y1 = y1;
  return end;
}

static void help_screen(const uno_ui *v) {
  background();
  panel(12, 10, 296, 208);
  char b[24], *p = put(b, "How to play  ");
  p = num(p, (unsigned)v->page + 1);
  p = put(p, "/");
  num(p, UNO_HELP_PAGES);
  ctext(&np_font_small, 160, 28, b, MUTED);
  ctext(&np_font_title, 160, 56, HELP_TITLE[v->page], FOCUS);
  gfx_paragraph(&np_font_body, 160, 86, 268, 19, HELP[v->page], WHITE, 256);
  /* a card to look at on each page */
  static const uint8_t show[UNO_HELP_PAGES][3] = {{3, 16, C_WILD},
                                                  {V_SKIP, 13 + V_REVERSE, 26 + V_DRAW2},
                                                  {C_WILD4, C_WILD4, C_WILD4},
                                                  {39 + 5, 39 + 5, 39 + 5},
                                                  {9, 26 + V_SKIP, C_WILD}};
  int n = show[v->page][0] == show[v->page][1] ? 1 : 3;
  for (int i = 0; i < n; i++) card_face(show[v->page][i], 160 - (n * 40 - 4) / 2 + i * 40, 158, 36, 54);
}

static void scene(void *ctx) {
  const void *const *pair = ctx;
  const uno_game *g = pair[0];
  const uno_ui *v = pair[1];
  switch (v->screen) {
    case V_TITLE: title_screen(v); break;
    case V_SETUP: setup_screen(v); break;
    case V_HELP: help_screen(v); break;
    default: table(g, v); break;
  }
  if (v->screen == V_REVEAL) reveal_panel(v);
  if (v->screen == V_PAUSE) {
    panel(60, 40, 200, 160);
    ctext(&np_font_body, 160, 62, "Paused", WHITE);
    static const char *const items[PAUSE_COUNT] = {"Resume", "New game", "How to play", "Quit game"};
    for (int i = 0; i < PAUSE_COUNT; i++) button(84, 76 + i * 30, 152, 24, items[i], v->sel == i);
  } else if (v->screen == V_QUIT) {
    panel(50, 82, 220, 72);
    ctext(&np_font_body, 160, 104, "Quit the game?", WHITE);
    button(64, 118, 92, 24, "Keep playing", v->sel == 0);
    button(164, 118, 92, 24, "Quit", v->sel == 1);
  }
  if (gfx_y1 > 226) keyline(uno_keys(g, v));
}

const char *uno_keys(const uno_game *g, const uno_ui *v) {
  switch (v->screen) {
    case V_TITLE: return "Up Down|choose|OK|open|Back|quit";
    case V_SETUP: return v->sel < ROW_HOUSE ? "Up Down|choose|Left Right|change|OK|deal|Back|menu" :
                         v->sel < ROW_DEAL ? "Up Down|choose|OK|on or off|Back|menu" : "Up Down|choose|OK|deal|Back|menu";
    case V_HELP: return v->first_run ? "Left Right|pages|OK|next|Back|skip" : "Left Right|pages|Back|close";
    case V_REVEAL: return "OK|go on";
    case V_PAUSE: return "Up Down|choose|OK|select|Back|resume";
    case V_QUIT: return "Left Right|choose|OK|select";
    default: break;
  }
  if (g->phase >= P_HAND_OVER) return !v->show_end ? "Back|menu" : g->phase == P_GAME_OVER ? "Left Right|choose|OK|select" : "OK|next hand|Back|menu";
  bool catchable = g->uno_open != UNO_NONE && g->uno_open != 0;
  if (g->turn != 0)
    return catchable ? "Left Right|look|shift|catch!|Back|menu" : g->uno_open == 0 ? "Left Right|look|shift|UNO!|Back|menu" :
                       "Left Right|look at your cards|Back|menu";
  switch (g->phase) {
    case P_COLOR: return "Left Right|colour|OK|call it";
    case P_DRAWN:
      return g->r.house & H_MUST_PLAY ? "OK|play the new card|shift|UNO|Back|menu" : "OK|play it|Up|keep it|shift|UNO|Back|menu";
    case P_PENALTY:
      if (g->challengeable) return "Up Down|choose|OK|select";
      return "Left Right|pick|OK|stack|Up|take|Back|menu";
    default:
      if (catchable) return "Left Right|pick|OK|play|Up|draw|shift|catch!";
      if (g->count[0] == 2) return "Left Right|pick|OK|play|Up|draw|shift|UNO!";
      return "Left Right|pick|OK|play|Up|draw|Back|menu";
  }
}

void uno_render(const uno_game *g, const uno_ui *v) {
  const void *pair[2] = {g, v};
  color_t *old = gfx_buf;
  gfx_buf = strip;
  gfx_render(scene, (void *)pair, 0, SCREEN_H);
  gfx_buf = old;
}

#ifdef HOST
/* every kind of card, at the hand's size and the discard pile's: for looking at them */
static void cards_scene(void *ctx) {
  (void)ctx;
  table_bg();
  static const uint8_t kinds[15] = {0, 14, 28, 42, 4, 18, 32, 46, V_SKIP, 13 + V_REVERSE, 26 + V_DRAW2, C_WILD, C_WILD4, 39 + 6, 9};
  for (int i = 0; i < 8; i++) card_face(kinds[i], 4 + i * 39, 4, 36, 54);
  for (int i = 8; i < 15; i++) card_face(kinds[i], 4 + (i - 8) * 39, 62, 36, 54);
  for (int i = 0; i < 6; i++) card_face(kinds[8 + i], 4 + i * 53, 122, 52, 78);
  card_back(4 + 6 * 53, 160, 13, 19);
}
/* the app's icon, 55 x 56, over a background colour (twice: black and white make its transparency) */
static color_t icon_bg;
static void icon_scene(void *ctx) {
  (void)ctx;
  gfx_fill(0, gfx_y0, SCREEN_W, gfx_y1 - gfx_y0, icon_bg);
  gfx_rrect(0, 0, 55, 56, 12, HEX(0x1F6B4A), 256);
  card_face(13 + 1, 3, 11, 25, 37);
  card_face(7, 13, 6, 25, 37);
  card_back(23, 9, 30, 44);
}
void uno_render_icon(color_t bg) {
  color_t *old = gfx_buf;
  gfx_buf = strip, icon_bg = bg;
  gfx_render(icon_scene, 0, 0, 64);
  gfx_buf = old;
}
void uno_render_cards(void) {
  color_t *old = gfx_buf;
  gfx_buf = strip;
  gfx_render(cards_scene, 0, 0, SCREEN_H);
  gfx_buf = old;
}
#endif
