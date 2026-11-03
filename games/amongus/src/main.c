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
extern bool game_running;

int main(void) {
  /* the band drawn into lives on main's stack */
  uint16_t band[SCREEN_W * BAND];
  gfx_init(band);
  plat_begin();
  game_init();
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
      perf_frames++;
    } else {
      plat_sleep(1);
    }
  }
  return plat_end();
}
#endif
