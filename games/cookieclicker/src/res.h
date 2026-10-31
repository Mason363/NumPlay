/* Cookie Clicker: data.bin (tools/pack.py), read where it lies in flash.
 *
 * Pictures (IM_*) are the part of them that isn't clear, a byte, half a byte or nothing a
 * pixel, with a palette of colours and opacities; most are packed, and unpacked by gfx.c
 * when drawn (tools/pictures.py); the icons are the game's icon sheet at 24 pixels; fonts (F_*)
 * are 4-bit glyphs; texts are the game's own, in a small markup (see text.c); and
 * the game's tables: buildings, tiers, upgrades, achievements, milks. */
#ifndef CC_RES_H
#define CC_RES_H
#include <stdbool.h>
#include <stdint.h>
#include "data.h"

extern const uint8_t cc_data[];

typedef struct {
  uint8_t w, h;
  uint8_t x, y, cw, ch;     /* the part kept (outside it, clear) */
  uint8_t bits, ncolors_1;  /* bits a pixel (FMT_*), | IM_PACKED, | IM_REF; colours - 1 */
  uint32_t px, pal;         /* offsets: the part's pixels (each row a whole number of bytes, the first pixel
                               lowest) or its packed data, the palette (colours RGB565, then their opacities) */
} ImageRec;
enum { FMT_P0 = 0, FMT_P4 = 4, FMT_P8 = 8, IM_PACKED = 0x80, IM_REF = 0x40 };   /* (P0: all of one colour) */

typedef struct {
  double price, cps;
  uint32_t name, single, plural, action, extra, extras, desc, pad;
  int16_t icon, column, pad1;
  uint16_t pad2;
} BuildingRec;

typedef struct {
  double price;
  uint32_t name;
  int16_t unlock, achiev_unlock, icon_row, special;
  uint16_t color, req;
} TierRec;

enum { POOL_NORMAL, POOL_COOKIE, POOL_PRESTIGE, POOL_TOGGLE, POOL_TECH, POOL_DEBUG };
enum { SEASON_NONE, SEASON_CHRISTMAS, SEASON_HALLOWEEN, SEASON_VALENTINES, SEASON_EASTER, SEASON_FOOLS };
typedef struct {
  double price;
  float order;
  uint32_t name, desc, quote;
  uint16_t icon;
  uint8_t pool, tier;
  int8_t building, building2;
  uint8_t kitten, flags;    /* flags: the season it belongs to, 0x80 vanilla */
  float power;
  uint16_t require;
  uint16_t parents[6];
  int16_t posx, posy;
  uint16_t pad;
  double unlock_cookies;
} UpgradeRec;

enum { APOOL_NORMAL, APOOL_SHADOW, APOOL_DUNGEON };
typedef struct {
  uint32_t name, desc, quote;
  float order;
  uint16_t icon;
  uint8_t pool;
  int8_t building;
} AchievRec;

typedef struct {
  uint32_t name;
  uint16_t icon, pad;
} MilkRec;

/* what ties a building to its upgrades and achievements (by tier: data.h's TIER_*; 0xFFFF none) */
#define NONE 0xFFFF
typedef struct {
  uint16_t tiered[N_TIERS], tiered_ach[N_TIERS];
  uint16_t synergies[6];
  uint16_t fortune, grandma, unshackle, level10;   /* level10: the achievement for its level 10 */
  uint16_t prod_ach[4];
  uint16_t pad2[2];
  double prod_pow[3];
} BuildingLinks;
typedef struct {
  double threshold;
  uint16_t id, pad;
  uint32_t pad2;
} Threshold;
typedef struct {
  double cookies;
  uint16_t up, ach, require_up, require_ach;
  uint8_t season, pad[7];
} UnlockAtRec;

_Static_assert(sizeof(BuildingLinks) == 128, "BuildingLinks");
_Static_assert(sizeof(Threshold) == 16, "Threshold");
_Static_assert(sizeof(UnlockAtRec) == 24, "UnlockAtRec");
_Static_assert(sizeof(ImageRec) == 16, "ImageRec");
_Static_assert(sizeof(BuildingRec) == 56, "BuildingRec");
_Static_assert(sizeof(TierRec) == 24, "TierRec");
_Static_assert(sizeof(UpgradeRec) == 64, "UpgradeRec");
_Static_assert(sizeof(AchievRec) == 20, "AchievRec");
_Static_assert(sizeof(MilkRec) == 8, "MilkRec");

#define RES(type, off) ((const type *)(const void *)(cc_data + (off)))
static inline const ImageRec *image_rec(int i) { return RES(ImageRec, OFF_IMAGES) + i; }
static inline const BuildingRec *building_rec(int i) { return RES(BuildingRec, OFF_BUILDINGS) + i; }
static inline const TierRec *tier_rec(int i) { return RES(TierRec, OFF_TIERS) + i; }
static inline const UpgradeRec *upgrade_rec(int i) { return RES(UpgradeRec, OFF_UPGRADES) + i; }
static inline const AchievRec *achiev_rec(int i) { return RES(AchievRec, OFF_ACHIEVS) + i; }
static inline const MilkRec *milk_rec(int i) { return RES(MilkRec, OFF_MILKS) + i; }
static inline const BuildingLinks *building_links(int i) { return RES(BuildingLinks, OFF_BUILDING_LINKS) + i; }
/* a list of ids (LIST(COOKIE_UPGRADES)[i], i < N_COOKIE_UPGRADES) */
#define LIST(name) RES(uint16_t, OFF_##name)
static inline const Threshold *bank_achiev(int i) { return RES(Threshold, OFF_BANK_ACHIEVS) + i; }
static inline const Threshold *cps_achiev(int i) { return RES(Threshold, OFF_CPS_ACHIEVS) + i; }
static inline const UnlockAtRec *unlock_at(int i) { return RES(UnlockAtRec, OFF_UNLOCK_AT) + i; }
static inline const char *res_text(uint32_t off) { return (const char *)cc_data + OFF_TEXT + off; }
/* the picture of icon (x, y) of the game's sheet, -1 if it is empty */
static inline int icon_image(int x, int y) {
  if ((unsigned)x >= ICON_COLS || (unsigned)y >= ICON_ROWS) return -1;
  uint16_t i = RES(uint16_t, OFF_ICONS)[y * ICON_COLS + x];
  return i == 0xFFFF ? -1 : i;
}
static inline int icon_index_image(int index) { return index == 0xFFFF ? -1 : icon_image(index % ICON_COLS, index / ICON_COLS); }
#define ICON(x, y) ((y) * ICON_COLS + (x))
#endif
