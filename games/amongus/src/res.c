/* Among Us: the pictures, fonts and tiles in data.bin: "AUSD", the counts, then where each one starts. The big
 * panels and the kill's frames are packed (DEFLATE) and unpacked into RAM when shown. */
#include <string.h>
#include "inflate.h"
#include "res.h"

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

const void *blob(int id) { return au_data + rd32(au_data + 16 + 4 * id); }

const Pic *pic(int id) {
  if (id < 0 || id >= N_PICS) return 0;
  return (const Pic *)blob(id);
}

const Font *font(int id) { return (const Font *)blob(N_PICS + id); }

/* the RAM unpacked into: a big slot (a panel), and two small ones after a panel's 25 KB (the kill's frames over its
 * flames) */
#define SMALL_AT 25600
#define SMALL 8192
_Static_assert(BIG_UNPACKED <= SMALL_AT + 2 * SMALL && KILL_PIC_MAX <= SMALL, "the unpacking room");
static uint32_t scratch_words[(SMALL_AT + 2 * SMALL) / 4];
#define SCRATCH ((uint8_t *)scratch_words)
static int slot_id[3] = {-1, -1, -1};
static uint32_t big_size;

const void *unpacked(int id, int slot) {
  if (slot_id[slot] == id) return slot ? SCRATCH + SMALL_AT + (slot - 1) * SMALL : SCRATCH;
  const uint8_t *z = (const uint8_t *)blob(id);
  uint32_t size = rd32(z);
  uint32_t zlen = rd32(au_data + 16 + 4 * (id + 1)) - rd32(au_data + 16 + 4 * id) - 4;
  uint8_t *dst = slot ? SCRATCH + SMALL_AT + (slot - 1) * SMALL : SCRATCH;
  uint32_t cap = slot ? SMALL : SMALL_AT + 2 * SMALL;
  if (slot == 0) {
    big_size = size;
    if (size > SMALL_AT) slot_id[1] = slot_id[2] = -1;
  } else if (big_size > SMALL_AT) slot_id[0] = -1, big_size = 0;
  if (size > cap || inflate_raw(z + 4, zlen, dst, cap) != (int32_t)size) {
    /* (never with the game's own data) */
    memset(dst, 0, 16);
    slot_id[slot] = -1;
    return dst;
  }
  slot_id[slot] = id;
  return dst;
}

uint8_t *scratch_borrow(uint32_t *size) {
  slot_id[0] = slot_id[1] = slot_id[2] = -1;
  big_size = 0;
  *size = sizeof scratch_words;
  return SCRATCH;
}
