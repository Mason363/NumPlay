/* Mr Bullet: the contents of data.bin (tools/pack.py): the levels, packed, and the pictures. */
#ifndef MB_RES_H
#define MB_RES_H
#include <stdbool.h>
#include <stdint.h>
#include "data.h"

typedef struct {
  uint16_t w, h;
  int16_t px4, py4;        /* the pivot x 4 */
  uint8_t ncol, flags;     /* flags 1: opaque, 2: a byte a pixel (else 4 bits, colour 0 clear) */
} Pic;

extern const uint8_t mb_data[];
const Pic *res_pic(int i);
const uint16_t *res_pic_palette(const Pic *p);
const uint8_t *res_pic_pixels(const Pic *p);
/* a level unpacked into out (LEVEL_MAX_BYTES): its size, or 0 */
int res_level(int i, uint8_t *out);
int res_mode_first(int mode);
int res_mode_count(int mode);
#endif
