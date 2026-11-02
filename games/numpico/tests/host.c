/* NumPico on a computer, for the tests: the app with its carts from a table file (tools/p8cart.py --bin),
 * the calculator's keys from a seeded random generator, its clock virtual (it moves only when the app
 * waits), its screen a buffer that can be saved as pictures, its files in memory.
 *
 * usage: host TABLE [--frames N] [--seed S] [--heap BYTES] [--keys none|random|menu] [--cart I]
 *             [--shots DIR] [--every N]
 * Plays N frames of the cart (the first, or --cart I from the list), then presses Home. Prints how each
 * run of a cart ended: "end <how> <what>: <message>", and Lua's memory at its most. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/pico.h"
#include "../src/plat.h"

const uint8_t *np_carts;
uint32_t np_carts_size;
int numpico_main(void);

static uint16_t screen[320 * 240];
static uint32_t now_ms, rng = 1, frames, limit = 600, every = 0, presses;
static int keys_mode = 1, cart_pick = -1, picked;
static const char *shots;
static uint8_t *heap;
static size_t heap_bytes = 1 << 20;
static uint32_t held_keys, hold_left;

static uint32_t rand32(void) {
  rng ^= rng << 13, rng ^= rng >> 17, rng ^= rng << 5;
  return rng;
}

static void shot(void) {
  char path[512];
  snprintf(path, sizeof path, "%s/frame_%05u.ppm", shots, frames);
  FILE *f = fopen(path, "wb");
  if (!f) return;
  fprintf(f, "P6 320 240 255\n");
  for (int i = 0; i < 320 * 240; i++) {
    uint16_t c = screen[i];
    uint8_t rgb[3] = {(uint8_t)((c >> 11) * 255 / 31), (uint8_t)(((c >> 5) & 63) * 255 / 63), (uint8_t)((c & 31) * 255 / 31)};
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
}

void plat_push(int x, int y, int w, int h, const uint16_t *px) {
  if (x < 0 || y < 0 || w < 0 || h < 0 || x + w > 320 || y + h > 240) {
    fprintf(stderr, "host: rect out of the screen %d,%d %dx%d\n", x, y, w, h);
    abort();
  }
  for (int j = 0; j < h; j++) memcpy(screen + (y + j) * 320 + x, px + j * w, w * 2);
  /* (a frame of the cart: its last row) */
  if (x == PIC_X && y + h == 240) {
    frames++;
    if (shots && every && frames % every == 0) shot();
  }
}

uint32_t plat_keys(void) {
  if (frames >= limit || now_ms > 1200000) return PK_HOME; /* (or after 20 minutes on the clock: a cart stuck) */
  /* the list: the cart to play */
  if (cart_pick >= 0 && !picked) {
    static int step;
    step++;
    if (step % 2) return 0;
    static int moved;
    if (moved < cart_pick) return moved++, PK_DOWN;
    picked = 1;
    return PK_OK | PK_O;
  }
  if (keys_mode == 0) return 0;
  if (!hold_left) {
    static const uint32_t pads[] = {PK_LEFT, PK_RIGHT, PK_UP, PK_DOWN, PK_O, PK_X, PK_O | PK_RIGHT, PK_X | PK_LEFT, 0, 0};
    uint32_t r = rand32();
    held_keys = pads[r % 10];
    if (keys_mode == 2 && r % 97 == 0) held_keys = PK_MENU, presses++;
    hold_left = 1 + (r >> 8) % 12;
  }
  hold_left--;
  return held_keys | (held_keys & PK_O ? PK_OK : 0) | (held_keys & PK_X ? PK_BACK : 0);
}

void np_push(int x, int y, int w, int h, const uint16_t *px) { plat_push(x, y, w, h, px); }
void *np_alloc(uint32_t size) { (void)size; return NULL; }
uint32_t plat_millis(void) { return now_ms; }
void plat_sleep(uint32_t ms) { now_ms += ms ? ms : 1; }

static struct {
  char name[64];
  uint8_t data[512];
  uint32_t len;
} files[16];
bool plat_save(const char *name, const void *data, uint32_t len) {
  for (int i = 0; i < 16; i++)
    if (!files[i].name[0] || !strcmp(files[i].name, name)) {
      if (len > sizeof files[i].data) return false;
      snprintf(files[i].name, sizeof files[i].name, "%s", name);
      memcpy(files[i].data, data, len);
      files[i].len = len;
      return true;
    }
  return false;
}
const uint8_t *plat_load(const char *name, uint32_t *len) {
  for (int i = 0; i < 16; i++)
    if (files[i].name[0] && !strcmp(files[i].name, name)) return *len = files[i].len, files[i].data;
  return NULL;
}
uint32_t plat_random(void) { return 12345; }
void plat_heap(uint8_t **start, uint8_t **end) { *start = heap, *end = heap + heap_bytes; }
void plat_begin(void) {}
int plat_end(void) { return 0; }

void host_ended(int how) {
  static const char *const names[] = {"quit", "home", "error", "memory", "load"};
  printf("end %s", names[how]);
  if (how == PICO_ERROR) printf(" %s line %d: %s", pico_error.what, pico_error.line, pico_error.msg);
  printf("\npeak %u bytes, %u frames\n", pico_peak, frames);
  fflush(stdout);
  if (how != PICO_HOME) frames = limit; /* (then Home, from the screen that says so) */
}

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: host TABLE [--frames N] [--seed S] [--heap BYTES] [--keys none|random|menu] [--cart I] [--shots DIR] [--every N]\n");
    return 2;
  }
  for (int i = 2; i + 1 < argc; i += 2) {
    if (!strcmp(argv[i], "--frames")) limit = (uint32_t)atoi(argv[i + 1]);
    else if (!strcmp(argv[i], "--seed")) rng = (uint32_t)atoi(argv[i + 1]) * 2654435761u + 1;
    else if (!strcmp(argv[i], "--heap")) heap_bytes = (size_t)atol(argv[i + 1]);
    else if (!strcmp(argv[i], "--keys")) keys_mode = !strcmp(argv[i + 1], "none") ? 0 : !strcmp(argv[i + 1], "menu") ? 2 : 1;
    else if (!strcmp(argv[i], "--cart")) cart_pick = atoi(argv[i + 1]);
    else if (!strcmp(argv[i], "--shots")) shots = argv[i + 1];
    else if (!strcmp(argv[i], "--every")) every = (uint32_t)atoi(argv[i + 1]);
  }
  FILE *f = fopen(argv[1], "rb");
  if (!f) return perror(argv[1]), 2;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t *t = malloc(n > 0 ? n : 1);
  if (fread(t, 1, n, f) != (size_t)n) return 2;
  fclose(f);
  np_carts = t, np_carts_size = (uint32_t)n;
  heap = malloc(heap_bytes);
  int r = numpico_main();
  if (shots) shot();
  free(heap);
  free(t);
  printf("done %u frames, %u menu presses\n", frames, presses);
  return r;
}
