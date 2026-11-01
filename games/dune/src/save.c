/* The save (save.h): a bytes file with a magic, a version, its size and a checksum; anything else (a file
 * from another version, damaged, cut short) is not read: the copy in the Python script is tried, then a fresh
 * start. */
#include <string.h>
#include "plat.h"
#include "save.h"

uint32_t dn_sum(const void *data, uint32_t n) {
  const uint8_t *p = data;
  uint32_t h = 2166136261u;
  for (uint32_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
  return h;
}

void dn_profile_new(dn_profile *p) {
  memset(p, 0, sizeof *p);
  p->balls = 1, p->lands = 1;
}

bool dn_file_ok(const dn_file *f, dn_run *scratch) {
  if (f->magic != DN_MAGIC || f->version != DN_VERSION || f->size != sizeof *f) return false;
  if (f->sum != dn_sum(f, offsetof(dn_file, sum))) return false;
  const dn_profile *p = &f->p;
  if (!(p->balls & 1) || p->balls >> DN_BALLS || !(p->lands & 1) || p->lands >> DN_LANDS) return false;
  if (p->ball >= DN_BALLS || !(p->balls >> p->ball & 1) || p->land >= DN_LANDS || !(p->lands >> p->land & 1)) return false;
  if (p->has_run > 1 || p->coins > 100000000u || p->best > 100000000u) return false;
  if (!p->has_run) return true;
  memcpy(scratch, f->run, DN_RUN_SAVED);
  dn_shapes(scratch);
  return dn_valid(scratch) && scratch->ball == p->ball && scratch->land == p->land;
}

static const char head[] = "# Dune's saves: keep this file\n";
static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static uint8_t copy[1 + sizeof head + 2 + sizeof DN_SAVE_NAME + 1 + (sizeof(dn_file) + 2) / 3 * 4 + 2];

/* the copy: the file in base 64, in a comment line of a script */
static bool copy_write(const dn_file *f) {
  const uint8_t *bytes = (const uint8_t *)f;
  uint32_t len = sizeof *f, nl = sizeof DN_SAVE_NAME - 1;
  uint8_t *p = copy;
  *p++ = 0;   /* (a script's first byte: not imported) */
  memcpy(p, head, sizeof head - 1), p += sizeof head - 1;
  *p++ = '#', *p++ = '>';
  memcpy(p, DN_SAVE_NAME, nl), p += nl;
  *p++ = ':';
  for (uint32_t i = 0; i < len; i += 3, p += 4) {
    uint32_t left = len - i, v = (uint32_t)bytes[i] << 16 | (left > 1 ? (uint32_t)bytes[i + 1] << 8 : 0) | (left > 2 ? bytes[i + 2] : 0);
    p[0] = (uint8_t)b64[v >> 18 & 63];
    p[1] = (uint8_t)b64[v >> 12 & 63];
    p[2] = left > 1 ? (uint8_t)b64[v >> 6 & 63] : '=';
    p[3] = left > 2 ? (uint8_t)b64[v & 63] : '=';
  }
  *p++ = '\n';
  return plat_save(DN_COPY_NAME, copy, (uint32_t)(p - copy));
}

static int b64v(uint8_t c) {
  for (int i = 0; i < 64; i++)
    if ((uint8_t)b64[i] == c) return i;
  return -1;
}

static bool copy_read(dn_file *f) {
  uint32_t n, nl = sizeof DN_SAVE_NAME - 1;
  const uint8_t *c = plat_load(DN_COPY_NAME, &n);
  if (!c) return false;
  for (uint32_t i = 0; i + nl + 3 <= n; i++) {
    if (c[i] != '#' || c[i + 1] != '>' || memcmp(c + i + 2, DN_SAVE_NAME, nl) || c[i + 2 + nl] != ':') continue;
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

static dn_file file;     /* (static: the stack stays small) */
static dn_run scratch;

bool save_load(dn_profile *p, dn_run *r) {
  uint32_t n;
  const uint8_t *d = plat_load(DN_SAVE_NAME, &n);
  bool ok = false;
  if (d && n == sizeof file) {
    memcpy(&file, d, sizeof file);
    ok = dn_file_ok(&file, &scratch);
  }
  if (!ok && copy_read(&file) && dn_file_ok(&file, &scratch)) {
    ok = true;
    plat_save(DN_SAVE_NAME, &file, sizeof file);   /* back where it belongs */
  }
  if (!ok) {
    dn_profile_new(p);
    return false;
  }
  *p = file.p;
  if (!p->has_run) return false;
  memcpy(r, &scratch, sizeof *r);
  return true;
}

bool save_write(const dn_profile *p, const dn_run *r) {
  memset(&file, 0, sizeof file);
  file.magic = DN_MAGIC, file.version = DN_VERSION, file.size = sizeof file;
  file.p = *p;
  file.p.has_run = r != 0;
  if (r) memcpy(file.run, r, DN_RUN_SAVED);
  file.sum = dn_sum(&file, offsetof(dn_file, sum));
  bool ok = plat_save(DN_SAVE_NAME, &file, sizeof file);
  copy_write(&file);
  return ok;
}
