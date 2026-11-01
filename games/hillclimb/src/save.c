/* Saves: hillclimb.sav (coins, vehicles, stages, upgrades, bests), and a copy of it in a Python script:
 * installing apps from the NumWorks website keeps only Python scripts, so the save comes back from it when the
 * file itself is gone. The copy is a comment line "#>name:base64" (the way NumPlay and its big games keep theirs). */
#include <stddef.h>
#include <string.h>
#include "game.h"
#include "plat.h"

#define SAVE_NAME "hillclimb.sav"
#define COPY_NAME "hillclimb_saves.py"
#define MAGIC 0x31524348u   /* "HCR1" (all versions; the size and the version tell them apart) */

Save sv;

typedef struct {
  uint32_t magic, size;
  Save s;
} SaveFile;

/* versions 1 and 2: 8 vehicles, 10 stages (version 1 without the stages' levels) */
#define OLD_V 8
#define OLD_S 10
typedef struct {
  uint32_t coins;
  uint16_t vehicles, stages;
  uint8_t level[OLD_V][4];
  uint16_t best[OLD_S][OLD_V];
  uint8_t vehicle, stage, seen_help, pad;
  uint32_t runs, meters, flips, best_air_ms, coins_earned;
  uint8_t stage_level[OLD_S];
  uint8_t pad2[2];
} SaveV2;
#define SAVE_V1_SIZE 224
_Static_assert(offsetof(SaveV2, stage_level) == SAVE_V1_SIZE, "version 1 is version 2 without the stages' levels");
_Static_assert(sizeof(SaveV2) == 236, "version 2's size");
/* (tests/check.py and NumPlay's tests/test_games.py read these) */
_Static_assert(sizeof(Save) == 444 && offsetof(Save, best) == 64 && offsetof(Save, runs) == 408 &&
               offsetof(Save, stage_level) == 428, "version 3's layout");
/* the upgrades' last levels and prices before version 3 (they were not the game's: version 3 has the game's own) */
static const uint8_t OLD_MAX[OLD_V][4] = {{13, 14, 16, 10}, {12, 14, 16, 10}, {13, 14, 15, 10}, {13, 14, 16, 10},
                                         {12, 14, 16, 12}, {14, 14, 17, 10}, {15, 15, 15, 20}, {15, 15, 15, 20}};
static const uint16_t OLD_COST[OLD_V][4] = {{1000, 1000, 1000, 1000}, {1500, 1000, 1000, 3000}, {1500, 1200, 1200, 1500},
                                           {1500, 1200, 1200, 1200}, {1500, 1500, 1500, 2500}, {2000, 2000, 2000, 3000},
                                           {3000, 3000, 3000, 3000}, {3000, 3000, 3000, 3000}};

static const char copy_head[] = "# Hill Climb Racing's saves: keep this file\n";
static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void copy_write(void) {
  uint32_t len;
  const uint8_t *d = plat_load(SAVE_NAME, &len);
  if (!d || !len) return;
  uint32_t nl = (uint32_t)strlen(SAVE_NAME), total = 1 + (sizeof copy_head - 1) + 2 + nl + 1 + (len + 2) / 3 * 4 + 2;
  plat_remove(COPY_NAME);
  uint8_t *o = plat_reserve(COPY_NAME, total), *p = o;
  if (!o) return;
  d = plat_load(SAVE_NAME, &len);   /* (making the copy may have moved the file) */
  if (!d) return;
  *p++ = 0;   /* (the script's status byte: not imported) */
  memcpy(p, copy_head, sizeof copy_head - 1), p += sizeof copy_head - 1;
  *p++ = '#', *p++ = '>';
  memcpy(p, SAVE_NAME, nl), p += nl;
  *p++ = ':';
  for (uint32_t i = 0; i < len; i += 3, p += 4) {
    /* (written out: GCC 15.2 for the calculator got a loop over the four wrong, see NumPlay's
     * launcher/src/storage.c) */
    uint32_t left = len - i, v = (uint32_t)d[i] << 16 | (left > 1 ? d[i + 1] << 8 : 0) | (left > 2 ? d[i + 2] : 0);
    p[0] = (uint8_t)b64[v >> 18 & 63];
    p[1] = (uint8_t)b64[v >> 12 & 63];
    p[2] = left > 1 ? (uint8_t)b64[v >> 6 & 63] : '=';
    p[3] = left > 2 ? (uint8_t)b64[v & 63] : '=';
  }
  *p++ = '\n';
  *p++ = 0;
  plat_reserved(COPY_NAME, (uint32_t)(p - o));
}

static int b64_value(uint8_t c) {
  for (int i = 0; i < 64; i++)
    if ((uint8_t)b64[i] == c) return i;
  return -1;
}

static const uint8_t *copy_line(uint32_t *len) {
  uint32_t n, nl = (uint32_t)strlen(SAVE_NAME);
  const uint8_t *c = plat_load(COPY_NAME, &n);
  if (!c) return NULL;
  for (uint32_t i = 0; i + nl + 3 <= n; i++)
    if (c[i] == '#' && c[i + 1] == '>' && !memcmp(c + i + 2, SAVE_NAME, nl) && c[i + 2 + nl] == ':') {
      const uint8_t *l = c + i + 3 + nl;
      uint32_t m = 0;
      while (l + m < c + n && l[m] != '\n' && l[m]) m++;
      *len = m;
      return l;
    }
  return NULL;
}

void save_restore_copy(void) {
  uint32_t have, len;
  if (plat_load(SAVE_NAME, &have)) return;
  const uint8_t *l = copy_line(&len);
  if (!l || !len || len % 4) return;
  uint32_t size = len / 4 * 3 - (l[len - 1] == '=') - (l[len - 2] == '=');
  uint8_t *out = plat_reserve(SAVE_NAME, size);
  if (!out) return;
  l = copy_line(&len);
  bool ok = l != NULL && len / 4 * 3 - (l[len - 1] == '=') - (l[len - 2] == '=') == size;
  uint32_t o = 0;
  for (uint32_t i = 0; ok && i < len; i += 4) {
    int v0 = l[i] == '=' ? 0 : b64_value(l[i]), v1 = l[i + 1] == '=' ? 0 : b64_value(l[i + 1]);
    int v2 = l[i + 2] == '=' ? 0 : b64_value(l[i + 2]), v3 = l[i + 3] == '=' ? 0 : b64_value(l[i + 3]);
    if (v0 < 0 || v1 < 0 || v2 < 0 || v3 < 0) ok = false;
    uint32_t wd = (uint32_t)(v0 << 18 | v1 << 12 | v2 << 6 | v3);
    if (o < size) out[o++] = (uint8_t)(wd >> 16);
    if (o < size) out[o++] = (uint8_t)(wd >> 8);
    if (o < size) out[o++] = (uint8_t)wd;
  }
  if (ok && o == size) plat_reserved(SAVE_NAME, size);
  else plat_remove(SAVE_NAME);
}

static void defaults(void) {
  memset(&sv, 0, sizeof sv);
  sv.version = SAVE_VERSION;
  sv.vehicles = 1, sv.stages = 1;
  for (int v = 0; v < NVEHICLES; v++)
    for (int u = 0; u < 4; u++) sv.level[v][u] = 1;
}

/* a version 1 or 2 save into version 3: everything kept; an upgrade above its game's last level is brought down to it
 * and what its levels above cost comes back as coins */
static void from_v2(const SaveV2 *o, bool v1) {
  sv.coins = o->coins;
  sv.vehicles = o->vehicles & ((1 << OLD_V) - 1), sv.stages = o->stages & ((1 << OLD_S) - 1);
  uint64_t refund = 0;
  for (int v = 0; v < OLD_V; v++)
    for (int u = 0; u < 4; u++) {
      int l = o->level[v][u];
      if (l < 1 || l > OLD_MAX[v][u]) l = 1;
      for (; l > vehicles[v].max[u]; l--) refund += (uint64_t)OLD_COST[v][u] * (uint64_t)(l * l);
      sv.level[v][u] = (uint8_t)l;
    }
  uint64_t c = sv.coins + refund;
  sv.coins = c > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)c;
  for (int st = 0; st < OLD_S; st++)
    for (int v = 0; v < OLD_V; v++) sv.best[st][v] = o->best[st][v];
  sv.vehicle = o->vehicle, sv.stage = o->stage, sv.seen_help = o->seen_help;
  sv.runs = o->runs, sv.meters = o->meters, sv.flips = o->flips, sv.best_air_ms = o->best_air_ms;
  sv.coins_earned = o->coins_earned;
  if (!v1)
    for (int st = 0; st < OLD_S; st++) sv.stage_level[st] = o->stage_level[st];
}

void save_load(void) {
  defaults();
  uint32_t len;
  const uint8_t *d = plat_load(SAVE_NAME, &len);
  if (!d || len < 8) return;
  uint32_t magic, size;
  memcpy(&magic, d, 4);
  memcpy(&size, d + 4, 4);
  if (magic != MAGIC || size != len - 8) return;
  if (size == sizeof(Save)) {
    Save s;
    memcpy(&s, d + 8, sizeof s);
    if (s.version != SAVE_VERSION) return;
    sv = s;
  } else if (size == sizeof(SaveV2) || size == SAVE_V1_SIZE) {
    SaveV2 o;
    memset(&o, 0, sizeof o);
    memcpy(&o, d + 8, size);
    from_v2(&o, size == SAVE_V1_SIZE);
  } else {
    return;
  }
  /* (whatever the file says, the first vehicle and stage are owned and levels stay in range) */
  sv.version = SAVE_VERSION;
  sv.vehicles &= (1 << NVEHICLES) - 1, sv.stages &= (1 << NSTAGES) - 1;
  sv.vehicles |= 1, sv.stages |= 1;
  if (sv.vehicle >= NVEHICLES || !(sv.vehicles >> sv.vehicle & 1)) sv.vehicle = 0;
  if (sv.stage >= NSTAGES || !(sv.stages >> sv.stage & 1)) sv.stage = 0;
  for (int v = 0; v < NVEHICLES; v++)
    for (int u = 0; u < 4; u++)
      if (sv.level[v][u] < 1 || sv.level[v][u] > vehicles[v].max[u]) sv.level[v][u] = 1;
}

bool save_write(void) {
  SaveFile f;
  memset(&f, 0, sizeof f);
  sv.version = SAVE_VERSION;
  f.magic = MAGIC, f.size = sizeof(Save), f.s = sv;
  bool ok = plat_save(SAVE_NAME, &f, sizeof f);
  copy_write();
  return ok;
}
