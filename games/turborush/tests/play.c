/* Turbo Rush on a computer, for tests and pictures (make host, make test).
 *
 *   play test DIR        laps, crashes into the roadside and into cars, the rivals finishing every circuit at every
 *                        level, a driver finishing races, the Grand Prix and Free Ride through the whole app, saves
 *                        written and read back, damaged saves, a few hundred sessions of random keys, and the same
 *                        race twice, to the same hash on every computer
 *   play shots DIR       pictures of the screens (DIR/<name>.ppm)
 *   play docs DIR        the README's pictures (DIR/<name>.ppm)
 *   play film DIR MS T [TIME]   MS milliseconds of a race on circuit T (time of day 0..2), a bot driving
 *                        (DIR/<frame>_<ms>.raw, for tools/record.py --frames:
 *                        docs/media/gallery/turborush.gif is `play film DIR 24000 0`, 11000-20000)
 */
#define main tr_app_main
#include "../src/main.c"
#undef main
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

/* ---------------------------------------------------------------- the calculator, on a computer */
static uint16_t fb[240][320];
static void (*on_frame)(void);
void plat_push(int x, int y, int w, int h, const uint16_t *px) {
  for (int j = 0; j < h; j++)
    for (int i = 0; i < w; i++) fb[y + j][x + i] = px[j * w + i];
  if (on_frame && y + h == 240) on_frame();
}
void *np_alloc(uint32_t n) { (void)n; return 0; }
static double clock_ms;
static uint32_t (*script)(uint32_t frame);
static uint32_t frame_no, quit_at;
static void (*watch)(void);
uint32_t plat_keys(void) {
  clock_ms += 5;
  frame_no++;
  if (watch) watch();
  if (frame_no >= quit_at) return TK_HOME;
  return script ? script(frame_no) : 0;
}
uint32_t plat_millis(void) { return (uint32_t)clock_ms; }
void plat_sleep(uint32_t ms) { clock_ms += ms; }
void plat_vblank(void) {}
static uint32_t rnd_state = 1;
uint32_t plat_random(void) { return tr_rng(&rnd_state); }
bool plat_slow(void) { return false; }
void plat_begin(void) {}
int plat_end(void) { return 0; }

static struct { char name[32]; uint8_t data[4096]; uint32_t len; bool on; } files[4];
static bool storage_full;
bool plat_save(const char *name, const void *data, uint32_t len) {
  if (storage_full) return false;
  int k = -1;
  for (int i = 0; i < 4; i++)
    if (files[i].on && !strcmp(files[i].name, name)) k = i;
  for (int i = 0; i < 4 && k < 0; i++)
    if (!files[i].on) k = i;
  if (k < 0 || len > sizeof files[k].data) return false;
  strcpy(files[k].name, name), memcpy(files[k].data, data, len), files[k].len = len, files[k].on = true;
  return true;
}
const uint8_t *plat_load(const char *name, uint32_t *len) {
  for (int i = 0; i < 4; i++)
    if (files[i].on && !strcmp(files[i].name, name)) return *len = files[i].len, files[i].data;
  return 0;
}
static void forget(const char *name) {
  for (int i = 0; i < 4; i++)
    if (files[i].on && !strcmp(files[i].name, name)) files[i].on = false;
}
static uint8_t *file_data(const char *name, uint32_t *len) {
  for (int i = 0; i < 4; i++)
    if (files[i].on && !strcmp(files[i].name, name)) return *len = files[i].len, files[i].data;
  return 0;
}

static void shot(const char *dir, const char *name) {
  char path[512];
  snprintf(path, sizeof path, "%s/%s.ppm", dir, name);
  FILE *f = fopen(path, "wb");
  if (!f) exit(2);
  fprintf(f, "P6 320 240 255\n");
  for (int y = 0; y < 240; y++)
    for (int x = 0; x < 320; x++) {
      uint16_t c = fb[y][x];
      fputc((c >> 11) * 255 / 31, f), fputc((c >> 5 & 63) * 255 / 63, f), fputc((c & 31) * 255 / 31, f);
    }
  fclose(f);
}

static int fails;
#define RACE_HASH 0xAF46D491u   /* (made on a Mac with clang; Linux with GCC must agree) */
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* ---------------------------------------------------------------- a driver */
/* steers for the middle of a free lane, cancels the bend's pull, brakes for what comes and burns nitro on
   straights; skill 0..100 (how much margin it keeps) */
static uint64_t isqrt64(uint64_t v) {
  uint64_t r = 0, b = (uint64_t)1 << 62;
  while (b > v) b >>= 2;
  while (b) {
    if (v >= r + b) v -= r + b, r = (r >> 1) + b;
    else r >>= 1;
    b >>= 2;
  }
  return r;
}

static int bot_skill = 100;
static int32_t bot_lane;
static unsigned bot(const tr_race *r, const tr_track *t) {
  const tr_car *c = &r->car[0];
  int32_t v = c->v;
  /* the lane: stay in it, unless a slower car is close ahead; then the lane with the most room */
  static const int32_t L[3] = {0, -TR_LANE, TR_LANE};
  int32_t room[3], ahead = 1 << 30, ahead_v = 0;
  for (int k = 0; k < 3; k++) {
    room[k] = 90 * FX;
    for (int j = 1; j <= tr_traffic(r) && j < TR_CARS; j++) {
      int32_t dz = tr_wrap(t, r->car[j].z - c->z);
      if (abs(r->car[j].x - L[k]) >= TR_CAR_WID + FX / 2) continue;
      if (dz > -7 * FX && dz < 7 * FX) room[k] = -1;
      else if (dz >= 7 * FX && dz < room[k] && room[k] >= 0) room[k] = dz;
    }
  }
  for (int j = 1; j <= tr_traffic(r) && j < TR_CARS; j++) {
    int32_t dz = tr_wrap(t, r->car[j].z - c->z);
    if (dz > 0 && dz < ahead && abs(r->car[j].x - c->x) < TR_CAR_WID + FX / 2) ahead = dz, ahead_v = r->car[j].v;
  }
  int cur = 0;
  for (int k = 1; k < 3; k++)
    if (abs(L[k] - bot_lane) < abs(L[cur] - bot_lane)) cur = k;
  if (ahead < 50 * FX && ahead_v < v) {
    int best = cur;
    for (int k = 0; k < 3; k++)
      if (room[k] > room[best] + 8 * FX) best = k;
    bot_lane = L[best];
  }
  int32_t target = bot_lane;
  int32_t grip = 100 + 6 * r->up[UP_TIRES] - (r->rain ? 12 : 0);
  int32_t reach = v < 30 * FX ? v : 30 * FX;
  int32_t authority = 9 * FX * grip / 100 * reach / (30 * FX);
  int32_t drift = (int32_t)((int64_t)v * v * tr_curve_at(t, c->z + 4 * FX) / 1905200);
  int32_t want = (target - c->x) * 2 + drift;
  unsigned in = 0;
  if (authority > 0) {
    if (want > authority / 6) in |= IN_RIGHT;
    if (want < -authority / 6) in |= IN_LEFT;
  }
  /* speed: the slowest corner coming up, braking from here; and no faster than a car close ahead */
  int32_t lim = 255 * FX;
  for (int k = 0; k < 24; k++) {
    int s = tr_seg_of(t, c->z + k * TR_SEG * FX);
    int64_t vl = (int64_t)t->limit[s] * FX * (100 + 3 * r->up[UP_TIRES] - (r->rain ? 6 : 0)) / 100;
    vl = vl * (80 + bot_skill * 14 / 100) / 100;
    int64_t d = (int64_t)k * TR_SEG;
    int64_t allowed = (int64_t)isqrt64((uint64_t)(vl * vl) + (uint64_t)(2 * 12 * d) * FX * FX);
    if (allowed < lim) lim = (int32_t)allowed;
  }
  if (ahead < 14 * FX && ahead_v + 2 * FX < lim) lim = ahead_v + (ahead - 9 * FX) * 2;
  if (v > lim + FX) in |= IN_BRAKE;
  else in |= IN_GAS;
  if (!(in & IN_BRAKE) && lim >= 200 * FX && r->nitro > 600 && !r->nitro_lock && ahead > 30 * FX) in |= IN_NITRO;
  return in;
}

static uint32_t hash_race(const tr_race *r, uint32_t h) { return h ^ tr_sum(r, sizeof *r) * 16777619u; }

/* ---------------------------------------------------------------- rules */
static tr_track TT;
static tr_setup plain(int mode, int id, int level) {
  tr_setup s;
  memset(&s, 0, sizeof s);
  s.mode = (uint8_t)mode, s.track = (uint8_t)id, s.level = (uint8_t)level;
  return s;
}

/* the rivals out of the way: half a lap off, at your speed */
static void clear_field(tr_race *r) {
  for (int i = 1; i < TR_CARS; i++) {
    tr_car *c = &r->car[i];
    c->z = r->car[0].z - TT.length / 2 - i * 20 * FX, c->v = r->car[0].v, c->x = c->lane = 0, c->think = 60;
  }
}

/* a whole race driven by the bot; returns the steps it took (0: not finished in 10 minutes) */
static int drive(tr_race *r, int id, int level, int mode, uint32_t seed, int skill, uint32_t *hash) {
  tr_setup s = plain(mode, id, level);
  tr_new(r, &TT, &s, seed);
  bot_skill = skill, bot_lane = r->car[0].x;
  int steps = 0;
  while (r->phase != PH_DONE && steps < 11 * 60 * TR_HZ) {
    tr_step(r, &TT, bot(r, &TT), 0);
    steps++;
    if (hash) *hash = hash_race(r, *hash);
    if (!tr_valid(r)) {
      CHECK(0, "race not valid at step %d on %s", steps, tr_track_name[id]);
      break;
    }
  }
  return r->end == END_FINISH ? steps : 0;
}

static void tracks(void) {
  for (int id = 0; id < TR_TRACKS; id++) {
    tr_build(&TT, id);
    CHECK(TT.n > 300 && TT.n <= TR_MAXSEG, "%s: %d segments", tr_track_name[id], TT.n);
    CHECK(TT.y[0] == 0 && TT.y[TT.n] == 0, "%s: the hills don't come back to the start", tr_track_name[id]);
    int32_t sum = 0;
    for (int i = 0; i < TT.n; i++) sum += TT.curve[i];
    CHECK(sum > 0, "%s: the bends don't turn one way overall", tr_track_name[id]);
    int bad = 0;
    for (int i = 0; i < TT.n; i++)
      if (abs(TT.y[i + 1] - TT.y[i]) > 160) bad++;   /* no step of more than 1.6 m in 5 m, the line included */
    for (int i = 0; i < TT.n; i++) {
      int a = TT.curve[i], b = TT.curve[(i + 1) % TT.n];
      if (abs(a - b) > 12) bad++;                    /* bends ease in and out */
    }
    CHECK(!bad, "%s: %d segments too steep", tr_track_name[id], bad);
    printf("  %-14s %3d segments (%.2f km)\n", tr_track_name[id], TT.n, TT.n * TR_SEG / 1000.0);
  }
}

static void laps(void) {
  tr_race r;
  tr_setup s = plain(MODE_RACE, 0, LEVEL_EASY);
  tr_new(&r, &TT, &s, 5);
  CHECK(r.phase == PH_GRID && r.lap == 1 && tr_rank(&r, 0) == 8, "the start: on the grid, lap 1, last");
  int steps = 0, laps_seen = 1;
  while (r.phase == PH_GRID) tr_step(&r, &TT, IN_GAS, 0), steps++;
  CHECK(steps == 3 * TR_HZ + TR_HZ / 2, "the countdown took %d steps", steps);
  /* the rivals out of the way: one lap at a time */
  while (r.phase == PH_RACE && steps < 20000) {
    int before = r.lap;
    int32_t z = r.car[0].z;
    clear_field(&r);
    tr_step(&r, &TT, bot(&r, &TT), 0);
    steps++;
    if (r.lap != before) {
      CHECK(r.lap == before + 1, "lap %d to %d", before, r.lap);
      CHECK(z < (int32_t)before * TT.length && r.car[0].z >= (int32_t)before * TT.length, "the lap changed off the line");
      CHECK(r.banner == (r.lap == 3 ? BANNER_FINAL : BANNER_LAP), "the lap's banner");
      laps_seen++;
    }
  }
  CHECK(laps_seen == 3 && r.end == END_FINISH && r.car[0].place == 1, "three laps, then the finish (%d laps, end %d)",
        laps_seen, r.end);
  CHECK(r.car[0].z >= 3 * TT.length && r.car[0].finish_ms == r.time_ms, "finished at the line");
  CHECK(r.best_lap_ms > 20000 && r.best_lap_ms < r.time_ms, "a best lap (%u ms)", r.best_lap_ms);
  /* backwards isn't possible, and braking to a stop doesn't count laps */
  tr_new(&r, &TT, &s, 5);
  for (int i = 0; i < 2000; i++) tr_step(&r, &TT, IN_BRAKE, 0);
  CHECK(r.lap == 1 && r.car[0].v == 0, "standing still: still lap 1");
}

static void crashes(void) {
  tr_race r;
  /* into the roadside: Sunset Coast's palms, flat out to the right */
  tr_setup s = plain(MODE_RACE, 0, LEVEL_EASY);
  tr_new(&r, &TT, &s, 9);
  r.phase = PH_RACE, r.countdown = 0;
  r.car[0].z = 200 * FX, r.car[0].v = 55 * FX;
  int hits = 0, max_x = 0;
  for (int i = 0; i < 600; i++) {
    clear_field(&r);
    uint16_t d = r.damage;
    tr_step(&r, &TT, IN_GAS | IN_RIGHT, 0);
    if (r.damage > d) hits++;
    if (r.car[0].x > max_x) max_x = r.car[0].x;
  }
  CHECK(hits > 0 && r.damage > 0, "the palms are solid (%d hits)", hits);
  CHECK(max_x <= TR_OUT, "never past the edge (%d)", max_x);
  /* weaving off the road among the trees, from either side: never through one */
  int through = 0, bumps = 0;
  for (int id = 0; id < TR_TRACKS; id++) {
    s = plain(MODE_RACE, id, LEVEL_EASY);
    tr_new(&r, &TT, &s, 2);
    r.phase = PH_RACE, r.countdown = 0;
    uint32_t w = 99;
    for (int i = 0; i < 9000; i++) {
      clear_field(&r);
      r.damage = 0;
      if (i % 40 == 0) w = w * 1103515245u + 12345u;
      unsigned in = IN_GAS | ((w >> 16) & 1 ? IN_LEFT : IN_RIGHT) | (i % 300 < 20 ? IN_NITRO : 0);
      int32_t x0 = r.car[0].x, z0 = r.car[0].z;
      uint16_t imp = r.impact;
      tr_step(&r, &TT, in, 0);
      if (r.impact > imp) bumps++;
      int32_t per = TR_SEG * FX, base = z0 >= 0 ? z0 / per : -((-z0 + per - 1) / per);
      for (int k = -1; k <= 1; k++)
        for (int side = -1; side <= 1; side += 2) {
          tr_thing o = tr_scenery(&TT, (int)(base + k), side);
          if (o.kind < 0) continue;
          int32_t oz = (base + k) * per + per / 2;
          if (abs(oz - z0) >= 2 * FX || abs(oz - r.car[0].z) >= 2 * FX) continue;
          if ((x0 < o.x) != (r.car[0].x < o.x)) through++;
        }
    }
  }
  CHECK(!through && bumps > 50, "through the roadside %d times (%d bumps)", through, bumps);
  /* no car is ever inside another: a whole field driven with random keys */
  uint32_t lcg = 77;
  int inside = 0;
  for (int id = 0; id < TR_TRACKS; id++) {
    s = plain(MODE_RACE, id, LEVEL_EXPERT);
    tr_new(&r, &TT, &s, (uint32_t)id + 1);
    for (int i = 0; i < 6000 && r.phase != PH_DONE; i++) {
      lcg = lcg * 1103515245u + 12345u;
      unsigned in = (lcg >> 16) & 31;
      tr_step(&r, &TT, in | IN_GAS, 0);
      for (int a = 0; a < TR_CARS; a++)
        for (int b = a + 1; b < TR_CARS; b++) {
          int32_t dz = tr_wrap(&TT, r.car[b].z - r.car[a].z), dx = r.car[b].x - r.car[a].x;
          if (abs(dz) < TR_CAR_LEN - FX / 4 && abs(dx) < TR_CAR_WID - FX / 4) inside++;
        }
    }
  }
  CHECK(!inside, "cars inside each other %d times", inside);
  /* ramming the car in front: you stop at its bumper, it takes the knock */
  s = plain(MODE_RACE, 0, LEVEL_EASY);
  tr_new(&r, &TT, &s, 3);
  r.phase = PH_RACE, r.countdown = 0;
  r.car[0].z = 100 * FX, r.car[0].x = 0, r.car[0].v = 60 * FX;
  clear_field(&r);
  r.car[1].z = 108 * FX, r.car[1].x = 0, r.car[1].lane = 0, r.car[1].v = 20 * FX, r.car[1].think = 60;
  bool passed_through = false;
  for (int i = 0; i < 100; i++) {
    for (int k = 2; k < TR_CARS; k++) r.car[k].z = r.car[0].z - TT.length / 2, r.car[k].v = r.car[0].v;
    r.car[1].think = 60;
    tr_step(&r, &TT, IN_GAS, 0);
    if (r.car[0].z > r.car[1].z && abs(r.car[0].x - r.car[1].x) < TR_CAR_WID) passed_through = true;
  }
  CHECK(!passed_through, "drove through the car in front");
  CHECK(r.damage > 0, "the knock hurt");
  /* wrecked at 100%: out of the race, no prize but the minimum */
  tr_new(&r, &TT, &s, 3);
  r.phase = PH_RACE, r.countdown = 0, r.damage = 995;
  r.car[0].z = 200 * FX, r.car[0].v = 50 * FX;
  for (int i = 0; i < 800 && r.phase == PH_RACE; i++) tr_step(&r, &TT, IN_GAS | IN_RIGHT, 0);
  CHECK(r.phase == PH_DONE && r.end == END_WRECK, "wrecked at 100%% (end %d)", r.end);
}

static void rivals(void) {
  uint32_t h = 0;
  for (int id = 0; id < TR_TRACKS; id++) {
    for (int level = 0; level < 3; level++) {
      tr_race r;
      tr_setup s = plain(MODE_RACE, id, level);
      tr_new(&r, &TT, &s, (uint32_t)(id * 3 + level + 1));
      /* you sit on the grid: the rivals race on their own */
      int steps = 0;
      while (r.finished < TR_CARS - 1 && steps < 11 * 60 * TR_HZ) {
        tr_step(&r, &TT, 0, 0), steps++;
        h = hash_race(&r, h);
      }
      CHECK(r.finished == TR_CARS - 1, "%s, %s: %d of 7 rivals finished", tr_track_name[id], level ? level == 1 ? "normal" : "expert" : "easy",
            r.finished);
      uint32_t first = 0, last = 0;
      for (int i = 1; i < TR_CARS; i++) {
        if (r.car[i].place == 1) first = r.car[i].finish_ms;
        if (r.car[i].place == 7) last = r.car[i].finish_ms;
      }
      if (level == 1) printf("  %-14s rivals: %u:%04.1f to %u:%04.1f\n", tr_track_name[id], first / 60000, first % 60000 / 1000.0, last / 60000, last % 60000 / 1000.0);
      CHECK(last - first < 60000, "%s level %d: the field spread over %u ms", tr_track_name[id], level, last - first);
    }
  }
  printf("  rivals' hash %08x\n", h);
}

/* the bot against the field: stock car, every circuit and level */
static void racing(void) {
  int wins[3] = {0}, finishes = 0;
  for (int level = 0; level < 3; level++)
    for (int id = 0; id < TR_TRACKS; id++) {
      tr_race r;
      int steps = drive(&r, id, level, MODE_RACE, (uint32_t)(100 + id + level * 7), 100, NULL);
      CHECK(steps > 0, "%s level %d: the bot didn't finish (end %d)", tr_track_name[id], level, r.end);
      if (steps) finishes++;
      if (tr_rank(&r, 0) == 1) wins[level]++;
      if (level == 1)
        printf("  %-14s L%d you %u:%04.1f, %d%s, damage %d%%\n", tr_track_name[id], level, r.time_ms / 60000, r.time_ms % 60000 / 1000.0,
               tr_rank(&r, 0), "", r.damage / 10);
    }
  printf("  the bot won %d easy, %d normal, %d expert races of 6 each\n", wins[0], wins[1], wins[2]);
  CHECK(finishes == 18, "%d of 18 races finished", finishes);
  CHECK(wins[0] >= 4, "easy should be easy (%d wins)", wins[0]);
}


/* the bot, fully upgraded, against Expert */
static void upgraded(void) {
  int wins = 0;
  for (int id = 0; id < TR_TRACKS; id++) {
    tr_race r;
    tr_setup s = plain(MODE_RACE, id, LEVEL_EXPERT);
    for (int k = 0; k < 4; k++) s.up[k] = 4;
    tr_new(&r, &TT, &s, (uint32_t)(300 + id));
    bot_skill = 100, bot_lane = r.car[0].x;
    for (int steps = 0; r.phase != PH_DONE && steps < 11 * 60 * TR_HZ; steps++) tr_step(&r, &TT, bot(&r, &TT), 0);
    if (r.end == END_FINISH && tr_rank(&r, 0) == 1) wins++;
  }
  printf("  fully upgraded, the bot won %d of 6 Expert races\n", wins);
  CHECK(wins >= 3, "a full car should win against Expert (%d of 6)", wins);
}

/* Free Ride: traffic comes and goes, cars passed are counted, no damage, the odometer runs */
static void free_ride(void) {
  tr_race r;
  tr_setup s = plain(MODE_FREE, 3, 2);
  tr_new(&r, &TT, &s, 8);
  CHECK(r.phase == PH_RACE && tr_traffic(&r) == 6, "Free Ride starts at once, busy: 6 cars");
  bot_skill = 100, bot_lane = 0;
  int inside = 0;
  for (int i = 0; i < 10 * 60 * TR_HZ; i++) {
    tr_step(&r, &TT, bot(&r, &TT), 0);
    CHECK(tr_valid(&r), "Free Ride not valid at %d", i);
    for (int a = 0; a <= tr_traffic(&r); a++)
      for (int b = a + 1; b <= tr_traffic(&r); b++) {
        int32_t dz = tr_wrap(&TT, r.car[b].z - r.car[a].z), dx = r.car[b].x - r.car[a].x;
        if (abs(dz) < TR_CAR_LEN - FX / 4 && abs(dx) < TR_CAR_WID - FX / 4) inside++;
      }
    if (!tr_valid(&r)) break;
  }
  printf("  Free Ride: %.1f km in 10 minutes, %u cars passed\n", r.odometer / 256000.0, r.passes);
  CHECK(r.phase == PH_RACE && r.damage == 0, "Free Ride never ends nor hurts");
  CHECK(r.passes > 40 && r.odometer > 15000 * FX, "Free Ride: traffic to pass (%u) and distance", r.passes);
  CHECK(!inside, "traffic inside each other %d times", inside);
}

/* the same race twice, to the same hash on every computer */
static void same_twice(void) {
  uint32_t h1 = 0, h2 = 0;
  tr_race a, b;
  drive(&a, 4, LEVEL_NORMAL, MODE_GP, 2026, 100, &h1);
  drive(&b, 4, LEVEL_NORMAL, MODE_GP, 2026, 100, &h2);
  printf("  a race's hash %08X\n", h1);
  CHECK(h1 == h2 && h1 == RACE_HASH, "the same race twice, and as on every computer: %08X", h1);
}

/* ---------------------------------------------------------------- saves */
static void saves(void) {
  memset(files, 0, sizeof files);
  tr_file a, b;
  memset(&a, 0, sizeof a);
  tr_fresh(&a.p, &a.gp);
  a.p.credits = 12345, a.p.races = 9, a.p.wins = 4, a.p.titles = 1, a.p.up[0] = 3, a.p.up[3] = 4, a.p.paint = 5;
  a.p.style = 1, a.p.mode = MODE_FREE, a.p.track = 4, a.p.mood = 2, a.p.rain = 1, a.p.manual = 1, a.p.guide = 1;
  a.p.best_ms[2] = 160000, a.p.best_lap_ms[2] = 51000, a.p.best_lap_ms[5] = 47000;
  a.gp.on = 1, a.gp.round = 3;
  for (int i = 0; i < TR_CARS - 1; i++) a.gp.skill[i] = (uint8_t)(6 - i);
  a.gp.points[0] = 30, a.gp.points[3] = 41;
  tr_setup s = plain(MODE_GP, 3, 2);
  tr_new(&a.race, &TT, &s, 4);
  for (int i = 0; i < 2500; i++) tr_step(&a.race, &TT, bot(&a.race, &TT), 0);
  a.has_race = 1;
  CHECK(save_write(&a), "saved");
  CHECK(save_load(&b) && !memcmp(&a.p, &b.p, sizeof a.p) && !memcmp(&a.gp, &b.gp, sizeof a.gp) && b.has_race &&
            !memcmp(&a.race, &b.race, sizeof a.race), "read back as written");
  /* the copy in the script brings it back when the save is gone */
  forget(TR_SAVE_NAME);
  CHECK(save_load(&b) && b.p.credits == 12345 && b.has_race, "back from turborush_saves.py");
  CHECK(plat_load(TR_SAVE_NAME, &(uint32_t){0}) != NULL, "and put back in turborush.sav");
  /* every byte damaged in turn: never read (with the copy gone, a fresh start) */
  uint32_t n;
  uint8_t good[sizeof(tr_file)];
  memcpy(good, file_data(TR_SAVE_NAME, &n), sizeof good);
  forget(TR_COPY_NAME);
  int bad = 0;
  for (uint32_t i = 0; i < sizeof good; i++) {
    uint8_t *d = file_data(TR_SAVE_NAME, &n);
    memcpy(d, good, sizeof good);
    d[i] ^= (uint8_t)(1 + i % 255);
    if (save_load(&b) || b.p.credits || b.has_race || b.gp.on) bad++;
  }
  CHECK(!bad, "%d damaged saves read", bad);
  /* cut short, too long, empty, another version: a fresh start */
  uint8_t *d = file_data(TR_SAVE_NAME, &n);
  memcpy(d, good, sizeof good);
  files[0].len = sizeof good - 1;
  for (int k = 0; k < 4; k++) if (files[k].on && !strcmp(files[k].name, TR_SAVE_NAME)) files[k].len = sizeof good - 1;
  CHECK(!save_load(&b) && b.p.level == LEVEL_NORMAL, "a short save: fresh");
  tr_file v = a;
  v.version = 2;
  v.sum = tr_sum(&v, offsetof(tr_file, sum));
  plat_save(TR_SAVE_NAME, &v, sizeof v);
  CHECK(!save_load(&b), "another version: fresh");
  /* a checksum that fits over values that don't: not read either */
  uint32_t lcg = 5;
  int read = 0;
  for (int k = 0; k < 400; k++) {
    tr_file x;
    memcpy(&x, good, sizeof x);
    uint8_t *p = (uint8_t *)&x;
    for (int j = 0; j < 3; j++) {
      lcg = lcg * 1103515245u + 12345u;
      uint32_t at = 8 + (lcg >> 8) % (offsetof(tr_file, sum) - 8);
      p[at] = (uint8_t)(lcg >> 24);
    }
    x.sum = tr_sum(&x, offsetof(tr_file, sum));
    plat_save(TR_SAVE_NAME, &x, sizeof x);
    if (save_load(&b)) {
      read++;
      /* whatever was accepted must race and draw safely */
      if (b.has_race) {
        tr_build(&TT, b.race.track);
        int went_bad = 0;
        for (int i = 0; i < 300; i++) {
          tr_step(&b.race, &TT, (unsigned)i & 31, 0);
          if (!tr_valid(&b.race)) went_bad++;
        }
        CHECK(!went_bad, "an accepted race went bad");
      }
    }
  }
  printf("  saves: %d of 400 changed saves with a fitting checksum were still in range\n", read);
  /* odd but possible races (cars on top of each other, flat out, stopped, wide): read, then raced and drawn */
  int odd = 0, odd_bad = 0;
  for (int k = 0; k < 200; k++) {
    tr_file x;
    memcpy(&x, good, sizeof x);
    for (int i = 0; i < TR_CARS; i++) {
      tr_car *c = &x.race.car[i];
      lcg = lcg * 1103515245u + 12345u;
      c->z = (int32_t)(lcg >> 9) % (3 * 1800 * FX);
      lcg = lcg * 1103515245u + 12345u;
      c->x = i ? ((int32_t)(lcg >> 8) % (2 * TR_ROAD)) - TR_ROAD : ((int32_t)(lcg >> 8) % (2 * TR_OUT)) - TR_OUT;
      c->lane = ((int32_t)(lcg >> 20) % 3 - 1) * TR_LANE;
      lcg = lcg * 1103515245u + 12345u;
      c->v = (int32_t)(lcg >> 8) % (90 * FX);
      c->vx = 0;
    }
    x.race.car[1].z = x.race.car[2].z, x.race.car[1].x = x.race.car[2].x;   /* two cars in one place */
    x.sum = tr_sum(&x, offsetof(tr_file, sum));
    plat_save(TR_SAVE_NAME, &x, sizeof x);
    if (!save_load(&b) || !b.has_race) continue;
    odd++;
    tr_build(&TT, b.race.track);
    for (int i = 0; i < 400; i++) {
      tr_step(&b.race, &TT, bot(&b.race, &TT), 0);
      if (!tr_valid(&b.race)) odd_bad++;
    }
    view_setup(&TT, b.race.mood, b.race.rain);
    tr_render(&b, &TT, &b.race, &b.race, 256, &ui);
  }
  CHECK(odd > 150 && !odd_bad, "odd races: %d read, %d steps gone bad", odd, odd_bad);
  /* storage full: the game says so, and goes on */
  memset(files, 0, sizeof files);
  storage_full = true;
  CHECK(!save_write(&a), "a full storage fails the save");
  storage_full = false;
  memset(files, 0, sizeof files);
}

/* ---------------------------------------------------------------- the whole app, with keys */
static int bad_frames;
static void check_state(void) {
  if (ui.screen >= V_SCREENS) bad_frames++;
  if (sv.has_race && !tr_valid(&sv.race)) bad_frames++;
  if (sv.p.credits > TR_MAX_CREDITS || sv.p.wins > sv.p.races) bad_frames++;
  for (int k = 0; k < 4; k++) if (sv.p.up[k] > 4) bad_frames++;
  if (sv.gp.on && sv.gp.round >= TR_TRACKS) bad_frames++;
}

/* keys: a press every other frame for menus, or the bot's driving in a race */
static uint32_t (*menu_keys)(uint32_t f);
static uint32_t drive_keys(uint32_t f) {
  if (ui.screen == V_RACE && !ui.hold && sv.race.phase != PH_DONE) {
    unsigned in = bot(&sv.race, &track);
    uint32_t k = 0;
    if (in & IN_GAS) k |= TK_UP;
    if (in & IN_BRAKE) k |= TK_DOWN;
    if (in & IN_LEFT) k |= TK_LEFT;
    if (in & IN_RIGHT) k |= TK_RIGHT;
    if (in & IN_NITRO) k |= TK_OK;
    return k;
  }
  return menu_keys ? menu_keys(f) : 0;
}

/* the Grand Prix: Play, Start, the guide, six races driven by the bot, the standings after each, then the menu */
static uint32_t gp_menu(uint32_t f) {
  if (f % 4) return 0;
  switch (ui.screen) {
    case V_TITLE: return ui.menu[ui.sel] == MENU_PLAY ? TK_OK : TK_DOWN;
    case V_SETUP: return ui.sel == SET_START ? TK_OK : TK_DOWN;
    case V_GUIDE: return TK_OK;
    case V_RESULT: return ui.sel == 0 ? TK_OK : TK_UP;
    case V_STANDINGS: return ui.sel == 0 ? TK_OK : TK_LEFT;
    default: return 0;
  }
}

static void grand_prix(void) {
  memset(files, 0, sizeof files);
  frame_gap = 60;
  script = drive_keys, menu_keys = gp_menu, watch = check_state;
  frame_no = 0, quit_at = 4000000, bad_frames = 0;
  void watch_gp(void);   /* (it stops at the menu after the last race) */
  watch = watch_gp;
  tr_app_main();
  script = 0, menu_keys = 0, watch = 0;
  tr_file f;
  CHECK(save_load(&f), "the Grand Prix saved");
  CHECK(f.p.races == 6 && !f.gp.on && !f.has_race && f.p.guide == 1, "six races, the Grand Prix over (races %u, gp %d)",
        f.p.races, f.gp.on);
  CHECK(f.p.credits >= 6 * 50 + 200, "credits earned: %u", f.p.credits);
  printf("  Grand Prix through the keys: %u wins, %u credits, title %u, %u frames\n", f.p.wins, f.p.credits, f.p.titles,
         frame_no);
  CHECK(!bad_frames, "%d bad frames", bad_frames);
}
static int gp_phase;
void watch_gp(void) {
  check_state();
  if (ui.screen == V_STANDINGS && ui.round_done >= TR_TRACKS) gp_phase = 1;
  if (gp_phase == 1 && ui.screen == V_TITLE) quit_at = frame_no + 2, gp_phase = 2;
}

/* Free Ride: chosen on the race screen, a minute of driving, pause, Quit game to the menu (kept), Continue, then
   Home: it's saved */
static int fr_step;
static uint32_t fr_menu(uint32_t f) {
  if (f % 4) return 0;
  switch (ui.screen) {
    case V_TITLE:
      if (fr_step >= 2) return ui.menu[ui.sel] == MENU_CONTINUE ? TK_OK : TK_UP;
      return ui.menu[ui.sel] == MENU_PLAY ? TK_OK : TK_DOWN;
    case V_SETUP:
      if (sv.p.mode != MODE_FREE) return ui.sel == SET_MODE ? TK_RIGHT : TK_UP;
      return ui.sel == SET_START ? TK_OK : TK_UP;
    case V_GUIDE: return TK_OK;
    case V_PAUSE: return ui.sel == PAUSE_QUIT ? TK_OK : TK_DOWN;
    case V_LEAVE: return ui.sel == 1 ? TK_OK : TK_DOWN;
    default: return 0;
  }
}
static uint32_t fr_keys(uint32_t f) {
  if (ui.screen == V_RACE && fr_step == 0 && sv.race.odometer > 1500 * FX) fr_step = 1;
  if (ui.screen == V_RACE && fr_step == 1) return f % 8 < 4 ? TK_BACK : 0;
  if (ui.screen == V_TITLE && fr_step == 1) fr_step = 2;
  if (ui.screen == V_RACE && fr_step == 2) fr_step = 3, quit_at = frame_no + 600;
  return drive_keys(f);
}
static void free_ride_app(void) {
  memset(files, 0, sizeof files);
  frame_gap = 60;
  fr_step = 0;
  script = fr_keys, menu_keys = fr_menu, watch = check_state;
  frame_no = 0, quit_at = 400000, bad_frames = 0;
  tr_app_main();
  script = 0, menu_keys = 0, watch = 0;
  tr_file f;
  CHECK(fr_step == 3, "the Free Ride steps (%d)", fr_step);
  CHECK(save_load(&f) && f.has_race && f.race.mode == MODE_FREE && f.race.odometer > 1500 * FX,
        "Free Ride kept on Home (%d)", f.has_race);
  CHECK(!bad_frames, "%d bad frames", bad_frames);
  printf("  Free Ride through the keys: %.1f km, kept on Home\n", f.race.odometer / 256000.0);
}

static uint32_t lcg_keys;
static uint32_t rnd(void) { return lcg_keys = lcg_keys * 1103515245u + 12345u, lcg_keys >> 8; }
static uint32_t held_keys, held_until;
static uint32_t random_keys(uint32_t f) {
  if (f >= held_until) {
    static const uint32_t pick[] = {TK_OK, TK_OK, TK_OK, TK_LEFT, TK_RIGHT, TK_UP, TK_UP, TK_UP, TK_DOWN, TK_BACK,
                                    TK_GEAR_UP, TK_GEAR_DOWN, TK_UP | TK_OK, TK_UP | TK_LEFT, 0, 0};
    held_keys = pick[rnd() % (sizeof pick / sizeof pick[0])];
    held_until = f + 2 + rnd() % 40;
  }
  return held_keys;
}

static void fuzz(void) {
  memset(files, 0, sizeof files);
  frame_gap = 90;
  int sessions = 0, kept = 0;
  for (int s = 0; s < 300; s++) {
    lcg_keys = (uint32_t)s * 2654435761u + 1;
    if (s % 40 == 0) memset(files, 0, sizeof files);
    storage_full = s % 37 == 36;
    script = random_keys, watch = check_state;
    frame_no = 0, quit_at = 2000 + rnd() % 6000, held_until = 0, bad_frames = 0;
    tr_app_main();
    sessions++;
    storage_full = false;
    tr_file f;
    if (save_load(&f)) kept += f.has_race;
    CHECK(!bad_frames, "session %d: %d bad frames", s, bad_frames);
  }
  script = 0, watch = 0;
  printf("  keys: %d sessions of random keys, quit with Home each time (%d left a race going)\n", sessions, kept);
}

static void test(const char *dir) {
  printf("circuits\n");
  tracks();
  printf("laps\n");
  laps();
  printf("crashes\n");
  crashes();
  printf("rivals\n");
  rivals();
  printf("racing\n");
  racing();
  upgraded();
  printf("Free Ride\n");
  free_ride();
  printf("the same race\n");
  same_twice();
  printf("saves\n");
  saves();
  printf("the app\n");
  grand_prix();
  free_ride_app();
  fuzz();
  printf(fails ? "%d FAILED\n" : "all good\n", fails);
}

/* ---------------------------------------------------------------- pictures */
static void picture(const char *dir, const char *name) {
  bool title = ui.screen == V_TITLE || ui.screen == V_QUIT;
  tr_render(&sv, &track, title ? &demo_prev : &prev, title ? &demo : &sv.race, 256, &ui);
  shot(dir, name);
}

static void race_at(int id, int mood, int rain, int level, float meters) {
  tr_setup s = plain(MODE_RACE, id, level);
  s.mood = (uint8_t)mood, s.rain = (uint8_t)rain, s.paint = sv.p.paint, s.style = sv.p.style;
  tr_new(&sv.race, &track, &s, 11);
  shown_track = -1;
  show(&sv.race);
  bot_skill = 100;
  while (sv.race.car[0].z < (int32_t)(meters * FX) && sv.race.phase != PH_DONE) {
    prev = sv.race;
    tr_step(&sv.race, &track, bot(&sv.race, &track), 0);
  }
  prev = sv.race;
  sv.has_race = 1;
}

static void shots(const char *dir) {
  memset(&sv, 0, sizeof sv);
  tr_fresh(&sv.p, &sv.gp);
  sv.p.credits = 2350, sv.p.up[0] = 2, sv.p.up[1] = 1, sv.p.races = 14, sv.p.wins = 5, sv.p.titles = 1;
  sv.p.best_ms[0] = 151300, sv.p.best_lap_ms[0] = 49200, sv.p.best_ms[2] = 163800, sv.p.best_lap_ms[2] = 53100;
  now = 1000;
  to_title();
  for (int i = 0; i < 400; i++) demo_prev = demo, tr_step(&demo, &track, 0, 0);
  picture(dir, "title");
  ui.screen = V_SETUP, ui.sel = SET_START;
  picture(dir, "setup");
  ui.screen = V_GARAGE, ui.sel = 0, ui.note[0] = 0;
  picture(dir, "garage");
  ui.screen = V_RECORDS;
  picture(dir, "records");
  for (int k = 0; k < TR_HELP_PAGES; k++) {
    ui.screen = V_HELP, ui.page = (uint8_t)k;
    char n[16];
    snprintf(n, sizeof n, "help%d", k);
    picture(dir, n);
  }
  ui.screen = V_RACE;
  race_at(0, MOOD_DAY, 0, 1, -10);
  picture(dir, "grid");
  static const float AT[6] = {700, 900, 1250, 600, 820, 700};
  for (int id = 0; id < TR_TRACKS; id++) {
    race_at(id, id == 5 ? MOOD_NIGHT : id == 2 ? MOOD_SUNSET : MOOD_DAY, 0, 1, AT[id]);
    char n[16];
    snprintf(n, sizeof n, "track%d", id);
    picture(dir, n);
  }
  race_at(2, MOOD_DAY, 0, 1, 300);
  sv.race.boost = 1, prev.boost = 1;
  picture(dir, "nitro");
  sv.p.style = 1;
  race_at(2, MOOD_DAY, 0, 1, 300);
  sv.race.boost = 1, prev.boost = 1;
  picture(dir, "nitro2");
  sv.p.style = 0;
  race_at(1, MOOD_DAY, 1, 1, 500);
  picture(dir, "rain");
  ui.screen = V_PAUSE, ui.sel = 0;
  picture(dir, "pause");
  ui.screen = V_LEAVE, ui.sel = 1;
  picture(dir, "leave");
  /* banners */
  ui.screen = V_RACE;
  race_at(0, MOOD_SUNSET, 0, 1, 2300);
  picture(dir, "banner_lap");
  race_at(4, MOOD_DAY, 0, 0, 4700);
  picture(dir, "banner_final");
  /* a Grand Prix race to the end, then the results and standings */
  memset(&sv.gp, 0, sizeof sv.gp);
  gp_new();
  tr_setup st = setup_of(MODE_GP, 0);
  tr_new(&sv.race, &track, &st, 21);
  shown_track = -1;
  show(&sv.race);
  sv.has_race = 1;
  while (!(sv.race.phase == PH_DONE && sv.race.done >= 3 * TR_HZ)) {
    prev = sv.race;
    tr_step(&sv.race, &track, bot(&sv.race, &track), 0);
    if (sv.race.phase == PH_DONE && sv.race.done == 20) {
      prev = sv.race;
      picture(dir, "banner_finish");
    }
  }
  finish_race();
  picture(dir, "result");
  ui.screen = V_STANDINGS, ui.sel = 0;
  picture(dir, "standings");
  /* Free Ride */
  ui.screen = V_RACE;
  sv.p.traffic = 2;
  race_begin(MODE_FREE, 3);
  for (int i = 0; i < 2600; i++) prev = sv.race, tr_step(&sv.race, &track, bot(&sv.race, &track), 0);
  prev = sv.race;
  picture(dir, "free");
  /* the confirmations */
  ui.screen = V_NEWRUN, ui.sel = 0;
  picture(dir, "newrun");
  to_title();
  for (int i = 0; i < 300; i++) demo_prev = demo, tr_step(&demo, &track, 0, 0);
  ui.screen = V_QUIT, ui.sel = 0;
  picture(dir, "quit");
  ui.screen = V_GARAGE, ui.sel = 3;
  garage_buy(3);
  picture(dir, "garage_buy");
  ui.screen = V_TITLE, ui.sel = 0, ui.save_failed = true;
  picture(dir, "title_full");
  ui.save_failed = false;
}

/* raw frames of a race, the bot driving, every 40 ms (for tools/record.py) */
static void film(const char *dir, int ms, int id, int mood) {
  memset(&sv, 0, sizeof sv);
  tr_fresh(&sv.p, &sv.gp);
  sv.p.mood = (uint8_t)mood, sv.p.level = 1;
  now = 0;
  ui.screen = V_RACE;
  race_begin(MODE_RACE, id);
  bot_lane = sv.race.car[0].x;
  int frame = 0;
  for (int t = 0; t < ms; t += TR_TICK) {
    prev = sv.race;
    tr_step(&sv.race, &track, bot(&sv.race, &track), 0);
    if (t % 40) continue;
    tr_render(&sv, &track, &prev, &sv.race, 256, &ui);
    char path[512];
    snprintf(path, sizeof path, "%s/%06d_%06d.raw", dir, frame++, t);
    FILE *f = fopen(path, "wb");
    if (!f) exit(2);
    fwrite(fb, 2, 320 * 240, f);
    fclose(f);
  }
}

/* the README's pictures: a race on each circuit, taken once some rivals are close ahead */
static void doc_race(const char *dir, const char *name, int id, int mood, int rain, int style, int paint, int after_ms) {
  memset(&sv, 0, sizeof sv);
  tr_fresh(&sv.p, &sv.gp);
  sv.p.mood = (uint8_t)mood, sv.p.rain = (uint8_t)rain, sv.p.style = (uint8_t)style, sv.p.paint = (uint8_t)paint;
  sv.p.level = 1, sv.p.guide = 1;
  now = 0, ui.screen = V_RACE;
  race_begin(MODE_RACE, id);
  bot_lane = sv.race.car[0].x;
  for (int t = 0; t < 4 * 60000; t += TR_TICK) {
    prev = sv.race;
    tr_step(&sv.race, &track, bot(&sv.race, &track), 0);
    if (t < after_ms || sv.race.offroad || sv.race.banner_t) continue;
    int close = 0;
    for (int i = 1; i < TR_CARS; i++) {
      int32_t dz = tr_wrap(&track, sv.race.car[i].z - sv.race.car[0].z);
      if (dz > 8 * FX && dz < 70 * FX) close++;
    }
    if (close >= 2) break;
  }
  prev = sv.race;
  picture(dir, name);
}

static void docs(const char *dir) {
  doc_race(dir, "shot", 0, MOOD_DAY, 0, 0, 0, 16000);
  doc_race(dir, "alpine", 4, MOOD_DAY, 0, 1, 2, 20000);
  doc_race(dir, "desert", 2, MOOD_SUNSET, 0, 0, 3, 24000);
  doc_race(dir, "night", 5, MOOD_NIGHT, 0, 1, 1, 18000);
  doc_race(dir, "rain", 1, MOOD_DAY, 1, 0, 4, 22000);
  /* the standings after the first Grand Prix race, the garage, the race screen and the title */
  memset(&sv, 0, sizeof sv);
  tr_fresh(&sv.p, &sv.gp);
  sv.p.guide = 1;
  now = 0;
  gp_new();
  race_begin(MODE_GP, 0);
  bot_lane = sv.race.car[0].x;
  while (!(sv.race.phase == PH_DONE && sv.race.done >= 3 * TR_HZ)) prev = sv.race, tr_step(&sv.race, &track, bot(&sv.race, &track), 0);
  finish_race();
  ui.screen = V_STANDINGS, ui.sel = 0;
  picture(dir, "standings");
  sv.p.credits = 1840, sv.p.up[0] = 2, sv.p.up[1] = 1, sv.p.up[2] = 1, sv.p.paint = 2;
  ui.screen = V_GARAGE, ui.sel = 0, ui.note[0] = 0;
  picture(dir, "garage");
  sv.p.mode = MODE_RACE, sv.p.track = 4, sv.p.best_ms[4] = 172400, sv.p.best_lap_ms[4] = 55100;
  ui.screen = V_SETUP, ui.sel = SET_START;
  picture(dir, "setup");
  sv.p.track = 0, sv.has_race = 0;
  memset(&sv.gp, 0, sizeof sv.gp);
  to_title();
  for (int i = 0; i < 500; i++) demo_prev = demo, tr_step(&demo, &track, 0, 0);
  picture(dir, "title");
}

int main(int argc, char **argv) {
  if (argc >= 3 && !strcmp(argv[1], "docs")) {
    docs(argv[2]);
    return 0;
  }
  if (argc >= 5 && !strcmp(argv[1], "film")) {
    film(argv[2], atoi(argv[3]), atoi(argv[4]), argc > 5 ? atoi(argv[5]) : 0);
    return 0;
  }
  if (argc >= 3 && !strcmp(argv[1], "test")) {
    test(argv[2]);
    return fails ? 1 : 0;
  }
  if (argc >= 3 && !strcmp(argv[1], "shots")) {
    shots(argv[2]);
    return 0;
  }
  fprintf(stderr, "usage: play test DIR | shots DIR | docs DIR | film DIR MS CIRCUIT [TIME]\n");
  return 2;
}
