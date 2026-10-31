/* Universal Paperclips: the whole game's state and rules (sim.c, projects.c), with the web build's own numbers,
 * timers and randomness. The names follow the game's JavaScript (main.js, projects.js, combat.js, globals.js) so
 * each line can be checked against it; numbers are doubles, as there.
 *
 * Time: pc_advance() runs the game's timers by the millisecond: the 10 ms main loop, the 100 ms slow loop, the
 * battle's 16 ms frames, the stock market's 100 ms, 1 s and 2.5 s, and the tournaments' 50 ms steps. */
#ifndef PC_H
#define PC_H
#include <stdbool.h>
#include <stdint.h>

#define PC_PROJECTS 96
#define PC_STRATS 8
#define PC_STOCKS 5
#define PC_CHIPS 10
#define PC_BATTLE_NAMES 105
#define PC_MSG 5
#define PC_MSG_LEN 200
#define PC_NAME_LEN 40
#define PC_SHIPS 400

typedef struct {
  double price, amount, total, profit, age;
  char symbol[8];
} pc_stock;

typedef struct {
  double clipProbes, drifterProbes, territory, reportCount;
  int32_t id, victory, loss, whiteFlag, garbageFlag, pad;
} pc_battle;

/* Everything that is kept: the save is this struct (save.c). Doubles first, then 32-bit numbers, then bytes, so it
 * lays out the same on the calculator and on computers. */
typedef struct {
  /* ---- doubles: globals.js and main.js */
  double clips, unusedClips, clipRate, clipRateTemp, prevClips, clipRateTracker, clipmakerLevel, clipperCost,
      unsoldClips, funds, margin, wire, wireCost, adCost, demand, clipsSold, avgRev, avgSales, income, ticks,
      marketing, marketingLvl, processors, memory, operations, trust, nextTrust, transaction, clipperBoost,
      creativity, creativityOn, boostLvl, wirePurchase, wireSupply, marketingEffectiveness, milestoneFlag, bankroll,
      fib1, fib2, strategyEngineFlag, investmentEngineFlag, revPerSecFlag, compFlag, projectsFlag, autoClipperFlag,
      megaClipperFlag, megaClipperCost, megaClipperLevel, megaClipperBoost, creativitySpeed, creativityCounter,
      wireBuyerFlag, demandBoost, humanFlag, nanoWire, wireProductionFlag, spaceFlag, factoryFlag, harvesterFlag,
      wireDroneFlag, factoryLevel, factoryBoost, droneBoost, availableMatter, acquiredMatter, harvesterLevel,
      wireDroneLevel, factoryCost, harvesterCost, wireDroneCost, factoryRate, harvesterRate, wireDroneRate,
      harvesterBill, wireDroneBill, factoryBill, probeCount, totalMatter, foundMatter, qFlag, qClock, qChipCost,
      nextQchip, bribe, battleFlag, prestigeU, prestigeS, autoTourneyFlag, tothFlag, wirePriceCounter,
      wireBasePrice, farmLevel, batteryLevel, farmCost, batteryCost, storedPower, powMod, farmBill, batteryBill,
      momentum, swarmFlag, swarmStatus, swarmGifts, nextGift, giftCountdown, giftBits, giftBitGenerationRate, honor,
      maxTrust, maxTrustCost, disorgCounter, disorgFlag, synchCost, disorgMsg, threnodyCost, entertainCost,
      boredomLevel, boredomFlag, boredomMsg, wireBuyerStatus, wirePriceTimer, qFade, autoTourneyStatus, sliderPos,
      tempOps, standardOps, opFade, opFadeTimer, dismantle, endTimer1, endTimer2, endTimer3, endTimer4, endTimer5,
      endTimer6, finalClips, maxFactoryLevel, maxDroneLevel, incomeThen, incomeNow, trueAvgRev;
  double incomeTracker[10];
  /* investments */
  double portfolioSize, stockID, secTotal, portTotal, sellDelay, riskiness, investLevel, investUpgradeCost,
      stockGainThreshold, ledger, stockReportCounter;
  /* strategic modeling */
  double tourneyCost, tourneyLvl, stratCounter, hMove, vMove, hMovePrev, vMovePrev, aa, ab, ba, bb, rounds,
      currentRound, rCounter, tourneyInProg, winnerPtr, placeScore, showScore, high, yomi, yomiBoost, resultsTimer,
      resultsFlag;
  double stratScore[PC_STRATS];
  /* probes and combat */
  double probeSpeed, probeNav, probeRep, partialProbeSpawn, probeHaz, partialProbeHaz, probesLostHaz,
      probesLostDrift, probesLostCombat, probeFac, probeHarv, probeWire, probeCombat, probeDescendents, drifterCount,
      probeTrust, probeUsedTrust, probeLaunchLevel, probeCost, probeTrustCost, attackSpeed, battleSpeed,
      attackSpeedFlag, battleID, battleNameFlag, outcomeTimer, warTrigger, unitSize, driftersKilled, battleEndDelay,
      battleEndTimer, masterBattleClock, battleClock, honorCount, bonusHonor, honorReward, battleLeftShips,
      battleRightShips;
  double qChipValue[PC_CHIPS];
  double project216Ops;            /* the ops Disassemble Memory's price tag showed */
  double qOpsShown, honorAmount, mdps, maps, wpps, secTimer;
  double p10h, p100h, p1000h, p10w, p100w, p1000w, p10f, p100f, p10b, p100b;   /* the +10, +100, +1k prices */
  pc_stock stocks[PC_STOCKS];
  pc_battle battle;
  /* ---- 32-bit numbers */
  uint32_t rng[4];                 /* Math.random() */
  uint32_t now;                    /* ms since the game started running (the timers below are due then) */
  uint32_t at_main, at_slow, at_battle, at_stocks, at_shop, at_sell, at_pick, at_tourney, at_blink, at_hypno;
  int32_t nstocks, nstrats, pick, pickChoice, riskChoice, tourneyStep, h, v, gridLabel, lastCell;
  int32_t battles, battleResult, nactive, hypnoCount, blinkProject, blinkCount, nmsg, resetPending;
  int32_t nIncome, resultsShown, runEnabled, qShown, hypnoText, hypnoVisible, hypnoOn, ended;
  int32_t stratPos[PC_STRATS], results[PC_STRATS], nresults;
  /* ---- 16-bit numbers and bytes */
  uint16_t battleNumbers[PC_BATTLE_NAMES];
  int16_t uses[PC_PROJECTS];
  uint8_t flag[PC_PROJECTS];
  uint8_t active[PC_PROJECTS];     /* the projects on the list, in the order they came */
  uint8_t qChipActive[PC_CHIPS];
  uint8_t project218Seen, spare[3];
  char battleName[PC_NAME_LEN], threnodyTitle[PC_NAME_LEN], project133Title[PC_NAME_LEN];
  char tourneyText[64];
  char msg[PC_MSG][PC_MSG_LEN];    /* the console: msg[0] the newest */
} pc_game;

/* The battle canvas's ships: not kept (the game drops a battle in progress when it loads). */
typedef struct {
  float x, y, vx, vy;
  uint8_t alive, team, framesDead, gx, gy;
} pc_ship;

typedef struct {
  int numShips, numLeft, numRight;
  pc_ship ship[PC_SHIPS];
  uint16_t cell_n[15 * 31];
  uint16_t cell_ships[PC_SHIPS];   /* ships by cell (cell_first gives where each cell starts) */
  uint16_t cell_first[15 * 31 + 1];
} pc_fleet;

extern pc_game *G;                 /* the game being played (sim.c runs on it) */
extern pc_fleet *F;

/* ---- sim.c */
void pc_new(pc_game *g, uint32_t seed, double prestigeU, double prestigeS);
void pc_loaded(pc_game *g);        /* after a load: what the game's refresh() does */
void pc_advance(uint32_t ms);      /* runs the timers up to now + ms */
double pc_random(void);
void pc_message(const char *s);
void pc_messagef(const char *a, const char *b, const char *c);   /* a + b + c */
uint32_t pc_hash(const pc_game *g);

/* the buttons (each does what the game's onclick does; false when the button is disabled) */
enum {
  B_CLIP, B_WIREBUYER, B_WIRE, B_CLIPPER, B_MEGA, B_LOWER, B_RAISE, B_ADS, B_PROC, B_MEM, B_QCOMP, B_DEPOSIT,
  B_WITHDRAW, B_RISK, B_INVEST_UP, B_PICK, B_RUN, B_NEW_TOURNEY, B_AUTOTOURNEY, B_FACTORY, B_FACTORY_REBOOT,
  B_HARV1, B_HARV10, B_HARV100, B_HARV1000, B_HARV_REBOOT, B_WIRED1, B_WIRED10, B_WIRED100, B_WIRED1000,
  B_WIRED_REBOOT, B_FARM1, B_FARM10, B_FARM100, B_FARM_REBOOT, B_BATT1, B_BATT10, B_BATT100, B_BATT_REBOOT,
  B_ENTERTAIN, B_SYNCH, B_WORK, B_THINK, B_PROBE, B_PROBE_TRUST, B_MAX_TRUST,
  B_STAT_DOWN, B_STAT_UP = B_STAT_DOWN + 8, B_PROJECT = B_STAT_UP + 8, B_COUNT = B_PROJECT + PC_PROJECTS
};
bool pc_enabled(int b);
bool pc_press(int b);
const char *pc_strat_name(int i);
const char *pc_choice_name(int x, int b);   /* the payoff grid's moves: pair x, A (0) or B (1) */

/* ---- projects.c */
typedef struct {
  const char *title, *price, *desc;
} pc_project_text;
extern const pc_project_text PC_PROJECT_TEXT[PC_PROJECTS];
bool pc_project_trigger(int i);
bool pc_project_cost(int i);
void pc_project_effect(int i);
void pc_manage_projects(void);
void pc_remove_active(int i);
void pc_project_title(int i, char *out, int n);
void pc_project_price(int i, char *out, int n);
int pc_project_index(int number);  /* projectN -> its place (pc_project_index(46)); -1 if none */
enum {
  P1, P2, P3, P4, P5, P6, P7, P8, P9, P10, P10B, P11, P12, P13, P14, P15, P17, P16, P18, P19, P20, P21, P22, P23,
  P24, P25, P26, P34, P70, P35, P27, P28, P29, P30, P31, P41, P37, P38, P42, P43, P44, P45, P40, P40B, P46, P50,
  P51, P60, P61, P62, P63, P64, P65, P66, P100, P101, P102, P110, P111, P112, P118, P119, P120, P121, P125, P126,
  P127, P128, P129, P130, P131, P132, P133, P134, P135, P140, P141, P142, P143, P144, P145, P146, P147, P148, P200,
  P201, P210, P211, P212, P213, P214, P215, P216, P217, P218, P219
};
_Static_assert(P219 == PC_PROJECTS - 1, "96 projects");

/* functions of sim.c the projects call */
void pc_factory_reboot(void);
void pc_harvester_reboot(void);
void pc_wiredrone_reboot(void);
void pc_farm_reboot(void);
void pc_battery_reboot(void);
void pc_hypno_event(void);
void pc_add_strat(int i);
void pc_blink(int project);
#endif
