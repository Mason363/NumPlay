/* Plays Deltarune on a computer, with scripted keys, and saves frames.
 *
 *   play [--frames N] [--keys "10-40:right,50:z,..."] [--shots 30,60|all|every:N] [--out DIR] [--saves DIR]
 *        [--room NAME_NUMBER --at X,Y] [--plot P] [--party 1,2,3] [--flag F=V]... [--battle N] [--fuzz SEED]
 *        [--mash] [--trace]
 *
 * Keys: left right up down z x c home. "a-b:key" holds the key from frame a to b, "a:key" for one frame.
 * --room starts a new game in room N (its number in data.h) with Kris's top-left at (X, Y), past the title.
 * --battle starts encounter N (scr_encountersetup's) at once. --mash presses Z every 4 frames when no
 * scripted key is held (through talks). --fuzz presses random keys (seeded) the whole time.
 * Screenshots are DIR/shot_<frame>.ppm. At the end it prints the state: mode, room, Kris, plot, HP. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "../src/battle.h"
#include "../src/blk.h"
#include "../src/event.h"
#include "../src/game.h"
#include "../src/gfx.h"
#include "../src/res.h"
#include "../src/text.h"
#include "../src/world.h"

extern uint32_t host_keys, host_time;
void host_shot(const char *path);
void host_save_dir(const char *d);
extern const uint8_t dr_data[];

typedef struct { int a, b; uint32_t k; } Hold;
static Hold holds[4096];
static int nholds;

static uint32_t key_of(const char *s) {
  if (!strncmp(s, "left", 4)) return K_LEFT;
  if (!strncmp(s, "right", 5)) return K_RIGHT;
  if (!strncmp(s, "up", 2)) return K_UP;
  if (!strncmp(s, "down", 4)) return K_DOWN;
  if (!strncmp(s, "z", 1)) return K_Z;
  if (!strncmp(s, "x", 1)) return K_X;
  if (!strncmp(s, "c", 1)) return K_C;
  if (!strncmp(s, "home", 4)) return K_HOME;
  return 0;
}

static void parse_keys(const char *s) {
  while (*s) {
    int a = atoi(s), b = a;
    while (*s >= '0' && *s <= '9') s++;
    if (*s == '-') {
      s++;
      b = atoi(s);
      while (*s >= '0' && *s <= '9') s++;
    }
    if (*s == ':') s++;
    if (nholds < 4096) holds[nholds++] = (Hold){a, b, key_of(s)};
    while (*s && *s != ',') s++;
    if (*s == ',') s++;
  }
}

int main(int argc, char **argv) {
  int frames = 300, room = -1, ax = 0, ay = 0, plotv = -1, battle = -1, every = 0;
  unsigned fuzz = 0;
  bool all = false, mash = false, trace = false;
  const char *out = "build/shots", *shots = "";
  int party[3] = {0, 0, 0};
  static int flags[64][2];
  int nflags = 0;
  for (int i = 1; i < argc; i++) {
    const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : "";
    if (!strcmp(a, "--frames")) frames = atoi(v), i++;
    else if (!strcmp(a, "--keys")) parse_keys(v), i++;
    else if (!strcmp(a, "--shots")) {
      shots = v, i++;
      if (!strcmp(v, "all")) all = true;
      if (!strncmp(v, "every:", 6)) every = atoi(v + 6);
    } else if (!strcmp(a, "--out")) out = v, i++;
    else if (!strcmp(a, "--saves")) host_save_dir(v), i++;
    else if (!strcmp(a, "--room")) room = atoi(v), i++;
    else if (!strcmp(a, "--at")) sscanf(v, "%d,%d", &ax, &ay), i++;
    else if (!strcmp(a, "--plot")) plotv = atoi(v), i++;
    else if (!strcmp(a, "--party")) sscanf(v, "%d,%d,%d", &party[0], &party[1], &party[2]), i++;
    else if (!strcmp(a, "--flag") && nflags < 64) sscanf(v, "%d=%d", &flags[nflags][0], &flags[nflags][1]), nflags++, i++;
    else if (!strcmp(a, "--battle")) battle = atoi(v), i++;
    else if (!strcmp(a, "--fuzz")) fuzz = (unsigned)atoi(v), i++;
    else if (!strcmp(a, "--mash")) mash = true;
    else if (!strcmp(a, "--trace")) trace = true;
  }
  mkdir(out, 0755);
  dr_bin = dr_data;
  game_init();
  if (room >= 0 || battle >= 0) {
    memset(sv.name, 0, sizeof sv.name);
    strcpy(sv.name, "TEST");
    game_new();
    if (plotv >= 0) sv.plot = (int16_t)plotv;
    if (party[0]) for (int k = 0; k < 3; k++) sv.chr[k] = (uint8_t)party[k];
    for (int k = 0; k < nflags; k++) set_flag(flags[k][0], flags[k][1]);
    if (room >= 0) world_enter(room, ax, ay, DIR_DOWN);
    if (battle >= 0) battle_start(battle);
  }
  uint32_t seed = fuzz * 2654435761u + 1;
  for (int f = 0; f < frames && g_running; f++) {
    uint32_t k = 0;
    for (int h = 0; h < nholds; h++)
      if (f >= holds[h].a && f <= holds[h].b) k |= holds[h].k;
    if (fuzz) {
      seed ^= seed << 13, seed ^= seed >> 17, seed ^= seed << 5;
      static const uint32_t ks[] = {K_LEFT, K_RIGHT, K_UP, K_DOWN, K_Z, K_X, K_C, 0};
      k = ks[seed % 8] | ((seed >> 8) % 3 == 0 ? ks[(seed >> 12) % 8] : 0);
    } else if (mash && !k && f % 4 == 0) {
      k = K_Z;
    }
    host_keys = k;
    game_frame(k);
    host_time += 33;
    game_draw();
    bool shoot = all || (every && f % every == 0);
    if (!shoot && *shots) {
      char key[16];
      snprintf(key, sizeof key, "%d", f);
      const char *p = shots;
      while (*p) {
        if (!strncmp(p, key, strlen(key)) && (p[strlen(key)] == ',' || !p[strlen(key)])) shoot = true;
        while (*p && *p != ',') p++;
        if (*p) p++;
      }
    }
    if (shoot) {
      char path[512];
      snprintf(path, sizeof path, "%s/shot_%05d.ppm", out, f);
      host_shot(path);
    }
    if (trace && f % 30 == 0)
      printf("frame %d mode %d room %d kris %d,%d plot %d text %d event %d\n", f, g_mode, g_room, (int)pl.x, (int)pl.y,
             sv.plot, text_active(), event_running());
  }
  printf("STATE mode %d room %d kris %d,%d dir %d plot %d hp %d,%d,%d gold %ld party %d,%d,%d dropped %d blocks %u\n",
         g_mode, g_room, (int)pl.x, (int)pl.y, pl.dir, sv.plot, sv.hp[1], sv.hp[2], sv.hp[3], (long)sv.gold,
         sv.chr[0], sv.chr[1], sv.chr[2], gfx_dropped, (unsigned)blk_most_in_frame);
  return 0;
}
