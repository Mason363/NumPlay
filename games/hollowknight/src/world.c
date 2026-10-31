/* The room's game objects: their state, run each tick; the render groups they fade. */
#pragma GCC optimize("Os")   /* (its code small: not where a frame's time goes) */
#include "game.h"

#define DT 0.02f

/* ---------------------------------------------------------------- render groups: iTweenFadeTo */
#define MAX_FADES 16
static struct {
  uint8_t group;   /* (0: free) */
  float from, to, t, time;
} fades[MAX_FADES];

void group_fade(int g, float alpha, float time) {
  if (g <= 0 || g >= MAX_GROUPS) return;
  /* (a new fade on a group replaces its old one) */
  int k = -1;
  for (int i = 0; i < MAX_FADES && k < 0; i++)
    if (fades[i].group == g) k = i;
  for (int i = 0; i < MAX_FADES && k < 0; i++)
    if (!fades[i].group) k = i;
  if (time <= 0 || k < 0) {
    g_group_alpha[g] = (uint8_t)(alpha * 255 + 0.5f);
    if (k >= 0 && fades[k].group == g) fades[k].group = 0;
    return;
  }
  fades[k].group = (uint8_t)g;
  fades[k].from = g_group_alpha[g] / 255.0f, fades[k].to = alpha, fades[k].t = 0, fades[k].time = time;
}

static void fades_tick(void) {
  for (int i = 0; i < MAX_FADES; i++) {
    int g = fades[i].group;
    if (!g) continue;
    fades[i].t += DT;
    float k = fades[i].t >= fades[i].time ? 1 : fades[i].t / fades[i].time;
    float a = fades[i].from + (fades[i].to - fades[i].from) * k;
    g_group_alpha[g] = (uint8_t)(a * 255 + 0.5f);
    if (k >= 1) fades[i].group = 0;
  }
}

/* ---------------------------------------------------------------- the objects' state */
typedef struct {
  uint8_t state;
} EntState;
static EntState es[MAX_ENTS];
static bool hero_in_position;

bool persist_get(int i) { return i != NO_PERSIST && (g_pd.persist[i >> 3] >> (i & 7) & 1); }
/* GameManager.ResetSemiPersistentItems: as the Knight rests at a bench or dies */
void persist_reset_semi(void) {
  static const uint16_t semi[] = SEMI_PERSIST;
  for (unsigned k = 0; k < sizeof semi / sizeof semi[0]; k++) persist_clear(semi[k]);
  obj_keep_cocoons();
  vm_reset_semi();
}

void persist_clear(int i) {
  if (i != NO_PERSIST) g_pd.persist[i >> 3] &= (uint8_t)~(1 << (i & 7));
}

void persist_set(int i) {
  if (i != NO_PERSIST) g_pd.persist[i >> 3] |= (uint8_t)(1 << (i & 7));
}

/* masks' states (their FSMs') */
enum { M_PAUSE, M_IDLE, M_IDLE_STAY, M_FADE_OUT, M_FADE_IN, M_DONE };

static void mask_idle(const Ent *e, EntState *s) {
  if (e->flags == MK_SECRET) {
    if (persist_get(e->persist)) {
      group_fade(e->group, 0, 0.1f);   /* (Activate) */
      s->state = M_DONE;
    } else
      s->state = M_IDLE_STAY;
    return;
  }
  if (e->flags == MK_REMASK && e->p2 >= 0) {
    /* (Idle fades the owner at once; an inverse mask child the other way) */
    group_fade(e->group, e->p2, 0.01f);
    group_fade(e->group2, 1 - e->p2, 0.01f);
  }
  s->state = M_IDLE;
}

/* a simple mask's Idle: HIT fades it */
static void mask_hit(const Ent *e, EntState *s) {
  if (e->flags != MK_SIMPLE || s->state != M_IDLE) return;
  group_fade(e->group, 0, e->p0);
  s->state = M_DONE;
}

static void mask_trigger(const Ent *e, EntState *s, int kind) {
  float t = e->p0;
  switch (e->flags) {
    case MK_SECRET:
      if (s->state == M_IDLE_STAY && kind == EV_STAY) {
        group_fade(e->group, 0, t);
        persist_set(e->persist);
        s->state = M_DONE;
      }
      break;
    case MK_REMASK: {
      float out = e->p3 < 0 ? 0 : e->p3;   /* (Fade Out's alpha; Fade In's the other) */
      if ((s->state == M_IDLE || s->state == M_FADE_IN) && kind == EV_STAY) {
        group_fade(e->group, out, t);
        group_fade(e->group2, 1 - out, t);
        persist_set(e->persist);
        s->state = M_FADE_OUT;
      } else if (s->state == M_FADE_OUT && kind == EV_EXIT) {
        group_fade(e->group, 1 - out, t);
        group_fade(e->group2, out, t);
        s->state = M_FADE_IN;
      }
      break;
    }
    case MK_SIMPLE:   /* (its trigger sends HIT) */
      if ((e->p2 == 0 && kind == EV_ENTER) || (e->p2 == 1 && kind == EV_STAY)) mask_hit(e, s);
      break;
  }
}

/* ---------------------------------------------------------------- the room */
void world_enter(void) {
  memset(es, 0, sizeof es);
  memset(fades, 0, sizeof fades);
  memset(g_group_alpha, 255, sizeof g_group_alpha);
  for (int g = g_room.hide_lo; g && g <= g_room.hide_hi && g < MAX_GROUPS; g++) g_group_alpha[g] = 0;
  hero_in_position = false;
  int n;
  const Ent *e = room_ents(&n);
  for (int i = 0; i < n && i < MAX_ENTS; i++) {
    if (e[i].type != ENT_MASK) continue;
    if (e[i].flags == MK_SIMPLE) es[i].state = M_IDLE;
    else es[i].state = M_PAUSE;
    if (e[i].group2) group_fade(e[i].group2, 0, 0);   /* (Pause: the inverse mask hidden) */
  }
  obj_enter();
  titles_enter();
  vm_enter();
  /* (SceneManager: the room's map zone, PlayerData's) */
  static const uint8_t zones[NUM_ROOMS] = ROOM_ZONES;
  if (g_room.id >= 0 && g_room.id < NUM_ROOMS) g_pd.map_zone = zones[g_room.id];
  map_room_entered();
}

/* (Pause's WaitForHeroInPosition: the masks start as the Knight is placed; its Wait only counts when it was placed
 * already) */
bool world_hero_placed(void) { return hero_in_position; }

void world_hero_in_position(void) {
  if (hero_in_position) return;
  hero_in_position = true;
  int n;
  const Ent *e = room_ents(&n);
  for (int i = 0; i < n && i < MAX_ENTS; i++)
    if (e[i].type == ENT_MASK && es[i].state == M_PAUSE) mask_idle(&e[i], &es[i]);
  titles_hero_in_position();
}

/* an object's FSM sent HIT to another (a breakable's hitEventReciever) */
void world_send_hit(int i) {
  int n;
  const Ent *e = room_ents(&n);
  if (i < 0 || i >= n || i >= MAX_ENTS) return;
  if (e[i].type == ENT_MASK) mask_hit(&e[i], &es[i]);
}

void world_trigger(int i, int kind) {
  int n;
  const Ent *e = &room_ents(&n)[i];
  if (e->type == ENT_MASK) mask_trigger(e, &es[i], kind);
}

void world_tick(void) {
  obj_tick();
  fades_tick();
}
