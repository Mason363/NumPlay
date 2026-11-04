/* NumBlocks CE on a computer, for tests and pictures: the screen is a buffer saved as pictures, the keys come from
 * a script, time is simulated (a frame takes NB_MSPF ms).
 *
 *   NB_FRAMES=200 NB_KEYS="10-40:fwd,50:attack" NB_SHOTS="30,60" NB_OUT=dir NB_SAVES=dir build/nbce
 *
 * Keys: left right up down fwd back sleft sright jump sneak sprint attack use inv pause home drop pick enter 1..9.
 * NB_SDL=1 (built with SDL3: make host-sdl) opens a window to play in instead. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "../src/nb.h"

uint8_t *scr;
static uint8_t screen_mem[SCREEN_W * SCREEN_H];
static uint8_t spare[76800];
static uint8_t pal[256 * 3];
static int frame_no, frames = 100, mspf = 125;
static const char *out = "build/shots", *saves = "build/saves";
typedef struct { int a, b; uint32_t k; } Hold;
static Hold holds[256];
static int nholds;
static int shots[64], nshots;
uint32_t host_ms;

static uint32_t key_bit(const char *s) {
  static const struct { const char *n; uint32_t k; } t[] = {
    {"left", K_LEFT}, {"right", K_RIGHT}, {"up", K_UP}, {"down", K_DOWN}, {"fwd", K_FWD}, {"back", K_BACK},
    {"sleft", K_SLEFT}, {"sright", K_SRIGHT}, {"jump", K_JUMP}, {"sneak", K_SNEAK}, {"sprint", K_SPRINT},
    {"attack", K_ATTACK}, {"use", K_USE}, {"inv", K_INV}, {"pause", K_PAUSE}, {"home", K_HOME}, {"drop", K_DROP},
    {"pick", K_PICK}, {"enter", K_ENTER}, {"ok", K_OK}, {"esc", K_ESC}};
  for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++)
    if (!strcmp(s, t[i].n)) return t[i].k;
  if (s[0] >= '1' && s[0] <= '9' && !s[1]) return K_SLOT(s[0] - 0x31);
  fprintf(stderr, "unknown key %s\n", s);
  return 0;
}

static uint8_t gfx_mem[70000], tex_mem[70000];
static void load_bin(const char *name, uint8_t *mem, unsigned size) {
  char p[1024];
  snprintf(p, sizeof p, "%s/%s", GFX_DIR, name);
  FILE *g = fopen(p, "rb");
  if (!g || fread(mem, 1, 70000, g) != size) {
    fprintf(stderr, "no %s\n", p);
    exit(1);
  }
  fclose(g);
}
void plat_init(void) {
  scr = screen_mem;
  load_bin("NBCEDAT.bin", gfx_mem, GFX_SIZE);
  load_bin("NBCETEX.bin", tex_mem, TEXDAT_SIZE);
  gfx = gfx_mem, texdat = tex_mem;
  memcpy(pal, pal_rgb, sizeof pal);
  const char *e;
  if ((e = getenv("NB_FRAMES"))) frames = atoi(e);
  if ((e = getenv("NB_MSPF"))) mspf = atoi(e);
  if ((e = getenv("NB_OUT"))) out = e;
  if ((e = getenv("NB_SAVES"))) saves = e;
  if ((e = getenv("NB_SHOTS"))) {
    char *s = strdup(e);
    for (char *t = strtok(s, ","); t && nshots < 64; t = strtok(NULL, ",")) shots[nshots++] = atoi(t);
  }
  if ((e = getenv("NB_KEYS"))) {
    char *s = strdup(e);
    for (char *t = strtok(s, ","); t && nholds < 256; t = strtok(NULL, ",")) {
      int a, b;
      char k[16];
      if (sscanf(t, "%d-%d:%15s", &a, &b, k) == 3) holds[nholds++] = (Hold){a, b, key_bit(k)};
      else if (sscanf(t, "%d:%15s", &a, k) == 2) holds[nholds++] = (Hold){a, a, key_bit(k)};
    }
  }
  mkdir(out, 0755);
  mkdir(saves, 0755);
}

static void shot(const char *path) {
  FILE *f = fopen(path, "wb");
  if (!f) return;
  fprintf(f, "P6\n%d %d\n255\n", SCREEN_W, SCREEN_H);
  for (int i = 0; i < SCREEN_W * SCREEN_H; i++) fwrite(&pal[scr[i] * 3], 1, 3, f);
  fclose(f);
}

void plat_exit(void) {
  fprintf(stderr, "%d frames\n", frame_no);
  exit(0);
}

void plat_palette(int first, int n, const uint8_t *rgb) { memcpy(pal + first * 3, rgb, (size_t)n * 3); }

void plat_poll(void) {}
uint32_t plat_keys(void) {
  uint32_t k = 0;
  for (int i = 0; i < nholds; i++)
    if (frame_no >= holds[i].a && frame_no <= holds[i].b) k |= holds[i].k;
  return k;
}

uint32_t plat_ms(void) { return host_ms; }

void plat_frame(void) {
  for (int i = 0; i < nshots; i++)
    if (shots[i] == frame_no) {
      char p[512];
      snprintf(p, sizeof p, "%s/shot_%04d.ppm", out, frame_no);
      shot(p);
      extern unsigned long rs_visited, rs_drawn, rs_rows, rs_edges, rs_polys;
      fprintf(stderr, "polys %lu edges %lu rows %lu visited %lu drawn %lu\n", rs_polys, rs_edges, rs_rows, rs_visited, rs_drawn);
      rs_visited = rs_drawn = rs_rows = rs_edges = rs_polys = 0;
      extern unsigned long st_cols, st_list, st_frus, st_cov, st_culled;
      extern unsigned long st_rl, st_ru, st_h[8], st_c2, st_c3;
      fprintf(stderr, "cut to the screen %lu\n", st_c2);
      st_c2 = st_c3 = 0;
      fprintf(stderr, "rows locked %lu unlocked %lu; polys by rows %lu %lu %lu %lu %lu %lu %lu+\n", st_rl, st_ru, st_h[1], st_h[2], st_h[3], st_h[4], st_h[5], st_h[6], st_h[7]);
      st_rl = st_ru = 0; memset(st_h, 0, sizeof st_h);
      fprintf(stderr, "cols %lu listed %lu in view %lu tested %lu culled %lu\n", st_cols, st_list, st_frus, st_cov, st_culled);
      fprintf(stderr, "frame %d: %u cells, %u faces, %u spans, %u pixels; at %.2f %.2f %.2f yaw %d pitch %d, win %d %d %d\n",
              frame_no, stat_cells, stat_faces, stat_spans, stat_px, pl.x / 4096.0, pl.y / 4096.0, pl.z / 4096.0, pl.yaw,
              pl.pitch, win_x, win_y, win_z);
    }
  frame_no++;
  host_ms += (uint32_t)mspf;
  if (frame_no >= frames) plat_exit();
}

uint8_t *plat_spare(void) { return spare; }
void *plat_grow(void *p, unsigned bytes) { return realloc(p, bytes ? bytes : 1); }

static uint8_t loaded[70000];
const uint8_t *plat_load(const char *name, unsigned *len) {
  char p[512];
  snprintf(p, sizeof p, "%s/%s", saves, name);
  FILE *f = fopen(p, "rb");
  if (!f) return NULL;
  *len = (unsigned)fread(loaded, 1, sizeof loaded, f);
  fclose(f);
  return loaded;
}
bool plat_save(const char *name, const void *a, unsigned na, const void *b, unsigned nb) {
  if (na + nb > 65505) return false;   /* (an appvar's most) */
  char p[512];
  snprintf(p, sizeof p, "%s/%s", saves, name);
  FILE *f = fopen(p, "wb");
  if (!f) return false;
  fwrite(a, 1, na, f);
  if (nb) fwrite(b, 1, nb, f);
  fclose(f);
  return true;
}
void plat_delete(const char *name) {
  char p[512];
  snprintf(p, sizeof p, "%s/%s", saves, name);
  remove(p);
}

bool host_start(uint32_t *seed, int *mode, int *at) {
  const char *e = getenv("NB_NEW");
  if (!e) return false;
  at[0] = -9999;
  int n = sscanf(e, "%u,%d,%d,%d,%d,%d,%d,%d", seed, mode, &at[0], &at[1], &at[2], &at[3], &at[4], &at[5]);
  if (n < 8) at[0] = -9999;
  return n >= 2;
}

void plat_expand(const uint8_t *src, uint8_t *dst, int rows) {
  for (int y = 0; y < rows; y++) {
    uint8_t *d = dst + y * 2 * SCREEN_W;
    for (int x = 0; x < RW; x++) d[2 * x] = d[2 * x + 1] = src[y * RW + x];
    memcpy(d + SCREEN_W, d, SCREEN_W);
  }
}
