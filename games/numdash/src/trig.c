/* Triggers of 2.0+ levels: colour channels (fades, copies with HSV,
 * pulses), groups (moves with easing, rotations, opacity fades, on / off),
 * spawning, stopping, items, listeners (count, collision, tap), gameplay
 * rotation and camera triggers. Position triggers come in channels (GD
 * 2.2): the active channel's list fires as the player's forward coordinate
 * passes each one. Easing formulas are cocos2d's, as GD uses them (checked
 * against GD by gdsolver). */
#include "game.h"
#include <math.h>
#include <string.h>

static const uint8_t *rp;
static unsigned rv(void) {
  unsigned v = 0;
  for (int s = 0; s < 28; s += 7) {
    uint8_t b = *rp++;
    v |= (unsigned)(b & 127) << s;
    if (!(b & 128)) break;
  }
  return v;
}
static int rz(void) { unsigned v = rv(); return (v & 1) ? -(int)((v + 1) >> 1) : (int)(v >> 1); }
static uint16_t ticks_of(unsigned ms) { unsigned t = (ms * ND_HZ + 500) / 1000; return (uint16_t)(t > 65535 ? 65535 : t); }

/* the spawn being run: its group remap (dense + 1, as the stream) */
static const uint16_t (*remap)[2];
static unsigned nremap;
static unsigned G(unsigned v) {          /* stream group (dense + 1, 0 none) -> dense, or 0xffff */
  for (unsigned k = 0; k < nremap; k++) if (remap[k][0] == v) { v = remap[k][1]; break; }
  return v ? v - 1 : 0xffff;
}
static unsigned depth;                   /* spawn chains */

/* ------------------------------------------------------------ groups */

bool gset_on(const Game *g, unsigned set) {
  if (!set) return true;
  const uint16_t *s = g->L->ext->gsets + set;
  for (unsigned k = 1; k <= s[0]; k++)
    if (s[k] < MAX_GROUPS && !g->gr[s[k]].on) return false;
  return true;
}

static bool gset_has(const Game *g, unsigned set, unsigned grp) {
  if (!set) return false;
  const uint16_t *s = g->L->ext->gsets + set;
  for (unsigned k = 1; k <= s[0]; k++) if (s[k] == grp) return true;
  return false;
}

void gset_state(const Game *g, unsigned set, float *dx, float *dy, unsigned *alpha, bool *on) {
  *dx = *dy = 0;
  *alpha = 255;
  *on = true;
  if (!set) return;
  const uint16_t *s = g->L->ext->gsets + set;
  for (unsigned k = 1; k <= s[0]; k++) {
    if (s[k] >= MAX_GROUPS) continue;
    const GroupState *gr = &g->gr[s[k]];
    *dx += gr->dx;
    *dy += gr->dy;
    *alpha = *alpha * gr->alpha / 255;
    if (!gr->on) *on = false;
  }
}

/* Did any group of the set move vertically this tick or the last? */
bool gset_moved(const Game *g, unsigned set) {
  if (!set) return false;
  const uint16_t *s = g->L->ext->gsets + set;
  for (unsigned k = 1; k <= s[0]; k++) {
    if (s[k] >= MAX_GROUPS) continue;
    uint16_t m = g->gr[s[k]].moved, d = (uint16_t)((uint16_t)g->tick - m);
    if (m && d <= 1) return true;
  }
  return false;
}

bool gset_rot(const Game *g, unsigned set, float *angle, float *px, float *py) {
  if (!set || !g->nrg) return false;
  const uint16_t *s = g->L->ext->gsets + set;
  for (unsigned j = 0; j < g->nrg; j++)
    for (unsigned k = 1; k <= s[0]; k++)
      if (s[k] == g->rg[j].group) { *angle = g->rg[j].angle; *px = g->rg[j].px; *py = g->rg[j].py; return true; }
  return false;
}

bool trig_anchor(const Game *g, unsigned grp, float *x, float *y) {
  const LevelExt *e = g->L->ext;
  if (!e || grp >= e->ngroups || grp >= MAX_GROUPS || e->anchors[grp][0] == 32767) return false;
  *x = e->anchors[grp][0] + g->gr[grp].dx;
  *y = e->anchors[grp][1] + g->gr[grp].dy;
  return true;
}

/* ------------------------------------------------------------ easing */

static float bounce_out(float u) {
  if (u < 1 / 2.75f) return 7.5625f * u * u;
  if (u < 2 / 2.75f) { u -= 1.5f / 2.75f; return 7.5625f * u * u + .75f; }
  if (u < 2.5f / 2.75f) { u -= 2.25f / 2.75f; return 7.5625f * u * u + .9375f; }
  u -= 2.625f / 2.75f;
  return 7.5625f * u * u + .984375f;
}
static float elastic_out(float u, float per) {
  if (u <= 0 || u >= 1) return u;
  return powf(2, -10 * u) * sinf((u - per / 4) * 6.2831853f / per) + 1;
}
static float elastic_in(float u, float per) {
  if (u <= 0 || u >= 1) return u;
  u -= 1;
  return -powf(2, 10 * u) * sinf((u - per / 4) * 6.2831853f / per);
}

float ease(int kind, float rate, float u) {
  u = u < 0 ? 0 : u > 1 ? 1 : u;
  float p = rate > 0 ? rate : 2;
  switch (kind) {
    case 1: { float t = 2 * u; return t < 1 ? .5f * powf(t, p) : 1 - .5f * powf(2 - t, p); }
    case 2: return powf(u, p);
    case 3: return powf(u, 1 / p);
    case 4: return u < .5f ? elastic_in(2 * u, p) * .5f : elastic_out(2 * u - 1, p) * .5f + .5f;
    case 5: return elastic_in(u, p);
    case 6: return elastic_out(u, p);
    case 7: return u < .5f ? (1 - bounce_out(1 - 2 * u)) * .5f : bounce_out(2 * u - 1) * .5f + .5f;
    case 8: return 1 - bounce_out(1 - u);
    case 9: return bounce_out(u);
    case 10: return u < .5f ? .5f * powf(2, 10 * (2 * u - 1)) : .5f * (2 - powf(2, -10 * (2 * u - 1)));
    case 11: return u == 0 ? 0 : powf(2, 10 * (u - 1));
    case 12: return 1 - powf(2, -10 * u);
    case 13: return -.5f * (cosf(3.14159265f * u) - 1);
    case 14: return 1 - cosf(u * 1.5707963f);
    case 15: return sinf(u * 1.5707963f);
    case 16: {
      float o = 1.70158f * 1.525f;
      u *= 2;
      if (u < 1) return .5f * (u * u * ((o + 1) * u - o));
      u -= 2;
      return .5f * (u * u * ((o + 1) * u + o) + 2);
    }
    case 17: { float o = 1.70158f; return u * u * ((o + 1) * u - o); }
    case 18: { float o = 1.70158f; u -= 1; return u * u * ((o + 1) * u + o) + 1; }
    default: return u;
  }
}

/* ------------------------------------------------------------ channels */

static void chan_set(Channel *c, const uint8_t rgb[3], unsigned op, unsigned dur) {
  memcpy(c->from, c->cur, 3);
  memcpy(c->to, rgb, 3);
  c->op_from = c->op;
  c->op_to = (uint8_t)op;
  c->t = 0;
  c->dur = (uint16_t)dur;
  if (!dur) { memcpy(c->cur, rgb, 3); c->op = (uint8_t)op; }
}

static void chan_ease(Channel *c) {
  if (c->t >= c->dur) { memcpy(c->cur, c->to, 3); c->op = c->op_to; return; }
  c->t++;
  for (int k = 0; k < 3; k++) c->cur[k] = (uint8_t)(c->from[k] + ((int)c->to[k] - c->from[k]) * (int)c->t / (int)c->dur);
  c->op = (uint8_t)(c->op_from + ((int)c->op_to - c->op_from) * (int)c->t / (int)c->dur);
}

void hsv_shift(const uint8_t in[3], int h, int s64, int v64, unsigned flags, uint8_t out[3]) {
  float r = in[0] / 255.f, g = in[1] / 255.f, b = in[2] / 255.f;
  float mx = fmaxf(r, fmaxf(g, b)), mn = fminf(r, fminf(g, b)), d = mx - mn, hh = 0;
  if (d > 0) {
    if (mx == r) hh = fmodf((g - b) / d, 6);
    else if (mx == g) hh = (b - r) / d + 2;
    else hh = (r - g) / d + 4;
    hh *= 60;
  }
  float ss = mx > 0 ? d / mx : 0, vv = mx;
  hh = fmodf(hh + h + 720, 360);
  ss = (flags & CHF_SADD) ? ss + s64 / 64.f : ss * (s64 / 64.f);
  vv = (flags & CHF_VADD) ? vv + v64 / 64.f : vv * (v64 / 64.f);
  ss = ss < 0 ? 0 : ss > 1 ? 1 : ss;
  vv = vv < 0 ? 0 : vv > 1 ? 1 : vv;
  float c = vv * ss, x = c * (1 - fabsf(fmodf(hh / 60, 2) - 1)), m = vv - c, rr, gg, bb;
  int sec = (int)(hh / 60) % 6;
  switch (sec) {
    case 0: rr = c; gg = x; bb = 0; break;
    case 1: rr = x; gg = c; bb = 0; break;
    case 2: rr = 0; gg = c; bb = x; break;
    case 3: rr = 0; gg = x; bb = c; break;
    case 4: rr = x; gg = 0; bb = c; break;
    default: rr = c; gg = 0; bb = x; break;
  }
  out[0] = (uint8_t)((rr + m) * 255 + .5f);
  out[1] = (uint8_t)((gg + m) * 255 + .5f);
  out[2] = (uint8_t)((bb + m) * 255 + .5f);
}

/* ------------------------------------------------------------ start */

static void chan_rewind(Game *g, unsigned c) {
  const LevelExt *x = g->L->ext;
  g->ch_pos[c] = 0xffffffffu;
  if (!x || x->chan_off[c] == 0xffffffffu) return;
  rp = x->trig + x->chan_off[c];
  g->ch_fwd[c] = (float)rz() * 0.125f;
  g->ch_pos[c] = (uint32_t)(rp - x->trig);
}

void trig_start(Game *g) {
  const LevelExt *x = g->L->ext;
  for (unsigned k = 0; k < MAX_GROUPS; k++) g->gr[k] = (GroupState){0, 0, 255, 1, 0};
  for (unsigned c = 0; c < MAX_CHANNELS; c++) {
    Channel *ch = &g->ch[c];
    if (!x || c >= x->ndyn) {
      if (c >= CH_COUNT) memset(ch->cur, 255, 3);
      ch->op = ch->op_to = 255;
      continue;
    }
    const LChan *l = &x->chans[c];
    memcpy(ch->cur, l->rgb, 3);
    memcpy(ch->to, l->rgb, 3);
    ch->op = ch->op_to = l->opacity;
    ch->flags = l->flags;
    ch->copy = l->copy;
    ch->h = l->h;
    ch->s = l->s;
    ch->v = l->v;
  }
  g->nmv = g->npu = g->nfa = g->nro = g->nrg = g->nsp = g->nfo = g->nli = 0;
  memset(g->touch_bits, 0, sizeof(g->touch_bits));
  memset(g->items, 0, sizeof(g->items));
  g->active_ch = 0;
  g->frame = 0;
  g->time_mod = 1;
  g->zoom = g->zoom_from = g->zoom_to = 1;
  g->zoom_t = g->zoom_dur = 0;
  g->cam_static = 0;
  g->cam_off_x = g->cam_off_y = 0;
  g->end_trig = false;
#if ND_EXTRA_LEVELS
  g->no_input = false;
#endif
  for (unsigned c = 0; c < TRIG_CHANNELS; c++) chan_rewind(g, c);
}

/* ------------------------------------------------------------ firing */

static void spawn_group(Game *g, unsigned grp, const uint16_t (*rm)[2], unsigned nrm);

static void stop_group(Game *g, unsigned grp) {
  for (unsigned k = 0; k < g->nmv;) if (gset_has(g, g->mv[k].src, grp)) g->mv[k] = g->mv[--g->nmv]; else k++;
  for (unsigned k = 0; k < g->nro;) if (gset_has(g, g->ro[k].src, grp)) g->ro[k] = g->ro[--g->nro]; else k++;
  for (unsigned k = 0; k < g->nfa;) if (gset_has(g, g->fa[k].src, grp)) g->fa[k] = g->fa[--g->nfa]; else k++;
  for (unsigned k = 0; k < g->nsp;) if (gset_has(g, g->sp[k].src, grp)) g->sp[k] = g->sp[--g->nsp]; else k++;
  for (unsigned k = 0; k < g->nfo;) if (gset_has(g, g->fo[k].src, grp)) g->fo[k] = g->fo[--g->nfo]; else k++;
  for (unsigned k = 0; k < g->nli;) if (gset_has(g, g->li[k].src, grp)) g->li[k] = g->li[--g->nli]; else k++;
}

static RotGroup *rot_group(Game *g, unsigned grp, float px, float py) {
  for (unsigned k = 0; k < g->nrg; k++) if (g->rg[k].group == grp) return &g->rg[k];
  if (g->nrg >= MAX_ROTG) return NULL;
  g->rg[g->nrg] = (RotGroup){(uint16_t)grp, 0, px, py};
  return &g->rg[g->nrg++];
}

static void activate(Game *g, unsigned grp, unsigned on) {
  if (grp >= MAX_GROUPS) return;
  if (on) g->gr[grp].on = 1;
  spawn_group(g, grp, NULL, 0);
}

static void items_changed(Game *g) {
  for (unsigned k = 0; k < g->nli;) {
    Listener *l = &g->li[k];
    if (l->kind == TR_COUNT && l->a < MAX_ITEM_IDS && g->items[l->a] == l->count) {
      unsigned t = l->target, on = l->on;
      g->li[k] = g->li[--g->nli];
      activate(g, t, on);
      continue;
    }
    k++;
  }
}

static bool compare(int a, unsigned op, int b) {
  switch (op) {
    case 0: return a == b;
    case 1: return a > b;
    case 2: return a >= b;
    case 3: return a < b;
    case 4: return a <= b;
    default: return a != b;
  }
}

/* Reads a trigger's fields at rp; applies it only if `apply`. `src`: the
   trigger's own group set (for stop triggers). */
static void fire(Game *g, unsigned kind, bool apply, unsigned src) {
  switch (kind) {
    case TR_FADE: { unsigned v = *rp++; if (apply) g->fade_effect = (uint8_t)v; break; }
    case TR_TRAIL: { unsigned v = *rp++; if (apply) g->trail = v != 0; break; }
    case TR_COLOR: {
      unsigned c = rv();
      uint8_t rgb[3] = {rp[0], rp[1], rp[2]};
      unsigned op = rp[3], fl = rp[4];
      rp += 5;
      unsigned dur = ticks_of(rv());
      unsigned copy = 0xffff;
      int h = 0, s = 64, v = 64;
      unsigned hf = 0;
      if (fl & CHF_COPY) { copy = rv(); h = rz(); s = rz(); v = rz(); hf = *rp++; }
      for (int k = 0; k < 2 && apply; k++) {
        unsigned t = k ? ((fl & 8) && c == CH_BG ? CH_G1 : 255u) : c;
        if (t >= MAX_CHANNELS) continue;
        Channel *ch = &g->ch[t];
        chan_set(ch, rgb, op, dur);
        ch->flags = (uint8_t)((fl & (CHF_BLEND | CHF_P1 | CHF_P2 | CHF_COPY)) | (hf & (CHF_SADD | CHF_VADD)));
        ch->copy = (uint16_t)copy;
        ch->h = (int16_t)h; ch->s = (int16_t)s; ch->v = (int16_t)v;
      }
      break;
    }
    case TR_MOVE: {
      unsigned grp = rv();
      float dx = rz() * .25f, dy = rz() * .25f;
      float dur = (float)rv() * (ND_HZ / 1000.f);
      unsigned easing = rp[0], rate = rp[1], fl = rp[2];
      rp += 3;
      float xmod = 1, ymod = 1;
      if (fl & 3) { xmod = rz() * .01f; ymod = rz() * .01f; }
      if (fl & 4) {
        unsigned to = G(rv()), centre = G(rv()), axis = *rp++;
        float tx, ty, cx, cy;
        if (apply && trig_anchor(g, to, &tx, &ty) && trig_anchor(g, centre != 0xffff ? centre : grp, &cx, &cy)) {
          dx = axis == 2 ? 0 : tx - cx;
          dy = axis == 1 ? 0 : ty - cy;
        } else {
          dx = dy = 0;
        }
      }
      if (grp >= MAX_GROUPS || !apply || g->nmv >= MAX_MOVES) break;
      g->mv[g->nmv++] = (MoveAction){(uint16_t)grp, (uint16_t)src, (uint8_t)easing, (uint8_t)fl, -2, dx, dy, 0, 0, rate / 10.f, dur, xmod, ymod};
      break;
    }
    case TR_ALPHA: {
      unsigned grp = rv(), op = *rp++, dur = ticks_of(rv());
      if (grp >= MAX_GROUPS || !apply) break;
      GroupState *gr = &g->gr[grp];
      for (unsigned k = 0; k < g->nfa; k++) if (g->fa[k].group == grp) { g->fa[k] = g->fa[--g->nfa]; break; }
      if (!dur || g->nfa >= MAX_FADES) { gr->alpha = (uint8_t)op; break; }
      g->fa[g->nfa++] = (FadeAction){(uint16_t)grp, (uint16_t)src, gr->alpha, (uint8_t)op, 0, (uint16_t)dur};
      break;
    }
    case TR_TOGGLE: {
      unsigned grp = rv(), on = *rp++;
      if (grp < MAX_GROUPS && apply) g->gr[grp].on = (uint8_t)on;
      break;
    }
    case TR_PULSE: {
      PulseAction p;
      p.target = (uint16_t)rv();
      memcpy(p.rgb, rp, 3);
      p.flags = rp[3];
      rp += 4;
      p.tin = ticks_of(rv());
      p.hold = ticks_of(rv());
      p.tout = ticks_of(rv());
      p.copy = 0xffff; p.h = 0; p.s = 64; p.v = 64;
      if (p.flags & CHF_COPY) {
        p.copy = (uint16_t)rv();
        p.h = (int16_t)rz(); p.s = (int16_t)rz(); p.v = (int16_t)rz();
        p.flags |= (uint8_t)(*rp++ & (CHF_SADD | CHF_VADD));
      }
      p.t = 0;
      p.src = (uint16_t)src;
      if (!apply) break;
      if (g->npu < MAX_PULSES) g->pu[g->npu++] = p;
      else { memmove(g->pu, g->pu + 1, sizeof(PulseAction) * (MAX_PULSES - 1)); g->pu[MAX_PULSES - 1] = p; }
      break;
    }
    case TR_SPAWN: {
      unsigned grp = G(rv()), delay = ticks_of(rv()), n = *rp++;
      uint16_t rm[4][2];
      for (unsigned k = 0; k < n; k++) {
        unsigned a = rv(), b = rv();
        if (k < 4) { rm[k][0] = (uint16_t)a; rm[k][1] = (uint16_t)b; }
      }
      if (n > 4) n = 4;
      if (!apply || grp >= MAX_GROUPS) break;
      if (!delay) { spawn_group(g, grp, rm, n); break; }
      if (g->nsp >= MAX_SPAWNS) break;
      SpawnAction *a = &g->sp[g->nsp++];
      a->group = (uint16_t)grp; a->src = (uint16_t)src; a->ticks = (uint16_t)delay; a->nremap = (uint8_t)n;
      memcpy(a->remap, rm, sizeof(rm));
      break;
    }
    case TR_STOP: {
      unsigned grp = G(rv());
      if (apply && grp < MAX_GROUPS) stop_group(g, grp);
      break;
    }
    case TR_ROTATE: {
      unsigned grp = G(rv()), centre = G(rv());
      float deg = rz() * .25f, dur = (float)rv() * (ND_HZ / 1000.f);
      unsigned easing = rp[0], rate = rp[1], fl = rp[2];
      rp += 3;
      if (!apply || grp >= MAX_GROUPS || g->nro >= MAX_ROTS) break;
      float px, py;
      if (!trig_anchor(g, centre != 0xffff ? centre : grp, &px, &py)) break;
      g->ro[g->nro++] = (RotAction){(uint16_t)grp, (uint16_t)src, (uint8_t)easing, (uint8_t)fl, -2, deg, 0, rate / 10.f, dur, px, py};
      break;
    }
    case TR_FOLLOW: {
      unsigned grp = G(rv()), fol = G(rv());
      float xm = rz() * .01f, ym = rz() * .01f, dur = (float)rv() * (ND_HZ / 1000.f);
      float lx, ly;
      if (!apply || grp >= MAX_GROUPS || g->nfo >= MAX_FOLLOWS || !trig_anchor(g, fol, &lx, &ly)) break;
      g->fo[g->nfo++] = (FollowAction){(uint16_t)grp, (uint16_t)fol, (uint16_t)src, 0, xm, ym, dur, lx, ly};
      break;
    }
    case TR_COUNT: {
      unsigned item = *rp++;
      int count = rz();
      unsigned grp = G(rv()), on = *rp++;
      if (!apply || grp >= MAX_GROUPS || g->nli >= MAX_LISTEN) break;
      g->li[g->nli++] = (Listener){TR_COUNT, (uint8_t)item, 0, (uint8_t)on, (int16_t)count, (uint16_t)grp, (uint16_t)src};
      items_changed(g);
      break;
    }
    case TR_PICKUP: {
      unsigned item = *rp++;
      int count = rz();
      unsigned fl = *rp++;
      if (!apply || item >= MAX_ITEM_IDS) break;
      g->items[item] = (int16_t)((fl & 1) ? count : g->items[item] + count);
      items_changed(g);
      break;
    }
    case TR_COMPARE: {
      unsigned item = *rp++, op = *rp++;
      int value = rz();
      unsigned yes = G(rv()), no = G(rv());
      if (!apply || item >= MAX_ITEM_IDS) break;
      bool ok = compare(g->items[item] * 100, op, value);
      unsigned t = ok ? yes : no;
      if (t < MAX_GROUPS) spawn_group(g, t, NULL, 0);
      break;
    }
    case TR_COLLISION: {
      unsigned a = rp[0], b = rp[1];
      rp += 2;
      unsigned grp = G(rv()), on = *rp++;
      if (!apply || grp >= MAX_GROUPS || g->nli >= MAX_LISTEN) break;
      g->li[g->nli++] = (Listener){TR_COLLISION, (uint8_t)a, (uint8_t)b, (uint8_t)on, 0, (uint16_t)grp, (uint16_t)src};
      break;
    }
    case TR_TAP: {
      unsigned grp = G(rv()), mode = *rp++;
      if (!apply || grp >= MAX_GROUPS || g->nli >= MAX_LISTEN) break;
      g->li[g->nli++] = (Listener){TR_TAP, 0, 0, (uint8_t)mode, 0, (uint16_t)grp, (uint16_t)src};
      break;
    }
    case TR_GROT: {
      unsigned travel = rp[0], grav = rp[1], fl = rp[2], ch = rp[3];
      rp += 4;
      float vmod = rz() * .01f;
      if (!apply) break;
      game_rotate(g, travel, grav, fl, vmod);
      if ((fl & 1) && ch < TRIG_CHANNELS) g->active_ch = (uint8_t)ch;
      break;
    }
    case TR_END: if (apply) g->end_trig = true; break;
#if ND_EXTRA_LEVELS
    case TR_OPTIONS: { unsigned v = *rp++; if (apply && v) g->no_input = v == 1; break; }   /* (controls off or back on) */
#endif
    case TR_TIMEWARP: { unsigned m = *rp++; if (apply) g->time_mod = m / 100.f; break; }
    case TR_SHAKE: {
      unsigned st = *rp++, dur = rv();
      if (apply) { g->shake_amp = (float)st; g->shake_t = dur / 1000.f; }
      break;
    }
    case TR_CAMERA: {
      unsigned cam = *rp++;
      int a = rz(), b = rz();
      unsigned c = *rp++, dur = rv();
      if (!apply) break;
      if (cam == CAM_STATIC) {
        float x, y;
        if (c || !a) g->cam_static = 0;
        else if (trig_anchor(g, (unsigned)a - 1, &x, &y)) { g->cam_static = 1; g->cam_axis = (uint8_t)b; g->cam_tx = x; g->cam_ty = y; }
      } else if (cam == CAM_ZOOM) {
        g->zoom_from = g->zoom;
        g->zoom_to = a / 100.f;
        g->zoom_ease = (uint8_t)b;
        g->zoom_rate = c / 10.f;
        g->zoom_t = 0;
        g->zoom_dur = (uint16_t)ticks_of(dur);
        if (!g->zoom_dur) g->zoom = g->zoom_to;
      } else {
        g->cam_off_x = (float)a;
        g->cam_off_y = (float)b;
      }
      break;
    }
    default: break;
  }
}

/* Reads one trigger at rp and fires it unless its groups are off. */
static void run_one(Game *g) {
  unsigned kind = *rp++, set = 0;
  if (kind & 0x80) { set = rv(); kind &= 0x7f; }
  fire(g, kind, gset_on(g, set), set);
}

static void spawn_group(Game *g, unsigned grp, const uint16_t (*rm)[2], unsigned nrm) {
  const LevelExt *x = g->L->ext;
  if (!x || grp >= x->ngroups || depth > 6) return;
  const uint16_t (*keep)[2] = remap;
  unsigned keepn = nremap;
  const uint8_t *keep_rp = rp;
  remap = rm;
  nremap = nrm;
  depth++;
  for (unsigned k = x->spawn_first[grp]; k < x->spawn_first[grp + 1]; k++) {
    rp = x->trig + x->spawn_list[k];
    run_one(g);
  }
  depth--;
  remap = keep;
  nremap = keepn;
  rp = keep_rp;
}

void trig_log(Game *g, unsigned kind, unsigned arg, float val) {
  if (!g->L->ext || g->rebuilding || g->nlog >= MAX_LOG) return;
  g->log[g->nlog++] = (LogEntry){(uint16_t)g->tick, (uint8_t)kind, (uint8_t)arg, val};
}

void trig_touch(Game *g, unsigned idx) {
  const LevelExt *x = g->L->ext;
  if (!x || idx >= x->ntouch || idx >= sizeof(g->touch_bits) * 8) return;
  if (g->touch_bits[idx >> 3] >> (idx & 7) & 1) return;
  g->touch_bits[idx >> 3] |= (uint8_t)(1u << (idx & 7));
  trig_log(g, LOG_TOUCH, idx, 0);
  rp = x->trig + x->touch[idx];
  run_one(g);
}

void trig_tap(Game *g) {
  bool any = false;
  for (unsigned k = 0; k < g->nli; k++) {
    if (g->li[k].kind != TR_TAP) continue;
    any = true;
    activate(g, g->li[k].target, 0);
  }
  if (any) trig_log(g, LOG_TAP, 0, 0);
}

void trig_toggle_ring(Game *g, unsigned arg) {
  unsigned grp = arg & 0x3fff;
  if (grp < MAX_GROUPS) g->gr[grp].on = (arg & 0x4000) ? 1 : 0;
  trig_log(g, LOG_TOGGLE, 0, (float)arg);
}

/* ------------------------------------------------------------ per tick */

/* the player's forward coordinate along a frame's travel (world) */
static float forward(const Game *g, unsigned frame) {
  float X, Y;
  to_world(g, g->p.x, g->p.y, &X, &Y);
  switch (frame & 3) {
    case 1: return -Y;
    case 2: return -X;
    case 3: return Y;
    default: return X;
  }
}

static bool colblock_hit(Game *g, unsigned a, unsigned b) {
  /* collision blocks with ids a and b, near the player, overlapping */
  float ax[4], ay[4];
  unsigned na = 0;
  float X, Y;
  to_world(g, g->p.x, g->p.y, &X, &Y);
  LIter it;
  level_iter(g->L, X - 600, X + 900, &it);
  float bx[8], by[8];
  unsigned nb = 0;
  for (const RObj *o; (o = level_next(&it));) {
    if (o->type != OT_COLLISION_BLOCK) continue;
    unsigned id = (unsigned)obj_style(g->L, o)->arg;
    float x, y;
    obj_where(g, o, &x, &y);
    if (id == a && na < 4) { ax[na] = x; ay[na++] = y; }
    if (id == b && nb < 8) { bx[nb] = x; by[nb++] = y; }
  }
  for (unsigned i = 0; i < na; i++)
    for (unsigned j = 0; j < nb; j++)
      if (fabsf(ax[i] - bx[j]) < 30 && fabsf(ay[i] - by[j]) < 30) return true;
  return false;
}

/* Position triggers of the active channel the player has passed. GD fires
   them in its collision pass, before the tick's button: game.c calls this
   between the move and the player's own logic. */
void trig_cross(Game *g) {
  const LevelExt *x = g->L->ext;
  if (!x) return;
  for (int guard = 0; guard < 4; guard++) {
    unsigned c = g->active_ch;
    float f = forward(g, x->chan_frame[c]);
    while (g->ch_pos[c] != 0xffffffffu && g->ch_fwd[c] <= f && g->active_ch == c) {
      rp = x->trig + g->ch_pos[c];
      if (*rp == 0) { g->ch_pos[c] = 0xffffffffu; break; }   /* the channel's end */
      run_one(g);
      g->ch_fwd[c] += (float)rv() * 0.125f;
      g->ch_pos[c] = (uint32_t)(rp - x->trig);
    }
    if (g->active_ch == c) break;   /* else a rotation switched channels: go on there */
  }
}

void trig_step(Game *g, float player_dx) {
  const LevelExt *x = g->L->ext;
  if (!x) return;
  if (g->zoom_t < g->zoom_dur) {
    g->zoom_t++;
    g->zoom = g->zoom_from + (g->zoom_to - g->zoom_from) * ease(g->zoom_ease, g->zoom_rate, (float)g->zoom_t / g->zoom_dur);
  }
  /* delayed spawns */
  for (unsigned k = 0; k < g->nsp;) {
    SpawnAction *a = &g->sp[k];
    if (--a->ticks) { k++; continue; }
    SpawnAction s = *a;
    g->sp[k] = g->sp[--g->nsp];
    spawn_group(g, s.group, s.remap, s.nremap);
  }
  /* moves */
  float pdy = g->p.delta_y;
  for (unsigned k = 0; k < g->nmv;) {
    MoveAction *m = &g->mv[k];
    GroupState *gr = &g->gr[m->group];
    m->t++;
    float e = m->t >= m->dur ? 1 : ease(m->easing, m->rate, (float)m->t / m->dur);
    gr->dx += m->tx * e - m->done_x;
    if (m->ty * e != m->done_y) gr->moved = (uint16_t)g->tick;
    gr->dy += m->ty * e - m->done_y;
    m->done_x = m->tx * e;
    m->done_y = m->ty * e;
    if ((m->flags & 1) && m->t > 0) gr->dx += player_dx * m->xmod;
    if ((m->flags & 2) && m->t > 0 && pdy != 0) { gr->dy += pdy * m->ymod; gr->moved = (uint16_t)g->tick; }
    if (m->t >= m->dur) { *m = g->mv[--g->nmv]; continue; }
    k++;
  }
  /* rotations */
  for (unsigned k = 0; k < g->nro;) {
    RotAction *r = &g->ro[k];
    r->t++;
    float e = r->t >= r->dur ? 1 : ease(r->easing, r->rate, (float)r->t / r->dur);
    RotGroup *rg = rot_group(g, r->group, r->px, r->py);
    if (rg) rg->angle += r->deg * e - r->done;
    r->done = r->deg * e;
    if (r->t >= r->dur) { *r = g->ro[--g->nro]; continue; }
    k++;
  }
  /* follows */
  for (unsigned k = 0; k < g->nfo;) {
    FollowAction *f = &g->fo[k];
    float fx, fy;
    if (trig_anchor(g, f->follow, &fx, &fy)) {
      g->gr[f->group].dx += (fx - f->lx) * f->xmod;
      if (fy != f->ly && f->ymod != 0) g->gr[f->group].moved = (uint16_t)g->tick;
      g->gr[f->group].dy += (fy - f->ly) * f->ymod;
      f->lx = fx;
      f->ly = fy;
    }
    if (++f->t >= f->dur) { *f = g->fo[--g->nfo]; continue; }
    k++;
  }
  /* opacity fades */
  for (unsigned k = 0; k < g->nfa;) {
    FadeAction *a = &g->fa[k];
    a->t++;
    g->gr[a->group].alpha = (uint8_t)(a->from + ((int)a->to - a->from) * (int)a->t / (int)a->dur);
    if (a->t >= a->dur) { g->fa[k] = g->fa[--g->nfa]; continue; }
    k++;
  }
  /* pulses */
  for (unsigned k = 0; k < g->npu;) {
    PulseAction *p = &g->pu[k];
    p->t++;
    if (p->t > p->tin + p->hold + p->tout) { memmove(p, p + 1, sizeof(*p) * (g->npu - k - 1)); g->npu--; continue; }
    k++;
  }
  /* collision listeners (every fourth tick) */
  if (!(g->tick & 3))
    for (unsigned k = 0; k < g->nli;) {
      Listener *l = &g->li[k];
      if (l->kind == TR_COLLISION && colblock_hit(g, l->a, l->b)) {
        unsigned t = l->target, on = l->on;
        g->li[k] = g->li[--g->nli];
        activate(g, t, on);
        continue;
      }
      k++;
    }
  for (unsigned c = 0; c < x->ndyn && c < MAX_CHANNELS; c++) chan_ease(&g->ch[c]);
}

/* Pulse strength 0..1 at its current time. */
float pulse_level(const PulseAction *p) {
  if (p->t < p->tin) return p->tin ? (float)p->t / p->tin : 1;
  if (p->t <= p->tin + p->hold) return 1;
  return p->tout ? 1 - (float)(p->t - p->tin - p->hold) / p->tout : 0;
}
