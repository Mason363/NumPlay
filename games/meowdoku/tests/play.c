/* Meowdoku on a computer, for tests and pictures (make host, make test).
 *
 *   play check        every puzzle in all 8 turns: exactly one solution, all its colours, distinct colours;
 *                     the hints alone solve every puzzle without a wrong step; the dealing over 1000 levels
 *                     for a player who always wins and one who fails each level once; the rules (a wrong
 *                     cat, three of them, undo, locate); a level won with the keys; bad saves; random keys
 *   play film DIR     a game played with the keys, the screen saved as it goes (DIR/f_NNNN.ppm),
 *                     and the screens of the docs (DIR/shot_*.ppm)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define main meowdoku_main
#include "../src/main.c"
#undef main

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
static uint8_t file[4096];
static uint32_t file_len, has_file;
const uint8_t *np_file_read(const char *name, uint32_t *len) { return has_file ? (*len = file_len, file) : NULL; }
bool np_file_write(const char *name, const void *data, uint32_t len) {
  if (len > sizeof file) return false;
  memcpy(file, data, len), file_len = len, has_file = 1;
  return true;
}

/* the keys: a script of presses, each held `hold` ms; the clock moves on as the game sleeps */
static uint64_t clock_ms;
static struct { uint64_t t; int key, hold; } script[8192];
static int nscript, nshots, frame;
static uint64_t shots[64];
static const char *dir;
uint64_t eadk_timing_millis(void) { return clock_ms; }
uint32_t eadk_random(void) { return 0x9E3779B9u; }
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
    if (dir && clock_ms % 40 == 0) snprintf(path, sizeof path, "%s/f_%04d.ppm", dir, frame++), ppm(path);
  }
}
uint64_t eadk_keyboard_scan(void) {
  uint64_t k = 0;
  for (int i = 0; i < nscript; i++)
    if (clock_ms >= script[i].t && clock_ms < script[i].t + (uint64_t)script[i].hold) k |= 1ull << script[i].key;
  return k;
}

/* (tests/test_games.py reads meowdoku.sav at these places) */
_Static_assert(sizeof sv == 212 && __builtin_offsetof(__typeof__(sv), cell) == 26, "the save's layout");
static int fails;
#define CHECK(c, ...) do { if (!(c) && fails++ < 20) { printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

/* the rules, checked apart from the solver: n cats, one per row, column and colour, none touching */
static bool valid(const int *s) {
  int cols = 0, regs = 0;
  for (int r = 0; r < n; r++) {
    if (s[r] < 0 || s[r] >= n) return false;
    cols |= 1 << s[r], regs |= 1 << reg[r * n + s[r]];
    if (r && abs(s[r] - s[r - 1]) <= 1) return false;
  }
  return cols == (1 << n) - 1 && regs == (1 << n) - 1;
}

static void fresh(void) {
  memset(&sv, 0, sizeof sv);
  sv.magic = 'M', sv.version = 1, sv.level = 1, sv.strat = 1, sv.seed = 12345;
  ov = 0, hint.kind = 0, ucount = 0, err_until = 0;
}

/* every puzzle, every turn */
static void check_puzzles(void) {
  int sizes[12] = {0};
  for (int p = 0; p < NPUZ; p++) {
    int sp = -1;
    for (int i = 0; i < NSPECIAL; i++)
      if (lv_special[i][2] == p) sp = i;
    int size = 0;
    for (int q = 0; q < NPOOL; q++)
      if (p >= pool_first(q) && p < pool_first(q) + lv_pool[q][4]) size = lv_pool[q][0];
    for (int k = 0; k < NLK; k++)
      if (p >= lv_lk[k][3] && p < lv_lk[k][3] + lv_lk[k][4]) size = lv_lk[k][0];
    if (sp >= 0) size = lv_special[sp][1];
    CHECK(size, "puzzle %d belongs to no pool", p);
    sizes[size]++;
    for (int t = 0; t < (sp >= 0 ? 1 : 8); t++) {
      fresh();
      sv.puz = (uint16_t)p, sv.size = (uint8_t)size, sv.t = (uint8_t)t, sv.special = sp >= 0;
      sv.level = sp >= 0 ? lv_special[sp][0] : 1000;
      load_puzzle();
      int seen = 0, used = 0;
      for (int i = 0; i < n * n; i++) seen |= 1 << reg[i];
      CHECK(seen == (1 << n) - 1, "puzzle %d turn %d: colours missing", p, t);
      CHECK(solve() == 1, "puzzle %d turn %d: %d solutions", p, t, nsol);
      solve();
      CHECK(valid(sol), "puzzle %d turn %d: the solution breaks the rules", p, t);
      for (int i = 0; i < n; i++) {
        CHECK(color[i] >= 0 && color[i] < 12 && !(used >> color[i] & 1), "puzzle %d: colour %d twice", p, color[i]);
        used |= 1 << color[i];
      }
    }
  }
  printf("%d puzzles (%d bytes): ", NPUZ, (int)sizeof lv_data);
  for (int s = 4; s <= 11; s++)
    if (sizes[s]) printf("%dx%d %d  ", s, s, sizes[s]);
  printf("\n");
}

/* the hints alone, from an empty board (and from levels 1-10's prefilled cat), never wrong */
static void check_hints(void) {
  int used[10] = {0}, steps = 0, worst = 0;
  for (int p = 0; p < NPUZ; p++) {
    fresh();
    int size = 0, sp = -1;
    for (int q = 0; q < NPOOL; q++)
      if (p >= pool_first(q) && p < pool_first(q) + lv_pool[q][4]) size = lv_pool[q][0];
    for (int k = 0; k < NLK; k++)
      if (p >= lv_lk[k][3] && p < lv_lk[k][3] + lv_lk[k][4]) size = lv_lk[k][0];
    for (int i = 0; i < NSPECIAL; i++)
      if (lv_special[i][2] == p) sp = i, size = lv_special[i][1];
    sv.puz = (uint16_t)p, sv.size = (uint8_t)size, sv.t = (uint8_t)(p % 8), sv.special = sp >= 0;
    if (sp >= 0) sv.t = 0;
    sv.level = sp >= 0 ? lv_special[sp][0] : 1000;
    start_level();
    int k = 0;
    for (; k < 400 && !ov; k++) {
      clock_t h0 = clock();
      find_hint();
      int ms = (int)((clock() - h0) * 1000 / CLOCKS_PER_SEC);
      worst = ms > worst ? ms : worst;
      CHECK(hint.kind, "puzzle %d: no hint (%d cats)", p, cats());
      if (!hint.kind) break;
      used[hint.kind]++;
      for (int i = 0; i < n * n; i++) {
        bool cat_cell = sol[i / n] == i % n;
        if (hint.hl[i] == 2 && hint.kind != H_UNMARK && hint.kind < H_ONE_ROW) CHECK(!cat_cell, "puzzle %d: hint %d X's a cat's cell", p, hint.kind);
        if (hint.hl[i] == 2 && hint.kind >= H_COLOR_LINE) CHECK(!cat_cell, "puzzle %d: hint %d X's a cat's cell", p, hint.kind);
      }
      if (hint.kind >= H_ONE_ROW && hint.kind <= H_ONE_COLOR) CHECK(sol[hint.cell / n] == hint.cell % n, "puzzle %d: a cat in the wrong cell", p);
      int lives = sv.lives;
      apply_hint();
      CHECK(sv.lives == lives, "puzzle %d: a hint cost a fish", p);
    }
    steps += k;
    CHECK(ov == OV_WON, "puzzle %d: the hints don't finish it", p);
  }
  printf("hints: %d puzzles solved in %d steps (marks %d, X off %d, one place %d, colour in a line %d, line in a "
         "colour %d, sets %d, no room %d); the slowest hint %d ms\n",
         NPUZ, steps, used[H_MARKS], used[H_UNMARK], used[H_ONE_ROW] + used[H_ONE_COL] + used[H_ONE_COLOR], used[H_COLOR_LINE],
         used[H_LINE_COLOR], used[H_SETS], used[H_CHAIN], worst);
}

/* the dealing: sizes, specials, hard levels, ranks, the pools' turns */
static void check_dealing(void) {
  for (int player = 0; player < 2; player++) {
    fresh();
    deal();
    int hard_ok = 0, top = 0;
    for (int lv = 1; lv <= 1000; lv++) {
      CHECK(sv.level == lv, "level %d dealt as %d", lv, sv.level);
      int sp = special_at(lv);
      CHECK(sv.size == (sp >= 0 ? lv_special[sp][1] : size_at(lv)), "level %d: size %d", lv, sv.size);
      CHECK(!!sv.special == (sp >= 0), "level %d: special", lv);
      if (sp >= 0) CHECK(sv.puz == lv_special[sp][2], "level %d: not its special puzzle", lv);
      if (sp < 0 && is_hard(lv)) {
        int p = find_pool(sv.size, 5, lv >= 51);
        hard_ok += p >= 0 && sv.puz >= pool_first(p) && sv.puz < pool_first(p) + lv_pool[p][4];
      }
      CHECK(sv.puz < NPUZ && sv.t < 8, "level %d: puzzle %d turn %d", lv, sv.puz, sv.t);
      top = sv.strat > top ? sv.strat : top;
      if (player) { /* fails once, then wins */
        failed();
        ov = 0;
      }
      won();
      ov = 0;
    }
    int hard = 0;
    for (int lv = 30; lv <= 1000; lv += 10) hard += special_at(lv) < 0;
    CHECK(hard_ok == hard, "hard levels from the wrong pool (%d of %d)", hard_ok, hard);
    printf("dealing, %s: 1000 levels, difficulty up to %d, now %d\n", player ? "a level failed each time" : "clean wins", top, sv.strat);
    if (!player) CHECK(top == 6, "clean wins don't reach the top difficulty");
    else CHECK(sv.strat <= 2, "failing doesn't bring the difficulty down");
  }
}

/* the rules: a wrong cat, three of them, undo, locate */
static void check_rules(void) {
  fresh();
  sv.level = 20 + 3, sv.size = (uint8_t)size_at(23);
  deal();
  start_level();
  int wrong = sol[0] == 0 ? 1 : 0;
  set_mark(5, MARK, 1);
  CHECK(sv.cell[5] == MARK, "an X isn't put");
  undo_move();
  CHECK(sv.cell[5] == EMPTY, "undo doesn't take the X back");
  place(wrong);
  CHECK(sv.cell[wrong] == MARK && sv.lives == 2 && !sv.perfect, "a wrong cat isn't an X and a fish lost");
  place(sol[0]);
  CHECK(sv.cell[sol[0]] == CAT && sv.lives == 2, "the right cat isn't placed");
  int lv = sv.level;
  for (int r = 1; r < n && sv.lives; r++) {
    int c = sol[r] ? 0 : 1;
    if (sv.cell[r * n + c] == EMPTY) place(r * n + c);
  }
  CHECK(!sv.lives && ov == OV_FAILED && sv.retried && sv.level == lv, "three wrong cats don't fail the level");
  ov = 0;
  start_level();
  CHECK(sv.lives == 3 && cats() == 0, "trying again doesn't start over");
  for (int r = 0; r < n && !ov; r++) locate();
  CHECK(ov == OV_WON && sv.level == lv + 1 && sv.dirty == 0, "locate doesn't finish the level");
}

/* the keys: arrows to a cell, then OK twice */
static uint64_t ft;
static int fr, fc;
static void press(int key, int wait) { script[nscript].t = ft, script[nscript].hold = 60, script[nscript++].key = key, ft += (uint64_t)wait; }
static void go(int r, int c) {
  while (fr < r) press(eadk_key_down, 120), fr++;
  while (fr > r) press(eadk_key_up, 120), fr--;
  while (fc < c) press(eadk_key_right, 120), fc++;
  while (fc > c) press(eadk_key_left, 120), fc--;
}
static void reset_save(void) {
  static const uint8_t bad[4] = {0};
  np_file_write(SAVE_NAME, bad, 4);
}

/* level 1 played with the keys from a new save: the guide, then each cat placed with OK twice */
static void check_keys(void) {
  reset_save();
  fresh();
  deal();
  load_puzzle(); /* (the game will deal the same) */
  int s[NMAX], size = n;
  for (int r = 0; r < n; r++) s[r] = sol[r];
  nscript = 0, ft = clock_ms + 300, fr = fc = size / 2;
  press(eadk_key_ok, 400); /* the guide */
  for (int r = 0; r < size; r++) {
    go(r, s[r]);
    press(eadk_key_ok, 150), press(eadk_key_ok, 300);
  }
  press(eadk_key_ok, 400); /* Next level */
  press(eadk_key_home, 300);
  meowdoku_main();
  uint32_t len;
  const uint8_t *d = np_file_read(SAVE_NAME, &len);
  CHECK(d && len == sizeof sv, "no save");
  if (d) memcpy(&sv, d, sizeof sv);
  CHECK(sv.level == 2 && sv.wins == 1 && (sv.flags & 2) && sv.lives == 3, "level 1 isn't won with the keys (level %d)", sv.level);
}

/* bad saves start over; random keys never crash */
static void check_fuzz(void) {
  uint32_t r = 7;
  for (int run = 0; run < 300; run++) {
    uint8_t junk[sizeof sv];
    r = r * 1103515245u + 12345u;
    if (run % 3 == 0) { /* a save from a real game, altered */
      fresh();
      sv.level = (uint16_t)(1 + r % 600);
      deal();
      start_level();
      memcpy(junk, &sv, sizeof sv);
      junk[(r >> 8) % sizeof sv] ^= (uint8_t)(r >> 16);
    } else
      for (uint32_t i = 0; i < sizeof junk; i++) r = r * 1103515245u + 12345u, junk[i] = (uint8_t)(r >> 16);
    np_file_write(SAVE_NAME, junk, run % 5 ? sizeof junk : 7);
    static const int keys[] = {0, 1, 2, 3, 4, 4, 4, 5, 12, 13, 17, 52, 4, 1, 2};
    nscript = 0, ft = clock_ms + 100;
    for (int k = 0; k < 120; k++) {
      r = r * 1103515245u + 12345u;
      script[nscript].t = ft, script[nscript].hold = 30 + (int)(r >> 20) % 300;
      script[nscript++].key = keys[(r >> 8) % (sizeof keys / sizeof *keys)];
      ft += 20 + (r >> 12) % 200;
    }
    press(eadk_key_home, 100);
    meowdoku_main();
    uint32_t len;
    const uint8_t *d = np_file_read(SAVE_NAME, &len);
    CHECK(d && len == sizeof sv, "run %d: no save", run);
    if (d) memcpy(&sv, d, sizeof sv);
    CHECK(sane(), "run %d: a save it can't read back", run);
  }
  printf("300 runs of random keys on good and broken saves\n");
}

/* A film: the guide, level 1 won with the keys, then a level with a mistake, a hint and the menu. */
static void film(void) {
  reset_save();
  fresh();
  deal();
  load_puzzle();
  int s[NMAX], size = n;
  for (int r = 0; r < n; r++) s[r] = sol[r];
  nscript = 0, ft = 300, fr = fc = size / 2;
  shots[nshots++] = 250; /* how to play */
  press(eadk_key_ok, 1600);
  for (int r = 0; r < size; r++) {
    go(r, s[r]);
    press(eadk_key_ok, 150), press(eadk_key_ok, 350);
  }
  shots[nshots++] = ft + 300; /* level cleared */
  ft += 600;
  press(eadk_key_ok, 900);
  press(eadk_key_home, 300);
  meowdoku_main();
  printf("level 12 from frame %d\n", frame);
  /* level 12 (10 x 10), part way */
  fresh();
  sv.level = 12, sv.strat = 2, sv.flags = 1;
  deal();
  start_level();
  for (int r = 0; r < 4; r++) sv.cell[r * n + sol[r]] = CAT;
  find_hint();
  apply_hint();
  sv.flags |= 2;
  save();
  nscript = 0, ft = clock_ms + 300, fr = fc = n / 2;
  int wrong = 4 * n + (sol[4] + 3) % n;
  go(4, wrong % n);
  press(eadk_key_ok, 120), press(eadk_key_ok, 700);
  go(6, 2);
  press(eadk_key_ok, 300);
  press(eadk_key_right, 150), press(eadk_key_right, 500);
  shots[nshots++] = ft; /* the game */
  press(eadk_key_shift, 1500);
  shots[nshots++] = ft - 200; /* a hint */
  press(eadk_key_ok, 900);
  press(eadk_key_alpha, 1000);
  go(8, 1);
  press(eadk_key_ok, 150), press(eadk_key_ok, 700);
  press(eadk_key_back, 1200);
  shots[nshots++] = ft - 200; /* the menu */
  press(eadk_key_back, 600);
  press(eadk_key_home, 300);
  meowdoku_main();
}

/* the time a frame takes to draw, on the host (the calculator's is measured with tools/emu.py) */
static void check_speed(void) {
  fresh();
  sv.level = 16;
  deal();
  start_level();
  layout();
  gfx_buf = strip;
  clock_t t0 = clock();
  for (int i = 0; i < 50; i++) draw();
  printf("a 10x10 frame: %.2f ms on this computer\n", (double)(clock() - t0) * 1000 / CLOCKS_PER_SEC / 50);
}

int main(int argc, char **argv) {
  if (argc > 2 && !strcmp(argv[1], "film")) {
    dir = argv[2];
    film();
    return 0;
  }
  check_puzzles();
  check_hints();
  check_dealing();
  check_rules();
  check_keys();
  check_fuzz();
  check_speed();
  printf(fails ? "%d FAILED\n" : "all good\n", fails);
  return fails != 0;
}
