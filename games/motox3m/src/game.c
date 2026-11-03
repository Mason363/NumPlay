/* Moto X3M: the game's screens and rules around a run (see game.h). */
#include <math.h>
#include <stddef.h>
#include <string.h>
#include "bike.h"
#include "fx.h"
#include "game.h"
#include "gfx.h"
#include "level.h"
#include "plat.h"
#include "view.h"

Game g;
Save save;

#define DT (1.0f / 30)
static const int PRICES[NSKINS] = {0, 10, 20, 30, 40, 50};

void level_start(void);
void ui_draw(void);

/* ---- saves: "MX3S", a version, then the levels' stars and best times, the bike ---- */

#define SAVE_NAME "motox3m.sav"
#define SAVE_MAGIC 0x5333584Du
#define SAVE_VERSION 1

typedef struct {
  uint32_t magic;
  uint16_t version, size;
  Save s;
} SaveFile;
/* (tests/check.py and NumPlay's tests/test_games.py read these) */
_Static_assert(sizeof(SaveFile) == 264 && offsetof(SaveFile, s.best) == 60 && offsetof(SaveFile, s.skin) == 260,
               "the save's layout");

static void save_defaults(void) {
  memset(&save, 0, sizeof save);
  for (int i = 0; i < NLEVELS; i++) save.stars[i] = 0xFF;
  save.stars[0] = 0;
  save.skin = 1;
  save.owned = 1;
}

/* a copy in a Python script, "#>motox3m.sav:base64": installing apps from the NumWorks website keeps only Python
 * scripts, so the save comes back from it when the file itself is gone (the way NumPlay and its big games keep theirs) */
#define COPY_NAME "motox3m_saves.py"
static const char copy_head[] = "# Moto X3M's saves: keep this file\n";
static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
#define COPY_BYTES (1 + sizeof copy_head - 1 + 2 + sizeof SAVE_NAME - 1 + 1 + (sizeof(SaveFile) + 2) / 3 * 4 + 2)

static bool copy_write(const SaveFile *f) {
  uint8_t o[COPY_BYTES], *p = o;
  const uint8_t *d = (const uint8_t *)f;
  uint32_t len = sizeof *f;
  *p++ = 0;   /* (the script's status byte: not imported) */
  memcpy(p, copy_head, sizeof copy_head - 1), p += sizeof copy_head - 1;
  *p++ = '#', *p++ = '>';
  memcpy(p, SAVE_NAME, sizeof SAVE_NAME - 1), p += sizeof SAVE_NAME - 1;
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
  return plat_save(COPY_NAME, o, (uint32_t)(p - o));
}

static int b64_value(uint8_t c) {
  for (int i = 0; i < 64; i++)
    if ((uint8_t)b64[i] == c) return i;
  return -1;
}

/* the save from the copy; false if there is none or it is broken */
static bool copy_read(SaveFile *f) {
  uint32_t n, nl = sizeof SAVE_NAME - 1;
  const uint8_t *c = plat_load(COPY_NAME, &n), *l = NULL;
  if (!c) return false;
  for (uint32_t i = 0; i + nl + 3 <= n && !l; i++)
    if (c[i] == '#' && c[i + 1] == '>' && !memcmp(c + i + 2, SAVE_NAME, nl) && c[i + 2 + nl] == ':') l = c + i + 3 + nl;
  if (!l) return false;
  uint32_t len = 0;
  while (l + len < c + n && l[len] != '\n' && l[len]) len++;
  if (len != (sizeof *f + 2) / 3 * 4) return false;
  uint8_t *out = (uint8_t *)f;
  uint32_t o = 0;
  for (uint32_t i = 0; i < len; i += 4) {
    int v0 = b64_value(l[i]), v1 = b64_value(l[i + 1]);
    int v2 = l[i + 2] == '=' ? 0 : b64_value(l[i + 2]), v3 = l[i + 3] == '=' ? 0 : b64_value(l[i + 3]);
    if (v0 < 0 || v1 < 0 || v2 < 0 || v3 < 0) return false;
    uint32_t w = (uint32_t)(v0 << 18 | v1 << 12 | v2 << 6 | v3);
    if (o < sizeof *f) out[o++] = (uint8_t)(w >> 16);
    if (o < sizeof *f) out[o++] = (uint8_t)(w >> 8);
    if (o < sizeof *f) out[o++] = (uint8_t)w;
  }
  return true;
}

void save_read(void) {
  save_defaults();
  uint32_t len;
  const uint8_t *d = plat_load(SAVE_NAME, &len);
  SaveFile f;
  if (d && len >= sizeof f) memcpy(&f, d, sizeof f);
  else if (d || !copy_read(&f)) return;
  if (f.magic != SAVE_MAGIC || f.version != SAVE_VERSION || f.size != sizeof(Save)) return;
  if (f.s.skin < 1 || f.s.skin > NSKINS) return;
  for (int i = 0; i < NLEVELS; i++)
    if (f.s.stars[i] != 0xFF && f.s.stars[i] > 3) return;
  save = f.s;
  save.owned |= 1;
  if (save.stars[0] == 0xFF) save.stars[0] = 0;
}

bool save_write(void) {
  SaveFile f = {SAVE_MAGIC, SAVE_VERSION, sizeof(Save), save};
  bool ok = plat_save(SAVE_NAME, &f, sizeof f);
  return copy_write(&f) && ok;
}

int total_stars(void) {
  int t = 0;
  for (int i = 0; i < NLEVELS; i++)
    if (save.stars[i] != 0xFF) t += save.stars[i];
  return t;
}

const int *shop_prices(void) { return PRICES; }

/* ---- a run ---- */

static void hard_restart(void) {
  g.help = g.level == 0;
  bike_restart();
  level_restart();
  bike_review();
  g.started = false;
  g.timer = 0;
  g.flip_t = 99;
  g.dead_t = 0;
  cam_reset(bike.frame.pos.x, bike.frame.pos.y);
  view_reset();
}

static void start_level(int n) {
  g.level = n;
  if (!level_load(n)) {
    g.state = S_MAP;
    return;
  }
  bike_build(lv.h->startx, lv.h->starty, save.skin);
  level_start();
  hard_restart();
  /* the camera starts on the bike (RaceCamera.hardFocusOnCar) */
  for (int i = 0; i < 20; i++) cam_update(bike.frame.pos.x, bike.frame.pos.y, 0, false);
  g.state = S_PLAY;
}

void game_start_level(int n) { start_level(n); }

void game_flip(int n) {
  g.timer -= 0.5f * n;
  g.flip_n = n;
  g.flip_t = 0;
}

void game_died(void) { g.dead_t = 0; }

/* postRace */
void game_finished(void) {
  int n = g.level;
  float t = g.timer;
  int stars = 0;
  for (int i = 0; i < 3; i++)
    if (t <= lv.h->prize[i]) stars++;
  bool improved;
  if (save.stars[n] == 0xFF || save.best[n] == 0) {
    save.stars[n] = (uint8_t)stars;
    save.best[n] = t;
    improved = true;
  } else {
    if (stars > save.stars[n]) save.stars[n] = (uint8_t)stars;
    improved = t < save.best[n];
    if (improved) save.best[n] = t;
  }
  if (n + 1 < NLEVELS && save.stars[n + 1] == 0xFF) save.stars[n + 1] = 0;
  g.stars = stars, g.improved = improved, g.result = t;
  g.finish_t = 0;
  g.state = S_FINISH;
  g.sel = 1;
  save_write();
}

static void world_tick(uint32_t keys) {
  /* MaxGameCore.updateIternal: the timer, before the world moves */
  if (g.started) {
    if (!bike.dead) g.timer += DT;
  } else if (bike.accell || bike.tilt != 0) {
    g.started = true;
    g.help = false;
  }
  /* the world far from the view keeps still (memory for contacts is short on the calculator) */
  np_window(cam.x - 900, cam.x + cam.w + 900);
  for (int k = 0; k < 2; k++) {
    np_step(DT / 2);
    level_events();
  }
  level_update(DT);
  bike_update(DT, g.state == S_PLAY ? keys : 0);
  fx_update(DT);
  cam_update(bike.x, bike.y, bike.speed, bike.finished);
  if (bike.dead) g.dead_t += DT;
  g.flip_t += DT;
  g.finish_t += DT;
  view_ticks++;
}

static void respawn(void) {
  if (bike.safe_id >= 0) {
    bike_review();
    level_review();
    bike_review();
  } else hard_restart();
}

/* ---- input ---- */

static inline bool pressed(uint32_t k) { return (g.keys & k) && !(g.prev & k); }

static void play_input(void) {
  if (pressed(K_BACK)) {
    g.state = S_PAUSE;
    g.sel = 0;
    g.confirm = false;
    return;
  }
  if (bike.dead && !bike.finished && pressed(K_OK)) respawn();
}

static void pause_input(void) {
  if (g.confirm) {
    if (pressed(K_LEFT) || pressed(K_RIGHT) || pressed(K_UP) || pressed(K_DOWN)) g.sel ^= 1;
    if (pressed(K_BACK)) g.confirm = false, g.sel = 3;
    else if (pressed(K_OK)) {
      if (g.sel == 0) {
        save_write();
        g.running = false;
      } else g.confirm = false, g.sel = 3;
    }
    return;
  }
  if (pressed(K_UP)) g.sel = (g.sel + 3) % 4;
  if (pressed(K_DOWN)) g.sel = (g.sel + 1) % 4;
  if (pressed(K_BACK)) g.state = S_PLAY;
  else if (pressed(K_OK)) {
    switch (g.sel) {
    case 0: g.state = S_PLAY; break;
    case 1: hard_restart(); g.state = S_PLAY; break;
    case 2: g.state = S_MAP; g.map_sel = g.level; break;
    case 3: g.confirm = true, g.sel = 1; break;
    }
  }
}

static int last_open(void) {
  int l = 0;
  for (int i = 0; i < NLEVELS; i++)
    if (save.stars[i] != 0xFF) l = i;
  return l;
}

static void finish_input(void) {
  if (g.finish_t < 0.3f) return;
  if (pressed(K_LEFT)) g.sel = (g.sel + 2) % 3;
  if (pressed(K_RIGHT)) g.sel = (g.sel + 1) % 3;
  int next = g.level + 1 < NLEVELS ? g.level + 1 : g.level;
  if (pressed(K_BACK)) {
    g.state = S_MAP, g.map_sel = next;
    return;
  }
  if (pressed(K_OK)) {
    if (g.sel == 0) g.state = S_MAP, g.map_sel = next;
    else if (g.sel == 1) {
      if (g.level + 1 >= NLEVELS) g.state = S_MAP, g.map_sel = g.level;
      else start_level(g.level + 1);
    } else {
      g.prev_state = S_MAP;
      g.map_sel = next;
      g.state = S_SHOP, g.shop_sel = save.skin - 1;
    }
  }
}

static void map_input(void) {
  int n = g.map_sel;
  if (pressed(K_RIGHT) || pressed(K_DOWN)) n++;
  if (pressed(K_LEFT) || pressed(K_UP)) n--;
  if (n < 0) n = 0;
  if (n > last_open()) n = last_open();
  g.map_sel = n;
  if (pressed(K_BACK)) g.state = S_TITLE, g.sel = 0;
  else if (pressed(K_OK)) start_level(g.map_sel);
}

static void shop_input(void) {
  if (pressed(K_LEFT) && g.shop_sel > 0) g.shop_sel--;
  if (pressed(K_RIGHT) && g.shop_sel < NSKINS - 1) g.shop_sel++;
  if (pressed(K_BACK)) g.state = g.prev_state, g.sel = 1;
  else if (pressed(K_OK)) {
    int k = g.shop_sel;
    if (save.owned & (1 << k)) save.skin = (uint8_t)(k + 1);
    else if (total_stars() >= PRICES[k]) {
      save.owned |= (uint8_t)(1 << k);
      save.skin = (uint8_t)(k + 1);
    }
    save_write();
  }
}

static void title_input(void) {
  if (pressed(K_UP)) g.sel = (g.sel + 2) % 3;
  if (pressed(K_DOWN)) g.sel = (g.sel + 1) % 3;
  if (pressed(K_BACK)) g.running = false;
  else if (pressed(K_OK)) {
    if (g.sel == 0) {
      g.state = S_MAP;
      g.map_sel = last_open();
    } else if (g.sel == 1) {
      g.prev_state = S_TITLE;
      g.state = S_SHOP;
      g.shop_sel = save.skin - 1;
    } else g.state = S_HELP;
  }
}

void game_init(void) {
  memset(&g, 0, sizeof g);
  save_read();
  g.running = true;
  g.state = S_TITLE;
  g.prev = ~0u;   /* keys held at the start do nothing */
#ifdef START_LEVEL
  start_level(START_LEVEL - 1);   /* (speed tests) */
#endif
}

void game_tick(uint32_t keys) {
  g.keys = keys;
  if (keys & K_HOME) {
    save_write();
    g.running = false;
    return;
  }
  switch (g.state) {
  case S_TITLE: title_input(); break;
  case S_MAP: map_input(); break;
  case S_SHOP: shop_input(); break;
  case S_PLAY:
    play_input();
    if (g.state == S_PLAY) world_tick(keys);
    break;
  case S_PAUSE: pause_input(); break;
  case S_HELP:
    if (pressed(K_BACK) || pressed(K_OK)) g.state = S_TITLE, g.sel = 2;
    break;
  case S_FINISH:
    world_tick(0);
    finish_input();
    break;
  }
  if (g.state != S_PLAY && g.state != S_FINISH) view_ticks++;
  g.prev = keys;
}

void game_draw(void) {
  if (g.state == S_PLAY || g.state == S_PAUSE || g.state == S_FINISH) view_prepare();
  gfx_frame(ui_draw);
}
