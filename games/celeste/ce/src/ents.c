/* Entities: what the rooms hold (their records: tools/pack_ce.py Chapter.entity), after src/entities.c and berry.c,
 * which port the game's own classes. Platforms move whole pixels and carry or push Madeline as Monocle's Solids do. */
#include "ce.h"
#include "fconst.h"

Ent *g_ents;
int g_nents;
static int trig_ox, trig_oy;

/* a record: u8 type, u8 size, s16 x, y, u16 w, h, u16 id, u8 nodes, (s16 x, y) each, u8 flags, u8 n, u16 vals */
#define REC_X(r) S16((r) + 2)
#define REC_Y(r) S16((r) + 4)
#define REC_W(r) (int)R16((r) + 6)
#define REC_H(r) (int)R16((r) + 8)
#define REC_ID(r) (int)R16((r) + 10)
#define REC_NODES(r) ((r)[12])
static const uint8_t *rec_node(const uint8_t *r, int i) { return r + 13 + 4 * i; }
static int rec_flags(const uint8_t *r) { return r[13 + 4 * r[12]]; }
static int rec_nvals(const uint8_t *r) { return r[14 + 4 * r[12]]; }
static int rec_val(const uint8_t *r, int i) { return (int)R16(r + 15 + 4 * r[12] + 2 * i); }

Ent *ent_new(int type, int x, int y) {
  Ent *e = NULL;
  for (int i = 0; i < g_nents; i++)
    if (g_ents[i].type == 255) { e = &g_ents[i]; break; }
  if (!e) {
    if (g_nents >= MAX_ENTS) return NULL;
    e = &g_ents[g_nents++];
  }
  memset(e, 0, sizeof *e);
  e->type = (uint8_t)type;
  e->flags = EF_ACTIVE | EF_VISIBLE | EF_COLLIDABLE;
  e->room = (uint8_t)g_level.slot;
  e->x = x, e->y = y;
  return e;
}
void ent_free(Ent *e) {
  for (int k = 0; k < 2; k++)   /* what it drew goes */
    if (e->bb[k][0] < e->bb[k][2]) draw_gone_buf(k, e->bb[k][0], e->bb[k][1], e->bb[k][2], e->bb[k][3]);
  e->type = 255;
  e->flags = 0;
  while (g_nents && g_ents[g_nents - 1].type == 255) g_nents--;
}
static void box(Ent *e, int w, int h, int x, int y) { e->hw = (int16_t)w, e->hh = (int16_t)h, e->hx = (int16_t)x, e->hy = (int16_t)y; }

bool rect_hits(int l, int t, int r, int b, const Ent *o) {
  return r > e_left(o) && b > e_top(o) && l < e_right(o) && t < e_bottom(o);
}
bool ent_collide_at(const Ent *e, int x, int y, const Ent *o) {
  int l = x + e->hx, t = y + e->hy;
  return rect_hits(l, t, l + e->hw, t + e->hh, o);
}

/* ---------------------------------------------------------------- Madeline and the platforms */
extern bool player_riding(const Ent *s), player_riding_jumpthru(const Ent *j);
extern void player_push_h(int move, Ent *pusher), player_push_v(int move, Ent *pusher);
extern void player_carry_h(int move), player_carry_v(int move);
extern void player_set_lift(f32 lx, f32 ly);
extern bool player_climbing(void);
static Ent *pl(void) { return g_player.e && !g_player.dead ? g_player.e : NULL; }
static bool player_on_top(const Ent *s) { Ent *p = pl(); return p && ent_collide_at(s, s->x, s->y - 1, p); }
static bool player_climbing_on(const Ent *s) {
  Ent *p = pl();
  if (!p || !player_climbing()) return false;
  return g_player.facing < 0 ? ent_collide_at(s, s->x + 1, s->y, p) : ent_collide_at(s, s->x - 1, s->y, p);
}

static void movers_move(Ent *p, int dx, int dy) {   /* StaticMovers ride along */
  for (int i = 0; i < g_nents; i++)
    if (g_ents[i].type != 255 && g_ents[i].ptr == p && g_ents[i].type < 64) g_ents[i].x += dx, g_ents[i].y += dy;
}
static void solid_move_h_exact(Ent *s, int move) {
  Ent *p = pl();
  bool rider = p && (s->flags & EF_COLLIDABLE) && player_riding(s);
  s->x += move;
  movers_move(s, move, 0);
  if (!(s->flags & EF_COLLIDABLE) || !p) return;
  if (ent_collide_at(s, s->x, s->y, p)) {
    int mv = move > 0 ? e_right(s) - e_left(p) : e_left(s) - e_right(p);
    s->flags &= ~EF_COLLIDABLE;
    player_push_h(mv, s);
    player_set_lift(s->liftx, s->lifty);
    s->flags |= EF_COLLIDABLE;
  } else if (rider) {
    s->flags &= ~EF_COLLIDABLE;
    player_carry_h(move);
    player_set_lift(s->liftx, s->lifty);
    s->flags |= EF_COLLIDABLE;
  }
}
static void solid_move_v_exact(Ent *s, int move) {
  Ent *p = pl();
  bool rider = p && (s->flags & EF_COLLIDABLE) && player_riding(s);
  s->y += move;
  movers_move(s, 0, move);
  if (!(s->flags & EF_COLLIDABLE) || !p) return;
  if (ent_collide_at(s, s->x, s->y, p)) {
    int mv = move > 0 ? e_bottom(s) - e_top(p) : e_top(s) - e_bottom(p);
    s->flags &= ~EF_COLLIDABLE;
    player_push_v(mv, s);
    player_set_lift(s->liftx, s->lifty);
    s->flags |= EF_COLLIDABLE;
  } else if (rider) {
    s->flags &= ~EF_COLLIDABLE;
    player_carry_v(move);
    player_set_lift(s->liftx, s->lifty);
    s->flags |= EF_COLLIDABLE;
  }
}
/* Platform.MoveH/MoveV (amount: px, a float), the lift speed amount / DT unless given */
static void plat_move_h(Ent *s, f32 amount) {
  s->liftx = f_div(amount, F_DT);
  s->remx = f_add(s->remx, amount);
  int n = f_round(s->remx);
  if (n) s->remx = f_sub(s->remx, f_int(n)), solid_move_h_exact(s, n);
}
static void plat_move_v(Ent *s, f32 amount) {
  s->lifty = f_div(amount, F_DT);
  s->remy = f_add(s->remy, amount);
  int n = f_round(s->remy);
  if (n) s->remy = f_sub(s->remy, f_int(n)), solid_move_v_exact(s, n);
}
/* MoveVCollideSolids: down until something solid (or a jump-through) is in the way */
static bool plat_fall(Ent *s, f32 amount) {
  s->lifty = f_div(amount, F_DT);
  s->remy = f_add(s->remy, amount);
  int n = f_round(s->remy);
  if (!n) return false;
  s->remy = f_sub(s->remy, f_int(n));
  int sg = isign(n), moved = 0;
  bool hit = false;
  while (n) {
    if (solid_at(e_left(s), e_top(s) + moved + sg, e_right(s), e_bottom(s) + moved + sg, s) ||
        (sg > 0 && jumpthru_first_outside(s, s->x, s->y + moved + sg))) {
      hit = true;
      break;
    }
    moved += sg, n -= sg;
  }
  if (moved) solid_move_v_exact(s, moved);
  return hit;
}
static void shake_update(Ent *s) {   /* Platform.Shake: a new offset every 0.04 s while shaking */
  if (s->sub & 0x80) {
    if (g_level.frame % 2 == 0) s->a2 = (int16_t)(rndn(3) - 1), s->a3 = (int16_t)(rndn(3) - 1);
    if (s->a1 > 0 && (s->a1 -= DTU) <= 0) s->sub &= 0x7F, s->a2 = s->a3 = 0;
  }
}
static void shake_start(Ent *s, int time) { s->sub |= 0x80, s->a1 = (int16_t)time; }
static void shake_stop(Ent *s) { s->sub &= 0x7F, s->a2 = s->a3 = 0; }

/* ---------------------------------------------------------------- the leader (strawberries follow Madeline) */
#define MAX_FOLLOW 8
#define PAST 41
static int pastx[PAST], pasty[PAST], npast;
static Ent *fol[MAX_FOLLOW];
static int nfol;
static int fol_delay[MAX_FOLLOW];
static int fol_index(const Ent *e) {
  for (int i = 0; i < nfol; i++)
    if (fol[i] == e) return i;
  return -1;
}
static void leader_update(void) {
  Ent *p = g_player.e;
  if (!p) return;
  int x = p->x, y = p->y - 8;
  if (g_level.frame % 2 == 0 && (npast == 0 || iabs(x - pastx[0]) + iabs(y - pasty[0]) >= 3)) {   /* every 0.02 s */
    memmove(pastx + 1, pastx, sizeof pastx - sizeof pastx[0]);
    memmove(pasty + 1, pasty, sizeof pasty - sizeof pasty[0]);
    pastx[0] = x, pasty[0] = y;
    if (npast < PAST) npast++;
  }
  for (int i = 0, k = 5; i < nfol && k < npast; i++, k += 5) {
    if (fol_delay[i] > 0) { fol_delay[i] -= DTU; continue; }
    Ent *f = fol[i];   /* 1 - 0.01 ^ dt of the way, in 1/256 px */
    f->t0 += (int)(((int32_t)(pastx[k] * 256 - f->t0) * 1210) >> 14);
    f->t1 += (int)(((int32_t)(pasty[k] * 256 - f->t1) * 1210) >> 14);
    f->x = f->t0 >> 8, f->y = f->t1 >> 8;
  }
}
static void leader_lose(Ent *e) {
  int i = fol_index(e);
  if (i < 0) return;
  memmove(fol + i, fol + i + 1, sizeof(Ent *) * (size_t)(nfol - i - 1));
  memmove(fol_delay + i, fol_delay + i + 1, sizeof(int) * (size_t)(nfol - i - 1));
  nfol--;
}

/* ---------------------------------------------------------------- strawberries */
enum { BERRY_GOLD = 1, BERRY_GHOST = 2, BERRY_WINGED = 4, BERRY_FLYING = 8, BERRY_COLLECTED = 16, BERRY_HOME = 32 };
typedef struct { uint16_t room_id; uint8_t index; } BerryKey;
extern bool save_has_berry(int chapter, int index);
extern void save_add_berry(int chapter, int index, bool golden);
extern int g_berries_now;   /* this go's strawberries (the HUD) */
static void points_new(int x, int y, int index, bool ghost);

static int berry_anim(Ent *e, int which) {   /* 0 idle, 1 collect, 2 flap */
  int base = (e->sub & BERRY_GOLD) ? A_goldberry_idle : (e->sub & BERRY_GHOST) ? A_ghostberry_idle : A_strawberry_idle;
  return base + (which == 0 ? 0 : which == 1 ? (A_strawberry_collect - A_strawberry_idle) : (A_strawberry_flap - A_strawberry_idle));
}
static void anim_step(Ent *e) {   /* a0 anim, a1 frame, t3 timer (time units); a1 = -1: done */
  const uint8_t *an = anim(e->a0);
  int delay = (int)R16(an + 2);
  if (!delay || e->a1 < 0) return;
  e->t3 += DTU;
  while (e->t3 >= delay && e->a1 >= 0) {
    e->t3 -= delay;
    if (e->a1 + 1 < an[0]) e->a1++;
    else if (an[4] != 255) e->a0 = an[4], e->a1 = 0;
    else if (an[1] & 1) e->a1 = 0;
    else e->a1 = -1;
  }
}
static int anim_sprite(const Ent *e) {
  const uint8_t *an = anim(e->a0);
  int f = e->a1 < 0 ? an[0] - 1 : imin(e->a1, an[0] - 1);
  return (int)R16(an + 6 + 2 * f);
}
static void anim_play(Ent *e, int a) {
  if (e->a0 == a && e->a1 >= 0) return;
  e->a0 = (int16_t)a, e->a1 = 0, e->t3 = 0;
}

static void berry_collect(Ent *e) {
  if (e->sub & BERRY_COLLECTED) return;
  e->sub |= BERRY_COLLECTED;
  int index = 0;
  if (fol_index(e) >= 0) {
    index = g_player.strawb_index++;
    g_player.strawb_reset = TIM(2.5f);
    leader_lose(e);
  }
  save_add_berry(g_level.chapter, rec_val(e->rec, 2), e->sub & BERRY_GOLD);
  g_berries_now++;
  e->t2 = index;
  e->depth = -32000;
  anim_play(e, berry_anim(e, 1));
}
static void berry_update(Ent *e) {
  if (!(e->sub & BERRY_COLLECTED)) {
    e->a2 = (int16_t)(e->a2 + 174);   /* the wobble: 4 radians a second (16384ths of a turn) */
    int f = fol_index(e);
    if (f >= 0 && fol_delay[f] <= 0 && f == 0) {
      Player *p = &g_player;
      bool ok = !p->dead && ((e->sub & BERRY_GOLD) ? g_level.completed : p->on_safe_ground);
      if (ok) {
        if ((e->a3 += DTU) > TIM(.15f)) berry_collect(e);
      } else if (e->a3 > 0)
        e->a3 = 0;
    } else if (e->sub & BERRY_WINGED) {
      e->t1 += e->t2;   /* flap speed, speed units */
      int n = round_mu(e->t1, e->t2);
      e->y += n, e->t1 -= n * MU;
      if (e->sub & BERRY_FLYING) {
        if (e->y < g_level.rooms[e->room].y - 16) { ent_free(e); return; }
        e->t2 = imax(e->t2 - 1200 / 6, SPD(-200)) ;
      } else {
        e->t2 = approach(e->t2, SPD(20), 170);
        int sy = e->rec ? g_level.rooms[e->room].y + REC_Y(e->rec) : e->y;
        e->y = iclamp(e->y, sy - 5, sy + 5);
      }
    }
  }
  anim_step(e);
  if ((e->sub & BERRY_COLLECTED) && e->a1 < 0) {
    points_new(e->x, e->y, e->t2, e->sub & BERRY_GHOST);
    ent_free(e);
  }
}
static void berry_touch(Ent *e) {
  if (fol_index(e) >= 0 || (e->sub & BERRY_COLLECTED) || nfol >= MAX_FOLLOW) return;
  if (e->sub & BERRY_WINGED) e->sub &= ~(BERRY_WINGED | BERRY_FLYING), anim_play(e, berry_anim(e, 0));
  fol[nfol] = e, fol_delay[nfol] = TIM(.3f), nfol++;
  e->t0 = e->x * 256, e->t1 = e->y * 256;
  e->depth = -32000;
}
static void berry_render(Ent *e) {
  int y = e->y + ((e->sub & (BERRY_WINGED | BERRY_COLLECTED)) || fol_index(e) >= 0 ? 0 : sin15(((uint16_t)e->a2 >> 4) & 1023) * 2 / 32768);
  const uint8_t *s = sprite(anim_sprite(e));
  draw_sprite(s, e->x - s[0] / 2, y - s[1] / 2, 0);
}
/* the dash makes winged ones fly off */
void ents_on_dash(void) {
  for (int i = 0; i < g_nents; i++) {
    Ent *e = &g_ents[i];
    if ((e->type == ET_strawberry || e->type == ET_goldenBerry) && (e->sub & BERRY_WINGED) && !(e->sub & BERRY_FLYING) &&
        fol_index(e) < 0)
      e->sub |= BERRY_FLYING, e->t2 = SPD(-200), e->depth = -32000;
  }
}
/* the end of the chapter: what follows is collected */
void ents_collect_berries(void) {
  while (nfol) berry_collect(fol[0]);
}
/* a death: the followers go home */
void ents_lose_followers(void) { nfol = 0, npast = 0; }

/* StrawberryPoints: "1000" and up, rising */
static void points_new(int x, int y, int index, bool ghost) {
  Ent *e = ent_new(ET_strawberry, x, y);
  if (!e) return;
  e->type = 200;
  e->room = 2;
  e->flags = EF_ACTIVE | EF_VISIBLE;
  e->depth = -32700;
  e->a0 = (int16_t)(A_strawberry_fade0 + imin(index, 5));
  (void)ghost;
}
static void points_update(Ent *e) {
  e->t1 -= SPD(8);
  int n = round_mu(e->t1, -1);
  e->y += n, e->t1 -= n * MU;
  e->x = iclamp(e->x, g_camx + 8, g_camx + 312), e->y = iclamp(e->y, g_camy + 8, g_camy + 172);
  anim_step(e);
  if (e->a1 < 0) ent_free(e);
}

/* ---------------------------------------------------------------- spikes */
static void spikes_touch(Ent *e) {
  Player *p = &g_player;
  switch (e->type) {
    case ET_spikesUp: if (!f_negative(p->sy) && e_bottom(p->e) <= e_bottom(e)) player_die(0, -1); break;
    case ET_spikesDown: if (!f_pos(p->sy)) player_die(0, 1); break;
    case ET_spikesLeft: if (!f_negative(p->sx)) player_die(-1, 0); break;
    case ET_spikesRight: if (!f_pos(p->sx)) player_die(1, 0); break;
  }
}
static void spikes_render(Ent *e) {
  const uint8_t *r = e->rec;
  int n = rec_nvals(r), x = e->x + e->a2, y = e->y + e->a3;
  if (e->ptr) x += ((Ent *)e->ptr)->a2, y += ((Ent *)e->ptr)->a3;
  for (int j = 0; j < n; j++) {
    const uint8_t *s = csprite(rec_val(r, j));
    int fw = s[0], fh = s[1];
    switch (e->type) {
      case ET_spikesUp: draw_sprite(s, x + j * 8 + 4 - fw / 2, y + 1 - fh, 0); break;
      case ET_spikesDown: draw_sprite(s, x + j * 8 + 4 - fw / 2, y - 1, 0); break;
      case ET_spikesRight: draw_sprite(s, x - 1, y + j * 8 + 4 - fh / 2, 0); break;
      case ET_spikesLeft: draw_sprite(s, x + 1 - fw, y + j * 8 + 4 - fh / 2, 0); break;
    }
  }
}

/* ---------------------------------------------------------------- tiles in a row or a box */
static void tiles_render(Ent *e, int first, int cols, int n, int ox, int oy) {
  for (int i = 0; i < n; i++) draw_tile(rec_val(e->rec, first + i), e->x + ox + (i % cols) * 8, e->y + oy + (i / cols) * 8);
}

/* ---------------------------------------------------------------- spring */
static const uint8_t SPRING_SEQ[] = {0, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 4, 5};
static void spring_touch(Ent *e) {
  if (e->type == ET_spring) {
    if (f_negative(g_player.sy)) return;
    player_super_bounce(e_top(e));
  } else if (e->type == ET_wallSpringLeft)
    player_side_bounce(1, e_right(e), e->y);
  else
    player_side_bounce(-1, e_left(e), e->y);
  e->a0 = 1, e->t0 = 0;   /* the animation */
  e->t1 = 256;            /* the wiggle */
}
static void spring_update(Ent *e) {
  if (e->a0) {
    e->t0 += DTU;
    while (e->t0 >= TIM(.07f) && e->a0) {
      e->t0 -= TIM(.07f);
      if (++e->a0 > (int)sizeof SPRING_SEQ) e->a0 = 0;
    }
  }
  if (e->t1 > 0) e->t1 = imax(e->t1 - 256 * DTU / TIM(1), 0);
}
static void spring_render(Ent *e) {
  int f = e->a0 ? SPRING_SEQ[e->a0 - 1] : 0;
  const uint8_t *s = sprite(S_objects_spring_00 + f);
  int x = e->x, y = e->y;
  if (e->ptr) x += ((Ent *)e->ptr)->a2, y += ((Ent *)e->ptr)->a3;
  if (e->type == ET_spring) draw_sprite(s, x - 8, y - 16, 0);
  else {   /* on a wall: turned a quarter (the sprite is 16 x 16, its bottom against the wall) */
    static uint8_t buf[6 + 16 * (1 + 16 * 3)];
    (void)buf;
    draw_sprite(s, e->type == ET_wallSpringLeft ? x - 8 : x - 8, y - 8, e->type == ET_wallSpringRight ? DF_FLIPX : 0);
  }
}

/* ---------------------------------------------------------------- refill */
static void refill_touch(Ent *e) {
  if (!player_use_refill(rec_flags(e->rec) & 1)) return;
  e->flags &= ~EF_COLLIDABLE;
  e->t0 = TIM(2.5f);   /* respawn */
  e->sub = 1;
  level_freeze(TIM(.05f));
}
static void refill_update(Ent *e) {
  if (e->sub == 1) e->sub = 2;
  else if (e->sub == 2) {
    level_shake(TIM(.3f) / DTU);
    e->sub = 3;
    e->depth = 8999;
    if (rec_flags(e->rec) & 2) { ent_free(e); return; }
  }
  if (e->t0 > 0) {
    e->t0 -= DTU;
    if (e->t0 <= 0 && !(e->flags & EF_COLLIDABLE)) e->flags |= EF_COLLIDABLE, e->sub = 0, e->depth = -100;
  }
  e->t1 += DTU;   /* frames: 0.1 s each */
  e->t2 += DTU;   /* the sine: 0.6 a second */
}
static void refill_render(Ent *e) {
  if (e->sub >= 3 && !(e->flags & EF_COLLIDABLE)) {
    draw_sprite(sprite(S_objects_refill_outline), e->x - 8, e->y - 8, 0);
    return;
  }
  int y = e->y + sin15(e->t2 * 1024 / 1000) * 2 / 32768;   /* SineWave(0.6) */
  int f = e->t1 / TIM(.1f) % 5;
  draw_sprite(sprite(S_objects_refill_idle00 + f), e->x - 8, y - 8, 0);
  int fl = e->t1 % TIM(2);   /* a flash every 2 s */
  if (fl < 6 * TIM(.05f)) draw_sprite(sprite(S_objects_refill_flash00 + fl / TIM(.05f)), e->x - 8, y - 8, 0);
}

/* ---------------------------------------------------------------- crumble block */
/* sub: 0 waiting, 1 shaking (t0 particles left), 2 falling soon, 3 gone; t1 timer; a0 the player was on top */
static void crumble_update(Ent *e) {
  switch (e->sub & 0x7F) {
    case 0: {
      bool top = player_on_top(e);
      if (!top && !player_climbing_on(e)) break;
      e->a0 = top;
      e->t0 = top ? 1 : 3;
      e->t1 = TIM(.2f);
      e->t2 = top ? TIM(.6f) : TIM(1);   /* the shake */
      e->sub = 1;
      break;
    }
    case 1:
      if ((e->t1 -= DTU) > 0) break;
      if (--e->t0) { e->t1 = TIM(.2f); break; }
      e->t1 = TIM(.4f), e->sub = 2;
      if (e->a0 && !player_on_top(e)) goto fall;
      break;
    case 2:
      e->t1 -= DTU;
      if (e->t1 <= 0 || (e->a0 && !player_on_top(e))) goto fall;
      break;
    case 3:   /* back after 2 s, when nothing is in its way */
      if ((e->t1 -= DTU) > 0) break;
      if (pl() && ent_collide_at(e, e->x, e->y, pl())) break;
      e->flags |= EF_COLLIDABLE;
      e->sub = 0, e->t3 = 0;
      break;
  }
  if (e->t2 > 0) e->t2 -= DTU;
  if (e->sub == 3 || e->t3 < 0) e->t3 += DTU;
  return;
fall:
  e->flags &= ~EF_COLLIDABLE;
  e->sub = 3, e->t1 = TIM(2), e->t3 = 0;
}
static void crumble_render(Ent *e) {
  int n = rec_nvals(e->rec) / 2;
  if (e->sub == 3) {   /* the pieces fall (0.4 s, each group of four 0.05 s after the last), then the outline */
    for (int i = 0; i < n; i++) {
      int t = e->t3 - (i % 4) * TIM(.05f);
      if (t < 0) t = 0;
      if (t >= TIM(.4f)) continue;
      int k = t * 256 / TIM(.4f), fall = k * k / 256 * k / 256 * (((e->x + i * 8) * 7 % 3 + 1) * 12) / 256;
      draw_tile(rec_val(e->rec, i), e->x + i * 8, e->y + fall);
    }
    if (e->t3 > TIM(.25f)) tiles_render(e, n, n, n, 0, 0);
    return;
  }
  for (int i = 0; i < n; i++) {   /* shaking: each piece a pixel off at random (from the frame: drawing twice gives the same) */
    int sx = 0, sy = 0;
    if (e->t2 > 0) {
      unsigned h = ((unsigned)g_level.frame * 37u + (unsigned)i * 11u + (unsigned)e->x) * 0x9E37u;
      sx = (int)(h >> 9 & 0xFF) % 3 - 1, sy = (int)(h >> 3 & 0xFF) % 3 - 1;
    }
    draw_tile(rec_val(e->rec, i), e->x + i * 8 + sx, e->y + sy);
  }
}

/* ---------------------------------------------------------------- falling block */
static bool fall_check(Ent *e) { return (rec_flags(e->rec) & 1) ? (pl() && player_riding(e)) : player_on_top(e); }
static void falling_update(Ent *e) {
  shake_update(e);
  switch (e->sub & 0x7F) {
    case 0:
      if (e->a0 || fall_check(e)) e->sub = (uint8_t)((e->sub & 0x80) | 1);
      break;
    case 1:
      shake_start(e, 0);
      e->t0 = TIM(.2f);
      e->sub = 0x82;
      break;
    case 2:
      if ((e->t0 -= DTU) > 0) break;
      e->t0 = TIM(.4f);
      e->sub = 0x83;
      /* fall through */
    case 3: {
      bool wait = e->a0 || fall_check(e);
      if (!wait && (rec_flags(e->rec) & 1) && pl()) wait = ent_collide_at(e, e->x - 1, e->y, pl()) || ent_collide_at(e, e->x + 1, e->y, pl());
      if (e->t0 > 0 && wait) {
        e->t0 -= DTU;
        break;
      }
      shake_stop(e);
      e->ptr2 = 0;
      e->sub = 4;
      break;
    }
    case 4: {
      Room *rm = &g_level.rooms[e->room];
      e->ptr2 = f_approach(e->ptr2, F_MaxFall, f_mul(F_DuckFriction, F_DT));   /* Approach(speed, 160, 500 * DT) */
      if (plat_fall(e, f_mul(e->ptr2, F_DT))) {
        level_dir_shake(0, 1, TIM(.3f) / DTU);
        shake_start(e, 0);
        e->t0 = TIM(.2f);
        e->sub = 0x85;
        break;
      }
      if (e_top(e) > rm->y + rm->h + 16 || (e_top(e) > rm->y + rm->h - 1 && solid_at(e_left(e), e_top(e) + 1, e_right(e), e_bottom(e) + 1, e))) {
        for (int i = 0; i < g_nents; i++)
          if (g_ents[i].ptr == e) ent_free(&g_ents[i]);
        ent_free(e);
      }
      break;
    }
    case 5:
      if ((e->t0 -= DTU) > 0) break;
      shake_stop(e);
      if (tiles_solid(e_left(e), e_bottom(e), e_right(e), e_bottom(e) + 1)) {
        e->sub = 7;
        e->flags |= EF_SAFE;
        break;
      }
      e->sub = 6, e->t0 = 0;
      /* fall through */
    case 6:
      if ((e->t0 -= DTU) > 0) break;
      if (solid_at(e_left(e), e_top(e) + 1, e_right(e), e_bottom(e) + 1, e) || jumpthru_first_outside(e, e->x, e->y + 1)) {
        e->t0 = TIM(.1f);
        break;
      }
      e->sub = 1;
      break;
  }
}
static void block_render(Ent *e) { tiles_render(e, 0, REC_W(e->rec) / 8, rec_nvals(e->rec), e->a2, e->a3); }

/* ---------------------------------------------------------------- dash block: breaks when dashed into */
void ents_dash_into(Ent *hit, int dx, int dy, bool *rebound) {
  *rebound = false;
  if (!hit || hit->type != ET_dashBlock || !(rec_flags(hit->rec) & 2)) return;
  (void)dx, (void)dy;
  level_shake(TIM(.2f) / DTU);
  ent_free(hit);
  *rebound = true;
}

/* ---------------------------------------------------------------- fake walls: fade when entered */
static void fakewall_update(Ent *e) {
  Ent *p = pl();
  bool in = p && ent_collide_at(e, e->x, e->y, p);
  if (e->type == ET_coverupWall) {
    e->t0 = approach(e->t0, in ? 0 : 256, 256 * 4 * DTU / TIM(1));
    return;
  }
  if (e->sub) {
    e->t0 = approach(e->t0, 0, 256 * 2 * DTU / TIM(1));
    if (e->t0 <= 0) ent_free(e);
  } else if (in)
    e->sub = 1;
}
static void fakewall_render(Ent *e) {
  /* no blending at 8 bits: shown while more than half there, then gone (fades in a frame or two) */
  if (e->t0 > 128) block_render(e);
}

/* ---------------------------------------------------------------- jump-through */
static void jumpthru_render(Ent *e) { tiles_render(e, 0, 64, rec_nvals(e->rec), 0, 0); }

/* ---------------------------------------------------------------- zip mover */
/* sub: step (bit 7: shaking); t0 the frame of its move (its Ease.SineIn: ZIP_OUT and ZIP_BACK, worked out as the game
 * works them, in tools/fconst.py), t1 a wait; a0 its light */
static const f32 zip_out[ZIP_OUT_N] = ZIP_OUT, zip_back[ZIP_BACK_N] = ZIP_BACK;
static void zip_moveto(Ent *e, bool back, f32 pct) {   /* Platform.MoveTo(Vector2.Lerp(from, to, pct)) */
  Room *rm = &g_level.rooms[e->room];
  f32 sx = f_int(rm->x + REC_X(e->rec)), sy = f_int(rm->y + REC_Y(e->rec));
  const uint8_t *n = rec_node(e->rec, 0);
  f32 tx = f_int(rm->x + S16(n)), ty = f_int(rm->y + S16(n + 2));
  f32 x = back ? f_lerp(tx, sx, pct) : f_lerp(sx, tx, pct), y = back ? f_lerp(ty, sy, pct) : f_lerp(sy, ty, pct);
  plat_move_h(e, f_sub(x, f_add(f_int(e->x), e->remx)));
  plat_move_v(e, f_sub(y, f_add(f_int(e->y), e->remy)));
}
static void zip_update(Ent *e) {
  shake_update(e);
  e->liftx = e->lifty = 0;
  int st = e->sub & 0x7F;
  switch (st) {
    case 0:
      if (!(pl() && player_riding(e))) break;
      shake_start(e, TIM(.1f));
      e->t1 = TIM(.1f), st = 1;
      break;
    case 1:
      if ((e->t1 -= DTU) > 0) break;
      e->a0 = 3, e->t0 = 0, st = 2;
      break;
    case 2:   /* there in half a second, Ease.SineIn */
      zip_moveto(e, false, zip_out[e->t0]);
      if (++e->t0 >= ZIP_OUT_N) {
        shake_start(e, TIM(.2f));
        level_shake(TIM(.3f) / DTU);
        e->t1 = TIM(.5f), st = 3;
      }
      break;
    case 3:
      if ((e->t1 -= DTU) > 0) break;
      e->a0 = 2, e->t0 = 0, st = 4;
      break;
    case 4:   /* back in two seconds */
      zip_moveto(e, true, zip_back[e->t0]);
      if (++e->t0 >= ZIP_BACK_N) {
        shake_start(e, TIM(.2f));
        e->a0 = 1, e->t1 = TIM(.5f), st = 5;
      }
      break;
    case 5:
      if ((e->t1 -= DTU) > 0) break;
      st = 0;
      break;
  }
  e->sub = (uint8_t)((e->sub & 0x80) | st);
}
static void zip_render(Ent *e) {
  int x = e->x + e->a2, y = e->y + e->a3, w = e->hw, h = e->hh;
  draw_rect(x + 1, y + 1, w - 2, h - 2, BLACK);
  tiles_render(e, 0, w / 8, rec_nvals(e->rec), e->a2, e->a3);
}

/* ---------------------------------------------------------------- triggers */
static void trig_enter(Ent *e) {
  switch (e->type - 64) {
    case TT_cameraOffsetTrigger:
      g_level.cam_offx = (int16_t)rec_val(e->rec, 0), g_level.cam_offy = (int16_t)rec_val(e->rec, 1);
      break;
    case TT_changeRespawnTrigger: {
      const uint8_t *n = REC_NODES(e->rec) ? rec_node(e->rec, 0) : NULL;
      int tx = n ? g_level.rooms[e->room].x + S16(n) : e->x + e->hw / 2, ty = n ? g_level.rooms[e->room].y + S16(n + 2) : e->y + e->hh / 2;
      if (!solid_at(tx, ty - 4, tx + 1, ty - 3, NULL)) g_level.respawn_x = e->t0, g_level.respawn_y = e->t1;
      (void)tx;
      break;
    }
    case TT_eventTrigger:
      if (rec_val(e->rec, 0) == EV_end_city) {
        extern void game_chapter_end(void);
        game_chapter_end();
      }
      break;
  }
}
static void trig_stay(Ent *e) {
  if (e->type - 64 != TT_cameraTargetTrigger) return;
  Ent *p = g_player.e;
  int px = (e_left(p) + e_right(p)) / 2, py = (e_top(p) + e_bottom(p)) / 2;
  int l = e_left(e), r = e_right(e), t = e_top(e), b = e_bottom(e), cx = (l + r) / 2, cy = (t + b) / 2;
#define CMAP(v, a, bb) iclamp(((v) - (a)) * 256 / ((bb) - (a)), 0, 256)
  int k;
  switch (rec_val(e->rec, 1)) {
    case 1: k = CMAP(px, l, r); break;
    case 2: k = CMAP(px, r, l); break;
    case 3: k = CMAP(py, t, b); break;
    case 4: k = CMAP(py, b, t); break;
    case 5: k = imin(CMAP(px, l, cx), CMAP(px, r, cx)); break;
    case 6: k = imin(CMAP(py, t, cy), CMAP(py, b, cy)); break;
    default: k = 256;
  }
#undef CMAP
  const uint8_t *n = rec_node(e->rec, 0);
  extern int g_anchor_x, g_anchor_y, g_anchor_lerp;
  extern uint8_t g_anchor_ignore;
  g_anchor_x = g_level.rooms[e->room].x + S16(n) - 160, g_anchor_y = g_level.rooms[e->room].y + S16(n + 2) - 90;
  g_anchor_lerp = iclamp(rec_val(e->rec, 0) * k / 256, 0, 256);
  g_anchor_ignore = (uint8_t)(rec_flags(e->rec) & 3);   /* 1 x only (ignore y), 2 y only (ignore x) */
}
static void trig_leave(Ent *e) {
  if (e->type - 64 != TT_cameraTargetTrigger) return;
  for (int i = 0; i < g_nents; i++)
    if (&g_ents[i] != e && g_ents[i].type == 64 + TT_cameraTargetTrigger && (g_ents[i].sub & 1)) return;
  extern int g_anchor_lerp;
  g_anchor_lerp = 0;
}

/* ---------------------------------------------------------------- spawning a room */
static Ent *spawn(const uint8_t *r, int slot, bool trigger) {
  Room *rm = &g_level.rooms[slot];
  int type = r[0], x = rm->x + REC_X(r), y = rm->y + REC_Y(r), w = REC_W(r), h = REC_H(r);
  if (trigger) {
    Ent *e = ent_new(64 + type, x, y);
    if (!e) return NULL;
    e->rec = r, e->room = (uint8_t)slot;
    e->flags = EF_ACTIVE | EF_TRIGGER | EF_COLLIDABLE;
    box(e, w, h, 0, 0);
    if (type == TT_changeRespawnTrigger) {   /* the spawn point nearest its node (or middle) */
      const uint8_t *n = REC_NODES(r) ? rec_node(r, 0) : NULL;
      int tx = n ? rm->x + S16(n) : x + w / 2, ty = n ? rm->y + S16(n + 2) : y + h / 2;
      const uint8_t *p = rm->ents;
      int best = -1;
      int32_t bd = 0;
      p++;
      for (int i = 0; i < rm->ents[0]; i++, p += p[1]) {
        if (p[0] != ET_player) continue;
        int sx = rm->x + S16(p + 2), sy = rm->y + S16(p + 4);
        int32_t d = (int32_t)(sx - tx) * (sx - tx) + (int32_t)(sy - ty) * (sy - ty);
        if (best < 0 || d < bd) best = i, bd = d, e->t0 = sx, e->t1 = sy;
      }
    }
    return e;
  }
  Ent *e = ent_new(type, x, y);
  if (!e) return NULL;
  e->rec = r, e->room = (uint8_t)slot;
  int fl = rec_flags(r);
  switch (type) {
    case ET_player: ent_free(e); return NULL;
    case ET_spikesUp: box(e, w, 3, 0, -3), e->depth = -1, e->flags |= EF_PCOLLIDE; break;
    case ET_spikesDown: box(e, w, 3, 0, 0), e->depth = -1, e->flags |= EF_PCOLLIDE; break;
    case ET_spikesLeft: box(e, 3, h, -3, 0), e->depth = -1, e->flags |= EF_PCOLLIDE; break;
    case ET_spikesRight: box(e, 3, h, 0, 0), e->depth = -1, e->flags |= EF_PCOLLIDE; break;
    case ET_jumpThru: box(e, w, 5, 0, 0), e->depth = -60, e->flags |= EF_JUMPTHRU | EF_SAFE; break;
    case ET_strawberry:
    case ET_goldenBerry:
      box(e, 14, 14, -7, -7), e->depth = -100, e->flags |= EF_PCOLLIDE;
      if (type == ET_goldenBerry) e->sub |= BERRY_GOLD;
      else if (save_has_berry(g_level.chapter, rec_val(r, 2))) e->sub |= BERRY_GHOST;
      if (fl & 1) e->sub |= BERRY_WINGED;
      e->a0 = (int16_t)berry_anim(e, (fl & 1) ? 2 : 0);
      break;
    case ET_refill: box(e, 16, 16, -8, -8), e->depth = -100, e->flags |= EF_PCOLLIDE; break;
    case ET_spring: box(e, 16, 6, -8, -6), e->depth = -8501, e->flags |= EF_PCOLLIDE; break;
    case ET_wallSpringLeft: box(e, 6, 16, 0, -8), e->depth = -8501, e->flags |= EF_PCOLLIDE; break;
    case ET_wallSpringRight: box(e, 6, 16, -6, -8), e->depth = -8501, e->flags |= EF_PCOLLIDE; break;
    case ET_crumbleBlock: box(e, w, 8, 0, 0), e->flags |= EF_SOLID; break;
    case ET_fallingBlock: box(e, w, h, 0, 0), e->flags |= EF_SOLID, e->depth = (fl & 2) ? 5000 : 0; break;
    case ET_dashBlock: box(e, w, h, 0, 0), e->flags |= EF_SOLID, e->depth = -12999; break;
    case ET_fakeWall: box(e, w, h, 0, 0), e->flags &= ~EF_COLLIDABLE, e->depth = -13000, e->t0 = 256; break;
    case ET_coverupWall: box(e, w, h, 0, 0), e->flags &= ~EF_COLLIDABLE, e->depth = -13000, e->t0 = 256; break;
    case ET_zipMover: box(e, w, h, 0, 0), e->flags |= EF_SOLID, e->depth = -9999, e->a0 = 1; break;
    default:
      e->flags = EF_ACTIVE | EF_VISIBLE;   /* scenery not drawn yet */
      e->type = 100;
      break;
  }
  return e;
}
/* StaticMover: spikes and springs stuck to a solid move with it */
static void attach_movers(int slot) {
  for (int i = 0; i < g_nents; i++) {
    Ent *m = &g_ents[i];
    if (m->room != slot || !((m->type >= ET_spikesUp && m->type <= ET_spikesRight) || m->type == ET_spring ||
                             m->type == ET_wallSpringLeft || m->type == ET_wallSpringRight))
      continue;
    int dx = 0, dy = 0;
    switch (m->type) {
      case ET_spikesUp: case ET_spring: dy = 1; break;
      case ET_spikesDown: dy = -1; break;
      case ET_spikesLeft: case ET_wallSpringRight: dx = 1; break;
      case ET_spikesRight: case ET_wallSpringLeft: dx = -1; break;
    }
    for (int j = 0; j < g_nents; j++) {
      Ent *s = &g_ents[j];
      if (s->room != slot || !(s->flags & EF_SOLID) || s->type == 255) continue;
      if (ent_collide_at(m, m->x + dx, m->y + dy, s) && !ent_collide_at(m, m->x, m->y, s)) {
        m->ptr = s;
        if (m->type >= ET_spring) m->depth = (int16_t)(s->depth + 1);
        break;
      }
    }
  }
}
void ents_spawn_room(int slot) {
  Room *rm = &g_level.rooms[slot];
  const uint8_t *p = rm->ents;
  int n = *p++;
  for (int i = 0; i < n; i++, p += p[1]) spawn(p, slot, false);
  p = rm->trigs;
  n = *p++;
  for (int i = 0; i < n; i++, p += p[1]) spawn(p, slot, true);
  attach_movers(slot);
  (void)trig_ox, (void)trig_oy;
}
void ents_remove_room(int slot) {
  for (int i = 0; i < g_nents; i++) {
    Ent *e = &g_ents[i];
    if (e->type != 255 && e->room == slot && fol_index(e) < 0) ent_free(e);
  }
}
void ents_clear(void) {
  g_nents = 0;
  nfol = npast = 0;
}

/* ---------------------------------------------------------------- the frame */
void ents_update(void) {
  for (int i = 0; i < g_nents; i++) {
    Ent *e = &g_ents[i];
    if (e->type == 255 || !(e->flags & EF_ACTIVE)) continue;
    BENCH(11);
    switch (e->type) {
      case ET_player: player_update(); BENCH(12); break;
      case ET_strawberry: case ET_goldenBerry: berry_update(e); break;
      case ET_spring: case ET_wallSpringLeft: case ET_wallSpringRight: spring_update(e); break;
      case ET_refill: refill_update(e); break;
      case ET_crumbleBlock: crumble_update(e); break;
      case ET_fallingBlock: falling_update(e); break;
      case ET_fakeWall: case ET_coverupWall: fakewall_update(e); break;
      case ET_zipMover: zip_update(e); break;
      case 200: points_update(e); break;
    }
    BENCH(13);
  }
  leader_update();
  extern void player_after_update(void);
  player_after_update();
  BENCH(14);
}
/* PlayerCollider and triggers: what Madeline (her hurt box) touches */
void ents_player_touch(Ent *p) {
  for (int i = 0; i < g_nents && !g_player.dead; i++) {
    Ent *e = &g_ents[i];
    if (e->type == 255 || !(e->flags & EF_COLLIDABLE)) continue;
    bool hit = ent_collide_at(p, p->x, p->y, e);
    if (e->flags & EF_TRIGGER) {
      if (hit) {
        if (!(e->sub & 1)) e->sub |= 1, trig_enter(e);
        trig_stay(e);
      } else if (e->sub & 1)
        e->sub &= ~1, trig_leave(e);
      continue;
    }
    if (!hit || !(e->flags & EF_PCOLLIDE)) continue;
    switch (e->type) {
      case ET_spikesUp: case ET_spikesDown: case ET_spikesLeft: case ET_spikesRight: spikes_touch(e); break;
      case ET_strawberry: case ET_goldenBerry: berry_touch(e); break;
      case ET_spring: case ET_wallSpringLeft: case ET_wallSpringRight: spring_touch(e); break;
      case ET_refill: refill_touch(e); break;
    }
  }
}
static void ent_draw(Ent *e) {
  switch (e->type) {
    case ET_player: player_render(); break;
    case ET_spikesUp: case ET_spikesDown: case ET_spikesLeft: case ET_spikesRight: spikes_render(e); break;
    case ET_jumpThru: jumpthru_render(e); break;
    case ET_strawberry: case ET_goldenBerry: berry_render(e); break;
    case ET_spring: case ET_wallSpringLeft: case ET_wallSpringRight: spring_render(e); break;
    case ET_refill: refill_render(e); break;
    case ET_crumbleBlock: crumble_render(e); break;
    case ET_fallingBlock: case ET_dashBlock: block_render(e); break;
    case ET_fakeWall: case ET_coverupWall: fakewall_render(e); break;
    case ET_zipMover: zip_render(e); break;
    case 200: {
      const uint8_t *s = sprite(anim_sprite(e));
      draw_sprite(s, e->x - s[0] / 2, e->y - s[1] / 2, 0);
      break;
    }
  }
}

int g_cull_buf = -1;
/* back to front: higher depths first, then the order they came in */
static uint8_t order[MAX_ENTS], norder;
/* roughly on the view: what each draws stays within 48 px of its box (Madeline's hair and death go further: she is
 * always in) */
static bool ent_near(const Ent *e) {
  if (e->type == ET_player) return true;
  int l = e->x + e->hx - g_camx, t = e->y + e->hy - g_camy;
  return l < VIEW_W + 48 && t < VIEW_H + 48 && l + e->hw > -48 && t + e->hh > -48;
}
void ents_order(void) {
  norder = 0;
  for (int i = 0; i < g_nents; i++) {
    Ent *e = &g_ents[i];
    if (e->type == 255 || !(e->flags & EF_VISIBLE) || !ent_near(e)) continue;
    int k = norder++, d = e->depth;
    while (k > 0 && g_ents[order[k - 1]].depth < d) order[k] = order[k - 1], k--;
    order[k] = (uint8_t)i;
  }
}
int g_rec_buf = -1;
static void bb_start(void) { g_bb[0] = g_bb[1] = 0x7FFF, g_bb[2] = g_bb[3] = -0x7FFF, g_bb_sum = 0; }
static void bb_keep(Ent *e, int buf) {
  for (int k = 0; k < 4; k++) e->bb[buf][k] = (int16_t)iclamp(g_bb[k], -0x7FFF, 0x7FFF);
  e->sum[buf] = g_bb_sum;
}
void ents_render(int dmax, int dmin) {
  int cb = g_cull_buf, rb = g_rec_buf;
  for (int k = 0; k < norder; k++) {
    Ent *e = &g_ents[order[k]];
    if (e->depth > dmax || e->depth <= dmin) continue;
    if (cb >= 0 && (e->bb[cb][0] >= g_clip_x1 || e->bb[cb][2] <= g_clip_x0 || e->bb[cb][1] >= g_clip_y1 ||
                    e->bb[cb][3] <= g_clip_y0))
      continue;
    if (rb < 0) {
      ent_draw(e);
      continue;
    }
    bb_start(), g_rec = true;   /* (drawn and measured) */
    ent_draw(e);
    g_rec = false, bb_keep(e, rb);
  }
}

/* what each one would draw (its box and sum), drawing nothing */
static bool ent_static(const Ent *e) {   /* what never changes how it looks (until it goes) */
  switch (e->type) {
    case ET_spikesUp: case ET_spikesDown: case ET_spikesLeft: case ET_spikesRight: return !e->ptr;
    case ET_jumpThru: case ET_dashBlock: return true;
  }
  return false;
}
void ents_measure(int buf, void (*changed)(const int16_t *was, const int *now)) {
  g_dry = g_rec = true;
  for (int i = 0; i < g_nents; i++) {
    Ent *e = &g_ents[i];
    if (e->type == 255 || ent_static(e)) continue;
    bb_start();
    if ((e->flags & EF_VISIBLE) && ent_near(e)) ent_draw(e);
    for (int k = 0; k < 4; k++) g_bb[k] = iclamp(g_bb[k], -0x7FFF, 0x7FFF);
    int16_t *bb = e->bb[buf];
    if (e->sum[buf] != g_bb_sum || bb[0] != g_bb[0] || bb[1] != g_bb[1] || bb[2] != g_bb[2] || bb[3] != g_bb[3]) {
      changed(bb, g_bb);
      bb_keep(e, buf);
    }
  }
  g_dry = g_rec = false;
}
void ents_forget(int buf) {
  for (int i = 0; i < g_nents; i++) g_ents[i].bb[buf][0] = 1, g_ents[i].bb[buf][2] = 0;
}
