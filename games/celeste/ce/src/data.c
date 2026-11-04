/* The appvars (tools/pack_ce.py): CELG0.. what every chapter draws, CEL<n>0.. chapter n. Read in place. */
#include "ce.h"

const uint8_t *g_common[MAX_PARTS], *g_chap[MAX_PARTS];
static const uint8_t *chead, *sprites_c, *sprites_h, *anims, *meta, *hair;
const uint8_t *g_tile_flags;
static const uint8_t *tile_chunks;
static int ntiles;
int g_nrooms;
static const uint8_t *rooms;
const uint8_t *g_bg;
int g_bg_camx, g_bg_camy;
static uint16_t pal[256];
static uint8_t tint[3];

static int open_parts(const char *base, const uint8_t **parts) {
  char name[9];
  int n = 0, k = 0;
  while (base[k]) name[k] = base[k], k++;
  for (; n < MAX_PARTS; n++) {
    name[k] = (char)('0' + n), name[k + 1] = 0;
    parts[n] = plat_appvar(name);
    if (!parts[n]) break;
  }
  return n;
}

bool data_open(void) {
  if (!open_parts("CELG", g_common)) return false;
  chead = g_common[0];
  if (memcmp(chead, "CELG", 4) || chead[4] != FORMAT) return false;
  const uint8_t *p = chead + 5 + COMMON * 2;
  sprites_c = CPTR(p), anims = CPTR(p + 3), meta = CPTR(p + 6), hair = CPTR(p + 9);
  memcpy(pal, chead + 5, COMMON * 2);
  return true;
}

bool chapter_open(int n) {
  char base[5] = "CEL0";
  base[3] = (char)('0' + n);
  memset(g_chap, 0, sizeof g_chap);
  if (!open_parts(base, g_chap)) return false;
  const uint8_t *h = g_chap[0];
  if (memcmp(h, "CELC", 4) || h[4] != FORMAT) return false;
  memcpy(pal + COMMON, h + 6, (256 - COMMON) * 2);
  const uint8_t *p = h + 6 + (256 - COMMON) * 2;
  memcpy(tint, p, 3);
  p += 3;
  ntiles = (int)R16(p);
  tile_chunks = p + 3;
  p += 3 + 3 * p[2];
  g_tile_flags = HPTR(p);
  sprites_h = HPTR(p + 3);
  p += 6;
  g_bg = p[0] ? HPTR(p + 7) : NULL;
  g_bg_camx = S24(p + 1), g_bg_camy = S24(p + 4);
  p += 10;
  g_nrooms = (int)R16(p);
  rooms = p + 2;
  return true;
}

const uint8_t *room_rec(int i) { return HPTR(rooms + 3 * i); }
const uint8_t *tile_data(int id) {
  return HPTR(tile_chunks + 3 * (id / 1000)) + (id % 1000) * 64;
}
const uint8_t *sprite(int id) { return CPTR(sprites_c + 2 + 3 * id); }
const uint8_t *csprite(int id) { return HPTR(sprites_h + 2 + 3 * id); }
const uint8_t *anim(int id) { return CPTR(anims + 2 + 3 * id); }
const uint8_t *player_meta(int id) { return meta + 2 + 3 * id; }
const uint16_t *hair_mask(int which) { return (const uint16_t *)(const void *)(hair + 40 * which); }

/* 1555 colours, plus the chapter's tint (its additive sunset), faded to black by fade (256: none) */
static uint16_t shade(uint16_t c, int fade) {
  int r = (c >> 10 & 31) << 3, g = (c >> 5 & 31) << 3, b = (c & 31) << 3;
  r = imin(255, r + tint[0]), g = imin(255, g + tint[1]), b = imin(255, b + tint[2]);
  r = r * fade >> 8, g = g * fade >> 8, b = b * fade >> 8;
  return (uint16_t)((r >> 3) << 10 | (g >> 3) << 5 | (b >> 3));
}
static int cur_fade = 256;
static uint32_t cur_hair = 0xAC3232;
void palette_apply(int fade) {
  uint16_t out[256];
  cur_fade = fade;
  out[0] = 0;
  for (int i = 1; i < 256; i++) out[i] = shade(pal[i], fade);
  plat_palette(out, 0, 256);
  palette_hair(cur_hair);
}
void palette_hair(uint32_t rgb) {
  cur_hair = rgb;
  int r = rgb >> 16 & 255, g = rgb >> 8 & 255, b = rgb & 255;
  uint16_t h[2];
  h[0] = shade((uint16_t)((r >> 3) << 10 | (g >> 3) << 5 | (b >> 3)), cur_fade);
  r = r * 134 >> 8, g = g * 134 >> 8, b = b * 134 >> 8;
  h[1] = shade((uint16_t)((r >> 3) << 10 | (g >> 3) << 5 | (b >> 3)), cur_fade);
  plat_palette(h, 1, 2);
}
