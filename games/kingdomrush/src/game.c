/* The screens: what each key does, a step at 30 a second (the game's frame rate). */
#include <string.h>
#include "game.h"
#include "gfx.h"
#include "kr.h"

Game g;
static uint32_t last_keys, repeat_key;
static int repeat_t;

void game_init(void) {
  memset(&g, 0, sizeof g);
  g.running = true;
  load_progress();
  g.screen = SCR_TITLE;
}

void game_quit(void) {
  save_progress();
  g.running = false;
}

/* a key pressed this step; the arrows repeat while held (after 8 steps, then every 3) */
bool play_key(uint32_t k) {
  if (g.pressed & k) return true;
  return (k & (K_LEFT | K_RIGHT | K_UP | K_DOWN)) && repeat_key == k && repeat_t >= 8 && (repeat_t - 8) % 3 == 0;
}

void game_step(uint32_t keys) {
  g.ticks++;
  g.keys = keys;
  g.pressed = keys & ~last_keys;
  last_keys = keys;
  uint32_t arrows = keys & (K_LEFT | K_RIGHT | K_UP | K_DOWN);
  if (arrows && arrows == repeat_key) repeat_t++;
  else repeat_key = arrows, repeat_t = 0;
  if (g.pressed & K_HOME) {
    game_quit();
    return;
  }
  switch (g.screen) {
  case SCR_TITLE: title_step(); break;
  case SCR_MAP: map_step(); break;
  case SCR_UPGRADES: upgrades_step(); break;
  case SCR_BATTLE: play_step(); break;
  case SCR_HELP: help_step(); break;
  case SCR_HEROES: heroes_step(); break;
  }
}

void game_draw(void) {
  switch (g.screen) {
  case SCR_TITLE: title_draw(); break;
  case SCR_MAP: map_draw(); break;
  case SCR_UPGRADES: upgrades_draw(); break;
  case SCR_BATTLE: play_draw(); break;
  case SCR_HELP: help_draw(); break;
  case SCR_HEROES: heroes_draw(); break;
  }
}
