/* Jetpack Joyride on a computer, for tests and pictures (make host, make test).
 *
 *   play test           the rules against the original's numbers (the spec), a bot's runs (no broken state, every
 *                       hazard and vehicle met), saves written and read back exactly, damaged saves refused, and a few
 *                       hundred sessions of the whole app with random keys, quitting with Home each time
 *   play shots DIR      pictures of the screens and of each hazard and vehicle (DIR/<name>.ppm)
 *   play film DIR S A N N frames of a bot's run (seed S), every third step, after A (DIR/f_NNNN.ppm)
 */
#define main jj_app_main
#include "../src/main.c"
#undef main
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

/* ---------------------------------------------------------------- the calculator, on a computer */
static uint16_t fb[SCREEN_H][SCREEN_W];
void np_push(int x, int y, int w, int h, const color_t *px) {
  for (int j = 0; j < h; j++)
    for (int i = 0; i < w; i++) fb[y + j][x + i] = px[j * w + i];
}
void *np_alloc(uint32_t size) { return malloc(size); }   /* (gfx_init's; the game has its own strip) */
static double clock_ms;
static uint32_t (*script)(uint32_t frame);
static uint32_t frame_no, quit_at;
uint32_t plat_keys(void) {
  clock_ms += 1000.0 / 30;   /* the app's loop at 30 a second */
  frame_no++;
  if (frame_no >= quit_at) return JK_HOME;
  return script ? script(frame_no) : 0;
}
uint32_t plat_millis(void) { return (uint32_t)clock_ms; }
void plat_sleep(uint32_t ms) { clock_ms += ms; }
uint32_t plat_random(void) { return 0x9E3779B9u ^ frame_no; }
void plat_begin(void) {}
int plat_end(void) { return 0; }

/* files: a few named records in memory */
static struct { char name[32]; uint8_t data[4096]; uint32_t len; bool on; } files[4];
bool plat_save(const char *name, const void *data, uint32_t len) {
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

static void shot(const char *path) {
  FILE *f = fopen(path, "wb");
  if (!f) exit(2);
  fprintf(f, "P6 320 240 255\n");
  for (int y = 0; y < SCREEN_H; y++)
    for (int x = 0; x < SCREEN_W; x++) {
      uint16_t c = fb[y][x];
      fputc((c >> 11) * 255 / 31, f), fputc((c >> 5 & 63) * 255 / 63, f), fputc((c & 31) * 255 / 31, f);
    }
  fclose(f);
}

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* ---------------------------------------------------------------- a bot: plans (hold n, let go m) tried ahead */
static bool greedy;   /* the film's bot also goes for coins, tokens and vehicles */
static int survive(const jj_run *r0, int hold, int rel, int horizon) {
  static jj_run r;
  r = *r0;
  for (int f = 0; f < horizon; f++) {
    int cyc = hold + rel, p = cyc ? f % cyc : 0;
    jj_step(&r, p < hold);
    if (r.state != JJ_ALIVE) return f * 10;
  }
  return horizon * 10 + (greedy ? (int)(r.coins - r0->coins) + 30 * (r.tokens - r0->tokens) + 40 * (r.ride != r0->ride) : 0);
}
static int bot_h, bot_r, bot_left;
static bool bot(const jj_run *r) {
  if (bot_left <= 0) {
    static const int plans[][2] = {{0, 1}, {1, 0}, {2, 2}, {4, 4}, {6, 3}, {3, 6}, {8, 2}, {2, 8}, {1, 12}};
    int best = -1;
    for (unsigned k = 0; k < sizeof plans / sizeof plans[0] && (greedy || best < 1000); k++) {
      int v = survive(r, plans[k][0], plans[k][1], 100);
      if (v > best) best = v, bot_h = plans[k][0], bot_r = plans[k][1];
    }
    bot_left = 5;
  }
  bot_left--;
  int cyc = bot_h + bot_r, p = cyc ? (int)(r->t % (uint32_t)cyc) : 0;
  return p < bot_h;
}

/* ---------------------------------------------------------------- the rules against the spec */
static void units(void) {
  jj_run r;
  jj_start(&r, 7, 0, 0);
  /* falling and flying settle at gravity/(1-friction) and (boost-gravity)/(1-friction) points a frame */
  r.y = 200, r.vy = 0, r.ground = 0;
  float v = 0;
  for (int i = 0; i < 400; i++) v = (v + JJ_GRAVITY) * JJ_FRICTION;
  CHECK(fabsf(v - 7.35f) < 0.05f, "terminal fall %f", v);
  v = 0;
  for (int i = 0; i < 400; i++) v = (v + JJ_GRAVITY - JJ_BOOST) * JJ_FRICTION;
  CHECK(fabsf(v + 10.29f) < 0.05f, "terminal climb %f", v);
  /* held from the floor, Barry reaches the ceiling and stays under it */
  jj_start(&r, 7, 0, 0);
  for (int i = 0; i < 120; i++) {
    float before = r.y;
    r.safe = 1000;
    jj_step(&r, true);
    CHECK(r.y <= before + 0.001f || i == 0, "climbing");
  }
  CHECK(fabsf(r.y - (JJ_CEIL + jj_box_h(JJ_FOOT))) < 0.01f, "ceiling %f", r.y);
  for (int i = 0; i < 200; i++) r.safe = 1000, jj_step(&r, false);
  CHECK(r.y == JJ_FLOOR && r.ground, "floor %f", r.y);
  /* speed: 200 points a second at the start, 505 from 2000 m, linear between */
  jj_start(&r, 7, 0, 0);
  CHECK(fabsf(jj_speed(&r) - 200) < 0.01f, "start speed");
  r.dist = 1000u * 16 * JJ_DIST_DIV;
  CHECK(fabsf(jj_speed(&r) - 352.5f) < 0.01f, "speed at 1000 m %f", jj_speed(&r));
  r.dist = 2500u * 16 * JJ_DIST_DIV;
  CHECK(fabsf(jj_speed(&r) - 505) < 0.01f, "speed at 2500 m");
  /* a head start starts at 500 m */
  jj_start(&r, 7, 0, 1);
  CHECK(jj_metres(&r) == 500, "head start");
  /* every laser shot ends inside its pattern; every pattern fires */
  for (int p = 0; p < JJ_LASER_PATTERNS; p++) {
    int fired = 0;
    for (int b = 0; b < jj_lpats[p].count; b++)
      for (int t = 0; t <= jj_lpats[p].time + 20; t++) {
        int y, ph, k;
        jj_beam_state(p, b, t, &y, &ph, &k);
        if (ph == 2) {
          fired = 1;
          CHECK(t < jj_lpats[p].time, "laser %d beam %d fires at %d", p, b, t);
        }
        CHECK(y >= JJ_MID - 127 && y <= JJ_MID + 127, "laser height");
      }
    CHECK(fired, "laser pattern %d never fires", p);
  }
  /* coin patterns: the files' coins (coinpattern1.txt has 41) */
  {
    jj_coinset c = {0};
    int n = 0;
    c.pat = 0, memcpy(c.bits, jj_pat_bits + jj_pat_off[0], sizeof c.bits);
    for (int row = 0; row < jj_pat_h[0]; row++)
      for (int col = 0; col < jj_pat_w[0]; col++) n += jj_coin_at(&c, col, row);
    CHECK(n == 41, "coinpattern1 has %d coins", n);
  }
  /* the Final Spin: weights as slots.xml; a Second Chance, once won, is 40 times rarer; double coins and the head
     start come once */
  {
    jj_spin s;
    jj_spin_start(&s);
    CHECK(s.weight[JJ_PRIZE_REVIVE] == 100 && s.weight[JJ_PRIZE_START_500M] == 200, "spin weights");
    uint32_t g = 12345;
    int wins = 0, n = 40000, dc = 0;
    for (int i = 0; i < n; i++) {
      jj_spin_start(&s);
      int p = jj_spin_roll(&s, &g);
      if (p >= 0) wins++;
      if (p == JJ_PRIZE_DOUBLE_COINS) {
        dc++;
        CHECK(s.weight[JJ_PRIZE_DOUBLE_COINS] == 0, "double coins once");
      }
      if (p == JJ_PRIZE_REVIVE) CHECK(s.weight[JJ_PRIZE_REVIVE] == 2, "revive weight after a win %d", s.weight[JJ_PRIZE_REVIVE]);
    }
    CHECK(wins > n / 5 && wins < n * 3 / 10, "one spin in four wins: %d of %d", wins, n);
    CHECK(dc > 0, "double coins never won");
  }
  /* a fresh run is valid, and broken ones are refused */
  jj_start(&r, 99, 0, 0);
  CHECK(jj_valid(&r), "fresh run invalid");
  jj_run b = r;
  b.y = NAN;
  CHECK(!jj_valid(&b), "NaN accepted");
  b = r, b.coin[0].on = 1, b.coin[0].pat = 200;
  CHECK(!jj_valid(&b), "bad pattern accepted");
}

/* ---------------------------------------------------------------- what players reported (#222) */
static void calm(jj_run *r) { r->next = 1e6f, r->vehicle_at = 0xffffffffu, r->token_last = 0xffffffffu; }

static void reported(void) {
  /* a zapper hits what its electricity touches, not the air beside it: a diagonal one whose beam passes 8 points
     from Barry's top corner misses him (the old square test hit him there), one 3 points from it hits */
  for (int d = 8; d >= 3; d -= 5) {
    jj_run r;
    jj_start(&r, 5, 0, 0);
    calm(&r);
    float k = (float)d / 1.41421356f;   /* Barry's box: 120..136 by 260..290; its corner (136, 260) */
    jj_zapper *z = &r.zap[0];
    z->on = 1, z->ang = 8192, z->len = 120;   /* 45 degrees, down to the right */
    z->x = 136 + k + jj_speed(&r) / 60.0f, z->y = 260 - k;
    jj_step(&r, false);
    CHECK((r.state == JJ_ALIVE) == (d == 8), "diagonal zapper %d points from Barry: %s", d, r.state == JJ_ALIVE ? "missed" : "hit");
  }
  /* the jetpack's bullets knock out the scientists they meet, however high Barry flies; a scientist that's not under
     them walks on */
  for (int firing = 0; firing < 2; firing++) {
    jj_run r;
    jj_start(&r, 9, 0, 0);
    calm(&r);
    r.y = 80, r.ground = 0;
    jj_scientist *c = &r.sci[0];
    c->on = 1, c->x = 300, c->y = JJ_FLOOR, c->vx = 0.5f, c->dir = -1, c->state = JJ_SCI_WALK, c->alert = 1, c->near = 100;
    int bullets = 0;
    for (int i = 0; i < 150 && c->on; i++) {
      jj_step(&r, firing != 0);
      for (int b = 0; b < JJ_BULLETS; b++) bullets += r.bul[b].vy != 0;
      if (r.y > 100) r.y = 80;
    }
    bool down = c->state == JJ_SCI_AIR || c->state == JJ_SCI_SLIDE;
    CHECK(down == (firing != 0), "a scientist under Barry at the ceiling, %s: state %d", firing ? "firing" : "not firing", c->state);
    CHECK((bullets > 0) == (firing != 0), "bullets %d", bullets);
  }
  /* bullets come from the jetpack's barrels, 10 points behind and 28 under Barry's middle (weapons.xml), at 15 points a
     frame within 17.5 degrees of straight down (185) */
  {
    jj_run r;
    jj_start(&r, 21, 0, 0);
    calm(&r);
    r.y = 150, r.ground = 0;
    jj_step(&r, true);
    const jj_bullet *b = &r.bul[0];
    float dx = jj_speed(&r) / 60.0f, ox = b->x - (b->vx - dx), oy = b->y - b->vy;
    CHECK(b->vy != 0 && fabsf(ox - (JJ_BX - 6)) < 0.01f && fabsf(oy - (r.y + 12)) < 0.01f, "bullet from %f %f", ox, oy);
    CHECK(fabsf(sqrtf(b->vx * b->vx + b->vy * b->vy) - 15) < 0.1f && b->vx / b->vy > -0.33f && b->vx / b->vy < 0.23f, "bullet speed and angle");
  }
  /* knocked out, his body spins while it's in the air, then he slides to a stop on his front */
  {
    jj_run r;
    jj_start(&r, 33, 0, 0);
    calm(&r);
    r.y = 200, r.ground = 0;
    jj_missile *m = &r.mis[0];
    m->on = 1, m->state = JJ_FLY, m->x = JJ_BX + 20, m->y = 185;
    jj_step(&r, false);
    CHECK(r.state == JJ_TUMBLE && r.spin >= 8 * 65536 / 360 && r.spin <= 18 * 65536 / 360 + 1, "spin %d", r.spin);
    uint16_t rot = r.rot;
    int turned = 0, frames = 0;
    while (!jj_over(&r) && frames < 3000) {
      jj_step(&r, false), frames++;
      if (r.y < JJ_FLOOR && r.rot != rot) turned++;
      rot = r.rot;
    }
    CHECK(jj_over(&r) && turned > 10, "never came to rest or never turned (%d)", turned);
  }
  /* the rooms change: every run starts in the lab, then the tube, the cave and the greenhouse come in an order of its
     own, never the same room twice running; the same run always has the same rooms */
  {
    int seen[4] = {0}, differ = 0, twice = 0;
    char prev[64] = {0};
    for (uint32_t k = 1; k <= 40; k++) {
      jj_run r;
      jj_start(&r, k * 2654435761u, 0, 0);
      CHECK(jj_room_at(r.level, 0) == 0 && jj_room_at(r.level, 600) == 0, "run %u doesn't start in the lab", k);
      char order[64];
      int n = 0, last = 0, lastroom = -1;
      for (int x = 0; x < 40000 && n < 63; x += 60) {
        int room = jj_room_at(r.level, x);
        if (room != last) {
          if (room) {
            order[n++] = (char)('0' + room);
            if (room == lastroom) twice++;
            lastroom = room;
          }
          last = room;
        }
        seen[room]++;
      }
      order[n] = 0;
      differ += strcmp(order, prev) != 0;
      memcpy(prev, order, sizeof prev);
      CHECK(jj_room_at(r.level, 12345) == jj_room_at(r.level, 12345) && jj_room_at(r.level ^ 1, 0) == 0, "rooms");
      int again = jj_room_at(r.level, 20000);
      jj_room_at(r.level, 0);
      CHECK(jj_room_at(r.level, 20000) == again, "the same run, other rooms");
    }
    CHECK(seen[1] && seen[2] && seen[3] && seen[0], "rooms seen: lab %d tube %d cave %d greenhouse %d", seen[0], seen[1], seen[2], seen[3]);
    CHECK(differ >= 30 && !twice, "room orders: %d of 40 differ, %d repeats", differ, twice);
    printf("reported: zappers by their glow, bullets for real, the spinning fall, rooms (%d different orders in 40 runs)\n", differ);
  }
}

/* ---------------------------------------------------------------- the bot's runs */
static void bots(void) {
  int seen_ride[JJ_RIDES] = {0}, lasers = 0, missiles = 0, rotating = 0, tokens = 0, swarm = 0;
  uint32_t total_m = 0;
  for (int s = 1; s <= 24; s++) {
    jj_run r;
    jj_start(&r, (uint32_t)s * 2654435761u, s % 5 == 0, 0);
    bot_left = 0, greedy = s % 2 == 0;   /* half of them go for coins and tokens too */
    while (r.state == JJ_ALIVE && jj_metres(&r) < 5000) {
      jj_step(&r, bot(&r));
      seen_ride[r.ride] = 1;
      if (r.laser) lasers++;
      if (r.set == JJ_SET_MISSILE) swarm++;
      for (int i = 0; i < JJ_MISSILES; i++) missiles += r.mis[i].on;
      for (int i = 0; i < JJ_ZAPPERS; i++) rotating += r.zap[i].on && r.zap[i].spin;
      if (r.t % 60 == 0) CHECK(jj_valid(&r), "seed %d: invalid at %u", s, r.t);
    }
    for (int i = 0; i < 6000 && !jj_over(&r); i++) jj_step(&r, false);
    CHECK(jj_over(&r) || r.state == JJ_ALIVE, "seed %d: never came to rest", s);
    CHECK(jj_valid(&r), "seed %d: invalid at the end", s);
    tokens += r.tokens_taken;
    total_m += jj_metres(&r);
  }
  greedy = false;
  for (int v = 0; v < JJ_RIDES; v++) CHECK(seen_ride[v], "vehicle %d never ridden", v);
  CHECK(lasers && missiles && rotating && swarm, "hazards: lasers %d missiles %d rotating %d swarm %d", lasers, missiles, rotating, swarm);
  CHECK(tokens > 12, "tokens %d", tokens);
  printf("bot: 24 runs, %u m in all, %d tokens\n", total_m, tokens);
}

/* ---------------------------------------------------------------- a way past every zapper (issue #271)
 * Where the bot dies on a zapper on foot, every way of holding and letting go from a second and a half before is
 * tried (as many as differ in Barry's height and speed): in the runs of 60 seeds, at most 2 of those deaths may have
 * had no way through (from a spot the bot got into for other things: a missile, coins). */
#define BEAM 600
static jj_run beam_now[BEAM], beam_next[2 * BEAM];
static bool way_through(const jj_run *from, int frames) {
  int n = 1;
  beam_now[0] = *from;
  for (int f = 0; f < frames; f++) {
    int m = 0;
    for (int i = 0; i < n; i++)
      for (int h = 0; h < 2; h++) {
        jj_run t = beam_now[i];
        jj_step(&t, h);
        if (t.state != JJ_ALIVE) continue;
        bool same = false;
        for (int j = 0; j < m && !same; j++)
          same = fabsf(beam_next[j].y - t.y) < 0.75f && fabsf(beam_next[j].vy - t.vy) < 0.15f && beam_next[j].ride == t.ride;
        if (!same && m < 2 * BEAM) beam_next[m++] = t;
      }
    if (!m) return false;
    n = m > BEAM ? BEAM : m;
    for (int j = 0; j < n; j++) beam_now[j] = beam_next[m > BEAM ? (long)j * m / BEAM : j];
  }
  return true;
}
static void zapper_ways(void) {
  static jj_run hist[128];
  int deaths = 0, none = 0;
  for (int s = 1; s <= 60; s++) {
    jj_run r;
    jj_start(&r, (uint32_t)s * 2654435761u, 0, 0);
    bot_left = 0;
    unsigned k = 0;
    while (r.state == JJ_ALIVE && jj_metres(&r) < 6000) hist[k++ & 127] = r, jj_step(&r, bot(&r));
    if (r.state == JJ_ALIVE || r.by != JJ_BY_ZAPPER || k < 100 || hist[(k - 90) & 127].ride != JJ_FOOT) continue;
    deaths++;
    none += !way_through(&hist[(k - 90) & 127], 150);
  }
  CHECK(none <= 2, "zappers with no way past: %d of %d deaths on them", none, deaths);
  printf("zappers: %d deaths on them in 60 runs, %d with no way past\n", deaths, none);
}

/* ---------------------------------------------------------------- saves */
static void saves(void) {
  jj_run r, back;
  jj_profile p = {1234, 567, 8, 1, 1, 0, 0}, q;
  jj_start(&r, 4242, 0, 0);
  bot_left = 0;
  for (int i = 0; i < 3000 && r.state == JJ_ALIVE; i++) jj_step(&r, bot(&r));
  forget(JJ_SAVE_NAME), forget(JJ_COPY_NAME);
  CHECK(save_write(&p, &r), "save failed");
  memset(&back, 0, sizeof back);
  save_load(&q, &back);
  CHECK(q.best == 1234 && q.coins == 567 && q.has_run == 1, "profile back");
  CHECK(!memcmp(&back, &r, sizeof r), "run back");
  /* the same run goes on the same way */
  for (int i = 0; i < 600; i++) {
    bool h = bot(&r);
    jj_step(&r, h), jj_step(&back, h);
  }
  CHECK(!memcmp(&back, &r, sizeof r), "run diverged after loading");
  /* the file gone: the copy in the script brings it back */
  forget(JJ_SAVE_NAME);
  memset(&q, 0, sizeof q);
  save_load(&q, &back);
  CHECK(q.best == 1234 && q.has_run, "copy back");
  uint32_t n;
  CHECK(plat_load(JJ_SAVE_NAME, &n) != 0, "save restored from the copy");
  /* every byte damaged in turn: refused (or, by chance, still valid), never a crash */
  forget(JJ_COPY_NAME);
  save_write(&p, &r);
  uint8_t keep[4096];
  const uint8_t *d = plat_load(JJ_SAVE_NAME, &n);
  memcpy(keep, d, n);
  forget(JJ_COPY_NAME);
  int refused = 0;
  for (uint32_t i = 0; i < n; i++) {
    uint8_t bad[4096];
    memcpy(bad, keep, n);
    bad[i] ^= (uint8_t)(1 + i % 255);
    plat_save(JJ_SAVE_NAME, bad, n);
    forget(JJ_COPY_NAME);
    memset(&q, 0xAB, sizeof q);
    save_load(&q, &back);
    refused += q.best == 0 && q.has_run == 0;
    if (q.has_run) CHECK(jj_valid(&back), "damaged save gave a broken run");
  }
  CHECK(refused == (int)n, "damaged saves refused: %d of %u", refused, n);
  /* too short, too long, empty */
  plat_save(JJ_SAVE_NAME, keep, n - 1), save_load(&q, &back);
  CHECK(q.best == 0, "short save");
  /* a version 1 save (1.16, 1.17): its records come back, its run's coins are banked and its distance counts for the
     best, and it's written again in this version; from the file or from the copy in the script */
  for (int from_copy = 0; from_copy < 2; from_copy++) {
    uint8_t v1[JJ_V1_SIZE] = {0};
    uint32_t magic = JJ_MAGIC, dist = 1500u * 16 * 18, coins = 77, sum;
    uint16_t ver = 1, size = JJ_V1_SIZE;
    jj_profile old = {1000, 500, 3, 1, 0, 0, 1};
    memcpy(v1, &magic, 4), memcpy(v1 + 4, &ver, 2), memcpy(v1 + 6, &size, 2), memcpy(v1 + 8, &old, sizeof old);
    memcpy(v1 + JJ_V1_RUN + 40, &dist, 4), memcpy(v1 + JJ_V1_RUN + 44, &coins, 4);
    sum = jj_sum(v1, JJ_V1_SIZE - 4), memcpy(v1 + JJ_V1_SIZE - 4, &sum, 4);
    forget(JJ_SAVE_NAME), forget(JJ_COPY_NAME);
    if (from_copy) {
      static char txt[2048];
      static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
      int o = snprintf(txt, sizeof txt, "#>%s:", JJ_SAVE_NAME);
      for (int i = 0; i < JJ_V1_SIZE; i += 3) {
        int left = JJ_V1_SIZE - i;
        uint32_t v = (uint32_t)v1[i] << 16 | (left > 1 ? (uint32_t)v1[i + 1] << 8 : 0) | (left > 2 ? v1[i + 2] : 0);
        txt[o++] = b64[v >> 18 & 63], txt[o++] = b64[v >> 12 & 63];
        txt[o++] = left > 1 ? b64[v >> 6 & 63] : '=', txt[o++] = left > 2 ? b64[v & 63] : '=';
      }
      txt[o++] = '\n';
      plat_save(JJ_COPY_NAME, txt, (uint32_t)o);
    } else {
      plat_save(JJ_SAVE_NAME, v1, JJ_V1_SIZE);
    }
    memset(&q, 0xAB, sizeof q);
    save_load(&q, &back);
    CHECK(q.best == 1500 && q.coins == 577 && q.runs == 3 && q.guide_seen == 1 && !q.has_run, "version 1 save (%s): best %u coins %u",
          from_copy ? "copy" : "file", q.best, q.coins);
    const uint8_t *d2 = plat_load(JJ_SAVE_NAME, &n);
    jj_file f2;
    CHECK(d2 && n == sizeof f2, "version 1 save not written again");
    if (d2 && n == sizeof f2) {
      memcpy(&f2, d2, sizeof f2);
      CHECK(jj_file_ok(&f2) && f2.p.coins == 577, "rewritten save");
    }
  }
  printf("saves: %u bytes, every damaged byte refused, version 1 saves read\n", (unsigned)sizeof(jj_file));
}

/* ---------------------------------------------------------------- the whole app with random keys */
static uint32_t fuzz_rng = 77;
static uint32_t fuzz_keys(uint32_t f) {
  static uint32_t held, until;
  if (f >= until) {
    uint32_t x = jj_rand(&fuzz_rng);
    static const uint32_t pick[] = {0, JK_OK, JK_OK, JK_OK, JK_UP, JK_DOWN, JK_BACK, JK_OK | JK_UP, 0, JK_OK};
    held = pick[x % 10];
    until = f + 1 + (x >> 8) % 40;
  }
  return held;
}
static void fuzz(void) {
  int sessions = 200;
  for (int s = 0; s < sessions; s++) {
    if (s % 50 == 0) forget(JJ_SAVE_NAME), forget(JJ_COPY_NAME);
    memset(&run, 0, sizeof run), memset(&prof, 0, sizeof prof), memset(&view, 0, sizeof view);
    fuzz_rng = (uint32_t)s * 7919u + 1;
    script = fuzz_keys, frame_no = 0, quit_at = 100 + jj_rand(&fuzz_rng) % 1200;
    jj_app_main();
    uint32_t n;
    const uint8_t *d = plat_load(JJ_SAVE_NAME, &n);
    CHECK(d && n == sizeof(jj_file), "session %d: no save", s);
    if (d) {
      jj_file f;
      memcpy(&f, d, sizeof f);
      CHECK(jj_file_ok(&f), "session %d: the save doesn't read back", s);
    }
  }
  printf("fuzz: %d sessions of random keys, each quit with Home and its save read back\n", sessions);
}

/* ---------------------------------------------------------------- the Final Spin, through the app */
static int spin_wins, spin_losses, spin_revives;
static int8_t last_msg;
static uint8_t last_screen;
static uint32_t spin_keys(uint32_t f) {
  if (view.spin_msg != last_msg) {
    if (view.spin_msg >= 0) spin_wins++;
    if (view.spin_msg == -2) spin_losses++;
  }
  if (last_screen == JJ_SCREEN_SPIN && view.screen == JJ_SCREEN_PLAY) spin_revives++;
  last_msg = view.spin_msg, last_screen = view.screen;
  if (f == 10) return JK_BACK;                       /* resume the run left with Home */
  return (f % 8) < 2 && f > 20 ? JK_OK : 0;          /* then OK: spins, results, play again... */
}
static void spins(void) {
  jj_run end;
  for (uint32_t seed = 3;; seed++) {   /* a run to its end */
    jj_start(&end, seed * 40503u, 0, 0);
    bot_left = 0;
    while (end.state == JJ_ALIVE) jj_step(&end, bot(&end));
    break;
  }
  for (int i = 0; i < 5000 && end.state != JJ_STOPPED; i++) jj_step(&end, false);
  int sessions = 60;
  spin_wins = spin_losses = spin_revives = 0;
  for (int s = 0; s < sessions; s++) {
    jj_profile p = {100, 1000, (uint32_t)s * 17, 1, 0, 0, 1};
    end.tokens = 6;
    forget(JJ_SAVE_NAME), forget(JJ_COPY_NAME);
    save_write(&p, &end);
    memset(&run, 0, sizeof run), memset(&prof, 0, sizeof prof), memset(&view, 0, sizeof view);
    script = spin_keys, frame_no = 0, quit_at = 900, last_msg = -1, last_screen = 0;
    jj_app_main();
    uint32_t n;
    const uint8_t *d = plat_load(JJ_SAVE_NAME, &n);
    jj_file f;
    CHECK(d && n == sizeof f, "spin session %d: no save", s);
    if (!d) continue;
    memcpy(&f, d, sizeof f);
    CHECK(jj_file_ok(&f), "spin session %d: the save doesn't read back", s);
    /* the tokens were all spent or cashed in: the coins grew, or a run came back */
    CHECK(f.p.coins > 1000 || f.p.has_run || f.p.head_start || f.p.double_coins, "spin session %d: nothing happened", s);
  }
  int spins = spin_wins + spin_losses;
  CHECK(spins > sessions * 5 && spin_wins * 5 > spins && spin_wins * 3 < spins, "spins %d, wins %d", spins, spin_wins);
  CHECK(spin_revives > 0, "no Second Chance brought a run back");
  printf("final spin: %d spins, %d won, %d Second Chances back to the run\n", spins, spin_wins, spin_revives);
}

/* ---------------------------------------------------------------- pictures */
static void picture(const char *dir, const char *name) {
  char path[512];
  snprintf(path, sizeof path, "%s/%s.ppm", dir, name);
  sync_view();
  jj_draw(&run, &view);
  shot(path);
}

/* the bot plays until `want` is true, then a picture */
static bool until_seen(uint32_t seed, bool (*want)(const jj_run *), int max_frames) {
  jj_start(&run, seed, 0, 0);
  bot_left = 0;
  for (int i = 0; i < max_frames && run.state == JJ_ALIVE; i++) {
    jj_step(&run, bot(&run));
    if (want(&run)) return true;
  }
  return false;
}
static bool w_laser(const jj_run *r) {
  if (!r->laser) return false;
  int y, ph, k;
  jj_beam_state(r->laser - 1, 0, r->laser_t, &y, &ph, &k);
  return ph == 2 && k > 10 && jj_lpats[r->laser - 1].count >= 3;
}
static bool w_missile(const jj_run *r) {
  for (int i = 0; i < JJ_MISSILES; i++)
    if (r->mis[i].on && r->mis[i].state == JJ_FLY && r->mis[i].x < 380 && r->mis[i].x > 250) return true;
  return false;
}
static bool w_warn(const jj_run *r) {
  int n = 0;
  for (int i = 0; i < JJ_MISSILES; i++) n += r->mis[i].on && r->mis[i].state != JJ_FLY;
  return n >= 1 && r->coin[0].on;
}
static bool w_rotating(const jj_run *r) {
  for (int i = 0; i < JJ_ZAPPERS; i++)
    if (r->zap[i].on && r->zap[i].spin && r->zap[i].x > 200 && r->zap[i].x < 380) return true;
  return false;
}
static bool w_coins(const jj_run *r) {
  for (int i = 0; i < JJ_COINSETS; i++)
    if (r->coin[i].on && r->coin[i].x < 300 && r->coin[i].x > 150) return true;
  return false;
}
static int want_ride;
static bool w_ride(const jj_run *r) { return r->ride == want_ride && r->t % 7 == 0 && r->ride_speed > 0 && r->next < 200; }
static bool w_token(const jj_run *r) { return r->token[0].on && r->token[0].x < 400; }
static bool w_box(const jj_run *r) { return r->box.on && r->box.x < 420; }

static void shots(const char *dir) {
  memset(&prof, 0, sizeof prof);
  prof.best = 2380, prof.coins = 1264;
  memset(&view, 0, sizeof view);
  view.screen = JJ_SCREEN_TITLE, view.clock = 200;
  picture(dir, "title");
  view.screen = JJ_SCREEN_PLAY;
  struct { const char *name; bool (*w)(const jj_run *); } cases[] = {
      {"coins", w_coins}, {"rotating", w_rotating}, {"warning", w_warn}, {"missile", w_missile},
      {"laser", w_laser}, {"token", w_token}, {"box", w_box}};
  for (unsigned c = 0; c < sizeof cases / sizeof cases[0]; c++) {
    bool ok = false;
    for (uint32_t seed = 1; seed < 400 && !ok; seed++) ok = until_seen(seed * 2654435761u, cases[c].w, 60 * 300);
    CHECK(ok, "no %s seen", cases[c].name);
    picture(dir, cases[c].name);
  }
  static const char *const rides[JJ_RIDES] = {"", "stomper", "hog", "bird", "gravity", "teleporter"};
  for (want_ride = 1; want_ride < JJ_RIDES; want_ride++) {
    bool ok = false;
    for (uint32_t seed = 1; seed < 400 && !ok; seed++) ok = until_seen(seed * 2246822519u, w_ride, 60 * 300);
    CHECK(ok, "no %s seen", rides[want_ride]);
    picture(dir, rides[want_ride]);
  }
  view.screen = JJ_SCREEN_PAUSE, view.sel = 0;
  picture(dir, "pause");
  view.screen = JJ_SCREEN_GUIDE;
  picture(dir, "guide");
  /* a run to its end, then the Final Spin and the results */
  for (uint32_t seed = 3;; seed++) {
    jj_start(&run, seed * 40503u, 0, 0);
    bot_left = 0;
    while (run.state == JJ_ALIVE) jj_step(&run, bot(&run));
    if (run.tokens) break;
  }
  for (int i = 0; i < 5000 && !jj_over(&run); i++) jj_step(&run, false);
  view.screen = JJ_SCREEN_SPIN, view.spinning = 0, view.spin_msg = JJ_PRIZE_REVIVE;
  view.reel[0] = view.reel[1] = view.reel[2] = 48 * JJ_PRIZE_REVIVE;
  picture(dir, "spin");
  view.screen = JJ_SCREEN_RESULTS, view.sel = 0, view.run_m = jj_metres(&run), view.run_coins = run.coins, view.new_best = 1;
  picture(dir, "results");
}

static void film(const char *dir, int seed, int start, int frames) {
  memset(&view, 0, sizeof view);
  view.screen = JJ_SCREEN_PLAY;
  jj_start(&run, (uint32_t)seed * 2654435761u, 0, 0);
  bot_left = 0, greedy = true;
  char path[512];
  for (int f = -start; f < frames; f++) {
    for (int k = 0; k < 3; k++) jj_step(&run, run.state == JJ_ALIVE ? bot(&run) : false);
    if (f < 0) continue;
    snprintf(path, sizeof path, "%s/f_%04d.ppm", dir, f);
    sync_view();
    jj_draw(&run, &view);
    shot(path);
  }
}

int main(int argc, char **argv) {
  gfx_buf = strip;
  if (argc >= 2 && !strcmp(argv[1], "test")) {
    clock_t c0 = clock();
    units();
    reported();
    bots();
    zapper_ways();
    clock_t c1 = clock();
    saves();
    spins();
    fuzz();
    fprintf(stderr, "(%.0f s rules and bots, %.0f s saves and keys)\n", (double)(c1 - c0) / CLOCKS_PER_SEC,
            (double)(clock() - c1) / CLOCKS_PER_SEC);
    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails != 0;
  }
  if (argc >= 3 && !strcmp(argv[1], "shots")) {
    shots(argv[2]);
    return fails != 0;
  }
  if (argc >= 6 && !strcmp(argv[1], "film")) {
    film(argv[2], atoi(argv[3]), atoi(argv[4]), atoi(argv[5]));
    return 0;
  }
  fprintf(stderr, "usage: play test | shots DIR | film DIR SEED START FRAMES\n");
  return 2;
}
