/* NumPlay on the TI-84 Plus CE, TI-84 Plus CE Python and TI-83 Premium CE (CE C toolchain): the 320 x 240 screen
 * is the LCD's memory in the software's own 16-bit mode, the keys come from the keypad (platform/ti84ce/README.md),
 * the files are one appvar per game, archived when the game ends.
 *
 * Build defines: NP_APP_ID (the game's id). The appvar is "NP" and the id's first 6 letters. */
#include <eadk.h>
#include <fileioc.h>
#include <keypadc.h>
#include <stdlib.h>
#include <string.h>
#include <sys/lcd.h>
#include <sys/rtc.h>
#include <sys/timers.h>
#include <ti/screen.h>
#include <time.h>
#include "np_port.h"

#ifndef NP_APP_ID
#define NP_APP_ID "numplay"
#endif

static uint16_t *const lcd = (uint16_t *)lcd_Ram;

void eadk_display_push_rect(eadk_rect_t r, const eadk_color_t *px) {
  if (r.x >= LCD_WIDTH || r.y >= LCD_HEIGHT) return;
  unsigned w = r.x + r.width > LCD_WIDTH ? LCD_WIDTH - r.x : r.width;
  for (unsigned y = 0; y < r.height && r.y + y < LCD_HEIGHT; y++)
    memcpy(&lcd[(r.y + y) * LCD_WIDTH + r.x], px + y * r.width, w * 2);
}
void eadk_display_push_rect_uniform(eadk_rect_t r, eadk_color_t c) {
  for (unsigned y = r.y; y < (unsigned)r.y + r.height && y < LCD_HEIGHT; y++)
    for (unsigned x = r.x; x < (unsigned)r.x + r.width && x < LCD_WIDTH; x++) lcd[y * LCD_WIDTH + x] = c;
}
void eadk_display_pull_rect(eadk_rect_t r, eadk_color_t *px) {
  for (unsigned y = 0; y < r.height; y++)
    for (unsigned x = 0; x < r.width; x++)
      px[y * r.width + x] = r.x + x < LCD_WIDTH && r.y + y < LCD_HEIGHT ? lcd[(r.y + y) * LCD_WIDTH + r.x + x] : 0;
}
bool eadk_display_wait_for_vblank(void) { return true; }

/* the NumWorks keys on the CE keypad: (group, bit) for each */
#define K(g, b, k) {g, b, k}
static const struct {
  uint8_t group, bit;
  eadk_key_t key;
} KEYS[] = {
  K(7, kb_Left, eadk_key_left), K(7, kb_Right, eadk_key_right), K(7, kb_Up, eadk_key_up), K(7, kb_Down, eadk_key_down),
  K(6, kb_Enter, eadk_key_ok), K(1, kb_2nd, eadk_key_exe), K(6, kb_Clear, eadk_key_back), K(1, kb_Mode, eadk_key_home),
  K(1, kb_Del, eadk_key_backspace), K(2, kb_Alpha, eadk_key_alpha), K(1, kb_Yequ, eadk_key_shift),
  K(2, kb_Math, eadk_key_toolbox), K(5, kb_Vars, eadk_key_var), K(3, kb_GraphVar, eadk_key_xnt),
  K(3, kb_0, eadk_key_zero), K(3, kb_1, eadk_key_one), K(4, kb_2, eadk_key_two), K(5, kb_3, eadk_key_three),
  K(3, kb_4, eadk_key_four), K(4, kb_5, eadk_key_five), K(5, kb_6, eadk_key_six), K(3, kb_7, eadk_key_seven),
  K(4, kb_8, eadk_key_eight), K(5, kb_9, eadk_key_nine), K(6, kb_Add, eadk_key_plus), K(6, kb_Sub, eadk_key_minus),
  K(6, kb_Mul, eadk_key_multiplication), K(6, kb_Div, eadk_key_division), K(4, kb_DecPnt, eadk_key_dot),
  K(3, kb_Comma, eadk_key_comma), K(4, kb_LParen, eadk_key_left_parenthesis),
  K(5, kb_RParen, eadk_key_right_parenthesis), K(6, kb_Power, eadk_key_power), K(2, kb_Square, eadk_key_square),
  K(2, kb_Ln, eadk_key_ln), K(2, kb_Log, eadk_key_log), K(3, kb_Sin, eadk_key_sine), K(4, kb_Cos, eadk_key_cosine),
  K(5, kb_Tan, eadk_key_tangent),
};

eadk_keyboard_state_t eadk_keyboard_scan(void) {
  kb_Scan();
  eadk_keyboard_state_t s = 0;
  for (unsigned i = 0; i < sizeof KEYS / sizeof KEYS[0]; i++)
    if (kb_Data[KEYS[i].group] & KEYS[i].bit) s |= (eadk_keyboard_state_t)1 << KEYS[i].key;
  if (kb_On) s |= (eadk_keyboard_state_t)1 << eadk_key_home, kb_ClearOnLatch();
  return s;
}

eadk_event_t eadk_event_get(int32_t *timeout) {
  static eadk_keyboard_state_t before = ~(eadk_keyboard_state_t)0;
  uint64_t until = eadk_timing_millis() + (uint64_t)(timeout && *timeout > 0 ? *timeout : 0);
  do {
    eadk_keyboard_state_t now = eadk_keyboard_scan(), hit = now & ~before;
    before = now;
    for (unsigned k = 0; k < 64; k++)
      if (hit >> k & 1) return (eadk_event_t)k;
  } while (eadk_timing_millis() < until);
  if (timeout) *timeout = 0;
  return eadk_event_none;
}

/* time: the 32768 Hz clock */
static clock_t t0;
uint64_t eadk_timing_millis(void) { return (uint64_t)(clock() - t0) * 1000 / CLOCKS_PER_SEC; }
void eadk_timing_msleep(uint32_t ms) {
  while (ms > 60000) msleep(60000), ms -= 60000;
  msleep((uint16_t)ms);
}
void eadk_timing_usleep(uint32_t us) { eadk_timing_msleep((us + 999) / 1000); }
uint32_t eadk_random(void) { return (uint32_t)random() << 16 ^ (uint32_t)random(); }
void eadk_backlight_set_brightness(uint8_t b) { lcd_BacklightLevel = (uint8_t)(255 - b); }
uint8_t eadk_backlight_brightness(void) { return (uint8_t)(255 - lcd_BacklightLevel); }
bool eadk_battery_is_charging(void) { return false; }
uint8_t eadk_battery_level(void) { return 3; }
float eadk_battery_voltage(void) { return 4.0f; }
bool eadk_usb_is_plugged(void) { return false; }
const char *eadk_external_data = 0;
size_t eadk_external_data_size = 0;

/* the files: a 4 KB buffer, the appvar "NP" + the id's first 6 letters */
#define STORAGE_SIZE 4096
static uint8_t storage[STORAGE_SIZE + 16];
static bool loaded;
static char var[9];

static const char *var_name(void) {
  if (!var[0]) {
    var[0] = 'N', var[1] = 'P';
    for (int i = 0; i < 6 && NP_APP_ID[i]; i++) var[2 + i] = NP_APP_ID[i];
  }
  return var;
}
static uint32_t storage_end(void) {
  uint32_t at = 0;
  while (at + 2 <= STORAGE_SIZE && (storage[at] | storage[at + 1] << 8)) at += storage[at] | storage[at + 1] << 8;
  return at;
}
uint8_t *np_storage(uint32_t *size) {
  if (!loaded) {
    loaded = true;
    uint8_t h = ti_Open(var_name(), "r");
    if (h) {
      size_t n = ti_Read(storage, 1, STORAGE_SIZE, h);
      ti_Close(h);
      if (storage_end() + 2 > n) memset(storage, 0, sizeof storage);
    }
  }
  *size = STORAGE_SIZE;
  return storage;
}
static void keep(bool archive) {
  uint8_t h = ti_Open(var_name(), "w");
  if (!h) return;
  ti_Write(storage, 1, storage_end() + 2, h);
  if (archive) ti_SetArchiveStatus(true, h);
  ti_Close(h);
}
void np_storage_changed(void) { keep(false); }

bool np_slow(void) { return true; }
const char *np_target(void) { return "ti84ce"; }
void np_begin(void) {
  t0 = clock();
  srandom(rtc_Time());
  kb_DisableOnLatch();
}
int np_end(void) {
  if (loaded) keep(true);
  os_ClrHomeFull();
  return 0;
}
