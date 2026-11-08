/* Doodle Jump on a computer, for tests and pictures (make host, make test).
 *
 *   play test           the rules against the spec (jumps, springs, trampolines, flights, steering, the platforms,
 *                       items and monsters by height, bombs, breaking, monsters, black holes, UFOs, bullets); the page
 *                       alone carried past 100000 points 200 times, always with a way up that needs no bomb; a bot
 *                       climbs 300 times and must get past 12000 points in 100 of them; random keys for 300 climbs
 *                       and 40 sessions of the whole app break nothing; the title scores nothing; saves come back
 *                       (from the file, its copy in doodlejump_saves.py, and version 1's from NumPlay's copy)
 *   play shots DIR      the screens, as pictures (DIR/NAME.ppm)
 *   play film DIR N     N frames of the bot's climb in the app, a picture each (DIR/f_NNNN.ppm) */
#define main dj_main
#include "../src/main.c"
#undef main
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- the platform */
static uint16_t fb[SCREEN_H][SCREEN_W];
static uint32_t clock_ms, seed = 7, pushes;
static uint32_t (*keyfn)(uint32_t t);
typedef struct {
  char name[32];
  uint8_t data[4096];
  uint32_t len;
} rec_t;
static rec_t disk[8];

uint32_t plat_keys(void) { return keyfn ? keyfn(clock_ms) : 0; }
uint32_t plat_millis(void) { return clock_ms; }
void plat_sleep(uint32_t ms) { clock_ms += ms; }
void plat_vblank(void) {}
void plat_push(int x, int y, int w, int h, const uint16_t *px) {
  if (x < 0 || y < 0 || x + w > SCREEN_W || y + h > SCREEN_H) {
    printf("push out of the screen: %d %d %d %d\n", x, y, w, h);
    exit(1);
  }
  for (int j = 0; j < h; j++)
    for (int i = 0; i < w; i++) fb[y + j][x + i] = px[j * w + i];
  pushes++;
}
static rec_t *find(const char *name) {
  for (int i = 0; i < 8; i++)
    if (disk[i].len && !strcmp(disk[i].name, name)) return &disk[i];
  return NULL;
}
bool plat_save(const char *name, const void *data, uint32_t len) {
  rec_t *r = find(name);
  for (int i = 0; !r && i < 8; i++)
    if (!disk[i].len) r = &disk[i];
  if (!r || len > sizeof r->data || !len) return false;
  strcpy(r->name, name), memcpy(r->data, data, len), r->len = len;
  return true;
}
const uint8_t *plat_load(const char *name, uint32_t *len) {
  rec_t *r = find(name);
  if (!r) return NULL;
  *len = r->len;
  return r->data;
}
static void forget(const char *name) {
  rec_t *r = find(name);
  if (r) r->len = 0;
}
uint32_t plat_random(void) { return seed = seed * 1664525u + 1013904223u; }
void plat_begin(void) {}
int plat_end(void) { return 0; }

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

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

/* ---------------------------------------------------------------- the bot */
static float wrap_dx(float from, float to) {
  float d = to - from;
  if (d > PAGE_W / 2) d -= PAGE_W;
  if (d < -PAGE_W / 2) d += PAGE_W;
  return d;
}
static float fabs2(float v) { return v < 0 ? -v : v; }
/* steers for the highest platform it can still reach (not a brown one), away from monsters, and shoots what is
   above it */
static int bot(bool *fire) {
  float reach = W.y + (W.vy > 0 ? W.vy * W.vy / (2 * GRAV) : 0) - 4;
  plat_t *best = 0;
  float aim = 0;
  for (int i = 0; i < NP; i++) {
    plat_t *p = &W.plat[i];
    if (p->state == 1 || p->type == P_BROWN || p->y > reach || p->y < W.y - 260) continue;
    if (p->type == P_BOMB && p->t > 120) continue;
    float t = 0, vy = W.vy, y = W.y; /* frames until he comes down to its height */
    while (t < 200 && !(vy < 0 && y <= p->y)) vy -= GRAV, y += vy, t++;
    float px = p->x;
    if (p->type == P_BLUE) { /* where it will be by then, bouncing off the sides */
      float sp = blue_speed(), span = PAGE_W - 2 * PW, q = p->x - PW + p->dir * sp * t;
      q = q - 2 * span * (float)(int)(q / (2 * span));
      if (q < 0) q += 2 * span;
      px = PW + (q > span ? 2 * span - q : q);
    }
    if (fabs2(wrap_dx(W.x, px)) > t * VMAX * 0.8f + PW * 0.6f) continue;
    if (!best || p->y > best->y) best = p, aim = px;
  }
  *fire = false;
  for (int i = 0; i < NM; i++) {
    mon_t *m = &W.mon[i];
    if (m->kind == M_NONE || m->state || m->kind == M_HOLE) continue;
    float hw, hh;
    mon_box(m->kind, &hw, &hh);
    if (m->y > W.y && m->y < W.y + 400 && fabs2(wrap_dx(W.x, mon_x(m))) < hw) *fire = (W.frame % 6) == 0;
  }
  if (!best) return 0;
  float d = wrap_dx(W.x, aim);
  for (int i = 0; i < NM; i++) { /* a black hole, a UFO or a monster in the way: around it */
    mon_t *m = &W.mon[i];
    if (m->kind == M_NONE || m->state) continue;
    float hw, hh, md = wrap_dx(W.x, mon_x(m));
    mon_box(m->kind, &hw, &hh);
    if (m->y > W.y - 60 && m->y < W.y + 200 && fabs2(md) < hw + 30 &&
        (m->kind == M_HOLE || m->kind == M_UFO || W.vy > 0))
      return md > 0 ? -1 : 1;
  }
  return d > 3 ? 1 : d < -3 ? -1 : 0;
}

/* the platforms that stay (not brown, not a bomb) never more than a jump apart, and no bomb lit above the page */
static void check_page(void) {
  if (W.dead) return; /* (falling past the page, it goes down) */
  float ys[NP];
  int n = 0;
  for (int i = 0; i < NP; i++) {
    const plat_t *p = &W.plat[i];
    if (p->type != P_BROWN && p->type != P_BOMB && p->y > W.cam - 40) ys[n++] = p->y;
    CHECK(p->type != P_BOMB || p->y < W.cam + VIEW || (p->t == 0 && p->state == 0), "a bomb lit above the page at %.0f", p->y);
  }
  for (int i = 1; i < n; i++)
    for (int j = i; j > 0 && ys[j] < ys[j - 1]; j--) {
      float t = ys[j];
      ys[j] = ys[j - 1], ys[j - 1] = t;
    }
  for (int i = 1; i < n; i++) CHECK(ys[i] - ys[i - 1] <= 160.01f, "platforms %.0f apart at %.0f", ys[i] - ys[i - 1], ys[i]);
  CHECK(W.x >= 0 && W.x < PAGE_W && W.y == W.y, "doodler off the page: %f", W.x);
}

static int climb(bool smart, int frames) {
  seed = seed * 7 + 11;
  game_new(seed);
  uint32_t last = 0;
  int f = 0;
  for (; f < frames && !game_over(); f++) {
    bool fire = false;
    int steer = smart ? bot(&fire) : (int)(plat_random() % 3) - 1;
    if (!smart) fire = plat_random() % 20 == 0;
    if (fire) game_shoot();
    game_step(steer);
    CHECK(W.score >= last, "the score went down");
    last = W.score;
    if (f % 30 == 0) check_page();
  }
  return f;
}

/* ---------------------------------------------------------------- the rules against the spec */
static void clear_page(void) {
  for (int i = 0; i < NP; i++) W.plat[i] = (plat_t){0, -10000, 0, 1, P_GREEN, I_NONE, 1};
  for (int i = 0; i < NM; i++) W.mon[i].kind = M_NONE;
}
/* how high he goes from y 1000 at speed v, nothing in the way */
static float apex(float v, int fly, int item) {
  game_new(1);
  clear_page();
  W.y = W.cam = W.top = 1000, W.vy = v, W.fly = fly, W.item = (uint8_t)item;
  float top = W.y;
  for (int f = 0; f < 2000 && !W.dead; f++) {
    W.plat[0] = (plat_t){0, W.cam + 5000, 0, 1, P_GREEN, I_NONE, 1}; /* (no new platforms: they stay far away) */
    for (int i = 0; i < NP; i++) W.plat[i].y = W.cam + 5000, W.plat[i].state = 1;
    W.top = W.cam + 5000;
    game_step(0);
    if (W.y > top) top = W.y;
  }
  return top - 1000;
}
/* drops him onto a platform of this type with this item, from 30 points above, his feet at x */
static void land_on(int type, int item, float x) {
  game_new(3);
  clear_page();
  W.plat[0] = (plat_t){160, 200, 0, 1, (uint8_t)type, (uint8_t)item, 0};
  W.x = x, W.y = 230, W.vy = -6, W.cam = 0;
  for (int f = 0; f < 10 && W.vy < 0; f++) game_step(0);
}

static void rules(void) {
  float a = apex(JUMP, 0, 0), s = apex(SPRING, 0, 0), t = apex(TRAMP, 0, 0);
  CHECK(a > 165 && a < 175, "a jump goes %.1f points up, not 173", a);
  CHECK(s > 560 && s < 575, "a spring %.1f, not 571", s);
  CHECK(t > 950 && t < 970, "a trampoline %.1f, not 966", t);
  float p = apex(0, FLY, I_PROP), j = apex(0, FLY, I_JET);
  CHECK(p > 1200 && p < 1300, "the propeller hat %.1f, not 1200 and a coast", p);
  CHECK(j > 2400 && j < 2800, "the jetpack %.1f, not 2400 and a coast", j);
  /* steering: 0.6 a frame up to 7, and slowing to a stop without a key */
  game_new(1);
  for (int f = 0; f < 11; f++) game_step(1);
  CHECK(W.vx > 6.5f && W.vx < 7.0f, "11 frames of steering: %.2f", W.vx);
  for (int f = 0; f < 30; f++) game_step(1);
  CHECK(W.vx == VMAX, "top speed %.2f", W.vx);
  for (int f = 0; f < 60; f++) game_step(0);
  CHECK(W.vx < 0.01f, "still moving without a key: %.2f", W.vx);
  /* landing */
  land_on(P_GREEN, I_NONE, 160);
  CHECK(W.vy == JUMP && W.y == 200 && W.jumps == 1, "no jump off a green platform");
  land_on(P_GREEN, I_NONE, 160 + PW + FEET + 1);
  CHECK(W.vy < 0, "a jump with his feet off the platform");
  land_on(P_GREEN, I_SPRING, 168);
  CHECK(W.vy == SPRING && W.plat[0].state == 2, "no spring");
  land_on(P_GREEN, I_SPRING, 140);
  CHECK(W.vy == JUMP, "a spring off its side of the platform");
  land_on(P_GREEN, I_TRAMP, 160);
  CHECK(W.vy == TRAMP && W.flip == 72, "no trampoline, or no turn on it");
  land_on(P_BROWN, I_NONE, 160);
  CHECK(W.vy < 0 && W.plat[0].state == 1, "a brown platform held him");
  land_on(P_WHITE, I_NONE, 160);
  CHECK(W.vy == JUMP && W.plat[0].state == 1, "a white platform stayed after a jump");
  land_on(P_BLUE, I_NONE, 160);
  CHECK(W.vy == JUMP, "no jump off a blue platform");
  /* a bomb: lit once on the page, gone 180 frames later */
  game_new(5);
  clear_page();
  W.plat[1] = (plat_t){100, 600, 0, 1, P_BOMB, I_NONE, 0};
  W.plat[2] = (plat_t){100, 300, 0, 1, P_BOMB, I_NONE, 0};
  W.x = 10, W.y = 100, W.cam = 0, W.fly = 10000, W.item = I_PROP; /* (out of the way) */
  for (int f = 0; f < 181; f++) {
    W.cam = 0, W.y = 100;
    game_step(0);
  }
  CHECK(W.plat[1].t == 0 && W.plat[1].state == 0, "a bomb lit above the page");
  CHECK(W.plat[2].state == 1, "a bomb on the page not gone after 180 frames");
  /* monsters */
  game_new(9);
  clear_page();
  W.mon[0] = (mon_t){160, 300, 0, M_BLOB, 0, {0}};
  W.x = 160, W.y = 330, W.vy = -5;
  for (int f = 0; f < 20 && !W.mon[0].state; f++) game_step(0);
  CHECK(W.mon[0].state == 1 && !W.dead && W.mon_stomped == 1, "landing on a monster didn't take it out");
  game_new(9);
  clear_page();
  W.mon[0] = (mon_t){160, 300, 0, M_BLOB, 0, {0}};
  W.x = 160, W.y = 240, W.vy = 6;
  for (int f = 0; f < 20 && !W.dead; f++) game_step(0);
  CHECK(W.dead == D_MONSTER, "hitting a monster from below didn't end the climb");
  game_new(9);
  clear_page();
  W.mon[0] = (mon_t){160, 300, 0, M_HOLE, 0, {0}};
  W.x = 160, W.y = 330, W.vy = -5;
  for (int f = 0; f < 20 && !W.dead; f++) game_step(0);
  CHECK(W.dead == D_HOLE, "a black hole didn't take him");
  for (int f = 0; f < 50; f++) game_step(0);
  CHECK(game_over(), "no end after a black hole");
  game_new(9);
  clear_page();
  W.mon[0] = (mon_t){160, 300, 0, M_UFO, 0, {0}};
  W.x = 160, W.y = 240, W.vy = 6;
  for (int f = 0; f < 20 && !W.dead; f++) game_step(0);
  CHECK(W.dead == D_UFO, "a UFO didn't take him");
  /* bullets: monsters and UFOs, not black holes */
  int kinds[] = {M_FLY, M_BAT, M_EYE, M_BLOB, M_CROC, M_HAIRY, M_BUG, M_RED, M_PURPLE, M_UFO, M_HOLE};
  for (int k = 0; k < 11; k++) {
    game_new(9);
    clear_page();
    W.mon[0] = (mon_t){160, 400, 0, (uint8_t)kinds[k], 0, {0}};
    W.x = 160, W.y = 100, W.vy = 0, W.cam = 0;
    W.plat[0] = (plat_t){160, 100, 0, 1, P_GREEN, I_NONE, 0};
    CHECK(game_shoot(), "can't shoot");
    for (int f = 0; f < 40; f++) W.vy = 0, W.y = 100, game_step(0);
    CHECK(W.mon[0].state == (kinds[k] != M_HOLE), "a bullet and monster %d", kinds[k]);
  }
  /* where things come from, carried up the page by a hat */
  float low[16];
  for (int i = 0; i < 16; i++) low[i] = 1e9f;
  for (int g = 0; g < 40; g++) {
    game_new((uint32_t)g * 77 + 1);
    for (int f = 0; f < 60 * 60; f++) {
      W.fly = 2, W.item = I_PROP;
      game_step(0);
      for (int i = 0; i < NP; i++) {
        plat_t *q = &W.plat[i];
        if (q->y > 40 && q->y < low[q->type]) low[q->type] = q->y;
        if (q->item && q->y < low[8 + q->item]) low[8 + q->item] = q->y;
      }
      for (int i = 0; i < NM; i++)
        if (W.mon[i].kind) {
          int c = W.mon[i].kind == M_HOLE || W.mon[i].kind == M_UFO ? 14 : 13;
          if (W.mon[i].y < low[c]) low[c] = W.mon[i].y;
        }
    }
  }
  CHECK(low[P_BROWN] > 300, "brown at %.0f", low[P_BROWN]);
  CHECK(low[P_BLUE] > 1000, "blue at %.0f", low[P_BLUE]);
  CHECK(low[P_WHITE] > 3000, "white at %.0f", low[P_WHITE]);
  CHECK(low[P_BOMB] > 8000, "a bomb at %.0f", low[P_BOMB]);
  CHECK(low[8 + I_TRAMP] > 2000 && low[8 + I_PROP] > 1000 && low[8 + I_JET] > 3000, "items too low");
  CHECK(low[13] > 2500 && low[14] > 8000, "monsters too low: %.0f, holes %.0f", low[13], low[14]);
  CHECK(low[P_BLUE] < 2000 && low[P_WHITE] < 4000 && low[P_BOMB] < 10000 && low[14] < 12000, "never seen");
}

/* ---------------------------------------------------------------- the app */
static uint32_t fuzz_held, fuzz_until;
static uint32_t fuzz_keys(uint32_t t) {
  uint32_t held = fuzz_held, until = fuzz_until;
  if (t >= until) {
    static const uint32_t pick[] = {K_LEFT, K_RIGHT, K_UP, K_DOWN, K_OK, K_BACK, 0, K_LEFT, K_RIGHT, K_OK};
    fuzz_held = held = pick[plat_random() % 10], fuzz_until = until = t + 30 + plat_random() % 400;
  }
  return t > 60000 ? K_HOME : held;
}
static const uint32_t (*script)[3]; /* from, to, keys */
static uint32_t script_keys(uint32_t t) {
  uint32_t k = 0;
  for (int i = 0; script[i][1]; i++)
    if (t >= script[i][0] && t < script[i][1]) k |= script[i][2];
  return k;
}
static uint32_t bot_keys(uint32_t t) {
  if (screen != SC_PLAY) return t / 300 % 2 ? K_OK : 0; /* past the guide and the title */
  bool fire;
  int s = bot(&fire);
  return (s > 0 ? K_RIGHT : s < 0 ? K_LEFT : 0) | (fire && t / 20 % 2 ? K_OK : 0) | (t > 400000 ? K_HOME : 0);
}
static void run(uint32_t (*fn)(uint32_t)) {
  keyfn = fn, clock_ms = 1000, fuzz_until = 0;
  for (uint32_t i = 0; i < sizeof sv; i++) ((uint8_t *)&sv)[i] = 0;
  screen = sel = tab = 0;
  dj_main();
}

static uint8_t old_save[OLD_SIZE];
static void make_old(uint32_t best) { /* version 1: 'D', 1, flags, face, sum, best, ... its sum over the words */
  memset(old_save, 0, sizeof old_save);
  old_save[0] = 'D', old_save[1] = 1, old_save[2] = 3;
  memcpy(old_save + 8, &best, 4);
  uint32_t h = 0x811C9DC5u, w;
  for (int i = 8; i < OLD_SIZE; i += 4) memcpy(&w, old_save + i, 4), h = (h ^ w) * 0x01000193u;
  memcpy(old_save + 4, &h, 4);
}
static void b64_line(char *o, const char *tag, const uint8_t *d, uint32_t n) {
  o += snprintf(o, 64, "%s", tag);
  for (uint32_t i = 0; i < n; i += 3, o += 4) {
    uint32_t left = n - i, v = (uint32_t)d[i] << 16 | (left > 1 ? d[i + 1] << 8 : 0) | (left > 2 ? d[i + 2] : 0);
    o[0] = b64[v >> 18 & 63], o[1] = b64[v >> 12 & 63], o[2] = left > 1 ? b64[v >> 6 & 63] : '=', o[3] = left > 2 ? b64[v & 63] : '=';
  }
  strcpy(o, "\n");
}

static void saves(void) {
  memset(disk, 0, sizeof disk);
  /* the guide, the title's play, a climb, Home: kept */
  static const uint32_t s1[][3] = {{1500, 1600, K_OK}, {2200, 2300, K_OK}, {6000, 6100, K_HOME}, {0, 0, 0}};
  script = s1;
  run(script_keys);
  uint32_t n;
  const uint8_t *d = plat_load(SAVE_NAME, &n);
  CHECK(d && n == sizeof(save_t) && d[0] == 'D' && d[1] == 2 && (d[2] & 3) == 3, "the climb not kept");
  world_t kept = W;
  CHECK(kept.score > 40, "no height in the kept climb: %u", kept.score);
  const uint8_t *c = plat_load(COPY_NAME, &n);
  CHECK(c && c[0] == 0 && c[n - 1] == 0 && n > sizeof(save_t), "no copy, or it doesn't end with a zero");
  /* back paused on the same climb */
  static const uint32_t s2[][3] = {{1500, 1600, K_HOME}, {0, 0, 0}};
  script = s2;
  run(script_keys);
  CHECK(!memcmp(&W, &kept, sizeof W) && screen == SC_PAUSE, "the climb didn't come back as it was");
  /* only the copy left (installed again): the same */
  forget(SAVE_NAME);
  run(script_keys);
  CHECK(!memcmp(&W, &kept, sizeof W), "the copy didn't bring the climb back");
  /* damaged: the copy */
  rec_t *r = find(SAVE_NAME);
  r->data[100] ^= 4;
  load();
  CHECK(!memcmp(&sv.w, &kept, sizeof W), "a damaged save, and the copy not read");
  /* both damaged: afresh */
  r->data[100] ^= 4, r->data[120] ^= 1;
  find(COPY_NAME)->data[60] ^= 1;
  load();
  CHECK(sv.best[0] == 0 && !(sv.flags & 1), "a damaged file should start afresh");
  /* version 1, inside NumPlay: its best and its guide */
  memset(disk, 0, sizeof disk);
  make_old(12345);
  plat_save(SAVE_NAME, old_save, OLD_SIZE);
  load();
  CHECK(sv.best[0] == 12345 && (sv.flags & 2) && !(sv.flags & 1), "version 1's best not read: %u", sv.best[0]);
  /* ...and from NumPlay's copy, as it is ("#>") or packed ("#="), after the calculator was emptied */
  static char text[4096];
  memset(disk, 0, sizeof disk);
  text[0] = 1;
  strcpy(text + 1, "# NumPlay's saves: keep this file\n");
  b64_line(text + strlen(text + 1) + 1, "#>numplay.cfg:", (const uint8_t *)"abc", 3);
  b64_line(text + strlen(text + 1) + 1, "#>doodlejump.sav:", old_save, OLD_SIZE);
  plat_save(NUMPLAY_COPY, text, (uint32_t)strlen(text + 1) + 2);
  load();
  CHECK(sv.best[0] == 12345, "version 1 not read from NumPlay's copy");
  static uint16_t table[NP_LZ_HASH];
  static uint8_t packed[NP_LZ_BOUND(OLD_SIZE)];
  uint32_t pn = np_lz_pack(old_save, OLD_SIZE, packed, table);
  memset(disk, 0, sizeof disk);
  text[0] = 1;
  strcpy(text + 1, "# NumPlay's saves: keep this file\n");
  b64_line(text + strlen(text + 1) + 1, "#=doodlejump.sav:488:", packed, pn);
  plat_save(NUMPLAY_COPY, text, (uint32_t)strlen(text + 1) + 2);
  load();
  CHECK(sv.best[0] == 12345, "version 1 not read from NumPlay's packed copy");
  /* a game over: the score among the best, the stats */
  memset(disk, 0, sizeof disk);
  make_old(500);
  plat_save(SAVE_NAME, old_save, OLD_SIZE);
  script = s1;
  static const uint32_t s3[][3] = {{1500, 1600, K_OK}, {3000, 9000, K_RIGHT}, {9000, 30000, K_LEFT}, {30000, 30100, K_HOME}, {0, 0, 0}};
  script = s3;
  run(script_keys);
  load();
  CHECK(sv.games >= 1 && sv.best[0] >= 500 && sv.best[1] + sv.best[0] > 500, "a game over not counted: %u games, best %u %u",
        sv.games, sv.best[0], sv.best[1]);
}

static void title_scores_nothing(void) {
  memset(disk, 0, sizeof disk);
  make_old(777);
  plat_save(SAVE_NAME, old_save, OLD_SIZE);
  static const uint32_t s[][3] = {{20000, 20100, K_HOME}, {0, 0, 0}};
  script = s;
  run(script_keys);
  load();
  CHECK(screen == SC_TITLE && sv.best[0] == 777 && sv.best[1] == 0 && sv.games == 0 && !(sv.flags & 1),
        "points or a climb on the title");
}

static void shots(const char *dir) {
  char path[512];
  memset(disk, 0, sizeof disk);
#define SHOT(name) snprintf(path, sizeof path, "%s/%s.ppm", dir, name), picture(path)
  /* each screen, drawn as the app draws it */
  clock_ms = 1000;
  screen = SC_HELP, gfx_screen(draw_help), SHOT("help");
  for (int i = 0; i < NBEST; i++) sv.best[i] = 40000u / (uint32_t)(i + 1) + 123u * (uint32_t)i;
  sv.games = 57, sv.total = 57 * 6400, sv.last = 8812, sv.jumps = 15301, sv.mon_shot = 64, sv.mon_stomped = 31;
  sv.ufo_shot = 3, sv.ufo_stomped = 2, sv.jets = 12, sv.hats = 40, sv.ends[0] = 31, sv.ends[1] = 18, sv.ends[2] = 3;
  sv.ends[3] = 5;
  screen = SC_TITLE, sel = 0, anim = 30, gfx_screen(draw_title), SHOT("title");
  screen = SC_SCORES, tab = 0, gfx_screen(draw_scores), SHOT("scores");
  tab = 1, gfx_screen(draw_scores), SHOT("stats");
  /* the bot's climb, pictures at heights */
  seed = 11;
  game_new(seed);
  uint32_t marks[] = {0, 3000, 9000, 15000, 30000}, next = 0;
  for (int f = 0; f < 60 * 60 * 20 && next < 5 && !W.dead; f++) {
    bool fire;
    int st = bot(&fire);
    if (fire) game_shoot();
    game_step(st);
    if (W.score >= marks[next] && (next == 0 || W.mon[0].kind || W.mon[1].kind || W.mon[2].kind || W.mon[3].kind)) {
      screen = SC_PLAY, draw_margins(), draw_play();
      snprintf(path, sizeof path, "%s/play%u.ppm", dir, next++), picture(path);
    }
  }
  screen = SC_PAUSE, sel = 0, draw_play(), SHOT("pause");
  screen = SC_ASK, sel = 0, draw_play(), SHOT("ask");
  screen = SC_OVER, sel = 0, gfx_screen(draw_over), SHOT("over");
}

static void film(const char *dir, int frames) {
  memset(disk, 0, sizeof disk);
  keyfn = bot_keys, clock_ms = 1000, seed = 21;
  for (uint32_t i = 0; i < sizeof sv; i++) ((uint8_t *)&sv)[i] = 0;
  sv.flags = 2;
  plat_save(SAVE_NAME, &sv, 1); /* (a broken file: the guide is still shown) */
  start_climb();
  char path[512];
  for (int f = 0; f < frames; f++) {
    now = clock_ms;
    bool fire;
    int st = bot(&fire);
    if (fire && f % 3 == 0) game_shoot();
    for (int k = 0; k < 2; k++) game_step(st);
    if (game_over()) break;
    if (f == 0) draw_margins();
    draw_play();
    snprintf(path, sizeof path, "%s/f_%04d.ppm", dir, f), picture(path);
    clock_ms += 33;
  }
  printf("score %u\n", W.score);
}

int main(int argc, char **argv) {
  if (argc >= 3 && !strcmp(argv[1], "shots")) {
    shots(argv[2]);
    return 0;
  }
  if (argc >= 4 && !strcmp(argv[1], "film")) {
    film(argv[2], atoi(argv[3]));
    return 0;
  }
  CHECK(offsetof(save_t, w.score) == 108 && sizeof(save_t) < 2048, "the save's layout moved (tests/test_games.py reads it)");
  rules();
  /* the page alone, the doodler carried up by a hat past 100000, 200 times: always a way up */
  for (int g = 0; g < 200; g++) {
    seed = seed * 7 + 11;
    game_new(seed);
    for (int f = 0; f < 60 * 250; f++) {
      W.fly = 2, W.item = I_PROP;
      game_step(0);
      if (f % 10 == 0) check_page();
    }
    CHECK(W.score > 100000, "the page only got to %u", W.score);
  }
  long total = 0;
  uint32_t best = 0, far = 0;
  int ends[5] = {0};
  for (int g = 0; g < 300; g++) {
    climb(true, 60 * 60 * 5);
    total += W.score, best = W.score > best ? W.score : best;
    if (W.score > 12000) far++;
    ends[W.dead]++;
  }
  printf("bot: 300 climbs, %ld on average, best %u, %u past 12000 (fell %d, monsters %d, black holes %d, UFOs %d)\n",
         total / 300, best, far, ends[D_FELL], ends[D_MONSTER], ends[D_HOLE], ends[D_UFO]);
  CHECK(far >= 100, "the bot should get past 12000 (bombs, black holes, UFOs) in 100 climbs of 300");
  for (int g = 0; g < 300; g++) climb(false, 60 * 60);
  /* the whole app, random keys */
  for (int g = 0; g < 40; g++) {
    memset(disk, 0, sizeof disk);
    run(fuzz_keys);
    uint32_t n;
    const uint8_t *d = plat_load(SAVE_NAME, &n);
    CHECK(d && n == sizeof(save_t), "the app left no save");
  }
  title_scores_nothing();
  saves();
  printf(fails ? "FAILED (%d)\n" : "ok\n", fails);
  return fails != 0;
}
