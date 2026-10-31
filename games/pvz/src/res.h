/* Plants vs. Zombies: data.bin (tools/pack.py), read where it lies in flash.
 *
 * Animations (A_*, and the single pictures I_*) are frames of runs of palette
 * colours; the lawns and menus' backgrounds (BG_*) are DEFLATE chunks of rows,
 * unpacked into RAM when a screen starts; fonts (F_*) are 4-bit glyphs. */
#ifndef PVZ_RES_H
#define PVZ_RES_H
#include <stdbool.h>
#include <stdint.h>
#include "data.h"

extern const uint8_t *pvz_bin;
static inline uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static inline int16_t rds16(const uint8_t *p) { return (int16_t)rd16(p); }
static inline uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static inline const uint8_t *table(int t) { return pvz_bin + rd32(pvz_bin + 4 + 4 * t); }
enum { T_ANIMS, T_FRAMES, T_PALS, T_BACKS, T_FONTS, T_BOSS };

typedef struct {
  uint16_t first, pal, cycle, base;   /* base: the animation an overlay is drawn over, 0xFFFF none */
  uint8_t frames, bpp;
} Anim;
static inline Anim anim_info(int a) {
  const uint8_t *p = table(T_ANIMS) + 12 * a;
  return (Anim){rd16(p), rd16(p + 2), rd16(p + 4), rd16(p + 6), p[8], p[9]};
}
static inline int anim_frames(int a) { return table(T_ANIMS)[12 * a + 8]; }
/* the frame of animation a shown t hundredths of a second into it (looping) */
int anim_frame_at(int a, uint32_t t);

/* PVZ_SHARED_RLE_ROWS_V1: bit 31 on a 4-bit frame offset enables row references. */
typedef struct {
  int16_t x, y;             /* top-left from the anchor */
  uint16_t w, h;
  const uint8_t *rows, *end;   /* end: past its last row (data.bin keeps the frames in order, then the palettes) */
  uint8_t shared_rows;
} Frame;
static inline Frame frame_info(int f) {
  const uint8_t *p = table(T_FRAMES) + 12 * f;
  int n = (int)rd32(pvz_bin + 32);   /* (the header: 6 tables, the animations, the frames) */
  uint32_t off = rd32(p + 8);
  return (Frame){rds16(p), rds16(p + 2), rd16(p + 4), rd16(p + 6), pvz_bin + (off & 0x7fffffffu),
                 f + 1 < n ? pvz_bin + (rd32(p + 20) & 0x7fffffffu) : table(T_PALS), (uint8_t)(off >> 31)};
}
/* a palette: its colours (RGB565, aligned) */
static inline const uint16_t *pal_colors(int p, int *n) {
  const uint8_t *q = pvz_bin + rd32(table(T_PALS) + 4 * p);
  if (n) *n = rd16(q);
  return (const uint16_t *)(const void *)(q + 4);
}

typedef struct {
  uint16_t w, h, ncol, rows, pal, nchunks;
  const uint8_t *chunks;    /* nchunks + 1 offsets (u32) */
} Back;
static inline Back back_info(int b) {
  const uint8_t *p = table(T_BACKS) + 16 * b;
  return (Back){rd16(p), rd16(p + 2), rd16(p + 4), rd16(p + 6), rd16(p + 8), rd16(p + 10), pvz_bin + rd32(p + 12)};
}

typedef struct {
  uint8_t first, count, ascent, descent;
  const uint8_t *glyphs;    /* 8 bytes each */
  const uint8_t *data;
} Font;
static inline Font font_info(int f) {
  const uint8_t *p = pvz_bin + rd32(table(T_FONTS) + 4 * f);
  return (Font){p[0], p[1], p[2], p[3], p + 4, p + 4 + 8 * p[1]};
}
#endif
