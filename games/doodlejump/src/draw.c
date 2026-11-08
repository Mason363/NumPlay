/* Doodle Jump: the page as the climb has it, at half size in the middle of the screen, with the original's pictures
 * (shrunk to a quarter: a pixel is two points). */
#include "dj.h"
#include "gfx.h"

#define X0 80 /* the page's left on the screen */
static int sx(float x) { return X0 + (int)(x * 0.5f + 1000.5f) - 1000; }
static int sy(float y) { return 240 - ((int)((y - W.cam) * 0.5f + 1000.5f) - 1000); }

/* drawn again across the page's edge when it reaches over it */
static void spr_wrap(int id, int x, int y, int alpha) {
  gfx_spr(id, x, y, alpha);
  if (x < X0 + 40) gfx_spr(id, x + 160, y, alpha);
  if (x > X0 + 120) gfx_spr(id, x - 160, y, alpha);
}

static void platform(const plat_t *p) {
  int x = sx(p->x), y = sy(p->y);
  if (y < -40 || y > 260) return;
  int t = (int)p->t;
  switch (p->type) {
  case P_GREEN: gfx_spr(S_P_GREEN, x, y, 32); break;
  case P_BLUE: gfx_spr(S_P_BLUE, x, y, 32); break;
  case P_WHITE:
    if (p->state != 1) gfx_spr(S_P_WHITE, x, y, 32);
    return;
  case P_BROWN: /* breaking in two, then falling */
    if (p->state != 1) gfx_spr(S_P_BROWN0, x, y, 32);
    else if (t < 40) gfx_spr(t < 3 ? S_P_BROWN1 : t < 6 ? S_P_BROWN2 : S_P_BROWN3, x, y + t * t / 16, 32);
    return;
  case P_BOMB: /* yellow, orange, red, then blown to bits */
    if (p->state != 1) gfx_spr(S_P_BOMB0 + (t * 5 / 181), x, y, 32);
    else if (t < 18) gfx_spr(S_P_BOOM0 + t / 6, x, y, 32);
    return;
  }
  /* what's on it */
  if (p->item == I_SPRING) gfx_spr(p->state == 2 ? S_SPRING1 : S_SPRING0, sx(p->x + 8), y + 1, 32);
  else if (p->item == I_TRAMP)
    gfx_spr(p->state == 2 && t < 12 ? (t < 4 || t >= 8 ? S_TRAMP1 : S_TRAMP2) : S_TRAMP0, x, y + 1, 32);
  else if (p->item == I_PROP) gfx_spr(S_PROP_ITEM, x, y + 1, 32);
  else if (p->item == I_JET) gfx_spr(S_JET_ITEM, x, y + 1, 32);
}

static void monster(const mon_t *m) {
  int x = sx(mon_x(m)), y = sy(m->y), t = (int)m->t, id;
  if (y < -60 || y > 300) return;
  switch (m->kind) {
  case M_FLY: id = S_M_FLY0 + (m->state ? 0 : t / 4 % 5); break;
  case M_BAT: id = S_M_BAT0 + (m->state ? 0 : t / 5 % 5); break;
  case M_EYE: id = S_M_EYE0 + t / 12 % 2; break;
  case M_BLOB: id = S_M_BLOB0 + t / 12 % 2; break;
  case M_CROC: id = S_M_CROC0 + t / 20 % 2; break;
  case M_BUG: id = S_M_BUG0 + t / 8 % 2; break;
  case M_HAIRY: id = S_M_HAIRY; break;
  case M_RED: id = S_M_RED; break;
  case M_PURPLE: id = S_M_PURPLE; break;
  case M_HOLE: id = S_M_HOLE; break;
  default: /* the UFO and its light */
    if (!m->state) spr_wrap(S_M_BEAM, x, y + 4, 20);
    id = S_M_UFO0 + t / 10 % 2;
  }
  if (!m->state && m->kind != M_HOLE && m->kind != M_UFO && m->kind != M_FLY && m->kind != M_BAT)
    x += t / 3 % 2; /* (the ones that stay put shake) */
  spr_wrap(id, x, y, 32);
}

/* the doodler, his feet at x, y on the screen */
static void doodler(int x, int y, int alpha) {
  bool right = W.face, bent = W.frame - W.land < 7 && W.land, up = W.shot && W.frame - W.shot < 18;
  if (W.fly > 0 && W.item == I_JET) { /* the jetpack on his back, firing */
    int f = FLY - W.fly, id = f < 6 ? f / 3 : W.fly < 12 ? 8 + (W.fly < 6) : 2 + f / 3 % 6;
    gfx_spr(S_JET0 + id, x + (right ? -9 : 9), y - 20, alpha);
  }
  if (W.flip && !W.dead) { /* turning over on a trampoline */
    int k = (72 - W.flip) * 8 / 72 % 8;
    if (k) {
      gfx_spr((right ? S_D_TURNR1 : S_D_TURNL1) + k - 1, x, y - 11, alpha);
      return;
    }
  }
  if (up) {
    gfx_spr(bent ? S_D_PUCAODSKOK : S_D_PUCA, x, y, alpha);
    gfx_spr(S_D_NOSE, x, y, alpha);
  } else if (right) gfx_spr(bent ? S_D_RIGHTODSKOK : S_D_RIGHT, x, y, alpha);
  else gfx_spr(bent ? S_D_LEFTODSKOK : S_D_LEFT, x, y, alpha);
  if (W.fly > 0 && W.item == I_PROP) { /* the hat, its blades turning once it lifts him */
    int f = FLY - W.fly;
    gfx_spr(f < 8 ? S_HAT0 : S_HAT1 + f / 2 % 3, x, y - 21, alpha);
  }
  if (W.dead == D_MONSTER) gfx_spr(S_STARS0 + W.frame / 6 % 3, x, y - 26, alpha); /* seeing stars */
}

void draw_doodler_at(int x, int y, bool right, bool bent) {
  gfx_spr(right ? (bent ? S_D_RIGHTODSKOK : S_D_RIGHT) : (bent ? S_D_LEFTODSKOK : S_D_LEFT), x, y, 32);
}

static void score(int x, int base, uint32_t v) {
  char d[10];
  int n = 0;
  do d[n++] = (char)(v % 10), v /= 10; while (v);
  while (n--) {
    int id = S_DIGIT1 + (d[n] ? d[n] - 1 : 9);
    gfx_spr(id, x, base, 32);
    x += dj_spr[id].w + 1;
  }
}

void draw_page(void) {
  /* the paper scrolls with the page: its lines 8.8 points apart */
  gfx_paper(X0, 0, 160, 240, 71, 54, -(int)(W.cam * 8.0f) % 71, 0);
  for (int i = 0; i < NP; i++) platform(&W.plat[i]);
  for (int i = 0; i < NM; i++)
    if (W.mon[i].kind) monster(&W.mon[i]);
  for (int i = 0; i < NS; i++)
    if (W.shot_on[i]) gfx_spr(S_BULLET, sx(W.shot_x[i]), sy(W.shot_y[i]), 32);
  if (W.drop) gfx_spr(W.drop == I_JET ? S_JET9 : S_HAT0, sx(W.drop_x), sy(W.drop_y), 32);
  int alpha = W.dead == D_HOLE || W.dead == D_UFO ? 32 - (int)(W.dead_t * 32 / 40) : 32;
  if (alpha > 0) {
    int x = sx(W.x), y = sy(W.y);
    doodler(x, y, alpha);
    if (x < X0 + 20) doodler(x + 160, y, alpha);
    if (x > X0 + 140) doodler(x - 160, y, alpha);
  }
  /* the bar at the top: the score, and the pause button */
  gfx_spr(S_TOPBAR, X0, 0, 32);
  score(X0 + 4, 11, W.score);
  gfx_spr(S_PAUSE, X0 + 150, 8, 32);
}
