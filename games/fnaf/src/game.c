/* The scenes around the nights: the title, the newspaper, how to play, the night's card, 6 AM, the
 * paychecks and the pink slip, the game over, the custom night and Golden Freddy; keys and saves. The TI-84 Plus
 * CE version (ce/) has drawings of its own: FNAF_CE leaves these out. */
#include <string.h>
#include "game.h"
#ifndef FNAF_CE
#include "gfx.h"
#include "res.h"
#else
#include "plat.h"
#endif

Game g;
static uint32_t seed = 0x2C1B3C6Du;

uint32_t rnd(void) {
  seed ^= seed << 13, seed ^= seed >> 17, seed ^= seed << 5;
  return seed;
}

static bool pressed(uint32_t k) { return (g.pressed & k) != 0; }

void caption(const char *s) {
  for (int i = 0; i < 4; i++)
    if (g.cap[i] == s && g.cap_t[i] > 1.2f) {   /* (the same again: it stays up) */
      g.cap_t[i] = 3;
      return;
    }
  memmove(g.cap, g.cap + 1, 3 * sizeof g.cap[0]);
  memmove(g.cap_t, g.cap_t + 1, 3 * sizeof g.cap_t[0]);
  g.cap[3] = s;
  g.cap_t[3] = 3;
}

static void go(int scene) {
  g.scene = scene;
  g.t = 0;
  g.menu = 0;
}

void start_night(int night) {
  g.n.night = (uint8_t)night;
  memset(g.cap, 0, sizeof g.cap);
  go(SC_INTRO);
}

void game_init(void) {
  seed ^= plat_millis() * 2654435761u;
  g.running = true;
  g.held = 0xFFFFFFFFu;   /* (the key that opened the app is not a press) */
  save_restore_copy();
  save_load();
  if (save_resume_load()) {   /* Home left a night: back in it, paused */
    go(SC_NIGHT);
    g.paused = 1;
  } else
    go(SC_TITLE);
}

/* ------------------------------------------------------------------ the title */
enum { M_NEW, M_CONTINUE, M_SIXTH, M_CUSTOM };

static int face;   /* the title: which of Freddy's faces, how bright */
static int face_light = 20;
static float face_t, light_t;

static void title_step(void) {
  /* Freddy's face twitches, and its light wavers */
  face_t += DT, light_t += DT;
  if (face_t >= 0.08f) face_t = 0, face = rnd() % 100 < 4 ? 1 + (int)(rnd() % 3) : 0;
  if (light_t >= 0.3f) light_t = 0, face_light = 22 + (int)(rnd() % 17);
  g.menu_n = (uint8_t)(2 + (g.stars & 1) + ((g.stars >> 1) & 1));
  if (pressed(K_UP | K_DIGIT(8))) g.menu = (uint8_t)((g.menu + g.menu_n - 1) % g.menu_n);
  if (pressed(K_DOWN | K_DIGIT(2))) g.menu = (uint8_t)((g.menu + 1) % g.menu_n);
  if (pressed(K_BACK)) g.running = false;
  if (!pressed(K_OK)) return;
  switch (g.menu) {
  case M_NEW:
    g.reached = 1;
    save_progress();
    g.n.night = 1;
    go(SC_NEWS);
    break;
  case M_CONTINUE: start_night(g.reached); break;
  case M_SIXTH: start_night(6); break;
  case M_CUSTOM: go(SC_CUSTOM); break;
  }
}

#ifndef FNAF_CE
static void title_draw(void) {
  gfx_fill(0, gfx_y0, SCREEN_W, BAND_H, BLACK);
  pic_draw(PIC_TITLE0 + face, 0, 0);
  gfx_dim(0, gfx_y0, SCREEN_W, BAND_H, face_light);
  gfx_static(0, gfx_y0, SCREEN_W, BAND_H, 7, rnd());
  /* the slow white band going down, and lines that flash */
  int sy = (int)(g.t / 20 * (SCREEN_H + 10)) % (SCREEN_H + 10) - 10;
  gfx_blend(0, sy, SCREEN_W, 10, WHITE, 3);
  if ((int)(g.t * 10) % 7 == 0) gfx_blend(0, (int)(g.t * 997) % SCREEN_H, SCREEN_W, 2, WHITE, 8);
  gfx_img(IMG_TITLE, 20, 18, WHITE);
  for (int i = 0; i < 3; i++)
    if (g.stars >> i & 1) gfx_img(IMG_STAR, 20 + 27 * i, 132, WHITE);
  static const char *const items[] = {"New Game", "Continue", "6th Night", "Custom Night"};
  int y0 = g.stars ? 160 : 150;
  for (int i = 0; i < g.menu_n; i++) {
    int y = y0 + 18 * i;
    gfx_text(FONT_BIG, 44, y, items[i], WHITE);
    if (g.menu == i) gfx_text(FONT_BIG, 20, y, ">>", WHITE);
  }
  if (g.menu == M_CONTINUE) {
    char s[] = "Night 0";
    s[6] = (char)('0' + g.reached);
    gfx_text(FONT_HUD, 44, y0 + 18 * g.menu_n + 4, s, WHITE);
  }
}
#endif

/* ------------------------------------------------------------------ the newspaper, how to play, the card */
#ifndef FNAF_CE
static int fade_in_out(float t, float in, float hold, float out) {   /* 0 black to 32 */
  float a = t < in ? t / in : t < hold ? 1 : t < hold + out ? 1 - (t - hold) / out : 0;
  return (int)(a * 32);
}
#endif

#ifndef FNAF_CE
static void news_draw(void) {
  pic_draw(PIC_NEWSPAPER, 0, 0);
  gfx_dim(0, gfx_y0, SCREEN_W, BAND_H, fade_in_out(g.t, 2, 7, 2));
}
#endif

static void news_step(void) {
  if (pressed(K_OK) && g.t > 2 && g.t < 7) g.t = 7;
  if (g.t >= 9) {
    if (!g.keys_seen) {
      go(SC_KEYS);
      g.after_keys = 0;
    } else
      start_night(1);
  }
}

#ifndef FNAF_CE
static void keys_draw(void) {
  static const char *const rows[][2] = {
    {"In the office", NULL},
    {"4 6, < >", "look left and right"},
    {"7   1", "left door, left light"},
    {"9   3", "right door, right light"},
    {"OK, 8", "the monitor up and down"},
    {"0", "mute the call"},
    {"On the monitor", NULL},
    {"1 to 7", "a camera (again: 1B, 1C...)"},
    {"arrows", "around the map"},
    {"Back", "pause"},
  };
  gfx_fill(0, gfx_y0, SCREEN_W, BAND_H, BLACK);
  gfx_text_c(FONT_BIG, 160, 8, "How to survive", WHITE);
  int y = 34;
  for (unsigned i = 0; i < sizeof rows / sizeof rows[0]; i++) {
    if (!rows[i][1]) {
      gfx_text(FONT_HUD, 16, y + 2, rows[i][0], RGB(0x8FD18F));
      y += 17;
      continue;
    }
    gfx_text(FONT_HUD, 26, y, rows[i][0], WHITE);
    gfx_text(FONT_TEXT, 108, y, rows[i][1], RGB(0xC8C8C8));
    y += 15;
  }
  gfx_text_c(FONT_TEXT, 160, 196, "Watch the power: every door, light and", RGB(0xC8C8C8));
  gfx_text_c(FONT_TEXT, 160, 209, "camera drains it. Survive until 6 AM.", RGB(0xC8C8C8));
  if ((int)(g.t * 2) & 1) gfx_text_c(FONT_HUD, 160, 226, "OK", WHITE);
}
#endif

static void keys_step(void) {
  if (!pressed(K_OK | K_BACK) || g.t < 0.4f) return;
  if (!g.keys_seen) {
    g.keys_seen = 1;
    save_progress();
  }
  if (g.after_keys) {
    g.scene = SC_NIGHT;
    g.paused = 1;
  } else
    start_night(g.n.night ? g.n.night : 1);
}

#ifndef FNAF_CE
static const char *const nth[] = {"", "1st Night", "2nd Night", "3rd Night", "4th Night", "5th Night", "6th Night",
                                  "7th Night"};
#endif

#ifndef FNAF_CE
static void intro_draw(void) {
  gfx_fill(0, gfx_y0, SCREEN_W, BAND_H, BLACK);
  gfx_text_c(FONT_HUGE, 160, 92, "12:00 AM", WHITE);
  gfx_text_c(FONT_BIG, 160, 124, nth[g.n.night], WHITE);
  if (g.t < 0.15f) gfx_static(0, gfx_y0, SCREEN_W, BAND_H, 20, rnd());
  if (g.t > 1.2f) gfx_dim(0, gfx_y0, SCREEN_W, BAND_H, (int)((2.2f - g.t) * 32));
}
#endif

static void intro_step(void) {
  if (g.t < 2.2f) return;
  night_begin(g.n.night);
  go(SC_NIGHT);
}

/* ------------------------------------------------------------------ the night, paused */
static void night_scene_step(void) {
  if (g.paused) {
    if (g.confirm) {
      if (pressed(K_LEFT | K_RIGHT | K_UP | K_DOWN | K_DIGIT(4) | K_DIGIT(6))) g.pause_item ^= 1;
      if (pressed(K_BACK)) g.confirm = 0;
      if (pressed(K_OK)) {
        if (g.pause_item) go(SC_TITLE), g.paused = 0;
        g.confirm = 0, g.pause_item = 2;
      }
      return;
    }
    if (pressed(K_UP | K_DIGIT(8))) g.pause_item = (uint8_t)((g.pause_item + 2) % 3);
    if (pressed(K_DOWN | K_DIGIT(2))) g.pause_item = (uint8_t)((g.pause_item + 1) % 3);
    if (pressed(K_BACK)) g.paused = 0;
    if (pressed(K_OK)) {
      if (g.pause_item == 0) g.paused = 0;
      else if (g.pause_item == 1) go(SC_KEYS), g.after_keys = 1;
      else g.confirm = 1, g.pause_item = 0;
    }
    return;
  }
  if (pressed(K_BACK)) {
    g.paused = 1;
    g.pause_item = 0;
    return;
  }
  night_step();
}

#ifndef FNAF_CE
static void pause_draw(void) {
  view_night();
  gfx_dim(0, gfx_y0, SCREEN_W, BAND_H, 9);
  if (g.confirm) {
    gfx_text_c(FONT_BIG, 160, 84, "Quit this night?", WHITE);
    gfx_text_c(FONT_TEXT, 160, 106, "It starts over next time.", RGB(0xB0B0B0));
    gfx_text(FONT_BIG, 104, 132, "No", WHITE);
    gfx_text(FONT_BIG, 190, 132, "Yes", WHITE);
    gfx_text(FONT_BIG, g.pause_item ? 168 : 82, 132, ">>", WHITE);
    return;
  }
  gfx_text_c(FONT_HUGE, 160, 62, "Paused", WHITE);
  static const char *const items[] = {"Resume", "How to play", "Quit night"};
  for (int i = 0; i < 3; i++) {
    gfx_text(FONT_BIG, 112, 104 + 20 * i, items[i], WHITE);
    if (g.pause_item == i) gfx_text(FONT_BIG, 88, 104 + 20 * i, ">>", WHITE);
  }
}
#endif

/* ------------------------------------------------------------------ 6 AM, the paychecks */
static void six_begin(void) {
  int n = g.n.night;
  if (n < 5 && g.reached < n + 1) g.reached = (uint8_t)(n + 1);
  if (n == 5) g.stars |= 1;
  if (n == 6) g.stars |= 2;
  if (n == 7 && g.n.ai[0] == 20 && g.n.ai[1] == 20 && g.n.ai[2] == 20 && g.n.ai[3] == 20) g.stars |= 4;
  save_progress();
  caption("*chimes*");
}

#ifndef FNAF_CE
static void six_draw(void) {
  gfx_fill(0, gfx_y0, SCREEN_W, BAND_H, BLACK);
  /* 5 rolls up into 6 */
  float r = g.t < 1 ? 0 : g.t > 6 ? 1 : (g.t - 1) / 5;
  int cy = 104, dh = 30, off = (int)(r * dh);
  if (gfx_y0 + BAND_H > cy - 2 && gfx_y0 < cy + dh) {
    gfx_text(FONT_HUGE, 118, cy - off, "5", WHITE);
    gfx_text(FONT_HUGE, 118, cy + dh - off, "6", WHITE);
    gfx_fill(110, cy - dh, 30, dh, BLACK);    /* (only the window between shows) */
    gfx_fill(110, cy + dh, 30, dh, BLACK);
  }
  gfx_text(FONT_HUGE, 148, cy, "AM", WHITE);
  gfx_text_c(FONT_TEXT, 160, 170, g.t > 6.2f ? "*children cheering*" : "*chimes*", RGB(0xA0A0A0));
  gfx_dim(0, gfx_y0, SCREEN_W, BAND_H, fade_in_out(g.t, 1, 10.5f, 1));
}
#endif

static void six_step(void) {
  if (g.t < DT * 1.5f) six_begin();
  if (pressed(K_OK) && g.t > 6.5f && g.t < 10.5f) g.t = 10.5f;
  if (g.t < 11.5f) return;
  int n = g.n.night;
  if (n < 5) start_night(n + 1);
  else {
    g.paper = (uint8_t)(n - 5);
    go(SC_PAPER);
  }
}

#ifndef FNAF_CE
static void paper_draw(void) {
  static const int pics[3] = {PIC_PAYCHECK, PIC_OVERTIME, PIC_PINKSLIP};
  pic_draw(pics[g.paper], 0, 0);
  gfx_dim(0, gfx_y0, SCREEN_W, BAND_H, fade_in_out(g.t, 2, 17, 2));
}
#endif

static void paper_step(void) {
  if (pressed(K_OK) && g.t > 2 && g.t < 17) g.t = 17;
  if (g.t >= 19) go(SC_TITLE);
}

/* ------------------------------------------------------------------ the end of a night */
#ifndef FNAF_CE
static void over_draw(void) {
  if (g.t < 2.5f) {
    gfx_fill(0, gfx_y0, SCREEN_W, BAND_H, BLACK);
    gfx_static(0, gfx_y0, SCREEN_W, BAND_H, 30, rnd());
    return;
  }
  pic_draw(PIC_GAMEOVER, 0, 0);
  gfx_static(0, gfx_y0, SCREEN_W, BAND_H, g.t < 3 ? 26 : 6, rnd());
  gfx_text_r(FONT_HUGE, 306, 200, "Game Over", WHITE);
}
#endif

static void over_step(void) {
  if ((pressed(K_OK) && g.t > 3) || g.t > 13) go(SC_TITLE);
}

#ifndef FNAF_CE
static void golden_draw(void) {
  pic_draw(PIC_GOLDEN, ((int)(g.t * 60) & 2) - 1, 0);
  gfx_static(0, gfx_y0, SCREEN_W, BAND_H, 6 + (int)(rnd() % 8), rnd());
}
#endif

static void golden_step(void) {
  if (g.t > 1.6f) g.running = false;   /* and the game is gone, as it crashes on a computer */
}

/* ------------------------------------------------------------------ the custom night */
#ifndef FNAF_CE
static const int faces[4] = {PIC_FACE_FREDDY, PIC_FACE_BONNIE, PIC_FACE_CHICA, PIC_FACE_FOXY};
static const char *const names[4] = {"Freddy", "Bonnie", "Chica", "Foxy"};
#endif

#ifndef FNAF_CE
static void custom_draw(void) {
  gfx_fill(0, gfx_y0, SCREEN_W, BAND_H, BLACK);
  gfx_text_c(FONT_BIG, 160, 14, "Customize Night", WHITE);
  for (int i = 0; i < 4; i++) {
    int x = 12 + 76 * i, y = 52;
    pic_part(faces[i], x, y, 0, 0, 64, 64);
    gfx_frame_rect(x - 2, y - 2, 68, 68, g.custom_sel == i ? WHITE : RGB(0x404040));
    gfx_text_c(FONT_HUD, x + 32, y + 72, names[i], WHITE);
    gfx_text_c(FONT_TEXT, x + 32, y + 88, "A.I. Level", RGB(0xB0B0B0));
    char s[3] = {(char)('0' + g.custom[i] / 10), (char)('0' + g.custom[i] % 10), 0};
    gfx_text_c(FONT_HUGE, x + 32, y + 104, g.custom[i] >= 10 ? s : s + 1, WHITE);
    if (g.custom_sel == i) {
      gfx_text(FONT_BIG, x + 2, y + 106, "<", WHITE);
      gfx_text(FONT_BIG, x + 52, y + 106, ">", WHITE);
    }
  }
  gfx_text(FONT_TEXT, 12, 214, "< > pick   up down: level", RGB(0x909090));
  gfx_text_r(FONT_BIG, 308, 208, "Ready", (int)(g.t * 2) & 1 ? WHITE : RGB(0xC0C0C0));
}
#endif

static void custom_step(void) {
  if (pressed(K_LEFT | K_DIGIT(4))) g.custom_sel = (uint8_t)((g.custom_sel + 3) % 4);
  if (pressed(K_RIGHT | K_DIGIT(6))) g.custom_sel = (uint8_t)((g.custom_sel + 1) % 4);
  int d = 0;
  uint32_t up = K_UP | K_DIGIT(8), down = K_DOWN | K_DIGIT(2);
  if (pressed(up)) d = 1, g.repeat = 0.35f;
  else if (pressed(down)) d = -1, g.repeat = 0.35f;
  else if (g.held & (up | down)) {   /* held: faster */
    g.repeat -= DT;
    if (g.repeat <= 0) g.repeat = 0.06f, d = g.held & up ? 1 : -1;
  }
  int8_t *v = &g.custom[g.custom_sel];
  *v = (int8_t)(*v + d < 0 ? 0 : *v + d > 20 ? 20 : *v + d);
  if (pressed(K_BACK)) {
    save_progress();
    go(SC_TITLE);
  }
  if (pressed(K_OK)) {
    save_progress();
    if (g.custom[0] == 1 && g.custom[1] == 9 && g.custom[2] == 8 && g.custom[3] == 7) go(SC_GOLDEN);
    else start_night(7);
  }
}

/* ------------------------------------------------------------------ steps and drawing */
void game_step(uint32_t keys) {
  g.pressed = keys & ~g.held;
  g.held = keys;
  for (int i = 0; i < 4; i++)
    if (g.cap_t[i] > 0) g.cap_t[i] -= DT;
  if (g.blip > 0) g.blip -= DT;
  if (keys & K_HOME) {   /* Home (or On/Off): a night is kept as it is */
    if (g.scene == SC_NIGHT) save_resume();
    g.running = false;
    return;
  }
  g.t += DT;
  switch (g.scene) {
  case SC_TITLE: title_step(); break;
  case SC_NEWS: news_step(); break;
  case SC_KEYS: keys_step(); break;
  case SC_INTRO: intro_step(); break;
  case SC_NIGHT: night_scene_step(); break;
  case SC_SCARE:
    g.scare_t += DT;
    if (g.scare_t > 0.95f) go(SC_OVER);
    break;
  case SC_SIX: six_step(); break;
  case SC_PAPER: paper_step(); break;
  case SC_OVER: over_step(); break;
  case SC_CUSTOM: custom_step(); break;
  case SC_GOLDEN: golden_step(); break;
  }
}

#ifndef FNAF_CE
void game_draw(void) {
  static void (*const draws[])(void) = {
    [SC_TITLE] = title_draw, [SC_NEWS] = news_draw, [SC_KEYS] = keys_draw, [SC_INTRO] = intro_draw,
    [SC_NIGHT] = view_night, [SC_SCARE] = view_scare, [SC_SIX] = six_draw, [SC_PAPER] = paper_draw,
    [SC_OVER] = over_draw, [SC_CUSTOM] = custom_draw, [SC_GOLDEN] = golden_draw};
  gfx_frame(g.scene == SC_NIGHT && g.paused ? pause_draw : draws[g.scene]);
}
#endif
