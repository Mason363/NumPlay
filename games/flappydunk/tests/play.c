/* Flappy Dunk on a computer, for tests and pictures (make host, make test).
 *   play test            the rules against the original's numbers (spec/), a bot through endless runs and every
 *                        challenge, random keys through every screen, saves (kept, damaged, lost, from the copy,
 *                        from the first version's)
 *   play shot DIR        pictures of the screens (DIR/<name>.ppm)
 *   play film DIR N      a bot's endless run, every 2nd frame of N (DIR/f_NNNN.ppm), for the GIF */
#define main fd_app_main
#include "../src/main.c"
#undef main
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

/* ---------------------------------------------------------------- the calculator, on a computer */
static uint16_t screen_px[240][320];
void np_push(int x, int y, int w, int h, const color_t *px) {
  for (int j = 0; j < h; j++)
    for (int i = 0; i < w; i++) screen_px[y + j][x + i] = px[j * w + i];
}
void *np_alloc(uint32_t n) { return calloc(1, n); }
static uint64_t clock_ms;
static uint32_t (*script)(void);
static uint32_t frame_no, quit_at = 0xFFFFFFFFu;
uint32_t plat_keys(void) {
  frame_no++;
  if (frame_no >= quit_at) return PK_HOME;
  return script ? script() : 0;
}
uint32_t plat_millis(void) { return (uint32_t)(clock_ms += 10); }
void plat_sleep(uint32_t ms) { clock_ms += ms; }
static uint32_t lcg_s = 1;
static uint32_t lcg(void) { return lcg_s = lcg_s * 1664525u + 1013904223u; }
uint32_t plat_random(void) { return lcg(); }
void plat_begin(void) {}
int plat_end(void) { return 0; }
static struct {
  char name[32];
  uint8_t data[4096];
  uint32_t len;
  bool on;
} files[4];
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
static void files_clear(void) { memset(files, 0, sizeof files); }
static void file_drop(const char *name) {
  for (int i = 0; i < 4; i++)
    if (files[i].on && !strcmp(files[i].name, name)) files[i].on = false;
}
static uint8_t *file_of(const char *name, uint32_t *len) {
  for (int i = 0; i < 4; i++)
    if (files[i].on && !strcmp(files[i].name, name)) return *len = files[i].len, files[i].data;
  return 0;
}

static void ppm(const char *path) {
  FILE *f = fopen(path, "wb");
  if (!f) exit(2);
  fprintf(f, "P6 320 240 255\n");
  for (int y = 0; y < 240; y++)
    for (int x = 0; x < 320; x++) {
      uint16_t c = screen_px[y][x];
      fputc((c >> 11) * 255 / 31, f), fputc((c >> 5 & 63) * 255 / 63, f), fputc((c & 31) * 255 / 31, f);
    }
  fclose(f);
}

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)
static bool near(float a, float b, float e) { return fabsf(a - b) <= e; }

/* ---------------------------------------------------------------- the bot (src/bot.c) */
bool bot_wants_flap(int effort); /* (src/bot.c) */
static bool bot(void) { return bot_wants_flap(8); }
/* plays the run in G to its end (or max steps): flaps chosen every 4 steps */
static int bot_play(int max_steps) {
  int steps = 0;
  if (G.state == ST_READY) game_flap();
  for (; steps < max_steps && G.state <= ST_PLAY; steps++) {
    if (steps % 4 == 0 && bot()) game_flap();
    game_step();
  }
  return steps;
}

/* ---------------------------------------------------------------- the tests */
static void test_numbers(void) {
  /* Parameters (Tuning.GameDifficulty and the scene's curves) */
  CHECK(near(param_sample(P_SCROLL, 0), 4, 1e-4f) && near(param_sample(P_SCROLL, 200), 5, 1e-4f) &&
            near(param_sample(P_SCROLL, 100), 4.5f, 1e-3f), "scroll speed %f %f", param_sample(P_SCROLL, 0), param_sample(P_SCROLL, 200));
  CHECK(near(param_sample(P_WIDTH, 0), 0.8f, 1e-4f) && near(param_sample(P_WIDTH, 40), 0.6f, 1e-4f) &&
            near(param_sample(P_WIDTH, 999), 0.6f, 1e-4f), "hoop width");
  CHECK(near(param_sample(P_HEIGHT, 0), 0, 1e-4f) && near(param_sample(P_HEIGHT, 40), 1, 1e-3f) &&
            near(param_sample(P_HEIGHT, 10), 0.58f, 0.02f), "hoop height %f", param_sample(P_HEIGHT, 10));
  CHECK(near(param_sample(P_MOVING, 15), 0, 1e-4f) && near(param_sample(P_MOVING, 75), 0.8f, 1e-3f), "moving hoops");
  CHECK(near(param_sample(P_LATERAL, 70), 0, 1e-4f) && near(param_sample(P_LATERAL, 150), 0.5f, 1e-3f), "lateral hoops");
  CHECK(near(param_sample(P_ANGLE, 8), 0, 0.01f) && near(param_sample(P_ANGLE, 50), 1, 0.01f), "angles");
  CHECK(param_sample(P_GRAVITY, 7) == -40 && param_sample(P_FLAP, 99) == 12, "gravity and flap");
  /* the flap: the ball rises 12^2 / 80 = 1.8 and moves on at 4 */
  game_new(-1, false, 5);
  game_flap();
  CHECK(G.vy == 12 && near(G.vx, 4, 0.03f), "flap %f %f", G.vx, G.vy); /* (4 hoops made: 4.02) */
  float top = G.y;
  for (int i = 0; i < 60; i++) game_step(), top = G.y > top ? G.y : top;
  CHECK(near(top, 1.8f, 0.08f), "flap height %f", top);
  extern void game_test_spawn(int n);
  /* the endless hoops: 9 apart from x 3, the first in the middle, tilts and widths as the hoops go by */
  game_new(-1, false, 7);
  CHECK(near(G.h[0].x, 3, 1e-4f) && near(G.h[0].y, 0, 1e-4f) && near(G.h[1].x, 12, 1e-4f), "first hoops");
  CHECK(G.h[0].angle == 0 && near(G.h[0].w, 0.8f, 1e-4f) && G.h[0].type == H_STATIC, "first hoop");
  int tilted = 0, moving = 0, lateral = 0;
  for (int n = 0; n < 4000; n++) {
    G.spawned = n % 160;
    float a = (float)0, w;
    (void)w;
    extern float game_test_angle(int n);
    a = game_test_angle(n % 160);
    if (a != 0) tilted++;
    CHECK(fmodf(fabsf(a), 5) == 0 && a <= 60 && a >= -20, "angle %f", a);
    if (n % 160 < 8) CHECK(a == 0, "a tilt before 8 hoops");
  }
  CHECK(tilted > 1000, "tilts %d", tilted);
  /* moving and lateral hoops come after 15 and 70; heights within the hoop types' reach */
  for (int r = 0; r < 30; r++)
    for (int n = 0; n < 160; n++) {
      game_test_spawn(n);
      const hoop_t *h = &G.h[0];
      if (h->type != H_STATIC) {
        moving++;
        CHECK(n > 15, "a moving hoop at %d", n);
      }
      if (h->type == H_LATERAL) {
        lateral++;
        CHECK(n > 70, "a lateral hoop at %d", n);
      }
      CHECK(fabsf(h->y) <= (h->type == H_VERTICAL ? 4.5f : 6.5f) * param_sample(P_HEIGHT, n) + 1e-4f, "height %f", h->y);
      CHECK(near(h->w, param_sample(P_WIDTH, n), 1e-5f), "width");
    }
  CHECK(moving > 100 && lateral > 20, "moving %d lateral %d", moving, lateral);
}

/* swishes and baskets score as Stats.OnPassHoop; the floor ends the run */
static int passes[64], npasses;
static void test_rules(void) {
  /* drop the ball straight through the first hoop: a swish, 2 points */
  game_new(-1, false, 3);
  fd_quiet = true;
  G.state = ST_PLAY;
  G.x = 3, G.y = 2, G.vx = 0, G.vy = 0;
  for (int i = 0; i < 100 && G.hoops == 0; i++) game_step();
  CHECK(G.hoops == 1 && G.score == 2 && G.swishes == 1 && G.mult == 2, "swish: hoops %u score %u", G.hoops, G.score);
  /* the next one through its rim: a basket, 1 point, the streak broken */
  hoop_t *h = &G.h[1];
  G.x = h->hx + 1.6f * h->w - 0.3f, G.y = h->hy + 1.2f, G.vx = 0, G.vy = 0, G.cam = G.x;
  for (int i = 0; i < 200 && G.hoops == 1 && G.state == ST_PLAY; i++) game_step();
  CHECK(G.state == ST_PLAY && G.hoops == 2 && G.score == 3 && G.streak == 0 && G.mult == 1,
        "basket: state %d hoops %u score %u", G.state, G.hoops, G.score);
  /* up through a hoop: over */
  game_new(-1, false, 3);
  G.state = ST_PLAY;
  G.x = 3, G.y = -1.5f, G.vx = 0, G.vy = 9;
  for (int i = 0; i < 100 && G.state == ST_PLAY; i++) game_step();
  CHECK(G.state == ST_DEAD && G.end == END_BELOW, "from below: %d %d", G.state, G.end);
  /* the floor: over */
  game_new(-1, false, 3);
  G.state = ST_PLAY;
  for (int i = 0; i < 300 && G.state == ST_PLAY; i++) game_step();
  CHECK(G.state == ST_DEAD && G.end == END_GROUND, "floor: %d %d", G.state, G.end);
  /* flying past a hoop: over */
  game_new(-1, false, 3);
  G.state = ST_PLAY;
  G.y = 6, G.vx = 4;
  for (int i = 0; i < 400 && G.state == ST_PLAY; i++) G.vy = 0, game_step();
  CHECK(G.state == ST_DEAD && G.end == END_MISS, "missed: %d %d", G.state, G.end);
  fd_quiet = false;
  (void)passes, (void)npasses;
}

static void test_bot(void) {
  /* endless runs: the bot must get far, every hoop scored by the rules, the ball always in the field */
  int best = 0, total = 0;
  for (int r = 0; r < 6; r++) {
    game_new(-1, false, (uint32_t)r * 977 + 5);
    game_flap();
    uint32_t score = 0, mult = 1;
    for (int s = 0; s < 6000 && G.state == ST_PLAY; s++) {
      if (s % 4 == 0 && bot()) game_flap();
      uint32_t h0 = G.hoops, sw0 = G.swishes;
      game_step();
      if (G.hoops != h0) {
        uint32_t pts = G.swishes != sw0 ? mult + 1 : 1;
        score += pts, mult = pts;
        CHECK(G.score == score && G.mult == mult, "score %u, by the rules %u", G.score, score);
      }
      CHECK(G.y == G.y && G.y > -FD_TOP - 1 && G.y < FD_TOP + 1, "ball off the field %f", G.y);
    }
    total += (int)G.hoops;
    if ((int)G.hoops > best) best = (int)G.hoops;
  }
  printf("endless: best %d hoops, %d in 6 runs\n", best, total);
  CHECK(best >= 25, "the bot passed %d hoops at best", best);
  /* every challenge, normal and mirrored (mirroring changes only the picture) */
  int won = 0;
  for (int c = 0; c < 51; c++) {
    bool ok = false;
    for (int tries = 0; tries < 4 && !ok; tries++) {
      game_new(c, false, (uint32_t)(c * 31 + tries * 7 + 1));
      bot_play(9000);
      ok = G.state == ST_WON;
      if (ok) {
        const fd_challenge_t *ch = &fd_challenges[c];
        CHECK(G.hoops == ch->count, "challenge %d won with %u of %u hoops", c + 1, G.hoops, ch->count);
        if (ch->swishes >= 0)
          CHECK(ch->condition ? (int)G.swishes >= ch->swishes : (int)G.swishes == ch->swishes,
                "challenge %d won with %u swishes", c + 1, G.swishes);
      }
    }
    won += ok;
  }
  printf("challenges: the bot completes %d of 51\n", won);
  CHECK(won >= 30, "only %d challenges completed", won);
}

/* ---- the whole app: random keys through every screen, saves */
static uint32_t rnd_keys(void) {
  static const uint32_t k[] = {PK_OK, PK_UP, PK_DOWN, PK_LEFT, PK_RIGHT, PK_BACK, PK_EXE, 0, 0, 0, PK_OK, PK_OK};
  return (lcg() >> 8) % 3 ? 0 : k[(lcg() >> 8) % 12];
}
static void run_app(uint32_t (*s)(void), uint32_t frames) {
  script = s, frame_no = 0, quit_at = frames;
  fd_app_main();
  script = 0;
}
static uint32_t ok_then_bot(void) {
  if (sc == SC_HELP) return frame_no % 2 ? PK_OK : 0;
  if (sc == SC_TITLE) return frame_no % 2 ? PK_OK : 0;
  if (sc == SC_PLAY && G.state <= ST_PLAY) {
    if (G.state == ST_READY) return frame_no % 2 ? PK_OK : 0;
    return bot() ? PK_OK : 0;
  }
  return 0;
}
static void test_app(void) {
  /* first run: How to play, then a bot's endless run, Home: the best and the games saved, the copy too */
  files_clear();
  memset(&sv, 0, sizeof sv);
  run_app(ok_then_bot, 900);
  uint32_t n;
  const uint8_t *d = file_of(SAVE_NAME, &n);
  CHECK(d && n == sizeof(save_t), "no save");
  save_t s0;
  memcpy(&s0, d, sizeof s0);
  CHECK(save_ok(&s0) && s0.games >= 1 && s0.best >= 1 && (s0.flags & 1), "save: games %u best %u", s0.games, s0.best);
  const uint8_t *c = file_of(COPY_NAME, &n);
  CHECK(c && c[0] == 0 && c[n - 1] == 0 && !memchr(c + 1, 0, n - 2), "the copy isn't a script ending with a zero");
  /* the save lost, the copy left: it comes back */
  file_drop(SAVE_NAME);
  memset(&sv, 0, sizeof sv);
  load();
  CHECK(sv.best == s0.best && sv.games == s0.games, "from the copy: best %u", sv.best);
  /* a damaged save, no copy: afresh, no crash */
  d = file_of(SAVE_NAME, &n);
  file_drop(COPY_NAME);
  save();
  uint8_t *dd = file_of(SAVE_NAME, &n);
  dd[20] ^= 0x55;
  file_drop(COPY_NAME);
  load();
  CHECK(sv.best == 0 && sv.games == 0, "a damaged save kept");
  /* the first version's save (200 bytes): best 57, 140 swishes, 33 games, 9 swishes in a row, the Ocean ball */
  files_clear();
  uint32_t w[50] = {0};
  uint8_t *b = (uint8_t *)w;
  b[0] = 'F', b[1] = 1, b[2] = 3, b[3] = 2;
  w[2] = 57, w[3] = 140, w[4] = 33, w[5] = 9;
  uint32_t hh = 0x811C9DC5u;
  for (int i = 2; i < 50; i++) hh = (hh ^ w[i]) * 0x01000193u;
  w[1] = hh;
  plat_save(SAVE_NAME, w, 200);
  run_app(0, 40);
  d = file_of(SAVE_NAME, &n);
  memcpy(&s0, d, sizeof s0);
  CHECK(n == sizeof(save_t) && save_ok(&s0) && s0.best == 57 && s0.swishes == 140 && s0.games == 33,
        "migrated: best %u swishes %u games %u", s0.best, s0.swishes, s0.games);
  CHECK(bit(s0.unlocked, 3) && bit(s0.unlocked, 10) && bit(s0.unlocked, 6) && bit(s0.unlocked, kind_first[K_HOOP] + 5) &&
            !bit(s0.unlocked, 13) && s0.equip[K_BALL] == 10,
        "migrated balls: green %d football %d pink %d ball %d", bit(s0.unlocked, 3), bit(s0.unlocked, 10), bit(s0.unlocked, 6),
        s0.equip[K_BALL]);
  /* random keys through every screen */
  for (int r = 0; r < 300; r++) {
    lcg_s = (uint32_t)r * 7919u + 1;
    if (r % 50 == 0) files_clear();
    run_app(rnd_keys, 300 + (lcg() >> 8) % 900);
    CHECK(sc >= SC_TITLE && sc <= SC_HELP, "screen %d", sc);
  }
}

/* ---------------------------------------------------------------- pictures */
static void shots(const char *dir) {
  gfx_buf = strip;
  char p[512];
  files_clear();
  memset(&sv, 0, sizeof sv);
  sv.flags = 1;
  /* every skin open, a few on */
  for (int k = 0; k < NSKINS; k++) bit_set(sv.unlocked, k), bit_set(sv.seen, k);
  sv.best = 87;
  save();
  /* the title */
  lcg_s = 9;
  quit_at = 0xFFFFFFFF;
  script = 0;
  load(), apply_skins(), dr_init(), start_endless(), sc = SC_TITLE, sel = 0;
  for (int i = 0; i < 60; i++) game_step();
  render();
  snprintf(p, sizeof p, "%s/title.ppm", dir), ppm(p);
  /* an endless run on fire: the bot until a streak */
  for (int tries = 0; tries < 40; tries++) {
    game_new(-1, false, (uint32_t)tries * 13 + 2);
    sc = SC_PLAY;
    game_flap();
    bool shot = false;
    for (int s = 0; s < 8000 && G.state == ST_PLAY; s++) {
      if (s % 4 == 0 && bot()) game_flap();
      game_step();
      if (G.fever == 2 && G.streak >= 3 && G.pop_t > 0.05f && G.score > 20 && G.next < G.nh && G.h[G.next].hx - G.x < 4.6f && G.h[G.next].hx - G.x > 2.0f && G.y > G.h[G.next].hy + 0.5f) {
        render();
        snprintf(p, sizeof p, "%s/shot.ppm", dir), ppm(p);
        shot = true;
        break;
      }
    }
    if (shot) break;
  }
  /* tilted and moving hoops further on */
  game_new(-1, false, 44);
  sc = SC_PLAY;
  game_flap();
  for (int s = 0; s < 30000 && G.state == ST_PLAY && G.hoops < 60; s++) {
    if (s % 4 == 0 && bot()) game_flap();
    game_step();
  }
  render();
  snprintf(p, sizeof p, "%s/moving.ppm", dir), ppm(p);
  /* results */
  G.state = ST_DEAD, G.end = END_GROUND, sc = SC_RESULT, sel = 0, best0 = 30;
  render();
  snprintf(p, sizeof p, "%s/results.ppm", dir), ppm(p);
  /* skins */
  sv.equip[K_BALL] = 17, sv.equip[K_WING] = 3, sv.equip[K_HOOP] = 4, apply_skins();
  bit_set(sv.unlocked, 0);
  memset(sv.unlocked, 0, sizeof sv.unlocked);
  for (int k = 0; k < NSKINS; k++)
    if (k % 3 != 2 || skins[k].rule == R_DEFAULT) bit_set(sv.unlocked, k);
  bit_set(sv.unlocked, kind_first[0] + 17), bit_set(sv.unlocked, kind_first[1] + 3), bit_set(sv.unlocked, kind_first[2] + 4);
  open_skins();
  sel = 5;
  render();
  snprintf(p, sizeof p, "%s/skins.ppm", dir), ppm(p);
  /* challenges */
  for (int i = 0; i < 14; i++) bit_set(sv.ch_done, i);
  open_challenges();
  ch_sel = 14;
  render();
  snprintf(p, sizeof p, "%s/challenges.ppm", dir), ppm(p);
  /* a challenge with a mad hoop, and one mirrored */
  apply_skins();
  game_new(2, false, 5);
  sc = SC_PLAY;
  game_flap();
  for (int s = 0; s < 260; s++) {
    if (s % 4 == 0 && bot()) game_flap();
    game_step();
  }
  render();
  snprintf(p, sizeof p, "%s/challenge.ppm", dir), ppm(p);
  /* pause */
  sc = SC_PAUSE, sel = 0;
  render();
  snprintf(p, sizeof p, "%s/pause.ppm", dir), ppm(p);
  sc = SC_HELP;
  render();
  snprintf(p, sizeof p, "%s/help.ppm", dir), ppm(p);
  /* challenges that look different: mirrored, upside down, pixelated, narrow, the mad hoop up close */
  static const struct { int c; bool m; int steps; const char *name; } looks[] = {
    {1, true, 400, "mirror"}, {21, false, 300, "upside"}, {8, false, 250, "pixel"}, {13, false, 300, "narrow"},
    {48, false, 260, "wtf"}};
  for (unsigned i = 0; i < sizeof looks / sizeof looks[0]; i++) {
    game_new(looks[i].c, looks[i].m, 3);
    sc = SC_PLAY;
    game_flap();
    for (int s = 0; s < looks[i].steps && G.state == ST_PLAY; s++) {
      if (s % 4 == 0 && bot()) game_flap();
      game_step();
    }
    render();
    snprintf(p, sizeof p, "%s/%s.ppm", dir, looks[i].name), ppm(p);
  }
}

static void film(const char *dir, int frames) {
  gfx_buf = strip;
  char p[512];
  files_clear();
  memset(&sv, 0, sizeof sv);
  sv.flags = 1;
  for (int k = 0; k < NSKINS; k++) bit_set(sv.unlocked, k);
  apply_skins(), dr_init();
  for (int tries = 0; tries < 30; tries++) {
    game_new(-1, false, (uint32_t)tries * 101 + 9);
    sc = SC_PLAY;
    game_flap();
    int s = 0;
    for (; s < frames * 3 && G.state == ST_PLAY; s++) {
      if (s % 4 == 0 && bot()) game_flap();
      game_step();
    }
    if (G.state == ST_PLAY && G.best_streak >= 3) break;
  }
  game_new(-1, false, 0);
  for (int tries = 0; tries < 30; tries++) {
    game_new(-1, false, (uint32_t)tries * 101 + 9);
    sc = SC_PLAY;
    game_flap();
    int s = 0, f = 0;
    for (; f < frames && G.state <= ST_PLAY; s++) {
      if (s % 4 == 0 && G.state == ST_PLAY && bot()) game_flap();
      game_step();
      if (s % 6 == 5) { /* 16.7 frames a second of the game's time, every 2nd kept */
        if (f % 2 == 0) {
          render();
          snprintf(p, sizeof p, "%s/f_%04d.ppm", dir, f / 2), ppm(p);
        }
        f++;
      }
    }
    if (f >= frames) break;
  }
}

float game_test_angle(int n);
int main(int argc, char **argv) {
  if (argc >= 3 && !strcmp(argv[1], "shot")) return shots(argv[2]), 0;
  if (argc >= 4 && !strcmp(argv[1], "film")) return film(argv[2], atoi(argv[3])), 0;
  test_numbers();
  test_rules();
  test_bot();
  test_app();
  printf(fails ? "%d FAILED\n" : "all good\n", fails);
  return fails != 0;
}
