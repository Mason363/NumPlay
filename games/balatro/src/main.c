/* Balatro for NumWorks: the app, its screens and controls. */
#include <eadk.h>
#include <string.h>
#include "../../common/epsilon_app.h"
#include "../../common/jump.h"
#include "art.h"
#include "assets.h"
#include "bg.h"
#include "ev.h"
#include "jokers.h"
#include "save.h"
#include "ui.h"
#include "util.h"

#ifdef __ELF__
#ifndef NP_PORT /* (another target: platform/port.mk names the app) */
const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "Balatro";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;
#endif
#endif

static np_jump_t leave;
static int in_game, has_save;

/* ------------------------------------------------------------ input */
enum { K_LEFT, K_UP, K_DOWN, K_RIGHT, K_OK, K_BACK, K_HOME, K_ONOFF = 8, K_SHIFT = 12, K_ALPHA, K_XNT, K_VAR,
       K_TOOLBOX, K_BACKSPACE, K_EXE = 52 };
static uint64_t keys, prev_keys;
static int rep_key = -1, rep_t;
static int pressed[64];
static int buf_key[4], buf_age[4], nbuf; /* presses kept while busy */

static void quit_now(void) {
  if (in_game) save_write(R.phase != PH_GAMEOVER);
  else save_write(has_save);
  np_jump(leave);
}

static void poll_input(int dt) {
  prev_keys = keys;
  keys = eadk_keyboard_scan();
  if ((keys >> K_HOME) & 1 || (keys >> K_ONOFF) & 1) quit_now();
  memset(pressed, 0, sizeof pressed);
  for (int k = 0; k < 53; k++)
    if (((keys >> k) & 1) && !((prev_keys >> k) & 1)) {
      pressed[k] = 1;
      if (k <= K_RIGHT) rep_key = k, rep_t = 330;
    }
  if (rep_key >= 0) {
    if (!((keys >> rep_key) & 1)) rep_key = -1;
    else if ((rep_t -= dt) <= 0) {
      pressed[rep_key] = 1;
      rep_t = 90;
    }
  }
}
static int held(int k) { return (keys >> k) & 1; }

/* ------------------------------------------------------------ screens */
enum { SCR_TITLE, SCR_GAME };
enum { OV_NONE, OV_OPTIONS, OV_RUNINFO, OV_DECK, OV_CREDITS, OV_HELP, OV_SETUP, OV_CONFIRM_NEW };
static int screen = SCR_TITLE, overlay = OV_NONE, ov_sel, ov_tab;
static int title_sel;
static int menu_open = -1, menu_sel; /* action menu on the focused item */
static int deny_t;                    /* "can't" feedback */

enum { Z_HAND = 1, Z_JOKERS, Z_CONS, Z_BTN, Z_DECK, Z_SHOP, Z_BOTTOM, Z_SHOPBTN, Z_PACK, Z_SKIP };
static int zone = Z_HAND, zi;
static int focus_time, last_zone, last_zi; /* how long the focus stayed put */
int moving;                                /* the card in focus picked up to be moved (ui.c draws it) */

extern void layout_cards(int dt, int show_hand, int hand_y);
extern void draw_jokers_row(int focus_j, int focus_c, int sel_j, int sel_c);
extern void draw_hand_cards(int focus_h);
extern void draw_play_cards(void);
extern void draw_deck_pile(int focused);
extern void draw_popups(void);
extern int ui_hand_x(int i, int n);
extern int blind_debuffs_hand(void);
extern int cons_usable(int id, int in_pack);

static void deny(void) { deny_t = 300; }
static void autosave(void) { save_write(R.phase != PH_GAMEOVER); }

static void bg_for_phase(void) {
  if (screen == SCR_TITLE) {
    bg_mode(1);
    return;
  }
  bg_mode(0);
  if (R.phase == PH_PACK) {
    switch (R.pack_kind) {
      case PK_ARCANA: bg_colours(0x8867A5, 0x2C3536, 0, 1.5f, 0, 0); break;
      case PK_SPECTRAL: bg_colours(0x4584FA, 0x2C3536, 0, 2, 0, 0); break;
      case PK_STANDARD: bg_colours(0x2C3536, 0xFE5F55, 0, 3, 0, 0); break;
      case PK_BUFFOON: bg_colours(0xFF9A00, 0x374244, 0, 2, 0, 0); break;
      default: bg_colours(0x374244, 0, 0, 3, 0, 0);
    }
    return;
  }
  int b = -1;
  if (R.phase == PH_ROUND || R.phase == PH_CASHOUT) b = R.blind;
  if (R.phase == PH_BLIND_SELECT) b = R.blind_on == 2 ? R.boss : BL_SMALL;
  if (R.phase == PH_SHOP) b = BL_SMALL;
  if (R.won && R.phase != PH_ROUND) {
    bg_colours(0x4F6367, 0, 0, 1, 0, 0);
    return;
  }
  if (b < BL_OX) bg_colours(0x50846E, 0, 0, 1, 0, 0);
  else if (blind_info[b].showdown) bg_colours(0x009DFF, 0xFE5F55, 0x21282A, 3, 0.5f, 0);
  else {
    uint32_t c = blind_info[b].colour;
    /* lighten(mix(boss, black, 0.3), 0.1) */
    int r = ((c >> 16) & 255) * 3 / 10 + 0x37 * 7 / 10, g = ((c >> 8) & 255) * 3 / 10 + 0x42 * 7 / 10,
        bl = (c & 255) * 3 / 10 + 0x44 * 7 / 10;
    r += (255 - r) / 10, g += (255 - g) / 10, bl += (255 - bl) / 10;
    bg_colours((uint32_t)(r << 16 | g << 8 | bl), c, 0, 2, R.phase == PH_ROUND ? 0.25f : 0, 0);
  }
}

/* ------------------------------------------------------------ title */
static int title_t;
#define LOGO_CARD 316 /* the Ace fills the logo's height, as in the game */

static void draw_title(void) {
  int lx = 160 - 106, ly = 14 + (title_t < 600 ? (600 - title_t) / 30 : 0);
  g_sprite(SPR_LOGO, lx, ly, 0, 256);
  /* the Ace of Spades standing in for the logo's A, floating gently */
  int bob = (int)((g_time / 140) % 24);
  bob = bob < 12 ? bob / 4 : (24 - bob) / 4;
  int cx = 160 - CW / 2, cy = ly + 46 - bob;
  g_sprite(SPR_E + 1, cx + 2, cy + 3 + bob, FX_SHADOW, LOGO_CARD);
  g_sprite(SPR_E + 1, cx, cy, 0, LOGO_CARD);
  g_sprite(SPR_F + 3 * 13 + 12, cx, cy, 0, LOGO_CARD);
  /* buttons, like the main menu (they make way for an open panel) */
  if (overlay) return;
  static const char *const lbl[4] = {T("PLAY"), T("OPTIONS"), T("QUIT"), T("CREDITS")};
  static const C col[4] = {C_BLUE, C_ORANGE, C_RED, C_GREEN};
  g_rrect(20, 195, 280, 42, 7, HEXC(0x1A2224));
  g_rrect(21, 196, 278, 40, 6, HEXC(0x3A5055));
  g_rrect(23, 198, 274, 36, 5, HEXC(0x2B3A3D));
  for (int i = 0; i < 4; i++) {
    int bw = i == 0 ? 80 : 58, bx = i == 0 ? 30 : 116 + (i - 1) * 60;
    button(bx, 204, bw, 24, col[i], lbl[i], i == 0 ? T_LARGE : T_MED, title_sel == i, 1);
  }
}

/* ------------------------------------------------------------ overlays */
static void big_panel(int x, int y, int w, int h) {
  g_shade(0, 0, 320, 240, 0, 14);
  g_rrect(x - 2, y - 2, w + 4, h + 6, 7, HEXC(0x1A2224));
  g_rrect(x, y, w, h, 6, HEXC(0x3A5055));
  g_rrect(x + 3, y + 3, w - 6, h - 6, 5, HEXC(0x2B3A3D));
}

/* a panel's title, large, in the top 20 pixels of the panel */
static void panel_title(const char *t, int x, int y, int w) { g_text_box(t, x, y + 3, w, 20, F_TITLE, C_WHITE); }

static void tab_button(int x, int y, int w, const char *label, int on) {
  if (on) g_rect(x + w / 2 - 3, y - 4, 6, 3, C_RED);
  button(x, y, w, 15, on ? C_RED : HEXC(0x8C3B35), label, F_LABEL, 0, 1);
}

/* a deck or stake row: dark box, art on the left, name over a white box */
static void setup_row(int y, int h, int bh) {
  g_rrect(62, y + 1, 196, h, 5, HEXC(0x1A2224));
  g_rrect(61, y, 196, h, 5, HEXC(0x1F2B2D));
  g_rrect(106, y + 3, 148, h - 6, 4, HEXC(0x3A4B4E));
  g_rrect(109, y + h - 4 - bh, 142, bh, 3, C_WHITE);
}

static void draw_setup(void) {
  big_panel(30, 10, 260, 214);
  int ntabs = has_save ? 2 : 1;
  tab_button(ntabs == 1 ? 128 : 94, 20, 64, T("New Run"), ov_tab == 0);
  if (ntabs == 2) tab_button(162, 20, 64, T("Continue"), ov_tab == 1);
  char t[64];
  /* the deck: name, then its text or the run's numbers on white */
  int bh = ov_tab == 0 ? 25 : 45;
  setup_row(42, 68, bh);
  g_sprite(SPR_E, 69, 53, FX_SHADOW, 256);
  g_sprite(SPR_E, 67, 51, 0, 256);
  int wy = 42 + 68 - 4 - bh; /* the white box */
  g_text_box(T("Red Deck"), 106, 45, 148, wy - 45, F_NAME, C_WHITE);
  if (ov_tab == 0)
    g_text_box(T("\003+1\020 discard\nevery round"), 109, wy, 142, bh, F_TEXT, C_TEXT_DARK);
  else {
    static const char *const lab[4] = {T("Round"), T("Ante"), T("Money"), T("Best Hand")};
    for (int i = 0; i < 4; i++) {
      int yy = wy + 3 + i * 10;
      g_text(lab[i], 170, yy, F_TEXT | T_RIGHT, C_TEXT_DARK);
      g_text(":", 173, yy, F_TEXT, C_TEXT_DARK);
      C c = i == 0 ? C_RED : i == 1 ? C_BLUE : i == 2 ? C_MONEY : C_RED;
      if (i == 0) fmt_int(t, R.round);
      else if (i == 1) fmt_int(t, R.ante);
      else if (i == 2) fmt_money(t, R.money);
      else fmt_commas(t, R.best_hand);
      g_text(t, 178, yy, F_TEXT, c);
    }
  }
  /* the stake */
  setup_row(115, 38, 19);
  g_sprite(SPR_STAKE, 76, 126, 0, 256);
  g_text_box(T("White Stake"), 106, 117, 148, 13, F_NAME, C_WHITE);
  g_text_box(T("Base Difficulty"), 109, 130, 142, 19, F_TEXT, C_TEXT_DARK);
  if (ov_tab == 0) {
    char *o = str_cat(t, T("Wins \004"));
    o = fmt_int(o, S.wins);
    o = str_cat(o, T("\001   Best Ante \004"));
    o = fmt_int(o, S.best_ante > 0 ? S.best_ante : 0);
    o = str_cat(o, T("\001   Best Hand \003"));
    fmt_commas(o, S.best_hand);
  } else {
    char *o = str_cat(t, T("Seed \004"));
    str_cat(o, R.seed);
  }
  g_text(t, 160, 159, F_LABEL | T_CENTER, C_WHITE);
  button(106, 172, 108, 24, C_BLUE, ov_tab == 0 ? T("PLAY") : T("CONTINUE"), T_LARGE, ov_sel == 0, 1);
  button(38, 202, 244, 16, C_ORANGE, T("Back"), T_MED, ov_sel == 1, 1);
}

static const char help_lines[] =
    T("\004Arrows\020  move between cards, Jokers, buttons\n\004OK\020  select a card, or pick a Joker/item and\n     choose \006Sell\020, \003Use\020, \005Buy\020...\n\004EXE\020  \002Play Hand\020      \004DEL\020  \003Discard\020\n\004shift\020  sort the hand by rank or suit\n\004alpha\020  pick up a card or Joker to move it,\n     \004left/right\020 to carry it, \004OK\020 to put it down\n\004toolbox\020  Run Info      \004var\020  view deck\n\004back\020  back / Options     \004home\020  save, quit\nHold \004OK\020 or \004EXE\020 to speed up scoring.\n\nPlay poker hands: score = \002Chips\020 x \003Mult\020.\nBeat each Blind's score before your\nhands run out. Buy Jokers in the shop\nand beat the Ante 8 Boss Blind to win!");

static void draw_help(void) {
  big_panel(10, 6, 300, 228);
  panel_title(T("How to play"), 10, 6, 300);
  g_rrect(18, 30, 284, 172, 4, HEXC(0x1F2B2D));
  g_text(help_lines, 160 - g_textw(help_lines, F_TEXT) / 2, 30 + (172 - g_lines(help_lines) * LINE_H + 3) / 2, F_TEXT, C_WHITE);
  button(120, 208, 80, 18, C_ORANGE, T("Back"), T_MED, 1, 1);
}

static void draw_credits(void) {
  big_panel(12, 10, 296, 220);
  panel_title(T("Credits"), 12, 10, 296);
  g_rrect(20, 34, 280, 166, 4, HEXC(0x1F2B2D));
  g_text(T("Inspired by \004Balatro\020 by LocalThunk.\nNot affiliated with LocalThunk or Playstack.\n\nCard, Joker and interface art adapted\nfrom Balatro by LocalThunk.\nRules and card texts follow the game.\nFont: \004m6x11\020 by Daniel Linssen.\n\nMade by \004Mason Chen\020 as part of NumPlay.\n\nPlease support the original game!"),
         160, 53, F_TEXT | T_CENTER, C_WHITE);
  button(120, 206, 80, 18, C_ORANGE, T("Back"), T_MED, 1, 1);
}

enum { OPT_CONTINUE, OPT_SPEED, OPT_HELP, OPT_NEWRUN, OPT_MAINMENU, OPT_BACK };
static const char *const opt_items[6] = {T("Continue"), T("Game Speed"), T("How to Play"), T("New Run"), T("Main Menu"), T("Back")};
static const uint8_t opt_game[5] = {OPT_CONTINUE, OPT_SPEED, OPT_HELP, OPT_NEWRUN, OPT_MAINMENU};
static const uint8_t opt_title[3] = {OPT_SPEED, OPT_HELP, OPT_BACK};
static int opt_count(void) { return in_game ? 5 : 3; }
static int opt_id(int i) { return in_game ? opt_game[i] : opt_title[i]; }
static int opt_index(int id) {
  for (int i = 0; i < opt_count(); i++)
    if (opt_id(i) == id) return i;
  return 0;
}

static void draw_options(void) {
  int n = opt_count(), h = 32 + n * 28, y0 = 120 - h / 2;
  big_panel(70, y0, 180, h);
  panel_title(T("Options"), 70, y0, 180);
  static const char *const sp[4] = {"0.5", "1", "2", "4"};
  for (int i = 0; i < n; i++) {
    int id = opt_id(i), y = y0 + 28 + i * 28;
    C c = id == OPT_CONTINUE ? C_BLUE : id == OPT_SPEED || id == OPT_BACK ? C_ORANGE : id == OPT_HELP ? C_GREEN : C_RED;
    if (id == OPT_SPEED) {
      char t[24];
      char *o = str_cat(t, T("Game Speed < "));
      o = str_cat(o, sp[S.speed & 3]);
      str_cat(o, " >");
      button(84, y, 152, 21, c, t, T_MED, ov_sel == i, 1);
    } else
      button(84, y, 152, 21, c, opt_items[id], T_MED, ov_sel == i, 1);
  }
}

static void draw_confirm_new(void) {
  big_panel(60, 70, 200, 100);
  panel_title(T("New run?"), 60, 70, 200);
  g_text_box(T("The current run will be lost."), 60, 96, 200, 16, F_LABEL, HEXC(0xDDDDDD));
  button(80, 126, 70, 24, C_RED, T("Yes"), T_MED, ov_sel == 0, 1);
  button(170, 126, 70, 24, C_BLUE, T("No"), T_MED, ov_sel == 1, 1);
}

/* Run Info: poker hands, blinds, vouchers */
static void draw_run_info(void) {
  char t[40];
  big_panel(6, 4, 308, 232);
  static const char *const tabs[3] = {T("Poker Hands"), T("Blinds"), T("Vouchers")};
  for (int i = 0; i < 3; i++) tab_button(40 + i * 82, 12, 76, tabs[i], ov_tab == i);
  if (ov_tab == 0) {
    static const uint8_t order[12] = {H_FLUSH_FIVE, H_FLUSH_HOUSE, H_FIVE_KIND, H_STRAIGHT_FLUSH, H_FOUR_KIND,
                                      H_FULL_HOUSE, H_FLUSH, H_STRAIGHT, H_THREE_KIND, H_TWO_PAIR, H_PAIR,
                                      H_HIGH_CARD};
    int y = 34;
    for (int k = 0; k < 12; k++) {
      int h = order[k];
      if (!R.hands[h].visible) continue;
      int lv = R.hands[h].level;
      static const uint32_t lvc[7] = {0xEFEFEF, 0x95ACFF, 0x65EFAF, 0xFAE37E, 0xFFC052, 0xF87D75, 0xCAA0EF};
      C lc = HEXC(lvc[lv < 1 ? 0 : lv > 7 ? 6 : lv - 1]);
      /* lvl | name | chips X mult | # played, as in the game */
      g_rrect(14, y, 292, 16, 4, HEXC(0xDDE4E6));
      g_rrect(16, y + 2, 34, 12, 3, lc);
      fmt_int(str_cat(t, T("lvl.")), lv);
      g_text_box(t, 16, y + 2, 34, 12, F_TEXT, C_TEXT_DARK);
      g_text(hand_names[h], 56, y + 3, T_MED, C_TEXT_DARK);
      g_rrect(172, y + 2, 42, 12, 3, C_BLUE);
      fmt_commas(t, hand_chips(h));
      g_text(t, 211, y + 3, text_fit(t, 38, T_MED) | T_RIGHT | T_SHADOW, C_WHITE);
      g_text("X", 219, y + 3, T_MED | T_CENTER, C_RED);
      g_rrect(224, y + 2, 42, 12, 3, C_RED);
      fmt_commas(t, hand_mult(h));
      g_text(t, 227, y + 3, text_fit(t, 38, T_MED) | T_SHADOW, C_WHITE);
      g_text("#", 276, y + 3, T_MED, C_ATTN);
      fmt_int(t, R.hands[h].played);
      g_text(t, 301, y + 3, T_MED | T_RIGHT, C_TEXT_DARK);
      y += 18;
    }
  } else if (ov_tab == 1) {
    /* the columns end together, under the boss's text */
    static char bd[80], bd2[96];
    desc_text(bd, sizeof bd, TK_BLIND, R.boss, 0);
    wrap_text(bd2, sizeof bd2, bd, 78, F_TEXT);
    int colh = 131 + g_lines(bd2) * LINE_H + 5 + 4 - 34;
    for (int k = 0; k < 3; k++) {
      int b = k == 0 ? BL_SMALL : k == 1 ? BL_BIG : R.boss;
      int x = 16 + k * 98, w = 92;
      C main = k == 0 ? mix565(C_BLACK, C_BLUE, 19) : k == 1 ? mix565(C_BLACK, C_ORANGE, 19) : HEXC(blind_info[b].colour);
      g_rrect(x, 34, w, colh, 5, mix565(main, C_BLACK, 16));
      g_rrect(x + 3, 37, w - 6, 15, 3, readable(main, 0));
      g_text_box(blind_name(b), x + 3, 37, w - 6, 15, text_fit(blind_name(b), w - 12, T_MED) | T_SHADOW, C_WHITE);
      draw_blind_chip(b, x + w / 2 - 13, 57, 256);
      g_rrect(x + 4, 88, w - 8, 38, 3, HEXC(0x1D2B2C));
      g_text(T("Score at least"), x + w / 2, 91, F_LABEL | T_CENTER, C_WHITE);
      char n[24];
      int f = num_fit(n, blind_amount(R.ante) * blind_info[b].mult2 / 2, w - 14, T_LARGE);
      g_text(n, x + w / 2, 101 + (11 - g_cap(f) + 1) / 2, f | T_CENTER | T_SHADOW, C_RED);
      label_dollars(T("Reward:"), blind_info[b].dollars, x + w / 2, 116, w - 12);
      if (b >= BL_OX) {
        g_rrect(x + 4, 131, w - 8, g_lines(bd2) * LINE_H + 5, 3, C_WHITE);
        g_text(bd2, x + w / 2, 134, F_TEXT | T_CENTER, C_TEXT_DARK);
      }
    }
  } else {
    int n = 0;
    for (int v = 0; v < 32; v++)
      if (has_voucher(v)) {
        int x = 20 + (n % 7) * 41, y = 36 + (n / 7) * 52;
        g_sprite(SPR_V + v, x, y, 0, 256);
        n++;
      }
    if (!n) g_text_box(T("No Vouchers redeemed yet"), 6, 100, 308, 30, F_NAME, C_WHITE);
  }
  g_text(T("left/right: tabs     back: close"), 160, 225, F_TEXT | T_CENTER, HEXC(0xBBBBBB));
}

/* View Deck: the cards by suit */
static void draw_deck_view(void) {
  char t[24];
  big_panel(4, 4, 312, 232);
  tab_button(96, 12, 62, T("Remaining"), ov_tab == 0);
  tab_button(162, 12, 62, T("Full Deck"), ov_tab == 1);
  static const uint8_t suit_order[4] = {S_SPADES, S_HEARTS, S_CLUBS, S_DIAMONDS};
  int counts[4] = {0};
  for (int s = 0; s < 4; s++) {
    int y = 34 + s * 49;
    int list[MAXCARDS], n = 0;
    for (int r = 14; r >= 2; r--)
      for (int i = 0; i < R.ncards_alloc; i++) {
        const card_t *c = &R.cards[i];
        if (!(c->flags & CF_USED) || c->suit != suit_order[s] || c->rank != r) continue;
        int in_deck = 0;
        for (int k = 0; k < R.ndeck; k++) in_deck |= R.deck[k] == i;
        if (ov_tab == 0 && !in_deck && R.phase == PH_ROUND) continue;
        list[n++] = i;
      }
    counts[s] = n;
    int step = n > 1 ? (244 - CW) / (n - 1) : 0;
    if (step > 18) step = 18;
    for (int k = 0; k < n; k++) {
      vcard_t v = {0};
      const card_t *c = &R.cards[list[k]];
      v.rank = c->rank, v.suit = c->suit, v.enh = c->enh, v.ed = c->ed, v.seal = c->seal;
      draw_card(&v, 66 + k * step, y, 256, 0);
    }
  }
  static const char *const sn[4] = {SUIT_SPADE, SUIT_HEART, SUIT_CLUB, SUIT_DIAMOND};
  static const C sc[4] = {HEXC(0x3C4368), HEXC(0xF03464), HEXC(0x235955), HEXC(0xF06B3F)};
  g_rrect(10, 32, 50, 196, 4, HEXC(0x1F2B2D));
  for (int s = 0; s < 4; s++) {
    int y = 34 + s * 49 + 16;
    g_rrect(14, y + 1, 15, 15, 3, HEXC(0x0E181A));
    g_rrect(14, y, 15, 15, 3, C_WHITE);
    g_text(sn[s], 18, y + 4, T_SMALL, sc[s]);
    fmt_int(t, counts[s]);
    g_text_box(t, 30, y, 28, 15, F_VALUE, C_WHITE);
  }
}

/* ------------------------------------------------------------ helpers */
static int hand_focus(void) { return zone == Z_HAND ? zi : -1; }

/* Play Hand, Sort Hand (Rank, Suit), Discard: under the hand, keys below */
#define BTN_Y 205
static void round_buttons(int show) {
  if (!show) return;
  int n = nselected();
  int can_play = n > 0 && n <= 5 && !ui_busy(), can_disc = n > 0 && R.discards_left > 0 && !ui_busy();
  int f = zone == Z_BTN ? zi : -1;
  const int px = HAND_X, pw = 52, sx = px + pw + 4, sw = 59, dx = sx + sw + 4, dw = 52;
  button(px, BTN_Y, pw, 22, C_BLUE, T("Play Hand"), F_LABEL, f == 0, can_play);
  keycap(px + pw / 2, 229, "exe");
  panel(sx, BTN_Y - 1, sw, 23, 4, HEXC(0x4F6367), 0);
  g_text(T("Sort Hand"), sx + sw / 2, BTN_Y + 1, F_LABEL | T_CENTER, C_WHITE);
  button(sx + 3, BTN_Y + 10, 25, 11, C_ORANGE, T("Rank"), F_LABEL, f == 1, 1);
  button(sx + sw - 28, BTN_Y + 10, 25, 11, C_ORANGE, T("Suit"), F_LABEL, f == 2, 1);
  keycap(sx + sw / 2, 229, "shift");
  button(dx, BTN_Y, dw, 22, C_RED, T("Discard"), F_LABEL, f == 3, can_disc);
  keycap(dx + dw / 2, 229, "del");
}

static void draw_focus_tooltip(void) {
  if (ui_busy() || moving) return;
  if (zone == Z_JOKERS && zi < V.njokers && menu_open < 0) {
    vitem_t *v = &V.jokers[zi];
    if (v->flags & JF_DOWN) return;
    const joker_t *j = zi < R.njokers ? &R.jokers[zi] : 0;
    draw_tooltip_for(TK_JOKER, v->id, v->ed, j, (int)v->x, (int)v->y, CW);
  } else if (zone == Z_CONS && zi < V.ncons && menu_open < 0) {
    vitem_t *v = &V.cons[zi];
    draw_tooltip_for(TK_CONS, v->id, v->ed, 0, (int)v->x, (int)v->y, CW);
  } else if (zone == Z_HAND && zi < V.nhand) {
    vcard_t *c = &V.cards[V.hand[zi]];
    if (c->down) return;
    int special = c->enh || c->seal || c->ed || (c->flags & CF_DEBUFF) || c->perma;
    if (special || focus_time > 700) draw_tooltip_for(TK_CARD, 0, 0, c, (int)c->x, (int)c->y - 4, CW);
  }
}

static int menu_items(int *kinds) {
  int n = 0;
  if (zone == Z_JOKERS) kinds[n++] = 1; /* sell */
  if (zone == Z_CONS) {
    kinds[n++] = 2; /* use */
    kinds[n++] = 1; /* sell */
  }
  return n;
}

static void draw_menu(void) {
  if (menu_open < 0 || (zone != Z_JOKERS && zone != Z_CONS)) return;
  int kinds[4], n = menu_items(kinds);
  vitem_t *v = zone == Z_JOKERS ? &V.jokers[menu_open] : &V.cons[menu_open];
  /* on the card's right edge, as in the game (its left edge at the screen's
     right), so the rows and panels under the card stay clear */
  const int bw = 32;
  int x = (int)v->x + CW - 3, y = ROW_Y + (n > 1 ? 3 : 10);
  if (x + bw > 318) x = (int)v->x - bw + 3;
  char t[20];
  for (int i = 0; i < n; i++) {
    int yy = y + i * 19;
    if (kinds[i] == 1) {
      int val = zone == Z_JOKERS ? joker_sell_value(&R.jokers[menu_open]) : cons_sell_value(&R.cons[menu_open]);
      fmt_money(str_cat(t, T("SELL\n")), val);
      button(x, yy, bw, 22, C_GREEN, t, F_LABEL, menu_sel == i, 1);
    } else
      button(x, yy, bw, 15, C_RED, T("USE"), F_LABEL, menu_sel == i, cons_usable(R.cons[menu_open].id, 0));
    if (kinds[i] == 2) y -= 1;
  }
}

static void clamp_focus(void) {
  if (zone == Z_HAND && zi >= V.nhand) zi = V.nhand - 1;
  if (zone == Z_JOKERS && zi >= V.njokers) zi = V.njokers - 1;
  if (zone == Z_CONS && zi >= V.ncons) zi = V.ncons - 1;
  if (zi < 0) zi = 0;
  if (zone == Z_JOKERS && V.njokers == 0) zone = V.ncons ? Z_CONS : (R.phase == PH_SHOP ? Z_SHOPBTN : Z_HAND), zi = 0;
  if (zone == Z_CONS && V.ncons == 0) zone = V.njokers ? Z_JOKERS : (R.phase == PH_SHOP ? Z_SHOPBTN : Z_HAND), zi = 0;
  if (zone == Z_HAND && V.nhand == 0 && R.phase == PH_ROUND) zone = Z_BTN, zi = 0;
}

static int nearest_hand(int x) {
  int best = 0, bd = 9999;
  for (int i = 0; i < V.nhand; i++) {
    int d = ui_hand_x(i, V.nhand) - x;
    if (d < 0) d = -d;
    if (d < bd) bd = d, best = i;
  }
  return best;
}

static int item_x(int z, int i) {
  if (z == Z_JOKERS && i < V.njokers) return (int)V.jokers[i].x;
  if (z == Z_CONS && i < V.ncons) return (int)V.cons[i].x;
  if (z == Z_HAND && i < V.nhand) return ui_hand_x(i, V.nhand);
  return 160;
}

/* left/right within the jokers and consumables row */
static int row_nav(void) {
  if (zone != Z_JOKERS && zone != Z_CONS) return 0;
  int n = zone == Z_JOKERS ? V.njokers : V.ncons;
  if (pressed[K_LEFT]) {
    if (zi > 0) zi--;
    else if (zone == Z_CONS && V.njokers) zone = Z_JOKERS, zi = V.njokers - 1;
    return 1;
  }
  if (pressed[K_RIGHT]) {
    if (zi < n - 1) zi++;
    else if (zone == Z_JOKERS && V.ncons) zone = Z_CONS, zi = 0;
    return 1;
  }
  return 0;
}

static void up_to_row(int x) {
  if (!V.njokers && !V.ncons) return;
  int best = 0, bd = 9999, bz = V.njokers ? Z_JOKERS : Z_CONS;
  for (int i = 0; i < V.njokers; i++) {
    int d = (int)V.jokers[i].x - x;
    if (d < 0) d = -d;
    if (d < bd) bd = d, best = i, bz = Z_JOKERS;
  }
  for (int i = 0; i < V.ncons; i++) {
    int d = (int)V.cons[i].x - x;
    if (d < 0) d = -d;
    if (d < bd) bd = d, best = i, bz = Z_CONS;
  }
  zone = bz, zi = best;
}

static void menu_input(void) {
  int kinds[4], n = menu_items(kinds);
  if (pressed[K_UP] && menu_sel > 0) menu_sel--;
  if (pressed[K_DOWN] && menu_sel < n - 1) menu_sel++;
  if (pressed[K_BACK] || pressed[K_LEFT] || pressed[K_RIGHT]) {
    menu_open = -1;
    return;
  }
  if (pressed[K_OK] || pressed[K_EXE]) {
    int ok = 0, k = kinds[menu_sel];
    if (zone == Z_JOKERS && k == 1) ok = sell_joker(menu_open);
    if (zone == Z_CONS && k == 1) ok = sell_cons(menu_open);
    if (zone == Z_CONS && k == 2) ok = use_cons(menu_open);
    if (!ok) deny();
    else autosave();
    menu_open = -1;
  }
}

static void move_focused(int dir) {
  if (zone == Z_JOKERS) {
    int to = zi + dir;
    if (to >= 0 && to < R.njokers) {
      move_joker(zi, to);
      vitem_t t = V.jokers[zi];
      V.jokers[zi] = V.jokers[to], V.jokers[to] = t;
      zi = to;
      autosave();
    }
  } else if (zone == Z_CONS) {
    int to = zi + dir;
    if (to >= 0 && to < R.ncons) {
      move_cons(zi, to);
      vitem_t t = V.cons[zi];
      V.cons[zi] = V.cons[to], V.cons[to] = t;
      zi = to;
      autosave();
    }
  } else if (zone == Z_HAND) {
    int to = zi + dir;
    if (to >= 0 && to < R.nhand) {
      move_hand_card(zi, to);
      uint8_t t = V.hand[zi];
      V.hand[zi] = V.hand[to], V.hand[to] = t;
      zi = to;
      autosave();
    }
  }
}

/* Rearranging, as dragging does in the game: alpha picks up the card, Joker or consumable in focus, left and right
   carry it along its row, and OK (or alpha, or Back) puts it down. Holding alpha while pressing left or right works
   too: letting go of alpha then puts it down. */
static int moved_held;
static int can_move(void) {
  if (menu_open >= 0) return 0;
  if (zone == Z_HAND) return zi < R.nhand && zi < V.nhand && R.nhand > 1;
  if (zone == Z_JOKERS) return zi < R.njokers && R.njokers > 1;
  if (zone == Z_CONS) return zi < R.ncons && R.ncons > 1;
  return 0;
}
/* 1 when it used the keys */
static int move_input(void) {
  if (moving && !can_move()) moving = 0;
  if (!moving) {
    if (!pressed[K_ALPHA] || !can_move()) return 0;
    moving = 1, moved_held = 0;
    return 1;
  }
  if (pressed[K_LEFT] || pressed[K_RIGHT]) {
    move_focused(pressed[K_LEFT] ? -1 : 1);
    if (held(K_ALPHA)) moved_held = 1;
    return 1;
  }
  if (pressed[K_OK] || pressed[K_ALPHA] || pressed[K_BACK] || pressed[K_EXE] || (moved_held && !held(K_ALPHA))) {
    moving = 0;
    return 1;
  }
  if (pressed[K_UP] || pressed[K_DOWN] || pressed[K_BACKSPACE] || pressed[K_SHIFT]) moving = 0;   /* and the key does its job */
  return 0;
}

/* the key line's part for it, under the row in focus: "alpha move", or how to put the card down */
static void move_hint(int x, int y, int right) {
  const char *k = moving ? "OK" : "alpha", *what = moving ? T("put down") : T("move");
  int kw = g_textw(k, T_SMALL) + 6, w = kw + 3 + g_textw(what, F_LABEL);
  if (right) x -= w;
  keycap(x + kw / 2, y, k);
  g_text(what, x + kw + 3, y + 1, F_LABEL, C_WHITE);
}
static void move_hints(int hand_y) {
  if (overlay || ui_busy() || !(moving || can_move())) return;
  char t[8];
  if (zone == Z_HAND) move_hint(HAND_X + HAND_W, hand_y + CH + 2, 1);
  else if (zone == Z_JOKERS) {
    char *o = fmt_int(t, R.njokers);
    *o++ = '/', fmt_int(o, joker_slots());
    move_hint(JOKER_X + g_textw(t, F_LABEL) + 6, ROW_Y + CH + 3, 0);
  } else if (zone == Z_CONS) {
    char *o = fmt_int(t, R.ncons);
    *o++ = '/', fmt_int(o, cons_slots());
    move_hint(CONS_X + CONS_W - g_textw(t, F_LABEL) - 6, ROW_Y + CH + 3, 1);
  }
}

static void do_sort(int by_suit) {
  R.sort_suit = (uint8_t)by_suit;
  sort_hand(R.sort_suit);
  ev_reset();
  ui_sync_all(0);
  autosave();
}

/* ------------------------------------------------------------ round */
static void round_input(void) {
  if (menu_open >= 0) {
    menu_input();
    return;
  }
  if (move_input()) return;
  if (pressed[K_EXE]) {
    if (!play_hand()) deny();
    else zone = Z_HAND, autosave();
    return;
  }
  if (pressed[K_BACKSPACE]) {
    if (!discard_hand()) deny();
    else autosave();
    return;
  }
  if (pressed[K_SHIFT]) {
    do_sort(!R.sort_suit);
    return;
  }
  if (row_nav()) return;
  switch (zone) {
    case Z_HAND:
      if (pressed[K_LEFT] && zi > 0) zi--;
      if (pressed[K_RIGHT]) {
        if (zi < V.nhand - 1) zi++;
        else zone = Z_DECK;
      }
      if (pressed[K_UP]) up_to_row(item_x(Z_HAND, zi));
      if (pressed[K_DOWN]) zone = Z_BTN, zi = nselected() ? 0 : 1;
      if (pressed[K_OK] && zi < R.nhand) {
        int before = R.sel[zi];
        toggle_select(zi);
        if (before == R.sel[zi]) deny();
        V.cards[R.hand[zi]].raise = R.sel[zi];
        autosave();
      }
      if (pressed[K_BACK]) {
        if (nselected()) {
          for (int i = 0; i < R.nhand; i++)
            if (!(R.cards[R.hand[i]].flags & CF_FORCED)) R.sel[i] = 0, V.cards[R.hand[i]].raise = 0;
        } else
          overlay = OV_OPTIONS, ov_sel = 0;
      }
      break;
    case Z_DECK:
      if (pressed[K_LEFT]) zone = Z_HAND, zi = V.nhand - 1;
      if (pressed[K_UP]) up_to_row(300);
      if (pressed[K_DOWN]) zone = Z_BTN, zi = 3;
      if (pressed[K_OK]) overlay = OV_DECK, ov_tab = 0;
      if (pressed[K_BACK]) overlay = OV_OPTIONS, ov_sel = 0;
      break;
    case Z_JOKERS: case Z_CONS:
      if (pressed[K_DOWN]) zone = Z_HAND, zi = nearest_hand(item_x(zone, zi));
      if (pressed[K_OK]) menu_open = zi, menu_sel = 0;
      if (pressed[K_BACK]) zone = Z_HAND, zi = 0;
      break;
    case Z_BTN:
      if (pressed[K_LEFT] && zi > 0) zi--;
      if (pressed[K_RIGHT]) {
        if (zi < 3) zi++;
        else zone = Z_DECK;
      }
      if (pressed[K_UP]) zone = Z_HAND, zi = nearest_hand(HAND_X + 26 + zi * 57);
      if (pressed[K_OK]) {
        if (zi == 0) {
          if (!play_hand()) deny();
          else zone = Z_HAND, autosave();
        } else if (zi == 3) {
          if (!discard_hand()) deny();
          else autosave();
        } else
          do_sort(zi == 2);
      }
      if (pressed[K_BACK]) overlay = OV_OPTIONS, ov_sel = 0;
      break;
    default: zone = Z_HAND, zi = 0;
  }
  clamp_focus();
}

static void draw_round(int dt, int show_buttons) {
  layout_cards(dt, 1, HAND_Y);
  draw_sidebar();
  int fj = zone == Z_JOKERS ? zi : -1, fc = zone == Z_CONS ? zi : -1;
  draw_jokers_row(fj, fc, zone == Z_JOKERS ? menu_open : -1, zone == Z_CONS ? menu_open : -1);
  g_shade(HAND_X - 4, HAND_Y - 7, HAND_W + 8, CH + 15, 5, 7);
  draw_deck_pile(zone == Z_DECK);
  draw_play_cards();
  draw_hand_cards(hand_focus());
  char t[12];
  char *o = fmt_int(t, V.nhand);
  *o++ = '/';
  fmt_int(o, hand_size());
  g_text(t, HAND_X + HAND_W / 2, HAND_Y + CH + 3, F_LABEL | T_CENTER, C_WHITE);
  if (show_buttons) round_buttons(!ui_busy() || V.nplay == 0);
  move_hints(HAND_Y);
  /* the selected hand, shown in the hand box before playing it */
  if (!ui_busy() && R.phase == PH_ROUND) {
    int h = preview_hand();
    if (h >= 0) {
      V.hand_name = (int16_t)h, V.hand_level = R.hands[h].level;
      V.chips = hand_chips(h), V.mult = hand_mult(h);
      if (blind_debuffs_hand()) V.chips = 0, V.mult = 0;
    } else
      V.hand_name = -1, V.chips = 0, V.mult = 0;
    V.flame = 0;
#ifndef __ELF__
    /* simulator only: BALATRO_PREVIEW=hand shows that hand (screenshots) */
    extern char *getenv(const char *);
    const char *pv = getenv("BALATRO_PREVIEW");
    if (pv) {
      int ph = 0;
      while (*pv >= '0' && *pv <= '9') ph = ph * 10 + *pv++ - '0';
      ph %= NHANDS;
      V.hand_name = (int16_t)ph, V.hand_level = R.hands[ph].level, V.chips = hand_chips(ph), V.mult = hand_mult(ph);
    }
#endif
  }
  draw_menu();
  draw_popups();
  draw_focus_tooltip();
}

/* ------------------------------------------------------------ blind select */
static int bs_buttons(int *kind) {
  int n = 0;
  kind[n++] = 0; /* select */
  if (R.blind_on < 2) kind[n++] = 1; /* skip */
  if (R.blind_on == 2 && (has_voucher(V_RETCON) || (has_voucher(V_DIRECTORS_CUT) && !R.boss_rerolled))) kind[n++] = 2;
  return n;
}

static void draw_blind_select(int dt) {
  layout_cards(dt, 0, HAND_Y);
  draw_sidebar();
  draw_jokers_row(zone == Z_JOKERS ? zi : -1, zone == Z_CONS ? zi : -1, zone == Z_JOKERS ? menu_open : -1,
                  zone == Z_CONS ? menu_open : -1);
  move_hints(HAND_Y);
  int kinds[3];
  bs_buttons(kinds);
  int tip_tag = -1, tip_x = 0, tip_y = 0;
  /* three columns filling the play area, the one to play raised and outlined */
  const int w = (AREA_W - 6) / 3, y = 66;
  for (int k = 0; k < 3; k++) {
    int b = k == 0 ? BL_SMALL : k == 1 ? BL_BIG : R.boss;
    int x = AREA_X + k * (w + 3);
    int on = k == R.blind_on;
    int st = R.blind_state[k];
    int yy = y + (on ? 0 : 12);
    C main = k == 0 ? mix565(C_BLACK, C_BLUE, 19) : k == 1 ? mix565(C_BLACK, C_ORANGE, 19) : HEXC(blind_info[b].colour);
    if (st == BS_DEFEATED || st == BS_SKIPPED) main = C_BLACK;
    g_rrect(x - 2, yy - 2, w + 4, 184, 7, on ? C_WHITE : HEXC(0x1D2628));
    g_rrect(x, yy, w, 180, 6, mix565(main, C_BLACK, 16));
    const char *lab = st == BS_SELECT ? T("Select") : st == BS_DEFEATED ? T("Defeated") : st == BS_SKIPPED ? T("Skipped") : T("Upcoming");
    int focused = on && zone == Z_BTN && kinds[zi] == 0;
    button(x + 5, yy + 4, w - 10, 16, on ? C_ORANGE : HEXC(0x4F6367), lab, F_LABEL, focused, on);
    /* the name: medium, smaller when it is long (the game shrinks it too) */
    g_rrect(x + 1, yy + 24, w - 2, 15, 3, readable(main, 0));
    const char *nm = blind_name(b);
    g_text_box(nm, x + 1, yy + 24, w - 2, 15, text_fit(nm, w - 4, T_MED) | T_SHADOW, C_WHITE);
    draw_blind_chip(b, x + w / 2 - 13, yy + 43, 256);
    int dy = yy + 72;
    if (b >= BL_OX) {
      static char d[80], d2[96];
      desc_text(d, sizeof d, TK_BLIND, b, 0);
      wrap_text(d2, sizeof d2, d, w - 8, F_TEXT);
      g_text(d2, x + w / 2, dy, F_LABEL | T_CENTER, C_WHITE);
      dy += g_lines(d2) * LINE_H + 2;
    }
    g_rrect(x + 2, dy, w - 4, 38, 3, HEXC(0x1D2B2C));
    g_text(T("Score at least"), x + w / 2, dy + 3, F_LABEL | T_CENTER, C_WHITE);
    char n[24];
    int f = num_fit(n, blind_amount(R.ante) * blind_info[b].mult2 / 2, w - 12, T_LARGE);
    g_text(n, x + w / 2, dy + 13 + (11 - g_cap(f) + 1) / 2, f | T_CENTER | T_SHADOW, C_RED);
    label_dollars(T("Reward:"), blind_info[b].dollars, x + w / 2, dy + 28, w - 8);
    dy += 42;
    if (k < 2) {
      g_text(T("or"), x + w / 2, dy, F_LABEL | T_CENTER, C_WHITE);
      int sf = on && zone == Z_BTN && kinds[zi] == 1;
      int tag = R.skip_tag[k];
      if (st == BS_SKIPPED || st == BS_DEFEATED) g_sprite(SPR_T + tag, x + w / 2 - 9, dy + 10, FX_GREY, 256);
      else {
        button(x + 4, dy + 11, w - 29, 17, on ? C_RED : HEXC(0x7A2F2A), T("Skip Blind"), F_LABEL, sf, on);
        g_sprite(SPR_T + tag, x + w - 23, dy + 10, on ? 0 : FX_DIM, 256);
      }
      if (sf) tip_tag = tag, tip_x = x + w - 23, tip_y = dy + 10;
    } else if (on && (has_voucher(V_RETCON) || (has_voucher(V_DIRECTORS_CUT) && !R.boss_rerolled))) {
      int rf = zone == Z_BTN && kinds[zi] == 2;
      button(x + 4, dy + 2, w - 8, 24, C_RED, T("Reroll Boss\n$10"), F_LABEL, rf, R.money >= 10);
    }
  }
  draw_menu();
  draw_popups();
  if (!ui_busy() && tip_tag >= 0) {
    int orb = -1;
    draw_tooltip_for(TK_TAG, tip_tag, 0, &orb, tip_x, tip_y, 19);
  }
  if (zone == Z_JOKERS || zone == Z_CONS) draw_focus_tooltip();
}

static void blind_select_input(void) {
  int kinds[3], nb = bs_buttons(kinds);
  if (zone == Z_JOKERS || zone == Z_CONS) {
    if (menu_open >= 0) {
      menu_input();
      return;
    }
    if (move_input()) return;
    if (row_nav()) return;
    if (pressed[K_DOWN] || pressed[K_BACK]) zone = Z_BTN, zi = 0;
    if (pressed[K_OK]) menu_open = zi, menu_sel = 0;
    clamp_focus();
    return;
  }
  zone = Z_BTN;
  if (zi >= nb) zi = nb - 1;
  if (pressed[K_DOWN] || pressed[K_RIGHT]) zi = (zi + 1) % nb;
  if (pressed[K_LEFT]) zi = (zi + nb - 1) % nb;
  if (pressed[K_UP]) {
    if (zi == 0 && (V.njokers || V.ncons)) zone = V.njokers ? Z_JOKERS : Z_CONS, zi = 0;
    else zi = (zi + nb - 1) % nb;
  }
  if (pressed[K_BACK]) overlay = OV_OPTIONS, ov_sel = 0;
  if (pressed[K_OK] || pressed[K_EXE]) {
    if (kinds[zi] == 0) {
      blind_select();
      zone = Z_HAND, zi = 0;
    } else if (kinds[zi] == 1) {
      blind_skip();
      zi = 0;
      if (R.phase == PH_PACK) zone = Z_PACK;
    } else if (R.money >= 10)
      boss_reroll();
    else
      deny();
    autosave();
  }
}

/* ------------------------------------------------------------ cash out */
static int cash_t;

static void dollar_row(char *t, int n) {
  for (int i = 0; i < n && i < 12; i++) *t++ = '$';
  *t = 0;
}

/* the round's earnings, one row at a time as in the game: a big number or
   name on the left, its text, and the dollars on the right */
static void draw_cashout(int dt) {
  layout_cards(dt, 0, HAND_Y);
  draw_sidebar();
  draw_jokers_row(-1, -1, -1, -1);
  char t[48];
  int x = AREA_X + 3, w = AREA_W - 6, y = 64;
  cash_t += dt;
  /* the panel wraps its rows, as in the game */
  int ph = 40 + 33 + (R.cash_hands > 0) * 16 + R.ncash_joker * 16 + (R.cash_tag ? 18 : 0) + (R.cash_interest ? 16 : 0) + 6;
  if (ph > 174) ph = 174;
  g_rrect(x - 2, y - 2, w + 4, ph + 4, 7, HEXC(0x1A2224));
  g_rrect(x, y, w, ph, 6, HEXC(0x3A5055));
  g_rrect(x + 3, y + 3, w - 6, ph - 6, 5, HEXC(0x2B3A3D));
  fmt_money(str_cat(t, T("Cash Out: ")), R.cash_total);
  button(x + 10, y + 8, w - 20, 24, C_ORANGE, t, T_LARGE, 1, 1);
  int yy = y + 40, appear = 250;
  const int lx = x + 8, rx = x + w - 8;
  if (cash_t < appear) goto done;
  draw_blind_chip(R.blind, lx - 2, yy, 256);
  g_text(T("Score at least"), lx + 28, yy + 1, F_LABEL, C_WHITE);
  {
    char n[24];
    int f = num_fit(n, R.blind_chips, rx - 40 - (lx + 42), T_LARGE);
    g_sprite(SPR_CHIP, lx + 28, yy + 12, 0, 256);
    g_text(n, lx + 42, yy + 12 + (11 - g_cap(f) + 1) / 2, f | T_SHADOW, C_RED);
  }
  dollar_row(t, R.cash_blind);
  g_text(t, rx, yy + 10, F_NAME | T_RIGHT, C_MONEY);
  yy += 31;
  for (int d = lx; d < rx; d += 4) g_rect(d, yy - 2, 2, 1, HEXC(0x6A7D80));
  yy += 2;
  /* rows: 15 pixels each */
  if (R.cash_hands > 0) {
    if (cash_t < (appear += 300)) goto done;
    fmt_int(t, R.cash_hands);
    g_text(t, lx, yy, F_VALUE, C_BLUE);
    g_text(T("Remaining Hands ($1 each)"), lx + 3 + g_textw(t, T_LARGE), yy + 4, F_LABEL, C_WHITE);
    dollar_row(t, R.cash_hands);
    g_text(t, rx, yy + 2, F_NAME | T_RIGHT, C_MONEY);
    yy += 16;
  }
  for (int k = 0; k < R.ncash_joker; k++) {
    if (cash_t < (appear += 300)) goto done;
    int ji = R.cash_joker_id[k];
    if (ji < R.njokers) g_text(text_get(TXT_JOKER + R.jokers[ji].id, 0), lx, yy + 2, F_NAME, C_ATTN);
    dollar_row(t, R.cash_joker[k]);
    g_text(t, rx, yy + 2, F_NAME | T_RIGHT, C_MONEY);
    yy += 16;
  }
  if (R.cash_tag) {
    if (cash_t < (appear += 300)) goto done;
    g_sprite(SPR_T + TAG_INVESTMENT, lx - 2, yy - 2, 0, 256);
    g_text(T("Defeat the Boss Blind"), lx + 20, yy + 4, F_LABEL, C_WHITE);
    dollar_row(t, R.cash_tag);
    g_text(t, rx, yy + 2, F_NAME | T_RIGHT, C_MONEY);
    yy += 18;
  }
  if (R.cash_interest) {
    if (cash_t < (appear += 300)) goto done;
    int amt = 1 + has_joker(J_TO_THE_MOON);
    fmt_int(t, R.cash_interest);
    g_text(t, lx, yy, F_VALUE, C_ATTN);
    int tx = lx + 3 + g_textw(t, T_LARGE);
    char *o = fmt_int(t, amt);
    o = str_cat(o, T(" interest per $5 ("));
    o = fmt_int(o, R.interest_cap / 5 * amt);
    str_cat(o, T(" max)"));
    g_text(t, tx, yy + 4, F_LABEL, C_WHITE);
    dollar_row(t, R.cash_interest);
    g_text(t, rx, yy + 2, F_NAME | T_RIGHT, C_MONEY);
    yy += 16;
  }
done:
  draw_popups();
}

/* ------------------------------------------------------------ shop */
#define SHOP_X AREA_X
#define SHOP_Y 62
#define SHOP_W AREA_W

/* the shop cards, centred in their dark box (x + 68 .. right - 6) */
static int shop_item_x(int i, int n) {
  int area = SHOP_W - 74 - 8, step = n > 1 ? (area - CW) / (n - 1) : 0;
  if (step > CW + 10) step = CW + 10;
  int total = step * (n - 1) + CW;
  return SHOP_X + 68 + 4 + (area - total) / 2 + i * step;
}
static int bottom_n(void) { return R.nshop_v + R.nshop_p; }
/* vouchers in the left box (x + 4 .. + 92), packs in the right one */
static int bottom_x(int i) {
  if (i < R.nshop_v) return R.nshop_v == 1 ? SHOP_X + 4 + 50 - CW / 2 : SHOP_X + 22 + i * 24;
  int np = R.nshop_p, bx = SHOP_X + 96, bw = SHOP_W - 100;
  int step = CW + 12, total = step * (np - 1) + CW;
  return bx + (bw - total) / 2 + (i - R.nshop_v) * step;
}
static sitem_t *bottom_item(int i) { return i < R.nshop_v ? &R.shop_v[i] : &R.shop_p[i - R.nshop_v]; }
static int affordable(int cost) { return cost == 0 || R.money + 20 * has_joker(J_CREDIT_CARD) >= cost; }

static void draw_shop(int dt) {
  layout_cards(dt, 0, HAND_Y);
  draw_sidebar();
  int fj = zone == Z_JOKERS ? zi : -1, fc = zone == Z_CONS ? zi : -1;
  draw_jokers_row(fj, fc, zone == Z_JOKERS ? menu_open : -1, zone == Z_CONS ? menu_open : -1);
  move_hints(HAND_Y);
  int x = SHOP_X, y = SHOP_Y, w = SHOP_W;
  g_rrect(x - 1, y - 1, w + 2, 180, 7, HEXC(0x1A2224));
  g_rrect(x, y, w, 178, 6, C_RED);
  g_rrect(x + 2, y + 2, w - 4, 174, 5, HEXC(0x3B4B4E));
  char t[24];
  int fb = zone == Z_SHOPBTN ? zi : -1;
  button(x + 6, y + 6, 58, 36, C_RED, T("Next\nRound"), F_LABEL, fb == 0, 1);
  /* Reroll: the word small, the price large */
  int ok = affordable(R.reroll_cost);
  button(x + 6, y + 46, 58, 36, C_GREEN, "", F_LABEL, fb == 1, ok);
  int sx = x + 6 + (fb == 1 ? focus_shake : 0);
  g_text(T("Reroll"), sx + 29, y + 50, F_LABEL | T_CENTER, ok ? C_WHITE : HEXC(0xBBBBBB));
  fmt_money(t, R.reroll_cost);
  g_text(t, sx + 29, y + 62, F_VALUE | T_CENTER, ok ? C_WHITE : HEXC(0xBBBBBB));
  g_shade(x + 68, y + 6, w - 74, 76, 4, 10);
  for (int i = 0; i < R.nshop; i++) {
    int ix = shop_item_x(i, R.nshop), iy = y + 27;
    int f = zone == Z_SHOP && zi == i;
    draw_item(&R.shop[i], ix, iy - (f ? 4 : 0) - (menu_open == i && zone == Z_SHOP ? 3 : 0), 256, 0);
  }
  for (int i = 0; i < R.nshop; i++) { /* price tags stay on top of a lifted card */
    int cost = item_cost(&R.shop[i]);
    draw_price(shop_item_x(i, R.nshop) + CW / 2, y + 8, cost, affordable(cost));
  }
  /* the voucher box, its name along the left edge as in the game */
  g_shade(x + 4, y + 88, 88, 84, 4, 10);
  char *o = str_cat(t, T("ANTE "));
  o = fmt_int(o, R.ante);
  str_cat(o, T(" VOUCHER"));
  g_text(t, x + 9, y + 130, T_SMALL | T_ROT | T_CENTER, HEXC(0x7D8E91));
  g_shade(x + 96, y + 88, w - 100, 84, 4, 10);
  for (int i = 0; i < bottom_n(); i++) {
    sitem_t *it = bottom_item(i);
    int ix = bottom_x(i), iy = y + 111;
    int f = zone == Z_BOTTOM && zi == i;
    draw_item(it, ix, iy - (f ? 4 : 0) - (menu_open == i && zone == Z_BOTTOM ? 3 : 0), 256, 0);
  }
  for (int i = 0; i < bottom_n(); i++) {
    int cost = item_cost(bottom_item(i));
    draw_price(bottom_x(i) + CW / 2, y + 92, cost, affordable(cost));
  }
  if (menu_open >= 0 && (zone == Z_SHOP || zone == Z_BOTTOM)) {
    sitem_t *it = zone == Z_SHOP ? &R.shop[menu_open] : bottom_item(menu_open);
    int ix = zone == Z_SHOP ? shop_item_x(menu_open, R.nshop) : bottom_x(menu_open);
    int iy = zone == Z_SHOP ? y + 27 + CH - 4 : y + 111 + CH - 14;
    const char *lab = it->kind == IT_VOUCHER ? T("REDEEM") : it->kind == IT_PACK ? T("OPEN") : T("BUY");
    button(ix - 4, iy, 43, 15, C_GREEN, lab, F_LABEL, menu_sel == 0, affordable(item_cost(it)));
    if (it->kind == IT_CONS)
      button(ix - 4, iy + 18, 43, 22, C_RED, T("BUY\n& USE"), F_LABEL, menu_sel == 1, cons_usable(it->id, 1));
  }
  draw_menu();
  draw_popups();
  if (!ui_busy() && menu_open < 0) {
    if (zone == Z_SHOP && zi < R.nshop) {
      sitem_t *it = &R.shop[zi];
      int ix = shop_item_x(zi, R.nshop);
      if (it->kind == IT_JOKER) {
        joker_t j = {0};
        j.id = it->id;
        joker_init(&j);
        if (it->id == J_TODO_LIST) j.c = (uint8_t)it->a;
        draw_tooltip_for(TK_JOKER, it->id, it->ed, &j, ix, y + 27, CW);
      } else if (it->kind == IT_CONS)
        draw_tooltip_for(TK_CONS, it->id, it->ed, 0, ix, y + 27, CW);
      else if (it->kind == IT_CARD) {
        vcard_t v = {0};
        v.rank = it->card.rank, v.suit = it->card.suit, v.enh = it->card.enh, v.ed = it->card.ed, v.seal = it->card.seal;
        draw_tooltip_for(TK_CARD, 0, 0, &v, ix, y + 27, CW);
      }
    } else if (zone == Z_BOTTOM && zi < bottom_n()) {
      sitem_t *it = bottom_item(zi);
      draw_tooltip_for(it->kind == IT_VOUCHER ? TK_VOUCHER : TK_PACK, it->id, 0, 0, bottom_x(zi), y + 111, CW);
    } else if (zone == Z_JOKERS || zone == Z_CONS)
      draw_focus_tooltip();
  }
}

static void shop_input(void) {
  if (menu_open >= 0 && (zone == Z_JOKERS || zone == Z_CONS)) {
    menu_input();
    return;
  }
  if (menu_open >= 0) {
    sitem_t *it = zone == Z_SHOP ? &R.shop[menu_open] : bottom_item(menu_open);
    int n = it->kind == IT_CONS ? 2 : 1;
    if (pressed[K_UP] && menu_sel > 0) menu_sel--;
    if (pressed[K_DOWN] && menu_sel < n - 1) menu_sel++;
    if (pressed[K_BACK] || pressed[K_LEFT] || pressed[K_RIGHT]) menu_open = -1;
    else if (pressed[K_OK] || pressed[K_EXE]) {
      int area = zone == Z_SHOP ? 0 : menu_open < R.nshop_v ? 1 : 2;
      int idx = zone == Z_SHOP || menu_open < R.nshop_v ? menu_open : menu_open - R.nshop_v;
      if (menu_sel == 1 && !cons_usable(it->id, 1)) deny();
      else if (!shop_buy(area, idx, menu_sel == 1)) deny();
      else {
        autosave();
        if (R.phase == PH_PACK) zone = Z_PACK, zi = 0;
      }
      menu_open = -1;
    }
    return;
  }
  if (move_input()) return;
  if (pressed[K_BACK]) {
    if (zone != Z_SHOPBTN) zone = Z_SHOPBTN, zi = 0;
    else overlay = OV_OPTIONS, ov_sel = 0;
    return;
  }
  if (row_nav()) return;
  switch (zone) {
    case Z_SHOPBTN:
      if (pressed[K_UP]) {
        if (zi > 0) zi--;
        else up_to_row(120);
      }
      if (pressed[K_DOWN]) {
        if (zi < 1) zi++;
        else if (bottom_n()) zone = Z_BOTTOM, zi = 0;
      }
      if (pressed[K_RIGHT]) {
        if ((zi == 0 || !bottom_n()) && R.nshop) zone = Z_SHOP, zi = 0;
        else if (bottom_n()) zone = Z_BOTTOM, zi = 0;
      }
      if (pressed[K_OK] || pressed[K_EXE]) {
        if (zi == 0) {
          shop_leave();
          zone = R.phase == PH_PACK ? Z_PACK : Z_BTN, zi = 0;
          autosave();
        } else if (!shop_reroll()) deny();
        else autosave();
      }
      break;
    case Z_SHOP:
      if (pressed[K_LEFT]) {
        if (zi > 0) zi--;
        else zone = Z_SHOPBTN, zi = 0;
      }
      if (pressed[K_RIGHT] && zi < R.nshop - 1) zi++;
      if (pressed[K_UP]) up_to_row(item_x(Z_SHOP, 0));
      if (pressed[K_DOWN]) {
        if (bottom_n()) zone = Z_BOTTOM, zi = R.nshop_v < bottom_n() ? R.nshop_v : 0;
        else zone = Z_SHOPBTN, zi = 1;
      }
      if ((pressed[K_OK] || pressed[K_EXE]) && zi < R.nshop) menu_open = zi, menu_sel = 0;
      if (!R.nshop) zone = Z_SHOPBTN, zi = 0;
      break;
    case Z_BOTTOM:
      if (pressed[K_LEFT]) {
        if (zi > 0) zi--;
        else zone = Z_SHOPBTN, zi = 1;
      }
      if (pressed[K_RIGHT] && zi < bottom_n() - 1) zi++;
      if (pressed[K_UP]) {
        if (R.nshop) zone = Z_SHOP, zi = 0;
        else zone = Z_SHOPBTN, zi = 1;
      }
      if ((pressed[K_OK] || pressed[K_EXE]) && zi < bottom_n()) menu_open = zi, menu_sel = 0;
      if (!bottom_n()) zone = Z_SHOPBTN, zi = 0;
      break;
    case Z_JOKERS: case Z_CONS:
      if (pressed[K_DOWN]) zone = R.nshop ? Z_SHOP : Z_SHOPBTN, zi = 0;
      if (pressed[K_OK]) menu_open = zi, menu_sel = 0;
      break;
    default: zone = Z_SHOPBTN, zi = 0;
  }
  clamp_focus();
}

/* ------------------------------------------------------------ packs */
static int pack_x(int i) {
  int n = R.npack, area = AREA_W, step = n > 1 ? (area - CW) / (n - 1) : 0;
  if (step > CW + 8) step = CW + 8;
  int total = step * (n - 1) + CW;
  return AREA_X + (area - total) / 2 + i * step;
}

static void draw_pack(int dt) {
  int with_hand = R.pack_kind == PK_ARCANA || R.pack_kind == PK_SPECTRAL;
  layout_cards(dt, with_hand, HAND_Y + 18);
  draw_sidebar();
  draw_jokers_row(zone == Z_JOKERS ? zi : -1, zone == Z_CONS ? zi : -1, zone == Z_JOKERS ? menu_open : -1,
                  zone == Z_CONS ? menu_open : -1);
#if !NP_TEXT_EXTRA
  static const char *const names[5] = {"Arcana Pack", "Celestial Pack", "Standard Pack", "Buffoon Pack",
                                       "Spectral Pack"};
  static const char *const sizes[3] = {"", "Jumbo ", "Mega "};
#endif
  int py = 70;
  for (int i = 0; i < R.npack; i++) {
    int f = zone == Z_PACK && zi == i;
    draw_item(&R.pack[i], pack_x(i), py - (f ? 5 : 0), 256, 0);
  }
  /* the pack's name and how many to choose, Skip on the right */
  int bw = 170, bx = AREA_X + (AREA_W - bw) / 2, by = with_hand ? 132 : 138, bh = 28;
  panel(bx, by, bw, bh, 5, HEXC(0x2F3A3C), 2);
  char t[32];
#if NP_TEXT_EXTRA
  /* (the packs' names among the texts: other languages put the size elsewhere) */
  char *o = str_cat(t, text_get(TXT_EXTRA + 16 + R.pack_kind * 3 + R.pack_size, 0));
#else
  char *o = str_cat(t, sizes[R.pack_size]);
  str_cat(o, names[R.pack_kind]);
#endif
  g_text(t, bx + 6, by + 4, text_fit(t, bw - 56, T_MED) | T_SHADOW, C_WHITE);
  o = str_cat(t, T("Choose "));
  fmt_int(o, R.pack_picks);
  g_text(t, bx + 6, by + 17, F_LABEL, C_WHITE);
  button(bx + bw - 42, by + 6, 36, 16, C_RED, T("Skip"), F_LABEL, zone == Z_SKIP, 1);
  if (with_hand) draw_hand_cards(hand_focus());
  move_hints(HAND_Y + 18);
  draw_menu();
  draw_popups();
  if (!ui_busy() && menu_open < 0) {
    if (zone == Z_PACK && zi < R.npack) {
      sitem_t *it = &R.pack[zi];
      int ix = pack_x(zi);
      int ok = it->kind != IT_CONS || cons_usable(it->id, 1);
      if (it->kind == IT_JOKER && R.njokers >= joker_slots() && it->ed != ED_NEG) ok = 0;
      button(ix - 4, py + CH - 3, 43, 15, it->kind == IT_CONS ? C_RED : C_GREEN, it->kind == IT_CONS ? T("USE") : T("SELECT"),
             F_LABEL, 1, ok);
      if (it->kind == IT_JOKER) {
        joker_t j = {0};
        j.id = it->id;
        joker_init(&j);
        draw_tooltip_for(TK_JOKER, it->id, it->ed, &j, ix, py, CW);
      } else if (it->kind == IT_CONS)
        draw_tooltip_for(TK_CONS, it->id, it->ed, 0, ix, py, CW);
      else {
        vcard_t v = {0};
        v.rank = it->card.rank, v.suit = it->card.suit, v.enh = it->card.enh, v.ed = it->card.ed, v.seal = it->card.seal;
        draw_tooltip_for(TK_CARD, 0, 0, &v, ix, py, CW);
      }
    } else
      draw_focus_tooltip();
  }
}

static void pack_input(void) {
  int with_hand = R.pack_kind == PK_ARCANA || R.pack_kind == PK_SPECTRAL;
  if (menu_open >= 0) {
    menu_input();
    return;
  }
  if (move_input()) return;
  if (pressed[K_BACK]) {
    zone = Z_SKIP;
    return;
  }
  if (row_nav()) return;
  switch (zone) {
    case Z_PACK:
      if (pressed[K_LEFT] && zi > 0) zi--;
      if (pressed[K_RIGHT] && zi < R.npack - 1) zi++;
      if (pressed[K_DOWN]) zone = Z_SKIP;
      if (pressed[K_UP]) up_to_row(item_x(Z_PACK, zi));
      if (pressed[K_OK] || pressed[K_EXE]) {
        if (!pack_choose(zi)) deny();
        else {
          autosave();
          if (R.phase != PH_PACK) zone = R.phase == PH_SHOP ? Z_SHOPBTN : Z_BTN, zi = 0;
        }
      }
      break;
    case Z_HAND:
      if (pressed[K_LEFT] && zi > 0) zi--;
      if (pressed[K_RIGHT] && zi < V.nhand - 1) zi++;
      if (pressed[K_UP]) zone = Z_SKIP;
      if (pressed[K_OK] && zi < R.nhand) {
        toggle_select(zi);
        V.cards[R.hand[zi]].raise = R.sel[zi];
      }
      break;
    case Z_SKIP:
      if (pressed[K_UP]) zone = Z_PACK, zi = 0;
      if (pressed[K_DOWN] && with_hand) zone = Z_HAND, zi = 0;
      if (pressed[K_OK] || pressed[K_EXE]) {
        pack_skip();
        autosave();
        zone = R.phase == PH_SHOP ? Z_SHOPBTN : R.phase == PH_PACK ? Z_PACK : Z_BTN, zi = 0;
      }
      break;
    case Z_JOKERS: case Z_CONS:
      if (pressed[K_DOWN]) zone = Z_PACK, zi = 0;
      if (pressed[K_OK]) menu_open = zi, menu_sel = 0;
      break;
    default: zone = Z_PACK, zi = 0;
  }
  clamp_focus();
}

/* ------------------------------------------------------------ game over / win */
static const char *const quips_lose[] = {T("Better luck\nnext time!"), T("Don't give up!"), T("So close!"),
                                         T("The house\nalways wins...")};
static const char *const quips_win[] = {T("You did it!"), T("Unbelievable!"), T("A true\ncard shark!")};

static void stat_row(int x, int w, int y, const char *label, const char *value, C col) {
  g_text(label, x + 14, y + 2, F_LABEL, C_WHITE);
  g_text(value, x + w - 14, y, text_fit(value, w / 2, T_MED) | T_RIGHT | T_SHADOW, col);
}

static void draw_end(int win) {
  char t[40];
  draw_sidebar();
  draw_jokers_row(-1, -1, -1, -1);
  int x = AREA_X, w = AREA_W;
  big_panel(x, 20, w, 216);
  g_text_box(win ? T("YOU WIN!") : T("GAME OVER"), x, 26, w, 26, T_LARGE | T_X2 | T_SHADOW, win ? C_ATTN : C_RED);
  draw_joker_sprite(J_JOKER, 0, x + 12, 56, 256, 0);
  const char *q = win ? quips_win[R.round % 3] : quips_lose[R.round % 4];
  panel(x + 54, 60, 104, 28, 5, C_WHITE, 1);
  g_rect(x + 50, 70, 5, 4, C_WHITE);
  g_text_box(q, x + 54, 60, 104, 28, F_TEXT, C_TEXT_DARK);
  char *o = str_cat(t, T("Ante \004"));
  o = fmt_int(o, win ? WIN_ANTE : R.ante);
  o = str_cat(o, T("\001    Round \004"));
  fmt_int(o, R.round);
  g_text(t, x + 106, 93, F_LABEL | T_CENTER, C_WHITE);
  int yy = 106;
  g_rrect(x + 8, yy - 3, w - 16, 88, 4, HEXC(0x1F2B2D));
  fmt_commas(t, R.best_hand);
  stat_row(x, w, yy, T("Best Hand"), t, C_RED);
  int mp = 0;
  for (int h = 0; h < NHANDS; h++)
    if (R.hands[h].played > R.hands[mp].played) mp = h;
  stat_row(x, w, yy + 12, T("Most Played Hand"), R.hands[mp].played ? hand_names[mp] : "-", C_ATTN);
  fmt_int(t, R.cards_played);
  stat_row(x, w, yy + 24, T("Cards Played"), t, C_BLUE);
  fmt_int(t, R.cards_discarded);
  stat_row(x, w, yy + 36, T("Cards Discarded"), t, C_RED);
  fmt_int(t, R.cards_bought);
  stat_row(x, w, yy + 48, T("Cards Purchased"), t, C_MONEY);
  fmt_int(t, R.rerolls);
  stat_row(x, w, yy + 60, T("Times Rerolled"), t, C_GREEN);
  stat_row(x, w, yy + 72, T("Seed"), R.seed, C_WHITE);
  if (win) {
    int bw = (w - 16 - 8) / 3;
    button(x + 8, 196, bw, 30, C_ORANGE, T("Endless\nMode"), T_MED, ov_sel == 0, 1);
    button(x + 12 + bw, 196, bw, 30, C_BLUE, T("New\nRun"), T_MED, ov_sel == 1, 1);
    button(x + 16 + 2 * bw, 196, bw, 30, C_RED, T("Main\nMenu"), T_MED, ov_sel == 2, 1);
  } else {
    int bw = (w - 16 - 6) / 2;
    button(x + 8, 196, bw, 30, C_BLUE, T("New Run"), T_MED, ov_sel == 0, 1);
    button(x + 14 + bw, 196, bw, 30, C_RED, T("Main Menu"), T_MED, ov_sel == 1, 1);
  }
}

static void start_new_run(void) {
  run_new(0);
  S.runs++;
  ev_reset();
  memset(&V, 0, sizeof V);
  ui_sync_all(1);
  V.hand_name = -1;
  zone = Z_BTN, zi = 0;
  in_game = 1, has_save = 1;
  screen = SCR_GAME;
  overlay = OV_NONE;
  autosave();
}

static void continue_run(void) {
  ev_reset();
  memset(&V, 0, sizeof V);
  ui_sync_all(1);
  V.hand_name = -1;
  zone = R.phase == PH_ROUND ? Z_HAND : R.phase == PH_SHOP ? Z_SHOPBTN : R.phase == PH_PACK ? Z_PACK : Z_BTN;
  zi = 0;
  in_game = 1;
  screen = SCR_GAME;
  overlay = OV_NONE;
}

static void to_title(void) {
  if (in_game) save_write(R.phase != PH_GAMEOVER);
  has_save = in_game ? R.phase != PH_GAMEOVER : has_save;
  in_game = 0;
  screen = SCR_TITLE;
  overlay = OV_NONE;
  title_sel = 0, title_t = 0;
}

static void end_input(int win) {
  int n = win ? 3 : 2;
  if (pressed[K_LEFT] || pressed[K_UP]) ov_sel = (ov_sel + n - 1) % n;
  if (pressed[K_RIGHT] || pressed[K_DOWN]) ov_sel = (ov_sel + 1) % n;
  if (pressed[K_OK] || pressed[K_EXE]) {
    int a = win ? ov_sel : ov_sel + 1;
    if (a == 0) {
      continue_endless();
      ev_reset();
      ui_sync_all(0);
      zone = Z_BTN, zi = 0;
      autosave();
    } else if (a == 1)
      start_new_run();
    else {
      if (!win) {
        in_game = 0, has_save = 0;
        save_write(0);
      }
      to_title();
    }
  }
}

/* ------------------------------------------------------------ overlays input */
static void overlay_input(void) {
  switch (overlay) {
    case OV_OPTIONS:
      {
        int n = opt_count();
        if (ov_sel >= n) ov_sel = 0;
        if (pressed[K_UP]) ov_sel = (ov_sel + n - 1) % n;
        if (pressed[K_DOWN]) ov_sel = (ov_sel + 1) % n;
      }
      if (opt_id(ov_sel) == OPT_SPEED && (pressed[K_LEFT] || pressed[K_RIGHT])) {
        S.speed = (uint8_t)((S.speed + (pressed[K_RIGHT] ? 1 : 3)) % 4);
        save_write(in_game ? R.phase != PH_GAMEOVER : has_save);
      }
      if (pressed[K_BACK]) overlay = OV_NONE;
      if (pressed[K_OK] || pressed[K_EXE]) {
        switch (opt_id(ov_sel)) {
          case OPT_CONTINUE: case OPT_BACK: overlay = OV_NONE; break;
          case OPT_SPEED:
            S.speed = (uint8_t)((S.speed + 1) % 4);
            save_write(in_game ? R.phase != PH_GAMEOVER : has_save);
            break;
          case OPT_HELP: overlay = OV_HELP; break;
          case OPT_NEWRUN: overlay = OV_CONFIRM_NEW, ov_sel = 1; break;
          case OPT_MAINMENU: to_title(); break;
        }
      }
      break;
    case OV_CONFIRM_NEW:
      if (pressed[K_LEFT] || pressed[K_RIGHT]) ov_sel ^= 1;
      if (pressed[K_BACK]) overlay = OV_OPTIONS, ov_sel = opt_index(OPT_NEWRUN);
      if (pressed[K_OK] || pressed[K_EXE]) {
        if (ov_sel == 0) start_new_run();
        else overlay = OV_OPTIONS, ov_sel = opt_index(OPT_NEWRUN);
      }
      break;
    case OV_RUNINFO:
      if (pressed[K_LEFT]) ov_tab = (ov_tab + 2) % 3;
      if (pressed[K_RIGHT]) ov_tab = (ov_tab + 1) % 3;
      if (pressed[K_BACK] || pressed[K_OK] || pressed[K_TOOLBOX]) overlay = OV_NONE;
      break;
    case OV_DECK:
      if (pressed[K_LEFT] || pressed[K_RIGHT]) ov_tab ^= 1;
      if (pressed[K_BACK] || pressed[K_OK] || pressed[K_VAR]) overlay = OV_NONE;
      break;
    case OV_HELP:
      if (pressed[K_BACK] || pressed[K_OK] || pressed[K_EXE]) overlay = OV_OPTIONS, ov_sel = opt_index(OPT_HELP);
      break;
    case OV_CREDITS:
      if (pressed[K_BACK] || pressed[K_OK] || pressed[K_EXE]) overlay = OV_NONE;
      break;
    case OV_SETUP:
      if ((pressed[K_LEFT] || pressed[K_RIGHT]) && has_save) ov_tab ^= 1;
      if (pressed[K_UP] || pressed[K_DOWN]) ov_sel ^= 1;
      if (pressed[K_BACK]) overlay = OV_NONE;
      else if (pressed[K_OK] || pressed[K_EXE]) {
        if (ov_sel == 1) overlay = OV_NONE;
        else if (ov_tab == 1 && has_save) continue_run();
        else start_new_run();
      }
      break;
  }
}

static void draw_overlay(void) {
  switch (overlay) {
    case OV_OPTIONS: draw_options(); break;
    case OV_RUNINFO: draw_run_info(); break;
    case OV_DECK: draw_deck_view(); break;
    case OV_HELP: draw_help(); break;
    case OV_CREDITS: draw_credits(); break;
    case OV_SETUP: draw_setup(); break;
    case OV_CONFIRM_NEW: draw_confirm_new(); break;
  }
}

/* ------------------------------------------------------------ main */
static int quit(void) {
  save_write(in_game ? R.phase != PH_GAMEOVER : has_save);
  return np_app_end();
}

#ifdef TEST_PHASE
/* test builds only (make EXTRA=-DTEST_PHASE=...): start in a prepared state */
static void test_preset(void) {
  run_new("TESTSEED");
  R.money = 40;
  static const uint8_t j[5] = {J_JOKER, J_FIBONACCI, J_BLUEPRINT, J_BARON, J_HOLOGRAM};
  for (int i = 0; i < 5; i++) joker_add(j[i], i == 1 ? ED_POLY : i == 3 ? ED_FOIL : 0, 1);
  add_cons(C_CHARIOT, 0);
  add_cons(C_JUPITER, 0);
#ifdef TEST_LEVEL
  for (int h = 0; h < NHANDS; h++) R.hands[h].level = TEST_LEVEL;
#endif
  if (TEST_PHASE == 1) shop_enter();
  if (TEST_PHASE == 2) pack_open(PK_ARCANA, 2, 0);
  if (TEST_PHASE == 3) blind_select();
  ev_reset();
  has_save = 1;
  continue_run();
}
#endif

#ifndef __ELF__
#include <stdlib.h>
/* simulator only: BALATRO_TEST starts a prepared run (for screenshots) */
static void test_setup(void) {
  const char *t = getenv("BALATRO_TEST");
  if (!t) return;
  run_new(getenv("BALATRO_SEED") ? getenv("BALATRO_SEED") : "TESTSEED");
  R.money = atoi(t) > 0 ? atoi(t) : 50;
  const char *j = getenv("BALATRO_JOKERS");
  while (j && *j) {
    int id = atoi(j), ed = 0;
    while (*j && *j != ',') {
      if (*j == 'f') ed = ED_FOIL;
      if (*j == 'h') ed = ED_HOLO;
      if (*j == 'p') ed = ED_POLY;
      if (*j == 'n') ed = ED_NEG;
      j++;
    }
    joker_add(id, ed, 1);
    if (*j) j++;
  }
  const char *c = getenv("BALATRO_CONS");
  while (c && *c) {
    add_cons(atoi(c), 0);
    while (*c && *c != ',') c++;
    if (*c) c++;
  }
  const char *lv = getenv("BALATRO_LEVEL");
  if (lv)
    for (int h = 0; h < NHANDS; h++) R.hands[h].level = (int16_t)atoi(lv);
  const char *an = getenv("BALATRO_ANTE");
  if (an) R.ante = (int16_t)atoi(an);
  const char *bo = getenv("BALATRO_BLINDON");
  if (bo) {
    R.blind_on = (uint8_t)atoi(bo);
    for (int k = 0; k < R.blind_on; k++) R.blind_state[k] = BS_DEFEATED;
    R.blind_state[R.blind_on] = BS_SELECT;
  }
  const char *bs = getenv("BALATRO_BOSS");
  if (bs) R.boss = (uint8_t)atoi(bs);
  const char *ph = getenv("BALATRO_PHASE");
  if (ph && *ph == 's') shop_enter();
  if (ph && *ph == 'p') pack_open(atoi(ph + 1) / 3, atoi(ph + 1) % 3, 0);
  if (ph && *ph == 'r') blind_select();
  /* BALATRO_HAND=13S,13H,...: the first cards in hand (rank 2-14, suit SHCD) */
  const char *hd = getenv("BALATRO_HAND");
  for (int i = 0; hd && *hd && i < R.nhand; i++) {
    card_t *c = &R.cards[R.hand[i]];
    c->rank = (uint8_t)atoi(hd);
    while (*hd >= '0' && *hd <= '9') hd++;
    c->suit = (uint8_t)(*hd == 'H' ? S_HEARTS : *hd == 'C' ? S_CLUBS : *hd == 'D' ? S_DIAMONDS : S_SPADES);
    while (*hd && *hd != ',') hd++;
    if (*hd) hd++;
  }
  ev_reset();
  has_save = 1;
  continue_run();
}
#endif

int main(void) {
  np_app_begin();
  if (np_save_jump(leave)) return quit();
  g_init();
  art_flush();
  has_save = save_read();
  bg_mode(1);
  bg_reset();
#ifndef __ELF__
  test_setup();
#endif
#ifdef TEST_PHASE
  test_preset();
#endif
  uint64_t last = eadk_timing_millis();
  int last_phase = -1, last_blind = -1, last_screen = -1, last_won = -1;
  for (;;) {
    uint64_t now = eadk_timing_millis();
    int dt = (int)(now - last);
    if (dt > 100) dt = 100;
    if (dt < 0) dt = 0;
    last = now;
    g_time = (uint32_t)now;
    poll_input(dt);
    if (held(K_OK) || held(K_EXE)) ui_hold_ms += dt;
    else ui_hold_ms = 0;
    if (deny_t > 0) deny_t -= dt;
    if (zone != last_zone || zi != last_zi) focus_time = 0, last_zone = zone, last_zi = zi;
    else focus_time += dt;
    focus_shake = deny_t > 0 ? ((deny_t / 50) & 1 ? 2 : -2) : 0;
    if (screen == SCR_GAME && R.phase == PH_WIN && last_phase != PH_WIN && last_phase >= 0) {
      S.wins++;
      save_write(1);
    }
    if (screen != last_screen || (screen == SCR_GAME && (R.phase != last_phase || R.blind != last_blind || R.won != last_won))) {
      bg_for_phase();
      last_phase = R.phase, last_blind = R.blind, last_won = R.won;
      cash_t = 0;
      last_screen = screen;
    }
    if (overlay) moving = 0;
    g_begin();
    g_bg(1, 0);
    if (screen == SCR_TITLE) {
      title_t += dt;
      draw_title();
      if (overlay) {
        overlay_input();
        draw_overlay();
      } else {
        if (pressed[K_LEFT]) title_sel = (title_sel + 3) % 4;
        if (pressed[K_RIGHT]) title_sel = (title_sel + 1) % 4;
        if (pressed[K_BACK]) return quit();
        if (pressed[K_OK] || pressed[K_EXE]) {
          if (title_sel == 0) overlay = OV_SETUP, ov_tab = has_save ? 1 : 0, ov_sel = 0;
          else if (title_sel == 1) overlay = OV_OPTIONS, ov_sel = 0;
          else if (title_sel == 3) overlay = OV_CREDITS;
          else return quit();
        }
      }
    } else {
      ui_update(dt);
      int busy = ui_busy();
      /* taps made while cards are still moving are kept (a few, briefly) and
         played back one per frame as soon as the game is ready; never
         Play/Discard, never a key still held down */
      static const uint8_t bufk[6] = {K_LEFT, K_UP, K_DOWN, K_RIGHT, K_OK, K_SHIFT};
      if (busy && !overlay) {
        for (int i = 0; i < nbuf; i++) buf_age[i] += dt;
        for (int i = 0; i < 6; i++)
          if (pressed[bufk[i]]) {
            if (nbuf == 4) {
              for (int k = 0; k < 3; k++) buf_key[k] = buf_key[k + 1], buf_age[k] = buf_age[k + 1];
              nbuf--;
            }
            buf_key[nbuf] = bufk[i], buf_age[nbuf++] = 0;
          }
      } else if (nbuf) {
        int k = buf_key[0];
        if (!overlay && buf_age[0] < 700 && !held(k)) pressed[k] = 1;
        for (int i = 0; i < nbuf - 1; i++) buf_key[i] = buf_key[i + 1], buf_age[i] = buf_age[i + 1];
        nbuf--;
        if (overlay) nbuf = 0;
      }
      if (overlay) {
        overlay_input();
      } else if (!busy) {
        if (pressed[K_TOOLBOX]) overlay = OV_RUNINFO, ov_tab = 0;
        else if (pressed[K_VAR]) overlay = OV_DECK, ov_tab = 0;
        else switch (R.phase) {
            case PH_BLIND_SELECT: blind_select_input(); break;
            case PH_ROUND: round_input(); break;
            case PH_CASHOUT:
              if (pressed[K_OK] || pressed[K_EXE]) {
                cash_out();
                zone = Z_SHOPBTN, zi = 0;
                autosave();
              } else if (pressed[K_BACK])
                overlay = OV_OPTIONS, ov_sel = 0;
              break;
            case PH_SHOP: shop_input(); break;
            case PH_PACK: pack_input(); break;
            case PH_GAMEOVER: end_input(0); break;
            case PH_WIN: end_input(1); break;
          }
      }
      switch (R.phase) {
        case PH_BLIND_SELECT:
          if (busy && V.nhand) draw_round(dt, 0);
          else draw_blind_select(dt);
          break;
        case PH_ROUND: draw_round(dt, 1); break;
        case PH_CASHOUT:
          if (busy) draw_round(dt, 0);
          else draw_cashout(dt);
          break;
        case PH_SHOP: draw_shop(dt); break;
        case PH_PACK: draw_pack(dt); break;
        case PH_GAMEOVER:
          if (busy) draw_round(dt, 0);
          else draw_end(0);
          break;
        case PH_WIN:
          if (busy) draw_round(dt, 0);
          else draw_end(1);
          break;
      }
      if (S.best_ante < R.max_ante_reached) S.best_ante = R.max_ante_reached;
      if (R.best_hand > S.best_hand) S.best_hand = R.best_hand;
      draw_overlay();
    }
    /* the title's paint keeps moving at its own pace behind a panel (Options, the new run...): 8 rows of it a frame,
       fewer only while a frame takes too long (a page full of text on the N0110). Run Info and the deck cover the
       screen in a run. */
    static int title_rows = 8;
    bg_update(dt / 1000.f, overlay == OV_RUNINFO || overlay == OV_DECK ? 0 : screen == SCR_TITLE ? title_rows : 6);
    g_end();
    uint64_t spent = eadk_timing_millis() - now;
    if (screen == SCR_TITLE) {
      if (spent > 30 && title_rows > 2) title_rows--;
      else if (spent < 26 && title_rows < 8) title_rows++;
    }
    if (spent < 30) eadk_timing_msleep((uint32_t)(30 - spent));
  }
}
