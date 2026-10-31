/* Universal Paperclips on a computer, for tests and pictures (make host, make test).
 *
 *   play test DIR        the rules against the web build's numbers (gamesrc spec), JavaScript's number printing,
 *                        a bot playing the whole game to the end through the app's keys with fast time, saves
 *                        kept and damaged, 300 sessions of random keys, and the same game twice to the same hash
 *   play docs PICS FILM  the README's pictures (PICS/<name>.ppm) and the frames of its GIF (FILM/<frame>_<ms>.raw,
 *                        for tools/record.py --frames), from the bot's game
 *   play bot [SEED]      only the bot, with its progress
 *   play states DIR      saves from the bot's game, for the emulator (DIR/<stage>.sav)
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
#define RUN_HASH 0x87B339A5u   /* (made on a Mac with clang; Linux with GCC must agree) */
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
static const char *states_dir;   /* play states: saves taken from the bot's game, for the emulator */
static void dump_state(const char *name) {
  if (!states_dir) return;
  static uint8_t buf[PC_FILE_SIZE];
  save_pack(&game, 1, (uint8_t)ui.tab, buf);
  char path[512];
  snprintf(path, sizeof path, "%s/%s.sav", states_dir, name);
  FILE *f = fopen(path, "wb");
  if (f) fwrite(buf, 1, sizeof buf, f), fclose(f);
}
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
  static int dumped;
  if (states_dir) {
    if (dumped == 0 && game.strategyEngineFlag == 1 && game.investmentEngineFlag == 1) dump_state("business"), dumped++;
    if (dumped == 1 && game.humanFlag == 0 && game.factoryLevel >= 5) dump_state("earth"), dumped++;
    if (dumped == 2 && game.spaceFlag == 1 && game.battles > 0 && game.flag[P121]) dump_state("battle"), dumped++;
    if (dumped == 3 && game.flag[P148] == 1) dump_state("reject"), dumped++;
  }
  if (game.milestoneFlag >= 20 && game.finalClips >= 100) {
    if (states_dir) dump_state("end");
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

/* ---------------------------------------------------------------- the rules, against the spec */
static void press(int b) { CHECK(pc_press(b), "button %d pressed", b); }

static void rules_business(void) {
  fresh_game(1);
  CHECK(S.wire == 1000 && S.wireCost == 20 && S.margin == .25 && S.clipperCost == 5 && S.trust == 2 &&
        S.nextTrust == 3000 && S.processors == 1 && S.memory == 1 && S.funds == 0 && S.adCost == 100,
        "the start (globals.js)");
  CHECK(S.availableMatter == 6e27 && S.totalMatter == 3e55 && S.probeCost == 1e17 && S.probeTrustCost == 500 &&
        S.maxTrust == 20 && S.tourneyCost == 1000, "matter, probes and tournaments at the start");
  run(10);
  CHECK(NEAR(S.demand, 3.2), "demand = .8 / .25: %g", S.demand);
  press(B_CLIP);
  CHECK(S.clips == 1 && S.wire == 999 && S.unsoldClips == 1 && S.unusedClips == 1, "Make Paperclip: a clip from an inch");
  press(B_RAISE);
  CHECK(S.margin == .26, "raise: .26 (%g)", S.margin);
  press(B_LOWER);
  CHECK(S.margin == .25, "lower: .25");
  for (int i = 0; i < 30; i++) pc_press(B_LOWER);
  CHECK(S.margin == .01 && !pc_enabled(B_LOWER), "the price stops at $.01");
  S.margin = .25;
  CHECK(!pc_enabled(B_CLIPPER), "AutoClippers need $5");
  S.funds = 1000;
  press(B_CLIPPER);
  CHECK(S.clipmakerLevel == 1 && S.funds == 995 && NEAR(S.clipperCost, 1.1 + 5), "an AutoClipper, then 1.1^1 + 5");
  press(B_CLIPPER);
  CHECK(NEAR(S.clipperCost, 1.21 + 5), "1.1^2 + 5 = %g", S.clipperCost);
  double c0 = S.clips;
  S.wire = 1000000;
  run(1000);
  CHECK(NEAR(S.clips - c0, 2), "2 AutoClippers: 2 clips a second (%g)", S.clips - c0);
  S.megaClipperFlag = 1, S.funds = 10000;
  press(B_MEGA);
  CHECK(S.megaClipperLevel == 1 && NEAR(S.megaClipperCost, 1070), "a MegaClipper, then 1.07^1 * 1000");
  c0 = S.clips;
  run(1000);
  CHECK(NEAR(S.clips - c0, 2 + 500), "a MegaClipper: 500 a second (%g)", S.clips - c0);
  S.funds = 1000;
  press(B_ADS);
  CHECK(S.marketingLvl == 2 && S.adCost == 200 && S.funds == 900, "Marketing: level 2, then $200");
  run(10);
  CHECK(NEAR(S.demand, .8 / .25 * 1.1), "demand x 1.1 a level");
  S.funds = 100, S.wireCost = 20;
  double w = S.wire;
  press(B_WIRE);
  CHECK(S.wire == w + 1000 && S.funds == 80 && S.wirePurchase == 1 && NEAR(S.wireBasePrice, 20.05), "a spool of wire");
  /* sales, over a minute: P(sale) = demand / 100 every 100 ms, floor(.7 demand^1.15) each */
  fresh_game(2);
  S.unsoldClips = 1e9, S.wire = 0;
  run(10);
  double expect = 600 * (S.demand / 100) * js_floor(.7 * js_pow(S.demand, 1.15));
  run(60000);
  CHECK(S.clipsSold > expect * .75 && S.clipsSold < expect * 1.25, "sales in a minute: %g, about %g", S.clipsSold, expect);
  CHECK(NEAR(S.funds, js_floor(S.clipsSold * .25 * 100 + .5) / 100) || (S.funds > S.clipsSold * .249 && S.funds < S.clipsSold * .251),
        "the money is a quarter a clip");
  /* trust: 3000, 5000, 8000, 13000 clips (Fibonacci x 1000) */
  fresh_game(3);
  S.clips = 2999, run(10);
  CHECK(S.trust == 2, "no trust before 3000 clips");
  S.clips = 3000, run(10);
  CHECK(S.trust == 3 && S.nextTrust == 5000, "trust at 3000, next at 5000");
  S.clips = 5000, run(10);
  S.clips = 8000, run(10);
  CHECK(S.trust == 5 && S.nextTrust == 13000, "then 8000 and 13000");
  CHECK(strstr(S.msg[0], "TRUST INCREASED") != 0, "its message");
  /* operations: processors / 10 a tick, up to memory * 1000 */
  fresh_game(4);
  S.compFlag = 1;
  run(50000);
  CHECK(S.operations >= 499 && S.operations <= 501, "a processor: 10 ops a second (%g)", S.operations);
  run(60000);
  CHECK(S.operations == 1000, "memory 1: 1000 at most");
  S.creativityOn = 1;
  run(40000);
  CHECK(S.creativity == 10, "creativity: 1 every 4 s with full ops (%g)", S.creativity);
  S.trust = 10;
  press(B_PROC);
  CHECK(S.processors == 2 && NEAR(S.creativitySpeed, js_log10(2) * js_pow(2, 1.1) + 1), "a processor: creativity speed");
  press(B_MEM);
  CHECK(S.memory == 2, "memory");
  S.trust = 4;
  CHECK(!pc_enabled(B_PROC) && !pc_enabled(B_MEM), "no trust left: no more");
  /* milestones */
  fresh_game(5);
  S.funds = 5, run(10);
  CHECK(S.milestoneFlag == 1 && S.autoClipperFlag == 1, "$5: AutoClippers");
  S.clips = 2000, run(10);
  CHECK(S.milestoneFlag == 3 && S.compFlag == 1 && S.projectsFlag == 1, "2,000 clips: projects (%g)", S.milestoneFlag);
  CHECK(!strcmp(S.msg[0], "Trust-Constrained Self-Modification enabled"), "its message: %s", S.msg[0]);
}

static void rules_projects(void) {
  fresh_game(6);
  S.compFlag = 1, S.projectsFlag = 1;
  run(10);
  CHECK(S.nactive == 1 && S.active[0] == P42, "RevTracker first");
  S.clipmakerLevel = 1, S.standardOps = 750;
  run(10);
  CHECK(S.nactive == 2 && S.active[1] == P1 && S.uses[P1] == 0, "Improved AutoClippers when there's an AutoClipper");
  CHECK(pc_enabled(B_PROJECT + P1), "750 ops: it can be bought");
  press(B_PROJECT + P1);
  CHECK(S.clipperBoost == 1.25 && S.flag[P1] == 1 && S.nactive == 1 && S.standardOps < 10, "+25%%, ops spent");
  run(10);
  CHECK(S.uses[P4] == 0 && S.active[S.nactive - 1] == P4, "then Even Better AutoClippers");
  S.wirePurchase = 1, S.standardOps = 1750, run(10);
  press(B_PROJECT + P7);
  CHECK(S.wireSupply == 1500 && !strcmp(S.msg[0], "Wire extrusion technique improved, 1,500 supply from every spool"),
        "Improved Wire Extrusion: %s", S.msg[0]);
  /* Beg for More Wire: when nothing's left, and again */
  fresh_game(7);
  S.wire = 0;
  run(10);
  bool beg = false;
  for (int k = 0; k < S.nactive; k++) beg |= S.active[k] == P2;
  CHECK(beg, "Beg for More Wire when out of everything");
  press(B_PROJECT + P2);
  CHECK(S.trust == 1 && S.wire == 1000 && S.uses[P2] == 1, "a spool for a trust, and it can come back");
  /* the creativity projects and trust */
  fresh_game(8);
  S.creativityOn = 1, S.creativity = 250, S.compFlag = 1, S.projectsFlag = 1;
  run(10);
  int got = 0;
  for (int k = 0; k < S.nactive; k++) got += S.active[k] == P13 || S.active[k] == P14 || S.active[k] == P15 ||
                                          S.active[k] == P17 || S.active[k] == P19 || S.active[k] == P6;
  CHECK(got == 6, "Limerick, Lexical Processing, Combinatory Harmonics, Hadwiger, Toth, Donkey Space (%d)", got);
  press(B_PROJECT + P19);
  CHECK(S.trust == 3 && S.creativity == 0, "Donkey Space: +1 trust for 250 creat");
  run(10);
  bool sm = false;
  for (int k = 0; k < S.nactive; k++) sm |= S.active[k] == P20;
  CHECK(sm, "Strategic Modeling after Donkey Space");
  char t[64];
  pc_project_price(P51, t, sizeof t);
  CHECK(!strcmp(t, "(10,000 ops)"), "Photonic Chip's price: %s", t);
  S.flag[P50] = 1, S.qFlag = 1, S.memory = 20, S.standardOps = 10000, run(10);
  press(B_PROJECT + P51);
  pc_project_price(P51, t, sizeof t);
  CHECK(S.qChipActive[0] == 1 && S.nextQchip == 1 && S.qChipCost == 15000 && !strcmp(t, "(15000 ops)"),
        "a chip, then 15000 (no commas after, as the page): %s", t);
  /* the hypnodrones */
  fresh_game(9);
  S.flag[P70] = 1, S.trust = 100, S.clipmakerLevel = 50;
  run(10);
  S.wire = 123;
  press(B_PROJECT + P35);
  CHECK(S.humanFlag == 0 && S.trust == 0 && S.clipmakerLevel == 0 && S.nanoWire == 123 && S.hypnoOn, "Release the HypnoDrones");
  run(4000);
  CHECK(!S.hypnoOn && S.hypnoText == 3, "the HypnoDrone screen: 120 blinks of 32 ms");
}

static void rules_quantum_tourney(void) {
  fresh_game(10);
  S.qFlag = 1, S.qChipActive[0] = 1, S.qChipActive[3] = 1, S.compFlag = 1, S.memory = 50;
  run(1000);
  double q = js_sin(S.qClock * .1) + js_sin(S.qClock * .4);
  CHECK(NEAR(S.qClock, 1.0) || (S.qClock > .99 && S.qClock < 1.01), "the quantum clock: .01 a tick (%g)", S.qClock);
  double ops = S.standardOps;
  press(B_QCOMP);
  CHECK(S.standardOps == ops + js_ceil(q * 360) && S.qShown == 2, "Compute: ceil(360 x the chips) ops");
  S.qChipActive[0] = 0;
  press(B_QCOMP);
  CHECK(S.qShown == 1, "no chip: Need Photonic Chips");
  /* a tournament: RANDOM, A100, B100 */
  fresh_game(11);
  S.strategyEngineFlag = 1, S.compFlag = 1, S.memory = 20, S.standardOps = 10000, S.nstrats = 3;
  run(10);
  CHECK(!pc_enabled(B_RUN), "Run waits for a tournament");
  press(B_NEW_TOURNEY);
  CHECK(S.tourneyInProg == 1 && S.rounds == 9 && S.runEnabled && !pc_enabled(B_NEW_TOURNEY), "a tournament: 3 x 3 rounds");
  S.aa = 3, S.ab = 1, S.ba = 9, S.bb = 2;
  S.pickChoice = 1;
  run(200);
  press(B_RUN);
  static const int8_t H[9] = {0, 0, 0, 1, 1, 1, 2, 2, 2}, V[9] = {0, 1, 2, 1, 2, 0, 1, 2, 0};
  bool order = true;
  for (int r = 0; r < 9; r++) {
    order &= S.h == H[r] && S.v == V[r];
    run(1000);
  }
  CHECK(order, "the pairings in the game's order (h, v)");
  run(200);
  CHECK(S.tourneyInProg == 0 && S.resultsShown && S.nresults == 3, "the results");
  double a = S.stratScore[1] - 20 * S.aa - 20 * S.ab;   /* A100: aa against A, ab against B */
  CHECK(a >= 20 * 1 && a <= 20 * 3 && S.stratScore[2] - 20 * S.bb - 20 * S.ba >= 20 * 2, "A100's and B100's scores");
  int place = 0;
  for (int i = 0; i < 3; i++)
    if (S.results[i] == 1) place = i;
  double boost = 3 - place - 1;
  if (boost == 0) boost = 1;
  CHECK(S.yomi == S.stratScore[1] * boost, "yomi: the pick's score x the strategies it beat (%g)", S.yomi);
  for (int i = 1; i < 3; i++) CHECK(S.stratScore[S.results[i]] <= S.stratScore[S.results[i - 1]], "results in order");
  /* GREEDY with these payoffs (the biggest is BA): always B */
  fresh_game(12);
  S.strategyEngineFlag = 1, S.compFlag = 1, S.memory = 20, S.standardOps = 10000, S.nstrats = 4;
  run(10);
  press(B_NEW_TOURNEY);
  S.aa = 3, S.ab = 1, S.ba = 9, S.bb = 2;
  press(B_RUN);
  bool greedy = true;
  for (int t = 0; t < 16 * 20; t++) {
    run(50);
    if (S.h == 3 && S.rCounter > 0) greedy &= S.hMove == 2;
  }
  CHECK(greedy, "GREEDY picks B when BA pays most");
}

static void rules_investments(void) {
  fresh_game(13);
  S.investmentEngineFlag = 1, S.funds = 1000;
  press(B_DEPOSIT);
  CHECK(S.bankroll == 1000 && S.funds == 0 && S.ledger == -1000, "deposit");
  run(200);
  CHECK(S.riskiness == 7 && S.portTotal == 1000, "low risk: 7");
  run(120000);
  CHECK(S.nstocks >= 1, "stocks bought in two minutes (%d)", S.nstocks);
  for (int i = 0; i < S.nstocks; i++)
    CHECK(S.stocks[i].amount >= 1 && strlen(S.stocks[i].symbol) >= 1 && strlen(S.stocks[i].symbol) <= 4, "a stock");
  press(B_WITHDRAW);
  CHECK(S.bankroll == 0 && S.funds > 0, "withdraw the cash");
  S.yomi = 1000;
  press(B_INVEST_UP);
  CHECK(S.investLevel == 1 && S.investUpgradeCost == 658 && S.yomi == 900 && NEAR(S.stockGainThreshold, .51),
        "an upgrade: then floor(2^e x 100) = %g", S.investUpgradeCost);
  press(B_RISK);
  run(100);
  CHECK(S.riskiness == 5, "medium risk: 5");
}

static void rules_earth(void) {
  fresh_game(14);
  S.humanFlag = 0, S.factoryFlag = 1, S.unusedClips = 1e20;
  static const double COST[9] = {1e8, 1e9, 9e9, 7.2e10, 5.04e11, 3.024e12, 1.512e13, 6.048e13, 1.2096e14};
  bool ok = true;
  for (int i = 0; i < 9; i++) ok &= NEAR(S.factoryCost, COST[i]), press(B_FACTORY);
  CHECK(ok && S.factoryLevel == 9, "factories: 1e8, then x10, x9, ... x4, then x2");
  pc_factory_reboot();
  CHECK(S.factoryLevel == 0 && S.factoryCost == 1e8 && NEAR(S.unusedClips, 1e20), "Disassemble All: the clips back");
  S.harvesterFlag = S.wireDroneFlag = 1;
  press(B_HARV1);
  CHECK(S.harvesterLevel == 1 && NEAR(S.harvesterCost, js_pow(2, 2.25) * 1e6), "a harvester drone: then 2^2.25 x 1e6");
  press(B_HARV1000);
  CHECK(S.harvesterLevel == 1001, "+1k");
  press(B_WIRED10);
  CHECK(S.wireDroneLevel == 10, "+10 wire drones");
  /* power: farms 50 MW, factories 200, drones 1 each */
  fresh_game(15);
  S.humanFlag = 0, S.farmLevel = 2, S.factoryLevel = 1;
  run(10);
  CHECK(NEAR(S.powMod, .5), "1 MW of farms for 2 of factory: half power (%g)", S.powMod);
  S.farmLevel = 10, S.batteryLevel = 1;
  run(1000);
  CHECK(NEAR(S.storedPower, 100 * 3) && S.powMod >= 1, "the extra 3 MW into the battery each tick (%g)", S.storedPower);
  /* matter into wire */
  fresh_game(16);
  S.humanFlag = 0, S.harvesterLevel = 10, S.wireDroneLevel = 10, S.farmLevel = 10, S.wire = 0;
  run(10);
  double m = S.acquiredMatter + S.wire;
  CHECK(NEAR(m, 10 * 26180337.0 * 2), "harvesters: 26180337 g each a tick, x2 with the slider on Work (%g)", m);
  /* the swarm */
  fresh_game(17);
  S.humanFlag = 0, S.swarmFlag = 1, S.harvesterLevel = 500, S.wireDroneLevel = 500, S.farmLevel = 100, S.sliderPos = 100;
  run(10);
  CHECK(S.swarmStatus == 0 && S.giftCountdown > 0, "the swarm: active");
  double bits = 125000 / (js_log(1000) * 1.0);
  run((uint32_t)(bits * 10) + 100);
  CHECK(S.swarmGifts == js_round(js_log10(1000) * 100 / 100), "a gift of round(log10(drones) x slider / 100) = %g", S.swarmGifts);
  S.harvesterLevel = 5000;
  run(1100000);
  CHECK(S.disorgFlag == 1 && S.swarmStatus == 5, "uneven drones: disorganized (%g)", S.disorgCounter);
  S.yomi = 5000;
  press(B_SYNCH);
  CHECK(S.disorgFlag == 0 && S.yomi == 0, "Synchronize: 5,000 yomi");
}

static void rules_space(void) {
  fresh_game(18);
  S.spaceFlag = 1, S.humanFlag = 0, S.yomi = 100000;
  static const double COST[4] = {500, 1385, 2513, 3837};
  bool ok = true;
  for (int i = 0; i < 4; i++) ok &= S.probeTrustCost == COST[i], press(B_PROBE_TRUST);
  CHECK(ok && S.probeTrust == 4, "probe trust: floor((n + 1)^1.47 x 500)");
  run(10);
  press(B_STAT_UP + 3), run(10), press(B_STAT_UP + 3), run(10), press(B_STAT_UP + 2), run(10), press(B_STAT_UP + 0), run(10);
  CHECK(S.probeHaz == 2 && S.probeRep == 1 && S.probeSpeed == 1 && S.probeUsedTrust == 4 && !pc_enabled(B_STAT_UP + 1),
        "the design: 4 of 4");
  CHECK(NEAR(S.attackSpeed, .3), "speed raises attack speed by .1");
  S.unusedClips = 1e30;
  press(B_PROBE);
  CHECK(S.probeCount == 1 && S.probeLaunchLevel == 1 && NEAR(S.unusedClips, 1e30 - 1e17), "a probe: 100 quadrillion clips");
  S.probeCount = 1e6, S.probeHaz = 0;
  double before = S.probeCount;
  run(10);
  CHECK(S.probesLostHaz > before * .009 && S.probesLostHaz < before * .011, "no hazard remediation: 1%% a tick (%g)", S.probesLostHaz);
  /* drift, and battles */
  fresh_game(19);
  S.spaceFlag = 1, S.humanFlag = 0, S.probeCount = 1e9, S.drifterCount = 2e9, S.probeTrust = 20, S.probeHaz = 40;
  S.probeCombat = 8, S.probeRep = 10, S.unusedClips = 1e40;
  run(100);
  CHECK(S.battles == 1 && S.battleFlag == 1, "drifters over a million: a battle");
  CHECK(S.unitSize > .9e7 && S.unitSize < 1.1e7 && F->numShips == (int)(S.battleLeftShips + S.battleRightShips) && S.battleRightShips <= 200,
        "a battle: ships of unitSize probes (%g, %d ships)", S.unitSize, F->numShips);
  run(60000);
  CHECK(S.probesLostCombat > 0 && S.driftersKilled > 0, "combat 8: both sides lose some (%g, %g)", S.probesLostCombat,
        S.driftersKilled);
  CHECK(!strncmp(S.battleName, "Drifter Attack ", 15), "its name: %s", S.battleName);
  S.flag[P121] = 1, S.battleNameFlag = 1;
  for (int i = 0; i < 2000 && S.battles; i++) run(100);
  for (int i = 0; i < 2000 && !S.battles; i++) S.drifterCount = 2e9, run(10);
  CHECK(strncmp(S.battleName, "Drifter Attack", 14) != 0 && strchr(S.battleName, ' '), "named battles: %s", S.battleName);
}

static void rules_endings(void) {
  /* Reject, then the dismantling */
  fresh_game(20);
  S.spaceFlag = 1, S.humanFlag = 0, S.compFlag = 1, S.projectsFlag = 1, S.processors = 100, S.memory = 200;
  S.milestoneFlag = 15, S.standardOps = 200000, S.foundMatter = S.totalMatter, S.availableMatter = 0, S.wire = 0;
  S.uses[P46] = 0, S.flag[P46] = 1;
  run(10);
  for (int p = P140; p <= P146; p++) press(B_PROJECT + p), run(10);
  int choice = 0;
  for (int k = 0; k < S.nactive; k++) choice += S.active[k] == P147 || S.active[k] == P148;
  CHECK(choice == 2, "Accept or Reject");
  press(B_PROJECT + P148);
  choice = 0;
  for (int k = 0; k < S.nactive; k++) choice += S.active[k] == P147 || S.active[k] == P148;
  CHECK(S.flag[P148] && choice == 0, "Reject: both go");
  run(10010);
  press(B_PROJECT + P210);
  CHECK(S.dismantle == 1 && S.probeCount == 0, "Disassemble the Probes after 10 s");
  run(3510), S.standardOps = 200000, run(10);
  press(B_PROJECT + P211);
  run(3010), S.standardOps = 200000, run(10);
  press(B_PROJECT + P212);
  run(1510), S.standardOps = 200000, run(10);
  press(B_PROJECT + P213);
  CHECK(S.dismantle == 4 && S.wire == 50, "the strategy engine: 50 inches of wire");
  run(1010), S.standardOps = 200000, run(10);
  press(B_PROJECT + P214);
  run(3010), S.standardOps = 200000, run(10);
  CHECK(S.wire == 60, "the photonic chips: 10 more inches (%g)", S.wire);
  press(B_PROJECT + P215);
  run(1510);
  press(B_PROJECT + P216);
  CHECK(S.dismantle == 7 && S.memory == 0 && S.processors == 0 && S.wire == 100, "memory, processors: 100 inches in all");
  for (int i = 0; i < 100; i++) press(B_CLIP);
  CHECK(S.finalClips == 100 && S.wire == 0, "the last 100 clips");
  run(9100);
  CHECK(S.milestoneFlag == 20 && !strcmp(S.msg[0], "\xA9 2017 Everybody House Games") &&
        !strcmp(S.msg[2], "a game by Frank Lantz"), "the credits: %s", S.msg[1]);
  /* Accept: a new universe */
  fresh_game(21);
  S.flag[P147] = 1, S.compFlag = 1, S.projectsFlag = 1, S.memory = 400, S.standardOps = 300000;
  run(10);
  press(B_PROJECT + P200);
  CHECK(S.resetPending && S.prestigeU == 1, "The Universe Next Door");
  pc_new(&S, 5, S.prestigeU, S.prestigeS);
  run(10);
  CHECK(NEAR(S.demand, 3.2 * 1.1), "the next universe: demand +10%% (%g)", S.demand);
  /* the sim level: creativity 10% faster */
  pc_new(&S, 5, 0, 1);
  S.compFlag = 1, S.creativityOn = 1, S.standardOps = 1000;
  run(36400);
  CHECK(S.creativity == 10, "Sim Level 1: 1 creat every 364 ticks (%g)", S.creativity);
}

static void numbers(void) {
  char b[400];
  static const struct {
    double v;
    const char *s;
  } TS[] = {{0.1 + 0.2, "0.30000000000000004"}, {1e21, "1e+21"}, {123456789012345680000.0, "123456789012345680000"},
            {1.0 / 3, "0.3333333333333333"}, {5e-7, "5e-7"}, {0.000001, "0.000001"}, {3e55, "3e+55"}, {100, "100"},
            {-12.5, "-12.5"}, {1.5e300, "1.5e+300"}};
  for (unsigned i = 0; i < sizeof TS / sizeof TS[0]; i++)
    CHECK(!strcmp(js_tostring(TS[i].v, b, sizeof b), TS[i].s), "toString %s: %s", TS[i].s, b);
  CHECK(!strcmp(js_tofixed(1.005, 2, b, sizeof b), "1.00") && !strcmp(js_tofixed(2.5, 0, b, sizeof b), "3") &&
        !strcmp(js_tofixed(.25, 2, b, sizeof b), "0.25"), "toFixed");
  CHECK(!strcmp(js_commas(1234567.891, 2, b, sizeof b), "1,234,567.89"), "formatWithCommas: %s", b);
  CHECK(!strcmp(js_commas(1.5e25, 0, b, sizeof b), "15,000,000,000,000,000,000,000,000"), "with e+: %s", b);
  CHECK(!strcmp(js_commas(999.99, 0, b, sizeof b), "999"), "cut, not rounded");
  CHECK(!strcmp(js_spellf(0, b, sizeof b), "0") && !strcmp(js_spellf(5, b, sizeof b), "5.0") &&
        !strcmp(js_spellf(1234, b, sizeof b), "1.2 thousand"), "spellf small: %s", b);
  CHECK(!strcmp(js_spellf(3e55, b, sizeof b), "30.0 septendecillion") && !strcmp(js_spellf(6e27, b, sizeof b), "6.0 octillion"),
        "spellf big: %s", b);
  CHECK(!strcmp(js_cruncher(1234567, 2, b, sizeof b), "1.23 million") && !strcmp(js_cruncher(12, 2, b, sizeof b), "12"),
        "numberCruncher: %s", b);
  CHECK(!strcmp(js_locale(2625, b, sizeof b), "2,625") && !strcmp(js_locale(12345.6789, b, sizeof b), "12,345.679"),
        "toLocaleString: %s", b);
  CHECK(!strcmp(js_time(370012, b, sizeof b), "1 hour 1 minute 40 seconds"), "timeCruncher: %s", b);
  double e = 0;
  for (int i = 1; i < 2000; i++) {
    double x = i * 0.37, d = js_log(js_exp(x / 100)) - x / 100;
    if (d < 0) d = -d;
    if (d > e) e = d;
  }
  CHECK(e < 1e-14, "log(exp(x)) = x (%g)", e);
  CHECK(js_log10(1000) == 3 && js_pow(2, 10) == 1024 && js_pow(3, .5) > 1.7320508 && js_pow(3, .5) < 1.7320509, "log10, pow");
  CHECK(js_sin(0) == 0 && js_sin(3.14159265358979) < 1e-14 && js_sin(1.5707963267948966) == 1, "sin");
}
/* ---------------------------------------------------------------- the app: keys, the pause, saves */
static const struct {
  uint32_t from, to, keys;
} *keyplan;
static int nkeyplan;
static uint32_t plan_keys(void) {
  uint32_t k = 0;
  for (int i = 0; i < nkeyplan; i++)
    if (frame_no >= keyplan[i].from && frame_no < keyplan[i].to) k |= keyplan[i].keys;
  return k;
}
#define PLAN(...)                                                                    \
  do {                                                                               \
    static const struct {                                                            \
      uint32_t from, to, keys;                                                       \
    } p_[] = {__VA_ARGS__};                                                          \
    keyplan = (const void *)p_, nkeyplan = (int)(sizeof p_ / sizeof p_[0]);          \
  } while (0)
static void session(uint32_t frames) {
  frame_no = 0, quit_at = frames, script = plan_keys, step_ms = 0;
  pc_app_main();
  script = 0, quit_at = 0xFFFFFFFFu;
}
static bool save_ok(void) {
  uint32_t n;
  const uint8_t *d = file_data(PC_SAVE_NAME, &n);
  return d && pc_file_ok(d, n);
}

static void app(void) {
  memset(files, 0, sizeof files);
  rnd_state = 77, clock_ms = 0;
  /* the first time: how to play, OK starts; then OK held on Make Paperclip for 2 s (33 ms frames) */
  PLAN({5, 6, PK_OK}, {10, 70, PK_OK});
  session(80);
  CHECK(save_ok(), "saved on Home");
  uint32_t n;
  CHECK(file_data(PC_COPY_NAME, &n) && n > PC_FILE_SIZE, "and copied into paperclips_saves.py");
  CHECK(game.clips >= 15 && game.clips <= 30, "OK held: a clip, then one every 70 ms after 400 ms (%g)", game.clips);
  double clips = game.clips;
  /* again: no guide; Back pauses, How to play, its pages, back; Quit game asks, then quits */
  PLAN({5, 6, PK_BACK}, {10, 11, PK_DOWN}, {15, 16, PK_OK}, {20, 21, PK_RIGHT}, {25, 26, PK_RIGHT}, {30, 31, PK_BACK},
       {35, 36, PK_DOWN}, {40, 41, PK_OK}, {45, 46, PK_OK});
  session(400);
  CHECK(frame_no < 60, "Quit game, then Quit: out (%u)", frame_no);
  CHECK(game.clips == clips && save_ok(), "the clips kept (%g)", game.clips);
  /* the tabs: Right goes on to Business, Left comes back */
  PLAN({5, 6, PK_RIGHT});
  session(10);
  CHECK(ui.tab == T_BUSINESS, "Right: the next tab");
  PLAN({5, 6, PK_LEFT}, {8, 9, PK_LEFT});
  session(12);
  CHECK(ui.tab == T_BUSINESS, "Left twice: round to the last tab (%d)", ui.tab);
  /* Quit game, then Keep playing: still in */
  PLAN({5, 6, PK_BACK}, {10, 11, PK_DOWN}, {12, 13, PK_DOWN}, {15, 16, PK_OK}, {20, 21, PK_DOWN}, {25, 26, PK_OK},
       {30, 31, PK_BACK});
  session(60);
  CHECK(frame_no == 60 && ui.overlay == O_NONE, "Keep playing, then Back: playing again");
}

static uint8_t keep_main[PC_FILE_SIZE + 16], keep_copy[16384];
static uint32_t keep_main_n, keep_copy_n;
static void keep(void) {
  /* (the lengths are known once file_data is back: not in the same call, whose arguments come in any order) */
  const uint8_t *m = file_data(PC_SAVE_NAME, &keep_main_n), *c = file_data(PC_COPY_NAME, &keep_copy_n);
  memcpy(keep_main, m, keep_main_n);
  memcpy(keep_copy, c, keep_copy_n);
}
static void restore(void) {
  memset(files, 0, sizeof files);
  plat_save(PC_SAVE_NAME, keep_main, keep_main_n);
  plat_save(PC_COPY_NAME, keep_copy, keep_copy_n);
}
static void resum(uint8_t *f) {
  uint32_t sum = pc_sum(f, PC_FILE_SIZE - 4);
  memcpy(f + PC_FILE_SIZE - 4, &sum, 4);
}

static void saves(void) {
  memset(files, 0, sizeof files);
  rnd_state = 5, clock_ms = 0;
  PLAN({5, 6, PK_OK}, {10, 200, PK_OK});
  session(220);
  double clips = game.clips;
  CHECK(clips > 50 && save_ok(), "a game saved");
  keep();
  nkeyplan = 0;
  /* damaged: the copy is read, and written back */
  restore();
  uint32_t n;
  uint8_t *d = file_data(PC_SAVE_NAME, &n);
  d[200] ^= 0x40;
  session(10);
  CHECK(game.clips == clips && save_ok(), "a damaged save: from the copy (%g)", game.clips);
  restore();
  forget(PC_SAVE_NAME);
  session(10);
  CHECK(game.clips == clips && save_ok(), "no save, the copy: from it");
  restore();
  d = file_data(PC_SAVE_NAME, &n);
  files[0].len = n - 1;
  for (int i = 0; i < 4; i++)
    if (files[i].on && !strcmp(files[i].name, PC_SAVE_NAME)) files[i].len = n - 1;
  session(10);
  CHECK(game.clips == clips, "cut short: from the copy");
  /* both damaged: a fresh start, with how to play */
  restore();
  file_data(PC_SAVE_NAME, &n)[100] ^= 1;
  uint8_t *c = file_data(PC_COPY_NAME, &n);
  c[n / 2] = '!';
  session(10);
  CHECK(game.clips == 0 && ui.overlay == O_GUIDE, "both damaged: a new game (%g)", game.clips);
  /* a sum that's right over numbers that aren't: not read */
  restore();
  forget(PC_COPY_NAME);
  d = file_data(PC_SAVE_NAME, &n);
  pc_game *s = (pc_game *)(void *)(keep_main + sizeof(pc_head));
  (void)s;
  int32_t bad = 99;
  memcpy(d + sizeof(pc_head) + offsetof(pc_game, nstrats), &bad, 4);
  resum(d);
  CHECK(!pc_file_ok(d, n), "a strategy past the eight: refused");
  session(10);
  CHECK(game.clips == 0, "and a new game instead");
  restore();
  forget(PC_COPY_NAME);
  d = file_data(PC_SAVE_NAME, &n);
  uint16_t v = PC_VERSION + 1;
  memcpy(d + 4, &v, 2);
  resum(d);
  session(10);
  CHECK(game.clips == 0, "another version: not read");
  restore();
  forget(PC_COPY_NAME);
  d = file_data(PC_SAVE_NAME, &n);
  uint8_t idx = 200;
  memcpy(d + sizeof(pc_head) + offsetof(pc_game, active), &idx, 1);
  memcpy(d + sizeof(pc_head) + offsetof(pc_game, nactive), &(int32_t){1}, 4);
  resum(d);
  CHECK(!pc_file_ok(d, n), "a project past the 96: refused");
  /* a full storage: the game goes on */
  memset(files, 0, sizeof files);
  for (int i = 0; i < 4; i++) {
    char name[16];
    snprintf(name, sizeof name, "full%d.py", i);
    plat_save(name, "x", 1);
  }
  PLAN({5, 6, PK_OK}, {10, 40, PK_OK});
  session(50);
  CHECK(game.clips > 0 && !file_data(PC_SAVE_NAME, &n), "no room to save: it plays on");
}

/* ---------------------------------------------------------------- random keys */
#define FUZZ_STATES 6
static uint8_t fuzz_state[FUZZ_STATES][PC_FILE_SIZE];
static int fuzz_states;
static uint32_t fuzz_rng;
static uint32_t fuzz_next(void) { return fuzz_rng = fuzz_rng * 1103515245u + 12345u; }
static uint32_t fuzz_held, fuzz_until;
static uint32_t fuzz_keys(void) {
  step_ms = (fuzz_next() >> 16) % 8 == 0 ? (fuzz_next() >> 8) % 20000 : (fuzz_next() >> 8) % 200;
  if (frame_no >= fuzz_until) {
    static const uint32_t K[] = {PK_OK, PK_UP, PK_DOWN, PK_LEFT, PK_RIGHT, PK_BACK, PK_OK, PK_DOWN, PK_OK, 0, 0};
    fuzz_held = K[(fuzz_next() >> 12) % (sizeof K / sizeof K[0])];
    if ((fuzz_next() >> 10) % 6 == 0) fuzz_held |= K[(fuzz_next() >> 14) % 9];
    fuzz_until = frame_no + 1 + (fuzz_next() >> 12) % 12;
  }
  return fuzz_held;
}
static void fuzz(int sessions) {
  int bad = 0;
  for (int k = 0; k < sessions; k++) {
    memset(files, 0, sizeof files);
    fuzz_rng = 0x5EED + (uint32_t)k * 7919u, rnd_state = (uint32_t)k, clock_ms = 0;
    int from = k % (fuzz_states + 1);
    if (from < fuzz_states) plat_save(PC_SAVE_NAME, fuzz_state[from], PC_FILE_SIZE);
    frame_no = 0, quit_at = 250 + (uint32_t)(k % 7) * 50, script = fuzz_keys, fuzz_until = 0;
    pc_app_main();
    script = 0;
    if (!pc_game_ok(&game) || !save_ok()) bad++;
  }
  quit_at = 0xFFFFFFFFu;
  CHECK(!bad, "%d random-key sessions: every save good (%d bad)", sessions, bad);
}

/* ---------------------------------------------------------------- the bot's game, to the end */
static void take_states(void) {
  static int taken;
  pc_game *s = &game;
  bool now = (taken == 0 && s->strategyEngineFlag == 1) || (taken == 1 && s->humanFlag == 0 && s->factoryLevel >= 3) ||
             (taken == 2 && s->spaceFlag == 1 && s->probeCount > 1000) || (taken == 3 && s->battles > 0) ||
             (taken == 4 && s->flag[P148] == 1) || (taken == 5 && s->milestoneFlag >= 20);
  if (now && fuzz_states < FUZZ_STATES) save_pack(s, 1, (uint8_t)ui.tab, fuzz_state[fuzz_states++]), taken++;
}

static void whole_game(void) {
  void (*old)(void) = on_frame;
  on_frame = take_states;
  uint32_t h1, h2;
  bool done = bot_game(1, &h1);
  on_frame = old;
  double t1 = game.ticks / 100;
  CHECK(done, "the bot plays the whole game to the credits (%.0f s of play, milestone %.0f)", t1, game.milestoneFlag);
  CHECK(game.milestoneFlag == 20 && game.finalClips >= 100 && game.dismantle == 7 && game.flag[P148] == 1,
        "the end: rejected the drift, took everything apart, made the last clips");
  bool credits = false;
  for (int i = 0; i < PC_MSG; i++) credits |= !strcmp(game.msg[i], "a game by Frank Lantz");
  CHECK(credits, "the credits in the console");
  CHECK(fuzz_states == FUZZ_STATES, "states kept for the random keys (%d)", fuzz_states);
  printf("  the whole game in %.0f s of play (%.1f h), %u presses, its hash %08X\n", t1, t1 / 3600, bot_presses, h1);
  /* twenty minutes of it again, twice: the same bits */
  bot_limit_s = 1200;
  bot_game(1, &h2);
  uint32_t h3;
  bot_game(1, &h3);
  bot_limit_s = 6 * 3600;
  CHECK(h2 == h3, "the same game twice: %08X and %08X", h2, h3);
  CHECK(h1 == RUN_HASH, "the whole game's hash, as on every computer: %08X", h1);
}

/* ---------------------------------------------------------------- the README's pictures and film, from the bot's game */
static const char *pic_dir;
static bool drawing;
static void view(int tab, bool top) {   /* the screen with a tab open (from its top), the app's put back after */
  pc_ui keep_ui = ui;
  drawing = true;
  if (tab >= 0 && ui_tab_shown(tab) && (top || tab != ui.tab)) {
    ui.tab = tab, ui.scroll[tab] = 0, ui.focus[tab] = -1, ui.focus_at[tab] = 0;
    memcpy(ui.seen, keep_ui.seen, sizeof ui.seen);
  }
  ui_render(&ui);
  ui = keep_ui;
  drawing = false;
}
static bool pic_done[16];
static const struct {
  const char *name;
  int tab;
} PICS[] = {{"projects", T_PROJECTS}, {"shot", T_BUSINESS}, {"computer", T_COMPUTER}, {"strategy", T_STRATEGY},
            {"invest", T_INVEST}, {"hypno", -1}, {"earth", T_CLIPS}, {"wire", T_WIRE}, {"power", T_POWER},
            {"swarm", T_COMPUTER}, {"space", T_SPACE}, {"probes", T_SPACE}, {"combat", T_COMBAT}, {"drift", T_PROJECTS},
            {"end", T_CLIPS}};
static bool pic_when(int i) {
  pc_game *s = &game;
  switch (i) {
    case 0: return s->humanFlag == 1 && s->nactive >= 4 && s->creativityOn && s->qFlag && s->milestoneFlag >= 5;
    case 1: return s->humanFlag == 1 && s->revPerSecFlag && s->marketingLvl >= 6 && s->unsoldClips > 0;
    case 2: return s->humanFlag == 1 && s->nextQchip >= 4 && s->qShown == 2 && s->qFade > .8;
    case 3: return s->humanFlag == 1 && s->resultsShown && s->nstrats >= 6;
    case 4: return s->humanFlag == 1 && s->nstocks >= 4;
    case 5: return s->hypnoOn && s->hypnoVisible && s->hypnoText == 3;
    case 6: return s->humanFlag == 0 && s->spaceFlag == 0 && s->factoryLevel >= 12 && s->tothFlag;
    case 7: return s->humanFlag == 0 && s->spaceFlag == 0 && s->harvesterLevel >= 600 && s->maps > 0;
    case 8: return s->humanFlag == 0 && s->spaceFlag == 0 && s->farmLevel >= 40 && s->batteryLevel >= 1;
    case 9: return s->humanFlag == 0 && s->swarmFlag && s->swarmStatus == 0 && s->spaceFlag == 0 && s->swarmGifts > 0;
    case 10: return s->spaceFlag == 1 && s->probesLostDrift > 1000 && s->probesLostCombat > 0 && s->dismantle == 0;
    case 11: return s->spaceFlag == 1 && s->flag[P131] && s->probeTrust >= 20 && s->dismantle == 0;
    case 12: return s->battles > 0 && s->battleNameFlag && F->numLeft > 60 && F->numRight > 60 && s->battleResult == 0;
    case 13:
      for (int k = 0; k < s->nactive; k++)
        if (s->active[k] == P143) return true;
      return false;
    default: return s->milestoneFlag >= 20;
  }
}
static void pictures(void) {
  if (drawing || !pic_dir) return;
  for (unsigned i = 0; i < sizeof PICS / sizeof PICS[0]; i++)
    if (!pic_done[i] && pic_when((int)i)) {
      view(PICS[i].tab, true);
      shot(pic_dir, PICS[i].name);
      pic_done[i] = true;
    }
}

/* the film: a few seconds of each part of the game, at 15 frames a second */
static const char *film_dir;
static int film_part = -1, film_tab;
static uint32_t film_end, film_t, film_frames;
static bool film_when(int part) {
  pc_game *s = &game;
  switch (part) {
    case 0: return true;
    case 1: return s->humanFlag == 1 && s->tourneyInProg && s->nstrats >= 4 && s->currentRound >= 2;
    case 2: return s->hypnoOn;
    case 3: return s->battles > 0 && s->battleNameFlag && F->numLeft > 100 && F->numRight > 100;
    default: return s->flag[P216] == 1 && s->wire >= 60;
  }
}
static const int FILM_TAB[5] = {T_CLIPS, T_STRATEGY, -1, T_COMBAT, T_CLIPS};
static const uint32_t FILM_MS[5] = {6000, 3500, 4000, 4500, 5000};
static int film_next;
static void filming(void) {
  if (drawing || !film_dir) return;
  if (film_part < 0 && film_next < 5 && film_when(film_next))
    film_part = film_next++, film_tab = FILM_TAB[film_part], film_end = game.now + FILM_MS[film_part];
  if (film_part < 0) return;
  if (!(frame_no & 1)) {
    view(film_tab, false);
    char path[512];
    snprintf(path, sizeof path, "%s/%06u_%06u.raw", film_dir, film_frames++, film_t);
    FILE *f = fopen(path, "wb");
    if (!f) exit(2);
    fwrite(fb, 2, 320 * 240, f);
    fclose(f);
    film_t += 66;
  }
  if ((int32_t)(game.now - film_end) >= 0) film_part = -1;
}
static uint32_t film_script(void) {
  uint32_t k = bot_script();
  if (film_part >= 0) step_ms = 0;
  return k;
}
static void docs_frame(void) {
  pictures();
  filming();
}
static void docs(const char *pics, const char *film) {
  pic_dir = pics, film_dir = film;
  on_frame = docs_frame;
  memset(files, 0, sizeof files);
  memset(bot_seen_at, 0, sizeof bot_seen_at);
  rnd_state = 1, clock_ms = 0, frame_no = 0, quit_at = 0xFFFFFFFFu;
  bot_done = false, bot_target = -1, bot_last = 0;
  script = film_script;
  pc_app_main();
  on_frame = 0, script = 0;
  for (unsigned i = 0; i < sizeof PICS / sizeof PICS[0]; i++)
    if (!pic_done[i]) printf("no picture: %s\n", PICS[i].name);
}

int main(int argc, char **argv) {
  if (argc >= 4 && !strcmp(argv[1], "docs")) {
    docs(argv[2], argv[3]);
    return 0;
  }
  if (argc >= 3 && !strcmp(argv[1], "states")) {
    states_dir = argv[2];
    return bot_game(1, 0) ? 0 : 1;
  }
  if (argc >= 2 && !strcmp(argv[1], "bot")) {
    bot_verbose = true;
    bool ok = bot_game(argc >= 3 ? (uint32_t)atoi(argv[2]) : 1, 0);
    printf("bot %s: %.0f s of play, %u presses, %u frames, milestone %.0f, hash %08X\n", ok ? "finished" : "stuck",
           game.ticks / 100, bot_presses, frame_no, game.milestoneFlag, pc_sum(&game, PC_KEPT));
    for (int i = PC_MSG - 1; i >= 0; i--) printf("  > %s\n", game.msg[i]);
    return ok ? 0 : 1;
  }
  if (argc >= 3 && !strcmp(argv[1], "test")) {
    printf("rules\n");
    rules_business(), rules_projects(), rules_quantum_tourney(), rules_investments(), rules_earth(), rules_space();
    rules_endings(), numbers();
    printf("the app\n");
    app(), saves();
    printf("the whole game\n");
    whole_game();
    printf("random keys\n");
    fuzz(300);
    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
  }
  printf("usage: play test|shots|film|bot DIR\n");
  return 2;
}
