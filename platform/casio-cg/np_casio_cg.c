/* NumPlay on the Casio fx-CG50, Graph 90+E, fx-CG10 and fx-CG20, with libfxcg (the Prizm SDK).
 *
 * The screen: the 320 x 240 picture in the middle of the 384 x 216 one, its 240 rows shown on 216 (one in ten left
 * out), drawn straight into the calculator's screen memory as the game pushes its rows. The keys: read from the
 * keypad's own registers, every key held (platform/casio-cg/README.md has the map). The time: the calculator's
 * clock, 128 ticks a second. The files: <game>.sav in the storage memory, written when the game saves (at most
 * every few seconds) and when it ends.
 *
 * Build defines: NP_APP_ID (the game's id). */
#include <fxcg/display.h>
#include <fxcg/file.h>
#include <fxcg/keyboard.h>
#include <fxcg/rtc.h>
#include <fxcg/system.h>
#include <stdlib.h>
#include <string.h>
#include <eadk.h>
#include "np_port.h"

#ifndef NP_APP_ID
#define NP_APP_ID "numplay"
#endif

/* the RAM after a game's own: 128 KB (the add-in's 512 KB hold its data, this and the stack) */
__asm__(".section .bss\n.balign 16\n.global __heap_start\n__heap_start:\n.space 131072\n.global __heap_end\n__heap_end:\n"
        ".previous\n");

#define VW 384
#define VH 216
#define OX ((VW - EADK_SCREEN_WIDTH) / 2)
static uint16_t *vram;
static int16_t row_to[EADK_SCREEN_HEIGHT]; /* the screen row a picture row goes to, -1: left out */
static int16_t row_from[VH];              /* the picture row a screen row shows */
static bool started, dirty;
static int last_update;

static int ticks(void) { return RTC_GetTicks(); } /* 1/128 s, from midnight */
static void start(void) {
  if (started) return;
  started = true;
  Bdisp_EnableColor(1);
  EnableStatusArea(3); /* no status bar */
  vram = (uint16_t *)GetVRAMAddress();
  for (int y = 0; y < EADK_SCREEN_HEIGHT; y++) row_to[y] = -1;
  for (int d = 0; d < VH; d++) {
    int s = d * EADK_SCREEN_HEIGHT / VH;
    row_from[d] = (int16_t)s;
    row_to[s] = (int16_t)d;
  }
  memset(vram, 0, VW * VH * 2);
  last_update = ticks();
}

/* ---- the screen ---- */
static void update(void) {
  if (!dirty) return;
  dirty = false;
  Bdisp_PutDisp_DD();
  last_update = ticks();
}
static void maybe_update(void) {
  int t = ticks();
  if (t - last_update >= 2 || t < last_update) update(); /* (about 60 times a second at most) */
}
void eadk_display_push_rect(eadk_rect_t r, const eadk_color_t *px) {
  start();
  int w = r.x + r.width > EADK_SCREEN_WIDTH ? EADK_SCREEN_WIDTH - r.x : r.width;
  if (w <= 0) return;
  for (int y = 0; y < r.height && r.y + y < EADK_SCREEN_HEIGHT; y++) {
    int d = row_to[r.y + y];
    if (d >= 0) memcpy(&vram[d * VW + OX + r.x], px + y * r.width, (size_t)w * 2);
  }
  dirty = true;
  maybe_update();
}
void eadk_display_push_rect_uniform(eadk_rect_t r, eadk_color_t c) {
  start();
  for (int y = r.y; y < r.y + r.height && y < EADK_SCREEN_HEIGHT; y++) {
    int d = row_to[y];
    if (d < 0) continue;
    uint16_t *o = &vram[d * VW + OX];
    for (int x = r.x; x < r.x + r.width && x < EADK_SCREEN_WIDTH; x++) o[x] = c;
  }
  dirty = true;
}
void eadk_display_pull_rect(eadk_rect_t r, eadk_color_t *px) {
  start();
  for (int y = 0; y < r.height; y++) {
    int sy = r.y + y, d = sy < EADK_SCREEN_HEIGHT ? row_to[sy] : -1;
    if (d < 0 && sy < EADK_SCREEN_HEIGHT) d = row_to[sy > 0 ? sy - 1 : sy + 1]; /* (a row left out: its neighbour) */
    for (int x = 0; x < r.width; x++) {
      int sx = r.x + x;
      px[y * r.width + x] = d >= 0 && sx < EADK_SCREEN_WIDTH ? vram[d * VW + OX + sx] : 0;
    }
  }
}
bool eadk_display_wait_for_vblank(void) {
  start();
  update();
  return true;
}

/* ---- the keys: the keypad's registers, a bit a key (row and column of the keys' numbers, KEY_PRGM_*) ---- */
static const struct {
  uint8_t code;
  uint8_t to;
} KEYS[] = {
  {KEY_PRGM_LEFT, eadk_key_left}, {KEY_PRGM_RIGHT, eadk_key_right}, {KEY_PRGM_UP, eadk_key_up},
  {KEY_PRGM_DOWN, eadk_key_down}, {KEY_PRGM_RETURN, eadk_key_ok}, {KEY_PRGM_F1, eadk_key_exe},
  {KEY_PRGM_EXIT, eadk_key_back}, {KEY_PRGM_MENU, eadk_key_home}, {44, eadk_key_backspace},
  {KEY_PRGM_SHIFT, eadk_key_shift}, {KEY_PRGM_ALPHA, eadk_key_alpha}, {KEY_PRGM_OPTN, eadk_key_toolbox},
  {58, eadk_key_var}, {76, eadk_key_xnt}, {KEY_PRGM_0, eadk_key_zero}, {KEY_PRGM_1, eadk_key_one},
  {KEY_PRGM_2, eadk_key_two}, {KEY_PRGM_3, eadk_key_three}, {KEY_PRGM_4, eadk_key_four}, {KEY_PRGM_5, eadk_key_five},
  {KEY_PRGM_6, eadk_key_six}, {KEY_PRGM_7, eadk_key_seven}, {KEY_PRGM_8, eadk_key_eight},
  {KEY_PRGM_9, eadk_key_nine}, {42, eadk_key_plus}, {32, eadk_key_minus}, {43, eadk_key_multiplication},
  {33, eadk_key_division}, {61, eadk_key_dot}, {35, eadk_key_comma}, {55, eadk_key_left_parenthesis},
  {45, eadk_key_right_parenthesis}, {57, eadk_key_power}, {67, eadk_key_square}, {56, eadk_key_ln},
  {66, eadk_key_log}, {46, eadk_key_sine}, {36, eadk_key_cosine}, {26, eadk_key_tangent}, {51, eadk_key_ee},
  {41, eadk_key_ans}, {75, eadk_key_exp}, {65, eadk_key_pi}, {25, eadk_key_sqrt}, {KEY_PRGM_F2, eadk_key_imaginary},
  {KEY_PRGM_ACON, eadk_key_home},
};
static bool held(const uint16_t *k, int code) {
  int row = code % 10, col = code / 10 - 1;
  return k[row >> 1] >> (col + 8 * (row & 1)) & 1;
}
eadk_keyboard_state_t eadk_keyboard_scan(void) {
  start();
  uint16_t k[8];
  memcpy(k, (const void *)0xA44B0000, sizeof k);
  eadk_keyboard_state_t s = 0;
  for (unsigned i = 0; i < sizeof KEYS / sizeof KEYS[0]; i++)
    if (held(k, KEYS[i].code)) s |= (eadk_keyboard_state_t)1 << KEYS[i].to;
  return s;
}
eadk_event_t eadk_event_get(int32_t *timeout) {
  static eadk_keyboard_state_t before = ~(eadk_keyboard_state_t)0;
  uint64_t until = eadk_timing_millis() + (uint64_t)(timeout && *timeout > 0 ? *timeout : 0);
  do {
    eadk_keyboard_state_t now = eadk_keyboard_scan(), hit = now & ~before;
    before = now;
    for (int k = 0; k < 64; k++)
      if (hit >> k & 1) return (eadk_event_t)k;
    update();
    OS_InnerWait_ms(5);
  } while (eadk_timing_millis() < until);
  if (timeout) *timeout = 0;
  return eadk_event_none;
}

/* ---- time, chance, the rest ---- */
uint64_t eadk_timing_millis(void) {
  static int first = -1, wraps;
  static int before;
  int t = ticks();
  if (first < 0) first = before = t;
  if (t < before) wraps++; /* (midnight) */
  before = t;
  return (uint64_t)((int64_t)wraps * 128 * 86400 + t - first) * 1000 / 128;
}
void eadk_timing_msleep(uint32_t ms) {
  start();
  update();
  if (ms) OS_InnerWait_ms((int)ms);
}
void eadk_timing_usleep(uint32_t us) { eadk_timing_msleep((us + 999) / 1000); }
uint32_t eadk_random(void) {
  static uint32_t x;
  if (!x) x = (uint32_t)ticks() * 2654435761u ^ 0x9E3779B9u ^ *(volatile uint32_t *)0xA449000C;
  x ^= x << 13, x ^= x >> 17, x ^= x << 5;
  return x;
}
void eadk_backlight_set_brightness(uint8_t b) { (void)b; }
uint8_t eadk_backlight_brightness(void) { return 255; }
bool eadk_battery_is_charging(void) { return false; }
uint8_t eadk_battery_level(void) { return 3; }
float eadk_battery_voltage(void) { return (float)GetMainBatteryVoltage(1) / 100.0f; }
bool eadk_usb_is_plugged(void) { return false; }
const char *eadk_external_data = NULL;
size_t eadk_external_data_size = 0;

/* ---- the files: <game>.sav in the storage memory ---- */
#define STORAGE_SIZE (16 * 1024)
static uint8_t storage[STORAGE_SIZE + 64];
static bool loaded, unsaved;
static int last_save;
static void save_path(uint16_t *out) {
  char p[40] = "\\\\fls0\\";
  strncat(p, NP_APP_ID, 8);
  strcat(p, ".sav");
  Bfile_StrToName_ncpy(out, p, 40);
}
static uint32_t storage_end(void) {
  uint32_t at = 0;
  while (at + 2 <= STORAGE_SIZE && (storage[at] | storage[at + 1] << 8)) at += storage[at] | storage[at + 1] << 8;
  return at;
}
uint8_t *np_storage(uint32_t *size) {
  if (!loaded) {
    loaded = true;
    uint16_t path[40];
    save_path(path);
    int h = Bfile_OpenFile_OS(path, READ, 0);
    if (h >= 0) {
      int n = Bfile_GetFileSize_OS(h);
      if (n > STORAGE_SIZE) n = STORAGE_SIZE;
      if (n > 0) Bfile_ReadFile_OS(h, storage, n, 0);
      Bfile_CloseFile_OS(h);
      if (n < 2 || storage_end() + 2 > (uint32_t)n) memset(storage, 0, sizeof storage);
    }
  }
  *size = STORAGE_SIZE;
  return storage;
}
static void save_now(void) {
  uint16_t path[40];
  save_path(path);
  size_t n = (storage_end() + 2 + 1) & ~(size_t)1; /* (an even size) */
  Bfile_DeleteEntry(path);
  if (Bfile_CreateEntry_OS(path, CREATEMODE_FILE, &n) < 0) return;
  int h = Bfile_OpenFile_OS(path, WRITE, 0);
  if (h < 0) return;
  Bfile_WriteFile_OS(h, storage, (int)n);
  Bfile_CloseFile_OS(h);
  unsaved = false;
  last_save = ticks();
}
void np_storage_changed(void) {
  unsaved = true;
  int t = ticks();
  if (t - last_save >= 5 * 128 || t < last_save) save_now(); /* (flash is slow to write: a few seconds apart) */
}

bool np_slow(void) { return true; }
const char *np_target(void) { return "casio-cg"; }
void np_begin(void) { start(); }
int np_end(void) {
  if (unsaved) save_now();
  return 0;
}

/* ---- the C library's ties to the calculator (newlib, built without its own malloc and system calls) ---- */
#include <fxcg/heap.h>
#include <errno.h>
#include <sys/stat.h>
void *malloc(size_t n) { return sys_malloc((int)n); }
void free(void *p) {
  if (p) sys_free(p);
}
void *calloc(size_t n, size_t s) {
  void *p = sys_malloc((int)(n * s));
  if (p) memset(p, 0, n * s);
  return p;
}
void *realloc(void *p, size_t n) { return p ? sys_realloc(p, (int)n) : sys_malloc((int)n); }
void *_malloc_r(struct _reent *r, size_t n) { (void)r; return malloc(n); }
void _free_r(struct _reent *r, void *p) { (void)r; free(p); }
void *_calloc_r(struct _reent *r, size_t n, size_t s) { (void)r; return calloc(n, s); }
void *_realloc_r(struct _reent *r, void *p, size_t n) { (void)r; return realloc(p, n); }
void abort(void) {
  for (;;) {
    int key;
    GetKey(&key);
  }
}
void *_sbrk(ptrdiff_t n) { (void)n; errno = ENOMEM; return (void *)-1; }
int _write(int f, const void *b, size_t n) { (void)f, (void)b; return (int)n; }
int _read(int f, void *b, size_t n) { (void)f, (void)b, (void)n; return 0; }
int _close(int f) { (void)f; return -1; }
int _fstat(int f, struct stat *s) { (void)f; s->st_mode = S_IFCHR; return 0; }
int _isatty(int f) { (void)f; return 1; }
int _lseek(int f, int o, int w) { (void)f, (void)o, (void)w; return 0; }
void _exit(int c) { (void)c; abort(); }
int _kill(int p, int s) { (void)p, (void)s; return -1; }
int _getpid(void) { return 1; }
int _gettimeofday(void *t, void *z) { (void)t, (void)z; return -1; }
