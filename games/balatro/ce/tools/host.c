/* The TI-84 Plus CE version on a computer, for testing: the calculator's 8-bit screen and palette, its keys, its
 * appvars (the .bin files in gen/, as tools/mkart.py makes them) and the save (a file), with SDL 3 for a window.
 *
 * In the environment, as platform/sdl/np_sdl.c: NP_HEADLESS=1 for no window (and a clock that only moves when the
 * game sleeps: fast, and the same every run), NP_KEYS "1500-1700:ok,3000:back" holds keys from one time to another
 * (ms; NumWorks names as eadk_key_* or the CE's: 2nd enter clear mode del y= math vars), NP_SHOTS "2000:a.ppm,..."
 * saves the screen then, NP_QUIT_MS holds mode then (the game saves and ends), NP_SAVE the save's file
 * (default bin/host/NPbalato.bin). Keys in the window: arrows, Z or space 2nd, return enter, backspace clear,
 * escape mode, D del, S y=, A alpha, M math, V vars. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <eadk.h>
#include "ce.h"
#include "np_port.h"
#ifdef __has_include
#if __has_include(<SDL3/SDL.h>)
#include <SDL3/SDL.h>
#define HAVE_SDL 1
#endif
#endif

int balatro_main(void);

static uint8_t vram[SCR_W * SCR_H];
uint8_t *ce_vram = vram;
static uint16_t palette[256];
static int headless;
static uint64_t vclock, t0;
static long frames;

void ce_set_palette(int first, int n, const uint16_t *c) { memcpy(palette + first, c, (size_t)n * 2); }

static struct {
  char name[9];
  uint8_t *data;
  size_t size;
} vars[24];
const uint8_t *ce_appvar(const char *name, size_t *size) {
  for (int i = 0; i < 24; i++) {
    if (vars[i].data && !strcmp(vars[i].name, name)) {
      *size = vars[i].size;
      return vars[i].data;
    }
    if (!vars[i].data) {
      char path[64];
      snprintf(path, sizeof path, "gen/%s.bin", name);
      FILE *f = fopen(path, "rb");
      if (!f) return 0;
      fseek(f, 0, SEEK_END);
      long n = ftell(f);
      fseek(f, 0, SEEK_SET);
      vars[i].data = malloc((size_t)n + 16);
      if (fread(vars[i].data, 1, (size_t)n, f) != (size_t)n) n = 0;
      fclose(f);
      snprintf(vars[i].name, sizeof vars[i].name, "%s", name);
      vars[i].size = (size_t)n;
      *size = (size_t)n;
      return vars[i].data;
    }
  }
  return 0;
}

/* ---- zx0 (as the CE's C library unpacks it) */
static const uint8_t *zin;
static uint8_t zmask, zbits, zlast, zback;
static uint8_t zbyte(void) { return zlast = *zin++; }
static int zbit(void) {
  if (zback) {
    zback = 0;
    return zlast & 1;
  }
  zmask >>= 1;
  if (!zmask) zmask = 128, zbits = zbyte();
  return zbits & zmask ? 1 : 0;
}
static int zgamma(int inv) {
  int v = 1;
  while (!zbit()) v = v << 1 | (zbit() ^ inv);
  return v;
}
void zx0_Decompress(void *dst, const void *src) {
  uint8_t *out = dst;
  zin = src, zmask = 0, zback = 0;
  int off = 1, len;
  for (;;) {
    len = zgamma(0);
    while (len--) *out++ = zbyte();
    if (!zbit()) {
      len = zgamma(0);
      while (len--) *out = out[-off], out++;
      if (!zbit()) continue;
    }
    for (;;) {
      off = zgamma(1);
      if (off == 256) return;
      off = off * 128 - (zbyte() >> 1);
      zback = 1;
      len = zgamma(0) + 1;
      while (len--) *out = out[-off], out++;
      if (!zbit()) break;
    }
  }
}

/* ---- time */
#ifdef HAVE_SDL
static uint64_t real_ms(void) { return SDL_GetTicks(); }
#else
#include <time.h>
static uint64_t real_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}
#endif
uint64_t eadk_timing_millis(void) { return headless ? vclock : real_ms() - t0; }
uint32_t ce_ms(void) { return (uint32_t)eadk_timing_millis(); }
void eadk_timing_msleep(uint32_t ms) {
  if (headless) vclock += ms;
#ifdef HAVE_SDL
  else SDL_Delay(ms);
#endif
}

/* ---- the screen */
static void to_rgb(uint8_t *rgb) {
  for (int i = 0; i < SCR_W * SCR_H; i++) {
    uint16_t c = palette[vram[i]];
    int r = c >> 10 & 31, g = c >> 5 & 31, b = c & 31;
    rgb[3 * i] = (uint8_t)(r << 3 | r >> 2), rgb[3 * i + 1] = (uint8_t)(g << 3 | g >> 2), rgb[3 * i + 2] = (uint8_t)(b << 3 | b >> 2);
  }
}
static void shot(const char *path) {
  static uint8_t rgb[SCR_W * SCR_H * 3];
  to_rgb(rgb);
  FILE *f = fopen(path, "wb");
  if (!f) return;
  fprintf(f, "P6\n%d %d\n255\n", SCR_W, SCR_H);
  fwrite(rgb, 1, sizeof rgb, f);
  fclose(f);
}

/* ---- keys */
static const struct {
  const char *name;
  int key;
} NAMES[] = {{"left", eadk_key_left}, {"up", eadk_key_up}, {"down", eadk_key_down}, {"right", eadk_key_right},
             {"ok", eadk_key_ok}, {"2nd", eadk_key_ok}, {"back", eadk_key_back}, {"clear", eadk_key_back},
             {"home", eadk_key_home}, {"mode", eadk_key_home}, {"shift", eadk_key_shift}, {"y=", eadk_key_shift},
             {"alpha", eadk_key_alpha}, {"var", eadk_key_var}, {"vars", eadk_key_var}, {"toolbox", eadk_key_toolbox},
             {"math", eadk_key_toolbox}, {"backspace", eadk_key_backspace}, {"del", eadk_key_backspace},
             {"exe", eadk_key_exe}, {"enter", eadk_key_exe}};
static eadk_keyboard_state_t script_keys(uint64_t now) {
  const char *s = getenv("NP_KEYS");
  eadk_keyboard_state_t k = 0;
  while (s && *s) {
    unsigned long a = strtoul(s, (char **)&s, 10), b = a + 90;
    if (*s == '-') b = strtoul(s + 1, (char **)&s, 10);
    if (*s++ != ':') break;
    size_t n = strcspn(s, ",");
    for (size_t i = 0; i < sizeof NAMES / sizeof NAMES[0]; i++)
      if (strlen(NAMES[i].name) == n && !strncmp(s, NAMES[i].name, n) && now >= a && now <= b)
        k |= (eadk_keyboard_state_t)1 << NAMES[i].key;
    s += n;
    if (*s == ',') s++;
  }
  const char *q = getenv("NP_QUIT_MS");
  if (q && now >= strtoull(q, 0, 10)) k |= (eadk_keyboard_state_t)1 << eadk_key_home;
  return k;
}
static void script_shots(uint64_t now) {
  static uint64_t done_until;
  const char *s = getenv("NP_SHOTS");
  while (s && *s) {
    unsigned long t = strtoul(s, (char **)&s, 10);
    if (*s++ != ':') break;
    size_t n = strcspn(s, ",");
    char path[256];
    snprintf(path, sizeof path, "%.*s", (int)n, s);
    if (t > done_until && t <= now) shot(path);
    s += n;
    if (*s == ',') s++;
  }
  done_until = now;
}

#ifdef HAVE_SDL
static SDL_Window *win;
static SDL_Renderer *ren;
static SDL_Texture *tex;
static void present(void) {
  static uint8_t rgb[SCR_W * SCR_H * 3];
  to_rgb(rgb);
  SDL_UpdateTexture(tex, 0, rgb, SCR_W * 3);
  SDL_RenderClear(ren);
  SDL_RenderTexture(ren, tex, 0, 0);
  SDL_RenderPresent(ren);
}
static const struct {
  SDL_Scancode sc;
  int key;
} KEYMAP[] = {{SDL_SCANCODE_LEFT, eadk_key_left}, {SDL_SCANCODE_RIGHT, eadk_key_right}, {SDL_SCANCODE_UP, eadk_key_up},
              {SDL_SCANCODE_DOWN, eadk_key_down}, {SDL_SCANCODE_Z, eadk_key_ok}, {SDL_SCANCODE_SPACE, eadk_key_ok},
              {SDL_SCANCODE_RETURN, eadk_key_exe}, {SDL_SCANCODE_BACKSPACE, eadk_key_back},
              {SDL_SCANCODE_ESCAPE, eadk_key_home}, {SDL_SCANCODE_D, eadk_key_backspace},
              {SDL_SCANCODE_S, eadk_key_shift}, {SDL_SCANCODE_A, eadk_key_alpha}, {SDL_SCANCODE_M, eadk_key_toolbox},
              {SDL_SCANCODE_V, eadk_key_var}};
#endif

/* ---- NP_BOT=1: a player for long tests. It acts through the rules' own calls (what the screens' keys call),
   when the screens have shown the last move; it starts a run from the title with the OK key. NP_BOT_ANTE: the ante
   it plays to (then holds Home). */
#include "ev.h"
#include "game.h"
#include "ui.h"
int bot_ante_reached, bot_wins, bot_runs;
static int bot_value(const uint8_t *cards, int n, uint8_t *keep) {
  handinfo_t hi;
  evaluate_hand(cards, n, &hi);
  int chips = (int)hand_chips(hi.hand);
  for (int i = 0; i < hi.nscoring; i++) chips += card_chip_nominal(R.cards[cards[hi.scoring[i]]].rank);
  if (keep) {
    memset(keep, 0, 8);
    for (int i = 0; i < hi.nscoring; i++) keep[hi.scoring[i]] = 1;
  }
  return chips * (int)hand_mult(hi.hand) + (11 - hi.hand) * 10000;
}
static void bot_round(void) {
  for (int i = 0; i < R.ncons; i++)
    if (R.cons[i].id >= C_MERCURY && R.cons[i].id <= C_ERIS && use_cons(i)) return;
  int n = R.nhand, best = -1, bmask = 0;
  for (int m = 1; m < 1 << n; m++) {
    int c = __builtin_popcount((unsigned)m);
    if (c > 5) continue;
    uint8_t cards[5];
    int k = 0;
    for (int i = 0; i < n; i++)
      if (m >> i & 1) cards[k++] = R.hand[i];
    int v = bot_value(cards, k, 0);
    if (v > best) best = v, bmask = m;
  }
  for (int i = 0; i < n; i++) R.sel[i] = 0;
  if (best < (11 - H_TWO_PAIR) * 10000 && R.discards_left > 0 && R.hands_left > 1) {
    /* a weak hand: away with the lowest cards outside it */
    int k = 0;
    for (int r = 2; r <= 14 && k < 5; r++)
      for (int i = 0; i < n && k < 5; i++)
        if (!(bmask >> i & 1) && R.cards[R.hand[i]].rank == r && !(R.cards[R.hand[i]].flags & CF_FORCED)) R.sel[i] = 1, k++;
    if (k && discard_hand()) return;
    for (int i = 0; i < n; i++) R.sel[i] = 0;
  }
  for (int i = 0; i < n; i++)
    if (bmask >> i & 1) R.sel[i] = 1;
  if (!play_hand()) { /* (The Psychic, a forced card...): the first five */
    for (int i = 0; i < n; i++) R.sel[i] = i < 5;
    play_hand();
  }
}
static void bot_shop(void) {
  for (int i = 0; i < R.nshop; i++) {
    sitem_t *it = &R.shop[i];
    int cost = item_cost(it);
    if (cost > R.money) continue;
    if (it->kind == IT_JOKER && R.njokers < joker_slots() && shop_buy(0, i, 0)) return;
    if (it->kind == IT_CONS && it->id >= C_MERCURY && it->id <= C_ERIS && shop_buy(0, i, 1)) return;
  }
  if (R.nshop_v && R.money >= item_cost(&R.shop_v[0]) + 10 && shop_buy(1, 0, 0)) return;
  if (R.nshop_p && R.money >= item_cost(&R.shop_p[0]) + 8 && shop_buy(2, 0, 0)) return;
  if (R.money >= R.reroll_cost + 20 && R.rerolls % 3 == 0 && shop_reroll()) return;
  shop_leave();
}
static eadk_keyboard_state_t bot(uint64_t now) {
  static uint64_t next;
  static int quit;
  const char *top = getenv("NP_BOT_ANTE");
  if (quit) return (eadk_keyboard_state_t)1 << eadk_key_home;
  if (now < next) return 0;
  extern int ce_main_screen(void);
  if (ce_main_screen() == 0) { /* the title: OK opens the run's panel, OK plays (or continues) */
    next = now + 400;
    return (eadk_keyboard_state_t)1 << eadk_key_ok;
  }
  if (ce_main_screen() != 1) return 0; /* (a panel open) */
  static uint64_t idle_since;
  static int last_round = -1, last_phase = -1;
  if (R.round != last_round || R.phase != last_phase) idle_since = now, last_round = R.round, last_phase = R.phase;
  if (now - idle_since > 60000 && getenv("NP_BOT_DEBUG")) {
    fprintf(stderr, "bot: stuck in phase %d round %d busy %d nhand %d sel %d hands %d events %d..%d (%d)\n", R.phase,
            R.round, ui_busy(), R.nhand, nselected(), R.hands_left, ev_head, ev_tail, ev_head < ev_tail ? evq[ev_head].type : -1);
    idle_since = now;
  }
  if (ui_busy()) return 0;
  next = now + 250;
  if (getenv("NP_BOT_DEBUG") && now < 120000)
    fprintf(stderr, "bot %llu: phase %d round %d money %ld shop %d/%d/%d pack %d\n", (unsigned long long)now, R.phase,
            R.round, (long)R.money, R.nshop, R.nshop_v, R.nshop_p, R.npack);
  if (R.ante > bot_ante_reached) bot_ante_reached = R.ante;
  if (top && R.ante > atoi(top)) quit = 1;
  switch (R.phase) {
    case PH_BLIND_SELECT: blind_select(); break;
    case PH_ROUND: bot_round(); break;
    case PH_CASHOUT: cash_out(); break;
    case PH_SHOP: bot_shop(); break;
    case PH_PACK:
      for (int i = 0; i < R.npack; i++)
        if (pack_choose(i)) return 0;
      pack_skip();
      break;
    case PH_WIN:
      bot_wins++;
      continue_endless();
      break;
    case PH_GAMEOVER:
      bot_runs++;
      quit = 1;
      break;
  }
  return 0;
}

eadk_keyboard_state_t eadk_keyboard_scan(void) {
  uint64_t now = eadk_timing_millis();
  frames++;
  script_shots(now);
  eadk_keyboard_state_t k = script_keys(now);
  if (getenv("NP_BOT")) k |= bot(now);
#ifdef HAVE_SDL
  if (!headless) {
    SDL_Event e;
    while (SDL_PollEvent(&e))
      if (e.type == SDL_EVENT_QUIT) k |= (eadk_keyboard_state_t)1 << eadk_key_home;
    const bool *ks = SDL_GetKeyboardState(0);
    for (size_t i = 0; i < sizeof KEYMAP / sizeof KEYMAP[0]; i++)
      if (ks[KEYMAP[i].sc]) k |= (eadk_keyboard_state_t)1 << KEYMAP[i].key;
    present();
  }
#endif
  return k;
}

/* ---- files */
static const char *save_path(void) { return getenv("NP_SAVE") ? getenv("NP_SAVE") : "bin/host/NPbalato.bin"; }
static uint8_t *store;
static size_t store_size;
uint8_t *ce_storage_open(const char *name, size_t size) {
  (void)name;
  store = calloc(1, size);
  store_size = size;
  FILE *f = fopen(save_path(), "rb");
  if (f) {
    if (fread(store, 1, size, f)) {}
    fclose(f);
  }
  return store;
}
void ce_storage_keep(void) {
  FILE *f = fopen(save_path(), "wb");
  if (!f) return;
  fwrite(store, 1, store_size, f);
  fclose(f);
}
void np_begin(void) {}
int np_end(void) { return 0; }

int main(void) {
  headless = getenv("NP_HEADLESS") && *getenv("NP_HEADLESS") == '1';
#ifdef HAVE_SDL
  if (!headless) {
    SDL_Init(SDL_INIT_VIDEO);
    int sc = getenv("NP_SCALE") ? atoi(getenv("NP_SCALE")) : 3;
    win = SDL_CreateWindow("Balatro (TI-84 Plus CE)", SCR_W * sc, SCR_H * sc, 0);
    ren = SDL_CreateRenderer(win, 0);
    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, SCR_W, SCR_H);
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
  }
#else
  headless = 1;
#endif
  t0 = real_ms();
  if (!ce_art_init() || !ce_bg_init()) {
    fprintf(stderr, "host: an appvar is missing (make first)\n");
    return 1;
  }
  ce_set_palette(0, CE_NSTATIC, ce_palette);
  balatro_main();
  extern int ce_max_items, ce_max_str;
  extern long ce_strips_drawn, ce_adv_made, ce_pal_misses;
  fprintf(stderr, "host: %ld frames in %llu ms (at most %d items, %d bytes of text a frame), %ld strips drawn, "
          "%ld advance tables made, %ld colours looked up\n", frames, (unsigned long long)eadk_timing_millis(),
          ce_max_items, ce_max_str, ce_strips_drawn, ce_adv_made, ce_pal_misses);
  if (getenv("NP_BOT"))
    fprintf(stderr, "bot: ante %d reached, %d wins, round %d, best hand %.0f, money %ld\n", bot_ante_reached, bot_wins,
            R.round, R.best_hand, (long)R.money);
  return 0;
}
