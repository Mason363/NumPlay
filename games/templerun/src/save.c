/* What is kept (save.h): coins, the best score and distance, upgrades, wings and head starts, objectives, in
 * templerun.sav, and a copy of it in a Python script, templerun_saves.py: installing apps from the NumWorks website
 * keeps only Python scripts, so the progress comes back from it when the file itself is gone. The copy is a
 * comment line "#>subway.sav:base64" (the way NumPlay and its big games keep theirs). */
#include <string.h>
#include "game.h"
#include "plat.h"
#include "save.h"

#define SAVE_NAME "templerun.sav"
#define COPY_NAME "templerun_saves.py"
#define MAGIC 0x31524E54u   /* "TNR1" */
#define VERSION 1

typedef struct {
  uint32_t magic, version, size;
  Progress p;
} SaveFile;

static const char copy_head[] = "# Temple Run's saves: keep this file\n";
static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void copy_write(const uint8_t *d, uint32_t len) {
  uint32_t nl = sizeof SAVE_NAME - 1, total = 1 + (sizeof copy_head - 1) + 2 + nl + 1 + (len + 2) / 3 * 4 + 1 + 1;
  plat_remove(COPY_NAME);
  uint8_t *o = plat_reserve(COPY_NAME, total), *p = o;
  if (!o) return;
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

/* the save from its copy, into f; false if there is none (or it is damaged) */
static bool copy_read(SaveFile *f) {
  uint32_t n, nl = sizeof SAVE_NAME - 1;
  const uint8_t *c = plat_load(COPY_NAME, &n);
  if (!c) return false;
  for (uint32_t i = 0; i + nl + 3 <= n; i++) {
    if (c[i] != '#' || c[i + 1] != '>' || memcmp(c + i + 2, SAVE_NAME, nl) || c[i + 2 + nl] != ':') continue;
    const uint8_t *l = c + i + 3 + nl;
    uint32_t m = 0;
    while (l + m < c + n && l[m] != '\n' && l[m]) m++;
    if (!m || m % 4) return false;
    uint32_t size = m / 4 * 3 - (l[m - 1] == '=') - (l[m - 2] == '=');
    if (size != sizeof *f) return false;
    uint8_t *out = (uint8_t *)f;
    uint32_t o = 0;
    for (uint32_t k = 0; k < m; k += 4) {
      int v0 = b64_value(l[k]), v1 = b64_value(l[k + 1]);
      int v2 = l[k + 2] == '=' ? 0 : b64_value(l[k + 2]), v3 = l[k + 3] == '=' ? 0 : b64_value(l[k + 3]);
      if (v0 < 0 || v1 < 0 || v2 < 0 || v3 < 0) return false;
      uint32_t w = (uint32_t)(v0 << 18 | v1 << 12 | v2 << 6 | v3);
      if (o < size) out[o++] = (uint8_t)(w >> 16);
      if (o < size) out[o++] = (uint8_t)(w >> 8);
      if (o < size) out[o++] = (uint8_t)w;
    }
    return true;
  }
  return false;
}

static void fresh(void) {
  memset(&pg, 0, sizeof pg);
  pg.multiplier = 10;   /* (the game's: 10, one more an objective) */
}

static bool valid(const SaveFile *f) {
  if (f->magic != MAGIC || f->version != VERSION || f->size != sizeof(Progress) || f->p.multiplier < 10) return false;
  for (int i = 0; i < UP_COUNT; i++)
    if (f->p.up[i] > UP_MAX) return false;
  return true;
}

void save_load(void) {
  SaveFile f;
  uint32_t len;
  const uint8_t *d = plat_load(SAVE_NAME, &len);
  bool ok = false;
  if (d && len == sizeof f) {
    memcpy(&f, d, sizeof f);
    ok = valid(&f);
  }
  if (!ok && copy_read(&f) && valid(&f)) {
    ok = true;
    plat_save(SAVE_NAME, &f, sizeof f);   /* back where it belongs */
  }
  if (ok) pg = f.p;
  else fresh();
}

bool save_write(void) {
  SaveFile f;
  memset(&f, 0, sizeof f);
  f.magic = MAGIC, f.version = VERSION, f.size = sizeof(Progress), f.p = pg;
  bool ok = plat_save(SAVE_NAME, &f, sizeof f);
  copy_write((const uint8_t *)&f, sizeof f);
  return ok;
}
