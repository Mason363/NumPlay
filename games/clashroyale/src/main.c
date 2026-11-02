/* Clash Royale for the NumWorks calculator.
 *
 * Inspired by Clash Royale by Supercell, not affiliated with Supercell.
 * Made by Mason Chen as part of NumPlay. */
#include "game.h"
#include "gfx.h"
#include "plat.h"
#include "res.h"

#ifndef HOST
const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "Clash Royale";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;
extern const uint8_t cr_data[];
const uint8_t *cr_bin;

uint32_t perf_frames __attribute__((used));   /* read by tools/emu.py */
uint32_t perf_updates __attribute__((used));
uint32_t perf_max __attribute__((used));      /* the slowest frame's steps and drawing, ms */
uint32_t perf_slow __attribute__((used));     /* frames over 33 ms */

#ifdef CR_DEMO
extern bool bot_plays_both;   /* (make EXTRA=-DCR_DEMO: a bot plays your side too, for timing many units) */
#endif

int main(void) {
  uint16_t band[SCREEN_W * BAND_H];   /* (here: on the stack, out of the RAM apps get) */
#ifdef CR_DEMO
  bot_plays_both = true;
#endif
  gfx_set_band_memory(band);
  plat_begin();
  cr_bin = cr_data;
  game_init();
  uint32_t next = plat_millis() * 3;   /* in thirds of a millisecond: 50 a step, 60 steps a second */
  while (g.running) {
    /* the game's steps: drawn when caught up (at most 6 steps, two battle steps, for a drawing) */
    int n = 0;
    uint32_t t0 = plat_millis();
    uint32_t now = t0 * 3;
    while ((int32_t)(now - next) >= 0 && n < 6 && g.running) {
      game_step(plat_keys());
      perf_updates++;
      next += 50;
      n++;
    }
    if (!g.running) break;
    if ((int32_t)(now - next) > 600) next = now;   /* far behind: do not rush */
    if (n) {
      game_draw();
      perf_frames++;
      uint32_t dt = plat_millis() - t0;
      if (dt > perf_max) perf_max = dt;
      if (dt > 33) perf_slow++;
    } else
      plat_sleep(1);
  }
  return plat_end();
}
#endif
