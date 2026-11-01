/* Retro-Go's emulators on a computer: runs a ROM for some frames, with random keys if asked,
 * and writes the last frame as a PPM picture.
 *   host_core ROM FRAMES OUT.ppm [SEED] [SCRIPT]   (SEED: random pad presses; 0 for none)
 * The system comes from the file's extension (.gb .gbc .nes .sms .gg .sg). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "../src/rg.h"
#include "../src/gb/gb.h"
#include "../src/nes/nes_core.h"
#include "../src/sms/sms.h"

static uint8_t heap[RG_HEAP_SIZE] __attribute__((aligned(8)));
static uint32_t used;
bool rg_drawing = true;
static uint16_t frame[256 * 240];
static int lines_seen;
/* the three emulators behind the same few calls */
static enum { GB, NES, SMS } kind;

void *rg_alloc(uint32_t size) {
  size = (size + 7) & ~7u;
  if (used + size > sizeof heap) return NULL;
  void *p = heap + used;
  used += size;
  memset(p, 0, size);
  return p;
}
void rg_alloc_reset(void) { used = 0; }

void rg_line(int y, const uint16_t *px) {
  if (y < 0 || y >= 240) {
    fprintf(stderr, "line %d out of the picture\n", y);
    abort();
  }
  memcpy(frame + y * 256, px, (size_t)(kind == GB ? 160 : 256) * 2);
  lines_seen++;
}

/* the ROM, read only as the calculator's flash is: a write into it stops the test (and a read
   past its end too: the page after it isn't there) */
static uint8_t *load(const char *path, uint32_t *size) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  size_t pages = ((size_t)n + 4095) / 4096 * 4096 + 4096;
  uint8_t *d = mmap(NULL, pages, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
  if (d == MAP_FAILED || fread(d, 1, (size_t)n, f) != (size_t)n) return NULL;
  fclose(f);
  mprotect(d, pages - 4096, PROT_READ);
  mprotect(d + pages - 4096, 4096, PROT_NONE);
  *size = (uint32_t)n;
  return d;
}

static uint32_t crc32(const uint8_t *p, uint32_t n) {
  uint32_t c = 0xFFFFFFFF;
  for (uint32_t i = 0; i < n; i++) {
    c ^= p[i];
    for (int k = 0; k < 8; k++) c = c >> 1 ^ (0xEDB88320 & -(c & 1));
  }
  return ~c;
}

static void set_pad(int p) { kind == NES ? nes_set_pad(p) : kind == SMS ? sms_set_pad(p) : gb_set_pad(p); }
static void run_frame(void) { kind == NES ? nes_run_frame(true) : kind == SMS ? sms_run_frame(true) : gb_run_frame(true); }
static int chunks(rg_chunk_t *c) { return kind == NES ? nes_state_chunks(c) : kind == SMS ? sms_state_chunks(c) : gb_state_chunks(c); }
static void loaded(void) { kind == NES ? nes_state_loaded() : kind == SMS ? sms_state_loaded() : gb_state_loaded(); }
static void redraw(void) { kind == NES ? nes_redraw() : kind == SMS ? sms_redraw() : gb_redraw(); }
static void view(int *x, int *y, int *w, int *h) {
  if (kind == SMS) sms_view(x, y, w, h);
  else *x = 0, *y = 0, *w = kind == NES ? 256 : 160, *h = kind == NES ? 240 : 144;
}

int main(int argc, char **argv) {
  if (argc < 4) return fprintf(stderr, "usage: host_core ROM FRAMES OUT.ppm [SEED] [SCRIPT]\n"), 2;
  uint32_t size;
  uint8_t *rom = load(argv[1], &size);
  if (!rom) return fprintf(stderr, "can't read %s\n", argv[1]), 1;
  int frames = atoi(argv[2]);
  unsigned seed = argc > 4 ? (unsigned)atoi(argv[4]) : 0;
  const char *ext = strrchr(argv[1], '.');
  kind = ext && !strcmp(ext, ".nes") ? NES : ext && (!strcmp(ext, ".sms") || !strcmp(ext, ".gg") || !strcmp(ext, ".sg")) ? SMS : GB;
  int r = kind == NES ? nes_load(rom, size)
          : kind == SMS ? sms_load(rom, size, crc32(rom, size), !strcmp(ext, ".gg") ? SMS_GG : !strcmp(ext, ".sg") ? SMS_SG : SMS_SMS)
                        : gb_load(rom, size);
  if (r != RG_OK) return fprintf(stderr, "load failed: %d\n", r), 1;
  int pad = 0;
  const char *script = argc > 5 ? argv[5] : ""; /* "FRAME=PAD,FRAME=PAD...": the pad from then on */
  for (int i = 0; i < frames; i++) {
    if (seed) {
      seed = seed * 1103515245u + 12345u;
      if ((seed >> 16) % 8 == 0) pad = (int)((seed >> 8) & 0xFF);
    }
    while (*script && atoi(script) == i) {
      const char *eq = strchr(script, '=');
      if (!eq) break;
      pad = atoi(eq + 1);
      const char *comma = strchr(eq, ',');
      script = comma ? comma + 1 : "";
    }
    set_pad(pad), run_frame();
  }
  int vx, vy, vw, vh;
  view(&vx, &vy, &vw, &vh);
  /* STATE=1: a save state taken now, 120 more frames, the state put back, the same 120 frames:
     the same picture at the end (the state holds the whole machine); then states of random
     bytes, which must not crash it. REDRAW=1: the last frame drawn again is the same picture. */
  if (getenv("STATE")) {
    rg_chunk_t parts[RG_MAX_CHUNKS];
    int n = chunks(parts);
    static uint8_t copy[RG_HEAP_SIZE + 65536];
    uint32_t at = 0;
    for (int i = 0; i < n; i++) memcpy(copy + at, parts[i].at, parts[i].size), at += parts[i].size;
    static uint16_t first[256 * 240];
    for (int pass = 0; pass < 2; pass++) {
      if (pass) {
        n = chunks(parts);
        at = 0;
        for (int i = 0; i < n; i++) memcpy(parts[i].at, copy + at, parts[i].size), at += parts[i].size;
        loaded();
      }
      for (int i = 0; i < 120; i++) {
        int p = (i / 10) % 3 ? 0 : RG_PAD_RIGHT | (i & 16 ? RG_PAD_A : 0);
        set_pad(p), run_frame();
      }
      if (!pass) memcpy(first, frame, sizeof first);
    }
    printf("state %s\n", memcmp(first, frame, sizeof first) ? "differs" : "same");
    for (int k = 0; k < 30; k++) {
      n = chunks(parts);
      for (int i = 0; i < n; i++)
        for (uint32_t j = 0; j < parts[i].size; j++)
          if (k < 10 || (j * 7 + (unsigned)k) % 13 == 0) {
            seed = seed * 1103515245u + 12345u + (unsigned)k;
            ((uint8_t *)parts[i].at)[j] = (uint8_t)(seed >> 16);
          }
      loaded();
      for (int i = 0; i < 30; i++) set_pad(i & 0xFF), run_frame();
    }
    printf("random states: no crash\n");
    view(&vx, &vy, &vw, &vh);
  }
  if (getenv("REDRAW")) {
    static uint16_t last[256 * 240];
    memcpy(last, frame, sizeof last);
    redraw();
    int differ = 0;
    for (int y = vy; y < vy + vh; y++)
      for (int x = vx; x < vx + vw; x++) differ += last[y * 256 + x] != frame[y * 256 + x];
    printf("redraw: %d pixels differ\n", differ);
  }
  FILE *o = fopen(argv[3], "wb");
  fprintf(o, "P6\n%d %d\n255\n", vw, vh);
  for (int y = vy; y < vy + vh; y++)
    for (int x = vx; x < vx + vw; x++) {
      uint16_t c = frame[y * 256 + x];
      fputc((c >> 11) << 3 | (c >> 13), o);
      fputc(((c >> 5) & 63) << 2 | ((c >> 9) & 3), o);
      fputc((c & 31) << 3 | ((c >> 2) & 7), o);
    }
  fclose(o);
  printf("%d frames, %d lines, %u bytes of RAM\n", frames, lines_seen, used);
  return 0;
}
