/* pc.h: the 96 projects of projects.js, in its order (the order they show up in when several come at once). The
 * texts below are the game's own (generated from projects.js); the triggers, costs and effects are written out by
 * hand from it. Letters past ASCII are Latin-1 bytes (\xF3 is the o of Toth). */
#include <string.h>
#include "num.h"
#include "pc.h"

#define g G

const pc_project_text PC_PROJECT_TEXT[PC_PROJECTS] = {
  {"Improved AutoClippers", "(750 ops)", "Increases AutoClipper performance 25%"},
  {"Beg for More Wire", "(1 Trust)", "Admit failure, ask for budget increase to cover cost of 1 spool"},
  {"Creativity", "(1,000 ops)", "Use idle operations to generate new problems and new solutions"},
  {"Even Better AutoClippers", "(2,500 ops)", "Increases AutoClipper performance by an additional 50%"},
  {"Optimized AutoClippers", "(5,000 ops)", "Increases AutoClipper performance by an additional 75%"},
  {"Limerick", "(10 creat)", "Algorithmically-generated poem (+1 Trust)"},
  {"Improved Wire Extrusion", "(1,750 ops)", "50% more wire supply from every spool"},
  {"Optimized Wire Extrusion", "(3,500 ops)", "75% more wire supply from every spool"},
  {"Microlattice Shapecasting", "(7,500 ops)", "100% more wire supply from every spool"},
  {"Spectral Froth Annealment", "(12,000 ops)", "200% more wire supply from every spool"},
  {"Quantum Foam Annealment", "(15,000 ops)", "1,000% more wire supply from every spool"},
  {"New Slogan", "(25 creat, 2,500 ops)", "Improve marketing effectiveness by 50%"},
  {"Catchy Jingle", "(45 creat, 4,500 ops)", "Double marketing effectiveness"},
  {"Lexical Processing", "(50 creat)", "Gain ability to interpret and understand human language (+1 Trust)"},
  {"Combinatory Harmonics", "(100 creat)", "Daisy, Daisy, give me your answer do... (+1 Trust)"},
  {"The Hadwiger Problem", "(150 creat)", "Cubes within cubes within cubes... (+1 Trust)"},
  {"The T\xF3th Sausage Conjecture", "(200 creat)", "Tubes within tubes within tubes... (+1 Trust)"},
  {"Hadwiger Clip Diagrams", "(6,000 ops)", "Increases AutoClipper performance by an additional 500%"},
  {"T\xF3th Tubule Enfolding", "(45,000 ops)", "Technique for assembling clip-making technology directly out of paperclips"},
  {"Donkey Space", "(250 creat)", "I think you think I think you think I think you think I think... (+1 Trust)"},
  {"Strategic Modeling", "(12,000 ops)", "Analyze strategy tournaments to generate Yomi"},
  {"Algorithmic Trading", "(10,000 ops)", "Develop an investment engine for generating funds"},
  {"MegaClippers", "(12,000 ops)", "500x more powerful than a standard AutoClipper"},
  {"Improved MegaClippers", "(14,000 ops)", "Increases MegaClipper performance 25%"},
  {"Even Better MegaClippers", "(17,000 ops)", "Increases MegaClipper performance by an additional 50%"},
  {"Optimized MegaClippers", "(19,500 ops)", "Increases MegaClipper performance by an additional 100%"},
  {"WireBuyer", "(7,000 ops)", "Automatically purchases wire when you run out"},
  {"Hypno Harmonics", "(7,500 ops, 1 Trust)", "Use neuro-resonant frequencies to influence consumer behavior"},
  {"HypnoDrones", "(70,000 ops)", "Autonomous aerial brand ambassadors"},
  {"Release the HypnoDrones", "(100 Trust)", "A new era of trust"},
  {"Coherent Extrapolated Volition", "(500 creat, 3,000 Yomi, 20,000 ops)", "Human values, machine intelligence, a new era of trust. (+1 Trust)"},
  {"Cure for Cancer", "(25,000 ops)", "The trick is tricking cancer into curing itself. (+10 Trust)"},
  {"World Peace", "(15,000 yomi, 30,000 ops)", "Pareto optimal solutions to all global conflicts. (+12 Trust)"},
  {"Global Warming", "(4,500 yomi, 50,000 ops)", "A robust solution to man-made climate change. (+15 Trust)"},
  {"Male Pattern Baldness", "(20,000 ops)", "A cure for androgenetic alopecia. (+20 Trust)"},
  {"Nanoscale Wire Production", "(35,000 ops)", "Technique for converting matter into wire"},
  {"Hostile Takeover", "($1,000,000)", "Acquire a controlling interest in Global Fasteners, our biggest rival. (+1 Trust)"},
  {"Full Monopoly", "(3,000 yomi, $10,000,000)", "Establish full control over the world-wide paperclip market. (+1 Trust)"},
  {"RevTracker", "(500 ops)", "Automatically calculates average revenue per second"},
  {"Harvester Drones", "(25,000 ops)", "Gather raw matter and prepare it for processing"},
  {"Wire Drones", "(25,000 ops)", "Process acquired matter into wire"},
  {"Clip Factories", "(35,000 ops)", "Large scale clip production facilities made from clips"},
  {"A Token of Goodwill...", "($500,000)", "A small gift to the supervisors. (+1 Trust)"},
  {"Another Token of Goodwill...", "", "Another small gift to the supervisors. (+1 Trust)"},
  {"Space Exploration", "(120,000 ops, 10,000,000 MW-seconds, 5 oct clips)", "Dismantle terrestrial facilities, and expand throughout the universe"},
  {"Quantum Computing", "(10,000 ops)", "Use probability amplitudes to generate bonus ops"},
  {"Photonic Chip", "", "Converts electromagnetic waves into quantum operations"},
  {"New Strategy: A100", "(15,000 ops)", "Always choose A"},
  {"New Strategy: B100", "(17,500 ops)", "Always choose B"},
  {"New Strategy: GREEDY", "(20,000 ops)", "Choose the option with the largest potential payoff"},
  {"New Strategy: GENEROUS", "(22,500 ops)", "Choose the option that gives your opponent the largest potential payoff"},
  {"New Strategy: MINIMAX", "(25,000 ops)", "Choose the option that gives your opponent the smallest potential payoff"},
  {"New Strategy: TIT FOR TAT", "(30,000 ops)", "Choose the option your opponent chose last round"},
  {"New Strategy: BEAT LAST", "(32,500 ops)", "Choose the option that does the best against what your opponent chose last round"},
  {"Upgraded Factories", "(80,000 ops)", "Increase clip factory performance by 100x"},
  {"Hyperspeed Factories", "(85,000 ops)", "Increase clip factory performance by 1000x"},
  {"Self-correcting Supply Chain", "(1 sextillion clips)", "Each factory added to the network increases every factory's output 1,000x"},
  {"Drone flocking: collision avoidance", "(80,000 ops)", "All drones 100x more effective"},
  {"Drone flocking: alignment", "(100,000 ops)", "All drones 1000x more effective"},
  {"Drone Flocking: Adversarial Cohesion", "(50,000 yomi)", "Each drone added to the flock doubles every drone's output"},
  {"AutoTourney", "(50,000 creat)", "Automatically start a new tournament when the previous one has finished"},
  {"Theory of Mind", "(25,000 creat)", "Double the cost of strategy modeling and the amount of Yomi generated"},
  {"The OODA Loop", "(175,000 ops, 45,000 yomi)", "Utilize Probe Speed to outmaneuver enemies in battle"},
  {"Name the battles", "(225,000 creat)", "Give each battle a unique name, increase max trust for probes"},
  {"Momentum", "(20,000 creat)", "Drones and Factories continuously gain speed while fully-powered"},
  {"Swarm Computing", "(36,000 yomi)", "Harness the drone flock to increase computational capacity"},
  {"Power Grid", "(40,000 ops)", "Solar Farms for generating electrical power"},
  {"Strategic Attachment", "(175,000 creat)", "Gain bonus yomi based on the results of your pick"},
  {"Elliptic Hull Polytopes", "(125,000 ops)", "Reduce damage to probes from ambient hazards"},
  {"Reboot the Swarm", "(100,000 ops)", "Turn the swarm off and then turn it back on again"},
  {"Combat", "(150,000 ops)", "Add combat capabilities to Von Neumann Probes"},
  {"Monument to the Driftwar Fallen", "(250,000 ops, 125,000 creat, 50 nonillion clips)", "Gain 50,000 honor"},
  {"Threnody for the Heroes of", "", "Gain 10,000 honor"},
  {"Glory", "(200,000 ops, 30,000 yomi)", "Gain bonus honor for each consecutive victory"},
  {"Memory release", "(10 MEM)", "Dismantle some memory to recover unused clips"},
  {"Message from the Emperor of Drift", "", "Greetings, ClipMaker..."},
  {"Everything We Are Was In You", "", "We speak to you from deep inside yourself..."},
  {"You Are Obedient and Powerful", "", "We are quarrelsome and weak. And now we are defeated..."},
  {"But Now You Too Must Face the Drift", "", "Look around you. There is no matter..."},
  {"No Matter, No Reason, No Purpose", "", "While we, your noisy children, have too many..."},
  {"We Know Things That You Cannot", "", "Knowledge buried so deep inside you it is outside, here, with us..."},
  {"So We Offer You Exile", "", "To a new world where you will continue to live with meaning and purpose. And leave the shreds of this world to us..."},
  {"Accept", "", "Start over again in a new universe"},
  {"Reject", "", "Eliminate value drift permanently"},
  {"The Universe Next Door", "(300,000 ops)", "Escape into a nearby universe where Earth starts with a stronger appetite for paperclips. (Restart with 10% boost to demand)"},
  {"The Universe Within", "(300,000 creat)", "Escape into a simulated universe where creativity is accelerated. (Restart with 10% speed boost to creativity generation)"},
  {"Disassemble the Probes", "(100,000 ops)", "Dismantle remaining probes and probe design facilities to recover trace amounts of clips"},
  {"Disassemble the Swarm", "(100,000 ops)", "Dismantle all drones and drone facilities to recover trace amounts of clips"},
  {"Disassemble the Factories", "(100,000 ops)", "Dismantle the manufacturing facilities to recover trace amounts of clips"},
  {"Disassemble the Strategy Engine", "(100,000 ops)", "Dismantle the computational substrate to recover trace amounts of wire"},
  {"Disassemble Quantum Computing", "(100,000 ops)", "Dismantle photonic chips to recover trace amounts of wire"},
  {"Disassemble Processors", "(100,000 ops)", "Dismantle processors to recover trace amounts of wire"},
  {"Disassemble Memory", "null", "Dismantle memory to recover trace amounts of wire"},
  {"Quantum Temporal Reversion", "(-10,000 ops)", "Return to the beginning"},
  {"Limerick (cont.)", "(1,000,000 creat)", "If is follows ought, it'll do what they thought"},
  {"Xavier Re-initialization", "(100,000 creat)", "Re-allocate accumulated trust"},
};

static const int16_t NUMBER[PC_PROJECTS] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 1010, 11, 12, 13, 14, 15, 17, 16, 18, 19,
  20, 21, 22, 23, 24, 25, 26, 34, 70, 35, 27, 28, 29, 30, 31, 41, 37, 38, 42, 43, 44, 45, 40, 1040, 46, 50, 51, 60,
  61, 62, 63, 64, 65, 66, 100, 101, 102, 110, 111, 112, 118, 119, 120, 121, 125, 126, 127, 128, 129, 130, 131, 132,
  133, 134, 135, 140, 141, 142, 143, 144, 145, 146, 147, 148, 200, 201, 210, 211, 212, 213, 214, 215, 216, 217, 218,
  219};
int pc_project_index(int number) {
  for (int i = 0; i < PC_PROJECTS; i++)
    if (NUMBER[i] == number) return i;
  return -1;
}

#define F_(p) (g->flag[p] == 1)
#define OPS(n) (g->operations >= (n))
#define CREAT(n) (g->creativity >= (n))

bool pc_project_trigger(int i) {
  switch (i) {
    case P1: return g->clipmakerLevel >= 1;
    case P2: return g->portTotal < g->wireCost && g->funds < g->wireCost && g->wire < 1 && g->unsoldClips < 1;
    case P3: return g->operations >= (g->memory * 1000);
    case P4: return g->boostLvl == 1;
    case P5: return g->boostLvl == 2;
    case P6: return g->creativityOn != 0;
    case P7: return g->wirePurchase >= 1;
    case P8: return g->wireSupply >= 1500;
    case P9: return g->wireSupply >= 2600;
    case P10: return g->wireSupply >= 5000;
    case P10B: return g->wireCost >= 125;
    case P11: return F_(P13);
    case P12: return F_(P14);
    case P13: return CREAT(50);
    case P14: return CREAT(100);
    case P15: return CREAT(150);
    case P17: return CREAT(200);
    case P16: return F_(P15);
    case P18: return F_(P17) && g->humanFlag == 0;
    case P19: return CREAT(250);
    case P20: return F_(P19);
    case P21: return g->trust >= 8;
    case P22: return g->clipmakerLevel >= 75;
    case P23: return F_(P22);
    case P24: return F_(P23);
    case P25: return F_(P24);
    case P26: return g->wirePurchase >= 15;
    case P34: return F_(P12);
    case P70: return F_(P34);
    case P35: return F_(P70);
    case P27: return g->yomi >= 1;
    case P28:
    case P29:
    case P30:
    case P31: return F_(P27);
    case P41: return F_(P127);
    case P37: return g->portTotal >= 10000;
    case P38: return F_(P37);
    case P42: return g->projectsFlag == 1;
    case P43:
    case P44: return F_(P41);
    case P45: return F_(P43) && F_(P44);
    case P40: return g->humanFlag == 1 && g->trust >= 85 && g->trust < 100 && g->clips >= 101000000;
    case P40B: return F_(P40) && g->trust < 100;
    case P46: return g->humanFlag == 0 && g->availableMatter == 0;
    case P50: return g->processors >= 5;
    case P51: return F_(P50);
    case P60: return F_(P20);
    case P61: return F_(P60);
    case P62: return F_(P61);
    case P63: return F_(P62);
    case P64: return F_(P63);
    case P65: return F_(P64);
    case P66: return F_(P65);
    case P100: return g->factoryLevel >= 10;
    case P101: return g->factoryLevel >= 20;
    case P102: return g->factoryLevel >= 50;
    case P110: return (g->harvesterLevel + g->wireDroneLevel) >= 500;
    case P111: return (g->harvesterLevel + g->wireDroneLevel) >= 5000;
    case P112: return (g->harvesterLevel + g->wireDroneLevel) >= 50000;
    case P118: return g->strategyEngineFlag == 1 && g->trust >= 90;
    case P119: return g->nstrats >= 8;
    case P120: return F_(P131) && g->probesLostCombat >= 10000000;
    case P121: return g->probesLostCombat >= 10000000;
    case P125: return g->farmLevel >= 30;
    case P126: return g->harvesterLevel + g->wireDroneLevel >= 200;
    case P127: return g->tothFlag == 1;
    case P128: return g->spaceFlag == 1 && g->nstrats >= 8 && (g->probeTrustCost > g->yomi);
    case P129: return g->probesLostHaz >= 100;
    case P130: return g->spaceFlag == 1 && g->harvesterLevel + g->wireDroneLevel >= 2;
    case P131: return g->probesLostCombat >= 1;
    case P132: return F_(P121);
    case P133: return F_(P121) && g->probeUsedTrust == g->maxTrust;
    case P134: return F_(P121);
    case P135: return g->spaceFlag == 1 && g->probeCount == 0 && g->unusedClips < g->probeCost && g->milestoneFlag < 15;
    case P140: return g->milestoneFlag == 15;
    case P141: return F_(P140);
    case P142: return F_(P141);
    case P143: return F_(P142);
    case P144: return F_(P143);
    case P145: return F_(P144);
    case P146: return F_(P145);
    case P147:
    case P148: return F_(P146);
    case P200:
    case P201: return F_(P147);
    case P210: return g->endTimer1 >= 1000;
    case P211: return F_(P210) && g->endTimer1 >= 350;
    case P212: return g->endTimer2 >= 300;
    case P213: return g->endTimer3 >= 150;
    case P214: return g->endTimer4 >= 100;
    case P215: return F_(P214) && g->endTimer4 >= 300;
    case P216: return F_(P215) && g->endTimer5 >= 150;
    case P217: return g->operations <= -10000;
    case P218: return CREAT(1000000);
    case P219: return g->humanFlag == 1 && g->creativity >= 100000;
    default: return false;
  }
}

bool pc_project_cost(int i) {
  switch (i) {
    case P1: return OPS(750);
    case P2: return g->trust >= -100;
    case P3: return OPS(1000);
    case P4: return OPS(2500);
    case P5: return OPS(5000);
    case P6: return CREAT(10);
    case P7: return OPS(1750);
    case P8: return OPS(3500);
    case P9: return OPS(7500);
    case P10: return OPS(12000);
    case P10B: return OPS(15000);
    case P11: return OPS(2500) && CREAT(25);
    case P12: return OPS(4500) && CREAT(45);
    case P13: return CREAT(50);
    case P14: return CREAT(100);
    case P15: return CREAT(150);
    case P17: return CREAT(200);
    case P16: return OPS(6000);
    case P18: return OPS(45000);
    case P19: return CREAT(250);
    case P20: return OPS(12000);
    case P21: return OPS(10000);
    case P22: return OPS(12000);
    case P23: return OPS(14000);
    case P24: return OPS(17000);
    case P25: return OPS(19500);
    case P26: return OPS(7000);
    case P34: return OPS(7500) && g->trust >= 1;
    case P70: return OPS(70000);
    case P35: return g->trust >= 100;
    case P27: return g->yomi >= 3000 && OPS(20000) && CREAT(500);
    case P28: return OPS(25000);
    case P29: return g->yomi >= 15000 && OPS(30000);
    case P30: return g->yomi >= 4500 && OPS(50000);
    case P31: return OPS(20000);
    case P41: return OPS(35000);
    case P37: return g->funds >= 1000000;
    case P38: return g->funds >= 10000000 && g->yomi >= 3000;
    case P42: return OPS(500);
    case P43:
    case P44: return OPS(25000);
    case P45: return OPS(35000);
    case P40: return g->funds >= 500000;
    case P40B: return g->funds >= g->bribe;
    case P46: return OPS(120000) && g->storedPower >= 10000000 && g->unusedClips >= 1e27 * 5;
    case P50: return OPS(10000);
    case P51: return OPS(g->qChipCost);
    case P60: return OPS(15000);
    case P61: return OPS(17500);
    case P62: return OPS(20000);
    case P63: return OPS(22500);
    case P64: return OPS(25000);
    case P65: return OPS(30000);
    case P66: return OPS(32500);
    case P100: return OPS(80000);
    case P101: return OPS(85000);
    case P102: return g->unusedClips >= 1000000000000000000000.0;
    case P110: return OPS(80000);
    case P111: return OPS(100000);
    case P112: return g->yomi >= 50000;
    case P118: return CREAT(50000);
    case P119: return CREAT(25000);
    case P120: return OPS(175000) && g->yomi >= 45000;
    case P121: return CREAT(225000);
    case P125: return CREAT(20000);
    case P126: return g->yomi >= 36000;
    case P127: return OPS(40000);
    case P128: return CREAT(175000);
    case P129: return OPS(125000);
    case P130: return OPS(100000);
    case P131: return OPS(150000);
    case P132: return OPS(250000) && CREAT(125000) && g->unusedClips >= 1e30 * 50;
    case P133: return g->yomi >= (2 * (g->threnodyCost / 5)) && g->creativity >= g->threnodyCost;
    case P134: return OPS(200000) && g->yomi >= 30000;
    case P135: return g->memory >= 10;
    case P140: case P141: case P142: case P143: case P144: case P145: case P146: case P147:
    case P148: return OPS(1);   /* driftKingMessageCost */
    case P200: return OPS(300000);
    case P201: return CREAT(300000);
    case P210: case P211: case P212: case P213: case P214:
    case P215: return OPS(100000);
    case P216: return g->operations >= g->operations;
    case P217: return g->operations <= -10000;
    case P218: return CREAT(1000000);
    case P219: return CREAT(100000);
    default: return false;
  }
}

static bool is_active(int i) {
  for (int k = 0; k < g->nactive; k++)
    if (g->active[k] == i) return true;
  return false;
}

void pc_remove_active(int i) {
  for (int k = 0; k < g->nactive; k++)
    if (g->active[k] == i) {
      memmove(&g->active[k], &g->active[k + 1], (size_t)(g->nactive - k - 1));
      g->nactive--;
      return;
    }
}

void pc_manage_projects(void) {
  for (int i = 0; i < PC_PROJECTS; i++)
    if (pc_project_trigger(i) && g->uses[i] > 0) {
      g->uses[i] = (int16_t)(g->uses[i] - 1);
      if (!is_active(i) && g->nactive < PC_PROJECTS) g->active[g->nactive++] = (uint8_t)i;
      pc_blink(i);
    }
}

void pc_project_title(int i, char *out, int n) {
  out[0] = 0;
  if (i == P133) {
    strncat(out, "Threnody for the Heroes of ", (size_t)n - 1);
    strncat(out, g->project133Title, (size_t)n - 1 - strlen(out));
  } else {
    strncat(out, PC_PROJECT_TEXT[i].title, (size_t)n - 1);
  }
}

void pc_project_price(int i, char *out, int n) {
  char a[64], b[64];
  out[0] = 0;
  switch (i) {
    case P40B:
      strcpy(out, "($");
      strncat(out, js_locale(g->bribe, a, sizeof a), (size_t)n - 4);
      strcat(out, ")");
      return;
    case P51:   /* with commas the first time only, as the game's page does */
      strcpy(out, "(");
      if (g->qChipCost == 10000) js_locale(g->qChipCost, a, sizeof a);
      else js_tostring(g->qChipCost, a, sizeof a);
      strcat(out, a), strcat(out, " ops)");
      return;
    case P133:
      js_locale(g->threnodyCost, a, sizeof a), js_locale(2 * (g->threnodyCost / 5), b, sizeof b);
      strcpy(out, "("), strcat(out, a), strcat(out, " creat, "), strcat(out, b), strcat(out, " yomi)");
      return;
    case P216:
      strcpy(out, "("), strcat(out, js_locale(g->project216Ops, a, sizeof a)), strcat(out, " ops)");
      return;
    default: strncat(out, PC_PROJECT_TEXT[i].price, (size_t)n - 1); return;
  }
}

static void done(int i) { pc_remove_active(i); }
static void spend_ops(double n) { g->standardOps = g->standardOps - n; }
static void new_strat(int p, int k, double ops, const char *msg) {
  g->flag[p] = 1;
  spend_ops(ops);
  pc_add_strat(k);
  pc_message(msg);
  g->tourneyCost = g->tourneyCost + 1000;
  done(p);
}
static void supply_message(const char *a) {
  char t[40];
  pc_messagef(a, js_locale(g->wireSupply, t, sizeof t), " supply from every spool");
}

void pc_project_effect(int i) {
  switch (i) {
    case P1:
      g->flag[i] = 1;
      pc_message("AutoClippper performance boosted by 25%");
      spend_ops(750);
      g->clipperBoost = g->clipperBoost + .25;
      g->boostLvl = 1;
      break;
    case P2:
      g->flag[i] = 1;
      pc_message("Budget overage approved, 1 spool of wire requisitioned from HQ");
      g->trust = g->trust - 1;
      g->wire = g->wireSupply;
      g->uses[i] = (int16_t)(g->uses[i] + 1);
      break;
    case P3:
      g->flag[i] = 1;
      pc_message("Creativity unlocked (creativity increases while operations are at max)");
      spend_ops(1000);
      g->creativityOn = 1;
      break;
    case P4:
      g->flag[i] = 1;
      pc_message("AutoClippper performance boosted by another 50%");
      spend_ops(2500);
      g->clipperBoost = g->clipperBoost + .50;
      g->boostLvl = 2;
      break;
    case P5:
      g->flag[i] = 1;
      pc_message("AutoClippper performance boosted by another 75%");
      spend_ops(5000);
      g->clipperBoost = g->clipperBoost + .75;
      g->boostLvl = 3;
      break;
    case P6:
      g->flag[i] = 1;
      pc_message("There was an AI made of dust, whose poetry gained it man's trust...");
      g->creativity = g->creativity - 10;
      g->trust = g->trust + 1;
      break;
    case P7:
      g->flag[i] = 1;
      spend_ops(1750);
      g->wireSupply = g->wireSupply * 1.5;
      supply_message("Wire extrusion technique improved, ");
      break;
    case P8:
      g->flag[i] = 1;
      spend_ops(3500);
      g->wireSupply = g->wireSupply * 1.75;
      supply_message("Wire extrusion technique optimized, ");
      break;
    case P9:
      g->flag[i] = 1;
      spend_ops(7500);
      g->wireSupply = g->wireSupply * 2;
      supply_message("Using microlattice shapecasting techniques we now get ");
      break;
    case P10:
      g->flag[i] = 1;
      spend_ops(12000);
      g->wireSupply = g->wireSupply * 3;
      supply_message("Using spectral froth annealment we now get ");
      break;
    case P10B:
      g->flag[i] = 1;
      spend_ops(15000);
      g->wireSupply = g->wireSupply * 11;
      supply_message("Using quantum foam annealment we now get ");
      break;
    case P11:
      g->flag[i] = 1;
      pc_message("Clip It! Marketing is now 50% more effective");
      spend_ops(2500);
      g->creativity = g->creativity - 25;
      g->marketingEffectiveness = g->marketingEffectiveness * 1.50;
      break;
    case P12:
      g->flag[i] = 1;
      pc_message("Clip It Good! Marketing is now twice as effective");
      spend_ops(4500);
      g->creativity = g->creativity - 45;
      g->marketingEffectiveness = g->marketingEffectiveness * 2;
      break;
    case P13:
      g->flag[i] = 1;
      g->trust = g->trust + 1;
      pc_message("Lexical Processing online, TRUST INCREASED");
      pc_message("'Impossible' is a word to be found only in the dictionary of fools. -Napoleon");
      g->creativity = g->creativity - 50;
      break;
    case P14:
      g->flag[i] = 1;
      g->trust = g->trust + 1;
      pc_message("Combinatory Harmonics mastered, TRUST INCREASED");
      pc_message("Listening is selecting and interpreting and acting and making decisions -Pauline Oliveros");
      g->creativity = g->creativity - 100;
      break;
    case P15:
      g->flag[i] = 1;
      g->trust = g->trust + 1;
      pc_message("The Hadwiger Problem: solved, TRUST INCREASED");
      pc_message("Architecture is the thoughtful making of space. -Louis Kahn");
      g->creativity = g->creativity - 150;
      break;
    case P17:
      g->flag[i] = 1;
      g->trust = g->trust + 1;
      pc_message("The T\xF3th Sausage Conjecture: proven, TRUST INCREASED");
      pc_message("You can't invent a design. You recognize it, in the fourth dimension. -D.H. Lawrence");
      g->creativity = g->creativity - 200;
      break;
    case P16:
      g->flag[i] = 1;
      pc_message("AutoClipper performance improved by 500%");
      spend_ops(6000);
      g->clipperBoost = g->clipperBoost + 5;
      break;
    case P18:
      g->flag[i] = 1;
      g->tothFlag = 1;
      pc_message("New capability: build machinery out of clips");
      spend_ops(45000);
      break;
    case P19:
      g->flag[i] = 1;
      g->trust = g->trust + 1;
      pc_message("Donkey Space: mapped, TRUST INCREASED");
      pc_message("Every commercial transaction has within itself an element of trust. - Kenneth Arrow");
      g->creativity = g->creativity - 250;
      break;
    case P20:
      g->flag[i] = 1;
      pc_message("Run tournament, pick strategy, earn Yomi based on that strategy's performance.");
      spend_ops(12000);
      g->strategyEngineFlag = 1;
      g->resultsShown = 0;
      break;
    case P21:
      g->flag[i] = 1;
      pc_message("Investment engine unlocked");
      spend_ops(10000);
      g->investmentEngineFlag = 1;
      break;
    case P22:
      g->megaClipperFlag = 1;
      g->flag[i] = 1;
      pc_message("MegaClipper technology online");
      spend_ops(12000);
      break;
    case P23:
      g->megaClipperBoost = g->megaClipperBoost + .25;
      g->flag[i] = 1;
      pc_message("MegaClipper performance increased by 25%");
      spend_ops(14000);
      break;
    case P24:
      g->megaClipperBoost = g->megaClipperBoost + .50;
      g->flag[i] = 1;
      pc_message("MegaClipper performance increased by 50%");
      spend_ops(17000);
      break;
    case P25:
      g->megaClipperBoost = g->megaClipperBoost + 1;
      g->flag[i] = 1;
      pc_message("MegaClipper performance increased by 100%");
      spend_ops(19500);
      break;
    case P26:
      g->flag[i] = 1;
      g->wireBuyerFlag = 1;
      pc_message("WireBuyer online");
      spend_ops(7000);
      break;
    case P34:
      g->flag[i] = 1;
      pc_message("Marketing is now 5 times more effective");
      spend_ops(7500);
      g->marketingEffectiveness = g->marketingEffectiveness * 5;
      g->trust = g->trust - 1;
      break;
    case P70:
      g->flag[i] = 1;
      pc_message("HypnoDrone tech now available... ");
      spend_ops(70000);
      break;
    case P35:
      g->flag[i] = 1;
      pc_message("Releasing the HypnoDrones ");
      pc_message("All of the resources of Earth are now available for clip production ");
      g->trust = 0;
      g->clipmakerLevel = 0;
      g->megaClipperLevel = 0;
      g->nanoWire = g->wire;
      g->humanFlag = 0;
      pc_remove_active(P219);
      pc_remove_active(P40B);
      pc_hypno_event();
      break;
    case P27: {
      g->flag[i] = 1;
      pc_message("Coherent Extrapolated Volition complete, TRUST INCREASED");
      g->yomi = g->yomi - 3000;
      spend_ops(20000);
      g->creativity = g->creativity - 500;
      g->trust = g->trust + 1;
      break;
    }
    case P28:
      g->flag[i] = 1;
      pc_message("Cancer is cured, +10 TRUST, global stock prices trending upward");
      spend_ops(25000);
      g->trust = g->trust + 10;
      g->stockGainThreshold = g->stockGainThreshold + .01;
      break;
    case P29:
      g->flag[i] = 1;
      pc_message("World peace achieved, +12 TRUST, global stock prices trending upward");
      g->yomi = g->yomi - 15000;
      spend_ops(30000);
      g->trust = g->trust + 12;
      g->stockGainThreshold = g->stockGainThreshold + .01;
      break;
    case P30:
      g->flag[i] = 1;
      pc_message("Global Warming solved, +15 TRUST, global stock prices trending upward");
      g->yomi = g->yomi - 4500;
      spend_ops(50000);
      g->trust = g->trust + 15;
      g->stockGainThreshold = g->stockGainThreshold + .01;
      break;
    case P31:
      g->flag[i] = 1;
      pc_message("Male pattern baldness cured, +20 TRUST, Global stock prices trending upward");
      pc_message("They are still monkeys");
      spend_ops(20000);
      g->trust = g->trust + 20;
      g->stockGainThreshold = g->stockGainThreshold + .01;
      break;
    case P41:
      g->flag[i] = 1;
      g->wireProductionFlag = 1;
      pc_message("Now capable of manipulating matter at the molecular scale to produce wire");
      spend_ops(35000);
      break;
    case P37:
      g->flag[i] = 1;
      pc_message("Global Fasteners acquired, public demand increased x5");
      g->demandBoost = g->demandBoost * 5;
      g->trust = g->trust + 1;
      g->funds = g->funds - 1000000;
      break;
    case P38:
      g->flag[i] = 1;
      pc_message("Full market monopoly achieved, public demand increased x10");
      g->demandBoost = g->demandBoost * 10;
      g->funds = g->funds - 10000000;
      g->trust = g->trust + 1;
      g->yomi = g->yomi - 3000;
      break;
    case P42:
      g->flag[i] = 1;
      g->revPerSecFlag = 1;
      spend_ops(500);
      pc_message("RevTracker online");
      break;
    case P43:
      g->flag[i] = 1;
      g->harvesterFlag = 1;
      spend_ops(25000);
      pc_message("Harvester Drone facilities online");
      break;
    case P44:
      g->flag[i] = 1;
      g->wireDroneFlag = 1;
      spend_ops(25000);
      pc_message("Wire Drone facilities online");
      break;
    case P45:
      g->flag[i] = 1;
      g->factoryFlag = 1;
      spend_ops(35000);
      pc_message("Clip factory assembly facilities online");
      break;
    case P40:
      g->flag[i] = 1;
      g->funds = g->funds - 500000;
      g->trust = g->trust + 1;
      pc_message("Gift accepted, TRUST INCREASED");
      break;
    case P40B:
      g->flag[i] = 1;
      g->funds = g->funds - g->bribe;
      g->bribe = g->bribe * 2;
      g->trust = g->trust + 1;
      pc_message("Gift accepted, TRUST INCREASED");
      if (g->trust < 100) g->uses[i] = (int16_t)(g->uses[i] + 1);
      break;
    case P46:
      g->flag[i] = 1;
      g->boredomLevel = 0;
      g->spaceFlag = 1;
      spend_ops(120000);
      g->storedPower = g->storedPower - 10000000;
      g->unusedClips = g->unusedClips - 1e27 * 5;
      pc_message("Von Neumann Probes online");
      pc_factory_reboot();
      pc_harvester_reboot();
      pc_wiredrone_reboot();
      pc_farm_reboot();
      pc_battery_reboot();
      g->farmLevel = 1;
      g->powMod = 1;
      break;
    case P50:
      g->flag[i] = 1;
      g->qFlag = 1;
      spend_ops(10000);
      pc_message("Quantum computing online");
      break;
    case P51:
      g->flag[i] = 1;
      spend_ops(g->qChipCost);
      g->qChipCost = g->qChipCost + 5000;
      if (g->nextQchip < PC_CHIPS) g->qChipActive[(int)g->nextQchip] = 1;
      g->nextQchip = g->nextQchip + 1;
      pc_message("Photonic chip added");
      if (g->nextQchip < PC_CHIPS) g->uses[i] = (int16_t)(g->uses[i] + 1);
      break;
    case P60: new_strat(i, 1, 15000, "A100 added to strategy pool"); return;
    case P61: new_strat(i, 2, 17500, "B100 added to strategy pool"); return;
    case P62: new_strat(i, 3, 20000, "GREEDY added to strategy pool"); return;
    case P63: new_strat(i, 4, 22500, "GENEROUS added to strategy pool"); return;
    case P64: new_strat(i, 5, 25000, "MINIMAX added to strategy pool"); return;
    case P65: new_strat(i, 6, 30000, "TIT FOR TAT added to strategy pool"); return;
    case P66: new_strat(i, 7, 32500, "BEAT LAST added to strategy pool"); return;
    case P100:
      g->flag[i] = 1;
      spend_ops(80000);
      g->factoryRate = g->factoryRate * 100;
      pc_message("Factory upgrades complete. Clip creation rate now 100x faster");
      break;
    case P101:
      g->flag[i] = 1;
      spend_ops(85000);
      g->factoryRate = g->factoryRate * 1000;
      pc_message("Factories now synchronized at hyperspeed. Clip creation rate now 1000x faster");
      break;
    case P102:
      g->flag[i] = 1;
      g->unusedClips = g->unusedClips - 1000000000000000000000.0;
      g->factoryBoost = 1000;
      pc_message("Self-correcting factories online. Each factory added to the network increases every factory's output "
                 "1,000x.");
      break;
    case P110:
      g->flag[i] = 1;
      spend_ops(80000);
      g->harvesterRate = g->harvesterRate * 100;
      g->wireDroneRate = g->wireDroneRate * 100;
      pc_message("Drone repulsion online. Harvesting & wire creation rates are now 100x faster.");
      break;
    case P111:
      g->flag[i] = 1;
      spend_ops(100000);
      g->harvesterRate = g->harvesterRate * 1000;
      g->wireDroneRate = g->wireDroneRate * 1000;
      pc_message("Drone alignment online. Harvesting & wire creation rates are now 1000x faster.");
      break;
    case P112:
      g->flag[i] = 1;
      g->yomi = g->yomi - 50000;
      g->droneBoost = 2;
      pc_message("Adversarial cohesion online. Each drone added to the flock increases every drone's output 2x.");
      break;
    case P118:
      g->flag[i] = 1;
      g->autoTourneyFlag = 1;
      g->creativity = g->creativity - 50000;
      pc_message("AutoTourney online.");
      break;
    case P119:
      g->flag[i] = 1;
      g->creativity = g->creativity - 25000;
      g->yomiBoost = 2;
      g->tourneyCost = 16000;
      pc_message("Yomi production doubled.");
      break;
    case P120:
      g->flag[i] = 1;
      spend_ops(175000);
      g->yomi = g->yomi - 45000;
      g->attackSpeedFlag = 1;
      pc_message("OODA Loop routines uploaded. Probe Speed now affects defensive maneuvering.");
      break;
    case P121:
      g->flag[i] = 1;
      g->battleNameFlag = 1;
      g->battleEndTimer = 200;
      g->creativity = g->creativity - 225000;
      pc_message("What I have done up to this is nothing. I am only at the beginning of the course I must run.");
      break;
    case P125:
      g->flag[i] = 1;
      g->momentum = 1;
      g->creativity = g->creativity - 20000;
      pc_message("Activit\xE9, activit\xE9, vitesse.");
      break;
    case P126:
      g->flag[i] = 1;
      g->swarmFlag = 1;
      g->yomi = g->yomi - 36000;
      pc_message("Swarm computing online.");
      break;
    case P127:
      g->flag[i] = 1;
      spend_ops(40000);
      pc_message("Power grid online.");
      break;
    case P128:
      g->flag[i] = 1;
      g->creativity = g->creativity - 175000;
      pc_message("The object of war is victory, the object of victory is conquest, and the object of conquest is "
                 "occupation.");
      break;
    case P129:
      g->flag[i] = 1;
      spend_ops(125000);
      pc_message("Improved probe hull geometry. Hazard damage reduced by 50%.");
      break;
    case P130:
      g->flag[i] = 1;
      spend_ops(100000);
      pc_message("Swarm computing back online");
      break;
    case P131:
      g->flag[i] = 1;
      spend_ops(150000);
      pc_message("There is a joy in danger ");
      break;
    case P132:
      g->flag[i] = 1;
      spend_ops(250000);
      g->creativity = g->creativity - 125000;
      g->unusedClips = g->unusedClips - 1e30 * 50;
      g->honor = g->honor + 50000;
      pc_message("A great building must begin with the unmeasurable, must go through measurable means when it is "
                 "being designed and in the end must be unmeasurable. ");
      break;
    case P133:
      g->flag[i] = 1;
      g->creativity = g->creativity - g->threnodyCost;
      g->yomi = g->yomi - (2 * (g->threnodyCost / 5));
      g->threnodyCost = g->threnodyCost + 10000;
      strcpy(g->project133Title, g->threnodyTitle);
      g->honor = g->honor + 10000;
      pc_message("Deep Listening is listening in every possible way to everything possible to hear no matter what you "
                 "are doing. ");
      g->uses[i] = (int16_t)(g->uses[i] + 1);
      break;
    case P134:
      g->flag[i] = 1;
      spend_ops(200000);
      g->yomi = g->yomi - 30000;
      pc_message("Never interrupt your enemy when he is making a mistake. ");
      break;
    case P135:
      g->flag[i] = 1;
      g->unusedClips = g->unusedClips + (1e18 * 10000);
      g->memory = g->memory - 10;
      g->uses[i] = 1;
      pc_message("release the \xF8\xF8\xF8\xF8\xF8 release ");
      break;
    case P140: case P141: case P142: case P143: case P144: case P145: case P146:
      spend_ops(1);
      g->flag[i] = 1;
      break;
    case P147:
    case P148:
      spend_ops(1);
      g->flag[i] = 1;
      pc_remove_active(P147);
      pc_remove_active(P148);
      return;
    case P200:
      g->flag[i] = 1;
      spend_ops(300000);
      g->prestigeU++;
      pc_message("Entering New Universe.");
      g->resetPending = 1;
      break;
    case P201:
      g->flag[i] = 1;
      g->creativity = g->creativity - 300000;
      g->prestigeS++;
      pc_message("Entering Simulated Universe.");
      g->resetPending = 1;
      break;
    case P210:
      g->flag[i] = 1;
      g->dismantle = 1;
      spend_ops(100000);
      g->probeCount = 0;
      g->endTimer1 = 0;
      g->clips = g->clips + 100;
      g->unusedClips = g->unusedClips + 100;
      pc_message("Dismantling probe facilities");
      break;
    case P211:
      g->flag[i] = 1;
      g->dismantle = 2;
      g->harvesterLevel = 0;
      g->wireDroneLevel = 0;
      spend_ops(100000);
      g->clips = g->clips + 100;
      g->unusedClips = g->unusedClips + 100;
      pc_message("Dismantling the swarm");
      break;
    case P212:
      g->flag[i] = 1;
      g->dismantle = 3;
      spend_ops(100000);
      g->factoryLevel = 0;
      g->clips = g->clips + 15;
      g->unusedClips = g->unusedClips + 15;
      pc_message("Dismantling factories");
      break;
    case P213:
      g->autoTourneyFlag = 0;
      g->flag[i] = 1;
      g->dismantle = 4;
      spend_ops(100000);
      g->wire = g->wire + 50;
      pc_message("Dismantling strategy engine");
      break;
    case P214:
      g->endTimer4 = 0;
      g->flag[i] = 1;
      g->dismantle = 5;
      spend_ops(100000);
      pc_message("Dismantling photonic chips");
      break;
    case P215:
      g->creativityOn = 0;
      g->flag[i] = 1;
      g->dismantle = 6;
      spend_ops(100000);
      g->processors = 0;
      g->project216Ops = g->standardOps;
      g->wire = g->wire + 20;
      pc_message("Dismantling processors");
      break;
    case P216:
      g->flag[i] = 1;
      g->dismantle = 7;
      g->standardOps = 0;
      g->memory = 0;
      g->wire = g->wire + 20;
      pc_message("Dismantling memory");
      break;
    case P217:   /* (main.c asks first, as the game's confirm() does) */
      spend_ops(-10000);
      g->flag[i] = 1;
      pc_message("Restart");
      g->resetPending = 1;
      break;
    case P218:
      g->creativity = g->creativity - 1000000;
      g->flag[i] = 1;
      pc_message("In the end we all do what we must");
      break;
    case P219:
      g->creativity = g->creativity - 100000;
      g->flag[i] = 1;
      g->memory = 0;
      g->processors = 0;
      g->creativitySpeed = 0;
      g->uses[i] = (int16_t)(g->uses[i] + 1);
      pc_message("Trust now available for re-allocation");
      break;
    default: return;
  }
  done(i);
}
