/* Celeste for the TI-84 Plus CE.
 *
 * Inspired by Celeste, not affiliated with Maddy Makes Games.
 * Made by Mason Chen as part of NumPlay. */
#include "ce.h"

void game_init(void);
uint32_t g_perf_updates, g_perf_frames;   /* (read by the tests and the emulator) */

int main(void) {
  plat_init();
  if (!data_open()) {
    draw_clear_screen(0);
    draw_text("Celeste needs its appvars:", 20, 100, 255);
    draw_text("send CELG*.8xv and CEL*.8xv too.", 20, 112, 255);
    plat_frame();
    while (!(plat_keys() & (K_QUIT | K_BACK | K_OK))) plat_wait(300);
    plat_exit();
    return 0;
  }
  game_init();
  /* 60 updates a second (546.13 ticks of the 32768 Hz clock each), drawn when caught up, 3 at most a frame */
  unsigned last = plat_ticks();
  int acc = 0;
  while (g_running) {
    unsigned now = plat_ticks();
    acc += (int)((now - last) & 0xFFFF) * 60;
    last = now;
    if (acc > 32768 * 6) acc = 32768 * 6;   /* far behind: no rush */
    int n = 0;
    while (acc >= 32768 && n < 3 && g_running) {
      game_frame();
      g_perf_updates++;
      acc -= 32768;
      n++;
    }
    if (!g_running) break;
    if (n) {
      plat_begin_draw();
      game_draw();
      plat_frame();
      g_perf_frames++;
    } else
      plat_wait((unsigned)(32768 - acc) / 60);
  }
  plat_exit();
  return 0;
}
