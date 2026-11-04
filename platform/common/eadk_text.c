/* eadk_display_draw_string for every target but the NumWorks: the firmware's cells (7 x 14, 10 x 18 large) with
 * NumPlay's own fonts (launcher/src/font_data.c), as the launcher does on Upsilon (launcher/src/compat.c). Letters
 * beyond ASCII show as '?'. */
#include <eadk.h>
#include "../../launcher/src/font.h"

static eadk_color_t blend(eadk_color_t bg, eadk_color_t fg, int a16) {
  if (a16 >= 15) return fg;
  if (a16 <= 0) return bg;
  uint32_t B = (bg | (uint32_t)bg << 16) & 0x07E0F81F, F = (fg | (uint32_t)fg << 16) & 0x07E0F81F;
  uint32_t M = ((B * (uint32_t)(16 - a16) + F * (uint32_t)a16) >> 4) & 0x07E0F81F;
  return (eadk_color_t)(M | M >> 16);
}

static void cell(const np_font_t *f, int cw, int ch, int x, int y, char c, eadk_color_t fg, eadk_color_t bg) {
  eadk_color_t px[10 * 18];
  for (int i = 0; i < cw * ch; i++) px[i] = bg;
  unsigned k = (uint8_t)c;
  if (k >= f->first && k < (unsigned)f->first + f->count) {
    const np_glyph_t *g = &f->glyphs[k - f->first];
    int pen = (cw - g->advance) / 2, base = (ch + f->ascent - f->descent) / 2;
    const uint8_t *d = f->data + g->offset;
    for (int j = 0; j < g->h; j++)
      for (int i = 0; i < g->w; i++) {
        int n = j * g->w + i, a = (d[n >> 1] >> ((n & 1) * 4)) & 15;
        int cx = pen + g->x + i, cy = base + g->y + j;
        if (a && cx >= 0 && cx < cw && cy >= 0 && cy < ch) px[cy * cw + cx] = blend(bg, fg, a);
      }
  }
  if (x + cw > EADK_SCREEN_WIDTH || y + ch > EADK_SCREEN_HEIGHT) return;
  eadk_display_push_rect((eadk_rect_t){(uint16_t)x, (uint16_t)y, (uint16_t)cw, (uint16_t)ch}, px);
}

void eadk_display_draw_string(const char *text, eadk_point_t point, bool large_font, eadk_color_t text_color,
                              eadk_color_t background_color) {
  const np_font_t *f = large_font ? &np_font_body : &np_font_small;
  int cw = large_font ? 10 : 7, ch = large_font ? 18 : 14, x = point.x, y = point.y;
  for (const char *s = text; *s; s++) {
    if (*s == '\n') {
      x = point.x, y += ch;
      continue;
    }
    if (((uint8_t)*s & 0xC0) == 0x80) continue; /* (the rest of a UTF-8 letter) */
    cell(f, cw, ch, x, y, (uint8_t)*s >= 0x80 ? '?' : *s, text_color, background_color);
    x += cw;
  }
}
