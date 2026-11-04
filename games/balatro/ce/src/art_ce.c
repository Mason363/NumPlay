/* The art on the TI-84 Plus CE: read where it is, in the archived appvars (tools/mkart.py). The cards' size of art
 * (Jokers, Tarots, Planets, Spectrals, Vouchers, packs) is zx0-packed there and unpacked into a few slots of RAM when
 * shown, the least recently used one making way. */
#include <string.h>
#include "art.h"
#include "assets.h"
#include "ce.h"
#ifndef NP_HOST
#include <compression.h>
#else
void zx0_Decompress(void *dst, const void *src);
#endif

#define NSLOT 6

const uint8_t *ce_lut, *ce_fire, *ce_edpat;
const uint8_t *ce_text_blob; /* src/data.c's texts (gen/data.c reads them here) */
static const uint8_t *vars[CE_ART_NVARS];
static struct {
  int16_t id;
  uint16_t used;
} slot[NSLOT];
static uint8_t slot_px[NSLOT][CE_PACKED_MAX];
static uint16_t clock_;

int ce_art_init(void) {
  size_t n;
  for (int i = 0; i < CE_ART_NVARS; i++)
    if (!(vars[i] = ce_appvar(ce_art_vars[i], &n))) return 0;
  if (!(ce_lut = ce_appvar("BALLUT", &n)) || !(ce_fire = ce_appvar("BALFIRE", &n)) || !(ce_edpat = ce_appvar("BALED", &n)))
    return 0;
  ce_text_blob = ce_lut + LUT_TEXT;
  art_flush();
  return 1;
}

void art_flush(void) {
  for (int i = 0; i < NSLOT; i++) slot[i].id = -1, slot[i].used = 0;
}

const uint8_t *ce_sprite(int id) {
  const art_sprite_t *s = &art_sprites[id];
  const uint8_t *src = vars[s->group >> 1] + s->off;
  if (!(s->group & 1)) return src;
  clock_++;
  int lru = 0;
  for (int i = 0; i < NSLOT; i++) {
    if (slot[i].id == id) {
      slot[i].used = clock_;
      return slot_px[i];
    }
    if ((uint16_t)(clock_ - slot[i].used) > (uint16_t)(clock_ - slot[lru].used)) lru = i;
  }
  zx0_Decompress(slot_px[lru], src);
  slot[lru].id = (int16_t)id, slot[lru].used = clock_;
  return slot_px[lru];
}
