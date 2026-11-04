/* A computer standing in for the calculator (ce.h), for tests: the two screen buffers and the palette in memory,
 * keys and time from the test (host_keys, host_ticks), the appvars from files, the save in a file. host_shot
 * writes what the screen shows. With HOST_SDL, a window too (host_sdl.c). */
#include "ce.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint8_t *scr;
uint16_t ce_pal[256];
bool ce_pal_dirty;
char ce_missing[9];

static uint8_t buffers[2][SCR_W * SCR_H];
static int shown;                       /* the buffer the screen shows */
static uint16_t shown_pal[256];
uint32_t host_keys, host_ticks;
const char *host_data_dir = "data";     /* where FNAFD0.bin... are */
const char *host_save_dir = "build/host-saves";
size_t host_ram_size = 96 * 1024;       /* the free RAM a calculator would have */
long host_frames;                       /* frames shown */

bool ce_init(void) {
  memset(buffers, 0, sizeof buffers);
  shown = 0;
  scr = buffers[1];
  return true;
}

void ce_quit(void) { ce_save_done(); }

uint32_t ce_frames;

void ce_show(bool keep) {
  ce_frames += keep ? 3 : 1;
  if (ce_pal_dirty) memcpy(shown_pal, ce_pal, sizeof ce_pal), ce_pal_dirty = false;
  shown ^= 1;
  scr = buffers[shown ^ 1];
  if (keep) memcpy(scr, buffers[shown], sizeof buffers[0]);
  host_frames++;
}

void ce_wait(void) {}
uint32_t ce_keys(void) { return host_keys; }
uint32_t ce_ticks(void) { return host_ticks; }

bool ce_data(const char *prefix, int count, const uint8_t **vars) {
  for (int i = 0; i < count; i++) {
    char p[512];
    snprintf(p, sizeof p, "%s/%s%d.bin", host_data_dir, prefix, i);
    FILE *f = fopen(p, "rb");
    if (!f) {
      snprintf(ce_missing, sizeof ce_missing, "%s%d", prefix, i);
      return false;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc((size_t)n + 64);   /* (kept until the end, as the archive keeps them) */
    if (fread(b, 1, (size_t)n, f) != (size_t)n) n = 0;
    fclose(f);
    vars[i] = b;
  }
  return true;
}

static uint8_t *ram;
uint8_t *ce_ram(size_t *len) {
  if (!ram) ram = malloc(host_ram_size);
  *len = host_ram_size;
  return ram;
}

static uint8_t *save;
static size_t save_size;
static char save_name[64];
uint8_t *ce_save_open(const char *name, size_t size) {
  free(save);
  save = calloc(1, size);
  save_size = size;
  snprintf(save_name, sizeof save_name, "%s", name);
  char p[512];
  snprintf(p, sizeof p, "%s/%s.8xv.bin", host_save_dir, name);
  FILE *f = fopen(p, "rb");
  if (f) {
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if ((size_t)n == size && fread(save, 1, size, f) != size) memset(save, 0, size);
    fclose(f);
  }
  return save;
}

void ce_save_done(void) {
  if (!save) return;
  char p[512];
  snprintf(p, sizeof p, "%s/%s.8xv.bin", host_save_dir, save_name);
  FILE *f = fopen(p, "wb");
  if (!f) return;
  fwrite(save, 1, save_size, f);
  fclose(f);
}

/* the screen as shown, in RGB */
void host_rgb(uint8_t *out) {
  const uint8_t *b = buffers[shown];
  for (int i = 0; i < SCR_W * SCR_H; i++) {
    uint16_t c = shown_pal[b[i]];
    int r = c >> 10 & 31, g = (c >> 5 & 31) << 1 | c >> 15, bl = c & 31;
    out[3 * i] = (uint8_t)(r * 255 / 31), out[3 * i + 1] = (uint8_t)(g * 255 / 63), out[3 * i + 2] = (uint8_t)(bl * 255 / 31);
  }
}

void host_shot(const char *path) {
  static uint8_t rgb[SCR_W * SCR_H * 3];
  host_rgb(rgb);
  FILE *f = fopen(path, "wb");
  if (!f) return;
  fprintf(f, "P6\n%d %d\n255\n", SCR_W, SCR_H);
  fwrite(rgb, 1, sizeof rgb, f);
  fclose(f);
}

/* zx0 (the toolchain's zx0_Decompress on the calculator): Einar Saukas's format, version 2. Elias gamma
 * numbers with their bits interlaced; a new offset's high part has its bits inverted, its low 7 bits are a byte's
 * top 7, and that byte's lowest bit is the first bit of the length that follows. */
typedef struct {
  const uint8_t *s;
  int mask, value, back, last;
} Zx0;

static int zbyte(Zx0 *z) { return z->last = *z->s++; }

static int zbit(Zx0 *z) {
  if (z->back) {
    z->back = 0;
    return z->last & 1;
  }
  z->mask >>= 1;
  if (!z->mask) z->mask = 128, z->value = zbyte(z);
  return (z->value & z->mask) != 0;
}

static int zgamma(Zx0 *z, int inverted) {
  int v = 1;
  while (!zbit(z)) v = v << 1 | (zbit(z) ^ inverted);
  return v;
}

void zx0_Decompress(void *dst, const void *src) {
  Zx0 z = {src, 0, 0, 0, 0};
  uint8_t *o = dst;
  int offset = 1, n;
  for (;;) {
    for (n = zgamma(&z, 0); n--;) *o++ = (uint8_t)zbyte(&z);    /* literals */
    if (!zbit(&z)) {
      for (n = zgamma(&z, 0); n--; o++) *o = o[-offset];       /* the last offset again */
      if (!zbit(&z)) continue;
    }
    for (;;) {                                                  /* a new offset */
      n = zgamma(&z, 1);
      if (n == 256) return;
      offset = n * 128 - (zbyte(&z) >> 1);
      z.back = 1;
      for (n = zgamma(&z, 0) + 1; n--; o++) *o = o[-offset];
      if (!zbit(&z)) break;
    }
  }
}
