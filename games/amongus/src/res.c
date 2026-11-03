/* Among Us: the pictures, fonts and tiles in data.bin: "AUSD", the counts, then where each one starts. */
#include "res.h"

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

const void *blob(int id) { return au_data + rd32(au_data + 16 + 4 * id); }

const Pic *pic(int id) {
  if (id < 0 || id >= N_PICS) return 0;
  return (const Pic *)blob(id);
}

const Font *font(int id) { return (const Font *)blob(N_PICS + id); }
