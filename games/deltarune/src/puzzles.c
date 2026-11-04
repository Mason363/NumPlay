#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif
/* The Dark World's chests and puzzles (puzzles.h): obj_treasure_room, the glowing tiles (obj_glowtilepuzz,
 * obj_glowtile), the suits puzzles (obj_suitspuzz and its buttons) and the blocks Kris pushes
 * (obj_pushableblock, obj_blocktile). */
#include <stdio.h>
#include <string.h>
#include "cut.h"
#include "game.h"
#include "gfx.h"
#include "gmtexts.h"
#include "puzzles.h"
#include "tables.h"

/* ---------------------------------------------------------------- what Kris (and the party) stand on */
static bool box_over(int ax0, int ay0, int ax1, int ay1, int bx0, int by0, int bx1, int by1) {
  return ax1 > bx0 && ax0 < bx1 && ay1 > by0 && ay0 < by1;
}
/* Kris's box, or a follower's (k 0) */
bool party_on(int x0, int y0, int x1, int y1, bool followers) {
  if (!pl.hidden && box_over(pl.x, pl.y + 50, pl.x + 38, pl.y + 78, x0, y0, x1, y1)) return true;
  if (!followers || !follow_shown()) return false;
  for (int c = CH_SUSIE; c <= CH_RALSEI; c++) {
    int fx, fy;
    if (have_char(c) && follower_xy(c, &fx, &fy) && box_over(fx + 4, fy + 62, fx + 42, fy + 90, x0, y0, x1, y1))
      return true;
  }
  return false;
}

/* ---------------------------------------------------------------- the key items' names (scr_keyiteminfo) */
const char *keyitem_name(int id) {
  static const uint16_t names[8] = {0xFFFF, scr_keyiteminfo_slash_scr_keyiteminfo_gml_14_0,
                                    scr_keyiteminfo_slash_scr_keyiteminfo_gml_19_0, scr_keyiteminfo_slash_scr_keyiteminfo_gml_24_0,
                                    scr_keyiteminfo_slash_scr_keyiteminfo_gml_28_0, scr_keyiteminfo_slash_scr_keyiteminfo_gml_32_0,
                                    scr_keyiteminfo_slash_scr_keyiteminfo_gml_36_0, scr_keyiteminfo_slash_scr_keyiteminfo_gml_40_0};
  return id > 0 && id < 8 ? str(names[id]) : " ";
}
const char *keyitem_desc(int id) {
  static const uint16_t d[8] = {0xFFFF, scr_keyiteminfo_slash_scr_keyiteminfo_gml_13_0,
                                scr_keyiteminfo_slash_scr_keyiteminfo_gml_18_0, scr_keyiteminfo_slash_scr_keyiteminfo_gml_23_0,
                                scr_keyiteminfo_slash_scr_keyiteminfo_gml_27_0, scr_keyiteminfo_slash_scr_keyiteminfo_gml_31_0,
                                scr_keyiteminfo_slash_scr_keyiteminfo_gml_35_0, scr_keyiteminfo_slash_scr_keyiteminfo_gml_39_0};
  return id > 0 && id < 8 ? str(d[id]) : "---";
}

/* ---------------------------------------------------------------- treasure (obj_treasure_room) */
enum { T_ARMOR, T_WEAPON, T_ITEM, T_KEY, T_GOLD };
typedef struct {
  int flag, type, id;
  bool extratext;
} Chest;

/* the room's chest: its flag, what is in it; false: none there */
static bool chest_of(int x, int y, Chest *t) {
  const RoomInfo *r = world_room_info();
  t->flag = 104, t->type = T_ARMOR, t->id = 4, t->extratext = false;
  switch (g_room) {
    case room_field_maze: t->extratext = true; break;
    case room_field_secret1: t->flag = 117, t->type = T_KEY, t->id = 7; break;
    case room_forest_dancers1:
      if (x >= r->w / 2) t->flag = 116, t->type = T_KEY, t->id = 6;
      else t->flag = 107, t->type = T_ITEM, t->id = 2, t->extratext = true;
      break;
    case room_forest_area2A: t->flag = 108, t->type = T_WEAPON, t->id = 9, t->extratext = true; break;
    case room_forest_area3A: t->flag = 109, t->type = T_ARMOR, t->id = 2, t->extratext = true; break;
    case room_forest_area4:
      if (y <= r->h / 2) t->flag = 110, t->type = T_GOLD, t->id = 40, t->extratext = true;
      else t->flag = 111, t->type = T_ITEM, t->id = 2, t->extratext = true;
      break;
    case room_cc_prison_prejoker:
      if (flag(112) == 1 || flag(242) == 0) return false;
      t->flag = 112, t->type = flag(242) == 1 ? T_WEAPON : T_ARMOR, t->id = 7;
      break;
    case room_cc_2f: t->flag = 113, t->type = T_ITEM, t->id = 11; break;
    case room_cc_4f: t->flag = 114, t->type = T_ITEM, t->id = 2, t->extratext = true; break;
    case room_forest_secret1: t->flag = 116, t->type = T_KEY, t->id = 6; break;
    default: break;
  }
  return true;
}

void chests_setup(void) {
  Inst in;
  for (int k = 0; world_inst(obj_treasure_room, k, &in); k++) {
    Chest t;
    if (!chest_of(in.x, in.y, &t)) continue;
    Obj *o = obj_new(spr_treasurebox, in.x, in.y);
    o->flags |= OF_TALK | OF_SOLID;
    o->obj = obj_treasure_room, o->inst = (int16_t)in.n;
    o->img = flag(t.flag) == 1 ? FX : 0;
    obj_set_box(o, 0, 16, 40, 24);
    o->event = EV_CHEST;
  }
}

static const char *subst2(char *o, int n, uint16_t fmt, const char *a, const char *b) {
  const char *f = str(fmt);
  int k = 0;
  for (int i = 0; f[i] && k < n - 1; i++) {
    if (f[i] == '~' && (f[i + 1] == '1' || f[i + 1] == '2')) {
      for (const char *s = f[i + 1] == '1' ? a : b; s && *s && k < n - 1; s++) o[k++] = *s;
      i++;
    } else {
      o[k++] = f[i];
    }
  }
  o[k] = 0;
  return o;
}

/* Kris opens it: what was inside goes where it belongs, or there's no room for it */
int chest_event(Ev *c) {
  static char m0[120], m1[120], gold[24];
  static int hathy_equip;
  EV_BEGIN;
  {
    Obj *o = c->self;
    Chest t;
    chest_of(o->x / FX, o->y / FX, &t);
    o->img = FX;
    text_clear();
    if (flag(t.flag) == 1) {
      if (g_room == room_field_maze) text_add(str(obj_treasure_room_slash_Other_10_gml_19_0));
      else if ((g_room == room_forest_dancers1 || g_room == room_cc_4f) && t.extratext && have_char(CH_RALSEI)) {
        if (have_char(CH_SUSIE)) {
          static const int ids[] = {obj_treasure_room_slash_Other_10_gml_31_0, F_('R', 8), obj_treasure_room_slash_Other_10_gml_33_0,
                                    F_('S', 7), obj_treasure_room_slash_Other_10_gml_35_0, -1};
          say_add(ids);
        } else {
          static const int ids[] = {obj_treasure_room_slash_Other_10_gml_24_0, F_('R', 8), obj_treasure_room_slash_Other_10_gml_26_0, -1};
          say_add(ids);
        }
      } else if (g_room == room_forest_area3A) text_add(str(obj_treasure_room_slash_Other_10_gml_40_0));
      else text_add(str(obj_treasure_room_slash_Other_10_gml_18_0));
      text_open();
    } else {
      const char *name = " ", *kind = " ";
      switch (t.type) {
        case T_ARMOR:
          name = str(armor_defs[t.id].name), kind = str(obj_treasure_room_slash_Other_10_gml_54_0), gm_armorget(t.id);
          break;
        case T_WEAPON:
          name = str(weapon_defs[t.id].name), kind = str(obj_treasure_room_slash_Other_10_gml_61_0), gm_weaponget(t.id);
          break;
        case T_ITEM:
          name = str(item_defs[t.id].name), kind = str(obj_treasure_room_slash_Other_10_gml_68_0), gm_itemget(t.id);
          break;
        case T_KEY:
          name = keyitem_name(t.id), kind = str(obj_treasure_room_slash_Other_10_gml_75_0), gm_keyitemget(t.id);
          break;
        default:
          g_noroom = 0;
          sv.gold += t.id;
          kind = str(obj_treasure_room_slash_Other_10_gml_82_0);
          snprintf(gold, sizeof gold, "%d%s", t.id, str(obj_treasure_room_slash_Other_10_gml_83_0));
          name = gold;
          break;
      }
      subst2(m0, sizeof m0, obj_treasure_room_slash_Other_10_gml_87_0, name, NULL);
      text_add(m0);
      if (!g_noroom) {
        if (g_room == room_field_maze && plot() <= 40) {
          static const int ids[] = {F_('R', 0), obj_treasure_room_slash_Other_10_gml_105_0, obj_treasure_room_slash_Other_10_gml_106_0,
                                    obj_treasure_room_slash_Other_10_gml_107_0, -1};
          text_add(subst2(m1, sizeof m1, obj_treasure_room_slash_Other_10_gml_103_0, name, kind));
          say_add(ids);
          hathy_equip = 1;
        } else {
          text_add(subst2(m1, sizeof m1, obj_treasure_room_slash_Other_10_gml_99_0, name, kind));
        }
        set_flag(t.flag, 1);
      } else {
        text_add(subst2(m1, sizeof m1, obj_treasure_room_slash_Other_10_gml_115_0, kind, NULL));
        c->v[0] = 1;   /* (closes again) */
      }
      text_open();
    }
  }
  WAIT_UNTIL(!text_active());
  if (c->v[0]) c->self->img = 0;
  if (hathy_equip) hathy_equip = 0, chest_equip_hint();
  EV_END;
}

/* ---------------------------------------------------------------- the glowing tiles */
/* obj_glowtilepuzz: the hourglass starts it; the lit tiles must be stood on together, three times, before the
 * sand runs out */
static struct {
  bool on;
  int x, y;                 /* the hourglass */
  int ntile, tx[8], ty[8];
  uint8_t tstate[8];        /* 0 off, 1 lit (to step on), 2 stepped */
  int8_t stepbuf[8];
  bool tactive[8];
  int active, failtimer, failmax, failamt, wonamt, wonmax, funtotal, funbuffer, plotamt, talked, shakecon;
} G;

static void glow_tiles_off(void) {
  for (int i = 0; i < G.ntile; i++) G.tactive[i] = false, G.tstate[i] = 0, G.stepbuf[i] = -1;
}
/* the next set lit (event_user 2) */
static void glow_light(void) {
  int c1 = 2, c2 = 2, c3;
  G.funbuffer = 0;
  glow_tiles_off();
  if (G.wonamt >= G.wonmax) return;
  if (G.wonamt == 0) c1 = 2, c2 = 2, G.funtotal = 1;
  if (G.wonamt == 1) c1 = 0, c2 = 1, G.funtotal = 2;
  if (G.wonamt >= 2) c1 = 1, c2 = 2, G.funtotal = 2;
  c3 = c1;
  if (g_room == room_field_puzzle2) {
    if (G.wonamt == 0) c1 = c2 = c3 = 3, G.funtotal = 1;
    if (G.wonamt == 1) c1 = 2, c2 = 2, c3 = 1, G.funtotal = 2;
    if (G.wonamt == 2) c1 = 2, c2 = 3, c3 = 4, G.funtotal = 3;
    if (G.wonamt >= 3) c1 = 1, c2 = 0, c3 = 4, G.funtotal = 3;
  }
  int cs[3] = {c1, c2, c3};
  for (int k = 0; k < 3; k++)
    if (cs[k] < G.ntile) G.tactive[cs[k]] = true, G.tstate[cs[k]] = 1;
}

/* puzzle 1's spikes: one pair (and its wall) a win */
static void glow_spikes(int i, bool down) {
  Obj *w = obj_find(ACT_X4 + i);
  for (int j = 0; j < MAX_OBJS; j++)
    if (!(objs[j].flags & OF_GONE) && objs[j].spr == spr_spiketile && (objs[j].name == ACT_X1 + i || objs[j].arg == 100 + i))
      objs[j].img = down ? FX : 0;
  if (w) {
    if (down) w->flags = (uint16_t)(w->flags & ~OF_SOLID);
    else w->flags |= OF_SOLID;
  }
}

void glow_setup(void) {
  memset(&G, 0, sizeof G);
  Inst in;
  if (!world_inst(obj_glowtilepuzz, 0, &in)) return;
  G.on = true, G.x = in.x, G.y = in.y;
  G.wonmax = 3, G.failmax = 200, G.plotamt = 1, G.funtotal = 99;
  for (int k = 0; k < 8 && world_inst(obj_glowtile, k, &in); k++) G.tx[k] = in.x, G.ty[k] = in.y, G.ntile++;
  Obj *h = obj_new(spr_hourglass_switch_off, G.x, G.y);
  h->name = ACT_X6, h->flags |= OF_TALK | OF_SOLID, h->ispd = FX / 10;
  obj_set_box(h, 0, 0, 40, 80);
  h->event = EV_GLOW_SWITCH;
  if (g_room == room_field_puzzle1) {
    G.failmax = 240, G.plotamt = 35;
    for (int i = 0; i < 3; i++) {
      Obj *w = obj_new(0xFFFF, 1280 + i * 40, 280);
      w->name = (uint8_t)(ACT_X4 + i), w->flags |= OF_SOLID | OF_HIDDEN;
      obj_set_box(w, 0, 0, 40, 80);
      Obj *s1 = obj_new(spr_spiketile, 1280 + i * 40, 280);
      s1->name = (uint8_t)(ACT_X1 + i), s1->flags |= OF_BACK;
      Obj *s2 = obj_new(spr_spiketile, 1280 + i * 40, 320);
      s2->arg = (int16_t)(100 + i), s2->flags |= OF_BACK;
      if (plot() >= 35) G.active = 2, glow_spikes(i, true);
    }
  }
  if (g_room == room_field_puzzle2) {
    G.failmax = 240, G.plotamt = 38;
    if (plot() >= 38) G.active = 2;
  }
}

int glow_switch_event(Ev *c) {
  if (G.active == 0) {
    G.active = 1, G.failtimer = 0;
    Obj *h = obj_get(ACT_X6);
    h->spr = spr_hourglass_switch_on, h->img = 0, h->ispd = 0;
    if (g_room == room_field_puzzle1)
      for (int i = 0; i < G.wonmax; i++) glow_spikes(i, false);
    G.wonamt = 0;
    glow_light();
  }
  (void)c;
  return 1;
}

static void glow_step(void) {
  if (!G.on) return;
  /* the tiles under the party (obj_glowtile's collision: event_user 7) */
  for (int i = 0; i < G.ntile; i++) {
    int x0 = G.tx[i] + 10, y0 = G.ty[i] + 6;
    if (G.tactive[i] && party_on(x0, y0, x0 + 20, y0 + 12, true)) {
      G.tstate[i] = 2;
      G.stepbuf[i] = 3;
    }
    if (G.stepbuf[i] > 0 && --G.stepbuf[i] == 0 && G.tactive[i]) G.tstate[i] = 1;
  }
  int cur = 0;
  for (int i = 0; i < G.ntile; i++) cur += G.tstate[i] == 2 && G.stepbuf[i] > 0;
  G.funbuffer--;
  if (cur == G.funtotal && G.funbuffer < -1) {
    if (g_room == room_field_puzzle1) glow_spikes(G.wonamt, true);
    G.wonamt++;
    G.funbuffer = 7;
    if (G.wonamt == G.wonmax) {
      if (g_room == room_field_puzzle2) G.shakecon = 1;
      if (plot() < G.plotamt) set_plot(G.plotamt);
      G.active = 2;
      Obj *h = obj_get(ACT_X6);
      h->spr = spr_hourglass_switch_off, h->img = 0;
      G.funbuffer = -1;
      glow_tiles_off();
    }
  }
  if (G.funbuffer == 4) glow_tiles_off();
  if (G.funbuffer == 1) glow_light();
  if (G.active == 1) {
    G.failtimer++;
    obj_get(ACT_X6)->img = G.failtimer / (G.failmax / 8) * FX;
    if (G.failtimer >= G.failmax) {
      G.active = 0;
      Obj *h = obj_get(ACT_X6);
      h->spr = spr_hourglass_switch_off, h->img = 0;
      G.failamt++;
      if (G.failamt >= 3 && G.talked == 0 && !event_running()) {
        G.talked = 1;
        room_queue_event(EV_GLOW_HINT);
      }
      glow_tiles_off();
    }
  }
  if (G.shakecon == 1 && !event_running()) G.shakecon = 2, room_queue_event(EV_GLOW_SHAKE);
}

int glow_hint_event(Ev *c) {
  static char hint[160];
  EV_BEGIN;
  if (g_room == room_field_puzzle1) {
    snprintf(hint, sizeof hint, "%s%s%s", str(obj_glowtilepuzz_slash_Step_0_gml_78_0), "Back",
             str(obj_glowtilepuzz_slash_Step_0_gml_78_1));
    text_clear();
    text_face(FC_RALSEI, 1);
    text_add(str(obj_glowtilepuzz_slash_Step_0_gml_77_0));
    text_add(hint);
    text_open();
  } else if (!have_char(CH_SUSIE)) {
    static const int ids[] = {obj_glowtilepuzz_slash_Step_0_gml_91_0, obj_glowtilepuzz_slash_Step_0_gml_92_0, -1};
    say_list(FC_RALSEI, 1, ids);
  } else {
    static const int ids[] = {obj_glowtilepuzz_slash_Step_0_gml_98_0, obj_glowtilepuzz_slash_Step_0_gml_99_0, F_('S', 2),
                              obj_glowtilepuzz_slash_Step_0_gml_101_0, -1};
    say_list(FC_RALSEI, 1, ids);
  }
  WAIT_UNTIL(!text_active());
  G.talked = 2;
  EV_END;
}

int glow_shake_event(Ev *c) {
  EV_BEGIN;
  g_shake = 30;
  WAIT(30);
  TALK(obj_glowtilepuzz_slash_Step_0_gml_148_0);
  G.shakecon = 4;
  EV_END;
}

static void glow_draw(void) {
  if (!G.on) return;
  for (int i = 0; i < G.ntile; i++) {
    uint16_t spr = G.tstate[i] == 2 ? spr_glowtile_on : G.tactive[i] ? spr_glowtile_step : spr_glowtile_off;
    int fr = spr == spr_glowtile_step ? (int)(g_frame / 5) % 11 : 0;
    gfx_sprite(spr, fr, (G.tx[i] - cam_x) / 2, (G.ty[i] - cam_y) / 2, 0, 255);
  }
}

/* ---------------------------------------------------------------- the suits puzzles (obj_suitspuzz) */
/* buttons: a spade, a diamond, and the swap (spade <-> club, diamond <-> heart); the row must read sol[] */
static struct {
  bool on;
  int x, y, max_suit, cur_s, won;
  int8_t suit[4], sol[4];
  bool drawclue;
  int bx[3], by[3], press[3];
} SP;

void suits_setup(int x, int y, int max_suit, const int8_t *sol, bool clue) {
  memset(&SP, 0, sizeof SP);
  SP.on = true, SP.x = x, SP.y = y, SP.max_suit = max_suit, SP.drawclue = clue;
  for (int i = 0; i < max_suit && i < 4; i++) SP.sol[i] = sol[i];
  Inst in;
  for (int k = 0; k < 3 && world_inst(obj_suitspuzz_button, k, &in); k++) {
    SP.bx[k] = in.x, SP.by[k] = in.y;
    Obj *b = obj_new(0xFFFF, in.x, in.y);
    b->flags |= OF_TALK | OF_HIDDEN, b->arg = (int16_t)k;
    obj_set_box(b, 8, 0, 24, 40);
    b->event = EV_SUITS_BUTTON;
  }
}
bool suits_won(void) { return SP.on && SP.won; }

int suits_button_event(Ev *c) {
  int k = c->self->arg;
  if (!SP.won) {
    if (k < 2) {
      if (SP.cur_s < SP.max_suit) SP.suit[SP.cur_s++] = (int8_t)(k + 1);
      else memset(SP.suit, 0, sizeof SP.suit), SP.cur_s = 0;
    } else {
      for (int i = 0; i < SP.max_suit; i++) {
        int8_t s = SP.suit[i];
        SP.suit[i] = s == 1 ? 3 : s == 3 ? 1 : s == 2 ? 4 : s == 4 ? 2 : 0;
      }
    }
  }
  SP.press[k] = 2;
  return 1;
}

static void suits_step(void) {
  if (!SP.on) return;
  int vic = 0;
  for (int i = 0; i < SP.max_suit; i++) vic += SP.suit[i] > 0 && SP.suit[i] == SP.sol[i];
  if (vic == SP.max_suit) SP.won = 1;
  for (int k = 0; k < 3; k++)
    if (SP.press[k] > 0) SP.press[k]--;
}

static void suits_draw(void) {
  if (!SP.on) return;
  for (int k = 0; k < 3; k++)
    if (SP.bx[k] || SP.by[k]) gfx_sprite(spr_suitsbutton, k * 2 + (SP.press[k] > 0), (SP.bx[k] - cam_x) / 2, (SP.by[k] - cam_y) / 2, 0, 255);
  for (int i = 0; i < SP.max_suit; i++) {
    int x = (SP.x + i * 40 - cam_x) / 2, y = (SP.y - cam_y) / 2;
    if (SP.drawclue) gfx_sprite(spr_suitsicon_fade, SP.sol[i] - 1, x, y, 0, 255);
    gfx_sprite(spr_suitsicon, 4, x, y + 20, 0, 255);
    if (SP.suit[i] > 0) gfx_sprite(spr_suitsicon, SP.suit[i] - 1, x, y + 20, 0, 255);
  }
}

/* ---------------------------------------------------------------- the room's puzzles, each frame */
void puzzles_reset(void) {
  memset(&G, 0, sizeof G);
  memset(&SP, 0, sizeof SP);
}
void puzzles_step(void) {
  glow_step();
  suits_step();
}
void puzzles_draw_under(void) {
  glow_draw();
  suits_draw();
}
