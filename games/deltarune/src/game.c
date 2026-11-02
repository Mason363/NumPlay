#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif
/* The game's modes and its frame (game.h): the title, the overworld, battles, menus. */
#include <string.h>
#include "battle.h"
#include "event.h"
#include "game.h"
#include "gfx.h"
#include "menu.h"
#include "plat.h"
#include "res.h"
#include "rooms.h"
#include "save.h"
#include "text.h"
#include "world.h"

Save sv;
int g_mode;
uint32_t g_frame;
bool g_running = true;
uint32_t g_pressed, g_held;
static uint32_t prev_keys;
static uint32_t seed = 0x2018;

uint32_t rnd(void) {
  seed ^= seed << 13, seed ^= seed >> 17, seed ^= seed << 5;
  return seed;
}
int rnd_n(int n) { return n > 0 ? (int)(rnd() % (uint32_t)n) : 0; }

bool have_char(int c) { return sv.chr[0] == c || sv.chr[1] == c || sv.chr[2] == c; }
int party_count(void) { return !!sv.chr[0] + !!sv.chr[1] + !!sv.chr[2]; }
void party_add(int c) {
  if (have_char(c)) return;
  for (int i = 0; i < 3; i++)
    if (!sv.chr[i]) {
      sv.chr[i] = (uint8_t)c;
      break;
    }
  follow_reset();
}
void party_remove(int c) {
  for (int i = 0; i < 3; i++)
    if (sv.chr[i] == c) {
      for (int k = i; k < 2; k++) sv.chr[k] = sv.chr[k + 1];
      sv.chr[2] = 0;
      break;
    }
  follow_reset();
}

/* scr_gamestart: the party's first stats */
void game_new(void) {
  char name[12];
  memcpy(name, sv.name, sizeof name);
  memset(&sv, 0, sizeof sv);
  memcpy(sv.name, name, sizeof name);
  sv.chr[0] = CH_KRIS;
  sv.lv = 1;
  sv.hp[CH_KRIS] = sv.maxhp[CH_KRIS] = 90, sv.at[CH_KRIS] = 10, sv.df[CH_KRIS] = 2;
  sv.hp[CH_SUSIE] = sv.maxhp[CH_SUSIE] = 110, sv.at[CH_SUSIE] = 14, sv.df[CH_SUSIE] = 2, sv.mag[CH_SUSIE] = 1;
  sv.hp[CH_RALSEI] = sv.maxhp[CH_RALSEI] = 70, sv.at[CH_RALSEI] = 8, sv.df[CH_RALSEI] = 2, sv.mag[CH_RALSEI] = 7;
  sv.weapon[CH_KRIS] = 1, sv.weapon[CH_SUSIE] = 2, sv.weapon[CH_RALSEI] = 3;
  sv.keyitem[0] = 1;   /* the Cell Phone */
  sv.lweapon = 2, sv.larmor = 3;   /* the Pencil and the Bandage (scr_gamestart's light items) */
  sv.lhp = sv.lmaxhp = 20;
  sv.lgold = 2;
  g_mode = MODE_WORLD;
  event_cancel();
  world_enter(room_krisroom, 246, 97, DIR_LEFT);
}

/* ---------------------------------------------------------------- the title */
static int title_sel, title_t;
static bool have_save;

static void title_update(void) {
  title_t++;
  int n = have_save ? 2 : 1;
  if (g_pressed & (K_UP | K_LEFT)) title_sel = (title_sel + n - 1) % n;
  if (g_pressed & (K_DOWN | K_RIGHT)) title_sel = (title_sel + 1) % n;
  if (g_pressed & K_Z) {
    if (have_save && title_sel == 0) {
      if (save_load()) return;
    }
    menu_name_start();
  }
}

static void title_draw(void) {
  gfx_rect(0, 0, SCREEN_W, SCREEN_H, C_BLACK, 255);
  gfx_sprite(IMAGE_LOGO, 0, 160 - spr_w(IMAGE_LOGO) / 2, 70, 0, 255);
  static const char *opts[2] = {"Continue", "New game"};
  int n = have_save ? 2 : 1;
  for (int i = 0; i < n; i++) {
    const char *s = have_save ? opts[i] : opts[1];
    int w = gfx_text_width(FONT_MAIN, s, 32);
    uint16_t col = title_sel == i ? C_YELLOW : C_WHITE;
    gfx_text(FONT_MAIN, 160 - w / 2, 150 + 22 * i, s, col, 255);
    if (title_sel == i) gfx_sprite(spr_heartsmall, 0, 160 - w / 2 - 16, 154 + 22 * i, 0, 255);
  }
  static const char keys[] = "OK confirm  Back cancel/run  Toolbox menu  Home save and quit";
  gfx_text(FONT_SMALL, 160 - gfx_text_width(FONT_SMALL, keys, 80) / 2, 228, keys, rgb565(0x808080), 255);
}

void game_init(void) {
  save_restore_copy();
  config_load();
  have_save = save_exists();
  g_mode = MODE_TITLE;
  if (save_resume_load()) return;
}

void game_quit_save(void) {
  if (g_mode == MODE_WORLD || g_mode == MODE_MENU || g_mode == MODE_BATTLE) save_resume_write();
  g_running = false;
}

void game_frame(uint32_t keys) {
  g_pressed = keys & ~prev_keys;
  g_held = keys;
  prev_keys = keys;
  g_frame++;
  if (g_pressed & K_HOME) {
    game_quit_save();
    return;
  }
  if (g_mode != MODE_TITLE) sv.time++;
  switch (g_mode) {
    case MODE_TITLE: title_update(); break;
    case MODE_INTRO: menu_name_update(); break;
    case MODE_WORLD: {
      bool talking = text_active();
      text_update(g_pressed, g_held);
      if (talking) g_pressed &= ~(uint32_t)(K_Z | K_C);
      event_update();
      if (g_mode != MODE_WORLD) break;
      world_update();
      room_step();
      break;
    }
    case MODE_BATTLE: battle_update(g_pressed, g_held); break;
    case MODE_MENU: menu_update(); break;
    case MODE_GAMEOVER: menu_gameover_update(); break;
    default: break;
  }
}

void game_draw(void) {
  gfx_begin();
  switch (g_mode) {
    case MODE_TITLE: title_draw(); break;
    case MODE_INTRO: menu_name_draw(); break;
    case MODE_WORLD: world_draw(); break;
    case MODE_BATTLE: battle_draw(); break;
    case MODE_MENU: menu_draw(); break;
    case MODE_GAMEOVER: menu_gameover_draw(); break;
    default: break;
  }
  gfx_end();
}
