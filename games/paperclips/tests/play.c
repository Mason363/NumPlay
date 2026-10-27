/* Universal Paperclips on a computer, for tests and pictures (make host, make test).
 *
 *   play test DIR        the rules against the web build's numbers (gamesrc spec), JavaScript's number printing,
 *                        a bot playing the whole game to the end through the app's keys with fast time, saves
 *                        kept and damaged, 300 sessions of random keys, and the same game twice to the same hash
 *   play shots DIR       pictures of the screens (DIR/<name>.ppm)
 *   play film DIR        frames for the README's GIF (DIR/<frame>_<ms>.raw, for tools/record.py --frames)
 *   play bot             only the bot, with its progress
 */
#define PC_CATCH_UP 60000   /* fast time: a frame may carry a minute of the game */
#define main pc_app_main
#include "../src/main.c"
#undef main
#include <stdio.h>
#include <stdlib.h>
#include "num.h"

/* ---------------------------------------------------------------- the calculator, on a computer */
static uint16_t fb[240][320];
static void (*on_frame)(void);
void plat_push(int x, int y, int w, int h, const uint16_t *px) {
  for (int j = 0; j < h; j++)
    for (int i = 0; i < w; i++) fb[y + j][x + i] = px[j * w + i];
  if (on_frame && y + h == 240) on_frame();
}
void *np_alloc(uint32_t n) { (void)n; return 0; }
static uint64_t clock_ms;
static uint32_t (*script)(void);
static uint32_t frame_no, quit_at = 0xFFFFFFFFu, step_ms;
uint32_t plat_keys(void) {
  frame_no++;
  clock_ms += step_ms;
  if (frame_no >= quit_at) return PK_HOME;
  return script ? script() : 0;
}
uint32_t plat_millis(void) { return (uint32_t)clock_ms; }
void plat_sleep(uint32_t ms) { clock_ms += ms; }
static uint32_t rnd_state = 1;
static uint32_t lcg(void) { return rnd_state = rnd_state * 1664525u + 1013904223u; }
uint32_t plat_random(void) { return lcg(); }
void plat_begin(void) {}
int plat_end(void) { return 0; }

static struct {
  char name[32];
  uint8_t data[16384];
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
static uint8_t *file_data(const char *name, uint32_t *len) {
  for (int i = 0; i < 4; i++)
    if (files[i].on && !strcmp(files[i].name, name)) return *len = files[i].len, files[i].data;
  return 0;
}
static void forget(const char *name) {
  for (int i = 0; i < 4; i++)
    if (files[i].on && !strcmp(files[i].name, name)) files[i].on = false;
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

static int fails, checks;
#define RUN_HASH 0x00000000u   /* (made on a Mac with clang; Linux with GCC must agree) */
#define CHECK(c, ...)                                         \
  do {                                                        \
    checks++;                                                 \
    if (!(c)) {                                               \
      fails++;                                                \
      printf("FAIL %s:%d: ", __FILE__, __LINE__);             \
      printf(__VA_ARGS__);                                    \
      printf("\n");                                           \
    }                                                         \
  } while (0)
#define NEAR(a, b) ((a) == (b) || ((a) - (b) < 1e-9 * ((b) < 0 ? -(b) : (b)) && (b) - (a) < 1e-9 * ((b) < 0 ? -(b) : (b))))

/* ---------------------------------------------------------------- the game without the app */
static pc_game S;
static pc_fleet SF;
static void fresh_game(uint32_t seed) {
  G = &S, F = &SF;
  memset(&SF, 0, sizeof SF);
  pc_new(&S, seed, 0, 0);
}
static void run(uint32_t ms) { pc_advance(ms); }

int main(int argc, char **argv);

/* ---------------------------------------------------------------- a bot that plays through the keys */
/* what the projects cost in ops and creativity (from the spec's projects.md; Photonic Chip, Threnody and
   Disassemble Memory change as they go) */
static const int32_t OPS_COST[PC_PROJECTS] = {750, 0, 1000, 2500, 5000, 0, 1750, 3500, 7500, 12000, 15000, 2500,
  4500, 0, 0, 0, 0, 6000, 45000, 0, 12000, 10000, 12000, 14000, 17000, 19500, 7000, 7500, 70000, 0, 20000, 25000,
  30000, 50000, 20000, 35000, 0, 0, 500, 25000, 25000, 35000, 0, 0, 120000, 10000, 0, 15000, 17500, 20000, 22500,
  25000, 30000, 32500, 80000, 85000, 0, 80000, 100000, 0, 0, 0, 175000, 0, 0, 0, 40000, 0, 125000, 100000, 150000,
  250000, 0, 200000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 300000, 0, 100000, 100000, 100000, 100000, 100000, 100000, 0, 0,
  0, 0};

static bool wanted(int i) {
  switch (i) {
    case P147: case P200: case P201: case P217: case P218: case P219: return false;
    default: return true;
  }
}
static double ops_need(int i) { return i == P51 ? game.qChipCost : OPS_COST[i]; }

/* the stats the bot wants for its probes, given their trust */
static void probe_plan(double T, double *want) {
  for (int k = 0; k < 8; k++) want[k] = 0;
  int left = (int)T;
  bool combat = game.flag[P131] == 1;
  /* hazards first (a probe lasts a second without them), then replication; then the rest in turns */
  static const int8_t ORDER[] = {3, 3, 3, 3, 2, 2, 2, 0, 1, 4, 5, 6, 2, 3, 2, 1, 0, 7, 7, 7, 7, 7, 7, 2, 3, 1, 0, 2,
                                 1, 0, 3, 2, 1, 0, 4, 5, 6, 2, 3, 1, 0, 2, 1, 0};
  for (unsigned n = 0; left > 0; n++) {
    int k = n < sizeof ORDER ? ORDER[n] : (int)(n % 4 == 0 ? 2 : n % 4 == 1 ? 1 : n % 4 == 2 ? 0 : 3);
    if (k == 7 && !combat) continue;
    want[k]++, left--;
  }
}

static int strat_wanted(void) { return game.nstrats > 3 ? 3 : game.nstrats - 1; }   /* GREEDY, once there */

static uint32_t bot_seen_at[B_COUNT];
static uint32_t gnow(void) { return game.now; }
static bool every(int b, uint32_t ms) { return gnow() - bot_seen_at[b] >= ms || !bot_seen_at[b]; }

/* the button the bot wants to press now, or -1 */
static uint32_t bot_block[B_COUNT];   /* buttons the bot couldn't find, and until when it leaves them */
#define R(b)                                         \
  do {                                               \
    int b_ = (b);                                    \
    if ((int32_t)(gnow() - bot_block[b_]) >= 0) return b_; \
  } while (0)
static int bot_want(void) {
  pc_game *s = &game;
  /* projects: the first that can be bought */
  for (int k = 0; k < s->nactive; k++) {
    int i = s->active[k];
    if (wanted(i) && pc_enabled(B_PROJECT + i)) R(B_PROJECT + i);
  }
  double ops_max = 0;   /* the most ops a project on the list asks for */
  for (int k = 0; k < s->nactive; k++)
    if (wanted(s->active[k]) && ops_need(s->active[k]) > ops_max) ops_max = ops_need(s->active[k]);
  if (s->spaceFlag == 1 && ops_max < 250000) ops_max = 250000;
  else if (s->humanFlag == 0 && ops_max < 130000) ops_max = 130000;
  if (pc_enabled(B_PROC) && every(B_PROC, 300)) {
    bool short_mem = false;   /* a project on the list asks for more ops than memory holds */
    for (int k = 0; k < s->nactive; k++)
      if (wanted(s->active[k]) && ops_need(s->active[k]) > s->memory * 1000) short_mem = true;
    if (s->processors >= 20 && s->spaceFlag == 1 && s->memory < 260) short_mem = true;
    if (s->processors >= 20 && s->humanFlag == 0 && s->memory < 125) short_mem = true;
    bool mem = short_mem ? s->memory < s->processors * 3 + 3 : s->memory < s->processors / 2;
    R(mem ? B_MEM : B_PROC);
  }
  if (s->qFlag == 1 && s->qChipActive[0] && s->dismantle < 5 && every(B_QCOMP, 150)) {
    double q = 0;
    for (int i = 0; i < PC_CHIPS; i++) q += s->qChipValue[i];
    if (q > 0.3) R(B_QCOMP);
  }
  if (s->humanFlag == 1) {
    double reserve = s->wireBuyerFlag ? s->wireCost : s->wireCost * 2;
    double rate = s->clipRate > 1 ? s->clipRate : 1;
    if (s->wire < rate * 3 + 1 && pc_enabled(B_WIRE) && every(B_WIRE, 200)) R(B_WIRE);
    if (s->autoClipperFlag != 0 && s->clipmakerLevel < 10 && pc_enabled(B_CLIPPER)) R(B_CLIPPER);
    if (s->clipmakerLevel < 4 && s->wire >= 1) R(B_CLIP);
    if (every(B_LOWER, 1500)) {
      bot_seen_at[B_LOWER] = gnow();
      if (s->unsoldClips > rate * 6 + 40 && pc_enabled(B_LOWER)) R(B_LOWER);
      if (s->unsoldClips < rate * 1.5 + 5 && s->margin < 5) R(B_RAISE);
    }
    bool slow_sales = s->unsoldClips > rate * 4 + 20, glut = s->unsoldClips > rate * 20 + 1000;
    if (slow_sales && s->funds >= s->adCost + reserve) R(B_ADS);
    if (!glut && s->megaClipperFlag != 0 && s->funds >= s->megaClipperCost + reserve) R(B_MEGA);
    if (!glut && s->autoClipperFlag != 0 && s->funds >= s->clipperCost + reserve && s->clipmakerLevel < 120)
      R(B_CLIPPER);
    if (s->funds >= s->adCost * 1.5 + reserve) R(B_ADS);
    if (s->investmentEngineFlag == 1 && every(B_DEPOSIT, 5000)) {
      double need = 0;
      for (int k = 0; k < s->nactive; k++) {
        int i = s->active[k];
        double d = i == P37 ? 1e6 : i == P38 ? 1e7 : i == P40 ? 5e5 : i == P40B ? s->bribe : 0;
        if (d > need) need = d;
      }
      bot_seen_at[B_DEPOSIT] = gnow();
      if (need > s->funds && s->bankroll + s->funds >= need) R(B_WITHDRAW);
      if (!need && s->bankroll > 50000 && every(B_WITHDRAW, 300000)) R(B_WITHDRAW);
      if (s->funds > 20000 && (s->portTotal < 10000 || (need > s->funds && s->funds > 100000))) R(B_DEPOSIT);
    }
  } else if (s->spaceFlag == 0) {
    /* the Earth: power first, drones even, factories, then batteries for the trip */
    double supply = s->farmLevel * 50 / 100;
    double demand = (s->harvesterLevel + s->wireDroneLevel) / 100 + s->factoryLevel * 2;
    bool done = s->availableMatter == 0 && s->acquiredMatter == 0;
    if (s->swarmFlag == 1 && s->sliderPos < 150) R(B_THINK);
    if (s->swarmStatus == 3 && pc_enabled(B_ENTERTAIN)) R(B_ENTERTAIN);
    if (s->swarmStatus == 5 && pc_enabled(B_SYNCH)) R(B_SYNCH);
    if (done) {
      if (s->harvesterLevel > 0) R(B_HARV_REBOOT);
      if (s->wireDroneLevel > 0) R(B_WIRED_REBOOT);
      if (s->factoryLevel > 0 && s->wire < 1) R(B_FACTORY_REBOOT);
      if (s->batteryLevel * 10000 < 10500000) {
        if (pc_enabled(B_BATT100)) R(B_BATT100);
        if (pc_enabled(B_BATT10)) R(B_BATT10);
        if (pc_enabled(B_BATT1)) R(B_BATT1);
      }
      if (supply < 400 + s->farmLevel * 0) {
        if (pc_enabled(B_FARM100)) R(B_FARM100);
        if (pc_enabled(B_FARM10)) R(B_FARM10);
        if (pc_enabled(B_FARM1)) R(B_FARM1);
      }
      return -1;
    }
    if (s->flag[P127] == 1 && (supply < demand || s->powMod < 1 || s->farmLevel < 1)) {
      if (pc_enabled(B_FARM100)) R(B_FARM100);
      if (pc_enabled(B_FARM10)) R(B_FARM10);
      if (pc_enabled(B_FARM1)) R(B_FARM1);
    }
    if (s->factoryFlag == 1 && pc_enabled(B_FACTORY)) R(B_FACTORY);
    /* the first factory before more drones (taking the drones apart gives their clips back) */
    if (s->factoryFlag == 1 && s->factoryLevel == 0 && s->unusedClips < s->factoryCost &&
        s->unusedClips + s->harvesterBill + s->wireDroneBill >= s->factoryCost)
      R(s->harvesterLevel > 0 ? B_HARV_REBOOT : B_WIRED_REBOOT);
    double keep = s->factoryLevel == 0 ? 1.2e8 : s->factoryFlag == 1 ? s->factoryCost : 0;
    if (s->wire < 1e6) keep = 0;
    if (s->availableMatter > 0) {
      bool harv = s->harvesterLevel <= s->wireDroneLevel;
      int b = harv ? B_HARV1 : B_WIRED1;
      const double price[4] = {harv ? s->harvesterCost : s->wireDroneCost, harv ? s->p10h : s->p10w,
                               harv ? s->p100h : s->p100w, harv ? s->p1000h : s->p1000w};
      if (s->harvesterFlag && s->wireDroneFlag)
        for (int k = 3; k >= 0; k--)
          if (pc_enabled(b + k) && s->unusedClips - price[k] >= keep) R(b + k);
    }
    if (s->factoryFlag == 1 && pc_enabled(B_FACTORY) && (s->flag[P127] == 0 || supply >= demand + 2)) R(B_FACTORY);
  } else {
    /* the universe: probes, their trust and design */
    if (s->probeCount < 1 && pc_enabled(B_PROBE)) R(B_PROBE);
    if (s->probeLaunchLevel < 20 && pc_enabled(B_PROBE)) R(B_PROBE);
    if (pc_enabled(B_MAX_TRUST)) R(B_MAX_TRUST);
    if (pc_enabled(B_PROBE_TRUST) && s->dismantle < 1) R(B_PROBE_TRUST);
    if (s->dismantle < 1) {
      double want[8];
      probe_plan(s->probeTrust, want);
      const double cur[8] = {s->probeSpeed, s->probeNav, s->probeRep, s->probeHaz, s->probeFac, s->probeHarv,
                             s->probeWire, s->probeCombat};
      for (int k = 0; k < 8; k++)
        if (cur[k] > want[k]) R(B_STAT_DOWN + k);
      for (int k = 0; k < 8; k++)
        if (cur[k] < want[k] && pc_enabled(B_STAT_UP + k)) R(B_STAT_UP + k);
    }
    if (s->flag[P130] == 1 && s->swarmFlag == 1 && s->sliderPos < 150 && s->dismantle < 2) R(B_THINK);
    if (s->swarmStatus == 3 && pc_enabled(B_ENTERTAIN)) R(B_ENTERTAIN);
    if (s->swarmStatus == 5 && pc_enabled(B_SYNCH)) R(B_SYNCH);
    if (s->flag[P216] == 1 && s->wire >= 1) R(B_CLIP);   /* the last clips */
  }
  /* strategy */
  if (s->strategyEngineFlag == 1 && s->dismantle < 4) {
    if (s->pickChoice != strat_wanted()) R(B_PICK);
    if (s->runEnabled) R(B_RUN);
    double yomi_need = s->spaceFlag == 1 ? 1e12 : s->humanFlag == 0 ? 90000 : 25000;
    bool full = (s->operations >= s->memory * 1000 * 0.9 || s->operations >= ops_max + s->tourneyCost) &&
                s->yomi < yomi_need;
    if (!(s->autoTourneyFlag == 1 && s->autoTourneyStatus == 1 && s->resultsShown) && full && pc_enabled(B_NEW_TOURNEY))
      R(B_NEW_TOURNEY);
  }
  return -1;
}

static int shown_tabs(int *t) {
  int n = 0;
  for (int k = 0; k < T_COUNT; k++)
    if (ui_tab_shown(k)) t[n++] = k;
  return n;
}

static int place_of(const int *list, int n, int b) {
  for (int i = 0; i < n; i++)
    if (list[i] == b) return i;
  return -1;
}

/* the key that takes the app one step nearer to pressing b */
static uint32_t key_toward(int b) {
  int tabs[T_COUNT], nt = shown_tabs(tabs), list[200];
  int where = -1;
  for (int k = 0; k < nt && where < 0; k++) {
    pc_ui tmp = ui;
    tmp.tab = tabs[k];
    int n = ui_buttons(&tmp, list, 200);
    if (place_of(list, n, b) >= 0) where = tabs[k];
  }
  if (where < 0) return 0;
  if (ui.tab != where) {
    int a = place_of(tabs, nt, ui.tab), z = place_of(tabs, nt, where);
    if (a < 0) return PK_RIGHT;
    int right = (z - a + nt) % nt;
    return right <= nt - right ? PK_RIGHT : PK_LEFT;
  }
  int n = ui_buttons(&ui, list, 200), at = place_of(list, n, ui.focus[ui.tab]), to = place_of(list, n, b);
  if (to < 0) return 0;
  if (at < to) return PK_DOWN;
  if (at > to) return PK_UP;
  return PK_OK;
}

static int bot_target = -1, bot_tries;
static uint32_t bot_last, bot_idle_ms = 1000, bot_presses;
static uint32_t bot_keys(void) {
  step_ms = 0;
  if (bot_last) return bot_last = 0;
  if (ui.overlay == O_GUIDE) return bot_last = PK_OK;
  if (ui.overlay == O_RESTART) return bot_last = PK_BACK;
  if (ui.overlay != O_NONE) return bot_last = PK_BACK;
  if (bot_target < 0 || bot_tries > 40 || !pc_enabled(bot_target)) {
    bot_target = bot_want(), bot_tries = 0;
    if (bot_target < 0) {
      step_ms = bot_idle_ms;
      return 0;
    }
  }
  uint32_t k = key_toward(bot_target);
  bot_tries++;
  if (k == PK_OK) {
    bot_seen_at[bot_target] = gnow() ? gnow() : 1;
    bot_presses++;
    bot_target = -1;
  }
  if (!k) bot_block[bot_target] = gnow() + 10000, bot_target = -1;
  return bot_last = k;
}

/* the game played by the bot from nothing to the end (the credits), through the app; false if it got stuck */
static const char *const STAGE[4] = {"business", "the Earth", "the universe", "the end"};
static int stage_of(const pc_game *s) {
  return s->milestoneFlag >= 15 ? 3 : s->spaceFlag == 1 ? 2 : s->humanFlag == 0 ? 1 : 0;
}
static bool bot_done;
static double bot_limit_s = 6 * 3600;
static bool bot_verbose;
static uint32_t bot_script(void) {
  static int stage = -1;
  static double next_report;
  int st = stage_of(&game);
  if (st != stage || (bot_verbose && game.ticks / 100 >= next_report)) {
    if (st != stage && bot_verbose) printf("  %s after %.0f s of play\n", STAGE[st], game.ticks / 100);
    if (bot_verbose && st == stage)
      printf("    %6.0f s: clips %.3g, funds %.3g, trust %.0f, ops %.0f/%.0f, creat %.0f, yomi %.0f, probes %.3g, "
             "drifters %.3g, found %.3g, projects %d\n", game.ticks / 100, game.clips, game.funds, game.trust,
             game.operations, game.memory * 1000, game.creativity, game.yomi, game.probeCount, game.drifterCount,
             game.foundMatter, game.nactive);
    if (bot_verbose && st == stage && st >= 1)
      printf("           unused %.3g fac %.0f harv %.3g wd %.3g farm %.0f batt %.0f stored %.3g matter %.3g/%.3g wire %.3g "
             "powmod %.2f swarm %.0f gifts %.0f proc %.0f mem %.0f toth %.0f 127 %d 41 %d 45 %d target %d\n",
             game.unusedClips, game.factoryLevel, game.harvesterLevel, game.wireDroneLevel, game.farmLevel,
             game.batteryLevel, game.storedPower, game.availableMatter, game.acquiredMatter, game.wire, game.powMod,
             game.swarmStatus, game.swarmGifts, game.processors, game.memory, game.tothFlag, game.flag[P127],
             game.flag[P41], game.flag[P45], bot_target);
    if (bot_verbose && st == stage && st == 0)
      printf("           auto %.0f mega %.0f mkt %.0f margin %.2f demand %.1f unsold %.0f bank %.3g wire %.0f/%.0f "
             "cost %.0f proc %.0f mem %.0f rate %.0f\n", game.clipmakerLevel, game.megaClipperLevel,
             game.marketingLvl, game.margin, game.demand, game.unsoldClips, game.bankroll, game.wire,
             game.wireSupply, game.wireCost, game.processors, game.memory, game.clipRate);
    stage = st;
    next_report = game.ticks / 100 + 600;
  }
  static double dbg_at;
  if (getenv("PCDBG") && game.ticks / 100 >= dbg_at) {
    dbg_at = game.ticks / 100 + atof(getenv("PCDBG"));
    printf("dbg %.0f s: clips %.4g unused %.4g fac %.0f/%.3g bill %.3g farm %.0f batt %.0f harv %.0f wd %.0f wire %.3g "
           "target %d tab %d\n", game.ticks / 100, game.clips, game.unusedClips, game.factoryLevel, game.factoryCost,
           game.factoryBill, game.farmLevel, game.batteryLevel, game.harvesterLevel, game.wireDroneLevel, game.wire,
           bot_target, ui.tab);
  }
  if (game.milestoneFlag >= 20 && game.finalClips >= 100) {
    bot_done = true;
    return PK_HOME;
  }
  if (game.ticks / 100 > bot_limit_s) return PK_HOME;
  return bot_keys();
}

static bool bot_game(uint32_t seed, uint32_t *hash) {
  memset(files, 0, sizeof files);
  memset(bot_seen_at, 0, sizeof bot_seen_at);
  memset(bot_block, 0, sizeof bot_block);
  rnd_state = seed, clock_ms = 0, frame_no = 0, quit_at = 0xFFFFFFFFu;
  bot_done = false, bot_target = -1, bot_last = 0, bot_presses = 0;
  script = bot_script;
  pc_app_main();
  script = 0;
  if (hash) *hash = pc_sum(&game, PC_KEPT);
  return bot_done;
}

int main(int argc, char **argv) {
  if (argc >= 2 && !strcmp(argv[1], "bot")) {
    bot_verbose = true;
    bool ok = bot_game(argc >= 3 ? (uint32_t)atoi(argv[2]) : 1, 0);
    printf("bot %s: %.0f s of play, %u presses, %u frames, milestone %.0f\n", ok ? "finished" : "stuck",
           game.ticks / 100, bot_presses, frame_no, game.milestoneFlag);
    for (int i = PC_MSG - 1; i >= 0; i--) printf("  > %s\n", game.msg[i]);
    return ok ? 0 : 1;
  }
  printf("usage: play test|shots|film|bot DIR\n");
  return 2;
}
