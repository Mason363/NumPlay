/* NumPlay's games on an ARM CPU in an emulator, big- or little-endian, for tools/be_check.py: the fx-CG50's SuperH is
 * big-endian, and a game that reads its data or pixels in place as little-endian numbers goes wrong there. Built both
 * ways with the Casio's sources, data and flags, a game is played with the same keys on the same clock and each
 * frame compared.
 *
 * Everything here is deterministic: the clock moves on its own (a frame waited for, a sleep, each reading of it), the
 * keys come from np_be_script, chance from a fixed seed, the files start empty. np_be_frame and np_be_exit are where
 * the emulator looks. */
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <eadk.h>
#include "np_port.h"

#ifndef NP_APP_ID
#define NP_APP_ID "numplay"
#endif

/* the RAM after a game's own, as on the Casio: 128 KB */
__asm__(".section .bss\n.balign 16\n.globl _heap_start\n_heap_start:\n.space 131072\n.globl _heap_end\n_heap_end:\n"
        ".previous\n");

uint16_t np_be_fb[EADK_SCREEN_WIDTH * EADK_SCREEN_HEIGHT];
/* the keys: held from one time to another (ms), written by the emulator before the game starts */
struct np_be_key {
  uint32_t from, to;
  uint64_t keys;
};
struct np_be_key np_be_script[64];
uint64_t np_be_us; /* the clock */
uint32_t np_be_frames;
static int dirty;

__attribute__((noinline, used)) void np_be_frame(void) {
  static uint64_t last;
  if (np_be_us < last + 16667) np_be_us = last + 16667; /* (60 frames a second at most, as a screen shows them) */
  last = np_be_us;
  np_be_frames++;
  dirty = 0;
  __asm__ volatile("" ::: "memory");
}
__attribute__((noinline, used, noreturn)) void np_be_exit(int code) {
  for (;;) __asm__ volatile("" ::"r"(code) : "memory");
}

/* ---- the screen ---- */
void eadk_display_push_rect(eadk_rect_t r, const eadk_color_t *px) {
  for (int y = 0; y < r.height; y++)
    for (int x = 0; x < r.width; x++) {
      int sx = r.x + x, sy = r.y + y;
      if (sx < EADK_SCREEN_WIDTH && sy < EADK_SCREEN_HEIGHT) np_be_fb[sy * EADK_SCREEN_WIDTH + sx] = px[y * r.width + x];
    }
  dirty = 1;
}
void eadk_display_push_rect_uniform(eadk_rect_t r, eadk_color_t c) {
  for (int y = r.y; y < r.y + r.height && y < EADK_SCREEN_HEIGHT; y++)
    for (int x = r.x; x < r.x + r.width && x < EADK_SCREEN_WIDTH; x++) np_be_fb[y * EADK_SCREEN_WIDTH + x] = c;
  dirty = 1;
}
void eadk_display_pull_rect(eadk_rect_t r, eadk_color_t *px) {
  for (int y = 0; y < r.height; y++)
    for (int x = 0; x < r.width; x++) {
      int sx = r.x + x, sy = r.y + y;
      px[y * r.width + x] = sx < EADK_SCREEN_WIDTH && sy < EADK_SCREEN_HEIGHT ? np_be_fb[sy * EADK_SCREEN_WIDTH + sx] : 0;
    }
}
bool eadk_display_wait_for_vblank(void) {
  np_be_us += 16667;
  np_be_frame();
  return true;
}

/* ---- the keys ---- */
eadk_keyboard_state_t eadk_keyboard_scan(void) {
  np_be_us += 200;
  if (dirty) np_be_frame(); /* (a game that doesn't wait between frames: its frame ends where it reads the keys) */
  uint32_t ms = (uint32_t)np_be_us / 1000u; /* (32 bits: the CPU's own division; a game is played for minutes) */
  eadk_keyboard_state_t s = 0;
  for (unsigned i = 0; i < sizeof np_be_script / sizeof np_be_script[0] && np_be_script[i].to; i++)
    if (ms >= np_be_script[i].from && ms < np_be_script[i].to) s |= np_be_script[i].keys;
  return s;
}
eadk_event_t eadk_event_get(int32_t *timeout) {
  static eadk_keyboard_state_t before = ~(eadk_keyboard_state_t)0;
  uint64_t until = np_be_us + 1000 * (uint64_t)(timeout && *timeout > 0 ? (uint32_t)*timeout : 0u);
  if (dirty) np_be_frame();
  do {
    eadk_keyboard_state_t now = eadk_keyboard_scan(), hit = now & ~before;
    before = now;
    for (int k = 0; k < 64; k++)
      if (hit >> k & 1) return (eadk_event_t)k;
    np_be_us += 10000;
    np_be_frame(); /* (the time the emulator stops at may come while a key is awaited) */
  } while (np_be_us < until);
  if (timeout) *timeout = 0;
  return eadk_event_none;
}

/* ---- time, chance, the rest ---- */
uint64_t eadk_timing_millis(void) {
  np_be_us += 50; /* (a loop waiting on the clock alone gets there) */
  return (uint32_t)np_be_us / 1000u;
}
void eadk_timing_msleep(uint32_t ms) {
  np_be_us += 1000 * (uint64_t)ms;
  np_be_frame(); /* (an idle game sleeps: the emulator sees the time go by) */
}
void eadk_timing_usleep(uint32_t us) { eadk_timing_msleep((us + 999) / 1000); }
uint32_t eadk_random(void) {
  static uint32_t x = 0x9E3779B9u;
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

/* ---- the files: empty at the start, kept in RAM ---- */
#define STORAGE_SIZE (16 * 1024)
static uint8_t storage[STORAGE_SIZE + 64];
uint8_t *np_storage(uint32_t *size) {
  *size = STORAGE_SIZE;
  return storage;
}
void np_storage_changed(void) {}
bool np_slow(void) { return true; }
const char *np_target(void) { return "casio-cg"; }
void np_begin(void) {}
int np_end(void) { return 0; }

/* ---- the start, and the C library's ties ---- */
int main(void);
__attribute__((used)) void np_be_start(void) { np_be_exit(main()); }
void abort(void) { np_be_exit(134); }
void _exit(int c) { np_be_exit(c); }
static uint8_t arena[4 << 20];
static size_t brk;
void *_sbrk(ptrdiff_t n) {
  if (brk + (size_t)n > sizeof arena) {
    errno = ENOMEM;
    return (void *)-1;
  }
  void *p = arena + brk;
  brk += (size_t)n;
  return p;
}
int _write(int f, const void *b, size_t n) { (void)f, (void)b; return (int)n; }
int _read(int f, void *b, size_t n) { (void)f, (void)b, (void)n; return 0; }
int _close(int f) { (void)f; return -1; }
int _fstat(int f, struct stat *s) { (void)f; s->st_mode = S_IFCHR; return 0; }
int _isatty(int f) { (void)f; return 1; }
int _lseek(int f, int o, int w) { (void)f, (void)o, (void)w; return 0; }
int _kill(int p, int s) { (void)p, (void)s; return -1; }
int _getpid(void) { return 1; }
int _gettimeofday(void *t, void *z) { (void)t, (void)z; return -1; }
int _open(const char *p, int f, int m) { (void)p, (void)f, (void)m; return -1; }
int _unlink(const char *p) { (void)p; return -1; }
int _link(const char *a, const char *b) { (void)a, (void)b; return -1; }
/* (setjmp's unwinding table names this, and newlib's small printf the wide characters' output: neither is used) */
void __aeabi_unwind_cpp_pr0(void) { abort(); }
#include <stdio.h>
#include <wchar.h>
wint_t _fputwc_r(struct _reent *r, wchar_t c, FILE *f) { (void)r, (void)f; return (wint_t)c; }
