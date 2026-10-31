/* Plays Temple Run on a computer, with scripted keys, and saves frames.
 *
 *   play [--frames N] [--keys "10-40:right,50:ok,..."] [--shots 30,60|all] [--out DIR] [--saves DIR] [--seed N]
 *        [--bot FROM] [--fuzz FROM] [--coins N] [--up N] [--use N] [--god] [--draw-every N] [--raw DIR:FROM-TO]
 *        [--invis] [--root F]... [--calm F] [--stats]
 *
 * Keys: left right up down ok back one three home lean_l lean_r shift. "a-b:key" holds the key from frame a to b,
 * "a:key" for one frame (frames are the game's steps, 60 a second). --bot plays the run from frame FROM on: it
 * reads the path ahead, turns at the corners, jumps, slides and leans. --fuzz presses random keys from FROM on
 * (Home never). --god: nothing hurts (invisible, and the wings always on). --up N: every upgrade at level N;
 * --use N: N of each thing to use. Screenshots are DIR/shot_<frame>.ppm; --raw keeps frames FROM..TO as raw
 * RGB565 files for tools/record.py --frames. --invis: always invisible (obstacles pass, gaps and corners don't).
 * --root F: at frame F the next tiles are plain (up to a corner) and the next one has a root across it; --calm F:
 * at frame F the last stumble is long ago. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "../src/app.h"
#include "../src/game.h"
#include "../src/r3d.h"
#include "../src/save.h"

extern uint32_t host_keys, host_time, host_seed;
extern uint16_t host_fb[SCREEN_W * SCREEN_H];
void host_shot(const char *path);
void host_save_dir(const char *d);

typedef struct { int a, b; uint32_t k; } Hold;
static Hold holds[4096];
static int nholds;

static uint32_t key_of(const char *s) {
  static const struct { const char *n; uint32_t k; } names[] = {
    {"left", K_LEFT}, {"right", K_RIGHT}, {"up", K_UP}, {"down", K_DOWN}, {"ok", K_OK}, {"back", K_BACK},
    {"one", K_ONE}, {"three", K_THREE}, {"home", K_HOME}, {"lean_l", K_LEAN_L}, {"lean_r", K_LEAN_R}, {"shift", K_SHIFT},
  };
  for (unsigned i = 0; i < sizeof names / sizeof names[0]; i++)
    if (!strcmp(s, names[i].n)) return names[i].k;
  fprintf(stderr, "play: unknown key %s\n", s);
  exit(2);
}

static void parse_keys(const char *spec) {
  static char buf[65536];
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

/* ---------------------------------------------------------------- the bot */
static const int DX[4] = {0, 1, 0, -1}, DZ[4] = {1, 0, -1, 0};

static uint32_t bot(void) {
  static int wait, side;
  if (pl.dead) return 0;
  uint32_t k = 0;
  float fx = (float)DX[pl.dir], fz = (float)DZ[pl.dir];
  /* lean along narrow paths (and toward coins off the middle) */
  for (int i = pl.tile; i < path_count && i <= pl.tile + 1; i++) {
    const Tile *t = tile_at(i);
    if (t->dir != pl.dir) break;
    if (t->kind == PK_NARROW_L) k |= K_LEAN_L;
    if (t->kind == PK_NARROW_R) k |= K_LEAN_R;
    if (!k && i == pl.tile && t->coins && (t->coin == COIN_LEFT || t->coin == COIN_RIGHT)) k |= t->coin == COIN_LEFT ? K_LEAN_L : K_LEAN_R;
  }
  if (wait > 0) {
    wait--;
    return k;
  }
  for (int i = pl.tile; i < path_count && i <= pl.tile + 3; i++) {
    const Tile *t = tile_at(i);
    float d = (game_tile_cx(t) - pl.x) * fx + (game_tile_cz(t) - pl.z) * fz;
    if (t->dir != pl.dir || d < -4) continue;
    if (t->kind == PK_CORNER_L || t->kind == PK_CORNER_R || t->kind == PK_T) {
      if (d < pl.v * 0.4f && !(t->hit & 2)) {
        side = t->kind == PK_CORNER_L ? -1 : t->kind == PK_CORNER_R ? 1 : (side ? side : 1);
        wait = 10;
        return k | (side < 0 ? K_LEFT : K_RIGHT);
      }
      break;
    }
    /* a jump just before it: in the air long enough for two in a row */
    float edge = t->kind == PK_HOLE ? 10 : t->kind == PK_HOLE_LONG ? 17 : t->ob == OB_TREE ? 3.6f : 3.2f;
    bool jumpy = t->ob == OB_ROOT || t->ob == OB_TREE || t->ob == OB_FIRE || t->kind == PK_HOLE || t->kind == PK_HOLE_LONG;
    if (jumpy && d - edge < 4 + pl.v * 0.06f && d - edge > -2 && pl.on_ground && !pl.stumbling) {
      wait = 12;
      return k | K_UP;
    }
    if (t->ob == OB_BRANCH && !pl.sliding) {
      float len = pl.v * 0.007f;
      if (len > 0.75f) len = 0.75f;
      if (d < pl.v * len * 0.5f + 3 && d > 4) {
        wait = 6;
        return k | K_DOWN;
      }
    }
  }
  return k;
}

int main(int argc, char **argv) {
  int frames = 600, draw_every = 1, bot_from = -1, fuzz_from = -1, coins = -1, up = -1, use = -1, raw_from = 0, raw_to = -1;
  const char *shots = "", *out = "build/shots", *saves = "build/host-saves", *raw = NULL;
  bool stats = false, all = false, god = false, invis = false;
  int roots[8], nroots = 0, calm = -1;
  static char rawdir[512];
  for (int i = 1; i < argc; i++) {
    const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : "";
    if (!strcmp(a, "--frames")) frames = atoi(v), i++;
    else if (!strcmp(a, "--keys")) parse_keys(v), i++;
    else if (!strcmp(a, "--shots")) shots = v, all = !strcmp(v, "all"), i++;
    else if (!strcmp(a, "--out")) out = v, i++;
    else if (!strcmp(a, "--saves")) saves = v, i++;
    else if (!strcmp(a, "--seed")) host_seed = (uint32_t)strtoul(v, NULL, 10) * 2654435761u + 1, i++;
    else if (!strcmp(a, "--bot")) bot_from = atoi(v), i++;
    else if (!strcmp(a, "--fuzz")) fuzz_from = atoi(v), i++;
    else if (!strcmp(a, "--coins")) coins = atoi(v), i++;
    else if (!strcmp(a, "--up")) up = atoi(v), i++;
    else if (!strcmp(a, "--use")) use = atoi(v), i++;
    else if (!strcmp(a, "--draw-every")) draw_every = atoi(v), i++;
    else if (!strcmp(a, "--raw")) {
      snprintf(rawdir, sizeof rawdir, "%s", v);
      char *c = strrchr(rawdir, ':');
      if (c) *c = 0, sscanf(c + 1, "%d-%d", &raw_from, &raw_to);
      raw = rawdir, i++;
    } else if (!strcmp(a, "--god")) god = true;
    else if (!strcmp(a, "--invis")) invis = true;
    else if (!strcmp(a, "--root") && nroots < 8) roots[nroots++] = atoi(v), i++;
    else if (!strcmp(a, "--calm")) calm = atoi(v), i++;
    else if (!strcmp(a, "--stats")) stats = true;
  }
  if (draw_every < 1) draw_every = 1;
  mkdir("build", 0755);
  mkdir(saves, 0755);
  mkdir(out, 0755);
  host_save_dir(saves);
  static uint16_t band[SCREEN_W * BAND_H];
  r_set_band_memory(band);
  static uint64_t work[400000 / 8];
  if (r_work_size() > sizeof work) {
    fprintf(stderr, "play: the renderer wants %u bytes\n", r_work_size());
    return 1;
  }
  r_init(work);
  app_init();
  if (coins >= 0 || up >= 0 || use >= 0) {
    if (coins >= 0) pg.coins = (uint32_t)coins;
    for (int i = 0; i < UP_COUNT && up >= 0; i++) pg.up[i] = (uint8_t)up;
    for (int i = 0; i < USE_COUNT && use >= 0; i++) pg.use[i] = (uint16_t)use;
    save_write();
  }
  uint32_t prev = 0, fuzz = 99991;
  int f, max_polys = 0, deaths = 0, last_death = 0;
  for (f = 0; f < frames && app_running; f++) {
    uint32_t k = 0;
    for (int i = 0; i < nholds; i++)
      if (f >= holds[i].a && f < holds[i].b) k |= holds[i].k;
    if (bot_from >= 0 && f >= bot_from) k |= bot();
    if (fuzz_from >= 0 && f >= fuzz_from) {
      fuzz = fuzz * 1103515245u + 12345u;
      static const uint32_t pick[] = {K_LEFT, K_RIGHT, K_UP, K_DOWN, K_OK, K_BACK, K_ONE, K_THREE, K_LEAN_L, K_LEAN_R, K_SHIFT, 0, 0, 0};
      if ((fuzz >> 16) % 3 == 0) k |= pick[(fuzz >> 20) % 14];
    }
    if (f == calm) pl.stumble_timer = 10;
    for (int i = 0; i < nroots; i++)
      if (f == roots[i]) {
        for (int j = pl.tile + 1; j < path_count && j <= pl.tile + 8; j++) {
          Tile *t = tile_at(j);
          if (t->kind == PK_CORNER_L || t->kind == PK_CORNER_R || t->kind == PK_T) break;
          if (t->kind != PK_STEP_UP && t->kind != PK_STEP_DOWN) t->kind = PK_STRAIGHT;
          t->ob = OB_NONE, t->hit = 0;
        }
        tile_at(pl.tile + 1)->ob = OB_ROOT;
      }
    if (invis && pl.invis < 1) pl.invis = 1;
    if (god) {
      if (pl.invis < 1) pl.invis = 1;
      pl.wings = 30;
    }
    bool was_dead = pl.dead;
    app_step(k, k & ~prev);
    prev = k;
    if (pl.dead && !was_dead) {
      deaths++, last_death = pl.death;
      if (stats) printf("dead at frame %d: death %d dist %.0f tile kind %d ob %d feet %.1f v %.0f vy %.0f jumping %d ground %d\n", f, pl.death, run.dist, tile_at(pl.tile)->kind, tile_at(pl.tile)->ob, pl.y - tile_at(pl.tile)->y, pl.v, pl.vy, pl.jumping, pl.on_ground);
    }
    host_time += (f % 3 == 2) ? 17 : 16;
    char name[64], list[8192];
    snprintf(name, sizeof name, ",%d,", f);
    snprintf(list, sizeof list, ",%s,", shots);
    bool shot = all || strstr(list, name);
    bool keep = raw && f >= raw_from && f <= raw_to && f % 4 == 0;
    if (shot || keep || f % draw_every == 0) {
      app_draw();
      if (r_count() > max_polys) max_polys = r_count();
    }
    if (keep) {
      char p[600];
      mkdir(raw, 0755);
      snprintf(p, sizeof p, "%s/%06d_%06d.raw", raw, f, (f - raw_from) * 50 / 3);
      FILE *o = fopen(p, "wb");
      if (o) fwrite(host_fb, 2, SCREEN_W * SCREEN_H, o), fclose(o);
    }
    if (shot) {
      char p[512];
      snprintf(p, sizeof p, "%s/shot_%05d.ppm", out, f);
      host_shot(p);
    }
  }
  if (stats)
    printf("frames %d running %d dead %d death %d deaths %d dist %.0f score %u coins %u polys %d total_coins %u best %u "
           "runs %u mult %u saved %d turns %d stumbles %d\n",
           f, app_running, pl.dead, last_death, deaths, run.dist, run.score, run.coins, max_polys, pg.coins, pg.best, pg.runs,
           pg.multiplier, run.saved, run.turns, run.stumbles);
  return 0;
}
