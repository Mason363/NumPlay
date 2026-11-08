/* Doodle Jump on a computer, for tests and pictures (make host, make test).
 *
 *   play test          the page alone, carried past 100000 points 200 times, always has a way up that needs
 *                      no bomb, and no bomb is lit before it shows; the title's bounce scores nothing; a bot
 *                      climbs 300 times: it must get past the moving, white and exploding platforms
 *                      and the monsters, the platforms he can stand on never more than a jump apart, the
 *                      doodler always on the page; random keys for 300 more climbs break nothing; a save
 *                      comes back, and a damaged one starts afresh
 *   play film DIR N    N frames of the bot's climb, a picture every 2 (DIR/f_NNNN.ppm) */
#define NP_MODULE 1 /* (its drawing pushed to fb, its saves kept in disk, below) */
#define main dj_main
#include "../src/main.c"
#undef main
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t fb[SCREEN_H][SCREEN_W];
static uint8_t disk[sizeof sv];
static uint32_t disk_len;
void np_push(int x, int y, int w, int h, const color_t *p) {
  for (int j = 0; j < h; j++)
    for (int i = 0; i < w; i++) fb[y + j][x + i] = p[j * w + i];
}
bool eadk_display_wait_for_vblank(void) { return true; }
uint64_t eadk_keyboard_scan(void) { return 0; }
uint64_t eadk_timing_millis(void) { return 0; }
void eadk_timing_msleep(uint32_t ms) { (void)ms; }
void *np_alloc(uint32_t size) { return malloc(size); }
static uint32_t seed = 3;
uint32_t eadk_random(void) { return seed = seed * 1664525u + 1013904223u; }
const uint8_t *np_file_read(const char *name, uint32_t *len) { *len = disk_len; return disk_len ? disk : 0; }
bool np_file_write(const char *name, const void *d, uint32_t len) { memcpy(disk, d, disk_len = len); return true; }

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static float wrap_dx(float from, float to) {
  float d = to - from;
  if (d > W / 2) d -= W;
  if (d < -W / 2) d += W;
  return d;
}
/* the bot: steers for the highest platform it can still reach (not a brown one), away from monsters, and
   shoots what is above it */
static int bot(bool *fire) {
  float reach = sv.y + (sv.vy > 0 ? sv.vy * sv.vy / (2 * G) : 0) - 4;
  plat_t *best = 0;
  float aim = 0;
  for (int i = 0; i < NP; i++) {
    plat_t *p = &sv.plat[i];
    if (p->state == 1 || p->type == P_BROWN || p->y > reach || p->y < sv.y - 260) continue;
    if (p->type == P_BOMB && p->t > 120) continue;
    float t = 0, vy = sv.vy, y = sv.y; /* frames until he comes down to its height */
    while (t < 200 && !(vy < 0 && y <= p->y)) vy -= G, y += vy, t++;
    float px = p->x;
    if (p->type == P_BLUE) { /* where it will be by then, bouncing off the sides */
      float sp = 1.0f + minf(sv.score / 8000.0f, 1.2f), span = W - 2 * PW, q = p->x - PW + p->dir * sp * t;
      q = q - 2 * span * (float)(int)(q / (2 * span));
      if (q < 0) q += 2 * span;
      px = PW + (q > span ? 2 * span - q : q);
    }
    if (fabs_(wrap_dx(sv.x, px)) > t * VMAX * 0.8f + PW * 0.6f) continue;
    if (!best || p->y > best->y) best = p, aim = px;
  }
  *fire = false;
  for (int i = 0; i < NM; i++) {
    mon_t *m = &sv.mon[i];
    if (m->kind == M_NONE || m->state || m->kind == M_HOLE) continue;
    if (m->y > sv.y && m->y < sv.y + 400 && fabs_(wrap_dx(sv.x, m->x)) < 30) *fire = (frame % 6) == 0;
  }
  if (!best) return 0;
  float d = wrap_dx(sv.x, aim);
  for (int i = 0; i < NM; i++) { /* a black hole or a monster in the way: around it */
    mon_t *m = &sv.mon[i];
    if (m->kind == M_NONE || m->state) continue;
    float md = wrap_dx(sv.x, m->x);
    if (m->y > sv.y - 60 && m->y < sv.y + 200 && fabs_(md) < 50 && (m->kind == M_HOLE || sv.vy > 0)) return md > 0 ? -1 : 1;
  }
  return d > 3 ? 1 : d < -3 ? -1 : 0;
}

/* the platforms that stay (not brown, not a bomb) never more than a jump apart, and no bomb lit above the page */
static void check_page(void) {
  float ys[NP];
  int n = 0;
  for (int i = 0; i < NP; i++) {
    const plat_t *p = &sv.plat[i];
    if (p->type != P_BROWN && p->type != P_BOMB && p->y > sv.cam - 40) ys[n++] = p->y;
    CHECK(p->type != P_BOMB || p->y < sv.cam + VIEW || (p->t == 0 && p->state == 0), "a bomb lit above the page at %.0f", p->y);
  }
  for (int i = 1; i < n; i++)
    for (int j = i; j > 0 && ys[j] < ys[j - 1]; j--) {
      float t = ys[j];
      ys[j] = ys[j - 1], ys[j - 1] = t;
    }
  for (int i = 1; i < n; i++) CHECK(ys[i] - ys[i - 1] <= 160.01f, "platforms %.0f apart at %.0f", ys[i] - ys[i - 1], ys[i]);
  CHECK(sv.x >= 0 && sv.x < W && sv.y == sv.y, "doodler off the page: %f", sv.x);
}

static int climb(bool smart, int frames) {
  seed = seed * 7 + 11;
  now = seed;
  new_climb();
  uint32_t last = 0;
  int f = 0;
  for (; f < frames && !over; f++) {
    bool fire = false;
    int steer = smart ? bot(&fire) : (int)(eadk_random() % 3) - 1;
    if (!smart) fire = eadk_random() % 20 == 0;
    if (fire && !(sv.flags & 4) && sv.fly <= 0) shoot(), now += 400;
    step(steer);
    CHECK(sv.score >= last, "the score went down");
    last = sv.score;
    if (f % 30 == 0) check_page();
  }
  return f;
}

static void picture(const char *path) {
  FILE *fp = fopen(path, "wb");
  if (!fp) exit(2);
  fprintf(fp, "P6 320 240 255\n");
  for (int y = 0; y < SCREEN_H; y++)
    for (int x = 0; x < SCREEN_W; x++) {
      uint16_t c = fb[y][x];
      fputc((c >> 11) * 255 / 31, fp), fputc((c >> 5 & 63) * 255 / 63, fp), fputc((c & 31) * 255 / 31, fp);
    }
  fclose(fp);
}
static void render(void) {
  gfx_buf = strip;
  gfx_render(scene, 0, 0, SCREEN_H);
}

int main(int argc, char **argv) {
  if (argc >= 4 && !strcmp(argv[1], "film")) {
    int frames = atoi(argv[3]), k = 0;
    seed = argc > 4 ? (uint32_t)atoi(argv[4]) : 3;
    now = 1000;
    sv.flags = 2;
    new_climb();
    char path[256];
    for (int f = 0; f < frames && !over; f++) {
      now += 17;
      bool fire;
      int steer = bot(&fire);
      if (fire && !(sv.flags & 4) && sv.fly <= 0) shoot();
      step(steer);
      if (f % 2) continue;
      render();
      snprintf(path, sizeof path, "%s/f_%04d.ppm", argv[2], k++);
      picture(path);
    }
    printf("score %u\n", sv.score);
    return 0;
  }
  /* the page alone, the doodler carried up by a hat past 100000, 200 times: always a way up */
  for (int g = 0; g < 200; g++) {
    seed = seed * 7 + 11;
    now = seed;
    new_climb();
    for (int f = 0; f < 60 * 250; f++) {
      sv.fly = 2, sv.item = I_PROP;
      step(0);
      if (f % 10 == 0) check_page();
    }
  }
  /* on the title he bounces in place and scores nothing */
  for (int g = 0; g < 50; g++) {
    seed = seed * 7 + 11;
    new_climb(), ready = true;
    uint32_t best_before = sv.best;
    for (int f = 0; f < 600; f++) {
      step(0);
      CHECK(sv.y < 40 + 174 && sv.cam == 0, "off the start platform on the title: %.0f", sv.y);
    }
    CHECK(sv.score == 0 && sv.best == best_before, "points on the title: %u", sv.score);
  }
  ready = false;
  step(0);
  CHECK(sv.score > 0, "no points once playing");
  long total = 0;
  uint32_t best = 0, far = 0;
  int dead = 0, holes = 0, fell = 0, hbins[8] = {0};
  for (int g = 0; g < 300; g++) {
    climb(true, 60 * 60 * 5);
    total += sv.score, best = sv.score > best ? sv.score : best;
    if (sv.score > 12000) far++;
    if (sv.flags & 4) { if (sv.vy == 0 && sv.fly == 0 && over) dead++; } else fell++;
    hbins[sv.score / 2500 > 7 ? 7 : sv.score / 2500]++;
  }
  if (getenv("DJ_STATS")) {
    printf("killed %d, fell %d (holes %d); ends by 2500s:", dead, fell, holes);
    for (int i = 0; i < 8; i++) printf(" %d", hbins[i]);
    printf("\n");
  }
  printf("bot: 300 climbs, %ld on average, best %u, %u past 12000\n", total / 300, best, far);
  CHECK(far >= 100, "the bot should get past 12000 (bombs, black holes) in 100 climbs of 300");
  for (int g = 0; g < 300; g++) climb(false, 60 * 60);
  /* the save: back as it was; damaged, a fresh start */
  climb(true, 600);
  sv.best = 777, sv.flags |= 1;
  save();
  float y = sv.y;
  memset(&sv, 0, sizeof sv);
  load();
  CHECK(sv.best == 777 && sv.y == y && (sv.flags & 1), "save not read back");
  disk[60] ^= 2;
  load();
  CHECK(sv.best == 0 && !(sv.flags & 1), "a damaged file should start afresh");
  render();
  printf(fails ? "FAILED (%d)\n" : "ok\n", fails);
  return fails != 0;
}
