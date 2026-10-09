/* Champion Island for the NumWorks calculator.
 *
 * Inspired by the Doodle Champion Island Games (Google, 2021), not
 * affiliated with Google or STUDIO4°C. Made by Mason Chen as part of NumPlay. */
#include "ci.h"
#include "ent.h"
#include "spr.h"
#include "gfx.h"

#ifndef HOST
const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "Champion Island";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;
#endif

void plat_begin(void);
int plat_end(void);

void ci_init(void) {
  node_reset();
  ent_reset();
  mem_layout(0, 0, 0, NULL);
  store_load();
  dialog_init();
  menus_init();
  hud_init();
  /* the doodle opens on its intro film the first time, then on the island */
#ifdef START_SCENE
#ifdef START_TUTORIAL_DONE
  store_set_bool("TUTORIAL_DONE", true);   /* recordings of the island after the tutorial */
  store_set_bool("intro_VIDEO_SEEN", true);
#endif
#ifdef START_ENDING
  store_set_bool("outro_VIDEO_SEEN", true);   /* the island after the ending */
#endif
  game_go(START_SCENE);   /* test builds: make EXTRA='-DSTART_SCENE=\"pingpong:hard\"' */
#else
  game_go(store_bool("intro_VIDEO_SEEN", false) ? "overworld" : "video:intro:skippable");
#endif
}

#ifndef HOST
uint32_t perf_frames __attribute__((used));   /* read by tools/emu.py */
uint32_t perf_ms __attribute__((used));
uint32_t perf_max __attribute__((used));      /* the slowest frame's work, ms (after the first second) */
uint32_t perf_slow __attribute__((used));     /* frames (after the first second) over 33 ms of work */

int main(void) {
  /* the display tree and the sprite decoder's ring, here on the stack: some calculator software gives apps less RAM */
  Node nodes_mem[NODE_MAX];
  uint8_t ring_mem[LZMA_DICT] __attribute__((aligned(4)));
  node_set_memory(nodes_mem);
  spr_set_ring(ring_mem);
  plat_begin();
#ifdef BENCH_STREAMS
  /* decode speed of every big image: perf_frames = raw KB, perf_ms = time */
  {
    extern uint32_t perf_ms;
    uint32_t t0 = plat_millis(), kb = 0;
    const uint8_t *t = ci_data + HDR(H_STREAMS);
    for (uint32_t i = 0; i < HDR(H_NSTREAMS); i++, t += 14) {
      Sprite s;
      sprite_info(rd16(t), &s);
      if (s.sheet != SHEET_OVERWORLD) continue;
      z_open(rd32(t + 2), rd32(t + 6), rd32(t + 10));
      z_get(NULL, rd32(t + 10));
      kb += rd32(t + 10) / 1024;
    }
    perf_frames = kb;
    perf_ms = plat_millis() - t0;
    for (;;) plat_sleep(1000);
  }
#endif
  ci_init();
  uint32_t next = plat_millis();
  bool slow = plat_slow();
  while (game_running) {
    uint32_t t0 = plat_millis();
    game_tick();
    /* (an N0110 or N0115, a frame behind: one more tick before drawing, so the game keeps its speed) */
    if (slow && game_running && (int32_t)(t0 - next) > 1000 / FPS) {
      game_tick();
      next += 1000 / FPS + (game.ticks % 3 == 0 ? 1 : 0);
    }
    if (!game_running) break;
    game_draw();
    perf_frames++;
    if (perf_frames > 30 && plat_millis() - t0 > perf_max) perf_max = plat_millis() - t0;
    if (perf_frames > 30 && plat_millis() - t0 > 33) perf_slow++;
    next += 1000 / FPS + (game.ticks % 3 == 0 ? 1 : 0);   /* 33.3 ms */
    uint32_t now = plat_millis();
    if ((int32_t)(next - now) > 0) plat_sleep(next - now);
    else if ((int32_t)(now - next) > 200) next = now;       /* far behind: do not rush */
  }
  store_save();
  return plat_end();
}
#endif
