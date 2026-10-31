/* What the Knight interacts with by pressing up: benches (Bench Control, its Detect Range's Detect Hero). */
#pragma GCC optimize("Os")   /* (its code small: not where a frame's time goes) */
#include <math.h>
#include "game.h"

#define DT 0.02f
#define MAX_BENCHES 2

enum {
  BS_IDLE, BS_IN_RANGE, BS_CANCEL_FRAME, BS_START_REST, BS_SAVE_FRAME, BS_PAUSE, BS_RESTING_INIT, BS_RESTING,
  BS_WAKE, BS_GET_OFF, BS_IDLE_PAUSE, BS_REGAIN, BS_RESPAWN_ASLEEP, BS_STARTLE, BS_MAP_UPDATE, BS_MAP_OPEN, BS_MAP_IDLE,
  BS_MAP_CLOSE, BS_INV_MAP
};

typedef struct {
  uint16_t ent;
  uint8_t state;
  int8_t prompt;
  bool near;           /* (Detect Hero: the Knight in its Detect Range) */
  bool sleeping, get_off_wake, face_right;
  float t, sx, sy, fx, fy;   /* (a timer; the Knight's tween: from, to) */
} Bench;
static Bench benches[MAX_BENCHES];
static int nbenches;

static const Ent *ent_at(int i) {
  int n;
  return &room_ents(&n)[i];
}

/* the Knight's collider in a trigger's box */
static bool hero_in(const Ent *b) {
  const Body *k = &g_hero.body;
  float x0 = k->x + k->ox - k->hx, x1 = k->x + k->ox + k->hx, y0 = k->y + k->oy - k->hy, y1 = k->y + k->oy + k->hy;
  return x0 < b->x1 && x1 > b->x0 && y0 < b->y1 && y1 > b->y0;
}

static uint32_t pressed(void) { return g_hero.keys & ~g_hero.prev_keys; }

static void near_changed(Bench *b, const Ent *e, bool near) {
  /* Detect Hero: Close, Idle (EaseColor over 0.5 s: the bench lit, its light brighter) */
  b->near = near;
  if (e->group) group_fade(e->group, near ? 1 : 0, 0.5f);
  if (e->group2) group_fade(e->group2, near ? 1 : 0.3686f, 0.5f);
}

void benches_enter(void) {
  nbenches = 0;
  int n;
  const Ent *es = room_ents(&n);
  for (int i = 0; i < n && nbenches < MAX_BENCHES; i++) {
    if (es[i].type != ENT_OBJ || es[i].flags != OK_BENCH) continue;
    Bench *b = &benches[nbenches++];
    memset(b, 0, sizeof *b);
    b->ent = (uint16_t)i, b->prompt = -1;
    if (es[i].group) group_fade(es[i].group, 0, 0);
    if (es[i].group2) group_fade(es[i].group2, 0.3686f, 0);
  }
}

/* the Knight sits at the bench: Start Rest */
static void start_rest(Bench *b, const Ent *e) {
  Hero *h = &g_hero;
  hero_relinquish_control();
  hero_stop_anim_control();
  hero_gravity(false);
  prompt_hide(b->prompt), b->prompt = -1;
  anim_play(&h->anim, CLIP_KNIGHT_SIT);
  /* (iTweenMoveTo: over the bench, a little up, in 0.2 s) */
  b->sx = h->body.x, b->sy = h->body.y, b->fx = e->x0 + e->x1, b->fy = h->body.y + e->y1;
  b->t = 0;
  b->state = BS_START_REST;
}

/* Rest Burst: health back, the bench the place to come back to, then the game saved */
static void rest_burst(Bench *b, const Ent *e) {
  hero_max_health();
  pd_set_flag(PDF_AT_BENCH, true);   /* (atBench: the inventory opens, charms can be changed) */
  b->sleeping = false, b->get_off_wake = false;
  save_set_respawn(str_at(e->s0), b->face_right = g_hero.cs.facing_right);
  persist_reset_semi();
  b->state = BS_PAUSE, b->t = 0;
}

static void get_off(Bench *b, const Ent *e) {
  /* Get Off: the Knight back down from the bench */
  Hero *h = &g_hero;
  pd_set_flag(PDF_AT_BENCH, false);
  hero_gravity(true);
  anim_play_from_frame(&h->anim, b->get_off_wake ? CLIP_KNIGHT_WAKE : CLIP_KNIGHT_GET_OFF, 0);
  hud_slide(false);
  b->sx = h->body.x, b->sy = h->body.y, b->fx = h->body.x - e->x1, b->fy = h->body.y - e->y1;
  b->t = 0;
  b->state = BS_GET_OFF;
}

static void tween_hero(Bench *b, float time, bool ease_out) {
  /* (iTweenMoveTo, easeOutSine; iTweenMoveBy, linear) */
  Hero *h = &g_hero;
  float k = b->t >= time ? 1 : b->t / time;
  if (ease_out) k = sinf(k * 1.5707964f);
  h->body.x = b->sx + (b->fx - b->sx) * k, h->body.y = b->sy + (b->fy - b->sy) * k;
  h->body.vx = h->body.vy = 0;
}

void benches_tick(void) {
  Hero *h = &g_hero;
  for (int i = 0; i < nbenches; i++) {
    Bench *b = &benches[i];
    const Ent *e = ent_at(b->ent);
    const Ent *rest = e + 1, *range = e + 2;
    bool near = hero_in(range);
    if (near != b->near) near_changed(b, e, near);
    bool in = hero_in(rest);
    b->t += DT;
    switch (b->state) {
      case BS_IDLE:
        if (in) {
          b->prompt = (int8_t)prompt_show(b->prompt, TXT_PROMPT_REST, e->p0, e->p1);
          b->state = BS_IN_RANGE;
        }
        break;
      case BS_IN_RANGE:
        if (!in) {
          prompt_hide(b->prompt), b->prompt = -1;
          b->state = BS_IDLE;
        } else if (pressed() & (K_UP | K_DOWN)) {
          /* Can Rest? */
          if (!h->accepting_input || !h->cs.on_ground || h->cs.attacking || h->cs.up_attacking || h->cs.down_attacking ||
              h->cs.dashing)
            b->state = BS_CANCEL_FRAME;
          else
            start_rest(b, e);
        }
        break;
      case BS_CANCEL_FRAME:
        prompt_hide(b->prompt), b->prompt = -1;
        b->state = BS_IDLE;
        break;
      case BS_START_REST:
        tween_hero(b, 0.2f, true);
        if (b->t >= 0.3f) b->state = BS_SAVE_FRAME;
        break;
      case BS_SAVE_FRAME:
        rest_burst(b, e);
        break;
      case BS_PAUSE:
        if (b->t >= 0.1f) {
          save_game();
          /* Update Map: with the quill and a map, the rooms visited mapped (Map Update, its message); Resting Init */
          b->t = 0;
          if (pd_flag(PDF_HAS_QUILL) && pd_flag(PDF_HAS_MAP) && map_update()) {
            anim_play_from_frame(&h->anim, CLIP_KNIGHT_MAP_UPDATE, 0);
            map_msg_show();
            b->state = BS_MAP_UPDATE;
          } else
            b->state = BS_RESTING_INIT;
        }
        break;
      case BS_MAP_UPDATE:
        if ((h->anim.events & ANIM_DONE) || !h->anim.playing) {
          anim_play(&h->anim, CLIP_KNIGHT_SIT_IDLE);   /* (Sit Idle) */
          b->state = BS_RESTING_INIT, b->t = 0;
        }
        break;
      case BS_RESTING_INIT:
        if (b->t >= 0.5f) b->state = BS_RESTING, b->t = 0;
        break;
      case BS_RESTING: {
        uint32_t p = pressed();
        bool up = p & (K_JUMP | K_ATTACK | K_UP | K_DOWN), left = p & K_LEFT, right = p & K_RIGHT;
        if (up || left || right) {
          /* Get Up, Get Left, Get Right: facing that way (Facing?) */
          bool r = left ? false : right ? true : h->cs.facing_right;
          if (r) hero_face(true);
          else hero_face(false);
          b->state = BS_WAKE, b->t = 0;
          if (b->sleeping) anim_play_from_frame(&h->anim, CLIP_KNIGHT_WAKE_TO_SIT, 0);
        } else if ((p & K_MAP) && pd_flag(PDF_HAS_MAP) && !inv_open()) {
          /* Open Map: Sit Map Open; tapped again soon, the inventory's map */
          anim_play_from_frame(&h->anim, CLIP_KNIGHT_SIT_MAP_OPEN, 0);
          g_pd.disable_pause = true;
          b->sleeping = false, b->get_off_wake = false;
          b->state = BS_MAP_OPEN, b->t = 0;
        } else if (b->t >= 10 && !b->sleeping) {
          /* Fall Asleep */
          anim_play(&h->anim, CLIP_KNIGHT_SIT_FALL_ASLEEP);
          b->get_off_wake = true, b->sleeping = true;
          b->t = 0;
        }
        break;
      }
      case BS_WAKE:
        /* Wake?: the Wake To Sit animation first if asleep */
        if (!b->sleeping || (h->anim.events & ANIM_DONE) || !h->anim.playing) get_off(b, e);
        break;
      case BS_GET_OFF:
        tween_hero(b, 0.2f, false);
        if (b->t >= 0.25f || (h->anim.events & ANIM_DONE)) {
          anim_play(&h->anim, CLIP_KNIGHT_IDLE);
          b->state = BS_IDLE_PAUSE, b->t = 0;
        }
        break;
      case BS_IDLE_PAUSE:
        if (b->t >= 0.1f) {
          hero_regain_control();
          hero_start_anim_control();
          b->state = BS_REGAIN, b->t = 0;
        }
        break;
      case BS_REGAIN:
        if (b->t >= 0.5f) b->state = BS_IDLE;   /* (Reactivate: Idle, in range or not) */
        break;
      case BS_MAP_OPEN:
        if (pressed() & K_MAP) {
          /* Inv Map Open */
          g_pd.disable_pause = false;
          inv_open_map();
          anim_play_from_frame(&h->anim, CLIP_KNIGHT_SIT_MAP_CLOSE, 0);
          b->state = BS_INV_MAP, b->t = 0;
        } else if (b->t >= 0.25f) {
          map_quick(true);   /* (Map Idle: OPEN QUICK MAP) */
          b->state = BS_MAP_IDLE;
        }
        break;
      case BS_MAP_IDLE:
        if (!(g_hero.keys & K_MAP) || inv_open()) {
          /* Close Map: Sit Map Close, a second, then pause again (Re-enable Pause) */
          map_quick(false);
          anim_play_from_frame(&h->anim, CLIP_KNIGHT_SIT_MAP_CLOSE, 0);
          b->state = BS_MAP_CLOSE, b->t = 0;
        }
        break;
      case BS_MAP_CLOSE:
        if (b->t >= 1) {
          g_pd.disable_pause = false;
          b->state = BS_RESTING, b->t = 0;
        }
        break;
      case BS_INV_MAP:
        if (b->t >= 0.42f) b->state = BS_RESTING, b->t = 0;
        break;
      case BS_RESPAWN_ASLEEP:
        /* Init Resting 2 (1.2 s), three frames, Kinemetise (up by the adjust vector), Init Resting (1.2 s): asleep,
         * then Startle */
        h->body.vx = h->body.vy = 0;
        if (b->t >= 1.26f && !b->sleeping) b->sleeping = true, h->body.x += e->x1, h->body.y += e->y1;
        if (b->t >= 2.46f) {
          anim_play_from_frame(&h->anim, CLIP_KNIGHT_WAKE_TO_SIT, 0);
          b->state = BS_STARTLE;
        }
        break;
      case BS_STARTLE:
        h->body.vx = h->body.vy = 0;
        if ((h->anim.events & ANIM_DONE) || !h->anim.playing) {
          b->get_off_wake = false, b->sleeping = false;   /* (Startle: Get Off Anim "Get Off") */
          /* (Update Map Silently: its message, no Map Update) */
          if (pd_flag(PDF_HAS_QUILL) && pd_flag(PDF_HAS_MAP) && map_update()) map_msg_show();
          b->state = BS_RESTING, b->t = 0;
        }
        break;
    }
  }
}

/* RESPAWN at the bench named so: the Knight (at its marker, on the ground) asleep on it; false if no such bench */
bool bench_respawn(const char *name) {
  for (int i = 0; i < nbenches; i++) {
    Bench *b = &benches[i];
    const Ent *e = ent_at(b->ent);
    if (strcmp(str_at(e->s0), name)) continue;
    Hero *h = &g_hero;
    hero_relinquish_control();
    hero_stop_anim_control();
    hero_gravity(false);
    hero_max_health();
    pd_set_flag(PDF_AT_BENCH, true);
    persist_reset_semi();   /* (Init Resting: ResetSemiPersistentItems) */
    anim_play(&h->anim, CLIP_KNIGHT_SIT_FALL_ASLEEP);
    h->body.vx = h->body.vy = 0;
    b->sleeping = false, b->get_off_wake = false;   /* (sleeping: here, once moved up) */
    b->state = BS_RESPAWN_ASLEEP, b->t = 0;
    return true;
  }
  return false;
}
