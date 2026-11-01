/* The save (save.h): a bytes file with a magic, a version, its size and a checksum; anything else (damaged, cut
 * short, from elsewhere) is not read: the copy in the Python script is tried, then a fresh start. */
#include <stddef.h>
#include <string.h>
#include "plat.h"
#include "save.h"

uint32_t tm_sum(const void *data, uint32_t n) {
  const uint8_t *p = data;
  uint32_t h = 2166136261u;
  for (uint32_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
  return h;
}

bool tm_file_ok(const tm_file *f) {
  if (f->magic != TM_MAGIC || f->version != TM_VERSION || f->size != sizeof *f) return false;
  if (f->sum != tm_sum(f, offsetof(tm_file, sum))) return false;
  return tm_progress_ok(&f->p);
}

static const char head[] = "# Tomb of the Mask's saves: keep this file\n";
static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static uint8_t copy[1 + sizeof head + 2 + sizeof TM_SAVE_NAME + 1 + (sizeof(tm_file) + 2) / 3 * 4 + 2];

/* the copy: the file in base 64, in a comment line of a script */
static bool copy_write(const tm_file *f) {
  const uint8_t *bytes = (const uint8_t *)f;
  uint32_t len = sizeof *f, nl = sizeof TM_SAVE_NAME - 1;
  uint8_t *p = copy;
  *p++ = 0;   /* (a script's first byte: not imported) */
  memcpy(p, head, sizeof head - 1), p += sizeof head - 1;
  *p++ = '#', *p++ = '>';
  memcpy(p, TM_SAVE_NAME, nl), p += nl;
  *p++ = ':';
  for (uint32_t i = 0; i < len; i += 3, p += 4) {
    uint32_t left = len - i, v = (uint32_t)bytes[i] << 16 | (left > 1 ? (uint32_t)bytes[i + 1] << 8 : 0) | (left > 2 ? bytes[i + 2] : 0);
    p[0] = (uint8_t)b64[v >> 18 & 63];
    p[1] = (uint8_t)b64[v >> 12 & 63];
    p[2] = left > 1 ? (uint8_t)b64[v >> 6 & 63] : '=';
    p[3] = left > 2 ? (uint8_t)b64[v & 63] : '=';
  }
  *p++ = '\n';
  return plat_save(TM_COPY_NAME, copy, (uint32_t)(p - copy));
}

static int b64v(uint8_t c) {
  for (int i = 0; i < 64; i++)
    if ((uint8_t)b64[i] == c) return i;
  return -1;
}

static bool copy_read(tm_file *f) {
  uint32_t n, nl = sizeof TM_SAVE_NAME - 1;
  const uint8_t *c = plat_load(TM_COPY_NAME, &n);
  if (!c) return false;
  for (uint32_t i = 0; i + nl + 3 <= n; i++) {
    if (c[i] != '#' || c[i + 1] != '>' || memcmp(c + i + 2, TM_SAVE_NAME, nl) || c[i + 2 + nl] != ':') continue;
    const uint8_t *l = c + i + 3 + nl;
    uint32_t m = 0;
    while (l + m < c + n && l[m] != '\n' && l[m]) m++;
    if (m != (sizeof *f + 2) / 3 * 4) return false;
    uint8_t *bytes = (uint8_t *)f;
    uint32_t o = 0;
    for (uint32_t k = 0; k < m; k += 4) {
      int v0 = b64v(l[k]), v1 = b64v(l[k + 1]), v2 = l[k + 2] == '=' ? 0 : b64v(l[k + 2]), v3 = l[k + 3] == '=' ? 0 : b64v(l[k + 3]);
      if (v0 < 0 || v1 < 0 || v2 < 0 || v3 < 0) return false;
      uint32_t w = (uint32_t)(v0 << 18 | v1 << 12 | v2 << 6 | v3);
      if (o < sizeof *f) bytes[o++] = (uint8_t)(w >> 16);
      if (o < sizeof *f) bytes[o++] = (uint8_t)(w >> 8);
      if (o < sizeof *f) bytes[o++] = (uint8_t)w;
    }
    return true;
  }
  return false;
}

static tm_file file;     /* (static: the stack stays small) */

bool save_load(tm_progress *p) {
  uint32_t n;
  const uint8_t *d = plat_load(TM_SAVE_NAME, &n);
  bool ok = false;
  if (d && n == sizeof file) {
    memcpy(&file, d, sizeof file);
    ok = tm_file_ok(&file);
  }
  if (!ok && copy_read(&file) && tm_file_ok(&file)) {
    ok = true;
    plat_save(TM_SAVE_NAME, &file, sizeof file);   /* back where it belongs */
  }
  if (!ok) {
    tm_new_progress(p);
    return false;
  }
  *p = file.p;
  return true;
}

bool save_write(const tm_progress *p) {
  memset(&file, 0, sizeof file);
  file.magic = TM_MAGIC, file.version = TM_VERSION, file.size = sizeof file;
  file.p = *p;
  file.sum = tm_sum(&file, offsetof(tm_file, sum));
  bool ok = plat_save(TM_SAVE_NAME, &file, sizeof file);
  copy_write(&file);
  return ok;
}
