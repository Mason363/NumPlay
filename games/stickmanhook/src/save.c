/* The save (save.h): a packed file with a magic, a version, its size and a checksum; anything else (a file
 * from another version, damaged, cut short) is not read, and the copy in the Python script is tried. */
#include <string.h>
#include "plat.h"
#include "save.h"

static uint32_t sum(const void *data, uint32_t n) {
  const uint8_t *p = data;
  uint32_t h = 2166136261u;
  for (uint32_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
  return h;
}

bool sh_profile_ok(const sh_profile *p) {
  if (p->current > p->done || p->done > 100000000u || p->guide_seen > 1 || p->has_course > 1 || p->unused) return false;
  if (!sh_skin_open(p->done, p->skin)) return false;
  return !p->has_course || sh_course_ok(p->current, &p->course);
}

bool sh_file_ok(const sh_file *f) {
  if (f->magic != SH_MAGIC || f->version != SH_VERSION || f->size != sizeof *f) return false;
  return f->sum == sum(f, offsetof(sh_file, sum)) && sh_profile_ok(&f->p);
}

static const char head[] = "# Stickman Hook's saves: keep this file\n";
static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static uint8_t copy[1 + sizeof head + 2 + sizeof SH_SAVE_NAME + 1 + (sizeof(sh_file) + 2) / 3 * 4 + 2];

static bool copy_write(const sh_file *f) {
  const uint8_t *d = (const uint8_t *)f;
  uint32_t len = sizeof *f, nl = sizeof SH_SAVE_NAME - 1;
  uint8_t *p = copy;
  *p++ = 0;   /* (a script's first byte: not imported) */
  memcpy(p, head, sizeof head - 1), p += sizeof head - 1;
  *p++ = '#', *p++ = '>';
  memcpy(p, SH_SAVE_NAME, nl), p += nl;
  *p++ = ':';
  for (uint32_t i = 0; i < len; i += 3, p += 4) {
    uint32_t left = len - i, v = (uint32_t)d[i] << 16 | (left > 1 ? (uint32_t)d[i + 1] << 8 : 0) | (left > 2 ? d[i + 2] : 0);
    p[0] = (uint8_t)b64[v >> 18 & 63];
    p[1] = (uint8_t)b64[v >> 12 & 63];
    p[2] = left > 1 ? (uint8_t)b64[v >> 6 & 63] : '=';
    p[3] = left > 2 ? (uint8_t)b64[v & 63] : '=';
  }
  *p++ = '\n';
  return plat_save(SH_COPY_NAME, copy, (uint32_t)(p - copy));
}

static int b64v(uint8_t c) {
  for (int i = 0; i < 64; i++)
    if ((uint8_t)b64[i] == c) return i;
  return -1;
}

static bool copy_read(sh_file *f) {
  uint32_t n, nl = sizeof SH_SAVE_NAME - 1;
  const uint8_t *c = plat_load(SH_COPY_NAME, &n);
  if (!c) return false;
  for (uint32_t i = 0; i + nl + 3 <= n; i++) {
    if (c[i] != '#' || c[i + 1] != '>' || memcmp(c + i + 2, SH_SAVE_NAME, nl) || c[i + 2 + nl] != ':') continue;
    const uint8_t *l = c + i + 3 + nl;
    uint32_t m = 0;
    while (l + m < c + n && l[m] != '\n' && l[m]) m++;
    if (m != (sizeof *f + 2) / 3 * 4) return false;
    uint8_t *out = (uint8_t *)f;
    uint32_t o = 0;
    for (uint32_t k = 0; k < m; k += 4) {
      int v0 = b64v(l[k]), v1 = b64v(l[k + 1]), v2 = l[k + 2] == '=' ? 0 : b64v(l[k + 2]), v3 = l[k + 3] == '=' ? 0 : b64v(l[k + 3]);
      if (v0 < 0 || v1 < 0 || v2 < 0 || v3 < 0) return false;
      uint32_t w = (uint32_t)(v0 << 18 | v1 << 12 | v2 << 6 | v3);
      if (o < sizeof *f) out[o++] = (uint8_t)(w >> 16);
      if (o < sizeof *f) out[o++] = (uint8_t)(w >> 8);
      if (o < sizeof *f) out[o++] = (uint8_t)w;
    }
    return true;
  }
  return false;
}

static sh_file file;

void save_load(sh_profile *p) {
  uint32_t n;
  const uint8_t *d = plat_load(SH_SAVE_NAME, &n);
  bool ok = false;
  if (d && n == sizeof file) {
    memcpy(&file, d, sizeof file);
    ok = sh_file_ok(&file);
  }
  if (!ok && copy_read(&file) && sh_file_ok(&file)) {
    ok = true;
    plat_save(SH_SAVE_NAME, &file, sizeof file);   /* back where it belongs */
  }
  if (ok) *p = file.p;
  else memset(p, 0, sizeof *p);
}

bool save_write(const sh_profile *p) {
  memset(&file, 0, sizeof file);
  file.magic = SH_MAGIC, file.version = SH_VERSION, file.size = sizeof file;
  file.p = *p;
  file.sum = sum(&file, offsetof(sh_file, sum));
  bool ok = plat_save(SH_SAVE_NAME, &file, sizeof file);
  copy_write(&file);
  return ok;
}
