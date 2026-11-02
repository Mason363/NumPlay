/* Kingdom Rush for the NumWorks calculator.
 *
 * Inspired by Kingdom Rush by Ironhide Game Studio, not affiliated with them.
 * Made by Mason Chen as part of NumPlay. */
#include "game.h"
#include "gfx.h"
#include "plat.h"
#include "res.h"

#ifndef HOST
const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "Kingdom Rush";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;
extern const uint8_t kr_data[];
const uint8_t *kr_bin;

uint32_t perf_frames __attribute__((used));   /* read by tools/emu.py */
uint32_t perf_updates __attribute__((used));

int main(void) {
  uint16_t band[SCREEN_W * BAND_H];   /* (on the stack, out of the RAM apps get) */
  gfx_set_band_memory(band);
  plat_begin();
  kr_bin = kr_data;
  game_init();
  uint32_t next = plat_millis() * 3;   /* in thirds of a millisecond: 100 a step, 30 steps a second */
  while (g.running) {
    /* the game's steps: drawn when caught up (at most 3 steps for a drawing) */
    int n = 0;
    uint32_t now = plat_millis() * 3;
    while ((int32_t)(now - next) >= 0 && n < 3 && g.running) {
      game_step(plat_keys());
      perf_updates++;
      next += 100;
      n++;
    }
    if (!g.running) break;
    if ((int32_t)(now - next) > 600) next = now;   /* far behind: do not rush */
    if (n) {
      game_draw();
      perf_frames++;
    } else
      plat_sleep(1);
  }
  return plat_end();
}
#endif
