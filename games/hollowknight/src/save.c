/* GameManager.SaveGame and LoadGame: PlayerData in a file of the calculator's, one a slot. The file: a header with a
 * checksum, PlayerData's saved part, then the objects' states by name (tools/ents.py: PHASH), so that a save keeps
 * meaning the same objects whatever the data's version. A file that does not check out is never loaded. */
#pragma GCC optimize("Os")   /* (its code small: not where a frame's time goes) */
#include "game.h"

#define SAVE_MAGIC 0x56534B48u   /* "HKSV" */
#define SAVE_VERSION 2   /* (1: PlayerData up to PD_SAVED_V1, loaded with the rest cleared) */

typedef struct {
  uint32_t magic, version, size, crc;   /* (size, crc: of what follows the header) */
} SaveHead;

static int cur_slot;

static uint32_t crc32(const uint8_t *p, uint32_t n) {
  uint32_t c = 0xFFFFFFFFu;
  while (n--) {
    c ^= *p++;
    for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
  }
  return ~c;
}

static void slot_name(int slot, char *out) {
  memcpy(out, "hk1.sav", 8);
  out[2] = (char)('1' + slot);
}

void save_select(int slot) { cur_slot = slot < 0 || slot >= SAVE_SLOTS ? 0 : slot; }

static const uint8_t *phash(uint32_t *n) {
  const uint8_t *s = section(SEC_PHASH);
  *n = rd32(s);
  return s + 4;
}

static void save_copy(void);

bool save_game(void) {
  /* (built in the decoders' scratch memory: no decoding goes on meanwhile) */
  uint8_t *buf = (uint8_t *)g_scratch;
  uint32_t cap = SCRATCH_N * 2;
  SaveHead h = {SAVE_MAGIC, SAVE_VERSION, 0, 0};
  uint32_t at = sizeof h;
  memcpy(buf + at, &g_pd, PD_SAVED);
  at += PD_SAVED;
  uint32_t nbits, count = 0;
  const uint8_t *names = phash(&nbits);
  uint32_t count_at = at;
  at += 4;
  for (uint32_t i = 0; i < nbits && i < MAX_PERSIST; i++) {
    if (!(g_pd.persist[i >> 3] >> (i & 7) & 1)) continue;
    if (at + 4 > cap) return false;
    memcpy(buf + at, names + 4 * i, 4);
    at += 4, count++;
  }
  memcpy(buf + count_at, &count, 4);
  h.size = at - sizeof h;
  h.crc = crc32(buf + sizeof h, h.size);
  memcpy(buf, &h, sizeof h);
  char name[8];
  slot_name(cur_slot, name);
  if (!plat_save(name, buf, at)) return false;
  save_copy();
  return true;
}

/* a save's bytes, checked: its PlayerData part (*pd_size long: its version's), its states' names */
static const uint8_t *whole(const uint8_t *p, uint32_t len, uint32_t *count, uint32_t *pd_size) {
  SaveHead h;
  if (!p || len < sizeof h) return NULL;
  memcpy(&h, p, sizeof h);
  uint32_t pd = h.version == SAVE_VERSION ? PD_SAVED : h.version == 1 ? PD_SAVED_V1 : 0;
  if (h.magic != SAVE_MAGIC || !pd || len < sizeof h + pd + 4 || h.size != len - sizeof h ||
      h.crc != crc32(p + sizeof h, h.size))
    return NULL;
  memcpy(count, p + sizeof h + pd, 4);
  if (sizeof h + pd + 4 + 4ull * *count != len) return NULL;
  *pd_size = pd;
  return p + sizeof h;
}

/* a slot's file, checked */
static const uint8_t *checked(int slot, uint32_t *count, uint32_t *pd_size) {
  char name[8];
  slot_name(slot, name);
  uint32_t len;
  const uint8_t *p = plat_load(name, &len);
  return whole(p, len, count, pd_size);
}

/* The copy: installing apps from the NumWorks website keeps only Python scripts, so the saves are also kept in one,
 * a comment line "#>hk1.sav:base64" a slot (the way NumPlay keeps its games' saves), and come back from it when a
 * save itself is gone. */
#define COPY_NAME "hollowknight_saves.py"
static const char copy_head[] = "# Hollow Knight's saves: keep this file\n";
static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void save_copy(void) {
  /* (its size first, then written in place: the files read again, as writing it may have moved them) */
  uint32_t total = 1 + (sizeof copy_head - 1) + 1, len, count, pd;
  char name[8];
  for (int s = 0; s < SAVE_SLOTS; s++) {
    slot_name(s, name);
    if (whole(plat_load(name, &len), len, &count, &pd)) total += 2 + 7 + 1 + (len + 2) / 3 * 4 + 1;
  }
  uint8_t *o = plat_reserve(COPY_NAME, total), *p = o;
  if (!o) return;
  *p++ = 0;   /* (the script's status byte: not imported) */
  memcpy(p, copy_head, sizeof copy_head - 1), p += sizeof copy_head - 1;
  for (int s = 0; s < SAVE_SLOTS; s++) {
    slot_name(s, name);
    const uint8_t *d = plat_load(name, &len);
    if (!whole(d, len, &count, &pd)) continue;
    *p++ = '#', *p++ = '>';
    memcpy(p, name, 7), p += 7;
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
  }
  *p++ = 0;
  plat_reserved(COPY_NAME, (uint32_t)(p - o));
}

static int b64_value(uint8_t c) {
  for (int i = 0; i < 64; i++)
    if ((uint8_t)b64[i] == c) return i;
  return -1;
}

/* the saves from the copy, those gone or broken (after the app was installed again) */
void save_restore(void) {
  for (int s = 0; s < SAVE_SLOTS; s++) {
    uint32_t n, count, pd;
    if (checked(s, &count, &pd)) continue;
    char tag[11] = "#>hk1.sav:";
    tag[4] = (char)('1' + s);
    const uint8_t *c = plat_load(COPY_NAME, &n), *l = NULL;
    if (!c) return;
    for (uint32_t i = 0; i + 10 <= n && !l; i++)
      if (!memcmp(c + i, tag, 10)) l = c + i + 10;
    if (!l) continue;
    uint32_t len = 0, o = 0;
    while (l + len < c + n && l[len] != '\n' && l[len]) len++;
    if (!len || len % 4) continue;
    uint32_t size = len / 4 * 3 - (l[len - 1] == '=') - (l[len - 2] == '=');
    uint8_t *out = (uint8_t *)g_scratch;
    if (size > SCRATCH_N * 2) continue;
    bool ok = true;
    for (uint32_t i = 0; i < len && ok; i += 4) {
      int v0 = l[i] == '=' ? 0 : b64_value(l[i]), v1 = l[i + 1] == '=' ? 0 : b64_value(l[i + 1]);
      int v2 = l[i + 2] == '=' ? 0 : b64_value(l[i + 2]), v3 = l[i + 3] == '=' ? 0 : b64_value(l[i + 3]);
      if (v0 < 0 || v1 < 0 || v2 < 0 || v3 < 0) ok = false;
      uint32_t w = (uint32_t)(v0 << 18 | v1 << 12 | v2 << 6 | v3);
      if (o < size) out[o++] = (uint8_t)(w >> 16);
      if (o < size) out[o++] = (uint8_t)(w >> 8);
      if (o < size) out[o++] = (uint8_t)w;
    }
    char name[8];
    slot_name(s, name);
    if (ok && o == size && whole(out, size, &count, &pd)) plat_save(name, out, size);
  }
}

int save_stats(int slot, SaveStats *st) {
  char name[8];
  slot_name(slot, name);
  uint32_t len, count, pd;
  if (!plat_load(name, &len)) return 0;
  const uint8_t *p = checked(slot, &count, &pd);
  if (!p) return -1;
  /* (its PlayerData's fields, read where they are) */
  memcpy(&st->max_health, p + offsetof(PlayerData, max_health), sizeof st->max_health);
  memcpy(&st->mp_reserve_max, p + offsetof(PlayerData, mp_reserve_max), sizeof st->mp_reserve_max);
  memcpy(&st->geo, p + offsetof(PlayerData, geo), sizeof st->geo);
  memcpy(&st->play_time, p + offsetof(PlayerData, play_time), sizeof st->play_time);
  st->zone = p[offsetof(PlayerData, map_zone)];
  return 1;
}

bool save_clear(int slot) {
  char name[8];
  slot_name(slot, name);
  bool ok = plat_remove(name);
  save_copy();
  return ok;
}

bool save_exists(int slot) {
  uint32_t count, pd;
  return checked(slot, &count, &pd) != NULL;
}

bool save_load(int slot) {
  uint32_t count, pd;
  const uint8_t *p = checked(slot, &count, &pd);
  if (!p) return false;
  save_select(slot);
  memset(&g_pd, 0, sizeof g_pd);
  memcpy(&g_pd, p, pd);
  /* (counts kept within their arrays whatever the file says) */
  for (int c = 0; c < 4; c++)
    if (g_pd.markers_placed[c] > 6) g_pd.markers_placed[c] = 6;
  if (g_pd.map_key_pref > 2) g_pd.map_key_pref = 0;
  /* (names kept within their bounds whatever the file says) */
  g_pd.respawn_scene[SCENE_NAME - 1] = g_pd.respawn_marker[SCENE_NAME - 1] = g_pd.shade_scene[SCENE_NAME - 1] = 0;
  uint32_t nbits;
  const uint8_t *names = phash(&nbits);
  const uint8_t *q = p + pd + 4;
  for (uint32_t k = 0; k < count; k++, q += 4) {
    uint32_t want = rd32(q);
    for (uint32_t i = 0; i < nbits && i < MAX_PERSIST; i++)
      if (rd32(names + 4 * i) == want) {
        g_pd.persist[i >> 3] |= (uint8_t)(1 << (i & 7));
        break;
      }
  }
  return true;
}

void save_set_respawn_kind(const char *marker, bool facing_right, int type) {
  /* (SetPlayerDataString respawnMarkerName, respawnScene; respawnType 1: a bench, 0: face down) */
  strncpy(g_pd.respawn_marker, marker, SCENE_NAME - 1);
  strncpy(g_pd.respawn_scene, room_name(g_room.id), SCENE_NAME - 1);
  g_pd.respawn_marker[SCENE_NAME - 1] = g_pd.respawn_scene[SCENE_NAME - 1] = 0;
  g_pd.respawn_type = (uint8_t)type;
  g_pd.respawn_facing_right = facing_right;
}

void save_set_respawn(const char *marker, bool facing_right) { save_set_respawn_kind(marker, facing_right, 1); }
