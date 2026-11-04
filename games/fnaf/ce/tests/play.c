/* Plays the CE version of Five Nights at Freddy's on a computer, with scripted keys (or a player of its own), and
 * saves frames as the calculator's screen shows them.
 *
 *   play [--frames N] [--keys "10-40:left,50:2nd,..."] [--shots 30,60|all|every:N] [--out DIR] [--saves DIR]
 *        [--data DIR] [--night N] [--ai F,B,C,X] [--power P] [--hour H] [--pos F,B,C] [--foxy N] [--gold]
 *        [--stars S] [--seed S] [--bot] [--fps N] [--ram BYTES]
 *
 * Keys (the CE's): left right up down 2nd enter clear mode del 0 to 9. "a-b:key" holds the key from step a to b,
 * "a:key" for one step. --night starts that night (its card first); --ai, --power, --hour, --pos (Freddy, Bonnie,
 * Chica: where they are), --foxy (his step out of the cove) and --gold then set the night; --stars the stars won.
 * --bot plays the night itself. Steps are 60 a second; a frame is drawn every 60/fps steps (default 30 fps).
 * Screenshots are DIR/shot_<step>.ppm. At the end it prints the state: "END scene ... night ... hour ...". */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "../src/fnaf.h"

extern uint32_t host_keys, host_ticks;
extern const char *host_data_dir, *host_save_dir;
extern size_t host_ram_size;
void host_shot(const char *path);
int fnaf_start(void);

typedef struct { int a, b; uint32_t k; } Hold;
static Hold holds[4096];
static int nholds;

static uint32_t key_of(const char *s) {
  static const struct { const char *n; uint32_t k; } names[] = {
    {"left", CK_LEFT}, {"right", CK_RIGHT}, {"up", CK_UP}, {"down", CK_DOWN}, {"2nd", CK_2ND}, {"ok", CK_2ND},
    {"enter", CK_ENTER}, {"clear", CK_CLEAR}, {"back", CK_CLEAR}, {"mode", CK_MODE}, {"home", CK_MODE},
    {"del", CK_DEL}};
  for (unsigned i = 0; i < sizeof names / sizeof names[0]; i++)
    if (!strcmp(s, names[i].n)) return names[i].k;
  if (s[0] >= '0' && s[0] <= '9' && !s[1]) return CK_DIGIT(s[0] - '0');
  fprintf(stderr, "unknown key %s\n", s);
  exit(2);
}

static void parse_keys(char *s) {
  for (char *t = strtok(s, ","); t; t = strtok(NULL, ",")) {
    char *colon = strchr(t, ':');
    if (!colon) continue;
    *colon = 0;
    int a = atoi(t), b = a;
    char *dash = strchr(t, '-');
    if (dash) b = atoi(dash + 1);
    holds[nholds++] = (Hold){a, b, key_of(colon + 1)};
  }
}

/* ------------------------------------------------------------------ a player (as the NumWorks version's tests) */
static uint32_t bot(int f) {
  Night *n = &g.n;
  if (g.scene != SC_NIGHT || g.paused) return 0;
  static int next_look, look_until;
  bool up = n->flip >= 1;
  bool bonnie = n->pos[BONNIE] == AT_DOOR, chica = n->pos[CHICA] == AT_DOOR;
  bool want_l = bonnie || n->foxy == 3;
  bool want_r = chica || (n->pos[FREDDY] == CAM_4B && (n->monitor || f >= next_look - 40));
  if (n->monitor) {
    if (up && n->cam != CAM_1C && !(f & 1)) return CK_DIGIT(1);
    if (up && f >= look_until && !(f & 1)) return CK_2ND;
    return 0;
  }
  if (n->flip > 0 || (f & 1)) return 0;
  if (n->door[LEFT] != want_l) return CK_DIGIT(7);
  if (n->door[RIGHT] != want_r) return CK_DIGIT(9);
  if (f >= next_look && !n->dark) {
    next_look = f + (n->foxy >= 1 ? 200 : 360) + (int)(rnd() % 200);
    look_until = f + 50;
    return CK_2ND;
  }
  return 0;
}

int main(int argc, char **argv) {
  int frames = 600, night = 0, power = -1, hour = -1, ai[4] = {-1, -1, -1, -1}, gold = 0, stars = -1, fps = 30;
  int pos[3] = {-1, -1, -1}, foxy = -1;
  bool use_bot = false, all = false;
  int every = 0;
  const char *out = "build/shots";
  static int shots[4096];
  int nshots = 0;
  for (int i = 1; i < argc; i++) {
    const char *a = argv[i];
    if (!strcmp(a, "--frames")) frames = atoi(argv[++i]);
    else if (!strcmp(a, "--keys")) parse_keys(argv[++i]);
    else if (!strcmp(a, "--shots")) {
      char *s = argv[++i];
      if (!strcmp(s, "all")) all = true;
      else if (!strncmp(s, "every:", 6)) every = atoi(s + 6);
      else
        for (char *t = strtok(s, ","); t && nshots < 4096; t = strtok(NULL, ",")) shots[nshots++] = atoi(t);
    } else if (!strcmp(a, "--out")) out = argv[++i];
    else if (!strcmp(a, "--saves")) host_save_dir = argv[++i];
    else if (!strcmp(a, "--data")) host_data_dir = argv[++i];
    else if (!strcmp(a, "--night")) night = atoi(argv[++i]);
    else if (!strcmp(a, "--power")) power = atoi(argv[++i]);
    else if (!strcmp(a, "--hour")) hour = atoi(argv[++i]);
    else if (!strcmp(a, "--ai")) sscanf(argv[++i], "%d,%d,%d,%d", &ai[0], &ai[1], &ai[2], &ai[3]);
    else if (!strcmp(a, "--seed")) {
      unsigned s = (unsigned)atoi(argv[++i]);
      for (unsigned k = 0; k < s % 1000; k++) rnd();
    } else if (!strcmp(a, "--bot")) use_bot = true;
    else if (!strcmp(a, "--gold")) gold = 1;
    else if (!strcmp(a, "--foxy")) foxy = atoi(argv[++i]);
    else if (!strcmp(a, "--pos")) sscanf(argv[++i], "%d,%d,%d", &pos[0], &pos[1], &pos[2]);
    else if (!strcmp(a, "--stars")) stars = atoi(argv[++i]);
    else if (!strcmp(a, "--fps")) fps = atoi(argv[++i]);
    else if (!strcmp(a, "--ram")) host_ram_size = (size_t)atol(argv[++i]);
    else {
      fprintf(stderr, "unknown argument %s\n", a);
      return 2;
    }
  }
  mkdir(out, 0755);
  mkdir(host_save_dir, 0755);
  ce_init();
  host_ticks = 32768;
  int r = fnaf_start();
  if (r) {
    printf("START FAILED %d %s\n", r, ce_missing);
    return 1;
  }
  if (stars >= 0) g.stars = (uint8_t)stars;
  if (night) {
    if (night == 7)
      for (int k = 0; k < 4; k++) g.custom[k] = (int8_t)(ai[k] >= 0 ? ai[k] : 0);
    start_night(night);
  }
  bool set = false;
  int f, step = 60 / (fps > 0 ? fps : 30);
  for (f = 0; f < frames && g.running; f++) {
    if (!set && g.scene == SC_NIGHT) {
      set = true;
      for (int k = 0; k < 4; k++)
        if (ai[k] >= 0) g.n.ai[k] = (int8_t)ai[k];
      if (power >= 0) g.n.power = (int16_t)power;
      if (hour >= 0) g.n.hour = (uint8_t)hour, g.n.t = hour * 89.0f + 1;
      if (gold) g.n.gold = 1;
      for (int k = 0; k < 3; k++)
        if (pos[k] >= 0) g.n.pos[k] = (uint8_t)pos[k];
      if (foxy >= 0) g.n.foxy = (uint8_t)foxy;
    }
    uint32_t k = 0;
    for (int i = 0; i < nholds; i++)
      if (f >= holds[i].a && f <= holds[i].b) k |= holds[i].k;
    if (use_bot && !k) k = bot(f);
    host_keys = k;
    int was = g.scene;
    game_step(fnaf_keys());
    if (g.scene != was && (g.scene == SC_SIX || g.scene == SC_SCARE || g.scene == SC_GOLDEN))
      printf("EVENT %s night %d hour %d power %d t %.1f who %d foxy_pounds %d\n",
             g.scene == SC_SIX ? "six" : g.scene == SC_SCARE ? "scare" : "golden", g.n.night, g.n.hour, g.n.power,
             g.n.t, g.scare, g.n.foxy_pounds);
    host_ticks += 32768 / 60 + (f % 15 == 0 ? 2 : 0);
    bool shot = all || (every && f % every == 0);
    for (int i = 0; i < nshots; i++) shot |= shots[i] == f;
    if (g.running && (shot || f % step == 0)) {
      game_draw();
      ce_show(keep_frame);
      keep_frame = false;
      if (shot) {
        char p[512];
        snprintf(p, sizeof p, "%s/shot_%05d.ppm", out, f);
        host_shot(p);
      }
    }
  }
  ce_quit();
  printf("END scene %d night %d hour %d power %d t %.1f frames %d running %d paused %d reached %d stars %d "
         "pos %d,%d,%d foxy %d gold %d dark %d muted %d ai %d,%d,%d,%d doors %d,%d cam %d keys_seen %d pounds %d\n",
         g.scene, g.n.night, g.n.hour, g.n.power, g.n.t, f, g.running, g.paused, g.reached, g.stars, g.n.pos[0],
         g.n.pos[1], g.n.pos[2], g.n.foxy, g.n.gold, g.n.dark, g.n.call_muted, g.n.ai[0], g.n.ai[1], g.n.ai[2],
         g.n.ai[3], g.n.door[0], g.n.door[1], g.n.cam, g.keys_seen, g.n.foxy_pounds);
  return 0;
}
