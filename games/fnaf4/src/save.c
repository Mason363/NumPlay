/* Saves: the progress (fnaf4.sav: the night reached, the stars), the night as it was when Home quit
 * (fnaf4res.sav, read once on the next start), and a copy of both in a Python script: installing apps from the
 * NumWorks website keeps only Python scripts, so the saves come back from it when the files themselves are
 * gone. Each is a comment line "#>name:base64" (the way NumPlay and its big games keep theirs). */
#include <string.h>
#include "game.h"
#include "plat.h"

#define SAVE_NAME "fnaf4.sav"
#define RESUME_NAME "fnaf4res.sav"
#define COPY_NAME "fnaf4_saves.py"
#define MAGIC 0x34464E46u   /* "FNF4" */
#define VERSION 1

typedef struct {   /* the game's Ini "fn4": night, beat5 to beat8; the extras' 20/20/20/20, Plushtrap's reward */
  uint32_t magic;
  uint8_t version, level, beat5, beat6;
  uint8_t beat7, beat8, twenty, reward;
} Progress;

typedef struct {
  uint32_t magic;
  uint32_t size;
  Night n;
} Resume;

static const char *const kept[] = {SAVE_NAME, RESUME_NAME};
#define NKEPT (int)(sizeof kept / sizeof kept[0])
static const char copy_head[] = "# Five Nights at Freddy's 4's saves: keep this file\n";
static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

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
    if (!l || !len || len % 4) continue;
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

void save_progress(void) {
  Progress p;
  memset(&p, 0, sizeof p);
  p.magic = MAGIC, p.version = VERSION;
  p.level = g.level, p.beat5 = g.beat5, p.beat6 = g.beat6, p.beat7 = g.beat7, p.beat8 = g.beat8;
  p.twenty = g.twenty, p.reward = g.reward;
  plat_save(SAVE_NAME, &p, sizeof p);
  copy_write();
}

void save_load(void) {
  uint32_t len;
  const uint8_t *d = plat_load(SAVE_NAME, &len);
  g.level = 1;
  if (!d || len != sizeof(Progress)) return;
  Progress p;
  memcpy(&p, d, sizeof p);
  if (p.magic != MAGIC || p.version != VERSION) return;
  g.level = p.level >= 1 && p.level <= 9 ? p.level : 1;
  g.beat5 = p.beat5 & 1, g.beat6 = p.beat6 & 1, g.beat7 = p.beat7 & 1, g.beat8 = p.beat8 & 1;
  g.twenty = p.twenty & g.beat7, g.reward = p.reward & 1;
}

void save_resume(void) {
  Resume r;
  memset(&r, 0, sizeof r);
  r.magic = MAGIC, r.size = sizeof(Night), r.n = g.n;
  plat_save(RESUME_NAME, &r, sizeof r);
  copy_write();
}

/* a kept night that can be played on: every place, state and number in its range */
static bool night_valid(const Night *n) {   /* (a resume file that doesn't hold together is dropped) */
  if (n->night < 1 || n->night > 8 || n->shadow > 2 || n->hour < 1 || n->hour > 12 || n->hour == 6) return false;
  if (n->x < 400 || n->x > 750 || n->s0 > 99 || n->gameover) return false;
  const uint8_t locs[5] = {n->bonnie.loc, n->chica.loc, n->foxy, n->fred, P_AWAY};
  for (int i = 0; i < 5; i++)
    if (locs[i] > P_BED) return false;
  for (int o = 0; o < O_COUNT; o++)
    if (n->ob[o].alive && (n->ob[o].an >= anim_count() || n->ob[o].img >= anim_len(n->ob[o].an))) return false;
  if (!n->ob[O_ROOM].alive || night_top() < 0) return false;
  return true;
}

bool save_resume_load(void) {
  uint32_t len;
  const uint8_t *d = plat_load(RESUME_NAME, &len);
  if (!d) return false;
  bool ok = false;
  if (len == sizeof(Resume)) {
    Resume r;
    memcpy(&r, d, sizeof r);
    if (r.magic == MAGIC && r.size == sizeof(Night)) {
      Night keep = g.n;
      g.n = r.n;
      ok = night_valid(&r.n);
      if (!ok) g.n = keep;
    }
  }
  plat_remove(RESUME_NAME);
  copy_write();
  return ok;
}
