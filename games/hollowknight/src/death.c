/* The Knight's death: his Hero Death object (the Hero Death Anim FSM: his body bursting, soul and geo lost, a shade
 * left where he fell), GameManager.PlayerDead (the save, the fade), then the respawn at his bench. */
#pragma GCC optimize("Os")   /* (its code small: not where a frame's time goes) */
#include <math.h>
#include "game.h"

#define DT 0.02f
#define DEATH_WAIT 2.85f   /* (HeroController.DEATH_WAIT) */

enum { DD_NONE, DD_START, DD_ASH, DD_END };
typedef struct {
  bool on, stuck, chipped;
  float x, y, vx, vy, ang, w, gravity;
} Corpse;
static struct {
  uint8_t phase;
  float t;               /* the FSM's timer */
  float game_t;          /* PlayerDead's */
  bool fading;
  float x, y, sx;        /* the Hero Death object: its place, its x scale (the Knight's) */
  Anim body, shade;
  bool body_on, shade_on, soul_cracked;
  Corpse nail, head;     /* (Corpse Nail Hero, Corpse Head) */
  Anim head_anim;
} dd;

void death_reset(void) { memset(&dd, 0, sizeof dd); }

static void corpse_fling(Corpse *c, float x, float y, float angle, float speed, float gravity, float ang, float w) {
  float a = angle * (float)M_PI / 180;
  c->on = true, c->stuck = false, c->chipped = false;
  c->x = x, c->y = y, c->vx = cosf(a) * speed, c->vy = sinf(a) * speed;
  c->gravity = gravity, c->ang = ang, c->w = w;
}

/* a corpse piece: a ray along its step, bouncing off the ground (ObjectBounce); the nail sticks in it */
static void corpse_step(Corpse *c, bool nail) {
  if (!c->on || c->stuck) return;
  c->vy -= 60 * c->gravity * DT;   /* (Physics2D's gravity: 60 units/s^2 here) */
  c->ang += c->w * DT;
  float dx = c->vx * DT, dy = c->vy * DT, len = sqrtf(dx * dx + dy * dy);
  PhysHit hit;
  if (len > 0 && phys_ray(c->x, c->y, dx / len, dy / len, len + 0.1f, CF_TERRAIN, &hit)) {
    c->x = hit.x + hit.nx * 0.1f, c->y = hit.y + hit.ny * 0.1f;
    if (nail && hit.ny > 0.99f) {
      /* Stick: upright-ish in the ground */
      float a = fmodf(c->ang, 360);
      if (a < 0) a += 360;
      c->ang = a < rand_range(40, 50) ? 45 : a > rand_range(70, 80) ? 75 : a;
      c->stuck = true, c->vx = c->vy = 0;
      return;
    }
    if (!nail && !c->chipped) {
      c->chipped = true;   /* (Chip: the cracked head) */
      anim_play(&dd.head_anim, CLIP_KNIGHT_DEATH_HEAD_CRACKED);
    }
    float speed = sqrtf(c->vx * c->vx + c->vy * c->vy);
    if (speed > 1) {
      float d = c->vx * hit.nx + c->vy * hit.ny;
      c->vx = (c->vx - 2 * d * hit.nx) * 0.6f, c->vy = (c->vy - 2 * d * hit.ny) * 0.6f;
      c->w *= 0.6f;
    } else
      c->vx = c->vy = 0, c->w = 0;
  } else
    c->x += dx, c->y += dy;
}

/* (Set Shade: the nearest shade marker, else where the Knight fell) */
static void set_shade(void) {
  strncpy(g_pd.shade_scene, room_name(g_room.id), SCENE_NAME - 1);
  g_pd.shade_scene[SCENE_NAME - 1] = 0;
  float best = 1e30f, sx = dd.x, sy = dd.y;
  int n;
  const Ent *es = room_ents(&n);
  for (int i = 0; i < n; i++) {
    if (es[i].type != ENT_SHADE_MARKER) continue;
    float ex = es[i].x1 - dd.x, ey = es[i].y1 - dd.y, d = ex * ex + ey * ey;
    if (d < best) best = d, sx = es[i].x0, sy = es[i].y0;
  }
  g_pd.shade_x = sx, g_pd.shade_y = sy;
  int hp = g_pd.max_health / 2;
  g_pd.shade_health = (int8_t)(hp < 1 ? 1 : hp > 99 ? 99 : hp);
  g_pd.shade_fireball_level = (int8_t)g_pd.fireball_level;
  /* Check MP: by the soul vessels the Knight has */
  int r = g_pd.mp_reserve_max;
  g_pd.shade_mp = (int16_t)(r >= 132 ? 5 : r >= 99 ? 4 : r >= 66 ? 3 : r >= 33 ? 2 : 1);
}

void death_start(float x, float y, bool facing_right) {
  death_reset();
  dd.sx = facing_right ? -1.0f : 1.0f;
  dd.x = x + 0.21f * dd.sx, dd.y = y;
  /* Remove Geo, Limit Soul, Set Shade, Check MP, Save */
  g_pd.geo_pool = g_pd.geo, g_pd.geo = 0;
  bool cracked = !g_pd.soul_limited;
  g_pd.soul_limited = true;
  set_shade();
  persist_reset_semi();   /* (PlayerDead: ResetSemiPersistentItems, then the save) */
  save_game();
  /* (Send Events: HERO LEAVE) */
  enemies_hero_leave();
  /* Initiate: a shake, the moment frozen; Start: the Death animation */
  cam_shake(SHAKE_ENEMY_KILL);
  game_freeze(0.01f, 0.35f, 0.1f, 0, false);
  anim_play_from_frame(&dd.body, CLIP_KNIGHT_DEATH, 0);
  dd.body_on = true;
  dd.phase = DD_START, dd.t = 0;
  dd.soul_cracked = cracked;   /* (for Limit Soul?: the HUD's limiter up) */
  /* PlayerDead: the camera stays */
  cam_freeze();
}

void death_tick(void) {
  if (dd.phase == DD_NONE) return;
  dd.t += DT, dd.game_t += DT;
  dd.body.events = 0;
  anim_update(&dd.body, DT);
  if (dd.shade_on) anim_update(&dd.shade, DT);
  dd.head_anim.events = 0;
  anim_update(&dd.head_anim, DT);
  corpse_step(&dd.nail, true);
  corpse_step(&dd.head, false);
  switch (dd.phase) {
    case DD_START:
      if (dd.t >= 0.5f) {
        /* Drain Soul; Blow: the nail flung */
        g_pd.mp = 0, g_pd.mp_reserve = 0;
        corpse_fling(&dd.nail, dd.x, dd.y, rand_range(50, 130), rand_range(18, 22), 0.8f, rand_range(0, 360), rand_range(-720, 720));
        dd.phase = DD_ASH;
      }
      break;
    case DD_ASH:
      if ((dd.body.events & ANIM_DONE) || !dd.body.playing) {
        /* Facing Check, Head Left / Right: the head flung */
        if (dd.sx > 0) corpse_fling(&dd.head, dd.x + 0.2f, dd.y - 0.02f, 100, 15, 1.5f, -15, 200);
        else corpse_fling(&dd.head, dd.x - 0.2f, dd.y - 0.02f, 80, 15, 1.5f, 20, -200);
        anim_play(&dd.head_anim, CLIP_KNIGHT_DEATH_HEAD_NORMAL);
        /* Limit Soul?, End: the soul limited; Shade?: the shade rises */
        if (dd.soul_cracked) hud_soul_limiter(true);
        g_pd.max_mp = 66;
        if (g_pd.mp > g_pd.max_mp) g_pd.mp = g_pd.max_mp;
        dd.body_on = false;
        anim_play_from_frame(&dd.shade, CLIP_SHADE_APPEAR, 0);
        dd.shade_on = true;
        dd.phase = DD_END;
      }
      break;
  }
  /* PlayerDead: after DEATH_WAIT the camera fades (RESPAWN FADE), 0.8 s later the respawn */
  if (!dd.fading && dd.game_t >= DEATH_WAIT) {
    dd.fading = true;
    game_fade(1, 0.75f, 0);
  }
  if (dd.fading && dd.game_t >= DEATH_WAIT + 0.8f) {
    death_reset();
    bool home = false;
    if (!game_respawn() && !(home = game_enter_home())) {
      /* (his bench in another part of Hallownest: where this part's world meets it; else King's Pass) */
      strcpy(g_pd.respawn_scene, "Tutorial_01");
      g_pd.respawn_type = 0;
      game_respawn();
    }
    if (home) hero_charm_update(), g_pd.mp = 0;   /* (as a respawn: health back, soul gone) */
  }
}

void death_draw(void) {
  if (dd.phase == DD_NONE) return;
  Inst in;
  if (dd.body_on && dd.body.sprite >= 0) {
    sprite_inst(dd.body.sprite, dd.x, dd.y, 0.004f, dd.sx, 1, 0, &in);
    gfx_actor(&in, SORT_KEY(0, 0));
  }
  if (dd.shade_on && dd.shade.sprite >= 0) {
    sprite_inst(dd.shade.sprite, dd.x - 0.1436f * dd.sx, dd.y + 0.201f, 0.004f, dd.sx, 1, 0, &in);
    gfx_actor(&in, SORT_KEY(0, 0));
  }
  if (dd.nail.on) {
    sprite_inst_rot(SPRITE_CORPSE_NAIL, dd.nail.x, dd.nail.y, 0.003f, 1, 1, dd.nail.ang, 0, &in);
    gfx_actor(&in, SORT_KEY(0, 0));
  }
  if (dd.head.on && dd.head_anim.sprite >= 0) {
    sprite_inst_rot(dd.head_anim.sprite, dd.head.x, dd.head.y, 0.003f, dd.sx, 1, dd.head.ang, 0, &in);
    gfx_actor(&in, SORT_KEY(0, 0));
  }
}
