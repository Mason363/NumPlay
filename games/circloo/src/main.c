/* circloO for the NumWorks calculator.
 *
 * Inspired by circloO by Florian van Strien, not affiliated with him.
 * Made by Mason Chen as part of NumPlay. */
#include "game.h"
#include "gfx.h"
#include "plat.h"

#ifndef HOST
#ifndef NP_PORT /* (another target: platform/port.mk names the app) */
const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "circloO";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;
#endif

uint32_t perf_frames __attribute__((used));   /* read by tools/emu.py */
uint32_t perf_max __attribute__((used));

int main(void) {
  uint16_t band[SCREEN_W * BAND];   /* (on main's stack: the app's RAM goes to the physics) */
  gfx_init(band);
  plat_begin();
  game_init();
  /* the game's 60 steps a second, at most 4 a drawing */
  uint32_t next = plat_millis() * 3;   /* in thirds of a millisecond: 50 a step */
  while (g.running) {
    int n = 0;
    uint32_t keys = plat_keys();
    if (keys & K_HOME) break;
    uint32_t now = plat_millis() * 3;
    while ((int32_t)(now - next) >= 0 && n < 4 && g.running) {
      game_tick(keys);
      next += 50;
      n++;
    }
    if (!g.running) break;
    if ((int32_t)(now - next) > 200) next = now;   /* far behind: do not rush */
    if (n) {
      uint32_t t0 = plat_millis();
      game_draw();
      uint32_t dt = plat_millis() - t0;
      if (dt > perf_max && perf_frames > 30) perf_max = dt;
      perf_frames++;
    } else {
      plat_sleep(1);
    }
  }
  save_write();
  return plat_end();
}
#endif
