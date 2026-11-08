/* The inventory (Inventory Control): opened with the inventory key when the Knight can be stopped, or at a bench;
 * the game is paused behind it (SetIsInventoryOpen: game.c), its screen frozen (the game's blurred: here the world
 * dimmed). Its panes: Inventory (UI Inventory: what the Knight has), Charms (UI Charms: the charms, equipped at a
 * bench), side by side; the cursor goes from one to the next through the arrows at their sides. Places in HUD units:
 * the border's from the inventory's place, a pane's from its own (tools/inv.py). */
#pragma GCC optimize("Os")   /* (its code small: not where a frame's time goes) */
#include <math.h>
#include "game.h"

#define DT 0.02f
#define HUD_PX (FOCAL / (-1.342f - CAM_Z))
#define FADE 0.2f          /* (FadeGroup: up and down) */
#define SLIDE 0.35f        /* (Tween Panes: the panes 31 apart) */
#define PANE_W 31.0f
#define CURSOR_TIME 0.15f  /* (Cursor Movement) */
#define TWEEN_TIME 0.25f   /* (a charm to its notch and back: iTweenMoveTo, easeOutSine) */
#define DIM 0.75f          /* (the screen behind it, darkened) */
#define REPEAT_FIRST 0.4f  /* (ui_list_getinput: a held direction again after this, then each REPEAT) */
#define REPEAT 0.1f
#define RESERVE 140        /* (the frame's instances kept for it) */

typedef UiPiece Piece;
typedef struct {
  int16_t sprite;
  float x, y, kx, ky, deg;
} TurnedPiece;
typedef UiText TextAt;
typedef struct {
  float w, h, ox, oy;   /* (a collider: its size and offset, as the world has them) */
} Box;
typedef struct {
  int16_t name, desc;
} NameDesc;

static const Piece border[INV_NBORDER] = INV_BORDER, frame = INV_FRAME, arrow_l = INV_ARROW_L, arrow_r = INV_ARROW_R,
                   pane_arrow_l = INV_PANE_ARROW_L, pane_arrow_r = INV_PANE_ARROW_R;
static const TextAt pane_name = INV_PANE_NAME, pane_name_l = INV_PANE_NAME_L, pane_name_r = INV_PANE_NAME_R;
static const int16_t pane_names[4] = TXT_INV_PANES;

/* the Charms pane */
static const Piece ch_bb = CH_BB, ch_glow = CH_GLOW, ch_next_dot = CH_NEXT_DOT, ch_notch_full = CH_NOTCH_FULL,
                   ch_notch_empty = CH_NOTCH_EMPTY, ch_divider = CH_DIVIDER, ch_cost_pip = CH_COST_PIP,
                   ch_key = CH_CONFIRM_KEY, ch_cursor = CH_CURSOR;
static const float ch_bb_pos[40][2] = CH_BB_POS;
static const uint8_t ch_bb_charm[40] = CH_BB_CHARM;
static const Box ch_bb_box = CH_BB_BOX, ch_eq_box = CH_EQ_BOX, ch_next_box = CH_NEXT_DOT_BOX;
static const int16_t ch_icons[41] = CH_ICONS, ch_names[41] = TXT_CHARM_NAMES_INV, ch_descs[41] = TXT_CHARM_DESCS;
static const TextAt ch_text_equipped = CH_TEXT_EQUIPPED, ch_text_notches = CH_TEXT_NOTCHES, ch_cost_text = CH_COST_TEXT,
                    ch_name = CH_NAME, ch_desc = CH_DESC, ch_confirm = CH_CONFIRM;
static const float ch_cursor_k[2] = CH_CURSOR_K, ch_corners[4][2] = CH_CURSOR_CORNERS;
static const uint8_t notch_full_rgb[3] = CH_NOTCH_FULL_RGB, notch_over_rgb[3] = CH_NOTCH_OVER_RGB;
/* (BuildEquippedCharms: the equipped row) */
#define EQ_X (-7.28f)
#define EQ_Y (-3.86f)
#define EQ_SCALE 1.15f
#define DETAIL_SCALE 1.5f
/* (a description's bottom, from the panes' top: over the border; for a charm, over Equip and its key at a bench, or
 * where charms are changed away from one; under the cost) */
#define DESC_BOTTOM (-13.7f)
#define DESC_BOTTOM_BENCH (-12.35f)
#define CH_DESC_GAP 0.3f
/* (how much lower what comes under a name (its top at top, the next's at next) must be for the name's bigger text) */
static float name_below(float top, float next) {
  const uint8_t *st = font_style(STYLE_DIALOGUE);
  float d = (font_asc(st) + font_desc(st)) / HUD_PX - (top - next);
  return d > 0 ? d : 0;
}

/* the Inventory pane: its fixed items (Mask Shards, Soul Vessel, the nail, Vengeful Spirit, Focus, Geo), the
 * equipment after them (Build Equipment List: four to a row), the relics below */
enum { IT_HEART, IT_VESSEL, IT_NAIL, IT_FIREBALL, IT_FOCUS, IT_GEO, NFIXED };
static const Piece inv_fixed[NFIXED] = INV_FIXED, inv_fixed_bg[NFIXED] = INV_FIXED_BG, inv_heart[4] = INV_HEART_PIECES,
                   inv_vessel[3] = INV_VESSEL_PIECES, inv_eq[] = INV_EQ, inv_trinkets[4] = INV_TRINKETS,
                   inv_trinket_bb = INV_TRINKET_BB;
static const TurnedPiece inv_dividers[2] = INV_DIVIDERS;
static const Box inv_fixed_box[NFIXED] = INV_FIXED_BOX, inv_eq_box[] = INV_EQ_BOX, inv_trinket_box[4] = INV_TRINKET_BOX;
static const TextAt inv_name = INV_NAME, inv_desc = INV_DESC, inv_geo_text = INV_GEO_TEXT, inv_trinket_text = INV_TRINKET_TEXT;
static const NameDesc txt_heart[6] = TXT_INV_HEART, txt_vessel[5] = TXT_INV_VESSEL, txt_fixed[4] = TXT_INV_FIXED,
                      txt_eq[] = TXT_INV_EQ, txt_trinkets[4] = TXT_INV_TRINKETS;
static const int16_t txt_quill[4] = TXT_INV_QUILL;
#define NEQ ((int)(sizeof inv_eq / sizeof inv_eq[0]))
enum { EQ_DASH, EQ_WALLJUMP, EQ_LANTERN, EQ_MAP, EQ_CITY_KEY, EQ_SIMPLE_KEY, EQ_RANCID_EGG };

/* (charmCost_N, as PlayerData starts) */
static const uint8_t costs[41] = {[1] = 1, [2] = 1, [3] = 1, [4] = 2, [6] = 2, [7] = 3, [8] = 2, [13] = 3, [14] = 1, [18] = 2,
                                  [19] = 3, [20] = 2};
int charm_cost(int id) { return id > 0 && id <= 40 ? costs[id] : 0; }
bool charm_equipped(int id) { return id > 0 && id <= 40 && pd_flag(PDF_EQUIPPED_CHARM_1 + id - 1); }
static bool charm_got(int id) { return id > 0 && id <= 40 && pd_flag(PDF_GOT_CHARM_1 + id - 1) && ch_icons[id] >= 0; }

enum { IV_CLOSED, IV_OPEN, IV_CLOSING };
enum { P_INV, P_CHARMS, P_JOURNAL, P_MAP, NPANES };
/* the Charms pane's places for the cursor: the collection, the equipped row, the arrows; its charm's moves */
enum { CA_COLLECTION, CA_EQUIPPED, CA_ARROW_L, CA_ARROW_R };
enum { TW_NONE, TW_UP, TW_DOWN, TW_FAIL, TW_FAIL_BACK };
#define MAX_INV_ITEMS (NFIXED + 7 + 4)

static struct {
  uint8_t st, pane, prev_pane;
  float t;               /* (closing: its time) */
  float border_a, pane_a;
  float slide_t;         /* (panes moving: time so far; -1 none) */
  int8_t slide_dir;
  /* input */
  uint32_t prev_keys, held, blocked;   /* (blocked: keys held as it closed, the game's again once let go) */
  float held_t;
  /* the cursor: where it is (x, y, half width, half height), where from, where to; its time */
  float cur[4], cfrom[4], cto[4], ct;
  bool cursor_set;
  /* Charms */
  uint8_t area, coll, eq;   /* (the collection: a backboard 1..40; the equipped row: 1.. its items) */
  uint8_t tween, tw_charm, oc_attempts;
  float tw_t, tw_from[2], tw_to[2];
  /* Inventory: the item selected (-1, -2: the arrows; its items: as the Knight has them, made when needed) */
  int8_t sel;
  bool map_shortcut;   /* (opened by the map tapped twice: Map Shortcut, the map zoomed into at once) */
} iv;

/* the Inventory pane's items shown: their kinds, their places */
typedef struct {
  uint8_t kind[MAX_INV_ITEMS];
  float xy[MAX_INV_ITEMS][2];
  int n;
} Items;

bool inv_open(void) { return iv.st != IV_CLOSED; }

/* (how much of what is behind it shows: its darkening) */
float inv_behind(void) {
  float ba = iv.st == IV_CLOSED && iv.border_a <= 0 ? 0 : iv.border_a < 0 ? 0 : iv.border_a;
  return 1 - ba * DIM;
}

static bool has_pane(int p) {
  return p == P_INV || (p == P_CHARMS && pd_flag(PDF_HAS_CHARM)) || (p == P_MAP && pd_flag(PDF_HAS_MAP));
}

/* ---------------------------------------------------------------- the Knight's charms */
static int equipped_count(void) {
  int n = 0;
  while (n < (int)sizeof g_pd.equipped && g_pd.equipped[n]) n++;
  return n;
}

static void equip(int id) {
  int n = equipped_count();
  if (n < (int)sizeof g_pd.equipped) g_pd.equipped[n] = (uint8_t)id;
  pd_set_flag(PDF_EQUIPPED_CHARM_1 + id - 1, true);
}

static void unequip(int id) {
  int n = equipped_count(), j = 0;
  for (int i = 0; i < n; i++)
    if (g_pd.equipped[i] != id) g_pd.equipped[j++] = g_pd.equipped[i];
  while (j < n) g_pd.equipped[j++] = 0;
  pd_set_flag(PDF_EQUIPPED_CHARM_1 + id - 1, false);
}

/* ---------------------------------------------------------------- places */
static float pane_x(int p) {
  /* (the current pane at 0; while they move, the one coming from a side, the one going to the other) */
  if (iv.slide_t < 0) return 0;
  float q = iv.slide_t / SLIDE;
  float e = sinf((q > 1 ? 1 : q) * 1.5707964f);   /* (easeOutSine) */
  if (p == iv.pane) return iv.slide_dir * PANE_W * (1 - e);
  return -iv.slide_dir * PANE_W * e;
}

/* the equipped row's n-th place (1..), and the next dot's (n = count + 1) */
static float eq_dx(void) {
  int n = equipped_count();
  return n < 9 ? 1.76f : n == 9 ? 1.7f : n == 10 ? 1.5f : 1.4f;
}
static void eq_pos(int n, float *x, float *y) { *x = EQ_X + (n - 1) * eq_dx(), *y = EQ_Y; }

static int bb_of_charm(int id) {
  for (int i = 0; i < 40; i++)
    if (ch_bb_charm[i] == id) return i + 1;
  return 1;
}

/* the cursor to a place: its center, its box */
static void cursor_to(float x, float y, const Box *b, float kx, float ky) {
  float to[4] = {x + b->ox * kx, y + b->oy * ky, b->w * kx / 2, b->h * ky / 2};
  if (!iv.cursor_set) {
    memcpy(iv.cur, to, sizeof to), memcpy(iv.cfrom, to, sizeof to), memcpy(iv.cto, to, sizeof to);
    iv.cursor_set = true, iv.ct = CURSOR_TIME;
    return;
  }
  memcpy(iv.cfrom, iv.cur, sizeof iv.cur), memcpy(iv.cto, to, sizeof to);
  iv.ct = 0;
}

static const Box arrow_box = {1.2f, 1.6f, 0, 0};

static void charms_cursor(void) {
  float x, y;
  if (iv.area == CA_COLLECTION) {
    cursor_to(ch_bb_pos[iv.coll - 1][0], ch_bb_pos[iv.coll - 1][1], &ch_bb_box, 1, 1);
  } else if (iv.area == CA_EQUIPPED) {
    eq_pos(iv.eq, &x, &y);
    cursor_to(x, y, iv.eq > equipped_count() ? &ch_next_box : &ch_eq_box, 1, 1);
  } else {
    const Piece *a = iv.area == CA_ARROW_L ? &arrow_l : &arrow_r;
    cursor_to(a->x, a->y, &arrow_box, 1, 1);
  }
}

/* the Inventory pane's items as the Knight has them now */
enum { K_HEART_I, K_VESSEL_I, K_NAIL_I, K_FIREBALL_I, K_FOCUS_I, K_GEO_I, K_EQ0 = 16, K_TRINKET0 = 32 };
static void build_items(Items *l) {
  int n = 0;
  for (int k = 0; k < NFIXED; k++) {
    if (k == IT_FIREBALL && !g_pd.fireball_level) continue;
    l->kind[n] = (uint8_t)k, l->xy[n][0] = inv_fixed[k].x, l->xy[n][1] = inv_fixed[k].y, n++;
  }
  int slot = 0;
  for (int k = 0; k < NEQ; k++) {
    bool has = k == EQ_DASH ? g_pd.has_dash : k == EQ_WALLJUMP ? pd_flag(PDF_HAS_WALLJUMP) : k == EQ_LANTERN ? pd_flag(PDF_HAS_LANTERN)
             : k == EQ_MAP ? (pd_flag(PDF_HAS_MAP) || pd_flag(PDF_HAS_QUILL)) : k == EQ_CITY_KEY ? pd_flag(PDF_HAS_CITY_KEY)
             : k == EQ_SIMPLE_KEY ? g_pd.simple_keys > 0 : g_pd.rancid_eggs > 0;
    if (!has) continue;
    l->kind[n] = (uint8_t)(K_EQ0 + k);
    l->xy[n][0] = INV_EQ_X + (slot % 4) * INV_EQ_DX, l->xy[n][1] = INV_EQ_Y + (slot / 4) * INV_EQ_DY;
    n++, slot++;
  }
  for (int k = 0; k < 4; k++)
    if (g_pd.trinkets[k]) {
      l->kind[n] = (uint8_t)(K_TRINKET0 + k), l->xy[n][0] = inv_trinkets[k].x, l->xy[n][1] = inv_trinkets[k].y;
      n++;
    }
  l->n = n;
}

static const Box *item_box(int kind) {
  return kind < NFIXED ? &inv_fixed_box[kind] : kind < K_TRINKET0 ? &inv_eq_box[kind - K_EQ0] : &inv_trinket_box[kind - K_TRINKET0];
}

static void inv_cursor(void) {
  Items l;
  build_items(&l);
  if (iv.sel >= l.n) iv.sel = (int8_t)(l.n - 1);
  if (iv.sel == -1) cursor_to(arrow_l.x, arrow_l.y, &arrow_box, 1, 1);
  else if (iv.sel == -2) cursor_to(arrow_r.x, arrow_r.y, &arrow_box, 1, 1);
  else cursor_to(l.xy[iv.sel][0], l.xy[iv.sel][1], item_box(l.kind[iv.sel]), 1, 1);
}

/* (the map pane's cursor: on an arrow, or not shown (an area chosen)) */
void inv_cursor_arrow(int which) {
  if (which == -1) cursor_to(arrow_l.x, arrow_l.y, &arrow_box, 1, 1);
  else if (which == -2) cursor_to(arrow_r.x, arrow_r.y, &arrow_box, 1, 1);
  else iv.cursor_set = false;
}

static bool other_panes(void) {
  int n = 0;
  for (int p = 0; p < NPANES; p++) n += has_pane(p);
  return n > 1;
}

/* ---------------------------------------------------------------- panes */
static void pane_enter(int p) {
  iv.pane = (uint8_t)p;
  iv.cursor_set = false;
  if (p == P_CHARMS) {
    /* (Activate: at the first charm's backboard) */
    iv.area = CA_COLLECTION, iv.coll = (uint8_t)bb_of_charm(1), iv.eq = 1, iv.oc_attempts = 0, iv.tween = TW_NONE;
    charms_cursor();
  } else if (p == P_MAP) {
    map_pane_enter(iv.map_shortcut);
    iv.map_shortcut = false;
  } else {
    iv.sel = 0;
    inv_cursor();
  }
}

/* (MOVE PANE: the next pane the Knight has that way, round) */
static void move_pane(int dir) {
  int p = iv.pane;
  for (int k = 0; k < NPANES; k++) {
    p = (p + dir + NPANES) % NPANES;
    if (has_pane(p)) break;
  }
  if (p == iv.pane) return;
  iv.prev_pane = iv.pane;
  iv.slide_t = 0, iv.slide_dir = (int8_t)dir;
  pane_enter(p);   /* (ACTIVATE: its cursor at its first place) */
}

static int neighbor_pane(int dir) {
  int p = iv.pane;
  for (int k = 0; k < NPANES; k++) {
    p = (p + dir + NPANES) % NPANES;
    if (has_pane(p)) return p == iv.pane ? -1 : p;
  }
  return -1;
}

/* ---------------------------------------------------------------- opening, closing */
static bool can_open(void) {
  const Hero *h = &g_hero;
  if (pd_flag(PDF_AT_BENCH)) return true;
  return !g_game.paused && !h->control_relinquished && !h->cs.recoiling && !h->cs.transitioning && !h->cs.hazard_death &&
         !h->cs.hazard_respawning && g_game.time_scale >= 1 && !g_pd.disable_pause && !h->cs.dashing && h->accepting_input &&
         !game_changing_room() && !msg_shown();
}

static void open(bool map_shortcut) {
  memset(&iv, 0, sizeof iv);
  iv.st = IV_OPEN, iv.slide_t = -1, iv.ct = CURSOR_TIME, iv.map_shortcut = map_shortcut;
  iv.prev_keys = g_hero.keys;
  g_pd.disable_pause = true;
  if (!pd_flag(PDF_AT_BENCH)) hero_relinquish_control_not_velocity();
  hud_slide(true);
  notices_close();
  int p = g_pd.current_inv_pane;
  pane_enter(has_pane(p) ? p : P_INV);
}

static void close_now(void) {
  iv.st = IV_CLOSED;
  iv.blocked = iv.prev_keys;
  g_pd.current_inv_pane = iv.pane;
  g_pd.disable_pause = false;
  hud_slide(false);
  if (!pd_flag(PDF_AT_BENCH)) hero_regain_control(), hero_start_anim_control();   /* (Regain Control) */
}

void inv_open_map(void) {
  if (iv.st != IV_CLOSED || !can_open()) return;
  g_pd.current_inv_pane = P_MAP;
  open(true);
}

static void start_close(float t) {
  iv.st = IV_CLOSING, iv.t = t;
  if (iv.pane == P_MAP) map_pane_close();
}

void inv_damage(void) {
  if (iv.st == IV_OPEN) start_close(FADE - 0.05f);   /* (Damage Close: down fast) */
}

void inv_reset(void) {
  memset(&iv, 0, sizeof iv);
  g_gfx_overlay = NULL, g_gfx_reserve = 0;
}

/* ---------------------------------------------------------------- the Charms pane's input */
static void charm_select_text(int *name, int *desc, int *cost, int *detail);

static void charms_confirm(void) {
  if (iv.tween != TW_NONE) return;
  int id = 0;
  if (iv.area == CA_COLLECTION) {
    id = ch_bb_charm[iv.coll - 1];
    if (!charm_got(id)) return;
  } else if (iv.area == CA_EQUIPPED) {
    if (iv.eq > equipped_count()) return;   /* (on the next dot) */
    id = g_pd.equipped[iv.eq - 1];
  } else {
    move_pane(iv.area == CA_ARROW_L ? -1 : 1);
    return;
  }
  if (!pd_flag(PDF_AT_BENCH)) return;   /* (charms are changed at a bench) */
  iv.tw_charm = (uint8_t)id, iv.tw_t = 0;
  float x, y;
  if (charm_equipped(id)) {
    /* Tween Down: from its place in the row to its backboard */
    int n = 1;
    while (n <= equipped_count() && g_pd.equipped[n - 1] != id) n++;
    eq_pos(n, &x, &y);
    iv.tw_from[0] = x, iv.tw_from[1] = y;
    int bb = bb_of_charm(id);
    iv.tw_to[0] = ch_bb_pos[bb - 1][0], iv.tw_to[1] = ch_bb_pos[bb - 1][1];
    iv.tween = TW_DOWN;
    unequip(id);   /* (its place in the row: gone as it moves) */
    return;
  }
  /* Slot Open?: a notch left to try it in */
  if (g_pd.charm_slots_filled >= g_pd.charm_slots) return;
  int bb = bb_of_charm(id);
  iv.tw_from[0] = ch_bb_pos[bb - 1][0], iv.tw_from[1] = ch_bb_pos[bb - 1][1];
  eq_pos(equipped_count() + 1, &x, &y);
  iv.tw_to[0] = x, iv.tw_to[1] = y;
  iv.tween = TW_UP;
}

/* the charm's move over: equipped (or overcharmed, or not: back), unequipped */
static void tween_done(void) {
  int id = iv.tw_charm;
  if (iv.tween == TW_UP) {
    /* Check Points */
    g_pd.charm_slots_filled = (uint8_t)(g_pd.charm_slots_filled + costs[id]);
    if (g_pd.charm_slots_filled > g_pd.charm_slots) {
      /* Overcharm Check: the fifth try breaks through (canOvercharm after) */
      if (!pd_flag(PDF_CAN_OVERCHARM) && ++iv.oc_attempts < 5) {
        iv.tween = TW_FAIL, iv.tw_t = 0;   /* (Fail: 0.5 s shaking, then back) */
        return;
      }
      pd_set_flag(PDF_CAN_OVERCHARM, true);
      pd_set_flag(PDF_OVERCHARMED, true);
    }
    equip(id);
    iv.tween = TW_NONE;
  } else if (iv.tween == TW_FAIL) {
    /* Fail Back */
    g_pd.charm_slots_filled = (uint8_t)(g_pd.charm_slots_filled - costs[id]);
    float t0 = iv.tw_from[0], t1 = iv.tw_from[1];
    iv.tw_from[0] = iv.tw_to[0], iv.tw_from[1] = iv.tw_to[1], iv.tw_to[0] = t0, iv.tw_to[1] = t1;
    iv.tween = TW_FAIL_BACK, iv.tw_t = 0;
    return;
  } else if (iv.tween == TW_DOWN) {
    /* Return Points, End Overcharm? */
    g_pd.charm_slots_filled = (uint8_t)(g_pd.charm_slots_filled - costs[id]);
    if (g_pd.charm_slots_filled <= g_pd.charm_slots) pd_set_flag(PDF_OVERCHARMED, false);
    iv.tween = TW_NONE;
  } else {
    iv.tween = TW_NONE;
  }
  /* (CharmUpdate: the Knight with them; the cursor where it was) */
  hero_charm_update();
  if (iv.area == CA_EQUIPPED && iv.eq > equipped_count() + (g_pd.charm_slots_filled < g_pd.charm_slots))
    iv.eq = (uint8_t)(equipped_count() + (g_pd.charm_slots_filled < g_pd.charm_slots));
  if (iv.area == CA_EQUIPPED && iv.eq < 1) iv.area = CA_COLLECTION;
  charms_cursor();
}

/* (Home as it is open: a charm on its way put in its place) -> whether the game may be saved, as with it closed */
bool inv_settle(void) {
  if (iv.st != IV_OPEN) return false;
  while (iv.tween != TW_NONE) tween_done();
  return true;
}

static void charms_move(uint32_t p) {
  if (iv.tween != TW_NONE) return;
  int eq_items = equipped_count() + (g_pd.charm_slots_filled < g_pd.charm_slots);
  if (iv.area == CA_COLLECTION) {
    if (p & K_DOWN) {
      if (iv.coll <= 30) iv.coll += 10;
    } else if (p & K_UP) {
      if (iv.coll < 11) {
        if (eq_items) iv.area = CA_EQUIPPED, iv.eq = 1;   /* (To Equipment) */
      } else
        iv.coll -= 10;
    } else if (p & K_LEFT) {
      if ((iv.coll - 1) % 10 == 0) {
        if (other_panes()) iv.area = CA_ARROW_L;
      } else
        iv.coll--;
    } else if (p & K_RIGHT) {
      if (iv.coll % 10 == 0) {
        if (other_panes()) iv.area = CA_ARROW_R;
      } else
        iv.coll++;
    }
  } else if (iv.area == CA_EQUIPPED) {
    if (p & K_DOWN) iv.area = CA_COLLECTION, iv.coll = 1;   /* (To Bot) */
    else if (p & K_LEFT) {
      if (iv.eq <= 1) {
        if (other_panes()) iv.area = CA_ARROW_L;
      } else
        iv.eq--;
    } else if ((p & K_RIGHT) && iv.eq < eq_items)
      iv.eq++;
  } else if (iv.area == CA_ARROW_L) {
    if (p & K_RIGHT) iv.area = CA_COLLECTION;
    else if (p & K_LEFT) {
      move_pane(-1);
      return;
    }
  } else {
    if (p & K_LEFT) iv.area = CA_COLLECTION;
    else if (p & K_RIGHT) {
      move_pane(1);
      return;
    }
  }
  charms_cursor();
}

/* ---------------------------------------------------------------- the Inventory pane's input */
static void inv_move(uint32_t p) {
  if (iv.sel < 0) {
    /* (the arrows: on, or back in) */
    bool left = iv.sel == -1;
    if ((left && (p & K_LEFT)) || (!left && (p & K_RIGHT))) {
      move_pane(left ? -1 : 1);
      return;
    }
    if ((left && (p & K_RIGHT)) || (!left && (p & K_LEFT))) {
      /* (back to the nearest item) */
      Items l;
      build_items(&l);
      float ax = left ? arrow_l.x : arrow_r.x;
      int best = 0;
      for (int i = 1; i < l.n; i++)
        if (fabsf(l.xy[i][0] - ax) < fabsf(l.xy[best][0] - ax)) best = i;
      iv.sel = (int8_t)best;
      inv_cursor();
    }
    return;
  }
  /* the nearest item that way (by how far it is, along more than across) */
  float dx = (p & K_RIGHT) ? 1 : (p & K_LEFT) ? -1 : 0, dy = (p & K_UP) ? 1 : (p & K_DOWN) ? -1 : 0;
  if (!dx && !dy) return;
  Items l;
  build_items(&l);
  float x0 = l.xy[iv.sel][0], y0 = l.xy[iv.sel][1];
  int best = -1;
  float bd = 1e9f;
  for (int i = 0; i < l.n; i++) {
    if (i == iv.sel) continue;
    float ex = l.xy[i][0] - x0, ey = l.xy[i][1] - y0;
    float along = ex * dx + ey * dy, across = fabsf(ex * dy - ey * dx);
    if (along < 0.3f || across > along * 2.5f) continue;
    float dist = along + across * 2;
    if (dist < bd) bd = dist, best = i;
  }
  if (best >= 0) iv.sel = (int8_t)best;
  else if (dx && other_panes()) iv.sel = (int8_t)(dx < 0 ? -1 : -2);
  else return;
  inv_cursor();
}

/* ---------------------------------------------------------------- each tick */
uint32_t inv_tick(uint32_t keys) {
  uint32_t pressed = keys & ~iv.prev_keys;
  iv.prev_keys = keys;
  if (iv.st == IV_CLOSED) {
    g_gfx_overlay = NULL, g_gfx_reserve = 0;
    iv.blocked &= keys;
    if ((pressed & K_INV) && can_open()) {
      open(false);
      iv.prev_keys = keys;
      return 0;
    }
    return keys & ~iv.blocked;
  }
  extern void inv_draw(void);
  g_gfx_overlay = inv_draw, g_gfx_reserve = RESERVE;
  if (iv.st == IV_CLOSING) {
    iv.t += DT;
    iv.border_a = iv.pane_a = 1 - iv.t / FADE;
    if (iv.t >= FADE) {
      close_now();
      iv.border_a = iv.pane_a = 0;
    }
    return 0;
  }
  /* fades up; the panes' move; the cursor's; a charm's */
  if (iv.border_a < 1) iv.border_a = fminf(1, iv.border_a + DT / FADE);
  if (iv.pane_a < 1) iv.pane_a = fminf(1, iv.pane_a + DT / FADE);
  if (iv.slide_t >= 0 && (iv.slide_t += DT) >= SLIDE) iv.slide_t = -1;
  if (iv.ct < CURSOR_TIME) {
    iv.ct += DT;
    float q = iv.ct >= CURSOR_TIME ? 1 : iv.ct / CURSOR_TIME;
    for (int k = 0; k < 4; k++) iv.cur[k] = iv.cfrom[k] + (iv.cto[k] - iv.cfrom[k]) * q;
  }
  if (iv.tween != TW_NONE) {
    iv.tw_t += DT;
    if (iv.tw_t >= (iv.tween == TW_FAIL ? 0.5f : TWEEN_TIME)) tween_done();
  }
  /* closing: the inventory key, or back (not as the map is zoomed into: Do Not Close) */
  if ((pressed & (K_INV | K_BACK)) && iv.tween == TW_NONE && !(iv.pane == P_MAP && map_pane_holds())) {
    start_close(0);
    return 0;
  }
  if (iv.slide_t >= 0) return 0;
  /* (directions: held, again after a moment, then often) */
  uint32_t dirs = keys & (K_LEFT | K_RIGHT | K_UP | K_DOWN);
  uint32_t move = pressed & dirs;
  if (dirs && dirs == iv.held) {
    iv.held_t += DT;
    if (iv.held_t >= REPEAT_FIRST) move = dirs, iv.held_t -= REPEAT;
  } else
    iv.held = dirs, iv.held_t = 0;
  if (iv.pane == P_MAP) {
    int r = map_pane_tick(keys, pressed, move, move && !(pressed & dirs));
    if (r == MAP_PANE_LEFT || r == MAP_PANE_RIGHT) move_pane(r == MAP_PANE_LEFT ? -1 : 1);
    else if (r == MAP_PANE_CLOSE) start_close(0);
  } else if (iv.pane == P_CHARMS) {
    if (move) charms_move(move);
    if (pressed & K_OK) charms_confirm();
  } else {
    if (move) inv_move(move);
    if ((pressed & K_OK) && iv.sel < 0) move_pane(iv.sel == -1 ? -1 : 1);
  }
  return 0;
}

/* ---------------------------------------------------------------- drawing */
void ui_piece(const Piece *p, float ox, float oy, float k, uint8_t tint) {
  if (p->sprite < 0) return;
  Inst in;
  sprite_inst(p->sprite, ox + p->x, oy + p->y, 0, p->kx * k, p->ky * k, tint, &in);
  gfx_overlay(&in);
}

void ui_sprite_at(int sprite, float x, float y, float k, uint8_t tint) {
  if (sprite < 0) return;
  Inst in;
  sprite_inst(sprite, x, y, 0, k, k, tint, &in);
  gfx_overlay(&in);
}

/* a text's lines (each top aligned at its place: 0 left, 1 center, 2 right) */
void ui_text_at(int text, int style, const TextAt *t, float ox, float oy, int align, float a) {
  text_box(text, style, ox + t->x, oy + t->top, 1e3f, align, a);
}

static void digits(char *out, int v) {
  char b[12];
  int n = 0;
  if (v < 0) v = 0;
  do b[n++] = (char)('0' + v % 10), v /= 10;
  while (v && n < 11);
  for (int i = 0; i < n; i++) out[i] = b[n - 1 - i];
  out[n] = 0;
}

void ui_number_at(int v, int style, float x, float top, int align, float a) {
  /* (kept until the frame is drawn: the text runs point at them) */
  static char pool[8][12];
  static int next;
  char *s = pool[next++ & 7];
  digits(s, v);
  const uint8_t *st = font_style(style);
  int n = (int)strlen(s);
  float w = text_width(style, (const uint8_t *)s, n);
  float px = VIEW_W / 2 + x * HUD_PX - (align == 1 ? w / 2 : align == 2 ? w : 0);
  gfx_text(style, px, VIEW_H / 2 - top * HUD_PX + font_asc(st), (const uint8_t *)s, n, (uint32_t)(a * 255 + 0.5f) << 24 | 0xFFFFFF, 0);
}

/* an action's key (ActionButtonIcon: the calculator's key, by name, in a box as wide as it) at (x, y) -> its half width */
float ui_key(int name, float x, float y, uint8_t white, float a) {
  const uint8_t *st = font_style(STYLE_MSG_S), *s = text_get(name);
  int n = (int)strlen((const char *)s);
  float w = text_width(STYLE_MSG_S, s, n), bw = w / HUD_PX + 0.3f;
  Inst in;
  sprite_inst(SPRITE_MSG_KEY, x, y, 0, bw / 0.95f, 0.75f, white, &in);
  gfx_overlay(&in);
  gfx_text(STYLE_MSG_S, VIEW_W / 2 + x * HUD_PX - w / 2, VIEW_H / 2 - y * HUD_PX + (font_asc(st) - font_desc(st)) / 2, s, n,
           (uint32_t)(a * 255 + 0.5f) << 24 | 0xFFFFFF, 0);
  return bw / 2;
}

static void cursor_draw(float ox, float oy, uint8_t tint) {
  /* (its corners, about its box: TL, TR, BL, BR) */
  static const int8_t sx[4] = {-1, 1, -1, 1}, sy[4] = {1, 1, -1, -1};
  for (int k = 0; k < 4; k++) {
    Inst in;
    sprite_inst(ch_cursor.sprite, ox + iv.cur[0] + sx[k] * iv.cur[2] * ch_cursor_k[0],
                oy + iv.cur[1] + sy[k] * iv.cur[3] * ch_cursor_k[1], 0, ch_cursor.kx * ch_corners[k][0],
                ch_cursor.ky * ch_corners[k][1], tint, &in);
    gfx_overlay(&in);
  }
}

static void charm_select_text(int *name, int *desc, int *cost, int *detail) {
  *name = *desc = -1, *cost = 0, *detail = 0;
  int id = 0;
  if (iv.area == CA_COLLECTION) id = ch_bb_charm[iv.coll - 1];
  else if (iv.area == CA_EQUIPPED) {
    if (iv.eq <= equipped_count()) id = g_pd.equipped[iv.eq - 1];
    else *desc = equipped_count() ? -1 : pd_flag(PDF_AT_BENCH) ? TXT_CHARM_ALERT_NONE_BENCH : TXT_CHARM_ALERT_NONE;
  }
  if (!charm_got(id)) return;
  *name = ch_names[id], *desc = ch_descs[id], *cost = costs[id], *detail = id;
}

static void charms_draw(float ox, float oy, float a, uint8_t white) {
  uint8_t glow = gfx_dyn_tint(24, 255, 255, 255, (uint8_t)(a * CH_GLOW_A * 255 + 0.5f));
  uint8_t full = gfx_dyn_tint(25, notch_full_rgb[0], notch_full_rgb[1], notch_full_rgb[2], (uint8_t)(a * 255 + 0.5f));
  uint8_t over = gfx_dyn_tint(26, notch_over_rgb[0], notch_over_rgb[1], notch_over_rgb[2], (uint8_t)(a * 255 + 0.5f));
  /* the collection: the backboards, the charms the Knight has (glowing; not those equipped, nor the one moving) */
  for (int i = 0; i < 40; i++) {
    float x = ox + ch_bb_pos[i][0], y = oy + ch_bb_pos[i][1];
    ui_sprite_at(ch_bb.sprite, x, y, ch_bb.kx, white);
    int id = ch_bb_charm[i];
    if (!charm_got(id) || charm_equipped(id) || (iv.tween != TW_NONE && iv.tw_charm == id)) continue;
    ui_piece(&ch_glow, x, y, 1, glow);
    ui_sprite_at(ch_icons[id], x, y, 1 / CHARM_ICON_K, white);
  }
  /* the equipped row, the next dot */
  int n = equipped_count();
  for (int i = 1; i <= n; i++) {
    float x, y;
    eq_pos(i, &x, &y);
    if (!(iv.tween == TW_FAIL_BACK && g_pd.equipped[i - 1] == iv.tw_charm))
      ui_sprite_at(ch_icons[g_pd.equipped[i - 1]], ox + x, oy + y, EQ_SCALE / CHARM_ICON_K, white);
  }
  if (g_pd.charm_slots_filled < g_pd.charm_slots && !(iv.tween == TW_UP || iv.tween == TW_FAIL)) {
    float x, y;
    eq_pos(n + 1, &x, &y);
    ui_piece(&ch_next_dot, ox + x, oy + y, 1, white);
  }
  /* the notches: full as the charms take them; over them (overcharmed, or trying), red */
  int slots = g_pd.charm_slots, filled = g_pd.charm_slots_filled;
  for (int i = 0; i < slots || i < filled; i++) {
    float x = ox + CH_NOTCH_X + i * CH_NOTCH_DX, y = oy + CH_NOTCH_Y;
    bool over_on = filled > slots && (pd_flag(PDF_OVERCHARMED) || iv.tween == TW_FAIL);
    if (i < filled) ui_piece(&ch_notch_full, x, y, 1, over_on ? over : full);
    else ui_piece(&ch_notch_empty, x, y, 1, white);
  }
  ui_text_at(pd_flag(PDF_OVERCHARMED) ? TXT_CHARM_TXT_OVERCHARMED : TXT_CHARM_TXT_EQUIPPED, STYLE_TUTE, &ch_text_equipped,
          ox, oy, 0, a);
  ui_text_at(TXT_CHARM_NOTCHES, STYLE_MSG, &ch_text_notches, ox, oy, 0, a);
  ui_piece(&ch_divider, ox, oy, 1, white);
  /* the charm moving to its notch or back (shaking as it fails) */
  if (iv.tween != TW_NONE) {
    float q = iv.tween == TW_FAIL ? 1 : iv.tw_t / TWEEN_TIME;
    float e = sinf((q > 1 ? 1 : q) * 1.5707964f);
    float x = iv.tw_from[0] + (iv.tw_to[0] - iv.tw_from[0]) * e, y = iv.tw_from[1] + (iv.tw_to[1] - iv.tw_from[1]) * e;
    if (iv.tween == TW_FAIL) x += rand_range(-0.1f, 0.1f), y += rand_range(-0.1f, 0.1f);   /* (ObjectJitterRealtime) */
    ui_sprite_at(ch_icons[iv.tw_charm], ox + x, oy + y, EQ_SCALE / CHARM_ICON_K, white);
  }
  /* what is selected: its name, cost, picture, description; Equip or Unequip (at a bench) */
  int name, desc, cost, detail;
  charm_select_text(&name, &desc, &cost, &detail);
  ui_text_at(name, STYLE_DIALOGUE, &ch_name, ox, oy, 1, a);
  /* (its picture under its cost where its description still fits under that, in the message's size or the smaller;
   * else the description up in the picture's place) */
  float down = name_below(ch_name.top, ch_cost_text.top);   /* (all under the name lower by as much as it is bigger) */
  float bottom = detail ? DESC_BOTTOM_BENCH : DESC_BOTTOM, top = ch_desc.top - down;
  int style = text_box(desc, STYLE_MSG, 0, 0, ch_desc.w, 0, 0) <= top - bottom ? STYLE_MSG : STYLE_MSG_S;
  bool picture = !detail || text_box(desc, style, 0, 0, ch_desc.w, 0, 0) <= top - bottom;
  if (!picture) {
    const uint8_t *st = font_style(STYLE_MSG);
    top = ch_cost_text.top - down - (font_asc(st) + font_desc(st)) / HUD_PX - CH_DESC_GAP;
    style = text_fit(desc, ch_desc.w, top - bottom);
  }
  if (!detail) style = text_fit(desc, ch_desc.w, top - bottom);
  if (detail) {
    ui_text_at(TXT_CHARM_TXT_COST, STYLE_MSG, &ch_cost_text, ox, oy - down, 2, a);
    for (int i = 0; i < cost; i++) ui_piece(&ch_cost_pip, ox + CH_COST_X + i * CH_COST_DX, oy - down + CH_COST_Y, 1, white);
    if (picture)
      ui_sprite_at(ch_icons[detail], ox + CH_DETAIL_X, oy - down + CH_DETAIL_Y, DETAIL_SCALE / CHARM_ICON_K, white);
  }
  text_box(desc, style, ox + ch_desc.x, oy + top, ch_desc.w, 0, a);
  if (detail && pd_flag(PDF_AT_BENCH)) {
    /* (as the keys' actions elsewhere, the smaller message's size) */
    ui_text_at(charm_equipped(detail) ? TXT_CTRL_UNEQUIP : TXT_CTRL_EQUIP, STYLE_MSG_S, &ch_confirm, ox, oy, 2, a);
    /* (the key: the calculator's, by name; its box widened to the right to fit the name) */
    const uint8_t *st = font_style(STYLE_MSG_S);
    float w = text_width(STYLE_MSG_S, text_get(TXT_KEY_OK), 2), bw = w / HUD_PX + 0.3f;
    float kx = bw > CH_CONFIRM_KEY_W ? bw / CH_CONFIRM_KEY_W : 1, cx = ox + ch_key.x + (kx - 1) * CH_CONFIRM_KEY_W / 2;
    Inst in;
    sprite_inst(ch_key.sprite, cx, oy + ch_key.y, 0, kx, 1, white, &in);
    gfx_overlay(&in);
    gfx_text(STYLE_MSG_S, VIEW_W / 2 + cx * HUD_PX - w / 2,
             VIEW_H / 2 - (oy + ch_key.y) * HUD_PX + (font_asc(st) - font_desc(st)) / 2, text_get(TXT_KEY_OK), 2,
             (uint32_t)(a * 255 + 0.5f) << 24 | 0xFFFFFF, 0);
  } else if (detail) {
    /* (away from a bench, in their place: where charms are changed) */
    ui_text_at(TXT_CHARM_BENCH, STYLE_MSG_S, &ch_confirm, ox, oy, 2, a);
  }
  if (iv.cursor_set) cursor_draw(ox, oy, white);
}

static void items_draw(float ox, float oy, float a, uint8_t white) {
  Items l;
  build_items(&l);
  for (int i = 0; i < 2; i++) {
    const TurnedPiece *d = &inv_dividers[i];
    Inst in;
    sprite_inst_rot(d->sprite, ox + d->x, oy + d->y, 0, d->kx, d->ky, d->deg, white, &in);
    gfx_overlay(&in);
  }
  bool any_trinket = false;
  for (int i = 0; i < l.n; i++) any_trinket |= l.kind[i] >= K_TRINKET0;
  if (any_trinket) ui_piece(&inv_trinket_bb, ox, oy, 1, white);
  for (int i = 0; i < l.n; i++) {
    int k = l.kind[i];
    float x = ox + l.xy[i][0], y = oy + l.xy[i][1];
    if (k < NFIXED) {
      ui_piece(&inv_fixed_bg[k], ox, oy, 1, white);
      ui_piece(&inv_fixed[k], ox, oy, 1, white);
      if (k == IT_HEART && g_pd.heart_pieces > 0 && g_pd.heart_pieces <= 4) ui_piece(&inv_heart[g_pd.heart_pieces - 1], ox, oy, 1, white);
      if (k == IT_VESSEL && g_pd.vessel_fragments > 0) ui_piece(&inv_vessel[g_pd.vessel_fragments >= 3 ? 2 : g_pd.vessel_fragments - 1], ox, oy, 1, white);
      if (k == IT_GEO) ui_number_at(g_pd.geo, STYLE_MSG, ox + inv_geo_text.x, oy + inv_geo_text.top, 0, a);
    } else if (k < K_TRINKET0) {
      const Piece *p = &inv_eq[k - K_EQ0];
      ui_sprite_at(p->sprite, x, y, p->kx, white);
      int amount = k == K_EQ0 + EQ_SIMPLE_KEY ? g_pd.simple_keys : k == K_EQ0 + EQ_RANCID_EGG ? g_pd.rancid_eggs : 0;
      if (amount > 1) ui_number_at(amount, STYLE_MSG, x + inv_trinket_text.x, y + inv_trinket_text.top, 1, a);
    } else {
      const Piece *p = &inv_trinkets[k - K_TRINKET0];
      ui_sprite_at(p->sprite, x, y, p->kx, white);
      ui_number_at(g_pd.trinkets[k - K_TRINKET0], STYLE_MSG, x + inv_trinket_text.x, y + inv_trinket_text.top, 1, a);
    }
  }
  /* the selected item's name and description */
  if (iv.sel >= 0 && iv.sel < l.n) {
    int k = l.kind[iv.sel];
    NameDesc nd = {-1, -1};
    if (k == IT_HEART) {
      int h = g_pd.max_health >= 9 ? 5 : !pd_flag(PDF_HEART_PIECE_COLLECTED) ? 0 : 1 + (g_pd.heart_pieces > 3 ? 3 : g_pd.heart_pieces);
      nd = txt_heart[h];
    } else if (k == IT_VESSEL) {
      int v = g_pd.mp_reserve_max >= 99 ? 4 : !pd_flag(PDF_VESSEL_FRAGMENT_COLLECTED) ? 0 : 1 + (g_pd.vessel_fragments > 2 ? 2 : g_pd.vessel_fragments);
      nd = txt_vessel[v];
    } else if (k == IT_NAIL) nd = txt_fixed[0];
    else if (k == IT_FIREBALL) nd = txt_fixed[1];
    else if (k == IT_FOCUS) nd = txt_fixed[2];
    else if (k == IT_GEO) nd = txt_fixed[3];
    else if (k < K_TRINKET0) {
      nd = txt_eq[k - K_EQ0];
      if (k == K_EQ0 + EQ_MAP && pd_flag(PDF_HAS_QUILL))
        nd = pd_flag(PDF_HAS_MAP) ? (NameDesc){txt_quill[2], txt_quill[3]} : (NameDesc){txt_quill[0], txt_quill[1]};
    } else
      nd = txt_trinkets[k - K_TRINKET0];
    ui_text_at(nd.name, STYLE_DIALOGUE, &inv_name, ox, oy, 1, a);
    float top = inv_desc.top - name_below(inv_name.top, inv_desc.top);
    text_box(nd.desc, text_fit(nd.desc, inv_desc.w, top - DESC_BOTTOM), ox + inv_desc.x, oy + top, inv_desc.w, 0, a);
  }
  if (iv.cursor_set) cursor_draw(ox, oy, white);
}

static void pane_draw(int p, float a, uint8_t white) {
  float ox = INV_X + pane_x(p), oy = INV_Y;
  if (p == P_CHARMS) charms_draw(ox, oy, a, white);
  else if (p == P_MAP) {
    map_pane_draw(ox, oy, a, white, p == iv.pane);
    if (iv.cursor_set && p == iv.pane) cursor_draw(ox, oy, white);
  } else
    items_draw(ox, oy, a, white);
}

void inv_draw(void) {
  if (iv.st == IV_CLOSED && iv.border_a <= 0) return;
  float ba = iv.border_a < 0 ? 0 : iv.border_a, pa = iv.pane_a < 0 ? 0 : iv.pane_a;
  /* the world behind, darkened; the dark frame about it; the border; the arrows; the panes' names */
  gfx_overlay_fill(-20, -12, 20, 12, gfx_dyn_tint(29, 0, 0, 0, (uint8_t)(ba * DIM * 255 + 0.5f)));
  uint8_t bw = gfx_dyn_tint(30, 255, 255, 255, (uint8_t)(ba * 255 + 0.5f));
  ui_piece(&frame, INV_X, INV_Y, 1, bw);
  for (int i = 0; i < INV_NBORDER; i++) ui_piece(&border[i], INV_X, INV_Y, 1, bw);
  int l = neighbor_pane(-1), r = neighbor_pane(1);
  /* (the arrows and the side panes' names: away as the map is zoomed into, World Map's FadeGroup) */
  float na = iv.pane == P_MAP && iv.slide_t < 0 ? ba * map_pane_nav() : ba;
  uint8_t nw = gfx_dyn_tint(30, 255, 255, 255, (uint8_t)(na * 255 + 0.5f));
  if (l >= 0) ui_piece(&arrow_l, INV_X, INV_Y, 1, nw), ui_piece(&pane_arrow_l, INV_X, INV_Y, 1, nw);
  if (r >= 0) ui_piece(&arrow_r, INV_X, INV_Y, 1, nw), ui_piece(&pane_arrow_r, INV_X, INV_Y, 1, nw);
  ui_text_at(pane_names[iv.pane], STYLE_TUTE, &pane_name, INV_X, INV_Y, 1, ba);
  if (l >= 0) ui_text_at(pane_names[l], STYLE_TUTE, &pane_name_l, INV_X, INV_Y, 0, na);
  if (r >= 0) ui_text_at(pane_names[r], STYLE_TUTE, &pane_name_r, INV_X, INV_Y, 2, na);
  /* the panes (the one going too, as they move) */
  uint8_t pw = gfx_dyn_tint(31, 255, 255, 255, (uint8_t)(pa * 255 + 0.5f));
  if (iv.slide_t >= 0) pane_draw(iv.prev_pane, pa, pw);
  pane_draw(iv.pane, pa, pw);
}
