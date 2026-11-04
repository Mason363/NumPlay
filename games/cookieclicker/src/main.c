/* Cookie Clicker for the NumWorks calculator.
 *
 * Cookie Clicker is by Orteil and Opti (DashNet): its pictures, texts, numbers and rules come from
 * the game itself (version 2.058, from its public mirror; see tools/fetch.py). Not affiliated with
 * DashNet. Made by Mason Chen as part of NumPlay. */
#include "game.h"
#include "gfx.h"
#include "plat.h"
#include "ui.h"

bool g_quit;
extern bool game_load(void);
extern bool game_save(void);

/* the game: loaded or new, its frames, its saves every minute */
void cc_begin(void) {
  G.seed = plat_random() | 1;
  game_new();
  if (!game_load()) {
    S.start_date = S.full_date = S.play_time = 0;
    calculate_gains();
  }
  game_rebuild_upgrades();
  ui_init();
}

void cc_frame(uint32_t pressed, uint32_t held) {
  ui_keys(pressed, held);
  game_logic();
  ui_tick();
  if (G.T % (FPS * 60) == 0 && G.T > FPS * 10 && !G.on_ascend && G.ascend_timer == 0) game_save();
}

#ifndef HOST
#ifndef NP_PORT /* (another target: platform/port.mk names the app) */
const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "Cookie Clicker";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;
#endif

uint32_t perf_frames __attribute__((used));   /* read by tools/emu.py */
uint32_t perf_updates __attribute__((used));

int main(void) {
  uint16_t band[SCREEN_W * GFX_BAND];   /* (here: on the stack, out of the RAM apps get) */
  gfx_set_band_memory(band);
  plat_begin();
  cc_begin();
  uint32_t next = plat_millis(), prev = 0;
  int sub = 0;
  /* the game's frames are thirtieths of a second; drawn when caught up, at most 4 frames a drawing */
  while (!g_quit) {
    int n = 0;
    uint32_t now = plat_millis();
    while ((int32_t)(now - next) >= 0 && n < 4 && !g_quit) {
      uint32_t held = plat_keys();
      if (held & K_HOME) g_quit = true;
      cc_frame(held & ~prev, held);
      prev = held;
      perf_updates++;
      next += 33 + (++sub % 3 == 0);   /* (33, 33, 34: a thirtieth of a second) */
      n++;
    }
    if (g_quit) break;
    if ((int32_t)(now - next) > 300) next = now;   /* far behind: slower, rather than rushing */
    if (n) {
      ui_frame();
      perf_frames++;
    } else
      plat_sleep(1);
  }
  game_save();
  return plat_end();
}
#endif
