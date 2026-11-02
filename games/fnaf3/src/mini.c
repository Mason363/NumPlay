/* Five Nights at Freddy's 3: the 8-bit minigames, as their frames' events run them (gamesrc spec: BB, Mangle,
 * Toy Chica, GFreddy, RWQFSFASXC and Marion; the jump, which the phone builds lost, from the PC build's).
 *
 * A level is a few screens of the game's 800 x 480 (here 320 x 192 between black bars), the camera stepping a whole
 * screen when the player leaves one. The player is the game's: a "hitbox" with feelers round it that the solid
 * backdrops stop, gravity 10 pixels every 100 ms for each of two feet off the ground, a jump of 7 steps of 20 pixels
 * every 60 ms. Each level hides a cake: carry the one from Mangle's Quest to the four children (BB's Air Adventure,
 * Chica's Party, Stage 01, Shadow Bonnie) and Happiest Day ends the game well. */
#include <string.h>
#include "game.h"
#include "gfx.h"
#include "plat.h"
#include "data.h"
#include "mini_data.h"

#define M g.mg
#define MAXI 64

typedef struct {
  int16_t x, y;
  uint8_t obj, vis, alive, anim, dir, img, cnt, pad;
  int16_t s0, s1;
} Inst;

static Inst in[MAXI];
static int nin;
static const MgLevel *L;

/* ------------------------------------------------------------ the level's objects */
static const MgObj *obj(int o) { return &mg_objs[L->obj0 + o]; }
static int first(int o) {   /* the first instance of an object, -1 none */
  for (int i = 0; i < nin; i++)
    if (in[i].obj == o && in[i].alive) return i;
  return -1;
}
static const MgAnim *anim_of(const Inst *p) {
  const MgObj *ob = obj(p->obj);
  const MgAnim *best = NULL;
  for (int k = 0; k < ob->nanim; k++) {
    const MgAnim *a = &mg_anims[ob->anim0 + k];
    if (a->anim == p->anim && (a->dir == p->dir || !best)) best = a;
  }
  return best ? best : &mg_anims[ob->anim0];
}
static void box(const Inst *p, int *x0, int *y0, int *x1, int *y1) {
  const MgObj *ob = obj(p->obj);
  *x0 = p->x - ob->hx, *y0 = p->y - ob->hy, *x1 = *x0 + ob->w, *y1 = *y0 + ob->h;
}
static bool rect_hit(int ax0, int ay0, int ax1, int ay1, int bx0, int by0, int bx1, int by1) {
  return ax0 < bx1 && bx0 < ax1 && ay0 < by1 && by0 < ay1;
}
/* a feeler (w x h, hot spot hx, hy) at (x, y) on a solid backdrop */
static bool solid_at(int x, int y, int w, int h, int hx, int hy) {
  int x0 = x - hx, y0 = y - hy;
  for (int k = 0; k < L->nsolid; k++) {
    const MgRect *r = &mg_solids[L->solid0 + k];
    if (rect_hit(x0, y0, x0 + w, y0 + h, r->x, r->y, r->x + r->w, r->y + r->h)) return true;
  }
  return false;
}
/* a box on any visible instance of an object: its index, -1 none */
static int touching(int x0, int y0, int x1, int y1, int o) {
  for (int i = 0; i < nin; i++) {
    if (in[i].obj != o || !in[i].alive || !in[i].vis) continue;
    int a, b, c, d;
    box(&in[i], &a, &b, &c, &d);
    if (rect_hit(x0, y0, x1, y1, a, b, c, d)) return i;
  }
  return -1;
}
#define FEEL(n, dx, dy) solid_at(M.hx + (dx), M.hy + (dy), MGF_##n##_W, MGF_##n##_H, MGF_##n##_HX, MGF_##n##_HY)
#define FEEL_ON(n, dx, dy, o) (touching(M.hx + (dx) - MGF_##n##_HX, M.hy + (dy) - MGF_##n##_HY, \
                               M.hx + (dx) - MGF_##n##_HX + MGF_##n##_W, M.hy + (dy) - MGF_##n##_HY + MGF_##n##_H, o) >= 0)

static bool every(int id, int ms) {
  uint16_t p = (uint16_t)(ms * 60 / 1000);
  if (!M.every[id]) M.every[id] = p;
  if (--M.every[id]) return false;
  M.every[id] = p;
  return true;
}

/* each level's objects by their names (MGO_*, mini_data.h), -1 where a level has none */
typedef struct {
  int8_t active, collect, exit, exit2, get_this, get_this2, big_cake, child, balloons, win_screen, kid, wall;
  int8_t carry, follower, zip_up, zip_right, zip_left, block[4], parts[4], kids[4], trigger;
  int8_t dy, step;
} Names;
static const Names names[MGL_COUNT] = {
  [MGL_BB] = {MGO_BB_ACTIVE, MGO_BB_COLLECT, MGO_BB_EXIT, -1, MGO_BB_GET_THIS, -1, MGO_BB_BIG_CAKE, MGO_BB_ACTIVE_6,
              MGO_BB_BALLOONS, MGO_BB_ACTIVE_5, -1, -1, -1, -1, -1, -1, -1, {-1, -1, -1, -1}, {-1, -1, -1, -1},
              {-1, -1, -1, -1}, -1, -10, 15},
  [MGL_MANGLE] = {MGO_MANGLE_ACTIVE, MGO_MANGLE_COLLECT, MGO_MANGLE_EXIT, -1, MGO_MANGLE_GET_THIS, -1, -1, -1,
                  MGO_MANGLE_BALLOONS, MGO_MANGLE_ACTIVE_5, MGO_MANGLE_KID, MGO_MANGLE_WALL, -1, -1, -1, -1, -1,
                  {-1, -1, -1, -1}, {MGO_MANGLE_ACTIVE_6, MGO_MANGLE_ACTIVE_7, MGO_MANGLE_ACTIVE_8, MGO_MANGLE_ACTIVE_9},
                  {-1, -1, -1, -1}, -1, -45, 15},
  [MGL_CHICA] = {MGO_CHICA_ACTIVE, MGO_CHICA_COLLECT, MGO_CHICA_EXIT, MGO_CHICA_EXIT_2, MGO_CHICA_GET_THIS,
                 MGO_CHICA_GET_THIS_2, -1, MGO_CHICA_ACTIVE_4, MGO_CHICA_BALLOONS, MGO_CHICA_ACTIVE_5, -1, -1,
                 MGO_CHICA_COLLECT_4, MGO_CHICA_ACTIVE_9, -1, -1, -1, {-1, -1, -1, -1}, {-1, -1, -1, -1},
                 {MGO_CHICA_ACTIVE_8, -1, -1, -1}, -1, -20, 15},
  [MGL_STAGE] = {MGO_STAGE_ACTIVE, MGO_STAGE_COLLECT, MGO_STAGE_EXIT, -1, MGO_STAGE_GET_THIS, -1, MGO_STAGE_BIG_CAKE,
                 MGO_STAGE_ACTIVE_6, MGO_STAGE_BALLOONS, MGO_STAGE_ACTIVE_5, -1, -1, -1, -1, MGO_STAGE_ZIP_UP,
                 MGO_STAGE_ZIP_RIGHT, MGO_STAGE_ACTIVE_11, {-1, -1, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}, -1,
                 -35, 15},
  [MGL_SHADOW] = {MGO_SHADOW_ACTIVE, -1, MGO_SHADOW_EXIT, -1, MGO_SHADOW_GET_THIS, -1, MGO_SHADOW_BIG_CAKE,
                  MGO_SHADOW_ACTIVE_6, MGO_SHADOW_BALLOONS, MGO_SHADOW_ACTIVE_5, MGO_SHADOW_KID, -1, -1, -1,
                  MGO_SHADOW_ZIP_UP, MGO_SHADOW_ZIP_RIGHT, MGO_SHADOW_ACTIVE_11,
                  {MGO_SHADOW_BLOCK, MGO_SHADOW_BLOCK_2, MGO_SHADOW_BLOCK_3, MGO_SHADOW_BLOCK_4}, {-1, -1, -1, -1},
                  {-1, -1, -1, -1}, -1, -35, 15},
  [MGL_HAPPY] = {MGO_HAPPY_ACTIVE, MGO_HAPPY_COLLECT, MGO_HAPPY_EXIT, -1, MGO_HAPPY_GET_THIS, -1, MGO_HAPPY_BIG_CAKE,
                 MGO_HAPPY_ACTIVE_6, MGO_HAPPY_BALLOONS, MGO_HAPPY_ACTIVE_5, -1, -1, -1, -1, -1, -1, -1,
                 {-1, -1, -1, -1}, {-1, -1, -1, -1},
                 {MGO_HAPPY_ACTIVE_8, MGO_HAPPY_ACTIVE_13, MGO_HAPPY_ACTIVE_14, MGO_HAPPY_ACTIVE_15},
                 MGO_HAPPY_TRIGGER, -10, 20},
};
#define NM names[M.level]

static void set_vis(int o, bool v) {
  if (o < 0) return;
  for (int i = 0; i < nin; i++)
    if (in[i].obj == o) in[i].vis = v;
}
static bool exists(int o) { return o >= 0 && first(o) >= 0; }
static bool shown(int o) {
  int i = o >= 0 ? first(o) : -1;
  return i >= 0 && in[i].vis;
}
static void kill(int o) {
  if (o < 0) return;
  for (int i = 0; i < nin; i++)
    if (in[i].obj == o) in[i].alive = 0;
}

/* ------------------------------------------------------------ a level */
void mini_begin(int level, bool from_extras) {
  memset(&M, 0, sizeof M);
  M.level = (uint8_t)level, M.from_extras = from_extras;
  L = &mg_levels[level];
  M.hx = L->start_x, M.hy = L->start_y;
  M.camx = (int16_t)(M.hx / 800 * 800), M.camy = (int16_t)(M.hy / 480 * 480);
  nin = 0;
  for (int k = 0; k < L->ninst && nin < MAXI; k++) {
    const MgInst *s = &mg_insts[L->inst0 + k];
    Inst *p = &in[nin++];
    memset(p, 0, sizeof *p);
    p->x = s->x, p->y = s->y, p->obj = s->obj, p->vis = 1, p->alive = 1;
    if (s->obj == NM.collect) M.counter++;
  }
  M.counter++;   /* ("Counter": the exit opens at 1) */
  uint8_t f = g.mg_flags;
  /* (what starts hidden: the win's screen, the exits that open, the cakes given, the balloons without BB's) */
  set_vis(NM.win_screen, false);
  set_vis(NM.big_cake, false), set_vis(NM.get_this2, false);
  if (M.level != MGL_CHICA) set_vis(NM.exit, false);
  set_vis(NM.carry, false);
  set_vis(NM.balloons, f & MG_BB);
  if (f & MG_CAKE && M.level == MGL_MANGLE) kill(NM.get_this);
  static const uint8_t given[MGL_COUNT] = {MG_K1, 0, MG_K2, MG_K3, MG_K4, 0};
  if (given[level] && (f & given[level])) kill(NM.child);
  if (level == MGL_HAPPY) {
    static const uint8_t k[4] = {MG_K1, MG_K2, MG_K3, MG_K4};
    for (int i = 0; i < 4; i++) set_vis(NM.kids[i], f & k[i]);
  }
  if (level == MGL_SHADOW && (f & MG_CAKE)) set_vis(NM.big_cake, true), g.mg_flags |= MG_K4, save_progress();
}

static void win(void) {
  if (!M.win) M.win = 1, set_vis(NM.win_screen, true);
}

static void player(void) {
  uint32_t k = g.held;
  bool frozen = M.win || M.cake_t;
  /* gravity: each foot off the ground (and off a balloon) */
  if (!FEEL(FEEL_BOTTOM, 3, 41) && !FEEL_ON(FEEL_BOTTOM, 3, 41, NM.balloons) && !M.jump && !frozen && every(0, 100))
    M.hy += 10;
  if (!FEEL(FEEL_BOTTOM_2, 3, 60) && !FEEL_ON(FEEL_BOTTOM_2, 3, 60, NM.balloons) && !M.jump && !frozen && every(1, 100))
    M.hy += 10;
  bool wall_r = NM.wall >= 0 && FEEL_ON(FEEL_RIGHT, 39, -3, NM.wall), wall_l = NM.wall >= 0 && FEEL_ON(FEEL_LEFT, -33, -3, NM.wall);
  if ((k & (K_RIGHT | K_DIGIT(6))) && !FEEL(FEEL_RIGHT, 39, -3) && !wall_r && !frozen && every(2, 100))
    M.hx += NM.step, M.dir = 0, M.moving = 1;
  if ((k & (K_LEFT | K_DIGIT(4))) && !FEEL(FEEL_LEFT, -33, -3) && !wall_l && !frozen && every(3, 100))
    M.hx -= NM.step, M.dir = 16, M.moving = 1;
  bool jump = g.pressed & (K_OK | K_EXE | K_UP | K_DIGIT(8));
  bool ground = FEEL(FEEL_BOTTOM_2, 3, 60) || FEEL_ON(FEEL_BOTTOM_2, 3, 60, NM.balloons);
  if (jump && !M.jump && ground && !M.latch && !frozen) M.jump = 7, M.latch = 1;   /* (the PC build's) */
  if (M.jump > 0 && every(4, 60)) M.jump--, M.hy -= 20;
  if (FEEL(FEEL_TOP, 5, -31)) M.jump = 0;
  if (FEEL(FEEL_BOTTOM_3, 3, 30) && !M.cake_t) M.hy -= 5;
  if (!(g.held & (K_OK | K_EXE | K_UP | K_DIGIT(8)))) M.latch = 0;
  /* the camera: a whole screen when the player leaves one */
  if (M.hy > M.camy + 480) M.camy += 480, M.secret = 1, M.changed = 1;
  if (M.hy < M.camy) M.camy -= 480, M.secret = 1, M.changed = 1;
  if (M.hx > M.camx + 800) M.camx += 800, M.changed = 1, M.secret |= M.level != MGL_MANGLE;
  if (M.hx < M.camx) M.camx -= 800, M.changed = 1, M.secret |= M.level != MGL_MANGLE;
  int a = first(NM.active);
  if (a >= 0) in[a].x = M.hx, in[a].y = (int16_t)(M.hy + NM.dy), in[a].dir = M.dir;
}

/* the player's sprite's box */
static void pbox(int *x0, int *y0, int *x1, int *y1) {
  int a = first(NM.active);
  if (a >= 0) box(&in[a], x0, y0, x1, y1);
  else *x0 = *x1 = M.hx, *y0 = *y1 = M.hy;
}
static bool hitbox_on(int o) {
  return o >= 0 && touching(M.hx - MGF_HITBOX_HX, M.hy - MGF_HITBOX_HY, M.hx - MGF_HITBOX_HX + MGF_HITBOX_W,
                            M.hy - MGF_HITBOX_HY + MGF_HITBOX_H, o) >= 0;
}
static int player_on(int o) {
  int x0, y0, x1, y1;
  pbox(&x0, &y0, &x1, &y1);
  return o >= 0 ? touching(x0, y0, x1, y1, o) : -1;
}

static void level_events(void) {
  int i;
  /* the collectables, the cake that wins, the exit */
  if ((i = player_on(NM.collect)) >= 0 && !M.win) in[i].alive = 0, M.counter--, M.counter2 += M.level == MGL_CHICA ? 1 : 100;
  if ((M.level == MGL_MANGLE ? hitbox_on(NM.get_this) : player_on(NM.get_this) >= 0) && !M.win) {
    kill(NM.get_this), win();
    g.mg_flags |= M.level == MGL_MANGLE ? MG_CAKE : MG_BB;
    save_progress();
  }
  if (M.counter == 1 && M.level != MGL_CHICA) set_vis(NM.exit, true);
  if (player_on(NM.exit) >= 0 || player_on(NM.exit2) >= 0) win();
  if (M.changed && !(g.mg_flags & MG_BB)) kill(NM.balloons);
  M.changed = 0;
  /* giving the cake: the child's place (its "big cake" shows) */
  if ((M.level == MGL_BB || M.level == MGL_STAGE) && (g.mg_flags & MG_CAKE) && exists(NM.child) && !shown(NM.big_cake)) {
    int bc = first(NM.big_cake);
    if (bc >= 0) {
      int a, b, c, d;
      box(&in[bc], &a, &b, &c, &d);
      if (rect_hit(M.hx - MGF_HITBOX_HX, M.hy - MGF_HITBOX_HY, M.hx - MGF_HITBOX_HX + MGF_HITBOX_W,
                   M.hy - MGF_HITBOX_HY + MGF_HITBOX_H, a, b, c, d)) {
        set_vis(NM.big_cake, true);
        g.mg_flags |= M.level == MGL_BB ? MG_K1 : MG_K3;
        save_progress();
      }
    }
  }
  if (M.level == MGL_CHICA && (g.mg_flags & MG_CAKE) && exists(NM.child) && !shown(NM.get_this2)) {
    int gt = first(NM.get_this2);
    if (gt >= 0) {
      int a, b, c, d;
      box(&in[gt], &a, &b, &c, &d);
      if (rect_hit(M.hx - MGF_HITBOX_HX, M.hy - MGF_HITBOX_HY, M.hx - MGF_HITBOX_HX + MGF_HITBOX_W,
                   M.hy - MGF_HITBOX_HY + MGF_HITBOX_H, a, b, c, d))
        set_vis(NM.get_this2, true), g.mg_flags |= MG_K2, save_progress();
    }
  }
  if (shown(NM.big_cake) || shown(NM.get_this2)) {   /* the child takes it: 100 frames, it changes, 200: done */
    M.cake_t++;
    if (M.cake_t > 100 && M.level != MGL_HAPPY && (i = first(NM.child)) >= 0) in[i].anim = 12;
    if (M.cake_t > 200 && M.level != MGL_HAPPY) win();
  }
  switch (M.level) {
  case MGL_MANGLE: {
    /* Mangle's parts: each touched once, then carried round him */
    static const int8_t off[4][2] = {{61, 41}, {21, 17}, {25, 29}, {47, 27}};
    int a = first(NM.active), all = 1;
    for (int k = 0; k < 4; k++) {
      int p = first(NM.parts[k]);
      if (p < 0) continue;
      if (!in[p].s0 && hitbox_on(NM.parts[k])) in[p].s0 = 1, M.counter2 += 100;
      if (in[p].s0 && a >= 0 && !M.win)
        in[p].dir = M.dir, in[p].x = (int16_t)(in[a].x + (M.dir ? off[k][0] : -off[k][0])), in[p].y = (int16_t)(in[a].y + off[k][1]);
      all &= in[p].s0;
    }
    if (all || M.secret) set_vis(NM.exit, true);
    if (hitbox_on(NM.kid)) win();
    if (M.counter2 >= 400 && (i = first(NM.wall)) >= 0) in[i].alive = 0;
    break;
  }
  case MGL_CHICA: {
    if ((i = player_on(NM.kids[0])) >= 0 && M.counter2 > 0 && !in[i].s0) in[i].s0 = 1, in[i].anim = 12, M.counter2--;
    int c = first(NM.carry), a = first(NM.active);
    if (c >= 0 && a >= 0) in[c].x = (int16_t)(in[a].x + (M.dir ? -34 : 34)), in[c].y = (int16_t)(in[a].y - 35);
    set_vis(NM.carry, M.counter2 > 0);
    int f = first(NM.follower);
    if (f >= 0 && !M.win && every(5, 100)) {
      if (M.hx > in[f].x) in[f].x += 10, in[f].dir = 0;
      else if (M.hx < in[f].x) in[f].x -= 10, in[f].dir = 16;
    }
    break;
  }
  case MGL_STAGE: case MGL_SHADOW:
    if (player_on(NM.zip_left) >= 0 && every(6, 50)) M.hx -= 10;
    if (player_on(NM.zip_up) >= 0 && every(7, 50)) M.hy -= 10;
    if (player_on(NM.zip_right) >= 0 && every(8, 50)) M.hx += 20;
    if (M.level == MGL_SHADOW) {
      static const int8_t push[4][2] = {{-30, 0}, {30, 0}, {0, -30}, {0, 30}};
      for (int k = 0; k < 4; k++)
        if (hitbox_on(NM.block[k])) M.hx += push[k][0], M.hy += push[k][1];
      if (hitbox_on(NM.kid)) win();
    }
    break;
  case MGL_HAPPY: {
    bool all = true;
    for (int k = 0; k < 4; k++) all &= shown(NM.kids[k]);
    if (hitbox_on(NM.trigger) && exists(NM.child) && all && !shown(NM.big_cake))
      set_vis(NM.big_cake, true), g.goodend = 1, save_progress();
    if (M.cake_t > 1300) win();
    break;
  }
  }
}

void mini_step(void) {
  M.frame++;
  if (!M.win) player();
  level_events();
  if (M.win) M.win_t++;
  /* the sprites' pictures go round */
  for (int i = 0; i < nin; i++)
    if (in[i].alive && ++in[i].cnt >= 6) in[i].cnt = 0, in[i].img++;
}

int mini_done(void) { return M.win_t >= 200; }

void mini_draw(void) {
  gfx_fill(0, 0, SCREEN_W, SCREEN_H, BLACK);
  int sx = M.camx / 800, sy = M.camy / 480;
  if (sx >= 0 && sx < L->sw && sy >= 0 && sy < L->sh) {
    uint16_t id = mg_screens[L->screen0 + sy * L->sw + sx];
    if (id != 0xFFFF) pic_part(id, 0, 24, 0, 0, SCREEN_W, 192);
  }
  for (int i = 0; i < nin; i++) {
    const Inst *p = &in[i];
    if (!p->alive || !p->vis || p->obj == NM.win_screen) continue;
    const MgAnim *a = anim_of(p);
    int x = (p->x - M.camx) * 2 / 5, y = (p->y - M.camy) * 2 / 5 + 24;
    if (x < -160 || x > SCREEN_W + 160 || y < -120 || y > SCREEN_H + 120) continue;
    gfx_sprite(a->first + p->img % a->n, x, y);
  }
  gfx_fill(0, 0, SCREEN_W, 24, BLACK);
  gfx_fill(0, 216, SCREEN_W, 24, BLACK);
  if (M.win) {   /* the win: the game's white bars */
    int i = first(NM.win_screen);
    if (i >= 0) gfx_sprite(anim_of(&in[i])->first, 0, 24);
  }
  if (M.frame < 300) gfx_text(FONT_HUD, 6, SCREEN_H - 14, "Left Right walk   OK jump   Back pause", RGB(0x9A9A9A));
}
