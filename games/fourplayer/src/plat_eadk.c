/* EADK: the NumWorks's display, keyboard, time and files, which every target of platform/ also gives. */
#include <eadk.h>
#include "../../common/epsilon_app.h"
#include "../../common/epsilon_files.h"
#include "plat.h"

#ifndef NP_PORT /* (another target: platform/port.mk names the app) */
const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "2 3 4 Players";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;
#endif

uint64_t plat_keys(void) { return (uint64_t)eadk_keyboard_scan(); }
uint32_t plat_millis(void) { return (uint32_t)eadk_timing_millis(); }
void plat_sleep(uint32_t ms) { eadk_timing_msleep(ms); }
void plat_vblank(void) { eadk_display_wait_for_vblank(); }
uint32_t plat_random(void) { return eadk_random(); }
void plat_push(int x, int y, int w, int h, const uint16_t *px) {
  eadk_display_push_rect((eadk_rect_t){(uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h}, px);
}
bool plat_save(const char *name, const void *data, uint32_t len) { return ef_write(name, data, len); }
const uint8_t *plat_load(const char *name, uint32_t *len) { return ef_read(name, len); }
void plat_begin(void) { np_app_begin(); }
int plat_end(void) { return np_app_end(); }
