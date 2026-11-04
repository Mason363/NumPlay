/* NumPlay on a computer (Windows, macOS, Linux, a Raspberry Pi) with SDL 3: the 320 x 240 screen in a window, the
 * NumWorks keypad on the keyboard (platform/sdl/README.md), the files in one file per game.
 *
 * Build defines: NP_APP_ID (the game's id: its files' name, or NP_FILES_ID's), NP_APP_NAME (the window's title). In the environment:
 * NP_SAVES the folder for the files (default ~/.numplay), NP_SCALE the window's size, 1 to 6 (default 3).
 *
 * Tests (tools/verify.py), with NP_HEADLESS=1 for no window: NP_KEYS "1500-1700:ok,3000:back" holds keys
 * from one time to another (ms since the start; key names as eadk_key_*, without the prefix), NP_SHOTS
 * "2000:title.ppm,..." saves the screen at those times, NP_QUIT_MS holds Home then (the game saves and ends). */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <eadk.h>
#include "np_port.h"
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#ifndef NP_APP_ID
#define NP_APP_ID "numplay"
#endif
#ifndef NP_FILES_ID /* (its files' name: the game's own, or that of the game whose saves it shares) */
#define NP_FILES_ID NP_APP_ID
#endif
#ifndef NP_APP_NAME
#define NP_APP_NAME "NumPlay"
#endif

static SDL_Window *win;
static SDL_Renderer *ren;
static SDL_Texture *tex;
static uint16_t fb[EADK_SCREEN_WIDTH * EADK_SCREEN_HEIGHT];
static bool dirty, quitting, headless, started;
static bool tapped[SDL_SCANCODE_COUNT];
static uint64_t t0, last_present;

/* ---- scripted keys and pictures (tests) ---- */
static const struct {
  const char *name;
  eadk_key_t key;
} NAMES[] = {{"left", eadk_key_left}, {"up", eadk_key_up}, {"down", eadk_key_down}, {"right", eadk_key_right},
             {"ok", eadk_key_ok}, {"back", eadk_key_back}, {"home", eadk_key_home}, {"shift", eadk_key_shift},
             {"alpha", eadk_key_alpha}, {"xnt", eadk_key_xnt}, {"var", eadk_key_var}, {"toolbox", eadk_key_toolbox},
             {"backspace", eadk_key_backspace}, {"exe", eadk_key_exe}, {"zero", eadk_key_zero}, {"one", eadk_key_one},
             {"two", eadk_key_two}, {"three", eadk_key_three}, {"four", eadk_key_four}, {"five", eadk_key_five},
             {"six", eadk_key_six}, {"seven", eadk_key_seven}, {"eight", eadk_key_eight}, {"nine", eadk_key_nine},
             {"plus", eadk_key_plus}, {"minus", eadk_key_minus}};
static eadk_keyboard_state_t script_keys(uint64_t now) {
  const char *s = getenv("NP_KEYS");
  eadk_keyboard_state_t k = 0;
  while (s && *s) {
    unsigned long a = strtoul(s, (char **)&s, 10), b = a + 90;
    if (*s == '-') b = strtoul(s + 1, (char **)&s, 10);
    if (*s++ != ':') break;
    size_t n = strcspn(s, ",");
    for (size_t i = 0; i < sizeof NAMES / sizeof NAMES[0]; i++)
      if (strlen(NAMES[i].name) == n && !strncmp(s, NAMES[i].name, n) && now >= a && now <= b)
        k |= (eadk_keyboard_state_t)1 << NAMES[i].key;
    s += n;
    if (*s == ',') s++;
  }
  const char *q = getenv("NP_QUIT_MS");
  if (q && now >= strtoull(q, NULL, 10)) k |= (eadk_keyboard_state_t)1 << eadk_key_home;
  return k;
}
static void script_shots(uint64_t now) {
  static int done;
  const char *s = getenv("NP_SHOTS");
  for (int i = 0; s && *s; i++) {
    unsigned long t = strtoul(s, (char **)&s, 10);
    if (*s++ != ':') return;
    size_t n = strcspn(s, ",");
    if (i >= done && now >= t) {
      char path[512];
      snprintf(path, sizeof path, "%.*s", (int)n, s);
      FILE *f = fopen(path, "wb");
      if (f) {
        fprintf(f, "P6\n%d %d\n255\n", EADK_SCREEN_WIDTH, EADK_SCREEN_HEIGHT);
        for (int p = 0; p < EADK_SCREEN_WIDTH * EADK_SCREEN_HEIGHT; p++) {
          uint16_t c = fb[p];
          uint8_t rgb[3] = {(uint8_t)((c >> 11) * 255 / 31), (uint8_t)((c >> 5 & 63) * 255 / 63), (uint8_t)((c & 31) * 255 / 31)};
          fwrite(rgb, 1, 3, f);
        }
        fclose(f);
      }
      done = i + 1;
    }
    s += n;
    if (*s == ',') s++;
  }
}

/* the RAM after a game's own (the NumWorks gives a game what its software leaves): 256 KB here */
#define NP_HEAP_SIZE (256 * 1024)
#if defined(__EMSCRIPTEN__)
/* (WebAssembly: two labels in one block of the module's data) */
__asm__(".section .bss.np_heap,\"\",@\n.p2align 4\n.globl _heap_start\n_heap_start:\n.skip 262144\n.size _heap_start, 262144\n"
        ".globl _heap_end\n_heap_end:\n.skip 16\n.size _heap_end, 16\n");
#elif defined(__APPLE__)
__asm__(".data\n.p2align 4\n.globl __heap_start\n__heap_start:\n.space 262144\n.globl __heap_end\n__heap_end:\n");
#else
__asm__(".section .bss\n.balign 16\n.globl _heap_start\n_heap_start:\n.space 262144\n.globl _heap_end\n_heap_end:\n"
        ".previous\n");
#endif

static void start(void) {
  if (started) return;
  started = true;
  headless = getenv("NP_HEADLESS") != NULL;
  if (headless) {
    SDL_Init(0);
    t0 = SDL_GetTicks();
    return;
  }
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    fprintf(stderr, "NumPlay: %s\n", SDL_GetError());
    exit(1);
  }
  int scale = getenv("NP_SCALE") ? atoi(getenv("NP_SCALE")) : 3;
  if (scale < 1 || scale > 6) scale = 3;
  win = SDL_CreateWindow(NP_APP_NAME, EADK_SCREEN_WIDTH * scale, EADK_SCREEN_HEIGHT * scale, SDL_WINDOW_RESIZABLE);
  ren = win ? SDL_CreateRenderer(win, NULL) : NULL;
  if (win && !ren) ren = SDL_CreateRenderer(win, "software"); /* (no GPU: tests, a bare Raspberry Pi) */
  tex = ren ? SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, EADK_SCREEN_WIDTH,
                                EADK_SCREEN_HEIGHT)
            : NULL;
  if (!tex) {
    fprintf(stderr, "NumPlay: %s\n", SDL_GetError());
    exit(1);
  }
  SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
  SDL_SetRenderLogicalPresentation(ren, EADK_SCREEN_WIDTH, EADK_SCREEN_HEIGHT, SDL_LOGICAL_PRESENTATION_LETTERBOX);
  t0 = SDL_GetTicks();
}

static void present(void) {
#ifdef __EMSCRIPTEN__
  emscripten_sleep(0); /* (the page gets its turn: the game's loop never returns, Asyncify lets it pause) */
#endif
  last_present = SDL_GetTicks();
  if (!dirty || headless) return;
  dirty = false;
  SDL_UpdateTexture(tex, NULL, fb, EADK_SCREEN_WIDTH * 2);
  SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
  SDL_RenderClear(ren);
  SDL_RenderTexture(ren, tex, NULL, NULL);
  SDL_RenderPresent(ren);
}
/* the window is kept alive: events read, and the screen shown 60 times a second at most */
static void pump(void) {
  start();
  SDL_Event e;
  while (!headless && SDL_PollEvent(&e)) {
    if (e.type == SDL_EVENT_QUIT) quitting = true;
    if (e.type == SDL_EVENT_KEY_DOWN) tapped[e.key.scancode] = true; /* (a tap shorter than a frame still counts) */
  }
  script_shots(SDL_GetTicks() - t0);
  if (SDL_GetTicks() - last_present >= 16) present();
}

/* ---- the screen ---- */
void eadk_display_push_rect(eadk_rect_t r, const eadk_color_t *px) {
  start();
  for (int y = 0; y < r.height; y++) {
    int sy = r.y + y;
    if (sy >= EADK_SCREEN_HEIGHT) break;
    for (int x = 0; x < r.width; x++)
      if (r.x + x < EADK_SCREEN_WIDTH) fb[sy * EADK_SCREEN_WIDTH + r.x + x] = px[y * r.width + x];
  }
  dirty = true;
  if (SDL_GetTicks() - last_present >= 16) pump();
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
  pump();
  present();
  return true;
}

/* ---- the keyboard: the NumWorks keypad on a computer's keys ---- */
static const struct {
  SDL_Scancode sc;
  eadk_key_t key;
} KEYS[] = {
  {SDL_SCANCODE_LEFT, eadk_key_left}, {SDL_SCANCODE_RIGHT, eadk_key_right}, {SDL_SCANCODE_UP, eadk_key_up},
  {SDL_SCANCODE_DOWN, eadk_key_down}, {SDL_SCANCODE_RETURN, eadk_key_ok}, {SDL_SCANCODE_KP_ENTER, eadk_key_exe},
  {SDL_SCANCODE_SPACE, eadk_key_exe}, {SDL_SCANCODE_ESCAPE, eadk_key_back}, {SDL_SCANCODE_HOME, eadk_key_home},
  {SDL_SCANCODE_LSHIFT, eadk_key_shift}, {SDL_SCANCODE_RSHIFT, eadk_key_shift}, {SDL_SCANCODE_LALT, eadk_key_alpha},
  {SDL_SCANCODE_RALT, eadk_key_alpha}, {SDL_SCANCODE_TAB, eadk_key_toolbox}, {SDL_SCANCODE_BACKSPACE, eadk_key_backspace},
  {SDL_SCANCODE_DELETE, eadk_key_backspace}, {SDL_SCANCODE_V, eadk_key_var}, {SDL_SCANCODE_X, eadk_key_xnt},
  {SDL_SCANCODE_0, eadk_key_zero}, {SDL_SCANCODE_1, eadk_key_one}, {SDL_SCANCODE_2, eadk_key_two},
  {SDL_SCANCODE_3, eadk_key_three}, {SDL_SCANCODE_4, eadk_key_four}, {SDL_SCANCODE_5, eadk_key_five},
  {SDL_SCANCODE_6, eadk_key_six}, {SDL_SCANCODE_7, eadk_key_seven}, {SDL_SCANCODE_8, eadk_key_eight},
  {SDL_SCANCODE_9, eadk_key_nine}, {SDL_SCANCODE_KP_0, eadk_key_zero}, {SDL_SCANCODE_KP_1, eadk_key_one},
  {SDL_SCANCODE_KP_2, eadk_key_two}, {SDL_SCANCODE_KP_3, eadk_key_three}, {SDL_SCANCODE_KP_4, eadk_key_four},
  {SDL_SCANCODE_KP_5, eadk_key_five}, {SDL_SCANCODE_KP_6, eadk_key_six}, {SDL_SCANCODE_KP_7, eadk_key_seven},
  {SDL_SCANCODE_KP_8, eadk_key_eight}, {SDL_SCANCODE_KP_9, eadk_key_nine}, {SDL_SCANCODE_EQUALS, eadk_key_plus},
  {SDL_SCANCODE_KP_PLUS, eadk_key_plus}, {SDL_SCANCODE_MINUS, eadk_key_minus}, {SDL_SCANCODE_KP_MINUS, eadk_key_minus},
  {SDL_SCANCODE_KP_MULTIPLY, eadk_key_multiplication}, {SDL_SCANCODE_SLASH, eadk_key_division},
  {SDL_SCANCODE_KP_DIVIDE, eadk_key_division}, {SDL_SCANCODE_PERIOD, eadk_key_dot}, {SDL_SCANCODE_COMMA, eadk_key_comma},
  {SDL_SCANCODE_LEFTBRACKET, eadk_key_left_parenthesis}, {SDL_SCANCODE_RIGHTBRACKET, eadk_key_right_parenthesis},
  {SDL_SCANCODE_E, eadk_key_exp}, {SDL_SCANCODE_L, eadk_key_ln}, {SDL_SCANCODE_S, eadk_key_sine},
  {SDL_SCANCODE_C, eadk_key_cosine}, {SDL_SCANCODE_T, eadk_key_tangent}, {SDL_SCANCODE_P, eadk_key_pi},
  {SDL_SCANCODE_R, eadk_key_sqrt}, {SDL_SCANCODE_Q, eadk_key_square}, {SDL_SCANCODE_A, eadk_key_ans},
};

eadk_keyboard_state_t eadk_keyboard_scan(void) {
  pump();
  int n = 0;
  const bool *down = headless ? NULL : SDL_GetKeyboardState(&n);
  eadk_keyboard_state_t s = 0;
  for (size_t i = 0; i < sizeof KEYS / sizeof KEYS[0]; i++)
    if (((int)KEYS[i].sc < n && down[KEYS[i].sc]) || tapped[KEYS[i].sc]) s |= (eadk_keyboard_state_t)1 << KEYS[i].key;
  memset(tapped, 0, sizeof tapped);
  if (quitting) s |= (eadk_keyboard_state_t)1 << eadk_key_home | (eadk_keyboard_state_t)1 << eadk_key_on_off;
  return s | script_keys(SDL_GetTicks() - t0);
}

/* a key press as an event, waiting up to *timeout ms */
eadk_event_t eadk_event_get(int32_t *timeout) {
  static eadk_keyboard_state_t before = ~(eadk_keyboard_state_t)0;
  uint64_t until = SDL_GetTicks() + (uint64_t)(timeout && *timeout > 0 ? *timeout : 0);
  do {
    eadk_keyboard_state_t now = eadk_keyboard_scan(), hit = now & ~before;
    before = now;
    for (int k = 0; k < 64; k++)
      if (hit >> k & 1) return (eadk_event_t)k;
    SDL_Delay(5);
  } while (SDL_GetTicks() < until);
  if (timeout) *timeout = 0;
  return eadk_event_none;
}

/* ---- time, chance, the rest ---- */
uint64_t eadk_timing_millis(void) {
  start();
  return SDL_GetTicks() - t0;
}
void eadk_timing_msleep(uint32_t ms) {
  pump();
  present();
  SDL_Delay(ms);
}
void eadk_timing_usleep(uint32_t us) { eadk_timing_msleep((us + 999) / 1000); }
uint32_t eadk_random(void) {
  static uint32_t x;
  if (!x) x = (uint32_t)SDL_GetPerformanceCounter() | 1;
  x ^= x << 13, x ^= x >> 17, x ^= x << 5;
  return x;
}
void eadk_backlight_set_brightness(uint8_t b) { (void)b; }
uint8_t eadk_backlight_brightness(void) { return 255; }
bool eadk_battery_is_charging(void) { return false; }
uint8_t eadk_battery_level(void) { return 3; }
float eadk_battery_voltage(void) { return 4.2f; }
bool eadk_usb_is_plugged(void) { return false; }
const char *eadk_external_data = NULL;
size_t eadk_external_data_size = 0;

/* ---- the files: one file, NP_SAVES/<game>.files ---- */
#define STORAGE_SIZE (64 * 1024)
static uint8_t storage[STORAGE_SIZE + 64]; /* (the records, then room for the cache words after them) */
static bool storage_loaded;

static void storage_path(char *p, size_t n) {
  const char *dir = getenv("NP_SAVES");
  char home[512];
  if (!dir) {
    const char *h = getenv("HOME");
    if (!h) h = getenv("USERPROFILE");
    snprintf(home, sizeof home, "%s/.numplay", h ? h : ".");
    SDL_CreateDirectory(home);
    dir = home;
  }
  snprintf(p, n, "%s/%s.files", dir, NP_FILES_ID);
}

#ifdef __EMSCRIPTEN__
/* in a browser: the records in the page's localStorage, as base64 */
EM_JS(int, web_load, (const char *key, uint8_t *buf, int max), {
  var v = localStorage.getItem(UTF8ToString(key));
  if (!v) return 0;
  var b = atob(v), n = Math.min(b.length, max);
  for (var i = 0; i < n; i++) HEAPU8[buf + i] = b.charCodeAt(i);
  return n;
});
EM_JS(void, web_save, (const char *key, const uint8_t *buf, int n), {
  var s = "";
  for (var i = 0; i < n; i++) s += String.fromCharCode(HEAPU8[buf + i]);
  try { localStorage.setItem(UTF8ToString(key), btoa(s)); } catch (e) {}
});
#endif
uint8_t *np_storage(uint32_t *size) {
  if (!storage_loaded) {
    storage_loaded = true;
#ifdef __EMSCRIPTEN__
    size_t n = (size_t)web_load("numplay:" NP_FILES_ID, storage, STORAGE_SIZE);
    if (n) {
#else
    char p[600];
    storage_path(p, sizeof p);
    FILE *f = fopen(p, "rb");
    if (f) {
      size_t n = fread(storage, 1, STORAGE_SIZE, f);
      fclose(f);
#endif
      /* (a file that isn't a list of records ending with a zero size starts empty) */
      uint32_t at = 0;
      while (at + 2 <= n && (storage[at] | storage[at + 1] << 8)) at += storage[at] | storage[at + 1] << 8;
      if (at + 2 > n) memset(storage, 0, sizeof storage);
    }
  }
  *size = STORAGE_SIZE;
  return storage;
}

void np_storage_changed(void) {
#ifdef __EMSCRIPTEN__
  uint32_t used = 0;
  while (used + 2 <= STORAGE_SIZE && (storage[used] | storage[used + 1] << 8)) used += storage[used] | storage[used + 1] << 8;
  web_save("numplay:" NP_FILES_ID, storage, (int)used + 2);
  return;
#endif
  char p[600], tmp[620];
  storage_path(p, sizeof p);
  snprintf(tmp, sizeof tmp, "%s.new", p);
  uint32_t end = 0;
  while (end + 2 <= STORAGE_SIZE && (storage[end] | storage[end + 1] << 8)) end += storage[end] | storage[end + 1] << 8;
  FILE *f = fopen(tmp, "wb");
  if (!f) return;
  bool ok = fwrite(storage, 1, end + 2, f) == end + 2;
  ok = fclose(f) == 0 && ok;
  if (ok) rename(tmp, p); /* (the old file stays until the new one is whole) */
}

bool np_slow(void) { return false; }
const char *np_target(void) {
#ifdef __EMSCRIPTEN__
  return "web";
#else
  return "sdl";
#endif
}
void np_begin(void) { start(); }
int np_end(void) {
  script_shots(~(uint64_t)0 >> 1); /* (the pictures not taken yet: the last screen) */
  if (win) {
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    win = NULL;
  }
  return 0;
}
