/* The game's texts as they are shown: every number in them written again the way numbers are
 * (the game's BeautifyInText), and the store's descriptions put together. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "game.h"

/* src to dst (size bytes), each run of digits (and commas) written by beautify() */
void text_beautify(const char *src, char *dst, int size) {
  int o = 0;
  while (*src && o < size - 1) {
    if (*src >= '0' && *src <= '9') {
      double v = 0;
      while ((*src >= '0' && *src <= '9') || *src == ',') {
        if (*src != ',') v = v * 10 + (*src - '0');
        src++;
      }
      char t[64];
      beautify(v, 0, t);
      for (char *c = t; *c && o < size - 1; c++) dst[o++] = *c;
    } else if (*src == 6)   /* (keeps two numbers apart: 0.0<span></span>2) */
      src++;
    else
      dst[o++] = *src++;
  }
  dst[o] = 0;
}

/* an upgrade's description as the store's tooltip shows it (with what some of them add) */
void upgrade_description(int u, char *out, int size) {
  char raw[1024];
  int n = 0;
  raw[0] = 0;
  if (u == U_ELDER_PLEDGE) {
    if (S.pledges == 0) n += snprintf(raw + n, sizeof raw - n, "You haven't pledged to the elders yet.\x05");
    else n += snprintf(raw + n, sizeof raw - n, "You've pledged to the elders \x01%u times\x02.\x05", S.pledges);
  }
  const UpgradeRec *r = upgrade_rec(u);
  if (r->tier < N_TIERS && r->building >= 0 && r->tier != TIER_FORTUNE && r->tier != TIER_SYNERGY1 &&
      r->tier != TIER_SYNERGY2 && S.ascension_mode != 1) {
    uint16_t un = building_links(r->building)->unshackle, tu = LIST(TIER_UNSHACKLE)[r->tier];
    if (un != NONE && has(un) && tu != NONE && has(tu))
      n += snprintf(raw + n, sizeof raw - n, "Unshackled! \x01+%d%%\x02 extra production.\x05",
                    (int)round((r->building == 1 ? 0.5 : (20 - r->building) * 0.1) * 100));
  }
  if (u == U_CENTURY_EGG) {
    double day = floor((S.play_time - S.start_date) / 1000 / 10) * 10 / 60 / 60 / 24;
    if (day > 100) day = 100;
    double k = (1 - pow(1 - day / 100, 3)) * 0.1;
    char t[32];
    beautify(k * 100, 1, t);
    n += snprintf(raw + n, sizeof raw - n, "Current boost: \x01+%s%%\x02\x05", t);
  }
  n += snprintf(raw + n, sizeof raw - n, "%s", res_text(r->desc));
  /* (what makes the game bake while it's closed: no use on the calculator, which can't count that time; kept, as
   * the heavenly upgrades past them need them) */
  if ((u >= U_ANGELS && u <= U_TWIN_GATES_OF_TRANSCENDENCE) || (u >= U_BELPHEGOR && u <= U_LUCIFER))
    snprintf(raw + n, sizeof raw - n, "\x05On the calculator, the game doesn't bake while it's closed: this does nothing "
             "here, but the upgrades past it need it.");
  text_beautify(raw, out, size);
}
