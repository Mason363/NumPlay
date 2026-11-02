/* The enemies past Greenpath (HK_XL: the bigger game), each kind's FSM as the game has it. Part of enemy.c: its
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
    sprite_inst(SPRITE_GAS, g->x, g->y, -0.001f, d, d, tint, &in);
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

/* ---------------------------------------------------------------- the kinds' hooks (enemy.c) */
static bool xl_wants_contacts(const Enemy *e) { return FSM(e) == EF_FUNGLING; }
static void xl_enter(void) { memset(gas, 0, sizeof gas); }
static void xl_tick(void) { gas_tick(); }
static void xl_draw(void) { gas_draw(); }
static int xl_touch(float x0, float y0, float x1, float y1, int *side) { return gas_touch(x0, y0, x1, y1, side); }
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
}

static void xl_fixed(Enemy *e) {
  if (FSM(e) == EF_FUNGLING) fungling_fixed(e);
  else if (FSM(e) == EF_AMBLOOM) ambloom_fixed(e);
  else if (FSM(e) == EF_FUNGOON) fungoon_fixed(e);
  else if (FSM(e) == EF_SHRUMELING) shrumeling_fixed(e);
}

static void xl_update(Enemy *e) {
  if (FSM(e) == EF_FUNGLING) fungling_update(e);
  else if (FSM(e) == EF_AMBLOOM) walker_update(e);
  else if (FSM(e) == EF_FUNGOON) fungoon_update(e);
  else if (FSM(e) == EF_SHRUMELING) shrumeling_update(e);
}
