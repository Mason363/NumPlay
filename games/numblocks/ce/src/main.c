/* NumBlocks on the TI-84 Plus CE: the title, the worlds, and the game's loop (Minecraft's 20 ticks a second, the
 * picture as often as the CE can draw it). */
#include "nb.h"
#include "draw.h"

extern int new_mode;
extern int xp_level, xp_points;
int xp_need(int l);
void gui_close(void);
bool gui_needs_world(void);
void gui_redraw(void);

static uint32_t rng = 0x12345678;
int rnd(int n) {
  rng ^= rng << 13, rng ^= rng >> 17, rng ^= rng << 5;
  return n > 0 ? (int)((rng >> 8) % (uint32_t)n) : 0;
}

void xp_add(int n) {
  xp_points += n;
  while (xp_points >= xp_need(xp_level)) xp_points -= xp_need(xp_level), xp_level++;
}

static bool in_game;
static uint32_t last_ms, tick_acc, ticks_since_save;
static int look_held;

#define TITLE_SEED 20151008u   /* the title's world */

static bool backdrop_ok;
static void title_backdrop(void) {
  /* the title's world: a view over the land, the same each time */
  backdrop_ok = true;
  world_new(TITLE_SEED);
  int x, z;
  gen_spawn(&x, &z);
  memset(&pl, 0, sizeof pl);
  pl.hit_face = -1;
  world_center(x, 40, z, true);
  pl.x = (fx)x * FX1 + FX1 / 2, pl.z = (fx)z * FX1 + FX1 / 2;
  pl.y = (fx)world_top(x, z) * FX1 + FX1 * 2;
  world_time = 3000;
  render_sky_palette();
  render_title(0x3000);
  compose(rbuf, false, false);
}

static void to_title(void) {
  in_game = false;
  gui = GUI_NONE;
  title_backdrop();
  gui_open(GUI_TITLE);
}

static bool seen_controls(void) {
  unsigned len;
  const uint8_t *p = plat_load("NBCEOPT", &len);
  return p && len >= 1 && p[0] == 1;
}

static void start_game(void) {
  in_game = true;
  gui = GUI_NONE;
  render_sky_palette();
  last_ms = plat_ms(), tick_acc = 0, ticks_since_save = 0;
  if (!seen_controls()) {
    static const uint8_t one = 1;
    plat_save("NBCEOPT", &one, 1, 0, 0);
    render_frame();
    compose(rbuf, false, false);
    gui_open(GUI_CONTROLS);
  }
}

void game_save_quit(void) {
  if (in_game) save_world();
}

static void loading(const char *what) {
  canvas_screen();
  dirt_background(0, SCREEN_H);
  text_center(SCREEN_W / 2, SCREEN_H / 2 - 4, what, C_WHITE);
  plat_frame();
}

static void act(void) {
  int a = menu_action;
  menu_action = ACT_NONE;
  switch (a) {
    case ACT_QUIT: plat_exit(); break;
    case ACT_PLAY:
      loading("Loading world");
      if (load_world(cur_world)) start_game();
      else to_title();
      break;
    case ACT_NEW: {
      int s = world_free_slot();
      if (!s) break;
      loading("Building terrain");
      uint32_t seed = plat_ms() * 2654435761u ^ (uint32_t)rnd(0x7FFFFF);
      new_world(s, cur_name, seed, new_mode);
      save_world();
      start_game();
      break;
    }
    case ACT_SAVE_QUIT:
      loading("Saving world");
      save_world();
      to_title();
      break;
    case ACT_RESPAWN:
      player_spawn();
      xp_level = xp_points = 0;
      world_center(FXI(pl.x), FXI(pl.y), FXI(pl.z), true);
      gui = GUI_NONE;
      break;
    case ACT_TITLE:
      save_world();
      to_title();
      break;
  }
}

/* looking around: faster the longer an arrow is held (60 to 180 degrees a second) */
static void look(uint32_t keys, uint32_t dt) {
  int dx = (keys & K_RIGHT ? 1 : 0) - (keys & K_LEFT ? 1 : 0), dy = (keys & K_DOWN ? 1 : 0) - (keys & K_UP ? 1 : 0);
  if (!dx && !dy) {
    look_held = 0;
    return;
  }
  look_held += (int)dt;
  int deg = 60 + (look_held > 600 ? 120 : look_held * 120 / 600);   /* degrees a second */
  int32_t step = (int32_t)deg * 182 * (int32_t)dt / 1000;
  pl.yaw = (uint16_t)(pl.yaw + dx * step);
  int32_t p = pl.pitch + dy * step;
  if (p > 16383) p = 16383;
  if (p < -16383) p = -16383;
  pl.pitch = (int16_t)p;
}

static void play(uint32_t keys, uint32_t pressed) {
  uint32_t now = plat_ms(), dt = now - last_ms;
  last_ms = now;
  if (dt > 250) dt = 250;
  if (gui == GUI_NONE) look(keys, dt);
  tick_acc += dt;
  int n = 0;
  while (tick_acc >= 50 && n < 4) {
    tick_acc -= 50, n++;
    if (gui == GUI_NONE) player_tick(keys, n == 1 ? pressed : 0);
    gui_tick();
    world_time++;
    if (!(world_time & 63)) render_sky_palette();
    if (++ticks_since_save >= 20 * 60 * 5) ticks_since_save = 0, save_world();
  }
  if (tick_acc > 200) tick_acc = 0;
  world_center(FXI(pl.x), FXI(pl.y) + 1, FXI(pl.z), false);
  for (int k = 0; k < 2 && world_busy(); k++) world_step();
  if (gui == GUI_NONE) {
    if (pressed & K_INV) {
      gui_open(pl.mode ? GUI_CREATIVE : GUI_INVENTORY);
      return;
    }
    if (pressed & K_PAUSE) {
      gui_open(GUI_PAUSE);
      return;
    }
    render_frame();
    hud_draw();
    compose(rbuf, true, true);
  }
}

int main(void) {
  plat_init();
  wblk = plat_spare();
  wlight = wblk + WS * WS * WH;
  render_init();
  canvas_screen();
#ifdef HOST
  /* (tests: straight into a new world, NB_NEW=seed,mode[,x,y,z,yaw,pitch,time]) */
  extern bool host_start(uint32_t *seed, int *mode, int *at);
  uint32_t hseed;
  int hmode, at[6];
  if (host_start(&hseed, &hmode, at)) {
    strcpy(cur_name, "Test");
    new_world(1, cur_name, hseed, hmode);
    if (at[0] != -9999) {
      pl.x = (fx)at[0] * FX1 + FX1 / 2, pl.y = (fx)at[1] * FX1, pl.z = (fx)at[2] * FX1 + FX1 / 2;
      pl.yaw = (uint16_t)at[3], pl.pitch = (int16_t)at[4], world_time = (uint32_t)at[5];
      pl.flying = pl.mode;
      world_center(at[0], at[1], at[2], true);
    }
    in_game = true;
    render_sky_palette();
    last_ms = plat_ms();
  } else
#endif
  to_title();
  uint32_t prev = ~0u;
  for (;;) {
    uint32_t keys = plat_keys(), pressed = keys & ~prev;
    prev = keys;
    if (keys & K_HOME) {
      if (in_game) {
        loading("Saving world");
        save_world();
      }
      break;
    }
    if (gui != GUI_NONE) gui_input(keys, pressed);
    if (menu_action) act();
    if (in_game) play(keys, pressed);
    /* back on the title from another menu: its world behind it again */
    if (gui != GUI_TITLE) backdrop_ok = false;
    else if (!backdrop_ok && !in_game) title_backdrop(), gui_redraw();
    gui_draw();
    plat_frame();
  }
  plat_exit();
  return 0;
}
