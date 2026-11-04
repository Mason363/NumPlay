/* NumPlay's games talk to the calculator through these functions: NumWorks's EADK (its apps' interface), the
 * same names and values. On a NumWorks the calculator's own eadk.h is used; every other target (platform/<target>/)
 * implements this one, so a game builds there unchanged.
 *
 * The screen is 320 x 240, RGB565. The keyboard is the NumWorks keypad: a target maps its own keys onto it (see
 * its README). Time is in milliseconds since the app started. */
#ifndef EADK_H
#define EADK_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint16_t eadk_color_t;
static const eadk_color_t eadk_color_black = 0x0000;
static const eadk_color_t eadk_color_white = 0xFFFF;
static const eadk_color_t eadk_color_red = 0xF800;
static const eadk_color_t eadk_color_green = 0x07E0;
static const eadk_color_t eadk_color_blue = 0x001F;

typedef struct {
  uint16_t x, y;
} eadk_point_t;
typedef struct {
  uint16_t x, y, width, height;
} eadk_rect_t;

#define EADK_SCREEN_WIDTH 320
#define EADK_SCREEN_HEIGHT 240
static const eadk_rect_t eadk_screen_rect = {0, 0, EADK_SCREEN_WIDTH, EADK_SCREEN_HEIGHT};

/* the keys, as bits of a keyboard state: bit n is key n */
typedef uint64_t eadk_keyboard_state_t;
typedef enum {
  eadk_key_left = 0, eadk_key_up = 1, eadk_key_down = 2, eadk_key_right = 3, eadk_key_ok = 4, eadk_key_back = 5,
  eadk_key_home = 6, eadk_key_on_off = 8, eadk_key_shift = 12, eadk_key_alpha = 13, eadk_key_xnt = 14,
  eadk_key_var = 15, eadk_key_toolbox = 16, eadk_key_backspace = 17, eadk_key_exp = 18, eadk_key_ln = 19,
  eadk_key_log = 20, eadk_key_imaginary = 21, eadk_key_comma = 22, eadk_key_power = 23, eadk_key_sine = 24,
  eadk_key_cosine = 25, eadk_key_tangent = 26, eadk_key_pi = 27, eadk_key_sqrt = 28, eadk_key_square = 29,
  eadk_key_seven = 30, eadk_key_eight = 31, eadk_key_nine = 32, eadk_key_left_parenthesis = 33,
  eadk_key_right_parenthesis = 34, eadk_key_four = 36, eadk_key_five = 37, eadk_key_six = 38,
  eadk_key_multiplication = 39, eadk_key_division = 40, eadk_key_one = 42, eadk_key_two = 43, eadk_key_three = 44,
  eadk_key_plus = 45, eadk_key_minus = 46, eadk_key_zero = 48, eadk_key_dot = 49, eadk_key_ee = 50,
  eadk_key_ans = 51, eadk_key_exe = 52
} eadk_key_t;

eadk_keyboard_state_t eadk_keyboard_scan(void);
static inline bool eadk_keyboard_key_down(eadk_keyboard_state_t state, eadk_key_t key) {
  return (state >> (uint8_t)key) & 1;
}

/* key presses as events (the few games that use them): a key's event is its number */
typedef uint16_t eadk_event_t;
enum {
  eadk_event_left = 0, eadk_event_up = 1, eadk_event_down = 2, eadk_event_right = 3, eadk_event_ok = 4,
  eadk_event_back = 5, eadk_event_backspace = 17, eadk_event_exe = 52, eadk_event_none = 0xFFFF
};
eadk_event_t eadk_event_get(int32_t *timeout);

void eadk_display_push_rect(eadk_rect_t rect, const eadk_color_t *pixels);
void eadk_display_push_rect_uniform(eadk_rect_t rect, eadk_color_t color);
void eadk_display_pull_rect(eadk_rect_t rect, eadk_color_t *pixels);
bool eadk_display_wait_for_vblank(void);
void eadk_display_draw_string(const char *text, eadk_point_t point, bool large_font, eadk_color_t text_color,
                              eadk_color_t background_color);

void eadk_timing_usleep(uint32_t us);
void eadk_timing_msleep(uint32_t ms);
uint64_t eadk_timing_millis(void);

void eadk_backlight_set_brightness(uint8_t brightness);
uint8_t eadk_backlight_brightness(void);
bool eadk_battery_is_charging(void);
uint8_t eadk_battery_level(void);
float eadk_battery_voltage(void);
bool eadk_usb_is_plugged(void);
uint32_t eadk_random(void);

extern const char *eadk_external_data;
extern size_t eadk_external_data_size;

#endif
