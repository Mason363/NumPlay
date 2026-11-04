/* Among Us for the NumWorks calculator.
 *
 * Inspired by Among Us by Innersloth, not affiliated with them.
 * Made by Mason Chen as part of NumPlay. */
#include "game.h"
#include "gfx.h"
#include "plat.h"

#ifndef HOST
const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "Among Us";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;

uint32_t perf_frames __attribute__((used));   /* read by tools/emu.py */
uint32_t perf_max __attribute__((used));
uint32_t perf_slow __attribute__((used));   /* frames over 33 ms */
extern bool game_running;

#ifdef PERF_SCENE
/* (measuring builds only: make EXTRA=-DPERF_SCENE=n starts a round in a scene) */
#include "mg.h"
#include "world.h"
static void perf_scene(int n) {
  g.opt.role = n == 9 ? 2 : 1;
  game_new();
  g.screen = S_PLAY;
  int imp = 1;
  while (!g.p[imp].imp) imp++;
  for (int k = 0; k < 90; k++) game_tick(0);
  switch (n) {
  case 1: kill_player(imp, 0); break;
  case 2: kill_player(imp, 2), start_meeting(0, 2); break;
  case 3: start_meeting(0, NONE), g.screen = S_MEETING, g.meet_t = 0; bot_meeting_start(); break;
  case 4: map_open(O_MAP); break;
  case 5: g.sab_cd = 0, sabotage(SAB_LIGHTS); break;
  case 6: for (int c = 0; c < N_CONSOLES; c++) if (CONSOLES[c].kind == 3) g.p[0].pos = (V2){CONSOLES[c].sx, CONSOLES[c].sy}, mg_open(c, MAX_TASKS); break;
  case 7: g.p[0].pos = (V2){-21.0f, -5.0f}; break;   /* the reactor room */
  case 8: g.p[0].pos = (V2){-8.0f, -10.0f}; break;   /* electrical */
  case 9: map_open(O_SABMAP); break;
  }
  g.cam = (V2){g.p[0].pos.x, g.p[0].pos.y + FEET_Y};
}
#endif

int main(void) {
  /* the band drawn into lives on main's stack */
  uint16_t band[SCREEN_W * BAND];
  gfx_init(band);
  plat_begin();
  game_init();
#ifdef PERF_SCENE
  perf_scene(PERF_SCENE);
#endif
  /* the game's fixed 30 ticks a second (TimeManager's Fixed Timestep), at most 3 a drawing */
  uint32_t next = plat_millis() * 3;   /* in thirds of a millisecond: 100 a tick */
  while (game_running) {
    int n = 0;
    uint32_t now = plat_millis() * 3;
    while ((int32_t)(now - next) >= 0 && n < 3 && game_running) {
      game_tick(plat_keys());
      next += 100;
      n++;
    }
    if (!game_running) break;
    if ((int32_t)(now - next) > 300) next = now;   /* far behind: do not rush */
    if (n) {
      uint32_t t0 = plat_millis();
      game_draw();
      uint32_t dt = plat_millis() - t0;
      if (dt > perf_max && perf_frames > 30) perf_max = dt;
      if (dt > 33 && perf_frames > 30) perf_slow++;
      perf_frames++;
    } else {
      plat_sleep(1);
    }
  }
  return plat_end();
}
#endif
