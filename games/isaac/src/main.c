/* The Binding of Isaac for the NumWorks calculator.
 *
 * Inspired by The Binding of Isaac by Edmund McMillen and Florian Himsl, not
 * affiliated with them. Made by Mason Chen as part of NumPlay. */
#include "game.h"
#include "gfx.h"
#include "plat.h"
#include "res.h"

#ifndef HOST
#ifndef NP_PORT /* (another target: platform/port.mk names the app) */
const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "Isaac";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;
#endif
extern const uint8_t is_data[];

uint32_t perf_frames __attribute__((used));   /* read by tools/emu.py */
uint32_t perf_updates __attribute__((used));

int main(void) {
  uint16_t band[SCREEN_W * GFX_BAND];   /* (here: on the stack, out of the RAM apps get) */
  gfx_set_band_memory(band);
  plat_begin();
  is_bin = is_data;
  g_rng ^= plat_millis() * 2654435761u;
  game_init();
  uint32_t next = plat_millis() * 3;   /* in thirds of a millisecond: 100 a frame, the game's 30 a second */
  while (g_running) {
    /* the game's frames: drawn when caught up (at most 2 updates for a drawing) */
    int n = 0;
    uint32_t now = plat_millis() * 3;
    while ((int32_t)(now - next) >= 0 && n < 2 && g_running) {
      game_frame(plat_keys());
      perf_updates++;
      next += 100;
      n++;
    }
    if (!g_running) break;
    if ((int32_t)(now - next) > 600) next = now;   /* far behind: no rushing */
    if (n) {
      game_draw();
      perf_frames++;
    } else
      plat_sleep(1);
  }
  return plat_end();
}
#endif
