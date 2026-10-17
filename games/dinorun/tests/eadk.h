#ifndef DINO_FAKE_EADK_H
#define DINO_FAKE_EADK_H
#include <stdint.h>
typedef struct {uint16_t x,y,width,height;} eadk_rect_t;
/* Test-only names; the real NumWorks SDK supplies these. */
enum {eadk_key_left=0,eadk_key_up=1,eadk_key_down=2,eadk_key_right=3,
      eadk_key_ok=4,eadk_key_back=5,eadk_key_home=6,eadk_key_on_off=8,
      eadk_key_shift=12,eadk_key_backspace=17,eadk_key_eight=31,
      eadk_key_two=43,eadk_key_exe=52};
uint64_t eadk_keyboard_scan(void);
uint32_t eadk_timing_millis(void);
void eadk_timing_msleep(uint32_t ms);
void eadk_display_push_rect(eadk_rect_t rect,const uint16_t *px);
#endif
