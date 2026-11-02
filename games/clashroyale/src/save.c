/* Saves: the profile (clashroyale.sav: the deck, trophies and tallies), the battle as it was when Home quit
 * (crresume.sav, packed, read once on the next start), and a copy of the profile in a Python script: installing
 * apps from the NumWorks website keeps only Python scripts, so the profile comes back from it when the file itself
 * is gone. The copy is a comment line "#>name:base64" (the way NumPlay and its big games keep theirs). */
#include <stddef.h>
#include <string.h>
#ifdef DEBUG_RESUME
#include <stdio.h>
#endif
#include "../../common/np_lz.h"
#include "data.h"
#include "game.h"
#include "plat.h"

#define SAVE_NAME "clashroyale.sav"
#define RESUME_NAME "crresume.sav"
#define COPY_NAME "clashroyale_saves.py"
#define MAGIC 0x31595243u   /* "CRY1" */
#define VERSION 1

typedef struct {
  uint32_t magic;
  uint16_t version, size;
  Profile p;
  uint32_t sum;   /* FNV-1a of what is before it */
} SaveFile;

static uint32_t fnv(const void *d, uint32_t n) {
  const uint8_t *p = d;
  uint32_t h = 2166136261u;
  for (uint32_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
  return h;
}

static const char copy_head[] = "# Clash Royale's saves: keep this file\n";
static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void copy_write(void) {
  uint32_t len;
  const uint8_t *d = plat_load(SAVE_NAME, &len);
  plat_remove(COPY_NAME);
  if (!d || !len) return;
  uint32_t nl = (uint32_t)strlen(SAVE_NAME);
  uint32_t total = 1 + (sizeof copy_head - 1) + 2 + nl + 1 + (len + 2) / 3 * 4 + 1 + 1;
  uint8_t *o = plat_reserve(COPY_NAME, total), *p = o;
  if (!o) return;
  d = plat_load(SAVE_NAME, &len);   /* (the storage may have moved) */
  if (!d) {
    plat_remove(COPY_NAME);
    return;
  }
  *p++ = 0;   /* (the script's status byte: not imported) */
  memcpy(p, copy_head, sizeof copy_head - 1), p += sizeof copy_head - 1;
  *p++ = '#', *p++ = '>';
  memcpy(p, SAVE_NAME, nl), p += nl;
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

/* the profile from the copy, into out (sizeof SaveFile); false if there is none */
static bool copy_read(uint8_t *out) {
  uint32_t n, nl = (uint32_t)strlen(SAVE_NAME);
  const uint8_t *c = plat_load(COPY_NAME, &n);
  if (!c) return false;
  for (uint32_t i = 0; i + nl + 3 <= n; i++) {
    if (c[i] != '#' || c[i + 1] != '>' || memcmp(c + i + 2, SAVE_NAME, nl) || c[i + 2 + nl] != ':') continue;
    const uint8_t *l = c + i + 3 + nl;
    uint32_t m = 0;
    while (l + m < c + n && l[m] != '\n' && l[m]) m++;
    if (m % 4 || m / 4 * 3 - (l[m - 1] == '=') - (l[m - 2] == '=') != sizeof(SaveFile)) return false;
    uint32_t o = 0;
    for (uint32_t k = 0; k < m; k += 4) {
      int v[4];
      for (int j = 0; j < 4; j++) {
        v[j] = l[k + j] == '=' ? 0 : b64_value(l[k + j]);
        if (v[j] < 0) return false;
      }
      uint32_t w = (uint32_t)(v[0] << 18 | v[1] << 12 | v[2] << 6 | v[3]);
      if (o < sizeof(SaveFile)) out[o++] = (uint8_t)(w >> 16);
      if (o < sizeof(SaveFile)) out[o++] = (uint8_t)(w >> 8);
      if (o < sizeof(SaveFile)) out[o++] = (uint8_t)w;
    }
    return true;
  }
  return false;
}

static bool profile_ok(const SaveFile *s) {
  if (s->magic != MAGIC || s->version != VERSION || s->size != sizeof(SaveFile)) return false;
  if (s->sum != fnv(s, offsetof(SaveFile, sum))) return false;
  for (int k = 0; k < 8; k++) {
    if (s->p.deck[k] >= NCARDS) return false;
    for (int j = 0; j < k; j++)
      if (s->p.deck[j] == s->p.deck[k]) return false;
  }
  return s->p.trophies >= 0 && s->p.trophies < 100000;
}

void save_load(void) {
  uint32_t len;
  SaveFile s;
  const uint8_t *d = plat_load(SAVE_NAME, &len);
  bool ok = false;
  if (d && len == sizeof s) {
    memcpy(&s, d, sizeof s);
    ok = profile_ok(&s);
  }
  if (!ok) {
    /* the copy, after the app was installed again */
    uint8_t buf[sizeof(SaveFile)];
    if (copy_read(buf)) {
      memcpy(&s, buf, sizeof s);
      ok = profile_ok(&s);
      if (ok) plat_save(SAVE_NAME, &s, sizeof s);
    }
  }
  if (ok) g.prof = s.p;
}

void save_write(void) {
  SaveFile s;
  memset(&s, 0, sizeof s);
  s.magic = MAGIC, s.version = VERSION, s.size = sizeof s;
  s.p = g.prof;
  s.sum = fnv(&s, offsetof(SaveFile, sum));
  uint32_t len;
  const uint8_t *old = plat_load(SAVE_NAME, &len);
  if (old && len == sizeof s && !memcmp(old, &s, sizeof s)) return;
  if (plat_save(SAVE_NAME, &s, sizeof s)) copy_write();
}

/* ------------------------------------------------------------------ the battle Home kept */
void resume_write(void) {
  uint16_t table[NP_LZ_HASH];   /* (8 KB of stack: Home comes from the main loop, little is on it) */
  bt.magic = 0x31424352u, bt.size = sizeof bt;
  uint32_t n = np_lz_pack((const uint8_t *)&bt, sizeof bt, NULL, table);
#ifdef DEBUG_RESUME
  printf("resume_write: %u packed to %u\n", (unsigned)sizeof bt, n);
#endif
  plat_remove(RESUME_NAME);
  uint8_t *o = plat_reserve(RESUME_NAME, n + 8);
  if (!o) return;
  uint32_t head[2] = {fnv(&bt, sizeof bt), (uint32_t)sizeof bt};
  memcpy(o, head, 8);
  np_lz_pack((const uint8_t *)&bt, sizeof bt, o + 8, table);
  plat_reserved(RESUME_NAME, n + 8);
}

void resume_clear(void) { plat_remove(RESUME_NAME); }

/* a kept battle that can be played on: every index and number in its range */
static bool battle_valid(const Battle *b) {
  if (b->magic != 0x31424352u || b->size != sizeof *b || b->over || b->arena >= NARENAS) return false;
  if (b->time_ms < 0 || b->time_ms > 600000) return false;
  for (int t = 0; t < 2; t++) {
    const Side *s = &b->side[t];
    if (s->nqueue > 8 || s->elixir < 0 || s->elixir > 10 || s->crowns > 3) return false;
    for (int k = 0; k < 8; k++)
      if (s->deck[k] >= NCARDS || (k < s->nqueue && s->queue[k] >= NCARDS)) return false;
    for (int k = 0; k < 4; k++)
      if (s->hand[k] != NONE && s->hand[k] >= NCARDS) return false;
    int16_t tw[3] = {s->king, s->princess[0], s->princess[1]};
    for (int k = 0; k < 3; k++)
      if (tw[k] >= MAXE) return false;
    if (s->king < 0) return false;
  }
  for (int i = 0; i < MAXE; i++) {
    const Ent *e = &b->e[i];
    if (e->used > 1 || (e->used && (e->type >= NUNITS || e->team > 1 || e->lvl > 15 || e->face > 7 || e->anim > 2))) return false;
    if (e->used && (e->x < -2000 || e->x > ARENA_X + 2000 || e->y < -2000 || e->y > ARENA_Y + 2000)) return false;
    if (e->target >= MAXE) return false;
  }
  for (int i = 0; i < MAXP; i++) {
    const Proj *p = &b->p[i];
    if (p->used > 1 || (p->used && (p->type >= NPROJS || p->team > 1 || p->lvl > 15 || p->target >= MAXE))) return false;
  }
  for (int i = 0; i < MAXA; i++) {
    const Area *a = &b->a[i];
    if (a->used > 1 || (a->used && (a->type >= NAREAS || a->team > 1 || a->lvl > 15 || a->hits > 4))) return false;
  }
  for (int i = 0; i < MAXFX; i++)
    if (b->fx[i].used > 1 || (b->fx[i].used && (b->fx[i].kind > FX_TEXT || b->fx[i].len <= 0))) return false;
  return true;
}

bool resume_load(void) {
  uint32_t len;
  const uint8_t *d = plat_load(RESUME_NAME, &len);
  if (!d) return false;
  bool ok = false;
  if (len > 8) {
    uint32_t head[2];
    memcpy(head, d, 8);
    /* (straight into the battle: nothing else uses it yet) */
    int32_t un = head[1] == sizeof bt ? np_lz_unpack(d + 8, len - 8, (uint8_t *)&bt, sizeof bt) : -2;
    ok = un == (int32_t)sizeof bt && fnv(&bt, sizeof bt) == head[0] && battle_valid(&bt);
#ifdef DEBUG_RESUME
    printf("resume: size %u/%u unpacked %d fnv %d valid %d\n", head[1], (unsigned)sizeof bt, un, fnv(&bt, sizeof bt) == head[0], battle_valid(&bt));
#endif
    if (!ok) memset(&bt, 0, sizeof bt);
  }
  plat_remove(RESUME_NAME);
  return ok;
}
