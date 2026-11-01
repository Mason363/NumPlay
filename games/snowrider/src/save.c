/* Snow Rider 3D: the save, snowrider.sav. One checked format ("SNR1", a version, its size and an FNV-1a sum of the
 * rest); anything damaged, cut short or from elsewhere is not read: the copy in snowrider_saves.py is tried, then a
 * fresh start. The copy is the file in base 64 in a comment line of a Python script, the only kind of file the
 * NumWorks installer keeps, so installing the app again doesn't erase the progress. */
#include <stddef.h>
#include <string.h>
#include "save.h"

#define SAVE_NAME "snowrider.sav"
#define COPY_NAME "snowrider_saves.py"

static uint32_t fnv(const void *data, uint32_t n) {
  const uint8_t *p = data;
  uint32_t h = 2166136261u;
  for (uint32_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
  return h;
}

void sr_fresh(SaveFile *f) {
  memset(f, 0, sizeof *f);
  memcpy(f->magic, "SNR1", 4);
  f->version = SAVE_VERSION, f->size = sizeof *f;
  f->owned = 1;
}

bool sr_file_ok(const SaveFile *f) {
  if (memcmp(f->magic, "SNR1", 4) || f->version != SAVE_VERSION || f->size != sizeof *f) return false;
  if (f->sum != fnv(f, offsetof(SaveFile, sum))) return false;
  if (!(f->owned & 1) || f->owned >> NSKINS || f->skin >= NSKINS || !(f->owned >> f->skin & 1)) return false;
  if (f->guide > 1 || f->best > 99999 || f->gifts > 99999 || f->spare) return false;
  return true;
}

static const char head[] = "# Snow Rider 3D's saves: keep this file\n";
static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static bool copy_write(const SaveFile *f) {
  uint8_t copy[1 + sizeof head + 2 + sizeof SAVE_NAME + 1 + (sizeof(SaveFile) + 2) / 3 * 4 + 2];
  const uint8_t *bytes = (const uint8_t *)f;
  uint32_t len = sizeof *f, nl = sizeof SAVE_NAME - 1;
  uint8_t *p = copy;
  *p++ = 0;   /* (a script's first byte: not imported) */
  memcpy(p, head, sizeof head - 1), p += sizeof head - 1;
  *p++ = '#', *p++ = '>';
  memcpy(p, SAVE_NAME, nl), p += nl;
  *p++ = ':';
  for (uint32_t i = 0; i < len; i += 3, p += 4) {
    uint32_t left = len - i;
    uint32_t v = (uint32_t)bytes[i] << 16 | (left > 1 ? (uint32_t)bytes[i + 1] << 8 : 0) | (left > 2 ? bytes[i + 2] : 0);
    p[0] = (uint8_t)b64[v >> 18 & 63];
    p[1] = (uint8_t)b64[v >> 12 & 63];
    p[2] = left > 1 ? (uint8_t)b64[v >> 6 & 63] : '=';
    p[3] = left > 2 ? (uint8_t)b64[v & 63] : '=';
  }
  *p++ = '\n';
  return plat_save(COPY_NAME, copy, (uint32_t)(p - copy));
}

static int b64v(uint8_t c) {
  for (int i = 0; i < 64; i++)
    if ((uint8_t)b64[i] == c) return i;
  return -1;
}

static bool copy_read(SaveFile *f) {
  uint32_t n, nl = sizeof SAVE_NAME - 1;
  const uint8_t *c = plat_load(COPY_NAME, &n);
  if (!c) return false;
  for (uint32_t i = 0; i + nl + 3 <= n; i++) {
    if (c[i] != '#' || c[i + 1] != '>' || memcmp(c + i + 2, SAVE_NAME, nl) || c[i + 2 + nl] != ':') continue;
    const uint8_t *l = c + i + 3 + nl;
    uint32_t m = 0;
    while (l + m < c + n && l[m] != '\n' && l[m]) m++;
    if (m != (sizeof *f + 2) / 3 * 4) return false;
    uint8_t *bytes = (uint8_t *)f;
    uint32_t o = 0;
    for (uint32_t k = 0; k < m; k += 4) {
      int v0 = b64v(l[k]), v1 = b64v(l[k + 1]), v2 = l[k + 2] == '=' ? 0 : b64v(l[k + 2]),
          v3 = l[k + 3] == '=' ? 0 : b64v(l[k + 3]);
      if (v0 < 0 || v1 < 0 || v2 < 0 || v3 < 0) return false;
      uint32_t w = (uint32_t)(v0 << 18 | v1 << 12 | v2 << 6 | v3);
      if (o < sizeof *f) bytes[o++] = (uint8_t)(w >> 16);
      if (o < sizeof *f) bytes[o++] = (uint8_t)(w >> 8);
      if (o < sizeof *f) bytes[o++] = (uint8_t)w;
    }
    return sr_file_ok(f);
  }
  return false;
}

int sr_load(SaveFile *f) {
  uint32_t n = 0;
  const uint8_t *d = plat_load(SAVE_NAME, &n);
  if (d && n == sizeof *f) {
    memcpy(f, d, sizeof *f);   /* (a file is not aligned) */
    if (sr_file_ok(f)) return 1;
  }
  if (copy_read(f)) {
    sr_save(f);   /* (the save comes back from it) */
    return 2;
  }
  sr_fresh(f);
  return 0;
}

bool sr_save(SaveFile *f) {
  memcpy(f->magic, "SNR1", 4);
  f->version = SAVE_VERSION, f->size = sizeof *f;
  f->sum = fnv(f, offsetof(SaveFile, sum));
  bool ok = plat_save(SAVE_NAME, f, sizeof *f);
  return copy_write(f) && ok;
}
