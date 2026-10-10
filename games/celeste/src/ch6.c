#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")   /* not drawn every frame: smaller over faster */
#endif
/* Reflection (chapter 6): pinball bumpers, feathers, star jump blocks and the
 * plateau, Kevins (crush blocks), the big waterfalls, Badeline's boost, the
 * heart statue, the tentacles, and Badeline the boss with her shots, beams,
 * falling and moving blocks (water is the Celestial Resort's, ch3.c).
 * Ported from the game's classes (Bumper.cs, FlyFeather.cs, CrushBlock.cs, FinalBoss.cs...). */
#include "badeline.h"

/* ---------------------------------------------------------------- helpers (Calc) */
static float angle_diff(float a, float b) {
  float d = b - a;
  while (d > PI_F) d -= 2 * PI_F;
  while (d <= -PI_F) d += 2 * PI_F;
  return d;
}
static float angle_lerp(float a, float b, float t) { return a + angle_diff(a, b) * t; }
static V2 closest_on_line(V2 a, V2 b, V2 p) {   /* Calc.ClosestPointOnLine */
  V2 v = v2sub(b, a), w = v2sub(p, a);
  float t = (w.x * v.x + w.y * v.y) / (v.x * v.x + v.y * v.y);
  t = clampf(t, 0, 1);
  return v2add(a, v2mul(v, t));
}
static uint16_t tex_named(const char *p) { return res_tex_by_name(p); }
static int tex_family(const char *prefix, uint16_t *out, int n) {
  char p[64];
  int k = 0;
  for (int i = 0; i < n; i++) {
    out[i] = tex_named(path2(p, prefix, NULL, i));
    if (out[i] != 0xFFFF) k = i + 1;
  }
  return k;
}
static void draw_centered(uint16_t tex, float x, float y, uint16_t tint, uint8_t alpha, float sx, float sy, float rot) {
  Tex t;
  if (tex == 0xFFFF || !tex_get(tex, &t)) return;
  float k = t.scale > 1 ? t.scale : 1;   /* stored smaller, drawn scaled up */
  gfx_tex_ex(tex, x, y, t.fw * k / 2.f, t.fh * k / 2.f, sx, sy, rot, tint, alpha, 0);
}
static float hash01(uint32_t seed, uint32_t i) {
  uint32_t h = seed * 0x9E3779B1u ^ (i + 0x7F4A7C15u) * 0x85EBCA6Bu;
  h ^= h >> 15, h *= 0x2C1B3C6Du, h ^= h >> 12, h *= 0x297A2D39u, h ^= h >> 15;
  return (h >> 8) / 16777216.f;
}
static uint8_t ent_ref(const Ent *e) { return (uint8_t)(e - g_ents); }
static Ent *ent_at(uint8_t i) { return &g_ents[i]; }
static Ent *first_of(const EntClass *c) {
  for (int i = 0; i < g_nents; i++)
    if (g_ents[i].cls == c && g_ents[i].dead != 1) return &g_ents[i];
  return NULL;
}
static void player_hurtbox(const Ent *pe, float *l, float *t, float *r, float *b) {
  float w = pe->cw, h = pe->ch, x = pe->cx, y = pe->cy;
  if (h == 11) h = 9;
  else if (h == 6) h = 4;
  else if (h == 8 && w == 8) w = 6, h = 6, x = -3, y = -9;
  *l = pe->x + x, *t = pe->y + y, *r = *l + w, *b = *t + h;
}
static inline void px_blend(uint16_t *strip, int y0, int y1, int vx, int vy, uint16_t c, int a) {
  if ((unsigned)vx >= VIEW_W || vy < y0 || vy >= y1 || a <= 0) return;
  uint16_t *d = strip + (vy - y0) * VIEW_W + vx;
  *d = a >= 256 ? c : blend565(*d, scale565(c, a), a);
}

/* ---------------------------------------------------------------- compact sprites */
typedef struct { uint8_t bank, anim, frame, playing; int8_t last; uint8_t pad[3]; float timer; } CSpr;
static Sprite cs_raw(const CSpr *c) {
  Sprite s;
  memset(&s, 0, sizeof s);
  s.bank = c->bank, s.anim = c->anim, s.frame = c->frame, s.playing = c->playing;
  s.timer = c->timer, s.rate = 1, s.last_goto = c->last, s.sx = s.sy = 1, s.tint = 0xFFFF, s.alpha = 255, s.visible = 1;
  return s;
}
static void cs_back(CSpr *c, const Sprite *s) {
  c->bank = s->bank, c->anim = s->anim, c->frame = s->frame, c->playing = s->playing, c->timer = s->timer, c->last = s->last_goto;
}
static void cs_init(CSpr *c, int bank) {
  Sprite s;
  spr_init(&s, bank);
  cs_back(c, &s);
}
static void cs_play(CSpr *c, int anim, bool restart) {
  Sprite s = cs_raw(c);
  spr_play(&s, anim, restart);
  cs_back(c, &s);
}
static int cs_update(CSpr *c) {
  if (!c->playing || c->anim == 255) return -1;
  Sprite s = cs_raw(c);
  int n = spr_frames(&s);
  float delay = n ? spr_anim_duration(c->bank, c->anim) / n : 0;
  int ended = (c->frame >= n - 1 && delay > 0 && c->timer + DT >= delay) ? c->anim : -1;
  spr_update(&s);
  cs_back(c, &s);
  return ended;
}
static uint16_t cs_tex(const CSpr *c) {
  Sprite s = cs_raw(c);
  uint16_t t = spr_tex(&s);
  if (t != 0xFFFF && !res_cached(t)) {   /* an animation not loaded yet: all its frames in one pass (as spr_draw) */
    uint16_t ids[32], gotos[8];
    int ng, a = c->anim != 255 ? c->anim : c->last;
    if (a >= 0) res_load(ids, bank_anim_textures(c->bank, a, ids, 32, gotos, &ng));
  }
  return t;
}
static void cs_origin(const CSpr *c, uint16_t t, float *ox, float *oy) {
  Sprite s;
  spr_init(&s, c->bank);
  Tex tx;
  *ox = s.ox, *oy = s.oy;
  if (s.justify && tex_get(t, &tx)) *ox = tx.fw * tx.scale * s.jx, *oy = tx.fh * tx.scale * s.jy;
}
static void cs_draw(const CSpr *c, float x, float y, float sx, float sy, float rot, uint16_t tint, uint8_t alpha) {
  uint16_t t = cs_tex(c);
  if (t == 0xFFFF) return;
  float ox, oy;
  cs_origin(c, t, &ox, &oy);
  gfx_tex_ex(t, x, y, ox, oy, sx, sy, rot, tint, alpha, 0);
}
/* frame-list animations for textures whose bank paths differ in case from the atlas */
typedef struct { const uint8_t *frames; uint8_t n, loop, next; float delay; } FAnim;
typedef struct { float t; uint8_t anim, frame, playing, pad; } FSpr;
static void fs_play(FSpr *s, int anim, bool restart) {
  if (s->anim == anim && !restart) return;
  s->anim = (uint8_t)anim, s->frame = 0, s->t = 0, s->playing = 1;
}
static void fs_update(FSpr *s, const FAnim *anims) {
  if (!s->playing) return;
  const FAnim *a = &anims[s->anim];
  s->t += DT;
  if (s->t < a->delay) return;
  s->t -= a->delay;
  if (++s->frame < a->n) return;
  if (a->loop) s->frame = 0;
  else if (a->next != 255) s->anim = a->next, s->frame = 0;
  else s->frame = (uint8_t)(a->n - 1), s->playing = 0;
}
static int fs_index(const FSpr *s, const FAnim *anims) { return anims[s->anim].frames[s->frame]; }
/* the frame's texture; its animation's frames are loaded in one pass when it is not */
static uint16_t fs_tex(const FSpr *s, const FAnim *anims, const uint16_t *tex) {
  const FAnim *a = &anims[s->anim];
  uint16_t t = tex[fs_index(s, anims)];
  if (t != 0xFFFF && !res_cached(t)) {
    uint16_t ids[32];
    int n = a->n < 32 ? a->n : 32;
    for (int i = 0; i < n; i++) ids[i] = tex[a->frames[i]];
    res_load(ids, n);
  }
  return t;
}

/* ---------------------------------------------------------------- Bumper (bigSpinner) */
/* Sprites.xml's objects/bumper/idle is objects/Bumper/Idle in the atlas: the frames are listed here */
enum { BA_ON, BA_IDLE, BA_HIT, BA_OFF };
static const uint8_t BI_ON[] = {42, 43, 44}, BI_HIT[] = {34, 35, 36, 37, 38, 39, 40, 41, 42}, BI_OFF[] = {42};
static const uint8_t BE_ON[] = {40, 41, 42}, BE_HIT[] = {32, 33, 34, 35, 36, 37, 38, 39, 40}, BE_OFF[] = {40};
static uint8_t BI_IDLE[34], BE_IDLE[32];
static const FAnim BUMPER_ANIMS[2][4] = {
    {{BI_ON, 3, 0, BA_IDLE, 0.06f}, {BI_IDLE, 34, 1, 255, 0.06f}, {BI_HIT, 9, 0, BA_OFF, 0.06f}, {BI_OFF, 1, 1, 255, 0.06f}},
    {{BE_ON, 3, 0, BA_IDLE, 0.06f}, {BE_IDLE, 32, 1, 255, 0.06f}, {BE_HIT, 9, 0, BA_OFF, 0.06f}, {BE_OFF, 1, 1, 255, 0.06f}}};
static uint16_t bumper_tex[2][45];
typedef struct {
  V2 anchor, start, end, hit_dir;
  float sine, tween, respawn;
  FSpr spr[2];
  Wiggler hit;
  uint8_t has_node, go_back, fire, mode;
} Bumper;
static void bumper_position(Ent *e) {   /* UpdatePosition: anchor + (sine * 3, sine/2 * 2) */
  Bumper *b = ST(e, Bumper);
  e->x = b->anchor.x + sinf(b->sine) * 3, e->y = b->anchor.y + sinf(b->sine / 2) * 2;
}
static void bumper_on_player(Ent *e, Player *p) {
  Bumper *b = ST(e, Bumper);
  /* Circle(12) against the hurtbox */
  float l, t, r, bt;
  player_hurtbox(p->ent, &l, &t, &r, &bt);
  float nx = clampf(e->x, l, r), ny = clampf(e->y, t, bt);
  if ((nx - e->x) * (nx - e->x) + (ny - e->y) * (ny - e->y) >= 144) return;
  if (b->fire) {
    V2 v = safe_norm(v2sub(player_center(p), v2(e->x, e->y)), 1);
    b->hit_dir = v2mul(v, -1);
    wiggler_restart(&b->hit);
    b->respawn = 0.6f;
    player_die(p, v, false);
    particles_emit(PL_MID, &P_Bumper_P_FireHit, 12, v2add(v2(e->x, e->y), v2mul(v, 12)), v2(3, 3), vangle(v));
  } else if (b->respawn <= 0) {
    b->respawn = 0.6f;
    V2 v = player_explode_launch(p, v2(e->x, e->y), false);
    fs_play(&b->spr[0], BA_HIT, true);
    fs_play(&b->spr[1], BA_HIT, true);
    level_dir_shake(v, 0.15f);
    particles_emit(PL_MID, &P_Bumper_P_Launch, 12, v2add(v2(e->x, e->y), v2mul(v, 12)), v2(3, 3), vangle(v));
  }
}
static void bumper_update(Ent *e) {
  Bumper *b = ST(e, Bumper);
  /* CoreModeListener: the mode is read each frame */
  if (b->mode != g_level.core_mode) b->mode = (uint8_t)g_level.core_mode, b->fire = g_level.core_mode == 1;
  b->sine = fmodf(b->sine + 2 * PI_F * 0.44f * DT + PI_F * 8, PI_F * 8);
  if (b->has_node) {   /* the looping tween, 1.8181819 s, CubeInOut */
    b->tween += DT / 1.8181819f;
    if (b->tween >= 1) b->tween -= 1, b->go_back = !b->go_back;
    float k = ease_cube_inout(b->tween);
    b->anchor = b->go_back ? v2lerp(b->end, b->start, k) : v2lerp(b->start, b->end, k);
  }
  fs_update(&b->spr[0], BUMPER_ANIMS[0]);
  fs_update(&b->spr[1], BUMPER_ANIMS[1]);
  wiggler_update(&b->hit);
  if (b->respawn > 0) {
    b->respawn -= DT;
    if (b->respawn <= 0) fs_play(&b->spr[0], BA_ON, false), fs_play(&b->spr[1], BA_ON, false);
  } else if (level_on_interval(0.05f)) {
    float a = rndf() * 2 * PI_F;
    float dir = b->fire ? -PI_F / 2 : a, len = b->fire ? 12.f : 8.f;
    particles_emit(PL_MID, b->fire ? &P_Bumper_P_FireAmbience : &P_Bumper_P_Ambience, 1, v2add(v2(e->x, e->y), angle_vec(a, len)),
                   v2(2, 2), dir);
  }
  bumper_position(e);
}
static void bumper_render(Ent *e) {
  Bumper *b = ST(e, Bumper);
  int k = b->fire ? 1 : 0;
  float ox = k ? b->hit_dir.x * b->hit.value * 8 : 0, oy = k ? b->hit_dir.y * b->hit.value * 8 : 0;
  draw_centered(fs_tex(&b->spr[k], BUMPER_ANIMS[k], bumper_tex[k]), e->x + ox, e->y + oy, 0xFFFF, 255, 1, 1, 0);
  if (b->respawn <= 0) {
    light_add(e->x, e->y, 0x008080, 1, 16, 32);   /* Color.Teal */
    bloom_add(e->x, e->y, 0.5f, 16);
  }
}
static const EntClass BUMPER = {.name = "bigSpinner", .size = sizeof(Bumper), .update = bumper_update, .render = bumper_render,
                                .on_player = bumper_on_player, .kind = KIND_PCOLLIDE};
static void new_bumper(const EData *d) {
  Ent *e = ent_new(&BUMPER, d->x, d->y);
  if (!e) return;
  ent_box(e, 24, 24, -12, -12);   /* Circle(12): the hit test is the circle */
  Bumper *b = ST(e, Bumper);
  b->anchor = b->start = v2(d->x, d->y);
  if (d->nnodes) b->has_node = 1, b->end = ed_node(d, 0);
  b->sine = fmodf(rndf() * PI_F * 2 * 2 + PI_F * 8, PI_F * 8);   /* SineWave.Randomize */
  fs_play(&b->spr[0], BA_IDLE, true);
  fs_play(&b->spr[1], BA_IDLE, true);
  wiggler_init(&b->hit, 1.2f, 2);
  b->mode = 255;
  bumper_position(e);
  if (!bumper_tex[0][0]) {
    for (int i = 0; i < 34; i++) BI_IDLE[i] = (uint8_t)i;
    for (int i = 0; i < 32; i++) BE_IDLE[i] = (uint8_t)i;
    tex_family("objects/Bumper/Idle", bumper_tex[0], 45);
    tex_family("objects/Bumper/Evil", bumper_tex[1], 43);
  }
}

/* ---------------------------------------------------------------- FlyFeather (infiniteStar) */
typedef struct {
  CSpr spr;
  Wiggler wig, shield_wig, move_wig;
  V2 move_dir;
  float sine, respawn, light, co_wait, co_dir;
  uint8_t shielded, single, visible, outline, co, pad[3];
} Feather;
static void feather_on_player(Ent *e, Player *p) {
  Feather *f = ST(e, Feather);
  V2 speed = p->speed;
  if (f->shielded && !player_dash_attacking(p)) {
    player_point_bounce(p, e_center(e));
    wiggler_restart(&f->move_wig);
    wiggler_restart(&f->shield_wig);
    f->move_dir = safe_norm(v2sub(e_center(e), player_center(p)), 1);
    if (f->move_dir.x == 0 && f->move_dir.y == 0) f->move_dir = v2(0, 1);
    return;
  }
  if (player_start_star_fly(p)) {
    e->collidable = 0;
    level_shake(0.3f);   /* CollectRoutine */
    f->visible = 0;
    f->co = 1, f->co_wait = 0.05f;
    f->co_dir = (speed.x != 0 || speed.y != 0) ? vangle(speed) : vangle(v2sub(v2(e->x, e->y), player_center(p)));
    if (!f->single) f->outline = 1, f->respawn = 3;
  }
}
static void feather_update(Ent *e) {
  Feather *f = ST(e, Feather);
  cs_update(&f->spr);
  wiggler_update(&f->wig);
  wiggler_update(&f->shield_wig);
  wiggler_update(&f->move_wig);
  f->sine = fmodf(f->sine + 2 * PI_F * 0.6f * DT + PI_F * 8, PI_F * 8);
  if (f->co) {
    if (f->co_wait > 0 && (f->co_wait -= DT) > 0) {
    } else {
      particles_emit(PL_FG, &P_FlyFeather_P_Collect, 10, v2(e->x, e->y), v2(6, 6), 0);   /* SlashFx.Burst: not drawn */
      f->co = 0;
    }
  }
  if (f->respawn > 0) {
    f->respawn -= DT;
    if (f->respawn <= 0 && !e->collidable) {   /* Respawn */
      f->outline = 0;
      e->collidable = 1;
      f->visible = 1;
      wiggler_restart(&f->wig);
      particles_emit(PL_FG, &P_FlyFeather_P_Respawn, 16, v2(e->x, e->y), v2(2, 2), 0);
    }
  }
  f->light = approach(f->light, f->visible ? 1 : 0, 4 * DT);
}
static void feather_render(Ent *e) {
  Feather *f = ST(e, Feather);
  float sx = -f->move_dir.x * f->move_wig.value * 8, sy = sinf(f->sine) * 2 - f->move_dir.y * f->move_wig.value * 8;
  if (f->outline) draw_centered(T_objects_flyFeather_outline, e->x, e->y, 0xFFFF, 255, 1, 1, 0);
  if (f->visible) {
    float s = 1 + f->wig.value * 0.2f;
    cs_draw(&f->spr, e->x + sx, e->y + sy, s, s, 0, 0xFFFF, 255);
    if (f->shielded) gfx_circle(e->x + sx, e->y + sy, 10 - f->shield_wig.value * 2, 0xFFFF, 255, 12);
  }
  light_add(e->x, e->y, 0xFFFFFF, f->light, 16, 48);
  bloom_add(e->x, e->y + sinf(f->sine) * 2, f->light * 0.8f, 20);
}
static const EntClass FEATHER = {.name = "infiniteStar", .size = sizeof(Feather), .update = feather_update, .render = feather_render,
                                 .on_player = feather_on_player, .kind = KIND_PCOLLIDE};
static void feather_new(float x, float y, bool shielded, bool single) {
  Ent *e = ent_new(&FEATHER, x, y);
  if (!e) return;
  ent_box(e, 20, 20, -10, -10);
  Feather *f = ST(e, Feather);
  f->shielded = shielded;
  f->single = single;
  f->visible = 1;
  f->light = 1;
  cs_init(&f->spr, SB_flyFeather);
  wiggler_init(&f->wig, 1, 4);
  wiggler_init(&f->shield_wig, 0.5f, 4);
  wiggler_init(&f->move_wig, 0.8f, 2);
  f->move_wig.start_zero = true;
  f->sine = fmodf(rndf() * PI_F * 2 * 2 + PI_F * 8, PI_F * 8);
}
static void new_feather(const EData *d) { feather_new(d->x, d->y, EAB(d, infiniteStar, shielded), EAB(d, infiniteStar, singleUse)); }

/* ---------------------------------------------------------------- StarJumpBlock */
/* its edges, railings and corners come from where other star jump blocks are: worked out at Awake */
typedef struct {
  float start_y, ylerp, sink;
  uint16_t seed;
  uint8_t sinks, w8, h8, corners, pad[2];
  uint8_t open[16];   /* top cells, bottom cells, left cells, right cells: one bit each */
} StarJump;
static uint16_t sj_left[7], sj_rail[7], sj_right[7], sj_edgeh[4], sj_edgev[4], sj_corner[4];
static const EntClass STARJUMP;
static bool sj_open(Ent *e, float x, float y) {   /* Open: no star jump block at that cell's middle */
  float px = e->x + x + 4, py = e->y + y + 4;
  for (int i = 0; i < g_nents; i++) {
    Ent *o = &g_ents[i];
    if (o->cls == &STARJUMP && o->dead != 1 && collide_rect(o, px, py, px + 1, py + 1)) return false;
  }
  return true;
}
static int sj_mod(int x, int m) { return (x % m + m) % m; }
static void sj_bit_set(StarJump *s, int i) {
  if (i < 128) s->open[i >> 3] |= (uint8_t)(1 << (i & 7));
}
static bool sj_bit(const StarJump *s, int i) { return i < 128 && (s->open[i >> 3] >> (i & 7) & 1); }
static void sj_awake(Ent *e) {
  StarJump *s = ST(e, StarJump);
  int w = (int)e->cw, h = (int)e->ch, k = 0;
  for (int i = 8; i < w - 8; i += 8, k++) {
    if (sj_open(e, (float)i, -8)) sj_bit_set(s, 2 * k);
    if (sj_open(e, (float)i, (float)h)) sj_bit_set(s, 2 * k + 1);
  }
  int base = 2 * k;
  k = 0;
  for (int j = 8; j < h - 8; j += 8, k++) {
    if (sj_open(e, -8, (float)j)) sj_bit_set(s, base + 2 * k);
    if (sj_open(e, (float)w, (float)j)) sj_bit_set(s, base + 2 * k + 1);
  }
  /* the corners: what each one's two neighbours are */
  uint8_t c = 0;
  c |= sj_open(e, -8, 0) << 0 | sj_open(e, 0, -8) << 1;
  c |= sj_open(e, (float)w, 0) << 2 | sj_open(e, w - 8.f, -8) << 3;
  c |= sj_open(e, -8, h - 8.f) << 4 | sj_open(e, 0, (float)h) << 5;
  c |= sj_open(e, (float)w, h - 8.f) << 6 | sj_open(e, w - 8.f, (float)h) << 7;
  s->corners = c;
}
static void sj_update(Ent *e) {
  StarJump *s = ST(e, StarJump);
  plat_update(e);
  if (!s->sinks) return;
  if (solid_has_player_rider(e)) s->sink = 0.1f;
  else if (s->sink > 0) s->sink -= DT;
  s->ylerp = approach(s->ylerp, s->sink > 0 ? 1 : 0, DT);
  plat_move_to_y(e, lerpf(s->start_y, s->start_y + 12, ease_sine_inout(s->ylerp)));
}
/* Calc.Random.Choose: a hash of the place */
static uint16_t sj_pick(StarJump *s, const uint16_t *t, int n, uint32_t at) { return t[(int)(hash01(s->seed, at) * n)]; }
static void sj_img(uint16_t t, float x, float y, float sx, float sy) {   /* an Image with CenterOrigin */
  draw_centered(t, x, y, 0xFFFF, 255, sx, sy, 0);
}
static void sj_render(Ent *e) {
  StarJump *s = ST(e, StarJump);
  float x = e->x, y = e->y;
  int w = (int)e->cw, h = (int)e->ch, k = 0;
  if (x + w < g_camx - 8 || x > g_camx + 328 || y + h < g_camy - 8 || y > g_camy + 188) return;
  for (int i = 8; i < w - 8; i += 8, k++) {
    if (sj_bit(s, 2 * k)) {
      sj_img(sj_pick(s, sj_edgeh, 4, (uint32_t)k * 4), x + i + 4, y + 4, 1, 1);
      gfx_tex(sj_rail[sj_mod((int)(x + i) / 8, 7)], x + i, y - 8, 0, 0xFFFF, 255);
    }
    if (sj_bit(s, 2 * k + 1)) sj_img(sj_pick(s, sj_edgeh, 4, (uint32_t)k * 4 + 1), x + i + 4, y + h - 4, 1, -1);
  }
  int base = 2 * k;
  k = 0;
  for (int j = 8; j < h - 8; j += 8, k++) {
    if (sj_bit(s, base + 2 * k)) sj_img(sj_pick(s, sj_edgev, 4, (uint32_t)k * 4 + 2), x + 4, y + j + 4, -1, 1);
    if (sj_bit(s, base + 2 * k + 1)) sj_img(sj_pick(s, sj_edgev, 4, (uint32_t)k * 4 + 3), x + w - 4, y + j + 4, 1, 1);
  }
  uint8_t c = s->corners;
  /* top left */
  if ((c & 1) && (c & 2)) {
    sj_img(sj_pick(s, sj_corner, 4, 1000), x + 4, y + 4, -1, 1);
    gfx_tex(sj_left[sj_mod((int)x / 8, 7)], x, y - 8, 0, 0xFFFF, 255);
  } else if (c & 1)
    sj_img(sj_pick(s, sj_edgev, 4, 1000), x + 4, y + 4, -1, 1);
  else if (c & 2) {
    sj_img(sj_pick(s, sj_edgeh, 4, 1000), x + 4, y + 4, 1, 1);
    gfx_tex(sj_rail[sj_mod((int)x / 8, 7)], x, y - 8, 0, 0xFFFF, 255);
  }
  /* top right */
  if ((c & 4) && (c & 8)) {
    sj_img(sj_pick(s, sj_corner, 4, 1001), x + w - 4, y + 4, 1, 1);
    gfx_tex(sj_right[sj_mod((int)(x + w) / 8 - 1, 7)], x + w - 8, y - 8, 0, 0xFFFF, 255);
  } else if (c & 4)
    sj_img(sj_pick(s, sj_edgev, 4, 1001), x + w - 4, y + 4, 1, 1);
  else if (c & 8) {
    sj_img(sj_pick(s, sj_edgeh, 4, 1001), x + w - 4, y + 4, 1, 1);
    gfx_tex(sj_rail[sj_mod((int)(x + w) / 8 - 1, 7)], x + w - 8, y - 8, 0, 0xFFFF, 255);
  }
  /* bottom left */
  if ((c & 16) && (c & 32)) sj_img(sj_pick(s, sj_corner, 4, 1002), x + 4, y + h - 4, -1, -1);
  else if (c & 16)
    sj_img(sj_pick(s, sj_edgev, 4, 1002), x + 4, y + h - 4, -1, -1);
  else if (c & 32)
    sj_img(sj_pick(s, sj_edgeh, 4, 1002), x + 4, y + h - 4, 1, -1);
  /* bottom right */
  if ((c & 64) && (c & 128)) sj_img(sj_pick(s, sj_corner, 4, 1003), x + w - 4, y + h - 4, 1, -1);
  else if (c & 64)
    sj_img(sj_pick(s, sj_edgev, 4, 1003), x + w - 4, y + h - 4, 1, -1);
  else if (c & 128)
    sj_img(sj_pick(s, sj_edgeh, 4, 1003), x + w - 4, y + h - 4, 1, -1);
}
static const EntClass STARJUMP = {.name = "starJumpBlock", .size = sizeof(StarJump), .update = sj_update, .render = sj_render,
                                  .awake = sj_awake, .kind = KIND_SOLID};
static void new_starjump(const EData *d) {
  Ent *e = ent_new(&STARJUMP, d->x, d->y);
  if (!e) return;
  ent_box(e, EA(d, starJumpBlock, width), EA(d, starJumpBlock, height), 0, 0);
  e->depth = -10000;
  StarJump *s = ST(e, StarJump);
  s->sinks = EAB(d, starJumpBlock, sinks);
  s->start_y = d->y;
  s->seed = (uint16_t)rndi(65536);
  if (!sj_rail[0]) {
    tex_family("objects/starjumpBlock/leftrailing", sj_left, 7);
    tex_family("objects/starjumpBlock/railing", sj_rail, 7);
    tex_family("objects/starjumpBlock/rightrailing", sj_right, 7);
    tex_family("objects/starjumpBlock/edgeH", sj_edgeh, 4);
    tex_family("objects/starjumpBlock/edgeV", sj_edgev, 4);
    tex_family("objects/starjumpBlock/corner", sj_corner, 4);
  }
}

/* ---------------------------------------------------------------- Plateau */
static void plateau_render(Ent *e) { gfx_tex(T_scenery_fallplateau, e->x, e->y, 0, 0xFFFF, 255); }
static const EntClass PLATEAU = {.name = "plateau", .update = plat_update, .render = plateau_render, .kind = KIND_SOLID};
static void new_plateau(const EData *d) {
  Ent *e = ent_new(&PLATEAU, d->x, d->y);
  if (!e) return;
  ent_box(e, 104, 4, 8, 0);   /* Collider.Left += 8 */
  e->safe = 1;
}

/* ---------------------------------------------------------------- CrushBlock (Kevin) */
#define KEVIN_STACK 8
typedef struct {
  V2 crush_dir;
  float speed, co_wait, alarm, wait_sfx;
  CSpr face;
  int16_t ret[KEVIN_STACK][2];   /* returnStack: From */
  int8_t retdir[KEVIN_STACK][2];  /* and Direction */
  uint16_t seed;
  uint8_t nret, can_activate, can_h, can_v, chill, giant, axes, co, slowing, next_face, lit, pad;
} Kevin;
static uint16_t kevin_block[4], kevin_lit[4];   /* lit: left, right, top, bottom */
static void kevin_particles(Ent *e, V2 dir) {   /* ActivateParticles */
  float a;
  V2 at, range;
  int n;
  if (dir.x > 0) a = 0, at = v2(e_right(e) - 1, e_cym(e)), range = v2(0, (e->ch - 2) * 0.5f), n = (int)(e->ch / 8) * 4;
  else if (dir.x < 0) a = PI_F, at = v2(e_left(e) + 1, e_cym(e)), range = v2(0, (e->ch - 2) * 0.5f), n = (int)(e->ch / 8) * 4;
  else if (dir.y > 0) a = PI_F / 2, at = v2(e_cxm(e), e_bottom(e) - 1), range = v2((e->cw - 2) * 0.5f, 0), n = (int)(e->cw / 8) * 4;
  else a = -PI_F / 2, at = v2(e_cxm(e), e_top(e) + 1), range = v2((e->cw - 2) * 0.5f, 0), n = (int)(e->cw / 8) * 4;
  particles_emit(PL_MID, &P_CrushBlock_P_Activate, n + 2, at, range, a);
}
static bool kevin_can(Kevin *k, V2 dir) {   /* CanActivate */
  if (k->giant && dir.x <= 0) return false;
  if (k->can_activate && !v2eq(k->crush_dir, dir)) {
    if (dir.x != 0 && !k->can_h) return false;
    if (dir.y != 0 && !k->can_v) return false;
    return true;
  }
  return false;
}
static void kevin_attack(Ent *e, V2 dir) {
  Kevin *k = ST(e, Kevin);
  cs_play(&k->face, k->giant ? A_giant_crushblock_face_hit : A_crushblock_face_hit, false);
  k->crush_dir = dir;
  k->can_activate = 0;
  k->co = 1, k->co_wait = 0, k->slowing = 0, k->alarm = 0;   /* attackCoroutine.Replace(AttackSequence()) */
  e->remx = e->remy = 0;
  kevin_particles(e, dir);
  k->lit = dir.x < 0 ? 1 : dir.x > 0 ? 2 : dir.y < 0 ? 3 : 4;
  k->next_face = dir.x < 0 ? 0 : dir.x > 0 ? 1 : dir.y < 0 ? 2 : 3;
  bool push = true;
  if (k->nret) {
    int8_t *t = k->retdir[k->nret - 1];
    if ((t[0] == dir.x && t[1] == dir.y) || (t[0] == -dir.x && t[1] == -dir.y)) push = false;
  }
  if (push && k->nret < KEVIN_STACK) {
    k->ret[k->nret][0] = (int16_t)e->x, k->ret[k->nret][1] = (int16_t)e->y;
    k->retdir[k->nret][0] = (int8_t)dir.x, k->retdir[k->nret][1] = (int8_t)dir.y;
    k->nret++;
  }
}
static int kevin_dashed(Ent *e, Player *p, V2 dir) {
  (void)p;
  if (kevin_can(ST(e, Kevin), v2mul(dir, -1))) {
    kevin_attack(e, v2mul(dir, -1));
    return DASH_REBOUND;
  }
  return DASH_NORMAL;
}
/* MoveHCollideSolidsAndBounds / MoveVCollideSolidsAndBounds */
static bool kevin_move_bounds(Ent *e, float amount, bool vertical, bool check_bottom) {
  Room *rm = g_level.room;
  if (vertical) e->lift.y = amount / DT;
  else e->lift.x = amount / DT;
  float *rem = vertical ? &e->remy : &e->remx;
  *rem += amount;
  int n = roundi(*rem);
  if (!n) return false;
  *rem -= n;
  bool hit;
  if (!vertical) {
    if (e_left(e) + n < rm->x) hit = true, n = rm->x - (int)e_left(e);
    else if (e_right(e) + n > rm->x + rm->w) hit = true, n = rm->x + rm->w - (int)e_right(e);
    else hit = false;
  } else {
    int bottom = rm->y + rm->h + 32;
    if (e_top(e) + n < rm->y) hit = true, n = rm->y - (int)e_top(e);
    else if (check_bottom && e_bottom(e) + n > bottom) hit = true, n = bottom - (int)e_bottom(e);
    else hit = false;
  }
  /* MoveExactCollideSolids through the engine: a whole move, with no remainder */
  float keep = *rem, lift = vertical ? e->lift.y : e->lift.x;
  *rem = 0;
  bool solid = vertical ? plat_move_v_collide_solids(e, (float)n, true) : plat_move_h_collide_solids(e, (float)n, true);
  *rem = keep;
  if (vertical) e->lift.y = lift;
  else e->lift.x = lift;
  return solid || hit;
}
static bool kevin_move_check(Ent *e, float amount, bool vertical) {   /* MoveHCheck / MoveVCheck */
  Room *rm = g_level.room;
  if (!kevin_move_bounds(e, amount, vertical, !vertical)) return false;
  if (!vertical) {
    if (amount < 0 && e_left(e) <= rm->x) return true;
    if (amount > 0 && e_right(e) >= rm->x + rm->w) return true;
  } else if (amount < 0 && e_top(e) <= rm->y)
    return true;
  for (int i = 1; i <= 4; i++)
    for (int s = 1; s >= -1; s -= 2) {
      float dx = vertical ? (float)(i * s) : (float)sgn(amount), dy = vertical ? (float)sgn(amount) : (float)(i * s);
      if (!collide_solid(e, e->x + dx, e->y + dy)) {
        if (vertical) plat_move_h_exact(e, i * s), plat_move_v_exact(e, sgn(amount));
        else plat_move_v_exact(e, i * s), plat_move_h_exact(e, sgn(amount));
        return false;
      }
    }
  return true;
}
static bool point_water(V2 p);
static void kevin_impact(Ent *e) {   /* the impact particles where it hit solids */
  Kevin *k = ST(e, Kevin);
  V2 d = k->crush_dir;
  if (d.x != 0) {
    float x = d.x < 0 ? e_left(e) - 1 : e_right(e) + 1;
    for (int i = 0; i < e->ch / 8; i++) {
      V2 p = v2(x, e_top(e) + 4 + i * 8);
      if (!point_water(p) && point_solid(p.x, p.y)) {
        particles_emit1(PL_FG, &P_CrushBlock_P_Impact, v2add(p, v2(0, 2)), d.x < 0 ? 0 : PI_F);
        particles_emit1(PL_FG, &P_CrushBlock_P_Impact, v2sub(p, v2(0, 2)), d.x < 0 ? 0 : PI_F);
      }
    }
  } else {
    float y = d.y < 0 ? e_top(e) - 1 : e_bottom(e) + 1;
    for (int i = 0; i < e->cw / 8; i++) {
      V2 p = v2(e_left(e) + 4 + i * 8, y);
      if (!point_water(p) && point_solid(p.x, p.y)) {
        particles_emit1(PL_FG, &P_CrushBlock_P_Impact, v2add(p, v2(2, 0)), d.y < 0 ? PI_F / 2 : -PI_F / 2);
        particles_emit1(PL_FG, &P_CrushBlock_P_Impact, v2sub(p, v2(2, 0)), d.y < 0 ? PI_F / 2 : -PI_F / 2);
      }
    }
  }
}
static void kevin_update(Ent *e) {
  Kevin *k = ST(e, Kevin);
  plat_update(e);
  int ended = cs_update(&k->face);
  if (ended == (k->giant ? A_giant_crushblock_face_hit : A_crushblock_face_hit)) {   /* OnLastFrame("hit") */
    static const int FACE[4] = {A_crushblock_face_left, A_crushblock_face_right, A_crushblock_face_up, A_crushblock_face_down};
    if (k->giant) cs_play(&k->face, A_giant_crushblock_face_right, false);
    else cs_play(&k->face, FACE[k->next_face], false);
  }
  if (k->alarm > 0 && (k->alarm -= DT) <= 0) {   /* chillOut's slowing alarm */
    cs_play(&k->face, k->giant ? A_giant_crushblock_face_hurt : A_crushblock_face_hurt, false);
    k->lit = 0;
  }
  /* AttackSequence */
  if (k->co) {
    if (k->co_wait > 0) k->co_wait -= DT;
    else
      switch (k->co) {
        case 1:
          plat_start_shaking(e, 0.4f);
          k->co_wait = 0.4f;
          k->co = 2;
          break;
        case 2:
          if (!k->chill) k->can_activate = 1;
          k->speed = 0;
          k->co = 3;
          /* fall through */
        case 3: {
          if (!k->chill) k->speed = approach(k->speed, 240, 500 * DT);
          else if (k->slowing || tiles_solid_rect(e_left(e) + k->crush_dir.x * 256, e_top(e) + k->crush_dir.y * 256,
                                                   e_right(e) + k->crush_dir.x * 256, e_bottom(e) + k->crush_dir.y * 256)) {
            k->speed = approach(k->speed, 24, 500 * DT * 0.25f);
            if (!k->slowing) k->slowing = 1, k->alarm = 0.5f;
          } else
            k->speed = approach(k->speed, 240, 500 * DT);
          bool hit = k->crush_dir.x == 0 ? kevin_move_check(e, k->speed * k->crush_dir.y * DT, true)
                                         : kevin_move_check(e, k->speed * k->crush_dir.x * DT, false);
          k = ST(e, Kevin);
          if (e_top(e) >= g_level.room->y + g_level.room->h + 32) {
            ent_remove(e);
            return;
          }
          if (!hit) {
            if (level_on_interval(0.02f)) {
              V2 at;
              float a;
              if (k->crush_dir.x > 0) at = v2(e_left(e) + 1, rnd_rangef(e_top(e) + 3, e_bottom(e) - 3)), a = PI_F;
              else if (k->crush_dir.x < 0) at = v2(e_right(e) - 1, rnd_rangef(e_top(e) + 3, e_bottom(e) - 3)), a = 0;
              else if (k->crush_dir.y > 0) at = v2(rnd_rangef(e_left(e) + 3, e_right(e) - 3), e_top(e) + 1), a = -PI_F / 2;
              else at = v2(rnd_rangef(e_left(e) + 3, e_right(e) - 3), e_bottom(e) - 1), a = PI_F / 2;
              particles_emit1(PL_MID, &P_CrushBlock_P_Crushing, at, a);
            }
            break;
          }
          /* CollideFirst<FallingBlock>(Position + crushDir).Triggered = true */
          for (int i = 0; i < g_nents; i++) {
            Ent *o = &g_ents[i];
            if (o->cls && o->dead != 1 && !strcmp(o->cls->name, "fallingBlock") && collide_ent_at(e, e->x + k->crush_dir.x, e->y + k->crush_dir.y, o) &&
                MORE(o->cls)->on_staticmover_trigger)
              MORE(o->cls)->on_staticmover_trigger(o, NULL);
          }
          kevin_impact(e);
          level_dir_shake(k->crush_dir, 0.3f);
          plat_start_shaking(e, 0.4f);
          k->crush_dir = v2(0, 0);
          k->lit = 0;
          if (k->chill) {
            k->co = 0;
            break;
          }
          cs_play(&k->face, k->giant ? A_giant_crushblock_face_hurt : A_crushblock_face_hurt, false);
          k->co_wait = 0.4f;
          k->co = 4;
          k->speed = 0;
          k->wait_sfx = 0;
          break;
        }
        case 4:   /* the return: a frame first (yield null), then towards the last place */
          k->co = 5;
          break;
        case 5: {
          if (!k->nret) {
            k->co = 0;
            break;
          }
          k->speed = approach(k->speed, 60, 160 * DT);
          k->wait_sfx -= DT;
          int16_t *from = k->ret[k->nret - 1];
          int8_t *dir = k->retdir[k->nret - 1];
          if (dir[0] != 0) plat_move_towards_x(e, from[0], k->speed * DT);
          if (dir[1] != 0) plat_move_towards_y(e, from[1], k->speed * DT);
          k = ST(e, Kevin);
          V2 ex = plat_exact(e);
          if ((dir[0] != 0 && ex.x != from[0]) || (dir[1] != 0 && ex.y != from[1])) {
            k->co = 4;
            break;
          }
          k->speed = 0;
          k->nret--;
          if (!k->nret) cs_play(&k->face, k->giant ? A_giant_crushblock_face_idle : A_crushblock_face_idle, false);
          k->wait_sfx = 0.1f;
          plat_start_shaking(e, 0.2f);
          k->co_wait = 0.2f;
          k->co = 4;
          break;
        }
      }
  }
}
static void kevin_render(Ent *e) {
  Kevin *k = ST(e, Kevin);
  float x = e->x + e->shakex, y = e->y + e->shakey;
  int w = (int)e->cw, h = (int)e->ch, nx = w / 8 - 1, ny = h / 8 - 1;
  gfx_rect(x + 2, y + 2, w - 4, h - 4, rgb(0x62222B), 255);
  uint16_t idle = kevin_block[k->axes];
  /* AddImage: the border tiles (a black copy outside each), with the lit edges over them */
  for (int pass = 0; pass < 2; pass++)
    for (int i = 0; i <= nx; i++)
      for (int j = 0; j <= ny; j++) {
        if (i != 0 && i != nx && j != 0 && j != ny) continue;
        int bx = i == 0 ? -1 : i == nx ? 1 : 0, by = j == 0 ? -1 : j == ny ? 1 : 0;
        int tx = i == 0 ? 0 : i == nx ? 3 : 1 + (int)(hash01(k->seed, (uint32_t)(i * 64 + j)) * 2);
        int ty = j == 0 ? 0 : j == ny ? 3 : 1 + (int)(hash01(k->seed, (uint32_t)(i * 64 + j) + 4096) * 2);
        if (bx != 0 && by != 0) tx = i == 0 ? 0 : 3, ty = j == 0 ? 0 : 3;
        float px = x + i * 8, py = y + j * 8;
        if (!pass) {
          if (bx) gfx_tex_part(idle, px + bx, py, tx * 8, ty * 8, 8, 8, GF_SILHOUETTE, 0, 255);
          if (by) gfx_tex_part(idle, px, py + by, tx * 8, ty * 8, 8, 8, GF_SILHOUETTE, 0, 255);
          continue;
        }
        gfx_tex_part(idle, px, py, tx * 8, ty * 8, 8, 8, 0, 0xFFFF, 255);
        if (k->lit == 1 && bx < 0) gfx_tex_part(kevin_lit[0], px, py, 0, ty * 8, 8, 8, 0, 0xFFFF, 255);
        if (k->lit == 2 && bx > 0) gfx_tex_part(kevin_lit[1], px, py, 0, ty * 8, 8, 8, 0, 0xFFFF, 255);
        if (k->lit == 3 && by < 0) gfx_tex_part(kevin_lit[2], px, py, tx * 8, 0, 8, 8, 0, 0xFFFF, 255);
        if (k->lit == 4 && by > 0) gfx_tex_part(kevin_lit[3], px, py, tx * 8, 0, 8, 8, 0, 0xFFFF, 255);
      }
  /* the face, nudged towards a player pressed against it */
  float fx = w / 2.f, fy = h / 2.f;
  if (k->crush_dir.x == 0 && k->crush_dir.y == 0) {
    Ent *pe = level_player_ent();
    if (pe && collide_ent_at(e, e->x - 1, e->y, pe)) fx -= 1;
    else if (pe && collide_ent_at(e, e->x + 1, e->y, pe)) fx += 1;
    else if (pe && collide_ent_at(e, e->x, e->y - 1, pe)) fy -= 1;
  }
  cs_draw(&k->face, x + fx, y + fy, 1, 1, 0, 0xFFFF, 255);
}
static const EntClass KEVIN = {.name = "crushBlock", .size = sizeof(Kevin), .update = kevin_update, .render = kevin_render,
                               .kind = KIND_SOLID, .more = &(const EntMore){.on_dash_collide = kevin_dashed}};
static void new_kevin(const EData *d) {
  float w = EA(d, crushBlock, width), h = EA(d, crushBlock, height);
  Ent *e = ent_new(&KEVIN, d->x, d->y);
  if (!e) return;
  ent_box(e, w, h, 0, 0);
  Kevin *k = ST(e, Kevin);
  k->chill = EAB(d, crushBlock, chillout);
  k->giant = w >= 48 && h >= 48 && k->chill;
  k->can_activate = 1;
  const char *ax = EAS(d, crushBlock, axes);
  if (!strcmp(ax, "Horizontal")) k->axes = 1, k->can_h = 1;
  else if (!strcmp(ax, "Vertical")) k->axes = 2, k->can_v = 1;
  else k->axes = 3, k->can_h = k->can_v = 1;
  k->seed = (uint16_t)rndi(65536);
  cs_init(&k->face, k->giant ? SB_giant_crushblock_face : SB_crushblock_face);
  if (!kevin_block[0]) {
    tex_family("objects/crushblock/block", kevin_block, 4);
    kevin_lit[0] = tex_named("objects/crushblock/lit_left");
    kevin_lit[1] = tex_named("objects/crushblock/lit_right");
    kevin_lit[2] = tex_named("objects/crushblock/lit_top");
    kevin_lit[3] = tex_named("objects/crushblock/lit_bottom");
  }
}
/* ---------------------------------------------------------------- Water (ch3.c's): what the blocks ask of it */
static bool point_water(V2 p) {   /* Scene.CollideCheck<Water>(point) */
  for (int i = 0; i < g_nents; i++) {
    Ent *o = &g_ents[i];
    if ((o->kind & KIND_WATER) && o->cls && o->dead != 1 && o->collidable && collide_rect(o, p.x, p.y, p.x + 1, p.y + 1)) return true;
  }
  return false;
}

/* ---------------------------------------------------------------- BigWaterfall */
typedef struct { float sine, fade; uint8_t fg, nlines, pad[2]; float lines[8]; } Waterfall;
static float wf_render_x(Ent *e) {   /* RenderPositionAtCamera(camera + (160, 90)).X */
  Waterfall *w = ST(e, Waterfall);
  float val = e->x + e->cw / 2 - (g_level.cam.x + 160);
  return e->x + (w->fg ? val * 0.2f : -val * 0.6f);
}
static void wf_update(Ent *e) {
  Waterfall *w = ST(e, Waterfall);
  w->sine += DT;
  /* TransitionListener: OnIn fade = f, OnOut fade = 1 - f */
  if (g_level.transitioning) w->fade = e->room == g_level.room_slot ? g_level.tr_at : 1 - g_level.tr_at;
}
/* the BG layer: every 3 rows its edges and lines are pushed sideways by a sine */
static void wf_strip(uint16_t *strip, int y0, int y1, void *ctx) {
  Ent *e = ctx;
  Waterfall *w = ST(e, Waterfall);
  uint16_t surf = rgb(0x89DBF0), fill = rgb(0x29A7EA);
  int sa = (int)(128 * w->fade), fa = (int)(77 * w->fade);
  int x = (int)wf_render_x(e) - g_camx, W = (int)e->cw;
  float start = fmaxf(e->y, floorf(g_level.cam.y / 3) * 3);
  for (int vy = y0; vy < y1; vy++) {
    float wy = (float)(vy + g_camy);
    if (wy < start || wy >= e->y + e->ch) continue;
    float row = start + floorf((wy - start) / 3) * 3;
    int n = (int)(sinf(row / 6 - w->sine * 8) * 2);
    uint16_t *line = strip + (vy - y0) * VIEW_W;
    if (fa > 0)
      for (int i = 0; i < W; i++) {   /* Draw.Rect(x, y, width, height, fillColor) */
        int vx = x + i;
        if ((unsigned)vx < VIEW_W) line[vx] = blend565(line[vx], scale565(fill, fa), fa);
      }
    for (int i = 0; i < 4 + n; i++) px_blend(strip, y0, y1, x + i, vy, surf, sa);
    for (int i = W - 4 + n; i < W; i++) px_blend(strip, y0, y1, x + i, vy, surf, sa);
    for (int k = 0; k < w->nlines; k++) px_blend(strip, y0, y1, x + n + (int)w->lines[k], vy, surf, sa);
  }
}
static void wf_render(Ent *e) {
  Waterfall *w = ST(e, Waterfall);
  float x = wf_render_x(e);
  if (x + e->cw + 4 < g_camx || x - 4 > g_camx + VIEW_W) return;
  if (w->fg) {   /* Water.FillColor (LightSkyBlue * 0.3) and SurfaceColor (* 0.8) */
    uint16_t c = rgb(0x87CEFA);
    uint8_t fa = a8(0.3f * w->fade), sa = a8(0.8f * w->fade);
    gfx_rect(x, e->y, e->cw, e->ch, c, fa);
    gfx_rect(x - 1, e->y, 3, e->ch, c, sa);
    gfx_rect(x + e->cw - 2, e->y, 3, e->ch, c, sa);
    for (int k = 0; k < w->nlines; k++) gfx_rect(x + w->lines[k], e->y, 1, e->ch, c, sa);
    return;
  }
  int y0 = (int)e->y - g_camy, y1 = (int)(e->y + e->ch) - g_camy;
  if (y1 <= 0 || y0 >= VIEW_H) return;
  gfx_custom(wf_strip, e, y0 < 0 ? 0 : y0, y1 > VIEW_H ? VIEW_H : y1);
}
static const EntClass WATERFALL = {.name = "bigWaterfall", .size = sizeof(Waterfall), .update = wf_update, .render = wf_render};
static void new_waterfall(const EData *d) {
  float w = EA(d, bigWaterfall, width), h = EA(d, bigWaterfall, height);
  Ent *e = ent_new(&WATERFALL, d->x, d->y);
  if (!e) return;
  ent_box(e, w, h, 0, 0);
  e->collidable = 0;
  e->tags = TAG_TRANSITION_UPDATE;
  Waterfall *f = ST(e, Waterfall);
  f->fg = !strcmp(EAS(d, bigWaterfall, layer), "FG");
  f->fade = g_level.transitioning ? 0 : 1;   /* Added */
  e->depth = f->fg ? -49900 : 10010;
  (void)rndf();   /* its parallax: only RenderPosition is drawn */
  f->lines[0] = f->fg ? 3 : 6, f->lines[1] = f->fg ? w - 4 : w - 7;
  f->nlines = 2;
  if (w > 16) {
    int n = rndi((int)(w / 16));
    for (int i = 0; i < n && f->nlines < 8; i++) f->lines[f->nlines++] = 8 + rndf() * (w - 16);
  }
}

/* ---------------------------------------------------------------- BadelineBoost */
/* Sprites.xml's objects/badelineBoost/ is objects/badelineboost/ in the atlas: the frames are listed here */
enum { BB_IDLE, BB_FLASH, BB_BLINK };
static const uint8_t BBF_IDLE[] = {0, 1, 2, 3, 4, 5}, BBF_FLASH[] = {6, 7, 8, 9, 10, 11},
                     BBF_BLINK[] = {12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25};
static const FAnim BB_ANIMS[3] = {{BBF_IDLE, 6, 0, 255, 0.08f}, {BBF_FLASH, 6, 0, BB_IDLE, 0.08f}, {BBF_BLINK, 14, 0, BB_IDLE, 0.08f}};
static uint16_t bb_tex[26];
#define BB_NODES 9   /* with her own place: 7A's e-13 has the most, 8 nodes */
typedef struct {
  FSpr spr;
  CSpr dummy;           /* BadelineDummy: her sprite (the hair is not drawn) */
  Wiggler wig;
  SineWave wave;        /* the dummy's float */
  V2 spr_pos, pfrom, pto, bfrom, bto, from, to, dummy_at;
  float wait, p, alarm;
  Tween tw;
  int16_t nodes[BB_NODES][2];
  uint8_t node, nnodes, co, holding, travelling, dummy_on, stretch, can_skip;
  int8_t dummy_facing, final_, tw_on, lock;
} BBoost;
/* the tween to the next node: from the routine it starts next frame, from Skip (before the components) now */
static void bboost_tween_begin(Ent *e, bool now) {
  BBoost *b = ST(e, BBoost);
  b->from = v2(e->x, e->y);
  b->to = v2(b->nodes[b->node][0], b->nodes[b->node][1]);
  tween_start(&b->tw, fminf(3, v2len(v2sub(b->to, b->from)) / 320));
  b->tw_on = now ? 1 : 2;
  b->stretch = 1;
}
static void bboost_on_player(Ent *e, Player *p) {
  BBoost *b = ST(e, BBoost);
  if (b->co) return;
  /* BoostRoutine, up to its first yield */
  b->holding = 1, b->travelling = 1;
  b->node++;
  b->spr_pos = v2(0, 0);
  e->collidable = 0;
  b->final_ = b->node >= b->nnodes;
  /* TODO cutscene: finalCh9Boost (CS10_FinalLaunch) is Farewell's */
  if (p->holding) {   /* Drop() */
    Ent *h = p->holding;
    p->holding = NULL;
    holdable_release(h, v2(0, 0));
  }
  player_set_state(p, ST_DUMMY);
  p->dummy_auto_animate = false;
  p->dummy_gravity = false;
  if (p->inventory_dashes > 1) p->dashes = 1;
  else player_refill_dash(p);
  player_refill_stamina(p);
  p->speed = v2(0, 0);
  int num = sgn(p->ent->x - e->x);
  if (num == 0) num = -1;
  b->dummy_on = 1;
  cs_init(&b->dummy, SB_badeline);
  cs_play(&b->dummy, A_badeline_fallSlow, true);
  sine_set(&b->wave, 0);
  p->facing = -num;
  b->dummy_facing = (int8_t)num;
  b->pfrom = v2(p->ent->x, p->ent->y);
  b->pto = v2add(v2(e->x, e->y), v2((float)(num * 4), -3));
  b->bfrom = b->dummy_at = v2(e->x, e->y);
  b->bto = v2add(v2(e->x, e->y), v2((float)(-num * 4), 3));
  b->p = 0;
  b->co = 1;
}
static void bboost_routine(Ent *e) {
  BBoost *b = ST(e, BBoost);
  Player *p = level_player();
  if (b->wait > 0) {
    b->wait -= DT;
    return;
  }
  switch (b->co) {
    case 1:   /* for (p = 0; p < 1; p += DT / 0.2): both move together */
      if (b->p < 1) {
        V2 v = v2lerp(b->pfrom, b->pto, b->p);
        if (p && p->ent->cls) actor_move_to_x(p->ent, v.x, NULL), actor_move_to_y(p->ent, v.y, NULL);
        b->dummy_at = v2lerp(b->bfrom, b->bto, b->p);
        b->p += DT / 0.2f;
        break;
      }
      /* finalBoost: Level.ZoomTo(1.5) and Engine.TimeRate 0.5 are not done */
      cs_play(&b->dummy, A_badeline_boost, false);
      b->wait = 0.1f;
      b->co = 2;
      break;
    case 2:
      if (p && !p->dead) actor_move_v(p->ent, 5, NULL, NULL);
      b->wait = 0.1f;
      b->co = 3;
      break;
    case 3:
      /* endLevel (Farewell's golden boost) does not happen here */
      b->alarm = 0.15f;   /* then a dash back for the player, and the dummy goes */
      level_shake(0.3f);
      b->holding = 0;
      if (!b->final_) {
        if (p) {   /* Player.BadelineBoostLaunch(CenterX) */
          p->launch_approach_x = e->x, p->has_launch_approach = true;
          p->speed = v2(0, -330);
          p->auto_jump = true;
          if (p->holding) {
            Ent *h = p->holding;
            p->holding = NULL;
            holdable_release(h, v2(0, 0));
          }
          player_refill_dash(p);
          player_refill_stamina(p);
          p->dash_cooldown_timer = 0.2f;
          player_set_state(p, ST_LAUNCH);
        }
        bboost_tween_begin(e, false);
        level_dir_shake(v2(0, -1), 0.3f);
        b->co = 0;
      } else {
        level_freeze(0.1f);   /* Engine.FreezeTimer */
        b->co = 4;
      }
      break;
    case 4:   /* after a yield null (the displacement is not drawn) */
      level_flash_a(0xFFFF, 0.5f, true);
      level_dir_shake(v2(0, -1), 0.6f);
      if (p) {   /* Player.SummitLaunch(X) */
        p->summit_target_x = e->x;
        player_set_state(p, ST_SUMMITLAUNCH);
      }
      /* Finish() */
      particles_emit(PL_MID, &P_BadelineOldsite_P_Vanish, 12, v2(e->x, e->y), v2(6, 6), P_BadelineOldsite_P_Vanish.direction);
      g_level.cam_lock = 0;
      g_level.cam_offset = v2(0, -16);
      ent_remove(e);
      b->co = 0;
      break;
  }
}
static void bboost_update(Ent *e) {
  BBoost *b = ST(e, BBoost);
  Player *p = level_player();
  bool visible = !b->holding && !b->travelling;
  if (visible && level_on_interval(0.05f))
    particles_emit(PL_BG, &P_BadelineBoost_P_Ambience, 1, v2(e->x, e->y), v2(3, 3), P_BadelineBoost_P_Ambience.direction);
  if (b->holding && p) p->speed = v2(0, 0);
  if (!b->travelling && p) {
    V2 d = v2sub(player_center(p), v2(e->x, e->y));
    float num = clamped_map(v2len(d), 16, 64, 12, 0);
    b->spr_pos = approach_v(b->spr_pos, v2mul(safe_norm(d, 1), num), 32 * DT);
    if (b->can_skip && p->ent->x - e->x >= 100 && b->node + 1 < b->nnodes) {   /* Skip() */
      b->travelling = 1;
      b->node++;
      e->collidable = 0;
      bboost_tween_begin(e, true);
    }
  }
  /* components: the sprite, the stretch tween, the wiggler, the alarm, the routine */
  fs_update(&b->spr, BB_ANIMS);
  if (!b->spr.playing && b->spr.anim == BB_IDLE) {   /* goto="idle:10,flash:2,blink" */
    int r = rndi(13);
    fs_play(&b->spr, r < 10 ? BB_IDLE : r < 12 ? BB_FLASH : BB_BLINK, true);
  }
  if (b->tw_on == 2) b->tw_on = 1;   /* added by last frame's routine: it runs from now on */
  if (b->stretch && b->tw_on == 1) {
    {
      bool done = tween_update(&b->tw);
      float k = ease_sine_inout(b->tw.percent);
      V2 at = v2lerp(b->from, b->to, k);
      e->x = at.x, e->y = at.y;
      if (k < 0.9f && level_on_interval(0.03f)) {
        uint16_t t = fs_tex(&b->spr, BB_ANIMS, bb_tex);
        if (t != 0xFFFF) trail_add(e->x + b->spr_pos.x, e->y + b->spr_pos.y, t, 12, 12, 1, 1, 0xFF6DEF, 0.5f, e->depth + 1);
        particles_emit(PL_FG, &P_BadelineBoost_P_Move, 1, v2(e->x, e->y), v2(4, 4), P_BadelineBoost_P_Move.direction);
      }
      if (done) {
        b->stretch = 0, b->tw_on = 0;
        if (e->x >= g_level.room->x + g_level.room->w) {
          ent_remove(e);
          return;
        }
        b->travelling = 0;
        e->collidable = 1;
      }
    }
  }
  if (b->dummy_on) {
    cs_update(&b->dummy);
    sine_update(&b->wave);
  }
  wiggler_update(&b->wig);
  if (b->alarm > 0 && (b->alarm -= DT) <= 0) {
    if (p && p->dashes < p->inventory_dashes) p->dashes++;
    b->dummy_on = 0;
  }
  if (b->co) bboost_routine(e);
}
static void bboost_render(Ent *e) {
  BBoost *b = ST(e, BBoost);
  if (b->dummy_on) {
    cs_draw(&b->dummy, b->dummy_at.x, b->dummy_at.y + b->wave.value * 2, (float)b->dummy_facing, 1, 0, 0xFFFF, 255);
    light_add(b->dummy_at.x, b->dummy_at.y - 8, 0xDB7093, 1, 20, 60);   /* Color.PaleVioletRed */
  }
  bool visible = !b->holding && !b->travelling;
  if (visible) {
    float s = 1 + b->wig.value * 0.4f;
    draw_centered(fs_tex(&b->spr, BB_ANIMS, bb_tex), e->x + b->spr_pos.x, e->y + b->spr_pos.y, 0xFFFF, 255, s, s, 0);
  }
  if (b->stretch && b->tw_on == 1) {
    float k = yoyo(ease_sine_inout(b->tw.percent));
    draw_centered(T_objects_badelineboost_stretch, e->x, e->y, 0xFFFF, 255, 1 + k * 2, 1 - k * 0.75f, vangle(v2sub(b->to, b->from)));
  }
  if (visible || b->stretch) {
    light_add(e->x, e->y, 0xFFFFFF, 0.7f, 12, 20);
    bloom_add(e->x, e->y, 0.5f, 12);
  }
}
static void bboost_awake(Ent *e) {   /* CollideCheck<FakeWall>: in front of it */
  for (int i = 0; i < g_nents; i++) {
    Ent *o = &g_ents[i];
    if (o->cls && o->dead != 1 && o->collidable && !strcmp(o->cls->name, "fakeWall") && collide_ent_at(e, e->x, e->y, o)) e->depth = -12500;
  }
}
/* Level.LoadLevel sets CameraLockMode back to None: here, when the locking entity goes with its room */
static void lock_release(Ent *e) {
  for (int i = 0; i < g_nents; i++) {
    Ent *o = &g_ents[i];
    if (o != e && o->cls == e->cls && o->dead != 1) return;
  }
  g_level.cam_lock = 0;
}
static void bboost_removed(Ent *e) {
  if (ST(e, BBoost)->lock) lock_release(e);
}
static const EntClass BBOOST = {.name = "badelineBoost", .size = sizeof(BBoost), .update = bboost_update, .render = bboost_render,
                                .awake = bboost_awake, .removed = bboost_removed, .on_player = bboost_on_player,
                                .kind = KIND_PCOLLIDE};
static void new_bboost(const EData *d) {
  Ent *e = ent_new(&BBOOST, d->x, d->y);
  if (!e) return;
  e->depth = -1000000;
  ent_circle(e, 16, 0, 0);
  BBoost *b = ST(e, BBoost);
  if (!bb_tex[0]) tex_family("objects/badelineboost/idle", bb_tex, 26);
  fs_play(&b->spr, BB_IDLE, true);
  wiggler_init(&b->wig, 0.4f, 3);
  b->wave.freq = 0.25f;
  /* NodesWithPosition */
  b->nodes[0][0] = (int16_t)d->x, b->nodes[0][1] = (int16_t)d->y;
  int n = 1;
  for (int i = 0; i < d->nnodes && n < BB_NODES; i++, n++) {
    V2 v = ed_node(d, i);
    b->nodes[n][0] = (int16_t)v.x, b->nodes[n][1] = (int16_t)v.y;
  }
  b->nnodes = (uint8_t)n;
  b->can_skip = EAB(d, badelineBoost, canSkip);
  if (EAB(d, badelineBoost, lockCamera)) level_camera_locker(e, 1, 0, 160), b->lock = 1;   /* CameraLocker(BoostSequence) */
}

/* ---------------------------------------------------------------- ReflectionHeartStatue */
static const char *HEART_CODE[6] = {"U", "L", "DR", "UR", "L", "UL"};
typedef struct { float alarm, t; uint8_t index, lit, pad[2]; } HTorch;
typedef struct {
  float wait, p;
  V2 gem_at;
  uint8_t inputs[6], ninputs, enabled, co, orbs, torch[4], dummy, pad[3];
} HStatue;
static uint16_t ht_tex[7], ht_hint[4];
static void htorch_lit(Ent *e) {   /* PlayLit: from a random frame */
  HTorch *t = ST(e, HTorch);
  t->lit = 1;
  t->t = rndi(6) * 0.08f;
}
static void htorch_update(Ent *e) {
  HTorch *t = ST(e, HTorch);
  if (t->alarm > 0 && (t->alarm -= DT) <= 0) htorch_lit(e);
  if (t->lit) t->t += DT;
}
static void htorch_render(Ent *e) {
  HTorch *t = ST(e, HTorch);
  draw_centered(ht_hint[t->index & 3], e->x, e->y + 28, 0xFFFF, 255, 1, 1, 0);
  int f = t->lit ? 1 + (int)floorf(t->t / 0.08f) % 6 : 0;
  if (ht_tex[f] != 0xFFFF) gfx_tex_ex(ht_tex[f], e->x, e->y, 32, 64, 1, 1, 0, 0xFFFF, 255, 0);
  if (t->lit) {
    light_add(e->x, e->y, 0x20B2AA, 1, 24, 48);   /* Color.LightSeaGreen */
    bloom_add(e->x, e->y, 0.6f, 16);
  }
}
static const EntClass HTORCH = {.name = "reflectionHeartTorch", .size = sizeof(HTorch), .update = htorch_update, .render = htorch_render};
static uint8_t dash_code(V2 dir) {   /* "U"/"D" then "L"/"R", as bits */
  return (uint8_t)((dir.y < 0 ? 1 : dir.y > 0 ? 2 : 0) | (dir.x < 0 ? 4 : dir.x > 0 ? 8 : 0));
}
/* a torch's code: torch 1 flipped left-right, 2 up-down, 3 both (FlipCode) */
static uint8_t heart_code(int torch, int i) {
  uint8_t v = 0;
  for (const char *s = HEART_CODE[i]; *s; s++) {
    char c = *s;
    if ((torch & 1) && (c == 'L' || c == 'R')) c = c == 'L' ? 'R' : 'L';
    if ((torch & 2) && (c == 'U' || c == 'D')) c = c == 'U' ? 'D' : 'U';
    v |= c == 'U' ? 1 : c == 'D' ? 2 : c == 'L' ? 4 : 8;
  }
  return v;
}
static void heart_flag(char *out, int i) {
  memcpy(out, "heartTorch_", 11);
  out[11] = (char)('0' + i), out[12] = 0;
}
static void hstatue_spawn_heart(Ent *e) {
  /* new HeartGem(Position - (0, 52)): the heart gem is another file's (blackGem), made from a record */
  uint8_t attrs[EA_blackGem__size + 4];
  memset(attrs, 0, sizeof attrs);
  EData d;
  memset(&d, 0, sizeof d);
  d.type = ET_blackGem;
  d.x = e->x, d.y = e->y - 52;
  d.room = g_level.room ? g_level.room->index : 0;
  d.rx = (float)(g_level.room ? g_level.room->x : 0), d.ry = (float)(g_level.room ? g_level.room->y : 0);
  d.id = -1;
  d.attrs = d.nodes = attrs;
  ent_create(&d);
}
static void hstatue_check_all(Ent *e, bool skip) {   /* CheckIfAllActivated */
  HStatue *s = ST(e, HStatue);
  if (!s->enabled) return;
  char flag[16];
  for (int i = 0; i < 4; i++) {
    heart_flag(flag, i);
    if (!level_get_flag(flag)) return;
  }
  s->enabled = 0;   /* Activate */
  if (skip) hstatue_spawn_heart(e);
  else s->co = 1, s->wait = 0.533f;
}
static void hstatue_on_dash(Ent *e, V2 dir) {
  HStatue *s = ST(e, HStatue);
  if (!s->enabled) return;
  if (s->ninputs == 6) memmove(s->inputs, s->inputs + 1, 5), s->ninputs--;
  s->inputs[s->ninputs++] = dash_code(dir);
  char flag[16];
  for (int t = 0; t < 4; t++) {
    heart_flag(flag, t);
    if (level_get_flag(flag) || s->ninputs < 6) continue;
    bool ok = true;
    for (int i = 0; i < 6 && ok; i++) ok = s->inputs[i] == heart_code(t, i);
    if (!ok) continue;
    level_set_flag(flag, true);   /* Torch.Activate: lit 0.2 s later */
    Ent *te = ent_at(s->torch[t]);
    if (te->cls == &HTORCH) ST(te, HTorch)->alarm = 0.2f;
  }
  hstatue_check_all(e, false);
}
typedef struct { float scale; } WhiteHeart;
static void wh_render(Ent *e) {
  float s = ST(e, WhiteHeart)->scale;
  if (s > 0) draw_centered(T_collectables_heartGem_white00, e->x, e->y, 0xFFFF, 255, s, s, 0);
  bloom_add(e->x, e->y, s, 16);
}
static const EntClass WHITEHEART = {.name = "reflectionHeartDummy", .size = sizeof(WhiteHeart), .render = wh_render};
static void hstatue_update(Ent *e) {   /* ActivateRoutine */
  HStatue *s = ST(e, HStatue);
  if (!s->co) return;
  if (s->wait > 0) {
    s->wait -= DT;
    return;
  }
  Ent *dummy = ent_at(s->dummy);
  if (dummy->cls != &WHITEHEART) dummy = NULL;
  switch (s->co) {
    case 1: {   /* the white heart, then 20 orbs, one a frame */
      Ent *d = ent_new(&WHITEHEART, e->x, e->y - 52);
      s = ST(e, HStatue);
      if (d) {
        d->depth = 1;
        d->collidable = 0;
        ST(d, WhiteHeart)->scale = 0;
        s->dummy = ent_ref(d);
        dummy = d;
      }
      s->orbs = 0;
      s->co = 2;
    }
      /* fall through */
    case 2:
      if (s->orbs < 20) {
        absorb_orb_new(v2(e->x, e->y - 20), dummy);
        s = ST(e, HStatue);
        s->orbs++;
        break;
      }
      s->wait = 0.8f;
      s->co = 3;
      s->p = 0;
      break;
    case 3:
      if (s->p < 1) {
        if (dummy) ST(dummy, WhiteHeart)->scale = s->p;
        level_shake(0.3f);
        s->p += DT / 0.6f;
        break;
      }
      absorb_orbs_remove();
      if (dummy) ent_remove(dummy);
      level_flash(0xFFFF, false);
      hstatue_spawn_heart(e);
      s->co = 0;
      break;
  }
}
static void hstatue_render(Ent *e) {
  HStatue *s = ST(e, HStatue);
  Tex t;
  if (tex_get(T_objects_reflectionHeart_statue, &t))   /* JustifyOrigin(0.5, 1), Origin.Y - 1 */
    gfx_tex_ex(T_objects_reflectionHeart_statue, e->x, e->y, t.fw * t.scale / 2.f, t.fh * t.scale - 1.f, 1, 1, 0, 0xFFFF, 255, 0);
  /* ForsakenCitySatellite.Colors of the code */
  static const uint32_t COLORS[6] = {0xF0F0F0, 0x9171F2, 0x0A44E0, 0xB32D00, 0x9171F2, 0xFFCD37};
  for (int j = 0; j < 6; j++) {
    V2 at = v2add(s->gem_at, v2((j - 2.5f) * 24, 0));
    draw_centered(T_objects_reflectionHeart_gem, at.x, at.y, rgb(COLORS[j]), 255, 1, 1, 0);
    bloom_add(at.x, at.y, 0.3f, 12);
  }
}
static void hstatue_awake(Ent *e) { hstatue_check_all(e, true); }
static const EntClass HSTATUE = {.name = "reflectionHeartStatue", .size = sizeof(HStatue), .update = hstatue_update,
                                 .render = hstatue_render, .awake = hstatue_awake,
                                 .more = &(const EntMore){.on_dash = hstatue_on_dash}};
static void new_hstatue(const EData *d) {
  if (d->nnodes < 5) return;
  Ent *e = ent_new(&HSTATUE, d->x, d->y);
  if (!e) return;
  e->depth = 8999;
  e->collidable = 0;
  if (!ht_tex[0]) tex_family("objects/reflectionHeart/torch", ht_tex, 7), tex_family("objects/reflectionHeart/hint", ht_hint, 4);
  HStatue *s = ST(e, HStatue);
  s->gem_at = ed_node(d, 4);
  s->enabled = !g_session.heart;
  char flag[16];
  for (int i = 0; i < 4; i++) {
    V2 at = ed_node(d, i);
    Ent *t = ent_new(&HTORCH, at.x, at.y);
    s = ST(e, HStatue);
    if (!t) continue;
    t->depth = 8999;
    t->collidable = 0;
    ST(t, HTorch)->index = (uint8_t)i;
    heart_flag(flag, i);
    if (level_get_flag(flag)) htorch_lit(t);   /* Torch.Added */
    s->torch[i] = ent_ref(t);
  }
}
/* ---------------------------------------------------------------- the boss's blocks: tiles 'g', highlighted 'G' */
/* their tiles: in the blocks' pool (entities.c), with the room */
/* the tiles at alpha a, and the same box in 'G' (the same random picks) at alpha hl */
static void bt_draw(const TileQ *q, int w, int h, float x, float y, float a, float hl) {
  if (!q) return;
  if (a > 0) tiles_draw(q, 'g', w, h, x, y, 0xFFFF, a8(a));
  if (hl > 0) tiles_draw(q, 'G', w, h, x, y, 0xFFFF, a8(hl));
}

/* FallingBlock(finalBoss: true), the map's finalBossFallingBlock */
typedef struct {
  float wait, timer, delay, speed, hl, hl_from, hl_to, hl_p;
  TileQ *q;
  uint8_t w, h, step, triggered, hl_run, pad;
} BFall;
static void bfall_highlight(BFall *b, float to) {   /* HighlightFade(to): its coroutine starts next frame */
  b->hl_from = b->hl, b->hl_to = to, b->hl_p = 0, b->hl_run = 2;
}
static void bfall_drop(Ent *e) {   /* StopShaking, the dust, and the fall begins */
  BFall *b = ST(e, BFall);
  plat_stop_shaking(e);
  for (int i = 2; i < e->cw; i += 4) {
    if (rect_solid(e->x + i, e->y - 2, e->x + i + 1, e->y - 1))
      particles_emit(PL_MID, &P_FallingBlock_P_FallDustA, 2, v2(e->x + i, e->y), v2(4, 4), PI_F / 2);
    particles_emit(PL_MID, &P_FallingBlock_P_FallDustB, 2, v2(e->x + i, e->y), v2(4, 4), P_FallingBlock_P_FallDustB.direction);
  }
  b->speed = 0;
  b->step = 4;
}
static void bfall_update(Ent *e) {
  BFall *b = ST(e, BFall);
  plat_update(e);
  if (b->hl_run == 2) b->hl_run = 1, b->hl_p = 0, b->hl = b->hl_from;   /* its first step: p = 0 */
  else if (b->hl_run == 1) {
    b->hl_p += DT / 0.5f;
    if (b->hl_p < 1) b->hl = lerpf(b->hl_from, b->hl_to, ease_cube_inout(b->hl_p));
    else b->hl = b->hl_to, b->hl_run = 0;
  }
  if (b->wait > 0) {
    b->wait -= DT;
    return;
  }
  Room *rm = g_level.room;
  switch (b->step) {
    case 0:   /* the boss triggers it (the player cannot) */
      if (!b->triggered) break;
      b->step = 1;
      /* fall through */
    case 1:   /* while (FallDelay > 0) { FallDelay -= DT; yield null; } */
      if (b->delay > 0) {
        b->delay -= DT;
        break;
      }
      b->step = 2;
      /* fall through */
    case 2:   /* shake, highlight, 0.2 s */
      plat_start_shaking(e, 0);
      bfall_highlight(b, 1);
      b->wait = 0.2f;
      b->timer = 0.2f;
      b->step = 3;
      break;
    case 3:   /* while (timer > 0 && PlayerWaitCheck()) { yield null; timer -= DT; }: Triggered keeps it waiting */
      if (b->timer > 0) {
        b->step = 10;
        break;
      }
      bfall_drop(e);
      break;
    case 10:
      b->timer -= DT;
      if (b->timer > 0) break;
      bfall_drop(e);
      break;
    case 4:
      b->speed = approach(b->speed, 130, 500 * DT);
      if (plat_move_v_collide_solids(e, b->speed * DT, true)) {   /* landed */
        level_dir_shake(v2(0, 1), 0.2f);
        bfall_highlight(b, 0);
        plat_start_shaking(e, 0);
        for (int i = 2; i <= e->cw; i += 4)   /* LandParticles */
          if (rect_solid(e->x + i, e_bottom(e) + 3, e->x + i + 1, e_bottom(e) + 4)) {
            particles_emit(PL_FG, &P_FallingBlock_P_FallDustA, 1, v2(e->x + i, e_bottom(e)), v2(4, 4), -PI_F / 2);
            particles_emit(PL_FG, &P_FallingBlock_P_LandDust, 1, v2(e->x + i, e_bottom(e)), v2(4, 4), i < e->cw / 2 ? PI_F : 0);
          }
        b->wait = 0.2f;
        b->step = 6;
        break;
      }
      if (e_top(e) > rm->y + rm->h + 16 || (e_top(e) > rm->y + rm->h - 1 && collide_solid(e, e->x, e->y + 1))) {
        e->collidable = e->visible = 0;
        b->wait = 0.2f;   /* (and 0.2 s more and a shake when it falls into another room) */
        b->step = 5;
      }
      break;
    case 5:
      ent_remove(e);
      plat_static_movers_destroy(e);
      b->step = 9;
      break;
    case 6:
      plat_stop_shaking(e);
      if (tiles_solid_rect(e_left(e), e_bottom(e), e_right(e), e_bottom(e) + 1)) {
        e->safe = 1;
        b->step = 9;
        break;
      }
      b->step = 7;
      /* fall through */
    case 7:   /* while (CollideCheck<Platform>(Position + (0, 1))) yield 0.1 */
      if (collide_solid(e, e->x, e->y + 1) || collide_jumpthru(e, e->x, e->y + 1)) {
        b->wait = 0.1f;
        break;
      }
      b->step = 2;
      break;
    default: break;
  }
}
static void bfall_render(Ent *e) {
  BFall *b = ST(e, BFall);
  bt_draw(b->q, b->w, b->h, e->x + e->shakex, e->y + e->shakey, 1 - b->hl, b->hl);
}
static void bfall_shake(Ent *e, V2 a) { plat_static_movers_shake(e, a); }
static const EntClass BFALL = {.name = "finalBossFallingBlock", .size = sizeof(BFall), .update = bfall_update,
                               .render = bfall_render, .kind = KIND_SOLID, .more = &(const EntMore){.on_shake = bfall_shake}};
static void new_bfall(const EData *d) {
  float w = EA(d, finalBossFallingBlock, width), h = EA(d, finalBossFallingBlock, height);
  Ent *e = ent_new(&BFALL, d->x, d->y);
  if (!e) return;
  ent_box(e, w, h, 0, 0);
  BFall *b = ST(e, BFall);
  b->w = (uint8_t)(w / 8), b->h = (uint8_t)(h / 8);
  if ((b->q = blocks_new_tiles(b->w * b->h))) tiles_box('g', b->w, b->h, b->q);
}

/* FinalBossMovingBlock */
#define BM_NODES 4
typedef struct {
  float wait, delay, hl, p, alarm;
  Tween tw;
  V2 from, to;
  int16_t nodes[BM_NODES][2];
  TileQ *q;
  uint8_t w, h, node, nnodes, boss_node, step, highlighted, tw_on;
} BMove;
static const EntClass BMOVE;
static void bmove_particles(Ent *e, V2 moved, bool impact) {   /* ImpactParticles / StopParticles */
  float dir = vangle(moved);
  if (impact) {
    if (moved.x != 0) {
      float x = moved.x < 0 ? e_left(e) - 1 : e_right(e) + 1;
      for (int i = 0; i < e->ch / 8; i++) {
        V2 p = v2(x, e_top(e) + 4 + i * 8);
        if (!point_water(p) && point_solid(p.x, p.y)) {
          particles_emit1(PL_FG, &P_CrushBlock_P_Impact, v2add(p, v2(0, 2)), moved.x < 0 ? 0 : PI_F);
          particles_emit1(PL_FG, &P_CrushBlock_P_Impact, v2sub(p, v2(0, 2)), moved.x < 0 ? 0 : PI_F);
        }
      }
    }
    if (moved.y != 0) {
      float y = moved.y < 0 ? e_top(e) - 1 : e_bottom(e) + 1;
      for (int i = 0; i < e->cw / 8; i++) {
        V2 p = v2(e_left(e) + 4 + i * 8, y);
        if (!point_water(p) && point_solid(p.x, p.y)) {
          particles_emit1(PL_FG, &P_CrushBlock_P_Impact, v2add(p, v2(2, 0)), moved.y < 0 ? PI_F / 2 : -PI_F / 2);
          particles_emit1(PL_FG, &P_CrushBlock_P_Impact, v2sub(p, v2(2, 0)), moved.y < 0 ? PI_F / 2 : -PI_F / 2);
        }
      }
    }
    return;
  }
  if (moved.x != 0) {
    float x = moved.x > 0 ? e_right(e) - 1 : e_left(e);
    for (int i = 0; i < e->ch; i += 4) particles_emit1(PL_MID, &P_FinalBossMovingBlock_P_Stop, v2(x, e_top(e) + 2 + i + rndi(3) - 1), dir);
  }
  if (moved.y != 0) {
    float y = moved.y > 0 ? e_bottom(e) - 1 : e_top(e);
    for (int i = 0; i < e->cw; i += 4) particles_emit1(PL_MID, &P_FinalBossMovingBlock_P_Stop, v2(e_left(e) + 2 + i + rndi(3) - 1, y), dir);
  }
}
static void bmove_finish(Ent *e) {
  V2 from = v2(e_right(e) + 10, e_cym(e));   /* CenterRight + (10, 0) */
  for (int i = 0; i < e->cw / 8; i++)
    for (int j = 0; j < e->ch / 8; j++) level_debris(e->x + 4 + i * 8, e->y + 4 + j * 8, 'f', from);
  V2 c = e_center(e);
  for (int i = 0; i < e->cw; i += 4)   /* BreakParticles */
    for (int j = 0; j < e->ch; j += 4) {
      V2 p = v2(e->x + 2 + i, e->y + 2 + j);
      particles_emit(PL_MID, &P_FinalBossMovingBlock_P_Break, 1, p, v2(2, 2), vangle(v2sub(p, c)));
    }
  plat_static_movers_destroy(e);
  ent_remove(e);
}
static void bmove_start(Ent *e, float delay) {   /* StartMoving: the coroutine starts next frame */
  BMove *b = ST(e, BMove);
  b->delay = delay;
  b->step = 1;
  b->wait = 0;
}
static void bmove_destroy(Ent *e, float delay) {
  BMove *b = ST(e, BMove);
  if (e->dead == 1) return;
  b->step = 0;
  if (delay <= 0) {
    bmove_finish(e);
    return;
  }
  plat_start_shaking(e, delay);
  b->alarm = delay;
}
static void bmove_update(Ent *e) {
  BMove *b = ST(e, BMove);
  plat_update(e);
  if (b->alarm > 0 && (b->alarm -= DT) <= 0) {
    bmove_finish(e);
    return;
  }
  if (b->tw_on == 2) b->tw_on = 1;   /* added by last frame's coroutine: it runs from now on */
  if (b->tw_on == 1) {
    bool done = tween_update(&b->tw);
    V2 at = v2lerp(b->from, b->to, ease_cube_in(b->tw.percent));
    plat_move_to(e, at.x, at.y);
    if (done) {
      b->tw_on = 0;
      V2 d = safe_norm(v2sub(b->to, b->from), 2);
      if (tiles_solid_rect(e_left(e) + d.x, e_top(e) + d.y, e_right(e) + d.x, e_bottom(e) + d.y)) bmove_particles(e, v2sub(b->to, b->from), true);
      else bmove_particles(e, v2sub(b->to, b->from), false);
    }
  }
  if (!b->step) return;
  if (b->wait > 0) {
    b->wait -= DT;
    return;
  }
  switch (b->step) {
    case 1:
      plat_start_shaking(e, 0.2f + b->delay);
      if (b->highlighted) {
        b->wait = 0.2f + b->delay + 0.2f;
        b->step = 3;
        break;
      }
      b->p = 0;   /* for (p = 0; p < 1; p += DT / (0.2 + delay + 0.2)) { highlight = CubeIn(p); yield null; } */
      b->hl = 0;
      b->step = 2;
      break;
    case 2:
      b->p += DT / (0.2f + b->delay + 0.2f);
      if (b->p < 1) {
        b->hl = ease_cube_in(b->p);
        break;
      }
      b->hl = 1;
      b->highlighted = 1;
      /* fall through */
    case 3:
      b->delay = 0;
      b->node = (uint8_t)((b->node + 1) % b->nnodes);
      b->from = v2(e->x, e->y);
      b->to = v2(b->nodes[b->node][0], b->nodes[b->node][1]);
      tween_start(&b->tw, 0.8f);
      b->tw_on = 2;
      b->wait = 0.8f;
      b->step = 1;
      break;
  }
}
static void bmove_render(Ent *e) {
  BMove *b = ST(e, BMove);
  float x = e->x + e->shakex, y = e->y + e->shakey;
  bt_draw(b->q, b->w, b->h, x, y, 1 - b->hl, b->hl);
  if (b->hl > 0 && b->hl < 1) {   /* a hollow rect closing in, Color.Lerp(Purple, Pink, 0.7) */
    int n = (int)((1 - b->hl) * 16);
    gfx_hollow_rect(x - n, y - n, e->cw + 2 * n, e->ch + 2 * n, rgb(0xD886B4), 255);
  }
}
static void bmove_shake(Ent *e, V2 a) { plat_static_movers_shake(e, a); }
static const EntClass BMOVE = {.name = "finalBossMovingBlock", .size = sizeof(BMove), .update = bmove_update,
                               .render = bmove_render, .kind = KIND_SOLID, .more = &(const EntMore){.on_shake = bmove_shake}};
static void new_bmove(const EData *d) {
  float w = EA(d, finalBossMovingBlock, width), h = EA(d, finalBossMovingBlock, height);
  Ent *e = ent_new(&BMOVE, d->x, d->y);
  if (!e) return;
  ent_box(e, w, h, 0, 0);
  BMove *b = ST(e, BMove);
  b->w = (uint8_t)(w / 8), b->h = (uint8_t)(h / 8);
  b->boss_node = (uint8_t)EA(d, finalBossMovingBlock, nodeIndex);
  b->nodes[0][0] = (int16_t)d->x, b->nodes[0][1] = (int16_t)d->y;
  int n = 1;
  for (int i = 0; i < d->nnodes && n < BM_NODES; i++, n++) {
    V2 v = ed_node(d, i);
    b->nodes[n][0] = (int16_t)v.x, b->nodes[n][1] = (int16_t)v.y;
  }
  b->nnodes = (uint8_t)n;
  if ((b->q = blocks_new_tiles(b->w * b->h))) tiles_box('g', b->w, b->h, b->q);
}

/* ---------------------------------------------------------------- FinalBossShot */
typedef struct {
  CSpr spr;
  SineWave sine;
  V2 anchor, speed, perp;
  float cant_kill, appear, sine_mult, pdir;
  uint8_t boss, dead, in_cam, pad;
} Shot;
static const EntClass BOSS, SHOT, BEAM;
static void shot_on_player(Ent *e, Player *p) {
  Shot *s = ST(e, Shot);
  if (s->dead) return;
  if (s->cant_kill > 0) {
    s->dead = 1;
    ent_remove(e);
  } else
    player_die(p, safe_norm(v2sub(player_center(p), v2(e->x, e->y)), 1), false);
}
static V2 boss_center(const Ent *e) { return v2(e->x, e->y - 6); }   /* Circle(14, 0, -6) */
static V2 boss_shot_origin(Ent *e);
static void shot_update(Ent *e) {
  Shot *s = ST(e, Shot);
  cs_update(&s->spr);
  sine_update(&s->sine);
  if (s->appear > 0) {
    Ent *b = ent_at(s->boss);
    if (b->cls == &BOSS) s->anchor = boss_shot_origin(b), e->x = s->anchor.x, e->y = s->anchor.y;
    s->appear -= DT;
    return;
  }
  if (s->cant_kill > 0) s->cant_kill -= DT;
  s->anchor = v2add(s->anchor, v2mul(s->speed, DT));
  V2 at = v2add(s->anchor, v2mul(s->perp, s->sine_mult * s->sine.value * 3));
  e->x = at.x, e->y = at.y;
  s->sine_mult = approach(s->sine_mult, 1, 2 * DT);
  if (s->dead) return;
  bool in = e->x > g_level.cam.x - 8 && e->x < g_level.cam.x + 328 && e->y > g_level.cam.y - 8 && e->y < g_level.cam.y + 188;
  if (in && !s->in_cam) s->in_cam = 1;
  else if (!in && s->in_cam) {
    s->dead = 1;
    ent_remove(e);
    return;
  }
  if (level_on_interval(0.04f)) particles_emit(PL_FG, &P_FinalBossShot_P_Trail, 1, v2(e->x, e->y), v2(2, 2), s->pdir);
}
static void shot_render(Ent *e) {
  Shot *s = ST(e, Shot);
  static const int8_t OUT[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
  for (int i = 0; i < 4; i++) cs_draw(&s->spr, e->x + OUT[i][0], e->y + OUT[i][1], 1, 1, 0, 0, 255);   /* Color.Black */
  cs_draw(&s->spr, e->x, e->y, 1, 1, 0, 0xFFFF, 255);
}
static const EntClass SHOT = {.name = "finalBossShot", .size = sizeof(Shot), .update = shot_update, .render = shot_render,
                              .on_player = shot_on_player, .kind = KIND_PCOLLIDE};

/* ---------------------------------------------------------------- FinalBossBeam */
typedef struct {
  CSpr beam, start;
  float charge, follow, active, angle, beam_alpha, side_alpha;
  uint8_t boss, start_on, pad[2];
} Beam;
static V2 boss_beam_origin(Ent *e);
static bool segment_rect(V2 a, V2 b, float l, float t, float r, float bt) {   /* Collide.RectToLine */
  float t0 = 0, t1 = 1, dx = b.x - a.x, dy = b.y - a.y;
  float p[4] = {-dx, dx, -dy, dy}, q[4] = {a.x - l, r - a.x, a.y - t, bt - a.y};
  for (int i = 0; i < 4; i++) {
    if (p[i] == 0) {
      if (q[i] < 0) return false;
      continue;
    }
    float k = q[i] / p[i];
    if (p[i] < 0) t0 = fmaxf(t0, k);
    else t1 = fminf(t1, k);
    if (t0 > t1) return false;
  }
  return true;
}
static void beam_update(Ent *e) {
  Beam *b = ST(e, Beam);
  Ent *be = ent_at(b->boss);
  if (be->cls != &BOSS) {
    ent_remove(e);
    return;
  }
  int ended = cs_update(&b->beam);
  if (b->start_on) cs_update(&b->start);
  if (ended == A_badeline_beam_shoot) {   /* OnLastFrame("shoot") */
    ent_remove(e);
    return;
  }
  Player *p = level_player();
  V2 origin = boss_beam_origin(be);
  b->beam_alpha = approach(b->beam_alpha, 1, 2 * DT);
  if (b->charge > 0) {
    b->side_alpha = approach(b->side_alpha, 1, DT);
    if (p && !p->dead) {
      b->follow -= DT;
      b->charge -= DT;
      V2 pc = player_center(p);
      if (b->follow > 0 && !v2eq(pc, origin)) {
        V2 val = closest_on_line(origin, v2add(origin, angle_vec(b->angle, 2000)), pc);
        val = approach_v(val, pc, 200 * DT);
        b->angle = vangle(v2sub(val, origin));
      } else if (b->beam.anim == A_badeline_beam_charge)
        cs_play(&b->beam, A_badeline_beam_lock, false);
      if (b->charge <= 0) {
        level_dir_shake(angle_vec(b->angle, 1), 0.15f);
        /* DissipateParticles: along the beam, from the screen's center */
        V2 cam = v2add(g_level.cam, v2(160, 90));
        V2 v2a = v2add(origin, angle_vec(b->angle, 12)), v3 = v2add(origin, angle_vec(b->angle, 2000));
        V2 dir = safe_norm(v2sub(v3, v2a), 1), perp = v2(-dir.y, dir.x);
        float d1 = vangle(perp), d2 = vangle(v2mul(perp, -1));
        float num = v2len(v2sub(cam, v2a)) - 12;
        V2 at = closest_on_line(v2a, v3, cam);
        for (int i = 0; i < 200; i += 12)
          for (int j = -1; j <= 1; j += 2) {
            V2 r = v2mul(perp, rnd_rangef(-1, 1));
            particles_emit1(PL_FG, &P_FinalBossBeam_P_Dissipate, v2add(v2add(v2add(at, v2mul(dir, (float)i)), v2mul(perp, 2.f * j)), r), d1);
            particles_emit1(PL_FG, &P_FinalBossBeam_P_Dissipate, v2add(v2sub(v2add(at, v2mul(dir, (float)i)), v2mul(perp, 2.f * j)), r), d2);
            if (i != 0 && i < num) {
              particles_emit1(PL_FG, &P_FinalBossBeam_P_Dissipate, v2add(v2add(v2sub(at, v2mul(dir, (float)i)), v2mul(perp, 2.f * j)), r), d1);
              particles_emit1(PL_FG, &P_FinalBossBeam_P_Dissipate, v2add(v2sub(v2sub(at, v2mul(dir, (float)i)), v2mul(perp, 2.f * j)), r), d2);
            }
          }
      }
    }
  } else if (b->active > 0) {
    b->side_alpha = approach(b->side_alpha, 0, DT * 8);
    if (b->beam.anim != A_badeline_beam_shoot) {
      cs_play(&b->beam, A_badeline_beam_shoot, false);
      cs_play(&b->start, A_badeline_beam_start_shoot, true);
      b->start_on = 1;
    }
    b->active -= DT;
    if (b->active > 0 && p && !p->dead && p->ent->cls) {   /* PlayerCollideCheck: three lines against the player's hitbox */
      V2 a = v2add(origin, angle_vec(b->angle, 12)), c = v2add(origin, angle_vec(b->angle, 2000));
      V2 n = safe_norm(v2sub(c, a), 2), side = v2(-n.y, n.x);
      Ent *pe = p->ent;
      float l = e_left(pe), t = e_top(pe), r = e_right(pe), bt = e_bottom(pe);
      if (segment_rect(v2add(a, side), v2add(c, side), l, t, r, bt) || segment_rect(v2sub(a, side), v2sub(c, side), l, t, r, bt) ||
          segment_rect(a, c, l, t, r, bt))
        player_die(p, safe_norm(v2sub(player_center(p), origin), 1), false);
    }
  }
}
/* the darkness on both sides of the beam: clear on it, dark 120 px out, then dark to 240 px */
typedef struct { V2 at, perp; int a; } BeamDark;
static BeamDark beam_dark;
static void beam_dark_strip(uint16_t *strip, int y0, int y1, void *ctx) {
  const BeamDark *d = ctx;
  for (int vy = y0; vy < y1; vy++) {
    uint16_t *row = strip + (vy - y0) * VIEW_W;
    float py = vy + g_camy + 0.5f - d->at.y;
    float base = py * d->perp.y;
    for (int vx = 0; vx < VIEW_W; vx++) {
      float dist = fabsf((vx + g_camx + 0.5f - d->at.x) * d->perp.x + base);
      if (dist >= 240) continue;
      int a = dist >= 120 ? d->a : (int)(d->a * dist / 120);
      if (a > 0) row[vx] = blend565(row[vx], 0, a);
    }
  }
}
static void beam_render(Ent *e) {
  Beam *b = ST(e, Beam);
  Ent *be = ent_at(b->boss);
  if (be->cls != &BOSS) return;
  V2 val = boss_beam_origin(be);
  uint16_t t = cs_tex(&b->beam);
  Tex tx;
  float w = (t != 0xFFFF && tex_get(t, &tx)) ? tx.fw * (float)tx.scale : 128;
  V2 step = angle_vec(b->angle, w);
  bool shoot = b->beam.anim == A_badeline_beam_shoot;
  if (shoot) val = v2add(val, angle_vec(b->angle, 8));
  uint8_t alpha = a8(b->beam_alpha);
  for (int i = 0; i < 15; i++) {   /* only the copies on screen are drawn */
    V2 end = v2add(val, step);
    float l = fminf(val.x, end.x) - 16, r = fmaxf(val.x, end.x) + 16, tp = fminf(val.y, end.y) - 16, bt = fmaxf(val.y, end.y) + 16;
    if (r > g_camx && l < g_camx + VIEW_W && bt > g_camy && tp < g_camy + VIEW_H) cs_draw(&b->beam, val.x, val.y, 1, 1, b->angle, 0xFFFF, alpha);
    val = end;
  }
  if (shoot && b->start_on) cs_draw(&b->start, boss_beam_origin(be).x, boss_beam_origin(be).y, 1, 1, b->angle, 0xFFFF, alpha);
  int a = (int)(b->side_alpha * 0.35f * 256);
  if (a <= 0) return;
  V2 n = safe_norm(step, 1);
  beam_dark.at = val, beam_dark.perp = v2(-n.y, n.x), beam_dark.a = a;
  gfx_custom(beam_dark_strip, &beam_dark, 0, VIEW_H);
}
static const EntClass BEAM = {.name = "finalBossBeam", .size = sizeof(Beam), .update = beam_update, .render = beam_render};

/* ---------------------------------------------------------------- FinalBoss */
/* the attack sequences as little programs: each op runs, a wait or a yield ends the frame */
enum { OP_END, OP_WAIT, OP_YIELD, OP_SHOOT, OP_CHARGE, OP_AIM, OP_SHOOTAT, OP_WAITAIM, OP_A2BEGIN, OP_BEAM, OP_A2LOCK, OP_A2RECOIL,
       OP_MARK, OP_LOOP, OP_SKIPNOT0, OP_ENDIF0 };
typedef struct { uint8_t op, arg; } Op;   /* waits in 1/20 s */
#define BEAM_SEQ {OP_YIELD, 0}, {OP_A2BEGIN, 0}, {OP_WAIT, 2}, {OP_BEAM, 0}, {OP_WAIT, 18}, {OP_A2LOCK, 0}, {OP_WAIT, 10}, {OP_A2RECOIL, 0}, {OP_YIELD, 0}
#define VSHOTS {OP_AIM, 0}, {OP_SHOOTAT, 0}, {OP_WAITAIM, 3}, {OP_SHOOTAT, 0}, {OP_WAITAIM, 3}
#define VGAP {OP_CHARGE, 0}, {OP_WAIT, 10}
static const Op ATK01[] = {{OP_CHARGE, 0}, {OP_MARK, 0}, {OP_WAIT, 10}, {OP_SHOOT, 0}, {OP_WAIT, 20}, {OP_CHARGE, 0}, {OP_WAIT, 3}, {OP_WAIT, 6}, {OP_LOOP, 0}};
static const Op ATK02[] = {{OP_MARK, 0}, {OP_WAIT, 10}, BEAM_SEQ, {OP_WAIT, 8}, {OP_CHARGE, 0}, {OP_WAIT, 6}, {OP_SHOOT, 0}, {OP_WAIT, 10}, {OP_WAIT, 6}, {OP_LOOP, 0}};
static const Op ATK03[] = {{OP_CHARGE, 0}, {OP_WAIT, 2}, {OP_MARK, 0}, VSHOTS, VGAP, VSHOTS, VGAP, VSHOTS, VGAP, VSHOTS, VGAP, VSHOTS,
                           {OP_WAIT, 40}, {OP_CHARGE, 0}, {OP_WAIT, 14}, {OP_LOOP, 0}};
static const Op ATK04[] = {{OP_CHARGE, 0}, {OP_WAIT, 2}, {OP_MARK, 0}, VSHOTS, VGAP, VSHOTS, VGAP, VSHOTS, VGAP, VSHOTS, VGAP, VSHOTS,
                           {OP_WAIT, 30}, BEAM_SEQ, {OP_WAIT, 30}, {OP_CHARGE, 0}, {OP_LOOP, 0}};
static const Op ATK05[] = {{OP_WAIT, 4}, {OP_MARK, 0}, BEAM_SEQ, {OP_WAIT, 12}, {OP_CHARGE, 0}, {OP_WAIT, 6}, VSHOTS, VGAP, VSHOTS, VGAP, VSHOTS,
                           {OP_WAIT, 16}, {OP_LOOP, 0}};
static const Op ATK06[] = {{OP_MARK, 0}, BEAM_SEQ, {OP_WAIT, 14}, {OP_LOOP, 0}};
static const Op ATK07[] = {{OP_MARK, 0}, {OP_SHOOT, 0}, {OP_WAIT, 16}, {OP_CHARGE, 0}, {OP_WAIT, 16}, {OP_LOOP, 0}};
static const Op ATK08[] = {{OP_MARK, 0}, {OP_WAIT, 2}, BEAM_SEQ, {OP_WAIT, 16}, {OP_LOOP, 0}};
static const Op ATK09[] = {{OP_CHARGE, 0}, {OP_MARK, 0}, {OP_WAIT, 10}, {OP_SHOOT, 0}, {OP_WAIT, 3}, {OP_CHARGE, 0}, {OP_SHOOT, 0},
                           {OP_WAIT, 8}, {OP_CHARGE, 0}, {OP_WAIT, 2}, {OP_LOOP, 0}};
static const Op ATK10[] = {{OP_END, 0}};
static const Op ATK11[] = {{OP_SKIPNOT0, 2}, {OP_CHARGE, 0}, {OP_WAIT, 12}, {OP_MARK, 0}, {OP_SHOOT, 0}, {OP_WAIT, 38}, {OP_CHARGE, 0},
                           {OP_WAIT, 12}, {OP_LOOP, 0}};
static const Op ATK13[] = {{OP_ENDIF0, 0}, {OP_YIELD, 0}, {OP_CHARGE, 0}, {OP_MARK, 0}, {OP_WAIT, 10}, {OP_SHOOT, 0}, {OP_WAIT, 20},
                           {OP_CHARGE, 0}, {OP_WAIT, 3}, {OP_WAIT, 6}, {OP_LOOP, 0}};
static const Op ATK14[] = {{OP_MARK, 0}, {OP_WAIT, 4}, BEAM_SEQ, {OP_WAIT, 6}, {OP_LOOP, 0}};
static const Op ATK15[] = {{OP_MARK, 0}, {OP_WAIT, 4}, BEAM_SEQ, {OP_WAIT, 24}, {OP_LOOP, 0}};
static const Op *const ATTACKS[16] = {ATK01, ATK01, ATK02, ATK03, ATK04, ATK05, ATK06, ATK07, ATK08, ATK09, ATK10, ATK11, NULL, ATK13, ATK14, ATK15};

#define BOSS_NODES 12
typedef struct {
  CSpr spr;               /* badeline_boss, or (pattern 0, before the first hit) the player-like badeline */
  Wiggler wig;
  SineWave fsine;
  Tween tw;
  V2 spr_pos, avoid, from, to, aim;
  float wait, mv_wait, mv_timer, hit_alarm, spike_alarm;
  int16_t nodes[BOSS_NODES][2];
  uint8_t pattern, node, nnodes, normal, sitting, moving, player_moved, attacking;
  uint8_t ip, mv_step, aimed, last_hit, dialog, start_hit, tw_on, no_player;
  int8_t facing, normal_sx, last_anim;
  uint8_t syncing, autoanim;   /* the intro's BadelineAutoAnimator on the normal sprite */
  Hair hair;                   /* normalHair */
  Wiggler pop;
} Boss;
static void level_break_dash_block_ext(Ent *e, V2 dir);
static void autoanim(Sprite *spr, Wiggler *pop, int8_t *last, uint8_t *syncing, bool enabled);
static V2 boss_spr_scale(const Ent *e) {
  const Boss *b = ST(e, Boss);
  if (b->normal) {
    float k = 1 + 0.25f * b->pop.value;
    return v2(b->normal_sx * k, k);
  }
  float s = 1 + b->wig.value * 0.2f;
  return v2(b->facing * s, s);
}
static V2 boss_shot_origin(Ent *e) {   /* Center + Sprite.Position + (6 * Scale.X, 2) */
  Boss *b = ST(e, Boss);
  return v2add(v2add(boss_center(e), b->spr_pos), v2(6 * boss_spr_scale(e).x, 2));
}
static V2 boss_beam_origin(Ent *e) { return v2add(v2add(boss_center(e), ST(e, Boss)->spr_pos), v2(0, -14)); }
static bool player_at_attract(const Player *p) {   /* AtAttractTarget */
  return p->state == ST_ATTRACT && p->ent->x == p->attract_to.x && p->ent->y == p->attract_to.y;
}
static void boss_make_sprite(Ent *e) {   /* CreateBossSprite */
  Boss *b = ST(e, Boss);
  cs_init(&b->spr, SB_badeline_boss);
  cs_play(&b->spr, A_badeline_boss_idle, true);
  b->facing = -1;
  b->normal = 0;
}
static void boss_start_attacking(Ent *e) {
  Boss *b = ST(e, Boss);
  if (!ATTACKS[b->pattern & 15]) return;   /* pattern 12: none */
  b->ip = 0, b->wait = 0, b->attacking = 1;
}
static void boss_play(Ent *e, int anim, bool restart) { cs_play(&ST(e, Boss)->spr, anim, restart); }
static void boss_add_shot(Ent *e, V2 target) {   /* FinalBossShot.Init */
  Boss *b = ST(e, Boss);
  if (b->moving) return;   /* Added: removed while the boss moves */
  V2 c = boss_center(e);
  Ent *s = ent_new(&SHOT, c.x, c.y);
  if (!s) return;
  ent_box(s, 4, 4, -2, -2);
  s->depth = -1000000;
  Shot *sh = ST(s, Shot);
  cs_init(&sh->spr, SB_badeline_projectile);
  cs_play(&sh->spr, A_badeline_projectile_charge, true);
  sh->boss = ent_ref(e);
  sh->anchor = c;
  sh->cant_kill = 0.15f;
  sh->appear = 0.1f;
  sh->sine.freq = 1.4f;
  sine_set(&sh->sine, 0);
  sh->speed = safe_norm(v2sub(target, c), 100);
  V2 pp = v2(-sh->speed.y, sh->speed.x);
  sh->perp = safe_norm(pp, 1);
  sh->pdir = vangle(v2mul(sh->speed, -1));
}
static void boss_add_beam(Ent *e, Player *p) {   /* FinalBossBeam.Init */
  Boss *b = ST(e, Boss);
  if (b->moving) return;
  Ent *be = ent_new(&BEAM, e->x, e->y);
  if (!be) return;
  be->depth = -1000000;
  be->collidable = 0;
  Beam *m = ST(be, Beam);
  cs_init(&m->beam, SB_badeline_beam);
  cs_play(&m->beam, A_badeline_beam_charge, true);
  cs_init(&m->start, SB_badeline_beam_start);
  m->boss = ent_ref(e);
  m->charge = 1.4f, m->follow = 0.9f, m->active = 0.12f;
  V2 origin = boss_beam_origin(e), pc = player_center(p);
  int num = p->ent->y <= e->y + 16 ? 1 : -1;
  if (p->ent->x >= e->x) num = -num;
  float ang = vangle(v2sub(pc, origin));
  V2 val = closest_on_line(origin, v2add(origin, angle_vec(ang, 2000)), pc);
  V2 d = v2sub(pc, origin);
  val = v2add(val, v2mul(safe_norm(v2(-d.y, d.x), 100), (float)num));
  m->angle = vangle(v2sub(val, origin));
}
static void boss_run_attack(Ent *e) {
  Boss *b = ST(e, Boss);
  const Op *prog = ATTACKS[b->pattern & 15];
  if (!b->attacking || !prog) return;
  if (b->wait > 0) {
    b->wait -= DT;
    return;
  }
  Player *p = level_player();
  bool alive = p && !p->dead && p->ent->cls;
  for (int guard = 0; guard < 64; guard++) {
    Op op = prog[b->ip++];
    switch (op.op) {
      case OP_END: b->attacking = 0; return;
      case OP_WAIT: b->wait = op.arg * 0.05f; return;
      case OP_YIELD: return;
      case OP_SHOOT:   /* Shoot() */
        boss_play(e, A_badeline_boss_attack1Recoil, true);
        if (alive) boss_add_shot(e, player_center(p));
        break;
      case OP_CHARGE: boss_play(e, A_badeline_boss_attack1Begin, false); break;   /* StartShootCharge */
      case OP_AIM:
        b->aimed = alive;
        if (alive) b->aim = player_center(p);
        break;
      case OP_SHOOTAT:   /* ShootAt(at) */
        if (b->aimed) boss_play(e, A_badeline_boss_attack1Recoil, true), boss_add_shot(e, b->aim);
        break;
      case OP_WAITAIM:
        if (b->aimed) {
          b->wait = op.arg * 0.05f;
          return;
        }
        break;
      case OP_A2BEGIN: boss_play(e, A_badeline_boss_attack2Begin, true); break;
      case OP_BEAM:
        if (alive) boss_add_beam(e, p);
        break;
      case OP_A2LOCK: boss_play(e, A_badeline_boss_attack2Lock, true); break;
      case OP_A2RECOIL: boss_play(e, A_badeline_boss_attack2Recoil, false); break;
      case OP_MARK: break;
      case OP_LOOP:
        while (b->ip > 0 && prog[b->ip - 1].op != OP_MARK) b->ip--;
        break;
      case OP_SKIPNOT0:
        if (b->node != 0) b->ip += op.arg;
        break;
      case OP_ENDIF0:
        if (b->node == 0) {
          b->attacking = 0;
          return;
        }
        break;
    }
  }
}
static int ents_by_x(const EntClass *cls, uint8_t *out, int max) {   /* GetEntitiesCopy, sorted by (int)(a.X - b.X) */
  int n = 0;
  for (int i = 0; i < g_nents && n < max; i++)
    if (g_ents[i].cls == cls && g_ents[i].dead != 1) out[n++] = (uint8_t)i;
  for (int i = 1; i < n; i++) {
    uint8_t k = out[i];
    int j = i;
    while (j > 0 && (int)(g_ents[out[j - 1]].x - g_ents[k].x) > 0) out[j] = out[j - 1], j--;
    out[j] = k;
  }
  return n;
}
static void boss_trigger_falling(float left_of) {   /* TriggerFallingBlocks */
  uint8_t ids[32];
  int n = ents_by_x(&BFALL, ids, 32), num = 0;
  for (int i = 0; i < n; i++) {
    Ent *f = &g_ents[ids[i]];
    BFall *b = ST(f, BFall);
    if (b->triggered) continue;
    if (f->x >= left_of) break;
    plat_start_shaking(f, 0);
    b->triggered = 1;
    b->delay = 0.4f * num++;
  }
}
static void boss_trigger_moving(int node) {   /* TriggerMovingBlocks */
  uint8_t ids[32];
  int n = ents_by_x(&BMOVE, ids, 32);
  if (node > 0) {   /* DestroyMovingBlocks(node - 1) */
    float t = 0;
    for (int i = 0; i < n; i++)
      if (ST(&g_ents[ids[i]], BMove)->boss_node == node - 1) bmove_destroy(&g_ents[ids[i]], t), t += 0.05f;
  }
  float t = 0;
  for (int i = 0; i < n; i++) {
    Ent *m = &g_ents[ids[i]];
    if (m->dead == 1 || !m->cls) continue;
    if (ST(m, BMove)->boss_node == node) bmove_start(m, t), t += 0.15f;
  }
}
static void boss_hit(Ent *e, Player *p) {   /* OnPlayer */
  Boss *b = ST(e, Boss);
  if (b->normal) {
    V2 keep = b->spr_pos;
    boss_make_sprite(e);
    b->spr_pos = keep;
  }
  boss_play(e, A_badeline_boss_getHit, false);
  e->collidable = 0;
  b->avoid = v2(0, 0);
  b->node++;
  if (b->dialog && b->node >= 1 && b->node <= 3) {
    static const char *const TIRED[3] = {"ch6_boss_tired_a", "ch6_boss_tired_b", "ch6_boss_tired_c"};
    mini_textbox(TIRED[b->node - 1]);
  }
  for (int i = 0; i < g_nents; i++)
    if ((g_ents[i].cls == &SHOT || g_ents[i].cls == &BEAM) && g_ents[i].dead != 1) {
      if (g_ents[i].cls == &SHOT) ST(&g_ents[i], Shot)->dead = 1;
      ent_remove(&g_ents[i]);
    }
  boss_trigger_falling(e->x);
  boss_trigger_moving(b->node);
  b->attacking = 0;   /* attackCoroutine.Active = false */
  b->moving = 1;
  b->last_hit = b->node == b->nnodes - 1;
  const char *room = level_room_name_of(g_level.room->index);
  if (g_session.mode == M_A && b->last_hit && !strcmp(room, "boss-19")) b->spike_alarm = 0.25f;
  /* the music (badeline_fight, badeline_glitch) is not played */
  b->mv_step = 1;   /* Add(new Coroutine(MoveSequence(player, lastHit))) */
  b->mv_wait = 0;
  b->no_player = p == NULL;
  b->sitting = 0;   /* (the intro cutscene would have stood her up) */
}
static void boss_on_player(Ent *e, Player *p) { boss_hit(e, p); }
static void tentacles_retreat_all(void);
static void boss_move_sequence(Ent *e) {
  Boss *b = ST(e, Boss);
  if (b->tw_on == 2) b->tw_on = 1;   /* added by last frame's coroutine: it runs from now on */
  if (b->tw_on == 1) {
    bool done = tween_update(&b->tw);
    float k = ease_sine_inout(b->tw.percent);
    V2 at = v2lerp(b->from, b->to, k);
    e->x = at.x, e->y = at.y;
    if (k >= 0.1f && k <= 0.9f && level_on_interval(0.02f)) {
      uint16_t t = cs_tex(&b->spr);
      float ox, oy;
      cs_origin(&b->spr, t, &ox, &oy);
      V2 sc = boss_spr_scale(e);
      if (t != 0xFFFF) trail_add(e->x + b->spr_pos.x, e->y + b->spr_pos.y, t, ox, oy, sc.x, sc.y, 0xAC3232, 0.5f, e->depth + 1);
      particles_emit(PL_MID, &P_Player_P_DashB, 2, boss_center(e), v2(3, 3), vangle(v2sub(b->to, b->from)));
    }
    if (done) {   /* OnComplete */
      b->tw_on = 0;
      boss_play(e, A_badeline_boss_recoverHit, false);
      b->moving = 0;
      e->collidable = 1;
      Player *p = level_player();
      if (p && p->ent->cls) {
        b->facing = (int8_t)sgn(p->ent->x - e->x);
        if (b->facing == 0) b->facing = -1;
      }
      boss_start_attacking(e);
      sine_set(&b->fsine, 0);   /* floatSine.Reset() */
    }
  }
  if (!b->mv_step) return;
  if (b->mv_wait > 0) {
    b->mv_wait -= DT;
    return;
  }
  Player *p = level_player();
  bool alive = p && !p->dead && p->ent->cls && !b->no_player;
  switch (b->mv_step) {
    case 1:   /* the glitch tweens are not drawn */
      if (alive) player_start_attract(p, v2add(boss_center(e), v2(0, 4)));
      b->mv_timer = 0.15f;
      b->mv_step = 2;
      /* fall through */
    case 2:
      if (alive && !player_at_attract(p)) {
        b->mv_step = 3;
        return;
      }
      b->mv_step = 4;
      if (b->mv_timer > 0) {
        b->mv_wait = b->mv_timer;
        return;
      }
      break;
    case 3:   /* after the yield null */
      b->mv_timer -= DT;
      if (alive && !player_at_attract(p)) return;
      b->mv_step = 4;
      if (b->mv_timer > 0) {
        b->mv_wait = b->mv_timer;
        return;
      }
      break;
    default: break;
  }
  switch (b->mv_step) {
    case 4:
      tentacles_retreat_all();
      if (alive) level_freeze(0.1f);   /* Engine.TimeRate 0.5 / 0.75 is not slowed */
      if (alive) {   /* PushPlayer: Player.FinalBossPushLaunch */
        int dir = sgn(e->x - b->nodes[b->node][0]);
        if (dir == 0) dir = -1;
        p->has_launch_approach = false;
        p->speed = v2(0.9f * dir * 280, -150);
        p->auto_jump = true;
        player_refill_dash(p);
        player_refill_stamina(p);
        p->dash_cooldown_timer = 0.28f;
        player_set_state(p, ST_LAUNCH);
      }
      level_shake(0.3f);
      b->mv_wait = 0.05f;
      b->mv_step = 5;
      break;
    case 5:
      for (float num = 0; num < PI_F * 2; num += 0.17453292f) {
        V2 at = v2add(v2add(boss_center(e), b->spr_pos), angle_vec(num + rnd_rangef(-PI_F / 90, PI_F / 90), (float)(16 + rndi(4))));
        particles_emit1(PL_MID, &P_FinalBoss_P_Burst, at, num);
      }
      b->mv_wait = 0.05f;
      b->mv_step = 6;
      break;
    case 6:   /* the tween back to TimeRate 1 and FinalBossStarfield's alpha are not here */
      b->mv_wait = 0.2f;
      b->mv_step = 7;
      break;
    case 7: {
      b->from = v2(e->x, e->y);
      int k = b->node < b->nnodes ? b->node : b->nnodes - 1;
      b->to = v2(b->nodes[k][0], b->nodes[k][1]);
      tween_start(&b->tw, fmaxf(v2len(v2sub(b->to, b->from)) / 600, DT));
      b->tw_on = 2;
      b->mv_step = 0;
      break;
    }
  }
}
static void boss_update_(Ent *e) {
  Boss *b = ST(e, Boss);
  /* components: the attack, the float sine, the wiggler, the sprite, the move sequence, the alarms */
  boss_run_attack(e);
  sine_update(&b->fsine);
  wiggler_update(&b->wig);
  cs_update(&b->spr);
  if (b->normal && b->autoanim) {
    Sprite s = cs_raw(&b->spr);
    autoanim(&s, &b->pop, &b->last_anim, &b->syncing, true);
    cs_back(&b->spr, &s);
  }
  if (b->pop.active) wiggler_update(&b->pop);
  boss_move_sequence(e);
  b = ST(e, Boss);
  if (b->hit_alarm > 0 && (b->hit_alarm -= DT) <= 0) boss_hit(e, NULL);   /* startHit: OnPlayer(null) after 0.5 s */
  if (b->spike_alarm > 0 && (b->spike_alarm -= DT) <= 0) {   /* boss-19: every spinner breaks */
    Room *rm = g_level.room;
    spinners_destroy_near(rm->x + rm->w / 2.f, rm->y + rm->h / 2.f, (float)(rm->w > rm->h ? rm->w : rm->h));
  }
  if (b->sitting) return;
  Player *p = level_player();
  bool has_p = p && p->ent->cls && !p->dead;
  if (!b->moving && has_p) {
    if (b->facing == -1 && p->ent->x > e->x + 20) b->facing = 1, wiggler_restart(&b->wig);
    else if (b->facing == 1 && p->ent->x < e->x - 20) b->facing = -1, wiggler_restart(&b->wig);
  }
  if (!b->player_moved && has_p && (p->speed.x != 0 || p->speed.y != 0)) {
    b->player_moved = 1;
    if (b->pattern != 0) boss_start_attacking(e);
    boss_trigger_moving(0);
  }
  if (!b->moving) b->spr_pos = v2add(b->avoid, v2(b->fsine.value * 3, b->fsine.value_over_two * 4));
  else b->spr_pos = approach_v(b->spr_pos, v2(0, 0), 12 * DT);
  /* CollideFirst<DashBlock> with a 6 px circle: broken from below */
  V2 c = boss_center(e);
  for (int i = 0; i < g_nents; i++) {
    Ent *o = &g_ents[i];
    if (!o->cls || o->dead == 1 || !o->collidable || strcmp(o->cls->name, "dashBlock")) continue;
    float nx = clampf(c.x, e_left(o), e_right(o)), ny = clampf(c.y, e_top(o), e_bottom(o));
    if ((nx - c.x) * (nx - c.x) + (ny - c.y) * (ny - c.y) < 36) {
      level_break_dash_block_ext(o, v2(0, -1));
      break;
    }
  }
  if (!level_in_bounds(v2(e->x, e->y), 24)) {
    e->active = e->visible = e->collidable = 0;
    return;
  }
  V2 target = v2(0, 0);
  if (!b->moving && has_p) {
    V2 d = v2sub(c, player_center(p));
    float l = clamped_map(v2len(d), 32, 88, 12, 0);
    if (l > 0) target = safe_norm(d, l);
  }
  b->avoid = approach_v(b->avoid, target, 40 * DT);
}
static Sprite boss_normal_sprite(const Ent *e) {   /* NormalSprite, with its scale, for its hair */
  Sprite s = cs_raw(&ST(e, Boss)->spr);
  V2 k = boss_spr_scale(e);
  s.sx = k.x, s.sy = k.y;
  return s;
}
static void boss_update(Ent *e) {
  boss_update_(e);
  Boss *b = ST(e, Boss);
  if (e->dead == 1 || !b->normal) return;
  Sprite s = boss_normal_sprite(e);   /* normalHair.AfterUpdate (it faces left) */
  hair_follow(&b->hair, &s, v2add(v2(e->x, e->y), b->spr_pos), -1);
}
static void boss_render(Ent *e) {
  Boss *b = ST(e, Boss);
  V2 s = boss_spr_scale(e);
  float x = e->x + b->spr_pos.x, y = e->y + b->spr_pos.y;
  if (b->normal) {   /* NormalSprite.Position.Floor(), after its hair */
    Sprite ns = boss_normal_sprite(e);
    hair_draw(&b->hair, &ns, -1, rgb(0x9B3FB5), 1);
    x = floorf(x), y = floorf(y);
  }
  cs_draw(&b->spr, x, y, s.x, s.y, 0, 0xFFFF, 255);
  light_add(x, y - 10, 0xFFFFFF, 1, 32, 64);
}
static void boss_mid_new(void);
static const EntClass BOSS = {.name = "finalBoss", .size = sizeof(Boss), .update = boss_update, .render = boss_render,
                              .removed = lock_release, .on_player = boss_on_player, .kind = KIND_PCOLLIDE};
static void new_boss(const EData *d) {
  Ent *e = ent_new(&BOSS, d->x, d->y);
  if (!e) return;
  ent_circle(e, 14, 0, -6);
  Boss *b = ST(e, Boss);
  b->pattern = (uint8_t)EA(d, finalBoss, patternIndex);
  b->dialog = EAB(d, finalBoss, dialog);
  b->start_hit = EAB(d, finalBoss, startHit);
  b->nodes[0][0] = (int16_t)d->x, b->nodes[0][1] = (int16_t)d->y;
  int n = 1;
  for (int i = 0; i < d->nnodes && n < BOSS_NODES; i++, n++) {
    V2 v = ed_node(d, i);
    b->nodes[n][0] = (int16_t)v.x, b->nodes[n][1] = (int16_t)v.y;
  }
  b->nnodes = (uint8_t)n;
  b->fsine.freq = 0.6f;
  sine_set(&b->fsine, 0);
  wiggler_init(&b->wig, 0.6f, 3);
  b->wig.active = false;
  b->facing = -1;
  level_camera_locker(e, EAB(d, finalBoss, cameraLockY) ? 2 : 3, 140, EA(d, finalBoss, cameraPastY));   /* CameraLocker */
  /* Added */
  const char *room = level_room_name_of(d->room);
  if (b->pattern == 0) {   /* the player-like Badeline, laughing */
    cs_init(&b->spr, SB_badeline);
    cs_play(&b->spr, A_badeline_laugh, true);
    b->normal = 1, b->normal_sx = -1;
  } else
    boss_make_sprite(e);
  wiggler_init(&b->pop, 0.5f, 4);
  for (int i = 0; i < 4; i++) b->hair.n[i] = v2(e->x, e->y);
  if (b->pattern == 0 && !level_get_flag("boss_intro") && !strcmp(room, "boss-00")) {
    b->sitting = 1;   /* until CS06_BossIntro (the eventTrigger ch6_boss_intro) stands her up */
    e->y += 16;
    cs_play(&b->spr, A_badeline_pretendDead, true);
    b->normal_sx = 1;
  } else if (b->pattern == 0 && !level_get_flag("boss_mid") && !strcmp(room, "boss-14"))
    boss_mid_new();
  else if (b->start_hit)
    b->hit_alarm = 0.5f;
}

/* ---------------------------------------------------------------- ReflectionTentacles */
/* four layers (the map's one and three behind it), each a row of tentacles drawn as flat quads
 * in the layer's color (the arm textures' light streaks are not drawn) */
#define TENT_MAX 44
#define TENT_NODES 10
typedef struct { int16_t x, y, fx, fy; uint8_t lerp, pad; } Tent;   /* 1/2 px from the layer's first node */
typedef struct {
  V2 base, outwards, last_out;
  float ease, offset, fear, sound;
  uint16_t seed;
  uint8_t index, nnodes, slide_until, layer, count, has_player;
  int16_t nodes[TENT_NODES][2];
  Tent t[TENT_MAX];
} Tentacles;
static const EntClass TENTACLES;
static float tent_rand(const Tentacles *t, int i, int k) { return hash01(t->seed, (uint32_t)(i * 8 + k)); }
static float tent_width(const Tentacles *t, int i) { return 4 + tent_rand(t, i, 2) * 16; }
static V2 tent_node(const Tentacles *t, int i) { return v2(t->nodes[i][0], t->nodes[i][1]); }
static V2 tent_pos(const Tentacles *t, int i) { return v2(t->base.x + t->t[i].x * 0.5f, t->base.y + t->t[i].y * 0.5f); }
static void tent_set(Tentacles *t, int i, V2 p) {
  t->t[i].x = (int16_t)clampf(roundf((p.x - t->base.x) * 2), -32767, 32767);
  t->t[i].y = (int16_t)clampf(roundf((p.y - t->base.y) * 2), -32767, 32767);
}
static V2 tent_target(const Tentacles *t, int i, V2 position, float along, const Player *p) {   /* TargetTentaclePosition */
  V2 val = v2sub(position, v2mul(t->outwards, t->offset)), val2 = val;
  V2 perp = v2(-t->outwards.y, t->outwards.x);
  if (p) val2 = closest_on_line(v2sub(val2, v2mul(perp, 200)), v2add(val2, v2mul(perp, 200)), v2(p->ent->x, p->ent->y));
  V2 val4 = v2add(val, v2mul(perp, -220 + along + tent_width(t, i) * 0.5f));
  float num = v2len(v2sub(val2, val4));
  return v2add(val4, v2mul(t->outwards, num * 0.6f));
}
static void tent_retreat(Tentacles *t) {
  if (t->index >= t->nnodes - 1) return;
  t->last_out = t->outwards;
  t->ease = 0;
  t->index++;
  for (int i = 0; i < t->count; i++) t->t[i].lerp = 0, t->t[i].fx = t->t[i].x, t->t[i].fy = t->t[i].y;
}
static void tentacles_retreat_all(void) {
  for (int i = 0; i < g_nents; i++)
    if (g_ents[i].cls == &TENTACLES && g_ents[i].dead != 1) tent_retreat(ST(&g_ents[i], Tentacles));
}
static void tent_move(Tentacles *t, V2 pos, const Player *p) {   /* MoveTentacles */
  float num = 0;
  for (int i = 0; i < t->count; i++) {
    V2 target = tent_target(t, i, pos, num, p), at = tent_pos(t, i);
    float k = 1 - powf(0.1f * (0.25f + tent_rand(t, i, 0) * 0.75f), DT);
    tent_set(t, i, v2add(at, v2mul(v2sub(target, at), k)));
    num += tent_width(t, i);
  }
}
static void tent_snap(Tentacles *t, const Player *p) {
  float num = 0;
  for (int i = 0; i < t->count; i++) {
    t->t[i].lerp = 255;
    tent_set(t, i, tent_target(t, i, tent_node(t, t->index), num, p));
    num += tent_width(t, i);
  }
}
static void tent_awake(Ent *e) {
  Tentacles *t = ST(e, Tentacles);
  Player *p = level_player();
  bool flag = false;
  while (p && p->ent->cls && t->index < t->nnodes - 1) {
    V2 n = tent_node(t, t->index);
    V2 c = closest_on_line(n, v2add(n, v2mul(t->outwards, 10000)), player_center(p));
    if (v2len(v2sub(n, c)) >= t->fear) break;
    flag = true;
    tent_retreat(t);
  }
  if (flag) {
    t->ease = 1;
    tent_snap(t, NULL);
  }
}
static void tent_update(Ent *e) {
  Tentacles *t = ST(e, Tentacles);
  t->sound -= DT;
  Player *pl = level_player();
  const Player *p = pl && pl->ent->cls ? pl : NULL;
  t->has_player = p != NULL;
  if (t->slide_until > t->index) {
    if (p) {
      V2 n = tent_node(t, t->index);
      V2 c = closest_on_line(v2sub(n, v2mul(t->outwards, 10000)), v2add(n, v2mul(t->outwards, 10000)), player_center(p));
      if (v2len(v2sub(c, n)) < 32) {
        tent_retreat(t);
        t->outwards = safe_norm(v2sub(tent_node(t, t->index - 1), tent_node(t, t->index)), 1);
      } else
        tent_move(t, v2sub(c, v2mul(t->outwards, 190)), p);
    }
  } else {
    bool boss = first_of(&BOSS) != NULL;
    if (!boss && p && !p->dead && t->index < t->nnodes - 1) {
      V2 n = tent_node(t, t->index);
      V2 c = closest_on_line(n, v2add(n, v2mul(t->outwards, 10000)), player_center(p));
      if (v2len(v2sub(n, c)) < t->fear) tent_retreat(t);
    }
    if (t->index > 0) {
      t->ease = approach(t->ease, 1, (t->index != t->nnodes - 1 ? 1 : 2) * DT);
      V2 d = v2sub(tent_node(t, t->index - 1), tent_node(t, t->index));
      float q = 1 - (1 - t->ease) * (1 - t->ease);   /* Ease.QuadOut */
      t->outwards = angle_vec(angle_lerp(vangle(t->last_out), vangle(d), q), 1);
      float num = 0;
      for (int i = 0; i < t->count; i++) {
        V2 target = tent_target(t, i, tent_node(t, t->index), num, p);
        if (t->t[i].lerp < 255) {
          float l = t->t[i].lerp / 255.f + DT / (0.5f + tent_rand(t, i, 4) * 0.25f);
          t->t[i].lerp = (uint8_t)(l >= 1 ? 255 : l * 255);
          V2 from = v2(t->base.x + t->t[i].fx * 0.5f, t->base.y + t->t[i].fy * 0.5f);
          tent_set(t, i, v2lerp(from, target, ease_cube_inout(fminf(l, 1))));
        } else {
          V2 at = tent_pos(t, i);
          float k = 1 - powf(0.1f * (0.25f + tent_rand(t, i, 0) * 0.75f), DT);
          tent_set(t, i, v2add(at, v2mul(v2sub(target, at), k)));
        }
        num += tent_width(t, i);
      }
    } else
      tent_move(t, tent_node(t, t->index), p);
  }
}
static float fast_sin(float x) {   /* a parabola, good to 0.1% after the correction */
  x = fmodf(x + PI_F, 2 * PI_F);
  if (x < 0) x += 2 * PI_F;
  x -= PI_F;
  float y = 1.2732395f * x - 0.4052847f * x * fabsf(x);
  return 0.225f * (y * fabsf(y) - y) + y;
}
/* one convex quad, filled where pixel centers are inside */
static void quad_fill(uint16_t *strip, int y0, int y1, const V2 *v, uint16_t pc, int a) {
  float miny = v[0].y, maxy = v[0].y;
  for (int i = 1; i < 4; i++) miny = fminf(miny, v[i].y), maxy = fmaxf(maxy, v[i].y);
  int ra = (int)ceilf(miny - 0.5f), rb = (int)ceilf(maxy - 0.5f);
  if (ra < y0) ra = y0;
  if (rb > y1) rb = y1;
  for (int y = ra; y < rb; y++) {
    float yc = y + 0.5f, xl = 1e9f, xr = -1e9f;
    for (int i = 0; i < 4; i++) {
      V2 p = v[i], q = v[(i + 1) & 3];
      if ((p.y <= yc) == (q.y <= yc)) continue;
      float x = p.x + (yc - p.y) * (q.x - p.x) / (q.y - p.y);
      xl = fminf(xl, x), xr = fmaxf(xr, x);
    }
    if (xl > xr) continue;
    int xa = (int)ceilf(xl - 0.5f), xb = (int)ceilf(xr - 0.5f);
    if (xa < 0) xa = 0;
    if (xb > VIEW_W) xb = VIEW_W;
    uint16_t *d = strip + (y - y0) * VIEW_W;
    if (a >= 256)
      for (int x = xa; x < xb; x++) d[x] = pc;
    else
      for (int x = xa; x < xb; x++) d[x] = blend565(d[x], pc, a);
  }
}
static void tent_strip(uint16_t *strip, int y0, int y1, void *ctx) {
  Ent *e = ctx;
  Tentacles *t = ST(e, Tentacles);
  static const uint32_t COLOR[4] = {0x3F2A4F, 0x7B3555, 0x662847, 0x492632};
  float alpha = t->index == t->nnodes - 1 ? 1 - t->ease : 1;
  int a = (int)(alpha * 256);
  if (a <= 0) return;
  uint16_t pc = a >= 256 ? rgb(COLOR[t->layer & 3]) : scale565(rgb(COLOR[t->layer & 3]), a);
  V2 out = t->outwards, perpv = v2(out.y, -out.x);   /* -outwards.Perpendicular() */
  float cx = (float)g_camx, cy = (float)g_camy, sy0 = y0 + cy, sy1 = y1 + cy;
  float time = g_level.time_active;
  for (int i = 0; i < t->count; i++) {
    V2 pos = tent_pos(t, i);
    float w = tent_width(t, i), len = 32 + tent_rand(t, i, 1) * 64, wave = tent_rand(t, i, 3);
    V2 val2 = v2mul(perpv, w * 0.5f + 2);
    /* the filler, 240 px back */
    V2 q[4] = {v2add(pos, val2), v2sub(v2add(pos, v2mul(val2, 1.5f)), v2mul(out, 240)), v2sub(v2sub(pos, v2mul(val2, 1.5f)), v2mul(out, 240)),
               v2sub(pos, val2)};
    float lo = q[0].y, hi = q[0].y;
    for (int k = 1; k < 4; k++) lo = fminf(lo, q[k].y), hi = fmaxf(hi, q[k].y);
    if (hi >= sy0 && lo < sy1) {
      for (int k = 0; k < 4; k++) q[k].x -= cx, q[k].y -= cy;
      quad_fill(strip, y0, y1, q, pc, a);
    }
    /* the arm: 10 quads along a wave */
    float seg = len / 10 + yoyo(t->t[i].lerp / 255.f) * 6;
    float reach = seg * 10 + 6 + w;
    if (pos.y + reach < sy0 && pos.y - reach < sy0) continue;
    if (pos.y - reach >= sy1 && pos.y + reach >= sy1) continue;
    if (fminf(pos.y - reach, pos.y + reach) >= sy1 || fmaxf(pos.y - reach, pos.y + reach) < sy0) continue;
    V2 v3 = pos, v4 = val2;
    float pw = 1;
    for (int j = 1; j <= 10; j++) {
      pw *= 1.1f;
      float num2 = j / 10.f;
      float s = fast_sin(time * wave * pw * 2 + wave * 3 + j * 0.05f);
      V2 v6 = v2add(v2add(v3, v2mul(out, seg)), v2mul(perpv, s * (2 + 4 * num2)));
      V2 v7 = v2mul(val2, 1 - num2);
      V2 r[4] = {v2sub(v6, v7), v2sub(v3, v4), v2add(v3, v4), v2add(v6, v7)};
      float rl = r[0].y, rh = r[0].y;
      for (int k = 1; k < 4; k++) rl = fminf(rl, r[k].y), rh = fmaxf(rh, r[k].y);
      if (rh >= sy0 && rl < sy1) {
        for (int k = 0; k < 4; k++) r[k].x -= cx, r[k].y -= cy;
        quad_fill(strip, y0, y1, r, pc, a);
      }
      v3 = v6, v4 = v7;
    }
  }
}
static void tent_render(Ent *e) {
  Tentacles *t = ST(e, Tentacles);
  if (!t->count) return;
  gfx_custom(tent_strip, e, 0, VIEW_H);
}
static const EntClass TENTACLES = {.name = "tentacles", .size = sizeof(Tentacles), .update = tent_update, .render = tent_render,
                                   .awake = tent_awake};
static Ent *tent_create(const int16_t (*nodes)[2], int n, float fear, int slide, int layer) {   /* Create */
  Ent *e = ent_new(&TENTACLES, nodes[0][0], nodes[0][1]);
  if (!e) return NULL;
  e->collidable = 0;
  e->tags = TAG_TRANSITION_UPDATE;
  Tentacles *t = ST(e, Tentacles);
  for (int i = 0; i < n; i++) {
    t->nodes[i][0] = (int16_t)(nodes[i][0] + rndi(16) - 8);
    t->nodes[i][1] = (int16_t)(nodes[i][1] + rndi(16) - 8);
  }
  t->nnodes = (uint8_t)n;
  t->base = tent_node(t, 0);
  e->x = t->base.x, e->y = t->base.y;
  t->outwards = safe_norm(v2sub(tent_node(t, 0), tent_node(t, 1)), 1);
  t->fear = fear;
  t->slide_until = (uint8_t)slide;
  t->layer = (uint8_t)layer;
  t->sound = 0.25f;
  static const int32_t DEPTH[4] = {-1000000, 8990, 10010, 10011};
  static const float OFFSET[4] = {110, 80, 50, 20};
  e->depth = DEPTH[layer];
  t->offset = OFFSET[layer];
  t->seed = (uint16_t)rndi(65536);
  float along = 0;
  int k = 0;
  while (k < TENT_MAX && along < 440) {   /* Width 4..20 each, until 440 px wide */
    tent_set(t, k, tent_target(t, k, tent_node(t, 0), along, NULL));
    t->t[k].lerp = 0;
    along += tent_width(t, k);
    k++;
  }
  t->count = (uint8_t)k;
  return e;
}
static void new_tentacles(const EData *d) {
  int16_t nodes[TENT_NODES][2];
  int n = 0;
  nodes[n][0] = (int16_t)d->x, nodes[n][1] = (int16_t)d->y, n++;
  for (int i = 0; i < d->nnodes && n < TENT_NODES; i++, n++) {
    V2 v = ed_node(d, i);
    nodes[n][0] = (int16_t)v.x, nodes[n][1] = (int16_t)v.y;
  }
  if (n < 2) return;
  const char *fd = EAS(d, tentacles, fear_distance);
  float fear = !strcmp(fd, "close") ? 16 : !strcmp(fd, "medium") ? 40 : !strcmp(fd, "far") ? 80 : 0;
  int slide = (int)EA(d, tentacles, slide_until);
  Ent *e = tent_create(nodes, n, fear, slide, 0);
  if (!e) return;
  /* Added: three more layers from the same (original) nodes */
  for (int i = 1; i < 4; i++) tent_create(nodes, n, fear, ST(e, Tentacles)->slide_until, i);
}

/* ---------------------------------------------------------------- LookoutBlocker */
/* a box the lookout's view cannot enter (the lookout, towerviewer, finds it by its class name) */
static const EntClass LOOKOUT_BLOCKER = {.name = "lookoutBlocker"};
static void new_lookout_blocker(const EData *d) {
  Ent *e = ent_new(&LOOKOUT_BLOCKER, d->x, d->y);
  if (!e) return;
  ent_box(e, EA(d, lookoutBlocker, width), EA(d, lookoutBlocker, height), 0, 0);
  e->visible = 0;
}

/* DashBlock.Break, from entities.c */
void level_break_dash_block(Ent *e, V2 dir);
static void level_break_dash_block_ext(Ent *e, V2 dir) { level_break_dash_block(e, dir); }

/* ---------------------------------------------------------------- the story's shared pieces (badeline.h) */
Ent *scene_new(const EntClass *cls, void (*on_end)(Ent *e, bool skipped), bool fade_in_on_skip, bool ending_after) {
  Ent *e = ent_new(cls, 0, 0);
  if (!e) return NULL;
  e->collidable = 0;
  if (!cls->render) e->visible = 0;
  cutscene_start(e, on_end, fade_in_on_skip, ending_after);
  return e;
}
void player_dummy(Player *p) {
  player_set_state(p, ST_DUMMY);
  p->state_locked = true;
}
void player_free(Player *p) {
  p->state_locked = false;
  player_set_state(p, ST_NORMAL);
}
Co *ev_co(Co *c, int8_t *last, int index) {
  if (*last != index) {
    *last = (int8_t)index;
    memset(c, 0, sizeof *c);
  }
  return c;
}
void level_goto(const char *room) {
  int idx = chapter_find_room(g_level.chapter, room);
  if (idx >= 0) level_load_room(idx, INTRO_NONE);
}
void player_intro_none(Player *p) {
  if (p->state == ST_INTRORESPAWN) player_set_state(p, ST_NORMAL);
  p->ent->visible = 1;
}
void oshiro_sprite_update(Sprite *s, Wiggler *w) {
  int bank, idle;
  spr_update(s);
  if (textbox_portrait(&bank, &idle)) {
    bool side = bank == SB_portrait_oshiro && ((idle >= A_portrait_oshiro_idle_sidehappy && idle <= A_portrait_oshiro_talk_sideworried) ||
                                                (idle >= A_portrait_oshiro_idle_sidesuspicious && idle <= A_portrait_oshiro_talk_sidesuspicious));
    int to = side && s->anim == A_oshiro_idle ? A_oshiro_side : !side && s->anim == A_oshiro_side ? A_oshiro_idle : -1;
    if (to >= 0) {   /* Pop(name, flip: true) */
      spr_play(s, to, false);
      s->sx = -s->sx;
      wiggler_restart(w);
    }
  }
  if (w->active) {
    wiggler_update(w);
    s->sx = sgn(s->sx) * (1 + w->value * 0.2f), s->sy = 1 - w->value * 0.2f;
  }
}
void fade_wipe(bool in, float duration, WipeDone done) {
  wipe_start(WIPE_FADE, in, done);
  g_wipe.duration = duration;
}
void sprite_set_frame(Sprite *s, int frame) {   /* Sprite.SetAnimationFrame */
  int n = spr_frames(s);
  if (s->anim == 255 || n <= 0) return;
  s->frame = (uint8_t)(frame % n);
  s->timer = 0;
}

/* PlayerHair.AfterUpdate with its default steps */
void hair_follow(Hair *h, const Sprite *s, V2 render_pos, int facing) {
  int hx, hy, fr, carry;
  bool has;
  V2 *n = h->n;
  hair_meta(spr_tex(s), &hx, &hy, &fr, &has, &carry);
  n[0] = v2(render_pos.x + hx * facing, render_pos.y - 9 * s->sy + hy);
  V2 target = v2add(n[0], v2(-facing * 0.5f * 2, 2)), prev = n[0];
  for (int i = 1; i < 4; i++) {
    n[i] = approach_v(n[i], target, (1 - i / 4.f * 0.5f) * 64 * DT);
    if (v2len(v2sub(n[i], prev)) > 3) n[i] = v2add(prev, safe_norm(v2sub(n[i], prev), 3));
    target = v2add(n[i], v2(-facing * 0.5f, 2));
    prev = n[i];
  }
}
/* the nodes after the bangs: characters/player/hair00 (a disc in 10x10, its rows' first and last pixels) scaled,
 * drawn in one layer (scaled textures are few a frame) */
static const uint8_t HAIR_LO[10] = {10, 3, 2, 1, 1, 1, 1, 2, 3, 10}, HAIR_HI[10] = {0, 6, 7, 8, 8, 8, 8, 7, 6, 0};
static void hair_strip(uint16_t *strip, int y0, int y1, void *ctx) {
  const Hair *h = ctx;
  int a = h->alpha >= 255 ? 256 : h->alpha;
  for (int pass = 0; pass < 2; pass++)   /* the outlines (nodes 1 to 3), then the nodes back to front */
    for (int k = 1; k < 4; k++) {
      int i = pass ? 4 - k : k;
      float num = 0.25f + (1 - i / 4.f) * 0.75f, sx = num * h->sx;
      float px = floorf(h->n[i].x + 0.5f) - g_camx, py = floorf(h->n[i].y + 0.5f) - g_camy;
      for (int d = pass ? 4 : 0; d < (pass ? 5 : 4); d++) {
        float ox = px + (d == 0 ? -1 : d == 1 ? 1 : 0), oy = py + (d == 2 ? -1 : d == 3 ? 1 : 0);
        for (int y = (int)floorf(oy - 5 * num); y <= (int)ceilf(oy + 5 * num); y++) {
          int v = (int)floorf((y + 0.5f - oy) / num + 5);
          if (y < y0 || y >= y1 || v < 0 || v > 9) continue;
          for (int x = (int)floorf(ox - 5 * sx); x <= (int)ceilf(ox + 5 * sx); x++) {
            int u = (int)floorf((x + 0.5f - ox) / sx + 5);
            if (u >= HAIR_LO[v] && u <= HAIR_HI[v]) px_blend(strip, y0, y1, x, y, pass ? h->color : 0, a);
          }
        }
      }
    }
}
/* PlayerHair.Render: the outline, then the nodes back to front */
void hair_draw(Hair *h, const Sprite *s, int facing, uint16_t color, float alpha) {
  int hx, hy, fr, carry;
  bool has;
  hair_meta(spr_tex(s), &hx, &hy, &fr, &has, &carry);
  uint8_t a = a8(alpha);
  if (!has || !a || !s->sx) return;
  static const uint16_t BANGS[3] = {T_characters_player_bangs00, T_characters_player_bangs01, T_characters_player_bangs02};
  uint16_t t = BANGS[fr < 3 ? fr : 0];
  float x = floorf(h->n[0].x), y = floorf(h->n[0].y), sx = facing * fabsf(s->sx);
  h->sx = fabsf(s->sx), h->color = color, h->alpha = a;
  for (int d = 0; d < 4; d++)
    gfx_tex_ex(t, x + (d == 0 ? -1 : d == 1 ? 1 : 0), y + (d == 2 ? -1 : d == 3 ? 1 : 0), 5, 5, sx, 1, 0, 0, a, GF_SILHOUETTE);
  float top = h->n[0].y, bot = top;
  for (int i = 1; i < 4; i++) top = fminf(top, h->n[i].y), bot = fmaxf(bot, h->n[i].y);
  gfx_custom(hair_strip, h, (int)floorf(top) - g_camy - 7, (int)ceilf(bot) - g_camy + 7);
  gfx_tex_ex(t, x, y, 5, 5, sx, 1, 0, color, a, GF_SILHOUETTE);
}

/* ---------------------------------------------------------------- BadelineDummy */
static void bd_step(Ent *e, BDummy *b) {   /* FloatTo / WalkTo, an update at a time */
  V2 at = v2(e->x, e->y);
  if (b->walking) {
    if (b->job.step == 1) {
      b->floatness = 0;
      spr_play(&b->spr, A_badeline_walk, false);
      if (sgn(b->target.x - e->x) != 0) b->spr.sx = (float)sgn(b->target.x - e->x);
      b->job.step = 2;
    }
    if (e->x != b->target.x) {
      e->x = approach(e->x, b->target.x, DT * b->walk_speed);
      return;
    }
    spr_play(&b->spr, A_badeline_idle, false);
    job_end(&b->job);
    return;
  }
  switch (b->job.step) {
    case 1: {
      spr_play(&b->spr, A_badeline_fallSlow, false);
      if (b->face && sgn(b->target.x - e->x) != 0) b->spr.sx = (float)sgn(b->target.x - e->x);
      V2 v = safe_norm(v2sub(b->target, at), 1);
      b->perp = v2(-v.y, v.x);
      b->speed = 0;
      b->job.step = 2;
    }
      /* fall through */
    case 2:
      if (!v2eq(at, b->target)) {
        b->speed = approach(b->speed, b->float_speed, b->float_accel * DT);
        at = approach_v(at, b->target, b->speed * DT);
        e->x = at.x, e->y = at.y;
        b->floatness = approach(b->floatness, 4, 8 * DT);
        b->float_normal = approach_v(b->float_normal, b->perp, DT * 12);
        if (b->fade_light) b->light_alpha = approach(b->light_alpha, 0, DT * 2);
        return;
      }
      if (b->quick_end) b->floatness = 2;
      b->job.step = 3;
      /* fall through */
    default:
      if (b->floatness != 2) {
        b->floatness = approach(b->floatness, 2, 8 * DT);
        return;
      }
      if (b->turn_at_end) b->spr.sx = b->turn_at_end;
      job_end(&b->job);
  }
}
/* BadelineAutoAnimator: laughs or yells with Badeline's portrait (its pop: the wiggler) */
static void autoanim(Sprite *spr, Wiggler *pop, int8_t *last, uint8_t *syncing, bool enabled) {
  int bank, idle;
  bool flag = false;
  if (enabled && textbox_portrait(&bank, &idle) && bank == SB_portrait_badeline) {
    if (idle == A_portrait_badeline_idle_scoff) {
      if (!*syncing) *last = (int8_t)(spr->anim == 255 ? -1 : spr->anim);
      spr_play(spr, A_badeline_laugh, false);
      *syncing = 1, flag = 1;
    } else if (idle == A_portrait_badeline_idle_yell || idle == A_portrait_badeline_idle_freakA ||
               idle == A_portrait_badeline_idle_freakB || idle == A_portrait_badeline_idle_freakC) {
      if (!*syncing) {
        wiggler_restart(pop);
        *last = (int8_t)(spr->anim == 255 ? -1 : spr->anim);
      }
      spr_play(spr, A_badeline_angry, false);
      *syncing = 1, flag = 1;
    }
  }
  if (*syncing && !flag) {
    *syncing = 0;
    if (*last < 0 || *last == A_badeline_spin) *last = A_badeline_fallSlow;
    if (spr->anim == A_badeline_angry) wiggler_restart(pop);
    spr_play(spr, *last, false);
  }
}
static void bd_autoanim(BDummy *b) { autoanim(&b->spr, &b->pop, &b->last_anim, &b->syncing, b->auto_enabled); }
static void bd_update(Ent *e) {
  BDummy *b = BD(e);
  if (b->spr.sx != 0) b->hair_facing = (int8_t)sgn(b->spr.sx);
  spr_update(&b->spr);
  bd_autoanim(b);
  if (b->pop.active) {
    wiggler_update(&b->pop);
    float s = 1 + 0.25f * b->pop.value;
    b->spr.sx = sgn(b->spr.sx) * s, b->spr.sy = s;
  }
  sine_update(&b->wave);
  b->spr_pos = v2mul(b->float_normal, b->wave.value * b->floatness);
  if (job_due(&b->job)) bd_step(e, b);
  hair_follow(&b->hair, &b->spr, v2add(v2(e->x, e->y), b->spr_pos), b->hair_facing);
}
static void bd_render(Ent *e) {
  BDummy *b = BD(e);
  hair_draw(&b->hair, &b->spr, b->hair_facing, rgb(0x9B3FB5), b->hair_alpha);
  spr_draw(&b->spr, floorf(e->x + b->spr_pos.x), floorf(e->y + b->spr_pos.y));
  if (!b->no_light) light_add(e->x, e->y - 8, 0xDB7093, b->light_alpha, 20, 60);   /* Color.PaleVioletRed */
}
static const EntClass BDUMMY = {.name = "badelineDummy", .size = sizeof(BDummy), .update = bd_update, .render = bd_render};
Ent *bd_new(V2 at) {
  Ent *e = ent_new(&BDUMMY, at.x, at.y);
  if (!e) return NULL;
  ent_box(e, 6, 6, -3, -7);
  e->collidable = 0;
  BDummy *b = BD(e);
  spr_init(&b->spr, SB_badeline);
  spr_play(&b->spr, A_badeline_fallSlow, false);
  b->spr.sx = -1;
  b->hair_facing = -1;
  for (int i = 0; i < 4; i++) b->hair.n[i] = v2(at.x, at.y);
  b->float_speed = 120, b->float_accel = 240, b->floatness = 2;
  b->float_normal = v2(0, 1);
  b->wave.freq = 0.25f;
  b->light_alpha = b->hair_alpha = 1;
  b->auto_enabled = 1;
  b->last_anim = -1;
  wiggler_init(&b->pop, 0.5f, 4);
  return e;
}
void bd_appear(Ent *e) {   /* the displacement burst is not drawn */
  particles_emit(PL_MID, &P_BadelineOldsite_P_Vanish, 12, v2(e->x, e->y - 4), v2(6, 6), P_BadelineOldsite_P_Vanish.direction);
}
void bd_vanish(Ent *e) {
  bd_appear(e);
  ent_remove(e);
}
void bd_float_to(Ent *e, V2 target, int turn_at_end, bool face, bool fade_light, bool quick_end) {
  BDummy *b = BD(e);
  b->target = target, b->turn_at_end = (int8_t)turn_at_end, b->face = face, b->fade_light = fade_light;
  b->quick_end = quick_end, b->walking = 0;
  job_start(&b->job);
}
void bd_walk_to(Ent *e, float x, float speed) {
  BDummy *b = BD(e);
  b->target.x = x, b->walk_speed = speed, b->walking = 1;
  job_start(&b->job);
}
void bd_set_frame(Ent *e, int frame) { sprite_set_frame(&BD(e)->spr, frame); }

/* ---------------------------------------------------------------- NPC */
static void npc_move_step(Ent *e, Npc *n) {   /* MoveTo, an update at a time */
  switch (n->mv.step) {
    case 1:
      if (n->mv_remove) e->tags |= TAG_TRANSITION_UPDATE;
      if (sgn(n->mv_target.x - e->x) != 0) n->spr.sx = (float)sgn(n->mv_target.x - e->x);
      n->mv_alpha = n->mv_fade ? 0 : 1;
      if (n->move_anim != 255) spr_play(&n->spr, n->move_anim, false);
      n->mv_speed = 0;
      n->mv.step = 2;
      /* fall through */
    case 2:
      if (n->move_y ? !v2eq(v2(e->x, e->y), n->mv_target) : e->x != n->mv_target.x) {
        n->mv_speed = approach(n->mv_speed, n->maxspeed, 160 * DT);
        if (n->move_y) {
          V2 at = approach_v(v2(e->x, e->y), n->mv_target, n->mv_speed * DT);
          e->x = at.x, e->y = at.y;
        } else
          e->x = approach(e->x, n->mv_target.x, n->mv_speed * DT);
        n->spr.alpha = a8(n->mv_alpha);
        n->mv_alpha = approach(n->mv_alpha, 1, DT);
        return;
      }
      if (n->idle_anim != 255) spr_play(&n->spr, n->idle_anim, false);
      n->mv.step = 3;
      /* fall through */
    case 3:
      if (n->mv_alpha < 1) {
        n->spr.alpha = a8(n->mv_alpha);
        n->mv_alpha = approach(n->mv_alpha, 1, DT);
        return;
      }
      if (n->mv_turn) n->spr.sx = n->mv_turn;
      if (n->mv_remove) ent_remove(e);
      n->mv.step = 4;
      return;   /* yield return null */
    default: job_end(&n->mv);
  }
}
static void npc_update(Ent *e) {
  Npc *n = NPC_(e);
  spr_update(&n->spr);
  if (n->has_talk && talk_update(&n->talk, e) && n->on_talk) n->on_talk(e);
  n = NPC_(e);
  if (job_due(&n->mv)) npc_move_step(e, n);
  if (n->has_light) {
    Room *rm = g_level.room;
    bool in = e->x > rm->x - 16 && e->y > rm->y - 16 && e->x < rm->x + rm->w + 16 && e->y < rm->y + rm->h + 16;
    n->light_alpha = approach(n->light_alpha, in && !g_level.transitioning ? 1 : 0, DT * 2);
  }
  if (n->gravity) {   /* NPC06_Theo_Plateau and _Ending: falling to the ground */
    if (!collide_solid(e, e->x, e->y + 1)) n->speed_y += 400 * DT, e->y += n->speed_y * DT;
    else n->speed_y = 0;
  }
  if (n->haha) hahaha_enable(&g_ents[n->haha - 1], n->spr.anim == A_granny_laugh);
  if (n->think) n->think(e);
}
static void npc_render(Ent *e) {
  Npc *n = NPC_(e);
  if (n->paint) {
    n->paint(e);
    return;
  }
  spr_draw(&n->spr, e->x, e->y);
  if (n->has_light) light_add(e->x, e->y + n->light_dy, n->light_color, n->light_alpha, n->light_r0, n->light_r1);
  if (n->has_talk) talk_render(&n->talk, e);
}
static const EntClass NPC = {.name = "npc", .size = sizeof(Npc), .update = npc_update, .render = npc_render,
                             .removed = talk_removed};
Ent *npc_new(float x, float y, int bank, int anim, void (*think)(Ent *e)) {
  Ent *e = ent_new(&NPC, x, y);
  if (!e) return NULL;
  e->depth = 1000;
  ent_box(e, 8, 8, -4, -8);
  Npc *n = NPC_(e);
  spr_init(&n->spr, bank);
  if (anim >= 0) spr_play(&n->spr, anim, false);
  n->maxspeed = 80;
  n->move_y = 1;
  n->idle_anim = n->move_anim = 255;
  n->think = think;
  return e;
}
void npc_light(Ent *e, int dy, uint32_t color, int r0, int r1) {
  Npc *n = NPC_(e);
  n->has_light = 1, n->light_dy = (int8_t)dy, n->light_color = color, n->light_r0 = (uint8_t)r0, n->light_r1 = (uint8_t)r1;
  n->light_alpha = 1;
}
void npc_talker(Ent *e, int x, int y, int w, int h, V2 draw_at, void (*on_talk)(Ent *e)) {
  Npc *n = NPC_(e);
  talk_init(&n->talk, x, y, w, h, draw_at);
  n->has_talk = 1, n->on_talk = on_talk;
}
void npc_haha(Ent *e) {
  Ent *h = hahaha_new(v2(e->x + 8, e->y - 4), NULL);
  if (!h) return;
  hahaha_enable(h, false);
  NPC_(e)->haha = (uint16_t)(h - g_ents + 1);
}
void npc_move_to(Ent *e, V2 target, bool fade_in, int turn_at_end, bool remove_at_end) {
  Npc *n = NPC_(e);
  n->mv_target = target, n->mv_fade = fade_in, n->mv_turn = (int8_t)turn_at_end, n->mv_remove = remove_at_end;
  job_start(&n->mv);
}
Ent *npc_find(int which) {
  for (int i = 0; i < g_nents; i++)
    if (g_ents[i].cls == &NPC && g_ents[i].dead != 1 && NPC_(&g_ents[i])->which == which) return &g_ents[i];
  return NULL;
}

/* ================================================================ the story */
/* this chapter's cutscenes: one class, its routine in `run`; Add(new Coroutine(...)) of a zoom or a camera move
 * goes on beside it (from the update after: 2, or from an event: 1) */
typedef struct {
  Cutscene cs;
  void (*run)(Ent *e), (*draw)(Ent *e);
  bool (*evs)(void *s, Co *c, int i);   /* its textbox events (a nested routine each) */
  bool (*par)(void *s, Co *c);          /* the routine beside it */
  Ent *tb, *a, *b, *bd;        /* who it is about: the boss, Granny, Badeline crying, Theo, a BadelineDummy */
  Walk walk;
  Step step, cam, zoom;
  Co ev, pco;                  /* a textbox event's, and the routine beside the cutscene's */
  V2 cam_to, zoom_at, v[3];
  float (*cam_ease)(float);
  float cam_dur, cam_delay, zoom_dur, f[8];
  int8_t ev_i;
  uint8_t cam_on, zoom_on, par_on, i, j, k;
} S6;
#define S6_(e) ST(e, S6)
static void s6_render(Ent *e) {
  if (S6_(e)->draw) S6_(e)->draw(e);
}
static void s6_update(Ent *e);
static const EntClass SCENE6 = {.name = "cutscene6", .size = sizeof(S6), .update = s6_update, .render = s6_render};
static Ent *s6_new(void (*run)(Ent *e), void (*on_end)(Ent *e, bool skipped), bool fade_in_on_skip, bool ending_after) {
  Ent *e = scene_new(&SCENE6, on_end, fade_in_on_skip, ending_after);
  if (e) S6_(e)->run = run, S6_(e)->ev_i = -1;
  return e;
}
static void s6_update(Ent *e) {
  S6 *s = S6_(e);
  s->run(e);
  if (e->dead == 1) return;
  s = S6_(e);
  if (s->zoom_on == 2) s->zoom_on = 1;
  else if (s->zoom_on && !zoom_to(&s->zoom, s->zoom_at, 2, s->zoom_dur)) s->zoom_on = 0;
  if (s->cam_on == 2) s->cam_on = 1;
  else if (s->cam_on && (s->cam_delay > 0 ? (s->cam_delay -= DT, true) : camera_to(&s->cam, s->cam_to, s->cam_dur, s->cam_ease)) == false)
    s->cam_on = 0;
  if (s->par_on == 2) s->par_on = 1;
  else if (s->par_on && !s->par(s, &s->pco)) s->par_on = 0;
}
/* Add(new Coroutine(Level.ZoomTo(at, 2, duration))) and Add(new Coroutine(CameraTo(to, duration, ease, delay))) */
static void s6_zoom(S6 *s, V2 at, float duration, bool from_event) {
  step_reset(&s->zoom);
  s->zoom_at = at, s->zoom_dur = duration, s->zoom_on = from_event ? 1 : 2;
}
static void s6_cam(S6 *s, V2 to, float duration, float (*ease)(float), float delay, bool from_event) {
  step_reset(&s->cam);
  s->cam_to = to, s->cam_dur = duration, s->cam_ease = ease, s->cam_delay = delay, s->cam_on = from_event ? 1 : 2;
}
static bool s6_ev(void *ctx, int i) {
  S6 *s = S6_((Ent *)ctx);
  return !s->evs(s, ev_co(&s->ev, &s->ev_i, i), i);
}
static float room_bottom(void) { return (float)(g_level.room->y + g_level.room->h); }
static bool menu_confirm(void) { return btn_pressed(&g_in.confirm) || btn_pressed(&g_in.jump); }
static void player_drop_to_ground(Player *p) {   /* while (!OnGround() && Y < Bounds.Bottom) Y++ */
  while (!actor_on_ground(p->ent, 1) && p->ent->y < room_bottom()) p->ent->y++;
}

/* ---------------------------------------------------------------- CS06_BossIntro, CS06_BossMid */
static bool bintro_nb(void *ctx, Co *c, int i) {
  S6 *s = ctx;
  Ent *boss = s->a;
  Boss *b = ST(boss, Boss);
  if (i == 0) {   /* BadelineFloat */
    NB_BEGIN(c);
    s6_zoom(s, v2(170, 110), 1, true);
    b->sitting = 0;
    cs_play(&b->spr, A_badeline_fallSlow, false);
    b->normal_sx = -1;
    b->autoanim = 1, b->last_anim = -1;
    s->f[1] = boss->y;
    for (s->f[2] = 0; s->f[2] < 1; s->f[2] += DT * 4) {
      boss->y = lerpf(s->f[1], s->f[0], ease_cube_inout(s->f[2]));
      NB_YIELD(c);
    }
    NB_END(c);
  }
  NB_BEGIN(c);   /* PlayerStepForward */
  NB_WALK(c, s->walk, walk_exact((int)g_player.ent->x + 8));
  NB_END(c);
}
static void bintro_run(Ent *e) {
  S6 *s = S6_(e);
  Player *p = &g_player;
  Co *c = &s->cs.co;
  CO_BEGIN(c);
  player_dummy(p);
  while (!p->dead && !actor_on_ground(p->ent, 1)) CO_YIELD(c);
  while (p->dead) CO_YIELD(c);
  p->facing = 1;
  s6_cam(s, v2((p->ent->x + s->a->x) / 2 - 160, room_bottom() - 180), 1, NULL, 0, false);
  CO_WAIT(c, 0.5f);
  if (!p->dead) CO_WALK(c, s->walk, walk_exact((int)(s->f[3] - 8)));
  p->facing = 1;
  CO_SAY(c, s->tb, "ch6_boss_start", s6_ev, e);
  CO_ZOOM_BACK(c, &s->step, 0.5f);
  cutscene_end(e);
  return;
  CO_END(c);
}
static void bintro_end(Ent *e, bool skipped) {
  S6 *s = S6_(e);
  Player *p = &g_player;
  if (skipped) {
    p->ent->x = s->f[3];
    player_drop_to_ground(p);
  }
  player_free(p);
  Boss *b = ST(s->a, Boss);
  s->a->y = s->f[0];
  if (b->normal) {
    b->normal_sx = -1;
    cs_play(&b->spr, A_badeline_laugh, false);
  }
  b->sitting = 0;
  b->autoanim = 0;   /* boss.Remove(animator) */
  level_set_flag("boss_intro", true);
}
static void bmid_run(Ent *e) {
  S6 *s = S6_(e);
  Player *p = &g_player;
  Co *c = &s->cs.co;
  CO_BEGIN(c);
  do CO_YIELD(c);
  while (!level_player());
  player_dummy(p);
  while (!actor_on_ground(p->ent, 1)) CO_YIELD(c);
  CO_WALK(c, s->walk, walk_exact((int)p->ent->x + 20));
  CO_ZOOM_TO(c, &s->step, v2(80, 110), 2, 0.5f);
  CO_SAY(c, s->tb, "ch6_boss_middle", NULL, NULL);
  CO_WAIT(c, 0.1f);
  CO_ZOOM_BACK(c, &s->step, 0.4f);
  cutscene_end(e);
  return;
  CO_END(c);
}
static void bmid_end(Ent *e, bool skipped) {
  Player *p = level_player();
  (void)e;
  if (p) {
    if (skipped) player_drop_to_ground(p);
    player_free(p);
  }
  Ent *boss = first_of(&BOSS);
  if (boss) boss_hit(boss, NULL);
  level_set_flag("boss_mid", true);
}
static void boss_mid_new(void) { s6_new(bmid_run, bmid_end, true, false); }

/* ---------------------------------------------------------------- CS06_Reflection */
static void reflect6_run(Ent *e) {
  S6 *s = S6_(e);
  Player *p = &g_player;
  Co *c = &s->cs.co;
  CO_BEGIN(c);
  player_dummy(p);
  p->force_camera_update = true;
  CO_WALK(c, s->walk, walk_exact((int)s->f[0]));
  CO_WAIT(c, 0.1f);
  p->facing = 1;
  CO_WAIT(c, 0.1f);
  CO_ZOOM_TO(c, &s->step, v2(200, 90), 2, 1);
  CO_SAY(c, s->tb, "CH6_REFLECT_AFTER", NULL, NULL);
  CO_ZOOM_BACK(c, &s->step, 0.5f);
  cutscene_end(e);
  return;
  CO_END(c);
}
static void reflect6_end(Ent *e, bool skipped) {
  (void)e, (void)skipped;
  player_free(&g_player);
  g_player.force_camera_update = false;
  level_set_flag("reflection", true);
}

/* ---------------------------------------------------------------- NPC06_Granny and CS06_Granny */
static bool granny6_nb(void *ctx, Co *c, int i) {
  S6 *s = ctx;
  Player *p = &g_player;
  Npc *g = NPC_(s->a);
  NB_BEGIN(c);
  if (i == 0) {   /* ZoomIn */
    s->v[0] = v2add(v2sub(v2lerp(v2(s->a->x, s->a->y), v2(p->ent->x, p->ent->y), 0.5f), g_level.cam), v2(0, -20));
    NB_ZOOM_TO(c, &s->step, s->v[0], 2, 0.5f);
  } else if (i == 1) {   /* Laughs */
    if (s->j) {
      s->j = 0;
      NB_WAIT(c, 0.5f);
    }
    spr_play(&g->spr, A_granny_laugh, false);
    NB_WAIT(c, 1);
  } else if (i == 2) {   /* StopLaughing */
    spr_play(&g->spr, A_granny_idle, false);
    NB_WAIT(c, 0.25f);
  } else if (i == 3 || i == 4) {   /* MaddyWalksRight, MaddyWalksLeft */
    NB_WAIT(c, 0.1f);
    p->facing = i == 3 ? 1 : -1;
    NB_WALK(c, s->walk, walk_exact((int)p->ent->x + p->facing * 8));
    NB_WAIT(c, 0.1f);
  } else if (i == 5)   /* WaitABit */
    NB_WAIT(c, 0.8f);
  else {   /* MaddyTurnsRight */
    NB_WAIT(c, 0.1f);
    p->facing = 1;
    NB_WAIT(c, 0.1f);
  }
  NB_END(c);
}
static void granny6_run(Ent *e) {
  S6 *s = S6_(e);
  Player *p = &g_player;
  Co *c = &s->cs.co;
  CO_BEGIN(c);
  player_dummy(p);
  p->force_camera_update = true;
  if (s->i == 0) {
    CO_WALK(c, s->walk, walk_to(s->a->x - 40));
    p->facing = 1;
    s->j = 1;   /* firstLaugh */
    CO_SAY(c, s->tb, "ch6_oldlady", s6_ev, e);
  } else {
    s->v[0] = v2add(v2sub(v2lerp(v2(s->a->x, s->a->y), v2(p->ent->x, p->ent->y), 0.5f), g_level.cam), v2(0, -20));
    CO_ZOOM_TO(c, &s->step, s->v[0], 2, 0.5f);
    CO_WALK(c, s->walk, walk_to(s->a->x - 20));
    p->facing = 1;
    CO_SAY(c, s->tb, s->i == 1 ? "ch6_oldlady_b" : "ch6_oldlady_c", NULL, NULL);
  }
  CO_ZOOM_BACK(c, &s->step, 0.5f);
  cutscene_end(e);
  return;
  CO_END(c);
}
static void granny6_end(Ent *e, bool skipped) {
  S6 *s = S6_(e);
  char f[12] = "granny_0";
  (void)skipped;
  player_free(&g_player);
  g_player.force_camera_update = false;
  spr_play(&NPC_(s->a)->spr, A_granny_idle, false);
  f[7] = (char)('0' + s->i);
  level_set_flag(f, true);
}
static void granny6_talk(Ent *e) {   /* OnTalk */
  Npc *n = NPC_(e);
  Ent *cs = s6_new(granny6_run, granny6_end, true, false);
  if (cs) S6_(cs)->a = e, S6_(cs)->i = (uint8_t)n->i[0], S6_(cs)->evs = granny6_nb;
  n->i[0]++;
  n->talk.enabled = n->i[0] > 0 && n->i[0] < 3;
}
static void granny6_think(Ent *e) {
  Player *p = level_player();
  if (NPC_(e)->i[0] == 0 && p && p->ent->x > e->x - 60) granny6_talk(e);
}
static void new_granny6(const EData *d) {
  Ent *e = npc_new(d->x, d->y, SB_granny, A_granny_idle, granny6_think);
  if (!e) return;
  Npc *n = NPC_(e);
  n->which = NPC_GRANNY06;
  n->spr.sx = -1;
  npc_haha(e);
  char f[12] = "granny_0";
  while (n->i[0] < 9 && (f[7] = (char)('0' + n->i[0]), level_get_flag(f))) n->i[0]++;
  npc_talker(e, -20, -8, 30, 8, v2(0, -24), granny6_talk);
  n->talk.enabled = n->i[0] > 0 && n->i[0] < 3;
}

/* ---------------------------------------------------------------- NPC06_Badeline_Crying, its orbs, CS06_BossEnd */
typedef struct { V2 target, speed, aim, from; float ease, reset, p, offset; uint8_t mode, aiming, index; } COrb;
static float ease_bigback_in(float t) { return t * t * (4 * t - 3); }
static void corb_update(Ent *e) {
  COrb *o = ST(e, COrb);
  Player *p = level_player();
  if (o->mode == 0) {   /* FloatRoutine */
    if (!o->aiming) o->aim = v2add(o->target, angle_vec(rndf() * 2 * PI_F, 16 + rndf() * 40)), o->reset = 0, o->aiming = 1;
    V2 d = v2sub(o->aim, v2(e->x, e->y));
    if (o->reset < 1 && v2len(d) > 8) {
      o->speed = v2add(o->speed, v2mul(safe_norm(d, 1), 420 * DT));
      if (v2len(o->speed) > 90) o->speed = safe_norm(o->speed, 90);
      e->x += o->speed.x * DT, e->y += o->speed.y * DT;
      o->reset += DT;
      o->ease = approach(o->ease, 1, DT * 4);
    } else
      o->aiming = 0;
  } else if (o->mode == 1 && p) {   /* CircleRoutine */
    V2 to = v2add(player_center(p), angle_vec(g_level.time_active * 2 + o->offset, 24));
    o->p = approach(o->p, 1, DT * 2);
    V2 at = v2add(o->from, v2mul(v2sub(to, o->from), ease_cube_inout(o->p)));
    e->x = at.x, e->y = at.y;
  } else if (o->mode == 2 && o->p < 1) {   /* AbsorbRoutine */
    float k = ease_bigback_in(o->p);
    V2 at = v2add(o->from, v2mul(v2sub(o->aim, o->from), k));
    e->x = at.x, e->y = at.y;
    o->ease = 0.2f + (1 - k) * 0.8f;
    o->p += DT;
  }
}
static void corb_render(Ent *e) {
  float k = ST(e, COrb)->ease;
  draw_centered(T_characters_badeline_orb, e->x, e->y, 0xFFFF, 255, k, k, 0);
}
static const EntClass CORB = {.name = "badelineOrb", .size = sizeof(COrb), .update = corb_update, .render = corb_render};
static void corb_routine(int mode, int i) {   /* Routine.Replace(CircleRoutine(i / 8 * 2pi)) or (AbsorbRoutine()) */
  Player *p = level_player();
  for (int k = 0; k < g_nents; k++) {
    Ent *e = &g_ents[k];
    COrb *o = ST(e, COrb);
    if (e->cls != &CORB || e->dead == 1 || (mode == 1 && o->index != i)) continue;
    o->mode = (uint8_t)(p ? mode : 3), o->from = v2(e->x, e->y), o->p = 0;
    o->offset = i / 8.f * 2 * PI_F;
    if (p) o->aim = player_center(p);
  }
}
static void bossend_start(Ent *b);
/* the crying Badeline: f[0] white alpha, f[1] its size, i[0] started */
static void crying_think(Ent *e) {
  Npc *n = NPC_(e);
  Player *p = level_player();
  if (!n->i[1]) {   /* Awake: after the connection, she is gone, with the tentacles */
    n->i[1] = 1;
    if (level_get_flag("badeline_connection")) {
      for (int i = 0; i < g_nents; i++)
        if (g_ents[i].cls == &TENTACLES) ent_remove(&g_ents[i]);
      ent_remove(e);
      return;
    }
  }
  if (!n->i[0] && p && p->ent->x > e->x - 32) {
    bossend_start(e);
    n->i[0] = 1;
  }
}
static void crying_paint(Ent *e) {
  Npc *n = NPC_(e);
  if (!n->i[2]) spr_draw(&n->spr, e->x, e->y);   /* Sprite.Visible = false once she is white */
  Tex t;
  if (n->f[0] > 0 && tex_get(T_characters_badelineBoss_calm_white, &t))   /* white: Sprite's origin and place */
    gfx_tex_ex(T_characters_badelineBoss_calm_white, e->x, e->y, t.fw * t.scale * 0.5f, t.fh * t.scale * 0.64f, n->f[1], n->f[1], 0,
               0xFFFF, a8(n->f[0]), 0);
  light_add(e->x, e->y - 6, 0xFFFFFF, n->f[1], 24, 64);
}
static void new_crying(const EData *d) {
  Ent *e = npc_new(d->x, d->y, SB_badeline_boss, A_badeline_boss_scaredIdle, crying_think);
  if (!e) return;
  NPC_(e)->f[1] = 1;
  NPC_(e)->paint = crying_paint;
}
static bool bossend_nb(void *ctx, Co *c, int i) {
  S6 *s = ctx;
  Player *p = &g_player;
  Ent *bd = s->a;
  NB_BEGIN(c);
  if (i == 0) NB_WAIT(c, 0.5f);   /* StartMusic */
  else if (i == 1) {   /* PlayerHug */
    s6_zoom(s, v2sub(v2add(v2(bd->x, bd->y + bd->cy + bd->ch / 2), v2(0, -24)), g_level.cam), 0.5f, true);
    NB_WAIT(c, 0.6f);
    NB_WALK(c, s->walk, walk_exact((int)bd->x - 10));
    p->facing = 1;
    NB_WAIT(c, 0.25f);
    p->dummy_auto_animate = false;
    spr_play(&p->spr, A_player_hug, false);
    NB_WAIT(c, 0.5f);
  } else {   /* BadelineCalmDown */
    NB_WAIT(c, 0.5f);
    spr_play(&NPC_(bd)->spr, A_badeline_boss_scaredTransition, false);
    /* FinalBossStarfield's alpha goes down (the backdrop is not drawn here: its second) */
    for (s->f[5] = 1; s->f[5] > 0; s->f[5] -= DT) NB_YIELD(c);
    NB_WAIT(c, 1.5f);
  }
  NB_END(c);
}
/* Disperse(): f[1] size, k orbs */
static bool disperse_nb(S6 *s, Co *c) {
  Ent *bd = s->a;
  Npc *n = NPC_(bd);
  NB_BEGIN(c);
  s->k = 0;
  while (s->k < 8) {
    for (s->f[6] = n->f[1] - 0.125f; n->f[1] > s->f[6]; n->f[1] -= DT) NB_YIELD(c);
    Ent *o = ent_new(&CORB, bd->x, bd->y);
    if (o) {
      o->depth = -10001, o->collidable = 0;
      ST(o, COrb)->target = v2(bd->x - 16, bd->y - 40), ST(o, COrb)->ease = 0.2f, ST(o, COrb)->index = s->k;
    }
    s->k++;
  }
  NB_WAIT(c, 3.25f);
  for (s->k = 0; s->k < 8; s->k++) {
    corb_routine(1, s->k);
    NB_WAIT(c, 0.2f);
  }
  NB_WAIT(c, 2);
  corb_routine(2, 0);
  NB_WAIT(c, 1);
  NB_END(c);
}
/* LevelUpEffect */
static void levelup_update(Ent *e) {
  Sprite *s = ST(e, Sprite);
  spr_update(s);
  if (s->anim == 255) ent_remove(e);
}
static void levelup_render(Ent *e) { spr_draw(ST(e, Sprite), e->x, e->y); }
static const EntClass LEVELUP = {.name = "levelUpEffect", .size = sizeof(Sprite), .update = levelup_update, .render = levelup_render};
static void levelup_new(V2 at) {
  Ent *e = ent_new(&LEVELUP, at.x, at.y);
  if (!e) return;
  e->depth = -1000000, e->collidable = 0;
  spr_init(ST(e, Sprite), SB_player_level_up);
  spr_play(ST(e, Sprite), A_player_level_up_levelUp, true);
}
static void crying_paint(Ent *e);
static void crying_remove(Ent *bd) {   /* badeline.RemoveSelf(), with her orbs */
  if (!bd || bd->dead == 1 || bd->cls != &NPC || NPC_(bd)->paint != crying_paint) return;
  for (int i = 0; i < g_nents; i++)
    if (g_ents[i].cls == &CORB) ent_remove(&g_ents[i]);
  ent_remove(bd);
}
static bool bossend_par(void *ctx, Co *c) {   /* CenterCameraOnPlayer */
  S6 *s = ctx;
  NB_BEGIN(c);
  NB_WAIT(c, 0.5f);
  s->v[1] = g_level.zoom_focus;
  s->v[2] = v2sub(v2((float)g_level.room->x + 580, (float)g_level.room->y + 124), g_level.cam);
  for (s->f[7] = 0; s->f[7] < 1; s->f[7] += DT) {
    g_level.zoom_focus = v2add(s->v[1], v2mul(v2sub(s->v[2], s->v[1]), ease_sine_inout(s->f[7])));
    NB_YIELD(c);
  }
  NB_END(c);
}
/* f[0] fade, f[1] picture fade, f[2] picture glow, f[3] timer, f[4] white alpha; i: the picture (1, 2), j: waiting */
static bool pic_fade(S6 *s, float to, float duration) { return (s->f[1] = approach(s->f[1], to, DT / duration)) != to; }
static void bossend_run(Ent *e) {
  S6 *s = S6_(e);
  Player *p = &g_player;
  Co *c = &s->cs.co;
  s->f[3] += DT;
  if (s->f[4] > 0 && s->f[4] < 1 && s->a->dead != 1) {   /* badeline.TurnWhite(1) */
    s->f[4] += DT;
    NPC_(s->a)->f[0] = fminf(s->f[4], 1);
    if (s->f[4] >= 1) NPC_(s->a)->i[2] = 1;
  }
  CO_BEGIN(c);
  player_dummy(p);
  while (!actor_on_ground(p->ent, 1)) CO_YIELD(c);
  p->facing = 1;
  CO_WAIT(c, 1);
  CO_SAY(c, s->tb, "ch6_boss_ending", s6_ev, e);
  CO_WAIT(c, 0.5f);
  while ((s->f[0] += DT) < 1) CO_YIELD(c);
  s->i = 1;
  while (pic_fade(s, 1, 1)) CO_YIELD(c);
  s->j = 1;   /* WaitForPress */
  while (!menu_confirm()) CO_YIELD(c);
  s->j = 0;
  while (pic_fade(s, 0, 0.5f)) CO_YIELD(c);
  s->i = 2;
  while (pic_fade(s, 1, 1)) CO_YIELD(c);
  s->j = 1;   /* WaitForPress */
  while (!menu_confirm()) CO_YIELD(c);
  s->j = 0;
  while ((s->f[2] += DT / 2) < 1) CO_YIELD(c);
  CO_WAIT(c, 0.2f);
  while (pic_fade(s, 0, 0.5f)) CO_YIELD(c);
  while ((s->f[0] -= DT * 12) > 0) CO_YIELD(c);
  s->f[4] = 0.0001f;   /* Add(new Coroutine(badeline.TurnWhite(1))) */
  CO_WAIT(c, 0.5f);
  spr_play(&p->spr, A_player_idle, false);
  CO_WAIT(c, 0.25f);
  CO_WALK(c, s->walk, walk_back((int)p->ent->x - 8));
  memset(&s->pco, 0, sizeof s->pco);
  s->par_on = 2;   /* Add(new Coroutine(CenterCameraOnPlayer())) */
  memset(&s->ev, 0, sizeof s->ev);
  CO_NEST(c, disperse_nb(s, &s->ev));
  level_set_flag("badeline_connection", true);
  level_flash(0xFFFF, false);
  g_session.inv_dashes = 2;
  p->inventory_dashes = 2;
  crying_remove(s->a);
  CO_WAIT(c, 0.1f);
  levelup_new(v2(p->ent->x, p->ent->y));
  CO_WAIT(c, 2);
  CO_ZOOM_BACK(c, &s->step, 0.5f);
  cutscene_end(e);
  return;
  CO_END(c);
}
static void bossend_draw(Ent *e) {
  S6 *s = S6_(e);
  if (s->f[0] <= 0 || g_level.paused) return;
  gfx_hud(true);
  gfx_rect(-2, -2, 324, 184, 0, a8(ease_cube_out(fminf(s->f[0], 1)) * 0.8f));
  if (s->i && s->f[1] > 0) {   /* Portraits "hug1", "hug2" and the glow */
    float num = ease_cube_out(s->f[1]), k = 1 + (1 - num) * 0.025f, glow = ease_cube_out(s->f[1] * s->f[2]);
    static const uint16_t PICS[5] = {T__hug1, T__hug2, T__hug_light2a, T__hug_light2b, T__hug_light2c};
    for (int i = s->i - 1; i < 5; i = i < 2 ? 2 : i + 1) {
      if (i >= 2 && s->f[2] <= 0) break;
      uint16_t t = PICS[i];
      Tex tx;
      if (!tex_get(t, &tx)) continue;
      for (int add = 0; add < (i < 2 ? 1 : 2); add++)   /* the glow: once, then once more added */
        gfx_tex_ex(t, 160, 90, tx.fw * tx.scale / 2.f, tx.fh * tx.scale / 2.f, k, k, 0, 0xFFFF, a8(i < 2 ? num : glow), add ? GF_ADD : 0);
    }
    Tex t;
    if (s->j && tex_get(T__textboxbutton, &t))
      gfx_tex_ex(T__textboxbutton, 1520 / 6.f, (880 + (fmodf(s->f[3], 1) < 0.25f ? 6 : 0)) / 6.f, t.fw / 2.f, t.fh / 2.f, 1, 1, 0, 0xFFFF,
                 255, 0);
  }
  gfx_hud(false);
}
static void bossend_end(Ent *e, bool skipped) {
  S6 *s = S6_(e);
  Player *p = &g_player;
  zoom_reset();
  g_session.inv_dashes = 2;
  p->inventory_dashes = 2;
  if (skipped) levelup_new(v2(p->ent->x, p->ent->y));
  p->dummy_auto_animate = true;
  player_free(p);
  crying_remove(s->a);
  level_set_flag("badeline_connection", true);
}
static void bossend_start(Ent *b) {
  Ent *e = s6_new(bossend_run, bossend_end, true, false);
  if (!e) return;
  e->tags |= TAG_HUD;
  S6_(e)->a = b, S6_(e)->draw = bossend_draw, S6_(e)->evs = bossend_nb, S6_(e)->par = bossend_par;
}

/* ---------------------------------------------------------------- NPC06_Theo_Ending, NPC06_Granny_Ending, CS06_Ending */
static void theo6_think(Ent *e) {   /* the yolo alarm: yoloEnd after 0.8 s */
  Npc *n = NPC_(e);
  if (n->f[0] > 0 && (n->f[0] -= DT) <= 0) spr_play(&n->spr, A_theo_yoloEnd, false);
}
static void ending6_end(Ent *e, bool skipped) {
  (void)e, (void)skipped;
  level_complete_area(true, false);
  if (g_wipe.active && g_wipe.type == WIPE_SPOTLIGHT) g_wipe.focus.y -= 20;
}

static bool ending6_nb(void *ctx, Co *c, int i) {
  S6 *s = ctx;
  Player *p = &g_player;
  Ent *bd = s->bd, *granny = s->a, *theo = s->b;
  NB_BEGIN(c);
  if (i == 0) {   /* GrannyEnter */
    NB_WAIT(c, 0.25f);
    BD(bd)->spr.sx = 1;
    NB_WAIT(c, 0.1f);
    granny->visible = 1;
    bd_float_to(bd, v2(bd->x - 10, bd->y), 1, false, false, false);
    npc_move_to(granny, v2(p->ent->x + 40, p->ent->y), false, 0, false);
    NB_AWAIT(c, &NPC_(granny)->mv);
  } else if (i == 1) {   /* TheoEnter */
    p->facing = -1;
    BD(bd)->spr.sx = -1;
    NB_WAIT(c, 0.25f);
    s->v[0] = v2add(g_level.cam, v2(-40, 0));
    step_reset(&s->step);
    NB_NEST(c, camera_to(&s->step, s->v[0], 1, NULL));
    theo->visible = 1;
    s6_cam(s, v2add(g_level.cam, v2(40, 0)), 2, NULL, 1, true);
    bd_float_to(bd, v2(bd->x + 6, bd->y + 4), -1, false, false, false);
    npc_move_to(theo, v2(p->ent->x - 32, p->ent->y), false, 0, false);
    NB_AWAIT(c, &NPC_(theo)->mv);
    spr_play(&NPC_(theo)->spr, A_theo_tired, false);
  } else if (i == 2) {   /* MaddyTurnsRight */
    NB_WAIT(c, 0.1f);
    p->facing = 1;
    NB_WAIT(c, 0.1f);
    bd_float_to(bd, v2(bd->x - 2, bd->y + 10), -1, false, false, false);
    NB_AWAIT(c, &BD(bd)->job);
    NB_WAIT(c, 0.1f);
  } else if (i == 3 || i == 4) {   /* BadelineTurnsRight, BadelineTurnsLeft */
    NB_WAIT(c, 0.1f);
    BD(bd)->spr.sx = i == 3 ? 1 : -1;
    NB_WAIT(c, 0.1f);
  } else if (i == 5)   /* WaitAbit */
    NB_WAIT(c, 0.4f);
  else if (i == 6) {   /* TurnToLeft */
    NB_WAIT(c, 0.1f);
    p->facing = -1;
    NB_WAIT(c, 0.05f);
    BD(bd)->spr.sx = -1;
    NB_WAIT(c, 0.1f);
  } else {   /* TheoRaiseFist, TheoStopTired */
    spr_play(&NPC_(theo)->spr, i == 7 ? A_theo_yolo : A_theo_idle, false);
    if (i == 7) NPC_(theo)->f[0] = 0.8f;
    NB_YIELD(c);
  }
  NB_END(c);
}
static void ending6_run(Ent *e) {
  S6 *s = S6_(e);
  Player *p = &g_player;
  Co *c = &s->cs.co;
  CO_BEGIN(c);
  player_dummy(p);
  CO_WAIT(c, 1);
  p->dashes = p->inventory_dashes = 1;
  g_session.inv_dashes = 1;
  s->bd = bd_new(player_center(p));
  if (!s->bd || !s->b) {
    cutscene_end(e);
    return;
  }
  bd_appear(s->bd);
  BD(s->bd)->float_speed = 80;
  BD(s->bd)->spr.sx = -1;
  bd_float_to(s->bd, v2(p->ent->x + 24, p->ent->y - 20), -1, false, false, false);
  CO_AWAIT(c, &BD(s->bd)->job);
  CO_ZOOM_TO(c, &s->step, v2(160, 120), 2, 1);
  CO_SAY(c, s->tb, "ch6_ending", s6_ev, e);
  p->dummy_auto_animate = false;
  spr_play(&p->spr, A_player_bagdown, false);
  cutscene_end(e);
  return;
  CO_END(c);
}
static void granny6e_think(Ent *e) {
  Npc *n = NPC_(e);
  Player *p = level_player();
  if (n->i[0] || !p || !actor_on_ground(p->ent, 1)) return;
  n->i[0] = 1;   /* talked */
  Ent *cs = s6_new(ending6_run, ending6_end, false, true);
  if (!cs) return;
  level_register_complete();
  S6_(cs)->a = e, S6_(cs)->b = npc_find(NPC_THEO06_ENDING), S6_(cs)->evs = ending6_nb;
}
static void new_ending_npc(const EData *d, bool theo) {
  Ent *e = npc_new(d->x, d->y, theo ? SB_theo : SB_granny, theo ? A_theo_idle : A_granny_idle, theo ? theo6_think : granny6e_think);
  if (!e) return;
  Npc *n = NPC_(e);
  e->visible = 0;
  n->which = theo ? NPC_THEO06_ENDING : NPC_GRANNY06_ENDING;
  n->idle_anim = theo ? A_theo_idle : A_granny_idle;
  n->move_anim = theo ? A_theo_run : A_granny_walk;
  n->maxspeed = theo ? 72 : 30;
  if (theo) n->move_y = 0, n->gravity = 1;
  else n->spr.sx = -1;
  npc_light(e, -8, 0xFFFFFF, 16, 32);
}

/* ---------------------------------------------------------------- the dream: Bonfire, StarJumpController */
static uint8_t room_hook;   /* the next room is room 04, after the dream */
static Ent *bonfire_find(void) {   /* the map's bonfire (src/entities.c: a sprite of the campfire bank) */
  for (int i = 0; i < g_nents; i++) {
    Ent *e = &g_ents[i];
    if (e->cls && e->dead != 1 && e->cls->name && !strcmp(e->cls->name, "sprite") && ST(e, Sprite)->bank == SB_campfire) return e;
  }
  return NULL;
}
static void bonfire_mode(Ent *b, int mode, bool activated) {   /* Bonfire.SetMode: 0 unlit, 1 lit, 2 smoking */
  if (!b) return;
  bool dream = g_session.dreaming;
  spr_play(ST(b, Sprite), mode == 2   ? A_campfire_smoking
                          : mode == 0 ? A_campfire_idle
                          : activated ? (dream ? A_campfire_startDream : A_campfire_start)
                                      : (dream ? A_campfire_burnDream : A_campfire_burn),
           false);
}
/* StarJumpController: the star blocks' fill (rays drifting up, the same from a hash of each ray and its
 * cycle: no table) and the camera kept up while flying */
typedef struct { float t, cam_timer; } SJCtrl;
static void sjctrl_update(Ent *e) {
  SJCtrl *k = ST(e, SJCtrl);
  Player *p = level_player();
  k->t += DT;
  if (!p) return;
  float *oy = &g_level.cam_offset.y;
  if (*oy == -38.4f) {
    if (p->state != ST_STARFLY) {
      if ((k->cam_timer += DT) >= 0.5f) k->cam_timer = 0, *oy = -12.8f;
    } else
      k->cam_timer = 0;
  } else if (p->state == ST_STARFLY) {
    if ((k->cam_timer += DT) >= 0.1f) k->cam_timer = 0, *oy = -38.4f;
  } else
    k->cam_timer = 0;
}
static float fmodp(float x, float m) { return fmodf(fmodf(x, m) + m, m); }
/* the fill: Color.Lerp(Black, LightSkyBlue, 0.3), then 100 rays (a3ffff * 0.25), in every star block crossing the
 * rows: one layer for them all (a frame has 16) */
#define SJ_RECTS 24
static void sjfill_strip(uint16_t *strip, int y0, int y1, void *ctx) {
  const Ent *ce = ctx;
  int16_t r[SJ_RECTS][4];
  int nr = 0, ya = y1, yb = y0;
  for (int i = 0; i < g_nents && nr < SJ_RECTS; i++) {   /* the blocks' rectangles in these rows, filled */
    const Ent *b = &g_ents[i];
    if (b->cls != &STARJUMP || b->dead == 1) continue;
    int bx0 = (int)floorf(b->x) - g_camx, by0 = (int)floorf(b->y) - g_camy, bx1 = bx0 + (int)b->cw, by1 = by0 + (int)b->ch;
    int16_t *q = r[nr];
    q[0] = (int16_t)(bx0 > 0 ? bx0 : 0), q[2] = (int16_t)(bx1 < VIEW_W ? bx1 : VIEW_W);
    q[1] = (int16_t)(by0 > y0 ? by0 : y0), q[3] = (int16_t)(by1 < y1 ? by1 : y1);
    if (q[1] >= q[3] || q[0] >= q[2]) continue;
    for (int y = q[1]; y < q[3]; y++)
      for (int x = q[0]; x < q[2]; x++) strip[(y - y0) * VIEW_W + x] = rgb(0x283D4B);
    if (q[1] < ya) ya = q[1];
    if (q[3] > yb) yb = q[3];
    nr++;
  }
  float t = ST(ce, SJCtrl)->t;
  const float vx = -0.09983342f, vy = -0.99500417f;   /* AngleToVector(-1.6707964) */
  for (int i = 0; i < 100 && nr; i++) {
    float dur = 4 + hash01(666, (uint32_t)i * 8) * 8, c = t / dur + hash01(666, (uint32_t)i * 8 + 1);
    uint32_t cyc = (uint32_t)c, h = (uint32_t)i * 8 + cyc * 1024;
    float pct = c - cyc, e = ease_cube_inout(yoyo(pct));
    int a = (int)(0.25f * e * 256);
    if (a <= 0) continue;
    float w = 8 + floorf(hash01(667, h) * 72), len = 20 + floorf(hash01(668, h) * 180);
    float rx = hash01(669, h) * 320, ry = hash01(670, h) * 580 + 8 * pct * dur;
    float px = floorf(fmodp(rx - g_level.cam.x * 0.9f, 320)), py = floorf(-200 + fmodp(ry - g_level.cam.y * 0.7f, 580));
    /* the two triangles: the parallelogram p - w2, p - w2 - l, p + w2, p + w2 + l (w2 = (-vy, vx) * w, l = v * len) */
    V2 q[4] = {v2(px + vy * w, py - vx * w), v2(px + vy * w - vx * len, py - vx * w - vy * len), v2(px - vy * w, py + vx * w),
               v2(px - vy * w + vx * len, py + vx * w + vy * len)};
    for (int y = ya; y < yb; y++) {
      float yc = y + 0.5f, xl = 1e9f, xr = -1e9f;
      for (int k = 0; k < 4; k++) {
        V2 p = q[k], o = q[(k + 1) & 3];
        if ((p.y <= yc) == (o.y <= yc)) continue;
        float x = p.x + (yc - p.y) * (o.x - p.x) / (o.y - p.y);
        xl = fminf(xl, x), xr = fmaxf(xr, x);
      }
      if (xl > xr) continue;
      int x0 = (int)ceilf(xl - 0.5f), x1 = (int)ceilf(xr - 0.5f);
      for (int k = 0; k < nr; k++) {
        if (y < r[k][1] || y >= r[k][3]) continue;
        for (int x = x0 > r[k][0] ? x0 : r[k][0]; x < x1 && x < r[k][2]; x++) px_blend(strip, y0, y1, x, y, rgb(0xA3FFFF), a);
      }
    }
  }
}

/* drawn just behind the blocks (depth -9999), before their edges */
static void sjctrl_render(Ent *e) { gfx_custom(sjfill_strip, e, 0, VIEW_H); }
static const EntClass SJCTRL = {.name = "starJumpController", .size = sizeof(SJCtrl), .update = sjctrl_update, .render = sjctrl_render};

/* ---------------------------------------------------------------- NPC06_Theo_Plateau and CS06_Campfire */
enum { Q_OUTFOR, Q_TEMPLE, Q_EXPLAIN, Q_THANKYOU, Q_WHY, Q_DEPRESSION, Q_DEFENSE, Q_VACATION, Q_TRUST, Q_FAMILY, Q_GRANDPA, Q_TIPS,
       Q_SELFIE, Q_SLEEP, Q_SLEEP_CONFIRM, Q_SLEEP_CANCEL };
static const char *const CAMP_Q[16] = {"outfor", "temple", "explain", "thankyou", "why", "depression", "defense", "vacation",
                                       "trust", "family", "grandpa", "tips", "selfie", "sleep", "sleep_confirm", "sleep_cancel"};
/* each question's portrait (its ask's first): Theo's on the left, Madeline's on the right, and its idle animation */
#define PT(a) (0x80 | A_portrait_theo_idle_##a)
#define PM(a) (A_portrait_madeline_idle_##a)
static const uint8_t CAMP_PORT[16] = {PT(thinking), PT(wtf), PT(wtf), PM(distracted), PM(distracted), PT(serious), PT(wtf),
                                      PT(thinking), PM(distracted), PM(normal), PM(normal), PM(normal), PM(normal), PM(distracted),
                                      PM(sad), PM(normal)};
#define QB(n) (1u << Q_##n)
enum { GO_START, GO_SLEEP, GO_END, GO_REPEAT = 0x10 };
typedef struct { uint8_t q, go; uint16_t req, exc; } CampOpt;   /* Option: Require, ExcludedBy (all of them), Repeatable */
static const CampOpt CAMP_OPTS[16] = {
    {Q_OUTFOR, GO_START, 0, QB(WHY)}, {Q_TEMPLE, GO_START, QB(TRUST), 0}, {Q_TRUST, GO_START, QB(EXPLAIN), 0},
    {Q_FAMILY, GO_START, QB(TRUST) | QB(WHY), 0}, {Q_GRANDPA, GO_START, QB(FAMILY) | QB(DEFENSE), 0}, {Q_TIPS, GO_START, QB(GRANDPA), 0},
    {Q_EXPLAIN, GO_START, 0, 0}, {Q_THANKYOU, GO_START, QB(EXPLAIN), 0}, {Q_WHY, GO_START, QB(EXPLAIN) | QB(TRUST), 0},
    {Q_DEPRESSION, GO_START, QB(WHY), 0}, {Q_DEFENSE, GO_START, QB(DEPRESSION), 0}, {Q_VACATION, GO_START, QB(DEPRESSION), 0},
    {Q_SELFIE, GO_END, QB(DEFENSE) | QB(GRANDPA), 0}, {Q_SLEEP, GO_SLEEP | GO_REPEAT, QB(WHY), QB(DEFENSE) | QB(GRANDPA)},
    /* "sleep" */
    {Q_SLEEP_CANCEL, GO_START | GO_REPEAT, 0, 0}, {Q_SLEEP_CONFIRM, GO_END, 0, 0}};
#define CAMP_MAX 9
typedef struct {
  S6 s;                       /* a: Theo, b: the bonfire; v[0] cameraStart, v[1] Theo's and v[2] Madeline's places at the fire */
  Wiggler wig;                /* Theo waking up */
  V2 selfie_pos;
  float opt_ease, hl[CAMP_MAX], theo_alarm, selfie_p, selfie_rot, selfie_timer;
  uint16_t asked;
  uint8_t opts[CAMP_MAX], nopts, sel, node, selfie, selfie_wait, q;
  char key[24];
  char text[CAMP_MAX][56];    /* the asks, cleaned once */
  TextDraw td[CAMP_MAX];      /* (the long ones) */
} Camp;
#define CAMP(e) ST(e, Camp)
static bool camp_can_ask(const Camp *k, const CampOpt *o) {   /* Option.CanAsk */
  if (!(o->go & GO_REPEAT) && (k->asked >> o->q & 1)) return false;
  if ((k->asked & o->req) != o->req) return false;
  return !o->exc || (k->asked & o->exc) != o->exc;
}
static void camp_key(Camp *k, bool answer, int q) {   /* ch6_theo_ask_ / ch6_theo_say_ and the question */
  path2(k->key, answer ? "ch6_theo_say_" : "ch6_theo_ask_", CAMP_Q[q], -1);
}
/* an ask's room in its box (Option: 1400 wide, the portrait's 100 and the padding before the text) */
static float camp_pad(void) { return (140 - font_line_height(FONT_S) * 6 * 0.7f) / 2; }
static float camp_room(void) { return (1400 - 20 - camp_pad() - 100 - 20) / 6; }
/* the game's text is 0.7 of the font: a long ask (larger here) is drawn at 0.8 of it, on two lines if it still runs out
 * of its box */
static void camp_fit(char *s) {
  int n = (int)strlen(s);
  if (font_measure(s, n, FONT_S) * 0.8f <= camp_room()) return;
  int best = -1;
  for (int i = 0; i < n; i++)
    if (s[i] == ' ' && (best < 0 || (i > n / 2 ? i - n / 2 : n / 2 - i) < (best > n / 2 ? best - n / 2 : n / 2 - best))) best = i;
  if (best > 0) s[best] = '\n';
}
static void camp_options(Camp *k) {
  int from = k->node == GO_SLEEP ? 14 : 0, to = k->node == GO_SLEEP ? 16 : 14;
  k->nopts = 0, k->sel = 0;
  for (int i = from; i < to && k->nopts < CAMP_MAX; i++)
    if (camp_can_ask(k, &CAMP_OPTS[i])) {
      camp_key(k, false, CAMP_OPTS[i].q);
      dialog_clean(k->key, k->text[k->nopts], 56);
      camp_fit(k->text[k->nopts]);
      k->hl[k->nopts] = 0;
      k->opts[k->nopts++] = (uint8_t)i;
    }
}
static void camp_selfie_draw(Camp *k) {   /* Selfie: the picture (Portraits "selfieCampfire") */
  uint16_t t = T__selfieCampfire;
  Tex tx;
  float w = 0, h = 0;
  if (tex_get(t, &tx)) {
    w = tx.fw * tx.scale, h = tx.fh * tx.scale;
    gfx_tex_ex(t, k->selfie_pos.x / 6, k->selfie_pos.y / 6, w / 2, h / 2, 1, 1, k->selfie_rot, 0xFFFF, 255, 0);
  }
  if (k->selfie_wait && tex_get(T__textboxbutton, &tx))
    gfx_tex_ex(T__textboxbutton, k->selfie_pos.x / 6 + w / 2 + 40 / 6.f, k->selfie_pos.y / 6 + h / 2 + (fmodf(k->selfie_timer, 1) < 0.25f ? 1 : 0),
               tx.fw / 2.f, tx.fh / 2.f, 1, 1, 0, 0xFFFF, 255, 0);
}
static void camp_draw(Ent *e) {
  Camp *k = CAMP(e);
  if (g_level.paused) return;
  gfx_hud(true);
  if (k->selfie) camp_selfie_draw(k);
  for (int i = 0; i < k->nopts; i++) {   /* Option.Render at (260, 120 + 160 i) */
    int q = CAMP_OPTS[k->opts[i]].q, port = CAMP_PORT[q];
    bool right = !(port & 0x80);
    float num = ease_cube_out(clampf(k->opt_ease, 0, 1)), hh = ease_cube_inout(k->hl[i]);
    float x = 260 + hh * 32, y = 120 + 160.f * i - 32 * (1 - num);
    int g = (int)(128 + 127 * hh);
    uint16_t box = right ? T__textbox_madeline_ask : T__textbox_theo_ask;
    gfx_tex_ex(box, x / 6, y / 6, 0, 0, 1, 1, 0, rgb((uint32_t)(g << 16 | g << 8 | g)), a8(num), 0);
    Sprite s;   /* the portrait, 100 high: its idle animation's first frame (one texture each: the cache is small) */
    spr_init(&s, right ? SB_portrait_madeline : SB_portrait_theo);
    spr_play(&s, port & 0x7F, true);
    uint16_t pt = spr_tex(&s);
    Tex tx;
    int l = (int)(255 * (0.5f + hh * 0.5f));
    if (pt != 0xFFFF && tex_get(pt, &tx)) {
      float ox = s.justify ? tx.fw * tx.scale * s.jx : s.ox, oy = s.justify ? tx.fh * tx.scale * s.jy : s.oy;
      gfx_tex_ex(pt, (x + (right ? 1380 - 50 : 20 + 50)) / 6, (y + 70) / 6, ox, oy, 100 / 240.f, 100 / 240.f, 0,
                 rgb((uint32_t)(l << 16 | l << 8 | l)), a8(num), right ? GF_FLIPX : 0);
    }
    float pad = camp_pad(), at = right ? x + 1400 - 20 - pad - 100 : x + 20 + pad + 100;
    uint8_t ta = a8((0.6f + 0.4f * hh) * num);
    const char *t = k->text[i];
    if (strchr(t, '\n') || font_measure(t, (int)strlen(t), FONT_S) > camp_room()) {
      k->td[i] = (TextDraw){t, at, y + 70, right ? 1 : 0, 0.5f, 0.7f, 0xFFFF, ta};
      text_draw(&k->td[i]);
    } else
      font_draw_justified(t, at / 6, (y + 70) / 6, right ? 1 : 0, 0.5f, FONT_S, 0xFFFF, ta);
  }
  gfx_hud(false);
}
static bool camp_nb(void *ctx, Co *c, int i) {
  Camp *k = ctx;
  S6 *s = &k->s;
  Player *p = &g_player;
  Ent *theo = s->a;
  Npc *tn = NPC_(theo);
  NB_BEGIN(c);
  if (i == 0)   /* WaitABit */
    NB_WAIT(c, 0.8f);
  else if (i == 2)   /* BeerSequence */
    NB_WAIT(c, 0.5f);
  else {   /* SelfieSequence */
    s6_zoom(s, v2(160, 105), 0.5f, true);
    NB_WAIT(c, 0.1f);
    spr_play(&tn->spr, A_theo_idle, false);
    k->theo_alarm = 0.25f;   /* Alarm: Theo turns */
    p->dummy_auto_animate = true;
    NB_WALK(c, s->walk, walk_mult((int)(theo->x + 5), 0.7f));
    NB_WAIT(c, 0.2f);
    spr_play(&tn->spr, A_theo_takeSelfie, false);
    NB_WAIT(c, 1);
    level_flash(0xFFFF, false);   /* Selfie.PictureRoutine("selfieCampfire") */
    k->selfie = 1;
    NB_WAIT(c, 0.5f);
    for (k->selfie_p = 0; k->selfie_p < 1;) {   /* OpenRoutine */
      k->selfie_p += DT;
      k->selfie_pos = v2lerp(v2(992, 1080), v2(960, 540), ease_cube_out(k->selfie_p));
      k->selfie_rot = lerpf(0.5f, 0, ease_back_out(k->selfie_p));
      NB_YIELD(c);
    }
    k->selfie_wait = 1;   /* WaitForInput */
    while (!menu_confirm() && !btn_pressed(&g_in.back)) NB_YIELD(c);
    k->selfie_wait = 0;
    for (k->selfie_p = 0; k->selfie_p < 1;) {   /* EndRoutine */
      k->selfie_p += DT * 2;
      k->selfie_pos = v2lerp(v2(960, 540), v2(928, 0), ease_back_in(k->selfie_p));
      k->selfie_rot = lerpf(0, -0.15f, ease_back_in(k->selfie_p));
      NB_YIELD(c);
    }
    NB_YIELD(c);
    k->selfie = 0;
    NB_WAIT(c, 0.5f);
    step_reset(&s->step);
    NB_NEST(c, zoom_back(&s->step, 0.5f));
    NB_WAIT(c, 0.2f);
    tn->spr.sx = 1;
    NB_WALK(c, s->walk, walk_mult((int)s->v[2].x, 0.7f));
    spr_play(&tn->spr, A_theo_wakeup, false);
    NB_YIELD(c);   /* yield return 0.1 (a double): one update */
    p->dummy_auto_animate = false;
    p->facing = -1;
    spr_play(&p->spr, A_player_sleep, false);
    NB_WAIT(c, 2);
    spr_play(&p->spr, A_player_halfWakeUp, false);
  }
  NB_END(c);
}
static bool key_edge(uint32_t k) { return (g_in.keys & ~g_in.prev & k) != 0; }
static void camp_run(Ent *e) {
  Camp *k = CAMP(e);
  S6 *s = &k->s;
  Player *p = &g_player;
  Ent *theo = s->a;
  Npc *tn = NPC_(theo);
  Co *c = &s->cs.co;
  CO_BEGIN(c);
  CO_WAIT(c, 0.1f);
  /* PlayerLightApproach: the player's light is the engine's */
  s6_cam(s, v2(g_level.cam.x + 90, g_level.cam.y), 6, ease_cube_in, 0, false);
  p->dummy_auto_animate = false;
  spr_play(&p->spr, A_player_carryTheoWalk, false);
  for (s->f[0] = 0; s->f[0] < 3.5f; s->f[0] += DT) {
    g_wipe.focus = v2(40, 120);   /* SpotlightWipe.FocusPoint */
    p->ent->x += 32 * DT;         /* NaiveMove */
    CO_YIELD(c);
  }
  spr_play(&p->spr, A_player_carryTheoCollapse, false);
  CO_WAIT(c, 0.3f);
  particles_emit(PL_FG, &P_Payphone_P_Snow, 2, v2(p->ent->x + 16, p->ent->y + 1), v2(4, 0), P_Payphone_P_Snow.direction);
  particles_emit(PL_FG, &P_Payphone_P_SnowB, 12, v2(p->ent->x + 16, p->ent->y + 1), v2(10, 0), P_Payphone_P_SnowB.direction);
  CO_WAIT(c, 0.7f);
  fade_wipe(false, 1.5f, NULL);
  g_wipe.end_timer = 2.5f;
  CO_NEST(c, wipe_busy());
  bonfire_mode(s->b, 1, true);
  CO_WAIT(c, 2.45f);
  s->cam_on = 0;   /* camTo.Cancel() */
  theo->x = s->v[1].x, theo->y = s->v[1].y;
  spr_play(&tn->spr, A_theo_sleep, false);
  sprite_set_frame(&tn->spr, spr_frames(&tn->spr) - 1);
  p->ent->x = s->v[2].x, p->ent->y = s->v[2].y;
  p->facing = -1;
  spr_play(&p->spr, A_player_asleep, false);
  level_set_flag("starsbg", true);
  level_set_flag("duskbg", false);
  wipe_start(WIPE_FADE, true, NULL);   /* fade.EndTimer = 0; new FadeWipe(level, true) */
  CO_YIELD(c);
  zoom_reset();
  g_level.cam = v2(s->b->x - 160, s->b->y - 140);
  CO_WAIT(c, 3);
  CO_WAIT(c, 1.5f);
  wiggler_init(&k->wig, 0.6f, 3);
  wiggler_restart(&k->wig);
  particles_emit(PL_MID, &P_NPC01_Theo_P_YOLO, 4, v2(theo->x - 4, theo->y - 14), v2(3, 3), P_NPC01_Theo_P_YOLO.direction);
  CO_WAIT(c, 0.5f);
  spr_play(&tn->spr, A_theo_wakeup, false);
  CO_WAIT(c, 1);
  spr_play(&p->spr, A_player_halfWakeUp, false);
  CO_WAIT(c, 0.25f);
  CO_SAY(c, s->tb, "ch6_theo_intro", NULL, NULL);
  k->node = GO_START;
  while (k->node != GO_END) {
    camp_options(k);
    if (!k->nopts) break;
    while ((k->opt_ease += DT * 4) < 1) CO_YIELD(c);
    k->opt_ease = 1;
    CO_WAIT(c, 0.25f);
    while (!menu_confirm()) {
      if (key_edge(K_UP) && k->sel > 0) k->sel--;
      else if (key_edge(K_DOWN) && k->sel < k->nopts - 1) k->sel++;
      CO_YIELD(c);
    }
    while ((k->opt_ease -= DT * 4) > 0) CO_YIELD(c);
    {
      const CampOpt *o = &CAMP_OPTS[k->opts[k->sel]];
      k->asked |= (uint16_t)(1u << o->q);
      k->node = o->go & 15;
      k->nopts = 0;   /* currentOptions = null */
      camp_key(k, true, o->q);
    }
    CO_SAY(c, s->tb, k->key, s6_ev, e);
  }
  fade_wipe(false, 3, NULL);
  CO_NEST(c, wipe_busy());
  cutscene_end(e);
  return;
  CO_END(c);
}
static void camp_update(Ent *e) {
  Camp *k = CAMP(e);
  Npc *tn = NPC_(k->s.a);
  for (int i = 0; i < k->nopts; i++) k->hl[i] = approach(k->hl[i], i == k->sel ? 1 : 0, DT * 4);
  if (k->selfie_wait) k->selfie_timer += DT;
  if (k->theo_alarm > 0 && (k->theo_alarm -= DT) <= 0) tn->spr.sx = -1;
  if (k->wig.active) {
    wiggler_update(&k->wig);
    tn->spr.sx = tn->spr.sy = 1 + 0.1f * k->wig.value;
  }
  s6_update(e);
}
static const EntClass CAMPFIRE = {.name = "cutscene6", .size = sizeof(Camp), .update = camp_update, .render = s6_render};
static void sjend_wait_new(Ent *theo, V2 player_start, V2 cam_start, bool zoom_back);
static void camp_end(Ent *e, bool skipped) {
  Camp *k = CAMP(e);
  S6 *s = &k->s;
  Player *p = &g_player;
  Ent *theo = s->a;
  Npc *tn = NPC_(theo);
  Room *rm = g_level.room;
  if (!skipped) {   /* a 3 s fade in, the zoom going back with it */
    zoom_snap(v2(160, 120), 2);
    fade_wipe(true, 3, NULL);
  }
  k->selfie = 0;
  level_set_flag("campfire_chat", true);
  level_set_flag("starsbg", false);
  level_set_flag("duskbg", false);
  g_session.dreaming = 1;
  Ent *ce = ent_new(&SJCTRL, 0, 0);
  if (ce) ce->collidable = 0, ce->depth = -9999;
  sjend_wait_new(theo, s->v[2], s->v[0], !skipped);
  feather_new(rm->x + 272.f, rm->y + 2616.f, false, false);
  bonfire_mode(s->b, 1, false);
  spr_play(&tn->spr, A_theo_sleep, false);
  sprite_set_frame(&tn->spr, spr_frames(&tn->spr) - 1);
  tn->spr.sx = tn->spr.sy = 1;
  theo->x = s->v[1].x, theo->y = s->v[1].y;
  spr_play(&p->spr, A_player_asleep, false);
  p->ent->x = s->v[2].x, p->ent->y = s->v[2].y;
  p->state_locked = false;
  player_set_state(p, skipped ? ST_NORMAL : ST_INTROWAKEUP);
  p->speed = v2(0, 0);
  p->facing = -1;
  g_level.cam = player_camera_target(p);
}
static void camp_begin(Ent *theo) {   /* CS06_Campfire.OnBegin, from Theo's Awake */
  Player *p = level_player();
  Ent *bon = bonfire_find();
  if (!p || !bon) return;
  Ent *e = scene_new(&CAMPFIRE, camp_end, true, false);
  if (!e) return;
  e->tags |= TAG_HUD;
  Camp *k = CAMP(e);
  S6 *s = &k->s;
  Room *rm = g_level.room;
  s->run = camp_run, s->draw = camp_draw, s->evs = camp_nb, s->ev_i = -1;
  s->a = theo, s->b = bon;
  level_set_flag("duskbg", true);
  g_level.cam = v2((float)rm->x, bon->y - 144);
  zoom_snap(v2(80, 120), 2);
  s->v[0] = g_level.cam;
  theo->x = g_level.cam.x - 48;
  s->v[1] = v2(bon->x - 16, bon->y);
  p->ent->x = rm->x - 40.f;
  player_dummy(p);
  s->v[2] = v2(bon->x + 20, bon->y);
  if (level_get_flag("campfire_chat")) {
    k->s.cs.skipped = true;
    zoom_reset();
    cutscene_end(e);
  }
}
static void theo6p_think(Ent *e) {
  Npc *n = NPC_(e);
  if (!n->i[0] && level_player()) n->i[0] = 1, camp_begin(e);
}
static void new_theo6p(const EData *d) {
  Ent *e = npc_new(d->x, d->y, SB_theo, A_theo_idle, theo6p_think);
  if (!e) return;
  Npc *n = NPC_(e);
  n->which = NPC_THEO06_PLATEAU;
  n->idle_anim = A_theo_idle, n->move_anim = A_theo_walk, n->maxspeed = 48, n->move_y = 0, n->gravity = 1;
  npc_light(e, -6, 0xFFFFFF, 16, 48);
}

/* ---------------------------------------------------------------- CS06_StarJumpEnd */
/* S6: a Theo, bd Badeline, tb the textbox; v[0] playerStart, v[1] cameraStart, v[2] the cutscene's center;
 * f[0] maddySine, f[1] its target, f[2] its anchor, f[3] a loop's, f[4] the spin's timer; i: what goes beside it
 * (0 the camera, 1 the spin, 2 the circling), j: tentacles made, k: flags (1 waiting, 2 shaking, 4 zoom back,
 * 8 circling, 16 spinning, 32 flying down) */
typedef struct {
  S6 s;
  Ent *breath, *tent[4];
  V2 from, to, start, target, tw_from, tw_to;
  float tw;                   /* StartCirclingPlayer's Tween, -1 none */
} SJEnd;
#define SJE(e) ST(e, SJEnd)
static void tent_nodes(SJEnd *k, float y0, float y1, bool snap) {
  float cx = g_level.room->x + g_level.room->w / 2.f;
  for (int i = 0; i < k->s.j; i++) {
    Ent *t = k->tent[i];
    if (!t || t->cls != &TENTACLES || t->dead == 1) continue;
    Tentacles *tt = ST(t, Tentacles);
    if (snap) tt->index = 0;
    tt->nodes[0][0] = tt->nodes[1][0] = (int16_t)cx;
    tt->nodes[0][1] = (int16_t)(g_player.ent->y + y0), tt->nodes[1][1] = (int16_t)(g_player.ent->y + y1);
    if (snap) tent_snap(tt, NULL);
  }
}
static bool sjend_par(void *ctx, Co *c) {
  SJEnd *k = ctx;
  S6 *s = &k->s;
  Player *p = &g_player;
  Ent *bd = s->bd;
  if (s->i == 0) {   /* the two camera moves: (-160, -70) in 1.5 s, then (-160, -120) in 2 s */
    NB_BEGIN(c);
    step_reset(&s->cam);
    NB_NEST(c, camera_to(&s->cam, v2add(s->v[2], v2(-160, -70)), 1.5f, ease_cube_out));
    step_reset(&s->cam);
    NB_NEST(c, camera_to(&s->cam, v2add(s->v[2], v2(-160, -120)), 2, ease_cube_inout));
    NB_END(c);
  }
  if (s->i == 1) {   /* SpinCharacters */
    NB_BEGIN(c);
    k->start = v2(p->ent->x, p->ent->y), k->target = v2(bd->x, bd->y);
    k->from = v2mul(v2add(k->start, k->target), 0.5f);
    s->f[5] = fabsf(k->start.x - k->from.x);
    s->f[4] = PI_F / 2;
    spr_play(&p->spr, A_player_spin, false);
    spr_play(&BD(bd)->spr, A_badeline_spin, false);
    BD(bd)->spr.sx = 1;
    while (s->k & 16) {
      int n = (int)(s->f[4] / (PI_F * 2) * 14 + 10);
      sprite_set_frame(&p->spr, n);
      bd_set_frame(bd, n + 7);
      float sn = sinf(s->f[4]), cs = cosf(s->f[4]);
      p->ent->x = k->from.x - sn * s->f[5], p->ent->y = k->from.y - cs * 8;
      bd->x = k->from.x + sn * s->f[5], bd->y = k->from.y + cs * 8;
      s->f[4] += DT * 2;
      NB_YIELD(c);
    }
    p->facing = 1;
    spr_play(&p->spr, A_player_fallSlow, false);
    BD(bd)->spr.sx = -1;
    spr_play(&BD(bd)->spr, A_badeline_angry, false);
    BD(bd)->auto_enabled = 0;
    k->from = v2(p->ent->x, p->ent->y), k->to = v2(bd->x, bd->y);
    for (s->f[4] = 0; s->f[4] < 1; s->f[4] += DT * 3) {
      V2 a = v2lerp(k->from, k->start, ease_cube_out(s->f[4])), b = v2lerp(k->to, k->target, ease_cube_out(s->f[4]));
      p->ent->x = a.x, p->ent->y = a.y, bd->x = b.x, bd->y = b.y;
      NB_YIELD(c);
    }
    NB_END(c);
  }
  NB_BEGIN(c);   /* BadelineCirclePlayer */
  s->f[4] = 0;
  s->f[5] = v2len(v2sub(v2(bd->x, bd->y), v2(p->ent->x, p->ent->y)));
  s->k |= 8;
  while (s->k & 8) {
    s->f[4] -= DT * 4;
    s->f[5] = approach(s->f[5], 24, DT * 32);
    {
      V2 at = v2add(v2(p->ent->x, p->ent->y), angle_vec(s->f[4], s->f[5]));
      bd->x = at.x, bd->y = at.y;
      int sg = sgn(p->ent->x - bd->x);
      if (sg) BD(bd)->spr.sx = (float)sg;
      if ((int)((g_level.time_active - DT) / 0.1f) < (int)(g_level.time_active / 0.1f)) {   /* OnInterval: TrailManager.Add */
        Sprite *sp = &BD(bd)->spr;
        trail_add(floorf(bd->x), floorf(bd->y), spr_tex(sp), sp->ox, sp->oy, sp->sx, sp->sy, 0xAC3232, 1, bd->depth + 1);
      }
    }
    NB_YIELD(c);
  }
  BD(bd)->spr.sx = -1;
  bd_float_to(bd, v2(p->ent->x + 40, p->ent->y - 16), -1, false, false, false);
  NB_AWAIT(c, &BD(bd)->job);
  NB_END(c);
}
static bool sjend_nb(void *ctx, Co *c, int i) {
  SJEnd *k = ctx;
  S6 *s = &k->s;
  Player *p = &g_player;
  Ent *bd = s->bd;
  NB_BEGIN(c);
  if (i == 0) {   /* TentaclesAppear (the northern lights going down are the backdrop's) */
    level_shake(0.3f);
    if (s->j < 4) {
      int16_t nodes[2][2] = {{(int16_t)(g_level.cam.x + 160), (int16_t)(g_level.cam.y + 400)},
                             {(int16_t)(g_level.cam.x + 160), (int16_t)(g_level.cam.y + 600)}};
      Ent *t = tent_create(nodes, 2, 0, 0, s->j);
      if (t) {
        Tentacles *tt = ST(t, Tentacles);
        tt->nodes[0][1] = (int16_t)(g_level.cam.y + 140);
      }
      k->tent[s->j++] = t;
    }
    s->k &= (uint8_t)~16;   /* charactersSpinning = false */
    spr_play(&BD(bd)->spr, A_badeline_angry, false);
    s->f[1] = 1;
    NB_YIELD(c);
  } else if (i == 1) {   /* TentaclesGrab (BreathingRumbler: rumble only) */
    s->f[1] = 0;
    spr_play(&p->spr, A_player_tentacle_grab, false);
    NB_WAIT(c, 0.1f);
    level_shake(0.3f);
  } else if (i == 2) {   /* FeatherMinigame */
    k->breath = breath_new(false);
    while (!breath_done(k->breath) && !breath_pausing(k->breath)) NB_YIELD(c);
  } else if (i == 3) {   /* EndFeatherMinigame */
    s->k &= (uint8_t)~8;
    breath_resume(k->breath);
    while (!breath_done(k->breath)) NB_YIELD(c);
    k->breath = NULL;
  } else {   /* StartCirclingPlayer */
    memset(&s->pco, 0, sizeof s->pco);
    s->i = 2, s->par_on = 1;
    k->tw_from = v2(p->ent->x, p->ent->y);   /* Tween.Set(0.5, CubeOut) */
    k->tw_to = v2(g_level.room->x + g_level.room->w / 2.f, p->ent->y);
    k->tw = 0;
    NB_YIELD(c);
  }
  NB_END(c);
}
static void sjend_run(Ent *e) {
  SJEnd *k = SJE(e);
  S6 *s = &k->s;
  Player *p = &g_player;
  Room *rm = g_level.room;
  Co *c = &s->cs.co;
  CO_BEGIN(c);
  {
    Ent *ce = first_of(&SJCTRL);
    if (ce) ent_remove(ce);
    for (int i = 0; i < g_nents; i++)
      if (g_ents[i].cls == &STARJUMP) g_ents[i].collidable = 0;
  }
  s->v[2] = v2(rm->x + 160.f, rm->y + 150.f);
  g_level.cam_offset.y = -30;
  memset(&s->pco, 0, sizeof s->pco);
  s->i = 0, s->par_on = 2;   /* the camera moves (NorthernLights' OffsetY tween is the backdrop's) */
  p->dashes = 0;
  player_set_state(p, ST_DUMMY);
  p->dummy_gravity = false;
  p->dummy_auto_animate = false;
  spr_play(&p->spr, A_player_fallSlow, false);
  p->dashes = 1;
  p->speed = v2(0, -80);
  p->facing = 1;
  p->force_camera_update = false;
  while (v2len(p->speed) > 0 || !v2eq(v2(p->ent->x, p->ent->y), s->v[2])) {
    p->speed = approach_v(p->speed, v2(0, 0), 200 * DT);
    {
      V2 at = approach_v(v2(p->ent->x, p->ent->y), s->v[2], 64 * DT);
      p->ent->x = at.x, p->ent->y = at.y;
    }
    CO_YIELD(c);
  }
  spr_play(&p->spr, A_player_spin, false);
  CO_WAIT(c, 3.5f);
  p->facing = 1;
  s->bd = bd_new(v2(p->ent->x, p->ent->y));
  if (!s->bd) {
    cutscene_end(e);
    return;
  }
  particles_emit(PL_MID, &P_Player_P_Split, 16, player_center(p), v2(6, 6), P_Player_P_Split.direction);   /* CreateSplitParticles */
  BD(s->bd)->spr.sx = -1;
  k->start = v2(p->ent->x, p->ent->y);
  k->target = v2add(s->v[2], v2(-30, 0));
  s->f[2] = s->v[2].y;
  for (s->f[3] = 0; s->f[3] <= 1; s->f[3] += 2 * DT) {
    CO_YIELD(c);
    if (s->f[3] > 1) s->f[3] = 1;
    {
      V2 a = v2lerp(k->start, k->target, ease_cube_out(s->f[3]));
      p->ent->x = a.x, p->ent->y = a.y;
      s->bd->x = s->v[2].x + (s->v[2].x - a.x), s->bd->y = a.y;
    }
  }
  s->k |= 16;   /* SpinCharacters */
  memset(&s->pco, 0, sizeof s->pco);
  s->i = 1, s->par_on = 2;
  CO_WAIT(c, 1);
  CO_SAY(c, s->tb, "ch6_dreaming", s6_ev, e);
  /* BadelineFlyDown */
  spr_play(&BD(s->bd)->spr, A_badeline_fallFast, false);
  BD(s->bd)->float_speed = 600, BD(s->bd)->float_accel = 1200;
  bd_float_to(s->bd, v2(s->bd->x, g_level.cam.y + 200), 0, true, true, false);
  s->k |= 32;
  CO_WAIT(c, 0.7f);
  for (int i = 0; i < g_nents; i++) {
    Ent *o = &g_ents[i];
    if (o->cls == &FEATHER || o->cls == &STARJUMP || (o->cls && o->cls->name && !strcmp(o->cls->name, "jumpThru"))) ent_remove(o);
  }
  level_shake(0.3f);
  g_level.cam_offset.y = 0;
  spr_play(&p->spr, A_player_tentacle_pull, false);
  p->speed.y = 160;
  /* FallEffects is not in the engine */
  for (s->f[3] = 0; s->f[3] < 1; s->f[3] += DT / 3) {
    p->speed.y += DT * 100;
    p->ent->x = clampf(p->ent->x, rm->x + 32.f, rm->x + rm->w - 32.f);
    if (s->f[3] > 0.7f) g_level.cam_offset.y -= 100 * DT;
    tent_nodes(k, 300, 600, false);
    CO_YIELD(c);
  }
  level_flash(0xFFFF, false);   /* (the bloom is not drawn) */
  g_session.dreaming = 0;
  g_level.cam_offset.y = 0;
  g_level.cam = s->v[1];
  bonfire_mode(bonfire_find(), 2, true);
  {
    Ent *pl = first_of(&PLATEAU);
    if (pl) pl->depth = (int16_t)(p->ent->depth + 10);
  }
  p->ent->x = s->v[0].x, p->ent->y = s->v[0].y + 8;
  p->speed = v2(0, 0);
  spr_play(&p->spr, A_player_tentacle_dangling, false);
  p->facing = -1;
  if (s->a) {
    s->a->x -= 24;
    spr_play(&NPC_(s->a)->spr, A_theo_alert, false);
  }
  tent_nodes(k, 32, 400, true);
  s->k |= 2;   /* shaking */
  CO_SAY(c, s->tb, "ch6_theo_watchout", NULL, NULL);
  s->k &= (uint8_t)~2;
  {
    Ent *pl = first_of(&PLATEAU);
    if (pl) {
      V2 from = v2(pl->x + 8 + 52, pl->y + 2 + 8);   /* the plateau's center, 8 lower */
      for (int x = 0; x < 104; x += 8)
        for (int n = 0; n < 2; n++) level_debris(pl->x + 8 + x + rndf() * 8, pl->y + rndf() * 8, '3', from);
      ent_remove(pl);
    }
    Ent *bon = bonfire_find();
    if (bon) ent_remove(bon);
  }
  level_shake(0.3f);
  p->speed.y = 160;
  spr_play(&p->spr, A_player_tentacle_pull, false);
  p->force_camera_update = false;
  fade_wipe(false, 3, NULL);
  k->target = g_level.cam;
  k->start = v2add(g_level.cam, v2(0, 400));
  while (g_wipe.active && g_wipe.percent < 1) {
    g_level.cam = v2lerp(k->target, k->start, ease_cube_in(g_wipe.percent));
    p->speed.y += 400 * DT;
    tent_nodes(k, 300, 600, false);
    CO_YIELD(c);
  }
  cutscene_end(e);   /* the wipe's onComplete */
  return;
  CO_END(c);
}
static void sjend_end(Ent *e, bool skipped) {
  SJEnd *k = SJE(e);
  breath_remove(k->breath);
  level_set_flag("plateau_2", true);
  g_session.dreaming = 0;
  g_session.first_level = 0;
  /* OverworldReflectionsFall (the mountain) is not here: straight to room 04, falling in (at the spawn nearest
   * its top middle, the background fading in); skipped: room 00 */
  int r = chapter_find_room(g_level.chapter, skipped ? "00" : "04");
  room_hook = !skipped;
  if (r >= 0) level_load_room_near(r, skipped ? INTRO_NONE : INTRO_FALL, skipped ? 0 : 0.5f, skipped ? 1 : 0);
}
static void sjend_update(Ent *e) {
  SJEnd *k = SJE(e);
  S6 *s = &k->s;
  Player *p = level_player();
  if (s->k & 4) {   /* the campfire's end: Level.ZoomBack with the fade in */
    if (!zoom_back(&s->zoom, 3)) s->k &= (uint8_t)~4;
  }
  if (s->k & 1) {
    if (p && p->ent->y <= g_level.room->y + 160) {   /* Start() */
      s->k &= (uint8_t)~1;
      cutscene_start(e, sjend_end, true, false);
    }
    return;
  }
  if (k->tw >= 0 && p) {
    k->tw = fminf(1, k->tw + DT / 0.5f);
    V2 a = v2lerp(k->tw_from, k->tw_to, ease_cube_out(k->tw));
    p->ent->x = a.x, p->ent->y = a.y;
    if (k->tw >= 1) k->tw = -1;
  }
  s6_update(e);
  if (e->dead == 1) return;
  if ((s->k & 32) && s->bd && !job_busy(&BD(s->bd)->job)) {   /* flown down: badeline.RemoveSelf() */
    s->k &= (uint8_t)~32;
    ent_remove(s->bd);
    s->bd = NULL;
  }
  if (s->k & 2) level_shake(0.2f);
  /* Distort.Anxiety is not drawn */
  s->f[0] = approach(s->f[0], s->f[1], 12 * DT);
  if (s->f[0] > 0 && p) p->ent->y = s->f[2] + sinf(g_level.time_active * 2) * 3 * s->f[0];
}
static const EntClass SJEND = {.name = "cutscene6", .size = sizeof(SJEnd), .update = sjend_update};
static void sjend_wait_new(Ent *theo, V2 player_start, V2 cam_start, bool zoom_back_) {
  Ent *e = ent_new(&SJEND, 0, 0);
  if (!e) return;
  e->collidable = 0, e->visible = 0, e->depth = 10100;
  SJEnd *k = SJE(e);
  S6 *s = &k->s;
  s->run = sjend_run, s->evs = sjend_nb, s->par = sjend_par, s->ev_i = -1;
  s->a = theo, s->v[0] = player_start, s->v[1] = cam_start;
  s->k = 1 | (zoom_back_ ? 4 : 0);
  k->tw = -1;
  step_reset(&s->zoom);
}

/* ---------------------------------------------------------------- after the dream: the next room's start */
typedef struct { float delay, p; } BgFade;
static void bgfade_update(Ent *e) {   /* BackgroundFadeIn(Black, 2, 30) */
  BgFade *f = ST(e, BgFade);
  if (f->delay > 0) f->delay -= DT;
  else if ((f->p += DT / 30) >= 1) ent_remove(e);
}
static void bgfade_render(Ent *e) { gfx_rect(g_level.cam.x - 10, g_level.cam.y - 10, 340, 200, 0, a8(1 - ST(e, BgFade)->p)); }
static const EntClass BGFADE = {.name = "backgroundFadeIn", .size = sizeof(BgFade), .update = bgfade_update, .render = bgfade_render};
/* ---------------------------------------------------------------- EventTrigger */
typedef struct { uint8_t which, triggered; } Event6;
static void event6_enter(Ent *e, Player *p) {
  Event6 *ev = ST(e, Event6);
  (void)p;
  if (ev->triggered) return;
  ev->triggered = 1;
  float cx = e->x + e->cx + e->cw / 2;
  Ent *boss = first_of(&BOSS), *cs;
  if (ev->which == 0) {   /* ch6_boss_intro */
    if (level_get_flag("boss_intro") || !boss || !(cs = s6_new(bintro_run, bintro_end, true, false))) return;
    S6 *s = S6_(cs);
    s->a = boss, s->evs = bintro_nb;
    s->f[0] = boss->y - 16, s->f[3] = cx;
  } else if (!level_get_flag("reflection") && (cs = s6_new(reflect6_run, reflect6_end, true, false)))   /* ch6_reflect */
    S6_(cs)->f[0] = cx - 5;
}
static const EntClass EVENT6 = {.name = "eventTrigger", .size = sizeof(Event6), .kind = KIND_TRIGGER,
                                .more = &(const EntMore){.on_enter = event6_enter}};
static bool new_event6(const EData *d) {
  const char *ev = EAS(d, eventTrigger, event);
  int which = !strcmp(ev, "ch6_boss_intro") ? 0 : !strcmp(ev, "ch6_reflect") ? 1 : -1;
  if (which < 0) return false;
  Ent *e = ent_new(&EVENT6, d->x, d->y);
  if (e) ent_box(e, EA(d, eventTrigger, width), EA(d, eventTrigger, height), 0, 0), e->visible = 0, ST(e, Event6)->which = (uint8_t)which;
  return true;
}

/* ---------------------------------------------------------------- factories */
bool ents_ch6(const EData *d) {
  if (room_hook) {   /* the room after the dream (CS06_StarJumpEnd): the background fades in */
    room_hook = 0;
    Ent *f = ent_new(&BGFADE, 0, 0);
    if (f) f->depth = 10100, f->collidable = 0, f->tags = TAG_PERSISTENT | TAG_TRANSITION_UPDATE, ST(f, BgFade)->delay = 2;
  }
  switch (d->type) {
    case ET_bigSpinner: new_bumper(d); return true;
    case ET_infiniteStar: new_feather(d); return true;
    case ET_starJumpBlock: new_starjump(d); return true;
    case ET_plateau: new_plateau(d); return true;
    case ET_crushBlock: new_kevin(d); return true;
    case ET_bigWaterfall: new_waterfall(d); return true;
    case ET_badelineBoost: new_bboost(d); return true;
    case ET_reflectionHeartStatue: new_hstatue(d); return true;
    case ET_finalBoss: new_boss(d); return true;
    case ET_finalBossFallingBlock: new_bfall(d); return true;
    case ET_finalBossMovingBlock: new_bmove(d); return true;
    case ET_tentacles: new_tentacles(d); return true;
    case ET_npc: {
      const char *who = EAS(d, npc, npc);
      if (!strcmp(who, "granny_06_intro")) new_granny6(d);
      else if (!strcmp(who, "theo_06_plateau")) new_theo6p(d);
      else if (!strcmp(who, "badeline_06_crying")) new_crying(d);
      else if (!strcmp(who, "granny_06_ending") || !strcmp(who, "theo_06_ending")) new_ending_npc(d, who[0] == 't');
      else return false;
      return true;
    }
    default: return false;
  }
}
bool trigs_ch6(const EData *d) {
  switch (d->type) {
    case TT_lookoutBlocker: new_lookout_blocker(d); return true;
    case TT_eventTrigger: return new_event6(d);
    default: return false;
  }
}
