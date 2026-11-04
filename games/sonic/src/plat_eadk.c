/* The calculator: EADK display, keyboard, time and files.
 *
 * The keys as a Mega Drive pad: the arrows (or 8 4 6 2) are the pad, OK, EXE and 5 are A, B and C
 * (all three jump), Back is Start (pause). */
#include <eadk.h>
#include "../../common/epsilon_app.h"
#include "../../common/epsilon_files.h"
#include "plat.h"

#ifndef NP_PORT /* (another target: platform/port.mk names the app) */
const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "Sonic";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;
#endif

#define KEY(k) ((uint64_t)1 << (k))

uint32_t plat_keys(void) {
  uint64_t k = eadk_keyboard_scan();
  uint32_t r = 0;
  if (k & (KEY(eadk_key_left) | KEY(eadk_key_four))) r |= K_LEFT;
  if (k & (KEY(eadk_key_right) | KEY(eadk_key_six))) r |= K_RIGHT;
  if (k & (KEY(eadk_key_up) | KEY(eadk_key_eight))) r |= K_UP;
  if (k & (KEY(eadk_key_down) | KEY(eadk_key_two))) r |= K_DOWN;
  if (k & KEY(eadk_key_ok)) r |= K_A;
  if (k & KEY(eadk_key_exe)) r |= K_B;
  if (k & (KEY(eadk_key_five) | KEY(eadk_key_zero) | KEY(eadk_key_shift))) r |= K_C;
  if (k & (KEY(eadk_key_back) | KEY(eadk_key_backspace))) r |= K_START;
  if (k & (KEY(eadk_key_home) | KEY(eadk_key_on_off))) r |= K_HOME;
  if (k) r |= K_ANY;
  return r;
}

uint32_t plat_millis(void) { return (uint32_t)eadk_timing_millis(); }
void plat_sleep(uint32_t ms) { eadk_timing_msleep(ms); }
void plat_push(int x, int y, int w, int h, const uint16_t *px) {
  eadk_display_push_rect((eadk_rect_t){(uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h}, px);
}
void plat_fill(int x, int y, int w, int h, uint16_t c) {
  if (w > 0 && h > 0) eadk_display_push_rect_uniform((eadk_rect_t){(uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h}, c);
}
bool plat_save(const char *name, const void *data, uint32_t len) { return ef_write(name, data, len); }
const uint8_t *plat_load(const char *name, uint32_t *len) { return ef_read(name, len); }
uint8_t *plat_reserve(const char *name, uint32_t len) {
  uint32_t n;
  return ef_write(name, NULL, len) ? (uint8_t *)(uintptr_t)ef_read(name, &n) : NULL;   /* (the storage is RAM) */
}
void plat_reserved(const char *name, uint32_t len) { (void)name, (void)len; }
bool plat_remove(const char *name) { return ef_remove(name); }
void plat_begin(void) { np_app_begin(); }
int plat_end(void) { return np_app_end(); }
bool plat_slow(void) { return epsilon_slow_model(); }
