/* The keys pressed while a frame is drawn, for the targets whose frames can take a tenth of a second or more (the
 * TI-Nspire, the TI-84 Plus CE): a game reads the keyboard once a frame, and a tap that came and went between two
 * reads was lost. The port reads the keys now and then while drawing (np_keys_drawing) and its eadk_keyboard_scan
 * gives what was seen (np_keys_scan):
 * - a key that was up at the game's last scan and went down meanwhile is down, even if it's up again by now;
 * - a key that was down then, went up and down again (the same key tapped again: the cursor moved twice) is up now
 *   and down at the next scan, so the game sees it let go and pressed again.
 * Included once, by the port's own file. */
#ifndef NP_KEYS_DRAWN_H
#define NP_KEYS_DRAWN_H
#include <eadk.h>

static eadk_keyboard_state_t np_kd_shown;   /* what the game's last scan gave */
static eadk_keyboard_state_t np_kd_down;    /* went down since, from up */
static eadk_keyboard_state_t np_kd_up;      /* went up since, from down */
static eadk_keyboard_state_t np_kd_again;   /* went up, then down again */
static eadk_keyboard_state_t np_kd_next;    /* down at the next scan (pressed again) */

/* the keys as they are now, read while drawing */
static void np_keys_drawing(eadk_keyboard_state_t now) {
  np_kd_down |= now & ~np_kd_shown;
  np_kd_again |= np_kd_up & now;
  np_kd_up |= np_kd_shown & ~now;
}
/* the game's scan: the keys as they are now, and what was seen while drawing */
static eadk_keyboard_state_t np_keys_scan(eadk_keyboard_state_t now) {
  np_keys_drawing(now);
  eadk_keyboard_state_t s = ((now | np_kd_down) & ~np_kd_again) | np_kd_next;
  np_kd_next = np_kd_again & ~np_kd_next;
  np_kd_down = np_kd_up = np_kd_again = 0;
  return np_kd_shown = s;
}
#endif
