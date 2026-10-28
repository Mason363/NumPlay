/* Snow Rider 3D on a computer.
 *   build/play test <dir>             the tests (make test)
 *   build/play shots <dir> <script>   a script of keys, pictures saved as it says
 * A script has lines "<ms> <keys> [picture]": keys a set of l r u d o b h (- for none, A the autopilot), held from
 * that time on. SR_GOD=1: the sled goes through everything (to see far down the slope); SR_ZSHOTS=z1,z2...: a
 * picture as the sled passes each. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/sr.h"
#include "../src/r3d.h"
#include "../src/save.h"

extern uint32_t host_time;
void host_shot(const char *path);
void host_save_dir(const char *d);
bool host_god;

/* the autopilot: looks ahead along a fan of headings, steers to the clearest, jumps when it must */
static float clear_along(float yaw) {
  float o[3] = {sled.x, sled.py + 0.25f, sled.z}, d[3] = {sinf(yaw * DEG), 0, cosf(yaw * DEG)};
  Hit h;
  float best = 14;
  for (int s = -1; s <= 1; s++) {
    float os[3] = {o[0] + d[2] * 0.4f * s, o[1], o[2] - d[0] * 0.4f * s}, low[3] = {os[0], os[1] - 0.15f, os[2]};
    if (ray_cast(os, d, best, L_OBSTACLE, &h)) best = h.t;
    if (ray_cast(low, d, best, L_OBSTACLE, &h)) best = h.t;
  }
  for (float t = 1.5f; t < best; t += 1.5f) {
    float g[3] = {o[0] + d[0] * t, o[1] + 3, o[2] + d[2] * t}, dn[3] = {0, -1, 0};
    if (!ray_cast(g, dn, 8, L_SURFACE, &h)) return t * 0.8f;   /* a hole */
  }
  return best;
}

static uint32_t autopilot(void) {
  float best = -1, best_yaw = 0;
  for (float a = -60; a <= 60; a += 6) {
    float c = clear_along(a) - fabsf(a) * 0.02f - fabsf(a - sled.hrot) * 0.01f;
    if (c > best) best = c, best_yaw = a;
  }
  uint32_t k = 0;
  if (best_yaw < sled.hrot - 3) k |= SK_LEFT;
  if (best_yaw > sled.hrot + 3) k |= SK_RIGHT;
  if (clear_along(sled.hrot) < 2.5f) k |= SK_UP;
  return k;
}

#define AUTO 0x80000000u
static uint32_t prev, step_no;
static void step(uint32_t keys) {
  uint32_t k = keys & AUTO ? (keys & ~AUTO) | (mode == M_PLAY ? autopilot() : 0) : keys;
  host_time = ++step_no * 50 / 3;
  game_step(k, k & ~prev);
  prev = k;
}
static void steps(int n, uint32_t keys) {
  for (int i = 0; i < n && game_running; i++) step(keys);
}
static void press(uint32_t key) { step(key), step(0); }

static void restart(const char *dir) {
  host_save_dir(dir);
  game_running = true, mode = M_INTRO, screen = S_NONE, prev = 0;
  game_init();
}

/* ---------------------------------------------------------------- tests */
static int failed;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); failed++; } } while (0)

static void play_from_menu(void) {
  press(SK_OK);                    /* the title */
  press(SK_OK);                    /* Play */
  if (screen == S_GUIDE) press(SK_OK);
}

static void test_slope(void) {
  /* a long slope: every structure made, the instances in their ring, nothing out of the slope's width */
  world_start();
  int made = 0;
  for (float z = 0; z < 20000; z += 1)
    if (world_update(z)) made++;
  CHECK(made > 1900, "structures made: %d", made);
  int alive = 0;
  for (int i = 0; i < MAXI; i++)
    if (insts[i].alive) {
      alive++;
      CHECK(insts[i].lo[0] > -40 && insts[i].hi[0] < 60, "an instance at x %.1f..%.1f", insts[i].lo[0], insts[i].hi[0]);
    }
  CHECK(alive > 50, "instances alive: %d", alive);
}

static void test_ride(const char *dir) {
  /* the autopilot through the sled's own physics: points, gifts, and the slope under it all along */
  restart(dir);
  play_from_menu();
  CHECK(mode == M_PLAY, "playing: mode %d", mode);
  host_god = true;
  steps(60 * 120, AUTO);
  host_god = false;
  CHECK(mode == M_PLAY && score > 60, "two minutes down the slope: mode %d, score %d", mode, score);
  CHECK(gifts_game >= 1, "gifts picked up: %d", gifts_game);
  CHECK(sled.x > 0 && sled.x < STRUCT_W * CHUNK_X, "between the walls: x %.1f", sled.x);
  /* the speed grows with the score */
  float want = SLED_BASEMOVESPEED + powf((float)score / SLED_SPEEDACCELERATION * SLED_SPEEDACCELERATIONAMPLITUDE, 0.55f);
  CHECK(fabsf(sled.move_speed - want) < 0.5f, "speed %.2f for score %d (%.2f)", sled.move_speed, score, want);
}

static void test_walls(const char *dir) {
  /* held left or right, the sled meets a wall and breaks; the run ends with its score */
  for (int side = 0; side < 2; side++) {
    restart(dir);
    play_from_menu();
    steps(60 * 3, 0);
    steps(60 * 8, side ? SK_RIGHT : SK_LEFT);
    CHECK(mode == M_END, "into the %s wall: mode %d at x %.1f", side ? "right" : "left", mode, sled.x);
    CHECK(sled.x > 0.5f && sled.x < STRUCT_W * CHUNK_X - 0.5f, "stopped by the wall: x %.1f", sled.x);
    steps(60 * 2, 0);
    press(SK_OK);   /* Play again */
    CHECK(mode == M_PLAY && score == 0, "play again: mode %d", mode);
  }
}

static void test_jump(const char *dir) {
  restart(dir);
  play_from_menu();
  steps(60 * 2, 0);
  float y0 = sled.py;
  step(SK_UP);
  steps(12, SK_UP);
  CHECK(sled.py > y0 + 0.2f && !sled.grounded, "a jump: from %.2f to %.2f", y0, sled.py);
  steps(60 * 2, 0);
  CHECK(sled.grounded, "landed");
}

static void test_saves(const char *dir) {
  char p[512], q[512];
  snprintf(p, sizeof p, "%s/snowrider.sav", dir), snprintf(q, sizeof q, "%s/snowrider_saves.py", dir);
  remove(p), remove(q);
  restart(dir);
  CHECK(best == 0 && gifts == 0 && owned == 1 && !guide_read, "a fresh start");
  play_from_menu();
  steps(60 * 3, 0);
  steps(60 * 8, SK_LEFT);
  CHECK(mode == M_END, "a run ended");
  int b = best, g = gifts;
  CHECK(b == score && guide_read, "best %d of score %d, guide read %d", b, score, guide_read);
  /* the save, then the copy alone (installed again), then a damaged save with its copy, then both damaged */
  restart(dir);
  CHECK(best == b && gifts == g && guide_read && plays == 1, "read back: best %d gifts %d plays %d", best, gifts, plays);
  remove(p);
  restart(dir);
  CHECK(best == b && plays == 1, "from the copy: best %d", best);
  FILE *f = fopen(p, "r+b");
  CHECK(f != NULL, "saved again");
  if (f) fseek(f, 9, SEEK_SET), fputc(0x77, f), fclose(f);
  restart(dir);
  CHECK(best == b, "a damaged save, the copy read: best %d", best);
  f = fopen(q, "r+b");
  if (f) fseek(f, 120, SEEK_SET), fputc('!', f), fclose(f);
  f = fopen(p, "wb");
  if (f) fwrite("SNR1", 1, 4, f), fclose(f);
  restart(dir);
  CHECK(best == 0 && owned == 1 && skin == 0, "both damaged: a fresh start");
  /* buying a sled with gifts */
  gifts = 60;
  press(SK_OK);
  press(SK_DOWN), press(SK_OK);   /* Sleds */
  CHECK(screen == S_SLEDS, "the sleds");
  press(SK_RIGHT), press(SK_OK);  /* the Metal Sleigh, 50 gifts */
  CHECK(owned == 3 && skin == 1 && gifts == 10, "bought: owned %d skin %d gifts %d", owned, skin, gifts);
  restart(dir);
  CHECK(owned == 3 && skin == 1 && gifts == 10, "kept: owned %d skin %d gifts %d", owned, skin, gifts);
}

static void test_random(const char *dir) {
  /* random keys: never a crash, the sanitizers watching; a picture now and then */
  uint32_t seed = 1;
  for (int run = 0; run < 300; run++) {
    restart(dir);
    for (int i = 0; i < 60 * 12 && game_running; i++) {
      seed ^= seed << 13, seed ^= seed >> 17, seed ^= seed << 5;
      uint32_t k = seed % 7 == 0 ? (1u << (seed >> 8) % 6) : prev & ~(seed % 11 == 0 ? SK_BACK | SK_OK : 0);
      if (mode == M_MAIN && sel == 2 && screen == S_NONE) k &= ~SK_OK;   /* (not Quit: the session goes on) */
      step(k & ~SK_HOME);
      if (i % 97 == 0) draw_frame();
    }
  }
}

static int test(const char *dir) {
  test_slope();
  test_ride(dir);
  test_walls(dir);
  test_jump(dir);
  test_saves(dir);
  test_random(dir);
  printf(failed ? "%d tests failed\n" : "tests passed\n", failed);
  return failed != 0;
}

/* ---------------------------------------------------------------- pictures */
static uint32_t parse(const char *k) {
  uint32_t r = 0;
  for (; *k; k++)
    r |= *k == 'l' ? SK_LEFT : *k == 'r' ? SK_RIGHT : *k == 'u' ? SK_UP : *k == 'd' ? SK_DOWN : *k == 'o' ? SK_OK
       : *k == 'b' ? SK_BACK : *k == 'h' ? SK_HOME : *k == 'A' ? AUTO : 0;
  return r;
}

static int shots(const char *out, const char *script) {
  restart(out);
  host_god = getenv("SR_GOD") != NULL;
  const char *zs = getenv("SR_ZSHOTS") ? getenv("SR_ZSHOTS") : "";
  FILE *f = fopen(script, "r");
  if (!f) return 1;
  char line[256], shot[128] = "";
  uint32_t keys = 0, until = 0;
  bool more = fgets(line, sizeof line, f) != NULL;
  for (uint32_t i = 0; game_running && i < 60 * 600; i++) {
    uint32_t t = i * 50 / 3;
    while (more) {
      unsigned ms;
      char k[32] = "", name[128] = "";
      if (sscanf(line, "%u %31s %127s", &ms, k, name) < 2 || ms > t) break;
      keys = parse(k), until = ms;
      if (name[0]) strcpy(shot, name);
      more = fgets(line, sizeof line, f) != NULL;
    }
    step(keys);
    if (*zs && mode == M_PLAY && sled.z >= atof(zs)) {
      snprintf(shot, sizeof shot, "z%d", atoi(zs));
      while (*zs && *zs != ',') zs++;
      if (*zs) zs++;
    }
    if (shot[0]) {
      draw_frame();
      char p[512];
      snprintf(p, sizeof p, "%s/%s.ppm", out, shot);
      host_shot(p);
      printf("%s: mode %d screen %d score %d z %.1f x %.1f, %d polygons\n", shot, mode, screen, score, sled.z, sled.x, r_count());
      shot[0] = 0;
    }
    if (!more && t > until + 3000) break;
  }
  fclose(f);
  return 0;
}

int main(int argc, char **argv) {
  static uint16_t band[SCREEN_W * BAND_H];
  r_set_band_memory(band);
  r_init(malloc(r_work_size()));
  draw_init();
  if (argc >= 3 && !strcmp(argv[1], "test")) return test(argv[2]);
  if (argc >= 4 && !strcmp(argv[1], "shots")) return shots(argv[2], argv[3]);
  fprintf(stderr, "build/play test <dir> | build/play shots <dir> <script>\n");
  return 2;
}
