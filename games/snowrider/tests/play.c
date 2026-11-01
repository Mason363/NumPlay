/* Snow Rider 3D on a computer.
 *   build/play test <dir>             the tests (make test)
 *   build/play shots <dir> <script>   a script of keys, pictures saved as it says
 * A script has lines "<ms> <keys> [picture]": keys a set of l r u d o b h (- for none, A the autopilot), held from
 * that time on. SR_GOD=1: the sled goes through everything (to see far down the slope); SR_ZSHOTS=z1,z2...: a
 * picture as the sled passes each; SR_WARM=n: n frames drawn before each picture (as in play, the far meshes settle). */
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
#define SNOWBALL_SIDE 43   /* SideGroundSnowball in data.c */

#include "auto.h"

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

static void test_balls(void) {
  /* the snowballs: they keep coming from their hill (a ball each 0.7 s) and roll across the slope (to the other
   * side's SnowballDestroy), and leave nothing behind */
  world_start();
  int on = 0, ghosts = 0;
  float z = 0;
  for (int i = 0; i < 60 * 300; i++) {
    while (world_update(z += 6 * DT_REF)) {}
    world_balls(DT_REF);
    for (int b = 0; b < MAXB; b++) {
      on += balls[b].alive && balls[b].x > 0 && balls[b].x < STRUCT_W * CHUNK_X && balls[b].z > z;
      ghosts += ball_inst[b].alive && !balls[b].alive;
    }
  }
  CHECK(on * DT_REF > 180, "snowballs on the slope ahead: %.0f s", on * DT_REF);
  CHECK(!ghosts, "snowballs gone but still there: %d", ghosts);
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

static void test_sides(void) {
  /* from the side chunks, rays along the snow outwards and ahead: the walls and banks stop them (collision meshes
   * with fewer triangles than the game's let them out, the snowballs' side most of all): under 1 in 20 */
  int out = 0, out_snow = 0, n = 0;
  for (uint32_t seed = 1; seed <= 2; seed++) {
    rnd_seed(seed);
    world_start();
    for (float zs = 20; zs < 600; zs += CHUNK_Z) {
      while (generate_distance < zs + 40) world_update(generate_distance + 1);
      for (float z = zs + 0.5f; z < zs + CHUNK_Z; z += 0.25f)
        for (float x = 1.5f; x < STRUCT_W * CHUNK_X; x += x == 4.5f ? 11 : 1) {
          float o[3] = {x, 5, z}, dn[3] = {0, -1, 0};
          Hit h;
          if (!ray_cast(o, dn, 6, L_SURFACE, &h)) continue;   /* (a crack) */
          int pf = h.inst->prefab;
          float gy = h.p[1], s = x < 10 ? -1 : 1;
          for (float up = 0.1f; up < 0.4f; up += 0.2f)
            for (int a = 0; a <= 30; a += 30) {
              float p[3] = {x, gy + up, z}, d[3] = {s * cosf(a * DEG), 0, sinf(a * DEG)};
              n++;
              if (!ray_cast(p, d, 12, L_OBSTACLE | L_SURFACE, &h)) out++, out_snow += pf == SNOWBALL_SIDE;
            }
        }
    }
  }
  CHECK(out * 20 < n && out_snow < 30, "rays out of the slope: %d of %d, %d by the snowballs", out, n, out_snow);
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
  press(SK_LEFT);                 /* Sleds: the sign at the bottom left */
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
  test_balls();
  test_ride(dir);
  test_walls(dir);
  test_sides();
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
      for (int k = 0; k < (getenv("SR_WARM") ? atoi(getenv("SR_WARM")) : 0); k++) draw_frame();
      draw_frame();
      char p[512];
      snprintf(p, sizeof p, "%s/%s.ppm", out, shot);
      host_shot(p);
      extern int r_dropped;
      printf("%s: mode %d screen %d score %d z %.1f x %.1f, %d polygons (%d left out)\n", shot, mode, screen, score, sled.z, sled.x, r_count(), r_dropped);
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
