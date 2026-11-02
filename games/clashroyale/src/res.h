/* Clash Royale: data.bin (tools/pack.py), read where it lies in flash. */
#ifndef CR_RES_H
#define CR_RES_H
#include <stdint.h>
#include "data.h"

extern const uint8_t *cr_bin;

/* (byte by byte: the data is not always aligned) */
static inline uint32_t rd16(const uint8_t *p) { return (uint32_t)(p[0] | p[1] << 8); }
static inline int32_t rds16(const uint8_t *p) { return (int16_t)(p[0] | p[1] << 8); }
static inline uint32_t rd32(const uint8_t *p) { return (uint32_t)(p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24); }
static inline const uint8_t *section(int s) { return cr_bin + rd32(cr_bin + 8 + 4 * s); }
#endif
