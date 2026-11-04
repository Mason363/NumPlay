/* The save: kingdomrush.sav, "KRS", a version, then the progress (game.h's Progress), and a check byte; and a
 * copy of it in a Python script, kingdomrush_saves.py: installing apps from the NumWorks website keeps only Python
 * scripts, so the save comes back from it when the file itself is gone. The copy is a comment line
 * "#>kingdomrush.sav:base64" (the way NumPlay and its big games keep theirs). */
#include <string.h>
#include "game.h"
#include "plat.h"

#define SAVE_NAME "kingdomrush.sav"
#define COPY_NAME "kingdomrush_saves.py"
#define SAVE_VERSION 2   /* 1: before the Hero Room (2 bytes less) */
#define NAME_LEN (sizeof SAVE_NAME - 1)

static uint8_t sum(const uint8_t *p, uint32_t n) {
  uint8_t s = 0x5A;
  for (uint32_t i = 0; i < n; i++) s = (uint8_t)(s * 31 + p[i]);
  return s;
}

static const char copy_head[] = "# Kingdom Rush's save: keep this file\n";
static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void copy_write(void) {
  uint32_t len;
  const uint8_t *d = plat_load(SAVE_NAME, &len);
  plat_remove(COPY_NAME);
  if (!d || !len) return;
  uint32_t total = 1 + (sizeof copy_head - 1) + 2 + NAME_LEN + 1 + (len + 2) / 3 * 4 + 1 + 1;
  uint8_t *o = plat_reserve(COPY_NAME, total), *p = o;
  if (!o) return;
  d = plat_load(SAVE_NAME, &len);   /* (the storage may have moved) */
  if (!d) return;
  *p++ = 0;   /* (the script's status byte: not imported) */
  memcpy(p, copy_head, sizeof copy_head - 1), p += sizeof copy_head - 1;
  *p++ = '#', *p++ = '>';
  memcpy(p, SAVE_NAME, NAME_LEN), p += NAME_LEN;
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
  *p++ = 0;
  plat_reserved(COPY_NAME, (uint32_t)(p - o));
}

static int b64_value(uint8_t c) {
  for (int i = 0; i < 64; i++)
    if ((uint8_t)b64[i] == c) return i;
  return -1;
}

static const uint8_t *copy_line(uint32_t *len) {
  uint32_t n;
  const uint8_t *c = plat_load(COPY_NAME, &n);
  if (!c) return NULL;
  for (uint32_t i = 0; i + NAME_LEN + 3 <= n; i++)
    if (c[i] == '#' && c[i + 1] == '>' && !memcmp(c + i + 2, SAVE_NAME, NAME_LEN) && c[i + 2 + NAME_LEN] == ':') {
      const uint8_t *l = c + i + 3 + NAME_LEN;
      uint32_t m = 0;
      while (l + m < c + n && l[m] != '\n' && l[m]) m++;
      *len = m;
      return l;
    }
  return NULL;
}

/* the save back from the script when the file is gone */
static void restore_copy(void) {
  uint32_t have, len;
  if (plat_load(SAVE_NAME, &have)) return;
  const uint8_t *l = copy_line(&len);
  if (!l || !len || len % 4) return;
  uint32_t size = len / 4 * 3 - (l[len - 1] == '=') - (l[len - 2] == '=');
  uint8_t *out = plat_reserve(SAVE_NAME, size);
  if (!out) return;
  l = copy_line(&len);
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
  if (ok && o == size) plat_reserved(SAVE_NAME, size);
  else plat_remove(SAVE_NAME);
}

void save_progress(void) {
  uint8_t buf[4 + sizeof(Progress) + 1];
  memcpy(buf, "KRS", 3);
  buf[3] = SAVE_VERSION;
  memcpy(buf + 4, &g.p, sizeof g.p);
  buf[sizeof buf - 1] = sum(buf, sizeof buf - 1);
  plat_save(SAVE_NAME, buf, sizeof buf);
  copy_write();
}

void load_progress(void) {
  memset(&g.p, 0, sizeof g.p);
  g.p.difficulty = 1;
  restore_copy();
  uint32_t n = 0;
  const uint8_t *d = plat_load(SAVE_NAME, &n);
  if (!d || n < 5 || memcmp(d, "KRS", 3) || sum(d, n - 1) != d[n - 1]) return;
  uint32_t len = d[3] == SAVE_VERSION ? sizeof(Progress) : d[3] == 1 ? PROGRESS_V1 : 0;
  if (!len || n != 4 + len + 1) return;
  Progress p;
  memset(&p, 0, sizeof p);
  memcpy(&p, d + 4, len);
  /* anything out of range: a fresh start rather than a broken one */
  for (int i = 0; i < 12; i++)
    if (p.camp[i] > 3 || p.heroic[i] > 1 || p.iron[i] > 1) return;
  for (int i = 0; i < 6; i++)
    if (p.ups[i] > 5) return;
  if (p.difficulty > 2) p.difficulty = 1;
  if (p.hero_room > 1) p.hero_room = 0;
  if (!p.hero_room || p.hero > 7) p.hero = 0;
  g.p = p;
}
