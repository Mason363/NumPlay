/* Plays Subway Surfers on a computer, with scripted keys, and saves frames.
 *
 *   play [--frames N] [--keys "10-40:right,50:ok,..."] [--shots 30,60|all] [--out DIR] [--saves DIR]
 *        [--seed N] [--bot FROM] [--coins N] [--keys-owned N] [--draw-every N] [--power FRAME:KIND] [--god] [--when roof|tunnel|covered] [--stats]
 *   play --view WHO,ANIM,T,YAW,DIST[,HEIGHT] --out DIR   (one person, or the dog (5), alone: DIR/view.ppm)
 *
 * Keys: left right up down ok back one three home. "a-b:key" holds the key from frame a to b, "a:key" for one
 * frame (frames are the game's steps, 60 a second). --bot plays the run from frame FROM on: it reads the
 * tracks ahead and changes lanes, jumps and rolls (it is no expert: it stumbles and crashes now and then).
 * Screenshots are DIR/shot_<frame>.ppm. --raw DIR:FROM-TO keeps the frames drawn from FROM to TO as raw RGB565
 * files for tools/record.py --frames (GIFs for the documentation). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "../src/actors.h"
#include "../src/app.h"
#include "../src/data.h"
#include "../src/game.h"
#include "../src/r3d.h"
#include "../src/save.h"

extern uint32_t host_keys, host_time, host_seed;
extern uint16_t host_fb[SCREEN_W * SCREEN_H];
void host_shot(const char *path);
void host_save_dir(const char *d);

typedef struct { int a, b; uint32_t k; } Hold;
static int power_at[8], power_kind[8], npower;
static Hold holds[4096];
static int nholds;

static uint32_t key_of(const char *s) {
  static const struct { const char *n; uint32_t k; } names[] = {
    {"left", K_LEFT}, {"right", K_RIGHT}, {"up", K_UP}, {"down", K_DOWN}, {"ok", K_OK}, {"back", K_BACK},
    {"one", K_ONE}, {"three", K_THREE}, {"home", K_HOME},
  };
  for (unsigned i = 0; i < sizeof names / sizeof names[0]; i++)
    if (!strcmp(s, names[i].n)) return names[i].k;
  fprintf(stderr, "play: unknown key %s\n", s);
  exit(2);
}

static void parse_keys(const char *spec) {
  char buf[65536];
  snprintf(buf, sizeof buf, "%s", spec);
  for (char *t = strtok(buf, ","); t && nholds < 4096; t = strtok(NULL, ",")) {
    char *colon = strchr(t, ':');
    if (!colon) continue;
    *colon = 0;
    int a = atoi(t), b = a + 1;
    char *dash = strchr(t, '-');
    if (dash) b = atoi(dash + 1) + 1;
    holds[nholds++] = (Hold){a, b, key_of(colon + 1)};
  }
}

#include "bot.h"

/* ---------------------------------------------------------------- one person, alone */
static void view_bg(void) {
  for (int y = r_y0; y < r_y1; y++)
    for (int x = 0; x < SCREEN_W; x++) r_col[(y - r_y0) * SCREEN_W + x] = (uint16_t)(0x8C51 + (y >> 4) * 0x0020);
}

static void view(const char *spec, const char *out) {
  int who = 0;
  char anim[16] = "stand";
  float t = 0, yaw = 3.14159f, dist = 3.2f, height = 0.95f;
  sscanf(spec, "%d,%15[a-z],%f,%f,%f,%f", &who, anim, &t, &yaw, &dist, &height);
  static uint16_t band[SCREEN_W * BAND_H];
  r_set_band_memory(band);
  static uint64_t work[200000 / 8];
  if (r_work_size() > sizeof work) abort();
  r_init(work);
  r_begin();
  r_camera(0, height, -dist, 0.05f, 300, 160, 120);
  /* a clip by its name: the runner's, the Inspector's (who 4) or the dog's (who 5) */
  static const struct { const char *n; const Clip *c[3]; } clips[] = {
    {"run", {&clip_runner_run, &clip_inspector_run, &clip_dog_run}}, {"jump", {&clip_runner_jump, &clip_inspector_jump, &clip_dog_jump}},
    {"hang", {&clip_runner_hang, &clip_inspector_jump, &clip_dog_jump}}, {"roll", {&clip_runner_roll, &clip_inspector_roll, &clip_dog_run}},
    {"board", {&clip_runner_board, &clip_inspector_run, &clip_dog_run}}, {"fly", {&clip_runner_fly, &clip_inspector_run, &clip_dog_run}},
    {"death", {&clip_runner_death, &clip_inspector_catch, &clip_dog_catch}}, {"catch", {&clip_runner_death, &clip_inspector_catch, &clip_dog_catch}},
    {"stand", {&clip_runner_idle, &clip_inspector_whistle, &clip_dog_run}},
  };
  const Clip *c = clips[0].c[0];
  for (unsigned i = 0; i < sizeof clips / sizeof clips[0]; i++)
    if (!strcmp(anim, clips[i].n)) c = clips[i].c[who < 4 ? 0 : who - 3];
  Anim an = {0};
  anim_play(&an, c, t, 0);
  actor_draw(who, &an, (V3){0, 0, 0}, yaw, 0, 1);
  r_render(view_bg, NULL);
  char path[512];
  snprintf(path, sizeof path, "%s/view.ppm", out);
  host_shot(path);
  printf("polys %d\n", r_count());
}

int main(int argc, char **argv) {
  int frames = 600, draw_every = 1, bot_from = -1;
  const char *out = "build/shots", *shots = "", *saves = "build/host-saves";
  long coins = -1, keys_owned = -1;
  bool stats = false, god = false;
  const char *raw = NULL, *when = NULL;   /* when: a screenshot the first time the runner is on a roof, in a tunnel */
  int when_n = 0;
  int raw_from = 0, raw_to = -1;   /* god: nothing hurts (long runs for tests) */
  const char *view_spec = NULL;
  for (int i = 1; i < argc; i++) {
    const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : "";
    if (!strcmp(a, "--frames")) frames = atoi(v), i++;
    else if (!strcmp(a, "--keys")) parse_keys(v), i++;
    else if (!strcmp(a, "--shots")) shots = v, i++;
    else if (!strcmp(a, "--out")) out = v, i++;
    else if (!strcmp(a, "--saves")) saves = v, i++;
    else if (!strcmp(a, "--seed")) host_seed = (uint32_t)strtoul(v, NULL, 0), i++;
    else if (!strcmp(a, "--bot")) bot_from = atoi(v), i++;
    else if (!strcmp(a, "--coins")) coins = atol(v), i++;
    else if (!strcmp(a, "--keys-owned")) keys_owned = atol(v), i++;
    else if (!strcmp(a, "--draw-every")) draw_every = atoi(v), i++;
    else if (!strcmp(a, "--stats")) stats = true;
    else if (!strcmp(a, "--holes")) {
      extern int r_show_holes;
      r_show_holes = 1;
    }
    else if (!strcmp(a, "--god")) god = true;
    else if (!strcmp(a, "--when")) when = v, i++;
    else if (!strcmp(a, "--view")) view_spec = v, i++;
    else if (!strcmp(a, "--raw")) {
      static char dir[256];
      snprintf(dir, sizeof dir, "%s", v);
      char *c = strrchr(dir, ':');
      if (c) *c = 0, sscanf(c + 1, "%d-%d", &raw_from, &raw_to);
      raw = dir, i++;
    }
    else if (!strcmp(a, "--power") && npower < 8) {
      /* FRAME:KIND, KIND a power-up (0 jetpack, 1 sneakers, 2 magnet, 3 2x) */
      power_at[npower] = atoi(v), power_kind[npower] = atoi(strchr(v, ':') ? strchr(v, ':') + 1 : "0"), npower++, i++;
    }
    else {
      fprintf(stderr, "play: unknown option %s\n", a);
      return 2;
    }
  }
  mkdir("build", 0755);
  mkdir(out, 0755);
  if (view_spec) {
    view(view_spec, out);
    return 0;
  }
  mkdir(saves, 0755);
  host_save_dir(saves);
  static uint16_t band[SCREEN_W * BAND_H];
  r_set_band_memory(band);
  static uint64_t work[200000 / 8];
  if (r_work_size() > sizeof work) abort();
  r_init(work);
  app_init();
  if (coins >= 0) pg.coins = (uint32_t)coins;
  if (keys_owned >= 0) pg.keys = (uint32_t)keys_owned;
  bool all = !strcmp(shots, "all");
  uint32_t prev = 0;
  int max_polys = 0, f = 0;
  float roof_over = -100;
  extern uint32_t perf_polys;
  for (f = 0; f < frames && app_running; f++) {
    uint32_t k = 0;
    for (int i = 0; i < nholds; i++)
      if (f >= holds[i].a && f < holds[i].b) k |= holds[i].k;
    if (bot_from >= 0 && f >= bot_from) k |= bot();
    host_keys = k;
    for (int i = 0; i < npower; i++)
      if (f == power_at[i]) {
        pl.power[power_kind[i]] = power_time(power_kind[i]);
        if (power_kind[i] == PW_JETPACK) pl.fly = 8, world_sky_coins(pl.z + 12, pl.z + 12 + pl.speed * 9);
      }
    if (god && pl.safe < 0.1f) pl.safe = 0.1f;
    bool was_dead = pl.dead;
    app_step(k, k & ~prev);
    prev = k;
    /* how far the runner's head went into a low roof (never: tests/check.py) */
    if (pl.fly <= 0 && !pl.dead && pl.y + 9.2f * U - world_roof(pl.z) > roof_over) roof_over = pl.y + 9.2f * U - world_roof(pl.z);
    if (stats && pl.dead && !was_dead) {
      /* what the runner ran into */
      printf("dead at frame %d: z %.1f x %.2f y %.2f lane %d from %d state %d roll %.2f chase %.2f\n", f, pl.z, pl.x, pl.y,
             pl.lane, pl.from_lane, pl.state, pl.roll, pl.chase);
      for (int i = 0; i < nobs; i++)
        if (obs[i].z1 > pl.z - 3 && obs[i].z0 < pl.z + 6)
          printf("  obstacle type %d lane %d z %.1f..%.1f v %.1f\n", obs[i].type, obs[i].lane, obs[i].z0, obs[i].z1, obs[i].v);
    }
    host_time += (f % 3 == 2) ? 17 : 16;
    char name[64];
    snprintf(name, sizeof name, ",%d,", f);
    char list[8192];
    snprintf(list, sizeof list, ",%s,", shots);
    bool shot = all || strstr(list, name);
    if (when && !pl.dead) {
      bool now = (!strcmp(when, "roof") && pl.on_roof) || (!strcmp(when, "tunnel") && world_tunnel(pl.z - 2) && world_tunnel(pl.z + 30)) ||
                 (!strcmp(when, "covered") && world_covered(pl.z + 10) && world_covered(pl.z + 40));
      when_n = now ? when_n + 1 : 0;
      if (when_n == 25) shot = true;
    }
    bool keep = raw && f >= raw_from && f <= raw_to && f % 4 == 0;   /* (15 frames a second) */
    if (shot || keep || f % draw_every == 0) {
      extern uint32_t r_stat[4], r_stat_rows;
      uint32_t before = r_stat[1] + r_stat[2] + r_stat[3] + r_stat[0] / 3 + r_stat_rows * 6;
      app_draw();
      if ((int)perf_polys > max_polys) max_polys = (int)perf_polys;
      uint32_t cost = r_stat[1] + r_stat[2] + r_stat[3] + r_stat[0] / 3 + r_stat_rows * 6 - before;
      static uint32_t worst[5], worst_f[5];
      for (int k = 0; k < 5; k++)
        if (cost > worst[k]) {
          for (int m = 4; m > k; m--) worst[m] = worst[m - 1], worst_f[m] = worst_f[m - 1];
          worst[k] = cost, worst_f[k] = (uint32_t)f;
          break;
        }
      if (stats && f == frames - 1)
        for (int k = 0; k < 5; k++) printf("worst frame %u: %u\n", worst_f[k], worst[k]);
    }
    if (keep) {
      char p[512];
      mkdir(raw, 0755);
      snprintf(p, sizeof p, "%s/%06d_%06d.raw", raw, f, (f - raw_from) * 50 / 3);
      FILE *o = fopen(p, "wb");
      if (o) fwrite(host_fb, 2, SCREEN_W * SCREEN_H, o), fclose(o);
    }
    if (shot) {
      char p[512];
      snprintf(p, sizeof p, "%s/shot_%05d.ppm", out, f);
      host_shot(p);
      /* and how many pixels were tried at each place (a grey level each) */
      extern uint16_t r_heat[SCREEN_W * SCREEN_H];
      snprintf(p, sizeof p, "%s/heat_%05d.pgm", out, f);
      FILE *h = fopen(p, "wb");
      if (h) {
        fprintf(h, "P5 %d %d 255\n", SCREEN_W, SCREEN_H);
        for (int i = 0; i < SCREEN_W * SCREEN_H; i++) fputc(r_heat[i] * 32 > 255 ? 255 : r_heat[i] * 32, h);
        fclose(h);
      }
    }
  }
  extern uint32_t r_stat[4], r_stat_tex[64];
  if (stats)
    for (int i = 0; i < 64; i++)
      if (r_stat_tex[i]) {
        extern uint32_t r_stat_texhole[64];
        printf("texture %d: %u holes %u\n", i, r_stat_tex[i] / (unsigned)(f / draw_every + 1), r_stat_texhole[i] / (unsigned)(f / draw_every + 1));
      }
  extern uint32_t r_stat_rows, r_stat_polys, r_stat_skips, r_stat_chunks, r_stat_written, r_stat_holes;
  if (stats) printf("holes %u chunks %u skipped %u written (clear) %u\n", r_stat_holes / (unsigned)(f / draw_every + 1), r_stat_chunks / (unsigned)(f / draw_every + 1), r_stat_skips / (unsigned)(f / draw_every + 1), r_stat_written / (unsigned)(f / draw_every + 1));
  if (stats) printf("rows a frame %u, polygon bands %u\n", r_stat_rows / (unsigned)(f / draw_every + 1), r_stat_polys / (unsigned)(f / draw_every + 1));
  if (stats) printf("pixels a frame: flat %u shaded %u textured %u perspective %u\n", r_stat[0] / (unsigned)(f / draw_every + 1),
                    r_stat[1] / (unsigned)(f / draw_every + 1), r_stat[2] / (unsigned)(f / draw_every + 1),
                    r_stat[3] / (unsigned)(f / draw_every + 1));
  if (stats)
    printf("frames %d running %d dead %d dist %.0f score %u coins %u multiplier %d polys %d total_coins %u "
           "best %u runs %u mset %u jumps %d rolls %d stumbles %d roof_over %.2f\n",
           f, app_running, pl.dead, run.dist, run.score, run.coins, run.multiplier, max_polys, pg.coins, pg.best,
           pg.runs, pg.mset, run.jumps, run.rolls, run.stumbles, roof_over);
  return 0;
}
