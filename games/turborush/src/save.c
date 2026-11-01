/* The save (save.h): checked field by field; anything damaged, cut short or from elsewhere is not read: the copy in
 * the Python script is tried, then a fresh start. */
#include <stddef.h>
#include <string.h>
#include "plat.h"
#include "save.h"

void tr_fresh(tr_profile *p, tr_gp *gp) {
  memset(p, 0, sizeof *p);
  p->level = LEVEL_NORMAL, p->traffic = 1;
  memset(gp, 0, sizeof *gp);
}

unsigned tr_price(const tr_profile *p, int item) {
  if (item < 0 || item > 3 || p->up[item] >= 4) return 0;
  return 400u + 250u * p->up[item] + 75u * (unsigned)item;
}

uint32_t tr_sum(const void *data, uint32_t n) {
  const uint8_t *p = data;
  uint32_t h = 2166136261u;
  for (uint32_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
  return h;
}

static bool profile_ok(const tr_profile *p) {
  if (p->credits > TR_MAX_CREDITS || p->races > 999999 || p->wins > p->races || p->titles > p->races) return false;
  for (int i = 0; i < TR_TRACKS; i++) {
    if (p->best_ms[i] > 11 * 60000 || p->best_lap_ms[i] > 11 * 60000) return false;
    if (p->best_ms[i] && (!p->best_lap_ms[i] || p->best_lap_ms[i] > p->best_ms[i])) return false;
  }
  for (int k = 0; k < 4; k++)
    if (p->up[k] > 4) return false;
  return p->paint < 8 && p->style < 2 && p->mode <= MODE_FREE && p->track < TR_TRACKS && p->level <= 2 &&
         p->traffic <= 2 && p->mood <= 2 && p->rain <= 1 && p->manual <= 1 && p->guide <= 1 && !p->spare[0] &&
         !p->spare[1];
}

static bool gp_ok(const tr_gp *g) {
  if (g->on > 1 || g->round >= TR_TRACKS || g->spare) return false;
  if (!g->on) {
    for (const uint8_t *b = (const uint8_t *)g; b < (const uint8_t *)(g + 1); b++)
      if (*b) return false;
    return true;
  }
  unsigned seen = 0;
  for (int i = 0; i < TR_CARS - 1; i++) {
    if (g->skill[i] >= TR_CARS - 1 || seen & 1u << g->skill[i]) return false;
    seen |= 1u << g->skill[i];
  }
  for (int i = 0; i < TR_CARS; i++)
    if (g->points[i] > 15 * TR_TRACKS) return false;
  return true;
}

bool tr_file_ok(const tr_file *f) {
  if (f->magic != TR_MAGIC || f->version != TR_VERSION || f->size != sizeof *f) return false;
  if (f->sum != tr_sum(f, offsetof(tr_file, sum))) return false;
  for (int i = 0; i < 5; i++)
    if (f->spare[i]) return false;
  if (!profile_ok(&f->p) || !gp_ok(&f->gp) || f->has_race > 1) return false;
  if (!f->has_race) return true;
  const tr_race *r = &f->race;
  if (!tr_valid(r)) return false;
  /* a Grand Prix race goes with its Grand Prix */
  return r->mode != MODE_GP || (f->gp.on && r->track == f->gp.round);
}

static const char head[] = "# Turbo Rush's saves: keep this file\n";
static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static uint8_t copy[1 + sizeof head + 2 + sizeof TR_SAVE_NAME + 1 + (sizeof(tr_file) + 2) / 3 * 4 + 2];

/* the copy: the file in base 64, in a comment line of a script */
static bool copy_write(const tr_file *f) {
  const uint8_t *bytes = (const uint8_t *)f;
  uint32_t len = sizeof *f, nl = sizeof TR_SAVE_NAME - 1;
  uint8_t *p = copy;
  *p++ = 0; /* (a script's first byte: not imported) */
  memcpy(p, head, sizeof head - 1), p += sizeof head - 1;
  *p++ = '#', *p++ = '>';
  memcpy(p, TR_SAVE_NAME, nl), p += nl;
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
  return plat_save(TR_COPY_NAME, copy, (uint32_t)(p - copy));
}

static int b64v(uint8_t c) {
  for (int i = 0; i < 64; i++)
    if ((uint8_t)b64[i] == c) return i;
  return -1;
}

static bool copy_read(tr_file *f) {
  uint32_t n, nl = sizeof TR_SAVE_NAME - 1;
  const uint8_t *c = plat_load(TR_COPY_NAME, &n);
  if (!c) return false;
  for (uint32_t i = 0; i + nl + 3 <= n; i++) {
    if (c[i] != '#' || c[i + 1] != '>' || memcmp(c + i + 2, TR_SAVE_NAME, nl) || c[i + 2 + nl] != ':') continue;
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

bool save_load(tr_file *f) {
  uint32_t n;
  const uint8_t *d = plat_load(TR_SAVE_NAME, &n);
  bool ok = false;
  if (d && n == sizeof *f) {
    memcpy(f, d, sizeof *f);
    ok = tr_file_ok(f);
  }
  if (!ok && copy_read(f) && tr_file_ok(f)) {
    ok = true;
    plat_save(TR_SAVE_NAME, f, sizeof *f);   /* back where it belongs */
  }
  if (!ok) {
    memset(f, 0, sizeof *f);
    tr_fresh(&f->p, &f->gp);
  }
  return ok;
}

static tr_file out;   /* (static: the stack stays small) */
bool save_write(const tr_file *f) {
  out = *f;
  out.magic = TR_MAGIC, out.version = TR_VERSION, out.size = sizeof out;
  memset(out.spare, 0, sizeof out.spare);
  if (!out.has_race) memset(&out.race, 0, sizeof out.race);   /* (a race over is not kept) */
  out.sum = tr_sum(&out, offsetof(tr_file, sum));
  bool ok = plat_save(TR_SAVE_NAME, &out, sizeof out);
  copy_write(&out);
  return ok;
}
