/* Plays Cookie Clicker on a computer, and saves frames.
 *
 *   play [--frames N] [--keys "10-40:ok,50:exe,..."] [--shots 30,60|every:N] [--out DIR] [--saves DIR]
 *        [--preset NAME] [--seed S] [--raw DIR --raw-from T --raw-to T] [--quiet] [--bot]
 *
 * Keys: left right up down ok exe back shift home alpha mul div toolbox var del xnt 0-9. "a-b:key" holds the
 * key from frame a to b, "a:key" for one frame (frames are thirtieths of a second; one is drawn every frame).
 * Presets set a bakery up before the keys play: start (a new game), early, mid, late, ascend (on the heavens),
 * grimoire and pantheon (the minigames open), golden (a golden cookie on screen), wrath, wrinklers.
 * --bot plays by itself: it clicks, buys the cheapest of what it can afford, catches golden cookies, and so on;
 * it prints what the bakery got to. Screenshots are DIR/shot_<frame>.ppm; --raw saves the frames drawn as RGB565
 * (DIR/frame_<ms>.raw, for NumPlay's tools/record.py --frames). The end prints the game's state. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "../src/game.h"
#include "../src/gfx.h"
#include "../src/ui.h"

extern uint32_t host_keys, host_time, host_seed;
extern uint16_t host_fb[SCREEN_W * SCREEN_H];
void host_shot(const char *path);
void host_save_dir(const char *d);
extern bool g_quit;
extern bool game_save(void);
void cc_begin(void);
void cc_frame(uint32_t pressed, uint32_t held);
extern void game_recount(void);

typedef struct { int a, b; uint32_t k; } Hold;
static Hold holds[8192];
static int nholds;

static uint32_t key_named(const char *s) {
  static const struct { const char *n; uint32_t k; } t[] = {
    {"left", K_LEFT}, {"right", K_RIGHT}, {"up", K_UP}, {"down", K_DOWN}, {"ok", K_OK}, {"exe", K_EXE},
    {"back", K_BACK}, {"shift", K_SHIFT}, {"home", K_HOME}, {"alpha", K_ALPHA}, {"mul", K_MUL}, {"div", K_DIV},
    {"toolbox", K_TOOLBOX}, {"var", K_VAR}, {"del", K_DEL}, {"xnt", K_XNT},
  };
  for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++)
    if (!strcmp(s, t[i].n)) return t[i].k;
  if (s[0] >= '0' && s[0] <= '9' && !s[1]) return K_DIGIT(s[0] == '0' ? 10 : s[0] - '0');
  fprintf(stderr, "unknown key %s\n", s);
  exit(2);
}

static void parse_keys(const char *spec) {
  static char buf[1 << 17];
  snprintf(buf, sizeof buf, "%s", spec);
  for (char *tok = strtok(buf, ","); tok; tok = strtok(NULL, ",")) {
    char *colon = strchr(tok, ':');
    if (!colon) continue;
    *colon = 0;
    int a, b;
    if (sscanf(tok, "%d-%d", &a, &b) != 2) b = a = atoi(tok);
    holds[nholds].a = a, holds[nholds].b = b, holds[nholds].k = key_named(colon + 1);
    if (nholds < 8191) nholds++;
  }
}

static int shots[1024], nshots_at, every;
static const char *raw_dir;
static int raw_from, raw_to = -1;
static void parse_shots(const char *s) {
  if (!strncmp(s, "every:", 6)) {
    every = atoi(s + 6);
    return;
  }
  char buf[8192];
  snprintf(buf, sizeof buf, "%s", s);
  for (char *t = strtok(buf, ","); t && nshots_at < 1024; t = strtok(NULL, ",")) shots[nshots_at++] = atoi(t);
}
static bool shot_now(int t) {
  if (every && t % every == 0) return true;
  for (int i = 0; i < nshots_at; i++)
    if (shots[i] == t) return true;
  return false;
}

/* ------------------------------------------------------------------ presets */
static void own(int b, int n) {
  for (int i = 0; i < n; i++) {
    S.buildings[b].amount++, S.buildings[b].bought++;
    if (S.buildings[b].amount > S.buildings[b].highest) S.buildings[b].highest = S.buildings[b].amount;
    extern void building_bought(int b);
    building_bought(b);
  }
}
/* every upgrade in the store whose price is under max, bought */
static void buy_upgrades(double max) {
  for (int pass = 0; pass < 6; pass++) {
    game_rebuild_upgrades();
    calculate_gains();
    for (int i = 0; i < G.nstore; i++) {
      int u = G.store[i];
      if (upgrade_price(u) <= max && !upgrade_needs_confirm(u)) {
        S.cookies += upgrade_price(u) + 1;
        upgrade_buy(u, true);
      }
    }
  }
}
static void preset(const char *name) {
  if (!strcmp(name, "start")) return;
  S.seen_keys = 1;
  U.overlay = OV_NONE;
  if (!strcmp(name, "early")) {
    S.cookies = 1400, S.cookies_earned = 3200, S.cookie_clicks = 420, S.handmade_cookies = 640;
    own(B_CURSOR, 12), own(B_GRANDMA, 6), own(B_FARM, 2);
    buy_upgrades(2000);
    S.cookies = 1400;
  } else if (!strcmp(name, "mid") || !strcmp(name, "golden") || !strcmp(name, "wrath") || !strcmp(name, "grimoire") ||
             !strcmp(name, "wrinklers") || !strcmp(name, "pantheon") || !strcmp(name, "show")) {
    S.cookies_earned = 6.2e9, S.cookie_clicks = 5400, S.handmade_cookies = 2.1e7;
    static const int n[] = {80, 70, 50, 40, 35, 25, 20, 15, 8, 3};
    for (int b = 0; b < 10; b++) own(b, n[b]);
    buy_upgrades(4e9);
    S.cookies = 3.4e8;
    S.lumps = 7, S.lumps_total = 9, S.lump_t = S.play_time - 1000;
    for (int a = 0; a < 60; a++) win(a);
    game_recount();
    if (!strcmp(name, "golden")) {   /* (as one comes by itself) */
      Shimmer *m = shimmer_new(SHIMMER_GOLDEN, GC_NONE, false);
      if (m) m->spawn_lead = 1, S.golden_spawned = 1;
    }
    if (!strcmp(name, "wrath")) {
      Shimmer *m = shimmer_new(SHIMMER_GOLDEN, GC_NONE, false);
      if (m) m->wrath = 1, m->spawn_lead = 1;
      S.elder_wrath = 3;
    }
    if (!strcmp(name, "wrinklers")) {
      S.elder_wrath = 2;
      bit_set(S.upgrades_bought, U_ONE_MIND, true), bit_set(S.upgrades_bought, U_COMMUNAL_BRAINSWEEP, true);
      for (int i = 0; i < 6; i++) S.wrinklers[i].phase = 2, S.wrinklers[i].close = 1, S.wrinklers[i].hp = 2.1f;
    }
    if (!strcmp(name, "show")) {   /* (for the pictures: a golden cookie soon, the Grimoire ready) */
      S.buildings[B_WIZARD_TOWER].level = 1;
      extern void grimoire_reset(bool hard);
      grimoire_reset(true);
      S.golden_time = S.golden_max;
    }
    if (!strcmp(name, "pantheon")) {
      S.buildings[B_TEMPLE].level = 1;
      S.gods[0] = GOD_INDUSTRY + 1, S.gods[2] = GOD_AGES + 1, S.swaps = 2;
      S.on_minigame = B_TEMPLE + 1;
      U.view = VIEW_ROWS;
    }
    if (!strcmp(name, "grimoire")) {
      S.buildings[B_WIZARD_TOWER].level = 1;
      extern void grimoire_reset(bool hard);
      grimoire_reset(true);
      S.on_minigame = B_WIZARD_TOWER + 1;
      U.view = VIEW_ROWS;
    }
  } else if (!strcmp(name, "late") || !strcmp(name, "ascend")) {
    S.cookies_earned = 4.4e21, S.cookie_clicks = 21000, S.handmade_cookies = 5e17;
    S.cookies_reset = 2e19, S.resets = 3, S.prestige = floor(pow(S.cookies_reset / 1e12, 1.0 / 3));
    S.heavenly_chips = 40;
    for (int b = 0; b < NB; b++) own(b, b < 14 ? 300 - b * 15 : 40 - (b - 14) * 5);
    for (int i = 0; i < N_HEAVENLY; i++) {
      int u = LIST(HEAVENLY)[i];
      if (upgrade_rec(u)->price < 1e7) bit_set(S.upgrades_bought, u, true), bit_set(S.upgrades_unlocked, u, true);
    }
    buy_upgrades(1e21);
    S.cookies = 7.7e20;
    bit_set(S.upgrades_bought, U_A_FESTIVE_HAT, true), bit_set(S.upgrades_bought, U_A_CRUMBLY_EGG, true);
    bit_set(S.upgrades_unlocked, U_A_FESTIVE_HAT, true), bit_set(S.upgrades_unlocked, U_A_CRUMBLY_EGG, true);
    S.santa_level = 6, S.dragon_level = 12, S.dragon_aura = AURA_DRAGON_CURSOR;
    S.buildings[B_WIZARD_TOWER].level = 3, S.buildings[B_GRANDMA].level = 5;
    S.lumps = 41, S.lumps_total = 60;
    for (int a = 0; a < 300; a++) win(a);
    game_recount();
    if (!strcmp(name, "ascend")) {
      G.on_ascend = true;
      extern void earn_heavenly_chips(double forfeited, bool silent);
      S.heavenly_chips = 2.4e6;
    }
  } else {
    fprintf(stderr, "unknown preset %s\n", name);
    exit(2);
  }
  for (int i = 0; i < MAX_NOTES; i++) G.notes[i].title[0] = 0;
  G.recalculate = true, G.upgrades_dirty = true, G.store_dirty = true;
  calculate_gains();
  game_rebuild_upgrades();
  G.cookies_d = S.cookies;
  ui_news_next();
  U.news_fade = 0;
}

/* ------------------------------------------------------------------ the bot */
static uint32_t bot_keys(int t) {
  /* clicks every other frame; catches shimmers; buys what the store has chosen when it can, else looks for the
   * cheapest it can afford */
  uint32_t k = 0;
  if (t % 2 == 0) k |= K_OK;
  for (int i = 0; i < MAX_SHIMMERS; i++)
    if (G.shimmers[i].used && t % 7 == 0) k |= K_SHIFT;
  if (t % 30 == 15) {
    int best = -1;
    double bp = 1e300;
    for (int i = 0; i < G.nstore; i++) {
      int u = G.store[i];
      if (upgrade_can_buy(u) && !upgrade_needs_confirm(u) && upgrade_price(u) < bp) bp = upgrade_price(u), best = u;
    }
    if (best >= 0) upgrade_buy(best, false);
    int bb = -1;
    double bbp = 1e300;
    for (int b = 0; b < NB; b++) {
      double p = building_price(b);
      if (p <= S.cookies && p < bbp * 0.6) bbp = p, bb = b;
    }
    if (bb >= 0) building_buy(bb, 1);
  }
  return k;
}

int main(int argc, char **argv) {
  int frames = 300;
  const char *out = "build/shots", *saves = "build/host-saves", *pre = NULL;
  bool quiet = false, bot = false;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--keys") && i + 1 < argc) parse_keys(argv[++i]);
    else if (!strcmp(argv[i], "--shots") && i + 1 < argc) parse_shots(argv[++i]);
    else if (!strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
    else if (!strcmp(argv[i], "--saves") && i + 1 < argc) saves = argv[++i];
    else if (!strcmp(argv[i], "--preset") && i + 1 < argc) pre = argv[++i];
    else if (!strcmp(argv[i], "--seed") && i + 1 < argc) host_seed = (uint32_t)atol(argv[++i]);
    else if (!strcmp(argv[i], "--raw") && i + 1 < argc) raw_dir = argv[++i];
    else if (!strcmp(argv[i], "--raw-from") && i + 1 < argc) raw_from = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--raw-to") && i + 1 < argc) raw_to = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--quiet")) quiet = true;
    else if (!strcmp(argv[i], "--bot")) bot = true;
    else {
      fprintf(stderr, "unknown option %s\n", argv[i]);
      return 2;
    }
  }
  mkdir(out, 0755);
  mkdir(saves, 0755);
  host_save_dir(saves);
  static uint16_t band[SCREEN_W * GFX_BAND];
  gfx_set_band_memory(band);
  cc_begin();
  if (pre) preset(pre);
  uint32_t prev = 0;
  int t;
  for (t = 0; t < frames && !g_quit; t++) {
    host_keys = 0;
    for (int i = 0; i < nholds; i++)
      if (t >= holds[i].a && t <= holds[i].b) host_keys |= holds[i].k;
    if (bot) host_keys |= bot_keys(t);
    if (host_keys) host_keys |= K_ANY;
    if (host_keys & K_HOME) g_quit = true;
    if (g_quit) break;
    cc_frame(host_keys & ~prev, host_keys);
    prev = host_keys;
    host_time += 33;
    bool raw = raw_dir && t >= raw_from && t <= raw_to;
    if (shot_now(t) || raw || t % 7 == 0) ui_frame();   /* (drawn now and then anyway: the drawing is tested too) */
    if (shot_now(t)) {
      char p[512];
      snprintf(p, sizeof p, "%s/shot_%05d.ppm", out, t);
      host_shot(p);
      if (!quiet) printf("shot %s\n", p);
    }
    if (raw) {
      char p[512];
      snprintf(p, sizeof p, "%s/frame_%06d.raw", raw_dir, (int)((t - raw_from) * 1000 / 30));
      FILE *f = fopen(p, "wb");
      if (f) fwrite(host_fb, 2, SCREEN_W * SCREEN_H, f), fclose(f);
    }
  }
  if (g_quit) game_save();
  int owned = 0;
  for (int b = 0; b < NB; b++) owned += S.buildings[b].amount;
  printf("frames %d cookies %.0f earned %.0f cps %.1f clicks %.0f buildings %d upgrades %d achievements %d "
         "prestige %.0f chips %.0f golden %u ascending %d running %d\n",
         t, S.cookies, S.cookies_earned, G.cookies_ps, S.cookie_clicks, owned, G.upgrades_owned, G.achievements_owned,
         S.prestige, S.heavenly_chips, S.golden_clicks, G.on_ascend ? 1 : G.ascend_timer > 0 ? 2 : 0, !g_quit);
  return 0;
}
