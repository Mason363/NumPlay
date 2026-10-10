#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")   /* not drawn every frame: smaller over faster */
#endif
/* Old Site (chapter 2), ported from the game's classes: dream blocks
 * (DreamBlock), the chasers (BadelineOldsite), touch switches and switch
 * gates, hanging lamps, floating and foreground debris, the dream mirror and
 * its cutscene, the payphone, lookouts (Lookout), invisible barriers
 * and the chapter's triggers. Dream dashing itself is in player.c.
 * The story: Badeline's intro (CS02_BadelineIntro), the phone calls
 * (CS02_DreamingPhonecall, CS02_Ending), the journal (CS02_Journal), Theo
 * at his campfire (NPC02_Theo, Selfie), and BadelineDummy. */
#include <stdlib.h>
#include "npc.h"
#include "talk.h"

/* the story's shared pieces (ch1.c) */
void ch1_talk_add(Ent *owner, Talk *t, int x, int y, int w, int h, V2 draw_at);
void ch1_talk_remove(Ent *owner);
int ch1_counter(const char *name);
void ch1_set_counter(const char *name, int v);
void ch1_flash(uint32_t color);
int ch1_clean_lines(const char *key, char *out, int cap);
bool save_flag(int bit);
void save_set_flag(int bit);
bool textbox_portrait(int *bank, int *idle);

/* ---------------------------------------------------------------- helpers */
static uint16_t tex(const char *p) { return res_tex_by_name(p); }
static float shake_val(void) {   /* Calc.Random.ShakeVector */
  static const int8_t o[5] = {-1, -1, 0, 1, 1};
  return o[rndi(5)];
}
static uint32_t lerp_rgb(uint32_t a, uint32_t b, float t) {
  t = clampf(t, 0, 1);
  int r = (int)(((a >> 16) & 255) + (((int)((b >> 16) & 255) - (int)((a >> 16) & 255)) * t));
  int g = (int)(((a >> 8) & 255) + (((int)((b >> 8) & 255) - (int)((a >> 8) & 255)) * t));
  int bl = (int)((a & 255) + (((int)(b & 255) - (int)(a & 255)) * t));
  return (uint32_t)(r << 16 | g << 8 | bl);
}
static uint32_t scale_rgb(uint32_t c, float k) {
  k = clampf(k, 0, 1);
  return (uint32_t)((int)(((c >> 16) & 255) * k) << 16 | (int)(((c >> 8) & 255) * k) << 8 | (int)((c & 255) * k));
}
static const char *room_of(const Ent *e) {
  const Room *r = &g_level.rooms[e->room];
  return r->info ? str(rd16(r->info)) : "";
}
/* the room an entity is being created in (entities_load sets room_slot) */
static const char *loading_room(void) {
  const Room *r = &g_level.rooms[g_level.room_slot];
  return r->info ? str(rd16(r->info)) : "";
}
/* Session.GetLevelFlag: a room was visited (Level.LoadLevel adds the one it loads before its entities) */
static bool room_visited(const char *name) {
  int i = chapter_find_room(g_session.chapter, name);
  if (i < 0) return false;
  return g_level.rooms[g_level.room_slot].index == i || (g_session.visited[i >> 3] & (1 << (i & 7)));
}
static bool player_on_ground(Player *p) { return actor_on_ground(p->ent, 1); }
/* entity references in states (only 4-byte aligned): an index into g_ents */
static int16_t ent_ref(const Ent *e) { return e ? (int16_t)(e - g_ents) : -1; }
static Ent *ent_at(int16_t i, const EntClass *cls) { return i >= 0 && g_ents[i].cls == cls && g_ents[i].dead != 1 ? &g_ents[i] : NULL; }
static float quad_curve(float a, float c, float b, float t) { return (1 - t) * (1 - t) * a + 2 * (1 - t) * t * c + t * t * b; }

/* drawing straight into a strip (gfx_custom layers): view coordinates */
static inline void sp_plot(uint16_t *strip, int sy0, int sy1, int x, int y, uint16_t c, int a) {
  if (y < sy0 || y >= sy1 || (unsigned)x >= VIEW_W) return;
  uint16_t *d = strip + (y - sy0) * VIEW_W + x;
  *d = a >= 256 ? c : blend565(*d, scale565(c, a), a);
}
static void sp_rect(uint16_t *strip, int sy0, int sy1, int x, int y, int w, int h, uint16_t c, int a) {
  int x0 = x < 0 ? 0 : x, x1 = x + w > VIEW_W ? VIEW_W : x + w;
  int y0 = y < sy0 ? sy0 : y, y1 = y + h > sy1 ? sy1 : y + h;
  if (x0 >= x1 || y0 >= y1 || a <= 0) return;
  uint16_t pc = a >= 256 ? c : scale565(c, a);
  for (int r = y0; r < y1; r++) {
    uint16_t *d = strip + (r - sy0) * VIEW_W + x0;
    if (a >= 256)
      for (int i = x0; i < x1; i++) *d++ = c;
    else
      for (int i = x0; i < x1; i++, d++) *d = blend565(*d, pc, a);
  }
}
static void sp_line(uint16_t *strip, int sy0, int sy1, int x0, int y0, int x1, int y1, uint16_t c) {
  if ((y0 < sy0 && y1 < sy0) || (y0 >= sy1 && y1 >= sy1)) return;
  int dx = abs(x1 - x0), dy = -abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
  for (int guard = 0; guard < 1024; guard++) {
    sp_plot(strip, sy0, sy1, x0, y0, c, 256);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) err += dy, x0 += sx;
    if (e2 <= dx) err += dx, y0 += sy;
  }
}
static inline int vx(float x) { return (int)floorf(x + 0.5f) - g_camx; }
static inline int vy(float y) { return (int)floorf(y + 0.5f) - g_camy; }
/* a stored row of a texture as palette indices (0: transparent), like gfx.c's */
enum { OFF_rowbuf = 0, END_rowbuf = OFF_rowbuf + (int)sizeof(uint8_t[256]) };
#define rowbuf (*(uint8_t(*)[256])(void *)(g_chram[2] + OFF_rowbuf))
static void tex_row_idx(const Tex *t, int r, uint8_t *out) { tex_row(t, r, out); }
/* a small texture's whole frame (fw x fh) as palette indices, and its palette */
static bool tex_frame_idx(uint16_t id, uint8_t *buf, int fw, int fh, const uint16_t **col, const uint8_t **al) {
  Tex t;
  if (!tex_get(id, &t) || t.fw != fw || t.fh != fh || t.w > (int)sizeof rowbuf) return false;
  memset(buf, 0, (size_t)(fw * fh));
  for (int r = 0; r < t.h; r++) {
    tex_row_idx(&t, r, rowbuf);
    for (int i = 0; i < t.w; i++)
      if (t.oy + r < fh && t.ox + i < fw) buf[(t.oy + r) * fw + t.ox + i] = rowbuf[i];
  }
  int n;
  *col = pal_colors(t.pal, &n);
  *al = pal_alpha(t.pal);
  return true;
}
/* a texture a gfx_custom layer reads, loaded now and marked drawn this frame: nothing loads while the
 * strips are drawn (res.c), and what was drawn longest ago goes first */
static void tex_touch(uint16_t id) {
  Tex t;
  if (id != 0xFFFF) tex_get(id, &t);
}
/* a palette entry drawn with a tint (or as a silhouette of it) and an alpha */
static inline void sp_texel(uint16_t *d, const uint16_t *col, const uint8_t *al, uint8_t v, uint16_t tint, int ga, bool sil) {
  int aa = al[v - 1] + (al[v - 1] >> 7);
  uint16_t c = sil ? (aa >= 256 ? tint : scale565(tint, aa)) : tint != 0xFFFF ? mul565(col[v - 1], tint) : col[v - 1];
  if (ga < 256) c = scale565(c, ga), aa = (aa * ga) >> 8;
  *d = aa >= 256 ? c : blend565(*d, c, aa);
}

/* ---------------------------------------------------------------- DreamBlock */
typedef struct {
  V2 start, end;
  Tween tw;
  uint8_t tw_reverse;
  float anim, wobble_from, wobble_to, wobble_ease;
  float white_fill, white_height;
  uint32_t seed;
  uint16_t n;             /* particles */
  uint8_t moving, one_use, active, inside;
  int8_t sx, sy;          /* the Shaker's offset */
} Dream;
static void dream_setup(Ent *e) {
  Dream *d = ST(e, Dream);
  d->n = (uint16_t)(int)(e->cw / 8 * (e->ch / 8) * 0.7f);
  d->seed = (uint32_t)rnd_next(&g_rnd);
}
/* a particle's random values (Setup), from a hash of the block's seed */
static uint32_t dream_hash(uint32_t x) {
  x ^= x >> 16, x *= 0x7feb352du, x ^= x >> 15, x *= 0x846ca68bu, x ^= x >> 16;
  return x;
}
static const uint32_t DREAM_COLORS[3][3] = {{0xFFEF11, 0xFF00D0, 0x08a310}, {0x5fcde4, 0x7fb25e, 0xE0564C}, {0x5b6ee1, 0xCC3B3B, 0x7daa64}};
static const uint8_t DREAM_LAYERS[6] = {0, 1, 1, 2, 2, 2};
/* the particle textures (objects/dreamblock/particles): pixels around the center */
static const int8_t PT_DOT[] = {0, 0};
static const int8_t PT_PLUS[] = {0, -1, -1, 0, 0, 0, 1, 0, 0, 1};
static const int8_t PT_DIAMOND[] = {0, -2, -1, -1, 0, -1, 1, -1, -2, 0, -1, 0, 1, 0, 2, 0, -1, 1, 0, 1, 1, 1, 0, 2};

static float dream_amp(float seed, float index) {
  return (sinf(seed + index / 16.f + sinf(seed * 2.f + index / 32.f) * 6.2831855f) + 1) * 1.5f;
}
static void dream_wobble_line(uint16_t *strip, int sy0, int sy1, Dream *d, V2 from, V2 to, float offset, uint16_t line, uint16_t back) {
  V2 dv = v2sub(to, from);
  float num = v2len(dv);
  V2 n = v2norm(dv), perp = v2(n.y, -n.x);
  float last = 0;
  const int step = 16;
  for (int i = 2; (float)i < num - 2; i += step) {
    float amp = lerpf(dream_amp(d->wobble_from + offset, (float)i), dream_amp(d->wobble_to + offset, (float)i), d->wobble_ease);
    if ((float)(i + step) >= num) amp = 0;
    float len = fminf((float)step, num - 2 - i);
    V2 a = v2add(v2add(from, v2mul(n, (float)i)), v2mul(perp, last));
    V2 b = v2add(v2add(from, v2mul(n, i + len)), v2mul(perp, amp));
    sp_line(strip, sy0, sy1, vx(a.x - perp.x), vy(a.y - perp.y), vx(b.x - perp.x), vy(b.y - perp.y), back);
    sp_line(strip, sy0, sy1, vx(a.x - perp.x * 2), vy(a.y - perp.y * 2), vx(b.x - perp.x * 2), vy(b.y - perp.y * 2), back);
    sp_line(strip, sy0, sy1, vx(a.x), vy(a.y), vx(b.x), vy(b.y), line);
    last = amp;
  }
}
static const EntClass DREAMBLOCK;
static bool dream_on_camera(const Ent *e) {
  float cl = g_level.cam.x, ct = g_level.cam.y;
  return !(e_right(e) < cl || e_left(e) > cl + 320 || e_bottom(e) < ct || e_top(e) > ct + 180);
}
static void dream_draw(uint16_t *strip, int sy0, int sy1, Ent *e) {
  Dream *d = ST(e, Dream);
  float X = e->x + d->sx, Y = e->y + d->sy, W = e->cw, H = e->ch;
  if (vy(Y - 4) >= sy1 || vy(Y + H + 4) < sy0) return;
  uint32_t back = d->active ? 0x000000 : 0x1f2e2d, line = d->active ? 0xFFFFFF : 0x6a8480;
  sp_rect(strip, sy0, sy1, vx(X), vy(Y), (int)W, (int)H, rgb(back), 256);
  /* the stars: a parallax layer each, wrapped inside the block */
  for (int i = 0; i < d->n; i++) {
    uint32_t h1 = dream_hash(d->seed + (uint32_t)i * 3u), h2 = dream_hash(h1 ^ 0x68e31da4u), h3 = dream_hash(h2 + 0x1b56c4e9u);
    int layer = DREAM_LAYERS[h3 % 6];
    float px = (h1 >> 8) * (1.f / 16777216.f) * W, py = (h2 >> 8) * (1.f / 16777216.f) * H;
    float k = 0.3f + 0.25f * layer;
    px += g_level.cam.x * k, py += g_level.cam.y * k;
    px = e->x + fmodf(px - e->x, W);
    if (px < e->x) px += W;
    py = e->y + fmodf(py - e->y, H);
    if (py < e->y) py += H;
    if (!(px >= e->x + 2 && py >= e->y + 2 && px < e_right(e) - 2 && py < e_bottom(e) - 2)) continue;
    int cy = (int)floorf(py + d->sy) - g_camy;
    if (cy + 2 < sy0 || cy - 2 >= sy1) continue;
    int cx = (int)floorf(px + d->sx) - g_camx;
    float toff = ((h3 >> 8) & 0xFFFF) * (1.f / 65536.f);
    uint32_t col;
    int a = 256;
    if (d->active) col = DREAM_COLORS[layer][(h3 >> 4) % 3];
    else {
      float m = 0.5f + layer / 2.f * 0.5f;   /* Color.LightGray * m */
      col = scale_rgb(0xD3D3D3, m);
      a = (int)(m * 256);
    }
    const int8_t *pt;
    int np;
    if (layer == 0) {
      int f = (int)fmodf(toff * 4 + d->anim, 4);   /* particleTextures[3 - f] */
      if (f == 3) pt = PT_DIAMOND, np = 12;
      else if (f == 1) pt = PT_DOT, np = 1;
      else pt = PT_PLUS, np = 5;
    } else if (layer == 1) {
      int f = (int)fmodf(toff * 2 + d->anim, 2);
      if (f == 0) pt = PT_PLUS, np = 5;
      else pt = PT_DOT, np = 1;
    } else
      pt = PT_DOT, np = 1;
    uint16_t c = rgb(col);
    for (int j = 0; j < np; j++) sp_plot(strip, sy0, sy1, cx + pt[2 * j], cy + pt[2 * j + 1], c, a);
  }
  if (d->white_fill > 0) sp_rect(strip, sy0, sy1, vx(X), vy(Y), (int)W, (int)(H * d->white_height), 0xFFFF, (int)(d->white_fill * 256));
  uint32_t lc = line, bc = back;
  if (d->white_fill > 0) lc = lerp_rgb(lc, 0xFFFFFF, d->white_fill), bc = lerp_rgb(bc, 0xFFFFFF, d->white_fill);
  uint16_t l565 = rgb(lc), b565 = rgb(bc);
  dream_wobble_line(strip, sy0, sy1, d, v2(X, Y), v2(X + W, Y), 0, l565, b565);
  dream_wobble_line(strip, sy0, sy1, d, v2(X + W, Y), v2(X + W, Y + H), 0.7f, l565, b565);
  dream_wobble_line(strip, sy0, sy1, d, v2(X + W, Y + H), v2(X, Y + H), 1.5f, l565, b565);
  dream_wobble_line(strip, sy0, sy1, d, v2(X, Y + H), v2(X, Y), 2.5f, l565, b565);
  uint16_t cc = rgb(line);
  sp_rect(strip, sy0, sy1, vx(X), vy(Y), 2, 2, cc, 256);
  sp_rect(strip, sy0, sy1, vx(X + W - 2), vy(Y), 2, 2, cc, 256);
  sp_rect(strip, sy0, sy1, vx(X), vy(Y + H - 2), 2, 2, cc, 256);
  sp_rect(strip, sy0, sy1, vx(X + W - 2), vy(Y + H - 2), 2, 2, cc, 256);
}
/* all the dream blocks of one depth are drawn by one strip layer */
static void dream_layer(uint16_t *strip, int sy0, int sy1, void *ctx) {
  int depth = (int)(intptr_t)ctx;
  for (int i = 0; i < g_nents; i++) {
    Ent *e = &g_ents[i];
    if (e->cls == &DREAMBLOCK && e->dead != 1 && e->visible && e->depth == depth && dream_on_camera(e)) dream_draw(strip, sy0, sy1, e);
  }
}
static uint32_t dream_drawn[2] = {0xFFFFFFFF, 0xFFFFFFFF};
static void dream_render(Ent *e) {
  if (!dream_on_camera(e)) return;
  int k = e->depth == 5000;
  if (dream_drawn[k] == g_frame) return;
  dream_drawn[k] = g_frame;
  gfx_custom(dream_layer, (void *)(intptr_t)e->depth, 0, VIEW_H);
}
static void dream_on_player_exit(Ent *e, Player *p) {
  Dream *d = ST(e, Dream);
  dust_burst(v2(p->ent->x, p->ent->y), atan2f(p->speed.y, p->speed.x), 16);
  if (d->one_use) {   /* OneUseDestroy */
    e->collidable = e->visible = 0;
    plat_static_movers_enable(e, false);
    ent_remove(e);
  }
}
static void dream_update(Ent *e) {
  Dream *d = ST(e, Dream);
  if (d->moving) {   /* the yoyo tween (a component: before Platform.Update) */
    bool end = tween_update(&d->tw);
    float t = ease_sine_inout(d->tw_reverse ? 1 - d->tw.percent : d->tw.percent);
    V2 to = v2(lerpf(d->start.x, d->end.x, t), lerpf(d->start.y, d->end.y, t));
    if (e->collidable) plat_move_to(e, to.x, to.y);
    else {   /* MoveToNaive: no pushing, no riders */
      float ax = to.x - (e->x + e->remx), ay = to.y - (e->y + e->remy);
      e->lift = v2(ax / DT, ay / DT);
      e->remx += ax, e->remy += ay;
      int nx = roundi(e->remx), ny = roundi(e->remy);
      e->remx -= nx, e->remy -= ny;
      e->x += nx, e->y += ny;
      if (nx || ny) plat_static_movers_move(e, v2((float)nx, (float)ny));
    }
    if (end) tween_start(&d->tw, d->tw.duration), d->tw_reverse = !d->tw_reverse;
  }
  plat_update(e);
  if (d->active) {
    d->anim += 6 * DT;
    d->wobble_ease += DT * 2;
    if (d->wobble_ease > 1) {
      d->wobble_ease = 0;
      d->wobble_from = d->wobble_to;
      d->wobble_to = rndf() * 2 * PI_F;
    }
  }
  /* DreamBlock.OnPlayerExit, called by the player when its dream dash ends */
  Player *p = level_player();
  bool in = p && p->state == ST_DREAMDASH && p->dream_block == e;
  if (d->inside && !in && p) dream_on_player_exit(e, p);
  d->inside = in;
}
static void dream_awake(Ent *e) { plat_static_movers_attach(e); }
static void dream_activate_now(Ent *e) {   /* ActivateNoRoutine */
  Dream *d = ST(e, Dream);
  if (d->active) return;
  d->active = 1;
  dream_setup(e);
  d->white_height = 0, d->white_fill = 0;
  d->sx = d->sy = 0;
}
static const EntClass DREAMBLOCK = {.name = "dreamBlock", .size = sizeof(Dream), .update = dream_update, .render = dream_render,
                                    .awake = dream_awake, .kind = KIND_SOLID | KIND_DREAMBLOCK};
static void new_dreamblock(const EData *d) {
  Ent *e = ent_new(&DREAMBLOCK, d->x, d->y);
  if (!e) return;
  ent_box(e, EA(d, dreamBlock, width), EA(d, dreamBlock, height), 0, 0);
  e->safe = 1;
  e->depth = EAB(d, dreamBlock, below) ? 5000 : -11000;
  Dream *s = ST(e, Dream);
  s->wobble_from = rndf() * 2 * PI_F;
  s->wobble_to = rndf() * 2 * PI_F;
  s->one_use = EAB(d, dreamBlock, oneUse);
  s->white_height = 1;
  s->active = g_session.dreamdash;
  if (s->active && d->nnodes) {   /* Added: the tween between the two places */
    s->moving = 1;
    s->start = v2(d->x, d->y);
    s->end = ed_node(d, 0);
    float dur = v2len(v2sub(s->end, s->start)) / 12;
    if (EAB(d, dreamBlock, fastMoving)) dur /= 3;
    tween_start(&s->tw, dur);
  }
  dream_setup(e);
}

/* ---------------------------------------------------------------- PlayerHair (Badeline's) */
#define BHAIR 4   /* PlayerSprite.HairCount */
typedef struct { V2 n[BHAIR]; float wave; } Hair;
static void hair_start(Hair *h, V2 pos, int facing) {   /* PlayerHair.Start */
  for (int i = 0; i < BHAIR; i++) h->n[i] = v2(pos.x - facing * 200, pos.y + 200);
}
/* PlayerHair.AfterUpdate, with the default steps (0, 2), 0.5, 64, 0 */
static void hair_after_update(Hair *h, const Sprite *s, V2 render_pos, int facing) {
  int hx, hy, fr, carry;
  bool has;
  hair_meta(spr_tex(s), &hx, &hy, &fr, &has, &carry);
  h->n[0] = v2(render_pos.x + hx * facing, render_pos.y - 9 * s->sy + hy);
  V2 target = v2add(h->n[0], v2(-facing * 0.5f * 2, 2)), prev = h->n[0];
  for (int i = 1; i < BHAIR; i++) {
    float m = (1 - (float)i / BHAIR * 0.5f) * 64 * DT;
    V2 d = v2sub(target, h->n[i]);
    float l = v2len(d);
    h->n[i] = l <= m ? target : v2add(h->n[i], v2mul(d, m / l));
    V2 dd = v2sub(h->n[i], prev);
    if (v2len(dd) > 3) h->n[i] = v2add(prev, v2mul(v2norm(dd), 3));
    target = v2add(h->n[i], v2(-facing * 0.5f, 2));
    prev = h->n[i];
  }
}
/* the smaller hair nodes (characters/player/hair00 scaled down) are drawn in a strip layer:
 * as affine draws they would use up gfx.c's few affine slots. hair00: an 8x8 disc in 10x10, origin (5, 5) */
static const uint8_t HAIR_SPAN[10][2] = {{0, 0}, {3, 7}, {2, 8}, {1, 9}, {1, 9}, {1, 9}, {1, 9}, {2, 8}, {3, 7}, {0, 0}};
/* cw: a clip rect (view pixels) when not 0 */
typedef struct { float px[BHAIR], py[BHAIR], s[BHAIR]; uint16_t color; int16_t cx, cy, cw, ch; int a; } HairJob;
enum { OFF_hair_jobs = ((END_rowbuf + 7) & ~7), END_hair_jobs = OFF_hair_jobs + (int)sizeof(HairJob[8]) };
#define hair_jobs (*(HairJob(*)[8])(void *)(g_chram[2] + OFF_hair_jobs))
enum { OFF_lamp_px = ((END_hair_jobs + 7) & ~7), END_lamp_px = OFF_lamp_px + (int)sizeof(uint8_t[8 * 24]) };
#define lamp_px (*(uint8_t(*)[8 * 24])(void *)(g_chram[2] + OFF_lamp_px))   /* objects/hanginglamp as indices */
enum { OFF_debris_px = ((END_lamp_px + 7) & ~7), END_debris_px = OFF_debris_px + (int)sizeof(uint8_t[64 * 8]) };
#define debris_px (*(uint8_t(*)[64 * 8])(void *)(g_chram[2] + OFF_debris_px))
static int nhair_jobs;
static uint32_t hair_jobs_frame = 0xFFFFFFFF;
static void hair_disc(uint16_t *strip, int sy0, int sy1, float PX, float PY, float s, uint16_t c, int a, const HairJob *j) {
  int x0 = (int)floorf(PX - 5 * s) - 1, x1 = (int)ceilf(PX + 5 * s) + 1;
  int y0 = (int)floorf(PY - 5 * s) - 1, y1 = (int)ceilf(PY + 5 * s) + 1;
  if (y0 < sy0) y0 = sy0;
  if (y1 > sy1) y1 = sy1;
  for (int Y = y0; Y < y1; Y++) {
    int v = (int)floorf((Y + 0.5f - PY) / s + 5);
    if (v < 0 || v >= 10) continue;
    for (int X = x0; X < x1; X++) {
      int u = (int)floorf((X + 0.5f - PX) / s + 5);
      if (j->cw && (X < j->cx || X >= j->cx + j->cw || Y < j->cy || Y >= j->cy + j->ch)) continue;
      if (u >= HAIR_SPAN[v][0] && u < HAIR_SPAN[v][1]) sp_plot(strip, sy0, sy1, X, Y, c, a);
    }
  }
}
static void hair_layer(uint16_t *strip, int sy0, int sy1, void *ctx) {
  HairJob *j = ctx;
  for (int i = 1; i < BHAIR; i++)
    for (int k = 0; k < 4; k++)
      hair_disc(strip, sy0, sy1, j->px[i] + (k == 0 ? -1 : k == 1 ? 1 : 0), j->py[i] + (k == 2 ? -1 : k == 3 ? 1 : 0), j->s[i], 0, j->a, j);
  for (int i = BHAIR - 1; i >= 1; i--) hair_disc(strip, sy0, sy1, j->px[i], j->py[i], j->s[i], j->color, j->a, j);
}
static void hair_render_into(Hair *h, const Sprite *s, int facing, uint16_t color, float alpha, HairJob *job) {
  int hx, hy, fr, carry;
  bool has;
  hair_meta(spr_tex(s), &hx, &hy, &fr, &has, &carry);
  if (!has || alpha <= 0) return;
  static const uint16_t BANGS[3] = {T_characters_player_bangs00, T_characters_player_bangs01, T_characters_player_bangs02};
  uint8_t a = (uint8_t)(clampf(alpha, 0, 1) * 255);
  h->n[0] = v2(floorf(h->n[0].x), floorf(h->n[0].y));
  uint16_t bangs = BANGS[fr < 3 ? fr : 0];
  float bsx = facing * fabsf(s->sx);
  V2 at = h->n[0];
  bool bangs_in = !job || !job->cw ||
                  (at.x - g_camx >= job->cx && at.x - g_camx < job->cx + job->cw && at.y - g_camy >= job->cy && at.y - g_camy < job->cy + job->ch);
  if (bangs_in) {
  gfx_tex_ex(bangs, at.x - 1, at.y, 5, 5, bsx, 1, 0, 0, a, GF_SILHOUETTE);
  gfx_tex_ex(bangs, at.x + 1, at.y, 5, 5, bsx, 1, 0, 0, a, GF_SILHOUETTE);
  gfx_tex_ex(bangs, at.x, at.y - 1, 5, 5, bsx, 1, 0, 0, a, GF_SILHOUETTE);
  gfx_tex_ex(bangs, at.x, at.y + 1, 5, 5, bsx, 1, 0, 0, a, GF_SILHOUETTE);
  }
  if (!job) {
    if (hair_jobs_frame != g_frame) hair_jobs_frame = g_frame, nhair_jobs = 0;
    if (nhair_jobs < (int)(sizeof hair_jobs / sizeof hair_jobs[0])) job = &hair_jobs[nhair_jobs++], job->cw = 0;
  }
  if (job) {
    HairJob *j = job;
    float y0 = 1e9f, y1 = -1e9f;
    for (int i = 1; i < BHAIR; i++) {
      j->px[i] = floorf(h->n[i].x + 0.5f) - g_camx, j->py[i] = floorf(h->n[i].y + 0.5f) - g_camy;
      j->s[i] = (0.25f + (1 - (float)i / BHAIR) * 0.75f) * fmaxf(fabsf(s->sx), 0.01f);
      y0 = fminf(y0, j->py[i] - 7), y1 = fmaxf(y1, j->py[i] + 7);
    }
    j->color = color, j->a = a + (a >> 7);
    gfx_custom(hair_layer, j, (int)y0, (int)y1);
  }
  if (bangs_in) gfx_tex_ex(bangs, at.x, at.y, 5, 5, bsx, 1, 0, color, a, GF_SILHOUETTE);
}
static void hair_render(Hair *h, const Sprite *s, int facing, uint16_t color, float alpha) { hair_render_into(h, s, facing, color, alpha, NULL); }
/* the player's animations that Badeline's sprite also has */
static const uint8_t BADELINE_ANIMS[][2] = {
    {A_player_idle, A_badeline_idle}, {A_player_idleA, A_badeline_idleA}, {A_player_idleB, A_badeline_idleB},
    {A_player_idleC, A_badeline_idleC}, {A_player_lookUp, A_badeline_lookUp}, {A_player_walk, A_badeline_walk},
    {A_player_push, A_badeline_push}, {A_player_runSlow, A_badeline_runSlow}, {A_player_runFast, A_badeline_runFast},
    {A_player_runStumble, A_badeline_runStumble}, {A_player_dash, A_badeline_dash},
    {A_player_dreamDashIn, A_badeline_dreamDashIn}, {A_player_dreamDashLoop, A_badeline_dreamDashLoop},
    {A_player_dreamDashOut, A_badeline_dreamDashOut}, {A_player_slide, A_badeline_slide},
    {A_player_jumpSlow, A_badeline_jumpSlow}, {A_player_jumpFast, A_badeline_jumpFast},
    {A_player_fallSlow, A_badeline_fallSlow}, {A_player_fallFast, A_badeline_fallFast}, {A_player_tired, A_badeline_tired},
    {A_player_wallslide, A_badeline_wallslide}, {A_player_climbLookBackStart, A_badeline_climbLookBackStart},
    {A_player_climbLookBack, A_badeline_climbLookBack}, {A_player_climbup, A_badeline_climbup},
    {A_player_duck, A_badeline_duck}, {A_player_edge, A_badeline_edge}, {A_player_sleep, A_badeline_sleep},
    {A_player_faint, A_badeline_faint}, {A_player_fainted, A_badeline_fainted}, {A_player_flip, A_badeline_flip},
    {A_player_skid, A_badeline_skid}, {A_player_dangling, A_badeline_dangling}, {A_player_spin, A_badeline_spin},
    {A_player_hug, A_badeline_hug}};
static int badeline_anim(int player_anim) {
  for (unsigned i = 0; i < sizeof BADELINE_ANIMS / 2; i++)
    if (BADELINE_ANIMS[i][0] == player_anim) return BADELINE_ANIMS[i][1];
  return -1;
}

/* ---------------------------------------------------------------- Player.ChaserStates */
/* the player's last 3.2 s: position, animation, facing, depth, one record per Player.Update */
#define CHASE_N 192
enum { OFF_chase_x = ((END_debris_px + 7) & ~7), END_chase_x = OFF_chase_x + (int)sizeof(int16_t[CHASE_N]) };
#define chase_x (*(int16_t(*)[CHASE_N])(void *)(g_chram[2] + OFF_chase_x))
enum { OFF_chase_y = ((END_chase_x + 7) & ~7), END_chase_y = OFF_chase_y + (int)sizeof(int16_t[CHASE_N]) };
#define chase_y (*(int16_t(*)[CHASE_N])(void *)(g_chram[2] + OFF_chase_y))
enum { OFF_chase_t = ((END_chase_y + 7) & ~7), END_chase_t = OFF_chase_t + (int)sizeof(uint8_t[CHASE_N]) };
#define chase_t (*(uint8_t(*)[CHASE_N])(void *)(g_chram[2] + OFF_chase_t))
enum { OFF_chase_anim = ((END_chase_t + 7) & ~7), END_chase_anim = OFF_chase_anim + (int)sizeof(uint8_t[CHASE_N]) };
#define chase_anim (*(uint8_t(*)[CHASE_N])(void *)(g_chram[2] + OFF_chase_anim))
enum { OFF_chase_flags = ((END_chase_anim + 7) & ~7), END_chase_flags = OFF_chase_flags + (int)sizeof(uint8_t[CHASE_N]) };
#define chase_flags (*(uint8_t(*)[CHASE_N])(void *)(g_chram[2] + OFF_chase_flags))
static int chase_head, chase_n;
static float chase_last = -1;
typedef struct { V2 pos; int anim, facing, depth; } ChaseState;
static uint32_t chase_tick(void) { return (uint32_t)(g_level.time_active / DT + 0.5f); }
static void chase_record(void) {
  Player *p = level_player();
  if (!p || g_level.time_active == chase_last) return;
  chase_last = g_level.time_active;
  int i = (chase_head + chase_n) % CHASE_N;
  if (chase_n < CHASE_N) chase_n++;
  else chase_head = (chase_head + 1) % CHASE_N;
  chase_x[i] = (int16_t)p->ent->x, chase_y[i] = (int16_t)p->ent->y;
  chase_t[i] = (uint8_t)(chase_tick() - 1);   /* (recorded before Player.Update: the last frame's) */
  chase_anim[i] = p->spr.anim;
  chase_flags[i] = (uint8_t)((p->facing < 0 ? 1 : 0) | (p->ent->depth == D_PLAYERDREAMDASHING ? 2 : 0));
}
/* Player.GetChasePosition */
static bool chase_get(float time_ago, ChaseState *out) {
  Player *p = level_player();
  if (!p || p->dead) return false;
  uint8_t now = (uint8_t)chase_tick();
  bool older = chase_n == CHASE_N;
  for (int k = 0; k < chase_n; k++) {
    int i = (chase_head + k) % CHASE_N;
    float num = (uint8_t)(now - chase_t[i]) * DT;
    if (num <= time_ago) {
      if (older || time_ago - num < 0.02f) {
        out->pos = v2(chase_x[i], chase_y[i]);
        out->anim = chase_anim[i];
        out->facing = chase_flags[i] & 1 ? -1 : 1;
        out->depth = chase_flags[i] & 2 ? D_PLAYERDREAMDASHING : D_PLAYER;
        return true;
      }
      return false;
    }
    older = true;
  }
  return false;
}
static void recorder_update(Ent *e) { (void)e; chase_record(); }
static const EntClass CHASE_RECORDER = {.name = "chaseRecorder", .size = 0, .update = recorder_update};

/* ---------------------------------------------------------------- BadelineOldsite */
typedef struct {
  Sprite spr;
  Hair hair;
  V2 from, to, end;
  float hover_timer, spr_x, spr_y, scale, color_a, hair_alpha;
  float wait, delay;
  Tween tw, pop;
  uint8_t index, step, following, hovering, ignore_anim, hair_visible, intro;
  int8_t facing;
} Chaser;
enum { CH_WAIT_PLAYER, CH_DELAY, CH_TWEEN, CH_FOLLOW, CH_STOP_WAIT, CH_INTRO };
#define FOLLOW_BEHIND 1.55f
static uint16_t chaser_hair_color(int index) { return rgb(lerp_rgb(0x9B3FB5, 0xFFFFFF, index / 6.f)); }
static void chaser_trail(Ent *e) {
  Chaser *c = ST(e, Chaser);
  if (!level_on_interval(0.1f)) return;
  uint16_t t = spr_tex(&c->spr);
  if (t != 0xFFFF)
    trail_add(e->x + c->spr_x, e->y + c->spr_y, t, c->spr.ox, c->spr.oy, c->scale * c->facing, c->scale, 0xAC3232, 1, e->depth + 1);
}
static void chaser_on_player(Ent *e, Player *p) {
  V2 d = v2sub(v2(p->ent->x, p->ent->y), v2(e->x, e->y));
  player_die(p, v2norm(d), false);
}
static void chaser_start_chasing(Ent *e) {   /* StartChasingRoutine */
  Chaser *c = ST(e, Chaser);
  c->hovering = 1;
  c->step = CH_WAIT_PLAYER;
}
static void bintro_start(Ent *bad, V2 end);
static void chaser_update(Ent *e) {
  Chaser *c = ST(e, Chaser);
  Player *p = level_player();
  ChaseState cs;
  if (p && p->dead && c->step != CH_INTRO) {
    spr_play(&c->spr, A_badeline_laugh, false);
    c->spr_x = sinf(c->hover_timer) * 4;
    c->hovering = 1;
    c->hover_timer += DT * 2;
    if (e->depth != -12500) e->depth = -12500, ents_mark_unsorted();
    chaser_trail(e);
  } else if (c->following && chase_get(FOLLOW_BEHIND + c->delay, &cs)) {
    V2 d = v2sub(cs.pos, v2(e->x, e->y));
    float l = v2len(d), m = 500 * DT;
    if (l <= m) e->x = cs.pos.x, e->y = cs.pos.y;
    else e->x += d.x * m / l, e->y += d.y * m / l;
    if (!c->ignore_anim && cs.anim != 255) {
      int a = badeline_anim(cs.anim);
      if (a >= 0 && a != c->spr.anim) spr_play(&c->spr, a, true);
    }
    if (!c->ignore_anim) c->facing = (int8_t)cs.facing;
    if (e->depth != cs.depth) e->depth = cs.depth, ents_mark_unsorted();
    chaser_trail(e);
  }
  if (c->hovering) {
    c->hover_timer += DT;
    c->spr_y = sinf(c->hover_timer * 2) * 4;
  } else
    c->spr_y = approach(c->spr_y, 0, DT * 4);
  /* base.Update(): the components */
  c->hair.wave += DT * 4;
  spr_update(&c->spr);
  if (c->pop.active) {   /* PopIntoExistence */
    tween_update(&c->pop);
    float t = ease_cube_in(c->pop.percent);
    c->scale = t, c->color_a = t, c->hair_alpha = t;
  }
  if (c->step == CH_INTRO) {   /* CS02_BadelineIntro moves it */
  } else if (c->wait > 0)
    c->wait -= DT;
  else switch (c->step) {
      case CH_WAIT_PLAYER:
        if (!p || p->just_respawned) break;
        c->to = v2(p->ent->x, p->ent->y);
        c->wait = c->delay;
        c->step = CH_DELAY;
        break;
      case CH_DELAY:
        if (!e->visible) {
          e->visible = 1;
          c->scale = 0, c->color_a = 0;
          c->hair_visible = 1, c->hair_alpha = 0;
          tween_start(&c->pop, 0.5f);
        }
        spr_play(&c->spr, A_badeline_fallSlow, false);
        c->hair_visible = 1;
        c->hovering = 0;
        c->from = v2(e->x, e->y);
        tween_start(&c->tw, FOLLOW_BEHIND - 0.1f);
        c->step = CH_TWEEN;
        break;
      case CH_TWEEN: {
        bool end = tween_update(&c->tw);
        float t = ease_cube_in(c->tw.percent);
        e->x = lerpf(c->from.x, c->to.x, t), e->y = lerpf(c->from.y, c->to.y, t);
        if (c->to.x != c->from.x) c->facing = c->to.x > c->from.x ? 1 : -1;
        chaser_trail(e);
        if (!end) break;
        e->collidable = 1;
        c->following = 1;
        c->step = CH_FOLLOW;
        break;
      }
      case CH_FOLLOW:
        if (!strcmp(room_of(e), "2")) {   /* StopChasing */
          Room *rm = &g_level.rooms[e->room];
          float r = (float)(rm->x + 148), b = (float)(rm->y + 168 + 184);
          if (e->x > r) e->x = r;
          if (e->y > b) e->y = b;
          if (e->x == r && e->y == b) {
            c->following = 0;
            c->ignore_anim = 1;
            spr_play(&c->spr, A_badeline_laugh, false);
            c->facing = 1;
            c->wait = 1;
            c->step = CH_STOP_WAIT;
          }
        }
        break;
      case CH_STOP_WAIT:
        particles_emit(PL_MID, &P_BadelineOldsite_P_Vanish, 12, e_center(e), v2(6, 6), 0);
        ent_remove(e);
        break;
    }
  hair_after_update(&c->hair, &c->spr, v2(e->x + c->spr_x, e->y + c->spr_y), c->facing);
}
static void chaser_render(Ent *e) {
  Chaser *c = ST(e, Chaser);
  float x = floorf(e->x + c->spr_x), y = floorf(e->y + c->spr_y);
  if (c->hair_visible) {
    Sprite s = c->spr;
    s.sx = c->scale;
    hair_render(&c->hair, &s, c->facing, chaser_hair_color(c->index), c->hair_alpha);
  }
  Sprite s = c->spr;
  s.sx = c->scale * c->facing, s.sy = c->scale;
  s.alpha = (uint8_t)(clampf(c->color_a, 0, 1) * 255);
  spr_draw(&s, x, y);
}
static const EntClass CHASER = {.name = "darkChaser", .size = sizeof(Chaser), .update = chaser_update, .render = chaser_render,
                                .on_player = chaser_on_player, .kind = KIND_PCOLLIDE};
static int chasers_loaded[2];   /* Level.LoadLevel numbers each room's chasers */
static int chasers_room[2] = {-1, -1};
static void new_chaser(const EData *d) {
  int slot = g_level.room_slot;
  bool normal = g_session.mode == 0;
  const char *room = loading_room();
  if (chasers_room[slot] != d->room || !level_player_ent()) chasers_loaded[slot] = 0, chasers_room[slot] = d->room;
  int index = chasers_loaded[slot]++;
  if (index == 0) {   /* the player's ChaserStates: kept while it lives */
    if (!level_player_ent()) chase_n = 0, chase_head = 0;
    Ent *r = ent_new(&CHASE_RECORDER, 0, 0);
    if (r) r->depth = -1, r->collidable = 0, r->visible = 0;
  }
  /* Added */
  if (normal && (room_visited("11") || !room_visited("3"))) return;
  Ent *e = ent_new(&CHASER, d->x, d->y);
  if (!e) return;
  e->depth = -1;
  ent_box(e, 6, 6, -3, -7);
  e->collidable = 0;
  e->visible = 0;
  Chaser *c = ST(e, Chaser);
  c->index = (uint8_t)index;
  spr_init(&c->spr, SB_badeline);
  spr_play(&c->spr, A_badeline_fallSlow, true);
  c->facing = 1;
  c->scale = 1, c->color_a = 1, c->hair_alpha = 1;
  c->delay = 0.4f * index;
  hair_start(&c->hair, v2(d->x, d->y), 1);
  if (normal && !level_get_flag("evil_maddy_intro") && !strcmp(room, "3")) {
    c->hovering = 0;
    e->visible = 1;
    c->hair_visible = 0;
    spr_play(&c->spr, A_badeline_pretendDead, false);
    c->step = CH_INTRO;
    bintro_start(e, v2(d->x + 8, d->y - 24));   /* Scene.Add(new CS02_BadelineIntro(this)) */
  } else
    chaser_start_chasing(e);
}

/* ---------------------------------------------------------------- TouchSwitch (and its Switch) */
#define TS_INACTIVE 0x5fcde4
#define TS_ACTIVE 0xFFFFFF
#define TS_FINISH 0xf141df
typedef struct {
  float ease, timer, rate, anim_t, k;
  Wiggler wig;
  uint32_t color;          /* icon.Color before its alpha */
  uint8_t activated, finished, frame, spin;
} Touch;
static uint16_t ts_icon[6], ts_container;
static const EntClass TOUCHSWITCH;
static void switch_flag_name(char *buf, const char *room) { path2(buf, "switches_", room, -1); }
static void touch_finish(Ent *e) {   /* Switch.Finish -> OnFinish */
  Touch *t = ST(e, Touch);
  t->finished = 1;
  t->ease = 0;
}
/* Switch.FinishedCheck */
static bool switches_finished_check(void) {
  for (int i = 0; i < g_nents; i++)
    if (g_ents[i].cls == &TOUCHSWITCH && g_ents[i].dead != 1 && !ST(&g_ents[i], Touch)->activated) return false;
  for (int i = 0; i < g_nents; i++)
    if (g_ents[i].cls == &TOUCHSWITCH && g_ents[i].dead != 1) touch_finish(&g_ents[i]);
  return true;
}
/* Switch.Check: the first switch is finished (the switch gates', and Mirror Temple's TouchSwitches gates': ch5.c) */
bool level_switch_check(void) {
  for (int i = 0; i < g_nents; i++)
    if (g_ents[i].cls == &TOUCHSWITCH && g_ents[i].dead != 1) return ST(&g_ents[i], Touch)->finished;
  return false;
}
static void touch_turn_on(Ent *e) {
  Touch *t = ST(e, Touch);
  if (t->activated || t->finished) return;
  t->activated = 1;   /* Switch.Activate -> OnActivate */
  wiggler_start(&t->wig, 0.5f, 4);
  for (int i = 0; i < 32; i++) {
    float a = rndf() * 2 * PI_F;
    particles_emit1(PL_MID, &P_TouchSwitch_P_FireWhite, v2add(v2(e->x, e->y), angle_vec(a, 6)), a);
  }
  t->rate = 4;
  switches_finished_check();
}
static void touch_on_player(Ent *e, Player *p) { (void)p, touch_turn_on(e); }
/* the touch switches a circle reaches (their Collider, 16 x 16): a seeker's regeneration blast turns them on */
void level_touch_switches_in_circle(V2 c, float r) {
  for (int i = 0; i < g_nents; i++) {
    Ent *e = &g_ents[i];
    if (e->cls != &TOUCHSWITCH || e->dead == 1) continue;
    float nx = clampf(c.x, e->x - 8, e->x + 8), ny = clampf(c.y, e->y - 8, e->y + 8);
    if ((nx - c.x) * (nx - c.x) + (ny - c.y) * (ny - c.y) < r * r) touch_turn_on(e);
  }
}
static void touch_awake(Ent *e) {   /* Switch.EntityAdded: CheckLevelFlag -> StartFinished */
  Touch *t = ST(e, Touch);
  char f[48];
  switch_flag_name(f, room_of(e));
  if (level_get_flag(f) && !t->finished) {
    t->activated = t->finished = 1;
    t->rate = 0.1f;
    t->spin = 0, t->frame = 0;
    t->color = TS_FINISH;
    t->ease = 1;
  }
}
static void touch_update(Ent *e) {
  Touch *t = ST(e, Touch);
  /* HoldableCollider (20x20) and SeekerCollider (24x24, inside the camera) */
  for (int i = 0; i < g_nents; i++) {
    Ent *o = &g_ents[i];
    if (!o->cls || o->dead == 1 || o == e) continue;
    bool hold = o->kind & KIND_HOLDABLE, seeker = !hold && o->cls->name && !strcmp(o->cls->name, "seeker");
    if (!hold && !seeker) continue;
    float r = hold ? 10 : 12;
    if (collide_rect(o, e->x - r, e->y - r, e->x + r, e->y + r)) {
      if (hold) touch_turn_on(e);
      else if (e->x > g_level.cam.x - 10 && e->x < g_level.cam.x + 330 && e->y > g_level.cam.y - 10 && e->y < g_level.cam.y + 190)
        touch_turn_on(e);
    }
  }
  t->timer += DT * 8;
  t->ease = approach(t->ease, (t->finished || t->activated) ? 1 : 0, DT * 2);
  t->color = lerp_rgb(TS_INACTIVE, t->finished ? TS_FINISH : TS_ACTIVE, t->ease);
  t->k = 0.5f + (sinf(t->timer) + 1) / 2 * (1 - t->ease) * 0.5f + 0.5f * t->ease;
  if (t->finished) {
    if (t->rate > 0.1f) {
      t->rate -= 2 * DT;
      if (t->rate <= 0.1f) {
        t->rate = 0.1f;
        wiggler_start(&t->wig, 0.5f, 4);
        t->spin = 0, t->frame = 0, t->anim_t = 0;
      }
    } else if (level_on_interval(0.03f)) {
      V2 at = v2add(v2add(v2(e->x, e->y), v2(0, 1)), angle_vec(rndf() * 2 * PI_F, 5));
      particles_emit1(PL_BG, &P_TouchSwitch_P_Fire, at, P_TouchSwitch_P_Fire.direction);
    }
  }
  /* base.Update(): the icon ("spin": 0-5 at 0.1 s, looping) and the wiggler */
  if (t->spin) {
    t->anim_t += DT * t->rate;
    if (fabsf(t->anim_t) >= 0.1f) t->frame = (uint8_t)((t->frame + 1) % 6), t->anim_t -= 0.1f;
  }
  wiggler_update(&t->wig);
}
static void touch_render(Ent *e) {
  Touch *t = ST(e, Touch);
  uint16_t c = rgb(t->color);
  uint8_t a = (uint8_t)(clampf(t->k, 0, 1) * 255);
  float pulse = 1 + t->wig.value * 0.25f;
  gfx_tex_ex(ts_container, e->x, e->y - 1, 10, 10, 1, 1, 0, 0, 255, GF_SILHOUETTE);
  gfx_tex_ex(ts_container, e->x, e->y, 10, 10, pulse, pulse, 0, c, a, 0);
  gfx_tex_ex(ts_icon[t->frame], e->x, e->y, 10, 10, 1, 1, 0, c, a, 0);
  light_add(e->x, e->y, 0xFFFFFF, 0.8f, 16, 32);
  bloom_add(e->x, e->y, t->ease, 16);
}
static const EntClass TOUCHSWITCH = {.name = "touchSwitch", .size = sizeof(Touch), .update = touch_update, .render = touch_render,
                                     .awake = touch_awake, .on_player = touch_on_player, .kind = KIND_PCOLLIDE};
static void new_touchswitch(const EData *d) {
  Ent *e = ent_new(&TOUCHSWITCH, d->x, d->y);
  if (!e) return;
  e->depth = 2000;
  ent_box(e, 30, 30, -15, -15);   /* the PlayerCollider's hitbox */
  Touch *t = ST(e, Touch);
  t->spin = 1, t->rate = 1;
  t->color = TS_INACTIVE;
  char p[40];
  for (int i = 0; i < 6; i++) ts_icon[i] = tex(path2(p, "objects/touchswitch/icon", NULL, i));
  ts_container = tex("objects/touchswitch/container");
}

/* ---------------------------------------------------------------- SwitchGate */
typedef struct {
  V2 start, node;
  float rate, anim_t, wait, tw_left;
  Wiggler wig;
  uint32_t color;
  uint16_t slice;
  uint8_t persistent, step, frame, particle_at, tw_on;
} Gate;
static uint16_t gate_icon[6];
static void gate_dust(Ent *e, bool cond, V2 a, V2 step, V2 inward, V2 spread, int n, float dir) {
  if (!cond) return;
  for (int i = 0; i < n; i++) {
    V2 p = v2add(a, v2mul(step, (float)i));
    if (point_solid(p.x, p.y) && !point_solid(p.x + inward.x, p.y + inward.y)) {
      particles_emit1(PL_FG, &P_SwitchGate_P_Dust, v2add(p, spread), dir);
      particles_emit1(PL_FG, &P_SwitchGate_P_Dust, v2sub(p, spread), dir);
    }
  }
  (void)e;
}
static void gate_update(Ent *e) {
  Gate *g = ST(e, Gate);
  /* components: the tween, then the Sequence coroutine, then the icon and wiggler */
  if (g->tw_on) {
    g->tw_left -= DT;
    float t = ease_cube_out(1 - fmaxf(0, g->tw_left) / 2);
    plat_move_to(e, lerpf(g->start.x, g->node.x, t), lerpf(g->start.y, g->node.y, t));
    if (level_on_interval(0.1f)) {
      g->particle_at = (uint8_t)((g->particle_at + 1) % 2);
      for (int i = 0; (float)i < e->cw / 8; i++)
        for (int j = 0; (float)j < e->ch / 8; j++)
          if ((i + j) % 2 == g->particle_at)
            particles_emit1(PL_BG, &P_SwitchGate_P_Behind, v2(e->x + i * 8 + rnd_rangef(2, 6), e->y + j * 8 + rnd_rangef(2, 6)),
                            P_SwitchGate_P_Behind.direction);
    }
    if (g->tw_left <= 0) g->tw_on = 0;
  }
  if (g->wait > 0) g->wait -= DT;
  else switch (g->step) {
      case 0:   /* while (!Switch.Check) */
        if (!level_switch_check()) break;
        if (g->persistent) {
          char f[48];
          switch_flag_name(f, room_of(e));
          level_set_flag(f, true);
        }
        g->wait = 0.1f, g->step = 1;
        break;
      case 1:
        plat_start_shaking(e, 0.5f);
        g->step = 2;
        /* fall through */
      case 2:
        if (g->rate < 1) {
          g->color = lerp_rgb(TS_INACTIVE, TS_ACTIVE, g->rate);
          g->rate += DT * 2;
          break;
        }
        g->wait = 0.1f, g->step = 3;
        break;
      case 3:
        g->tw_on = 1, g->tw_left = 2;   /* Tween.Create(Oneshot, CubeOut, 2) */
        g->particle_at = 0;
        g->wait = 1.8f, g->step = 4;
        break;
      case 4: {
        uint8_t was = e->collidable;
        e->collidable = 0;
        float l = e_left(e), r = e_right(e), t = e_top(e), b = e_bottom(e);
        int nh = (int)(e->ch / 8), nw = (int)(e->cw / 8);
        gate_dust(e, g->node.x <= g->start.x, v2(l - 1, t + 4), v2(0, 8), v2(1, 0), v2(0, 2), nh, PI_F);
        gate_dust(e, g->node.x >= g->start.x, v2(r + 1, t + 4), v2(0, 8), v2(-2, 0), v2(0, 2), nh, 0);
        gate_dust(e, g->node.y <= g->start.y, v2(l + 4, t - 1), v2(8, 0), v2(0, 1), v2(2, 0), nw, -PI_F / 2);
        gate_dust(e, g->node.y >= g->start.y, v2(l + 4, b + 1), v2(8, 0), v2(0, -2), v2(2, 0), nw, PI_F / 2);
        e->collidable = was;
        plat_start_shaking(e, 0.2f);
        g->step = 5;
      }
        /* fall through */
      case 5:
        if (g->rate > 0) {
          g->color = lerp_rgb(TS_ACTIVE, TS_FINISH, 1 - g->rate);
          g->rate -= DT * 4;
          break;
        }
        g->rate = 0;
        g->frame = 0, g->anim_t = 0;
        wiggler_start(&g->wig, 0.5f, 4);
        {
          uint8_t was = e->collidable;
          e->collidable = 0;
          V2 c = e_center(e);
          if (!point_solid(c.x, c.y))
            for (int i = 0; i < 32; i++) {
              float a = rndf() * 2 * PI_F;
              particles_emit1(PL_FG, &P_TouchSwitch_P_Fire, v2add(v2(e->x + e->cw / 2, e->y + e->ch / 2), angle_vec(a, 4)), a);
            }
          e->collidable = was;
        }
        g->step = 6;
        break;
      default: break;
    }
  if (g->rate != 0) {   /* the icon spins at its Rate */
    g->anim_t += DT * g->rate;
    if (fabsf(g->anim_t) >= 0.1f) {
      int s = g->anim_t > 0 ? 1 : -1;
      g->frame = (uint8_t)((g->frame + s + 6) % 6);
      g->anim_t -= s * 0.1f;
    }
  }
  wiggler_update(&g->wig);
  plat_update(e);
}
static void gate_render(Ent *e) {
  Gate *g = ST(e, Gate);
  V2 sh = e_shake(e);
  int nx = (int)(e->cw / 8) - 1, ny = (int)(e->ch / 8) - 1;
  for (int i = 0; i <= nx; i++)
    for (int j = 0; j <= ny; j++) {
      int a = i < nx ? (i < 1 ? i : 1) : 2, b = j < ny ? (j < 1 ? j : 1) : 2;
      gfx_tex_part(g->slice, e->x + sh.x + i * 8, e->y + sh.y + j * 8, a * 8, b * 8, 8, 8, 0, 0xFFFF, 255);
    }
  float s = 1 + g->wig.value, cx = e->x + e->cw / 2 + sh.x, cy = e->y + e->ch / 2 + sh.y;
  uint16_t ic = gate_icon[g->frame];
  for (int i = -1; i < 2; i++)
    for (int j = -1; j < 2; j++)
      if (i || j) gfx_tex_ex(ic, cx + i, cy + j, 6, 6, s, s, 0, 0, 255, GF_SILHOUETTE);
  gfx_tex_ex(ic, cx, cy, 6, 6, s, s, 0, rgb(g->color), 255, 0);
}
static void gate_awake(Ent *e) {
  Gate *g = ST(e, Gate);
  plat_static_movers_attach(e);
  char f[48];
  switch_flag_name(f, room_of(e));
  if (level_get_flag(f)) {
    plat_move_to(e, g->node.x, g->node.y);
    g->rate = 0, g->frame = 0;
    g->color = TS_FINISH;
    g->step = 6;
  }
}
static const EntClass SWITCHGATE = {.name = "switchGate", .size = sizeof(Gate), .update = gate_update, .render = gate_render,
                                    .awake = gate_awake, .kind = KIND_SOLID};
static void new_switchgate(const EData *d) {
  Ent *e = ent_new(&SWITCHGATE, d->x, d->y);
  if (!e) return;
  ent_box(e, EA(d, switchGate, width), EA(d, switchGate, height), 0, 0);
  Gate *g = ST(e, Gate);
  g->start = v2(d->x, d->y);
  g->node = ed_node(d, 0);
  g->persistent = EAB(d, switchGate, persistent);
  g->color = TS_INACTIVE;
  const char *sp = EAS(d, switchGate, sprite);
  char p[48];
  g->slice = tex(path2(p, "objects/switchgate/", *sp ? sp : "block", -1));
  for (int i = 0; i < 6; i++) gate_icon[i] = tex(path2(p, "objects/switchgate/icon", NULL, i));
}

/* ---------------------------------------------------------------- HangingLamp */
/* drawn in a strip layer (rotated, with its outline): as images it would take ~10 draws per 8 px */
typedef struct { float speed, rot, sound_delay; int16_t length; } Lamp;
static uint16_t lamp_tex;
static const uint16_t *lamp_col;
static const uint8_t *lamp_alpha;
static uint32_t lamp_px_frame = 0xFFFFFFFF;
static bool lamp_decode(void) {
  if (lamp_px_frame != g_frame) lamp_px_frame = g_frame, lamp_col = NULL, tex_frame_idx(lamp_tex, lamp_px, 8, 24, &lamp_col, &lamp_alpha);
  return lamp_col != NULL;
}
/* the lamp's pixel at (lx, ly) in its unrotated frame: x in [-4, 4), y in [0, length) */
static uint8_t lamp_texel(int lx, int ly, int len) {
  int u = lx + 4;
  if (u < 0 || u >= 8 || ly < 0 || ly >= len) return 0;
  if (ly >= len - 8) return lamp_px[(16 + ly - (len - 8)) * 8 + u];   /* the lamp (0, 16) */
  uint8_t v = ly < 8 ? lamp_px[ly * 8 + u] : 0;                          /* the top (0, 0) over the chain */
  return v ? v : lamp_px[(8 + ly % 8) * 8 + u];                           /* the chain (0, 8), every 8 px */
}
static const EntClass LAMP;
static void lamp_layer(uint16_t *strip, int sy0, int sy1, void *ctx) {
  Ent *e = ctx;
  Lamp *l = ST(e, Lamp);
  int len = l->length;
  float c = cosf(l->rot), s = sinf(l->rot);
  float ox = floorf(e->x) - g_camx, oy = floorf(e->y) - g_camy;
  float ext = fabsf(s) * len + 6;
  int x0 = (int)(ox - ext), x1 = (int)(ox + ext) + 1, y0 = (int)oy - 2, y1 = (int)(oy + len) + 2;
  if (y0 < sy0) y0 = sy0;
  if (y1 > sy1) y1 = sy1;
  for (int Y = y0; Y < y1; Y++)
    for (int X = x0; X < x1; X++) {
      if ((unsigned)X >= VIEW_W) continue;
      /* the pixel's texel and the 8 around it (DrawOutline: black, offset 1) */
      uint8_t here = 0;
      bool outline = false;
      for (int j = -1; j <= 1; j++)
        for (int i = -1; i <= 1; i++) {
          float dx = X + 0.5f - i - ox, dy = Y + 0.5f - j - oy;
          float lx = dx * c + dy * s, ly = -dx * s + dy * c;
          uint8_t v = lamp_texel((int)floorf(lx), (int)floorf(ly), len);
          if (!i && !j) here = v;
          else if (v) outline = true;
        }
      uint16_t *d = strip + (Y - sy0) * VIEW_W + X;
      if (here) sp_texel(d, lamp_col, lamp_alpha, here, 0xFFFF, 256, false);
      else if (outline) *d = 0;
    }
}
static void lamp_update(Ent *e) {
  Lamp *l = ST(e, Lamp);
  l->sound_delay -= DT;
  Player *p = level_player();
  if (p && collide_ent_at(e, e->x, e->y, p->ent)) {
    l->speed = -p->speed.x * 0.005f * ((p->ent->y - e->y) / l->length);
    if (fabsf(l->speed) < 0.1f) l->speed = 0;
    else if (l->sound_delay <= 0) l->sound_delay = 0.25f;
  }
  float num = signf(l->rot) == signf(l->speed) ? 8 : 6;
  if (fabsf(l->rot) < 0.5f) num *= 0.5f;
  if (fabsf(l->rot) < 0.25f) num *= 0.5f;
  float was = l->rot;
  l->speed += -signf(l->rot) * num * DT;
  l->rot += l->speed * DT;
  l->rot = clampf(l->rot, -0.4f, 0.4f);
  if (fabsf(l->rot) < 0.02f && fabsf(l->speed) < 0.2f) l->rot = l->speed = 0;
  else if (signf(l->rot) != signf(was) && l->sound_delay <= 0 && fabsf(l->speed) > 0.5f) l->sound_delay = 0.25f;
}
static void lamp_render(Ent *e) {
  Lamp *l = ST(e, Lamp);
  if (lamp_decode()) {
    float oy = floorf(e->y) - g_camy;
    gfx_custom(lamp_layer, e, (int)oy - 2, (int)(oy + l->length) + 2);
  }
  V2 at = v2add(v2(e->x, e->y), angle_vec(l->rot + PI_F / 2, l->length - 4.f));
  bloom_add(at.x, at.y, 1, 48);
  light_add(at.x, at.y, 0xFFFFFF, 1, 24, 48);
}
static const EntClass LAMP = {.name = "hanginglamp", .size = sizeof(Lamp), .update = lamp_update, .render = lamp_render};
static void new_lamp(const EData *d) {
  Ent *e = ent_new(&LAMP, d->x + 4, d->y);
  if (!e) return;
  int len = (int)EA(d, hanginglamp, height);
  if (len < 16) len = 16;
  e->depth = 2000;
  ent_box(e, 8, (float)len, -4, 0);
  ST(e, Lamp)->length = (int16_t)len;
  lamp_tex = tex("objects/hanginglamp");
}

/* ---------------------------------------------------------------- FloatingDebris */
typedef struct { V2 start, push; float rot, rot_speed, accel; SineWave sine; uint8_t piece; } Debris2;
static uint16_t debris_tex;
static void fdebris_on_player(Ent *e, Player *p) {
  Debris2 *d = ST(e, Debris2);
  V2 v = v2sub(v2(e->x, e->y), player_center(p));
  float l = v2len(v);
  v = l > 0 ? v2mul(v, v2len(p->speed) * 0.2f / l) : v2(0, 0);   /* SafeNormalize(length) */
  if (v.x * v.x + v.y * v.y > d->push.x * d->push.x + d->push.y * d->push.y) d->push = v;
  d->accel = 1;
}
static void fdebris_update(Ent *e) {
  Debris2 *d = ST(e, Debris2);
  sine_update(&d->sine);   /* base.Update() */
  if (d->push.x != 0 || d->push.y != 0) {
    e->x += d->push.x * DT, e->y += d->push.y * DT;
    float m = 64 * d->accel * DT, l = v2len(d->push);
    d->push = l <= m ? v2(0, 0) : v2sub(d->push, v2mul(d->push, m / l));
  } else {
    d->accel = 1;
    V2 to = v2sub(d->start, v2(e->x, e->y));
    float l = v2len(to), m = 6 * DT;
    if (l <= m) e->x = d->start.x, e->y = d->start.y;
    else e->x += to.x * m / l, e->y += to.y * m / l;
  }
  d->rot += d->rot_speed * DT;
}
/* all the pieces (8x8 parts of scenery/debris, rotating about their centers) in one strip layer */
static const EntClass FDEBRIS;
static const uint16_t *debris_col;
static const uint8_t *debris_alpha;
static uint32_t debris_frame = 0xFFFFFFFF;
static void fdebris_layer(uint16_t *strip, int sy0, int sy1, void *ctx) {
  (void)ctx;
  for (int k = 0; k < g_nents; k++) {
    Ent *e = &g_ents[k];
    if (e->cls != &FDEBRIS || e->dead == 1 || !e->visible) continue;
    Debris2 *d = ST(e, Debris2);
    float px = floorf(e->x + 0.5f) - g_camx, py = floorf(e->y + d->sine.value * 2 + 0.5f) - g_camy;
    if (py + 7 < sy0 || py - 7 >= sy1 || px < -8 || px > VIEW_W + 8) continue;
    float c = cosf(d->rot), s = sinf(d->rot);
    for (int Y = (int)py - 6; Y < (int)py + 7; Y++) {
      if (Y < sy0 || Y >= sy1) continue;
      for (int X = (int)px - 6; X < (int)px + 7; X++) {
        if ((unsigned)X >= VIEW_W) continue;
        float dx = X + 0.5f - px, dy = Y + 0.5f - py;
        int u = (int)floorf(dx * c + dy * s + 4), v = (int)floorf(-dx * s + dy * c + 4);
        if ((unsigned)u >= 8 || (unsigned)v >= 8) continue;
        uint8_t idx = debris_px[v * 64 + d->piece * 8 + u];
        if (idx) sp_texel(strip + (Y - sy0) * VIEW_W + X, debris_col, debris_alpha, idx, 0xFFFF, 256, false);
      }
    }
  }
}
static void fdebris_render(Ent *e) {
  (void)e;
  if (debris_frame == g_frame) return;
  debris_frame = g_frame;
  if (tex_frame_idx(debris_tex, debris_px, 64, 8, &debris_col, &debris_alpha)) gfx_custom(fdebris_layer, NULL, 0, VIEW_H);
}
static const EntClass FDEBRIS = {.name = "floatingDebris", .size = sizeof(Debris2), .update = fdebris_update, .render = fdebris_render,
                                 .on_player = fdebris_on_player, .kind = KIND_PCOLLIDE};
static void new_fdebris(const EData *dd) {
  Ent *e = ent_new(&FDEBRIS, dd->x, dd->y);
  if (!e) return;
  ent_box(e, 12, 12, -6, -6);
  e->depth = -5;
  Debris2 *d = ST(e, Debris2);
  d->start = v2(dd->x, dd->y);
  debris_tex = tex("scenery/debris");
  d->piece = (uint8_t)rndi(64 / 8);
  static const int8_t R[11] = {-2, -1, 0, 0, 0, 0, 0, 0, 0, 1, 2};
  d->rot_speed = R[rndi(11)] * 40 * (PI_F / 180);
  d->sine.freq = 0.4f;
  sine_randomize(&d->sine);
  d->accel = 1;
}

/* ---------------------------------------------------------------- ForegroundDebris */
typedef struct { V2 start; float parallax; SineWave sine[3]; uint8_t rock_b; } FgDebris;
static uint16_t fg_rock[2][3];
static void fgdebris_update(Ent *e) {
  FgDebris *f = ST(e, FgDebris);
  for (int i = 0; i < 3; i++) sine_update(&f->sine[i]);
}
static void fgdebris_render(Ent *e) {
  FgDebris *f = ST(e, FgDebris);
  V2 off = v2sub(v2add(g_level.cam, v2(160, 90)), f->start);
  float x = e->x - off.x * f->parallax, y = e->y - off.y * f->parallax;
  int n = f->rock_b ? 2 : 3;
  for (int i = 0; i < n; i++) {   /* the images, in reverse order of the family */
    uint16_t t = fg_rock[f->rock_b][n - 1 - i];
    gfx_tex_ex(t, x, y + f->sine[i].value * 2, 24, 24, 1, 1, 0, 0xFFFF, 255, 0);
  }
}
static const EntClass FGDEBRIS = {.name = "foregroundDebris", .size = sizeof(FgDebris), .update = fgdebris_update,
                                  .render = fgdebris_render};
static void new_fgdebris(const EData *d) {
  Ent *e = ent_new(&FGDEBRIS, d->x, d->y);
  if (!e) return;
  e->depth = -999900;
  e->collidable = 0;
  FgDebris *f = ST(e, FgDebris);
  f->start = v2(d->x, d->y);
  f->rock_b = (uint8_t)rndi(2);
  int n = f->rock_b ? 2 : 3;
  for (int i = 0; i < n; i++) {
    f->sine[i].freq = 0.4f;
    sine_randomize(&f->sine[i]);
  }
  f->parallax = 0.05f + rndf() * 0.08f;
  char p[40];
  for (int i = 0; i < 3; i++) fg_rock[0][i] = tex(path2(p, "scenery/fgdebris/rock_a", NULL, i));
  for (int i = 0; i < 2; i++) fg_rock[1][i] = tex(path2(p, "scenery/fgdebris/rock_b", NULL, i));
}

/* ---------------------------------------------------------------- InvisibleBarrier */
static void barrier_update(Ent *e) {
  e->collidable = 1;
  Ent *p = level_player_ent();
  if (p && collide_ent_at(e, e->x, e->y, p)) e->collidable = 0;
  if (!e->collidable) e->active = 0;
}
static const EntClass BARRIER = {.name = "invisibleBarrier", .size = 0, .update = barrier_update, .kind = KIND_SOLID | KIND_CLIMBBLOCKER};
static void new_barrier(const EData *d, float w, float h) {
  Ent *e = ent_new(&BARRIER, d->x, d->y);
  if (!e) return;
  ent_box(e, w, h, 0, 0);
  e->safe = 1;
  e->tags = TAG_TRANSITION_UPDATE;
  e->collidable = 0;
  e->visible = 0;
}

/* ---------------------------------------------------------------- Lookout (towerviewer) */
typedef struct {
  float easer, timer[4], mult[4], dir[4];   /* up, down, left, right */
  float track_pct;
  uint8_t track, only_y;
} LookHud;
enum { LU, LD, LL, LR };
static void lookhud_update(Ent *e) {
  LookHud *h = ST(e, LookHud);
  Room *rm = g_level.room;
  V2 c = g_level.cam;
  bool bl = false, br = false, bu = h->track && h->track_pct >= 1, bd = h->track && h->track_pct <= 0;   /* (no LookoutBlocker here) */
  h->dir[LL] = approach(h->dir[LL], (!bl && c.x > rm->x + 2) ? 1 : 0, DT * 8);
  h->dir[LR] = approach(h->dir[LR], (!br && c.x + 320 < rm->x + rm->w - 2) ? 1 : 0, DT * 8);
  h->dir[LU] = approach(h->dir[LU], (!bu && c.y > rm->y + 2) ? 1 : 0, DT * 8);
  h->dir[LD] = approach(h->dir[LD], (!bd && c.y + 180 < rm->y + rm->h - 2) ? 1 : 0, DT * 8);
  bool on[4] = {g_in.aim_y < 0, g_in.aim_y > 0, g_in.aim_x < 0, g_in.aim_x > 0};
  for (int i = 0; i < 4; i++) {
    h->mult[i] = approach(h->mult[i], on[i] ? 0 : 1, DT * 2);
    h->timer[i] += DT * (on[i] ? 12 : 6);
  }
}
/* GFX.Gui["towerarrow"] is not packed: a small arrow head instead, at the HUD's 1/6 scale */
static void look_arrow(float x, float y, int dir, uint16_t c, uint8_t a) {
  for (int i = 0; i < 4; i++) {
    float w = (float)i;
    switch (dir) {
      case LU: gfx_rect(x - w, y - 2 + i, 2 * w + 1, 1, c, a); break;
      case LD: gfx_rect(x - w, y + 2 - i, 2 * w + 1, 1, c, a); break;
      case LL: gfx_rect(x - 2 + i, y - w, 1, 2 * w + 1, c, a); break;
      default: gfx_rect(x + 2 - i, y - w, 1, 2 * w + 1, c, a); break;
    }
  }
}
static void lookhud_render(Ent *e) {
  LookHud *h = ST(e, LookHud);
  float num = ease_cube_inout(h->easer);
  uint8_t a = (uint8_t)(num * 255);
  if (!a) return;
  int n2 = (int)(80 * num), n3 = (int)(80 * num * 0.5625f), n4 = 8;
  const float k = 1 / 6.f;
  gfx_hud(true);
  gfx_rect(n2 * k, n3 * k, (1920 - n2 * 2 - n4) * k, n4 * k, 0xFFFF, a);
  gfx_rect(n2 * k, (n3 + n4) * k, (n4 + 2) * k, (1080 - n3 * 2 - n4) * k, 0xFFFF, a);
  gfx_rect((1920 - n2 - n4 - 2) * k, n3 * k, (n4 + 2) * k, (1080 - n3 * 2 - n4) * k, 0xFFFF, a);
  gfx_rect((n2 + n4) * k, (1080 - n3 - n4) * k, (1920 - n2 * 2 - n4) * k, n4 * k, 0xFFFF, a);
  float up = n3 * h->dir[LU] - sinf(h->timer[LU]) * 18 * lerpf(0.5f, 1, h->mult[LU]) - (1 - h->mult[LU]) * 12;
  look_arrow(160, up * k + 4, LU, 0xFFFF, (uint8_t)(a * h->dir[LU]));
  float down = 1080 - n3 * h->dir[LD] + sinf(h->timer[LD]) * 18 * lerpf(0.5f, 1, h->mult[LD]) + (1 - h->mult[LD]) * 12;
  look_arrow(160, down * k - 4, LD, 0xFFFF, (uint8_t)(a * h->dir[LD]));
  if (!h->track && !h->only_y) {
    float l = n2 * h->dir[LL] - sinf(h->timer[LL]) * 18 * lerpf(0.5f, 1, h->mult[LL]) - (1 - h->mult[LL]) * 12;
    look_arrow(l * k + 4, 90, LL, 0xFFFF, (uint8_t)(a * h->dir[LL]));
    float r = 1920 - n2 * h->dir[LR] + sinf(h->timer[LR]) * 18 * lerpf(0.5f, 1, h->mult[LR]) + (1 - h->mult[LR]) * 12;
    look_arrow(r * k - 4, 90, LR, 0xFFFF, (uint8_t)(a * h->dir[LR]));
  } else if (h->track) {   /* the track and its cursor (GUI art: drawn as plain shapes) */
    int n15 = 1080 - n3 * 2 - 128 - 64, n16 = 1920 - n2 - 64;
    float n17 = (1080 - n15) / 2.f + 32;
    gfx_rect((n16 - 7) * k, (n17 + 7) * k, 14 * k, (n15 - 14) * k, 0, a);
    gfx_rect((n16 - 6) * k, (n17 + (1 - h->track_pct) * n15 - 6) * k, 12 * k, 12 * k, 0xFFFF, a);
  }
  gfx_hud(false);
}
static const EntClass LOOKHUD = {.name = "lookoutHud", .size = sizeof(LookHud), .update = lookhud_update, .render = lookhud_render};

/* the sprite "lookout" (Justify (0.5, 1)) as a frame table (smaller than a Sprite in the state), frames
 * of objects/lookout/lookout or nobackpack; an animation loads when first drawn, the room loads "idle" */
enum { LA_IDLE, LA_LOOKRIGHT, LA_LOOKLEFT, LA_LOOKING, LA_UP, LA_UPRIGHT, LA_RIGHT, LA_DOWNRIGHT, LA_DOWN, LA_DOWNLEFT, LA_LEFT,
       LA_UPLEFT, LA_ENDED = 255 };
static const uint8_t LA_FRAMES[12][10] = {{5, 5, 5, 5, 5, 5, 5, 5, 5, 6}, {0, 1, 2, 3, 4}, {15, 16, 17, 3, 4}, {4}, {7}, {8}, {9}, {10},
                                           {11}, {12}, {13}, {14}};
static const uint8_t LA_N[12] = {10, 5, 5, 1, 1, 1, 1, 1, 1, 1, 1, 1};
static const float LA_DELAY[12] = {0.1f, 0.08f, 0.08f, 0.08f, 0, 0, 0, 0, 0, 0, 0, 0};
static uint16_t look_tex[2][18];
typedef struct { uint8_t anim, frame, variant; float timer; } LSpr;
static void lspr_play(LSpr *s, int a) {
  if (s->anim == a) return;
  s->anim = (uint8_t)a, s->frame = 0, s->timer = 0;
}
static void lspr_update(LSpr *s) {
  if (s->anim >= 12 || LA_DELAY[s->anim] <= 0) return;
  s->timer += DT;
  if (s->timer < LA_DELAY[s->anim]) return;
  s->timer -= LA_DELAY[s->anim];
  if (++s->frame < LA_N[s->anim]) return;
  if (s->anim == LA_LOOKRIGHT || s->anim == LA_LOOKLEFT) s->anim = LA_LOOKING;   /* goto */
  s->frame = 0;
}
#define LOOK_NODES 28   /* the summit's lookout (7A's g-03) has the most */
typedef struct {
  LSpr spr;
  Talk talk;
  V2 cam, speed, cam_start, cam_start_center, last_dir;
  float wait, node_pct, t;
  int16_t hud;
  int16_t nodes[LOOK_NODES][2];
  Walk walk;
  Step zs;                /* the summit's Level.ZoomTo */
  uint8_t nnodes, node, step, interacting, summit, only_y, at_top;
} Look;
static void look_play(Look *l, int anim) { lspr_play(&l->spr, anim); }
static V2 look_node(const Look *l, int i) { return v2(l->nodes[i][0], l->nodes[i][1]); }
static void look_end(Ent *e, Look *l, Player *p) {
  l->interacting = 0;
  ent_remove(ent_at(l->hud, &LOOKHUD));
  l->hud = -1;
  if (p && !p->dead) player_set_state(p, ST_NORMAL);
  l->step = 0;
  (void)e;
}
static void look_update(Ent *e) {
  Look *l = ST(e, Look);
  Player *p = level_player();
  bool talk = talk_update(&l->talk, e);
  if (talk && p) {   /* Interact */
    l->spr.variant = p->no_backpack ? 1 : 0;   /* (animPrefix "badeline_": Play As Badeline, not here) */
    l->interacting = 1;
    l->step = 1;
    return;   /* the coroutine starts next frame */
  }
  Ent *hud = ent_at(l->hud, &LOOKHUD);
  LookHud *h = hud ? ST(hud, LookHud) : NULL;
  if (l->step && p) {
    if (l->wait > 0) l->wait -= DT;
    else switch (l->step) {
        case 1:   /* LookRoutine */
          if (p->holding) holdable_release(p->holding, v2(0, 0)), p->holding = NULL;
          player_set_state(p, ST_DUMMY);
          l->walk = walk_exact((int)floorf(e->x));   /* yield return DummyWalkToExact((int)X, cancelOnFall: true) */
          l->walk.flags = WALK_CANCEL_ON_FALL;
          l->step = 2;
          break;
        case 2:
          if (!player_walk(p, &l->walk)) l->step = 21;
          break;
        case 21: {
          Ent *pe = p->ent;
          if (fabsf(e->x - pe->x) > 4 || p->dead || !player_on_ground(p)) {
            if (!p->dead) player_set_state(p, ST_NORMAL);
            l->step = 0, l->interacting = 0;
            break;
          }
          look_play(l, p->facing == 1 ? LA_LOOKRIGHT : LA_LOOKLEFT);
          p->ent->visible = 0;   /* the player's sprite and hair */
          l->wait = 0.2f, l->step = 3;
          break;
        }
        case 3:
          hud = ent_new(&LOOKHUD, 0, 0);
          l->hud = ent_ref(hud);
          if (hud) {
            hud->depth = -2000000;
            hud->collidable = 0;
            hud->tags = TAG_HUD;
            LookHud *nh = ST(hud, LookHud);
            nh->track = l->nnodes > 0;
            nh->only_y = l->only_y;
          }
          l->node_pct = 0, l->node = 0;
          l->step = 4;
          break;
        case 4:
          if (h && (h->easer = approach(h->easer, 1, DT * 3)) < 1) break;
          l->cam = g_level.cam;
          l->speed = v2(0, 0), l->last_dir = v2(0, 0);
          l->cam_start = g_level.cam;
          l->cam_start_center = v2add(l->cam_start, v2(160, 90));
          l->step = 5;
          /* fall through */
        case 5: {
          if (btn_pressed(&g_in.back) || btn_pressed(&g_in.confirm) || btn_pressed(&g_in.dash) || btn_pressed(&g_in.jump) || !l->interacting) {
            l->step = 6;
            goto look_out;
          }
          V2 v = v2((float)g_in.aim_x, (float)g_in.aim_y);
          if (v.x && v.y) v = v2mul(v, 0.70710677f);
          if (l->only_y) v.x = 0;
          l->last_dir = v;
          if (l->spr.anim != LA_LOOKLEFT && l->spr.anim != LA_LOOKRIGHT) {
            int a;
            if (v.x == 0) a = v.y == 0 ? LA_LOOKING : v.y > 0 ? LA_DOWN : LA_UP;
            else if (v.x > 0) a = v.y == 0 ? LA_RIGHT : v.y > 0 ? LA_DOWNRIGHT : LA_UPRIGHT;
            else a = v.y == 0 ? LA_LEFT : v.y > 0 ? LA_DOWNLEFT : LA_UPLEFT;
            look_play(l, a);
          }
          Room *rm = g_level.room;
          const float accel = 800, maxspd = 240;
          if (!l->nnodes) {
            l->speed = v2add(l->speed, v2mul(v, accel * DT));
            if (v.x == 0) l->speed.x = approach(l->speed.x, 0, accel * 2 * DT);
            if (v.y == 0) l->speed.y = approach(l->speed.y, 0, accel * 2 * DT);
            float sl = v2len(l->speed);
            if (sl > maxspd) l->speed = v2mul(l->speed, maxspd / sl);
            l->cam.x += l->speed.x * DT;
            if (l->cam.x < rm->x || l->cam.x + 320 > rm->x + rm->w) l->speed.x = 0;
            l->cam.x = clampf(l->cam.x, (float)rm->x, (float)(rm->x + rm->w - 320));
            l->cam.y += l->speed.y * DT;
            if (l->cam.y < rm->y || l->cam.y + 180 > rm->y + rm->h) l->speed.y = 0;
            l->cam.y = clampf(l->cam.y, (float)rm->y, (float)(rm->y + rm->h - 180));
            g_level.cam = l->cam;
            break;
          }
          /* track mode: along the nodes */
          V2 a = l->node <= 0 ? l->cam_start_center : look_node(l, l->node - 1), b = look_node(l, l->node);
          float len = v2len(v2sub(a, b));
          V2 at;
          if (l->node_pct < 0.25f && l->node > 0) {
            V2 pa = l->node <= 1 ? l->cam_start_center : look_node(l, l->node - 2);
            V2 c0 = v2add(pa, v2mul(v2sub(a, pa), 0.75f)), c1 = v2add(a, v2mul(v2sub(b, a), 0.25f));
            float t = 0.5f + l->node_pct / 0.25f * 0.5f;
            at = v2(quad_curve(c0.x, a.x, c1.x, t), quad_curve(c0.y, a.y, c1.y, t));
          } else if (l->node_pct > 0.75f && l->node < l->nnodes - 1) {
            V2 nb = look_node(l, l->node + 1);
            V2 c0 = v2add(a, v2mul(v2sub(b, a), 0.75f)), c1 = v2add(b, v2mul(v2sub(nb, b), 0.25f));
            float t = (l->node_pct - 0.75f) / 0.25f * 0.5f;
            at = v2(quad_curve(c0.x, b.x, c1.x, t), quad_curve(c0.y, b.y, c1.y, t));
          } else
            at = v2add(a, v2mul(v2sub(b, a), l->node_pct));
          g_level.cam = v2add(at, v2(-160, -90));
          l->node_pct -= v.y * (maxspd / len) * DT;
          if (l->node_pct < 0) {
            if (l->node > 0) l->node--, l->node_pct = 1;
            else l->node_pct = 0;
          } else if (l->node_pct > 1) {
            if (l->node < l->nnodes - 1) l->node++, l->node_pct = 0;
            else {
              l->node_pct = 1;
              if (l->summit) {
                l->step = 6;
                goto look_out;
              }
            }
          }
          float done = 0, total = 0;
          for (int i = 0; i < l->nnodes; i++) {
            float sl = v2len(v2sub(i == 0 ? l->cam_start_center : look_node(l, i - 1), look_node(l, i)));
            total += sl;
            if (i < l->node) done += sl;
            else if (i == l->node) done += sl * l->node_pct;
          }
          if (h) h->track_pct = done / total;
          break;
        }
        look_out:
        case 6:
          p->ent->visible = 1;
          look_play(l, LA_IDLE);
          l->step = 7;
          /* fall through */
        case 7:
          if (h && (h->easer = approach(h->easer, 0, DT * 3)) > 0) break;
          l->at_top = l->summit && l->node >= l->nnodes - 1 && l->node_pct >= 0.95f;
          if (l->at_top) {
            l->wait = 0.5f;
            step_reset(&l->zs);   /* Add(new Coroutine(level.ZoomTo((160, 90), 2, 3))): it starts next frame */
            l->step = 8;
            break;
          }
          l->step = 9;
          break;
        case 8:   /* (the "escape" music parameter: no music) */
          if (l->zs.p < 1) zoom_to(&l->zs, v2(160, 90), 2, 3);
          if (!(btn_pressed(&g_in.back) || btn_pressed(&g_in.confirm) || btn_pressed(&g_in.dash) || btn_pressed(&g_in.jump)) &&
              l->interacting)
            break;
          l->step = 9;
          /* fall through */
        case 9:
          if (v2len(v2sub(l->cam_start, g_level.cam)) > 600) {   /* back near the start, behind a FadeWipe */
            l->cam = g_level.cam;
            l->speed = v2norm(v2sub(l->cam, l->cam_start));
            l->t = 0;
            wipe_start(WIPE_FADE, false, NULL);
            g_wipe.duration = l->at_top ? 1.f : 0.5f;
            l->step = 10;
            break;
          }
          l->step = 11;
          break;
        case 10:
          l->t += DT / (l->at_top ? 1.f : 0.5f);
          if (l->t < 1) {
            g_level.cam = v2sub(l->cam, v2mul(l->speed, lerpf(0, 64, ease_cube_in(l->t))));
            break;
          }
          g_level.cam = v2add(l->cam_start, v2mul(l->speed, 32));
          wipe_start(WIPE_FADE, true, NULL);
          l->step = 11;
          break;
        case 11:
          zoom_snap(v2(0, 0), 1);   /* Level.ZoomSnap(Vector2.Zero, 1) */
          look_end(e, l, p);
          break;
      }
  }
  /* base.Update() (the sprite), then Update's own */
  if (p) {
    bool active = l->interacting || p->state != ST_DUMMY;
    if (active) lspr_update(&l->spr);
    else l->spr.frame = 0;
  }
  if (l->talk.enabled && collide_solid(e, e->x, e->y)) l->talk.enabled = 0;
}
static void look_render(Ent *e) {
  Look *l = ST(e, Look);
  int a = l->spr.anim < 12 ? l->spr.anim : LA_IDLE;
  const uint16_t *tx = look_tex[l->spr.variant];
  if (!res_cached(tx[LA_FRAMES[a][l->spr.frame]])) {   /* an animation not loaded yet: all its frames at once */
    uint16_t ids[10];
    for (int i = 0; i < LA_N[a]; i++) ids[i] = tx[LA_FRAMES[a][i]];
    res_load(ids, LA_N[a]);
  }
  gfx_tex(tx[LA_FRAMES[a][l->spr.frame]], e->x - 16, e->y - 32, 0, 0xFFFF, 255);
  light_add(e->x - 1, e->y - 11, 0xFFFFFF, 0.8f, 16, 24);
}
static void look_removed(Ent *e) {
  Look *l = ST(e, Look);
  if (l->interacting) {
    Player *p = level_player();
    if (p) player_set_state(p, ST_NORMAL), p->ent->visible = 1;
    ent_remove(ent_at(l->hud, &LOOKHUD));
  }
}
static const EntClass LOOKOUT = {.name = "towerviewer", .size = sizeof(Look), .update = look_update, .render = look_render,
                                 .removed = look_removed, .kind = KIND_TALK};
static void new_lookout(const EData *d) {
  Ent *e = ent_new(&LOOKOUT, d->x, d->y);
  if (!e) return;
  e->depth = -8500;
  ent_box(e, 4, 4, -2, -4);
  Look *l = ST(e, Look);
  ch1_talk_add(e, &l->talk, -24, -8, 48, 8, v2(-0.5f, -20));
  l->talk.must_face = false;
  l->summit = EAB(d, towerviewer, summit);
  l->only_y = EAB(d, towerviewer, onlyY);
  l->hud = -1;
  l->spr.anim = LA_IDLE;
  char p[40];
  for (int i = 0; i < 18; i++) {
    look_tex[0][i] = tex(path2(p, "objects/lookout/lookout", NULL, i));
    look_tex[1][i] = tex(path2(p, "objects/lookout/nobackpack", NULL, i));
  }
  l->nnodes = (uint8_t)(d->nnodes < LOOK_NODES ? d->nnodes : LOOK_NODES);
  for (int i = 0; i < l->nnodes; i++) {
    V2 n = ed_node(d, i);
    l->nodes[i][0] = (int16_t)n.x, l->nodes[i][1] = (int16_t)n.y;
  }
}

/* ---------------------------------------------------------------- DreamMirror and CS02_Mirror */
#define GLASS_W 54
#define GLASS_H 29
typedef struct {
  Sprite glass, refl;
  Hair hair;
  V2 rpos;                /* the reflection, inside the glass (its render target) */
  V2 bpos;                /* BadelineDummy, after the break */
  float shine_alpha, shine_offset, wait, t, bspeed, refl_sx;
  float act_p, zb_t;
  Step zs;                /* Level.ZoomTo / ZoomBack */
  Walk walk;              /* DummyRunTo */
  int16_t dream, cs_ent;
  int16_t hb_x, hb_w;     /* the hitbox, widened once the player is in */
  int8_t dir, act_shx, act_shy;
  uint8_t smashed, smash_ended, update_shine, auto_refl, step, cs, act, bstate, refl_on, zb_on;
} Mirror;
static uint16_t mirror_frame_tex, mirror_bg, mirror_fg;
static const EntClass MIRROR;
static bool mirror_hit(Ent *e, Mirror *m, Ent *pe) {
  float l = e->x + m->hb_x, t = e->y - 39 - 32;
  return collide_rect(pe, l, t, l + m->hb_w, t + 39 + 32);
}
/* the glass, drawn like its render target: glassbg, the reflection, the shine, all at 0.7 */
typedef struct { uint16_t col; uint8_t a; } Px;
static void mirror_layer(uint16_t *strip, int sy0, int sy1, void *ctx) {
  Ent *e = ctx;
  Mirror *m = ST(e, Mirror);
  int ox = vx(e->x - 27), oy = vy(e->y - 29);
  Tex bg, fg, rt;
  if (!tex_get(mirror_bg, &bg) || !tex_get(mirror_fg, &fg)) return;
  uint16_t rtex = m->refl_on ? spr_tex(&m->refl) : 0xFFFF;
  bool has_r = rtex != 0xFFFF && tex_get(rtex, &rt);
  int n;
  const uint16_t *bgc = pal_colors(bg.pal, &n), *fgc = pal_colors(fg.pal, &n), *rc = has_r ? pal_colors(rt.pal, &n) : NULL;
  const uint8_t *bga = pal_alpha(bg.pal), *fga = pal_alpha(fg.pal), *ra = has_r ? pal_alpha(rt.pal) : NULL;
  static uint8_t r1[GLASS_W], r2[GLASS_W], r3[GLASS_W], r4[64];
  int hx = 0, hy = 0, hfr = 0, carry;
  bool hhas = false;
  if (has_r) hair_meta(rtex, &hx, &hy, &hfr, &hhas, &carry);
  int rflip = m->refl_sx < 0;
  for (int y = 0; y < GLASS_H; y++) {
    int Y = oy + y;
    if (Y < sy0 || Y >= sy1) continue;
    Px row[GLASS_W];
    memset(row, 0, sizeof row);
    /* glassbg */
    int br = y - bg.oy;
    if (br >= 0 && br < bg.h) {
      tex_row_idx(&bg, br, r1);
      for (int i = 0; i < bg.w; i++)
        if (r1[i] && bg.ox + i < GLASS_W) row[bg.ox + i] = (Px){bgc[r1[i] - 1], bga[r1[i] - 1]};
    }
    /* the reflection: hair, then the sprite (origin from Justify, flipped by its scale) */
    if (has_r) {
      if (hhas) {
        for (int pass = 0; pass < 2; pass++)
          for (int k = 0; k < BHAIR; k++) {
            int i = pass ? BHAIR - 1 - k : k;
            float s = 0.25f + (1 - (float)i / BHAIR) * 0.75f;
            V2 c = m->hair.n[i];
            for (int x = 0; x < GLASS_W; x++)
              for (int o = pass ? 4 : 0; o < (pass ? 5 : 4); o++) {
                float px = c.x + (o == 0 ? -1 : o == 1 ? 1 : 0), py = c.y + (o == 2 ? -1 : o == 3 ? 1 : 0);
                int v = (int)floorf((y + 0.5f - floorf(py + 0.5f)) / s + 5), u = (int)floorf((x + 0.5f - floorf(px + 0.5f)) / s + 5);
                if (v >= 0 && v < 10 && u >= HAIR_SPAN[v][0] && u < HAIR_SPAN[v][1])
                  row[x] = (Px){pass ? rgb(0x9B3FB5) : 0, 255};
              }
          }
      }
      int sx0 = (int)floorf(m->rpos.x - m->refl.ox * (rflip ? -1 : 1) + 0.5f), sy0r = (int)floorf(m->rpos.y - m->refl.oy + 0.5f);
      int rr = y - sy0r - rt.oy;
      if (rr >= 0 && rr < rt.h && rt.w <= 64) {
        tex_row_idx(&rt, rr, r4);
        for (int i = 0; i < rt.w; i++) {
          if (!r4[i]) continue;
          int x = rflip ? sx0 - (rt.ox + i) - 1 : sx0 + rt.ox + i;
          if (x >= 0 && x < GLASS_W) row[x] = (Px){rc[r4[i] - 1], ra[r4[i] - 1]};
        }
      }
    }
    /* glassfg, twice, at the shine's offset */
    int sa = (int)(m->shine_alpha * 256);
    for (int k = 0; k < 2; k++) {
      int fr = y - (int)(m->shine_offset - k * fg.fh) - fg.oy;
      if (fr < 0 || fr >= fg.h) continue;
      tex_row_idx(&fg, fr, k ? r3 : r2);
      uint8_t *rb = k ? r3 : r2;
      for (int i = 0; i < fg.w; i++) {
        if (!rb[i] || fg.ox + i >= GLASS_W) continue;
        Px *d = &row[fg.ox + i];
        int a = (fga[rb[i] - 1] * sa) >> 8;
        uint16_t c = scale565(fgc[rb[i] - 1], sa);
        d->col = blend565(scale565(d->col, 256), c, a);
        d->a = (uint8_t)(a + ((255 - a) * d->a >> 8));
      }
    }
    for (int x = 0; x < GLASS_W; x++) {
      int X = ox + x;
      if (!row[x].a || (unsigned)X >= VIEW_W) continue;
      int a = (row[x].a * 179) >> 8;   /* * 0.7 */
      uint16_t *d = strip + (Y - sy0) * VIEW_W + X;
      *d = blend565(*d, scale565(row[x].col, 179), a + 1);
    }
  }
}
static void mirror_render(Ent *e) {
  Mirror *m = ST(e, Mirror);
  if (m->smashed) spr_draw(&m->glass, e->x, e->y);
  else {
    tex_touch(mirror_bg), tex_touch(mirror_fg);
    if (m->refl_on) tex_touch(spr_tex(&m->refl));
    gfx_custom(mirror_layer, e, vy(e->y - 29), vy(e->y) + 1);
  }
  gfx_tex(mirror_frame_tex, e->x - 32, e->y - 39, 0, 0xFFFF, 255);   /* frame, JustifyOrigin(0.5, 1) */
  if (m->bstate) {   /* BadelineDummy */
    Sprite s = m->refl;
    s.sx = m->refl_sx;
    hair_render(&m->hair, &s, m->refl_sx < 0 ? -1 : 1, rgb(0x9B3FB5), 1);
    spr_draw(&s, floorf(m->bpos.x), floorf(m->bpos.y));
  }
}
static Ent *first_dreamblock(void) {
  Ent *best = NULL;
  for (int i = 0; i < g_nents; i++) {
    Ent *o = &g_ents[i];
    if (o->cls == &DREAMBLOCK && o->dead != 1 && (!best || (int16_t)(o->order - best->order) < 0)) best = o;
  }
  return best;
}
/* DreamBlock.Activate, on the first dream block; true when done */
static bool dream_activate_step(Mirror *m) {
  Ent *e = ent_at(m->dream, &DREAMBLOCK);
  if (!e) return true;
  Dream *d = ST(e, Dream);
  if (m->wait > 0) {
    m->wait -= DT;
    return false;
  }
  switch (m->act) {
    case 0:
      m->wait = 1, m->act = 1, m->act_p = 0;
      return false;
    case 1:   /* the Shaker (0.02 s) while it fills with white */
      if (m->act_p < 1) {
        if (level_on_interval(0.02f)) d->sx = (int8_t)shake_val(), d->sy = (int8_t)shake_val();
        d->white_fill = ease_cube_in(m->act_p);
        m->act_p += DT;
        return false;
      }
      d->sx = d->sy = 0;
      m->wait = 0.5f, m->act = 2;
      return false;
    case 2:
      dream_activate_now(e);
      d->white_height = 1, d->white_fill = 1;
      m->act_p = 1, m->act = 3;
      /* fall through */
    case 3:
      if (m->act_p > 0) {
        d->white_height = m->act_p;
        if (level_on_interval(0.1f))
          for (int i = 0; (float)i < e->cw; i += 4)
            particles_emit1(PL_FG, &P_Strawberry_P_WingsBurst, v2(e->x + i, e->y + e->ch * d->white_height + 1),
                            P_Strawberry_P_WingsBurst.direction);
        if (level_on_interval(0.1f)) level_shake(0.3f);
        m->act_p -= DT * 0.5f;
        return false;
      }
      m->act = 4;
      /* fall through */
    default:
      if (d->white_fill > 0) {
        d->white_fill -= DT * 3;
        return false;
      }
      return true;
  }
}
static void mirror_cs_end(Ent *e, Mirror *m, Player *p, bool skipped) {   /* CS02_Mirror.OnEnd */
  m->update_shine = 0;   /* Broken(WasSkipped) */
  m->smashed = m->smash_ended = 1;
  spr_play(&m->glass, A_glass_broken, false);
  if (skipped) m->bstate = 0, m->refl_on = 0;   /* the BadelineDummy goes */
  if (p) {
    player_set_state(p, ST_NORMAL);
    p->dummy_auto_animate = true;
    p->speed = v2(0, 0);
    p->ent->x = e->x + 8 * m->dir;
    p->facing = m->dir ? -m->dir : 1;
    p->inventory_dreamdash = true;
  }
  for (int i = 0; i < g_nents; i++)
    if (g_ents[i].cls == &DREAMBLOCK && g_ents[i].dead != 1) dream_activate_now(&g_ents[i]);
  zoom_reset();               /* Level.ResetZoom */
  g_session.dreamdash = 1;   /* Session.Inventory.DreamDash */
  m->cs = 0, m->zb_on = 0;
}
/* CS02_Mirror's Cutscene and DreamMirror.BreakRoutine */
static void mirror_cutscene(Ent *e, Mirror *m, Player *p) {
  if (m->zb_on) {   /* the ZoomBack coroutine: 1.2 s, then Level.ZoomBack(3) */
    if (m->zb_t > 0) m->zb_t -= DT;
    else if (!zoom_back(&m->zs, 3)) m->zb_on = 0;
  }
  if (m->cs >= 20) {   /* yield return dreamBlock.Activate(); yield return 0.5f; EndCutscene */
    if (m->cs == 0xFF || !dream_activate_step(m)) return;
    if (m->cs == 20) m->cs = 21, m->wait = 0.5f;
    else if (m->wait > 0) m->wait -= DT;
    else m->cs = 0xFF;
    return;
  }
  if (m->wait > 0) {
    m->wait -= DT;
    return;
  }
  Ent *pe = p->ent;
  switch (m->cs) {
    case 1:
      m->dir = (int8_t)signf(pe->x - e->x);
      player_set_state(p, ST_DUMMY);
      m->wait = 1, m->cs = 2;
      break;
    case 2:
      p->facing = -m->dir;
      m->wait = 0.4f, m->cs = 3;
      break;
    case 3:   /* yield return DummyRunTo(mirror.X + 8 * direction) */
      m->walk = (Walk){e->x + 8 * m->dir, 1, RUN_TO, 0, 0, 0};
      m->cs = 4;
      break;
    case 4:
      if (player_walk(p, &m->walk)) break;
      m->wait = 0.5f, m->cs = 5;
      break;
    case 5:   /* yield return Level.ZoomTo(mirror - camera - (0, 24), 2, 1) */
      step_reset(&m->zs);
      m->cs = 15;
      break;
    case 15:
      if (zoom_to(&m->zs, v2(e->x - g_level.cam.x, e->y - g_level.cam.y - 24), 2, 1)) break;
      m->wait = 0.5f, m->cs = 6;
      break;
    case 6:   /* BreakRoutine */
      m->auto_refl = 0;
      spr_play(&m->refl, A_badeline_runFast, false);
      m->cs = 7;
      /* fall through */
    case 7:
      if (fabsf(m->rpos.x - GLASS_W / 2.f) > 3) {
        m->rpos.x += m->dir * 32 * DT;
        break;
      }
      spr_play(&m->refl, A_badeline_idle, false);
      m->wait = 0.65f + 0.15f, m->cs = 8;
      break;
    case 8:
      m->update_shine = 0;
      if (m->shine_offset != 33 || m->shine_alpha < 1) {
        m->shine_offset = approach(m->shine_offset, 33, DT * 120);
        m->shine_alpha = approach(m->shine_alpha, 1, DT * 4);
        break;
      }
      m->smashed = 1;
      spr_play(&m->glass, A_glass_break, false);
      m->wait = 0.6f, m->cs = 9;
      break;
    case 9:
      level_shake(0.3f);
      for (float x = -GLASS_W / 2.f; x < GLASS_W / 2.f; x += 8)
        for (float y = -GLASS_H; y < 0; y += 8)
          if (rndf() < 0.5f)
            particles_emit(PL_MID, &P_DreamMirror_P_Shatter, 2, v2(e->x + x + 4, e->y + y + 4), v2(8, 8), atan2f(y, x));
      m->smash_ended = 1;
      m->bpos = v2(m->rpos.x + e->x - 27, m->rpos.y + e->y - 29);
      m->bstate = 1;
      spr_play(&m->refl, A_badeline_idle, false);
      m->refl_on = 0;
      for (int i = 0; i < BHAIR; i++) m->hair.n[i] = v2add(m->hair.n[i], v2(e->x - 27, e->y - 29));
      m->wait = 1.2f, m->cs = 10;
      break;
    case 10:
      m->bspeed = -m->dir * 32.f;
      m->refl_sx = (float)-m->dir;
      spr_play(&m->refl, A_badeline_runFast, false);
      m->cs = 11;
      /* fall through */
    case 11:
      if (fabsf(m->bpos.x - e->x) < 60) {
        m->bspeed += DT * -m->dir * 128;
        m->bpos.x += m->bspeed * DT;
        break;
      }
      spr_play(&m->refl, A_badeline_jumpFast, false);
      m->cs = 12;
      /* fall through */
    case 12:
      if (fabsf(m->bpos.x - e->x) < 128) {
        m->bspeed += DT * -m->dir * 128;
        m->bpos.x += m->bspeed * DT;
        m->bpos.y -= fabsf(m->bspeed) * DT * 0.8f;
        break;
      }
      m->bstate = 0;
      m->wait = 1.5f, m->cs = 13;
      break;
    case 13:   /* back in the cutscene: Madeline looks up, the camera goes up */
      p->dummy_auto_animate = false;
      spr_play(&p->spr, A_player_lookUp, false);
      m->t = 0;
      m->rpos = g_level.cam;   /* (the reflection is gone: its fields keep the camera's start) */
      m->cs = 14;
      /* fall through */
    case 14:
      if (m->t < 1) {
        g_level.cam = v2add(m->rpos, v2(0, -80 * ease_cube_inout(m->t)));
        m->t += DT * 1.2f;
        break;
      }
      m->dream = ent_ref(first_dreamblock());
      m->zb_on = 1, m->zb_t = 1.2f;   /* Add(new Coroutine(ZoomBack())) */
      step_reset(&m->zs);
      m->act = 0;
      m->cs = 20;
      break;
  }
}
/* CS02_Mirror, the CutsceneEntity (skippable): its steps run on the mirror's state */
typedef struct { Cutscene c; int16_t mirror; } CS02;
static void cs02_update(Ent *e) {
  Ent *me = ent_at(ST(e, CS02)->mirror, &MIRROR);
  Player *p = level_player();
  if (!me || !p) return;
  Mirror *m = ST(me, Mirror);
  if (m->cs && m->cs != 0xFF) mirror_cutscene(me, m, p);
  if (m->cs == 0xFF) cutscene_end(e);   /* EndCutscene: OnEnd */
}
static void cs02_on_end(Ent *e, bool skipped) {
  Ent *me = ent_at(ST(e, CS02)->mirror, &MIRROR);
  if (me) mirror_cs_end(me, ST(me, Mirror), level_player(), skipped);
}
static const EntClass CS02_MIRROR = {.name = "CS02_Mirror", .size = sizeof(CS02), .update = cs02_update};
static void mirror_update(Ent *e) {
  Mirror *m = ST(e, Mirror);
  Player *p = level_player();
  spr_update(&m->glass);
  spr_update(&m->refl);
  if (!m->smashed && p) {   /* InteractRoutine */
    if (m->step == 0 && mirror_hit(e, m, p->ent)) m->hb_w += 32, m->hb_x -= 16, m->step = 1;
    else if (m->step == 1 && !mirror_hit(e, m, p->ent)) {
      m->step = 2, m->cs = 1;
      Ent *c = ent_new(&CS02_MIRROR, 0, 0);   /* Scene.Add(new CS02_Mirror(player, this)) */
      m->cs_ent = ent_ref(c);
      if (c) ST(c, CS02)->mirror = ent_ref(e), cutscene_start(c, cs02_on_end, true, false);
    }
  }
  if (m->cs && p && !ent_at(m->cs_ent, &CS02_MIRROR)) {   /* (no room for the cutscene's entity: run here) */
    if (m->cs != 0xFF) mirror_cutscene(e, m, p);
    if (m->cs == 0xFF) mirror_cs_end(e, m, p, false);
  }
  /* BeforeRender: the reflection copies the player, mirrored */
  if (!m->smashed && p && m->auto_refl) {
    m->rpos = v2(e->x - p->ent->x + 27, p->ent->y - e->y + 29);
    m->refl_sx = (float)-p->facing * fabsf(p->spr.sx);
    int a = badeline_anim(p->spr.anim);
    if (a >= 0 && a != m->refl.anim && p->spr.anim != 255) spr_play(&m->refl, a, false);
  }
  if (m->update_shine) {
    Tex fg;
    float h = tex_get(mirror_fg, &fg) ? fg.fh : 73;
    m->shine_offset = h - (float)(int)fmodf(g_level.cam.y * 0.8f, h);
  }
  if (m->refl_on || m->bstate) {
    int f = m->refl_sx < 0 ? -1 : 1;
    V2 at = m->bstate ? m->bpos : m->rpos;
    hair_after_update(&m->hair, &m->refl, at, f);
    m->hair.wave += DT * 4;
  }
}
static const EntClass MIRROR = {.name = "dreammirror", .size = sizeof(Mirror), .update = mirror_update, .render = mirror_render};
static void new_mirror(const EData *d) {
  Ent *e = ent_new(&MIRROR, d->x, d->y);
  if (!e) return;
  e->depth = 9500;
  e->collidable = 0;
  Mirror *m = ST(e, Mirror);
  mirror_frame_tex = tex("objects/mirror/frame");
  mirror_bg = tex("objects/mirror/glassbg");
  mirror_fg = tex("objects/mirror/glassfg");
  m->dream = m->cs_ent = -1;
  spr_init(&m->glass, SB_glass);
  spr_play(&m->glass, A_glass_idle, true);
  m->hb_x = -32 + 8, m->hb_w = 64 - 16;
  m->shine_alpha = 0.5f;
  m->update_shine = 1;
  m->smashed = g_session.dreamdash;
  if (m->smashed) {
    spr_play(&m->glass, A_glass_broken, true);
    m->smash_ended = 1;
  } else {
    spr_init(&m->refl, SB_badeline);
    spr_play(&m->refl, A_badeline_idle, true);
    m->refl_on = 1, m->auto_refl = 1;
    m->refl_sx = 1;
    hair_start(&m->hair, v2(d->x, d->y), 1);
  }
}

/* ---------------------------------------------------------------- the story's helpers (ch1.c) */
#define EV_BEGIN       \
  if (c->wait > 0) {   \
    c->wait -= DT;     \
    return false;      \
  }                    \
  switch (c->co) {     \
    case 0:
#define EV_YIELD_(n) \
  do {               \
    c->co = (n);     \
    return false;    \
    case (n):;       \
  } while (0)
#define EV_YIELD EV_YIELD_(__COUNTER__ + 1)
#define EV_WAIT(t)   \
  do {               \
    c->wait = (t);   \
    EV_YIELD;        \
  } while (0)
#define EV_NEST(call)      \
  do {                     \
    EV_YIELD;              \
    while (call) EV_YIELD; \
    EV_YIELD;              \
  } while (0)
#define EV_END \
  default:;    \
  }            \
  return true
static Co *ev_start(Co *c, int8_t *at, int index) {
  if (*at != index) *at = (int8_t)index, memset(c, 0, sizeof *c);
  return c;
}
static Ent *find_class(const EntClass *cls) {
  for (int i = 0; i < g_nents; i++)
    if (g_ents[i].cls == cls && g_ents[i].dead != 1) return &g_ents[i];
  return NULL;
}
static void draw_npc(const Sprite *spr, float x, float y) {   /* Scale.X turns it */
  Sprite s = *spr;
  s.flipx = s.sx < 0;
  s.sx = fabsf(s.sx);
  spr_draw(&s, x, y);
}
/* Sprite.PlayRoutine's wait: true while the animation plays */
static bool spr_playing(const Sprite *s, int anim) { return s->anim == anim && s->playing; }

/* ---------------------------------------------------------------- BadelineDummy (ch2_bdummy_: chapters 3 and 4 too) */
typedef struct {
  Sprite spr;
  Hair hair;
  HairJob job;
  SineWave wave;
  Wiggler pop;                  /* BadelineAutoAnimator's */
  V2 float_normal, perp, from;
  float floatness, light, speed, p, clip[4], calpha;
  uint8_t step, syncing, last_anim, clipped;
} BDummy;
static const EntClass BDUMMY;
static float bd_facing(const BDummy *b) { return b->spr.sx < 0 ? -1.f : 1.f; }
static V2 bd_center(const Ent *e) { return v2(e->x, e->y - 4); }   /* Hitbox(6, 6, -3, -7) */
static void bdummy_update(Ent *e) {
  BDummy *b = ST(e, BDummy);
  b->hair.wave += DT * 4;   /* the components: Hair, Sprite, the auto animator (and its pop), the wave */
  spr_update(&b->spr);
  int bank, idle;
  bool sync = false;
  if (textbox_portrait(&bank, &idle) && bank == SB_portrait_badeline) {   /* BadelineAutoAnimator */
    if (idle == A_portrait_badeline_idle_scoff) {
      if (!b->syncing) b->last_anim = b->spr.anim;
      spr_play(&b->spr, A_badeline_laugh, false);
      b->syncing = 1, sync = 1;
    } else if (idle == A_portrait_badeline_idle_yell || idle == A_portrait_badeline_idle_freakA ||
               idle == A_portrait_badeline_idle_freakB || idle == A_portrait_badeline_idle_freakC) {
      if (!b->syncing) wiggler_restart(&b->pop), b->last_anim = b->spr.anim;
      spr_play(&b->spr, A_badeline_angry, false);
      b->syncing = 1, sync = 1;
    }
  }
  if (b->syncing && !sync) {
    b->syncing = 0;
    if (b->last_anim == 255 || b->last_anim == A_badeline_spin) b->last_anim = A_badeline_fallSlow;
    if (b->spr.anim == A_badeline_angry) wiggler_restart(&b->pop);
    spr_play(&b->spr, b->last_anim, false);
  }
  if (b->pop.active) {
    wiggler_update(&b->pop);
    float k = 1 + 0.25f * b->pop.value;
    b->spr.sx = bd_facing(b) * k, b->spr.sy = k;
  }
  sine_update(&b->wave);
  V2 off = v2mul(b->float_normal, b->wave.value * b->floatness);   /* Sprite.Position */
  hair_after_update(&b->hair, &b->spr, v2(e->x + off.x, e->y + off.y), (int)bd_facing(b));
}
static void bdummy_render(Ent *e) {
  BDummy *b = ST(e, BDummy);
  V2 off = v2mul(b->float_normal, b->wave.value * b->floatness);
  float x = floorf(e->x + off.x), y = floorf(e->y + off.y);   /* RenderPosition floored */
  if (!b->clipped) {
    b->job.cw = 0;
    hair_render_into(&b->hair, &b->spr, (int)bd_facing(b), rgb(0x9B3FB5), 1, &b->job);
    draw_npc(&b->spr, x, y);
  } else {   /* cut to a rect (the resort mirror's glass shows her) */
    int cx0 = (int)b->clip[0] - g_camx, cy0 = (int)b->clip[1] - g_camy, cx1 = (int)b->clip[2] - g_camx, cy1 = (int)b->clip[3] - g_camy;
    b->job.cx = (int16_t)cx0, b->job.cy = (int16_t)cy0, b->job.cw = (int16_t)(cx1 - cx0), b->job.ch = (int16_t)(cy1 - cy0);
    hair_render_into(&b->hair, &b->spr, (int)bd_facing(b), rgb(0x9B3FB5), b->calpha, &b->job);
    uint16_t t = spr_tex(&b->spr);
    Tex tx;
    if (t != 0xFFFF && tex_get(t, &tx)) {
      float ox = b->spr.justify ? tx.fw * b->spr.jx : b->spr.ox, oy = b->spr.justify ? tx.fh * b->spr.jy : b->spr.oy;
      bool flip = b->spr.sx < 0;
      int fx = (int)(x - (flip ? tx.fw - ox : ox)) - g_camx, fy = (int)(y - oy) - g_camy;   /* the frame's top left */
      int l = fx > cx0 ? fx : cx0, r = fx + tx.fw < cx1 ? fx + tx.fw : cx1, tp = fy > cy0 ? fy : cy0, bt = fy + tx.fh < cy1 ? fy + tx.fh : cy1;
      if (l < r && tp < bt)
        gfx_tex_part(t, (float)(l + g_camx), (float)(tp + g_camy), flip ? tx.fw - (r - fx) : l - fx, tp - fy, r - l, bt - tp, flip ? GF_FLIPX : 0,
                     0xFFFF, (uint8_t)(clampf(b->calpha, 0, 1) * 255));
    }
  }
  light_add(e->x, e->y - 8, 0xDB7093, b->light, 20, 60);
}
static const EntClass BDUMMY = {.size = sizeof(BDummy), .name = "BadelineDummy", .update = bdummy_update, .render = bdummy_render};
Ent *ch2_bdummy_new(V2 at) {
  Ent *e = ent_new(&BDUMMY, at.x, at.y);
  if (!e) return NULL;
  e->collidable = 0;
  BDummy *b = ST(e, BDummy);
  spr_init(&b->spr, SB_badeline);
  spr_play(&b->spr, A_badeline_fallSlow, false);
  b->spr.sx = -1;
  hair_start(&b->hair, at, -1);
  wiggler_init(&b->pop, 0.5f, 4);
  b->wave.freq = 0.25f;
  sine_set(&b->wave, 0);
  b->float_normal = v2(0, 1);
  b->floatness = 2;
  b->light = 1;
  b->last_anim = 255;
  return e;
}
void ch2_bdummy_appear(Ent *e) {   /* Appear (the displacement burst: no distortion here) */
  particles_emit(PL_MID, &P_BadelineOldsite_P_Vanish, 12, bd_center(e), v2(6, 6), P_BadelineOldsite_P_Vanish.direction);
}
void ch2_bdummy_vanish(Ent *e) {   /* Vanish */
  particles_emit(PL_MID, &P_BadelineOldsite_P_Vanish, 12, bd_center(e), v2(6, 6), P_BadelineOldsite_P_Vanish.direction);
  ent_remove(e);
}
void ch2_bdummy_face(Ent *e, int f) { ST(e, BDummy)->spr.sx = (float)f; }
void ch2_bdummy_clip(Ent *e, float x0, float y0, float x1, float y1, float alpha) {
  BDummy *b = ST(e, BDummy);
  b->clip[0] = x0, b->clip[1] = y0, b->clip[2] = x1, b->clip[3] = y1;
  b->calpha = alpha, b->clipped = 1;
}
void ch2_bdummy_floatness(Ent *e, float f) { ST(e, BDummy)->floatness = f; }
/* FloatTo: a nested step; flags: BD_TURN (turnAtEndTo: turn), BD_NOFACE, BD_FADELIGHT, BD_QUICKEND */
enum { BD_TURN = 1, BD_NOFACE = 2, BD_FADELIGHT = 4, BD_QUICKEND = 8 };
bool ch2_bdummy_float_to(Ent *e, V2 target, int turn, int flags) {
  BDummy *b = ST(e, BDummy);
  switch (b->step) {
    case 0: {
      spr_play(&b->spr, A_badeline_fallSlow, false);
      if (!(flags & BD_NOFACE) && signf(target.x - e->x) != 0) b->spr.sx = signf(target.x - e->x);
      V2 d = v2norm(v2sub(target, v2(e->x, e->y)));
      b->perp = v2(-d.y, d.x);
      b->speed = 0;
      b->step = 1;
    }
      /* fall through */
    case 1:
      if (e->x != target.x || e->y != target.y) {
        b->speed = approach(b->speed, 120, 240 * DT);
        V2 d = v2sub(target, v2(e->x, e->y));
        float l = v2len(d), k = b->speed * DT;
        if (l <= k) e->x = target.x, e->y = target.y;
        else e->x += d.x / l * k, e->y += d.y / l * k;
        b->floatness = approach(b->floatness, 4, 8 * DT);
        V2 n = v2sub(b->perp, b->float_normal);
        l = v2len(n), k = DT * 12;
        b->float_normal = l <= k ? b->perp : v2add(b->float_normal, v2mul(n, k / l));
        if (flags & BD_FADELIGHT) b->light = approach(b->light, 0, DT * 2);
        return true;
      }
      if (flags & BD_QUICKEND) b->floatness = 2;
      b->step = 2;
      /* fall through */
    default:
      if (b->floatness != 2) {
        b->floatness = approach(b->floatness, 2, 8 * DT);
        return true;
      }
      if (flags & BD_TURN) b->spr.sx = (float)turn;
      b->step = 0;
      return false;
  }
}
/* WalkTo */
bool ch2_bdummy_walk_to(Ent *e, float x, float speed) {
  BDummy *b = ST(e, BDummy);
  if (!b->step) {
    b->floatness = 0;
    spr_play(&b->spr, A_badeline_walk, false);
    if (signf(x - e->x) != 0) b->spr.sx = signf(x - e->x);
    b->step = 1;
  }
  if (e->x != x) {
    e->x = approach(e->x, x, DT * speed);
    return true;
  }
  spr_play(&b->spr, A_badeline_idle, false);
  b->step = 0;
  return false;
}
/* SmashBlock: the first DashBlock breaks */
bool ch2_bdummy_smash(Ent *e, V2 target) {
  BDummy *b = ST(e, BDummy);
  switch (b->step) {
    case 0:
      spr_play(&b->spr, A_badeline_dreamDashLoop, false);
      b->from = v2(e->x, e->y);
      b->p = 0, b->step = 1;
      /* fall through */
    case 1:
      if (b->p < 1) {
        V2 at = v2lerp(b->from, target, ease_cube_out(b->p));
        e->x = at.x, e->y = at.y;
        b->p += DT * 6;
        return true;
      }
      for (int i = 0; i < g_nents; i++)
        if (g_ents[i].cls && g_ents[i].dead != 1 && level_is_dash_block(&g_ents[i])) {
          level_break_dash_block(&g_ents[i], v2(0, -1));
          break;
        }
      spr_play(&b->spr, A_badeline_idle, false);
      b->p = 0, b->step = 2;
      /* fall through */
    case 2:
      if (b->p < 1) {
        V2 at = v2lerp(target, b->from, ease_cube_out(b->p));
        e->x = at.x, e->y = at.y;
        b->p += DT * 4;
        return true;
      }
      spr_play(&b->spr, A_badeline_fallSlow, false);
      b->step = 0;
      return false;
  }
  return false;
}

/* ---------------------------------------------------------------- Selfie (ch2_selfie_: chapter 4's too) */
/* the photos: Portraits' selfie (with selfieFilter sliding over it) and selfieGondola */
enum { SF_PICTURE, SF_FILTER, SF_OPEN };
typedef struct { Co co; float p, timer, rot, filter; V2 pos; uint16_t photo; uint8_t mode, waiting, done; } Selfie;
static bool selfie_confirm(void) { return btn_pressed(&g_in.confirm) || btn_pressed(&g_in.back) || btn_pressed(&g_in.jump) || btn_pressed(&g_in.dash); }
static void selfie_update(Ent *e) {
  Selfie *s = ST(e, Selfie);
  if (s->waiting) s->timer += DT;
  Co *c = &s->co;
  CO_BEGIN(c);
  if (s->mode == SF_PICTURE) {
    ch1_flash(0xFFFFFF);
    CO_WAIT(c, 0.5f);
  }
  for (s->p = 0; s->p < 1;) {   /* OpenRoutine */
    s->p += DT;
    s->pos = v2lerp(v2(992, 1080 + 400), v2(960, 540), ease_cube_out(s->p));
    s->rot = lerpf(0.5f, 0, ease_back_out(s->p));
    CO_YIELD(c);
  }
  if (s->mode == SF_FILTER) {
    CO_WAIT(c, 0.5f);
    for (s->p = 0; s->p < 1; s->p += DT / 0.4f) {   /* the filter slides in */
      s->filter = ease_sine_inout(fminf(s->p, 1));
      CO_YIELD(c);
    }
    s->filter = 1;
  }
  s->waiting = 1;   /* WaitForInput */
  CO_YIELD(c);
  while (!selfie_confirm()) CO_YIELD(c);
  s->waiting = 0;
  if (s->mode == SF_OPEN) {
    s->done = 1;
    return;
  }
  for (s->p = 0; s->p < 1;) {   /* EndRoutine */
    s->p += DT * 2;
    s->pos = v2lerp(v2(960, 540), v2(928, -400), ease_back_in(s->p));
    s->rot = lerpf(0, -0.15f, ease_back_in(s->p));
    CO_YIELD(c);
  }
  CO_YIELD(c);
  s->done = 1;
  ent_remove(e);
  CO_END(c);
}
static void selfie_render(Ent *e) {
  Selfie *s = ST(e, Selfie);
  if (g_level.frozen || g_level.paused || g_level.skipping_cutscene || !s->p) return;
  float x = s->pos.x / 6, y = s->pos.y / 6, h = 800 / 6.f;
  gfx_hud(true);
  Tex t;
  if (tex_get(s->photo, &t)) {
    h = t.fh * t.scale;
    gfx_tex_ex(s->photo, x, y, t.fw * t.scale / 2.f, h / 2, 1, 1, s->rot, 0xFFFF, 255, 0);
  }
  int at = s->filter > 0 && tex_get(T__selfieFilter, &t) ? (int)roundf(t.fw * s->filter) : 0;
  if (at > 0)   /* overImage: the filter's right part, as wide as it has come, right edges together */
    gfx_tex_part_ex(T__selfieFilter, x, y, t.fw - at, 0, at, t.fh, at - t.fw / 2.f, t.fh / 2.f, 1, 1, s->rot, 0xFFFF, 255, 0);
  if (s->waiting) {
    float bx = x + h / 2 + 40 / 6.f, by = y + h / 2 + (fmodf(s->timer, 1) < 0.25f ? 1 : 0);
    if (tex_get(T__textboxbutton, &t)) gfx_tex_ex(T__textboxbutton, bx, by, t.fw * t.scale / 2.f, t.fh * t.scale / 2.f, 1, 1, 0, 0xFFFF, 255, 0);
  }
  gfx_hud(false);
}
static const EntClass SELFIE = {.size = sizeof(Selfie), .name = "Selfie", .update = selfie_update, .render = selfie_render};
Ent *ch2_selfie_new(int mode) {
  Ent *e = ent_new(&SELFIE, 0, 0);
  if (!e) return NULL;
  e->tags = TAG_HUD;
  e->collidable = 0;
  ST(e, Selfie)->mode = (uint8_t)mode;
  ST(e, Selfie)->photo = mode == SF_OPEN ? T__selfieGondola : T__selfie;
  return e;
}
/* the routine has run: true while it runs (its entity is kept for an open picture) */
bool ch2_selfie_running(Ent *e) { return e && e->cls == &SELFIE && e->dead != 1 && !ST(e, Selfie)->done; }

/* ---------------------------------------------------------------- Payphone */
typedef struct {
  Sprite spr;
  float flicker, flicker_for, light2;
  int8_t last_frame, blink_x;
  uint8_t light, blink, broken, light2_on;
} Phone;
static const EntClass PAYPHONE;
static void phone_update(Ent *e) {
  Phone *p = ST(e, Phone);
  spr_update(&p->spr);
  if (!p->broken) {
    p->flicker -= DT;
    if (p->flicker <= 0) {
      if (level_on_interval(0.025f)) {
        bool on = rndf() > 0.5f;
        p->light = on, p->blink = !on;
      }
      if (p->flicker < -p->flicker_for) {
        static const float A[4] = {0.4f, 0.6f, 0.8f, 1};
        static const float B[3] = {0.1f, 0.2f, 0.05f};
        p->flicker = A[rndi(4)];
        p->flicker_for = B[rndi(3)];
        p->light = 1, p->blink = 0;
      }
    }
  } else
    p->light = p->blink = 0;
  if (p->spr.anim == A_payphone_eat && p->spr.frame == 5 && p->last_frame != 5) {
    V2 at = v2add(g_level.cam, v2(236, 152));
    particles_emit(PL_FG, &P_Payphone_P_Snow, 10, at, v2(10, 0), P_Payphone_P_Snow.direction);
    particles_emit(PL_FG, &P_Payphone_P_SnowB, 8, at, v2(6, 0), P_Payphone_P_SnowB.direction);
    level_dir_shake(v2(0, 1), 0.3f);
  }
  p->last_frame = (int8_t)p->spr.frame;
}
static void phone_render(Ent *e) {
  Phone *p = ST(e, Phone);
  spr_draw(&p->spr, e->x, e->y);
  if (p->blink) gfx_tex(T_cutscenes_payphone_blink, e->x - 56 + p->blink_x, e->y - 80, 0, 0xFFFF, 255);   /* at the sprite's origin */
  if (p->light) {
    light_add(e->x - 6, e->y - 45, 0xFFFFFF, 1, 8, 96);   /* a spotlight, downwards */
    bloom_add(e->x - 6, e->y - 45, 0.8f, 8);
  }
  if (p->light2_on) light_add(e->x + 16, e->y - 28, 0xFFFFFF, p->light2, 32, 48);
}
static const EntClass PAYPHONE = {.name = "payphone", .size = sizeof(Phone), .update = phone_update, .render = phone_render};
static void new_payphone(const EData *d) {
  Ent *e = ent_new(&PAYPHONE, d->x, d->y);
  if (!e) return;
  e->depth = 1;
  e->collidable = 0;
  Phone *p = ST(e, Phone);
  spr_init(&p->spr, SB_payphone);
  spr_play(&p->spr, A_payphone_idle, false);
  p->light = 1;
  p->flicker_for = 0.1f;
}

/* ---------------------------------------------------------------- CS02_BadelineIntro */
typedef struct {
  Cutscene cs;
  Ent *bad, *tb;
  V2 end, from, cam_to;
  Step step, cam;
  Co ev;
  float t;
  int8_t evi;
  uint8_t cam_on;
} BIntro;
static bool bintro_event(void *ctx, int index) {   /* TurnAround, RevealBadeline, StartLaughing, StopLaughing */
  BIntro *s = ST((Ent *)ctx, BIntro);
  Ent *be = s->bad;
  Chaser *ch = ST(be, Chaser);
  Player *p = &g_player;
  Co *c = ev_start(&s->ev, &s->evi, index);
  switch (index) {
    case 0: {
      EV_BEGIN;
      p->facing = -1;
      EV_WAIT(0.2f);
      step_reset(&s->cam);   /* Add(new Coroutine(CameraTo((Bounds.X, Camera.Y), 0.5))) */
      s->cam_to = v2((float)g_level.room->x, g_level.cam.y);
      s->cam_on = 1;
      step_reset(&s->step);
      EV_NEST(zoom_to(&s->step, v2(84, 135), 2, 0.5f));
      EV_WAIT(0.2f);
      EV_END;
    }
    case 1: {
      EV_BEGIN;
      EV_WAIT(0.1f);
      EV_WAIT(0.1f);   /* (the displacement burst: no distortion here) */
      ch->hovering = 1, ch->hair_visible = 1;
      spr_play(&ch->spr, A_badeline_fallSlow, false);
      s->from = v2(be->x, be->y);
      for (s->t = 0; s->t < 1; s->t += DT) {
        V2 at = v2lerp(s->from, s->end, ease_cube_inout(s->t));
        be->x = at.x, be->y = at.y;
        EV_YIELD;
      }
      p->facing = (int)signf(be->x - p->ent->x);
      EV_WAIT(1);
      EV_END;
    }
    case 2: {
      EV_BEGIN;
      EV_WAIT(0.2f);
      spr_play(&ch->spr, A_badeline_laugh, true);
      EV_YIELD;
      EV_END;
    }
    default: {
      EV_BEGIN;
      spr_play(&ch->spr, A_badeline_fallSlow, true);
      EV_YIELD;
      EV_END;
    }
  }
}
static void bintro_end(Ent *e, bool skipped) {   /* OnEnd */
  (void)skipped;
  BIntro *s = ST(e, BIntro);
  Player *p = level_player();
  if (p) {
    p->state_locked = false;
    p->facing = -1;
    player_set_state(p, ST_NORMAL);
    p->just_respawned = true;
  }
  Ent *be = s->bad;
  Chaser *c = ST(be, Chaser);
  be->x = s->end.x, be->y = s->end.y;
  be->visible = 1;
  c->hair_visible = 1;
  spr_play(&c->spr, A_badeline_fallSlow, false);
  c->hovering = 0;
  chaser_start_chasing(be);
  level_set_flag("evil_maddy_intro", true);
}
static void bintro_update(Ent *e) {
  BIntro *s = ST(e, BIntro);
  if (s->cam_on && !camera_to(&s->cam, s->cam_to, 0.5f, NULL)) s->cam_on = 0;
  Player *p = level_player();
  Co *c = &s->cs.co;
  CO_BEGIN(c);
  while (!p) {
    CO_YIELD(c);
    p = level_player();
  }
  while (!actor_on_ground(p->ent, 1)) CO_YIELD(c);
  player_set_state(p, ST_DUMMY);
  p->state_locked = true;
  CO_WAIT(c, 1);
  s->evi = -1;
  CO_SAY(c, s->tb, "CH2_BADELINE_INTRO", bintro_event, e);
  step_reset(&s->step);
  CO_NEST(c, zoom_back(&s->step, 0.5f));
  cutscene_end(e);
  return;
  CO_END(c);
}
static const EntClass BINTRO = {.size = sizeof(BIntro), .name = "CS02_BadelineIntro", .update = bintro_update};
static void bintro_start(Ent *bad, V2 end) {
  Ent *e = ent_new(&BINTRO, 0, 0);
  if (!e) return;
  e->collidable = 0, e->visible = 0;
  BIntro *s = ST(e, BIntro);
  s->bad = bad;
  s->end = end;
  cutscene_start(e, bintro_end, true, false);
}

/* ---------------------------------------------------------------- CS02_DreamingPhonecall, CS02_Ending */
typedef struct {
  Cutscene cs;
  Ent *phone, *tb, *evil;
  Walk walk;
  Step step;
  Co ev;
  V2 wire_speed;
  float light, t, wire_wait;
  int8_t evi;
  uint8_t wire_on, end;
} Call;
static bool call_event(void *ctx, int index) {   /* ShowShadowMadeline */
  (void)index;
  Call *s = ST((Ent *)ctx, Call);
  Phone *ph = ST(s->phone, Phone);
  Co *c = ev_start(&s->ev, &s->evi, 0);
  EV_BEGIN;
  step_reset(&s->step);
  EV_NEST(zoom_to(&s->step, v2(240, 116), 2, 0.5f));
  s->evil = ch2_bdummy_new(v2add(v2(s->phone->x, s->phone->y), v2(32, -24)));
  if (s->evil) ch2_bdummy_appear(s->evil);
  EV_WAIT(0.2f);
  ph->blink_x++;
  spr_play(&ph->spr, A_payphone_jumpBack, false);
  EV_NEST(spr_playing(&ph->spr, A_payphone_jumpBack));
  spr_play(&ph->spr, A_payphone_scare, false);
  EV_NEST(spr_playing(&ph->spr, A_payphone_scare));
  EV_WAIT(1.2f);
  EV_END;
}
static void call_wire(Call *s) {   /* WireFalls */
  if (s->wire_wait > 0) {
    s->wire_wait -= DT;
    return;
  }
  Ent *w = NULL;
  for (int i = 0; i < g_nents && !w; i++)
    if (g_ents[i].cls && g_ents[i].dead != 1 && g_ents[i].cls->name && !strcmp(g_ents[i].cls->name, "wire")) w = &g_ents[i];
  V2 *b = w ? wire_curve_begin(w) : NULL;
  if (!b || b->x >= g_level.room->x + g_level.room->w) {
    s->wire_on = 0;
    return;
  }
  s->wire_speed = v2add(s->wire_speed, v2mul(v2(0.7f, 1), 200 * DT));
  *b = v2add(*b, v2mul(s->wire_speed, DT));
}
static Ent *call_wiping;
static void call_wiped(void) {   /* the FadeWipe's end: EndCutscene(level) */
  if (call_wiping && call_wiping->cls) cutscene_end(call_wiping);
  call_wiping = NULL;
}
static void call_end(Ent *e, bool skipped) {
  (void)skipped;
  Call *s = ST(e, Call);
  if (s->end) {   /* CS02_Ending */
    level_complete_area(true, false);
    return;
  }
  zoom_reset();
  g_session.dreaming = 0;
  int r = chapter_find_room(g_session.chapter, "end_0");
  if (r >= 0) level_load_room(r, INTRO_WAKEUP);
  g_level.in_cutscene = false;
}
static void call_update(Ent *e) {
  Call *s = ST(e, Call);
  Ent *pe = s->phone;
  Phone *ph = ST(pe, Phone);
  Player *p = &g_player;
  if (s->wire_on) call_wire(s);
  if (ph->light2_on == 1 && (ph->light2 += DT / 2) >= 1) ph->light2 = 1, ph->light2_on = 2;   /* its Tween */
  Co *c = &s->cs.co;
  CO_BEGIN(c);
  player_set_state(p, ST_DUMMY);
  p->dashes = 1;
  if (!s->end) CO_WAIT(c, 0.3f);
  for (s->light = 1; s->light > 0;) {   /* the player's light fades */
    s->light -= DT * (s->end ? 1.25f : 2);
    CO_YIELD(c);
  }
  if (s->end) {   /* CS02_Ending */
    CO_WAIT(c, 1);
    s->walk = walk_to(pe->x - 4);
    CO_NEST(c, player_walk(p, &s->walk));
    CO_WAIT(c, 0.2f);
    p->facing = 1;
    CO_WAIT(c, 0.5f);
    p->ent->visible = 0;
    spr_play(&ph->spr, A_payphone_pickUp, false);
    CO_NEST(c, spr_playing(&ph->spr, A_payphone_pickUp));
    CO_WAIT(c, 0.25f);
    CO_WAIT(c, 6);
    spr_play(&ph->spr, A_payphone_talkPhone, false);
    CO_SAY(c, s->tb, "CH2_END_PHONECALL", NULL, NULL);
    CO_WAIT(c, 0.3f);
    cutscene_end(e);
    return;
  }
  CO_WAIT(c, 3.2f);
  s->walk = walk_to(pe->x - 24);
  CO_NEST(c, player_walk(p, &s->walk));
  CO_WAIT(c, 1.5f);
  p->facing = -1;
  CO_WAIT(c, 1.5f);
  p->facing = 1;
  CO_WAIT(c, 0.25f);
  s->walk = walk_to(pe->x - 4);
  CO_NEST(c, player_walk(p, &s->walk));
  CO_WAIT(c, 1.5f);
  p->ent->visible = 0;
  spr_play(&ph->spr, A_payphone_pickUp, false);
  CO_NEST(c, spr_playing(&ph->spr, A_payphone_pickUp));
  CO_WAIT(c, 1);
  spr_play(&ph->spr, A_payphone_talkPhone, false);
  s->evi = -1;
  CO_SAY(c, s->tb, "CH2_DREAM_PHONECALL", call_event, e);
  if (s->evil) {
    ch2_bdummy_vanish(s->evil);
    s->evil = NULL;
    CO_WAIT(c, 1);
  }
  s->wire_on = 1, s->wire_wait = 0.5f;
  ph->broken = 1;
  level_shake(0.2f);
  ph->light2_on = 1, ph->light2 = 0;
  spr_play(&ph->spr, A_payphone_transform, false);
  CO_NEST(c, spr_playing(&ph->spr, A_payphone_transform));
  CO_WAIT(c, 0.4f);
  spr_play(&ph->spr, A_payphone_eat, false);
  CO_NEST(c, spr_playing(&ph->spr, A_payphone_eat));
  spr_play(&ph->spr, A_payphone_monsterIdle, false);
  CO_WAIT(c, 1.2f);
  g_level.in_cutscene = false;   /* level.EndCutscene(), then the wipe ends this one */
  call_wiping = e;
  wipe_start(WIPE_FADE, false, call_wiped);
  for (;;) CO_YIELD(c);
  CO_END(c);
}
static const EntClass CALL = {.size = sizeof(Call), .name = "CS02_DreamingPhonecall", .update = call_update};

/* ---------------------------------------------------------------- CS02_Journal and its PoemPage */
/* The pages (PoemPage here, CS03_Memo's MemoPage: ch2_page_new): GFX.Gui's poempage, memo/memo and
 * memo/title are not packed, so a page of their color (855 x 580, and 624 x 920 x 1.5 interface pixels)
 * with the text at the font's size (FancyText's 0.7 and 0.75 scales are not drawn); their rotation neither */
typedef struct { V2 pos; float alpha, h; uint8_t out; } PageCtl;   /* what the cutscenes move (ch3.c too) */
#define PAGE_N 640
typedef struct {
  PageCtl c;
  float timer, tx, ty, tw, maxw, widest;   /* where its text goes (its layer reads them) */
  int16_t n;
  uint8_t memo, loaded, ta;
  char txt[PAGE_N];
} Page;
static float page_line(const Page *g, int at, int *end, float maxw) {   /* the words of a line that fit */
  float w = 0;
  int i = at, last = -1;
  while (i < g->n && g->txt[i] != '\n') {
    int j = i;
    while (j < g->n && g->txt[j] != ' ' && g->txt[j] != '\n') j++;
    float ww = font_measure(g->txt + at, j - at, FONT_S);
    if (ww > maxw && last >= 0) break;
    w = ww, last = j, i = j;
    if (i < g->n && g->txt[i] == ' ') i++;
  }
  *end = last >= 0 ? last : i;
  return w;
}
void ch1_glyph(uint16_t *strip, int sy0, int sy1, uint32_t ch, float x, float top, bool flip, uint16_t col, uint8_t alpha);
/* the text, a layer of its own (one command for all its glyphs) */
static void page_text(uint16_t *strip, int sy0, int sy1, void *ctx) {
  Page *g = ctx;
  float ly = g->ty, lh = font_line_height(FONT_S);
  for (int i = 0, end; i < g->n; i = end + 1, ly += lh) {
    float lw = page_line(g, i, &end, g->maxw);
    while (i < end && g->txt[i] == ' ') i++;
    if (ly + lh < sy0 || ly >= sy1) continue;
    float x = g->tx + g->tw / 2 - (g->memo ? g->widest : lw) / 2;
    for (const char *p = g->txt + i, *q = g->txt + end; p < q;) {
      uint32_t c = utf8_next(&p, q);
      const char *t = p;
      ch1_glyph(strip, sy0, sy1, c, x, ly, false, 0, g->ta);
      x += font_glyph_advance(FONT_S, c, p < q ? utf8_next(&t, q) : 0);
    }
  }
}
static void page_update(Ent *e) {
  Page *g = ST(e, Page);
  g->timer += DT;
  if (!g->loaded) g->loaded = 1, g->n = (int16_t)ch1_clean_lines(g->memo ? "CH3_MEMO" : "CH2_POEM", g->txt, PAGE_N);
}
static void page_render(Ent *e) {
  Page *g = ST(e, Page);
  if (g_level.frozen || g_level.paused || g_level.skipping_cutscene || g->c.alpha <= 0 || !g->loaded) return;
  uint8_t a = (uint8_t)(clampf(g->c.alpha, 0, 1) * 255);
  float w = g->memo ? 936 / 6.f : 855 / 6.f, h = g->memo ? 1380 / 6.f : 580 / 6.f;
  /* the poem's lines break where the game's do (its width at 0.7, in the body font's pixels: 1/4.8) */
  float maxw = g->memo ? (936 - 120) / 6.f : (855 - 120) / 0.7f / 4.8f, lh = font_line_height(FONT_S);
  int lines = 0;
  float widest = 0;
  for (int i = 0, end; i < g->n; i = end + 1, lines++) widest = fmaxf(widest, page_line(g, i, &end, maxw));
  /* (the text at its full size takes more room: the page grows around it) */
  if (g->memo) h = fmaxf(h, 210 / 6.f + lines * lh + 20);
  else w = fmaxf(w, widest + 120 / 6.f), h = fmaxf(h, lines * lh + 120 / 6.f);
  float x = g->c.pos.x / 6 - w / 2, y = g->c.pos.y / 6 - (g->memo ? 0 : h / 2);   /* origin: (w / 2, 0) or the middle */
  g->c.h = h;
  gfx_hud(true);
  gfx_rect(x, y, w, h, g->memo ? rgb(0xE4DAC5) : rgb(0xDED1C9), a);
  /* the poem: DrawJustifyPerLine in the middle; the memo: Draw at (w / 2, 210) justified (0.5, 0) */
  g->ty = g->memo ? y + 210 / 6.f : y + h / 2 - lines * lh / 2;
  g->tx = x, g->tw = w, g->maxw = maxw, g->widest = widest, g->ta = (uint8_t)(a * 0.6f);
  gfx_custom(page_text, g, (int)g->ty - 2, (int)(g->ty + lines * lh) + 2);
  if (!g->c.out) {
    Tex t;
    float bx = x + w + 40 / 6.f, by = y + h + (fmodf(g->timer, 1) < 0.25f ? 1 : 0);
    if (tex_get(T__textboxbutton, &t)) gfx_tex_ex(T__textboxbutton, bx, by, t.fw * t.scale / 2.f, t.fh * t.scale / 2.f, 1, 1, 0, 0xFFFF, 255, 0);
  }
  gfx_hud(false);
}
static const EntClass PAGE = {.size = sizeof(Page), .name = "PoemPage", .update = page_update, .render = page_render};
Ent *ch2_page_new(bool memo) {
  Ent *e = ent_new(&PAGE, 0, 0);
  if (!e) return NULL;
  e->tags = TAG_HUD;
  e->collidable = 0;
  ST(e, Page)->memo = memo;
  return e;
}
typedef struct { Cutscene cs; Ent *tb, *page; float p; V2 from; } Journal;
static void journal_end(Ent *e, bool skipped) {
  (void)skipped;
  Journal *s = ST(e, Journal);
  Player *p = level_player();
  if (p) p->state_locked = false, player_set_state(p, ST_NORMAL);
  level_set_flag("poem_read", true);
  if (s->page && s->page->cls == &PAGE) ent_remove(s->page);
}
static void journal_update(Ent *e) {
  Journal *s = ST(e, Journal);
  Player *p = &g_player;
  PageCtl *pg = s->page && s->page->cls == &PAGE ? ST(s->page, PageCtl) : NULL;
  Co *c = &s->cs.co;
  CO_BEGIN(c);
  player_set_state(p, ST_DUMMY);
  p->state_locked = true;
  if (!level_get_flag("poem_read")) {
    CO_SAY(c, s->tb, "ch2_journal", NULL, NULL);
    CO_WAIT(c, 0.1f);
  }
  s->page = ch2_page_new(false);
  if (!s->page) goto done;
  CO_YIELD(c);
  for (s->p = 0; s->p < 1; s->p += DT) {   /* EaseIn (its rotation, -0.1 to 0.05, is not drawn) */
    pg->pos = v2lerp(v2(960, 740), v2(960, 540), ease_cube_out(s->p));
    pg->alpha = ease_cube_out(s->p);
    CO_YIELD(c);
  }
  while (!btn_pressed(&g_in.confirm) && !btn_pressed(&g_in.jump) && !btn_pressed(&g_in.dash)) CO_YIELD(c);
  pg->out = 1;   /* EaseOut */
  s->from = pg->pos;
  for (s->p = 0; s->p < 1; s->p += DT * 1.5f) {
    pg->pos = v2lerp(s->from, v2(960, 340), ease_cube_in(s->p));
    pg->alpha = 1 - ease_cube_in(s->p);
    CO_YIELD(c);
  }
  ent_remove(s->page);
  s->page = NULL;
done:
  cutscene_end(e);
  return;
  CO_END(c);
}
static const EntClass JOURNAL = {.size = sizeof(Journal), .name = "CS02_Journal", .update = journal_update};
static void journal_start(Ent *trig, Player *p) {
  (void)trig, (void)p;
  Ent *e = ent_new(&JOURNAL, 0, 0);
  if (e) e->collidable = 0, e->visible = 0, cutscene_start(e, journal_end, true, false);
}

/* ---------------------------------------------------------------- NPC02_Theo */
typedef struct {
  Talk talk;
  Sprite spr;
  uint8_t has_talk;
} Theo2;
typedef struct {
  Cutscene cs;
  Ent *theo, *tb, *selfie;
  NpcWalk nw;
  Co ev;
  int8_t evi, conv, intro;
} Theo2Talk;
static bool theo2_event(void *ctx, int index) {   /* ShowPhotos, HidePhotos, Selfie, PlayerApproach48px / SelfieFiltered */
  Theo2Talk *s = ST((Ent *)ctx, Theo2Talk);
  Ent *te = s->theo;
  Theo2 *t = ST(te, Theo2);
  Co *c = ev_start(&s->ev, &s->evi, index);
  if (s->conv == 1) index = 4;   /* CH2_THEO_B: SelfieFiltered */
  switch (index) {
    case 0: {
      EV_BEGIN;
      EV_NEST(npc_approach(&s->nw, te, &t->spr, 10, 0));
      spr_play(&t->spr, A_theo_getPhone, false);
      EV_WAIT(2);
      EV_END;
    }
    case 1: {
      EV_BEGIN;
      spr_play(&t->spr, A_theo_idle, false);
      EV_WAIT(0.5f);
      EV_END;
    }
    case 2: {
      EV_BEGIN;
      EV_WAIT(0.5f);
      t->spr.sx = -t->spr.sx;
      spr_play(&t->spr, A_theo_takeSelfie, false);
      EV_WAIT(1);
      s->selfie = ch2_selfie_new(SF_PICTURE);
      EV_NEST(ch2_selfie_running(s->selfie));
      s->selfie = NULL;
      t->spr.sx = -t->spr.sx;
      EV_END;
    }
    case 3: {
      EV_BEGIN;
      EV_NEST(npc_approach(&s->nw, te, &t->spr, 48, 0));
      EV_END;
    }
    default: {
      EV_BEGIN;
      s->selfie = ch2_selfie_new(SF_FILTER);
      EV_NEST(ch2_selfie_running(s->selfie));
      s->selfie = NULL;
      EV_END;
    }
  }
}
static void theo2_talk_end(Ent *e, bool skipped) {   /* OnTalkEnd */
  Theo2Talk *s = ST(e, Theo2Talk);
  Ent *te = s->theo;
  Theo2 *t = ST(te, Theo2);
  int conv = ch1_counter("theo");
  if (conv == 4) {
    level_set_flag("theoDoneTalking", true);
    if (t->has_talk) t->has_talk = 0, ch1_talk_remove(te);
  }
  Player *p = level_player();
  if (p) {
    p->state_locked = false;
    player_set_state(p, ST_NORMAL);
    if (skipped) p->ent->x = (float)(int)(te->x + 48), p->facing = -1;
  }
  t->spr.sx = 1;
  if (s->selfie && s->selfie->cls == &SELFIE) ent_remove(s->selfie);
  ch1_set_counter("theo", conv + 1);
}
static void theo2_talk_update(Ent *e) {   /* Talk */
  static const char *const KEYS[5] = {NULL, "CH2_THEO_B", "CH2_THEO_C", "CH2_THEO_D", "CH2_THEO_E"};
  Theo2Talk *s = ST(e, Theo2Talk);
  Ent *te = s->theo;
  Theo2 *t = ST(te, Theo2);
  Co *c = &s->cs.co;
  CO_BEGIN(c);
  s->evi = -1;
  s->conv = (int8_t)ch1_counter("theo");
  if (!save_flag(0) || !save_flag(1)) {
    s->intro = !save_flag(0) ? 0 : 1;
    level_set_flag("hadntMetTheoAtStart", true);
    save_set_flag(s->intro);
    CO_NEST(c, npc_approach(&s->nw, te, &t->spr, 48, 1));
    CO_SAY(c, s->tb, s->intro ? "CH2_THEO_INTRO_NEVER_INTRODUCED" : "CH2_THEO_INTRO_NEVER_MET", NULL, NULL);
  } else if (s->conv <= 0) {
    s->conv = 0;
    CO_NEST(c, npc_approach(&s->nw, te, &t->spr, 0, 1));
    CO_WAIT(c, 0.2f);
    if (level_get_flag("hadntMetTheoAtStart")) {
      CO_NEST(c, npc_approach(&s->nw, te, &t->spr, 48, 0));
      CO_SAY(c, s->tb, "CH2_THEO_A", theo2_event, e);
    } else
      CO_SAY(c, s->tb, "CH2_THEO_A_EXT", theo2_event, e);
  } else if (s->conv <= 4) {
    CO_NEST(c, npc_approach(&s->nw, te, &t->spr, 48, 1));
    CO_SAY(c, s->tb, KEYS[s->conv], theo2_event, e);
  }
  cutscene_end(e);
  return;
  CO_END(c);
}
static const EntClass THEO2TALK = {.size = sizeof(Theo2Talk), .name = "NPC02_Theo.Talk", .update = theo2_talk_update};
static void theo2_update(Ent *e) {
  Theo2 *t = ST(e, Theo2);
  spr_update(&t->spr);
  if (t->has_talk && talk_update(&t->talk, e)) {   /* OnTalk */
    if (!save_flag(0) || !save_flag(1)) ch1_set_counter("theo", -1);
    Ent *c = ent_new(&THEO2TALK, 0, 0);
    if (!c) return;
    c->collidable = 0, c->visible = 0;
    ST(c, Theo2Talk)->theo = e;
    cutscene_start(c, theo2_talk_end, true, false);
  }
}
static void theo2_render(Ent *e) { draw_npc(&ST(e, Theo2)->spr, e->x, e->y); }
static const EntClass THEO2 = {.size = sizeof(Theo2), .name = "NPC02_Theo", .update = theo2_update, .render = theo2_render};
static void new_theo2(const EData *d) {
  Ent *e = ent_new(&THEO2, d->x, d->y);
  if (!e) return;
  e->depth = 1000;
  ent_box(e, 8, 8, -4, -8);
  Theo2 *t = ST(e, Theo2);
  spr_init(&t->spr, SB_theo);
  spr_play(&t->spr, A_theo_idle, false);
  if (!level_get_flag("theoDoneTalking")) t->has_talk = 1, ch1_talk_add(e, &t->talk, -20, -8, 100, 8, v2(0, -24));
}

/* ---------------------------------------------------------------- triggers */
/* EventTrigger: the chapter's two endings */
typedef struct { uint8_t which, triggered; } Event;
static void event_enter(Ent *e, Player *p) {
  (void)p;
  Event *ev = ST(e, Event);
  if (ev->triggered) return;
  ev->triggered = 1;
  Ent *ph = find_class(&PAYPHONE);
  Ent *c = ph ? ent_new(&CALL, 0, 0) : NULL;
  if (!c) return;
  c->collidable = 0, c->visible = 0;
  Call *s = ST(c, Call);
  s->phone = ph;
  s->end = ev->which == 2;
  if (s->end) level_register_complete();   /* CS02_Ending.OnBegin */
  cutscene_start(c, call_end, false, s->end);
}
static const EntClass EVENTTRIG = {.name = "eventTrigger", .size = sizeof(Event), .kind = KIND_TRIGGER,
                                   .more = &(const EntMore){.on_enter = event_enter}};

/* InteractTrigger: talk to read something (ch2_interact_new: chapter 3's too) */
typedef struct { Talk talk; void (*on_talk)(Ent *e, Player *p); } Interact;
static void interact_update(Ent *e) {
  Interact *it = ST(e, Interact);
  Player *p = level_player();
  if (talk_update(&it->talk, e) && p && it->on_talk) it->on_talk(e, p);
}
static const EntClass INTERACT = {.name = "interactTrigger", .size = sizeof(Interact), .update = interact_update};
void ch2_interact_new(const EData *d, void (*on_talk)(Ent *e, Player *p)) {
  Ent *e = ent_new(&INTERACT, d->x, d->y);
  if (!e) return;
  float w = EA(d, interactTrigger, width), h = EA(d, interactTrigger, height);
  ent_box(e, w, h, 0, 0);
  e->visible = 0;
  Interact *it = ST(e, Interact);
  ch1_talk_add(e, &it->talk, 0, 0, (int)w, (int)h, d->nnodes ? v2sub(ed_node(d, 0), v2(d->x, d->y)) : v2(w / 2, 0));
  it->talk.must_face = false;
  it->on_talk = on_talk;
}


/* RespawnTargetTrigger: where the respawn point goes when a transition ends inside it */
typedef struct { V2 target; uint8_t was_transitioning; } RespawnTarget;
static void rtarget_update(Ent *e) {
  RespawnTarget *r = ST(e, RespawnTarget);
  if (r->was_transitioning && !g_level.transitioning) {   /* Level.TransitionRoutine's end */
    Ent *p = level_player_ent();
    if (p && collide_ent_at(p, p->x, p->y, e)) {
      V2 s = level_closest_spawn(r->target);
      g_session.rx = (int32_t)s.x, g_session.ry = (int32_t)s.y;
    }
  }
  r->was_transitioning = g_level.transitioning;
}
static const EntClass RTARGET = {.name = "respawnTargetTrigger", .size = sizeof(RespawnTarget), .update = rtarget_update};

/* ---------------------------------------------------------------- the factories */
bool ents_ch2(const EData *d) {
  switch (d->type) {
    case ET_dreamBlock: new_dreamblock(d); return true;
    case ET_darkChaser: new_chaser(d); return true;
    case ET_touchSwitch: new_touchswitch(d); return true;
    case ET_switchGate: new_switchgate(d); return true;
    case ET_hanginglamp: new_lamp(d); return true;
    case ET_floatingDebris: new_fdebris(d); return true;
    case ET_foregroundDebris: new_fgdebris(d); return true;
    case ET_invisibleBarrier: new_barrier(d, EA(d, invisibleBarrier, width), EA(d, invisibleBarrier, height)); return true;
    case ET_payphone: new_payphone(d); return true;
    case ET_towerviewer: new_lookout(d); return true;
    case ET_dreammirror: new_mirror(d); return true;
    case ET_npc:
      if (strcmp(EAS(d, npc, npc), "theo_02_campfire")) return false;
      new_theo2(d);
      return true;
  }
  return false;
}

bool trigs_ch2(const EData *d) {
  switch (d->type) {
    case TT_eventTrigger: {
      const char *ev = EAS(d, eventTrigger, event);
      int which = !strcmp(ev, "end_oldsite_dream") ? 1 : !strcmp(ev, "end_oldsite_awake") ? 2 : 0;
      if (!which) return false;   /* other chapters' events */
      Ent *e = ent_new(&EVENTTRIG, d->x, d->y);
      if (e) ent_box(e, EA(d, eventTrigger, width), EA(d, eventTrigger, height), 0, 0), e->visible = 0, ST(e, Event)->which = (uint8_t)which;
      return true;
    }
    case TT_interactTrigger:
      if (strcmp(EAS(d, interactTrigger, event), "ch2_poem")) return false;
      ch2_interact_new(d, journal_start);
      return true;
    case TT_respawnTargetTrigger: {
      Ent *e = ent_new(&RTARGET, d->x, d->y);
      if (!e) return true;
      ent_box(e, EA(d, respawnTargetTrigger, width), EA(d, respawnTargetTrigger, height), 0, 0);
      e->visible = 0;
      e->tags = TAG_TRANSITION_UPDATE;
      ST(e, RespawnTarget)->target = ed_node(d, 0);
      return true;
    }
  }
  return false;
}

/* this chapter's tables: in the memory it is given while it is played (res_chapter_ram) */
uint32_t ch2_ram(void) { return g_session.area == 2 ? END_chase_flags : END_debris_px; }   /* the chasers: Old Site's */
