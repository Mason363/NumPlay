#ifndef TURBO_HOST_EADK
#define TURBO_HOST_EADK
#include <stdint.h>
enum { eadk_key_left=0,eadk_key_up=1,eadk_key_down=2,eadk_key_right=3,eadk_key_ok=4,
  eadk_key_back=5,eadk_key_home=6,eadk_key_on_off=8,eadk_key_one=42,eadk_key_two=43,
  eadk_key_seven=30,eadk_key_four=36,eadk_key_zero=48,eadk_key_exe=52 };
uint64_t eadk_timing_millis(void);
uint32_t eadk_random(void);
uint64_t eadk_keyboard_scan(void);
void eadk_display_wait_for_vblank(void);
void eadk_timing_msleep(uint32_t);
#endif
