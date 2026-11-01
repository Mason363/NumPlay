/* Plays Five Nights at Freddy's 2 on a computer, with scripted keys (or a player of its own), and saves frames.
 *
 *   play [--frames N] [--keys "10-40:left,50:ok,..."] [--shots 30,60|all|every:N] [--out DIR] [--saves DIR]
 *        [--night N] [--ai A,B,...] [--hour H] [--box B] [--step WHO:STEP] [--ready WHO] [--foxy-hall]
 *        [--custom A,...] [--stars S] [--seed S] [--bot] [--fuzz]
 *
 * Keys: left right up down ok back home del 0 to 9. "a-b:key" holds the key from frame (step) a to b, "a:key" for
 * one step. --night starts that night (its card first); --ai (the eleven: Toy Freddy, Toy Bonnie, Toy Chica,
 * Mangle, Balloon Boy, Withered Freddy, Bonnie, Chica, Foxy, Golden Freddy, the Puppet; -1 keeps one), --hour,
 * --box (the music box), --step (an animatronic's place on its path), --ready and --foxy-hall then set the night;
 * --custom the custom night's ten levels, --stars the stars won. --bot plays the night itself (it knows where
 * everyone is). --fuzz presses random keys (never Home). Frames are steps (60 a second); the screen is drawn every
 * second step. Screenshots are DIR/shot_<frame>.ppm. At the end it prints the state: "END scene ... night ...". */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "../src/game.h"
#include "../src/gfx.h"
#include "../src/res.h"

extern uint32_t host_keys, host_time;
void host_shot(const char *path);
void host_save_dir(const char *d);
extern const uint8_t fnaf2_data[];
const uint8_t *fnaf2_bin;

typedef struct { int a, b; uint32_t k; } Hold;
static Hold holds[4096];
static int nholds;

static uint32_t key_of(const char *s) {
  static const struct { const char *n; uint32_t k; } names[] = {
    {"left", K_LEFT}, {"right", K_RIGHT}, {"up", K_UP}, {"down", K_DOWN}, {"ok", K_OK}, {"back", K_BACK},
    {"home", K_HOME}, {"del", K_DEL}};
  for (unsigned i = 0; i < sizeof names / sizeof names[0]; i++)
    if (!strcmp(s, names[i].n)) return names[i].k;
  if (s[0] >= '0' && s[0] <= '9' && !s[1]) return K_DIGIT(s[0] - '0');
  fprintf(stderr, "unknown key %s\n", s);
  exit(2);
}

static void parse_keys(char *s) {
  for (char *t = strtok(s, ","); t; t = strtok(NULL, ",")) {
    char *colon = strchr(t, ':');
    if (!colon || nholds >= 4096) continue;
    *colon = 0;
    int a = atoi(t), b = a;
    char *dash = strchr(t, '-');
    if (dash) b = atoi(dash + 1);
    holds[nholds++] = (Hold){a, b, key_of(colon + 1)};
  }
}

/* ------------------------------------------------------------------ a player */
static bool at_last(int who) {
  int at = bot_at(who);
  return at == L_LSPOT || at == L_RSPOT || (at == L_HALL && (who == TF || who == WF)) ||
         (who == WB && at == L_LVENT) || (who == WC && at == L_RVENT);
}

static uint32_t bot(int f) {
  Night *n = &g.n;
  static int flash_until, next_flash, winding;
  if (g.scene != SC_NIGHT || g.paused) return 0;
  bool up = n->flip >= 1, down = n->flip <= 0 && !n->monitor;
  bool showing = n->occ_state == OCC_SHOWN || n->gf == GF_SHOWN;
  bool staying = n->occ_state == OCC_MASKED || n->occ_state == OCC_LEAVING || n->gf == GF_FADING;
  /* someone waiting at a vent or the hallway: the mask, unless the music box needs winding */
  bool threat = false;
  for (int k = 0; k < WALKERS; k++)
    if (at_last(k) && !(k == MANGLE && n->mangle_in) && !(k == BB && n->bb_in)) threat = true;
  bool need_wind = n->box < 900 || (winding && n->box < BOX_FULL - 20);
  static int up_since, flashed_after;
  if (n->monitor) {
    if (!up) return 0;
    if (!up_since) up_since = f;
    flashed_after = 0;
    /* never more than 3.5 s up: Foxy may have come to the hallway */
    if (n->cam != L_PRIZE && f - up_since < 210) return (f & 1) ? 0 : K_DIGIT(1);
    if (n->box < BOX_FULL - 20 && f - up_since < 210) {
      winding = 1;
      return K_UP;
    }
    winding = n->box < BOX_FULL - 20;
    return (f & 1) ? 0 : K_OK;
  }
  up_since = 0;
  if (!down) return 0;
  if (showing && !n->mask) return K_DOWN;
  if (staying) return 0;
  if (n->mask) {
    if (threat && n->box > 400 && !(n->foxy_hall && !n->foxy_off)) return 0;
    return (f & 1) ? 0 : K_DOWN;   /* off */
  }
  if (n->mask_anim > 0) return 0;
  /* the hallway: a short flash every 2 s, quick ones while Foxy is there until he's off (never at Golden
     Freddy's head) */
  if (!flashed_after && n->gf != GF_HALL) next_flash = f, flashed_after = 1;   /* a look down the hall */
  if (n->ai[FOXY] >= 0 && n->gf != GF_HALL && n->battery < battery_frames(n->night) - 30) {
    if (f < flash_until) return K_DIGIT(5);
    if (f >= next_flash) {
      bool foxy_there = n->foxy_hall && !n->foxy_off;
      flash_until = f + 8;
      next_flash = f + (foxy_there ? 20 : 120);
      return K_DIGIT(5);
    }
  }
  if (threat && n->box > 400 && n->occ_state == OCC_NONE) return (f & 1) ? 0 : K_DOWN;
  if (need_wind || n->foxy_off) return (f & 1) ? 0 : K_OK;
  return 0;
}

/* random keys: mostly the ones a night uses, now and then Back, never Home */
static uint32_t fuzz(void) {
  static uint32_t keep;
  static int left;
  if (left-- > 0) return keep;
  static const uint32_t keys[] = {K_LEFT, K_RIGHT, K_UP, K_DOWN, K_OK, K_BACK, K_DEL, K_DIGIT(0), K_DIGIT(1),
                                  K_DIGIT(2), K_DIGIT(3), K_DIGIT(4), K_DIGIT(5), K_DIGIT(6), K_DIGIT(7),
                                  K_DIGIT(8), K_DIGIT(9), 0, 0, 0};
  uint32_t r = rnd();
  keep = keys[r % 20];
  if (keep == K_BACK && (r >> 8) % 4) keep = K_OK;
  left = (int)((r >> 12) % 40);
  return keep;
}

static int parse_list(const char *s, int *v, int n) {
  int k = 0;
  while (*s && k < n) {
    v[k++] = atoi(s);
    while (*s && *s != ',') s++;
    if (*s) s++;
  }
  return k;
}

int main(int argc, char **argv) {
  int frames = 600, night = 0, hour = -1, ai[BOTS], custom[10], ncustom = 0, stars = -1, box = -1, ready = -1;
  int step_who = -1, step_at = 0;
  bool use_bot = false, use_fuzz = false, all = false, foxy_hall = false, trace = false;
  int every = 0;
  for (int k = 0; k < BOTS; k++) ai[k] = -100;
  const char *out = "build/shots", *saves = "build/host-saves";
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
    else if (!strcmp(a, "--saves")) saves = argv[++i];
    else if (!strcmp(a, "--night")) night = atoi(argv[++i]);
    else if (!strcmp(a, "--hour")) hour = atoi(argv[++i]);
    else if (!strcmp(a, "--box")) box = atoi(argv[++i]);
    else if (!strcmp(a, "--ready")) ready = atoi(argv[++i]);
    else if (!strcmp(a, "--foxy-hall")) foxy_hall = true;
    else if (!strcmp(a, "--ai")) {
      int v[BOTS], n = parse_list(argv[++i], v, BOTS);
      for (int k = 0; k < n; k++)
        if (v[k] != -1) ai[k] = v[k];
    } else if (!strcmp(a, "--custom")) ncustom = parse_list(argv[++i], custom, 10);
    else if (!strcmp(a, "--step")) sscanf(argv[++i], "%d:%d", &step_who, &step_at);
    else if (!strcmp(a, "--seed")) {
      unsigned s = (unsigned)atoi(argv[++i]);
      for (unsigned k = 0; k < s % 1000; k++) rnd();
    } else if (!strcmp(a, "--bot")) use_bot = true;
    else if (!strcmp(a, "--fuzz")) use_fuzz = true;
    else if (!strcmp(a, "--trace")) trace = true;
    else if (!strcmp(a, "--stars")) stars = atoi(argv[++i]);
    else {
      fprintf(stderr, "unknown argument %s\n", a);
      return 2;
    }
  }
  mkdir(out, 0755);
  mkdir(saves, 0755);
  host_save_dir(saves);
  static uint16_t band[SCREEN_W * BAND_H];
  gfx_set_band_memory(band);
  fnaf2_bin = fnaf2_data;
  host_time = 1000;
  game_init();
  if (stars >= 0) g.stars = (uint8_t)stars;
  for (int k = 0; k < ncustom; k++) g.custom[k] = (int8_t)custom[k];
  if (night) start_night(night);
  bool set = false;
  int f;
  for (f = 0; f < frames && g.running; f++) {
    if (!set && g.scene == SC_NIGHT && !g.paused) {   /* the night as asked */
      set = true;
      for (int k = 0; k < BOTS; k++)
        if (ai[k] != -100) g.n.ai[k] = (int8_t)ai[k];
      if (hour >= 0) g.n.hour = (uint8_t)hour, g.n.t = hour * HOUR + 1;
      if (box >= 0) g.n.box = (float)box;
      if (step_who >= 0 && step_who < WALKERS) g.n.step[step_who] = (uint8_t)step_at;
      if (ready >= 0 && ready < WALKERS) g.n.ready[ready] = 1;
      if (foxy_hall) g.n.foxy_hall = 1;
    }
    uint32_t k = 0;
    for (int i = 0; i < nholds; i++)
      if (f >= holds[i].a && f <= holds[i].b) k |= holds[i].k;
    if (use_bot && !k) k = bot(f);
    if (use_fuzz && !k) k = fuzz();
    host_keys = k;
    int was = g.scene;
    game_step(k);
    if (trace && g.scene == SC_NIGHT && f % 30 == 0)
      printf("T %.1f foxy %d d %.1f got %d off %d fl %d | mask %d mon %d flip %.2f box %d occ %d/%d threat-ready %d%d%d%d%d%d%d%d cam %d batt %d hl %d bb %d\n",
             g.n.t, g.n.foxy_hall, g.n.foxy_d, g.n.foxy_got, g.n.foxy_off, g.n.foxy_flashes, g.n.mask, g.n.monitor,
             g.n.flip, (int)g.n.box, g.n.occ == 0xFF ? -1 : g.n.occ, g.n.occ_state, g.n.ready[0], g.n.ready[1],
             g.n.ready[2], g.n.ready[3], g.n.ready[4], g.n.ready[5], g.n.ready[6], g.n.ready[7], g.n.cam, (int)g.n.battery, g.n.hall_light, g.n.bb_in);
    if (g.scene != was && (g.scene == SC_SIX || g.scene == SC_SCARE))
      printf("EVENT %s night %d hour %d box %d t %.1f who %d\n", g.scene == SC_SIX ? "six" : "scare", g.n.night,
             g.n.hour, (int)g.n.box, g.n.t, g.scare);
    host_time += (f % 3 == 2) ? 17 : 16;
    bool shot = all || (every && f % every == 0);
    for (int i = 0; i < nshots; i++) shot |= shots[i] == f;
    if (g.running && (shot || !(f & 1))) {
      game_draw();
      if (shot) {
        char p[512];
        snprintf(p, sizeof p, "%s/shot_%05d.ppm", out, f);
        host_shot(p);
      }
    }
  }
  printf("END scene %d night %d hour %d box %d t %.1f frames %d running %d paused %d reached %d stars %d "
         "steps %d,%d,%d,%d,%d,%d,%d,%d occ %d occ_state %d mangle_in %d bb_in %d foxy_hall %d gf %d puppet %d "
         "battery %d mask %d monitor %d cam %d keys_seen %d ai %d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d scare %d\n",
         g.scene, g.n.night, g.n.hour, (int)g.n.box, g.n.t, f, g.running, g.paused, g.reached, g.stars, g.n.step[0],
         g.n.step[1], g.n.step[2], g.n.step[3], g.n.step[4], g.n.step[5], g.n.step[6], g.n.step[7],
         g.n.occ == 0xFF ? -1 : g.n.occ, g.n.occ_state, g.n.mangle_in, g.n.bb_in, g.n.foxy_hall, g.n.gf, g.n.puppet,
         (int)g.n.battery, g.n.mask, g.n.monitor, g.n.cam, g.keys_seen, g.n.ai[0], g.n.ai[1], g.n.ai[2], g.n.ai[3],
         g.n.ai[4], g.n.ai[5], g.n.ai[6], g.n.ai[7], g.n.ai[8], g.n.ai[9], g.n.ai[10], g.scare);
  return 0;
}
