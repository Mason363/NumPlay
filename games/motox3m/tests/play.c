/* Plays Moto X3M on a computer, with scripted keys, and saves frames.
 *
 *   play [--level N] [--frames N] [--keys "10-40:right,50:ok,..."] [--shots 30,60|every:N] [--out DIR]
 *        [--saves DIR] [--gas] [--auto] [--stunt H] [--fuzz SEED] [--draw] [--trace N]
 *
 * Keys: left right up down ok back home. "a-b:key" holds the key from frame a to b, "a:key" for one frame (a frame
 * is a game tick, 1/30 s). --level starts that level (1-50) past the menus; --gas holds the gas from the start.
 * Screenshots are DIR/shot_<frame>.ppm. --fuzz presses random keys through menus and runs. --trace N prints the
 * bike every N ticks. --auto drives (the gas, and leaning to keep the bike level); --stunt H also back flips when
 * the ground is more than H below a jump. At the end: END state S level L timer T dead D finished F x X y Y. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "../src/bike.h"
#include "../src/game.h"
#include "../src/gfx.h"
#include "../src/level.h"
#include "../src/view.h"

extern uint32_t host_keys, host_time;
void host_shot(const char *path);
void host_save_dir(const char *d);

typedef struct { int a, b; uint32_t k; } Hold;
static Hold holds[4096];
static int nholds;
static int shots[4096], nshots, every;

static uint32_t key_of(const char *s) {
  static const char *const names[] = {"left", "right", "up", "down", "ok", "back", "home"};
  static const uint32_t bits[] = {K_LEFT, K_RIGHT, K_UP, K_DOWN, K_OK, K_BACK, K_HOME};
  for (int i = 0; i < 7; i++)
    if (!strcmp(s, names[i])) return bits[i];
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

static uint32_t rng = 1;
static uint32_t rnd(void) {
  rng ^= rng << 13, rng ^= rng >> 17, rng ^= rng << 5;
  return rng;
}

static int arbs_used(void) {
  int n = 0;
  for (int i = 0; i < NP_MAX_ARBS; i++) n += sp.arbs[i].used;
  return n;
}
static int asleep(void) {
  int n = 0;
  for (int i = 0; i < sp.nbodies; i++) n += (sp.bodies[i]->flags & BF_SLEEP) != 0;
  return n;
}

int main(int argc, char **argv) {
  int frames = 300, level = 0, trace = 0, air_t = 0;
  float stunt = 0, flip_from = NAN, flip_seen = 0;
  bool gas = false, fuzz = false, draw = false, autop = false;
  const char *out = "build/shots";
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--frames")) frames = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--level")) level = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--keys")) parse_keys(argv[++i]);
    else if (!strcmp(argv[i], "--out")) out = argv[++i];
    else if (!strcmp(argv[i], "--saves")) host_save_dir(argv[++i]);
    else if (!strcmp(argv[i], "--gas")) gas = true;
    else if (!strcmp(argv[i], "--auto")) autop = true;
    else if (!strcmp(argv[i], "--stunt")) autop = true, stunt = (float)atof(argv[++i]);
    else if (!strcmp(argv[i], "--draw")) draw = true;
    else if (!strcmp(argv[i], "--trace")) trace = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--fuzz")) fuzz = true, rng = (uint32_t)atoi(argv[++i]) * 2654435761u + 1;
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
  static uint32_t visbuf[VIEW_VIS_BYTES / 4];
  static float scrbuf[VIEW_SCR_FLOATS];
  gfx_init(band);
  view_init(visbuf, scrbuf);
  game_init();
  if (level) game_start_level(level - 1);
  uint32_t fk = 0;
  static int state_hist[8];
  for (int f = 0; f < frames && g.running; f++) {
    uint32_t k = 0;
    for (int i = 0; i < nholds; i++)
      if (f >= holds[i].a && f <= holds[i].b) k |= holds[i].k;
    if (gas) k |= K_UP;
    if (autop && g.state == S_PLAY) {
      /* gas, and lean to keep the bike level; after a crash, go on */
      float r = fmodf(bike.frame.rot, 6.2831853f);
      if (r > 3.14159f) r -= 6.2831853f;
      if (r < -3.14159f) r += 6.2831853f;
      k |= K_UP;
      if (r < -0.35f) k |= K_RIGHT;
      else if (r > 0.35f) k |= K_LEFT;
      if (bike.dead && g.dead_t > 0.5f && f % 2) k = K_OK;
    }
    if (stunt && g.state == S_PLAY && !bike.dead) {
      /* in a long jump (the ground far below), a back flip, then level again for the landing */
      bool air = !bike.wheel_ground[0] && !bike.wheel_ground[1] && !bike.frame_ground;
      air_t = air ? air_t + 1 : 0;
      if (air_t == 4) {
        Body *b;
        int sb;
        float d;
        /* (from below the bike, which the ray would hit) */
        flip_from = np_raycast((V2){bike.frame.pos.x, bike.frame.pos.y + 70}, (V2){0, 1}, 4000, &b, &sb, &d) && d + 70 > stunt
                        ? bike.frame.rot : NAN;
      }
      if (air_t >= 4 && !isnan(flip_from) && bike.frame.rot > flip_from - 6.2831853f + 0.9f) k = K_UP | K_LEFT;
    }
    if (g.flip_t < flip_seen) printf("FLIP %d at %d\n", g.flip_n, f);
    flip_seen = g.flip_t;
    if (autop && g.state == S_FINISH && f % 30 == 0) k = 0;
    if (fuzz) {
      if (rnd() % 8 == 0) fk = rnd() & (K_LEFT | K_RIGHT | K_UP | K_DOWN | K_OK | (rnd() % 16 == 0 ? K_BACK : 0));
      k = fk;
      if (g.state == S_TITLE || (g.state == S_PAUSE && g.confirm)) k &= ~(uint32_t)K_BACK;   /* (stay in the game) */
      if (g.state == S_PAUSE && (g.confirm || g.sel == 3)) k &= ~(uint32_t)K_OK;
    }
    host_keys = k;
    state_hist[g.state]++;
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
    if (trace && f % trace == 0 && (g.state == S_PLAY || g.state == S_FINISH))
      printf("t%4d x %8.1f y %7.1f rot %6.2f v %6.1f wheel %6.2f ground %d%d%d timer %5.2f dead %d arbs %d sleep %d\n", f,
             bike.frame.pos.x, bike.frame.pos.y, bike.frame.rot, bike.speed, bike.wheel[0].angvel, bike.wheel_ground[0],
             bike.wheel_ground[1], bike.frame_ground, g.timer, bike.dead, arbs_used(), asleep());
  }
  if (fuzz) printf("STATES title %d map %d shop %d play %d pause %d finish %d\n", state_hist[0], state_hist[1], state_hist[2],
                  state_hist[3], state_hist[4], state_hist[5]);
  printf("END state %d level %d timer %.2f dead %d finished %d x %.1f y %.1f arena %d overflow %d\n", g.state,
         g.level + 1, g.timer, bike.dead, bike.finished, bike.frame.pos.x, bike.frame.pos.y, arena_peak, sp.overflow);
  return 0;
}
