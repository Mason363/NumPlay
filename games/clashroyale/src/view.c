/* The battle on screen: the arena, the units in depth order, the towers' hitpoints, the cursor, and the hand. */
#include <stdio.h>
#include <string.h>
#include "../../common/np_lz.h"
#include "game.h"
#include "gfx.h"
#include "res.h"

#define FIELD_H 180
#define HUD_Y 180
#define BLUE RGB(0x3C8CF0)
#define RED RGB(0xE8403C)
#define PINK RGB(0xD83CE0)
#define GOLD RGB(0xFFD23C)

static uint8_t arena_idx[SCREEN_W * FIELD_H];
static uint8_t arena_pal[512];
static int arena_loaded = -1;

void view_load_arena(int a) {
  if (a == arena_loaded) return;
  const uint8_t *s = section(SEC_ARENAS);
  if (a < 0 || a >= (int)rd32(s)) a = 0;
  const uint8_t *p = s + rd32(s + 4 + 4 * a);
  memcpy(arena_pal, p, 512);
  uint32_t n = rd32(p + 512);
  if (np_lz_unpack(p + 516, n, arena_idx, sizeof arena_idx) != (int32_t)sizeof arena_idx) memset(arena_idx, 0, sizeof arena_idx);
  arena_loaded = a;
}

/* ------------------------------------------------------------------ units */
static const uint8_t DSLOT[8] = {2, 3, 4, 3, 2, 1, 0, 1};   /* the face's picture: the game's 1, 3, 5, 7, 9 */
static const uint8_t DMIR[8] = {0, 0, 0, 1, 1, 1, 0, 0};

static int height_px(int32_t h) { return h * 6 / 1000; }   /* (heights drawn a little lower than the game's) */

static int anim_frame(const Ent *e, const AnimDef *a, const UnitDef *d) {
  if (a->nf <= 1) return 0;
  if (e->anim == 0) {
    int f = (int)((e->anim_ms * 30 / 1000) % (a->len ? a->len : 1));
    return f * a->nf / (a->len ? a->len : 1);
  }
  if (e->anim == 1) {
    int fms = a->len * 1000 / 30 / a->nf;
    int act = a->act == 255 ? a->nf - 1 : a->act;
    if (fms && e->post / fms + act < a->nf && e->post < d->hit_ms) return act + e->post / fms;
    return d->hit_ms ? act * e->t / d->hit_ms : 0;
  }
  return 0;
}

static int unit_img(const Ent *e, bool *mirror) {
  const UnitDef *d = &UNITS[e->type];
  const SetDef *s = &SETS[d->set];
  const AnimDef *a = e->anim == 0 ? &s->run : e->anim == 1 ? &s->attack : &s->idle;
  if (d->flags & U_HIDES) {   /* Tesla: up, half, down */
    a = &s->idle;
    *mirror = false;
    return a->img + (e->hidden ? 2 : e->hide_t > 0 ? 1 : 0);
  }
  int dir = 0;
  *mirror = false;
  if (a->ndir >= 5) dir = DSLOT[e->face], *mirror = DMIR[e->face];
  else if (a->ndir == 1 && !(d->flags & U_BUILDING)) *mirror = e->face >= 3 && e->face <= 5;
  int f = anim_frame(e, a, d);
  if (f >= a->nf) f = a->nf - 1;
  return a->img + dir * a->nf + f;
}

/* number: its hitpoints written over the bar (1), under it (2), or not (0) */
static void hp_bar(int x, int y, int w, int32_t hp, int32_t max, int team, int number) {
  if (max <= 0) return;
  int fw = (int)((int64_t)(w - 2) * (hp > 0 ? hp : 0) / max);
  gfx_fill(x, y, w, 4, RGB(0x202020));
  gfx_fill(x + 1, y + 1, fw, 2, team ? RED : BLUE);
  if (number) {
    char t[12];
    snprintf(t, sizeof t, "%ld", (long)(hp > 0 ? hp : 0));
    gfx_text_oc(FONT_NUM, x + w / 2, number == 2 ? y + 6 : y - 10, t, WHITE);
  }
}

static void draw_tower(const Ent *e) {
  const UnitDef *d = &UNITS[e->type];
  int px = e->x / 100, py = e->y / 100;
  bool mirror;
  gfx_ellipse(px, py + 2, d->radius / 100 + 3, d->radius / 160 + 2, BLACK, 7);
  if (d->flags & U_KING) {
    gfx_img_tint(SETS[d->set].idle.img, px, py, e->team, false, WHITE, e->flash > 0 ? 12 : 0);
    hp_bar(px - 16, py - 44, 32, e->hp, e->maxhp, e->team, 1);
    return;
  }
  gfx_img_tint(SETS[d->set].idle.img, px, py, e->team, false, WHITE, e->flash > 0 ? 12 : 0);
  /* the tower's back wall, then the Princess (or the Bomber) standing in it, aiming */
  int top = py - d->attached_h * 77 / 10000;
  if (d->top_set >= 0) gfx_img(SETS[d->top_set].idle.img, px, top, e->team, false);
  if (d->attached != NONE) {
    Ent a = *e;
    a.type = (uint8_t)d->attached;
    int img = unit_img(&a, &mirror);
    gfx_img(img, px, top, e->team, mirror);
  }
  /* (a tower at the top of the screen: its bar under it, where it can be seen) */
  if (d->flags & U_TOWER) {
    if (py - 48 < 0) hp_bar(px - 14, py + 8, 28, e->hp, e->maxhp, e->team, 2);
    else hp_bar(px - 14, py - 38, 28, e->hp, e->maxhp, e->team, 1);
  }
}

static void draw_unit(const Ent *e) {
  const UnitDef *d = &UNITS[e->type];
  if (d->flags & U_TOWER || d->attached != NONE) {
    draw_tower(e);
    if (d->flags & U_TOWER) return;
  }
  int px = e->x / 100, py = e->y / 100;
  int h = d->flags & U_FLY ? height_px(d->fly_height) : 0;
  if (d->flags & U_JUMP && e->x >= RIVER_X0 - 500 && e->x < RIVER_X1 + 500) {
    int32_t m = (RIVER_X0 + RIVER_X1) / 2, k = 1500 - (e->x > m ? e->x - m : m - e->x);
    if (k > 0) h = k * 12 / 1500;
  }
  bool mirror;
  int img = unit_img(e, &mirror);
  if (d->attached == NONE) {
    /* the shadow, a little of the side's colour in it: whose a Skeleton is, at a glance */
    gfx_ellipse(px, py, d->radius / 110 + 2, d->radius / 180 + 1, e->team ? RGB(0x500808) : RGB(0x081850), h ? 9 : 12);
    if (e->freeze > 0)
      gfx_img_tint(img, px, py - h, e->team, mirror, RGB(0xA0E0FF), 16);
    else if (e->deploy > 0)
      gfx_img_tint(img, px, py - h, e->team, mirror, WHITE, 14);
    else
      gfx_img_tint(img, px, py - h, e->team, mirror, WHITE, e->flash > 0 ? 12 : 0);
  }
  int bx, by, bw, bh;
  gfx_img_box(img, mirror, &bx, &by, &bw, &bh);
  if (e->deploy > 0 && d->deploy_ms) {
    /* the deploy clock */
    int w = 12, f = w * (d->deploy_ms - e->deploy) / d->deploy_ms;
    gfx_fill(px - w / 2, py + 3, w, 2, RGB(0x202020));
    gfx_fill(px - w / 2, py + 3, f, 2, WHITE);
  }
  if (e->hp < e->maxhp && !(d->flags & U_BOMB)) {
    int w = d->radius >= 700 ? 18 : 12;
    hp_bar(px - w / 2, py - h + by - 5, w, e->hp, e->maxhp, e->team, 0);
  }
  if (d->dmg2 && (e->flags & E_ATTACKING) && ent_alive(&bt, e->target)) {
    /* the Inferno Tower's beam */
    const Ent *t = &bt.e[e->target];
    int th = UNITS[t->type].flags & U_FLY ? height_px(UNITS[t->type].fly_height) : 4;
    uint16_t c = e->lock >= d->t1 + d->t2 ? RGB(0xFFF080) : e->lock >= d->t1 ? RGB(0xFF9030) : RGB(0xE03020);
    gfx_line(px, py - 18, t->x / 100, t->y / 100 - th, c);
    gfx_line(px + 1, py - 18, t->x / 100 + 1, t->y / 100 - th, c);
  }
  if ((d->flags & U_HIDES) && !e->hidden && (e->flags & E_ATTACKING) && e->post < 150 && ent_alive(&bt, e->target)) {
    const Ent *t = &bt.e[e->target];
    gfx_line(px, py - 12, t->x / 100, t->y / 100 - 6, RGB(0xB0F0FF));
  }
}

/* ------------------------------------------------------------------ projectiles and effects */
static void draw_proj(const Proj *p) {
  const ProjDef *d = &PROJS[p->type];
  int px = p->x / 100, py = p->y / 100;
  int32_t total = p->total > 0 ? p->total : 1, t = p->t < total ? p->t : total;
  int z0 = height_px(p->z0);
  int arc = p->spell ? 34 : d->gravity >= 150 ? 18 : d->gravity ? 8 : 0;
  int h = z0 * (total - t) / total + arc * 4 * t / total * (total - t) / total;
  if (d->flags & (P_ARROW | P_SPEAR)) {
    int32_t dx = p->tx - p->x, dy = p->ty - p->y, dd = dist(0, 0, dx, dy);
    int lx = dd ? (int)(dx * 4 / dd) : 4, ly = dd ? (int)(dy * 4 / dd) : 0;
    uint16_t c = d->flags & P_SPEAR ? RGB(0xC0A070) : RGB(0xF0E8D0);
    gfx_line(px - lx, py - h - ly, px + lx / 2, py - h + ly / 2, c);
    if (!p->spell) gfx_line(px - lx, py - h - ly + 1, px, py - h + 1, RGB(0x604020));
    return;
  }
  if (d->img >= 0) {
    gfx_ellipse(px, py, 2, 1, BLACK, 8);
    gfx_img(d->img, px, py - h, 0, p->tx < p->sx);
  }
}

static void draw_fx(const Fx *f) {
  int t = f->t, len = f->len ? f->len : 1;
  switch (f->kind) {
  case FX_EXPLOSION:
    gfx_img(f->size >= 2 ? IMG_EXPLOSION + 6 * t / len : IMG_PUFF + 4 * t / len, f->x, f->y, 0, false);
    break;
  case FX_PUFF:
  case FX_SPLASH:
    gfx_img(IMG_PUFF + 4 * t / len, f->x, f->y - 4, 0, false);
    break;
  case FX_ZAP:
    gfx_img(IMG_ZAP + 3 * t / len, f->x, f->y, 0, false);
    gfx_ring(f->x, f->y, 25 * (len - t / 2) / len, 25 * (len - t / 2) / len, 2, RGB(0xC0F0FF), 20);
    break;
  case FX_LIGHTNING:
    gfx_img(IMG_LIGHTNING + 3 * t / len, f->x, f->y, 0, false);
    break;
  case FX_TEXT:
    gfx_text_oc(FONT_NUM, f->x, f->y - t / 60, "+1", PINK);
    break;
  }
}

/* ------------------------------------------------------------------ the field */
typedef struct {
  int16_t y;
  uint8_t kind, i;
} Item;
static Item items[MAXE + MAXP + MAXFX + 8];
static int nitems;

static int cmp_y(const Item *a, const Item *b) { return a->y - b->y; }

static void sort_items(void) {
  for (int i = 1; i < nitems; i++) {   /* (insertion: nearly sorted from frame to frame) */
    Item v = items[i];
    int j = i - 1;
    while (j >= 0 && cmp_y(&items[j], &v) > 0) items[j + 1] = items[j], j--;
    items[j + 1] = v;
  }
}

static void collect(void) {
  nitems = 0;
  for (int i = 0; i < MAXE; i++)
    if (bt.e[i].used) {
      const UnitDef *d = &UNITS[bt.e[i].type];
      items[nitems++] = (Item){(int16_t)(bt.e[i].y / 100 + (d->flags & U_FLY ? 400 : 0)), 0, (uint8_t)i};
    }
  for (int i = 0; i < MAXP; i++)
    if (bt.p[i].used) items[nitems++] = (Item){(int16_t)(bt.p[i].y / 100 + 800), 1, (uint8_t)i};
  for (int i = 0; i < MAXFX; i++)
    if (bt.fx[i].used) items[nitems++] = (Item){(int16_t)(bt.fx[i].y + 600), 2, (uint8_t)i};
  sort_items();
}

static void draw_rubble(void) {
  for (int t = 0; t < 2; t++) {
    const Side *s = &bt.side[t];
    int32_t px = t ? ARENA_X - 6500 : 6500;
    for (int k = 0; k < 2; k++)
      if (s->princess[k] < 0) gfx_img(IMG_RUBBLE, px / 100, (k ? 14500 : 3500) / 100, 0, t);
    if (s->king < 0) gfx_img(IMG_RUBBLE + 1, (t ? ARENA_X - 3000 : 3000) / 100, 90, 0, t);
  }
}

static void draw_areas(void) {
  for (int i = 0; i < MAXA; i++) {
    const Area *a = &bt.a[i];
    if (!a->used) continue;
    const AreaDef *d = &AREAS[a->type];
    int r = d->radius / 100;
    if (d->speed_mul < 0) {
      gfx_ellipse(a->x / 100, a->y / 100, r, r, RGB(0xA8E8FF), 9);
      gfx_ring(a->x / 100, a->y / 100, r, r, 2, WHITE, 18);
    } else if (d->speed_mul > 0) {
      gfx_ellipse(a->x / 100, a->y / 100, r, r, RGB(0xC040E0), 8);
      gfx_ring(a->x / 100, a->y / 100, r, r, 2, RGB(0xE070FF), 18);
    } else if (d->proj != NONE) {
      gfx_ring(a->x / 100, a->y / 100, r, r, 1, RGB(0xE0F0FF), 10);
    }
  }
}

static void draw_zone(void) {
  /* where the card may go: the rest tinted red, as the game does while a card is held */
  int card = bt.side[0].hand[g.slot];
  if (card == NONE || CARDS[card].kind == K_SPELL) return;
  bool top = bt.side[1].princess[0] < 0, bot = bt.side[1].princess[1] < 0;
  int x0 = RIVER_X0 / 100, x1 = (RIVER_X1 + 4000) / 100;
  gfx_blend(x0, 0, SCREEN_W - x0, FIELD_H, RED, 7);
  /* (the open pockets: undo the tint by drawing them again from the arena) */
  if (top || bot) {
    for (int y = gfx_y0; y < gfx_y0 + BAND_H && y < FIELD_H; y++) {
      bool open = y < FIELD_H / 2 ? top : bot;
      if (!open) continue;
      const uint8_t *s = arena_idx + y * SCREEN_W;
      uint16_t *d = gfx_band + (y - gfx_y0) * SCREEN_W;
      for (int x = RIVER_X1 / 100; x < x1; x++) d[x] = (uint16_t)(arena_pal[2 * s[x]] | arena_pal[2 * s[x] + 1] << 8);
    }
  }
}

static void draw_cursor(void) {
  int card = bt.side[0].hand[g.slot];
  int x = g.cx * 10, y = g.cy * 10;
  int32_t ux = g.cx * 1000 + 500, uy = g.cy * 1000 + 500;
  bool ok = card != NONE && battle_can_place(&bt, 0, card, ux, uy);
  uint16_t c = ok ? WHITE : RED;
  if (card != NONE) {
    const CardDef *cd = &CARDS[card];
    if (cd->kind == K_SPELL) {
      int r = cd->radius / 100;
      gfx_ellipse(x + 5, y + 5, r, r, WHITE, 6);
      gfx_ring(x + 5, y + 5, r, r, 1, c, 26);
    } else if (cd->unit != NONE) {
      const UnitDef *u = &UNITS[cd->unit];
      int img = SETS[u->set].idle.img + (SETS[u->set].idle.ndir >= 5 ? 2 : 0) * SETS[u->set].idle.nf;
      int h = u->flags & U_FLY ? height_px(u->fly_height) : 0;
      gfx_img_tint(img, x + 5, y + 5 - h, 0, false, ok ? WHITE : RED, 16);
      if (cd->kind == K_BUILDING && u->range > 0 && u->range < 14000) {
        int r = u->range / 100;
        gfx_ring(x + 5, y + 5, r, r, 1, WHITE, 8);
      }
    }
  }
  gfx_frame_rect(x, y, 10, 10, c);
}

void view_battle(void) {
  if (!gfx_rows(0, FIELD_H)) return;
  gfx_arena(arena_idx, arena_pal, 0, FIELD_H);
  if (g.field && g.screen == SC_BATTLE) draw_zone();
  draw_rubble();
  draw_areas();
  for (int k = 0; k < nitems; k++) {
    const Item *it = &items[k];
    if (it->kind == 0) {
      const Ent *e = &bt.e[it->i];
      if (e->used) draw_unit(e);
    } else if (it->kind == 1) {
      if (bt.p[it->i].used) draw_proj(&bt.p[it->i]);
    } else if (bt.fx[it->i].used) {
      draw_fx(&bt.fx[it->i]);
    }
  }
  if (g.field && g.screen == SC_BATTLE) draw_cursor();
  /* the time left, top right */
  char t[16];
  int32_t s = (bt.time_ms + 999) / 1000;
  snprintf(t, sizeof t, "%ld:%02ld", (long)(s / 60), (long)(s % 60));
  gfx_blend(250, 0, 70, 30, BLACK, 14);
  gfx_text_c(FONT_SMALL, 285, 1, bt.overtime ? "Overtime" : "Time left", bt.overtime ? RGB(0xFF8060) : RGB(0xE0E0E0));
  gfx_text_oc(FONT_MID, 285, 14, t, bt.overtime || bt.time_ms <= 60000 ? RGB(0xFFB040) : WHITE);
  if (battle_double(&bt)) gfx_text_oc(FONT_NUM, 285, 33, "Elixir x2", PINK);
  if (g.msg && g.msg_t > 0) {
    int w = gfx_text_w(FONT_MID, g.msg) + 16;
    gfx_blend(160 - w / 2, 76, w, 24, BLACK, 18);
    gfx_text_oc(FONT_MID, 160, 81, g.msg, WHITE);
  }
}

/* ------------------------------------------------------------------ the hand */
static void card_at(int card, int x, int y, bool big, bool dim) {
  if (card == NONE) {
    gfx_round_rect(x, y, 36, 45, 3, RGB(0x6A4A2A));
    return;
  }
  const CardDef *c = &CARDS[card];
  gfx_img_dim(c->icon, x + 18, y + 22, 0, false, dim ? 18 : 0);
  if (big) {
    gfx_img(IMG_DROP_BIG, x + 6, y + 38, 0, false);
    char t[4];
    snprintf(t, sizeof t, "%ld", (long)c->cost);
    gfx_text_oc(FONT_NUM, x + 6, y + 34, t, WHITE);
  }
}

void view_hud(void) {
  if (!gfx_rows(HUD_Y, SCREEN_H - HUD_Y)) return;
  const Side *s = &bt.side[0];
  gfx_fill(0, HUD_Y, SCREEN_W, SCREEN_H - HUD_Y, RGB(0xC48B52));
  gfx_fill(0, HUD_Y, SCREEN_W, 2, RGB(0x6E4423));
  /* next */
  gfx_text_c(FONT_SMALL, 20, HUD_Y + 4, "Next", RGB(0x3A2210));
  if (s->nqueue) {
    const CardDef *c = &CARDS[s->queue[0]];
    gfx_img(c->icon + 1, 20, HUD_Y + 34, 0, false);
  }
  /* the four cards */
  for (int k = 0; k < 4; k++) {
    int x = 42 + k * 44, y = HUD_Y + 5;
    int card = s->hand[k];
    bool sel = k == g.slot && (g.field || g.screen == SC_BATTLE);
    if (sel) y -= 3;
    gfx_round_rect(x - 2, y - 2, 40, 49, 3, sel ? (g.field ? WHITE : GOLD) : RGB(0x8A5A30));
    bool afford = card != NONE && CARDS[card].cost <= s->elixir;
    card_at(card, x, y, true, !afford);
    char n[2] = {(char)('1' + k), 0};
    gfx_text_o(FONT_NUM, x + 28, y + 1, n, sel ? GOLD : WHITE);
  }
  /* elixir */
  int ex = 42, ey = SCREEN_H - 9, ew = 176;
  gfx_fill(ex, ey, ew, 7, RGB(0x3A2416));
  int32_t regen = battle_regen_ms(&bt);
  int32_t fill = s->elixir * 1000 + (s->elixir < GL(MAX_MANA) ? s->elixir_t * 1000 / regen : 0);
  int fw = (int)((ew - 2) * fill / (GL(MAX_MANA) * 1000));
  gfx_fill(ex + 1, ey + 1, fw, 5, PINK);
  for (int k = 1; k < 10; k++) gfx_fill(ex + 1 + (ew - 2) * k / 10, ey + 1, 1, 5, RGB(0x3A2416));
  gfx_img(IMG_DROP_BIG, 22, SCREEN_H - 12, 0, false);
  char t[4];
  snprintf(t, sizeof t, "%ld", (long)s->elixir);
  gfx_text_oc(FONT_MID, 22, SCREEN_H - 22, t, WHITE);
  /* crowns and keys */
  gfx_img(IMG_CROWN, 234, HUD_Y + 12, 0, false);
  snprintf(t, sizeof t, "%d", bt.side[0].crowns);
  gfx_text_o(FONT_MID, 246, HUD_Y + 4, t, BLUE);
  gfx_text_o(FONT_MID, 260, HUD_Y + 4, "-", WHITE);
  snprintf(t, sizeof t, "%d", bt.side[1].crowns);
  gfx_text_o(FONT_MID, 271, HUD_Y + 4, t, RED);
  uint16_t k = RGB(0x3A2210);
  static const char *const hand[3] = {"1-4: a card", "OK: place it", "Back: pause"};
  static const char *const field[3] = {"arrows: move", "OK: drop it", "Back: cancel"};
  for (int i = 0; i < 3; i++) gfx_text(FONT_SMALL, 224, HUD_Y + 21 + 13 * i, (g.field ? field : hand)[i], k);
}

void view_prepare(void) { collect(); }
