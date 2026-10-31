/* Snow Rider 3D for the NumWorks calculator.
 *
 * A port of Snow Rider 3D, its world read from the game's own Unity project (tools/gen.py) and its scripts
 * rewritten in C. Not affiliated with the makers of Snow Rider 3D. Made as part of NumPlay. */
#include "sr.h"
#include "r3d.h"
#include "gfx.h"

/* (gfx.c's own strips and arena are not used: it draws into the renderer's bands) */
void np_push(int x, int y, int w, int h, const color_t *px) { plat_push(x, y, w, h, px); }
void *np_alloc(uint32_t size) { (void)size; return 0; }

#ifndef HOST
const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "Snow Rider 3D";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;

uint32_t perf_frames __attribute__((used));   /* read by tools/emu.py */
uint32_t perf_max __attribute__((used));
uint32_t perf_slow __attribute__((used));

int main(void) {
  uint16_t band[SCREEN_W * BAND_H];   /* (on the stack, out of the RAM apps get) */
  r_set_band_memory(band);
  /* the renderer's lists: the RAM after the game's own data */
  extern char _heap_start[], _heap_end[];
  uintptr_t heap = ((uintptr_t)_heap_start + 7) & ~(uintptr_t)7;
  if ((uintptr_t)_heap_end < heap + r_work_size()) return 1;
  r_init((void *)heap);
  plat_begin();
  draw_init();
  game_init();
  uint32_t prev = plat_keys();   /* (keys still down from the launcher wait to be let go) */
  /* the game's 60 steps a second, drawn when caught up (at most 4 steps a drawing: slower models skip) */
  uint32_t next = plat_millis() * 3;   /* in thirds of a millisecond: 50 a step */
  while (game_running) {
    int n = 0;
    uint32_t now = plat_millis() * 3, keys = plat_keys();
    while ((int32_t)(now - next) >= 0 && n < 4 && game_running) {
      game_step(keys, n ? 0 : keys & ~prev);
      next += 50;
      n++;
    }
    if (n) prev = keys;
    if (!game_running) break;
    if ((int32_t)(now - next) > 400) next = now;   /* far behind: do not rush */
    if (n) {
      uint32_t t0 = plat_millis();
      draw_frame();
      uint32_t dt = plat_millis() - t0;
      if (dt > perf_max) perf_max = dt;
      if (dt > 33) perf_slow++;
      perf_frames++;
    } else
      plat_sleep(1);
  }
  return plat_end();
}
#endif
