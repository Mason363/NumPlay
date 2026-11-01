/* The save (save.h): a magic, a version, the profile, the game packed (PackBits) and a checksum. Anything
 * else (another version, damaged, cut short) is not read, and the copy in the Python script is tried. */
#include <string.h>
#include "plat.h"
#include "save.h"

uint32_t dk_sum(const void *data, uint32_t n) {
  const uint8_t *p = data;
  uint32_t h = 2166136261u;
  for (uint32_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
  return h;
}

#define RAW_SIZE (DK_SNAP + 0x400 + 6)
static uint8_t raw[RAW_SIZE];

static void flatten(const dk_state *s) {
  memcpy(raw, s->ram, DK_SNAP);
  memcpy(raw + DK_SNAP, s->vram, 0x400);
  uint8_t *t = raw + DK_SNAP + 0x400;
  t[0] = s->palette, t[1] = s->over;
  for (int i = 0; i < 4; i++) t[2 + i] = (uint8_t)(s->loop >> (8 * i));
}

uint32_t dk_pack(const dk_state *s, uint8_t *out, uint32_t room) {
  flatten(s);
  uint32_t i = 0, o = 0;
  while (i < RAW_SIZE) {
    uint32_t run = 1;
    while (i + run < RAW_SIZE && run < 129 && raw[i + run] == raw[i]) run++;
    if (run >= 3) {
      if (o + 2 > room) return 0;
      out[o++] = (uint8_t)(126 + run), out[o++] = raw[i];
      i += run;
      continue;
    }
    uint32_t lit = 0; /* literals until a run of 3 */
    while (i + lit < RAW_SIZE && lit < 128 &&
           !(i + lit + 2 < RAW_SIZE && raw[i + lit] == raw[i + lit + 1] && raw[i + lit] == raw[i + lit + 2]))
      lit++;
    if (o + 1 + lit > room) return 0;
    out[o++] = (uint8_t)(lit - 1);
    memcpy(out + o, raw + i, lit);
    o += lit, i += lit;
  }
  return o;
}

bool dk_unpack(dk_state *s, const uint8_t *in, uint32_t n) {
  uint32_t i = 0, o = 0;
  while (i < n) {
    uint8_t c = in[i++];
    if (c < 128) {
      uint32_t lit = c + 1u;
      if (i + lit > n || o + lit > RAW_SIZE) return false;
      memcpy(raw + o, in + i, lit);
      i += lit, o += lit;
    } else {
      uint32_t run = c - 126u;
      if (i >= n || o + run > RAW_SIZE) return false;
      memset(raw + o, in[i++], run);
      o += run;
    }
  }
  if (o != RAW_SIZE) return false;
  memset(s, 0, sizeof *s);
  memcpy(s->ram, raw, DK_SNAP);
  memcpy(s->vram, raw + DK_SNAP, 0x400);
  const uint8_t *t = raw + DK_SNAP + 0x400;
  s->palette = t[0], s->over = t[1];
  s->loop = (uint32_t)t[2] | (uint32_t)t[3] << 8 | (uint32_t)t[4] << 16 | (uint32_t)t[5] << 24;
  return dk_valid(s);
}

/* the file: magic 4, version 2, length 2, profile 16, packed game 0+, checksum 4 */
#define HEAD 24
static uint8_t file[DK_SAVE_MAX];
static void put32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static uint32_t get32(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

bool dk_file_ok(const uint8_t *d, uint32_t n, dk_profile *p, dk_state *s) {
  if (!d || n < HEAD + 4 || n > DK_SAVE_MAX) return false;
  if (get32(d) != DK_MAGIC || (d[4] | d[5] << 8) != DK_VERSION || (uint32_t)(d[6] | d[7] << 8) != n) return false;
  if (get32(d + n - 4) != dk_sum(d, n - 4)) return false;
  dk_profile q = {get32(d + 8), get32(d + 12), get32(d + 16), (uint16_t)(d[20] | d[21] << 8), d[22], d[23]};
  if (q.guide_seen > 1 || q.has_run > 1 || q.best > 999999 || q.best_level > 255) return false;
  if (q.has_run) {
    if (!dk_unpack(s, d + HEAD, n - HEAD - 4)) return false;
  } else if (n != HEAD + 4) {
    return false;
  }
  *p = q;
  return true;
}

static const char head[] = "# Donkey Kong's saves: keep this file\n";
static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static uint8_t copy[1 + sizeof head + 2 + sizeof DK_SAVE_NAME + 1 + (DK_SAVE_MAX + 2) / 3 * 4 + 2];

static bool copy_write(const uint8_t *d, uint32_t len) {
  uint32_t nl = sizeof DK_SAVE_NAME - 1;
  uint8_t *p = copy;
  *p++ = 0; /* (a script's first byte: not imported) */
  memcpy(p, head, sizeof head - 1), p += sizeof head - 1;
  *p++ = '#', *p++ = '>';
  memcpy(p, DK_SAVE_NAME, nl), p += nl;
  *p++ = ':';
  for (uint32_t i = 0; i < len; i += 3, p += 4) {
    uint32_t left = len - i, v = (uint32_t)d[i] << 16 | (left > 1 ? (uint32_t)d[i + 1] << 8 : 0) | (left > 2 ? d[i + 2] : 0);
    p[0] = (uint8_t)b64[v >> 18 & 63];
    p[1] = (uint8_t)b64[v >> 12 & 63];
    p[2] = left > 1 ? (uint8_t)b64[v >> 6 & 63] : '=';
    p[3] = left > 2 ? (uint8_t)b64[v & 63] : '=';
  }
  *p++ = '\n';
  return plat_save(DK_COPY_NAME, copy, (uint32_t)(p - copy));
}

static int b64v(uint8_t c) {
  for (int i = 0; i < 64; i++)
    if ((uint8_t)b64[i] == c) return i;
  return -1;
}

static uint32_t copy_read(void) { /* the copy, decoded into file[]: its length, or 0 */
  uint32_t n, nl = sizeof DK_SAVE_NAME - 1;
  const uint8_t *c = plat_load(DK_COPY_NAME, &n);
  if (!c) return 0;
  for (uint32_t i = 0; i + nl + 3 <= n; i++) {
    if (c[i] != '#' || c[i + 1] != '>' || memcmp(c + i + 2, DK_SAVE_NAME, nl) || c[i + 2 + nl] != ':') continue;
    const uint8_t *l = c + i + 3 + nl;
    uint32_t m = 0, o = 0;
    while (l + m < c + n && l[m] != '\n' && l[m]) m++;
    if (m % 4 || m / 4 * 3 > DK_SAVE_MAX) return 0;
    for (uint32_t k = 0; k < m; k += 4) {
      int v0 = b64v(l[k]), v1 = b64v(l[k + 1]), v2 = l[k + 2] == '=' ? 0 : b64v(l[k + 2]),
          v3 = l[k + 3] == '=' ? 0 : b64v(l[k + 3]);
      if (v0 < 0 || v1 < 0 || v2 < 0 || v3 < 0) return 0;
      uint32_t w = (uint32_t)(v0 << 18 | v1 << 12 | v2 << 6 | v3);
      file[o++] = (uint8_t)(w >> 16);
      if (l[k + 2] != '=') file[o++] = (uint8_t)(w >> 8);
      if (l[k + 3] != '=') file[o++] = (uint8_t)w;
    }
    return o;
  }
  return 0;
}

void save_load(dk_profile *p, dk_state *s) {
  uint32_t n;
  const uint8_t *d = plat_load(DK_SAVE_NAME, &n);
  if (d && n <= DK_SAVE_MAX) {
    memcpy(file, d, n);
    if (dk_file_ok(file, n, p, s)) return;
  }
  n = copy_read();
  if (n && dk_file_ok(file, n, p, s)) {
    plat_save(DK_SAVE_NAME, file, n); /* back where it belongs */
    return;
  }
  memset(p, 0, sizeof *p);
}

bool save_write(const dk_profile *p, const dk_state *s) {
  uint32_t n = HEAD;
  memset(file, 0, HEAD);
  put32(file, DK_MAGIC);
  file[4] = DK_VERSION;
  put32(file + 8, p->best);
  put32(file + 12, p->games);
  put32(file + 16, p->boards);
  file[20] = (uint8_t)p->best_level, file[21] = (uint8_t)(p->best_level >> 8);
  file[22] = p->guide_seen;
  file[23] = 0;
  if (s) {
    uint32_t k = dk_pack(s, file + HEAD, DK_SAVE_MAX - HEAD - 4);
    if (k) file[23] = 1, n += k;
  }
  file[6] = (uint8_t)(n + 4), file[7] = (uint8_t)((n + 4) >> 8);
  put32(file + n, dk_sum(file, n));
  n += 4;
  bool ok = plat_save(DK_SAVE_NAME, file, n);
  copy_write(file, n);
  return ok;
}
