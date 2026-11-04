/* 2 3 4 Players for the NumWorks calculator (and NumPlay's other targets): one-key minigames for 2, 3 or 4
 * players on one calculator, and teams, 2 v 2 and 3 v 3.
 *
 * Inspired by the party app 2 3 4 Player Games. Made by Mason Chen as part of NumPlay. */
#include "app.h"
#include "plat.h"

#ifndef HOST
uint32_t perf_frames __attribute__((used)); /* read by tools/emu.py */
uint32_t perf_max __attribute__((used));
uint32_t perf_slow __attribute__((used));

int main(void) {
  plat_begin();
  app_init(plat_millis());
  uint32_t last = plat_millis();
  for (;;) {
    if (!app_step(plat_keys(), plat_millis())) break;
    if (app_dirty()) {
      plat_vblank();
      uint32_t t0 = plat_millis();
      app_draw();
      uint32_t t = plat_millis(), dt = t - t0;
      if (dt > perf_max) perf_max = dt;
      if (t - last > 33) perf_slow++;
      last = t;
      perf_frames++;
    } else {
      plat_sleep(10);
      last = plat_millis();
    }
  }
  return plat_end();
}
#endif
