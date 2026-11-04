/* Hollow Knight for the NumWorks calculator.
 *
 * Inspired by Hollow Knight, not affiliated with Team Cherry.
 * Made by Mason Chen as part of NumPlay. */
#include "game.h"

#ifndef HOST
#ifndef NP_PORT /* (another target: platform/port.mk names the app) */
#ifdef HK_FW
const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "HK Fungal Wastes";
#else
const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "Hollow Knight";
#endif
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;
#endif
extern const uint8_t hk_data[];

void plat_begin(void);
int plat_end(void);

uint32_t perf_frames __attribute__((used));   /* read by tools/emu.py */
uint32_t perf_updates __attribute__((used));

#define TICK_MS 20      /* the game's step (Unity's fixed step) */
#define MAX_TICKS 4     /* steps caught up before a frame is drawn */
#define SLOW_MS 34      /* a frame's work (on average) over which the room is drawn fast (g_gfx_fast): under 29 fps... */
#define QUICK_MS 18     /* ...and under which, fast, it is drawn whole again (whole, under 34: not back and forth) */

#ifdef PERF_BENCH
/* timing: a few views (rooms by number), panning a little: each one's ms per frame x 100 (read by the emulator; the
 * last view there only so that it reads the one before) */
static const struct { int room; float x, y; } bench[] = {{0, 30, 14.11f}, {0, 186, 68}, {0, 60, 30}, {5, 52, 10}, {35, 14, 104},
                                                        {10, 22, 25}, {32, 18, 66}, {20, 25, 22}, {1, 82, 10}, {0, 30, 14}};
uint32_t perf_bench[sizeof bench / sizeof bench[0]] __attribute__((used));
static int run(void) {
  plat_begin();
  hk_bin = hk_data;
#ifdef PERF_FAST
  g_gfx_fast = true;
#endif
  for (unsigned i = 0; i < sizeof bench / sizeof bench[0]; i++) {
    room_load(bench[i].room);
    g_cam_x = bench[i].x, g_cam_y = bench[i].y;
    for (int f = 0; f < 3; f++) gfx_frame();
    uint32_t t0 = plat_millis();
    for (int f = 0; f < 12; f++) {
      g_cam_x += 0.1f;
      gfx_frame();
      perf_frames++;
    }
    perf_bench[i] = (plat_millis() - t0) * 100 / 12;
  }
  return plat_end();
}
#elif defined(PERF_SURVEY)
/* timing: every room, the view at each of its gates and its middle, panning a little; per room: frames, ms, the
 * slowest spot's ms per frame (x10) and where (read by the emulator) */
uint32_t perf_survey[NUM_ROOMS][5] __attribute__((used));
static int run(void) {
  plat_begin();
  hk_bin = hk_data;
  for (int r = 0; r < NUM_ROOMS; r++) {
    if (!room_load(r)) continue;
    float w = g_room.h->w, h = g_room.h->h;
    float spots[33][2];
    int ns = 0, n;
    const Ent *es = room_ents(&n);
    spots[ns][0] = w / 2, spots[ns][1] = h / 2, ns++;
    for (int i = 0; i < n && ns < 33; i++)
      if (es[i].type == ENT_GATE) spots[ns][0] = (es[i].x0 + es[i].x1) / 2, spots[ns][1] = (es[i].y0 + es[i].y1) / 2, ns++;
    for (int k = 0; k < ns; k++) {
      float cx = spots[k][0], cy = spots[k][1];
      cx = cx < 14.6f ? 14.6f : cx > w - 14.6f ? w - 14.6f : cx;
      cy = cy < 8.3f ? 8.3f : cy > h - 8.3f ? h - 8.3f : cy;
      g_cam_x = cx, g_cam_y = cy;
      gfx_frame();   /* (the tiles in) */
      uint32_t t0 = plat_millis();
      for (int f = 0; f < 8; f++) {
        g_cam_x = cx + 0.15f * f;
        gfx_frame();
        perf_frames++;
      }
      uint32_t ms = plat_millis() - t0;
      perf_survey[r][0] += 8, perf_survey[r][1] += ms;
      if (ms * 10 / 8 > perf_survey[r][2]) perf_survey[r][2] = ms * 10 / 8, perf_survey[r][3] = (uint32_t)cx, perf_survey[r][4] = (uint32_t)cy;
    }
  }
  return plat_end();
}
#elif defined(PERF_ROOM) && !defined(PERF_PLAY)
/* timing: the camera panning across a room */
#ifndef PERF_X
#define PERF_X 30
#endif
#ifndef PERF_Y
#define PERF_Y 40
#endif
#ifndef PERF_DX
#define PERF_DX 0.15f
#endif
static int run(void) {
  plat_begin();
  hk_bin = hk_data;
  plat_fill(0, 0, SCREEN_W, SCREEN_H, 0);
  room_load(PERF_ROOM);
  g_cam_x = PERF_X, g_cam_y = PERF_Y;
  for (;;) {
    if (plat_keys() & K_HOME) break;
    gfx_frame();
    perf_frames++, perf_updates++;
    g_cam_x += PERF_DX;
    if (g_cam_x > g_room.h->w - 14.6f) g_cam_x = 14.6f;
  }
  return plat_end();
}
#elif defined(PERF_PLAY)
/* timing: the game itself, from a place in a room (PERF_ROOM, PERF_X, PERF_Y), with the keys the emulator holds */
static int run(void) {
  plat_begin();
  hk_bin = hk_data;
  plat_fill(0, 0, SCREEN_W, SCREEN_H, 0);
  game_new();
  game_enter(PERF_ROOM, PERF_X, PERF_Y, true);
  uint32_t last = plat_millis(), acc = 0, avg8 = 0;
  for (;;) {
    uint32_t keys = plat_keys();
    if (keys & K_HOME) break;
    uint32_t now = plat_millis();
    acc += now - last, last = now;
    int ticks = 0;
    while (acc >= TICK_MS && ticks < MAX_TICKS) {
      game_tick(keys);
      acc -= TICK_MS, ticks++, perf_updates++;
    }
    if (ticks == MAX_TICKS) acc = 0;
    if (!ticks) {
      plat_sleep(TICK_MS - acc);
      continue;
    }
    game_draw_layers();
    gfx_frame();
    perf_frames++;
    avg8 = avg8 - avg8 / 8 + (plat_millis() - now);
    if (!g_gfx_fast && avg8 > SLOW_MS * 8) g_gfx_fast = true;
    else if (g_gfx_fast && avg8 < QUICK_MS * 8) g_gfx_fast = false;
  }
  return plat_end();
}
#else
static int run(void) {
  plat_begin();
  hk_bin = hk_data;
  plat_fill(0, 0, SCREEN_W, SCREEN_H, 0);
  menu_start();
  uint32_t last = plat_millis(), acc = 0, avg8 = 0;   /* (the frames' work: ms x 8, an average of the last few) */
  for (;;) {
    uint32_t keys = plat_keys();
    if (keys & K_HOME) {
      /* (as Quit to Menu: saved, when the pause menu could be opened, or in the inventory: charms just changed kept) */
      if (menu_in_game() && (!g_pd.disable_pause || inv_settle()) && !game_changing_room()) save_game();
      break;
    }
    if (menu_quit()) break;
    uint32_t now = plat_millis();
    acc += now - last, last = now;
    int ticks = 0;
    while (acc >= TICK_MS && ticks < MAX_TICKS) {
      if (!menu_tick(keys)) game_tick(stag_tick(shop_tick(inv_tick(keys))));
      acc -= TICK_MS, ticks++, perf_updates++;
    }
    if (ticks == MAX_TICKS) acc = 0;   /* (too slow to catch up: the game slows down instead) */
    if (!ticks) {
      plat_sleep(TICK_MS - acc);
      continue;
    }
    /* (the game, or the title screen and the save profiles alone; the pause menu over the game) */
    g_gfx_no_room = !menu_in_game();
    if (menu_in_game()) game_draw_layers();
    menu_draw();
    gfx_frame();
    perf_frames++;
    if (menu_in_game()) {
      avg8 = avg8 - avg8 / 8 + (plat_millis() - now);
      if (!g_gfx_fast && avg8 > SLOW_MS * 8) g_gfx_fast = true;
      else if (g_gfx_fast && avg8 < QUICK_MS * 8) g_gfx_fast = false;
    }
  }
  return plat_end();
}
#endif

/* The decoders' scratch memory on the stack: the calculator gives apps 32 KB of it, of which the game uses under 5 KB,
 * while the RAM it gives them is what its software's own variables leave (153676 bytes on 25.2, 148928 on 23.2), and
 * the installer refuses an app that needs more. */
int main(void) {
  uint16_t scratch[SCRATCH_N] __attribute__((aligned(4)));
  g_scratch = scratch;
  return run();
}
#endif
