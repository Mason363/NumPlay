/* Moto X3M: the level and the bike on the screen (MaxGameView: background, landscape, objects, front). */
#include <math.h>
#include <string.h>
#include "bike.h"
#include "bike_data.h"
#include "fx.h"
#include "gfx.h"
#include "level.h"
#include "view.h"

#define TEX_SCALE 0.5f     /* pictures are kept at half the game's size */
#define MAXPTS 300

static float *scr;          /* 2 * (MAXPTS + 4) floats, on main's stack */
static float bg_dx;
uint32_t view_ticks;

/* the drawings in view this frame, with their transform */
typedef struct {
  uint16_t d;
  int16_t y0, y1;
  float ox, oy, c, s;      /* local to world */
} Vis;
static Vis *vis;            /* VIEW_MAXVIS of them, on main's stack */
static int nvis;
#define MAXVIS VIEW_MAXVIS
_Static_assert(sizeof(Vis) * VIEW_MAXVIS <= VIEW_VIS_BYTES, "the drawings in view need more room");

void view_init(void *visbuf, float *scrbuf) {
  vis = visbuf;
  scr = scrbuf;
}

static inline float sx(float x) { return (x - cam.px) * cam.scale + SCREEN_W / 2 + cam.shx; }
static inline float sy(float y) { return (y - cam.py) * cam.scale + SCREEN_H / 2 + cam.shy; }

void view_reset(void) { bg_dx = 0; }

static const Obj *owner_of(const DrawRec *d) { return d->owner >= 0 ? &lv.objs[d->owner] : 0; }

/* where an owned drawing is: its object's body, or the object's place */
static void pose(const DrawRec *d, float *ox, float *oy, float *c, float *s) {
  const Obj *o = owner_of(d);
  if (!o || (d->kind == D_SPRITE && !o->body)) {
    *ox = *oy = 0, *c = 1, *s = 0;
    return;
  }
  if (o->body) {
    *ox = o->body->pos.x, *oy = o->body->pos.y, *c = o->body->axis.y, *s = o->body->axis.x;
  } else if (d->kind == D_SPRITE) {
    *ox = *oy = 0, *c = 1, *s = 0;
  } else {
    *ox = o->r->x, *oy = o->r->y, *c = cosf(o->r->rot), *s = sinf(o->r->rot);
  }
}

static bool hidden(const DrawRec *d) {
  const Obj *o = owner_of(d);
  if (!o) return false;
  if (o->hidden) return true;
  if (o->body && !(o->body->flags & BF_SPACE) && o->r->type != O_SHAPER && o->r->type != O_PLANK) return true;
  return false;
}

void view_prepare(void) {
  cam.shx = fx_shake_x(), cam.shy = fx_shake_y();
  nvis = 0;
  const DrawRec *ds = lv.draws;
  for (int i = 0; i < lv.h->ndraws && nvis < MAXVIS; i++) {
    const DrawRec *d = &ds[i];
    if (hidden(d)) continue;
    float ox, oy, c, s;
    pose(d, &ox, &oy, &c, &s);
    float x0, y0, x1, y1;
    if (d->kind == D_SPRITE) {
      const float *p = lv.params + d->params;
      const Pic *pc = pic(d->img);
      float r = (pc ? (float)(pc->w > pc->h ? pc->w : pc->h) : 16) / TEX_SCALE * fmaxf(fabsf(p[3]), fabsf(p[4]));
      float px = p[0], py = p[1];
      x0 = ox + px * c - py * s, y0 = oy + px * s + py * c;
      x1 = x0 + r, y1 = y0 + r, x0 -= r, y0 -= r;
    } else {
      float cx = 0.5f * (d->minx + d->maxx), cy = 0.5f * (d->miny + d->maxy);
      float r = 0.5f * hypotf((float)(d->maxx - d->minx), (float)(d->maxy - d->miny)) + 64;
      float wx = ox + cx * c - cy * s, wy = oy + cx * s + cy * c;
      x0 = wx - r, x1 = wx + r, y0 = wy - r, y1 = wy + r;
    }
    if (x1 < cam.x - 16 || x0 > cam.x + cam.w + 16 || y1 < cam.y - 16 || y0 > cam.y + cam.h + 16) continue;
    float ya = sy(y0), yb = sy(y1);
    if (yb < 0 || ya > SCREEN_H) continue;
    Vis *v = &vis[nvis++];
    v->d = (uint16_t)i, v->y0 = (int16_t)fmaxf(-1000, ya), v->y1 = (int16_t)fminf(1000, yb);
    v->ox = ox, v->oy = oy, v->c = c, v->s = s;
  }
}

/* ---- background ---- */

void view_bg_scroll(float target_x) {
  (void)target_x;
}

static void draw_bg(void) {
  int t = lv.h->theme;
  gfx_pic_at(PIC_BG[t][0], 0, 0, 1, 256);
  for (int k = 1; k < 3; k++) {
    int id = PIC_BG[t][k];
    if (id == 0xFFFF) continue;
    const Pic *p = pic(id);
    float part = k == 1 ? -0.2f : -0.4f, y = k == 1 ? 125 : 135;
    if (!gfx_rows(y, y + p->h)) continue;
    float e = bike.x + bg_dx;
    float n = fmodf(-e * part, (float)p->w);
    float x0 = -n;
    for (int i = 0; i < 6; i++) gfx_pic_at(id, floorf(x0 + i * (p->w - 1)), y, 1, 256);
  }
}

/* ---- the level's drawings ---- */

static int to_screen(const Vis *v, const DrawRec *d) {
  const int16_t *q = lv.dpts + 2 * d->pts;
  int n = d->n > MAXPTS ? MAXPTS : d->n;
  for (int i = 0; i < n; i++) {
    float lx = q[2 * i], ly = q[2 * i + 1];
    float wx = v->ox + lx * v->c - ly * v->s, wy = v->oy + lx * v->s + ly * v->c;
    scr[2 * i] = sx(wx), scr[2 * i + 1] = sy(wy);
  }
  return n;
}

/* screen to the drawing's own coordinates, then to its picture: (local - origin) * k */
static Map local_map(const Vis *v, float ox, float oy, float k) {
  /* screen -> world: w = (s - centre) / scale + cam */
  float is = 1 / cam.scale;
  float cx = SCREEN_W / 2 + cam.shx, cy = SCREEN_H / 2 + cam.shy;
  /* world -> local: rotate by -angle about (ox, oy) of the pose */
  Map m;
  float c = v->c, s = v->s;
  /* lx = (wx - px) * c + (wy - py) * s ; ly = -(wx - px) * s + (wy - py) * c */
  float ax = -cx * is + cam.px - v->ox, ay = -cy * is + cam.py - v->oy;
  m.ux = is * c * k, m.uy = is * s * k, m.u0 = (ax * c + ay * s - ox) * k;
  m.vx = -is * s * k, m.vy = is * c * k, m.v0 = (-ax * s + ay * c - oy) * k;
  return m;
}

static void draw_poly(const Vis *v, const DrawRec *d) {
  int n = to_screen(v, d);
  const Pic *p = pic(d->img);
  if (!p) return;
  const float *pr = lv.params + d->params;
  Map m = local_map(v, pr[0], pr[1], TEX_SCALE);
  gfx_poly_pic(scr, n, p, &m, true, 256);
}

/* a strip: the picture repeated along the line, its height across it (Drawing.drawBitmapPath2; ropes the same) */
static void draw_strip(const Vis *v, const DrawRec *d) {
  const Pic *p = pic(d->img);
  if (!p) return;
  const int16_t *q = lv.dpts + 2 * d->pts;
  float tw = p->w / TEX_SCALE, th = p->h / TEX_SCALE;
  bool rope = lv.params[d->params] != 0;
  float along = 0;
  float margin = th * cam.scale;
  float b0 = gfx_y0 - margin, b1 = gfx_y0 + BAND + margin;
  for (int i = 0; i + 1 < d->n; i++) {
    float ax = q[2 * i], ay = q[2 * i + 1], bx = q[2 * i + 2], by = q[2 * i + 3];
    float dx = bx - ax, dy = by - ay, len = sqrtf(dx * dx + dy * dy);
    if (len <= 0) continue;
    {
      /* only the segments that reach this band's rows and the screen */
      float y0 = sy(v->oy + ax * v->s + ay * v->c), y1 = sy(v->oy + bx * v->s + by * v->c);
      float x0 = sx(v->ox + ax * v->c - ay * v->s), x1 = sx(v->ox + bx * v->c - by * v->s);
      if ((y0 < b0 && y1 < b0) || (y0 > b1 && y1 > b1) || (x0 < -margin && x1 < -margin) ||
          (x0 > SCREEN_W + margin && x1 > SCREEN_W + margin)) {
        along += len;
        continue;
      }
    }
    float ux = dx / len, uy = dy / len, nx = -uy * th / 2, ny = ux * th / 2;
    /* pieces of the picture's width */
    float t = 0;
    while (t < len - 0.01f) {
      float step = rope ? len - t : fminf(tw, len - t);
      float x0 = ax + ux * t, y0 = ay + uy * t, x1 = x0 + ux * step, y1 = y0 + uy * step;
      float wq[8] = {x0 - nx, y0 - ny, x1 - nx, y1 - ny, x1 + nx, y1 + ny, x0 + nx, y0 + ny};
      float lo = 1e9f, hi = -1e9f;
      for (int k = 0; k < 4; k++) {
        float lx = wq[2 * k], ly = wq[2 * k + 1];
        float wx = v->ox + lx * v->c - ly * v->s, wy = v->oy + lx * v->s + ly * v->c;
        scr[2 * k] = sx(wx), scr[2 * k + 1] = sy(wy);
        if (scr[2 * k + 1] < lo) lo = scr[2 * k + 1];
        if (scr[2 * k + 1] > hi) hi = scr[2 * k + 1];
      }
      if (gfx_rows(lo, hi)) {
        /* the piece's own axes: u along (from x0), v across (from the top edge) */
        Vis pv = *v;
        float c = v->c * ux - v->s * uy, s = v->s * ux + v->c * uy;
        float tx = x0 - nx, ty = y0 - ny;
        pv.ox = v->ox + tx * v->c - ty * v->s, pv.oy = v->oy + tx * v->s + ty * v->c;
        pv.c = c, pv.s = s;
        float k = rope ? TEX_SCALE : TEX_SCALE * (step < tw ? tw / step : 1);
        Map m = local_map(&pv, rope ? -fmodf(along + t, tw) : 0, 0, TEX_SCALE);
        if (!rope && step < tw) m.ux *= k / TEX_SCALE, m.uy *= k / TEX_SCALE, m.u0 *= k / TEX_SCALE;
        gfx_poly_pic(scr, 4, p, &m, true, 256);
      }
      t += step;
    }
    along += len;
  }
}

static int anim_frame(const DrawRec *d, int frames) {
  const Obj *o = owner_of(d);
  if (frames <= 1) return 0;
  if (d->flags & 1) return (int)(view_ticks % (uint32_t)frames);   /* Dec12: always */
  if (o && o->r->type == O_SAFE) {
    if (!o->active) return 0;
    int f = (int)(o->t1 * 30);
    return f < frames ? f : frames - 1;
  }
  return (int)(view_ticks % (uint32_t)frames);
}

static void draw_sprite(const Vis *v, const DrawRec *d) {
  const float *p = lv.params + d->params;
  const Obj *o = owner_of(d);
  float wx = v->ox + p[0] * v->c - p[1] * v->s, wy = v->oy + p[0] * v->s + p[1] * v->c;
  float ang = atan2f(v->s, v->c) + p[2];
  int frame = anim_frame(d, (int)p[5]);
  int alpha = 256;
  if (o && o->r->type == O_REMOVED) alpha = (int)(o->alpha * 256);
  float k = cam.scale / TEX_SCALE;
  gfx_pic(d->img + frame, sx(wx), sy(wy), k * p[3], k * p[4], ang, alpha);
}

static void draw_water(const Vis *v, const DrawRec *d) {
  /* WaterShape: blue at 40 %, its top edge waving by 4 px every 16 px, a light line on it */
  const int16_t *q = lv.dpts + 2 * d->pts;
  int n = 0;
  float x0 = q[0], y0 = q[1], wdx = q[2] - q[0];
  int c = (int)(wdx / 16);
  if (c < 1) c = 1;
  float u = wdx / c;
  float tmp[2 * (MAXPTS + 40)];
  tmp[2 * n] = x0, tmp[2 * n + 1] = y0, n++;
  for (int i = 0; i < c && n < MAXPTS + 30; i++) {
    float dd = (i + 1) * u;
    tmp[2 * n] = x0 + dd, tmp[2 * n + 1] = y0 + 4 * sinf(0.05f * (lv.wave + dd)), n++;
  }
  int top = n;
  for (int i = 2; i < d->n && n < MAXPTS + 38; i++) tmp[2 * n] = q[2 * i], tmp[2 * n + 1] = q[2 * i + 1], n++;
  for (int i = 0; i < n; i++) {
    float lx = tmp[2 * i], ly = tmp[2 * i + 1];
    float wx = v->ox + lx * v->c - ly * v->s, wy = v->oy + lx * v->s + ly * v->c;
    tmp[2 * i] = sx(wx), tmp[2 * i + 1] = sy(wy);
  }
  gfx_poly_color(tmp, n, rgb565(0x0087FF), 102);
  float w = 2 * cam.scale;
  for (int i = 0; i + 1 < top; i++)
    gfx_line(tmp[2 * i], tmp[2 * i + 1], tmp[2 * i + 2], tmp[2 * i + 3], w < 1 ? 1 : w, rgb565(0xBFE1FF), 256);
}

static void draw_items(int cont) {
  for (int i = 0; i < nvis; i++) {
    const Vis *v = &vis[i];
    const DrawRec *d = &lv.draws[v->d];
    if (d->cont != cont) continue;
    if (v->y1 < gfx_y0 || v->y0 > gfx_y0 + BAND) continue;
    switch (d->kind) {
    case D_POLY: draw_poly(v, d); break;
    case D_STRIP: draw_strip(v, d); break;
    case D_SPRITE: draw_sprite(v, d); break;
    case D_WATER: draw_water(v, d); break;
    }
  }
}

/* motors and pivots (their pictures sit at their joints), geysers */
static void draw_joints(void) {
  float k = cam.scale / TEX_SCALE;
  for (int i = 0; i < lv.nobj; i++) {
    const Obj *o = &lv.objs[i];
    int t = o->r->type;
    if ((t == O_MOTOR || t == O_PIVOT) && o->r->img != 0xFFFF) {
      float x = t == O_PIVOT && !o->body2 ? o->r->x : o->x->gx, y = t == O_PIVOT && !o->body2 ? o->r->y : o->x->gy;
      if (t == O_MOTOR && !o->body2) x = o->r->x, y = o->r->y;
      if (!fx_in_camera(x, y, 64)) continue;
      gfx_pic(o->r->img, sx(x), sy(y), k, k, t == O_MOTOR ? fmodf(o->x->grot, 6.2831853f) : 0, 256);
    } else if (t == O_GEYSER && o->active) {
      const float *p = obj_params(o);
      float w = p[2], c = cosf(o->r->rot), s = sinf(o->r->rot);
      float bx = o->r->x - 0.5f * w * c, by = o->r->y - 0.5f * w * s;
      if (!fx_in_camera(o->r->x, o->r->y, w)) continue;
      gfx_pic(PIC_GEYSERBASE, sx(bx), sy(by), k, k, o->r->rot, 256);
      int f = (int)(view_ticks % 6);
      gfx_pic(PIC_GEYSER_ANIM + f, sx(bx), sy(by), k, k, o->r->rot, 256);
      /* the air: streaks along it */
      for (int j = 0; j < 12; j++) {
        float a = fmodf(view_ticks * (p[5] / 30) / w + j * 0.0833f, 1.0f);
        float off = ((j * 37) % 11 / 10.0f - 0.5f) * p[3] * 0.8f;
        float px = bx + c * a * w - s * off, py = by + s * a * w + c * off;
        gfx_pic(PIC_PARTICLE_BOOST, sx(px), sy(py), k, k, o->r->rot, (int)(256 * (1 - a)));
      }
    }
  }
}

/* ---- the bike ---- */

static void part_pic(int slot, const Body *b) {
  float k = cam.scale / TEX_SCALE;
  gfx_pic(PIC_BIKE[bike.skin - 1][slot], sx(b->pos.x), sy(b->pos.y), k, k, b->rot, 256);
}

static void draw_bike(void) {
  Bike *bk = &bike;
  const Body *f = &bk->frame;
  float k = cam.scale / TEX_SCALE;
  int sk = bk->skin;
  /* wheels, suspensions, the frame */
  part_pic(2, &bk->wheel[0]);
  part_pic(2, &bk->wheel[1]);
  if (!bk->exploded) {
    V2 u = np_local_to_world(f, GEO_SUSPEND_BACK);
    float dx = bk->wheel[0].pos.x - u.x, dy = bk->wheel[0].pos.y - u.y;
    gfx_pic(PIC_BIKE[sk - 1][9], sx(u.x), sy(u.y), k * hypotf(dx, dy) / GEO_SUSPEND_BACK_W, k, atan2f(dy, dx), 256);
  }
  bool front_first = sk == 3 || sk == 6 || sk == 2;
  if (front_first && !bk->exploded)
    gfx_pic(PIC_BIKE[sk - 1][10], sx(bk->wheel[1].pos.x), sy(bk->wheel[1].pos.y), k, k, CAR_axisFrontAngle + f->rot, 256);
  if (bk->boosting) {
    V2 r = np_local_to_world(f, v2(-20, -10));
    gfx_pic(PIC_ROCKET + (int)(view_ticks % 6), sx(r.x), sy(r.y), k, k, f->rot, 256);
  }
  gfx_pic(PIC_BIKE[sk - 1][bk->exploded ? 1 : 0], sx(f->pos.x), sy(f->pos.y), k, k, f->rot, 256);
  if (!front_first && !bk->exploded)
    gfx_pic(PIC_BIKE[sk - 1][10], sx(bk->wheel[1].pos.x), sy(bk->wheel[1].pos.y), k, k, CAR_axisFrontAngle + f->rot, 256);
  /* the rider: the far limbs (after a crash), then his parts in the skin's order */
  static const int slot[NPARTS] = {3, 4, 7, 8, 5, 6, 7, 8, 5, 6};
  if (bk->part[P_HIP2].flags & BF_SPACE) {
    static const int far[4] = {P_ARM2, P_FOREARM2, P_HIP2, P_SHIN2};
    for (int i = 0; i < 4; i++) part_pic(slot[far[i]], &bk->part[far[i]]);
  }
  static const int ord6[6] = {P_HIP, P_CORPSE, P_HEAD, P_SHIN, P_ARM, P_FOREARM};
  static const int ord2[6] = {P_CORPSE, P_HEAD, P_HIP, P_SHIN, P_ARM, P_FOREARM};
  static const int ord[6] = {P_HEAD, P_CORPSE, P_HIP, P_SHIN, P_ARM, P_FOREARM};
  const int *o = sk == 6 ? ord6 : sk == 2 ? ord2 : ord;
  for (int i = 0; i < 6; i++) part_pic(slot[o[i]], &bk->part[o[i]]);
}

static void draw_scene(void) {
  draw_bg();
  draw_items(C_SHAPES);
  draw_items(C_LAND);
  fx_draw_fire();
  draw_items(C_OBJ);
  draw_joints();
  draw_bike();
  fx_draw_back();
  draw_items(C_FRONT);
  fx_draw_front();
}

void view_draw_world(void) { draw_scene(); }
