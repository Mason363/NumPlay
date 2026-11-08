/* The Knight's Spell Control FSM: holding the cast key focuses soul into health; a tap casts a spell. */
#include <math.h>
#include "game.h"

#define DT 0.02f
#define BUTTON_DOWN_TIME 0.25f
#define FOCUS_START_TIME 0.25f
#define TIME_PER_MP_DRAIN 0.027f      /* (Time Per MP Drain UnCH: no charm) */
#define TIME_PER_MP_DRAIN_CH 0.018f   /* (Time Per MP Drain CH: Quick Focus) */
#define GRACE_TIME 0.45f
#define MP_COST 33

enum {
  SP_INACTIVE, SP_BUTTON_DOWN, SP_CANCEL, SP_FOCUS_START, SP_FOCUS, SP_FOCUS_HEAL, SP_FOCUS_GET_FINISH, SP_FOCUS_CANCEL,
  SP_BACK_IN, SP_FIREBALL_ANTIC, SP_FIREBALL_RECOIL
};
#define FIREBALL_RECOIL_DISTANCE 1.0f
static void fireball_spawn(void);

static struct {
  uint8_t state;
  float t, grace;
  bool pressed_up, pressed_down, refocusing, didnt_hit_full, can_cancel;
  int start_mp;
  /* (HeroController's MP drain) */
  bool drain;
  float drain_timer, drain_time;
  int drained;
} sp;

static bool cast_held(void) { return g_hero.keys & K_SPELL; }
static bool cast_pressed(void) { return (g_hero.keys & K_SPELL) && !(g_hero.prev_keys & K_SPELL); }

void fireballs_reset(void);
void spell_reset(void) {
  memset(&sp, 0, sizeof sp);
  fireballs_reset();
}

/* StartMPDrain, StopMPDrain; the drain itself is HeroController.Update's */
/* (StartMPDrain, at the Time Per MP Drain that Set Focus Speed chose) */
static void drain_start(void) {
  sp.drain = true, sp.drain_timer = 0, sp.drained = 0;
  sp.drain_time = charm_on(7) ? TIME_PER_MP_DRAIN_CH : TIME_PER_MP_DRAIN;
}

static void focus_end_common(void) {
  sp.drain = false;
  g_hero.cs.focusing = false;
}

static void regain_control(void) {
  /* Regain Control, then Back In?: holding still (and not stopped at full health) goes on focusing */
  g_hero.cs.focusing = false;
  hero_regain_control();
  hero_start_anim_control();
  sp.can_cancel = false;
  sp.state = SP_BACK_IN;
}

static void can_focus(void) {
  /* Can Focus? */
  if (g_pd.mp < MP_COST || !hero_can_focus()) {
    sp.didnt_hit_full = false;
    sp.state = SP_CANCEL, sp.t = DT;   /* (Cancel: the next frame) */
    return;
  }
  /* Start Slug Anim, Focus Start */
  sp.can_cancel = true;
  sp.grace = 0, sp.didnt_hit_full = true, sp.refocusing = false;
  g_hero.cs.focusing = true;
  hero_relinquish_control();
  hero_stop_anim_control();
  g_hero.body.vx = 0;
  anim_play(&g_hero.anim, CLIP_KNIGHT_FOCUS);
  sp.state = SP_FOCUS_START, sp.t = FOCUS_START_TIME;
}

static void focus_cancel(void) {
  focus_end_common();
  anim_play_from_frame(&g_hero.anim, CLIP_KNIGHT_FOCUS_END, 0);
  sp.state = SP_FOCUS_CANCEL;
}

static void focus_enter(void) {
  sp.start_mp = g_pd.mp;
  sp.state = SP_FOCUS;
  drain_start();
}

/* Grace Check: let go soon after starting (or after a heal), the soul is given back */
static void grace_check(void) {
  float limit = sp.refocusing ? GRACE_TIME : 0.2f;
  if (sp.grace <= limit) g_pd.mp = (int16_t)sp.start_mp;
  focus_cancel();
}

void spell_update(void) {
  Hero *h = &g_hero;
  /* HeroController.Update's MP drain */
  if (sp.drain) {
    sp.drain_timer += DT;
    while (sp.drain_timer >= sp.drain_time) {
      sp.drained++;
      sp.drain_timer -= sp.drain_time;
      if (g_pd.mp > 0) g_pd.mp--;
      if (sp.drained == MP_COST && sp.state == SP_FOCUS) {
        /* FOCUS COMPLETED: Spore Cloud, Set HP Amount, Focus Heal */
        sp.drain = false;
        sp.grace = 0, sp.refocusing = true;
        anim_play_from_frame(&h->anim, CLIP_KNIGHT_FOCUS_GET, 0);
        cam_shake(SHAKE_ENEMY_KILL);
        hero_add_health(1);
        sp.state = SP_FOCUS_HEAL, sp.t = 0.2f;
      }
    }
  }
  switch (sp.state) {
    case SP_INACTIVE:
      sp.can_cancel = false;
      if (cast_pressed()) {
        sp.state = SP_BUTTON_DOWN, sp.t = BUTTON_DOWN_TIME;
        sp.pressed_up = sp.pressed_down = false;
      } else if (cast_held())
        can_focus();   /* (ALREADY DOWN) */
      break;
    case SP_BUTTON_DOWN:
      if (h->keys & K_UP) sp.pressed_up = true;
      if (h->keys & K_DOWN) sp.pressed_down = true;
      if (!cast_held()) {
        /* Can Cast?, Spell Choice: no spells yet, back to Inactive */
        sp.state = SP_INACTIVE;
        spell_cast(sp.pressed_up, sp.pressed_down);
      } else if ((sp.t -= DT) <= 0)
        can_focus();
      break;
    case SP_CANCEL:
      if ((sp.t -= DT) <= 0) sp.state = SP_INACTIVE;
      break;
    case SP_FOCUS_START:
      h->body.vx = 0;
      if (!cast_held() || !h->cs.on_ground) focus_cancel();
      else if ((sp.t -= DT) <= 0) focus_enter();
      break;
    case SP_FOCUS:
      h->body.vx = 0;
      sp.grace += DT;
      if (!cast_held() || !h->cs.on_ground) {
        focus_end_common();
        grace_check();
      }
      break;
    case SP_FOCUS_HEAL:
      h->body.vx = 0;
      if ((sp.t -= DT) <= 0) {
        /* Full HP? */
        if (g_pd.health >= g_pd.max_health || g_pd.mp < MP_COST) {
          sp.didnt_hit_full = false;
          focus_end_common();
          anim_play_from_frame(&h->anim, CLIP_KNIGHT_FOCUS_GET_ONCE, 0);
          sp.state = SP_FOCUS_GET_FINISH, sp.t = 0.23f;
        } else
          focus_enter();
      }
      break;
    case SP_FOCUS_GET_FINISH:
      if ((sp.t -= DT) <= 0) regain_control();
      break;
    case SP_FOCUS_CANCEL:
      if (h->anim.events & ANIM_DONE || !h->anim.playing) regain_control();
      break;
    case SP_BACK_IN:
      sp.state = SP_INACTIVE;
      if (cast_held() && sp.didnt_hit_full) can_focus();
      break;
    case SP_FIREBALL_ANTIC:
      h->body.vx = h->body.vy = 0;
      if (h->anim.events & ANIM_DONE) {
        /* Level Check, Fireball 1: its cast, the soul it takes, the fireball */
        anim_play_from_frame(&h->anim, CLIP_KNIGHT_FIREBALL1_CAST, 0);
        g_pd.mp = (int16_t)(g_pd.mp > MP_COST ? g_pd.mp - MP_COST : 0);   /* (TakeMP) */
        fireball_spawn();
        /* Fireball Recoil: back a little, no gravity, till the cast is over */
        hero_gravity(false);
        sp.state = SP_FIREBALL_RECOIL;
        enemies_hero_cast_spell();   /* (HERO CAST SPELL, to all) */
      }
      break;
    case SP_FIREBALL_RECOIL:
      h->body.vy = 0;
      h->body.vx = FIREBALL_RECOIL_DISTANCE * 2 * (h->cs.facing_right ? -1.0f : 1.0f);
      if (h->anim.events & ANIM_DONE) {
        /* Spell End */
        h->body.vx = 0;
        hero_regain_control();
        hero_start_anim_control();
        hero_gravity(true);
        sp.can_cancel = false;
        sp.state = SP_INACTIVE;
      }
      break;
  }
}

/* the FSM's FSM CANCEL (and the Knight's death): its focus or spell stopped */
void spell_cancel(void) {
  if (sp.state == SP_INACTIVE) return;
  bool focusing = sp.state >= SP_FOCUS_START && sp.state <= SP_FOCUS_CANCEL;
  bool casting = sp.state == SP_FIREBALL_ANTIC || sp.state == SP_FIREBALL_RECOIL;
  sp.drain = false;
  sp.didnt_hit_full = false;
  g_hero.cs.focusing = false;
  sp.state = SP_INACTIVE;
  if (focusing) hero_start_anim_control();
  if (casting) {
    /* (RegainControl, StartAnimationControl, AffectedByGravity) */
    hero_regain_control();
    hero_start_anim_control();
    hero_gravity(true);
  }
}

/* its global events as the Knight is hit (Reset Cam Zoom, Cancel All) or leaves the room (Cancel Some): the same, then,
 * whatever it was doing (their Can Cancel test is switched off), RegainControl, StartAnimationControl and
 * AffectedByGravity: what a door, a script or anything else took from him, he has again in the next room */
void spell_cancel_regain(void) {
  spell_cancel();
  hero_regain_control();
  hero_start_anim_control();
  hero_gravity(true);
}

bool spell_busy(void) { return sp.state != SP_INACTIVE && sp.state != SP_BUTTON_DOWN; }

/* Can Cast?, Spell Choice: up a scream, down a quake (the Knight has neither yet: Has Scream?, Has Quake? go on to),
 * a fireball if he has it */
void spell_cast(bool up, bool down) {
  (void)up, (void)down;
  if (g_pd.mp < MP_COST || !hero_can_cast() || g_pd.fireball_level <= 0) return;
  /* Wallside?: on a wall, turned away from it; Fireball Antic: the Knight held still in the air */
  Hero *h = &g_hero;
  sp.can_cancel = true;
  if (h->cs.wall_sliding) hero_face(!h->cs.facing_right);
  anim_play_from_frame(&h->anim, CLIP_KNIGHT_FIREBALL_ANTIC, 0);
  hero_relinquish_control();
  hero_stop_anim_control();
  hero_gravity(false);
  h->body.vx = h->body.vy = 0;
  sp.state = SP_FIREBALL_ANTIC;
}

/* ---------------------------------------------------------------- Vengeful Spirit (Fireball Top, Fireball) */
#define MAX_FIREBALLS 2
#define FIRE_SPEED 40.0f
#define FB_SX 1.3f             /* (Set Damage: its scale with Shaman Stone; 1 without) */
#define FB_SY 1.6f
#define FB_DAMAGE 15
#define FB_DAMAGE_SHAMAN 20
#define FB_MAGNITUDE 1.5f      /* (damages_enemy: magnitudeMult) */
enum { FB_OFF, FB_PAUSE, FB_IDLE, FB_DISSIPATE, FB_GONE, FB_WALL };

typedef struct {
  uint8_t st;
  bool right;
  float t, sx, sy;        /* (its scale) */
  Body body;              /* (its Terrain Checker's box: what stops it) */
  Anim anim, blast, impact;
  float bx, by;           /* the blast's place (Fireball Top's child) */
  bool blast_on, impact_on;
  uint32_t hit_enemies;   /* (damages_enemy: each once) */
  uint32_t hit_objs[3];
} Fireball;
static Fireball fbs[MAX_FIREBALLS];

void fireballs_reset(void) { memset(fbs, 0, sizeof fbs); }

static void fireball_spawn(void) {
  Fireball *f = &fbs[0];
  for (int i = 0; i < MAX_FIREBALLS; i++)
    if (fbs[i].st == FB_OFF || fbs[i].st == FB_GONE) {
      f = &fbs[i];
      break;
    }
  const Hero *h = &g_hero;
  memset(f, 0, sizeof *f);
  f->sx = charm_on(19) ? FB_SX : 1, f->sy = charm_on(19) ? FB_SY : 1;   /* (Set Damage) */
  /* Fireball Top, at the Knight: its blast ahead of him, the camera shaken, the fireball sent off */
  f->right = h->cs.facing_right;
  float k = f->right ? 1.0f : -1.0f;
  f->bx = h->body.x + 1.13f * k, f->by = h->body.y;
  f->blast_on = true;
  anim_play_from_frame(&f->blast, CLIP_FIREBALL_BLAST, 0);
  cam_shake(SHAKE_AVERAGE);
  Body *b = &f->body;
  b->x = h->body.x + 1.1683f * k, b->y = h->body.y - 0.5428f;
  b->vx = FIRE_SPEED * k;
  /* (the Terrain Checker's box, at the fireball's scale) */
  b->ox = 0.3948f * f->sx * k, b->oy = -0.02f * f->sy, b->hx = 0.68255f * f->sx, b->hy = 0.315f * f->sy;
  b->mask = CF_TERRAIN;
  anim_play(&f->anim, CLIP_FIREBALL_BALL);
  f->st = FB_PAUSE;
}

static void fireball_hit_things(Fireball *f) {
  /* (its trigger box: 2.61 x 2.12 about (0.53, -0.02), at its scale) */
  float k = f->right ? 1.0f : -1.0f, cx = f->body.x + 0.5344f * f->sx * k, cy = f->body.y - 0.0179f * f->sy;
  float hx = 2.6111f / 2 * f->sx, hy = 2.1246f / 2 * f->sy;
  float dir = f->right ? 0 : 180;
  int damage = f->sx > 1 ? FB_DAMAGE_SHAMAN : FB_DAMAGE;
  f->hit_enemies |= enemies_spell(cx - hx, cy - hy, cx + hx, cy + hy, dir, damage, FB_MAGNITUDE, f->hit_enemies);
  obj_spell(cx - hx, cy - hy, cx + hx, cy + hy, dir, f->hit_objs);
  vm_spell(cx - hx, cy - hy, cx + hx, cy + hy, dir);
}

void fireballs_tick(void) {
  for (int i = 0; i < MAX_FIREBALLS; i++) {
    Fireball *f = &fbs[i];
    if (f->st == FB_OFF) continue;
    f->t += DT;
    if (f->blast_on) {
      f->blast.events = 0;
      anim_update(&f->blast, DT);
      if (f->blast.events & ANIM_DONE) f->blast_on = false;   /* (DeactivateAfter2dtkAnimation) */
    }
    if (f->impact_on) {
      f->impact.events = 0;
      anim_update(&f->impact, DT);
      if (f->impact.events & ANIM_DONE) f->impact_on = false;
    }
    f->anim.events = 0;
    anim_update(&f->anim, DT);
    switch (f->st) {
      case FB_PAUSE:
        /* (a frame on: Init, then Idle; moving meanwhile) */
        f->st = FB_IDLE, f->t = DT;
        /* fall through */
      case FB_IDLE: {
        body_step(&f->body, DT);
        fireball_hit_things(f);
        /* (any collision with the terrain, or stopped by it: WALL) */
        if (f->body.ncontacts > 0 || fabsf(f->body.vx) < 0.1f) {
          /* Wall Impact: still, its impact ahead of it; the camera shaken */
          f->body.vx = 0;
          cam_shake(SHAKE_AVERAGE);
          f->impact_on = true;
          anim_play_from_frame(&f->impact, CLIP_FIREBALL_FIREBALL_WALL_IMPACT, 0);
          f->st = FB_WALL, f->t = 0;
        } else if (f->t >= 0.45f) {
          /* Dissipate: its end, no longer hitting */
          anim_play_from_frame(&f->anim, CLIP_FIREBALL_BALL_END, 0);
          f->st = FB_DISSIPATE, f->t = 0;
        }
        break;
      }
      case FB_DISSIPATE:
        f->body.x += f->body.vx * DT;
        if (f->anim.events & ANIM_DONE) f->st = FB_GONE, f->t = 0;
        break;
      case FB_GONE:
      case FB_WALL:
        /* (Dissipate End, Break: recycled after 2 s) */
        if (f->t >= 2 && !f->blast_on && !f->impact_on) f->st = FB_OFF;
        break;
    }
  }
}

/* (their material's UI/BlendModes/Screen: the blast, the ball and its impact screened over what is behind, their black
 * nothing) */
static void fireball_actor(Inst *in) {
  in->flags = (uint8_t)((in->flags & ~F_BLEND) | BL_SCREEN);
  gfx_actor(in, SORT_KEY(0, 0));
}

#ifdef HOST
bool g_fireballs_hidden;   /* (tests: the view without them) */
#endif

void fireballs_draw(void) {
#ifdef HOST
  if (g_fireballs_hidden) return;
#endif
  for (int i = 0; i < MAX_FIREBALLS; i++) {
    const Fireball *f = &fbs[i];
    if (f->st == FB_OFF) continue;
    float k = f->right ? 1.0f : -1.0f;
    Inst in;
    if (f->blast_on) {
      sprite_inst(f->blast.sprite, f->bx, f->by, -0.001f, 2 * k, 2, 0, &in);
      fireball_actor(&in);
    }
    if (f->st == FB_PAUSE || f->st == FB_IDLE || f->st == FB_DISSIPATE) {
      sprite_inst(f->anim.sprite, f->body.x, f->body.y, -0.002f, f->sx * k, f->sy, 0, &in);
      fireball_actor(&in);
    }
    if (f->impact_on) {
      /* (Wall Impact: its child, 1.71 ahead, at 2.5 its scale) */
      sprite_inst(f->impact.sprite, f->body.x + 1.7094f * f->sx * k, f->body.y, -0.002f, 2.5f * f->sx * k, 2.5f * f->sy, 0, &in);
      fireball_actor(&in);
    }
  }
}
