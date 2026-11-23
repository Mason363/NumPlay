#include "platform.h"
#include <eadk.h>
#include <string.h>
#include "../../common/epsilon_app.h"

bool platform_begin(void) {
  np_app_begin();
  return true;
}
int platform_end(void) { return np_app_end(); }

bool platform_init(void) { return true; }
void platform_close(void) {}
uint32_t platform_millis(void) { return (uint32_t)eadk_timing_millis(); }
void platform_sleep(unsigned ms) { eadk_timing_msleep(ms); }
uint32_t platform_random(void) { return eadk_random(); }

static uint32_t scan_keys(void) {
  eadk_keyboard_state_t k = eadk_keyboard_scan();
  uint32_t out = 0;
  static const struct { uint8_t key; uint32_t bit; } map[] = {
    {eadk_key_left, K_LEFT}, {eadk_key_right, K_RIGHT}, {eadk_key_up, K_UP}, {eadk_key_down, K_DOWN},
    {eadk_key_ok, K_OK}, {eadk_key_exe, K_EXE}, {eadk_key_back, K_BACK}, {eadk_key_home, K_HOME},
    {eadk_key_on_off, K_HOME}, {eadk_key_toolbox, K_TOOL}, {eadk_key_shift, K_SHIFT},
    {eadk_key_backspace, K_ERASE}, {eadk_key_var, K_SAVE}, {eadk_key_alpha, K_UNDO},
    {eadk_key_zero, K_CHECK}, {eadk_key_plus, K_PLUS}, {eadk_key_minus, K_MINUS},
    {eadk_key_xnt, K_COPY}, {eadk_key_ln, K_PROPS}};
  for (unsigned i = 0; i < sizeof(map) / sizeof(map[0]); i++)
    if (eadk_keyboard_key_down(k, (eadk_key_t)map[i].key)) out |= map[i].bit;
  return out;
}

/* Keys are also sampled between strip transfers so that a quick tap made
 * while a frame is being sent is not lost. */
static uint32_t last_scan, captured;
static void capture_keys(void) {
  uint32_t now = scan_keys();
  captured |= now & ~last_scan;
  last_scan = now;
}
uint32_t platform_keys(void) {
  capture_keys();
  uint32_t out = last_scan | captured;
  captured = 0;
  return out;
}

/* Strips whose pixels did not change since the last frame are skipped. */
static uint32_t strip_hash[32];
void platform_frame_begin(bool vsync) {
  capture_keys();
  if (vsync) eadk_display_wait_for_vblank();
}
void platform_strip(int y, int h, const uint16_t *pixels) {
  const uint32_t *w = (const uint32_t *)pixels;
  uint32_t hash = 2166136261u;
  for (int i = 0, n = 320 * h / 2; i < n; i++) hash = (hash ^ w[i]) * 16777619u;
  int slot = (y / 8) & 31;
  if (strip_hash[slot] != hash) {
    strip_hash[slot] = hash;
    eadk_display_push_rect((eadk_rect_t){0, (uint16_t)y, 320, (uint16_t)h}, pixels);
  }
  capture_keys();
}
void platform_frame_end(void) {
  capture_keys();
#ifdef NP_PORT
  eadk_timing_msleep(0); /* the frame is whole: shown now (the TI-Nspire shows a frame where the game waits, not where
                            it reads the keys between strips) */
#endif
}

/* Epsilon's record buffer, found through the userland header of the
 * software running this app (see games/common/epsilon_app.h). */
uint8_t *platform_storage(size_t *size) {
  uint32_t n = 0;
  uint8_t *a = epsilon_storage(&n);
  if (a) *size = n;
  return a;
}
void platform_storage_commit(void) {}
