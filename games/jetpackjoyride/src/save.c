/* The save (save.h): a packed file with a magic, a version, its size and a checksum; anything else (a file
 * from another version, damaged, cut short) is not read, and the copy in the Python script is tried. */
#include <string.h>
#include "plat.h"
#include "save.h"

uint32_t jj_sum(const void *data, uint32_t n) {
  const uint8_t *p = data;
  uint32_t h = 2166136261u;
  for (uint32_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
  return h;
}

bool jj_file_ok(const jj_file *f) {
  if (f->magic != JJ_MAGIC || f->version != JJ_VERSION || f->size != sizeof *f) return false;
  if (f->sum != jj_sum(f, offsetof(jj_file, sum))) return false;
  const jj_profile *p = &f->p;
  if (p->guide_seen > 1 || p->head_start > 1 || p->double_coins > 1 || p->has_run > 1) return false;
  return !p->has_run || jj_valid(&f->run);
}

_Static_assert(offsetof(jj_file, run) == JJ_V1_RUN && offsetof(jj_run, dist) == 40 && offsetof(jj_run, coins) == 44,
               "the first fields stay where version 1 had them");

bool jj_file_v1(const uint8_t *d, uint32_t n, jj_profile *p) {
  uint32_t magic, sum, dist, coins;
  uint16_t version, size;
  if (n != JJ_V1_SIZE) return false;
  memcpy(&magic, d, 4), memcpy(&version, d + 4, 2), memcpy(&size, d + 6, 2), memcpy(&sum, d + JJ_V1_SIZE - 4, 4);
  if (magic != JJ_MAGIC || version != 1 || size != JJ_V1_SIZE || sum != jj_sum(d, JJ_V1_SIZE - 4)) return false;
  memcpy(p, d + offsetof(jj_file, p), sizeof *p);
  if (p->guide_seen > 1 || p->head_start > 1 || p->double_coins > 1 || p->has_run > 1) return false;
  if (p->has_run) {
    memcpy(&dist, d + JJ_V1_RUN + offsetof(jj_run, dist), 4), memcpy(&coins, d + JJ_V1_RUN + offsetof(jj_run, coins), 4);
    uint32_t m = dist / (16 * 18);   /* (DISTANCE_DIVISOR) */
    if (coins < 100000000u && p->coins < 0xffffffffu - coins) p->coins += coins;
    if (m < 10000000u && m > p->best) p->best = m;
    p->has_run = 0;
  }
  return true;
}

static const char head[] = "# Jetpack Joyride's saves: keep this file\n";
static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static uint8_t copy[1 + sizeof head + 2 + sizeof JJ_SAVE_NAME + 1 + (sizeof(jj_file) + 2) / 3 * 4 + 2];

static bool copy_write(const jj_file *f) {
  const uint8_t *d = (const uint8_t *)f;
  uint32_t len = sizeof *f, nl = sizeof JJ_SAVE_NAME - 1;
  uint8_t *p = copy;
  *p++ = 0;   /* (a script's first byte: not imported) */
  memcpy(p, head, sizeof head - 1), p += sizeof head - 1;
  *p++ = '#', *p++ = '>';
  memcpy(p, JJ_SAVE_NAME, nl), p += nl;
  *p++ = ':';
  for (uint32_t i = 0; i < len; i += 3, p += 4) {
    uint32_t left = len - i, v = (uint32_t)d[i] << 16 | (left > 1 ? (uint32_t)d[i + 1] << 8 : 0) | (left > 2 ? d[i + 2] : 0);
    p[0] = (uint8_t)b64[v >> 18 & 63];
    p[1] = (uint8_t)b64[v >> 12 & 63];
    p[2] = left > 1 ? (uint8_t)b64[v >> 6 & 63] : '=';
    p[3] = left > 2 ? (uint8_t)b64[v & 63] : '=';
  }
  *p++ = '\n';
  return plat_save(JJ_COPY_NAME, copy, (uint32_t)(p - copy));
}

static int b64v(uint8_t c) {
  for (int i = 0; i < 64; i++)
    if ((uint8_t)b64[i] == c) return i;
  return -1;
}

static uint8_t raw[sizeof(jj_file) + 4];
/* the copy in the script, decoded into raw: its length, or 0 */
static uint32_t copy_read(void) {
  uint32_t n, nl = sizeof JJ_SAVE_NAME - 1;
  const uint8_t *c = plat_load(JJ_COPY_NAME, &n);
  if (!c) return 0;
  for (uint32_t i = 0; i + nl + 3 <= n; i++) {
    if (c[i] != '#' || c[i + 1] != '>' || memcmp(c + i + 2, JJ_SAVE_NAME, nl) || c[i + 2 + nl] != ':') continue;
    const uint8_t *l = c + i + 3 + nl;
    uint32_t m = 0;
    while (l + m < c + n && l[m] != '\n' && l[m]) m++;
    uint32_t len = m == (sizeof(jj_file) + 2) / 3 * 4 ? sizeof(jj_file) : m == (JJ_V1_SIZE + 2) / 3 * 4 ? JJ_V1_SIZE : 0;
    if (!len) return 0;
    uint8_t *out = raw;
    uint32_t o = 0;
    for (uint32_t k = 0; k < m; k += 4) {
      int v0 = b64v(l[k]), v1 = b64v(l[k + 1]), v2 = l[k + 2] == '=' ? 0 : b64v(l[k + 2]), v3 = l[k + 3] == '=' ? 0 : b64v(l[k + 3]);
      if (v0 < 0 || v1 < 0 || v2 < 0 || v3 < 0) return 0;
      uint32_t w = (uint32_t)(v0 << 18 | v1 << 12 | v2 << 6 | v3);
      if (o < len) out[o++] = (uint8_t)(w >> 16);
      if (o < len) out[o++] = (uint8_t)(w >> 8);
      if (o < len) out[o++] = (uint8_t)w;
    }
    return len;
  }
  return 0;
}

static jj_file file;   /* (static: the stack stays small) */

void save_load(jj_profile *p, jj_run *r) {
  uint32_t n;
  const uint8_t *d = plat_load(JJ_SAVE_NAME, &n);
  bool ok = false;
  if (d && n == sizeof file) {
    memcpy(&file, d, sizeof file);
    ok = jj_file_ok(&file);
  }
  if (!ok && d && jj_file_v1(d, n, p)) {   /* an older version's: its records go on in a new file */
    save_write(p, 0);
    return;
  }
  if (!ok) {
    uint32_t len = copy_read();
    if (len == sizeof file) {
      memcpy(&file, raw, sizeof file);
      if ((ok = jj_file_ok(&file))) plat_save(JJ_SAVE_NAME, &file, sizeof file);   /* back where it belongs */
    } else if (len == JJ_V1_SIZE && jj_file_v1(raw, len, p)) {
      save_write(p, 0);
      return;
    }
  }
  if (ok) {
    *p = file.p;
    if (p->has_run) *r = file.run;
  } else {
    memset(p, 0, sizeof *p);
  }
}

bool save_write(const jj_profile *p, const jj_run *r) {
  memset(&file, 0, sizeof file);
  file.magic = JJ_MAGIC, file.version = JJ_VERSION, file.size = sizeof file;
  file.p = *p;
  file.p.has_run = r != 0;
  if (r) file.run = *r;
  file.sum = jj_sum(&file, offsetof(jj_file, sum));
  bool ok = plat_save(JJ_SAVE_NAME, &file, sizeof file);
  copy_write(&file);
  return ok;
}
