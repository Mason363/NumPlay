/* Saves: the run in progress (isaacrun.sav, kept on Home and at each new
 * floor, gone when the run ends), the progress over all runs (isaac.sav),
 * and a copy of both in a Python script: installing apps from the NumWorks
 * website keeps only Python scripts, so the saves come back from it when the
 * files themselves are gone. Each is a comment line "#>name:base64" (the way
 * NumPlay and its big games keep theirs). */
#include <string.h>
#include "game.h"

#define RUN_NAME "isaacrun.sav"
#define PROGRESS_NAME "isaac.sav"
#define COPY_NAME "isaac_saves.py"
#define RUN_MAGIC 0x31525349u        /* "ISR1" */
#define PROGRESS_MAGIC 0x31505349u   /* "ISP1" */

extern uint8_t item_taken[ITEM_MAX];

static const char *const kept[] = {PROGRESS_NAME, RUN_NAME};
#define NKEPT (int)(sizeof kept / sizeof kept[0])
static const char copy_head[] = "# The Binding of Isaac's saves: keep this file\n";
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

/* ---------------------------------------------------------------- the run */
static uint8_t buf[6144];
static uint32_t at;
static bool over;
static void put(const void *p, uint32_t n) {
  if (at + n > sizeof buf) {
    over = true;
    return;
  }
  memcpy(buf + at, p, n), at += n;
}
static void put8(int v) {
  uint8_t b = (uint8_t)v;
  put(&b, 1);
}
static const uint8_t *src;
static uint32_t src_len, rp;
static bool bad;
static void get(void *p, uint32_t n) {
  if (rp + n > src_len) {
    bad = true;
    memset(p, 0, n);
    return;
  }
  memcpy(p, src + rp, n), rp += n;
}
static int get8(void) {
  uint8_t b;
  get(&b, 1);
  return b;
}

bool save_run(void) {
  if (g_mode != MODE_PLAY && g_mode != MODE_PAUSE && g_mode != MODE_MAP && g_mode != MODE_STAGE && g_mode != MODE_CONFIRM) return false;
  if (pl.dead) return false;
  room_save_things();
  at = 0, over = false;
  uint32_t magic = RUN_MAGIC, size = sizeof(Player);
  put(&magic, 4), put(&size, 4);
  put(&g_run_seed, 4), put(&g_rng, 4), put(&g_frame, 4);
  put(&pl, sizeof pl);
  /* the items already seen this run, as bits */
  for (int i = 0; i < ITEM_MAX; i += 8) {
    int b = 0;
    for (int k = 0; k < 8 && i + k < ITEM_MAX; k++) b |= (item_taken[i + k] ? 1 : 0) << k;
    put8(b);
  }
  /* the floor, then each room: its grid in runs of (cell, state) */
  put8(fl.stage), put8(fl.alt), put8(fl.curse), put8(fl.nrooms), put(fl.map, sizeof fl.map);
  put8(fl.cur), put8(fl.start), put8(fl.boss_room), put8(fl.devil_room), put8(fl.boss);
  put8(fl.devil_open | fl.devil_angel << 1 | fl.boss_beaten << 2 | fl.trapdoor << 3 | (fl.devil_side & 3) << 4
       | fl.golden_key << 6 | fl.golden_bomb << 7);
  put(&fl.seed, 4);
  for (int i = 0; i < fl.nrooms; i++) {
    Room *r = &fl.rooms[i];
    put(r, 6);    /* kind, template, group, state, map cell (the fields before the grid; those after it below) */
    put8(r->nsaved), put8(r->boss), put8(r->variant), put8(r->doors_open);
    int n = 0;
    while (n < ROWS * COLS) {
      int c = r->grid[n / COLS][n % COLS], h = r->gridhp[n / COLS][n % COLS], len = 1;
      while (n + len < ROWS * COLS && len < 255 && r->grid[(n + len) / COLS][(n + len) % COLS] == c
             && r->gridhp[(n + len) / COLS][(n + len) % COLS] == h)
        len++;
      put8(len), put8(c), put8(h);
      n += len;
    }
    put(r->saved, sizeof(Saved) * r->nsaved);
  }
  if (over) return false;
  bool ok = plat_save(RUN_NAME, buf, at);
  copy_write();
  return ok;
}

bool load_run(void) {
  uint32_t len;
  src = plat_load(RUN_NAME, &len);
  if (!src || len < 16) return false;
  src_len = len, rp = 0, bad = false;
  uint32_t magic, size;
  get(&magic, 4), get(&size, 4);
  if (magic != RUN_MAGIC || size != sizeof(Player)) return false;
  get(&g_run_seed, 4), get(&g_rng, 4), get(&g_frame, 4);
  get(&pl, sizeof pl);
  for (int i = 0; i < ITEM_MAX; i += 8) {
    int b = get8();
    for (int k = 0; k < 8 && i + k < ITEM_MAX; k++) item_taken[i + k] = (uint8_t)((b >> k) & 1);
  }
  memset(&fl, 0, sizeof fl);
  fl.stage = (uint8_t)get8(), fl.alt = (uint8_t)get8(), fl.curse = (uint8_t)get8(), fl.nrooms = (uint8_t)get8();
  get(fl.map, sizeof fl.map);
  fl.cur = (int8_t)get8(), fl.start = (int8_t)get8(), fl.boss_room = (int8_t)get8(), fl.devil_room = (int8_t)get8();
  fl.boss = (uint8_t)get8();
  int b = get8();
  fl.devil_open = b & 1, fl.devil_angel = (b >> 1) & 1, fl.boss_beaten = (b >> 2) & 1, fl.trapdoor = (b >> 3) & 1;
  fl.devil_side = (int8_t)((b >> 4) & 3);
  fl.golden_key = (b >> 6) & 1, fl.golden_bomb = (b >> 7) & 1;
  get(&fl.seed, 4);
  if (fl.nrooms > MAX_ROOMS) return false;
  for (int i = 0; i < fl.nrooms && !bad; i++) {
    Room *r = &fl.rooms[i];
    get(r, 6);
    r->nsaved = (uint8_t)get8(), r->boss = (uint8_t)get8(), r->variant = (uint8_t)get8(), r->doors_open = (uint8_t)get8();
    int n = 0;
    while (n < ROWS * COLS && !bad) {
      int l = get8(), c = get8(), h = get8();
      if (!l) bad = true;
      for (int k = 0; k < l && n < ROWS * COLS; k++, n++) r->grid[n / COLS][n % COLS] = (uint8_t)c, r->gridhp[n / COLS][n % COLS] = (uint8_t)h;
    }
    if (r->nsaved > ROOM_SAVED) bad = true;
    else get(r->saved, sizeof(Saved) * r->nsaved);
  }
  if (bad || fl.cur < 0 || fl.cur >= fl.nrooms) return false;
  return true;
}

bool run_saved(void) {
  uint32_t len;
  const uint8_t *d = plat_load(RUN_NAME, &len);
  uint32_t magic, size;
  if (!d || len < 8) return false;
  memcpy(&magic, d, 4), memcpy(&size, d + 4, 4);
  return magic == RUN_MAGIC && size == sizeof(Player);
}

void save_remove_run(void) {
  plat_remove(RUN_NAME);
  copy_write();
}

/* ---------------------------------------------------------------- progress over every run */
typedef struct {
  uint32_t magic;
  uint16_t runs, deaths;
  uint8_t wins, best_stage, pad[2];
} Progress;

void save_progress(void) {
  Progress p = {PROGRESS_MAGIC, g_runs, g_deaths, g_wins, g_best_stage, {g_swap, g_last_char}};
  plat_save(PROGRESS_NAME, &p, sizeof p);
  copy_write();
}

void load_progress(void) {
  uint32_t len;
  const uint8_t *d = plat_load(PROGRESS_NAME, &len);
  Progress p;
  if (!d || len != sizeof p) return;
  memcpy(&p, d, sizeof p);
  if (p.magic != PROGRESS_MAGIC) return;
  g_runs = p.runs, g_deaths = p.deaths, g_wins = p.wins, g_best_stage = p.best_stage, g_swap = p.pad[0] & 1;
  g_last_char = p.pad[1] < CH_COUNT ? p.pad[1] : 0;
}
