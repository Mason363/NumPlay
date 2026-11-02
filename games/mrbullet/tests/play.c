/* Mr Bullet on a computer: the tests (make test) and pictures.
 *
 *   build/play test DIR       physics checks, a bot through the first levels of every mode, every level played
 *                             with random keys, saves; pictures of a few levels into DIR
 *   build/play shot LEVEL OUT a picture of a level (PPM), after FRAMES steps with the aim at ANGLE (env) */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "draw.h"
#include "game.h"
#include "plat.h"
#include "res.h"
#include "save.h"
#include "ui.h"
#include "world.h"

extern long b2_heap_now, b2_heap_peak;

/* ------------------------------------------------------------------ the platform */
static uint16_t screen[SCREEN_W * SCREEN_H];
static uint32_t clock_ms;
static char dir[512] = "build";
uint32_t plat_keys(void) { return 0; }
uint32_t plat_millis(void) { return clock_ms; }
void plat_sleep(uint32_t ms) { clock_ms += ms; }
void plat_push(int x, int y, int w, int h, const uint16_t *px) {
  for (int r = 0; r < h; r++) memcpy(&screen[(y + r) * SCREEN_W + x], &px[r * w], (size_t)w * 2);
}
void np_push(int x, int y, int w, int h, const color_t *px) { plat_push(x, y, w, h, px); }
void *np_alloc(uint32_t size) { (void)size; return NULL; }   /* (gfx_init: not used) */
static uint8_t files[2][8192];
static uint32_t file_len[2];
static int file_of(const char *name) { return strcmp(name, MB_SAVE_NAME) ? 1 : 0; }
bool plat_save(const char *name, const void *data, uint32_t len) {
  int f = file_of(name);
  if (len > sizeof files[f]) return false;
  memcpy(files[f], data, len);
  file_len[f] = len;
  return true;
}
const uint8_t *plat_load(const char *name, uint32_t *len) {
  int f = file_of(name);
  if (!file_len[f]) return NULL;
  *len = file_len[f];
  return files[f];
}
void plat_begin(void) {}
int plat_end(void) { return 0; }

static uint64_t level_mem[LEVEL_MAX_BYTES / 8 + 1];
static uint64_t strip[SCREEN_W * STRIP_H * 2 / 8];
static uint64_t scratch[SAVE_SCRATCH / 8];

static void save_ppm(const char *path) {
  FILE *f = fopen(path, "wb");
  if (!f) return;
  fprintf(f, "P6\n%d %d\n255\n", SCREEN_W, SCREEN_H);
  for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
    uint16_t c = screen[i];
    uint8_t rgb[3] = {(uint8_t)((c >> 11) << 3 | (c >> 13)), (uint8_t)(((c >> 5) & 63) << 2 | ((c >> 9) & 3)),
                      (uint8_t)((c & 31) << 3 | ((c >> 2) & 7))};
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
}

static void scene(void *ctx) {
  (void)ctx;
  draw_world(true);
  char t[16];
  snprintf(t, sizeof t, "Level %d", g.level - res_mode_first(g.mode) + 1);
  draw_hud(t, "Left Right", g.grenades ? "power" : "turn");
}
static void picture(const char *name) {
  gfx_buf = (uint16_t *)strip;
  gfx_render(scene, NULL, 0, SCREEN_H);
  char path[700];
  snprintf(path, sizeof path, "%s/%s.ppm", dir, name);
  save_ppm(path);
}

/* ------------------------------------------------------------------ checks */
static int failures;
#define CHECK(c, ...) do { if (!(c)) { failures++; printf("FAIL " __VA_ARGS__); printf("\n"); } } while (0)

static bool finite_world(void) {
  for (int i = 0; i < g.nchar; i++) {
    float x, y, s, c;
    if (game_part(&g.ch[i], P_TORSO, &x, &y, &s, &c) && !(isfinite(x) && isfinite(y))) return false;
  }
  return true;
}

/* one shot at an angle (and power), then steps until the level ends */
static int try_shot(int level, float aim, float power, int *stars) {
  if (!game_load(level, (uint8_t *)level_mem)) return -1;
  g.aim = aim;
  g.ch[g.hero].left = cosf(aim) < 0;
  g.power = power;
  game_fire();
  for (int s = 0; s < 900 && g.state == ST_PLAY; s++) game_step();
  *stars = game_stars();
  return g.state;
}

static void physics(void) {
  /* the first level, untouched: everyone stays standing where they are */
  CHECK(game_load(0, (uint8_t *)level_mem), "level 1 loads");
  float x0[MAX_CHAR], y0[MAX_CHAR];
  for (int i = 0; i < g.nchar; i++) {
    float s, c;
    game_part(&g.ch[i], P_TORSO, &x0[i], &y0[i], &s, &c);
  }
  for (int s = 0; s < 600; s++) game_step();
  for (int i = 0; i < g.nchar; i++) {
    float x, y, s, c;
    game_part(&g.ch[i], P_TORSO, &x, &y, &s, &c);
    CHECK(g.ch[i].alive, "character %d stands", i);
    CHECK(fabsf(x - x0[i]) < 2 && fabsf(y - y0[i]) < 2, "character %d stays (%.1f %.1f)", i, x - x0[i], y - y0[i]);
  }
  CHECK(g.state == ST_PLAY, "nothing happens on its own");
  /* a bullet straight at the enemy: about 23 m/s, the level won with three stars */
  int stars = 0;
  int st = try_shot(0, g.ch[g.hero].left ? 3.14159f : 0, 0, &stars);
  CHECK(st == ST_WON && stars == 3, "level 1: one bullet ahead wins (state %d, %d stars)", st, stars);
  /* a bullet straight up comes back down? no: no gravity, it bounces off the ceiling or leaves */
  game_load(0, (uint8_t *)level_mem);
  g.aim = -1.5707963f;
  game_fire();
  float vx = 0, vy = 0;
  for (int i = 0; i < MAX_SHOT; i++)
    if (g.shot[i].body >= 0) world_velocity(g.shot[i].body, &vx, &vy);
  float v = sqrtf(vx * vx + vy * vy) / 60;
  CHECK(v > 22 && v < 24, "a bullet leaves at 23 m/s (%.2f)", v);
  game_step();
  CHECK(finite_world(), "the world stays finite");
  game_end();
}

/* the bot: every angle by half a degree (and a few powers for grenades) until one shot clears the level */
static int solve(int level, int *stars_out) {
  int best = -1, best_stars = -1;
  bool gren = game_load(level, (uint8_t *)level_mem) && g.grenades;
  int powers = gren ? 6 : 1;
  for (int p = 0; p < powers && best_stars < 3; p++)
    for (int a = 0; a < 720 && best_stars < 3; a++) {
      int stars;
      float aim = (a * 0.5f - 180) * 3.14159265f / 180;
      if (try_shot(level, aim, 0.25f + 0.15f * p, &stars) == ST_WON && stars > best_stars) best = a, best_stars = stars;
    }
  *stars_out = best_stars;
  return best;
}

static uint32_t rng = 12345;
static uint32_t rnd(void) { rng ^= rng << 13, rng ^= rng >> 17, rng ^= rng << 5; return rng; }

static int max_contacts, max_bodies;
static void fuzz_level(int level) {
  if (!game_load(level, (uint8_t *)level_mem)) {
    failures++;
    printf("FAIL level %d loads\n", level + 1);
    return;
  }
  for (int s = 0; s < 700 && g.state == ST_PLAY; s++) {
    uint32_t r = rnd() % 16;
    if (r == 0) game_fire();
    else if (r < 4) game_aim_by(0.05f);
    else if (r < 7) game_aim_by(-0.05f);
    else if (r == 7) game_turn(rnd() & 1);
    else if (r == 8) g.power = (rnd() % 100) / 100.0f;
    game_step();
    if (world_contacts() > max_contacts) {
      max_contacts = world_contacts();
    }
    if (world_bodies() > max_bodies) max_bodies = world_bodies();
    if (!finite_world()) {
      failures++;
      printf("FAIL level %d: a character went to infinity\n", level + 1);
      break;
    }
  }
  if ((level % 7) == 0) {
    gfx_buf = (uint16_t *)strip;
    gfx_render(scene, NULL, 0, SCREEN_H);
  }
  game_end();
}

static void ui_fuzz(void) {
  ui_init((uint8_t *)level_mem, (uint16_t *)strip, scratch);
  static const uint32_t keys[] = {0, KB_OK, KB_UP, KB_DOWN, KB_LEFT, KB_RIGHT, KB_BACK, KB_OK | KB_UP};
  for (int f = 0; f < 40000; f++) {
    uint32_t k = keys[rnd() % 8];
    if (rnd() % 3) k = 0;
    clock_ms += 16;
    if (!ui_frame(k, clock_ms)) ui_init((uint8_t *)level_mem, (uint16_t *)strip, scratch);
    if (ui_dirty() && (f % 5) == 0) ui_draw();
  }
  ui_quit();
}

static void saves(void) {
  Progress p, q;
  save_scratch(scratch);
  memset(files, 0, sizeof files), file_len[0] = file_len[1] = 0;
  CHECK(!save_load(&p), "no save: a fresh start");
  p.stars[3] = 2, p.flags[3] = LV_CLEARED, p.last[1] = 330, p.guide = 1, p.mode = 1;
  CHECK(save_write(&p), "the save is written");
  CHECK(save_load(&q) && !memcmp(&p, &q, sizeof p), "the save comes back");
  files[0][20] ^= 0x55;
  CHECK(save_load(&q) && !memcmp(&p, &q, sizeof p), "a damaged save comes back from its copy");
  file_len[0] = 0, file_len[1] = 0;
  CHECK(!save_load(&q), "no files: a fresh start");
}

int main(int argc, char **argv) {
  if (argc >= 3 && !strcmp(argv[1], "shot")) {
    int level = atoi(argv[2]) - 1;
    const char *fr = getenv("FRAMES"), *an = getenv("ANGLE");
    if (argc >= 4) snprintf(dir, sizeof dir, "%s", argv[3]);
    if (!game_load(level, (uint8_t *)level_mem)) return 1;
    if (an) {
      game_aim_by(0);
      g.aim = (float)atof(an) * 3.14159265f / 180;
      g.ch[g.hero].left = cosf(g.aim) < 0;
    }
    int frames = fr ? atoi(fr) : 0;
    if (frames > 0) game_fire();
    for (int i = 0; i < frames; i++) game_step();
    char name[32];
    snprintf(name, sizeof name, "level%d", level + 1);
    picture(name);
    return 0;
  }
  if (argc >= 4 && !strcmp(argv[1], "trace")) {
    /* a shot's first steps: where the bullet is and the walls it hit */
    game_load(atoi(argv[2]) - 1, (uint8_t *)level_mem);
    g.aim = (float)atof(argv[3]) * 3.14159265f / 180;
    g.ch[g.hero].left = cosf(g.aim) < 0;
    for (int i = 0; i < g.nobj; i++)
      printf("object %d (i %d) kind %d flags %x body %d\n", i, g.obj[i].idx, g.obj[i].kind, g.obj[i].flags,
             g.obj[i].body);
    game_fire();
    for (int s = 0; s < 150; s++) {
      game_step();
      for (int i = 0; i < MAX_SHOT; i++)
        if (g.shot[i].body >= 0) {
          float x, y, a, b;
          world_xf(g.shot[i].body, &x, &y, &a, &b);
          printf("step %d: bullet at %.1f %.1f, %d walls\n", s, x, y, g.shot[i].hits);
        }
      for (int i = 0; i < g.nobj; i++)
        if (g.obj[i].body >= 0 && world_dynamic(g.obj[i].body) && s % 10 == 0) {
          float x, y, vx, vy, a, b;
          world_xf(g.obj[i].body, &x, &y, &a, &b);
          world_velocity(g.obj[i].body, &vx, &vy);
          printf("step %d: object %d at %.1f %.1f moving %.1f %.1f\n", s, i, x, y, vx, vy);
        }
    }
    return 0;
  }
  if (argc >= 3 && !strcmp(argv[1], "seq")) {
    /* levels in a row, shot as the calculator's measuring build shoots them: Box2D's memory after each */
    b2_heap_limit = getenv("LIMIT") ? atol(getenv("LIMIT")) : 81000;
    for (int k = 2; k < argc; k++) {
      b2_heap_peak = b2_heap_now;
      max_contacts = 0;
      game_load(atoi(argv[k]) - 1, (uint8_t *)level_mem);
      for (int s = 0; s < 480; s++) {
        if (s % 90 == 0) {
          g.aim = -0.2f - 0.25f * (float)(s / 90);
          g.ch[g.hero].left = 0;
          game_fire();
        }
        game_step();
        if (world_contacts() > max_contacts) max_contacts = world_contacts();
      }
      long peak = b2_heap_peak;
      game_end();
      { extern long b2_alloc_max; printf("level %s: Box2D's memory at most %ld, %ld left after, biggest block %ld, contacts %d\n", argv[k], peak, b2_heap_now, b2_alloc_max, max_contacts); b2_alloc_max = 0; }
    }
    return 0;
  }
  if (argc >= 5 && !strcmp(argv[1], "try")) {
    /* which angles (degrees, by tenths) clear a level with one shot */
    int l = atoi(argv[2]) - 1;
    for (float a = (float)atof(argv[3]); a <= (float)atof(argv[4]); a += 0.5f) {
      int stars;
      printf("%.1f: %s\n", a, try_shot(l, a * 3.14159265f / 180, 0.55f, &stars) == ST_WON ? "cleared" : "-");
    }
    return 0;
  }
  if (argc >= 4 && !strcmp(argv[1], "bot")) {
    for (int l = atoi(argv[2]) - 1; l < atoi(argv[3]); l++) {
      int stars, a = solve(l, &stars);
      printf("level %d: %s", l + 1, a >= 0 ? "solved" : "not solved in one shot");
      if (a >= 0) printf(" at %.1f deg, %d stars", a * 0.5f - 180, stars);
      printf("\n");
    }
    return 0;
  }
  if (argc >= 2 && !strcmp(argv[1], "mem")) {
    /* Box2D's memory: loaded, then the most while played with random keys */
    for (int l = 0; l < LEVEL_COUNT; l++) {
      b2_heap_peak = b2_heap_now = 0;
      game_load(l, (uint8_t *)level_mem);
      long loaded = b2_heap_now;
      game_end();
      b2_heap_peak = b2_heap_now = 0;
      max_contacts = max_bodies = 0;
      fuzz_level(l);
      printf("%d %ld %ld %d %d\n", l + 1, loaded, b2_heap_peak, max_contacts, max_bodies);
    }
    return 0;
  }
  if (argc >= 3) snprintf(dir, sizeof dir, "%s", argv[2]);
  physics();
  printf("physics: done (%d failures)\n", failures);
  saves();
  /* the bot through the first levels of each mode */
  int solved = 0, tried = 0;
  for (int m = 0; m < MODE_COUNT; m++) {
    int first = res_mode_first(m), n = m == 0 ? 48 : 4;
    for (int l = first; l < first + n; l++) {
      game_load(l, (uint8_t *)level_mem);
      if (g.tr > 1) continue;   /* (one shot can't: two or more are needed) */
      int stars, a = solve(l, &stars);
      tried++;
      if (a >= 0) solved++;
      printf("level %d (mode %d #%d): %s", l + 1, m, l - first + 1, a >= 0 ? "solved" : "not solved in one shot");
      if (a >= 0) printf(" at %.1f deg, %d stars", a * 0.5f - 180, stars);
      printf("\n");
    }
  }
  printf("bot: %d of %d levels cleared with one shot\n", solved, tried);
  CHECK(solved * 10 >= tried * 7, "the bot clears most of the first levels with one shot");
  /* every level, random keys; Box2D's memory held as on the calculator (its blocks are bigger here: 64 bits) */
  b2_heap_limit = 120000;
  long peak = 0;
  for (int l = 0; l < LEVEL_COUNT; l++) {
    b2_heap_peak = b2_heap_now;
    fuzz_level(l);
    if (b2_heap_peak > peak) peak = b2_heap_peak;
  }
  printf("fuzz: %d levels, Box2D's memory at most %ld bytes\n", LEVEL_COUNT, peak);
  ui_fuzz();
  printf("ui fuzz: done\n");
  for (int l = 0; l < 3; l++) {
    game_load(l, (uint8_t *)level_mem);
    char name[32];
    snprintf(name, sizeof name, "level%d", l + 1);
    picture(name);
    game_end();
  }
  printf(failures ? "%d FAILURES\n" : "all passed\n", failures);
  return failures ? 1 : 0;
}
