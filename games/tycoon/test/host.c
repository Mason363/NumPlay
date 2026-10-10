/* PC test host for NumTycoon: runs the real game code against a fake EADK.
 *   host SCRIPT OUTDIR
 * SCRIPT lines:  key MS HOLD NAME | shot MS NAME | cheat MS KIND VALUE | end MS
 * Keys go through the same eadk_keyboard_scan bits as the calculator. The
 * save lives in a RAM file system, so saving and loading are tested too. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "stub/eadk.h"
#include "font.h"

static uint16_t fb[320 * 240];
static uint64_t vnow;
static struct { int at, hold, bit; } keyev[1024];
static int nkeys;
static struct { int at; char name[64]; } shots[64];
static int nshots, endat = 5000;
static char outdir[256] = ".";
static struct { int at; char kind[16]; double v; bool done; } cheats[64];
static int ncheats;

static const struct { const char *n; int b; } KN[] = {
  {"left", 0}, {"up", 1}, {"down", 2}, {"right", 3}, {"ok", 4}, {"back", 5}, {"home", 6}, {"shift", 12},
  {"alpha", 13}, {"backspace", 17}, {"1", 42}, {"2", 43}, {"3", 44}, {"4", 36}, {"5", 37}, {"exe", 52}, {"0", 48}};

uint64_t eadk_keyboard_scan(void) {
  uint64_t k = 0;
  for (int i = 0; i < nkeys; i++)
    if ((int)vnow >= keyev[i].at && (int)vnow < keyev[i].at + keyev[i].hold) k |= 1ull << keyev[i].bit;
  return k;
}
uint64_t eadk_timing_millis(void) { return vnow; }
uint32_t eadk_random(void) { return (uint32_t)rand(); }

static void dump(const char *name) {
  char p[512];
  snprintf(p, sizeof p, "%s/%s.ppm", outdir, name);
  FILE *f = fopen(p, "wb");
  fprintf(f, "P6\n320 240\n255\n");
  for (int i = 0; i < 320 * 240; i++) {
    uint16_t c = fb[i];
    fputc((c >> 11) * 255 / 31, f), fputc((c >> 5 & 63) * 255 / 63, f), fputc((c & 31) * 255 / 31, f);
  }
  fclose(f);
}
static int shot_i;
void host_cheat(const char *kind, double v);
void host_finish(void);
static void bot_step(void);
void eadk_display_wait_for_vblank(void) {
  if (getenv("BOT")) bot_step();
  for (int i = 0; i < ncheats; i++)
    if (!cheats[i].done && (int)vnow >= cheats[i].at) cheats[i].done = true, host_cheat(cheats[i].kind, cheats[i].v);
  while (shot_i < nshots && (int)vnow >= shots[shot_i].at) dump(shots[shot_i++].name);
  if ((int)vnow >= endat) {
    host_finish();
    exit(0);
  }
}
void eadk_timing_msleep(uint32_t ms) { vnow += ms; }
void eadk_display_push_rect(eadk_rect_t r, const eadk_color_t *px) {
  for (int y = 0; y < r.height; y++)
    for (int x = 0; x < r.width; x++)
      if (r.x + x < 320 && r.y + y < 240) fb[(r.y + y) * 320 + r.x + x] = px[y * r.width + x];
}
void eadk_display_push_rect_uniform(eadk_rect_t r, eadk_color_t c) {
  for (int y = 0; y < r.height; y++)
    for (int x = 0; x < r.width; x++)
      if (r.x + x < 320 && r.y + y < 240) fb[(r.y + y) * 320 + r.x + x] = c;
}
static uint16_t blend(uint16_t bg, uint16_t fg, int a) {
  int br = bg >> 11, bgc = bg >> 5 & 63, bb = bg & 31, fr = fg >> 11, fgc = fg >> 5 & 63, fbb = fg & 31;
  return (uint16_t)(((br + (fr - br) * a / 255) << 11) | ((bgc + (fgc - bgc) * a / 255) << 5) | (bb + (fbb - bb) * a / 255));
}
void eadk_display_draw_string(const char *s, eadk_point_t p, bool large, eadk_color_t fg, eadk_color_t bg) {
  int w = large ? 10 : 7, h = large ? 18 : 12;
  const unsigned char *f = large ? font_large : font_small;
  for (int n = 0; s[n]; n++) {
    int c = (unsigned char)s[n];
    if (c < 32 || c > 126) c = '?';
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++) {
        int X = p.x + n * w + x, Y = p.y + y;
        if (X < 320 && Y < 240) fb[Y * 320 + X] = blend(bg, fg, f[((c - 32) * h + y) * w + x]);
      }
  }
}

/* a RAM file system in Epsilon's format, for the game's saves */
static uint8_t fsmem[4 + 8192 + 4 + 512];
void host_fs_io(bool write);
uint8_t *host_storage(uint32_t *size) {
  static bool init;
  if (!init) {
    init = true;
    uint32_t m = 0xEE0BDDBAu;
    memcpy(fsmem, &m, 4), memcpy(fsmem + 4 + 8192, &m, 4);
  }
  *size = 8192;
  return fsmem + 4;
}
void host_fs_io(bool write) {
  const char *p = getenv("FSFILE");
  if (!p) return;
  FILE *f = fopen(p, write ? "wb" : "rb");
  if (!f) return;
  if (write) fwrite(fsmem, 1, sizeof fsmem, f);
  else if (fread(fsmem, 1, sizeof fsmem, f) != sizeof fsmem) {}
  fclose(f);
}
#define main game_main
#include "tycoon_patched.c"
#undef main

/* a greedy player, to check the pace of the game: BOT=1 */
static void bot_step(void) {
  static uint64_t next;
  static double marks[] = {1e3, 1e6, 1e8, 1e9, 1e12, 1e15};
  static bool hit[6];
  if (V.franchises) memset(hit, 0, 0);
  for (int m = 0; m < 6; m++)
    if (!hit[m] && V.life >= marks[m]) hit[m] = true, printf("  t=%5llus  lifetime earnings %.0e  (inc %.3g/s, managers %d)\n", (unsigned long long)(vnow / 1000), marks[m], inc, __builtin_popcount(V.mgr));
  if (scr != S_GAME) return;
  if (vnow < next) return;
  next = vnow + 250;
  if (tr_on) catch_truck();
  { static int fr; uint32_t g = stars_gain(); if (g >= 5 + V.stars_total / 2) { franchise(); printf("  t=%5llus  FRANCHISE #%u: +%u stars (total %u)\n", (unsigned long long)(vnow / 1000), ++fr, g, V.stars_total); }
    for (int k = 0; k < NS; k++) { while (V.su[k] < SUP[k].max && V.stars >= SUP[k].cost[V.su[k]]) buy_star(k); } }
  for (int i = 0; i < NB; i++) run_biz(i);
  for (int pass = 0; pass < 6; pass++) {
    int best = -1;
    double bs = 0;
    for (int i = 0; i < NB; i++)
      if (unlocked(i) && V.money >= unit_cost(i)) {
        double sc = BIZ[i].rev * mult_biz(i) / (BIZ[i].time * unit_cost(i));
        if (V.owned[i] == 0) sc *= 1e6;
        if (sc > bs) bs = sc, best = i;
      }
    int mi = -1;
    for (int i = 0; i < NB; i++)
      if (V.owned[i] && !(V.mgr >> i & 1) && V.money >= BIZ[i].mgr && (mi < 0 || BIZ[i].mgr < BIZ[mi].mgr)) mi = i;
    if (mi >= 0) { hire(mi); continue; }
    build_ulist();
    if (ucount && V.money >= up_cost(ulist[0])) { buy_up(ulist[0]); continue; }
    if (best >= 0) { V.qty = 0; buy_biz(best); continue; }
    break;
  }
}
void host_cheat(const char *kind, double v) {
  if (!strcmp(kind, "money")) V.money += v, V.life += v, V.run += v;
  else if (!strcmp(kind, "owned_all")) for (int i = 0; i < NB; i++) V.owned[i] = (uint32_t)v;
  else if (!strcmp(kind, "mgr_all")) V.mgr = 0x3FF;
  else if (!strcmp(kind, "life")) V.life += v, V.run += v;
  else if (!strcmp(kind, "truck")) tr_on = true, tr_x = 100, tr_v = 30;
  else if (!strcmp(kind, "phase")) V.phase = (uint32_t)v;
  else if (!strcmp(kind, "rain")) rain_ms = 20000;
  else if (!strcmp(kind, "showcase")) {
    static const uint32_t own[NB] = {88, 64, 52, 41, 30, 26, 14, 9, 3, 1};
    for (int i = 0; i < NB; i++) V.owned[i] = own[i];
    V.mgr = 0x1F, V.bu[0] = 3, V.bu[1] = 1, V.gu = 3, V.money = v, V.life = v * 40, V.run = v * 40, V.stars = 12, V.stars_total = 12;
    V.ach = 0x3F, V.qty = 0, V.tab = 0;
  }
  else if (!strcmp(kind, "stars")) V.stars += (uint32_t)v, V.stars_total += (uint32_t)v;
}
void host_finish(void) {
  uint32_t n = 0;
  const uint8_t *d = ef_read(SAVE_NAME, &n);
  host_fs_io(true);
  printf("save: %u bytes (%s), money %.3g life %.3g stars %u/%u ach %d franchises %u\n", n, d ? "found" : "none", V.money, V.life,
         V.stars, V.stars_total, ach_count(), V.franchises);
}
int main(int argc, char **argv) {
  if (argc > 2) strncpy(outdir, argv[2], 255);
  host_storage(&(uint32_t){0});
  host_fs_io(false);
  FILE *f = fopen(argv[1], "r");
  char kind[16], name[64];
  int a, b;
  double v;
  while (f && fscanf(f, "%15s", kind) == 1) {
    if (!strcmp(kind, "key") && fscanf(f, "%d %d %63s", &a, &b, name) == 3) {
      for (unsigned i = 0; i < sizeof KN / sizeof KN[0]; i++)
        if (!strcmp(KN[i].n, name)) keyev[nkeys++] = (typeof(keyev[0])){a, b, KN[i].b};
    } else if (!strcmp(kind, "shot") && fscanf(f, "%d %63s", &a, name) == 2) {
      shots[nshots].at = a, strcpy(shots[nshots++].name, name);
    } else if (!strcmp(kind, "cheat") && fscanf(f, "%d %63s %lf", &a, name, &v) == 3) {
      cheats[ncheats].at = a, strcpy(cheats[ncheats].kind, name), cheats[ncheats++].v = v;
    } else if (!strcmp(kind, "end") && fscanf(f, "%d", &a) == 1) {
      endat = a;
    }
  }
  int rc = game_main();
  host_finish();
  return rc;
}
