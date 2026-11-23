/* Crystal spinners (CrystalStaticSpinner) that are not attached to solids,
 * kept as compact records with the room's tiles (by tile row): a room can have over a thousand. One layer
 * entity draws them (with their fillers and black border) and checks the
 * player against them, like each spinner's own PlayerCollider would. */
#include "entities.h"

enum { SC_BLUE, SC_RED, SC_PURPLE };
static uint16_t fg_tex[3][4], bg_tex[3][4], fgb_tex[3][4], bgb_tex[3][4];   /* (b: their borders, tools/pack.py) */
static int nfg[3], nbg[3];

static int area_color(void) {
  int a = g_session.area;
  if (a == 5) return SC_RED;
  if (a == 6) return SC_PURPLE;
  if (a == 9) return g_level.core_mode == 2 ? SC_BLUE : SC_RED;   /* Core: blue when cold */
  return SC_BLUE;
}

static void load_textures(void) {
  static const char *cols[3] = {"blue", "red", "purple"};
  char p[48], q[56];
  for (int c = 0; c < 3; c++) {
    nfg[c] = nbg[c] = 0;
    for (int i = 0; i < 4; i++) {
      char b[24];
      path2(b, "fg_", cols[c], -1);
      path2(p, "danger/crystal/", b, i);
      uint16_t t = res_tex_by_name(p);
      if (t != 0xFFFF) fgb_tex[c][nfg[c]] = res_tex_by_name(path2(q, p, "_border", -1)), fg_tex[c][nfg[c]++] = t;
      path2(b, "bg_", cols[c], -1);
      path2(p, "danger/crystal/", b, i);
      t = res_tex_by_name(p);
      if (t != 0xFFFF) bgb_tex[c][nbg[c]] = res_tex_by_name(path2(q, p, "_border", -1)), bg_tex[c][nbg[c]++] = t;
    }
  }
}

static inline uint32_t hash2(int a, int b) {
  uint32_t h = (uint32_t)a * 0x9E3779B1u ^ (uint32_t)b * 0x85EBCA77u;
  h ^= h >> 15;
  h *= 0x2C1B3C6Du;
  return h ^ (h >> 12);
}

typedef struct { const Room *rm; int i, row; float x, y; uint8_t mask; } Sp;
static void at(const Room *rm, int i, int row, Sp *s) {
  uint16_t v = rm->spin[i];
  s->rm = rm, s->i = i, s->row = row;
  s->x = (float)(rm->x + ((v & 1023) - SPIN_X0) * 8 + ((v >> 10) & 7));
  s->y = (float)(rm->y + (row + rm->spin_row0) * 8 + (v >> 13));
  s->mask = (rm->spin_mask[i >> 1] >> ((i & 1) * 4)) & 15;
}
static inline bool gone(const Room *rm, int i) { return rm->spin_gone[i >> 3] & (1 << (i & 7)); }
/* the rows of spinners whose y can be in [y0, y1] */
static void rows(const Room *rm, float y0, float y1, int *r0, int *r1) {
  *r0 = (int)floorf((y0 - rm->y) / 8) - rm->spin_row0 - 1;
  *r1 = (int)floorf((y1 - rm->y) / 8) - rm->spin_row0 + 2;
  if (*r0 < 0) *r0 = 0;
  if (*r1 > rm->spin_rows) *r1 = rm->spin_rows;
}
/* the room's spinners still there with x in [x0, x1], in the rows around [y0, y1] */
#define FOR_SPINNERS(rm, x0, y0, x1, y1, sp)                                                          \
  for (int r0_, r1_, row_ = (rows(rm, y0, y1, &r0_, &r1_), r0_); row_ < r1_; row_++)              \
    for (int i_ = rm->spin_first[row_]; i_ < rm->spin_first[row_ + 1]; i_++)                        \
      if (!gone(rm, i_) && (at(rm, i_, row_, &sp), sp.x >= (x0) && sp.x <= (x1)))

/* the sprites of one spinner (CreateSprites), drawn into the strip: four quarters of an fg texture, fillers towards
 * neighbours less than 24 px away on its right; pass 0 draws them black, a pixel around (Border) */
typedef struct { Tex fg[4], bg[4], fgb[4], bgb[4]; int nf, nb; bool borders; } Look;
/* a texture's frame centered at strip-relative (cx, cy), turned q quarter turns */
static void blit_centered(uint16_t *strip, int sy0, int sy1, const Tex *t, int cx, int cy, int q, uint8_t f, uint16_t tint) {
  int x0 = cx - t->fw / 2, y0 = cy - t->fh / 2;
  if (q == 0) blit_tex(strip, sy0, sy1, t, x0 + t->ox, y0 + t->oy, f, tint, 255);
  else if (q == 2)   /* half a turn: both flips */
    blit_tex(strip, sy0, sy1, t, x0 + t->fw - t->ox - t->w, y0 + t->fh - t->oy - t->h, f | GF_FLIPX | GF_FLIPY, tint, 255);
  else blit_turned(strip, sy0, sy1, t, x0, y0, q, f, tint);
}
static const int8_t SHIFT_X[4] = {0, 0, -1, 1}, SHIFT_Y[4] = {-1, 1, 0, 0};
/* the sprites of one spinner (CreateSprites), drawn into the strip: four quarters of an fg texture, fillers towards
 * neighbours less than 24 px away on its right; pass 0 draws them black, a pixel around (Border: from their own
 * border textures, but for spinners missing quarters) */
static void draw_spinner(uint16_t *strip, int sy0, int sy1, const Sp *s, const Look *lk, int pass) {
  int fi = (int)(hash2((int)s->x, (int)s->y) % (uint32_t)lk->nf);
  const Tex *fg = &lk->fg[fi];
  static const int8_t qx[4] = {0, 10, 10, 0}, qy[4] = {0, 0, 10, 10}, ox[4] = {12, 2, 2, 12}, oy[4] = {12, 12, 2, 2};
  int X = (int)s->x - g_camx, Y = (int)s->y - g_camy, shifts = pass ? 1 : 4;
  uint8_t f = pass ? 0 : GF_SILHOUETTE;
  uint16_t tint = pass ? 0xFFFF : 0;
  if (pass == 0 && lk->borders && !s->mask) blit_centered(strip, sy0, sy1, &lk->fgb[fi], X, Y, 0, 0, 0xFFFF);
  else if (pass != 1)
    for (int k = 0; k < shifts; k++) {
      int bx = pass ? 0 : SHIFT_X[k], by = pass ? 0 : SHIFT_Y[k];
      if (!s->mask)   /* the four quarters make the whole texture (it is opaque: their overlaps change nothing) */
        blit_centered(strip, sy0, sy1, fg, X + bx, Y + by, 0, f, tint);
      else
        for (int q = 0; q < 4; q++)
          if (!(s->mask & (1 << q)))
            blit_part_ex(strip, sy0, sy1, fg, X - ox[q] + bx, Y - oy[q] + by, qx[q], qy[q], 14, 14, f, tint, 255);
    }
  if (pass == 2 || !lk->nb) return;
  /* each pair once: from the one first in (row, x) order */
  const Room *rm = s->rm;
  for (int row = s->row; row < s->row + 4 && row < rm->spin_rows; row++)
    for (int j = row == s->row ? s->i + 1 : rm->spin_first[row]; j < rm->spin_first[row + 1]; j++) {
      if (gone(rm, j)) continue;
#if NP_SOFT_FLOAT
      /* (no FPU: their places are whole pixels, so the same in integers) */
      int sx = (int)s->x, sy = (int)s->y, ox, oy;
      {
        uint16_t v = rm->spin[j];
        ox = rm->x + ((v & 1023) - SPIN_X0) * 8 + ((v >> 10) & 7), oy = rm->y + (row + rm->spin_row0) * 8 + (v >> 13);
      }
      int ddx = ox - sx, ddy = oy - sy;
      if (ddx * ddx + ddy * ddy >= 24 * 24) continue;
      uint32_t k = hash2(sx + ox, (sy + oy) * 7 + 3);
      int bi = (int)(k % (uint32_t)lk->nb), q = (int)((k >> 8) & 3);
      int mx = ((sx + ox + 1) >> 1) - g_camx, my = ((sy + oy + 1) >> 1) - g_camy;
#else
      Sp o;
      at(rm, j, row, &o);
      float ddx = o.x - s->x, ddy = o.y - s->y;
      if (ddx * ddx + ddy * ddy >= 24 * 24) continue;
      uint32_t k = hash2((int)(s->x + o.x), (int)(s->y + o.y) * 7 + 3);
      int bi = (int)(k % (uint32_t)lk->nb), q = (int)((k >> 8) & 3);
      int mx = (int)floorf((s->x + o.x) / 2 + 0.5f) - g_camx, my = (int)floorf((s->y + o.y) / 2 + 0.5f) - g_camy;
#endif
      if (my + 14 < sy0 || my - 14 >= sy1) continue;
      if (pass == 0 && lk->borders) blit_centered(strip, sy0, sy1, &lk->bgb[bi], mx, my, q, 0, 0xFFFF);
      else
        for (int n = 0; n < shifts; n++)
          blit_centered(strip, sy0, sy1, &lk->bg[bi], mx + (pass ? 0 : SHIFT_X[n]), my + (pass ? 0 : SHIFT_Y[n]), q, f, tint);
    }
}

/* CrystalStaticSpinner with attachToSolid: an entity of its own, a StaticMover riding the solid it overlaps (its
 * sprites shake with it); drawn by the layer below with the others. Its fillers go halfway to the attached spinners on
 * its right less than 24 px away (CreateSprites, from Awake). */
typedef struct {
  float shx, shy;     /* OnShake: where its crystal's images are */
  uint8_t fi, nfill;
  int8_t fdx[6], fdy[6];   /* the fillers: the neighbour's offset (halved when drawn) */
  uint8_t fb[6];           /* their texture, and quarter turns << 4 */
} ASpin;
static const EntClass ASPIN;
__attribute__((noinline, optimize("Os")))   /* (a few: smaller over faster) */
static void draw_attached(uint16_t *strip, int sy0, int sy1, const Ent *e, const ASpin *a, const Look *lk, int pass) {
  int fi = a->fi % lk->nf;
  int cx = (int)floorf(e->x + a->shx + 0.5f) - g_camx, cy = (int)floorf(e->y + a->shy + 0.5f) - g_camy;
  if (pass != 1) {   /* the crystal, or (pass 0) its Border: black a pixel around */
    if (pass == 0 && lk->borders) blit_centered(strip, sy0, sy1, &lk->fgb[fi], cx, cy, 0, 0, 0xFFFF);
    else
      for (int s = 0; s < (pass ? 1 : 4); s++)
        blit_centered(strip, sy0, sy1, &lk->fg[fi], cx + (pass ? 0 : SHIFT_X[s]), cy + (pass ? 0 : SHIFT_Y[s]), 0, pass ? 0 : GF_SILHOUETTE,
                      pass ? 0xFFFF : 0);
  }
  if (pass == 2 || !lk->nb) return;
  for (int i = 0; i < a->nfill; i++) {   /* the fillers (and their Border) */
    int bi = (a->fb[i] & 15) % lk->nb, q = a->fb[i] >> 4;
    int mx = (int)floorf(e->x + a->fdx[i] / 2.f + 0.5f) - g_camx, my = (int)floorf(e->y + a->fdy[i] / 2.f + 0.5f) - g_camy;
    if (my + 14 < sy0 || my - 14 >= sy1) continue;
    if (pass == 0 && lk->borders) blit_centered(strip, sy0, sy1, &lk->bgb[bi], mx, my, q, 0, 0xFFFF);
    else
      for (int s = 0; s < (pass ? 1 : 4); s++)
        blit_centered(strip, sy0, sy1, &lk->bg[bi], mx + (pass ? 0 : SHIFT_X[s]), my + (pass ? 0 : SHIFT_Y[s]), q, pass ? 0 : GF_SILHOUETTE,
                      pass ? 0xFFFF : 0);
  }
}
static bool any_attached(void) {
  for (int i = 0; i < g_nents; i++)
    if (g_ents[i].cls == &ASPIN && g_ents[i].dead != 1) return true;
  return false;
}

/* the dust bunnies (dust.c draws them): those whose x is in [x0, x1], y in [y0, y1] */
void spinners_each(const Room *rm, float x0, float y0, float x1, float y1, void (*fn)(float x, float y, uint16_t nodes, void *ctx),
                   void *ctx) {
  Sp sp;
  FOR_SPINNERS(rm, x0, y0, x1, y1, sp) if (sp.y >= y0 && sp.y <= y1) fn(sp.x, sp.y, rd16(rm->spin_dust + 2 * sp.i), ctx);
}

/* each pass draws every spinner in view straight into the strips (a room can show hundreds: too many to queue) */
static void layer_strip(uint16_t *strip, int sy0, int sy1, void *ctx) {
  int pass = (int)(intptr_t)ctx, col = area_color();
  Look look, *lk = &look;   /* (the cache can have moved them since the frame began: their places now) */
  lk->nf = lk->nb = 0;
  lk->borders = pass == 0;
  for (int i = 0; i < nfg[col]; i++)
    if (tex_get(fg_tex[col][i], &lk->fg[lk->nf])) lk->borders &= tex_get(fgb_tex[col][i], &lk->fgb[lk->nf++]);
  for (int i = 0; i < nbg[col]; i++)
    if (tex_get(bg_tex[col][i], &lk->bg[lk->nb])) lk->borders &= tex_get(bgb_tex[col][i], &lk->bgb[lk->nb++]);
  if (!lk->nf) return;
  float cx = g_level.cam.x, cy = g_level.cam.y, wy0 = (float)(sy0 + g_camy), wy1 = (float)(sy1 + g_camy);
  for (int s = 0; s < 2; s++) {
    const Room *rm = &g_level.rooms[s];
    if (rm->index < 0 || !rm->nspin || rm->spin_dust) continue;
    Sp sp;
    /* InView: within the camera plus 16; those whose sprites (and fillers, 24 px on) reach the strip */
    FOR_SPINNERS(rm, cx - 16, wy0 - 40, cx + 336, wy1 + 16, sp) {
      if (sp.y <= cy - 16 || sp.y >= cy + 196 || sp.x <= cx - 16 || sp.x >= cx + 336) continue;
      if (sp.y + 14 < wy0 - 26 || sp.y - 14 >= wy1) continue;
      draw_spinner(strip, sy0, sy1, &sp, lk, pass);
    }
  }
  for (int i = 0; i < g_nents; i++) {   /* the attached ones */
    Ent *e = &g_ents[i];
    if (e->cls != &ASPIN || e->dead == 1 || !e->visible) continue;
    if (e->y <= cy - 16 || e->y >= cy + 196 || e->x <= cx - 16 || e->x >= cx + 336) continue;   /* InView */
    if (e->y + 14 < wy0 - 26 || e->y - 14 >= wy1) continue;
    draw_attached(strip, sy0, sy1, e, ST(e, ASpin), lk, pass);
  }
}
static void layer_render(Ent *e) {
  int pass = (int)e->eid;   /* 0: border (depth -8498), 1: fillers (-8499), 2: crystals (-8500) */
  int col = area_color();
  bool any = false;
  for (int s = 0; s < 2; s++) any |= g_level.rooms[s].index >= 0 && g_level.rooms[s].nspin && !g_level.rooms[s].spin_dust;
  if (!any && !any_attached()) return;
  Tex t;   /* loaded now (nothing loads while the strips are drawn), and kept for this frame */
  for (int i = 0; i < nfg[col]; i++) tex_get(pass ? fg_tex[col][i] : fgb_tex[col][i], &t), tex_get(fg_tex[col][i], &t);
  for (int i = 0; i < nbg[col]; i++) tex_get(pass ? bg_tex[col][i] : bgb_tex[col][i], &t), tex_get(bg_tex[col][i], &t);
  gfx_custom(layer_strip, (void *)(intptr_t)(pass == 0 ? 0 : pass == 1 ? 1 : 2), 0, VIEW_H);
}

/* the collider: Circle(6) and Hitbox(16, 4, -8, -3) */
static bool hits(const Sp *s, float l, float t, float r, float b) {
  float nx = clampf(s->x, l, r), ny = clampf(s->y, t, b), dx = s->x - nx, dy = s->y - ny;
  if (dx * dx + dy * dy < 36) return true;
  return r > s->x - 8 && l < s->x + 8 && b > s->y - 3 && t < s->y + 1;
}

bool spinners_hit_rect(float l, float t, float r, float b) {
  for (int s = 0; s < 2; s++) {
    const Room *rm = &g_level.rooms[s];
    if (rm->index < 0 || !rm->nspin) continue;
    Sp sp;
    FOR_SPINNERS(rm, l - 8, t - 8, r + 8, b + 8, sp) if (hits(&sp, l, t, r, b)) return true;
  }
  for (int i = 0; i < g_nents; i++) {
    Ent *e = &g_ents[i];
    Sp sp = {.x = e->x, .y = e->y};
    if (e->cls == &ASPIN && e->dead != 1 && e->collidable && hits(&sp, l, t, r, b)) return true;
  }
  return false;
}

static void layer_update(Ent *e) {
  if (e->eid != 2) return;
  Player *p = level_player();
  if (!p || p->dead || p->state == ST_CASSETTEFLY) return;
  Ent *pe = p->ent;
  /* the hurtbox, as the player's PlayerCollider check sees it (8 x 9 above the feet; ducking 8 x 4) */
  float l = pe->x - 4, r = pe->x + 4, t = pe->y - (player_ducking(p) ? 6.f : 11.f);
  float b = t + (player_ducking(p) ? 4.f : 9.f);
  for (int s = 0; s < 2; s++) {
    const Room *rm = &g_level.rooms[s];
    if (rm->index < 0 || !rm->nspin) continue;
    Sp sp;
    FOR_SPINNERS(rm, l - 8, t - 8, r + 8, b + 8, sp) {
      /* Collidable only near the player (within 128) */
      if (fabsf(pe->x - sp.x) >= 128 || fabsf(pe->y - sp.y) >= 128) continue;
      if (hits(&sp, l, t, r, b)) {
        V2 d = v2norm(v2(pe->x - sp.x, pe->y - sp.y));
        player_die(p, d, false);
        if (rm->spin_dust && p->dead) dust_hit(sp.x, sp.y);   /* DustGraphic.OnHitPlayer */
        return;
      }
    }
  }
}

void spinners_destroy_near(float x, float y, float rad) {
  for (int s = 0; s < 2; s++) {
    Room *rm = &g_level.rooms[s];
    if (rm->index < 0 || !rm->nspin) continue;
    Sp sp;
    FOR_SPINNERS(rm, x - rad, y - rad, x + rad, y + rad, sp)
      if (fabsf(sp.x - x) < rad && fabsf(sp.y - y) < rad) rm->spin_gone[sp.i >> 3] |= (uint8_t)(1 << (sp.i & 7));
  }
  for (int i = 0; i < g_nents; i++)
    if (g_ents[i].cls == &ASPIN && fabsf(g_ents[i].x - x) < rad && fabsf(g_ents[i].y - y) < rad) ent_remove(&g_ents[i]);
}

/* the attached spinner's own: the player's death, riding, shaking, the fillers */
__attribute__((noinline)) static bool aspin_collide(const Ent *e, float l, float t, float r, float b) {
  Sp sp = {.x = e->x, .y = e->y};
  return hits(&sp, l, t, r, b);
}
static void aspin_on_player(Ent *e, Player *p) { player_die(p, v2norm(v2(p->ent->x - e->x, p->ent->y - e->y)), false); }
static bool aspin_riding(Ent *e, Ent *p) { return aspin_collide(e, e_left(p), e_top(p), e_right(p), e_bottom(p)); }   /* CollideCheck */
static void aspin_shake(Ent *e, V2 v) { ST(e, ASpin)->shx = v.x, ST(e, ASpin)->shy = v.y; }
static void aspin_enable(Ent *e, bool on) { e->visible = e->collidable = on; }
static void aspin_awake(Ent *e) {
  ASpin *a = ST(e, ASpin);
  uint32_t h = hash2((int)e->x, (int)e->y);
  a->fi = (uint8_t)(h >> 4);
  for (int i = 0; i < g_nents && a->nfill < 6; i++) {
    Ent *o = &g_ents[i];
    float dx = o->x - e->x, dy = o->y - e->y;
    if (o == e || o->cls != &ASPIN || o->dead == 1 || o->x < e->x || dx * dx + dy * dy >= 24 * 24) continue;
    uint32_t k = hash2((int)(e->x + o->x), (int)(e->y + o->y) * 7 + 3);
    a->fdx[a->nfill] = (int8_t)dx, a->fdy[a->nfill] = (int8_t)dy;
    a->fb[a->nfill++] = (uint8_t)((k & 15) | ((k >> 8) & 3) << 4);
  }
}
static const EntClass ASPIN = {.name = "spinner", .size = sizeof(ASpin), .awake = aspin_awake, .on_player = aspin_on_player,
                               .kind = KIND_PCOLLIDE | KIND_STATICMOVER,
                               .more = &(const EntMore){.collide_rect = aspin_collide, .sm_riding = aspin_riding,
                                                        .sm_shake = aspin_shake, .sm_enable = aspin_enable}};
bool spinner_create(const EData *d) {
  if (d->type != ET_spinner || !EAB(d, spinner, attachToSolid)) return false;
  if (dust_spinner_new(d)) return true;   /* (the Celestial Resort's and the Summit's d- rooms': dust.c) */
  Ent *e = ent_new(&ASPIN, d->x, d->y);
  if (e) e->depth = -8500, e->ctype = COL_LIST, e->cx = -8, e->cy = -6, e->cw = 16, e->ch = 12;
  return true;
}

static const EntClass LAYER = {.name = "spinners", .size = 0, .update = layer_update, .render = layer_render};
void spinners_entities(void) {
  load_textures();
  for (int k = 0; k < 3; k++) {
    Ent *e = ent_new(&LAYER, 0, 0);
    if (!e) return;
    e->eid = (uint16_t)k;
    e->depth = k == 0 ? -8498 : k == 1 ? -8499 : -8500;
    e->tags = TAG_GLOBAL | TAG_TRANSITION_UPDATE;
    e->collidable = 0;
    e->dead = 0;
  }
}
