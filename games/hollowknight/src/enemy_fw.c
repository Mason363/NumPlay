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


/* ---------------------------------------------------------------- the Mantis' ranges: the Knight in one of its children's
 * boxes (or circles), sized with it, and in sight from it (their FSMs' raycast: no terrain between) */
static bool hero_in_sight_of(const Enemy *e, const char *name) {
  const Ent *r = range_named(e, name);
  if (!r || g_hero.hidden) return false;
  float k = fabsf(e->sx), cx = e->body.x + r->x0 * e->sx, cy = e->body.y + r->y0 * k;
  float kx = g_hero.body.x + g_hero.body.ox, ky = g_hero.body.y + g_hero.body.oy;
  if (r->y1 < 0) {   /* (a circle: the Knight's box meets it) */
    float qx = kx < cx - g_hero.body.hx ? kx + g_hero.body.hx : kx > cx + g_hero.body.hx ? kx - g_hero.body.hx : cx;
    float qy = ky < cy - g_hero.body.hy ? ky + g_hero.body.hy : ky > cy + g_hero.body.hy ? ky - g_hero.body.hy : cy;
    if ((qx - cx) * (qx - cx) + (qy - cy) * (qy - cy) >= r->x1 * k * r->x1 * k) return false;
  } else if (!(fabsf(kx - cx) < r->x1 * k + g_hero.body.hx && fabsf(ky - cy) < r->y1 * k + g_hero.body.hy))
    return false;
  /* (from the range's place, the Mantis', to the Knight's) */
  float dx = g_hero.body.x - e->body.x, dy = g_hero.body.y - e->body.y, l = sqrtf(dx * dx + dy * dy);
  return l <= 0 || !phys_ray(e->body.x, e->body.y, dx / l, dy / l, l, CF_TERRAIN, NULL);
}

/* FaceObject (sprite facing right): turned to the Knight, its clip from the start as it turns */
static void face_hero_r(Enemy *e, int clip) {
  float want = hero_x() > e->body.x ? fabsf(e->sx) : -fabsf(e->sx);
  if (e->sx != want) {
    set_scale_x(e, want);
    if (clip >= 0) anim_play_from_frame(&e->anim, clip, 0);
  }
}

/* (RayCast2d down 3 from a place by its own: no ground there) */
static bool no_ground_at(const Enemy *e, float dx) { return !phys_ray(e->body.x + dx, e->body.y - 1, 0, -1, 3, CF_TERRAIN, NULL); }

/* ---------------------------------------------------------------- Mantis Warriors (Mantis): still till the Knight comes
 * in front of them, behind or above; then a lunge with a swipe (or a step back first), a slash up, or a turn. Their slashes
 * clash with the nail. Once the Mantis Lords are beaten they bow to him as he comes near, unless he strikes */
enum { MT_PAUSE, MT_IDLE, MT_FEINT, MT_ATTACK_ANTIC, MT_ATTACK1, MT_SWIPE, MT_RECOVER, MT_COOLDOWN, MT_TURN, MT_UP_ANTIC,
       MT_UP_SLASH, MT_UP_RECOVER, MT_RETURN_L, MT_RETURN_R, MT_FRIENDLY, MT_BOW };
/* (qx: Atk Speed Crt; qy: Raycast X; b0: Moving; wait: a state's; t0: Feint's; pause1: its slashes' clash, a while) */
static void mantis_idle(Enemy *e) {
  anim_play(&e->anim, CLIP(e, R_IDLE));
  e->body.vx = 0;
  e->wait = 6, e->st = MT_IDLE;
}

/* (Init's and Reset's: its speed and raycast the way it faces) */
static void mantis_facing(Enemy *e) {
  e->qy = -2, e->qx = -30;
  if (!(e->sx < 0)) e->qy = 2, e->qx = 30;
}

static void mantis_friendly(Enemy *e) {
  /* Friendly Idle: harmless, its Head Box off */
  e->damage = 0;
  mk_hitbox(e, STR_HEAD_BOX, false);
  e->st = MT_FRIENDLY;
}

static void mantis_attack_antic(Enemy *e) {
  anim_play_from_frame(&e->anim, CLIP(e, R_A1), 0);
  e->b0 = true;
  mk_hitbox(e, STR_HEAD_BOX, false), mk_hitbox(e, STR_HEAD_BOX_F, true);
  e->body.vx = 0;
  e->st = MT_ATTACK_ANTIC;
}

static void mantis_feint_or_attack(Enemy *e) {
  if (rand_range(0, 1) < 0.65f) {
    mantis_attack_antic(e);
    return;
  }
  /* Feint: a step back first, a while, or till there is no ground behind */
  e->body.vx = e->qx * -0.3f;
  anim_play(&e->anim, CLIP(e, R_A8));
  e->t0 = 0.3f, e->st = MT_FEINT;
  if (no_ground_at(e, -e->qy)) mantis_attack_antic(e);
}

static void mantis_turn(Enemy *e) {
  e->qx = -e->qx, e->qy = -e->qy;
  anim_play_from_frame(&e->anim, CLIP(e, R_TURN), 0);
  set_scale_x(e, -e->sx);
  e->st = MT_TURN;
}

static void mantis_up_antic(Enemy *e) {
  anim_play_from_frame(&e->anim, CLIP(e, R_A5), 0);
  e->body.vx = 0;
  e->st = MT_UP_ANTIC;
}

static void mantis_cooldown(Enemy *e) {
  anim_play(&e->anim, CLIP(e, R_IDLE));
  mk_hitbox(e, STR_HEAD_BOX, true);
  e->wait = 0.5f, e->st = MT_COOLDOWN;
}

/* (RayCast2d ahead and down, BoolAllTrue: lunging, at an edge) Stop At Edge: still, no longer lunging */
static void mantis_edge(Enemy *e) {
  if (e->b0 && no_ground_at(e, e->qy)) e->body.vx = 0, e->b0 = false;
}

static void mantis_reset(Enemy *e) {
  e->damage = 1;
  mantis_facing(e);
  mantis_idle(e);
}

static void mantis_start(Enemy *e) {
  e->start_x = e->body.x, e->start_y = e->body.y;
  e->b0 = false, e->pause1 = 0;
  e->wait = 0.2f, e->st = MT_PAUSE;
  anim_play(&e->anim, CLIP(e, R_IDLE));
}

static void mantis_fixed(Enemy *e) {
  if (e->st == MT_RECOVER) e->body.vx = 0;
  else if (e->st == MT_RETURN_L) e->body.vx = -6;
  else if (e->st == MT_RETURN_R) e->body.vx = 6;
}

static void mantis_update(Enemy *e) {
  bool done = (e->anim.events & ANIM_DONE) != 0, hit = (e->flags & 8) != 0;
  if (e->pause1 > 0) e->pause1 -= DT;
  switch (e->st) {
    case MT_PAUSE:
      if ((e->wait -= DT) <= 0) {
        /* Init; Lords Defeated? */
        mantis_facing(e);
        if (pd_flag(PDF_DEFEATED_MANTIS_LORDS)) mantis_friendly(e);
        else mantis_idle(e);
      }
      break;
    case MT_IDLE:
      if (hit) {
        /* Turn?: the Knight behind it, a turn; else at him */
        if ((hero_x() > e->body.x) != (e->sx > 0)) mantis_turn(e);
        else mantis_attack_antic(e);
      } else if (hero_in_sight_of(e, "Back Range"))
        mantis_turn(e);
      else if (hero_in_sight_of(e, "Front Range"))
        mantis_feint_or_attack(e);
      else if (hero_in_sight_of(e, "Above Range"))
        mantis_up_antic(e);
      else if ((e->wait -= DT) <= 0) {
        /* Return?: back where it started, if on its level and away from it */
        float dx = e->body.x - e->start_x;
        if (fabsf(e->body.y - e->start_y) > 0.25f || fabsf(dx) <= 1) mantis_idle(e);
        else {
          anim_play(&e->anim, CLIP(e, R_WALK));
          set_scale_x(e, dx > 0 ? -1.0f : 1.0f);   /* (SetScale: 1, not its own size) */
          e->st = dx > 0 ? MT_RETURN_L : MT_RETURN_R;
        }
      }
      break;
    case MT_FEINT:
      if (hit || (e->t0 -= DT) <= 0 || no_ground_at(e, -e->qy)) mantis_attack_antic(e);
      break;
    case MT_ATTACK_ANTIC:
      if (done) {
        /* Set Speed; Attack 1 */
        e->body.vx = e->qx;
        mk_hitbox(e, STR_HEAD_BOX_F, false);
        anim_play_from_frame(&e->anim, CLIP(e, R_A2), 0);
        e->st = MT_ATTACK1;
        mantis_edge(e);
      }
      break;
    case MT_ATTACK1:
      mantis_edge(e);
      if (done) {
        /* Attack Swipe */
        anim_play_from_frame(&e->anim, CLIP(e, R_A3), 0);
        mk_hitbox(e, STR_SLASH_FORWARD, true);
        e->st = MT_SWIPE;
        mantis_edge(e);
      }
      break;
    case MT_SWIPE:
      mantis_edge(e);
      if (done) {
        /* Attack Recover */
        anim_play_from_frame(&e->anim, CLIP(e, R_A4), 0);
        mk_hitbox(e, STR_SLASH_FORWARD, false);
        e->body.vx = 0;
        e->st = MT_RECOVER;
      }
      break;
    case MT_RECOVER:
      if (done) mantis_cooldown(e);
      break;
    case MT_COOLDOWN:
      if ((e->wait -= DT) <= 0) mantis_idle(e);
      break;
    case MT_TURN:
      if (done) mantis_feint_or_attack(e);
      break;
    case MT_UP_ANTIC:
      if (done) {
        anim_play_from_frame(&e->anim, CLIP(e, R_A6), 0);
        mk_hitbox(e, STR_SLASH_UP, true);
        e->st = MT_UP_SLASH;
      }
      break;
    case MT_UP_SLASH:
      if (done) {
        anim_play_from_frame(&e->anim, CLIP(e, R_A7), 0);
        mk_hitbox(e, STR_SLASH_UP, false);
        e->st = MT_UP_RECOVER;
      }
      break;
    case MT_UP_RECOVER:
      if (done) mantis_cooldown(e);
      break;
    case MT_RETURN_L:
    case MT_RETURN_R:
      if (e->st == MT_RETURN_L ? e->body.x <= e->start_x : e->body.x >= e->start_x) mantis_reset(e);
      break;
    case MT_FRIENDLY:
      if (hit) mantis_reset(e);
      else if (hero_dist(e) < 10) anim_play(&e->anim, CLIP_MANTIS_BOW), e->st = MT_BOW;
      break;
    case MT_BOW:
      if (hit) {
        mantis_reset(e);
        break;
      }
      face_hero_r(e, CLIP_MANTIS_TURN_TO_BOW);
      if (hero_dist(e) > 12) {
        anim_play(&e->anim, CLIP_MANTIS_BOW_END);   /* (Anim; Friendly Idle) */
        e->st = MT_FRIENDLY;
      }
      break;
  }
}

static void shape_bounds(const float *pts, int n, float *x0, float *y0, float *x1, float *y1);

/* its Slash Forward, Slash Up met by the nail: they clash (nail_clash_tink), the Knight thrown back */
static int mantis_nail(const float *pts, int npts, float direction) {
  int out = 0;
  float x0, y0, x1, y1;
  shape_bounds(pts, npts, &x0, &y0, &x1, &y1);
  for (int i = 0; i < MAX_ENEMIES; i++) {
    Enemy *e = &en[i];
    if (e->mode != EM_ALIVE || FSM(e) != EF_MANTIS || e->pause1 > 0 || (e->st != MT_SWIPE && e->st != MT_UP_SLASH)) continue;
    if (!hitbox_touch(e, x0, y0, x1, y1)) continue;
    e->pause1 = 0.12f;
    game_freeze(0.01f, 0.25f, 0.1f, 0, false);   /* (FreezeMoment 3) */
    hero_nail_parry();
    cam_shake(SHAKE_ENEMY_KILL);
    switch (cardinal(direction)) {
      case 0: hero_recoil_left(); break;
      case 1: hero_recoil_down(); break;
      case 2: hero_recoil_right(); break;
      default: hero_bounce(); break;
    }
    out |= HB_RECOIL;
  }
  return out;
}

/* ---------------------------------------------------------------- Mantis Youths (Mantis Flyer): asleep on the walls till
 * the Knight comes near and in sight; then they hover about him, at a height, and dart at him when he is below; once the
 * Lords are beaten they leave him be unless he strikes */
enum { MF_REST, MF_WAKE, MF_CHASE, MF_PAUSE, MF_ANTIC, MF_ATTACK, MF_RETURN, MF_RECOVER, MF_CD, MF_IDLE_BUZZ, MF_FRIENDLY };
/* (t0: a state's time; t1: CD's second wait; qx: Attack Angle; pause0: FaceDirection's; tx: its rotation's from) */
static void mflyer_chase(Enemy *e) {
  anim_play(&e->anim, CLIP(e, R_IDLE));
  e->t0 = 8, e->st = MF_CHASE;
}

static void mflyer_wake(Enemy *e) {
  /* Wake: Startle, harmful; turned upright in 0.2 s (iTweenRotateTo, easeOutSine) */
  anim_play_from_frame(&e->anim, CLIP(e, R_A2), 0);
  e->damage = 1;
  if (e->rq) {
    const Ent *c = enemy_rec(e, ET_COLLIDER);
    e->tx = e->rq * 90.0f, e->ang = e->tx, e->rq = 0;
    if (c) enemy_set_box(e, c->x0, c->y0, c->x1, c->y1);
  }
  e->t1 = 0, e->st = MF_WAKE;
}

static void mflyer_start(Enemy *e) {
  e->start_x = e->body.x, e->start_y = e->body.y;
  e->ax = e->ay = 0, e->wait = 0, e->pause0 = 0, e->ang = 0;
  anim_play(&e->anim, CLIP(e, R_A1));
  e->st = MF_REST;
  if (pd_flag(PDF_DEFEATED_MANTIS_LORDS)) e->damage = 0, e->st = MF_FRIENDLY;   /* (Lords Defeated) */
}

static void mflyer_fixed(Enemy *e) {
  switch (e->st) {
    case MF_REST:
    case MF_WAKE:
    case MF_FRIENDLY: e->body.vx = e->body.vy = 0; break;
    case MF_CHASE:
    case MF_PAUSE:
    case MF_CD: distance_fly_at(e, 7, 6, 0.2f, true, 4); break;
    case MF_ANTIC: e->body.vx *= 0.95f, e->body.vy *= 0.95f; break;   /* (DecelerateV2) */
    case MF_RECOVER: e->body.vx = e->body.vy = 0; break;
    case MF_IDLE_BUZZ: idle_buzz_p(e, 0.75f, 1, 1.75f, 15, 1.5f); break;
  }
}

static void mflyer_update(Enemy *e) {
  bool done = (e->anim.events & ANIM_DONE) != 0, hit = (e->flags & 8) != 0;
  bool near = hero_in_sight_of(e, "Wake Region");   /* (its Wake Region's bool) */
  switch (e->st) {
    case MF_REST:
      if (hit || near) mflyer_wake(e);
      break;
    case MF_FRIENDLY:
      if (hit) mflyer_wake(e);
      break;
    case MF_WAKE:
      if (e->ang != 0) {
        float k = (e->t1 += DT) / 0.2f;
        e->ang = k >= 1 ? 0 : e->tx * (1 - sinf(k * (float)M_PI / 2));
      }
      if (done) e->ang = 0, mflyer_chase(e);
      break;
    case MF_CHASE:
      face_hero_r(e, CLIP(e, R_A3));
      if (hero_in_sight_of(e, "Attack Range")) {
        /* Pause: a moment, still coming */
        anim_play(&e->anim, CLIP(e, R_IDLE));
        e->t0 = rand_range(0.15f, 0.5f), e->st = MF_PAUSE;
      } else if ((e->t0 -= DT) <= 0) {
        /* Check Near: the Knight still about, chasing; else back to buzzing */
        if (near) mflyer_chase(e);
        else anim_play(&e->anim, CLIP(e, R_IDLE)), e->start_x = e->body.x, e->start_y = e->body.y, e->st = MF_IDLE_BUZZ;
      }
      break;
    case MF_PAUSE:
      face_hero_r(e, CLIP(e, R_A3));
      if ((e->t0 -= DT) <= 0) {
        if (!hero_in_sight_of(e, "Attack Range")) {
          e->t0 = rand_range(0.15f, 0.5f);   /* (Recheck: CANCEL, Pause again) */
          break;
        }
        /* Get Angle: at the Knight, down to the side it faces; Attack Antic */
        float a = atan2f(hero_y() - e->body.y, hero_x() - e->body.x) * 180 / (float)M_PI;
        if (a < 0) a += 360;
        if (e->sx < 0) a = a < 180 ? 180 : a > 270 ? 270 : a;
        else a = a < 270 ? 270 : a > 360 ? 360 : a;
        e->qx = a;
        anim_play_from_frame(&e->anim, CLIP(e, R_A4), 0);
        e->st = MF_ANTIC;
      }
      break;
    case MF_ANTIC:
      if (done) {
        /* Attack: a dart at its angle, its sting on */
        float a = e->qx * (float)M_PI / 180;
        anim_play_from_frame(&e->anim, CLIP(e, R_A5), 0);
        e->body.vx = cosf(a) * 25, e->body.vy = sinf(a) * 25;
        mk_hitbox(e, STR_STING_HIT, true);
        e->t0 = 0.15f, e->st = MF_ATTACK;
      }
      break;
    case MF_ATTACK:
      if ((e->t0 -= DT) <= 0) {
        /* Return: back the way it came, its sting off */
        float a = (e->qx += 180) * (float)M_PI / 180;
        mk_hitbox(e, STR_STING_HIT, false);
        e->body.vx = cosf(a) * 25, e->body.vy = sinf(a) * 25;
        e->t0 = 0.25f, e->st = MF_RETURN;
      }
      break;
    case MF_RETURN:
      if ((e->t0 -= DT) <= 0) {
        e->body.vx = e->body.vy = 0;
        anim_play_from_frame(&e->anim, CLIP(e, R_A6), 0);
        e->st = MF_RECOVER;
      }
      break;
    case MF_RECOVER:
      if (done) {
        /* CD: hovering, half a second or less */
        anim_play(&e->anim, CLIP(e, R_IDLE));
        e->t0 = 0.5f, e->t1 = rand_range(0.35f, 0.65f), e->st = MF_CD;
      }
      break;
    case MF_CD:
      face_hero_r(e, CLIP(e, R_A3));
      if ((e->t0 -= DT) <= 0 || (e->t1 -= DT) <= 0) mflyer_chase(e);
      break;
    case MF_IDLE_BUZZ:
      if (near) {
        mflyer_chase(e);
        break;
      }
      face_direction_r(e, &e->pause0, CLIP(e, R_A3), 0.5f);
      break;
  }
}


/* ---------------------------------------------------------------- the Mantis Lords (Mantis Lord): the first fights alone,
 * then the other two together (Battle Sub's Start chooses their moves), till both fall (Mantis Battle's Battle Control).
 * Each comes in from out of sight to dash across the floor, to stab down from above, or to throw its lance from a wall,
 * then leaves. Their thrones, the challenge, the cage and the floors are the scripts' (vm.c), which tell them when */
#define ML_DASH_HERO_L 29.98f   /* (its FSM's variables, the arena's places) */
#define ML_DASH_HERO_R 30.18f
#define ML_DASH_SPEED 55
#define ML_DASH_X_L 22.25f
#define ML_DASH_X_R 37.65f
#define ML_DASH_Y 10.28f
#define ML_DSTAB_SPEED (-65)
#define ML_DSTAB_X_MIN 21.5f
#define ML_DSTAB_X_MAX 38.5f
#define ML_DSTAB_Y 16.96f
#define ML_LAND_Y 8.99f
#define ML_THROW_HERO_L 22
#define ML_THROW_HERO_R 37
#define ML_WALL_X_L 18.2f
#define ML_WALL_X_R 41.77f
#define ML_WALL_Y_MIN 10.96f
#define ML_WALL_Y_MAX 16.92f
enum { ML_OFF, ML_PAUSE, ML_START_PAUSE, ML_IDLE, ML_SUB_IDLE, ML_DASH_ARRIVE, ML_DASH_ANTIC, ML_DASH_ATTACK, ML_DASH_RECOVER,
       ML_DASH_HIT_OFF, ML_DASH_LEAVE, ML_DSTAB_ARRIVE, ML_DSTAB_ATTACK, ML_DSTAB_LAND, ML_DSTAB_LEAVE, ML_ARRIVE, ML_THROW_ANTIC,
       ML_THROW, ML_THROW_CD, ML_LEAVE_PAUSE, ML_WALL_LEAVE1, ML_WALL_LEAVE2, ML_AFTER_THROW,
       /* (its corpse: Corpse Mantis Lord (First)) */
       ML_CORPSE, ML_CORPSE_STOP, ML_DEPART, ML_GONE };
/* (the events Battle Sub's Start sends them) */
enum { MLE_NONE, MLE_DASH, MLE_DSTAB, MLE_DASH_L, MLE_DASH_R, MLE_HIGH_THROW, MLE_WIDE_L, MLE_WIDE_R, MLE_NARROW_L,
       MLE_NARROW_R };
/* (each lord's FSM's bools, Shot Event and counts, by which it is: 0 the first, 1 and 2 the others) */
static struct {
  bool sub, attacking_r, throw_r, throw_ud, no_dust;
  uint8_t shot;      /* (MLE_WIDE_L .. MLE_NARROW_R, 0 none) */
  uint8_t ct[6];     /* (Ct Dash, Dstab, Throw; then their Ms) */
  uint8_t loops;
} mls[3];
/* (e: c1 which; qx Dash Speed Crt; start_x, start_y Away Pos; wait a state's time; b0 Dash Recover's next frame) */

/* the lances (Shot Mantis Lord): out fast, slowing, then back past it */
#define MAX_LANCES 4
static struct {
  bool on;
  float x, y, vx, vy, force, t;
  Anim anim;
} lances[MAX_LANCES];

static void lance_throw(Enemy *e) {
  int k = 0;
  for (int i = 0; i < MAX_LANCES; i++)
    if (!lances[i].on) k = i;
  const Ent *v = enemy_rec(e, ET_VARS);
  typeof(lances[0]) *l = &lances[k];
  memset(l, 0, sizeof *l);
  l->on = true;
  l->x = e->body.x + (v ? v->p0 : -2.01f) * e->sx, l->y = e->body.y + (v ? v->p1 : -1.97f);   /* (its Throw Point) */
  anim_play(&l->anim, CLIP_MANTISLORD_MANTIS_AIR_PROJECTILE);
  const typeof(mls[0]) *m = &mls[e->c1];
  if (m->shot) {
    /* (Shot Event: Wide, Narrow, left or right; High Pt) */
    bool wide = m->shot == MLE_WIDE_L || m->shot == MLE_WIDE_R, right = m->shot == MLE_WIDE_L || m->shot == MLE_NARROW_L;
    l->vx = (wide ? 33 : 16.5f) * (right ? 1 : -1), l->force = (wide ? 24 : 12) * (right ? -1 : 1);
    l->vy = -2.2f;
    return;
  }
  /* Check UD: down, or up from a little lower (and up more from high: Corner Shot?); Check LR */
  if (m->throw_ud) l->vy = -1.1f;
  else {
    l->vy = 1.1f, l->y -= 0.5f;
    if (l->y >= 19) l->vy = 2;
  }
  l->vx = m->throw_r ? 33 : -33, l->force = m->throw_r ? -24 : 24;
}

static void lances_tick(void) {
  for (int i = 0; i < MAX_LANCES; i++) {
    typeof(lances[0]) *l = &lances[i];
    if (!l->on) continue;
    l->anim.events = 0;
    anim_update(&l->anim, DT);
    l->vx += l->force * DT;   /* (AddForce2d, its mass 1) */
    l->x += l->vx * DT, l->y += l->vy * DT;
    if ((l->t += DT) >= 6) l->on = false;   /* (Recycle) */
  }
}

static int lances_touch(float x0, float y0, float x1, float y1, int *side) {
  for (int i = 0; i < MAX_LANCES; i++) {
    const typeof(lances[0]) *l = &lances[i];
    float cx = l->x + 0.0122f * 1.5f, cy = l->y + 0.0184f * 1.5f, hx = 3.2207f * 0.75f, hy = 0.8937f * 0.75f;
    if (l->on && x1 > cx - hx && x0 < cx + hx && y1 > cy - hy && y0 < cy + hy) {
      *side = l->x > (x0 + x1) / 2 ? SIDE_RIGHT : SIDE_LEFT;
      return 1;
    }
  }
  return 0;
}

static void lances_draw(void) {
  for (int i = 0; i < MAX_LANCES; i++) {
    const typeof(lances[0]) *l = &lances[i];
    if (!l->on || l->anim.sprite < 0) continue;
    Inst in;
    sprite_inst(l->anim.sprite, l->x, l->y, -0.002f, 1.5f, 1.5f, 0, &in);
    gfx_actor(&in, SORT_KEY(0, 0));
  }
}

/* (seen or not, hit or not: SetMeshRenderer, SetCollider) */
static void mlord_shown(Enemy *e, bool on) {
  if (on) e->flags &= (uint8_t)~(1 | 32);
  else e->flags |= 1 | 32;
}

static void mlord_play(Enemy *e, int clip, int st) {
  anim_play_from_frame(&e->anim, clip, 0);
  e->st = (uint8_t)st;
}

static void mlord_away(Enemy *e) {
  /* Away: back out of sight; Idle */
  e->body.x = e->start_x, e->body.y = e->start_y, e->body.vx = e->body.vy = 0;
  mlord_shown(e, false);
  e->wait = 0.4f, e->st = ML_IDLE;
}

static void mlord_dash(Enemy *e, bool right) {
  /* Dash R, L: from one side, across; Dash Arrive */
  e->body.x = right ? ML_DASH_X_L : ML_DASH_X_R, e->body.y = ML_DASH_Y;
  set_scale_x(e, right ? 1 : -1);
  e->qx = right ? ML_DASH_SPEED : -ML_DASH_SPEED;
  mls[e->c1].attacking_r = right;
  mlord_shown(e, true);
  mlord_play(e, CLIP_MANTISLORD_DASH_ARRIVE, ML_DASH_ARRIVE);
}

static void mlord_dash_dir(Enemy *e) {
  float hx = hero_x();
  mlord_dash(e, hx < ML_DASH_HERO_L ? false : hx > ML_DASH_HERO_R ? true : rand_range(0, 1) < 0.5f);
}

static void mlord_dstab(Enemy *e) {
  /* Dstab Pos: over the Knight (its Shift? changes nothing that shows), turned either way at random; higher if he is
   * high (Shift Y?); Dstab Arrive */
  float x = hero_x();
  x = x < ML_DSTAB_X_MIN ? ML_DSTAB_X_MIN : x > ML_DSTAB_X_MAX ? ML_DSTAB_X_MAX : x;
  e->body.x = x, e->body.y = ML_DSTAB_Y;
  if (rand_range(0, 1) >= 0.5f) set_scale_x(e, -e->sx);
  if (rand_range(0, 1) >= 0.5f) rand_range(-4, 4);
  if (hero_y() >= 12) e->body.y = 19.2f;
  mlord_shown(e, true);
  mlord_play(e, CLIP_MANTISLORD_DSTAB_ARRIVE, ML_DSTAB_ARRIVE);
}

/* Throw L, R: on a wall, its lance across; Set Throws; Arrive */
static void mlord_throw_at(Enemy *e, bool left_wall) {
  e->body.x = left_wall ? ML_WALL_X_L : ML_WALL_X_R;
  set_scale_x(e, left_wall ? -1 : 1);
  mls[e->c1].throw_r = left_wall;
  mlord_shown(e, true);
  mlord_play(e, CLIP_MANTISLORD_WALL_ARRIVE, ML_ARRIVE);
}

static void mlord_throw_pos(Enemy *e) {
  float hx = hero_x();
  mlord_throw_at(e, hx > ML_THROW_HERO_R ? true : hx < ML_THROW_HERO_L ? false : rand_range(0, 1) < 0.5f);
}

static void mlord_throw(Enemy *e) {
  /* Throw UD?: down from higher, or up from lower; Throw Pos */
  typeof(mls[0]) *m = &mls[e->c1];
  m->throw_ud = rand_range(0, 1) < 0.5f, m->shot = 0;
  e->body.y = m->throw_ud ? rand_range(11.5f, 12.5f) : rand_range(9.5f, 10);
  mlord_throw_pos(e);
}

static void mlord_high_throw(Enemy *e) {
  /* High Check LR: at the Knight's height, a little lower */
  typeof(mls[0]) *m = &mls[e->c1];
  m->throw_ud = false, m->no_dust = true, m->shot = 0;
  float y = hero_y() - 1;
  e->body.y = y < 9 ? 9 : y > 21.93f ? 21.93f : y;
  mlord_throw_pos(e);
}

static void mlord_shot_throw(Enemy *e, int ev) {
  /* Wide, Narrow Throw L, R: high on a wall, its lance's flight its Shot Event's; Arrive 2 */
  bool left = ev == MLE_WIDE_L || ev == MLE_NARROW_L;
  set_scale_x(e, left ? -1 : 1);
  e->body.x = left ? ML_WALL_X_L : ML_WALL_X_R, e->body.y = rand_range(14.5f, 15.5f);
  mls[e->c1].shot = (uint8_t)ev;
  mlord_shown(e, true);
  mlord_play(e, CLIP_MANTISLORD_WALL_ARRIVE, ML_ARRIVE);
}

/* (an event to it: in Sub Idle only) */
static void mlord_event(Enemy *e, int ev) {
  if (e->st != ML_SUB_IDLE) return;
  switch (ev) {
    case MLE_DASH: mlord_dash_dir(e); break;
    case MLE_DSTAB: mlord_dstab(e); break;
    case MLE_DASH_L: mlord_dash(e, false); break;
    case MLE_DASH_R: mlord_dash(e, true); break;
    case MLE_HIGH_THROW: mlord_high_throw(e); break;
    default: mlord_shot_throw(e, ev); break;
  }
}

/* Attack Choice: high, or far to a side, a throw at his height; else a dash, a stab or a throw, none too often
 * (SendRandomEventV3) */
static void mlord_choose(Enemy *e) {
  float hx = hero_x(), hy = hero_y();
  if (hy > 19 || hx < 20.16f || hx > 39.87f) {
    mlord_high_throw(e);
    return;
  }
  static const uint8_t max[3] = {2, 3, 2}, missed[3] = {5, 5, 5};
  typeof(mls[0]) *m = &mls[e->c1];
  int pick = -1;
  while (pick < 0) {
    int k = (int)rand_range(0, 2.999f), forced = -1;
    for (int i = 0; i < 3; i++)
      if (m->ct[3 + i] >= missed[i]) forced = i;
    if (forced >= 0 || m->ct[k] < max[k]) {
      int p = forced >= 0 ? forced : k;
      uint8_t v = forced >= 0 ? 1 : (uint8_t)(m->ct[k] + 1);
      for (int i = 0; i < 3; i++) m->ct[i] = 0, m->ct[3 + i]++;
      m->ct[p] = v, m->ct[3 + p] = 0;
      pick = p;
    }
    if (m->loops < 255) m->loops++;
    if (m->loops > 100) pick = 0;
  }
  if (pick == 0) mlord_dash_dir(e);
  else if (pick == 1) mlord_dstab(e);
  else mlord_throw(e);
}

static void mlord_start(Enemy *e) {
  const Ent *v = enemy_rec(e, ET_VARS);
  e->c1 = v ? (uint8_t)v->a : 0;
  memset(&mls[e->c1], 0, sizeof mls[0]);
  mls[e->c1].sub = e->c1 != 0;
  e->start_x = e->body.x, e->start_y = e->body.y;   /* (Away Pos) */
  mlord_shown(e, false);
  e->st = ML_OFF;
  anim_play(&e->anim, CLIP_MANTISLORD_DASH_ARRIVE);
  if (e->c1 == 0) memset(lances, 0, sizeof lances);
}

static void mlord_fixed(Enemy *e) {
  if (e->st == ML_DSTAB_ATTACK) {
    /* ChaseObjectGround: after the Knight, 7 a step's faster up to 7 */
    float vx = e->body.vx;
    if (e->body.x < hero_x()) vx += 7;
    else if (e->body.x > hero_x()) vx -= 7;
    e->body.vx = vx > 7 ? 7 : vx < -7 ? -7 : vx;
  }
  /* (kinematic: its velocity, nothing in its way) */
  e->body.x += e->body.vx * DT, e->body.y += e->body.vy * DT;
}

static void mlord_update(Enemy *e) {
  bool done = (e->anim.events & ANIM_DONE) != 0;
  typeof(mls[0]) *m = &mls[e->c1];
  switch (e->st) {
    case ML_OFF: break;
    case ML_PAUSE: e->wait = 0.5f, e->st = ML_START_PAUSE; break;   /* (Init: Away Pos) */
    case ML_START_PAUSE:
      if ((e->wait -= DT) <= 0) {
        if (m->sub) e->st = ML_SUB_IDLE;
        else e->wait = 0.4f, e->st = ML_IDLE;
      }
      break;
    case ML_IDLE:
      if (m->sub) e->st = ML_SUB_IDLE;
      else if ((e->wait -= DT) <= 0) mlord_choose(e);
      break;
    case ML_SUB_IDLE:
      if (!m->sub) e->wait = 0.4f, e->st = ML_IDLE;   /* (CANCEL) */
      break;
    case ML_DASH_ARRIVE:
      if (done) mlord_play(e, CLIP_MANTISLORD_DASH_ANTIC, ML_DASH_ANTIC);
      break;
    case ML_DASH_ANTIC:
      if (done) {
        /* Dash Attack: across, its Dash Hit on, till past the other side */
        anim_play(&e->anim, CLIP_MANTISLORD_DASH_ATTACK);
        e->body.vx = e->qx;
        mk_hitbox(e, STR_DASH_HIT, true);
        e->st = ML_DASH_ATTACK;
      }
      break;
    case ML_DASH_ATTACK:
      if (m->attacking_r ? e->body.x > ML_DASH_X_R : e->body.x < ML_DASH_X_L) {
        /* End R, L; Dash Recover */
        e->body.x = m->attacking_r ? ML_DASH_X_R : ML_DASH_X_L;
        e->body.vx = 0;
        mlord_play(e, CLIP_MANTISLORD_DASH_RECOVER, ML_DASH_RECOVER);
      }
      break;
    case ML_DASH_RECOVER:
      /* (the next frame) Dash Hit Off */
      mk_hitbox(e, STR_DASH_HIT, false);
      e->st = ML_DASH_HIT_OFF;
      if (!done) break;
      /* fall through */
    case ML_DASH_HIT_OFF:
      if (done) {
        /* Dash Leave: its collider off */
        e->flags |= 1;
        mlord_play(e, CLIP_MANTISLORD_DASH_LEAVE, ML_DASH_LEAVE);
      }
      break;
    case ML_DASH_LEAVE:
    case ML_DSTAB_LEAVE:
      if (done) mlord_away(e);
      break;
    case ML_DSTAB_ARRIVE:
      if (done) {
        /* Dstab Attack: down fast, after the Knight */
        anim_play(&e->anim, CLIP_MANTISLORD_DSTAB_ATTACK);
        e->body.vy = ML_DSTAB_SPEED;
        e->st = ML_DSTAB_ATTACK;
      }
      break;
    case ML_DSTAB_ATTACK:
      if (e->body.y <= ML_LAND_Y) {
        /* Dstab Land */
        e->body.y = ML_LAND_Y, e->body.vx = e->body.vy = 0;
        mlord_play(e, CLIP_MANTISLORD_DSTAB_LAND, ML_DSTAB_LAND);
      }
      break;
    case ML_DSTAB_LAND:
      if (done) mlord_play(e, CLIP_MANTISLORD_DSTAB_LEAVE, ML_DSTAB_LEAVE);
      break;
    case ML_ARRIVE:
      if (done) mlord_play(e, CLIP_MANTISLORD_THROW_ANTIC, ML_THROW_ANTIC);
      break;
    case ML_THROW_ANTIC:
      if (done) {
        /* Throw: its lance; No Dust off */
        mlord_play(e, CLIP_MANTISLORD_THROW, ML_THROW);
        lance_throw(e);
        m->no_dust = false;
      }
      break;
    case ML_THROW:
      if (done) e->wait = 0.5f, e->st = ML_THROW_CD;
      break;
    case ML_THROW_CD:   /* (Throw CD; Keep Throwing?: CANCEL, one throw; Leave Pause) */
    case ML_LEAVE_PAUSE:
      if ((e->wait -= DT) <= 0) {
        if (e->st == ML_THROW_CD) e->wait = 0.5f, e->st = ML_LEAVE_PAUSE;
        else mlord_play(e, CLIP_MANTISLORD_WALL_LEAVE_1, ML_WALL_LEAVE1);
      }
      break;
    case ML_WALL_LEAVE1:
      if (done) mlord_play(e, CLIP_MANTISLORD_WALL_LEAVE_2, ML_WALL_LEAVE2);
      break;
    case ML_WALL_LEAVE2:
      if (done) {
        /* After Throw Pause: away, a while */
        e->body.x = e->start_x, e->body.y = e->start_y;
        mlord_shown(e, false);
        e->wait = 0.65f, e->st = ML_AFTER_THROW;
      }
      break;
    case ML_AFTER_THROW:
      if ((e->wait -= DT) <= 0) e->wait = 0.4f, e->st = ML_IDLE;
      break;
  }
}

/* ---------------------------------------------------------------- their fight (Battle Sub's Start, Battle Control) */
enum { MB_WAIT, MB_MAIN, MB_SUBS, MB_RETURN2, MB_BOW, MB_DONE };
enum { SB_OFF, SB_PAUSE, SB_CHOOSE, SB_MOVE, SB_D_DASH, SB_KILLED };
static struct {
  uint8_t st, sub, enemies, loops;
  float t, sub_t;
  uint8_t ct[14];   /* (Battle Sub's Start: Ct, then Ms, of its seven moves) */
  struct {
    uint8_t lord, ev;   /* (1 or 2; MLE_*) */
    float t;
  } later[8];
} mlb;

static Enemy *mlord(int which) {
  for (int i = 0; i < MAX_ENEMIES; i++)
    if (en[i].mode != EM_OFF && FSM(&en[i]) == EF_MANTISLORD && en[i].c1 == which) return &en[i];
  return NULL;
}

/* (SendEventByName to Mantis 1 or 2, with its delay) */
static void mlb_send(int lord, int ev, float delay) {
  if (delay < DT) {
    Enemy *e = mlord(lord);
    if (e && e->mode == EM_ALIVE) mlord_event(e, ev);
    return;
  }
  for (int k = 0; k < 8; k++)
    if (!mlb.later[k].lord) {
      mlb.later[k].lord = (uint8_t)lord, mlb.later[k].ev = (uint8_t)ev, mlb.later[k].t = delay;
      return;
    }
}

static void mlb_broadcast(const uint8_t *ids) {
  if (ids[g_room.id] != 255) vm_broadcast(ids[g_room.id]);
}

/* Choose Move: high or far to a side, Mantis 1 throws at his height; else one of seven moves, none too often */
static void mlb_choose(void) {
  float hx = hero_x(), hy = hero_y();
  if (hy > 19 || hx < 20.16f || hx > 39.87f) {
    mlb_send(1, MLE_HIGH_THROW, 0.5f);   /* (High Throw; Set Subs; then again next frame) */
    return;
  }
  static const uint8_t max[7] = {2, 2, 2, 2, 2, 1, 1}, missed[7] = {10, 10, 10, 10, 10, 12, 12};
  int pick = -1;
  while (pick < 0) {
    int k = (int)rand_range(0, 6.999f), forced = -1;
    for (int i = 0; i < 7; i++)
      if (mlb.ct[7 + i] >= missed[i]) forced = i;
    if (forced >= 0 || mlb.ct[k] < max[k]) {
      int p = forced >= 0 ? forced : k;
      uint8_t v = forced >= 0 ? 1 : (uint8_t)(mlb.ct[k] + 1);
      for (int i = 0; i < 7; i++) mlb.ct[i] = 0, mlb.ct[7 + i] < 255 ? mlb.ct[7 + i]++ : 0;
      mlb.ct[p] = v, mlb.ct[7 + p] = 0;
      pick = p;
    }
    if (mlb.loops < 255) mlb.loops++;
    if (mlb.loops > 100) pick = 0;
  }
  mlb.sub = SB_MOVE;
  switch (pick) {
    case 0: mlb_send(1, MLE_DSTAB, 0), mlb_send(2, MLE_DASH, 0.8f), mlb.sub_t = 2.75f; break;   /* (Stab Dash) */
    case 1: mlb_send(1, MLE_DASH, 0), mlb_send(2, MLE_DSTAB, 1.5f), mlb.sub_t = 2.75f; break;   /* (Dash Stab) */
    case 2:
      /* (Double Dash: D Dash Dir, right of 0 always; D Dash R: Mantis 1 now, Mantis 2 after) */
      mlb_send(1, MLE_DASH_R, 0);
      mlb.sub = SB_D_DASH, mlb.sub_t = 1.5f;
      break;
    case 3:
      /* (Pincer Check: the Knight in the middle, both at once; else not again soon) */
      if (hx >= 27.24f && hx <= 33.35f) mlb_send(1, MLE_DASH_R, 0), mlb_send(2, MLE_DASH_L, 0), mlb.sub_t = 2;
      else mlb.ct[3] = 0, mlb.ct[10] = 255, mlb.sub = SB_CHOOSE;
      break;
    case 4: mlb_send(1, MLE_DSTAB, 0), mlb_send(2, MLE_DSTAB, 0.75f), mlb.sub_t = 2.2f; break;   /* (Double Stab) */
    case 5: mlb_send(1, MLE_NARROW_L, 0.5f), mlb_send(2, MLE_NARROW_R, 0.5f), mlb.sub_t = 4; break;
    default: mlb_send(1, MLE_WIDE_L, 0.5f), mlb_send(2, MLE_WIDE_R, 0.5f), mlb.sub_t = 4; break;
  }
}

static void mlb_tick(void) {
  if (mlb.st == MB_WAIT) return;
  for (int k = 0; k < 8; k++)
    if (mlb.later[k].lord && (mlb.later[k].t -= DT) <= 0) {
      Enemy *e = mlord(mlb.later[k].lord);
      if (e && e->mode == EM_ALIVE) mlord_event(e, mlb.later[k].ev);
      mlb.later[k].lord = 0;
    }
  switch (mlb.sub) {
    case SB_PAUSE:
      if ((mlb.sub_t -= DT) <= 0) mlb.sub = SB_CHOOSE;
      break;
    case SB_CHOOSE: mlb_choose(); break;
    case SB_MOVE:
      if ((mlb.sub_t -= DT) <= 0) mlb.sub = SB_CHOOSE;
      break;
    case SB_D_DASH:
      if ((mlb.sub_t -= DT) <= 0) {
        /* (D Dash R 2: Mantis 2 the way that catches him) */
        mlb_send(2, hero_x() < 27.24f ? MLE_DASH_L : MLE_DASH_R, 0);
        mlb.sub = SB_MOVE, mlb.sub_t = 2.25f;
      }
      break;
  }
  if (mlb.st == MB_RETURN2 || mlb.st == MB_BOW) {
    static const uint8_t bow[NUM_ROOMS] = VMEV_MLORD_BOW, defeat[NUM_ROOMS] = VMEV_MANTIS_DEFEAT,
                         cage[NUM_ROOMS] = VMEV_MLORD_CAGE_UP, floor[NUM_ROOMS] = VMEV_MLORD_FLOOR_EXTEND,
                         gate[NUM_ROOMS] = VMEV_MLORD_GATE_OPEN;
    if ((mlb.t -= DT) > 0) return;
    if (mlb.st == MB_RETURN2) {
      /* Bow: beaten; the thrones bow */
      pd_set_flag(PDF_DEFEATED_MANTIS_LORDS, true);
      mlb_broadcast(bow);
      mlb.st = MB_BOW, mlb.t = 3;
    } else {
      /* MANTIS DEFEAT; Floor + Cage: the cage up, the floors out, the gate open */
      mlb_broadcast(defeat);
      mlb_broadcast(cage), mlb_broadcast(floor), mlb_broadcast(gate);
      mlb.st = MB_DONE;
    }
  }
}

/* a lord down: Battle Control's count, Battle Sub's KILLED; its corpse */
static void mlord_die(Enemy *e) {
  static const uint8_t ret[NUM_ROOMS] = VMEV_RETURN;
  mk_hitbox(e, STR_DASH_HIT, false);
  if (e->c1 != 0) {
    mls[1].sub = mls[2].sub = false;   /* (Killed) */
    mlb.sub = SB_KILLED;
    if (--mlb.enemies == 1) {
      if (ret[g_room.id] != 255) vm_send_named(VMSTR_MANTIS_LORD_THRONE_3, ret[g_room.id]);   /* (Return 1) */
    } else if (mlb.enemies == 0) {
      /* (Journal; Return 2: the last throne's lord back) */
      if (ret[g_room.id] != 255) vm_send_named(VMSTR_MANTIS_LORD_THRONE_1, ret[g_room.id]);
      mlb.st = MB_RETURN2, mlb.t = 3.75f;
    }
  }
  /* (its corpse, a little up: facing the Knight, flashing, the ground rumbling) */
  e->mode = EM_CORPSE;
  e->body.y += 0.5f, e->body.vx = e->body.vy = 0;
  mlord_shown(e, true);
  set_scale_x(e, hero_x() > e->body.x ? 1 : -1);
  anim_play(&e->anim, CLIP_MANTISLORD_DEATH_NEW_1);
  cam_rumble(RUMBLE_MED);
  e->wait = 1, e->t0 = 0, e->st = ML_CORPSE;
}

static void mlord_corpse_update(Enemy *e) {
  static const uint8_t defeated[NUM_ROOMS] = VMEV_MANTIS_DEFEATED;
  e->t0 += DT;
  if ((e->wait -= DT) > 0 && !(e->st == ML_DEPART && (e->anim.events & ANIM_DONE))) return;
  switch (e->st) {
    case ML_CORPSE:
      cam_rumble(RUMBLE_OFF);
      if (e->c1 == 0) e->st = ML_DEPART, anim_play_from_frame(&e->anim, CLIP_MANTISLORD_DEATH_NEW_2, 0), e->wait = 99;
      else e->st = ML_CORPSE_STOP, e->wait = 0.5f;   /* (Stop Effect) */
      break;
    case ML_CORPSE_STOP: e->st = ML_DEPART, anim_play_from_frame(&e->anim, CLIP_MANTISLORD_DEATH_NEW_2, 0), e->wait = 99; break;
    case ML_DEPART: e->flags |= 32, e->st = ML_GONE, e->wait = 1; break;   /* (Leave Pause: hidden) */
    case ML_GONE:
      if (defeated[g_room.id] != 255) vm_broadcast(defeated[g_room.id]);   /* (Destroy: MANTIS DEFEATED) */
      e->mode = EM_OFF;
      break;
  }
}

/* (its corpse flashes white: SpriteFlash.FlashingSuperDash, up 0.1 s, down 0.1 s, again) */
static void mlords_draw(void) {
  for (int i = 0; i < MAX_ENEMIES; i++) {
    const Enemy *e = &en[i];
    if (e->mode != EM_CORPSE || FSM(e) != EF_MANTISLORD || (e->flags & 32) || e->anim.sprite < 0) continue;
    uint8_t tint = 0;
    if (e->st == ML_CORPSE) {
      float p = fmodf(e->t0, 0.21f), k = p < 0.1f ? p / 0.1f : p < 0.11f ? 1 : 1 - (p - 0.11f) / 0.1f;
      tint = gfx_dyn_flash(12, 255, 255, 255, 255, 255, 255, 255, (uint8_t)(0.7f * k * 255));
    }
    Inst in;
    sprite_inst(e->anim.sprite, e->body.x, e->body.y, e->z, e->sx, 1, tint, &in);
    gfx_actor(&in, SORT_KEY(0, 0));
  }
}

/* the scripts' events the fight hears: the first lord's start, the others'; MANTIS DEFEAT: the mantises bow, the young
 * leave */
void enemies_vm_event(int ev) {
  static const uint8_t main[NUM_ROOMS] = VMEV_MLORD_START_MAIN, sub[NUM_ROOMS] = VMEV_MLORD_START_SUB,
                       defeat[NUM_ROOMS] = VMEV_MANTIS_DEFEAT;
  if (ev == 255) return;
  if (ev == main[g_room.id]) {
    Enemy *e = mlord(0);
    if (e && e->st == ML_OFF) e->st = ML_PAUSE;
    mlb.st = MB_MAIN;
  } else if (ev == sub[g_room.id]) {
    /* Battle Sub's Start: both on; Init Pause */
    for (int k = 1; k <= 2; k++) {
      Enemy *e = mlord(k);
      if (e && e->st == ML_OFF) e->st = ML_PAUSE;
    }
    mlb.st = MB_SUBS, mlb.sub = SB_PAUSE, mlb.sub_t = 0.5f, mlb.enemies = 2;
  } else if (ev == defeat[g_room.id]) {
    for (int i = 0; i < MAX_ENEMIES; i++) {
      Enemy *e = &en[i];
      if (e->mode != EM_ALIVE) continue;
      if (FSM(e) == EF_MANTIS) e->wait = 0.2f, e->st = MT_PAUSE;   /* (Pause, then friendly) */
      else if (FSM(e) == EF_MANTISFLYER) e->mode = EM_OFF;          /* (Disappear) */
    }
  }
}

/* ---------------------------------------------------------------- the kinds' hooks (enemy.c) */
static bool xl_wants_contacts(const Enemy *e) { return FSM(e) == EF_FUNGLING; }
static void xl_enter(void) {
  memset(gas, 0, sizeof gas);
  memset(globs, 0, sizeof globs);
  memset(bombs, 0, sizeof bombs);
  memset(lances, 0, sizeof lances);
  memset(&mlb, 0, sizeof mlb);
}
static void xl_tick(void) {
  gas_tick();
  globs_tick();
  bombs_tick();
  lances_tick();
  mlb_tick();
}
static void xl_draw(void) {
  gas_draw();
  globs_draw();
  bombs_draw();
  lances_draw();
  mlords_draw();
}
static int xl_touch(float x0, float y0, float x1, float y1, int *side) {
  int d = gas_touch(x0, y0, x1, y1, side);
  if (!d) d = globs_touch(x0, y0, x1, y1, side);
  if (!d) d = bombs_touch(x0, y0, x1, y1, side);
  return d ? d : lances_touch(x0, y0, x1, y1, side);
}
static int xl_nail(const float *pts, int npts, float direction) {
  return ogres_nail(pts, npts, direction) | bombs_nail(pts, npts) | mantis_nail(pts, npts, direction);
}
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
  else if (FSM(e) == EF_MANTIS) mantis_start(e);
  else if (FSM(e) == EF_MANTISFLYER) mflyer_start(e);
  else if (FSM(e) == EF_MANTISLORD) mlord_start(e);
}

static void xl_fixed(Enemy *e) {
  if (FSM(e) == EF_FUNGLING) fungling_fixed(e);
  else if (FSM(e) == EF_AMBLOOM) ambloom_fixed(e);
  else if (FSM(e) == EF_FUNGOON) fungoon_fixed(e);
  else if (FSM(e) == EF_SHRUMELING) shrumeling_fixed(e);
  else if (FSM(e) == EF_OGRE) ogre_fixed(e);
  else if (FSM(e) == EF_SPORG) e->body.vx = e->body.vy = 0;   /* (kinematic) */
  else if (FSM(e) == EF_MANTIS) mantis_fixed(e);
  else if (FSM(e) == EF_MANTISFLYER) mflyer_fixed(e);
}

static void xl_update(Enemy *e) {
  if (FSM(e) == EF_FUNGLING) fungling_update(e);
  else if (FSM(e) == EF_AMBLOOM) walker_update(e);
  else if (FSM(e) == EF_FUNGOON) fungoon_update(e);
  else if (FSM(e) == EF_SHRUMELING) shrumeling_update(e);
  else if (FSM(e) == EF_OGRE) ogre_update(e);
  else if (FSM(e) == EF_SPORG) sporg_update(e);
  else if (FSM(e) == EF_MANTIS) mantis_update(e);
  else if (FSM(e) == EF_MANTISFLYER) mflyer_update(e);
  else if (FSM(e) == EF_MANTISLORD) mlord_update(e);
}
