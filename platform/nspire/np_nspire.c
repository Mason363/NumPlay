/* NumPlay on the TI-Nspire CX and CX II (Ndless): the 320 x 240 screen through lcd_blit, the keys from the keypad
 * (platform/nspire/README.md), the files in /documents/ndless/numplay_<game>.sav.tns.
 *
 * Time: Ndless's clock counts whole seconds, so the second of the fast timers (0x900D0020) runs free at 32768 Hz
 * while the game runs, as the Nspire's SDL does, and is put back as it was at the end. If it doesn't count, time
 * goes by 16 ms a screen drawn (a game's pace stays right, only its clock is rough).
 *
 * Waits read that clock. Not Ndless's msleep: it waits for the timer's interrupt with every other one masked, the ON
 * key's too, and on real calculators the games calling it hundreds of times a second (Undertale, Isaac, Among Us...)
 * soon waited there for good, with only the reset button left.
 *
 * Build defines: NP_APP_ID (the game's id). */
#include <libndls.h>
#include <stdlib.h>
#include <string.h>
#include <eadk.h>
#include "np_port.h"

#ifndef NP_APP_ID
#define NP_APP_ID "numplay"
#endif
#ifndef NP_FILES_ID /* (its files' name: the game's own, or that of the game whose saves it shares) */
#define NP_FILES_ID NP_APP_ID
#endif

/* the RAM after a game's own: 1 MB (the CX has 64) */
__asm__(".section .bss\n.balign 16\n.globl _heap_start\n_heap_start:\n.space 1048576\n.globl _heap_end\n_heap_end:\n"
        ".previous\n");

static uint16_t fb[EADK_SCREEN_WIDTH * EADK_SCREEN_HEIGHT];
static bool started, dirty, timer_ok;
static uint64_t fallback_ms;
static uint32_t last_blit;

/* ---- the clock ---- */
#define T2 ((volatile uint32_t *)0x900D0020) /* load, value, control */
static uint32_t saved_load, saved_control;
static uint32_t ticks_hi, last_raw, spin_ms;
/* a busy loop of about n / spin_ms milliseconds (only for the time a dead clock can't measure) */
static void spin(uint32_t n) {
  for (uint32_t i = 0; i < n; i++) __asm__ volatile("");
}
static void timer_start(void) {
  spin_ms = is_cx2 ? 56000 : 18000; /* (the loop's turns a millisecond: about 7 cycles each, at 396 or 132 MHz) */
  saved_load = T2[0], saved_control = T2[2];
  T2[2] = 0;
  T2[0] = 0xFFFFFFFF;
  T2[2] = 0x82; /* enabled, 32 bits, free running, no interrupt */
  uint32_t before = T2[1];
  spin(2 * spin_ms);
  timer_ok = T2[1] != before;
  last_raw = 0xFFFFFFFF - T2[1];
}
static void timer_stop(void) {
  T2[2] = 0;
  T2[0] = saved_load;
  T2[2] = saved_control;
}
static uint64_t ticks(void) { /* 32768ths of a second since the start */
  uint32_t raw = 0xFFFFFFFF - T2[1];
  if (raw < last_raw) ticks_hi++;
  last_raw = raw;
  return (uint64_t)ticks_hi << 32 | raw;
}
/* n ticks of the clock go by (tools/nspire_emu.py skips the loop, as wfi) */
__attribute__((noinline)) void np_nspire_pause(uint32_t n) {
  uint64_t until = ticks() + n;
  while (ticks() < until) {}
}
static void pause_ms(uint32_t ms) {
  if (!ms) return;
  if (timer_ok) np_nspire_pause((uint32_t)((uint64_t)ms * 32768 / 1000));
  else spin(ms * spin_ms), fallback_ms += ms;
}

static void start(void) {
  if (started) return;
  started = true;
  lcd_init(SCR_320x240_565);
  timer_start();
}

/* ---- the screen ---- */
static void blit(void) {
  if (!dirty) return;
  dirty = false;
  lcd_blit(fb, SCR_320x240_565);
  if (!timer_ok) fallback_ms += 16;
  last_blit = (uint32_t)eadk_timing_millis(); /* (a game showing its frames: none half drawn from push_rect) */
}
void eadk_display_push_rect(eadk_rect_t r, const eadk_color_t *px) {
  start();
  for (int y = 0; y < r.height && r.y + y < EADK_SCREEN_HEIGHT; y++) {
    int w = r.x + r.width > EADK_SCREEN_WIDTH ? EADK_SCREEN_WIDTH - r.x : r.width;
    if (w > 0) memcpy(&fb[(r.y + y) * EADK_SCREEN_WIDTH + r.x], px + y * r.width, (size_t)w * 2);
  }
  dirty = true;
  uint32_t now = (uint32_t)eadk_timing_millis();
  if (now - last_blit >= 16) blit();
}
void eadk_display_push_rect_uniform(eadk_rect_t r, eadk_color_t c) {
  start();
  for (int y = r.y; y < r.y + r.height && y < EADK_SCREEN_HEIGHT; y++)
    for (int x = r.x; x < r.x + r.width && x < EADK_SCREEN_WIDTH; x++) fb[y * EADK_SCREEN_WIDTH + x] = c;
  dirty = true;
}
void eadk_display_pull_rect(eadk_rect_t r, eadk_color_t *px) {
  for (int y = 0; y < r.height; y++)
    for (int x = 0; x < r.width; x++) {
      int sx = r.x + x, sy = r.y + y;
      px[y * r.width + x] = sx < EADK_SCREEN_WIDTH && sy < EADK_SCREEN_HEIGHT ? fb[sy * EADK_SCREEN_WIDTH + sx] : 0;
    }
}
bool eadk_display_wait_for_vblank(void) {
  start();
  blit();
  if (timer_ok) { /* as the NumWorks's: till the screen's next refresh, 60 a second (games pace themselves by it) */
    uint64_t now = ticks(), next = (now * 60 / 32768 + 1) * 32768 / 60;
    np_nspire_pause((uint32_t)(next - now));
  }
  return true;
}

/* ---- the keys: the NumWorks keypad on the Nspire's ---- */
#define K(n, k) {&KEY_NSPIRE_##n, k}
static const struct {
  const t_key *key;
  eadk_key_t to;
} KEYS[] = {
  K(ENTER, eadk_key_ok), K(RET, eadk_key_exe), K(ESC, eadk_key_back),
  K(HOME, eadk_key_home), K(DEL, eadk_key_backspace), K(SHIFT, eadk_key_shift), K(CTRL, eadk_key_alpha),
  K(MENU, eadk_key_toolbox), K(VAR, eadk_key_var), K(TAB, eadk_key_xnt), K(0, eadk_key_zero), K(1, eadk_key_one),
  K(2, eadk_key_two), K(3, eadk_key_three), K(4, eadk_key_four), K(5, eadk_key_five), K(6, eadk_key_six),
  K(7, eadk_key_seven), K(8, eadk_key_eight), K(9, eadk_key_nine), K(PLUS, eadk_key_plus), K(MINUS, eadk_key_minus),
  K(MULTIPLY, eadk_key_multiplication), K(DIVIDE, eadk_key_division), K(PERIOD, eadk_key_dot),
  K(COMMA, eadk_key_comma), K(LP, eadk_key_left_parenthesis), K(RP, eadk_key_right_parenthesis),
  K(EXP, eadk_key_power), K(SQU, eadk_key_square), K(PI, eadk_key_pi), K(SIN, eadk_key_sine), K(COS, eadk_key_cosine),
  K(TAN, eadk_key_tangent), K(EE, eadk_key_ee), K(NEGATIVE, eadk_key_ans), K(eEXP, eadk_key_exp),
  /* the letters: the NumWorks key that types each one with alpha (a e^x, b ln, c log, d i, g sin, h cos, k √...),
     space its minus and ?! its 0, as the NumWorks types them. On a CX, sin and cos are only there, as g and h. */
  K(A, eadk_key_exp), K(B, eadk_key_ln), K(C, eadk_key_log), K(D, eadk_key_imaginary), K(E, eadk_key_comma),
  K(F, eadk_key_power), K(G, eadk_key_sine), K(H, eadk_key_cosine), K(I, eadk_key_tangent), K(J, eadk_key_pi),
  K(K, eadk_key_sqrt), K(L, eadk_key_square), K(M, eadk_key_seven), K(N, eadk_key_eight), K(O, eadk_key_nine),
  K(P, eadk_key_left_parenthesis), K(Q, eadk_key_right_parenthesis), K(R, eadk_key_four), K(S, eadk_key_five),
  K(T, eadk_key_six), K(U, eadk_key_multiplication), K(V, eadk_key_division), K(W, eadk_key_one), K(X, eadk_key_two),
  K(Y, eadk_key_three), K(Z, eadk_key_plus), K(SPACE, eadk_key_minus), K(QUESEXCL, eadk_key_zero),
};
/* the arrows: the touchpad's regions (tpad_arrow_t, the corners two arrows, the middle OK), or the clickpad's keys */
#define A(k) (1ull << eadk_key_##k)
static const eadk_keyboard_state_t PAD[] = {0, A(up), A(up) | A(right), A(right), A(right) | A(down), A(down),
                                           A(down) | A(left), A(left), A(left) | A(up), A(ok)};
static const t_key *const PAD_KEYS[] = {NULL, &KEY_NSPIRE_UP, &KEY_NSPIRE_UPRIGHT, &KEY_NSPIRE_RIGHT,
                                        &KEY_NSPIRE_RIGHTDOWN, &KEY_NSPIRE_DOWN, &KEY_NSPIRE_DOWNLEFT,
                                        &KEY_NSPIRE_LEFT, &KEY_NSPIRE_LEFTUP, &KEY_NSPIRE_CLICK};
eadk_keyboard_state_t eadk_keyboard_scan(void) {
  start();
  /* what was drawn and isn't shown yet, once 16 ms have gone by: a game waiting for a key in a loop of its own,
     drawing nothing more, shows its last lines too */
  if (dirty && (uint32_t)eadk_timing_millis() - last_blit >= 16) blit();
  eadk_keyboard_state_t s = 0;
  for (unsigned i = 0; i < sizeof KEYS / sizeof KEYS[0]; i++)
    if (isKeyPressed(*KEYS[i].key)) s |= (eadk_keyboard_state_t)1 << KEYS[i].to;
  if (is_touchpad) { /* read once: isKeyPressed reads it over I2C for each arrow */
    touchpad_report_t r;
    if (!touchpad_scan(&r) && r.pressed && r.arrow < sizeof PAD / sizeof PAD[0]) s |= PAD[r.arrow];
  } else
    for (unsigned i = 1; i < sizeof PAD / sizeof PAD[0]; i++)
      if (isKeyPressed(*PAD_KEYS[i])) s |= PAD[i];
  if (on_key_pressed()) s |= A(home) | A(on_off);
  return s;
}
eadk_event_t eadk_event_get(int32_t *timeout) {
  static eadk_keyboard_state_t before = ~(eadk_keyboard_state_t)0;
  uint64_t until = eadk_timing_millis() + (uint64_t)(timeout && *timeout > 0 ? *timeout : 0);
  blit(); /* (what was drawn is shown while the key is awaited) */
  do {
    eadk_keyboard_state_t now = eadk_keyboard_scan(), hit = now & ~before;
    before = now;
    for (int k = 0; k < 64; k++)
      if (hit >> k & 1) return (eadk_event_t)k;
    pause_ms(5);
  } while (eadk_timing_millis() < until);
  if (timeout) *timeout = 0;
  return eadk_event_none;
}

/* ---- time, chance, the rest ---- */
uint64_t eadk_timing_millis(void) {
  start();
  return timer_ok ? ticks() * 1000 / 32768 : fallback_ms;
}
void eadk_timing_msleep(uint32_t ms) {
  start();
  blit();
  pause_ms(ms);
}
void eadk_timing_usleep(uint32_t us) { eadk_timing_msleep((us + 999) / 1000); }
uint32_t eadk_random(void) {
  static uint32_t x;
  if (!x) x = (uint32_t)ticks() ^ *(volatile uint32_t *)0x90090000 ^ 0x9E3779B9u;
  x ^= x << 13, x ^= x >> 17, x ^= x << 5;
  return x;
}
void eadk_backlight_set_brightness(uint8_t b) { (void)b; }
uint8_t eadk_backlight_brightness(void) { return 255; }
bool eadk_battery_is_charging(void) { return false; }
uint8_t eadk_battery_level(void) { return 3; }
float eadk_battery_voltage(void) { return 4.0f; }
bool eadk_usb_is_plugged(void) { return false; }
const char *eadk_external_data = NULL;
size_t eadk_external_data_size = 0;

/* ---- the files (the OS's own calls: newlib's fopen leaves its FILE pool on the OS's heap till a restart) ---- */
#define STORAGE_SIZE (64 * 1024)
static uint8_t storage[STORAGE_SIZE + 64];
static bool loaded;
#define SAVE_PATH "/documents/ndless/numplay_" NP_FILES_ID ".sav.tns"
static uint32_t storage_end(void) {
  uint32_t at = 0;
  while (at + 2 <= STORAGE_SIZE && (storage[at] | storage[at + 1] << 8)) at += storage[at] | storage[at + 1] << 8;
  return at;
}
uint8_t *np_storage(uint32_t *size) {
  if (!loaded) {
    loaded = true;
    NUC_FILE *f = nuc_fopen(SAVE_PATH, "rb");
    if (f) {
      size_t n = nuc_fread(storage, 1, STORAGE_SIZE, f);
      nuc_fclose(f);
      if (storage_end() + 2 > n) memset(storage, 0, sizeof storage);
    }
  }
  *size = STORAGE_SIZE;
  return storage;
}
void np_storage_changed(void) {
  NUC_FILE *f = nuc_fopen(SAVE_PATH, "wb");
  if (!f) return;
  nuc_fwrite(storage, 1, storage_end() + 2, f);
  nuc_fclose(f);
}

bool np_slow(void) { return false; }
const char *np_target(void) { return "nspire"; }
void np_begin(void) { start(); }
int np_end(void) {
  if (started) timer_stop(), lcd_init(SCR_TYPE_INVALID), started = false;
  refresh_osscr();
  return 0;
}
