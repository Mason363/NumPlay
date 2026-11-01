/* The saves (save.h): checked field by field; anything damaged, cut short or from elsewhere is not read: the copy in
 * the Python script is tried, then a fresh start. The ghosts' file is its own: when it is damaged the ghosts are
 * simply gone. */
#include <stddef.h>
#include <string.h>
#include "plat.h"
#include "save.h"

uint32_t pt_sum(const void *data, uint32_t n) {
  const uint8_t *p = data;
  uint32_t h = 2166136261u;
  for (uint32_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
  return h;
}

void pt_fresh(pt_profile *p) { memset(p, 0, sizeof *p); }

static bool profile_ok(const pt_profile *p) {
  for (int t = 0; t < PT_NTRACKS; t++) {
    if (p->best[t] > PT_MAX_TIME) return false;
    for (int k = 0; k < PT_SPLITS; k++) {
      uint32_t s = p->split[t][k];
      if (s > PT_MAX_TIME || (s && (!p->best[t] || s > p->best[t]))) return false;
      if (k && s && s < p->split[t][k - 1]) return false;
    }
  }
  return p->track < PT_NTRACKS && p->paint < PT_PAINTS && p->view <= 1 && p->guide <= 1;
}

bool pt_file_ok(const pt_file *f) {
  if (f->magic != PT_MAGIC || f->version != PT_VERSION || f->size != sizeof *f) return false;
  if (f->sum != pt_sum(f, offsetof(pt_file, sum))) return false;
  return profile_ok(&f->p);
}

static const char head[] = "# PolyTrack's saves: keep this file\n";
static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static uint8_t copy[1 + sizeof head + 2 + sizeof PT_SAVE_NAME + 1 + (sizeof(pt_file) + 2) / 3 * 4 + 2];

static bool copy_write(const pt_file *f) {
  const uint8_t *bytes = (const uint8_t *)f;
  uint32_t len = sizeof *f, nl = sizeof PT_SAVE_NAME - 1;
  uint8_t *p = copy;
  *p++ = 0; /* (a script's first byte: not imported) */
  memcpy(p, head, sizeof head - 1), p += sizeof head - 1;
  *p++ = '#', *p++ = '>';
  memcpy(p, PT_SAVE_NAME, nl), p += nl;
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
  return plat_save(PT_COPY_NAME, copy, (uint32_t)(p - copy));
}

static int b64v(uint8_t c) {
  for (int i = 0; i < 64; i++)
    if ((uint8_t)b64[i] == c) return i;
  return -1;
}

static bool copy_read(pt_file *f) {
  uint32_t n, nl = sizeof PT_SAVE_NAME - 1;
  const uint8_t *c = plat_load(PT_COPY_NAME, &n);
  if (!c) return false;
  for (uint32_t i = 0; i + nl + 3 <= n; i++) {
    if (c[i] != '#' || c[i + 1] != '>' || memcmp(c + i + 2, PT_SAVE_NAME, nl) || c[i + 2 + nl] != ':') continue;
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

bool save_load(pt_file *f) {
  uint32_t n;
  const uint8_t *d = plat_load(PT_SAVE_NAME, &n);
  bool ok = false;
  if (d && n == sizeof *f) {
    memcpy(f, d, sizeof *f);
    ok = pt_file_ok(f);
  }
  if (!ok && copy_read(f) && pt_file_ok(f)) {
    ok = true;
    plat_save(PT_SAVE_NAME, f, sizeof *f);   /* back where it belongs */
  }
  if (!ok) {
    memset(f, 0, sizeof *f);
    pt_fresh(&f->p);
  }
  return ok;
}

static pt_file out;   /* (static: the stack stays small) */
bool save_write(const pt_file *f) {
  out = *f;
  out.magic = PT_MAGIC, out.version = PT_VERSION, out.size = sizeof out;
  out.sum = pt_sum(&out, offsetof(pt_file, sum));
  bool ok = plat_save(PT_SAVE_NAME, &out, sizeof out);
  copy_write(&out);
  return ok;
}

/* ------------------------------------------------------------------ ghosts:
   "PTG1", version (u16), count (u16), then per ghost: track (u8), pad (u8), length (u16), time (u32), bytes;
   then the sum of everything before it */
static uint8_t gfile[PT_GHOST_BYTES];
static uint32_t glen;

static bool gfile_ok(const uint8_t *d, uint32_t n) {
  if (n < 12 || n > PT_GHOST_BYTES) return false;
  uint32_t magic;
  memcpy(&magic, d, 4);
  if (magic != PT_GMAGIC || (d[4] | d[5] << 8) != PT_VERSION) return false;
  uint32_t sum;
  memcpy(&sum, d + n - 4, 4);
  if (sum != pt_sum(d, n - 4)) return false;
  int count = d[6] | d[7] << 8;
  uint32_t o = 8;
  for (int i = 0; i < count; i++) {
    if (o + 8 > n - 4) return false;
    if (d[o] >= PT_NTRACKS) return false;
    uint32_t len = (uint32_t)(d[o + 2] | d[o + 3] << 8);
    uint32_t ms;
    memcpy(&ms, d + o + 4, 4);
    if (!len || len > PT_GHOST_MAX || ms > PT_MAX_TIME) return false;
    o += 8 + len;
  }
  return o == n - 4;
}

void ghosts_load(void) {
  uint32_t n;
  const uint8_t *d = plat_load(PT_GHOST_NAME, &n);
  glen = 0;
  if (d && gfile_ok(d, n)) {
    memcpy(gfile, d, n);
    glen = n;
  }
}

bool ghosts_find(int track, ghost_entry *g) {
  if (!glen) return false;
  int count = gfile[6] | gfile[7] << 8;
  uint32_t o = 8;
  for (int i = 0; i < count; i++) {
    uint32_t len = (uint32_t)(gfile[o + 2] | gfile[o + 3] << 8);
    if (gfile[o] == track) {
      g->track = (uint8_t)track, g->len = (uint16_t)len;
      memcpy(&g->ms, gfile + o + 4, 4);
      g->data = gfile + o + 8;
      return true;
    }
    o += 8 + len;
  }
  return false;
}

bool ghosts_put(int track, uint32_t ms, const uint8_t *data, uint16_t len) {
  if (!len || len > PT_GHOST_MAX) return false;
  /* the entries kept: the others, the last raced first, while they fit after the new one */
  int count = 0;
  uint32_t end = 8;
  if (glen) {
    int old = gfile[6] | gfile[7] << 8;
    uint32_t q = 8;
    for (int i = 0; i < old; i++) {
      uint32_t l = (uint32_t)(gfile[q + 2] | gfile[q + 3] << 8);
      if (gfile[q] != track && end + 8 + l + 8 + len + 4 <= PT_GHOST_BYTES) {
        memmove(gfile + end, gfile + q, 8 + l);
        end += 8 + l;
        count++;
      }
      q += 8 + l;
    }
  }
  /* room at the front */
  uint32_t shift = 8 + (uint32_t)len;
  memmove(gfile + 8 + shift, gfile + 8, end - 8);
  end += shift;
  uint8_t *e = gfile + 8;
  e[0] = (uint8_t)track, e[1] = 0, e[2] = (uint8_t)len, e[3] = (uint8_t)(len >> 8);
  memcpy(e + 4, &ms, 4);
  memcpy(e + 8, data, len);
  count++;
  uint32_t magic = PT_GMAGIC;
  memcpy(gfile, &magic, 4);
  gfile[4] = PT_VERSION, gfile[5] = 0, gfile[6] = (uint8_t)count, gfile[7] = (uint8_t)(count >> 8);
  uint32_t sum = pt_sum(gfile, end);
  memcpy(gfile + end, &sum, 4);
  glen = end + 4;
  return plat_save(PT_GHOST_NAME, gfile, glen);
}
