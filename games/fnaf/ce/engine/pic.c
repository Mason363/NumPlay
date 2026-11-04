/* Decoding the pictures (pic.h). The grid's blocks into pixels: pic_expand, in C here, in assembly on the
 * calculator (pic_ez80.s, CE_ASM). */
#include "pic.h"

void zx0_Decompress(void *dst, const void *src);

static int bw_of(const uint8_t *pic) { return pic[1] | pic[2] << 8; }
int pic_width(const uint8_t *pic) { return 2 * bw_of(pic); }
int pic_height(const uint8_t *pic) { return 2 * pic[3]; }
int pic_base(const uint8_t *pic) { return pic[0] & 1 ? pic[4] | pic[5] << 8 : -1; }
bool pic_keeps(const uint8_t *pic) { return (pic[0] & 2) != 0; }

void pic_decode(const uint8_t *pic, uint8_t *dst, int stride, const uint8_t *map, uint8_t *temp) {
  int bw = bw_of(pic), bh = pic[3];
  uint8_t *grid = temp ? temp : dst + 2 * bh * stride - bw * bh;   /* (in place: the blocks' bytes at its end) */
  zx0_Decompress(grid, pic + (pic[0] & 1 ? 6 : 4));
  pic_expand(grid, dst, stride, bw, bh, map);
}

void pic_decode_wide(const uint8_t *pic, uint8_t *dst, int stride, const uint8_t *map, uint8_t *temp) {
  int bw = bw_of(pic), bh = pic[3];
  uint8_t *grid = temp ? temp : dst + 2 * bh * stride - bw * bh;   /* (in place: as pic_decode, read before written) */
  zx0_Decompress(grid, pic + (pic[0] & 1 ? 6 : 4));
  pic_expand_wide(grid, dst, stride, bw, bh, map);
}

#ifndef CE_ASM
void pic_expand(const uint8_t *grid, uint8_t *dst, int stride, int bw, int bh, const uint8_t *map) {
  for (int by = 0; by < bh; by++) {
    uint8_t *r0 = dst + 2 * by * stride, *r1 = r0 + stride;
    for (int bx = 0; bx < bw; bx++, r0 += 2, r1 += 2) {
      uint8_t k = *grid++;
      if (k == 255) continue;
      r0[0] = map[k], r0[1] = map[256 + k], r1[0] = map[512 + k], r1[1] = map[768 + k];
    }
  }
}

void pic_expand_wide(const uint8_t *grid, uint8_t *dst, int stride, int bw, int bh, const uint8_t *map) {
  for (int by = 0; by < bh; by++) {
    uint8_t *r0 = dst + 2 * by * stride, *r1 = r0 + stride;
    for (int bx = 0; bx < bw; bx++, r0 += 4, r1 += 4) {
      uint8_t k = *grid++;
      if (k == 255) continue;
      r0[0] = r0[1] = map[k], r0[2] = r0[3] = map[256 + k];
      r1[0] = r1[1] = map[512 + k], r1[2] = r1[3] = map[768 + k];
    }
  }
}
#endif
