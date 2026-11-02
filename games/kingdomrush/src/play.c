/* The battle screen: the battlefield at 0.4 of the game's size on the right, the counts and spells on the left.
 *
 * Keys: the arrows hop between build spots, the wave flags and the enemies; OK opens the tower menu around a
 * spot (its items hop with the arrows, OK buys); 1 and 2 aim the rain of fire and the reinforcements (a cross
 * the arrows move, OK drops it); backspace calls the next wave early; Back pauses. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "game.h"
#include "gfx.h"
#include "kr.h"
#include "settings.h"
#include "special.h"
#include "ui.h"
#include "anim.h"
#include "strings.h"

#define MAP_X 40
#define SX(x) (MAP_X + (int)lroundf((x) * 0.4f))
#define SY(y) ((int)lroundf((y) * 0.4f))

static uint8_t bg_mem[288 * 240];

enum { M_SELECT, M_RING, M_AIM, M_PAUSE, M_NOTE, M_RESULT, M_QUIT };
enum { AIM_FIRE, AIM_REINF, AIM_RALLY };
enum { SEL_SPOT, SEL_FLAG, SEL_ENEMY };

static struct {
  uint8_t mode, aim, sel_kind, ring_sel, nring, pause_sel, result_sel;
  int16_t sel;              /* the spot, flag or enemy */
  float cx, cy;             /* the cursor (the game's coordinates) */
  RingItem ring[6];
  int16_t aim_spot;
  int16_t msg_t;
  char msg[48];
  uint8_t note;
  int16_t result_t;
  uint8_t level, bmode;
  uint8_t saved;
} P;

/* ------------------------------------------------------------------ start */
void play_start(int level, int mode) {
  memset(&P, 0, sizeof P);
  P.level = (uint8_t)level, P.bmode = (uint8_t)mode;
  memcpy(upgrades, g.p.ups, sizeof upgrades);
  enemy_health_pct = g.p.difficulty == 0 ? 80 : g.p.difficulty == 2 ? 120 : 100;
  battle_seed = g.ticks * 2654435761u;
  battle_start(level, mode);
  bg_load(PIC_LEVEL1 + level, bg_mem);
  bg_dim(false);
  P.mode = M_SELECT, P.sel_kind = SEL_SPOT, P.sel = 0;
  /* start at the spot nearest the middle */
  float best = 1e9f;
  for (int s = 0; s < B.nspots; s++) {
    float dx = B.spot[s].x - 350, dy = B.spot[s].y - 300, d = dx * dx + dy * dy;
    if (B.spot[s].kind != 0xFF && d < best) best = d, P.sel = (int16_t)s;
  }
  g.screen = SCR_BATTLE;
}

/* ------------------------------------------------------------------ the selection */
static Pt sel_pos(void) {
  if (P.sel_kind == SEL_SPOT) return (Pt){B.spot[P.sel].x, B.spot[P.sel].y - 20};
  if (P.sel_kind == SEL_FLAG) return B.flag[P.sel];
  Enemy *e = EN(P.sel);
  if (e) return (Pt){e->x, e->y + enemy_stats[e->type].y_adj};
  return (Pt){350, 300};
}

static void fix_selection(void) {
  if (P.sel_kind == SEL_ENEMY && (!EN(P.sel) || B.en[P.sel].dead || !B.en[P.sel].active)) {
    /* the enemy is gone: the nearest spot */
    Pt at = {P.cx, P.cy};
    float best = 1e9f;
    P.sel_kind = SEL_SPOT;
    for (int s = 0; s < B.nspots; s++) {
      float dx = B.spot[s].x - at.x, dy = B.spot[s].y - 20 - at.y, d = dx * dx + dy * dy;
      if (B.spot[s].kind != 0xFF && d < best) best = d, P.sel = (int16_t)s;
    }
  }
  if (P.sel_kind == SEL_FLAG && (!B.flags_up || !B.flag_path[P.sel])) P.sel_kind = SEL_SPOT, P.sel = 0;
  Pt p = sel_pos();
  P.cx = p.x, P.cy = p.y;
}

/* the candidate nearest in a direction: along it counts once, across it twice */
static void hop(int dx, int dy) {
  Pt at = sel_pos();
  float best = 1e9f;
  int bk = -1, bi = -1;
  for (int kind = 0; kind < 3; kind++) {
    int n = kind == SEL_SPOT ? B.nspots : kind == SEL_FLAG ? B.nflags : MAX_ENEMIES;
    for (int i = 0; i < n; i++) {
      Pt p;
      if (kind == SEL_SPOT) {
        if (B.spot[i].kind == 0xFF) continue;
        p = (Pt){B.spot[i].x, B.spot[i].y - 20};
      } else if (kind == SEL_FLAG) {
        if (!B.flags_up || i >= MAX_PATHS || !B.flag_path[i]) continue;
        p = B.flag[i];
      } else {
        Enemy *e = &B.en[i];
        if (!e->alive || e->dead || !e->active) continue;
        p = (Pt){e->x, e->y + enemy_stats[e->type].y_adj};
      }
      if (kind == P.sel_kind && i == P.sel) continue;
      float vx = p.x - at.x, vy = p.y - at.y;
      float along = vx * dx + vy * dy, across = fabsf(vx * dy - vy * dx);
      if (along <= 4) continue;
      float score = along + 2 * across;
      if (kind == SEL_ENEMY) score += 25;   /* (the spots come first when close) */
      if (score < best) best = score, bk = kind, bi = i;
    }
  }
  if (bk >= 0) P.sel_kind = (uint8_t)bk, P.sel = (int16_t)bi;
}

/* ------------------------------------------------------------------ messages */
static void say(const char *s) {
  snprintf(P.msg, sizeof P.msg, "%s", s);
  P.msg_t = 75;
}

/* ------------------------------------------------------------------ the ring */
static void open_ring(void) {
  P.nring = (uint8_t)ring_items(P.sel, P.ring);
  if (!P.nring) return;
  P.mode = M_RING;
  /* the first one you can afford, or the first */
  P.ring_sel = 0;
  for (int i = 0; i < P.nring; i++)
    if (P.ring[i].cost >= 0 && P.ring[i].cost <= B.cash && !P.ring[i].locked) {
      P.ring_sel = (uint8_t)i;
      break;
    }
}

static void ring_hop(int dx, int dy) {
  RingItem *c = &P.ring[P.ring_sel];
  float best = 1e9f;
  int bi = -1;
  for (int i = 0; i < P.nring; i++) {
    if (i == P.ring_sel) continue;
    float vx = (float)(P.ring[i].x - c->x), vy = (float)(P.ring[i].y - c->y);
    float along = vx * dx + vy * dy, across = fabsf(vx * dy - vy * dx);
    if (along <= 0) continue;
    float score = along + 2 * across;
    if (score < best) best = score, bi = i;
  }
  if (bi >= 0) P.ring_sel = (uint8_t)bi;
}

static void ring_do(void) {
  RingItem *it = &P.ring[P.ring_sel];
  int s = P.sel;
  if (it->locked) {
    say("Not yet: it comes in a later stage");
    return;
  }
  if (it->action == RA_RALLY) {
    P.mode = M_AIM, P.aim = AIM_RALLY, P.aim_spot = (int16_t)s;
    Spot *t = &B.spot[s];
    if (t->kind == TW_SORCERER) {
      Soldier *el = SO(t->elemental);
      P.cx = el ? el->rx : t->rally.x, P.cy = el ? el->ry : t->rally.y;
    } else
      P.cx = t->rally.x, P.cy = t->rally.y;
    return;
  }
  if (it->action == RA_SELL) {
    spot_sell(s);
    P.mode = M_SELECT;
    return;
  }
  if (it->cost > B.cash) {
    say("Not enough gold");
    return;
  }
  if (it->action >= RA_ARCHER && it->action <= RA_ENGINEER) spot_build(s, (int[]){TW_ARCHER, TW_BARRACKS, TW_MAGE, TW_ENGINEER}[it->action - RA_ARCHER]);
  else if (it->action == RA_UPGRADE) spot_upgrade(s, 0);
  else if (it->action == RA_SPECIAL1) spot_upgrade(s, 1);
  else if (it->action == RA_SPECIAL2) spot_upgrade(s, 2);
  else if (it->action >= RA_ABILITY0) spot_buy_ability(s, it->action - RA_ABILITY0);
  /* abilities keep the menu open, as the game's (closeOnExcec false) */
  if (it->action >= RA_ABILITY0) {
    P.nring = (uint8_t)ring_items(s, P.ring);
    return;
  }
  P.mode = M_SELECT;
}

/* ------------------------------------------------------------------ the end */
static void finish(bool won) {
  P.mode = M_RESULT, P.result_t = 0, P.result_sel = 0;
  if (won && !P.saved) {
    int l = P.level;
    if (P.bmode == MODE_CAMPAIGN) {
      int st = battle_stars();
      if (st > g.p.camp[l]) g.p.camp[l] = (uint8_t)st;
    } else if (P.bmode == MODE_HEROIC)
      g.p.heroic[l] = 1;
    else
      g.p.iron[l] = 1;
    save_progress();
    P.saved = 1;
  }
}

/* ------------------------------------------------------------------ a step */
static void aim_move(int dx, int dy) {
  P.cx += dx * 15.0f, P.cy += dy * 15.0f;
  if (P.cx < 0) P.cx = 0;
  if (P.cx > 699) P.cx = 699;
  if (P.cy < 0) P.cy = 0;
  if (P.cy > 599) P.cy = 599;
}

static void start_aim(int which) {
  if (which == AIM_FIRE ? power_fire_cooldown() != 0 : power_reinf_cooldown() != 0) {
    int cd = which == AIM_FIRE ? power_fire_cooldown() : power_reinf_cooldown();
    say(cd < 0 ? "Not yet: it comes with the first wave" : "Still recharging");
    return;
  }
  P.mode = M_AIM, P.aim = (uint8_t)which;
  Pt p = sel_pos();
  /* aim at the enemy furthest along, or where the selection is */
  int best = -1;
  for (int i = 0; i < MAX_ENEMIES; i++) {
    Enemy *e = &B.en[i];
    if (!e->alive || !e->active || e->dead) continue;
    if (best < 0 || path_len(e->path, e->lane) - e->node < path_len(B.en[best].path, B.en[best].lane) - B.en[best].node) best = i;
  }
  if (best >= 0) p = (Pt){B.en[best].x, B.en[best].y};
  P.cx = p.x, P.cy = p.y;
}

static int dir_key(int *dx, int *dy) {
  *dx = *dy = 0;
  if (play_key(K_LEFT)) *dx = -1;
  else if (play_key(K_RIGHT)) *dx = 1;
  else if (play_key(K_UP)) *dy = -1;
  else if (play_key(K_DOWN)) *dy = 1;
  return *dx || *dy;
}

void play_step(void) {
  uint32_t pr = g.pressed;
  bool ok = pr & (K_OK | K_EXE);
  int dx, dy;
  if (P.msg_t) P.msg_t--;
  switch (P.mode) {
  case M_SELECT:
  case M_RING:
  case M_AIM:
    if (pr & K_BACK) {
      if (P.mode == M_SELECT) P.mode = M_PAUSE, P.pause_sel = 0;
      else P.mode = M_SELECT;
      return;
    }
    if (pr & K_DIGIT(1)) start_aim(AIM_FIRE);
    else if (pr & K_DIGIT(2)) start_aim(AIM_REINF);
    else if (pr & (K_DEL | K_DIGIT(3))) {
      if (battle_call_wave()) P.mode = P.mode == M_RING ? M_SELECT : P.mode;
      else if (B.state == LS_NORMAL) say("The next wave isn't ready");
    }
    if (P.mode == M_SELECT) {
      if (dir_key(&dx, &dy)) hop(dx, dy);
      fix_selection();
      if (ok) {
        if (P.sel_kind == SEL_SPOT) {
          Spot *t = &B.spot[P.sel];
          if (t->ice == 2) {   /* J.T.'s ice: three hits */
            if (++t->ice_hits >= 3) t->ice = 3, t->ice_t = 0;
          } else if (!t->disabled && !t->building)
            open_ring();
        } else if (P.sel_kind == SEL_FLAG)
          battle_call_wave();
      }
    } else if (P.mode == M_RING) {
      if (dir_key(&dx, &dy)) ring_hop(dx, dy);
      if (ok) ring_do();
      if (B.spot[P.sel].kind == 0xFF || B.spot[P.sel].disabled) P.mode = M_SELECT;
    } else if (P.mode == M_AIM) {
      if (dir_key(&dx, &dy)) aim_move(dx, dy);
      if (ok) {
        Pt at = {P.cx, P.cy};
        if (P.aim == AIM_FIRE) {
          power_fire(at);
          P.mode = M_SELECT;
        } else if (P.aim == AIM_REINF) {
          if (power_reinforce(at)) P.mode = M_SELECT;
          else say("Put them on a road");
        } else {
          if (spot_rally_ok(P.aim_spot, at)) {
            spot_rally(P.aim_spot, at);
            P.mode = M_SELECT;
          } else
            say("On a road, near the tower");
        }
      }
    }
    break;
  case M_PAUSE:
    if (pr & K_BACK) P.mode = M_SELECT;
    if (play_key(K_UP)) P.pause_sel = (uint8_t)((P.pause_sel + 3) % 4);
    if (play_key(K_DOWN)) P.pause_sel = (uint8_t)((P.pause_sel + 1) % 4);
    if (ok) {
      if (P.pause_sel == 0) P.mode = M_SELECT;
      else if (P.pause_sel == 1) play_start(P.level, P.bmode);
      else if (P.pause_sel == 2) g.screen = SCR_HELP;
      else P.mode = M_QUIT, P.pause_sel = 1;
    }
    return;
  case M_QUIT:
    if (play_key(K_LEFT) || play_key(K_RIGHT)) P.pause_sel ^= 1;
    if (pr & K_BACK) P.mode = M_PAUSE, P.pause_sel = 3;
    if (ok) {
      if (P.pause_sel == 0) g.screen = SCR_MAP;
      else P.mode = M_PAUSE, P.pause_sel = 3;
    }
    return;
  case M_NOTE:
    if (ok || (pr & K_BACK)) P.mode = M_SELECT;
    return;
  case M_RESULT:
    P.result_t++;
    if (play_key(K_LEFT) || play_key(K_RIGHT)) P.result_sel ^= 1;
    if (ok && P.result_t > 20) {
      if (P.result_sel == 0 && B.state == LS_WIN) g.screen = SCR_MAP;
      else if (P.result_sel == 0) play_start(P.level, P.bmode);
      else if (B.state == LS_WIN) play_start(P.level, P.bmode);
      else g.screen = SCR_MAP;
    }
    return;
  }
  if (P.mode == M_PAUSE || P.mode == M_QUIT) return;
  battle_step();
  if (B.pending_note) {
    int n = B.pending_note;
    B.pending_note = 0;
    if (!(g.p.seen_note[n >> 3] & (1 << (n & 7))) && note_text(n)) {
      g.p.seen_note[n >> 3] = (uint8_t)(g.p.seen_note[n >> 3] | 1 << (n & 7));
      P.note = (uint8_t)n, P.mode = M_NOTE;
    }
  }
  if (B.state == LS_WIN) finish(true);
  else if (B.state == LS_OVER) finish(false);
}

/* ------------------------------------------------------------------ drawing */
static uint8_t terrain(void) { return P.level >= 9 ? 2 : P.level >= 6 ? 1 : 0; }

typedef struct {
  int16_t y;
  uint8_t kind;
  uint8_t i;
} Item;
enum { IT_SPOT, IT_ENEMY, IT_SOLDIER, IT_FX };
static Item items[MAX_SPOTS + MAX_ENEMIES + MAX_SOLDIERS + MAX_FX];
static int nitems;

static void collect(void) {
  nitems = 0;
  for (int s = 0; s < B.nspots; s++)
    if (B.spot[s].kind != 0xFF) items[nitems++] = (Item){B.spot[s].y, IT_SPOT, (uint8_t)s};
  for (int i = 0; i < MAX_ENEMIES; i++)
    if (B.en[i].alive) items[nitems++] = (Item){(int16_t)B.en[i].y, IT_ENEMY, (uint8_t)i};
  for (int i = 0; i < MAX_SOLDIERS; i++)
    if (B.so[i].alive && !B.so[i].hidden) items[nitems++] = (Item){(int16_t)B.so[i].y, IT_SOLDIER, (uint8_t)i};
  for (int i = 0; i < MAX_FX; i++)
    if (B.fx[i].alive && !B.fx[i].decal && B.fx[i].anim != 0xFFFF) items[nitems++] = (Item){(int16_t)B.fx[i].y, IT_FX, (uint8_t)i};
  for (int i = 1; i < nitems; i++) {   /* by y, as the game sorts its entities */
    Item k = items[i];
    int j = i - 1;
    while (j >= 0 && items[j].y > k.y) items[j + 1] = items[j], j--;
    items[j + 1] = k;
  }
}

static void draw_bar(int x, int y, int w, int v, int max) {
  if (max <= 0) return;
  int f = v * w / max;
  if (f < 0) f = 0;
  if (v > 0 && f == 0) f = 1;
  gfx_fill(x - 1, y - 1, w + 2, 4, RGB(0x1A0F08));
  gfx_fill(x, y, w, 2, RGB(0xC62020));
  gfx_fill(x, y, f, 2, RGB(0x46D23C));
}

static void draw_enemy(Enemy *e) {
  int x = SX(e->x), y = SY(e->y);
  if (e->teleporting) {
    spr_anim(AN_FX_TELEPORT, e->teleport_t, false, x, y, false);
    return;
  }
  spr_anim(e->anim, e->anim_t, !e->dead, x, y, e->left);
  if (e->sp_state == 9) gfx_ellipse(x, y - 5, 6, 7, RGB(0xF0A030), 20);   /* a demon lord's shield */
  if (e->thorned) gfx_ellipse(x, y, 5, 2, RGB(0x6EB43C), 32);
  if (e->dead || !e->active) return;
  const EnemyStat *s = &enemy_stats[e->type];
  if (e->health < e->init_health) {
    int w = s->bar_w == 2 ? 24 : s->bar_w == 1 ? 14 : 9;
    draw_bar(x - w / 2, SY(e->y + s->bar_y), w, e->health, e->init_health);
  }
}

static void draw_soldier(Soldier *so) {
  int x = SX(so->x), y = SY(so->y);
  spr_anim(so->anim, so->anim_t, so->walking || so->idle, x, y, so->left);
  if (so->active && so->health < so->init_health) draw_bar(x - 4, y - 9, 9, so->health, so->init_health);
}

static void draw_spot(int s) {
  Spot *t = &B.spot[s];
  int x = SX(t->x), y = SY(t->y);
  if (t->kind == TW_NONE) {
    spr_anim(AN_T_HOLDER_FOREST + terrain(), 0, false, x, SY(t->y - 36), false);
    return;
  }
  draw_tower(t, x, y);
  if (t->building) draw_bar(x - 7, SY(t->y - 45), 14, 30 - t->building, 30);
  if (t->ice) {
    static const int16_t an[7] = {0, AN_B_ICEBLOCK_START, AN_B_ICEBLOCK_IDLE, AN_B_ICEBLOCK_END, AN_B_VEZNANBLOCK_START,
                                  AN_B_VEZNANBLOCK_IDLE, AN_B_VEZNANBLOCK_RELEASE};
    spr_anim(an[t->ice], t->ice == 2 || t->ice == 5 ? (int)(B.frame & 255) : t->ice_t, t->ice == 2 || t->ice == 5, x, y, false);
  }
}

static void draw_bullets(void) {
  for (int i = 0; i < MAX_BULLETS; i++) {
    Bullet *b = &B.bu[i];
    if (!b->alive) continue;
    draw_bullet(b, SX(b->x), SY(b->y));
  }
}

static void draw_fx(Fx *f) {
  int x = SX(f->x), y = SY(f->y);
  if (f->anim == 0xFFFF) {   /* gold: "+15" rising */
    char s[12];
    snprintf(s, sizeof s, "+%d", f->value);
    gfx_text_oc(FONT_SMALL, x, y - f->t / 3 - 8, s, RGB(0xFFD32A));
    return;
  }
  spr_anim(f->anim, f->t, false, x, y, f->flip);
}

static void draw_range(Pt c, int w, uint16_t col) {
  gfx_ellipse(SX(c.x), SY(c.y), (int)lroundf(w * 0.2f), (int)lroundf(w * RANGE_RATIO * 0.2f), col, 24);
}

static void draw_field(void) {
  bg_draw(MAP_X, 0);
  /* the decals: tower bases, blood, craters, arrows in the ground, eggs */
  for (int s = 0; s < B.nspots; s++) draw_base(&B.spot[s], SX(B.spot[s].x), SY(B.spot[s].y), terrain());
  for (int i = 0; i < MAX_FX; i++)
    if (B.fx[i].alive && B.fx[i].decal) draw_fx(&B.fx[i]);
  for (int i = 0; i < (int)(sizeof B.egg / sizeof B.egg[0]); i++)
    if (B.egg[i].on)
      spr_anim(B.egg[i].on == 2 ? AN_B_JUGGERNAUTEGG_START : AN_B_SPIDEREGG, B.egg[i].t, false, SX(B.egg[i].x), SY(B.egg[i].y), false);
  /* the selected spot's range */
  if ((P.mode == M_SELECT || P.mode == M_RING) && P.sel_kind == SEL_SPOT) {
    Spot *t = &B.spot[P.sel];
    if (t->kind != TW_NONE && t->kind != 0xFF && !t->building)
      draw_range((Pt){t->x, t->y - 10}, tower_range(t->kind, t->level), RGB(0xFFFFFF));
  }
  if (P.mode == M_RING && P.ring[P.ring_sel].range)
    draw_range((Pt){B.spot[P.sel].x, B.spot[P.sel].y - 10}, P.ring[P.ring_sel].range, RGB(0x9CF080));
  for (int k = 0; k < nitems; k++) {
    Item *it = &items[k];
    if (SY(it->y) < gfx_y0 - 60 || SY(it->y) > gfx_y0 + BAND_H + 60) continue;
    switch (it->kind) {
    case IT_SPOT: draw_spot(it->i); break;
    case IT_ENEMY: draw_enemy(&B.en[it->i]); break;
    case IT_SOLDIER: draw_soldier(&B.so[it->i]); break;
    default: draw_fx(&B.fx[it->i]);
    }
  }
  draw_bullets();
  for (int i = 0; i < MAX_FX; i++)
    if (B.fx[i].alive && B.fx[i].anim == 0xFFFF) draw_fx(&B.fx[i]);
  /* the wave flags at the roads' starts */
  if (B.flags_up && B.state == LS_NORMAL)
    for (int p = 0; p < B.nflags && p < MAX_PATHS; p++) {
      if (!B.flag_path[p]) continue;
      int x = SX(B.flag[p].x), y = SY(B.flag[p].y);
      draw_wave_flag(x, y, B.flag_path[p] == 2, B.frame, P.sel_kind == SEL_FLAG && P.sel == p && P.mode == M_SELECT);
    }
}

static void draw_selection(void) {
  if (P.mode == M_SELECT) {
    Pt p = sel_pos();
    int x = SX(p.x);
    if (P.sel_kind == SEL_SPOT) {
      Spot *t = &B.spot[P.sel];
      int ty = SY(t->y - (t->kind == TW_NONE ? 36 : 18));
      int w = t->kind == TW_NONE ? 14 : 15, h = t->kind == TW_NONE ? 7 : 14;
      gfx_ellipse(SX(t->x), ty, w, h, RGB(0xFFE14D), 32);
      gfx_ellipse(SX(t->x), ty, w + 1, h + 1, RGB(0x6A4A00), 32);
    } else if (P.sel_kind == SEL_ENEMY) {
      Enemy *e = EN(P.sel);
      if (e) {
        gfx_ellipse(x, SY(e->y), 7, 3, RGB(0xFF4A3A), 32);
        gfx_ellipse(x, SY(e->y), 8, 4, RGB(0x500000), 32);
      }
    }
  } else if (P.mode == M_RING)
    draw_ring(P.ring, P.nring, P.ring_sel, SX(B.spot[P.sel].x), SY(B.spot[P.sel].y - 25), B.cash);
  else if (P.mode == M_AIM) {
    int x = SX(P.cx), y = SY(P.cy);
    Pt at = {P.cx, P.cy};
    bool good = P.aim == AIM_FIRE || (P.aim == AIM_REINF ? power_reinforce_ok(at) : spot_rally_ok(P.aim_spot, at));
    if (P.aim == AIM_FIRE) {
      int lo, hi, r = S_powers_fireball_range;
      (void)lo, (void)hi;
      if (up_on(UP_RAIN, 3)) r += r / 4;
      draw_range(at, r, RGB(0xFF7A2A));
    }
    if (P.aim == AIM_RALLY) {
      Spot *t = &B.spot[P.aim_spot];
      int r = t->kind == TW_SORCERER ? S_mages_sorcerer_elementalRallyRange : barrack_stat(TW_BARRACKS, 1).rally;
      draw_range((Pt){t->x, t->y - 10}, r, RGB(0xFFFFFF));
    }
    uint16_t c = good ? RGB(0xFFE14D) : RGB(0xFF3030);
    gfx_fill(x - 6, y, 13, 1, c);
    gfx_fill(x, y - 6, 1, 13, c);
    gfx_ellipse(x, y, 4, 4, c, 32);
  }
}

static void draw_side(void) {
  draw_sidebar(P.mode == M_AIM ? (P.aim == AIM_FIRE ? 1 : P.aim == AIM_REINF ? 2 : 0) : 0);
  /* what is selected: an enemy's life and armour, a tower's numbers */
  if (P.mode == M_SELECT && P.sel_kind == SEL_ENEMY && EN(P.sel)) draw_enemy_info(EN(P.sel));
  else if (P.mode == M_SELECT && P.sel_kind == SEL_SPOT) draw_spot_info(&B.spot[P.sel]);
}

static void draw_keys(void) {
  const char *k;
  switch (P.mode) {
  case M_RING: k = P.ring[P.ring_sel].name; break;
  case M_AIM: k = P.aim == AIM_RALLY ? "Arrows move  OK set rally point  Back cancel" : "Arrows aim  OK cast  Back cancel"; break;
  default:
    if (P.sel_kind == SEL_FLAG) k = "OK call the wave now (gold for it)";
    else if (P.sel_kind == SEL_SPOT && B.spot[P.sel].ice == 2) k = "OK break the ice (3 hits)";
    else if (P.sel_kind == SEL_SPOT && B.spot[P.sel].kind == TW_NONE) k = "OK build   1 fire   2 soldiers   Back pause";
    else k = "OK tower menu   1 fire   2 soldiers   Back pause";
  }
  draw_key_line(MAP_X, k, P.mode == M_RING ? &P.ring[P.ring_sel] : 0, B.cash);
  if (P.msg_t) {
    int w = gfx_text_w(FONT_SMALL, P.msg) + 10;
    int x = MAP_X + 140 - w / 2;
    gfx_panel(x, 98, w, 15, RGB(0x2A1A0C), RGB(0xE0B040));
    gfx_text_c(FONT_SMALL, MAP_X + 140, 100, P.msg, WHITE);
  }
}

static void draw_band(void) {
  draw_field();
  draw_selection();
  draw_side();
  if (P.mode == M_SELECT || P.mode == M_RING || P.mode == M_AIM) draw_keys();
  if (B.state == LS_STARTING) gfx_blend(MAP_X, 0, 280, 240, BLACK, 16 - B.start_t / 2);
  switch (P.mode) {
  case M_PAUSE: draw_pause(P.pause_sel); break;
  case M_QUIT: draw_quit(P.pause_sel); break;
  case M_NOTE: draw_note(P.note); break;
  case M_RESULT: draw_result(B.state == LS_WIN, battle_stars(), P.bmode, P.result_sel, P.result_t); break;
  }
}

void play_draw(void) {
  collect();
  bg_dim(P.mode == M_PAUSE || P.mode == M_QUIT || P.mode == M_NOTE || (P.mode == M_RESULT && P.result_t > 10));
  gfx_frame(draw_band);
}
