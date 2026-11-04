/* Among Us: the rules (see game.h). Numbers from the game (2021.3.31): PlayerPhysics, PlayerControl, ShipStatus,
 * GameOptionsData; see gamesrc/amongus/spec/rules.md. */
#include <math.h>
#include <string.h>
#include "game.h"
#include "gfx.h"
#include "plat.h"
#include "res.h"
#include "world.h"

Game g;
bool autopilot;   /* (tests: the bots' brain plays the player too) */
bool bots_frozen; /* (tests: the bots stand still) */
uint16_t color_pal[N_COLORS][256];

const uint32_t BODY_COLORS[N_COLORS] = {0xC61111, 0x132ED1, 0x117F2D, 0xED54BA, 0xEF7D0D, 0xF5F557,
                                        0x3F474E, 0xD6E0F0, 0x6B2FBB, 0x71491E, 0x38FEDC, 0x50EF39};
const uint32_t SHADOW_COLORS[N_COLORS] = {0x7A0838, 0x09158E, 0x0A4D2E, 0xAB2BAD, 0xB33E15, 0xC38823,
                                          0x1E1F26, 0x8394BF, 0x3B177C, 0x5E2615, 0x24A8BE, 0x15A742};
const char *const COLOR_NAMES[N_COLORS] = {"Red",   "Blue",  "Green",  "Pink",  "Orange", "Yellow",
                                           "Black", "White", "Purple", "Brown", "Cyan",   "Lime"};
#define VISOR 0x95CADC

/* ShipStatus' CommonTasks, LongTasks, NormalTasks */
const TaskDef TASK_DEFS[] = {
    {T_SWIPE_CARD, R_ADMIN, 1, 0, 0, "Swipe Card"},
    {T_FIX_WIRING, R_ELECTRICAL, 3, 0, 0, "Fix Wiring"},
    {T_CLEAR_ASTEROIDS, R_WEAPONS, 20, 1, 0, "Clear Asteroids"},
    {T_ALIGN_ENGINE, R_UPPER_ENGINE, 2, 1, 0, "Align Engine Output"},
    {T_SUBMIT_SCAN, R_MEDBAY, 1, 1, 0, "Submit Scan"},
    {T_INSPECT_SAMPLE, R_MEDBAY, 1, 1, 0, "Inspect Sample"},
    {T_FUEL_ENGINES, R_STORAGE, 4, 1, 0, "Fuel Engines"},
    {T_START_REACTOR, R_REACTOR, 5, 1, 0, "Start Reactor"},
    {T_EMPTY_CHUTE, R_O2, 2, 1, 0, "Empty Chute"},
    {T_EMPTY_GARBAGE, R_CAFETERIA, 2, 1, 0, "Empty Garbage"},
    {T_UPLOAD_DATA, R_COMMS, 2, 2, 0, "Upload Data"},
    {T_CALIBRATE, R_ELECTRICAL, 1, 2, 0, "Calibrate Distributor"},
    {T_CHART_COURSE, R_NAV, 1, 2, 0, "Chart Course"},
    {T_CLEAN_O2, R_O2, 1, 2, 0, "Clean O2 Filter"},
    {T_UNLOCK_MANIFOLDS, R_REACTOR, 1, 2, 0, "Unlock Manifolds"},
    {T_UPLOAD_DATA, R_ELECTRICAL, 2, 2, 0, "Upload Data"},
    {T_STABILIZE, R_NAV, 1, 2, 0, "Stabilize Steering"},
    {T_UPLOAD_DATA, R_WEAPONS, 2, 2, 0, "Upload Data"},
    {T_PRIME_SHIELDS, R_SHIELDS, 1, 2, 0, "Prime Shields"},
    {T_UPLOAD_DATA, R_CAFETERIA, 2, 2, 0, "Upload Data"},
    {T_UPLOAD_DATA, R_NAV, 2, 2, 0, "Upload Data"},
    {T_DIVERT_POWER, R_ELECTRICAL, 2, 2, R_SHIELDS, "Divert Power to Shields"},
    {T_DIVERT_POWER, R_ELECTRICAL, 2, 2, R_WEAPONS, "Divert Power to Weapons"},
    {T_DIVERT_POWER, R_ELECTRICAL, 2, 2, R_COMMS, "Divert Power to Communications"},
    {T_DIVERT_POWER, R_ELECTRICAL, 2, 2, R_UPPER_ENGINE, "Divert Power to Upper Engine"},
    {T_DIVERT_POWER, R_ELECTRICAL, 2, 2, R_O2, "Divert Power to O2"},
    {T_DIVERT_POWER, R_ELECTRICAL, 2, 2, R_NAV, "Divert Power to Navigation"},
    {T_DIVERT_POWER, R_ELECTRICAL, 2, 2, R_LOWER_ENGINE, "Divert Power to Lower Engine"},
    {T_DIVERT_POWER, R_ELECTRICAL, 2, 2, R_SECURITY, "Divert Power to Security"},
};
const int N_TASK_DEFS = sizeof TASK_DEFS / sizeof TASK_DEFS[0];

uint32_t rnd(void) {
  uint32_t x = g.rng ? g.rng : 0x9E3779B9u;
  x ^= x << 13, x ^= x >> 17, x ^= x << 5;
  g.rng = x;
  return x;
}
float rnd_f(void) { return (float)(rnd() >> 8) / 16777216.0f; }

/* ---- the rules' numbers ---- */

float player_speed(int i) {
  const Player *P = &g.p[i];
  return (P->alive ? 2.5f : 3.0f) * g.opt.speed * 0.25f;
}

float vision_radius(int i) {
  const Player *P = &g.p[i];
  if (!P->alive) return 30;
  if (P->imp) return 5.0f * g.opt.imp_vision * 0.25f;
  return (1.0f + 4.0f * g.light) * g.opt.crew_vision * 0.25f;
}

float kill_distance(void) {
  static const float D[3] = {1.0f, 1.8f, 2.5f};
  return D[g.opt.kill_dist % 3];
}
float kill_cooldown(void) { return g.opt.kill_cd * 0.5f; }

/* ---- tasks ---- */

int task_steps(const Task *t) { return TASK_DEFS[t->def].steps; }
bool task_done(const Task *t) { return t->step >= task_steps(t); }

const char *task_name(const Task *t) {
  const TaskDef *d = &TASK_DEFS[t->def];
  if (d->type == T_UPLOAD_DATA && t->step == 0) return "Download Data";
  if (d->type == T_DIVERT_POWER && t->step == 1) return "Accept Diverted Power";
  return d->name;
}

/* does console c serve task t at its current step */
bool console_serves(int c, const Task *t) {
  const ConsoleRec *C = &CONSOLES[c];
  const TaskDef *d = &TASK_DEFS[t->def];
  if (C->kind != 0 || task_done(t)) return false;
  int s = t->step;
  switch (d->type) {
  case T_FIX_WIRING:
    return (C->types >> T_FIX_WIRING & 1) && C->id == t->data[s];
  case T_ALIGN_ENGINE:
    return (C->types >> T_ALIGN_ENGINE & 1) && C->id == s;
  case T_UPLOAD_DATA:
    if (s == 0) return C->room == d->room && C->valid[0].type == T_UPLOAD_DATA;
    return C->room == R_ADMIN && (C->types >> T_UPLOAD_DATA & 1);
  case T_DIVERT_POWER:
    if (s == 0) return C->types >> T_DIVERT_POWER & 1;
    return C->room == d->target && C->valid[0].type == T_DIVERT_POWER;
  default:
    if (C->types >> d->type & 1) return true;
    for (int k = 0; k < 2; k++)
      if (C->valid[k].type == d->type && s >= C->valid[k].lo && s <= C->valid[k].hi) return true;
    return false;
  }
}

int task_console(const Player *P, const Task *t) {
  (void)P;
  for (int c = 0; c < N_CONSOLES; c++)
    if (console_serves(c, t)) return c;
  return -1;
}

int task_room(const Task *t) {
  int c = task_console(0, t);
  return c >= 0 ? CONSOLES[c].room : TASK_DEFS[t->def].room;
}

void task_step_done(int pl, int ti) {
  Task *t = &g.p[pl].tasks[ti];
  if (task_done(t)) return;
  t->step++;
  if (task_done(t)) {
    if (g.opt.taskbar == 0) g.task_bar = tasks_progress();
    check_win();
  }
}

float tasks_progress(void) {
  int all = 0, done = 0;
  for (int i = 0; i < g.nplayers; i++) {
    if (g.p[i].imp) continue;
    for (int k = 0; k < g.p[i].ntasks; k++) {
      all++;
      done += task_done(&g.p[i].tasks[k]);
    }
  }
  return all ? (float)done / all : 0;
}

/* ShipStatus.Begin: the same common tasks for everyone, then long and short ones, at most one of each type */
static void give_tasks(void) {
  uint8_t common[2];
  int nc = g.opt.common > 2 ? 2 : g.opt.common;
  int order[2] = {0, 1};
  if (rnd() & 1) order[0] = 1, order[1] = 0;
  for (int k = 0; k < nc; k++) common[k] = (uint8_t)order[k];
  uint8_t wires[3];
  {
    /* three of the six wiring panels, in their order */
    uint8_t ids[6] = {0, 1, 2, 3, 4, 5};
    for (int k = 5; k > 0; k--) {
      int j = rnd_n(k + 1);
      uint8_t t = ids[k];
      ids[k] = ids[j], ids[j] = t;
    }
    for (int k = 0; k < 3; k++) wires[k] = ids[k];
    for (int a = 0; a < 3; a++)
      for (int b = a + 1; b < 3; b++)
        if (wires[b] < wires[a]) {
          uint8_t t = wires[a];
          wires[a] = wires[b], wires[b] = t;
        }
  }
  for (int i = 0; i < g.nplayers; i++) {
    Player *P = &g.p[i];
    P->ntasks = 0;
    uint32_t used = 0;
    for (int k = 0; k < nc; k++) {
      Task *t = &P->tasks[P->ntasks++];
      memset(t, 0, sizeof *t);
      t->def = common[k];
      memcpy(t->data, wires, 3);
      used |= 1u << TASK_DEFS[t->def].type;
    }
    for (int kind = 1; kind <= 2; kind++) {
      int want = kind == 1 ? g.opt.longt : g.opt.shortt;
      for (int n = 0; n < want && P->ntasks < MAX_TASKS; n++) {
        int cands[32], nca = 0;
        for (int d = 0; d < N_TASK_DEFS; d++)
          if (TASK_DEFS[d].kind == kind && !(used >> TASK_DEFS[d].type & 1)) cands[nca++] = d;
        if (!nca) break;
        int d = cands[rnd_n(nca)];
        Task *t = &P->tasks[P->ntasks++];
        memset(t, 0, sizeof *t);
        t->def = (uint8_t)d;
        used |= 1u << TASK_DEFS[d].type;
        if (TASK_DEFS[d].type == T_INSPECT_SAMPLE) t->timer = -1;
      }
    }
  }
}

/* ---- the game ---- */

/* ShipStatus.SpawnPlayer: up, turned by the player's number, SpawnRadius out from the spawn centre, then
 * 0.3636 up (the player's picture over its feet) */
void spawn_at_table(int i) {
  Player *P = &g.p[i];
  float a = 6.2831853f * (i - 1) / g.nplayers + 1.5707963f;
  V2 p = {-0.72f + cosf(a) * 1.6f, 0.62f + sinf(a) * 1.6f + 0.3636f - FEET_Y};
  /* (where the circle meets the table, the game's physics pushes the player out: so do we, away from it) */
  const V2 table = {-0.86f, 0.84f};
  for (int k = 0; k < 40; k++) {
    V2 q = ship_move(p, (V2){0, 0}, PLAYER_R + 0.02f);
    bool inside = !ship_wall_between(table, p);
    if (!inside && fabsf(q.x - p.x) + fabsf(q.y - p.y) < 1e-4f) break;
    float dx = p.x - table.x, dy = p.y - table.y, l = sqrtf(dx * dx + dy * dy) + 1e-6f;
    p.x += dx / l * 0.05f, p.y += dy / l * 0.05f;
  }
  P->pos = p;
  P->vel = (V2){0, 0};
  P->vent = NONE;
  P->vent_anim = 0;
  P->walking = 0;
  P->busy = 0;
  P->flip = cosf(a) > 0.01f;
}

static void spawn_circle(void) {
  for (int i = 0; i < g.nplayers; i++) spawn_at_table(i);
}

void game_new(void) {
  Options *o = &g.opt;
  uint32_t seed = g.rng ^ (plat_millis() * 2654435761u) ^ 0x5bd1e995u;
  Options keep = *o;
  memset(&g, 0, sizeof g);
  g.opt = keep;
  g.rng = seed ? seed : 1;
  g.nplayers = (uint8_t)(1 + o->bots);
  if (g.nplayers < 4) g.nplayers = 4;
  if (g.nplayers > MAXP) g.nplayers = MAXP;
  /* colours: the player's, then the others in the game's order */
  int c = 0;
  for (int i = 0; i < g.nplayers; i++) {
    Player *P = &g.p[i];
    memset(P, 0, sizeof *P);
    if (i == 0) P->color = g.opt.color % N_COLORS;
    else {
      while (c == g.opt.color % N_COLORS) c++;
      P->color = (uint8_t)c++;
    }
    P->alive = 1;
    P->emergencies = g.opt.meetings;
    P->kill_cd = 10;   /* the first round's */
  }
  /* impostors: the lobby's count, limited as the game does by the number of players */
  int nimp = g.opt.impostors;
  int maxi = g.nplayers <= 6 ? 1 : g.nplayers <= 8 ? 2 : 3;
  if (nimp > maxi) nimp = maxi;
  if (nimp < 1) nimp = 1;
  int role = g.opt.role;
  if (role == 2) g.p[0].imp = 1, nimp--;
  for (int k = 0; k < nimp; k++) {
    int i;
    do i = rnd_n(g.nplayers);
    while (g.p[i].imp || (i == 0 && role != 0));
    g.p[i].imp = 1;
  }
  give_tasks();
  spawn_circle();
  for (int i = 0; i < 16; i++) g.vent_t[i] = -10;
  g.me = 0;
  g.light = 1;
  g.switches = 0x1F;
  g.sab_cd = 10;
  g.emergency_cd = g.opt.meet_cd;
  g.task_bar = 0;
  g.ejected = NONE;
  g.killer = NONE;
  for (int i = 0; i < g.nplayers; i++) bot_init(i);
  g.cam = g.p[0].pos;
  g.screen = S_INTRO;
  g.screen_t = 0;
}

int living(bool imp) {
  int n = 0;
  for (int i = 0; i < g.nplayers; i++) n += g.p[i].alive && g.p[i].imp == imp;
  return n;
}

static void end_game(int winner, int reason) {
  if (g.screen == S_END) return;
  g.winner = (uint8_t)winner;
  g.end_reason = (uint8_t)reason;
  g.screen = S_END;
  g.screen_t = 0;
  g.overlay = O_NONE;
}

/* reasons: 0 by vote, 1 by kills, 2 tasks, 3 sabotage */
void check_win(void) {
  if (g.screen == S_END || g.screen == S_TITLE || g.screen == S_LOBBY) return;
  int imp = living(true), crew = living(false);
  if (imp == 0) end_game(0, 0);
  else if (imp >= crew) end_game(1, 1);
  else if (tasks_progress() >= 1) end_game(0, 2);
}

/* PhysicsHelpers.AnythingBetween with the ship and its objects */
bool can_see(int a, int b) {
  const Player *A = &g.p[a], *B = &g.p[b];
  if (B->vent != NONE) return false;
  float dx = A->pos.x - B->pos.x, dy = A->pos.y - B->pos.y, r = vision_radius(a);
  if (dx * dx + dy * dy > r * r) return false;
  return !ship_light_between(A->pos, B->pos);
}

void kill_player(int killer, int victim) {
  Player *K = &g.p[killer], *V = &g.p[victim];
  if (!V->alive) return;
  V->alive = 0;
  V->busy = 0;
  for (int b = 0; b < MAXP; b++)
    if (!g.bodies[b].on) {
      g.bodies[b] = (Body){V->pos, (uint8_t)victim, 1, {0, 0}};
      break;
    }
  /* the killer lands where the victim stood */
  K->flip = V->pos.x < K->pos.x;
  K->pos = V->pos;
  K->kill_cd = kill_cooldown();
  if (victim == g.me) {
    g.screen = S_KILLED;
    g.screen_t = 0;
    g.killer = (uint8_t)killer;
    g.kill_anim = (uint8_t)rnd_n(4);
    g.overlay = O_NONE;
  }
  for (int i = 0; i < g.nplayers; i++)
    if (g.p[i].alive && i != killer && is_bot(i)) bot_saw_kill(i, killer), bot_see(i);
  check_win();
}

void start_meeting(int caller, int body) {
  if (g.screen == S_END) return;
  g.caller = (uint8_t)caller;
  g.reported = (uint8_t)body;   /* the dead player's number, or NONE for the button */
  if (body == NONE) g.p[caller].emergencies--;
  g.screen = S_REPORT;
  g.screen_t = 0;
  g.overlay = O_NONE;
  /* a meeting ends the critical sabotages */
  if (g.sab == SAB_REACTOR || g.sab == SAB_O2) {
    g.sab = SAB_NONE;
    g.sab_cd = 30;
  }
  for (int i = 0; i < g.nplayers; i++) {
    Player *P = &g.p[i];
    if (P->vent != NONE) P->vent = NONE, P->vent_anim = 0;
    P->busy = 0;
  }
}

/* ---- sabotage (SabotageSystemType, ReactorSystemType, LifeSuppSystemType, SwitchSystem, HudOverrideSystemType,
 * DoorsSystemType) ---- */

bool sabotage_ready(void) { return g.sab == SAB_NONE && g.sab_cd <= 0; }

void sabotage(int kind) {
  if (!sabotage_ready()) return;
  g.sab = (uint8_t)kind;
  g.sab_age = 0;
  g.sab_t = 30;   /* the Skeld's meltdown and oxygen: 30 seconds */
  g.o2_done = 0;
  g.reactor_hold[0] = g.reactor_hold[1] = NONE;
  if (kind == SAB_O2) {
    g.o2_code = 0;
    for (int k = 0; k < 5; k++) g.o2_code = g.o2_code * 10 + (uint32_t)rnd_n(10);
  }
  if (kind == SAB_LIGHTS) {
    /* some switches flipped, at least one */
    uint8_t s;
    do s = (uint8_t)(rnd() & 0x1F);
    while (s == 0x1F);
    g.switches = s;
  }
  g.sab_fixing = 0;
}

void fix_sabotage(void) {
  g.sab = SAB_NONE;
  g.sab_cd = 30;
  g.switches = 0x1F;
}

void close_doors(int room) {
  if (g.door_cd[room] > 0) return;
  for (int d = 0; d < N_DOORS; d++)
    if (DOORS[d].room == room) {
      g.door_t[d] = 10;
      g.door_anim[d] = 0;
    }
  g.door_cd[room] = 30;
}

bool door_closed(int d) { return g.door_t[d] > 0; }

/* ---- vents (Vent.UsableDistance 0.75) ---- */

int nearest_vent(V2 p, float maxd) {
  int best = -1;
  float bd = maxd * maxd;
  for (int v = 0; v < N_VENTS; v++) {
    float dx = VENTS[v].x - p.x, dy = VENTS[v].y - p.y, d = dx * dx + dy * dy;
    if (d <= bd && !ship_wall_between(p, (V2){VENTS[v].x, VENTS[v].y})) bd = d, best = v;
  }
  return best;
}

void enter_vent(int i, int v) {
  Player *P = &g.p[i];
  P->vent = (uint8_t)v;
  P->vent_anim = 1;
  P->anim_t = 0;
  P->pos = (V2){VENTS[v].x, VENTS[v].y + 0.05f};
  g.vent_t[v] = g.t;
}

void exit_vent(int i) {
  Player *P = &g.p[i];
  if (P->vent == NONE) return;
  g.vent_t[P->vent] = g.t;
  P->vent_anim = 2;
  P->anim_t = 0;
}

void move_vent(int i, int v) {
  Player *P = &g.p[i];
  P->vent = (uint8_t)v;
  P->pos = (V2){VENTS[v].x, VENTS[v].y + 0.05f};
}

/* ---- saves: "AUSG" and a version, then the whole game; settings and wins in their own file ---- */

#define SAVE_GAME "amongus.sav"
#define SAVE_SET "amongus.set"
#define MAGIC_G 0x47535541u
#define MAGIC_S 0x53535541u
#define VERSION 1

typedef struct {
  uint32_t magic;
  uint16_t version, pad;
  uint32_t size;
} SaveHead;

#include "../../common/np_lz.h"
#define COPY_NAME "amongus_saves.py"

/* a copy of both files in a Python script: installing apps from the NumWorks website keeps only scripts */
static const char copy_head[] = "# Among Us' saves: keep this file\n";
static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static uint8_t *b64_put(uint8_t *p, const uint8_t *d, uint32_t len) {
  for (uint32_t i = 0; i < len; i += 3, p += 4) {
    uint32_t left = len - i, v = (uint32_t)d[i] << 16 | (left > 1 ? d[i + 1] << 8 : 0) | (left > 2 ? d[i + 2] : 0);
    p[0] = (uint8_t)b64[v >> 18 & 63];
    p[1] = (uint8_t)b64[v >> 12 & 63];
    p[2] = left > 1 ? (uint8_t)b64[v >> 6 & 63] : '=';
    p[3] = left > 2 ? (uint8_t)b64[v & 63] : '=';
  }
  return p;
}

static int b64_value(uint8_t c) {
  for (int i = 0; i < 64; i++)
    if ((uint8_t)b64[i] == c) return i;
  return -1;
}

/* the line "#>name:..." of the copy, decoded into out; its length or -1 */
static int32_t copy_get(const char *name, uint8_t *out, uint32_t cap) {
  uint32_t n, nl = (uint32_t)strlen(name);
  const uint8_t *c = plat_load(COPY_NAME, &n), *l = NULL;
  if (!c) return -1;
  for (uint32_t i = 0; i + nl + 3 <= n && !l; i++)
    if (c[i] == '#' && c[i + 1] == '>' && !memcmp(c + i + 2, name, nl) && c[i + 2 + nl] == ':') l = c + i + 3 + nl;
  if (!l) return -1;
  uint32_t len = 0;
  while (l + len < c + n && l[len] != '\n' && l[len]) len++;
  if (len % 4) return -1;
  uint32_t o = 0;
  for (uint32_t i = 0; i < len; i += 4) {
    int v0 = b64_value(l[i]), v1 = b64_value(l[i + 1]);
    int pad = (l[i + 2] == '=') + (l[i + 3] == '=');
    int v2 = l[i + 2] == '=' ? 0 : b64_value(l[i + 2]), v3 = l[i + 3] == '=' ? 0 : b64_value(l[i + 3]);
    if (v0 < 0 || v1 < 0 || v2 < 0 || v3 < 0) return -1;
    uint32_t w = (uint32_t)(v0 << 18 | v1 << 12 | v2 << 6 | v3);
    for (int k = 0; k < 3 - pad; k++) {
      if (o >= cap) return -1;
      out[o++] = (uint8_t)(w >> (16 - 8 * k));
    }
  }
  return (int32_t)o;
}

/* packed game: the head, then the game LZ-packed; the room for it borrowed from the pictures' (res.c) */
#define PACKED_MAX ((sizeof(Game) + sizeof(Game) / 8 + 64 + 3) & ~3u)
#define COPY_MAX (1 + sizeof copy_head + 64 + (PACKED_MAX + 2) / 3 * 4 + 64)
static uint8_t *packed;

static uint32_t pack_game(void) {
  uint32_t room;
  packed = scratch_borrow(&room);
  if (room < PACKED_MAX + 2 * NP_LZ_HASH + COPY_MAX) return 0;   /* (never: the tests check it) */
  uint16_t *table = (uint16_t *)(void *)(packed + PACKED_MAX);
  SaveHead h = {MAGIC_G, VERSION, 0, sizeof(Game)};
  memcpy(packed, &h, sizeof h);
  return sizeof h + np_lz_pack((const uint8_t *)&g, sizeof(Game), packed + sizeof h, table);
}

typedef struct {
  SaveHead h;
  Options opt;
  uint16_t wins_crew, wins_imp, games, pad;
} SetFile;

static void write_copy(uint32_t glen, const SetFile *s) {
  uint32_t room;
  uint8_t *base = scratch_borrow(&room);
  uint8_t *o = base + PACKED_MAX + 2 * NP_LZ_HASH, *p = o;   /* (after the packed game) */
  *p++ = 0;
  memcpy(p, copy_head, sizeof copy_head - 1), p += sizeof copy_head - 1;
  memcpy(p, "#>" SAVE_SET ":", 2 + sizeof SAVE_SET), p += 2 + sizeof SAVE_SET;
  p = b64_put(p, (const uint8_t *)s, sizeof *s);
  *p++ = '\n';
  if (glen) {
    memcpy(p, "#>" SAVE_GAME ":", 2 + sizeof SAVE_GAME), p += 2 + sizeof SAVE_GAME;
    p = b64_put(p, packed, glen);
    *p++ = '\n';
  }
  *p++ = 0;
  plat_save(COPY_NAME, o, (uint32_t)(p - o));
}

SetFile set_file;

static void settings_defaults(void) {
  Options *o = &g.opt;
  o->bots = 7, o->impostors = 1, o->role = 0, o->color = 0;
  o->kill_cd = 90, o->kill_dist = 1;
  o->crew_vision = 4, o->imp_vision = 6, o->speed = 4;
  o->common = 1, o->longt = 1, o->shortt = 2;
  o->meetings = 1, o->meet_cd = 15, o->discuss = 15, o->vote = 120 / 5;
  o->confirm = 1, o->anon = 0, o->visual = 1, o->taskbar = 0;
}

static bool options_ok(const Options *o) {
  return o->bots >= 3 && o->bots <= 9 && o->impostors >= 1 && o->impostors <= 3 && o->role <= 2 && o->color < N_COLORS &&
         o->kill_cd >= 20 && o->kill_cd <= 120 && o->kill_dist <= 2 && o->crew_vision >= 1 && o->crew_vision <= 20 &&
         o->imp_vision >= 1 && o->imp_vision <= 20 && o->speed >= 2 && o->speed <= 12 && o->common <= 2 &&
         o->longt <= 3 && o->shortt <= 5 && o->meetings <= 9 && o->meet_cd <= 60 && o->discuss <= 120 &&
         o->vote <= 60 && o->confirm <= 1 && o->anon <= 1 && o->visual <= 1 && o->taskbar <= 2;
}

void settings_save(void) {
  set_file.h = (SaveHead){MAGIC_S, VERSION, 0, sizeof(SetFile)};
  set_file.opt = g.opt;
  plat_save(SAVE_SET, &set_file, sizeof set_file);
  bool live = g.screen != S_TITLE && g.screen != S_LOBBY && g.screen != S_END && g.screen != S_HOWTO;
  write_copy(live ? pack_game() : 0, &set_file);
}

static void settings_load(void) {
  settings_defaults();
  memset(&set_file, 0, sizeof set_file);
  uint32_t n;
  SetFile s;
  const uint8_t *d = plat_load(SAVE_SET, &n);
  if (d && n == sizeof s) memcpy(&s, d, sizeof s);
  else if (copy_get(SAVE_SET, (uint8_t *)&s, sizeof s) != (int32_t)sizeof s) return;
  if (s.h.magic != MAGIC_S || s.h.version != VERSION || s.h.size != sizeof(SetFile) || !options_ok(&s.opt)) return;
  set_file = s;
  g.opt = s.opt;
}

bool game_save(void) {
  uint32_t len = pack_game();
  if (!len) return false;
  bool ok = plat_save(SAVE_GAME, packed, len);
  set_file.h = (SaveHead){MAGIC_S, VERSION, 0, sizeof(SetFile)};
  set_file.opt = g.opt;
  plat_save(SAVE_SET, &set_file, sizeof set_file);
  write_copy(len, &set_file);
  return ok;
}

static bool game_sane(const Game *G) {
  if (G->nplayers < 4 || G->nplayers > MAXP || G->screen > S_KILLED || G->overlay > O_EMERGENCY || G->me >= MAXP ||
      !options_ok(&G->opt) || G->sab > SAB_COMMS || G->nlines > 32)
    return false;
  for (int i = 0; i < G->nplayers; i++) {
    const Player *P = &G->p[i];
    if (P->color >= N_COLORS || P->ntasks > MAX_TASKS || (P->vent != NONE && P->vent >= N_VENTS) || P->bot.plen > 40 ||
        P->bot.pi > 40 || !isfinite(P->pos.x) || !isfinite(P->pos.y) || fabsf(P->pos.x) > 40 || fabsf(P->pos.y) > 40)
      return false;
    for (int k = 0; k < P->ntasks; k++) {
      const Task *t = &P->tasks[k];
      if (t->def >= N_TASK_DEFS || t->step > TASK_DEFS[t->def].steps) return false;
      for (int j = 0; j < 3; j++)
        if (t->data[j] > 5) return false;
    }
    for (int k = 0; k < P->bot.plen; k++)
      if (P->bot.path[k] >= NAV_W * NAV_H) return false;
  }
  for (int l = 0; l < G->nlines; l++)
    if (G->lines[l].who >= MAXP) return false;
  return true;
}

bool game_load(void) {
  uint32_t n, room;
  const uint8_t *d = plat_load(SAVE_GAME, &n);
  uint8_t *buf = scratch_borrow(&room);
  if (room < 2 * PACKED_MAX) return false;
  Game *tmp = (Game *)(void *)(buf + PACKED_MAX);
  if (d && n <= PACKED_MAX) memcpy(buf, d, n);
  else {
    int32_t m = copy_get(SAVE_GAME, buf, PACKED_MAX);
    if (m < 0) return false;
    n = (uint32_t)m;
  }
  SaveHead h;
  if (n < sizeof h) return false;
  memcpy(&h, buf, sizeof h);
  if (h.magic != MAGIC_G || h.version != VERSION || h.size != sizeof(Game)) return false;
  if (np_lz_unpack(buf + sizeof h, n - sizeof h, (uint8_t *)tmp, sizeof *tmp) != (int32_t)sizeof *tmp) return false;
  if (!game_sane(tmp)) return false;
  g = *tmp;
  return true;
}

void game_forget(void) {
  plat_remove(SAVE_GAME);
  set_file.h = (SaveHead){MAGIC_S, VERSION, 0, sizeof(SetFile)};
  set_file.opt = g.opt;
  write_copy(0, &set_file);
}

void game_init(void) {
  for (int c = 0; c < N_COLORS; c++) gfx_mix_palette(color_pal[c], BODY_COLORS[c], SHADOW_COLORS[c], VISOR);
  ship_init();
  memset(&g, 0, sizeof g);
  settings_load();
  g.rng = 0x12345u ^ plat_millis();
  g.screen = S_TITLE;
  if (game_load()) {
    /* back where the game was left, paused */
    ship_init();
    if (g.screen == S_PLAY || g.screen == S_KILLED) g.back_screen = S_PLAY, g.screen = S_PAUSE, g.sel = 0;
  }
}
