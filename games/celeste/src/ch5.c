#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")   /* not drawn every frame: smaller over faster */
#endif
/* Mirror Temple (chapter 5): torches, temple eyes and gates, dash switches,
 * swap blocks, cracked walls, seeker barriers and seekers, Theo's crystal
 * (the holdable), condition blocks, the mirrors and the mirror portal, the
 * seeker statue, the big eyeball and the player seeker of the void.
 * Ported from the game's classes (Torch.cs, TempleEye.cs, TempleGate.cs...). */
#include <stdlib.h>
#include "badeline.h"

/* ---------------------------------------------------------------- helpers (Calc) */
static float angle_diff(float a, float b) {
  float d = b - a;
  while (d > PI_F) d -= 2 * PI_F;
  while (d <= -PI_F) d += 2 * PI_F;
  return d;
}
static float angle_lerp(float a, float b, float t) { return a + angle_diff(a, b) * t; }
static float angle_approach(float v, float t, float m) {
  float d = angle_diff(v, t);
  if (fabsf(d) < m) return t;
  return v + clampf(d, -m, m);
}
static float dist2(V2 a, V2 b) { return (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y); }
/* Scene.OnInterval(interval, offset) */
static bool on_interval_off(float interval, float offset) {
  float t = g_level.time_active;
  return floorf((t - offset - DT) / interval) < floorf((t - offset) / interval);
}
static uint16_t tex_named(const char *p) { return res_tex_by_name(p); }
static void tex_family(const char *prefix, uint16_t *out, int n) {
  char p[64];
  for (int i = 0; i < n; i++) out[i] = tex_named(path2(p, prefix, NULL, i));
}
/* frame f of a family drawn as an animation: when it is not loaded, the whole family is, in one pass */
static uint16_t fam_tex(const uint16_t *tex, int n, int f) {
  if (tex[f] != 0xFFFF && !res_cached(tex[f])) res_load(tex, n);
  return tex[f];
}
/* MTexture.DrawCentered */
static void draw_centered(uint16_t tex, float x, float y, uint16_t tint, uint8_t alpha, float sx, float sy, float rot) {
  Tex t;
  if (tex == 0xFFFF || !tex_get(tex, &t)) return;
  float k = t.scale > 1 ? t.scale : 1;   /* stored smaller, drawn scaled up */
  gfx_tex_ex(tex, x, y, t.fw * k / 2.f, t.fh * k / 2.f, sx, sy, rot, tint, alpha, 0);
}
static uint16_t lerp565(uint32_t a, uint32_t b, float t) {
  t = clampf(t, 0, 1);
  int r = (int)(((a >> 16) & 255) + (((int)((b >> 16) & 255) - (int)((a >> 16) & 255)) * t));
  int g = (int)(((a >> 8) & 255) + (((int)((b >> 8) & 255) - (int)((a >> 8) & 255)) * t));
  int bl = (int)((a & 255) + (((int)(b & 255) - (int)(a & 255)) * t));
  return rgb((uint32_t)(r << 16 | g << 8 | bl));
}

/* the chapter's rooms: names and indices (EntityID.Level) */
static const char *room_name(int i) { return level_room_name_of(i); }
static int room_index(const char *name, int n) {
  char buf[24];
  if (n >= (int)sizeof buf) return -1;
  memcpy(buf, name, (size_t)n);
  buf[n] = 0;
  return chapter_find_room(g_level.chapter, buf);
}
/* "prefix" + EntityID.Key ("room:id") */
static const char *id_flag(char *buf, const char *prefix, int room, int id) {
  char num[8];
  int k = 0, v = id < 0 ? 0 : id;
  char tmp[8];
  do tmp[k++] = (char)('0' + v % 10), v /= 10;
  while (v);
  for (int i = 0; i < k; i++) num[i] = tmp[k - 1 - i];
  num[k] = 0;
  char *o = buf;
  for (const char *s = prefix; *s; s++) *o++ = *s;
  for (const char *s = room_name(room); *s; s++) *o++ = *s;
  *o++ = ':';
  for (const char *s = num; *s; s++) *o++ = *s;
  *o = 0;
  return buf;
}

/* entity references in states are indices (states are only 4-byte aligned) */
static uint8_t ent_ref(const Ent *e) { return (uint8_t)(e - g_ents); }
static Ent *ent_at(uint8_t i) { return &g_ents[i]; }
static Ent *first_of(const EntClass *c) {
  for (int i = 0; i < g_nents; i++)
    if (g_ents[i].cls == c && g_ents[i].dead != 1) return &g_ents[i];
  return NULL;
}

/* the player's hurtbox (Player.hurtbox) overlapping a rect: PlayerCollider checks */
static void player_hurtbox(const Ent *pe, float *l, float *t, float *r, float *b) {
  float w = pe->cw, h = pe->ch, x = pe->cx, y = pe->cy;
  if (h == 11) h = 9;
  else if (h == 6) h = 4;
  else if (h == 8 && w == 8) w = 6, h = 6, x = -3, y = -9;
  *l = pe->x + x, *t = pe->y + y, *r = *l + w, *b = *t + h;
}
static Player *player_hits(float l, float t, float r, float b) {
  Player *p = level_player();
  if (!p || p->dead || p->state == ST_CASSETTEFLY) return NULL;
  float pl, pt, pr, pb;
  player_hurtbox(p->ent, &pl, &pt, &pr, &pb);
  return pr > l && pb > t && pl < r && pt < b ? p : NULL;
}

/* ---------------------------------------------------------------- compact sprites */
/* A Monocle Sprite's playing state; banks give the frames and origins */
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
static int cs_cur(const CSpr *c) { return c->anim != 255 ? c->anim : -1; }
/* Sprite.Update; returns the animation that went past its last frame (OnLastFrame), else -1 */
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
static void cs_draw(const CSpr *c, float x, float y, float sx, float sy, float rot, uint16_t tint, uint8_t alpha) {
  Sprite s;
  spr_init(&s, c->bank);
  uint16_t t = cs_tex(c);
  Tex tx;
  if (t == 0xFFFF || !tex_get(t, &tx)) return;
  float ox = s.justify ? tx.fw * tx.scale * s.jx : s.ox, oy = s.justify ? tx.fh * tx.scale * s.jy : s.oy;
  gfx_tex_ex(t, x, y, ox, oy, sx, sy, rot, tint, alpha, 0);
}

/* DeathEffect.Draw */
static void death_draw(V2 pos, uint16_t color, float ease) {
  uint16_t c2 = ((int)floorf(ease * 10) % 2 == 0) ? color : 0xFFFF;
  float s = ease < 0.5f ? 0.5f + ease : ease_cube_out(1 - (ease - 0.5f) * 2);
  for (int pass = 0; pass < 2; pass++)
    for (int i = 0; i < 8; i++) {
      V2 at = v2add(pos, angle_vec((i / 8.f + ease * 0.25f) * 2 * PI_F, ease_cube_out(ease) * 24));
      if (!pass) {
        static const int8_t o[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
        for (int k = 0; k < 4; k++)
          gfx_tex_ex(T_characters_player_hair00, at.x + o[k][0], at.y + o[k][1], 5, 5, s, s, 0, 0, 255, GF_SILHOUETTE);
      } else
        gfx_tex_ex(T_characters_player_hair00, at.x, at.y, 5, 5, s, s, 0, c2, 255, GF_SILHOUETTE);
    }
}

/* ---------------------------------------------------------------- Torch */
/* 585 in the chapter, 54 in a room: one entity per room holds them all */
#define MAX_TORCH 100
typedef struct {
  int16_t x, y;
  uint16_t bit, frames;                /* bit: in Session.torches; frames: updates since the sprite started playing */
  uint8_t slot, lit, lit_bank;         /* lit_bank: startLit, the "litTorch" sprite */
  uint8_t on_mode, tween;              /* on_mode: the sprite plays "on" (lit when added), else "turnOn" first;
                                        * tween: the light's tween, in updates (60: done) */
} Torch;
enum { OFF_torches = 0, END_torches = OFF_torches + (int)sizeof(Torch[MAX_TORCH]) };
#define torches (*(Torch(*)[MAX_TORCH])(void *)(g_chram[5] + OFF_torches))
static int ntorches;
typedef struct { uint8_t slot, n; } TorchMgr;   /* n: the room's torches unlit at first, so far */
#define TORCH_BITS (8 * (int)sizeof g_session.torches_lit)
static uint16_t torch_tex[2][9];

static void torch_update(Ent *e) {
  int slot = ST(e, TorchMgr)->slot;
  for (int i = 0; i < ntorches; i++) {
    Torch *t = &torches[i];
    if (t->slot != slot) continue;
    if (t->lit) {
      t->frames++;
      if (t->frames * DT > 12.24f) t->frames -= 720;   /* 25 loops of "on" (12 s), whole frames either way */
      if (t->tween < 60) t->tween++;
      continue;
    }
    /* PlayerCollider: Hitbox(32, 32, -16, -16) */
    if (player_hits(t->x - 16, t->y - 16, t->x + 16, t->y + 16)) {
      t->lit = 1;
      t->frames = 0;
      t->tween = 0;
      if (t->bit < TORCH_BITS) g_session.torches_lit[t->bit >> 3] |= (uint8_t)(1 << (t->bit & 7));   /* Session.SetFlag */
      particles_emit(PL_FG, &P_Torch_P_OnLight, 12, v2(t->x, t->y), v2(3, 3), 0);
    }
  }
}
static void torch_render(Ent *e) {
  int slot = ST(e, TorchMgr)->slot;
  for (int i = 0; i < ntorches; i++) {
    Torch *t = &torches[i];
    if (t->slot != slot) continue;
    int f = 0;
    float time = t->frames * DT;
    if (t->lit) {
      if (t->on_mode || time >= 0.24f) {   /* "on": frames 3-8 */
        float on = t->on_mode ? time : time - 0.24f;
        f = 3 + (int)floorf(on / 0.08f) % 6;
      } else
        f = 1 + (int)floorf(time / 0.08f);   /* "turnOn": frames 1-3 */
    }
    draw_centered(fam_tex(torch_tex[t->lit_bank], 9, f), t->x, t->y, 0xFFFF, 255, 1, 1, 0);
    if (t->lit) {
      /* VertexLight and BloomPoint */
      uint32_t c = t->lit_bank ? 0xFFD27F : 0x7FFFFF;   /* StartLitColor, Torch.Color */
      float ease = 1, bloom = 0.5f;
      if (t->tween < 60) {
        ease = ease_back_out(t->tween * DT);
        uint16_t col = lerp565(0xFFFFFF, c, ease);
        c = (uint32_t)((col >> 11) << 19 | ((col >> 5) & 63) << 10 | (col & 31) << 3);
        bloom = 0.5f + 0.5f * (1 - ease);
      }
      light_add(t->x, t->y, c, 1, 48 + (1 - ease) * 32, 64 + (1 - ease) * 32);
      bloom_add(t->x, t->y, bloom, 8);
    }
  }
}
static void torch_removed(Ent *e) {
  int slot = ST(e, TorchMgr)->slot, n = 0;
  for (int i = 0; i < ntorches; i++)
    if (torches[i].slot != slot) torches[n++] = torches[i];
  ntorches = n;
}
static const EntClass TORCHES = {.name = "torch", .size = sizeof(TorchMgr), .update = torch_update, .render = torch_render,
                                 .removed = torch_removed};

static void new_torch(const EData *d) {
  int slot = g_level.room_slot;
  Ent *m = NULL;
  for (int i = 0; i < g_nents; i++)
    if (g_ents[i].cls == &TORCHES && g_ents[i].dead != 1 && ST(&g_ents[i], TorchMgr)->slot == slot) m = &g_ents[i];
  if (!m) {
    m = ent_new(&TORCHES, 0, 0);
    if (!m) return;
    m->depth = 2000;
    m->collidable = 0;
    ST(m, TorchMgr)->slot = (uint8_t)slot;
  }
  bool start_lit = EAB(d, torch, startLit);
  int bit = start_lit ? 0xFFFF : g_level.room->info[RR_TORCHES] * 2 + ST(m, TorchMgr)->n++;
  if (ntorches >= MAX_TORCH) return;
  if (torch_tex[0][0] == 0) {
    tex_family("objects/temple/torch", torch_tex[0], 9);
    tex_family("objects/temple/litTorch", torch_tex[1], 9);
  }
  Torch *t = &torches[ntorches++];
  memset(t, 0, sizeof *t);
  t->x = (int16_t)d->x, t->y = (int16_t)d->y;
  t->bit = (uint16_t)bit;
  t->slot = (uint8_t)slot;
  t->lit_bank = start_lit;
  t->tween = 60;
  if (start_lit || (bit < TORCH_BITS && (g_session.torches_lit[bit >> 3] >> (bit & 7) & 1))) {   /* Session.GetFlag */
    t->lit = 1;   /* Added: lit, sprite.Play("on") */
    t->on_mode = 1;
  }
}

/* ---------------------------------------------------------------- TempleEye */
/* 468 in the chapter, 71 in a room: one entity per room and layer draws them */
#define MAX_EYES 128
enum { EYE_BG = 1, EYE_KNOWN = 2, EYE_BLINK = 4, EYE_BURST = 8 };
typedef struct {
  int16_t x, y;
  int8_t px, py;   /* pupilPosition, 1/32 pixels */
  int8_t tx, ty;   /* pupilTarget, 1/100 */
  uint8_t slot, flags, n, pad;   /* n: updates since the eyelid's blink or the burst began */
  float timer;     /* blinkTimer */
} Eye;
enum { OFF_eyes = ((END_torches + 7) & ~7), END_eyes = OFF_eyes + (int)sizeof(Eye[MAX_EYES]) };
#define eyes (*(Eye(*)[MAX_EYES])(void *)(g_chram[5] + OFF_eyes))
static int neyes;
typedef struct { uint8_t slot, bg; } EyeMgr;
static uint16_t eye_tex[2][2], eye_lid[2][4], eye_burst[2][9];
static const uint8_t EYE_BLINK_FRAMES[6] = {0, 1, 1, 2, 3, 0};
static const EntClass THEO;
static void eye_set_timer(Eye *y) { y->timer = rnd_rangef(1, 15); }
static V2 theo_center(Ent *t) { return v2(t->x, t->y - 5); }
static void eye_target(Eye *y, Ent *theo) {
  V2 d = safe_norm(v2sub(theo_center(theo), v2(y->x, y->y)), 1);
  y->tx = (int8_t)roundi(d.x * 100), y->ty = (int8_t)roundi(d.y * 100);
}
static void eyes_resolve(int slot) {
  Ent *theo = first_of(&THEO);
  for (int i = 0; i < neyes; i++) {
    Eye *y = &eyes[i];
    if (y->slot != slot || (y->flags & EYE_KNOWN)) continue;
    y->flags |= EYE_KNOWN;
    if (!point_solid(y->x, y->y)) y->flags |= EYE_BG;   /* Added: isBG = !CollideCheck<Solid>(Position) */
    if (theo) {   /* Awake */
      eye_target(y, theo);
      y->px = (int8_t)(y->tx * 96 / 100), y->py = (int8_t)(y->ty * 96 / 100);
    }
  }
}
static void eyes_awake(Ent *e) { eyes_resolve(ST(e, EyeMgr)->slot); }
static void eyes_update(Ent *e) {
  EyeMgr *m = ST(e, EyeMgr);
  if (!m->bg) return;   /* the background manager updates them all */
  Ent *theo = first_of(&THEO);
  bool chance = level_on_interval(0.25f);
  int n = 0;
  for (int i = 0; i < neyes; i++) {
    Eye *y = &eyes[i];
    if (y->slot != m->slot) {
      eyes[n++] = *y;
      continue;
    }
    if (y->flags & EYE_BURST) {   /* "burst": 0.08 a frame, then RemoveSelf */
      if ((int)(++y->n * DT / 0.08f) >= 9) continue;
      eyes[n++] = *y;
      continue;
    }
    /* pupilPosition = Approach(pupilPosition, pupilTarget * 3, 16 * DT) */
    V2 p = approach_v(v2(y->px / 32.f, y->py / 32.f), v2(y->tx * 0.03f, y->ty * 0.03f), 16 * DT);
    y->px = (int8_t)roundi(p.x * 32), y->py = (int8_t)roundi(p.y * 32);
    if (theo) {
      eye_target(y, theo);
      if (chance && rndf() < 0.01f && !(y->flags & EYE_BLINK)) y->flags |= EYE_BLINK, y->n = 0;
    }
    y->timer -= DT;
    if (y->timer <= 0) {
      eye_set_timer(y);
      if (!(y->flags & EYE_BLINK)) y->flags |= EYE_BLINK, y->n = 0;
    }
    /* the eyelid's "blink": frames 0 1 1 2 3 0, 0.08 each, then "open" */
    if ((y->flags & EYE_BLINK) && (int)(++y->n * DT / 0.08f) >= 6) y->flags &= (uint8_t)~EYE_BLINK;
    eyes[n++] = *y;
  }
  neyes = n;
}
static void eyes_render(Ent *e) {
  EyeMgr *m = ST(e, EyeMgr);
  for (int i = 0; i < neyes; i++) {
    Eye *y = &eyes[i];
    if (y->slot != m->slot || !(y->flags & EYE_KNOWN) || !(y->flags & EYE_BG) != !m->bg) continue;
    int k = (y->flags & EYE_BG) ? 0 : 1;
    if (y->x < g_camx - 16 || y->x > g_camx + 336 || y->y < g_camy - 16 || y->y > g_camy + 196) continue;
    if (y->flags & EYE_BURST) {
      int f = (int)(y->n * DT / 0.08f);
      draw_centered(fam_tex(eye_burst[k], 9, f < 9 ? f : 8), y->x, y->y, 0xFFFF, 255, 1, 1, 0);
      continue;
    }
    draw_centered(eye_tex[k][0], y->x, y->y, 0xFFFF, 255, 1, 1, 0);
    draw_centered(eye_tex[k][1], y->x + y->px / 32.f, y->y + y->py / 32.f, 0xFFFF, 255, 1, 1, 0);
    int f = (y->flags & EYE_BLINK) ? EYE_BLINK_FRAMES[(int)(y->n * DT / 0.08f) % 6] : 0;
    draw_centered(eye_lid[k][f], y->x, y->y, 0xFFFF, 255, 1, 1, 0);
  }
}
static void eyes_removed(Ent *e) {
  EyeMgr *m = ST(e, EyeMgr);
  if (!m->bg) return;
  int n = 0;
  for (int i = 0; i < neyes; i++)
    if (eyes[i].slot != m->slot) eyes[n++] = eyes[i];
  neyes = n;
}
static const EntClass EYES = {.name = "templeEye", .size = sizeof(EyeMgr), .update = eyes_update, .render = eyes_render,
                              .awake = eyes_awake, .removed = eyes_removed};
/* TempleEye.Burst, for every eye (TempleBigEyeball) */
static void eyes_burst_all(void) {
  for (int i = 0; i < neyes; i++)
    if (!(eyes[i].flags & EYE_BURST)) eyes[i].flags |= EYE_BURST, eyes[i].n = 0;
}

static void new_eye(const EData *d) {
  int slot = g_level.room_slot;
  bool have = false;
  for (int i = 0; i < g_nents; i++)
    if (g_ents[i].cls == &EYES && g_ents[i].dead != 1 && ST(&g_ents[i], EyeMgr)->slot == slot) have = true;
  if (!have)
    for (int bg = 1; bg >= 0; bg--) {
      Ent *m = ent_new(&EYES, 0, 0);
      if (!m) return;
      m->depth = bg ? 8990 : -10001;
      m->collidable = 0;
      ST(m, EyeMgr)->slot = (uint8_t)slot;
      ST(m, EyeMgr)->bg = (uint8_t)bg;
    }
  if (neyes >= MAX_EYES) return;
  if (!eye_tex[0][0]) {
    eye_tex[0][0] = tex_named("scenery/temple/eye/bg_eye");
    eye_tex[0][1] = tex_named("scenery/temple/eye/bg_pupil");
    eye_tex[1][0] = tex_named("scenery/temple/eye/fg_eye");
    eye_tex[1][1] = tex_named("scenery/temple/eye/fg_pupil");
    tex_family("scenery/temple/eye/bg_lid", eye_lid[0], 4);
    tex_family("scenery/temple/eye/fg_lid", eye_lid[1], 4);
    tex_family("scenery/temple/eye/bg_burst", eye_burst[0], 9);
    tex_family("scenery/temple/eye/fg_burst", eye_burst[1], 9);
  }
  Eye *y = &eyes[neyes++];
  memset(y, 0, sizeof *y);
  y->x = (int16_t)d->x, y->y = (int16_t)d->y;
  y->slot = (uint8_t)slot;
  eye_set_timer(y);
}
/* Shaker: Calc.Random.ShakeVector picks from {-1, -1, 0, 1, 1} */
static int8_t shake_off(void) {
  static const int8_t o[5] = {-1, -1, 0, 1, 1};
  return o[rndi(5)];
}
typedef struct { float timer; int8_t x, y; uint8_t on, pad; } Shaker;
static void shaker_for(Shaker *s, float t) { s->on = 1, s->timer = t; }
static void shaker_update(Shaker *s) {
  if (s->on && s->timer > 0) {
    s->timer -= DT;
    if (s->timer <= 0) {
      s->on = 0, s->x = s->y = 0;
      return;
    }
  }
  if (s->on && level_on_interval(0.05f)) s->x = shake_off(), s->y = shake_off();
}

/* ---------------------------------------------------------------- TempleGate */
enum { TG_NEAREST, TG_BEHIND, TG_BEHIND_ALWAYS, TG_HOLDING_THEO, TG_TOUCH, TG_BEHIND_THEO };
typedef struct {
  uint8_t type, look, open, claimed, lock, co, anim, frame;   /* look: default, mirror, theo */
  uint8_t alarm_step, pad[3];
  Shaker shaker;
  int16_t closed_h, pad2;
  float draw_h, draw_speed, hold_wait, anim_t, alarm, co_wait;
  V2 hold_from;
} Gate;
static uint16_t gate_tex[3][15];
enum { GA_IDLE, GA_OPEN, GA_HIT };
static void gate_play(Gate *g, int anim) {   /* Sprite.Play(anim) (no restart) */
  if (g->anim == anim) return;
  g->anim = (uint8_t)anim, g->frame = 0, g->anim_t = 0;
}
/* SetHeight: grows by pushing from above, as the gate comes down */
static void gate_set_height(Ent *e, int h) {
  if (h < e->ch) {
    e->ch = (float)h;
    return;
  }
  float y = e->y;
  int num = (int)e->ch;
  if (e->ch < 64) {
    e->y -= 64 - e->ch;
    e->ch = 64;
  }
  plat_move_v_exact(e, h - num);
  e->y = y;
  e->ch = (float)h;
}
static void gate_start_open(Ent *e) {
  Gate *g = ST(e, Gate);
  gate_set_height(e, 0);
  g->draw_h = 4;
  g->open = 1;
}
static void gate_open(Ent *e) {
  Gate *g = ST(e, Gate);
  g->hold_wait = 0.2f;
  g->draw_speed = 200;
  g->draw_h = e->ch;
  shaker_for(&g->shaker, 0.2f);
  gate_set_height(e, 0);
  gate_play(g, GA_OPEN);
  g->open = 1;
}
static void gate_close(Ent *e) {
  Gate *g = ST(e, Gate);
  g->hold_wait = 0.2f;
  g->draw_speed = 300;
  g->draw_h = fmaxf(4, e->ch);
  shaker_for(&g->shaker, 0.2f);
  gate_set_height(e, g->closed_h);
  gate_play(g, GA_HIT);
  g->open = 0;
}
static void gate_switch_open(Ent *e) {   /* sprite "open", then 0.2 s: shake, then 0.2 s: Open */
  Gate *g = ST(e, Gate);
  gate_play(g, GA_OPEN);
  g->alarm = 0.2f;
  g->alarm_step = 1;
}
static Ent *theo_entity(void);
static bool gate_theo_nearby(Ent *e) {
  Gate *g = ST(e, Gate);
  Ent *t = theo_entity();
  if (!t || t->x > e->x + 10) return true;
  return dist2(g->hold_from, theo_center(t)) < (g->open ? 6400 : 4096);
}
bool level_switch_check(void);   /* Switch.Check: the touch switches (ch2.c) are all on */
void level_touch_switches_in_circle(V2 c, float r);   /* (ch2.c) */
static void gate_update(Ent *e) {
  Gate *g = ST(e, Gate);
  plat_update(e);
  /* components: the sprite, the shaker, coroutines, alarms */
  static const float DELAY[3] = {0.1f, 0.1f, 0.08f};
  static const uint8_t LEN[3] = {1, 9, 5};
  if (g->anim != GA_IDLE) {
    g->anim_t += DT;
    if (g->anim_t >= DELAY[g->anim]) {
      g->anim_t -= DELAY[g->anim];
      if (++g->frame >= LEN[g->anim]) g->anim = GA_IDLE, g->frame = 0, g->anim_t = 0;   /* goto idle */
    }
  }
  shaker_update(&g->shaker);
  Ent *pe = level_player_ent();
  switch (g->co) {
    case 1:   /* CloseBehindPlayer */
      if (!g->lock && pe && e_left(pe) > e_right(e) + 4) g->co = 0, gate_close(e);
      break;
    case 2: {   /* CloseBehindPlayerAndTheo */
      Ent *t = theo_entity();
      if (pe && e_left(pe) > e_right(e) + 4 && !g->lock && t && e_left(t) > e_right(e) + 4) g->co = 0, gate_close(e);
      break;
    }
    case 3:   /* CheckTouchSwitches */
      if (g->co_wait > 0) {
        g->co_wait -= DT;
        break;
      }
      if (!level_switch_check()) break;
      gate_play(g, GA_OPEN);
      g->co_wait = 0.5f;
      g->co = 4;
      break;
    case 4:
      if (g->co_wait > 0) {
        g->co_wait -= DT;
        break;
      }
      shaker_for(&g->shaker, 0.2f);
      g->co_wait = 0.2f;
      g->co = 5;
      break;
    case 5:
      if (g->co_wait > 0) {
        g->co_wait -= DT;
        break;
      }
      if (g->lock) break;
      g->co = 0;
      gate_open(e);
      break;
  }
  if (g->alarm_step && (g->alarm -= DT) <= 0) {
    if (g->alarm_step == 1) {
      shaker_for(&g->shaker, 0.2f);
      g->alarm = 0.2f;
      g->alarm_step = 2;
    } else {
      g->alarm_step = 0;
      gate_open(e);
    }
  }
  if (g->type == TG_HOLDING_THEO) {
    if (g->hold_wait > 0) g->hold_wait -= DT;
    else if (!g->lock) {
      if (g->open && !gate_theo_nearby(e)) {
        gate_close(e);
        /* CollideFirst<Player>(Position + (8, 0))?.Die */
        if (pe && collide_ent_at(e, e->x + 8, e->y, pe)) player_die(level_player(), v2(0, 0), false);
      } else if (!g->open && gate_theo_nearby(e))
        gate_open(e);
    }
  }
  float h = fmaxf(4, e->ch);
  if (g->draw_h != h) {
    g->lock = 1;
    g->draw_h = approach(g->draw_h, h, g->draw_speed * DT);
  } else
    g->lock = 0;
}
static void gate_awake(Ent *e) {
  Gate *g = ST(e, Gate);
  Ent *pe = level_player_ent();
  if (g->type == TG_BEHIND) {
    if (pe && e_left(pe) < e_right(e) && e_bottom(pe) >= e_top(e) && e_top(pe) <= e_bottom(e)) {
      gate_start_open(e);
      g->co = 1;
    }
  } else if (g->type == TG_BEHIND_ALWAYS) {
    gate_start_open(e);
    g->co = 1;
  } else if (g->type == TG_BEHIND_THEO) {
    gate_start_open(e);
    g->co = 2;
  } else if (g->type == TG_HOLDING_THEO) {
    if (gate_theo_nearby(e)) gate_start_open(e);
    e->cw = 16;
  } else if (g->type == TG_TOUCH)
    g->co = 3;
  g->draw_h = fmaxf(4, e->ch);
}
static void gate_render(Ent *e) {
  Gate *g = ST(e, Gate);
  float sx = (float)sgn(g->shaker.x);
  gfx_rect(e->x - 2, e->y - 8, 14, 10, 0, 255);
  static const uint8_t FIRST[3] = {0, 6, 0};
  uint16_t t = fam_tex(gate_tex[g->look], 15, FIRST[g->anim] + g->frame);
  int dh = (int)g->draw_h;
  /* DrawSubrect(0, 48 - drawHeight, 24, drawHeight): the bottom of the door, from the gate's top */
  if (dh > 0) gfx_tex_part(t, e->x + 4 - 12 + sx, e->y, 0, 48 - dh, 24, dh, 0, 0xFFFF, 255);
}
static const EntClass GATE = {.name = "templeGate", .size = sizeof(Gate), .update = gate_update, .render = gate_render,
                              .awake = gate_awake, .kind = KIND_SOLID};

static void new_gate(const EData *d) {
  int h = (int)EA(d, templeGate, height);
  Ent *e = ent_new(&GATE, d->x, d->y);
  if (!e) return;
  ent_box(e, 8, (float)h, 0, 0);
  e->safe = 1;
  e->depth = -9000;
  Gate *g = ST(e, Gate);
  static const char *TYPES[] = {"NearestSwitch", "CloseBehindPlayer", "CloseBehindPlayerAlways", "HoldingTheo", "TouchSwitches",
                                "CloseBehindPlayerAndTheo"};
  const char *ty = EAS(d, templeGate, type);
  for (int i = 0; i < 6; i++)
    if (!strcmp(ty, TYPES[i])) g->type = (uint8_t)i;
  const char *sp = EAS(d, templeGate, sprite);
  g->look = !strcmp(sp, "mirror") ? 1 : !strcasecmp(sp, "theo") ? 2 : 0;
  g->closed_h = (int16_t)h;
  g->hold_wait = 0.2f;
  g->hold_from = v2(d->x + 4, d->y + h / 2);
  if (!gate_tex[0][0]) {
    tex_family("objects/door/TempleDoor", gate_tex[0], 15);
    tex_family("objects/door/TempleDoorB", gate_tex[1], 15);
    tex_family("objects/door/TempleDoorC", gate_tex[2], 15);
  }
}

/* ---------------------------------------------------------------- DashSwitch */
enum { SIDE_UP, SIDE_DOWN, SIDE_LEFT, SIDE_RIGHT };
typedef struct {
  uint8_t side, persistent, all_gates, pressed, was_on, mirror, room, pad;
  uint16_t eid, pad2;
  CSpr spr;
  V2 target;
  float speed_y, start_y;
} DSwitch;
static const int8_t SIDE_DIR[4][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};
static const int8_t SIDE_SPR[4][2] = {{8, 0}, {8, 8}, {0, 8}, {8, 8}};
static const float SIDE_ROT[4] = {-PI_F / 2, PI_F / 2, PI_F, 0};
static Ent *dswitch_gate(Ent *e) {   /* GetGate: the nearest unclaimed NearestSwitch gate of the room */
  Ent *best = NULL;
  float bd = 0;
  for (int i = 0; i < g_nents; i++) {
    Ent *o = &g_ents[i];
    if (o->cls != &GATE || o->dead == 1 || o->room != e->room) continue;
    Gate *g = ST(o, Gate);
    if (g->type != TG_NEAREST || g->claimed) continue;
    float d2 = dist2(v2(e->x, e->y), v2(o->x, o->y));
    if (!best || d2 < bd) best = o, bd = d2;
  }
  if (best) ST(best, Gate)->claimed = 1;
  return best;
}
static void portal_switch_hit(int side);
static int dswitch_hit(Ent *e, Player *p, V2 dir) {
  (void)p;
  DSwitch *s = ST(e, DSwitch);
  V2 pd = v2(SIDE_DIR[s->side][0], SIDE_DIR[s->side][1]);
  if (s->pressed || !v2eq(dir, pd)) return DASH_NORMAL;
  cs_play(&s->spr, s->mirror ? A_dashSwitch_mirror_push : A_dashSwitch_default_push, false);
  s->pressed = 1;
  plat_move_to(e, s->target.x, s->target.y);
  e->collidable = 0;
  e->x -= pd.x * 2, e->y -= pd.y * 2;
  s = ST(e, DSwitch);
  V2 at = v2(e->x + SIDE_SPR[s->side][0], e->y + SIDE_SPR[s->side][1]);
  V2 perp = v2(dir.y * 6, -dir.x * 6);   /* direction.Perpendicular() * 6 */
  perp = v2(fabsf(perp.x), fabsf(perp.y));
  float a = SIDE_ROT[s->side] - PI_F;
  particles_emit(PL_FG, s->mirror ? &P_DashSwitch_P_PressAMirror : &P_DashSwitch_P_PressA, 10, at, perp, a);
  particles_emit(PL_FG, s->mirror ? &P_DashSwitch_P_PressBMirror : &P_DashSwitch_P_PressB, 4, at, perp, a);
  if (s->all_gates) {
    for (int i = 0; i < g_nents; i++) {
      Ent *o = &g_ents[i];
      if (o->cls == &GATE && o->dead != 1 && o->room == e->room && ST(o, Gate)->type == TG_NEAREST) gate_switch_open(o);
    }
  } else {
    Ent *g = dswitch_gate(e);
    if (g) gate_switch_open(g);
  }
  Room *rm = g_level.room;
  portal_switch_hit(sgn(e->x - (rm->x + rm->w / 2)));
  if (s->persistent) {
    char flag[48];
    level_set_flag(id_flag(flag, "dashSwitch_", s->room, s->eid), true);
  }
  return DASH_NORMAL;
}
static void dswitch_awake(Ent *e) {
  DSwitch *s = ST(e, DSwitch);
  char flag[48];
  if (!s->persistent || !level_get_flag(id_flag(flag, "dashSwitch_", s->room, s->eid))) return;
  cs_play(&s->spr, s->mirror ? A_dashSwitch_mirror_pushed : A_dashSwitch_default_pushed, false);
  e->x = s->target.x - SIDE_DIR[s->side][0] * 2, e->y = s->target.y - SIDE_DIR[s->side][1] * 2;
  s->pressed = 1;
  e->collidable = 0;
  if (s->all_gates) {
    for (int i = 0; i < g_nents; i++) {
      Ent *o = &g_ents[i];
      if (o->cls == &GATE && o->dead != 1 && o->room == e->room && ST(o, Gate)->type == TG_NEAREST) gate_start_open(o);
    }
    return;
  }
  Ent *g = dswitch_gate(e);
  if (g) gate_start_open(g);
}
static void dswitch_update(Ent *e) {
  DSwitch *s = ST(e, DSwitch);
  plat_update(e);
  cs_update(&s->spr);
  if (s->pressed || s->side != SIDE_DOWN) return;
  Ent *pe = level_player_ent();
  Player *on = pe && collide_ent_at(e, e->x, e->y - 1, pe) ? level_player() : NULL;   /* GetPlayerOnTop */
  if (on) {
    if (on->holding) dswitch_hit(e, on, v2(0, 1));
    else {
      if (s->speed_y < 0) s->speed_y = 0;
      s->speed_y = approach(s->speed_y, 70, 200 * DT);
      plat_move_towards_y(e, s->start_y + 2, s->speed_y * DT);
    }
  } else {
    if (s->speed_y > 0) s->speed_y = 0;
    s->speed_y = approach(s->speed_y, -150, 200 * DT);
    plat_move_towards_y(e, s->start_y, -s->speed_y * DT);
  }
  s = ST(e, DSwitch);
  s->was_on = on != NULL;
}
static void dswitch_render(Ent *e) {
  DSwitch *s = ST(e, DSwitch);
  cs_draw(&s->spr, e->x + SIDE_SPR[s->side][0], e->y + SIDE_SPR[s->side][1], 1, 1, SIDE_ROT[s->side], 0xFFFF, 255);
}
static const EntClass DSWITCH = {.name = "dashSwitch", .size = sizeof(DSwitch), .update = dswitch_update,
                                 .render = dswitch_render, .awake = dswitch_awake, .kind = KIND_SOLID,
                                 .more = &(const EntMore){.on_dash_collide = dswitch_hit}};
static bool is_dswitch(Ent *e) { return e && e->cls == &DSWITCH; }

static void new_dswitch(const EData *d, bool horizontal) {
  int side;
  bool persistent, all;
  const char *spr;
  if (horizontal) {
    side = EAB(d, dashSwitchH, leftSide) ? SIDE_LEFT : SIDE_RIGHT;
    persistent = EAB(d, dashSwitchH, persistent), all = EAB(d, dashSwitchH, allGates), spr = EAS(d, dashSwitchH, sprite);
  } else {
    side = EAB(d, dashSwitchV, ceiling) ? SIDE_UP : SIDE_DOWN;
    persistent = EAB(d, dashSwitchV, persistent), all = EAB(d, dashSwitchV, allGates), spr = EAS(d, dashSwitchV, sprite);
  }
  Ent *e = ent_new(&DSWITCH, d->x, d->y);
  if (!e) return;
  e->safe = 1;
  if (side == SIDE_UP || side == SIDE_DOWN) ent_box(e, 16, 8, 0, 0);
  else ent_box(e, 8, 16, 0, 0);
  DSwitch *s = ST(e, DSwitch);
  s->side = (uint8_t)side, s->persistent = persistent, s->all_gates = all;
  s->mirror = *spr && strcmp(spr, "default") != 0;
  s->room = (uint8_t)d->room, s->eid = (uint16_t)d->id;
  cs_init(&s->spr, s->mirror ? SB_dashSwitch_mirror : SB_dashSwitch_default);
  cs_play(&s->spr, s->mirror ? A_dashSwitch_mirror_idle : A_dashSwitch_default_idle, false);
  s->target = v2(d->x + SIDE_DIR[side][0] * 8, d->y + SIDE_DIR[side][1] * 8);
  s->start_y = d->y;
}
/* ---------------------------------------------------------------- SwapBlock */
typedef struct {
  V2 start, end;
  float lerp, speed, max_fwd, max_back, ret_timer, red_alpha, prem, mid_t;
  uint8_t target, swapping, moon, pad;
  int16_t rx, ry, rw, rh;   /* moveRect */
} Swap;
typedef struct { float timer; uint8_t block, pad[3]; } SwapPath;
static uint16_t swap_tex[2][3], swap_path[2], swap_mid[2][2][4];   /* [moon][green, red, target] */
static const EntClass SWAP;

static void swap_on_dash(Ent *e, V2 dir) {   /* DashListener.OnDash */
  (void)dir;
  Swap *s = ST(e, Swap);
  s->swapping = s->lerp < 1;
  s->target = 1;
  s->ret_timer = 0.8f;
  if (s->lerp >= 0.2f) s->speed = s->max_fwd;
  else s->speed = lerpf(s->max_fwd * 0.333f, s->max_fwd, s->lerp / 0.2f);
}
static void swap_particles(Ent *e, V2 normal) {
  Swap *s = ST(e, Swap);
  V2 pos, range;
  float dir, num;
  if (normal.x > 0) pos = v2(e_left(e), e_cym(e)), range = v2(0, e->ch - 6), dir = PI_F, num = fmaxf(2, e->ch / 14);
  else if (normal.x < 0) pos = v2(e_right(e), e_cym(e)), range = v2(0, e->ch - 6), dir = 0, num = fmaxf(2, e->ch / 14);
  else if (normal.y > 0) pos = v2(e_cxm(e), e_top(e)), range = v2(e->cw - 6, 0), dir = -PI_F / 2, num = fmaxf(2, e->cw / 14);
  else pos = v2(e_cxm(e), e_bottom(e)), range = v2(e->cw - 6, 0), dir = PI_F / 2, num = fmaxf(2, e->cw / 14);
  s->prem += num;
  int n = (int)s->prem;
  s->prem -= n;
  particles_emit(PL_MID, &P_SwapBlock_P_Move, n, pos, v2mul(range, 0.5f), dir);
}
static void swap_update(Ent *e) {
  Swap *s = ST(e, Swap);
  plat_update(e);
  s->mid_t += DT;   /* the middle lights' "idle" loop */
  if (s->ret_timer > 0) {
    s->ret_timer -= DT;
    if (s->ret_timer <= 0) s->target = 0, s->speed = 0;
  }
  s->red_alpha = approach(s->red_alpha, s->target != 1 ? 1 : 0, DT * 32);
  if (s->target == 0 && s->lerp == 0) s->mid_t = 0;   /* SetAnimationFrame(0) */
  if (s->target == 1) s->speed = approach(s->speed, s->max_fwd, s->max_fwd / 0.2f * DT);
  else s->speed = approach(s->speed, s->max_back, s->max_back / 1.5f * DT);
  float was = s->lerp;
  s->lerp = approach(s->lerp, s->target, s->speed * DT);
  if (s->lerp != was) {
    V2 d = v2sub(s->end, s->start);
    V2 lift = v2mul(d, s->speed);
    if (s->target == 1) lift = v2mul(d, s->max_fwd);
    if (s->lerp < was) lift = v2mul(lift, -1);
    if (s->target == 1 && level_on_interval(0.02f)) swap_particles(e, d);
    V2 to = v2add(s->start, v2mul(d, s->lerp));
    plat_move_h_lift(e, to.x - (e->x + e->remx), lift.x);   /* MoveTo(position, liftSpeed) */
    plat_move_v_lift(e, to.y - (e->y + e->remy), lift.y);
  }
  if (s->swapping && s->lerp >= 1) s->swapping = 0;
}
/* DrawBlockStyle: a nine-slice of 8x8 cells and the middle light */
static void swap_style(V2 pos, float w, float h, uint16_t nine, uint16_t mid, uint8_t alpha) {
  int nw = (int)(w / 8), nh = (int)(h / 8);
  if (nw >= 2 && nh >= 2 && nw * 8 == w && nh * 8 == h) {   /* the same cells (none over another), in one command */
    gfx_nine(nine, pos.x, pos.y, (int)w, (int)h, false, 0xFFFF, alpha);
    if (mid != 0xFFFF) draw_centered(mid, pos.x + w / 2, pos.y + h / 2, 0xFFFF, alpha, 1, 1, 0);
    return;
  }
  gfx_tex_part(nine, pos.x, pos.y, 0, 0, 8, 8, 0, 0xFFFF, alpha);
  gfx_tex_part(nine, pos.x + w - 8, pos.y, 16, 0, 8, 8, 0, 0xFFFF, alpha);
  gfx_tex_part(nine, pos.x, pos.y + h - 8, 0, 16, 8, 8, 0, 0xFFFF, alpha);
  gfx_tex_part(nine, pos.x + w - 8, pos.y + h - 8, 16, 16, 8, 8, 0, 0xFFFF, alpha);
  for (int i = 1; i < nw - 1; i++) {
    gfx_tex_part(nine, pos.x + i * 8, pos.y, 8, 0, 8, 8, 0, 0xFFFF, alpha);
    gfx_tex_part(nine, pos.x + i * 8, pos.y + h - 8, 8, 16, 8, 8, 0, 0xFFFF, alpha);
  }
  for (int j = 1; j < nh - 1; j++) {
    gfx_tex_part(nine, pos.x, pos.y + j * 8, 0, 8, 8, 8, 0, 0xFFFF, alpha);
    gfx_tex_part(nine, pos.x + w - 8, pos.y + j * 8, 16, 8, 8, 8, 0, 0xFFFF, alpha);
  }
  for (int i = 1; i < nw - 1; i++)
    for (int j = 1; j < nh - 1; j++) gfx_tex_part(nine, pos.x + i * 8, pos.y + j * 8, 8, 8, 8, 8, 0, 0xFFFF, alpha);
  if (mid != 0xFFFF) draw_centered(mid, pos.x + w / 2, pos.y + h / 2, 0xFFFF, alpha, 1, 1, 0);
}
static void swap_render(Ent *e) {
  Swap *s = ST(e, Swap);
  V2 pos = v2(e->x + e->shakex, e->y + e->shakey);
  int f = (int)floorf(s->mid_t / 0.08f) % 4;
  if (s->lerp != s->target && s->speed > 0) {
    V2 dir = safe_norm(v2sub(s->end, s->start), 1);
    if (s->target == 1) dir = v2mul(dir, -1);
    float num2 = 16 * (s->speed / s->max_fwd);
    for (int i = 2; i < num2; i += 2)
      swap_style(v2add(pos, v2mul(dir, (float)i)), e->cw, e->ch, swap_tex[s->moon][0], swap_mid[s->moon][0][f], a8(1 - i / num2));
  }
  if (s->red_alpha < 1) swap_style(pos, e->cw, e->ch, swap_tex[s->moon][0], swap_mid[s->moon][0][f], 255);
  if (s->red_alpha > 0) swap_style(pos, e->cw, e->ch, swap_tex[s->moon][1], swap_mid[s->moon][1][f], a8(s->red_alpha));
}
static const EntClass SWAP = {.name = "swapBlock", .size = sizeof(Swap), .update = swap_update, .render = swap_render,
                              .kind = KIND_SOLID, .more = &(const EntMore){.on_dash = swap_on_dash}};

static void swappath_update(Ent *e) { ST(e, SwapPath)->timer += DT * 4; }
static void swappath_render(Ent *e) {
  SwapPath *p = ST(e, SwapPath);
  Ent *b = ent_at(p->block);
  if (!b->cls || b->cls != &SWAP) return;
  Swap *s = ST(b, Swap);
  if (!s->moon) {
    uint16_t t = swap_path[s->start.x == s->end.x ? 1 : 0];   /* its 16x16 over the move rect, the last ones cut: one command */
    gfx_tiled(t, (float)s->rx, (float)s->ry, s->rw, s->rh, 16, 16, 0xFFFF, 255);
  }
  float num = 0.5f * (0.5f + (sinf(p->timer) + 1) * 0.25f);
  swap_style(v2(s->rx, s->ry), s->rw, s->rh, swap_tex[s->moon][2], 0xFFFF, a8(num));
}
static const EntClass SWAPPATH = {.name = "swapBlockPath", .size = sizeof(SwapPath), .update = swappath_update, .render = swappath_render};

static void new_swap(const EData *d) {
  float w = EA(d, swapBlock, width), h = EA(d, swapBlock, height);
  Ent *e = ent_new(&SWAP, d->x, d->y);
  if (!e) return;
  ent_box(e, w, h, 0, 0);
  e->depth = -9999;
  Swap *s = ST(e, Swap);
  s->moon = !strcmp(EAS(d, swapBlock, theme), "Moon");
  s->start = v2(d->x, d->y);
  s->end = ed_node(d, 0);
  s->max_fwd = 360 / v2len(v2sub(s->start, s->end));
  s->max_back = s->max_fwd * 0.4f;
  s->red_alpha = 1;
  int x0 = (int)fminf(d->x, s->end.x), y0 = (int)fminf(d->y, s->end.y);
  int x1 = (int)fmaxf(d->x + w, s->end.x + w), y1 = (int)fmaxf(d->y + h, s->end.y + h);
  s->rx = (int16_t)x0, s->ry = (int16_t)y0, s->rw = (int16_t)(x1 - x0), s->rh = (int16_t)(y1 - y0);
  if (!swap_tex[0][0]) {
    swap_tex[0][0] = tex_named("objects/swapblock/block");
    swap_tex[0][1] = tex_named("objects/swapblock/blockRed");
    swap_tex[0][2] = tex_named("objects/swapblock/target");
    swap_tex[1][0] = tex_named("objects/swapblock/moon/block");
    swap_tex[1][1] = tex_named("objects/swapblock/moon/blockRed");
    swap_tex[1][2] = tex_named("objects/swapblock/moon/target");
    swap_path[0] = tex_named("objects/swapblock/pathH");
    swap_path[1] = tex_named("objects/swapblock/pathV");
    tex_family("objects/swapblock/midBlock", swap_mid[0][0], 4);
    tex_family("objects/swapblock/midBlockRed", swap_mid[0][1], 4);
    tex_family("objects/swapblock/moon/midBlock", swap_mid[1][0], 4);
    tex_family("objects/swapblock/moon/midBlockRed", swap_mid[1][1], 4);
  }
  Ent *p = ent_new(&SWAPPATH, d->x, d->y);   /* Awake: the PathRenderer */
  if (p) {
    p->depth = 8999;
    p->collidable = 0;
    ST(p, SwapPath)->block = ent_ref(e);
    ST(p, SwapPath)->timer = rndf();
  }
}

/* ---------------------------------------------------------------- TempleCrackedBlock */
typedef struct { uint8_t w, h, persistent, broken; uint32_t hash; float frame; } Cracked;
static uint16_t cracked_tex[10];
static const EntClass CRACKED;
static void cracked_break(Ent *e, V2 from) {
  Cracked *c = ST(e, Cracked);
  if (c->broken) return;
  if (c->persistent) level_set_do_not_load(c->hash);
  c->broken = 1;
  e->collidable = 0;
  int w = c->w, h = c->h;
  float x = e->x, y = e->y;
  for (int i = 0; i < w; i++)
    for (int j = 0; j < h; j++) level_debris(x + i * 8 + 4, y + j * 8 + 4, '1', from);
}
static void cracked_awake(Ent *e) {
  Cracked *c = ST(e, Cracked);
  Ent *pe = level_player_ent();
  if (pe && collide_ent_at(e, e->x, e->y, pe)) {   /* Added: CollideCheck<Player> */
    if (c->persistent) level_set_do_not_load(c->hash);
    ent_remove(e);
  } else
    e->collidable = e->visible = 1;
}
static void cracked_update(Ent *e) {
  Cracked *c = ST(e, Cracked);
  plat_update(e);
  if (c->broken) {
    c->frame += DT * 15;
    if (c->frame >= 10) ent_remove(e);
  }
}
static int cracked_tile(int i, int n) {   /* the cell of the 48x48 picture for tile i of n */
  return (i < n / 2 && i < 2) ? i : (i < n / 2 || i < n - 2) ? 2 + i % 2 : 5 - (n - i - 1);
}
static void cracked_render(Ent *e) {
  Cracked *c = ST(e, Cracked);
  int f = (int)c->frame;
  if (f >= 10) return;
  for (int i = 0; i < c->w; i++)
    for (int j = 0; j < c->h; j++)
      gfx_tex_part(fam_tex(cracked_tex, 10, f), e->x + i * 8, e->y + j * 8, cracked_tile(i, c->w) * 8, cracked_tile(j, c->h) * 8, 8, 8, 0,
                   0xFFFF, 255);
}
static const EntClass CRACKED = {.name = "templeCrackedBlock", .size = sizeof(Cracked), .update = cracked_update,
                                 .render = cracked_render, .awake = cracked_awake, .kind = KIND_SOLID};
static void new_cracked(const EData *d) {
  uint32_t h = level_entity_hash(d);
  float w = EA(d, templeCrackedBlock, width), ht = EA(d, templeCrackedBlock, height);
  Ent *e = ent_new(&CRACKED, d->x, d->y);
  if (!e) return;
  ent_box(e, w, ht, 0, 0);
  e->safe = 1;
  e->collidable = e->visible = 0;
  Cracked *c = ST(e, Cracked);
  c->w = (uint8_t)(w / 8), c->h = (uint8_t)(ht / 8);
  c->persistent = EAB(d, templeCrackedBlock, persistent);
  c->hash = h;
  if (!cracked_tex[0]) tex_family("objects/temple/breakBlock", cracked_tex, 10);
}

/* ---------------------------------------------------------------- SeekerBarrier and its renderer */
typedef struct { float flash, solidify, delay, ptime; uint16_t seed; uint8_t flashing, pad; } Barrier;
static const EntClass BARRIER;
static bool barrier_dirty;
static void barrier_reflect(Ent *e) {   /* OnReflectSeeker */
  Barrier *b = ST(e, Barrier);
  b->flash = b->solidify = b->delay = 1;
  b->flashing = 1;
  float l = e->x, t = e->y, r = e->x + e->cw, bt = e->y + e->ch;
  for (int i = 0; i < g_nents; i++) {
    Ent *o = &g_ents[i];
    if (o == e || o->cls != &BARRIER || o->dead == 1) continue;
    bool adj = collide_rect(o, (float)(int)l, (float)(int)t - 2, (float)(int)l + (int)e->cw, (float)(int)t - 2 + (int)e->ch + 4) ||
               collide_rect(o, (float)(int)l - 2, (float)(int)t, (float)(int)l - 2 + (int)e->cw + 4, (float)(int)t + (int)e->ch);
    (void)r, (void)bt;
    if (adj && !ST(o, Barrier)->flashing) barrier_reflect(o);
  }
}
static void barrier_update(Ent *e) {
  Barrier *b = ST(e, Barrier);
  if (b->flashing) {
    b->flash = approach(b->flash, 0, DT * 4);
    if (b->flash <= 0) b->flashing = 0;
  } else if (b->delay > 0)
    b->delay -= DT;
  else if (b->solidify > 0)
    b->solidify = approach(b->solidify, 0, DT);
  b->ptime += DT;   /* the particles fall: their places come from the seed and the time */
  plat_update(e);
}
static void barrier_removed(Ent *e) {
  (void)e;
  barrier_dirty = true;
}
static const EntClass BARRIER = {.name = "seekerBarrier", .size = sizeof(Barrier), .update = barrier_update,
                                 .removed = barrier_removed, .kind = KIND_SOLID};
static void barriers_collidable(bool on) {
  for (int i = 0; i < g_nents; i++)
    if (g_ents[i].cls == &BARRIER && g_ents[i].dead != 1) g_ents[i].collidable = on;
}

#define MAX_EDGES 40
typedef struct {
  Ent *parent;
  int16_t ax, ay, len;
  int8_t nx, ny, px, py;
  uint8_t visible, pad;
  float wave_t, wave_sol;   /* UpdateWave's time and the parent's Solidify then; -1: no wave yet */
} BEdge;
enum { OFF_edges = ((END_eyes + 7) & ~7), END_edges = OFF_edges + (int)sizeof(BEdge[MAX_EDGES]) };
#define edges (*(BEdge(*)[MAX_EDGES])(void *)(g_chram[5] + OFF_edges))
static int nedges;
static bool barrier_inside(int tx, int ty) {
  for (int i = 0; i < g_nents; i++) {
    Ent *o = &g_ents[i];
    if (o->cls != &BARRIER || o->dead == 1) continue;
    if (tx >= (int)o->x / 8 && tx * 8 < o->x + o->cw && ty >= (int)o->y / 8 && ty * 8 < o->y + o->ch) return true;
  }
  return false;
}
static void edges_rebuild(void) {   /* RebuildEdges */
  barrier_dirty = false;
  nedges = 0;
  static const int8_t DIRS[4][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};
  for (int k = 0; k < g_nents; k++) {
    Ent *it = &g_ents[k];
    if (it->cls != &BARRIER || it->dead == 1) continue;
    for (int i = (int)it->x / 8; i < (it->x + it->cw) / 8; i++)
      for (int j = (int)it->y / 8; j < (it->y + it->ch) / 8; j++)
        for (int d = 0; d < 4; d++) {
          int vx = DIRS[d][0], vy = DIRS[d][1], px = -vy, py = vx;
          if (barrier_inside(i + vx, j + vy) || (barrier_inside(i - px, j - py) && !barrier_inside(i + vx - px, j + vy - py)))
            continue;
          int bx = i + px, by = j + py;
          V2 off = v2(4 + (vx - px) * 4.f, 4 + (vy - py) * 4.f);
          while (barrier_inside(bx, by) && !barrier_inside(bx + vx, by + vy)) bx += px, by += py;
          if (nedges >= MAX_EDGES) return;
          BEdge *ed = &edges[nedges++];
          V2 a = v2sub(v2add(v2(i * 8.f, j * 8.f), off), v2(it->x, it->y));
          V2 b = v2sub(v2add(v2(bx * 8.f, by * 8.f), off), v2(it->x, it->y));
          V2 n = safe_norm(v2sub(b, a), 1);
          ed->parent = it;
          ed->ax = (int16_t)a.x, ed->ay = (int16_t)a.y;
          ed->len = (int16_t)v2len(v2sub(a, b));
          ed->nx = (int8_t)n.x, ed->ny = (int8_t)n.y;
          ed->px = (int8_t)n.y, ed->py = (int8_t)-n.x;   /* -Normal.Perpendicular() */
          ed->visible = 1;
          ed->wave_t = -1;
        }
  }
}
static float edge_wave(const BEdge *ed, int along) {   /* GetWaveAt */
  float len = ed->len;
  if (along <= 1 || along >= len - 1) return 0;
  if (ed->wave_sol >= 1) return 0;
  float num = ed->wave_t + along * 0.25f;
  float num2 = sinf(num) * 2 + sinf(num * 0.25f);
  float yo = along / len;
  yo = yo <= .5f ? yo * 2 : 1 - (yo - .5f) * 2;
  return (1 + num2 * ease_sine_inout(yo)) * (1 - ed->wave_sol);
}
typedef struct { uint8_t pad; } BRenderer;
static void brender_update(Ent *e) {
  (void)e;
  if (barrier_dirty) edges_rebuild();
  float l = g_level.cam.x - 4, t = g_level.cam.y - 4, r = l + 328, b = t + 188;
  for (int i = 0; i < nedges; i++) {
    BEdge *ed = &edges[i];
    if (!ed->parent->cls) continue;
    float x0 = ed->parent->x + fminf(ed->ax, ed->ax + ed->nx * ed->len), x1 = ed->parent->x + fmaxf(ed->ax, ed->ax + ed->nx * ed->len);
    float y0 = ed->parent->y + fminf(ed->ay, ed->ay + ed->ny * ed->len), y1 = ed->parent->y + fmaxf(ed->ay, ed->ay + ed->ny * ed->len);
    bool in = l < x1 && r > x0 && t < y1 && b > y0;
    if (ed->visible) {
      if (on_interval_off(0.25f, i * 0.01f) && !in) ed->visible = 0;
    } else if (on_interval_off(0.05f, i * 0.01f) && in)
      ed->visible = 1;
    if (ed->visible && (on_interval_off(0.05f, i * 0.01f) || ed->wave_t < 0)) {
      ed->wave_t = g_level.time_active * 3;
      ed->wave_sol = ST(ed->parent, Barrier)->solidify;
    }
  }
}
static inline void px_blend(uint16_t *strip, int y0, int y1, int vx, int vy, uint16_t c, int a) {
  if ((unsigned)vx >= VIEW_W || vy < y0 || vy >= y1) return;
  uint16_t *d = strip + (vy - y0) * VIEW_W + vx;
  *d = blend565(*d, scale565(c, a), a);
}
static void fill_blend(uint16_t *strip, int y0, int y1, int x, int y, int w, int h, uint16_t c, int a) {
  int xa = x < 0 ? 0 : x, xb = x + w > VIEW_W ? VIEW_W : x + w, ya = y < y0 ? y0 : y, yb = y + h > y1 ? y1 : y + h;
  uint16_t s = scale565(c, a);
  for (int r = ya; r < yb; r++) {
    uint16_t *d = strip + (r - y0) * VIEW_W + xa;
    for (int i = xa; i < xb; i++, d++) *d = blend565(*d, s, a);
  }
}
/* a little hash: the particles' places (Calc.Random.NextFloat at construction) */
static float hash01(uint32_t seed, uint32_t i) {
  uint32_t h = seed * 0x9E3779B1u ^ (i + 0x7F4A7C15u) * 0x85EBCA6Bu;
  h ^= h >> 15, h *= 0x2C1B3C6Du, h ^= h >> 12, h *= 0x297A2D39u, h ^= h >> 15;
  return (h >> 8) / 16777216.f;
}
static void brender_strip(uint16_t *strip, int y0, int y1, void *ctx) {
  (void)ctx;
  int cx = g_camx, cy = g_camy;
  /* the renderer: the fills and the waving edges, white at 0.15 */
  for (int k = 0; k < g_nents; k++) {
    Ent *b = &g_ents[k];
    if (b->cls == &BARRIER && b->dead != 1 && b->visible)
      fill_blend(strip, y0, y1, (int)b->x - cx, (int)b->y - cy, (int)b->cw, (int)b->ch, 0xFFFF, 38);
  }
  for (int i = 0; i < nedges; i++) {
    BEdge *ed = &edges[i];
    if (!ed->visible || !ed->parent->cls || ed->wave_t < 0) continue;
    int ax = (int)ed->parent->x + ed->ax - cx, ay = (int)ed->parent->y + ed->ay - cy;
    for (int j = 0; j <= ed->len; j++) {
      int x = ax + ed->nx * j, y = ay + ed->ny * j;
      /* Draw.Line(p, p + Perpendicular * wave): a 1-pixel rect turned towards the line */
      float w = edge_wave(ed, j);
      int n = (int)floorf(fabsf(w) + 0.5f), dx = ed->px * sgn(w), dy = ed->py * sgn(w);
      for (int s = 0; s < n; s++) {
        if (dx > 0) px_blend(strip, y0, y1, x + s, y, 0xFFFF, 38);
        else if (dx < 0) px_blend(strip, y0, y1, x - 1 - s, y - 1, 0xFFFF, 38);
        else if (dy > 0) px_blend(strip, y0, y1, x - 1, y + s, 0xFFFF, 38);
        else px_blend(strip, y0, y1, x, y - 1 - s, 0xFFFF, 38);
      }
    }
  }
  /* each barrier: its falling particles (white 0.5), the flash */
  static const float SPEEDS[3] = {12, 20, 40};
  for (int k = 0; k < g_nents; k++) {
    Ent *b = &g_ents[k];
    if (b->cls != &BARRIER || b->dead == 1 || !b->visible) continue;
    Barrier *br = ST(b, Barrier);
    int n = (int)ceilf(b->cw * b->ch / 16);
    if ((int)b->y - cy > y1 || (int)(b->y + b->ch) - cy < y0) continue;
    for (int i = 0; i < n; i++) {
      float x = hash01(br->seed, 2 * i) * (b->cw - 1), y = hash01(br->seed, 2 * i + 1) * (b->ch - 1);
      y = fmodf(y + SPEEDS[i % 3] * br->ptime, b->ch - 1);
      px_blend(strip, y0, y1, (int)floorf(b->x + x + 0.5f) - cx, (int)floorf(b->y + y + 0.5f) - cy, 0xFFFF, 128);
    }
    if (br->flashing) fill_blend(strip, y0, y1, (int)b->x - cx, (int)b->y - cy, (int)b->cw, (int)b->ch, 0xFFFF, (int)(br->flash * 128));
  }
}
static void brender_render(Ent *e) {
  (void)e;
  gfx_custom(brender_strip, NULL, 0, VIEW_H);
}
static const EntClass BRENDERER = {.name = "seekerBarrierRenderer", .size = sizeof(BRenderer), .update = brender_update,
                                   .render = brender_render};
static void new_barrier(const EData *d) {
  if (!first_of(&BRENDERER)) {
    Ent *r = ent_new(&BRENDERER, 0, 0);
    if (!r) return;
    r->tags = TAG_GLOBAL | TAG_TRANSITION_UPDATE;
    r->collidable = 0;
    r->depth = 0;
  }
  Ent *e = ent_new(&BARRIER, d->x, d->y);
  if (!e) return;
  ent_box(e, EA(d, seekerBarrier, width), EA(d, seekerBarrier, height), 0, 0);
  e->collidable = 0;
  Barrier *b = ST(e, Barrier);
  b->seed = (uint16_t)(rndi(65536));
  barrier_dirty = true;
}

/* ---------------------------------------------------------------- ConditionBlock: an ExitBlock when its condition holds */
static void put_attr(uint8_t *b, int off, char kind, float v) {
  switch (kind) {
    case 'b': b[off] = v != 0; break;
    case 'i': {
      int16_t x = (int16_t)v;
      memcpy(b + off, &x, 2);
      break;
    }
    case 'I': {
      int32_t x = (int32_t)v;
      memcpy(b + off, &x, 4);
      break;
    }
    case 'f': memcpy(b + off, &v, 4); break;
  }
}
static void new_condition(const EData *d) {
  const char *mode = EAS(d, conditionBlock, condition), *cid = EAS(d, conditionBlock, conditionID);
  const char *colon = strchr(cid, ':');
  if (!colon) return;
  int room = room_index(cid, (int)(colon - cid)), id = atoi(colon + 1);
  bool ok = false;
  if (!strcmp(mode, "Button")) {
    char flag[48];
    path2(flag, "dashSwitch_", cid, -1);
    ok = level_get_flag(flag);
  } else if (!strcmp(mode, "Strawberry")) {   /* Session.Strawberries */
    int k = room >= 0 ? session_berry_index(room, id) : -1;
    ok = k >= 0 && (g_session.berries >> k & 1);
  } else   /* Key */
    ok = room >= 0 && level_do_not_load(level_entity_id(room, id));
  if (!ok) return;
  uint8_t attrs[EA_exitBlock__size + 4];
  memset(attrs, 0, sizeof attrs);
  put_attr(attrs, EA_exitBlock_width, EK_exitBlock_width, EA(d, conditionBlock, width));
  put_attr(attrs, EA_exitBlock_height, EK_exitBlock_height, EA(d, conditionBlock, height));
  memcpy(attrs + EA_exitBlock_tileType, d->attrs + EA_conditionBlock_tileType, 2);
  EData x = *d;
  x.type = ET_exitBlock;
  x.attrs = attrs;
  x.nnodes = 0;
  ent_create(&x);
}

/* ---------------------------------------------------------------- TempleMirror */
typedef struct { int16_t w, h; int8_t rx, ry; } Mirror;
/* a sprite (scale 1 or -1) drawn inside [l, r) x [t, b) only */
static void spr_draw_clip(const Sprite *s, float x, float y, float l, float t, float r, float b, uint8_t alpha) {
  uint16_t tex = spr_tex(s);
  if (tex == 0xFFFF) return;
  if (!res_cached(tex)) {   /* the animation's frames, in one pass (as spr_draw) */
    uint16_t ids[32], gotos[8];
    int ng, a = s->anim != 255 ? s->anim : s->last_goto;
    if (a >= 0) res_load(ids, bank_anim_textures(s->bank, a, ids, 32, gotos, &ng));
  }
  Tex tx;
  if (!tex_get(tex, &tx)) return;
  float ox = s->justify ? tx.fw * tx.scale * s->jx : s->ox, oy = s->justify ? tx.fh * tx.scale * s->jy : s->oy;
  bool flip = s->sx < 0;
  float L = floorf(x - (flip ? tx.fw - ox : ox)), T = floorf(y - oy);
  int u0, u1;
  if (!flip) u0 = (int)ceilf(l - L), u1 = (int)ceilf(r - L);
  else u0 = (int)ceilf(L + tx.fw - r), u1 = (int)ceilf(L + tx.fw - l);
  int v0 = (int)ceilf(t - T), v1 = (int)ceilf(b - T);
  u0 = clampi(u0, 0, tx.fw), u1 = clampi(u1, 0, tx.fw), v0 = clampi(v0, 0, tx.fh), v1 = clampi(v1, 0, tx.fh);
  if (u1 <= u0 || v1 <= v0) return;
  gfx_tex_part(tex, flip ? L + tx.fw - u1 : L + u0, T + v0, u0, v0, u1 - u0, v1 - v0, flip ? GF_FLIPX : 0, s->tint, alpha);
}
static void mirror_render(Ent *e) {
  Mirror *m = ST(e, Mirror);
  gfx_rect(e->x + 3, e->y + 3, m->w - 6, m->h - 6, rgb(0x05070E), 255);
  /* MirrorSurfaces (depth 9490): what has a MirrorReflection, seen at its place less the surface's ReflectionOffset,
   * inside the surface (x + 2, y + 2, w - 4, h - 4), at half alpha. The NPCs' (Madeline's own is not drawn) */
  for (int i = 0; i < g_nents; i++) {
    Ent *o = &g_ents[i];
    if (!o->cls || o->dead == 1 || strcmp(o->cls->name, "npc")) continue;
    Npc *n = NPC_(o);
    spr_draw_clip(&n->spr, o->x - m->rx, o->y - m->ry, e->x + 2, e->y + 2, e->x + m->w - 2, e->y + m->h - 2, n->spr.alpha / 2);
  }
}
static void mirrorframe_render(Ent *e) {   /* Frame: scenery/templemirror as a nine-slice */
  Mirror *m = ST(e, Mirror);
  uint16_t t = T_scenery_templemirror;
  float x = e->x, y = e->y, w = m->w, h = m->h;
  gfx_tex_part(t, x, y, 0, 0, 8, 8, 0, 0xFFFF, 255);
  gfx_tex_part(t, x + w - 8, y, 16, 0, 8, 8, 0, 0xFFFF, 255);
  gfx_tex_part(t, x, y + h - 8, 0, 16, 8, 8, 0, 0xFFFF, 255);
  gfx_tex_part(t, x + w - 8, y + h - 8, 16, 16, 8, 8, 0, 0xFFFF, 255);
  for (int i = 1; i < w / 8 - 1; i++) {
    gfx_tex_part(t, x + i * 8, y, 8, 0, 8, 8, 0, 0xFFFF, 255);
    gfx_tex_part(t, x + i * 8, y + h - 8, 8, 16, 8, 8, 0, 0xFFFF, 255);
  }
  for (int j = 1; j < h / 8 - 1; j++) {
    gfx_tex_part(t, x, y + j * 8, 0, 8, 8, 8, 0, 0xFFFF, 255);
    gfx_tex_part(t, x + w - 8, y + j * 8, 16, 8, 8, 8, 0, 0xFFFF, 255);
  }
}
static const EntClass MIRROR = {.name = "templeMirror", .size = sizeof(Mirror), .render = mirror_render};
static const EntClass MIRRORFRAME = {.name = "templeMirrorFrame", .size = sizeof(Mirror), .render = mirrorframe_render};
static void new_mirror(const EData *d) {
  int w = (int)EA(d, templeMirror, width), h = (int)EA(d, templeMirror, height);
  Ent *e = ent_new(&MIRROR, d->x, d->y);
  if (!e) return;
  e->depth = 9500;
  ent_box(e, (float)w, (float)h, 0, 0);
  e->collidable = 0;
  ST(e, Mirror)->w = (int16_t)w, ST(e, Mirror)->h = (int16_t)h;
  ST(e, Mirror)->rx = (int8_t)EA(d, templeMirror, reflectX), ST(e, Mirror)->ry = (int8_t)EA(d, templeMirror, reflectY);
  Ent *f = ent_new(&MIRRORFRAME, d->x, d->y);
  if (!f) return;
  f->depth = 8995;
  f->collidable = 0;
  ST(f, Mirror)->w = (int16_t)w, ST(f, Mirror)->h = (int16_t)h;
}
/* ---------------------------------------------------------------- TheoCrystal (Holdable) */
typedef struct {
  ActorExt ext;
  V2 speed, prev_lift, prev_pos;
  float no_grav, swat_timer, hard_cool, gravity_timer, cannot_hold, death_t, co_wait, co_p;
  CSpr spr;
  int32_t idle_depth;
  uint8_t held, on_pedestal, dead, shattering, co, hit_seeker, pad[2];   /* hit_seeker: an entity, 255 none */
} Theo;
static Ent *theo_entity(void) { return first_of(&THEO); }
static void theo_die(Ent *e);
static void seeker_on_holdable(Ent *s, Ent *theo);
static bool seeker_hits_holdable(Ent *s, Ent *theo);
static void bigeye_on_holdable(Ent *b, Ent *theo);
static const EntClass SEEKER, BIGEYE;

static void theo_impact(Ent *e, V2 dir) {   /* ImpactParticles */
  float a;
  V2 at, range;
  if (dir.x > 0) a = PI_F, at = v2(e_right(e), e->y - 4), range = v2(0, 6);
  else if (dir.x < 0) a = 0, at = v2(e_left(e), e->y - 4), range = v2(0, 6);
  else if (dir.y > 0) a = -PI_F / 2, at = v2(e->x, e_bottom(e)), range = v2(6, 0);
  else a = PI_F / 2, at = v2(e->x, e_top(e)), range = v2(6, 0);
  particles_emit(PL_MID, &P_TheoCrystal_P_Impact, 12, at, range, a);
}
static void theo_collide_h(Ent *e, Collision *c) {
  Theo *t = ST(e, Theo);
  if (is_dswitch(c->hit)) dswitch_hit(c->hit, NULL, v2((float)sgn(t->speed.x), 0));
  if (fabsf(t->speed.x) > 100) theo_impact(e, c->dir);
  t->speed.x *= -0.4f;
}
static void theo_collide_v(Ent *e, Collision *c) {
  Theo *t = ST(e, Theo);
  if (is_dswitch(c->hit)) dswitch_hit(c->hit, NULL, v2(0, (float)sgn(t->speed.y)));
  if (t->speed.y > 0) {
    if (t->hard_cool <= 0) t->hard_cool = 0.5f;
  }
  if (t->speed.y > 160) theo_impact(e, c->dir);
  if (t->speed.y > 140 && !(c->hit && c->hit->cls == &SWAP) && !is_dswitch(c->hit)) t->speed.y *= -0.6f;
  else t->speed.y = 0;
}
static bool theo_riding(Ent *e, Ent *s) { return ST(e, Theo)->speed.y == 0 && collide_ent_at(e, e->x, e->y + 1, s); }
static void theo_squish(Ent *e, Collision *c) {
  if (!actor_try_squish_wiggle(e, c)) theo_die(e);
}
static void theo_die(Ent *e) {
  Theo *t = ST(e, Theo);
  if (t->dead) return;
  t->dead = 1;
  Player *p = level_player();
  if (p) player_die(p, v2((float)-p->facing, 0), false);
  t->death_t = 0;
  e->depth = -1000000;
  ents_mark_unsorted();
  e->allow_pushing = 0;
}
/* Holdable.Release */
static void theo_release(Ent *e, V2 force) {
  Theo *t = ST(e, Theo);
  if (collide_solid(e, e->x, e->y)) {
    if (force.x != 0) {
      bool ok = false;
      int s = sgn(force.x), n = 0;
      while (!ok && n++ < 10)
        if (!collide_solid(e, e->x + s * n, e->y)) ok = true;
      if (ok) e->x += s * n;
    }
    while (collide_solid(e, e->x, e->y)) e->y += 1;
  }
  e->depth = t->idle_depth;
  ents_mark_unsorted();
  t->held = 0;
  t->gravity_timer = 0.1f;
  t->cannot_hold = 0.1f;
  /* OnRelease */
  e->tags &= (uint8_t)~TAG_PERSISTENT;
  e->room = (uint8_t)g_level.room_slot;   /* it belongs to the room it is dropped in */
  if (force.x != 0 && force.y == 0) force.y = -0.4f;
  t->speed = v2mul(force, 200);
  if (t->speed.x != 0 || t->speed.y != 0) t->no_grav = 0.1f;
}
/* Player.Swat */
static void theo_swat(Ent *e, Ent *seeker, int dir) {
  Theo *t = ST(e, Theo);
  if (!t->held || t->hit_seeker != 255) return;
  t->swat_timer = 0.1f;
  t->hit_seeker = ent_ref(seeker);
  Player *p = level_player();
  if (p && p->holding == e) {
    p->holding = NULL;
    theo_release(e, v2(0.8f * dir, -0.25f));
  }
}
static bool theo_dangerous(Ent *e, Ent *hc) {
  Theo *t = ST(e, Theo);
  return !t->held && (t->speed.x != 0 || t->speed.y != 0) && t->hit_seeker != ent_ref(hc);
}
static void theo_explode_launch(Ent *e, V2 from) {
  Theo *t = ST(e, Theo);
  if (!t->held) t->speed = safe_norm(v2sub(theo_center(e), from), 120);   /* SlashFx.Burst: not drawn */
}
/* Spring.OnHoldable -> TheoCrystal.HitSpring (springs are entities.c's: their shape tells the side) */
static void theo_springs(Ent *e) {
  Theo *t = ST(e, Theo);
  if (t->held) return;
  for (int i = 0; i < g_nents; i++) {
    Ent *s = &g_ents[i];
    if (!s->cls || s->dead == 1 || !s->collidable || strcmp(s->cls->name, "spring") || !collide_ent_at(e, e->x, e->y, s)) continue;
    if (s->cw == 16) {   /* Floor */
      if (t->speed.y >= 0) t->speed.x *= 0.5f, t->speed.y = -160, t->no_grav = 0.15f;
    } else if (s->cx == 0) {   /* WallLeft */
      if (t->speed.x <= 0) {
        actor_move_towards_y(e, e_cym(s) + 5, 4, NULL);
        t->speed = v2(220, -80), t->no_grav = 0.1f;
      }
    } else if (t->speed.x >= 0) {   /* WallRight */
      actor_move_towards_y(e, e_cym(s) + 5, 4, NULL);
      t->speed = v2(-220, -80), t->no_grav = 0.1f;
    }
  }
}
static void theo_update(Ent *e) {
  Theo *t = ST(e, Theo);
  /* Actor: LiftSpeedGraceTime is 0.1 for Theo */
  if (t->ext.lift_timer > 0.1f) t->ext.lift_timer -= 0.06f;
  actor_update_lift(e);
  /* Holdable.Update */
  if (t->cannot_hold > 0) t->cannot_hold -= DT;
  if (t->gravity_timer > 0) t->gravity_timer -= DT;
  cs_update(&t->spr);
  if (t->dead) {   /* the DeathEffect */
    t->death_t = approach(t->death_t, 1, DT / 0.834f);
    return;
  }
  if (t->shattering) {   /* Shatter() */
    switch (t->co) {
      case 0:
        if (t->co_p < 1) {
          e->x += t->speed.x * (1 - t->co_p) * DT, e->y += t->speed.y * (1 - t->co_p) * DT;
          t->co_p += DT;
          break;
        }
        t->co = 1, t->co_wait = 0.5f;
        /* fall through */
      case 1:
        if ((t->co_wait -= DT) > 0) break;
        level_shake(0.3f);
        cs_play(&t->spr, A_theo_crystal_shatter, false);
        t->co = 2, t->co_wait = 1;
        break;
      case 2:
        if ((t->co_wait -= DT) > 0) break;
        level_shake(0.3f);
        t->co = 3;
        break;
    }
    return;
  }
  if (t->swat_timer > 0) t->swat_timer -= DT;
  t->hard_cool -= DT;
  if (t->on_pedestal) {
    if (e->depth != 8999) e->depth = 8999, ents_mark_unsorted();
    return;
  }
  if (e->depth != 100 && !t->held) e->depth = 100, ents_mark_unsorted();
  Room *rm = g_level.room;
  if (t->held)
    t->prev_lift = v2(0, 0);
  else {
    if (actor_on_ground(e, 1)) {
      float target = !actor_on_ground_at(e, e->x + 3, e->y, 1) ? 20 : actor_on_ground_at(e, e->x - 3, e->y, 1) ? 0 : -20;
      t->speed.x = approach(t->speed.x, target, 800 * DT);
      V2 lift = actor_lift(e);
      if (lift.x == 0 && lift.y == 0 && (t->prev_lift.x != 0 || t->prev_lift.y != 0)) {
        t->speed = t->prev_lift;
        t->prev_lift = v2(0, 0);
        t->speed.y = fminf(t->speed.y * 0.6f, 0);
        if (t->speed.x != 0 && t->speed.y == 0) t->speed.y = -60;
        if (t->speed.y < 0) t->no_grav = 0.15f;
      } else {
        t->prev_lift = lift;
        if (lift.y < 0 && t->speed.y < 0) t->speed.y = 0;
      }
    } else if (t->gravity_timer <= 0) {   /* Hold.ShouldHaveGravity */
      float g = 800, f = 350;
      if (fabsf(t->speed.y) <= 30) g *= 0.5f;
      if (t->speed.y < 0) f *= 0.5f;
      t->speed.x = approach(t->speed.x, 0, f * DT);
      if (t->no_grav > 0) t->no_grav -= DT;
      else t->speed.y = approach(t->speed.y, 200, g * DT);
    }
    t->prev_pos = plat_exact(e);
    actor_move_h(e, t->speed.x * DT, theo_collide_h, NULL);
    actor_move_v(e, ST(e, Theo)->speed.y * DT, theo_collide_v, NULL);
    t = ST(e, Theo);
    if (e->x > rm->x + rm->w) {
      actor_move_h(e, 32 * DT, NULL, NULL);
      if (e_left(e) - 8 > rm->x + rm->w) {
        ent_remove(e);
        return;
      }
    } else if (e_left(e) < rm->x) {
      e->x += rm->x - e_left(e);
      t->speed.x *= -0.4f;
    } else if (e_top(e) < rm->y - 4) {
      e->y += rm->y + 4 - e_top(e);
      t->speed.y = 0;
    } else if (e_top(e) > rm->y + rm->h)
      theo_die(e);
    if (e->x < rm->x + 10) actor_move_h(e, 32 * DT, NULL, NULL);
    Ent *pe = level_player_ent();
    Ent *gate = NULL;   /* CollideFirst<TempleGate> */
    for (int i = 0; i < g_nents && !gate; i++)
      if (g_ents[i].cls == &GATE && g_ents[i].dead != 1 && g_ents[i].collidable && collide_ent_at(e, e->x, e->y, &g_ents[i]))
        gate = &g_ents[i];
    if (gate && pe) {
      gate->collidable = 0;
      actor_move_h(e, (float)(sgn(pe->x - e->x) * 32) * DT, NULL, NULL);
      gate->collidable = 1;
    }
  }
  t = ST(e, Theo);
  if (!t->dead) {   /* Hold.CheckAgainstColliders */
    for (int i = 0; i < g_nents; i++) {
      Ent *o = &g_ents[i];
      if (!o->cls || o->dead == 1) continue;
      if (o->cls == &SEEKER && seeker_hits_holdable(o, e)) seeker_on_holdable(o, e);
      else if (o->cls == &BIGEYE && collide_ent_at(e, e->x, e->y, o)) bigeye_on_holdable(o, e);
    }
    theo_springs(e);
  }
  t = ST(e, Theo);
  if (t->hit_seeker != 255 && t->swat_timer <= 0) {
    Ent *hs = ent_at(t->hit_seeker);
    if (!(hs->cls == &SEEKER && hs->dead != 1 && seeker_hits_holdable(hs, e))) t->hit_seeker = 255;
  }
  /* TODO tutorial: BirdTutorialGui "tutorial_carry" / "tutorial_hold" in e-00 */
}
static void theo_awake(Ent *e) {
  /* Added: a crystal carried in from the last room wins */
  for (int i = 0; i < g_nents; i++) {
    Ent *o = &g_ents[i];
    if (o != e && o->cls == &THEO && o->dead != 1 && ST(o, Theo)->held) ent_remove(e);
  }
}
static void theo_render(Ent *e) {
  Theo *t = ST(e, Theo);
  if (t->dead) {
    death_draw(v2(e->x, e->y - 5), rgb(0x228B22), t->death_t);   /* Color.ForestGreen */
    return;
  }
  cs_draw(&t->spr, e->x, e->y, -1, 1, 0, 0xFFFF, 255);
  light_add(e->x, e->y - 5, 0xFFFFFF, 1, 32, 64);
}
static const EntClass THEO = {.name = "theoCrystal", .size = sizeof(Theo), .update = theo_update, .render = theo_render,
                              .awake = theo_awake, .kind = KIND_ACTOR | KIND_HOLDABLE,
                              .more = &(const EntMore){.on_squish = theo_squish, .is_riding_solid = theo_riding}};
static void new_theo(const EData *d) {
  Ent *e = ent_new(&THEO, d->x, d->y);
  if (!e) return;
  ent_box(e, 8, 10, -4, -10);
  e->depth = 100;
  e->tags = TAG_TRANSITION_UPDATE;
  Theo *t = ST(e, Theo);
  cs_init(&t->spr, SB_theo_crystal);
  t->prev_pos = v2(d->x, d->y);
  t->hit_seeker = 255;
}

/* the player's Holdable hooks (entities.c calls them) */
Ent *level_holdable_check(void) {
  Player *p = level_player();
  if (!p) return NULL;
  for (int i = 0; i < g_nents; i++) {
    Ent *e = &g_ents[i];
    if (e->cls != &THEO || e->dead == 1) continue;
    Theo *t = ST(e, Theo);
    /* Holdable.Check: the PickupCollider, Hitbox(16, 22, -8, -16) */
    if (!collide_rect(p->ent, e->x - 8, e->y - 16, e->x + 8, e->y + 6)) continue;
    if (t->cannot_hold > 0 || t->dead || t->held) continue;
    /* Holdable.Pickup */
    t->idle_depth = e->depth;
    e->depth = p->ent->depth - 1;
    ents_mark_unsorted();
    e->visible = 1;
    t->held = 1;
    t->speed = v2(0, 0);   /* OnPickup */
    e->tags |= TAG_PERSISTENT;
    return e;
  }
  return NULL;
}
/* Level.EnforceBounds: a TheoCrystal about and nothing held keeps the player from leaving right */
bool theo_left_behind(const Player *p) { return g_session.area == 5 && !p->holding && first_of(&THEO); }
void holdable_carry(Ent *h, V2 at) {
  if (h->cls != &THEO) return;
  h->x = at.x, h->y = at.y;
}
void holdable_release(Ent *h, V2 force) {
  if (h->cls == &THEO) theo_release(h, force);
}

/* ---------------------------------------------------------------- TheoCrystalPedestal */
typedef struct { uint8_t dropped; } Pedestal;
static int pedestal_hit(Ent *e, Player *p, V2 dir) {
  (void)p, (void)dir;
  Ent *t = theo_entity();
  if (t) {
    ST(t, Theo)->on_pedestal = 0;
    ST(t, Theo)->speed = v2(0, -300);
  }
  ST(e, Pedestal)->dropped = 1;
  e->collidable = 0;
  level_flash(0xFFFF, false);
  level_freeze(0.1f);
  return DASH_REBOUND;
}
static void pedestal_awake(Ent *e) {
  if (level_get_flag("foundTheoInCrystal")) {
    ST(e, Pedestal)->dropped = 1;
    return;
  }
  Ent *t = theo_entity();
  if (t) t->depth = e->depth + 1, ents_mark_unsorted();   /* (CS05_SaveTheo makes it solid) */
}
static void pedestal_update(Ent *e) {
  Ent *t = theo_entity();
  if (t && !ST(e, Pedestal)->dropped) {
    t->x = e->x, t->y = e->y - 32;
    ST(t, Theo)->on_pedestal = 1;
  }
  plat_update(e);
}
static void pedestal_render(Ent *e) {
  gfx_tex_ex(T_characters_theoCrystal_pedestal, e->x, e->y, 32, 72, 1, 1, 0, 0xFFFF, 255, 0);   /* JustifyOrigin(0.5, 1) */
}
static const EntClass PEDESTAL = {.name = "theoCrystalPedestal", .size = sizeof(Pedestal), .update = pedestal_update,
                                  .render = pedestal_render, .awake = pedestal_awake, .kind = KIND_SOLID,
                                  .more = &(const EntMore){.on_dash_collide = pedestal_hit}};
static void new_pedestal(const EData *d) {
  Ent *e = ent_new(&PEDESTAL, d->x, d->y);
  if (!e) return;
  ent_box(e, 32, 32, -16, -64);
  e->depth = 8998;
  e->collidable = 0;
  e->tags = TAG_TRANSITION_UPDATE;
}
/* ---------------------------------------------------------------- lines against solids */
/* Scene.CollideCheck<Solid>(from, to): Grid.Collide(from, to) for the tiles, RectToLine for the rest */
static int sector(float x, float y, float w, float h, V2 p) {
  int s = 0;
  if (p.x < x) s |= 1;
  else if (p.x >= x + w) s |= 2;
  if (p.y < y) s |= 4;
  else if (p.y >= y + h) s |= 8;
  return s;
}
static bool seg_seg(V2 a1, V2 a2, V2 b1, V2 b2) {   /* Collide.LineCheck */
  V2 a = v2sub(a2, a1), b = v2sub(b2, b1);
  float d = a.x * b.y - a.y * b.x;
  if (d == 0) return false;
  V2 c = v2sub(b1, a1);
  float t = (c.x * b.y - c.y * b.x) / d;
  if (t < 0 || t > 1) return false;
  float u = (c.x * a.y - c.y * a.x) / d;
  return u >= 0 && u <= 1;
}
static bool rect_line(float x, float y, float w, float h, V2 f, V2 t) {   /* Collide.RectToLine */
  int s1 = sector(x, y, w, h, f), s2 = sector(x, y, w, h, t);
  if (!s1 || !s2) return true;
  if (s1 & s2) return false;
  int s = s1 | s2;
  if ((s & 4) && seg_seg(v2(x, y), v2(x + w, y), f, t)) return true;
  if ((s & 8) && seg_seg(v2(x, y + h), v2(x + w, y + h), f, t)) return true;
  if ((s & 1) && seg_seg(v2(x, y), v2(x, y + h), f, t)) return true;
  if ((s & 2) && seg_seg(v2(x + w, y), v2(x + w, y + h), f, t)) return true;
  return false;
}
static bool tile_at(int cx, int cy) { return level_tile_type(0, cx, cy) != '0'; }
static bool grid_line(V2 f, V2 t) {   /* Grid.Collide(from, to) */
  f = v2mul(f, 1 / 8.f), t = v2mul(t, 1 / 8.f);
  bool steep = fabsf(t.y - f.y) > fabsf(t.x - f.x);
  if (steep) f = v2(f.y, f.x), t = v2(t.y, t.x);
  if (f.x > t.x) {
    V2 k = f;
    f = t, t = k;
  }
  float err = 0, slope = fabsf(t.y - f.y) / (t.x - f.x);
  int ys = f.y < t.y ? 1 : -1, y = (int)floorf(f.y), xe = (int)floorf(t.x);
  for (int x = (int)floorf(f.x); x <= xe; x++) {
    if (steep ? tile_at(y, x) : tile_at(x, y)) return true;
    err += slope;
    if (err >= 0.5f) y += ys, err -= 1;
  }
  return false;
}
static bool line_solid(V2 f, V2 t) {
  if (grid_line(f, t)) return true;
  for (int i = 0; i < g_nents; i++) {
    Ent *o = &g_ents[i];
    if (!(o->kind & KIND_SOLID) || !o->cls || o->dead == 1 || !o->collidable || o->ctype != COL_BOX) continue;
    if (rect_line(o->x + o->cx, o->y + o->cy, o->cw, o->ch, f, t)) return true;
  }
  return false;
}

/* ---------------------------------------------------------------- Pathfinder (Level.Pathfinder.Find) */
/* the map lives in free texture-cache RAM while it runs (res_scratch): cost (28 bits), solid, parent direction */
#define PF_INF 0x0FFFFFFFu
static const int8_t PF_DIR[4][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
#define SEEKER_TAB 6
#define SEEKER_PATH 32
#define SEEKER_NODES 96
typedef struct {
  Ent *owner;
  uint8_t node0, nnodes, npath, used;
  int16_t path[SEEKER_PATH][2];
} SeekerTab;
enum { OFF_seeker_tab = ((END_edges + 7) & ~7), END_seeker_tab = OFF_seeker_tab + (int)sizeof(SeekerTab[SEEKER_TAB]) };
#define seeker_tab (*(SeekerTab(*)[SEEKER_TAB])(void *)(g_chram[5] + OFF_seeker_tab))
enum { OFF_seeker_nodes = ((END_seeker_tab + 7) & ~7), END_seeker_nodes = OFF_seeker_nodes + (int)sizeof(int16_t[SEEKER_NODES][2]) };
#define seeker_nodes (*(int16_t(*)[SEEKER_NODES][2])(void *)(g_chram[5] + OFF_seeker_nodes))
static int nseeker_nodes;

static int pf_find(SeekerTab *tab, V2 from, V2 to) {
  Room *rm = g_level.room;
  int ox = rm->x / 8, oy = rm->y / 8, w = rm->w / 8, h = rm->h / 8;
  uint32_t cap, need = (uint32_t)(w * h) * 4;
  uint8_t *mem = res_scratch(need + 2048, &cap);   /* textures not drawn last frame make way */
  if (need + 256 > cap) return 0;   /* the open list takes what is left */
  uint32_t *map = (uint32_t *)(void *)mem;
  uint16_t *active = (uint16_t *)(void *)(mem + need);
  int maxact = (int)((cap - need) / 2);
  for (int j = 0; j < h; j++)
    for (int i = 0; i < w; i++) map[j * w + i] = PF_INF | (tile_at(ox + i, oy + j) ? 1u << 28 : 0);
  for (int k = 0; k < g_nents; k++) {
    Ent *o = &g_ents[k];
    if (!(o->kind & KIND_SOLID) || !o->cls || o->dead == 1 || !o->collidable || o->ctype != COL_BOX) continue;
    for (int i = (int)floorf(e_left(o) / 8); i < (int)ceilf(e_right(o) / 8); i++)
      for (int j = (int)floorf(e_top(o) / 8); j < (int)ceilf(e_bottom(o) / 8); j++) {
        int x = i - ox, y = j - oy;
        if (x >= 0 && y >= 0 && x < w && y < h) map[y * w + x] |= 1u << 28;
      }
  }
#define SOLID(i) (map[i] >> 28 & 1)
#define COST(i) (map[i] & PF_INF)
#define PAR(i) ((int)(map[i] >> 29))   /* 0: none, 1 + direction it came along */
  int sx = (int)floorf(from.x / 8) - ox, sy = (int)floorf(from.y / 8) - oy;
  int ex = (int)floorf(to.x / 8) - ox, ey = (int)floorf(to.y / 8) - oy;
  if (sx < 0 || sy < 0 || sx >= w || sy >= h || ex < 0 || ey < 0 || ex >= w || ey >= h) return 0;
  if (SOLID(sy * w + sx) || SOLID(ey * w + ex)) return 0;
  int na = 0;
  active[na++] = (uint16_t)(sy * w + sx);
  map[sy * w + sx] &= ~PF_INF;
  bool found = false;
  while (na > 0 && !found) {
    int cur = active[--na], cx = cur % w, cy = cur / w;
    for (int m = 0; m < 4; m++) {
      int nx = cx + PF_DIR[m][0], ny = cy + PF_DIR[m][1];
      if (nx < 0 || ny < 0 || nx >= w || ny >= h || SOLID(ny * w + nx)) continue;
      uint32_t add = 1;
      for (int n = 0; n < 4; n++) {
        int qx = nx + PF_DIR[n][0], qy = ny + PF_DIR[n][1];
        if (qx >= 0 && qy >= 0 && qx < w && qy < h && SOLID(qy * w + qx)) {
          add = 7;
          break;
        }
      }
      if (PAR(cur)) {   /* fewerTurns */
        int pd = PAR(cur) - 1, px = cx - PF_DIR[pd][0], py = cy - PF_DIR[pd][1];
        if (nx != px && ny != py) add += 4;
      }
      uint32_t cost = COST(cur);
      if (PF_DIR[m][1] != 0) add += (uint32_t)(int)((float)cost * 0.5f);
      uint32_t nc = cost + add;
      if (nc >= PF_INF) continue;
      int ni = ny * w + nx;
      if (COST(ni) > nc) {
        map[ni] = (map[ni] & (1u << 28)) | nc | (uint32_t)(m + 1) << 29;
        /* List.BinarySearch with the comparer cost(b) - cost(a): costs fall along the list */
        int lo = 0, hi = na - 1, at = -1;
        while (lo <= hi) {
          int mid = lo + ((hi - lo) >> 1);
          int64_t c = (int64_t)COST(ni) - (int64_t)COST(active[mid]);
          if (c == 0) {
            at = mid;
            break;
          }
          if (c < 0) lo = mid + 1;
          else hi = mid - 1;
        }
        if (at < 0) at = lo;
        if (na >= maxact) return 0;
        memmove(active + at + 1, active + at, (size_t)(na - at) * 2);
        active[at] = (uint16_t)ni;
        na++;
        if (nx == ex && ny == ey) {
          found = true;
          break;
        }
      }
    }
  }
  if (!found) return 0;
  /* back from the end (the start tile left out); collinear points are removed, then the list is reversed */
  int16_t pts[SEEKER_PATH][2];
  int np = 0, guard = 0, x = ex, y = ey;
  while ((x != sx || y != sy) && guard++ < 1000) {
    int16_t px = (int16_t)((x + 0.5f) * 8 + rm->x), py = (int16_t)((y + 0.5f) * 8 + rm->y);
    if (np >= 2 && ((pts[np - 2][0] == pts[np - 1][0] && pts[np - 1][0] == px) || (pts[np - 2][1] == pts[np - 1][1] && pts[np - 1][1] == py)))
      np--;
    if (np >= SEEKER_PATH) return 0;
    pts[np][0] = px, pts[np][1] = py, np++;
    int d = PAR(y * w + x) - 1;
    if (d < 0) return 0;
    x -= PF_DIR[d][0], y -= PF_DIR[d][1];
  }
  if (guard >= 1000) return 0;
  for (int i = 0; i < np; i++) tab->path[i][0] = pts[np - 1 - i][0], tab->path[i][1] = pts[np - 1 - i][1];
  tab->npath = (uint8_t)np;
  return 1;
#undef SOLID
#undef COST
#undef PAR
}

/* ---------------------------------------------------------------- Seeker */
enum { SK_IDLE, SK_PATROL, SK_SPOTTED, SK_ATTACK, SK_STUNNED, SK_SKIDDING, SK_REGEN, SK_RETURNED };
typedef struct {
  ActorExt ext;
  V2 speed, last_spotted, last_path_to;
  float sx, sy, light, light_r;
  float patrol_wait, lose_timer, turn_delay, attack_speed, co_wait;
  float sinex, siney;
  Wiggler wig;
  Shaker shaker;
  CSpr spr;
  uint16_t rnd_calls;
  uint8_t state, co, locked, next, tab, path_idx;
  int8_t facing, sprite_facing;
  uint8_t spotted, can_see, path_found, wind_up, strong_skid, bounce_w;
  int8_t bounce_x, pad;
  uint8_t pc[2], pad2[2];   /* the PlayerCollider entities */
} Seeker;
static SeekerTab *seeker_tab_of(Seeker *s) { return &seeker_tab[s->tab]; }
static int tab_alloc(Ent *owner, const EData *d) {
  for (int i = 0; i < SEEKER_TAB; i++) {
    if (seeker_tab[i].used) continue;
    SeekerTab *t = &seeker_tab[i];
    memset(t, 0, sizeof *t);
    t->used = 1;
    t->owner = owner;
    t->node0 = (uint8_t)nseeker_nodes;
    for (int k = 0; k < d->nnodes && nseeker_nodes < SEEKER_NODES; k++) {
      V2 n = ed_node(d, k);
      seeker_nodes[nseeker_nodes][0] = (int16_t)n.x, seeker_nodes[nseeker_nodes][1] = (int16_t)n.y;
      nseeker_nodes++, t->nnodes++;
    }
    return i;
  }
  return -1;
}
static void tab_free(int i) {
  SeekerTab *t = &seeker_tab[i];
  if (!t->used) return;
  int a = t->node0, n = t->nnodes;
  memmove(seeker_nodes + a, seeker_nodes + a + n, sizeof(seeker_nodes[0]) * (size_t)(nseeker_nodes - a - n));
  nseeker_nodes -= n;
  for (int k = 0; k < SEEKER_TAB; k++)
    if (seeker_tab[k].used && seeker_tab[k].node0 > a) seeker_tab[k].node0 = (uint8_t)(seeker_tab[k].node0 - n);
  t->used = 0;
}
static V2 seeker_center(Ent *e) { return v2(e->x, e->y); }   /* the physics hitbox is centred */
static V2 follow_target(Seeker *s) { return v2(s->last_spotted.x, s->last_spotted.y - 2); }
static void seeker_set_state(Ent *e, int st);
static void seeker_play(Seeker *s, int anim, bool restart);
static void seeker_last_frame(Seeker *s, int anim) {   /* sprite.OnLastFrame */
  if ((anim == A_seeker_flipMouth || anim == A_seeker_flipEyes || anim == A_seeker_skid) && s->sprite_facing != s->facing) {
    s->sprite_facing = s->facing;
    if (s->next != 255) {
      int n = s->next;
      s->next = 255;
      seeker_play(s, n, false);
    }
  }
}
static void seeker_play(Seeker *s, int anim, bool restart) {
  if (cs_cur(&s->spr) != anim || restart) {   /* OnChange(LastAnimationID, id) */
    s->next = 255;
    seeker_last_frame(s, s->spr.last);
  }
  cs_play(&s->spr, anim, restart);
}
static void turn_facing(Seeker *s, float dir, int goto_anim) {
  if (dir != 0) s->facing = (int8_t)sgn(dir);
  if (s->sprite_facing != s->facing) {
    if (s->state == SK_SKIDDING) seeker_play(s, A_seeker_skid, false);
    else if (s->state == SK_ATTACK || s->state == SK_SPOTTED) seeker_play(s, A_seeker_flipMouth, false);
    else seeker_play(s, A_seeker_flipEyes, false);
    s->next = (uint8_t)(goto_anim < 0 ? 255 : goto_anim);
  } else if (goto_anim >= 0)
    seeker_play(s, goto_anim, false);
}
static void snap_facing(Seeker *s, float dir) {
  if (dir != 0) s->sprite_facing = s->facing = (int8_t)sgn(dir);
}
static bool seeker_can_see(Ent *e, Player *p) {
  if (!p) return false;
  Seeker *s = ST(e, Seeker);
  V2 c = seeker_center(e), pc = player_center(p);
  V2 cam = g_level.cam;
  bool inside = c.x >= cam.x && c.x < cam.x + 320 && c.y >= cam.y && c.y < cam.y + 180;
  if (s->state != SK_SPOTTED && !inside && dist2(c, pc) > 25600) return false;
  V2 d = v2sub(pc, c);
  V2 v = safe_norm(v2(-d.y, d.x), 2);
  if (!line_solid(v2add(c, v), v2add(pc, v))) return !line_solid(v2sub(c, v), v2sub(pc, v));
  return false;
}
static V2 path_speed(Ent *e, float mag) {   /* GetPathSpeed */
  Seeker *s = ST(e, Seeker);
  SeekerTab *t = seeker_tab_of(s);
  while (s->path_idx < t->npath) {
    V2 p = v2(t->path[s->path_idx][0], t->path[s->path_idx][1]);
    if (dist2(seeker_center(e), p) < 36) {
      s->path_idx++;
      continue;
    }
    return safe_norm(v2sub(p, seeker_center(e)), mag);
  }
  return v2(0, 0);
}
static float speed_mag(Ent *e, float base) {
  Player *p = level_player();
  if (p) return dist2(seeker_center(e), player_center(p)) > 12544 ? base * 3 : base * 1.5f;
  return base;
}
static void seeker_find_path(Ent *e) {
  Seeker *s = ST(e, Seeker);
  s->last_path_to = s->last_spotted;
  s->path_idx = 0;
  s->path_found = (uint8_t)pf_find(seeker_tab_of(s), seeker_center(e), follow_target(s));
}
static int choose_patrol(Ent *e) {
  Seeker *s = ST(e, Seeker);
  Player *p = level_player();
  if (!p) return SK_IDLE;
  SeekerTab *t = seeker_tab_of(s);
  V2 pt[3];
  float dd[3] = {0, 0, 0};
  int num = 0;
  for (int i = 0; i < t->nnodes; i++) {
    V2 v = v2(seeker_nodes[t->node0 + i][0], seeker_nodes[t->node0 + i][1]);
    if (dist2(seeker_center(e), v) < 576) continue;
    float d2 = dist2(v, player_center(p));
    for (int k = 0; k < 3; k++)
      if (d2 < dd[k] || dd[k] <= 0) {
        num++;
        for (int j = 2; j > k; j--) dd[j] = dd[j - 1], pt[j] = pt[j - 1];
        dd[k] = d2, pt[k] = v;
        break;
      }
  }
  if (num <= 0) return SK_IDLE;
  /* random = new Random(LevelData.LoadSeed): replayed up to this call */
  NetRandom r;
  int seed = 0;
  for (const char *c = room_name(g_level.room->index); *c; c++) seed += (uint8_t)*c;
  rnd_seed(&r, seed);
  for (int i = 0; i < s->rnd_calls; i++) rnd_next(&r);
  s->rnd_calls++;
  s->last_spotted = pt[rnd_range(&r, num < 3 ? num : 3)];
  seeker_find_path(e);
  return SK_PATROL;
}
static void seeker_trail(Ent *e) {
  Seeker *s = ST(e, Seeker);
  float k = 1 - 0.3f * s->wig.value;
  uint16_t t = cs_tex(&s->spr);
  if (t != 0xFFFF) trail_add(e->x, e->y, t, 48, 48, s->sx * k * s->sprite_facing, s->sy * k, 0x99E550, 0.5f, e->depth + 1);
}
static bool seeker_can_attack(Ent *e) {
  Seeker *s = ST(e, Seeker);
  if (fabsf(e->y - s->last_spotted.y) > 24) return false;
  if (fabsf(e->x - s->last_spotted.x) < 16) return false;
  V2 v = safe_norm(v2sub(follow_target(s), seeker_center(e)), 1);
  if (-v.y > 0.5f || v.y > 0.5f) return false;
  if (collide_solid(e, e->x + sgn(s->last_spotted.x - e->x) * 24, e->y)) return false;
  return true;
}
/* the state machine's update callbacks */
static int seeker_state_update(Ent *e) {
  Seeker *s = ST(e, Seeker);
  switch (s->state) {
    case SK_IDLE: {
      if (s->can_see) return SK_SPOTTED;
      V2 v = v2(0, 0);
      if (s->spotted && dist2(seeker_center(e), follow_target(s)) > 64) {
        float m = speed_mag(e, 50);
        v = s->path_found ? path_speed(e, m) : safe_norm(v2sub(follow_target(s), seeker_center(e)), m);
      }
      if (v.x == 0 && v.y == 0) v = v2(sinf(s->sinex) * 6, sinf(s->siney) * 6);
      s->speed = approach_v(s->speed, v, 200 * DT);
      if (s->speed.x * s->speed.x + s->speed.y * s->speed.y > 400) turn_facing(s, s->speed.x, -1);
      if (s->sprite_facing == s->facing) seeker_play(s, A_seeker_idle, false);
      return SK_IDLE;
    }
    case SK_PATROL: {
      if (s->can_see) return SK_SPOTTED;
      if (s->patrol_wait > 0) {
        s->patrol_wait -= DT;
        if (s->patrol_wait <= 0) return choose_patrol(e);
      } else if (dist2(seeker_center(e), s->last_spotted) < 144)
        s->patrol_wait = 0.4f;
      s = ST(e, Seeker);
      float m = speed_mag(e, 25);
      V2 t = s->path_found ? path_speed(e, m) : safe_norm(v2sub(follow_target(s), seeker_center(e)), m);
      s->speed = approach_v(s->speed, t, 600 * DT);
      if (s->speed.x * s->speed.x + s->speed.y * s->speed.y > 100) turn_facing(s, s->speed.x, -1);
      if (s->sprite_facing == s->facing) seeker_play(s, A_seeker_search, false);
      return SK_PATROL;
    }
    case SK_SPOTTED: {
      if (!s->can_see) {
        s->lose_timer -= DT;
        if (s->lose_timer < 0) return SK_IDLE;
      } else
        s->lose_timer = 0.6f;
      float m = speed_mag(e, 60);
      V2 ft = follow_target(s), c = seeker_center(e);
      V2 v = s->path_found ? path_speed(e, m) : safe_norm(v2sub(ft, c), m);
      if (dist2(c, ft) < 2500 && e->y < ft.y) {
        float a = vangle(v);
        if (e->y < ft.y - 2) a = angle_lerp(a, PI_F / 2, 0.5f);
        else if (e->y > ft.y + 2) a = angle_lerp(a, -PI_F / 2, 0.5f);
        v = angle_vec(a, 60);
        float off = sgn(e->x - s->last_spotted.x) * 48.f;
        if (fabsf(e->x - s->last_spotted.x) < 36 && !collide_solid(e, e->x + off, e->y)) {
          /* CollideCheck<Solid>(lastSpottedAt + val2): the seeker moved there */
          if (!collide_solid(e, s->last_spotted.x + off, s->last_spotted.y)) v.x = (float)(sgn(e->x - s->last_spotted.x) * 60);
        }
      }
      s->speed = approach_v(s->speed, v, 600 * DT);
      s->turn_delay -= DT;
      if (s->turn_delay <= 0) turn_facing(s, s->speed.x, A_seeker_spotted);
      return SK_SPOTTED;
    }
    case SK_ATTACK:
      if (!s->wind_up) {
        V2 v = safe_norm(v2sub(follow_target(s), seeker_center(e)), 1);
        V2 sn = safe_norm(s->speed, 1);
        if (sn.x * v.x + sn.y * v.y < 0.4f) return SK_SKIDDING;
        s->attack_speed = approach(s->attack_speed, 260, 300 * DT);
        float a = angle_approach(vangle(s->speed), vangle(v), 0.61086524f * DT);
        s->speed = safe_norm(angle_vec(a, v2len(s->speed)), s->attack_speed);
        if (level_on_interval(0.04f)) {
          V2 b = safe_norm(v2mul(s->speed, -1), 1);
          particles_emit(PL_MID, &P_Seeker_P_Attack, 2, v2add(v2(e->x, e->y), v2mul(b, 4)), v2(4, 4), vangle(b));
        }
        if (level_on_interval(0.06f)) seeker_trail(e);
      }
      return SK_ATTACK;
    case SK_STUNNED: s->speed = approach_v(s->speed, v2(0, 0), 150 * DT); return SK_STUNNED;
    case SK_SKIDDING:
      s->speed = approach_v(s->speed, v2(0, 0), (s->strong_skid ? 400 : 200) * DT);
      if (s->speed.x * s->speed.x + s->speed.y * s->speed.y < 400) return s->can_see ? SK_SPOTTED : SK_IDLE;
      return SK_SKIDDING;
    case SK_REGEN:
      s->speed.x = approach(s->speed.x, 0, 150 * DT);
      s->speed = approach_v(s->speed, v2(0, 0), 150 * DT);
      return SK_REGEN;
  }
  return s->state;
}
static const EntClass RECOVER;
static void seeker_explode_push(Ent *e);
/* the state's coroutine: one step a frame unless waiting */
static void seeker_coroutine(Ent *e) {
  Seeker *s = ST(e, Seeker);
  if (!s->co) return;
  if (s->co_wait > 0) {
    s->co_wait -= DT;
    return;
  }
  int st = s->state, step = s->co++;
  switch (st) {
    case SK_IDLE: {   /* IdleCoroutine */
      SeekerTab *t = seeker_tab_of(s);
      if (step == 1 && !(t->nnodes && s->spotted)) {
        s->co = 0;
        break;
      }
      if (step <= 2) {
        if (dist2(seeker_center(e), follow_target(s)) > 64) s->co = 2;
        else s->co_wait = 0.3f, s->co = 3;
        break;
      }
      s->co = 0;
      seeker_set_state(e, SK_PATROL);
      break;
    }
    case SK_SPOTTED:
      if (step == 1) s->co_wait = 0.2f;
      else if (!seeker_can_attack(e)) s->co = 2;
      else {
        s->co = 0;
        seeker_set_state(e, SK_ATTACK);
      }
      break;
    case SK_ATTACK:
      if (step == 1) {
        turn_facing(s, s->last_spotted.x - e->x, A_seeker_windUp);
        s->co_wait = 0.3f;
      } else {
        s->wind_up = 0;
        s->attack_speed = 180;
        s->speed = safe_norm(v2sub(v2sub(s->last_spotted, v2(0, 2)), seeker_center(e)), 180);
        snap_facing(s, s->speed.x);
        s->co = 0;
      }
      break;
    case SK_STUNNED:
      if (step == 1) s->co_wait = 0.8f;
      else {
        s->co = 0;
        seeker_set_state(e, SK_IDLE);
      }
      break;
    case SK_SKIDDING:
      if (step == 1) s->co_wait = 0.08f;
      else s->strong_skid = 1, s->co = 0;
      break;
    case SK_REGEN:
      switch (step) {
        case 1: s->co_wait = 1; break;
        case 2: s->shaker.on = 1, s->co_wait = 0.2f; break;
        case 3: seeker_play(s, A_seeker_pulse, false), s->co_wait = 0.5f; break;
        case 4: {
          seeker_play(s, A_seeker_recover, false);
          Ent *b = ent_new(&RECOVER, e->x, e->y);   /* RecoverBlast.Spawn */
          if (b) {
            b->depth = -199;
            b->collidable = 0;
            cs_init(ST(b, CSpr), SB_seekerShockWave);
            cs_play(ST(b, CSpr), A_seekerShockWave_shockwave, true);
          }
          ST(e, Seeker)->co_wait = 0.15f;
          break;
        }
        default:
          seeker_explode_push(e);
          s = ST(e, Seeker);
          s->shaker.on = 0, s->shaker.x = s->shaker.y = 0, s->shaker.timer = 0;
          s->locked = 0;
          s->co = 0;
          seeker_set_state(e, SK_RETURNED);
          break;
      }
      break;
    case SK_RETURNED:
      if (step == 1) s->co_wait = 0.3f;
      else {
        s->co = 0;
        seeker_set_state(e, SK_IDLE);
      }
      break;
    default: s->co = 0; break;
  }
}
static void seeker_set_state(Ent *e, int st) {
  Seeker *s = ST(e, Seeker);
  if (s->locked || s->state == st) return;
  int prev = s->state;
  s->state = (uint8_t)st;
  /* ends */
  if (prev == SK_SKIDDING) s->sprite_facing = s->facing;
  if (prev == SK_REGEN) {
    e->collidable = 1;
    s->light_r = 0;   /* Light 32 / 64 */
  }
  /* coroutine: replaced (or cancelled) */
  bool has_co = st == SK_IDLE || st == SK_SPOTTED || st == SK_ATTACK || st == SK_STUNNED || st == SK_SKIDDING || st == SK_REGEN ||
                st == SK_RETURNED;
  s->co = has_co ? 1 : 0;
  s->co_wait = 0;
  /* begins */
  switch (st) {
    case SK_PATROL: {
      int to = choose_patrol(e);
      ST(e, Seeker)->patrol_wait = 0;
      if (to != SK_PATROL) seeker_set_state(e, to);
      break;
    }
    case SK_SPOTTED: {
      Player *p = level_player();
      if (p) turn_facing(s, p->ent->x - e->x, A_seeker_spot);
      s->lose_timer = 0.6f;
      s->turn_delay = 1;
      break;
    }
    case SK_ATTACK:
      s->wind_up = 1;
      s->attack_speed = -60;
      s->speed = safe_norm(v2sub(follow_target(s), seeker_center(e)), -60);
      break;
    case SK_SKIDDING:
      s->strong_skid = 0;
      turn_facing(s, (float)-s->facing, -1);
      break;
    case SK_REGEN:
      seeker_play(s, A_seeker_takeHit, false);
      e->collidable = 0;
      s->locked = 1;
      s->light_r = 1;   /* Light 16 / 32 */
      break;
  }
}
/* the regeneration's blast: the player, Theo, cracked walls, touch switches */
static void seeker_explode_push(Ent *e) {
  V2 c = v2(e->x, e->y);
  Player *p = level_player();
  /* Collider = pushRadius (Circle 40): CollideFirst<Player> */
  if (p && !p->dead) {
    float nx = clampf(c.x, e_left(p->ent), e_right(p->ent)), ny = clampf(c.y, e_top(p->ent), e_bottom(p->ent));
    if ((nx - c.x) * (nx - c.x) + (ny - c.y) * (ny - c.y) < 1600 && !line_solid(c, player_center(p))) player_explode_launch(p, c, true);
  }
  Ent *th = theo_entity();
  if (th) {
    float nx = clampf(c.x, e_left(th), e_right(th)), ny = clampf(c.y, e_top(th), e_bottom(th));
    if ((nx - c.x) * (nx - c.x) + (ny - c.y) * (ny - c.y) < 1600 && !line_solid(c, theo_center(th))) theo_explode_launch(th, c);
  }
  for (int i = 0; i < g_nents; i++) {
    Ent *o = &g_ents[i];
    if (o->cls != &CRACKED || o->dead == 1 || !o->collidable) continue;
    float nx = clampf(c.x, e_left(o), e_right(o)), ny = clampf(c.y, e_top(o), e_bottom(o));
    if ((nx - c.x) * (nx - c.x) + (ny - c.y) * (ny - c.y) < 1600) cracked_break(o, c);
  }
  level_touch_switches_in_circle(c, 40);
  for (float a = 0; a < PI_F * 2; a += 0.17453292f) {
    V2 at = v2add(c, angle_vec(a + rnd_rangef(-PI_F / 90, PI_F / 90), (float)(12 + rndi(6))));
    particles_emit1(PL_MID, &P_Seeker_P_Regen, at, a);
  }
}
static void seeker_slammed(Ent *e, Collision *c) {
  Seeker *s = ST(e, Seeker);
  float dir, x;
  if (c->dir.x > 0) dir = PI_F, x = e_right(e);
  else dir = 0, x = e_left(e);
  particles_emit(PL_MID, &P_Seeker_P_HitWall, 12, v2(x, e->y), v2(0, 4), dir);
  int sx = sgn(s->speed.x);
  if (is_dswitch(c->hit)) dswitch_hit(c->hit, NULL, v2((float)sx, 0));
  /* breakWallsHitbox: Hitbox(6, 14, -3, -7) */
  for (int i = 0; i < g_nents; i++) {
    Ent *o = &g_ents[i];
    if (o->cls != &CRACKED || o->dead == 1 || !o->collidable) continue;
    if (collide_rect(o, e->x - 3 + sx, e->y - 7, e->x + 3 + sx, e->y + 7)) cracked_break(o, v2(e->x, e->y));
  }
  s = ST(e, Seeker);
  level_dir_shake(v2((float)sx, 0), 0.3f);
  s->speed.x = sx * -100.f;
  s->speed.y *= 0.4f;
  s->sx = 0.6f, s->sy = 1.4f;
  shaker_for(&s->shaker, 0.5f);
  wiggler_restart(&s->wig);
  seeker_set_state(e, SK_STUNNED);
  if (c->hit && c->hit->cls == &BARRIER) barrier_reflect(c->hit);
}
static void seeker_collide_h(Ent *e, Collision *c) {
  Seeker *s = ST(e, Seeker);
  if (s->state == SK_ATTACK && c->hit) {
    int n = sgn(s->speed.x);
    if ((!collide_solid(e, e->x + n, e->y + 4) && !actor_move_v_exact(e, 4, NULL, NULL)) ||
        (!collide_solid(e, e->x + n, e->y - 4) && !actor_move_v_exact(e, -4, NULL, NULL)))
      return;
  }
  s = ST(e, Seeker);
  if ((s->state == SK_ATTACK || s->state == SK_SKIDDING) && fabsf(s->speed.x) >= 100) seeker_slammed(e, c);
  else s->speed.x *= -0.2f;
}
static void seeker_collide_v(Ent *e, Collision *c) {
  (void)c;
  Seeker *s = ST(e, Seeker);
  if (s->state == SK_ATTACK) s->speed.y *= -0.6f;
  else s->speed.y *= -0.2f;
}
static void seeker_got_bounced(Ent *e, V2 from) {
  Seeker *s = ST(e, Seeker);
  level_freeze(0.15f);
  V2 c = v2(e->x, e->y + 2);   /* Center with the attack hitbox */
  s->speed = safe_norm(v2sub(c, from), 200);
  seeker_set_state(e, SK_REGEN);
  s = ST(e, Seeker);
  s->sx = 1.4f, s->sy = 0.6f;
  particles_emit(PL_MID, &P_Seeker_P_Stomp, 8, v2(c.x, c.y - 5), v2(6, 3), 0);
}
/* attackHitbox Hitbox(12, 8, -6, -2), bounceHitbox Hitbox(12 or 16, 6, x, -8) */
static void seeker_attack_player(Ent *e, Player *p) {
  Seeker *s = ST(e, Seeker);
  if (s->state != SK_STUNNED) {
    player_die(p, safe_norm(v2sub(player_center(p), v2(e->x, e->y)), 1), false);
    return;
  }
  V2 bc = v2(e->x + s->bounce_x + s->bounce_w / 2.f, e->y - 5);
  player_point_bounce(p, bc);
  s->speed = safe_norm(v2sub(bc, player_center(p)), 100);
  wiggler_restart(&s->wig);
}
static void seeker_bounce_player(Ent *e, Player *p) {   /* OnBouncePlayer */
  if (player_hits(e->x - 6, e->y - 2, e->x + 6, e->y + 6)) seeker_attack_player(e, p);
  else {
    player_bounce(p, e->y - 2);   /* Top, with the attack hitbox as the collider */
    seeker_got_bounced(e, player_center(p));
  }
}
/* its two PlayerColliders (attackHitbox, bounceHitbox) are small entities the player checks, in this order */
typedef struct { uint8_t owner, bounce, pad[2]; } SeekerPC;
static void seekerpc_on_player(Ent *e, Player *p) {
  SeekerPC *c = ST(e, SeekerPC);
  Ent *s = ent_at(c->owner);
  if (s->cls != &SEEKER || s->dead == 1 || !s->collidable) return;
  if (c->bounce) seeker_bounce_player(s, p);
  else seeker_attack_player(s, p);
}
static const EntClass SEEKERPC = {.name = "seekerCollider", .size = sizeof(SeekerPC), .on_player = seekerpc_on_player,
                                  .kind = KIND_PCOLLIDE};
static void seeker_sync_colliders(Ent *e) {
  Seeker *s = ST(e, Seeker);
  for (int k = 0; k < 2; k++) {
    Ent *c = ent_at(s->pc[k]);
    if (c->cls != &SEEKERPC || ST(c, SeekerPC)->owner != ent_ref(e)) continue;
    c->x = e->x, c->y = e->y;
    c->collidable = e->collidable;
    if (ST(c, SeekerPC)->bounce) ent_box(c, s->bounce_w, 6, s->bounce_x, -8);
    else ent_box(c, 12, 8, -6, -2);
  }
}
static bool seeker_hits_holdable(Ent *s, Ent *theo) {
  return s->collidable && collide_rect(theo, s->x - 6, s->y - 2, s->x + 6, s->y + 6);
}
static void seeker_on_holdable(Ent *e, Ent *theo) {   /* OnHoldable */
  Seeker *s = ST(e, Seeker);
  Theo *t = ST(theo, Theo);
  if (s->state != SK_REGEN && theo_dangerous(theo, e)) {
    if (!t->held) t->speed = safe_norm(v2sub(theo_center(theo), v2(e->x, e->y)), 120);   /* HitSeeker */
    seeker_set_state(e, SK_STUNNED);
    ST(e, Seeker)->speed = safe_norm(v2sub(v2(e->x, e->y), theo_center(theo)), 120);
    wiggler_restart(&ST(e, Seeker)->wig);
  } else if ((s->state == SK_ATTACK || s->state == SK_SKIDDING) && t->held) {
    theo_swat(theo, e, sgn(s->speed.x));
    seeker_set_state(e, SK_STUNNED);
    ST(e, Seeker)->speed = safe_norm(v2sub(v2(e->x, e->y), theo_center(theo)), 120);
    wiggler_restart(&ST(e, Seeker)->wig);
  }
}
static void seeker_update(Ent *e) {
  Seeker *s = ST(e, Seeker);
  actor_update_lift(e);
  s->light = approach(s->light, 1, DT * 2);
  barriers_collidable(true);
  s->sx = approach(s->sx, 1, 2 * DT);
  s->sy = approach(s->sy, 1, 2 * DT);
  if (s->state == SK_REGEN)
    s->can_see = 0;
  else {
    Player *p = level_player();
    s->can_see = seeker_can_see(e, p);
    if (s->can_see) s->spotted = 1, s->last_spotted = player_center(p);
  }
  if (!v2eq(s->last_path_to, s->last_spotted)) seeker_find_path(e);
  /* components: shaker, the state machine, the sines, the sprite, the wiggler */
  shaker_update(&s->shaker);
  seeker_set_state(e, seeker_state_update(e));
  seeker_coroutine(e);
  s = ST(e, Seeker);
  s->sinex = fmodf(s->sinex + 2 * PI_F * 0.5f * DT, PI_F * 8);
  s->siney = fmodf(s->siney + 2 * PI_F * 0.7f * DT, PI_F * 8);
  int ended = cs_update(&s->spr);
  if (ended >= 0) {
    seeker_last_frame(s, ended);
    if (cs_cur(&s->spr) >= 0) s->next = 255;   /* the goto (loops too) calls OnChange */
  }
  wiggler_update(&s->wig);
  actor_move_h(e, s->speed.x * DT, seeker_collide_h, NULL);
  actor_move_v(e, ST(e, Seeker)->speed.y * DT, seeker_collide_v, NULL);
  s = ST(e, Seeker);
  Room *rm = g_level.room;
  Collision none = {{0, 0}, {0, 0}, {0, 0}, NULL, NULL};
  if (e_left(e) < rm->x && s->speed.x < 0) {
    e->x += rm->x - e_left(e);
    none.dir = v2(-1, 0);
    seeker_collide_h(e, &none);
  } else if (e_right(e) > rm->x + rm->w && s->speed.x > 0) {
    e->x -= e_right(e) - (rm->x + rm->w);
    none.dir = v2(1, 0);
    seeker_collide_h(e, &none);
  }
  s = ST(e, Seeker);
  if (e_top(e) < rm->y - 8 && s->speed.y < 0) {
    e->y += rm->y - 8 - e_top(e);
    seeker_collide_v(e, &none);
  } else if (e_bottom(e) > rm->y + rm->h && s->speed.y > 0) {
    e->y -= e_bottom(e) - (rm->y + rm->h);
    seeker_collide_v(e, &none);
  }
  /* SeekerColliders: the touch switches check for seekers themselves (ch2.c) */
  s = ST(e, Seeker);
  if (s->state == SK_ATTACK && s->speed.x > 0) s->bounce_w = 16, s->bounce_x = -10;
  else if (s->state == SK_ATTACK && s->speed.y < 0) s->bounce_w = 16, s->bounce_x = -6;
  else s->bounce_w = 12, s->bounce_x = -6;
  barriers_collidable(false);
  seeker_sync_colliders(e);
}
static void seeker_squish(Ent *e, Collision *c);
static void seeker_render(Ent *e) {
  Seeker *s = ST(e, Seeker);
  float k = 1 - 0.3f * s->wig.value;
  cs_draw(&s->spr, e->x + s->shaker.x, e->y + s->shaker.y, s->sx * k * s->sprite_facing, s->sy * k, 0, 0xFFFF, 255);
  light_add(e->x, e->y, 0xFFFFFF, s->light, s->light_r ? 16 : 32, s->light_r ? 32 : 64);
}
static void seeker_awake(Ent *e) {
  Seeker *s = ST(e, Seeker);
  Ent *pe = level_player_ent();
  if (!pe || e->x == pe->x) snap_facing(s, 1);
  else snap_facing(s, (float)sgn(pe->x - e->x));
}
static void seeker_removed(Ent *e) {
  Seeker *s = ST(e, Seeker);
  if (s->tab < SEEKER_TAB && seeker_tab[s->tab].owner == e) tab_free(s->tab);
  for (int k = 0; k < 2; k++) {
    Ent *c = ent_at(s->pc[k]);
    if (c->cls == &SEEKERPC && ST(c, SeekerPC)->owner == ent_ref(e)) ent_remove(c);
  }
}
static bool seeker_riding(Ent *e, Ent *o) { return (void)e, (void)o, false; }
static const EntClass SEEKER = {.name = "seeker", .size = sizeof(Seeker), .update = seeker_update, .render = seeker_render,
                                .awake = seeker_awake, .removed = seeker_removed, .kind = KIND_ACTOR,
                                .more = &(const EntMore){.on_squish = seeker_squish,
                                                         .is_riding_solid = seeker_riding, .is_riding_jumpthru = seeker_riding}};

/* the HotPink DeathEffect of a squished seeker */
typedef struct { float t; uint16_t color, pad; } DeathFx;
static void deathfx_update(Ent *e) {
  DeathFx *d = ST(e, DeathFx);
  d->t = approach(d->t, 1, DT / 0.834f);
  if (d->t >= 1) ent_remove(e);
}
static void deathfx_render(Ent *e) { death_draw(v2(e->x, e->y), ST(e, DeathFx)->color, ST(e, DeathFx)->t); }
static const EntClass DEATHFX = {.name = "deathEffect", .size = sizeof(DeathFx), .update = deathfx_update, .render = deathfx_render};
static void seeker_squish(Ent *e, Collision *c) {
  if (actor_try_squish_wiggle(e, c)) return;
  Ent *d = ent_new(&DEATHFX, e->x, e->y);
  if (d) {
    d->depth = -1000000;
    d->collidable = 0;
    ST(d, DeathFx)->color = rgb(0xFF69B4);
  }
  ent_remove(e);
}

/* RecoverBlast: the shockwave sprite, gone after its last frame */
static void recover_update(Ent *e) {
  if (cs_update(ST(e, CSpr)) >= 0) ent_remove(e);
}
static void recover_render(Ent *e) { cs_draw(ST(e, CSpr), e->x, e->y, 1, 1, 0, 0xFFFF, 255); }
static const EntClass RECOVER = {.name = "seekerRecoverBlast", .size = sizeof(CSpr), .update = recover_update, .render = recover_render};

static Ent *spawn_seeker(float x, float y, int tab, float light) {
  Ent *e = ent_new(&SEEKER, x, y);
  if (!e) {
    if (tab >= 0) tab_free(tab);
    return NULL;
  }
  e->depth = -200;
  ent_box(e, 6, 6, -3, -3);
  e->ignore_jumpthrus = 1;
  Seeker *s = ST(e, Seeker);
  s->tab = (uint8_t)(tab < 0 ? SEEKER_TAB : tab);
  if (tab >= 0) seeker_tab[tab].owner = e;
  s->facing = s->sprite_facing = 1;
  s->next = 255;
  s->sx = s->sy = 1;
  s->light = light;
  s->bounce_w = 12, s->bounce_x = -6;
  s->last_spotted = s->last_path_to = v2(0, 0);
  wiggler_init(&s->wig, 0.8f, 2);
  cs_init(&s->spr, SB_seeker);
  s->state = 255;
  seeker_set_state(e, SK_IDLE);
  Ent *pc[2];
  for (int k = 0; k < 2; k++) {
    pc[k] = ent_new(&SEEKERPC, x, y);
    if (pc[k]) pc[k]->visible = 0, ST(pc[k], SeekerPC)->owner = ent_ref(e);
  }
  if (pc[0] && pc[1] && pc[1] < pc[0]) {   /* the player checks them by slot: the attack one first */
    Ent *t = pc[0];
    pc[0] = pc[1], pc[1] = t;
  }
  s = ST(e, Seeker);
  for (int k = 0; k < 2; k++) {
    s->pc[k] = ent_ref(pc[k] ? pc[k] : e);
    if (pc[k]) ST(pc[k], SeekerPC)->bounce = (uint8_t)k;
  }
  seeker_sync_colliders(e);
  return e;
}
static void new_seeker(const EData *d) {
  int tab = tab_alloc(NULL, d);
  if (tab < 0) return;
  spawn_seeker(d->x, d->y, tab, 1);
}
/* ---------------------------------------------------------------- SeekerStatue */
typedef struct { CSpr spr; float alarm; uint8_t player_right, tab, pad[2]; } Statue;
static void breakout_particles(V2 c) {
  for (float a = 0; a < PI_F * 2; a += 0.17453292f) {
    V2 at = v2add(c, angle_vec(a + rnd_rangef(-PI_F / 90, PI_F / 90), (float)(12 + rndi(8))));
    particles_emit1(PL_MID, &P_Seeker_P_BreakOut, at, a);
  }
}
static void statue_update(Ent *e) {
  Statue *s = ST(e, Statue);
  int ended = cs_update(&s->spr);
  if (ended == A_seeker_hatch) {   /* OnLastFrame("hatch"): the seeker breaks out */
    int tab = s->tab;
    s->tab = 255;
    float x = e->x, y = e->y;
    ent_remove(e);
    spawn_seeker(x, y, tab, 0);
    return;
  }
  if (s->alarm > 0 && (s->alarm -= DT) <= 0) breakout_particles(v2(e->x, e->y));
  Ent *pe = level_player_ent();
  if (!pe || cs_cur(&s->spr) != A_seeker_statue) return;
  bool go = s->player_right ? pe->x > e->x + 32 : v2len(v2sub(v2(pe->x, pe->y), v2(e->x, e->y))) < 220;
  if (go) {
    breakout_particles(v2(e->x, e->y));
    cs_play(&s->spr, A_seeker_hatch, false);
    s->alarm = 0.8f;
  }
}
static void statue_render(Ent *e) { cs_draw(&ST(e, Statue)->spr, e->x, e->y, 1, 1, 0, 0xFFFF, 255); }
static void statue_removed(Ent *e) {
  Statue *s = ST(e, Statue);
  if (s->tab < SEEKER_TAB && seeker_tab[s->tab].owner == e) tab_free(s->tab);
}
static const EntClass STATUE = {.name = "seekerStatue", .size = sizeof(Statue), .update = statue_update, .render = statue_render,
                                .removed = statue_removed};
static void new_statue(const EData *d) {
  Ent *e = ent_new(&STATUE, d->x, d->y);
  if (!e) return;
  e->depth = 8999;
  e->collidable = 0;
  Statue *s = ST(e, Statue);
  s->player_right = !strcmp(EAS(d, seekerStatue, hatch), "PlayerRightOfX");
  int tab = tab_alloc(e, d);
  s = ST(e, Statue);
  s->tab = (uint8_t)(tab < 0 ? 255 : tab);
  cs_init(&s->spr, SB_seeker);
  cs_play(&s->spr, A_seeker_statue, false);
}

/* ---------------------------------------------------------------- TempleMirrorPortal */
typedef struct {
  uint8_t can_trigger, counter, nroutines, active;   /* active: Activate()'s coroutine (2: from the next update) */
  uint8_t step[2];
  int8_t side[2];
  float wait[2];
  float light_target, buffer_alpha, buffer_timer, debris_start, distortion_fade;
  uint8_t curtain, torch[2], pad2;   /* entities */
} Portal;
/* the story's memory in the chapter's: the portal's debris, the fade carried into the void */
#define PDEBRIS 50
typedef struct { const Ent *e; int x, y; } PortalJob;   /* what the portal's strip layers draw */
typedef struct {
  float dpct[PDEBRIS];                     /* < 0: not Enabled */
  uint8_t dang[PDEBRIS], ddur[PDEBRIS];   /* direction (1/256 turn), duration (0.5 + n / 255 * 0.7) */
  PortalJob job;
  float portal_fade;                       /* CS04_MirrorPortal's Fader, into the next room */
  uint8_t portal_wake, room_seen;
} Story5;
enum { OFF_story5 = ((END_seeker_nodes + 7) & ~7), END_story5 = OFF_story5 + (int)sizeof(Story5) };
#define story5 (*(Story5 *)(void *)(g_chram[5] + OFF_story5))
static bool have_story5(void) { return g_level.ch && (rd16(g_level.ch + CH_FILES) >> 5 & 1); }
typedef struct { CSpr spr; uint8_t dropped, pad[3]; } Curtain;
typedef struct { float bloom, light; uint8_t lit, pad[3]; float t; } PTorch;
static uint16_t ptorch_tex[7];
static const EntClass PORTAL, CURTAIN, PORTALBG, PTORCH;
static void ptorch_light(Ent *t) {
  PTorch *p = ST(t, PTorch);
  p->lit = 1, p->t = 0;
  p->bloom = 1, p->light = 0;
}
static void ptorch_update(Ent *e) {
  PTorch *p = ST(e, PTorch);
  if (!p->lit) return;
  p->t += DT;
  if (p->bloom > 0.5f) p->bloom -= DT;
  if (p->light < 1) p->light = approach(p->light, 1, DT);
}
static void ptorch_render(Ent *e) {
  PTorch *p = ST(e, PTorch);
  int f = p->lit ? 1 + (int)floorf(p->t / 0.08f) % 6 : 0;   /* "lit": frames 1-6 */
  gfx_tex_ex(fam_tex(ptorch_tex, 7, f), e->x, e->y, 32, 64, 1, 1, 0, 0xFFFF, 255, 0);
  if (p->lit) {
    light_add(e->x, e->y, 0x20B2AA, p->light, 32, 128);   /* Color.LightSeaGreen */
    bloom_add(e->x, e->y, p->bloom, 16);
  }
}
static const EntClass PTORCH = {.name = "templePortalTorch", .size = sizeof(PTorch), .update = ptorch_update, .render = ptorch_render};

static void curtain_drop(Ent *c) {
  Curtain *k = ST(c, Curtain);
  cs_play(&k->spr, A_temple_portal_curtain_fall, false);
  c->depth = -8999;
  ents_mark_unsorted();
  c->collidable = 1;
  bool hit = false;
  Ent *pe = level_player_ent();
  while (pe && collide_ent_at(c, c->x, c->y, pe) && !hit) {
    c->collidable = 0;
    hit = actor_move_v(pe, -1, NULL, NULL);
    c->collidable = 1;
  }
}
static void curtain_update(Ent *e) {
  plat_update(e);
  cs_update(&ST(e, Curtain)->spr);
  if (!e->collidable) return;
  Ent *pe = level_player_ent();
  Player *p = level_player();
  if (!pe || !p) return;
  if (collide_ent_at(e, e->x - 1, e->y, pe) && actor_on_ground(pe, 1) && g_in.aim_x > 0) {
    actor_move_v(pe, e_top(e) - e_bottom(pe), NULL, NULL);
    actor_move_h(pe, 1, NULL, NULL);
  } else if (collide_ent_at(e, e->x + 1, e->y, pe) && actor_on_ground(pe, 1) && g_in.aim_x < 0) {
    actor_move_v(pe, e_top(e) - e_bottom(pe), NULL, NULL);
    actor_move_h(pe, -1, NULL, NULL);
  }
}
static void curtain_render(Ent *e) { cs_draw(&ST(e, Curtain)->spr, e->x, e->y, 1, 1, 0, 0xFFFF, 255); }
static const EntClass CURTAIN = {.name = "templePortalCurtain", .size = sizeof(Curtain), .update = curtain_update,
                                 .render = curtain_render, .kind = KIND_SOLID};
static void portalbg_render(Ent *e) {
  draw_centered(T_objects_temple_portal_surface, e->x, e->y, 0xFFFF, 255, 1, 1, 0);
  /* MirrorSurface: the reflections are not drawn */
}
static const EntClass PORTALBG = {.name = "templePortalBg", .render = portalbg_render};

static void portal_switch_hit(int side) {   /* TempleMirrorPortal.OnSwitchHit: a new OnSwitchRoutine */
  Ent *e = first_of(&PORTAL);
  if (!e) return;
  Portal *p = ST(e, Portal);
  if (p->nroutines >= 2) return;
  int i = p->nroutines++;
  p->step[i] = 0, p->side[i] = (int8_t)side, p->wait[i] = 0.4f;
}
static void portal_activate_step(Portal *p) {   /* ActivateRoutine, an update of it */
  Story5 *st = &story5;
  p->buffer_alpha = approach(p->buffer_alpha, 1, DT);
  p->buffer_timer += 4 * DT;
  g_level.lighting = approach(g_level.lighting, 0.2f, DT * 0.25f);
  if (p->debris_start < PDEBRIS) {
    int n = (int)p->debris_start;
    st->dang[n] = (uint8_t)rndi(256);
    st->ddur[n] = (uint8_t)(rndf() * 255);
    if (st->dpct[n] < 0) st->dpct[n] = 0;   /* Enabled */
  }
  p->debris_start += DT * 10;
  for (int i = 0; i < PDEBRIS; i++)
    if (st->dpct[i] >= 0) st->dpct[i] = fmodf(st->dpct[i], 1) + DT / (0.5f + st->ddur[i] / 255.f * 0.7f);
}
static void portal_update(Ent *e) {
  Portal *p = ST(e, Portal);
  if (p->active == 1) portal_activate_step(p);
  else if (p->active == 2) p->active = 1;
  for (int i = 0; i < p->nroutines; i++) {
    if (p->wait[i] > 0) {
      p->wait[i] -= DT;
      continue;
    }
    switch (p->step[i]) {
      case 0: {
        Ent *t = ent_at(p->torch[p->side[i] < 0 ? 0 : 1]);
        if (t->cls == &PTORCH) ptorch_light(t);
        p->counter++;
        p->light_target = fmaxf(0, g_level.lighting - 0.2f);
        p->step[i] = 1;
      }
        /* fall through */
      case 1:
        if (g_session.mode == 0 && (g_level.lighting -= DT) > p->light_target) break;
        p->wait[i] = 0.15f;
        p->step[i] = 2;
        break;
      case 2:
        if (p->counter < 2) {
          p->step[i] = 9;
          break;
        }
        p->wait[i] = 0.1f;
        p->step[i] = 3;
        break;
      case 3:
        if (ent_at(p->curtain)->cls == &CURTAIN) curtain_drop(ent_at(p->curtain));
        p->can_trigger = 1;
        p->wait[i] = 0.1f;
        p->step[i] = 4;
        break;
      case 4:
        for (int x = 0; x < 120; x += 12)
          for (int y = 0; y < 60; y += 6)
            particles_emit(PL_MID, &P_TempleMirrorPortal_P_CurtainDrop, 1, v2(ent_at(p->curtain)->x - 57 + x, ent_at(p->curtain)->y - 27 + y),
                           v2(6, 3), 0);
        p->step[i] = 9;
        break;
    }
  }
}
/* the portal's buffer (BeforeRender): objects/temple/portal/portal drawn ten times, black to purple, growing and
 * turning, in 120 x 64; each copy covers what is outside its hole (radius 60 x its scale): rings */
static void portal_buffer_strip(uint16_t *strip, int sy0, int sy1, void *ctx) {
  const PortalJob *j = ctx;
  const Portal *p = ST(j->e, Portal);
  float t = fmodf(p->buffer_timer, 1) * 0.1f;
  int a = (int)(p->buffer_alpha * 256);
  for (int y = j->y < sy0 ? sy0 : j->y; y < j->y + 64 && y < sy1; y++) {
    float dy = y - j->y + 0.5f - 32;
    uint16_t *row = strip + (y - sy0) * VIEW_W;
    for (int x = j->x < 0 ? 0 : j->x; x < j->x + 120 && x < VIEW_W; x++) {
      float dx = x - j->x + 0.5f - 60, d = sqrtf(dx * dx + dy * dy), num = 0;
      for (int i = 9; i >= 0; i--)
        if (d >= 60 * (t + i / 10.f) && d <= 150 * (t + i / 10.f)) {
          num = t + i / 10.f;
          break;
        }
      int c = (int)(128 * num);   /* Color.Lerp(Black, Purple, num) */
      row[x] = blend565(row[x], scale565(rgb((uint32_t)(c << 16 | c)), a), a);
    }
  }
}
/* the debris: particles/blob in Color.Lerp(f442d4, black), scaled 1 to 0.2, flying in */
static void portal_debris_strip(uint16_t *strip, int sy0, int sy1, void *ctx) {
  const Ent *e = ((const PortalJob *)ctx)->e;
  const Story5 *st = &story5;
  for (int i = 0; i < PDEBRIS; i++) {
    if (st->dpct[i] < 0) continue;
    float num = ease_sine_out(st->dpct[i]);
    V2 at = v2add(v2(e->x, e->y), angle_vec(st->dang[i] / 256.f * 2 * PI_F, (1 - num) * (190 - g_level.zoom * 30)));
    uint16_t col = lerp565(0xF442D4, 0, num);
    float r = 4 * (1 - 0.8f * clampf(num, 0, 1)), cx = at.x - g_camx, cy = at.y - g_camy;
    for (int y = (int)floorf(cy - r); y <= (int)ceilf(cy + r); y++) {
      if (y < sy0 || y >= sy1) continue;
      for (int x = (int)floorf(cx - r); x <= (int)ceilf(cx + r); x++) {
        float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
        if ((unsigned)x < VIEW_W && dx * dx + dy * dy <= r * r) strip[(y - sy0) * VIEW_W + x] = col;
      }
    }
  }
}
static void portal_render(Ent *e) {
  Portal *p = ST(e, Portal);
  PortalJob *j = &story5.job;
  if (p->active) {
    j->e = e, j->x = (int)floorf(e->x - 60) - g_camx, j->y = (int)floorf(e->y - 32) - g_camy;
    if (p->buffer_alpha > 0) gfx_custom(portal_buffer_strip, j, j->y, j->y + 64);
  }
  draw_centered(T_objects_temple_portal_portalframe, e->x, e->y, 0xFFFF, 255, 1, 1, 0);
  if (p->active) gfx_custom(portal_debris_strip, j, 0, VIEW_H);
}

/* ---------------------------------------------------------------- CS04_MirrorPortal */
/* the Fader: black over the level, and Madeline over it while she floats */
typedef struct { float fade, target; uint8_t ended, first; } PFader;
static void pfader_update(Ent *e) {
  PFader *f = ST(e, PFader);
  if (f->first) {   /* in the room after: the wipe is cancelled, Madeline wakes up there (B-side) */
    f->first = 0;
    g_wipe.active = false;
    Player *p = level_player();
    if (story5.portal_wake && p) {
      spr_play(&p->spr, A_player_asleep, false);
      p->facing = 1;
      player_set_state(p, ST_INTROWAKEUP);
    }
    story5.portal_fade = 0, story5.portal_wake = 0;
  }
  f->fade = approach(f->fade, f->target, DT * 0.5f);
  if (f->target <= 0 && f->fade <= 0 && f->ended) ent_remove(e);
}
static void pfader_render(Ent *e) {
  PFader *f = ST(e, PFader);
  if (f->fade > 0) gfx_rect(g_level.cam.x - 10, g_level.cam.y - 10, 340, 200, 0, a8(f->fade));
  Player *p = level_player();
  if (p && !actor_on_ground(p->ent, 2) && p->ent->cls->render) p->ent->cls->render(p->ent);
}
static const EntClass PFADER = {.name = "mirrorPortalFader", .size = sizeof(PFader), .update = pfader_update, .render = pfader_render};
static Ent *pfader_new(float fade, float target) {
  Ent *e = ent_new(&PFADER, 0, 0);
  if (!e) return NULL;
  e->depth = -1000000;
  e->collidable = 0;
  ST(e, PFader)->fade = fade, ST(e, PFader)->target = target;
  return e;
}
typedef struct {
  Cutscene cs;
  Ent *portal, *fader;
  Walk walk;
  Step zoom, step;
  V2 from, target;
  float p;
  uint8_t centering, zooming;
} MPortal;
static void mportal_update(Ent *e) {
  MPortal *s = ST(e, MPortal);
  Player *p = &g_player;
  Ent *pe = p->ent, *po = s->portal;
  Co *c = &s->cs.co;
  CO_BEGIN(c);
  player_dummy(p);
  p->dashes = 1;
  s->centering = 1;   /* CenterCamera() */
  CO_WALK(c, s->walk, walk_exact((int)po->x));
  CO_WAIT(c, 0.25f);
  CO_WALK(c, s->walk, walk_exact((int)po->x - 16));
  CO_WAIT(c, 0.5f);
  CO_WALK(c, s->walk, walk_exact((int)po->x + 16));
  CO_WAIT(c, 0.25f);
  p->facing = -1;
  CO_WAIT(c, 0.25f);
  CO_WALK(c, s->walk, walk_exact((int)po->x));
  CO_WAIT(c, 0.1f);
  p->dummy_auto_animate = false;
  spr_play(&p->spr, A_player_lookUp, false);
  CO_WAIT(c, 1);
  p->dummy_auto_animate = true;
  ST(po, Portal)->active = 2;   /* Activate() */
  step_reset(&s->zoom);
  s->zooming = 2;   /* ZoomTo((160, 90), 3, 12) in a coroutine of its own */
  CO_WAIT(c, 0.25f);
  /* (ForceStrongWindHair.X = -1: the hair blown back is not here) */
  CO_WALK(c, s->walk, walk_back((int)pe->x + 12));
  CO_WAIT(c, 0.5f);
  p->facing = 1;
  p->dummy_auto_animate = false;
  p->dummy_gravity = false;
  spr_play(&p->spr, A_player_runWind, false);
  while (p->spr.rate > 0) {
    actor_move_h(pe, p->spr.rate * 10 * DT, NULL, NULL);
    actor_move_v(pe, -(1 - p->spr.rate) * 6 * DT, NULL, NULL);
    p->spr.rate -= DT * 0.15f;
    CO_YIELD(c);
  }
  CO_WAIT(c, 0.5f);
  spr_play(&p->spr, A_player_fallFast, false);
  p->spr.rate = 1;
  s->target = v2(po->x, po->y + 8);
  s->from = v2(pe->x, pe->y);
  for (s->p = 0; s->p < 1; s->p += DT * 2) {
    {
      V2 at = v2lerp(s->from, s->target, ease_sine_inout(s->p));
      pe->x = at.x, pe->y = at.y;
    }
    CO_YIELD(c);
  }
  if (s->fader && s->fader->cls == &PFADER) ST(s->fader, PFader)->target = 1;
  CO_WAIT(c, 2);
  spr_play(&p->spr, A_player_sleep, false);
  CO_WAIT(c, 1);
  CO_ZOOM_BACK(c, &s->step, 1);
  if (g_session.mode == M_A)   /* the "templevoid" grade, Glitch and ScreenPadding growing over a second: not drawn */
    for (s->p = 0; s->p < 1; s->p += DT) CO_YIELD(c);
  while ((ST(po, Portal)->distortion_fade -= DT * 2) > 0) CO_YIELD(c);
  cutscene_end(e);
  return;
  CO_END(c);
}
static void mportal_tick(Ent *e) {
  MPortal *s = ST(e, MPortal);
  mportal_update(e);
  if (e->dead == 1) return;
  if (s->centering) {
    V2 t = v2(s->portal->x - 160, s->portal->y - 90);
    if (v2len(v2sub(g_level.cam, t)) > 1) g_level.cam = v2add(g_level.cam, v2mul(v2sub(t, g_level.cam), 1 - powf(0.01f, DT)));
    else s->centering = 0;
  }
  if (s->zooming == 2) s->zooming = 1;
  else if (s->zooming && !zoom_to(&s->zoom, v2(160, 90), 3, 12)) s->zooming = 0;
}
static void mportal_end(Ent *e, bool skipped) {   /* OnEnd: the next room at the end of the frame */
  MPortal *s = ST(e, MPortal);
  if (have_story5()) {
    story5.portal_fade = !skipped && s->fader && s->fader->cls == &PFADER ? fmaxf(ST(s->fader, PFader)->fade, 0.01f) : 0;
    story5.portal_wake = g_session.mode != M_A;
  }
  g_session.dreaming = 1;
  g_session.nkeys = 0;
  /* the level moves on at the frame's end, at the spawn nearest the room's top left */
  int r = g_session.mode == M_A ? room_index("void", 4) : room_index("c-00", 4);
  if (r >= 0) level_load_room_near(r, g_session.mode == M_A ? INTRO_TEMPLEMIRRORVOID : INTRO_WAKEUP, 0, 0);
}
static const EntClass MPORTAL = {.name = "CS04_MirrorPortal", .size = sizeof(MPortal), .update = mportal_tick};
static void portal_on_player(Ent *e, Player *pl) {
  (void)pl;
  Portal *p = ST(e, Portal);
  if (!p->can_trigger) return;
  p->can_trigger = 0;
  Ent *cs = scene_new(&MPORTAL, mportal_end, true, false);
  if (!cs) return;
  MPortal *s = ST(cs, MPortal);
  s->portal = e;
  s->fader = pfader_new(0, 0);
}
/* a room is loading: the fade from the portal goes on in it */
static void story5_room(void) {
  if (!have_story5() || !g_level.room || story5.room_seen == (uint8_t)(g_level.room->index + 1)) return;
  story5.room_seen = (uint8_t)(g_level.room->index + 1);
  if (story5.portal_fade > 0) {
    Ent *f = pfader_new(story5.portal_fade, 0);
    if (f) ST(f, PFader)->ended = 1, ST(f, PFader)->first = 1;
  }
}
static const EntClass PORTAL = {.name = "templeMirrorPortal", .size = sizeof(Portal), .update = portal_update, .render = portal_render,
                                .on_player = portal_on_player, .kind = KIND_PCOLLIDE};
static void new_portal(const EData *d) {
  Ent *e = ent_new(&PORTAL, d->x, d->y);
  if (!e) return;
  e->depth = 2000;
  ent_box(e, 120, 64, -60, -32);
  Ent *c = ent_new(&CURTAIN, d->x, d->y);
  if (c) {
    c->depth = 1999;
    ent_box(c, 140, 12, -70, 33);
    c->safe = 1;
    c->collidable = 0;
    cs_init(&ST(c, Curtain)->spr, SB_temple_portal_curtain);
  }
  Ent *bg = ent_new(&PORTALBG, d->x, d->y);
  if (bg) bg->depth = 9500, bg->collidable = 0;
  if (!ptorch_tex[0]) tex_family("objects/temple/portal/portaltorch", ptorch_tex, 7);
  Ent *t[2];
  for (int i = 0; i < 2; i++) {
    t[i] = ent_new(&PTORCH, d->x + (i ? 90 : -90), d->y);
    if (t[i]) t[i]->depth = 8999, t[i]->collidable = 0;
  }
  Portal *p = ST(e, Portal);
  p->distortion_fade = 1;
  if (have_story5())
    for (int i = 0; i < PDEBRIS; i++) story5.dpct[i] = -1;
  p->curtain = ent_ref(c ? c : e);
  p->torch[0] = ent_ref(t[0] ? t[0] : e), p->torch[1] = ent_ref(t[1] ? t[1] : e);
}

/* ---------------------------------------------------------------- TempleBigEyeball */
typedef struct {
  CSpr spr;
  Wiggler bounce, pupil_w;
  V2 pupil, pupil_target;
  float pupil_delay, pupil_speed, shock_timer, co_wait, fade;
  Step zoom;
  V2 zoom_at;
  uint8_t triggered, shock_flag, bursting, co, pupil_visible, scene, zooming, pad;
} BigEye;
/* Burst()'s Level.StartCutscene(OnSkip): its skip completes the area */
typedef struct { Cutscene cs; } EyeBurst;
static void eyeburst_end(Ent *e, bool skipped) {
  (void)e, (void)skipped;
  level_complete_area(false, false);
}
static const EntClass EYEBURST = {.name = "templeBigEyeballBurst", .size = sizeof(EyeBurst)};
typedef struct { uint8_t hit, pad[3]; } Shockwave;
static void shock_on_player(Ent *e, Player *p) {
  if (p->state == ST_DASH) return;
  p->speed.x = -100;
  if (p->speed.y > 30) p->speed.y = 30;
  ST(e, Shockwave)->hit = 1;
}
static void shock_update(Ent *e) {
  e->x -= 300 * DT;
  if (e->x < g_level.room->x - 20) ent_remove(e);
  /* the displacement ring is not drawn */
}
static const EntClass SHOCKWAVE = {.name = "templeBigEyeballShockwave", .size = sizeof(Shockwave), .update = shock_update,
                                   .on_player = shock_on_player, .kind = KIND_PCOLLIDE};
static void bigeye_on_player(Ent *e, Player *p) {
  BigEye *b = ST(e, BigEye);
  if (b->triggered) return;
  player_explode_launch(p, v2add(player_center(p), v2(20, 0)), true);
  wiggler_restart(&ST(e, BigEye)->bounce);
}
static void bigeye_on_holdable(Ent *e, Ent *theo) {
  BigEye *b = ST(e, BigEye);
  Theo *t = ST(theo, Theo);
  if (b->triggered || t->speed.x <= 32 || t->held) return;
  t->speed = v2(-50, -10);
  b->triggered = 1;
  wiggler_restart(&b->bounce);
  e->collidable = 0;
  b->bursting = 1;   /* Burst() */
  b->co = 1;
  /* Level.StartCutscene(OnSkip, fadeInOnSkip: false, endingChapterAfterCutscene: true), RegisterAreaComplete */
  Ent *sc = scene_new(&EYEBURST, eyeburst_end, false, true);
  b->scene = ent_ref(sc ? sc : e);
  level_register_complete();
  level_freeze(0.1f);
}
static void bigeye_update(Ent *e) {
  BigEye *b = ST(e, BigEye);
  cs_update(&b->spr);
  wiggler_update(&b->bounce);
  wiggler_update(&b->pupil_w);
  Player *pl = level_player();
  if (b->co) {   /* Burst() */
    if (b->co_wait > 0) b->co_wait -= DT;
    else
      switch (b->co++) {
        case 1: break;   /* yield null */
        case 2: {
          g_level.in_cutscene = true;
          if (pl) {
            player_set_state(pl, ST_DUMMY);
            pl->state_locked = true;
            if (pl->on_ground) pl->dummy_auto_animate = false, spr_play(&pl->spr, A_player_shaking, false);
          }
          Ent *th = theo_entity();
          if (th) {
            b->zoom_at = v2(th->x - g_level.cam.x, th->y - 10 - g_level.cam.y);
            step_reset(&b->zoom);
            b->zooming = 2;
            ST(th, Theo)->shattering = 1;   /* theo.Shatter() */
            ST(th, Theo)->co = 0, ST(th, Theo)->co_p = 0;
          }
          eyes_burst_all();
          cs_play(&b->spr, A_temple_eyeball_burst, false);
          b->pupil_visible = 0;
          level_shake(0.4f);
          b->co_wait = 2;
          break;
        }
        case 3:
          if (pl && pl->on_ground) pl->dummy_auto_animate = false, spr_play(&pl->spr, A_player_shaking, false);
          e->visible = 0;
          /* fall through */
        default:
          if ((b->fade += DT) < 1) b->co = 4;   /* the Fader */
          else b->co_wait = 1, b->co = 99;
          break;
        case 99:   /* Level.EndCutscene, CompleteArea(spotlightWipe: false) */
          b->co = 0;
          if (ent_at(b->scene)->cls == &EYEBURST) cutscene_end(ent_at(b->scene));
          else level_complete_area(false, false);
          return;
      }
  }
  if (b->zooming == 2) b->zooming = 1;   /* ZoomTo(theo.TopCenter, 2, 0.5): a coroutine of its own */
  else if (b->zooming && !zoom_to(&b->zoom, b->zoom_at, 2, 0.5f)) b->zooming = 0;
  Ent *pe = level_player_ent();
  if (!b->triggered && b->shock_timer > 0) {
    b->shock_timer -= DT;
    if (b->shock_timer <= 0) {
      if (pe) {
        b->shock_timer = clamped_map(fabsf(e->x - pe->x), 100, 500, 2, 3);
        b->shock_flag = !b->shock_flag;
        if (b->shock_flag) b->shock_timer -= 1;
      }
      Ent *s = ent_new(&SHOCKWAVE, e->x + 50, e->y);
      if (s) {
        s->depth = -1000000;
        ent_box(s, 48, 200, -30, -100);
      }
      b = ST(e, BigEye);
      wiggler_restart(&b->pupil_w);
      b->pupil_target = v2(-1, 0);
      b->pupil_speed = 120;
      b->pupil_delay = fmaxf(0.5f, b->pupil_delay);
    }
  }
  b->pupil = approach_v(b->pupil, v2mul(b->pupil_target, 12), DT * b->pupil_speed);
  b->pupil_speed = approach(b->pupil_speed, 40, DT * 400);
  Ent *th = theo_entity();
  if (th && fabsf(e->x - th->x) < 64 && fabsf(e->y - th->y) < 64) b->pupil_target = safe_norm(v2sub(theo_center(th), v2(e->x, e->y)), 1);
  else if (b->pupil_delay < 0) {
    b->pupil_target = angle_vec(rndf() * PI_F * 2, 1);
    static const float CH[3] = {0.2f, 1, 2};
    b->pupil_delay = CH[rndi(3)];
  } else
    b->pupil_delay -= DT;
}
static void bigeye_render(Ent *e) {
  BigEye *b = ST(e, BigEye);
  cs_draw(&b->spr, e->x, e->y, 1 + 0.15f * b->bounce.value, 1, 0, 0xFFFF, 255);
  float s = 1 + b->pupil_w.value * 0.15f;
  if (b->pupil_visible) draw_centered(T_danger_templeeye_pupil, e->x + b->pupil.x, e->y + b->pupil.y, 0xFFFF, 255, s, s, 0);
}
/* the Fader: white over everything (Tags.HUD) */
typedef struct { uint8_t eye, pad[3]; } EyeFader;
static void eyefader_render(Ent *e) {
  Ent *b = ent_at(ST(e, EyeFader)->eye);
  if (b->cls != &BIGEYE || ST(b, BigEye)->fade <= 0) return;
  gfx_hud(true);
  gfx_rect(0, 0, 320, 180, 0xFFFF, a8(ST(b, BigEye)->fade));
  gfx_hud(false);
}
static const EntClass EYEFADER = {.name = "templeBigEyeballFader", .size = sizeof(EyeFader), .render = eyefader_render};
static const EntClass BIGEYE = {.name = "templeBigEyeball", .size = sizeof(BigEye), .update = bigeye_update, .render = bigeye_render,
                                .on_player = bigeye_on_player, .kind = KIND_PCOLLIDE};
static void new_bigeye(const EData *d) {
  Ent *e = ent_new(&BIGEYE, d->x, d->y);
  if (!e) return;
  ent_box(e, 48, 64, -24, -32);
  BigEye *b = ST(e, BigEye);
  cs_init(&b->spr, SB_temple_eyeball);
  wiggler_init(&b->bounce, 0.5f, 3);
  wiggler_init(&b->pupil_w, 0.5f, 3);
  b->shock_timer = 2;
  b->pupil_speed = 40;
  b->pupil_visible = 1;
  Ent *f = ent_new(&EYEFADER, 0, 0);
  if (f) {
    f->depth = D_TOP;
    f->collidable = 0;
    ST(f, EyeFader)->eye = ent_ref(e);
  }
}

/* ---------------------------------------------------------------- PlayerSeeker (the void) */
typedef struct {
  CSpr spr;
  V2 speed, dash_dir;
  float dash_timer, trail_a, trail_b, sx, sy, co_wait, tween;
  V2 cam_from;
  Shaker shaker;
  int8_t facing;
  uint8_t enabled, co, pad;
} PSeeker;
static V2 pseeker_cam(Ent *e) {
  Room *rm = g_level.room;
  return v2(clampf(e->x - 160, rm->x, rm->x + rm->w - 320), clampf(e->y - 90, rm->y, rm->y + rm->h - 180));
}
static void pseeker_trail(Ent *e) {
  PSeeker *s = ST(e, PSeeker);
  uint16_t t = cs_tex(&s->spr);
  if (t != 0xFFFF) trail_add(e->x, e->y, t, 48, 48, s->sx * s->facing, s->sy, 0x99E550, 1, e->depth + 1);
}
static void pseeker_dash(Ent *e, V2 dir) {
  PSeeker *s = ST(e, PSeeker);
  if (s->dash_timer <= 0) {
    pseeker_trail(e);
    s->trail_a = 0.1f, s->trail_b = 0.25f;
  }
  s->dash_timer = 0.3f;
  s->dash_dir = dir;
  if (dir.x == 0 && dir.y == 0) s->dash_dir.x = (float)s->facing;
  if (s->dash_dir.x != 0) s->facing = (int8_t)sgn(s->dash_dir.x);
  s->speed = v2mul(s->dash_dir, 400);
  cs_play(&s->spr, A_seeker_attacking, false);
  level_dir_shake(s->dash_dir, 0.3f);
  if (s->dash_dir.x == 0) s->sx = 0.6f, s->sy = 1.4f;
  else s->sx = 1.4f, s->sy = 0.6f;
}
static void pseeker_collide(Ent *e, Collision *c) {
  PSeeker *s = ST(e, PSeeker);
  if (s->dash_timer <= 0) {
    if (c->dir.x != 0) s->speed.x = 0;
    if (c->dir.y != 0) s->speed.y = 0;
    return;
  }
  float dir;
  V2 at, range;
  if (c->dir.x > 0) dir = PI_F, at = v2(e_right(e), e->y), range = v2(0, 4);
  else if (c->dir.x < 0) dir = 0, at = v2(e_left(e), e->y), range = v2(0, 4);
  else if (c->dir.y > 0) dir = -PI_F / 2, at = v2(e->x, e_bottom(e)), range = v2(4, 0);
  else dir = PI_F / 2, at = v2(e->x, e_top(e)), range = v2(4, 0);
  particles_emit(PL_MID, &P_Seeker_P_HitWall, 12, at, range, dir);
  if (c->hit && c->hit->cls == &BARRIER) barrier_reflect(c->hit);
  if (c->dir.x != 0) s->speed.x *= -0.8f, s->sx = 0.6f, s->sy = 1.4f;
  else if (c->dir.y != 0) s->speed.y *= -0.8f, s->sx = 1.4f, s->sy = 0.6f;
  if (c->hit && c->hit->cls == &CRACKED) {
    level_freeze(0.15f);
    cracked_break(c->hit, v2(e->x, e->y));
  }
}
static void pseeker_on_player(Ent *e, Player *p) {
  if (p->dead) return;
  /* Leader.StoreStrawberries: TODO; Engine.TimeRate 0.25 */
  player_die(p, safe_norm(v2sub(v2(p->ent->x, p->ent->y), v2(e->x, e->y)), 1), true);
  int idx = room_index("c-00", 4);   /* End(): then c-00, waking up */
  if (idx >= 0 && g_level.room) g_level.room->index = idx, g_session.has_respawn = 0;
  g_level.can_retry = true;
}
/* Player.StartTempleMirrorVoidSleep (the TempleMirrorVoid intro) */
static void pseeker_sleep(void) {
  Player *p = level_player();
  if (!p) return;
  spr_play(&p->spr, A_player_asleep, false);
  p->facing = 1;
  player_set_state(p, ST_DUMMY);
  p->state_locked = true;
  p->dummy_auto_animate = false;
  p->dummy_gravity = false;
}
static void pseeker_update(Ent *e) {
  PSeeker *s = ST(e, PSeeker);
  barriers_collidable(true);
  /* IntroSequence */
  if (s->co_wait > 0) s->co_wait -= DT;
  else
    switch (s->co) {
      case 0: s->co = 1; break;   /* yield null */
      case 1:
        pseeker_sleep();   /* (already asleep: the intro did it) */
        s->co_wait = 3, s->co = 2;
        break;
      case 2:
        s->cam_from = g_level.cam;
        s->tween = 0;
        s->co_wait = 2, s->co = 3;
        break;
      case 3:
        shaker_for(&s->shaker, 0.5f);
        breakout_particles(v2(e->x, e->y));
        s->co_wait = 1, s->co = 4;
        break;
      case 4:
        shaker_for(&s->shaker, 0.5f);
        breakout_particles(v2(e->x, e->y));
        s->co_wait = 1, s->co = 5;
        break;
      case 5:
        breakout_particles(v2(e->x, e->y));
        shaker_for(&s->shaker, 1);
        cs_play(&s->spr, A_seeker_hatch, false);
        s->enabled = 1;
        s->co_wait = 0.8f, s->co = 6;
        break;
      case 6:
        breakout_particles(v2(e->x, e->y));
        s->co_wait = 0.7f, s->co = 7;
        break;
    }
  if (s->co >= 3 && s->tween < 1) {   /* the camera's tween to the seeker */
    s->tween = fminf(1, s->tween + DT / 2);
    g_level.cam = v2add(s->cam_from, v2mul(v2sub(pseeker_cam(e), s->cam_from), ease_sine_inout(s->tween)));
  }
  shaker_update(&s->shaker);
  int ended = cs_update(&s->spr);
  if ((ended == A_seeker_flipMouth || ended == A_seeker_flipEyes)) s->facing = (int8_t)-s->facing;
  s->sx = approach(s->sx, 1, 2 * DT);
  s->sy = approach(s->sy, 1, 2 * DT);
  if (s->enabled && cs_cur(&s->spr) != A_seeker_hatch) {
    if (s->dash_timer > 0) {
      s->speed = approach_v(s->speed, v2(0, 0), 800 * DT);
      s->dash_timer -= DT;
      if (s->dash_timer <= 0) cs_play(&s->spr, A_seeker_spotted, false);
      if (s->trail_a > 0 && (s->trail_a -= DT) <= 0) pseeker_trail(e);
      if (s->trail_b > 0 && (s->trail_b -= DT) <= 0) pseeker_trail(e);
      if (level_on_interval(0.04f)) {
        V2 v = safe_norm(s->speed, 1);
        particles_emit(PL_MID, &P_Seeker_P_Attack, 2, v2add(v2(e->x, e->y), v2mul(v, 4)), v2(4, 4), vangle(v));
      }
    } else {
      V2 aim = safe_norm(v2((float)g_in.aim_x, (float)g_in.aim_y), 1);
      s->speed = v2add(s->speed, v2mul(aim, 600 * DT));
      float n = v2len(s->speed);
      if (n > 120) s->speed = safe_norm(s->speed, approach(n, 120, DT * 700));
      if (aim.y == 0) s->speed.y = approach(s->speed.y, 0, 400 * DT);
      if (aim.x == 0) s->speed.x = approach(s->speed.x, 0, 400 * DT);
      if ((aim.x != 0 || aim.y != 0) && cs_cur(&s->spr) == A_seeker_idle) cs_play(&s->spr, A_seeker_spotted, false);
      int sx = sgn(s->speed.x);
      if (sx != 0 && s->facing != sx && sgn((float)g_in.aim_x) == sx && fabsf(s->speed.x) > 20 &&
          cs_cur(&s->spr) != A_seeker_flipMouth && cs_cur(&s->spr) != A_seeker_flipEyes)
        cs_play(&s->spr, A_seeker_flipMouth, false);
      if (btn_pressed(&g_in.dash)) {   /* Input.Aim.EightWayNormal */
        V2 d = v2((float)g_in.aim_x, (float)g_in.aim_y);
        pseeker_dash(e, safe_norm(d, 1));
      }
    }
    actor_move_h(e, s->speed.x * DT, pseeker_collide, NULL);
    actor_move_v(e, ST(e, PSeeker)->speed.y * DT, pseeker_collide, NULL);
    s = ST(e, PSeeker);
    Room *rm = g_level.room;
    e->x = clampf(e->x, rm->x, rm->x + rm->w), e->y = clampf(e->y, rm->y, rm->y + rm->h);
    Player *p = level_player();
    if (p) {
      float d = v2len(v2sub(v2(e->x, e->y), v2(p->ent->x, p->ent->y)));
      if (d < 200 && p->spr.anim == A_player_asleep) {
        p->spr.rate = 2;
        spr_play(&p->spr, A_player_wakeUp, false);
      } else if (d < 100 && p->spr.anim != A_player_wakeUp) {
        p->spr.rate = 1;
        spr_play(&p->spr, A_player_runFast, false);
        p->facing = e->x > p->ent->x ? -1 : 1;
      }
      if (d < 50 && s->dash_timer <= 0) pseeker_dash(e, safe_norm(v2sub(player_center(p), v2(e->x, e->y)), 1));
      /* Engine.TimeRate, Distort.Anxiety: not here */
      V2 t = pseeker_cam(e);
      g_level.cam = v2add(g_level.cam, v2mul(v2sub(t, g_level.cam), 1 - powf(0.01f, DT)));
    }
  }
  barriers_collidable(false);
}
static void pseeker_render(Ent *e) {
  PSeeker *s = ST(e, PSeeker);
  cs_draw(&s->spr, e->x + s->shaker.x, e->y + s->shaker.y, s->sx * s->facing, s->sy, 0, 0xFFFF, 255);
  light_add(e->x, e->y, 0xFFFFFF, 1, 32, 64);
}
static void pseeker_awake(Ent *e) {
  (void)e;
  g_level.can_retry = false;   /* ColorGrade "templevoid", ScreenPadding 32: not here */
  /* the player came in with IntroTypes.TempleMirrorVoid, which player_spawn does not know: asleep from the start */
  Player *p = level_player();
  if (p) p->speed = v2(0, 0);
  pseeker_sleep();
}
static const EntClass PSEEKER = {.name = "playerSeeker", .size = sizeof(PSeeker), .update = pseeker_update, .render = pseeker_render,
                                 .awake = pseeker_awake, .on_player = pseeker_on_player, .kind = KIND_ACTOR | KIND_PCOLLIDE};
static void new_pseeker(const EData *d) {
  Ent *e = ent_new(&PSEEKER, d->x, d->y);
  if (!e) return;
  ent_box(e, 10, 10, -5, -5);
  PSeeker *s = ST(e, PSeeker);
  cs_init(&s->spr, SB_seeker);
  cs_play(&s->spr, A_seeker_statue, false);
  s->facing = 1;
  s->sx = s->sy = 1;
}

/* ================================================================ the story: Theo, Badeline, the cutscenes */
static bool on_ground(Ent *e) { return actor_on_ground(e, 1); }
static float room_right(void) { return (float)(g_level.room->x + g_level.room->w); }

/* ---------------------------------------------------------------- NPC05_Theo_Entrance and CS05_Entrance */
typedef struct {
  Cutscene cs;
  Ent *theo, *tb;
  V2 move_to;
  Co ev;
  int8_t ev_i;
  uint8_t started;
} Entrance;
static bool entrance_nb(Entrance *s, Co *c, int i) {
  switch (i) {
    case 0: g_player.facing = 1; return false;           /* MaddyTurnsRight */
    case 1: NPC_(s->theo)->spr.sx *= -1; return false;   /* TheoTurns */
    default:                                              /* TheoLeaves */
      NB_BEGIN(c);
      npc_move_to(s->theo, v2(room_right() + 32, s->theo->y), false, 0, false);
      NB_AWAIT(c, &NPC_(s->theo)->mv);
      NB_END(c);
  }
}
static bool entrance_ev(void *ctx, int i) {
  Entrance *s = ST((Ent *)ctx, Entrance);
  return !entrance_nb(s, ev_co(&s->ev, &s->ev_i, i), i);
}
static void entrance_update(Ent *e) {
  Entrance *s = ST(e, Entrance);
  Player *p = &g_player;
  Co *c = &s->cs.co;
  CO_BEGIN(c);
  s->started = 1;
  player_dummy(p);
  p->ent->x = s->theo->x - 32;
  s->move_to = v2(s->theo->x - 32, p->ent->y);
  p->facing = -1;
  g_wipe.focus = v2(s->theo->x - 16 - g_level.cam.x, s->theo->y - 8 - g_level.cam.y);   /* SpotlightWipe.FocusPoint */
  CO_WAIT(c, 2);
  p->facing = 1;
  CO_WAIT(c, 0.3f);
  npc_move_to(s->theo, v2(s->theo->x + 48, s->theo->y), false, 0, false);
  CO_AWAIT(c, &NPC_(s->theo)->mv);
  s->ev_i = -1;
  CO_SAY(c, s->tb, "ch5_entrance", entrance_ev, e);
  cutscene_end(e);
  return;
  CO_END(c);
}
static void entrance_end(Ent *e, bool skipped) {
  Entrance *s = ST(e, Entrance);
  Player *p = &g_player;
  (void)skipped;
  if (s->started) {
    player_free(p);
    p->force_camera_update = false;
    p->ent->x = s->move_to.x, p->ent->y = s->move_to.y;
    p->facing = 1;
  }
  ent_remove(s->theo);
  level_set_flag("entrance", true);
}
static const EntClass ENTRANCE = {.name = "CS05_Entrance", .size = sizeof(Entrance), .update = entrance_update};
static Ent *theo_npc_new(const EData *d, int which) {
  Ent *e = npc_new(d->x, d->y, SB_theo, -1, NULL);
  if (!e) return NULL;
  Npc *n = NPC_(e);
  n->which = (uint8_t)which;
  n->idle_anim = A_theo_idle, n->move_anim = A_theo_walk, n->maxspeed = 48;
  return e;
}
static void new_theo_entrance(const EData *d) {
  if (level_get_flag("entrance")) return;
  Ent *e = theo_npc_new(d, NPC_THEO05_ENTRANCE);
  if (!e) return;
  npc_light(e, -12, 0xFFFFFF, 32, 64);
  Ent *cs = scene_new(&ENTRANCE, entrance_end, true, false);
  if (cs) ST(cs, Entrance)->theo = e;
}

/* ---------------------------------------------------------------- NPC05_Theo_Mirror and CS05_TheoInMirror */
typedef struct {
  Cutscene cs;
  Ent *theo, *tb;
  Walk walk;
  int final_x;
} InMirror;
static void inmirror_update(Ent *e) {
  InMirror *s = ST(e, InMirror);
  Player *p = &g_player;
  Co *c = &s->cs.co;
  CO_BEGIN(c);
  player_dummy(p);
  CO_WALK(c, s->walk, walk_to(s->theo->x - 16));
  CO_WAIT(c, 0.5f);
  NPC_(s->theo)->spr.sx = -1;
  CO_WAIT(c, 0.25f);
  CO_SAY(c, s->tb, "ch5_theo_mirror", NULL, NULL);
  npc_move_to(s->theo, v2(s->theo->x + 64, s->theo->y), false, 0, false);   /* a coroutine of its own */
  CO_WAIT(c, 0.4f);
  CO_WALK(c, s->walk, walk_exact(s->final_x));
  cutscene_end(e);
  return;
  CO_END(c);
}
static void inmirror_end(Ent *e, bool skipped) {
  InMirror *s = ST(e, InMirror);
  Player *p = &g_player;
  (void)skipped;
  player_free(p);
  p->ent->x = (float)s->final_x;
  actor_move_v(p->ent, 200, NULL, NULL);
  p->speed = v2(0, 0);
  ent_remove(s->theo);
  level_set_flag("theoInMirror", true);
}
static const EntClass INMIRROR = {.name = "CS05_TheoInMirror", .size = sizeof(InMirror), .update = inmirror_update};
static void theo_mirror_think(Ent *e) {
  Npc *n = NPC_(e);
  Player *p = level_player();
  if (!n->i[0] && p && p->ent->x > e->x - 64) {
    n->i[0] = 1;
    Ent *cs = scene_new(&INMIRROR, inmirror_end, true, false);
    if (cs) ST(cs, InMirror)->theo = e, ST(cs, InMirror)->final_x = (int)e->x + 24;
  }
}
static void new_theo_mirror(const EData *d) {
  if (level_get_flag("theoInMirror")) return;
  Ent *e = theo_npc_new(d, NPC_THEO05_MIRROR);
  if (!e) return;
  e->visible = 0;   /* seen in the mirror only (MirrorReflection.IgnoreEntityVisible) */
  NPC_(e)->think = theo_mirror_think;
}

/* ---------------------------------------------------------------- NPC05_Badeline (evil_05) and CS05_Badeline */
typedef struct {
  Co co;
  Tween tw;                /* MoveToNode's */
  Job job;
  V2 nodes[4], from, to;
  float speed;
  Ent *shadow, *cs;
  uint8_t nnodes, index, fallen, scene;
} Evil;
static const EntClass EVIL;
static void evil_move_to_node(Ent *e, int index) {
  Evil *v = ST(e, Evil);
  v->from = v2(v->shadow->x, v->shadow->y);
  v->to = v->nodes[index];
  tween_start(&v->tw, 0.5f);
  job_start(&v->job);
}
typedef struct {
  Cutscene cs;
  Ent *npc, *tb;
  Step step;
  V2 focus;
  Co ev;
  int8_t ev_i;
  uint8_t index, moved;
  char key[24];
} CsEvil;
static bool csevil_nb(Ent *e, CsEvil *s, Co *c) {   /* BadelineLeaves */
  (void)e;
  NB_BEGIN(c);
  NB_WAIT(c, 0.1f);
  s->moved = 1;
  evil_move_to_node(s->npc, s->index);
  NB_WAIT(c, 0.5f);
  spr_play(&g_player.spr, A_player_tiredStill, false);
  NB_WAIT(c, 0.5f);
  spr_play(&g_player.spr, A_player_idle, false);
  NB_WAIT(c, 0.6f);
  NB_END(c);
}
static bool csevil_ev(void *ctx, int i) {
  CsEvil *s = ST((Ent *)ctx, CsEvil);
  return !csevil_nb(ctx, s, ev_co(&s->ev, &s->ev_i, i));
}
static void csevil_update(Ent *e) {
  CsEvil *s = ST(e, CsEvil);
  Player *p = &g_player;
  Co *c = &s->cs.co;
  CO_BEGIN(c);
  player_dummy(p);
  CO_WAIT(c, 0.25f);
  if (s->index == 3) {
    p->dummy_auto_animate = false;
    spr_play(&p->spr, A_player_tired, false);
    CO_WAIT(c, 0.2f);
  }
  while (p->ent && !on_ground(p->ent)) CO_YIELD(c);
  {
    Ent *sh = ST(s->npc, Evil)->shadow;
    s->focus = v2add(v2mul(v2add(v2(sh->x, sh->y - 4), player_center(p)), 0.5f), v2(-g_level.cam.x, -12 - g_level.cam.y));
  }
  CO_ZOOM_TO(c, &s->step, s->focus, 2, 0.5f);
  s->ev_i = -1;
  path2(s->key, "ch5_shadow_maddy_", NULL, -1);
  s->key[17] = (char)('0' + s->index), s->key[18] = 0;
  CO_SAY(c, s->tb, s->key, csevil_ev, e);
  if (!s->moved) evil_move_to_node(s->npc, s->index);
  CO_ZOOM_BACK(c, &s->step, 0.5f);
  cutscene_end(e);
  return;
  CO_END(c);
}
static void csevil_end(Ent *e, bool skipped) {
  CsEvil *s = ST(e, CsEvil);
  Evil *v = ST(s->npc, Evil);
  (void)skipped;
  v->shadow->x = v->nodes[s->index].x, v->shadow->y = v->nodes[s->index].y;   /* SnapToNode */
  player_free(&g_player);
  char f[12] = "badeline_0";
  f[9] = (char)('0' + s->index);
  level_set_flag(f, true);
}
static const EntClass CSEVIL = {.name = "CS05_Badeline", .size = sizeof(CsEvil), .update = csevil_update};
static bool evil_flag(int i) {
  char f[12] = "badeline_0";
  f[9] = (char)('0' + i);
  return level_get_flag(f);
}
static void evil_update(Ent *e) {
  Evil *v = ST(e, Evil);
  BDummy *b = BD(v->shadow);
  Player *p = level_player();
  Room *rm = g_level.room;
  if (g_level.transitioning) {   /* TransitionListener.OnOut */
    if (e->room != g_level.room_slot) {
      float a = 1 - fminf(1, g_level.tr_at * 2);
      b->hair_alpha = a, b->spr.alpha = a8(a), b->light_alpha = a;
    }
    return;
  }
  if (job_due(&v->job)) {   /* MoveToNode's tween */
    bool done = tween_update(&v->tw);
    float k = ease_cube_inout(v->tw.percent);
    V2 at = v2lerp(v->from, v->to, k);
    v->shadow->x = at.x, v->shadow->y = at.y;
    if (level_on_interval(0.03f))
      particles_emit(PL_FG, &P_BadelineOldsite_P_Vanish, 2, v2add(at, v2(0, -6)), v2(2, 2), P_BadelineOldsite_P_Vanish.direction);
    if (k >= 0.1f && k <= 0.9f && level_on_interval(0.05f)) {
      uint16_t t = spr_tex(&b->spr);
      if (t != 0xFFFF) trail_add(at.x + b->spr_pos.x, at.y + b->spr_pos.y, t, b->spr.ox, b->spr.oy, b->spr.sx, b->spr.sy, 0xFF0000, 0.5f, v->shadow->depth + 1);
    }
    if (done) job_end(&v->job);
  }
  Co *c = &v->co;
  CO_BEGIN(c);
  b->spr.sx = -1;
  if (!v->scene) {   /* FirstScene */
    b->float_speed = 150;
    for (;;) {
      p = level_player();
      if (p && p->ent->y > rm->y + 180 && !on_ground(p->ent) && !v->fallen) {
        player_set_state(p, ST_TEMPLEFALL);
        v->fallen = 1;
      }
      if (p && p->ent->x > e->x - 64 && p->ent->y > e->y - 32) break;
      CO_YIELD(c);
    }
    evil_move_to_node(e, 0);
    while (v->shadow->x < rm->x + rm->w + 8) {
      CO_YIELD(c);
      p = level_player();
      if (p && p->ent->x > v->shadow->x - 24) v->shadow->x = p->ent->x + 24;
    }
  } else {   /* SecondScene */
    b->float_speed = 300, b->float_accel = 400;
    CO_WAIT(c, 0.1f);
    while (v->index < v->nnodes) {
      p = level_player();
      while (!p || v2len(v2sub(v2(p->ent->x, p->ent->y), v2(v->shadow->x, v->shadow->y))) > 70) {
        CO_YIELD(c);
        p = level_player();
      }
      if (v->index < 4 && !evil_flag(v->index)) {
        v->cs = scene_new(&CSEVIL, csevil_end, true, false);
        if (v->cs) {
          CsEvil *s = ST(v->cs, CsEvil);
          s->npc = e, s->index = v->index;
        }
        CO_YIELD(c);
        while (v->cs && v->cs->cls == &CSEVIL && v->cs->dead != 1) CO_YIELD(c);
        v->index++;
      }
    }
  }
  ent_remove(v->shadow);
  ent_remove(e);
  CO_END(c);
}
static const EntClass EVIL = {.name = "npc05Badeline", .size = sizeof(Evil), .update = evil_update};
static void new_evil(const EData *d) {
  const char *room = level_room_name_of(d->room);
  bool first = !strcmp(room, "c-00");
  int start = 0;
  if (first) {
    if (level_visited("c-01")) return;
  } else if (!strcmp(room, "c-01")) {
    if (level_visited("c-01b")) return;
    while (start < 4 && evil_flag(start)) start++;
    if (start >= 4) return;
  } else
    return;
  Ent *e = ent_new(&EVIL, d->x, d->y);
  if (!e) return;
  e->visible = e->collidable = 0;
  e->tags = TAG_TRANSITION_UPDATE;
  Evil *v = ST(e, Evil);
  v->nnodes = (uint8_t)(d->nnodes < 4 ? d->nnodes : 4);
  for (int i = 0; i < v->nnodes; i++) v->nodes[i] = ed_node(d, i);
  v->scene = !first;
  v->index = (uint8_t)start;
  v->shadow = bd_new(start > 0 ? v->nodes[start - 1] : v2(d->x, d->y));
  if (!v->shadow) {
    ent_remove(e);
    return;
  }
  v->shadow->depth = -1000000;
}

/* ---------------------------------------------------------------- CS05_SeeTheo */
typedef struct {
  Cutscene cs;
  Ent *tb, *theo;
  Step step, bstep;
  V2 focus;
  Co ev, br;
  int8_t ev_i;
  uint8_t index, brightening;
} SeeTheo;
static bool seetheo_brighten(SeeTheo *s) {   /* Brighten() */
  Co *c = &s->br;
  NB_BEGIN(c);
  step_reset(&s->bstep);
  NB_NEST(c, zoom_back(&s->bstep, 0.5f));
  NB_WAIT(c, 0.3f);
  g_session.dark_room_alpha = 0.3f;
  while (g_level.lighting != g_session.dark_room_alpha) {
    g_level.lighting = approach(g_level.lighting, g_session.dark_room_alpha, DT * 0.5f);
    NB_YIELD(c);
  }
  NB_END(c);
}
static bool seetheo_nb(SeeTheo *s, Co *c, int i) {
  Player *p = &g_player;
  switch (i) {
    case 0:   /* ZoomIn */
      NB_BEGIN(c);
      s->focus = v2sub(v2lerp(v2(p->ent->x, p->ent->y), v2(s->theo->x, s->theo->y), 0.5f), v2(g_level.cam.x, g_level.cam.y + 20));
      NB_ZOOM_TO(c, &s->step, s->focus, 2, 0.5f);
      NB_END(c);
    case 1:   /* MadelineTurnsAround */
      NB_BEGIN(c);
      NB_WAIT(c, 0.3f);
      p->facing = -1;
      NB_WAIT(c, 0.1f);
      NB_END(c);
    case 2:   /* WaitABit */
      NB_BEGIN(c);
      NB_WAIT(c, 1);
      NB_END(c);
    default:   /* MadelineTurnsBackAndBrighten */
      NB_BEGIN(c);
      NB_WAIT(c, 0.1f);
      memset(&s->br, 0, sizeof s->br);
      s->brightening = 2;
      NB_WAIT(c, 0.2f);
      p->facing = 1;
      NB_WAIT(c, 0.1f);
      while (s->brightening) NB_YIELD(c);
      NB_END(c);
  }
}
static bool seetheo_ev(void *ctx, int i) {
  SeeTheo *s = ST((Ent *)ctx, SeeTheo);
  return !seetheo_nb(s, ev_co(&s->ev, &s->ev_i, i), i);
}
static void seetheo_update(Ent *e) {
  SeeTheo *s = ST(e, SeeTheo);
  Player *p = &g_player;
  Co *c = &s->cs.co;
  CO_BEGIN(c);
  while (!p->ent || !on_ground(p->ent)) CO_YIELD(c);
  player_dummy(p);
  CO_WAIT(c, 0.25f);
  s->theo = theo_entity();
  if (s->theo && sgn(p->ent->x - s->theo->x) != 0) p->facing = sgn(s->theo->x - p->ent->x);
  CO_WAIT(c, 0.25f);
  s->ev_i = -1;
  if (s->index == 0) CO_SAY(c, s->tb, "ch5_see_theo", seetheo_ev, e);
  else if (s->index == 1) CO_SAY(c, s->tb, "ch5_see_theo_b", NULL, NULL);
  CO_ZOOM_BACK(c, &s->step, 0.5f);
  cutscene_end(e);
  return;
  CO_END(c);
}
static void seetheo_after(Ent *e) {   /* the added Brighten coroutine runs after the main one */
  SeeTheo *s = ST(e, SeeTheo);
  if (s->brightening == 2) s->brightening = 1;   /* added this update: it starts with the next */
  else if (s->brightening && !seetheo_brighten(s)) s->brightening = 0;
}
static void seetheo_tick(Ent *e) {
  seetheo_update(e);
  if (e->dead != 1) seetheo_after(e);
}
static void seetheo_end(Ent *e, bool skipped) {
  Player *p = &g_player;
  (void)e, (void)skipped;
  player_free(p);
  p->force_camera_update = false;
  p->dummy_auto_animate = true;
  g_session.dark_room_alpha = 0.3f;
  g_level.lighting = g_session.dark_room_alpha;
  level_set_flag("seeTheoInCrystal", true);
}
static const EntClass SEETHEO = {.name = "CS05_SeeTheo", .size = sizeof(SeeTheo), .update = seetheo_tick};
static void see_theo(int index) {
  Ent *e = scene_new(&SEETHEO, seetheo_end, true, false);
  if (e) ST(e, SeeTheo)->index = (uint8_t)index;
}

/* ---------------------------------------------------------------- CS05_SaveTheo */
typedef struct {
  Cutscene cs;
  Ent *theo, *tb;
  V2 end;
  Walk walk;
  Step step;
  Co ev;
  int8_t ev_i;
  uint8_t dashing;
  float dash_x;
} SaveTheo;
/* TryToBreakCrystal: Madeline dashes up into the pedestal (Player.OverrideDashDirection, MInput.Disabled) */
static bool savetheo_nb(SaveTheo *s, Co *c) {
  Player *p = &g_player;
  NB_BEGIN(c);
  {
    Ent *ped = first_of(&PEDESTAL);
    if (ped) ped->collidable = 1;
  }
  NB_WALK(c, s->walk, walk_to(s->theo->x));
  NB_WAIT(c, 0.1f);
  NB_ZOOM_TO(c, &s->step, v2(160, 90), 2, 0.5f);
  p->dummy_auto_animate = false;
  spr_play(&p->spr, A_player_lookUp, false);
  NB_WAIT(c, 1);
  p->state_locked = false;
  player_set_state(p, ST_DASH);   /* StartDash */
  p->dashes = 0;
  s->dashing = 1, s->dash_x = p->ent->x;
  NB_WAIT(c, 0.1f);
  while (!on_ground(p->ent) || p->speed.y < 0) {
    p->dashes = 0;
    p->force_move_x = 0, p->force_move_x_timer = DT * 2;   /* Input.MoveX = 0 */
    NB_YIELD(c);
  }
  s->dashing = 0;
  player_set_state(p, ST_DUMMY);
  p->state_locked = true;
  p->dummy_auto_animate = true;
  NB_WALK(c, s->walk, walk_back((int)s->end.x));
  NB_WAIT(c, 1.5f);
  NB_END(c);
}
static bool savetheo_ev(void *ctx, int i) {
  SaveTheo *s = ST((Ent *)ctx, SaveTheo);
  return !savetheo_nb(s, ev_co(&s->ev, &s->ev_i, i));
}
static void savetheo_update(Ent *e) {
  SaveTheo *s = ST(e, SaveTheo);
  Player *p = &g_player;
  if (s->dashing == 1 && p->state == ST_DASH && p->co_step == 2) {
    /* the dash went the way the keys say: up it goes instead (OverrideDashDirection) */
    s->dashing = 2;
    p->ent->x = s->dash_x;
    p->speed = v2(0, -240);
    p->dash_dir = v2(0, -1);
    level_dir_shake(v2(0, -1), 0.2f);
  }
  Co *c = &s->cs.co;
  CO_BEGIN(c);
  player_dummy(p);
  p->force_camera_update = true;
  CO_WALK(c, s->walk, walk_to(s->theo->x - 18));
  p->facing = 1;
  s->ev_i = -1;
  CO_SAY(c, s->tb, "ch5_found_theo", savetheo_ev, e);
  CO_WAIT(c, 0.25f);
  CO_ZOOM_BACK(c, &s->step, 0.5f);
  cutscene_end(e);
  return;
  CO_END(c);
}
static void savetheo_end(Ent *e, bool skipped) {
  SaveTheo *s = ST(e, SaveTheo);
  Player *p = &g_player;
  (void)skipped;
  p->ent->x = s->end.x, p->ent->y = s->end.y;
  for (int i = 0; i < 64 && !on_ground(p->ent); i++) actor_move_v(p->ent, 1, NULL, NULL);
  g_level.cam = player_camera_target(p);
  level_set_flag("foundTheoInCrystal", true);
  zoom_reset();
  /* a new Player there (the old one removed): the state of a fresh one */
  p->state_locked = false;
  player_set_state(p, ST_NORMAL);
  p->speed = v2(0, 0);
  p->dashes = player_max_dashes(p);
  p->dummy_auto_animate = p->dummy_gravity = true;
  p->force_camera_update = false;
  p->facing = 1;
  Ent *ped = first_of(&PEDESTAL);
  if (ped) ped->collidable = 0, ST(ped, Pedestal)->dropped = 1;
  Ent *t = s->theo;
  t->depth = 100;
  ents_mark_unsorted();
  ST(t, Theo)->on_pedestal = 0;
  ST(t, Theo)->speed = v2(0, 0);
  for (int i = 0; i < 64 && !on_ground(t); i++) actor_move_v(t, 1, NULL, NULL);
}
static const EntClass SAVETHEO = {.name = "CS05_SaveTheo", .size = sizeof(SaveTheo), .update = savetheo_update};

/* ---------------------------------------------------------------- CS05_Reflection1 */
typedef struct {
  Cutscene cs;
  Ent *tb;
  Walk walk;
  Step step;
  Co ev;
  int8_t ev_i;
} Reflect;
static bool reflect_nb(Reflect *s, Co *c, int i) {
  Player *p = &g_player;
  switch (i) {
    case 0:   /* MadelineFallsToKnees */
      NB_BEGIN(c);
      NB_WAIT(c, 0.2f);
      p->dummy_auto_animate = false;
      spr_play(&p->spr, A_player_tired, false);
      NB_WAIT(c, 0.2f);
      NB_ZOOM_TO(c, &s->step, v2(90, 116), 2, 0.5f);
      NB_WAIT(c, 0.2f);
      NB_END(c);
    case 1:   /* MadelineStopsPanicking */
      NB_BEGIN(c);
      NB_WAIT(c, 0.8f);
      spr_play(&p->spr, A_player_tiredStill, false);
      NB_WAIT(c, 0.4f);
      NB_END(c);
    default:   /* MadelineGetsUp */
      p->dummy_auto_animate = true;
      spr_play(&p->spr, A_player_idle, false);
      return false;
  }
}
static bool reflect_ev(void *ctx, int i) {
  Reflect *s = ST((Ent *)ctx, Reflect);
  return !reflect_nb(s, ev_co(&s->ev, &s->ev_i, i), i);
}
static void reflect_update(Ent *e) {
  Reflect *s = ST(e, Reflect);
  Player *p = &g_player;
  Co *c = &s->cs.co;
  CO_BEGIN(c);
  player_dummy(p);
  p->force_camera_update = true;
  {
    Ent *m = first_of(&MIRROR);
    CO_WALK(c, s->walk, walk_to(m ? m->x + ST(m, Mirror)->w / 2.f + 8 : p->ent->x));
  }
  CO_WAIT(c, 0.2f);
  p->facing = -1;
  CO_WAIT(c, 0.3f);
  s->ev_i = -1;
  if (!p->dead) CO_SAY(c, s->tb, "ch5_reflection", reflect_ev, e);
  else CO_WAIT(c, 100);
  CO_ZOOM_BACK(c, &s->step, 0.5f);
  cutscene_end(e);
  return;
  CO_END(c);
}
static void reflect_end(Ent *e, bool skipped) {
  (void)e, (void)skipped;
  player_free(&g_player);
  g_player.force_camera_update = false;
  level_set_flag("reflection", true);
}
static const EntClass REFLECT = {.name = "CS05_Reflection1", .size = sizeof(Reflect), .update = reflect_update};

/* ---------------------------------------------------------------- TheoPhone and CS05_TheoPhone */
static void phone_render(Ent *e) {
  gfx_tex_ex(T_characters_theo_phone, e->x, e->y, 4, 8, 1, 1, 0, 0xFFFF, 255, 0);   /* JustifyOrigin(0.5, 1) */
  /* its light blinks every half second */
  if ((int)floorf(g_level.time_active / 0.5f) % 2 == 0) light_add(e->x, e->y, 0x7CFC00, 1, 8, 16);   /* Color.LawnGreen */
}
static const EntClass PHONE = {.name = "theoPhone", .render = phone_render};
typedef struct {
  Cutscene cs;
  Ent *tb;
  Walk walk;
  Step step;
  Co ev;
  int8_t ev_i;
  float target_x;
} CsPhone;
static void phone_remove(void) {
  Ent *ph = first_of(&PHONE);
  if (ph) ent_remove(ph);
}
static bool csphone_nb(CsPhone *s, Co *c, int i) {
  Player *p = &g_player;
  if (i == 0) {   /* WalkToPhone */
    NB_BEGIN(c);
    NB_WAIT(c, 0.25f);
    NB_WALK(c, s->walk, walk_exact((int)s->target_x));
    p->facing = -1;
    NB_WAIT(c, 0.5f);
    p->dummy_auto_animate = false;
    spr_play(&p->spr, A_player_duck, false);
    NB_WAIT(c, 0.5f);
    NB_END(c);
  }
  NB_BEGIN(c);   /* StandBackUp */
  phone_remove();
  NB_WAIT(c, 0.6f);
  spr_play(&p->spr, A_player_idle, false);
  NB_WAIT(c, 0.2f);
  NB_END(c);
}
static bool csphone_ev(void *ctx, int i) {
  CsPhone *s = ST((Ent *)ctx, CsPhone);
  return !csphone_nb(s, ev_co(&s->ev, &s->ev_i, i), i);
}
static void csphone_update(Ent *e) {
  CsPhone *s = ST(e, CsPhone);
  Player *p = &g_player;
  Co *c = &s->cs.co;
  CO_BEGIN(c);
  player_set_state(p, ST_DUMMY);
  if (p->ent->x != s->target_x) p->facing = sgn(s->target_x - p->ent->x);
  CO_WAIT(c, 0.5f);
  CO_ZOOM_TO(c, &s->step, v2(80, 60), 2, 0.5f);
  s->ev_i = -1;
  CO_SAY(c, s->tb, "CH5_PHONE", csphone_ev, e);
  CO_ZOOM_BACK(c, &s->step, 0.5f);
  cutscene_end(e);
  return;
  CO_END(c);
}
static void csphone_end(Ent *e, bool skipped) {
  (void)e, (void)skipped;
  phone_remove();
  player_set_state(&g_player, ST_NORMAL);
}
static const EntClass CSPHONE = {.name = "CS05_TheoPhone", .size = sizeof(CsPhone), .update = csphone_update};

/* ---------------------------------------------------------------- InteractTrigger (ch5_theo_phone, ch5_see_theo_b) */
typedef struct {
  Talk talk;
  float timeout;
  uint8_t which, used;   /* which: 0 ch5_theo_phone, 1 ch5_mirror_reflection, 2 ch5_see_theo, 3 ch5_see_theo_b */
} Interact;
static const char *const INTERACT_EVENTS[4] = {"ch5_theo_phone", "ch5_mirror_reflection", "ch5_see_theo", "ch5_see_theo_b"};
static bool interact_flag(int which) {
  char f[32];
  return level_get_flag(path2(f, "it_", INTERACT_EVENTS[which], -1));
}
static void interact_update(Ent *e) {
  Interact *it = ST(e, Interact);
  if (it->used) {
    if ((it->timeout -= DT) <= 0) ent_remove(e);
    return;
  }
  if (interact_flag(it->which)) {
    ent_remove(e);
    return;
  }
  if (!talk_update(&it->talk, e)) return;
  Player *p = level_player();
  if (!p) return;
  /* OnTalk */
  if (it->which == 0) {
    Ent *cs = scene_new(&CSPHONE, csphone_end, true, false);
    if (cs) ST(cs, CsPhone)->target_x = e_cxm(e);
  } else if (it->which == 1) {
    scene_new(&REFLECT, reflect_end, true, false);
  } else
    see_theo(it->which - 2);
  char f[32];
  level_set_flag(path2(f, "it_", INTERACT_EVENTS[it->which], -1), true);
  it->used = 1;
  it->timeout = 0.25f;
}
static void interact_render(Ent *e) { talk_render(&ST(e, Interact)->talk, e); }
static const EntClass INTERACT = {.name = "interactTrigger", .size = sizeof(Interact), .update = interact_update,
                                  .render = interact_render, .removed = talk_removed};
static bool new_interact(const EData *d) {
  const char *ev = EAS(d, interactTrigger, event);
  int which = -1;
  for (int i = 0; i < 4; i++)
    if (!strcmp(ev, INTERACT_EVENTS[i])) which = i;
  if (which < 0) return false;
  if (interact_flag(which)) return true;   /* Added: every event done */
  int w = (int)EA(d, interactTrigger, width), h = (int)EA(d, interactTrigger, height);
  Ent *e = ent_new(&INTERACT, d->x, d->y);
  if (!e) return true;
  ent_box(e, (float)w, (float)h, 0, 0);
  e->depth = -1000000;
  Interact *it = ST(e, Interact);
  V2 at = d->nnodes ? v2sub(ed_node(d, 0), v2(d->x, d->y)) : v2(w / 2.f, 0);
  talk_init(&it->talk, 0, 0, w, h, at);
  it->talk.must_face = false;
  it->which = (uint8_t)which;
  if (which == 0) {
    Ent *ph = ent_new(&PHONE, d->x + w / 2.f - 8, d->y + h - 1);
    if (ph) ph->collidable = 0;
  }
  return true;
}

/* ---------------------------------------------------------------- EventTrigger */
enum { EV_SEE_THEO, EV_FOUND_THEO, EV_MIRROR_REFLECTION, EV_CANCEL_SEE_THEO };
typedef struct { uint8_t which, triggered, brighten; } Event;
static void event_enter(Ent *e, Player *p) {
  Event *ev = ST(e, Event);
  if (ev->triggered) return;
  ev->triggered = 1;
  switch (ev->which) {
    case EV_SEE_THEO: see_theo(0); break;
    case EV_FOUND_THEO:
      if (!level_get_flag("foundTheoInCrystal")) {
        Ent *t = theo_entity();
        Ent *cs = t ? scene_new(&SAVETHEO, savetheo_end, true, false) : NULL;
        if (cs) ST(cs, SaveTheo)->theo = t, ST(cs, SaveTheo)->end = v2(t->x - 24, t->y);
      }
      break;
    case EV_MIRROR_REFLECTION:
      if (!level_get_flag("reflection")) scene_new(&REFLECT, reflect_end, true, false);
      break;
    default: {
      level_set_flag("it_ch5_see_theo", true);
      level_set_flag("it_ch5_see_theo_b", true);
      char f[40];
      level_set_flag(path2(f, "ignore_darkness_", level_room_name(), -1), true);
      ev->brighten = 1;
    }
  }
  (void)p;
}
static void event_update(Ent *e) {   /* Brighten() */
  Event *ev = ST(e, Event);
  if (ev->brighten && (g_level.lighting = approach(g_level.lighting, 0.15f, DT * 4)) == 0.15f) ev->brighten = 0;
}
static const EntClass EVENTTRIG = {.name = "eventTrigger", .size = sizeof(Event), .update = event_update, .kind = KIND_TRIGGER,
                                   .more = &(const EntMore){.on_enter = event_enter}};
static bool new_event(const EData *d) {
  static const char *const names[4] = {"ch5_see_theo", "ch5_found_theo", "ch5_mirror_reflection", "cancel_ch5_see_theo"};
  const char *ev = EAS(d, eventTrigger, event);
  int which = -1;
  for (int i = 0; i < 4; i++)
    if (!strcmp(ev, names[i])) which = i;
  if (which < 0) return false;
  Ent *e = ent_new(&EVENTTRIG, d->x, d->y);
  if (e) ent_box(e, EA(d, eventTrigger, width), EA(d, eventTrigger, height), 0, 0), e->visible = 0, ST(e, Event)->which = (uint8_t)which;
  return true;
}

/* ---------------------------------------------------------------- triggers */
static const EntClass CHECKPOINT_BLOCKER = {.name = "checkpointBlockerTrigger", .kind = KIND_TRIGGER};

/* ---------------------------------------------------------------- the factories */
bool ents_ch5(const EData *d) {
  story5_room();
  switch (d->type) {
    case ET_npc: {
      const char *who = EAS(d, npc, npc);
      if (!strcmp(who, "theo_05_entrance")) new_theo_entrance(d);
      else if (!strcmp(who, "theo_05_inmirror")) new_theo_mirror(d);
      else if (!strcmp(who, "evil_05")) new_evil(d);
      else return false;
      return true;
    }
    case ET_torch: new_torch(d); return true;
    case ET_templeEye: new_eye(d); return true;
    case ET_templeGate: new_gate(d); return true;
    case ET_dashSwitchH: new_dswitch(d, true); return true;
    case ET_dashSwitchV: new_dswitch(d, false); return true;
    case ET_swapBlock: new_swap(d); return true;
    case ET_templeCrackedBlock: new_cracked(d); return true;
    case ET_seekerBarrier: new_barrier(d); return true;
    case ET_seeker: new_seeker(d); return true;
    case ET_seekerStatue: new_statue(d); return true;
    case ET_theoCrystal: new_theo(d); return true;
    case ET_theoCrystalPedestal: new_pedestal(d); return true;
    case ET_conditionBlock: new_condition(d); return true;
    case ET_templeMirror: new_mirror(d); return true;
    case ET_templeMirrorPortal: new_portal(d); return true;
    case ET_templeBigEyeball: new_bigeye(d); return true;
    case ET_playerSeeker: new_pseeker(d); return true;
  }
  return false;
}

/* ---------------------------------------------------------------- MiniTextboxTrigger */
typedef struct { uint32_t hash; uint8_t mode, once, triggered; int8_t deaths; const char *dialog; } MiniTrig;
static void minitrig_fire(Ent *e) {   /* Trigger(): one of its dialogs (they are split by commas) */
  MiniTrig *t = ST(e, MiniTrig);
  if (t->triggered || (t->deaths >= 0 && g_session.deaths_in_current_level != (uint32_t)t->deaths)) return;
  t->triggered = 1;
  int n = 1;
  for (const char *c = t->dialog; *c; c++) n += *c == ',';
  int k = rndi(n);
  const char *a = t->dialog;
  while (k--) a = strchr(a, ',') + 1;
  char key[40];
  int len = 0;
  while (*a == ' ') a++;
  while (a[len] && a[len] != ',' && len < 39) len++;
  while (len && a[len - 1] == ' ') len--;
  memcpy(key, a, (size_t)len);
  key[len] = 0;
  mini_textbox(key);
  if (t->once) level_set_do_not_load(t->hash);
}
static void minitrig_awake(Ent *e) {
  if (ST(e, MiniTrig)->mode == 1) minitrig_fire(e);   /* OnLevelStart */
}
static void minitrig_enter(Ent *e, Player *p) {
  (void)p;
  if (ST(e, MiniTrig)->mode == 0) minitrig_fire(e);   /* OnPlayerEnter */
}
static void minitrig_update(Ent *e) {   /* OnTheoEnter: a HoldableCollider */
  Ent *t = theo_entity();
  if (ST(e, MiniTrig)->mode == 2 && t && collide_ent_at(e, e->x, e->y, t)) minitrig_fire(e);
}
static const EntClass MINITRIG = {.name = "minitextboxTrigger", .size = sizeof(MiniTrig), .update = minitrig_update,
                                  .awake = minitrig_awake, .kind = KIND_TRIGGER,
                                  .more = &(const EntMore){.on_enter = minitrig_enter}};
static void new_minitrig(const EData *d) {
  uint32_t h = level_entity_hash(d);
  Ent *e = ent_new(&MINITRIG, d->x, d->y);
  if (!e) return;
  ent_box(e, EA(d, minitextboxTrigger, width), EA(d, minitextboxTrigger, height), 0, 0);
  e->visible = 0;
  MiniTrig *t = ST(e, MiniTrig);
  const char *mode = EAS(d, minitextboxTrigger, mode);
  t->mode = !strcmp(mode, "OnLevelStart") ? 1 : !strcmp(mode, "OnTheoEnter") ? 2 : 0;
  t->once = EAB(d, minitextboxTrigger, only_once);
  t->deaths = (int8_t)EA(d, minitextboxTrigger, death_count);
  t->dialog = EAS(d, minitextboxTrigger, dialog_id);
  t->hash = h;
}

bool trigs_ch5(const EData *d) {
  switch (d->type) {
    case TT_checkpointBlockerTrigger: {
      Ent *e = ent_new(&CHECKPOINT_BLOCKER, d->x, d->y);
      if (e) ent_box(e, EA(d, checkpointBlockerTrigger, width), EA(d, checkpointBlockerTrigger, height), 0, 0), e->visible = 0;
      return true;
    }
    case TT_minitextboxTrigger: new_minitrig(d); return true;
    case TT_eventTrigger: return new_event(d);
    case TT_interactTrigger: return new_interact(d);
  }
  return false;
}

/* this chapter's tables: in the memory it is given while it is played (res_chapter_ram) */
uint32_t ch5_ram(void) { return END_story5; }
