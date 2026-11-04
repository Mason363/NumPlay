/* Imposter for the NumWorks calculator (and NumPlay's other targets): the party game played by passing one
 * calculator around.
 *
 * Inspired by the party game Imposter. Made by Mason Chen as part of NumPlay. */
#include "game.h"
#include "plat.h"
#include "ui.h"

#ifndef HOST
uint32_t perf_frames __attribute__((used)); /* read by tools/emu.py */
uint32_t perf_max __attribute__((used));

int main(void) {
  plat_begin();
  app_init(plat_millis());
  for (;;) {
    uint32_t letters, keys = plat_keys(&letters);
    if (!app_step(keys, letters, plat_millis())) break;
    if (app_dirty()) {
      plat_vblank();
      uint32_t t0 = plat_millis();
      app_draw();
      uint32_t dt = plat_millis() - t0;
      if (dt > perf_max) perf_max = dt;
      perf_frames++;
    } else {
      plat_sleep(10);
    }
  }
  return plat_end();
}
#endif
