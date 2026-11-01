/* Saves: the game in cookieclicker.sav, written every minute (as the game saves itself) and when it
 * quits. Its copy is cookieclicker_saves.py, a comment line "#>name:base64" (the way NumPlay and its
 * big games keep theirs: installing apps erases every file but Python scripts). */
#include <stddef.h>
#include <string.h>
#include "game.h"

#define SAVE_NAME "cookieclicker.sav"
#define COPY_NAME "cookieclicker_saves.py"
#define MAGIC 0x314B4343u   /* "CCK1" */

/* (the line is about 4000 characters: the Python editor doesn't draw a line that long, so the head is all the
 * script shows there) */
static const char copy_head[] = "# Cookie Clicker's saves: keep this file\n";
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
  d = plat_load(SAVE_NAME, &len);   /* (the reservation may have moved the files) */
  if (!d) return;
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

static const uint8_t *copy_line(uint32_t *len) {
  uint32_t n, nl = (uint32_t)strlen(SAVE_NAME);
  const uint8_t *c = plat_load(COPY_NAME, &n);
  if (!c) return NULL;
  for (uint32_t i = 0; i + nl + 3 <= n; i++)
    if (c[i] == '#' && c[i + 1] == '>' && !memcmp(c + i + 2, SAVE_NAME, nl) && c[i + 2 + nl] == ':') {
      const uint8_t *l = c + i + 3 + nl;
      uint32_t m = 0;
      while (l + m < c + n && l[m] != '\n' && l[m]) m++;
      *len = m;
      return l;
    }
  return NULL;
}

/* the save from its copy, when the save is gone (apps were installed again) */
void save_restore_copy(void) {
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

/* ------------------------------------------------------------------ the game */
extern void game_recount(void);
extern void ui_init(void);

bool game_load(void) {
  save_restore_copy();
  uint32_t len;
  const uint8_t *d = plat_load(SAVE_NAME, &len);
  if (!d || len != sizeof(SaveState)) return false;
  uint32_t m;
  memcpy(&m, d, 4);
  if (m != MAGIC) return false;
  memcpy(&S, d, sizeof(SaveState));
  game_recount();
  if (S.on_ascend) {   /* (quit while ascending: back to the heavens) */
    extern void earn_heavenly_chips(double forfeited, bool silent);
    earn_heavenly_chips(S.cookies_earned, true);
    G.on_ascend = true;
  }
  G.recalculate = true, G.upgrades_dirty = true, G.store_dirty = true;
  calculate_gains();
  G.cookies_d = S.cookies;
  return true;
}

bool game_save(void) {
  S.magic = MAGIC;
  S.on_ascend = G.on_ascend || G.ascend_timer > 0;
  bool ok = plat_save(SAVE_NAME, &S, sizeof(SaveState));
  copy_write();
  return ok;
}

/* the Options' "Wipe save" */
void game_wipe(void) {
  plat_remove(SAVE_NAME);
  plat_remove(COPY_NAME);
  game_new();
  calculate_gains();
  ui_init();
  S.seen_keys = 1;
}
