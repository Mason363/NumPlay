/* Among Us: the bots (the game has none: these play as people do). Crewmates walk the ship to their tasks and do
 * them, report the bodies they see and remember who they saw where; impostors fake tasks, kill when nobody else
 * would see, vent, sabotage and join fixes. They know only what they saw. */
#include <math.h>
#include <string.h>
#include "game.h"
#include "world.h"

enum { B_IDLE, B_WALK, B_TASK, B_FIX, B_HUNT, B_VENT, B_WAIT, B_REPORT, B_BUTTON };
/* what a walk is for */
enum { G_NONE, G_TASK, G_FAKE, G_FIX, G_BODY, G_BUTTON, G_WANDER, G_VENT, G_HUNT };

int kill_target(int i);
int console_use(int i, int c);
void walk(int i, V2 dir);

/* ---- the walking grid: Dijkstra from the goal, then down the distances ---- */

#define NCELLS (NAV_W * NAV_H)
static uint16_t ndist[NCELLS];
static uint16_t heap[NCELLS];
static int nheap;

static inline bool nav_ok(int x, int y) {
  return x >= 0 && y >= 0 && x < NAV_W && y < NAV_H && (NAV_BITS[y * ((NAV_W + 7) / 8) + (x >> 3)] >> (x & 7) & 1);
}
static inline V2 cell_pos(int c) {
  return (V2){NAV_X0 + (c % NAV_W + 0.5f) * NAV_CS, NAV_Y0 + (c / NAV_W + 0.5f) * NAV_CS};
}

static int nearest_cell(V2 p) {
  int cx = (int)floorf((p.x - NAV_X0) / NAV_CS), cy = (int)floorf((p.y - NAV_Y0) / NAV_CS);
  int best = -1;
  float bd = 1e9f;
  for (int r = 0; r <= 4 && best < 0; r++)
    for (int y = cy - r; y <= cy + r; y++)
      for (int x = cx - r; x <= cx + r; x++) {
        if (!nav_ok(x, y)) continue;
        V2 q = cell_pos(y * NAV_W + x);
        float d = (q.x - p.x) * (q.x - p.x) + (q.y - p.y) * (q.y - p.y);
        if (d < bd && (r > 2 || !ship_wall_between(p, q))) bd = d, best = y * NAV_W + x;
      }
  return best;
}

static void hpush(int c) {
  int i = nheap++;
  while (i > 0) {
    int p = (i - 1) / 2;
    if (ndist[heap[p]] <= ndist[c]) break;
    heap[i] = heap[p];
    i = p;
  }
  heap[i] = (uint16_t)c;
}
static int hpop(void) {
  int top = heap[0], last = heap[--nheap], i = 0;
  for (;;) {
    int ch = 2 * i + 1;
    if (ch >= nheap) break;
    if (ch + 1 < nheap && ndist[heap[ch + 1]] < ndist[heap[ch]]) ch++;
    if (ndist[heap[ch]] >= ndist[last]) break;
    heap[i] = heap[ch];
    i = ch;
  }
  if (nheap) heap[i] = (uint16_t)last;
  return top;
}

static bool door_blocks(V2 a, V2 b) {
  for (int d = 0; d < N_DOORS; d++) {
    if (!door_closed(d)) continue;
    const DoorRec *D = &DOORS[d];
    if (fabsf(a.x - D->x) > 2.5f || fabsf(a.y - D->y) > 2.5f) continue;
    for (int s = 0; s < 4; s++) {
      const Seg *S = &D->seg[s];
      float d1 = (S->x1 - S->x0) * (a.y - S->y0) - (S->y1 - S->y0) * (a.x - S->x0);
      float d2 = (S->x1 - S->x0) * (b.y - S->y0) - (S->y1 - S->y0) * (b.x - S->x0);
      if ((d1 > 0) == (d2 > 0)) continue;
      float d3 = (b.x - a.x) * (S->y0 - a.y) - (b.y - a.y) * (S->x0 - a.x);
      float d4 = (b.x - a.x) * (S->y1 - a.y) - (b.y - a.y) * (S->x1 - a.x);
      if ((d3 > 0) != (d4 > 0)) return true;
    }
  }
  return false;
}

static void dijkstra(int goal) {
  memset(ndist, 0xFF, sizeof ndist);
  nheap = 0;
  ndist[goal] = 0;
  hpush(goal);
  bool doors = false;
  for (int d = 0; d < N_DOORS; d++) doors |= door_closed(d);
  static const int8_t DX[8] = {1, -1, 0, 0, 1, 1, -1, -1}, DY[8] = {0, 0, 1, -1, 1, -1, 1, -1};
  while (nheap) {
    int c = hpop();
    int x = c % NAV_W, y = c / NAV_W;
    for (int k = 0; k < 8; k++) {
      int nx = x + DX[k], ny = y + DY[k];
      if (!nav_ok(nx, ny)) continue;
      if (k >= 4 && (!nav_ok(nx, y) || !nav_ok(x, ny))) continue;
      int n = ny * NAV_W + nx;
      uint16_t nd = (uint16_t)(ndist[c] + (k >= 4 ? 14 : 10));
      if (nd >= ndist[n]) continue;
      if (doors && door_blocks(cell_pos(c), cell_pos(n))) continue;
      ndist[n] = nd;
      hpush(n);
    }
  }
}

/* is the straight line from a to b walkable (every point on it in a walkable cell, no wall across) */
static bool straight(V2 a, V2 b) {
  float dx = b.x - a.x, dy = b.y - a.y, l = sqrtf(dx * dx + dy * dy);
  int n = (int)(l / 0.2f) + 1;
  for (int k = 1; k < n; k++) {
    float t = (float)k / n;
    int x = (int)floorf((a.x + dx * t - NAV_X0) / NAV_CS), y = (int)floorf((a.y + dy * t - NAV_Y0) / NAV_CS);
    if (!nav_ok(x, y)) return false;
  }
  return !ship_wall_between(a, b) && !door_blocks(a, b);
}

/* a path for bot i to goal (straight lines between corners of the grid's way) */
static bool plan(int i, V2 goal) {
  Player *P = &g.p[i];
  Bot *B = &P->bot;
  B->goal = goal;
  B->plen = B->pi = 0;
  if (!P->alive) return true;   /* (ghosts fly straight) */
  int from = nearest_cell(P->pos), to = nearest_cell(goal);
  if (from < 0 || to < 0) return false;
  dijkstra(to);
  if (ndist[from] == 0xFFFF) return false;
  /* down the distances, keeping only the cells a straight walk can't skip */
  uint16_t cells[160];
  int n = 0, c = from;
  cells[n++] = (uint16_t)c;
  static const int8_t DX[8] = {1, -1, 0, 0, 1, 1, -1, -1}, DY[8] = {0, 0, 1, -1, 1, -1, 1, -1};
  while (c != to && n < 160) {
    int x = c % NAV_W, y = c / NAV_W, best = -1;
    for (int k = 0; k < 8; k++) {
      int nx = x + DX[k], ny = y + DY[k];
      if (!nav_ok(nx, ny)) continue;
      int m = ny * NAV_W + nx;
      if (ndist[m] < ndist[c] && (best < 0 || ndist[m] < ndist[best])) best = m;
    }
    if (best < 0) break;
    c = best;
    cells[n++] = (uint16_t)c;
  }
  V2 at = P->pos;
  int k = 0;
  while (k < n - 1 && B->plen < 40) {
    int far = k + 1;
    for (int j = n - 1; j > k + 1; j--)
      if (straight(at, cell_pos(cells[j]))) {
        far = j;
        break;
      }
    B->path[B->plen++] = cells[far];
    at = cell_pos(cells[far]);
    k = far;
  }
  return true;
}

#ifdef HOST
/* (tests) is p walled in: no walkable cell within 1.5 units that a straight line reaches */
bool bot_enclosed(V2 p) {
  int cx = (int)floorf((p.x - NAV_X0) / NAV_CS), cy = (int)floorf((p.y - NAV_Y0) / NAV_CS);
  for (int y = cy - 3; y <= cy + 3; y++)
    for (int x = cx - 3; x <= cx + 3; x++)
      if (nav_ok(x, y) && !ship_wall_between(p, cell_pos(y * NAV_W + x))) return false;
  return true;
}
#endif

/* ---- perception ---- */

static float dist2(V2 a, V2 b) { return (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y); }

/* would anyone (but the impostors and the victim) see a kill at p */
static bool witnessed(int imp, int victim, V2 p) {
  for (int j = 0; j < g.nplayers; j++) {
    const Player *Q = &g.p[j];
    if (j == imp || j == victim || !Q->alive || Q->imp || Q->vent != NONE) continue;
    float r = vision_radius(j) + 0.5f;
    if (dist2(Q->pos, p) < r * r && !ship_light_between(Q->pos, p)) return true;
  }
  return false;
}

static void suspect(Bot *B, int who, int by) {
  int v = B->sus[who] + by;
  B->sus[who] = (int8_t)(v > 100 ? 100 : v < -100 ? -100 : v);
}

/* what bot i sees now */
void bot_see(int i) {
  Player *P = &g.p[i];
  Bot *B = &P->bot;
  if (!P->alive) return;
  for (int j = 0; j < g.nplayers; j++) {
    if (j == i) continue;
    Player *Q = &g.p[j];
    bool seen = Q->alive && can_see(i, j);
    if (seen) {
      B->seen_t[j] = g.t;
      B->seen_room[j] = Q->room;
      B->with_t[j] += 0.2f;
      if (B->with_t[j] > 60) B->with_t[j] = 60;
      /* a visual task proves a crewmate */
      if (g.opt.visual && Q->busy && !Q->imp && (Q->bot.task < MAX_TASKS) &&
          TASK_DEFS[Q->tasks[Q->bot.task].def].type == T_SUBMIT_SCAN)
        B->sus[j] = -100, B->scanned = (uint8_t)j;
      if (Q->vent_anim) {
        B->saw_vent = (uint8_t)j, B->saw_vent_room = Q->room;
        suspect(B, j, 100);
      }
    } else if (B->with_t[j] > 0) B->with_t[j] -= 0.05f;
  }
  if (P->imp) return;
  /* bodies */
  for (int b = 0; b < MAXP; b++) {
    const Body *D = &g.bodies[b];
    if (!D->on) continue;
    float r = vision_radius(i);
    if (dist2(P->pos, D->pos) > r * r || ship_light_between(P->pos, D->pos)) continue;
    if (B->state == B_REPORT && B->found_body == b) continue;
    B->found_body = (uint8_t)b;
    B->body_room = (uint8_t)ship_room_at(D->pos);
    /* who was near it */
    B->near_body = NONE;
    float bd = 4.0f * 4.0f;
    for (int j = 0; j < g.nplayers; j++) {
      if (j == i || !g.p[j].alive || g.p[j].vent != NONE) continue;
      float d = dist2(g.p[j].pos, D->pos);
      if (d < bd && can_see(i, j)) bd = d, B->near_body = (uint8_t)j;
    }
    if (B->near_body != NONE) suspect(B, B->near_body, 40);
    B->state = B_REPORT;
    B->target = (uint8_t)b;
    plan(i, D->pos);
  }
}

/* a kill seen as it happens (kill_player asks each living bot) */
void bot_saw_kill(int i, int killer) {
  Bot *B = &g.p[i].bot;
  if (g.p[i].imp) return;
  if (can_see(i, killer)) {
    B->saw_kill = (uint8_t)killer;
    B->saw_kill_room = g.p[killer].room;
    suspect(B, killer, 100);
  }
}

/* ---- deciding ---- */

/* how many crewmates (but j) are near player j */
static int company(int j) {
  int n = 0;
  for (int k = 0; k < g.nplayers; k++)
    if (k != j && g.p[k].alive && !g.p[k].imp && g.p[k].vent == NONE && dist2(g.p[k].pos, g.p[j].pos) < 4.5f * 4.5f) n++;
  return n;
}

/* an impostor goes after the crewmate most alone (and nearest): false if nobody is worth it */
static bool start_hunt(int i) {
  Player *P = &g.p[i];
  Bot *B = &P->bot;
  int best = -1;
  float bs = -1e9f;
  for (int j = 0; j < g.nplayers; j++) {
    const Player *Q = &g.p[j];
    if (!Q->alive || Q->imp || Q->vent != NONE) continue;
    float s = -company(j) * 8 - sqrtf(dist2(P->pos, Q->pos)) * 0.6f + rnd_f() * 4;
    if (s > bs) bs = s, best = j;
  }
  if (best < 0 || bs < -30) return false;
  B->state = B_HUNT;
  B->task = (uint8_t)best;
  B->t = 0;
  B->stuck_t = 0;
  return true;
}

static float task_time(const Task *t) {
  switch (TASK_DEFS[t->def].type) {
  case T_SWIPE_CARD: return 3.5f;
  case T_FIX_WIRING: return 4;
  case T_CLEAR_ASTEROIDS: return 16;
  case T_ALIGN_ENGINE: return 3;
  case T_SUBMIT_SCAN: return 10;
  case T_INSPECT_SAMPLE: return 2.5f;
  case T_FUEL_ENGINES: return 3.5f;
  case T_START_REACTOR: return 14;
  case T_EMPTY_CHUTE:
  case T_EMPTY_GARBAGE: return 3.5f;
  case T_UPLOAD_DATA: return 9;
  case T_CALIBRATE: return 6;
  case T_CHART_COURSE: return 4;
  case T_CLEAN_O2: return 6;
  case T_UNLOCK_MANIFOLDS: return 6;
  case T_STABILIZE: return 2.5f;
  case T_PRIME_SHIELDS: return 3;
  case T_DIVERT_POWER: return t->step ? 1.5f : 3;
  default: return 3;
  }
}

static void go(int i, V2 to, int why, int cons) {
  Bot *B = &g.p[i].bot;
  if (!plan(i, to)) {
    B->state = B_WAIT;
    B->t = 1 + rnd_f();
    return;
  }
  B->state = B_WALK;
  B->target = (uint8_t)why;
  B->cons = (uint8_t)cons;
  B->stuck_t = 0;
  B->last = g.p[i].pos;
}

static V2 stand(int c) { return (V2){CONSOLES[c].sx, CONSOLES[c].sy}; }

/* the nearest console of an unfinished task step; its task in *task */
static int next_task(int i, int *task) {
  Player *P = &g.p[i];
  int best = -1;
  float bd = 1e9f;
  for (int k = 0; k < P->ntasks; k++) {
    Task *t = &P->tasks[k];
    if (task_done(t) || t->timer > 0) continue;
    for (int c = 0; c < N_CONSOLES; c++) {
      if (!console_serves(c, t)) continue;
      float d = dist2(P->pos, stand(c));
      if (d < bd) bd = d, best = c, *task = k;
    }
  }
  return best;
}

/* the consoles that fix the sabotage and still need someone (reactor and oxygen have two) */
static int fix_consoles(int *out) {
  static const uint8_t FIX[] = {0, T_RESET_REACTOR, T_RESTORE_O2, T_FIX_LIGHTS, T_FIX_COMMS};
  int n = 0;
  for (int c = 0; c < N_CONSOLES && n < 4; c++) {
    const ConsoleRec *C = &CONSOLES[c];
    if (C->kind || !(C->types >> FIX[g.sab] & 1)) continue;
    if (g.sab == SAB_O2 && (g.o2_done >> (C->room == R_ADMIN ? 0 : 1) & 1)) continue;
    out[n++] = c;
  }
  return n;
}

static bool fixing(int j, int c) {
  const Bot *B = &g.p[j].bot;
  return (B->state == B_FIX || (B->state == B_WALK && B->target == G_FIX)) && B->cons == c;
}

/* a sabotage: the nearest free crewmate goes to each console that needs someone (after a moment to notice it) */
void bots_dispatch(void) {
  if (!g.sab || g.sab_age < 1.5f || (int)(g.t * 30) % 10) return;
  int cons[4], n = fix_consoles(cons);
  for (int k = 0; k < n; k++) {
    int c = cons[k];
    bool covered = false;
    for (int j = 0; j < g.nplayers; j++) covered |= is_bot(j) && g.p[j].alive && !g.p[j].imp && fixing(j, c);
    if (covered) continue;
    int best = -1;
    float bd = 1e9f;
    for (int j = 0; j < g.nplayers; j++) {
      const Player *Q = &g.p[j];
      if (!is_bot(j) || !Q->alive || Q->imp || Q->vent != NONE || Q->bot.state == B_REPORT) continue;
      bool busy = false;
      for (int m = 0; m < n; m++) busy |= fixing(j, cons[m]);
      if (busy) continue;
      float d = dist2(Q->pos, stand(c));
      /* (lights and comms can wait for a task to be done) */
      if (Q->bot.state == B_TASK && (g.sab == SAB_LIGHTS || g.sab == SAB_COMMS)) d += 200;
      if (d < bd) bd = d, best = j;
    }
    if (best < 0) continue;
    g.p[best].busy = 0;
    go(best, stand(cons[k]), G_FIX, cons[k]);
  }
}

static void wander(int i) {
  /* somewhere else on the ship: a console's spot */
  int c = rnd_n(N_CONSOLES);
  go(i, stand(c), G_WANDER, c);
}

static void decide(int i) {
  Player *P = &g.p[i];
  Bot *B = &P->bot;
  if (!P->imp) {
    int t = -1, c = next_task(i, &t);
    if (c >= 0) {
      go(i, stand(c), G_TASK, c);
      B->task = (uint8_t)t;
      return;
    }
    /* all done (or waiting on a sample): call a meeting on a strong suspicion, else walk around */
    if (P->alive && P->emergencies && g.emergency_cd <= 0 && !g.sab) {
      for (int j = 0; j < g.nplayers; j++)
        if (g.p[j].alive && B->sus[j] >= 80 && rnd_n(3) == 0) {
          go(i, stand(N_CONSOLES - 1 >= 0 ? 0 : 0), G_BUTTON, 0);
          for (int c2 = 0; c2 < N_CONSOLES; c2++)
            if (CONSOLES[c2].kind == 2) go(i, stand(c2), G_BUTTON, c2);
          return;
        }
    }
    wander(i);
    return;
  }
  /* an impostor */
  if (!P->alive) {
    if (sabotage_ready() && rnd_n(4) == 0) sabotage(SAB_LIGHTS + rnd_n(2));
    wander(i);
    return;
  }
  if (P->kill_cd < 5 && start_hunt(i)) return;
  /* a task to fake: the console of one of its (fake) tasks, or any */
  int t = -1, c = next_task(i, &t);
  if (c < 0 || rnd_n(3) == 0) c = rnd_n(N_CONSOLES);
  go(i, stand(c), G_FAKE, c);
}

/* ---- each tick ---- */

static void follow(int i) {
  Player *P = &g.p[i];
  Bot *B = &P->bot;
  V2 to = B->pi < B->plen ? cell_pos(B->path[B->pi]) : B->goal;
  if (!P->alive) to = B->goal;
  float dx = to.x - P->pos.x, dy = to.y - P->pos.y, d = sqrtf(dx * dx + dy * dy);
  float step = player_speed(i) * DT;
  if (d < step * 1.5f) {
    if (P->alive && B->pi < B->plen) {
      B->pi++;
      walk(i, (V2){dx, dy});
      return;
    }
    /* there */
    walk(i, (V2){0, 0});
    P->pos = ship_move(P->pos, (V2){to.x - P->pos.x, to.y - P->pos.y}, PLAYER_R);
    B->state = B_IDLE;
    B->plen = 0;
    return;
  }
  walk(i, (V2){dx, dy});
  /* stuck against something: plan again */
  B->stuck_t += DT;
  if (B->stuck_t > 1.0f) {
    if (dist2(P->pos, B->last) < 0.3f * 0.3f) {
      if (!plan(i, B->goal)) B->state = B_IDLE;
    }
    B->stuck_t = 0;
    B->last = P->pos;
  }
}

static void arrive(int i) {
  Player *P = &g.p[i];
  Bot *B = &P->bot;
  int why = B->target;
  switch (why) {
  case G_TASK: {
    Task *t = &P->tasks[B->task];
    if (!console_serves(B->cons, t)) {
      B->state = B_IDLE;
      return;
    }
    B->state = B_TASK;
    B->t = task_time(t) * (0.9f + 0.6f * rnd_f());
    P->busy = 1;
    return;
  }
  case G_FAKE:
    B->state = B_TASK;
    B->task = NONE;
    B->t = 2 + rnd_f() * 6;
    return;
  case G_FIX:
    if (!g.sab) {
      B->state = B_IDLE;
      return;
    }
    B->state = B_FIX;
    B->t = g.sab == SAB_REACTOR ? 99 : g.sab == SAB_O2 ? 4 + rnd_f() * 2 : g.sab == SAB_LIGHTS ? 3 + rnd_f() * 2
                                                                                            : 4 + rnd_f() * 3;
    return;
  case G_BUTTON:
    if (P->alive && P->emergencies && g.emergency_cd <= 0 && !g.sab && g.screen == S_PLAY) {
      start_meeting(i, NONE);
      return;
    }
    B->state = B_IDLE;
    return;
  case G_WANDER:
    B->state = B_WAIT;
    B->t = 1 + rnd_f() * 4;
    return;
  default:
    B->state = B_IDLE;
    return;
  }
}

static void impostor_tick(int i) {
  Player *P = &g.p[i];
  Bot *B = &P->bot;

  /* kill whoever is in reach when nobody would see */
  if (P->kill_cd <= 0 && P->vent == NONE) {
    int t = kill_target(i);
    if (t >= 0 && !witnessed(i, t, g.p[t].pos) && !witnessed(i, t, P->pos) && rnd_n(4) == 0) {
      kill_player(i, t);
      /* away from the body: a vent close by, else somewhere else */
      int v = nearest_vent(P->pos, 3.5f);
      if (v >= 0 && rnd_n(3)) {
        go(i, (V2){VENTS[v].sx, VENTS[v].sy}, G_VENT, v);
      } else if (rnd_n(8) == 0 && g.screen == S_PLAY) {
        start_meeting(i, t);   /* a self report */
      } else
        wander(i);
      return;
    }
  }
  /* the kill nearly ready: off to find someone */
  if (P->kill_cd < 4 && P->vent == NONE && rnd_n(15) == 0 &&
      (B->state == B_TASK || B->state == B_WAIT || B->state == B_IDLE ||
       (B->state == B_WALK && (B->target == G_FAKE || B->target == G_WANDER))))
    start_hunt(i);
  /* sabotage now and then, and the lights when about to kill */
  if (sabotage_ready() && P->alive) {
    if (P->kill_cd < 3 && B->state == B_HUNT && company(B->task) && rnd_n(30 * 4) == 0) sabotage(SAB_LIGHTS);
    else if (rnd_n(30 * 35) == 0) {
      int r = rnd_n(100);
      sabotage(r < 35 ? SAB_LIGHTS : r < 55 ? SAB_COMMS : r < 80 ? SAB_REACTOR : SAB_O2);
    }
  }
  if (rnd_n(30 * 40) == 0) {
    static const uint8_t DR[] = {R_CAFETERIA, R_STORAGE, R_ELECTRICAL, R_MEDBAY, R_SECURITY, R_UPPER_ENGINE,
                                 R_LOWER_ENGINE};
    int r = B->state == B_HUNT ? g.p[B->task].room : DR[rnd_n(7)];
    for (unsigned k = 0; k < sizeof DR; k++)
      if (DR[k] == r) close_doors(r);
  }
}

void bot_init(int i) {
  Bot *B = &g.p[i].bot;
  memset(B, 0, sizeof *B);
  B->state = B_WAIT;
  B->t = 0.5f + rnd_f() * 1.5f;
  B->saw_kill = B->saw_vent = B->near_body = B->found_body = NONE;
  B->task = NONE;
  B->with = NONE;
  B->scanned = NONE;
  B->think = rnd_f() * 0.2f;
}

void bot_tick(int i) {
  Player *P = &g.p[i];
  Bot *B = &P->bot;
  if (P->vent_anim) {
    walk(i, (V2){0, 0});
    return;
  }
  /* look around five times a second */
  B->think -= DT;
  if (B->think <= 0) {
    B->think += 0.2f;
    bot_see(i);
    if (g.screen != S_PLAY) return;
  }
  if (P->imp && P->alive) {
    impostor_tick(i);
    if (g.screen != S_PLAY) return;
  }
  if (P->imp && P->alive && g.sab && rnd_n(400) == 0 && B->state != B_HUNT && B->state != B_VENT) {
    /* to the sabotage, as if to fix it */
    int cons[4], n = fix_consoles(cons);
    if (n) go(i, stand(cons[rnd_n(n)]), G_FAKE, cons[0]);
  }
  switch (B->state) {
  case B_IDLE:
    walk(i, (V2){0, 0});
    P->busy = 0;
    decide(i);
    break;
  case B_WAIT:
    walk(i, (V2){0, 0});
    B->t -= DT;
    if (B->t <= 0) B->state = B_IDLE;
    break;
  case B_WALK:
    follow(i);
    if (B->state == B_IDLE) {
      if (B->target == G_VENT && P->imp && P->alive) {
        int v = B->cons;
        enter_vent(i, v);
        B->state = B_VENT;
        B->t = 1 + rnd_f() * 2;
        B->plen = (uint8_t)rnd_n(3);
      } else
        arrive(i);
    }
    break;
  case B_REPORT: {
    const Body *D = &g.bodies[B->target];
    if (!D->on) {
      B->state = B_IDLE;
      break;
    }
    if (dist2(P->pos, D->pos) < 2.5f * 2.5f && !ship_wall_between(P->pos, D->pos)) {
      walk(i, (V2){0, 0});
      start_meeting(i, D->who);
      return;
    }
    follow(i);
    if (B->state == B_IDLE) B->state = B_REPORT, plan(i, D->pos);
    break;
  }
  case B_TASK:
    walk(i, (V2){0, 0});
    B->t -= DT;
    if (B->t <= 0) {
      P->busy = 0;
      if (B->task < MAX_TASKS) {
        Task *t = &P->tasks[B->task];
        int type = TASK_DEFS[t->def].type;
        if (type == T_INSPECT_SAMPLE && t->timer < 0) t->timer = 60;
        else if (type == T_CLEAR_ASTEROIDS) {
          while (!task_done(t)) task_step_done(i, B->task);
        } else
          task_step_done(i, B->task);
      }
      B->state = B_WAIT;
      B->t = 0.4f + rnd_f() * 1.6f;
      /* now and then a look around before the next task */
      if (!P->imp && rnd_n(4) == 0) wander(i);
    }
    break;
  case B_FIX: {
    walk(i, (V2){0, 0});
    const ConsoleRec *C = &CONSOLES[B->cons];
    if (!g.sab) {
      B->state = B_IDLE;
      break;
    }
    if (g.sab == SAB_REACTOR) {
      g.reactor_hold[C->id & 1] = (uint8_t)i;
      break;
    }
    B->t -= DT;
    if (B->t <= 0) {
      if (g.sab == SAB_O2) g.o2_done |= (uint8_t)(C->room == R_ADMIN ? 1 : 2);
      else if (g.sab == SAB_LIGHTS) g.switches = 0x1F;
      else if (g.sab == SAB_COMMS) fix_sabotage();
      B->state = B_IDLE;
    }
    break;
  }
  case B_HUNT: {
    const Player *Q = &g.p[B->task];
    if (!Q->alive || P->kill_cd > 8) {
      B->state = B_IDLE;
      break;
    }
    /* in company for a while: someone else */
    B->stuck_t = company(B->task) > 1 ? B->stuck_t + DT : 0;
    if (B->stuck_t > 5) {
      if (!start_hunt(i)) B->state = B_IDLE;
      break;
    }
    B->t -= DT;
    if (B->t <= 0 || B->pi >= B->plen) {
      B->t = 0.7f;
      if (!plan(i, Q->pos)) {
        B->state = B_IDLE;
        break;
      }
    }
    if (dist2(P->pos, Q->pos) < 0.8f * 0.8f) walk(i, (V2){0, 0});
    else {
      follow(i);
      if (B->state == B_IDLE) B->state = B_HUNT;
    }
    break;
  }
  case B_VENT:
    walk(i, (V2){0, 0});
    B->t -= DT;
    if (B->t <= 0 && P->vent != NONE) {
      if (B->plen) {
        /* to a linked vent */
        int links[3], n = 0;
        for (int k = 0; k < 3; k++)
          if (VENTS[P->vent].link[k] >= 0) links[n++] = VENTS[P->vent].link[k];
        if (n) move_vent(i, links[rnd_n(n)]);
        B->plen--;
        B->t = 0.6f + rnd_f();
      } else {
        /* out when nobody would see */
        if (!witnessed(i, NONE, P->pos) || B->t < -6) {
          exit_vent(i);
          B->state = B_WAIT;
          B->t = 0.6f;
        }
      }
    }
    break;
  default:
    B->state = B_IDLE;
  }
}

/* ---- meetings ---- */

enum {
  L_BODY_IN, L_NEAR_BODY, L_SAW_KILL, L_SAW_VENT, L_WAS_IN, L_WITH, L_SUS, L_WHO, L_WHERE, L_SKIP, L_NOT_ME,
  L_SCANNED, L_BUTTON
};

static void say(int who, int kind, int a, int b) {
  if (g.nlines >= 32) {
    memmove(g.lines, g.lines + 1, sizeof g.lines[0] * 31);
    g.nlines--;
    if (g.line_shown) g.line_shown--;
  }
  g.lines[g.nlines++] = (Line){(uint8_t)who, (uint8_t)kind, (uint8_t)a, (uint8_t)b};
}

/* each bot's lines for this meeting, said one after another in bot_meeting_tick */
static uint8_t queue_who[48], queue_kind[48], queue_a[48], queue_b[48];
static float queue_at[48];
static int nqueue, qnext;

static void queue(int who, int kind, int a, int b, float at) {
  if (nqueue >= 48) return;
  queue_who[nqueue] = (uint8_t)who, queue_kind[nqueue] = (uint8_t)kind, queue_a[nqueue] = (uint8_t)a;
  queue_b[nqueue] = (uint8_t)b, queue_at[nqueue] = at;
  nqueue++;
}

static int top_suspect(int i, int *score) {
  const Bot *B = &g.p[i].bot;
  int best = -1, bs = -1000;
  for (int j = 0; j < g.nplayers; j++) {
    if (j == i || !g.p[j].alive) continue;
    if (g.p[i].imp && g.p[j].imp) continue;
    if (B->sus[j] > bs) bs = B->sus[j], best = j;
  }
  *score = bs;
  return best;
}

void bot_meeting_start(void) {
  nqueue = qnext = 0;
  g.nlines = 0;
  g.line_shown = 0;
  float at = 1.5f;
  /* a body: who each crewmate last saw in its room lately is a little suspect */
  if (g.reported != NONE) {
    int room = NONE;
    for (int b = 0; b < MAXP; b++)
      if (g.bodies[b].on && g.bodies[b].who == g.reported) room = ship_room_at(g.bodies[b].pos);
    for (int i = 0; i < g.nplayers; i++) {
      if (!is_bot(i) || !g.p[i].alive || g.p[i].imp) continue;
      Bot *B = &g.p[i].bot;
      for (int j = 0; j < g.nplayers; j++)
        if (j != i && g.p[j].alive && B->seen_room[j] == room && room != R_HALLWAY && g.t - B->seen_t[j] < 25)
          suspect(B, j, B->with_t[j] > 20 ? 5 : 15);
    }
  }
  /* the caller first */
  int c = g.caller;
  if (is_bot(c)) {
    Bot *B = &g.p[c].bot;
    if (g.reported != NONE) {
      queue(c, L_BODY_IN, g.reported, g.p[c].imp ? g.p[c].room : B->body_room, at), at += 1.6f;
      if (!g.p[c].imp && B->saw_kill != NONE && g.p[B->saw_kill].alive)
        queue(c, L_SAW_KILL, B->saw_kill, B->saw_kill_room, at), at += 1.6f;
      else if (!g.p[c].imp && B->near_body != NONE && g.p[B->near_body].alive)
        queue(c, L_NEAR_BODY, B->near_body, B->body_room, at), at += 1.6f;
      else if (g.p[c].imp) {
        /* a lie: whoever it last saw */
        int best = -1;
        float bt = -1;
        for (int j = 0; j < g.nplayers; j++)
          if (j != c && g.p[j].alive && !g.p[j].imp && B->seen_t[j] > bt) bt = B->seen_t[j], best = j;
        if (best >= 0 && rnd_n(2)) queue(c, L_NEAR_BODY, best, g.p[c].room, at), at += 1.6f;
      }
    } else {
      int s, w = top_suspect(c, &s);
      if (w >= 0 && s >= 40) queue(c, B->saw_vent == w ? L_SAW_VENT : L_SUS, w, B->saw_vent_room, at), at += 1.6f;
      else queue(c, L_BUTTON, 0, 0, at), at += 1.6f;
    }
  }
  /* the others, in a random order */
  int order[MAXP], n = 0;
  for (int i = 0; i < g.nplayers; i++)
    if (is_bot(i) && i != c && g.p[i].alive) order[n++] = i;
  for (int k = n - 1; k > 0; k--) {
    int j = rnd_n(k + 1), t = order[k];
    order[k] = order[j], order[j] = t;
  }
  for (int k = 0; k < n; k++) {
    int i = order[k];
    Bot *B = &g.p[i].bot;
    at += 0.6f + rnd_f() * 1.6f;
    if (!g.p[i].imp && B->saw_kill != NONE && g.p[B->saw_kill].alive) {
      queue(i, L_SAW_KILL, B->saw_kill, B->saw_kill_room, at);
      continue;
    }
    if (!g.p[i].imp && B->saw_vent != NONE && g.p[B->saw_vent].alive) {
      queue(i, L_SAW_VENT, B->saw_vent, B->saw_vent_room, at);
      continue;
    }
    int r = rnd_n(10);
    if (!g.p[i].imp && B->scanned != NONE && g.p[B->scanned].alive && r < 3) {
      queue(i, L_SCANNED, B->scanned, 0, at);
      continue;
    }
    if (r < 2) {
      queue(i, g.reported != NONE ? L_WHERE : L_WHO, g.reported, 0, at);
      continue;
    }
    /* an alibi: where it was, with whom (impostors make one up) */
    int with = NONE;
    float wt = 8;
    for (int j = 0; j < g.nplayers; j++)
      if (j != i && g.p[j].alive && B->with_t[j] > wt) wt = B->with_t[j], with = j;
    int room = g.p[i].room;
    if (g.p[i].imp && rnd_n(2)) room = TASK_DEFS[g.p[i].tasks[rnd_n(g.p[i].ntasks)].def].room;
    if (with != NONE && !g.p[i].imp) queue(i, L_WITH, with, room, at);
    else queue(i, L_WAS_IN, room, 0, at);
    int s, w = top_suspect(i, &s);
    if (w >= 0 && (s >= 30 || (g.p[i].imp && rnd_n(3) == 0))) at += 1.2f, queue(i, L_SUS, w, g.p[w].room, at);
  }
}

void bot_meeting_tick(void) {
  while (qnext < nqueue && g.meet_t >= queue_at[qnext]) {
    int who = queue_who[qnext];
    if (g.p[who].alive) say(who, queue_kind[qnext], queue_a[qnext], queue_b[qnext]);
    /* what's said moves the listeners: an accusation weighs by how much the speaker is trusted */
    int k = queue_kind[qnext], a = queue_a[qnext];
    if (k == L_SAW_KILL || k == L_SAW_VENT || k == L_SUS || k == L_NEAR_BODY) {
      for (int i = 0; i < g.nplayers; i++) {
        if (i == who || !is_bot(i) || !g.p[i].alive || g.p[i].imp) continue;
        Bot *B = &g.p[i].bot;
        if (i == a) {
          suspect(B, who, 25);   /* being accused */
          continue;
        }
        int trust = B->sus[who] < -30 ? 2 : B->sus[who] > 40 ? 0 : 1;
        int w = (k == L_SAW_KILL ? 45 : k == L_SAW_VENT ? 40 : k == L_NEAR_BODY ? 20 : 12) * trust;
        suspect(B, a, w);
      }
    }
    if (k == L_SCANNED)
      for (int i = 0; i < g.nplayers; i++)
        if (is_bot(i) && !g.p[i].imp && g.p[i].bot.sus[who] < 30) suspect(&g.p[i].bot, a, -40);
    qnext++;
  }
}

/* the bots' votes (each at its own moment in the voting time) */
void bot_votes(void) {
  for (int i = 0; i < g.nplayers; i++) {
    if (!is_bot(i) || !g.p[i].alive || g.votes[i] != NONE) continue;
    if (g.meet_stage != 1) continue;
    /* decide after a few seconds of voting, or soon after everyone spoke */
    float when = 2.0f + (float)((i * 37 + g.nlines * 11) % 9);
    if (g.meet_t < when) continue;
    Bot *B = &g.p[i].bot;
    int s, w = top_suspect(i, &s);
    if (g.p[i].imp) {
      /* with the crowd against a crewmate, or skip */
      int counts[MAXP + 1] = {0};
      for (int j = 0; j < g.nplayers; j++)
        if (g.votes[j] != NONE) counts[g.votes[j]]++;
      int best = SKIP, bc = 0;
      for (int j = 0; j < g.nplayers; j++)
        if (!g.p[j].imp && g.p[j].alive && counts[j] > bc) bc = counts[j], best = j;
      g.votes[i] = (uint8_t)(bc ? best : (w >= 0 && s > 10 && rnd_n(2)) ? w : SKIP);
    } else {
      /* sure enough: that one; half sure: with the crowd if it's the same one */
      int counts[MAXP + 1] = {0};
      for (int j = 0; j < g.nplayers; j++)
        if (g.votes[j] != NONE && g.votes[j] != SKIP) counts[g.votes[j]]++;
      bool crowd = w >= 0 && counts[w] >= 2 && s >= 15 && B->sus[w] > -30;
      g.votes[i] = (uint8_t)(w >= 0 && (s >= 30 || crowd) ? w : SKIP);
    }
  }
}
