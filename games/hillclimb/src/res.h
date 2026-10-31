/* Hill Climb Racing: the pictures and fonts in data.bin (tools/pack.py). */
#ifndef HC_RES_H
#define HC_RES_H
#include <stdint.h>
#include "data.h"

typedef struct {
  uint16_t w, h, ncolors, flags;   /* flags IMG_* */
  uint16_t ppm4;                   /* its pixels a meter x 4 (0: a picture of the menus) */
  int16_t px4, py4;                /* its pivot x 4 */
  uint16_t clear;                  /* a cropped one's see-through colour, all around its crop */
} Img;
/* opaque: its colours also as RGB565; cropped: only the box around its visible pixels kept (its x, y, w, h, a byte
 * each, before the pixels); tiled (pictures of the menus): its pixels in 8 x 8 tiles, each with its own few colours */
enum { IMG_OPAQUE = 1, IMG_CROPPED = 2, IMG_TILED = 4 };

typedef struct {
  uint8_t w, h;
  int8_t x, y;        /* the bitmap's top-left from the pen, y from the baseline */
  uint8_t adv, packed, pad[2];   /* packed: its rows in runs (tools/pack.py pack_glyphs) */
  uint32_t off;
} Glyph;
typedef struct {
  uint8_t first, count, height, ascent;
} Font;

extern const uint8_t hc_data[];
const Img *img(int id);
static inline const uint32_t *img_pal(const Img *i) { return (const uint32_t *)(const void *)(i + 1); }
static inline const uint16_t *img_pal565(const Img *i) { return (const uint16_t *)(const void *)(img_pal(i) + i->ncolors); }
/* a cropped picture's box (x, y, w, h) */
static inline const uint8_t *img_crop(const Img *i) {
  return (const uint8_t *)(img_pal(i) + i->ncolors) + (i->flags & IMG_OPAQUE ? 2 * i->ncolors : 0);
}
static inline const uint8_t *img_px(const Img *i) { return img_crop(i) + (i->flags & IMG_CROPPED ? 4 : 0); }
/* a tiled picture's pixel (x, y): its tiles' offsets (u16, from after them), then each tile: its colours' count n and
 * the colours, then its pixels as indices of them, 1, 2 or 4 bits each (high bits first); n 0: its pixels as they are */
static inline uint8_t img_tile_index(const Img *i, const uint8_t *px, int x, int y) {
  unsigned w = i->w, h = i->h;
  if (i->flags & IMG_CROPPED) {
    const uint8_t *c = img_crop(i);
    unsigned cx = (unsigned)(x - c[0]), cy = (unsigned)(y - c[1]);
    if (cx >= c[2] || cy >= c[3]) return (uint8_t)i->clear;
    x = (int)cx, y = (int)cy, w = c[2], h = c[3];
  }
  unsigned cols = (w + 7) >> 3, rows = (h + 7) >> 3, at = ((unsigned)y >> 3) * cols + ((unsigned)x >> 3);
  const uint8_t *t = px + 2 * cols * rows + (px[2 * at] | px[2 * at + 1] << 8);
  unsigned n = *t++, tw = w - ((unsigned)x & ~7u) < 8 ? w - ((unsigned)x & ~7u) : 8;
  unsigned pos = ((unsigned)y & 7) * tw + ((unsigned)x & 7);
  if (!n) return t[pos];
  if (n == 1) return t[0];
  unsigned bits = n == 2 ? 1 : n <= 4 ? 2 : 4, bit = pos * bits;
  return t[(t[n + (bit >> 3)] >> (8 - bits - (bit & 7))) & ((1u << bits) - 1)];
}

const Font *font(int id);
static inline const Glyph *font_glyphs(const Font *f) { return (const Glyph *)(const void *)(f + 1); }
static inline const uint8_t *font_bits(const Font *f) { return (const uint8_t *)(font_glyphs(f) + f->count); }
#endif
