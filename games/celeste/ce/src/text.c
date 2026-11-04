/* Renogare, the game's font (tools/pack_ce.py font_item): each glyph's cover, 2 bits a pixel, drawn in a colour over
 * another one known to be under it: the four shades between them are the palette's nearest colours, found once for
 * each pair of colours. Screen coordinates (320 x 240), (x, y) the line's top left. */
#include "ce.h"

static const uint8_t *glyph(const uint8_t *f, int c) { return f + 3 + 7 * (c - 32); }
static int kern(const uint8_t *f, int a, int b) {   /* quarter pixels */
  const uint8_t *k = f + 3 + 7 * 95;
  for (int i = 0; i < f[2]; i++, k += 3)
    if (k[0] == a && k[1] == b) return (int8_t)k[2];
  return 0;
}
static bool printable(int c) { return c >= 32 && c < 127; }

int text_width(int font, const char *s) {
  const uint8_t *f = g_fonts[font];
  int w = 0;
  for (; *s; s++) {
    int c = (uint8_t)*s;
    if (!printable(c)) continue;
    w += glyph(f, c)[4];
    if (printable((uint8_t)s[1])) w += kern(f, c, (uint8_t)s[1]);
  }
  return (w + 2) >> 2;
}
int text_height(int font) { return g_fonts[font][0]; }

void text_draw(int font, const char *s, int x, int y, uint32_t rgb, uint32_t under) {
  /* the shades of the last few pairs of colours (finding them looks through the palette) */
  static struct { uint32_t rgb, under; uint8_t gen, shade[4]; } cache[8];
  static uint8_t next, gen = 1;
  extern uint8_t g_palette_gen;
  if (gen != g_palette_gen) {   /* (another palette: found again) */
    for (int i = 0; i < 8; i++) cache[i].gen = 0;
    gen = g_palette_gen;
  }
  int at = 0;
  while (at < 8 && !(cache[at].gen && cache[at].rgb == rgb && cache[at].under == under)) at++;
  if (at == 8) {
    at = next, next = (uint8_t)((next + 1) & 7);
    for (int k = 1; k < 4; k++) {
      int r = (int)(under >> 16 & 255), g = (int)(under >> 8 & 255), b = (int)(under & 255);
      r += ((int)(rgb >> 16 & 255) - r) * k / 3, g += ((int)(rgb >> 8 & 255) - g) * k / 3, b += ((int)(rgb & 255) - b) * k / 3;
      cache[at].shade[k] = pal_nearest((uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b);
    }
    cache[at].rgb = rgb, cache[at].under = under, cache[at].gen = 1;
  }
  const uint8_t *shade = cache[at].shade;
  const uint8_t *f = g_fonts[font];
  int pen = x * 4;
  for (; *s; s++) {
    int c = (uint8_t)*s;
    if (!printable(c)) continue;
    const uint8_t *g = glyph(f, c);
    int gx = ((pen + 2) >> 2) + (int8_t)g[0], gy = y + (int8_t)g[1], w = g[2], h = g[3], stride = (w + 3) >> 2;
    const uint8_t *bits = f + R16(g + 5);
    for (int r = 0; r < h; r++, bits += stride) {
      int sy = gy + r;
      if ((unsigned)sy >= 240) continue;
      uint8_t *d = g_fb + sy * 320;
      for (int i = 0; i < w; i++) {
        int a = bits[i >> 2] >> ((i & 3) * 2) & 3, sx = gx + i;
        if (a && (unsigned)sx < 320) d[sx] = shade[a];
      }
    }
    pen += g[4];
    if (printable((uint8_t)s[1])) pen += kern(f, c, (uint8_t)s[1]);
  }
}
void text_center(int font, const char *s, int cx, int y, uint32_t rgb, uint32_t under) {
  text_draw(font, s, cx - text_width(font, s) / 2, y, rgb, under);
}
void text_right(int font, const char *s, int rx, int y, uint32_t rgb, uint32_t under) {
  text_draw(font, s, rx - text_width(font, s), y, rgb, under);
}
