/* Host stand-in for the calculator's EADK API, to build and test the game on a PC. */
#ifndef EADK_STUB_H
#define EADK_STUB_H
#include <stdbool.h>
#include <stdint.h>
typedef uint16_t eadk_color_t;
typedef struct { uint16_t x, y; } eadk_point_t;
typedef struct { uint16_t x, y, width, height; } eadk_rect_t;
enum { eadk_key_left = 0, eadk_key_up = 1, eadk_key_down = 2, eadk_key_right = 3, eadk_key_ok = 4, eadk_key_back = 5,
  eadk_key_home = 6, eadk_key_on_off = 8, eadk_key_shift = 12, eadk_key_alpha = 13, eadk_key_backspace = 17,
  eadk_key_four = 36, eadk_key_five = 37, eadk_key_six = 38, eadk_key_one = 42, eadk_key_two = 43, eadk_key_three = 44,
  eadk_key_zero = 48, eadk_key_exe = 52 };
uint64_t eadk_keyboard_scan(void);
uint64_t eadk_timing_millis(void);
void eadk_timing_msleep(uint32_t ms);
void eadk_display_wait_for_vblank(void);
void eadk_display_push_rect(eadk_rect_t r, const eadk_color_t *px);
void eadk_display_push_rect_uniform(eadk_rect_t r, eadk_color_t c);
void eadk_display_draw_string(const char *s, eadk_point_t p, bool large, eadk_color_t fg, eadk_color_t bg);
uint32_t eadk_random(void);
#endif
