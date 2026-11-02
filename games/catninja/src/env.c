/* Cat Ninja's things: environment/ (mines, buttons, dirt, crates, shooters and their bullets, falling and
 * sun spikes, crystals, lasers, bombs, the big crystal), entities/Boss, effects/ (trails and particles),
 * tools/text/TextManager and tools/Collision, in C. Each list keeps the original's order: a thing
 * taken out is spliced out, and one put back goes to the end, as the Vectors did. */
#include <math.h>
#include <string.h>
#include "cn.h"

mine_t mines[MAX_MINES];
button_t buttons[MAX_BUTTONS];
dirt_t dirts[MAX_DIRTS];
crate_t crates[MAX_CRATES];
shooter_t shooters[MAX_SHOOTERS];
bullet_t bullets[MAX_BULLETS];
fall_t falls[MAX_FALLS];
sun_t suns[MAX_SUNS];
sphere_t spheres[MAX_SPHERES];
screen_t screens[MAX_SCREENS];
bomb_t bombs[MAX_BOMBS];
trail_t trails[MAX_TRAILS];
particle_t particles[MAX_PARTICLES];
dtext_t texts[MAX_TEXTS];
int n_mines, n_buttons, n_dirts, n_crates, n_shooters, n_falls, n_suns, n_spheres, n_screens, n_trails, n_particles,
    n_texts;
static int n_bombs;
bigsphere_t big;
boss_t boss;
static laser_t lasers[MAX_LASERS];
static int n_lasers;
static double boss_csx = -1; /* the boss's container.scaleX (kept by remove(), as the original does) */

/* the Vectors: which things are in them, in order (the things themselves stay, to be put back) */
static uint8_t ord_dirt[MAX_DIRTS], ord_crate[MAX_CRATES], ord_fall[MAX_FALLS], ord_button[MAX_BUTTONS],
    ord_sphere[MAX_SPHERES];
static int o_dirt, o_crate, o_fall, o_button, o_sphere;
/* Main.env.resetters, in the order they were made: kind << 8 | index */
static uint16_t resetters[MAX_MINES + MAX_DIRTS + MAX_CRATES + MAX_FALLS];
static int n_resetters;
enum { R_MINE, R_DIRT, R_CRATE, R_FALL };

extern int collide_down(int x, int y), collide_right(int x, int y), collide_left(int x, int y), collide_up(int x, int y);
extern bool node_walkable(int node);
static double tw(double v) { return (double)(int)(v * 20) / 20; }
static int around(double v) { return (int)floor(v + 0.5); } /* Math.round */
static void splice(uint8_t *ord, int *n, int i) {
  memmove(ord + i, ord + i + 1, (size_t)(*n - i - 1));
  (*n)--;
}
static int find(const uint8_t *ord, int n, int v) {
  for (int i = 0; i < n; i++)
    if (ord[i] == v) return i;
  return -1;
}
static bool remove_from(uint8_t *ord, int *n, int v) { /* Main.env.remove */
  int i = find(ord, *n, v);
  if (i < 0) return false;
  splice(ord, n, i);
  return true;
}
static void resetter(int kind, int i) {
  if (n_resetters < (int)(sizeof resetters / sizeof resetters[0])) resetters[n_resetters++] = (uint16_t)(kind << 8 | i);
}
static int gi(int x, int y) { return y * gw + x; }
static bool in_grid(int i) { return i >= 0 && i < gw * gh; }

void env_clear(void) {
  n_mines = n_buttons = n_dirts = n_crates = n_shooters = n_falls = n_suns = n_spheres = n_screens = n_lasers = 0;
  o_dirt = o_crate = o_fall = o_button = o_sphere = 0;
  n_resetters = 0;
  big.on = false;
  for (int i = 0; i < MAX_BULLETS; i++)
    if (bullets[i].owner >= 0) bullets[i].on = false;
}

/* ---------------------------------------------------------------- tools/Collision.inRadius */
static bool in_radius(int px, int py, int tx, int ty, int radius) {
  static const int8_t sides[8] = {1, 0, 0, 1, -1, 0, 0, -1};
  for (int r = 0; r < radius; r++) {
    int lx = -1 - r, ly = -1 - r;
    for (int s = 0; s < 8; s += 2) {
      int ox = lx, oy = ly;
      for (int k = 0; k < (r + 1) * 2; k++) {
        int cx = px + ox, cy = py + oy;
        if (!(cx > 0 && cx < gw && cy > 0 && cy < gh)) return false;
        if ((grid[gi(cx, cy)] & SOLID) && (cx != P.coordx || cy != P.coordy)) {
          ox += sides[s], oy += sides[s + 1], lx = ox, ly = oy;
          continue;
        }
        ox += sides[s], oy += sides[s + 1], lx = ox, ly = oy;
        if (tx == cx && ty == cy) return true;
      }
    }
  }
  return false;
}

/* ---------------------------------------------------------------- environment/Mine */
static const int mine_pic[3] = {SPR_MINEBLINKOFF_0, SPR_MINEBLINKON_0, SPR_MINEOFF_0}; /* (+3 each way it faces) */
static void mine_reset(mine_t *m) {
  m->pic = SPR_MINEBLINKOFF_0 + 3 * m->graphic;
  m->timer = 0, m->setoff = false, m->rate = 10, m->blinkon = false, m->off = false;
}
void mine_create(int x, int y, int g) {
  if (n_mines == MAX_MINES) return;
  mine_t *m = &mines[n_mines];
  m->x = x, m->y = y, m->graphic = g;
  mine_reset(m);
  resetter(R_MINE, n_mines++);
}
static void mine_update(mine_t *m) {
  if (m->off) return;
  if (P.x >= m->x - 20 && P.x <= m->x + 20 && P.y >= m->y - 20 && P.y <= m->y + 20) m->setoff = true;
  if (m->setoff && ++m->timer >= m->rate) {
    m->pic = mine_pic[m->blinkon ? 0 : 1] + 3 * m->graphic;
    m->blinkon = !m->blinkon;
    m->timer = 0;
    m->rate -= 2;
    if (m->rate <= 0) {
      if (in_radius(m->x / 20, m->y / 20, P.coordx, P.coordy, 4)) player_die();
      particles_create(m->x, m->y, 20, 15, 15, 1, false);
      m->pic = SPR_MINEOFF_0 + 3 * m->graphic;
      m->off = true;
    }
  }
}

/* ---------------------------------------------------------------- environment/Button */
void button_create(int x, int y, int datanum) {
  if (n_buttons == MAX_BUTTONS) return;
  button_t *b = &buttons[n_buttons];
  b->x = x, b->y = y, b->sx = b->sy = 1, b->datanum = datanum, b->pressed = false, b->pic = SPR_BUTTON_0 + datanum - 16;
  b->msx = b->msy = 0, b->oy = 0;
  if (datanum == 16 || datanum == 19) b->msy = 0.25, b->oy = 5;
  else b->msx = 0.25;
  ord_button[o_button++] = (uint8_t)n_buttons++;
}
static void button_slam(int k) {
  button_t *b = &buttons[k];
  if (!b->pressed && P.x >= b->x - 25 && P.x <= b->x + 25 && P.y >= b->y - 25 && P.y <= b->y + 25) {
    b->sx -= b->msx, b->sy -= b->msy;
    b->y = tw(b->y + b->oy);
    if (b->sx <= 0.25 || b->sy <= 0.25) {
      interp(0, LB_UNLOCKLASER, true);
      b->pressed = true;
      if (b->sx <= 0.25) b->sx = 0.25;
      else if (b->sy <= 0.25) b->sy = 0.25;
      change_tile((int)(b->x / 20), (int)(b->y / 20), b->datanum + 12, true, true, true, true);
      remove_from(ord_button, &o_button, k);
    }
  }
}
void env_slam(void) {
  for (int i = 0; i < o_button; i++) button_slam(ord_button[i]);
}

/* ---------------------------------------------------------------- environment/Dirt */
void dirt_create(int x, int y) {
  if (n_dirts == MAX_DIRTS) return;
  dirts[n_dirts] = (dirt_t){x, y, 0, 0, false, true, false};
  ord_dirt[o_dirt++] = (uint8_t)n_dirts;
  resetter(R_DIRT, n_dirts++);
}
void dirt_break(int i) {
  dirt_t *d = &dirts[i];
  particles_create(d->x, d->y, 4, 5, 5, 4, false);
  if (remove_from(ord_dirt, &o_dirt, i)) d->alive = false;
  int g = gi(d->x / 20, d->y / 20);
  if (in_grid(g)) grid[g] &= 0x7F;
}
static void dirt_update(int i) {
  dirt_t *d = &dirts[i];
  int dx = d->x / 20, dy = d->y / 20;
  if (!d->activated) {
    if ((P.coordx + 1 == dx && P.coordy + 1 == dy) || (P.coordx == dx && P.coordy + 1 == dy) ||
        (P.coordx + 1 == dx && P.coordy == dy) || (P.coordx == dx && P.coordy == dy)) {
      timer_add(200, T_DIRT, i, false);
      d->activated = true, d->playing = true, d->frame = 0, d->ftimer = 0;
      return;
    }
    if ((P.coordx + 2 == dx && P.coordy == dy) || (P.coordx - 1 == dx && P.coordy == dy))
      if (P.rolling) dirt_break(i), d->activated = true;
  } else if (d->playing && ++d->ftimer >= 4) {
    d->ftimer = 0;
    d->frame = d->frame ? 0 : 1;
  }
}
static void dirt_reset(int i) {
  dirt_t *d = &dirts[i];
  if (!d->alive) {
    d->alive = true, d->playing = false, d->frame = 0, d->activated = false;
    ord_dirt[o_dirt++] = (uint8_t)i;
    int g = gi(d->x / 20, d->y / 20);
    if (in_grid(g)) grid[g] |= SOLID;
  }
}

/* ---------------------------------------------------------------- environment/Crate */
static void crate_walk(int x, int y, bool w, bool force) {
  if ((x != P.coordx || y != P.coordy || force) && (x - 1 != P.coordx || y != P.coordy || force)) {
    int g = gi(x, y);
    if (in_grid(g)) grid[g] = (uint8_t)((grid[g] & 0x7F) | (w ? 0 : SOLID));
  }
}
static void crate_updatepos(crate_t *c) {
  c->x = c->ox, c->y = c->oy, c->lx = c->x / 20, c->ly = c->y / 20;
  int g = gi(c->lx, c->ly);
  if (in_grid(g)) grid[g] |= SOLID;
}
void crate_create(int x, int y) {
  if (n_crates == MAX_CRATES) return;
  crate_t *c = &crates[n_crates];
  c->ox = x, c->oy = y, c->alive = true;
  crate_updatepos(c);
  ord_crate[o_crate++] = (uint8_t)n_crates;
  resetter(R_CRATE, n_crates++);
}
static void crate_break(int i) {
  crate_t *c = &crates[i];
  crate_walk(c->lx, c->ly, true, true);
  particles_create(c->x, c->y, 8, 25, 10, 4, false);
  if (remove_from(ord_crate, &o_crate, i)) c->alive = false;
}
static void crate_update(int i) {
  crate_t *c = &crates[i];
  int cx = around(c->x / 20.0), cy = c->y / 20;
  if (c->lx != cx) crate_walk(c->lx, c->ly, true, false), c->lx = cx, crate_walk(c->lx, c->ly, false, false);
  if (c->ly != cy) crate_walk(c->lx, c->ly, true, false), c->ly = cy, crate_walk(c->lx, c->ly, false, false);
  if (level != 27 && ((P.coordx + 2 == cx && P.coordy == cy) || (P.coordx - 1 == cx && P.coordy == cy)))
    if ((P.rolling && P.speedx >= 8) || (P.rolling && P.speedx <= -8)) crate_break(i);
}
static void crate_reset(int i) {
  crate_t *c = &crates[i];
  crate_walk(c->lx, c->ly, true, true);
  crate_updatepos(c);
  if (!c->alive) c->alive = true, ord_crate[o_crate++] = (uint8_t)i;
}

/* ---------------------------------------------------------------- environment/Bullet */
static void bullet_angle(bullet_t *b) {
  b->angle = -atan2(P.y - b->y, P.x - b->x) * (180 / M_PI);
  b->sx = b->bspeed * cos(M_PI / 180 * b->angle);
  b->sy = -b->bspeed * sin(M_PI / 180 * b->angle);
}
static bullet_t *bullet_new(double x, double y, int owner, bool homing) {
  for (int i = 0; i < MAX_BULLETS; i++)
    if (!bullets[i].on) {
      bullet_t *b = &bullets[i];
      memset(b, 0, sizeof *b);
      b->x = x, b->y = y, b->owner = owner, b->bspeed = 6, b->homing = homing, b->on = true;
      b->color = (int)(rnd01() * (255 * 255 * 255));
      bullet_angle(b);
      return b;
    }
  return 0;
}
/* true when it is gone */
static bool bullet_update(bullet_t *b) {
  double r = 2 + rnd01() * 2;
  trail_create(b->x + r + 0.5, b->y + r + 0.5, TR_DOT, 0, 0.08, 1, 1, 0, b->color, r);
  b->x += b->sx, b->y += b->sy;
  double cx = b->x / 20, cy = b->y / 20;
  if (!node_walkable(collide_right((int)cx, (int)cy)) || !node_walkable(collide_left((int)cx, (int)cy)) ||
      !node_walkable(collide_down((int)cx, (int)(cy - 1))) || !node_walkable(collide_up((int)cx, (int)cy)))
    return true;
  if (!P.dead) {
    if (++b->htimer < 150 && b->homing && ++b->homingtimer >= 10) bullet_angle(b), b->homingtimer = 0;
    if (b->x >= P.x - 20 && b->x <= P.x + 20 && b->y >= P.y - 20 && b->y <= P.y + 20) {
      player_die();
      return true;
    }
  }
  return false;
}

/* ---------------------------------------------------------------- environment/ShieldShooter */
void shooter_create(int x, int y, int o, bool homing) {
  if (n_shooters == MAX_SHOOTERS) return;
  shooters[n_shooters++] = (shooter_t){x, y, o, 100, 0, 0, 0, homing, false, false};
}
static void shooter_anim(shooter_t *s) { /* AnimateControl.update, 4 frames, no loop */
  if (s->paused) return;
  if (++s->ftimer >= 5) {
    if (!s->reverse) {
      if (++s->frame >= 4) s->frame = 3;
    } else if (--s->frame < 0) {
      s->frame = 0;
    }
    s->ftimer = 0;
  }
}
static void shooter_update(int k) {
  shooter_t *s = &shooters[k];
  if (s->x >= P.x - 500 && s->x <= P.x + 500 && s->y >= P.y - 400 && s->y <= P.y + 400) {
    ++s->timer;
    if (!P.dead && s->timer >= 100) {
      bullet_new(s->x, s->y, k, s->homing);
      s->timer = (int)(rnd01() * 100 / 4);
      s->startat = s->timer;
      s->frame = 3;
      s->reverse = true;
    }
    if (s->timer >= 80) s->reverse = false, shooter_anim(s);
    if (s->timer >= 40 && s->timer <= s->startat + 60 && s->reverse) shooter_anim(s);
  }
  if (s->x >= P.x - 1200 && s->x <= P.x + 1200 && s->y >= P.y - 800 && s->y <= P.y + 800) {
    for (int i = 0; i < MAX_BULLETS; i++)
      if (bullets[i].on && bullets[i].owner == k && bullet_update(&bullets[i])) {
        particles_create(bullets[i].x, bullets[i].y, 2, 10, 10, 3, false);
        bullets[i].on = false;
      }
  }
}

/* ---------------------------------------------------------------- environment/FallingSpike */
void fall_create(int x, int y, int dir) {
  if (n_falls == MAX_FALLS) return;
  fall_t *f = &falls[n_falls];
  static const int8_t v[4][2] = {{0, -10}, {0, 10}, {10, 0}, {-10, 0}};
  f->sx = v[dir][0], f->sy = v[dir][1], f->dir = dir, f->spx = f->x = x, f->spy = f->y = y, f->fall = false, f->alive = true;
  ord_fall[o_fall++] = (uint8_t)n_falls;
  resetter(R_FALL, n_falls++);
}
static void fall_remove(int i) {
  fall_t *f = &falls[i];
  particles_create(f->x, f->y, 5, 10, 10, 5, false);
  if (remove_from(ord_fall, &o_fall, i)) f->alive = false;
}
static void fall_update(int i) {
  fall_t *f = &falls[i];
  if (f->x >= P.x - 800 && f->x <= P.x + 800 && f->y >= P.y - 600 && f->y <= P.y + 600) {
    int cx = around(f->x / 20.0), cy = around(f->y / 20.0);
    if ((cx == P.coordx || f->x / 20 == P.coordx) && (cy == P.coordy || f->y / 20 == P.coordy)) {
      fall_remove(i);
      player_die();
    }
    if (f->fall) {
      f->x += f->sx, f->y += f->sy;
      int g = gi(cx, cy);
      if (!in_grid(g) || (grid[g] & SOLID)) fall_remove(i);
    } else {
      int tx = cx, ty = cy, sx = f->sx / 10, sy = f->sy / 10;
      while (cx >= 0 && cx < gw && cy >= 0 && cy < gh) {
        tx += sx, ty += sy;
        int g = gi(tx, ty);
        if (!in_grid(g) || (grid[g] & SOLID)) break;
        if (tx >= P.coordx - 1 && tx <= P.coordx + 1 && ty >= P.coordy - 1 && ty <= P.coordy + 1) {
          f->fall = true;
          break;
        }
      }
    }
  }
}
static void fall_reset(int i) {
  fall_t *f = &falls[i];
  f->fall = false, f->x = f->spx, f->y = f->spy;
  if (!f->alive) f->alive = true, ord_fall[o_fall++] = (uint8_t)i;
}

/* ---------------------------------------------------------------- environment/SunSpike */
void sun_create(int x, int y) {
  if (n_suns == MAX_SUNS) return;
  suns[n_suns++] = (sun_t){x, y, 0, 0, 0, 0};
}
static int gtype(int x, int y) {
  int g = gi(x, y);
  return in_grid(g) ? TYPE(g) : 0;
}
static void sun_speed(sun_t *s, int cx, int cy) {
  const int v = 6;
  if (gtype(cx, cy + 1) == 1) s->sx = v, s->sy = 0;
  else if (gtype(cx, cy + 1) != 1 && s->sx == v && s->sy == 0) s->sx = 0, s->sy = v;
  else if (gtype(cx - 1, cy) == 1) s->sx = 0, s->sy = v;
  else if (gtype(cx - 1, cy) != 1 && s->sx == 0 && s->sy == v) s->sx = -v, s->sy = 0;
  else if (gtype(cx, cy - 1) == 1) s->sx = -v, s->sy = 0;
  else if (gtype(cx, cy - 1) != 1 && s->sx == -v && s->sy == 0) s->sx = 0, s->sy = -v;
  else if (gtype(cx + 1, cy) == 1) s->sx = 0, s->sy = -v;
  else if (gtype(cx + 1, cy) != 1 && s->sx == 0 && s->sy == -v) s->sx = v, s->sy = 0;
}
static void sun_update(sun_t *s) {
  int cx = around(s->x / 20.0), cy = around(s->y / 20.0);
  s->x += s->sx, s->y += s->sy;
  if (cx != s->lastx || cy != s->lasty) {
    s->x = cx * 20, s->y = cy * 20;
    trail_create(s->x + 10, s->y + 10, TR_PIC, SPR_SUNSPIKE, 0.02, 0.5, 0, 0, 0, 0);
    s->lastx = cx, s->lasty = cy;
    sun_speed(s, cx, cy);
  }
  if (s->x >= P.x - 800 && s->x <= P.x + 800 && s->y >= P.y - 600 && s->y <= P.y + 600) {
    cx = around(s->x / 20.0), cy = around(s->y / 20.0);
    if (cx >= P.coordx - 1 && cx <= P.coordx + 1 && cy >= P.coordy - 1 && cy <= P.coordy + 1) player_die();
  }
}

/* ---------------------------------------------------------------- environment/Sphere: the crystals */
int sphere_create(double x, double y, bool collectable) {
  if (n_spheres == MAX_SPHERES) return -1;
  spheres[n_spheres] = (sphere_t){x, y, 0, 0, collectable, true};
  ord_sphere[o_sphere++] = (uint8_t)n_spheres;
  return n_spheres++;
}
static void sphere_update(int i) {
  sphere_t *s = &spheres[i];
  if (++s->ftimer >= 5) { /* (AnimateControl: 11 frames, looping) */
    s->ftimer = 0;
    if (++s->frame >= 11) s->frame = 0;
  }
  if (s->collectable && P.x >= s->x - 20 && P.x <= s->x + 20 && P.y >= s->y - 20 && P.y <= s->y + 20) {
    if (remove_from(ord_sphere, &o_sphere, i)) s->alive = false;
    particles_create(s->x, s->y, 40, 15, 15, 6, false);
    int tx = (int)(s->x / 20), ty = (int)(s->y / 20);
    change_tile(tx, ty, 0, false, true, true, true);
    ++collected;
    for (int k = 0; k < CN_CRYSTALS; k++)
      if ((int8_t)cn_crystals[k][0] == level && cn_crystals[k][1] == tx && cn_crystals[k][2] == ty)
        crystals_got[k >> 3] |= (uint8_t)(1 << (k & 7));
  }
}

/* ---------------------------------------------------------------- environment/BigSphere */
static uint8_t big_list[MAX_SPHERES];
static int n_big_list;
void bigsphere_create(int x, int y) {
  big = (bigsphere_t){true, false, x, y + 20, 0.15, 0, 0, 0, 0};
  n_big_list = 0;
  if (collected == 0) collected = 1;
}
static void bigsphere_finish(void) {
  P.stopMovement = false;
  big.finished = true;
  double w = 167 * big.scale, h = 163 * big.scale;
  int x0 = (int)((big.x - w / 2) / 20), y = (int)((big.y - h) / 20) + 1, x = x0;
  for (int r = 0; r < around(h / 20) + 1; r++) {
    for (int c = 0; c < around(w / 2 / 20) + 2; c++) {
      x++;
      int g = gi(x, y);
      if (in_grid(g)) grid[g] = 1 | SOLID;
    }
    x = x0, y++;
  }
  text_create(big.crystals > 20 ? TX_ENDGOOD : TX_ENDBAD, 0, 40, 0.05, false, 0);
}
void bigsphere_update(void) {
  if (++big.ftimer >= 4) {
    big.ftimer = 0;
    if (++big.frame >= 6) big.frame = 0;
  }
  if (!big.finished && P.x < big.x + 250) {
    P.stopMovement = true, P.rotation = 0;
    if (++big.timer >= 10) {
      big.timer = 0;
      if (collected > 0) {
        int s = sphere_create(P.x, P.y, false);
        if (s >= 0 && n_big_list < MAX_SPHERES) big_list[n_big_list++] = (uint8_t)s;
        --collected;
        info_update(true);
      }
    }
  }
  for (int i = 0; i < n_big_list; i++) {
    sphere_t *s = &spheres[big_list[i]];
    s->x = tw(s->x - (s->x - (big.x - 10)) / 10);
    s->y = tw(s->y - (s->y - (big.y - 20)) / 10);
    if (s->x >= big.x - 12 && s->x <= big.x + 12 && s->y >= big.y - 22 && s->y <= big.y + 22) {
      big.scale += 0.02;
      if (big.scale >= 0.5) big.scale = 0.5;
      if (remove_from(ord_sphere, &o_sphere, big_list[i])) s->alive = false;
      particles_create(big.x, big.y, 5, 15, 15, 6, false);
      memmove(big_list + i, big_list + i + 1, (size_t)(n_big_list - i - 1));
      n_big_list--, i--;
      ++big.crystals;
      if (n_big_list <= 0) bigsphere_finish();
    }
  }
}

/* ---------------------------------------------------------------- environment/Laser */
void laser_create(int fx, int fy, int tx, int ty, int ms) {
  if (n_lasers == MAX_LASERS) return;
  laser_t *l = &lasers[n_lasers];
  memset(l, 0, sizeof *l);
  l->fx = fx, l->fy = fy, l->tx = tx, l->ty = ty;
  if (fx - tx > 0) l->g1 = 12, l->g2 = 11, l->go1 = 21, l->go2 = 22, l->lg = 15;
  else if (fx - tx < 0) l->g1 = 11, l->g2 = 12, l->go1 = 21, l->go2 = 22, l->lg = 15;
  else if (fy - ty > 0) l->g1 = 13, l->g2 = 10, l->go1 = 20, l->go2 = 23, l->lg = 14;
  else if (fy - ty < 0) l->g1 = 10, l->g2 = 13, l->go1 = 20, l->go2 = 23, l->lg = 14;
  timer_add(ms, T_LASER, n_lasers++, true);
}
void laser_switch(int i) {
  if (i >= n_lasers) return;
  laser_t *l = &lasers[i];
  int x = l->fx, y = l->fy;
  l->on = !l->on;
  for (int k = 0; k < l->tx - l->fx + (l->ty - l->fy) + 1; k++) {
    change_tile(x, y, l->on ? l->lg : 0, true, false, true, true);
    if (x != l->tx) x++;
    else if (y != l->ty) y++;
  }
  change_tile(l->fx, l->fy, l->on ? l->g1 : l->go1, true, false, true, true);
  change_tile(l->tx, l->ty, l->on ? l->g2 : l->go2, true, false, true, true);
}

/* ---------------------------------------------------------------- the update and the reset (EnvironmentManager) */
void env_update(void) {
  for (int i = 0; i < o_dirt; i++) dirt_update(ord_dirt[i]);
  for (int i = 0; i < n_mines; i++) mine_update(&mines[i]);
  for (int i = 0; i < o_crate; i++) crate_update(ord_crate[i]);
  for (int i = 0; i < n_shooters; i++) shooter_update(i);
  for (int i = 0; i < o_sphere; i++) sphere_update(ord_sphere[i]);
  for (int i = 0; i < o_fall; i++) fall_update(ord_fall[i]);
  for (int i = 0; i < n_suns; i++) sun_update(&suns[i]);
  if (big.on) bigsphere_update();
}
void env_reset(void) {
  for (int i = 0; i < n_resetters; i++) {
    int k = resetters[i] >> 8, j = resetters[i] & 255;
    if (k == R_MINE) mine_reset(&mines[j]);
    else if (k == R_DIRT) dirt_reset(j);
    else if (k == R_CRATE) crate_reset(j);
    else fall_reset(j);
  }
}
/* what draw.c shows: the things still in their Vectors */
int env_dirts(const uint8_t **o) { return *o = ord_dirt, o_dirt; }
int env_crates(const uint8_t **o) { return *o = ord_crate, o_crate; }
int env_falls(const uint8_t **o) { return *o = ord_fall, o_fall; }
int env_buttons(const uint8_t **o) { return *o = ord_button, o_button; }
int env_spheres(const uint8_t **o) { return *o = ord_sphere, o_sphere; }
int env_bombs(const bomb_t **b) { return *b = bombs, n_bombs; }

/* ---------------------------------------------------------------- environment/Bomb */
static void bomb_new(double x, double y) {
  if (n_bombs == MAX_BOMBS) return;
  bomb_t *b = &bombs[n_bombs++];
  b->x = x, b->y = y, b->speedx = 2 + rnd01() * (16 - 2), b->gravity = 4 + rnd01() * (10 - 4), b->rot = 0, b->on = true;
}
static void bomb_explode(bomb_t *b) {
  b->on = false;
  if (b->x >= P.x - 40 && b->x <= P.x + 40 && b->y >= P.y - 40 && b->y <= P.y + 40) player_die();
  particles_create(b->x, b->y, 20, 15, 15, 1, false);
}
static void bomb_update(bomb_t *b) {
  double r = 2 + rnd01() * 4;
  trail_create(b->x + r + 0.5, b->y + r + 0.5, TR_CIRCLE_RED, 0, 0.04, 0.8, 0, 0, 0, r);
  b->x = tw(b->x - b->speedx), b->y = tw(b->y - b->gravity);
  b->gravity -= 0.25, b->speedx *= 0.99;
  b->rot -= b->speedx + b->gravity;
  int cx = (int)(b->x / 20), cy = (int)(b->y / 20);
  if (!node_walkable(collide_right(cx, cy)) || !node_walkable(collide_left(cx, cy)) ||
      !node_walkable(collide_down(cx, cy - 1)) || !node_walkable(collide_up(cx, cy))) {
    bomb_explode(b);
    return;
  }
  if (!P.dead && cx == P.coordx && cy == P.coordy) {
    bomb_explode(b);
    player_die();
  }
}

/* ---------------------------------------------------------------- entities/Boss */
const int16_t boss_points[BOSS_POINTS * 2] = {
    -10, 40, 0, 80, 10, 40, 0, 0, 0, 80, 15, 120, 20, 80, 10, 40, 15, 120, 40, 140, 42, 120, 20, 80, 40, 140, 80, 135, 65,
    110, 42, 120, 80, 135, 110, 110, 95, 90, 65, 110, 110, 110, 120, 65, 110, 40, 95, 90, 120, 65, 125, 25, 110, 0, 110,
    40, 125, 25, 115, -25, 105, -45, 110, 0, 105, -45, 70, -55, 70, -30, 110, 0, 70, -55, 25, -50, 40, -30, 70, -30, 25,
    -50, 0, 0, 10, 40, 40, -30, 20, 80, 40, 50, 40, 50, 25, 8, 42, 120, 55, 80, 40, 50, 40, 50, 65, 110, 55, 80, 55, 80,
    55, 80, 95, 90, 70, 60, 55, 80, 55, 80, 110, 40, 85, 45, 55, 80, 55, 80, 110, 0, 85, 20, 85, 45, 85, 45, 70, -30, 65,
    -10, 85, 20, 85, 20, 40, -30, 55, 10, 65, -10, 65, -10, 40, 50, 55, 10, 55, 10, 55, 10, 55, 10, 70, 60, 70, 60, 70, 60,
    55, 10, 85, 45, 85, 45, 85, 45, 40, -30, 22, 8, 40, 50, 56, 16, 110, 40, 85, 45, 85, 20, 110, 0};
static int boss_jx[BOSS_POINTS], boss_jy[BOSS_POINTS];
static double boss_w, boss_h; /* base.width, base.height */
static int getrand(void) { return (int)(rnd01() * boss.lineoffset - rnd01() * boss.lineoffset); }
static void boss_draw(void) { /* drawboss(): the points' wobble this frame, and the shape's size */
  int r[4] = {getrand(), getrand(), getrand(), getrand()};
  int x0 = 0, x1 = 0, y0 = 0, y1 = 0;
  for (int k = 0; k < BOSS_POINTS; k++) {
    int a = (int)(rnd01() * 3), b = (int)(rnd01() * 3);
    boss_jx[k] = boss_points[k * 2] + r[a], boss_jy[k] = boss_points[k * 2 + 1] + r[b];
    if (boss_jx[k] < x0) x0 = boss_jx[k];
    if (boss_jx[k] > x1) x1 = boss_jx[k];
    if (boss_jy[k] < y0) y0 = boss_jy[k];
    if (boss_jy[k] > y1) y1 = boss_jy[k];
  }
  boss_w = x1 - x0 + 1, boss_h = y1 - y0 + 1;
  boss.jit[0] = x0, boss.jit[1] = y0; /* (the shape's corner: base.x is -width/2 from it) */
}
void boss_draw_points(int *xy) {
  for (int k = 0; k < BOSS_POINTS; k++) xy[k * 2] = boss_jx[k], xy[k * 2 + 1] = boss_jy[k];
}
double boss_csx_get(void) { return boss_csx; }
double boss_width(void) { return boss_w; }
double boss_height(void) { return boss_h; }
void boss_create(void) {
  boss_csx = -1;
  boss_draw();
  boss.x = tw(3600 + boss_w / 2), boss.y = tw(550 + boss_h * boss.sy / 2);
  boss.created = true, boss.intro = true, boss.defeated = false, boss.particletimer = boss.deftimer = 0;
}
void boss_remove(void) {
  if (boss.created) {
    boss.id = 0;
    boss.x = tw(3600 + boss_w / 2), boss.y = tw(650 + boss_h / 2);
    boss.created = false;
    boss.sx = boss.sy = 1, boss.alpha = 1;
    n_bombs = 0;
    for (int i = 0; i < MAX_BULLETS; i++)
      if (bullets[i].owner < 0) bullets[i].on = false;
  }
}
static void boss_hit(void) {
  ++boss.id;
  if (boss.id == 1) boss.lineoffset = 7, boss_csx = -1.1, boss.sy = 1.1;
  else if (boss.id == 2) boss.lineoffset = 6, boss_csx = -1.4, boss.sy = 1.4, boss.homing = true;
  else if (boss.id == 3) boss.lineoffset = 10, boss_csx = -2, boss.sy = 2, boss.homing = true;
}
static void boss_defeat(void) {
  if (boss.id >= 4) {
    particles_create(boss.x, boss.y, 10, 25, 25, 1, false);
    boss.defeated = true, boss.particletimer = boss.deftimer = 0;
  }
}
void boss_update(void) {
  if (!boss.invincible && !boss.defeated) {
    if (boss.x >= P.x - boss_w / 2 && boss.x <= P.x + boss_w / 2 && boss.y >= P.y - boss_h && boss.y <= P.y + boss_h) {
      if (P.y < boss.y - boss_h / 1.5) {
        P.gravity = -40, P.speedx = -P.maxspeedx;
        boss_hit();
        boss.destx = 3550, boss.desty = 600, boss.speed = 15, boss.routineid = 3, boss.ptimer = 0, boss.prate = 100,
        boss.attackid = 0, boss.invincible = true, boss.invincibletimer = 0;
        boss_defeat();
        for (int k = 0; k < 4; k++)
          particles_create(boss.x + rnd01() * 100 - rnd01() * 100, boss.y + rnd01() * 100 - rnd01() * 100, 5, 25, 25,
                           1 + (int)(rnd01() * 6), false);
      } else {
        player_die();
        boss.invincible = true, boss.invincibletimer = 0;
      }
    }
  }
  if (boss.intro) {
    boss.x = tw(boss.x - 1);
    if (boss.x <= 3250) {
      boss.x = 3250, boss.intro = false;
      P.stopMovement = false;
      text_create(TX_BOSS, 0, 20, 0.1, true, 220);
      boss.attackid = boss.routineid = boss.weapon = boss.ptimer = 0, boss.prate = 50, boss.destx = 3350, boss.desty = 600;
      boss.bullettimer = boss.bulletsshot = 0, boss.homing = false, boss.speed = 100, boss.invincible = false,
      boss.invincibletimer = 0;
    }
  } else if (boss.defeated) {
    if (boss.alpha > 0) {
      if (++boss.particletimer >= 5) {
        boss.particletimer = 0;
        particles_create(boss.x + rnd01() * 100 - rnd01() * 100, boss.y + rnd01() * 100 - rnd01() * 100, 10, 25, 25,
                         1 + (int)(rnd01() * 6), false);
      }
      boss.alpha -= 0.001;
    }
    if (++boss.deftimer >= 400) {
      boss_remove();
      boss.deftimer = 0;
      if (!P.dead) {
        playerpoints[8] = 29;
        map_move(3);
      }
    }
  } else {
    boss.x = tw(boss.x - (boss.x - boss.destx) / boss.speed);
    boss.y = tw(boss.y - (boss.y - boss.desty) / boss.speed);
    if (boss.attackid == 1 && ++boss.bullettimer >= boss.bulletrate && boss.bulletsshot < boss.maxbullets) {
      boss.bullettimer = 0, ++boss.bulletsshot;
      if (boss.weapon == 1) bullet_new(boss.x, boss.y - 100, -1, boss.homing);
      else bomb_new(boss.x, boss.y - 100);
    }
    if (++boss.ptimer >= boss.prate) {
      boss.ptimer = 0;
      ++boss.routineid;
      int id = boss.id;
      if (boss.routineid == 1) {
        boss.bulletsshot = 0, boss.attackid = 1, boss.prate = 300, boss.bulletrate = 50;
        if (id == 2) boss.bulletrate = 100, boss.prate = 320;
        else if (id == 3) boss.bulletrate = 80, boss.prate = 550;
        boss.maxbullets = id == 2 ? 1 : id == 3 ? 3 : 4;
        boss.weapon = 1, boss.speed = 80, boss.destx = 3350, boss.desty = 600;
      } else if (boss.routineid == 2) {
        boss.attackid = 1, boss.bulletsshot = 0, boss.maxbullets = id == 2 ? 8 : id == 3 ? 15 : 5;
        boss.prate = 150, boss.bulletrate = 25;
        if (id == 2) boss.bulletrate = 20;
        else if (id == 3) boss.bulletrate = 20, boss.prate = 300;
        boss.speed = 80, boss.destx = 3400, boss.desty = 650, boss.weapon = 2;
      } else if (boss.routineid == 3) {
        boss.attackid = 0, boss.prate = 150, boss.destx = 3300, boss.desty = 800 + id * 50;
      } else if (boss.routineid == 4) {
        boss.attackid = 0, boss.prate = 0;
        boss.routineid = id >= 2 ? 0 : (int)(rnd01() * 2);
      }
    }
    if (++boss.invincibletimer >= 80) boss.invincibletimer = 0, boss.invincible = false;
    for (int i = 0; i < MAX_BULLETS; i++)
      if (bullets[i].on && bullets[i].owner < 0 && bullet_update(&bullets[i])) {
        particles_create(bullets[i].x, bullets[i].y, 2, 10, 10, 3, false);
        bullets[i].on = false;
      }
    for (int i = 0; i < n_bombs; i++) {
      bomb_update(&bombs[i]);
      if (!bombs[i].on) {
        particles_create(bombs[i].x, bombs[i].y, 2, 10, 10, 1, false);
        memmove(&bombs[i], &bombs[i + 1], (size_t)(n_bombs - i - 1) * sizeof bombs[0]);
        n_bombs--, i--;
      }
    }
  }
  boss_draw();
}

/* ---------------------------------------------------------------- effects/Trail */
void trail_create(double x, double y, int kind, int pic, double fade, double alpha, int tint, double rot, int color,
                  double r) {
  if (n_trails == MAX_TRAILS) memmove(trails, trails + 1, (MAX_TRAILS - 1) * sizeof trails[0]), n_trails--;
  trail_t *t = &trails[n_trails++];
  memset(t, 0, sizeof *t);
  t->x = x, t->y = y, t->kind = kind, t->pic = pic, t->fade = fade, t->alpha = alpha, t->tint = tint, t->rot = rot;
  t->color = color, t->r = (float)r, t->scale = 1;
  if (kind == TR_CIRCLE_RED) {
    t->sx = rnd01() - rnd01(), t->sy = 2 + rnd01() - rnd01();
    if (currentrotation == 180) t->sy -= 4;
  } else if (kind == TR_DOT) {
    t->sx = rnd01() - rnd01(), t->sy = rnd01() - rnd01();
  }
  if (tint == 2) { /* the cat's colours as they are now */
    double m[3] = {P.tint.r, P.tint.g, P.tint.b};
    uint8_t *o[3] = {&t->tr, &t->tg, &t->tb};
    for (int k = 0; k < 3; k++) *o[k] = (uint8_t)(m[k] * 32 > 255 ? 255 : m[k] * 32);
  }
}
void trails_update(void) {
  for (int i = 0; i < n_trails; i++) {
    trail_t *t = &trails[i];
    if (t->kind != TR_PIC) t->x += t->sx, t->y += t->sy, t->scale -= 0.01;
    t->alpha -= t->fade;
    if (t->alpha <= 0) {
      memmove(&trails[i], &trails[i + 1], (size_t)(n_trails - i - 1) * sizeof trails[0]);
      n_trails--; /* (the next one waits a frame, as in TrailManager) */
    }
  }
}

/* ---------------------------------------------------------------- effects/Particle */
void particles_create(double x, double y, int amount, int sx, int sy, int type, bool parts) {
  static int part_of; /* (which piece of the cat: the original's five, in turn) */
  part_of = 0;
  for (int i = 0; i < amount; i++) {
    particle_t *p = &particles[n_particles++];
    memset(p, 0, sizeof *p);
    p->x = x, p->y = y, p->alpha = 1, p->type = parts ? 0 : type;
    if (parts) {
      extern int cat_parts[5][4];
      p->pic = part_of, p->px0 = cat_parts[part_of][0], p->py0 = cat_parts[part_of][1], p->pw = cat_parts[part_of][2],
      p->ph = cat_parts[part_of][3];
      p->size = p->pw > p->ph ? p->pw : p->ph;
      if (++part_of >= 5) part_of = 0;
      p->delayrate = 100 + (int)(rnd01() * 200);
    } else {
      p->size = type == 2 || type == 6 ? 2 + (int)(rnd01() * 8) : type == 5 ? 2 + (int)(rnd01() * 2) : 2 + (int)(rnd01() * 4);
      p->delayrate = type == 1 ? 100 + (int)(rnd01() * 200) : (int)(rnd01() * 100);
    }
    p->speedx = rnd01() * sx - rnd01() * sx;
    p->gravity = -(rnd01() * sy);
    if (n_particles >= MAX_PARTICLES) {
      memmove(particles, particles + 1, (MAX_PARTICLES - 1) * sizeof particles[0]);
      n_particles--;
    }
  }
}
void particles_update(void) {
  for (int i = 0; i < n_particles; i++) {
    particle_t *p = &particles[i];
    bool gone = false;
    if (++p->delaytimer >= p->delayrate) p->alpha -= 0.05;
    if (p->alpha <= 0) gone = true;
    int cx = (int)(p->x / 20), cy = (int)(p->y / 20);
    if (currentrotation == 0) cy--;
    else if (currentrotation == 90) cx--, cy--;
    else if (currentrotation == -90) cy--;
    p->gravity += 0.5;
    if (p->gravity >= 20) p->gravity = 20;
    if (p->gravity >= 0 && !node_walkable(collide_down(cx, cy))) {
      if (p->speedx == 0 && p->gravity >= 5) p->speedx += rnd01() * 2 - rnd01() * 2;
      p->gravity = -p->gravity / 2;
      if (p->gravity < 0.5 && p->gravity > -0.5) p->gravity = 0;
      p->onground = true;
    } else {
      p->onground = false;
    }
    if (p->speedx > 0 && !node_walkable(collide_right(cx, cy)) && !p->onground) p->speedx = 0;
    if (p->speedx < 0 && !node_walkable(collide_left(cx, cy)) && !p->onground) p->speedx = 0;
    if (p->gravity < 0 && !node_walkable(collide_up(cx, cy))) p->gravity = 0;
    if (p->gravity > 0 && !node_walkable(collide_down(cx, cy))) p->gravity = 0;
    p->rot += p->speedx;
    p->speedx *= 0.98;
    if (p->speedx < 0.5 && p->speedx > -0.5) p->speedx = 0;
    switch (currentrotation) {
    case 0: p->x += p->speedx, p->y += p->gravity; break;
    case 90: p->y -= p->speedx, p->x += p->gravity; break;
    case 180: p->x -= p->speedx, p->y -= p->gravity; break;
    case -90: p->y += p->speedx, p->x -= p->gravity; break;
    }
    if (p->y >= gh * 20 - p->size) gone = true;
    if (gone) {
      memmove(&particles[i], &particles[i + 1], (size_t)(n_particles - i - 1) * sizeof particles[0]);
      n_particles--;
    }
  }
}

/* ---------------------------------------------------------------- tools/text/TextManager, DisplayText */
int text_create(int text, int x, int y, double speed, bool fade, int delay) {
  if (n_texts == MAX_TEXTS) { /* (full: the oldest that is gone, else the oldest) */
    int k = 0;
    for (int i = 0; i < n_texts; i++)
      if (!texts[i].visible) {
        k = i;
        break;
      }
    memmove(&texts[k], &texts[k + 1], (size_t)(n_texts - k - 1) * sizeof texts[0]);
    n_texts--;
    if (afk_text == k) afk_text = -1;
    else if (afk_text > k) afk_text--;
  }
  dtext_t *t = &texts[n_texts];
  memset(t, 0, sizeof *t);
  t->text = text, t->x = x, t->y = y, t->alpha = 1, t->destx = -1, t->visible = true;
  if (text == TX_SPEC2 || text == TX_SPEC3 || text == TX_SPEC4) t->destx = 0;
  t->logo = text == TX_SPEC3;
  if (fade) t->fading = true, t->fadeout = true, t->fadedelay = delay, t->fadespeed = speed;
  else if (speed != 1) t->fadein = true, t->fadespeed = speed, t->alpha = 0, t->fading = true;
  return n_texts++;
}
void text_screenmessage(int first, int count, int delay) {
  text_create(first + (int)(rnd01() * (count - 1)), 0, 40, 0.05, true, delay);
}
void texts_update(void) {
  for (int i = 0; i < n_texts; i++) {
    dtext_t *t = &texts[i];
    if (t->destx != -1) {
      t->x = tw(t->x + (t->destx - t->x) / 25);
      if (t->x >= t->destx - 1 && t->x <= t->destx + 1) t->destx = -1;
    }
    if (t->fading) {
      if (++t->fadedelaytimer < t->fadedelay) continue;
      if (t->fadein) {
        t->alpha += t->fadespeed;
        if (t->alpha >= 1) {
          t->alpha = 1, t->fadein = false;
          if (t->doublefade && !t->faded) t->fadeout = true, t->faded = true;
          else t->fading = false;
        }
      } else {
        t->alpha -= t->fadespeed;
        if (t->alpha <= 0) {
          t->alpha = 0, t->visible = false, t->fadeout = false;
          if (t->doublefade && !t->faded) t->fadein = true, t->faded = true;
          else t->fading = false;
        }
      }
    }
  }
}
