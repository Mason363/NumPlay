/* The game: a scene, the Knight in it, the camera; ticks of 1/50 s. */
#include "game.h"
#ifdef HOST
#include <stdio.h>
#include <stdlib.h>
#endif

PlayerData g_pd;
Game g_game;
static uint8_t inside[MAX_ENTS / 8];   /* the triggers the Knight is in */
static uint8_t ent_off[MAX_ENTS / 8];  /* (triggers whose objects are off) */

bool room_flag(int flag) { return flag >= 0 && flag < PDF_COUNT && pd_flag(flag); }

void ent_set_enabled(int i, bool on) {
  if (i < 0 || i >= MAX_ENTS) return;
  if (on) ent_off[i >> 3] &= (uint8_t)~(1 << (i & 7));
  else ent_off[i >> 3] |= (uint8_t)(1 << (i & 7));
}

void game_new(void) {
  memset(&g_pd, 0, sizeof g_pd);
  memset(&g_game, 0, sizeof g_game);
  g_game.time_scale = 1;
  g_pd.health = g_pd.max_health = 5;
  g_pd.max_mp = 99;
  g_pd.nail_damage = 5;
  g_pd.can_dash = false;
  g_pd.charm_slots = 3;
  strcpy(g_pd.respawn_scene, "Tutorial_01");
  strcpy(g_pd.respawn_marker, "Death Respawn Marker");
  dialogue_reset();
  prompts_reset();
  inv_reset();
  shop_reset();
  collect_reset();
  stag_reset();
  death_reset();
  white_blanker_reset();
  map_reset();
}

/* a room just loaded: no triggers in yet, gates whose objects a PlayerData bool turns off (a box flagged 2 after the
 * gate: the bool, off if it is true or false), the world's things */
static void door_reset(void);
static void room_entered(void) {
  memset(inside, 0, sizeof inside);
  door_reset();
  memset(ent_off, 0, sizeof ent_off);
  prompts_reset();   /* (PromptMarker.RecycleOnLevelLoad) */
  int n;
  const Ent *es = room_ents(&n);
  if (n > MAX_ENTS) n = MAX_ENTS;
  for (int i = 0; i < n; i++)
    if (es[i].type == ENT_GATE) {
      if (es[i].flags & G_ENTRY_ONLY) ent_set_enabled(i, false);
      for (int j = i + 1; j < n && es[j].type == ENT_BOX; j++)
        if ((es[j].flags & 2) && pd_flag((int)es[j].p0) == (es[j].p1 != 0)) ent_set_enabled(i, false);
    }
  world_enter();
}

bool game_enter(int room, float x, float y, bool facing_right) {
  if (!room_load(room)) return false;
  room_entered();
  hero_init(x, y, facing_right);
  cam_init();
  world_hero_in_position();
  g_game.hazard_x = x, g_game.hazard_y = y, g_game.hazard_facing_right = facing_right;
  return true;
}

/* a new game (GameManager.StartNewGame): the room's gate the Knight comes in at, as through it (King's Pass: falling in
 * from its top) */
static void scene_load(void);
bool game_enter_gate(int room, const char *gate) {
  if (!room_load(room)) return false;
  int n;
  const Ent *es = room_ents(&n);
  for (int i = 0; i < n; i++)
    if (es[i].type == ENT_GATE && !strcmp(str_at(es[i].s0), gate)) {
      hero_init(es[i].x0, es[i].y0, true);
      g_game.next_room = room, g_game.next_entry = es[i].s0, g_game.next_delay = 0;
      scene_load();
      return g_room.id == room;
    }
  return false;
}

/* HeroController.Respawn, as GameManager starts it: the respawn scene, the Knight at its marker (on the ground below),
 * health back, soul gone; on a bench he wakes on it, else on the ground */
bool game_respawn(void) {
  int room = -1;
  for (int i = 0; i < NUM_ROOMS && room < 0; i++)
    if (!strcmp(room_name(i), g_pd.respawn_scene)) room = i;
  if (room < 0 || !room_load(room)) return false;
  room_entered();
  float x = g_room.h->w / 2, y = g_room.h->h / 2;
  bool right = g_pd.respawn_facing_right;
  int n;
  const Ent *es = room_ents(&n);
  for (int i = 0; i < n; i++)
    if (es[i].type == ENT_RESPAWN && !strcmp(str_at(es[i].s0), g_pd.respawn_marker)) {
      x = es[i].x0, y = es[i].y0;
      if (g_pd.respawn_type != 1) right = es[i].flags & FACING_RIGHT;
      break;
    }
  hero_init(x, y, right);
  g_hero.body.y = hero_ground_y(x, y);
  hero_charm_update();   /* (CharmUpdate, MaxHealth) */
  g_pd.mp = 0;   /* (ClearMP) */
  g_game.hazard_x = g_hero.body.x, g_game.hazard_y = g_hero.body.y, g_game.hazard_facing_right = right;
  world_hero_in_position();
  if (g_pd.respawn_type == 1) {
    /* (FinishedEnteringScene, then the bench's RESPAWN) */
    hero_finished_entering_scene(true);
    g_pd.disable_pause = false;
    if (!bench_respawn(g_pd.respawn_marker)) hero_wake_up_ground();
  } else
    hero_wake_up_ground();
  cam_init();
  /* (the camera's RESPAWN: from black) */
  g_game.fade = 1, g_screen_fade = 255;
  game_fade(0, 0.5f, 0.1f);
  return true;
}

/* the parts of Hallownest (each an app), by their first rooms' numbers among all of theirs */
static const uint8_t part_base[] = PART_BASES;
int room_part(int global) {
  int k = 0;
  while (k + 1 < (int)sizeof part_base && global >= part_base[k + 1]) k++;
  return k;
}

bool game_enter_home(void) { return CROSS_HOME_GATE[0] && game_enter_gate(CROSS_HOME_ROOM, CROSS_HOME_GATE); }

/* a save made as the Knight went from another part into this one: by the gate he went to */
bool game_enter_cross(void) {
  int r = (int)g_pd.cross_room - 1 - ROOM_BASE;
  if (!g_pd.cross_room || r < 0 || r >= NUM_ROOMS) return false;
  char gate[sizeof g_pd.cross_gate];
  memcpy(gate, g_pd.cross_gate, sizeof gate);
  gate[sizeof gate - 1] = 0;
  g_pd.cross_room = 0;
  return game_enter_gate(r, gate);
}

const Ent *room_ents(int *n) {
  *n = g_room.h->nent;
  return (const Ent *)(const void *)(section(SEC_RBLOB) + g_room.h->ents);
}

/* ---------------------------------------------------------------- triggers: the Knight's box in the objects' */

/* ---------------------------------------------------------------- scene transitions (TransitionPoint, BeginSceneTransition) */
enum { SP_NONE, SP_LEAVING };

/* TransitionPoint.GetGatePosition: by its name */
static int gate_kind(const Ent *e) {
  if (e->flags & G_DOOR) return GATE_DOOR;
  const char *n = str_at(e->s0);
  if (strstr(n, "top")) return GATE_TOP;
  if (strstr(n, "right")) return GATE_RIGHT;
  if (strstr(n, "left")) return GATE_LEFT;
  if (strstr(n, "bot")) return GATE_BOTTOM;
  return strstr(n, "door") ? GATE_DOOR : GATE_UNKNOWN;
}

void game_fade_scene_in(void) { game_fade(0, 0.5f, 0.1f); }

void game_transition(int room, int entry, int gate, float delay, bool without_input) {
  Game *g = &g_game;
  if (g->scene_phase != SP_NONE || room < 0 || room == 0xFFFF) return;
  g->scene_phase = SP_LEAVING, g->scene_t = 0;
  g->next_room = room, g->next_entry = (uint16_t)entry, g->next_delay = delay;
  g_hero.enter_without_input = without_input;
  hero_leave_scene(gate);
  cam_freeze();
  if (g->fade < 1) game_fade(1, 0.33f, 0);   /* (the camera's FADE OUT) */
}

/* TryDoTransition: the Knight through a gate (facing it, not recoiling), or pushed back out of it */
static void gate_touched(const Ent *e) {
  Hero *h = &g_hero;
  /* (only as the game plays: not while the Knight is still coming in) */
  if ((e->flags & G_DOOR) || g_game.scene_phase != SP_NONE || h->cs.transitioning) return;
  int g = gate_kind(e);
  bool back = h->cs.recoiling || (g == GATE_RIGHT && !h->cs.facing_right) || (g == GATE_LEFT && h->cs.facing_right) ||
              e->a == 0xFFFF;   /* (or to a room this game leaves out) */
  const Body *b = &h->body;
  if (back && (g == GATE_RIGHT || g == GATE_LEFT)) {
    h->body.vx = 0;
    h->body.x += g == GATE_RIGHT ? e->x0 - (b->x + b->ox + b->hx) : e->x1 - (b->x + b->ox - b->hx);
  } else if (back && (g == GATE_TOP || g == GATE_BOTTOM)) {
    h->body.vy = 0;
    h->body.y += g == GATE_TOP ? e->y0 - (b->y + b->oy + b->hy) : e->y1 - (b->y + b->oy - b->hy);
  } else if (!back)
    game_transition(e->a, e->s1, g, e->p0, false);
}

/* the new room, the Knight at its entry gate */
static void scene_load(void) {
  Game *g = &g_game;
  g->scene_phase = SP_NONE;
  if (g->next_room & 0x8000) {
    /* (a room of another part of Hallownest: the way there kept, the game saved, a note) */
    g_pd.cross_room = (uint8_t)((g->next_room & 0x7FFF) + 1);
    strncpy(g_pd.cross_gate, str_at(g->next_entry), sizeof g_pd.cross_gate - 1);
    g_pd.cross_gate[sizeof g_pd.cross_gate - 1] = 0;
    save_game();
    menu_cross(room_part(g->next_room & 0x7FFF));
    return;
  }
  if (!room_load(g->next_room)) return;
  /* (OnNextLevelReady: the pause allowed again, as a door took it away) */
  g_pd.disable_pause = false;
  room_entered();
  int n;
  const Ent *es = room_ents(&n);
  const char *want = str_at(g->next_entry);
  for (int i = 0; i < n; i++) {
    const Ent *e = &es[i];
    if (e->type != ENT_GATE || strcmp(str_at(e->s0), want)) continue;
    float gx = (e->x0 + e->x1) / 2, gy = (e->y0 + e->y1) / 2;
    for (int j = i + 1; j < n && es[j].type == ENT_BOX; j++)
      if (es[j].flags == 1) gx = es[j].x0, gy = es[j].y0;
    hero_enter_scene(i, gate_kind(e), gx, gy, e->p1, e->p2, e->flags, g->next_delay + e->p0);
    return;
  }
  /* (no such gate: in the middle of the room) */
#ifdef HOST
  if (getenv("HKROOM")) printf("no gate %s in %s\n", want, room_name(g_room.id));
#endif
  hero_enter_scene(-1, GATE_DOOR, g_room.h->w / 2, g_room.h->h / 2, 0, 0, 0, g->next_delay);
}

static void scene_tick(float real_dt) {
  Game *g = &g_game;
  if (g->scene_phase != SP_LEAVING) return;
  /* (the scene loads once the camera has faded: half a second) */
  g->scene_t += real_dt;
  if (g->scene_t >= 0.5f) scene_load();
}

/* ---------------------------------------------------------------- doors (Door Control) */
/* a door's record (a box flagged 4 after its gate): its prompt's place (x0, y0), Crossroads Ascent (x1), Over Hero (y1);
 * the pause entering (p0), its prompt (p1: Enter, Ascend, Descend); the room it leads to (a), the gate there (s1) */
static const int16_t door_labels[] = {TXT_PROMPT_ENTER, TXT_PROMPT_ASCEND, TXT_PROMPT_DESCEND};
static struct {
  int16_t ent;     /* the door whose prompt is up (-1: none) */
  int8_t prompt;
  bool entering, visited;
  float t;         /* entering: since */
  float clock;     /* since the Knight finished entering the room (Wait for enter scene, Pause, Init) */
} door;

static const Ent *door_rec(int i) {
  int n;
  const Ent *es = room_ents(&n);
  for (int j = i + 1; j < n && es[j].type == ENT_BOX; j++)
    if (es[j].flags == 4) return &es[j];
  return NULL;
}

static void door_reset(void) { door.ent = -1, door.prompt = -1, door.entering = false, door.clock = 0; }

/* (Can Enter?: HeroController.CanInteract) */
static bool hero_can_interact(void) {
  const Hero *h = &g_hero;
  const CState *c = &h->cs;
  return h->accepting_input && h->state != HS_NO_INPUT && !c->dashing && !c->attacking && !h->control_relinquished &&
         !c->hazard_death && !c->hazard_respawning && !c->recoil_frozen && !c->recoiling && !c->transitioning &&
         c->on_ground;
}

static void door_trigger(int i, int kind) {
  const Ent *d = door_rec(i);
  if (!d || door.entering) return;
  if (kind == EV_EXIT) {
    /* (Out of range: Idle) */
    if (door.ent == i) prompt_hide(door.prompt), door.ent = -1;
    return;
  }
  /* (Idle until its pauses are over: 1 s; a Crossroads Ascent's 3.5, or 10 the first time; then In Range) */
  float wait = 0.2f + (d->x1 != 0 ? (door.visited ? 3.5f : 10) : 1);
  if (door.clock < wait || d->a == 0xFFFF) return;
  if (door.ent != i) {
    int l = (int)d->p1;
    door.ent = (int16_t)i;
    door.prompt = (int8_t)prompt_show(-1, door_labels[l >= 0 && l < 3 ? l : 0], d->x0, d->y0);
  }
  uint32_t p = g_hero.keys & ~g_hero.prev_keys;
  if (!(p & (K_UP | K_DOWN))) return;
  if (!hero_can_interact()) {
    /* (Cancel Frame: the prompt down, then Idle) */
    prompt_hide(door.prompt), door.ent = -1;
    return;
  }
  /* (Enter Anim?, Enter: the Knight's Enter (or Exit, over him) clip, his control gone, the camera's FADE OUT) */
  door.entering = true, door.t = 0;
  hero_relinquish_control();
  hero_stop_anim_control();
  anim_play(&g_hero.anim, d->y1 != 0 ? CLIP_KNIGHT_EXIT : CLIP_KNIGHT_ENTER);
  g_pd.disable_pause = true;
  game_fade(1, 0.33f, 0);
}

static void door_tick(void) {
  if (!door.entering) {
    if (world_hero_placed() && !g_hero.cs.transitioning) {
      /* (Init's Visited Check, after the first pause) */
      if (door.clock < 0.2f && door.clock + 0.02f >= 0.2f) door.visited = pd_flag(PDF_VISITED_CROSSROADS);
      door.clock += 0.02f;
    }
    return;
  }
  /* (half a second; then Change Scene) */
  if ((door.t += 0.02f) < 0.5f || g_game.scene_phase != SP_NONE) return;
  const Ent *d = door_rec(door.ent);
  hero_start_anim_control();
  if (d) game_transition(d->a, d->s1, GATE_DOOR, d->p0, false);
}

static void trigger_event(int i, const Ent *e, int kind) {
  Hero *h = &g_hero;
  switch (e->type) {
    case ENT_CAMLOCK:
      if (kind == EV_EXIT) cam_release(i);
      else cam_lock(i);   /* (Enter and Stay) */
      break;
    case ENT_GATE:
      if (e->flags & G_DOOR) door_trigger(i, kind);
      else if (kind != EV_EXIT) gate_touched(e);   /* (OnTriggerEnter2D, and OnTriggerStay2D until it has gone) */
      break;
    case ENT_HAZARD_TRIGGER:
      if (kind == EV_ENTER) {
        int n;
        const Ent *m = &room_ents(&n)[e->a];
        g_game.hazard_x = m->x0, g_game.hazard_y = m->y0, g_game.hazard_facing_right = m->flags & FACING_RIGHT;
      }
      break;
    default:
      world_trigger(i, kind);
      break;
  }
  (void)h;
}

static void triggers_tick(void) {
  const Body *b = &g_hero.body;
  float x0 = b->x + b->ox - b->hx, x1 = b->x + b->ox + b->hx, y0 = b->y + b->oy - b->hy, y1 = b->y + b->oy + b->hy;
  int n;
  const Ent *es = room_ents(&n);
  if (n > MAX_ENTS) n = MAX_ENTS;
  for (int i = 0; i < n; i++) {
    const Ent *e = &es[i];
    if (e->type != ENT_CAMLOCK && e->type != ENT_GATE && e->type != ENT_HAZARD_TRIGGER && e->type != ENT_MASK) continue;
    bool in = false, off = ent_off[i >> 3] >> (i & 7) & 1;
    int j = i + 1;
    for (; j < n && es[j].type == ENT_BOX; j++)
      if (es[j].flags == 0) in |= x1 > es[j].x0 && x0 < es[j].x1 && y1 > es[j].y0 && y0 < es[j].y1;
    if (x1 > e->x0 && x0 < e->x1 && y1 > e->y0 && y0 < e->y1 && !in) {
      /* (its own collider a polygon, not a box: its outline after, 4 points a record) */
      float pts[32];
      int np = 0;
      for (; j < n && es[j].type == ENT_SHAPE && np < 16; j++) {
        const Ent *q = &es[j];
        float v[8] = {q->x0, q->y0, q->x1, q->y1, q->p0, q->p1, q->p2, q->p3};
        memcpy(pts + 2 * np, v, sizeof v);
        np += 4;
      }
      in = !np || box_meets_polygon(x0, y0, x1, y1, pts, np);
    }
    bool was = inside[i >> 3] >> (i & 7) & 1;
    if (off) in = false;
    if (in) inside[i >> 3] |= (uint8_t)(1 << (i & 7));
    else inside[i >> 3] &= (uint8_t)~(1 << (i & 7));
    if (in || was) trigger_event(i, e, in && was ? EV_STAY : in ? EV_ENTER : EV_EXIT);
  }
}

/* ---------------------------------------------------------------- GameManager's coroutines */
enum { FZ_NONE, FZ_DOWN, FZ_WAIT, FZ_UP };

/* FreezeMoment(rampDownTime, waitTime, rampUpTime, targetSpeed): time slows to the target (in unscaled time), stays,
 * then comes back; hero: the Knight's recoil goes on after (StartRecoil waits for it) */
void game_freeze(float down, float wait, float up, float target, bool hero) {
  Game *g = &g_game;
  g->freeze_phase = FZ_DOWN, g->freeze_t = 0, g->freeze_from = g->time_scale;
  g->freeze_down = down, g->freeze_wait = wait, g->freeze_up = up, g->freeze_target = target;
  g->freeze_hero = g->freeze_hero || hero;
}

void game_freeze_moment(void) { game_freeze(0.001f, 0.25f, 0.05f, 0.0001f, true); }   /* (DAMAGE_FREEZE_*) */

static void set_time_scale(float s) { g_game.time_scale = s > 0.01f ? s : 0; }

static void freeze_tick(float real_dt) {
  Game *g = &g_game;
  if (g->freeze_phase == FZ_NONE) return;
  /* (SetTimeScale's loop: the scale for this frame, then the timer) */
  if (g->freeze_phase == FZ_DOWN) {
    if (g->freeze_t >= g->freeze_down) {
      set_time_scale(g->freeze_target);
      g->freeze_phase = FZ_WAIT, g->freeze_t = 0;
    } else
      set_time_scale(g->freeze_from + (g->freeze_target - g->freeze_from) * (g->freeze_t / g->freeze_down));
  }
  if (g->freeze_phase == FZ_WAIT) {
    if (g->freeze_t >= g->freeze_wait) g->freeze_phase = FZ_UP, g->freeze_t = 0, g->freeze_from = g->time_scale;
  }
  if (g->freeze_phase == FZ_UP) {
    if (g->freeze_t >= g->freeze_up) {
      set_time_scale(1);
      g->freeze_phase = FZ_NONE;
      if (g->freeze_hero) g->freeze_hero = false, hero_recoil_unfreeze();
      return;
    }
    set_time_scale(g->freeze_from + (1 - g->freeze_from) * (g->freeze_t / g->freeze_up));
  }
  g->freeze_t += real_dt;
}

/* the camera's fades (its Blanker): to black and back */
void game_fade(float to, float time, float delay) {
  Game *g = &g_game;
  g->fade_from = g->fade, g->fade_to = to, g->fade_t = 0, g->fade_time = time, g->fade_delay = delay;
}

static void fade_tick(void) {
  Game *g = &g_game;
  if (g->fade_delay > 0) {
    g->fade_delay -= 0.02f;
    return;
  }
  if (g->fade == g->fade_to) return;
  g->fade_t += 0.02f;
  float k = g->fade_time <= 0 || g->fade_t >= g->fade_time ? 1 : g->fade_t / g->fade_time;
  g->fade = g->fade_from + (g->fade_to - g->fade_from) * k;
  g_screen_fade = (uint8_t)(g->fade * 255 + 0.5f);
}

/* PlayerDeadFromHazard: the camera stops, the screen goes black, the Knight comes back at the hazard marker */
enum { HZ_NONE, HZ_NEXT_FRAME, HZ_FADING };
void game_player_dead_from_hazard(void) { g_game.hazard_phase = HZ_NEXT_FRAME, g_game.hazard_t = 0; }

static void hazard_tick(void) {
  Game *g = &g_game;
  if (g->hazard_phase == HZ_NEXT_FRAME) {
    cam_freeze();
    game_fade(1, 0.75f, 0);   /* (HAZARD FADE) */
    g->hazard_phase = HZ_FADING, g->hazard_t = 0;
  } else if (g->hazard_phase == HZ_FADING) {
    g->hazard_t += 0.02f;
    if (g->hazard_t >= 0.8f) {
      g->hazard_phase = HZ_NONE;
      hero_hazard_respawn();
    }
  }
}

/* ---------------------------------------------------------------- a tick: 1/50 s of real time */
static void step(uint32_t keys) {
  g_game.time += 0.02f;
  hero_fixed(keys);
  enemies_fixed();
  triggers_tick();
  door_tick();
  hero_check_damage();
  hero_update();
  fireballs_tick();
  death_tick();
  world_tick();
  benches_tick();
  vm_tick();
  msg_tick();
  collect_tick();
  titles_tick();
  prompts_tick();
  dialogue_tick();
  hazard_tick();
  fade_tick();
  hero_late_update();
  cam_tick();
  hud_tick();
  map_hud_tick();
}

void game_tick(uint32_t keys) {
  Game *g = &g_game;
  g_pd.play_time += 0.02f;   /* (GameManager: PlayerData's playTime, in real time) */
  /* (the inventory up: GameManager.SetIsInventoryOpen, the game paused, its time scale 0, till it is down again; the HUD
   * slides away meanwhile) */
  if (inv_open()) {
    hud_tick();
    return;
  }
  freeze_tick(0.02f);
  scene_tick(0.02f);
  /* (game time runs at the time scale: frozen, no steps) */
  g->step_acc += 0.02f * g->time_scale;
  if (g->step_acc >= 0.02f - 1e-6f) {
    g->step_acc -= 0.02f;
    step(keys);
  }
}

bool game_changing_room(void) { return g_game.scene_phase != SP_NONE; }

void game_draw_layers(void) {
  collect_draw();
  hud_draw();
  map_draw();
  msg_draw();
  titles_draw();
  dialogue_draw();
  prompts_draw();
  obj_draw();
  vm_draw();
  hero_draw();
  fireballs_draw();
  death_draw();
}

void game_draw(void) {
  game_draw_layers();
  gfx_frame();
}
