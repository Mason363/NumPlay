/* Drawing the lawn: plants, zombies, shots, sun and the dead, row by row (draw.h). */
#include <string.h>
#include "draw.h"
#include "game.h"
#include "gfx.h"
#include "res.h"

float cam_x;   /* the view's left in the game's pixels (0: the lawn as it is played) */

/* the game's pixels to the screen's */
static inline int SX(float x) { float v = (x - cam_x) * 0.4f; return (int)(v < 0 ? v - 0.5f : v + 0.5f); }
static inline int SY(float y) { float v = y * 0.4f; return (int)(v < 0 ? v - 0.5f : v + 0.5f); }
int screen_x(float x) { return SX(x); }
int screen_y(float y) { return SY(y); }

/* ---------------------------------------------------------------- plants */

int plant_anim(int kind) {
  static const uint16_t a[N_PLANTS] = {
    A_PEASHOOTER, A_SUNFLOWER, A_CHERRYBOMB, A_WALLNUT, A_POTATOMINE, A_SNOWPEA, A_CHOMPER, A_REPEATER,
    A_PUFFSHROOM, A_SUNSHROOM, A_FUMESHROOM, A_GRAVEBUSTER, A_HYPNOSHROOM, A_SCAREDYSHROOM, A_ICESHROOM, A_DOOMSHROOM,
    A_LILYPAD, A_SQUASH, A_THREEPEATER, A_TANGLEKELP, A_JALAPENO, A_SPIKEWEED, A_TORCHWOOD, A_TALLNUT,
    A_SEASHROOM, A_PLANTERN, A_CACTUS, A_BLOVER, A_SPLITPEA, A_STARFRUIT, A_PUMPKIN, A_MAGNETSHROOM,
    A_CABBAGEPULT, A_FLOWERPOT, A_KERNELPULT, A_COFFEEBEAN, A_GARLIC, A_UMBRELLA, A_MARIGOLD, A_MELONPULT,
    A_EXPLODENUT,
  };
  return a[kind];
}

/* the plant's animation now, and its time in it */
static int plant_now(const Plant *p, uint32_t *t) {
  int kind = p->kind, a = plant_anim(kind);
  uint32_t since = B.ticks - p->shot;
  *t = B.ticks + p->id * 37u;
  int full = plant_defs[kind].health;
  switch (kind) {
  case P_WALLNUT: case P_TALLNUT: case P_PUMPKIN: case P_GARLIC: {
    static const uint16_t c1[] = {A_WALLNUT_CRACKED1, A_TALLNUT_CRACKED1, A_PUMPKIN_CRACKED1, A_GARLIC_CRACKED1};
    static const uint16_t c2[] = {A_WALLNUT_CRACKED2, A_TALLNUT_CRACKED2, A_PUMPKIN_CRACKED2, A_GARLIC_CRACKED2};
    int k = kind == P_WALLNUT ? 0 : kind == P_TALLNUT ? 1 : kind == P_PUMPKIN ? 2 : 3;
    if (p->health < full / 3) a = c2[k];
    else if (p->health < full * 2 / 3) a = c1[k];
    break;
  }
  case P_CHOMPER:
    if (p->state == CH_BITE) a = A_CHOMPER_BITE, *t = (uint32_t)(70 - p->aux) * anim_info(A_CHOMPER_BITE).cycle / 70;
    else if (p->state == CH_DIGEST) a = A_CHOMPER_DIGEST;
    break;
  case P_POTATOMINE:
    if (p->action > 0) a = A_POTATOMINE_UNARMED;
    break;
  case P_SUNSHROOM:
    if (p->flags & PF_GROWN) a = A_SUNSHROOM_BIG;
    break;
  case P_SCAREDYSHROOM:
    if (p->flags & PF_SCARED) a = A_SCAREDYSHROOM_SCARED;
    break;
  case P_SQUASH:
    if (p->state) a = A_SQUASH_ATTACK, *t = (uint32_t)(185 - p->aux) * anim_info(A_SQUASH_ATTACK).cycle / 185;
    break;
  case P_TANGLEKELP:
    if (p->state) a = A_TANGLEKELP_GRAB;
    break;
  case P_CACTUS:
    if (p->flags & PF_TALL) a = A_CACTUS_TALL;
    break;
  case P_MAGNETSHROOM:
    if (p->action > 0) a = A_MAGNETSHROOM_ATTACK;
    break;
  case P_CABBAGEPULT: case P_KERNELPULT: case P_MELONPULT: {
    int att = kind == P_CABBAGEPULT ? A_CABBAGEPULT_ATTACK : kind == P_KERNELPULT ? A_KERNELPULT_ATTACK : A_MELONPULT_ATTACK;
    uint32_t c = anim_info(att).cycle;
    if (since < c / 2) a = att, *t = since + c / 2;
    break;
  }
  case P_FUMESHROOM:
    if (since < 30) a = A_FUMESHROOM_ATTACK;
    break;
  case P_UMBRELLA:
    if (since < anim_info(A_UMBRELLA_BLOCK).cycle) a = A_UMBRELLA_BLOCK, *t = since;
    break;
  case P_BLOVER:
    if (B.ticks - p->born > 50) a = A_BLOVER_LOOP;
    break;
  case P_CHERRYBOMB: case P_JALAPENO: case P_ICESHROOM: case P_DOOMSHROOM: case P_COFFEEBEAN: case P_GRAVEBUSTER:
    /* the fuse, once */
    *t = (uint32_t)(B.ticks - p->born) * anim_info(a).cycle / (kind == P_GRAVEBUSTER ? 400 : 100);
    if (*t >= anim_info(a).cycle) *t = anim_info(a).cycle - 1;
    break;
  }
  return a;
}

static int plant_layer(const Plant *p) {
  return p->kind == P_LILYPAD || p->kind == P_FLOWERPOT ? 0 : p->kind == P_PUMPKIN ? 2 : p->kind == P_COFFEEBEAN ? 3 : 1;
}

void draw_plant_at(int kind, int x, int y, int alpha) {
  Plant fake;
  memset(&fake, 0, sizeof fake);
  fake.kind = (uint8_t)kind, fake.health = plant_defs[kind].health, fake.shot = (uint32_t)-1000;
  uint32_t t;
  int a = plant_now(&fake, &t);
  gfx_anim(a, anim_frame_at(a, B.ticks), x, y, 0, TINT_NONE, alpha);
}

static void draw_plant(const Plant *p) {
  float x = cell_x(p->col), y = row_y(p->row, x);
  if (B.env == ENV_ROOF && p->kind != P_FLOWERPOT && plant_at(p->col, p->row, P_FLOWERPOT)) y -= 12;
  if (water_row(p->row) && p->kind != P_LILYPAD && p->kind != P_TANGLEKELP && p->kind != P_SEASHROOM) y -= 3;
  uint32_t t;
  int a = plant_now(p, &t);
  if (p->kind == P_SQUASH && p->state) {
    /* the jump: up, over, down onto its zombie */
    float k = (185 - p->aux) / 185.0f;
    float k2 = k * 2 > 1 ? 1 : k * 2;
    x += (p->tx - x) * k2;
    y -= (k < 0.5f ? k * 2 : (1 - k) * 2) * 70;
  }
  int tint = sleeping(p) ? TINT_SLEEP : TINT_NONE;
  if (p->kind != P_LILYPAD && p->kind != P_FLOWERPOT && p->kind != P_TANGLEKELP && p->kind != P_SPIKEWEED &&
      p->kind != P_PUMPKIN && !water_row(p->row))
    gfx_ellipse(SX(x), SY(y) - 1, 9, 3, 0, 9);
  gfx_anim(a, anim_frame_at(a, t), SX(x), SY(y), 0, tint, 32);
  if (tint == TINT_SLEEP && (B.ticks / 60 + p->id) % 3 == 0) {
    int k = (int)((B.ticks + p->id * 13) % 120);
    gfx_text(F_SMALL, SX(x) + 6 + k / 20, SY(y) - 18 - k / 8, "z", rgb565(0x9EB8FF), GFX_OUTLINE);
  }
}

/* ---------------------------------------------------------------- zombies */

/* walk, eat, headless, die and burn of each kind */
static const uint16_t z_walk[N_ZOMBIES] = {
  A_ZOMBIE_WALK, A_FLAG_WALK, A_CONE_WALK, A_POLE_WALK, A_BUCKET_WALK, A_NEWSPAPER_WALK, A_DOOR_WALK, A_FOOTBALL_WALK,
  A_DANCER_WALK, A_BACKUP_WALK, A_DUCKY1_WALK, A_SNORKEL_WALK, A_ZOMBONI_WALK, A_BOBSLED_WALK, A_DOLPHIN_WALK,
  A_JACK_WALK, A_BALLOON_WALK, A_DIGGER_WALK, A_POGO_WALK, A_BUNGEE_IDLE, A_LADDER_WALK, A_CATAPULT_WALK,
  A_GARGANTUAR_WALK, A_IMP_WALK,
};
static const uint16_t z_eat[N_ZOMBIES] = {
  A_ZOMBIE_EAT, A_FLAG_EAT, A_CONE_EAT, A_POLE_EAT, A_BUCKET_EAT, A_NEWSPAPER_EAT, A_DOOR_EAT, A_FOOTBALL_EAT,
  A_DANCER_EAT, A_BACKUP_EAT, A_DUCKY1_EAT, A_SNORKEL_EAT, A_ZOMBONI_WALK, A_BOBSLED_EAT, A_DOLPHIN_EAT, A_JACK_EAT,
  A_BALLOON_EAT, A_DIGGER_EAT, A_POGO_EAT, A_BUNGEE_IDLE, A_LADDER_EAT, A_CATAPULT_IDLE, A_GARGANTUAR_SMASH, A_IMP_EAT,
};
static const uint16_t z_headless[N_ZOMBIES] = {
  A_ZOMBIE_HEADLESS, A_ZOMBIE_HEADLESS, A_ZOMBIE_HEADLESS, A_POLE_HEADLESS, A_ZOMBIE_HEADLESS, A_NEWSPAPER_HEADLESS,
  A_ZOMBIE_HEADLESS, A_FOOTBALL_HEADLESS, A_DANCER_HEADLESS, A_BACKUP_HEADLESS, A_DUCKY1_WALK, A_SNORKEL_WALK,
  A_ZOMBONI_CRITICAL, A_BOBSLED_WALK, A_DOLPHIN_WALK, A_JACK_HEADLESS, A_BALLOON_WALK, A_DIGGER_WALK, A_POGO_WALK,
  A_BUNGEE_IDLE, A_LADDER_BARE, A_CATAPULT_WALK, A_GARGANTUAR_WALK, A_IMP_WALK,
};
static const uint16_t z_die[N_ZOMBIES] = {
  A_ZOMBIE_DIE, A_ZOMBIE_DIE, A_ZOMBIE_DIE, A_POLE_DIE, A_ZOMBIE_DIE, A_NEWSPAPER_DIE, A_ZOMBIE_DIE, A_FOOTBALL_DIE,
  A_DANCER_DIE, A_BACKUP_DIE, A_DUCKY_DIE, A_SNORKEL_DIE, A_ZOMBONI_DIE, A_BOBSLED_DIE, A_DOLPHIN_DIE, A_JACK_DIE,
  A_BALLOON_DIE, A_DIGGER_DIE, A_POGO_DIE, A_BUNGEE_DIE, A_LADDER_DIE, A_CATAPULT_DIE, A_GARGANTUAR_DIE, A_IMP_DIE,
};
static const uint16_t z_burn[N_ZOMBIES] = {
  A_ZOMBIE_BURN, A_ZOMBIE_BURN, A_ZOMBIE_BURN, A_POLE_BURN, A_ZOMBIE_BURN, A_NEWSPAPER_BURN, A_ZOMBIE_BURN,
  A_ZOMBIE_BURN, A_DANCER_BURN, A_BACKUP_BURN, A_ZOMBIE_BURN, A_ZOMBIE_BURN, A_ZOMBONI_DIE, A_ZOMBIE_BURN, A_ZOMBIE_BURN,
  A_ZOMBIE_BURN, A_ZOMBIE_BURN, A_ZOMBIE_BURN, A_ZOMBIE_BURN, A_BUNGEE_DIE, A_ZOMBIE_BURN, A_CATAPULT_DIE,
  A_GARGANTUAR_DIE, A_IMP_DIE,
};

/* in the pool's water, the zombie floats in a ducky tube */
static bool in_pool(const Zombie *z) { return water_row(z->row) && z->x > 100 && z->x < 770; }

static int walk_frame(const Zombie *z, int a) {
  float dist = 49.8f;
  switch (z->kind) {
  case Z_GARGANTUAR: dist = 124.1f; break;
  case Z_IMP: dist = 62.9f; break;
  case Z_DANCER: case Z_BACKUP: dist = 28.6f * 3; break;
  case Z_SNORKEL: dist = 59.9f; break;
  case Z_FOOTBALL: dist = 30 * 2; break;
  case Z_NEWSPAPER: dist = 57.8f; break;
  case Z_POLE: dist = z->flags & ZF_POLE ? 30 * 2 : 67.8f; break;
  case Z_DIGGER: case Z_LADDER: dist = 80; break;
  }
  if (z->flags & ZF_MINI) dist *= 0.5f;
  int n = anim_frames(a);
  return (int)(z->walk / dist * n) % n;
}

/* the animation a zombie shows now, its frame, and whether it is drawn turned round */
static int zombie_now(const Zombie *z, int *frame, int *flip) {
  int k = z->kind, a;
  bool eating = (z->flags & ZF_EATING) != 0;
  *flip = (z->flags & (ZF_HYPNO | ZF_BACKWARDS)) != 0;
  /* a cone or bucket knocked off: a plain zombie (a screen door lost, a football helmet...) */
  int look = k;
  if ((k == Z_CONE || k == Z_BUCKET) && z->armor <= 0) look = Z_NORMAL;
  if (k == Z_DOOR && z->shield <= 0) look = Z_NORMAL;
  if (look == Z_NORMAL || look == Z_CONE || look == Z_BUCKET || look == Z_FLAG) {
    if (in_pool(z)) {
      a = eating ? (look == Z_CONE ? A_DUCKY2_EAT : look == Z_BUCKET ? A_DUCKY3_EAT : A_DUCKY1_EAT)
                 : (look == Z_CONE ? A_DUCKY2_WALK : look == Z_BUCKET ? A_DUCKY3_WALK : A_DUCKY1_WALK);
      *frame = eating ? anim_frame_at(a, (uint32_t)z->anim) : walk_frame(z, a);
      return a;
    }
  }
  if (zombie_headless(z) && k != Z_BUNGEE) {
    a = z_headless[look];
    *frame = walk_frame(z, a);
    return a;
  }
  a = eating ? z_eat[look] : z_walk[look];
  switch (k) {
  case Z_POLE:
    if (z->vault > 0) {
      a = A_POLE_JUMP;
      *frame = (int)((168 - z->vault) / 168 * anim_frames(a));
      return a;
    }
    if (z->flags & ZF_POLE) a = A_POLE_RUN;
    break;
  case Z_NEWSPAPER:
    if (z->rage > 0) {
      a = A_NEWSPAPER_GASP;
      *frame = (int)((150 - z->rage) / 150 * anim_frames(a));
      return a;
    }
    if (z->flags & ZF_PAPER_GONE) a = eating ? A_NEWSPAPER_MAD_EAT : A_NEWSPAPER_MAD_WALK;
    break;
  case Z_FOOTBALL:
    if (z->armor <= 0) a = eating ? A_FOOTBALL_BARE_EAT : A_FOOTBALL_BARE_WALK;
    break;
  case Z_DANCER:
    if (z->phase == DP_POINT) a = A_DANCER_SUMMON;
    break;
  case Z_SNORKEL:
    if (zombie_submerged(z)) a = A_SNORKEL_SWIM;
    break;
  case Z_DOLPHIN:
    if (z->vault > 0) {
      a = A_DOLPHIN_JUMP;
      *frame = (int)((120 - z->vault) / 120 * anim_frames(a));
      return a;
    }
    if (z->flags & ZF_DOLPHIN) a = in_pool(z) ? A_DOLPHIN_RIDE : A_DOLPHIN_WALK;
    else if (in_pool(z)) a = eating ? A_DOLPHIN_EAT : A_DOLPHIN_SWIM;
    break;
  case Z_JACK:
    if (z->flags & ZF_OPENING) {
      a = A_JACK_OPEN;
      *frame = (int)((110 - z->u.jack.in) * anim_frames(a) / 110);
      return a;
    }
    break;
  case Z_BALLOON:
    if (z->pop > 0) {
      a = A_BALLOON_POP;
      *frame = (50 - z->pop) * anim_frames(a) / 50;
      return a;
    }
    if (z->flags & ZF_FLYING) a = A_BALLOON_FLY;
    break;
  case Z_DIGGER:
    if (z->flags & ZF_UNDERGROUND) a = A_DIGGER_DIG;
    else if (z->emerge > 0) {
      a = A_DIGGER_RISE;
      int left = z->emerge > 230 ? 230 : z->emerge;
      *frame = (230 - left) * anim_frames(a) / 230;
      return a;
    }
    break;
  case Z_POGO:
    if (z->flags & ZF_POGO) a = A_POGO_BOUNCE;
    break;
  case Z_BUNGEE:
    a = z->phase == BP_WAIT ? A_BUNGEE_IDLE : A_BUNGEE_DROP;
    break;
  case Z_LADDER:
    if (z->ladder_in > 0) {
      a = A_LADDER_PLACE;
      *frame = (100 - z->ladder_in) * anim_frames(a) / 100;
      return a;
    }
    if (z->shield <= 0) a = eating ? A_LADDER_EAT : A_LADDER_BARE;
    break;
  case Z_CATAPULT:
    a = z->phase == CP_SHOOT ? A_CATAPULT_SHOOT : z->phase == CP_RELOAD ? A_CATAPULT_IDLE : A_CATAPULT_WALK;
    if (z->phase == CP_SHOOT) {
      *frame = (300 - z->u.catapult.left) * anim_frames(a) / 130;
      if (*frame >= anim_frames(a)) *frame = anim_frames(a) - 1;
      return a;
    }
    break;
  case Z_GARGANTUAR: {
    bool bare = z->u.giant.thrown != 0;
    if (z->phase == GP_SMASH) {
      a = bare ? A_GARGANTUAR_BARE_SMASH : A_GARGANTUAR_SMASH;
      *frame = (int)((134 - z->u.giant.left) / 134 * anim_frames(a));
      return a;
    }
    if (z->phase == GP_THROW) {
      a = A_GARGANTUAR_THROW;
      *frame = (int)((138 - z->u.giant.left) / 138 * anim_frames(a));
      return a;
    }
    a = bare ? A_GARGANTUAR_BARE_WALK : A_GARGANTUAR_WALK;
    break;
  }
  case Z_ZOMBONI:
    a = z->health < 1350 / 3 ? A_ZOMBONI_CRITICAL : z->health < 1350 * 2 / 3 ? A_ZOMBONI_DAMAGED : A_ZOMBONI_WALK;
    *frame = (int)(z->anim / 8) % anim_frames(a);
    return a;
  case Z_IMP:
    if (z->u.flight.left > 0) {
      a = A_IMP_THROWN;
      *frame = (z->u.flight.duration - z->u.flight.left) * anim_frames(a) / (z->u.flight.duration + 1);
      return a;
    }
    if (z->landing > 0) {
      a = A_IMP_LAND;
      *frame = (21 - z->landing) * anim_frames(a) / 21;
      return a;
    }
    break;
  }
  if (eating || a == A_DANCER_SUMMON || a == A_BALLOON_FLY || a == A_BUNGEE_IDLE || a == A_BUNGEE_DROP ||
      a == A_SNORKEL_SWIM || a == A_DIGGER_DIG || a == A_CATAPULT_IDLE || a == A_POGO_BOUNCE || a == A_DOLPHIN_RIDE)
    *frame = anim_frame_at(a, (uint32_t)z->anim);
  else
    *frame = walk_frame(z, a);
  return a;
}

static int zombie_tint(const Zombie *z) {
  if (B.ticks - z->hit_at < 10) return TINT_HIT;
  if (z->frozen > 0) return TINT_FROZEN;
  if (z->flags & ZF_HYPNO) return TINT_HYPNO;
  if (z->chill > 0) return TINT_CHILL;
  return TINT_NONE;
}

static void draw_zombie(const Zombie *z) {
  float x = z->x, y = row_y(z->row, z->x) + 1;
  int frame, flip, a = zombie_now(z, &frame, &flip);
  int tint = zombie_tint(z);
  int sc = z->flags & ZF_MINI ? 128 : 256;
  /* where it is off the ground */
  if (z->kind == Z_IMP && z->u.flight.left > 0) {
    float t = 1 - (float)z->u.flight.left / z->u.flight.duration;
    y -= t * (1 - t) * 4 * 120;
  }
  if (z->kind == Z_BUNGEE) {
    float up = z->phase == BP_DOWN ? z->u.bungee.left * 8.0f : z->phase == BP_UP ? (100 - z->u.bungee.left) * 8.0f : 0;
    float ty = row_y(z->row, x) - 15;
    if (z->phase != BP_UP || z->u.bungee.left > 50) gfx_image(I_BUNGEE_TARGET, SX(x), SY(ty));
    y -= up;
    /* the cord, up to the sky */
    int cx = SX(x), top = SY(y - 80);
    for (int cy = top - 26; cy > -26; cy -= 26) gfx_image(I_BUNGEE_CORD, cx - 1, cy);
  }
  if (z->kind == Z_BOBSLED && z->u.sled.sled && !z->u.sled.leader)
    gfx_image(I_SLED, SX(x + 70), SY(row_y(z->row, x) + 10));
  if (zombie_flying(z)) y -= 0;
  /* rising from a grave or the water: only what is above the ground */
  if (z->rise > 0) {
    int full = z->kind == Z_BACKUP ? 150 : 150;
    int ground = SY(row_y(z->row, x));
    int h = 80 * (full - z->rise) / full;
    gfx_clip(0, 0, SCREEN_W, ground + 2);
    y += (float)(80 - h) * 2.5f * (sc / 256.0f);
  }
  if (in_pool(z) && z->kind != Z_SNORKEL && z->kind != Z_DOLPHIN && z->kind != Z_BALLOON) y += 0;
  if (!zombie_flying(z) && !(z->flags & ZF_UNDERGROUND) && z->kind != Z_BUNGEE && !in_pool(z))
    gfx_ellipse(SX(x), SY(row_y(z->row, x)), sc == 256 ? 11 : 6, 3, 0, 9);
  gfx_anim_scaled(a, frame, SX(x), SY(y), sc, sc, flip ? GFX_FLIP : 0, tint, 32);
  gfx_clip_all();
  if (z->butter > 0) gfx_image(I_BUTTER, SX(x) + (flip ? 2 : -2), SY(y) - (sc == 256 ? 46 : 23));
}

/* the dead: their animation played once (fx_corpse) */
Fx *fx_corpse(const Zombie *z, int burn, int life, int burnt) {
  int k = z->kind;
  if ((k == Z_CONE || k == Z_BUCKET || k == Z_FLAG || k == Z_NORMAL) && in_pool(z)) k = Z_DUCKY;
  int a = burn ? z_burn[k] : z_die[k];
  if (k == Z_GARGANTUAR && z->u.giant.thrown) a = A_GARGANTUAR_BARE_DIE;
  if (k == Z_SNORKEL && zombie_submerged(z)) return NULL;
  if (k == Z_BUNGEE && z->phase != BP_WAIT) return NULL;
  fx_add(FX_CORPSE, z->x, row_y(z->row, z->x) + 1, life, z->row);
  Fx *f = &B.fx[B.nfx - 1];
  if (f->kind != FX_CORPSE) return NULL;
  f->anim = (uint16_t)a;
  f->flags = (uint8_t)((z->flags & (ZF_HYPNO | ZF_BACKWARDS) ? 1 : 0) | (z->flags & ZF_MINI ? 2 : 0) | (burnt ? 4 : 0));
  f->tint = (int16_t)(burn && a == z_die[k] ? TINT_BLACK : TINT_NONE);
  return f;
}

static void draw_corpse(const Fx *f) {
  int n = anim_frames(f->anim), sc = f->flags & 2 ? 128 : 256;
  int fade = f->life - 30;
  int frame = f->t * n / (fade > 10 ? fade : 10);
  if (frame >= n) frame = n - 1;
  int alpha = f->t < fade ? 32 : 32 * (f->life - f->t) / 30;
  int sy = 256;
  if (f->flags & 8) sy = 256 - 200 * (f->t < 20 ? f->t : 20) / 20;   /* mowed: flattened */
  gfx_anim_scaled(f->anim, frame, SX(f->x), SY(f->y), sc, sy * sc / 256, f->flags & 1 ? GFX_FLIP : 0, f->tint, alpha);
}

/* ---------------------------------------------------------------- shots, sun and coins */

static void draw_shot(const Shot *s) {
  static const uint16_t img[] = {I_PEA, I_SNOW_PEA, 0, I_PUFF, I_SPIKE, I_STAR, I_CABBAGE, I_KERNEL, I_BUTTER, I_MELON,
                                 I_BASKETBALL};
  int x = SX(s->x), y = SY(s->y - (s->flags & SF_HIGH ? 72 : 0));
  if (s->kind < S_CABBAGE && s->kind != S_STAR) {
    /* straight shots cast a shadow on the ground */
    gfx_ellipse(x, SY(row_y(s->row, s->x)) - 1, 4, 1, 0, 8);
  }
  if (s->kind == S_FIRE) gfx_anim(A_FIRE_PEA, (int)(B.ticks / 4 + s->id), x, y, s->vx < 0 ? GFX_FLIP : 0, TINT_NONE, 32);
  else gfx_image(img[s->kind], x, y);
}

void draw_sun(float x, float y, int value, int alpha) {
  int sc = value == 15 ? 170 : 256;
  gfx_anim_scaled(A_SUN, anim_frame_at(A_SUN, B.ticks), SX(x), SY(y), sc, sc, 0, TINT_NONE, alpha);
}

static void draw_coin(float x, float y, int value) {
  gfx_image(value >= 1000 ? I_COIN_DIAMOND : value >= 50 ? I_COIN_GOLD : I_COIN_SILVER, SX(x), SY(y));
}

/* ---------------------------------------------------------------- effects */

static void draw_fx(const Fx *f) {
  int x = SX(f->x), y = SY(f->y), t = f->t, life = f->life > 0 ? f->life : 1;
  switch (f->kind) {
  case FX_BOOM: {
    int r = 6 + 30 * t / life, a = 32 - 32 * t / life;
    gfx_ellipse(x, y, r + 4, r * 3 / 4 + 3, rgb565(0xFF3A10), a * 3 / 4);
    gfx_ellipse(x, y, r, r * 3 / 4, rgb565(0xFFB020), a);
    gfx_ellipse(x, y, r / 2, r * 3 / 8, rgb565(0xFFF5B0), a);
    if (t < life * 2 / 3)
      gfx_text(F_BIG, x, y + 6, f->v == 1 ? "SPUDOW!" : "POWIE!", rgb565(0xFFE14A), GFX_CENTER | GFX_OUTLINE);
    break;
  }
  case FX_DOOM: {
    int r = 10 + 60 * t / life, a = 32 - 28 * t / life;
    gfx_ellipse(x, y, r, r * 2 / 3, rgb565(0x3A1A4A), a);
    gfx_ellipse(x, y - r / 2, r * 2 / 3, r / 2, rgb565(0x6B3A80), a);
    gfx_ellipse(x, y - r, r / 2, r / 3, rgb565(0xB080C0), a * 2 / 3);
    break;
  }
  case FX_FIRE: {
    int n = anim_frames(A_JALAPENO_FIRE), frame = t * n / life;
    gfx_anim(A_JALAPENO_FIRE, frame, SX(BOARD_LEFT - 20), y, 0, TINT_NONE, 32);
    gfx_anim(A_JALAPENO_FIRE, frame, SX(BOARD_LEFT + 330), y, 0, TINT_NONE, 32);
    break;
  }
  case FX_FREEZE:
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, rgb565(0xD8F0FF), 20 - 20 * t / life);
    break;
  case FX_SPLAT: {
    uint16_t c = f->v == S_ICE ? rgb565(0xC8F0FF) : f->v == S_FIRE ? rgb565(0xFF9A20) : f->v == S_PUFF ? rgb565(0xD0A0E0)
               : f->v >= S_CABBAGE ? rgb565(0x9AD040) : rgb565(0x7AD030);
    int r = 2 + 3 * t / life;
    gfx_ellipse(x, y, r, r, c, 32 - 32 * t / life);
    break;
  }
  case FX_DIRT:
    gfx_ellipse(x - 4 - t / 6, y - t / 5, 3, 2, rgb565(0x6A4A2A), 32 - 32 * t / life);
    gfx_ellipse(x + 4 + t / 6, y - t / 5, 3, 2, rgb565(0x7A5A30), 32 - 32 * t / life);
    break;
  case FX_SPLASH:
    gfx_ellipse(x, y - t / 4, 6 + t / 8, 3, rgb565(0xE0F4FF), 28 - 28 * t / life);
    break;
  case FX_SMOKE: {
    uint16_t c = f->v == 2 ? rgb565(0xE070E0) : f->v == 3 ? rgb565(0xB49AD8) : rgb565(0xC8C0B0);
    int r = 4 + 8 * t / life;
    if (f->v == 3) {
      /* the fume cloud, over its lane */
      int n = anim_frames(A_FUME_CLOUD);
      gfx_anim(A_FUME_CLOUD, t * n / life, x, y + 12, 0, TINT_NONE, 32);
    } else
      gfx_ellipse(x, y - t / 4, r, r * 2 / 3, c, 24 - 24 * t / life);
    break;
  }
  case FX_POW:
    gfx_ellipse(x, y, 6 + t / 3, 4 + t / 4, C_WHITE, 28 - 28 * t / life);
    break;
  case FX_TEXT:
    break;   /* (the screen's messages: hud.c) */
  case FX_CORPSE:
    draw_corpse(f);
    break;
  }
}

/* ---------------------------------------------------------------- the lawn */

int left_fog_column(void) {
  if (B.env != ENV_FOG) return 99;
  if (B.level == 31) return 6;
  if (B.level <= 36) return 5;
  return 4;
}

/* the fog over the right of the lawn, cleared round planterns, blown off by a blover */
static uint8_t fog_cells[MAX_ROWS][COLS + 1];
static void fog_band(uint16_t *band, int y0, int y1, void *ctx) {
  (void)ctx;
  uint16_t fog = rgb565(0x8A96B0);
  for (int y = y0; y < y1; y++) {
    uint16_t *out = band + (y % GFX_BAND) * SCREEN_W;
    float gy = y / 0.4f;
    int row = row_at(gy, 440);
    if (row < 0) row = 0;
    if (row >= B.nrows) row = B.nrows - 1;
    for (int x = 0; x < SCREEN_W; x++) {
      float gx = x / 0.4f + cam_x;
      int col = col_at(gx);
      if (col < 0) continue;
      int d = col > COLS ? fog_cells[row][COLS] : fog_cells[row][col];
      /* (a soft edge on its left) */
      if (col <= COLS && col > 0) {
        float in = (gx - BOARD_LEFT) / CELL_W - col;
        int prev = fog_cells[row][col - 1];
        if (in < 0.5f) d = prev + (int)((d - prev) * (in + 0.5f));
      }
      if (!d) continue;
      int n = ((x * 7 + y * 13 + (int)(B.ticks / 8)) & 7) - 3;
      int a = d + n;
      a = a < 0 ? 0 : a > 32 ? 32 : a;
      out[x] = mix565(fog, out[x], a);
    }
  }
}

static void draw_fog(void) {
  int left = left_fog_column();
  if (left >= COLS) return;
  /* blown away: off to the right, then back */
  float blow = 0;
  if (B.fog_blown > 0) blow = B.fog_blown > 3800 ? (4000 - B.fog_blown) / 200.0f : B.fog_blown > 400 ? 1 : B.fog_blown / 400.0f;
  float shift = blow * 9;
  for (int r = 0; r < B.nrows; r++)
    for (int c = 0; c <= COLS; c++) {
      float fc = c - shift;
      int d = fc >= left ? 26 : fc >= left - 1 ? 13 : 0;
      fog_cells[r][c] = (uint8_t)d;
    }
  /* planterns light their 3 x 3 (and a bit) */
  for (int i = 0; i < B.nplants; i++) {
    const Plant *p = &B.plants[i];
    if (p->kind != P_PLANTERN || p->health <= 0) continue;
    for (int r = p->row - 1; r <= p->row + 1; r++)
      for (int c = p->col - 1; c <= p->col + 1; c++)
        if (r >= 0 && r < B.nrows && c >= 0 && c <= COLS) fog_cells[r][c] = 0;
    for (int c = p->col - 2; c <= p->col + 2; c++)
      if (c >= 0 && c <= COLS && fog_cells[p->row][c]) fog_cells[p->row][c] /= 2;
  }
  gfx_custom(fog_band, NULL);
  /* and its clouds drifting over it */
  for (int r = 0; r < B.nrows; r++)
    for (int c = left; c <= COLS; c++) {
      if (!fog_cells[r][c]) continue;
      float x = cell_x(c) + shift * CELL_W + 20 * __builtin_sinf((B.ticks / 300.0f) + r * 1.7f + c);
      gfx_anim(I_FOG0 + (r * 3 + c) % 8, 0, SX(x), SY(BOARD_TOP + r * cell_h() + cell_h() / 2), 0, TINT_NONE, 14);
    }
}

void draw_lawn(bool hud_mower) {
  (void)hud_mower;
  /* on the ground: craters, graves, ice */
  for (int i = 0; i < B.ncraters; i++) {
    const Crater *c = &B.craters[i];
    float x = cell_x(c->col);
    gfx_anim(is_night() || B.has_boss ? I_CRATER1 : I_CRATER0, 0, SX(x), SY(row_y(c->row, x)), 0, TINT_NONE,
             c->left < 300 ? 32 * c->left / 300 : 32);
  }
  for (int r = 0; r < B.nrows; r++) {
    const Ice *ice = &B.ice[r];
    if (ice->left <= 0) continue;
    int a = ice->left < 200 ? 32 * ice->left / 200 : 32;
    for (float x = ice->x; x < 820; x += 75)
      gfx_anim(I_ICE_TRAIL, 0, SX(x), SY(row_y(r, x) - 22), 0, TINT_NONE, a);
  }
  if (bowling_line()) gfx_image(I_BOWLING_STRIPE, SX(BOARD_LEFT + 3 * CELL_W), SY(BOARD_TOP + 250));
  /* row by row, from the back */
  for (int r = 0; r < B.nrows; r++) {
    for (int i = 0; i < B.ngraves; i++)
      if (B.graves[i].row == r) {
        float x = cell_x(B.graves[i].col);
        gfx_image(I_GRAVE, SX(x), SY(row_y(r, x)) + 2);
      }
    for (int layer = 0; layer < 4; layer++)
      for (int i = 0; i < B.nplants; i++) {
        const Plant *p = &B.plants[i];
        if (p->row == r && p->health > 0 && plant_layer(p) == layer && layer < 2) draw_plant(p);
      }
    for (int i = 0; i < B.nvases; i++)
      if (B.vases[i].row == r) {
        const Vase *v = &B.vases[i];
        float x = cell_x(v->col);
        gfx_image(I_VASE0 + (v->green ? 1 : v->zombie ? 2 : 0), SX(x), SY(row_y(r, x)) + 2);
      }
    for (int i = 0; i < B.ndrops; i++)
      if (B.drops[i].row == r) {
        const Drop *d = &B.drops[i];
        float x = cell_x(d->col);
        int y = SY(row_y(r, x) - 40);
        if (d->left > 300 || (B.ticks / 10) % 2) {
          gfx_image(I_PACKET, SX(x) - 10, y - 14);
          gfx_image(I_PACKET_PEASHOOTER + d->kind, SX(x), y - 2);
        }
      }
    for (int i = 0; i < B.nladders; i++)
      if (B.ladders[i].row == r) {
        float x = cell_x(B.ladders[i].col) + 20;
        gfx_image(I_LADDER, SX(x), SY(row_y(r, x)));
      }
    /* the zombies of the row, the farthest first */
    int order[MAX_ZOMBIES], n = 0;
    for (int i = 0; i < B.nzombies; i++)
      if (B.zombies[i].row == r && B.zombies[i].health > 0) order[n++] = i;
    for (int a = 1; a < n; a++)
      for (int b = a; b > 0 && B.zombies[order[b]].x > B.zombies[order[b - 1]].x; b--) {
        int t = order[b];
        order[b] = order[b - 1], order[b - 1] = t;
      }
    for (int i = 0; i < B.nfx; i++)
      if (B.fx[i].kind == FX_CORPSE && B.fx[i].v == r) draw_fx(&B.fx[i]);
    for (int i = 0; i < n; i++) {
      const Zombie *z = &B.zombies[order[i]];
      if (!zombie_flying(z)) draw_zombie(z);
    }
    for (int i = 0; i < B.nplants; i++) {
      const Plant *p = &B.plants[i];
      if (p->row == r && p->health > 0 && plant_layer(p) >= 2) draw_plant(p);
    }
    for (int i = 0; i < B.nrollers; i++)
      if (B.rollers[i].row == r) {
        const Roller *o = &B.rollers[i];
        int a = o->kind == P_EXPLODENUT ? A_EXPLODENUT : A_WALLNUT_ROLL;
        gfx_anim(a, (int)(o->x / 8), SX(o->x), SY(o->y), 0, TINT_NONE, 32);
      }
    if (B.mowers[r].state < 2) {
      float x = B.mowers[r].x;
      int img = B.env == ENV_ROOF ? I_ROOF_CLEANER : water_row(r) ? I_POOL_CLEANER : I_MOWER;
      gfx_image(img, SX(x + 14), SY(row_y(r, x) + 5));   /* (the picture's middle: all of it on the screen) */
    }
    for (int i = 0; i < B.nshots; i++)
      if (B.shots[i].row == r && B.shots[i].kind != S_STAR && B.shots[i].kind < S_CABBAGE) draw_shot(&B.shots[i]);
  }
  /* above the rows: what flies */
  for (int i = 0; i < B.nzombies; i++)
    if (zombie_flying(&B.zombies[i]) && B.zombies[i].health > 0) draw_zombie(&B.zombies[i]);
  for (int i = 0; i < B.nshots; i++)
    if (B.shots[i].kind == S_STAR || B.shots[i].kind >= S_CABBAGE) draw_shot(&B.shots[i]);
  if (B.has_boss) boss_draw();
  for (int i = 0; i < B.nfx; i++)
    if (B.fx[i].kind != FX_CORPSE && B.fx[i].kind != FX_SUNFLY && B.fx[i].kind != FX_COINFLY) draw_fx(&B.fx[i]);
  draw_fog();
  for (int i = 0; i < B.ncoins; i++) draw_coin(B.coins[i].x, B.coins[i].y, B.coins[i].value);
  for (int i = 0; i < B.nsuns; i++) {
    const Sun *s = &B.suns[i];
    draw_sun(s->x, s->y, s->value, s->left < 200 ? 16 + 16 * (s->left / 10 % 2) : 32);
  }
}

/* the zombies waiting on the street before a level (its kinds, standing about) */
void draw_street(const uint8_t *kinds, int n) {
  for (int i = 0; i < n; i++) {
    int k = kinds[i];
    float x = 900 + (i * 53) % 230 + (i % 3) * 10, y = 160 + ((i * 97) % 5) * 85.0f + (i % 2) * 20;
    int a = z_walk[k];
    if (k == Z_BUNGEE) continue;
    if (k == Z_BALLOON) a = A_BALLOON_FLY;
    if (k == Z_POLE) a = A_POLE_RUN;
    if (k == Z_DIGGER) a = A_DIGGER_WALK;
    gfx_anim(a, (int)((B.ticks / 12 + i * 5) % anim_frames(a)), SX(x), SY(y), 0, TINT_NONE, 32);
  }
}
