/* The game's texts as they are shown: every number in them written again the way numbers are
 * (the game's BeautifyInText), and the store's descriptions put together. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "game.h"

/* CC_CCK2_TEXT_BPE_V1: on-demand lossless CCK2 text expansion.
 * Raw strings remain zero-copy flash pointers; compressed strings use eight
 * rotating 1024-byte RAM slots. Callers cannot retain a decoded pointer across
 * eight other distinct compressed text lookups. */
#define CC_TEXT_BPE_SLOTS 8
#define CC_TEXT_BPE_SLOT 1024
static char cc_bpe_cache[CC_TEXT_BPE_SLOTS][CC_TEXT_BPE_SLOT];
static uint32_t cc_bpe_key[CC_TEXT_BPE_SLOTS];
static uint8_t cc_bpe_valid[CC_TEXT_BPE_SLOTS];
static unsigned cc_bpe_next;
static const uint8_t cc_bpe_token_index[256] = {
  [255] = 1,
  [254] = 2,
  [253] = 3,
  [252] = 4,
  [251] = 5,
  [250] = 6,
  [249] = 7,
  [248] = 8,
  [247] = 9,
  [246] = 10,
  [245] = 11,
  [244] = 12,
  [243] = 13,
  [242] = 14,
  [241] = 15,
  [240] = 16,
  [239] = 17,
  [238] = 18,
  [237] = 19,
  [236] = 20,
  [235] = 21,
  [234] = 22,
  [233] = 23,
  [232] = 24,
  [231] = 25,
  [230] = 26,
  [229] = 27,
  [228] = 28,
  [227] = 29,
  [226] = 30,
  [225] = 31,
  [224] = 32,
  [223] = 33,
  [222] = 34,
  [221] = 35,
  [220] = 36,
  [219] = 37,
  [218] = 38,
  [217] = 39,
  [216] = 40,
  [215] = 41,
  [214] = 42,
  [213] = 43,
  [212] = 44,
  [211] = 45,
  [210] = 46,
  [209] = 47,
  [208] = 48,
  [207] = 49,
  [206] = 50,
  [205] = 51,
  [204] = 52,
  [203] = 53,
  [202] = 54,
  [201] = 55,
  [200] = 56,
  [199] = 57,
  [198] = 58,
  [197] = 59,
  [196] = 60,
  [195] = 61,
  [194] = 62,
  [193] = 63,
  [192] = 64,
  [191] = 65,
  [190] = 66,
  [189] = 67,
  [188] = 68,
  [187] = 69,
  [186] = 70,
  [185] = 71,
  [184] = 72,
  [183] = 73,
  [182] = 74,
  [181] = 75,
  [180] = 76,
  [179] = 77,
  [178] = 78,
  [177] = 79,
  [176] = 80,
  [175] = 81,
  [174] = 82,
  [173] = 83,
  [172] = 84,
  [171] = 85,
  [170] = 86,
  [169] = 87,
  [168] = 88,
  [167] = 89,
  [166] = 90,
  [165] = 91,
  [163] = 92,
  [162] = 93,
  [161] = 94,
  [160] = 95,
  [159] = 96,
};
static const uint8_t cc_bpe_pairs[][2] = {
  {101, 32},
  {115, 32},
  {105, 110},
  {116, 104},
  {101, 114},
  {116, 32},
  {48, 48},
  {100, 32},
  {111, 117},
  {97, 110},
  {111, 110},
  {121, 32},
  {101, 110},
  {111, 114},
  {44, 32},
  {253, 103},
  {97, 108},
  {101, 115},
  {97, 114},
  {111, 32},
  {249, 249},
  {116, 105},
  {32, 99},
  {111, 102},
  {252, 255},
  {240, 32},
  {101, 248},
  {101, 254},
  {121, 247},
  {116, 236},
  {111, 111},
  {114, 101},
  {232, 32},
  {251, 32},
  {97, 32},
  {105, 116},
  {97, 116},
  {115, 116},
  {253, 32},
  {225, 107},
  {111, 109},
  {99, 104},
  {216, 105},
  {108, 244},
  {46, 32},
  {111, 119},
  {119, 105},
  {246, 248},
  {105, 254},
  {101, 108},
  {235, 235},
  {97, 99},
  {97, 115},
  {32, 112},
  {108, 105},
  {97, 107},
  {114, 32},
  {111, 108},
  {242, 32},
  {101, 109},
  {117, 110},
  {97, 250},
  {117, 114},
  {114, 105},
  {245, 32},
  {118, 255},
  {209, 252},
  {118, 251},
  {233, 213},
  {108, 32},
  {97, 100},
  {97, 254},
  {119, 104},
  {115, 250},
  {114, 111},
  {101, 99},
  {105, 108},
  {2, 32},
  {46, 10},
  {101, 119},
  {105, 99},
  {103, 104},
  {108, 101},
  {189, 32},
  {97, 109},
  {84, 104},
  {103, 114},
  {115, 105},
  {115, 104},
  {117, 108},
  {239, 32},
  {243, 32},
  {227, 199},
  {102, 197},
  {108, 255},
  {246, 32},
};

const char *res_text(uint32_t off) {
  const uint8_t *in = cc_data + OFF_TEXT + (off & 0x7FFFFFFFu);
  if (!(off & 0x80000000u)) return (const char *)(const void *)in;
  for (unsigned i = 0; i < CC_TEXT_BPE_SLOTS; i++)
    if (cc_bpe_valid[i] && cc_bpe_key[i] == off) return cc_bpe_cache[i];
  unsigned slot = cc_bpe_next++ % CC_TEXT_BPE_SLOTS;
  cc_bpe_key[slot] = off;
  cc_bpe_valid[slot] = 0;
  char *out = cc_bpe_cache[slot];
  uint8_t stack[128];
  unsigned top = 0, n = 0;
  for (;;) {
    unsigned ch;
    if (top) ch = stack[--top];
    else { ch = *in++; if (!ch) break; }
    unsigned index = cc_bpe_token_index[ch];
    if (index) {
      if (top + 2 > sizeof stack) { out[0] = 0; return out; }
      stack[top++] = cc_bpe_pairs[index - 1][1];
      stack[top++] = cc_bpe_pairs[index - 1][0];
    } else {
      if (n + 1 >= CC_TEXT_BPE_SLOT) { out[0] = 0; return out; }
      out[n++] = (char)ch;
    }
  }
  out[n] = 0;
  cc_bpe_valid[slot] = 1;
  return out;
}

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
