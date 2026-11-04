/* Plants vs. Zombies for the NumWorks calculator.
 *
 * Inspired by Plants vs. Zombies by PopCap Games, not affiliated with PopCap Games or Electronic Arts.
 * Made by Mason Chen as part of NumPlay. */
#include "gfx.h"
#include "plat.h"
#include "res.h"
#include "screens.h"

const uint8_t *pvz_bin;

#ifndef HOST
#ifndef NP_PORT /* (another target: platform/port.mk names the app) */
const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "Plants vs. Zombies";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;
#endif
extern const uint8_t pvz_data[];

uint32_t perf_frames __attribute__((used));   /* read by tools/emu.py */
uint32_t perf_updates __attribute__((used));

int main(void) {
  uint16_t band[SCREEN_W * GFX_BAND];   /* (here: on the stack, out of the RAM apps get) */
  gfx_set_band_memory(band);
  plat_begin();
  pvz_bin = pvz_data;
  game_init();
  uint32_t next = plat_millis();
  /* the game's ticks are hundredths of a second; drawn when caught up, at most 8 ticks a drawing */
  while (g_running) {
    int n = 0;
    uint32_t now = plat_millis();
    while ((int32_t)(now - next) >= 0 && n < 8 && g_running) {
      game_frame(plat_keys());
      perf_updates++;
      next += 10;
      n++;
    }
    if (!g_running) break;
    if ((int32_t)(now - next) > 200) next = now;   /* far behind: slower, rather than rushing */
    if (n) {
      game_draw();
      perf_frames++;
    } else
      plat_sleep(1);
  }
  return plat_end();
}
#endif
