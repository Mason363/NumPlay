#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")   /* not drawn every frame: smaller over faster */
#endif
/* Celeste for the NumWorks calculator.
 *
 * Inspired by Celeste, not affiliated with Maddy Makes Games.
 * Made by Mason Chen as part of NumPlay. */
#include "celeste.h"

#ifndef HOST
const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "Celeste";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;
extern const uint8_t cel_data[];

void plat_begin(void);
int plat_end(void);

uint32_t perf_frames __attribute__((used));   /* read by tools/emu.py */
uint32_t perf_updates __attribute__((used));

int main(void) {
  uint32_t strip[GFX_STRIP_BYTES / 4], cmds[GFX_CMD_BYTES / 4];   /* (gfx.c: here, out of the RAM apps get) */
  gfx_set_memory(strip, cmds);
  plat_begin();
  cel_bin = cel_data;
  game_init();
  uint32_t next = plat_millis() * 3;   /* in thirds of a millisecond: 50 per update */
  while (g_running) {
    /* 60 updates a second; draw when caught up (at most 2 updates per frame) */
    int n = 0;
    uint32_t now = plat_millis() * 3;
    while ((int32_t)(now - next) >= 0 && n < 2 && g_running) {
      game_frame();
      perf_updates++;
      next += 50;
      n++;
    }
    if (!g_running) break;
    if ((int32_t)(now - next) > 300) next = now;   /* far behind: do not rush */
    if (n) {
      game_draw();
      perf_frames++;
    } else
      plat_sleep(1);
  }
  game_save();
  return plat_end();
}
#endif
