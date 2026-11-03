/* Moto X3M: the pictures, fonts and levels in data.bin: "MX3D", their counts, then where each one starts. */
#include "res.h"

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

const Pic *pic(int id) {
  if (id < 0 || id >= N_PICS) return 0;
  return (const Pic *)(const void *)(mx_data + rd32(mx_data + 16 + 4 * id));
}

const Font *font(int id) {
  return (const Font *)(const void *)(mx_data + rd32(mx_data + 16 + 4 * (N_PICS + id)));
}

const LevelHdr *level(int n) {
  return (const LevelHdr *)(const void *)(mx_data + rd32(mx_data + 16 + 4 * (N_PICS + N_FONTS + n)));
}
