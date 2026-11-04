/* Among Us: the rules against the game's numbers (gamesrc/amongus/spec/rules.md), called by play --rules. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "../src/game.h"
#include "../src/world.h"

int console_use(int i, int c);
int hud_action(int *what);
int kill_target(int i);
void game_forget(void);

static int fails, checks;
#define CHECK(c, ...)                                                                                                  \
  do {                                                                                                                 \
    checks++;                                                                                                          \
    if (!(c)) {                                                                                                        \
      fails++;                                                                                                         \
      printf("FAIL line %d: ", __LINE__);                                                                              \
      printf(__VA_ARGS__);                                                                                             \
      printf("\n");                                                                                                    \
    }                                                                                                                  \
  } while (0)
static bool near(float a, float b) { return fabsf(a - b) < 1e-3f; }

/* a round with these many bots and impostors, everybody frozen where they spawn */
static void round_of(int bots, int imps, int role) {
  g.opt.bots = (uint8_t)bots, g.opt.impostors = (uint8_t)imps, g.opt.role = (uint8_t)role;
  game_new();
  g.screen = S_PLAY;
}
static void run(float seconds) {
  for (int k = 0; k < (int)(seconds * 30 + 0.5f) && g.screen == S_PLAY; k++) game_tick(0);
}
static int count_imps(void) {
  int n = 0;
  for (int i = 0; i < g.nplayers; i++) n += g.p[i].imp;
  return n;
}
static int first(bool imp, int except) {
  for (int i = 0; i < g.nplayers; i++)
    if (g.p[i].alive && (g.p[i].imp != 0) == imp && i != except) return i;
  return -1;
}
static int console_of(int kind_or_type, bool is_type) {
  for (int c = 0; c < N_CONSOLES; c++)
    if (is_type ? (!CONSOLES[c].kind && (CONSOLES[c].types >> kind_or_type & 1)) : CONSOLES[c].kind == kind_or_type) return c;
  return -1;
}

int rules(void) {
  bots_frozen = true;
  /* the lobby's defaults (GameOptionsData) */
  const Options *o = &g.opt;
  CHECK(o->kill_cd == 90 && o->kill_dist == 1, "kill cooldown 45 s, distance medium");
  CHECK(o->crew_vision == 4 && o->imp_vision == 6 && o->speed == 4, "vision 1x / 1.5x, speed 1x");
  CHECK(o->common == 1 && o->longt == 1 && o->shortt == 2, "tasks 1 common, 1 long, 2 short");
  CHECK(o->meetings == 1 && o->meet_cd == 15 && o->discuss == 15 && o->vote * 5 == 120, "meetings 1, 15 s, 15 s, 120 s");
  CHECK(o->confirm && !o->anon && o->visual && o->taskbar == 0 && o->impostors == 1 && o->bots == 7, "the rest");

  /* players */
  round_of(7, 1, 1);
  CHECK(g.nplayers == 8 && count_imps() == 1 && !g.p[0].imp, "8 players, 1 impostor, the player a crewmate");
  CHECK(near(player_speed(0), 2.5f), "speed 2.5");
  CHECK(near(vision_radius(0), 5.0f), "crew vision 5");
  int imp = first(true, -1);
  CHECK(near(vision_radius(imp), 7.5f), "impostor vision 7.5");
  CHECK(near(kill_distance(), 1.8f) && near(kill_cooldown(), 45), "kill distance 1.8, cooldown 45");
  for (int i = 0; i < g.nplayers; i++) CHECK(near(g.p[i].kill_cd, 10), "the first kill after 10 s");
  /* tasks: the same common task for all, then long and short ones, one of each type */
  int common = g.p[0].tasks[0].def;
  for (int i = 0; i < g.nplayers; i++) {
    const Player *P = &g.p[i];
    CHECK(P->ntasks == 4, "4 tasks (%d)", P->ntasks);
    CHECK(P->tasks[0].def == common && TASK_DEFS[common].kind == 0, "the common task shared");
    CHECK(TASK_DEFS[P->tasks[1].def].kind == 1 && TASK_DEFS[P->tasks[2].def].kind == 2 && TASK_DEFS[P->tasks[3].def].kind == 2,
          "long then short");
    for (int a = 0; a < P->ntasks; a++)
      for (int b = a + 1; b < P->ntasks; b++)
        CHECK(TASK_DEFS[P->tasks[a].def].type != TASK_DEFS[P->tasks[b].def].type, "one task of each type");
    CHECK(P->tasks[0].data[0] < P->tasks[0].data[1] && P->tasks[0].data[1] < P->tasks[0].data[2], "wiring panels in order");
    for (int k = 0; k < P->ntasks; k++) CHECK(task_console(P, &P->tasks[k]) >= 0, "every task has a console");
  }
  /* impostors by players (GameOptionsData.MaxImpostors) */
  round_of(5, 3, 0);
  CHECK(count_imps() == 1, "6 players: 1 impostor");
  round_of(7, 3, 0);
  CHECK(count_imps() == 2, "8 players: 2 impostors");
  round_of(9, 3, 2);
  CHECK(count_imps() == 3 && g.p[0].imp, "10 players: 3, the player one of them");

  /* kills */
  round_of(7, 1, 2);
  int victim = first(false, -1);
  g.p[0].pos = (V2){g.p[victim].pos.x + 1.0f, g.p[victim].pos.y};
  g.p[0].kill_cd = 0;
  CHECK(kill_target(0) == victim, "a crewmate a unit away in reach");
  g.p[0].pos.x += 1.0f;
  CHECK(kill_target(0) != victim, "two units: out of reach");
  g.p[0].pos.x -= 1.0f;
  V2 at = g.p[victim].pos;
  kill_player(0, victim);
  CHECK(!g.p[victim].alive && g.bodies[0].on && g.bodies[0].who == victim, "a body where it stood");
  CHECK(near(g.p[0].pos.x, at.x) && near(g.p[0].kill_cd, 45), "the killer on the spot, 45 s to wait");
  /* the ghost: faster, no light, tasks still count */
  CHECK(near(player_speed(victim), 3.0f), "ghosts at 3.0");

  /* a body reported while the player's kill plays: the meeting after it */
  round_of(7, 1, 1);
  imp = first(true, -1);
  kill_player(imp, 0);
  start_meeting(1, 0);
  CHECK(g.screen == S_KILLED && g.meet_pending, "the kill plays on");
  for (int k = 0; k < 90 && g.screen == S_KILLED; k++) game_tick(0);
  CHECK(g.screen == S_REPORT && g.caller == 1 && g.reported == 0, "then the report");

  /* reports: within 3 units, not through a wall */
  round_of(7, 1, 1);
  victim = 1;
  imp = first(true, -1);
  kill_player(imp, victim);
  int what = -1;
  g.p[0].pos = (V2){g.bodies[0].pos.x + 2.9f, g.bodies[0].pos.y};
  CHECK(hud_action(&what) == 2, "a body 2.9 away: report");
  g.p[0].pos = (V2){g.bodies[0].pos.x + 3.2f, g.bodies[0].pos.y};
  CHECK(hud_action(&what) != 2, "3.2 away: no report");

  /* winning: impostors as many as the crew */
  round_of(3, 1, 1);
  imp = first(true, -1);
  for (int k = 0; k < 2; k++) kill_player(imp, first(false, -1));
  CHECK(g.screen == S_END && g.winner == 1 && g.end_reason == 1, "1 impostor, 1 crewmate: impostors win");
  /* the tasks */
  round_of(7, 1, 1);
  for (int i = 0; i < g.nplayers; i++)
    for (int k = 0; k < g.p[i].ntasks && !g.p[i].imp; k++)
      while (!task_done(&g.p[i].tasks[k])) task_step_done(i, k);
  CHECK(g.screen == S_END && g.winner == 0 && g.end_reason == 2, "every task done: crewmates win");
  /* dead crewmates' tasks count */
  round_of(7, 1, 1);
  imp = first(true, -1);
  kill_player(imp, 0);
  for (int i = 0; i < g.nplayers; i++)
    for (int k = 0; k < g.p[i].ntasks && !g.p[i].imp; k++)
      while (!task_done(&g.p[i].tasks[k])) task_step_done(i, k);
  CHECK(g.screen == S_END && g.winner == 0 && g.end_reason == 2, "ghosts' tasks finish it too");

  /* sabotage: 30 s to fix the reactor; two hands at once fix it */
  round_of(7, 1, 1);
  g.sab_cd = 0;
  CHECK(sabotage_ready(), "sabotage ready");
  sabotage(SAB_REACTOR);
  CHECK(g.sab == SAB_REACTOR && !sabotage_ready(), "one sabotage at a time");
  run(29.5f);
  CHECK(g.screen == S_PLAY, "29.5 s: still on");
  run(1);
  CHECK(g.screen == S_END && g.winner == 1 && g.end_reason == 3, "30 s: meltdown, impostors win");
  round_of(7, 1, 1);
  g.sab_cd = 0;
  sabotage(SAB_REACTOR);
  run(5);
  g.reactor_hold[0] = 1, g.reactor_hold[1] = 2;
  run(1.0f / 30);
  CHECK(g.sab == SAB_NONE && near(g.sab_cd, 30), "both hands: fixed, 30 s before the next");
  g.sab_cd = 0;
  sabotage(SAB_REACTOR);
  g.reactor_hold[0] = g.reactor_hold[1] = 1;
  run(1.0f / 30);
  CHECK(g.sab == SAB_REACTOR, "one player can't hold both");
  /* a meeting ends it */
  start_meeting(1, NONE);
  CHECK(g.sab == SAB_NONE, "a meeting ends the meltdown");
  /* oxygen: both keypads */
  round_of(7, 1, 1);
  g.sab_cd = 0;
  sabotage(SAB_O2);
  CHECK(g.o2_code < 100000, "a five-digit code");
  g.o2_done = 1;
  run(1);
  CHECK(g.sab == SAB_O2, "one keypad isn't enough");
  g.o2_done = 3;
  run(1.0f / 30);
  CHECK(g.sab == SAB_NONE, "both: oxygen restored");
  /* lights: crew vision down to 1, impostors keep theirs; all switches up fixes it */
  g.sab_cd = 0;
  sabotage(SAB_LIGHTS);
  CHECK(g.switches != 0x1F, "some switches down");
  run(1.5f);
  CHECK(near(vision_radius(0), 1.0f) && near(vision_radius(first(true, -1)), 7.5f), "lights out: 1 for the crew");
  g.switches = 0x1F;
  run(2);
  CHECK(g.sab == SAB_NONE && near(vision_radius(0), 5.0f), "switches up: light back");
  /* doors: 10 s closed, the room's 30 s cooldown */
  round_of(7, 1, 1);
  close_doors(R_STORAGE);
  int d = 0;
  while (d < N_DOORS && DOORS[d].room != R_STORAGE) d++;
  CHECK(door_closed(d), "storage doors closed");
  run(9.8f);
  CHECK(door_closed(d), "still closed at 9.8 s");
  run(0.4f);
  CHECK(!door_closed(d), "open after 10 s");
  close_doors(R_STORAGE);
  CHECK(!door_closed(d), "the room's cooldown");
  run(21);
  close_doors(R_STORAGE);
  CHECK(door_closed(d), "again after 30 s");

  /* the emergency button: one each, 15 s after the start, not during a crisis */
  round_of(7, 1, 1);
  int b = console_of(2, false);
  g.p[0].pos = (V2){CONSOLES[b].x, CONSOLES[b].y - 1.0f};
  CHECK(console_use(0, b) < 0, "the button waits 15 s");
  run(15.1f);
  CHECK(console_use(0, b) >= 0, "then it's ready");
  g.sab_cd = 0;
  sabotage(SAB_O2);
  CHECK(console_use(0, b) < 0, "not during a crisis");
  g.o2_done = 3;
  run(0.1f);
  start_meeting(0, NONE);
  CHECK(g.p[0].emergencies == 0 && g.screen == S_REPORT, "the meeting called, none left");

  /* votes: the most votes out, a tie or skips nobody */
  round_of(7, 1, 1);
  start_meeting(0, NONE);
  run(0);
  g.screen = S_MEETING, g.meet_stage = 1, g.meet_t = 0;
  imp = first(true, -1);
  memset(g.votes, NONE, sizeof g.votes);
  for (int i = 0; i < g.nplayers; i++) g.votes[i] = (uint8_t)(i < 5 ? imp : SKIP);
  game_tick(0);
  CHECK(g.meet_stage == 2 && g.ejected == imp, "5 against 3 skips: out");
  for (int k = 0; k < 6 * 30 && g.screen != S_EJECT; k++) game_tick(0);
  CHECK(g.screen == S_EJECT && !g.p[imp].alive, "ejected");
  for (int k = 0; k < 8 * 30 && g.screen == S_EJECT; k++) game_tick(0);
  CHECK(g.screen == S_END && g.winner == 0 && g.end_reason == 0, "the impostor out: crewmates win");
  round_of(7, 1, 1);
  start_meeting(0, NONE);
  g.screen = S_MEETING, g.meet_stage = 1, g.meet_t = 0;
  for (int i = 0; i < g.nplayers; i++) g.votes[i] = (uint8_t)(i < 4 ? 1 : 2);
  game_tick(0);
  CHECK(g.ejected == NONE && g.tie, "4 to 4: a tie, nobody out");
  round_of(7, 1, 1);
  start_meeting(0, NONE);
  g.screen = S_MEETING, g.meet_stage = 1, g.meet_t = 0;
  for (int i = 0; i < g.nplayers; i++) g.votes[i] = (uint8_t)(i < 5 ? SKIP : 1);
  game_tick(0);
  CHECK(g.ejected == NONE && !g.tie, "most skipped: nobody out");

  /* vents: impostors only, within 0.75 */
  round_of(7, 1, 2);
  g.p[0].pos = (V2){VENTS[0].x + 0.5f, VENTS[0].y};
  what = -1;
  CHECK(hud_action(&what) == 3 && what == 0, "the vent 0.5 away");
  g.p[0].imp = 0;
  CHECK(hud_action(&what) != 3, "not for crewmates");

  /* the tasks' consoles: every task step has one */
  for (int d2 = 0; d2 < N_TASK_DEFS; d2++) {
    Task t = {(uint8_t)d2, 0, {0, 2, 4}, 0, 0};
    for (; t.step < TASK_DEFS[d2].steps; t.step++) CHECK(task_console(0, &t) >= 0, "%s step %d", TASK_DEFS[d2].name, t.step);
  }
  bots_frozen = false;
  printf("RULES %d checks, %d failed\n", checks, fails);
  return fails;
}
