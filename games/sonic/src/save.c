/* Saves: where the game was left (the act, lives, continues, emeralds, score; Home, On/Off and Quit
 * game keep it, and so does each act as it starts), and whether the keys screen was seen, in
 * sonic.sav; and a copy in a Python script, sonic_saves.py: installing apps from the NumWorks
 * website keeps only Python scripts, so the save comes back from it when the file itself is gone.
 * The copy is a comment line "#>sonic.sav:base64" (the way NumPlay and its big games keep theirs). */
#include "s1.h"
#include "game.h"
#include "plat.h"

#define SAVE_NAME "sonic.sav"
#define COPY_NAME "sonic_saves.py"
#define MAGIC 0x56533153u   /* "S1SV" */

typedef struct {
  uint32_t magic;
  uint8_t version, seen_keys, has_game, zone, act, lives, continues, emeralds;
  uint8_t emldlist[6], lastspecial, pad;
  uint32_t score, scorelife;
  uint32_t frames;    /* time played */
} SaveFile;

static SaveFile sv;
static bool loaded;
static uint32_t counted;   /* the frames already added to sv.frames */

static const char copy_head[] = "# Sonic the Hedgehog's saves: keep this file\n";
static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void copy_write(const uint8_t *d, uint32_t len) {
  uint32_t total = 1 + (sizeof copy_head - 1) + 2 + (uint32_t)strlen(SAVE_NAME) + 1 + (len + 2) / 3 * 4 + 1 + 1;
  plat_remove(COPY_NAME);
  uint8_t *o = plat_reserve(COPY_NAME, total), *p = o;
  if (!o) return;
  *p++ = 0;   /* (the script's status byte: not imported) */
  memcpy(p, copy_head, sizeof copy_head - 1), p += sizeof copy_head - 1;
  *p++ = '#', *p++ = '>';
  memcpy(p, SAVE_NAME, strlen(SAVE_NAME)), p += strlen(SAVE_NAME);
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

/* the save back from the script, when the file is gone */
void save_restore_copy(void) {
  uint32_t n, have;
  if (plat_load(SAVE_NAME, &have)) return;
  const uint8_t *c = plat_load(COPY_NAME, &n);
  if (!c) return;
  static const char tag[] = "#>" SAVE_NAME ":";
  for (uint32_t i = 0; i + sizeof tag - 1 <= n; i++) {
    if (memcmp(c + i, tag, sizeof tag - 1)) continue;
    const uint8_t *l = c + i + sizeof tag - 1;
    uint32_t m = 0;
    while (l + m < c + n && l[m] != '\n' && l[m]) m++;
    if (!m || m % 4 || m / 4 * 3 < sizeof(SaveFile)) return;
    uint8_t out[sizeof(SaveFile) + 3];
    uint32_t o = 0;
    for (uint32_t k = 0; k < m && o < sizeof out; k += 4) {
      int v0 = l[k] == '=' ? 0 : b64_value(l[k]), v1 = l[k + 1] == '=' ? 0 : b64_value(l[k + 1]);
      int v2 = l[k + 2] == '=' ? 0 : b64_value(l[k + 2]), v3 = l[k + 3] == '=' ? 0 : b64_value(l[k + 3]);
      if (v0 < 0 || v1 < 0 || v2 < 0 || v3 < 0) return;
      uint32_t wd = (uint32_t)(v0 << 18 | v1 << 12 | v2 << 6 | v3);
      if (o < sizeof out) out[o++] = (uint8_t)(wd >> 16);
      if (o < sizeof out) out[o++] = (uint8_t)(wd >> 8);
      if (o < sizeof out) out[o++] = (uint8_t)wd;
    }
    SaveFile f;
    memcpy(&f, out, sizeof f);
    if (f.magic == MAGIC) plat_save(SAVE_NAME, &f, sizeof f);
    return;
  }
}

static void load(void) {
  if (loaded) return;
  loaded = true;
  uint32_t len;
  const uint8_t *d = plat_load(SAVE_NAME, &len);
  memset(&sv, 0, sizeof sv);
  if (d && len == sizeof sv) memcpy(&sv, d, sizeof sv);
  if (sv.magic != MAGIC) memset(&sv, 0, sizeof sv);
  sv.magic = MAGIC, sv.version = 1;
}

static void write(void) {
  plat_save(SAVE_NAME, &sv, sizeof sv);
  copy_write((const uint8_t *)&sv, sizeof sv);
}

bool save_seen_keys(void) {
  load();
  return sv.seen_keys;
}
void save_set_seen_keys(void) {
  load();
  sv.seen_keys = 1;
  write();
}

bool save_exists(void) {
  load();
  return sv.has_game;
}
void save_peek(int *zone, int *act, int *lives) {
  load();
  *zone = sv.zone, *act = sv.act, *lives = sv.lives;
}

/* the game as it is: in a level (or its special stage), the act it is at */
void save_progress(void) {
  load();
  if (V.f_demo || (V.gamemode != id_Level && V.gamemode != id_Special && V.gamemode != id_Continue)) return;
  if (V.gamemode == id_Level && !V.lives) return;
  sv.has_game = 1;
  sv.zone = V.zone, sv.act = V.act;
  sv.lives = V.lives ? V.lives : 1;
  sv.continues = V.continues;
  if (V.gamemode == id_Continue) sv.lives = 3, sv.continues = V.continues ? V.continues - 1 : 0;
  sv.emeralds = V.emeralds;
  memcpy(sv.emldlist, V.emldlist, 6);
  sv.lastspecial = V.lastspecial;
  sv.score = V.score;
  sv.scorelife = V.scorelife;
  sv.frames += V.vblank_count - counted;
  counted = V.vblank_count;
  write();
}

bool save_load_progress(void) {
  load();
  if (!sv.has_game) return false;
  V.zone = sv.zone, V.act = sv.act;
  V.lives = sv.lives;
  V.continues = sv.continues;
  V.emeralds = sv.emeralds;
  memcpy(V.emldlist, sv.emldlist, 6);
  V.lastspecial = sv.lastspecial;
  V.score = sv.score;
  V.scorelife = sv.scorelife;
  return true;
}

/* the game over, or the game finished: nothing to continue */
void save_clear_progress(void) {
  load();
  if (!sv.has_game) return;
  sv.has_game = 0;
  write();
}

void game_save_on_quit(void) { save_progress(); }
