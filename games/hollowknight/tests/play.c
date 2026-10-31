/* Host runner: draws rooms and plays the game with scripted keys, saving screenshots.
 *   play DATA.bin --room NAME --cam X,Y --shot PATH */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "../src/game.h"

extern uint32_t host_keys, host_time;
void host_shot(const char *path);

/* every way into every room (each gate or door of every room, to where it leads), as the game goes through it: the
 * Knight must come in seen, in the room, free, and able to move (--gates all, or the rooms one leads from) */
static int gate_kind_of(const Ent *e) {
  if (e->flags & G_DOOR) return GATE_DOOR;
  const char *n = str_at(e->s0);
  return strstr(n, "top") ? GATE_TOP : strstr(n, "right") ? GATE_RIGHT : strstr(n, "left") ? GATE_LEFT
         : strstr(n, "bot") ? GATE_BOTTOM : GATE_DOOR;
}
static bool shielded;   /* (the enemies can't hurt the Knight: what is checked is the way, not the fights) */
static void run_ticks(int n, uint32_t keys) {
  for (int i = 0; i < n; i++) {
    if (shielded) g_hero.cs.invulnerable = true;
    game_tick(stag_tick(shop_tick(inv_tick(keys))));
    if (getenv("HKGATETRACE"))
      printf("   %s pos %.3f,%.3f v %.2f,%.2f state %d ts %d ground %d hidden %d\n", room_name(g_room.id), g_hero.body.x,
             g_hero.body.y, g_hero.body.vx, g_hero.body.vy, g_hero.state, g_hero.transition_state, g_hero.cs.on_ground,
             g_hero.hidden);
  }
}
/* every hazard respawn point of every room (the gates' and the hazard triggers'): ground under it, in the room; and
 * whether the room has hazards at all */
static int markers_test(void) {
  int bad = 0;
  for (int r = 0; r < NUM_ROOMS; r++) {
    if (!room_load(r)) continue;
    int n;
    const Ent *es = room_ents(&n);
    int hazards = 0;
    for (int i = 0; i < n; i++)
      if (es[i].type == ENT_DAMAGE && (int)es[i].p0 >= HAZ_SPIKES) hazards++;
    for (int i = 0; i < n; i++) {
      int m = -1;
      if (es[i].type == ENT_GATE && es[i].p3 > 0) m = (int)es[i].p3 - 1;
      if (es[i].type == ENT_HAZARD_TRIGGER) m = es[i].a;
      if (m < 0) continue;
      if (m >= n || es[m].type != ENT_HAZARD_MARKER) {
        printf("BAD %s ent %d: marker %d is not a hazard marker\n", room_name(r), i, m);
        bad++;
        continue;
      }
      PhysHit hit;
      float x = es[m].x0, y = es[m].y0;
      bool ground = phys_ray(x, y, 0, -1, 50, CF_TERRAIN, &hit);
      bool in = x > 0 && x < g_room.h->w && y > 0 && y < g_room.h->h;
      if (!ground || !in || (ground && hit.dist > 15))
        printf("%s %s ent %d (%s) marker %d at %.2f,%.2f: %s%s (hazards in the room: %d)\n", hazards ? "BAD " : "note", room_name(r), i,
               es[i].type == ENT_GATE ? str_at(es[i].s0) : "trigger", m, x, y, ground ? "" : "no ground below", in ? "" : " outside the room", hazards);
      bad += hazards && (!ground || !in);
    }
  }
  printf("markers: %d bad\n", bad);
  return bad ? 1 : 0;
}

/* from a room, a long walk of made up keys (runs, jumps, dashes, slashes, as a player might), following the Knight
 * from room to room: he must never leave the room's bounds, be without control long, or be wedged in place */
static uint32_t wander_rng = 1;
static uint32_t wander_rand(void) { return wander_rng = wander_rng * 1103515245u + 12345u, wander_rng >> 16; }
static int wander_test(const char *which, int seed, int ticks) {
  int fails = 0;
  for (int r = 0; r < NUM_ROOMS; r++) {
    if (strcmp(which, "all") && strcmp(which, room_name(r))) continue;
    game_new();
    g_pd.can_dash = true, g_pd.has_dash = true;
    if (!room_load(r)) continue;
    /* (in at its first gate that leads somewhere, else its middle) */
    int n;
    const Ent *es = room_ents(&n);
    float sx = g_room.h->w / 2, sy = g_room.h->h / 2;
    for (int i = 0; i < n; i++)
      if (es[i].type == ENT_GATE && !(es[i].flags & G_DOOR)) {
        sx = (es[i].x0 + es[i].x1) / 2, sy = (es[i].y0 + es[i].y1) / 2;
        sx = sx < 3 ? 3 : sx > g_room.h->w - 3 ? g_room.h->w - 3 : sx;
        sy = sy < 3 ? 3 : sy > g_room.h->h - 3 ? g_room.h->h - 3 : sy;
        break;
      }
    char start[64];
    snprintf(start, sizeof start, "%s", room_name(r));
    if (!game_enter(r, sx, sy, true)) continue;
    g_hero.body.y = hero_ground_y(sx, sy);
    wander_rng = (uint32_t)(seed * 7919 + r * 104729 + 1);
    uint32_t keys = 0;
    int left = 0, no_input = 0, out = 0, still = 0, unseen = 0, reports = 0;
    float lx = g_hero.body.x, ly = g_hero.body.y;
    const Hero *h = &g_hero;
    for (int t = 0; t < ticks && reports < 3; t++) {
      if (--left <= 0) {
        uint32_t q = wander_rand();
        keys = (q & 3) == 0 ? 0 : (q & 3) == 1 ? K_LEFT : K_RIGHT;
        if (q >> 2 & 1) keys |= K_JUMP;
        if ((q >> 3 & 7) == 0) keys |= K_DASH;
        if ((q >> 6 & 3) == 0) keys |= K_ATTACK;
        if ((q >> 8 & 15) == 0) keys |= K_DOWN;
        if ((q >> 12 & 15) == 0) keys |= K_UP;
        left = 5 + (int)(wander_rand() % 60);
      }
      uint32_t k = keys;
      if ((keys & (K_JUMP | K_ATTACK | K_DASH)) && (t & 15) > 11) k &= ~(K_JUMP | K_ATTACK | K_DASH);   /* (pressed again) */
      game_tick(stag_tick(shop_tick(inv_tick(k))));
      if (getenv("HKWANDERTRACE") && t >= atoi(getenv("HKWANDERTRACE")) - 60 && t <= atoi(getenv("HKWANDERTRACE")) + 20)
        printf("   %d %s keys %02x pos %.3f,%.3f v %.2f,%.2f state %d ground %d wall %d\n", t, room_name(g_room.id), k, h->body.x,
               h->body.y, h->body.vx, h->body.vy, h->state, h->cs.on_ground, h->cs.touching_wall);
      bool busy = h->cs.transitioning || h->cs.dead || h->cs.hazard_death || h->cs.hazard_respawning || game_changing_room() ||
                  h->hidden;
      float w = g_room.h->w, hh = g_room.h->h, x = h->body.x, y = h->body.y;
      if (!busy && (x < -3 || x > w + 3 || y < -3 || y > hh + 3)) {
        if (++out == 25) printf("FAIL wander %s (seed %d) tick %d: out of %s at %.2f,%.2f\n", start, seed, t, room_name(g_room.id), x, y), fails++, reports++;
      } else
        out = 0;
      if (!busy && h->state == HS_NO_INPUT && !h->control_relinquished) {
        if (++no_input == 500) printf("FAIL wander %s (seed %d) tick %d: no control for 10 s in %s at %.2f,%.2f\n", start, seed, t, room_name(g_room.id), x, y), fails++, reports++;
      } else
        no_input = 0;
      if (!busy && fabsf(x - lx) < 0.01f && fabsf(y - ly) < 0.01f && (k & (K_LEFT | K_RIGHT | K_JUMP))) {
        if (still == 399 && getenv("HKWANDERWHO")) {
          printf("   (shop %d inv %d msg %d dialogue %d map %d collect %d accepting %d relinq %d)\n", shop_open(), inv_open(), msg_shown(),
                 dialogue_box_shown(), map_shown(), collect_active(), h->accepting_input, h->control_relinquished);
        }
        if (++still == 400) printf("note wander %s (seed %d) tick %d: in place 8 s, keys moving, in %s at %.2f,%.2f state %d\n", start, seed, t, room_name(g_room.id), x, y, h->state), reports++;
      } else
        still = 0;
      /* (and seen: drawn every so often, inside the view) */
      if (!busy && t % 10 == 0) {
        extern uint32_t g_hero_undrawn;
        uint32_t before = g_hero_undrawn;
        game_draw();
        if (g_hero_undrawn != before) printf("FAIL wander %s (seed %d) tick %d: not drawn in %s at %.2f,%.2f\n", start, seed, t, room_name(g_room.id), x, y), fails++, reports++;
      }
      /* (and the view moves smoothly: no jump across the screen in a step) */
      static float lcx, lcy;
      static int calm;
      float jump = fabsf(g_cam_x - lcx) + fabsf(g_cam_y - lcy);
      if (t == 0) calm = 0;
      if (calm > 25 && jump > 3)
        printf("FAIL wander %s (seed %d) tick %d: the view jumped %.2f in a step in %s at %.2f,%.2f\n", start, seed, t, jump,
               room_name(g_room.id), x, y), fails++, reports++;
      calm = busy || h->cs.hazard_respawning || h->respawning || t == 0 ? 0 : calm + 1;
      lcx = g_cam_x, lcy = g_cam_y;
      if (!busy && (fabsf(x - g_cam_x) > (VIEW_W / 2) / FOCAL * -CAM_Z || fabsf(y - g_cam_y) > (VIEW_H / 2) / FOCAL * -CAM_Z)) {
        if (++unseen == 100) printf("FAIL wander %s (seed %d) tick %d: out of view 2 s in %s at %.2f,%.2f, view at %.2f,%.2f\n", start, seed, t, room_name(g_room.id), x, y, g_cam_x, g_cam_y), fails++, reports++;
      } else
        unseen = 0;
      lx = x, ly = y;
    }
    if (getenv("HKWANDERLOG")) printf("   %s: ended in %s at %.2f,%.2f\n", start, room_name(g_room.id), h->body.x, h->body.y);
  }
  printf("wander: %d failing\n", fails);
  return fails ? 1 : 0;
}

/* every arena (Battle Control; the False Knight's is enemy.c's own): started and its enemies beaten (each hit from
 * afar, the sleeping woken, the Knight shielded); then its fight must be over and the room's battle gates open, and
 * stay so as the Knight comes back to the room, and after a save and a load */
static int gates_shut(float *gx, float *gy) {
  int n, shut = 0;
  const Ent *es = room_ents(&n);
  for (int i = 0; i < n; i++) {
    const Ent *g = &es[i];
    if (g->type != ENT_OBJ || g->flags != OK_BGATE || !g->group) continue;
    /* (rays across it, either way, at a few places: one meets a collider of its own) */
    PhysHit hit;
    bool on = false;
    for (float d = -3; d <= 3 && !on; d += 0.5f)
      for (int s = -1; s <= 1 && !on; s += 2)
        on = (phys_ray(g->x0 - 4 * s, g->y0 + d, s, 0, 8, 0xFF, &hit) && hit.col >= g->a && hit.col < g->a + g->group) ||
             (phys_ray(g->x0 + d, g->y0 - 4 * s, 0, s, 8, 0xFF, &hit) && hit.col >= g->a && hit.col < g->a + g->group);
    if (on && !shut++) *gx = g->x0, *gy = g->y0;
  }
  return shut;
}
static int arenas_test(void) {
  extern void enemies_debug_hit(int damage);
  int fails = 0, total = 0;
  setenv("HKHITR", "200", 1);   /* (every enemy of the room hit) */
  if (getenv("HKSAVES")) {
    extern void host_save_dir(const char *d);
    host_save_dir(getenv("HKSAVES"));
  }
  for (int r = 0; r < NUM_ROOMS; r++) {
    if (!room_load(r)) continue;
    int n, ar = -1;
    const Ent *es = room_ents(&n);
    for (int i = 0; i < n; i++)
      if (es[i].type == ENT_OBJ && es[i].flags == OK_ARENA) ar = i;
    if (ar < 0) continue;
    total++;
    const Ent a = es[ar];
    /* (in its trigger, else at the room's first gate) */
    float x = (a.x0 + a.x1) / 2, y = (a.y0 + a.y1) / 2;
    if (a.x1 <= a.x0)
      for (int i = 0; i < n; i++)
        if (es[i].type == ENT_GATE && !(es[i].flags & G_DOOR)) {
          x = (es[i].x0 + es[i].x1) / 2, y = (es[i].y0 + es[i].y1) / 2;
          x = x < 3 ? 3 : x > g_room.h->w - 3 ? g_room.h->w - 3 : x;
          y = y < 3 ? 3 : y > g_room.h->h - 3 ? g_room.h->h - 3 : y;
          break;
        }
    char name[64], why[256] = "";
    snprintf(name, sizeof name, "%s", room_name(r));
    game_new();
    if (!game_enter(r, x, y, true)) continue;
    shielded = true;
    run_ticks(5, 0);
    arena_start();            /* (START: from its trigger or an enemy) */
    enemies_battle_start();   /* (sleepers woken) */
    int t = 0, shut_in_fight = 0;
    float gx = 0, gy = 0;
    for (; t < 6000 && !persist_get(a.persist); t++) {
      if (t % 10 == 0) enemies_debug_hit(30);
      run_ticks(1, 0);
      if (t == 10) shut_in_fight = gates_shut(&gx, &gy);
    }
    run_ticks(250, 0);   /* (End Wait, the gates opening) */
    int shut = gates_shut(&gx, &gy);
    if (!shut_in_fight) strcat(why, " no gate seen shut in the fight;");
    if (!persist_get(a.persist)) strcat(why, " fight not over;");
    if (shut) snprintf(why + strlen(why), sizeof why - strlen(why), " %d gates shut after it (at %.1f,%.1f);", shut, gx, gy);
    /* (the room again, as the Knight comes back) */
    game_enter(r, x, y, true);
    run_ticks(100, 0);
    if ((shut = gates_shut(&gx, &gy)))
      snprintf(why + strlen(why), sizeof why - strlen(why), " back: %d gates shut (at %.1f,%.1f);", shut, gx, gy);
    /* (saved, a new game, loaded, the room again) */
    save_select(SAVE_SLOTS - 1);
    if (!save_game()) strcat(why, " not saved;");
    game_new();
    if (!save_load(SAVE_SLOTS - 1)) strcat(why, " not loaded;");
    if (!persist_get(a.persist)) strcat(why, " loaded: fight not over;");
    game_enter(r, x, y, true);
    run_ticks(100, 0);
    if ((shut = gates_shut(&gx, &gy)))
      snprintf(why + strlen(why), sizeof why - strlen(why), " loaded: %d gates shut (at %.1f,%.1f);", shut, gx, gy);
    shielded = false;
    fails += why[0] != 0;
    printf("%s %s: won in %.1f s, %d gates shut in the fight%s\n", why[0] ? "FAIL" : "ok  ", name, t * 0.02f, shut_in_fight, why);
  }
  printf("arenas: %d, %d failing\n", total, fails);
  return fails || !total ? 1 : 0;
}

/* the charms (issue #138): OK changes nothing away from a bench. Resting at one: a charm equipped (its effect on:
 * Lifeblood Heart's lifeblood), none past the notches, one unequipped, one overcharmed on the fifth try and taken off
 * again; Home as one moves (inv_settle) saves it in its place, and a load brings them back */
static int charm_fails;
static void charm_check(bool ok, const char *what) {
  printf("%s charms: %s (equipped %d %d %d, notches %d of %d)\n", ok ? "ok  " : "FAIL", what, g_pd.equipped[0],
         g_pd.equipped[1], g_pd.equipped[2], g_pd.charm_slots_filled, g_pd.charm_slots);
  charm_fails += !ok;
}
/* (keys as --play has them, without drawing: L R U D, O OK, I the inventory) */
static void charm_keys(const char *keys) {
  for (const char *p = keys; *p; p++) {
    uint32_t k = *p == 'L' ? K_LEFT : *p == 'R' ? K_RIGHT : *p == 'U' ? K_UP : *p == 'D' ? K_DOWN : *p == 'O' ? K_OK | K_JUMP
               : *p == 'I' ? K_INV : 0;
    run_ticks(2, k);
    run_ticks(*p == 'O' ? 60 : *p == 'I' ? 40 : 12, 0);   /* (a charm's move, a failed one there and back; the panes) */
  }
}
static int charms_test(void) {
  if (getenv("HKSAVES")) {
    extern void host_save_dir(const char *d);
    host_save_dir(getenv("HKSAVES"));
  }
  /* (Gathering Swarm 1, Wayward Compass 2, Quick Focus 7, Lifeblood Heart 8: in the collection's second, first,
   * 26th and 28th places; the inventory opening on its Charms pane, the cursor on the second place) */
  game_new();
  pd_set_flag(PDF_HAS_CHARM, true);
  static const int got[] = {1, 2, 7, 8};
  for (int i = 0; i < 4; i++) pd_set_flag(PDF_GOT_CHARM_1 + got[i] - 1, true);
  g_pd.current_inv_pane = 1;
  save_select(0);
  game_enter(ROOM_TOWN, 120, 12, true);
  run_ticks(60, 0);
  charm_keys("IO");
  charm_check(inv_open() && !charm_on(1) && !g_pd.charm_slots_filled, "away from a bench, OK equips nothing");
  charm_keys("I");
  /* (Dirtmouth's bench: sat on) */
  game_enter(ROOM_TOWN, 132.4f, 12, true);
  run_ticks(60, 0);
  run_ticks(3, K_UP);
  run_ticks(150, 0);
  charm_check(pd_flag(PDF_AT_BENCH), "resting at the bench");
  charm_keys("IO");
  charm_check(charm_on(1) && g_pd.charm_slots_filled == 1 && g_pd.equipped[0] == 1, "Gathering Swarm equipped");
  charm_keys("DDRRRRRRO");
  charm_check(charm_on(8) && g_pd.charm_slots_filled == 3 && g_pd.health_blue == 2, "Lifeblood Heart equipped, its lifeblood on");
  charm_keys("UULLLLLLLO");
  charm_check(!charm_on(2) && g_pd.charm_slots_filled == 3, "no notch left: Wayward Compass not equipped");
  charm_keys("RO");
  charm_check(!charm_on(1) && g_pd.charm_slots_filled == 2 && g_pd.equipped[0] == 8 && !g_pd.equipped[1],
              "Gathering Swarm unequipped");
  charm_keys("DDRRRRO");
  charm_keys("OOO");
  charm_check(!charm_on(7) && g_pd.charm_slots_filled == 2 && !pd_flag(PDF_OVERCHARMED), "Quick Focus refused four times");
  charm_keys("O");
  charm_check(charm_on(7) && g_pd.charm_slots_filled == 5 && pd_flag(PDF_OVERCHARMED) && pd_flag(PDF_CAN_OVERCHARM),
              "the fifth try overcharms");
  charm_keys("O");
  charm_check(!charm_on(7) && g_pd.charm_slots_filled == 2 && !pd_flag(PDF_OVERCHARMED), "Quick Focus off, overcharming over");
  /* (Home as Gathering Swarm moves up: put in its place, then saved; a new game; the save loaded) */
  charm_keys("UULLLL");
  run_ticks(2, K_OK);
  run_ticks(3, 0);
  bool settled = g_pd.disable_pause && inv_settle();
  charm_check(settled && charm_on(1) && g_pd.charm_slots_filled == 3, "Home in the inventory: Gathering Swarm in place");
  charm_check(save_game(), "saved");
  game_new();
  charm_check(save_load(0) && game_respawn(), "loaded");
  run_ticks(10, 0);
  charm_check(charm_on(1) && charm_on(8) && !charm_on(7) && g_pd.charm_slots_filled == 3 && g_pd.equipped[0] == 8 &&
              g_pd.equipped[1] == 1 && g_pd.health_blue == 2 && pd_flag(PDF_AT_BENCH),
              "loaded at the bench with Lifeblood Heart and Gathering Swarm on");
  printf("charms: %d failing\n", charm_fails);
  return charm_fails ? 1 : 0;
}

/* the stag stations: their Stag Blanker (Blanker Control) black as the room comes in, faded out after 0.65 s over a
 * second, so that each is seen, come in by its gate, by the stag and loaded at its bench; and the rides, the stag called
 * with the bell first */
static int station_fails;
static float seen(void) {   /* (the share of the screen under the HUD that is not black) */
  extern uint16_t host_fb[];
  for (int i = 0; i < 3; i++) game_draw();   /* (the tiles in, as while playing) */
  int lit = 0, n = 0;
  for (int y = 70; y < SCREEN_H; y++)
    for (int x = 0; x < SCREEN_W; x++, n++) lit += host_fb[y * SCREEN_W + x] != 0;
  return (float)lit / n;
}
static void station_check(int room, const char *how) {
  float s = seen();
  bool ok = g_room.id == room && s > 0.2f && !g_hero.hidden;   /* (black: the HUD only, under 1%) */
  printf("%s stations: %s %s, %.0f%% of the screen seen\n", ok ? "ok  " : "FAIL", room_name(room), how, s * 100);
  station_fails += !ok;
}
static void station_new(int stag_at) {
  game_new();
  pd_set_flag(PDF_OPENED_CROSSROADS, true), pd_set_flag(PDF_OPENED_GREENPATH, true), pd_set_flag(PDF_MET_STAG, true);
  g_pd.stag_position1 = (uint8_t)(stag_at + 1);
}
static int stations_test(void) {
  if (getenv("HKSAVES")) {
    extern void host_save_dir(const char *d);
    host_save_dir(getenv("HKSAVES"));
  }
  static const struct { int room; const char *gate; float bench_x; } st[] = {
      {ROOM_ROOM_TOWN_STAG_STATION, "left1", 0}, {ROOM_CROSSROADS_47, "right1", 29.34f}, {ROOM_FUNGUS1_16_ALT, "right1", 30.97f}};
  for (unsigned i = 0; i < sizeof st / sizeof st[0]; i++) {
    station_new(1);
    game_enter_gate(st[i].room, st[i].gate);
    run_ticks(150, 0);
    station_check(st[i].room, "come in by its gate");
    if (!st[i].bench_x) continue;
    /* (rested at its bench, the game saved; a new game; the save loaded) */
    station_new(1);
    save_select(0);
    game_enter(st[i].room, st[i].bench_x, 7, true);
    run_ticks(60, 0);
    run_ticks(3, K_UP);
    run_ticks(150, 0);
    bool rested = pd_flag(PDF_AT_BENCH);
    game_new();
    bool loaded = rested && save_load(0) && game_respawn();
    run_ticks(200, 0);
    if (!loaded) printf("FAIL stations: %s: not rested at its bench and loaded\n", room_name(st[i].room)), station_fails++;
    else station_check(st[i].room, "loaded at its bench");
  }
  /* (the Crossroads: the stag at Dirtmouth called by the bell's ring (the nail), then up in its Travel Range, Dirtmouth
   * chosen; again with the stag there, Greenpath chosen) */
  for (int k = 0; k < 2; k++) {
    station_new(k ? 1 : 0);
    game_enter(ROOM_CROSSROADS_47, k ? 16.3f : 18.7f, 7, true);
    run_ticks(120, 0);
    if (!k) {
      run_ticks(3, K_ATTACK);
      run_ticks(600, 0);
      run_ticks(15, K_LEFT);
      run_ticks(60, 0);
    }
    run_ticks(3, K_UP);
    run_ticks(200, 0);
    run_ticks(2, k ? K_DOWN : K_UP);
    run_ticks(30, 0);
    run_ticks(2, K_OK);
    run_ticks(300, 0);
    run_ticks(2, K_OK);   /* (the ride skipped after 1.5 s) */
    run_ticks(300, 0);
    station_check(k ? ROOM_FUNGUS1_16_ALT : ROOM_ROOM_TOWN_STAG_STATION, k ? "by the stag" : "by the stag, called by its bell");
  }
  printf("stations: %d failing\n", station_fails);
  return station_fails ? 1 : 0;
}

/* the Snail Shaman (Mason363/NumPlay#245), played in his temple from a new game: in by its left gate, his talk, his
 * spell summoned and taken (the Knight up again behind the bone gate), his talk through it; the Elder Baldur killed by
 * Vengeful Spirit, his talk opening the gate; saved and loaded, the gate still open, the Baldur still gone. Then as #245
 * had it: the Baldur killed, the game saved and loaded before the Shaman is told, his talk then; the lifeblood cocoon
 * broken there before, broken still after the load and a rest at his bench, whole again after a rest at another (the
 * Knight put by the Baldur and the cocoon: the climbs are not what is checked) */
static int shaman_fails;
static void shaman_check(bool ok, const char *what) {
  printf("%s shaman: %s (shaman %d, fireball %d, in %s at %.2f,%.2f)\n", ok ? "ok  " : "FAIL", what, g_pd.shaman,
         g_pd.fireball_level, room_name(g_room.id), g_hero.body.x, g_hero.body.y);
  shaman_fails += !ok;
}
static bool hero_free(void) {
  const Hero *h = &g_hero;
  return h->accepting_input && !h->control_relinquished && h->state != HS_NO_INPUT && !dialogue_box_shown();
}
static void walk_to(float x) {
  for (int t = 0; t < 600 && fabsf(g_hero.body.x - x) > 0.2f; t++) run_ticks(1, g_hero.body.x < x ? K_RIGHT : K_LEFT);
  run_ticks(30, 0);
}
static void put_at(float x, float y) {
  g_hero.body.x = x, g_hero.body.y = y, g_hero.body.vx = g_hero.body.vy = 0;
  run_ticks(60, 0);
}
/* (Up, then OK on each page while his box is up: true once it came up and went, the Knight free again) */
static bool shaman_talk(void) {
  run_ticks(3, K_UP);
  bool shown = false;
  for (int t = 0; t < 4000; t += 40) {
    run_ticks(40, 0);
    shown |= dialogue_box_shown();
    if (shown && hero_free()) return true;
    if (dialogue_box_shown()) run_ticks(2, K_OK | K_JUMP);
  }
  return false;
}
static const Ent *temple_ent(int kind, int a) {
  int n;
  const Ent *es = room_ents(&n);
  for (int i = 0; i < n; i++)
    if (es[i].type == ENT_OBJ && es[i].flags == kind && (a < 0 || es[i].a == a)) return &es[i];
  return NULL;
}
static bool gate_shut(void) { return phys_ray(44, 8.6f, -1, 0, 4, CF_SOLID, NULL); }   /* (the bone gate, at 42.4) */
/* (by the Elder Baldur, on its right, Vengeful Spirit cast at it (the soul given) until the Shaman hears of it) */
static void kill_baldur(void) {
  put_at(64, 21.5f);
  run_ticks(2, K_LEFT);
  for (int k = 0; k < 12 && g_pd.shaman != 4 && g_pd.shaman != 5; k++) {
    g_pd.mp = 33;
    run_ticks(3, K_FOCUS | K_SPELL);
    run_ticks(60, 0);
  }
  run_ticks(100, 0);
}
static bool baldur_gone(void) {
  /* (dead in the save; its block off: the Knight walks on past where it was) */
  const Ent *b = temple_ent(OK_ENEMY, EK_ELDERBALDUR);
  put_at(58, 21.5f);
  walk_to(47.5f);
  return b && persist_get(b->persist) && g_hero.body.x < 48.5f;
}
static bool rest_at(int room, float x, float y) {
  game_enter(room, x, y, true);
  run_ticks(60, 0);
  run_ticks(3, K_UP);
  run_ticks(150, 0);
  bool rested = pd_flag(PDF_AT_BENCH);
  run_ticks(3, K_RIGHT);
  run_ticks(60, 0);
  return rested;
}
static int shaman_test(void) {
  if (getenv("HKSAVES")) {
    extern void host_save_dir(const char *d);
    host_save_dir(getenv("HKSAVES"));
  }
  shielded = true;
  game_new();
  save_select(SAVE_SLOTS - 1);
  game_enter_gate(ROOM_CROSSROADS_SHAMANTEMPLE, "left1");
  run_ticks(100, 0);
  walk_to(21.8f);
  shaman_check(shaman_talk() && g_pd.shaman == 1, "met, his spell summoned");
  /* (the spell: under it, a jump; its message, dismissed; the Knight gets up) */
  walk_to(19.5f);
  run_ticks(20, K_JUMP);
  for (int t = 0; t < 2000 && !msg_shown(); t++) run_ticks(1, 0);
  run_ticks(400, 0);
  run_ticks(2, K_JUMP);
  for (int t = 0; t < 40 && !(g_pd.shaman == 2 && hero_free()); t++) {
    run_ticks(50, 0);
    run_ticks(3, K_RIGHT);
  }
  shaman_check(g_pd.fireball_level == 1 && g_pd.shaman == 2 && hero_free() && g_hero.body.x > 43,
               "Vengeful Spirit taken, the Knight up behind the gate");
  walk_to(43.2f);
  shaman_check(shaman_talk() && g_pd.shaman == 3 && gate_shut(), "his talk through the gate");
  save_game();   /* (kept for #245's way, below) */
  kill_baldur();
  shaman_check(g_pd.shaman == 4, "the Elder Baldur killed, the Shaman told");
  put_at(46, 8.5f);
  walk_to(43.2f);
  shaman_check(shaman_talk() && g_pd.shaman == 6 && !gate_shut(), "his talk, the gate opened");
  walk_to(38);
  shaman_check(g_hero.body.x < 40, "through the gate");
  save_select(SAVE_SLOTS - 2);
  save_game();
  game_new();
  shaman_check(save_load(SAVE_SLOTS - 2) && game_respawn(), "saved, a new game, loaded");
  run_ticks(200, 0);
  shaman_check(g_room.id == ROOM_CROSSROADS_SHAMANTEMPLE && g_pd.shaman == 6 && !gate_shut(), "loaded: the gate open");
  walk_to(21.8f);   /* (back where he was met, once the gate is open) */
  shaman_check(shaman_talk() && g_pd.shaman == 6 && !gate_shut(), "loaded: his talk where he was met");
  shaman_check(baldur_gone(), "loaded: the Elder Baldur gone");
  /* (#245's way: the save before the Baldur loaded; the cocoon broken; the Baldur killed; saved and loaded) */
  game_new();
  save_select(SAVE_SLOTS - 1);
  shaman_check(save_load(SAVE_SLOTS - 1) && game_respawn() && g_pd.shaman == 3, "the save before the Baldur loaded");
  run_ticks(200, 0);
  const Ent *cocoon = temple_ent(OK_COCOON, -1);
  put_at(38.5f, 46.5f);
  run_ticks(3, K_UP | K_ATTACK);
  run_ticks(100, 0);
  shaman_check(cocoon && persist_get(cocoon->persist), "the lifeblood cocoon broken");
  kill_baldur();
  shaman_check(g_pd.shaman == 4, "the Elder Baldur killed again");
  save_game();
  game_new();
  shaman_check(save_load(SAVE_SLOTS - 1) && game_respawn(), "saved, a new game, loaded");
  run_ticks(200, 0);
  shaman_check(g_pd.shaman == 4 && gate_shut() && persist_get(cocoon->persist), "loaded: the gate shut, the cocoon broken");
  walk_to(43.2f);
  shaman_check(shaman_talk() && g_pd.shaman == 6 && !gate_shut(), "loaded: his talk, the gate opened");
  shaman_check(baldur_gone(), "loaded: the Elder Baldur gone");
  /* (his bench, beyond the gate: the cocoon in its room stays broken; another's: whole again) */
  shaman_check(rest_at(ROOM_CROSSROADS_SHAMANTEMPLE, 15.09f, 8) && persist_get(cocoon->persist),
               "rested at his bench: the cocoon broken still");
  shaman_check(rest_at(ROOM_CROSSROADS_47, 29.34f, 7) && !persist_get(cocoon->persist),
               "rested at another bench: the cocoon whole again");
  /* (broken again, saved away from a bench (as Home does) and loaded at that bench: whole again, Init Resting's) */
  game_enter(ROOM_CROSSROADS_SHAMANTEMPLE, 38.5f, 46.5f, true);
  run_ticks(60, 0);
  run_ticks(3, K_UP | K_ATTACK);
  run_ticks(100, 0);
  bool broken = persist_get(cocoon->persist);
  save_game();
  game_new();
  shaman_check(broken && save_load(SAVE_SLOTS - 1) && game_respawn(), "broken again, saved, a new game, loaded");
  run_ticks(200, 0);
  shaman_check(g_room.id == ROOM_CROSSROADS_47 && !persist_get(cocoon->persist), "loaded at that bench: the cocoon whole");
  shielded = false;
  printf("shaman: %d failing\n", shaman_fails);
  return shaman_fails ? 1 : 0;
}

/* the enemies the game keeps dead until the Knight rests (semi persistent): killed, still dead as the Knight comes back;
 * out again after a rest at a bench, a death, or a load at a bench; and after a rest at a bench in their own room (the
 * game saved them as they died, their objects gone). The persistent ones (the Elder Baldur) and a broken lifeblood
 * cocoon in the room rested in stay as they are */
static int respawn_fails;
static void respawn_check(bool ok, const char *what) {
  printf("%s respawn: %s (in %s)\n", ok ? "ok  " : "FAIL", what, room_name(g_room.id));
  respawn_fails += !ok;
}
static bool is_semi(int persist) {
  static const uint16_t semi[] = SEMI_PERSIST;
  for (unsigned k = 0; k < sizeof semi / sizeof semi[0]; k++)
    if (semi[k] == persist) return true;
  return false;
}
/* (the room's semi persistent enemies: how many, how many dead in the save, how many out alive) */
static void semi_enemies(int *n, int *dead, int *alive) {
  extern bool enemies_alive_ent(int ent);
  int ne;
  const Ent *es = room_ents(&ne);
  *n = *dead = *alive = 0;
  for (int i = 0; i < ne; i++)
    if (es[i].type == ENT_OBJ && es[i].flags == OK_ENEMY && es[i].persist != NO_PERSIST && is_semi(es[i].persist)) {
      (*n)++;
      *dead += persist_get(es[i].persist);
      *alive += enemies_alive_ent(i);
    }
}
/* (each enemy in the room hit from afar until the semi persistent ones are dead) */
static bool kill_semi(void) {
  extern void enemies_debug_hit(int damage);
  int n, dead, alive;
  semi_enemies(&n, &dead, &alive);
  for (int t = 0; t < 3000 && dead < n; t++) {
    if (t % 10 == 0) enemies_debug_hit(30);
    run_ticks(1, 0);
    semi_enemies(&n, &dead, &alive);
  }
  run_ticks(100, 0);
  return n && dead == n;
}
static bool semi_out(int room, float x, float y, bool out) {
  game_enter(room, x, y, true);
  run_ticks(30, 0);
  int n, dead, alive;
  semi_enemies(&n, &dead, &alive);
  return out ? n && !dead && alive == n : n && dead == n && !alive;
}
static int respawn_test(void) {
  if (getenv("HKSAVES")) {
    extern void host_save_dir(const char *d);
    host_save_dir(getenv("HKSAVES"));
  }
  setenv("HKHITR", "200", 1);
  shielded = true;
  game_new();
  save_select(SAVE_SLOTS - 1);
  const int room = ROOM_CROSSROADS_01;
  const float rx = 40, ry = 12;
  game_enter(room, rx, ry, true);
  run_ticks(30, 0);
  respawn_check(kill_semi(), "its semi persistent enemies killed");
  respawn_check(semi_out(room, rx, ry, false), "come back to: still dead");
  respawn_check(rest_at(ROOM_CROSSROADS_47, 29.34f, 7) && semi_out(room, rx, ry, true), "after a rest at a bench: out again");
  kill_semi();
  shielded = false, g_hero.cs.invulnerable = false;
  hero_take_damage(SIDE_LEFT, 99, HAZ_NORMAL);
  for (int t = 0; t < 1500 && g_room.id == room; t++) run_ticks(1, 0);
  run_ticks(200, 0);
  shielded = true;
  respawn_check(g_room.id == ROOM_CROSSROADS_47 && semi_out(room, rx, ry, true), "killed, the Knight dead: out again");
  kill_semi();
  save_game();
  game_new();
  respawn_check(save_load(SAVE_SLOTS - 1) && game_respawn(), "killed, saved away from a bench, a new game, loaded");
  run_ticks(200, 0);
  respawn_check(g_room.id == ROOM_CROSSROADS_47 && semi_out(room, rx, ry, true), "loaded at a bench: out again");
  /* (the temple: its Husk killed, the Elder Baldur (persistent) killed, the cocoon broken, a rest at its own bench) */
  g_pd.fireball_level = 1, g_pd.has_spell = true, g_pd.shaman = 6;
  game_enter(ROOM_CROSSROADS_SHAMANTEMPLE, 46, 8.5f, true);
  run_ticks(30, 0);
  kill_baldur();
  const Ent *baldur = temple_ent(OK_ENEMY, EK_ELDERBALDUR), *cocoon = temple_ent(OK_COCOON, -1);
  put_at(38.5f, 46.5f);
  run_ticks(3, K_UP | K_ATTACK);
  run_ticks(100, 0);
  game_enter(ROOM_CROSSROADS_SHAMANTEMPLE, 46, 8.5f, true);
  run_ticks(30, 0);
  bool killed = kill_semi() && baldur && persist_get(baldur->persist) && cocoon && persist_get(cocoon->persist);
  respawn_check(killed, "the temple's Husk and Elder Baldur killed, its cocoon broken");
  respawn_check(rest_at(ROOM_CROSSROADS_SHAMANTEMPLE, 15.09f, 8), "rested at its bench");
  respawn_check(semi_out(ROOM_CROSSROADS_SHAMANTEMPLE, 46, 8.5f, true) && persist_get(baldur->persist) &&
                    persist_get(cocoon->persist), "come back to: the Husk out again, the Elder Baldur gone, the cocoon broken");
  shielded = false;
  unsetenv("HKHITR");
  printf("respawn: %d failing\n", respawn_fails);
  return respawn_fails ? 1 : 0;
}

/* the ground's seams, as the calculator rounds: from many places in every room the Knight lands on the ground, then is
 * put a few float steps lower (as near as rounding leaves him), and must walk both ways where no wall stops him; and
 * jumping at a wall, put a few steps nearer it, he must fall back down, not hang on it */
static bool wall_ahead(int dir) {
  const Body *b = &g_hero.body;
  float x = b->x + b->ox + dir * b->hx, y0 = b->y + b->oy - b->hy, y1 = y0 + 2 * b->hy;
  for (float y = y0 + 0.08f; y < y1; y += 0.1f)
    if (phys_ray(x - dir * 0.3f, y, dir, 0, 0.4f, CF_SOLID, NULL)) return true;
  /* (or the room's end; or a gate he is sent back from: to a room the game leaves out) */
  if (x < 0.3f || x > g_room.h->w - 0.3f) return true;
  int n;
  const Ent *es = room_ents(&n);
  for (int i = 0; i < n; i++)
    if (es[i].type == ENT_GATE && !(es[i].flags & G_DOOR))
      for (int j = i + 1; j < n && es[j].type == ENT_BOX; j++)
        if (es[j].flags == 0 && x + 0.2f > es[j].x0 && x - 0.2f < es[j].x1 && y1 > es[j].y0 && y0 < es[j].y1) return true;
  return false;
}
static float ulps(float v, int n) {   /* (n float steps down, or up) */
  for (int i = 0; i < abs(n); i++) v = nextafterf(v, n < 0 ? -1e9f : 1e9f);
  return v;
}
static int seams_test(void) {
  int fails = 0, floors = 0, walls = 0;
  const Hero *h = &g_hero;
  shielded = true;
  for (int r = 0; r < NUM_ROOMS; r++) {
    if (!room_load(r)) continue;
    char name[64];
    snprintf(name, sizeof name, "%s", room_name(r));
    float w = g_room.h->w, hh = g_room.h->h;
    for (float x = 2.13f; x < w - 2; x += 1.37f) {
      float lastgy = -1;
      for (float y = hh - 2; y > 2; y -= 1.3f) {
        if (!room_load(r)) continue;
        PhysHit hit;
        if (!phys_ray(x, y, 0, -1, 30, CF_TERRAIN, &hit) || hit.ny < 0.99f || hit.y == lastgy) continue;
        float gy = lastgy = hit.y;
        bool inside = false;
        for (float yy = gy + 0.3f; yy < gy + 3; yy += 0.5f)
          for (float xx = x - 0.6f; xx <= x + 0.61f; xx += 0.3f) inside |= phys_tile_solid((int)floorf(xx), (int)floorf(yy));
        if (inside || phys_ray(x, gy + 0.05f, 0, 1, 3, CF_SOLID, NULL) || phys_ray(x - 0.4f, gy + 0.5f, 1, 0, 0.8f, CF_SOLID, NULL))
          continue;
        game_new();
        game_enter(r, x, gy + 2.4f, true);
        int t;
        for (t = 0; t < 100 && !(h->cs.on_ground && h->state != HS_HARD_LANDING); t++) run_ticks(1, 0);
        if (!h->cs.on_ground || h->state == HS_NO_INPUT || h->control_relinquished || !h->accepting_input || g_room.id != r)
          continue;
        floors++;
        g_hero.body.y = ulps(g_hero.body.y, -3);
        for (int dir = 1; dir >= -1; dir -= 2) {
          float lx = h->body.x;
          int still = 0;
          for (t = 0; t < 30; t++) {
            run_ticks(1, dir > 0 ? K_RIGHT : K_LEFT);
            if (!h->cs.on_ground || h->state == HS_NO_INPUT || g_room.id != r || game_changing_room() || g_game.freeze_phase) break;
            still = fabsf(h->body.x - lx) < 0.01f && h->state == HS_RUNNING ? still + 1 : 0;
            if (still == 3) {
              if (!wall_ahead(dir) && !h->cs.transitioning)
                printf("FAIL seams %s: landed at %.2f,%.3f, stuck going %s at %.4f,%.5f\n", name, x, gy, dir > 0 ? "right" : "left",
                       h->body.x, h->body.y), fails++;
              break;
            }
            lx = h->body.x;
          }
          run_ticks(5, 0);
        }
        /* (a wall a little way off: the Knight jumps at it) */
        for (int dir = -1; dir <= 1; dir += 2) {
          PhysHit wh;
          if (!phys_ray(x, gy + 1.0f, dir, 0, 3, CF_TERRAIN, &wh) || fabsf(wh.nx) < 0.99f || !room_load(r)) continue;
          game_new();
          game_enter(r, x, gy + 1.42f, true);
          run_ticks(30, 0);
          if (!h->cs.on_ground || h->state == HS_NO_INPUT || h->control_relinquished || g_room.id != r) continue;
          walls++;
          int held = 0, worst = 0;
          bool nudged = false;
          for (t = 0; t < 120; t++) {
            run_ticks(1, (dir > 0 ? K_RIGHT : K_LEFT) | (t < 15 || (t > 40 && t < 55) ? K_JUMP : 0));
            if (g_room.id != r || game_changing_room() || h->cs.transitioning || h->state == HS_NO_INPUT || g_game.freeze_phase) break;
            if (!nudged && h->cs.touching_wall) g_hero.body.x = ulps(g_hero.body.x, 3 * dir), nudged = true;
            held = !h->cs.on_ground && fabsf(h->body.vy) < 1e-4f && !h->cs.jumping && h->state != HS_HARD_LANDING ? held + 1 : 0;
            if (held > worst) worst = held;
          }
          if (worst >= 40 && h->body.y + h->body.oy - h->body.hy > 0.5f)   /* (not the room's bottom: a pit out of it) */
            printf("FAIL seams %s: from %.2f,%.3f jumping %s, held up in the air %d ticks at %.4f,%.5f\n", name, x, gy,
                   dir > 0 ? "right" : "left", worst, h->body.x, h->body.y), fails++;
        }
      }
    }
  }
  shielded = false;
  printf("seams: %d landings, %d jumps at walls, %d failing\n", floors, walls, fails);
  return fails ? 1 : 0;
}

/* the text on the screens that show much of it (the shops, the inventory and its charms and map, the menus, the
 * messages, a conversation), each frame drawn: in the screen, no line over another, none left out (gfx.c's audit) */
extern bool g_text_audit;
extern char g_text_audit_log[2048];
static int text_fails;
static char text_seen[64][160];
static int text_nseen;
static void text_frame(const char *where, bool menu) {
  if (menu) {
    g_gfx_no_room = !menu_in_game();
    if (menu_in_game()) game_draw_layers();
    menu_draw();
    gfx_frame();
  } else
    game_draw();
  if (getenv("HKTEXTSHOTS")) {   /* (each frame checked, as an image: HKTEXTSHOTS=its folder) */
    static int nshot;
    char path[256];
    snprintf(path, sizeof path, "%s/%03d_%s.ppm", getenv("HKTEXTSHOTS"), nshot++, where);
    for (char *c = path + strlen(getenv("HKTEXTSHOTS")) + 1; *c; c++)
      if (*c == ' ') *c = '_';
    host_shot(path);
  }
  extern uint32_t g_text_overrun;
  if (g_text_overrun) {
    size_t k = strlen(g_text_audit_log);
    snprintf(g_text_audit_log + k, sizeof g_text_audit_log - k, "a description too long for its box; ");
    g_text_overrun = 0;
  }
  if (!g_text_audit_log[0]) return;
  char line[160];
  snprintf(line, sizeof line, "%s: %s", where, g_text_audit_log);
  for (int i = 0; i < text_nseen; i++)
    if (!strcmp(text_seen[i], line)) return;
  if (text_nseen < 64) strcpy(text_seen[text_nseen++], line);
  printf("FAIL texts %s\n", line);
  text_fails++;
}
/* (keys as --play has them; each step drawn) */
static void text_keys(const char *where, const char *keys, bool menu) {
  for (const char *p = keys; *p;) {
    uint32_t k = 0;
    for (; *p && *p != '*'; p++)
      k |= *p == 'L' ? K_LEFT : *p == 'R' ? K_RIGHT : *p == 'U' ? K_UP : *p == 'D' ? K_DOWN : *p == 'O' ? K_OK | K_JUMP
         : *p == 'B' ? K_BACK | K_ATTACK : *p == 'P' ? K_PAUSE : *p == 'I' ? K_INV : *p == 'M' ? K_MAP
         : *p == 'S' ? K_DASH : 0;
    int n = *p == '*' ? atoi(++p) : 1;
    while (*p && *p != ',') p++;
    if (*p == ',') p++;
    for (int i = 0; i < n; i++) {
      if (!menu || !menu_tick(k)) game_tick(stag_tick(shop_tick(inv_tick(k))));
      if (i == n - 1) text_frame(where, menu);   /* (as it settles: panes slide, windows open) */
    }
  }
}
static void text_room(const char *room, float x, float y, const char *flags) {
  game_new();
  g_pd.geo = 9999;
  for (const char *f = flags; *f;) {
    pd_set_flag(atoi(f), true);
    while (*f && *f != ',') f++;
    if (*f) f++;
  }
  for (int r = 0; r < NUM_ROOMS; r++)
    if (!strcmp(room_name(r), room)) game_enter(r, x, y, true);
}
static int texts_test(void) {
  g_text_audit = true;
  char down[512] = "_*80,U*2,_*100";
  for (int i = 0; i < 14; i++) strcat(down, ",D*2,_*25");
  /* the shops: every row, then the confirmation (a new game's stock; then with the maps and pins had, the rest) */
  static const struct { const char *room; float x; const char *flags; } shops[] = {
      {"Room_mapper", 17.35f, "123,149"}, {"Room_mapper", 17.35f, "123,149,1,2,130,131,132,133,134,135,136"},
      {"Room_shop", 15.6f, "23,122"}, {"Room_Charm_Shop", 17.14f, "124"}};
  for (unsigned k = 0; k < sizeof shops / sizeof shops[0]; k++) {
    text_room(shops[k].room, shops[k].x, 6.5f, shops[k].flags);
    text_keys(shops[k].room, down, false);
    text_keys(shops[k].room, "O*2,_*60,D*2,_*20,B*2,_*40", false);
  }
  /* the inventory: its panes, every place on them (with all it can hold); the charms, every one (at a bench too); the
   * map */
  for (int bench = 0; bench < 2; bench++) {
    text_room("Town", 60, 12, bench ? "0,1,2,3,14,119,123,37,38,39,40,42,43,44,45,50,53,54,55,56,57"
                                    : "1,2,3,14,119,123,37,38,39,40,42,43,44,45,50,53,54,55,56,57");
    g_pd.charm_slots = 3, g_pd.has_spell = true, g_pd.fireball_level = 1, g_pd.simple_keys = 1, g_pd.rancid_eggs = 1;
    g_pd.heart_pieces = 2, g_pd.vessel_fragments = 1;
    pd_set_flag(PDF_HEART_PIECE_COLLECTED, true), pd_set_flag(PDF_VESSEL_FRAGMENT_COLLECTED, true);
    for (int i = 0; i < 4; i++) g_pd.trinkets[i] = 1;
    char keys[1024] = "_*40,I*2,_*60";
    for (int i = 0; i < 6; i++) {   /* (each row, left to right, then down) */
      for (int j = 0; j < 7; j++) strcat(keys, ",R*2,_*15");
      strcat(keys, ",D*2,_*15");
      for (int j = 0; j < 7; j++) strcat(keys, ",L*2,_*15");
      strcat(keys, ",D*2,_*15");
    }
    text_keys(bench ? "inventory at a bench" : "inventory", keys, false);
  }
  /* the quick map of each area */
  static const char *const quick[] = {"Town", "Crossroads_04", "Fungus1_01"};
  for (unsigned i = 0; i < sizeof quick / sizeof quick[0]; i++) {
    text_room(quick[i], 20, 12, "1,2,6,12,16,130,131");
    memset(g_pd.rooms_mapped, 0xFF, sizeof g_pd.rooms_mapped), memset(g_pd.rooms_visited, 0xFF, sizeof g_pd.rooms_visited);
    text_keys("quick map", "_*60,M*90", false);
  }
  /* the inventory's map, its key and pins shown (Shift); the stag's menu, every station open */
  text_room("Town", 60, 12, "1,2,130,131,132,133,134,135,136,137,138,139,140,141,142");
  memset(g_pd.rooms_mapped, 0xFF, sizeof g_pd.rooms_mapped), memset(g_pd.rooms_visited, 0xFF, sizeof g_pd.rooms_visited);
  text_keys("map", "_*40,I*2,_*60,L*2,_*20,L*2,_*20,L*2,_*20,L*2,_*40,S*2,_*30,S*2,_*30,S*2,_*30,O*2,_*80,S*2,_*30,B*2,_*60",
            false);
  {
    static const int16_t opened[] = STAG_OPENED;
    text_room("Room_Town_Stag_Station", 40, 8, "");
    for (unsigned i = 0; i < sizeof opened / sizeof opened[0]; i++)
      if (opened[i] >= 0) pd_set_flag(opened[i], true);
    g_pd.stag_position1 = 1;
    stag_menu_open(0, 0);
    text_keys("stag", "_*60,D*2,_*20,D*2,_*20", false);
  }
  /* the menus: the title, the profiles, how to play's two pages, the pause menu */
  game_new();
  menu_start();
  text_keys("menus", "_*40,O*2,_*40,B*2,_*30,D*2,_*10,O*2,_*40,O*2,_*40,O*2,_*30", true);
  /* the messages as the fireball and the dash are taken; a relic's notice; the charm tutorial */
  text_room("Crossroads_01", 40, 12, "");
  msg_show(MSG_FIREBALL);
  text_keys("message", "_*200", false);
  text_room("Crossroads_01", 40, 12, "");
  msg_show(MSG_DASH);
  text_keys("message", "_*200", false);
  /* a relic's notice; a charm's, and the charm tutorial */
  {
    typedef struct { int16_t name, desc; } ND;
    static const ND tr[4] = TXT_INV_TRINKETS;
    text_room("Crossroads_01", 40, 12, "");
    notice_icon(0);
    notice_show(tr[0].name);
    text_keys("notice", "_*30,_*60,_*100", false);
    text_room("Crossroads_01", 40, 12, "");
    charm_notice(1);
    text_keys("charm notice", "_*30,_*60,_*100", false);
    text_room("Crossroads_01", 40, 12, "");
    charm_tute();
    text_keys("charm tutorial", "_*30,_*60,_*100", false);
    charm_tute_close();
  }
  /* King's Pass's first tablet: its text's first page, then its last with the focus tutorial up under it; the first
   * map's lesson */
  text_room("Tutorial_01", 111.46f, 31, "");
  text_keys("focus tablet", "_*60,U*3,_*300", false);
  text_keys("focus tablet", "O*2,_*300", false);
  text_room("Crossroads_01", 40, 12, "");
  first_map_prompt(true);
  text_keys("first map", "_*200", false);
  first_map_prompt(false);
  /* a conversation (Elderbug's first) */
  text_room("Town", 76, 12, "");
  text_keys("Elderbug", "_*40,U*2,_*120,O*2,_*60,O*2,_*60,O*2,_*60,O*2,_*60,O*2,_*60", false);
  g_text_audit = false;
  printf("texts: %d failing\n", text_fails);
  return text_fails ? 1 : 0;
}

/* each shop's first charm: bought (each row from the top, as bought until one is a charm), its lesson up as the shop
 * closes, then sent away by a key once it stops, the Knight's control given back (a shop's lesson: Input) */
static int shop_charm_test(void) {
  static const struct { const char *room; float x; const char *flags; } shops[] = {
      {"Room_mapper", 17.35f, "123,149"}, {"Room_shop", 15.6f, "23,122"}, {"Room_Charm_Shop", 17.14f, "124"}};
  int fails = 0;
  shielded = true;
  for (unsigned k = 0; k < sizeof shops / sizeof shops[0]; k++) {
    text_room(shops[k].room, shops[k].x, 6.5f, shops[k].flags);
    run_ticks(80, 0);
    bool lesson = false;
    for (int row = 0; row < 12 && !pd_flag(PDF_HAS_CHARM); row++) {
      for (int t = 0; t < 30 && !hero_free(); t++) run_ticks(50, 0);   /* (the shop down, or what it gave) */
      run_ticks(3, K_UP);
      run_ticks(150, 0);
      for (int i = 0; i < 8 && !shop_open(); i++) run_ticks(2, K_OK | K_JUMP), run_ticks(60, 0);   /* (its talk first) */
      for (int i = 0; i < row; i++) run_ticks(2, K_DOWN), run_ticks(20, 0);
      run_ticks(2, K_OK), run_ticks(40, 0);
      run_ticks(2, K_OK), run_ticks(150, 0);
      if (pd_flag(PDF_HAS_CHARM)) lesson = !shop_open() && !hero_free();
      if (shop_open()) run_ticks(2, K_BACK), run_ticks(60, 0), run_ticks(2, K_BACK), run_ticks(60, 0);
    }
    run_ticks(300, 0);   /* (its stop, 5 s after it came up) */
    bool waits = !hero_free();
    run_ticks(2, K_JUMP);
    run_ticks(100, 0);
    bool ok = pd_flag(PDF_HAS_CHARM) && lesson && waits && hero_free();
    printf("%s shop charm: %s: %s\n", ok ? "ok  " : "FAIL", shops[k].room,
           !pd_flag(PDF_HAS_CHARM) ? "no charm bought" : !lesson ? "no lesson" : !waits ? "the lesson gone by itself"
           : !hero_free() ? "the Knight without control after it" : "bought, the lesson sent away, the Knight free");
    fails += !ok;
  }
  shielded = false;
  printf("shop charm: %d failing\n", fails);
  return fails ? 1 : 0;
}

/* the way to the False Knight (Mason363/NumPlay#172), played as one game, room by room, each room's keys from when the
 * Knight comes into it: from the new game's fall into King's Pass, through Dirtmouth and down its well, past the Temple of
 * the Black Egg, down through the tollbooth into Crossroads_03 (its lever's gates open: the game's Knight opens them from
 * below, by rooms this port leaves out), along the Husk Guard's corridor and up into the arena, where the False Knight
 * must come down; and from the Aspid Hunters' arena by the hot spring back up to the well and on the same way. Nothing
 * hurts the Knight: the way is what is checked, not the fights. (Each room's keys, as --play has them, were found by
 * trying moves on the computer.) */
typedef struct { int room; const char *keys; } Leg;
#define WAY_KINGS_PASS \
  "_*400,R*300,RA*4,R*12,RA*4,R*12,RA*4,R*12,RA*4,R*12,RA*4,R*12,RA*4,R*12,RA*4,R*12,RA*4,R*12," \
  "RA*4,R*12,RA*4,R*12,RA*4,R*12,RA*4,R*12,RA*4,R*12,RA*4,R*12,RA*4,R*12,RA*4,R*12,RA*4,R*12,RA*4," \
  "R*12,RA*4,R*12,RA*4,R*12,RA*4,R*12,RA*4,R*12,RA*4,R*12,RA*4,R*12,RA*4,R*12,L*8,_*1,L*8,_*1,R*25," \
  "_*1,RJ*30,_*1,R*25,_*1,RJ*30,_*1,RA*6,_*1,R*8,_*1,R*36,RJ*30,R*4,_*20,R*12,RJ*30,R*2,_*20,J*6," \
  "RJ*24,_*20,L*8,_*1,LJ*30,_*1,L*8,_*1,LJ*30,_*1,L*8,_*1,R*8,_*1,L*8,_*1,LJ*30,_*1,L*8,_*1,L*8,_*1," \
  "J*22,LJ*8,L*10,_*1,LJ*30,_*1,LJ*30,_*1,LJ*30,_*1,LJ*30,_*1,LJ*30,_*1,LJ*30,_*1,L*25,_*1,LJ*30," \
  "_*1,LJ*30,_*1,LJ*30,_*1,J*30,_*1,J*22,RJ*8,R*10,_*1,LJ*30,_*1,L*8,_*1,R*8,_*1,J*16,RJ*14,_*1," \
  "J*22,RJ*8,R*10,_*1,RJ*30,_*1,R*8,_*1,J*16,LJ*14,_*1,LJ*30,_*1,L*8,_*1,LJ*30,_*1,L*8,_*1,LJ*30," \
  "_*1,L*8,_*1,R*8,_*1,J*22,LJ*8,L*10,_*1,RJ*30,_*1,R*25,_*1,R*8,_*1,R*25,_*1,R*25,_*1,L*8,_*1,L*8," \
  "_*1,L*8,_*1,RJ*30,_*1,R*25,_*1,RJ*30,_*1,R*25,_*1,RA*6,_*1,R*8,_*1,R*25,_*1,RJ*30,_*1,RJ*30,_*1," \
  "L*8,_*1,L*8,_*1,R*25,_*1,R*8,_*1,RJ*30,_*1,R*8,_*1,RJ*30,_*1,R*25,_*1,L*8,_*1,L*8,_*1,J*22,RJ*8," \
  "R*10,_*1,R*25,_*1,RJ*12,_*1,RJ*30,_*1,R*25,_*1,RJ*30,_*1,J*8,RJ*22,_*1,J*22,RJ*8,R*10,_*1,RJ*30," \
  "_*1,LJ*12,_*1,J*22,LJ*8,L*10,_*1,LJ*30,_*1,L*25,_*1,LJ*30,_*1,_*40,R*12,_*5,J*10,UA*4,_*30,J*10," \
  "UA*4,_*30,J*10,UA*4,_*30,R*12,_*10,J*16,RJ*14,R*20,_*20,R*300,R*8,RA*4,_*8,R*8,RA*4,_*8,R*8,RA*4," \
  "_*8,R*8,RA*4,_*8,R*8,RA*4,_*8,R*8,RA*4,_*8,R*8,RA*4,_*8,R*8,RA*4,_*8,R*8,RA*4,_*8,R*8,RA*4,_*8," \
  "R*8,RA*4,_*8,R*8,RA*4,_*8,R*8,RA*4,_*8,R*8,RA*4,_*8,R*8,RA*4,_*8,R*8,RA*4,_*8,R*8,RA*4,_*8,R*8," \
  "RA*4,_*8,R*8,RA*4,_*8,R*8,RA*4,_*8"
#define WAY_TOWN "_*30,R*1300"
#define WAY_08 \
  "_*60,L*8,_*1,L*8,_*1,L*8,_*1,L*8,_*1,LJ*30,_*1,LJ*30,_*1,LJ*30,_*1,J*8,LJ*22,_*1,LJ*30,_*1,LJ*30," \
  "_*1,LJ*30,_*1,L*8,_*1,LJ*30,_*1,LJ*30,_*1,LJ*30,_*1,LJ*30,_*1"
#define WAY_33 \
  "_*60,LJ*30,_*1,J*22,LJ*8,L*10,_*1,J*22,LJ*8,L*10,_*1,RJ*30,_*1,J*30,_*1,LJ*30,_*1,L*8,_*1,LJ*30," \
  "_*1,L*8,_*1,L*8,_*1,J*22,RJ*8,R*10,_*1,LJ*30,_*41,LJ*12,L*14,_*20,L*12,_*10,J*16,RJ*8,_*30," \
  "J*8,RJ*22,_*30,J*30,_*60"
#define WAY_07 \
  "_*60,R*25,_*1,R*25,_*1,J*22,RJ*8,R*10,_*1,LJ*30,_*1,L*25,_*1,L*25,_*1,RJ*30,_*1,LJ*30,_*1,L*8," \
  "_*1,L*8,_*1,R*25,_*1,RJ*30,_*1,R*25,_*1,J*22,RJ*8,R*10,_*1,LJ*30,_*1,LJ*30,_*1,L*8,_*1,R*25,_*1," \
  "R*25,_*1,J*22,RJ*8,R*10,_*1,J*22,LJ*8,L*10,_*1,J*22,RJ*8,R*10,_*1,J*22,LJ*8,L*10,_*1,LJ*30,_*1," \
  "J*30,_*1,J*22,LJ*8,L*10,_*1,J*22,RJ*8,R*10,_*1,LJ*30,_*1,L*8,_*1,R*25,_*1,J*22,RJ*8,R*10,_*1," \
  "LJ*30,_*1,L*8,_*1,LJ*30,_*1,L*25,_*1,LJ*30,_*1,L*8,_*1,RJ*30,_*1,R*8,_*1,LJ*30,_*1,L*8,_*1,L*8," \
  "_*1,L*25,_*1,RJ*30,_*1,R*8,_*1,RJ*30,_*1,R*25,_*1,RJ*30,_*1,R*25,_*1,RJ*30,_*1,L*8,_*1,LJ*30,_*1," \
  "RJ*30,_*1,R*25,_*1,R*25,_*1,RJ*30,_*1"
#define WAY_01_LEFT \
  "_*60,L*8,_*1,R*8,_*1,L*3,_*1,RJ*30,_*1,RJ*30,_*1,RJ*30,_*1,R*8,_*1,RJ*30,_*1,RJ*30,_*1,RJ*30,_*1," \
  "RJ*30,_*1,RJ*30,_*1,RJ*30,_*1,RJ*30,_*1,RJ*30,_*1,RJ*30,_*1,_*30,R*2,RJ*30,R*40,_*20,J*14,RJ*16," \
  "R*6,_*20,R*12,RJ*30,R*4,_*20,R*8,RJ*40,R*120"
#define WAY_14 "_*30,R*150,L*12,_*30,L*300"
#define WAY_03 "_*60,L*90,R*12,_*30,R*12,_*30,R*70,L*8,_*30,R*8,L*30,_*20,L*37,_*30,LJ*30,L*60"
#define WAY_21 "_*30,L*500,_*10,LJ*30,L*260,_*10,LJ*30,L*100"
#define WAY_10 "_*30,L*152,_*20,J*10,LJ*14,_*20,RJ*30,_*20,RJ*30,_*20,RJ*30,_*20,R*60,_*20,RJ*12,R*6,_*20,LJ*40,L*20,_*20,LJ*30,L*300"
#define WAY_TEMPLE \
  {ROOM_CROSSROADS_02, "_*30,R*600"}, {ROOM_CROSSROADS_39, "_*30,R*700"}, {ROOM_CROSSROADS_14, WAY_14}, \
  {ROOM_CROSSROADS_16, "_*30,L*200"}, {ROOM_CROSSROADS_03, WAY_03}, {ROOM_CROSSROADS_21, WAY_21}, \
  {ROOM_CROSSROADS_10, WAY_10}
static const Leg way_new_game[] = {
  {ROOM_TUTORIAL_01, WAY_KINGS_PASS}, {ROOM_TOWN, WAY_TOWN},
  {ROOM_CROSSROADS_01, "_*150,R*58,RJ*30,R*40,_*20,J*14,RJ*16,R*6,_*20,R*12,RJ*30,R*4,_*20,R*8,RJ*40,R*120"}, WAY_TEMPLE};
static const Leg way_hot_spring[] = {
  {ROOM_CROSSROADS_08, WAY_08}, {ROOM_CROSSROADS_33, WAY_33}, {ROOM_CROSSROADS_07, WAY_07},
  {ROOM_CROSSROADS_01, WAY_01_LEFT}, WAY_TEMPLE};
static int way_run(const char *name, const Leg *legs, int n) {
  extern bool enemies_fk_awake(void);
  int leg = 0, t = 0, idle = 0;
  const char *p = legs[0].keys;
  for (int tick = 0; tick < 30000; tick++) {
    if (leg + 1 < n && g_room.id == legs[leg + 1].room) leg++, p = legs[leg].keys, t = idle = 0;
    if (g_room.id == ROOM_CROSSROADS_10 && enemies_fk_awake()) {
      printf("ok   way %s: the False Knight comes down after %.0f s\n", name, tick * 0.02f);
      return 0;
    }
    /* (the leg's keys: "KEYS*TICKS,...", as --play has them) */
    uint32_t keys = 0;
    if (*p) {
      const char *q = p;
      for (; *q && *q != '*'; q++)
        keys |= *q == 'L' ? K_LEFT : *q == 'R' ? K_RIGHT : *q == 'U' ? K_UP : *q == 'D' ? K_DOWN : *q == 'J' ? K_JUMP
              : *q == 'A' ? K_ATTACK : 0;
      int k = *q == '*' ? atoi(q + 1) : 1;
      if (++t >= k) {
        while (*q && *q != ',') q++;
        p = *q == ',' ? q + 1 : q, t = 0;
      }
    } else if (++idle > 400) {
      printf("FAIL way %s: stuck in %s at %.2f,%.2f after its keys\n", name, room_name(g_room.id), g_hero.body.x, g_hero.body.y);
      return 1;
    }
    if (g_room.id != legs[leg].room && !game_changing_room()) {
      printf("FAIL way %s: in %s at %.2f,%.2f, not %s\n", name, room_name(g_room.id), g_hero.body.x, g_hero.body.y,
             room_name(leg + 1 < n ? legs[leg + 1].room : legs[leg].room));
      return 1;
    }
    run_ticks(1, keys);
  }
  printf("FAIL way %s: the False Knight never came down (in %s at %.2f,%.2f)\n", name, room_name(g_room.id), g_hero.body.x,
         g_hero.body.y);
  return 1;
}
static int way_test(void) {
  int fails = 0;
  /* (a new game, as the menu starts one: the Knight falling into King's Pass from its top) */
  game_new();
  shielded = true;
  game_enter_gate(ROOM_TUTORIAL_01, "top1");
  g_game.fade = 1, g_screen_fade = 255;
  game_fade_scene_in();
  fails += way_run("from King's Pass", way_new_game, sizeof way_new_game / sizeof way_new_game[0]);
  /* (the Aspid Hunters beaten, the Knight back from the hot spring) */
  game_new();
  room_load(ROOM_CROSSROADS_08);
  int ne;
  const Ent *es = room_ents(&ne);
  for (int i = 0; i < ne; i++)
    if (es[i].type == ENT_OBJ && es[i].flags == OK_ARENA) persist_set(es[i].persist);
  game_enter_gate(ROOM_CROSSROADS_08, "right1");
  fails += way_run("from the Aspid Hunters' arena", way_hot_spring, sizeof way_hot_spring / sizeof way_hot_spring[0]);
  shielded = false;
  printf("way: %d failing\n", fails);
  return fails ? 1 : 0;
}

static int gates_test(const char *which) {
  if (!strcmp(which, "way")) return way_test();
  if (!strcmp(which, "texts")) return texts_test();
  if (!strcmp(which, "seams")) return seams_test();
  if (!strcmp(which, "markers")) return markers_test();
  if (!strcmp(which, "arenas")) return arenas_test();
  if (!strcmp(which, "charms")) return charms_test();
  if (!strcmp(which, "stations")) return stations_test();
  if (!strcmp(which, "shaman")) return shaman_test();
  if (!strcmp(which, "respawn")) return respawn_test();
  if (!strcmp(which, "shopcharm")) return shop_charm_test();
  if (!strncmp(which, "wander:", 7)) {   /* (wander:ROOM|all:seed:ticks) */
    char room[64];
    int seed = 1, ticks = 6000;
    sscanf(which + 7, "%63[^:]:%d:%d", room, &seed, &ticks);
    return wander_test(room, seed, ticks);
  }
  int fails = 0, total = 0;
  for (int from = 0; from < NUM_ROOMS; from++) {
    if (strcmp(which, "all") && strcmp(which, room_name(from))) continue;
    if (!room_load(from)) continue;
    int n;
    const Ent *es = room_ents(&n);
    struct { int ent, kind, to; uint16_t entry; float delay, x, y; } ways[64];
    int nw = 0;
    for (int i = 0; i < n && nw < 64; i++) {
      if (es[i].type != ENT_GATE || (es[i].flags & G_ENTRY_ONLY)) continue;
      int to = es[i].a;
      uint16_t entry = es[i].s1;
      float delay = es[i].p0;
      if (es[i].flags & G_DOOR) {   /* (a door's record: the box flagged 4 after it) */
        to = 0xFFFF;
        for (int j = i + 1; j < n && es[j].type == ENT_BOX; j++)
          if (es[j].flags == 4) to = es[j].a, entry = es[j].s1, delay = es[j].p0;
      }
      if (to == 0xFFFF || to >= NUM_ROOMS) continue;
      ways[nw].ent = i, ways[nw].kind = gate_kind_of(&es[i]), ways[nw].to = to, ways[nw].entry = entry;
      ways[nw].delay = delay, ways[nw].x = (es[i].x0 + es[i].x1) / 2, ways[nw].y = (es[i].y0 + es[i].y1) / 2;
      nw++;
    }
    char from_name[64];
    snprintf(from_name, sizeof from_name, "%s", room_name(from));
    for (int w = 0; w < nw; w++) {
      game_new();
      if (!game_enter(from, ways[w].x, ways[w].y, true)) continue;
      /* (a side gate behind a collider of its own room, not the ground: walled off until something opens it, as
       * Crossroads_33's to the pillar) */
      bool walled = false;
      if (ways[w].kind == GATE_LEFT || ways[w].kind == GATE_RIGHT) {
        PhysHit hit;
        float dir = ways[w].kind == GATE_LEFT ? 1 : -1;
        walled = phys_ray(ways[w].x, hero_ground_y(ways[w].x, ways[w].y) + 0.5f, dir, 0, 8, 0xFF, &hit) && hit.col > 0;
      }
      char gate_name[64];
      snprintf(gate_name, sizeof gate_name, "%s", str_at(room_ents(&n)[ways[w].ent].s0));
      shielded = true;
      run_ticks(5, 0);
      if (ways[w].kind == GATE_DOOR) {
        /* (a door, as its Door Control goes through it: Enter takes the Knight's control, Change Scene) */
        hero_relinquish_control();
        hero_stop_anim_control();
        run_ticks(25, 0);
        hero_start_anim_control();
      }
      game_transition(ways[w].to, ways[w].entry, ways[w].kind, ways[w].delay, false);
      run_ticks(400, 0);   /* (the fade, the room, the Knight walking, dropping or jumping in, landing) */
      total++;
      const Hero *h = &g_hero;
      char why[256] = "";
      if (g_room.id != ways[w].to) snprintf(why + strlen(why), sizeof why - strlen(why), " in %s, not there", room_name(g_room.id));
      if (h->hidden) strcat(why, " hidden");
      if (h->cs.transitioning) strcat(why, " still entering");
      if (!h->accepting_input) strcat(why, " no input");
      if (h->state == HS_NO_INPUT) strcat(why, " no-input state");
      if (h->cs.dead || h->cs.hazard_death) strcat(why, " dead");
      if (h->body.x < 0 || h->body.x > g_room.h->w || h->body.y < 0 || h->body.y > g_room.h->h) strcat(why, " out of the room");
      if (!h->cs.on_ground) strcat(why, " not on the ground");
      /* (and he can slash, look up and down: nothing the way in took is kept from him, as a door's control) */
      if (!why[0]) {
        if (h->control_relinquished) strcat(why, " control kept");
        run_ticks(1, K_ATTACK);
        if (!h->cs.attacking) strcat(why, " can't attack");
        run_ticks(30, 0);
      }
      float x0 = h->body.x, y0 = h->body.y;
      int room0 = g_room.id;
      if (getenv("HKGATETRACE")) {
        printf("   arrived in %s by entry %s\n", room_name(g_room.id), str_at(ways[w].entry));
        int ne;
        const Ent *e2 = room_ents(&ne);
        for (int i = 0; i < ne; i++)
          if (e2[i].type == ENT_GATE || (e2[i].x0 < 8 && e2[i].x1 > -4))
            printf("   ent %d type %d flags %d %s box %.2f,%.2f..%.2f,%.2f a %d p %.2f %.2f %.2f %.2f\n", i, e2[i].type, e2[i].flags,
                   e2[i].type == ENT_GATE ? str_at(e2[i].s0) : "", e2[i].x0, e2[i].y0, e2[i].x1, e2[i].y1, e2[i].a, e2[i].p0,
                   e2[i].p1, e2[i].p2, e2[i].p3);
        PhysHit hit;
        for (float yy = 3; yy < 7; yy += 0.5f)
          if (phys_ray(-3, yy, 1, 0, 10, 0xFF, &hit)) printf("   ray right from -3,%.1f: hit %.2f,%.2f col %d flags %d\n", yy, hit.x, hit.y, hit.col, phys_col_flags(hit.col));
        for (float xx = -2; xx < 4; xx += 0.5f)
          if (phys_ray(xx, 6, 0, -1, 10, 0xFF, &hit)) printf("   ray down from %.1f,6: hit %.2f,%.2f col %d flags %d\n", xx, hit.x, hit.y, hit.col, phys_col_flags(hit.col));
      }
      /* (then spikes, where the room has some: back at the room's hazard respawn point, seen, free, on the ground) */
      int ne, hazards = 0;
      const Ent *e1 = room_ents(&ne);
      for (int i = 0; i < ne; i++)
        if (e1[i].type == ENT_DAMAGE && (int)e1[i].p0 >= HAZ_SPIKES) hazards++;
      if (g_room.id == room0 && !why[0] && hazards) {
        run_ticks(10, 0);
        hero_take_damage(SIDE_LEFT, 1, HAZ_SPIKES);
        if (getenv("HKSPIKETRACE")) setenv("HKGATETRACE", "1", 1);
        run_ticks(300, 0);
        if (getenv("HKSPIKETRACE")) unsetenv("HKGATETRACE");
        if (g_room.id != room0) strcat(why, " spikes: another room");
        if (h->hidden) strcat(why, " spikes: hidden");
        if (!h->accepting_input || h->state == HS_NO_INPUT) strcat(why, " spikes: no input");
        if (h->cs.dead || h->cs.hazard_death) strcat(why, " spikes: dead");
        if (h->body.x < 0 || h->body.x > g_room.h->w || h->body.y < 0 || h->body.y > g_room.h->h) strcat(why, " spikes: out of the room");
        if (!h->cs.on_ground) strcat(why, " spikes: not on the ground");
        if (why[0]) snprintf(why + strlen(why), sizeof why - strlen(why), " (respawn %.2f,%.2f)", h->body.x, h->body.y);
        if (why[0] && getenv("HKGATETRACE")) {
          room_load(room0);
          int ne;
          const Ent *e2 = room_ents(&ne);
          for (int i = 0; i < ne; i++)
            if (e2[i].type == ENT_GATE && e2[i].p3 > 0) {
              const Ent *m = &e2[(int)e2[i].p3 - 1];
              printf("   gate %s marker %d type %d at %.2f,%.2f\n", str_at(e2[i].s0), (int)e2[i].p3 - 1, m->type, m->x0, m->y0);
            }
        }
      }
      /* (then: can he move? right, left, a jump) */
      run_ticks(30, K_RIGHT);
      float xr = h->body.x;
      run_ticks(60, K_LEFT);
      float xl = h->body.x;
      run_ticks(1, 0);
      run_ticks(15, K_JUMP);
      float yj = h->body.y;
      if (g_room.id == room0 && fabsf(xr - x0) < 0.05f && fabsf(xl - xr) < 0.05f) strcat(why, " can't walk");
      if (g_room.id == room0 && fabsf(yj - y0) < 0.05f && fabsf(xl - x0) < 0.05f) strcat(why, " can't jump");

      shielded = false;
      bool bad = why[0] != 0 && !walled;
      fails += bad;
      printf("%s %s %s -> %s: at %.2f,%.2f%s%s\n", bad ? "FAIL" : why[0] ? "note" : "ok  ", from_name, gate_name, room_name(ways[w].to),
             x0, y0, why, bad || !why[0] ? "" : " (the gate is walled off in a new game)");
      room_load(from);
      es = room_ents(&n);
    }
  }
  printf("gates: %d ways, %d failing\n", total, fails);
  return fails ? 1 : 0;
}

int main(int argc, char **argv) {
  if (argc < 2) return 1;
  FILE *f = fopen(argv[1], "rb");
  if (!f) return 1;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t *data = malloc((size_t)n);
  if (fread(data, 1, (size_t)n, f) != (size_t)n) return 1;
  fclose(f);
  hk_bin = data;
  g_gfx_fast = getenv("HKFAST") != NULL;   /* (as the calculator draws when it is slow) */
  const char *room = "Tutorial_01", *shot = NULL;
  float cx = 40, cy = 14;
  int repeat = 1, pan = 0, sweep = 0;
  float step = 2;
  const char *items_out = NULL, *play = NULL, *shots = NULL;
  float px = 20, py = 20;
  int shot_every = 0, trace = 0;
  const char *gates = NULL, *gate_in = NULL;
  float dx = 0.15f;
  for (int i = 2; i < argc; i++) {
    if (!strcmp(argv[i], "--room")) room = argv[++i];
    else if (!strcmp(argv[i], "--cam")) sscanf(argv[++i], "%f,%f", &cx, &cy);
    else if (!strcmp(argv[i], "--shot")) shot = argv[++i];
    else if (!strcmp(argv[i], "--repeat")) repeat = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--pan")) pan = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--items")) items_out = argv[++i];
    else if (!strcmp(argv[i], "--dx")) dx = (float)atof(argv[++i]);
    else if (!strcmp(argv[i], "--sweep2d")) sweep = 2, step = (float)atof(argv[++i]);
    else if (!strcmp(argv[i], "--play")) play = argv[++i];
    else if (!strcmp(argv[i], "--at")) sscanf(argv[++i], "%f,%f", &px, &py);
    else if (!strcmp(argv[i], "--shots")) shots = argv[++i], shot_every = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--trace")) trace = 1;
    else if (!strcmp(argv[i], "--gates")) gates = argv[++i];
    else if (!strcmp(argv[i], "--gate")) gate_in = argv[++i];   /* (--play: in through the room's gate so named) */
    else if (!strcmp(argv[i], "--list")) {   /* (the rooms' names) */
      for (int r = 0; r < NUM_ROOMS; r++) printf("%s\n", room_name(r));
      return 0;
    }
  }
  if (gates) return gates_test(gates);
  int id = -1;
  for (int i = 0; i < NUM_ROOMS; i++)
    if (!strcmp(room_name(i), room)) id = i;
  if (id < 0 || !room_load(id)) {
    fprintf(stderr, "no room %s\n", room);
    return 1;
  }
  g_cam_x = cx, g_cam_y = cy;
  if (play) {
    /* the game with scripted keys: "KEYS*TICKS,..." (L R U D J jump A attack S dash F focus/cast, _ none) */
    game_new();
    g_pd.can_dash = true;
    if (getenv("HKMP")) g_pd.mp = (int16_t)atoi(getenv("HKMP"));
    if (getenv("HKHEALTH")) g_pd.health = (int8_t)atoi(getenv("HKHEALTH"));
    if (getenv("HKGRUBS")) g_pd.grubs_collected = (uint8_t)atoi(getenv("HKGRUBS"));
    if (getenv("HKPIECES")) {
      int hp = 0, vf = 0;
      sscanf(getenv("HKPIECES"), "%d,%d", &hp, &vf);
      g_pd.heart_pieces = (uint8_t)hp, g_pd.vessel_fragments = (uint8_t)vf;
    }
    if (getenv("HKGEO")) g_pd.geo = atoi(getenv("HKGEO"));
    if (getenv("HKSTAG")) g_pd.stag_position1 = (uint8_t)(atoi(getenv("HKSTAG")) + 1);   /* (stagPosition) */
    if (getenv("HKFIRE")) g_pd.fireball_level = atoi(getenv("HKFIRE"));
    if (getenv("HKHORNETGP")) g_pd.hornet_greenpath = (uint8_t)atoi(getenv("HKHORNETGP"));
    if (getenv("HKDASH")) g_pd.has_dash = true;
    if (getenv("HKMENDERSTATE")) g_pd.mender_state = (uint8_t)atoi(getenv("HKMENDERSTATE"));
    if (getenv("HKSHAMAN")) g_pd.shaman = (uint8_t)atoi(getenv("HKSHAMAN"));
    if (getenv("HKMAPPED")) memset(g_pd.rooms_mapped, 0xFF, sizeof g_pd.rooms_mapped);   /* (every room mapped) */
    if (getenv("HKTRINKETS"))   /* (relics: "1,2,0,0") */
      sscanf(getenv("HKTRINKETS"), "%hhu,%hhu,%hhu,%hhu", &g_pd.trinkets[0], &g_pd.trinkets[1], &g_pd.trinkets[2], &g_pd.trinkets[3]);
    for (const char *f = getenv("HKFLAGS"); f && *f;) {   /* (PlayerData bools on, by number: "7,8") */
      pd_set_flag(atoi(f), true);
      while (*f && *f != ',') f++;
      if (*f) f++;
    }
    for (const char *f = getenv("HKPERSIST"); f && *f;) {   /* (objects' states in the save on, by bit: "134") */
      persist_set(atoi(f));
      while (*f && *f != ',') f++;
      if (*f) f++;
    }
    /* (HKMENU: from the title screen, as the calculator starts; HKSAVES: the saves' folder) */
    bool menu = getenv("HKMENU") != NULL;
    if (getenv("HKSAVES")) {
      extern void host_save_dir(const char *d);
      host_save_dir(getenv("HKSAVES"));
    }
    if (menu)
      menu_start();
    /* (HKLOAD=slot: that save loaded, the Knight at its respawn point) */
    else if (getenv("HKLOAD")) {
      if (!save_load(atoi(getenv("HKLOAD"))) || (getenv("HKGEO") && (g_pd.geo = atoi(getenv("HKGEO")), 0)) || !game_respawn()) {
        printf("load failed\n");
        return 1;
      }
    } else if (gate_in)
      game_enter_gate(id, gate_in);
    else
      game_enter(id, px, py, true);
    int tick = 0;
    for (const char *p = play; *p;) {
      uint32_t keys = 0;
      for (; *p && *p != '*'; p++)
        keys |= *p == 'L' ? K_LEFT : *p == 'R' ? K_RIGHT : *p == 'U' ? K_UP : *p == 'D' ? K_DOWN : *p == 'J' ? K_JUMP
              : *p == 'A' ? K_ATTACK : *p == 'S' ? K_DASH : *p == 'F' ? K_FOCUS | K_SPELL
              /* (the calculator's OK, Back, Backspace: also jump, attack) */
              : *p == 'O' ? K_OK | K_JUMP : *p == 'B' ? K_BACK | K_ATTACK : *p == 'P' ? K_PAUSE
              : *p == 'I' ? K_INV : *p == 'M' ? K_MAP : 0;
      int n = *p == '*' ? atoi(++p) : 1;
      while (*p && *p != ',') p++;
      if (*p == ',') p++;
      for (int i = 0; i < n; i++, tick++) {
        /* (HKTALK=tick:text a conversation then; HKPROMPT=tick a prompt over the Knight then) */
        if (getenv("HKTALK") && atoi(getenv("HKTALK")) == tick) {
          dialogue_box_up();
          dialogue_start(atoi(strchr(getenv("HKTALK"), ':') + 1));
        }
        if (getenv("HKKILL") && atoi(getenv("HKKILL")) == tick) hero_take_damage(SIDE_LEFT, 99, HAZ_NORMAL);
        /* (HKFLING=tick: geo flung up, a way to the Knight's right) */
        if (getenv("HKFLING") && atoi(getenv("HKFLING")) == tick)
          geo_fling_at(1, getenv("HKFLINGN") ? atoi(getenv("HKFLINGN")) : 4, g_hero.body.x + 6, g_hero.body.y + 1, 8, 12, 60, 120, 0);
        /* (HKHURT=tick[:damage]) */
        if (getenv("HKHURT") && atoi(getenv("HKHURT")) == tick)
          hero_take_damage(SIDE_LEFT, strchr(getenv("HKHURT"), ':') ? atoi(strchr(getenv("HKHURT"), ':') + 1) : 1, HAZ_NORMAL);
        /* (HKSPIKES=tick: spikes then) */
        if (getenv("HKSPIKES") && atoi(getenv("HKSPIKES")) == tick) hero_take_damage(SIDE_LEFT, 1, HAZ_SPIKES);
        /* (HKREENTER=tick: the room entered again then, as it was left) */
        if (getenv("HKREENTER") && atoi(getenv("HKREENTER")) == tick) game_enter(id, px, py, true);
        if (getenv("HKGOD")) g_pd.health = g_pd.max_health;
        if (getenv("HKSHIELD")) g_hero.cs.invulnerable = true;   /* (nothing hurts him: the way, not the fights) */
        if (getenv("HKHIT") && atoi(getenv("HKHIT")) <= tick && (!getenv("HKHITEND") || tick < atoi(getenv("HKHITEND"))) &&
            tick % (getenv("HKHITN") ? atoi(getenv("HKHITN")) : 10) == 0) {
          extern void enemies_debug_hit(int damage);
          enemies_debug_hit(getenv("HKHITD") ? atoi(getenv("HKHITD")) : 21);
        }
        if (getenv("HKPROMPT") && atoi(getenv("HKPROMPT")) == tick)
          prompt_show(-1, TXT_PROMPT_LISTEN, g_hero.body.x, g_hero.body.y + 1.5f);
        if (!menu || !menu_tick(keys)) game_tick(stag_tick(shop_tick(inv_tick(keys))));
        if (trace && menu) printf("   menu in game %d\n", menu_in_game());
        if (trace)
          printf("%4d keys %02x pos %.3f,%.3f v %.3f,%.3f state %d ground %d jump %d fall %d clip %d frame %d cam %.2f,%.2f\n", tick,
                 keys, g_hero.body.x, g_hero.body.y, g_hero.body.vx, g_hero.body.vy, g_hero.state, g_hero.cs.on_ground,
                 g_hero.cs.jumping, g_hero.cs.falling, g_hero.anim.clip, g_hero.anim.frame, g_cam_x, g_cam_y);
        if (trace && getenv("HKCONTACTS"))
          for (int c = 0; c < g_hero.body.ncontacts; c++)
            printf("   contact col %d n %.2f,%.2f\n", g_hero.body.ccol[c], g_hero.body.cnx[c], g_hero.body.cny[c]);
        if (trace && getenv("HKSTATS")) printf("   health %d blue %d mp %d geo %d\n", g_pd.health, g_pd.health_blue, g_pd.mp, (int)g_pd.geo);
        if (trace && getenv("VMDEBUG")) {
          extern void vm_debug(void);
          vm_debug();
        }
        if (trace && getenv("ENEMIES")) {
          extern void enemies_debug(void);
          enemies_debug();
        }
        if (shots && shot_every && tick % shot_every == 0) {
          char path[256];
          snprintf(path, sizeof path, "%s/%05d.ppm", shots, tick);
          game_draw();
          host_shot(path);
        }
      }
    }
    if (getenv("HKACTORS")) {
      extern uint32_t g_actors_dropped, g_actors_peak, g_items_dropped_total, g_items_peak, g_hero_undrawn;
      printf("actors: peak %u, dropped %u; items: peak %u, dropped %u; the Knight not drawn in %u frames\n", g_actors_peak,
             g_actors_dropped, g_items_peak, g_items_dropped_total, g_hero_undrawn);
    }
    if (shot) {
      int nf = getenv("NF") ? atoi(getenv("NF")) : 3;
      for (int i = 0; i < nf; i++) {   /* (the tile cache warm, as while playing) */
        if (!menu) {
          game_draw();
          continue;
        }
        g_gfx_no_room = !menu_in_game();
        if (menu_in_game()) game_draw_layers();
        menu_draw();
        gfx_frame();
      }
      host_shot(shot);
    }
    if (getenv("HKGROUPS")) {
      extern uint32_t g_gfx_dropped;
      printf("fade %d items %u dropped %u cam %.2f,%.2f groups:", g_screen_fade, g_gfx_items, g_gfx_dropped, g_cam_x, g_cam_y);
      for (int g = 1; g < MAX_GROUPS; g++)
        if (g_group_alpha[g] != 255) printf(" %d=%d", g, g_group_alpha[g]);
      printf("\n");
      int n;
      const Ent *e = room_ents(&n);
      for (int i = 0; i < n; i++)
        if (e[i].type == ENT_MASK || e[i].type == ENT_BOX)
          printf("ent %d type %d flags %d group %d,%d box %.1f,%.1f %.1f,%.1f p %.2f %.2f %.2f %.2f\n", i, e[i].type, e[i].flags,
                 e[i].group, e[i].group2, e[i].x0, e[i].y0, e[i].x1, e[i].y1, e[i].p0, e[i].p1, e[i].p2, e[i].p3);
    }
    printf("end: pos %.3f,%.3f state %d\n", g_hero.body.x, g_hero.body.y, g_hero.state);
    if (getenv("HKROOM")) printf("room: %s\n", room_name(g_room.id));
    if (getenv("HKCAM")) cam_debug();
    if (getenv("HKPD")) {
      /* (the PlayerData bools on, by number; Hornet's Greenpath count) */
      printf("pd:");
      for (int i = 0; i < PDF_COUNT; i++)
        if (pd_flag(i)) printf(" %d", i);
      int np = 0;
      for (int i = 0; i < MAX_PERSIST; i++) np += persist_get(i);
      printf(" hornet_greenpath %d trinkets %d %d %d %d charms %d saved %d health %d+%d mp %d geo %d grubs %d"
             " max %d pieces %d %d reserve %d shaman %d fireball %d\n", g_pd.hornet_greenpath,
             g_pd.trinkets[0], g_pd.trinkets[1], g_pd.trinkets[2], g_pd.trinkets[3], g_pd.charms_owned, np, g_pd.health,
             g_pd.health_blue, g_pd.mp, (int)g_pd.geo, g_pd.grubs_collected, g_pd.max_health, g_pd.heart_pieces,
             g_pd.vessel_fragments, g_pd.mp_reserve_max, g_pd.shaman, g_pd.fireball_level);
    }
    return 0;
  }
  if (sweep == 2) {
    /* every camera position on a grid, drawn twice (the second frame's tiles are the view's): the most tiles a view
     * uses, and where */
    extern uint32_t g_tex_used;
    const RoomHdr *h = g_room.h;
    float x0 = 14.6f, x1 = h->w - 14.6f, y0 = 8.3f, y1 = h->h - 8.3f;
    if (x1 < x0) x1 = x0;
    if (y1 < y0) y1 = y0;
    uint32_t worst = 0, maxitems = 0, maxfront = 0;
    float wx = 0, wy = 0;
    for (float y = y0; y <= y1 + 0.01f; y += step)
      for (float x = x0; x <= x1 + 0.01f; x += step) {
        g_cam_x = x, g_cam_y = y;
        gfx_frame();
        gfx_frame();
        tex_frame();
        if (g_tex_used > worst) worst = g_tex_used, wx = x, wy = y;
        if (g_gfx_items > maxitems) maxitems = g_gfx_items;
        { extern uint32_t g_gfx_dropped; if (g_gfx_dropped > maxfront) maxfront = g_gfx_dropped; }
      }
    extern uint32_t g_peak_pals, g_peak_arena, g_peak_soft;
    printf("%s %u %.1f %.1f items %u dropped %u pals %u arena %u soft %u\n", room, worst, wx, wy, maxitems, maxfront, g_peak_pals,
           g_peak_arena, g_peak_soft);
    return 0;
  }
  if (pan) {
    extern uint32_t g_tex_used;
    uint32_t d0 = g_tex_decodes, maxd = 0, maxp = 0, maxu = 0, sumu = 0;
    for (int fr = 0; fr < pan; fr++) {
      uint32_t a = g_tex_decodes;
      gfx_frame();
      if (fr > 2 && g_tex_decodes - a > maxd) maxd = g_tex_decodes - a;
      if (g_gfx_pixels > maxp) maxp = g_gfx_pixels;
      if (fr > 0) { sumu += g_tex_used; if (g_tex_used > maxu) maxu = g_tex_used; }
      g_cam_x += dx;
    }
    extern uint32_t g_soft_misses;
    printf("%s pan %d frames: %.1f decodes/frame (max %u), max pixels %u, tiles used avg %u max %u, soft misses %.1f/frame\n", room, pan,
           (g_tex_decodes - d0) / (double)pan, maxd, maxp, sumu / (pan - 1), maxu, g_soft_misses / (double)pan);
    return 0;
  }
#ifdef HOST
  { extern uint32_t g_kind_px[6]; gfx_frame(); memset(g_kind_px, 0, sizeof g_kind_px); gfx_frame();
    printf("run pixels: solid %u axis %u turned %u soft %u bilinear %u (soft mono turned %u)\n", g_kind_px[0], g_kind_px[1], g_kind_px[2], g_kind_px[3], g_kind_px[4], g_kind_px[5]); }
#endif
  clock_t t0 = clock();
  for (int r = 0; r < repeat; r++) gfx_frame();
  double ms = (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC / repeat;
  extern uint32_t g_tex_calls;
  printf("%s %.1f,%.1f: %u items, %u pixels, %u decodes, %u misses, %u tile lookups, %.2f ms/frame\n", room, cx, cy, g_gfx_items,
         g_gfx_pixels, g_tex_decodes, g_tex_misses, g_tex_calls / repeat, ms);
  if (shot) host_shot(shot);
  if (getenv("TEXUSED")) {
    void tex_dump_used(const char *);
    tex_dump_used(getenv("TEXUSED"));
  }
  return 0;
}
