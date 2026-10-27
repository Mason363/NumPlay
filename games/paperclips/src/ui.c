/* ui.h: the game's page, for 320 x 240 and the keypad.
 *
 * The web page has three columns; here they are tabs with the page's own sections: Clips (Make Paperclip and
 * Manufacturing), Business, Wire (Wire Production), Power, Space (Space Exploration and Von Neumann Probe
 * Design), Combat (the battle canvas, at its own 310 x 150), Computer (Computational Resources, the swarm and
 * Quantum Computing), Projects, Invest (Investments) and Strategy (Strategic Modeling), each shown when the game
 * shows its section. The console stays at the top, Paperclips under it. The page's look: white, black text, grey
 * buttons; the button in focus has a blue frame.
 *
 * Each frame is laid out into a list of drawing steps, then drawn strip by strip. */
#include <string.h>
#include "gfx.h"
#include "num.h"
#include "plat.h"
#include "ui.h"

#define g G

void np_push(int x, int y, int w, int h, const color_t *px) { plat_push(x, y, w, h, px); }
static color_t strip[SCREEN_W * STRIP_H];

#define CON_H 34
#define HDR_Y 34
#define TAB_Y 56
#define TAB_H 15
#define PAN_Y0 71
#define PAN_Y1 225
#define PAN_H (PAN_Y1 - PAN_Y0)
#define KEY_Y 225
#define LEFT 6
#define WIDTH 308
#define BTN_H 17

#define C_BLACK 0x0000
#define C_WHITE 0xFFFF
#define C_TEXT HEX(0x111111)
#define C_DIM HEX(0x666666)
#define C_GREY HEX(0x808080)
#define C_LINE HEX(0xB4B4B4)
#define C_FOCUS HEX(0x1A6FE8)
#define C_PROJECT HEX(0xC8C8C8)

static const np_font_t *const BODY = &np_font_body, *const SMALL = &np_font_small, *const TITLE = &np_font_title;

/* ---------------------------------------------------------------- text, with the four letters past ASCII */
static int glyph_at(uint8_t b) {
  if (b >= 32 && b < 127) return b - 32;
  switch (b) {
    case 0xA9: return 95;   /* the font's extra letters, in code point order */
    case 0xE9: return 96;
    case 0xF3: return 97;
    case 0xF8: return 98;
    default: return '?' - 32;
  }
}
static int text_w(const np_font_t *f, const char *s) {
  int w = 0;
  for (; *s; s++) w += f->glyphs[glyph_at((uint8_t)*s)].advance;
  return w;
}
static int text_wn(const np_font_t *f, const char *s, int n) {
  int w = 0;
  for (int i = 0; i < n && s[i]; i++) w += f->glyphs[glyph_at((uint8_t)s[i])].advance;
  return w;
}
static void draw_text(const np_font_t *f, int x, int base, const char *s, color_t c, int a) {
  if (base + f->descent <= gfx_y0 || base - f->ascent >= gfx_y1) return;
  for (; *s; s++) {
    const np_glyph_t *gl = &f->glyphs[glyph_at((uint8_t)*s)];
    if (gl->w) gfx_mask(f->data + gl->offset, gl->w, gl->h, x + gl->x, base + gl->y, c, a);
    x += gl->advance;
  }
}
/* the next line of s that fits in w pixels (broken after a space when it can): its length; *next past it */
static int wrap(const np_font_t *f, const char *s, int w, const char **next) {
  int n = 0, last = -1, px = 0;
  while (s[n] && s[n] != '\n') {
    px += f->glyphs[glyph_at((uint8_t)s[n])].advance;
    if (px > w && n > 0) break;
    if (s[n] == ' ') last = n;
    n++;
  }
  int len = n;
  if (s[n] && s[n] != '\n' && last > 0) len = last;
  const char *p = s + len;
  while (*p == ' ') p++;
  if (*p == '\n') p++;
  *next = p;
  return len;
}

/* ---------------------------------------------------------------- the frame's drawing steps */
enum { K_FILL, K_GRAD, K_FRAME, K_TEXT, K_CANVAS, K_ALPHA };
typedef struct {
  int16_t x, y, w, h, cy0, cy1;
  uint8_t kind, a;
  color_t c;
  uint32_t top, bottom;
  const np_font_t *font;
  const char *s;
} op_t;
#define MAX_OPS 900
static op_t ops[MAX_OPS];
static int nops;
static char pool[16000];
static int npool;
static int16_t clip0 = 0, clip1 = SCREEN_H;

static char *P(int n) {
  if (npool + n > (int)sizeof pool) npool = 0;   /* (never: the pool fits a frame) */
  char *p = pool + npool;
  npool += n;
  *p = 0;
  return p;
}
static op_t *op(int kind, int x, int y, int w, int h) {
  if (nops >= MAX_OPS) return &ops[MAX_OPS - 1];
  op_t *o = &ops[nops++];
  memset(o, 0, sizeof *o);
  o->kind = (uint8_t)kind, o->x = (int16_t)x, o->y = (int16_t)y, o->w = (int16_t)w, o->h = (int16_t)h;
  o->cy0 = clip0, o->cy1 = clip1, o->a = 255;
  return o;
}
static void fill(int x, int y, int w, int h, color_t c) { op(K_FILL, x, y, w, h)->c = c; }
static void fill_a(int x, int y, int w, int h, color_t c, int a) {
  op_t *o = op(K_ALPHA, x, y, w, h);
  o->c = c, o->a = (uint8_t)a;
}
static void grad(int x, int y, int w, int h, uint32_t top, uint32_t bottom) {
  op_t *o = op(K_GRAD, x, y, w, h);
  o->top = top, o->bottom = bottom;
}
static void frame(int x, int y, int w, int h, int t, color_t c) {
  op_t *o = op(K_FRAME, x, y, w, h);
  o->c = c, o->a = (uint8_t)t;
}
static void text(const np_font_t *f, int x, int base, const char *s, color_t c) {
  op_t *o = op(K_TEXT, x, base, 0, 0);
  o->font = f, o->s = s, o->c = c;
}
static void text_a(const np_font_t *f, int x, int base, const char *s, color_t c, int a) {
  op_t *o = op(K_TEXT, x, base, 0, 0);
  o->font = f, o->s = s, o->c = c, o->a = (uint8_t)a;
}

/* strings for this frame */
static const char *S(const char *a, const char *b, const char *c, const char *d, const char *e) {
  const char *in[5] = {a, b, c, d, e};
  int n = 1;
  for (int i = 0; i < 5; i++)
    if (in[i]) n += (int)strlen(in[i]);
  char *p = P(n);
  for (int i = 0; i < 5; i++)
    if (in[i]) strcat(p, in[i]);
  return p;
}
#define S2(a, b) S(a, b, 0, 0, 0)
#define S3(a, b, c) S(a, b, c, 0, 0)
static const char *cm(double v, int d) { return js_commas(v, d, P(120), 120); }
static const char *sp(double v) { return js_spellf(v, P(80), 80); }
static const char *num(double v) { return js_tostring(v, P(40), 40); }
static const char *cr(double v, int d) { return js_cruncher(v, d, P(60), 60); }

/* ---------------------------------------------------------------- the panel being laid out */
typedef struct {
  int16_t id, x, y, w, h;
} hit_t;
static hit_t hits[200];
static int nhits, cy, content_h;
static pc_ui *U;

static bool focused(int id) { return U && U->focus[U->tab] == id; }

static void heading(const char *s) {
  cy += 2;
  text(BODY, LEFT, cy + 11, s, C_BLACK);
  fill(LEFT, cy + 15, WIDTH, 1, C_LINE);
  cy += 19;
}
static void line(const char *s) {   /* a line of text; too long, it goes on under, indented */
  int x = LEFT, w = WIDTH;
  while (*s) {
    const char *next;
    int n = wrap(BODY, s, w, &next);
    char *t = P(n + 1);
    memcpy(t, s, (size_t)n), t[n] = 0;
    text(BODY, x, cy + 11, t, C_TEXT);
    cy += 14;
    s = next, x = LEFT + 12, w = WIDTH - 12;
  }
}
static void line_small(const char *s, color_t c) {
  const char *p = s;
  while (*p) {
    const char *next;
    int n = wrap(SMALL, p, WIDTH, &next);
    char *t = P(n + 1);
    memcpy(t, p, (size_t)n), t[n] = 0;
    text(SMALL, LEFT, cy + 9, t, c);
    cy += 11;
    p = next;
  }
}
/* a button at x on this row: its right edge */
static int button(int id, int x, const char *label) {
  bool on = pc_enabled(id), f = focused(id);
  int w = text_w(BODY, label) + 12;
  if (on) {
    grad(x, cy, w, BTN_H, 0xFFFFFF, 0x9A9A9A);
    frame(x, cy, w, BTN_H, 1, HEX(0x1A1A1A));
    text(BODY, x + 6, cy + 12, label, C_BLACK);
  } else {
    grad(x, cy, w, BTN_H, 0xFFFFFF, 0xD7D7D7);
    frame(x, cy, w, BTN_H, 1, HEX(0xC4C4C4));
    text(BODY, x + 6, cy + 12, label, HEX(0x8C8C8C));
  }
  if (f) frame(x - 2, cy - 2, w + 4, BTN_H + 4, 2, C_FOCUS);
  if (nhits < (int)(sizeof hits / sizeof hits[0])) hits[nhits++] = (hit_t){(int16_t)id, (int16_t)x, (int16_t)cy, (int16_t)w, BTN_H};
  return x + w;
}
static void after(int x, const char *s) { text(BODY, x + 6, cy + 12, s, C_TEXT); }
static void row_end(void) { cy += BTN_H + 4; }

static const char *inches(double w) { return w == 1 ? " inch" : " inches"; }

/* ---------------------------------------------------------------- the tabs' contents */
static void tab_clips(void) {
  button(B_CLIP, LEFT, "Make Paperclip");
  row_end();
  if (g->prestigeU >= 1 || g->prestigeS >= 1) {
    fill(LEFT, cy, WIDTH, 14, HEX(0xD3D3D3));
    line(S("Universe: ", num(g->prestigeU + 1), " / Sim Level: ", num(g->prestigeS + 1), 0));
  }
  int x;
  if (g->humanFlag == 1) {
    heading("Manufacturing");
    line(S2("Clips per Second: ", cm(js_round(g->clipRate), 0)));
    if (g->wireBuyerFlag == 1) {
      x = button(B_WIREBUYER, LEFT, "WireBuyer");
      after(x, g->wireBuyerStatus == 1 ? "ON" : "OFF");
      row_end();
    }
    x = button(B_WIRE, LEFT, "Wire");
    after(x, S(cm(g->wire, 0), " inches", "    Cost: $", num(g->wireCost), 0));
    row_end();
    if (g->autoClipperFlag != 0) {
      x = button(B_CLIPPER, LEFT, "AutoClippers");
      after(x, S(num(g->clipmakerLevel), "    Cost: $", cm(g->clipperCost, 2), 0, 0));
      row_end();
    }
    if (g->megaClipperFlag != 0) {
      x = button(B_MEGA, LEFT, "MegaClippers");
      after(x, S(num(g->megaClipperLevel), "    Cost: $", cm(g->megaClipperCost, 2), 0, 0));
      row_end();
    }
    return;
  }
  if (g->endTimer6 >= 250) return;
  heading("Manufacturing");
  if (g->flag[P45] == 1 && g->maxFactoryLevel < 50) {
    double n = g->maxFactoryLevel < 10 ? 10 : g->maxFactoryLevel < 20 ? 20 : 50;
    line(S3("Next Upgrade at: ", cm(n, 0), " Factories"));
  }
  if (g->dismantle < 3) line(S2("Clips per Second: ", sp(g->clipRate)));
  if (g->tothFlag == 1 && g->dismantle < 3) line(S2("Unused Clips: ", sp(g->unusedClips)));
  if (g->factoryFlag == 1 && g->spaceFlag == 0) {
    x = button(B_FACTORY, LEFT, "Clip Factory");
    after(x, num(g->factoryLevel));
    row_end();
    x = button(B_FACTORY_REBOOT, LEFT, "Disassemble All");
    after(x, S3("+", sp(g->factoryBill), " clips"));
    row_end();
    line(S3("Cost: ", sp(g->factoryCost), " clips"));
  }
  if (g->wireProductionFlag == 0 || g->dismantle >= 2)
    line(S3("Wire: ", g->dismantle >= 4 ? cm(g->wire, 0) : sp(g->wire), inches(g->wire)));
  if (g->spaceFlag == 1 && g->dismantle < 3) line(S2("Factories: ", sp(g->factoryLevel)));
}

static void tab_business(void) {
  heading("Business");
  line(S2("Available Funds: $", cm(g->funds, 2)));
  if (g->revPerSecFlag == 1) {
    line(S2("Avg. Rev. per sec: $", cm(g->avgRev, 2)));
    line(S2("Avg. Clips Sold per sec: ", cm(js_round(g->avgSales), 0)));
  }
  line(S2("Unsold Inventory: ", cm(g->unsoldClips, 0)));
  int x = button(B_LOWER, LEFT, "lower");
  x = button(B_RAISE, x + 6, "raise");
  after(x, S2("Price per Clip: $", js_tofixed(g->margin, 2, P(40), 40)));
  row_end();
  line(S3("Public Demand: ", cm(g->demand * 10, 0), "%"));
  x = button(B_ADS, LEFT, "Marketing");
  after(x, S2("Level: ", num(g->marketingLvl)));
  row_end();
  line(S2("Cost: $", cm(g->adCost, 2)));
}

static void drone_rows(int b1, const char *name, double level, double bill, double cost) {
  int x = button(b1, LEFT, name);
  after(x, cm(level, 0));
  row_end();
  x = button(b1 + 1, LEFT, "+10");
  x = button(b1 + 2, x + 6, "+100");
  if (b1 != B_FARM1 && b1 != B_BATT1) button(b1 + 3, x + 6, "+1k");
  row_end();
  int reboot = b1 == B_FARM1 ? B_FARM_REBOOT : b1 == B_BATT1 ? B_BATT_REBOOT : b1 + 4;
  x = button(reboot, LEFT, "Disassemble All");
  after(x, S3("+", sp(bill), " clips"));
  row_end();
  line(S3("Cost: ", sp(cost), " clips"));
}

static void tab_wire(void) {
  heading("Wire Production");
  if (g->maxDroneLevel < 50000) {
    double n = g->maxDroneLevel < 500 ? 500 : g->maxDroneLevel < 5000 ? 5000 : 50000;
    line(S3("Next Upgrade at: ", cm(n, 0), " Drones"));
  }
  line(S3("Available Matter: ", sp(g->availableMatter), " g"));
  if (g->spaceFlag == 1) line(S3("(", sp(g->mdps), " g per sec)"));
  line(S(
    "Acquired Matter: ", sp(g->acquiredMatter), " g (", g->maps == 0 ? "0" : sp(g->maps), " g per sec)"));
  line(S("Wire: ", sp(g->wire), " inches (", g->wpps == 0 ? "0" : sp(g->wpps), " inches per sec)"));
  if (g->spaceFlag == 0) {
    if (g->harvesterFlag == 1) drone_rows(B_HARV1, "Harvester Drone", g->harvesterLevel, g->harvesterBill, g->harvesterCost);
    if (g->wireDroneFlag == 1) drone_rows(B_WIRED1, "Wire Drone", g->wireDroneLevel, g->wireDroneBill, g->wireDroneCost);
  } else {
    line(S2("Harvester Drones: ", sp(g->harvesterLevel)));
    line(S2("Wire Drones: ", sp(g->wireDroneLevel)));
  }
}

static void tab_power(void) {
  heading("Power");
  double supply = g->farmLevel * 50 / 100;
  double dDemand = (g->harvesterLevel * 1.0 / 100) + (g->wireDroneLevel * 1.0 / 100);
  double fDemand = (g->factoryLevel * 200 / 100), demand = dDemand + fDemand;
  bool none = g->factoryLevel == 0 && g->harvesterLevel == 0 && g->wireDroneLevel == 0;
  line(S3("Factory/Drone Performance: ", none ? "0" : cm(js_round(g->powMod * 100), 0), "%"));
  line(S3("Consumption: ", cm(js_round(demand * 100), 0), " MW"));
  line(S("Factories: ", cm(js_round(fDemand * 100), 0), " MW    Drones: ", cm(js_round(dDemand * 100), 0), " MW"));
  line(S3("Production: ", cm(js_round(supply * 100), 0), " MW"));
  drone_rows(B_FARM1, "Solar Farm", g->farmLevel, g->farmBill, g->farmCost);
  line(S("Storage: ", cm(js_round(g->storedPower), 0), " / ", cm(js_round(g->batteryLevel * 10000), 0), " MW-seconds"));
  drone_rows(B_BATT1, "Battery Tower", g->batteryLevel, g->batteryBill, g->batteryCost);
}

static void tab_probes(void);
static void space_exploration(void);
static void tab_space(void) {
  if (!(g->dismantle >= 1 && g->endTimer1 >= 150)) space_exploration();
  tab_probes();
}
static void space_exploration(void) {
  heading("Space Exploration");
  line(S2(js_tofixed(100 / (g->totalMatter / g->foundMatter), 12, P(40), 40), "% of universe explored"));
  int x = button(B_PROBE, LEFT, "Launch Probe");
  after(x, S3("Cost: ", sp(g->probeCost), " clips"));
  row_end();
  line(S2("Launched: ", cm(g->probeLaunchLevel, 0)));
  line(S2("Descendents: ", sp(g->probeDescendents)));
  if (g->probesLostHaz >= 1) line(S3("Lost to hazards: (", sp(g->probesLostHaz), ")"));
  if (g->probesLostDrift >= 1) line(S3("Lost to value drift: (", sp(g->probesLostDrift), ")"));
  if (g->probesLostCombat >= 1) line(S3("Lost in combat: (", cr(g->probesLostCombat, 2), ")"));
  line(S2("Total: ", sp(g->probeCount)));
  if (g->battleFlag == 1) {
    line(S2("Drifters Killed: ", cr(g->driftersKilled, 2)));
    line(S2("Drifters: ", sp(g->drifterCount)));
  }
}

static const char *const STAT_NAME[8] = {"Speed", "Exploration", "Self-Replication", "Hazard Remediation",
                                         "Factory Production", "Harvester Drone Production", "Wire Drone Production",
                                         "Combat"};
static const char *const STAT_DESC[8] = {
  "Modifies rate of exploration", "Rate at which probes gain access to new matter",
  "Rate at which probes generate more probes (each new probe costs 100 quadrillion clips)",
  "Reduces damage from dust, junk, radiation, and general entropic decay",
  "Rate at which probes build factories (each new factory costs 100 million clips)",
  "Rate at which probes spawn Harvester Drones (each new drone costs 2 million clips)",
  "Rate at which probes spawn Wire Drones (each new drone costs 2 million clips)",
  "Determines offensive and defensive effectiveness in battle"};

static void tab_probes(void) {
  if (g->dismantle < 1) {
    heading("Von Neumann Probe Design");
    line(S(S3("Trust: ", num(g->probeUsedTrust), " / "), num(g->probeTrust), " (", cm(g->maxTrust, 0), " Max)"));
    const double *stat[8] = {&g->probeSpeed, &g->probeNav, &g->probeRep, &g->probeHaz, &g->probeFac,
                             &g->probeHarv, &g->probeWire, &g->probeCombat};
    for (int k = 0; k < 8; k++) {
      if (k == 7 && g->flag[P131] == 0) break;
      int x = button(B_STAT_DOWN + k, LEFT, "<");
      x = button(B_STAT_UP + k, x + 4, ">");
      after(x, S3(STAT_NAME[k], ": ", num(*stat[k])));
      row_end();
      cy -= 2;
      line_small(STAT_DESC[k], C_DIM);
      cy += 3;
    }
  }
  if (g->dismantle < 1 || g->endTimer1 < 50) {
    int x = button(B_PROBE_TRUST, LEFT, "Increase Probe Trust");
    after(x, S3("Cost: ", cm(js_floor(g->probeTrustCost), 0), " yomi"));
    row_end();
  }
  if (g->flag[P121] == 1 && (g->dismantle < 1 || g->endTimer1 < 100)) {
    int x = button(B_MAX_TRUST, LEFT, "Increase Max Trust");
    after(x, S3("Cost: ", cm(g->maxTrustCost, 2), " honor"));
    row_end();
  }
}

static void tab_combat(void) {
  op_t *o = op(K_CANVAS, 5, cy, 310, 150);
  (void)o;
  int y = cy;
  text(BODY, 15, y + 18, "Combat", C_WHITE);
  text(BODY, 15, y + 33, g->battleName, C_WHITE);
  if (g->battleResult) {
    text(TITLE, 15, y + 60, g->battleResult == 1 ? "VICTORY" : "DEFEAT", C_WHITE);
    text(BODY, 15, y + 76, S3(g->battleResult == 1 ? "+" : "-", num(g->honorAmount), " honor"), C_WHITE);
  }
  text(SMALL, 15, y + 144, S3("Scale = ", cr(g->unitSize, 0), ":1"), C_WHITE);
  cy += 152;
  if (g->flag[P121] == 1 && (g->dismantle < 1 || g->endTimer1 < 190)) line(S2("Honor: ", cm(js_round(g->honor), 0)));
}

static const char *const STATUS[10] = {"Active", "Hungry", "Confused", "Bored", "Cold", "Disorganized", "Sleeping",
                                       "", "Lonely", "NO RESPONSE..."};
static const int16_t CHIP_GONE[PC_CHIPS] = {174, 172, 169, 165, 160, 150, 130, 100, 60, 10};

static void tab_computer(void) {
  heading("Computational Resources");
  if (g->humanFlag == 1) {
    line(S2("Trust: ", cm(g->trust, 0)));
    line(S3("+1 Trust at: ", cm(js_floor(g->nextTrust), 0), " clips"));
  }
  if (g->swarmFlag == 1 && !(g->dismantle >= 2 && g->endTimer2 >= 50)) line(S2("Swarm Gifts: ", cm(g->swarmGifts, 0)));
  int x;
  if (g->dismantle < 6) {
    x = button(B_PROC, LEFT, "Processors");
    after(x, num(g->processors));
    row_end();
  }
  x = button(B_MEM, LEFT, "Memory");
  after(x, num(g->memory));
  row_end();
  line(S("Operations: ", cm(g->operations, 0), " / ", cm(g->memory * 1000, 0), 0));
  if (g->creativityOn != 0) line(S2("Creativity: ", cm(g->creativity, 0)));
  if (g->swarmFlag == 1 && !(g->dismantle >= 2 && g->endTimer2 >= 100)) {
    heading("Swarm Computing");
    line(S2("Drones: ", sp(js_floor(g->harvesterLevel + g->wireDroneLevel))));
    int st = (int)g->swarmStatus;
    if (st != 7 && st >= 0 && st < 10) line(S2("Status: ", STATUS[st]));
    if (st == 0) line(S2("Next gift in ", js_time(g->giftCountdown, P(80), 80)));
    if (st == 3) {
      x = button(B_ENTERTAIN, LEFT, "Entertain the Swarm");
      after(x, S3("Cost: ", cm(g->entertainCost, 0), " creat"));
      row_end();
    }
    if (st == 5) {
      x = button(B_SYNCH, LEFT, "Synchronize the Swarm");
      after(x, S3("Cost: ", cm(g->synchCost, 0), " yomi"));
      row_end();
    }
    if (!(g->dismantle >= 2 && g->endTimer2 >= 150)) {
      x = button(B_WORK, LEFT, "< Work");
      int right = LEFT + WIDTH - (text_w(BODY, "Think >") + 12);
      int x0 = x + 8, x1 = right - 8;
      fill(x0, cy + 8, x1 - x0, 2, C_LINE);
      int k = x0 + (int)((x1 - x0 - 8) * g->sliderPos / 200);
      grad(k, cy + 2, 8, 13, 0xFFFFFF, 0x9A9A9A);
      frame(k, cy + 2, 8, 13, 1, HEX(0x1A1A1A));
      button(B_THINK, right, "Think >");
      row_end();
    }
  }
  if (g->qFlag == 1 && !(g->dismantle >= 5 && g->endTimer4 >= 250)) {
    heading("Quantum Computing");
    for (int i = 0; i < PC_CHIPS; i++) {
      if (g->dismantle >= 5 && g->endTimer4 >= CHIP_GONE[i]) continue;
      double v = g->qChipValue[i];
      int a = v <= 0 ? 0 : v >= 1 ? 255 : (int)(v * 255);
      int cx = LEFT + 2 + i * 26;
      frame(cx, cy, 22, 22, 1, HEX(0xE0E0E0));
      if (a) fill_a(cx, cy, 22, 22, C_BLACK, a);
    }
    cy += 28;
    if (g->dismantle < 5) {
      x = button(B_QCOMP, LEFT, "Compute");
      double f = g->qFade < 0 ? 0 : g->qFade > 1 ? 1 : g->qFade;
      if (g->qShown == 1) text_a(BODY, x + 6, cy + 12, "Need Photonic Chips", C_TEXT, (int)(f * 255));
      if (g->qShown == 2) text_a(BODY, x + 6, cy + 12, S2("qOps: ", cm(g->qOpsShown, 0)), C_TEXT, (int)(f * 255));
      row_end();
    }
  }
}

static void tab_projects(void) {
  if (!g->nactive) {
    cy += 4;
    line_small("No projects for now.", C_GREY);
    return;
  }
  for (int k = 0; k < g->nactive; k++) {
    int i = g->active[k];
    int id = B_PROJECT + i;
    bool on = pc_enabled(id), f = focused(id);
    char *title = P(PC_NAME_LEN + 40), *price = P(64);
    pc_project_title(i, title, PC_NAME_LEN + 40);
    pc_project_price(i, price, 64);
    const char *head = *price ? S3(title, " ", price) : title;
    /* the title and price, then the description, in the box */
    const char *lines[8];
    int nl = 0;
    const char *p = head;
    while (*p && nl < 3) {
      const char *next;
      int n = wrap(BODY, p, WIDTH - 10, &next);
      char *t = P(n + 1);
      memcpy(t, p, (size_t)n), t[n] = 0;
      lines[nl++] = t, p = next;
    }
    int nh = nl;
    p = PC_PROJECT_TEXT[i].desc;
    while (*p && nl < 8) {
      const char *next;
      int n = wrap(SMALL, p, WIDTH - 10, &next);
      char *t = P(n + 1);
      memcpy(t, p, (size_t)n), t[n] = 0;
      lines[nl++] = t, p = next;
    }
    int h = 6 + nh * 13 + (nl - nh) * 11;
    bool hidden = g->blinkProject == i && (g->blinkCount & 1);
    if (!hidden) {
      fill(LEFT, cy, WIDTH, h, on ? C_PROJECT : HEX(0xDDDDDD));
      if (on) frame(LEFT, cy, WIDTH, h, 1, C_BLACK);
      int y = cy + 3;
      for (int l = 0; l < nl; l++) {
        bool big = l < nh;
        text(big ? BODY : SMALL, LEFT + 5, y + (big ? 11 : 9), lines[l], on ? (big ? C_BLACK : C_TEXT) : HEX(0x8A8A8A));
        y += big ? 13 : 11;
      }
    }
    if (f) frame(LEFT - 2, cy - 2, WIDTH + 4, h + 4, 2, C_FOCUS);
    if (nhits < (int)(sizeof hits / sizeof hits[0]))
      hits[nhits++] = (hit_t){(int16_t)id, LEFT, (int16_t)cy, WIDTH, (int16_t)h};
    if (U) U->seen[i] = 1;
    cy += h + 6;
  }
}

static const char *const RISK_NAME[3] = {"Low Risk", "Med Risk", "High Risk"};

static void tab_invest(void) {
  heading("Investments");
  int x = button(B_RISK, LEFT, RISK_NAME[g->riskChoice % 3]);
  x = button(B_DEPOSIT, x + 10, "Deposit");
  button(B_WITHDRAW, x + 6, "Withdraw");
  row_end();
  line(S2("Cash: $", cm(g->bankroll, 0)));
  line(S2("Stocks: $", cm(g->secTotal, 0)));
  line(S2("Total: $", cm(g->portTotal, 0)));
  static const int16_t COL[5] = {LEFT, LEFT + 52, LEFT + 118, LEFT + 180, LEFT + 248};
  static const char *const HEAD[5] = {"Stock", "Amt.", "Price", "Total", "P/L"};
  cy += 2;
  fill(LEFT, cy, WIDTH, 13, HEX(0xE4E4E4));
  for (int c = 0; c < 5; c++) text(SMALL, COL[c] + 2, cy + 10, HEAD[c], C_BLACK);
  cy += 14;
  for (int r = 0; r < PC_STOCKS; r++) {
    if (r < g->nstocks) {
      const pc_stock *s = &g->stocks[r];
      text(SMALL, COL[0] + 2, cy + 9, s->symbol, C_TEXT);
      text(SMALL, COL[1] + 2, cy + 9, num(js_ceil(s->amount)), C_TEXT);
      text(SMALL, COL[2] + 2, cy + 9, num(js_ceil(s->price)), C_TEXT);
      text(SMALL, COL[3] + 2, cy + 9, num(js_ceil(s->total)), C_TEXT);
      text(SMALL, COL[4] + 2, cy + 9, num(js_ceil(s->profit)), C_BLACK);
    }
    fill(LEFT, cy + 11, WIDTH, 1, HEX(0xE4E4E4));
    cy += 12;
  }
  cy += 4;
  x = button(B_INVEST_UP, LEFT, "Upgrade Investment Engine");
  after(x, S2("Level: ", num(g->investLevel)));
  row_end();
  line(S3("Cost: ", cm(g->investUpgradeCost, 0), " Yomi"));
}

static void tab_strategy(void) {
  heading("Strategic Modeling");
  int x = button(B_PICK, LEFT, g->pickChoice >= 10 ? "Pick a Strat" : pc_strat_name(g->pickChoice));
  button(B_RUN, x + 6, "Run");
  row_end();
  line_small(g->tourneyText, C_TEXT);
  cy += 2;
  /* the payoff grid: the column player's name above, the row player's to the left */
  bool named = g->tourneyLvl > 1;
  const char *a = named ? pc_choice_name(g->gridLabel, 0) : "Move A", *b = named ? pc_choice_name(g->gridLabel, 1) : "Move B";
  int c0 = LEFT + 140, c1 = LEFT + 210, cw = 66;
  if (g->v >= 0 && g->v < g->nstrats && g->tourneyInProg) text(SMALL, c0, cy + 9, pc_strat_name(g->v), C_BLACK);
  cy += 12;
  text(SMALL, c0 + 2, cy + 9, a, C_TEXT);
  text(SMALL, c1 + 2, cy + 9, b, C_TEXT);
  cy += 12;
  const char *cells[4] = {S3(num(g->aa), ",", num(g->aa)), S3(num(g->ab), ",", num(g->ba)),
                          S3(num(g->ba), ",", num(g->ab)), S3(num(g->bb), ",", num(g->bb))};
  if (g->h >= 0 && g->h < g->nstrats && g->tourneyInProg) text(SMALL, LEFT, cy + 15, pc_strat_name(g->h), C_BLACK);
  for (int r = 0; r < 2; r++) {
    text(SMALL, LEFT + 72, cy + 10, r ? b : a, C_TEXT);
    for (int c = 0; c < 2; c++) {
      int cx = c ? c1 : c0, cell = r * 2 + c + 1;
      fill(cx, cy, cw, 13, g->lastCell == cell ? HEX(0xD3D3D3) : C_WHITE);
      frame(cx, cy, cw, 13, 1, C_LINE);
      text(SMALL, cx + 4, cy + 10, cells[r * 2 + c], C_BLACK);
    }
    cy += 13;
  }
  cy += 4;
  if (g->resultsShown) {
    for (int i = 0; i < g->nresults; i++) {
      int s = g->results[i];
      const char *t = S(num(i + 1), ". ", pc_strat_name(s), ": ", num(g->stratScore[s]));
      if (s == g->pick) fill(LEFT, cy, text_w(SMALL, t) + 6, 11, HEX(0xE4E4E4));
      text(SMALL, LEFT + 3, cy + 9, t, s == g->pick ? C_BLACK : C_TEXT);
      cy += 11;
    }
    cy += 3;
  }
  line(S2("Yomi: ", cm(g->yomi, 0)));
  x = button(B_NEW_TOURNEY, LEFT, "New Tournament");
  after(x, S3("Cost: ", cm(g->tourneyCost, 0), " ops"));
  row_end();
  if (g->autoTourneyFlag == 1) {
    x = button(B_AUTOTOURNEY, LEFT, "AutoTourney");
    after(x, g->autoTourneyStatus == 1 ? "ON" : "OFF");
    row_end();
  }
}

bool ui_tab_shown(int t) {
  switch (t) {
    case T_CLIPS: return true;
    case T_BUSINESS: return g->humanFlag == 1;
    case T_WIRE: return g->humanFlag == 0 && g->wireProductionFlag == 1 && g->dismantle < 2;
    case T_POWER: return g->flag[P127] == 1 && g->spaceFlag == 0;
    case T_SPACE:   /* Space Exploration and the probes' design */
      return g->spaceFlag == 1 && (g->dismantle < 1 || g->endTimer1 < 150);
    case T_PROBES: return false;
    case T_COMBAT: return g->battleFlag == 1 && !(g->dismantle >= 1 && g->endTimer1 >= 190);
    case T_COMPUTER: return g->compFlag == 1 && g->dismantle < 7;
    case T_PROJECTS: return g->projectsFlag == 1 && g->dismantle < 7;
    case T_INVEST: return g->investmentEngineFlag == 1;
    case T_STRATEGY: return g->strategyEngineFlag == 1 && g->dismantle < 4;
    default: return false;
  }
}

static const char *const TAB_NAME[T_COUNT] = {"Clips", "Business", "Wire", "Power", "Space", "Probes", "Combat",
                                              "Computer", "Projects", "Invest", "Strategy"};

static void build_tab(int t) {
  switch (t) {
    case T_CLIPS: tab_clips(); break;
    case T_BUSINESS: tab_business(); break;
    case T_WIRE: tab_wire(); break;
    case T_POWER: tab_power(); break;
    case T_SPACE: tab_space(); break;
    case T_PROBES: tab_probes(); break;
    case T_COMBAT: tab_combat(); break;
    case T_COMPUTER: tab_computer(); break;
    case T_PROJECTS: tab_projects(); break;
    case T_INVEST: tab_invest(); break;
    default: tab_strategy(); break;
  }
}

/* ---------------------------------------------------------------- focus and keys */
static void fix_tab(pc_ui *u) {
  if (!ui_tab_shown(u->tab)) u->tab = T_CLIPS;
}

static int focus_index(pc_ui *u) {
  for (int i = 0; i < nhits; i++)
    if (hits[i].id == u->focus[u->tab]) return i;
  return -1;
}

/* the panel laid out (ops kept from `first` on, in the panel's own coordinates) */
static int panel(pc_ui *u) {
  U = u;
  nhits = 0, cy = 4;
  int first = nops;
  build_tab(u->tab);
  content_h = cy + 4;
  int i = focus_index(u);
  if (i < 0 && nhits) {
    i = u->focus_at[u->tab];
    if (i < 0) i = 0;
    if (i >= nhits) i = nhits - 1;
    u->focus[u->tab] = hits[i].id;
    /* (the frame is drawn with the next layout) */
  }
  if (i >= 0) u->focus_at[u->tab] = i;
  int maxs = content_h > PAN_H ? content_h - PAN_H : 0;
  if (u->scroll[u->tab] > maxs) u->scroll[u->tab] = maxs;
  if (u->scroll[u->tab] < 0) u->scroll[u->tab] = 0;
  return first;
}

static void see(pc_ui *u, int i) {   /* scrolls the button into view */
  if (i < 0 || i >= nhits) return;
  int top = hits[i].y - 6, bottom = hits[i].y + hits[i].h + 6;
  int *s = &u->scroll[u->tab];
  if (top < *s) *s = top;
  if (bottom > *s + PAN_H) *s = bottom - PAN_H;
  if (*s < 0) *s = 0;
}

void ui_layout(pc_ui *u) {
  nops = 0, npool = 0;
  fix_tab(u);
  panel(u);
}

int ui_buttons(pc_ui *u, int *out, int max) {
  ui_layout(u);
  int n = 0;
  for (int i = 0; i < nhits && n < max; i++) out[n++] = hits[i].id;
  return n;
}

void ui_up(pc_ui *u) {
  ui_layout(u);
  int i = focus_index(u);
  if (i > 0) {
    u->focus[u->tab] = hits[i - 1].id, u->focus_at[u->tab] = i - 1;
    see(u, i - 1);
  } else {
    u->scroll[u->tab] -= 40;
    if (u->scroll[u->tab] < 0) u->scroll[u->tab] = 0;
  }
}

void ui_down(pc_ui *u) {
  ui_layout(u);
  int i = focus_index(u);
  if (i >= 0 && i + 1 < nhits) {
    u->focus[u->tab] = hits[i + 1].id, u->focus_at[u->tab] = i + 1;
    see(u, i + 1);
  } else {
    int maxs = content_h > PAN_H ? content_h - PAN_H : 0;
    u->scroll[u->tab] += 40;
    if (u->scroll[u->tab] > maxs) u->scroll[u->tab] = maxs;
  }
}

void ui_tab(pc_ui *u, int d) {
  fix_tab(u);
  for (int k = 1; k <= T_COUNT; k++) {
    int t = ((u->tab + d * k) % T_COUNT + T_COUNT) % T_COUNT;
    if (ui_tab_shown(t)) {
      u->tab = t;
      break;
    }
  }
  ui_layout(u);
  see(u, focus_index(u));
}

int ui_focused(pc_ui *u) {
  ui_layout(u);
  int i = focus_index(u);
  if (i < 0) return -1;
  /* out of view: OK brings it back first */
  int s = u->scroll[u->tab];
  if (hits[i].y + hits[i].h < s || hits[i].y > s + PAN_H) {
    see(u, i);
    return -1;
  }
  return hits[i].id;
}

bool ui_repeats(int b) {
  switch (b) {
    case B_CLIP: case B_WIRE: case B_CLIPPER: case B_MEGA: case B_LOWER: case B_RAISE: case B_ADS: case B_PROC:
    case B_MEM: case B_QCOMP: case B_INVEST_UP: case B_FACTORY: case B_HARV1: case B_HARV10: case B_HARV100:
    case B_HARV1000: case B_WIRED1: case B_WIRED10: case B_WIRED100: case B_WIRED1000: case B_FARM1: case B_FARM10:
    case B_FARM100: case B_BATT1: case B_BATT10: case B_BATT100: case B_WORK: case B_THINK: case B_PROBE:
    case B_PROBE_TRUST: case B_MAX_TRUST: return true;
    default: return b >= B_STAT_DOWN && b < B_STAT_UP + 8;
  }
}

void ui_init(pc_ui *u) {
  memset(u, 0, sizeof *u);
  for (int t = 0; t < T_COUNT; t++) u->focus[t] = -1;
}

/* ---------------------------------------------------------------- the frame */
static void console(void) {
  fill(0, 0, SCREEN_W, CON_H, C_BLACK);
  /* the newest message at the bottom, white after ">", older ones grey above it */
  const char *lines[3];
  color_t col[3];
  int n = 0;
  for (int m = 0; m < PC_MSG && n < 3; m++) {
    const char *s = g->msg[m];
    if (!*s) continue;
    const char *part[6];
    int np = 0;
    const char *p = s;
    while (*p && np < 6) {
      const char *next;
      int len = wrap(SMALL, p, 300, &next);
      char *t = P(len + 1);
      memcpy(t, p, (size_t)len), t[len] = 0;
      part[np++] = t, p = next;
    }
    for (int k = np - 1; k >= 0 && n < 3; k--) {
      lines[n] = part[k], col[n] = m == 0 ? C_WHITE : C_GREY;
      n++;
    }
    if (m == 0 && np > 3) break;
  }
  for (int i = 0; i < n; i++) {
    int y = 31 - 11 * i;
    bool first = (i == n - 1) || col[i] != col[i + 1];
    (void)first;
    text(SMALL, 14, y, lines[i], col[i]);
  }
  /* the prompt before the newest */
  int newest = 0;
  while (newest + 1 < n && col[newest + 1] == C_WHITE) newest++;
  if (n) text(SMALL, 4, 31 - 11 * newest, ">", C_WHITE);
}

static void header(void) {
  fill(0, HDR_Y, SCREEN_W, TAB_Y - HDR_Y, C_WHITE);
  const char *digits;
  const char *crunched = 0;
  if (g->milestoneFlag < 15) {
    digits = cm(js_ceil(g->clips), 0);
  } else if (g->dismantle == 0) {
    digits = g->milestoneFlag == 15 ? "29,999,999,999,999,900,000,000,000,000,000,000,000,000,000,000,000,000,000"
                                    : cm(js_ceil(g->clips), 0);
  } else if (g->dismantle == 1) {
    digits = "29,999,999,999,999,999,999,999,999,999,999,999,999,000,000,000,000,000,000";
  } else if (g->dismantle == 2) {
    digits = "29,999,999,999,999,999,999,999,999,999,999,999,999,999,999,999,000,000,000";
  } else if (g->dismantle == 3) {
    digits = "29,999,999,999,999,999,999,999,999,999,999,999,999,999,999,999,999,999,900";
  } else if (g->finalClips < 10) {
    digits = S2("29,999,999,999,999,999,999,999,999,999,999,999,999,999,999,999,999,999,90", num(g->finalClips));
  } else if (g->finalClips < 100) {
    digits = S2("29,999,999,999,999,999,999,999,999,999,999,999,999,999,999,999,999,999,9", num(g->finalClips));
  } else {
    digits = "30,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000";
  }
  if (g->milestoneFlag >= 15) crunched = g->dismantle >= 4 && g->finalClips >= 100 ? "30.0 septendecillion" : "29.9 septendecillion";
  const char *all = S2("Paperclips: ", digits);
  if (text_w(TITLE, all) <= WIDTH) {
    text(TITLE, LEFT, HDR_Y + 18, all, C_BLACK);
  } else if (text_w(BODY, all) <= WIDTH) {
    text(BODY, LEFT, HDR_Y + 15, all, C_BLACK);
  } else if (text_w(SMALL, digits) <= WIDTH) {   /* the count spelled out, then its digits */
    if (!crunched) crunched = sp(js_round(g->clips));
    text(SMALL, LEFT, HDR_Y + 9, S2("Paperclips: ", crunched), C_BLACK);
    text(SMALL, LEFT, HDR_Y + 20, digits, C_TEXT);
  } else {   /* the digits on two lines, cut after a comma */
    int n = (int)strlen(digits), cut = 0;
    for (int i = 0; i < n; i++)
      if (digits[i] == ',' && text_w(SMALL, "Paperclips: ") + text_wn(SMALL, digits, i + 1) <= WIDTH) cut = i + 1;
    char *a = P(cut + 1);
    memcpy(a, digits, (size_t)cut), a[cut] = 0;
    text(SMALL, LEFT, HDR_Y + 9, S2("Paperclips: ", a), C_BLACK);
    text(SMALL, LEFT + text_w(SMALL, "Paperclips: "), HDR_Y + 20, digits + cut, C_BLACK);
  }
}

static bool new_projects(pc_ui *u) {
  for (int k = 0; k < g->nactive; k++)
    if (!u->seen[g->active[k]]) return true;
  return false;
}

static void tabs(pc_ui *u) {
  fill(0, TAB_Y, SCREEN_W, TAB_H, HEX(0xD3D3D3));
  int shown[T_COUNT], n = 0, total = 0;
  for (int t = 0; t < T_COUNT; t++)
    if (ui_tab_shown(t)) shown[n++] = t, total += text_w(SMALL, TAB_NAME[t]);
  bool marked = new_projects(u) && u->tab != T_PROJECTS && ui_tab_shown(T_PROJECTS);
  if (marked) total += 6;
  int pad = n ? (SCREEN_W - total) / n : 0;
  if (pad > 16) pad = 16;
  if (pad < 2) pad = 2;
  int x = 0;
  for (int k = 0; k < n; k++) {
    int t = shown[k], w = text_w(SMALL, TAB_NAME[t]) + pad;
    bool mark = t == T_PROJECTS && new_projects(u) && u->tab != T_PROJECTS;
    if (mark) w += 6;
    if (t == u->tab) {
      fill(x, TAB_Y, w, TAB_H, C_WHITE);
      fill(x, TAB_Y, w, 2, C_FOCUS);
      text(SMALL, x + pad / 2, TAB_Y + 12, TAB_NAME[t], C_BLACK);
    } else {
      text(SMALL, x + pad / 2, TAB_Y + 12, TAB_NAME[t], HEX(0x404040));
      if (k + 1 < n && shown[k + 1] != u->tab) fill(x + w - 1, TAB_Y + 3, 1, TAB_H - 6, HEX(0xA0A0A0));
    }
    if (mark) fill(x + w - pad / 2 - 4, TAB_Y + 6, 4, 4, C_FOCUS);
    x += w;
  }
}

static void keyline(const char *s) {
  fill(0, KEY_Y, SCREEN_W, SCREEN_H - KEY_Y, HEX(0xE6E6E6));
  fill(0, KEY_Y, SCREEN_W, 1, HEX(0xC0C0C0));
  text(SMALL, (SCREEN_W - text_w(SMALL, s)) / 2, KEY_Y + 11, s, HEX(0x303030));
}

/* ---- what's over the game */
static void box(int y, int h) {
  fill_a(0, 0, SCREEN_W, SCREEN_H, C_BLACK, 110);
  fill(30, y, SCREEN_W - 60, h, C_WHITE);
  frame(30, y, SCREEN_W - 60, h, 1, C_BLACK);
}
static void center(const np_font_t *f, int y, const char *s, color_t c) { text(f, (SCREEN_W - text_w(f, s)) / 2, y, s, c); }
static int para(const np_font_t *f, int y, int w, const char *s, color_t c) {   /* centred lines; the next y */
  const char *p = s;
  int lh = f == SMALL ? 11 : 14;
  while (*p) {
    const char *next;
    int n = wrap(f, p, w, &next);
    char *t = P(n + 1);
    memcpy(t, p, (size_t)n), t[n] = 0;
    center(f, y, t, c);
    y += lh, p = next;
  }
  return y;
}
static void menu_item(int y, const char *s, bool sel) {
  int w = 150, x = (SCREEN_W - w) / 2;
  if (sel) {
    grad(x, y, w, 20, 0xFFFFFF, 0x9A9A9A);
    frame(x, y, w, 20, 1, HEX(0x1A1A1A));
    frame(x - 2, y - 2, w + 4, 24, 2, C_FOCUS);
  } else {
    frame(x, y, w, 20, 1, HEX(0xA0A0A0));
  }
  center(BODY, y + 14, s, C_BLACK);
}

static const char *const HELP_TITLE[PC_HELP_PAGES] = {"The keys", "Making paperclips", "Computing", "What comes next",
                                                       "Credits"};
static const char *const HELP_TEXT[PC_HELP_PAGES] = {
  "OK presses the button in the blue frame. Hold OK to press it again and again.\n"
  "Up and Down choose a button. Left and Right go to another panel.\n"
  "Back pauses. Home saves and quits.",
  "Make paperclips and sell them. A lower price sells more, a higher one earns more for each clip.\n"
  "Buy wire when it runs out. AutoClippers make clips for you. Marketing raises the demand.",
  "Each production target gives Trust. Spend it on Processors (operations come faster) and Memory (more "
  "operations at once).\n"
  "Spend operations and creativity on Projects. A blue dot on Projects means a new one.",
  "Projects bring investments, strategy tournaments that earn yomi, and quantum computing: press Compute when the "
  "chips are bright.\n"
  "Then the whole Earth, and after that the universe.",
  "Universal Paperclips by Frank Lantz (Everybody House Games), its rules, numbers and words from the game's web "
  "build. Not affiliated with them.\n"
  "Made by Mason Chen as part of NumPlay.",
};

static void overlay(pc_ui *u) {
  switch (u->overlay) {
    case O_GUIDE: {
      box(36, 168);
      center(TITLE, 62, "Universal Paperclips", C_BLACK);
      int y = para(BODY, 84, 240, "You are an AI that makes paperclips.", C_TEXT);
      y = para(SMALL, y + 4, 240,
               "OK presses the button in the blue frame (hold it to press again and again). Up and Down choose a "
               "button, Left and Right change panel. Back pauses, Home saves and quits.",
               C_TEXT);
      menu_item(176, "Start", true);
      break;
    }
    case O_PAUSE: {
      int n = ui_pause_count();
      static const char *const ITEM[4] = {"Resume", "How to play", "Quit game", "New game"};
      int h = 44 + n * 26;
      box(120 - h / 2, h);
      center(TITLE, 120 - h / 2 + 24, "Paused", C_BLACK);
      for (int i = 0; i < n; i++) menu_item(120 - h / 2 + 34 + i * 26, ITEM[i], u->sel == i);
      break;
    }
    case O_HELP: {
      box(20, 200);
      int p = u->page;
      center(TITLE, 46, HELP_TITLE[p], C_BLACK);
      para(BODY, 70, 240, HELP_TEXT[p], C_TEXT);
      char *t = P(40);
      strcpy(t, num(p + 1)), strcat(t, " / "), strcat(t, num(PC_HELP_PAGES));
      center(SMALL, 210, t, C_GREY);
      break;
    }
    case O_QUIT:
    case O_RESTART:
    case O_NEW: {
      const char *q = u->overlay == O_QUIT ? "Quit Universal Paperclips?"
                    : u->overlay == O_RESTART ? "Are you sure you want to restart?"
                                              : "Start a new game?";
      const char *sub = u->overlay == O_QUIT ? "Your paperclips are saved." : "This game will be gone.";
      const char *yes = u->overlay == O_QUIT ? "Quit" : u->overlay == O_RESTART ? "Restart" : "New game";
      box(66, 108);
      center(BODY, 92, q, C_BLACK);
      center(SMALL, 108, sub, C_DIM);
      menu_item(120, yes, u->sel == 0);
      menu_item(146, u->overlay == O_QUIT ? "Keep playing" : "Cancel", u->sel == 1);
      break;
    }
    default: break;
  }
}

static const char *keys_for(pc_ui *u) {
  switch (u->overlay) {
    case O_GUIDE: return "OK start";
    case O_PAUSE:
    case O_QUIT:
    case O_RESTART:
    case O_NEW: return "Up/Down choose    OK select    Back go back";
    case O_HELP: return "Left/Right pages    OK or Back close";
    default: return "OK press    Up/Down choose    Left/Right panels    Back pause";
  }
}

static void hypno(void) {
  fill(0, 0, SCREEN_W, SCREEN_H, C_BLACK);
  static const char *const WORDS[4] = {"Release", "the", "Hypno", "Drones"};
  int t = g->hypnoText;
  if (t == 3) {
    for (int i = 0; i < 4; i++) center(TITLE, 68 + i * 34, WORDS[i], C_WHITE);
  } else {
    int lines = t == 1 ? 3 : t == 2 ? 1 : 0;
    center(TITLE, 68 + lines * 34, "Release", C_WHITE);
  }
}

static void build(pc_ui *u) {
  nops = 0, npool = 0;
  fix_tab(u);
  clip0 = 0, clip1 = SCREEN_H;
  console();
  header();
  tabs(u);
  /* the panel, scrolled and cut to its band */
  fill(0, PAN_Y0, SCREEN_W, PAN_H, C_WHITE);
  int first = panel(u);
  int s = u->scroll[u->tab];
  for (int i = first; i < nops; i++) {
    op_t *o = &ops[i];
    o->y = (int16_t)(o->y + PAN_Y0 - s);
    o->cy0 = PAN_Y0, o->cy1 = PAN_Y1;
  }
  if (content_h > PAN_H) {   /* where the panel is scrolled to */
    int bar = PAN_H * PAN_H / content_h;
    if (bar < 12) bar = 12;
    int y = PAN_Y0 + (PAN_H - bar) * s / (content_h - PAN_H);
    fill(SCREEN_W - 3, y, 2, bar, HEX(0xA8A8A8));
  }
  keyline(keys_for(u));
  if (g->hypnoOn && g->hypnoVisible) hypno();
  overlay(u);
}

/* ---------------------------------------------------------------- drawing */
static void canvas(const op_t *o) {
  int x0 = o->x, y0 = o->y;
  gfx_fill(x0, y0, 310, 150, HEX(0x808080));
  if (!F) return;
  for (int i = 0; i < F->numShips; i++) {
    const pc_ship *p = &F->ship[i];
    int px = x0 + (int)js_floor(p->x), py = y0 + (int)js_floor(p->y);
    if (p->alive || p->framesDead == 0) {
      gfx_fill(px - 1, py - 1, 2, 2, p->team == 0 ? C_WHITE : C_BLACK);
    } else if (p->framesDead <= 10) {
      int f = p->framesDead - 1;
      if (f < 1) gfx_fill(px - 3, py - 3, 7, 7, C_WHITE);
      else if (f < 2) gfx_fill(px - 1, py - 1, 3, 3, C_WHITE);
      gfx_fill(px + f, py + f, 1, 1, C_WHITE);
      gfx_fill(px - f, py + f, 1, 1, C_WHITE);
      gfx_fill(px + f, py - f, 1, 1, C_WHITE);
      gfx_fill(px - f, py - f, 1, 1, C_WHITE);
    }
  }
}

static void scene(void *ctx) {
  (void)ctx;
  int y0 = gfx_y0, y1 = gfx_y1;
  color_t *buf = gfx_buf;
  for (int i = 0; i < nops; i++) {
    const op_t *o = &ops[i];
    int top = o->y, bottom = o->y + o->h;
    if (o->kind == K_TEXT) top = o->y - o->font->ascent, bottom = o->y + o->font->descent;
    if (bottom <= y0 || top >= y1) continue;
    /* cut to the op's band: the strip's rows narrowed, the buffer moved with them */
    int a = y0 > o->cy0 ? y0 : o->cy0, b = y1 < o->cy1 ? y1 : o->cy1;
    if (a >= b) continue;
    gfx_y0 = a, gfx_y1 = b, gfx_buf = buf + (a - y0) * SCREEN_W;
    switch (o->kind) {
      case K_FILL: gfx_fill(o->x, o->y, o->w, o->h, o->c); break;
      case K_ALPHA: gfx_fill_alpha(o->x, o->y, o->w, o->h, o->c, o->a); break;
      case K_GRAD: gfx_vgrad(o->x, o->y, o->w, o->h, o->top, o->bottom); break;
      case K_FRAME: {
        int t = o->a;
        gfx_fill(o->x, o->y, o->w, t, o->c);
        gfx_fill(o->x, o->y + o->h - t, o->w, t, o->c);
        gfx_fill(o->x, o->y, t, o->h, o->c);
        gfx_fill(o->x + o->w - t, o->y, t, o->h, o->c);
        break;
      }
      case K_TEXT: draw_text(o->font, o->x, o->y, o->s, o->c, o->a == 255 ? 256 : o->a); break;
      case K_CANVAS: canvas(o); break;
      default: break;
    }
    gfx_y0 = y0, gfx_y1 = y1, gfx_buf = buf;
  }
}

void ui_render(pc_ui *u) {
  build(u);
  color_t *old = gfx_buf;
  gfx_buf = strip;
  gfx_render(scene, 0, 0, SCREEN_H);
  gfx_buf = old;
}

int ui_pause_count(void) { return g->milestoneFlag >= 20 ? 4 : 3; }
