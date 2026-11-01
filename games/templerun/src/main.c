#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif
/* Temple Run for the NumWorks calculator.
 *
 * Inspired by Temple Run by Imangi Studios (its rules and numbers), not affiliated with them.
 * Made by Mason Chen as part of NumPlay. */
#include "app.h"
#include "game.h"
#include "plat.h"
#include "r3d.h"

#ifndef HOST
#ifdef TR_BENCH
#include "../tests/bot.h"
#endif
const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "Temple Run";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;

uint32_t perf_frames __attribute__((used));   /* read by tools/emu.py */
uint32_t perf_max __attribute__((used));
uint32_t perf_slow __attribute__((used));
uint32_t perf_draw __attribute__((used));   /* (milliseconds spent drawing) */
uint32_t host_god __attribute__((used));    /* (set by tools/bench.py: nothing hurts the runner) */

int main(void) {
  uint16_t band[SCREEN_W * BAND_H];   /* (here: on the stack, out of the RAM apps get) */
  r_set_band_memory(band);
  /* the renderer's lists: the RAM after the game's own data */
  extern char _heap_start[], _heap_end[];
  uintptr_t heap = ((uintptr_t)_heap_start + 7) & ~(uintptr_t)7;
  if ((uintptr_t)_heap_end < heap + r_work_size()) return 1;
  r_init((void *)heap);
  plat_begin();
  app_init();
  uint32_t prev = 0;
  /* the game's 60 steps a second, drawn when caught up (at most 4 steps a drawing: slower models skip) */
  uint32_t next = plat_millis() * 3;   /* in thirds of a millisecond: 50 a step */
  while (app_running) {
    int n = 0;
    uint32_t now = plat_millis() * 3, keys = plat_keys();
#ifdef TR_BENCH
    /* (timing: two steps a drawing, the same frames whatever the speed) */
    while (n < 2 && app_running) {
      if (app_in_run()) keys = bot();
#else
    while ((int32_t)(now - next) >= 0 && n < 4 && app_running) {
#endif
      if (host_god) {
        if (pl.invis < 1) pl.invis = 1;
        pl.wings = 30;
      }
      app_step(keys, n ? 0 : keys & ~prev);
      next += 50;
      n++;
    }
    if (n) prev = keys;
    if (!app_running) break;
    if ((int32_t)(now - next) > 400) next = now;   /* far behind: do not rush */
    if (n) {
      uint32_t t0 = plat_millis();
      app_draw();
      uint32_t dt = plat_millis() - t0;
      perf_draw += dt;
      if (dt > perf_max) perf_max = dt;
      if (dt > 33) perf_slow++;
      perf_frames++;
    } else
      plat_sleep(1);
  }
  return plat_end();
}
#endif
