/* The save (save.h): a bytes file with a magic, a version, its size and a checksum, every value checked; anything
 * else (damaged, cut short, from elsewhere) is not read: the copy in the Python script is tried, then a fresh
 * start. */
#include <stddef.h>
#include <string.h>
#include "plat.h"
#include "save.h"

void fnf_fresh(fnf_progress_t *p) {
  memset(p, 0, sizeof *p);
  p->week = 1, p->week_diff = FNF_DIFF_NORMAL, p->song_diff = FNF_DIFF_NORMAL;
}

uint32_t fnf_sum(const void *data, uint32_t n) {
  const uint8_t *p = data;
  uint32_t h = 2166136261u;
  for (uint32_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
  return h;
}

bool fnf_file_ok(const fnf_file_t *f) {
  if (f->magic != FNF_MAGIC || f->version != FNF_VERSION || f->size != sizeof *f) return false;
  if (f->sum != fnf_sum(f, offsetof(fnf_file_t, sum))) return false;
  const fnf_progress_t *p = &f->p;
  if (p->downscroll > 1 || p->guide > 1 || p->offset < -FNF_OFFSET_MAX || p->offset > FNF_OFFSET_MAX) return false;
  if (p->week >= FNF_LEVELS || p->week_diff >= FNF_DIFFS || p->song >= FNF_SONGS || p->song_diff >= FNF_DIFFS ||
      p->list > 1)
    return false;
  for (int i = 0; i < FNF_CHARTS; i++)
    if (p->chart[i].rank > 6 || p->chart[i].completion > 10000) return false;
  return true;
}

static const char head[] = "# Friday Night Funkin' saves: keep this file\n";
static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static uint8_t copy[1 + sizeof head + 2 + sizeof FNF_SAVE_NAME + 1 + (sizeof(fnf_file_t) + 2) / 3 * 4 + 2];

/* the copy: the file in base 64, in a comment line of a script */
static bool copy_write(const fnf_file_t *f) {
  const uint8_t *bytes = (const uint8_t *)f;
  uint32_t len = sizeof *f, nl = sizeof FNF_SAVE_NAME - 1;
  uint8_t *p = copy;
  *p++ = 0;   /* (a script's first byte: not imported) */
  memcpy(p, head, sizeof head - 1), p += sizeof head - 1;
  *p++ = '#', *p++ = '>';
  memcpy(p, FNF_SAVE_NAME, nl), p += nl;
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
  return plat_save(FNF_COPY_NAME, copy, (uint32_t)(p - copy));
}

static int b64v(uint8_t c) {
  for (int i = 0; i < 64; i++)
    if ((uint8_t)b64[i] == c) return i;
  return -1;
}

static bool copy_read(fnf_file_t *f) {
  uint32_t n, nl = sizeof FNF_SAVE_NAME - 1;
  const uint8_t *c = plat_load(FNF_COPY_NAME, &n);
  if (!c) return false;
  for (uint32_t i = 0; i + nl + 3 <= n; i++) {
    if (c[i] != '#' || c[i + 1] != '>' || memcmp(c + i + 2, FNF_SAVE_NAME, nl) || c[i + 2 + nl] != ':') continue;
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
    return true;
  }
  return false;
}

static fnf_file_t file;   /* (static: the stack stays small) */

bool save_load(fnf_progress_t *p) {
  uint32_t n;
  const uint8_t *d = plat_load(FNF_SAVE_NAME, &n);
  bool ok = false;
  if (d && n == sizeof file) {
    memcpy(&file, d, sizeof file);
    ok = fnf_file_ok(&file);
  }
  if (!ok && copy_read(&file) && fnf_file_ok(&file)) {
    ok = true;
    plat_save(FNF_SAVE_NAME, &file, sizeof file);   /* back where it belongs */
  }
  if (!ok) {
    fnf_fresh(p);
    return false;
  }
  *p = file.p;
  return true;
}

bool save_write(const fnf_progress_t *p) {
  memset(&file, 0, sizeof file);
  file.magic = FNF_MAGIC, file.version = FNF_VERSION, file.size = sizeof file;
  file.p = *p;
  file.sum = fnf_sum(&file, offsetof(fnf_file_t, sum));
  bool ok = plat_save(FNF_SAVE_NAME, &file, sizeof file);
  copy_write(&file);
  return ok;
}

void fnf_record(fnf_progress_t *p, unsigned chart, int32_t score, int rank, int32_t completion) {
  if (chart >= FNF_CHARTS) return;
  fnf_record_t *r = &p->chart[chart];
  if (!r->rank) {
    r->score = score, r->rank = (uint8_t)(rank + 1), r->completion = (uint16_t)completion;
    return;
  }
  if (score > r->score) r->score = score;
  if (!(r->rank > rank + 1 || r->completion > completion)) r->rank = (uint8_t)(rank + 1), r->completion = (uint16_t)completion;
}
