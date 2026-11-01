/* Saves (save.h): the game's SAVE (undertale.sav), the moment kept when Home quits (utresume.sav, read once
 * on the next start), and a copy of both in a Python script: installing apps from the NumWorks website keeps
 * only Python scripts, so the saves come back from it when the files themselves are gone. Each is a
 * comment line "#>name:base64" (the way NumPlay and its big games keep theirs). */
#include <string.h>
#include "game.h"
#include "menu.h"
#include "plat.h"
#include "save.h"
#include "text.h"
#include "world.h"

#define SAVE_NAME "undertale.sav"
#define RESUME_NAME "utresume.sav"
#define COPY_NAME "undertale_saves.py"
#define CONFIG_NAME "utconfig.sav"
#define MAGIC 0x31305455u   /* "UT01" */
#define CONFIG_MAGIC 0x31435455u   /* "UTC1" */

typedef struct {
  uint32_t magic;
  uint32_t size;
  Save s;
} SaveFile;

static const char *const kept[] = {SAVE_NAME, RESUME_NAME, CONFIG_NAME};
#define NKEPT (int)(sizeof kept / sizeof kept[0])
static const char copy_head[] = "# Undertale's saves: keep this file\n";
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

static bool write_file(const char *name, const Save *s) {
  SaveFile f;
  memset(&f, 0, sizeof f);
  f.magic = MAGIC, f.size = sizeof(Save), f.s = *s;
  bool ok = plat_save(name, &f, sizeof f);
  copy_write();
  return ok;
}

static bool read_file(const char *name, Save *s) {
  uint32_t len;
  const uint8_t *d = plat_load(name, &len);
  if (!d || len != sizeof(SaveFile)) return false;
  SaveFile f;
  memcpy(&f, d, sizeof f);
  if (f.magic != MAGIC || f.size != sizeof(Save)) return false;
  *s = f.s;
  return true;
}

bool save_write(void) { return write_file(SAVE_NAME, &sv); }
bool save_write_auto(void) { return write_file(SAVE_NAME, &sv); }
bool save_exists(void) {
  Save s;
  return read_file(SAVE_NAME, &s);
}
bool save_peek(Save *s) { return read_file(SAVE_NAME, s); }

bool save_load(void) {
  Save s;
  if (!read_file(SAVE_NAME, &s)) return false;
  sv = s;
  memcpy(g_name, sv.name, sizeof g_name);
  sv.hp = sv.maxhp;
  g_mode = MODE_WORLD;
  world_enter(sv.room, sv.x, sv.y, DIR_DOWN);
  return true;
}

/* Home: the moment kept as it is, room and place; read back once */
void save_resume_write(void) {
  if (g_mode != MODE_WORLD && g_mode != MODE_MENU && g_mode != MODE_SAVE) {
    /* (in a battle: kept as it was before it) */
  }
  Save s = sv;
  s.room = (int16_t)g_room, s.x = pl.x, s.y = pl.y;
  write_file(RESUME_NAME, &s);
}

bool save_resume_load(void) {
  Save s;
  if (!read_file(RESUME_NAME, &s)) return false;
  plat_remove(RESUME_NAME);
  copy_write();
  sv = s;
  memcpy(g_name, sv.name, sizeof g_name);
  g_mode = MODE_WORLD;
  world_enter(sv.room, sv.x, sv.y, DIR_DOWN);
  return true;
}

/* Settings: kept apart from the SAVE (a new game keeps them), as the game keeps its own */
uint8_t g_walk_fast;
typedef struct {
  uint32_t magic;
  uint8_t walk_fast, unused[3];
} ConfigFile;

void config_load(void) {
  uint32_t len;
  const uint8_t *d = plat_load(CONFIG_NAME, &len);
  ConfigFile f;
  if (!d || len != sizeof f) return;
  memcpy(&f, d, sizeof f);
  if (f.magic == CONFIG_MAGIC) g_walk_fast = f.walk_fast ? 1 : 0;
}

void config_save(void) {
  ConfigFile f;
  memset(&f, 0, sizeof f);
  f.magic = CONFIG_MAGIC, f.walk_fast = g_walk_fast;
  plat_save(CONFIG_NAME, &f, sizeof f);
  copy_write();
}
