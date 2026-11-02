/* The enemies past Greenpath (HK_XL: the bigger game), each kind's FSM as the game has it. Part of enemy.c: its
 * helpers are theirs. */

/* the Knight in an enemy's Wake Region (EnemyWakeRegion: WAKE as he comes in, SLEEP as he leaves): +1 in, -1 out,
 * 0 as it was (b0: in) */
static int wake_region(Enemy *e) {
  const Ent *r = enemy_rec(e, ET_RANGE);
  bool in = r && hero_in_box(e, r->x0, r->y0, r->x1, r->y1);
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

/* ---------------------------------------------------------------- the kinds' hooks (enemy.c) */
static bool xl_wants_contacts(const Enemy *e) { return FSM(e) == EF_FUNGLING; }

static void xl_contact(Enemy *e, const BodyEvent *v) {
  if (FSM(e) == EF_FUNGLING && v->kind == EV_ENTER && e->st == FG_IDLE && !e->b1) {
    if (side_hit(e, 0)) e->b1 = true, e->c0 = 0;
    else if (side_hit(e, 2)) e->b1 = true, e->c0 = 2;
  }
}

static void xl_start(Enemy *e, const Ent *d) {
  if (FSM(e) == EF_FUNGLING) fungling_start(e);
  else if (FSM(e) == EF_AMBLOOM) walker_init(e);
}

static void xl_fixed(Enemy *e) {
  if (FSM(e) == EF_FUNGLING) fungling_fixed(e);
  else if (FSM(e) == EF_AMBLOOM) ambloom_fixed(e);
}

static void xl_update(Enemy *e) {
  if (FSM(e) == EF_FUNGLING) fungling_update(e);
  else if (FSM(e) == EF_AMBLOOM) walker_update(e);
}
