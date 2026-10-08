#include <eadk.h>
#include "np.h"

uint32_t np_keys(void) {
  eadk_keyboard_state_t k = eadk_keyboard_scan();
#define DOWN(key) eadk_keyboard_key_down(k, key)
  uint32_t out = 0;
  if (DOWN(eadk_key_left) || DOWN(eadk_key_four)) out |= K_LEFT;
  if (DOWN(eadk_key_right) || DOWN(eadk_key_six)) out |= K_RIGHT;
  if (DOWN(eadk_key_up) || DOWN(eadk_key_eight)) out |= K_UP;
  if (DOWN(eadk_key_down) || DOWN(eadk_key_two)) out |= K_DOWN;
  if (DOWN(eadk_key_ok) || DOWN(eadk_key_exe) || DOWN(eadk_key_five)) out |= K_OK;
  if (DOWN(eadk_key_back)) out |= K_BACK;
  if (DOWN(eadk_key_home) || DOWN(eadk_key_on_off)) out |= K_HOME;
  if (DOWN(eadk_key_backspace)) out |= K_BACKSPACE;
#undef DOWN
  return out;
}

uint32_t np_millis(void) { return (uint32_t)eadk_timing_millis(); }
void np_sleep(unsigned ms) { eadk_timing_msleep(ms); }
void np_vblank(void) { eadk_display_wait_for_vblank(); }

void np_push(int x, int y, int w, int h, const color_t *px) {
  eadk_display_push_rect((eadk_rect_t){(uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h}, px);
}

void np_fill_screen(int x, int y, int w, int h, color_t c) {
  eadk_display_push_rect_uniform((eadk_rect_t){(uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h}, c);
}

void np_wait_release(void) {
  while (eadk_keyboard_scan()) eadk_timing_msleep(10);
}

/* The arena is the last of NumPlay's RAM: after it, up to _heap_end, the RAM the calculator's
 * software gives apps goes on (more of it on some software than on other). */
uint32_t np_arena_room(void) {
#if PLATFORM_DEVICE && !NP_SIMULATOR
  extern char _heap_end[];
  uint32_t room = (uint32_t)(_heap_end - (char *)np_arena);
  return room > np_arena_size ? room : np_arena_size;
#else
  return np_arena_size;
#endif
}

static uint32_t alloc_pos;
void *np_alloc(uint32_t size) {
  size = (size + 7) & ~7u;
  if (alloc_pos + size > np_arena_room()) return 0;
  void *p = np_arena + alloc_pos;
  alloc_pos += size;
  return p;
}
void np_alloc_reset(void) { alloc_pos = 0; }
