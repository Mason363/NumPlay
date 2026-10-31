/* The shops' menu (Shop Menu: shop_control, Item List Control, ui_list_getinput; its confirmation, ui_list and Confirm
 * Control; what is sold, ShopMenuStock and ShopItemStats). The shop's region (a script) takes the Knight's control and
 * sends SHOP UP; nothing left to sell, the shopkeeper says so; else the window opens: the list of what is left on its
 * left (each 1.5 below the last, at the selector the one selected), its name and description on the right, then a
 * confirmation; what is bought is paid and given; the window closes on its key (SHOP CLOSED to the region). Places in
 * HUD units from the screen's center (tools/shop.py). */
#pragma GCC optimize("Os")   /* (its code small: not where a frame's time goes) */
#include <math.h>
#include "game.h"

#define DT 0.02f
#define RESERVE 140   /* (the frame's instances kept for it) */
#define HUD_PX (FOCAL / (-1.342f - CAM_Z))

typedef struct {
  float x, y, kx, ky;
  int16_t up, down;   /* (InvAnimateUpAndDown: its clips, its delay up) */
  float delay;
} AnimPiece;
typedef struct {
  int16_t sprite;
  float k;                 /* (its sprite, as the confirmation shows it; the row's, k of that) */
  int16_t name, desc, cost;
  uint8_t type;            /* ShopItemStats.specialType: ST_* */
  int16_t flag, req;       /* (its PlayerData bool, set as it is bought; the one it needs, -1 none) */
  uint8_t charms, charm;   /* (the charms it needs; the charm it is) */
} Item;
enum { ST_NONE, ST_HEART, ST_CHARM, ST_VESSEL, ST_NOTCH = 8, ST_MAP, ST_KEY, ST_EGG, ST_PIN = 16, ST_MARKER };

static const UiPiece backboard = SHOP_BACKBOARD, mask_top = SHOP_MASK_TOP, mask_bottom = SHOP_MASK_BOTTOM,
                     arrow_u = SHOP_ARROW_U, arrow_d = SHOP_ARROW_D, selector = SHOP_SELECTOR, confirm_geo = SHOP_CONFIRM_GEO,
                     pointer_l = SHOP_POINTER_L, pointer_r = SHOP_POINTER_R, peg = SHOP_PEG, row_geo = SHOP_ROW_GEO;
static const AnimPiece borders[2] = SHOP_BORDERS, figureheads[SHOP_N] = SHOP_FIGUREHEADS;
static const UiText t_name = SHOP_T_NAME, t_desc = SHOP_T_DESC, t_notch = SHOP_T_NOTCH_TEXT, t_cname = SHOP_T_CONFIRM_NAME,
                    t_ccost = SHOP_T_CONFIRM_COST, t_cmsg = SHOP_T_CONFIRM_MSG, t_yes = SHOP_T_YES, t_no = SHOP_T_NO,
                    t_thanks = SHOP_T_THANKS, t_act_confirm = SHOP_T_ACT_CONFIRM, t_act_cancel = SHOP_T_ACT_CANCEL;
static const float pegs_x[6] = SHOP_PEGS_X, key_confirm[2] = SHOP_KEY_CONFIRM, key_cancel[2] = SHOP_KEY_CANCEL,
                   row_cost[2] = SHOP_ROW_COST, confirm_item[2] = SHOP_CONFIRM_ITEM;
static const uint8_t mask_a[] = SHOP_MASK_A;
static const Item items[] = SHOP_ITEMS;
static const uint8_t first[SHOP_N + 1] = SHOP_FIRST, active_rgb[3] = SHOP_ACTIVE_RGB, inactive_rgb[3] = SHOP_INACTIVE_RGB;
static const char *const rooms[SHOP_N] = SHOP_ROOMS;
static const int16_t nostock[SHOP_N] = SHOP_NOSTOCK, titles[SHOP_N] = SHOP_TITLES;
#define MAX_ROWS 16
#define SHOP_KEY_GAP 0.25f   /* (between a key and its action) */
#define SHOP_KEY_OUT 0.3f    /* (the keys a little further out than the game's: their actions' text is bigger) */
#define SHOP_PEG_BELOW 0.09f /* (a peg's center under the bottom of Notch Cost's text) */

enum {
  SH_CLOSED, SH_TALK_UP, SH_TALK, SH_TALK_DOWN,           /* (nothing left: Box Up, its conversation, Box Down) */
  SH_WINDOW, SH_LIST_UP, SH_LIST_ON, SH_LIST,              /* Open Window, Item List Up, Activate Item List, Open */
  SH_MENU_DOWN, SH_CONFIRM_UP, SH_CONFIRM, SH_CHOSEN,      /* Menu Down, Activate confirm, the confirmation */
  SH_BUY, SH_BOB, SH_THANKS, SH_THANK_FADE, SH_NO, SH_DOWN /* Yes .. Thank Fade; No; Down (closing) */
};
static struct {
  uint8_t st, shop, n, cur, yes;   /* yes: the confirmation's choice, 1 yes, 2 no */
  uint8_t rows[MAX_ROWS];          /* what is listed: items' ranks */
  bool buy_yes;                    /* (the choice made) */
  float t;                         /* in the state */
  /* the list's place: tweened (iTweenMoveTo, 0.25 s, ease out; a shake past its ends) */
  float list_y, from_y, to_y, tw_t, tw_time, bump;
  /* the rows' shift (Shift_pos: those above the current one 0.8 up, those below 0.8 down; SHIFT, a 0.25 s tween
   * from where they are, ease out; SHIFT INSTANT) */
  float shift_from[MAX_ROWS], shift_t;
  int8_t shift_to[MAX_ROWS];
  /* FadeGroups: the window's (0.2 s), the list's (Shop Menu's, 0.1 s), the confirmation's (0.1 s): their alphas */
  float win_a, root_a, conf_a, thanks_a;
  int8_t win_d, root_d, conf_d, thanks_d;
  Anim border[2], fig;             /* (InvAnimateUpAndDown: up as the window comes, down as it goes) */
  float fig_wait;
  bool fig_down, fig_shown, border_shown;
  float arrow_t[2];                /* (Arrow Anim: the up and down arrows' bob, from a move) */
  float jitter, chosen_t, bob_t;
  float rep;                       /* (ui_list_getinput: held, a move again after 0.25 s, then each 0.15 s) */
  uint32_t prev;
} sh;

bool shop_open(void) { return sh.st != SH_CLOSED; }
void shop_reset(void) { memset(&sh, 0, sizeof sh); }

/* the shop this room has */
static int room_shop(void) {
  for (int i = 0; i < SHOP_N; i++)
    if (!strcmp(room_name(g_room.id), rooms[i])) return i;
  return -1;
}

/* ShopMenuStock: what is left (bought: its bool; needing one not set) */
static bool for_sale(const Item *it) { return !pd_flag(it->flag) && (it->req < 0 || pd_flag(it->req)); }
static void build_list(void) {
  sh.n = 0;
  for (int i = first[sh.shop]; i < first[sh.shop + 1] && sh.n < MAX_ROWS; i++)
    if (for_sale(&items[i])) sh.rows[sh.n++] = (uint8_t)i;
}
static const Item *cur_item(void) { return &items[sh.rows[sh.cur < sh.n ? sh.cur : 0]]; }
/* CanBuy: the geo for it, and the charms */
static bool can_buy(const Item *it) { return g_pd.geo >= it->cost && g_pd.charms_owned >= it->charms; }

/* the list's place for its current item (the item at the selector) */
static float list_at(int cur) { return SHOP_LIST_Y + cur * SHOP_ROW_DY; }
static void list_tween(float to, float time) { sh.from_y = sh.list_y, sh.to_y = to, sh.tw_t = 0, sh.tw_time = time; }
static float shift_at(int i) {
  float q = sh.shift_t >= 0.25f ? 1 : sinf(sh.shift_t / 0.25f * 1.5707964f);
  return sh.shift_from[i] + (sh.shift_to[i] * 0.8f - sh.shift_from[i]) * q;
}
/* (SHIFT, SHIFT INSTANT: to the current one's) */
static void shift(bool instant) {
  for (int i = 0; i < sh.n; i++) {
    int8_t to = (int8_t)(i < sh.cur ? 1 : i > sh.cur ? -1 : 0);
    sh.shift_from[i] = instant ? to * 0.8f : shift_at(i);
    sh.shift_to[i] = to;
  }
  sh.shift_t = instant ? 0.25f : 0;
}

static void fade(float *a, int8_t d, float time) {
  if (d > 0) *a = *a + DT / time > 1 ? 1 : *a + DT / time;
  else if (d < 0) *a = *a - DT / time < 0 ? 0 : *a - DT / time;
}

/* the window: FadeGroupUp (its pieces, the borders' and the figurehead's clips up), FadeGroupDown */
static void window(bool up) {
  sh.win_d = up ? 1 : -1;
  for (int i = 0; i < 2; i++) anim_play_from_frame(&sh.border[i], up ? borders[i].up : borders[i].down, 0);
  sh.border_shown = true;
  if (up) sh.fig_wait = figureheads[sh.shop].delay, sh.fig_shown = false, sh.fig_down = false;
  else anim_play_from_frame(&sh.fig, figureheads[sh.shop].down, 0), sh.fig_down = true, sh.fig_wait = 0;
}

static void to(int st) { sh.st = (uint8_t)st, sh.t = 0; }

void shop_event(int ev) {
  if (ev != VMEV_SHOP_UP || sh.st != SH_CLOSED) return;
  int s = room_shop();
  if (s < 0) return;
  memset(&sh, 0, sizeof sh);
  sh.shop = (uint8_t)s;
  build_list();
  if (!sh.n) {
    /* (Stock?: nothing left; Box Up, Choose Convo) */
    dialogue_box_up();
    to(SH_TALK_UP);
    return;
  }
  /* (Open Window: its FadeGroup up; SHOP WINDOW UP) */
  window(true);
  vm_broadcast(VMEV_SHOP_WINDOW_UP);
  to(SH_WINDOW);
  g_gfx_overlay = NULL;
}

/* a purchase: paid (Deduct Geo and set PD), then given as its kind is (Special Type?) */
static void pay(const Item *it) {
  pd_set_flag(it->flag, true);
  g_pd.geo -= it->cost;
  if (g_pd.geo < 0) g_pd.geo = 0;
}

/* -> whether the shop's window closes for it (Close Shop Window) */
static bool give(const Item *it) {
  switch (it->type) {
    case ST_HEART:
    case ST_VESSEL:
      /* (Heart Piece, Vessel Fragment: its Instant made, the window closed) */
      collect_start(it->type == ST_VESSEL);
      return true;
    case ST_CHARM:
      /* (Charm: one more; the first, the charms' lesson) */
      g_pd.charms_owned++;
      if (!pd_flag(PDF_HAS_CHARM)) {
        pd_set_flag(PDF_HAS_CHARM, true);
        charm_tute_input();
        return true;
      }
      return false;
    case ST_NOTCH:
      /* (Notch Up; RefreshOvercharm) */
      g_pd.charm_slots++;
      if (pd_flag(PDF_OVERCHARMED) && g_pd.charm_slots_filled <= g_pd.charm_slots) pd_set_flag(PDF_OVERCHARMED, false);
      return false;
    case ST_MAP: pd_set_flag(PDF_HAS_MAP, true); return false;
    case ST_KEY: g_pd.simple_keys++; return false;
    case ST_EGG: g_pd.rancid_eggs++; return false;
    case ST_PIN: pd_set_flag(PDF_HAS_PIN, true); return false;
    case ST_MARKER: pd_set_flag(PDF_HAS_MARKER, true); return false;
    default: return false;
  }
}

static void shop_draw(void);

uint32_t shop_tick(uint32_t keys) {
  if (sh.st == SH_CLOSED) return keys;
  uint32_t pressed = keys & ~sh.prev;
  sh.prev = keys;
  sh.t += DT;
  /* the fades, the clips, the tweens */
  fade(&sh.win_a, sh.win_d, 0.2f);
  fade(&sh.root_a, sh.root_d, 0.1f);
  fade(&sh.conf_a, sh.conf_d, 0.1f);
  fade(&sh.thanks_a, sh.thanks_d, 0.1f);
  for (int i = 0; i < 2; i++) sh.border[i].events = 0, anim_update(&sh.border[i], DT);
  if (sh.fig_wait > 0 && (sh.fig_wait -= DT) <= 0) anim_play_from_frame(&sh.fig, figureheads[sh.shop].up, 0), sh.fig_shown = true;
  else if (!sh.fig_shown && !sh.fig_down && sh.fig_wait <= 0 && sh.win_d > 0)
    anim_play_from_frame(&sh.fig, figureheads[sh.shop].up, 0), sh.fig_shown = true;
  sh.fig.events = 0;
  anim_update(&sh.fig, DT);
  if (sh.tw_t < sh.tw_time) {
    sh.tw_t += DT;
    float q = sh.tw_t >= sh.tw_time ? 1 : sinf(sh.tw_t / sh.tw_time * 1.5707964f);
    sh.list_y = sh.from_y + (sh.to_y - sh.from_y) * q;
  }
  if (sh.shift_t < 0.25f) sh.shift_t += DT;
  for (int i = 0; i < 2; i++)
    if (sh.arrow_t[i] > 0) sh.arrow_t[i] -= DT;
  if (sh.jitter > 0) sh.jitter -= DT;
  if (sh.bob_t > 0) sh.bob_t -= DT;
  bool up = false, down = false;
  if (keys & (K_UP | K_DOWN)) {
    /* (a move as it is pressed; held, again after 0.25 s, then each 0.15 s) */
    if (pressed & (K_UP | K_DOWN)) sh.rep = 0.25f, up = pressed & K_UP, down = !up && (pressed & K_DOWN);
    else if ((sh.rep -= DT) <= 0) sh.rep = 0.15f, up = keys & K_UP, down = !up && (keys & K_DOWN);
  }
  bool ok = pressed & K_OK, back = pressed & K_BACK;
  const Item *it = cur_item();
  switch (sh.st) {
    case SH_TALK_UP:
      if (sh.t >= 0.3f) {
        title_show(titles[sh.shop], TF_NPC);
        dialogue_start(nostock[sh.shop]);
        to(SH_TALK);
      }
      return keys;
    case SH_TALK:
      if (dialogue_finished()) {
        title_npc_down();
        dialogue_box_down();
        to(SH_TALK_DOWN);
      }
      return keys;
    case SH_TALK_DOWN:
      /* (End, Send Event: SHOP CLOSED) */
      vm_broadcast(VMEV_SHOP_CLOSED);
      to(SH_CLOSED);
      return keys;
    case SH_WINDOW:
      if (sh.t >= 0.15f) {
        /* (Item List Up: the arrows; then Activate Item List: the list where it was, its details; the list's group up) */
        sh.cur = 0, sh.list_y = sh.to_y = list_at(0), sh.tw_time = 0;
        shift(true);
        to(SH_LIST_UP);
      }
      break;
    case SH_LIST_UP:
      if (sh.t >= 0.1f) sh.root_d = 1, to(SH_LIST_ON);
      break;
    case SH_LIST_ON:
      if (sh.t >= 0.2f) to(SH_LIST);
      break;
    case SH_LIST:
      if (back) {
        /* (CLOSE: Down) */
        window(false);
        sh.root_d = -1;
        to(SH_DOWN);
        break;
      }
      if (up || down) {
        int c = sh.cur + (down ? 1 : -1);
        sh.arrow_t[down ? 1 : 0] = 0.15f;
        if (c < 0 || c >= sh.n) {
          /* (Fail Up, Fail Down: a shake past the end; not again as it is held) */
          if (sh.rep == 0.15f) break;
          list_tween(list_at(sh.cur) + (down ? 0.25f : -0.25f), 0.1f);
          sh.bump = list_at(sh.cur);
        } else {
          sh.cur = (uint8_t)c;
          list_tween(list_at(c), 0.25f);
          shift(false);
        }
        break;
      }
      if (sh.bump != 0 && sh.tw_t >= sh.tw_time) list_tween(sh.bump, 0.25f), sh.bump = 0;
      if (ok) {
        if (!can_buy(it)) {
          /* (Can't Buy: the selector shakes) */
          sh.jitter = 0.25f;
          break;
        }
        /* (Menu Down: the list's group down; In Second Menu) */
        sh.root_d = -1;
        to(SH_MENU_DOWN);
      }
      break;
    case SH_MENU_DOWN:
      if (sh.t >= 0.1f) sh.conf_d = 1, to(SH_CONFIRM_UP);
      break;
    case SH_CONFIRM_UP:
      if (sh.t >= 0.1f) sh.yes = 1, to(SH_CONFIRM);
      break;
    case SH_CONFIRM:
      if (up || down) sh.yes = sh.yes == 1 ? 2 : 1;
      else if (ok || back) {
        /* (Selection Made: the pointers move out and back; UI SELECTION MADE after 0.5 s; cancel, No) */
        if (back) sh.yes = 2;
        sh.buy_yes = sh.yes == 1;
        sh.chosen_t = 0.15f;
        to(SH_CHOSEN);
      }
      break;
    case SH_CHOSEN:
      if (sh.chosen_t > 0) sh.chosen_t -= DT;
      if (sh.t >= 0.5f) {
        sh.conf_d = -1;
        to(sh.buy_yes ? SH_BUY : SH_NO);
      }
      break;
    case SH_NO:
      if (sh.t >= 0.1f) {
        /* (Reset: RESET SHOP WINDOW; Check Relics, Item List Up: the list again, where it was) */
        vm_broadcast(VMEV_RESET_SHOP_WINDOW);
        to(SH_LIST_UP);
      }
      break;
    case SH_BUY:
      if (sh.t >= 0.1f) {
        pay(it);
        /* (Bob: the window up and back, the figurehead's clip again; the list back at its first) */
        sh.bob_t = 0.4f;
        anim_play_from_frame(&sh.fig, figureheads[sh.shop].up, 0);
        to(SH_BOB);
      }
      break;
    case SH_BOB:
      if (sh.t >= 0.2f) sh.thanks_d = 1, to(SH_THANKS);
      break;
    case SH_THANKS:
      /* (Thankyou, then Particles: a second) */
      if (sh.t >= 1.1f) sh.thanks_d = -1, to(SH_THANK_FADE);
      break;
    case SH_THANK_FADE:
      if (sh.t >= 0.1f) {
        bool close = give(it);
        build_list();
        sh.cur = 0, sh.list_y = sh.to_y = list_at(0), sh.tw_time = 0;
        shift(true);
        if (close || !sh.n) {
          /* (CLOSE SHOP WINDOW: Special Close, fast; SHOP CLOSED QUICK; nothing left, the same, by Down) */
          window(false);
          sh.root_d = -1;
          vm_broadcast(close ? VMEV_SHOP_CLOSED_QUICK : VMEV_RESET_SHOP_WINDOW);
          if (close) {
            to(SH_CLOSED);
            g_gfx_overlay = NULL, g_gfx_reserve = 0;
            return 0;
          }
          to(SH_DOWN);
        } else {
          vm_broadcast(VMEV_RESET_SHOP_WINDOW);
          to(SH_LIST_UP);
        }
      }
      break;
    case SH_DOWN:
      if (sh.t >= 0.5f) {
        /* (Send Event: SHOP CLOSED) */
        vm_broadcast(VMEV_SHOP_CLOSED);
        to(SH_CLOSED);
        g_gfx_overlay = NULL, g_gfx_reserve = 0;
        return 0;
      }
      break;
  }
  g_gfx_overlay = shop_draw, g_gfx_reserve = RESERVE;
  return 0;
}

/* ---------------------------------------------------------------- drawing */
static void anim_piece(const AnimPiece *p, const Anim *an, uint8_t tint) {
  Inst in;
  sprite_inst(an->sprite, p->x, p->y, 0, p->kx, p->ky, tint, &in);
  gfx_overlay(&in);
}

/* an action's key (ActionButtonIcon: the calculator's, by name) -> its half width */
static float key_at(const float *at, int name, float a, uint8_t white) { return ui_key(name, at[0], at[1], white, a); }

/* the selected item's description: on the window's left, over a dark panel (in the window, at a size that reads, it
 * would not fit the calculator's screen); the message's size, smaller where that runs out of the panel (text_fit) */
#define DESC_X0 (-13.35f)   /* (its text's left, right, top; the panel's bottom: HUD units) */
#define DESC_X1 (-1.35f)
#define DESC_TOP 5.05f
#define DESC_BOTTOM (-8.4f)
#define DESC_PAD 0.4f
static void desc_draw(const Item *it, float bob, float a) {
  float w = DESC_X1 - DESC_X0;
  int style = text_fit(it->desc, w, DESC_TOP - DESC_BOTTOM);
  float h = text_box(it->desc, style, 0, 0, w, 0, 0);
  gfx_overlay_fill(DESC_X0 - DESC_PAD, DESC_TOP - h - DESC_PAD + bob, DESC_X1 + DESC_PAD, DESC_TOP + DESC_PAD + bob,
                   gfx_dyn_tint(27, 0, 0, 0, (uint8_t)(a * SHOP_BACKBOARD_A * 255 + 0.5f)));
  text_box(it->desc, style, DESC_X0, DESC_TOP + bob, w, 0, a);
}

static void shop_draw(void) {
  float bob = sh.bob_t > 0 ? (sh.bob_t > 0.2f ? (0.4f - sh.bob_t) / 0.2f : sinf(sh.bob_t / 0.2f * 1.5707964f)) * 0.5f : 0;
  uint8_t win = gfx_dyn_tint(29, 255, 255, 255, (uint8_t)(sh.win_a * 255 + 0.5f));
  uint8_t root = gfx_dyn_tint(30, 255, 255, 255, (uint8_t)(sh.root_a * 255 + 0.5f));
  uint8_t conf = gfx_dyn_tint(31, 255, 255, 255, (uint8_t)(sh.conf_a * 255 + 0.5f));
  uint8_t white = gfx_dyn_tint(24, 255, 255, 255, 255);
  /* the window: its backboard, its borders and figurehead (their clips), the masks over the list's ends */
  {
    Inst in;
    sprite_inst(backboard.sprite, backboard.x, backboard.y + bob, 0, backboard.kx, backboard.ky,
                gfx_dyn_tint(28, 255, 255, 255, (uint8_t)(sh.win_a * SHOP_BACKBOARD_A * 255 + 0.5f)), &in);
    gfx_overlay(&in);
  }
  /* the list: each row's item, geo and cost (dim if it cannot be bought), those within the window */
  if (sh.root_a > 0) {
    uint8_t act = gfx_dyn_tint(25, active_rgb[0], active_rgb[1], active_rgb[2], (uint8_t)(sh.root_a * 255 + 0.5f));
    uint8_t ina = gfx_dyn_tint(26, inactive_rgb[0], inactive_rgb[1], inactive_rgb[2], (uint8_t)(sh.root_a * 255 + 0.5f));
    for (int i = 0; i < sh.n; i++) {
      const Item *it = &items[sh.rows[i]];
      float y = sh.list_y - i * SHOP_ROW_DY + shift_at(i) + bob;
      if (y > SHOP_ROW_TOP || y < SHOP_ROW_BOTTOM) continue;
      bool can = can_buy(it);
      ui_sprite_at(it->sprite, SHOP_LIST_X, y, it->k, can ? act : ina);
      ui_piece(&row_geo, SHOP_LIST_X, y, 1, can ? act : ina);
      /* (the text over the masks, as they cover it) */
      int mi = (int)lrintf((y - bob + 7) * 4);
      float cover = mi >= 0 && mi < (int)sizeof mask_a ? mask_a[mi] * (1 / 255.0f) * sh.win_a : 0;
      ui_number_at(it->cost, STYLE_TUTE, SHOP_LIST_X + row_cost[0], y + row_cost[1], 0,
                   sh.root_a * (can ? 1 : 0.55f) * (1 - cover));
    }
  }
  ui_piece(&mask_top, 0, bob, 1, win);
  ui_piece(&mask_bottom, 0, bob, 1, win);
  if (sh.border_shown)
    for (int i = 0; i < 2; i++) anim_piece(&borders[i], &sh.border[i], white);
  if (sh.fig_shown || sh.fig_down) anim_piece(&figureheads[sh.shop], &sh.fig, white);
  /* the list's group: its arrows (a bob as the selection moves), the selector, the selected item's details */
  if (sh.root_a > 0 && sh.n) {
    const Item *it = cur_item();
    float bu = sh.arrow_t[0] > 0.1f ? SHOP_ARROW_BOB : sh.arrow_t[0] > 0 ? SHOP_ARROW_BOB * sh.arrow_t[0] / 0.1f : 0;
    float bd = sh.arrow_t[1] > 0.1f ? SHOP_ARROW_BOB : sh.arrow_t[1] > 0 ? SHOP_ARROW_BOB * sh.arrow_t[1] / 0.1f : 0;
    if (sh.cur > 0) ui_piece(&arrow_u, 0, bu + bob, 1, root);
    if (sh.cur + 1 < sh.n) ui_piece(&arrow_d, 0, -bd + bob, 1, root);
    ui_piece(&selector, 0, (sh.jitter > 0 ? rand_range(-0.11f, 0.11f) : 0) + bob, 1, root);
    /* (its name over its column, on two lines where it is too long for one; what comes under it, lower by as much) */
    float down = text_box(it->name, STYLE_TUTE, t_name.x, t_name.top + bob, t_name.w, 1, sh.root_a) -
                 font_line(font_style(STYLE_TUTE)) / HUD_PX;
    int notch = it->type == ST_CHARM ? charm_cost(it->charm) : 0;
    if (notch > 0) {
      /* (Notch Cost: its pegs, centred as Centre Pegs has them, as far under its text as the game has them) */
      const uint8_t *st = font_style(STYLE_MSG);
      float pegs = peg.y - (t_notch.top - (font_asc(st) + font_desc(st)) / HUD_PX - SHOP_PEG_BELOW);
      ui_text_at(TXT_SHOP_CHARM_TXT_NOTCHCOST, STYLE_MSG, &t_notch, 0, bob - down, 1, sh.root_a);
      for (int i = 0; i < notch && i < 6; i++) ui_piece(&peg, pegs_x[notch - 1] + i * SHOP_PEG_DX, bob - down - pegs, 1, root);
    }
    desc_draw(it, bob, sh.root_a);
  }
  /* the confirmation: the item, its cost, the question, Yes and No (the pointers at the one chosen) */
  if (sh.conf_a > 0) {
    const Item *it = cur_item();
    ui_text_at(it->name, STYLE_TUTE, &t_cname, 0, 0, 1, sh.conf_a);
    ui_sprite_at(it->sprite, confirm_item[0], confirm_item[1], 1, conf);
    ui_piece(&confirm_geo, 0, 0, 1, conf);
    ui_number_at(it->cost, STYLE_NOTICE, t_ccost.x, t_ccost.top, 0, sh.conf_a);
    ui_text_at(TXT_SHOP_SHOP_PURCHASE_CONFIRM, STYLE_TUTE, &t_cmsg, 0, 0, 1, sh.conf_a);
    ui_text_at(TXT_SHOP_YES, STYLE_DIALOGUE, &t_yes, 0, 0, 1, sh.conf_a);
    ui_text_at(TXT_SHOP_NO, STYLE_DIALOGUE, &t_no, 0, 0, 1, sh.conf_a);
    if (sh.st == SH_CONFIRM || sh.st == SH_CHOSEN) {
      float dy = sh.yes == 2 ? SHOP_NO_DY : 0, out = sh.chosen_t > 0.1f ? (0.15f - sh.chosen_t) / 0.05f * 0.45f
                                                  : sh.chosen_t > 0 ? sh.chosen_t / 0.1f * 0.45f : 0;
      ui_piece(&pointer_l, -out, dy, 1, white);
      ui_piece(&pointer_r, out, dy, 1, white);
    }
  }
  if (sh.thanks_a > 0) ui_text_at(TXT_SHOP_SHOP_PURCHASE_COMPLETE, STYLE_NOTICE, &t_thanks, 0, 0, 1, sh.thanks_a);
  /* the actions: Buy and Exit, by their keys (Confirm and Cancel in the confirmation) */
  if (sh.st == SH_LIST || sh.st == SH_CONFIRM) {
    /* (each by its key, as far from it as the game has them) */
    bool c = sh.st == SH_CONFIRM;
    const float kc[2] = {key_confirm[0] + SHOP_KEY_OUT, key_confirm[1]}, kb[2] = {key_cancel[0] - SHOP_KEY_OUT, key_cancel[1]};
    float ok = key_at(kc, TXT_KEY_OK, 1, white), back = key_at(kb, TXT_KEY_BACK, 1, white);
    ui_text_at(c ? TXT_SHOP_CTRL_CONFIRM : TXT_SHOP_CTRL_BUY, STYLE_MSG_S, &t_act_confirm, kc[0] - ok - SHOP_KEY_GAP -
               t_act_confirm.x, 0, 2, 1);
    ui_text_at(c ? TXT_SHOP_CTRL_CANCEL : TXT_SHOP_CTRL_EXIT, STYLE_MSG_S, &t_act_cancel, kb[0] + back + SHOP_KEY_GAP -
               t_act_cancel.x, 0, 0, 1);
  }
}
