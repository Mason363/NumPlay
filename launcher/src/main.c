/* NumPlay: one app for all the games. */
#include <eadk.h>
#include "np.h"
#include "sys.h"

#if PLATFORM_DEVICE && !NP_SIMULATOR
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;
/* Tells the games (games/common/epsilon_app.h) that the launcher takes care
 * of Home and of the RAM. */
const uint8_t numplay_launcher[1] = {1};

/* The games read the keyboard through these (the build renames their calls),
 * so NumPlay knows when a game ended because of Home or On/Off. */
static bool home_pressed;
eadk_keyboard_state_t np_keyboard_scan(void) {
  eadk_keyboard_state_t s = eadk_keyboard_scan();
  if (s & ((1ull << eadk_key_home) | (1ull << eadk_key_on_off))) home_pressed = true;
  return s;
}
eadk_event_t np_event_get(int32_t *timeout) {
  eadk_event_t e = eadk_event_get(timeout);
  if (e == 6 || e == 8) home_pressed = true; /* Home, On/Off */
  return e;
}
#else
static bool home_pressed;
#endif

int main(void) {
  np_session_begin();
  np_finish_pending_uninstalls();
  /* just updated? the saves come back from their copy, then the copy is refreshed */
  np_progress_restore();
  np_progress_backup();
  /* TwiliPlay always opens its normal launcher, even with older saved settings. */
  np_wait_release();
  int selected = np_game_count ? 0 : -1;
  bool returning = false;
  for (;;) {
    int r = np_home(&selected, returning);
    returning = false;
    if (r == -2) break;
    if (r == -1) {
      np_settings();
      np_progress_backup();
      continue;
    }
    home_pressed = false;
    np_game_run(r);
    np_progress_backup();
    /* Home in a game goes all the way home, like everywhere on the calculator */
    if (home_pressed) break;
    np_wait_release();
    returning = true;
  }
  return np_session_end();
}
