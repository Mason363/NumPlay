/* The TI-84 Plus CE version on a computer, for testing: the calculator's 8-bit screen and palette, its keys, its
 * appvars (the .bin files in gen/, as tools/mkart.py makes them) and the save (a file), with SDL 3 for a window.
 *
 * In the environment, as platform/sdl/np_sdl.c: NP_HEADLESS=1 for no window (and a clock that only moves when the
 * game sleeps: fast, and the same every run), NP_KEYS "1500-1700:ok,3000:back" holds keys from one time to another
 * (ms; NumWorks names as eadk_key_* or the CE's: 2nd enter clear mode del y= math vars), NP_SHOTS "2000:a.ppm,..."
 * saves the screen then, NP_QUIT_MS holds mode then (the game saves and ends), NP_SAVE the save's file
 * (default bin/host/NPbalato.bin). Keys in the window: arrows, Z or space 2nd, return enter, backspace clear,
 * escape mode, D del, S y=, A alpha, M math, V vars. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <eadk.h>
#include "ce.h"
#include "np_port.h"
#ifdef __has_include
#if __has_include(<SDL3/SDL.h>)
#include <SDL3/SDL.h>
#define HAVE_SDL 1
#endif
#endif

int balatro_main(void);

static uint8_t vram[SCR_W * SCR_H];
uint8_t *ce_vram = vram;
static uint16_t palette[256];
static int headless;
static uint64_t vclock, t0;
static long frames;

void ce_set_palette(int first, int n, const uint16_t *c) { memcpy(palette + first, c, (size_t)n * 2); }

static struct {
  char name[9];
  uint8_t *data;
  size_t size;
} vars[24];
const uint8_t *ce_appvar(const char *name, size_t *size) {
  for (int i = 0; i < 24; i++) {
    if (vars[i].data && !strcmp(vars[i].name, name)) {
      *size = vars[i].size;
      return vars[i].data;
    }
    if (!vars[i].data) {
      char path[64];
      snprintf(path, sizeof path, "gen/%s.bin", name);
      FILE *f = fopen(path, "rb");
      if (!f) return 0;
      fseek(f, 0, SEEK_END);
      long n = ftell(f);
      fseek(f, 0, SEEK_SET);
      vars[i].data = malloc((size_t)n + 16);
      if (fread(vars[i].data, 1, (size_t)n, f) != (size_t)n) n = 0;
      fclose(f);
      snprintf(vars[i].name, sizeof vars[i].name, "%s", name);
      vars[i].size = (size_t)n;
      *size = (size_t)n;
      return vars[i].data;
    }
  }
  return 0;
}

/* ---- zx0 (as the CE's C library unpacks it) */
static const uint8_t *zin;
static uint8_t zmask, zbits, zlast, zback;
static uint8_t zbyte(void) { return zlast = *zin++; }
static int zbit(void) {
  if (zback) {
    zback = 0;
    return zlast & 1;
  }
  zmask >>= 1;
  if (!zmask) zmask = 128, zbits = zbyte();
  return zbits & zmask ? 1 : 0;
}
static int zgamma(int inv) {
  int v = 1;
  while (!zbit()) v = v << 1 | (zbit() ^ inv);
  return v;
}
void zx0_Decompress(void *dst, const void *src) {
  uint8_t *out = dst;
  zin = src, zmask = 0, zback = 0;
  int off = 1, len;
  for (;;) {
    len = zgamma(0);
    while (len--) *out++ = zbyte();
    if (!zbit()) {
      len = zgamma(0);
      while (len--) *out = out[-off], out++;
      if (!zbit()) continue;
    }
    for (;;) {
      off = zgamma(1);
      if (off == 256) return;
      off = off * 128 - (zbyte() >> 1);
      zback = 1;
      len = zgamma(0) + 1;
      while (len--) *out = out[-off], out++;
      if (!zbit()) break;
    }
  }
}

/* ---- time */
#ifdef HAVE_SDL
static uint64_t real_ms(void) { return SDL_GetTicks(); }
#else
#include <time.h>
static uint64_t real_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}
#endif
uint64_t eadk_timing_millis(void) { return headless ? vclock : real_ms() - t0; }
uint32_t ce_ms(void) { return (uint32_t)eadk_timing_millis(); }
void eadk_timing_msleep(uint32_t ms) {
  if (headless) vclock += ms;
#ifdef HAVE_SDL
  else SDL_Delay(ms);
#endif
}

/* ---- the screen */
static void to_rgb(uint8_t *rgb) {
  for (int i = 0; i < SCR_W * SCR_H; i++) {
    uint16_t c = palette[vram[i]];
    int r = c >> 10 & 31, g = c >> 5 & 31, b = c & 31;
    rgb[3 * i] = (uint8_t)(r << 3 | r >> 2), rgb[3 * i + 1] = (uint8_t)(g << 3 | g >> 2), rgb[3 * i + 2] = (uint8_t)(b << 3 | b >> 2);
  }
}
static void shot(const char *path) {
  static uint8_t rgb[SCR_W * SCR_H * 3];
  to_rgb(rgb);
  FILE *f = fopen(path, "wb");
  if (!f) return;
  fprintf(f, "P6\n%d %d\n255\n", SCR_W, SCR_H);
  fwrite(rgb, 1, sizeof rgb, f);
  fclose(f);
}

/* ---- keys */
static const struct {
  const char *name;
  int key;
} NAMES[] = {{"left", eadk_key_left}, {"up", eadk_key_up}, {"down", eadk_key_down}, {"right", eadk_key_right},
             {"ok", eadk_key_ok}, {"2nd", eadk_key_ok}, {"back", eadk_key_back}, {"clear", eadk_key_back},
             {"home", eadk_key_home}, {"mode", eadk_key_home}, {"shift", eadk_key_shift}, {"y=", eadk_key_shift},
             {"alpha", eadk_key_alpha}, {"var", eadk_key_var}, {"vars", eadk_key_var}, {"toolbox", eadk_key_toolbox},
             {"math", eadk_key_toolbox}, {"backspace", eadk_key_backspace}, {"del", eadk_key_backspace},
             {"exe", eadk_key_exe}, {"enter", eadk_key_exe}};
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
  if (q && now >= strtoull(q, 0, 10)) k |= (eadk_keyboard_state_t)1 << eadk_key_home;
  return k;
}
static void script_shots(uint64_t now) {
  static uint64_t done_until;
  const char *s = getenv("NP_SHOTS");
  while (s && *s) {
    unsigned long t = strtoul(s, (char **)&s, 10);
    if (*s++ != ':') break;
    size_t n = strcspn(s, ",");
    char path[256];
    snprintf(path, sizeof path, "%.*s", (int)n, s);
    if (t > done_until && t <= now) shot(path);
    s += n;
    if (*s == ',') s++;
  }
  done_until = now;
}

#ifdef HAVE_SDL
static SDL_Window *win;
static SDL_Renderer *ren;
static SDL_Texture *tex;
static void present(void) {
  static uint8_t rgb[SCR_W * SCR_H * 3];
  to_rgb(rgb);
  SDL_UpdateTexture(tex, 0, rgb, SCR_W * 3);
  SDL_RenderClear(ren);
  SDL_RenderTexture(ren, tex, 0, 0);
  SDL_RenderPresent(ren);
}
static const struct {
  SDL_Scancode sc;
  int key;
} KEYMAP[] = {{SDL_SCANCODE_LEFT, eadk_key_left}, {SDL_SCANCODE_RIGHT, eadk_key_right}, {SDL_SCANCODE_UP, eadk_key_up},
              {SDL_SCANCODE_DOWN, eadk_key_down}, {SDL_SCANCODE_Z, eadk_key_ok}, {SDL_SCANCODE_SPACE, eadk_key_ok},
              {SDL_SCANCODE_RETURN, eadk_key_exe}, {SDL_SCANCODE_BACKSPACE, eadk_key_back},
              {SDL_SCANCODE_ESCAPE, eadk_key_home}, {SDL_SCANCODE_D, eadk_key_backspace},
              {SDL_SCANCODE_S, eadk_key_shift}, {SDL_SCANCODE_A, eadk_key_alpha}, {SDL_SCANCODE_M, eadk_key_toolbox},
              {SDL_SCANCODE_V, eadk_key_var}};
#endif

eadk_keyboard_state_t eadk_keyboard_scan(void) {
  uint64_t now = eadk_timing_millis();
  frames++;
  script_shots(now);
  eadk_keyboard_state_t k = script_keys(now);
#ifdef HAVE_SDL
  if (!headless) {
    SDL_Event e;
    while (SDL_PollEvent(&e))
      if (e.type == SDL_EVENT_QUIT) k |= (eadk_keyboard_state_t)1 << eadk_key_home;
    const bool *ks = SDL_GetKeyboardState(0);
    for (size_t i = 0; i < sizeof KEYMAP / sizeof KEYMAP[0]; i++)
      if (ks[KEYMAP[i].sc]) k |= (eadk_keyboard_state_t)1 << KEYMAP[i].key;
    present();
  }
#endif
  return k;
}

/* ---- files */
static const char *save_path(void) { return getenv("NP_SAVE") ? getenv("NP_SAVE") : "bin/host/NPbalato.bin"; }
static uint8_t *store;
static size_t store_size;
uint8_t *ce_storage_open(const char *name, size_t size) {
  (void)name;
  store = calloc(1, size);
  store_size = size;
  FILE *f = fopen(save_path(), "rb");
  if (f) {
    if (fread(store, 1, size, f)) {}
    fclose(f);
  }
  return store;
}
void ce_storage_keep(void) {
  FILE *f = fopen(save_path(), "wb");
  if (!f) return;
  fwrite(store, 1, store_size, f);
  fclose(f);
}
void np_begin(void) {}
int np_end(void) { return 0; }

int main(void) {
  headless = getenv("NP_HEADLESS") && *getenv("NP_HEADLESS") == '1';
#ifdef HAVE_SDL
  if (!headless) {
    SDL_Init(SDL_INIT_VIDEO);
    int sc = getenv("NP_SCALE") ? atoi(getenv("NP_SCALE")) : 3;
    win = SDL_CreateWindow("Balatro (TI-84 Plus CE)", SCR_W * sc, SCR_H * sc, 0);
    ren = SDL_CreateRenderer(win, 0);
    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, SCR_W, SCR_H);
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
  }
#else
  headless = 1;
#endif
  t0 = real_ms();
  if (!ce_art_init() || !ce_bg_init()) {
    fprintf(stderr, "host: an appvar is missing (make first)\n");
    return 1;
  }
  ce_set_palette(0, CE_NSTATIC, ce_palette);
  balatro_main();
  fprintf(stderr, "host: %ld frames in %llu ms\n", frames, (unsigned long long)eadk_timing_millis());
  return 0;
}
