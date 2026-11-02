/* data.bin (res.h): a header, the levels' index, the pictures' index, then their bytes. */
#include <string.h>
#include "inflate.h"
#include "res.h"

static uint32_t u32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t u16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

static const uint8_t *pic_at(int i) {
  uint32_t levels = u32(mb_data);
  return mb_data + u32(mb_data + 16 + 8 * levels + 4 * i);
}
const Pic *res_pic(int i) { return (const Pic *)pic_at(i); }
const uint16_t *res_pic_palette(const Pic *p) { return (const uint16_t *)((const uint8_t *)p + sizeof(Pic)); }
const uint8_t *res_pic_pixels(const Pic *p) { return (const uint8_t *)(res_pic_palette(p) + p->ncol); }

int res_level(int i, uint8_t *out) {
  if (i < 0 || i >= LEVEL_COUNT) return 0;
  const uint8_t *e = mb_data + 16 + 8 * i;
  uint32_t off = u32(e);
  uint16_t packed = u16(e + 4), size = u16(e + 6);
  if (size > LEVEL_MAX_BYTES) return 0;
  if (!inflate_raw(mb_data + off, packed, out, size)) return 0;
  return size;
}

static const int first[] = MODE_FIRST;
int res_mode_first(int mode) { return first[mode]; }
int res_mode_count(int mode) { return first[mode + 1] - first[mode]; }
