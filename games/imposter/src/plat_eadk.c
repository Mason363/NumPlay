/* EADK: the NumWorks's display, keyboard, time and files, which every target of platform/ also gives. */
#include <eadk.h>
#include "../../common/epsilon_app.h"
#include "../../common/epsilon_files.h"
#include "plat.h"

#ifndef NP_PORT /* (another target: platform/port.mk names the app) */
const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "Imposter";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;
#endif

#define KEY(k) ((uint64_t)1 << (k))

/* the letters printed over the NumWorks's keys, A on exp to Z on +, a space on - */
static const uint8_t letter_keys[27] = {
  eadk_key_exp, eadk_key_ln, eadk_key_log, eadk_key_imaginary, eadk_key_comma, eadk_key_power,
  eadk_key_sine, eadk_key_cosine, eadk_key_tangent, eadk_key_pi, eadk_key_sqrt, eadk_key_square,
  eadk_key_seven, eadk_key_eight, eadk_key_nine, eadk_key_left_parenthesis, eadk_key_right_parenthesis,
  eadk_key_four, eadk_key_five, eadk_key_six, eadk_key_multiplication, eadk_key_division,
  eadk_key_one, eadk_key_two, eadk_key_three, eadk_key_plus, eadk_key_minus};

/* (another target's keys carry other letters, or none: names are typed on the screen's letters there) */
bool plat_typing(void) {
#ifdef NP_PORT
  return false;
#else
  return true;
#endif
}

uint32_t plat_keys(uint32_t *letters) {
  uint64_t k = eadk_keyboard_scan();
  uint32_t r = 0, l = 0;
  if (k & KEY(eadk_key_left)) r |= KB_LEFT;
  if (k & KEY(eadk_key_right)) r |= KB_RIGHT;
  if (k & KEY(eadk_key_up)) r |= KB_UP;
  if (k & KEY(eadk_key_down)) r |= KB_DOWN;
  if (k & KEY(eadk_key_ok)) r |= KB_OK;
  if (k & KEY(eadk_key_exe)) r |= KB_EXE;
  if (k & KEY(eadk_key_back)) r |= KB_BACK;
  if (k & (KEY(eadk_key_home) | KEY(eadk_key_on_off))) r |= KB_HOME;
  if (k & KEY(eadk_key_backspace)) r |= KB_ERASE;
  if (k) r |= KB_ANY;
  if (plat_typing())
    for (int i = 0; i < 27; i++)
      if (k & KEY(letter_keys[i])) l |= (uint32_t)1 << i;
  *letters = l;
  return r;
}

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
