/* Moto X3M for the NumWorks calculator.
 *
 * Inspired by Moto X3M by Madpuffers, not affiliated with them.
 * Made by Mason Chen as part of NumPlay. */
#include "game.h"
#include "gfx.h"
#include "plat.h"
#include "view.h"

#ifndef HOST
#ifndef NP_PORT /* (another target: platform/port.mk names the app) */
const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "Moto X3M";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;
#endif

uint32_t perf_frames __attribute__((used));   /* read by tools/emu.py */
uint32_t perf_max __attribute__((used));

int main(void) {
  /* the band drawn into lives on main's stack, leaving the app's RAM to the physics */
  uint16_t band[SCREEN_W * BAND];
  uint32_t visbuf[VIEW_VIS_BYTES / 4];
  float scrbuf[VIEW_SCR_FLOATS];
  gfx_init(band);
  view_init(visbuf, scrbuf);
  plat_begin();
  game_init();
  /* the game's ticks at 30 a second (GameplayState.DESIRED_FRAME_TIME), at most 3 a drawing (MAX_FRAME_TIME) */
  uint32_t next = plat_millis() * 3;   /* in thirds of a millisecond: 100 a tick */
  while (g.running) {
    int n = 0;
    uint32_t now = plat_millis() * 3;
    while ((int32_t)(now - next) >= 0 && n < 3 && g.running) {
      game_tick(plat_keys());
      next += 100;
      n++;
    }
    if (!g.running) break;
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
  save_write();
  return plat_end();
}
#endif
