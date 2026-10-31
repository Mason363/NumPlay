/* Mahjong on a computer, for tests and pictures (make host, make test).
 *
 *   play test          the layouts and the rules; deals of every layout won, many seeds; shuffles that can be won;
 *                      undo and picking; saves kept, damaged and read back from the copy; a game won with the keys
 *                      (the arrows reach every tile it takes); random keys
 *   play film DIR      a game played with the keys, the screen saved as it goes (DIR/f_NNNN.ppm), and the
 *                      screens of the docs (DIR/shot_N.ppm)
 */
#define main mahjong_main
#include "../src/main.c"
#undef main
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint16_t fb[SCREEN_H][SCREEN_W];
void eadk_display_push_rect(eadk_rect_t r, const eadk_color_t *p) {
  for (int j = 0; j < r.height; j++)
    for (int i = 0; i < r.width; i++) fb[r.y + j][r.x + i] = p[j * r.width + i];
}
void eadk_display_push_rect_uniform(eadk_rect_t r, eadk_color_t c) {
  for (int j = 0; j < r.height; j++)
    for (int i = 0; i < r.width; i++) fb[r.y + j][r.x + i] = c;
}
bool eadk_display_wait_for_vblank(void) { return true; }
void *np_alloc(uint32_t size) { return malloc(size); }

/* the files */
static struct { char name[32]; uint8_t data[1024]; uint32_t len; } files[4];
static int find(const char *name) {
  for (int i = 0; i < 4; i++)
    if (files[i].len && !strcmp(files[i].name, name)) return i;
  return -1;
}
const uint8_t *host_read(const char *name, uint32_t *len) {
  int i = find(name);
  if (i < 0) return NULL;
  *len = files[i].len;
  return files[i].data;
}
bool host_write(const char *name, const void *data, uint32_t len) {
  int i = find(name);
  for (int k = 0; k < 4 && i < 0; k++)
    if (!files[k].len) i = k;
  if (i < 0 || len > sizeof files[i].data || !len) return false;
  snprintf(files[i].name, sizeof files[i].name, "%s", name);
  memcpy(files[i].data, data, len), files[i].len = len;
  return true;
}

/* the keys: a script of presses, each held 60 ms; the clock moves on as the game sleeps */
static uint64_t clock_ms;
static struct { uint64_t t; int key; } script[8192];
static int nscript, nshots, frame, quiet;
static uint64_t shots[64], ft;
static const char *dir;
static uint32_t random_value = 0x9E3779B9u;
uint64_t eadk_timing_millis(void) { return clock_ms; }
uint32_t eadk_random(void) { return random_value; }
static void ppm(const char *path) {
  FILE *f = fopen(path, "wb");
  if (!f) return;
  fprintf(f, "P6 %d %d 255\n", SCREEN_W, SCREEN_H);
  for (int y = 0; y < SCREEN_H; y++)
    for (int x = 0; x < SCREEN_W; x++) {
      uint16_t c = fb[y][x];
      fputc((c >> 11) * 255 / 31, f), fputc((c >> 5 & 63) * 255 / 63, f), fputc((c & 31) * 255 / 31, f);
    }
  fclose(f);
}
void eadk_timing_msleep(uint32_t ms) {
  for (uint32_t i = 0; i < ms; i++) {
    clock_ms++;
    char path[512];
    for (int s = 0; s < nshots; s++)
      if (shots[s] == clock_ms) snprintf(path, sizeof path, "%s/shot_%d.ppm", dir, s), ppm(path);
    if (dir && clock_ms % 50 == 0 && !quiet) { /* (frames as tools/record.py --frames reads them) */
      snprintf(path, sizeof path, "%s/frame_%06llu.raw", dir, (unsigned long long)clock_ms);
      FILE *f = fopen(path, "wb");
      if (f) fwrite(fb, 2, SCREEN_W * SCREEN_H, f), fclose(f);
      frame++;
    }
  }
}
uint64_t eadk_keyboard_scan(void) {
  uint64_t k = 0;
  for (int i = 0; i < nscript; i++)
    if (clock_ms >= script[i].t && clock_ms < script[i].t + 60) k |= 1ull << script[i].key;
  return k;
}
static void press(int key, int wait) {
  if (nscript < (int)(sizeof script / sizeof script[0])) script[nscript].t = ft, script[nscript++].key = key;
  ft += (uint64_t)wait;
}
static void run(void) { /* the app, with the script from now on */
  for (int i = 0; i < nscript; i++) script[i].t += clock_ms;
  for (int s = 0; s < nshots; s++) shots[s] += clock_ms;
  mahjong_main();
  nscript = nshots = 0, ft = 0;
}

static uint8_t kept_files[sizeof files];
static void keep_files(void) { memcpy(kept_files, files, sizeof files); }
static void back_files(void) { memcpy(files, kept_files, sizeof files); }

static int fails;
#define CHECK(c, ...) do { if (!(c) && fails++ < 40) { printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

/* free, from the tiles' places alone (not the lists the game keeps) */
static bool free_by_hand(int a) {
  bool l = false, r = false;
  for (int j = 0; j < NT; j++) {
    if (j == a || gone(j) || iabs(ty[j] - ty[a]) > 1) continue;
    if (tz[j] > tz[a] && iabs(tx[j] - tx[a]) < 2) return false;
    if (tz[j] == tz[a]) l |= tx[j] == tx[a] - 2, r |= tx[j] == tx[a] + 2;
  }
  return !(l && r);
}

static void check_layouts(void) {
  for (int l = 0; l < LAYOUTS; l++) {
    load_layout(l);
    for (int i = 0; i < NT; i++) {
      CHECK(sx(i) >= 0 && sx(i) + TW + DEP <= SCREEN_W && sy(i) >= 0 && sy(i) + TH + DEP <= BAR,
            "%s: tile %d off the screen (%d, %d)", layout_name[l], i, sx(i), sy(i));
      for (int j = 0; j < i; j++)
        CHECK(tz[i] != tz[j] || iabs(tx[i] - tx[j]) > 1 || iabs(ty[i] - ty[j]) > 1, "%s: tiles %d and %d overlap",
              layout_name[l], i, j);
      if (i) CHECK(tz[i - 1] < tz[i] || (tz[i - 1] == tz[i] && (tx[i - 1] < tx[i] || (tx[i - 1] == tx[i] && ty[i - 1] < ty[i]))),
                   "%s: not in the order they're drawn", layout_name[l]);
    }
  }
  /* the Turtle: 87, 36, 16, 4 and 1 tiles a layer, the top one on the four below it */
  load_layout(0);
  int per[5] = {0}, top = -1;
  for (int i = 0; i < NT; i++) per[tz[i] < 5 ? tz[i] : 0]++, top = tz[i] == 4 ? i : top;
  CHECK(per[0] == 87 && per[1] == 36 && per[2] == 16 && per[3] == 4 && per[4] == 1, "the Turtle's layers");
  CHECK(top >= 0 && tx[top] == 14 && ty[top] == 7 && ndn[top] == 4 + 4 + 4 + 4, "the Turtle's top tile");
}

/* plays the deal back: the pairs in the order they were taken when it was made */
static int solve(int n) {
  for (int k = 0; k < n; k += 2) {
    int a = order[k], b = order[k + 1];
    if (!fr[a] || !fr[b] || !match(a, b)) return 0;
    choose(a), choose(b);
    if (!gone(a) || !gone(b)) return 0;
  }
  return 1;
}

static void check_rules(void) {
  ov = 0, seed = 7;
  new_game(0);
  for (int i = 0; i < NT; i++) CHECK(fr[i] == free_by_hand(i), "tile %d's freedom", i);
  int nf = 0;
  for (int i = 0; i < NT; i++) nf += fr[i];
  /* at the start of the Turtle: the ends of the first layer's rows but the middle two (shut in by the end tiles),
     two of the three end tiles, the ends of the second and third layers' rows, and the top: 14 + 12 + 8 + 1 */
  CHECK(nf == 35, "the Turtle starts with %d free tiles, not 35", nf);
  /* matching */
  for (int i = 0; i < NT; i++) sv.face[i] = (uint8_t)(i < 136 ? i / 4 : i - 102);
  CHECK(family(34) == family(37) && family(38) == family(41) && family(34) != family(38) && family(33) != family(34),
        "flowers and seasons");
  sv.face[0] = 3, sv.face[1] = 3, sv.face[2] = 4, sv.face[3] = 35, sv.face[4] = 36, sv.face[5] = 39;
  CHECK(match(0, 1) && !match(0, 2) && match(3, 4) && !match(4, 5), "what matches");
  /* picking: a tile, another that doesn't match it (picked instead), the same again (put back) */
  seed = 11;
  new_game(0);
  int a = order[0], b = order[1], c = -1;
  for (int i = 0; i < NT && c < 0; i++)
    if (fr[i] && !match(i, a)) c = i;
  choose(a);
  CHECK(sv.pick == a, "a tile picked");
  if (c >= 0) {
    choose(c);
    CHECK(sv.pick == c, "picking one that doesn't match");
    choose(c);
    CHECK(sv.pick == NONE, "unpicked");
  }
  choose(a), choose(b);
  CHECK(gone(a) && gone(b) && sv.pairs == 1 && left == NT - 2, "a pair taken");
  undo();
  CHECK(!gone(a) && !gone(b) && sv.pairs == 0 && left == NT, "undo");
  undo();
  CHECK(sv.pairs == 0 && left == NT, "nothing to undo");
  /* a hint: two free tiles that match, another pair the next time */
  hint();
  int ha = hint_a, hb = hint_b;
  CHECK(ha >= 0 && fr[ha] && fr[hb] && match(ha, hb), "a hint");
  hint();
  CHECK(hint_a >= 0 && (hint_a != ha || hint_b != hb || moves == 1), "the next hint");
}

static void check_deals(void) {
  clock_t t0 = clock();
  int deals = 0;
  for (int l = 0; l < LAYOUTS; l++)
    for (int s = 1; s <= 60; s++, deals++) {
      seed = 0x2545F491u * (uint32_t)s + (uint32_t)l, ov = 0;
      new_game(l);
      int count[ART_KINDS] = {0};
      for (int i = 0; i < NT; i++) count[kind(i)]++;
      for (int k = 0; k < ART_KINDS; k++) CHECK(count[k] == (k < FLOWER ? 4 : 1), "%s: %d of kind %d", layout_name[l], count[k], k);
      uint32_t before = sv.best[l];
      sv.ms = 1000u * (uint32_t)s;
      CHECK(solve(NT), "%s, seed %d: the deal can't be won as laid", layout_name[l], s);
      CHECK(left == 0 && ov == OV_WON && !(sv.flags & 1), "%s: won", layout_name[l]);
      CHECK(sv.best[l] == (before && before < sv.ms ? before : sv.ms), "%s: the best time", layout_name[l]);
    }
  printf("%d deals won, %.1f ms each\n", deals, (double)(clock() - t0) * 1000 / CLOCKS_PER_SEC / deals);
  /* shuffles: halfway through, the tiles left laid again so they can still be won */
  for (int l = 0; l < LAYOUTS; l++)
    for (int s = 1; s <= 25; s++) {
      seed = 977u * (uint32_t)s + (uint32_t)l, ov = 0;
      new_game(l);
      uint8_t ord[NT];
      memcpy(ord, order, NT);
      int take = 2 * (int)(rnd() % 60);
      for (int k = 0; k < take; k += 2) choose(ord[k]), choose(ord[k + 1]);
      int count[ART_KINDS] = {0}, after[ART_KINDS] = {0};
      for (int i = 0; i < NT; i++)
        if (!gone(i)) count[kind(i)]++;
      shuffle();
      for (int i = 0; i < NT; i++)
        if (!gone(i)) after[kind(i)]++;
      CHECK(!memcmp(count, after, sizeof count), "a shuffle changed the tiles");
      CHECK(solve(left), "%s, seed %d: the shuffle can't be won as laid", layout_name[l], s);
    }
}

static void check_saves(void) {
  memset(files, 0, sizeof files);
  seed = 5, ov = 0;
  new_game(2);
  choose(order[0]), choose(order[1]);
  sv.ms = 123456, sv.best[1] = 99000;
  save();
  uint8_t kept[sizeof sv];
  memcpy(kept, &sv, sizeof sv);
  memset(&sv, 0, sizeof sv);
  load();
  CHECK(!memcmp(kept, &sv, sizeof sv), "the save read back");
  files[find(SAVE_NAME)].data[40] ^= 1; /* damaged: from the copy */
  memset(&sv, 0, sizeof sv);
  load();
  CHECK(!memcmp(kept, &sv, sizeof sv), "the copy read back");
  files[find(SAVE_NAME)].len = 0;       /* gone, as after an install: from the copy */
  memset(&sv, 0, sizeof sv);
  load();
  CHECK(!memcmp(kept, &sv, sizeof sv), "the copy read back after an install");
  files[find(COPY_NAME)].data[120] = files[find(COPY_NAME)].data[120] == 'A' ? 'B' : 'A'; /* both damaged */
  files[0].len = 5;
  load();
  CHECK(sv.flags == 0 && sv.best[1] == 0, "a fresh start");
  /* no game yet, how to play read: kept */
  sv.flags = 2;
  save();
  memset(&sv, 0, sizeof sv);
  load();
  CHECK(sv.flags == 2, "how to play read, no game");
}

/* the keys that take the cursor from tile c to tile t (the arrows alone), found by trying them */
static const int arrow_key[4] = {eadk_key_left, eadk_key_right, eadk_key_up, eadk_key_down};
static bool path(int t) {
  int from[NT], how[NT], queue[NT], qh = 0, qt = 0, c = sv.cur;
  for (int i = 0; i < NT; i++) from[i] = -1;
  from[c] = c, queue[qt++] = c;
  while (qh < qt && from[t] < 0) {
    int u = queue[qh++];
    for (int d = 0; d < 4; d++) {
      sv.cur = (uint8_t)u;
      step(d < 2 ? (d ? 1 : -1) : 0, d < 2 ? 0 : (d == 3 ? 1 : -1));
      int v = sv.cur;
      if (from[v] < 0) from[v] = u, how[v] = d, queue[qt++] = v;
    }
  }
  sv.cur = (uint8_t)c;
  if (from[t] < 0) return false;
  int keys[NT], n = 0;
  for (int v = t; v != c; v = from[v]) keys[n++] = arrow_key[how[v]];
  while (n) press(keys[--n], 130);
  sv.cur = (uint8_t)t;
  return true;
}

/* a game won with the keys: how to play, the Turtle, then every pair, reached with the arrows */
static void check_keys_win(void) {
  memset(files, 0, sizeof files);
  clock_ms = 1000, random_value = 0x1234567u;
  ft = 1500;
  press(eadk_key_ok, 400);  /* how to play read */
  press(eadk_key_ok, 600);  /* the Turtle */
  press(eadk_key_home, 300);
  run();
  CHECK(sv.flags == 3 && sv.layout == 0, "a game started with the keys");
  uint8_t ord[NT];
  memcpy(ord, order, NT);
  keep_files();
  /* the keys, worked out on the game as it goes */
  ft = 1500;
  int unreached = 0;
  for (int k = 0; k < NT; k += 2) {
    for (int h = 0; h < 2; h++) {
      int t = ord[k + h];
      if (!path(t)) unreached++, sv.cur = (uint8_t)t; /* (not with the arrows: then it fails below) */
      press(eadk_key_ok, 160);
      choose(t);
    }
    ov = 0;
  }
  CHECK(!unreached, "%d tiles the arrows don't reach", unreached);
  press(eadk_key_ok, 500); /* the win's card: New game */
  press(eadk_key_back, 300); /* no game: back quits */
  press(eadk_key_home, 300);
  back_files();
  int presses_made = nscript;
  run();
  CHECK(!(sv.flags & 1) && sv.wins[0] == 1 && sv.best[0] > 0 && sv.pairs == 72, "the game won with the keys");
  printf("a Turtle won with %d keys in %u:%02u\n", presses_made, sv.best[0] / 60000, sv.best[0] / 1000 % 60);
}

static void fuzz(void) {
  static const int keys[] = {eadk_key_left, eadk_key_right, eadk_key_up, eadk_key_down, eadk_key_ok, eadk_key_ok,
                             eadk_key_ok, eadk_key_exe, eadk_key_back, eadk_key_shift, eadk_key_backspace,
                             eadk_key_two, eadk_key_eight, eadk_key_four, eadk_key_six, eadk_key_zero};
  uint32_t r = 2463534242u;
  memset(files, 0, sizeof files);
  for (int s = 0; s < 120; s++) {
    random_value = r;
    ft = 300;
    int n = 40 + (int)(r % 200);
    for (int i = 0; i < n; i++) {
      r ^= r << 13, r ^= r >> 17, r ^= r << 5;
      press(keys[r % 16], 20 + (int)(r >> 8) % 120);
    }
    press(r & 1 ? eadk_key_home : eadk_key_on_off, 100);
    run();
    if (s % 7 == 3 && find(SAVE_NAME) >= 0) files[find(SAVE_NAME)].data[r % sizeof sv] ^= (uint8_t)(1 + r % 255);
    if (s % 11 == 5 && find(COPY_NAME) >= 0) files[find(COPY_NAME)].data[r % 300] = (uint8_t)r;
    if (s % 13 == 6 && find(SAVE_NAME) >= 0) files[find(SAVE_NAME)].len = r % 400 + 1;
    memset(&sv, 0, sizeof sv);
    load();
    CHECK(sv.magic == 0 || valid(), "session %d left a save that doesn't read back", s);
  }
}

/* a film: how to play, the Turtle, a few pairs taken with the keys, a hint, a wrong pick, undo, the menu */
static void film(void) {
  memset(files, 0, sizeof files);
  clock_ms = 1000, random_value = 0x2468ACEu, frame = 0;
  ft = 1200;
  shots[nshots++] = 1100; /* how to play */
  press(eadk_key_ok, 700);
  shots[nshots++] = ft - 100; /* the layouts */
  press(eadk_key_ok, 800);
  press(eadk_key_home, 300);
  quiet = 1;
  run();
  quiet = 0;
  uint8_t ord[NT];
  memcpy(ord, order, NT);
  keep_files();
  ft = 900;
  for (int k = 0; k < 12; k += 2) {
    for (int h = 0; h < 2; h++) {
      if (!path(ord[k + h])) sv.cur = ord[k + h];
      ft += 150;
      press(eadk_key_ok, 450);
      choose(ord[k + h]);
      if (k == 6 && !h) shots[nshots++] = ft - 100; /* a tile picked */
    }
  }
  press(eadk_key_shift, 1500);
  shots[nshots++] = ft - 600; /* a hint */
  press(eadk_key_backspace, 900);
  press(eadk_key_back, 900);
  shots[nshots++] = ft - 300; /* the menu */
  press(eadk_key_back, 600);
  press(eadk_key_home, 200);
  back_files();
  run();
}

/* pictures for the docs: DIR/pic_N.ppm */
static void pic(const char *path) {
  gfx_buf = strip;
  draw();
  ppm(path);
}
static void pics(const char *d) {
  char p[512];
  memset(files, 0, sizeof files);
  now = 0;
  np_lz_unpack(art_lz, sizeof art_lz, art, sizeof art);
  for (int i = 0; i < 16; i++) pal[i] = gfx_rgb(art_pal[i]);
  for (int l = 0; l < LAYOUTS; l++) { /* each layout, a few pairs in, a tile picked */
    seed = 0x51ED270Bu + (uint32_t)l * 77, ov = 0;
    new_game(l);
    for (int k = 0; k < 2 * (3 + l); k++) choose(order[k]);
    sv.ms = 61000u + 23000u * (uint32_t)l;
    step(1, 0), step(0, 1);
    choose(sv.cur);
    step(-1, 0);
    snprintf(p, sizeof p, "%s/pic_%d.ppm", d, l), pic(p);
  }
  ov = OV_NEW, menu = 0, sv.best[0] = 512000, sv.best[2] = 431000;
  snprintf(p, sizeof p, "%s/pic_4.ppm", d), pic(p);
  ov = 0, seed = 99;
  new_game(0);
  for (int k = 0; k < NT; k++) choose(order[k]);
  sv.ms = 488000;
  snprintf(p, sizeof p, "%s/pic_5.ppm", d), pic(p);
  ov = OV_STUCK, menu = 0, sv.flags |= 1;
  snprintf(p, sizeof p, "%s/pic_6.ppm", d), pic(p);
}

int main(int argc, char **argv) {
  if (argc > 2 && !strcmp(argv[1], "pics")) {
    pics(argv[2]);
    return 0;
  }
  setvbuf(stdout, NULL, _IONBF, 0);
  if (argc > 2 && !strcmp(argv[1], "film")) {
    dir = argv[2];
    film();
    return 0;
  }
  check_layouts();
  check_rules();
  printf("layouts and rules checked\n");
  check_deals();
  printf("rules checked\n");
  check_saves();
  check_keys_win();
  fuzz();
  printf(fails ? "%d checks failed\n" : "all checks passed\n", fails);
  return fails != 0;
}
