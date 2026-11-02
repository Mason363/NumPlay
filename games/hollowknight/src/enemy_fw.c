/* The enemies of the second app, Hollow Knight: Fungal Wastes (HK_FW), each kind's FSM as the game has it. Part of enemy.c: its
 * helpers are theirs. */

/* a range of an enemy's by its name (ET_RANGE: a child's trigger), or NULL */
static const Ent *range_named(const Enemy *e, const char *name) {
  if (e->ent == NO_ENT) return NULL;
  const Ent *d = ent_at(e->ent);
  for (int i = 1; i <= d->s0; i++)
    if (d[i].type == ENT_BOX && d[i].flags == ET_RANGE && !strcmp(str_at(d[i].s0), name)) return &d[i];
  return NULL;
}
static bool hero_in_range(const Enemy *e, const char *name) {
  const Ent *r = range_named(e, name);
  return r && hero_in_box(e, r->x0, r->y0, r->x1, r->y1);
}

/* a place in an enemy's own units -> the world's */
static void enemy_at(const Enemy *e, float lx, float ly, float *x, float *y) {
  enemy_point(e, lx, ly, x, y);
  *x += e->body.x, *y += e->body.y;
}

/* the Knight in an enemy's Wake Region (EnemyWakeRegion: WAKE as he comes in, SLEEP as he leaves): +1 in, -1 out,
 * 0 as it was (b0: in) */
static int wake_region(Enemy *e) {
  bool in = hero_in_range(e, "Wake Region");
  int ev = in && !e->b0 ? 1 : !in && e->b0 ? -1 : 0;
  e->b0 = in;
  return ev;
}

/* IdleBuzzV2: IdleBuzz with a range of its own each way, about a start it is given */
static void idle_buzz_xy(Enemy *e, float wmin, float wmax, float vmax, float amax, float rx, float ry) {
  float vx = e->body.vx, vy = e->body.vy, x = e->body.x, y = e->body.y;
  if (y < e->start_y - ry) {
    if (vy < 0) e->ay = amax / 2000, vy /= 1.125f, e->wait = rand_range(wmin, wmax);
  } else if (y > e->start_y + ry && vy > 0)
    e->ay = -amax / 2000, vy /= 1.125f, e->wait = rand_range(wmin, wmax);
  if (x < e->start_x - rx) {
    if (vx < 0) e->ax = amax / 2000, vx /= 1.125f, e->wait = rand_range(wmin, wmax);
  } else if (x > e->start_x + rx && vx > 0)
    e->ax = -amax / 2000, vx /= 1.125f, e->wait = rand_range(wmin, wmax);
  if (e->wait <= 1.1920929e-7f) {
    e->ay = y < e->start_y - ry ? rand_range(0, amax) : y > e->start_y + ry ? rand_range(-amax, 0) : rand_range(-amax, amax);
    e->ax = x < e->start_x - rx ? rand_range(0, amax) : x > e->start_x + rx ? rand_range(-amax, 0) : rand_range(-amax, amax);
    e->ay /= 2000, e->ax /= 2000;
    e->wait = rand_range(wmin, wmax);
  }
  if (e->wait > 1.1920929e-7f) e->wait -= DT;
  vx += e->ax, vy += e->ay;
  e->body.vx = vx > vmax ? vmax : vx < -vmax ? -vmax : vx;
  e->body.vy = vy > vmax ? vmax : vy < -vmax ? -vmax : vy;
}

/* FaceDirection (sprite facing right): turns to its velocity, playing a clip, pausing between turns */
static void face_direction_r(Enemy *e, float *pause, int clip, float pause_time) {
  if (*pause > 0) {
    *pause -= DT;
    return;
  }
  float want = e->body.vx > 0 ? fabsf(e->sx) : -fabsf(e->sx);
  if (e->sx != want) {
    *pause = pause_time;
    set_scale_x(e, want);
    if (clip >= 0) anim_play_from_frame(&e->anim, clip, 0);
  }
}

/* ---------------------------------------------------------------- Funglings (Fungoon baby): asleep till the Knight
 * comes into their Wake Region, then buzzing about where they started, bumped off the walls they meet */
enum { FG_SLEEP, FG_IDLE };
/* (b0: the Knight in its Wake Region; b1: it met a wall this step; c0: which side, 0 right 2 left; pause0:
 * FaceDirection's) */
static void fungling_sleep(Enemy *e) {
  e->st = FG_SLEEP;
  e->body.vx = e->body.vy = 0;   /* (and kinematic: still) */
}

static void fungling_start(Enemy *e) {
  e->start_x = e->body.x, e->start_y = e->body.y;   /* (Init: Start Pos) */
  e->wait = 0, e->ax = e->ay = 0, e->b0 = e->b1 = false;
  fungling_sleep(e);
}

static void fungling_fixed(Enemy *e) {
  if (e->st == FG_SLEEP) e->body.vx = e->body.vy = 0;
  else idle_buzz_xy(e, 0.75f, 1, 2.5f, 20, 3, 1);
}

static void fungling_update(Enemy *e) {
  int w = wake_region(e);
  if (w < 0) {
    fungling_sleep(e);
    return;
  }
  if (e->st == FG_SLEEP) {
    if (w > 0) {
      /* Wake: Idle */
      anim_play(&e->anim, CLIP(e, R_IDLE));
      e->st = FG_IDLE;
    }
    return;
  }
  face_direction_r(e, &e->pause0, CLIP(e, R_A1), 0.5f);
  if (e->b1) {
    /* (CheckCollisionSideEnter: a wall right, LEFT; left, RIGHT) Bump: turned, pushed away from it */
    e->b1 = false;
    bool right = e->c0 == 2;
    set_scale_x(e, right ? fabsf(e->sx) : -fabsf(e->sx));
    anim_play_from_frame(&e->anim, CLIP(e, R_A1), 0);
    e->body.vx += (right ? 20 : -20) * DT;   /* (AddForce2d, its mass 1) */
  }
}

/* ---------------------------------------------------------------- Amblooms (Fung Crawler): a Walker, nothing more */
static void ambloom_fixed(Enemy *e) {
  if (e->wk_state == WK_WALKING) {
    const Ent *w = walker_rec(e);
    if (w) e->body.vx = e->wk_facing > 0 ? w->y0 : w->x0;
  }
}

/* ---------------------------------------------------------------- gas (Gas Projectile: Gas Control): flung, a circle
 * that grows for 0.75 s, hurting; then its particles die away, the circle gone. Its particles (Gas Attack: many small
 * puffs about it, shrinking as they go) drawn as one puff of their texture */
#define MAX_GAS 32
#define GAS_GROW 0.75f
#define GAS_FADE 0.35f   /* (its particles' lifetime) */
typedef struct {
  bool on;
  float x, y, vx, vy, t;
} Gas;
static Gas gas[MAX_GAS];

static void gas_fling(float x, float y, float speed, float deg) {
  for (int i = 0; i < MAX_GAS; i++)
    if (!gas[i].on) {
      float a = deg * (float)M_PI / 180;
      gas[i] = (Gas){true, x, y, cosf(a) * speed, sinf(a) * speed, 0};
      return;
    }
}

static float gas_scale(const Gas *g) { return 1 + 2 * (g->t < GAS_GROW ? g->t : GAS_GROW); }   /* (FloatAddV2, 2 a second) */

static void gas_tick(void) {
  for (int i = 0; i < MAX_GAS; i++) {
    Gas *g = &gas[i];
    if (!g->on) continue;
    if ((g->t += DT) >= GAS_GROW + 0.4f) {   /* (Stop: 0.4 s, then recycled) */
      g->on = false;
      continue;
    }
    /* (its circle meets the terrain: stopped there) */
    float dx = g->vx * DT, dy = g->vy * DT, l = sqrtf(dx * dx + dy * dy), r = 0.5f * gas_scale(g);
    if (l > 0 && phys_ray(g->x, g->y, dx / l, dy / l, l + r, CF_TERRAIN, NULL)) g->vx = g->vy = 0;
    else g->x += dx, g->y += dy;
  }
}

static int gas_touch(float x0, float y0, float x1, float y1, int *side) {
  for (int i = 0; i < MAX_GAS; i++) {
    const Gas *g = &gas[i];
    if (!g->on || g->t >= GAS_GROW) continue;
    float r = 0.5f * gas_scale(g), qx = g->x < x0 ? x0 : g->x > x1 ? x1 : g->x, qy = g->y < y0 ? y0 : g->y > y1 ? y1 : g->y;
    if ((qx - g->x) * (qx - g->x) + (qy - g->y) * (qy - g->y) < r * r) {
      *side = g->x > (x0 + x1) / 2 ? SIDE_RIGHT : SIDE_LEFT;
      return 1;
    }
  }
  return 0;
}

static void gas_draw(void) {
  uint8_t tint = gfx_dyn_tint(9, 255, 255, 255, 200);   /* (its particles' colour over their life: white, 0.78) */
  for (int i = 0; i < MAX_GAS; i++) {
    const Gas *g = &gas[i];
    if (!g->on) continue;
    /* (as wide as its particles spread, its shape's radius 0.75 at its scale, and their size; shrinking as they die) */
    float k = g->t < GAS_GROW ? 1 : 1 - (g->t - GAS_GROW) / GAS_FADE;
    if (k <= 0) continue;
    float d = (1.5f * gas_scale(g) + 1.0f) * k;
    Inst in;
    sprite_inst(SPRITE_GAS, g->x, g->y, -0.001f, d / 4, d / 4, tint, &in);   /* (its sprite 4 units wide) */
    gfx_actor_minor(&in, SORT_KEY(0, 0));
  }
}

/* ---------------------------------------------------------------- Fungoons (Fungus Flyer): asleep till the Knight
 * comes near; buzzing about, they spit gas down at him as he comes under them, a little to one side */
enum { FN_SLEEP, FN_IDLE, FN_ANTIC, FN_SPIT, FN_RECOVER, FN_CD };
/* (b0: in its Wake Region; qx, qy: its spit's angles; wait: a state's; t1: FlingObjectsFromGlobalPoolTime's timer;
 * c0: its spit effect on) */
static void fungoon_start(Enemy *e) {
  e->start_x = e->body.x, e->start_y = e->body.y;
  e->wait = 0, e->ax = e->ay = 0, e->b0 = false, e->c0 = 0;
  e->st = FN_SLEEP, e->body.vx = e->body.vy = 0;
}

static void fungoon_fixed(Enemy *e) {
  switch (e->st) {
    case FN_SLEEP: e->body.vx = e->body.vy = 0; break;
    case FN_IDLE: idle_buzz_xy(e, 0.5f, 0.75f, 4, 20, 3, 1); break;
    case FN_SPIT: e->body.vy += 1 * DT; break;   /* (AddForce2d up, its mass 1) */
    case FN_CD: idle_buzz_xy(e, 0.75f, 1, 2.5f, 20, 3, 1); break;
  }
}

static void fungoon_update(Enemy *e) {
  int w = wake_region(e);
  if (w < 0) {
    e->st = FN_SLEEP, e->c0 = 0;   /* (SLEEP: its spit effect off) */
    return;
  }
  bool done = (e->anim.events & ANIM_DONE) != 0;
  switch (e->st) {
    case FN_SLEEP:
      if (w > 0) anim_play(&e->anim, CLIP(e, R_IDLE)), e->st = FN_IDLE;   /* (Wake) */
      break;
    case FN_IDLE: {
      bool r = hero_in_range(e, "Attack R"), l = !r && hero_in_range(e, "Attack L");
      if (!r && !l) break;
      /* Attack Right, Left: facing that way, the spit's angles; Spit Antic */
      set_scale_x(e, r ? -1 : 1);
      e->qx = r ? 278 : 248, e->qy = r ? 292 : 262;
      anim_play_from_frame(&e->anim, CLIP(e, R_A1), 0);
      e->st = FN_ANTIC;
      break;
    }
    case FN_ANTIC:
      if (done) {
        /* Spit: gas a while, its effect at its mouth */
        anim_play_from_frame(&e->anim, CLIP(e, R_A2), 0);
        anim_play_from_frame(&e->sub, CLIP(e, R_A4), 0);
        e->st = FN_SPIT, e->wait = 1, e->c0 = 1;
      }
      break;
    case FN_SPIT:
      e->sub.events = 0;
      anim_update(&e->sub, DT);
      if ((e->t1 += DT) >= 0.05f) {
        e->t1 = 0;
        float x, y;
        enemy_at(e, -0.46f, -2.71f, &x, &y);   /* (Spawn Point) */
        gas_fling(x, y, rand_range(10, 11), rand_range(e->qx, e->qy));
      }
      if ((e->wait -= DT) <= 0) anim_play_from_frame(&e->anim, CLIP(e, R_A3), 0), e->st = FN_RECOVER, e->c0 = 0;
      break;
    case FN_RECOVER:
      if (done) {
        /* Spit CD: buzzing, facing left again, for a second */
        set_scale_x(e, 1);
        anim_play(&e->anim, CLIP(e, R_IDLE));
        e->st = FN_CD, e->wait = 1;
      }
      break;
    case FN_CD:
      if ((e->wait -= DT) <= 0) e->st = FN_IDLE;
      break;
  }
}

/* (its spit effect at its mouth: Spit Effect, on while it spits) */
static void fungoon_draw(const Enemy *e, float z, uint8_t tint) {
  if (e->st != FN_SPIT || !e->c0 || e->sub.sprite < 0) return;
  float x, y;
  enemy_at(e, -0.8231f, -3.8136f, &x, &y);
  Inst in;
  sprite_inst(e->sub.sprite, x, y, z - 0.001f, e->sx, 1, tint, &in);
  gfx_actor(&in, SORT_KEY(0, 0));
}

/* ---------------------------------------------------------------- Shrumelings (Shroom Mini): asleep in one of three
 * poses, they wake as the Knight comes near and run from him, bouncing off the walls */
enum { SM_SLEEPING, SM_WAKE, SM_STARTLE, SM_RUN_R, SM_RUN_L };
#define SM_ACCEL (0.13f * 1.2f)   /* (0.13 a frame of the game's 60 a second) */
/* (b0: in its Wake Region; ax: Scale; c0: its pose) */
static void shrumeling_start(Enemy *e) {
  /* Scale: 0.75 to 1, either way; Anim Pick */
  e->ax = rand_range(0.75f, 1);
  e->sx = rand_range(0, 1) < 0.5f ? -e->ax : e->ax;
  e->c0 = (uint8_t)rand_range(0, 2.999f);
  static const int16_t rest[3] = {CLIP_SHRUMELING_MINI_REST_1, CLIP_SHRUMELING_MINI_REST_2, CLIP_SHRUMELING_MINI_REST_3};
  anim_play(&e->anim, rest[e->c0]);
  e->st = SM_SLEEPING, e->b0 = false;
}

static void shrumeling_run(Enemy *e, bool right) {
  set_scale_x(e, right ? -e->ax : e->ax);
  anim_play(&e->anim, CLIP(e, R_A2));
  e->st = right ? SM_RUN_R : SM_RUN_L;
}

static void shrumeling_fixed(Enemy *e) {
  if (e->st != SM_RUN_R && e->st != SM_RUN_L) return;
  bool right = e->st == SM_RUN_R;
  float v = e->body.vx + (right ? SM_ACCEL : -SM_ACCEL);
  e->body.vx = v < -4.5f ? -4.5f : v > 4.5f ? 4.5f : v;
}

static void shrumeling_update(Enemy *e) {
  int w = wake_region(e);
  bool done = (e->anim.events & ANIM_DONE) != 0;
  switch (e->st) {
    case SM_SLEEPING:
      if (w > 0) {
        static const int16_t wake[3] = {CLIP_SHRUMELING_MINI_REST_TO_WAKE_1, CLIP_SHRUMELING_MINI_REST_TO_WAKE_2,
                                        CLIP_SHRUMELING_MINI_REST_TO_WAKE_3};
        anim_play_from_frame(&e->anim, wake[e->c0], 0);
        e->st = SM_WAKE;
      }
      break;
    case SM_WAKE:
      if (done) anim_play_from_frame(&e->anim, CLIP(e, R_A1), 0), e->st = SM_STARTLE;
      break;
    case SM_STARTLE:
      if (done) shrumeling_run(e, hero_x() <= e->body.x);   /* (Direction: away from the Knight) */
      break;
    case SM_RUN_R:
    case SM_RUN_L: {
      bool right = e->st == SM_RUN_R;
      if (right ? hero_x() > e->body.x : hero_x() < e->body.x) {
        shrumeling_run(e, !right);   /* (the Knight that way: the other) */
        break;
      }
      if (e->body.ncontacts && side_hit(e, right ? 0 : 2)) {
        /* Bump: off the wall, up */
        e->body.vx = right ? rand_range(-5, -2.5f) : rand_range(2.5f, 5), e->body.vy = 10;
      }
      break;
    }
  }
}

/* ---------------------------------------------------------------- globs (Vomit Glob): flung, falling; landed, a puddle
 * that hurts for a while, then shrinks away */
#define MAX_GLOBS 12
enum { GL_OFF, GL_AIR, GL_LANDED, GL_SHRINK };
typedef struct {
  uint8_t st;
  float x, y, vx, vy, scale, t;
  Anim anim;
} Glob;
static Glob globs[MAX_GLOBS];

static void glob_fling(float x, float y, float vx, float vy) {
  for (int i = 0; i < MAX_GLOBS; i++)
    if (globs[i].st == GL_OFF) {
      Glob *g = &globs[i];
      memset(g, 0, sizeof *g);
      /* Init: a scale of its own; Idle */
      g->st = GL_AIR, g->x = x, g->y = y, g->vx = vx, g->vy = vy, g->scale = rand_range(1, 1.75f);
      anim_play(&g->anim, CLIP_GLOB_IDLE);
      return;
    }
}

static void globs_tick(void) {
  for (int i = 0; i < MAX_GLOBS; i++) {
    Glob *g = &globs[i];
    if (g->st == GL_OFF) continue;
    g->anim.events = 0;
    anim_update(&g->anim, DT);
    if (g->st == GL_AIR) {
      g->vy += -60 * 0.7f * DT;   /* (its Rigidbody2D's gravity) */
      float hx = 0.295f * g->scale, hy = 0.305f * g->scale, cy = g->y + 0.37f * g->scale, dx = g->vx * DT, dy = g->vy * DT;
      PhysHit h;
      if (dx != 0 && phys_ray(g->x, cy, dx > 0 ? 1 : -1, 0, fabsf(dx) + hx, CF_SOLID, &h)) g->vx = 0, dx = 0;   /* (a wall) */
      if (dy < 0 && phys_ray(g->x, cy, 0, -1, -dy + hy, CF_SOLID, &h)) {
        /* Land: still, a little up; Land; its puddle hurting, for 1.25 to 1.75 s */
        g->y = h.y - (0.37f * g->scale - hy) + 0.25f;
        g->x += dx;
        g->st = GL_LANDED, g->t = rand_range(1.25f, 1.75f);
        anim_play_from_frame(&g->anim, CLIP_GLOB_LAND, 0);
        continue;
      }
      g->x += dx, g->y += dy;
      if (g->y < -10) g->st = GL_OFF;
    } else if (g->st == GL_LANDED) {
      if ((g->t -= DT) <= 0) g->st = GL_SHRINK, g->t = 0;
    } else if ((g->t += DT) >= 0.25f + 1)
      g->st = GL_OFF;   /* (Shrink: to a tenth in 0.25 s; Stop Steam: hidden, 1 s; Recycle) */
  }
}

static int globs_touch(float x0, float y0, float x1, float y1, int *side) {
  for (int i = 0; i < MAX_GLOBS; i++) {
    const Glob *g = &globs[i];
    float cx, cy, hx, hy;
    if (g->st == GL_AIR) cx = g->x, cy = g->y + 0.37f * g->scale, hx = 0.295f * g->scale, hy = 0.305f * g->scale;
    else if (g->st == GL_LANDED)   /* (its Puddle Box) */
      cx = g->x - 0.0107f * g->scale, cy = g->y + 0.09f * g->scale, hx = 0.6387f * g->scale, hy = 0.132f * g->scale;
    else
      continue;
    if (x1 > cx - hx && x0 < cx + hx && y1 > cy - hy && y0 < cy + hy) {
      *side = g->x > (x0 + x1) / 2 ? SIDE_RIGHT : SIDE_LEFT;
      return 1;
    }
  }
  return 0;
}

static void globs_draw(void) {
  for (int i = 0; i < MAX_GLOBS; i++) {
    const Glob *g = &globs[i];
    if (g->st == GL_OFF || g->anim.sprite < 0) continue;
    float k = g->scale;
    if (g->st == GL_SHRINK) {
      if (g->t >= 0.25f) continue;
      k *= 1 - 0.9f * (g->t / 0.25f);
    }
    Inst in;
    sprite_inst(g->anim.sprite, g->x, g->y, -0.004f, k, k, 0, &in);
    gfx_actor_minor(&in, SORT_KEY(0, 0));
  }
}

/* ---------------------------------------------------------------- Shrumal Ogres (Shroom Brawler): asleep till woken
 * (by their Wake Region, or by their arena), they roam about where they started, turning to face the Knight; every
 * second and a half or so they spit globs at him or, if he is near, smash the ground before them. Their cap blocks
 * the nail; awake, they are only hurt from the sides till they first attack, then only as they attack */
enum { OG_SLEEP, OG_WAKE, OG_IDLE, OG_TURN, OG_SPIT_ANTIC, OG_SPIT, OG_SPIT_CD, OG_BOUNCE, OG_SMASH_ANTIC, OG_SMASHING,
       OG_SMASH_RECOVER };
#define OGF_RIGHT 1    /* (ET_VARS a: Facing Right; its Wake Region on) */
#define OGF_REGION 2
/* (b0: in its Wake Region; b1: Facing Right; ax, ay: Min X, Max X; qx: Move Speed; wait: Move Time's; t0: Wait Timer;
 * t1: Wait Time; c0: its cap on) */
static void ogre_set_speeds(Enemy *e, bool right) {
  e->b1 = right;
  set_scale_x(e, right ? -1 : 1);
}

static void ogre_start(Enemy *e) {
  const Ent *v = enemy_rec(e, ET_VARS);
  /* Init: Min X, Max X its Roam Distance (2) about it; Facing Right?; Sleep: its cap off, its collider off */
  e->ax = e->body.x - 2, e->ay = e->body.x + 2;
  ogre_set_speeds(e, v && (v->a & OGF_RIGHT));
  e->st = OG_SLEEP, e->c0 = 0, e->b0 = false;
  e->flags |= 1;
  anim_play(&e->anim, CLIP(e, R_A7));
}

static void ogre_wake(Enemy *e) {
  /* Wake: its collider and cap on, falling; invincible from above; Wake */
  e->flags &= (uint8_t)~1;
  e->c0 = 1;
  e->body.gravity_scale = 1;
  e->flags |= 4, e->inv_dir = 4;
  anim_play_from_frame(&e->anim, CLIP(e, R_A1), 0);
  e->st = OG_WAKE;
}

static void ogre_set_wait(Enemy *e) {
  e->t1 = rand_range(1.5f, 1.75f), e->t0 = 0;   /* Set Wait Time */
}

static void ogre_idle(Enemy *e) {
  /* Idle: Run, its Move Speed, for its Move Time */
  anim_play(&e->anim, CLIP(e, R_IDLE));
  e->body.vx = e->qx;
  e->st = OG_IDLE;
}

static void ogre_change_movement(Enemy *e) {
  /* Change Movement: a Move Time, a Move Speed; RIGHT: on at once; else the other way, a new wait */
  e->wait = rand_range(1, 2), e->qx = rand_range(1, 3);
  if (rand_range(0, 1) >= 0.5f) {
    e->qx = -e->qx;
    ogre_set_wait(e);
  }
  ogre_idle(e);
}

static void ogre_invincible(Enemy *e, bool on) {
  /* (SetInvincible: from every way; SetCollider of its Cap Hit with it) */
  if (on) e->flags |= 4, e->inv_dir = 0, e->c0 = 1;
  else e->flags &= (uint8_t)~4, e->c0 = 0;
}

static void ogre_fixed(Enemy *e) {
  if (e->st == OG_SLEEP) e->body.vx = e->body.vy = 0;
  else if (e->st == OG_SMASHING) e->body.vx = e->b1 ? 0.85f : -0.85f;   /* (Smash Speed, every frame) */
}

static void ogre_update(Enemy *e) {
  const Ent *v = enemy_rec(e, ET_VARS);
  bool done = (e->anim.events & ANIM_DONE) != 0, blocked = (e->rc_flags & RF_BLOCKED) != 0;
  if (e->st == OG_SLEEP) {
    if (v && (v->a & OGF_REGION) && wake_region(e) > 0) ogre_wake(e);
    return;
  }
  bool hero_left = hero_x() < e->body.x;
  switch (e->st) {
    case OG_WAKE:
      if (done) ogre_change_movement(e);
      break;
    case OG_IDLE:
      if (blocked) {
        anim_play_from_frame(&e->anim, CLIP(e, R_A6), 0), e->st = OG_BOUNCE;   /* Bounce */
        break;
      }
      if ((e->wait -= DT) <= 0) {
        ogre_change_movement(e);   /* (CHANGE DIR) */
        break;
      }
      if (e->body.x > e->ay) {
        /* Return Left, Right: back the other way */
        e->wait = rand_range(1, 2), e->qx = -rand_range(1, 3);
        ogre_idle(e);
        break;
      }
      if (e->body.x < e->ax) {
        e->wait = rand_range(1, 2), e->qx = rand_range(1, 3);
        ogre_idle(e);
        break;
      }
      if (e->b1 == hero_left) {
        /* Turn L, R: to the Knight */
        ogre_set_speeds(e, !hero_left);
        anim_play_from_frame(&e->anim, CLIP(e, R_TURN), 0);
        e->st = OG_TURN;
        break;
      }
      if ((e->t0 += DT) > e->t1) {
        /* Spit Antic: too far, given up; near, Smash Antic; else still, open to the nail, Spit */
        e->t1 = rand_range(1.25f, 1.5f);
        float dx = hero_x() - e->body.x, dy = hero_y() - e->body.y;
        if (dx * dx + dy * dy > 25 * 25) {
          ogre_set_wait(e);
          ogre_idle(e);
        } else if (hero_in_range(e, "Smash Region")) {
          anim_play_from_frame(&e->anim, CLIP(e, R_A3), 0);
          e->body.vx = 0;
          e->st = OG_SMASH_ANTIC;
        } else {
          ogre_invincible(e, false);
          e->body.vx = 0;
          anim_play_from_frame(&e->anim, CLIP(e, R_A2), 0);
          e->t0 = 0;
          e->st = OG_SPIT_ANTIC;
        }
      }
      break;
    case OG_TURN:
      e->t0 += DT;
      if (blocked) anim_play_from_frame(&e->anim, CLIP(e, R_A6), 0), e->st = OG_BOUNCE;
      else if (done) ogre_idle(e);
      break;
    case OG_SPIT_ANTIC:
      if (e->anim.events & ANIM_TRIGGER) {
        /* Spit: invincible again; 2 to 4 globs at the Knight, up */
        ogre_invincible(e, true);
        float t = (hero_x() - e->body.x) * 0.8f, lo = e->b1 ? 2.5f : -13, hi = e->b1 ? 13 : -2.5f;
        t = t < lo ? lo : t > hi ? hi : t;
        float x, y;
        enemy_at(e, v ? v->p0 : -1.95f, v ? v->p1 : 0.04f, &x, &y);
        for (int n = (int)rand_range(2, 4.999f); n > 0; n--) glob_fling(x, y, rand_range(t - 1.5f, t + 1.5f), rand_range(23, 26));
        e->st = OG_SPIT;
      }
      break;
    case OG_SPIT:
      if (blocked) anim_play_from_frame(&e->anim, CLIP(e, R_A6), 0), e->st = OG_BOUNCE;
      else if (done) anim_play(&e->anim, CLIP(e, R_IDLE)), e->wait = 1, e->st = OG_SPIT_CD;   /* Spit CD */
      break;
    case OG_SPIT_CD:
      if (blocked) anim_play_from_frame(&e->anim, CLIP(e, R_A6), 0), e->st = OG_BOUNCE;
      else if ((e->wait -= DT) <= 0) {
        e->wait = 0;   /* (Idle's Move Time: the last, run out) */
        ogre_idle(e);
      }
      break;
    case OG_BOUNCE:
      if (done) ogre_idle(e);
      break;
    case OG_SMASH_ANTIC:
      if (done) {
        /* Smashing: its Smash Hits on, slowly forward, open to the nail, for 2.25 s or till the Knight is behind */
        mk_hitbox(e, STR_SMASH_HITS, true);
        anim_play_from_frame(&e->anim, CLIP(e, R_A4), 0);
        ogre_invincible(e, false);
        e->wait = 2.25f;
        e->st = OG_SMASHING;
      }
      break;
    case OG_SMASHING:
      if ((e->wait -= DT) <= 0 || e->b1 == hero_left) {
        /* Smash Recover */
        mk_hitbox(e, STR_SMASH_HITS, false);
        ogre_invincible(e, true);
        anim_play_from_frame(&e->anim, CLIP(e, R_A5), 0);
        e->body.vx = 0;
        e->st = OG_SMASH_RECOVER;
      }
      break;
    case OG_SMASH_RECOVER:
      if (done) ogre_idle(e);
      break;
  }
}

/* its Cap Hit: the nail meeting it blocked, the Knight bounced off it */
static int ogres_nail(const float *pts, int npts, float direction) {
  int out = 0;
  for (int i = 0; i < MAX_ENEMIES; i++) {
    Enemy *e = &en[i];
    if (e->mode != EM_ALIVE || FSM(e) != EF_OGRE || !e->c0) continue;
    const Ent *v = enemy_rec(e, ET_VARS);
    if (!v || v->x1 <= 0) continue;
    float cx, cy;
    enemy_at(e, v->x0, v->y0, &cx, &cy);
    if (!box_meets_shape(cx - v->x1, cy - v->y1, cx + v->x1, cy + v->y1, pts, npts)) continue;
    out |= HB_BOUNCE | HB_RECOIL;
    if (swing_bits >> i & 1) continue;
    swing_bits |= 1u << i;
    int dir = cardinal(direction);
    if (dir == 0) hero_recoil_left();
    else if (dir == 2) hero_recoil_right();
    FREEZE_MOMENT_1();
  }
  return out;
}

void enemies_wake(int wave) {
  for (int i = 0; i < MAX_ENEMIES; i++) {
    Enemy *e = &en[i];
    if (e->mode != EM_ALIVE || FSM(e) != EF_OGRE || e->st != OG_SLEEP) continue;
    const Ent *v = enemy_rec(e, ET_VARS);
    if (v && (int)v->p2 == wave) ogre_wake(e);
  }
}

int enemies_wave_hp(int wave) {
  for (int i = 0; i < MAX_ENEMIES; i++) {
    const Enemy *e = &en[i];
    if (e->mode != EM_ALIVE || FSM(e) != EF_OGRE) continue;
    const Ent *v = enemy_rec(e, ET_VARS);
    if (v && (int)v->p2 == wave) return e->hp;
  }
  return -1;
}

/* ---------------------------------------------------------------- Spore Bombs: flung, they chase the Knight a while,
 * then swell and burst in a Gas Explosion that hurts all about; the nail or the Knight touching one bursts it at once */
#define MAX_BOMBS 6
#define BOMB_R (0.19f * 1.5f)
enum { BB_OFF, BB_WAIT, BB_CHASE, BB_READY, BB_BOOM };
typedef struct {
  uint8_t st;
  float x, y, vx, vy, t, ang;
  Anim anim, lines;
} Bomb;
static Bomb bombs[MAX_BOMBS];

static void bomb_fire(float x, float y, float deg) {
  for (int i = 0; i < MAX_BOMBS; i++)
    if (bombs[i].st == BB_OFF) {
      Bomb *b = &bombs[i];
      memset(b, 0, sizeof *b);
      float a = deg * (float)M_PI / 180;
      b->st = BB_WAIT, b->x = x, b->y = y, b->vx = cosf(a) * 5.5f, b->vy = sinf(a) * 5.5f, b->t = 0.3f;
      anim_play(&b->anim, CLIP_SPORG_IDLE_2);
      return;
    }
}

static void bomb_explode(Bomb *b) {
  /* Explode: a Gas Explosion there (its collider on for 0.5 s, its flash fading in 1 s), a shake; the bomb gone */
  b->st = BB_BOOM, b->t = 0, b->vx = b->vy = 0;
  anim_play_from_frame(&b->lines, CLIP_FX_IMPACT_LINES, 0);
  cam_shake(SHAKE_AVERAGE);
}

static void bombs_tick(void) {
  for (int i = 0; i < MAX_BOMBS; i++) {
    Bomb *b = &bombs[i];
    if (b->st == BB_OFF) continue;
    b->anim.events = 0, b->lines.events = 0;
    anim_update(&b->anim, DT);
    if (b->st == BB_BOOM) {
      anim_update(&b->lines, DT);
      if ((b->t += DT) >= 1.5f) b->st = BB_OFF;   /* (Collider End: 1 s more, then Recycle) */
      continue;
    }
    if (b->st == BB_CHASE) {
      /* ChaseObjectV2: a force towards the Knight, the speed held to 5 */
      float dx = hero_x() - b->x, dy = hero_y() - b->y, l = sqrtf(dx * dx + dy * dy);
      if (l > 1) dx /= l, dy /= l;
      float sp = sqrtf(b->vx * b->vx + b->vy * b->vy);
      if (sp > 5) b->vx *= 5 / sp, b->vy *= 5 / sp;
      b->vx += dx * 8 * DT, b->vy += dy * 8 * DT;
    } else if (b->st == BB_READY)
      b->vx *= 0.95f, b->vy *= 0.95f;   /* (DecelerateV2) */
    /* (ObjectBounce: off the terrain at its speed) */
    float dx = b->vx * DT, dy = b->vy * DT;
    if (dx != 0 && phys_ray(b->x, b->y, dx > 0 ? 1 : -1, 0, fabsf(dx) + BOMB_R, CF_SOLID, NULL)) b->vx = -b->vx, dx = 0;
    if (dy != 0 && phys_ray(b->x, b->y, 0, dy > 0 ? 1 : -1, fabsf(dy) + BOMB_R, CF_SOLID, NULL)) b->vy = -b->vy, dy = 0;
    b->x += dx, b->y += dy;
    b->ang += b->vx * -50 * DT;   /* (Rotate By Speed) */
    if ((b->t -= DT) > 0) continue;
    if (b->st == BB_WAIT) b->st = BB_CHASE, b->t = 2.9f;
    else if (b->st == BB_CHASE) {
      /* Ready: slowing, swelling */
      b->st = BB_READY, b->t = 0.6f;
      anim_play_from_frame(&b->anim, CLIP_SPORG_EXPLODE, 0);
    } else
      bomb_explode(b);
  }
}

static int bombs_touch(float x0, float y0, float x1, float y1, int *side) {
  for (int i = 0; i < MAX_BOMBS; i++) {
    Bomb *b = &bombs[i];
    if (b->st == BB_CHASE && x1 > b->x - BOMB_R && x0 < b->x + BOMB_R && y1 > b->y - BOMB_R && y0 < b->y + BOMB_R)
      bomb_explode(b);   /* (the HeroBox: COLLIDE) */
    if (b->st != BB_BOOM || b->t >= 0.5f) continue;
    /* (its circle, 3 across) */
    float qx = b->x < x0 ? x0 : b->x > x1 ? x1 : b->x, qy = b->y < y0 ? y0 : b->y > y1 ? y1 : b->y;
    if ((qx - b->x) * (qx - b->x) + (qy - b->y) * (qy - b->y) < 9) {
      *side = b->x > (x0 + x1) / 2 ? SIDE_RIGHT : SIDE_LEFT;
      return 2;
    }
  }
  return 0;
}

static int bombs_nail(const float *pts, int npts) {
  int out = 0;
  for (int i = 0; i < MAX_BOMBS; i++) {
    Bomb *b = &bombs[i];
    if ((b->st == BB_WAIT || b->st == BB_CHASE) && box_meets_shape(b->x - BOMB_R, b->y - BOMB_R, b->x + BOMB_R, b->y + BOMB_R, pts, npts))
      bomb_explode(b), out |= HB_RECOIL;   /* (the Attack layer: COLLIDE) */
  }
  return out;
}

static void bombs_draw(void) {
  for (int i = 0; i < MAX_BOMBS; i++) {
    const Bomb *b = &bombs[i];
    Inst in;
    if (b->st == BB_BOOM) {
      /* its orange flash fading out over a second, its twelve Impact Lines about it */
      int al = (int)(255 * (1 - b->t)) & ~15;
      if (al > 0) {
        sprite_inst(SPRITE_GAS_FLASH, b->x, b->y, -0.01f, 1, 1, gfx_dyn_tint(12 + i % 4, 255, 255, 255, (uint8_t)al), &in);
        gfx_actor_minor(&in, SORT_KEY(0, 0));
      }
      if (b->lines.playing && b->lines.sprite >= 0) {
        static const int16_t deg[12] = {0, 30, 60, 91, 124, 156, 189, 215, 242, 278, 310, 340};
        for (int k = 0; k < 12; k++) {
          sprite_inst_rot(b->lines.sprite, b->x, b->y, -0.011f, 1, 1, deg[k], 0, &in);
          gfx_actor_minor(&in, SORT_KEY(0, 0));
        }
      }
      continue;
    }
    if (b->st == BB_OFF || b->anim.sprite < 0) continue;
    sprite_inst_rot(b->anim.sprite, b->x, b->y, -0.004f, 1.5f, 1.5f, b->ang, 0, &in);
    gfx_actor_minor(&in, SORT_KEY(0, 0));
  }
}

/* ---------------------------------------------------------------- Sporgs (Shroom Turret): asleep in the wall till the
 * Knight comes near; then every 3.5 to 4.25 s a Spore Bomb out the way they face, while he is about */
enum { SG_DORMANT, SG_WAKE, SG_IDLE, SG_ANTIC, SG_FIRE, SG_AWAY };
/* (b0: the Knight in its Hero Detect; wait: Idle's) */
static void sporg_start(Enemy *e) {
  e->st = SG_DORMANT, e->b0 = false;
  e->flags |= 1;   /* (its collider off) */
  anim_play(&e->anim, CLIP(e, R_A3));
}

static void sporg_idle(Enemy *e, float t0, float t1) {
  anim_play(&e->anim, CLIP(e, R_IDLE));
  e->wait = rand_range(t0, t1), e->st = SG_IDLE;
}

/* the Knight in its Hero Detect: its box in its own axes, it turned as it is (its Fire Angle less 90) */
static bool sporg_detect(const Enemy *e) {
  const Ent *r = range_named(e, "Hero Detect"), *v = enemy_rec(e, ET_VARS);
  if (!r || !v || g_hero.hidden) return false;
  float a = (v->p0 - 90) * (float)M_PI / 180, c = cosf(a), s = sinf(a);
  float dx = g_hero.body.x + g_hero.body.ox - e->body.x, dy = g_hero.body.y + g_hero.body.oy - e->body.y;
  float lx = dx * c + dy * s, ly = -dx * s + dy * c;
  return fabsf(lx - r->x0) < r->x1 + g_hero.body.hx && fabsf(ly - r->y0) < r->y1 + g_hero.body.hy;
}

static void sporg_update(Enemy *e) {
  bool in = sporg_detect(e), enter = in && !e->b0, leave = !in && e->b0;
  e->b0 = in;
  bool done = (e->anim.events & ANIM_DONE) != 0;
  if (leave) {
    /* (HERO EXIT, from anywhere) Hero Away */
    anim_play(&e->anim, CLIP(e, R_IDLE));
    e->st = SG_AWAY;
    return;
  }
  switch (e->st) {
    case SG_DORMANT:
      if (enter) {
        /* Wake: Sleep to Idle, its collider on */
        anim_play_from_frame(&e->anim, CLIP(e, R_A1), 0);
        e->flags &= (uint8_t)~1;
        e->st = SG_WAKE;
      }
      break;
    case SG_WAKE:
      if (done) sporg_idle(e, 0.5f, 0.5f);   /* First Idle */
      break;
    case SG_AWAY:
      if (enter) sporg_idle(e, 0.5f, 0.5f);
      break;
    case SG_IDLE:
      if ((e->wait -= DT) <= 0) anim_play_from_frame(&e->anim, CLIP(e, R_A2), 0), e->st = SG_ANTIC;   /* Attack Antic */
      break;
    case SG_ANTIC:
      if (e->anim.events & ANIM_TRIGGER) {
        /* Fire: a Spore Bomb from its Spawn Point, at its Fire Angle */
        const Ent *v = enemy_rec(e, ET_VARS);
        if (v) bomb_fire(e->body.x + v->p1, e->body.y + v->p2, v->p0);
        e->st = SG_FIRE;
      }
      break;
    case SG_FIRE:
      if (done) sporg_idle(e, 3.5f, 4.25f);
      break;
  }
}

/* ---------------------------------------------------------------- the kinds' hooks (enemy.c) */
static bool xl_wants_contacts(const Enemy *e) { return FSM(e) == EF_FUNGLING; }
static void xl_enter(void) {
  memset(gas, 0, sizeof gas);
  memset(globs, 0, sizeof globs);
  memset(bombs, 0, sizeof bombs);
}
static void xl_tick(void) {
  gas_tick();
  globs_tick();
  bombs_tick();
}
static void xl_draw(void) {
  gas_draw();
  globs_draw();
  bombs_draw();
}
static int xl_touch(float x0, float y0, float x1, float y1, int *side) {
  int d = gas_touch(x0, y0, x1, y1, side);
  if (!d) d = globs_touch(x0, y0, x1, y1, side);
  return d ? d : bombs_touch(x0, y0, x1, y1, side);
}
static int xl_nail(const float *pts, int npts, float direction) { return ogres_nail(pts, npts, direction) | bombs_nail(pts, npts); }
static void xl_draw_enemy(const Enemy *e, float z, uint8_t tint) {
  if (FSM(e) == EF_FUNGOON && e->mode == EM_ALIVE) fungoon_draw(e, z, tint);
}

static void xl_contact(Enemy *e, const BodyEvent *v) {
  if (FSM(e) == EF_FUNGLING && v->kind == EV_ENTER && e->st == FG_IDLE && !e->b1) {
    if (side_hit(e, 0)) e->b1 = true, e->c0 = 0;
    else if (side_hit(e, 2)) e->b1 = true, e->c0 = 2;
  }
}

static void xl_start(Enemy *e, const Ent *d) {
  if (FSM(e) == EF_FUNGLING) fungling_start(e);
  else if (FSM(e) == EF_AMBLOOM) walker_init(e);
  else if (FSM(e) == EF_FUNGOON) fungoon_start(e);
  else if (FSM(e) == EF_SHRUMELING) shrumeling_start(e);
  else if (FSM(e) == EF_OGRE) ogre_start(e);
  else if (FSM(e) == EF_SPORG) sporg_start(e);
}

static void xl_fixed(Enemy *e) {
  if (FSM(e) == EF_FUNGLING) fungling_fixed(e);
  else if (FSM(e) == EF_AMBLOOM) ambloom_fixed(e);
  else if (FSM(e) == EF_FUNGOON) fungoon_fixed(e);
  else if (FSM(e) == EF_SHRUMELING) shrumeling_fixed(e);
  else if (FSM(e) == EF_OGRE) ogre_fixed(e);
  else if (FSM(e) == EF_SPORG) e->body.vx = e->body.vy = 0;   /* (kinematic) */
}

static void xl_update(Enemy *e) {
  if (FSM(e) == EF_FUNGLING) fungling_update(e);
  else if (FSM(e) == EF_AMBLOOM) walker_update(e);
  else if (FSM(e) == EF_FUNGOON) fungoon_update(e);
  else if (FSM(e) == EF_SHRUMELING) shrumeling_update(e);
  else if (FSM(e) == EF_OGRE) ogre_update(e);
  else if (FSM(e) == EF_SPORG) sporg_update(e);
}
