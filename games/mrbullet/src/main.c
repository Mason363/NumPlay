/* Mr Bullet for the NumWorks calculator.
 *
 * Inspired by Mr Bullet - Spy Puzzles by Lion Studios, not affiliated with them. Made by Mason Chen as part of
 * NumPlay; the physics is Box2D by Erin Catto. */
#include "data.h"
#include "draw.h"
#include "plat.h"
#include "save.h"
#include "ui.h"
#include "world.h"

#ifndef HOST
/* (read in tools/emu.py: frames drawn, the slowest drawing, frames slower than 33 ms) */
uint32_t perf_frames __attribute__((used));
uint32_t perf_max __attribute__((used));
uint32_t perf_slow __attribute__((used));
uint32_t perf_step_max __attribute__((used));

void np_push(int x, int y, int w, int h, const color_t *px) { plat_push(x, y, w, h, px); }
extern char _heap_start[], _heap_end[];

#ifdef MEASURE
/* (a build for tools/emu.py: the levels of MEASURE_LIST played with shots at set angles, each step drawn; perf_max
   is then the slowest step with its drawing, perf_slow the steps over 33 ms) */
#include "game.h"
static const int16_t measure_list[] = {MEASURE_LIST};
static void scene(void *ctx) {
  (void)ctx;
  draw_world(true);
  draw_hud("Level", "Left Right", "turn");
}
int main(void) {
  uint64_t level[LEVEL_MAX_BYTES / 8 + 1];
  uint64_t strip[SCREEN_W * STRIP_H * 2 / 8];
  b2_heap_limit = (long)(_heap_end - _heap_start) - 32768;
  gfx_buf = (uint16_t *)strip;
  for (unsigned k = 0; k < sizeof measure_list / sizeof *measure_list; k++) {
    if (!game_load(measure_list[k] - 1, (uint8_t *)level)) continue;
    for (int s = 0; s < 480; s++) {
      if (s % 90 == 0) {
        g.aim = -0.2f - 0.25f * (float)(s / 90);
        g.ch[g.hero].left = 0;
        game_fire();
      }
      uint32_t t0 = plat_millis();
      game_step();
      gfx_render(scene, 0, 0, SCREEN_H);
      uint32_t dt = plat_millis() - t0;
      if (dt > perf_max) perf_max = dt;
      if (dt > 33) perf_slow++;
      perf_frames++;
    }
    game_end();
  }
  for (;;) plat_sleep(1000);
}
#else
int main(void) {
  /* on main's stack: the level's bytes, the screen's strip and the save's scratch memory; the heap is Box2D's */
  uint64_t level[LEVEL_MAX_BYTES / 8 + 1];
  uint64_t strip[SCREEN_W * STRIP_H * 2 / 8];
  uint64_t scratch[SAVE_SCRATCH / 8];
  plat_begin();
  /* Box2D makes no new contact past the heap less 32 KB (for a step's islands and the broad phase growing) */
  b2_heap_limit = (long)(_heap_end - _heap_start) - 32768;
  ui_init((uint8_t *)level, (uint16_t *)strip, scratch);
  for (;;) {
    uint32_t now = plat_millis();
    if (!ui_frame(plat_keys(), now)) break;
    if (ui_dirty()) {
      uint32_t t0 = plat_millis();
      ui_draw();
      uint32_t dt = plat_millis() - t0;
      if (dt > perf_max && perf_frames > 30) perf_max = dt;
      if (t0 - now + dt > 33) perf_slow++;
      if (t0 - now + dt > perf_step_max && perf_frames > 30) perf_step_max = t0 - now + dt;
      perf_frames++;
    } else {
      plat_sleep(5);
    }
  }
  return plat_end();
}
#endif
#endif
