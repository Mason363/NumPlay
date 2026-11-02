/* Five Nights at Freddy's 4: the scenes around the nights, the keys and the saves.
 *
 * As the game: its warning, the title (New Game, Continue, the 6th night and Extras once the 5th is beaten, a star
 * for each of nights 5 to 8), the night's card, the night, 6 AM on the alarm clock, Fun with Plushtrap after the
 * first five nights, game over, the ending after the 6th night, the box after the Nightmare. */
#include <stdio.h>
#include <string.h>
#include "game.h"
#include "gfx.h"
#include "data.h"
#include "plat.h"

Game g;

static uint32_t seed = 0x4F4A2B1u;
uint32_t rnd(void) {
  seed = seed * 1664525u + 1013904223u;
  return seed >> 8;
}
uint32_t rnd_n(uint32_t n) { return n ? rnd() % n : 0; }

static bool pressed(uint32_t k) { return (g.pressed & k) != 0; }
#define OKS (K_OK | K_EXE)
#define UPS (K_UP | K_DIGIT(8))
#define DOWNS (K_DOWN | K_DIGIT(2))
#define GREY_TEXT RGB(0x9A9A9A)
#define MENU RGB(MENU_RED)
static void game_band(void);

void caption(const char *s) {
  for (int i = 0; i < 3; i++) g.cap[i] = g.cap[i + 1], g.cap_t[i] = g.cap_t[i + 1];
  g.cap[3] = s, g.cap_t[3] = 3;
}

static void go(int scene) { g.scene = scene, g.t = 0, g.menu = 0; }

void start_night(int night, int shadow) {
  night_begin(night, shadow);
  memset(g.cap, 0, sizeof g.cap);
  go(SC_CARD);
}

void game_init(void) {
  memset(&g, 0, sizeof g);
  g.running = true;
  seed ^= plat_millis() * 2654435761u;
  save_restore_copy();
  save_load();
  if (save_resume_load()) g.scene = SC_NIGHT, g.paused = 1;
}

/* ------------------------------------------------------------ the title */
enum { M_NEW, M_CONTINUE, M_SIXTH, M_EXTRA };
static int title_items(uint8_t *items) {
  int n = 0;
  items[n++] = M_NEW, items[n++] = M_CONTINUE;
  if (g.beat5) items[n++] = M_SIXTH, items[n++] = M_EXTRA;
  return n;
}
static int continue_night(void) { return g.level < 1 ? 1 : g.level > 5 ? 5 : g.level; }

static void title_step(void) {
  uint8_t items[4];
  int n = title_items(items);
  if (pressed(UPS)) g.menu = (uint8_t)((g.menu + n - 1) % n);
  if (pressed(DOWNS)) g.menu = (uint8_t)((g.menu + 1) % n);
  if (pressed(K_BACK)) g.running = false;
  if (!pressed(OKS)) return;
  switch (items[g.menu]) {
  case M_NEW: g.level = 1, save_progress(), start_night(1, 0); break;
  case M_CONTINUE: start_night(continue_night(), 0); break;
  case M_SIXTH: start_night(6, 0); break;
  case M_EXTRA: go(SC_EXTRAS), g.ex_sel = 0, g.ex_lr = 0; break;
  }
}

static void title_draw(void) {
  /* (the logo flickers: fainter now and then, and a glitch of 1 in 6 half-seconds) */
  int k = (int)(g.t * 2), r = (int)(rnd() % 20);
  int pic = (k * 7 + 3) % 6 == 0 ? PIC_TITLE_GLITCH1 + (k % 3) : r < 6 ? PIC_TITLE_FAINT : PIC_TITLE;
  pic_draw(pic, 0, 0);
  uint8_t items[4];
  int n = title_items(items);
  static const uint8_t ids[4] = {IMG_M_NEW, IMG_M_CONTINUE, IMG_M_SIXTH, IMG_M_EXTRA};
  for (int i = 0; i < n; i++) {
    int y = 104 + 23 * i;
    if (i == g.menu)
      gfx_img(IMG_REDLINE, -70, y + gfx_img_h(ids[items[i]]) / 2 - gfx_img_h(IMG_REDLINE) / 2, RGB((0x30 + rnd() % 0xA0) << 16));
    gfx_img(ids[items[i]], 90, y, MENU);
    if (items[i] == M_CONTINUE) {
      char t[12];
      snprintf(t, sizeof t, "Night %d", continue_night());
      gfx_text_s(FONT_HUD, 90 + gfx_img_w(IMG_M_CONTINUE) + 8, y, t, GREY_TEXT);
    }
  }
  uint8_t stars[4] = {g.beat5, g.beat6, g.beat7, g.beat8};
  for (int i = 0; i < 4; i++)
    if (stars[i]) gfx_img(IMG_STAR, 6 + 40 * i, SCREEN_H - 6 - gfx_img_h(IMG_STAR), RGB(0x8B0000));
}

/* ------------------------------------------------------------ extras */
enum { X_ANIMATRONICS, X_FOXY, X_FREDBEAR, X_JUMPSCARES, X_PLUSHTRAP, X_NIGHTMARE, X_EXIT };
static const uint8_t ex_ids[7] = {IMG_M_ANIMATRONICS, IMG_M_MAKINGFOXY, IMG_M_MAKINGFREDBEAR, IMG_M_JUMPSCARES,
                                  IMG_M_PLUSHTRAP, IMG_M_NIGHTMARE, IMG_M_EXIT};
static bool ex_open(int i) { return i <= X_FOXY || i == X_EXIT || g.beat6; }
static const char *const names[7] = {"Nightmare Freddy", "Nightmare Bonnie", "Nightmare Chica", "Nightmare Foxy",
                                     "Nightmare Fredbear", "Plushtrap", "Nightmare"};
static void extras_step(void) {
  int n = 7;
  if (pressed(UPS)) do g.ex_sel = (uint8_t)((g.ex_sel + n - 1) % n); while (!ex_open(g.ex_sel));
  if (pressed(DOWNS)) do g.ex_sel = (uint8_t)((g.ex_sel + 1) % n); while (!ex_open(g.ex_sel));
  if (pressed(UPS | DOWNS)) g.ex_lr = 0, g.t = 0;
  int max = g.ex_sel <= X_JUMPSCARES ? 6 : 0;
  if (pressed(K_LEFT | K_DIGIT(4)) && g.ex_lr > 0) g.ex_lr--, g.t = 0;
  if (pressed(K_RIGHT | K_DIGIT(6)) && g.ex_lr < max) g.ex_lr++, g.t = 0;
  if (pressed(K_BACK)) go(SC_TITLE);
  if (!pressed(OKS)) return;
  switch (g.ex_sel) {
  case X_ANIMATRONICS:
    if (g.ex_lr == 6 && g.beat7) g.twenty ^= 1;   /* (the game's double click on the Nightmare) */
    break;
  case X_PLUSHTRAP: plush_begin(true), go(SC_PLUSHCARD); break;
  case X_NIGHTMARE: start_night(g.twenty ? 8 : 7, g.twenty ? 2 : 1); break;
  case X_EXIT: go(SC_TITLE); break;
  }
}

static void extras_draw(void) {
  gfx_fill(0, 0, SCREEN_W, SCREEN_H, BLACK);
  for (int i = 0; i < 7; i++) {
    int y = 10 + 22 * i + (i == X_EXIT ? 14 : 0);
    int id = i == X_NIGHTMARE && g.twenty ? IMG_M_TWENTY : ex_ids[i];
    if (i == g.ex_sel) gfx_img(IMG_REDLINE, -150, y + gfx_img_h(id) / 2 - gfx_img_h(IMG_REDLINE) / 2, RGB(0xA00000));
    gfx_img(id, 8, y, ex_open(i) ? MENU : RGB(0x301010));
  }
  int cx = 230;
  int lr = g.ex_lr;
  switch (g.ex_sel) {
  case X_ANIMATRONICS:
    pic_draw(PIC_ANIMATRONIC0 + (lr == 6 && !g.beat7 ? 7 : lr), cx - 88, 4);
    gfx_text_c(FONT_TEXT, cx, 216, lr == 6 && !g.beat7 ? "???" : names[lr], WHITE);
    break;
  case X_FOXY: pic_draw(PIC_MAKINGFOXY0 + lr, cx - 88, 20); break;
  case X_FREDBEAR: pic_draw(PIC_MAKINGFREDBEAR0 + lr, cx - 88, 20); break;
  case X_JUMPSCARES: {
    int an = scare_anim(lr);
    int f = (int)(g.t * 60), len = an >= 0 ? anim_frames(an) : 1;
    int id = an < 0 ? PIC_NIGHTMARE : anim_pic(an, f >= len ? anim_len(an) - 1 : f * anim_len(an) / len);
    pic_part(id, cx - 88, 20, 72, 16, 176, 176);
    gfx_text_c(FONT_TEXT, cx, 216, names[lr], WHITE);
    break;
  }
  case X_PLUSHTRAP:
    pic_draw(PIC_PLUSHTRAP, cx - 88, 20);
    gfx_text_c(FONT_TEXT, cx, 210, "OK play", WHITE);
    break;
  case X_NIGHTMARE: gfx_text_c(FONT_TEXT, cx, 110, g.twenty ? "OK 20/20/20/20" : "OK the Nightmare", WHITE); break;
  }
  if (g.ex_sel <= X_JUMPSCARES) {
    int max = 6;
    if (lr > 0) gfx_text(FONT_BIG, cx - 100, 96, "<", MENU);
    if (lr < max) gfx_text(FONT_BIG, cx + 92, 96, ">", MENU);
  }
  const char *keys = g.ex_sel <= X_JUMPSCARES ? "Up Down choose   Left Right turn   Back title" : "Up Down choose   OK pick   Back title";
  gfx_text(FONT_HUD, 8, SCREEN_H - 13, keys, GREY_TEXT);
}

/* ------------------------------------------------------------ the night and around it */
static void captions_draw(void) {
  int y = SCREEN_H - 30;
  if (g.hold) gfx_text_s(FONT_TEXT, 8, y, g.hold, WHITE), y -= 13;
  for (int i = 3; i >= 0; i--) {
    if (!g.cap[i] || g.cap_t[i] <= 0) continue;
    gfx_text_s(FONT_TEXT, 8, y, g.cap[i], WHITE);
    y -= 13;
  }
}

static void pause_step(void) {
  if (g.confirm) {
    if (pressed(OKS)) {
      if (g.scene == SC_NIGHT) save_resume();
      go(SC_TITLE), g.paused = 0, g.confirm = 0;
    }
    if (pressed(K_BACK)) g.confirm = 0;
    return;
  }
  if (pressed(UPS | DOWNS)) g.pause_item ^= 1;
  if (pressed(K_BACK)) g.paused = 0;
  if (pressed(OKS)) {
    if (g.pause_item == 0) g.paused = 0;
    else g.confirm = 1;
  }
}

static void pause_draw(void) {
  gfx_dim(0, 60, SCREEN_W, 120, 22);
  if (g.confirm) {
    gfx_text_c(FONT_TEXT, SCREEN_W / 2, 90, "Quit the night?", WHITE);
    gfx_text_c(FONT_HUD, SCREEN_W / 2, 120, "OK quit   Back stay", WHITE);
    return;
  }
  gfx_text_c(FONT_BIG, SCREEN_W / 2, 74, "Paused", WHITE);
  gfx_text_c(FONT_TEXT, SCREEN_W / 2, 110, g.pause_item == 0 ? "> Resume <" : "Resume", WHITE);
  gfx_text_c(FONT_TEXT, SCREEN_W / 2, 134, g.pause_item == 1 ? "> Quit game <" : "Quit game", WHITE);
}

/* 6 AM (the "night win" frame): the night's star saved, the next night reached */
static void six_begin(void) {
  int night = g.n.night;
  g.level = (uint8_t)(night + 1);
  if (night == 5) g.beat5 = 1;
  if (night == 6) g.beat6 = 1;
  if (night == 7) g.beat7 = 1;
  if (night == 8) g.beat8 = 1;
  save_progress();
  go(SC_SIX);
}

static void six_done(void) {
  int night = g.n.night;
  if (night <= 5) plush_begin(false), go(SC_PLUSHCARD);
  else if (night == 6) g.text = 0, g.gone = 0, g.talk_t = 0, g.wait = 0, go(SC_END);
  else if (night == 7) g.wait = 0, go(SC_BOX);
  else go(SC_TITLE);
}

/* the next night after Fun with Plushtrap (its "night" was saved at 6 AM) */
static void after_plush(void) {
  if (g.p.from_extras) go(SC_EXTRAS);
  else start_night(g.level, 0);
}

/* the ending: the words come one at a time ("text counter", a second at a time while none shows), OK for the next
 * (as the game's touch, once the box is still); each answer takes a toy away */
static const char *const lines[14] = {NULL, "Can you hear me?", "I don't know if you can hear me.", "....I'm sorry.",
                                      NULL, "You're broken.", NULL, "We are still your friends.", NULL,
                                      "Do you still believe that?", NULL, "I'm still here.", NULL,
                                      "I will put you back together."};
static void end_step(void) {
  if (g.wait) g.wait--;
  if (g.text == 0) {
    int was = (int)g.talk_t;
    g.talk_t += 1.0f / 60;
    int s = (int)g.talk_t;
    static const uint8_t at[6] = {3, 6, 9, 12, 15, 18}, to[6] = {1, 5, 7, 9, 11, 13};
    for (int i = 0; i < 6; i++)
      if (s == at[i] && was != s) g.text = to[i], g.wait = 50;
    if (g.talk_t >= 30) go(SC_TITLE);
    return;
  }
  if (pressed(OKS) && !g.wait) {
    g.text++, g.wait = 50;
    if (g.text == 4) g.text = 0;
    else if (g.text >= 6 && g.text % 2 == 0) g.text = 0, g.gone++;
  }
}
static void end_draw(void) {
  pic_draw(g.talk_t >= 25 ? PIC_ENDING7 : g.talk_t >= 21 ? PIC_ENDING6 : PIC_ENDING0 + g.gone, 0, 0);
  const char *l = g.text < 14 ? lines[g.text] : NULL;
  if (l) {
    gfx_fill(0, 0, SCREEN_W, 64, RGB(0x05050A));
    gfx_text_c(FONT_SERIF, SCREEN_W / 2, 22, l, g.text <= 3 ? RGB(0x787878) : RGB(0xFFFFA0));
    if (!g.wait && (int)(g.t * 4) % 2) gfx_img(IMG_ARROW, SCREEN_W - 26, 44, WHITE);
  }
}

void game_step(uint32_t keys) {
  g.pressed = keys & ~g.held;
  g.held = keys;
  if (keys & K_HOME) {
    if (g.scene == SC_NIGHT && !g.n.gameover) save_resume();
    g.running = false;
    return;
  }
  for (int i = 0; i < 4; i++)
    if (g.cap_t[i] > 0) g.cap_t[i] -= 1.0f / 60;
  switch (g.scene) {
  case SC_WARN:
    if (g.t > 2 || pressed(OKS | K_BACK)) go(SC_TITLE);
    break;
  case SC_TITLE: title_step(); break;
  case SC_EXTRAS: extras_step(); break;
  case SC_CARD:
    if (g.t > 2.1f) go(SC_NIGHT);
    break;
  case SC_NIGHT: {
    if (g.paused) {
      pause_step();
      break;
    }
    if (pressed(K_BACK)) {
      g.paused = 1, g.pause_item = 0;
      break;
    }
    night_step();
    Night *n = &g.n;
    int top = night_top();
    bool fredbear = top == O_FREDBEAR;
    if (fredbear && n->shadow) go(SC_NIGHTMARE);                                      /* 413 */
    else if (top >= O_JUMP && n->ob[top].done) go(SC_OVER);                          /* 193 to 196, 277, 289, 290, 359 */
    else if (n->hour == 6) six_begin();                                               /* 370 */
    break;
  }
  case SC_OVER:
    if (g.t > 7 || (g.t > 1 && pressed(OKS | K_BACK))) go(SC_TITLE);
    break;
  case SC_NIGHTMARE:
    if (g.t > 5) go(SC_WARN);
    break;
  case SC_SIX:
    if (g.t > 10 || (g.t > 4 && pressed(OKS))) six_done();
    break;
  case SC_PLUSHCARD:
    if (g.t > 4 || (g.t > 1 && pressed(OKS))) go(SC_PLUSH);
    break;
  case SC_PLUSH:
    if (g.paused) {
      pause_step();
      break;
    }
    if (pressed(K_BACK)) {
      g.paused = 1, g.pause_item = 0;
      break;
    }
    plush_step();
    if (plush_done() == 1) after_plush();
    if (plush_done() == 2) go(SC_TOOBAD);
    break;
  case SC_TOOBAD:
    if (g.t > 6 || (g.t > 1 && pressed(OKS))) after_plush();
    break;
  case SC_END: end_step(); break;
  case SC_BOX:
    if (pressed(OKS) && !g.wait) g.wait = RATTLE_N * 2;   /* (still locked: the locks shake) */
    if (g.wait) g.wait--;
    if (g.t > 20) go(SC_TITLE);
    break;
  }
  g.t += 1.0f / 60;
}

void game_draw(void) { gfx_frame(game_band); }

static void key_line(const char *s) {
  gfx_dim(0, SCREEN_H - 14, SCREEN_W, 14, 20);
  gfx_text(FONT_HUD, 6, SCREEN_H - 13, s, WHITE);
}

/* (gfx_frame calls this for each band) */
static void game_band(void) {
  switch (g.scene) {
  case SC_WARN:
    gfx_fill(0, 0, SCREEN_W, SCREEN_H, BLACK);
    gfx_text_c(FONT_SERIF, SCREEN_W / 2, 96, "WARNING!", RGB(0xC00000));
    gfx_text_c(FONT_TEXT, SCREEN_W / 2, 122, "This game contains flashing lights, loud noises,", RGB(0xC00000));
    gfx_text_c(FONT_TEXT, SCREEN_W / 2, 136, "and lots of jumpscares!", RGB(0xC00000));
    break;
  case SC_TITLE: title_draw(); break;
  case SC_EXTRAS: extras_draw(); break;
  case SC_CARD: {
    pic_draw(PIC_WHATNIGHT, 0, 0);
    gfx_fill(0, 13, 170, 38, BLACK);
    gfx_img(IMG_NIGHTWORD, 12, 16, WHITE);
    char t[4];
    snprintf(t, sizeof t, "%d", g.n.night);
    gfx_text(FONT_HUGE, 16 + gfx_img_w(IMG_NIGHTWORD), 6, t, WHITE);
    if (g.t > 2) gfx_img(IMG_CLOCK, SCREEN_W - 26, SCREEN_H - 26, WHITE);
    break;
  }
  case SC_NIGHT:
    view_night();
    captions_draw();
    if (night_top() < O_JUMP) {
      const char *k = night_keys();
      if (k) key_line(k);
    }
    if (g.paused) pause_draw();
    break;
  case SC_OVER: pic_draw(PIC_GAMEOVER, 0, 0); break;
  case SC_NIGHTMARE:
    pic_draw(PIC_NIGHTMARE, 0, 0);
    gfx_static(0, 0, SCREEN_W, SCREEN_H, 6 + (int)rnd_n(8), (uint32_t)(g.t * 60));
    break;
  case SC_SIX: {
    /* the alarm clock: each digit spins until it stops (2, 2.5, 3, 3.5 s) on 06:00, which blinks from 4 s */
    gfx_fill(0, 0, SCREEN_W, SCREEN_H, BLACK);
    static const uint8_t final[4] = {0, 6, 0, 0};
    static const int16_t cx[4] = {80, 129, 195, 244};
    bool shown = g.t < 4 || (int)((g.t - 4) / 0.24f) % 2 == 0;
    for (int i = 0; i < 4 && shown; i++) {
      int d = g.t >= 2 + 0.5f * i ? final[i] : (int)((rnd() >> 4) % 10);
      int id = IMG_DIGIT0 + d;
      gfx_img(id, cx[i] - gfx_img_w(id) / 2, 120 - gfx_img_h(id) / 2, RGB(CLOCK_RED));
    }
    if (shown) gfx_img(IMG_COLON, 164 - gfx_img_w(IMG_COLON) / 2, 120 - gfx_img_h(IMG_COLON) / 2, RGB(CLOCK_RED));
    break;
  }
  case SC_PLUSHCARD:
    pic_draw(PIC_PLUSHCARD, 0, 0);
    gfx_fill(0, 176, SCREEN_W, 64, BLACK);
    gfx_text_c(FONT_TEXT, SCREEN_W / 2, 186, "Hold OK for the flashlight.", WHITE);
    gfx_text_c(FONT_TEXT, SCREEN_W / 2, 202, "Stop Plushtrap on the X with it", WHITE);
    gfx_text_c(FONT_TEXT, SCREEN_W / 2, 216, "to skip 2 hours of the next night.", WHITE);
    break;
  case SC_PLUSH:
    view_plush();
    captions_draw();
    key_line("OK light   Back pause");
    if (g.paused) pause_draw();
    break;
  case SC_TOOBAD: pic_draw(PIC_TOOBAD, 0, 0); break;
  case SC_END: end_draw(); break;
  case SC_BOX: {
    int k = g.wait ? (RATTLE_N * 2 - g.wait) / 2 : 0;
    pic_draw(k > 0 && k < RATTLE_N ? PIC_LOCKBOX1 + k - 1 : PIC_LOCKBOX, 0, 0);
    if (g.t > 15) {
      int a = (int)((g.t - 15) / 0.06f);
      a = a > 75 ? 75 : a;
      int v = 0xFF * a / 255;
      gfx_img(IMG_FORGOTTEN, SCREEN_W / 2 - gfx_img_w(IMG_FORGOTTEN) / 2, 62 - gfx_img_h(IMG_FORGOTTEN) / 2,
              (uint16_t)RGB(v << 16 | v << 8 | v));
    }
    if (g.t < 15) key_line("OK the locks");
    break;
  }
  }
}
