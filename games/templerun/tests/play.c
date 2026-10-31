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
 * --root F: at frame F the next pieces are plain (up to a corner) and the next one has a root across it; --calm F:
 * at frame F the last stumble is long ago; --plain F: from frame F on, the pieces ahead are plain (up to a corner).
 * --trace F prints the runner and the pieces under and ahead of it from frame F on. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
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

/* ground at a point, on any piece laid */
static bool ground_at(float x, float z) {
  int gx = (int)(x / TILE + (x >= 0 ? 0.5f : -0.5f)), gz = (int)(z / TILE + (z >= 0 ? 0.5f : -0.5f));
  for (int i = 0; i < NTILES; i++)
    if (tiles[i].alive && tiles[i].gx == gx && tiles[i].gz == gz && path_ground(&tiles[i], x, z)) return true;
  return false;
}

/* how far ahead the ground goes on, at lean l */
static float ground_ahead(float l, float upto) {
  int d = pl.dir;
  float fx = (float)DX[d], fz = (float)DZ[d], rx = fz, rz = -fx;
  const Tile *t = &tiles[path_root];
  float s = (pl.x - path_cx(t)) * rx + (pl.z - path_cz(t)) * rz;
  for (float a = 1; a <= upto; a += 2)
    if (!ground_at(pl.x + fx * a + rx * (l - s), pl.z + fz * a + rz * (l - s))) return a;
  return upto + 1;
}

static uint32_t bot(void) {
  static int wait, at_gx = 1 << 20, at_gz, side;
  if (pl.dead) return 0;
  uint32_t k = 0;
  int d = pl.dir;
  float fx = (float)DX[d], fz = (float)DZ[d];
  /* lean where the ground goes on farthest (narrow paths) */
  bool narrow = tiles[path_root].obst == OB_NARROW || (path_next(path_root, d) >= 0 && tiles[path_next(path_root, d)].obst == OB_NARROW);
  if (narrow) {
    float g0 = ground_ahead(0, 60), gl = ground_ahead(-6, 60), gr = ground_ahead(6, 60);
    if (g0 < 50 && (gl > g0 + 4 || gr > g0 + 4)) k |= gl >= gr ? K_LEAN_L : K_LEAN_R;
  }
  if (wait > 0) {
    wait--;
    return k;
  }
  /* corners and junctions: turn where the path goes on (straight on, if it does) */
  for (int i = path_root, n = 0; i >= 0 && n < 3; i = path_next(i, d), n++) {
    const Tile *t = &tiles[i];
    if (!path_is_turn(t) || t->turned) continue;
    float a = (path_cx(t) - pl.x) * fx + (path_cz(t) - pl.z) * fz;
    if (a > pl.v * 0.4f || a < -6) break;
    if (path_opens(t, d)) break;
    bool l = path_opens(t, (d + 3) & 3), r = path_opens(t, (d + 1) & 3);
    if (at_gx != t->gx || at_gz != t->gz) at_gx = t->gx, at_gz = t->gz, side = l && r ? (rand() & 1 ? 1 : -1) : l ? -1 : 1;
    wait = 10;
    return k | (side < 0 ? K_LEFT : K_RIGHT);
  }
  /* obstacles: jump the roots and trunks, slide under branches and fire */
  for (int i = path_root, n = 0; i >= 0 && n < 2; i = path_next(i, d), n++) {
    const Tile *t = &tiles[i];
    for (int o = 0; o < floor_nobstacles; o++) {
      const FloorObstacle *ob = &floor_obstacles[o];
      if (ob->piece != t->mesh || ob->height <= t->y + 0.01f) continue;
      float lo = 1e9f, hi = -1e9f;
      for (int p = 0; p < ob->n; p++) {
        float a = (path_cx(t) + ob->p[p][0] * 0.25f - pl.x) * fx + (path_cz(t) + ob->p[p][1] * 0.25f - pl.z) * fz;
        lo = a < lo ? a : lo, hi = a > hi ? a : hi;
      }
      if (hi < 0) continue;
      if (ob->flags & FO_SLIDE) {
        float len = pl.v * 0.007f > 0.75f ? 0.75f : pl.v * 0.007f;
        if (lo < pl.v * len * 0.5f && !pl.sliding && !pl.jumping) {
          wait = 6;
          return k | K_DOWN;
        }
      } else if (lo < 4 + pl.v * 0.06f && pl.on_ground && !pl.stumbling) {
        wait = 12;
        return k | K_UP;
      }
    }
  }
  /* gaps, and steps up: jump at the edge */
  float lean = (k & K_LEAN_L) ? -6 : (k & K_LEAN_R) ? 6 : 0;
  float edge = ground_ahead(lean, 40);
  if (edge < 4 + pl.v * 0.05f && pl.on_ground && !pl.stumbling) {
    int next = path_next(path_root, d);
    const Tile *t = &tiles[path_root];
    bool corner = path_is_turn(t) && !path_opens(t, d);
    if (!corner && !(next >= 0 && tiles[next].y < t->y - 1 && tiles[next].obst != OB_HOLE)) {
      wait = 12;
      return k | K_UP;
    }
  }
  return k;
}

/* the test's own path: plain straight pieces ahead (up to a corner), and a root across the next one */
static void plain_ahead(bool root) {
  int d = pl.dir, i = path_next(path_root, d);
  for (int n = 0; i >= 0 && n < 8; i = path_next(i, d), n++) {
    Tile *t = &tiles[i];
    if (path_is_turn(t) || t->obst == OB_LEVEL || t->level != TH_TEMPLE) break;
    bool v = d == D_N || d == D_S;
    t->type = v ? PT_STRAIGHT_V : PT_STRAIGHT_H;
    t->mesh = v ? M_templeStraightVert : M_templeStraightHorz;
    t->obst = OB_NONE, t->has_obstacles = 0;
    if (root && n == 0) {
      t->mesh = v ? M_templeStraightTreeTripLeftVert : M_templeStraightTreeTripUpHorz;
      t->obst = OB_STUMBLE, t->has_obstacles = 1;
    }
  }
}

int main(int argc, char **argv) {
  int frames = 600, draw_every = 1, bot_from = -1, fuzz_from = -1, coins = -1, up = -1, use = -1, raw_from = 0, raw_to = -1;
  const char *shots = "", *out = "build/shots", *saves = "build/host-saves", *raw = NULL;
  bool stats = false, all = false, god = false, invis = false;
  int roots[8], nroots = 0, calm = -1, trace = -1, plain = -1;
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
    else if (!strcmp(a, "--trace")) trace = atoi(v), i++;
    else if (!strcmp(a, "--plain")) plain = atoi(v), i++;
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
      if (f == roots[i]) plain_ahead(true);
    if (plain >= 0 && f >= plain && f % 20 == 0) plain_ahead(false);
    if (invis && pl.invis < 1) pl.invis = 1;
    if (god) {
      if (pl.invis < 1) pl.invis = 1;
      pl.wings = 30;
    }
    if (trace >= 0 && f >= trace && f % 3 == 0) {
      const Tile *t = &tiles[path_root];
      int n = path_next(path_root, pl.dir);
      printf("%d: wish %d dir %d x %.1f z %.1f y %.1f ground %.1f over %d fall %d jump %d v %.0f | root %d type %d level %d mesh %d (%d,%d) | next type %d level %d mesh %d\n", f, pl.turn_wish, pl.dir, pl.x, pl.z, pl.y,
             pl.ground, pl.over, pl.falling, pl.jumping, pl.v, path_root, t->type, t->level, t->mesh, t->gx, t->gz, n >= 0 ? tiles[n].type : -1,
             n >= 0 ? tiles[n].level : -1, n >= 0 ? tiles[n].mesh : -1);
    }
    bool was_dead = pl.dead;
    app_step(k, k & ~prev);
    prev = k;
    if (pl.dead && !was_dead) {
      deaths++, last_death = pl.death;
      if (stats) printf("dead at frame %d: death %d dist %.0f piece %d type %d obstacle %d feet %.1f v %.0f jumping %d\n", f, pl.death, run.dist, tiles[path_root].mesh, tiles[path_root].type, tiles[path_root].obst, pl.y - pl.ground, pl.v, pl.jumping);
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
