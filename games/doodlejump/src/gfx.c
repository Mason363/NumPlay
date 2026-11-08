/* Doodle Jump: drawing (see gfx.h). */
#include "gfx.h"
#include "plat.h"

uint16_t gfx_buf[160 * 240];
view_t V;

void gfx_begin(int x, int y, int w, int h) { V = (view_t){gfx_buf, x, y, w, h}; }
void gfx_end(void) { plat_push(V.x0, V.y0, V.w, V.h, V.buf); }
void gfx_screen(void (*draw)(void)) {
  for (int half = 0; half < 2; half++) {
    gfx_begin(0, half * 120, SCREEN_W, 120);
    draw();
    gfx_end();
  }
}

uint16_t gfx_mix(uint16_t bg, uint16_t fg, int a) {
  uint32_t b = (bg | (uint32_t)bg << 16) & 0x07E0F81Fu, f = (fg | (uint32_t)fg << 16) & 0x07E0F81Fu;
  uint32_t r = ((f * (uint32_t)a + b * (uint32_t)(32 - a)) >> 5) & 0x07E0F81Fu;
  return (uint16_t)(r | r >> 16);
}

/* the box clipped to the view, in the buffer's rows and columns; false if nothing is left */
static bool clip(int *x, int *y, int *w, int *h) {
  int x0 = *x - V.x0, y0 = *y - V.y0, x1 = x0 + *w, y1 = y0 + *h;
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > V.w) x1 = V.w;
  if (y1 > V.h) y1 = V.h;
  if (x0 >= x1 || y0 >= y1) return false;
  *x = x0, *y = y0, *w = x1 - x0, *h = y1 - y0;
  return true;
}
void gfx_fill(int x, int y, int w, int h, uint16_t c) {
  if (!clip(&x, &y, &w, &h)) return;
  for (int j = 0; j < h; j++) {
    uint16_t *p = V.buf + (y + j) * V.w + x;
    for (int i = 0; i < w; i++) p[i] = c;
  }
}
void gfx_shade(int x, int y, int w, int h, uint16_t c, int a) {
  if (!clip(&x, &y, &w, &h)) return;
  for (int j = 0; j < h; j++) {
    uint16_t *p = V.buf + (y + j) * V.w + x;
    for (int i = 0; i < w; i++) p[i] = gfx_mix(p[i], c, a);
  }
}

/* a picture's rows: for each, a byte of runs, then each run's gap, length and pixels (a byte each, into its palette) */
static void blit(int id, int x, int y, int tint, int alpha) {
  const dj_sprite_t *s = &dj_spr[id];
  int sx = x + s->x - V.x0, sy = y + s->y - V.y0;
  if (sx >= V.w || sx + s->w <= 0 || sy >= V.h || sy + s->h <= 0 || alpha <= 0) return;
  const uint8_t *p = dj_pix + s->pix, *al = dj_alpha + s->pal;
  const uint16_t *pal = dj_pal + s->pal;
  for (int r = 0; r < s->h; r++) {
    int yy = sy + r, runs = *p++, xx = sx;
    if (yy >= V.h) return;
    uint16_t *row = V.buf + yy * V.w;
    while (runs--) {
      xx += *p++;
      int n = *p++;
      if (yy >= 0) {
        int i0 = xx < 0 ? -xx : 0, i1 = xx + n > V.w ? V.w - xx : n;
        for (int i = i0; i < i1; i++) {
          int k = p[i], a = alpha >= 32 ? al[k] : al[k] * alpha >> 5;
          uint16_t c = tint >= 0 ? (uint16_t)tint : pal[k];
          if (a >= 32) row[xx + i] = c;
          else if (a > 0) row[xx + i] = gfx_mix(row[xx + i], c, a);
        }
      }
      p += n, xx += n;
    }
  }
}
void gfx_spr(int id, int x, int y, int alpha) { blit(id, x, y, -1, alpha); }
void gfx_spr_tint(int id, int x, int y, uint16_t c, int alpha) { blit(id, x, y, c, alpha); }

#define PAPER RGB(247, 240, 232) /* the original's graph paper (loadingtile) */
#define PAPER_LINE RGB(241, 229, 214)

static int fdiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
void gfx_paper(int x, int y, int w, int h, int cell, int left, int scroll, int dim) {
  int bx = x, by = y, bw = w, bh = h;
  if (!clip(&bx, &by, &bw, &bh)) return;
  uint16_t plain = PAPER, line = PAPER_LINE, shadow = RGB(150, 140, 125);
  if (dim) plain = gfx_mix(plain, shadow, dim), line = gfx_mix(line, shadow, dim);
  uint16_t tmpl[320];
  for (int i = 0; i < bw; i++) { /* a line where a multiple of cell falls in the column */
    int a = (bx + V.x0 - x) * 16 + i * 16 - left;
    tmpl[i] = fdiv(a + 15, cell) != fdiv(a + 15 - 16, cell) ? line : plain;
  }
  for (int j = 0; j < bh; j++) {
    int a = (by + V.y0 + j) * 16 + scroll;
    uint16_t *p = V.buf + (by + j) * V.w + bx;
    if (fdiv(a + 15, cell) != fdiv(a - 1, cell))
      for (int i = 0; i < bw; i++) p[i] = line;
    else
      for (int i = 0; i < bw; i++) p[i] = tmpl[i];
  }
}

static const dj_glyph_t *font(int f) { return f == F_BIG ? dj_font_big : dj_font_small; }
int gfx_text_w(int f, const char *s) {
  int w = 0;
  for (; *s; s++)
    if (*s >= 32 && *s < 127) w += font(f)[*s - 32].advance;
  return w;
}
int gfx_text(int f, int x, int base, const char *s, uint16_t c, int align) {
  int w = gfx_text_w(f, s);
  x -= align == 1 ? w / 2 : align == 2 ? w : 0;
  for (; *s; s++)
    if (*s >= 32 && *s < 127) {
      const dj_glyph_t *g = &font(f)[*s - 32];
      if (*s != ' ') blit(g->sprite, x, base, c, 32);
      x += g->advance;
    }
  return w;
}
