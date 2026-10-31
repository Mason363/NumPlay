/* Bad Time Simulator on a computer, for tests and pictures (make host, make test).
 *
 *   play test DIR          every attack against a second reading of jcw87's scripts (expect.h), the heart's, the
 *                          damage's and the things' numbers against the spec, a bot through the whole fight to the
 *                          win (the same fight to the same hash on every computer), Practice, Endless and Single
 *                          attack, saves (kept, damaged, from the copy, storage full) and 300 sessions of the whole
 *                          app with random keys
 *   play quick DIR         only the attacks, the numbers and the saves
 *   play bot DIR           only the bot's fight (BOT_VERBOSE=1: attack by attack)
 *   play trace N ATTACK    N ticks of the bot in one attack, tick by tick
 *   play shots DIR         pictures of the screens (DIR/<name>.ppm)
 *   play film DIR N ATTACK N ticks of the bot playing an attack (DIR/<frame>_<ms>.raw, every other tick, for
 *                          tools/record.py --frames)
 */
#define main bt_app_main
#include "../src/main.c"
#undef main
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include "expect.h"

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
static uint32_t frame_no, quit_at, session_seed = 1;
uint32_t plat_keys(void) {
  clock_ms += 10;
  frame_no++;
  if (frame_no >= quit_at) return BK_HOME;
  return script ? script(frame_no) : 0;
}
uint32_t plat_millis(void) { return (uint32_t)clock_ms; }
void plat_sleep(uint32_t ms) { clock_ms += ms; }
uint32_t plat_random(void) { return 0x9E3779B9u ^ frame_no ^ session_seed; }
void plat_begin(void) {}
int plat_end(void) { return 0; }

static struct { char name[32]; uint8_t data[1024]; uint32_t len; bool on; } files[4];
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
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint32_t fnv(uint32_t h, const void *p, size_t n) {
  const uint8_t *b = p;
  for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 16777619u;
  return h;
}

/* ---------------------------------------------------------------- the attacks, against the scripts */
static uint32_t log_hash;
static int log_count;
static void log_line(const char *l) {
  if (log_count) log_hash = (log_hash ^ '\n') * 16777619u;
  log_hash = fnv(log_hash, l, strlen(l));
  log_count++;
}

static world_t W;
static void test_attacks(void) {
  for (int a = 0; a < N_ATTACKS; a++) {
    sim_reset(&W, 0x5EED0000u + (uint32_t)a);
    W.zl = W.tl = 33, W.zt = W.tt = 251, W.zr = W.tr = 608, W.zb = W.tb = 391;
    W.checking = 1, sim_log = log_line, log_hash = 2166136261u, log_count = 0;
    sim_start_attack(&W, a);
    W.tick = 0;
    while (!W.sc.ended && W.tick < 30 * 120) sim_tick(&W, 0, 0);
    uint32_t end = W.tick - 1;
    CHECK(end == EXPECT[a].end && (uint32_t)log_count == EXPECT[a].events && log_hash == EXPECT[a].hash,
          "%s: ends at %u (%u), %d things (%u), hash %08X (%08X)", atk_name[a], end, EXPECT[a].end, log_count,
          EXPECT[a].events, log_hash, EXPECT[a].hash);
  }
  sim_log = 0;
  printf("attacks: the %d against the scripts\n", N_ATTACKS);
}

/* ---------------------------------------------------------------- the numbers (spec/fight.md) */
static void fresh(world_t *w, int mode) {
  sim_reset(w, 1);
  w->zl = w->tl = 133, w->zt = w->tt = 251, w->zr = w->tr = 508, w->zb = w->tb = 391;
  w->sc.attack = -1;
  w->heart_on = w->heart_free = 1;
  sim_heart_mode(w, mode);
  w->hx = 320, w->hy = mode == HEART_BLUE ? 378 : 320;
}

static void test_numbers(void) {
  world_t *w = &W;
  /* the red heart: 150 px/s, 75 with shift; opposite keys cancel */
  fresh(w, HEART_RED);
  sim_tick(w, BK_RIGHT, BK_RIGHT);
  CHECK(w->hx == 325, "red heart: 5 px a tick (%g)", (double)w->hx);
  sim_tick(w, BK_RIGHT | BK_SLOW, 0);
  CHECK(w->hx == 327.5f, "slow: 2.5 px a tick (%g)", (double)w->hx);
  sim_tick(w, BK_LEFT | BK_RIGHT, 0);
  CHECK(w->hx == 327.5f && !w->moving, "left and right: still");
  for (int i = 0; i < 100; i++) sim_tick(w, BK_UP, 0);
  CHECK(w->hy == 251 + 13, "the wall: centre 13 px in (%g)", (double)w->hy);
  /* the blue heart: a jump of 180 px/s, then the gravity table; let go: 30 px/s up at most */
  fresh(w, HEART_BLUE);
  sim_tick(w, 0, 0);
  CHECK(w->hy == 378 && w->hvy == 0, "blue heart: standing (%g, %g)", (double)w->hy, (double)w->hvy);
  sim_tick(w, BK_UP, BK_UP);
  CHECK(w->hvy == -180 && w->hy == 372, "jump: -180 px/s (%g, %g)", (double)w->hvy, (double)w->hy);
  sim_tick(w, BK_UP, 0);
  CHECK(w->hvy == -174, "rising: 180 px/s^2 (%g)", (double)w->hvy);
  sim_tick(w, 0, 0);
  CHECK(w->hvy == -30 + 15, "let go: -30, then 450 px/s^2 (%g)", (double)w->hvy);
  /* a fall from rest: 180 then 540 px/s^2, no more past 240 px/s */
  fresh(w, HEART_BLUE);
  w->hy = 300;
  float v = 0, expect_v[40];
  for (int i = 0; i < 40; i++) {
    float g = v < 240 && v > 15 ? 540 : v <= 15 && v > -30 ? 180 : v <= -30 && v > -120 ? 450 : v <= -120 ? 180 : 0;
    v += g / 30.0f;
    if (v > 750) v = 750;
    expect_v[i] = v;
  }
  bool fall_ok = true;
  for (int i = 0; i < 12; i++) {
    sim_tick(w, 0, 0);
    if (w->hvy != 0 && w->hvy != expect_v[i]) fall_ok = false;
  }
  CHECK(fall_ok, "the blue heart's fall follows the gravity table");
  /* a slam: down at MaxFallSpeed; landing at 330 px/s or more shakes, and hurts with slam damage on */
  fresh(w, HEART_RED);
  w->hy = 280, w->max_fall = 750, w->slam_damage = 1;
  w->heart_mode = HEART_BLUE, w->heart_dir = DIR_DOWN, w->slammed = 1, w->hvy = 750;
  int hp = w->hp;
  for (int i = 0; i < 10; i++) sim_tick(w, 0, 0);
  CHECK(w->hy == 378 && w->hp == hp - 1 && !w->slammed, "slam: down, 1 HP (%g, %d)", (double)w->hy, w->hp);
  /* damage: 1 HP a tick inside a white bone, KR 6 the first tick, then 2 */
  fresh(w, HEART_RED);
  w->nbones = 1;
  memset(&w->bones[0], 0, sizeof w->bones[0]);
  w->bones[0] = (bone_t){310, 250, 20, 100, 0, 0, DIR_RIGHT, COL_WHITE, 6, 1};
  sim_tick(w, 0, 0);
  CHECK(w->hp == 91 && w->kr == 6, "a bone: -1 HP, KR 6 (%d, %d)", w->hp, w->kr);
  sim_tick(w, 0, 0);
  CHECK(w->hp == 90 && w->kr == 8, "again: -1 HP, KR +2 (%d, %d)", w->hp, w->kr);
  /* blue bones hurt only a moving heart, orange ones only a still one */
  fresh(w, HEART_RED);
  w->nbones = 1;
  w->bones[0] = (bone_t){300, 250, 60, 100, 0, 0, DIR_RIGHT, COL_BLUE, 6, 1};
  sim_tick(w, 0, 0);
  CHECK(w->hp == 92, "a blue bone, standing still: no harm");
  sim_tick(w, BK_RIGHT, BK_RIGHT);
  CHECK(w->hp == 91, "a blue bone, moving: harm");
  w->bones[0].color = COL_ORANGE;
  sim_tick(w, BK_LEFT, BK_LEFT);
  CHECK(w->hp == 91, "an orange bone, moving: no harm");
  sim_tick(w, 0, 0);
  CHECK(w->hp == 90, "an orange bone, standing still: harm");
  /* KR: capped at 40 and at HP - 1, drained 1 HP at a time: every 1, 2, 5, 15 or 30 ticks by KR */
  fresh(w, HEART_RED);
  w->heart_on = 0, w->kr = 40;
  int expect_ticks = 0, k = 40, t = 0;
  while (k > 0) {
    t++, expect_ticks++;
    int need = k >= 40 ? 1 : k >= 30 ? 2 : k >= 20 ? 5 : k >= 10 ? 15 : 30;
    if (t >= need) k--, t = 0;
  }
  int ticks = 0;
  while (w->kr > 0 && ticks < 2000) sim_tick(w, 0, 0), ticks++;
  CHECK(ticks == expect_ticks && w->hp == 52, "KR 40 drains in %d ticks (%d), 40 HP (%d)", expect_ticks, ticks, w->hp);
  fresh(w, HEART_RED);
  w->heart_on = 0, w->hp = 10, w->kr = 30;
  sim_tick(w, 0, 0);
  CHECK(w->kr <= 9, "KR below HP (%d)", w->kr);
  /* bones: speed / 30 px a tick, gone past the layout's edge */
  fresh(w, HEART_RED);
  w->heart_on = 0;
  w->nbones = 1;
  w->bones[0] = (bone_t){600, 250, 10, 50, 6, 0, DIR_RIGHT, COL_WHITE, 6, 1};
  sim_tick(w, 0, 0);
  CHECK(w->bones[0].x == 606, "a bone at 180 px/s: 6 px a tick");
  for (int i = 0; i < 5; i++) sim_tick(w, 0, 0);
  CHECK(w->nbones == 1, "still in the layout");
  sim_tick(w, 0, 0);
  CHECK(w->nbones == 0, "gone past x 640");
  /* the zone: 480 px/s an edge (16 a tick) */
  fresh(w, HEART_RED);
  w->heart_on = 0;
  sim_zone_to(w, 33, 251, 608, 391, true);
  sim_tick(w, 0, 0);
  CHECK(w->zl == 117 && w->zr == 524, "the zone: 16 px a tick (%g, %g)", (double)w->zl, (double)w->zr);
  /* a blaster: comes in (1/3 of the way a tick), waits `spin`, opens 0.1 s, fires; the beam hurts until blast +
   * 7/30 s, KR 10 */
  fresh(w, HEART_RED);
  w->hx = 320, w->hy = 320;
  w->nblasters = 1;
  memset(&w->blasters[0], 0, sizeof w->blasters[0]);
  blaster_t *b = &w->blasters[0];
  b->x = b->end_x = 100, b->y = b->end_y = 320, b->ang = b->end_ang = 0;
  b->size = 1, b->sx = b->sy = b->scale = 2, b->timer = 10, b->blast = 8, b->karma = 10, b->opacity = 100;
  int first = -1, last = -1;
  for (int i = 0; i < 60; i++) {
    int before = w->hits;
    sim_tick(w, 0, 0);
    if (w->hits != before) {
      if (first < 0) {
        first = i;
        CHECK(w->kr == 10, "a beam: KR 10 (%d)", w->kr);
      }
      last = i;
    }
  }
  CHECK(first == 10 + 3 && last == first + 8 + 6 - 1, "a beam: from tick %d (13) to %d (26)", first, last);
  printf("numbers: the heart, damage, KR, bones, the zone and blasters as the spec says\n");
}

/* ---------------------------------------------------------------- a bot that dodges by looking ahead */
static world_t probe;
static uint32_t bot_prev;

/* key schedules: a lateral direction held for `lat` ticks, a jump held for `jump` ticks */
typedef struct {
  uint32_t dir;
  uint8_t lat, jump, from;   /* the jump key held from tick `from` for `jump` ticks */
} plan_t;

static uint32_t plan_keys(const world_t *w, const plan_t *p, int t) {
  uint32_t k = t < p->lat ? p->dir : 0;
  if (w->heart_mode == HEART_BLUE && p->jump && t >= p->from && t < p->from + p->jump) {
    static const uint32_t JUMP[4] = {BK_LEFT, BK_UP, BK_RIGHT, BK_DOWN};
    k |= JUMP[w->heart_dir & 3];
  }
  return k;
}

#define HORIZON 24
static long score_plan(const world_t *w, const plan_t *p, uint32_t prev) {
  memcpy(&probe, w, sizeof probe);
  long score = 0;
  for (int t = 0; t < HORIZON; t++) {
    uint32_t k = plan_keys(w, p, t);
    uint16_t hits = probe.hits;
    sim_tick(&probe, k, k & ~prev);
    prev = k;
    if (probe.hits != hits) score += 1000L * (HORIZON - t);
    if (probe.sc.ended) break;
  }
  /* a little: the middle of the box, keeping still */
  float cx = (probe.zl + probe.zr) / 2, cy = (probe.zt + probe.zb) / 2;
  float d = (probe.hx - cx) * (probe.hx - cx) + (probe.hy - cy) * (probe.hy - cy);
  score += (long)(d / 400.0f);
  score += p->lat ? 2 : 0;
  return score;
}

static uint32_t bot_dodge(const world_t *w) {
  static plan_t plans[64];
  int n = 0;
  if (w->heart_mode == HEART_RED) {
    static const uint32_t DIRS[9] = {0, BK_LEFT, BK_RIGHT, BK_UP, BK_DOWN, BK_LEFT | BK_UP, BK_RIGHT | BK_UP,
                                     BK_LEFT | BK_DOWN, BK_RIGHT | BK_DOWN};
    for (int i = 0; i < 9; i++)
      for (int l = 0; l < 3; l++) plans[n++] = (plan_t){DIRS[i], (uint8_t)(i ? (l == 0 ? HORIZON : l == 1 ? 8 : 3) : 0), 0, 0};
  } else {
    bool across_x = w->heart_dir == DIR_DOWN || w->heart_dir == DIR_UP;
    uint32_t lat[3] = {0, across_x ? BK_LEFT : BK_UP, across_x ? BK_RIGHT : BK_DOWN};
    /* (a jump needs the key pressed again: from the next tick when it's held now) */
    static const uint8_t JUMP[7][2] = {{0, 0}, {0, 4}, {0, HORIZON}, {1, 4}, {1, HORIZON}, {4, 4}, {4, HORIZON}};
    for (int i = 0; i < 3; i++)
      for (int j = 0; j < 7; j++)
        for (int l = 0; l < (i ? 2 : 1); l++)
          plans[n++] = (plan_t){lat[i], (uint8_t)(i ? (l == 0 ? HORIZON : 6) : 0), JUMP[j][1], JUMP[j][0]};
  }
  long best = 0;
  int bi = 0;
  for (int i = 0; i < n; i++) {
    long s = score_plan(w, &plans[i], bot_prev);
    if (i == 0 || s < best) best = s, bi = i;
  }
  return plan_keys(w, &plans[bi], 0);
}

/* the keys for the next tick of a Normal fight: dodge, then FIGHT (or eat when low) */
static uint32_t bot_keys(const fight_t *f) {
  const world_t *w = &f->w;
  bool odd = f->w.tick & 1;
  switch (f->state) {
    case FS_ATTACK:
      if (f->text_kind == TXT_BUBBLE) return odd ? BK_OK : 0;
      if (w->heart_free && w->heart_on && w->sc.running) return bot_dodge(w);
      return 0;
    case FS_MENU: {
      int want = w->hp < 45 && f->nitems ? 2 : 0;
      if (f->sel != want) return odd ? BK_RIGHT : 0;
      return odd ? BK_OK : 0;
    }
    case FS_SUB: return odd ? BK_OK : 0;
    case FS_TARGET: {
      float mid = (w->zl + w->zr) / 2, d = f->bar_x - mid;
      return d > -20 && d < 20 ? BK_OK : 0;
    }
    case FS_INFO:
    case FS_WIN: return odd ? BK_OK : 0;
    default: return 0;
  }
}

static fight_t F;
/* a whole fight with the bot: its hash (every tick's state); won: 1, lost: 0 */
static int bot_fight(int mode, int arg, uint32_t seed, uint32_t *hash, uint32_t max_ticks, int *worst_hp) {
  fight_start(&F, mode, arg, seed);
  bot_prev = 0;
  uint32_t h = 2166136261u;
  int low = MAX_HP;
  static int verbose = -1;
  if (verbose < 0) verbose = getenv("BOT_VERBOSE") != 0;
  int last_state = -1, hp_in = 0;
  uint16_t hits_in = 0;
  for (uint32_t t = 0; t < max_ticks && !fight_over(&F); t++) {
    uint32_t k = bot_keys(&F);
    fight_tick(&F, k, k & ~bot_prev);
    bot_prev = k;
    if (verbose && F.state != last_state) {
      if (F.state == FS_ATTACK) hp_in = F.w.hp, hits_in = F.w.hits;
      if (last_state == FS_ATTACK)
        printf("  %-24s HP %2d -> %2d, %3d ticks hurt (tick %u)\n", atk_name[F.last_attack], hp_in, F.w.hp,
               F.w.hits - hits_in, (unsigned)F.w.tick);
      last_state = F.state;
    }
    int16_t v[4] = {F.w.hp, F.w.kr, (int16_t)F.state, (int16_t)F.last_attack};
    h = fnv(h, v, sizeof v);
    h = fnv(h, &F.w.hx, sizeof F.w.hx);
    h = fnv(h, &F.w.hy, sizeof F.w.hy);
    if (F.state == FS_ATTACK && F.w.hp < low) low = F.w.hp;
  }
  if (hash) *hash = h;
  if (worst_hp) *worst_hp = low;
  return F.state == FS_WON;
}

#define RUN_SEED 0xB0B0B0u
#define RUN_HASH 0xA1958315u
static void test_bot(void) {
  uint32_t h;
  int low;
  int won = bot_fight(MODE_NORMAL, 0, RUN_SEED, &h, 30 * 60 * 20, &low);
  CHECK(won, "the bot doesn't get through the fight (attack %d of 24, hit attempts %d, HP %d)", F.progress,
        F.hit_attempts, F.w.hp);
  CHECK(F.progress == 24, "the bot's furthest attack: %d", F.progress);
  printf("bot: %s the whole fight in %u attacks, HP never below %d, hash %08X\n", won ? "won" : "LOST",
         (unsigned)F.attacks_done, low, h);
  if (RUN_HASH) CHECK(h == RUN_HASH, "the fight's hash: %08X, not %08X (not the same on every computer?)", h, RUN_HASH);
  uint32_t h2;
  bot_fight(MODE_NORMAL, 0, RUN_SEED, &h2, 30 * 60 * 20, 0);
  CHECK(h == h2, "the same fight twice: %08X then %08X", h, h2);
}

static void test_modes(void) {
  /* Practice: every attack in order, then the final over and over */
  fight_start(&F, MODE_PRACTICE, 0, 99);
  bot_prev = 0;
  int seen[N_ATTACKS] = {0};
  for (uint32_t t = 0; t < 30 * 60 * 12 && !fight_over(&F); t++) {
    uint32_t k = bot_keys(&F);
    fight_tick(&F, k, k & ~bot_prev);
    bot_prev = k;
    if (F.state == FS_ATTACK) seen[F.last_attack] = 1;
    CHECK(F.w.hp > 0, "Practice: the heart can't break");
    if (F.w.hp <= 0) break;
  }
  int all = 1;
  for (int i = 0; i < N_ATTACKS; i++) all &= i == A_SPARE || seen[i];
  CHECK(all, "Practice: every attack comes");
  CHECK(F.last_attack == A_FINAL, "Practice: ends on the final attack (%s)", atk_name[F.last_attack]);
  /* Endless: phase 2 never reaches the final attack */
  fight_start(&F, MODE_ENDLESS, 1, 7);
  bot_prev = 0;
  for (uint32_t t = 0; t < 30 * 60 * 4 && !fight_over(&F); t++) {
    uint32_t k = bot_keys(&F);
    fight_tick(&F, k, k & ~bot_prev);
    bot_prev = k;
    CHECK(F.last_attack != A_FINAL && F.last_attack >= A_MULTI1, "Endless phase 2: the second half's attacks");
    if (F.last_attack == A_FINAL) break;
  }
  CHECK(F.attacks_done >= 10, "Endless: attacks back to back (%u)", (unsigned)F.attacks_done);
  /* Single attack: the same one again and again */
  fight_start(&F, MODE_SINGLE, A_BONESTAB2, 5);
  bot_prev = 0;
  for (uint32_t t = 0; t < 30 * 40 && !fight_over(&F); t++) {
    uint32_t k = bot_keys(&F);
    fight_tick(&F, k, k & ~bot_prev);
    bot_prev = k;
  }
  CHECK(F.last_attack == A_BONESTAB2 && F.attacks_done >= 2, "Single attack: again");
  /* the turns' order, as jcw87's StartAttack */
  fight_t g;
  memset(&g, 0, sizeof g);
  CHECK(fight_progress_of(0, 0) == 1 && fight_progress_of(5, 13) == 14 && fight_progress_of(13, 3) == 14 &&
            fight_progress_of(14, 0) == 15 && fight_progress_of(22, 9) == 23 && fight_progress_of(23, 0) == 24,
        "progress");
  printf("modes: Practice, Endless and Single attack\n");
}

/* ---------------------------------------------------------------- saves */
static void test_saves(void) {
  memset(files, 0, sizeof files);
  bt_progress p, q;
  CHECK(!save_load(&p) && p.best == 0 && p.wins == 0, "no save: a fresh start");
  p.best = 17, p.wins = 3, p.guide = 1, p.endless[1] = 12;
  CHECK(save_write(&p), "a save");
  memset(&q, 0, sizeof q);
  CHECK(save_load(&q) && !memcmp(&p, &q, sizeof p), "a save read back");
  uint32_t n;
  uint8_t *d = file_data(BT_SAVE_NAME, &n);
  CHECK(d && n == sizeof(bt_file), "badtime.sav: %u bytes", n);
  /* damaged ways: each is refused, then the copy brings it back */
  for (int k = 0; k < 6; k++) {
    save_write(&p);
    d = file_data(BT_SAVE_NAME, &n);
    if (k == 0) d[0] ^= 0xFF;                       /* not ours */
    if (k == 1) d[4] = 9;                           /* another version */
    if (k == 2) d[9] ^= 1;                          /* the checksum's wrong */
    if (k == 3) files[0].len = 10;                  /* cut short */
    if (k == 4) {                                   /* sums right, but best 99 */
      bt_file f;
      memcpy(&f, d, sizeof f);
      f.p.best = 99;
      f.sum = bt_sum(&f, offsetof(bt_file, sum));
      memcpy(d, &f, sizeof f);
    }
    if (k == 5) forget(BT_SAVE_NAME);               /* gone (a reinstall) */
    memset(&q, 0, sizeof q);
    CHECK(save_load(&q) && !memcmp(&p, &q, sizeof p), "damaged save %d: back from the copy", k);
    d = file_data(BT_SAVE_NAME, &n);
    CHECK(d && n == sizeof(bt_file) && bt_file_ok((const bt_file *)d), "damaged save %d: written back", k);
  }
  /* both broken: a fresh start, no crash */
  save_write(&p);
  d = file_data(BT_SAVE_NAME, &n), d[2] ^= 0x55;
  uint8_t *c = file_data(BT_COPY_NAME, &n);
  CHECK(c && n > 20, "badtime_saves.py");
  if (c) for (uint32_t i = 0; i < n; i++) if (c[i] == ':') c[i + 3] = '!';
  memset(&q, 0xAA, sizeof q);
  CHECK(!save_load(&q) && q.best == 0 && q.wins == 0, "both damaged: a fresh start");
  /* storage full: the game goes on */
  storage_full = true;
  CHECK(!save_write(&p), "storage full: not saved");
  storage_full = false;
  printf("saves: kept, damaged (6 ways) and back from the copy, storage full\n");
}

/* ---------------------------------------------------------------- the whole app, random keys */
static uint32_t rnd_state;
static uint32_t random_keys(uint32_t frame) {
  (void)frame;
  rnd_state ^= rnd_state << 13, rnd_state ^= rnd_state >> 17, rnd_state ^= rnd_state << 5;
  static uint32_t held;
  if ((rnd_state & 7) == 0) {
    static const uint32_t K[8] = {BK_LEFT, BK_RIGHT, BK_UP, BK_DOWN, BK_OK, BK_OK, BK_BACK, BK_SLOW};
    held ^= K[(rnd_state >> 3) & 7];
    if ((rnd_state >> 9 & 15) == 0) held = 0;
  }
  return held;
}

static void test_random(void) {
  int sessions = 300;
  for (int s = 0; s < sessions; s++) {
    memset(files, 0, sizeof files);
    if (s & 1) {   /* half of them past how to play */
      bt_progress p;
      bt_fresh(&p);
      p.guide = 1;
      save_write(&p);
    }
    rnd_state = 0x1234567u + (uint32_t)s * 7919u, session_seed = (uint32_t)s;
    script = random_keys, frame_no = 0, quit_at = 900 + (uint32_t)(s % 7) * 150, clock_ms = 0;
    bt_app_main();
    uint32_t n;
    const uint8_t *d = file_data(BT_SAVE_NAME, &n);
    CHECK(d && n == sizeof(bt_file) && bt_file_ok((const bt_file *)d), "session %d: the save's fine", s);
  }
  script = 0;
  printf("random keys: %d sessions, saves fine\n", sessions);
}

/* ---------------------------------------------------------------- pictures */
static const char *shots_dir;
static void run_bot_until(int mode, int arg, uint32_t seed, int (*stop)(const fight_t *), uint32_t max) {
  fight_start(&F, mode, arg, seed);
  bot_prev = 0;
  for (uint32_t t = 0; t < max && !fight_over(&F); t++) {
    if (stop(&F)) return;
    uint32_t k = bot_keys(&F);
    fight_tick(&F, k, k & ~bot_prev);
    bot_prev = k;
  }
}
static int stop_attack;
static uint32_t stop_tick;
static int at_attack_tick(const fight_t *f) {
  return f->state == FS_ATTACK && f->last_attack == stop_attack && f->w.sc.running && f->w.sc.attack >= 0 &&
         f->w.tick >= stop_tick;
}
static void snap_fight(const char *name, const char *title) {
  draw_fight(&F, title);
  shot(shots_dir, name);
}
static void snap_single(int attack, uint32_t ticks, const char *name) {
  fight_start(&F, MODE_SINGLE, attack, 42);
  bot_prev = 0;
  for (uint32_t t = 0; t < ticks; t++) {
    uint32_t k = bot_keys(&F);
    fight_tick(&F, k, k & ~bot_prev);
    bot_prev = k;
    F.w.hp = F.w.hp < 40 ? 40 : F.w.hp;
  }
  snap_fight(name, 0);
}

static void make_shots(const char *dir) {
  shots_dir = dir;
  /* the title */
  memset(files, 0, sizeof files);
  sv.best = 17, sv.wins = 1, view = V_TITLE, sel = 0, frame = 12;
  draw();
  shot(dir, "title");
  /* the fight: the intro's blue eye, the menu, the attacks */
  snap_single(A_INTRO, 30, "intro");
  snap_single(A_INTRO, 140, "gavels");
  snap_single(A_PLATFORMS2, 150, "platforms");
  snap_single(A_BONEGAP1, 70, "bonegap");
  snap_single(A_BLUEBONE, 40, "bluebone");
  snap_single(A_BONESTAB1, 40, "bonestab");
  snap_single(A_MULTI2, 120, "multi");
  snap_single(A_RANDOMBLASTER1, 70, "random");
  snap_single(A_PLATFORMBLASTER, 95, "platblast");
  snap_single(A_PLATFORMBLASTER, 130, "platblast2");
  snap_single(A_INTRO, 100, "intro2");
  snap_single(A_INTRO, 82, "sine");
  snap_single(A_MULTI1, 60, "multi1");
  snap_single(A_BONEGAP2, 120, "bonegap2");
  snap_single(A_FINAL, 300, "final");
  snap_single(A_FINAL, 880, "spiral");
  snap_single(A_FINAL, 1300, "final2");
  /* the menu, after the first attack */
  stop_attack = A_BONEGAP1, stop_tick = 0;
  run_bot_until(MODE_NORMAL, 0, 3, at_attack_tick, 30 * 60);
  /* (back to the menu before it) */
  fight_start(&F, MODE_NORMAL, 0, 3);
  bot_prev = 0;
  for (uint32_t t = 0; t < 30 * 60 && !(F.state == FS_MENU && F.text_shown == F.text_len); t++) {
    uint32_t k = bot_keys(&F);
    if (F.state == FS_MENU) k = 0;
    fight_tick(&F, k, k & ~bot_prev);
    bot_prev = k;
  }
  snap_fight("menu", 0);
  F.sel = 2;
  fight_tick(&F, BK_OK, BK_OK);
  for (int i = 0; i < 3; i++) fight_tick(&F, 0, 0);
  fight_tick(&F, BK_RIGHT, BK_RIGHT);
  snap_fight("items", 0);
  fight_tick(&F, BK_BACK, BK_BACK);
  F.sel = 0;
  fight_tick(&F, 0, 0);
  fight_tick(&F, BK_OK, BK_OK), fight_tick(&F, 0, 0), fight_tick(&F, BK_OK, BK_OK);
  for (int i = 0; i < 18 && F.state == FS_TARGET; i++) fight_tick(&F, 0, 0);
  snap_fight("target", 0);
  fight_tick(&F, BK_OK, BK_OK);
  for (int i = 0; i < 26; i++) fight_tick(&F, 0, 0);
  snap_fight("dodge", 0);
  /* the speech bubble */
  for (int i = 0; i < 400 && !(F.state == FS_ATTACK && F.text_kind == TXT_BUBBLE && F.text_shown == F.text_len); i++)
    fight_tick(&F, 0, 0);
  snap_fight("talk", 0);
  /* the heart breaks */
  snap_single(A_BONEGAP2, 60, "x");
  F.w.hp = 1;
  for (int i = 0; i < 200 && F.state != FS_DEATH; i++) fight_tick(&F, BK_UP, 0);
  for (int i = 0; i < 30; i++) fight_tick(&F, 0, 0);
  snap_fight("death", 0);
  /* the first words, the last words, a lost fight */
  fight_start(&F, MODE_NORMAL, 0, 3);
  for (int i = 0; i < 30; i++) fight_tick(&F, 0, 0);
  snap_fight("court", 0);
  fight_start(&F, MODE_NORMAL, 0, RUN_SEED);
  bot_prev = 0;
  for (uint32_t t = 0; t < 30 * 60 * 20 && F.state != FS_WIN; t++) {
    uint32_t k = bot_keys(&F);
    fight_tick(&F, k, k & ~bot_prev);
    bot_prev = k;
  }
  for (int i = 0; i < 40; i++) fight_tick(&F, 0, 0);
  snap_fight("tired", 0);
  F.state = FS_LOST, F.mode = MODE_NORMAL, F.progress = 17;
  memcpy(&fight, &F, sizeof F);
  view = V_END;
  draw();
  shot(dir, "lost");
  /* pause, how to play, the end */
  fight_start(&F, MODE_NORMAL, 0, 3);
  for (int i = 0; i < 60; i++) fight_tick(&F, 0, 0);
  memcpy(&fight, &F, sizeof F);
  view = V_PAUSE, sel = 2;
  draw();
  shot(dir, "pause");
  view = V_HELP, page = 0, first_run = 0;
  draw();
  shot(dir, "help");
  F.state = FS_WON, F.mode = MODE_NORMAL, sv.wins = 2, sv.best = 24;
  memcpy(&fight, &F, sizeof F);
  view = V_END;
  draw();
  shot(dir, "won");
  printf("shots in %s\n", dir);
}

/* ---------------------------------------------------------------- frames for a GIF */
static void film(const char *dir, uint32_t n, int attack) {
  fight_start(&F, attack >= 0 ? MODE_SINGLE : MODE_NORMAL, attack, 77);
  bot_prev = 0;
  char path[512];
  for (uint32_t t = 0; t < n; t++) {
    uint32_t k = bot_keys(&F);
    fight_tick(&F, k, k & ~bot_prev);
    bot_prev = k;
    if (F.w.hp < 30) F.w.hp = 30;
    if (t % 2) continue;   /* (15 pictures a second) */
    draw_fight(&F, 0);
    snprintf(path, sizeof path, "%s/%06u_%06u.raw", dir, t, t * 100 / 3);
    FILE *f = fopen(path, "wb");
    if (!f) exit(2);
    fwrite(fb, 2, 320 * 240, f);
    fclose(f);
  }
}

int main(int argc, char **argv) {
  if (argc > 2 && !strcmp(argv[1], "quick")) {
    test_attacks();
    test_numbers();
    test_saves();
    printf(fails ? "%d FAILED\n" : "all good\n", fails);
    return fails != 0;
  }
  if (argc > 3 && !strcmp(argv[1], "trace")) {
    fight_start(&F, MODE_SINGLE, atoi(argv[3]) % N_ATTACKS, 42);
    bot_prev = 0;
    for (uint32_t t = 0; t < (uint32_t)atoi(argv[2]); t++) {
      uint32_t k = bot_keys(&F);
      uint16_t h = F.w.hits;
      fight_tick(&F, k, k & ~bot_prev);
      bot_prev = k;
      printf("%4u st%d keys %02X heart %d dir %d (%7.2f %7.2f) v (%7.2f %7.2f) hp %d kr %d stabs %d%s\n", (unsigned)F.w.tick, F.state, k,
             F.w.heart_mode, F.w.heart_dir, (double)F.w.hx, (double)F.w.hy, (double)F.w.hvx, (double)F.w.hvy, F.w.hp,
             F.w.kr, F.w.nstabs, F.w.hits != h ? "  HIT" : "");
      if (F.w.hp < 30) F.w.hp = 92;
    }
    return 0;
  }
  if (argc > 2 && !strcmp(argv[1], "bot")) {
    test_bot();
    printf(fails ? "%d FAILED\n" : "all good\n", fails);
    return fails != 0;
  }
  if (argc > 2 && !strcmp(argv[1], "test")) {
    test_attacks();
    test_numbers();
    test_saves();
    test_modes();
    test_bot();
    test_random();
    printf(fails ? "%d FAILED\n" : "all good\n", fails);
    return fails != 0;
  }
  if (argc > 2 && !strcmp(argv[1], "shots")) {
    make_shots(argv[2]);
    return 0;
  }
  if (argc > 4 && !strcmp(argv[1], "film")) {
    film(argv[2], (uint32_t)atoi(argv[3]), atoi(argv[4]));
    return 0;
  }
  printf("usage: play test DIR | shots DIR | film DIR TICKS ATTACK\n");
  return 2;
}
