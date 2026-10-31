/* Saves (save.h). The copy in pvz_saves.py is a comment line "#>name:base64" for each file (the way NumPlay and
 * its big games keep theirs). */
#include <stddef.h>
#include <string.h>
#include "game.h"
#include "plat.h"
#include "save.h"

#define SAVE_NAME "pvz.sav"
#define LEVEL_NAME "pvzlevel.sav"
#define COPY_NAME "pvz_saves.py"
#define MAGIC 0x315A5650u   /* "PVZ1" */
#define LEVEL_MAGIC 0x4C5A5650u   /* "PVZL" */

Progress progress;

static const char *const kept[] = {SAVE_NAME, LEVEL_NAME};
#define NKEPT (int)(sizeof kept / sizeof kept[0])
static const char copy_head[] =
  "# Plants vs. Zombies keeps a copy of your progress here, so\n"
  "# that reinstalling it doesn't erase it. If you delete this\n"
  "# file, the game writes it again: your progress stays.\n";
static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void copy_write(void) {
  uint32_t total = 1 + (sizeof copy_head - 1) + 1, len;
  int n = 0;
  for (int k = 0; k < NKEPT; k++)
    if (plat_load(kept[k], &len) && len) total += 2 + (uint32_t)strlen(kept[k]) + 1 + (len + 2) / 3 * 4 + 1, n++;
  plat_remove(COPY_NAME);
  if (!n) return;
  uint8_t *o = plat_reserve(COPY_NAME, total), *p = o;
  if (!o) return;
  *p++ = 0;   /* (the script's status byte: not imported) */
  memcpy(p, copy_head, sizeof copy_head - 1), p += sizeof copy_head - 1;
  for (int k = 0; k < NKEPT; k++) {
    const uint8_t *d = plat_load(kept[k], &len);
    if (!d || !len) continue;
    uint32_t nl = (uint32_t)strlen(kept[k]);
    *p++ = '#', *p++ = '>';
    memcpy(p, kept[k], nl), p += nl;
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

static const uint8_t *copy_line(int k, uint32_t *len) {
  uint32_t n, nl = (uint32_t)strlen(kept[k]);
  const uint8_t *c = plat_load(COPY_NAME, &n);
  if (!c) return NULL;
  for (uint32_t i = 0; i + nl + 3 <= n; i++)
    if (c[i] == '#' && c[i + 1] == '>' && !memcmp(c + i + 2, kept[k], nl) && c[i + 2 + nl] == ':') {
      const uint8_t *l = c + i + 3 + nl;
      uint32_t m = 0;
      while (l + m < c + n && l[m] != '\n' && l[m]) m++;
      *len = m;
      return l;
    }
  return NULL;
}

void save_restore_copy(void) {
  for (int k = 0; k < NKEPT; k++) {
    uint32_t have, len;
    if (plat_load(kept[k], &have)) continue;
    const uint8_t *l = copy_line(k, &len);
    if (!l || !len || len % 4) continue;
    uint32_t size = len / 4 * 3 - (l[len - 1] == '=') - (l[len - 2] == '=');
    uint8_t *out = plat_reserve(kept[k], size);
    if (!out) continue;
    l = copy_line(k, &len);
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
    if (ok && o == size) plat_reserved(kept[k], size);
    else plat_remove(kept[k]);
  }
}

/* ---------------------------------------------------------------- the progress */

void progress_load(void) {
  uint32_t len;
  const uint8_t *d = plat_load(SAVE_NAME, &len);
  memset(&progress, 0, sizeof progress);
  progress.level = 1, progress.slots = 6;
  if (d && len == 4 + sizeof progress) {
    uint32_t m;
    memcpy(&m, d, 4);
    if (m == MAGIC) memcpy(&progress, d + 4, sizeof progress);
  }
  if (progress.level < 1 || progress.level > N_LEVELS) progress.level = 1;
  if (progress.slots < 6 || progress.slots > 10) progress.slots = 6;
  if (progress.nlast > 10) progress.nlast = 0;
}

bool progress_save(void) {
  uint8_t buf[4 + sizeof progress];
  uint32_t m = MAGIC;
  memcpy(buf, &m, 4);
  memcpy(buf + 4, &progress, sizeof progress);
  bool ok = plat_save(SAVE_NAME, buf, sizeof buf);
  copy_write();
  return ok;
}

bool plant_unlocked(int kind) {
  if (progress.finished || kind == P_PEASHOOTER) return true;
  for (int l = 1; l < progress.level && l <= N_LEVELS; l++)
    if (level_defs[l - 1].reward == kind) return true;
  return false;
}
int level_world(int level) { return (level - 1) / 10 + 1; }
int level_stage(int level) { return (level - 1) % 10 + 1; }

/* ---------------------------------------------------------------- the level in progress */

/* the board before its arrays, then each array as long as it is: what is in flight (shots, effects) is left */
#define HEAD offsetof(Board, nplants)
bool board_save(void) {
  uint32_t n = 8 + HEAD + 7 * 4 + B.nplants * sizeof(Plant) + B.nzombies * sizeof(Zombie) + B.nsuns * sizeof(Sun) +
               B.ncoins * sizeof(Coin) + B.nrollers * sizeof(Roller) + (B.has_boss ? sizeof boss : 0);
  uint8_t *o = plat_reserve(LEVEL_NAME, n), *p = o;
  if (!o) {
    plat_remove(LEVEL_NAME);
    copy_write();
    return false;
  }
  uint32_t m = LEVEL_MAGIC;
  memcpy(p, &m, 4), p += 4;
  memcpy(p, &n, 4), p += 4;
  memcpy(p, &B, HEAD), p += HEAD;
  int32_t counts[7] = {B.nplants, B.nzombies, 0, B.nsuns, B.ncoins, 0, B.nrollers};
  memcpy(p, counts, sizeof counts), p += sizeof counts;
  memcpy(p, B.plants, B.nplants * sizeof(Plant)), p += B.nplants * sizeof(Plant);
  memcpy(p, B.zombies, B.nzombies * sizeof(Zombie)), p += B.nzombies * sizeof(Zombie);
  memcpy(p, B.suns, B.nsuns * sizeof(Sun)), p += B.nsuns * sizeof(Sun);
  memcpy(p, B.coins, B.ncoins * sizeof(Coin)), p += B.ncoins * sizeof(Coin);
  memcpy(p, B.rollers, B.nrollers * sizeof(Roller)), p += B.nrollers * sizeof(Roller);
  if (B.has_boss) memcpy(p, &boss, sizeof boss), p += sizeof boss;
  plat_reserved(LEVEL_NAME, n);
  copy_write();
  return true;
}

bool board_saved(void) {
  uint32_t len, m;
  const uint8_t *d = plat_load(LEVEL_NAME, &len);
  if (!d || len < 8 + HEAD) return false;
  memcpy(&m, d, 4);
  return m == LEVEL_MAGIC;
}

void board_forget(void) {
  if (plat_remove(LEVEL_NAME)) copy_write();
}

bool board_load(void) {
  uint32_t len, m, n;
  const uint8_t *d = plat_load(LEVEL_NAME, &len);
  if (!d || len < 8 + HEAD + 28) return false;
  memcpy(&m, d, 4), memcpy(&n, d + 4, 4);
  if (m != LEVEL_MAGIC || n != len) return false;
  const uint8_t *head = d + 8, *p = head + HEAD;
  uint8_t level = head[offsetof(Board, level)], has_boss = head[offsetof(Board, has_boss)];
  int32_t c[7];
  memcpy(c, p, sizeof c), p += sizeof c;
  if (c[0] < 0 || c[0] > MAX_PLANTS || c[1] < 0 || c[1] > MAX_ZOMBIES || c[3] < 0 || c[3] > MAX_SUNS || c[4] < 0 ||
      c[4] > MAX_COINS || c[6] < 0 || c[6] > MAX_ROLLERS)
    return false;
  uint32_t need = 8 + HEAD + 28 + c[0] * sizeof(Plant) + c[1] * sizeof(Zombie) + c[3] * sizeof(Sun) + c[4] * sizeof(Coin) +
                  c[6] * sizeof(Roller) + (has_boss ? sizeof boss : 0);
  if (need != len || level < 1 || level > N_LEVELS) return false;
  memcpy(&B, head, HEAD);
  B.nplants = c[0], B.nzombies = c[1], B.nshots = 0, B.nsuns = c[3], B.ncoins = c[4], B.nfx = 0, B.nrollers = c[6];
  memcpy(B.plants, p, c[0] * sizeof(Plant)), p += c[0] * sizeof(Plant);
  memcpy(B.zombies, p, c[1] * sizeof(Zombie)), p += c[1] * sizeof(Zombie);
  memcpy(B.suns, p, c[3] * sizeof(Sun)), p += c[3] * sizeof(Sun);
  memcpy(B.coins, p, c[4] * sizeof(Coin)), p += c[4] * sizeof(Coin);
  memcpy(B.rollers, p, c[6] * sizeof(Roller)), p += c[6] * sizeof(Roller);
  if (B.has_boss) memcpy(&boss, p, sizeof boss);
  /* (1-5 kept by an earlier version, standing still after the shovel lesson: the bowling goes on) */
  if (B.mode == MODE_BOWLING && B.tutorial != 4) B.started = 1;
  board_forget();
  return true;
}
