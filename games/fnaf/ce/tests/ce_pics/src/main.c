/* The calculator's decoder (engine/pic_ez80.s) checked: every picture decoded with its bases, a sum of its bytes
 * (two 16-bit sums, as tests/pics.c --sums makes them on a computer) into the appvar FNAFSUM. tests/check.py runs
 * it in tools/ce_emu and compares; it also times the decoding (the cycles in FNAFSUM's last 4 bytes...). */
#include <fileioc.h>
#include <string.h>
#include <time.h>
#include "../../../src/fnaf.h"

static const uint8_t *vars[DATA_VARS];
static uint8_t sums[PIC_COUNT * 4 + 8];

int main(void) {
  if (!ce_data(DATA_VAR, DATA_VARS, vars)) return 1;
  static const Assets a = {vars, asset_at, pic_group, PIC_FIRST, MAP_FIRST, FONT_FIRST};
  gfx_init(&a, (uint8_t *)0xD52C00, 160 * 120);   /* (the second screen buffer: room) */
  uint8_t *buf = (uint8_t *)0xD40000;   /* (the screen's memory: room for any picture) */
  clock_t t0 = clock();
#ifdef ONLY
  for (int p = ONLY; p <= ONLY; p++) {
#else
  for (int p = 0; p < PIC_COUNT; p++) {
#endif
    int w = pic_w(p), h = pic_h(p);
    pic_full(p, buf, w);
    uint16_t s1 = 0, s2 = 0;
    for (uint8_t *q = buf, *e = buf + w * h; q < e; q++) s1 += *q, s2 += s1;
    sums[4 * p] = (uint8_t)s1, sums[4 * p + 1] = (uint8_t)(s1 >> 8);
    sums[4 * p + 2] = (uint8_t)s2, sums[4 * p + 3] = (uint8_t)(s2 >> 8);
  }
  uint32_t t = (uint32_t)(clock() - t0);
#ifdef DUMP   /* (one picture's bytes, to look at: FNAFDMP) */
  pic_full(DUMP, buf, pic_w(DUMP));
  uint8_t d = ti_Open("FNAFDMP", "w");
  if (d) {
    ti_Write(buf, 1, (size_t)(pic_w(DUMP) * pic_h(DUMP)), d);
    ti_Close(d);
  }
#endif
  memcpy(sums + PIC_COUNT * 4, &t, 4);
  uint8_t f = ti_Open("FNAFSUM", "w");
  if (f) {
    ti_Write(sums, 1, sizeof sums, f);
    ti_Close(f);
  }
  return 0;
}
