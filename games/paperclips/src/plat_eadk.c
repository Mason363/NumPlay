/* The calculator: keys, time, the screen and files (EADK, and NumPlay's careful file helpers). */
#include <eadk.h>
#include "../../common/epsilon_app.h"
#include "../../common/epsilon_files.h"
#include "plat.h"

const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "Paperclips";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;

#define KEY(k) ((uint64_t)1 << (k))

uint32_t plat_keys(void) {
  uint64_t k = eadk_keyboard_scan();
  uint32_t r = 0;
  if (k & (KEY(eadk_key_ok) | KEY(eadk_key_exe) | KEY(eadk_key_five))) r |= PK_OK;
  if (k & (KEY(eadk_key_up) | KEY(eadk_key_eight))) r |= PK_UP;
  if (k & (KEY(eadk_key_down) | KEY(eadk_key_two))) r |= PK_DOWN;
  if (k & (KEY(eadk_key_left) | KEY(eadk_key_four))) r |= PK_LEFT;
  if (k & (KEY(eadk_key_right) | KEY(eadk_key_six))) r |= PK_RIGHT;
  if (k & (KEY(eadk_key_back) | KEY(eadk_key_backspace))) r |= PK_BACK;
  if (k & (KEY(eadk_key_home) | KEY(eadk_key_on_off))) r |= PK_HOME;
  return r;
}
uint32_t plat_millis(void) { return (uint32_t)eadk_timing_millis(); }
void plat_sleep(uint32_t ms) { eadk_timing_msleep(ms); }
void plat_push(int x, int y, int w, int h, const uint16_t *px) {
  eadk_display_push_rect((eadk_rect_t){(uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h}, (const eadk_color_t *)px);
}
bool plat_save(const char *name, const void *data, uint32_t len) { return ef_write(name, data, len); }
const uint8_t *plat_load(const char *name, uint32_t *len) { return ef_read(name, len); }
uint32_t plat_random(void) { return eadk_random(); }
void plat_begin(void) { np_app_begin(); }
int plat_end(void) { return np_app_end(); }
