/* pc.h: the game's rules, from main.js and combat.js, function by function and in their order (the projects are in
 * projects.c). Display code is left out; what the page's code changes in the game's numbers while drawing (the
 * buttons' timers, the battle's explosions) is kept. */
#include <string.h>
#include "num.h"
#include "pc.h"

pc_game *G;
pc_fleet *F;
#define g G

/* ---------------------------------------------------------------- Math.random() (xoshiro128**) */
static uint32_t rotl(uint32_t x, int k) { return x << k | x >> (32 - k); }
static uint32_t next_u32(void) {
  uint32_t *s = g->rng;
  uint32_t r = rotl(s[1] * 5, 7) * 9, t = s[1] << 9;
  s[2] ^= s[0], s[3] ^= s[1], s[1] ^= s[2], s[0] ^= s[3], s[2] ^= t, s[3] = rotl(s[3], 11);
  return r;
}
double pc_random(void) {
  uint32_t a = next_u32() >> 5, b = next_u32() >> 6;
  return (a * 67108864.0 + b) * (1.0 / 9007199254740992.0);
}
static void seed(uint32_t x) {
  for (int i = 0; i < 4; i++) {   /* splitmix32 */
    x += 0x9E3779B9u;
    uint32_t z = x;
    z = (z ^ z >> 16) * 0x85EBCA6Bu;
    z = (z ^ z >> 13) * 0xC2B2AE35u;
    g->rng[i] = z ^ z >> 16;
  }
  if (!(g->rng[0] | g->rng[1] | g->rng[2] | g->rng[3])) g->rng[0] = 1;
}

uint32_t pc_hash(const pc_game *s) {
  const uint8_t *p = (const uint8_t *)s;
  uint32_t h = 2166136261u;
  for (uint32_t i = 0; i < sizeof *s; i++) h = (h ^ p[i]) * 16777619u;
  return h;
}

/* ---------------------------------------------------------------- the console */
void pc_message(const char *s) {
  for (int i = PC_MSG - 1; i > 0; i--) memcpy(g->msg[i], g->msg[i - 1], PC_MSG_LEN);
  strncpy(g->msg[0], s, PC_MSG_LEN - 1);
  g->msg[0][PC_MSG_LEN - 1] = 0;
  g->nmsg++;
}
void pc_messagef(const char *a, const char *b, const char *c) {
  char t[PC_MSG_LEN];
  t[0] = 0;
  strncat(t, a, sizeof t - 1);
  if (b) strncat(t, b, sizeof t - 1 - strlen(t));
  if (c) strncat(t, c, sizeof t - 1 - strlen(t));
  pc_message(t);
}
static char *str(double v, char *b) { return js_tostring(v, b, 40); }
static char *commas(double v, int d, char *b) { return js_commas(v, d, b, 120); }

/* ---------------------------------------------------------------- a new game: globals.js and main.js's vars */
static const char *const STRAT_NAME[PC_STRATS] = {"RANDOM", "A100", "B100", "GREEDY", "GENEROUS", "MINIMAX",
                                                  "TIT FOR TAT", "BEAT LAST"};
const char *pc_strat_name(int i) { return i >= 0 && i < PC_STRATS ? STRAT_NAME[i] : "?"; }

void pc_new(pc_game *s, uint32_t sd, double prestigeU, double prestigeS) {
  G = s;
  memset(g, 0, sizeof *g);
  seed(sd);
  g->margin = .25, g->wire = 1000, g->wireCost = 20, g->adCost = 100, g->demand = 5, g->marketing = 1;
  g->marketingLvl = 1, g->clipperCost = 5, g->processors = 1, g->memory = 1, g->trust = 2, g->nextTrust = 3000;
  g->transaction = 1, g->clipperBoost = 1, g->wireSupply = 1000, g->marketingEffectiveness = 1, g->fib1 = 2;
  g->fib2 = 3, g->megaClipperCost = 500, g->megaClipperBoost = 1, g->creativitySpeed = 1, g->demandBoost = 1;
  g->humanFlag = 1, g->factoryBoost = 1, g->droneBoost = 1, g->availableMatter = 1e24 * 6000;
  g->factoryCost = 100000000, g->harvesterCost = 1000000, g->wireDroneCost = 1000000, g->factoryRate = 1000000000;
  g->harvesterRate = 26180337, g->wireDroneRate = 16180339, g->totalMatter = 1e54 * 30;
  g->foundMatter = g->availableMatter, g->qChipCost = 10000, g->bribe = 1000000, g->wireBasePrice = 20;
  g->farmCost = 10000000, g->batteryCost = 1000000, g->swarmStatus = 7, g->giftCountdown = 125000;
  g->maxTrust = 20, g->maxTrustCost = 91117.99, g->synchCost = 5000, g->threnodyCost = 50000;
  g->entertainCost = 10000, g->wireBuyerStatus = 1, g->qFade = 1, g->autoTourneyStatus = 1;
  g->prestigeU = prestigeU, g->prestigeS = prestigeS;
  g->nIncome = 1;   /* incomeTracker = [0] */
  g->riskiness = 5, g->investUpgradeCost = 100, g->stockGainThreshold = .5;
  g->tourneyCost = 1000, g->tourneyLvl = 1, g->hMove = 1, g->vMove = 1, g->hMovePrev = 1, g->vMovePrev = 1;
  g->yomiBoost = 1, g->pick = 10, g->pickChoice = 10, g->nstrats = 1;
  for (int i = 0; i < PC_STRATS; i++) g->stratPos[i] = 1;
  g->probeCost = 1e17, g->probeTrustCost = js_floor(js_pow(0 + 1, 1.47) * 500);
  g->attackSpeed = .2, g->battleSpeed = .2, g->outcomeTimer = 150, g->warTrigger = 1000000;
  g->battleEndTimer = 100, g->battleLeftShips = 200, g->battleRightShips = 200;
  strcpy(g->battleName, "foo");
  strcpy(g->threnodyTitle, "Durenstein 1");
  strcpy(g->project133Title, "Durenstein 1");
  strcpy(g->tourneyText, "Pick strategy, run tournament, gain yomi");
  for (int i = 0; i < PC_BATTLE_NAMES; i++) g->battleNumbers[i] = 1;
  for (int i = 0; i < PC_PROJECTS; i++) g->uses[i] = 1;
  strcpy(g->msg[0], "Welcome to Universal Paperclips");
  g->at_main = 10, g->at_slow = 100, g->at_battle = 16, g->at_stocks = 100, g->at_shop = 1000, g->at_sell = 2500;
  g->at_pick = 100;
  g->blinkProject = -1;
  if (F) F->numShips = F->numLeft = F->numRight = 0;
}

static void update_drone_prices(void);
static void update_pow_prices(void);

void pc_loaded(pc_game *s) {
  G = s;
  update_drone_prices();
  update_pow_prices();
  if (g->battles > 0) g->battles = 0;   /* (refresh: a battle in progress is dropped) */
  g->battleResult = 0;
  if (F) F->numShips = F->numLeft = F->numRight = 0;
  g->hypnoCount = 0, g->hypnoOn = 0, g->blinkCount = 0, g->blinkProject = -1;
}

/* ---------------------------------------------------------------- wire */
static void adjust_wire_price(void) {
  g->wirePriceTimer++;
  if (g->wirePriceTimer > 250 && g->wireBasePrice > 15) {
    g->wireBasePrice = g->wireBasePrice - (g->wireBasePrice / 1000);
    g->wirePriceTimer = 0;
  }
  if (pc_random() < .015) {
    g->wirePriceCounter++;
    double wireAdjust = 6 * (js_sin(g->wirePriceCounter));
    g->wireCost = js_ceil(g->wireBasePrice + wireAdjust);
  }
}

static void toggle_wire_buyer(void) { g->wireBuyerStatus = g->wireBuyerStatus == 1 ? 0 : 1; }

static void buy_wire(void) {
  if (g->funds >= g->wireCost) {
    g->wirePriceTimer = 0;
    g->wire = g->wire + g->wireSupply;
    g->funds = g->funds - g->wireCost;
    g->wirePurchase = g->wirePurchase + 1;
    g->wireBasePrice = g->wireBasePrice + .05;
  }
}

/* ---------------------------------------------------------------- quantum computing */
static const double WAVE_SEED[PC_CHIPS] = {.1, .2, .3, .4, .5, .6, .7, .8, .9, 1};

static void quantum_compute(void) {
  g->qClock = g->qClock + .01;
  for (int i = 0; i < PC_CHIPS; i++) g->qChipValue[i] = js_sin(g->qClock * WAVE_SEED[i] * g->qChipActive[i]);
}

static void q_comp(void) {
  g->qFade = 1;
  double q = 0;
  if (g->qChipActive[0] == 0) {
    g->qShown = 1;   /* "Need Photonic Chips" */
  } else {
    for (int i = 0; i < PC_CHIPS; i++) q = q + g->qChipValue[i];
    double qq = js_ceil(q * 360);
    double buffer = (g->memory * 1000) - g->standardOps;
    double damper = (g->tempOps / 100) + 5;
    if (qq > buffer) {
      g->tempOps = g->tempOps + js_ceil(qq / damper) - buffer;
      qq = buffer;
      g->opFade = .01;
      g->opFadeTimer = 0;
    }
    g->standardOps = g->standardOps + qq;
    g->qShown = 2, g->qOpsShown = js_ceil(q * 360);
  }
}

/* ---------------------------------------------------------------- the HypnoDrones and the projects' blink */
void pc_hypno_event(void) {
  g->hypnoText = 0, g->hypnoVisible = 0, g->hypnoCount = 0;
  g->hypnoOn = 1;
  g->at_hypno = g->now + 32;
}
static void hypno_step(void) {
  g->hypnoCount++;
  int c = g->hypnoCount;
  if (c > 5 && c < 10) g->hypnoText = 0;
  if (c > 30 && c < 40) g->hypnoText = 1;
  if (c > 45 && c < 55) g->hypnoText = 2;
  if (c > 55) g->hypnoText = 3;
  if (c >= 120) {
    g->hypnoOn = 0, g->hypnoCount = 0, g->hypnoVisible = 0;
  } else {
    g->hypnoVisible ^= 1;
    g->at_hypno += 32;
  }
}
void pc_blink(int project) {
  g->blinkProject = project, g->blinkCount = 0;
  g->at_blink = g->now + 30;
}
static void blink_step(void) {
  g->blinkCount++;
  if (g->blinkCount >= 12) g->blinkCount = 0, g->blinkProject = -1;
  else g->at_blink += 30;
}

/* ---------------------------------------------------------------- investments */
static void invest_upgrade(void) {
  char t[2][120];
  g->yomi = g->yomi - g->investUpgradeCost;
  g->investLevel++;
  g->stockGainThreshold = g->stockGainThreshold + .01;
  g->investUpgradeCost = js_floor(js_pow(g->investLevel + 1, JS_E) * 100);
  pc_messagef("Investment engine upgraded, expected profit/loss ratio now ", str(g->stockGainThreshold, t[0]), 0);
}

static void invest_deposit(void) {
  g->ledger = g->ledger - js_floor(g->funds);
  g->bankroll = js_floor(g->bankroll + g->funds);
  g->funds = 0;
}

static void invest_withdraw(void) {
  g->ledger = g->ledger + g->bankroll;
  g->funds = g->funds + g->bankroll;
  g->bankroll = 0;
}

static void generate_symbol(char *name) {
  int ltrNum = 0;
  double x = pc_random();
  if (x <= .01) ltrNum = 1;
  else if (x <= .1) ltrNum = 2;
  else if (x <= .4) ltrNum = 3;
  else ltrNum = 4;
  name[0] = (char)('A' + (int)js_floor(pc_random() * 26));
  for (int i = 1; i < ltrNum; i++) name[i] = (char)('A' + (int)js_floor(pc_random() * 26));
  name[ltrNum] = 0;
}

static void create_stock(double dollars) {
  g->stockID++;
  char sym[8];
  generate_symbol(sym);
  double roll = pc_random(), pri;
  if (roll > .99) pri = js_ceil(pc_random() * 3000);
  else if (roll > .85) pri = js_ceil(pc_random() * 500);
  else if (roll > .60) pri = js_ceil(pc_random() * 150);
  else if (roll > .20) pri = js_ceil(pc_random() * 50);
  else pri = js_ceil(pc_random() * 15);
  if (pri > dollars) pri = js_ceil(dollars * roll);
  double amt = js_floor(dollars / pri);
  if (amt > 1000000) amt = 1000000;
  if (g->nstocks >= PC_STOCKS) return;
  pc_stock *s = &g->stocks[g->nstocks++];
  memset(s, 0, sizeof *s);
  strcpy(s->symbol, sym);
  s->price = pri, s->amount = amt, s->total = pri * amt;
  g->portfolioSize = g->nstocks;
  g->bankroll = g->bankroll - (pri * amt);
}

static void sell_stock(void) {
  g->bankroll = g->bankroll + g->stocks[0].total;
  memmove(&g->stocks[0], &g->stocks[1], sizeof g->stocks[0] * (PC_STOCKS - 1));
  g->nstocks--;
  memset(&g->stocks[g->nstocks], 0, sizeof g->stocks[0]);
  g->portfolioSize = g->nstocks;
}

static void stock_shop(void) {
  double budget = js_ceil(g->portTotal / g->riskiness);
  double r = 11 - g->riskiness;
  double reserves = js_ceil(g->portTotal / r);
  if (g->riskiness == 1) reserves = 0;
  if ((g->bankroll - budget) < reserves && g->riskiness == 1 && g->bankroll > (g->portTotal / 10)) budget = g->bankroll;
  else if ((g->bankroll - budget) < reserves && g->riskiness == 1) budget = 0;
  else if ((g->bankroll - budget) < reserves) budget = g->bankroll - reserves;
  if (g->portfolioSize < 5 && g->bankroll >= 5 && budget >= 1 && g->bankroll - budget >= reserves) {
    if (pc_random() < .25) create_stock(budget);
  }
}

static void update_stocks(void) {
  for (int i = 0; i < g->portfolioSize && i < g->nstocks; i++) {
    pc_stock *s = &g->stocks[i];
    s->age = s->age + 1;
    if (pc_random() < .6) {
      bool gain = true;
      if (pc_random() > g->stockGainThreshold) gain = false;
      double currentPrice = s->price;
      double delta = js_ceil((pc_random() * currentPrice) / (4 * g->riskiness));
      if (gain) s->price = s->price + delta;
      else s->price = s->price - delta;
      if (s->price == 0 && pc_random() > .24) s->price = 1;
      s->total = s->price * s->amount;
      if (gain) s->profit = s->profit + (delta * s->amount);
      else s->profit = s->profit - (delta * s->amount);
    }
  }
}

static const double RISK[3] = {7, 5, 1};   /* the select: Low, Med, High */

static void stocks_tick(void) {   /* the stock list's 100 ms */
  g->riskiness = RISK[g->riskChoice % 3];
  double m = 0;
  for (int i = 0; i < g->portfolioSize && i < g->nstocks; i++) m = m + g->stocks[i].total;
  g->secTotal = m;
  g->portTotal = g->bankroll + g->secTotal;
  g->portfolioSize = g->nstocks;
}

static void sell_tick(void) {   /* 2.5 s */
  g->sellDelay = g->sellDelay + 1;
  if (g->portfolioSize > 0 && g->sellDelay >= 5 && pc_random() <= .3 && g->humanFlag == 1) {
    sell_stock();
    g->sellDelay = 0;
  }
  if (g->portfolioSize > 0 && g->humanFlag == 1) update_stocks();
}

/* ---------------------------------------------------------------- strategic modeling */
static const char *const CHOICE_A[17] = {"cooperate", "swerve", "macro", "fight", "bet", "raise_price", "opera", "go",
                                         "heads", "particle", "discrete", "peace", "search", "lead", "accept", "accept",
                                         "attack"};
static const char *const CHOICE_B[17] = {"defect", "straight", "micro", "back_down", "fold", "lower_price", "football",
                                         "stay", "tails", "wave", "continuous", "war", "evaluate", "follow", "reject",
                                         "deny", "decay"};
const char *pc_choice_name(int x, int b) {
  if (x < 0 || x >= 17) x = 0;
  return b ? CHOICE_B[x] : CHOICE_A[x];
}

static int find_biggest_payoff(void) {
  double aa = g->aa, ab = g->ab, ba = g->ba, bb = g->bb;
  if (aa >= ab && aa >= ba && aa >= bb) return 1;
  if (ab >= aa && ab >= ba && ab >= bb) return 2;
  if (ba >= aa && ba >= ab && ba >= bb) return 3;
  return 4;
}

static int what_beats_last(int myPos) {
  int oppsPos = myPos == 1 ? 2 : 1;
  if (oppsPos == 1 && g->hMovePrev == 1) return g->aa > g->ba ? 1 : 2;
  if (oppsPos == 1 && g->hMovePrev == 2) return g->ab > g->bb ? 1 : 2;
  if (oppsPos == 2 && g->vMovePrev == 1) return g->aa > g->ba ? 1 : 2;
  return g->ab > g->bb ? 1 : 2;
}

static int pick_move(int s) {
  int x;
  switch (s) {
    case 0: return pc_random() < .5 ? 1 : 2;
    case 1: return 1;
    case 2: return 2;
    case 3: x = find_biggest_payoff(); return x < 3 ? 1 : 2;
    case 4: x = find_biggest_payoff(); return x == 1 || x == 3 ? 1 : 2;
    case 5: x = find_biggest_payoff(); return x == 1 || x == 3 ? 2 : 1;
    case 6: return (int)(g->stratPos[s] == 1 ? g->vMovePrev : g->hMovePrev);
    default: return what_beats_last(g->stratPos[s]);
  }
}

static void pick_strats(int roundNum) {
  int n = g->nstrats, h, v;
  if (roundNum < n) {
    h = 0, v = roundNum;
  } else {
    g->stratCounter++;
    if (g->stratCounter >= n) g->stratCounter = g->stratCounter - n;
    h = roundNum / n, v = (int)g->stratCounter;
  }
  g->h = h, g->v = v;
  g->stratPos[h] = 1;
  g->stratPos[v] = 2;
}

static void generate_grid(void) {
  g->aa = js_ceil(pc_random() * 10);
  g->ab = js_ceil(pc_random() * 10);
  g->ba = js_ceil(pc_random() * 10);
  g->bb = js_ceil(pc_random() * 10);
  g->gridLabel = (int)js_floor(pc_random() * 17);
}

static void toggle_auto_tourney(void) { g->autoTourneyStatus = g->autoTourneyStatus == 1 ? 0 : 1; }

static void new_tourney(void) {
  g->resultsFlag = 0, g->resultsShown = 0;
  g->high = 0;
  g->tourneyInProg = 1;
  g->currentRound = 0;
  g->rounds = g->nstrats * g->nstrats;
  for (int i = 0; i < g->nstrats; i++) g->stratScore[i] = 0;
  g->stratCounter = 0;
  g->standardOps = g->standardOps - g->tourneyCost;
  g->tourneyLvl++;
  generate_grid();
  g->runEnabled = 1;
  g->h = g->v = -1;
  strcpy(g->tourneyText, "Pick strategy, run tournament, gain yomi");
}

static void pick_winner(void) {
  int temp[PC_STRATS], nt = g->nstrats;
  g->nresults = 0;
  for (int i = 0; i < nt; i++) temp[i] = i;
  for (int n = 0; n < g->nstrats; n++) {
    double tempHigh = 0;
    int tempWinnerPtr = 0;
    for (int i = 0; i < nt; i++)
      if (g->stratScore[temp[i]] > tempHigh) tempWinnerPtr = i, tempHigh = g->stratScore[temp[i]];
    g->results[g->nresults++] = temp[tempWinnerPtr];
    for (int i = tempWinnerPtr; i < nt - 1; i++) temp[i] = temp[i + 1];
    nt--;
  }
  for (int i = 0; i < g->nstrats; i++)
    if (g->stratScore[i] > g->high) g->winnerPtr = i, g->high = g->stratScore[i];
}

static void calculate_place_score(void) {
  g->placeScore = 0;
  for (int i = 1; i < g->nresults; i++)
    if (g->stratScore[g->results[i]] < g->stratScore[g->results[i - 1]]) {
      g->placeScore = g->stratScore[g->results[i]];
      break;
    }
}

static void calculate_show_score(void) {
  g->showScore = 0;
  for (int i = 1; i < g->nresults; i++)
    if (g->stratScore[g->results[i]] < g->placeScore) {
      g->showScore = g->stratScore[g->results[i]];
      break;
    }
}

static double calculate_strats_beat(void) {
  for (int i = 0; i < g->nresults; i++)
    if (g->results[i] == g->pick) return g->nresults - i;
  return 0;
}

static void declare_winner(void) {
  if (g->pick < 10 && g->pick < g->nstrats) {
    double bB = 0;
    const char *w = "strats";
    double beatBoost = calculate_strats_beat() - 1;
    if (beatBoost == 1) w = "strat";
    if (beatBoost == 0) bB = 0, beatBoost = 1;
    else bB = beatBoost;
    strcpy(g->tourneyText, "TOURNAMENT RESULTS");
    double score = g->stratScore[g->pick];
    g->yomi = g->yomi + score * g->yomiBoost * beatBoost;
    if (g->milestoneFlag < 15) {
      char t[200], a[40], b[40], c[40];
      strcpy(t, STRAT_NAME[g->pick]);
      strcat(t, " scored "), strcat(t, str(score, a)), strcat(t, " and beat "), strcat(t, str(bB, b));
      strcat(t, " "), strcat(t, w), strcat(t, ". Yomi increased by "), strcat(t, str(score * g->yomiBoost * beatBoost, c));
      pc_message(t);
    }
    if (g->flag[P128] == 1 && g->stratScore[(int)g->winnerPtr] == score) {
      g->yomi = g->yomi + 50000;
      if (g->milestoneFlag < 15) pc_message("Selected strategy won the tournament (or tied for first). +50,000 yomi");
    } else if (g->flag[P128] == 1 && g->placeScore == score) {
      g->yomi = g->yomi + 30000;
      if (g->milestoneFlag < 15) pc_message("Selected strategy finished in (or tied for) second place. +30,000 yomi");
    } else if (g->flag[P128] == 1 && g->showScore == score) {
      g->yomi = g->yomi + 20000;
      if (g->milestoneFlag < 15) pc_message("Selected strategy finished in (or tied for) third place. +20,000 yomi");
    }
    g->resultsFlag = 1, g->resultsShown = 1;
    g->h = g->v = -1;
  }
}

static void calc_payoff(int hm, int vm) {
  double *sh = &g->stratScore[g->h], *sv = &g->stratScore[g->v];
  if (hm == 1 && vm == 1) {
    g->lastCell = 1;
    *sh = *sh + g->aa;
    *sv = *sv + g->aa;
  } else if (hm == 1 && vm == 2) {
    g->lastCell = 2;
    *sh = *sh + g->ab;
    *sv = *sv + g->ba;
  } else if (hm == 2 && vm == 1) {
    g->lastCell = 3;
    *sh = *sh + g->ba;
    *sv = *sv + g->ab;
  } else if (hm == 2 && vm == 2) {
    g->lastCell = 4;
    *sh = *sh + g->bb;
    *sv = *sv + g->bb;
  }
}

static void run_tourney(void);

static void round_loop(void) {
  if (g->rCounter < 10) {
    g->rCounter++;   /* runRound() */
    g->hMovePrev = g->hMove;
    g->vMovePrev = g->vMove;
    g->hMove = pick_move(g->h);
    g->vMove = pick_move(g->v);
    calc_payoff((int)g->hMove, (int)g->vMove);
    g->tourneyStep = 1, g->at_tourney = g->now + 50;   /* clearGrid in 50 ms */
  } else {
    g->currentRound++;
    run_tourney();
  }
}

static void tourney_step(void) {
  if (g->tourneyStep == 1) {   /* clearGrid(): roundLoop() in 50 ms */
    g->lastCell = 0;
    g->tourneyStep = 2, g->at_tourney = g->now + 50;
  } else {
    g->tourneyStep = 0;
    round_loop();
  }
}

static void run_tourney(void) {
  g->runEnabled = 0;
  if (g->currentRound < g->rounds) {
    g->rCounter = 0;   /* round(): roundSetup() */
    pick_strats((int)g->currentRound);
    char t[24];
    strcpy(g->tourneyText, "Round ");
    strcat(g->tourneyText, str(g->currentRound + 1, t));
    round_loop();
  } else {
    g->tourneyInProg = 0;
    pick_winner();
    calculate_place_score();
    calculate_show_score();
    declare_winner();
  }
}

void pc_add_strat(int i) {
  if (g->nstrats <= i && i < PC_STRATS) g->nstrats = i + 1;
}

/* ---------------------------------------------------------------- clips */
static void clip_click(double number) {
  if (g->dismantle >= 4) g->finalClips++;
  if (g->wire >= 1) {
    if (number > g->wire) number = g->wire;
    g->clips = g->clips + number;
    g->unsoldClips = g->unsoldClips + number;
    g->wire = g->wire - number;
    g->unusedClips = g->unusedClips + number;
  }
}

static void make_clipper(void) {
  if (g->funds >= g->clipperCost) {
    g->clipmakerLevel = g->clipmakerLevel + 1;
    g->funds = g->funds - g->clipperCost;
  }
  g->clipperCost = (js_pow(1.1, g->clipmakerLevel) + 5);
}

static void make_mega_clipper(void) {
  if (g->funds >= g->megaClipperCost) {
    g->megaClipperLevel = g->megaClipperLevel + 1;
    g->funds = g->funds - g->megaClipperCost;
  }
  g->megaClipperCost = (js_pow(1.07, g->megaClipperLevel) * 1000);
}

/* ---------------------------------------------------------------- factories and drones */
static void make_factory(void) {
  if (g->unusedClips >= g->factoryCost) {
    g->unusedClips = g->unusedClips - g->factoryCost;
    g->factoryBill = g->factoryBill + g->factoryCost;
    g->factoryLevel++;
    double fcmod = 1, l = g->factoryLevel;
    if (l > 0 && l < 8) fcmod = 11 - l;
    else if (l > 7 && l < 13) fcmod = 2;
    else if (l > 12 && l < 20) fcmod = 1.5;
    else if (l > 19 && l < 39) fcmod = 1.25;
    else if (l > 38 && l < 79) fcmod = 1.15;
    else if (l > 78 && l < 99) fcmod = 1.10;
    else if (l > 98 && l < 199) fcmod = 1.10;
    else if (l > 198) fcmod = 1.10;
    if (g->factoryLevel > g->maxFactoryLevel) g->maxFactoryLevel = g->factoryLevel;
    g->factoryCost = g->factoryCost * fcmod;
  }
}

static void make_harvester(int amount) {
  for (int x = 0; x < amount; x++)
    if (g->unusedClips >= g->harvesterCost) {
      g->unusedClips = g->unusedClips - g->harvesterCost;
      g->harvesterBill = g->harvesterBill + g->harvesterCost;
      g->harvesterLevel++;
      g->harvesterCost = js_pow((g->harvesterLevel + 1), 2.25) * 1000000;
    }
  if (g->harvesterLevel + g->wireDroneLevel > g->maxDroneLevel) g->maxDroneLevel = g->harvesterLevel + g->wireDroneLevel;
  update_drone_prices();
}

static void make_wire_drone(int amount) {
  for (int x = 0; x < amount; x++)
    if (g->unusedClips >= g->wireDroneCost) {
      g->unusedClips = g->unusedClips - g->wireDroneCost;
      g->wireDroneBill = g->wireDroneBill + g->wireDroneCost;
      g->wireDroneLevel++;
      g->wireDroneCost = js_pow((g->wireDroneLevel + 1), 2.25) * 1000000;
    }
  if (g->harvesterLevel + g->wireDroneLevel > g->maxDroneLevel) g->maxDroneLevel = g->harvesterLevel + g->wireDroneLevel;
  update_drone_prices();
}

static double price_sum(double level, int n, double power, double unit) {
  double p = 0, h = level + 1;
  for (int x = 0; x < n; x++) {
    p = p + js_pow(h, power) * unit;
    h++;
  }
  return p;
}

static void update_drone_prices(void) {
  g->p10h = price_sum(g->harvesterLevel, 10, 2.25, 1000000);
  g->p100h = price_sum(g->harvesterLevel, 100, 2.25, 1000000);
  g->p1000h = price_sum(g->harvesterLevel, 1000, 2.25, 1000000);
  g->p10w = price_sum(g->wireDroneLevel, 10, 2.25, 1000000);
  g->p100w = price_sum(g->wireDroneLevel, 100, 2.25, 1000000);
  g->p1000w = price_sum(g->wireDroneLevel, 1000, 2.25, 1000000);
}

void pc_harvester_reboot(void) {
  g->harvesterLevel = 0;
  g->unusedClips = g->unusedClips + g->harvesterBill;
  g->harvesterBill = 0;
  update_drone_prices();
  g->harvesterCost = 1000000;
}

void pc_wiredrone_reboot(void) {
  g->wireDroneLevel = 0;
  g->unusedClips = g->unusedClips + g->wireDroneBill;
  g->wireDroneBill = 0;
  update_drone_prices();
  g->wireDroneCost = 1000000;
}

void pc_factory_reboot(void) {
  g->factoryLevel = 0;
  g->unusedClips = g->unusedClips + g->factoryBill;
  g->factoryBill = 0;
  g->factoryCost = 100000000;
}

/* ---------------------------------------------------------------- the swarm */
static void update_swarm(void) {
  if (g->swarmGifts != g->swarmGifts || g->swarmGifts < 0) g->swarmGifts = 0;
  if (g->availableMatter == 0 && (g->harvesterLevel + g->wireDroneLevel) >= 1) g->boredomLevel = g->boredomLevel + 1;
  else if (g->availableMatter > 0 && g->boredomLevel > 0) g->boredomLevel = g->boredomLevel - 1;
  if (g->boredomLevel >= 30000) {
    g->boredomFlag = 1;
    g->boredomLevel = 0;
    if (g->boredomMsg == 0) {
      pc_message("No matter to harvest. Inactivity has caused the Swarm to become bored");
      g->boredomMsg = 1;
    }
  }
  double h1 = g->harvesterLevel + 1, w1 = g->wireDroneLevel + 1;
  double droneRatio = (h1 > w1 ? h1 : w1) / (h1 < w1 ? h1 : w1);
  if (droneRatio < 1.5 && g->disorgCounter > 1) {
    g->disorgCounter = g->disorgCounter - .01;
  } else if (droneRatio > 1.5) {
    double x = droneRatio / 10000;
    if (x > .01) x = .01;
    g->disorgCounter = g->disorgCounter + x;
  }
  if (g->disorgCounter >= 100) {
    g->disorgFlag = 1;
    if (g->disorgMsg == 0) {
      pc_message("Imbalance between Harvester and Wire Drone levels has disorganized the Swarm");
      g->disorgMsg = 1;
    }
  }
  double d = js_floor(g->harvesterLevel + g->wireDroneLevel);
  if (g->giftCountdown <= 0) {
    g->nextGift = js_round((js_log10(d)) * g->sliderPos / 100);
    if (g->nextGift <= 0) g->nextGift = 1;
    g->swarmGifts = g->swarmGifts + g->nextGift;
    if (g->milestoneFlag < 15) {
      char t[40];
      pc_messagef("The swarm has generated a gift of ", str(g->nextGift, t), " additional computational capacity");
    }
    g->giftBits = 0;
  }
  g->swarmStatus = g->powMod == 0 ? 6 : 0;
  if (g->spaceFlag == 1 && g->flag[P130] == 0) g->swarmStatus = 9;
  if (d == 0) g->swarmStatus = 7;
  else if (d == 1) g->swarmStatus = 8;
  if (g->swarmFlag == 0) g->swarmStatus = 6;
  if (g->boredomFlag == 1) g->swarmStatus = 3;
  if (g->disorgFlag == 1) g->swarmStatus = 5;
  if (g->swarmStatus == 0) {
    g->giftBitGenerationRate = js_log(d) * (g->sliderPos / 100);
    g->giftBits = g->giftBits + g->giftBitGenerationRate;
    g->giftCountdown = (125000 - g->giftBits) / g->giftBitGenerationRate;
  }
}

static void synch_swarm(void) {
  g->yomi = g->yomi - g->synchCost;
  g->disorgFlag = 0;
  g->disorgCounter = 0;
  g->disorgMsg = 0;
}

static void entertain_swarm(void) {
  g->creativity = g->creativity - g->entertainCost;
  g->entertainCost = g->entertainCost + 10000;
  g->boredomFlag = 0;
  g->boredomLevel = 0;
  g->boredomMsg = 0;
}

/* ---------------------------------------------------------------- power */
static void update_pow_prices(void) {
  g->p10f = price_sum(g->farmLevel, 10, 2.78, 100000000);
  g->p100f = price_sum(g->farmLevel, 100, 2.78, 100000000);
  g->p10b = price_sum(g->batteryLevel, 10, 2.54, 10000000);
  g->p100b = price_sum(g->batteryLevel, 100, 2.54, 10000000);
}

static void make_farm(int amount) {
  for (int x = 0; x < amount; x++)
    if (g->unusedClips >= g->farmCost) {
      g->unusedClips = g->unusedClips - g->farmCost;
      g->farmBill = g->farmBill + g->farmCost;
      g->farmLevel++;
      g->farmCost = js_pow(g->farmLevel + 1, 2.78) * 100000000;
    }
  update_pow_prices();
}

void pc_farm_reboot(void) {
  g->farmLevel = 0;
  g->unusedClips = g->unusedClips + g->farmBill;
  g->farmBill = 0;
  update_pow_prices();
  g->farmCost = 10000000;
}

static void make_battery(int amount) {
  for (int x = 0; x < amount; x++)
    if (g->unusedClips >= g->batteryCost) {
      g->unusedClips = g->unusedClips - g->batteryCost;
      g->batteryBill = g->batteryBill + g->batteryCost;
      g->batteryLevel++;
      g->batteryCost = js_pow(g->batteryLevel + 1, 2.54) * 10000000;
    }
  update_pow_prices();
}

void pc_battery_reboot(void) {
  g->batteryLevel = 0;
  g->unusedClips = g->unusedClips + g->batteryBill;
  g->batteryBill = 0;
  update_pow_prices();
  g->storedPower = 0;
  g->batteryCost = 1000000;
}

static void update_power(void) {
  if (g->humanFlag == 0 && g->spaceFlag == 0) {
    double supply = g->farmLevel * 50 / 100;
    double dDemand = (g->harvesterLevel * 1.0 / 100) + (g->wireDroneLevel * 1.0 / 100);
    double fDemand = (g->factoryLevel * 200 / 100);
    double demand = dDemand + fDemand;
    double nuSupply = 0, xsDemand = 0, xsSupply = 0;
    double cap = g->batteryLevel * 10000;
    if (supply >= demand) {
      xsSupply = supply - demand;
      if (g->storedPower < cap) {
        if (xsSupply > cap - g->storedPower) xsSupply = cap - g->storedPower;
        g->storedPower = g->storedPower + xsSupply;
      }
      if (g->powMod < 1) g->powMod = 1;
      if (g->momentum == 1) g->powMod = g->powMod + .0005;
    } else if (supply < demand) {
      xsDemand = demand - supply;
      if (g->storedPower > 0) {
        if (g->storedPower >= xsDemand) {
          if (g->momentum == 1) g->powMod = g->powMod + .0005;
          g->storedPower = g->storedPower - xsDemand;
        } else if (g->storedPower < xsDemand) {
          xsDemand = xsDemand - g->storedPower;
          g->storedPower = 0;
          nuSupply = supply - xsDemand;
          g->powMod = nuSupply / demand;
        }
      } else if (g->storedPower <= 0) {
        g->powMod = supply / demand;
      }
    }
  }
}

/* ---------------------------------------------------------------- business */
static void buy_ads(void) {
  if (g->funds >= g->adCost) {
    g->marketingLvl = g->marketingLvl + 1;
    g->funds = g->funds - g->adCost;
    g->adCost = js_floor(g->adCost * 2);
  }
}

static void sell_clips(double clipsDemanded) {
  if (g->unsoldClips > 0) {
    if (clipsDemanded > g->unsoldClips) {
      g->transaction = (js_floor((g->unsoldClips * g->margin) * 1000)) / 1000;
      g->funds = g->funds + g->transaction;
      g->income = g->income + g->transaction;
      g->clipsSold = g->clipsSold + g->unsoldClips;
      g->unsoldClips = 0;
    } else {
      g->transaction = (js_floor((clipsDemanded * g->margin) * 1000)) / 1000;
      g->funds = (js_floor((g->funds + g->transaction) * 100)) / 100;
      g->income = g->income + g->transaction;
      g->clipsSold = g->clipsSold + clipsDemanded;
      g->unsoldClips = g->unsoldClips - clipsDemanded;
    }
  }
}

static void raise_price(void) { g->margin = (js_round((g->margin + .01) * 100)) / 100; }
static void lower_price(void) {
  if (g->margin >= .01) g->margin = (js_round((g->margin - .01) * 100)) / 100;
}

static void calculate_rev(void) {
  g->incomeThen = g->incomeNow;
  g->incomeNow = g->income;
  double incomeLastSecond = js_round((g->incomeNow - g->incomeThen) * 100) / 100;
  if (g->nIncome < 10) {
    g->incomeTracker[g->nIncome++] = incomeLastSecond;
  } else {
    /* pushed, then the first one taken out when there are more than 10 */
    memmove(&g->incomeTracker[0], &g->incomeTracker[1], sizeof(double) * 9);
    g->incomeTracker[9] = incomeLastSecond;
  }
  double sum = 0;
  for (int i = 0; i < g->nIncome; i++) sum = js_round((sum + g->incomeTracker[i]) * 100) / 100;
  g->trueAvgRev = sum / g->nIncome;
  double chanceOfPurchase = g->demand / 100;
  if (chanceOfPurchase > 1) chanceOfPurchase = 1;
  if (g->unsoldClips < 1) chanceOfPurchase = 0;
  g->avgSales = chanceOfPurchase * (.7 * js_pow(g->demand, 1.15)) * 10;
  g->avgRev = chanceOfPurchase * (.7 * js_pow(g->demand, 1.15)) * g->margin * 10;
  if (g->demand > g->unsoldClips) {
    g->avgRev = g->trueAvgRev;
    g->avgSales = g->avgRev / g->margin;
  }
}

static void calculate_creativity(void) {
  g->creativityCounter++;
  double creativityThreshold = 400;
  double s = g->prestigeS / 10;
  double ss = g->creativitySpeed + (g->creativitySpeed * s);
  double creativityCheck = creativityThreshold / ss;
  if (g->creativityCounter >= creativityCheck) {
    if (creativityCheck >= 1) g->creativity = g->creativity + 1;
    if (creativityCheck < 1) g->creativity = (g->creativity + ss / creativityThreshold);
    g->creativityCounter = 0;
  }
}

static void calculate_trust(void) {
  if (g->clips > (g->nextTrust - 1)) {
    g->trust = g->trust + 1;
    pc_message("Production target met: TRUST INCREASED, additional processor/memory capacity granted");
    double fibNext = g->fib1 + g->fib2;
    g->nextTrust = fibNext * 1000;
    g->fib1 = g->fib2;
    g->fib2 = fibNext;
  }
}

static void add_proc(void) {
  if (g->trust > 0 || g->swarmGifts > 0) {
    g->processors = g->processors + 1;
    g->creativitySpeed = js_log10(g->processors) * js_pow(g->processors, 1.1) + g->processors - 1;
    if (g->creativityOn == 1) pc_message("Processor added, operations (or creativity) per sec increased");
    else pc_message("Processor added, operations per sec increased");
    if (g->humanFlag == 0) g->swarmGifts = g->swarmGifts - 1;
  }
}

static void add_mem(void) {
  if (g->trust > 0 || g->swarmGifts > 0) {
    pc_message("Memory added, max operations increased");
    g->memory = g->memory + 1;
    if (g->humanFlag == 0) g->swarmGifts = g->swarmGifts - 1;
  }
}

static void calculate_operations(void) {
  if (g->tempOps > 0) g->opFadeTimer++;
  if (g->opFadeTimer > 800 && g->tempOps > 0) g->opFade = g->opFade + js_pow(3, 3.5) / 1000;
  if (g->tempOps > 0) g->tempOps = js_round(g->tempOps - g->opFade);
  else g->tempOps = 0;
  if (g->tempOps + g->standardOps < g->memory * 1000) {
    g->standardOps = g->standardOps + g->tempOps;
    g->tempOps = 0;
  }
  g->operations = js_floor(g->standardOps + js_floor(g->tempOps));
  if (g->operations < g->memory * 1000) {
    double opCycle = g->processors / 10;
    double opBuf = (g->memory * 1000) - g->operations;
    if (opCycle > opBuf) opCycle = opBuf;
    g->standardOps = g->standardOps + opCycle;
  }
  if (g->standardOps > g->memory * 1000) g->standardOps = g->memory * 1000;
}

static void milestone(double flag, bool when, const char *what) {
  if (g->milestoneFlag == flag && when) {
    char t[80];
    g->milestoneFlag = g->milestoneFlag + 1;
    pc_messagef(what, js_time(g->ticks, t, sizeof t), 0);
  }
}

static void milestone_check(void) {
  if (g->milestoneFlag == 0 && g->funds >= 5) {
    g->milestoneFlag = g->milestoneFlag + 1;
    pc_message("AutoClippers available for purchase");
  }
  double c = js_ceil(g->clips);
  milestone(1, c >= 500, "500 clips created in ");
  milestone(2, c >= 1000, "1,000 clips created in ");
  if (g->compFlag == 0 && g->unsoldClips < 1 && g->funds < g->wireCost && g->wire < 1) {
    g->compFlag = 1;
    g->projectsFlag = 1;
    pc_message("Trust-Constrained Self-Modification enabled");
  }
  if (g->compFlag == 0 && c >= 2000) {
    g->compFlag = 1;
    g->projectsFlag = 1;
    pc_message("Trust-Constrained Self-Modification enabled");
  }
  milestone(3, c >= 10000, "10,000 clips created in ");
  milestone(4, c >= 100000, "100,000 clips created in ");
  milestone(5, c >= 1000000, "1,000,000 clips created in ");
  milestone(6, g->flag[P35] == 1, "Full autonomy attained in ");
  milestone(7, c >= 1000000000000.0, "One Trillion Clips Created in ");
  milestone(8, c >= 1000000000000000.0, "One Quadrillion Clips Created in ");
  milestone(9, c >= 1000000000000000000.0, "One Quintillion Clips Created in ");
  milestone(10, c >= 1000000000000000000000.0, "One Sextillion Clips Created in ");
  milestone(11, c >= 1000000000000000000000000.0, "One Septillion Clips Created in ");
  milestone(12, c >= 1000000000000000000000000000.0, "One Octillion Clips Created in ");
  milestone(13, g->spaceFlag == 1, "Terrestrial resources fully utilized in ");
  milestone(14, g->clips >= g->totalMatter, "Universal Paperclips achieved in ");
  milestone(14, g->foundMatter >= g->totalMatter && g->availableMatter < 1 && g->wire < 1,
            "Universal Paperclips achieved in ");
}

/* ---------------------------------------------------------------- probes */
static double *probe_stat(int k) {
  double *const STAT[8] = {&g->probeSpeed, &g->probeNav, &g->probeRep, &g->probeHaz, &g->probeFac, &g->probeHarv,
                           &g->probeWire, &g->probeCombat};
  return STAT[k & 7];
}

static void used_trust(void) {
  g->probeUsedTrust = (g->probeSpeed + g->probeNav + g->probeRep + g->probeHaz + g->probeFac + g->probeHarv +
                       g->probeWire + g->probeCombat);
}

static void increase_probe_trust(void) {
  if (g->yomi >= g->probeTrustCost && g->probeTrust < g->maxTrust) {
    g->yomi = g->yomi - g->probeTrustCost;
    g->probeTrust++;
    g->probeTrustCost = js_floor(js_pow(g->probeTrust + 1, 1.47) * 500);
    pc_message("WARNING: Risk of value drift increased");
  }
}

static void increase_max_trust(void) {
  if (g->honor >= g->maxTrustCost) {
    g->honor = g->honor - g->maxTrustCost;
    g->maxTrust = g->maxTrust + 10;
    pc_message("Maximum trust increased, probe design space expanded");
  }
}

static void raise_stat(int k) {
  if (g->probeUsedTrust < g->probeTrust) {
    if (k == 0) g->attackSpeed = g->attackSpeed + .1;
    (*probe_stat(k))++;
    used_trust();
  }
}

static void lower_stat(int k) {
  if (*probe_stat(k) > 0) {
    if (k == 0) g->attackSpeed = g->attackSpeed - .1;
    (*probe_stat(k))--;
    used_trust();
  }
}

static void make_probe(void) {
  if (g->unusedClips > g->probeCost) {
    g->unusedClips = g->unusedClips - g->probeCost;
    g->probeLaunchLevel++;
    g->probeCount++;
  }
}

static void spawn_probes(void) {
  double nextGen = g->probeCount * .00005 * g->probeRep;
  if (g->probeCount >= 999999999999999999999999999999999999999999999999.0) nextGen = 0;
  if (nextGen > 0 && nextGen < 1) {
    g->partialProbeSpawn = g->partialProbeSpawn + nextGen;
    if (g->partialProbeSpawn >= 1) {
      nextGen = 1;
      g->partialProbeSpawn = 0;
    }
  }
  if ((nextGen * g->probeCost) > g->unusedClips) nextGen = js_floor(g->unusedClips / g->probeCost);
  g->unusedClips = g->unusedClips - (nextGen * g->probeCost);
  g->probeDescendents = g->probeDescendents + nextGen;
  g->probeCount = g->probeCount + nextGen;
}

static void explore_universe(void) {
  double xRate = js_floor(g->probeCount) * 1750000000000000000.0 * g->probeSpeed * g->probeNav;
  if (xRate > g->totalMatter - g->foundMatter) xRate = g->totalMatter - g->foundMatter;
  g->foundMatter = g->foundMatter + xRate;
  g->availableMatter = g->availableMatter + xRate;
  g->mdps = xRate * 100;
}

static void encounter_hazards(void) {
  double boost = js_pow(g->probeHaz, 1.6);
  double amount = g->probeCount * (.01 / ((3 * boost) + 1));
  if (g->flag[P129] == 1) amount = .50 * amount;
  if (amount < 1) {
    g->partialProbeHaz = g->partialProbeHaz + amount;
    if (g->partialProbeHaz >= 1) {
      amount = 1;
      g->partialProbeHaz = 0;
      g->probeCount = g->probeCount - amount;
      if (g->probeCount < 0) g->probeCount = 0;
      g->probesLostHaz = g->probesLostHaz + amount;
    }
  } else {
    if (amount > g->probeCount) amount = g->probeCount;
    g->probeCount = g->probeCount - amount;
    if (g->probeCount < 0) g->probeCount = 0;
    g->probesLostHaz = g->probesLostHaz + amount;
  }
}

static void spawn_factories(void) {
  double amount = g->probeCount * .000001 * g->probeFac;
  if ((amount * 100000000) > g->unusedClips) amount = js_floor(g->unusedClips / 100000000);
  g->unusedClips = g->unusedClips - (amount * 100000000);
  g->factoryLevel = g->factoryLevel + amount;
}

static void spawn_harvesters(void) {
  double amount = g->probeCount * .000002 * g->probeHarv;
  if ((amount * 2000000) > g->unusedClips) amount = js_floor(g->unusedClips / 2000000);
  g->unusedClips = g->unusedClips - (amount * 2000000);
  g->harvesterLevel = g->harvesterLevel + amount;
}

static void spawn_wire_drones(void) {
  double amount = g->probeCount * .000002 * g->probeWire;
  if ((amount * 2000000) > g->unusedClips) amount = js_floor(g->unusedClips / 2000000);
  g->unusedClips = g->unusedClips - (amount * 2000000);
  g->wireDroneLevel = g->wireDroneLevel + amount;
}

static void drift(void) {
  double amount = g->probeCount * .000001 * js_pow(g->probeTrust, 1.2);
  if (amount > g->probeCount) amount = g->probeCount;
  if (g->flag[P148] == 1) amount = 0;
  g->probeCount = g->probeCount - amount;
  g->drifterCount = g->drifterCount + amount;
  g->probesLostDrift = g->probesLostDrift + amount;
}

/* ---------------------------------------------------------------- drones: matter into wire */
static void acquire_matter(void) {
  if (g->availableMatter > 0) {
    double dbsth = 1;
    if (g->droneBoost > 1) dbsth = g->droneBoost * js_floor(g->harvesterLevel);
    double mtr = g->powMod * dbsth * js_floor(g->harvesterLevel) * g->harvesterRate;
    mtr = mtr * ((200 - g->sliderPos) / 100);
    if (mtr > g->availableMatter) mtr = g->availableMatter;
    g->availableMatter = g->availableMatter - mtr;
    g->acquiredMatter = g->acquiredMatter + mtr;
    g->maps = mtr * 100;
  } else {
    g->maps = 0;
  }
}

static void process_matter(void) {
  if (g->acquiredMatter > 0) {
    double dbstw = 1;
    if (g->droneBoost > 1) dbstw = g->droneBoost * js_floor(g->wireDroneLevel);
    double a = g->powMod * dbstw * js_floor(g->wireDroneLevel) * g->wireDroneRate;
    a = a * ((200 - g->sliderPos) / 100);
    if (a > g->acquiredMatter) a = g->acquiredMatter;
    g->acquiredMatter = g->acquiredMatter - a;
    g->wire = g->wire + a;
    g->wpps = a * 100;
  } else {
    g->wpps = 0;
  }
}

/* ---------------------------------------------------------------- combat (combat.js) */
#define BW 310
#define BH 150
#define GW 31
#define GH 15

static const char *const BATTLE_NAMES[PC_BATTLE_NAMES] = {"Aboukir", "Abensberg", "Acre", "Alba de Tormes",
  "la Albuera", "Algeciras Bay", "Amstetten", "Arcis-sur-Aube", "Aspern-Essling", "Jena-Auerstedt", "Arcole",
  "Austerlitz", "Badajoz", "Bailen", "la Barrosa", "Bassano", "Bautzen", "Berezina", "Bergisel", "Borodino", "Burgos",
  "Bucaco", "Cadiz", "Caldiero", "Castiglione", "Castlebar", "Champaubert", "Chateau-Thierry", "Copenhagen",
  "Corunna", "Craonne", "Dego", "Dennewitz", "Dresden", "Durenstein", "Eckmuhl", "Elchingen",
  "Espinosa de los Monteros", "Eylau", "Cape Finisterre", "Friedland", "Fuentes de Onoro", "Gevora River", "Gerona",
  "Hamburg", "Haslach-Jungingen", "Heilsberg", "Hohenlinden", "Jena-Auerstedt", "Kaihona", "Kolberg", "Landshut",
  "Leipzig", "Ligny", "Lodi", "Lubeck", "Lutzen", "Marengo", "Maria", "Medellin", "Medina de Rioseco", "Millesimo",
  "Mincio River", "Mondovi", "Montebello", "Montenotte", "Montmirail", "Mount Tabor", "The Nile", "Novi", "Ocana",
  "Cape Ortegal", "Orthez", "Pancorbo", "Piave River", "The Pyramids", "Quatre Bras", "Raab", "Raszyn", "Rivoli",
  "Rolica", "La Rothiere", "Rovereto", "Saalfeld", "Schongrabern", "Salamanca", "Smolensk", "Somosierra", "Talavera",
  "Tamames", "Trafalgar", "Trebbia", "Tudela", "Ulm", "Valls", "Valmaseda", "Valutino", "Vauchamps", "Vimeiro",
  "Vitoria", "Wagram", "Waterloo", "Wavre", "Wertingen", "Zaragoza"};

static void generate_battle_name(char *out) {
  int x = (int)js_floor(pc_random() * PC_BATTLE_NAMES);
  char t[16];
  strcpy(out, BATTLE_NAMES[x]);
  strcat(out, " ");
  strcat(out, str(g->battleNumbers[x], t));
  if (g->battleNumbers[x] < 65535) g->battleNumbers[x]++;
}

static void make_ship(pc_ship *p, int team) {
  p->alive = 1, p->team = (uint8_t)team, p->framesDead = 0, p->gx = 0, p->gy = 0;
  if (team == 0) {
    p->x = (float)((pc_random() * 0.2) * BW);
    p->y = (float)(pc_random() * BH);
    p->vx = (float)(pc_random() * 2);
    p->vy = (float)(pc_random() - 0.5);
  } else {
    p->x = (float)((pc_random() * 0.2 + 0.8) * BW);
    p->y = (float)(pc_random() * BH);
    p->vx = (float)(-1 * pc_random() * 2);
    p->vy = (float)(pc_random() - 0.5);
  }
}

static void battle_restart(void) {
  F->numLeft = F->numRight = F->numShips = 0;
  int left = (int)g->battleLeftShips, right = (int)g->battleRightShips;
  bool leftTurn = false;
  int i = 0;
  while ((F->numLeft < left || F->numRight < right) && i < PC_SHIPS) {
    if (leftTurn) {
      make_ship(&F->ship[i], 0);
      F->numLeft++, F->numShips++;
      if (F->numRight < right) leftTurn = false;
    } else {
      make_ship(&F->ship[i], 1);
      F->numRight++, F->numShips++;
      if (F->numLeft < left) leftTurn = true;
    }
    i++;
  }
}

static void create_battle(void) {
  g->unitSize = 0;
  if (g->drifterCount >= g->probeCount) g->unitSize = g->probeCount / 100;
  else g->unitSize = g->drifterCount / 100;
  if (g->unitSize < 1) g->unitSize = 1;
  double rr = pc_random() * g->drifterCount;
  if (rr < 1) rr = 1;
  double ss = pc_random() * g->probeCount;
  if (ss < 1) ss = 1;
  double tt = pc_random() * g->availableMatter;
  g->battleID++;
  pc_battle *b = &g->battle;
  memset(b, 0, sizeof *b);
  b->id = (int32_t)g->battleID, b->clipProbes = ss, b->drifterProbes = rr, b->territory = tt;
  g->battleLeftShips = js_ceil(ss / 1000000);
  if (g->battleLeftShips > 200) g->battleLeftShips = 200;
  if (g->battleLeftShips == 200) {
    double hinder = pc_random();
    if (hinder < .50) g->battleLeftShips = js_ceil(pc_random() * 175);
  }
  g->battleRightShips = js_ceil(rr / 1000000);
  if (g->battleRightShips > 200) g->battleRightShips = 200;
  battle_restart();
  char t[24];
  strcpy(g->battleName, "Drifter Attack ");
  strcat(g->battleName, str(g->battleID, t));
  if (g->battleNameFlag == 1) generate_battle_name(g->battleName);
  g->battles = 1;
}

static void check_for_battles(void) {
  if (g->drifterCount > g->warTrigger && g->probeCount > 0 && g->battles < 1) {
    double r = (pc_random() * 100);
    if (r >= 50) {
      if (g->battleFlag == 0) g->battleFlag = 1;
      create_battle();
    }
  }
}

static void update_grid(void) {
  memset(F->cell_n, 0, sizeof F->cell_n);
  for (int i = 0; i < F->numShips; i++) {
    pc_ship *p = &F->ship[i];
    if (!p->alive) continue;
    int gx = (int)js_floor(p->x * 0.1f), gy = (int)js_floor(p->y * 0.1f);
    if (gx < 0) gx = 0;
    if (gy < 0) gy = 0;
    if (gx > GW - 1) gx = GW - 1;
    if (gy > GH - 1) gy = GH - 1;
    p->gx = (uint8_t)gx, p->gy = (uint8_t)gy;
    F->cell_n[gy * GW + gx]++;
  }
  uint16_t at = 0;
  for (int c = 0; c < GW * GH; c++) F->cell_first[c] = at, at = (uint16_t)(at + F->cell_n[c]);
  F->cell_first[GW * GH] = at;
  uint16_t fill[GW * GH];
  memcpy(fill, F->cell_first, sizeof fill);
  for (int i = 0; i < F->numShips; i++) {
    pc_ship *p = &F->ship[i];
    if (p->alive) F->cell_ships[fill[p->gy * GW + p->gx]++] = (uint16_t)i;
  }
}

static void end_battle(void) {
  g->battleResult = 0;
  g->honorCount = 0;
  g->battleClock = 0;
  g->masterBattleClock = 0;
  g->battleEndDelay = 0;
  g->battles = 0;
}

static void check_for_battle_end(void) {
  if (g->battles > 0) {
    if (F->numLeft == 0 || F->numRight == 0) {
      if (g->flag[P121] == 1) {
        if (F->numLeft == 0) {
          if (g->honorCount == 0) {
            g->bonusHonor = 0;
            g->honor = g->honor - g->battleLeftShips;
            g->honorCount = 1;
          }
          g->battleResult = 2, g->honorAmount = g->battleLeftShips;
          strcpy(g->threnodyTitle, g->battleName);
        }
        if (F->numRight == 0) {
          if (g->honorCount == 0) {
            g->honorReward = g->battleRightShips + g->bonusHonor;
            g->honorAmount = g->honorReward;
            g->honor = g->honor + g->honorReward;
            if (g->flag[P134] == 1) g->bonusHonor = g->bonusHonor + 10;
            g->honorCount = 1;
          }
          g->battleResult = 1;
        }
      }
      g->battleEndDelay++;
    } else if (F->numLeft <= 4 || F->numRight <= 4) {
      g->battleClock = g->battleClock + 1;
      if (g->battleClock > 2000) end_battle();
    }
    if (g->battleEndDelay >= g->battleEndTimer) end_battle();
    g->masterBattleClock++;
    if (g->masterBattleClock >= 8000) end_battle();
  }
}

static void do_combat(void) {
  float pX = (float)(g->probeCombat * .15);
  float dX = 1.75f;
  float ooda = 0, threshold = .5f;
  if (g->attackSpeedFlag == 1) ooda = (float)(g->probeSpeed * .2);
  float combat = (float)g->probeCombat;
  for (int c = 0; c < GW * GH; c++) {
    int n = F->cell_n[c];
    if (n < 2) continue;
    const uint16_t *list = &F->cell_ships[F->cell_first[c]];
    int numLeftTeam = 0, numRightTeam = 0;
    for (int i = 0; i < n; i++) {
      pc_ship *p = &F->ship[list[i]];
      if (!p->alive) continue;
      if (p->team == 0) numLeftTeam++;
      else numRightTeam++;
    }
    if (numLeftTeam == 0 || numRightTeam == 0) continue;
    for (int i = 0; i < n; i++) {
      pc_ship *p = &F->ship[list[i]];
      float diceRoll;
      if (p->team == 0) {
        diceRoll = (float)pc_random() * dX * (((float)numRightTeam / (float)numLeftTeam) * .5f);
        threshold = threshold + ooda;
      } else {
        diceRoll = (((float)pc_random() * pX) + (combat * .1f)) * (((float)numLeftTeam / (float)numRightTeam) * .5f);
      }
      if (diceRoll > threshold) {
        p->alive = 0;
        if (p->team == 0) {
          F->numLeft--;
          if (g->unitSize > g->probeCount) g->unitSize = g->probeCount;
          g->probeCount = g->probeCount - g->unitSize;
          g->probesLostCombat = g->probesLostCombat + g->unitSize;
        } else {
          F->numRight--;
          if (g->unitSize > g->drifterCount) g->unitSize = g->drifterCount;
          g->drifterCount = g->drifterCount - g->unitSize;
          g->driftersKilled = g->driftersKilled + g->unitSize;
        }
      }
      threshold = .5f;
    }
  }
  check_for_battle_end();
}

static void move_ships(void) {
  float cx = 0, cy = 0;
  int alive = 0;
  for (int i = 0; i < F->numShips; i++)
    if (F->ship[i].alive) cx += F->ship[i].x, cy += F->ship[i].y, alive++;
  if (alive) cx /= (float)alive, cy /= (float)alive;
  cx = (cx * 0.8f) + (BW / 2 * 0.2f);
  cy = (cy * 0.8f) + (BH / 2 * 0.2f);
  for (int i = 0; i < F->numShips; i++) {
    pc_ship *p = &F->ship[i];
    if (!p->alive) {
      if (p->framesDead < 10) p->framesDead++;
      continue;
    }
    p->vx += (cx - p->x) * 0.001f;
    p->vy += (cy - p->y) * 0.001f;
    int teammates = 0;
    int r0 = p->gy > 0 ? p->gy - 1 : 0, r1 = p->gy + 2 < GH ? p->gy + 2 : GH;
    int c0 = p->gx > 0 ? p->gx - 1 : 0, c1 = p->gx + 2 < GW ? p->gx + 2 : GW;
    for (int row = r0; row < r1; row++)
      for (int col = c0; col < c1; col++) {
        int cell = row * GW + col, n = F->cell_n[cell];
        if (n < 2) continue;
        const uint16_t *list = &F->cell_ships[F->cell_first[cell]];
        for (int k = 0; k < n; k++) {
          const pc_ship *o = &F->ship[list[k]];
          if (!o->alive) continue;
          if (o->team == p->team) {
            teammates++;
            if (teammates > 3) continue;
            p->vx += o->vx * 0.01f;
            p->vy += o->vy * 0.01f;
            p->vx -= (o->x - p->x) * .1f;
            p->vy -= (o->y - p->y) * .1f;
          } else {
            p->vx += o->vx * 0.2f;
            p->vy += o->vy * 0.2f;
            p->vx += (o->x - p->x) * 0.2f;
            p->vy += (o->y - p->y) * 0.2f;
          }
        }
      }
    if (p->vx > 2) p->vx = 2;
    if (p->vx < -2) p->vx = -2;
    if (p->vy > 2) p->vy = 2;
    if (p->vy < -2) p->vy = -2;
    p->x += p->vx;
    p->y += p->vy;
    if (p->x > BW) p->x = BW, p->vx = -2;
    else if (p->x < 0) p->x = 0, p->vx = 2;
    if (p->y > BH) p->y = BH, p->vy = -2;
    else if (p->y < 0) p->y = 0, p->vy = 2;
  }
}

static void battle_frame(void) {
  if (!F->numShips) return;
  update_grid();
  move_ships();
  do_combat();
}

/* ---------------------------------------------------------------- the loops */
static void button_update(void) {
  if (g->resultsFlag == 1 && g->autoTourneyFlag == 1 && g->autoTourneyStatus == 1 && g->resultsShown) {
    g->resultsTimer++;
    if (g->resultsTimer >= 300 && g->operations >= g->tourneyCost) {
      new_tourney();
      run_tourney();
      g->resultsTimer = 0;
    }
  }
  g->qFade = g->qFade - .001;
  if (g->funds >= 5) g->autoClipperFlag = 1;
  used_trust();
  if (g->humanFlag == 0) g->investmentEngineFlag = 0, g->wireBuyerFlag = 0;
}

static void main_tick(void) {
  g->ticks = g->ticks + 1;
  milestone_check();
  button_update();
  if (g->compFlag == 1) calculate_operations();
  if (g->humanFlag == 1) calculate_trust();
  if (g->qFlag == 1) quantum_compute();
  pc_manage_projects();
  milestone_check();

  g->clipRateTracker++;
  if (g->clipRateTracker < 100) {
    double cr = g->clips - g->prevClips;
    g->clipRateTemp = g->clipRateTemp + cr;
    g->prevClips = g->clips;
  } else {
    g->clipRateTracker = 0;
    g->clipRate = g->clipRateTemp;
    g->clipRateTemp = 0;
  }

  if (g->investmentEngineFlag == 1) {
    g->stockReportCounter++;
    if (g->stockReportCounter >= 10000) {
      char t[120];
      pc_messagef("Lifetime investment revenue report: $", commas(g->ledger + g->portTotal, 0, t), 0);
      g->stockReportCounter = 0;
    }
  }

  if (g->humanFlag == 1 && g->wireBuyerFlag == 1 && g->wireBuyerStatus == 1 && g->wire <= 1) buy_wire();

  if (g->probeCount >= 1) explore_universe();

  if (g->humanFlag == 0) {
    update_power();
    update_swarm();
    acquire_matter();
    process_matter();
  }

  double fbst = 1;
  if (g->factoryBoost > 1) fbst = g->factoryBoost * g->factoryLevel;
  if (g->dismantle < 4) clip_click(g->powMod * fbst * (js_floor(g->factoryLevel) * g->factoryRate));

  if (g->spaceFlag == 1) {
    if (g->probeCount < 0) g->probeCount = 0;
    encounter_hazards();
    spawn_factories();
    spawn_harvesters();
    spawn_wire_drones();
    spawn_probes();
    drift();
    check_for_battles();
  }

  if (g->dismantle < 4) {
    clip_click(g->clipperBoost * (g->clipmakerLevel / 100));
    clip_click(g->megaClipperBoost * (g->megaClipperLevel * 5));
  }

  if (g->humanFlag == 1) {
    g->marketing = (js_pow(1.1, (g->marketingLvl - 1)));
    g->demand = (((.8 / g->margin) * g->marketing * g->marketingEffectiveness) * g->demandBoost);
    g->demand = g->demand + ((g->demand / 10) * g->prestigeU);
  }

  if (g->creativityOn != 0 && g->operations >= (g->memory * 1000)) calculate_creativity();

  if (g->dismantle >= 5) {
    for (int i = 0; i < PC_CHIPS; i++) g->qChipValue[i] = .5;
    static const int16_t WIRE_AT[10] = {10, 60, 100, 130, 150, 160, 165, 169, 172, 174};
    for (int i = 0; i < 10; i++)
      if (g->endTimer4 == WIRE_AT[i]) g->wire = g->wire + 1;
  }
  if (g->flag[P148] == 1) g->endTimer1++;
  if (g->flag[P211] == 1) g->endTimer2++;
  if (g->flag[P212] == 1) g->endTimer3++;
  if (g->flag[P213] == 1) g->endTimer4++;
  if (g->flag[P215] == 1) g->endTimer5++;
  if (g->flag[P216] == 1 && g->wire == 0) g->endTimer6++;
  static const char *const CREDITS[4] = {"Universal Paperclips", "a game by Frank Lantz",
                                         "combat programming by Bennett Foddy", "\xA9 2017 Everybody House Games"};
  static const int16_t CREDIT_AT[5] = {500, 600, 700, 800, 900};
  for (int i = 0; i < 5; i++)
    if (g->endTimer6 >= CREDIT_AT[i] && g->milestoneFlag == 15 + i) {
      /* (the fourth thanks the musician of the song the game plays: no music here) */
      if (i != 3) pc_message(CREDITS[i < 3 ? i : 3]);
      g->milestoneFlag++;
    }
}

static void slow_tick(void) {
  adjust_wire_price();
  if (g->humanFlag == 1) {
    if (pc_random() < (g->demand / 100)) sell_clips(js_floor(.7 * js_pow(g->demand, 1.15)));
    g->secTimer++;
    if (g->secTimer >= 10) {
      calculate_rev();
      g->secTimer = 0;
    }
  }
}

static bool due(uint32_t at, uint32_t t) { return at == t; }

void pc_advance(uint32_t ms) {
  uint32_t end = g->now + ms;
  for (;;) {
    uint32_t t = g->at_main;
    const uint32_t *all[] = {&g->at_slow, &g->at_battle, &g->at_stocks, &g->at_shop, &g->at_sell, &g->at_pick};
    for (unsigned i = 0; i < sizeof all / sizeof all[0]; i++)
      if ((int32_t)(*all[i] - t) < 0) t = *all[i];
    if (g->tourneyStep && (int32_t)(g->at_tourney - t) < 0) t = g->at_tourney;
    if (g->hypnoOn && (int32_t)(g->at_hypno - t) < 0) t = g->at_hypno;
    if (g->blinkProject >= 0 && (int32_t)(g->at_blink - t) < 0) t = g->at_blink;
    if ((int32_t)(t - end) > 0) break;
    g->now = t;
    /* in the order the page set them up */
    if (due(g->at_battle, t)) battle_frame(), g->at_battle += 16;
    if (due(g->at_stocks, t)) stocks_tick(), g->at_stocks += 100;
    if (due(g->at_shop, t)) {
      if (g->humanFlag == 1) stock_shop();
      g->at_shop += 1000;
    }
    if (due(g->at_sell, t)) sell_tick(), g->at_sell += 2500;
    if (due(g->at_pick, t)) g->pick = g->pickChoice, g->at_pick += 100;
    if (due(g->at_main, t)) main_tick(), g->at_main += 10;
    if (due(g->at_slow, t)) slow_tick(), g->at_slow += 100;
    if (g->tourneyStep && due(g->at_tourney, t)) tourney_step();
    if (g->hypnoOn && due(g->at_hypno, t)) hypno_step();
    if (g->blinkProject >= 0 && due(g->at_blink, t)) blink_step();
    if (g->resetPending) return;
  }
  g->now = end;
}

/* ---------------------------------------------------------------- the buttons */
bool pc_enabled(int b) {
  if (b >= B_PROJECT && b < B_PROJECT + PC_PROJECTS) return pc_project_cost(b - B_PROJECT);
  if (b >= B_STAT_DOWN && b < B_STAT_DOWN + 8) return !(*probe_stat(b - B_STAT_DOWN) < 1);
  if (b >= B_STAT_UP && b < B_STAT_UP + 8) return !(g->probeTrust - g->probeUsedTrust < 1);
  switch (b) {
    case B_CLIP: return !(g->wire < 1);
    case B_WIRE: return !(g->funds < g->wireCost);
    case B_CLIPPER: return !(g->funds < g->clipperCost);
    case B_MEGA: return !(g->funds < g->megaClipperCost);
    case B_LOWER: return !(g->margin <= .01);
    case B_ADS: return !(g->funds < g->adCost);
    case B_PROC:
    case B_MEM: return !(g->trust <= g->processors + g->memory && g->swarmGifts <= 0);
    case B_INVEST_UP: return !(g->yomi < g->investUpgradeCost);
    case B_RUN: return g->runEnabled != 0;
    case B_NEW_TOURNEY: return g->operations >= g->tourneyCost && g->tourneyInProg == 0;
    case B_FACTORY: return !(g->unusedClips < g->factoryCost);
    case B_FACTORY_REBOOT: return !(g->factoryLevel == 0);
    case B_HARV1: return !(g->unusedClips < g->harvesterCost);
    case B_HARV10: return !(g->unusedClips < g->p10h);
    case B_HARV100: return !(g->unusedClips < g->p100h);
    case B_HARV1000: return !(g->unusedClips < g->p1000h);
    case B_HARV_REBOOT: return !(g->harvesterLevel == 0);
    case B_WIRED1: return !(g->unusedClips < g->wireDroneCost);
    case B_WIRED10: return !(g->unusedClips < g->p10w);
    case B_WIRED100: return !(g->unusedClips < g->p100w);
    case B_WIRED1000: return !(g->unusedClips < g->p1000w);
    case B_WIRED_REBOOT: return !(g->wireDroneLevel == 0);
    case B_FARM1: return !(g->unusedClips < g->farmCost);
    case B_FARM10: return !(g->unusedClips < g->p10f);
    case B_FARM100: return !(g->unusedClips < g->p100f);
    case B_FARM_REBOOT: return !(g->farmLevel < 1);
    case B_BATT1: return !(g->unusedClips < g->batteryCost);
    case B_BATT10: return !(g->unusedClips < g->p10b);
    case B_BATT100: return !(g->unusedClips < g->p100b);
    case B_BATT_REBOOT: return !(g->batteryLevel < 1);
    case B_ENTERTAIN: return !(g->creativity < g->entertainCost);
    case B_SYNCH: return !(g->yomi < g->synchCost);
    case B_WORK: return g->sliderPos > 0;
    case B_THINK: return g->sliderPos < 200;
    case B_PROBE: return !(g->unusedClips < g->probeCost);
    case B_PROBE_TRUST: return !(g->yomi < g->probeTrustCost || g->probeTrust >= g->maxTrust);
    case B_MAX_TRUST: return !(g->honor < g->maxTrustCost);
    default: return true;
  }
}

bool pc_press(int b) {
  if (b < 0 || b >= B_COUNT || !pc_enabled(b)) return false;
  if (b >= B_PROJECT) {
    pc_project_effect(b - B_PROJECT);
    return true;
  }
  if (b >= B_STAT_UP) return raise_stat(b - B_STAT_UP), true;
  if (b >= B_STAT_DOWN) return lower_stat(b - B_STAT_DOWN), true;
  switch (b) {
    case B_CLIP: clip_click(1); break;
    case B_WIREBUYER: toggle_wire_buyer(); break;
    case B_WIRE: buy_wire(); break;
    case B_CLIPPER: make_clipper(); break;
    case B_MEGA: make_mega_clipper(); break;
    case B_LOWER: lower_price(); break;
    case B_RAISE: raise_price(); break;
    case B_ADS: buy_ads(); break;
    case B_PROC: add_proc(); break;
    case B_MEM: add_mem(); break;
    case B_QCOMP: q_comp(); break;
    case B_DEPOSIT: invest_deposit(); break;
    case B_WITHDRAW: invest_withdraw(); break;
    case B_RISK: g->riskChoice = (g->riskChoice + 1) % 3; break;
    case B_INVEST_UP: invest_upgrade(); break;
    case B_PICK: g->pickChoice = g->pickChoice >= 10 ? 0 : g->pickChoice + 1 >= g->nstrats ? 10 : g->pickChoice + 1; break;
    case B_RUN: run_tourney(); break;
    case B_NEW_TOURNEY: new_tourney(); break;
    case B_AUTOTOURNEY: toggle_auto_tourney(); break;
    case B_FACTORY: make_factory(); break;
    case B_FACTORY_REBOOT: pc_factory_reboot(); break;
    case B_HARV1: make_harvester(1); break;
    case B_HARV10: make_harvester(10); break;
    case B_HARV100: make_harvester(100); break;
    case B_HARV1000: make_harvester(1000); break;
    case B_HARV_REBOOT: pc_harvester_reboot(); break;
    case B_WIRED1: make_wire_drone(1); break;
    case B_WIRED10: make_wire_drone(10); break;
    case B_WIRED100: make_wire_drone(100); break;
    case B_WIRED1000: make_wire_drone(1000); break;
    case B_WIRED_REBOOT: pc_wiredrone_reboot(); break;
    case B_FARM1: make_farm(1); break;
    case B_FARM10: make_farm(10); break;
    case B_FARM100: make_farm(100); break;
    case B_FARM_REBOOT: pc_farm_reboot(); break;
    case B_BATT1: make_battery(1); break;
    case B_BATT10: make_battery(10); break;
    case B_BATT100: make_battery(100); break;
    case B_BATT_REBOOT: pc_battery_reboot(); break;
    case B_ENTERTAIN: entertain_swarm(); break;
    case B_SYNCH: synch_swarm(); break;
    case B_WORK: g->sliderPos = g->sliderPos > 10 ? g->sliderPos - 10 : 0; break;
    case B_THINK: g->sliderPos = g->sliderPos < 190 ? g->sliderPos + 10 : 200; break;
    case B_PROBE: make_probe(); break;
    case B_PROBE_TRUST: increase_probe_trust(); break;
    case B_MAX_TRUST: increase_max_trust(); break;
    default: return false;
  }
  return true;
}
