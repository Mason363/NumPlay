/* Plays Among Us on a computer, with scripted keys, and saves frames.
 *
 *   play [--frames N] [--keys "10-40:right,50:ok,..."] [--shots 30,60|every:N] [--out DIR] [--saves DIR]
 *        [--start crew|imp] [--auto] [--fuzz SEED] [--seed N] [--draw] [--trace N] [--bots N]
 *
 * Keys: left right up down ok exe back home tool 0-9. "a-b:key" holds the key from frame a to b, "a:key" for one
 * frame (a frame is a game tick, 1/30 s). --start begins a game past the menus, as a crewmate or an impostor.
 * --auto lets the bots' brain play the player too (a whole game to its end). --fuzz presses random keys.
 * Screenshots are DIR/shot_<frame>.ppm. At the end: END screen S winner W reason R t T alive A tasks P. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "../src/game.h"
#include "../src/gfx.h"
#include "../src/mg.h"
#include "../src/plat.h"
#include "../src/world.h"

extern uint32_t host_keys, host_time;
extern bool game_running, autopilot;
void host_shot(const char *path);
void host_save_dir(const char *d);

typedef struct { int a, b; uint32_t k; } Hold;
static Hold holds[4096];
static int nholds;
static int shots[4096], nshots, every;

static uint32_t key_of(const char *s) {
  static const char *const names[] = {"left", "right", "up", "down", "ok", "exe", "back", "home", "tool"};
  static const uint32_t bits[] = {K_LEFT, K_RIGHT, K_UP, K_DOWN, K_OK, K_EXE, K_BACK, K_HOME, K_TOOL};
  for (int i = 0; i < 9; i++)
    if (!strcmp(s, names[i])) return bits[i];
  if (s[0] >= '0' && s[0] <= '9' && !s[1]) return (uint32_t)K_D0 << (s[0] - '0');
  fprintf(stderr, "unknown key %s\n", s);
  exit(2);
}

static void parse_keys(const char *spec) {
  static char buf[65536];
  snprintf(buf, sizeof buf, "%s", spec);
  for (char *tok = strtok(buf, ","); tok; tok = strtok(NULL, ",")) {
    int a, b;
    char k[16];
    if (sscanf(tok, "%d-%d:%15s", &a, &b, k) == 3) holds[nholds++] = (Hold){a, b, key_of(k)};
    else if (sscanf(tok, "%d:%15s", &a, k) == 2) holds[nholds++] = (Hold){a, a, key_of(k)};
  }
}

/* opens a minigame straight away (tests and screenshots): its kind as in mg.h */
static void open_minigame(int kind) {
  static const int8_t TYPE[] = {T_SWIPE_CARD, T_FIX_WIRING, T_CLEAR_ASTEROIDS, T_ALIGN_ENGINE, T_SUBMIT_SCAN,
                                T_INSPECT_SAMPLE, T_FUEL_ENGINES, T_START_REACTOR, T_EMPTY_GARBAGE, T_UPLOAD_DATA,
                                T_CALIBRATE, T_CHART_COURSE, T_CLEAN_O2, T_UNLOCK_MANIFOLDS, T_STABILIZE,
                                T_PRIME_SHIELDS, T_DIVERT_POWER, T_DIVERT_POWER};
  g.screen = S_PLAY;
  Player *P = &g.p[g.me];
  if (kind < 18) {
    for (int d = 0; d < N_TASK_DEFS; d++)
      if (TASK_DEFS[d].type == TYPE[kind]) {
        memset(&P->tasks[0], 0, sizeof P->tasks[0]);
        P->tasks[0].def = (uint8_t)d;
        P->tasks[0].data[0] = 0, P->tasks[0].data[1] = 2, P->tasks[0].data[2] = 4;
        if (kind == 17) P->tasks[0].step = 1;
        if (TYPE[kind] == T_INSPECT_SAMPLE) P->tasks[0].timer = -1;
        break;
      }
    int c = task_console(P, &P->tasks[0]);
    P->pos = (V2){CONSOLES[c].sx, CONSOLES[c].sy};
    mg_open(c, 0);
  } else if (kind <= 21) {
    static const int SAB[] = {SAB_REACTOR, SAB_LIGHTS, SAB_COMMS, SAB_O2};
    g.sab_cd = 0;
    sabotage(SAB[kind - 18]);
    for (int c = 0; c < N_CONSOLES; c++) {
      const ConsoleRec *C = &CONSOLES[c];
      int t = SAB[kind - 18] == SAB_REACTOR ? T_RESET_REACTOR : SAB[kind - 18] == SAB_LIGHTS ? T_FIX_LIGHTS
              : SAB[kind - 18] == SAB_COMMS ? T_FIX_COMMS : T_RESTORE_O2;
      if (!C->kind && (C->types >> t & 1)) {
        P->pos = (V2){C->sx, C->sy};
        mg_open(c, MAX_TASKS);
        break;
      }
    }
  } else {
    for (int c = 0; c < N_CONSOLES; c++)
      if (CONSOLES[c].kind == (kind == 22 ? 3 : 2)) {
        P->pos = (V2){CONSOLES[c].sx, CONSOLES[c].sy};
        mg_open(c, MAX_TASKS);
        break;
      }
  }
  g.cam = (V2){P->pos.x, P->pos.y + FEET_Y};
}

/* a scene set up straight away (screenshots): kill, report, emergency, meeting, eject, map, sabmap, admin, reactor,
 * lights */
static void scene(const char *name) {
  Player *P = &g.p[g.me];
  int other = g.me == 1 ? 2 : 1, imp = -1;
  for (int i = 0; i < g.nplayers; i++)
    if (g.p[i].imp && i != g.me) imp = i;
  g.screen = S_PLAY;
  g.screen_t = 0;
  if (!strcmp(name, "kill")) {
    if (imp >= 0) kill_player(imp, g.me);
  } else if (!strcmp(name, "report")) {
    kill_player(imp >= 0 ? imp : other, other);
    start_meeting(g.me, other);
  } else if (!strcmp(name, "emergency")) {
    start_meeting(other, NONE);
  } else if (!strcmp(name, "meeting") || !strcmp(name, "eject")) {
    kill_player(imp >= 0 ? imp : other, other);
    start_meeting(3, other);
    g.screen = S_REPORT;
    g.screen_t = 2.6f;
    game_tick(0);
    if (!strcmp(name, "eject")) {
      g.meet_t = 999;
      g.meet_stage = 1;
      for (int i = 0; i < g.nplayers; i++) g.votes[i] = (uint8_t)(imp >= 0 ? imp : 4);
      game_tick(0);
      g.meet_t = 6;
      game_tick(0);
    }
  } else if (!strcmp(name, "map")) {
    map_open(O_MAP);
  } else if (!strcmp(name, "sabmap")) {
    map_open(O_SABMAP);
  } else if (!strcmp(name, "admin")) {
    map_open(O_ADMIN);
  } else if (!strcmp(name, "reactor") || !strcmp(name, "lights") || !strcmp(name, "o2") || !strcmp(name, "comms")) {
    g.sab_cd = 0;
    sabotage(name[0] == 'r' ? SAB_REACTOR : name[0] == 'l' ? SAB_LIGHTS : name[0] == 'o' ? SAB_O2 : SAB_COMMS);
  }
  (void)P;
}

/* ---- a player for the minigames: the keys that solve the open one, as a person would press them ---- */

static uint32_t tap(int f, uint32_t k) { return f & 1 ? 0 : k; }   /* a press every other frame */
static uint32_t toward(float dx, float dy, float slack) {
  uint32_t k = 0;
  if (dx > slack) k |= K_RIGHT;
  if (dx < -slack) k |= K_LEFT;
  if (dy > slack) k |= K_DOWN;
  if (dy < -slack) k |= K_UP;
  return k;
}
static uint32_t digit(int d) { return (uint32_t)K_D0 << d; }

static uint32_t solve(int f) {
  if (g.overlay != O_TASK || M->done) return 0;
  switch (M->kind) {
  case MG_CARD: return M->phase == 0 ? tap(f, K_OK) : M->phase == 3 ? 0 : K_RIGHT;
  case MG_WIRES:
    if (M->phase == 0) {
      int want = 0;
      while (want < 4 && M->state[want] != NONE) want++;
      return want == M->n ? tap(f, K_OK) : tap(f, K_DOWN);
    } else {
      int want = 0;
      while (want < 4 && M->order[4 + want] != M->order[M->sel]) want++;
      return want == M->n ? tap(f, K_OK) : tap(f, K_DOWN);
    }
  case MG_ASTEROIDS: {
    int best = -1;
    float bd = 1e9f;
    for (int k = 0; k < 10; k++) {
      const Thing *a = &M->th[k];
      if (a->on != 1 || a->x > 255 || a->x < 70) continue;
      float d = (a->x - M->a) * (a->x - M->a) + (a->y - M->b) * (a->y - M->b);
      if (d < bd) bd = d, best = k;
    }
    if (best < 0) return 0;
    if (bd < 12 * 12) return tap(f, K_OK);
    return toward(M->th[best].x - M->a, M->th[best].y - M->b, 3);
  }
  case MG_ALIGN: return M->a > 0.04f ? K_DOWN : M->a < -0.04f ? K_UP : 0;
  case MG_SAMPLE:
    if (M->phase == 0) return tap(f, K_OK);
    if (M->phase == 3) return M->sel == M->n ? tap(f, K_OK) : tap(f, K_RIGHT);
    return 0;
  case MG_FUEL: return K_OK;
  case MG_SIMON:
    if (M->phase == 1) {
      static const int CELL_DIGIT[9] = {7, 8, 9, 4, 5, 6, 1, 2, 3};
      return tap(f, digit(CELL_DIGIT[M->order[M->n]]));
    }
    return 0;
  case MG_GARBAGE: return K_DOWN;
  case MG_UPLOAD: return M->phase == 0 ? tap(f, K_OK) : 0;
  case MG_CALIBRATE: {
    float a = M->v[M->n < 3 ? M->n : 0];
    if (a > 3.14159f) a -= 6.2831853f;
    return fabsf(a) < 0.15f ? tap(f, K_OK) : 0;
  }
  case MG_COURSE: return K_RIGHT;
  case MG_LEAVES: {
    if (M->sel != NONE) return K_OK | K_LEFT;
    int best = -1;
    float bd = 1e9f;
    for (int k = 0; k < 6; k++) {
      const Thing *a = &M->th[k];
      if (a->on != 1) continue;
      float d = (a->x - M->a) * (a->x - M->a) + (a->y - M->b) * (a->y - M->b);
      if (d < bd) bd = d, best = k;
    }
    if (best < 0) return 0;
    if (bd < 8 * 8) return tap(f, K_OK);
    return toward(M->th[best].x - M->a, M->th[best].y - M->b, 2);
  }
  case MG_MANIFOLDS: {
    if (M->c > 0) return 0;
    int want = 0;
    while (want < 10 && M->order[want] != M->n) want++;
    if (want == M->sel) return tap(f, K_OK);
    if (want / 5 != M->sel / 5) return tap(f, K_DOWN);
    return tap(f, K_RIGHT);
  }
  case MG_STEERING: return toward(-M->a, -M->b, 1.5f);
  case MG_SHIELDS:
    for (int k = 0; k < 7; k++)
      if (!(M->n >> k & 1)) return M->sel == k ? tap(f, K_OK) : tap(f, digit(k + 1));
    return 0;
  case MG_DIVERT: return K_UP;
  case MG_ACCEPT: return tap(f, K_OK);
  case MG_HANDS: return K_OK;
  case MG_LIGHTS:
    for (int k = 0; k < 5; k++)
      if (!(g.switches >> k & 1)) return tap(f, digit(k + 1));
    return 0;
  case MG_COMMS: return M->a < M->c - 0.03f ? K_RIGHT : M->a > M->c + 0.03f ? K_LEFT : 0;
  case MG_KEYPAD: {
    if (M->c > 0 || M->phase) return 0;
    char code[8];
    snprintf(code, sizeof code, "%05u", (unsigned)g.o2_code);
    if (M->n < 5) return (f & 1) ? 0 : digit(code[M->n] - '0');
    return tap(f, K_OK);
  }
  case MG_EMERGENCY: return M->b > 0.4f ? tap(f, K_OK) : 0;
  default: return 0;
  }
}

static uint32_t frng = 1;
static uint32_t frnd(void) {
  frng ^= frng << 13, frng ^= frng >> 17, frng ^= frng << 5;
  return frng;
}

int main(int argc, char **argv) {
  int frames = 300, trace = 0, start = 0, bots = -1, mgkind = -1, imps = -1, killcd = -1, walk_frames = 0;
  const char *scene_name = NULL;
  float at_x = 1e9f, at_y = 0;
  uint32_t seed = 0;
  bool fuzz = false, draw = false, shot_end = false, walled = false, solving = false, do_rules = false;
  const char *out = "build/shots";
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--frames")) frames = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--keys")) parse_keys(argv[++i]);
    else if (!strcmp(argv[i], "--out")) out = argv[++i];
    else if (!strcmp(argv[i], "--saves")) host_save_dir(argv[++i]);
    else if (!strcmp(argv[i], "--start")) start = !strcmp(argv[++i], "imp") ? 2 : 1;
    else if (!strcmp(argv[i], "--auto")) autopilot = true;
    else if (!strcmp(argv[i], "--draw")) draw = true;
    else if (!strcmp(argv[i], "--walled")) walled = true;
    else if (!strcmp(argv[i], "--trace")) trace = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--seed")) seed = (uint32_t)atoi(argv[++i]);
    else if (!strcmp(argv[i], "--bots")) bots = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--imps")) imps = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--killcd")) killcd = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--mg")) mgkind = atoi(argv[++i]), start = start ? start : 1;
    else if (!strcmp(argv[i], "--scene")) scene_name = argv[++i], start = start ? start : 1;
    else if (!strcmp(argv[i], "--at")) at_x = (float)atof(argv[++i]), at_y = (float)atof(argv[++i]);
    else if (!strcmp(argv[i], "--walk")) walk_frames = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--shot-end")) shot_end = true;
    else if (!strcmp(argv[i], "--solve")) solving = true;
    else if (!strcmp(argv[i], "--rules")) do_rules = true;
    else if (!strcmp(argv[i], "--fuzz")) fuzz = true, frng = (uint32_t)atoi(argv[++i]) * 2654435761u + 1;
    else if (!strcmp(argv[i], "--shots")) {
      const char *s = argv[++i];
      if (!strncmp(s, "every:", 6)) every = atoi(s + 6);
      else
        for (const char *p = s; *p;) {
          shots[nshots++] = atoi(p);
          p = strchr(p, ',');
          if (!p) break;
          p++;
        }
    }
  }
  mkdir(out, 0755);
  static uint16_t band[SCREEN_W * BAND];
  gfx_init(band);
  host_time = 1000 + seed * 7919;
  game_init();
  if (do_rules) {
    int rules(void);
    return rules() ? 1 : 0;
  }
  printf("LOADED screen %d back %d t %.3f me (%.3f,%.3f) players %d color %d bots %d\n", g.screen, g.back_screen, g.t,
         g.p[g.me].pos.x, g.p[g.me].pos.y, g.nplayers, g.opt.color, g.opt.bots);
  if (start) {
    if (bots >= 0) g.opt.bots = (uint8_t)bots;
    if (imps >= 0) g.opt.impostors = (uint8_t)imps;
    if (killcd >= 0) g.opt.kill_cd = (uint8_t)(killcd * 2);
    g.opt.role = (uint8_t)start;
    g.rng = seed * 2654435761u + 77;
    game_new();
    g.screen = S_PLAY;
    /* (the round a little under way: the bots spread out) */
    for (int f = 0; f < walk_frames; f++) {
      for (int i = 0; i < g.nplayers; i++) g.p[i].kill_cd = 30, g.p[i].emergencies = 0;
      g.sab_cd = 30;
      game_tick(0), host_time += 33;
    }
    for (int i = 0; i < g.nplayers; i++) g.p[i].emergencies = g.opt.meetings;
    if (at_x < 1e8f) g.p[g.me].pos = (V2){at_x, at_y}, g.cam = (V2){at_x, at_y + FEET_Y};
    if (mgkind >= 0) open_minigame(mgkind);
    if (scene_name) scene(scene_name);
  }
  uint32_t fk = 0;
  int meetings = 0, ejects = 0, wrong = 0, prev_scr = -1;
  static int hist[16];
  int restarts = 0;
  for (int f = 0; f < frames; f++) {
    if (!game_running) {
      /* quit: the fuzz starts the app again (the saves read back), the others stop */
      if (!fuzz) break;
      game_running = true;
      restarts++;
      game_init();
    }
    uint32_t k = 0;
    for (int i = 0; i < nholds; i++)
      if (f >= holds[i].a && f <= holds[i].b) k |= holds[i].k;
    if (fuzz) {
      if (frnd() % 6 == 0) {
        uint32_t r = frnd();
        fk = r & (K_LEFT | K_RIGHT | K_UP | K_DOWN | K_OK | K_EXE | K_TOOL);
        if (frnd() % 12 == 0) fk |= K_BACK;
        if (frnd() % 10 == 0) fk |= (uint32_t)K_D0 << (frnd() % 10);
        if (frnd() % 300 == 0) fk = K_HOME;
      }
      k = fk;
    }
    if (autopilot && (g.screen == S_INTRO || g.screen == S_EJECT) && f % 20 == 0) k |= K_OK;
    if (solving) {
      /* (nobody kills, sabotages or calls a meeting meanwhile) */
      for (int i = 0; i < g.nplayers; i++)
        if (i != g.me) g.p[i].kill_cd = 99, g.p[i].emergencies = 0, g.p[i].bot.found_body = NONE;
      if (g.sab == SAB_NONE) g.sab_cd = 99;
      /* (the reactor's other hand: someone else holds it) */
      if (g.sab == SAB_REACTOR && g.overlay == O_TASK && M->kind == MG_HANDS)
        g.reactor_hold[(CONSOLES[M->cons].id & 1) ^ 1] = (uint8_t)(g.me == 1 ? 2 : 1);
      k |= solve(f);
      if (mgkind >= 0 && g.overlay == O_NONE) {
        printf("SOLVED mg %d at frame %d step %d sab %d\n", mgkind, f, g.p[g.me].tasks[0].step, g.sab);
        break;
      }
    }
    host_keys = k;
    hist[g.screen & 15]++;
    if (g.screen == S_REPORT && prev_scr != S_REPORT) meetings++;
    if (g.screen == S_EJECT && prev_scr != S_EJECT) ejects += g.ejected != NONE, wrong += g.ejected != NONE && !g.p[g.ejected].imp;
    prev_scr = g.screen;
    game_tick(k);
    /* nobody walled in (inside a table or a wall) */
    if (walled && f % 15 == 0)
      for (int i = 0; i < g.nplayers; i++) {
        extern bool bot_enclosed(V2 p);
        static bool told[MAXP];
        if (g.p[i].alive && g.p[i].vent == NONE && !told[i] && bot_enclosed(g.p[i].pos))
          told[i] = true, printf("WALLED f%d player %d (%.3f,%.3f)\n", f, i, g.p[i].pos.x, g.p[i].pos.y);
      }
    host_time += 33;
    bool shot = every ? f % every == 0 : false;
    for (int i = 0; i < nshots; i++) shot |= shots[i] == f;
    if (shot || draw || (fuzz && f % 4 == 0)) game_draw();
    if (shot) {
      char p[512];
      snprintf(p, sizeof p, "%s/shot_%04d.ppm", out, f);
      host_shot(p);
    }
    if (trace && f % trace == 0)
      printf("t%5d screen %d ov %d me (%.2f %.2f) room %d alive %d imp %d sab %d tasks %.2f\n", f, g.screen, g.overlay,
             g.p[g.me].pos.x, g.p[g.me].pos.y, g.p[g.me].room, g.p[g.me].alive, g.p[g.me].imp, g.sab, tasks_progress());
    if (g.screen == S_END && autopilot && g.screen_t > 3) {
      if (shot_end) {
        char p[512];
        snprintf(p, sizeof p, "%s/end.ppm", out);
        game_draw();
        host_shot(p);
      }
      break;
    }
  }
  if (fuzz)
    printf("RESTARTS %d\n", restarts),
    printf("SCREENS title %d lobby %d intro %d play %d report %d meeting %d eject %d end %d howto %d pause %d killed %d\n",
           hist[0], hist[1], hist[2], hist[3], hist[4], hist[5], hist[6], hist[7], hist[8], hist[9], hist[10]);
  int alive = 0;
  for (int i = 0; i < g.nplayers; i++) alive += g.p[i].alive;
  printf("END screen %d winner %d reason %d t %.1f alive %d tasks %.2f imp %d meetings %d ejected %d wrong %d\n", g.screen,
         g.winner, g.end_reason, g.t, alive, tasks_progress(), g.p[g.me].imp, meetings, ejects, wrong);
  return 0;
}
