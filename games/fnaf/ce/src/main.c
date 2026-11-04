/* Five Nights at Freddy's for the TI-84 Plus CE.
 *
 * Inspired by Five Nights at Freddy's by Scott Cawthon, not affiliated with him.
 * Made by Mason Chen as part of NumPlay. */
#include <string.h>
#include "fnaf.h"

static const uint8_t *vars[DATA_VARS];
uint8_t *scene, *extra;
int scene_pic = -1, extra_pic = -1;
bool keep_frame;
static int still_pic = -1;

const uint8_t INK_WHITE[4] = {0, UI_WHITE1, UI_WHITE2, UI_WHITE};
const uint8_t INK_GREY[4] = {0, UI_GREY1, UI_GREY2, UI_GREY};
const uint8_t INK_SILVER[4] = {0, UI_GREY1, UI_GREY2, UI_SILVER};
const uint8_t INK_DIM[4] = {0, UI_GREY1, UI_GREY1, UI_DIM};
const uint8_t INK_CAPTION[4] = {0, UI_CAPTION1, UI_CAPTION2, UI_CAPTION};
const uint8_t INK_CALL[4] = {0, UI_CALL1, UI_CALL2, UI_CALL};
const uint8_t INK_MINT[4] = {0, UI_MINT1, UI_MINT2, UI_MINT};
const uint8_t INK_BLACK[4] = {0, UI_BLACK, UI_BLACK, UI_BLACK};

Ink ink_fade(Ink ink, int a) {
  static uint8_t out[4];
  if (a >= 32) return ink;
  if (a < 6) return INK_BLACK;
  /* each level one step down, or two: what is left of the ink over black */
  int k = a >= 20 ? 1 : 2;
  out[0] = 0;
  for (int i = 1; i <= 3; i++) out[i] = i - k >= 1 ? ink[i - k] : UI_BLACK;
  return out;
}

/* ------------------------------------------------------------------ pictures in RAM */
void scene_want(int pic) {
  if (scene_pic == pic) return;
  /* the frames (or base) it is coded over: from the one scene holds, if it is among them */
  int chain[24], n = 0;
  for (int p = pic; p >= 0 && n < 24; p = pic_base(asset(PIC_FIRST + p))) {
    chain[n++] = p;
    if (p == scene_pic) break;
  }
  int from = n - 1;
  if (chain[from] == scene_pic) from--;
  scene_pic = -1;
  bool wide = pic_w(pic) == SCR_W / 2;
  for (int k = from; k >= 0; k--)
    if (wide) pic_into_wide(chain[k], scene, SCR_W);
    else pic_into(chain[k], scene, SCENE_W);
  scene_pic = pic;
}

static int extra_gen;   /* (extra is also where pictures unpack their grids: gfx_temp_uses) */
void extra_want(int pic, int stride) {
  if (extra_pic == pic && extra_gen == gfx_temp_uses) return;
  pic_full(pic, extra, stride);
  extra_pic = pic, extra_gen = gfx_temp_uses;
}

bool still(int pic) {
  if (still_pic == pic) return true;
  pic_full(pic, scr, SCR_W);
  still_pic = pic;
  keep_frame = true;
  return false;
}

void still_forget(void) { still_pic = -1; }

/* ------------------------------------------------------------------ the keys, the time */
uint32_t fnaf_keys(void) {
  uint32_t c = ce_keys(), k = 0;
  if (c & CK_LEFT) k |= K_LEFT;
  if (c & CK_RIGHT) k |= K_RIGHT;
  if (c & CK_UP) k |= K_UP;
  if (c & CK_DOWN) k |= K_DOWN;
  if (c & (CK_2ND | CK_ENTER)) k |= K_OK;
  if (c & CK_CLEAR) k |= K_BACK;
  if (c & (CK_MODE | CK_ON)) k |= K_HOME;
  if (c & CK_DEL) k |= K_DEL;
  for (int n = 0; n < 10; n++)
    if (c & CK_DIGIT(n)) k |= K_DIGIT(n);
  if (c) k |= K_ANY;
  return k;
}

uint32_t plat_millis(void) {
  uint32_t t = ce_ticks();
  return (t >> 15) * 1000 + ((t & 32767) * 1000 >> 15);
}

/* ------------------------------------------------------------------ starting */
static const Assets assets = {vars, asset_at, pic_group, PIC_FIRST, MAP_FIRST, FONT_FIRST};

/* 0: ready; 1: data missing (ce_missing); 2: not enough RAM */
int fnaf_start(void) {
  if (!ce_data(DATA_VAR, DATA_VARS, vars)) return 1;
  save_open();   /* (before the RAM is taken: the save is made there) */
  size_t n;
  scene = ce_ram(&n);
  if (n < RAM_NEEDED) return 2;
  extra = scene + SCENE_W * SCENE_H;
  gfx_init(&assets, extra, 160 * 120);
  game_init();
  return 0;
}

#ifndef HOST
void ce_message(const char *a, const char *b);

int main(void) {
  ce_init();
  int r = fnaf_start();
  if (r) {
    ce_message(r == 1 ? "Missing appvar:" : "Not enough RAM.", r == 1 ? ce_missing : "Archive some files.");
    return 0;
  }
  uint32_t last = ce_ticks(), acc = 0;
  while (g.running) {
    /* the game's steps, 60 a second (the 32768 Hz clock), then a frame */
    uint32_t now = ce_ticks();
    acc += (now - last) * 60;
    last = now;
    int steps = 0;
    while (acc >= 32768 && steps < 8 && g.running) {
      game_step(fnaf_keys());
      acc -= 32768;
      steps++;
    }
    if (acc > 32768 * 8) acc = 0;   /* far behind: no rush */
    if (!g.running) break;
    if (steps) {
      ce_wait();
      game_draw();
      ce_show(keep_frame);
      keep_frame = false;
    }
  }
  ce_quit();
  return 0;
}
#endif
