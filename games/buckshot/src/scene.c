#include "scene.h"
#include <eadk.h>

table_t T;
void (*redraw)(void);

/* first sprite of each item's 16 table slots: it<i><p|d><g> */
static int item_img(int side, int slot, int it) {
  return IMG_IT0P0 + it * 16 + side * 8 + slot;
}
/* the Dealer's items in his view */
static int ditem_img(int slot, int it) { return IMG_DI00 + it * 8 + slot; }

void table_reset(void) {
  T = (table_t){0};
  T.pose = T.pose2 = -1;
  T.gun_on_table = 1;
  T.held = -1;
  T.cursor = -1;
  view_table();
}

static void dealer_idle(void) {
  T.d.dy = 0;
  T.d.gone = 0;
  T.d.dim = 0;
  T.d.alone = -1;
  if (T.view == V_TABLE) {
    T.d.head = IMG_T_HEAD0;
    T.d.hands = G.dcuffed ? IMG_T_CUFFED : IMG_T_HANDS;
    T.d.pose = -1;
  } else {
    T.d.head = IMG_D_HEAD;
    T.d.hands = IMG_D_HANDS;
    T.d.pose = G.dcuffed ? IMG_D_CUFFED : -1;
  }
  T.gun_on_table = 1;
}

void view_table(void) {
  T.view = V_TABLE;
  dealer_idle();
}

void view_dealer(void) {
  T.view = V_DEALER;
  dealer_idle();
}

void ui_hint(const char *s) {
  if (s) gfx_text(&font_small, s, 4, 3, C_GREY);
}

/* the dialogue box: its rectangle for the given (whole) line */
static void dialogue_box(const char *s, int *x, int *y, int *w, int *h) {
  *w = gfx_text_w(&font_small, s) + 20;
  if (*w < 120) *w = 120;
  *h = gfx_lines(s) * (font_small.h + 3) + 9;
  *x = 160 - *w / 2;
  *y = 204 - *h / 2;
}

void ui_dialogue(const char *s, int len) {
  char buf[NP_TEXT_EXTRA ? 240 : 96];
  int n = 0;
  for (; s[n] && n < len && n < (int)sizeof buf - 1; n++) buf[n] = s[n];
  buf[n] = 0;
  /* the box is sized for the whole line so it does not grow while typing */
  int x, y, w, h;
  dialogue_box(s, &x, &y, &w, &h);
  gfx_fill(x, y, w, h, C_BLACK);
  gfx_text_c(&font_small, buf, 160, y + 6, C_WHITE);
}

void target_pos(int t, int *x, int *y) {
  if (t == 8) {
    const img_t *m = &art_img[IMG_T_GUN];
    *x = m->x + m->w / 2, *y = m->y + m->h / 2;
    return;
  }
  const int16_t *r = slot_rect[t >= 16][t & 7];
  *x = r[0] + r[2] / 2, *y = r[1] + r[3] / 2;
}

/* The screen box of a table slot, from the item that sits in it. */
static void slot_box(int side, int g, int *x, int *y, int *w, int *h) {
  int it = G.items[side][g];
  if (it < 0 && !side && T.held >= 0) it = T.held;
  const img_t *m;
  if (T.view == V_DEALER && side) {
    if (it < 0) {
      *x = dslot_rect[g][0], *y = dslot_rect[g][1], *w = dslot_rect[g][2], *h = dslot_rect[g][3];
      return;
    }
    m = &art_img[ditem_img(g, it)];
  } else {
    if (it < 0) {
      *x = slot_rect[side][g][0], *y = slot_rect[side][g][1], *w = slot_rect[side][g][2], *h = slot_rect[side][g][3];
      return;
    }
    m = &art_img[item_img(side, g, it)];
  }
  *x = m->x - 2, *y = m->y - 2, *w = m->w + 4, *h = m->h + 4;
}

/* The charges at a glance, top right: a small copy of the display (the
 * Dealer's bolts on the left, yours on the right), red once a wire is cut. */
static void draw_bolt(int x, int y, uint8_t c) {
  static const uint8_t B[10] = {0x07, 0x0E, 0x1C, 0x3F, 0x7E, 0x1C, 0x38, 0x70, 0x60, 0x40}; /* 7x10, bit 6 = left */
  for (int j = 0; j < 10; j++)
    for (int i = 0; i < 7; i++)
      if (B[j] >> (6 - i) & 1) gfx_fill(x + i, y + j, 1, 1, c);
}

static void draw_charges_hud(void) {
  if (T.no_charges || !G.maxhp) return;
  int n = G.maxhp < 2 ? 2 : G.maxhp, half = n * 8 + 4, w = half * 2 + 1, x = 316 - w, y = 3;
  gfx_fill(x - 1, y - 1, w + 2, 16, C_DARK);
  gfx_fill(x, y, w, 14, C_BLACK);
  gfx_fill(x + half, y + 1, 1, 12, C_DOT);
  for (int s = 0; s < 2; s++) {
    uint8_t c = G.wire[s] ? C_RED : C_DOT;
    int hp = G.hp[s] > 6 ? 6 : G.hp[s];
    for (int i = 0; i < hp; i++) {
      int bx = s ? x + 3 + i * 8 : x + w - 10 - i * 8; /* as on the display: his from the left, yours from the right */
      draw_bolt(bx, y + 2, c);
    }
  }
}

/* the table's charge display: both sides' bolts, lit */
static void draw_charges(void) {
  if (T.no_charges || !G.maxhp) return;
  gfx_sprite(IMG_C_FRAME, 0, 0);
  for (int s = 0; s < 2; s++) {
    if (G.wire[s] == 1) continue;
    for (int i = 0; i < G.hp[s] && i < 6; i++) gfx_sprite(IMG_C_SYM0 + (s ? 0 : 6) + i, 0, 0);
  }
}

static void draw_dealer_table(void) {
  if (T.d.gone) return;
  gfx_sprite_dim(T.d.head, 0, T.d.dy, T.d.dim);
  gfx_sprite_dim(T.d.hands, 0, 0, T.d.dim);
}

/* what lies still in a view, as a key for the background cache (gfx.c) */
static uint32_t still_key(void) {
  uint8_t k[32], n = 0;
  k[n++] = T.view;
  k[n++] = G.mode == MODE_DON || G.stage > 0;
  if (T.view == V_DEALER) {
    /* his view: his items and the shotgun too */
    for (int g = 0; g < 8; g++) k[n++] = (uint8_t)G.items[1][g];
    k[n++] = T.gun_on_table | G.sawed << 1;
  } else {
    /* the table: its charge display (the items are drawn over it each time) */
    k[n++] = T.no_charges;
    k[n++] = (uint8_t)G.hp[0], k[n++] = (uint8_t)G.hp[1], k[n++] = G.wire[0] | G.wire[1] << 2, k[n++] = (uint8_t)G.maxhp;
  }
  uint32_t h = 2166136261u;
  for (int i = 0; i < n; i++) h = (h ^ k[i]) * 16777619u;
  return h | 1;
}

/* the table and its charge display */
static void draw_table_still(void) {
  gfx_plate(IMG_PL_TABLE);
  if (G.mode == MODE_DON || G.stage > 0) gfx_sprite(IMG_T_GRID, 0, 0);
  draw_charges();
}

/* what lies on it, the Dealer behind */
static void draw_table(void) {
  draw_dealer_table();
  for (int side = 1; side >= 0; side--)
    for (int g = 0; g < 8; g++) {
      int it = G.items[side][g];
      if (it >= 0) gfx_sprite(item_img(side, g, it), 0, 0);
    }
  if (T.gun_on_table) gfx_sprite(G.sawed ? IMG_T_GUNSAW : IMG_T_GUN, 0, 0);
  if (G.pcuffed) gfx_sprite(IMG_T_PCUFF, 0, 0);
  if (T.box) gfx_sprite(IMG_T_BOX, 0, 0);
  if (T.held >= 0 && T.cursor >= 0 && T.cursor < 8) gfx_sprite(item_img(0, T.cursor, T.held), 0, -3);
  if (T.pose < 0) draw_charges_hud();
}

static void draw_dealer_still(void) {
  gfx_plate(IMG_PL_DEALER);
  if (G.mode == MODE_DON || G.stage > 0) gfx_sprite(IMG_D_GRID, 0, 0);
  for (int g = 0; g < 8; g++) {
    int it = G.items[1][g];
    if (it >= 0) gfx_sprite(ditem_img(g, it), 0, 0);
  }
  if (T.gun_on_table) gfx_sprite(G.sawed ? IMG_D_GUNSAW : IMG_D_GUN, 0, 0);
}

static void draw_dealer_view(void) {
  if (T.d.alone >= 0) {
    gfx_sprite(T.d.alone, 0, 0);
  } else if (!T.d.gone) {
    gfx_sprite_dim(T.d.head, 0, T.d.dy, T.d.dim);
    gfx_sprite_dim(T.d.hands, 0, 0, T.d.dim);
    gfx_sprite_dim(T.d.pose, 0, 0, T.d.dim);
  }
}

void table_draw(void) {
  uint32_t key = still_key();
  if (!gfx_cached(key)) {
    if (T.view == V_DEALER) draw_dealer_still();
    else draw_table_still();
    gfx_cache_store(key);
  }
  if (T.view == V_DEALER) draw_dealer_view();
  else draw_table();
  gfx_sprite(T.pose, 0, T.pose_dy);
  gfx_sprite(T.pose2, 0, 0);
  if (T.lbl) {
    /* the words on the table: "DEALER" across it, and "YOU" at your edge,
     * below the picture, written the same way */
    gfx_sprite_dim(IMG_P_LBL_DEALER, 0, 0, T.lbl == 1 ? 0 : 2);
    gfx_text_shadow(&font_big, T("YOU"), 160, 224, T.lbl == 2 ? C_WHITE : C_GREY);
  }
  if (T.cursor >= 0) {
    int x, y, w, h;
    if (T.cursor == 8) {
      const img_t *m = &art_img[G.sawed ? IMG_T_GUNSAW : IMG_T_GUN];
      x = m->x - 2, y = m->y - 2, w = m->w + 4, h = m->h + 4;
    } else {
      slot_box(T.cursor >= 16, T.cursor & 7, &x, &y, &w, &h);
    }
    gfx_brackets(x, y, w, h, C_WHITE);
  }
  if (T.title) {
    int lines = T.desc ? gfx_lines(T.desc) : 0;
    int y = 232 - lines * (font_small.h + 3) - font_big.h - 4;
    gfx_text_shadow(&font_big, T.title, 160, y, C_WHITE);
    if (T.desc) gfx_text_shadow(&font_small, T.desc, 160, y + font_big.h + 4, C_WHITE);
  }
  if (T.say) ui_dialogue(T.say, T.say_len);
  ui_hint(T.hint);
}

void table_show(void) {
  static uint8_t rushed;
  redraw = table_show;
  if (rushed != T.rush) gfx_light(256, T.rush ? 48 : 0, 0), rushed = T.rush;
  table_draw();
  gfx_present();
}

/* The Dealer breathes while he waits: a key, or -1 after each step. */
int idle_key(void) {
  static int k;
  int e = key_wait(380);
  if (e >= 0) return e;
  if (T.d.gone || T.d.alone >= 0) return -1;
  k = (k + 1) & 3;
  if (T.view == V_TABLE) {
    if (T.d.head < IMG_T_HEAD0 || T.d.head > IMG_T_HEAD3) return -1;
    T.d.head = IMG_T_HEAD0 + k;
  } else {
    if (T.d.head != IMG_D_HEAD || T.d.pose >= 0) return -1;
    static const int8_t dy[4] = {0, 1, 1, 0};
    T.d.dy = dy[k];
  }
  table_show();
  return -1;
}

/* ------------------------------------------------------------------ dialogue */

void say_on(const char *s) {
  int n = 0;
  while (s[n]) n++;
  /* the original types 50 characters a second: only the box is redrawn */
  T.say = s;
  T.say_len = 0;
  table_draw();
  gfx_present();
  int x, y, w, h;
  dialogue_box(s, &x, &y, &w, &h);
  uint64_t t0 = eadk_timing_millis();
  for (int k = 1; k <= n; k++) {
    int64_t wait = (int64_t)(t0 + 20 * k - eadk_timing_millis());
    int k2 = wait > 0 ? key_wait((int)wait) : -1;
    if (IS_OK(k2)) k = n; /* OK: the whole line at once */
    else if (k2 == KEY_BACK) app_pause_menu();
    if (s[k - 1] == ' ' || s[k - 1] == '\n') continue;
#if NP_TEXT_EXTRA
    if ((s[k] & 0xC0) == 0x80) continue; /* (in the middle of a letter) */
#endif
    T.say_len = k;
    ui_dialogue(s, k);
    gfx_present_rect(x, y, w, h);
  }
  T.say_len = n;
  redraw = table_show;
}

void say_off(void) {
  T.say = 0;
  table_show();
}

void say(const char *s, int ms) {
  say_on(s);
  pause_skip(ms);
  say_off();
}

/* ------------------------------------------------------------ charge display */

static void draw_name(void) {
  /* the player's name on the right of the display, in its dot-matrix font */
  int w = gfx_text_w(&font_dot, G.name);
  gfx_text(&font_dot, G.name, 283 - w, 90, C_DOT);
}

static void draw_side(int side, int hide_last) {
  int hp = G.hp[side];
  if (G.wire[side]) {
    gfx_sprite(side ? IMG_H_ERR_D : IMG_H_ERR_P, 0, 0);
    return;
  }
  for (int i = 1; i <= hp && i <= 6; i++) {
    if (hide_last && i == hp) continue;
    int skull = G.mode == MODE_STORY && G.stage == 2 && i <= 2;
    int img = skull ? IMG_H_SKULL0 + (side ? 0 : 2) + (i - 1) : IMG_H_SYM0 + (side ? 0 : 6) + (i - 1);
    gfx_sprite(img, 0, 0);
  }
}

/* the display's plate alone, and with its frame and your name (both kept
 * in the background cache while the display is on screen) */
static void health_plate(void) {
  if (!gfx_cached(0x48504C31u)) {
    gfx_plate(IMG_PL_HEALTH);
    gfx_cache_store(0x48504C31u);
  }
}

static void health_draw(int blink, int phase) {   /* blink: a bit a side */
  uint32_t key = 0x48465231u;
  for (const char *p = G.name; *p; p++) key = (key ^ (uint8_t)*p) * 16777619u;
  key |= 1;
  if (!gfx_cached(key)) {
    gfx_plate(IMG_PL_HEALTH);
    gfx_sprite(IMG_H_FRAME, 0, 0);
    draw_name();
    gfx_cache_store(key);
  }
  for (int s = 0; s < 2; s++) draw_side(s, (blink >> s & 1) && phase);
}

void health_view(int blink_side, int ms) {
  /* the last charge of someone at 1 flickers, like the original */
  int t = 0;
  while (t < ms) {
    int ph = (t / 150) & 1;
    int b = blink_side >= 0 ? 1 << blink_side : 0;
    if (blink_side < 0)
      for (int s = 0; s < 2; s++)
        if (G.hp[s] == 1 && !G.wire[s]) b |= 1 << s;   /* (both, when both are at 1) */
    health_draw(b, ph);
    gfx_present();
    int step = ms - t < 150 ? ms - t : 150;
    pause_ms(step);
    t += step;
  }
}

void health_bootup(void) {
  health_plate();
  gfx_present();
  pause_ms(500);
  gfx_sprite(IMG_H_FRAME, 0, 0);
  draw_name();
  gfx_present();
  pause_ms(850);
  health_view(-1, 900);
}

void round_indicator(void) {
  int don = G.mode == MODE_DON, k = 1 + G.stage;
  health_plate();
  gfx_present();
  pause_ms(800);
  for (int t = 0; t < 2000; t += 300) {
    health_plate();
    gfx_sprite(IMG_H_ROUND0, 0, 0);
    if (don) gfx_sprite(IMG_H_ENDLESS0, 0, 0);
    if ((t / 300) % 2 == 0) gfx_sprite((don ? IMG_H_ENDLESS0 : IMG_H_ROUND0) + k, 0, 0);
    gfx_present();
    pause_ms(300);
  }
  health_plate();
  gfx_present();
  pause_ms(300);
}

void health_wins(void) {
  char s[NP_TEXT_EXTRA ? 32 : 16];
  int n = 0;
  for (const char *p = G.name; *p; p++) s[n++] = *p;
  for (const char *p = T(" WINS!"); *p; p++) s[n++] = *p;
  s[n] = 0;
  health_plate();
  gfx_present();
  pause_ms(1000);
  health_plate();
  gfx_text(&font_dot, s, 160 - gfx_text_w(&font_dot, s) / 2, 104, C_DOT);
  gfx_present();
  pause_ms(2330);
  health_plate();
  gfx_present();
  pause_ms(300);
}

void wire_cut(int side) {
  /* the display of that side breaks: its defibrillator is gone */
  for (int k = 0; k < 6; k++) {
    health_draw(-1, 0);
    if (k & 1) gfx_sprite(side ? IMG_H_ERR_D : IMG_H_ERR_P, 0, 0);
    gfx_present();
    pause_ms(k & 1 ? 120 : 80);
  }
  health_view(-1, 800);
}

/* ------------------------------------------------------------------- shells */

void shells_view(const uint8_t *shown, int n, const char *text, int ms) {
  gfx_plate(IMG_PL_SHELLS);
  gfx_present();
  pause_ms(300);
  gfx_plate(IMG_PL_SHELLS);
  for (int i = 0; i < n; i++) gfx_sprite(shown[i] ? IMG_S_L0 + 2 * i : IMG_S_B0 + 2 * i, 0, 0);
  if (text) ui_dialogue(text, 999);
  gfx_present();
  pause_skip(ms);
  gfx_plate(IMG_PL_SHELLS);
  gfx_present();
  pause_ms(250);
}

/* ------------------------------------------------------------------ effects */

void fade_to(int from, int to, int ms) {
  int steps = ms / 40;
  if (steps < 1) steps = 1;
  for (int k = 1; k <= steps; k++) {
    gfx_light(from + (to - from) * k / steps, 0, 0);
    gfx_present();
    pause_ms(ms / steps);
  }
}

void flash(int live, int shake) {
  if (!live) return;
  static const int8_t sx[8] = {5, -6, 4, -3, 3, -2, 1, 0}, sy[8] = {-4, 3, -5, 2, -2, 2, -1, 0};
  for (int k = 0; k < 8; k++) {
    gfx_light(256, k < 3 ? 230 - k * 70 : 0, 0);
    if (shake) gfx_shake(sx[k], sy[k]);
    gfx_present();
    pause_ms(35);
  }
  gfx_shake(0, 0);
  gfx_light(256, 0, 0);
  gfx_present();
}
