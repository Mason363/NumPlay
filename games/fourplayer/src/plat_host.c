/* A computer: the screen goes to a frame buffer that tests save as images, keys come from a script, time is
 * simulated, files are in a folder. */
#include <stdio.h>
#include <stdlib.h>
#include "plat.h"

#define W 320
#define H 240
uint16_t host_fb[W * H];
uint32_t host_time; /* ms */
uint64_t host_keys;
uint32_t host_seed = 1;

uint64_t plat_keys(void) { return host_keys; }
uint32_t plat_millis(void) { return host_time; }
void plat_sleep(uint32_t ms) { host_time += ms; }
void plat_vblank(void) {}
uint32_t plat_random(void) {
  host_seed ^= host_seed << 13, host_seed ^= host_seed >> 17, host_seed ^= host_seed << 5;
  return host_seed;
}
void plat_push(int x, int y, int w, int h, const uint16_t *px) {
  for (int r = 0; r < h; r++)
    for (int c = 0; c < w; c++)
      if ((unsigned)(x + c) < W && (unsigned)(y + r) < H) host_fb[(y + r) * W + x + c] = px[r * w + c];
}

static char save_dir[256] = "build/host-saves";
void host_save_dir(const char *d) { snprintf(save_dir, sizeof save_dir, "%s", d); }
bool plat_save(const char *name, const void *data, uint32_t len) {
  char p[512];
  snprintf(p, sizeof p, "%s/%s", save_dir, name);
  FILE *f = fopen(p, "wb");
  if (!f) return false;
  bool ok = fwrite(data, 1, len, f) == len;
  fclose(f);
  return ok;
}
/* (each file read gets its own buffer: callers may hold two at once) */
static uint8_t loaded[4][1 << 14];
static int next_load;
const uint8_t *plat_load(const char *name, uint32_t *len) {
  char p[512];
  snprintf(p, sizeof p, "%s/%s", save_dir, name);
  FILE *f = fopen(p, "rb");
  if (!f) return NULL;
  uint8_t *b = loaded[next_load++ & 3];
  *len = (uint32_t)fread(b, 1, sizeof loaded[0], f);
  fclose(f);
  return b;
}
void plat_begin(void) {}
int plat_end(void) { return 0; }

/* writes the screen as a PPM image */
void host_shot(const char *path) {
  FILE *f = fopen(path, "wb");
  if (!f) return;
  fprintf(f, "P6\n%d %d\n255\n", W, H);
  for (int i = 0; i < W * H; i++) {
    uint16_t c = host_fb[i];
    uint8_t rgb[3] = {(uint8_t)((c >> 11) * 255 / 31), (uint8_t)(((c >> 5) & 63) * 255 / 63), (uint8_t)((c & 31) * 255 / 31)};
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
}
/* the screen as raw RGB565 (tools/record.py --frames) */
void host_raw(const char *path) {
  FILE *f = fopen(path, "wb");
  if (!f) return;
  fwrite(host_fb, 2, W * H, f);
  fclose(f);
}
