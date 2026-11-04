/* Among Us: the pictures, fonts and the ship's tiles in data.bin (tools/pack.py). */
#ifndef AU_RES_H
#define AU_RES_H
#include <stdint.h>
#include "data.h"

extern const uint8_t au_data[];

/* a picture: its colours (RGB565, then alpha 0-255), then a byte a pixel, or two (P_4BIT, high bits first).
 * P_MIX pictures (the players) have no colours of their own: their bytes index the mix table, coloured per player. */
typedef struct {
  uint16_t w, h;
  int16_t ox, oy;        /* its pivot, from the top-left */
  uint8_t ncolors, flags;
  uint16_t pad;
} Pic;
enum { P_4BIT = 1, P_OPAQUE = 2, P_MIX = 4 };

const Pic *pic(int id);
const void *blob(int id);
static inline const uint16_t *pic_pal(const Pic *p) { return (const uint16_t *)(const void *)(p + 1); }
static inline const uint8_t *pic_alpha(const Pic *p) {
  return (const uint8_t *)(pic_pal(p) + (p->ncolors ? p->ncolors : 256));
}
static inline const uint8_t *pic_px(const Pic *p) {
  if (p->flags & P_MIX) return (const uint8_t *)(p + 1);
  int n = p->ncolors ? p->ncolors : 256;
  uintptr_t a = (uintptr_t)((const uint8_t *)(pic_pal(p) + n) + n);
  return (const uint8_t *)((a + 3) & ~(uintptr_t)3);
}
static inline int pic_ncolors(const Pic *p) { return p->ncolors ? p->ncolors : 256; }

typedef struct {
  uint8_t w, h;
  int8_t x, y;             /* from the pen; y from the baseline */
  uint8_t adv, pad0;
  uint16_t pad1;
  uint32_t off;            /* 4-bit coverage, two pixels a byte */
} Glyph;
typedef struct {
  uint8_t first, count, height, ascent;
} Font;
const Font *font(int id);
static inline const Glyph *font_glyphs(const Font *f) { return (const Glyph *)(const void *)(f + 1); }
static inline const uint8_t *font_bits(const Font *f) { return (const uint8_t *)(font_glyphs(f) + f->count); }

/* a big picture (the ship's floor, the panels): tw x th tiles of 16x16, a tile number each (0xFFFF: nothing; bit 15:
 * colour 15 is clear), each tile 16 colours and 4-bit pixels */
typedef struct {
  uint16_t tw, th;
  int16_t ox, oy;          /* its pivot */
  uint32_t tiles_off;
} TilePic;
#define T_EMPTY 0xFFFF
#define T_CLEAR 0x8000
static inline const TilePic *tilepic(int blob_id) { return (const TilePic *)blob(blob_id); }
static inline const uint16_t *tile_map(const TilePic *s) { return (const uint16_t *)(const void *)(s + 1); }
static inline const uint8_t *tile_data(const TilePic *s, unsigned t) {
  return (const uint8_t *)s + s->tiles_off + (t & 0x7FFF) * 160u;
}
/* the colour at (x, y) (from the top-left), or -1 if clear */
static inline int tile_px(const TilePic *s, int x, int y) {
  if (x < 0 || y < 0 || x >= s->tw * 16 || y >= s->th * 16) return -1;
  unsigned t = tile_map(s)[(y >> 4) * s->tw + (x >> 4)];
  if (t == T_EMPTY) return -1;
  const uint8_t *d = tile_data(s, t);
  uint8_t b = d[32 + (y & 15) * 8 + ((x & 15) >> 1)];
  unsigned k = x & 1 ? b & 15 : b >> 4;
  if ((t & T_CLEAR) && k == 15) return -1;
  return ((const uint16_t *)(const void *)d)[k];
}
#endif
