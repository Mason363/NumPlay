/* Plays 2 3 4 Players on a computer, with scripted keys and computer players, and saves screenshots.
 *
 *   play [--frames N] [--keys "10:ok,20-30:p1,..."] [--shots "40:name.ppm,..."] [--out DIR] [--saves DIR]
 *        [--trace] [--fuzz SEED] [--seed N] [--bots FRAME] [--raw DIR] [--every K]
 *
 * A frame is 20 ms. Keys: left right up down ok exe back home onoff, and p1 to p6 the players' (shift,
 * backspace, 0, EXE, 7 and )). "a-b:key" holds the key from frame a to b, "a:key" for one frame.
 * --bots F: from frame F on, the computer plays for every player whenever a round is on (each game's bot).
 * --fuzz presses random keys (Home now and then ends a session, the next one starts from the save).
 * --raw DIR saves every frame drawn as raw RGB565 (tools/record.py --frames); --every K draws every Kth frame
 * (the screenshots and --raw draw theirs anyway). --trace prints each screen as it comes (SCREEN ...).
 * At the end: STATE ... */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/app.h"
#include "../src/fp.h"
#include "../src/plat.h"

extern uint32_t host_time, host_seed;
extern uint64_t host_keys;
void host_shot(const char *path);
void host_raw(const char *path);
void host_save_dir(const char *d);

typedef struct {
  int a, b;
  uint64_t k;
} Hold;
static Hold holds[8192];
static int nholds;
typedef struct {
  int frame;
  char name[128];
} Shot;
static Shot shots[256];
static int nshots;
static char out_dir[512] = ".", raw_dir[512];

static uint64_t key_of(const char *s) {
  static const char *const names[] = {"left", "right", "up", "down", "ok", "exe", "back", "home", "onoff"};
  static const int bits[] = {FK_LEFT, FK_RIGHT, FK_UP, FK_DOWN, FK_OK, FK_EXE, FK_BACK, FK_HOME, FK_ONOFF};
  for (int i = 0; i < 9; i++)
    if (!strcmp(s, names[i])) return KEY(bits[i]);
  if (s[0] == 'p' && s[1] >= '1' && s[1] <= '6' && !s[2]) return app_player_key(s[1] - '1');
  fprintf(stderr, "unknown key %s\n", s);
  exit(2);
}
static void parse_keys(const char *spec) {
  char buf[65536];
  snprintf(buf, sizeof buf, "%s", spec);
  for (char *t = strtok(buf, ","); t; t = strtok(NULL, ",")) {
    int a, b;
    char name[32];
    if (sscanf(t, "%d-%d:%31s", &a, &b, name) == 3) holds[nholds++] = (Hold){a, b, key_of(name)};
    else if (sscanf(t, "%d:%31s", &a, name) == 2) holds[nholds++] = (Hold){a, a, key_of(name)};
  }
}
static void parse_shots(const char *spec) {
  char buf[65536];
  snprintf(buf, sizeof buf, "%s", spec);
  for (char *t = strtok(buf, ","); t; t = strtok(NULL, ",")) {
    Shot s;
    if (sscanf(t, "%d:%127s", &s.frame, s.name) == 2) shots[nshots++] = s;
  }
}

static const char *const SCREEN[] = {"mode", "games", "rules", "play", "score", "over", "pause", "ask"};
static int frame_now;
static void print_state(const char *what) {
  app_state_t s;
  app_state(&s);
  printf("%s %s mode %d game %d cur %d target %d round %d sides %d score %d,%d,%d,%d winner %d matches %d", what,
         SCREEN[s.screen], s.mode, s.game, s.cur, s.target, s.round, s.nsides, s.score[0], s.score[1], s.score[2],
         s.score[3], s.winner, s.matches);
  if (!strcmp(what, "STATE")) {
    printf(" wins");
    for (int g = 0; g < 15; g++) printf(" %d,%d,%d,%d,%d,%d", s.wins[g][0], s.wins[g][1], s.wins[g][2], s.wins[g][3], s.wins[g][4], s.wins[g][5]);
  } else {
    printf(" frame %d", frame_now);
  }
  printf("\n");
}

int main(int argc, char **argv) {
  int frames = 600, fuzz = -1, bots = -1, every = 1;
  bool trace = false;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--keys") && i + 1 < argc) parse_keys(argv[++i]);
    else if (!strcmp(argv[i], "--shots") && i + 1 < argc) parse_shots(argv[++i]);
    else if (!strcmp(argv[i], "--out") && i + 1 < argc) snprintf(out_dir, sizeof out_dir, "%s", argv[++i]);
    else if (!strcmp(argv[i], "--raw") && i + 1 < argc) snprintf(raw_dir, sizeof raw_dir, "%s", argv[++i]);
    else if (!strcmp(argv[i], "--saves") && i + 1 < argc) host_save_dir(argv[++i]);
    else if (!strcmp(argv[i], "--trace")) trace = true;
    else if (!strcmp(argv[i], "--fuzz") && i + 1 < argc) fuzz = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--seed") && i + 1 < argc) host_seed = (uint32_t)atoi(argv[++i]) | 1;
    else if (!strcmp(argv[i], "--bots") && i + 1 < argc) bots = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--every") && i + 1 < argc) every = atoi(argv[++i]);
    else {
      fprintf(stderr, "play: what is %s?\n", argv[i]);
      return 2;
    }
  }
  uint32_t fz = (uint32_t)fuzz * 2654435761u + 12345;
  host_time = 1000;
  app_init(host_time);
  int last_screen = -1, sessions = 1, raw_n = 0;
  uint64_t fuzz_keys = 0;
  for (int f = 0; f < frames; f++) {
    uint64_t k = 0;
    for (int i = 0; i < nholds; i++)
      if (f >= holds[i].a && f <= holds[i].b) k |= holds[i].k;
    if (fuzz >= 0) { /* random keys: a few held at once, changing often */
      fz ^= fz << 13, fz ^= fz >> 17, fz ^= fz << 5;
      if (fz % 5 == 0) {
        static const int pool[] = {FK_LEFT, FK_RIGHT, FK_UP, FK_DOWN, FK_OK, FK_OK, FK_EXE, FK_BACK, FK_SHIFT, FK_BACKSPACE,
                                   FK_ZERO, FK_EXE, FK_SEVEN, FK_RPAREN, FK_SHIFT, FK_ZERO, FK_HOME};
        int key = pool[(fz >> 8) % (sizeof pool / sizeof *pool)];
        if (key == FK_HOME && (fz >> 20) % 6) key = FK_OK; /* (Home now and then) */
        fuzz_keys ^= KEY(key);
      }
      if (fz % 23 == 0) fuzz_keys = 0;
      k |= fuzz_keys;
    }
    if (bots >= 0 && f >= bots) k |= app_bot_keys();
    host_keys = k;
    host_time += 20;
    if (!app_step(k, host_time)) {
      if (fuzz < 0) {
        printf("QUIT frame %d\n", f);
        break;
      }
      host_keys = fuzz_keys = 0; /* a new session, from the save */
      app_init(host_time), sessions++;
    }
    app_state_t s;
    app_state(&s);
    frame_now = f;
    if (trace && s.screen != last_screen) print_state("SCREEN");
    last_screen = s.screen;
    bool shot = false;
    for (int i = 0; i < nshots; i++) shot |= shots[i].frame == f;
    if (app_dirty() && (f % every == 0 || shot || raw_dir[0])) app_draw();
    if (raw_dir[0]) {
      char p[1024];
      snprintf(p, sizeof p, "%s/%06d_%06d.raw", raw_dir, raw_n++, f * 20);
      host_raw(p);
    }
    for (int i = 0; i < nshots; i++)
      if (shots[i].frame == f) {
        char p[1024];
        app_draw();
        snprintf(p, sizeof p, "%s/%s", out_dir, shots[i].name);
        host_shot(p);
      }
  }
  if (fuzz >= 0) printf("SESSIONS %d\n", sessions);
  print_state("STATE");
  return 0;
}
