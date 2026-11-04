/* NumPlay on the TI-84 Plus CE, TI-84 Plus CE Python and TI-83 Premium CE (CE C toolchain): the 320 x 240 screen
 * is the LCD's memory in the software's own 16-bit mode, the keys come from the keypad (platform/ti84ce/README.md),
 * the files are one appvar per game, archived when the game ends.
 *
 * A game draws straight into the LCD's memory, a rectangle at a time, as on a NumWorks: nothing is copied but what
 * it sends. Time is the 32768 Hz clock (timer 1).
 *
 * Build defines: NP_APP_ID (the game's id). The appvar is "NP" and the id (6 letters at most). */
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
#ifndef NP_FILES_ID /* (its files' name: the game's own, or that of the game whose saves it shares) */
#define NP_FILES_ID NP_APP_ID
#endif

static uint16_t *const lcd = (uint16_t *)lcd_Ram;

/* the part of r on the screen: its width and height (0: none) */
static unsigned clip(eadk_rect_t r, unsigned *h) {
  if (r.x >= LCD_WIDTH || r.y >= LCD_HEIGHT) return *h = 0;
  *h = r.y + r.height > LCD_HEIGHT ? LCD_HEIGHT - r.y : r.height;
  return r.x + r.width > LCD_WIDTH ? LCD_WIDTH - r.x : r.width;
}

void eadk_display_push_rect(eadk_rect_t r, const eadk_color_t *px) {
  unsigned h, w = clip(r, &h);
  uint16_t *row = lcd + r.y * LCD_WIDTH + r.x;
  if (w == LCD_WIDTH && r.width == LCD_WIDTH) w *= h, h = 1; /* rows of the screen's width: one copy */
  for (; w && h; h--, row += LCD_WIDTH, px += r.width) memcpy(row, px, w * 2);
}
void eadk_display_push_rect_uniform(eadk_rect_t r, eadk_color_t c) {
  unsigned h, w = clip(r, &h);
  uint16_t *row = lcd + r.y * LCD_WIDTH + r.x;
  if (!w || !h) return;
  if ((c >> 8) == (c & 0xFF)) { /* black, white...: both bytes alike */
    for (; h; h--, row += LCD_WIDTH) memset(row, c & 0xFF, w * 2);
    return;
  }
  for (unsigned i = 0; i < w; i++) row[i] = c; /* the first row, then copies of it */
  for (uint16_t *p = row + LCD_WIDTH; --h; p += LCD_WIDTH) memcpy(p, row, w * 2);
}
void eadk_display_pull_rect(eadk_rect_t r, eadk_color_t *px) {
  unsigned h, w = clip(r, &h);
  const uint16_t *row = lcd + r.y * LCD_WIDTH + r.x;
  for (unsigned y = 0; y < r.height; y++, px += r.width)
    if (y < h) {
      memcpy(px, row + y * LCD_WIDTH, w * 2);
      memset(px + w, 0, (r.width - w) * 2);
    } else {
      memset(px, 0, r.width * 2);
    }
}
/* the LCD shows its memory as it is drawn (as the NumWorks's does): no wait, every frame counts on the CE */
__attribute__((noinline)) bool eadk_display_wait_for_vblank(void) { return true; } /* (kept: tools/ce_emu counts frames by it) */

/* the NumWorks keys on the CE keypad: (group, bit) for each. 2nd is OK, next to the arrows like the NumWorks's,
 * and enter is EXE, at the bottom right like it */
#define K(g, b, k) {g, b, k}
static const struct {
  uint8_t group, bit;
  eadk_key_t key;
} KEYS[] = {
  K(7, kb_Left, eadk_key_left), K(7, kb_Right, eadk_key_right), K(7, kb_Up, eadk_key_up), K(7, kb_Down, eadk_key_down),
  K(1, kb_2nd, eadk_key_ok), K(6, kb_Enter, eadk_key_exe), K(6, kb_Clear, eadk_key_back), K(1, kb_Mode, eadk_key_home),
  K(1, kb_Del, eadk_key_backspace), K(2, kb_Alpha, eadk_key_alpha), K(1, kb_Yequ, eadk_key_shift),
  K(2, kb_Math, eadk_key_toolbox), K(5, kb_Vars, eadk_key_var), K(3, kb_GraphVar, eadk_key_xnt),
  K(3, kb_0, eadk_key_zero), K(3, kb_1, eadk_key_one), K(4, kb_2, eadk_key_two), K(5, kb_3, eadk_key_three),
  K(3, kb_4, eadk_key_four), K(4, kb_5, eadk_key_five), K(5, kb_6, eadk_key_six), K(3, kb_7, eadk_key_seven),
  K(4, kb_8, eadk_key_eight), K(5, kb_9, eadk_key_nine), K(6, kb_Add, eadk_key_plus), K(6, kb_Sub, eadk_key_minus),
  K(6, kb_Mul, eadk_key_multiplication), K(6, kb_Div, eadk_key_division), K(4, kb_DecPnt, eadk_key_dot),
  K(3, kb_Comma, eadk_key_comma), K(4, kb_LParen, eadk_key_left_parenthesis),
  K(5, kb_RParen, eadk_key_right_parenthesis), K(6, kb_Power, eadk_key_power), K(2, kb_Square, eadk_key_square),
  K(2, kb_Ln, eadk_key_ln), K(2, kb_Log, eadk_key_log), K(3, kb_Sin, eadk_key_sine), K(4, kb_Cos, eadk_key_cosine),
  K(5, kb_Tan, eadk_key_tangent), K(5, kb_Chs, eadk_key_ans), /* (ans is 2nd (-) on the CE) */
};

eadk_keyboard_state_t eadk_keyboard_scan(void) {
  kb_Scan();
  eadk_keyboard_state_t s = 0;
  for (unsigned i = 0; i < sizeof KEYS / sizeof KEYS[0]; i++)
    if (kb_Data[KEYS[i].group] & KEYS[i].bit) s |= (eadk_keyboard_state_t)1 << KEYS[i].key;
  if (kb_On) s |= (eadk_keyboard_state_t)1 << eadk_key_home | (eadk_keyboard_state_t)1 << eadk_key_on_off, kb_ClearOnLatch();
  return s;
}

/* key presses as events: each key pressed once, in turn; an arrow or ⌫ held repeats, as on the NumWorks */
#define REPEATS ((eadk_keyboard_state_t)0xF | (eadk_keyboard_state_t)1 << eadk_key_backspace)
eadk_event_t eadk_event_get(int32_t *timeout) {
  static eadk_keyboard_state_t before = ~(eadk_keyboard_state_t)0, pending;
  static uint64_t repeat_at;
  static int held = -1;
  uint64_t t0 = eadk_timing_millis(), t = t0, until = t0 + (uint64_t)(timeout && *timeout > 0 ? *timeout : 0);
  for (;;) {
    eadk_keyboard_state_t now = eadk_keyboard_scan();
    pending |= now & ~before, before = now;
    if (held >= 0 && !(now >> held & 1)) held = -1;
    int k = -1;
    if (pending) {
      for (k = 0; !(pending >> k & 1); k++) {}
      pending &= pending - 1;
      held = REPEATS >> k & 1 ? k : -1, repeat_at = t + 300;
    } else if (held >= 0 && t >= repeat_at) {
      k = held, repeat_at = t + 70;
    }
    if (k >= 0) { /* what is left of the wait */
      if (timeout && *timeout > 0) *timeout = t - t0 >= (uint64_t)*timeout ? 0 : *timeout - (int32_t)(t - t0);
      return (eadk_event_t)k;
    }
    if (t >= until) break;
    t = eadk_timing_millis();
  }
  if (timeout) *timeout = 0;
  return eadk_event_none;
}

/* time: timer 1, 32768 ticks a second, counted in milliseconds on 64 bits (its 32 bits go round in 36 hours) */
static uint32_t ticks0, frac;
static uint64_t ms;
uint64_t eadk_timing_millis(void) {
  uint32_t t = (uint32_t)clock(), d = t - ticks0;
  ticks0 = t;
  frac += (d & 0x7FFF) * 1000; /* (thousandths of a tick: less than 32768 of them a millisecond) */
  ms += (uint64_t)(d >> 15) * 1000 + (frac >> 15);
  frac &= 0x7FFF;
  return ms;
}
void eadk_timing_msleep(uint32_t ms) {
  for (; ms >= 60000; ms -= 60000) ticksleep(60 * CLOCKS_PER_SEC);
  ticksleep((ms * 4096 + 124) / 125); /* ms * 32768 / 1000, rounded up */
}
void eadk_timing_usleep(uint32_t us) {
  for (; us >= 1000000; us -= 1000000) ticksleep(CLOCKS_PER_SEC);
  ticksleep((us * 4096 + 124999) / 125000); /* us * 32768 / 1000000, rounded up */
}
uint32_t eadk_random(void) { return (uint32_t)random() << 16 ^ (uint32_t)random(); }
void eadk_backlight_set_brightness(uint8_t b) { lcd_BacklightLevel = (uint8_t)(255 - b); }
uint8_t eadk_backlight_brightness(void) { return (uint8_t)(255 - lcd_BacklightLevel); }
bool eadk_battery_is_charging(void) { return false; }
uint8_t eadk_battery_level(void) { return 3; }
float eadk_battery_voltage(void) { return 4.0f; }
bool eadk_usb_is_plugged(void) { return false; }
const char *eadk_external_data = 0;
size_t eadk_external_data_size = 0;

/* the files: a 4 KB buffer, kept in the appvar */
#define STORAGE_SIZE 4096
static uint8_t storage[STORAGE_SIZE + 16];
static bool loaded;
static char var[9];

static const char *var_name(void) { /* NP, then the id: its first 5 letters and its last past 6 (NPflappd, NPflappk) */
  if (!var[0]) {
    const char *id = NP_FILES_ID;
    size_t n = strlen(id);
    var[0] = 'N', var[1] = 'P';
    for (size_t i = 0; i < 6 && i < n; i++) var[2 + i] = id[i < 5 || n <= 6 ? i : n - 1];
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
  uint32_t n = storage_end();
  if (n > STORAGE_SIZE - 2) return; /* (a broken list: the last good file stays) */
  uint8_t h = ti_Open(var_name(), "w");
  if (!h) return;
  ti_Write(storage, 1, (size_t)n + 2, h);
  if (archive) ti_SetArchiveStatus(true, h);
  ti_Close(h);
}
void np_storage_changed(void) { keep(false); }

bool np_slow(void) { return true; }
const char *np_target(void) { return "ti84ce"; }
void np_begin(void) {
  ticks0 = (uint32_t)clock();
  srandom(rtc_Time());
  kb_DisableOnLatch();
}
int np_end(void) {
  if (loaded) keep(true);
  /* a key still down (enter on a Quit button) would act on the home screen: let go first, 2 seconds at most */
  for (uint64_t t = eadk_timing_millis(); eadk_keyboard_scan() && eadk_timing_millis() - t < 2000;) {}
  os_ClrHomeFull();
  return 0;
}
