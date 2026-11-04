#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif
/* The Dark World's cutscene helpers (cut.h). */
#include <string.h>
#include "cut.h"
#include "game.h"
#include "gfx.h"

Obj *mk(uint16_t spr, int x, int y, int name) {
  Obj *o = obj_find(name);
  if (!o || !name) o = obj_new(spr, x, y);
  o->spr = spr, o->x = x * FX, o->y = y * FX, o->name = (uint8_t)name;
  o->flags &= (uint16_t)~(OF_HIDDEN | OF_FLIPX);
  o->hs = o->vs = 0, o->img = 0, o->ispd = 0, o->mt = 0, o->walking = false, o->alpha = 255;
  return o;
}
Obj *act(int name) { return obj_get(name); }
void go_hv(int name, int hs16, int vs16, int ispd16) {
  Obj *o = obj_get(name);
  o->hs = hs16, o->vs = vs16, o->ispd = ispd16, o->mt = 0, o->walking = false;
}
void move_over(int name, int x, int y, int t) {
  Obj *o = obj_get(name);
  if (t < 1) t = 1;
  o->tx = x * FX, o->ty = y * FX;
  o->hs = (o->tx - o->x) / t, o->vs = (o->ty - o->y) / t, o->mt = (int16_t)t, o->walking = false;
}
void halt_act(int name) {
  Obj *o = obj_get(name);
  o->hs = o->vs = 0, o->ispd = 0, o->mt = 0, o->walking = false;
}
void set_spr(int name, uint16_t spr, int ispd16) {
  Obj *o = obj_get(name);
  o->spr = spr, o->img = 0, o->ispd = ispd16;
}
void gone(int name) {
  Obj *o = obj_find(name);
  if (o) obj_free(o);
}

/* ---------------------------------------------------------------- the view */
static struct {
  int32_t x, y;
  int dx, dy, t;
} pan;
void cam_pan(int dx16, int dy16, int t) {
  cam_locked = true;
  pan.x = cam_x * FX, pan.y = cam_y * FX, pan.dx = dx16, pan.dy = dy16, pan.t = t;
}
void cam_pan_to(int x, int y, int t) {
  if (t < 1) t = 1;
  cam_pan((x - cam_x) * FX / t, (y - cam_y) * FX / t, t);
}
bool cam_panning(void) { return pan.t > 0; }
void cam_follow(void) {
  cam_locked = false, pan.t = 0;
}

/* ---------------------------------------------------------------- the party */
static bool markers_on;
void party_markers(void) {
  static const uint16_t ks[4] = {spr_krisd_dark, spr_krisr_dark, spr_krisu_dark, spr_krisl_dark};
  static const uint16_t ss[4] = {spr_susied_dark, spr_susier_dark, spr_susieu_dark, spr_susiel_dark};
  static const uint16_t rs[4] = {spr_ralseid, spr_ralseir, spr_ralseiu, spr_ralseil};
  int x, y;
  mk(ks[pl.dir & 3], pl.x, pl.y, ACT_K);
  if (have_char(CH_SUSIE) && follow_shown()) follower_xy(CH_SUSIE, &x, &y), mk(ss[pl.dir & 3], x, y, ACT_S);
  if (have_char(CH_RALSEI) && follow_shown()) follower_xy(CH_RALSEI, &x, &y), mk(rs[pl.dir & 3], x, y, ACT_R);
  pl.hidden = true, pl.frozen = true;
  follow_show(false);
  markers_on = true;
}
void party_back(void) {
  Obj *k = obj_find(ACT_K);
  if (k) pl.x = k->x / FX, pl.y = k->y / FX;
  gone(ACT_K), gone(ACT_S), gone(ACT_R);
  pl.hidden = false, pl.frozen = false;
  follow_reset();
  follow_show(true);
  markers_on = false;
}

static struct {
  int x, y, t;
} exc;
void exc_over(int x, int y) { exc.x = x, exc.y = y, exc.t = 20; }

void cut_step(void) {
  if (pan.t > 0) {
    pan.x += pan.dx, pan.y += pan.dy, pan.t--;
    cam_x = pan.x / FX, cam_y = pan.y / FX;
  }
  if (exc.t > 0) exc.t--;
}
void cut_draw(void) {
  if (exc.t > 0) gfx_sprite(spr_exc, 0, (exc.x - cam_x) / 2, (exc.y - cam_y) / 2, 0, 255);
}
void cut_reset(void) {
  memset(&pan, 0, sizeof pan);
  exc.t = 0;
  markers_on = false;
}
