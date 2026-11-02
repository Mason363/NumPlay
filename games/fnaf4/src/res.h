/* Five Nights at Freddy's 4: data.bin (tools/pack.py), read where it lies in flash. */
#ifndef FNAF4_RES_H
#define FNAF4_RES_H
#include <stdint.h>
#include "data.h"

extern const uint8_t *fnaf4_bin;

/* (byte by byte: the data is not always aligned) */
static inline uint32_t rd16(const uint8_t *p) { return (uint32_t)(p[0] | p[1] << 8); }
static inline int32_t rds16(const uint8_t *p) { return (int16_t)(p[0] | p[1] << 8); }
static inline uint32_t rd24(const uint8_t *p) { return (uint32_t)(p[0] | p[1] << 8 | p[2] << 16); }
static inline uint32_t rd32(const uint8_t *p) { return (uint32_t)(p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24); }
static inline const uint8_t *section(int s) { return fnaf4_bin + rd32(fnaf4_bin + 8 + 4 * s); }
#endif
