/* Among Us: a round on the ship: the player's keys, everyone's moves, doors, sabotage clocks. */
#include <math.h>
#include <string.h>
#include "game.h"
#include "plat.h"
#include "world.h"

/* ---- what the action button would show (HudManager.UseButton / ReportButton / VentButton) ---- */

enum { ACT_NONE, ACT_USE, ACT_REPORT, ACT_VENT, ACT_EMERGENCY, ACT_ADMIN, ACT_CAMS };

static bool in_reach(const Player *P, int c) {
  const ConsoleRec *C = &CONSOLES[c];
  float dx = P->pos.x - C->x, dy = P->pos.y - C->y;
  if (dx * dx + dy * dy > C->dist * C->dist) return false;
  if (C->below && P->pos.y > C->y) return false;
  return true;
}

/* the sabotage this console fixes (0 none) */
static int fixes(int c) {
  const ConsoleRec *C = &CONSOLES[c];
  if (C->kind) return 0;
  if (C->types >> T_RESET_REACTOR & 1) return SAB_REACTOR;
  if (C->types >> T_RESTORE_O2 & 1) return SAB_O2;
  if (C->types >> T_FIX_LIGHTS & 1) return SAB_LIGHTS;
  if (C->types >> T_FIX_COMMS & 1) return SAB_COMMS;
  return 0;
}

/* can player i use console c now: the task it serves (>= 0), MAX_TASKS for a sabotage or a system, -1 no */
int console_use(int i, int c) {
  const Player *P = &g.p[i];
  const ConsoleRec *C = &CONSOLES[c];
  if (!in_reach(P, c)) return -1;
  if (C->kind == 2) return P->alive && P->emergencies > 0 && g.emergency_cd <= 0 && g.sab != SAB_REACTOR &&
                                 g.sab != SAB_O2
                             ? MAX_TASKS
                             : -1;
  if (C->kind) return MAX_TASKS;
  int f = fixes(c);
  if (f) {
    if (!P->alive || g.sab != f) return -1;
    if (f == SAB_O2 && (g.o2_done >> (C->room == R_ADMIN ? 0 : 1) & 1)) return -1;
    return MAX_TASKS;
  }
  if (P->imp) return -1;
  for (int k = 0; k < P->ntasks; k++)
    if (console_serves(c, &P->tasks[k]) && !(P->tasks[k].timer > 0)) return k;
  return -1;
}

static int action(int *what) {
  Player *P = &g.p[g.me];
  if (P->vent != NONE) return ACT_VENT;
  if (P->alive) {
    for (int b = 0; b < MAXP; b++) {
      const Body *B = &g.bodies[b];
      if (!B->on) continue;
      float dx = B->pos.x - P->pos.x, dy = B->pos.y - P->pos.y;
      if (dx * dx + dy * dy <= 9.0f && !ship_wall_between(P->pos, B->pos)) {
        *what = b;
        return ACT_REPORT;
      }
    }
  }
  int best = -1;
  float bd = 99;
  for (int c = 0; c < N_CONSOLES; c++) {
    if (console_use(g.me, c) < 0) continue;
    float dx = CONSOLES[c].x - P->pos.x, dy = CONSOLES[c].y - P->pos.y, d = dx * dx + dy * dy;
    if (d < bd) bd = d, best = c;
  }
  if (P->imp && P->alive) {
    int v = nearest_vent(P->pos, 0.75f);
    if (v >= 0) {
      float dx = VENTS[v].x - P->pos.x, dy = VENTS[v].y - P->pos.y;
      if (best < 0 || dx * dx + dy * dy < bd) {
        *what = v;
        return ACT_VENT;
      }
    }
  }
  if (best < 0) return ACT_NONE;
  *what = best;
  switch (CONSOLES[best].kind) {
  case 1: return ACT_ADMIN;
  case 2: return ACT_EMERGENCY;
  case 3: return ACT_CAMS;
  default: return ACT_USE;
  }
}

int hud_action(int *what) { return action(what); }

/* the crewmate the impostor would kill (PlayerControl.FindClosestTarget) */
int kill_target(int i) {
  const Player *P = &g.p[i];
  if (!P->imp || !P->alive || P->vent != NONE) return -1;
  float r = kill_distance(), bd = r * r;
  int best = -1;
  for (int j = 0; j < g.nplayers; j++) {
    const Player *Q = &g.p[j];
    if (j == i || !Q->alive || Q->imp || Q->vent != NONE) continue;
    float dx = Q->pos.x - P->pos.x, dy = Q->pos.y - P->pos.y, d = dx * dx + dy * dy;
    if (d < bd && !ship_wall_between(P->pos, Q->pos)) bd = d, best = j;
  }
  return best;
}

/* ---- moving ---- */

void walk(int i, V2 dir) {
  Player *P = &g.p[i];
  float l = sqrtf(dir.x * dir.x + dir.y * dir.y);
  if (l < 1e-4f || P->vent != NONE || P->vent_anim) {
    P->vel = (V2){0, 0};
    P->walking = 0;
    return;
  }
  float s = player_speed(i) * DT / l;
  V2 d = {dir.x * s, dir.y * s};
  if (P->alive) P->pos = ship_move(P->pos, d, PLAYER_R);
  else {
    P->pos.x += d.x, P->pos.y += d.y;
    if (P->pos.x < -26) P->pos.x = -26;
    if (P->pos.x > 21) P->pos.x = 21;
    if (P->pos.y < -20) P->pos.y = -20;
    if (P->pos.y > 8) P->pos.y = 8;
  }
  P->vel = d;
  P->walking = 1;
  if (d.x < -1e-4f) P->flip = 1;
  if (d.x > 1e-4f) P->flip = 0;
}

/* ---- the world's clocks ---- */

static void world_tick(void) {
  g.t += DT;
  for (int d = 0; d < N_DOORS; d++) {
    if (g.door_t[d] > 0) {
      g.door_t[d] -= DT;
      if (g.door_t[d] <= 0) g.door_anim[d] = 0;
    }
    g.door_anim[d] += DT;
  }
  for (int r = 0; r < N_ROOMS; r++)
    if (g.door_cd[r] > 0) g.door_cd[r] -= DT;
  if (g.emergency_cd > 0) g.emergency_cd -= DT;
  if (g.sab == SAB_NONE && g.sab_cd > 0) g.sab_cd -= DT;
  /* lights: the switches' value moves the light (SwitchSystem: 255 to 0 over about a second) */
  bool dark = g.sab == SAB_LIGHTS;
  g.light += dark ? -DT : DT;
  if (g.light < 0) g.light = 0;
  if (g.light > 1) g.light = 1;
  if (g.sab == SAB_LIGHTS && g.switches == 0x1F) fix_sabotage();
  if (g.sab == SAB_REACTOR || g.sab == SAB_O2) {
    if (g.sab == SAB_REACTOR && g.reactor_hold[0] != NONE && g.reactor_hold[1] != NONE &&
        g.reactor_hold[0] != g.reactor_hold[1])
      fix_sabotage();
    else if (g.sab == SAB_O2 && g.o2_done == 3)
      fix_sabotage();
    else {
      g.sab_t -= DT;
      if (g.sab_t <= 0) {
        g.winner = 1;
        g.end_reason = 3;
        g.screen = S_END;
        g.screen_t = 0;
        g.overlay = O_NONE;
        return;
      }
    }
  }
  g.reactor_hold[0] = g.reactor_hold[1] = NONE;
  for (int i = 0; i < g.nplayers; i++) {
    Player *P = &g.p[i];
    if (P->imp && P->alive && P->kill_cd > 0) P->kill_cd -= DT;
    for (int k = 0; k < P->ntasks; k++)
      if (P->tasks[k].timer > 0) {
        P->tasks[k].timer -= DT;
        if (P->tasks[k].timer < 0) P->tasks[k].timer = 0;
      }
    /* the vent jump's frames (EnterVent 0.375 s, ExitVent 0.54 s) */
    if (P->vent_anim) {
      P->anim_t += DT;
      if (P->vent_anim == 1 && P->anim_t >= 0.375f) P->vent_anim = 0;
      else if (P->vent_anim == 2 && P->anim_t >= 0.54f) {
        P->vent_anim = 0;
        P->vent = NONE;
      }
    } else if (P->walking) P->anim_t += DT;
    else P->anim_t = 0;
    P->room = (uint8_t)ship_room_at(P->pos);
  }
}

/* ---- the player's turn ---- */

static uint32_t vent_key_dir(uint32_t pressed, int v) {
  /* the arrow nearest the direction to each linked vent */
  float kx = (pressed & K_RIGHT) ? 1 : (pressed & K_LEFT) ? -1 : 0;
  float ky = (pressed & K_UP) ? 1 : (pressed & K_DOWN) ? -1 : 0;
  if (!kx && !ky) return NONE;
  int best = NONE;
  float bd = 0.3f;
  for (int k = 0; k < 3; k++) {
    int w = VENTS[v].link[k];
    if (w < 0) continue;
    float dx = VENTS[w].x - VENTS[v].x, dy = VENTS[w].y - VENTS[v].y, l = sqrtf(dx * dx + dy * dy);
    float dot = (dx * kx + dy * ky) / l / sqrtf(kx * kx + ky * ky);
    if (dot > bd) bd = dot, best = w;
  }
  return (uint32_t)best;
}

static void do_action(void) {
  int what = -1;
  int a = action(&what);
  Player *P = &g.p[g.me];
  switch (a) {
  case ACT_REPORT:
    start_meeting(g.me, g.bodies[what].who);
    break;
  case ACT_VENT:
    if (P->vent != NONE) exit_vent(g.me);
    else enter_vent(g.me, what);
    break;
  case ACT_EMERGENCY:
  case ACT_CAMS:
    mg_open(what, MAX_TASKS);
    break;
  case ACT_ADMIN:
    g.overlay = O_ADMIN;
    g.sel = 0;
    break;
  case ACT_USE:
    mg_open(what, console_use(g.me, what));
    break;
  default:
    break;
  }
}

void play_tick(uint32_t keys, uint32_t pressed) {
  Player *P = &g.p[g.me];
  bool human = g.screen == S_PLAY;
  if (!human) walk(g.me, (V2){0, 0});
  else if (g.overlay == O_NONE) {
    V2 dir = {0, 0};
    if (keys & K_LEFT) dir.x -= 1;
    if (keys & K_RIGHT) dir.x += 1;
    if (keys & K_UP) dir.y += 1;
    if (keys & K_DOWN) dir.y -= 1;
    if (P->vent != NONE && !P->vent_anim) {
      uint32_t w = vent_key_dir(pressed, P->vent);
      if (w != NONE) move_vent(g.me, (int)w);
      dir = (V2){0, 0};
    }
    walk(g.me, dir);
    if (pressed & K_OK) do_action();
    if ((pressed & K_EXE) && P->kill_cd <= 0) {
      int t = kill_target(g.me);
      if (t >= 0) kill_player(g.me, t);
    }
    if (pressed & K_TOOL) {
      g.overlay = P->imp ? O_SABMAP : O_MAP;
      g.sel = 0;
    }
    if (pressed & K_BACK) {
      g.back_screen = S_PLAY;
      g.screen = S_PAUSE;
      g.sel = 0;
      return;
    }
  } else {
    walk(g.me, (V2){0, 0});
    if (g.overlay == O_TASK) mg_tick(keys, pressed);
    else map_tick(pressed, g.overlay);
  }
  int screen = g.screen;
  if (screen != S_PLAY && screen != S_KILLED) return;
  for (int i = 0; i < g.nplayers; i++)
    if (i != g.me) bot_tick(i);
  if (g.screen != screen) return;
  world_tick();
  /* bodies a crewmate bot sees */
  check_win();
}
