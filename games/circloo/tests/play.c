/* Plays circloO on a computer with scripted keys and saves screenshots.
 *
 *   play [--level N] [--hard] [--frames N] [--keys "10-40:right,50:ok,..."] [--shots 30,60|every:N] [--out DIR]
 *        [--saves DIR] [--auto] [--fuzz SEED] [--draw] [--trace N]
 *
 * A frame is one of the game's steps (1/60 s). Keys: left right up down ok back home. "a-b:key" holds it from
 * frame a to b, "a:key" for one frame. --level starts that level past the menus. --auto rolls the ball towards
 * the nearest circle it can take. --fuzz presses random keys through the menus and levels. Screenshots are
 * DIR/shot_<frame>.ppm. At the end: END state S level L hard H taken T complete C frames F. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/game.h"
#include "../src/gfx.h"
#include "../src/phys.h"
#include "../src/plat.h"

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

/* rolls towards the nearest circle the ball can take */
static uint32_t autoplay(void) {
  if (g.state != S_PLAY || L.ball < 0) return 0;
  const Body *b = &W.b[L.ball];
  float best = 1e30f, tx = 0;
  for (int k = 0; k < L.ncoll; k++) {
    const Coll *c = &L.coll[k];
    if (c->taken || c->io) continue;
    float d = sqrtf((c->x - 1500) * (c->x - 1500) + (c->y - 1500) * (c->y - 1500));
    if (d > L.R) continue;
    float dx = c->x * PX_M - b->x, dy = c->y * PX_M - b->y, dd = dx * dx + dy * dy;
    if (dd < best) best = dd, tx = dx;
  }
  if (best > 1e29f) return 0;
  float want = tx * 2.0f;   /* a speed towards it, m/s */
  if (want > 6) want = 6;
  if (want < -6) want = -6;
  return b->vx < want - 0.3f ? K_RIGHT : b->vx > want + 0.3f ? K_LEFT : 0;
}

int main(int argc, char **argv) {
  int level = 0, frames = 600, fuzz = -1, draw = 0, trace = 0, hard = 0, autop = 0;
  const char *out = "build/shots";
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--level") && i + 1 < argc) level = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--hard")) hard = 1;
    else if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--keys") && i + 1 < argc) parse_keys(argv[++i]);
    else if (!strcmp(argv[i], "--shots") && i + 1 < argc) {
      const char *s = argv[++i];
      if (!strncmp(s, "every:", 6)) every = atoi(s + 6);
      else {
        static char buf[65536];
        snprintf(buf, sizeof buf, "%s", s);
        for (char *t = strtok(buf, ","); t; t = strtok(NULL, ",")) shots[nshots++] = atoi(t);
      }
    } else if (!strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
    else if (!strcmp(argv[i], "--saves") && i + 1 < argc) host_save_dir(argv[++i]);
    else if (!strcmp(argv[i], "--auto")) autop = 1;
    else if (!strcmp(argv[i], "--fuzz") && i + 1 < argc) fuzz = atoi(argv[++i]), rng = (uint32_t)fuzz * 2654435761u + 1;
    else if (!strcmp(argv[i], "--draw")) draw = 1;
    else if (!strcmp(argv[i], "--trace") && i + 1 < argc) trace = atoi(argv[++i]);
  }
  static uint16_t band[SCREEN_W * BAND];
  gfx_init(band);
  game_init();
  if (level) {
    if (hard) save.unlocked[0] = 11;
    level_load(level, hard);
    g.state = S_PLAY;
    save.hard = (uint8_t)hard;
  }
  uint32_t fk = 0;
  int ended = 0;
  for (int f = 0; f < frames && g.running; f++) {
    uint32_t k = 0;
    for (int i = 0; i < nholds; i++)
      if (f >= holds[i].a && f <= holds[i].b) k |= holds[i].k;
    if (autop) k |= autoplay();
    if (fuzz >= 0) {
      if (f % 6 == 0) fk = rnd() & (K_LEFT | K_RIGHT | K_UP | K_DOWN | K_OK | (rnd() % 8 == 0 ? K_BACK : 0));
      k |= fk;
    }
    host_keys = k;
    host_time += 17;
    if (k & K_HOME) {
      ended = 1;
      break;
    }
    game_tick(k);
    int want = every ? f % every == 0 : 0;
    for (int i = 0; i < nshots; i++) want |= shots[i] == f;
    if (want || (draw && f % 2 == 0)) game_draw();
    if (want) {
      char p[512];
      snprintf(p, sizeof p, "%s/shot_%05d.ppm", out, f);
      host_shot(p);
    }
    if (trace && f % trace == 0 && L.ball >= 0)
      printf("f %d state %d ball %.1f %.1f v %.2f %.2f R %.0f taken %d nc %d\n", f, g.state, W.b[L.ball].x / PX_M,
             W.b[L.ball].y / PX_M, W.b[L.ball].vx, W.b[L.ball].vy, L.R, L.taken, W.nc);
  }
  (void)ended;
  save_write();
  printf("END state %d level %d hard %d taken %d complete %d frames %d R %.0f\n", g.state, L.level, L.hard, L.taken,
         L.complete, L.frames, L.R);
  return 0;
}
