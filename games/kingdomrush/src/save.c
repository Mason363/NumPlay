/* The save: kingdomrush.sav, "KRS", a version, then the progress (game.h's Progress), and a check byte. */
#include <string.h>
#include "game.h"
#include "plat.h"

#define SAVE_NAME "kingdomrush.sav"
#define SAVE_VERSION 1

static uint8_t sum(const uint8_t *p, uint32_t n) {
  uint8_t s = 0x5A;
  for (uint32_t i = 0; i < n; i++) s = (uint8_t)(s * 31 + p[i]);
  return s;
}

void save_progress(void) {
  uint8_t buf[4 + sizeof(Progress) + 1];
  memcpy(buf, "KRS", 3);
  buf[3] = SAVE_VERSION;
  memcpy(buf + 4, &g.p, sizeof g.p);
  buf[sizeof buf - 1] = sum(buf, sizeof buf - 1);
  plat_save(SAVE_NAME, buf, sizeof buf);
}

void load_progress(void) {
  memset(&g.p, 0, sizeof g.p);
  g.p.difficulty = 1;
  uint32_t n = 0;
  const uint8_t *d = plat_load(SAVE_NAME, &n);
  if (!d || n != 4 + sizeof(Progress) + 1 || memcmp(d, "KRS", 3) || d[3] != SAVE_VERSION || sum(d, n - 1) != d[n - 1])
    return;
  Progress p;
  memcpy(&p, d + 4, sizeof p);
  /* anything out of range: a fresh start rather than a broken one */
  for (int i = 0; i < 12; i++)
    if (p.camp[i] > 3 || p.heroic[i] > 1 || p.iron[i] > 1) return;
  for (int i = 0; i < 6; i++)
    if (p.ups[i] > 5) return;
  if (p.difficulty > 2) p.difficulty = 1;
  g.p = p;
}
