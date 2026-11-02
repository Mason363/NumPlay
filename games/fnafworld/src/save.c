/* FNAF World: the files (the game's Ini "fnafw1" to "fnafw3"), each fnafw1.sav to fnafw3.sav, and a copy of them in
 * a Python script: installing apps from the NumWorks website keeps only Python scripts, so the saves come back from
 * it when the files themselves are gone. Each is a comment line "#>name:base64" (the way NumPlay and its big games
 * keep theirs). */
#include <string.h>
#include "game.h"
#include "plat.h"

#define COPY_NAME "fnafw_saves.py"
#define MAGIC 0x57464E46u   /* "FNFW" */
#define VERSION 1

typedef struct {
  uint32_t magic;
  uint16_t version, size;
  File f;
  uint32_t check;
} Saved;

static const char *const kept[3] = {"fnafw1.sav", "fnafw2.sav", "fnafw3.sav"};
#define NKEPT 3
static const char copy_head[] = "# FNAF World's saves: keep this file\n";
static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static uint32_t checksum(const File *f) {
  const uint8_t *p = (const uint8_t *)f;
  uint32_t h = 2166136261u;
  for (unsigned i = 0; i < sizeof *f; i++) h = (h ^ p[i]) * 16777619u;
  return h;
}

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
    if (!l || len < 4 || len % 4) continue;
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

/* a file that holds together: every number in its range (a bad one is not loaded) */
static bool valid(File *f) {
  if (f->mode > 2 || f->diff > 2 || (f->started && (!f->mode || !f->diff))) return false;
  for (int k = 0; k < 8; k++)
    if (f->slot[k] > NCHAR) return false;
  for (int k = 0; k < 4; k++)
    if (f->chip[k] > NCHIP || f->bytes[k] > NBYTE) return false;
  if (f->x < 0 || f->x > 6000 || f->y < 0 || f->y > 4000) f->x = f->y = 0;
  if (f->armor != 0 && f->armor != 1 && f->armor != 2 && f->armor != 10) f->armor = 0;
  for (int c = 1; c <= NCHAR; c++) f->have[c] &= 1;
  return true;
}

bool load_file(int slot, File *f) {
  if (slot < 1 || slot > 3) return false;
  uint32_t len;
  const uint8_t *d = plat_load(kept[slot - 1], &len);
  if (!d || len != sizeof(Saved)) return false;
  Saved s;
  memcpy(&s, d, sizeof s);
  if (s.magic != MAGIC || s.version != VERSION || s.size != sizeof(File) || s.check != checksum(&s.f)) return false;
  if (!valid(&s.f)) return false;
  *f = s.f;
  return true;
}

void save_file(void) {
  if (g.slot < 1 || g.slot > 3 || !g.f.started) return;
  Saved s;
  memset(&s, 0, sizeof s);
  s.magic = MAGIC, s.version = VERSION, s.size = sizeof(File), s.f = g.f;
  s.check = checksum(&s.f);
  plat_save(kept[g.slot - 1], &s, sizeof s);
  copy_write();
  g.dirty = 0;
}

void save_new(int slot) {
  g.slot = (uint8_t)slot;
  save_file();
}

void save_summary(int slot, uint8_t *mode, uint8_t *diff, uint8_t *hours, uint8_t *minutes, bool *started) {
  File f;
  if (!load_file(slot, &f)) memset(&f, 0, sizeof f);
  *mode = f.mode, *diff = f.diff, *hours = f.hours, *minutes = f.minutes, *started = f.started;
}
