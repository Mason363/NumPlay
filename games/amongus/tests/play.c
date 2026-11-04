/* Plays Among Us on a computer, with scripted keys, and saves frames.
 *
 *   play [--frames N] [--keys "10-40:right,50:ok,..."] [--shots 30,60|every:N] [--out DIR] [--saves DIR]
 *        [--start crew|imp] [--auto] [--fuzz SEED] [--seed N] [--draw] [--trace N] [--bots N]
 *
 * Keys: left right up down ok exe back home tool 0-9. "a-b:key" holds the key from frame a to b, "a:key" for one
 * frame (a frame is a game tick, 1/30 s). --start begins a game past the menus, as a crewmate or an impostor.
 * --auto lets the bots' brain play the player too (a whole game to its end). --fuzz presses random keys.
 * Screenshots are DIR/shot_<frame>.ppm. At the end: END screen S winner W reason R t T alive A tasks P. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "../src/game.h"
#include "../src/gfx.h"
#include "../src/plat.h"
#include "../src/world.h"

extern uint32_t host_keys, host_time;
extern bool game_running, autopilot;
void host_shot(const char *path);
void host_save_dir(const char *d);

typedef struct { int a, b; uint32_t k; } Hold;
static Hold holds[4096];
static int nholds;
static int shots[4096], nshots, every;

static uint32_t key_of(const char *s) {
  static const char *const names[] = {"left", "right", "up", "down", "ok", "exe", "back", "home", "tool"};
  static const uint32_t bits[] = {K_LEFT, K_RIGHT, K_UP, K_DOWN, K_OK, K_EXE, K_BACK, K_HOME, K_TOOL};
  for (int i = 0; i < 9; i++)
    if (!strcmp(s, names[i])) return bits[i];
  if (s[0] >= '0' && s[0] <= '9' && !s[1]) return (uint32_t)K_D0 << (s[0] - '0');
  fprintf(stderr, "unknown key %s\n", s);
  exit(2);
}

static void parse_keys(const char *spec) {
  static char buf[65536];
  snprintf(buf, sizeof buf, "%s", spec);
  for (char *tok = strtok(buf, ","); tok; tok = strtok(NULL, ",")) {
    int a, b;
    char k[16];
    if (sscanf(tok, "%d-%d:%15s", &a, &b, k) == 3) holds[nholds++] = (Hold){a, b, key_of(k)};
    else if (sscanf(tok, "%d:%15s", &a, k) == 2) holds[nholds++] = (Hold){a, a, key_of(k)};
  }
}

/* opens a minigame straight away (tests and screenshots): its kind as in mg.h */
static void open_minigame(int kind) {
  static const int8_t TYPE[] = {T_SWIPE_CARD, T_FIX_WIRING, T_CLEAR_ASTEROIDS, T_ALIGN_ENGINE, T_SUBMIT_SCAN,
                                T_INSPECT_SAMPLE, T_FUEL_ENGINES, T_START_REACTOR, T_EMPTY_GARBAGE, T_UPLOAD_DATA,
                                T_CALIBRATE, T_CHART_COURSE, T_CLEAN_O2, T_UNLOCK_MANIFOLDS, T_STABILIZE,
                                T_PRIME_SHIELDS, T_DIVERT_POWER, T_DIVERT_POWER};
  g.screen = S_PLAY;
  Player *P = &g.p[g.me];
  if (kind < 18) {
    for (int d = 0; d < N_TASK_DEFS; d++)
      if (TASK_DEFS[d].type == TYPE[kind]) {
        memset(&P->tasks[0], 0, sizeof P->tasks[0]);
        P->tasks[0].def = (uint8_t)d;
        P->tasks[0].data[0] = 0, P->tasks[0].data[1] = 2, P->tasks[0].data[2] = 4;
        if (kind == 17) P->tasks[0].step = 1;
        if (TYPE[kind] == T_INSPECT_SAMPLE) P->tasks[0].timer = -1;
        break;
      }
    int c = task_console(P, &P->tasks[0]);
    P->pos = (V2){CONSOLES[c].sx, CONSOLES[c].sy};
    mg_open(c, 0);
  } else if (kind <= 21) {
    static const int SAB[] = {SAB_REACTOR, SAB_LIGHTS, SAB_COMMS, SAB_O2};
    g.sab_cd = 0;
    sabotage(SAB[kind - 18]);
    for (int c = 0; c < N_CONSOLES; c++) {
      const ConsoleRec *C = &CONSOLES[c];
      int t = SAB[kind - 18] == SAB_REACTOR ? T_RESET_REACTOR : SAB[kind - 18] == SAB_LIGHTS ? T_FIX_LIGHTS
              : SAB[kind - 18] == SAB_COMMS ? T_FIX_COMMS : T_RESTORE_O2;
      if (!C->kind && (C->types >> t & 1)) {
        P->pos = (V2){C->sx, C->sy};
        mg_open(c, MAX_TASKS);
        break;
      }
    }
  } else {
    for (int c = 0; c < N_CONSOLES; c++)
      if (CONSOLES[c].kind == (kind == 22 ? 3 : 2)) {
        P->pos = (V2){CONSOLES[c].sx, CONSOLES[c].sy};
        mg_open(c, MAX_TASKS);
        break;
      }
  }
  g.cam = (V2){P->pos.x, P->pos.y + FEET_Y};
}

static uint32_t frng = 1;
static uint32_t frnd(void) {
  frng ^= frng << 13, frng ^= frng >> 17, frng ^= frng << 5;
  return frng;
}

int main(int argc, char **argv) {
  int frames = 300, trace = 0, start = 0, bots = -1, mgkind = -1;
  uint32_t seed = 0;
  bool fuzz = false, draw = false;
  const char *out = "build/shots";
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--frames")) frames = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--keys")) parse_keys(argv[++i]);
    else if (!strcmp(argv[i], "--out")) out = argv[++i];
    else if (!strcmp(argv[i], "--saves")) host_save_dir(argv[++i]);
    else if (!strcmp(argv[i], "--start")) start = !strcmp(argv[++i], "imp") ? 2 : 1;
    else if (!strcmp(argv[i], "--auto")) autopilot = true;
    else if (!strcmp(argv[i], "--draw")) draw = true;
    else if (!strcmp(argv[i], "--trace")) trace = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--seed")) seed = (uint32_t)atoi(argv[++i]);
    else if (!strcmp(argv[i], "--bots")) bots = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--mg")) mgkind = atoi(argv[++i]), start = start ? start : 1;
    else if (!strcmp(argv[i], "--fuzz")) fuzz = true, frng = (uint32_t)atoi(argv[++i]) * 2654435761u + 1;
    else if (!strcmp(argv[i], "--shots")) {
      const char *s = argv[++i];
      if (!strncmp(s, "every:", 6)) every = atoi(s + 6);
      else
        for (const char *p = s; *p;) {
          shots[nshots++] = atoi(p);
          p = strchr(p, ',');
          if (!p) break;
          p++;
        }
    }
  }
  mkdir(out, 0755);
  static uint16_t band[SCREEN_W * BAND];
  gfx_init(band);
  host_time = 1000 + seed * 7919;
  game_init();
  if (start) {
    if (bots >= 0) g.opt.bots = (uint8_t)bots;
    g.opt.role = (uint8_t)start;
    g.rng = seed * 2654435761u + 77;
    game_new();
    if (mgkind >= 0) open_minigame(mgkind);
  }
  uint32_t fk = 0;
  static int hist[16];
  for (int f = 0; f < frames && game_running; f++) {
    uint32_t k = 0;
    for (int i = 0; i < nholds; i++)
      if (f >= holds[i].a && f <= holds[i].b) k |= holds[i].k;
    if (fuzz) {
      if (frnd() % 6 == 0) {
        uint32_t r = frnd();
        fk = r & (K_LEFT | K_RIGHT | K_UP | K_DOWN | K_OK | K_EXE | K_TOOL);
        if (frnd() % 12 == 0) fk |= K_BACK;
        if (frnd() % 10 == 0) fk |= (uint32_t)K_D0 << (frnd() % 10);
      }
      k = fk;
    }
    if (autopilot && (g.screen == S_INTRO || g.screen == S_EJECT) && f % 20 == 0) k |= K_OK;
    host_keys = k;
    hist[g.screen & 15]++;
    game_tick(k);
    host_time += 33;
    bool shot = every ? f % every == 0 : false;
    for (int i = 0; i < nshots; i++) shot |= shots[i] == f;
    if (shot || draw) game_draw();
    if (shot) {
      char p[512];
      snprintf(p, sizeof p, "%s/shot_%04d.ppm", out, f);
      host_shot(p);
    }
    if (trace && f % trace == 0)
      printf("t%5d screen %d ov %d me (%.2f %.2f) room %d alive %d imp %d sab %d tasks %.2f\n", f, g.screen, g.overlay,
             g.p[g.me].pos.x, g.p[g.me].pos.y, g.p[g.me].room, g.p[g.me].alive, g.p[g.me].imp, g.sab, tasks_progress());
    if (g.screen == S_END && autopilot && g.screen_t > 1) break;
  }
  if (fuzz)
    printf("SCREENS title %d lobby %d intro %d play %d report %d meeting %d eject %d end %d howto %d pause %d killed %d\n",
           hist[0], hist[1], hist[2], hist[3], hist[4], hist[5], hist[6], hist[7], hist[8], hist[9], hist[10]);
  int alive = 0;
  for (int i = 0; i < g.nplayers; i++) alive += g.p[i].alive;
  printf("END screen %d winner %d reason %d t %.1f alive %d tasks %.2f imp %d\n", g.screen, g.winner, g.end_reason, g.t,
         alive, tasks_progress(), g.p[g.me].imp);
  return 0;
}
