/* Finds OK presses that play a level to its end (make bot; build/bot LEVEL): a depth-first search over
 * the frames, saving and restoring the game's whole state (game.c's variables live in sections of their
 * own in this build), each state tried once. The presses it prints go into tests/play.c. */
#define main famidash_main
#include "../src/main.c"
#undef main
#include <stdio.h>
#include <stdlib.h>

void np_push(int x, int y, int w, int h, const color_t *px) { (void)x, (void)y, (void)w, (void)h, (void)px; }
void *np_alloc(uint32_t n) { (void)n; return 0; }
uint32_t plat_keys(void) { return 0; }
uint32_t plat_millis(void) { return 0; }
void plat_sleep(uint32_t ms) { (void)ms; }
uint32_t plat_random(void) { return 1; }
void plat_begin(void) {}
int plat_end(void) { return 0; }
bool plat_save(const char *name, const void *data, uint32_t len) { (void)name, (void)data, (void)len; return true; }
const uint8_t *plat_load(const char *name, uint32_t *len) { (void)name, (void)len; return 0; }

extern char bss_start[] __asm("section$start$__DATA$__fdbss");
extern char bss_end[] __asm("section$end$__DATA$__fdbss");
extern char data_start[] __asm("section$start$__DATA$__fddata");
extern char data_end[] __asm("section$end$__DATA$__fddata");
static size_t nb, nd;
static void snap(uint8_t *b) { memcpy(b, bss_start, nb), memcpy(b + nb, data_start, nd); }
static void unsnap(const uint8_t *b) { memcpy(bss_start, b, nb), memcpy(data_start, b + nb, nd); }
static uint64_t hash_state(void) {
  pad_t keep = joypad1;
  joypad1 = (pad_t){0, 0, 0};
  uint64_t h = 1469598103934665603ull;
  for (char *p = bss_start; p < bss_end; p++) h = (h ^ (uint8_t)*p) * 1099511628211ull;
  for (char *p = data_start; p < data_end; p++) h = (h ^ (uint8_t)*p) * 1099511628211ull;
  joypad1 = keep;
  return h;
}

#define MAXF 9000
#define HSIZE (1 << 25)
static uint64_t *seen;
static uint8_t hold[MAXF];
static long nodes;
static int deepest;
static bool seen_add(uint64_t h) {
  uint64_t k = h ? h : 1;
  for (uint32_t i = (uint32_t)(k & (HSIZE - 1));; i = (i + 1) & (HSIZE - 1)) {
    if (seen[i] == k) return false;
    if (!seen[i]) { seen[i] = k; return true; }
  }
}

static int dfs(int t, uint8_t *stack) {
  if (gameState == STATE_LVLDONE) return t;
  if (game_dead() || t >= MAXF - 1) return 0;
  if (++nodes % 200000 == 0) fprintf(stderr, "  %ld states, frame %d (deepest %d)\n", nodes, t, deepest);
  if (t > deepest) deepest = t;
  if (!seen_add(hash_state() ^ (uint64_t)t * 0x9E3779B97F4A7C15ull)) return 0;
  uint8_t *here = stack, *next = stack + nb + nd;
  snap(here);
  uint8_t first = t > 0 ? hold[t - 1] : 0; /* keep doing what we did: fewer key changes */
  for (int k = 0; k < 2; k++) {
    uint8_t c = k ? !first : first;
    if (k) unsnap(here);
    hold[t] = c;
    uint8_t h = c ? PAD_A : 0;
    joypad1.press = (uint8_t)(h & ~joypad1.hold), joypad1.release = (uint8_t)(~h & joypad1.hold), joypad1.hold = h;
    game_step();
    int r = dfs(t + 1, next);
    if (r) return r;
  }
  return 0;
}

int main(int argc, char **argv) {
  nb = (size_t)(bss_end - bss_start), nd = (size_t)(data_end - data_start);
  int lv = argc > 1 ? atoi(argv[1]) : 0;
  seen = calloc(HSIZE, sizeof *seen);
  uint8_t *stack = malloc((size_t)(nb + nd) * (MAXF + 2));
  gfx_buf = strip;
  ppu_init();
  rng_seed = 0xFDFDFDFD;
  level = (uint8_t)lv;
  game_begin();
  game_start();
  joypad1 = (pad_t){0, 0, 0};
  fprintf(stderr, "state: %zu bytes\n", nb + nd);
  if (argc > 2) { /* replay a list: start,len,... */
    for (int i = 2; i + 1 < argc; i += 2)
      for (int k = 0; k < atoi(argv[i + 1]); k++) hold[atoi(argv[i]) + k] = 1;
    for (int f = 0; f < MAXF; f++) {
      uint8_t h = hold[f] ? PAD_A : 0;
      joypad1.press = (uint8_t)(h & ~joypad1.hold), joypad1.release = (uint8_t)(~h & joypad1.hold), joypad1.hold = h;
      game_step();
      if (gameState == STATE_LVLDONE || game_dead()) { printf("%s at %d\n", game_dead() ? "dead" : "done", f + 1); return 0; }
    }
    return 0;
  }
  int r = dfs(0, stack);
  printf("level %d: %s, %ld states (deepest frame %d)\n", lv, r ? "done" : "no way found", nodes, deepest);
  if (r) {
    printf("{");
    for (int f = 0; f < r; f++)
      if (hold[f] && (f == 0 || !hold[f - 1])) {
        int e = f;
        while (e < r && hold[e]) e++;
        printf("%d,%d,", f, e - f);
      }
    printf("}\n");
  }
  return !r;
}
